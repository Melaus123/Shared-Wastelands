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

/* MAIN THREAD, in place of UpnpHostStart when the settings file says routerport=0: the router is not asked. Logs one
   [UPNP] line and counts it in the REPORT's `off` field. */
void UpnpHostOff(unsigned short port);

/* THE GAME IS QUITTING (the exit path in store.cpp). Begin: hosting has ended, the worker is told to remove the entry
   and stop; returns at once. Wait: waits at most coopupnp::kQuitWaitMs for the worker to finish. Both do nothing when
   this game never hosted. */
void UpnpQuitBegin();
void UpnpQuitWait();

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
