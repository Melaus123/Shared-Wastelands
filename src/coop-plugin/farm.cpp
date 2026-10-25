// farm.cpp - par16 (parity backlog P16; parity-audit-buildings-world.md H5 / M5). See farm.h for the rule in one paragraph.
//
// THE READ (Ghidra decompiles of Steam_1.0.65, RVAs from build/exports_all.txt - every
// offset below is seen used in the decompile named beside it; Read):
//   FarmBuilding::update 0xE5540 - the GROWTH TICK, no worker: water through _updateInputs 0xDF480; every Plant::age (plants
//     lektor +0x4E8: count +0x4F0, data +0x4F8, stride 0x28, age +0) grows by dt * fertility(+0x554) / growthTime(+0x53C)
//     (x droughtMultiplier +0x550 without water, x vt+0x368 power/production mult), grown(+0x518) = the lowest age; from 1 on
//     grown climbs by dt / harvestTime(+0x540) (ripe, waiting), past the next limit died(+0x51C) climbs by dt / deathTime(+0x544);
//     died >= 1 or harvested(+0x528) == plant count -> resetFarm 0xE48A0 (new random ages, harvested / died / cleared 0).
//     EVERY path ends in vt+0x598(this) - and when the global at 0x2133969 is set the engine does ONLY that and returns
//     (0xE5540 lines 22-27): that early-out is the held copy's path here. Its growth branch only runs with +0x160 set.
//   FarmBuilding::operate 0xE59A0 (who, amount) - the HARVEST, a worker's job: while ripe (grown >= 1, died < deathThreshold
//     +0x54C, harvested < count, productionState +0x468 != 1) the harvest bar *(+0x448) += mult * C * g_dt(0x2132734) * amount
//     * harvestRate(+0x538); at 1 the crop items are made INTO THE FARM'S OWN INVENTORY (vt+0x160, a 3-letter section) - never
//     into the worker's -, the bar goes to 0 and destroyAPlant 0xE47C0 sets that plant's age to -10, zeroes its parts' scale
//     (part +0, where the part's def +0x34 is 0) and harvested += 1. `who` (par16 fold #1, disassembly 0xE5A9F-0xE5AC8): the
//     worker's farming skill CharStats::getStat 0x883BF0(who+0x450, 0xC, false) x 0.01 is the argument of
//     getYieldChancePerCrop 0xDFDD0 - with who = 0 the engine passes 1.0 x 0.01 (skill 1). 0x883BF0 is a getter (a switch
//     over the stat, stat 0xC = CharStats +0xE8, then the modifiers) - it gives NO XP (this file said it did before the fold).
//     who is also where a "Failed" text is placed. A dead crop is CLEARED the same way (cleared +0x520 by clearRate +0x530).
//   FarmBuilding::timeSkip 0xDF6A0 (time, flag, p4) - the CATCH-UP growth for time the farm was not updated: rewrites the
//     growth state from the elapsed time and takes water ITEMS out of the input records' inventories (par16 fold #6).
//     par16 re-check #1: it takes THREE arguments (farm rcx, elapsed xmm1, flag r8b; flag set = no water items taken, 0xDF6A0
//     line 133): the fourth the decompile shows is r9 passed on untouched to 0x746320, which reads rcx/rdx/r8 only (disassembly
//     0x746320). Its ONLY caller is 0xE401E inside the farm's LOAD routine 0xE3DA0 (reads harvested, growth, "save time",
//     elapsed = now - save time, then timeSkip when elapsed > a minimum and +0x160 is set; flag = (vt+0x58(farm))+0x250 == 0):
//     it runs when the farm loads, before any update and before FarmTick has decided a writer - so a held catch-up is almost
//     always an UNDECIDED one, and it is kept (per farm, in its table row) until FarmTick decides.
//   The input records (lektor count +0x480, data +0x488, stride 0x20): stock +0, use rate +4, inventory +0x18 (_updateInputs
//     0xDF480; the rain branch of update adds to record 0's stock) - the farm's WATER (par16 fold #7).
//   updatePlantInstance 0xDF320 draws nothing for age < -1 (DAT_141683fcc) and reads +0x508; updateMaterial 0xE4500 reads
//     +0x4D0; destroyPhysical 0xE5E60 zeroes +0x508 (par16 fold #3 / #4).
//   So the writer's game grows AND harvests; the crop it makes lands in the farm's box, which the box road already carries
//   (inv4: the owner's game writes its own buildings' storage). A non-writer's harvest was a second crop from the same plant.
//
// THREADS: the detours may run on any thread - POD only, a CRITICAL_SECTION table, no std::string, no engine call but the
// engine's own tail and (operate on a held game) the same skill getter the engine's operate itself makes from that thread.
// The verdicts are decided on the MAIN THREAD (FarmTick) with items.cpp's writer ladder; engine writes (apply, the writer's
// operate for a request, the lever) happen at the K2 safe point (FarmDrain). Farms are re-found by key every time through
// this file's own hint (ObjectByPositionKeyHint: the pointer is re-proved each drain, never trusted across frames).
#include "farm.h"
#include "addresses.h"      /* AddrReg - the names below come from the address table */
#include "config.h"         /* RoleIsSingle */
#include "items.h"          /* ObjectPositionKey, ObjectByPositionKeyHint, FarmWriterHere */
#include "spawn.h"          /* FindSpawnedUid (any thread), FindSpawned (main thread) */
#include "store.h"          /* EngineWritesBlocked, PlayersPresent */
#include "../common/presence.h"   /* M11 C1: is another player in this world - the old link or the roster (PresenceDecide) */
#include "net/session.h"    /* SessionLinked, SendFarm */
#include "../common/farmwire.h"
#include "../common/keptwire.h"   /* KEPT: the kept farm's plant ages (FarmKeptLay) */
#include "coop_log.h"
#include "hooks.h"     /* coop::AddHook (own MinHook) */
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace coop {

static unsigned long long kFarmUpdateRva = 0;   static AddrReg kFarmUpdateRva_reg("FarmUpdate", &kFarmUpdateRva);         /* Steam_1.0.65 0xE5540 */
static unsigned long long kFarmOperateRva = 0;  static AddrReg kFarmOperateRva_reg("FarmOperate", &kFarmOperateRva);      /* Steam_1.0.65 0xE59A0 */
static unsigned long long kFarmPlantRva = 0;    static AddrReg kFarmPlantRva_reg("FarmUpdatePlantInstance", &kFarmPlantRva);   /* Steam_1.0.65 0xDF320 */
static unsigned long long kFarmMaterialRva = 0; static AddrReg kFarmMaterialRva_reg("FarmUpdateMaterial", &kFarmMaterialRva);  /* Steam_1.0.65 0xE4500 */
static unsigned long long kFarmTimeSkipRva = 0; static AddrReg kFarmTimeSkipRva_reg("FarmTimeSkip", &kFarmTimeSkipRva);    /* Steam_1.0.65 0xDF6A0 (fold #6) */
static unsigned long long kFarmYieldRva = 0;    static AddrReg kFarmYieldRva_reg("FarmYieldChance", &kFarmYieldRva);      /* Steam_1.0.65 0xDFDD0 (fold #1) */
static unsigned long long kGetStatRva = 0;      static AddrReg kGetStatRva_reg("CharStatsGetStat", &kGetStatRva);         /* Steam_1.0.65 0x883BF0 (fold #1) */
static unsigned long long kGameDtRva = 0;       static AddrReg kGameDtRva_reg("GameFrameDt", &kGameDtRva);                /* Steam_1.0.65 0x2132734, a float var (fold #2) */
static unsigned long long kTakeSpotRva = 0;     static AddrReg kTakeSpotRva_reg("BuildingTakeWorkSpot", &kTakeSpotRva);   /* Steam_1.0.65 0xF7FF0, 1.0.68 0xF8030 (T-674) */
static unsigned long long kSetEraseRva = 0;     static AddrReg kSetEraseRva_reg("WorkerSetErase", &kSetEraseRva);         /* Steam_1.0.65 0x28D400, 1.0.68 0x28D440 (T-674) */
static unsigned long long kSpHandCharRva = 0;   static AddrReg kSpHandCharRva_reg("Hand_asCharacter", &kSpHandCharRva);   /* Steam_1.0.65 0x7974F0 hand::getCharacter (T-674) */

namespace {

typedef void (*FmUpdateFn)(void* f);
typedef void (*FmOperateFn)(void* f, void* who, float amount);
typedef void (*FmPlantFn)(void* f, void* plant);
typedef void (*FmVoidFn)(void* f);
typedef void (*FmTimeSkipFn)(void* f, float t, char flag, void* p4);
typedef float (*FmYieldFn)(void* f, float skill01);
typedef float (*FmGetStatFn)(void* stats, int stat, bool raw);
typedef float (*FmMultFn)(void* f);   /* lever2: operate's vt+0x368(farm) - the production multiplier it scales the bar by */

const size_t kFmVtTail = 0x598;      /* the call every update path ends in (0xE5540:25, :145) */
const size_t kFmPlantCount = 0x4F0, kFmPlantData = 0x4F8, kFmPlantStride = 0x28;
const size_t kFmPlantParts = 0x18, kFmPlantPartData = 0x20, kFmPartStride = 0x20, kFmPartDef = 0x10, kFmPartDefKeep = 0x34;   /* destroyAPlant 0xE47C0 */
const size_t kFmGrown = 0x518, kFmDied = 0x51C, kFmCleared = 0x520, kFmGrowStart = 0x524, kFmHarvested = 0x528;
const size_t kFmProdState = 0x468, kFmBar = 0x448, kFmMarker = 0x26C;
const size_t kFmInCount = 0x480, kFmInData = 0x488, kFmInStride = 0x20;   /* _updateInputs 0xDF480: stock +0, rate +4 */
const size_t kFmPhysical = 0x160, kFmMesh = 0x508, kFmMaterial = 0x4D0;  /* fold #4: the engine's own guards */
const size_t kCharStats = 0x450;     /* operate: who+0x450 -> CharStats (0xE5AAB) */
const int    kStatFarming = 0xC;
const int    kFmCap = 1024;          /* farms a game tracks at once (fold #8: many players' bases); FULL is held and logged */
const int    kFmWorkers = 8;         /* workers a held farm keeps apart per round; more are merged into the last slot */
const int    kFmOpInCap = 32;        /* workers a writer keeps apart per farm */
const DWORD  kFmForgetMs = 30000;    /* an entry the update detour has not seen this long is dropped */
const DWORD  kFmPendMs = 60000;      /* a received FARM / FARM_OP whose farm does not resolve / is undecided is kept this long */
const float  kFmOpCap = 600.0f;      /* the work one worker slot may hold unsent */
const float  kFmOpChunk = 0.25f;     /* the writer runs a request in calls of at most this operate amount: one call ends at most one crop */
const int    kFmCallsPerDrain = 480; /* operate calls per farm per drain; the rest of the work waits for the next drain */
const int    kFmPubPerDrain = 8;
const float  kFmNominalDt = 1.0f / 60.0f;   /* only when the GameFrameDt row is missing (booked dtMissing) */

FmUpdateFn  orig_update = 0;
FmOperateFn orig_operate = 0;
FmTimeSkipFn orig_timeskip = 0;
FmYieldFn   orig_yield = 0;
int g_hkUpdate = 0, g_hkOperate = 0, g_hkTimeSkip = 0, g_hkYield = 0;

struct FmWork { unsigned int uid; float skill; float work; };
struct FmEntry { char key[64]; unsigned int hash; int verdict; DWORD seenAt; int used; int nw; FmWork w[kFmWorkers];
                int tsPend; float tsT; char tsFlag; DWORD tsAt; int tsLate;
                DWORD undecAt; };   /* par16 re-check #1: tsPend 1 held undecided, 2 due to replay; tsLate: counted late once;
                                       T-853 #7: undecAt = when the ladder turned undecided for it (0 = it answers) */
FmEntry g_fm[kFmCap];
int g_fmHigh = 0;                    /* under g_fmcs: one past the highest row ever used this world (scans stop there) */
CRITICAL_SECTION g_fmcs;
volatile LONG g_fmInit = 0, g_fmLinked = 0, g_fmFullFlag = 0, g_tsDue = 0;   /* g_tsDue: a held catch-up is due (re-check #1) */

/* the yield override (fold #1): armed on the MAIN THREAD around the writer's own operate call for one farm */
void* volatile g_yOverFarm = 0;
volatile DWORD g_yOverThread = 0;
volatile float g_yOverVal = 0.0f;

volatile LONG64 g_upSeen = 0, g_upRan = 0, g_upHeld = 0, g_upNoKey = 0, g_opHeld = 0, g_opLocal = 0, g_opNoEntry = 0, g_fmFull = 0,
                g_tailFault = 0, g_tsSeen = 0, g_tsRan = 0, g_tsHeld = 0, g_tsHeldUndecided = 0, g_tsReplayed = 0,
                g_tsDropped = 0, g_tsLate = 0, g_opMerged = 0, g_skillRead = 0,
                g_skillFault = 0, g_dtMissing = 0, g_yieldOverridden = 0, g_opZeroWork = 0, g_opCopySkipped = 0;
long long g_pubSent = 0, g_pubKeep = 0, g_pubFail = 0, g_readFail = 0, g_recvState = 0, g_recvOp = 0, g_applied = 0, g_applyWait = 0,
          g_applyRefusedWriter = 0, g_applyMismatch = 0, g_applyDropped = 0, g_applyFault = 0, g_applyWaitWriter = 0, g_refreshSkipped = 0,
          g_plantsHidden = 0, g_waterApplied = 0, g_inTooMany = 0,
          g_opSent = 0, g_opSendFail = 0, g_opApplied = 0, g_opAppliedCalls = 0, g_opRefused = 0, g_opDropped = 0, g_opWaitWriter = 0,
          g_opPaused = 0, g_opWithCopy = 0, g_opWithSkill = 0, g_opNoWorker = 0, g_opHandedToSelf = 0, g_opDroppedStale = 0,
          g_opDroppedLink = 0, g_blocked = 0, g_opFault = 0,
          g_verdictMine = 0, g_verdictOther = 0, g_verdictNone = 0, g_testRuns = 0, g_testRefused = 0,
          g_applyRefusedSender = 0, g_applyWaitSender = 0, g_applyKeptProven = 0;   /* M3 */
int g_said = 0;   /* [FARM] lines this world (64 max) */
int g_saidFull = 0, g_saidNoWorker = 0;

struct FmIn { coopfarm::FarmMsg m; DWORD at; unsigned int from; int proven; };   /* proven: the sender was the named writer when it came */
std::map<std::string, FmIn> g_inFarm;                   /* MAIN THREAD: the latest FARM per key, applied at K2 */
struct FmOpIn { std::vector<FmWork> w; DWORD at; unsigned int from; };
std::map<std::string, FmOpIn> g_inOp;                   /* MAIN THREAD: work per key and worker, run at K2 by the writer */
struct FmLast { coopfarm::FarmMsg m; DWORD at; int ever; };
std::map<std::string, FmLast> g_last;                   /* MAIN THREAD: what the writer sent last per key */
std::map<std::string, void*> g_hint;                    /* MAIN THREAD (fold #5): the last pointer per key, re-proved on use */
DWORD g_tickAt = 0, g_pubAt = 0;
int g_pubRot = 0;
/* the lever (MAIN THREAD armed, K2 run) */
int g_ftKind = 0; std::string g_ftSub; float g_ftVal = 0.0f;

void FmSay(const std::string& s) { if (g_said < 64) { ++g_said; DebugLog(s); } }

unsigned int FmHash(const char* k)
{
    unsigned int h = 2166136261u;
    for (; *k != 0; ++k) { h ^= (unsigned char)*k; h *= 16777619u; }
    return h;
}

/* under g_fmcs: the row of `key`, -1 when none */
int FmFindLocked(const char* key, unsigned int h)
{
    for (int i = 0; i < g_fmHigh; ++i)
        if (g_fm[i].used != 0 && g_fm[i].hash == h && std::strcmp(g_fm[i].key, key) == 0) return i;
    return -1;
}

/* MAIN THREAD (fold #5): the farm a key names, through this file's own hint - never the box cache's 16 rows */
void* FmResolve(const std::string& key)
{
    void*& h = g_hint[key];
    h = ObjectByPositionKeyHint(key.c_str(), h);
    if (h == 0) { g_hint.erase(key); return 0; }
    return h;
}

/* ANY THREAD. The entry's verdict (0 when none), stamping it seen; a new key gets an undecided entry; a key that finds the
   table full answers kFarmVerdictFull (held - fold #8). */
int FmSeen(const char* key)
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return 0;
    const unsigned int h = FmHash(key);
    int v = 0;
    ::EnterCriticalSection(&g_fmcs);
    const int i = FmFindLocked(key, h);
    if (i >= 0) { g_fm[i].seenAt = ::GetTickCount(); v = g_fm[i].verdict; }
    else
    {
        int free = -1;
        for (int j = 0; j < kFmCap; ++j) if (g_fm[j].used == 0) { free = j; break; }
        if (free >= 0)
        {
            FmEntry& e = g_fm[free];
            std::memset(&e, 0, sizeof(e));
            std::strncpy(e.key, key, sizeof(e.key) - 1);
            e.key[sizeof(e.key) - 1] = 0;
            e.hash = h; e.verdict = 0; e.seenAt = ::GetTickCount(); e.used = 1;
            if (free >= g_fmHigh) g_fmHigh = free + 1;
        }
        else { ::InterlockedIncrement64(&g_fmFull); ::InterlockedExchange(&g_fmFullFlag, 1); v = coopfarm::kFarmVerdictFull; }
    }
    ::LeaveCriticalSection(&g_fmcs);
    return v;
}

/* under g_fmcs: `work` for (uid, skill) into row i */
void FmAddWorkLocked(int i, unsigned int uid, float skill, float work)
{
    FmEntry& e = g_fm[i];
    int s = -1;
    for (int k = 0; k < e.nw; ++k)
        if (e.w[k].uid == uid && (uid != 0 || e.w[k].skill == skill)) { s = k; break; }
    if (s < 0)
    {
        if (e.nw < kFmWorkers) { s = e.nw++; e.w[s].uid = uid; e.w[s].skill = skill; e.w[s].work = 0.0f; }
        else { s = kFmWorkers - 1; ::InterlockedIncrement64(&g_opMerged); }
    }
    e.w[s].work += work;
    if (e.w[s].work > kFmOpCap) e.w[s].work = kFmOpCap;
}

/* ANY THREAD. A held game's worker put `work` (amount x g_dt) into the farm: kept for FarmTick to send. */
void FmAddOp(const char* key, unsigned int uid, float skill, float work)
{
    if (!(work > 0.0f && work < 1.0e6f)) { if (work == 0.0f) ::InterlockedIncrement64(&g_opZeroWork); return; }
    const unsigned int h = FmHash(key);
    int found = 0;
    ::EnterCriticalSection(&g_fmcs);
    const int i = FmFindLocked(key, h);
    if (i >= 0) { FmAddWorkLocked(i, uid, skill, work); found = 1; }
    ::LeaveCriticalSection(&g_fmcs);
    if (found == 0) ::InterlockedIncrement64(&g_opNoEntry);
}

/* ANY THREAD. This frame's g_dt (fold #2); a missing row reads a nominal frame (booked). */
float FmDt()
{
    if (kGameDtRva == 0) { ::InterlockedIncrement64(&g_dtMissing); return kFmNominalDt; }
    float dt = 0.0f;
    __try { dt = *(const float*)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kGameDtRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
    return (dt == dt && dt >= 0.0f && dt < 100.0f) ? dt : 0.0f;
}

/* ANY THREAD (fold #1). The worker's farming skill as operate reads it (getStat(who+0x450, 0xC, false)), -1 unknown. */
float FmSkillOf(void* who)
{
    if (who == 0 || kGetStatRva == 0) return coopfarm::kFarmSkillUnknown;
    float v = coopfarm::kFarmSkillUnknown;
    __try
    {
        void* stats = *(void**)((char*)who + kCharStats);
        if (stats == 0) return coopfarm::kFarmSkillUnknown;
        v = ((FmGetStatFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kGetStatRva))(stats, kStatFarming, false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ::InterlockedIncrement64(&g_skillFault); return coopfarm::kFarmSkillUnknown; }
    if (!(v == v && v >= 0.0f && v < 1.0e4f)) return coopfarm::kFarmSkillUnknown;
    ::InterlockedIncrement64(&g_skillRead);
    return v;
}

/* ---- MINE_OP - a worker's step on a production building (a mining node, a machine) this game does not write ----
   items.cpp detour_prodOperate hands the step here instead of running it (coopfarm::MineStepRoute); the work is kept per
   (building key, worker) and FarmTick sends it to the writer every 500 ms - while no writer is named it waits, up to
   coopfarm::kMinePendMs; the writer runs the engine's own
   ProductionBuilding::operate at K2 (FarmDrain), so the ore is made once, into the building's own box, and reaches every game
   as a box move. The table is POD under g_fmcs (the detour may run on any thread). */
const int   kMnCap = 256;            /* (building, worker) pairs a game relays at once; a pair past it runs its step here (counted) */
const DWORD kMnForgetMs = 30000;     /* a pair with no work and no step this long is dropped */
const int   kMnSayCap = 12;
struct MnRow { char key[64]; unsigned int hash; unsigned int uid; float amount; float work; DWORD seenAt; int used;
               DWORD undecidedAt; /* when this row's writer was first found unnamed, 0 = named */ };
MnRow g_mn[kMnCap];
int g_mnHigh = 0;                    /* under g_fmcs */
struct MnWork { unsigned int uid; float amount; float work; };
struct MnIn { std::vector<MnWork> w; DWORD at; unsigned int from; };
std::map<std::string, MnIn> g_inMine;   /* MAIN THREAD: received (or handed to self) work per building key, run at K2 */
volatile LONG64 g_mnHeld = 0, g_mnNoKey = 0, g_mnFull = 0;
volatile LONG64 g_mnBadAmount = 0;
long long g_mnSent = 0, g_mnSendFail = 0, g_mnToSelf = 0, g_mnRecv = 0, g_mnRan = 0, g_mnCalls = 0, g_mnRefused = 0, g_mnDropped = 0,
          g_mnFault = 0, g_mnPaused = 0, g_mnWithCopy = 0, g_mnNoWorker = 0, g_mnDroppedLink = 0, g_mnForgot = 0,
          g_mnKeptUndecided = 0, g_mnDroppedUndecided = 0, g_mnWaitWriter = 0, g_mnNotProduction = 0;
int g_mnSaid = 0;
void MnSay(const std::string& s) { if (g_mnSaid < kMnSayCap) { ++g_mnSaid; DebugLog(s); } }
long long g_mnSendFailLost = 0;   /* MINE_OP work not sent and not kept again (the relay table full) */
long long g_mnCapLost = 0;        /* MnAdd: work a row could not hold past kFmOpCap (under g_fmcs) */
/* the writer drops work it never ran (said: coopfarm::MineSayDue) */
void MnSayDropped(const std::string& key, const char* why, DWORD ms)
{
    if (coopfarm::MineSayDue(g_mnDropped) == 0) return;
    char h[400];
    std::sprintf(h, "[MINE] MINE_OP work dropped key=%s: %s for %u s; dropped=%lld", key.c_str(), why, (unsigned int)(ms / 1000), g_mnDropped);
    DebugLog(std::string(h));
}

/* ---- MINE_STATE - a node's bar on the games that do not write it ----
   A game that does not write a production building never runs its step, so its bar *(+0x448) and state +0x468 never move there.
   Every node a worker's step ran on HERE, for this game's own worker (items.cpp detour_prodOperate) or for relayed MINE_OP work
   (FarmDrain), is marked in this table (any thread, under g_fmcs); FarmTick sends its bar and state to every game
   (coopfarm::MineStatePublishDue), and a game another game writes it for writes them onto its copy at K2 (FarmDrain). */
const int   kMsCap = 256;            /* nodes worked here at once; a node past it is not published (counted) */
const DWORD kMsForgetMs = 60000;     /* a node not worked this long leaves the table (its last send went out long before) */
struct MsRow { char key[64]; unsigned int hash; DWORD workedAt; int used; int ever; DWORD sentAt; float sentProgress; int sentState;
               int finalSent; int unresSaid; DWORD unresAt; int readFailSaid; DWORD readFailAt; };
MsRow g_ms[kMsCap];
int g_msHigh = 0;                    /* under g_fmcs */
struct MsIn { float progress; int state; DWORD at; unsigned int from; };   /* T-853 #11: from = the sender */
std::map<std::string, MsIn> g_inMineState;   /* MAIN THREAD: the latest received bar and state per node key, applied at K2 */
volatile LONG64 g_msFull = 0;
long long g_msSent = 0, g_msSendFail = 0, g_msReadFail = 0, g_msRecv = 0, g_msApplied = 0, g_msRefused = 0, g_msApplyFault = 0,
          g_msNotProduction = 0, g_msUnresolved = 0, g_msSendUnresolved = 0;

/* ANY THREAD: the node at `key` was worked now */
void MsMark(const char* key)
{
    const unsigned int h = FmHash(key);
    const DWORD now = ::GetTickCount();
    ::EnterCriticalSection(&g_fmcs);
    int i = -1, free = -1;
    for (int j = 0; j < g_msHigh; ++j)
    {
        if (g_ms[j].used == 0) { if (free < 0) free = j; continue; }
        if (g_ms[j].hash == h && std::strcmp(g_ms[j].key, key) == 0) { i = j; break; }
    }
    if (i < 0 && free < 0 && g_msHigh < kMsCap) free = g_msHigh;
    if (i < 0 && free >= 0)
    {
        MsRow& r = g_ms[free];
        std::memset(&r, 0, sizeof(r));
        std::strncpy(r.key, key, sizeof(r.key) - 1);
        r.key[sizeof(r.key) - 1] = 0;
        r.hash = h; r.used = 1;
        if (free >= g_msHigh) g_msHigh = free + 1;
        i = free;
    }
    if (i >= 0) { g_ms[i].workedAt = now; g_ms[i].finalSent = 0; }
    ::LeaveCriticalSection(&g_fmcs);
    if (i < 0) ::InterlockedIncrement64(&g_msFull);
}

/* ANY THREAD. Work for (key, uid) into the table; amount = the operate amount of the worker's call. 1 kept, 0 the table full,
   -1 a new row whose amount is unusable (dropped at once - its work could never be sent). */
int MnAdd(const char* key, unsigned int uid, float amount, float work)
{
    const unsigned int h = FmHash(key);
    int kept = 0, capped = 0;
    long long capN = 0;
    ::EnterCriticalSection(&g_fmcs);
    int i = -1, free = -1;
    for (int j = 0; j < g_mnHigh; ++j)
    {
        if (g_mn[j].used == 0) { if (free < 0) free = j; continue; }
        if (g_mn[j].hash == h && g_mn[j].uid == uid && std::strcmp(g_mn[j].key, key) == 0) { i = j; break; }
    }
    if (i < 0 && free < 0 && g_mnHigh < kMnCap) free = g_mnHigh;
    if (i < 0 && free >= 0)
    {
        MnRow& r = g_mn[free];
        std::memset(&r, 0, sizeof(r));
        std::strncpy(r.key, key, sizeof(r.key) - 1);
        r.key[sizeof(r.key) - 1] = 0;
        r.hash = h; r.uid = uid; r.used = 1;
        if (free >= g_mnHigh) g_mnHigh = free + 1;
        i = free;
    }
    if (i >= 0)
    {
        MnRow& r = g_mn[i];
        r.seenAt = ::GetTickCount();
        if (amount > 0.0f && amount <= coopfarm::kMineAmountMax) r.amount = amount;
        if (work > 0.0f && work < 1.0e6f)
        {
            r.work += work;
            if (r.work > kFmOpCap) { r.work = kFmOpCap; capped = 1; }
        }
        kept = 1;
        if (!(r.amount > 0.0f)) { r.used = 0; kept = -1; capped = 0; }   /* the whole row is dropped (counted badAmount), not the cap */
        if (capped != 0) capN = ++g_mnCapLost;
    }
    ::LeaveCriticalSection(&g_fmcs);
    if (capped != 0 && coopfarm::MineSayDue(capN) != 0)
    {   /* the work past the cap is gone: no game runs it */
        char h[300];
        std::sprintf(h, "[MINE] work dropped key=%.63s: the queue for that node is full (it holds at most %d work) - capLost=%lld",
                     key, (int)kFmOpCap, capN);
        DebugLog(std::string(h));
    }
    return kept;
}

/* MAIN THREAD: (uid, amount, work) merged into a building's received work */
void MnMerge(MnIn* in, unsigned int uid, float amount, float work)
{
    for (size_t k = 0; k < in->w.size(); ++k)
        if (in->w[k].uid == uid)
        {
            in->w[k].work += work;
            if (in->w[k].work > kFmOpCap) in->w[k].work = kFmOpCap;
            in->w[k].amount = amount;
            return;
        }
    if ((int)in->w.size() >= kFmOpInCap) { in->w.back().work += work; return; }
    MnWork x; x.uid = uid; x.amount = amount; x.work = work;
    in->w.push_back(x);
}

/* ---- T-674: a machine's worker spots, counted by the game that runs the machine ----
   THE READ (Ghidra on Steam_1.0.65; the 1.0.68 rows found in the vanilla exe through the vtables whose operate slot is the
   trusted ProductionOperate row): the work step 0x347490 calls the building's take vt+0x4F8 with the worker's hand before it
   calls operate vt+0x4B0 (amount 1.0); for every vtable whose operate is ProductionBuilding::operate 0x2986C0 that take is
   Building 0xF7FF0: 0 when the hand is not in the worker set and the set's count +0x3E0 has reached the limit +0x3AC, else it
   adds the hand (0xF7D50) and answers 1 (1 also when it was listed already). The set is a std::set<hand> at +0x3D0 (head +0x3D8;
   a node's value at +0x18; its own find 0xF7890 compares the hand's four ids +0xC..+0x18, and a null hand, type 0xB, is never
   listed). A worker leaves through the set's erase 0x28D400(set, hand). T-853 #10: it has 21 call sites, all through the thunk
   0x384F1 (build/xrefs_28d400.txt), and not every one passes a building's set: 0x622E90 passes a set at +8 of an object it
   gets through the global at 0x2132F38 - so the erase detour acts only when set - 0x3D0 is a production building
   (ProductionBuildingIs: its operate slot), and that call is passed straight to the engine.
   Each game counted only its own workers, so a machine took limit x games workers.
   On a game that does not run the building (ProdStepWriterHere) a worker that is not listed is asked of the runner (SPOT_ASK,
   sent by FarmTick) and its step is refused until the answer: yes -> the engine's own take runs here; full -> refused
   for coopfarm::kSpotRetryMs, then asked again; no answer in coopfarm::kSpotAskGiveUpMs -> asked again. The runner takes the spot
   through the engine's own take with its copy of the worker (so its own workers see the machine full) and keeps the hand it
   added; a release, its copy going, or no longer running the building frees it through the set's own erase (FarmDrain, K2).
   A new row's runner is not known until FarmTick reads it: a new row has FarmTick read every row's runner on the next frame
   (g_spNewDue), so a worker at a building this game runs is refused only the steps of that one frame.
   The detours run on the work step's thread: POD rows under g_fmcs, no engine call but the hand lookup and the engine's own tail.
   T-853 #9 (Read, not measured in a run): the work step 0x347490 is a task's per-frame slot (Task_UseTurret vt+0x10,
   build/vtable_Task_UseTurret.txt), run by AITaskSytem::update inside Character::threadedUpdate - the AI worker thread (F062). The
   runner's own take / erase for another game's worker (SpTakeRun / SpEraseRun) and the asker's set walk in SpUnusedSweep run in
   SpDrain at the K2 safe point (FarmDrain, 0x7D17E0), where the AI worker and the physics thread are idle (mainLoop line 85, the AI
   pass at the tail of charsUpdate - the 2026-09-29 knock-down read, 06-hypotheses) - so they never run beside the engine's own.
   T-853 #4: a worker the runner has NO copy of is no longer refused (it could never work there): the runner COUNTS a spot for it
   (no hand in the set, g_spCt) and its own take detour refuses its own unlisted workers while the set's count plus the counted
   spots reaches the limit; the asker re-asks a granted spot every coopfarm::kSpotKeepMs, and a counted spot not asked for
   coopfarm::kSpotCountedForgetMs is freed. T-853 #5: any spot the runner frees but for a release is told to the asker
   (kSpotRevoked), which asks again; a granted row whose runner's slot changes is asked again of the new runner. */
typedef unsigned long long (*SpTakeFn)(void* b, void* hand);
typedef long long (*SpEraseFn)(void* set, void* hand);
typedef void* (*SpHandCharFn)(const void* hand);
SpTakeFn  orig_take = 0;
SpEraseFn orig_erase = 0;
int g_hkTake = 0, g_hkErase = 0;
const size_t kBldWorkerSet = 0x3D0;  /* std::set<hand>: head +8, count +0x10 */
const size_t kObjHandle = 0x58;      /* RootObjectBase::handle (items.cpp kObjHandleIds +0x60 are its ids) */
const size_t kHandBytes = 0x20;
const int    kSpCap = 256;           /* (building, worker) rows a game keeps; a worker past it takes its spot as before (counted) */
const int    kSpRunCap = 256;        /* spots a runner keeps for other games' workers; an ask past it is answered full (counted) */
const size_t kSpInCap = 1024;        /* received asks / releases waiting for K2 */
struct SpRow { char key[64]; unsigned int hash; unsigned int uid; unsigned int ids[4]; int used; int state; DWORD at; DWORD seenAt;
               int writer; int askDue; int releaseDue;
               int grantSlot; DWORD askAt; };   /* T-853: grantSlot = the slot that answered yes (-1 none); askAt = the last ask sent */
SpRow g_sp[kSpCap];
int g_spHigh = 0;                    /* under g_fmcs */
volatile LONG g_spGrantedN = 0;      /* rows in kSpotGranted: the erase detour reads nothing while 0 */
volatile LONG g_spNewDue = 0;        /* a worker row was made since the last runner read: FarmTick reads the runners on its next frame */
volatile LONG64 g_spSeen = 0, g_spPassListed = 0, g_spPassed = 0, g_spRefused = 0, g_spAsked = 0, g_spNoKey = 0, g_spNoUid = 0,
                g_spFull = 0, g_spLeft = 0, g_spLeftNoRow = 0, g_spHandFault = 0;
long long g_spGiveUp = 0;            /* under g_fmcs */
long long g_spAskSent = 0, g_spAskFail = 0, g_spAnsRecv = 0, g_spAnsYes = 0, g_spAnsFull = 0, g_spAnsNoCopy = 0, g_spAnsStray = 0,
          g_spRelSent = 0, g_spRelFail = 0, g_spInDropped = 0,
          g_srAskRecv = 0, g_srNotRunner = 0, g_srYes = 0, g_srFull = 0, g_srNoCopy = 0, g_srAnsFail = 0, g_srRelRecv = 0, g_srRelNoRow = 0,
          g_srFreedRel = 0, g_srFreedGone = 0, g_srFreedNotRunner = 0, g_srFreedUnloaded = 0, g_srFault = 0, g_srCapFull = 0;
long long g_spAnsRevoked = 0, g_spAnsKeep = 0, g_spStrayRel = 0, g_spUnusedRel = 0, g_spRegrant = 0, g_spKeepAsk = 0, g_srCounted = 0,
          g_srCountedFull = 0, g_srRevokeSent = 0, g_srRevokeFail = 0, g_srFreedCounted = 0, g_msRefusedSender = 0, g_msWaitSender = 0,
          g_verdictFallback = 0;   /* T-853 */
int g_spSaid = 0;
void SpSay(const std::string& s) { if (g_spSaid < 24) { ++g_spSaid; DebugLog(s); } }
struct SpIn { unsigned char kind; std::string key; unsigned int uid; };
std::vector<SpIn> g_inSpot;          /* MAIN THREAD: received asks and releases, handled at K2 (FarmDrain) */
struct SpRun { std::string key; unsigned int uid; unsigned char hand[kHandBytes]; int counted; DWORD askAt; };   /* T-853 #4: counted 1 = no copy here, no hand in the set; askAt = the last ask for it */
std::vector<SpRun> g_spRun;          /* MAIN THREAD: the spots this game took, as the runner, for other games' workers */
DWORD g_spSweepAt = 0;
DWORD g_spUnusedAt = 0;              /* T-853 #3: SpUnusedSweep's last run */
const size_t kBldWorkerLimit = 0x3AC, kBldWorkerCount = 0x3E0;   /* int limit, u64 count (0xF7FF0 and 0xF7BB0 read both) */
/* T-853 #4: the spots this game, as the runner, COUNTS for other games' workers it has no copy of - per building key, POD under
   g_fmcs (the take detour reads it on the work step's thread) */
struct SpCtRow { char key[64]; unsigned int hash; int n; };
SpCtRow g_spCt[kSpRunCap];
int g_spCtHigh = 0;                  /* under g_fmcs */
volatile LONG g_spCtN = 0;           /* counted spots in all: the take detour reads nothing while 0 */
volatile LONG64 g_spCtRefused = 0;
volatile LONG64 g_spCopyRefused = 0;   /* T-610: takes refused to a copy of another game's character (SpotCopyRefused) */

/* ANY THREAD, crash-guarded: the hand's four ids (+0xC..+0x18) and whether it is the null hand (type 0xB). 1 read. */
int SpHandIds(const void* hand, unsigned int* ids, int* isNull)
{
    __try
    {
        const unsigned int* p = (const unsigned int*)((const char*)hand + 0xC);
        ids[0] = p[0]; ids[1] = p[1]; ids[2] = p[2]; ids[3] = p[3];
        *isNull = (*(const int*)((const char*)hand + 8) == 0xB) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* ANY THREAD, crash-guarded: 1 when the building's worker set holds a hand with these ids - a bounded walk of the tree (left +0,
   right +0x10, the head's +8 the root, +0x39 set on the head); 0 also when the walk cannot finish */
int SpListed(const void* b, const unsigned int* ids)
{
    __try
    {
        const char* set = (const char*)b + kBldWorkerSet;
        const char* head = *(const char* const*)(set + 8);
        if (head == 0 || *(const unsigned long long*)(set + 0x10) > 4096) return 0;
        const char* stack[64];
        int sp = 0, steps = 0;
        const char* root = *(const char* const*)(head + 8);
        if (root != 0 && root != head && *(root + 0x39) == 0) stack[sp++] = root;
        while (sp > 0 && steps < 4096)
        {
            const char* nd = stack[--sp];
            ++steps;
            const unsigned int* v = (const unsigned int*)(nd + 0x24);
            if (v[0] == ids[0] && v[1] == ids[1] && v[2] == ids[2] && v[3] == ids[3]) return 1;
            const char* kid[2];
            kid[0] = *(const char* const*)nd; kid[1] = *(const char* const*)(nd + 0x10);
            for (int k = 0; k < 2; ++k)
                if (kid[k] != 0 && *(kid[k] + 0x39) == 0) { if (sp >= 64) return 0; stack[sp++] = kid[k]; }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 0;
}

/* ANY THREAD, crash-guarded: the character a hand names (hand::getCharacter), 0 none */
void* SpCharOf(const void* hand)
{
    if (kSpHandCharRva == 0) return 0;
    __try { return ((SpHandCharFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kSpHandCharRva))(hand); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ::InterlockedIncrement64(&g_spHandFault); return 0; }
}

/* MAIN THREAD (T-853 #4): d counted spots more (or fewer) for the building at `key` */
void SpCtAdjust(const char* key, int d)
{
    const unsigned int h = FmHash(key);
    ::EnterCriticalSection(&g_fmcs);
    int i = -1, free = -1;
    for (int j = 0; j < g_spCtHigh; ++j)
    {
        if (g_spCt[j].n <= 0) { if (free < 0) free = j; continue; }
        if (g_spCt[j].hash == h && std::strcmp(g_spCt[j].key, key) == 0) { i = j; break; }
    }
    if (i < 0 && d > 0)
    {
        if (free < 0 && g_spCtHigh < kSpRunCap) free = g_spCtHigh;
        if (free >= 0)
        {
            SpCtRow& r = g_spCt[free];
            std::memset(&r, 0, sizeof(r));
            std::strncpy(r.key, key, sizeof(r.key) - 1);
            r.key[sizeof(r.key) - 1] = 0;
            r.hash = h;
            if (free >= g_spCtHigh) g_spCtHigh = free + 1;
            i = free;
        }
    }
    if (i >= 0)
    {
        const int before = g_spCt[i].n;
        g_spCt[i].n += d;
        if (g_spCt[i].n < 0) g_spCt[i].n = 0;
        ::InterlockedExchangeAdd(&g_spCtN, (LONG)(g_spCt[i].n - before));
    }
    ::LeaveCriticalSection(&g_fmcs);
}

/* ANY THREAD (T-853 #4): the counted spots for the building at `key` */
int SpCtCount(const char* key)
{
    const unsigned int h = FmHash(key);
    int n = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_spCtHigh; ++j)
        if (g_spCt[j].n > 0 && g_spCt[j].hash == h && std::strcmp(g_spCt[j].key, key) == 0) { n = g_spCt[j].n; break; }
    ::LeaveCriticalSection(&g_fmcs);
    return n;
}

/* ANY THREAD, crash-guarded (T-853 #4): the worker set's count +0x3E0 and the limit +0x3AC, as the engine's take reads them. 1 read. */
int SpRoomRead(const void* b, unsigned long long* count, int* limit)
{
    __try
    {
        *count = *(const unsigned long long*)((const char*)b + kBldWorkerCount);
        *limit = *(const int*)((const char*)b + kBldWorkerLimit);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* under g_fmcs: the row of (key, uid), -1 none */
int SpFindLocked(const char* key, unsigned int h, unsigned int uid)
{
    for (int i = 0; i < g_spHigh; ++i)
        if (g_sp[i].used != 0 && g_sp[i].hash == h && g_sp[i].uid == uid && std::strcmp(g_sp[i].key, key) == 0) return i;
    return -1;
}

/* under g_fmcs: a row's state, keeping the granted count the erase detour reads */
void SpSetState(SpRow& r, int st, DWORD now)
{
    if (r.state == coopfarm::kSpotGranted && st != coopfarm::kSpotGranted) ::InterlockedDecrement(&g_spGrantedN);
    if (r.state != coopfarm::kSpotGranted && st == coopfarm::kSpotGranted) ::InterlockedIncrement(&g_spGrantedN);
    r.state = st;
    r.at = (now != 0) ? now : 1;
}

/* ANY THREAD: the take step's verdict for (key, uid) - coopfarm::SpotTakeStep on the worker's row, made here when new */
int SpTakeDecide(const char* key, unsigned int uid, const unsigned int* ids)
{
    const unsigned int h = FmHash(key);
    const DWORD now = ::GetTickCount();
    int act = coopfarm::kSpotPass;
    ::EnterCriticalSection(&g_fmcs);
    int i = SpFindLocked(key, h, uid);
    if (i < 0)
    {
        int free = -1;
        for (int j = 0; j < g_spHigh; ++j) if (g_sp[j].used == 0) { free = j; break; }
        if (free < 0 && g_spHigh < kSpCap) free = g_spHigh;
        if (free >= 0)
        {
            SpRow& r = g_sp[free];
            std::memset(&r, 0, sizeof(r));
            std::strncpy(r.key, key, sizeof(r.key) - 1);
            r.key[sizeof(r.key) - 1] = 0;
            r.hash = h; r.uid = uid; r.used = 1; r.state = coopfarm::kSpotNone; r.at = now; r.writer = coopfarm::kProdWriterNone;
            r.grantSlot = -1;
            if (free >= g_spHigh) g_spHigh = free + 1;
            i = free;
            ::InterlockedExchange(&g_spNewDue, 1);
        }
    }
    if (i >= 0)
    {
        SpRow& r = g_sp[i];
        for (int k = 0; k < 4; ++k) r.ids[k] = ids[k];
        r.seenAt = now;
        act = coopfarm::SpotTakeStep(0, 1, 0, r.writer, r.state, (unsigned int)(now - r.at));
        if (act == coopfarm::kSpotAskNow)
        {
            if (r.state == coopfarm::kSpotAsked) ++g_spGiveUp;
            SpSetState(r, coopfarm::kSpotAsked, now);
            r.askDue = 1;
        }
    }
    ::LeaveCriticalSection(&g_fmcs);
    if (i < 0) ::InterlockedIncrement64(&g_spFull);
    return act;
}

/* ANY THREAD: the work step's take on a production building (vt+0x4F8 = 0xF7FF0). Listed workers, a lone game, a building this
   game runs and a granted spot run the engine's own take; any other worker's step is refused until the runner answers. T-853 #4:
   while this game counts spots for other games' workers it has no copy of, its own unlisted worker is refused once the set's
   count plus those spots reaches the limit (the engine's own take does not know them). */
unsigned long long detour_take(void* b, void* hand)
{
    if (RoleIsSingle() || ::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0 || hand == 0 || ProductionBuildingIs(b) == 0) return orig_take(b, hand);
    ::InterlockedIncrement64(&g_spSeen);
    unsigned int ids[4];
    int isNull = 0;
    if (SpHandIds(hand, ids, &isNull) == 0 || isNull != 0) return orig_take(b, hand);
    if (SpListed(b, ids) != 0) { ::InterlockedIncrement64(&g_spPassListed); return orig_take(b, hand); }
    char key[64];
    if (ObjectPositionKey(b, key, (int)sizeof(key), 0, 3, 0) == 0) { ::InterlockedIncrement64(&g_spNoKey); return orig_take(b, hand); }
    if (::InterlockedCompareExchange(&g_spCtN, 0, 0) != 0)
    {   /* T-853 #4: counted spots for this building - the limit counts them too */
        const int ct = SpCtCount(key);
        unsigned long long cnt = 0;
        int lim = 0;
        if (ct > 0 && SpRoomRead(b, &cnt, &lim) != 0 && coopfarm::SpotRoomLeft(cnt, ct, lim) == 0) { ::InterlockedIncrement64(&g_spCtRefused); return 0; }
    }
    void* who = SpCharOf(hand);
    const unsigned int uid = (who != 0) ? FindSpawnedUid(who) : 0u;
    if (uid == 0) { ::InterlockedIncrement64(&g_spNoUid); return orig_take(b, hand); }   /* a worker no other game can name: as before */
    /* T-610: a copy of another game's character takes no spot here - its own game asks the runner, and the runner takes the spot
       with its copy through SpTakeRun (no detour), so that copy is listed above. Before, on the runner such a copy's own take
       passed (the writer is here) and its step ran here as well as on its own game. Any-thread owner test (O1). */
    if (coopfarm::SpotCopyRefused(0, 1, 0, coopfarm::WorkerIsCopy(uid, net::IsUidMineAnyThread(uid) ? 1 : 0,
                                                                  net::OwnedMirrorExactAnyThread() ? 1 : 0)) != 0)
    { ::InterlockedIncrement64(&g_spCopyRefused); return 0; }
    const int act = SpTakeDecide(key, uid, ids);
    if (act == coopfarm::kSpotPass) { ::InterlockedIncrement64(&g_spPassed); return orig_take(b, hand); }
    ::InterlockedIncrement64(act == coopfarm::kSpotAskNow ? &g_spAsked : &g_spRefused);
    return 0;
}

/* ANY THREAD: a worker left a building's worker set here (`erased` entries); a spot the runner granted is released (FarmTick) */
void SpNoteLeft(const char* key, const unsigned int* ids, long long erased)
{
    const unsigned int h = FmHash(key);
    const DWORD now = ::GetTickCount();
    int found = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int i = 0; i < g_spHigh; ++i)
    {
        SpRow& r = g_sp[i];
        if (r.used == 0 || r.hash != h || r.ids[0] != ids[0] || r.ids[1] != ids[1] || r.ids[2] != ids[2] || r.ids[3] != ids[3]
            || std::strcmp(r.key, key) != 0) continue;
        found = 1;
        if (coopfarm::SpotReleaseDue(r.state, erased) != 0) { r.releaseDue = 1; SpSetState(r, coopfarm::kSpotNone, now); }
    }
    ::LeaveCriticalSection(&g_fmcs);
    ::InterlockedIncrement64(found != 0 ? &g_spLeft : &g_spLeftNoRow);
}

/* ANY THREAD: the worker set's erase (0x28D400). The hand's ids are read BEFORE the engine's erase (the hand may be the node's own
   value, freed by it); only a production building's set (+0x3D0) and only while a granted spot exists here. */
long long detour_erase(void* set, void* hand)
{
    unsigned int ids[4];
    int isNull = 0, have = 0;
    if (::InterlockedCompareExchange(&g_spGrantedN, 0, 0) != 0 && set != 0 && hand != 0
        && ProductionBuildingIs((const char*)set - kBldWorkerSet) != 0 && SpHandIds(hand, ids, &isNull) != 0 && isNull == 0) have = 1;
    const long long n = orig_erase(set, hand);
    if (have == 0 || n <= 0) return n;
    char key[64];
    if (ObjectPositionKey((char*)set - kBldWorkerSet, key, (int)sizeof(key), 0, 3, 1) == 0) return n;
    SpNoteLeft(key, ids, n);
    return n;
}

/* MAIN THREAD, K2, crash-guarded: the engine's own take / erase on this game's building for a hand (bypassing the detours).
   Take: 1 taken, 0 full, -1 faulted. Erase: the entries erased, -1 faulted. */
int SpTakeRun(void* b, void* hand)
{
    __try { return (orig_take(b, hand) & 0xFF) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
long long SpEraseRun(void* b, void* hand)
{
    __try { return orig_erase((char*)b + kBldWorkerSet, hand); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int SpCopyHand(unsigned char* out, const void* who)
{
    __try { std::memcpy(out, (const char*)who + kObjHandle, kHandBytes); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* MAIN THREAD (FarmTick, every 500 ms and on the next frame after a new row): every row's runner read (ProdWriterDecide: the three-way answer and the runner's slot from
   one ask); asks, keep-alive asks and releases sent to the runner; a row not touched for kFmForgetMs is dropped unless its spot is
   granted or its release is still owed (an owed release whose runner stays unknown kFmForgetMs is dropped: the runner frees the spot
   itself when its copy goes, a counted spot when it is no longer asked for). T-853 #1: each row adds at most one release and one ask
   a round, so out holds 2 x kSpCap and every write into it is bounded. */
void SpSendRound(DWORD now)
{
    static SpRow cand[kSpCap];
    static int candW[kSpCap], candSlot[kSpCap];
    static SpRow out[2 * kSpCap];
    static int outKind[2 * kSpCap];
    const int outCap = 2 * kSpCap;
    int nCand = 0, nOut = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_spHigh && nCand < kSpCap; ++j) if (g_sp[j].used != 0) cand[nCand++] = g_sp[j];
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < nCand; ++k)   /* the runner, asked outside the lock (engine reads on the main thread) */
    {
        void* b = FmResolve(std::string(cand[k].key));
        candSlot[k] = -1;
        candW[k] = (b != 0) ? ProdWriterDecide(b, &candSlot[k]) : coopfarm::kProdWriterNone;
    }
    ::EnterCriticalSection(&g_fmcs);
    for (int k = 0; k < nCand; ++k)
    {
        const int j = SpFindLocked(cand[k].key, cand[k].hash, cand[k].uid);
        if (j < 0) continue;
        SpRow& r = g_sp[j];
        r.writer = candW[k];
        if (r.writer == coopfarm::kProdWriterHere)
        {   /* this game runs it now: its own take counts the worker; nothing is owed to another game (the old runner frees its spot
               once it no longer runs the building) */
            if (r.state != coopfarm::kSpotNone) SpSetState(r, coopfarm::kSpotNone, now);
            r.askDue = 0; r.releaseDue = 0;
        }
        if (coopfarm::SpotRegrantDue(r.state, r.grantSlot, candSlot[k]) != 0)
        {   /* T-853 #5: the machine passed to another runner - asked again of it (the old one frees its spot and says so) */
            SpSetState(r, coopfarm::kSpotAsked, now);
            r.askDue = 1; r.grantSlot = -1; ++g_spRegrant;
        }
        else if (r.askDue == 0 && coopfarm::SpotKeepAskDue(r.state, (unsigned int)(now - r.askAt)) != 0) { r.askDue = 1; ++g_spKeepAsk; }
        if (r.releaseDue != 0)
        {   /* T-853 #2: sent only to a named runner; with none named now it stays owed */
            const int route = coopfarm::SpotReleaseRoute(r.writer);
            if (route < 0) r.releaseDue = 0;
            else if (route > 0 && nOut < outCap) { r.releaseDue = 0; out[nOut] = r; outKind[nOut] = coopfarm::kSpotRelease; ++nOut; }
        }
        if (r.askDue != 0 && r.writer == coopfarm::kProdWriterOther && nOut < outCap)
        {
            r.askDue = 0; r.askAt = now;
            out[nOut] = r; outKind[nOut] = coopfarm::kSpotAsk; ++nOut;
        }
        if (r.state != coopfarm::kSpotGranted && r.askDue == 0 && now - r.seenAt > kFmForgetMs
            && (r.releaseDue == 0 || (r.writer == coopfarm::kProdWriterNone && now - r.at > kFmForgetMs))) r.used = 0;
    }
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < nOut; ++k)
    {
        coopfarm::FarmMsg m;
        m.kind = (unsigned char)outKind[k]; m.key = out[k].key; m.uid = out[k].uid;
        const bool ok = net::SendFarm(m);
        if (outKind[k] == coopfarm::kSpotAsk)
        {
            if (ok) { ++g_spAskSent; if (g_spAskSent <= 3) { char h[160]; std::sprintf(h, " uid=%u", out[k].uid); SpSay(std::string("[SPOT] SPOT_ASK sent key=") + out[k].key + h); } }
            else ++g_spAskFail;   /* the row stays asked: refused until kSpotAskGiveUpMs, then asked again */
        }
        else if (ok) ++g_spRelSent;
        else
        {   /* not sent: owed again next round */
            ++g_spRelFail;
            ::EnterCriticalSection(&g_fmcs);
            const int j = SpFindLocked(out[k].key, out[k].hash, out[k].uid);
            if (j >= 0) g_sp[j].releaseDue = 1;
            ::LeaveCriticalSection(&g_fmcs);
        }
    }
}

/* MAIN THREAD: SPOT_ANSWER onto the asking row (coopfarm::SpotAnswerApply). T-853: a yes records the answering game's slot (a later
   change of runner asks again - SpotRegrantDue); a revoke from that game sends the row back to asked (asked again next round); a
   yes that changed no row is released at once (coopfarm::SpotStrayYesRelease) - the runner took a spot nobody here will use. */
void SpOnAnswer(const coopfarm::FarmMsg& m, DWORD now, unsigned int fromPeer)
{
    ++g_spAnsRecv;
    int applied = 0, found = 0, st = coopfarm::kSpotNone;
    const int fromSlot = net::PlayerSlotOfKey(fromPeer);
    ::EnterCriticalSection(&g_fmcs);
    const int i = SpFindLocked(m.key.c_str(), FmHash(m.key.c_str()), m.uid);
    if (i >= 0)
    {
        SpRow& r = g_sp[i];
        found = 1; st = r.state;
        const int lost = coopfarm::SpotGrantLost(st, m.spot);
        const int notGranter = ((lost != 0 || m.spot == coopfarm::kSpotRevoked) && r.grantSlot >= 0 && fromSlot != r.grantSlot) ? 1 : 0;
        const int ns = (notGranter != 0) ? st : coopfarm::SpotAnswerApply(st, m.spot);
        if (ns != st)
        {
            SpSetState(r, ns, now);
            applied = 1;
            if (ns == coopfarm::kSpotGranted) r.grantSlot = fromSlot;
            if (lost != 0) { r.askDue = 1; r.grantSlot = -1; }
        }
    }
    ::LeaveCriticalSection(&g_fmcs);
    if (applied == 0)
    {
        if (found != 0 && st == coopfarm::kSpotGranted && m.spot == coopfarm::kSpotYes) { ++g_spAnsKeep; return; }   /* a keep-alive ask's yes */
        ++g_spAnsStray;
        if (coopfarm::SpotStrayYesRelease(found, net::IsUidMine(m.uid) ? 1 : 0, st, m.spot) != 0)
        {
            coopfarm::FarmMsg rel;
            rel.kind = coopfarm::kSpotRelease; rel.key = m.key; rel.uid = m.uid;
            if (net::SendFarm(rel)) ++g_spStrayRel; else ++g_spRelFail;
        }
        return;
    }
    if (m.spot == coopfarm::kSpotYes) ++g_spAnsYes; else if (m.spot == coopfarm::kSpotFull) ++g_spAnsFull;
    else if (m.spot == coopfarm::kSpotNoCopy) ++g_spAnsNoCopy; else ++g_spAnsRevoked;
    if (g_spAnsRecv <= 3)
    {
        char h[120];
        std::sprintf(h, " uid=%u result=%d", m.uid, (int)m.spot);
        SpSay("[SPOT] SPOT_ANSWER applied key=" + m.key + h);
    }
}

/* MAIN THREAD, the runner (T-853 #5): a spot it freed for any reason but a release is told to the asker (SPOT_ANSWER revoked) */
void SpRevokeSend(const std::string& key, unsigned int uid)
{
    coopfarm::FarmMsg m;
    m.kind = coopfarm::kSpotAnswer; m.key = key; m.uid = uid; m.spot = coopfarm::kSpotRevoked;
    if (net::SendFarm(m)) ++g_srRevokeSent; else ++g_srRevokeFail;   /* not sent: the asker's keep-alive ask is answered full */
}

/* K2 (T-853 #3), the asking game: a granted spot whose worker is not in the building's set coopfarm::kSpotGrantUseMs after the grant
   is released - the worker walked off before the yes came, left the set by a road the erase detour does not see, or the building is
   not loaded here any more. Once a second, only while a grant exists; the set walk runs here, where the AI worker is idle. */
void SpUnusedSweep(DWORD now)
{
    if (::InterlockedCompareExchange(&g_spGrantedN, 0, 0) == 0 || now - g_spUnusedAt < 1000) return;
    g_spUnusedAt = now;
    static SpRow cand[kSpCap];
    int n = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_spHigh && n < kSpCap; ++j)
        if (g_sp[j].used != 0 && coopfarm::SpotGrantUnused(g_sp[j].state, 0, (unsigned int)(now - g_sp[j].at)) != 0) cand[n++] = g_sp[j];
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < n; ++k)
    {
        void* b = FmResolve(std::string(cand[k].key));
        const int listed = (b != 0) ? SpListed(b, cand[k].ids) : 0;
        if (listed != 0) continue;
        ::EnterCriticalSection(&g_fmcs);
        const int j = SpFindLocked(cand[k].key, cand[k].hash, cand[k].uid);
        if (j >= 0 && g_sp[j].at == cand[k].at && coopfarm::SpotGrantUnused(g_sp[j].state, listed, (unsigned int)(now - g_sp[j].at)) != 0)
        {
            g_sp[j].releaseDue = 1;
            SpSetState(g_sp[j], coopfarm::kSpotNone, now);
            ++g_spUnusedRel;
        }
        ::LeaveCriticalSection(&g_fmcs);
    }
}

/* MAIN THREAD: the runner's row of (key, uid), -1 none */
int SrFind(const std::string& key, unsigned int uid)
{
    for (size_t i = 0; i < g_spRun.size(); ++i) if (g_spRun[i].uid == uid && g_spRun[i].key == key) return (int)i;
    return -1;
}

/* MAIN THREAD, K2: the runner answers asks and frees released spots; once a second it frees the spots whose copy is gone (a counted
   spot: not asked for coopfarm::kSpotCountedForgetMs), whose building is no longer loaded, or that it no longer runs
   (coopfarm::SpotRunnerFreeRow), and tells the asker (kSpotRevoked). The asking side's unused grants are released (SpUnusedSweep). */
void SpDrain(DWORD now)
{
    SpUnusedSweep(now);
    std::vector<SpIn> in;
    in.swap(g_inSpot);
    for (size_t k = 0; k < in.size(); ++k)
    {
        const SpIn& x = in[k];
        void* b = FmResolve(x.key);
        const int runs = (b != 0 && ProductionBuildingIs(b) != 0 && ProdStepWriterHere(b) == coopfarm::kProdWriterHere) ? 1 : 0;
        const int row = SrFind(x.key, x.uid);
        if (x.kind == coopfarm::kSpotRelease)
        {
            ++g_srRelRecv;
            if (row < 0) { ++g_srRelNoRow; continue; }
            if (g_spRun[row].counted != 0) SpCtAdjust(x.key.c_str(), -1);
            else if (b != 0 && SpEraseRun(b, g_spRun[row].hand) < 0) ++g_srFault;
            g_spRun.erase(g_spRun.begin() + row);
            ++g_srFreedRel;
            continue;
        }
        ++g_srAskRecv;
        void* who = (runs != 0) ? (void*)FindSpawned(x.uid) : 0;
        int took = 0;
        if (runs != 0 && row >= 0)
        {   /* already held for that worker (a repeat or keep-alive ask): a counted spot stands; a copy's is taken again (the engine's
               take answers 1 for a listed hand) - with the copy gone the sweep frees it and says so */
            g_spRun[row].askAt = now;
            if (g_spRun[row].counted != 0) took = 1;
            else if (who != 0)
            {
                const int t = SpTakeRun(b, (char*)who + kObjHandle);
                if (t < 0) ++g_srFault;
                took = (t > 0) ? 1 : 0;
            }
        }
        else if (runs != 0)
        {
            const int ct = SpCtCount(x.key.c_str());
            unsigned long long cnt = 0;
            int lim = 0;
            const int room = (SpRoomRead(b, &cnt, &lim) != 0) ? coopfarm::SpotRoomLeft(cnt, ct, lim) : 0;
            if ((int)g_spRun.size() >= kSpRunCap) ++g_srCapFull;   /* answered full: no room to keep it */
            else if (who != 0 && (ct == 0 || room != 0))
            {   /* the engine's own take with this game's copy (its own test is the set's count against the limit; the counted spots,
                   which it does not know, were checked above) */
                SpRun r;
                r.key = x.key; r.uid = x.uid; r.counted = 0; r.askAt = now;
                const int t = (SpCopyHand(r.hand, who) != 0) ? SpTakeRun(b, (char*)who + kObjHandle) : -1;
                if (t < 0) ++g_srFault;
                took = (t > 0) ? 1 : 0;
                if (took != 0) g_spRun.push_back(r);
            }
            else if (who == 0 && room != 0)
            {   /* T-853 #4: no copy of that worker here - a COUNTED spot (before, "no copy" refused it every 5 s for ever) */
                SpRun r;
                r.key = x.key; r.uid = x.uid; r.counted = 1; r.askAt = now;
                std::memset(r.hand, 0, sizeof(r.hand));
                g_spRun.push_back(r);
                SpCtAdjust(x.key.c_str(), 1);
                took = 1;
                ++g_srCounted;
            }
            else if (who == 0) ++g_srCountedFull;
        }
        const int ans = coopfarm::SpotAnswerFor(runs, took);
        if (ans < 0) { ++g_srNotRunner; continue; }
        if (ans == coopfarm::kSpotYes) ++g_srYes; else ++g_srFull;
        coopfarm::FarmMsg m;
        m.kind = coopfarm::kSpotAnswer; m.key = x.key; m.uid = x.uid; m.spot = (unsigned char)ans;
        if (!net::SendFarm(m)) ++g_srAnsFail;   /* the asker asks again after coopfarm::kSpotAskGiveUpMs */
        if (g_srAskRecv <= 3)
        {
            char h[120];
            std::sprintf(h, " uid=%u result=%d held=%d", x.uid, ans, (int)g_spRun.size());
            SpSay("[SPOT] SPOT_ASK answered key=" + x.key + h);
        }
    }
    if (g_spRun.empty() || now - g_spSweepAt < 1000) return;
    g_spSweepAt = now;
    for (size_t i = 0; i < g_spRun.size(); )
    {
        void* b = FmResolve(g_spRun[i].key);
        const int counted = g_spRun[i].counted;
        int freeIt = 0;
        if (b == 0) { freeIt = 1; ++g_srFreedUnloaded; }   /* its set went with the building */
        else
        {
            const int runs = (ProdStepWriterHere(b) == coopfarm::kProdWriterHere) ? 1 : 0;
            const int gone = (counted == 0 && FindSpawned(g_spRun[i].uid) == 0) ? 1 : 0;
            if (coopfarm::SpotRunnerFreeRow(0, counted, gone, (unsigned int)(now - g_spRun[i].askAt), runs) != 0)
            {
                freeIt = 1;
                if (counted == 0 && SpEraseRun(b, g_spRun[i].hand) < 0) ++g_srFault;
                if (runs == 0) ++g_srFreedNotRunner; else if (counted != 0) ++g_srFreedCounted; else ++g_srFreedGone;
            }
        }
        if (freeIt == 0) { ++i; continue; }
        if (counted != 0) SpCtAdjust(g_spRun[i].key.c_str(), -1);
        SpRevokeSend(g_spRun[i].key, g_spRun[i].uid);   /* T-853 #5: the asker asks again (the new runner, or this one later) */
        g_spRun.erase(g_spRun.begin() + i);
    }
}

/* The engine's own early-out: the tail call alone (0xE5540 lines 22-27). */
void FmTail(void* f)
{
    __try
    {
        void** vt = *(void***)f;
        FmVoidFn fn = (FmVoidFn)vt[kFmVtTail / sizeof(void*)];
        fn(f);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ::InterlockedIncrement64(&g_tailFault); }
}

void detour_update(void* f)
{
    ::InterlockedIncrement64(&g_upSeen);
    if (RoleIsSingle() || ::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0) { orig_update(f); return; }
    char key[64];
    if (ObjectPositionKey(f, key, (int)sizeof(key), 0, 3, 0) == 0) { ::InterlockedIncrement64(&g_upNoKey); orig_update(f); return; }
    const int v = FmSeen(key);
    if (coopfarm::FarmGate(0, 1, v) == coopfarm::kFarmHold) { ::InterlockedIncrement64(&g_upHeld); FmTail(f); return; }
    ::InterlockedIncrement64(&g_upRan);
    orig_update(f);
}

/* ANY THREAD (par16 re-check #1). An undecided farm's catch-up (elapsed t, flag) kept in its row: 1 held, 2 the verdict
   turned "other game" meanwhile (dropped), 0 run it now (this game writes it, or the row is gone). One per farm: a newer
   catch-up for the same farm replaces the kept one (the replaced one counted dropped). */
int FmHoldTs(const char* key, float t, char flag)
{
    const unsigned int h = FmHash(key);
    int r = 0;
    ::EnterCriticalSection(&g_fmcs);
    const int i = FmFindLocked(key, h);
    if (i >= 0)
    {
        FmEntry& e = g_fm[i];
        if (e.verdict == 0)
        {
            if (e.tsPend != 0) ::InterlockedIncrement64(&g_tsDropped);
            e.tsPend = 1; e.tsT = t; e.tsFlag = flag; e.tsAt = ::GetTickCount(); e.tsLate = 0;
            r = 1;
        }
        else if (coopfarm::FarmGate(0, 1, e.verdict) == coopfarm::kFarmHold) r = 2;
    }
    ::LeaveCriticalSection(&g_fmcs);
    return r;
}

/* fold #6: the catch-up growth is the writer's too - a held copy skips it whole (it would grow the copy and take its water
   items). par16 re-check #1: an UNDECIDED farm (the usual case - timeSkip runs inside the farm's load, before FarmTick) holds
   it too and keeps (elapsed, flag); FarmTick decides: this game writes it -> replayed at the K2 safe point (FarmDrain), another
   game writes it -> dropped. The engine's fourth register is not an argument (see THE READ): the replay passes 0. */
void detour_timeskip(void* f, float t, char flag, void* p4)
{
    ::InterlockedIncrement64(&g_tsSeen);
    if (RoleIsSingle() || ::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0) { orig_timeskip(f, t, flag, p4); return; }
    char key[64];
    if (ObjectPositionKey(f, key, (int)sizeof(key), 0, 3, 0) == 0) { orig_timeskip(f, t, flag, p4); return; }
    const int v = FmSeen(key);
    if (v == 0)
    {   /* H3: undecided - kept for the writer FarmTick names; 0 back = this game was named the writer meanwhile: run */
        const int held = FmHoldTs(key, t, flag);
        if (held == 1) { ::InterlockedIncrement64(&g_tsHeldUndecided); return; }
        if (held == 2) { ::InterlockedIncrement64(&g_tsHeld); return; }
    }
    else if (coopfarm::FarmGate(0, 1, v) == coopfarm::kFarmHold) { ::InterlockedIncrement64(&g_tsHeld); return; }
    ::InterlockedIncrement64(&g_tsRan);
    orig_timeskip(f, t, flag, p4);
}

void detour_operate(void* f, void* who, float amount)
{
    if (RoleIsSingle() || ::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0) { orig_operate(f, who, amount); return; }
    char key[64];
    if (ObjectPositionKey(f, key, (int)sizeof(key), 0, 3, 0) == 0) { orig_operate(f, who, amount); return; }
    /* T-610: a worker that is a COPY of another game's character is neither run nor relayed here (its own game runs or relays
       that work as FARM_OP) - as detour_prodOperate; the any-thread copy test, since operate runs on the AI worker thread (#9) */
    const unsigned int whoUid = (who != 0) ? FindSpawnedUid(who) : 0u;
    if (whoUid != 0 && coopfarm::MineWorkerSkip(0, coopfarm::WorkerIsCopy(whoUid, net::IsUidMineAnyThread(whoUid) ? 1 : 0,
                                                                          net::OwnedMirrorExactAnyThread() ? 1 : 0)) != 0)
    { ::InterlockedIncrement64(&g_opCopySkipped); return; }
    const int v = FmSeen(key);
    if (coopfarm::FarmGate(0, 1, v) == coopfarm::kFarmHold)
    {   /* NOT a local harvest: the WORK (amount x this frame's g_dt, fold #2) and the worker (uid + skill, fold #1) go to the
           writer (FarmTick sends them as FARM_OP) */
        ::InterlockedIncrement64(&g_opHeld);
        const float work = amount * FmDt();
        FmAddOp(key, (who != 0) ? FindSpawnedUid(who) : 0u, FmSkillOf(who), work);
        return;
    }
    ::InterlockedIncrement64(&g_opLocal);
    orig_operate(f, who, amount);
}

/* fold #1: getYieldChancePerCrop's skill argument replaced ONLY for the farm and thread the writer armed around its own
   operate call for a forwarded worker it has no copy of */
float detour_yield(void* f, float skill01)
{
    if (f != 0 && f == g_yOverFarm && ::GetCurrentThreadId() == g_yOverThread)
    {
        ::InterlockedIncrement64(&g_yieldOverridden);
        skill01 = g_yOverVal;
    }
    return orig_yield(f, skill01);
}

struct FmTsDue { char key[64]; float t; char flag; };   /* MAIN THREAD: a held catch-up due (re-check #1) */

/* MAIN THREAD, K2 (par16 re-check #1): a held catch-up replayed on the farm the key resolved to. 1 ran, 0 faulted. */
int FmReplayTs(void* b, float t, char flag)
{
    __try { orig_timeskip(b, t, flag, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* MAIN THREAD, K2 (par16 re-check #3): one worker's queued work through the engine's own operate, at most `budget` calls,
   inside a fault guard so the caller's yield-override clear after it runs on every path. Returns the calls made; *fault = 1
   when the engine faulted (the worker's remaining work is dropped by the caller). */
int FmRunWork(void* b, void* who, float* work, float dt, int budget, int* fault)
{
    volatile int calls = 0;
    *fault = 0;
    __try
    {
        while (calls < budget)
        {
            float amt = 0.0f;
            const float w = coopfarm::FarmWorkSlice(*work, dt, kFmOpChunk, &amt);
            if (!(w > 0.0f)) break;
            orig_operate(b, who, amt);
            *work -= w;
            if (*work < 1.0e-7f) *work = 0.0f;
            ++calls;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = 1; }
    return calls;
}

/* ---- POD reads and writes of one farm (MAIN THREAD / K2) ---- */
struct FmPod
{
    float grown, died, cleared, growStart;
    int harvested, prodState, hasBar;
    float bar;
    float marker[3];
    int n;
    float ages[coopfarm::kFarmMaxPlants];
    int nIn;
    float inStock[coopfarm::kFarmMaxInputs], inRate[coopfarm::kFarmMaxInputs];
};
FmPod g_pod;   /* MAIN THREAD scratch (2 KB) */

int FmReadPod(void* b, FmPod* p)
{
    __try
    {
        const char* c = (const char*)b;
        const unsigned int n = *(const unsigned int*)(c + kFmPlantCount);
        if (n > coopfarm::kFarmMaxPlants) return 0;
        const char* data = *(char* const*)(c + kFmPlantData);
        if (n > 0 && data == 0) return 0;
        p->n = (int)n;
        for (unsigned int i = 0; i < n; ++i) p->ages[i] = *(const float*)(data + (size_t)i * kFmPlantStride);
        p->grown = *(const float*)(c + kFmGrown);
        p->died = *(const float*)(c + kFmDied);
        p->cleared = *(const float*)(c + kFmCleared);
        p->growStart = *(const float*)(c + kFmGrowStart);
        p->harvested = *(const int*)(c + kFmHarvested);
        p->prodState = *(const int*)(c + kFmProdState);
        const float* bar = *(float* const*)(c + kFmBar);
        p->hasBar = (bar != 0) ? 1 : 0;
        p->bar = (bar != 0) ? *bar : 0.0f;
        for (int k = 0; k < 3; ++k) p->marker[k] = *(const float*)(c + kFmMarker + 4 * (size_t)k);
        /* fold #7: the water. More records than the wire carries = none carried (counted), never a truncated set. */
        const unsigned int ni = *(const unsigned int*)(c + kFmInCount);
        const char* in = *(char* const*)(c + kFmInData);
        p->nIn = 0;
        if (ni > coopfarm::kFarmMaxInputs) ++g_inTooMany;
        else if (ni > 0 && in != 0)
        {
            for (unsigned int i = 0; i < ni; ++i)
            {
                p->inStock[i] = *(const float*)(in + (size_t)i * kFmInStride);
                p->inRate[i] = *(const float*)(in + (size_t)i * kFmInStride + 4);
            }
            p->nIn = (int)ni;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* Writes `n` ages and the state and the water, zeroes the parts of every hidden plant the way destroyAPlant does (fold #3),
   then - only while the engine's own guards hold (+0x160 built, +0x508 mesh, +0x4D0 material: fold #4) - the engine's own
   visual refresh (updatePlantInstance per plant, updateMaterial - what update's own growth branch calls, 0xE5540 lines
   121-128). 1 done, 2 written without the refresh, 0 faulted. */
int FmWritePod(void* b, const FmPod* p, int n, int* hidden, int* water)
{
    int refreshed = 0;
    __try
    {
        char* c = (char*)b;
        const int local = (int)*(const unsigned int*)(c + kFmPlantCount);
        char* data = *(char**)(c + kFmPlantData);
        if (data == 0 || local <= 0) return 0;
        if (n > local) n = local;
        for (int i = 0; i < n; ++i)
        {
            char* plant = data + (size_t)i * kFmPlantStride;
            *(float*)plant = p->ages[i];
            if (coopfarm::FarmPlantHidden(p->ages[i]) == 0) continue;
            const unsigned int parts = *(const unsigned int*)(plant + kFmPlantParts);
            char* pd = *(char**)(plant + kFmPlantPartData);
            if (pd == 0 || parts > 4096) continue;
            for (unsigned int j = 0; j < parts; ++j)
            {
                char* part = pd + (size_t)j * kFmPartStride;
                const char* def = *(char* const*)(part + kFmPartDef);
                if (def != 0 && *(const char*)(def + kFmPartDefKeep) == 0) *(float*)part = 0.0f;
            }
            if (hidden) ++*hidden;
        }
        *(float*)(c + kFmGrown) = p->grown;
        *(float*)(c + kFmDied) = p->died;
        *(float*)(c + kFmCleared) = p->cleared;
        *(float*)(c + kFmGrowStart) = p->growStart;
        *(int*)(c + kFmHarvested) = (p->harvested > local) ? local : p->harvested;
        *(int*)(c + kFmProdState) = p->prodState;
        float* bar = *(float**)(c + kFmBar);
        if (bar != 0 && p->hasBar != 0) *bar = p->bar;
        for (int k = 0; k < 3; ++k) *(float*)(c + kFmMarker + 4 * (size_t)k) = p->marker[k];
        const unsigned int ni = *(const unsigned int*)(c + kFmInCount);
        char* in = *(char**)(c + kFmInData);
        if (in != 0 && p->nIn > 0 && ni <= coopfarm::kFarmMaxInputs)
        {
            const int m = ((int)ni < p->nIn) ? (int)ni : p->nIn;
            for (int i = 0; i < m; ++i)
            {
                *(float*)(in + (size_t)i * kFmInStride) = p->inStock[i];
                *(float*)(in + (size_t)i * kFmInStride + 4) = p->inRate[i];
            }
            if (water) *water = m;
        }
        if (*(const char*)(c + kFmPhysical) != 0 && *(void* const*)(c + kFmMesh) != 0 && *(void* const*)(c + kFmMaterial) != 0)
        {
            const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
            if (kFarmPlantRva != 0)
            {
                FmPlantFn pf = (FmPlantFn)(base + (uintptr_t)kFarmPlantRva);
                for (int i = 0; i < local; ++i) pf(b, data + (size_t)i * kFmPlantStride);
            }
            if (kFarmMaterialRva != 0) ((FmVoidFn)(base + (uintptr_t)kFarmMaterialRva))(b);
            refreshed = 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return refreshed ? 1 : 2;
}

void FmPodToMsg(const FmPod& p, const std::string& key, coopfarm::FarmMsg* m)
{
    m->kind = coopfarm::kFarmState; m->key = key;
    m->grown = p.grown; m->died = p.died; m->cleared = p.cleared; m->growStart = p.growStart;
    m->harvested = p.harvested; m->prodState = p.prodState; m->hasProgress = (unsigned char)(p.hasBar ? 1 : 0); m->progress = p.bar;
    for (int k = 0; k < 3; ++k) m->marker[k] = p.marker[k];
    m->ages.assign(p.ages, p.ages + p.n);
    m->inStock.assign(p.inStock, p.inStock + p.nIn);
    m->inRate.assign(p.inRate, p.inRate + p.nIn);
}

void FmMsgToPod(const coopfarm::FarmMsg& m, FmPod* p)
{
    p->grown = m.grown; p->died = m.died; p->cleared = m.cleared; p->growStart = m.growStart;
    p->harvested = m.harvested; p->prodState = m.prodState; p->hasBar = m.hasProgress; p->bar = m.progress;
    for (int k = 0; k < 3; ++k) p->marker[k] = m.marker[k];
    p->n = (int)m.ages.size();
    for (int i = 0; i < p->n; ++i) p->ages[i] = m.ages[i];
    p->nIn = (int)m.inStock.size();
    for (int i = 0; i < p->nIn; ++i) { p->inStock[i] = m.inStock[i]; p->inRate[i] = m.inRate[i]; }
}

struct FmCopy { char key[64]; unsigned int hash; int idx; int verdict; DWORD seenAt; };
FmCopy g_cp[kFmCap];   /* MAIN THREAD scratch */
int FmSnapshot(FmCopy* out)
{
    int k = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int i = 0; i < g_fmHigh; ++i)
        if (g_fm[i].used != 0)
        {
            std::memcpy(out[k].key, g_fm[i].key, sizeof(out[k].key));
            out[k].hash = g_fm[i].hash; out[k].idx = i; out[k].verdict = g_fm[i].verdict; out[k].seenAt = g_fm[i].seenAt; ++k;
        }
    ::LeaveCriticalSection(&g_fmcs);
    return k;
}

const char* FmVerdictWord(int v) { return v == 1 ? "this-game" : (v == 2 ? "other-game" : "undecided"); }

/* ANY THREAD (T-853 #7): how long the ladder has left the farm at `key` undecided (FarmTick times it), 0 when it answers */
unsigned int FmUndecidedMs(const char* key)
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return 0;
    const unsigned int h = FmHash(key);
    const DWORD now = ::GetTickCount();
    unsigned int ms = 0;
    ::EnterCriticalSection(&g_fmcs);
    const int i = FmFindLocked(key, h);
    if (i >= 0 && g_fm[i].undecAt != 0) ms = (unsigned int)(now - g_fm[i].undecAt);
    ::LeaveCriticalSection(&g_fmcs);
    return ms;
}

/* MAIN THREAD (T-853 #6 / #7): THE ONE ANSWER every farm site takes - who writes the farm and that game's slot from ONE ask of the
   ladder (items.cpp FarmWriterDecide: FarmWriterHere's verdict and the slot it names - the owner rung's, else the area holder's).
   Before, the slot came from a second ladder (ItemRequestTargetSlot) and a disagreement held FARM updates until they dropped. A farm
   the ladder has left undecided for coopfarm::kFarmUndecidedMs falls to the game that holds its area (coopfarm::FarmFallbackWriter).
   1 this game, 2 another game (*slot = its slot, -1 not known now), 0 nobody now. */
int FmWriterNow(const std::string& key, void* b, int* slot)
{
    if (slot != 0) *slot = -1;
    if (b == 0) return 0;
    FarmWriterView v;
    FarmWriterDecide(b, &v);
    if (v.verdict != 0) { if (slot != 0) *slot = v.slot; return v.verdict; }
    const int fb = coopfarm::FarmFallbackWriter(FmUndecidedMs(key.c_str()), v.byOwner, v.areaSlot, v.areaHere);
    if (fb == coopfarm::kFarmFallHere) return 1;
    if (fb == coopfarm::kFarmFallOther) { if (slot != 0) *slot = v.areaSlot; return 2; }
    return 0;
}

/* MAIN THREAD (M3): is a FARM from `fromPeer` from the farm's named writer (coopfarm::FarmFromWriter); wait while the farm does
   not resolve here or this game names no other game its writer */
int FmSenderJudge(const std::string& key, unsigned int fromPeer)
{
    void* b = FmResolve(key);
    int ws = -1;
    if (b == 0 || FmWriterNow(key, b, &ws) != 2) return coopfarm::kFarmFromWait;
    return coopfarm::FarmFromWriter(ws, net::PlayerSlotOfKey(fromPeer));
}

void FmSenderSay(const std::string& key, unsigned int fromPeer)
{
    if (coopfarm::MineSayDue(g_applyRefusedSender) == 0) return;
    int ws = -1;
    FmWriterNow(key, FmResolve(key), &ws);
    char h[200];
    std::sprintf(h, " from slot %d: this game names slot %d its writer; refusedSender=%lld", net::PlayerSlotOfKey(fromPeer), ws,
                 g_applyRefusedSender);
    DebugLog("[FARM] FARM refused key=" + key + h);
}

/* MAIN THREAD: (uid, skill, work) into a writer's queue for one farm */
void FmOpMerge(FmOpIn* in, unsigned int uid, float skill, float work)
{
    for (size_t k = 0; k < in->w.size(); ++k)
        if (in->w[k].uid == uid && (uid != 0 || in->w[k].skill == skill))
        {
            in->w[k].work += work;
            if (in->w[k].work > kFmOpCap) in->w[k].work = kFmOpCap;
            return;
        }
    if ((int)in->w.size() >= kFmOpInCap)
    {
        FmWork& last = in->w.back();
        last.work += work; if (last.work > kFmOpCap) last.work = kFmOpCap;
        ::InterlockedIncrement64(&g_opMerged);
        return;
    }
    FmWork x; x.uid = uid; x.skill = skill; x.work = (work > kFmOpCap) ? kFmOpCap : work;
    in->w.push_back(x);
}

/* the lever's farm: `min` = the smallest decided key (both games that load the same farms pick the same one), else the one
   key containing the substring. "" = none / ambiguous (the reason in *why). */
std::string FmPick(const std::string& sub, std::string* why)
{
    const int n = FmSnapshot(g_cp);
    std::string best;
    int hits = 0;
    for (int i = 0; i < n; ++i)
    {
        const std::string k(g_cp[i].key);
        if (sub == "min") { if (g_cp[i].verdict != 0 && (best.empty() || k < best)) best = k; continue; }
        if (k.find(sub) != std::string::npos) { best = k; ++hits; }
    }
    if (sub != "min" && hits > 1) { *why = "ambiguous (" + std::string(1, (char)('0' + (hits > 9 ? 9 : hits))) + "+ keys)"; return ""; }
    if (best.empty()) *why = "no farm seen here matches";
    return best;
}

/* lever2 (TEST LEVER ONLY). What ONE CROP costs on this farm, from the engine's own operate 0xE59A0 (Read, the harvest
   branch): bar(*(+0x448)) += vt+0x368(farm) x C x g_dt x amount x harvestRate(+0x538); a crop is made at bar >= 1.0 (the
   constant at 0x167B308 - Confirmed 1.0 in the image's .rdata) and the bar goes back to 0 (the rest of that call's work is
   lost). So one crop is 1.0 / (mult x C x rate) of WORK (work = amount x g_dt, what FARM_OP carries). C (0x2132ED4) is a
   zero-initialised .data global the game sets at run time, so it is read through operate's own instruction that loads it
   (operate+0xBC: movss xmm1,[rip+d32], bytes F3 0F 10 0D checked - Confirmed in the image, the target 0x2132ED4).
   Also the gate's death limit +0x54C. 1 read; 0 not (*why: 1 no operate address, 2 bytes differ, 3 fault, 4 a factor <= 0). */
int FmCropCost(void* b, float* mult, float* c, float* rate, float* deathLimit, float* workPerCrop, int* why)
{
    *mult = 0.0f; *c = 0.0f; *rate = 0.0f; *deathLimit = 0.0f; *workPerCrop = 0.0f; *why = 0;
    if (kFarmOperateRva == 0) { *why = 1; return 0; }
    __try
    {
        const unsigned char* ins = (const unsigned char*)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kFarmOperateRva + 0xBC);
        if (ins[0] != 0xF3 || ins[1] != 0x0F || ins[2] != 0x10 || ins[3] != 0x0D) { *why = 2; return 0; }
        const int d = *(const int*)(ins + 4);
        *c = *(const float*)(ins + 8 + d);
        *rate = *(const float*)((const char*)b + 0x538);
        *deathLimit = *(const float*)((const char*)b + 0x54C);
        void** vt = *(void***)b;
        *mult = ((FmMultFn)vt[0x368 / sizeof(void*)])(b);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *why = 3; return 0; }
    const float p = *mult * *c * *rate;
    if (!(p > 0.0f && p < 1.0e9f)) { *why = 4; return 0; }
    *workPerCrop = 1.0f / p;
    return 1;
}

/* lever2: the operate gate's words (0xE59A0, the harvest branch's condition: a bar, ripe, not dead, a plant left, the box
   not full) */
void FmGateWords(const FmPod& p, float deathLimit, char* out, size_t cap)
{
    _snprintf(out, cap - 1, "gate[bar,ripe,alive,plantLeft,boxNotFull]=%d,%d,%d,%d,%d(grown=%.3f died=%.3f limit=%.3f prodState=%d bar=%.3f)",
              p.hasBar, p.grown >= 1.0f ? 1 : 0, p.died < deathLimit ? 1 : 0, p.harvested < p.n ? 1 : 0, p.prodState != 1 ? 1 : 0,
              p.grown, p.died, deathLimit, p.prodState, p.bar);
    out[cap - 1] = 0;
}
int FmGateOpen(const FmPod& p, float deathLimit)
{
    return (p.hasBar != 0 && p.grown >= 1.0f && p.died < deathLimit && p.harvested < p.n && p.prodState != 1) ? 1 : 0;
}

void FmRunLever()
{
    const int kind = g_ftKind; g_ftKind = 0;
    if (kind == 0) return;
    ++g_testRuns;
    std::string why;
    const std::string key = FmPick(g_ftSub, &why);
    char h[512];
    if (key.empty()) { ++g_testRefused; DebugLog("[FARM] farmtest REFUSED: " + why); return; }
    if (orig_operate == 0 || orig_update == 0) { ++g_testRefused; DebugLog("[FARM] farmtest REFUSED: the farm hooks are not installed"); return; }
    void* b = FmResolve(key);
    if (b == 0) { ++g_testRefused; DebugLog("[FARM] farmtest REFUSED key=" + key + ": does not resolve here"); return; }
    if (FmReadPod(b, &g_pod) == 0) { ++g_testRefused; DebugLog("[FARM] farmtest REFUSED key=" + key + ": farm unreadable"); return; }
    const int hBefore = g_pod.harvested;
    if (kind == 1)
    {
        const int w = FarmWriterHere(b);
        if (w != 1)
        {
            ++g_testRefused;
            DebugLog("[FARM] farmtest grow REFUSED key=" + key + ": this game does not write that farm (writer " + FmVerdictWord(w == 2 ? 2 : 0)
                     + ") - run it on the writer's game");
            return;
        }
        const float age = (g_ftVal > 1.0f) ? 1.0f : g_ftVal;
        for (int i = 0; i < g_pod.n; ++i) if (i >= g_pod.harvested) g_pod.ages[i] = age;
        g_pod.grown = g_ftVal; g_pod.died = 0.0f;
        const int ok = FmWritePod(b, &g_pod, g_pod.n, 0, 0);
        std::sprintf(h, "[FARM] farmtest grow key=%s age=%.3f grown=%.3f plants=%d harvested=%d written=%d", key.c_str(), age, g_ftVal,
                     g_pod.n, g_pod.harvested, ok);
        DebugLog(std::string(h));
        return;
    }
    /* harvest (lever2): N CROPS through the engine's own operate and THIS game's gate (the detour). The writer calls it here,
       once per crop, with the amount that fills the bar from where it is (+2%); a held game puts the work of N crops into ONE
       FARM_OP request (the path a held worker's work takes) - the writer runs it in its own 0.25-amount calls over drains. */
    const int want = (int)(g_ftVal + 0.5f);
    float mult = 0.0f, cc = 0.0f, rate = 0.0f, dLim = 0.0f, per = 0.0f;
    int cwhy = 0;
    const int costOk = FmCropCost(b, &mult, &cc, &rate, &dLim, &per, &cwhy);
    int st0 = -1, q0 = -1;
    FarmBoxItemCount(b, &st0, &q0);
    const float dt = FmDt();
    ::EnterCriticalSection(&g_fmcs);
    const int r = FmFindLocked(key.c_str(), FmHash(key.c_str()));
    const int v = (r >= 0) ? g_fm[r].verdict : 0;
    ::LeaveCriticalSection(&g_fmcs);
    char gate[256];
    FmGateWords(g_pod, dLim, gate, sizeof(gate));
    std::sprintf(h, " crop cost: mult(vt+0x368)=%.4f C(0x2132ED4)=%.4f harvestRate(+0x538)=%.4f -> workPerCrop=%.3f (amount x g_dt; %.1f amount at dt=%.4f)",
                 mult, cc, rate, per, (dt > 0.0f) ? per / dt : -1.0f, dt);
    const std::string cost(h);
    if (costOk == 0)
    {
        ++g_testRefused;
        std::sprintf(h, "[FARM] farmtest harvest REFUSED key=%s: the crop cost could not be read (why=%d: 1 no operate address, 2 operate+0xBC bytes differ, 3 fault, 4 a factor <= 0)",
                     key.c_str(), cwhy);
        DebugLog(std::string(h) + cost + " " + gate);
        return;
    }
    if (!(dt > 0.0f)) { ++g_testRefused; DebugLog("[FARM] farmtest harvest REFUSED key=" + key + ": g_dt is 0 (the game is paused)"); return; }
    if (FmGateOpen(g_pod, dLim) == 0) DebugLog("[FARM] farmtest harvest key=" + key + ": the operate gate is CLOSED before the run - " + gate);
    if (v == 2)
    {
        float work = ((float)want - (g_pod.hasBar ? g_pod.bar : 0.0f) + 0.05f) * per;
        const char* capped = "";
        if (work > kFmOpCap) { work = kFmOpCap; capped = " CAPPED at the per-worker request cap kFmOpCap (fewer crops)"; }
        detour_operate(b, 0, work / dt);
        std::sprintf(h, "[FARM] farmtest harvest key=%s crops=%d writer=other-game sent work=%.3f as FARM_OP%s - harvested here %d, box stacks=%d qty=%d"
                     " (the writer harvests; `buildtest farm list` on both games shows harvested/box after its drains)",
                     key.c_str(), want, work, capped, hBefore, st0, q0);
        DebugLog(std::string(h) + cost + " " + gate);
        return;
    }
    int calls = 0, crops = 0;
    std::string stop = "asked crops made";
    while (crops < want)
    {
        if (FmReadPod(b, &g_pod) == 0) { stop = "farm unreadable"; break; }
        if (FmGateOpen(g_pod, dLim) == 0) { FmGateWords(g_pod, dLim, gate, sizeof(gate)); stop = std::string("operate gate closed: ") + gate; break; }
        const int hv = g_pod.harvested;
        const float amount = (1.0f - g_pod.bar + 0.02f) * per / dt;
        detour_operate(b, 0, amount);
        ++calls;
        if (FmReadPod(b, &g_pod) == 0) { stop = "farm unreadable"; break; }
        if (g_pod.harvested != hv + 1)
        {
            FmGateWords(g_pod, dLim, gate, sizeof(gate));
            stop = std::string("a full-bar call did not harvest (") + (g_pod.prodState == 1 ? "the box refused the crop: productionState 1" : "harvested did not step") + ") " + gate;
            break;
        }
        ++crops;
    }
    int hAfter = -1;
    if (FmReadPod(b, &g_pod) != 0) hAfter = g_pod.harvested;
    int st1 = -1, q1 = -1;
    FarmBoxItemCount(b, &st1, &q1);
    std::sprintf(h, "[FARM] farmtest harvest key=%s crops=%d calls=%d writer=%s harvestedHere %d -> %d box stacks %d -> %d qty %d -> %d (run here)",
                 key.c_str(), want, calls, FmVerdictWord(v), hBefore, hAfter, st0, st1, q0, q1);
    DebugLog(std::string(h) + " stop=" + stop + cost);
}

}   /* namespace */

void InstallFarm()
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) != 0) return;
    ::InitializeCriticalSection(&g_fmcs);
    std::memset(g_fm, 0, sizeof(g_fm));
    g_fmHigh = 0;
    ::InterlockedExchange(&g_fmInit, 1);
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    g_hkUpdate = (kFarmUpdateRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kFarmUpdateRva), (void*)&detour_update, (void**)&orig_update) == coop::SUCCESS) ? 1 : -1);
    /* the operate gate needs the growth gate: a held harvest with a growing copy would still be two writers */
    g_hkOperate = (g_hkUpdate != 1) ? -3 : (kFarmOperateRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kFarmOperateRva), (void*)&detour_operate, (void**)&orig_operate) == coop::SUCCESS) ? 1 : -1);
    /* fold #6: the catch-up growth follows the same gate (nothing to hold without the growth gate) */
    g_hkTimeSkip = (g_hkUpdate != 1) ? -3 : (kFarmTimeSkipRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kFarmTimeSkipRva), (void*)&detour_timeskip, (void**)&orig_timeskip) == coop::SUCCESS) ? 1 : -1);
    /* fold #1: the yield override is only for the writer's forwarded work */
    g_hkYield = (g_hkOperate != 1) ? -3 : (kFarmYieldRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kFarmYieldRva), (void*)&detour_yield, (void**)&orig_yield) == coop::SUCCESS) ? 1 : -1);
    char h[700];
    std::sprintf(h, "[FARM] par16 hooks: FarmBuilding::update 0xE5540 %d, FarmBuilding::operate 0xE59A0 %d, timeSkip 0xDF6A0 %d,"
                 " getYieldChancePerCrop 0xDFDD0 %d (1 installed; -1 failed; -2 no address; -3 skipped) - a farm grows and is harvested only"
                 " on its writer's game; the other game holds its copy and sends its workers' work (amount x g_dt, worker uid + skill) to"
                 " the writer. plantInstance 0xDF320 %s, updateMaterial 0xE4500 %s, getStat 0x883BF0 %s, g_dt 0x2132734 %s",
                 g_hkUpdate, g_hkOperate, g_hkTimeSkip, g_hkYield, kFarmPlantRva != 0 ? "bound" : "MISSING", kFarmMaterialRva != 0 ? "bound" : "MISSING",
                 kGetStatRva != 0 ? "bound" : "MISSING", kGameDtRva != 0 ? "bound" : "MISSING");
    if (g_hkUpdate == 1 && g_hkOperate == 1) DebugLog(std::string(h));
    else ErrorLog(std::string(h) + " - FARMS UNGATED: both games grow and harvest their own copy (P16 duplication)");
    if (g_hkUpdate == 1 && g_hkOperate == 1 && (g_hkTimeSkip != 1 || kGameDtRva == 0))
        ErrorLog("[FARM] par16 fold: timeSkip hook " + std::string(g_hkTimeSkip == 1 ? "ok" : "NOT installed (a held copy's catch-up growth runs and uses its water items)")
                 + ", g_dt " + std::string(kGameDtRva != 0 ? "bound" : "MISSING (forwarded work uses a nominal 1/60 s frame)"));
    if (g_hkOperate == 1 && g_hkYield != 1)
        DebugLog("[FARM] par16 fold: getYieldChancePerCrop hook not installed - a forwarded worker the writer has no copy of harvests at the engine's no-worker yield (skill 1)");
    /* T-674: the erase first - a take gate whose releases could not be seen would hold the runner's spots until the copies go */
    g_hkErase = (kSetEraseRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kSetEraseRva), (void*)&detour_erase, (void**)&orig_erase) == coop::SUCCESS) ? 1 : -1);
    g_hkTake = (g_hkErase != 1) ? -3 : (kTakeSpotRva == 0) ? -2
        : ((coop::AddHook((void*)(base + (uintptr_t)kTakeSpotRva), (void*)&detour_take, (void**)&orig_take) == coop::SUCCESS) ? 1 : -1);
    char sh[300];
    std::sprintf(sh, "[SPOT] T-674 hooks: worker-set erase %d, take a work spot %d (1 installed; -1 failed; -2 no address; -3 skipped), hand::getCharacter %s",
                 g_hkErase, g_hkTake, kSpHandCharRva != 0 ? "bound" : "MISSING");
    if (g_hkTake == 1 && kSpHandCharRva != 0) DebugLog(std::string(sh));
    else ErrorLog(std::string(sh) + " - a machine's worker spots are counted per game (limit x games workers)");
}

/* ANY THREAD, crash-guarded: a production building's bar *(+0x448) and state +0x468 (as FmReadPod reads a farm's) */
int MineBarReadPod(void* b, float* progress, int* state)
{
    *progress = 0.0f; *state = -1;
    if (b == 0) return 0;
    __try
    {
        const char* c = (const char*)b;
        *state = *(const int*)(c + kFmProdState);
        const float* bar = *(float* const*)(c + kFmBar);
        if (bar == 0) return 2;
        *progress = *bar;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *progress = 0.0f; *state = -1; return 0; }
}
/* K2 safe point, crash-guarded: the writer's bar and state onto this game's copy (as FmWritePod writes a farm's). 1 both written,
   2 the state only (no output record here), 0 faulted. */
int MineBarWritePod(void* b, float progress, int state)
{
    __try
    {
        char* c = (char*)b;
        *(int*)(c + kFmProdState) = state;
        float* bar = *(float**)(c + kFmBar);
        if (bar == 0) return 2;
        *bar = progress;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* MAIN THREAD (FarmTick, every 500 ms): MINE_STATE for every node worked here - its bar and state to every game, as
   coopfarm::MineStatePublishDue says. Not sent (no road now): asked again next round. */
const DWORD kMsMissSayMs = 60000;   /* a node that does not resolve, or whose bar does not read, is counted once per key per this long */
/* MAIN THREAD: 1 when the miss `which` (0 the key resolves to nothing here, 1 the bar did not read) of node `c` is counted now -
   once per key per kMsMissSayMs, so a node that stays unresolved or unreadable is not counted every round */
int MsMissDue(const MsRow& c, int which, DWORD now)
{
    int due = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_msHigh; ++j)
        if (g_ms[j].used != 0 && g_ms[j].hash == c.hash && std::strcmp(g_ms[j].key, c.key) == 0)
        {
            int& said = (which == 0) ? g_ms[j].unresSaid : g_ms[j].readFailSaid;
            DWORD& at = (which == 0) ? g_ms[j].unresAt : g_ms[j].readFailAt;
            if (said == 0 || now - at >= kMsMissSayMs) { said = 1; at = now; due = 1; }
            break;
        }
    ::LeaveCriticalSection(&g_fmcs);
    return due;
}
void MsSendRound(DWORD now)
{
    static MsRow cand[kMsCap];
    int nCand = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_msHigh; ++j)
    {
        MsRow& r = g_ms[j];
        if (r.used == 0) continue;
        if (now - r.workedAt > kMsForgetMs) { r.used = 0; continue; }
        cand[nCand++] = r;
    }
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < nCand; ++k)   /* engine reads outside the lock (main thread) */
    {
        const MsRow& c = cand[k];
        void* b = FmResolve(std::string(c.key));
        if (b == 0) { if (MsMissDue(c, 0, now) != 0) ++g_msSendUnresolved; continue; }   /* gone or out of the loaded area: not sent */
        float pg = 0.0f;
        int st = -1;
        if (MineBarReadPod(b, &pg, &st) != 1) { if (MsMissDue(c, 1, now) != 0) ++g_msReadFail; continue; }   /* no output record, or a fault */
        const int due = coopfarm::MineStatePublishDue((unsigned int)(now - c.workedAt), c.ever, (unsigned int)(now - c.sentAt),
                                                      c.sentProgress, c.sentState, pg, st, c.finalSent);
        if (due == coopfarm::kMineStateNone) continue;
        coopfarm::FarmMsg m;
        m.kind = coopfarm::kMineState; m.key = c.key; m.prodState = st; m.progress = pg;
        if (!net::SendFarm(m)) { ++g_msSendFail; continue; }
        ++g_msSent;
        if (coopfarm::MineSayDue(g_msSent) != 0)
        {
            char h[300];
            std::sprintf(h, "[MINE] MINE_STATE sent key=%s progress=%.2f state=%d; stateSent=%lld", c.key, pg, st, g_msSent);
            DebugLog(std::string(h));
        }
        ::EnterCriticalSection(&g_fmcs);
        for (int j = 0; j < g_msHigh; ++j)
            if (g_ms[j].used != 0 && g_ms[j].hash == c.hash && std::strcmp(g_ms[j].key, c.key) == 0)
            {
                g_ms[j].ever = 1; g_ms[j].sentAt = now; g_ms[j].sentProgress = pg; g_ms[j].sentState = st;
                if (due == coopfarm::kMineStateFinal && g_ms[j].workedAt == c.workedAt) g_ms[j].finalSent = 1;   /* not when worked again meanwhile */
                break;
            }
        ::LeaveCriticalSection(&g_fmcs);
    }
}

/* MAIN THREAD (FarmTick, every 500 ms): every kept worker's work goes to the building's writer as MINE_OP, at most kFarmOpMax
   per message (the rest next round); a building this game writes NOW takes the work itself (run at K2); a building with no
   writer named keeps its work, dropped once it has waited coopfarm::kMinePendMs. */
void MnSendRound(DWORD now)
{
    static MnRow cand[kMnCap];
    static int candV[kMnCap];
    static MnRow out[kMnCap];
    static int outV[kMnCap];
    static MnRow dropped[kMnCap];
    static long long droppedN[kMnCap];
    int nCand = 0, nOut = 0, nDropped = 0;
    ::EnterCriticalSection(&g_fmcs);
    for (int j = 0; j < g_mnHigh; ++j)
    {
        MnRow& r = g_mn[j];
        if (r.used == 0) continue;
        if (r.work > 0.0f && r.amount > 0.0f) cand[nCand++] = r;
        else if (now - r.seenAt > kMnForgetMs) { r.used = 0; ++g_mnForgot; }
    }
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < nCand; ++k)   /* the writer, asked outside the lock (engine reads on the main thread) */
    {
        void* b = FmResolve(std::string(cand[k].key));
        candV[k] = (b != 0) ? ProdStepWriterHere(b) : coopfarm::kProdWriterNone;
    }
    ::EnterCriticalSection(&g_fmcs);
    for (int k = 0; k < nCand; ++k)
    {
        MnRow* r = 0;
        for (int j = 0; j < g_mnHigh; ++j)
            if (g_mn[j].used != 0 && g_mn[j].hash == cand[k].hash && g_mn[j].uid == cand[k].uid && std::strcmp(g_mn[j].key, cand[k].key) == 0)
            { r = &g_mn[j]; break; }
        if (r == 0) continue;
        if (coopfarm::MineOpAccept(candV[k]) == coopfarm::kMineAccWait)
        {
            if (r->undecidedAt == 0) { r->undecidedAt = (now != 0) ? now : 1; ++g_mnKeptUndecided; }
            else if (coopfarm::MineHoldExpired(now - r->undecidedAt) != 0)
            {
                ++g_mnDroppedUndecided;
                if (nDropped < kMnCap) { dropped[nDropped] = *r; droppedN[nDropped] = g_mnDroppedUndecided; ++nDropped; }   /* said below, outside the lock */
                r->work = 0.0f; r->undecidedAt = 0;
            }
            continue;
        }
        r->undecidedAt = 0;
        out[nOut] = *r;
        outV[nOut] = candV[k];
        out[nOut].work = coopfarm::FarmOpTake(&r->work);
        if (out[nOut].work > 0.0f) ++nOut;
    }
    ::LeaveCriticalSection(&g_fmcs);
    for (int k = 0; k < nDropped; ++k)
    {
        if (coopfarm::MineSayDue(droppedN[k]) == 0) continue;
        char h[400];
        std::sprintf(h, "[MINE] work dropped key=%s uid=%u work=%.3f: no game was named the writer of that node for %u s; droppedUndecided=%lld",
                     dropped[k].key, dropped[k].uid, dropped[k].work, coopfarm::kMinePendMs / 1000, droppedN[k]);
        DebugLog(std::string(h));
    }
    for (int k = 0; k < nOut; ++k)
    {
        const MnRow& o = out[k];
        if (coopfarm::MineOpAccept(outV[k]) == coopfarm::kMineAccRun)
        {   /* this game writes the building now: the work runs here, never sent to a game that would refuse it */
            MnIn& in = g_inMine[std::string(o.key)];
            MnMerge(&in, o.uid, o.amount, o.work);
            in.at = now; in.from = 0;
            ++g_mnToSelf;
            continue;
        }
        coopfarm::FarmMsg m;
        m.kind = coopfarm::kMineOp; m.key = o.key; m.uid = o.uid; m.amount = o.amount; m.work = o.work;
        if (net::SendFarm(m))
        {
            ++g_mnSent;
            char h[200];
            std::sprintf(h, " uid=%u amount=%.3f work=%.4f - this game does not write that building; the worker's step goes to its writer",
                         o.uid, o.amount, o.work);
            MnSay(std::string("[MINE] MINE_OP sent key=") + o.key + h);
        }
        else
        {   /* not sent: kept for the next round */
            ++g_mnSendFail;
            char h[400];
            if (MnAdd(o.key, o.uid, o.amount, o.work) > 0)
            {
                if (coopfarm::MineSayDue(g_mnSendFail) != 0)
                {
                    std::sprintf(h, "[MINE] MINE_OP not sent key=%s: no road to the other games now - the work is kept for the next round; sendFail=%lld", o.key, g_mnSendFail);
                    DebugLog(std::string(h));
                }
            }
            else
            {
                ++g_mnSendFailLost;
                if (coopfarm::MineSayDue(g_mnSendFailLost) != 0)
                {
                    std::sprintf(h, "[MINE] MINE_OP not sent key=%s work=%.3f: no road to the other games now and the relay table is full - the work is dropped; sendFailLost=%lld",
                                 o.key, o.work, g_mnSendFailLost);
                    DebugLog(std::string(h));
                }
            }
        }
    }
}

void FarmTick()
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return;
    /* g_fmLinked = THIS GAME IS IN A MULTIPLAYER WORLD (not a single-player game): every farm and machine takes the writer road -
       the writer verdict, a worker's step relayed to the writer (to this game itself when it writes), the writer's publish -
       whether this game is alone in the world or not. A single-player game runs every farm's own engine. */
    const LONG linked = RoleIsSingle() ? 0 : 1;
    const LONG was = ::InterlockedExchange(&g_fmLinked, linked);
    if (linked == 0)
    {
        if (was != 0)
        {   /* a single-player game now: every farm runs its own engine again, nothing is owed */
            ::EnterCriticalSection(&g_fmcs);
            for (int i = 0; i < g_fmHigh; ++i)
            {
                g_fm[i].verdict = 0;
                if (g_fm[i].nw > 0) { ++g_opDroppedLink; g_fm[i].nw = 0; }
                /* re-check #1: every farm runs its own engine again - a held catch-up is this game's own */
                if (g_fm[i].used != 0 && g_fm[i].tsPend == 1) { g_fm[i].tsPend = 2; ::InterlockedExchange(&g_tsDue, 1); }
            }
            for (int j = 0; j < g_mnHigh; ++j)   /* no writer to send a worker's step to - nothing is owed */
                if (g_mn[j].used != 0) { if (g_mn[j].work > 0.0f) ++g_mnDroppedLink; g_mn[j].used = 0; }
            g_mnHigh = 0;
            std::memset(g_ms, 0, sizeof(g_ms));   /* nothing to publish to */
            g_msHigh = 0;
            std::memset(g_sp, 0, sizeof(g_sp));   /* T-674: no runner to ask; the spots this game holds as runner are freed by FarmDrain's sweep */
            g_spHigh = 0;
            ::InterlockedExchange(&g_spGrantedN, 0);
            ::LeaveCriticalSection(&g_fmcs);
            g_last.clear(); g_hint.clear(); g_inMineState.clear();
        }
        return;
    }
    const DWORD now = ::GetTickCount();
    if (now - g_tickAt < 500)
    {   /* a new worker row: its runner read now rather than at the next round, so its steps are not refused until then */
        if (::InterlockedCompareExchange(&g_spNewDue, 0, 0) != 0 && !EngineWritesBlocked())
        {
            ::InterlockedExchange(&g_spNewDue, 0);
            SpSendRound(now);
        }
        return;
    }
    g_tickAt = now;
    if (EngineWritesBlocked()) { ++g_blocked; return; }
    if (::InterlockedExchange(&g_fmFullFlag, 0) != 0 && g_saidFull == 0)
    {
        g_saidFull = 1;
        char h[200];
        std::sprintf(h, "[FARM] the farm table is FULL (%d rows): a farm past it is HELD on this game (nobody grows it here) - fold #8", kFmCap);
        ErrorLog(std::string(h));
    }
    MnSendRound(now);
    MsSendRound(now);
    ::InterlockedExchange(&g_spNewDue, 0);
    SpSendRound(now);
    const int n = FmSnapshot(g_cp);
    for (int i = 0; i < n; ++i)
    {
        const FmCopy& c = g_cp[i];
        const bool stale = (now - c.seenAt) > kFmForgetMs;
        int nv = 0, fvByOwner = 0, fvAreaSlot = -1, fvAreaHere = 0;
        if (!stale)
        {
            void* b = FmResolve(c.key);
            if (b != 0)
            {   /* T-853 #6: the ladder's answer with the area holder it read (for the undecided fallback below) */
                FarmWriterView fv;
                const int w = FarmWriterDecide(b, &fv);
                nv = (w == 1) ? 1 : ((w == 2) ? 2 : 0);
                fvByOwner = fv.byOwner; fvAreaSlot = fv.areaSlot; fvAreaHere = fv.areaHere;
            }
        }
        else g_hint.erase(c.key);
        if (nv == 1) ++g_verdictMine; else if (nv == 2) ++g_verdictOther; else ++g_verdictNone;
        FmWork out[kFmWorkers];
        int nOut = 0, toSelf = 0, droppedStale = 0;
        ::EnterCriticalSection(&g_fmcs);
        FmEntry& e = g_fm[c.idx];
        if (e.used != 0 && e.hash == c.hash && std::strcmp(e.key, c.key) == 0)
        {
            if (stale && e.seenAt == c.seenAt)
            {
                droppedStale = (e.nw > 0) ? 1 : 0;
                if (e.tsPend != 0) ::InterlockedIncrement64(&g_tsDropped);   /* re-check #1: the farm is gone */
                e.used = 0; e.nw = 0; e.tsPend = 0;
            }
            else if (!stale)
            {
                /* T-853 #7: the ladder's undecided spell is timed; past coopfarm::kFarmUndecidedMs the game that holds the farm's area
                   grows it and the others send it their work and take its FARM - before, a farm the ladder never decided was held on
                   every game for as long as that lasted */
                if (nv != 0) e.undecAt = 0;
                else
                {
                    if (e.undecAt == 0) e.undecAt = (now != 0) ? now : 1;
                    const int fb = coopfarm::FarmFallbackWriter((unsigned int)(now - e.undecAt), fvByOwner, fvAreaSlot, fvAreaHere);
                    if (fb == coopfarm::kFarmFallHere) { nv = 1; ++g_verdictFallback; }
                    else if (fb == coopfarm::kFarmFallOther) { nv = 2; ++g_verdictFallback; }
                }
                e.verdict = nv;
                /* re-check #1: a catch-up held while undecided - this game writes it: replayed at K2; another game: dropped;
                   still undecided after kFmPendMs: kept (H3: the farm's own update is held here too until a writer is named),
                   counted late once */
                if (e.tsPend == 1)
                {
                    if (nv == 1) { e.tsPend = 2; ::InterlockedExchange(&g_tsDue, 1); }
                    else if (nv == 2) { e.tsPend = 0; ::InterlockedIncrement64(&g_tsDropped); }
                    else if (now - e.tsAt > kFmPendMs && e.tsLate == 0) { e.tsLate = 1; ::InterlockedIncrement64(&g_tsLate); }
                }
                if (nv == 2 || nv == 1)
                {   /* 2: sent to the writer; 1 (fold #9): this game IS the writer now - the work runs here, never dropped */
                    int keep = 0;
                    for (int k = 0; k < e.nw; ++k)
                    {
                        FmWork x = e.w[k];
                        if (nv == 2) x.work = coopfarm::FarmOpTake(&e.w[k].work); else { e.w[k].work = 0.0f; toSelf = 1; }
                        if (x.work > 0.0f) out[nOut++] = x;
                        if (e.w[k].work > 0.0f) e.w[keep++] = e.w[k];
                    }
                    e.nw = keep;
                }
                /* 0: undecided - the work waits for a writer (fold #9) */
            }
        }
        ::LeaveCriticalSection(&g_fmcs);
        if (droppedStale != 0) ++g_opDroppedStale;
        if (!stale && nv != c.verdict)
            FmSay(std::string("[FARM] key=") + c.key + " writer " + FmVerdictWord(c.verdict) + " -> " + FmVerdictWord(nv)
                  + (nv == 2 ? " (growth held here; work goes to the writer)" : (nv == 1 ? " (this game grows it and publishes)" : "")));
        for (int k = 0; k < nOut; ++k)
        {
            if (nv == 1)
            {
                FmOpIn& in = g_inOp[c.key];
                FmOpMerge(&in, out[k].uid, out[k].skill, out[k].work);
                in.at = now; in.from = 0;
                continue;
            }
            coopfarm::FarmMsg m;
            m.kind = coopfarm::kFarmOp; m.key = c.key; m.uid = out[k].uid; m.skill = out[k].skill; m.work = out[k].work;
            if (net::SendFarm(m)) ++g_opSent;
            else
            {   /* not sent: kept for the next round */
                ++g_opSendFail;
                FmAddOp(c.key, out[k].uid, out[k].skill, out[k].work);
            }
        }
        if (toSelf != 0) ++g_opHandedToSelf;
    }
}

void FarmDrain()
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return;
    if (g_ftKind == 0 && g_inFarm.empty() && g_inOp.empty() && g_inMine.empty() && g_inMineState.empty() && ::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0
        && ::InterlockedCompareExchange(&g_tsDue, 0, 0) == 0 && g_inSpot.empty() && g_spRun.empty()
        && ::InterlockedCompareExchange(&g_spGrantedN, 0, 0) == 0) return;
    if (EngineWritesBlocked()) { ++g_blocked; return; }
    FmRunLever();
    const DWORD now = ::GetTickCount();
    /* 0. (par16 re-check #1) the catch-up growth held at load while the writer was undecided, now due on this game: the
       engine's own timeSkip with the kept arguments on the farm the key resolves to - dropped when it no longer resolves or
       another game has become its writer since FarmTick decided. */
    if (::InterlockedExchange(&g_tsDue, 0) != 0)
    {
        std::vector<FmTsDue> due;
        ::EnterCriticalSection(&g_fmcs);
        for (int i = 0; i < g_fmHigh; ++i)
            if (g_fm[i].used != 0 && g_fm[i].tsPend == 2)
            {
                FmTsDue d;
                std::memcpy(d.key, g_fm[i].key, sizeof(d.key)); d.t = g_fm[i].tsT; d.flag = g_fm[i].tsFlag;
                due.push_back(d);
                g_fm[i].tsPend = 0;
            }
        ::LeaveCriticalSection(&g_fmcs);
        const bool linkedNow = ::InterlockedCompareExchange(&g_fmLinked, 0, 0) != 0;
        for (size_t i = 0; i < due.size(); ++i)
        {
            void* b = FmResolve(due[i].key);
            if (b == 0 || orig_timeskip == 0 || (linkedNow && FmWriterNow(due[i].key, b, 0) == 2)) { ::InterlockedIncrement64(&g_tsDropped); continue; }
            if (FmReplayTs(b, due[i].t, due[i].flag) == 0)
            {
                ::InterlockedIncrement64(&g_tsDropped);
                FmSay(std::string("[FARM] held catch-up growth FAULTED on replay key=") + due[i].key);
                continue;
            }
            if (::InterlockedIncrement64(&g_tsReplayed) == 1)
            {
                char h[120];
                std::sprintf(h, " elapsed=%.1f flag=%d", due[i].t, (int)due[i].flag);
                FmSay(std::string("[FARM] first held catch-up growth replayed key=") + due[i].key + h);
            }
        }
    }
    /* 1. the writer's state onto this game's copy - accepted only while THIS game's ladder names ANOTHER game the writer
       (fold #8) AND (M3) that game is the sender: this game writing it refuses it, an undecided farm waits, a FARM from any
       other game is refused and counted (coopfarm::FarmFromWriter); a sender or writer slot not known now waits. */
    for (std::map<std::string, FmIn>::iterator it = g_inFarm.begin(); it != g_inFarm.end(); )
    {
        void* b = FmResolve(it->first);
        if (b == 0)
        {
            if (now - it->second.at > kFmPendMs) { ++g_applyDropped; g_inFarm.erase(it++); }
            else ++it;
            continue;
        }
        int ws = -1;
        const int w = FmWriterNow(it->first, b, &ws);   /* T-853 #6: the verdict and the writer's slot from one ask */
        if (w == 1) { ++g_applyRefusedWriter; FmSay("[FARM] FARM refused key=" + it->first + ": this game writes that farm"); g_inFarm.erase(it++); continue; }
        if (w != 2)
        {
            ++g_applyWaitWriter;
            if (now - it->second.at > kFmPendMs) { ++g_applyDropped; g_inFarm.erase(it++); }
            else ++it;
            continue;
        }
        const int fw = coopfarm::FarmFromWriter(ws, net::PlayerSlotOfKey(it->second.from));
        if (fw == coopfarm::kFarmFromRefuse) { ++g_applyRefusedSender; FmSenderSay(it->first, it->second.from); g_inFarm.erase(it++); continue; }
        if (fw == coopfarm::kFarmFromWait)
        {
            ++g_applyWaitSender;
            if (now - it->second.at > kFmPendMs) { ++g_applyDropped; g_inFarm.erase(it++); }
            else ++it;
            continue;
        }
        if (FmReadPod(b, &g_pod) == 0) { ++g_applyFault; g_inFarm.erase(it++); continue; }
        const int cnt = coopfarm::FarmApplyCount(g_pod.n, (int)it->second.m.ages.size());
        if (cnt < 0)
        {   /* the copy's plants are not made yet */
            ++g_applyWait;
            if (now - it->second.at > kFmPendMs) { ++g_applyDropped; g_inFarm.erase(it++); }
            else ++it;
            continue;
        }
        if (cnt != (int)it->second.m.ages.size() || cnt != g_pod.n) ++g_applyMismatch;
        FmMsgToPod(it->second.m, &g_pod);
        int hidden = 0, water = 0;
        const int wr = FmWritePod(b, &g_pod, cnt, &hidden, &water);
        if (wr != 0)
        {
            if (g_applied == 0)
            {
                char h[160];
                std::sprintf(h, " (from peer %u, hidden plants %d, water records %d, refreshed %d)", it->second.from, hidden, water, wr == 1 ? 1 : 0);
                FmSay("[FARM] first FARM applied key=" + it->first + h);
            }
            ++g_applied;
            g_plantsHidden += hidden;
            if (water > 0) ++g_waterApplied;
            if (wr == 2) ++g_refreshSkipped;
        }
        else ++g_applyFault;
        g_inFarm.erase(it++);
    }
    /* 2. another game's work, run by the writer through the engine's own operate: work / THIS game's g_dt per call (fold #2),
       the worker's copy here as `who` when it is loaded, else its carried skill through the yield override (fold #1). */
    const float dt = FmDt();
    for (std::map<std::string, FmOpIn>::iterator it = g_inOp.begin(); it != g_inOp.end(); )
    {
        void* b = FmResolve(it->first);
        if (b == 0)
        {
            if (now - it->second.at > kFmPendMs) { ++g_opDropped; g_inOp.erase(it++); }
            else ++it;
            continue;
        }
        const int wv = FmWriterNow(it->first, b, 0);
        if (wv == 2 || orig_operate == 0) { ++g_opRefused; FmSay("[FARM] FARM_OP refused key=" + it->first + ": this game does not write that farm"); g_inOp.erase(it++); continue; }
        if (wv != 1)
        {
            ++g_opWaitWriter;
            if (now - it->second.at > kFmPendMs) { ++g_opDropped; g_inOp.erase(it++); }
            else ++it;
            continue;
        }
        if (!(dt > 0.0f)) { ++g_opPaused; it->second.at = now; ++it; continue; }   /* paused: the work is held, not converted by a zero */
        int calls = 0;
        std::vector<FmWork>& ws = it->second.w;
        for (size_t k = 0; k < ws.size() && calls < kFmCallsPerDrain; ++k)
        {
            void* who = 0;
            if (ws[k].uid != 0) who = (void*)FindSpawned(ws[k].uid);
            const int over = (who == 0 && ws[k].skill >= 0.0f && g_hkYield == 1) ? 1 : 0;
            if (who != 0) ++g_opWithCopy;
            else if (over != 0) ++g_opWithSkill;
            else
            {
                ++g_opNoWorker;
                if (g_saidNoWorker == 0)
                {
                    g_saidNoWorker = 1;
                    char h[200];
                    std::sprintf(h, " worker uid=%u skill=%.1f: no copy here and no skill override - harvested at the engine's no-worker yield (skill 1)",
                                 ws[k].uid, ws[k].skill);
                    DebugLog("[FARM] FARM_OP key=" + it->first + h);
                }
            }
            if (over != 0) { g_yOverVal = ws[k].skill * 0.01f; g_yOverThread = ::GetCurrentThreadId(); g_yOverFarm = b; }
            int fault = 0;
            calls += FmRunWork(b, who, &ws[k].work, dt, kFmCallsPerDrain - calls, &fault);
            g_yOverFarm = 0; g_yOverThread = 0;   /* re-check #3: reached on every path (FmRunWork catches an engine fault) */
            if (fault != 0)
            {
                ++g_opFault; ws[k].work = 0.0f;
                FmSay("[FARM] FARM_OP key=" + it->first + ": the engine's operate FAULTED - that worker's queued work dropped");
            }
        }
        if (calls > 0)
        {
            if (g_opApplied == 0) FmSay("[FARM] first FARM_OP run key=" + it->first);
            ++g_opApplied; g_opAppliedCalls += calls;
        }
        size_t keep = 0;
        for (size_t k = 0; k < ws.size(); ++k) if (ws[k].work > 0.0f) ws[keep++] = ws[k];
        ws.resize(keep);
        if (ws.empty()) g_inOp.erase(it++);
        else { it->second.at = now; ++it; }   /* past the per-drain call cap: the rest runs next drain */
    }
    /* MINE_OP: the writer runs a worker's step it was sent through the engine's own ProductionBuilding::operate, at most one
       sender's call amount per call at this game's frame (updateOutput makes at most one item a call), at most kFmCallsPerDrain
       calls per building per drain; a game another game's writer holds refuses it; with no writer named it waits kFmPendMs
       (as FARM_OP); a key that resolves to something that is not a production building is refused. */
    const float mdt = FmDt();
    for (std::map<std::string, MnIn>::iterator it = g_inMine.begin(); it != g_inMine.end(); )
    {
        void* b = FmResolve(it->first);
        if (b == 0)
        {
            if (now - it->second.at > kFmPendMs) { ++g_mnDropped; MnSayDropped(it->first, "that key names no loaded building here", now - it->second.at); g_inMine.erase(it++); }
            else ++it;
            continue;
        }
        if (ProductionBuildingIs(b) == 0)
        {
            ++g_mnNotProduction;
            MnSay("[MINE] MINE_OP refused key=" + it->first + ": that key names no production building here");
            g_inMine.erase(it++);
            continue;
        }
        const int acc = coopfarm::MineOpAccept(ProdStepWriterHere(b));
        if (acc == coopfarm::kMineAccRefuse)
        {
            ++g_mnRefused;
            MnSay("[MINE] MINE_OP refused key=" + it->first + ": another game writes that building");
            g_inMine.erase(it++);
            continue;
        }
        if (acc == coopfarm::kMineAccWait)
        {
            ++g_mnWaitWriter;
            if (now - it->second.at > kFmPendMs) { ++g_mnDropped; MnSayDropped(it->first, "no game was named the writer of that node", now - it->second.at); g_inMine.erase(it++); }
            else ++it;
            continue;
        }
        if (!(mdt > 0.0f)) { ++g_mnPaused; it->second.at = now; ++it; continue; }   /* paused: the work is held */
        int calls = 0;
        std::vector<MnWork>& ws = it->second.w;
        for (size_t k = 0; k < ws.size() && calls < kFmCallsPerDrain; ++k)
        {
            void* who = (ws[k].uid != 0) ? (void*)FindSpawned(ws[k].uid) : 0;
            if (who != 0) ++g_mnWithCopy; else ++g_mnNoWorker;
            while (calls < kFmCallsPerDrain)
            {
                float amt = 0.0f;
                const float w = coopfarm::FarmWorkSlice(ws[k].work, mdt, ws[k].amount, &amt);
                if (!(w > 0.0f)) break;
                if (ProdOperateRun(b, who, amt) == 0)
                {
                    ++g_mnFault; ws[k].work = 0.0f;
                    MnSay("[MINE] MINE_OP key=" + it->first + ": the engine's operate did not run (faulted or no hook) - that worker's work dropped");
                    break;
                }
                ws[k].work -= w;
                if (ws[k].work < 1.0e-7f) ws[k].work = 0.0f;
                ++calls;
            }
        }
        if (calls > 0)
        {
            if (g_mnRan == 0) MnSay("[MINE] first MINE_OP run key=" + it->first + " - the worker's step ran here, on the building's writer");
            ++g_mnRan; g_mnCalls += calls;
            MsMark(it->first.c_str());   /* the step ran here: its bar goes to the other games (MINE_STATE) */
        }
        size_t keep = 0;
        for (size_t k = 0; k < ws.size(); ++k) if (ws[k].work > 0.0f) ws[keep++] = ws[k];
        ws.resize(keep);
        if (ws.empty()) g_inMine.erase(it++);
        else { it->second.at = now; ++it; }   /* past the per-drain call cap: the rest runs next drain */
    }
    /* MINE_STATE: another game's bar and state for a node, written onto this game's copy only while this game's own ladder names
       another game its writer (coopfarm::MineStateAccept) AND (T-853 #11) the sender is that game (coopfarm::FarmFromWriter on the
       runner's slot from the same ask); a key that resolves to nothing here, or whose slots are not known now, waits kFmPendMs. */
    for (std::map<std::string, MsIn>::iterator it = g_inMineState.begin(); it != g_inMineState.end(); )
    {
        void* b = FmResolve(it->first);
        if (b == 0)
        {
            if (now - it->second.at > kFmPendMs) { ++g_msUnresolved; g_inMineState.erase(it++); }
            else ++it;
            continue;
        }
        if (ProductionBuildingIs(b) == 0) { ++g_msNotProduction; g_inMineState.erase(it++); continue; }
        const MsIn in = it->second;
        char h[400];
        int runSlot = -1;
        const int pw = ProdWriterDecide(b, &runSlot);
        const int fromRunner = (coopfarm::MineStateAccept(pw) != 0) ? coopfarm::FarmFromWriter(runSlot, net::PlayerSlotOfKey(in.from))
                                                                     : coopfarm::kFarmFromRefuse;
        if (fromRunner == coopfarm::kFarmFromWait)
        {   /* another game runs it but its slot or the sender's is not known now: kept (the latest per key) up to kFmPendMs */
            ++g_msWaitSender;
            if (now - in.at > kFmPendMs) { ++g_msUnresolved; g_inMineState.erase(it++); }
            else ++it;
            continue;
        }
        if (fromRunner == coopfarm::kFarmFromWriter)
        {
            if (MineBarWritePod(b, in.progress, in.state) == 0) ++g_msApplyFault;
            else
            {
                ++g_msApplied;
                if (coopfarm::MineSayDue(g_msApplied) != 0)
                {
                    std::sprintf(h, "[MINE] MINE_STATE applied key=%s progress=%.2f state=%d - another game writes that node; stateApplied=%lld",
                                 it->first.c_str(), in.progress, in.state, g_msApplied);
                    DebugLog(std::string(h));
                }
            }
        }
        else
        {
            ++g_msRefused;
            if (coopfarm::MineStateAccept(pw) != 0) ++g_msRefusedSender;
            if (coopfarm::MineSayDue(g_msRefused) != 0)
            {
                std::sprintf(h, "[MINE] MINE_STATE refused key=%s: this game writes that node, names no writer, or the sender is not the game that runs it; stateRefused=%lld refusedSender=%lld",
                             it->first.c_str(), g_msRefused, g_msRefusedSender);
                DebugLog(std::string(h));
            }
        }
        g_inMineState.erase(it++);
    }
    /* T-674: the runner's asks, releases and sweep (before the unlinked return: a spot held for a game that left is freed) */
    SpDrain(now);
    /* 3. the writer publishes to every linked game: every 500 ms, at most kFmPubPerDrain farms, each at most every 2 s
       (coopfarm::FarmPublishDue) */
    if (::InterlockedCompareExchange(&g_fmLinked, 0, 0) == 0 || now - g_pubAt < 500) return;
    g_pubAt = now;
    const int n = FmSnapshot(g_cp);
    int sent = 0;
    for (int r = 0; r < n && sent < kFmPubPerDrain; ++r)
    {
        const int i = (g_pubRot + r) % n;
        if (g_cp[i].verdict != 1 || now - g_cp[i].seenAt > 10000) continue;
        const std::string key(g_cp[i].key);
        void* b = FmResolve(key);
        if (b == 0) continue;
        if (FmReadPod(b, &g_pod) == 0) { ++g_readFail; continue; }
        coopfarm::FarmMsg cur;
        FmPodToMsg(g_pod, key, &cur);
        FmLast& last = g_last[key];
        const int due = coopfarm::FarmPublishDue(last.m, cur, last.ever, now - last.at);
        if (due == 0) continue;
        ++sent;
        if (net::SendFarm(cur))
        {
            if (due == 2) ++g_pubKeep; else ++g_pubSent;
            last.m = cur; last.at = now; last.ever = 1;
        }
        else ++g_pubFail;
    }
    if (n > 0) g_pubRot = (g_pubRot + 1) % n;
}

void FarmForgetWorld()
{
    g_inFarm.clear(); g_inOp.clear(); g_last.clear(); g_hint.clear(); g_ftKind = 0; g_said = 0; g_saidFull = 0; g_saidNoWorker = 0;
    g_inMine.clear(); g_mnSaid = 0; g_inMineState.clear();
    g_inSpot.clear(); g_spRun.clear(); g_spSaid = 0;   /* T-674: the world's buildings and sets are gone with it */
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return;
    ::EnterCriticalSection(&g_fmcs);
    for (int i = 0; i < g_fmHigh; ++i) if (g_fm[i].used != 0 && g_fm[i].tsPend != 0) ::InterlockedIncrement64(&g_tsDropped);
    std::memset(g_fm, 0, sizeof(g_fm));
    g_fmHigh = 0;
    std::memset(g_mn, 0, sizeof(g_mn));
    g_mnHigh = 0;
    std::memset(g_ms, 0, sizeof(g_ms));
    g_msHigh = 0;
    std::memset(g_sp, 0, sizeof(g_sp));
    g_spHigh = 0;
    ::InterlockedExchange(&g_spGrantedN, 0);
    std::memset(g_spCt, 0, sizeof(g_spCt));   /* T-853 #4: the counted spots went with the world's buildings */
    g_spCtHigh = 0;
    ::InterlockedExchange(&g_spCtN, 0);
    ::LeaveCriticalSection(&g_fmcs);
    ::InterlockedExchange(&g_fmFullFlag, 0);
    ::InterlockedExchange(&g_tsDue, 0);
}

void FarmNoteRecv(const coopfarm::FarmMsg& m, unsigned int fromPeer)
{
    const DWORD now = ::GetTickCount();
    if (m.kind == coopfarm::kFarmState)
    {   /* M3: a FARM from a game this game does not name the writer is refused here; one not judged yet never replaces one
           that came from the proven writer */
        ++g_recvState;
        const int fw = FmSenderJudge(m.key, fromPeer);
        if (fw == coopfarm::kFarmFromRefuse) { ++g_applyRefusedSender; FmSenderSay(m.key, fromPeer); return; }
        std::map<std::string, FmIn>::iterator have = g_inFarm.find(m.key);
        if (fw == coopfarm::kFarmFromWait && have != g_inFarm.end() && have->second.proven != 0 && have->second.from != fromPeer) { ++g_applyKeptProven; return; }
        FmIn& in = g_inFarm[m.key];
        in.m = m; in.at = now; in.from = fromPeer; in.proven = (fw == coopfarm::kFarmFromWriter) ? 1 : 0;
        return;
    }
    if (m.kind == coopfarm::kSpotAnswer) { SpOnAnswer(m, now, fromPeer); return; }
    if (m.kind == coopfarm::kSpotAsk || m.kind == coopfarm::kSpotRelease)
    {   /* T-674: handled at K2 by the game that runs the building (SpDrain) */
        if (g_inSpot.size() >= kSpInCap) { ++g_spInDropped; return; }
        SpIn x;
        x.kind = m.kind; x.key = m.key; x.uid = m.uid;
        g_inSpot.push_back(x);
        return;
    }
    if (m.kind == coopfarm::kMineState)
    {   /* the writer's bar and state for a node, written at K2 when another game writes it here (the latest per key) */
        ++g_msRecv;
        MsIn& in = g_inMineState[m.key];
        in.progress = m.progress; in.state = m.prodState; in.at = now; in.from = fromPeer;
        return;
    }
    if (m.kind == coopfarm::kMineOp)
    {   /* a worker's step on a production building, run at K2 when this game writes it */
        ++g_mnRecv;
        MnIn& in = g_inMine[m.key];
        MnMerge(&in, m.uid, m.amount, m.work);
        in.at = now; in.from = fromPeer;
        return;
    }
    ++g_recvOp;
    FmOpIn& op = g_inOp[m.key];
    FmOpMerge(&op, m.uid, m.skill, m.work);
    op.at = now; op.from = fromPeer;
}

/* KEPT (build.cpp BdKeptExtras), K2 safe point: a kept farm laid onto this game's live farm - growth, death, start and plants harvested,
   and every plant's age built the way the farm's own update reads them back (keptwire::KeptFarmAges: the lowest age is the growth, the
   harvested plants hidden at -10), then FmWritePod's own refresh. Its water and bar are left as they are here (the kept input stocks
   are laid apart). The kept 'save time' is never used: it is game hours of the world before the reset. 1 laid, 0 not now (no plants
   yet, or the read or the write faulted); *plants = the live plant count. */
int FarmKeptLay(void* b, float growth, float death, float start, int harvested, int* plants)
{
    *plants = 0;
    if (b == 0 || FmReadPod(b, &g_pod) == 0 || g_pod.n <= 0) return 0;
    *plants = g_pod.n;
    keptwire::KeptFarmAges(growth, harvested, g_pod.n, g_pod.ages);
    g_pod.grown = growth; g_pod.died = death; g_pod.growStart = start;
    g_pod.harvested = harvested < 0 ? 0 : (harvested > g_pod.n ? g_pod.n : harvested);
    int hidden = 0, water = 0;
    return FmWritePod(b, &g_pod, g_pod.n, &hidden, &water) != 0 ? 1 : 0;
}
/* KEPT / TEST-ONLY bldstate, read-only: a farm's growth, death, start, plants harvested and plant count. 1 read, 0 not */
int FarmKeptRead(void* b, float* growth, float* death, float* start, int* harvested, int* plants)
{
    if (b == 0 || FmReadPod(b, &g_pod) == 0) return 0;
    *growth = g_pod.grown; *death = g_pod.died; *start = g_pod.growStart; *harvested = g_pod.harvested; *plants = g_pod.n;
    return 1;
}

std::string FarmTestArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string sub, a1, a2;
    is >> sub >> a1 >> a2;
    const char* usage = "error buildtest farm: usage buildtest farm list | grow <key-substring|min> <age> | harvest <key-substring|min> [crops 1-64, default 1]";
    if (sub == "list")
    {
        const int n = FmSnapshot(g_cp);
        std::string out = "ok farm list";
        char h[320];
        std::sprintf(h, " entries=%d", n);
        out += h;
        for (int i = 0; i < n; ++i)
        {
            void* b = FmResolve(g_cp[i].key);
            int ok = (b != 0) ? FmReadPod(b, &g_pod) : 0;
            int bs = -1, bq = -1;   /* lever2: the farm's own box, where operate puts its crops */
            if (b != 0) FarmBoxItemCount(b, &bs, &bq);
            std::sprintf(h, "[FARM] list key=%s writer=%s plants=%d harvested=%d grown=%.3f died=%.3f water=%.3f bar=%.3f boxStacks=%d boxQty=%d", g_cp[i].key,
                         FmVerdictWord(g_cp[i].verdict), ok ? g_pod.n : -1, ok ? g_pod.harvested : -1, ok ? g_pod.grown : -1.0f, ok ? g_pod.died : -1.0f,
                         (ok && g_pod.nIn > 0) ? g_pod.inStock[0] : -1.0f, (ok && g_pod.hasBar) ? g_pod.bar : -1.0f, bs, bq);
            DebugLog(std::string(h));
            out += " | "; out += (h + 7);
        }
        return out;
    }
    if (EngineWritesBlocked()) return "error buildtest farm: engine writes are blocked";
    if (a1.empty()) return usage;
    if (sub == "grow")
    {
        if (a2.empty()) return usage;
        const float v = (float)std::atof(a2.c_str());
        if (!(v >= 0.0f && v <= 3.0f)) return "error buildtest farm: age must be in [0, 3] (1 = ripe)";
        g_ftVal = v; g_ftSub = a1; g_ftKind = 1;
    }
    else if (sub == "harvest")
    {
        const int v = a2.empty() ? 1 : std::atoi(a2.c_str());   /* lever2: CROPS (plants harvested), not an operate amount */
        if (!(v >= 1 && v <= 64)) return "error buildtest farm: crops must be in [1, 64]";
        g_ftVal = v; g_ftSub = a1; g_ftKind = 2;
    }
    else return usage;
    DebugLog("[FARM] buildtest farm " + arg + " ARMED - run at the next safe point");
    return "ok buildtest farm armed";
}

void ReportFarm()
{
    int used = 0, high = 0;
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) != 0)
    {
        ::EnterCriticalSection(&g_fmcs);
        for (int i = 0; i < g_fmHigh; ++i) if (g_fm[i].used != 0) ++used;
        high = g_fmHigh;
        ::LeaveCriticalSection(&g_fmcs);
    }
    char b[2800];
    std::sprintf(b, "[FARM] REPORT farm[updSeen,updRan,updHeld,noKey,opHeld,opLocal,opNoEntry,full,tailFault]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " pub[sent,keep,fail,readFail]=%lld,%lld,%lld,%lld recv[state,op]=%lld,%lld"
                 " apply[applied,wait,refusedWriter,mismatch,dropped,fault]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " op[sent,sendFail,applied,calls,refused,dropped,droppedWriter]=%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " verdict[mine,other,none]=%lld,%lld,%lld test[runs,refused]=%lld,%lld blocked=%lld entries=%d hooks[update,operate]=%d,%d"
                 " fold[tsSeen,tsRan,tsHeld,tsHeldUndecided,tsReplayed,tsDropped,tsLate,applyWaitWriter,refreshSkipped,plantsHidden,waterApplied,inTooMany]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " foldOp[withCopy,withSkill,noWorker,yieldOverridden,skillRead,skillFault,merged,paused,waitWriter,handedToSelf,droppedStale,droppedLink,zeroWork,dtMissing,fault]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " foldHooks[timeSkip,yield]=%d,%d table[high,cap]=%d,%d opCopySkipped=%lld",
                 (long long)g_upSeen, (long long)g_upRan, (long long)g_upHeld, (long long)g_upNoKey, (long long)g_opHeld, (long long)g_opLocal,
                 (long long)g_opNoEntry, (long long)g_fmFull, (long long)g_tailFault,
                 g_pubSent, g_pubKeep, g_pubFail, g_readFail, g_recvState, g_recvOp,
                 g_applied, g_applyWait, g_applyRefusedWriter, g_applyMismatch, g_applyDropped, g_applyFault,
                 g_opSent, g_opSendFail, g_opApplied, g_opAppliedCalls, g_opRefused, g_opDropped, g_opDroppedStale + g_opDroppedLink,
                 g_verdictMine, g_verdictOther, g_verdictNone, g_testRuns, g_testRefused, g_blocked, used, g_hkUpdate, g_hkOperate,
                 (long long)g_tsSeen, (long long)g_tsRan, (long long)g_tsHeld, (long long)g_tsHeldUndecided, (long long)g_tsReplayed,
                 (long long)g_tsDropped, (long long)g_tsLate, g_applyWaitWriter, g_refreshSkipped,
                 g_plantsHidden, g_waterApplied, g_inTooMany,
                 g_opWithCopy, g_opWithSkill, g_opNoWorker, (long long)g_yieldOverridden, (long long)g_skillRead, (long long)g_skillFault,
                 (long long)g_opMerged, g_opPaused, g_opWaitWriter, g_opHandedToSelf, g_opDroppedStale, g_opDroppedLink, (long long)g_opZeroWork,
                 (long long)g_dtMissing, g_opFault, g_hkTimeSkip, g_hkYield, high, kFmCap, (long long)g_opCopySkipped);
    DebugLog(std::string(b));
    int mnUsed = 0;
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) != 0)
    {
        ::EnterCriticalSection(&g_fmcs);
        for (int i = 0; i < g_mnHigh; ++i) if (g_mn[i].used != 0) ++mnUsed;
        ::LeaveCriticalSection(&g_fmcs);
    }
    char mb[1500];
    std::sprintf(mb, "[MINE] REPORT relay[held,noKey,full,badAmount,sent,sendFail,toSelf,keptUndecided,droppedUndecided,droppedLink,forgot]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " run[recv,ran,calls,refused,notProduction,waitWriter,dropped,fault,paused,withCopy,noWorker]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " state[sent,sendFail,readFail,recv,applied,refused,applyFault,notProduction,unresolved,full,sendUnresolved]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " unencodable=%lld sendFailLost=%lld capLost=%lld pairs=%d",
                 (long long)g_mnHeld, (long long)g_mnNoKey, (long long)g_mnFull, (long long)g_mnBadAmount, g_mnSent, g_mnSendFail, g_mnToSelf,
                 g_mnKeptUndecided, g_mnDroppedUndecided, g_mnDroppedLink, g_mnForgot,
                 g_mnRecv, g_mnRan, g_mnCalls, g_mnRefused, g_mnNotProduction, g_mnWaitWriter, g_mnDropped, g_mnFault, g_mnPaused,
                 g_mnWithCopy, g_mnNoWorker,
                 g_msSent, g_msSendFail, g_msReadFail, g_msRecv, g_msApplied, g_msRefused, g_msApplyFault, g_msNotProduction, g_msUnresolved,
                 (long long)g_msFull, g_msSendUnresolved, net::FarmUnencodableCount(), g_mnSendFailLost, g_mnCapLost, mnUsed);
    DebugLog(std::string(mb));
    int spUsed = 0, spGranted = 0;
    long long giveUp = 0;
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) != 0)
    {
        ::EnterCriticalSection(&g_fmcs);
        for (int i = 0; i < g_spHigh; ++i) if (g_sp[i].used != 0) { ++spUsed; if (g_sp[i].state == coopfarm::kSpotGranted) ++spGranted; }
        giveUp = g_spGiveUp;
        ::LeaveCriticalSection(&g_fmcs);
    }
    char sb[1500];
    std::sprintf(sb, "[SPOT] REPORT take[seen,passListed,passed,refused,asked,noKey,noUid,full,handFault]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " ask[sent,fail,giveUp]=%lld,%lld,%lld answer[recv,yes,full,noCopy,stray]=%lld,%lld,%lld,%lld,%lld"
                 " left[noted,noRow]=%lld,%lld release[sent,fail]=%lld,%lld inDropped=%lld rows[used,granted]=%d,%d"
                 " runner[askRecv,notRunner,yes,full,noCopy,ansFail,relRecv,relNoRow,freedRel,freedGone,freedNotRunner,freedUnloaded,fault,capFull,held]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d hooks[erase,take]=%d,%d"
                 " farmSender[refused,wait,keptProven]=%lld,%lld,%lld copyRefused=%lld",
                 (long long)g_spSeen, (long long)g_spPassListed, (long long)g_spPassed, (long long)g_spRefused, (long long)g_spAsked,
                 (long long)g_spNoKey, (long long)g_spNoUid, (long long)g_spFull, (long long)g_spHandFault,
                 g_spAskSent, g_spAskFail, giveUp, g_spAnsRecv, g_spAnsYes, g_spAnsFull, g_spAnsNoCopy, g_spAnsStray,
                 (long long)g_spLeft, (long long)g_spLeftNoRow, g_spRelSent, g_spRelFail, g_spInDropped, spUsed, spGranted,
                 g_srAskRecv, g_srNotRunner, g_srYes, g_srFull, g_srNoCopy, g_srAnsFail, g_srRelRecv, g_srRelNoRow, g_srFreedRel, g_srFreedGone,
                 g_srFreedNotRunner, g_srFreedUnloaded, g_srFault, g_srCapFull, (int)g_spRun.size(), g_hkErase, g_hkTake,
                 g_applyRefusedSender, g_applyWaitSender, g_applyKeptProven, (long long)g_spCopyRefused);
    DebugLog(std::string(sb));
    std::sprintf(sb, "[SPOT] REPORT T-853 asker[revoked,keepYes,strayReleased,unusedReleased,regrant,keepAsk]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " runner[counted,countedFull,freedCounted,revokeSent,revokeFail,countedHeld,ownRefusedByCounted]=%lld,%lld,%lld,%lld,%lld,%ld,%lld"
                 " mineState[refusedSender,waitSender]=%lld,%lld farm[fallback]=%lld",
                 g_spAnsRevoked, g_spAnsKeep, g_spStrayRel, g_spUnusedRel, g_spRegrant, g_spKeepAsk,
                 g_srCounted, g_srCountedFull, g_srFreedCounted, g_srRevokeSent, g_srRevokeFail, (long)::InterlockedCompareExchange(&g_spCtN, 0, 0),
                 (long long)g_spCtRefused, g_msRefusedSender, g_msWaitSender, g_verdictFallback);
    DebugLog(std::string(sb));
}

int MineLinked() { return (::InterlockedCompareExchange(&g_fmLinked, 0, 0) != 0) ? 1 : 0; }

void MineWorkedNote(void* pb)
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0 || pb == 0) return;
    char key[64];
    if (ObjectPositionKey(pb, key, (int)sizeof(key), 0, 3, 0) == 0) return;
    MsMark(key);
}

int MineStepRelay(void* pb, void* who, float amount)
{
    if (::InterlockedCompareExchange(&g_fmInit, 0, 0) == 0) return 0;
    char key[64];
    if (pb == 0 || ObjectPositionKey(pb, key, (int)sizeof(key), 0, 3, 0) == 0) { ::InterlockedIncrement64(&g_mnNoKey); return 0; }
    const float work = amount * FmDt();   /* paused (g_dt 0): nothing is owed and nothing runs here */
    const int r = MnAdd(key, (who != 0) ? FindSpawnedUid(who) : 0u, amount, work);
    if (r == 0) { ::InterlockedIncrement64(&g_mnFull); return 0; }
    if (r < 0) { ::InterlockedIncrement64(&g_mnBadAmount); return 0; }
    ::InterlockedIncrement64(&g_mnHeld);
    return 1;
}

}   // namespace coop
