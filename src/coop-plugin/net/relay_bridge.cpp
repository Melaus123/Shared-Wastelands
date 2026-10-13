/* net/relay_bridge.cpp - THE HOST'S STEAM BRIDGE, and the route a world-server dial takes.

   THE BRIDGE (the hosting game). While this game hosts a world server on this PC - role host, its world-server address 127.0.0.1
   (or localhost) with a port - it also listens for Steam joiners: one SteamListener (net/steam_transport.cpp) on Steam P2P virtual
   port 0, Valve's relay only, up to steamlisten::kLinkCap links. Every link it accepts gets its OWN direct connection to the world
   server on 127.0.0.1:<port>, made as the game's own world-server link is made (CreateTransport(BACKEND_DIRECT), SetStoreLink, the
   game link's timeouts), and the bridge copies every message both ways unchanged - the same type, the same bytes, the same lane. The
   world server sees each Steam joiner as one more game on 127.0.0.1 and tells them apart by their HELLO, as it does any game. As soon
   as a joiner's world-server connection is up, and before its Steam link is accepted (so before the joiner can send anything), the
   bridge tells the world server who the joiner is (STEAM_ID, src/common/authproof.h: this hosting's secret, which proves the message
   is the bridge's, and the Steam-checked id, 0 when Steam checked none) for its access list and its wrong-password wait; neither is
   ever logged.
   - Whole messages: both transports hand up whole messages (the world-server connection unpacks the world server's bundles, the
     Steam link rejoins its pieces), and each is handed on as one message. Nothing is bundled toward the world server: a connection
     inside this PC gains nothing from it.
   - Lanes: Message::lane is the lane a message arrived on; it is handed on on the same lane.
   - Order: a joiner's arrival is held, not accepted (SteamListener::SetHoldArrivals), while its own connection to the world server
     is dialled, and is accepted once that connection is up - so nothing the joiner sends (its HELLO first) arrives before the world
     server can take it, and every message goes on in the order it came. A world server that refuses, drops or does not answer
     within kLocalConnectMs has the arrival refused with end reason steamlisten::kEndHostServerDown (1008): the joiner's Steam link
     never comes up, which its game treats as any failed dial, and its own re-dial tries again. An accepted joiner's Steam link is
     still connecting, and Steam takes nothing on it until it is up: what the world server sends meanwhile (it sends to every
     connected game about once a second) waits in the pair, its connection read only every kSlowPollMs, and goes to the joiner
     first and in order once that link is up (relaybridge.h JoinerSideStep); more than kFarBehindBytes waiting closes the pair.
   - Back-pressure: while a joiner's Steam link has a lot waiting, its world-server connection is read less often, so the world
     server keeps the rest (relaybridge.h SlowNext / ReadLocalNow); a joiner too far behind either way is closed (FarBehind).
   - Closing: a Steam link that ends closes its world-server connection gracefully (what the joiner sent last still reaches the world
     server); a world-server connection that ends closes its Steam link with linger (what the world server sent last still reaches the
     joiner); a guaranteed message either side refuses closes both. Closing the whole bridge (hosting ends, the world server it
     bridges to changes, this game exits) closes a world-server connection gracefully only while that close's wait still fits in
     kCloseAllBudgetMs (relaybridge.h CloseGracefully), the rest at once, so this game's frame is not held for long; an arrival
     still held then is refused with end reason steamlisten::kEndListenerNoRoom (1006).
   - Threading: all of it runs on the main thread, once per frame, from SteamProbeTick (title and main-loop pumps). While this game's
     frame stands still (a world load or a save here) nothing is relayed: the gap is measured and said (StallWorthALine).
   relaybridge=off (a player's switch) keeps the bridge closed. TEST ONLY: relaybridge=ip:<port> listens on 127.0.0.1:<port> over
   Steam's plain UDP instead of P2P (one PC with one Steam account cannot make a Steam-to-Steam connection); relaycap=<n> lowers
   the cap for new arrivals.

   THE ROUTE (every game). store.cpp SetStoreServer asks RelayRouteFor which route its address takes: steam:<id> dials that Steam id
   (Valve's relay only), anything else the direct route; TEST ONLY relayvia=ip:<port> sends every world-server dial over Steam's plain
   UDP to 127.0.0.1:<port>. The link it gets is an ITransport either way, and nothing above SetStoreServer asks which. Owner 592: a
   line says the route tried, the link-up line the route used, and a line the state and Steam's end reason of a Steam route closed
   here. No line carries a Steam id or an address other than 127.0.0.1. */
#include <windows.h>

#include "relay_bridge.h"
#include "steam_transport.h"
#include "steam_probe.h"
#include "../config.h"
#include "../coop_log.h"
#include "../../common/cfgtext.h"
#include "../../common/relaybridge.h"
#include "../../common/authproof.h"   /* STEAM_ID */
#include "../../common/relaysplit.h"
#include "../../common/steamlisten.h"
#include "../../common/gamelink.h"

#include <deque>
#include <sstream>
#include <locale>
#include <vector>

namespace coop {
bool StoreHostToken(unsigned char out[32]);   /* store.cpp, MAIN THREAD: this hosting's secret; false = this game started no world server */
namespace net {

namespace {

std::string N(long long v)   /* RE_Kenshi's locale mangles raw numbers (F030) */
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

/* One Steam link and its own world-server connection. */
struct Pair
{
    unsigned int link;          /* the listener's link id */
    ITransport*  local;         /* this link's connection to the world server */
    DWORD        dialMs;        /* when the connection was dialled */
    DWORD        lastReadMs;    /* when the connection was last read */
    bool         localUp;       /* the connection has come up */
    bool         slow;          /* read every kSlowPollMs only: the joiner's Steam link has a lot waiting */
    bool         held;          /* the joiner's arrival is held, not accepted yet: accepted when the connection comes up */
    bool         steamRefused;  /* Steam refused a guaranteed message toward the joiner: nothing more is sent to it */
    long long    toServer, toJoiner;
    std::string  closeWhy;      /* set = close this pair at the end of this tick */
    bool         closeSteam;    /* the Steam link (or the held arrival) is still the listener's and is closed with it */
    DWORD        acceptMs;      /* when the held arrival was accepted */
    std::deque<Message> waitJoiner;   /* from the world server while the joiner's Steam link is not up: handed on first, in order */
    long long    waitJoinerBytes;     /* their bytes (5 + payload each), bounded by FarBehind */
    Pair() : link(0), local(0), dialMs(0), lastReadMs(0), localUp(false), slow(false), held(true), steamRefused(false), toServer(0),
             toJoiner(0), closeSteam(true), acceptMs(0), waitJoinerBytes(0) {}
};

struct BridgeStats
{
    long long opened, openFailed, linksMax, accepted, refused, serverUp, serverFailed, ended, toServer, toJoiner, bytesToServer,
              bytesToJoiner, slowEpisodes, farBehind, unrelDropped, relFailed, stray, stalls, stallMaxMs, arrived, heldRefused,
              waitedMax,   /* the most messages that waited in one pair for its joiner's Steam link to come up */
              idKnown, idUnchecked,   /* STEAM_ID sent with a Steam-checked id / with 0 (Steam checked none) */
              idNoToken;              /* no STEAM_ID sent: this game holds no hosting secret (it did not start the world server) */
};

BridgeStats g_bs;                 /* zero at start (static storage) */
SteamListener* g_listener = 0;
std::vector<Pair*> g_pairs;
int g_want = -1;                  /* the last BridgeWant answer, for the line said when it changes */
bool g_openFailed = false;        /* the last Open failed: the next try waits kOpenRetryMs from g_openFailedMs */
DWORD g_openFailedMs = 0;
DWORD g_lastTickMs = 0;
std::string g_storeAddrOpen;      /* the world server the open bridge dials */
unsigned short g_storePortOpen = 0;
unsigned short g_ipPortOpen = 0;  /* the relay test's listener port; 0 = P2P */

long long g_routeDials[3];        /* dials per relaybridge::kRoute* */
long long g_routeSteamUp = 0, g_routeSteamClosed = 0;
int g_lastRoute = relaybridge::kRouteDirect;
bool g_exitClosed = false;        /* RelayBridgeShutdown ran: the game is exiting and the bridge is never opened again */

Channel LaneOf(const Message& m) { return m.lane == CH_UNRELIABLE ? CH_UNRELIABLE : CH_RELIABLE; }
const char* DataOf(const Message& m) { return m.payload.empty() ? 0 : &m.payload[0]; }

int SteamStateNow()
{
    if (!SteamProbeStarted()) return relaybridge::kSteamNotYet;
    return SteamProbeSockets() != 0 ? relaybridge::kSteamReady : relaybridge::kSteamAbsent;
}

int LinksUp()
{
    int n = 0;
    for (size_t i = 0; i < g_pairs.size(); ++i) if (g_pairs[i]->localUp) ++n;
    return n;
}

Pair* FindPair(unsigned int link)
{
    for (size_t i = 0; i < g_pairs.size(); ++i) if (g_pairs[i]->link == link) return g_pairs[i];
    return 0;
}

std::string ListenWhere()
{
    return g_ipPortOpen != 0 ? "127.0.0.1:" + N(g_ipPortOpen) + " over Steam's plain UDP (relay test relaybridge=ip)"
                             : std::string("Steam P2P virtual port 0, Valve's relay only");
}

/* graceful false: a world-server connection that is up is closed at once instead of waiting for the world server's acknowledgement
   (CloseBridge, once a graceful close no longer fits its budget). A held arrival is refused with end reason heldEnd -
   kEndHostServerDown when its world server did not answer, kEndListenerNoRoom when the whole bridge closes - and said in one line
   (the first 5 and every 100th): while the world server is down every joiner's re-dial comes back to this. */
void ClosePairAt(size_t i, bool graceful, int heldEnd)
{
    Pair* p = g_pairs[i];
    bool refused = false;
    if (p->closeSteam && g_listener != 0)
    {
        if (p->held) refused = g_listener->RefuseHeld(p->link, heldEnd);
        else g_listener->CloseLink(p->link, true);   /* linger: what the world server sent still goes */
    }
    if (p->local != 0)
    {
        if (p->local->State() == LINK_UP && graceful) p->local->Disconnect(); else p->local->Abort();   /* graceful: the joiner's last messages arrive */
        delete p->local;
        p->local = 0;
    }
    ++g_bs.ended;
    g_pairs.erase(g_pairs.begin() + i);
    if (refused)
    {
        ++g_bs.heldRefused;
        const char* const endText = steamlisten::EndText(heldEnd);
        if (relaybridge::LogThis(g_bs.heldRefused))
            DebugLog("[NET] relay bridge: link " + N(p->link) + " - a Steam joiner's arrival was refused with end reason " + N(heldEnd)
                     + " ('" + std::string(endText != 0 ? endText : "") + "') - " + p->closeWhy + "; its game's own re-dial tries again (refused "
                     + N(g_bs.heldRefused) + "; the first 5 and every 100th are said)");
        delete p;
        return;
    }
    DebugLog("[NET] relay bridge: link " + N(p->link) + " closed - " + p->closeWhy + " (" + N(p->toServer) + " message(s) went to the world server, "
             + N(p->toJoiner) + " to the joiner); the bridge holds " + N((long long)g_pairs.size()) + " link(s)");
    delete p;
}

void CloseBridge(const std::string& why)
{
    const DWORD began = ::GetTickCount();   /* a graceful close may wait ~300 ms for the world server: one starts only while it fits the budget */
    for (size_t i = g_pairs.size(); i > 0; --i)
    {
        if (g_pairs[i - 1]->closeWhy.empty()) g_pairs[i - 1]->closeWhy = why;   /* said in the pair's line, a refused arrival's too */
        ClosePairAt(i - 1, relaybridge::CloseGracefully((unsigned int)(DWORD)(::GetTickCount() - began)), steamlisten::kEndListenerNoRoom);
    }
    if (g_listener != 0)
    {
        g_listener->Close();
        delete g_listener;
        g_listener = 0;
        DebugLog("[NET] relay bridge: closed - " + why);
    }
}

bool OpenBridge(const std::string& storeAddr, unsigned short storePort, unsigned short ipPort)
{
    SteamListener* l = NewSteamListener();
    if (l == 0) return false;
    l->SetLinkTimeoutMs(coopgl::kGameLinkTimeoutMaximumMs);   /* a joiner's Steam link outlasts a world load here as the game link does */
    l->SetCap(ConfigRelayCap());   /* 0 (absent) = steamlisten::kLinkCap */
    l->SetHoldArrivals(true);      /* an arrival is accepted only once its own world-server connection is up */
    std::string err;
    if (!l->Open(ipPort, &err))
    {
        delete l;
        ++g_bs.openFailed;
        if (relaybridge::LogThis(g_bs.openFailed))
            ErrorLog("[NET] relay bridge: the Steam listener did not open - " + err + " - Steam joiners cannot reach this world; tried again every "
                     + N((long long)relaybridge::kOpenRetryMs / 1000) + " s (the first 5 and every 100th failures are said)");
        return false;
    }
    g_listener = l;
    g_storeAddrOpen = storeAddr; g_storePortOpen = storePort; g_ipPortOpen = ipPort;
    ++g_bs.opened;
    DebugLog("[NET] relay bridge: open - Steam joiners arrive on " + ListenWhere() + ", up to " + N(l->Cap())
             + "; each gets its own connection to the world server on 127.0.0.1:" + N(storePort) + " and its messages are copied both ways unchanged");
    return true;
}

/* Who this joiner is: STEAM_ID {this hosting's secret, the Steam-checked id (0 when Steam checked none)}, sent as soon as the joiner's
   world-server connection is up and before its Steam link is accepted, so it is that connection's first message - the world server
   believes it only so, and closes a connection where it comes later. With no secret (this game did not start the world server)
   nothing is sent and the joiner is an address joiner to the access list. false = the world-server connection refused it (closeWhy). */
bool SendJoinerId(Pair* p)
{
    unsigned char token[authproof::kTokenBytes];
    if (!StoreHostToken(token)) { ++g_bs.idNoToken; return true; }
    unsigned long long id = 0;
    if (!g_listener->LinkSteamId(p->link, &id)) id = 0;
    std::vector<char> b; authproof::SteamIdEncode(token, id, &b);
    if (!p->local->Send(0, (MsgType)authproof::kMsgSteamId, &b[0], b.size(), CH_RELIABLE))
    {
        ++g_bs.relFailed;
        p->closeWhy = "its world-server connection refused the joiner's Steam id";
        return false;
    }
    if (id != 0) ++g_bs.idKnown; else ++g_bs.idUnchecked;
    return true;
}

/* A message from the joiner, to the world server on its lane. The joiner's Steam link is accepted only once its world-server
   connection is up, so nothing comes before it; one that did would be counted as stray and dropped. */
void ToServer(Pair* p, const Message& m)
{
    if (!p->closeWhy.empty()) return;
    if (!p->localUp) { ++g_bs.stray; return; }
    if (p->local->Send(0, m.type, DataOf(m), m.payload.size(), LaneOf(m)))
    {
        ++p->toServer; ++g_bs.toServer; g_bs.bytesToServer += 5 + (long long)m.payload.size();
    }
    else if (LaneOf(m) == CH_UNRELIABLE) ++g_bs.unrelDropped;
    else { ++g_bs.relFailed; p->closeWhy = "its world-server connection refused a guaranteed message"; }
}

/* A message from the world server: to the joiner on its lane. A full Steam send buffer drops an unreliable one (that lane is
   lossy); a guaranteed one Steam refuses closes the pair, as one the world-server connection refuses does (the joiner cannot stay
   in step without it), and nothing more is sent toward the joiner. */
void ToJoiner(Pair* p, const Message& m)
{
    if (p->steamRefused) return;
    if (g_listener->Send(p->link, m.type, DataOf(m), m.payload.size(), LaneOf(m)))
    {
        ++p->toJoiner; ++g_bs.toJoiner; g_bs.bytesToJoiner += 5 + (long long)m.payload.size();
    }
    else if (LaneOf(m) == CH_UNRELIABLE) ++g_bs.unrelDropped;
    else
    {
        ++g_bs.relFailed;
        p->steamRefused = true;
        if (p->closeWhy.empty()) p->closeWhy = "Steam refused a guaranteed message toward the joiner";
    }
}

/* The joiner's Steam link is not up (the arrival still held, or accepted and connecting): what the world server handed up waits in
   the pair, in order, for HandOnWaiting. Too much waiting closes the pair, as FarBehind closes a joiner too far behind. */
void WaitForJoiner(Pair* p, const std::vector<Message>& out)
{
    for (size_t k = 0; k < out.size(); ++k)
    {
        p->waitJoiner.push_back(out[k]);
        p->waitJoinerBytes += 5 + (long long)out[k].payload.size();
    }
    if ((long long)p->waitJoiner.size() > g_bs.waitedMax) g_bs.waitedMax = (long long)p->waitJoiner.size();
    if (relaybridge::FarBehind(p->waitJoinerBytes) && p->closeWhy.empty())
    {
        ++g_bs.farBehind;
        p->closeWhy = "the joiner's Steam link did not come up while " + N(p->waitJoinerBytes) + " bytes waited for it (the limit is "
                      + N(relaybridge::kFarBehindBytes) + ")";
    }
}

/* The joiner's Steam link is up: what the world server sent while it was connecting goes to the joiner now, first and in order
   (ToJoiner: a guaranteed one Steam refuses closes the pair). One line per link that had something waiting. */
void HandOnWaiting(Pair* p, DWORD now)
{
    const long long waited = (long long)p->waitJoiner.size(), waitedBytes = p->waitJoinerBytes;
    while (!p->waitJoiner.empty())
    {
        ToJoiner(p, p->waitJoiner.front());
        p->waitJoiner.pop_front();
    }
    p->waitJoinerBytes = 0;
    DebugLog("[NET] relay bridge: link " + N(p->link) + " - the joiner's Steam link is up " + N((long long)(DWORD)(now - p->acceptMs))
             + " ms after it was accepted; " + N(waited) + " message(s) (" + N(waitedBytes) + " bytes) the world server sent meanwhile go to"
             " the joiner now, first and in order");
}

void NewPair(unsigned int link, DWORD now)
{
    Pair* p = new Pair();
    p->link = link; p->dialMs = now; p->lastReadMs = now;
    p->local = CreateTransport(BACKEND_DIRECT);
    std::string err = "no transport could be made";
    if (p->local != 0)
    {
        p->local->SetStoreLink();   /* the world server's bundles are unpacked into whole messages; type 53 is a LIVE envelope */
        p->local->SetPeerTimeouts(coopgl::kGameLinkTimeoutLimit, coopgl::kGameLinkTimeoutMinimumMs, coopgl::kGameLinkTimeoutMaximumMs);
        err.clear();
    }
    ++g_bs.arrived;
    g_pairs.push_back(p);
    if ((long long)g_pairs.size() > g_bs.linksMax) g_bs.linksMax = (long long)g_pairs.size();
    if (p->local == 0 || !p->local->Join(g_storeAddrOpen, g_storePortOpen, &err))
    {
        ++g_bs.serverFailed;
        p->closeWhy = "its connection to the world server on 127.0.0.1:" + N(g_storePortOpen) + " could not be dialled (" + err + ")";
        return;   /* closed with this tick's closes: the held arrival is refused */
    }
    if (relaybridge::LogThis(g_bs.arrived))
        DebugLog("[NET] relay bridge: link " + N(link) + " arrived on " + ListenWhere() + " - held, not accepted, while its own connection to the"
                 " world server on 127.0.0.1:" + N(g_storePortOpen) + " is dialled; the bridge holds " + N((long long)g_pairs.size()) + " of "
                 + N(g_listener->Cap()) + " (arrival " + N(g_bs.arrived) + "; the first 5 and every 100th are said)");
}

/* One pair's world-server side for this frame: back-pressure, what waited for the joiner's Steam link, read, edges, hand on (or
   wait while that link is not up: relaybridge.h JoinerSideStep). */
void ServerSide(Pair* p, DWORD now)
{
    if (!p->closeWhy.empty() || p->local == 0) return;
    long long steamWait = 0;
    const ITransport* s = g_listener->Link(p->link);
    if (s != 0) s->SendQueued(0, &steamWait);
    const int step = relaybridge::JoinerSideStep(!p->held, s != 0 && s->State() == LINK_UP);
    if (relaybridge::FarBehind(steamWait))
    {
        ++g_bs.farBehind;
        p->closeWhy = "the joiner fell too far behind: " + N(steamWait) + " bytes waited toward it on its Steam link (the limit is "
                      + N(relaybridge::kFarBehindBytes) + ")";
        return;
    }
    if (step == relaybridge::kJoinerPass && !p->waitJoiner.empty())
    {
        HandOnWaiting(p, now);   /* before anything read this frame: the order the world server sent them in */
        if (!p->closeWhy.empty()) return;
    }
    const bool slow = relaybridge::SlowNext(p->slow, steamWait);
    if (slow && !p->slow)
    {
        ++g_bs.slowEpisodes;
        if (relaybridge::LogThis(g_bs.slowEpisodes))
            DebugLog("[NET] relay bridge: link " + N(p->link) + " - " + N(steamWait) + " bytes wait toward the joiner on its Steam link; its world-server"
                     " connection is read every " + N((long long)relaybridge::kSlowPollMs) + " ms until under " + N(relaybridge::kFastAtBytes)
                     + " bytes, so the world server holds the rest (episode " + N(g_bs.slowEpisodes) + "; the first 5 and every 100th are said)");
    }
    p->slow = slow;

    std::vector<Message> out;
    const bool paced = p->slow || step == relaybridge::kJoinerHold;   /* a Steam link still connecting takes nothing: read as a slow one */
    if (relaybridge::ReadLocalNow(paced, (unsigned int)(now - p->lastReadMs))) { p->local->Poll(&out); p->lastReadMs = now; }
    else p->local->Flush();   /* the joiner's messages still leave; only reading (and so acknowledging) waits */
    const LinkState st = p->local->State();

    if (!p->localUp && st == LINK_UP)
    {
        p->localUp = true;
        ++g_bs.serverUp;
        if (!SendJoinerId(p)) return;   /* the joiner's arrival is still held: closing the pair refuses it */
        p->held = false;
        if (!g_listener->AcceptHeld(p->link))
        {
            p->closeSteam = false;   /* the listener closed the arrival and freed its place */
            p->closeWhy = "its world-server connection came up in " + N((long long)(DWORD)(now - p->dialMs)) + " ms but Steam did not accept the"
                          " joiner's held arrival (closed with end reason " + N(steamlisten::kEndListenerNoRoom) + ")";
            return;
        }
        ++g_bs.accepted;
        p->acceptMs = now;
        DebugLog("[NET] relay bridge: link " + N(p->link) + " - its world-server connection is up in " + N((long long)(DWORD)(now - p->dialMs))
                 + " ms; the joiner's Steam link is accepted now and its messages go to the world server from its first one; what the world"
                 " server sends waits until that link is up");
    }
    if (step == relaybridge::kJoinerPass)
        for (size_t k = 0; k < out.size(); ++k) ToJoiner(p, out[k]);   /* before any close below: a refusal's words still reach the joiner */
    else if (!out.empty()) WaitForJoiner(p, out);   /* the joiner's Steam link is not up (this frame's accept included) */
    if (!p->closeWhy.empty()) return;
    if (p->localUp && st != LINK_UP)
    {
        p->closeWhy = "its world-server connection ended";
        return;
    }
    if (!p->localUp && (st == LINK_DOWN || st == LINK_FAILED || relaybridge::LocalConnectLate((unsigned int)(now - p->dialMs))))
    {
        ++g_bs.serverFailed;
        p->closeWhy = (st == LINK_DOWN || st == LINK_FAILED) ? "the world server on 127.0.0.1:" + N(g_storePortOpen) + " refused or dropped its connection"
                                                             : "the world server on 127.0.0.1:" + N(g_storePortOpen) + " did not answer in "
                                                               + N((long long)relaybridge::kLocalConnectMs) + " ms";
        return;
    }
    long long localWait = 0;
    if (p->localUp && p->local->SendQueued(0, &localWait) && relaybridge::FarBehind(localWait))
    {
        ++g_bs.farBehind;
        p->closeWhy = "the world server fell too far behind: " + N(localWait) + " bytes waited toward it (the limit is " + N(relaybridge::kFarBehindBytes) + ")";
    }
}

void PumpBridge(DWORD now)
{
    std::vector<SteamListenEvent> ev;
    std::vector<Message> in;
    g_listener->Poll(&ev, &in);
    std::string why;
    if (g_listener->Stopped(&why)) { CloseBridge("a Steam call faulted (" + why + ")"); return; }

    /* the joiners' messages first, in the order Steam handed them: a link that ended this frame hands its last ones here */
    for (size_t k = 0; k < in.size(); ++k)
    {
        Pair* p = FindPair(in[k].peer);
        if (p == 0) { ++g_bs.stray; continue; }
        ToServer(p, in[k]);
    }
    for (size_t k = 0; k < ev.size(); ++k)
    {
        const SteamListenEvent& e = ev[k];
        if (e.kind == SL_ARRIVED) NewPair(e.link, now);   /* SL_ACCEPTED does not come: the bridge holds every arrival */
        else if (e.kind == SL_REFUSED)
        {
            ++g_bs.refused;
            if (relaybridge::LogThis(g_bs.refused))
                DebugLog("[NET] relay bridge: a Steam joiner was turned away - end reason " + N(e.endReason) + (e.endReason == steamlisten::kEndListenerFull
                         ? " (the bridge holds its cap of " + N(g_listener->Cap()) + ")" : std::string(" (this game could not hold another Steam link)"))
                         + "; the first 5 and every 100th are said");
        }
        else if (e.kind == SL_ENDED)
        {
            Pair* p = FindPair(e.link);
            if (p == 0) continue;
            p->closeSteam = false;   /* the listener has freed its place already */
            if (p->closeWhy.empty())
                p->closeWhy = "the joiner's Steam link ended (Steam end reason " + N(e.endReason) + (e.wasUp ? "" : ", before it came up") + ")";
        }
    }
    for (size_t i = 0; i < g_pairs.size(); ++i) ServerSide(g_pairs[i], now);
    for (size_t i = g_pairs.size(); i > 0; --i)
        if (!g_pairs[i - 1]->closeWhy.empty()) ClosePairAt(i - 1, true, steamlisten::kEndHostServerDown);
    if (g_listener != 0) g_listener->Flush();
}

}   /* namespace */

void RelayBridgeTick()
{
    if (g_exitClosed) return;   /* closed for the exit at the quit; the frames still run until the game shuts Steam down */
    const DWORD now = ::GetTickCount();
    if (g_lastTickMs != 0)
    {
        const unsigned int gap = (unsigned int)(DWORD)(now - g_lastTickMs);
        const int up = LinksUp();
        if (relaybridge::StallWorthALine(gap, up))
        {
            ++g_bs.stalls;
            if ((long long)gap > g_bs.stallMaxMs) g_bs.stallMaxMs = (long long)gap;
            if (relaybridge::LogThis(g_bs.stalls))
                DebugLog("[NET] relay bridge: this game's frame stood still " + N((long long)gap) + " ms with " + N((long long)up)
                         + " Steam joiner(s) linked - nothing was relayed meanwhile (a world load or a save here holds every Steam joiner; stall "
                         + N(g_bs.stalls) + ", the first 5 and every 100th are said)");
        }
    }
    g_lastTickMs = now;

    const std::string storeAddr = ConfigStoreAddr();
    const unsigned short storePort = ConfigStorePort();
    const int mode = ConfigRelayBridgeMode();
    const unsigned short ipPort = (mode == coopcfg::kRelayBridgeIp) ? ConfigRelayBridgePort() : (unsigned short)0;
    const int want = relaybridge::BridgeWant(ConfigRole() == kRoleHost, relaybridge::IsLoopbackHost(storeAddr), storePort,
                                             mode == coopcfg::kRelayBridgeOff, ipPort, SteamStateNow());
    if (want != g_want)
    {
        if (want == relaybridge::kWantNoSteam)
            DebugLog("[NET] relay bridge: this game hosts a world server on this PC but Steam networking is not available (see the [NET] steam: line)"
                     " - joiners reach it by its address only");
        else if (want == relaybridge::kWantBadPort)
            ErrorLog("[NET] relay bridge: relaybridge=ip:" + N(ipPort) + " (TEST ONLY) names the world server's own port - the bridge stays closed");
        else if (want == relaybridge::kWantClosed && g_want == relaybridge::kWantOpen && mode == coopcfg::kRelayBridgeOff)
            DebugLog("[NET] relay bridge: relaybridge=off - Steam joiners are not bridged");
        g_want = want;
    }
    if (g_listener != 0 && (want != relaybridge::kWantOpen || storePort != g_storePortOpen || ipPort != g_ipPortOpen || storeAddr != g_storeAddrOpen))
        CloseBridge(want == relaybridge::kWantOpen ? std::string("the world server it bridges to changed")
                                                   : std::string("this game no longer hosts a world server on this PC, or Steam is gone"));
    if (want != relaybridge::kWantOpen) { g_openFailed = false; return; }
    if (g_listener == 0)
    {
        if (g_openFailed && (DWORD)(now - g_openFailedMs) < (DWORD)relaybridge::kOpenRetryMs) return;
        if (!OpenBridge(storeAddr, storePort, ipPort)) { g_openFailed = true; g_openFailedMs = now; return; }
        g_openFailed = false;
    }
    PumpBridge(now);
}

void RelayBridgeShutdown()
{
    g_exitClosed = true;
    if (g_listener == 0 && g_pairs.empty()) return;
    CloseBridge("this game is exiting");
}

int RelayBridgeSteamFull(int* cap)
{
    if (cap != 0) *cap = 0;
    if (g_listener == 0 || !g_listener->IsOpen()) return 0;
    const int c = g_listener->Cap();
    if (cap != 0) *cap = c;
    return (c > 0 && g_listener->LinkCount() >= c) ? 1 : 0;
}

std::string RelayBridgeReportToken()
{
    return "relayBridge[open,linksNow,linksMax,accepted,refused,serverUp,serverFailed,ended,toServer,toJoiner,bytesToServer,bytesToJoiner,"
           "slowEpisodes,farBehind,unrelDropped,relFailed,stray,stalls,stallMaxMs,opened,openFailed,arrived,heldRefused,waitedMax,idKnown,idUnchecked,idNoToken]="
           + N(g_listener != 0 ? 1 : 0) + "," + N((long long)g_pairs.size()) + "," + N(g_bs.linksMax) + "," + N(g_bs.accepted) + ","
           + N(g_bs.refused) + "," + N(g_bs.serverUp) + "," + N(g_bs.serverFailed) + "," + N(g_bs.ended) + "," + N(g_bs.toServer) + ","
           + N(g_bs.toJoiner) + "," + N(g_bs.bytesToServer) + "," + N(g_bs.bytesToJoiner) + "," + N(g_bs.slowEpisodes) + ","
           + N(g_bs.farBehind) + "," + N(g_bs.unrelDropped) + "," + N(g_bs.relFailed) + "," + N(g_bs.stray) + ","
           + N(g_bs.stalls) + "," + N(g_bs.stallMaxMs) + "," + N(g_bs.opened) + "," + N(g_bs.openFailed) + "," + N(g_bs.arrived) + "," + N(g_bs.heldRefused) + "," + N(g_bs.waitedMax) + "," + N(g_bs.idKnown) + "," + N(g_bs.idUnchecked) + "," + N(g_bs.idNoToken)
           + " relayRoute[direct,steam,steamIp,steamUp,steamClosed]=" + N(g_routeDials[relaybridge::kRouteDirect]) + ","
           + N(g_routeDials[relaybridge::kRouteSteam]) + "," + N(g_routeDials[relaybridge::kRouteSteamIpTest]) + "," + N(g_routeSteamUp) + ","
           + N(g_routeSteamClosed);
}

int RelayRouteFor(const std::string& address)
{
    return relaybridge::RouteFor(address, ConfigRelayViaPort());
}

ITransport* RelayRouteMake(int route)
{
    ITransport* t = CreateTransport(route == relaybridge::kRouteDirect ? BACKEND_DIRECT : BACKEND_STEAM);
    if (t != 0 && route == relaybridge::kRouteSteamIpTest) SteamTransportSetIpTest(t, true);   /* plain UDP to 127.0.0.1 with allow-without-auth */
    return t;
}

void RelayRouteTarget(int route, std::string* address, unsigned short* port)
{
    if (address == 0 || port == 0) return;
    if (route == relaybridge::kRouteSteamIpTest) { *address = "127.0.0.1"; *port = ConfigRelayViaPort(); return; }
    unsigned long long id = 0;
    if (route == relaybridge::kRouteSteam && relaybridge::SteamAddressId(*address, &id)) *address = relaybridge::SteamAddressText(id);
}

void RelayRouteNoteDial(int route)
{
    if (route < relaybridge::kRouteDirect || route > relaybridge::kRouteSteamIpTest) route = relaybridge::kRouteDirect;
    g_lastRoute = route;
    ++g_routeDials[route];
    if (route == relaybridge::kRouteDirect) return;   /* the store's own connecting line names the direct route */
    if (relaybridge::LogThis(g_routeDials[route]))
        DebugLog("[NET] route: the world server is dialled by " + std::string(relaybridge::RouteWord(route)) + " (dial " + N(g_routeDials[route])
                 + " by this route; the first 5 and every 100th are said)"
                 + (route == relaybridge::kRouteSteamIpTest ? " to 127.0.0.1:" + N(ConfigRelayViaPort()) : std::string()));
}

std::string RelayRouteNoteUp(ITransport* link)
{
    if (link == 0) return "none";
    if (link->Backend() != BACKEND_STEAM) return relaybridge::RouteWord(relaybridge::kRouteDirect);
    ++g_routeSteamUp;
    SteamLinkFacts f;
    SteamTransportFacts(link, &f);
    return std::string(relaybridge::RouteWord(g_lastRoute)) + ", relayed=" + N(f.relayed) + ", up in " + N((long long)f.upMs) + " ms";
}

void RelayRouteNoteEnd(ITransport* link)
{
    if (link == 0 || link->Backend() != BACKEND_STEAM) return;
    ++g_routeSteamClosed;
    if (!relaybridge::LogThis(g_routeSteamClosed)) return;
    SteamLinkFacts f;
    const bool have = SteamTransportFacts(link, &f);
    DebugLog(std::string("[NET] route: the ") + relaybridge::RouteWord(g_lastRoute) + " link to the world server is closed here (it was "
             + LinkStateName(link->State()) + ") - Steam's last end reason: "
             + (have && f.endReason != 0 ? N(f.endReason) + " '" + relaysplit::HideIdNumbers(f.endText) + "'" : std::string("none (Steam had not ended it)"))
             + (have && f.stopped ? "; Steam use stopped in this game: " + f.stopWhy : std::string())
             + " (closed " + N(g_routeSteamClosed) + "; the first 5 and every 100th are said)");
}

}   /* namespace net */
}   /* namespace coop */
