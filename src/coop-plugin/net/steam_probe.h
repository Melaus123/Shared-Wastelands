/* net/steam_probe.h - T-290 S0: Steam networking and relay availability, probed and logged (no behaviour change).
   Both functions run on the main thread only (the title pump and the main-loop pump). */
#ifndef COOP_NET_STEAM_PROBE_H
#define COOP_NET_STEAM_PROBE_H

#include <string>

namespace coop {

/* Once per frame. The first call looks Steam up and logs "[NET] steam: ..."; later calls poll the relay status
   until it settles and log "[NET] relay: ..." once. Every call then pumps Steam's callbacks once Steam is in use this session
   and ticks the relay echo test (which returns at once unless relaytest= is set). */
void SteamProbeTick();

/* What the Steam transport (net/steam_transport.cpp) needs from the probe; main thread. */
bool  SteamProbeStarted();       /* the first look-up has run */
void* SteamProbeSockets();       /* ISteamNetworkingSockets ("SteamNetworkingSockets012"), or 0: absent, not signed in, refused, or a Steam call faulted */
void* SteamProbeUtils();         /* ISteamNetworkingUtils ("SteamNetworkingUtils004"), the same rule */
void  SteamNoteFault();          /* a Steam call outside the probe faulted: counted here; from now on both above answer 0 */
void  SteamNoteShutDown();       /* the game's exit has shut Steam down (the exit hook runs after it): from now on both above answer 0 */
bool  SteamProbeShutDown();      /* SteamNoteShutDown was called: a Steam close is skipped, not attempted */
void  SteamPumpForSession();     /* SteamAPI_RunCallbacks runs every frame for the rest of the session: asked when a Steam link or listener is made and when the friends callbacks (net/steam_friends.cpp) are registered */
int   SteamProbeHasDll();         /* 1 = the game's steam_api64.dll is loaded, 0 = none (GOG or no Steam), -1 = not looked up yet */
bool  SteamProbeRunning();        /* signed in to Steam, no Steam call in this game faulted, Steam not shut down */
void* SteamProbeInterface(const char* version);   /* any interface by its version string from the probe's ISteamClient, or 0 (a fault is counted) */

/* "steamNet[dll,signedIn,ifaces,polls,faults]=... relay=<word> steamLink[...]=... steamLinkEv[...]=... steamListen[...]=... steamPump=<n> steamPumpFaults=<n> relayTest=<word>"
   for the [net] REPORT line. */
std::string SteamProbeReportToken();

}   /* namespace coop */

#endif
