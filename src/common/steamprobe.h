/* src/common/steamprobe.h - T-290 S0: THE PURE HALF OF THE STEAM RELAY PROBE.
 *
 * The plugin (src/coop-plugin/net/steam_probe.cpp) asks Steam's networking whether the Steam Datagram Relay can be
 * reached and logs the answer once. Every decision it makes about that answer lives here, so the offline suite
 * (src/coop-test) checks the same translation unit the plugin links:
 *   AvailWord        ESteamNetworkingAvailability -> one lowercase word (values from Valve's public, BSD-3
 *                    steamnetworkingtypes.h, GameNetworkingSockets).
 *   RelayDecide      the settle rule: when does a polled status become the one logged answer (wall-time cap).
 *   RelayWantsPump   when the plugin starts pumping SteamAPI_RunCallbacks (still unsettled after 10 s).
 *   RelayPumpLine    the one line logged when pumping starts.
 *   RelayLine        the "[NET] relay: ..." line for a settled status.
 *   FindFixedFileVersion / FileVersionText   the DLL's FileVersion from its VS_VERSIONINFO resource bytes.
 *   CleanDebugText   Steam's fixed-size English debug text -> one printable log fragment.
 *
 * PURE: no Windows.h, no logging, no globals. C++03 (VS2010 v100).
 */
#ifndef COOP_STEAMPROBE_H
#define COOP_STEAMPROBE_H

#include <cstddef>
#include <string>

namespace coopsteam {

/* ESteamNetworkingAvailability (steamnetworkingtypes.h). */
enum
{
    kAvailCannotTry  = -102,
    kAvailFailed     = -101,
    kAvailPreviously = -100,
    kAvailRetrying   = -10,
    kAvailUnknown    = 0,
    kAvailNeverTried = 1,
    kAvailWaiting    = 2,
    kAvailAttempting = 3,
    kAvailCurrent    = 100
};

/* What one poll decides. */
enum
{
    RELAY_WAIT        = 0,   /* not settled - poll again next frame */
    RELAY_AVAILABLE   = 1,   /* status current: the relay network can be used */
    RELAY_UNAVAILABLE = 2,   /* cannot-try (a prerequisite is missing), or still not settled after the wait cap */
    RELAY_FAILED      = 3    /* Steam says it tried long enough and failed (failed), or it worked once and stopped (previously) */
};

/* THE WAIT IS BOUNDED BY WALL TIME, NOT BY FRAMES. T621: the title screen runs ~1800 frames/s, so a 3600-poll cap
   ran out in 2 s while Steam was still fetching the relay config. The status is still polled every frame and a
   settled status ends the wait whenever it arrives; kRelayWaitCapMs is only the safety cap for a status that never
   settles. Elapsed time is milliseconds since the probe started (the plugin takes GetTickCount differences). */
const long long kRelayWaitCapMs   = 90000;
/* Still unsettled this long after the probe started: the plugin pumps SteamAPI_RunCallbacks once per frame until the
   status settles or the cap ends the wait. */
const long long kRelayPumpAfterMs = 10000;

const char* AvailWord(int avail);
const char* RelayOutcomeWord(int outcome);
int RelayDecide(int avail, long long elapsedMs, long long capMs);
/* true when RelayDecide(avail, elapsedMs, capMs) is still WAIT and elapsedMs has reached afterMs. */
bool RelayWantsPump(int avail, long long elapsedMs, long long capMs, long long afterMs);

/* outcome is RelayDecide's non-WAIT answer. pumpFromMs is the elapsed ms when callback pumping started (-1 = never).
   msg is CleanDebugText's result ("" = none). */
std::string RelayLine(int outcome, int avail, int availConfig, int availAnyRelay, long long polls, long long elapsedMs,
                      long long pumpFromMs, const std::string& msg);
/* The one line logged when the pump decision is taken; exported=false: SteamAPI_RunCallbacks was not found and
   nothing is pumped (polling goes on). */
std::string RelayPumpLine(int avail, int availConfig, int availAnyRelay, long long polls, long long elapsedMs,
                          bool exported, const std::string& msg);

/* Finds VS_FIXEDFILEINFO (signature 0xFEEF04BD, 4-byte aligned) in a version resource's bytes and returns its
   FileVersion halves. false when the signature is absent or its fields run past len. */
bool FindFixedFileVersion(const unsigned char* blob, size_t len, unsigned int* versionMs, unsigned int* versionLs);
std::string FileVersionText(unsigned int versionMs, unsigned int versionLs);   /* "3.42.61.66" */

/* Reads at most cap bytes (stops at NUL), turns control bytes and non-ASCII into spaces, collapses runs of spaces,
   trims, and cuts to maxOut characters (a cut ends in "..."). */
std::string CleanDebugText(const char* buf, size_t cap, size_t maxOut);

}   /* namespace coopsteam */

#endif
