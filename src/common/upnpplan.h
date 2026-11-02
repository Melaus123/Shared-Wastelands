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
 *   MapOwner / MapOnHostStart / ServerMapsAtStart
 *                                     who holds a world server's entry (the hosting game, or a world kept running itself)
 *                                     and which HOST presses ask the router at all.
 *   HandOver / HandBack / MayRemove / PortLockName
 *                                     a game handing its entry to a world kept running, and the last look before a removal.
 *   NoteOfOutcome / NoteOfEnd / NotRemovedNote / NoteShown / NotClosedNote / NotClosedAtWaitEnd / StopStageAfter
 *                                     the kept world's window notes, and which of them its stop still shows.
 *   Description / IsOurDescription    the text the router's port list shows for the entry, and how our own entry is known.
 *   FirstRoute / NextRoute / RouteSettles / FoldOutcome
 *                                     the route order, when it stops, and what the routes' answers add up to.
 *   FindRouterOutcome                 Windows' answer to "find the router" -> go on / no router / failed (windows route).
 *   ClassifyExisting / ActionFor      what an entry already on the router for that port means, and what is done about it.
 *   RemoveAtEnd                       whether the entry is taken away when hosting ends.
 *   TimeLimitedOnly / AskAgainPermanent / WindowsStep / GrantIsTimeLimited / GrantKept / LeaseKept / RemoveLeftoverNow /
 *   RenewRefusedShowsFailed / AfterHandBack / AfterAsk / AfterRenewResult / RenewOfHandBack / EndLease
 *                                     a world kept running takes only openings with a time limit: which re-asks, routes,
 *                                     granted lifetimes and listed leases that leaves, what is handed back, and what an
 *                                     opening it could not hand back is held as.
 *   State, HostStart, HostEnd, NextStep, AfterMap, AfterRenew, AfterUnmap, RenewAfterMs, RenewWaitMs
 *                                     the background worker's state, what it does next, and when a lease is renewed.
 *   OutcomeWord / StepWord            one word each, for the [UPNP] log lines.
 *
 * NOTHING HERE IS SHOWN TO THE PLAYER (owner decision 382): the router work is background work, so success and failure
 * only reach the log and the REPORT line; the router help text on the hosting screen is unchanged.  The one exception is
 * a world kept running: its own window says, in src/common/keeprunning.h's words (RouterLine), what its ask and its removal
 * came to, because the person running it has no other way to know whether players from the internet can reach it.
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
   from the router on its own within the hour.  For the game's own hosting, a router that only takes permanent entries
   (IGD fault 725) gets lease 0, and such an entry stays until it is removed; a world kept running never makes a permanent
   entry (TimeLimitedOnly).  The windows route (NATUPnP's
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

/* WHO HOLDS THE ROUTER'S ENTRY FOR A WORLD SERVER - one owner each, so it is asked for once and removed once.  A world kept
   running (SharedWastelandsServer.exe --keep-running) outlives the game that started it, so the world server asks for its
   own entry when it starts listening and removes it when it stops (src/coop-store/store_main.cpp); any other world server
   is the hosting game's (upnp.cpp UpnpHostStart), removed when that process ends or the game quits. */
enum { kOwnerGame = 0, kOwnerServer = 1 };
inline int MapOwner(bool keepRunning) { return keepRunning ? kOwnerServer : kOwnerGame; }

/* WHICH HOST PRESSES ASK THE ROUTER. startResult is the panel's PanelStartNotebook answer: 1 = a new world server process
   was started on this computer (ask), 2 = the one this game started earlier still runs (its start already asked),
   0 = none was started (nothing listens here, or the world server is on another computer).  keptWorld: the process just
   started is a world kept running - it asks for itself, and this game does not (the game hands it its entry just before
   that process is made, ui.cpp PanelStartNotebook: HandOver). */
inline bool MapOnHostStart(int startResult, bool keptWorld) { return startResult == 1 && MapOwner(keptWorld) == kOwnerGame; }

/* THE WORLD SERVER'S OWN ASK: at its start, only a world kept running, and not when the game that started it was told to
   leave the router alone (routerport=0 in its settings file, passed on as --no-router). */
inline bool ServerMapsAtStart(bool keepRunning, bool routerOff) { return MapOwner(keepRunning) == kOwnerServer && !routerOff; }

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

/* A WORLD KEPT RUNNING TAKES ONLY OPENINGS WITH A TIME LIMIT.  It runs unattended, so an opening it makes must close by
   itself within about an hour when it crashes or is stopped wrongly - nobody may be there to remove it.  Its ask (owner
   kOwnerServer) therefore never makes a permanent entry: IGD fault 725 is not asked again with lease 0 (the igd route is
   refused), the windows route adds nothing (NATUPnP takes no lease) and only removes an entry of ours it finds on the port
   (WindowsStep: a permanent one the game handed over, or one a crashed game left - nothing else would remove it); an igd
   lease the router lists as none or longer than asked (LeaseKept), and a PCP / NAT-PMP lifetime granted so, is handed back
   at once (that route is refused): after a crash the port stays open an hour at most.  An opening the router does not take
   back is held only to be removed (AfterHandBack), never counted as opened.  When no route gives a time-limited opening the ask has failed (or found no
   router), and the kept world's window tells the person running it to open the port by hand.  The game's own hosting
   (kOwnerGame) still takes a permanent entry when nothing else is offered: it removes it when hosting ends, and the next
   host on that port takes over one a crashed game left. */
inline bool TimeLimitedOnly(int owner) { return owner == kOwnerServer; }

/* IGD fault 725 to a time-limited ask (src/common/routerwire.h AddFaultNext gave kAddRetryPermanent): asked again as
   permanent only when this ask may make a permanent entry. */
inline bool AskAgainPermanent(bool retryPermanentOffered, bool timeLimitedOnly) { return retryPermanentOffered && !timeLimitedOnly; }

/* What the windows route does about the entry on the port (ActionFor's `action`, the entry's description).  The game's
   own hosting: add / keep / leave, as ActionFor says.  An ask that takes time-limited openings only adds nothing (NATUPnP
   takes no lease) and removes an entry of ours it finds - one the game handed over or a crashed game left, with no time
   limit, which nothing else would remove; an entry made by hand forwarding here is kept (and left), another's left. */
enum { kWinAdd = 0, kWinKeep = 1, kWinLeave = 2, kWinRemoveOurs = 3, kWinNoAdd = 4 };
inline int WindowsStep(int action, const std::string& existingDescription, bool timeLimitedOnly)
{
    if (action == kActLeave) return kWinLeave;
    if (!timeLimitedOnly) return (action == kActKeep) ? kWinKeep : kWinAdd;
    if (action == kActKeep) return IsOurDescription(existingDescription) ? kWinRemoveOurs : kWinKeep;
    return kWinNoAdd;
}

/* A PCP / NAT-PMP lifetime the router granted is time-limited enough for a world kept running: not 0 (no limit), and no
   longer than the kAskLeaseSeconds asked - all ones (kLifeNoLimit, the largest lifetime the answer can carry, read as no
   limit) among the longer ones. */
const unsigned long kLifeNoLimit = 0xFFFFFFFFul;
inline bool GrantIsTimeLimited(unsigned long granted) { return granted != 0 && granted <= (unsigned long)kAskLeaseSeconds; }

/* Is a granted lifetime kept as the opening?  An ask that takes time-limited openings only hands any other back. */
inline bool GrantKept(unsigned long granted, bool timeLimitedOnly) { return !timeLimitedOnly || GrantIsTimeLimited(granted); }

/* A world kept running's igd renewal the router refused (fault 725 among others): its entry ends when its lease runs out
   unless a later renewal is accepted, so the window's "open" line no longer holds - the failed line (kNoteFailed) is shown,
   once per opening (alreadyShown), at that moment.  Silence is not a refusal: the renewal is asked again sooner. */
inline bool RenewRefusedShowsFailed(bool timeLimitedOnly, int route, int rr, bool alreadyShown)
{
    return timeLimitedOnly && route == kRouteIgd && rr == kRrRefused && !alreadyShown;
}

/* An entry of ours already on the router for this computer and port (one a crashed world or game left, or the one the game
   handed to a world kept running) that a time-limited-only ask could not lease again (the add was refused - fault 725
   among others - or answered 718 although the entry is this computer's and ours): it may have no time limit and nothing
   else will ever remove it, so it is removed at once - it is ours to remove - and it never counts as this world's opening
   (the igd route stays refused). */
inline bool RemoveLeftoverNow(bool timeLimitedOnly, int action, const std::string& existingDescription, int addResult)
{
    return timeLimitedOnly && action == kActKeep && IsOurDescription(existingDescription)
        && (addResult == kRrRefused || addResult == kRrElsewhere);
}

/* The lease the router lists for an entry of ours (igd GetSpecificPortMappingEntry's NewLeaseDuration: read back after an
   add or a renewal, or at a renewal answered 718): kept unless this ask takes time-limited openings only and the lease was
   read as 0 (none) or longer than asked.  A lease that could not be read leaves the one asked standing. */
inline bool LeaseKept(bool timeLimitedOnly, bool leaseRead, long listedSeconds)
{
    return !timeLimitedOnly || !leaseRead || (listedSeconds > 0 && listedSeconds <= kAskLeaseSeconds);
}

/* What became of an opening a world kept running hands back: none was handed back / handed back (removed, or already
   gone) / not handed back - the router did not take the removal, so it stays this world's to remove (AfterHandBack). */
enum { kBackNone = 0, kBackDone = 1, kBackFailed = 2 };

/* THE WORKER'S STATE. want: the port hosting needs open now (0 = not hosting). held: the port the router forwards for
   us now (0 = none). tried: the port already asked for in this hosting (one attempt per world server start - a router
   that said no is not asked again and again). removeAtEnd: RemoveAtEnd's answer for held.  route: the route that holds
   it (renewed and removed by that route).  lease: the seconds the router granted (0 = permanent, never renewed).
   renewDue: the lease's renewal time has come (set by the worker's timer, cleared by AfterRenew).  handedOver: the port
   this game handed to a world kept running (HandOver) - this game never removes an entry on it after that, whatever its
   router thread was doing at the hand-over (0 = none).  handedHeld / handedRemove / handedRoute / handedLease: the entry this
   game held on handedOver when it was left to that world (removeAtEnd, route and lease as they were), so HandBack can take it
   back when that world could not be started. */
struct State
{
    long want;
    long held;
    long tried;
    bool removeAtEnd;
    int  route;
    long lease;
    bool renewDue;
    long handedOver;
    bool handedHeld;
    bool handedRemove;
    int  handedRoute;
    long handedLease;
    bool noLimit;   /* the entry held has no time limit and is held only to be removed (AfterHandBack) */
    State() : want(0), held(0), tried(0), removeAtEnd(false), route(kRouteNone), lease(0), renewDue(false), handedOver(0),
              handedHeld(false), handedRemove(false), handedRoute(kRouteNone), handedLease(0), noLimit(false) {}
};

/* A world server was started on this computer on `port` (once per process start). */
inline void HostStart(State* s, long port)
{
    if (!PortOk(port)) return;
    s->want = port;
    s->tried = 0;
    /* a world server this game starts on a port it handed over is this game's again: the world kept running there has
       stopped (two cannot listen on one port) */
    if (s->handedOver == port) { s->handedOver = 0; s->handedHeld = false; }
}

/* Hosting ended: the world server stopped, or the game is quitting. */
inline void HostEnd(State* s)
{
    s->want = 0;
    s->tried = 0;
}

/* This game's hosting ends because it is starting a world kept running on `keptPort`: an entry held on that port is the new
   world server's now (it finds it there and keeps it, ClassifyExisting / ActionFor), so this game forgets it instead of
   removing it (NextStep gives forget); an entry held on any other port is removed as at any hosting end.  The port is
   marked (handedOver), so an ask for it still on this game's router thread lands as not this game's to remove (AfterMap),
   and a removal of it chosen before the hand-over is not sent (MayRemove). */
/* The entry held on the handed-over port is left to the world kept running: never removed here, and what it was is kept for
   HandBack. */
inline void LeaveHanded(State* s)
{
    s->handedHeld   = true;
    s->handedRemove = s->removeAtEnd;
    s->handedRoute  = s->route;
    s->handedLease  = s->lease;
    s->removeAtEnd  = false;
}
inline void HandOver(State* s, long keptPort)
{
    HostEnd(s);
    if (!PortOk(keptPort)) return;
    s->handedOver = keptPort;
    s->handedHeld = false;
    if (s->held == keptPort) LeaveHanded(s);
}

/* The world kept running on `keptPort` could not be started (its process was never made): the hand-over is undone, and the
   entry left to it is this game's again as it was before - removed now, this game's hosting having ended (unless it was
   never this game's to remove), even when the router thread already forgot it (a forget never touches the router). */
inline void HandBack(State* s, long keptPort)
{
    if (!PortOk(keptPort) || s->handedOver != keptPort) return;
    s->handedOver = 0;
    if (!s->handedHeld) return;
    s->handedHeld = false;
    if (s->held == 0)
    {
        s->held = keptPort;
        s->route = s->handedRoute;
        s->lease = s->handedLease;
        s->renewDue = false;
    }
    if (s->held == keptPort) s->removeAtEnd = s->handedRemove;
}

/* THE PORT'S ROUTER LOCK: the name of a lock every program on this computer that compiles upnp.cpp (the game and its world
   servers) holds while it asks the router for that port or removes it.  A world kept running therefore asks only after a
   removal of its port by the game that started it has ended - it then finds the entry gone and adds its own, or finds the
   game left it (MayRemove) and keeps it - so it never keeps an entry that is about to be removed. */
inline std::string PortLockName(long port)
{
    return std::string("Local\\SharedWastelandsRouterEntry-") + kProtocol + "-" + PortText(port);
}

/* WHAT A WORLD KEPT RUNNING TELLS THE PERSON RUNNING IT (src/common/keeprunning.h RouterLine says it in words): one note for
   its ask at start and one for the removal at its stop. */
enum
{
    kNoteMapped     = 1,   /* the router accepted the entry */
    kNoteAlready    = 2,   /* the router already forwarded the port to this computer */
    kNoteFailed     = 3,   /* refused, forwarded to another computer, or the ask could not run */
    kNoteNoRouter   = 4,   /* no router answered */
    kNoteRemoved    = 5,   /* the entry was removed at the stop */
    kNoteGone       = 6,   /* the entry was already gone at the stop (its lease ran out, or the router restarted) */
    kNoteNotRemoved = 7,   /* the removal was not accepted or not answered (the entry has a lease: it ends by itself) */
    kNoteLeft       = 8,   /* the entry is not this program's to remove (made by hand, or another device holds the port now) */
    kNoteNotRemovedNoLimit = 9   /* as kNoteNotRemoved, for an entry the router granted with no time limit (lease 0) */
};
inline int NoteOfOutcome(int outcome)
{
    if (outcome == kOutMapped)   return kNoteMapped;
    if (outcome == kOutAlready)  return kNoteAlready;
    if (outcome == kOutNoRouter) return kNoteNoRouter;
    return kNoteFailed;
}

/* An opening that was not closed: which note, from the lease the router granted the entry held (`held`: an entry is held;
   lease 0 = no time limit, it never ends by itself).  Nothing held, or its lease unknown: the note for a leased one. */
inline int NotRemovedNote(bool held, long lease) { return (held && lease == 0) ? kNoteNotRemovedNoLimit : kNoteNotRemoved; }

/* The stop's wait ran out: the note for an opening this program knows it holds (by the lease granted it), 0 when it holds
   none - its start-up ask still in progress has opened nothing it knows of. */
inline int NotClosedNote(bool held, long lease) { return held ? NotRemovedNote(true, lease) : 0; }

/* The note at the end of hosting: a removal not sent (forget) leaves the entry; a sent one says what it came to, and one
   not accepted says whether the entry ends by itself (heldLease). */
inline int NoteOfEnd(bool removalSent, int removalNote, long heldLease)
{
    if (!removalSent) return kNoteLeft;
    if (removalNote == kNoteNotRemoved) return NotRemovedNote(true, heldLease);
    return removalNote;
}

/* WHICH NOTES THE KEPT WORLD'S WINDOW STILL SHOWS, by how far its stop has come: the ask's note only while the world runs
   (never after "Saving the world..."), the removal's note until the stop's wait for the router ended (never after
   "The world has stopped."). */
enum { kStopRunning = 0, kStopBegun = 1, kStopWaited = 2 };
inline bool IsRemovalNote(int note)
{
    return note == kNoteRemoved || note == kNoteGone || note == kNoteNotRemoved || note == kNoteLeft || note == kNoteNotRemovedNoLimit;
}
inline bool NoteShown(int note, int stopStage) { return IsRemovalNote(note) ? stopStage < kStopWaited : stopStage == kStopRunning; }

/* The stop's wait for the router ended: the "did not close" note (NotClosedNote) is shown when the router thread had not
   finished, no removal note (nor this note) was shown, and an opening is known to be held (note != 0). */
inline bool NotClosedAtWaitEnd(bool routerFinished, bool removalShown, int note) { return !routerFinished && !removalShown && note != 0; }

/* How far the stop has come once a stop door reached `reached`: never back (a second door, such as Ctrl+C during a typed
   stop, runs the same stop again). */
inline int StopStageAfter(int now, int reached) { return reached > now ? reached : now; }

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
    if (s.held != 0 && s.held != s.want) return (s.removeAtEnd && s.held != s.handedOver) ? kStepUnmap : kStepForget;
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
    s->noLimit = false;
    if (outcome == kOutMapped || outcome == kOutAlready)
    {
        s->held = port;
        s->removeAtEnd = removeAtEnd;
        s->route = route;
        s->lease = ClampLease(lease);
        if (port == s->handedOver) LeaveHanded(s);   /* handed to a world kept running while it was asked: never removed here */
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

/* AN OPENING WITH NO TIME LIMIT (or a longer one than asked) THAT A WORLD KEPT RUNNING HANDS BACK, at its ask or at a
   renewal, on `route`.  Handed back -> nothing is held: nothing is removed and its stop shows no line for it (its window
   already showed the failed line).  Not handed back (the router did not take the removal) -> held only to be removed,
   never counted as opened: the worker asks the removal again instead of each renewal (kMinLeaseSeconds sets how soon),
   and the stop removes it, its note being the one for an entry with no time limit (EndLease). */
inline void AfterHandBack(State* s, long port, int route, bool handedBack)
{
    s->tried = port;
    s->renewDue = false;
    if (handedBack)
    {
        s->held = 0;
        s->removeAtEnd = false;
        s->route = kRouteNone;
        s->lease = 0;
        s->noLimit = false;
        return;
    }
    s->held = port;
    s->removeAtEnd = true;
    s->route = route;
    s->lease = kMinLeaseSeconds;
    s->noLimit = true;
}

/* After an ask (the worker's map step): AfterMap, or AfterHandBack for an opening the router did not take back. */
inline void AfterAsk(State* s, long port, int outcome, bool removeAtEnd, int route, long lease, int handBack)
{
    if (handBack == kBackFailed) AfterHandBack(s, port, route, false);
    else AfterMap(s, port, outcome, removeAtEnd, route, lease);
}

/* What a renewal came to (upnp.cpp RenewPort returns these), or, for an entry held only to be removed, the removal asked
   again instead (RenewOfHandBack, from that removal's note). */
enum { kRenewLost = -1, kRenewNotAccepted = 0, kRenewAccepted = 1, kRenewHandedBack = 2, kRenewNotHandedBack = 3 };
inline int RenewOfHandBack(int removalNote) { return removalNote == kNoteNotRemoved ? kRenewNotHandedBack : kRenewHandedBack; }
inline void AfterRenewResult(State* s, int result, long grantedLease)
{
    if (result == kRenewLost) AfterRenewLost(s);
    else if (result == kRenewHandedBack || result == kRenewNotHandedBack) AfterHandBack(s, s->held, s->route, result == kRenewHandedBack);
    else AfterRenew(s, result == kRenewAccepted, grantedLease);
}

/* The lease a stop's note is judged by (NoteOfEnd, NotClosedNote): none for an entry held only to be removed. */
inline long EndLease(const State& s) { return s.noLimit ? 0 : s.lease; }

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
/* THE LAST LOOK BEFORE A REMOVAL IS SENT (the worker, holding the port's lock PortLockName, on the live state): `port` is
   still held and still to be removed - not handed over, and not wanted again, since the step was chosen. */
inline bool MayRemove(const State& s, long port) { return port != 0 && s.held == port && NextStep(s) == kStepUnmap; }

inline void AfterUnmap(State* s)
{
    s->noLimit = false;
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
