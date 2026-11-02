/* upnp.h - T-53: the home router is asked to forward the world server's one UDP port while this game hosts (the mod's
   own UPnP IGD ask, then PCP / NAT-PMP, then Windows' NATUPnP), with a lease renewed while hosting lasts.
   The router work runs on its own background thread (upnp.cpp); the decisions are src/common/upnpplan.h, the wire texts
   src/common/routerwire.h. Nothing is shown to the player (owner decision 382) - [UPNP] log lines and the REPORT's
   upnp[...] fields only. */
#ifndef COOP_UPNP_H
#define COOP_UPNP_H

#include <string>

namespace coop {

/* MAIN THREAD, right after the MULTIPLAYER panel's HOST press started a world server on this computer. port: the port
   it listens on. serverProcess: its process handle (a HANDLE; this file keeps its own duplicate and watches it, so the
   caller may close its own at any time). Never waits: it records the request and wakes the worker. */
void UpnpHostStart(unsigned short port, void* serverProcess);

/* MAIN THREAD, just before the MULTIPLAYER panel's HOST press makes a world kept running on `port` (ui.cpp PanelStartNotebook) -
   before that process exists, so its own ask always comes after this: that world server asks the router for itself
   (UpnpServeStart, in its own process), so this game's hosting ends here - an entry it holds on `port` is left for that world
   server (coopupnp::HandOver), never removed by this game, even one its router thread is asking for or about to remove right
   now; one on any other port is removed as usual.  Does nothing when this game never asked the router.  Never waits. */
void UpnpHandOver(unsigned short port);

/* MAIN THREAD, when the world kept running could not be started after UpnpHandOver (its process was never made): the hand-over
   is undone (coopupnp::HandBack) - an entry on `port` left for that world is this game's again and is removed, this game's
   hosting having ended, as it was to be before the hand-over.  Does nothing when this game never asked the router.  Never waits. */
void UpnpHandBack(unsigned short port);

/* THE WORLD SERVER'S OWN ASK (SharedWastelandsServer.exe --keep-running compiles this file in; coopupnp::ServerMapsAtStart).
   ITS MAIN THREAD, once it listens on `port`: the router thread asks for the port as UpnpHostStart does, with no process to
   watch - the entry is removed by UpnpQuitBegin / UpnpQuitWait on the world server's stop.  note (may be 0) is called ON THE
   ROUTER THREAD, never under this file's lock, once for the ask's outcome and once for the removal (coopupnp::kNote*).
   Never waits. */
typedef void (*UpnpNoteFn)(int note, long port);
void UpnpServeStart(unsigned short port, UpnpNoteFn note);

/* MAIN THREAD, in place of UpnpHostStart when the settings file says routerport=0: the router is not asked. Logs one
   [UPNP] line and counts it in the REPORT's `off` field. */
void UpnpHostOff(unsigned short port);

/* THE GAME IS QUITTING (the exit path in store.cpp), or a world kept running stops (its QuitDrain). Begin: hosting has ended, the worker is told to remove the entry
   and stop; returns at once. Wait: waits at most coopupnp::kQuitWaitMs for the worker to finish - true when it finished
   (or there was none), false when the wait ran out. Both do nothing when this game never hosted. */
void UpnpQuitBegin();
bool UpnpQuitWait();

/* An opening this program could not be sure it closed (its stop's wait ran out): coopupnp::kNoteNotRemovedNoLimit when the
   entry it holds now was granted with no time limit, coopupnp::kNoteNotRemoved for a leased one, 0 when it holds none (its
   start-up ask still in progress has opened nothing it knows of) - coopupnp::NotClosedNote. */
int UpnpNotClosedNote();

/* THE INTERNET ADDRESS, FROM THE ROUTER (nothing outside the home is contacted by this ask).  MAIN THREAD, when
   the HOSTING window opens: the router thread is asked for the address the router reports (UPnP IGD GetExternalIPAddress,
   then NAT-PMP's address request, then NATUPnP's get_ExternalIPAddress on our port's entry for `port`, else on any entry
   the router lists).  Never waits; returns the ask's number (> 0), or 0 when the router thread could not be started (the
   answer is then "not found").  A newer ask replaces an older one. */
long UpnpAddrAsk(unsigned short port);

/* MAIN THREAD, polled: 0 = ask `askId` is not answered yet (or a newer ask replaced it), 1 = found (*ip set: a public IPv4
   address rebuilt from its four numbers, coopupnp::RouterAddressPublic), 2 = not found (*why says why in words - never an
   address; also when the router thread has stopped before answering). */
int UpnpAddrResult(long askId, std::string* ip, std::string* why);

/* THE INTERNET ADDRESS, FROM THE LOOKUP WEBSITES (owner 452: asked only when the router gave none and the player pressed SHOW
   or COPY).  MAIN THREAD: a short-lived worker thread of its own (never the router thread) asks src/common/addrlookup.h's
   websites in their order, the first usable answer winning, none after its deadline.  One lookup at a time: an ask while one
   runs joins it (the same number back).  Never waits, and nothing waits for the worker - the game's exit included.  Returns
   the ask's number (> 0), or 0 when the worker could not be started (not found). */
long UpnpLookupAsk();

/* MAIN THREAD, polled: 0 = not answered yet, 1 = found (*ip the address, *source the website that gave it), 2 = not found
   (*why names each website and why it gave none - never an address). */
int UpnpLookupResult(long askId, std::string* ip, std::string* source, std::string* why);

/* "upnp[asked,mapped,already,failed,removed,noRouter,off]=... upnpRoute[igd,pcp,natpmp,windows,renewed,renewFailed]=...
   upnpLast=<route>/<lease seconds>" for the [net] REPORT line: the asks' outcomes, which route settled each, the lease
   renewals, and the route and lease of the newest ask. */
std::string UpnpReportToken();

}   /* namespace coop */

#endif
