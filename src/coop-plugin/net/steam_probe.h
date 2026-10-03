/* net/steam_probe.h - T-290 S0: Steam networking and relay availability, probed and logged (no behaviour change).
   Both functions run on the main thread only (the title pump and the main-loop pump). */
#ifndef COOP_NET_STEAM_PROBE_H
#define COOP_NET_STEAM_PROBE_H

#include <string>

namespace coop {

/* Once per frame. The first call looks Steam up and logs "[NET] steam: ..."; later calls poll the relay status
   until it settles and log "[NET] relay: ..." once; after that it returns at its first line. */
void SteamProbeTick();

/* "steamNet[dll,signedIn,ifaces,polls,faults]=... relay=<word>" for the [net] REPORT line. */
std::string SteamProbeReportToken();

}   /* namespace coop */

#endif
