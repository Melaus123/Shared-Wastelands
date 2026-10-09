/* src/common/upnpplan.h - T-53: THE PURE HALF OF THE AUTOMATIC ROUTER PORT OPENING.
 *
 * When this game hosts, the world server it starts (SharedWastelandsServer.exe) listens on ONE UDP port, the PORT box's
 * number. src/coop-plugin/upnp.cpp asks the home router to forward that port to this computer while hosting lasts, and
 * to stop forwarding it when hosting ends.  It asks by up to four ROUTES, in this order (FirstRoute / NextRoute):
 *   igd      the mod's own UPnP IGD ask: an SSDP search sent from the home adapter's address, the router's device
 *            description, then SOAP AddPortMapping with a time-limited lease (src/common/routerwire.h);
 *   pcp      PCP (RFC 6887) to the router's UDP port 5351, for routers without UPnP;
 *   natpmp   NAT-PMP (RFC 6886), only when the router answered PCP in NAT-PMP's format;
 *   windows  Windows' own UPnP interface (NATUPnP), the last fallback.
 * The first route that settles the port (forwarded, already forwarded here, or forwarded to another computer) ends the
 * ask.  Every decision lives here, so the offline suite (src/coop-test) checks the same rules the plugin compiles:
 *   MapOnHostStart                    which HOST presses ask the router at all.
 *   Description / IsOurDescription    the text the router's port list shows for the entry, and how our own entry is known.
 *   FirstRoute / NextRoute / RouteSettles / FoldOutcome
 *                                     the route order, when it stops, and what the routes' answers add up to.
 *   FindRouterOutcome                 Windows' answer to "find the router" -> go on / no router / failed (windows route).
 *   ClassifyExisting / ActionFor      what an entry already on the router for that port means, and what is done about it.
 *   RemoveAtEnd                       whether the entry is taken away when hosting ends.
 *   State, HostStart, HostEnd, NextStep, AfterMap, AfterRenew, AfterUnmap, RenewAfterMs, RenewWaitMs
 *                                     the background worker's state, what it does next, and when a lease is renewed.
 *   OutcomeWord / StepWord            one word each, for the [UPNP] log lines.
 *
 * NOTHING HERE IS SHOWN TO THE PLAYER (owner decision 382): the router work is background work, so success and failure
 * only reach the log and the REPORT line; the router help text on the hosting screen is unchanged.
 *
 * PURE: no Windows.h, no logging, no globals. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_UPNPPLAN_H
#define COOP_COMMON_UPNPPLAN_H
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

#include <string>

namespace coopupnp {

/* The world server's one port is UDP (ENet). */
const char* const kProtocol = "UDP";

/* The start of every entry this mod puts on a router. The router's own port list shows it to whoever looks there. */
const char* const kDescPrefix = swnames::kServerTitle;

/* THE LEASE.  The igd, pcp and natpmp routes ask for an entry that lasts kAskLeaseSeconds and renew it while hosting
   lasts (RenewAfterMs), asking kAskLeaseSeconds every time, so a leased entry a crashed game leaves behind disappears
   from the router on its own within the hour.  A router that only takes permanent entries (IGD fault 725) gets lease 0,
   and such an entry stays until it is removed.  The windows route (NATUPnP's
   IStaticPortMappingCollection::Add) takes no lease at all: kWindowsLeaseSeconds, permanent until removed or the router
   restarts.  Either way the end of hosting removes the entry, and an entry a crashed game left on the same port,
   pointing at this computer with our description, is taken over by the next host on that port (ActionFor gives
   kActKeep, RemoveAtEnd gives true) and removed when that hosting ends. */
const long kAskLeaseSeconds     = 3600;
const long kWindowsLeaseSeconds = 0;
/* A lease the router grants is held to [kMinLeaseSeconds, kAskLeaseSeconds] for renewing (ClampLease): a router granting
   a second is not asked again every half second, and an hour's worth of milliseconds never overflows. */
const long kMinLeaseSeconds     = 120;

/* The lease the worker keeps for a granted `seconds`: 0 (permanent) stays 0; anything else is held to
   [kMinLeaseSeconds, kAskLeaseSeconds]. */
inline long ClampLease(long seconds)
{
    if (seconds <= 0) return 0;
    if (seconds < kMinLeaseSeconds) return kMinLeaseSeconds;
    if (seconds > kAskLeaseSeconds) return kAskLeaseSeconds;
    return seconds;
}

/* A renewal the router did not accept is asked again after this long (or at half the lease, when that is sooner). */
const unsigned long kRenewRetryMs = 300000ul;

/* At the game's exit the removal gets at most this long. The game window is already gone then, so the wait is unseen;
   a router that never answers cannot hold the exit longer, and its entry then stays until the next host on that port. */
const unsigned int kQuitWaitMs = 3000;

inline bool PortOk(long port) { return port >= 1 && port <= 65535; }

inline std::string PortText(long port)
{
    if (port <= 0) return "0";
    char buf[16];
    int n = 0;
    while (port > 0 && n < 15) { buf[n++] = (char)('0' + (int)(port % 10)); port /= 10; }
    std::string s;
    while (n > 0) s += buf[--n];
    return s;
}

/* "Shared Wastelands world server (UDP 27016)". */
inline std::string Description(long port)
{
    return std::string(kDescPrefix) + " (" + kProtocol + " " + PortText(port) + ")";
}

/* Our entry, whatever port it names (the prefix, compared exactly). */
inline bool IsOurDescription(const std::string& d)
{
    const std::string p(kDescPrefix);
    return d.size() >= p.size() && d.compare(0, p.size(), p) == 0;
}

/* WHICH HOST PRESSES ASK THE ROUTER. startResult is the panel's PanelStartNotebook answer: 1 = a new world server process
   was started on this computer (ask), 2 = the one this game started earlier still runs (its start already asked),
   0 = none was started (nothing listens here, or the world server is on another computer). */
inline bool MapOnHostStart(int startResult) { return startResult == 1; }

/* What one attempt came to. Each is counted once (the REPORT's upnp[...] field). */
enum
{
    kOutNone     = 0,   /* not decided yet - go on */
    kOutMapped   = 1,   /* the router accepted our new entry */
    kOutAlready  = 2,   /* the router already forwards that port to this computer and port */
    kOutFailed   = 3,   /* Windows' UPnP is missing, the router refused, or the port is forwarded somewhere else */
    kOutNoRouter = 4    /* no UPnP router was found on the home network (none there, UPnP off on it, or discovery off here) */
};

/* THE ROUTES, in the order they are tried. */
enum { kRouteNone = 0, kRouteIgd = 1, kRoutePcp = 2, kRoutePmp = 3, kRouteWindows = 4, kRouteCount = 5 };

inline const char* RouteWord(int route)
{
    switch (route)
    {
    case kRouteNone:    return "none";
    case kRouteIgd:     return "igd";
    case kRoutePcp:     return "pcp";
    case kRoutePmp:     return "natpmp";
    case kRouteWindows: return "windows";
    default:            return "other";
    }
}

/* What one route's attempt came to. */
enum
{
    kRrMapped       = 1,   /* the router accepted our entry */
    kRrAlready      = 2,   /* the router already forwards that port to this computer and port */
    kRrElsewhere    = 3,   /* the port is forwarded to another computer or program: left as it is, never overwritten */
    kRrRefused      = 4,   /* a router answered this route and refused (or answered in a way that cannot be read) */
    kRrNoAnswer     = 5,   /* nothing answered this route */
    kRrUnavailable  = 6,   /* this route could not be tried here (no router address known, Windows' UPnP missing) */
    kRrOtherVersion = 7    /* pcp only: the router answered in NAT-PMP's format - it speaks NAT-PMP instead */
};

inline const char* RouteResultWord(int rr)
{
    switch (rr)
    {
    case kRrMapped:       return "mapped";
    case kRrAlready:      return "already";
    case kRrElsewhere:    return "heldElsewhere";
    case kRrRefused:      return "refused";
    case kRrNoAnswer:     return "noAnswer";
    case kRrUnavailable:  return "unavailable";
    case kRrOtherVersion: return "otherVersion";
    default:              return "other";
    }
}

/* A route's answer that settles the port: nothing further is asked. */
inline bool RouteSettles(int rr) { return rr == kRrMapped || rr == kRrAlready || rr == kRrElsewhere; }

inline int FirstRoute() { return kRouteIgd; }

/* The route after `route` gave `rr` (kRouteNone = stop).  natpmp is tried only when the router answered pcp in
   NAT-PMP's format: both use the same port 5351, so silence or a refusal there is not asked again in the older format. */
inline int NextRoute(int route, int rr)
{
    if (RouteSettles(rr)) return kRouteNone;
    if (route == kRouteIgd) return kRoutePcp;
    if (route == kRoutePcp) return (rr == kRrOtherVersion) ? kRoutePmp : kRouteWindows;
    if (route == kRoutePmp) return kRouteWindows;
    return kRouteNone;
}

/* What the routes' answers so far add up to (start from kOutNoRouter): a settling answer decides it; a refusal makes it
   failed; silence or a route that could not be tried leaves it as it was (no router, unless one already refused). */
inline int FoldOutcome(int soFar, int rr)
{
    if (rr == kRrMapped)    return kOutMapped;
    if (rr == kRrAlready)   return kOutAlready;
    if (rr == kRrElsewhere || rr == kRrRefused) return kOutFailed;
    return soFar;
}

inline const char* OutcomeWord(int outcome)
{
    switch (outcome)
    {
    case kOutNone:     return "none";
    case kOutMapped:   return "mapped";
    case kOutAlready:  return "already";
    case kOutFailed:   return "failed";
    case kOutNoRouter: return "noRouter";
    default:           return "other";
    }
}

/* FIND THE ROUTER (the windows route). homeAddrFound: this computer has a home-network address to forward to (no address = no network with a
   router). comReady: CoInitializeEx succeeded on the router thread. created: CoCreateInstance(UPnPNAT) succeeded. hrCollection / gotCollection: get_StaticPortMappingCollection's
   HRESULT and whether it handed back a collection - Windows answers S_OK with NO collection when no router answered its
   search, and an error when the search could not run. kOutNone = a router answered, go on. */
inline int FindRouterOutcome(bool homeAddrFound, bool comReady, bool created, long hrCollection, bool gotCollection)
{
    if (!homeAddrFound) return kOutNoRouter;
    if (!comReady || !created) return kOutFailed;
    if (hrCollection < 0 || !gotCollection) return kOutNoRouter;
    return kOutNone;
}

/* AN ENTRY ALREADY ON THE ROUTER FOR THIS PORT. found: get_Item gave one. itsClient / itsInternalPort: where it forwards
   ("" / 0 when the router would not say). ourClient / ourPort: this computer's home-network address and the world server's
   port. */
enum { kExistNone = 0, kExistThisTarget = 1, kExistOtherTarget = 2 };
inline int ClassifyExisting(bool found, const std::string& itsClient, long itsInternalPort, const std::string& ourClient, long ourPort)
{
    if (!found) return kExistNone;
    if (!ourClient.empty() && itsClient == ourClient && itsInternalPort == ourPort) return kExistThisTarget;
    return kExistOtherTarget;
}

/* What is done about it: none -> add ours; this computer and port already -> keep it (nothing to add); forwarded
   anywhere else -> leave it as it is (it is another computer's or another program's, and never overwritten). */
enum { kActAdd = 0, kActKeep = 1, kActLeave = 2 };
inline int ActionFor(int existing)
{
    if (existing == kExistNone) return kActAdd;
    if (existing == kExistThisTarget) return kActKeep;
    return kActLeave;
}

/* Is the entry taken away when hosting ends? One we added: yes. One we kept: only when it carries our description (a
   crashed game's leftover) - an entry the player made by hand on the router stays theirs. One we left: never ours. */
inline bool RemoveAtEnd(int action, const std::string& existingDescription)
{
    if (action == kActAdd) return true;
    if (action == kActKeep) return IsOurDescription(existingDescription);
    return false;
}

/* THE WORKER'S STATE. want: the port hosting needs open now (0 = not hosting). held: the port the router forwards for
   us now (0 = none). tried: the port already asked for in this hosting (one attempt per world server start - a router
   that said no is not asked again and again). removeAtEnd: RemoveAtEnd's answer for held.  route: the route that holds
   it (renewed and removed by that route).  lease: the seconds the router granted (0 = permanent, never renewed).
   renewDue: the lease's renewal time has come (set by the worker's timer, cleared by AfterRenew). */
struct State
{
    long want;
    long held;
    long tried;
    bool removeAtEnd;
    int  route;
    long lease;
    bool renewDue;
    State() : want(0), held(0), tried(0), removeAtEnd(false), route(kRouteNone), lease(0), renewDue(false) {}
};

/* A world server was started on this computer on `port` (once per process start). */
inline void HostStart(State* s, long port)
{
    if (!PortOk(port)) return;
    s->want = port;
    s->tried = 0;
}

/* Hosting ended: the world server stopped, or the game is quitting. */
inline void HostEnd(State* s)
{
    s->want = 0;
    s->tried = 0;
}

enum
{
    kStepIdle   = 0,   /* nothing to do - wait for a change */
    kStepMap    = 1,   /* ask the router for `want` */
    kStepUnmap  = 2,   /* remove `held` from the router */
    kStepForget = 3,   /* stop holding `held` without touching the router (an entry that is not ours to remove) */
    kStepRenew  = 4    /* ask the router again for `held`, so its lease does not run out while hosting lasts */
};

inline const char* StepWord(int step)
{
    switch (step)
    {
    case kStepIdle:   return "idle";
    case kStepMap:    return "map";
    case kStepUnmap:  return "unmap";
    case kStepForget: return "forget";
    case kStepRenew:  return "renew";
    default:          return "other";
    }
}

/* What the worker does next. A held port that hosting no longer wants (ended, or a new world server on another port)
   goes first; then the wanted port, once; then a held lease whose renewal time has come. */
inline int NextStep(const State& s)
{
    if (s.held != 0 && s.held != s.want) return s.removeAtEnd ? kStepUnmap : kStepForget;
    if (s.want != 0 && s.held == 0 && s.tried != s.want) return kStepMap;
    if (s.held != 0 && s.held == s.want && s.lease > 0 && s.renewDue) return kStepRenew;
    return kStepIdle;
}

/* After an attempt for `port` came to `outcome` by `route` with a granted `lease`: mapped / already -> held, anything
   else -> not held. Either way it was tried, so it is not asked again until the next world server start. */
inline void AfterMap(State* s, long port, int outcome, bool removeAtEnd, int route = kRouteNone, long lease = 0)
{
    s->tried = port;
    s->renewDue = false;
    if (outcome == kOutMapped || outcome == kOutAlready)
    {
        s->held = port;
        s->removeAtEnd = removeAtEnd;
        s->route = route;
        s->lease = ClampLease(lease);
    }
    else
    {
        s->held = 0;
        s->removeAtEnd = false;
        s->route = kRouteNone;
        s->lease = 0;
    }
}

/* After a renewal: the router's newly granted lease when it accepted (a router may grant another length), the old one
   kept when it did not (the entry may still stand; the next renewal comes sooner, RenewAfterMs). */
inline void AfterRenew(State* s, bool ok, long grantedLease)
{
    s->renewDue = false;
    if (ok && grantedLease > 0) s->lease = ClampLease(grantedLease);
}

/* A renewal found the port forwarded elsewhere now (IGD fault 718, or PCP / NAT-PMP offering another port - the router
   restarted and another device took it): the port is no longer ours.  It stays held while hosting wants it, but it is
   never renewed again and never removed at the end (NextStep gives forget). */
inline void AfterRenewLost(State* s)
{
    s->renewDue = false;
    s->lease = 0;
    s->removeAtEnd = false;
}

/* THE REMOVAL AT HOSTING END (igd): the router is asked what it forwards on the port first, and our entry is deleted only
   when it still points at this computer and port and carries our description; nothing there = already gone (its lease
   ran out, or the router restarted); anything else is another device's now and is left as it is. */
enum { kDelRemove = 0, kDelGone = 1, kDelNotOurs = 2 };
inline int DeleteAction(bool found, const std::string& itsClient, long itsPort, const std::string& itsDesc,
                        const std::string& ourClient, long ourPort)
{
    if (!found) return kDelGone;
    if (ClassifyExisting(found, itsClient, itsPort, ourClient, ourPort) == kExistThisTarget && IsOurDescription(itsDesc)) return kDelRemove;
    return kDelNotOurs;
}

/* What the removal (igd) does after asking what the router forwards on the port: `check` is that call's result (1 = the
   entry came back, 0 = the router answered a fault, -1 = no answer or an unreadable one).  The entry that came back is
   judged by DeleteAction; fault 714 (no such entry) alone means it is gone; any other fault leaves it unknown and the
   delete is sent anyway (it names only our port and protocol); no answer sends nothing. */
enum { kDelCheckJudge = 0, kDelCheckGone = 1, kDelCheckUnchecked = 2, kDelCheckNoAnswer = 3 };
inline int DeleteCheckNext(int check, int fault)
{
    if (check == 1) return kDelCheckJudge;
    if (check == 0) return (fault == 714) ? kDelCheckGone : kDelCheckUnchecked;
    return kDelCheckNoAnswer;
}

/* A renewal (igd) the router refused with 718 (another entry holds the port): some routers refuse an add on an entry
   that already exists, our own included, so the entry is looked up first.  true = it is still this computer's, on our
   port and with our description (DeleteAction's rule): kept, and the renewal counts as done; false = the port is lost. */
inline bool RenewConflictStillOurs(bool found, const std::string& itsClient, long itsPort, const std::string& itsDesc,
                                   const std::string& ourClient, long ourPort)
{
    return DeleteAction(found, itsClient, itsPort, itsDesc, ourClient, ourPort) == kDelRemove;
}

/* After an unmap or a forget, whatever the router answered: a removal that failed is not tried again (the router may
   never answer, and the entry carries our description, so the next host on that port takes it over). */
inline void AfterUnmap(State* s)
{
    s->held = 0;
    s->removeAtEnd = false;
    s->route = kRouteNone;
    s->lease = 0;
    s->renewDue = false;
}

/* WHEN A LEASE IS RENEWED - the lease's own clock, not a guess about readiness: the router drops the entry when the
   lease runs out, so it is asked again at half the lease (the usual practice, RFC 6886 3.3), and after a renewal the
   router did not accept, again after kRenewRetryMs or half the lease, whichever is sooner.  0 = never (a permanent
   entry). */
inline unsigned long RenewAfterMs(long leaseSeconds, bool lastOk)
{
    const long lease = ClampLease(leaseSeconds);
    if (lease <= 0) return 0;
    const unsigned long half = (unsigned long)lease * 500ul;
    if (lastOk) return half;
    return (half < kRenewRetryMs) ? half : kRenewRetryMs;
}

/* How long the worker may still wait before the renewal armed at tick `armedAtMs` for `afterMs` is due, at tick `nowMs`
   (32-bit millisecond ticks that wrap).  0 = due now. */
inline unsigned long RenewWaitMs(unsigned long armedAtMs, unsigned long afterMs, unsigned long nowMs)
{
    const unsigned long elapsed = (nowMs - armedAtMs) & 0xFFFFFFFFul;
    return (elapsed >= afterMs) ? 0ul : afterMs - elapsed;
}

/* THE INTERNET ADDRESS THE ROUTER REPORTS (IStaticPortMapping::get_ExternalIPAddress) - is it one players elsewhere can
   reach?  Only a plain dotted IPv4 address outside the ranges that never face the internet counts: private (10.x,
   172.16-31.x, 192.168.x), the providers' shared range (100.64-127.x, carrier-grade NAT), this-network (0.x), loopback
   (127.x), link-local (169.254.x) and multicast / reserved (224.x and up).  A router behind another router or behind the
   provider's shared address reports one of those.  *kind (may be 0) says which, in words, for the log - never the address.
   *clean (may be 0) gets the address rebuilt from its four numbers ("08.8.8.8" -> "8.8.8.8"), "" when unreadable - the text
   shown and copied. */
inline bool RouterAddressPublic(const std::string& ip, std::string* kind, std::string* clean = 0)
{
    long part[4] = { 0, 0, 0, 0 };
    int n = 0, digits = 0;
    bool ok = !ip.empty();
    for (size_t i = 0; ok && i <= ip.size(); ++i)
    {
        const char c = (i < ip.size()) ? ip[i] : '.';
        if (c >= '0' && c <= '9')
        {
            if (++digits > 3) ok = false;
            else part[n] = part[n] * 10 + (c - '0');
        }
        else if (c == '.' && digits > 0 && part[n] <= 255)
        {
            digits = 0;
            if (++n > 4) ok = false;
            if (i == ip.size() && n != 4) ok = false;
            if (i < ip.size() && n >= 4) ok = false;
        }
        else ok = false;
    }
    const char* why = 0;
    if (!ok)                                                    why = "unreadable";
    else if (part[0] == 10 || (part[0] == 172 && part[1] >= 16 && part[1] <= 31) || (part[0] == 192 && part[1] == 168))
                                                                why = "private";
    else if (part[0] == 100 && part[1] >= 64 && part[1] <= 127) why = "shared (carrier-grade NAT)";
    else if (part[0] == 0)                                      why = "this-network";
    else if (part[0] == 127)                                    why = "loopback";
    else if (part[0] == 169 && part[1] == 254)                  why = "link-local";
    else if (part[0] >= 224)                                    why = "multicast or reserved";
    if (kind != 0) *kind = (why != 0) ? std::string(why) : std::string("public");
    if (clean != 0)
    {
        clean->clear();
        for (int k = 0; ok && k < 4; ++k)
        {
            if (k > 0) *clean += ".";
            if (part[k] >= 100) *clean += (char)('0' + part[k] / 100);
            if (part[k] >= 10)  *clean += (char)('0' + (part[k] / 10) % 10);
            *clean += (char)('0' + part[k] % 10);
        }
    }
    return why == 0;
}

}   /* namespace coopupnp */

#endif
