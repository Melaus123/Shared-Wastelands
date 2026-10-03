/* net/steam_probe.cpp - T-290 stage S0 (t290-relay-fallback.md section 3): IS THE STEAM RELAY REACHABLE FROM INSIDE
   KENSHI? This file only asks and logs. It opens no connection, sends nothing to another player, changes no route and
   draws nothing. The ENet links are untouched.

   WHAT IT DOES, once, on the first frame of the title screen (the game's own SteamAPI_Init ran long before):
     1. GetModuleHandle("steam_api64.dll") - NEVER LoadLibrary. The GOG build ships no Steam DLL: then one line
        "[NET] steam: absent (GOG or no Steam) - direct only" and nothing else, ever.
     2. GetProcAddress: SteamAPI_GetHSteamUser, SteamAPI_GetHSteamPipe, SteamInternal_CreateInterface,
        SteamAPI_ISteamClient_GetISteamGenericInterface (all exported by the game's 2016-era DLL - investigation
        section 2). A zero user or pipe means the game's SteamAPI_Init did not succeed (Steam closed): logged, stop.
     3. ISteamClient ("SteamClient017", the version the game's DLL was built with) -> GetISteamGenericInterface for
        the three modern interfaces by the version strings in Valve's public headers (GameNetworkingSockets master,
        BSD-3): SteamNetworkingSockets012, SteamNetworkingUtils004, SteamNetworkingMessages002. The objects are
        served by the installed Steam client (steamclient64.dll), not by the old DLL.
     4. ISteamNetworkingUtils::InitRelayNetworkAccess() - an INLINE in Valve's header, defined as
        CheckPingDataUpToDate(1e10f), so it is called as that virtual. It starts fetching the relay network config and
        measuring pings to Valve's relays; it sends nothing to any player.
   Then every frame (title pump and main-loop pump, both the main thread) GetRelayNetworkStatus is polled until
   coopsteam::RelayDecide settles it, and one "[NET] relay: ..." line is logged with the polls and the elapsed ms.

   THE WAIT IS BOUNDED BY WALL TIME (T621). The exit condition is the status settling; the bound is only a safety cap:
   kRelayWaitCapMs (90 s) measured with GetTickCount from the probe start (wrap-safe DWORD difference). A frame count
   is no bound - the title screen runs ~1800 frames/s and a 3600-poll cap ran out in 2 s mid config fetch.

   CALLBACKS ARE PUMPED ONLY AS A FALLBACK, AFTER 10 s UNSETTLED. Valve's public headers (GameNetworkingSockets,
   isteamnetworkingutils.h / isteamnetworkingsockets.h) describe SteamRelayNetworkStatus_t as a change notice
   ("triggered ... any time the status changes") and GetRelayNetworkStatus as the current status; they do not say
   the relay config fetch depends on SteamAPI_RunCallbacks - nor that it does not. So pumping is a hedge: if the
   status is still unsettled kRelayPumpAfterMs after the start, SteamAPI_RunCallbacks is resolved by GetProcAddress
   (guarded) and called once per frame, inside __try, until the status settles or the cap ends the wait; one line
   says so, and the settle line says from when. WHY IT IS SAFE FOR THE GAME: kenshi_x64.exe imports only
   SteamAPI_Init/Shutdown/GetHSteamUser/GetHSteamPipe and SteamInternal_CreateInterface (t290-relay-fallback.md,
   PE import table) - no SteamAPI_RegisterCallback, no SteamAPI_RegisterCallResult, no RunCallbacks - so the game has
   no callback handler that could now fire on this thread; queued callbacks with no handler are dropped. RESIDUAL
   RISK: another DLL in the process (a mod plugin) that registered handlers would have them run here, on the main
   thread, from this pump. The pump is on the main thread, the same thread the game's frame runs on.

   SAFETY. Every call into Steam is inside __try in a function that holds no C++ object (C2712); a fault is counted,
   logged once as "failed <reason>", and the probe stops for the process. Handles and pointers are null-checked before
   use. The own Steam ID is not read and nothing identifying is logged.

   VTABLE SLOTS (ISteamNetworkingUtils, SteamNetworkingUtils004, isteamnetworkingutils.h, no virtual destructor):
     0 AllocateMessage, 1 GetRelayNetworkStatus, 2 GetLocalPingLocation, 3 EstimatePingTimeBetweenTwoLocations,
     4 EstimatePingTimeFromLocalHost, 5 ConvertPingLocationToString, 6 ParsePingLocationString, 7 CheckPingDataUpToDate. */
#include <windows.h>

#include "steam_probe.h"
#include "../coop_log.h"
#include "../../common/steamprobe.h"

#include <cstring>
#include <sstream>
#include <locale>

namespace coop {

namespace {

std::string N(long long v)   // RE_Kenshi's locale mangles raw numbers (F030)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

const char* const kClientVersion   = "SteamClient017";
const char* const kSocketsVersion  = "SteamNetworkingSockets012";
const char* const kUtilsVersion    = "SteamNetworkingUtils004";
const char* const kMessagesVersion = "SteamNetworkingMessages002";

const int kSlotGetRelayNetworkStatus = 1;
const int kSlotCheckPingDataUpToDate = 7;

typedef int   (*FnGetHandle)();                                      /* HSteamUser / HSteamPipe are int32 */
typedef void* (*FnCreateInterface)(const char* version);
typedef void* (*FnGenericInterface)(intptr_t client, int user, int pipe, const char* version);
typedef bool  (*FnCheckPingDataUpToDate)(void* self, float maxAgeSeconds);
typedef int   (*FnGetRelayNetworkStatus)(void* self, void* details);
typedef void  (*FnRunCallbacks)();                                   /* SteamAPI_RunCallbacks */

/* SteamRelayNetworkStatus_t (isteamnetworkingutils.h): four int32 fields and char[256]. */
struct RelayStatus
{
    int  avail;
    int  pingMeasurementInProgress;
    int  availNetworkConfig;
    int  availAnyRelay;
    char debugMsg[256];
};

enum { PHASE_START = 0, PHASE_POLL = 1, PHASE_DONE = 2 };

int         g_phase    = PHASE_START;
int         g_dll      = -1;     /* -1 not probed yet, 0 absent, 1 present */
int         g_signedIn = 0;      /* 1 = user and pipe handles are live */
int         g_ifaces   = 0;      /* of the three networking interfaces, how many Steam handed back */
long long   g_polls    = 0;      /* GetRelayNetworkStatus calls */
long long   g_faults   = 0;      /* exceptions caught around Steam calls */
const char* g_relay    = "not-probed";
void*       g_utils    = 0;
DWORD       g_startMs  = 0;      /* GetTickCount at probe start; elapsed = (DWORD)(now - start), wrap-safe */
long long   g_pumpFrom = -1;     /* elapsed ms when callback pumping started; -1 = never */
int         g_pumpAsked = 0;     /* 1 = the pump decision was taken (once per process) */
FnRunCallbacks g_runCallbacks = 0;   /* non-zero while pumping */

/* ---- the only frames that call into Steam; no C++ objects below this line until the next marker ---- */

int SehHandles(FnGetHandle fnUser, FnGetHandle fnPipe, int* user, int* pipe)
{
    __try { *user = fnUser(); *pipe = fnPipe(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehCreateInterface(FnCreateInterface fn, const char* version, void** out)
{
    __try { *out = fn(version); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}

int SehGenericInterface(FnGenericInterface fn, void* client, int user, int pipe, const char* version, void** out)
{
    __try { *out = fn((intptr_t)client, user, pipe, version); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}

int SehInitRelayNetworkAccess(void* utils)
{
    __try
    {
        void** vt = *(void***)utils;
        if (vt == 0 || vt[kSlotCheckPingDataUpToDate] == 0)
            return 0;
        ((FnCheckPingDataUpToDate)vt[kSlotCheckPingDataUpToDate])(utils, 1e10f);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehGetRelayNetworkStatus(void* utils, RelayStatus* st, int* avail)
{
    __try
    {
        void** vt = *(void***)utils;
        if (vt == 0 || vt[kSlotGetRelayNetworkStatus] == 0)
            return 0;
        *avail = ((FnGetRelayNetworkStatus)vt[kSlotGetRelayNetworkStatus])(utils, st);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehFileVersion(HMODULE mod, unsigned int* ms, unsigned int* ls)
{
    __try
    {
        HRSRC r = ::FindResourceW(mod, MAKEINTRESOURCEW(1), RT_VERSION);
        if (r == 0) return 0;
        HGLOBAL g = ::LoadResource(mod, r);
        if (g == 0) return 0;
        const unsigned char* p = (const unsigned char*)::LockResource(g);
        DWORD n = ::SizeofResource(mod, r);
        if (p == 0 || n == 0) return 0;
        return coopsteam::FindFixedFileVersion(p, (size_t)n, ms, ls) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehRunCallbacks(FnRunCallbacks fn)
{
    __try { fn(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- end of the __try frames ---- */

void RelayFailed(const std::string& reason)
{
    g_relay = "failed";
    g_phase = PHASE_DONE;
    DebugLog("[NET] relay: failed " + reason);
}

void Start()
{
    g_phase = PHASE_DONE;   /* every early return below is final; only the healthy path moves on to polling */
    g_startMs = ::GetTickCount();   /* the probe start: the wait cap and the pump delay are measured from here */

    HMODULE mod = ::GetModuleHandleW(L"steam_api64.dll");
    if (mod == 0)
    {
        g_dll = 0;
        g_relay = "absent";
        DebugLog("[NET] steam: absent (GOG or no Steam) - direct only");
        return;
    }
    g_dll = 1;

    unsigned int vms = 0, vls = 0;
    std::string head = "[NET] steam: present steam_api64.dll ";
    if (SehFileVersion(mod, &vms, &vls))
        head += coopsteam::FileVersionText(vms, vls);
    else
        head += "(no version resource)";

    FnGetHandle        fnUser    = (FnGetHandle)::GetProcAddress(mod, "SteamAPI_GetHSteamUser");
    FnGetHandle        fnPipe    = (FnGetHandle)::GetProcAddress(mod, "SteamAPI_GetHSteamPipe");
    FnCreateInterface  fnCreate  = (FnCreateInterface)::GetProcAddress(mod, "SteamInternal_CreateInterface");
    FnGenericInterface fnGeneric = (FnGenericInterface)::GetProcAddress(mod, "SteamAPI_ISteamClient_GetISteamGenericInterface");
    const char* missing = fnUser == 0 ? "SteamAPI_GetHSteamUser"
                        : fnPipe == 0 ? "SteamAPI_GetHSteamPipe"
                        : fnCreate == 0 ? "SteamInternal_CreateInterface"
                        : fnGeneric == 0 ? "SteamAPI_ISteamClient_GetISteamGenericInterface" : 0;
    if (missing != 0)
    {
        DebugLog(head + ", export " + missing + " not found - direct only");
        RelayFailed(std::string("missing-export ") + missing);
        return;
    }

    int user = 0, pipe = 0;
    if (!SehHandles(fnUser, fnPipe, &user, &pipe))
    {
        ++g_faults;
        DebugLog(head + ", fault reading the Steam user/pipe handles - direct only");
        RelayFailed("fault-reading-handles");
        return;
    }
    if (user == 0 || pipe == 0)
    {
        DebugLog(head + ", not signed in (the game's SteamAPI_Init did not succeed - Steam closed?) - direct only");
        g_relay = "unavailable";
        DebugLog("[NET] relay: unavailable steam-not-running");
        return;
    }
    g_signedIn = 1;

    void* client = 0;
    if (!SehCreateInterface(fnCreate, kClientVersion, &client))
        ++g_faults;
    if (client == 0)
    {
        DebugLog(head + ", signed in, but " + kClientVersion + " was refused - direct only");
        RelayFailed(std::string("no-") + kClientVersion);
        return;
    }

    void* sockets = 0;
    void* utils = 0;
    void* messages = 0;
    if (!SehGenericInterface(fnGeneric, client, user, pipe, kSocketsVersion, &sockets)) ++g_faults;
    if (!SehGenericInterface(fnGeneric, client, user, pipe, kUtilsVersion, &utils)) ++g_faults;
    if (!SehGenericInterface(fnGeneric, client, user, pipe, kMessagesVersion, &messages)) ++g_faults;
    g_ifaces = (sockets != 0 ? 1 : 0) + (utils != 0 ? 1 : 0) + (messages != 0 ? 1 : 0);

    DebugLog(head + ", signed in; interfaces " + kSocketsVersion + "=" + (sockets != 0 ? "yes" : "no")
             + " " + kUtilsVersion + "=" + (utils != 0 ? "yes" : "no")
             + " " + kMessagesVersion + "=" + (messages != 0 ? "yes" : "no")
             + " (faults " + N(g_faults) + "); relay status is polled every frame (cap "
             + N(coopsteam::kRelayWaitCapMs / 1000) + " s), Steam callbacks are pumped only if it is still unsettled after "
             + N(coopsteam::kRelayPumpAfterMs / 1000) + " s");

    if (utils == 0)
    {
        RelayFailed(std::string("no-") + kUtilsVersion);
        return;
    }
    if (!SehInitRelayNetworkAccess(utils))
    {
        ++g_faults;
        RelayFailed("fault-in-InitRelayNetworkAccess");
        return;
    }
    g_utils = utils;
    g_relay = "waiting";
    g_phase = PHASE_POLL;
}

void Poll()
{
    ++g_polls;
    const long long elapsed = (long long)(DWORD)(::GetTickCount() - g_startMs);
    RelayStatus st;
    std::memset(&st, 0, sizeof(st));
    int avail = coopsteam::kAvailUnknown;
    if (!SehGetRelayNetworkStatus(g_utils, &st, &avail))
    {
        ++g_faults;
        RelayFailed("fault-in-GetRelayNetworkStatus after " + N(g_polls) + " polls, " + N(elapsed) + " ms");
        return;
    }
    int outcome = coopsteam::RelayDecide(avail, elapsed, coopsteam::kRelayWaitCapMs);
    if (outcome != coopsteam::RELAY_WAIT)
    {
        g_relay = coopsteam::RelayOutcomeWord(outcome);
        g_phase = PHASE_DONE;   /* pumping (if any) ends with the probe */
        DebugLog(coopsteam::RelayLine(outcome, avail, st.availNetworkConfig, st.availAnyRelay, g_polls, elapsed, g_pumpFrom,
                                      coopsteam::CleanDebugText(st.debugMsg, sizeof(st.debugMsg), 160)));
        return;
    }
    if (g_pumpAsked == 0
        && coopsteam::RelayWantsPump(avail, elapsed, coopsteam::kRelayWaitCapMs, coopsteam::kRelayPumpAfterMs))
    {
        g_pumpAsked = 1;
        HMODULE mod = ::GetModuleHandleW(L"steam_api64.dll");   /* never LoadLibrary */
        g_runCallbacks = mod != 0 ? (FnRunCallbacks)::GetProcAddress(mod, "SteamAPI_RunCallbacks") : 0;
        if (g_runCallbacks != 0)
            g_pumpFrom = elapsed;
        DebugLog(coopsteam::RelayPumpLine(avail, st.availNetworkConfig, st.availAnyRelay, g_polls, elapsed,
                                          g_runCallbacks != 0,
                                          coopsteam::CleanDebugText(st.debugMsg, sizeof(st.debugMsg), 160)));
    }
    if (g_runCallbacks != 0 && !SehRunCallbacks(g_runCallbacks))
    {
        ++g_faults;
        g_runCallbacks = 0;
        RelayFailed("fault-in-SteamAPI_RunCallbacks after " + N(g_polls) + " polls, " + N(elapsed) + " ms");
    }
}

}   /* anonymous namespace */

void SteamProbeTick()
{
    if (g_phase == PHASE_DONE)
        return;
    if (g_phase == PHASE_START)
    {
        Start();
        return;
    }
    Poll();
}

std::string SteamProbeReportToken()
{
    return "steamNet[dll,signedIn,ifaces,polls,faults]=" + N(g_dll) + "," + N(g_signedIn) + "," + N(g_ifaces)
           + "," + N(g_polls) + "," + N(g_faults) + " relay=" + g_relay;
}

}   /* namespace coop */
