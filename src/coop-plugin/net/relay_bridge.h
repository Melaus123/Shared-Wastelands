/* net/relay_bridge.h - THE HOST'S STEAM BRIDGE and THE ROUTE A WORLD-SERVER DIAL TAKES. Main thread only. relay_bridge.cpp says
   what each does; src/common/relaybridge.h holds their decisions. */
#ifndef SW_NET_RELAY_BRIDGE_H
#define SW_NET_RELAY_BRIDGE_H

#include "transport.h"

#include <string>

namespace coop {
namespace net {

/* Once per frame from SteamProbeTick (title and main-loop pumps): opens, runs and closes the host's Steam bridge. */
void RelayBridgeTick();
/* This game is exiting: the bridge is closed now (a world-server connection is closed gracefully only while its wait still fits in
   relaybridge::kCloseAllBudgetMs of the whole close), so Steam joiners learn at once that the host left instead of after Steam's
   timeout. Nothing to close: nothing done. Main thread. */
void RelayBridgeShutdown();

/* "relayBridge[...]=... relayRoute[...]=..." for the [net] REPORT line (printed inside the Steam probe's token). */
/* HOSTING's status line: 1 = the Steam listener is open and every one of its joiner places is taken (*cap = the places). Main thread. */
int RelayBridgeSteamFull(int* cap);
std::string RelayBridgeReportToken();

/* THE ROUTE, for store.cpp SetStoreServer and the two re-dials' connect budgets: which route (relaybridge::kRoute*) a dial of this
   address takes - relaybridge::RouteFor with this game's relayvia= test key. */
int RelayRouteFor(const std::string& address);
/* A new, not yet dialled transport for that route (0 = none could be made). */
ITransport* RelayRouteMake(int route);
/* What the transport of that route is handed to Join: a steam:<id> address in its dial form; in the relay test 127.0.0.1 and the
   relayvia= port. */
void RelayRouteTarget(int route, std::string* address, unsigned short* port);
/* A dial by that route is starting: counted, and a Steam route's dial said (the first 5 and every 100th). */
void RelayRouteNoteDial(int route);
/* The world-server link came up: the route it used, as a log fragment (counted for a Steam route). */
std::string RelayRouteNoteUp(ITransport* link);
/* The world-server link is about to be closed here: for a Steam route, one line with its state and Steam's last end reason. */
void RelayRouteNoteEnd(ITransport* link);

}   /* namespace net */
}   /* namespace coop */

#endif   /* SW_NET_RELAY_BRIDGE_H */
