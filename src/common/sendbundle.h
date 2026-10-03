/* M13 - ONE PACKET PER PASS PER DESTINATION, AS PURE DECISIONS.
 *
 * Without bundling every message a game or the world server sends is its own network packet. Each packet costs a
 * network-layer (ENet) command header, a reliable one also an acknowledgement coming back, and ENet puts at most 32
 * commands in one UDP datagram - so a world server relaying 100 players' movement sends a flood of tiny packets.
 * Now the messages for ONE destination that are handed to the network during one pass (a game's frame, one pass of the
 * world server's loop) are collected and sent as ONE packet, a BUNDLE, which the receiving end unpacks in order before
 * its ordinary handling. Nothing about any message inside changes.
 *
 * THE RULES:
 *   - Two LANES per link, one per channel: the guaranteed, ordered channel (0) and the unreliable one (1, a MOVE or STATE
 *     sent without the guarantee). A bundle holds messages of one lane only, in the order they were handed over, and goes
 *     on that lane's channel - so a guaranteed message never loses its guarantee or its order, and an unreliable one
 *     never waits behind guaranteed traffic (sendbound.h's rule stays true: bundling happens AFTER the waiting line, at
 *     the moment a message would have gone to ENet).
 *   - THE SIZE BUDGET is ENet's own one-command limit: a packet larger than the link's MTU less ENet's datagram header and
 *     its fragment command is split into fragments. Split, an UNRELIABLE packet is sent as RELIABLE fragments
 *     (enet_peer_send, peer.c - neither program sets ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT): it would join the guaranteed
 *     stream and could be held up behind it. So no bundle is ever larger than Budget(mtu) - 1364 bytes on ENet's default
 *     MTU of 1392 that both ends use - and each bundle is one command that fits one datagram.
 *   - A message too large to share a packet within that budget goes ALONE, as its own frame, exactly as before; the
 *     lane's open bundle goes first, so the order on the guaranteed lane holds.
 *   - The HANDSHAKE (STORE_HELLO, STORE_WELCOME, STORE_REFUSE) always goes alone, so a game or world server of another
 *     protocol can still read it and refuse in words. Bundling for a link starts only after both ends are known to speak
 *     the same world-server protocol (the game at a matching WELCOME, the world server once it has sent one).
 *   - A bundle that ends up holding ONE message is sent as that message's own frame - a bundle never costs bytes for
 *     nothing.
 *
 * THE WIRE (world-server link only; type 59 is MSG_TALK on the old game-to-game link and is never unpacked there):
 *   frame  = u8 type 59 | u32 payload length | payload            (the ordinary 5-byte frame header)
 *   payload = one or more entries, back to back, to the end:
 *   entry  = u8 type | u16 length | length bytes                   (the message's own type and payload, unchanged)
 * A receiver refuses the WHOLE bundle (dispatches none of it) when an entry is cut short, runs past the end, or is itself
 * a bundle.
 *
 * No Windows, no ENet, no threads. The offline suite drives all of it (coop-test, m13_*).
 */
#ifndef COOP_SENDBUNDLE_H
#define COOP_SENDBUNDLE_H

#include <cstring>
#include <vector>

namespace coopbundle {

const unsigned int kMsgBundle = 59;                                        /* store_main.cpp MSG_BUNDLE / store.cpp kStoreMsgBundle */
const unsigned int kMsgStoreHello = 29, kMsgStoreWelcome = 30, kMsgStoreRefuse = 42;   /* the handshake - always alone */
const size_t kFrameHeader = 5;   /* u8 type | u32 payload length - every frame on the link */
const size_t kEntryHeader = 3;   /* u8 type | u16 payload length - one message inside a bundle */
/* ENet's wire sizes (third_party/enet protocol.h); both programs check them against sizeof at compile time. */
const size_t kEnetProtocolHeader = 4, kEnetSendFragment = 24, kEnetChecksum = 4;
const size_t kEnetSendReliable = 6, kEnetSendUnsequenced = 8, kEnetAcknowledge = 8;
const unsigned int kEnetMinimumMtu = 576, kEnetMaximumMtu = 4096;

enum { kLaneReliable = 0, kLaneUnreliable = 1, kLanes = 2 };
enum { kPlaceAppend = 0, kPlaceSealFirst = 1, kPlaceAlone = 2 };   /* Place's answers */
enum { kTaken = 0, kAlone = 1 };                                   /* Collector::Offer's answers */

/* The largest frame (header included) that ENet sends as ONE command on a link of this MTU: ENet's own fragment
   threshold (enet_peer_send). The MTU is clamped to ENet's own protocol range. */
inline size_t Budget(unsigned int mtu, bool checksum)
{
    if (mtu < kEnetMinimumMtu) mtu = kEnetMinimumMtu;
    if (mtu > kEnetMaximumMtu) mtu = kEnetMaximumMtu;
    return (size_t)mtu - kEnetProtocolHeader - kEnetSendFragment - (checksum ? kEnetChecksum : 0);
}
/* Channel 0 is the guaranteed, ordered lane; every other channel is unreliable. */
inline int LaneOf(int channel) { return channel == 0 ? (int)kLaneReliable : (int)kLaneUnreliable; }
/* The handshake and a bundle itself never ride inside a bundle. */
inline bool MayBundle(unsigned int type)
{
    return type != kMsgBundle && type != kMsgStoreHello && type != kMsgStoreWelcome && type != kMsgStoreRefuse;
}
inline bool FrameOk(const char* frame, size_t len)
{
    if (frame == 0 || len < kFrameHeader) return false;
    unsigned int n = 0; std::memcpy(&n, frame + 1, 4);
    return (size_t)n == len - kFrameHeader;
}
/* Where one whole frame goes, given the lane's open bundle (openEntries messages, openBody entry bytes):
   kPlaceAppend into it, kPlaceSealFirst (the open bundle has no room: it goes out now and a new one starts with this
   frame), or kPlaceAlone (too large to share a packet, the handshake, or not a well-formed frame: it goes as its own
   packet, after the open bundle). */
inline int Place(size_t openEntries, size_t openBody, const char* frame, size_t len, size_t budget)
{
    if (!FrameOk(frame, len) || !MayBundle((unsigned char)frame[0])) return kPlaceAlone;
    const size_t entry = len - kFrameHeader + kEntryHeader;
    if (kFrameHeader + entry > budget) return kPlaceAlone;
    if (openEntries == 0) return kPlaceAppend;
    return (kFrameHeader + openBody + entry > budget) ? (int)kPlaceSealFirst : (int)kPlaceAppend;
}
/* True when the frame may share a packet at all (Place with nothing open). */
inline bool Fits(const char* frame, size_t len, size_t budget) { return Place(0, 0, frame, len, budget) != kPlaceAlone; }
/* What one sealed bundle saved against sending its messages alone: the frame bytes (each message's own 5-byte header
   against the bundle's header and the entries' 3-byte headers) plus ENet's command for every packet not sent - and,
   on the guaranteed lane, the acknowledgement the receiver no longer returns for it. 0 for a single message. */
inline long long SavedBytes(size_t entries, size_t frameBytes, size_t sealedBytes, int lane)
{
    if (entries < 2) return 0;
    const long long perPacket = lane == kLaneReliable ? (long long)(kEnetSendReliable + kEnetAcknowledge) : (long long)kEnetSendUnsequenced;
    return (long long)frameBytes - (long long)sealedBytes + (long long)(entries - 1) * perPacket;
}

/* One entry of a received bundle: its type and where its payload lies in the bundle's payload. */
struct Entry
{
    unsigned int type; size_t at, len;
    Entry() : type(0), at(0), len(0) {}
};
/* A bundle's payload (after the 5-byte frame header) into its entries, in order. false (and *out empty) for a payload
   that is empty, has an entry cut short or running past the end, or nests a bundle - the whole bundle is refused. */
inline bool Decode(const char* p, size_t n, std::vector<Entry>* out)
{
    out->clear();
    if (p == 0 || n == 0) return false;
    size_t at = 0;
    while (at < n)
    {
        if (n - at < kEntryHeader) { out->clear(); return false; }
        Entry e; e.type = (unsigned char)p[at];
        unsigned short len = 0; std::memcpy(&len, p + at + 1, 2);
        at += kEntryHeader;
        if ((size_t)len > n - at || e.type == kMsgBundle) { out->clear(); return false; }
        e.at = at; e.len = len; out->push_back(e);
        at += len;
    }
    return true;
}

/* What the collectors of one program have done, since start. */
struct Tally
{
    long long bundles;   /* bundles sent (two or more messages in one packet) */
    long long bundled;   /* messages that went inside them */
    long long single;    /* open bundles that held one message when sealed - sent as that message's own frame */
    long long alone;     /* messages sent alone: too large to share a packet, or the handshake */
    long long saved;     /* SavedBytes over every bundle */
    Tally() : bundles(0), bundled(0), single(0), alone(0), saved(0) {}
};

/* ONE DESTINATION'S OPEN BUNDLES, one per lane. The caller hands every frame bound for that destination to Offer at the
   moment it would have gone to the network, sends what Offer says must go now, and Seals both lanes once per pass,
   right before the network layer transmits. */
class Collector
{
public:
    Collector() { m_entries[0] = m_entries[1] = 0; m_frameBytes[0] = m_frameBytes[1] = 0; }
    /* One whole frame on `lane`. kTaken: it is in the lane's open bundle. kAlone: the caller sends the frame itself as
       one packet. Either way *first (when not empty) holds the lane's open bundle, sealed, which the caller must send
       FIRST - it had no room for this frame, or it must go before a frame that goes alone. *firstEntries = how many
       messages *first carries (0 = none). */
    int Offer(int lane, const char* frame, size_t len, size_t budget, std::vector<char>* first, size_t* firstEntries, Tally* t)
    {
        first->clear(); *firstEntries = 0;
        lane = LaneIndex(lane);
        const int p = Place(m_entries[lane], m_body[lane].size(), frame, len, budget);
        if (p != kPlaceAppend) *firstEntries = Seal(lane, first, t);
        if (p == kPlaceAlone) { if (t) ++t->alone; return kAlone; }
        const unsigned short n = (unsigned short)(len - kFrameHeader);
        std::vector<char>& b = m_body[lane];
        b.push_back(frame[0]);
        b.insert(b.end(), (const char*)&n, (const char*)&n + 2);
        b.insert(b.end(), frame + kFrameHeader, frame + len);
        ++m_entries[lane]; m_frameBytes[lane] += len;
        return kTaken;
    }
    /* The lane's open bundle as ONE frame (*out), and the lane emptied. One message goes as its own frame, unchanged;
       two or more as a bundle. Returns how many messages *out carries (0 = nothing was open, *out empty). */
    size_t Seal(int lane, std::vector<char>* out, Tally* t)
    {
        out->clear();
        lane = LaneIndex(lane);
        const size_t entries = m_entries[lane];
        if (entries == 0) return 0;
        const std::vector<char>& b = m_body[lane];
        if (entries == 1)
        {
            const unsigned int n = (unsigned int)(b.size() - kEntryHeader);
            out->push_back(b[0]);
            out->insert(out->end(), (const char*)&n, (const char*)&n + 4);
            out->insert(out->end(), b.begin() + kEntryHeader, b.end());
            if (t) ++t->single;
        }
        else
        {
            const unsigned int n = (unsigned int)b.size();
            out->push_back((char)(unsigned char)kMsgBundle);
            out->insert(out->end(), (const char*)&n, (const char*)&n + 4);
            out->insert(out->end(), b.begin(), b.end());
            if (t) { ++t->bundles; t->bundled += (long long)entries; t->saved += SavedBytes(entries, m_frameBytes[lane], out->size(), lane); }
        }
        m_body[lane].clear(); m_entries[lane] = 0; m_frameBytes[lane] = 0;
        return entries;
    }
    /* Packets and bytes the open bundles will be when sealed - counted with what the network layer holds, so the send
       bound (sendbound.h) sees them. */
    size_t OpenPackets() const { return (m_entries[0] != 0 ? 1u : 0u) + (m_entries[1] != 0 ? 1u : 0u); }
    size_t OpenBytes() const { return SealedSize(0) + SealedSize(1); }
    size_t OpenEntries() const { return m_entries[0] + m_entries[1]; }
    /* The link is gone: the open bundles go with it. Returns how many messages they held. */
    long long Clear()
    {
        const long long n = (long long)(m_entries[0] + m_entries[1]);
        for (int i = 0; i < kLanes; ++i) { m_body[i].clear(); m_entries[i] = 0; m_frameBytes[i] = 0; }
        return n;
    }
private:
    static int LaneIndex(int lane) { return (lane == kLaneUnreliable) ? (int)kLaneUnreliable : (int)kLaneReliable; }
    size_t SealedSize(int lane) const
    {
        if (m_entries[lane] == 0) return 0;
        return m_entries[lane] == 1 ? m_frameBytes[lane] : kFrameHeader + m_body[lane].size();
    }
    std::vector<char> m_body[kLanes];   /* the open bundle's entries */
    size_t m_entries[kLanes];           /* messages in it */
    size_t m_frameBytes[kLanes];        /* their frames' sizes, as they would have gone alone */
};

}   /* namespace coopbundle */

#endif
