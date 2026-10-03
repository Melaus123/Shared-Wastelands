// enet_transport.cpp - ITransport over ENet (direct IP / LAN backend).
//
// One of TWO equal backends (user amendment: the player picks; no default). Ships first
// only because it is testable on a single machine via loopback with the two-instance
// harness (F018).
//
// Wire format: a 5-byte header then payload.
//   [0]     msg type (u8)
//   [1..4]  payload length (u32, little-endian - both peers are x64 Windows, and the
//           version handshake refuses mismatched builds, so no byte-order dance)
// ENet already frames packets, so the length is a redundancy check that catches a
// truncated/foreign packet before it reaches the replication layer.

#include "transport.h"

#include "../coop_log.h"

#define ENET_IMPLEMENTATION_NOT_USED  // (ENet has no header-only mode; sources are compiled separately)
#include "../third_party/enet/include/enet/enet.h"
#include "../../common/sendbound.h"   /* M16 (T-197): what waits to be sent on this link is bounded */
#include "../../common/sendbundle.h"   /* M13: what is handed to ENet for the world server during one pass leaves as one packet per lane */

#include <sstream>
#include <locale>
#include <cstring>

namespace coop {
namespace net {

namespace {

const unsigned char kHeaderBytes = 5;
typedef char M13FrameHeaderAgrees[((size_t)kHeaderBytes == coopbundle::kFrameHeader) ? 1 : -1];
/* M13: sendbundle.h's size budget is ENet's own fragment threshold - its copies of ENet's wire sizes must be these. */
typedef char M13EnetSizesAgree[(sizeof(ENetProtocolHeader) == coopbundle::kEnetProtocolHeader && sizeof(ENetProtocolSendFragment) == coopbundle::kEnetSendFragment
    && sizeof(ENetProtocolSendReliable) == coopbundle::kEnetSendReliable && sizeof(ENetProtocolSendUnsequenced) == coopbundle::kEnetSendUnsequenced
    && sizeof(ENetProtocolAcknowledge) == coopbundle::kEnetAcknowledge && (unsigned int)ENET_PROTOCOL_MINIMUM_MTU == coopbundle::kEnetMinimumMtu
    && (unsigned int)ENET_PROTOCOL_MAXIMUM_MTU == coopbundle::kEnetMaximumMtu) ? 1 : -1];

/* M16 (T-197): THIS LINK'S NETWORK QUEUE, counted by ENet's own packet free callback - a packet is freed when it is
   on the wire (unreliable) or acknowledged (reliable), so count/bytes are what ENet still holds for this link. */
struct SbNetQ { long long count, bytes; SbNetQ() : count(0), bytes(0) {} };
void SbOnPacketFree(ENetPacket* p)
{
    SbNetQ* q = (SbNetQ*)p->userData;
    if (q != 0) { --q->count; q->bytes -= (long long)p->dataLength; }
}

std::string N(long long v)   // RE_Kenshi's locale mangles raw numbers (F030)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

class EnetTransport : public ITransport
{
public:
    EnetTransport()
        : host_(0), peer_(0), state_(LINK_DOWN), sent_(0), recv_(0),
          nextPeerId_(1), hosting_(false), connectGen_(0), tLimit_(0), tMinMs_(0), tMaxMs_(0),
          sbDirect_(0), sbWaited_(0), sbCoalesced_(0), sbEpisodes_(0), sbDropped_(0), sbSendFailed_(0), sbWaitMax_(0), liveDir_(coopsb::kDirNoLive),
          bundle_(false), bnSendFailed_(0), bnRecvBundles_(0), bnRecvBundled_(0), bnRecvBad_(0), bnLinksOn_(0), udpPackets_(0), udpBytes_(0)
    {
    }

    ~EnetTransport()
    {
        Disconnect();
    }

    bool Host(unsigned short port, std::string* errOut)
    {
        if (!EnsureEnet(errOut)) return false;
        Disconnect();

        ENetAddress addr;
        addr.host = ENET_HOST_ANY;
        addr.port = port;

        // 1 peer for the PoC (2-player). Raising this is a constant, not a redesign.
        host_ = enet_host_create(&addr, 1, 2, 0, 0);
        if (host_ == 0)
        {
            if (errOut) *errOut = "enet_host_create failed (port busy?)";
            state_ = LINK_FAILED;
            return false;
        }
        state_    = LINK_LISTENING;
        hosting_  = true;
        DebugLog("[net] hosting on port " + N(port) + " (direct/ENet)");
        return true;
    }

    bool Join(const std::string& address, unsigned short port, std::string* errOut)
    {
        if (!EnsureEnet(errOut)) return false;
        Disconnect();

        host_ = enet_host_create(0, 1, 2, 0, 0);
        if (host_ == 0)
        {
            if (errOut) *errOut = "enet_host_create (client) failed";
            state_ = LINK_FAILED;
            return false;
        }

        ENetAddress addr;
        if (enet_address_set_host(&addr, address.c_str()) != 0)
        {
            if (errOut) *errOut = "cannot resolve address '" + address + "'";
            enet_host_destroy(host_); host_ = 0;
            state_ = LINK_FAILED;
            return false;
        }
        addr.port = port;

        peer_ = enet_host_connect(host_, &addr, 2, 0);
        if (peer_ == 0)
        {
            if (errOut) *errOut = "no available peers for connection";
            enet_host_destroy(host_); host_ = 0;
            state_ = LINK_FAILED;
            return false;
        }
        state_   = LINK_CONNECTING;
        hosting_ = false;
        DebugLog("[net] connecting to " + address + ":" + N(port) + " (direct/ENet)");
        return true;
    }

    void Disconnect()
    {
        if (peer_ != 0)
        {
            // F069: disconnect_now() DISCARDS the outgoing queue, so a BYE queued just
            // before this never left the machine - a graceful leave was indistinguishable
            // from a crash. Flush what is queued, then disconnect gracefully and give the
            // handshake a brief window; fall back to the abrupt path if it does not land.
            /* mmo5 (T435): NOT enet_peer_disconnect. That queues the DISCONNECT at once, and the far end's
               enet_protocol_handle_disconnect (protocol.c:827) resets ITS queues - throwing away the reliable
               packets it had received but not yet handed to the game (T435: B got neither SESSION_CLOSING nor
               the BYE). disconnect_later sends the DISCONNECT only after every queued reliable command is
               acknowledged (protocol.c:190), so the far end's game has them first. Host and joiner alike. */
            { coopsb::Item x; while (box_.TakeAny(&x)) { if (!SendNow((Channel)x.channel, x.bytes)) ++sbSendFailed_; } }   /* M16: a graceful close hands every waiting message to ENet first */
            SbEdgeLog();   /* M16 (T742): an episode open at the close ends here - its end line is written, not reset silently */
            BundleSealAll();   /* M13: the open bundles go to ENet with everything else before the flush */
            enet_host_flush(host_);
            enet_peer_disconnect_later(peer_, 0);

            ENetEvent ev;
            bool closed = false;
            // ~300 ms total: long enough for a LAN/loopback round trip, short enough that
            // quitting never feels hung. This runs on the main thread, so it is capped.
            for (int i = 0; i < 6 && !closed; ++i)
            {
                while (enet_host_service(host_, &ev, 50) > 0)
                {
                    if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { closed = true; break; }
                    if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
                }
            }
            DebugLog(std::string("[net] disconnect: ") + (closed ? "graceful (the far end acknowledged every queued reliable"
                     " message, then the DISCONNECT)" : "NOT confirmed within ~300 ms - the peer is reset (the far end learns by"
                     " timeout; messages it had not acknowledged may be lost)") + " (mmo5: disconnect_later)");
            if (!closed)
                enet_peer_reset(peer_);
            peer_ = 0;
        }
        if (host_ != 0)
        {
            UdpHarvest();   /* M13: this host's datagram counts are kept for the REPORT */
            enet_host_destroy(host_);
            host_ = 0;
        }
        SbLinkGone("the link was closed");
        state_ = LINK_DOWN;
    }

    /* B12-f (review-b12e H-2): THE ABRUPT CLOSE, with no service wait. enet_peer_reset drops the peer
       locally and silently - the far end learns of it by timeout, which is the right trade for a peer that
       never finished connecting or has stopped answering. Disconnect() above stays the graceful path and is
       what an UP link still gets. Idempotent, and the destructor's Disconnect() after this one is a no-op
       because both pointers are already 0. */
    void Abort()
    {
        if (peer_ != 0) { enet_peer_reset(peer_); peer_ = 0; }
        if (host_ != 0) { UdpHarvest(); enet_host_destroy(host_); host_ = 0; }
        SbLinkGone("the link was aborted");
        state_ = LINK_DOWN;
    }

    // Push queued packets out immediately. Needed because a BYE sent in the same tick as
    // a disconnect would otherwise still be sitting in the queue (F069).
    void Flush()
    {
        SbDrain();   /* M16: what waits goes to ENet first, while its queue has room */
        BundleSealAll();   /* M13: and the open bundles, before ENet transmits */
        if (host_) enet_host_flush(host_);
    }

    void Poll(std::vector<Message>* out)
    {
        if (host_ == 0) return;

        /* M13: everything handed over since the last pass leaves now, one packet per lane - the first enet_host_service
           below is where ENet transmits (it held every packet until then anyway), so bundling adds no delay. */
        BundleSealAll();
        ENetEvent ev;
        // 0 timeout: never block the game's main thread.
        while (enet_host_service(host_, &ev, 0) > 0)
        {
            switch (ev.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                peer_  = ev.peer;
                state_ = LINK_UP;
                ++connectGen_;   /* P7v: the EDGE, countable even when a DISCONNECT and a CONNECT land in one Poll */
                bundle_ = false;   /* M13: a new connection bundles only after its own WELCOME */
                // The HOST is always peer 0, everywhere. T019 found the client labelling
                // the host as peer 1 - the same id the client itself holds - so an
                // inbound message appeared to come from the receiver. Ownership records
                // keyed on that id would have been silently wrong.
                ev.peer->data = hosting_ ? (void*)(size_t)(nextPeerId_++)
                                         : (void*)(size_t)0;
                DebugLog("[net] peer connected (link UP), remote peer id "
                         + N((long long)(size_t)ev.peer->data));
                if (tLimit_ != 0 || tMinMs_ != 0 || tMaxMs_ != 0)
                {
                    /* link1 (T416): the game link outlasts a world load - see src/common/gamelink.h */
                    enet_peer_timeout(ev.peer, tLimit_, tMinMs_, tMaxMs_);
                    DebugLog("[net] link1: peer timeouts set - limit " + N(tLimit_) + ", minimum " + N(tMinMs_)
                             + " ms, maximum " + N(tMaxMs_) + " ms (a world load stalls this socket; ENet's 5 s default dropped T416's link)");
                }
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                DebugLog("[net] peer disconnected (link DOWN)");
                peer_  = 0;
                state_ = LINK_DOWN;
                ++connectGen_;   /* P7v: the other half of the same edge counter */
                SbLinkGone("the link went down");   /* M16 */
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                Decode(&ev, out);
                enet_packet_destroy(ev.packet);
                break;

            default:
                break;
            }
        }
        SbDrain();   /* M16: what waits goes out as ENet's queue drains */
    }

    bool Send(unsigned int /*peer*/, MsgType type, const char* data, size_t len, Channel ch)
    {
        if (host_ == 0 || peer_ == 0 || state_ != LINK_UP) return false;

        std::vector<char> buf(kHeaderBytes + len);
        buf[0] = (char)(unsigned char)type;
        unsigned int n = (unsigned int)len;
        std::memcpy(&buf[1], &n, 4);
        if (len) std::memcpy(&buf[kHeaderBytes], data, len);

        enet_uint32 flags = (ch == CH_RELIABLE)
            ? ENET_PACKET_FLAG_RELIABLE
            : ENET_PACKET_FLAG_UNSEQUENCED;   // MOVE is latest-wins; ordering costs latency

        (void)flags;
        /* M16 (T-197): while ENet's queue for this link is under the bound, a reliable message goes straight out when
           nothing waits, and an unreliable MOVE or STATE goes straight out past whatever waits (it promises no order);
           otherwise the message WAITS here (a MOVE or STATE keeps only the newest per character, everything else is never
           dropped) and goes out from Poll/Flush - sendbound.h. true = sent or waiting, as 'queued' always meant here. */
        coopsb::Item it; it.type = (unsigned int)type; it.channel = (int)ch;
        it.cls = coopsb::ClassOf((unsigned int)type, data, len, liveDir_, &it.key);   /* M16 fold 2: by link */
        bool superseded = false;
        if (box_.Direct(NetCount(), NetBytes(), it, &superseded))
        {
            if (superseded) ++sbCoalesced_;   /* an older waiting value of this character was removed: this one replaces it */
            if (!SendNow(ch, buf)) return false;
            ++sbDirect_;
            return true;
        }
        it.bytes.swap(buf);
        if (box_.Put(it)) ++sbCoalesced_;
        ++sbWaited_;
        if (box_.Count() > sbWaitMax_) sbWaitMax_ = box_.Count();
        SbEdgeLog();
        return true;
    }

    /* M13: one whole frame toward ENet. While bundling is on (the world-server link after a WELCOME of the same protocol)
       it joins its lane's open bundle, which leaves when the bundles are sealed right before ENet transmits (Poll, Flush,
       Disconnect); a bundle with no room left, and a frame too large to share a packet, go to ENet at once - the open
       bundle first, so the guaranteed lane keeps its order. false = ENet refused the frame itself; a refused bundle is
       counted bundle[sendFailed] (src/common/sendbundle.h). */
    bool SendNow(Channel ch, const std::vector<char>& buf)
    {
        if (host_ == 0 || peer_ == 0 || buf.empty()) return false;
        if (!bundle_) return SendPacket(ch, &buf[0], buf.size());
        std::vector<char> first; size_t firstEntries = 0;
        const int r = col_.Offer(coopbundle::LaneOf((int)ch), &buf[0], buf.size(), coopbundle::Budget(peer_->mtu, host_->checksum != 0), &first, &firstEntries, &bnTally_);
        if (firstEntries != 0) BundleSend(ch, first, firstEntries);
        if (r == coopbundle::kAlone) return SendPacket(ch, &buf[0], buf.size());
        return true;
    }
    /* M13: a sealed bundle (or the one message an open bundle held) to ENet; a refusal loses every message in it - counted
       and logged (the first 5 and every 100th). */
    void BundleSend(Channel ch, const std::vector<char>& frame, size_t entries)
    {
        if (!frame.empty() && SendPacket(ch, &frame[0], frame.size())) return;
        bnSendFailed_ += (long long)entries;
        if (coopsbLogThis(bnSendFailed_))
            ErrorLog("[net] M13: ENet REFUSED a bundle of " + N((long long)entries) + " message(s), " + N((long long)frame.size()) + " bytes, on the "
                     + std::string(ch == CH_RELIABLE ? "guaranteed" : "unreliable") + " lane of the " + SbLinkName() + " - they are lost (counted bundle[sendFailed])");
    }
    /* M13: both lanes' open bundles to ENet, guaranteed lane first. */
    void BundleSealAll()
    {
        if (col_.OpenEntries() == 0) return;
        for (int lane = 0; lane < coopbundle::kLanes; ++lane)
        {
            std::vector<char> f;
            const size_t n = col_.Seal(lane, &f, &bnTally_);
            if (n != 0) BundleSend(lane == coopbundle::kLaneReliable ? CH_RELIABLE : CH_UNRELIABLE, f, n);
        }
    }
    /* M16 + M13: what ENet holds for this link, plus the open bundles it is about to get - what the send bound weighs. */
    long long NetCount() const { return netq_.count + (long long)col_.OpenPackets(); }
    long long NetBytes() const { return netq_.bytes + (long long)col_.OpenBytes(); }
    /* M16: one whole frame (a message or a bundle) to ENet as one packet, counted in netq_ until ENet frees it. */
    bool SendPacket(Channel ch, const char* data, size_t len)
    {
        if (host_ == 0 || peer_ == 0 || data == 0 || len == 0) return false;
        const enet_uint32 flags = (ch == CH_RELIABLE) ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;   // MOVE is latest-wins; ordering costs latency
        ENetPacket* pkt = enet_packet_create(data, len, flags);
        if (pkt == 0) return false;
        pkt->userData = &netq_; pkt->freeCallback = &SbOnPacketFree;
        ++netq_.count; netq_.bytes += (long long)len;
        if (enet_peer_send(peer_, (enet_uint8)ch, pkt) != 0)
        {
            enet_packet_destroy(pkt);   /* the free callback takes it back off netq_ */
            return false;
        }
        ++sent_;
        return true;
    }
    void SbDrain()
    {
        if (host_ == 0 || peer_ == 0 || state_ != LINK_UP) return;
        coopsb::Item x;
        while (box_.Take(NetCount(), NetBytes(), &x)) { if (!SendNow((Channel)x.channel, x.bytes)) ++sbSendFailed_; }
        SbEdgeLog();
    }
    void SbEdgeLog()
    {
        const int e = box_.Edge();
        if (e > 0 && coopsbLogThis(++sbEpisodes_))
        {
            /* T742: one message bigger than the byte bound (a zone record over 1 MB) fills ENet's queue on its own - said so */
            const std::string held = (netq_.count == 1 && netq_.bytes > coopsb::kNetBytesMax)
                ? "one large message (" + N(netq_.bytes) + " bytes) is going out in ENet"
                : N(netq_.count) + " packet(s) / " + N(netq_.bytes) + " bytes wait in ENet";
            DebugLog(std::string("[net] M16: the ") + SbLinkName() + " is CONGESTED - " + held + " (full at "
                     + N(coopsb::kNetCountMax) + " / " + N(coopsb::kNetBytesMax) + "). Reliable messages WAIT in order; an unreliable MOVE or STATE passes them and waits only while ENet's"
                     " queue is full, newest per character; nothing reliable is dropped (episode " + N(sbEpisodes_) + "; the first 5 and every 100th are logged)");
        }
        else if (e < 0 && coopsbLogThis(sbEpisodes_))
            DebugLog(std::string("[net] M16: the ") + SbLinkName() + " is no longer congested - every waiting message has gone to ENet (episode " + N(sbEpisodes_) + ")");
        if (box_.GrowthMark())
            DebugLog("[net] M16: " + N(box_.Count()) + " message(s), " + N(box_.Bytes()) + " bytes WAIT on the " + SbLinkName() + " and it is still growing - none is dropped");
    }
    /* T742: which link a line is about - liveDir_ is kDirUp only on the world-server link (SetStoreLink) */
    const char* SbLinkName() const { return liveDir_ == coopsb::kDirUp ? "world-server link" : "session link"; }
    static bool coopsbLogThis(long long n) { return n >= 1 && (n <= 5 || (n % 100) == 0); }
    void SbLinkGone(const char* why)
    {
        bundle_ = false;   /* M13: the next connection bundles only after its own WELCOME */
        const long long n = box_.Clear() + col_.Clear();   /* M13: the open bundles' messages were waiting to go too */
        if (box_.Edge() < 0 && coopsbLogThis(sbEpisodes_))   /* T742: an episode still open when the link goes ends here, logged */
            DebugLog(std::string("[net] M16: the ") + SbLinkName() + "'s congestion episode " + N(sbEpisodes_) + " ended with the link (" + why + ")");
        if (n <= 0) return;
        sbDropped_ += n;
        DebugLog(std::string("[net] M16: ") + why + " - " + N(n) + " waiting message(s) on the " + SbLinkName() + " went with it (ENet drops its own queue the same way); counted sendBound[droppedAtClose]");
    }
    /* M13: see transport.h. Only the world-server link bundles; turning it off sends what is open first. */
    void SetBundling(bool on)
    {
        const bool want = on && liveDir_ == coopsb::kDirUp && host_ != 0 && peer_ != 0 && state_ == LINK_UP;
        if (!want) { BundleSealAll(); bundle_ = false; return; }
        if (bundle_) return;
        bundle_ = true;
        if (coopsbLogThis(++bnLinksOn_))
            DebugLog("[net] M13: the " + std::string(SbLinkName()) + " now sends the messages of each pass as one packet per lane - bundles of at most "
                     + N((long long)coopbundle::Budget(peer_->mtu, host_->checksum != 0)) + " bytes (ENet's one-command limit at this link's MTU of "
                     + N((long long)peer_->mtu) + "); larger messages and the handshake go alone (link " + N(bnLinksOn_) + "; the first 5 and every 100th are logged)");
    }
    /* M13: ENet's datagram counters for this host, added up here and reset there (ENet's are 32-bit and never reset themselves). */
    void UdpHarvest()
    {
        if (host_ == 0) return;
        udpPackets_ += (long long)host_->totalSentPackets; udpBytes_ += (long long)host_->totalSentData;
        host_->totalSentPackets = 0; host_->totalSentData = 0;
    }
    /* M13: bundle[packets,bundles,bundled,bytesSaved,single,alone,sendFailed,recvBundles,recvBundled,recvBad,udpPackets,udpBytes],
       all since this transport was created - see sendbundle.h. packets = ENet packets this link sent (each bundle, each message
       that went on its own); bundles / bundled = bundles sent and the messages inside them; bytesSaved = their SavedBytes;
       single = open bundles that held one message (sent as its own frame); alone = messages too large to share a packet, or
       the handshake; sendFailed = messages in bundles ENet refused; recvBundles / recvBundled = bundles received and the
       messages in them; recvBad = bundles refused whole; udpPackets / udpBytes = what ENet put on the wire for this link. */
    std::string BundleToken()
    {
        if (liveDir_ != coopsb::kDirUp) return std::string();
        UdpHarvest();
        return " bundle[packets,bundles,bundled,bytesSaved,single,alone,sendFailed,recvBundles,recvBundled,recvBad,udpPackets,udpBytes]="
             + N((long long)sent_) + "," + N(bnTally_.bundles) + "," + N(bnTally_.bundled) + "," + N(bnTally_.saved) + "," + N(bnTally_.single)
             + "," + N(bnTally_.alone) + "," + N(bnSendFailed_) + "," + N(bnRecvBundles_) + "," + N(bnRecvBundled_) + "," + N(bnRecvBad_)
             + "," + N(udpPackets_) + "," + N(udpBytes_);
    }
    /* sendBound[waitNow,waitBytesNow,waitMax,netQueued,netBytes,direct,waited,coalesced,episodes,droppedAtClose,sendFailed] - see sendbound.h;
       waitMax = the most waiting since the previous REPORT; netQueued/netBytes = what ENet holds for this link now. */
    void SetStoreLink() { liveDir_ = coopsb::kDirUp; }   /* M16 fold 2: see transport.h */
    std::string SendBoundToken()
    {
        const long long wm = sbWaitMax_; sbWaitMax_ = box_.Count();
        return " sendBound[waitNow,waitBytesNow,waitMax,netQueued,netBytes,direct,waited,coalesced,episodes,droppedAtClose,sendFailed]="
             + N(box_.Count()) + "," + N(box_.Bytes()) + "," + N(wm) + "," + N(netq_.count) + "," + N(netq_.bytes) + "," + N(sbDirect_)
             + "," + N(sbWaited_) + "," + N(sbCoalesced_) + "," + N(sbEpisodes_) + "," + N(sbDropped_) + "," + N(sbSendFailed_);
    }

    LinkState   State() const       { return state_; }
    BackendId   Backend() const     { return BACKEND_DIRECT; }
    const char* BackendName() const { return "direct (ENet/UDP)"; }

    unsigned int RoundTripMs() const
    {
        return (peer_ != 0) ? (unsigned int)peer_->roundTripTime : 0;
    }
    /* P7f (review-p6z H-1): ENet's own count, read straight off the host. */
    unsigned int PeerCount() const
    {
        return (host_ != 0) ? (unsigned int)host_->connectedPeers : 0;
    }
    /* T-355 fold 2: see transport.h. peer_ is the one session peer; its data is the id set at CONNECT (0 = the host). */
    bool CurrentPeerId(unsigned int* out) const
    {
        if (peer_ == 0 || state_ != LINK_UP) return false;
        if (out != 0) *out = (unsigned int)(size_t)peer_->data;
        return true;
    }
    /* P7v: see transport.h. Bumped on every CONNECT and every DISCONNECT this transport observes. */
    unsigned int ConnectGen() const { return connectGen_; }
    /* link1 (T416): see transport.h. Applied at each CONNECT event (host and joiner), and at once to a peer already up. */
    void SetPeerTimeouts(unsigned int limit, unsigned int minimumMs, unsigned int maximumMs)
    {
        tLimit_ = limit; tMinMs_ = minimumMs; tMaxMs_ = maximumMs;
        if (peer_ != 0 && state_ == LINK_UP) enet_peer_timeout(peer_, tLimit_, tMinMs_, tMaxMs_);
    }
    /* addr1: see transport.h. peer_ is the host for a joining game (set by Join); ENet already resolved it. */
    std::string PeerHostIp() const
    {
        if (peer_ == 0) return std::string();
        char buf[64];
        std::memset(buf, 0, sizeof(buf));
        if (enet_address_get_host_ip(&peer_->address, buf, sizeof(buf) - 1) != 0) return std::string();
        return std::string(buf);
    }
    /* ui1: see transport.h. peer_ is the host for a joining game; lastReceiveTime is set from host->serviceTime (ENet's clock). */
    unsigned int HostSilenceMs() const
    {
        if (peer_ == 0 || state_ != LINK_UP) return 0;
        const enet_uint32 now = enet_time_get();
        const enet_uint32 d = now - peer_->lastReceiveTime;   /* ui1: unsigned - a wrap is harmless */
        return (d & 0x80000000u) != 0 ? 0u : (unsigned int)d;   /* a receive stamped after 'now' reads as just heard */
    }
    /* See transport.h. M16's Outbox (box_) holds what is not yet handed to ENet, counted on top. ENet keeps a command in outgoingCommands / outgoingSendReliableCommands until it is sent and a reliable
       one in sentReliableCommands until it is acknowledged; each command is one piece of `fragmentLength` bytes (pings and
       other control commands count as 0 bytes). Read on the thread that services the link, like every other peer_ access. */
    bool SendQueued(long long* packets, long long* bytes) const
    {
        if (peer_ == 0 || state_ != LINK_UP) return false;
        ENetList* const lists[3] = { &peer_->outgoingCommands, &peer_->outgoingSendReliableCommands, &peer_->sentReliableCommands };
        long long n = 0, b = 0;
        for (int k = 0; k < 3; ++k)
            for (ENetListIterator it = enet_list_begin(lists[k]); it != enet_list_end(lists[k]); it = enet_list_next(it))
            {
                ++n;
                b += (long long)((const ENetOutgoingCommand*)it)->fragmentLength;
            }
        n += box_.Count(); b += box_.Bytes();   /* M16's own waiting line (sendbound.h Outbox): messages not yet handed to ENet, one whole frame each */
        if (packets != 0) *packets = n;
        if (bytes != 0) *bytes = b;
        return true;
    }
    unsigned long long SentCount() const { return sent_; }
    unsigned long long RecvCount() const { return recv_; }

private:
    // ENet's global init is process-wide and must happen exactly once.
    static bool EnsureEnet(std::string* errOut)
    {
        static int s_state = 0;   // 0 = untried, 1 = ok, -1 = failed
        if (s_state == 1) return true;
        if (s_state == -1)
        {
            if (errOut) *errOut = "enet_initialize previously failed";
            return false;
        }
        if (enet_initialize() != 0)
        {
            s_state = -1;
            if (errOut) *errOut = "enet_initialize failed";
            ErrorLog("[net] enet_initialize FAILED");
            return false;
        }
        s_state = 1;
        DebugLog("[net] ENet initialized");
        return true;
    }

    void Decode(ENetEvent* ev, std::vector<Message>* out)
    {
        const ENetPacket* p = ev->packet;
        if (p->dataLength < kHeaderBytes)
        {
            ErrorLog("[net] dropped runt packet, len=" + N((long long)p->dataLength));
            return;
        }

        unsigned int declared = 0;
        std::memcpy(&declared, p->data + 1, 4);
        if (declared != p->dataLength - kHeaderBytes)
        {
            // Length disagreement means a truncated or foreign packet - refuse it here
            // rather than let a malformed body reach the replication layer.
            ErrorLog("[net] dropped packet: declared " + N(declared)
                     + " but body is " + N((long long)(p->dataLength - kHeaderBytes)));
            return;
        }

        /* M13: a BUNDLE (world-server link only - 59 is MSG_TALK on the session link) is unpacked here into its messages, in
           order, so everything above this sees them exactly as if each had come alone. A malformed one is refused whole. */
        if (liveDir_ == coopsb::kDirUp && (unsigned int)p->data[0] == coopbundle::kMsgBundle)
        {
            const char* body = (const char*)p->data + kHeaderBytes;
            std::vector<coopbundle::Entry> es;
            if (!coopbundle::Decode(body, declared, &es))
            {
                ++bnRecvBad_;
                if (coopsbLogThis(bnRecvBad_))
                    ErrorLog("[net] M13: dropped a malformed bundle of " + N((long long)declared) + " bytes from the world server - none of its messages"
                             " is read (counted bundle[recvBad]; the first 5 and every 100th are logged)");
                return;
            }
            ++bnRecvBundles_; bnRecvBundled_ += (long long)es.size();
            for (size_t i = 0; i < es.size(); ++i)
            {
                Message e;
                e.type = (MsgType)es[i].type;
                e.peer = (unsigned int)(size_t)ev->peer->data;
                if (es[i].len) e.payload.assign(body + es[i].at, body + es[i].at + es[i].len);
                out->push_back(e);
                ++recv_;
            }
            return;
        }
        Message m;
        m.type = (MsgType)p->data[0];
        m.peer = (unsigned int)(size_t)ev->peer->data;
        if (declared)
            m.payload.assign((const char*)p->data + kHeaderBytes,
                             (const char*)p->data + kHeaderBytes + declared);
        out->push_back(m);
        ++recv_;
    }

    ENetHost* host_;
    ENetPeer* peer_;
    LinkState state_;
    unsigned long long sent_;
    unsigned long long recv_;
    unsigned int nextPeerId_;
    bool hosting_;      // decides peer-id assignment: host is always 0
    unsigned int connectGen_;   /* P7v: CONNECT + DISCONNECT edges seen, monotonic, process-local */
    unsigned int tLimit_, tMinMs_, tMaxMs_;   /* link1: enet_peer_timeout terms for each CONNECTED peer; all 0 = never called */
    coopsb::Outbox box_;   /* M16: what waits to be handed to ENet, in order */
    SbNetQ netq_;          /* M16: what ENet still holds for this link (its packet free callback keeps it) */
    long long sbDirect_, sbWaited_, sbCoalesced_, sbEpisodes_, sbDropped_, sbSendFailed_, sbWaitMax_;
    int liveDir_;   /* M16 fold 2: coopsb::kDirUp on the world-server link (SetStoreLink), kDirNoLive on the session link */
    bool bundle_;                  /* M13: bundling is on for the connection up now (SetBundling) */
    coopbundle::Collector col_;    /* M13: the open bundle of each lane */
    coopbundle::Tally bnTally_;    /* M13: what the collector did */
    long long bnSendFailed_, bnRecvBundles_, bnRecvBundled_, bnRecvBad_, bnLinksOn_;
    long long udpPackets_, udpBytes_;   /* M13: ENet's datagram counters, harvested (UdpHarvest) */
};

} // namespace

ITransport* CreateTransport(BackendId which)
{
    switch (which)
    {
    case BACKEND_DIRECT:
        return new EnetTransport();
    case BACKEND_STEAM:
        // Deliberately not silently falling back to direct: the player chose Steam, and
        // quietly substituting a different backend would be exactly the kind of hidden
        // behaviour the "no default" amendment exists to prevent.
        ErrorLog("[net] Steam backend is not implemented yet (post-PoC) - refusing");
        return 0;
    default:
        return 0;
    }
}

const char* MsgTypeName(MsgType t)
{
    switch (t)
    {
    case MSG_HELLO:   return "HELLO";
    case MSG_WELCOME: return "WELCOME";
    case MSG_PING:    return "PING";
    case MSG_PONG:    return "PONG";
    case MSG_SPAWN:   return "SPAWN";
    case MSG_TASK:    return "TASK";
    case MSG_MOVE:    return "MOVE";
    case MSG_HIT:     return "HIT";
    case MSG_STATE:   return "STATE";
    case MSG_DOOR_STATE: return "DOOR_STATE";
    case MSG_SAY: return "SAY";
    case MSG_STATS: return "STATS";
    case MSG_CRIME: return "CRIME";
    case MSG_BOUNTY: return "BOUNTY";
    case MSG_CARRY_BREAK: return "CARRY_BREAK";
    case MSG_PRISON: return "PRISON";
    case MSG_TREAT: return "TREAT";
    case MSG_BUILD: return "BUILD";
    case MSG_NAME: return "NAME";   /* names1 */
    case MSG_SLAVE: return "SLAVE";   /* slave1 */
    case MSG_PARITY_REQ: return "PARITY_REQ";   /* par1 */
    case MSG_PARITY_BOX: return "PARITY_BOX";   /* par1 */
    case MSG_HIRE: return "HIRE";   /* recruit1 */
    case MSG_TALK: return "TALK";   /* P26 stages 1-3 */
    case MSG_CAPTURE: return "CAPTURE";   /* P11 */
    case MSG_CAPTURE_DONE: return "CAPTURE_DONE";   /* P11 */
    case MSG_CAPTURE_PLACED: return "CAPTURE_PLACED";   /* P11 f3 */
    case MSG_SHOT: return "SHOT";   /* P104 fix */
    case MSG_PEER_SLOT: return "PEER_SLOT";   /* M5b */
    case MSG_EFFECT: return "EFFECT";   /* T-327 */
    case MSG_INSIDE: return "INSIDE";   /* P25 fold 2 */
    case MSG_RELEASE: return "RELEASE";   /* M7a A1 build 2 [a1b2-en0] */
    case MSG_RELEASE_ACK: return "RELEASE_ACK";   /* M7a A1 build 2 */
    case MSG_ROSTER: return "ROSTER";   /* M7a A1 build 1 [a1b1-en0] */
    case MSG_RECEIPT: return "RECEIPT";   /* M7a A1 build 1 */
    case MSG_MOVESTOP: return "MOVESTOP";
    case MSG_NOT_SHOWN: return "NOT_SHOWN";   /* T-354 */
    case MSG_SESSION_CLOSING: return "SESSION_CLOSING";   /* mmo5 */
    case MSG_SQUAD_LEAD: return "SQUAD_LEAD";   /* T-1 B1 restructure */
    /* 58 (KEEPER) retired by protocol 89 - T-1 B3 restructure */
    case MSG_BYE:     return "BYE";
    default:          return "UNKNOWN";
    }
}

const char* LinkStateName(LinkState s)
{
    switch (s)
    {
    case LINK_DOWN:       return "down";
    case LINK_LISTENING:  return "listening";
    case LINK_CONNECTING: return "connecting";
    case LINK_UP:         return "UP";
    case LINK_FAILED:     return "FAILED";
    default:              return "?";
    }
}

} // namespace net
} // namespace coop
