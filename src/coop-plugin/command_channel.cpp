#include "command_channel.h"
#include "../common/tradecharge.h"   /* T-164 B4-1: buytest keeper cats=<n> */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "config.h"   /* E38 / decision 43: the file is a default - `host`, `join` and `store server` override it */
#include <vector>
#include <cstdlib>

#include "coop_log.h"
#include "game/SaveManager.h"
#include "game/Character.h"
#include <ogre/OgreVector3.h>
#include "ai_spike.h"
#include "net/session.h"
#include "spawn.h"
#include "replicate.h"
#include "combat.h"
#include "effect.h"   /* T-327: EffectTick, ReportEffect, eatbite */
#include "medical.h"
#include "stats.h"   /* S1: StatsTick */
#include "resurrect.h"   /* T-556: ResurrectTick and the TEST-ONLY `resurrect` lever */
#include "fallentab.h"   /* T-556: ReportFallenTab */
#include "store.h"
#include "ownstore.h"   /* mmo1: ownsave verb, [OWNSAVE] report */
#include "playerfaction.h"
#include "tags.h"   /* tags1: `tags on|off|lift <n>` */
#include "relations.h"
#include "playerstab.h"   /* T-545: the TEST-ONLY playerstab lever, [PLAYERS] REPORT */
#include "chat.h"   /* the TEST-ONLY chat lever */
#include "peace.h"
#include "towngen.h"
#include "../common/recruitmult.h"   /* recruit3: the recruitmult values */
#include "zones.h"      // M-A
#include "handoff.h"    // M-D
#include "hire.h"       /* recruit1: hiretest, ReportHire */
#include "worldgen.h"
#include "worldstate.h"
#include "items.h"
namespace coop { std::string RangedTestLever(const std::string& args); }   /* P104: combat.cpp */
namespace coop { std::string GroundLineLever(const std::string& args); std::string GroundFindLever(const std::string& args); }   /* T-310: combat.cpp */
namespace coop { bool GroundFindLastPick(bool target, float* x, float* z, long long* id); }   /* T-310: combat.cpp, playerteleport groundpick */
#include "speech.h"   /* ReportCrimeTest */
#include "../common/saywire.h"   /* the name levers' argument shapes (koself name, talkcarriers ev/act) */
#include "build.h"    /* build1-a: buildtest, buildlist, ReportBuild, BuildTick */
#include "settings.h"   /* settings1 S1: SettingsTick, settingsdump */
#include "crime.h"    /* crime3: CrimeTick, ReportCrime */
#include "doors.h"
#include "team.h"   /* T-546 step 3: the TEST-ONLY `team` lever */
#include "worldsync.h"
#include "soak.h"
#include "appearance.h"
#include "clothing.h"
#include "ui.h"   /* uishot: the TEST-ONLY uipreview verb */
#include "../common/uidrive.h"   /* pp1: the TEST-ONLY UI driver verbs (uiclick / uipick / uitype / uistate) */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "u8file.h"   /* UTF-8 paths through the wide Windows file calls - PathNextToDll's answer is UTF-8 */

#include <fstream>
#include <sstream>
#include <locale>

namespace coop {

namespace {

// The pump runs ~900x/s (F041). Poll ~3x/s.
const long long kPollInterval = 300;

long long      g_ticks          = 0;
std::string    g_lastRaw;                   // the whole line INCLUDING its nonce (T067/F222):
                                            // the nonce is part of the write's identity, so
                                            // the duplicate test has to see it
unsigned long long g_lastWriteTime = 0;     // cheap change detection on the cmd file
int            g_deferrals       = 0;       // logged so a stuck command is diagnosable

std::string Trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    std::string::size_type b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    std::string::size_type e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

void WriteStatus(const std::string& text)
{
    std::ofstream f(U8W(PathNextToDll(swnames::kStatusFile).c_str()).c_str(), std::ios::trunc);   /* the path is UTF-8: opened through the wide name */
    if (f) f << text << std::endl;
}

// Executes `load <name>`.
// Returns true if the command is FINISHED (success or permanent failure).
// Returns false to DEFER - the caller must not consume the command; we retry next tick.
bool DoLoad(const std::string& saveName)
{
    // Live reads at the moment of commitment - never cached from a previous tick.
    SaveManager* sm = SaveManager::getSingleton();
    if (sm == 0)
    {
        if (++g_deferrals % 20 == 1)
            DebugLog("[cmd] load deferred: SaveManager::getSingleton() is null (retrying)");
        return false;
    }

    if (!sm->anySavesExist())
    {
        if (++g_deferrals % 20 == 1)
            DebugLog("[cmd] load deferred: anySavesExist() is false (retrying)");
        return false;
    }

    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[cmd] loading save '" << saveName << "' after " << g_deferrals << " deferral(s)";
    DebugLog(ss.str());

    if (HandSaveBlocked())   /* T-239 fold (review LOW 3): mid-world in a multiplayer world the load is refused (the SaveManager::load hook) - say so */
    {
        WriteStatus("error load blocked-multiplayer " + saveName);
        DebugLog("[cmd] load '" + saveName + "' not sent: hand loads are refused in a multiplayer world (T-239)");
        g_deferrals = 0;
        return true;
    }
    sm->load(saveName);

    WriteStatus("ok load " + saveName);
    DebugLog("[cmd] SaveManager::load returned for '" + saveName + "'");
    g_deferrals = 0;
    return true;
}

// save2 (user decision 2026-09-25; cloud/ANSWERS.md "save verb - answers"): `save <name>` - a test save with no one at
// the keyboard. The name is 1..48 of letters, digits, '-', '_' (T-251: no cooptest-* rule in the mod - which slots a test may
// write is the test tools' limit); anything else is refused and logged. The request goes through SaveManager::save (the Save menu's door); completion is watched by SaveWatchTick.
std::string g_saveWatchName;     // the test save being watched to completion ("" = none)
DWORD g_saveWatchAt = 0;
DWORD g_saveFirstTryAt = 0;      // review-save2: when this save command was first tried (retries stop after 60 s)
bool  g_saveTrying = false;      // F924: g_saveFirstTryAt holds a real time (the `| 1u` marker put it 1 ms in the future)
int   g_rwLoadPhase = 0;         // restore1c: `repairworld` - the notebook repaired the world; the load of the save is being retried
bool SaveNameAllowed(const std::string& n)
{
    if (n.empty() || n.size() > 48) return false;   /* T-251: no cooptest-* prefix rule */
    for (size_t i = 0; i < n.size(); ++i)
    {
        const char c = n[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
// true = finished with this command (done, refused or failed); false = retry next tick.
bool DoSave(const std::string& saveName)
{
    if (!SaveNameAllowed(saveName))
    {
        ErrorLog("[cmd] save REFUSED: '" + saveName + "' is not a slot name (1..48 of letters/digits/-/_)");
        WriteStatus("error save refused-name " + saveName);
        return true;
    }
    // F924 (T306): the start was stored as `now | 1u`, one ms AHEAD of now on an even tick, so `now - start` wrapped to ~4e9
    // on the very first call and the save gave up at once. A flag marks "started"; the time is stored as read.
    if (!g_saveTrying) { g_saveTrying = true; g_saveFirstTryAt = ::GetTickCount(); }
    if (::GetTickCount() - g_saveFirstTryAt > 60000)   // review-save2: never retry forever
    {
        ErrorLog("[cmd] save '" + saveName + "' NOT accepted within 60 s - given up");
        WriteStatus("error save not-accepted " + saveName);
        g_saveTrying = false; g_deferrals = 0;
        return true;
    }
    if (!coop::GameplayRunning() || coop::EngineWritesBlocked())   // review-save2: not during a load either
    {
        if (++g_deferrals % 20 == 1) DebugLog("[cmd] save deferred: no world is running or one is loading (retrying)");
        return false;
    }
    SaveManager* sm = SaveManager::getSingleton();
    if (sm == 0) { if (++g_deferrals % 20 == 1) DebugLog("[cmd] save deferred: SaveManager::getSingleton() is null (retrying)"); return false; }
    const int r = coop::StoreRequestTestSave(sm, saveName);
    if (r == -2) { ErrorLog("[cmd] save REFUSED: the save hook (SaveManager::save) is not installed in this build"); WriteStatus("error save no-hook " + saveName); g_saveTrying = false; return true; }
    if (r == -3) { ErrorLog("[cmd] save REFUSED: the profile save folder redirect refused it (the open save is another folder - said above)"); WriteStatus("error save refused " + saveName); g_saveTrying = false; return true; }   /* T-251 fold item 2 */
    if (r == 0) { if (++g_deferrals % 20 == 1) DebugLog("[cmd] save deferred: another save or load is pending (retrying)"); return false; }
    if (r < 0) { ErrorLog("[cmd] save FAILED: the SaveManager request field could not be read"); WriteStatus("error save unreadable " + saveName); g_saveTrying = false; return true; }
    g_saveTrying = false;
    DebugLog("[cmd] save '" + saveName + "' accepted by SaveManager::save (isAutosave=1) after " + std::string(g_deferrals > 0 ? "deferrals" : "no deferral") + " - watching for completion");
    WriteStatus("ok save requested " + saveName);
    g_saveWatchName = saveName;
    g_saveWatchAt = ::GetTickCount();
    g_deferrals = 0;
    return true;
}
// Every command-channel tick: a watched test save is reported done (or timed out after 120 s).
void SaveWatchTick()
{
    if (g_saveWatchName.empty()) return;
    SaveManager* sm = SaveManager::getSingleton();
    std::string dir;
    const int d = sm != 0 ? coop::StoreTestSaveDone(sm, g_saveWatchName, &dir) : -1;
    if (d == 1)
    {
        DebugLog("[cmd] save '" + g_saveWatchName + "' FINISHED (request pump done, SaveFileSystem idle, " + dir
                 + "\\quick.save exists) - safe to load it now");
        WriteStatus("ok save done " + g_saveWatchName);
        g_saveWatchName.clear();
    }
    else if (d == 2)
    {
        ErrorLog("[cmd] save '" + g_saveWatchName + "' FAILED: the engine finished but " + dir + "\\quick.save does not exist");
        WriteStatus("error save failed " + g_saveWatchName);
        g_saveWatchName.clear();
    }
    else if (::GetTickCount() - g_saveWatchAt > 120000)
    {
        ErrorLog("[cmd] save '" + g_saveWatchName + "' did not finish within 120 s (done=" + (d < 0 ? std::string("unreadable") : std::string("no")) + ")");
        WriteStatus("error save timeout " + g_saveWatchName);
        g_saveWatchName.clear();
    }
}

// F176: ONE report set, called by BOTH verbs, because keeping two lists in step by hand
// has now failed twice.
//
// F125 recorded the first failure - `[P014]` lived only under `netstat`, so a plan that asked
// for `report` got nothing, and T037 survived on luck. The fix then was to ADD the missing
// call to the other list. That fixed the instance and left the trap: T057 asked for `report`
// three times and got no `[M2] REPORT` line at all, because `ReportReplication`,
// `ReportSpawns`, `ReportCombat` and `SessionReport` were still `netstat`-only. **The run's
// central question - is the owner streaming, and are the puppets tracking - was unanswerable
// from a build where the answer was already being computed.**
//
// So this time the DIVERGENCE IS MADE IMPOSSIBLE rather than corrected: both verbs call this,
// and a probe added here is reachable from either. Fixing the instance and fixing the
// possibility are different repairs, and only the second one holds.
void ReportEverything()
{
    coop::net::SessionReport();
    coop::ReportSpawns();
    coop::ReportReplication();
    coop::ReportCombat();
    coop::ReportCombatMode();
    coop::ReportMedical();
    coop::ReportEffect();   /* T-327: the [EFFECT] REPORT line */
    coop::ReportSuppression();
    coop::ReportSoak();
    coop::ReportAppearanceCounters();
    coop::ReportClothingCounters();
    coop::ReportZones();
    coop::ReportHandoff();
    coop::ReportHire();   /* recruit1 */
    coop::ReportStore();
    coop::ReportPlayerFaction();
    coop::ReportTownGen();
    coop::ReportDoors();   /* E45 */
    coop::ReportRelations();
    coop::ReportPlayersTab();   /* T-545 */
    coop::ReportFallenTab();    /* T-556: the FALLEN tab */
    coop::ReportPeace();   /* E42: folded in here rather than behind a verb of its own - F125/F177 */
    coop::ReportCrimeTest();   /* crime11: the lever's counters (P077 removed) */
    coop::ReportCrime();   /* crime3 */
    coop::ReportBuild();   /* build1-a */
    coop::ReportParity();  /* par1: [PARITY] REPORT and [ITEMS] REPORT */
    coop::ReportGround();  /* inv5 phase 1: [GROUND] REPORT */

    // P025/F200 - the world population, not just our own spawns. Added HERE rather than behind
    // a new verb for the reason this function exists at all: a probe reachable from only one
    // command is a probe that will be missing from the run that needed it (F125, F177). Every
    // existing test plan already calls `report`, so every future run carries the census.
    coop::WorldCensus();

    // P032. Unlike the roster this IS folded into `report` (F125/F177): it is a single line, and
    // its whole value is as a TIME SERIES - "how many world characters had been created by now" is
    // meaningless from one reading, and a probe you have to remember to sample at the right moment
    // is a probe that will be missing from the run that needed it.
    coop::ReportWorldGen();
    coop::ReportWorldSync();
}

} // namespace

std::string PathNextToDll(const char* filename)
{
    /* The DLL's own file name through the wide call, handed back as UTF-8: an install folder holding letters outside the ANSI
       code page is named exactly, and every caller opens the result through the u8file.h calls. The buffer is on the stack
       (this runs on every command poll) and holds four times MAX_PATH; an answer of 0 (failed) or the whole buffer (cut short)
       gives `filename` alone, the answer
       this always gave when the DLL's file could not be named. */
    wchar_t path[MAX_PATH * 4];
    HMODULE self = 0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&PathNextToDll, &self);
    const DWORD cap = (DWORD)(sizeof(path) / sizeof(path[0]));
    const DWORD n = GetModuleFileNameW(self, path, cap);
    if (n == 0 || n >= cap) return std::string(filename);
    path[n] = 0;
    std::string s = U8FromW(path);
    std::string::size_type slash = s.find_last_of("\\/");
    if (slash != std::string::npos) s.erase(slash + 1);
    return s + filename;
}

void* OwnSaveManagerPtr() { return SaveManager::getSingleton(); }   /* mmo4: the leave save's door, the same singleton DoSave uses */

long long g_cmdVerbsRun = 0;   /* mmo5: command-channel verbs run in this process (ownstore.h) */

/* pp1b: the status file, written from outside this file (ui.cpp UiDriveEngineClickFlush - an engine click refused at the end of
   the frame, after `ok uiclick` was already written; the quitmenu shape). WriteStatus itself stays file-local. */
void CommandStatusLate(const std::string& text)
{
    WriteStatus(text);
}

/* P26 T718 (TEST-ONLY): a pending `playerteleport near` / `talksight` wait re-checks every 500 ms (speech.cpp TalkWaitTick) - here,
   the same thread and place the commands run. A near wait that passed moves the player exactly as the command does. */
static void TalkWaitStep()
{
    float wx = 0, wz = 0; unsigned long wms = 0;
    if (coop::TalkWaitTick(&wx, &wz, &wms) != 1) return;
    const bool tok = coop::PlayerTeleportAbs(wx, wz);
    std::ostringstream wo;
    wo << "[TALKSIGHT] playerteleport near: DONE after " << wms << " ms of waiting" << (tok ? "" : " - but the teleport FAILED");
    DebugLog(wo.str());
}

void CommandChannelTick()
{
    // F322. THIS function IS the main thread, by definition - everything below it is documented
    // main-thread-only. Recording the id here rather than asserting it elsewhere means the
    // `GameWorld::destroy` detour can CHECK which thread it is on before sending, instead of
    // trusting F321's one-run measurement that destruction is always main-thread.
    /* P7v (design-noworld-queue 3.2) - FIRST STATEMENT OF THE FRAME. Everything below this line, the
       session pump included, now reads a CURRENT "is a world running", not last frame's answer. */
    coop::SoakRefreshRunning();
    coop::NoteMainThread();
    coop::SpawnPaceFrameStart();   /* the copy budget is per frame: both drains below share it */

    // Network servicing runs EVERY tick, not on the command throttle: a 300-tick delay
    // on packet handling would add latency to every replicated event. Main thread only,
    // per the threading rules (spec section 5).
    coop::net::SessionTick();
    /* P7v (design-noworld-queue 4.1) - THE ONE DRAIN, FIRST CALL. SessionTick now POLLS AND ENQUEUES
       and dispatches only the handshake/plugin-state table; this is where what it just enqueued is
       applied, before ReplicateTick drives the puppets - which is the ordering HEAD has and which a
       single call from inside StoreTick would have cost a frame. */
    coop::InQueueDrain();
    coop::net::SessionCatchupApplyTick();   /* M7a2 fold 1 [m7a2f-ap4]: the catch-up's engine writes (sweep timeout, reverse catch-up) - right after the drain's applies */
    coop::ReplicateTick();   // drives puppets every frame; sends on its own cadence
    coop::P071Tick();        // PROBE P071: last frame's placements, read one frame later
    coop::P072Tick();        /* PROBE P072: drains the building layout ring */
    coop::CombatTick();      // drains hits the AI worker thread queued (F095)
    coop::EffectTick();      /* T-327: the eater's MSG_EFFECT sends and stop rules, the owner's pairs, the queued answers */
    coop::CombatModeTick();  // P-15/F220: sends combat-mode EDGES for characters we own
    // M5: the transition watch has to run EVERY frame or it cannot see a state that lives
    // for less time than it takes to ask a question (T041's KO lasted <=2 s). The periodic
    // digest throttles itself inside.
    coop::SoakTick();
    coop::ZonesTick();       // M-A: loaded sectors (1 Hz), client -> host, host owner map
    coop::HandoffTick();     // M-D: squad handoffs (1 Hz)
    coop::SnapshotTick();    /* snap1 (user decision 2026-09-26): an armed snapshot is taken at its world minute; its block is written 64 lines a tick */
    TalkWaitStep();          /* P26 T718 (TEST): a pending playerteleport near / talksight wait for a character to load, every 500 ms */
    /* E24.3 (review-p5o HIGH-3): THE DRAIN RUNS BEFORE THE LINK PUMP. StoreTick pumps the notebook link, and an
       inbound UNIQUE_STATE is APPLIED inside that pump - it writes the engine's state table. With the drain after
       it, a change a worker thread pushed in the previous frame was read AFTER the apply had already overwritten
       the map, so this game's own observation was silently replaced by the notebook's older answer and never
       published. Reading first costs nothing and removes the window entirely. */
    coop::WorldStateTick();    /* E5: drains the unique-NPC state changes the writers' detours queued */
    coop::StoreTick();       // P1 persistence (1 Hz inside)
    coop::RelationsTick();   // P3 piece 2 (link-up snapshot; live changes; the queue drain)
    coop::PlayerFactionTick(); // a rename of my faction is a state change seen here (review-p3r H1)
    coop::TownGenTick();       // P4c: the main thread id, captured here
    coop::WorldGenTick();      /* P5c: the session flags the leaf gate reads off-thread */
    coop::ItemsTick();         /* E22a: drains the item moves the transfer detours queued, applies the ownership test and sends */
    coop::DoorsTick();       /* E45: publishes the door changes the detours recorded and applies the holder's */
    coop::BuildTick();       /* build1-a: logs the construction events the detours queued (P082) */
    coop::SettingsTick();    /* settings1 S1: [SETTINGS]/[MODS] at the link-up and world-live edges (look only) */
    coop::TeamTick();        /* T-546: held RESTORE rows answered once the world is loaded; the table asked for after a world teardown */
    coop::CaptureTestTick();   /* P11 (test-only lever): nothing unless `capturetest` armed it */
    coop::WorldSyncTick();   // P033: adopts world characters, throttled; no-op when off

    coop::SpawnDeathRetryTick();   /* T-303: a copy whose SPAWN said dead and that did not die on arrival - retried every ~0.25 s */

    // M4 step 3 (F126): periodic authoritative state, ~1 Hz, one owned character per tick.
    // Throttled here rather than inside StateTick so the cadence is visible at the call
    // site. Rides the same main-thread pump as everything else that touches the transport.
    static long long s_stateTicks = 0;
    if (++s_stateTicks % 120 == 0)
    {
        coop::RestWatchTick();   // R3 (read-ragdoll): an owned body whose ragdoll settled (or moved > 20) -> STATE at once
        coop::LimbOwnWatchTick();   // an owned character's limb lost or robotic limb fitted -> STATE at once
        coop::StateTick();
    }
    /* S1 (read-stats): the owner's stat values - changed ones, and each character every ~30 s; at most
       kStatsMaxPerTick (16) MSG_STATS per call, 128 owned characters read per call. */
    if (s_stateTicks % 30 == 0) coop::StatsTick();
    coop::ResurrectTick();   /* T-556: this game's own dead are snapshotted here; a bring-back's look is finished here */
    /* crime3: the owner's crime sampler - a crime lives ~20 game s, so each owned character is read every few ticks
       (128 per call, at most 16 MSG_CRIME a call). */
    if (s_stateTicks % 15 == 7) coop::CrimeTick();

    if (++g_ticks % kPollInterval != 0)
        return;

    const std::string cmdPath = PathNextToDll(swnames::kCmdFile);

    // Cheap change detection: only read the file when its write time moves.
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!U8GetFileAttributesEx(cmdPath.c_str(), GetFileExInfoStandard, &fad))
        return;   // no command file - nothing to do

    unsigned long long writeTime =
        ((unsigned long long)fad.ftLastWriteTime.dwHighDateTime << 32) |
        fad.ftLastWriteTime.dwLowDateTime;

    static std::string s_pending;   // command awaiting execution (may span ticks)

    if (writeTime != g_lastWriteTime)
    {
        g_lastWriteTime = writeTime;
        std::ifstream f(U8W(cmdPath.c_str()).c_str());   /* the path is UTF-8: opened through the wide name */
        std::string line;
        if (f && std::getline(f, line))
        {
            std::string cmd = Trim(line);

            // F210 - OPTIONAL NONCE PREFIX: "<anything>|<command>". Everything up to the first
            // '|' is discarded.
            //
            // Why this exists: the latch below refuses to re-run a command whose text is
            // identical to the last one, which is right - the file is re-read whenever its
            // timestamp moves, and a command must not fire twice for one write. But it also
            // means an executor sending `report` TWICE IN A ROW gets silence the second time,
            // and T064 lost two minutes to exactly that before the executor diagnosed it.
            // Every test plan in this project sends `report` repeatedly.
            //
            // The harness now prefixes a unique token, so two identical commands are two
            // different lines and the duplicate case cannot arise. Fixing the possibility
            // rather than asking every future plan to remember to vary its text (lesson 11).
            // T067 CAUGHT MY OWN FIX NOT WORKING. The nonce was stripped BEFORE the
            // duplicate test, so the test still compared the bare command and two `report`s
            // in a row were still ignored - the workaround only ever appeared to work because
            // a different command happened to sit between them. It cost T067 180 seconds.
            //
            // The whole point of a nonce is that it is part of the identity of the write. So
            // the duplicate test now compares the RAW LINE, and the stripped text is used only
            // to decide what to execute.
            const std::string raw = cmd;

            std::string::size_type bar = cmd.find('|');
            if (bar != std::string::npos) cmd = Trim(cmd.substr(bar + 1));

            if (!cmd.empty() && raw != g_lastRaw)
            {
                s_pending  = cmd;
                g_lastRaw  = raw;
                g_deferrals = 0;
                g_saveTrying = false;   // F924: a new command starts its own 60 s
                DebugLog("[cmd] new command: " + cmd);
            }
            else if (!cmd.empty())
            {
                // Say so rather than going quiet. A silent no-op is what cost T064 two minutes;
                // a line saying "I ignored this and why" costs nothing.
                DebugLog("[cmd] ignored duplicate of last RAW line: " + raw
                         + " (the harness must write a UNIQUE nonce before the bar)");
            }
        }
    }

    SaveWatchTick();   /* save2: a requested test save reported done */
    if (s_pending.empty())
        return;

    // Parse. Unknown commands fail permanently rather than retrying forever.
    std::istringstream is(s_pending);
    std::string verb;
    is >> verb;
    /* pp1 (player-path-plan section 0 item 6): the UI driver verbs do what a player's mouse does, so they do not mark this game
       as harness-driven - a player-path run must run the player's own host-left path. */
    if (coopui::CmdMarksHarness(verb) != 0) ++g_cmdVerbsRun;   /* mmo5: this game is harness-driven - the host-left fallback never exits it on its own; pp1b: nor do the read-only observation verbs (uidrive.h CmdIsObservationVerb) */

    bool finished = false;
    bool cmdChainB = false;   /* batch2: the verb chain is split in two halves - MSVC caps one if / else-if chain (C1061, blocks nested too deeply) */
    if (verb == "load")
    {
        /* PP5: a profile save folder is "<World> - Multiplayer - <Profile>" - it has spaces, so the whole rest of the
           line is the name (trimmed), not its first word. */
        std::string name;
        std::getline(is, name);
        name = Trim(name);
        if (name.empty())
        {
            DebugLog("[cmd] malformed: 'load' requires a save name");
            WriteStatus("error load missing-name");
            finished = true;
        }
        else
        {
            finished = DoLoad(name);
        }
    }
    else if (verb == "ownsave")   /* mmo1 TEST verb: ownsave <store name> | on | off | poke | flush (T-251: a player's own store opens by itself) */
    {
        std::string arg, st;
        is >> arg;
        {   /* mmo3: `ownsave poke money <n>` - any further words join the argument, one space apart */
            std::string more;
            while (is >> more) arg += " " + more;
        }
        coop::OwnSaveCommand(arg, &st);
        WriteStatus(st);
        finished = true;
    }
    else if (verb == "repairworld")   /* restore1c TEST verb: the host's "Repair a broken world" without the click */
    {
        std::string name;
        is >> name;
        if (name.empty() || !SaveNameAllowed(name))
        {
            ErrorLog("[cmd] repairworld REFUSED: '" + name + "' is not a save name (1..48 of letters/digits/-/_)");
            WriteStatus("error repairworld refused-name " + name); finished = true; g_rwLoadPhase = 0;
        }
        else if (g_rwLoadPhase == 1) { finished = DoLoad(name); if (finished) g_rwLoadPhase = 0; }
        else
        {
            std::string st;
            const int rc = coop::RepairWorldCommand(name, &st);
            if (rc == 2) { g_rwLoadPhase = 1; finished = DoLoad(name); if (finished) g_rwLoadPhase = 0; }
            else if (rc == 1) { WriteStatus(st); finished = true; }
        }
    }
    else if (verb == "save")   /* save2 */
    {
        std::string name;
        is >> name;
        if (name.empty()) { DebugLog("[cmd] malformed: 'save' requires a slot name"); WriteStatus("error save missing-name"); finished = true; }
        else finished = DoSave(name);
    }
    else if (verb == "handkey")   /* owner 204 TEST-ONLY: handkey save|load - what F5 / F9 do (F010: the harness cannot press a key) */
    {
        std::string which;
        is >> which;
        if (which != "save" && which != "load") { DebugLog("[cmd] malformed: 'handkey' requires save or load"); WriteStatus("error handkey bad-arg " + which); }
        else if (!coop::GameplayRunning()) { DebugLog("[cmd] handkey " + which + " not sent: no world is running (the keys act in a world)"); WriteStatus("error handkey no-world " + which); }
        else
        {
            SaveManager* sm = SaveManager::getSingleton();
            const int r = coop::StoreHandKey(sm, which == "load" ? 1 : 0);
            if (r == 1)
            {
                DebugLog("[cmd] handkey " + which + ": the engine's SaveManager::" + which + "(\"quicksave\"" + (which == "save" ? std::string(", 0") : std::string()) + ") was called through its hook, as the key calls it");
                WriteStatus("ok handkey " + which);
            }
            else if (r == -2) { ErrorLog("[cmd] handkey " + which + " REFUSED: the SaveManager::" + which + " hook is not installed in this build - not called"); WriteStatus("error handkey no-hook " + which); }
            else { ErrorLog("[cmd] handkey " + which + " FAILED: SaveManager::getSingleton() is null"); WriteStatus("error handkey no-savemanager " + which); }
        }
        finished = true;
    }
    else if (verb == "suppress")
    {
        coop::ArmSuppression();
        WriteStatus("ok suppress armed");
        finished = true;
    }
    else if (verb == "watch")
    {
        coop::ArmWatch();
        WriteStatus("ok watch armed");
        finished = true;
    }
    else if (verb == "watchplayer")
    {
        // AUD 2026-09-17: a MANUAL re-capture. The mod arms its own watch whenever the configured role is
        // host or client and no player load centre is held, so a session no longer needs this verb; it is
        // kept for a single-role game and for forcing a fresh capture by hand.
        coop::ArmWatchPlayer();
        WriteStatus("ok watchplayer armed");
        finished = true;
    }
    else if (verb == "on")
    {
        coop::SetSuppress(true);
        WriteStatus("ok decision-suppression on");
        finished = true;
    }
    else if (verb == "off")
    {
        coop::SetSuppress(false);
        WriteStatus("ok decision-suppression off");
        finished = true;
    }
    else if (verb == "freeze")
    {
        coop::SetFreeze(true);
        WriteStatus("ok locomotion-freeze on");
        finished = true;
    }
    else if (verb == "unfreeze")
    {
        coop::SetFreeze(false);
        WriteStatus("ok locomotion-freeze off");
        finished = true;
    }
    else if (verb == "move")
    {
        double dx = 0, dz = 0;
        is >> dx >> dz;
        bool ok = coop::MoveTarget((float)dx, (float)dz);
        WriteStatus(ok ? "ok move" : "error move");
        finished = true;
    }
    else if (verb == "moveorder")
    {
        // moveorder <dx> <dz>   TEST-ONLY. The captured player character (the one `move` walks) gets the game's own
        // 'Move order' task (type 29) at its position + (dx, dz): the entry a player's click-to-move leaves in a character's
        // order list, put there through the engine's order-injection point (InjectOrder -> OrdersReceiver::issueOrder with
        // the old orders cleared, no shift-queue and no subject). `move` only writes a walk target, so the character's task
        // stays 'Aimless' and nothing that keys on the owner's Move order (the other game ending its copy's path at the
        // order's goal) ever sees one; this verb gives that code a real order to work from.
        // moveorder <dx> <dz> squad   TEST-ONLY. The same order for the captured character AND every other living member of
        // its squad (the squad reader ReadSquadOf: Character+0x658 -> the ActivePlatoon's member array), each to its OWN
        // position + (dx, dz), as a box-selected group's click-to-move gives each selected character its own Move order.
        // One `[cmd] moveorder uid=` line per character, then `[cmd] moveorder squad members= ordered=`.
        const int kMoveOrderTask = 29;   // 'Move order' (.modding/03-systems/taskdata-table.md)
        double dx = 0, dz = 0;
        is >> dx >> dz;
        std::string mode;
        is >> mode;
        const bool squad = (mode == "squad");
        ::Character* c = coop::GetTarget();
        std::vector< ::Character*> who;
        int squadCount = -1, dead = 0, deadUnread = 0;
        if (c == 0) DebugLog("[cmd] moveorder: no target captured");
        else
        {
            who.push_back(c);
            if (squad)
            {
                coop::SquadView sv;
                if (coop::ReadSquadOf(c, &sv) == 1)
                {
                    squadCount = sv.count;
                    for (int i = 0; i < sv.stored; ++i)
                    {
                        ::Character* m = sv.members[i];
                        if (m == 0 || m == c) continue;
                        const int d = coop::StoreIsDeadPod(m);
                        if (d == 1) { ++dead; continue; }
                        if (d != 0) { ++deadUnread; continue; }   // unreadable: not ordered
                        who.push_back(m);
                    }
                }
            }
        }
        int ordered = 0;
        for (size_t i = 0; i < who.size(); ++i)
        {
            ::Character* m = who[i];
            Ogre::Vector3 p = m->worldPosition();
            Ogre::Vector3 goal(p.x + (float)dx, p.y, p.z + (float)dz);
            const unsigned int uid = coop::FindSpawnedUid(m);
            const bool ok = coop::InjectOrder(m, kMoveOrderTask, 0, goal);   // a gated puppet also gets its one decision pass; an owned character adopts it on its own next pass
            if (ok) ++ordered;
            std::stringstream ss;
            ss.imbue(std::locale::classic());
            ss.setf(std::ios::fixed); ss.precision(1);
            ss << "[cmd] moveorder uid=" << uid << " to=" << goal.x << "," << goal.z << " task=" << kMoveOrderTask << " ok=" << (ok ? 1 : 0);
            DebugLog(ss.str());
        }
        if (c == 0 && !squad) DebugLog("[cmd] moveorder uid=0 to=0.0,0.0 task=29 ok=0");
        if (squad)
        {
            std::stringstream ss;
            ss << "[cmd] moveorder squad members=" << who.size() << " ordered=" << ordered << " squadCount=" << squadCount
               << " dead=" << dead << " deadUnread=" << deadUnread;
            DebugLog(ss.str());
        }
        const bool ok = !who.empty() && ordered == (int)who.size();
        WriteStatus(ok ? "ok moveorder" : "error moveorder");
        finished = true;
    }
    else if (verb == "drive")
    {
        double dx = 0, dz = 0; int frames = 600;
        is >> dx >> dz;
        if (!(is >> frames)) frames = 600;
        bool ok = coop::DriveTarget((float)dx, (float)dz, frames);
        WriteStatus(ok ? "ok drive" : "error drive");
        finished = true;
    }
    else if (verb == "report")
    {
        ReportEverything();
        coop::OwnSaveReport();   /* mmo1 */
        WriteStatus("ok report");
        finished = true;
    }
    else if (verb == "relation")   /* P079 (rel2): read-only - one faction's standing with this game's player and with the peer */
    {
        std::string sid, sid2;
        is >> sid;
        std::string kind, rest;
        if (sid == "change") is >> kind;   /* TEST-ONLY `relation change crime|war|peace <faction>`; `relation of <faction>` - a name may hold spaces */
        if (sid == "change" || sid == "of")
        {
            std::getline(is, rest);
            while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.erase(0, 1);
            while (!rest.empty() && (rest[rest.size() - 1] == ' ' || rest[rest.size() - 1] == '\r' || rest[rest.size() - 1] == '\n')) rest.erase(rest.size() - 1);
        }
        else is >> sid2;   /* ally1: a second token makes it the pair readout `relation <a> <b>` (@me, @peer or a stringID) */
        if (sid == "change") WriteStatus(coop::RelationChangeLever(kind, rest));
        else if (sid == "of") WriteStatus(coop::RelationOfProbe(rest));
        else if (sid.empty()) { DebugLog("[cmd] malformed: 'relation' requires a faction stringID"); WriteStatus("error relation missing-sid"); }
        else if (!sid2.empty()) WriteStatus(coop::RelationPairProbe(sid, sid2));
        else WriteStatus(coop::RelationProbe(sid));
        finished = true;
    }
    else if (verb == "ally")   /* ally1 (T343): ally on|off - MY "mine -> stand-in" relation through the engine's setRelation; the relations forward carries it */
    {
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error ally usage"); }
        else WriteStatus(coop::AllySet(mode == "on"));
        finished = true;
    }
    else if (verb == "team")   /* T-546 step 3 TEST-ONLY: team invite <slot> | accept | decline | leave | remove <slot> | disband | show - team.cpp TeamCommand; [TEAM] lines */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::TeamCommand(rest));
        finished = true;
    }
    else if (verb == "worldrel")   /* par24 TEST-ONLY: worldrel [set <a> <b> <value> | test | get <a> <b>] - one world-vs-world standing moved on THIS game (it travels via the notebook) */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::WorldRelCommand(rest));
        finished = true;
    }
    else if (verb == "livemove")   /* M5b proof: livemove <n> - this game's next n MOVEs go THROUGH THE NOTEBOOK (LIVE WORLD) instead of the session link; 0 stops */
    {
        long long n = -1; is >> n;
        if (n < 0 || n > 600) { DebugLog("[cmd] malformed: 'livemove' requires a count 0..600"); WriteStatus("error livemove usage"); }
        else { coop::net::SessionMoveViaLive((int)n); WriteStatus("ok livemove"); }
        finished = true;
    }
    else if (verb == "liveprobe")   /* M6 TEST-ONLY: one LIVE AREA probe for this game's player sector - only the games with that sector loaded or one ring from it receive it */
    {
        WriteStatus(coop::StoreLiveProbe());
        finished = true;
    }
    else if (verb == "playerstab")   /* T-545 TEST-ONLY: playerstab [faction | open | select <slot> | stance ally|neutral|hostile | confirm | cancel] - the PLAYERS tab's own controls, fired as a click */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::PlayersTabCommand(rest));
        finished = true;
    }
    else if (verb == "relate")   /* relate1: relate <target> ally|neutral|hostile - towards another player's stand-in only; `ally on|off` is the alias */
    {
        std::string target, level; is >> target >> level;
        if (target.empty() || level.empty()) { DebugLog("[cmd] malformed: 'relate' requires <target> ally|neutral|hostile"); WriteStatus("error relate usage"); }
        else WriteStatus(coop::RelateSet(target, level));
        finished = true;
    }
    // P031. Deliberately its OWN verb rather than part of `report`, and the trade-off is
    // recorded because it cuts against F125/F177 ("a probe reachable from one command is a
    // probe that will be missing from the run that needed it"). The roster emits one line per
    // character - about 130 - so folding it into `report` would multiply every log in the
    // project by roughly six, including runs that have nothing to do with world population, and
    // an unreadable log is its own way of losing evidence. Any test plan touching P-16 must
    // call `roster` at the same points it calls `report`.
    // P032. `worldgen off` makes the three gated entry points refuse. It does NOT remove characters
    // that already exist, and the log line says so - a command whose name suggests more than it
    // does is how a reader concludes the mechanism failed when it was never asked to do that.
    // AUD 2026-09-17: LEAF is the default on every game (decision 48), so this verb is only needed for a
    // control run - `worldgen on` for the ungated arm, `worldgen off` for all four hooks.
    else if (verb == "worldgen")
    {
        // Validated STRICTLY, as every on|off toggle here now is (AUD 2026-09-17 made the last four strict).
        // This one suppresses world generation: a typo silently meaning "on" would leave a run
        // measuring the ungated arm while its report claimed the gate was active.
        std::string mode; is >> mode;
        if (mode == "on" || mode == "off" || mode == "leaf")
        {
            coop::SetWorldGenMode(mode == "on" ? 1 : (mode == "leaf" ? 2 : 0));
            WriteStatus("ok worldgen " + mode);
        }
        else
        {
            ErrorLog("[P032] worldgen: expected 'on', 'off' or 'leaf', got '" + mode
                     + "' - REFUSED, the switch is unchanged");
            WriteStatus("error worldgen needs on|off|leaf");
        }
        finished = true;
    }
    // P033. `worldsync once <n>` is the lever a run should use first: it measures 10 characters
    // before risking 130. `off` stops FURTHER adoption and deliberately does not undo any - the
    // log line says so, because a command that sounds like a rollback and is not is how a reader
    // concludes a mechanism failed when it was never asked to reverse anything.
    else if (verb == "worldsync")
    {
        std::string mode; is >> mode;
        int budget = 0;
        if (mode == "once") { if (!(is >> budget) || budget <= 0) budget = 0; }
        if (mode == "on" || mode == "off" || (mode == "once" && budget > 0))
        {
            coop::SetWorldSync(mode == "on" ? 1 : (mode == "once" ? 2 : 0), budget);
            WriteStatus("ok worldsync " + mode);
        }
        else
        {
            ErrorLog("[P033] worldsync: expected 'on', 'off' or 'once <n>' with n>0, got '"
                     + mode + "' - REFUSED, the switch is unchanged");
            WriteStatus("error worldsync needs on|off|once <n>");
        }
        finished = true;
    }
    // P033. `worldradius <units>` - 0 means unbounded. Its own verb rather than an argument to
    // `worldsync` so a run can change it between sweeps without restarting adoption.
    else if (verb == "worldradius")
    {
        double r = -1.0;
        if (is >> r && r >= 0.0)
        {
            coop::SetWorldRadius((float)r);
            WriteStatus("ok worldradius");
        }
        else
        {
            ErrorLog("[P033] worldradius: expected a number >= 0 (0 = unbounded) - REFUSED,"
                     " the radius is unchanged");
            WriteStatus("error worldradius needs a number >= 0");
        }
        finished = true;
    }
    // F305. `drivespeed <n>` - the MAGNITUDE of a puppet drive. -2 IS THE DEFAULT since AUD 2026-09-17:
    // each puppet is driven at the authority's own STREAMED desiredSpeed (-3 = its streamed velocity).
    // `drivespeed 0` is the control - it leaves the engine's own `desiredSpeed` alone; -1 = each puppet
    // uses its own speedCap; >0 = force that value.
    // Negative values are legal here, unlike `worldradius`, because they are real modes.
    else if (verb == "catchup2")   /* walk1: catchup2 on|off - the proportional, signed catch-up (default on) */
    {
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") WriteStatus("error catchup2 usage: catchup2 on|off");
        else { coop::SetCatchup2(mode == "on"); WriteStatus("ok catchup2 " + mode); }
        finished = true;
    }
    else if (verb == "runboost")   /* T-189 runboost (H051): runboost on|off - TEST-ONLY A/B lever; default on */
    {
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") WriteStatus("error runboost usage: runboost on|off");
        else { coop::SetRunBoost(mode == "on"); WriteStatus("ok runboost " + mode); }
        finished = true;
    }
    else if (verb == "drivespeed")
    {
        double v = 0.0;
        if (is >> v)
        {
            coop::SetDriveSpeed((float)v);
            WriteStatus("ok drivespeed");
        }
        else
        {
            ErrorLog("[M2] drivespeed: expected a number (0 = do not touch desiredSpeed,"
                     " -1 = use each puppet's own maxSpeed, >0 = force) - REFUSED,"
                     " the drive speed is unchanged");
            WriteStatus("error drivespeed needs a number");
        }
        finished = true;
    }
    // P037 / F315. `catchup on|off` - place a puppet that cannot walk to its target.
    else if (verb == "catchup")
    {
        std::string mode;
        if ((is >> mode) && (mode == "on" || mode == "off"))
        {
            coop::SetCatchup(mode == "on");
            WriteStatus("ok catchup " + mode);
        }
        else
        {
            ErrorLog("[M2] catchup: expected 'on' or 'off', got '" + mode
                     + "' - REFUSED, the switch is unchanged");
            WriteStatus("error catchup needs on|off");
        }
        finished = true;
    }
    // T-573 TEST-ONLY. `giveup <uid>` (0 = every copy) - mark copies given up as three failed placements would, so a run
    // can watch a given-up copy wait and be tried again ([M2] catch-up RE-ARMED).
    else if (verb == "giveup")
    {
        unsigned int uid = 0;
        if (is >> uid)
        {
            const int n = coop::ForceCatchupGiveUp(uid);
            WriteStatus(n > 0 ? "ok giveup" : "error giveup: no copy to mark (unknown uid, or already given up)");
        }
        else
        {
            ErrorLog("[M2] giveup: expected a uid (0 = every copy) - REFUSED, nothing is marked");
            WriteStatus("error giveup needs a uid (0 = every copy)");
        }
        finished = true;
    }
    else if (verb == "roster")
    {
        /* P8a: `roster` is unchanged.  `roster full [<page>]` is the per-character listing build/read-roster-t236.md
           section 4c asks for - uid, mine, puppet, squad, squadType, town, building, sector, spawnCause and the
           adoption gate - paged at 64 rows like boxlist, with `page=N of N` as the stop signal.  The argument is
           taken off the WHOLE rest of the line for the same reason boxlist's now is (F627). */
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        if (rest.empty()) { coop::WorldRoster(); WriteStatus("ok roster"); }
        else
        {
            std::string mode(rest), pg;
            const size_t sp = rest.find(' ');
            if (sp != std::string::npos) { mode = rest.substr(0, sp); pg = Trim(rest.substr(sp + 1)); }
            if (mode != "full") WriteStatus("error roster: usage roster | roster full [<page>]");
            else
            {
                int page = 1;
                if (!pg.empty())
                {
                    bool digits = true;
                    for (size_t i = 0; i < pg.size(); ++i) if (pg[i] < '0' || pg[i] > '9') digits = false;
                    page = digits ? atoi(pg.c_str()) : -1;
                    if (page < 1 || page > 4096) page = -1;
                }
                if (page < 0) WriteStatus("error roster full: <page> must be 1..4096");
                else { coop::WorldRosterFull(page); WriteStatus("ok roster full"); }
            }
        }
        finished = true;
    }
    // --- M0 networking. The backend is ALWAYS named explicitly by the player;
    // there is no default and no fallback (ratified amendment 2026-08-05).
    else if (verb == "host")
    {
        std::string backend; int port = 0;
        is >> backend >> port;
        if (backend.empty() || port <= 0 || port > 65535)
        {
            DebugLog("[cmd] usage: host <direct|steam> <port>");
            WriteStatus("error host usage");
        }
        else
        {
            bool ok = coop::net::SessionHost(backend, (unsigned short)port);
            /* DECISION 48 (P8a): there is nothing to clear.  Hosting a session says nothing about who may generate
               a town - the area map does - so the town-generation client declaration and its five call sites are gone. */
            if (ok) coop::ConfigSetRole(coop::kRoleHost, "the `host` verb");   /* E38: the file is a default, not a lock (decision 43) */
            WriteStatus(ok ? "ok host" : "error host");
        }
        finished = true;
    }
    else if (verb == "join")
    {
        std::string backend, addr; int port = 0;
        is >> backend >> addr >> port;
        if (backend.empty() || addr.empty() || port <= 0 || port > 65535)
        {
            DebugLog("[cmd] usage: join <direct|steam> <address> <port>");
            WriteStatus("error join usage");
        }
        else
        {
            /* the world server is every game's one door: `join` rings it at <address> <port> (the backend word is read, not used). The
               role first: the HELLO names the world road, and leaving role=single makes the store read its record index. */
            coop::ConfigSetRole(coop::kRoleClient, "the `join` verb");
            const bool ok = coop::SetStoreServer(addr, (unsigned short)port);
            WriteStatus(ok ? "ok join" : "error join");
        }
        finished = true;
    }
    else if (verb == "worldaway")   /* T-546 step 8 TEST verb: worldaway <seconds 5-600> - this game's world-server link closed here and the
                                       re-dial held that long, then dialled again as after any outage, its world kept ([STORE] worldaway) */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::StoreWorldAwayLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "leave")
    {
        /* mmo4 (e47-mmo-design 4.1): in a co-op world with the profile's own slot known, the writer is drained and that
           slot is saved FIRST; the store tick leaves the session at the save's completion edge (or its bounded give-up).
           Otherwise exactly as before. */
        const int leaveSaving = coop::OwnLeaveSaveForLeave();
        if (leaveSaving != 1) { coop::net::SessionSendClosing("leave"); coop::net::SessionLeave(); }   /* mmo5: a host announces SESSION_CLOSING first */
        /* DECISION 48 (P8a): the sticky declaration this line existed to clear no longer exists.  A game that has
           left is unlinked, and the pre-link arm answers that identically on both games. */
        WriteStatus(leaveSaving == 1 ? "ok leave saving-first" : "ok leave");   /* mmo4 */
        finished = true;
    }
    else if (verb == "ping")
    {
        bool ok = coop::net::SessionPing();
        WriteStatus(ok ? "ok ping" : "error ping");
        finished = true;
    }
    else if (verb == "netstat")
    {
        ReportEverything();
        WriteStatus("ok netstat");
        finished = true;
    }
    else if (verb == "medgate")
    {
        // medgate <0|1>  - H012: gate the medical tick for characters we do not own.
        int on = 0;
        if (!(is >> on)) on = 0;
        coop::SetMedicalGate(on != 0);
        WriteStatus("ok medgate");
        finished = true;
    }
    else if (verb == "hungerset")   /* par5 (parity P5) TEST verb: hungerset <uid> <value> - an OWNED character only */
    {
        unsigned int uid = 0; float v = 0.0f;
        if (!(is >> uid >> v)) WriteStatus("error hungerset: usage hungerset <uid> <value>");
        else WriteStatus(coop::HungerSetOwned(uid, v));
        finished = true;
    }
    else if (verb == "rangedtest")   /* P104 TEST verb: rangedtest <shooterUid> <targetUid> [crossbowSid [boltSid [n]]] | report | health <uid> */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::RangedTestLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "ffshow")   /* T-546 (owner 512) verb, read only: ffshow <uid1> <uid2> - how two characters stand towards each other ([FF] line) */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::FfShowLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "groundline")   /* T-310 TEST verb, read only: groundline <x1> <z1> <x2> <z2> [tolU] | uid <uidFrom> <uidTo> [tolU] | report */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::GroundLineLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "groundfind")   /* T-310 TEST verb, read only: groundfind <x> <z> <dist> <radius> [tolU] | uid <uid> <dist> <radius> [tolU] */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::GroundFindLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "resurrect")   /* T-556 TEST-ONLY: resurrect list | resurrect <n> [squad <i>] | resurrect tab ... - resurrect.cpp, fallentab.cpp; [FALLEN] lines */
    {
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::ResurrectLever(Trim(rest)));
        finished = true;
    }
    else if (verb == "kill")   /* par6 (parity P6) TEST verb: kill <uid> - the engine's death steps; a copy's gate must refuse it */
    {
        unsigned int uid = 0;
        if (!(is >> uid) || uid == 0) WriteStatus("error kill: usage kill <uid>");
        else WriteStatus(coop::KillLever(uid));
        finished = true;
    }
    else if (verb == "decaysoon")   /* TEST-ONLY: decaysoon <uid> <hours> - that dead body's rot start is set so the game rots it after <hours> in-game hours on this game (own or a copy; medical.cpp DecaySoonLever) */
    {
        unsigned int uid = 0;
        float hours = -1.0f;
        if (!(is >> uid >> hours) || uid == 0) WriteStatus("error decaysoon: usage decaysoon <uid> <hours>");
        else WriteStatus(coop::DecaySoonLever(uid, hours));
        finished = true;
    }
    else if (verb == "destroybody")   /* T-306 TEST verb: destroybody <uid> - a DEAD character this game owns has its body destroyed through GameWorld::destroy 0x798F50 'eaten', the engine's own call when an animal has eaten a body */
    {
        unsigned int uid = 0;
        if (!(is >> uid) || uid == 0) WriteStatus("error destroybody: usage destroybody <uid>");
        else WriteStatus(coop::DestroyBodyLever(uid));
        finished = true;
    }
    else if (verb == "copysquad")   /* TEST-ONLY verb: copysquad <uid> | nearest - a copy of another game's world character (the one named, or the nearest within 2000 u of this game's first player character) is moved by this game's engine into a new squad of its own faction, numbered by this game (hire.cpp CopySquadLever / CopySquadNearestLever) */
    {
        std::string arg; is >> arg;
        const unsigned int uid = arg.empty() ? 0u : (unsigned int)strtoul(arg.c_str(), 0, 10);
        if (arg == "nearest") WriteStatus(coop::CopySquadNearestLever());
        else if (uid == 0) WriteStatus("error copysquad: usage copysquad <uid> | nearest");
        else WriteStatus(coop::CopySquadLever(uid));
        finished = true;
    }
    else if (verb == "townfill")   /* TEST-ONLY verb: townfill force | show - force starts the building-by-building refill of the next town this game holds near its player, without the empty-town verdict and the buildings' home answers; show lists the refills running here (towngen.cpp TownFillLeverImpl) */
    {
        std::string a1; is >> a1;
        WriteStatus(coop::TownFillLever(a1)); finished = true;
    }
    else if (verb == "owedtest")   /* TEST-ONLY verb: owedtest aside on|off | show - while on, this game sets aside every building's residents and bar roll it would make, and its town check-up makes none (towngen.cpp OwedLever); show logs the owed rows as this game sees them */
    {
        std::string a1, a2; is >> a1 >> a2;
        if (a1 == "show") WriteStatus(coop::TownGenOwedLever("show"));
        else if (a1 == "aside" && (a2 == "on" || a2 == "off")) WriteStatus(coop::TownGenOwedLever(a2));
        else WriteStatus("error owedtest: usage owedtest aside on|off | owedtest show");
        finished = true;
    }
    else if (verb == "bodydown")   /* P10 TEST verb: bodydown <anchorUid> - an owned animal near the anchor knocked out; an animal copy's body rebuilt while down (crash2 proof) */
    {
        /* P10 fold 1 (T633): an optional radius in metres, default 300 */
        unsigned int uid = 0;
        if (!(is >> uid) || uid == 0) WriteStatus("error bodydown: usage bodydown <anchorUid> [radiusM]");
        else
        {
            float radiusM = 300.0f;
            float r = 0.0f;
            if (is >> r) radiusM = r;
            WriteStatus(coop::BodyDownLever(uid, radiusM));
        }
        finished = true;
    }
    else if (verb == "drift")
    {
        // drift <seconds>  - period of the rolling state digest; 0 turns it off.
        int secs = 15;
        if (!(is >> secs)) secs = 15;
        coop::SetDriftPeriod(secs);
        WriteStatus("ok drift");
        finished = true;
    }
    else if (verb == "replicate")
    {
        // replicate <uid>  - stream the captured character's position under that uid
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: replicate <uid>   (uid from a prior spawn, see netstat)");
            WriteStatus("error replicate usage");
        }
        else
        {
            bool ok = coop::ReplicateStart(uid);
            WriteStatus(ok ? "ok replicate" : "error replicate");
        }
        finished = true;
    }
    else if (verb == "task")
    {
        // task <uid> <taskType> [dx] [dz]
        // Sends a task order for an owned uid; the peer injects it into its puppet and
        // pulses the decision gate once so the puppet adopts it (F081/F082).
        // Offsets are relative to the captured character, resolved to world coords here.
        unsigned int uid = 0; int type = -1; double dx = 0, dz = 0;
        is >> uid >> type;
        if (!(is >> dx)) dx = 10.0;
        if (!(is >> dz)) dz = 0.0;
        if (uid == 0 || type < 0)
        {
            DebugLog("[cmd] usage: task <uid> <taskType> [dx] [dz]"
                     "   (29 = MOVE_CUS_ORDERED, the player 'go here' order)");
            WriteStatus("error task usage");
        }
        else
        {
            ::Character* ref = coop::GetTarget();
            if (ref == 0)
            {
                DebugLog("[cmd] task: no captured character to resolve the location against");
                WriteStatus("error task no-target");
            }
            else
            {
                Ogre::Vector3 p = ref->worldPosition();
                bool ok = coop::SendTask(uid, type,
                                         p.x + (float)dx, p.y, p.z + (float)dz);
                WriteStatus(ok ? "ok task" : "error task");
            }
        }
        finished = true;
    }
    else if (verb == "unreplicate")
    {
        coop::ReplicateStop();
        WriteStatus("ok unreplicate");
        finished = true;
    }
    else if (verb == "combatorders")
    {
        // combatorders on|off - F251. Default OFF: three measured variants all made the peer
        // autonomous. ON is for testing a different approach, not for shipping.
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean ON.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error combatorders usage"); }
        else { coop::SetCombatOrders(mode == "on"); WriteStatus("ok combatorders"); }
        finished = true;
    }
    else if (verb == "lead")
    {
        // lead on|off - F350 / P-20. Default ON. OFF reproduces the deployed behaviour exactly,
        // so the jank fix can be measured against its own absence in the same run.
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean ON.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error lead usage"); }
        else { coop::SetLead(mode == "on"); WriteStatus("ok lead"); }
        finished = true;
    }
    else if (verb == "reconcile")
    {
        // reconcile on|off   - H016. Default ON since combat1 (user decision 2026-09-25; replicate.cpp g_reconcileOn). Earlier: default OFF (the comment here
        // said ON and was corrected AUD 2026-09-17). Only acts inside native windows.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error reconcile usage"); }
        else { coop::SetReconcile(mode == "on"); WriteStatus("ok reconcile"); }
        finished = true;
    }
    else if (verb == "tags")
    {
        // tags on|off   - tags1: the name labels over the other player's characters (ON by default; the Insert key toggles in game).
        // tags lift <n> - the label's height above the feet in world units (0 = automatic). tags - the [TAGS] REPORT line.
        std::string mode; is >> mode;
        if (mode == "on" || mode == "off") { coop::TagsSetOn(mode == "on"); DebugLog(coop::TagsReportLine()); WriteStatus("ok tags"); }
        else if (mode == "lift") { float v = -1.0f; is >> v; if (v < 0.0f) WriteStatus("error tags lift usage"); else { coop::TagsSetLift(v); WriteStatus("ok tags"); } }
        else if (mode.empty()) { DebugLog(coop::TagsReportLine()); WriteStatus("ok tags"); }
        else WriteStatus("error tags usage");
        finished = true;
    }
    else if (verb == "context")
    {
        // context on|off   - M-B / H026. Default ON since AUD 2026-09-17 (`context off` is the control). See spawn.h.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error context usage"); }
        else { coop::SetContextApply(mode == "on"); WriteStatus("ok context"); }
        finished = true;
    }
    else if (verb == "playerteleport")
    {
        // playerteleport <dx> <dz>   - M-D harness: move the watched player so this engine unloads sectors.
        // playerteleport abs <x> <z> - T193: to an absolute world position (a relative offset from a wandering player lands off target)
        //                  [<y> | ground] - P25 fold 2 (TEST): at that height, or the terrain's height there (none = the player's own)
        // playerteleport ground      - P25 fold 2 (TEST): down (or up) to the terrain's height at the player's own x,z
        // playerteleport peer        - T-1 B0 (TEST): to about 5 m from this game's copy of the other player's lead character
        // playerteleport groundpick shooter|target - T-310 (TEST): to that end of THIS game's last [GROUNDFIND] FOUND line (x,z;
        //                              the same path as abs); "error playerteleport groundpick none" when this game has found none
        // playerteleport near <uid | nearest <dialogueName>> <units> - P26 stage 6 (TEST): to <units> from that outdoor character,
        //                          [maxdy <n>] (P26lvl): the point's terrain within n of the character's feet (straight line, then 16 around)
        //                          on the line toward this game's player (speech.cpp TalkNearPoint), through PlayerTeleportAbs;
        //                          T718: when the character is not loaded yet it WAITS up to 60 s for it to load (TalkWaitStep)
        std::string first; is >> first;
        if (first == "near")
        {
            std::string arg; std::getline(is, arg);
            float nx = 0, nz = 0; std::string why;
            const int nrc = coop::TalkNearPoint(Trim(arg), &nx, &nz, &why);
            if (nrc == 0) { DebugLog("[TALKSIGHT] playerteleport near REFUSED: " + why); WriteStatus("error playerteleport near: " + why); }
            else if (nrc == 2) { WriteStatus("ok playerteleport near: waiting up to 60 s for the character to load"); }
            else { WriteStatus(coop::PlayerTeleportAbs(nx, nz) ? "ok playerteleport near" : "error playerteleport near (the teleport failed)"); }
        }
        else if (first == "peer")
        {
            WriteStatus(coop::PlayerTeleportPeer() ? "ok playerteleport peer" : "error playerteleport peer");
        }
        else if (first == "groundpick")
        {
            std::string which, extra;
            float gx = 0, gz = 0; long long gid = 0;
            if (!(is >> which) || (which != "shooter" && which != "target") || (is >> extra))
            { WriteStatus("error playerteleport groundpick usage: playerteleport groundpick shooter|target"); }
            else if (!coop::GroundFindLastPick(which == "target", &gx, &gz, &gid)) { WriteStatus("error playerteleport groundpick none"); }
            else
            {
                std::ostringstream os; os.imbue(std::locale::classic());
                os << " " << which << " groundfind id=" << gid << " to " << gx << "," << gz;
                WriteStatus((coop::PlayerTeleportAbs(gx, gz) ? "ok playerteleport groundpick" : "error playerteleport groundpick") + os.str());
            }
        }
        else if (first == "ground")   /* P25 fold 2 (TEST) */
        {
            WriteStatus(coop::PlayerTeleportGround() ? "ok playerteleport ground" : "error playerteleport ground");
        }
        else if (first == "abs")
        {
            float ax = 0, az = 0;
            if (!(is >> ax >> az)) { WriteStatus("error playerteleport abs usage"); }
            else
            {
                std::string ytok; is >> ytok;   /* P25 fold 2: [<y> | ground] */
                if (ytok.empty()) WriteStatus(coop::PlayerTeleportAbs(ax, az) ? "ok playerteleport abs" : "error playerteleport abs");
                else
                {
                    float ay = 0.0f; const bool gnd = (ytok == "ground");
                    std::istringstream ys(ytok);
                    if (!gnd && !(ys >> ay)) WriteStatus("error playerteleport abs usage (the height is a number or ground)");
                    else WriteStatus(coop::PlayerTeleportAbsY(ax, az, gnd, ay) ? "ok playerteleport abs" : "error playerteleport abs");
                }
            }
        }
        else
        {
            float dx = 0, dz = 0; std::istringstream fs(first);
            if (!(fs >> dx) || !(is >> dz)) { WriteStatus("error playerteleport usage"); }
            else { WriteStatus(coop::PlayerTeleport(dx, dz) ? "ok playerteleport" : "error playerteleport"); }
        }
        finished = true;
    }
    else if (verb == "platoonkill")
    {
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::PlatoonKill(rest) ? "ok platoonkill" : "error platoonkill"); finished = true;
    }
    else if (verb == "uipreview")
    {
        // uipreview <what>   - TEST-ONLY (uishot, owner 2026-09-27): show one co-op screen for a harness screenshot, through
        // the code that shows it for real (ui.cpp UiPreviewCommand). Title: `panel <screen>`, `errorbox`; in a world: `closed`,
        // `lost`, `notice`; `off` takes the preview down. Presses nothing, writes nothing, never touches the host-left state.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::UiPreviewCommand(rest) ? "ok uipreview" : "error uipreview");
        finished = true;
    }
    else if (coopui::UiDriveIsVerb(verb) != 0)
    {
        /* uiclick <name> | uipick <list> <row text> | uitype <field> <text> | uistate   - TEST-ONLY (pp1, player-path-plan 1a):
           drive the REAL panel and Kenshi title buttons through their own handlers; a hidden or disabled widget is REFUSED
           (ui.cpp UiDriveCommand). The name table is common/uidrive.h. */
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus((coop::UiDriveCommand(verb, rest) ? "ok " : "error ") + verb);
        finished = true;
    }
    else if (verb == "hostaddr")
    {
        // hostaddr fake <ip> | hostaddr show | hostaddr hide   - TEST-ONLY (T-510): the HOSTING screen's INTERNET ADDRESS row
        // takes <ip> as if the home router had reported it (ui.cpp UiHostAddrCommand), and SHOW / HIDE without a click. Asks no
        // router and sends nothing; the address is never logged.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::UiHostAddrCommand(rest) ? "ok hostaddr" : "error hostaddr");
        finished = true;
    }
    else if (verb == "uimenu")
    {
        // uimenu ingame   - TEST-ONLY (T-228 (1)): open Kenshi's in-game menu (SAVE GAME, LOAD GAME, ... RESUME) through the
        // engine's own getter + show - the in-game ESC route (ui.cpp UiMenuCommand), no injected input (F010). Refused at the
        // title screen. Then `uiclick engine:SaveGameButton` reaches the real Save window.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::UiMenuCommand(rest) ? "ok uimenu" : "error uimenu");
        finished = true;
    }
    else if (verb == "chat")
    {
        // chat open everyone|faction|<slot> | send everyone|faction|<slot> sample <1-4> | fill sample <1-4> | tolist [search <letters>]
        //      | tab | click <line, 1 = newest> | opacity <0-100> | move <x> <y> <w> <h> | esc | close | state   - TEST-ONLY:
        // drives the chat window as the keys and clicks do (chat.cpp ChatCommand). It never takes free text - every command is
        // logged, and chat words never are - only the four fixed sample sentences; a command with more words than its form
        // is refused.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::ChatCommand(rest));
        finished = true;
    }
    else if (verb == "quitmenu")
    {
        // quitmenu   - E30-3 (P6p) lever: end this game through the GAME'S OWN pause-menu exit, not the
        // window close, so the P6l hook on the exit-confirm accept path is exercised by a run instead of
        // shipping unexercised. No arguments: the only thing the engine's callback reads is the result
        // code, and the lever passes the one the engine accepts.
        //
        // THE STATUS IS WRITTEN FIRST, on purpose. QuitViaMenu() raises the engine's quit byte, and while
        // the flag is only polled by the main loop, nothing here should depend on this tick's tail still
        // running. `error quitmenu` is therefore only reachable when the lever REFUSED - which it does,
        // loudly and with a reason, when the hook is not armed or no world is running.
        WriteStatus("ok quitmenu");
        if (!coop::QuitViaMenu()) WriteStatus("error quitmenu");
        finished = true;
    }
    else if (verb == "uniqueforce")
    {
        // uniqueforce <sid> dead|deadsend|alive|clearslot|show   - TEST-ONLY: this game's own unique-state entry for that named character is
        // written (deadsend also publishes DEAD to the world server, as a death would); clearslot empties its made slot so this game may
        // make the character again; show reads it, its made slot, whether the record-member filter holds it dead, whether the may-make
        // check holds it alive in another game, and its living bodies here with each one's owner, its area's runner and the duplicate decision.
        std::string sid, what; is >> sid >> what;
        const std::string line = coop::WorldStateForceLocal(sid, what);
        WriteStatus(line); if (line.compare(0, 3, "ok ") == 0) DebugLog("[UNIQ] " + line);
        finished = true;
    }
    else if (verb == "killnamed")
    {
        // killnamed <name substring>   - E21 (T223): kill the LOADED named character whose game-data name contains this
        // text. The rest of the line is the substring, spaces and all (a name like `Dust King` is two tokens), so it is
        // read with getline exactly as platoonkill reads a world id.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::KillNamed(rest) ? "ok killnamed" : "error killnamed"); finished = true;
    }
    else if (verb == "itemtest")
    {
        // itemtest <name substring> <base data sid> [count]   - E22a: put an item into a character we OWN so the
        // transfer detour fires on the main thread and ItemsTick sends ITEM_MOVE to the other game.
        // The rest of the line is read with getline, because a name like `Dust King` is two tokens; it is then
        // split FROM THE RIGHT so the name may contain spaces: an all-digit last token is the count, the token
        // before the count is the base data sid, and everything left of that is the name substring.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        /* inv2b: `itemtest takeone <n>` - TEST-ONLY (T385): one unit off the nearest held box's first stack of two or more through
           the engine's own Inventory::takeOneItemOnly 0x749FF0, into this game's first own character (coop::ItemTestTakeOneCommand).
           t338-lever: `itemtest takeone <n> other consume` - the unit is used where it was taken (destroyed, as an eat) instead of
           going into the pack, so the par1 drain routes the take as a CONSUME. */
        const bool takeOne = (rest.compare(0, 7, "takeone") == 0 && (rest.size() == 7 || rest[7] == ' '));
        /* give1: `itemtest confirmdelay <0..30>` - TEST-ONLY (T396): this game, as the OWNER of a request, holds each outgoing
           ITEM_CONFIRM that many seconds before sending it (0 = off, the default), so a run can show a late answer to a give. */
        const bool cfDelay = (rest.compare(0, 12, "confirmdelay") == 0 && (rest.size() == 12 || rest[12] == ' '));
        /* T-1 B4-1: `itemtest refusenext [n] [reason]` - TEST-ONLY: as the HOLDER, refuse the next n priced shop requests. */
        const bool refuseNext = (rest.compare(0, 10, "refusenext") == 0 && (rest.size() == 10 || rest[10] == ' '));
        /* T-164 B4-2: `itemtest refusehere <gate> [n]` - TEST-ONLY: as the REQUESTER, take that refusal exit after the put-back. */
        const bool refuseHere = (rest.compare(0, 10, "refusehere") == 0 && (rest.size() == 10 || rest[10] == ' '));
        /* T-164 211: `itemtest boxmove <fromBoxKey|peer> <toBoxKey> <sid|any>` - TEST-ONLY: one whole stack across the authority line
           (a box the other game holds / its character -> a box this game holds, or the reverse) through the engine's own calls. */
        const bool boxMove = (rest.compare(0, 7, "boxmove") == 0 && (rest.size() == 7 || rest[7] == ' '));
        /* T-215 (owner 177 a): `itemtest packitem [n]` - TEST-ONLY: n sellable single items into this game's own seller's general grid. */
        const bool packItem = (rest.compare(0, 8, "packitem") == 0 && (rest.size() == 8 || rest[8] == ' '));
        /* inv6: `itemtest steal <n> [sid]` / `itemtest stolen <me|peer|name>` / `itemtest givestolen <peer|name>` - TEST-ONLY (design Q5):
           a box item stolen through the engine's own right-click, the marked items of a character, and a GIVE of the stolen stack. */
        const bool ownSteal = (rest.compare(0, 5, "steal") == 0 && (rest.size() == 5 || rest[5] == ' '));
        const bool ownStolen = (rest.compare(0, 6, "stolen") == 0 && (rest.size() == 6 || rest[6] == ' '));
        const bool ownGive = (rest.compare(0, 10, "givestolen") == 0 && (rest.size() == 10 || rest[10] == ' '));
        /* inv6 fold (T401): `itemtest mark <sid|any> [<uid>]` - TEST-ONLY: an owner hand on my own item (T399's steal moved nothing). */
        const bool ownMark = (rest.compare(0, 4, "mark") == 0 && (rest.size() == 4 || rest[4] == ' '));
        /* items9: `itemtest set <any|sid|section:x,y> [q=<0-100>] [ch=<n>]` - TEST-ONLY: a high-grade weapon / a half-used medkit. */
        const bool ownSet = (rest.compare(0, 3, "set") == 0 && (rest.size() == 3 || rest[3] == ' '));
        std::vector<std::string> tok; std::string t;
        {
            std::istringstream ts(rest);
            while (ts >> t) tok.push_back(t);
        }
        std::string nm, sid; int cnt = 1; size_t end = tok.size();
        if (end >= 3)
        {
            bool digits = !tok[end - 1].empty();
            for (size_t i = 0; i < tok[end - 1].size(); ++i) if (tok[end - 1][i] < '0' || tok[end - 1][i] > '9') digits = false;
            if (digits)
            {
                int v = 0;
                for (size_t i = 0; i < tok[end - 1].size() && v < 100000; ++i) v = v * 10 + (tok[end - 1][i] - '0');
                cnt = v; --end;
            }
        }
        if (end >= 2)
        {
            sid = tok[end - 1]; --end;
            for (size_t i = 0; i < end; ++i) { if (i != 0) nm += " "; nm += tok[i]; }
        }
        if (boxMove) WriteStatus(coop::ItemTestBoxMoveCommand(Trim(rest.substr(7))));
        else if (refuseNext) WriteStatus(coop::ItemTestRefuseNextCommand(Trim(rest.substr(10))));
        else if (refuseHere) WriteStatus(coop::ItemTestRefuseHereCommand(Trim(rest.substr(10))));
        else if (packItem) WriteStatus(coop::ItemTestPackItemCommand(Trim(rest.substr(8))));
        else if (cfDelay) WriteStatus(coop::ItemTestConfirmDelayCommand(Trim(rest.substr(12))));
        else if (takeOne) WriteStatus(coop::ItemTestTakeOneCommand(Trim(rest.substr(7))));
        else if (ownSteal) WriteStatus(coop::ItemTestStealCommand(Trim(rest.substr(5))));
        else if (ownStolen) WriteStatus(coop::ItemTestStolenCommand(Trim(rest.substr(6))));
        else if (ownGive) WriteStatus(coop::ItemTestGiveStolenCommand(Trim(rest.substr(10))));
        else if (ownMark) WriteStatus(coop::ItemTestMarkCommand(Trim(rest.substr(4))));
        else if (ownSet) WriteStatus(coop::ItemTestSetCommand(Trim(rest.substr(3))));
        else WriteStatus(coop::ItemTest(nm, sid, cnt) ? "ok itemtest" : "error itemtest");
        finished = true;
    }
    else if (verb == "boxtest")
    {
        // boxtest <key substring> <base data sid> [count]   - E22c: put an item into the first LOADED STORAGE
        // BOX whose building instance id contains this text, through the engine's own add, so the item hooks
        // ring a box move and the drain publishes it when this game holds that patch of map. The hit line
        // prints the FULL instance id, so the next run can name that exact box.
        //
        // Split FROM THE RIGHT exactly as `itemtest` is, and for the same reason: an instance id has no
        // spaces, but a substring the operator types might, and the count is optional. An all-digit last
        // token is the count; the token before it is the base data sid; everything left of that is the key
        // substring.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        std::vector<std::string> tok; std::string t;
        {
            std::istringstream ts(rest);
            while (ts >> t) tok.push_back(t);
        }
        std::string nm, sid; int cnt = 1; size_t end = tok.size();
        if (end >= 3)
        {
            bool digits = !tok[end - 1].empty();
            for (size_t i = 0; i < tok[end - 1].size(); ++i) if (tok[end - 1][i] < '0' || tok[end - 1][i] > '9') digits = false;
            if (digits)
            {
                int v = 0;
                for (size_t i = 0; i < tok[end - 1].size() && v < 100000; ++i) v = v * 10 + (tok[end - 1][i] - '0');
                cnt = v; --end;
            }
        }
        if (end >= 2)
        {
            sid = tok[end - 1]; --end;
            for (size_t i = 0; i < end; ++i) { if (i != 0) nm += " "; nm += tok[i]; }
        }
        WriteStatus(coop::BoxTest(nm, sid, cnt) ? "ok boxtest" : "error boxtest"); finished = true;
    }
    else if (verb == "buytest")
    {
        // buytest <key substring> [sid] [npc] [stack]   - E35: buy ONE item out of the first LOADED SHOP
        // COUNTER whose building instance id contains this text, through the engine's own purchase call
        // (the PLAYER's right-click path by default, the NPC one on `npc` or as a stated fallback) and with
        // the money
        // trade window OPEN - so the price is the number the game itself would charge, P6o's trade detection
        // reads 1, and the shop request, the price on the wire and the keeper's cats all run without eyes.
        // The optional second token picks the first stock item whose base record id contains it.
        //
        // NOT split from the right the way `itemtest` and `boxtest` are, and the reason is a fact rather than
        // a convenience: those two take a COUNT, so their last token is ambiguous; this one does not. A
        // building instance id has no spaces in it - every id `boxtest` has ever printed is one token - so
        // the first token is the key and the second, if there is one, is the sid. A third token is refused
        // out loud rather than quietly folded into one of the first two.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        std::vector<std::string> tok; std::string t;
        {
            std::istringstream ts(rest);
            while (ts >> t) tok.push_back(t);
        }
        // P7d. TWO MODE WORDS may appear anywhere among the tokens and are taken out before the key and
        // the sid are read: `npc` forces the OLD purchase path (Inventory::buyItem, what an NPC shopper
        // runs) so the two paths can be compared in one run, and `stack` lets the pick take a stack of more
        // than one - which buys a CLONE and rings no seller-side hook, a different experiment that the log
        // line then names. They are matched EXACTLY, so a record id that merely contains "npc" is still a
        // perfectly good sid; a sid that IS the word "npc" cannot be asked for, and that is stated here
        // rather than discovered.
        // T-1 B4-1: a THIRD mode word, `sell` - the buyer SELLS one single non-money item of its own main grid to that
        // counter's trade window (the player's right-click, as packbuytest sell); the sid then picks the SELLER's item.
        // sell goes alone: with npc or stack it is refused as usage.
        int buytestNpc = 0, buytestStack = 0, buytestSell = 0;
        int buytestCats = -1, buytestCatsBad = 0;   /* T-164 B4-1: `cats=<n>` (buytest keeper only) */
        std::vector<std::string> arg;
        for (size_t bi = 0; bi < tok.size(); ++bi)
        {
            if (tok[bi].compare(0, 5, "cats=") == 0) { if (tradecharge::ParseCats(tok[bi].c_str(), &buytestCats) == 0) buytestCatsBad = 1; continue; }
            if (tok[bi] == "npc") { buytestNpc = 1; continue; }
            if (tok[bi] == "stack") { buytestStack = 1; continue; }
            if (tok[bi] == "sell") { buytestSell = 1; continue; }
            arg.push_back(tok[bi]);
        }
        // T-164 B4-0 (TEST LEVER): `buytest keeper <n|ghost|own|counter=<key text>> [sell] [cats=<n>]` - the FIRST key token being
        // exactly `keeper` selects BuyTestKeeper (items.cpp): n = a row of the last `keeperlist`, ghost = a keeper whose home area
        // the other game holds (local stock preferred, not required), own = a keeper whose stocked pieces this game all holds,
        // counter=<text> = PINNED to the keeper whose counter's position key contains <text>. lev343-aim (owner 343 b): every form
        // aims only at a counter the trade can take stock from (BuyTest's own stock-grid test) and chooses by squad id and leader
        // uid, never load order (src/common/keeperaim.h). The buyer is walked to the keeper first (B4-0b). The word `keeper` alone
        // therefore can no longer be a key substring for the old form.
        if (!arg.empty() && arg[0] == "keeper")
        {
            if (arg.size() != 2 || buytestNpc != 0 || buytestStack != 0 || buytestCatsBad != 0) WriteStatus("error buytest keeper usage: buytest keeper <n|ghost|own|counter=<key text>> [sell] [cats=<n>]");
            else WriteStatus(coop::BuyTestKeeper(arg[1], buytestSell, buytestCats) ? "ok buytest keeper" : "error buytest keeper");
        }
        else if (arg.empty() || arg.size() > 2 || buytestCats >= 0 || buytestCatsBad != 0 || (buytestSell != 0 && (buytestNpc != 0 || buytestStack != 0))) { WriteStatus("error buytest usage"); }
        else
        {
            const std::string want = (arg.size() > 1) ? arg[1] : std::string();
            WriteStatus(coop::BuyTest(arg[0], want, buytestNpc, buytestStack, buytestSell)
                        ? "ok buytest" : "error buytest");
        }
        finished = true;
    }
    else if (verb == "traderlist")
    {
        // traderlist   - T-1 B0 (TEST, read-only): every loaded trader squad - leader, which game runs each member, each pack's
        // digest and item count, then `[TRADER] list squads= splitSquads= members=`. items.cpp TraderList.
        WriteStatus(coop::TraderList() ? "ok traderlist" : "error traderlist");
        finished = true;
    }
    else if (verb == "packbuytest")
    {
        // packbuytest [ghost|own] [stack|sell] [goto] [cats=<n>]   - T-1 B0: stack = one unit off a stacked item, sell = one single item
        // of the buyer's to the trader; candidates are trader squads' leaders (items.cpp TlSquads). T-182: cats=<n> (any position)
        // sets the buyer's purse to n first, as buytest keeper cats=<n> does (logged `[STORE] packbuytest cats`).
        // packbuytest [ghost|own]   - par23 (parity P23), TEST LEVER: buy ONE single item out of a loaded wandering
        // trader's pack through the player's right-click path with the money window open. ghost (default): a trader
        // the other game writes - since T-1 B5 the trade is asked of that game (the shop road); own: the control, the engine's trade.
        // T-1 B5: [match|nomatch] (sell), [all] (the shift flag around the call), [sid=<baseSid>], and `packbuytest own
        // packfill=<room> [sid=<baseSid>]` (fill the caravan's packs, leave <room> units of room - no trade). Parsed in items.cpp.
        // T-1 B5 fold 2: [trader=spawned|trader=<uid>] on every form (goto and packfill too) keeps ONE trader squad - spawned: this
        // game's last traderspawn, else the nearest loaded Traders Guild trader squad; <uid>: the squad with that member.
        // par23b: `packbuytest [ghost|own] goto` only moves the buyer next to the nearest candidate (run the plain
        // form ~3 s later: it trades with that candidate and puts the buyer back). Parsed in items.cpp PackBuyTest.
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::PackBuyTest(rest) ? "ok packbuytest" : "error packbuytest");
        finished = true;
    }
    else if (verb == "bagtest")
    {
        // bagtest wear <who> <packSid|auto> | put <who> <sid> [count] | list <who> | out <who> <x> <y> | take <ghost> <x> <y>
        // | out <who> sid <baseSid> | take <ghost> sid <baseSid> | unwear <who> | wearback <who>   (bag23 lever: by record; a pack move with its contents)
        // (out / take: bag23 part 2 - one inner item of a pack to main, on our own character / out of the other game's)   - inv3a TEST lever (inv3-backpack-design.md
        // phase 1 test): create and wear a backpack on THIS game's own character, put items into it, or list any loaded
        // character's pack (read-only). <who> is `me`, `peer` or a name substring (coop::BagTestCommand).
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::BagTestCommand(Trim(rest)));
        finished = true;
    }
    else if (verb == "itemgive")
    {
        // itemgive <mine> <section> <x> <y> <ghost>   - inv3a TEST lever: the GIVE twin of itemtake (coop::ItemGive).
        std::string mineWho, sec, ghostWho; int gx = -1, gy = -1;
        if (!(is >> mineWho >> sec >> gx >> gy >> ghostWho)) WriteStatus("error itemgive usage: itemgive <me|name> <section> <x> <y> <peer|name>");
        else WriteStatus(coop::ItemGive(mineWho, sec, gx, gy, ghostWho) ? "ok itemgive" : "error itemgive");
        finished = true;
    }
    else if (verb == "itemtake")
    {
        // itemtake <name substring> <section> <x> <y> [qty]   - E34: take the item at (section, x, y) out of a
        // LOADED character this game does NOT own (a ghost) and put it into the local player's first
        // player-faction character, through the engine's own transfer calls - so the item hooks ring a REMOVE
        // on the ghost and an ADD on the player carrying the SAME Item object, which is the pair E22b's
        // request path is recognised by. Omit qty for the whole stack; that is the form that rings the pair.
        //
        // The rest of the line is read with getline, because a name like `Dust King` is two tokens, and it is
        // split FROM THE RIGHT. x, y and qty are all digits, so the number of TRAILING all-digit tokens says
        // whether a qty was given: three means `x y qty`, two means `x y`. A name that ends in a number
        // ("Guard 12") is safe, because its digits are not trailing. A SECTION whose name is all digits would
        // be misread; that is stated rather than guarded, because no section on this install is called "7".
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        std::vector<std::string> tok; std::string t;
        {
            std::istringstream ts(rest);
            while (ts >> t) tok.push_back(t);
        }
        size_t end = tok.size(), trail = 0;
        while (trail < 3 && trail < tok.size())
        {
            const std::string& w = tok[tok.size() - 1 - trail];
            bool digits = !w.empty();
            for (size_t i = 0; i < w.size(); ++i) if (w[i] < '0' || w[i] > '9') digits = false;
            if (!digits) break;
            ++trail;
        }
        int qty = 0;
        if (trail >= 3 && tok.size() >= 5) { qty = atoi(tok[end - 1].c_str()); --end; }
        if (end < 4) { WriteStatus("error itemtake usage"); }
        else
        {
            const int ty = atoi(tok[end - 1].c_str());
            const int tx = atoi(tok[end - 2].c_str());
            const std::string sec = tok[end - 3];
            std::string nm;
            for (size_t i = 0; i + 3 < end; ++i) { if (i != 0) nm += " "; nm += tok[i]; }
            WriteStatus(coop::ItemTake(nm, sec, tx, ty, qty) ? "ok itemtake" : "error itemtake");
        }
        finished = true;
    }
    else if (verb == "cursortest")
    {
        // cursortest open <boxKey> | close | pick <boxKey|me|peer> <section> <x> <y> [all] | drop <boxKey|me|peer> <section> <x> <y>
        // | back | list   - p105h TEST-ONLY lever (p105 design 10 / 15.1 / 15.2): the engine's own cursor pickup 0x712DD0 (the mouse
        // position set to the square around the call), drop 0x714990 (the square's top-left pixel) and return 0x70D000, on a container
        // whose window is open (open = the engine's loot open, boxopen's road). One [ITEMS] p105 cursortest line each (coop::CursorTestCommand).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::CursorTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "boxmove")
    {
        // boxmove <boxKey> <section> <fx> <fy> <tx> <ty> [n]   - p105h TEST-ONLY lever: n units (the whole item when omitted or >= its
        // count) from one square of a box to a FREE square of the same box, through the engine's own take-out + add, so the drain
        // sees a one-tick drag inside one container. boxmove <fromKey> <section> <fx> <fy> <toKey> [n] (T-159): the same into the
        // first free square of that section of ANOTHER box. One [ITEMS] p105 boxmove line (coop::BoxMoveCommand).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BoxMoveCommand(Trim(arg))); finished = true;
    }
    else if (verb == "holdtest")
    {
        // holdtest delay <s> [n] | refusenext <gone|busy|policy> | list   - P105 build 2 TEST-ONLY lever (design 10.3): on the WRITER, the
        // next n HOLD / LAND requests are kept unprocessed for s seconds and released in arrival order; the next HOLD is refused with
        // that reason; or its hold rows are listed. One [ITEMS] p105 holdtest line each (coop::HoldTestCommand).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::HoldTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "platoonwipe")
    {
        std::string rest; std::getline(is, rest); rest = Trim(rest);
        WriteStatus(coop::PlatoonWipe(rest) ? "ok platoonwipe" : "error platoonwipe"); finished = true;
    }
    else if (verb == "platoonnudge")
    {
        // platoonnudge <worldId words...> <dx> <dz>   - move a named awake platoon (the id may contain spaces: the last two tokens are dx dz)
        std::vector<std::string> toks; std::string tk; while (is >> tk) toks.push_back(tk);
        if (toks.size() < 3) { WriteStatus("error platoonnudge usage"); }
        else
        {
            const float dx = (float)atof(toks[toks.size() - 2].c_str()), dz = (float)atof(toks[toks.size() - 1].c_str());
            std::string id; for (size_t i = 0; i + 2 < toks.size(); ++i) { if (i) id += " "; id += toks[i]; }
            WriteStatus(coop::PlatoonNudge(id, dx, dz) ? "ok platoonnudge" : "error platoonnudge");
        }
        finished = true;
    }
    else if (verb == "squadnudge")
    {
        // squadnudge <dx> <dz>   - M-D step 2 harness: move the first squad I own by (dx, dz) into the other side's sector.
        float dx = 0, dz = 0;
        if (!(is >> dx >> dz)) { WriteStatus("error squadnudge usage"); }
        else { WriteStatus(coop::SquadNudge(dx, dz) ? "ok squadnudge" : "error squadnudge"); }
        finished = true;
    }
    else if (verb == "peerfaction")
    {
        // peerfaction <name> - P3: create (or rename) the peer faction as if the peer's player faction were called <name>.
        std::string name; std::getline(is, name); size_t s0 = name.find_first_not_of(" \t"); name = (s0 == std::string::npos) ? std::string() : name.substr(s0);
        Faction* f = coop::PeerFactionForTest(name.empty() ? std::string("Nameless") : name);
        WriteStatus(f ? "ok peerfaction" : "error peerfaction failed");
        finished = true;
    }
    else cmdChainB = true;   /* batch2: no verb of the first half - try the second half */
    if (cmdChainB)
    {
    if (verb == "profile")
    {
        /* prof1 (TEST, until the profile screen): `profile new <name>` asks the world for a new profile of this player;
           `profile delete <n>` deletes one - its save folder goes to the Recycle Bin when the world's answer comes back. */
        std::string sub; is >> sub;
        std::string rest; std::getline(is, rest); size_t s0 = rest.find_first_not_of(" \t"); rest = (s0 == std::string::npos) ? std::string() : rest.substr(s0);
        bool ok = false;
        if (sub == "new") ok = coop::StoreProfileRequest(1, 0, rest);
        else if (sub == "delete") ok = coop::StoreProfileRequest(2, (unsigned)std::atoi(rest.c_str()), std::string());
        WriteStatus(ok ? "ok profile " + sub : std::string("error profile (use: profile new <name> | profile delete <n>; the notebook link must be up)"));
        finished = true;
    }
    else if (verb == "renamemyfaction")
    {
        // renamemyfaction <name>       - the engine's write on this game's player faction (setName + the record), with no FACTION tab check:
        //                                 a rename the other games have not heard of yet (the world judges it: T-368 kind 5)
        // renamemyfaction tab <name>   - TEST-ONLY (T-368): the FACTION tab's name box gets <name> and its Enter - Kenshi's own handler,
        //                                through the mod's check of the world's faction names (ui.cpp UiFactionTabRename)
        std::string name; std::getline(is, name); size_t s0 = name.find_first_not_of(" \t"); name = (s0 == std::string::npos) ? std::string() : name.substr(s0);
        if (name.compare(0, 4, "tab ") == 0)
        {
            const std::string typed = name.substr(4);
            WriteStatus(coop::UiFactionTabRename(typed) ? "ok renamemyfaction tab" : "error renamemyfaction tab");
        }
        else WriteStatus(coop::RenameMyFaction(name) ? "ok renamemyfaction" : "error renamemyfaction");
        finished = true;
    }
    else if (verb == "boxlist")
    {
        // boxlist [<x>,<y>|all] [<page>]   - P7i: print every loaded building that carries an inventory, with its FULL
        // building instance id, so a plan can name a real box instead of guessing a needle. T232 sent
        // `boxtest rebirth ...` and missed; `buildings` above prints per-faction counts and not one id, so
        // there was nothing to look the right id up in. No argument = ring 1 of the player's sector, `<x>,<y>`
        // re-centres that ring, `all` = every loaded zone; a trailing number is the PAGE. Read-only: boxlist
        // creates nothing, moves nothing and sends nothing - it only reads and prints.
        /* P8a (F627 / T236 READOUT-b D-1): THE WHOLE REST OF THE LINE, not one whitespace token.  P7y put the
           two-argument parse in ItListWindowOf and left this line reading `is >> arg`, so `boxlist all 2` arrived
           as "all" and printed page 1 again - while the build's own hint told the operator to type exactly that.
           Only the one-token forms worked, and T236b's box-key diff was taken with the wrong window because of it. */
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BoxListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "settingsdump")
    {
        // settingsdump   - settings1 S1 (docs/design-settings1.md s5): [SETTINGS] advanced(save), [SETTINGS] gameplay and
        // [MODS] fingerprint, the same three lines the link and world-live edges print. Read-only: writes and sends nothing.
        WriteStatus(coop::SettingsDumpCommand()); finished = true;
    }
    else if (verb == "advset")
    {
        // advset <key> <value>   - settings2 S2 (TEST-ONLY lever): key = cod ep gdm bs nnm rs ps ht bl ae dh (or gp.<key>).
        // On the world's authority: sets the live advanced option and records it through the same change path as any
        // own change (the notebook stores it and broadcasts it to every game). On any other game: REFUSED here and
        // logged; the offer still goes up so the notebook logs its own refusal. Nothing else is changed.
        std::string k, v; is >> k >> v;
        WriteStatus(coop::SettingsAdvSetCommand(k, v)); finished = true;
    }
    else if (verb == "gtset")
    {
        // gtset <key> <value>   - settings4 S4 (TEST-ONLY lever): key = pop squad raidsize raidfreq attacks limbloss civ (or gt.<key>).
        // On the world's authority: sets the live gameplay-tab value (settings.cfg is not written) and records it as an own
        // change, so the notebook stores it and every other game follows it. On any other game: REFUSED and logged; the
        // offer still goes up so the notebook logs its own refusal.
        std::string k, v; is >> k >> v;
        WriteStatus(coop::SettingsGtSetCommand(k, v)); finished = true;
    }
    else if (verb == "researchlist")
    {
        // researchlist [<x>,<y>|all] [<page>]   - loot2a (T333, docs/design-loot2.md s4 "L2a probe"): every RESEARCH item
        // (item+0x180 "artifact" != 0) in every loaded box in the window, with the box's id, position key and holder, so
        // A and B can be diffed before any instancing is built. Same window/page argument as boxlist, whole rest of the
        // line (F627). Read-only: it creates, moves and sends nothing.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::ResearchListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "researchmode")
    {
        // researchmode [on|off]   - loot2b (T361, docs/design-loot2.md 4A "L2b' lift"): the HOST option `researchmode`. With
        // no argument it reports this game's value and the lift counters. With one it asks the notebook to store it for
        // the world - accepted only from the session authority, like `recruitmult`. Default OFF until L2c'. on: the game holding a
        // box lifts its research items into research_boxes.txt when the box is opened; off: they stay world items.
        std::string v; is >> v;
        if (v.empty()) WriteStatus("ok researchmode " + coop::ResearchModeStatus());
        else if (v != "on" && v != "off") WriteStatus("error researchmode: on|off");
        else if (!coop::StoreSendOption("researchmode", v)) WriteStatus("error researchmode: no notebook link - the option is kept by SharedWastelandsServer.exe, not by this game");
        else WriteStatus("ok researchmode sent");
        finished = true;
    }
    else if (verb == "snapshot")
    {
        // snapshot [<tag> [at <world hours>] [detail <uid>]]   - snap2: `at` = sample at that exact world instant (the harness
        // passes A's armed target to B); no `at` = the next whole minute at least 3 in-game minutes ahead; `detail` = item lines.
        // snapshot [<tag>]   - snap1 (user decision 2026-09-26): READ-ONLY. Arms one [SNAP] block for the next whole in-game minute
        // of the shared world clock (items.cpp SnapshotCommand / SnapshotTick); tools/parity_compare.py pairs the two games' blocks.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::SnapshotCommand(Trim(arg))); finished = true;
    }
    else if (verb == "boxdigest")
    {
        // boxdigest [<x>,<y>|ring|all]   - par1 (docs/design-loot2.md rev 2, 4A "PAR-1 pull"): one [PARITY] box key=.. items=..
        // digest=.. line per non-player box in the window - the flat walk AND the interior-furniture registry - then
        // [PARITY] boxdigest total boxes=N items=M. No argument or `ring` = ring 1 of the player's sector. Read-only: it
        // creates, moves and sends nothing.
        // boxdigest [<x>,<y>|ring|all] players   - the same with player-owned boxes too, so two games' machines can be compared.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BoxDigestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "restocknow")
    {
        // restocknow <x>,<y>   - par2 TEST-ONLY lever (docs/design-loot2.md rev 2, 4A "PAR-2 push"): at the next K2 safe
        // point, the engine's own refreshInventory(force) for every trader squad whose shop counter stands in that sector
        // and whose shop this game HOLDS; a squad another game holds is refused and counted. [PARITY] restocknow line.
        // restocknow wander near   - T-1 B2 TEST-ONLY: the same, for every loaded wandering trader squad within ~200 m of this
        // game's player; a squad this game does not run is refused and counted (refusedNotWriter / refusedUnrun).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::RestockNowCommand(Trim(arg))); finished = true;
    }
    else if (verb == "traderspawn")
    {
        // traderspawn [dist]   - T-1 TEST-ONLY lever (test ground for travelling traders' packs): at the next K2 safe point, ONE
        // wandering trader caravan (vanilla squad template 'Caravan' 1161-gamedata.base) is made `dist` world units (default 300,
        // about 30 m) beside this game's player through the engine's own RootObjectFactory::createRandomSquad 0x582F80, so its
        // members and its first stocking run through the mod's own hooks. [TRADER] spawn / spawn MISSED lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::TraderSpawnCommand(Trim(arg))); finished = true;
    }
    else if (verb == "boxopen")
    {
        // boxopen <n>   - parity3 TEST-ONLY lever: the n nearest non-player boxes are opened one after another through the
        // engine's own loot open (ForgottenGUI::_showTradeWindow TW_LOOTING -> showInventory, whose detour asks the holder),
        // each held until its answer lands (500..3000 ms), then closeAllInventories. [PARITY] boxopen / [PARITY] open lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BoxOpenCommand(Trim(arg))); finished = true;
    }
    else if (verb == "boxtake")
    {
        // boxtake <n> <sid|any>   - loot2c TEST-ONLY lever (T362, docs/design-loot2.md 4A "L2c' show/take"): among the n nearest
        // non-player boxes, the nearest one with a research row for <sid> is opened through the engine's own loot open; once this
        // player's copies are shown, ONE copy of <sid> is moved into this player's first character through the engine's own
        // transfer calls at the K2 safe point (so the drain sees it as a take), then the window is closed. [LOOT2] boxtake lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BoxTakeCommand(Trim(arg))); finished = true;
    }
    else if (verb == "groundtest")
    {
        // inv5 phase 1 TEST lever (.modding/investigations/inv5-ground-items-design.md 6), on this game's first own player character:
        // groundtest list [<r>]               - read-only; one [GROUND] item line per ground item within r (default 30), verdict
        //                                        asked as the ground (kAreaForGround), then [GROUND] list n= dup= fuzzy=.
        // groundtest drop <section> <x> <y>   - drops the item at that slot through the engine's own Inventory::dropItem
        //                                        (vt +0x38 -> CharacterHuman::dropItem 0x5C9CB0), so the real detour fires.
        // groundtest dropsid <sid>            - the same, for the first item of that base sid anywhere in the inventory.
        // groundtest pick <sid> [<r>]         - a PICKUP (TaskType 3) order on the nearest ground item of that sid within r (default 10).
        // groundtest takedelay <s 0..60> [<n 1..8>] - p105g T799 TEST lever: as the area's holder, the next n ground TAKEs
        //                                        another game sends are held s seconds unprocessed, then served (0 = off).
        // groundtest animaldrop <uid> [<sid>|- [<r>]] - ground5 fold 2 TEST lever: the nearest animal this game owns within r (default
        //                                        1000) of character <uid> drops an item it carries (given one <sid> first if it carries
        //                                        nothing) through Inventory::dropItem -> CharacterAnimal::dropItem 0x5C9A10.
        // groundtest overflow <uid> <sid> <n>  - ground5 fold 2 TEST lever: own character <uid> is given up to n single <sid> items through
        //                                        giveItem(dropOnFail) until its full inventory refuses one - the overflow road.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::GroundTestCommand(Trim(arg))); finished = true;
    }
    // PROBE-START: P084
    else if (verb == "captlist")
    {
        // captlist   - PROBE P084 (capt1, T337, .modding/investigations/captivity.md): one [CAPT] char line per character
        // that is in something (bed/cage/pole), wears a LockedArmour in boots/armour, or has a slave state, then a
        // [CAPT] captlist tally. Own characters and copies. Read-only: it creates, moves and sends nothing.
        WriteStatus(coop::CaptListCommand()); finished = true;
    }
    // PROBE-END: P084
    else if (verb == "capturestate")
    {
        // capturestate <uid>   - P11 TEST-ONLY read (items.cpp CaptureStateCommand): one [CAPT] capturestate line - KO, slave
        // state, slave-owner hand and who it names, inSomething, every inventory section with records and shackle locks.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::CaptureStateCommand(Trim(arg))); finished = true;
    }
    else if (verb == "capturetest")
    {
        // capturetest <victimUid> [template] [taskType]   - P11 TEST-ONLY lever (items.cpp CaptureTestArm), on the game that
        // will own the slaver: spawn one slaver (default 'Slaver Guard', 'Slave Traders') by the victim, wait for the victim's
        // KO on this game (cap 90 s), give it taskType (default 182) through AttackLocal, read back 30 s, SUMMARY line.
        // capturetest again <victimUid> [taskType]   - P11 f6: no new spawn - the LAST slaver capturetest spawned (refused if it
        // is gone) gets taskType (default 166, the cuff) after the same KO wait; the same read-back and SUMMARY line.
        // capturetest carry <carrierUid> <bodyUid> | capturetest carried <uid>   - M7b (T761) TEST-ONLY lever (spawn.cpp
        // CarryTestCommand): this game's own character picks up the other game's character's copy (the engine's pickupObject);
        // the body's own game keeps or breaks the carry (MSG_CARRY_BREAK). `carried` reads one character's carry state.
        // capturetest carry <carrierUid> name <name>   - TEST-ONLY: the body picked by name (a down character within 300 units of
        // the carrier, this game's own allowed); the same pickupObject.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::CaptureTestArm(Trim(arg))); finished = true;
    }
    else if (verb == "bedtest")
    {
        // bedtest <carrierUid> <bodyUid | name <name>> [bedKeySubstring] [type=<n>]   - TEST-ONLY lever (spawn.cpp BedTestCommand):
        // this game's own carrier, already carrying the body, is given the engine's put-in-bed order (default task type 70) on the
        // nearest bed with a free slot within 600 units. bedtest list [radius] [nearUid]   - reads only: the beds nearby. [BED] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BedTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "chaintest")
    {
        // chaintest off <uid>   - P11 f6 TEST-ONLY lever (items.cpp ChainTestCommand), on the game that OWNS <uid>: the engine's
        // setChainedMode(<uid>, false, empty hand) through the hooked entry - unchains it and takes its worn shackles off; slave
        // state and owner stay. Refused unless <uid> is this game's own, alive and chained. One [CAPT] chaintest line.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::ChainTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "crimetest")
    {
        // crimetest steal   - TEST-ONLY lever (crime1, docs/design-crime.md s2): this game's first own player-faction character
        // commits a theft against the nearest copy of a non-player character within 500 u, through the engine's own
        // notifyCrimeWitnessed 0x851F40 at the K2 safe point. Witnesses react through their own senses.
        // crimetest stealnear - the same, against the nearest non-player character whoever drives it (crime3: on the game
        // that owns the townspeople this is the positive control - its own witnesses, its own victim).
        // crimetest attacknear <sid> - rel3: the attackplayer attacker attacks the nearest living, conscious townsperson of <sid>.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::CrimeTestArm(Trim(arg))); finished = true;
    }
    else if (verb == "talktest")
    {
        /* talktest [npcUid|near] [targetUid|near] [event]   - TEST-ONLY lever (P26 stage 0, .modding/investigations/p26-npc-dialogue.md):
           at the K2 safe point the engine's own Dialogue::sendEvent 0x683F00 is called on the NPC's Dialogue with the target as
           `who` and the event (default 1, EV_PLAYER_TALK_TO_ME). near target = the other player's nearest character (a copy
           here); near NPC = the nearest living non-player character with a Dialogue. Logs what started; nothing else. */
        std::string arg; std::getline(is, arg);
        /* P25 T729: `talktest nearestname <npc name> <targetUid> [event] [copy | mine]` (P25 T729b) - the NPC picked by name when the command runs
           (speech.cpp TalkTestNameArm; waits up to 60 s for it to load) */
        const std::string ta = Trim(arg);
        const bool byName = (ta == "nearestname" || ta.compare(0, 12, "nearestname ") == 0 || ta.compare(0, 12, "nearestname\t") == 0
                             || ta == "nearestfaction" || ta.compare(0, 15, "nearestfaction ") == 0 || ta.compare(0, 15, "nearestfaction\t") == 0);   /* nearestfaction: the same pick by faction name */
        WriteStatus(byName ? coop::TalkTestNameArm(ta) : coop::TalkTestArm(ta)); finished = true;
    }
    else if (verb == "talksight")
    {
        /* talksight <npcUid | nearest <dialogueName>> <targetUid> [seconds] [walkover]   - TEST-ONLY lever (P26 stage 6, decision 233
           (a), speech.cpp 'TALKSIGHT LEVER'): the NPC (this game's own) notices the target through the engine's own sight entry,
           Dialogue::sendEvent event 3, once per K2 safe point until a conversation starts or the seconds run out. */
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::TalkSightArm(Trim(arg))); finished = true;
    }
    else if (verb == "talkcarriers")
    {
        /* talkcarriers <x> <z> <radius>   - READ-ONLY (P26 stage 6): the NPCs near (x,z) carrying a walk-over dialogue.
           talkcarriers <x> <z> <radius> [ev <n>] [act <type>]   - READ-ONLY, TEST-ONLY: the NPCs near (x,z) whose event-n conversation
           list (default 3, the sight event) holds a line carrying dialogue action <type> (default 2, TALK_TO_LEADER). */
        float x = 0, z = 0, r = 0;
        int ev = -1, act = -1;
        const bool xzr = (is >> x >> z >> r) && r > 0.0f && r <= 5000.0f;
        std::string rest; std::getline(is, rest);
        if (!xzr || !coopsay::TalkCarriersOptParse(rest, &ev, &act))
            WriteStatus("error talkcarriers usage: talkcarriers <x> <z> <radius 1..5000> [ev <0..255>] [act <0..255>]");
        else if (ev < 0 && act < 0) WriteStatus(coop::TalkCarriersCommand(x, z, r));
        else WriteStatus(coop::TalkCarriersActCommand(x, z, r, ev < 0 ? 3 : ev, act < 0 ? 2 : act));
        finished = true;
    }
    else if (verb == "talkprompt")
    {
        /* talkprompt [npcUid|near] [targetUid|near] [lineSid|auto] | talkprompt answer <0..9> | talkprompt close   - TEST-ONLY
           lever (P26 stages 1-3, speech.cpp): at the K2 safe point the engine's own Dialogue::startPlayerConversation 0x683890 is
           called THROUGH its hook (the stage-2 intercept) for an NPC this game owns toward the other player's character (a copy
           here); on the addressed game `answer k` clicks reply k of the mirrored window through the engine's replyClicked entry
           and `close` ends it through the engine's endDialogue entry. */
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::TalkPromptArm(Trim(arg))); finished = true;
    }
    else if (verb == "hiretest")
    {
        /* hiretest near|uid <n> [price]   - TEST-ONLY lever (recruit1 R0-a, docs/design-recruit1.md 5): my first player character
           hires the nearest Drifter within 300 u (or uid n) through the real hire flow, join type 18, default price 100. Armed
           here, run at the K2 safe point (hire.cpp). On the game that runs the person it is the own-game hire.
           hiretest near same [<x> <z> [price]] - recruit2 race: the Drifter nearest to one fixed map point (default The Hub
           -50978,2932; ties to the lower uid), so both games pick the same person in the same step. */
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::HireTestArm(Trim(arg))); finished = true;
    }
    else if (verb == "buildtest")
    {
        // buildtest place <sid> [dx dz] | progress <key-substring> <amount> | dismantle <key-substring>   - TEST-ONLY lever
        // (build1-a, docs/design-build1.md s3): armed here, run at the K2 safe point (build.cpp BuildTestDrain).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BuildTestArm(Trim(arg))); finished = true;
    }
    else if (verb == "standin")
    {
        // standin <slot> [read] - TEST-ONLY lever: make (or take into the table) the stand-in for any player number and read it back
        // (name, placeholder, the loaded buildings it owns); `read` makes nothing.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::StandInCommand(Trim(arg))); finished = true;
    }
    else if (verb == "buildgive")
    {
        // buildgive <key-substring|nearest> <slot> - TEST-ONLY lever: give a loaded building to that player's faction (its stand-in,
        // made as a placeholder if this game has none).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BuildGiveCommand(Trim(arg))); finished = true;
    }
    else if (verb == "buildlist")
    {
        // buildlist - read-only: the build1 registry (position key -> sid, owner, progress) with live values
        WriteStatus(coop::BuildListCommand()); finished = true;
    }
    else if (verb == "nearlist")
    {
        // nearlist <x> <z> <r> [<page>]   - PROBE P068 (B1): every loaded building within r of (x,z), with NO
        // inventory filter, its key / class / position / yaw / distance / sector / held-mine and a door's state.
        // Read-only: it creates nothing, moves nothing and sends nothing.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::NearListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "shoplist")
    {
        // shoplist [<x>,<y>|all]  - P7i: the same walk, filtered by `buytest`'s OWN test for a shop counter (a
        // trader-squad bind at UseableStuff::shopOwner +0x360). T232's `buytest rebirth` missed with
        // keyHits=4 keyHitsNotAShopCounter=4: four buildings matched the needle and not one was a counter.
        // A counter on this list is one that lever will accept. shoplist takes the same argument as boxlist,
        // has the same 64-line cap and page, and is read-only in the same sense.
        /* P8a (F627): the whole rest of the line - see boxlist above. */
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::ShopListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "keeperlist")
    {
        // keeperlist [<x>,<y>|all] [<page>]   - T-164 B4-0 (TEST, read-only): every loaded NPC squad with a home building,
        // one [KEEPER] row per squad and one per indoor piece of that building with an inventory, 64 lines a page like
        // shoplist. Row numbers are what `buytest keeper <n>` takes. items.cpp KeeperListCommand.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::KeeperListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "buildings") { WriteStatus(coop::BuildingsCommand()); finished = true; }
    else if (verb == "towns") { WriteStatus(coop::TownsCommand()); finished = true; }
    else if (verb == "pendnote")   /* TEST-ONLY verb (T-580): pendnote add <townSid> <x> <z> [stale|asleep] | clear | show <townSid> <x> <z> - a test note of a town at a world position in decision 34's pending set (store.cpp StorePendNoteCommand) */
    {
        std::string op, town, kind; float x = 0.0f, z = 0.0f; is >> op;
        if (op != "clear" && !(is >> town >> x >> z)) WriteStatus("error pendnote: add / show need <townSid> <x> <z> (numbers) - nothing added");
        else { if (op == "add") is >> kind; WriteStatus(coop::StorePendNoteCommand(op, town, x, z, kind)); }
        finished = true;
    }
    else if (verb == "storetest")
    {
        // storetest nopos <worldId or part of one> | storetest nopos off   - TEST-ONLY lever: that group's next SLEEP write reads as if
        // none of its members were in the world and its platoon stood at 0,0 (store.cpp StoreTestCommand, RecordPosChoose).
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::StoreTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "townlist") { WriteStatus(coop::TownListCommand()); finished = true; }   // towns1: read-only, every town with name and position
    else if (verb == "ownbuilding") { WriteStatus(coop::OwnBuildingCommand()); finished = true; }
    else if (verb == "jobtest")
    {
        // jobtest work <characterUid> <buildingKey> [plain] [task=<n>]   - P17 TEST-ONLY lever (items.cpp JobTestCommand): this game's
        // own character gets the engine's own giveOrder 0x5D1640 on the first loaded building whose key or instance id contains the
        // text - default task 87 OPERATE_MACHINERY, as a player's order (`plain` = without the player-order argument).
        // jobtest input <buildingKey> <itemSid> <n> [section=<name>]   - TEST-ONLY: n of the item into that machine's input section
        // (boxtest's path, the section whose limit list names the item unless one is named). jobtest show <buildingKey> - read-only.
        // Each prints the machine's name, product, inputs and sections. [JOB] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::JobTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "cagetest")
    {
        // cagetest <copyUid> nearest   - P43 TEST-ONLY lever (spawn.cpp CageTestCommand): this game's engine cages the other game's
        // character's copy in the nearest free cage at the K2 safe point (setPrisonMode, as a guard's task); PrisonWatchCopies then
        // sends the owner MSG_PRISON IN as for a guard's caging. [ARREST] cagetest lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::CageTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "treattest")
    {
        // treattest <copyUid> <part|all> <bandage 0..1>   - TEST-ONLY lever (spawn.cpp TreatTestCommand): the other game's character's
        // copy's bandage values are raised at the K2 safe point as a medic's work does; TreatTick then sends the owner MSG_TREAT.
        // [HEAL] treattest lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::TreatTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "bailtest")
    {
        // bailtest <copyUid>   - TEST-ONLY lever (spawn.cpp BailTestCommand): the bail confirm's own steps for one caged copy of
        // another game's character at the K2 safe point (no money taken), then its owner is told as after a real bail. [PRISON] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BailTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "slavetest")
    {
        // slavetest <uid> <state 0..3>   - TEST-ONLY lever (spawn.cpp SlaveTestCommand): the engine's setSlaveState on that
        // character as a task would call it; on a copy the owner is asked when this game runs its area. [SLAVE] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::SlaveTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "locktest")
    {
        // locktest read <uid> | locktest open|close <uid> shackles|cage | locktest escape <uid>   - P42 TEST-ONLY lever (spawn.cpp
        // LockTestCommand): read a prisoner's shackle and cage locks here (and the owner's word known here); open or close one as a
        // pick's result at the K2 safe point (on a copy: a rescuer's pick, asked of the owner); order our caged character to escape
        // (GET_OUT_OF_CAGE_ESCAPE). [LOCK] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::LockTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "buyhouse")
    {
        // buyhouse <buildingKey|nearest> [cats=<n>] | buyhouse show <buildingKey|last>   - P18 fold 1 (TEST LEVER): the REAL purchase (the engine's
        // own buy callback - the price is taken), or a read of the house's owner, doors and residents. build.cpp BuyHouseCommand.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::BuyHouseCommand(Trim(arg))); finished = true;
    }
    else if (verb == "doortest")
    {
        // doortest open|close|lock|npclock|show <nearest|last|key> | doortest show held   - T-160 (TEST-ONLY): this game moves one door
        // the way its own world would (openDoor / closeDoor / the lock button / the NPC lock action), or reads one; doors.cpp
        // DoorTestCommand. [DOORTEST] lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::DoorTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "townrepop")
    {
        // townrepop on|off|show   - P18 fold 2 (TEST LEVER): while on, every town the engine asks about is rebuilt by its own path
        // (worldgen.cpp TownRepopCommand) - a zone that loads then runs the town rebuild that drops saved buildings.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::TownRepopCommand(Trim(arg))); finished = true;
    }
    else if (verb == "towngen")
    {
        /* DECISION 48 (P8a): `towngen client` IS GONE.  The verb keeps its on/off lever and its report; the sub-verb
           that declared a game a client for the rest of the process is refused by name, with the reason, so a plan
           or harness still sending it gets an error that says what happened rather than a silent no-op. */
        std::string mode; is >> mode;
        if (mode == "client") WriteStatus("error towngen client: RETIRED by decision 48 - the game that holds the area generates its town's people; there is no client declaration");
        else if (mode != "on" && mode != "off") WriteStatus("error towngen: on|off");
        else { coop::SetTownGenOn(mode == "on"); WriteStatus("ok towngen"); }
        finished = true;
    }
    else if (verb == "speedvote")
    {
        // speedvote <speed> - E40 / decision 45. This player's OWN setting, as a vote: 0 pauses, any pace up to
        // coopclock::kClockSpeedMax (the buttons' 1, 2, 5, or a speed mod's 3, 10 ...) is accepted. Under `timemode
        // consensus` the slowest vote wins and 0 pauses the whole world; under `timemode fixed` only the host's
        // vote counts and a client's is recorded and ignored. It does NOT set this game's speed - the notebook's
        // broadcast does that, on every game at once - so `speedvote 0` followed by a report is how a run
        // measures a deliberate pause without a human touching the keyboard (the engine ignores injected input,
        // F010, so there is no other way to press the key).
        std::string n; is >> n;
        if (n.empty()) WriteStatus("error speedvote: <speed> (0 = pause, up to 50)");
        else if (!coop::SpeedVoteCommand((float)atof(n.c_str()))) WriteStatus("error speedvote: refused (0 = pause, or a pace above 0 up to 50)");
        else WriteStatus(std::string("ok speedvote ") + n);
        finished = true;
    }
    else if (verb == "gamespeed")
    {
        // gamespeed <x> - a test-only lever. Writes x straight into the engine's speed global, as a speed mod's own
        // code does: no button, no key, no setter call, no vote. The mod's clock tick then sees a change it did not
        // make and takes it as this player's vote. x is any number 0..1000, so a run can also write a speed the mod
        // refuses (above 50). `gamespeed sweep <from> <to> <step> <everyMs>` repeats the write from the clock tick every
        // everyMs (16..5000) from `from` to `to`, at most 100 writes - a speed mod's slider.
        std::string n; is >> n;
        if (n == "sweep")
        {
            float a = -1.0f, z = -1.0f, st = 0.0f; unsigned int ms = 0;
            is >> a >> z >> st >> ms;
            const float steps = (st != 0.0f) ? (z - a) / st : -1.0f;
            if (!(a >= 0.0f && a <= 1000.0f && z >= 0.0f && z <= 1000.0f) || !(steps >= 0.0f && steps <= 99.0f) || ms < 16 || ms > 5000)
                WriteStatus("error gamespeed sweep: <from 0..1000> <to 0..1000> <step towards to> <everyMs 16..5000>, at most 100 writes");
            else if (coop::GameSpeedTestSweep(a, z, st, ms) != 0) WriteStatus("error gamespeed: no speed global on this build");
            else WriteStatus("ok gamespeed sweep");
            finished = true;
        }
        const float x = n.empty() ? -1.0f : (float)atof(n.c_str());
        float was = -1.0f; int rc = 0;
        if (finished) {}
        else if (n.empty() || !(x >= 0.0f && x <= 1000.0f)) WriteStatus("error gamespeed: <x> (0..1000)");
        else if ((rc = coop::GameSpeedTestWrite(x, &was)) != 0) WriteStatus(rc == -1 ? "error gamespeed: no speed global on this build" : "error gamespeed: the write faulted");
        else { std::ostringstream o; o << "ok gamespeed " << n << " was " << was; WriteStatus(o.str()); }
        finished = true;
    }
    else if (verb == "timemode")
    {
        // timemode [fixed|consensus] - E40 / decision 45. The hosting option that decides whose speed setting
        // sets the world's pace. Kept by the notebook process in options.txt beside `basepolicy`, accepted only
        // from the game the notebook calls the session authority, and handed back to every game - so a value
        // takes effect here only after it has been stored there, and there is one enforcement point that
        // outlives any single game. With no argument this reports what the notebook's CLOCK last said.
        std::string mode; is >> mode;
        if (mode.empty()) WriteStatus(std::string("ok timemode ") + coop::TimeModeName());
        else if (mode != "fixed" && mode != "consensus") WriteStatus("error timemode: fixed|consensus");
        else if (!coop::StoreSendOption("timemode", mode)) WriteStatus("error timemode: no notebook link - the option is kept by SharedWastelandsServer.exe, not by this game");
        else WriteStatus("ok timemode sent");
        finished = true;
    }
    else if (verb == "refill")
    {
        // refill [now <town stringID or name>]   - refill1 (T374, docs/design-refill1.md s4; TEST-ONLY): with no argument the
        // [REFILL] counters. `refill now <town>` marks that town due at its next check-up on the game that holds it: the
        // 5-day wait is skipped, the nearby-player rule is NOT. Never used in play.
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::RefillCommand(rest)); finished = true;
    }
    else if (verb == "townres" || verb == "towns2")
    {
        // townres [now <town stringID or name> [nosight] | kill <town> <n>]   - towns2 (TEST-ONLY): with no argument the
        // [TOWNS2] counters. `now` skips one town's 5-day wait on the game that holds it (nosight: the sight rule is ignored
        // for that walk); `kill` kills n living members of that town's first eligible resident group (one is always left).
        // `towns2` is the same verb; `townres` exists because the cloud watcher's extra-command verbs are letters only.
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::Towns2Command(rest)); finished = true;
    }
    else if (verb == "recruitmult")
    {
        // recruitmult [auto|1|2|3|4]   - recruit3 (TEST-ONLY lever for the host option `recruitmult`): with no argument it
        // reports this game's option, the player slots seen and the multiplier it would apply. With one it asks the
        // notebook to store it for the world - accepted only from the session authority, like `basepolicy`.
        std::string v; is >> v;
        if (v.empty()) WriteStatus("ok recruitmult " + coop::RecruitMultStatus());
        else if (!coopr::RecruitMultValueOk(v)) WriteStatus("error recruitmult: auto|1|2|3|4");
        else if (!coop::StoreSendOption("recruitmult", v)) WriteStatus("error recruitmult: no notebook link - the option is kept by SharedWastelandsServer.exe, not by this game");
        else WriteStatus("ok recruitmult sent");
        finished = true;
    }
    else if (verb == "recruitlist")
    {
        // recruitlist <x> <z> <r>   - recruit3, READ-ONLY: every loaded character within r of (x,z), counted by faction
        // (Drifters first). Also printed as `[RECRUIT] list ...`. It creates, moves and sends nothing.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::RecruitListCommand(Trim(arg))); finished = true;
    }
    else if (verb == "basepolicy")
    {
        // basepolicy [shared|owner|locked] - E36 / decision 40. With NO argument it reports what this game has
        // been told. With one it asks the notebook process to store it for the whole world; the notebook
        // accepts that only from the game it calls the session authority, and hands the stored map back to
        // every game - so a value only ever takes effect here after it has been stored there. The refusal is
        // the notebook's to make and to log, deliberately: one enforcement point, and it is the one that
        // outlives any single game.
        std::string mode; is >> mode;
        if (mode.empty()) WriteStatus(std::string("ok basepolicy ") + coop::BasePolicyName());
        else if (mode != "shared" && mode != "owner" && mode != "locked") WriteStatus("error basepolicy: shared|owner|locked");
        else if (!coop::StoreSendOption("basepolicy", mode)) WriteStatus("error basepolicy: no notebook link - the option is kept by SharedWastelandsServer.exe, not by this game");
        else WriteStatus("ok basepolicy sent");
        finished = true;
    }
    else if (verb == "heartbeat")
    {
        // heartbeat on|off - E47-S2: publish this game's owned AWAKE world groups every 30 s and ~2 s after an item move
        // or a wake, instead of only when they fall asleep (F660). Default OFF for the first run (decision D8).
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error heartbeat usage"); }
        else { coop::SetHeartbeatOn(mode == "on"); WriteStatus("ok heartbeat"); }
        finished = true;
    }
    else if (verb == "zonestate")
    {
        // zonestate on|off - piece 3a: capture zone files on write, override zone loads from records.
        // Default ON since AUD 2026-09-17 (`zonestate off` is the control); the client sweeps for its own
        // player-faction characters through it, so its squad is only announced while this is on (F474).
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error zonestate usage"); }
        else { coop::SetZoneStateOn(mode == "on"); WriteStatus("ok zonestate"); }
        finished = true;
    }
    else if (verb == "relations")
    {
        // relations on|off - P3 piece 2: forward/apply faction standings. Default ON.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error relations usage"); }
        else { coop::SetRelationsOn(mode == "on"); WriteStatus("ok relations"); }
        finished = true;
    }
    else if (verb == "peace")
    {
        // peace on|off - E42, a DEVELOPER-ONLY lever (user approval 2026-09-05). It makes every faction
        // answer "not an enemy, and an ally" about any character in a player faction, and every
        // player-faction character answer the same about everyone else, by intercepting the two
        // decisions the ENGINE asks (Character::isEnemyOf / isAllyOf). It changes NO stored
        // relation value: nothing goes on the wire, nothing reaches the save, and relations.cpp is
        // neither consulted nor disturbed. Default OFF; both games run it locally.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error peace usage: peace on|off"); }
        else { coop::SetPeaceOn(mode == "on"); WriteStatus("ok peace"); }
        finished = true;
    }
    else if (verb == "store")
    {
        // store on|off   - P1 persistence: write/apply sleeping records. Default ON.
        // store server <addr> <port>   - P2: connect to SharedWastelandsServer.exe (port 0 = back to the in-game store)
        std::string mode; is >> mode;
        if (mode == "server")
        {
            std::string addr; unsigned int port = 0; is >> addr >> port;
            const bool ok = coop::SetStoreServer(port ? addr : std::string(), (unsigned short)port);
            /* E38 / decision 43: pointing a game at a notebook process is a declaration that this is a co-op
               world - the folder it is about to read and write is shared. If the file said `single` (or there was
               no file), the role moves to `host`: a game with a notebook link and no session is the only game in
               that world, which is what the host role means everywhere else in this build. A `join` that follows
               overrides it back to client. */
            if (ok && port != 0 && coop::RoleIsSingle()) coop::ConfigSetRole(coop::kRoleHost, "the `store server` verb - a notebook link means a co-op world");
            WriteStatus(ok ? "ok store server" : "error store server");
        }
        else if (mode != "on" && mode != "off") { WriteStatus("error store usage"); }
        else { coop::SetStoreOn(mode == "on"); WriteStatus("ok store"); }
        finished = true;
    }
    else if (verb == "orderdrive")
    {
        // orderdrive on|off   - H021. Default ON since AUD 2026-09-17 - copies walk by move orders;
        // `orderdrive off` is the straight-push control. See replicate.h.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error orderdrive usage"); }
        else { coop::SetOrderDrive(mode == "on"); WriteStatus("ok orderdrive"); }
        finished = true;
    }
    else if (verb == "pathdrive")
    {
        // pathdrive on|off   - D1 (read-movement). Default ON (D1-c) - a copy far from its aim walks by the engine's own
        // pathfinder (CharMovement::setDestination); `pathdrive off` = the push + H024 unstick. See replicate.h.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error pathdrive usage"); }
        else { coop::SetPathDrive(mode == "on"); WriteStatus("ok pathdrive"); }
        finished = true;
    }
    else if (verb == "handoff")
    {
        // handoff auto <n>   - H019 / M-C: hand up to n puppets their authority's current goal as an
        // order (alternating inject / control) and score the copy's first free pass ([VERDICT] handoff).
        std::string mode; int n = 0; is >> mode >> n;
        if (mode != "auto" || n <= 0) { WriteStatus("error handoff usage: handoff auto <n>"); }
        else { std::ostringstream o; o << "ok handoff " << coop::HandoffAuto(n); WriteStatus(o.str()); }
        finished = true;
    }
    else if (verb == "nativecombat")
    {
        // nativecombat on|off   - H015/P059. See combat.h. Default ON since combat1 (user decision 2026-09-25).
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error nativecombat usage"); }
        else { coop::SetNativeCombat(mode == "on"); WriteStatus("ok nativecombat"); }
        finished = true;
    }
    else if (verb == "taskinstall")
    {
        // taskinstall on|off - F384 / P055. **DEFAULT OFF.** Installs a TASK_MELEE_FOCUSED Task
        // directly as the puppet's current action, with ZERO AI decision passes - the fifth attempt
        // at P-15, and the first that does not route through the thing that broke the other four.
        // Off restores the pre-F384 behaviour so the run can measure it against its own absence.
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean OFF.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error taskinstall usage"); }
        else { coop::SetTaskInstall(mode == "on"); WriteStatus("ok taskinstall"); }
        finished = true;
    }
    else if (verb == "taskweapon")
    {
        // taskweapon on|off - P057 / F394 / F407. **DEFAULT OFF.** Hands a puppet with empty hands
        // the engine's own TASK_DRAW_WEAPON task, which does all its work synchronously at install
        // time, needing no AI pass and no per-frame driver.
        //
        // **IT USED TO DO A SECOND THING - EVICT THE REQUIREMENTS MEMO AT AI+0x238 - AND THAT IS
        // DELETED, NOT DISABLED (F407).** The eviction was written because the cached refusal was
        // believed to "answer forever". **T101 measured `evaluated=100 servedFromCache=28` of
        // `attempts=128`: 78% of attempts evaluated the requirements FRESH and refused anyway**, so
        // it was clearing a cache the majority path never consulted - while racing an ungated
        // worker-thread writer into the same red-black tree (F401).
        //
        // Separate from `taskinstall` on purpose, so the arms are distinguishable in the log rather
        // than inferred: (off,off) control, (on,off) does the weapon get drawn at all.
        // **(on,on) REMAINS BARRED** - not for the F401 reason, which is now gone, but for the
        // transient-`Tasker` double-free: one apply would install the equip task and microseconds
        // later end and delete it to install the melee task, in a field the worker thread reads
        // every frame. **Sequencing those onto different ticks is the next build, not this one.**
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean OFF.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error taskweapon usage"); }
        else { coop::SetWeaponTask(mode == "on"); WriteStatus("ok taskweapon"); }
        finished = true;
    }
    else if (verb == "swingrepl")
    {
        // swingrepl on|off - F348. Default ON. The peer is told when a blow starts and writes the
        // engine's own three fields to begin it; the AI decision gate is untouched either way.
        // Off restores the pre-F348 behaviour so a run can measure the fix against its absence.
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean ON.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error swingrepl usage"); }
        else { coop::SetSwingReplication(mode == "on"); WriteStatus("ok swingrepl"); }
        finished = true;
    }
    else if (verb == "combatpulse")
    {
        // combatpulse on|off  - F233. Default OFF (combat.cpp: g_combatPulse = false - the measured
        // variant failed and was left switchable; the comment here said ON and was corrected
        // AUD 2026-09-17). Exists so a run can measure the fix against its own absence without a rebuild.
        // STRICT since AUD 2026-09-17: a bare or mistyped mode used to mean ON.
        std::string mode; is >> mode;
        if (mode != "on" && mode != "off") { WriteStatus("error combatpulse usage"); }
        else { coop::SetCombatPulse(mode == "on"); WriteStatus("ok combatpulse"); }
        finished = true;
    }
    else if (verb == "puppetai")
    {
        // puppetai <uid> on|off   - DIAGNOSTIC ONLY. Turn one puppet's decision gate off, or
        // back on, on THIS instance.
        //
        // Why it exists (T068/F226): the peer now enters combat mode, but its combat state
        // machine never reaches any action state - 70 transitions, all of them alternating
        // between two states, zero swings, while the authority produced 639 transitions across
        // eight states including 75 swings. The leading explanation is that the puppet's
        // decisions are gated off, and Kenshi's AI combat class is exactly the thing that would
        // need a decision to choose an attack.
        //
        // That explanation is CHEAP TO TEST and expensive to assume, which is the whole of
        // lesson 16. This command tests it: ungate one puppet mid-fight and see whether swings
        // appear in the [M6] state log.
        //
        // It is NOT a fix and must not become one by default. An ungated puppet is a
        // free-running clone again, which is the defect F152 exists to prevent.
        unsigned int uid = 0; std::string mode;
        is >> uid >> mode;
        ::Character* c = (uid != 0) ? coop::FindSpawned(uid) : 0;
        if (c == 0)
        {
            WriteStatus("err puppetai: unknown uid");
            ErrorLog("[P028] puppetai refused: no local character for uid " + std::string(mode));
        }
        else if (mode != "on" && mode != "off") { WriteStatus("error puppetai usage"); }   /* AUD-b: STRICT - a bare or mistyped mode used to mean ON */
        else
        {
            bool on = (mode == "on");
            coop::SuppressCharacter(c, on);
            WriteStatus("ok puppetai");
            DebugLog("[P028] puppetai uid set: decisions now "
                     + std::string(on ? "SUPPRESSED" : "LIVE")
                     + " - DIAGNOSTIC, an ungated puppet is a free-running clone (F152)");
        }
        finished = true;
    }
    else if (verb == "attack")
    {
        // attack <attackerUid> <victimUid> [taskType]   - LOCAL, no session needed.
        // Default 5 = TASK_MELEE_FOCUSED (a deliberate duel rather than opportunistic).
        unsigned int a = 0, v = 0; int type = 5;
        is >> a >> v;
        if (!(is >> type)) type = 5;
        if (a == 0 || v == 0)
        {
            DebugLog("[cmd] usage: attack <attackerUid> <victimUid> [taskType]"
                     "   (4=MELEE_ATTACK, 5=TASK_MELEE_FOCUSED, 61=UNPROVOKED_FOCUSED)");
            WriteStatus("error attack usage");
        }
        else
        {
            bool ok = coop::AttackLocal(a, v, type);
            WriteStatus(ok ? "ok attack" : "error attack");
        }
        finished = true;
    }
    else if (verb == "tasks")
    {
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: tasks <uid>");
            WriteStatus("error tasks usage");
        }
        else
        {
            coop::ReportTask(uid);
            WriteStatus("ok tasks");
        }
        finished = true;
    }
    else if (verb == "spawnfaction")
    {
        // spawnfaction <name>   - subsequent spawns join that faction (name may have spaces)
        // spawnfaction          - clear, back to inheriting the reference character's faction
        std::string rest;
        std::getline(is, rest);
        rest = Trim(rest);
        bool ok = coop::SetSpawnFaction(rest);
        WriteStatus(ok ? "ok spawnfaction" : "error spawnfaction");
        finished = true;
    }
    else if (verb == "wound")
    {
        // wound <uid> <part> <stun> [cut]  - TEST HARNESS: damage one part on a character we
        // own, so the KO question can be asked deterministically instead of by lottery.
        unsigned int uid = 0; int part = -1; double stun = 0, cut = 0;
        is >> uid >> part >> stun;
        if (!(is >> cut)) cut = 0;
        if (uid == 0 || part < 0)
        {
            DebugLog("[cmd] usage: wound <uid> <part> <stun> [cut]");
            WriteStatus("error wound usage");
        }
        else
        {
            bool ok = coop::WoundPart(uid, part, (float)stun, (float)cut);
            WriteStatus(ok ? "ok wound" : "error wound");
        }
        finished = true;
    }
    else if (verb == "mirrorcap")
    {
        // mirrorcap <n>   - T-354 TEST-ONLY lever (spawn.cpp MirrorTestCapLever): the character table refuses new rows while n or
        // more are occupied (0 = off), so the full path - and the NOT_SHOWN report to another game's owner - runs without
        // filling 4096 rows. [M1] mirrorcap / uid mirror FULL / NOT_SHOWN lines; the [M1] REPORT's mirrorTestCap.
        int cap = -1;
        if (!(is >> cap) || cap < 0)
        {
            DebugLog("[cmd] usage: mirrorcap <rows, 0 = off>");
            WriteStatus("error mirrorcap usage");
        }
        else WriteStatus(coop::MirrorTestCapLever(cap));
        finished = true;
    }
    else if (verb == "p113squad")
    {
        // p113squad <cycles 1..50> [awake|sleep|alt] [sourceWorldId] | report | stop  - T-341 TEST-ONLY lever (store.cpp, PROBE P113):
        // per cycle the store builds a sleeping squad 25 m from GetTarget from a record (CreateUnknownSquad), reloads it once (LoadPod),
        // waits for the engine's wake (60 s ceiling), then Faction::removeSquad removes it (awake, or after the engine sleep pair),
        // and waits until P113 saw its records destroyed. [P113L] lines; [P113] destroy / SECOND DESTROY / SHARED HEAD lines.
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::P113SquadLever(rest));
        finished = true;
    }
    else if (verb == "eatbite")
    {
        // eatbite <eaterUid | animal> <victimUid> <seconds 1..120>  - T-327 TEST-ONLY lever (effect.cpp EatBiteLever): once a frame at
        // the K2 safe point, victim->gettingEaten(1.0, eater) THROUGH the hooked entry, until <seconds> pass or the call answers 1.
        // `animal` = the nearest living, standing animal this game drives within 100 m of the victim. [EFFECT] eatbite lines.
        std::string eaterArg;
        unsigned int victim = 0;
        double seconds = 0;
        if (!(is >> eaterArg >> victim >> seconds))
        {
            DebugLog("[cmd] usage: eatbite <eaterUid | animal> <victimUid> <seconds 1..120>");
            WriteStatus("error eatbite usage");
        }
        else WriteStatus(coop::EatBiteLever(eaterArg, victim, (float)seconds));
        finished = true;
    }
    else if (verb == "limbtest")
    {
        // limbtest cut <ownUid> <limb 0-3> | limbtest fit <ownUid> <limb 0-3> <robotLimbSid> | limbtest show <uid>  - TEST-ONLY
        // lever (clothing.cpp LimbTestLever). cut: this game's own character loses that limb through MedicalSystem::amputate with
        // the severed limb item, as combat does; fit: a robotic limb item made from the sid is fitted on that stump through
        // MedicalSystem::setRobotLimbItem, as a saved fitted limb is loaded. Both run at the K2 safe point. show: any
        // character's four limbs, read now. Limb 0 left arm, 1 right arm, 2 left leg, 3 right leg. [LIMB] limbtest lines.
        std::string rest; std::getline(is, rest);
        WriteStatus(coop::LimbTestLever(rest));
        finished = true;
    }
    else if (verb == "sneaktest")
    {
        // sneaktest <ownUid> on|off   - P40 TEST-ONLY lever (spawn.cpp SneakTestCommand): this game's own character in or out of
        // sneaking through the engine's Character::setStealthMode, as the sneak button; [SNEAK] sneaktest lines.
        std::string arg; std::getline(is, arg);
        WriteStatus(coop::SneakTestCommand(Trim(arg))); finished = true;
    }
    else if (verb == "koself")
    {
        // koself <uid> [seconds]  - P11 fold 1 TEST-ONLY lever (appearance.cpp KoSelfLever): on the game that DRIVES <uid>,
        // knock it out with the engine's own knockout (medical.cpp KnockOutOwned) and push its STATE; seconds (0..600, default
        // 0 = the engine's own clock) holds the wake-up clock at least that long. [P11] koself lines: the call, then a check
        // one tick and 2 s later (prone / unconcious / took). T652: `wound` did not knock the character out.
        // koself name <name> [seconds]  - TEST-ONLY (the carried hand-in fixture): the uid is the nearest own, living, non-player
        // character with that name ('_' = a space) within 300 units of this game's first player character (speech.cpp
        // LeverNearestNamed); then the same KoSelfLever.
        std::string rest; std::getline(is, rest);
        std::string nameKey; int nameSeconds = 0;
        if (coopsay::KoSelfNameParse(rest, &nameKey, &nameSeconds))
        {
            unsigned int nu = 0; float nd = -1.0f; std::string nn, why;
            if (coop::LeverNearestNamed(nameKey, coop::kLeverPickOwnLiving, 0, 300.0f, &nu, &nd, &nn, &why) == 0)
            {
                DebugLog("[P11] koself name '" + nameKey + "' refused: " + why);
                WriteStatus("error koself name: " + why);
            }
            else
            {
                std::ostringstream pk;
                pk << "[P11] koself name '" << nameKey << "' -> uid=" << nu << " '" << nn << "' dist=" << (int)nd;
                DebugLog(pk.str());
                WriteStatus(coop::KoSelfLever(nu, (float)nameSeconds));
            }
            finished = true;
        }
        else
        {
            std::istringstream ks(rest);
            unsigned int uid = 0; double seconds = 0;
            ks >> uid;
            if (!(ks >> seconds)) seconds = 0;
            if (uid == 0 || seconds < 0 || seconds > 600)
            {
                DebugLog("[cmd] usage: koself <uid> [seconds 0..600] | koself name <name, '_' = a space> [seconds 0..600]");
                WriteStatus("error koself usage");
            }
            else WriteStatus(coop::KoSelfLever(uid, (float)seconds));
            finished = true;
        }
    }
    else if (verb == "bodytest")
    {
        // bodytest pending <copyUid>  - TEST-ONLY lever (appearance.cpp BodyTestLever), on the game that holds the COPY: at the
        // end of the copy's next get-up blend the appearance's rebuild-pending byte is set inside the engine's ragdoll pass, so
        // the blend's last update meets a queued body rebuild; the copy body-rebuild guard must hold it. [BODYTEST] lines.
        std::string sub; unsigned int uid = 0;
        is >> sub >> uid;
        WriteStatus(coop::BodyTestLever(sub, uid));
        finished = true;
    }
    else if (verb == "looktest")
    {
        // looktest <uid>  - TEST-ONLY lever (appearance.cpp LookTestLever): on the game that DRIVES <uid>, one value of its
        // appearance record alternates between its original + 0.1 and its original, and the new APPEARANCE and CLOTHING are sent
        // to the other games at once; this game's own apply waits until the character stands out of ragdoll. [LOOK] lines.
        // looktest restore <uid>  - on the owner: the original value back, sent the same way.
        // looktest show <uid>  - the read half, any game: the character's look as this game shows it, read now.
        std::string first; unsigned int uid = 0;
        is >> first;
        const bool show = (first == "show");
        const bool restore = (first == "restore");
        if (show || restore) is >> uid;
        else uid = (unsigned int)std::strtoul(first.c_str(), 0, 10);
        if (uid == 0)
        {
            DebugLog("[cmd] usage: looktest <uid> | looktest restore <uid> | looktest show <uid>");
            WriteStatus("error looktest usage");
        }
        else WriteStatus(show ? coop::LookTestShow(uid) : coop::LookTestLever(uid, restore));
        finished = true;
    }
    else if (verb == "ko")
    {
        // ko <uid>  - M4 state readout: PoseState, hasDied, blood, bleed rate, ownership
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: ko <uid>");
            WriteStatus("error ko usage");
        }
        else
        {
            coop::ReportKO(uid);
            WriteStatus("ok ko");
        }
        finished = true;
    }
    else if (verb == "health")
    {
        // health <uid>  - per-limb flesh/max/percent plus blood, one diffable line
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: health <uid>");
            WriteStatus("error health usage");
        }
        else
        {
            coop::ReportHealth(uid);
            WriteStatus("ok health");
        }
        finished = true;
    }
    else if (verb == "look")
    {
        // look <uid>  - P019, the APPEARANCE of a replicated character: sex, race, body
        // mesh, hair, beard, physique, height, attachment count. F153: this is the layer
        // where the two instances were building different people, and until now nothing
        // in this project could read it.
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: look <uid>");
            WriteStatus("error look usage");
        }
        else
        {
            coop::ReportAppearance(uid);
            WriteStatus("ok look");
        }
        finished = true;
    }
    else if (verb == "watchtask")
    {
        // watchtask <uid> [seconds]  - P011, diagnostic: log every goal/hasPendingOrders CHANGE
        unsigned int uid = 0; int secs = 60;
        is >> uid;
        if (!(is >> secs)) secs = 60;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: watchtask <uid> [seconds]");
            WriteStatus("error watchtask usage");
        }
        else
        {
            coop::WatchTask(uid, secs);
            WriteStatus("ok watchtask");
        }
        finished = true;
    }
    else if (verb == "spawncontainer")
    {
        // spawncontainer 1  - explicit-faction spawns keep the reference platoon (default)
        // spawncontainer 0  - explicit-faction spawns pass a null container (F103 original)
        // Only affects spawns that also have an explicit faction set.
        int keep = 1;
        if (!(is >> keep)) keep = 1;
        coop::SetSpawnContainer(keep != 0);
        WriteStatus(keep != 0 ? "ok spawncontainer keep" : "ok spawncontainer null");
        finished = true;
    }
    else if (verb == "factions")
    {
        std::string filter; int n = 30;
        is >> filter;
        if (!(is >> n)) n = 30;
        if (filter == "\"\"" || filter == "*") filter.clear();
        coop::ListFactions(filter, n);
        WriteStatus("ok factions");
        finished = true;
    }
    else if (verb == "handle")
    {
        unsigned int uid = 0;
        is >> uid;
        if (uid == 0)
        {
            DebugLog("[cmd] usage: handle <uid>   (P009 diagnostic, H006)");
            WriteStatus("error handle usage");
        }
        else
        {
            coop::ReportHandle(uid);
            WriteStatus("ok handle");
        }
        finished = true;
    }
    else if (verb == "templates")
    {
        // templates            - first 20 of everything
        // templates <filter>   - substring match, up to 20
        // templates <filter> N - substring match, up to N
        // A quoted "" is NOT a wildcard: it arrives as a literal two-character filter
        // (T018 hit this). Omit the token entirely to list unfiltered.
        std::string filter; int n = 20;
        is >> filter;
        if (!(is >> n)) n = 20;
        if (filter == "\"\"" || filter == "*") filter.clear();   // forgive the obvious guesses
        coop::ListTemplates(filter, n);
        WriteStatus("ok templates");
        finished = true;
    }
    else if (verb == "spawn" || verb == "spawnlocal")
    {
        // spawn <template name> [dx] [dz]   - replicates if a link is up
        // spawnlocal <...>                  - never replicates (isolates the local path
        //                                     from the network path when diagnosing)
        //
        // Template names CONTAIN SPACES ("Lord Shiro", "Heavy Thrall"), so the name is
        // everything up to the optional trailing numbers rather than one token - T018
        // found that a token-only parse silently made most of the game unreachable.
        std::string rest;
        std::getline(is, rest);
        rest = Trim(rest);

        double dx = 3.0, dz = 0.0;
        std::string tmpl = rest;

        // Peel up to two trailing numeric tokens off the end; whatever remains is the name.
        for (int peel = 0; peel < 2; ++peel)
        {
            std::string::size_type sp = tmpl.find_last_of(' ');
            if (sp == std::string::npos) break;
            std::string tail = tmpl.substr(sp + 1);
            std::istringstream ts(tail);
            double v = 0;
            char leftover = 0;
            if (!(ts >> v) || (ts >> leftover)) break;   // not purely numeric - stop peeling
            if (peel == 0) dz = v; else dx = v;
            tmpl = Trim(tmpl.substr(0, sp));
        }
        // One trailing number means dx, not dz (the common "spawn X 4" case).
        if (dx == 3.0 && dz != 0.0) { dx = dz; dz = 0.0; }

        if (tmpl.empty())
        {
            DebugLog("[cmd] usage: spawn <template name> [dx] [dz]  (name may contain spaces)");
            WriteStatus("error spawn usage");
        }
        else
        {
            bool ok = coop::SpawnTemplate(tmpl, (float)dx, (float)dz, 0, verb == "spawn");
            WriteStatus(ok ? "ok spawn" : "error spawn");
        }
        finished = true;
    }
    else if (verb == "aifreeze")
    {
        int v = 0;
        is >> v;
        bool ok = coop::SetWorldAIFreeze(v != 0);
        WriteStatus(ok ? "ok aifreeze" : "error aifreeze");
        finished = true;
    }
    else if (verb == "release")
    {
        coop::ReleaseSuppression();
        WriteStatus("ok release");
        finished = true;
    }
    else
    {
        DebugLog("[cmd] unknown command verb: " + verb);
        WriteStatus("error unknown-verb " + verb);
        finished = true;
    }
    }   /* batch2: end of the second half of the verb chain */

    if (finished)
    {
        // F222: the latch is `g_lastRaw`, set when the line is READ, not here. `g_lastExecuted`
        // used to be set here and tested there, and after the nonce was introduced it was
        // testing a value the nonce had already been stripped out of - a variable that looked
        // like it guarded duplicates and did not. Removed rather than left to mislead.
        s_pending.clear();
    }
}

int StoreSaveRequestCode(const void* saveMgr);   /* store.cpp (store.h): SaveManager+0xA0, -1 unreadable */
/* The engine's pending request (SaveManager+0xA0) as it stands now - the title pump reads it to tell whether a world is on its way. */
int SaveRequestCodeNow()
{
    SaveManager* sm = SaveManager::getSingleton();
    return sm == 0 ? -1 : StoreSaveRequestCode(sm);
}
/* T-201 PP6' fold (item 6): CAN THE LOAD BE POSTED NOW? 1 = yes (SaveManager+0xA0 reads 0); 0 = a request is pending (+0xA0 != 0 -
   readable, the engine returns it to 0 when that request is done, so the caller waits on it: coopui::LoadPostStep); -1 = never on this
   title (no SaveManager, anySavesExist() false, or +0xA0 unreadable - none of which a wait changes). MAIN THREAD, title pump. */
int LoadPostReady()
{
    SaveManager* sm = SaveManager::getSingleton();
    if (sm == 0 || !sm->anySavesExist()) return -1;
    const int code = StoreSaveRequestCode(sm);
    return code == 0 ? 1 : code < 0 ? -1 : 0;
}
/* T-201 PP6': THE AUTOMATIC LOAD'S POST - SaveManager::load(name), then +0xA0 read back: load() posts NOTHING (no screen, no log)
   when a request is already pending or the engine's busy flag is set (autoload-engine-read Q2), so "posted" is the code, not the call.
   1 = posted (code 2); 0 = not tried, a request is pending (LoadPostReady 0 - wait); -1 = not tried, never on this title (LoadPostReady
   -1); -2 = tried and nothing was posted (load()'s own busy test, which this build cannot read). The caller has checked the folder's
   quick.save already (T243). MAIN THREAD, title pump. Outside the anonymous namespace: ui.cpp calls it. */
int PostLoadChecked(const std::string& saveName)
{
    const int ready = LoadPostReady();
    if (ready <= 0) return ready;
    SaveManager* sm = SaveManager::getSingleton();
    sm->load(saveName);
    return StoreSaveRequestCode(sm) == 2 ? 1 : -2;
}

} // namespace coop
