/* net/steam_transport.h - the Steam backend (ITransport over Steam Networking Sockets) and the relay echo test's controls.
   Main thread only, like every transport. steam_transport.cpp says what it does and how it reaches Steam. */
#ifndef SW_NET_STEAM_TRANSPORT_H
#define SW_NET_STEAM_TRANSPORT_H

#include "transport.h"

#include <string>
#include <vector>

namespace coop {
namespace net {

/* A new Steam transport (CreateTransport(BACKEND_STEAM) returns one). Host/Join refuse with a reason where Steam is absent. */
ITransport* NewSteamTransport();

/* RELAY TEST ONLY. Puts `t` (a Steam transport) in IP test mode: Host(port) listens on 127.0.0.1:port and Join("127.0.0.1", port)
   dials it over ordinary UDP, instead of Steam's P2P virtual port 0. allowWithoutAuth sets Steam's IP_AllowWithoutAuth (a dev value in
   Valve's words) on the sockets made after this call. false = `t` is not a live Steam transport. */
bool SteamTransportSetIpTest(ITransport* t, bool allowWithoutAuth);

/* RELAY TEST ONLY. Joins two Steam transports with CreateSocketPair over network loopback (the fake lag / loss apply): `a` is the
   joining end (peer id 0), `b` the hosting end. Both come UP at a later Poll. */
bool SteamTransportMakePair(ITransport* a, ITransport* b, std::string* errOut);

/* RELAY TEST ONLY. Steam's GLOBAL fake lag (ms) and loss (percent) on every packet this game sends over Steam sockets. */
bool SteamSetFakeLagLoss(int lagMs, int lossPct, std::string* errOut);

/* What one Steam transport knows about its link, for the relay test's lines. */
struct SteamLinkFacts
{
    unsigned int upMs;       /* ms from the dial (or the accept) to UP, of the last link that came up */
    int          relayed;    /* 1 = the link went through a relay when it came up (Steam's Relayed flag, or a relay data centre named) */
    int          endReason;  /* Steam's end reason (ESteamNetConnectionEnd) of the last link that ended; 0 = none yet */
    std::string  endText;    /* Steam's end text of it, printable characters only */
    int          stopped;    /* 1 = a fault stopped this transport for good */
    std::string  stopWhy;
    SteamLinkFacts() : upMs(0), relayed(0), endReason(0), stopped(0) {}
};
bool SteamTransportFacts(ITransport* t, SteamLinkFacts* out);

/* THE STEAM LISTENER - a host's many Steam links on one listen socket. Each connection it accepts is its own link (a link id from 1,
   not reused while the listener lives) with the transport's send queue, split and rejoin, send-rate step and end handling, up to its
   cap (steamlisten::kLinkCap, 32; SetCap lowers it for a test). One arriving past the cap is closed at once with end reason
   steamlisten::kEndListenerFull (1005). Its P2P links go through Valve's relay only. Main thread only. Poll it every
   frame: it polls every link, hands up their messages (Message::peer = the link id) and what happened (a link accepted, an arrival
   refused, a link ended - its place is free again). */
enum SteamListenEventKind { SL_ACCEPTED = 1, SL_REFUSED = 2, SL_ENDED = 3, SL_ARRIVED = 4 /* held (SetHoldArrivals), not accepted */ };
struct SteamListenEvent
{
    int          kind;        /* SL_* */
    unsigned int link;        /* the link id; 0 for a refused arrival */
    int          endReason;   /* SL_REFUSED: the reason it was closed with; SL_ENDED: Steam's end reason of the link */
    int          wasUp;       /* SL_ENDED: 1 = the link had come up */
    SteamListenEvent() : kind(0), link(0), endReason(0), wasUp(0) {}
};

class SteamListener
{
public:
    virtual ~SteamListener() {}   /* closes as Close() does */
    /* ipTestPort 0 = Steam P2P virtual port 0, relay only (the option is read back from Steam; any value but 0 closes it: false). Non-zero = RELAY TEST ONLY: 127.0.0.1:<port> over ordinary UDP with
       allow-without-auth (the relay-only rule does not apply to it). false + a reason where Steam is absent or refused. */
    virtual bool Open(unsigned short ipTestPort, std::string* errOut) = 0;
    /* Every link closed gracefully and every held arrival refused, then the listen socket (Steam closes what is still lingering with it). */
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    /* The cap for arrivals from now on: 1..steamlisten::kLinkCap; anything else = kLinkCap. Links already held stay. */
    virtual void SetCap(int cap) = 0;
    virtual int  Cap() const = 0;
    /* Steam's connected-link timeout (TimeoutConnected) for the links accepted from the next Open on; 0 = the transport's own 10 s.
       The host's bridge gives its links the game link's term, so a world load on either side does not end them. */
    virtual void SetLinkTimeoutMs(unsigned int ms) = 0;
    /* HOLD (off by default: an arrival within the cap is accepted at once). On: an arrival within the cap takes a place (it counts
       toward the cap) and a link id but is NOT accepted - Poll reports SL_ARRIVED and the owner calls AcceptHeld once it can serve
       the link, or RefuseHeld. Until then the link has no transport (Link() 0, Send false) and the joiner's Steam connection is
       still connecting; a held arrival the joiner gives up on is closed here and reported as SL_ENDED (wasUp 0). */
    virtual void SetHoldArrivals(bool on) = 0;
    /* Accepts a held arrival: true = it is now a link like any accepted one. false = no such held arrival, or Steam would not
       accept it (then it is closed with steamlisten::kEndListenerNoRoom and its place is free). */
    virtual bool AcceptHeld(unsigned int link) = 0;
    /* Closes a held arrival with `reason` (steamlisten::kEnd*) and its text (steamlisten::EndText); its place is free. false = no
       such held arrival. */
    virtual bool RefuseHeld(unsigned int link, int reason) = 0;
    virtual int  LinkCount() const = 0;
    virtual void Poll(std::vector<SteamListenEvent>* events, std::vector<Message>* in) = 0;
    virtual bool Send(unsigned int link, MsgType type, const char* data, size_t len, Channel ch) = 0;
    virtual void Flush() = 0;
    /* Closes one link here (linger: Steam still sends what it holds); its place is free at once. A held arrival is refused with
       steamlisten::kEndListenerNoRoom. false = no such link. */
    virtual bool CloseLink(unsigned int link, bool linger) = 0;
    /* One link's own transport, for its numbers (State, SendQueued, ByteCounts, RoundTripMs, HostSilenceMs ...); 0 = no such link.
       Send, Poll and close go through the listener only. */
    virtual const ITransport* Link(unsigned int link) const = 0;
    virtual bool LinkFacts(unsigned int link, SteamLinkFacts* out) const = 0;
    /* The link's remote Steam id as Steam reported it when the link came up over P2P - for the host's allow / block rules. NEVER
       logged. false = no such link, not up yet, not a Steam id, Steam marked the connection unauthenticated, or the relay test's
       plain-UDP route (an id Steam did not check). */
    virtual bool LinkSteamId(unsigned int link, unsigned long long* id64) const = 0;
    /* true = a fault stopped it for good (the reason in *why). */
    virtual bool Stopped(std::string* why) const = 0;
};
SteamListener* NewSteamListener();

/* "steamLink[up,ms,sent,recv,bytesOut,bytesIn,pieces,rejoined,queuedMax,refusedBig,endReason,rateNow,rateMin,rateMax,rateChanges]=... steamLinkEv[...]=..." - every Steam
   transport of this game summed, and "steamListen[...]=..." for the listeners, for the [net] REPORT line (printed inside the Steam
   probe's token). */
std::string SteamLinkReportToken();

/* P2P listen sockets of this game whose relay-only option (P2P_Transport_ICE_Enable) read back from Steam as 0 - the relay test's
   check that Steam kept the option. */
long long SteamIceOffRead0();

/* net/steam_probe.cpp: the relay network's status read now (GetRelayNetworkStatus) as one log fragment, for the end line of a Steam
   link that never connected. A fault in the read is counted on the probe (every Steam link stops). */
std::string SteamRelayStatusText();

/* net/relay_test.cpp: once per frame from SteamProbeTick; returns at its first line unless relaytest= is set. */
void RelayTestTick();
/* The relay test's state in one word: off, waiting, listening, dialing, running, passed, failed, listened. */
const char* RelayTestWord();

}   /* namespace net */
}   /* namespace coop */

#endif   /* SW_NET_STEAM_TRANSPORT_H */
