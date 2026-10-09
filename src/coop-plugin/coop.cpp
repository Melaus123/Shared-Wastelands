// kenshi-coop plugin.
//
// Built with the VS2010 v100 toolset (tools/vc2010-env.bat).
//
//   - toolchain/version self-check (T003, passed)
//   - PROBE P001: hook the main-loop function - the main-thread pump the sync layer needs.

#include "coop_log.h"
#include "game/GameWorld.h"
#include "game/TitleScreen.h"
#include "command_channel.h"
#include "ai_spike.h"
#include "combat.h"
#include "medical.h"
#include "store.h"
#include "relations.h"
#include "peace.h"
#include "towngen.h"
#include "playerfaction.h"
#include "worldgen.h"
#include "worldstate.h"
#include "items.h"
#include "doors.h"
#include "speech.h"   /* P3: Dialogue::say - NPC speech bubbles */
#include "hire.h"     /* recruit1 */
#include "crime.h"    /* pvp1: InstallCrime */
#include "build.h"    /* build1-a: InstallBuild */
#include "settings.h" /* settings2 S2: InstallSettings */
#include "policy.h"
#include "spawn.h"   // P011 WatchTaskTick rides the in-game pump
#include "replicate.h"   /* P25: InteriorKeepTick rides the in-game pump */
#include "soak.h"    // F333: MainLoopFrames - the only honest "is gameplay running" signal here
#include "appearance.h"
#include "ui.h"       /* E43 / P7z: the MULTIPLAYER button on the title screen */
#include "tags.h"     /* tags1: name labels over the other player's characters */
#include "bugreport.h" /* T-461: REPORT A BUG */
#include "playerstab.h" /* T-545: the PLAYERS tab, other players' factions on the FACTION tab */
#include "fallentab.h"   /* T-556: the FALLEN tab */
#include "chat.h"        /* in-game text chat */
#include "titleart.h"  /* T-513: the mod's own title-screen art */
#include "config.h"   /* E38 / decisions 42-43: shared_wastelands.cfg - the role and the addresses */
#include "addresses.h" /* P8h: the executable fingerprint and its address table - the gate below */
#include "net/steam_probe.h" /* T-290 S0: Steam networking and relay availability, probed and logged */
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3MainLoopGpu = 0; static coop::AddrReg kMig3MainLoopGpu_reg("GameWorld_gpuFrame", &kMig3MainLoopGpu);   /* Steam_1.0.65 0x787E70 */
static unsigned long long kMig3InitGameData = 0; static coop::AddrReg kMig3InitGameData_reg("GameWorld_setupRecords", &kMig3InitGameData);   /* Steam_1.0.65 0x86FC10 */
static unsigned long long kMig3TitleUpdate = 0; static coop::AddrReg kMig3TitleUpdate_reg("TitleScreen_update", &kMig3TitleUpdate);   /* Steam_1.0.65 0x9129B0 */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "../common/modsenabled.h"   /* owner 123/124: is this copy switched on in Kenshi's mod list */
#include <ogre/OgrePlugin.h>   /* RE_Kenshi's road waits for the launcher's OK as an Ogre plugin object */
#include <ogre/OgreRoot.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <iomanip>
#include <locale>

// RE_Kenshi's process-wide locale inserts digit-group separators into numbers, which
// mangles pointer output (0,000,7FF,...). Always format through this helper.
static std::string fmtPtr(const void* p)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0')
       << (unsigned long long)p;
    return ss.str();
}

static std::string fmtNum(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

static void probeOu(const char* when);   // P003, defined below

// ---------------------------------------------------------------------------
// PROBE-START: P001 - main-loop hook (frame pump proof)
// Question: can we hook a game function, and does the hook run on
// the main thread every frame? Establishes the pump for the future command queue.
// Remove or promote to final form once the command queue lands.
// ---------------------------------------------------------------------------
static void (*orig_mainLoop)(GameWorld*, float) = 0;
static long long g_frames = 0;
static bool g_firstFrameLogged = false;

// F333 - HAS GAMEPLAY STARTED? There is no null world to test against: F034 established that the
// GameWorld is a STATIC inside the exe image, so `ou` is valid and non-null from preload onward,
// and a pointer check therefore cannot distinguish "at the title screen" from "in a loaded game".
// This counter can: it is incremented only by the in-game main loop, which stops running at the
// menu and does not start until a save loads (T008).
//
// P039 needs it because it reads world state, and without it the probe prints a confident
// `paused=0 speedMul=0.00` for a world that does not exist yet - and spends its priming sample
// there, manufacturing a transition during load.
namespace coop { long long MainLoopFrames() { return g_frames; } }

/* tickwait (T450, Read 2026-09-27) - WAIT FOR THE ENGINE'S AI WORKER PASS BEFORE ANY OF OUR PER-FRAME WORK.
   GameWorld::mainLoop 0x787E70 begins by waiting for the AI worker thread object at GameWorld+0x790: when 0x25C690
   says that thread is busy it takes the thread's boost::shared_mutex (+0x68) exclusively with no deadline and releases
   it at once (build/decomp_787e70.txt, top). 0x78A070 is that same take-and-release as a function, taking the THREAD
   object (decomp_78a070.txt; 0x82AAF0 calls it with DAT_142133840 = GameWorld 0x1421330B0 + 0x790 before a teardown,
   and SaveManager::execute runs only after mainLoop's wait). This detour IS 0x787E70, and everything below touched live
   characters BEFORE orig_mainLoop - i.e. while the previous frame's AI pass could still be running (T450: the worker
   died in CharMovement::update -> MedianFilter::apply on storage zeroed mid-call). So the detour now makes the engine's
   own wait first, with mainLoop's own gate; mainLoop's wait then finds the worker already idle.
   NO LOCK OF OURS IS HELD HERE: this is the detour's first work, entered from the engine. A fault logs once and turns
   the wait off for the process. The counters print on the [SAVE] hb[ line (coop::TickWaitToken). */
static unsigned long long kGameWorldWaitAiRva = 0; static coop::AddrReg kGameWorldWaitAiRva_reg("GameWorldWaitAi", &kGameWorldWaitAiRva);   /* Steam_1.0.65 0x78A070: bool (thread*) - lock_exclusive(+0x68, no deadline) then unlock */
static unsigned long long kAiThreadBusyRva = 0;    static coop::AddrReg kAiThreadBusyRva_reg("AiThreadBusy", &kAiThreadBusyRva);            /* Steam_1.0.65 0x25C690: bool (thread*) - mainLoop's gate before its wait */
typedef unsigned char (*AiThreadFn)(void*);
static long long g_twCalls = 0, g_twUsTotal = 0, g_twUsMax = 0, g_twFaults = 0;
static int g_twOff = 0;

/* 1 waited, 0 nothing to wait for, -1 faulted. No C++ object in this function (C2712). */
static int TickWaitAiRaw(void* gw)
{
    __try
    {
        void* thr = *(void**)((char*)gw + 0x790);
        if (thr == 0) return 0;
        char* base = (char*)::GetModuleHandleA(0);
        AiThreadFn busy = (AiThreadFn)(base + kAiThreadBusyRva);
        if (busy(thr) == 0) return 0;
        AiThreadFn wait = (AiThreadFn)(base + kGameWorldWaitAiRva);
        wait(thr);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void TickWaitAi(GameWorld* gw)
{
    if (g_twOff || gw == 0 || kGameWorldWaitAiRva == 0 || kAiThreadBusyRva == 0) return;
    LARGE_INTEGER f, t0, t1;
    ::QueryPerformanceFrequency(&f);
    ::QueryPerformanceCounter(&t0);
    const int r = TickWaitAiRaw(gw);
    if (r < 0)
    {
        ++g_twFaults; g_twOff = 1;
        ErrorLog("[P001] tickWait: the AI-thread wait (0x78A070) faulted - turned off for this process");
        return;
    }
    if (r == 0) return;
    ::QueryPerformanceCounter(&t1);
    const long long us = (f.QuadPart > 0) ? (long long)((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart) : 0;
    ++g_twCalls; g_twUsTotal += us;
    if (us > g_twUsMax) g_twUsMax = us;
}

namespace coop {
std::string TickWaitToken()
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "tickWait[calls,msMax,msAvg,faults]=" << g_twCalls << "," << std::fixed << std::setprecision(2)
       << (g_twUsMax / 1000.0) << "," << (g_twCalls > 0 ? (g_twUsTotal / 1000.0) / (double)g_twCalls : 0.0)
       << "," << g_twFaults;
    return ss.str();
}
}

static void detour_mainLoop(GameWorld* thisptr, float time)
{
    ++g_frames;

    if (!g_firstFrameLogged)
    {
        g_firstFrameLogged = true;
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "[P001] first main-loop frame. this=" << fmtPtr(thisptr)
           << " ou=" << fmtPtr(coop::GameWorldPtr())
           << (thisptr == coop::GameWorldPtr() ? " (this==ou)" : " (this!=ou)")
           << " threadId=" << fmtNum((long long)::GetCurrentThreadId());
        DebugLog(ss.str());
    }

    /* tickwait: the engine's own AI-worker wait FIRST - nothing of ours below may overlap the previous AI pass */
    TickWaitAi(thisptr);

    // The command channel must ride BOTH pumps: TitleScreen::update stops the moment a
    // save loads (T008), so without this the channel would go dead exactly when in-game
    // testing begins. Both pumps are main-thread, and the tick is internally throttled.
    coop::CommandChannelTick();
    coop::SteamProbeTick();   /* T-290 S0 mainloop: a save loaded before the relay status settled keeps being polled here (returns at once when done) */

    // P011 order-lifecycle watch. In-game pump only - there are no characters at the
    // menu. Costs nothing while no watch is active, and logs only on change (F111).
    coop::WatchTaskTick();
    coop::AppearanceTick();   // H010a: send/apply appearance rolls once they settle (F157)
    coop::CorpseDecayTick();  // a dead copy of another game's character is held short of the rot limit; its owner decides
    coop::ProneParkTick();    // R1-a/R1-a-b: parked prone writes, once CharacterProneSafe says yes
    coop::CarryWatchTick();   // K1 (read-carry): an owned carrier's carry edge -> STATE at once
    coop::CarryApplyTick();   // K1: copies carry what their owners' STATE says, once both are built
    coop::PrisonTick();       // arrest2: a copy caged here is reported to its owner; an owner caged there is caged here
    coop::TreatTick();        // heal1: a medic here treated a copy -> its owner applies the treatment
    coop::NameTick();         // names1: an owned character's rename (marked by the setName hook) -> MSG_NAME
    coop::CaptureTick();      /* P11: what the slaver bodies recorded about a copy -> MSG_CAPTURE; timeouts */
    coop::SlaveTick();        // slave1: an owned character's slave-state change (marked by the hooks) -> MSG_SLAVE
    coop::RestApplyTick();    // R3 (read-ragdoll): post / verify the move of a settled copy's ragdoll to its owner's rest
    coop::GetupPulseTick();   // G1 (read-getup): a copy still ragdolled after its owner got up gets one AI decision pulse
    coop::SneakApplyTick();   // copies sneak or stand as their owners' STATE says, through the engine's setStealthMode
    coop::PoseWatchTick();    // POSE (read-poses): an owned character's in-place pose changed -> STATE at once
    coop::PoseApplyTick();    // POSE: copies take their owners' bed / replayed action, and are held still while posed
    coop::InteriorKeepTick(); /* P25: a building the other player's copy stands in keeps its inside loaded here (the engine's own keep call) */

    // heartbeat: ~every 5s at 60fps, cheap enough to leave in during the probe
    if (g_frames % 300 == 0)
    {
        std::stringstream ss;
        ss << "[P001] frames=" << fmtNum(g_frames)
           << " dt=" << fmtNum((long long)(time * 1000.0f)) << "ms";
        DebugLog(ss.str());
    }

    orig_mainLoop(thisptr, time);

    /* tags1: "Name (Faction)" labels over the other player's characters (and the Insert toggle).
       tags1 fold (2e): AFTER the engine loop - this hook IS 0x787E70, which copies the camera's view matrix into the
       UtilityT object the labels project with (build/decomp_787e70.txt:36), so running first used last frame's camera.
       Nothing in the tick has to precede the engine loop: it only reads positions and the camera and moves our own widgets. */
    coop::TagsTick();
    coop::PlayersTabTick();   /* T-545: other players' factions made known; the PLAYERS tab's table kept current while it shows */
    coop::FallenTabTick();    /* T-556: the FALLEN tab after PLAYERS, kept current while it shows; a confirmed bring-back runs */
    coop::BugReportTick(0);   /* T-461: REPORT A BUG - its window over the pause menu, the nearby-log ask and answer */
    coop::ChatTick();         /* in-game text chat - Enter, the window, the fading feed, join / leave lines */
    coop::UiDriveEngineClickFlush(0);   /* pp1b: a queued `uiclick engine:` fires at the tail of the in-world pump; T-201 PP6': never the one-press load */
}

static void installProbeP001()
{
    // Log BEFORE resolving, so a fault while resolving is preceded by its line (F032). The target is our
    // table row GameWorld_gpuFrame.
    DebugLog("[P001] resolving mainLoop_GPUSensitiveStuff via the address table...");

    intptr_t target = (intptr_t)coop::AddrAbs(kMig3MainLoopGpu);

    std::stringstream ss;
    ss << "[P001] hook target mainLoop_GPUSensitiveStuff = " << fmtPtr((void*)target);
    DebugLog(ss.str());

    if (target == 0)
    {
        ErrorLog("[P001] target address resolved to 0 - hook NOT installed");
        return;
    }

    coop::HookStatus st =
        coop::AddHook((void*)target, (void*)&detour_mainLoop, (void**)&orig_mainLoop);

    DebugLog(st == coop::SUCCESS ? "[P001] AddHook SUCCESS" : "[P001] AddHook FAILED");
}
// PROBE-END: P001

// ---------------------------------------------------------------------------
// PROBE-START: P002 - control hook (does OUR hooking work at all?)
// Question: P001's detour never fired at the main menu (T004 run 2). Two suspects:
//   (a) mainLoop_GPUSensitiveStuff simply isn't called at the menu (no GameWorld yet)
//   (b) our hooks install but aren't wired to executed code
// GameWorld::initialisationGameData IS called in our exact startup sequence -
// RE_Kenshi hooks it for its own mod loading, and we observed "Loading mod overrides"
// in every run. If P002 fires and P001 stays silent, (b) is eliminated.
// ---------------------------------------------------------------------------
static void (*orig_initGameData)(GameWorld*) = 0;
static void detour_initGameData(GameWorld* thisptr)
{
    std::stringstream ss;
    ss << "[P002] initialisationGameData ENTERED. this=" << fmtPtr(thisptr)
       << " threadId=" << fmtNum((long long)::GetCurrentThreadId());
    DebugLog(ss.str());

    orig_initGameData(thisptr);

    probeOu("after initialisationGameData");
    DebugLog("[P002] initialisationGameData returned");
}

static void installProbeP002()
{
    DebugLog("[P002] resolving initialisationGameData via the address table...");
    intptr_t target = (intptr_t)coop::AddrAbs(kMig3InitGameData);

    std::stringstream ss;
    ss << "[P002] hook target initialisationGameData = " << fmtPtr((void*)target);
    DebugLog(ss.str());

    if (target == 0)
    {
        ErrorLog("[P002] target resolved to 0 - hook NOT installed");
        return;
    }

    coop::HookStatus st =
        coop::AddHook((void*)target, (void*)&detour_initGameData, (void**)&orig_initGameData);
    DebugLog(st == coop::SUCCESS ? "[P002] AddHook SUCCESS" : "[P002] AddHook FAILED");
}
// PROBE-END: P002


// ---------------------------------------------------------------------------
// PROBE-START: P005 - menu-time frame pump (TitleScreen::update)
// Replaces the withdrawn P004, which hooked a 5-byte float setter picked by NAME
// (F037). This target was VERIFIED OFFLINE FIRST per F038: resolve_stub.py maps
// TitleScreen::update -> game RVA 0x9129B0, and Ghidra confirms a real
// 739-byte function with 11 distinct callees at that exact entry point.
// If it ticks at the menu while P001 stays at 0, suspect (a) is proven AND we have
// the menu-side pump needed to start a game programmatically.
// ---------------------------------------------------------------------------
static void (*orig_titleUpdate)(TitleScreen*) = 0;
static long long g_titleFrames = 0;
static bool g_titleFirstLogged = false;

static void detour_titleUpdate(TitleScreen* thisptr)
{
    ++g_titleFrames;

    if (!g_titleFirstLogged)
    {
        g_titleFirstLogged = true;
        std::stringstream ss;
        ss << "[P005] first TitleScreen::update. this=" << fmtPtr(thisptr)
           << " ou=" << fmtPtr(coop::GameWorldPtr())
           << " threadId=" << fmtNum((long long)::GetCurrentThreadId());
        DebugLog(ss.str());
    }

    if (g_titleFrames % 120 == 0)
    {
        std::stringstream ss;
        ss << "[P005] titleFrames=" << fmtNum(g_titleFrames)
           << " P001frames=" << fmtNum(g_frames)
           << " " << coop::AddrReportToken()   /* P8h - the gate's own counters, on the pump that runs first */
           << " " << coop::UiReportToken();   /* E43 / P7z - the title button rides THIS pump, so its
                                                counters belong on THIS periodic line */
        DebugLog(ss.str());
    }

    /* P8h - THE REFUSAL PATH, AND IT IS THE WHOLE OF WHAT A REFUSED GAME RUNS. When the address gate said no,
       this pump exists for one reason: to put the caption on the title screen. No link is opened, no command is
       read, and no hook other than this one was ever installed - the game is unmodded Kenshi with a disabled
       button on its menu. */
    if (coop::AddrOk() == 0)
    {
        coop::UiTitleTick();
        orig_titleUpdate(thisptr);
        return;
    }

    /* E38 / decision 42 - THE LINKS ARE OPENED HERE, ON THE FIRST TITLE TICK, and not in startPlugin. Preload is
       the wrong place twice over: a fault there deadlocked the process with no window, no dump and flat
       CPU (F032), and this call creates ENet transports. It is above CommandChannelTick on purpose, so the pump
       below services the link it just opened in this same tick rather than the next one. Self-latched: it does
       its work once and returns immediately every frame after. */
    coop::ConfigTitleTick();

    // Command channel rides this pump: it is the only main-thread function known to
    // run at the menu (F040), and Kenshi ignores injected input (F010).
    coop::UiTitleScreenNote(thisptr);   /* T-220: the TEST-ONLY `uiclick escape` calls the engine's ESC arm with this title */
    coop::CommandChannelTick();

    /* E43 / P7z - the MULTIPLAYER button. Last of the three, because it is the only one that can be
       skipped without consequence: it caches nothing, owns no link and writes no file, so a frame it
       misses is a frame in which the button is simply not put back yet. It disables itself for the
       process on its first fault. */
    coop::TitleArtTick();    /* T-513: the mod's own art as the title screen's background - before the menu column and the note are placed on it */
    coop::UiTitleTick();
    coop::BugReportTick(1);  /* T-461: REPORT A BUG - the title screen's button and its window */
    coop::PlayersTabTitleTick();   /* T-545: a SET HOSTILE box left up from the world is taken down */
    coop::ChatTitleTick();         /* the chat of the world just left is taken down */
    coop::FallenTabTitleTick();    /* T-556: a BRING BACK box left up from the world is taken down */
    coop::TagsTitleTick();   /* tags1: no world at the title - a name label still standing is destroyed */
    coop::SteamProbeTick();  /* T-290 S0 title: the first title frame looks Steam up (the game's SteamAPI_Init has run); later frames poll the relay status until it settles. Logs only. */

    orig_titleUpdate(thisptr);
    /* pp1b: a queued `uiclick engine:` fires HERE - after the engine's own title update has returned, the latest point this hook
       owns; nothing below touches thisptr, so a handler that tears the title screen down has nothing of ours to break */
    coop::UiDriveEngineClickFlush(1);   /* T-201 PP6': the title is up - the one-press load may post here */
}

/* Returns true when the hook is in. owner 97a: with no usable address table AddrInit has already put the byte-pattern
   answer into this same row (or left it 0), so this one path serves both. */
static bool installProbeP005()
{
    DebugLog("[P005] resolving TitleScreen::update via the address table row (or, with no usable table, the byte pattern)...");
    intptr_t target = (intptr_t)coop::AddrAbs(kMig3TitleUpdate);

    std::stringstream ss;
    ss << "[P005] hook target TitleScreen::update = " << fmtPtr((void*)target)
       << " (row TitleScreen_update = game RVA 0x" << std::hex << std::uppercase << kMig3TitleUpdate << std::dec
       << " as AddrInit bound it from '" << coop::AddrTableName() << "' or the title pattern, + module base)";
    DebugLog(ss.str());

    if (target == 0)
    {
        ErrorLog("[P005] target resolved to 0 - hook NOT installed");
        return false;
    }

    coop::HookStatus st =
        coop::AddHook((void*)target, (void*)&detour_titleUpdate, (void**)&orig_titleUpdate);
    DebugLog(st == coop::SUCCESS ? "[P005] AddHook SUCCESS" : "[P005] AddHook FAILED");
    return st == coop::SUCCESS;
}
// PROBE-END: P005

// ---------------------------------------------------------------------------
// PROBE-START: P003 - what does `ou` actually contain?
// T003/T004 printed ou = 0x00007FF7719A30B0, which looks like it lies in the exe's
// .data section rather than being a heap object. Suspicion: we may be reading the
// import slot (&ou) instead of the pointer VALUE. Printing both settles it, and the
// answer matters before anything dereferences ou.
// ---------------------------------------------------------------------------
static void probeOu(const char* when)
{
    std::stringstream ss;
    ss << "[P003] " << when
       << " ou(value)=" << fmtPtr(coop::GameWorldPtr());   /* our GameWorldGlobal row */
    DebugLog(ss.str());
}
// PROBE-END: P003

// --- PROBE P008: world-wide AI freeze via PlayerInterface::characterEditorMode (F064) ---
// The engine's own "freeze all character AI" flag (checked by Character::threadedUpdatePeriodic
// before ANY AI::periodicUpdate runs - F059). Candidate resync-quiesce mechanism.
// Offsets verified against OUR binary: PlayerInterface singleton global at RVA 0x2133630
// (decomp F059/F064); GameWorld::player at +0x580 (header). SAFETY: the two independent
// paths must agree on the same object or the command refuses to write anything.
namespace coop {

static unsigned long long kCoPlayerIfaceRva = 0; static coop::AddrReg kCoPlayerIfaceRva_reg("PlayerInterfaceGlobal", &kCoPlayerIfaceRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x2133630 */
bool SetWorldAIFreeze(bool on)
{
    if (coop::GameWorldPtr() == 0) { DebugLog("[P008] aifreeze: ou is null"); return false; }

    void* viaWorld  = *(void**)((char*)coop::GameWorldPtr() + 0x580);
    if (kCoPlayerIfaceRva == 0) { DebugLog("[P008] aifreeze: PlayerInterfaceGlobal is not in the address table"); return false; }
    void* viaGlobal = *(void**)(uintptr_t)coop::AddrAbs(kCoPlayerIfaceRva);
    if (viaWorld == 0 || viaWorld != viaGlobal)
    {
        ErrorLog(std::string("[P008] aifreeze REFUSED: PlayerInterface mismatch world=")
                 + fmtPtr(viaWorld) + " global=" + fmtPtr(viaGlobal));
        return false;
    }

    unsigned char* flag = (unsigned char*)viaWorld + 0x2F0;
    int before = *flag;
    *flag = on ? 1 : 0;
    DebugLog(std::string("[P008] world AI freeze ") + (on ? "ON" : "OFF")
             + ": characterEditorMode " + fmtNum(before) + " -> " + fmtNum(*flag));
    return true;
}

} // namespace coop

static void logEnvironment()
{
    /* the game build is identified by OUR fingerprint of the executable. Called right AFTER coop::AddrInit on both paths (accepted and refused), so the fingerprint and the
       table's name are what AddrInit computed; "unknown" = AddrInit refused before it got that far (e.g. the
       executable could not be read, or no table exists for this fingerprint). */
    std::string fp    = coop::AddrExeFingerprint();
    std::string table = coop::AddrTableName();
    std::stringstream ss;
    ss << "kenshi-coop: game build fingerprint=" << (fp.empty() ? std::string("unknown") : fp)
       << " table='" << (table.empty() ? std::string("unknown") : table) << "'"
       << " ou=" << fmtPtr(coop::GameWorldPtr())
       << " sizeof(std::string)=" << fmtNum((long long)sizeof(std::string)) << " (40=v100 ABI)";
    DebugLog(ss.str());
}

/* Every hook this plugin installs goes through coop::AddHook during the start (startPluginOn) - nothing is installed
   after it - so these are the totals. */
static void logHookCounts()
{
    long installed = 0, failed = 0;
    coop::HookCounts(&installed, &failed);
    DebugLog("[HOOK] own MinHook (multihook 4f18d18): " + fmtNum(installed) + " installed, "
             + fmtNum(failed) + " failed");
}

/* THE START-ONCE GUARD. The mod can be started by two roads in one game, both after the launcher's OK, from inside Ogre's
   Root::initialise: SharedWastelandsLoader.dll (the game's Plugins_x64.cfg line; its Ogre plugin object decides, then calls
   coopEarlyStart -> here) and RE_Kenshi (mods\Shared Wastelands\RE_Kenshi.json -> startPlugin, which registers this DLL's own
   Ogre plugin object; its initialise -> here). A player can have both, and --norekenshi turns RE_Kenshi's road off.
   The FIRST caller in the process starts the mod; every later caller logs "second start ignored" and returns.
   It is a per-process NAMED mutex, not a static, because the two roads can load two different copies of this DLL (a local
   mods copy and a Workshop copy are two modules with two sets of statics) - the name is the one thing both copies share.
   The handle is kept for the life of the process. If Windows will not create the mutex at all, this copy's own flag is the
   guard and the log says so. */
static HANDLE g_startGuard = 0;
static volatile LONG g_startedHere = 0;

static bool claimStart(const char* road)
{
    if (::InterlockedExchange(&g_startedHere, 1) != 0)
    {
        DebugLog(std::string("kenshi-coop: second start ignored (") + road + ") - this copy of the mod has already started");
        return false;
    }
    wchar_t name[64];
    swprintf_s(name, 64, L"Local\\KenshiCoop-start-%lu", (unsigned long)::GetCurrentProcessId());
    HANDLE h = ::CreateMutexW(0, FALSE, name);
    const DWORD err = ::GetLastError();
    if (h == 0)
    {
        ErrorLog(std::string("kenshi-coop: the start-once guard could not be created (Windows error ") + fmtNum((long long)err)
                 + ") - starting (" + road + "); only this copy's own flag guards against a second start");
        return true;
    }
    if (err == ERROR_ALREADY_EXISTS)
    {
        ::CloseHandle(h);
        DebugLog(std::string("kenshi-coop: second start ignored (") + road + ") - another copy of the mod already started in this process");
        return false;
    }
    g_startGuard = h;
    DebugLog(std::string("kenshi-coop: first start in this process (") + road + ") - start-once guard held");
    return true;
}

/* mig6 fold (review of 7d96a7d, F6): a start already claimed by ANY copy in this process? Asked BEFORE AddrInit, so a second
   copy still does nothing at all once one has started. It only LOOKS (OpenMutexW) - claimStart, after the gate accepted,
   is the claim. */
static bool startClaimedAlready(const char* road)
{
    if (g_startedHere != 0)
    {
        DebugLog(std::string("kenshi-coop: second start ignored (") + road + ") - this copy of the mod has already started");
        return true;
    }
    wchar_t name[64];
    swprintf_s(name, 64, L"Local\\KenshiCoop-start-%lu", (unsigned long)::GetCurrentProcessId());
    HANDLE h = ::OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (h == 0) return false;
    ::CloseHandle(h);
    DebugLog(std::string("kenshi-coop: second start ignored (") + road + ") - another copy of the mod already started in this process");
    return true;
}

/* mig6 fold (review of 7d96a7d, F6) - THE REFUSAL GUARD. A copy the address gate REFUSES no longer claims the start (a local
   copy with no table for this exe must not block a Workshop copy that has one). It still puts in the title-only hook so the
   greyed button carries the reason - but ONCE per process: the first refusing copy does it (per-process named mutex, the
   same reasoning as claimStart's), later refusing copies install nothing. */
static HANDLE g_refusalGuard = 0;
static volatile LONG g_refusalHere = 0;

static bool claimRefusalHook(const char* road)
{
    if (::InterlockedExchange(&g_refusalHere, 1) != 0)
    {
        DebugLog(std::string("kenshi-coop: refused again (") + road + ") - this copy already did the refused road; nothing more installed");
        return false;
    }
    wchar_t name[64];
    swprintf_s(name, 64, L"Local\\KenshiCoop-refused-%lu", (unsigned long)::GetCurrentProcessId());
    HANDLE h = ::CreateMutexW(0, FALSE, name);
    const DWORD err = ::GetLastError();
    if (h == 0)
    {
        ErrorLog(std::string("kenshi-coop: the refusal guard could not be created (Windows error ") + fmtNum((long long)err)
                 + ") - refused road (" + road + ") goes on; only this copy's own flag guards against a second title-only hook");
        return true;
    }
    if (err == ERROR_ALREADY_EXISTS)
    {
        ::CloseHandle(h);
        DebugLog(std::string("kenshi-coop: refused (") + road + ") - another copy already put in the title-screen-only hook;"
                 " nothing installed here, and the start is NOT claimed (another copy may still start the mod)");
        return false;
    }
    g_refusalGuard = h;
    return true;
}

/* Owner decisions 123/124 - IS THIS COPY SWITCHED ON IN KENSHI'S MOD LIST? Both roads ask FIRST, before anything else of
   the mod runs, and both ask after the launcher's OK (from inside Ogre's Root::initialise), when the launcher has written the
   list. RE_Kenshi calls startPlugin for every INSTALLED mod, switched on or not, so this road must ask; the loader's road
   asks the same question by the same rule.
   This copy is switched on when one of the *.mod files in ITS OWN folder is
   a line of Kenshi's mod list, <game>\data\mods.cfg (src/common/modsenabled.h - the same rule the loader uses).
   THE GAME FOLDER: the first of <exe folder> and its PARENT that holds data\mods.cfg. The exe folder is the game folder
   when the game runs normally; the parent covers RE_Kenshi relaunching the game from <game>\RE_Kenshi\ (review F5).
   Only one level up: further up is no longer the game.
   Missing, unreadable, over 64 KB, or an exception while asking -> OFF (owner 124, fail closed). The answer is asked once per
   copy; the one line saying why is logged once. */
static int g_switchState = 0;   /* 0 = not asked yet, 1 = on, 2 = off */

static std::wstring wFolderOf(const std::wstring& p)
{
    const std::wstring::size_type slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}

static bool wIsFile(const std::wstring& p)
{
    const DWORD a = ::GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::string wUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, 0, 0);
    return out;
}

/* true = switched on; `why` = the reason either way (UTF-8, for the log) */
static bool modSwitchedOnCheck(std::string& why)
{
    HMODULE self = 0;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)(void*)&modSwitchedOnCheck, &self))
    { why = "this DLL's own module could not be found"; return false; }
    wchar_t buf[2048];
    DWORD n = ::GetModuleFileNameW(self, buf, 2048);
    if (n == 0 || n >= 2048) { why = "this DLL's own path could not be read"; return false; }
    const std::wstring ownDir = wFolderOf(std::wstring(buf, n));
    n = ::GetModuleFileNameW(0, buf, 2048);
    if (n == 0 || n >= 2048) { why = "the game's own path could not be read"; return false; }
    const std::wstring exeDir = wFolderOf(std::wstring(buf, n));
    if (ownDir.empty() || exeDir.empty()) { why = "a module path has no folder"; return false; }

    std::wstring cfg = exeDir + L"\\data\\mods.cfg";
    if (!wIsFile(cfg))
    {
        const std::wstring up = wFolderOf(exeDir);
        const std::wstring cfgUp = up.empty() ? std::wstring() : up + L"\\data\\mods.cfg";
        if (cfgUp.empty() || !wIsFile(cfgUp))
        {
            why = "Kenshi's mod list was not found (" + wUtf8(cfg) + ", nor one folder up)";
            return false;
        }
        cfg = cfgUp;
    }
    std::string text((size_t)coopmods::kModsCfgMaxBytes + 1, '\0');
    DWORD got = 0, err = 0;
    BOOL ok = FALSE;
    HANDLE h = ::CreateFileW(cfg.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) err = ::GetLastError();
    else
    {
        ok = ::ReadFile(h, &text[0], (DWORD)text.size(), &got, 0);
        err = ::GetLastError();
        ::CloseHandle(h);
    }
    const coopmods::ModsCfgState st = coopmods::ModsCfgReadState(h != INVALID_HANDLE_VALUE, ok != FALSE, (unsigned long)got);
    if (st != coopmods::kModsCfgUsable)
    {
        why = "Kenshi's mod list " + wUtf8(cfg) + " " + coopmods::ModsCfgStateWhy(st);
        if (st != coopmods::kModsCfgTooLarge) why += " (Windows error " + fmtNum((long long)err) + ")";
        return false;
    }

    WIN32_FIND_DATAW fd;
    HANDLE f = ::FindFirstFileW((ownDir + L"\\*.mod").c_str(), &fd);
    std::string names;
    if (f != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            const std::wstring w(fd.cFileName);
            if (w.size() < 5 || _wcsicmp(w.c_str() + w.size() - 4, L".mod") != 0) continue;   /* *.mod also matches 8.3 names */
            const std::string u = wUtf8(w);
            if (coopmods::ModsCfgLists(text.data(), (long)got, u.c_str()))
            {
                ::FindClose(f);
                why = u + " is switched on in " + wUtf8(cfg);
                return true;
            }
            names += (names.empty() ? "" : ", ") + u;
        } while (::FindNextFileW(f, &fd));
        ::FindClose(f);
    }
    if (names.empty()) why = "this copy's folder " + wUtf8(ownDir) + " has no .mod file, so it cannot be switched on in " + wUtf8(cfg);
    else why = "switched off in Kenshi's mod list - " + names + " (in " + wUtf8(ownDir) + ") is not a line of " + wUtf8(cfg);
    return false;
}

static bool modSwitchedOn(const char* road)
{
    if (g_switchState != 0) return g_switchState == 1;   /* asked already; its line is logged */
    std::string why;
    bool on = false;
    try { on = modSwitchedOnCheck(why); }
    catch (...) { on = false; why = "an exception while reading Kenshi's mod list"; }
    g_switchState = on ? 1 : 2;
    if (on)
        DebugLog(std::string("kenshi-coop: ") + why + " (" + road + ")");
    else
        DebugLog(std::string("kenshi-coop: NOT STARTED (") + road + ") - " + why + ". Nothing was installed, not even the"
                 " title-screen hook; this game behaves as unmodded Kenshi. (Owner 123/124: the mod starts only when it is"
                 " switched on in Kenshi's mod list, and stays off when that list cannot be read.)");
    return on;
}

/* The refused road (the address gate said no): ONLY the title-screen hook, so the greyed MULTIPLAYER button carries the
   reason (owner 98). Called from startPluginOn, so both roads use it. */
static void startRefused()
{
    logEnvironment();   /* mig2 fold: after AddrInit, so it carries the fingerprint AddrInit computed */
    const bool titleHooked = installProbeP005();
    /* T-168: say what actually happened - the button carries the reason only if the title hook went in. */
    if (titleHooked)
        DebugLog("kenshi-coop: DISABLED - the address gate refused this executable (see the [ADDR] lines above)."
                 " The title screen was hooked (from the table's row, or by the byte pattern when there is no"
                 " usable table), so the greyed-out MULTIPLAYER button carries the reason; nothing else was"
                 " installed and this game behaves as unmodded Kenshi.");
    else
        ErrorLog("kenshi-coop: DISABLED - the address gate refused this executable (see the [ADDR] lines above)."
                 " The title screen could NOT be hooked either (see the [ADDR] and [P005] lines), so NO button"
                 " shows the reason and this log is the only place it is said. Nothing was installed and this"
                 " game behaves as unmodded Kenshi.");
    logHookCounts();   /* mig2 (T-155): the same totals line on this path too */
}

static void startPluginOn(const char* road)
{
    LARGE_INTEGER freq, t0;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&t0);
    if (!modSwitchedOn(road)) return;        /* mig6 fold (F1, owner 123/124): FIRST - switched off or unreadable = nothing */
    if (startClaimedAlready(road)) return;   /* mig6: before AddrInit - a second copy of the DLL must do nothing at all */
    DebugLog("kenshi-coop: plugin start");
    probeOu("at the start, after the launcher's OK");
    /* P8h - THE GATE, AND IT IS FIRST. Every RVA below was read out of ONE executable (Kenshi 1.0.65 x64 Steam,
       .modding/01-environment.md); on any other build they are arbitrary addresses. AddrInit fingerprints the
       running executable, loads addresses\<fingerprint>.txt from beside this DLL, and checks the bytes at every
       address that table names. If anything at all is wrong it logs ONE line and returns 0, and then the only
       thing this plugin installs is the title-screen pump - so the caption can say so - and NOTHING else: no
       command channel, no session, no store, no hook on any engine function. That is the same end state as
       role=single (config.h), reached because the numbers do not fit this binary rather than because a file
       said so. REFUSE RATHER THAN CRASH (user, approved 2026-09-04). */
    if (coop::AddrInit() == 0)
    {
        /* mig6 fold (F6): a refusing copy does NOT claim the start; its title-only hook goes in once per process */
        if (claimRefusalHook(road)) startRefused();   /* the title-screen hook only, once per process */
        return;
    }
    if (!claimStart(road)) return;   /* mig6 fold (F6): the start is claimed only once the gate has ACCEPTED this exe */
    logEnvironment();   /* mig2 fold: after AddrInit, so it carries the fingerprint AddrInit computed */
    /* E38 / decisions 42-43. FIRST, so every install below can see the role: InstallStore must know whether to read
       the notebook's record index at all (role=single must not - the folder belongs to a co-op world), and the slot
       has to be known before the first store HELLO can be sent. Reading a text file cannot fault the start the
       way a socket can, so unlike the linking half this DOES belong here. */
    coop::ConfigLoad();
    installProbeP001();
    installProbeP002();
    installProbeP005();
    /* P8i-b (review-p8i H-1): the front-end ESC arm reaches Kenshi's quit byte through
       TitleScreen::closeTheOtherBits, which cannot see a widget this plugin created - so with the
       Multiplayer panel open, Escape exited the game. This detour lets the panel answer that call. */
    coop::InstallUiTitleClose();
    coop::InstallUiPauseMenuArrange();   /* T-514 (owner 448): the pause menu's NEW GAME / SAVE GAME / LOAD GAME hidden in multiplayer, the menu closed up */
    coop::InstallAiSpike();
    coop::InstallCombatHook();
    coop::InstallCombatUpdateProbe();   // P045/T095
    // P058/F399. **Ordered AFTER the line above deliberately** - that call caches the image base
    // this recorder needs to turn a captured return address into an RVA.
    coop::InstallEndActionProbe();
    coop::InstallMedicalProbe();   // M4 step 1: measurement only, changes nothing
    coop::InstallStore();          // P1 persistence: sleep/wake hooks + the record store
    coop::InstallRelations();      // P3 piece 2: faction standings forwarded by their owner
    // E42 (approved 2026-09-05): the dev-only peace lever. Installed UNCONDITIONALLY and gated by a flag
    // that starts OFF - MinHook must not patch isEnemyOf / isAllyOf mid-run, because the AI worker
    // thread is executing them continuously. Installing here and testing a flag there is the safe order.
    coop::InstallPeace();          // E42: peace on|off - read-time is-enemy / is-ally overrides, writes nothing
    coop::InstallTownGen();        // P4: the client refuses town squad generation in host-held sectors (decision 25)
    // P032: counts world population generation. Counters only by default - `worldGenOn` starts at
    // 1, meaning the engine generates its world exactly as it does today. Phrased as the ENGINE's
    // state rather than "the gate is off", because two log labels have already cost a reader time
    // in this project by describing the switch instead of the behaviour.
    coop::InstallItems();        /* E22a / decision 38: the four item-transfer hooks - the owner of a character publishes its item moves */
    coop::InstallAnimalCreateBodyGuard();   /* crash2: AppearanceAnimal::createBody 0x539C50 - an animal copy's body rebuild waits while it lies in ragdoll (the P091 probe logs from inside it) */
    coop::InstallHumanCreateBodyGuard();    /* T-293 fold 1 (F2): AppearanceHuman::createBody 0x539440 - the same wait for a human copy in ragdoll */
    /* E45 / decision 40 (P8e): the three door detours.  AFTER InstallItems, because the door road
       borrows items.cpp's key builder and its holder rule and must not run before that file's own
       state is set up. */
    coop::InstallDoors();
    coop::InstallPolicy();       /* E36 / decision 40: OwnedByAPlayerFaction 0x546340 - under `shared` the other player's buildings answer as ours */
    coop::InstallWorldState();   /* E5 / decision 31(c): the unique-NPC state map is this world's only stored "what has happened" */
    coop::InstallSpeech();     /* P3 (read-parity3 GAP 3): Dialogue::say 0x67F2F0 - speech from the driving game */
    coop::InstallCrime();      /* pvp1: no crime is recorded between players (user decision 2026-09-24) */
    coop::InstallBuild();      /* build1-a: observe construction (P082); nothing is sent */
    coop::InstallSettings();   /* settings2 S2: GameplayOptions::load 0x3EEDE0 post-hook - the world's advanced options over the save's */
    coop::InstallNames();      /* names1: Character::setName 0x5CB840 - an owned character's rename is sent */
    coop::InstallFactionNameHooks();   /* T-368: the FACTION tab refuses a faction name another player of this world holds (Kenshi's own box) */
    coop::InstallCapture();    /* P11: task bodies 0x35A9C0 / 0x34E750 + the strip / dress / owner / shave hooks */
    coop::InstallSlaves();     /* slave1: setSlaveState 0x5A3EB0 + StateBroadcastData::periodicUpdate 0x5A44C0 */
    coop::InstallHire();       /* recruit1 R0-a: Dialogue::_doActions 0x67FAD0, LOG ONLY */
    coop::InstallWorldGen();
    logHookCounts();   /* mig1: every install above went through coop::AddHook (hooks.cpp, our own MinHook) */
    LARGE_INTEGER t1;
    ::QueryPerformanceCounter(&t1);
    const long long ms = freq.QuadPart > 0 ? (long long)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart) : -1;
    /* the start runs inside Ogre's Root::initialise, with the game window up and not answering until it returns */
    DebugLog("kenshi-coop: plugin ready (" + std::string(road) + ") - address table '" + coop::AddrTableName()
             + "'; the start took " + fmtNum(ms) + " ms");
}

/* ---- RE_Kenshi's road -------------------------------------------------------------------------------------------------
   mods\Shared Wastelands\RE_Kenshi.json names this DLL, and RE_Kenshi calls startPlugin from its GameWorld::initModsList
   hook - while Kenshi's launcher is opening, before the player has pressed OK, so before the launcher has written the mod
   list. startPlugin therefore decides nothing: it registers ONE Ogre plugin object with Root::installPlugin, and Ogre calls
   that object's initialise() from Root::initialise, which the game reaches only after the launcher closed with OK. That is
   where this road asks whether this copy is switched on and starts the mod (startPluginOn). Nothing is hooked and no file
   is written before then, except the one line saying the object could not be registered. With SharedWastelandsLoader's
   road present too, both objects are initialised in the same Root::initialise and the start-once guard keeps one start. */
class SwStartPlugin : public Ogre::Plugin
{
public:
    SwStartPlugin() : m_name("Shared Wastelands (RE_Kenshi road)"), m_initialised(0) {}
    const Ogre::String& getName() const { return m_name; }
    void install() {}
    void initialise()   /* NEVER THROWS: an exception out of it would leave Ogre's Root::initialise */
    {
        if (::InterlockedExchange(&m_initialised, 1) != 0) return;
        try
        {
            startPluginOn("RE_Kenshi's road, after the launcher's OK");
        }
        catch (...)
        {
            ErrorLog("kenshi-coop: an exception left the start (RE_Kenshi's road) - the mod may be half-started; the game goes on");
        }
    }
    void shutdown() {}
    void uninstall() {}
private:
    Ogre::String m_name;
    volatile LONG m_initialised;
public:
    static volatile LONG s_registered;
};
volatile LONG SwStartPlugin::s_registered = 0;

__declspec(dllexport) void startPlugin()
{
    if (::InterlockedExchange(&SwStartPlugin::s_registered, 1) != 0) return;   /* RE_Kenshi calls it once; a second call does nothing */
    try
    {
        Ogre::Root* root = Ogre::Root::getSingletonPtr();
        if (root == 0)
        {
            ErrorLog("kenshi-coop: NOT STARTED (RE_Kenshi's road) - Ogre's Root does not exist, so this road cannot wait for the"
                     " launcher's OK; the game runs unmodded unless SharedWastelandsLoader starts the mod");
            return;
        }
        /* THE OBJECT IS MADE ONCE WITH new AND NEVER DELETED. Ogre keeps the pointer until its own teardown (Root's shutdown
           calls shutdown(), its plugin unload calls uninstall()), which can run after this DLL's static destructors: an
           object with static storage would be destroyed by then and those calls would land in a dead object. A heap object
           nothing deletes is alive for every call Ogre makes, and this DLL is never unloaded while the game runs.
           It stays installed when this road decides "not started": initialise() runs inside Ogre's walk over its plugin
           list, where uninstalling would change the list being walked, and an installed object whose remaining calls do
           nothing is harmless.
           Before the launcher's OK installPlugin only lists the object; after it, Ogre initialises the object at once. */
        root->installPlugin(new SwStartPlugin());
    }
    catch (...)
    {
        ErrorLog("kenshi-coop: NOT STARTED (RE_Kenshi's road) - registering with Ogre threw; the game runs unmodded unless"
                 " SharedWastelandsLoader starts the mod");
    }
}

/* ---- SharedWastelandsLoader's road (owner 117) ---------------------------------------------------------------------------
   SharedWastelandsLoader.dll is an Ogre plugin named in the game's Plugins_x64.cfg; after the launcher's OK (from inside
   Ogre's Root::initialise) it reads Kenshi's mod list, finds this DLL by its marker file (mod-package\Shared Wastelands\
   shared-wastelands.loader.txt, src/common/loadermarker.h) and calls coopEarlyStart, which starts the mod at once.
   The marker's start= export (C name, so GetProcAddress finds it as written). NEVER THROWS: the loader, and Ogre above it,
   must never see an exception. */
extern "C" __declspec(dllexport) void coopEarlyStart()
{
    try
    {
        startPluginOn("SharedWastelandsLoader's road, after the launcher's OK");
    }
    catch (...)
    {
        ErrorLog("kenshi-coop: an exception left the start (SharedWastelandsLoader's road) - the mod may be half-started;"
                 " the game goes on");
    }
}
