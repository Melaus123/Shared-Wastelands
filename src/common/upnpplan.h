/* src/common/upnpplan.h - T-53: THE PURE HALF OF THE AUTOMATIC ROUTER PORT OPENING.
 *
 * When this game hosts, the world server it starts (SharedWastelandsServer.exe) listens on ONE UDP port, the PORT box's
 * number. src/coop-plugin/upnp.cpp asks the home router, through Windows' own UPnP interface (NATUPnP), to forward that
 * port to this computer, and to stop forwarding it when hosting ends. Every decision it takes lives here, so the offline
 * suite (src/coop-test) checks the same rules the plugin compiles:
 *   MapOnHostStart                    which HOST presses ask the router at all.
 *   Description / IsOurDescription    the text the router's port list shows for the entry, and how our own entry is known.
 *   FindRouterOutcome                 Windows' answer to "find the router" -> go on / no router / failed.
 *   ClassifyExisting / ActionFor      what an entry already on the router for that port means, and what is done about it.
 *   RemoveAtEnd                       whether the entry is taken away when hosting ends.
 *   State, HostStart, HostEnd, NextStep, AfterMap, AfterUnmap
 *                                     the background worker's state and what it does next.
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

/* LEASE 0, NOTHING TO RENEW. NATUPnP's IStaticPortMappingCollection::Add takes no lease: the entry is permanent until it
   is removed or the router restarts. So the end of hosting removes it, and an entry a crashed game left behind on the
   same port, pointing at this computer with our description, is taken over by the next host on that port (ActionFor
   gives kActKeep, RemoveAtEnd gives true) and removed when that hosting ends. */
const long kLeaseSeconds = 0;

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

/* FIND THE ROUTER. homeAddrFound: this computer has a home-network address to forward to (no address = no network with a
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
   that said no is not asked again and again). removeAtEnd: RemoveAtEnd's answer for held. */
struct State
{
    long want;
    long held;
    long tried;
    bool removeAtEnd;
    State() : want(0), held(0), tried(0), removeAtEnd(false) {}
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
    kStepForget = 3    /* stop holding `held` without touching the router (an entry that is not ours to remove) */
};

inline const char* StepWord(int step)
{
    switch (step)
    {
    case kStepIdle:   return "idle";
    case kStepMap:    return "map";
    case kStepUnmap:  return "unmap";
    case kStepForget: return "forget";
    default:          return "other";
    }
}

/* What the worker does next. A held port that hosting no longer wants (ended, or a new world server on another port)
   goes first; then the wanted port, once. */
inline int NextStep(const State& s)
{
    if (s.held != 0 && s.held != s.want) return s.removeAtEnd ? kStepUnmap : kStepForget;
    if (s.want != 0 && s.held == 0 && s.tried != s.want) return kStepMap;
    return kStepIdle;
}

/* After an attempt for `port` came to `outcome`: mapped / already -> held, anything else -> not held. Either way it was
   tried, so it is not asked again until the next world server start. */
inline void AfterMap(State* s, long port, int outcome, bool removeAtEnd)
{
    s->tried = port;
    if (outcome == kOutMapped || outcome == kOutAlready)
    {
        s->held = port;
        s->removeAtEnd = removeAtEnd;
    }
    else
    {
        s->held = 0;
        s->removeAtEnd = false;
    }
}

/* After an unmap or a forget, whatever the router answered: a removal that failed is not tried again (the router may
   never answer, and the entry carries our description, so the next host on that port takes it over). */
inline void AfterUnmap(State* s)
{
    s->held = 0;
    s->removeAtEnd = false;
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
