// camp.cpp - T-929: ONE LIST OF THE WORLD'S BANDIT CAMPS AND ANIMAL DENS (src/common/campwire.h, CAMP 93; the game's decisions in
// src/common/campgame.h). Owner decisions 1376 / 1377.
//
// THE ENGINE (build/decomp_8f6440.txt, decomp_8f5480.txt, decomp_57f880.txt, decomp_582970.txt). Every 2 s the threaded spawn
// check (ZoneManager::spawnChecksUpdateThreaded 0x8F8370, the engine's background worker) calls the camp pass 0x8F6440 with the
// region table. For each region with fewer camps (region+0x40) than its limit (region+0 int) times the world's camp scale (the float
// at 0x2132538), the pass asks 0x57F880(factory, region record (region+0x10), "nests", 13) for a camp kind, 0x8F5480(region, &spot)
// for a spot (it avoids the cell of this game's view point and spots near it, 0x8F4200), and when the spot is not (0,0,0) builds the
// camp with RootObjectFactory 0x582970(factory, kind, &spot, 1, faction, ...), adds it to the town list (TownList vt+0x10) and, when
// its cell is loaded, queues its furniture (the list at 0x2133678); then it returns - one camp per pass.
//
// THE RULE (campgame.h CampPassMode), one road whatever the player count:
//   DRAW   the engine's own kind and spot become this game's proposal: the spot picker hands the engine (0,0,0) so nothing is built,
//          later regions are skipped (the kind picker hands out none), and the proposal goes out on the main thread (single player:
//          answered here, accepted - there is no other game).
//   BUILD  an accepted camp is built by the next pass: the kind picker hands out the camp's kind in the camp's region only (none in any
//          other), the spot picker hands out the camp's spot, and the engine's own build, town-list entry and furniture step run. The
//          build's answer is the engine's own: what 0x582970 returned (the outermost call on the pass's thread). Only the pass's own
//          picker calls follow the rule (campgame.h CampKindAnswer): the build's own calls inside 0x582970 (the camp's material) are the
//          engine's.
//   SKIP   a proposal waits for its answer, or the world server's whole list is not here (or it has no list yet, or cannot read it).
//   SCAN   this game's own camps are read from the town list instead of running the pass (the engine's own camp list, TownList+0x88
//          count / +0x90 array - the list the engine's cell check 0x8F46E0 walks; each camp's region at Town+0x1A0, decomp_927ba0;
//          its position by vt+0x40): for the SEED (owner 1376 - the world has no list: this game's camps become it; a single-player
//          game's own list) or for the MATCH (owner 1376 - a camp not on the list is removed, a list camp this game lacks is built).
// THE VIEW RULE ON EVERY GAME (part 7, campgame.h CampNearView): a BUILD whose spot is within the engine's own distance of THIS game's
// view point withholds the spot and waits (CampTakeViewRetry) - every game applies the engine's rule to its own real view point.
// REMOVAL (part 3): the engine's own road for a camp (its cell check 0x8F5100: Nest vt+0x208, then TownDestroy 0x92A4F0, which takes the
// camp off its region's camp count and the town lists and frees nothing - decomp_92a4f0), on the main thread, only while no camp pass
// runs (g_cpPassMx, tried - never waited on), only for a camp still the one the scan read (CampRemoveStill), and only while its area is
// not loaded here (CampRemoveWhen - the engine removes a camp only as its area loads); a camp in a loaded area waits for the area to go.
// GONE (part 4, CampGoneSend): a TownDestroy called by the engine's cell check for a camp of the list.
// The pass's state lives in plain globals touched only by the pass's own thread while g_cpPassTid names it; everything the main
// thread and the pass share is under g_cpLock (the pass runs on the engine's worker - no main-thread-only container is read there).
// A CAMP'S FURNITURE (option A, campgame.h CampDebrisPoint): the engine lays a camp's furniture out again at every area load
// (CampDebrisPlace 0x937570) around a queued point and seeds its random stream from that point's whole x / z; a list camp's step is
// handed the list spot's x / z (the engine's height kept), so every game and every visit seeds the same stream from the same point.
// A GROUP'S HOME CAMP (part 6, campgame.h CampHomeWay): this game's camps by list key (g_cpTownOfKey - every camp the match named and
// every camp this game built); a group's CONTEXT carries its home camp's key and the receiver finds its own camp of that key.
#include "camp.h"
#include "addresses.h"
#include "store.h"
#include "config.h"      /* RoleIsSingle - ANY THREAD */
#include "coop_log.h"
#include "zones.h"       /* ZoneKeepSectorTri / SectorOf: is a camp's area loaded here (the pass's own test) */
#include "hooks.h"       /* coop::AddHook (own MinHook) */
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/RootObjectBase.h"   /* a camp's record (getRecord) */
#include <intrin.h>                /* _ReturnAddress: who called TownDestroy */
#include "../common/campwire.h"
#include "../common/campgame.h"
#include "../common/squadorigin.h"   /* kLoadAgeUnknown: the world has not run yet */
#include <Windows.h>
#include <stdint.h>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coop { void CampHomeBuilt(const std::string& campKey, void* town); std::string CtxCampHomeToken(); }   /* spawn.cpp (spawn.h): the groups owed their home camp */

namespace {

unsigned long long kCpPassRva = 0;  static coop::AddrReg kCpPassRva_reg("CampSpawnPass", &kCpPassRva);              /* Steam_1.0.65 0x8F6440 (1.0.68 0x8F7310): the camp pass (decomp_8f6440) */
unsigned long long kCpSpotRva = 0;  static coop::AddrReg kCpSpotRva_reg("CampSpotPick", &kCpSpotRva);                /* Steam_1.0.65 0x8F5480 (1.0.68 0x8F6350): float* (region, float* out) - the camp's spot */
unsigned long long kCpKindRva = 0;  static coop::AddrReg kCpKindRva_reg("GameDataRandomReference", &kCpKindRva);     /* Steam_1.0.65 0x57F880 (1.0.68 0x580310): GameData* (factory, GameData*, std::string* list, int type, int) - a random reference of that list */
unsigned long long kCpSpawnRva = 0; static coop::AddrReg kCpSpawnRva_reg("RootObjectFactory_spawn", &kCpSpawnRva);   /* Steam_1.0.65 0x582970 (1.0.68 0x583400): the build */
unsigned long long kCpScaleRva = 0; static coop::AddrReg kCpScaleRva_reg("CampLimitScale", &kCpScaleRva);            /* Steam_1.0.65 0x2132538 (var, 1.0.68 0x2133588): float - the world's camp scale (decomp_8f6440: count < limit * it) */

unsigned long long kCpTownListRva = 0; static coop::AddrReg kCpTownListRva_reg("TownList", &kCpTownListRva);                 /* Steam_1.0.65 0x21330A0 (var): a POINTER to the TownList */
unsigned long long kCpDestroyRva = 0;  static coop::AddrReg kCpDestroyRva_reg("TownDestroy", &kCpDestroyRva);              /* Steam_1.0.65 0x92A4F0 (1.0.68 0x92B3C0): TownList: destroy one town (decomp_92a4f0) */
unsigned long long kCpNestRetRva = 0;  static coop::AddrReg kCpNestRetRva_reg("CampNestCheckRet", &kCpNestRetRva);         /* Steam_1.0.65 0x8F51EB (1.0.68 0x8F60BB): the return into the engine's cell check 0x8F5100 from its TownDestroy */
unsigned long long kCpPiRva = 0;       static coop::AddrReg kCpPiRva_reg("PlayerInterfaceGlobal", &kCpPiRva);              /* Steam_1.0.65 0x2133630 (var): PlayerInterface* */
unsigned long long kCpViewRva = 0;     static coop::AddrReg kCpViewRva_reg("PlayerInterfaceViewPoint", &kCpViewRva);       /* Steam_1.0.65 0x7F1DC0: Vector3* (PlayerInterface*, Vector3* out) - the point the spot test 0x8F4200 measures from */
unsigned long long kCpDebrisRva = 0;   static coop::AddrReg kCpDebrisRva_reg("CampDebrisPlace", &kCpDebrisRva);            /* Steam_1.0.65 0x937570 (1.0.68 0x938440): (town, camp record, &point, hand) - the camp's furniture step (decomp_937570) */
unsigned long long kCpViewDistRva = 0; static coop::AddrReg kCpViewDistRva_reg("CampViewDistance", &kCpViewDistRva);       /* Steam_1.0.65 0x172F4F0 (ro, 1.0.68 0x1730490): float 1382.4 - the spot test's distance (its local static 0x2134218 is a copy) */

typedef void (*CpPassFn)(void* grid);
typedef void (*CpDestroyFn)(void* list, void* town);
typedef float* (*CpPosFn)(void* self, float* out);
typedef void (*CpNestOffFn)(void* self);
typedef float* (*CpViewFn)(void* pi, float* out);
CpDestroyFn orig_cpDestroy = 0;
typedef void* (*CpKindFn)(void* factory, void* gd, void* listName, unsigned long long type, unsigned long long p5);
typedef float* (*CpSpotFn)(void* region, float* out, void* kind);
typedef void* (*CpSpawnFn)(void* self, void* gd, void* pos, unsigned long long p4, void* p5, void* p6, void* p7, void* p8, void* p9,
                           unsigned long long p10, unsigned long long p11, unsigned long long p12);
CpPassFn orig_cpPass = 0;
CpKindFn orig_cpKind = 0;
CpSpotFn orig_cpSpot = 0;
CpSpawnFn orig_cpSpawn = 0;
typedef void (*CpDebrisFn)(void* town, void* rec, float* pt, unsigned long long hand);
CpDebrisFn orig_cpDebris = 0;
volatile LONG g_cpDebrisOffMain = 0;       /* furniture steps on another thread (the engine's point kept) */
volatile LONG g_cpReady = 0;   /* all four hooks are in: the rule is in force (else the engine's pass runs as itself) */

const size_t kCpRegion = 0x10;    /* region -> its world record (GameData*): the "nests" list's owner (decomp_8f6440 :84; 05-findings F-key: AreaBiomeGroup+0x10) */
const size_t kCpGdType = 0x50;    /* GameData -> its type (13 = a town record, decomp_8f6440 :89) */
const size_t kCpGdSid = 0x58;     /* GameData -> stringID (std::string, VS2010 layout) */
const int kCpTownType = 13;

struct CpBuild { campl::Camp rec; void* kindGd; CpBuild() : kindGd(0) {} };

/* ---- shared by the main thread and the pass: under g_cpLock ---- */
CRITICAL_SECTION g_cpLock;
volatile LONG g_cpLockReady = 0;
std::deque<CpBuild> g_cpBuildQ;            /* accepted camps waiting for a pass to build them */
int g_cpWaiting = 0;                       /* this game's proposal waits for its answer */
std::string g_cpWaitKey;                   /* ... its key */
int g_cpWaitSent = 0; long g_cpWaitGen = 0;   /* ... it went out on this link generation (0 sent = not yet) */
int g_cpHaveDrawn = 0; campl::Camp g_cpDrawn;   /* the pass drew this camp; the main thread sends it */
std::set<std::string> g_cpRegionFull;      /* regions the world server answered "full" for since their last removal */
int g_cpListComplete = 0; unsigned int g_cpListState = campl::kListNone;   /* the whole list arrived on this link, and its state */
struct CpDone { campl::Camp rec; int outcome; void* town; int posOk; float x, y, z; };   /* town / position: what the engine built (BUILT only) */
std::vector<CpDone> g_cpDoneQ;             /* forced builds' endings, for the main thread */
long long g_cpPassesSkipped = 0, g_cpPassesAlone = 0, g_cpPassesDraw = 0, g_cpPassesBuild = 0, g_cpStaleDraws = 0;
long g_cpLoadGen = 0;                      /* world loads begun (CampWorldLoadBegan): a pass that began before one keeps nothing it drew */
std::deque<CpBuild> g_cpViewWait;          /* part 7: accepted camps whose spot was near this game's view point - tried again (CampTakeViewRetry) */
int g_cpLastViewRetry = 0;                 /* the last pass retried one of them */
int g_cpScanDue = campg::kScanNone; long g_cpScanGen = 0;   /* parts 2 / 3: this game's camps are to be read (why, and the link the reason came on) */
/* one camp read by a SCAN (the town, its kind's record, its kind / region sids, the region's limit, its position) */
struct CpLive { void* town; void* kindGd; std::string kind, region; int limit; float x, y, z; CpLive() : town(0), kindGd(0), limit(0), x(0), y(0), z(0) {} };
std::vector<CpLive> g_cpScanOut; int g_cpScanReady = 0, g_cpScanOutWhy = 0; long g_cpScanOutGen = 0, g_cpScanOutLoad = 0, g_cpScanOutWorld = 0; float g_cpScanScale = 0.0f;
long long g_cpScanWaits = 0, g_cpScanOver = 0, g_cpScanNoRegion = 0, g_cpScanNotTown = 0, g_cpScanNotRunning = 0;
struct CpGone { std::string kind; float x, z; };
std::vector<CpGone> g_cpGoneQ;             /* part 4: camps the engine's cell check removed (TownDestroy's own thread), for the main thread */
const size_t kCpGoneCap = 256;
long long g_cpGoneOver = 0;
std::vector<void*> g_cpDestroyedQ;         /* part 6: every town TownDestroy took (any caller, any thread) - the main thread drops it from the key map */
int g_cpDestroyedOver = 0;                 /* ... more than kCpDestroyedCap before a drain: the whole key map is dropped (the next match / build names them again) */
const size_t kCpDestroyedCap = 256;
CRITICAL_SECTION g_cpPassMx;              /* held through each hooked camp pass; the main thread's removals TRY it (never wait) */

/* ---- the pass's own (its thread only, while g_cpPassTid names it) ---- */
volatile LONG g_cpPassTid = 0;
int g_cpMode = campg::kPassSkip;
long g_cpPassLoadGen = 0;
CpBuild g_cpForced;
int g_cpKindGiven = 0, g_cpSpotGiven = 0, g_cpSpawnSeen = 0, g_cpSpawned = 0, g_cpSpawnDepth = 0, g_cpNearView = 0;
void* g_cpSpawnTown = 0; int g_cpSpawnPosOk = 0; float g_cpSpawnPos[3] = { 0, 0, 0 };   /* the forced build's camp and its position after the build */
long long g_cpViewUnread = 0;
std::set<std::string> g_cpFullSnap;        /* g_cpRegionFull as the pass began */
void* g_cpDrawKind = 0; std::string g_cpDrawRegion; int g_cpDrawDone = 0; campl::Camp g_cpDrawRec;
long long g_cpDrawUnreadable = 0;

/* ---- main thread only ---- */
std::map<std::string, campl::Camp> g_cpList;   /* the world server's list as this game holds it */
int g_cpInList = 0; unsigned int g_cpBeginCount = 0;
std::set<std::string> g_cpBuiltHere;       /* keys this game built or queued this world load */
long g_cpLinkGenSeen = -1;
long long g_cpProposed = 0, g_cpAnsweredHere = 0, g_cpAccepted = 0, g_cpOthers = 0, g_cpKnown = 0, g_cpRefused = 0, g_cpRefusedStale = 0, g_cpWaitLost = 0;
long long g_cpBuilt = 0, g_cpNotBuilt = 0, g_cpNoKind = 0, g_cpMalformed = 0, g_cpLists = 0, g_cpListsIncomplete = 0, g_cpRemoved = 0, g_cpNoLinkDrop = 0;
int g_cpLoadScanned = 0;                   /* single player: this world load's own camps were read into g_cpList */
long g_cpSeededGen = -1;                   /* the link generation this game sent its seed on (-1 = none this world load) */
struct CpRemove { void* town; void* kindGd; std::string kind; float x, z; int held; };   /* held: its area was loaded here when last tried */
std::vector<CpRemove> g_cpRemoveQ;         /* part 3: this game's camps the list does not hold, removed while no pass runs and their area is not loaded */
std::vector<std::string> g_cpMatchBuilds;  /* part 3: list camps this game lacks, queued once the removals are done */
void* volatile g_cpOwnTown = 0;            /* the camp this game is removing itself (no GONE for it) */
long long g_cpSeeds = 0, g_cpSeedRecs = 0, g_cpSeedFail = 0, g_cpSeeded = 0, g_cpScans = 0, g_cpRemovedHere = 0, g_cpRemoveChanged = 0, g_cpRemoveFault = 0;
long long g_cpMatchQueued = 0, g_cpGoneSent = 0, g_cpGoneNoKey = 0, g_cpViewWaits = 0, g_cpRemoveHeld = 0, g_cpRetries = 0, g_cpRetryGaveUp = 0;
int g_cpOverLogged = 0;                    /* the "more camps than a seed may carry" line, once per world load */
std::map<std::string, int> g_cpBuildTries; /* list camps that ended NOT BUILT this world load, and how often (campgame.h CampRetryBuild) */
std::map<std::string, void*> g_cpTownOfKey; std::map<void*, std::string> g_cpKeyOfTown;   /* part 6: this game's camp of each list key */
long long g_cpHomeKeyOk = 0, g_cpHomeKeyStale = 0, g_cpHomeKeyUnknown = 0, g_cpHomeKeyBusy = 0, g_cpDestroyDropped = 0, g_cpRemoveWaitWorld = 0;
long long g_cpDebrisSteps = 0, g_cpDebrisForced = 0, g_cpDebrisUnread = 0;   /* option A: furniture steps, those laid out around a list spot, points unreadable */
long long g_cpDebrisBuilding = 0;   /* option A: a list camp's steps centred on one of its buildings (kept at the engine's point) */
int g_cpLogBudget = 200;

std::string CpN(long long v) { std::ostringstream o; o << v; return o.str(); }
void CpLog(const std::string& s) { if (g_cpLogBudget <= 0) return; --g_cpLogBudget; DebugLog("[CAMP] " + s + (g_cpLogBudget == 0 ? " (log cap reached - counters only from here)" : "")); }
bool CpOnMain() { const unsigned long mt = coop::StoreMainThreadId(); return mt != 0 && ::GetCurrentThreadId() == mt; }
bool CpInPass() { const LONG t = g_cpPassTid; return t != 0 && (DWORD)t == ::GetCurrentThreadId(); }
struct CpLocked { CpLocked() { ::EnterCriticalSection(&g_cpLock); } ~CpLocked() { ::LeaveCriticalSection(&g_cpLock); } };

/* ---- POD reads (SEH; no C++ objects in these functions) ---- */
int CpSidPod(void* gd, char* out, int cap)
{
    if (gd == 0) return 0;
    __try
    {
        const char* s = (const char*)gd + kCpGdSid;
        const size_t size = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
        if (size == 0 || size >= (size_t)cap) return 0;
        const char* p = (res >= 16) ? *(const char* const*)s : s;
        if (p == 0) return 0;
        std::memcpy(out, p, size); out[size] = 0; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CpTypePod(void* gd, int* out)
{
    if (gd == 0) return 0;
    __try { *out = *(const int*)((const char*)gd + kCpGdType); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CpRegionPod(void* region, void** gd, int* limit)
{
    if (region == 0) return 0;
    __try { *gd = *(void* const*)((const char*)region + kCpRegion); *limit = *(const int*)region; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CpScalePod(float* out)
{
    if (kCpScaleRva == 0) return 0;
    __try { *out = *(const float*)(uintptr_t)coop::AddrAbs(kCpScaleRva); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CpSpotPod(const float* v, float* out)
{
    __try { out[0] = v[0]; out[1] = v[1]; out[2] = v[2]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* CpDataBySidPod(const std::string* sid)
{
    __try
    {
        ::GameWorld* w = coop::GameWorldPtr();
        return w != 0 ? (void*)w->gamedata.getData(*sid) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- this game's own camps: the TownList's camp list (+0x88 count, +0x90 array - decomp_8f46e0, 92a4f0, 92b6d0) ---- */
const size_t kCpTlAllN = 0x58, kCpTlNestN = 0x88, kCpTlNestArr = 0x90, kCpTownRegion = 0x1A0;
const size_t kCpVtPos = 0x40, kCpVtNestOff = 0x208;   /* RootObject position; Nest vt+0x208 (0x9263A0) - the cell check's call before TownDestroy */
struct CpScanRow { void* town; void* kindGd; void* regionGd; int limit; float x, y, z; };
const int kCpScanCap = (int)campl::kCampSeedMax;
CpScanRow g_cpScanRows[kCpScanCap];        /* the pass's thread only */
void* CpTownListPod()
{
    if (kCpTownListRva == 0) return 0;
    __try { return *(void* const*)(uintptr_t)coop::AddrAbs(kCpTownListRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* one camp: its record, its region's record and limit, its position. 0 = no region (a camp no region counts) or a fault */
int CpNestReadPod(void* t, CpScanRow* r)
{
    if (t == 0) return 0;
    __try
    {
        void* region = *(void* const*)((const char*)t + kCpTownRegion);
        if (region == 0) return 0;
        r->town = t;
        r->kindGd = (void*)((RootObjectBase*)t)->getRecord();
        r->regionGd = *(void* const*)((const char*)region + kCpRegion);
        r->limit = *(const int*)region;
        float p[3] = { 0, 0, 0 };
        const float* q = ((CpPosFn)((*(void***)t)[kCpVtPos / 8]))(t, p);
        if (q == 0) return 0;
        r->x = q[0]; r->y = q[1]; r->z = q[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* every camp of the town list into rows: the count read; -1 = the town list does not read or holds no town yet (no world is loaded -
   the scan waits), -2 = more camps than kCpScanCap */
int CpScanPod(CpScanRow* rows, int cap, long long* noRegion)
{
    void* tl = CpTownListPod();
    if (tl == 0) return -1;
    __try
    {
        const char* c = (const char*)tl;
        if (*(const unsigned*)(c + kCpTlAllN) == 0) return -1;
        const unsigned n = *(const unsigned*)(c + kCpTlNestN);
        void* const* arr = *(void* const* const*)(c + kCpTlNestArr);
        if (n > 0 && arr == 0) return -1;
        int k = 0;
        for (unsigned i = 0; i < n; ++i)
        {
            if (arr[i] == 0) continue;
            if (k >= cap) return -2;
            if (CpNestReadPod(arr[i], &rows[k])) ++k; else ++*noRegion;
        }
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* PART 3 - THE ENGINE'S OWN REMOVAL of one camp (as its cell check 0x8F5100: Nest vt+0x208, then TownDestroy), only when the camp is
   still the one the scan read. 1 = removed, 0 = changed / not listed (nothing done), -1 = a fault or a row missing. MAIN THREAD, under
   g_cpPassMx. */
int CpRemovePod(void* t, void* kindGd, float x, float z)
{
    void* tl = CpTownListPod();
    if (tl == 0 || t == 0 || kCpDestroyRva == 0) return -1;
    __try
    {
        const char* c = (const char*)tl;
        const unsigned n = *(const unsigned*)(c + kCpTlNestN);
        void* const* arr = *(void* const* const*)(c + kCpTlNestArr);
        int listed = 0;
        for (unsigned i = 0; i < n && arr != 0; ++i) if (arr[i] == t) { listed = 1; break; }
        CpScanRow r; int same = 0; float dx = 1.0e9f, dz = 1.0e9f;
        if (listed && CpNestReadPod(t, &r)) { same = r.kindGd == kindGd ? 1 : 0; dx = r.x - x; dz = r.z - z; }
        if (!campg::CampRemoveStill(listed, same, dx, dz)) return 0;
        ((CpNestOffFn)((*(void***)t)[kCpVtNestOff / 8]))(t);
        CpDestroyFn d = orig_cpDestroy != 0 ? orig_cpDestroy : (CpDestroyFn)(uintptr_t)coop::AddrAbs(kCpDestroyRva);
        d(tl, t);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* PART 7 - this game's view point and the engine's distance; 0 = they do not read */
int CpViewPod(float* vp, float* dist)
{
    if (kCpPiRva == 0 || kCpViewRva == 0 || kCpViewDistRva == 0) return 0;
    __try
    {
        void* pi = *(void* const*)(uintptr_t)coop::AddrAbs(kCpPiRva);
        if (pi == 0) return 0;
        float o[3] = { 0, 0, 0 };
        const float* q = ((CpViewFn)(uintptr_t)coop::AddrAbs(kCpViewRva))(pi, o);
        if (q == 0) return 0;
        vp[0] = q[0]; vp[1] = q[1]; vp[2] = q[2];
        *dist = *(const float*)(uintptr_t)coop::AddrAbs(kCpViewDistRva);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the rows as camps (kind and region sids read; a kind that is not a town record is left out) */
void CpRowsToLive(const CpScanRow* rows, int n, std::vector<CpLive>* out, long long* notTown)
{
    out->clear();
    for (int i = 0; i < n; ++i)
    {
        char k[campl::kCampStrMax + 1], rg[campl::kCampStrMax + 1]; int type = 0;
        if (!CpSidPod(rows[i].kindGd, k, (int)sizeof k) || !CpTypePod(rows[i].kindGd, &type) || type != kCpTownType || !CpSidPod(rows[i].regionGd, rg, (int)sizeof rg)) { ++*notTown; continue; }
        CpLive l; l.town = rows[i].town; l.kindGd = rows[i].kindGd; l.kind = k; l.region = rg; l.limit = rows[i].limit; l.x = rows[i].x; l.y = rows[i].y; l.z = rows[i].z;
        out->push_back(l);
    }
}
/* a camp's position (vt+0x40, as the engine reads it right after its build); 0 = a fault */
int CpPosPod(void* t, float* out)
{
    if (t == 0) return 0;
    __try
    {
        float p[3] = { 0, 0, 0 };
        const float* q = ((CpPosFn)((*(void***)t)[kCpVtPos / 8]))(t, p);
        if (q == 0) return 0;
        out[0] = q[0]; out[1] = q[1]; out[2] = q[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* a camp's kind record; 0 = a fault */
int CpKindPod(void* t, void** kindGd)
{
    if (t == 0) return 0;
    __try { *kindGd = (void*)((RootObjectBase*)t)->getRecord(); return *kindGd != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- PART 6: this game's camp of each list key (MAIN THREAD) ---- */
void CpRegDropKey(const std::string& key)
{
    const std::map<std::string, void*>::iterator a = g_cpTownOfKey.find(key);
    if (a == g_cpTownOfKey.end()) return;
    g_cpKeyOfTown.erase(a->second); g_cpTownOfKey.erase(a);
}
void CpRegDropTown(void* town)
{
    const std::map<void*, std::string>::iterator b = g_cpKeyOfTown.find(town);
    if (b == g_cpKeyOfTown.end()) return;
    g_cpTownOfKey.erase(b->second); g_cpKeyOfTown.erase(b);
}
void CpRegSet(const std::string& key, void* town)
{
    if (key.empty() || town == 0) return;
    CpRegDropKey(key); CpRegDropTown(town);
    g_cpTownOfKey[key] = town; g_cpKeyOfTown[town] = key;
    coop::CampHomeBuilt(key, town);   /* the groups owed this camp as their home town get it now */
}
/* the towns TownDestroy took since the last drain leave the key map (a destroyed camp still reads as its kind and spot) */
void CpDrainDestroyed()
{
    std::vector<void*> d; int over = 0;
    {
        CpLocked l;
        if (g_cpDestroyedQ.empty() && !g_cpDestroyedOver) return;
        d.swap(g_cpDestroyedQ); over = g_cpDestroyedOver; g_cpDestroyedOver = 0;
    }
    if (over) { g_cpDestroyDropped += (long long)g_cpTownOfKey.size(); g_cpTownOfKey.clear(); g_cpKeyOfTown.clear(); return; }
    for (size_t i = 0; i < d.size(); ++i) if (g_cpKeyOfTown.find(d[i]) != g_cpKeyOfTown.end()) { CpRegDropTown(d[i]); ++g_cpDestroyDropped; }
}
/* is this town on the engine's camp list (TownList+0x88 / +0x90)? 1 yes, 0 no, -1 unreadable. MAIN THREAD, under g_cpPassMx. */
int CpListedPod(void* t)
{
    void* tl = CpTownListPod();
    if (tl == 0 || t == 0) return -1;
    __try
    {
        const char* c = (const char*)tl;
        const unsigned n = *(const unsigned*)(c + kCpTlNestN);
        void* const* arr = *(void* const* const*)(c + kCpTlNestArr);
        if (n > 0 && arr == 0) return -1;
        for (unsigned i = 0; i < n; ++i) if (arr[i] == t) return 1;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* read live when asked: the camp still reads as the list camp of its key (that kind, within kCampMatchDist of its spot) */
int CpRegStill(void* town, const std::string& key)
{
    const std::map<std::string, campl::Camp>::const_iterator it = g_cpList.find(key);
    if (it == g_cpList.end()) return 0;
    void* kgd = 0; float p[3] = { 0, 0, 0 }; char k[campl::kCampStrMax + 1];
    if (!CpKindPod(town, &kgd) || !CpSidPod(kgd, k, (int)sizeof k) || !CpPosPod(town, p)) return 0;
    return campg::CampRemoveStill(1, it->second.kind == k ? 1 : 0, p[0] - it->second.x, p[2] - it->second.z);
}

/* ---- the hooks ---- */
/* The kind picker. Every call but the pass's own (campgame.h CampKindAnswer: another thread, a call inside the camp's build - its
   material - or another record type): the engine's own. In a BUILD pass: the forced camp's kind in its region only, none anywhere else
   (the engine skips a region with no kind). In a DRAW pass: none once a camp was drawn, none in a region the world server answered
   "full" for, else the engine's own pick, remembered with its region's key. */
void* detour_cpKind(void* factory, void* gd, void* listName, unsigned long long type, unsigned long long p5)
{
    if (campg::CampKindAnswer(CpInPass() ? 1 : 0, g_cpSpawnDepth, (int)(unsigned int)(type & 0xFFFFFFFFull), kCpTownType) == campg::kKindEngine) return orig_cpKind(factory, gd, listName, type, p5);
    char rsid[campl::kCampStrMax + 1];
    if (g_cpMode == campg::kPassBuild)
    {
        if (g_cpKindGiven || !CpSidPod(gd, rsid, (int)sizeof rsid) || g_cpForced.rec.region != rsid) return 0;
        g_cpKindGiven = 1;
        return g_cpForced.kindGd;
    }
    if (g_cpDrawDone) return 0;
    if (!CpSidPod(gd, rsid, (int)sizeof rsid)) { ++g_cpDrawUnreadable; return 0; }
    if (g_cpFullSnap.find(rsid) != g_cpFullSnap.end()) return 0;
    void* k = orig_cpKind(factory, gd, listName, type, p5);
    g_cpDrawKind = k; g_cpDrawRegion = rsid;
    return k;
}
/* The spot picker. In a BUILD pass, after the forced kind: the forced spot. In a DRAW pass: the engine's own spot becomes the proposal
   and the engine is handed (0,0,0) - it builds nothing and goes on to the next region, where the kind picker hands out none. */
float* detour_cpSpot(void* region, float* out, void* kind)
{
    if (!CpInPass() || g_cpSpawnDepth != 0) return orig_cpSpot(region, out, kind);
    if (g_cpMode == campg::kPassBuild)
    {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f;
        if (!g_cpKindGiven || g_cpSpotGiven || g_cpNearView) return out;
        const float spot[3] = { g_cpForced.rec.x, g_cpForced.rec.y, g_cpForced.rec.z };
        float vp[3] = { 0, 0, 0 }, dist = 0.0f;
        if (!CpViewPod(vp, &dist)) ++g_cpViewUnread;   /* no view point (none to stand near): built */
        else if (campg::CampNearView(vp, spot, dist)) { g_cpNearView = 1; return out; }   /* part 7: the spot is withheld - the camp waits */
        out[0] = spot[0]; out[1] = spot[1]; out[2] = spot[2]; g_cpSpotGiven = 1;
        return out;
    }
    float* r = orig_cpSpot(region, out, kind);
    if (g_cpDrawDone || g_cpDrawKind == 0) return r;
    float s[3] = { 0, 0, 0 };
    if (!CpSpotPod(out, s) || (s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f)) return r;   /* the engine's "no spot": it skips the region itself */
    g_cpDrawDone = 1;
    out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f;
    char ksid[campl::kCampStrMax + 1]; void* rgd = 0; int limit = 0, type = 0; float scale = 0.0f;
    if (!CpSidPod(g_cpDrawKind, ksid, (int)sizeof ksid) || !CpTypePod(g_cpDrawKind, &type) || type != kCpTownType
        || !CpRegionPod(region, &rgd, &limit) || !CpScalePod(&scale)) { ++g_cpDrawUnreadable; return out; }
    campl::Camp c;
    c.kind = ksid; c.region = g_cpDrawRegion; c.x = s[0]; c.y = s[1]; c.z = s[2];
    c.regionCap = campg::CampCapOf(limit, scale);
    c.key = campl::CampKey(c.kind, c.x, c.z);
    if (!campl::CampValid(c)) { ++g_cpDrawUnreadable; return out; }
    g_cpDrawRec = c;
    return out;
}
/* The build. Its outermost call on a BUILD pass's thread after the forced spot is the forced camp's build; its answer is the engine's,
   and the camp's position is read as the engine reads it right after (the BUILT line compares it with the list's spot). */
void* detour_cpSpawn(void* self, void* gd, void* pos, unsigned long long p4, void* p5, void* p6, void* p7, void* p8, void* p9,
                     unsigned long long p10, unsigned long long p11, unsigned long long p12)
{
    if (!CpInPass()) return orig_cpSpawn(self, gd, pos, p4, p5, p6, p7, p8, p9, p10, p11, p12);
    const int outer = g_cpSpawnDepth == 0 ? 1 : 0;
    ++g_cpSpawnDepth;
    void* r = orig_cpSpawn(self, gd, pos, p4, p5, p6, p7, p8, p9, p10, p11, p12);
    --g_cpSpawnDepth;
    if (outer && g_cpMode == campg::kPassBuild && g_cpSpotGiven && !g_cpSpawnSeen)
    {
        g_cpSpawnSeen = 1; g_cpSpawned = r != 0 ? 1 : 0; g_cpSpawnTown = r;
        g_cpSpawnPosOk = (r != 0 && CpPosPod(r, g_cpSpawnPos)) ? 1 : 0;
    }
    return r;
}
/* OPTION A - A CAMP'S FURNITURE (campgame.h CampDebrisPoint). A list camp's furniture step is handed a copy of its point with the
   list spot's x / z (the engine's height kept); the caller's point is untouched, and every other step gets the engine's own point. */
void detour_cpDebris(void* town, void* rec, float* pt, unsigned long long hand)
{
    const int onMain = CpOnMain() ? 1 : 0;
    if (!onMain) { ::InterlockedIncrement(&g_cpDebrisOffMain); orig_cpDebris(town, rec, pt, hand); return; }
    float eng[3] = { 0, 0, 0 };
    if (g_cpLockReady == 0 || town == 0 || pt == 0 || !CpSpotPod(pt, eng)) { ++g_cpDebrisUnread; orig_cpDebris(town, rec, pt, hand); return; }
    ++g_cpDebrisSteps;
    CpDrainDestroyed();
    int listCamp = 0; float lx = 0.0f, lz = 0.0f;
    const std::map<void*, std::string>::const_iterator it = g_cpKeyOfTown.find(town);
    if (it != g_cpKeyOfTown.end() && CpRegStill(town, it->second))
    {
        const std::map<std::string, campl::Camp>::const_iterator c = g_cpList.find(it->second);
        if (c != g_cpList.end()) { listCamp = 1; lx = c->second.x; lz = c->second.z; }
    }
    float use[3] = { 0, 0, 0 };
    if (listCamp && hand != 0) ++g_cpDebrisBuilding;
    const int forced = campg::CampDebrisPoint(onMain, listCamp, hand == 0 ? 1 : 0, lx, lz, eng, use) == campg::kDebrisForced ? 1 : 0;
    if (!forced) { orig_cpDebris(town, rec, pt, hand); return; }
    ++g_cpDebrisForced;
    orig_cpDebris(town, rec, use, hand);
}
/* The pass. Decided at its start under the lock (campgame.h CampPassMode); a BUILD takes its camp off the queue - the build ends
   this pass, built or not (CampBuildOutcome). */
struct CpPassHeld { CpPassHeld() { ::EnterCriticalSection(&g_cpPassMx); } ~CpPassHeld() { ::LeaveCriticalSection(&g_cpPassMx); } };
void detour_cpPass(void* grid)
{
    if (g_cpReady == 0 || g_cpLockReady == 0) { orig_cpPass(grid); return; }
    CpPassHeld held;   /* the main thread's camp removals never overlap a pass */
    int mode = campg::kPassSkip, scanWhy = campg::kScanNone; long scanGen = 0;
    {
        CpLocked l;
        const int retry = campg::CampTakeViewRetry(g_cpBuildQ.empty() ? 0 : 1, g_cpViewWait.empty() ? 0 : 1, g_cpLastViewRetry);
        mode = campg::CampPassMode(1, coop::RoleIsSingle() ? 1 : 0, g_cpScanDue != campg::kScanNone ? 1 : 0, (!g_cpBuildQ.empty() || retry) ? 1 : 0, g_cpWaiting, g_cpListComplete, g_cpListState);
        g_cpPassLoadGen = g_cpLoadGen;
        g_cpLastViewRetry = 0;
        if (mode == campg::kPassBuild)
        {
            if (!g_cpBuildQ.empty()) { g_cpForced = g_cpBuildQ.front(); g_cpBuildQ.pop_front(); }
            else { g_cpForced = g_cpViewWait.front(); g_cpViewWait.pop_front(); g_cpLastViewRetry = 1; }
            ++g_cpPassesBuild;
        }
        else if (mode == campg::kPassDraw) { g_cpFullSnap = g_cpRegionFull; ++g_cpPassesDraw; }
        else if (mode == campg::kPassScan) { scanWhy = g_cpScanDue; scanGen = g_cpScanGen; }
        else if (mode == campg::kPassSkip) ++g_cpPassesSkipped;
        else ++g_cpPassesAlone;
    }
    if (mode == campg::kPassSkip) return;
    if (mode == campg::kPassEngineAlone) { orig_cpPass(grid); return; }
    if (mode == campg::kPassScan)
    {
        /* parts 2 / 3: this game's camps, read on the thread the engine itself adds camps on, once the world runs; the pass does nothing
           else */
        if (!campg::CampScanWorldReady(coop::StoreWorldLoadAgeMs() != sqorigin::kLoadAgeUnknown ? 1 : 0, coop::EngineWritesBlocked() ? 1 : 0)) { CpLocked l; ++g_cpScanWaits; ++g_cpScanNotRunning; return; }
        const long worldGen = coop::StoreWorldGenNow();
        long long noRegion = 0, notTown = 0;
        const int n = CpScanPod(g_cpScanRows, kCpScanCap, &noRegion);
        std::vector<CpLive> live;
        if (n >= 0) CpRowsToLive(g_cpScanRows, n, &live, &notTown);
        float scale = 0.0f; const int scaleOk = CpScalePod(&scale);
        CpLocked l;
        if (n == -1 || !scaleOk) { ++g_cpScanWaits; return; }   /* no world loaded yet: the scan waits for a later pass */
        if (g_cpScanDue != scanWhy || g_cpScanGen != scanGen || g_cpPassLoadGen != g_cpLoadGen) return;   /* asked again meanwhile: the next pass reads it */
        g_cpScanDue = campg::kScanNone;
        if (n == -2) { ++g_cpScanOver; return; }   /* more camps than a seed may carry: this game keeps its own (logged on the main thread) */
        g_cpScanNoRegion += noRegion; g_cpScanNotTown += notTown;
        g_cpScanOut.swap(live); g_cpScanReady = 1; g_cpScanOutWhy = scanWhy; g_cpScanOutGen = scanGen; g_cpScanOutLoad = g_cpLoadGen; g_cpScanOutWorld = worldGen; g_cpScanScale = scale;
        return;
    }
    g_cpMode = mode;
    g_cpKindGiven = 0; g_cpSpotGiven = 0; g_cpSpawnSeen = 0; g_cpSpawned = 0; g_cpSpawnDepth = 0; g_cpNearView = 0;
    g_cpSpawnTown = 0; g_cpSpawnPosOk = 0;
    g_cpDrawKind = 0; g_cpDrawRegion.clear(); g_cpDrawDone = 0; g_cpDrawRec = campl::Camp();
    ::InterlockedExchange(&g_cpPassTid, (LONG)::GetCurrentThreadId());
    orig_cpPass(grid);
    ::InterlockedExchange(&g_cpPassTid, 0);
    CpLocked l;
    if (mode == campg::kPassBuild)
    {
        CpDone d; d.rec = g_cpForced.rec; d.outcome = campg::CampBuildOutcome(g_cpKindGiven, g_cpNearView, g_cpSpotGiven, g_cpSpawned);
        d.town = g_cpSpawnTown; d.posOk = g_cpSpawnPosOk; d.x = g_cpSpawnPos[0]; d.y = g_cpSpawnPos[1]; d.z = g_cpSpawnPos[2];
        if (g_cpPassLoadGen == g_cpLoadGen)
        {
            if (d.outcome == campg::kBuiltNearView) { g_cpViewWait.push_back(g_cpForced); ++g_cpViewWaits; }   /* part 7: it waits for the view point to leave */
            else g_cpDoneQ.push_back(d);
        }
        g_cpForced = CpBuild();
        return;
    }
    if (!g_cpDrawRec.key.empty() && g_cpPassLoadGen != g_cpLoadGen) ++g_cpStaleDraws;
    else if (!g_cpDrawRec.key.empty())
    {
        g_cpWaiting = 1; g_cpWaitKey = g_cpDrawRec.key; g_cpWaitSent = 0; g_cpWaitGen = 0;
        g_cpHaveDrawn = 1; g_cpDrawn = g_cpDrawRec;
    }
    g_cpFullSnap.clear();
}

/* PART 4 - TownDestroy. Only a call from the engine's cell check (its return address, CampNestCheckRet) is a camp gone in play
   (campgame.h CampGoneSend says why); the camp is read before the engine unlists it and handed to the main thread. Every call, from any
   caller, hands the town to the main thread to leave the key map (part 6, CpDrainDestroyed), and goes on to the engine's own. ANY THREAD. */
void detour_cpDestroy(void* list, void* town)
{
    if (g_cpLockReady != 0 && town != 0) { CpLocked l; if (g_cpDestroyedQ.size() < kCpDestroyedCap) g_cpDestroyedQ.push_back(town); else g_cpDestroyedOver = 1; }
    if (g_cpReady != 0 && g_cpLockReady != 0 && kCpNestRetRva != 0 && town != 0 && town != g_cpOwnTown
        && (unsigned long long)(uintptr_t)_ReturnAddress() == coop::AddrAbs(kCpNestRetRva))
    {
        CpScanRow r; std::vector<CpLive> one; long long notTown = 0;
        if (CpNestReadPod(town, &r)) CpRowsToLive(&r, 1, &one, &notTown);
        if (!one.empty())
        {
            CpGone g; g.kind = one[0].kind; g.x = one[0].x; g.z = one[0].z;
            CpLocked l;
            if (g_cpGoneQ.size() < kCpGoneCap) g_cpGoneQ.push_back(g); else ++g_cpGoneOver;
        }
    }
    orig_cpDestroy(list, town);
}

void CpHook(unsigned long long rva, void* detour, void** orig, const char* what, int* ok)
{
    const unsigned long long abs = rva != 0 ? coop::AddrAbs(rva) : 0;
    if (abs == 0) { ErrorLog(std::string("[CAMP] ") + what + " is not in the address table - NOT HOOKED (the engine's own step runs)"); *ok = 0; return; }
    const coop::HookStatus s = coop::AddHook((void*)(uintptr_t)abs, detour, orig);
    DebugLog(std::string("[CAMP] ") + what + (s == coop::SUCCESS ? " AddHook SUCCESS" : " AddHook FAILED"));
    if (s != coop::SUCCESS) { *orig = 0; *ok = 0; }
}

void CpSend(const campl::CampMsg& m)
{
    std::vector<char> b;
    if (!campl::CampEncode(&b, m)) { CpLog("a CAMP message would not encode (op " + CpN((long long)m.op) + ") - not sent"); return; }
    coop::StoreSendCamp(b);
}
void CpSendNotBuilt(const std::string& key, unsigned int reason)
{
    if (coop::RoleIsSingle() || !coop::StoreCampLinkUp()) return;   /* a message nobody can receive */
    campl::CampMsg m; m.op = campl::kOpNotBuilt; m.reason = reason; m.key = key; CpSend(m);
}

void CpQueueBuild(const campl::Camp& c, const char* how);
/* AN ACCEPTED CAMP (a STORED outside a whole list; or, in single player, this game's own proposal answered here). MAIN THREAD. */
void CpAccepted(const campl::Camp& c, const char* how)
{
    int wasMine = 0;
    {
        CpLocked l;
        if (g_cpWaiting && g_cpWaitKey == c.key) { g_cpWaiting = 0; g_cpWaitKey.clear(); g_cpWaitSent = 0; wasMine = 1; }
    }
    const int act = campg::CampStoredAction(g_cpList.find(c.key) != g_cpList.end() ? 1 : 0, g_cpBuiltHere.find(c.key) != g_cpBuiltHere.end() ? 1 : 0);
    g_cpList[c.key] = c;
    if (act == campg::kStoredKnown) { ++g_cpKnown; return; }
    if (wasMine) ++g_cpAccepted; else ++g_cpOthers;
    CpQueueBuild(c, how);
}
/* a camp of the list queued for the engine's next camp pass (or NOT_BUILT: this game has no such town record). MAIN THREAD. */
void CpQueueBuild(const campl::Camp& c, const char* how)
{
    CpBuild b; b.rec = c; b.kindGd = CpDataBySidPod(&c.kind);
    int type = 0;
    if (b.kindGd == 0 || !CpTypePod(b.kindGd, &type) || type != kCpTownType)
    {
        ++g_cpNoKind;
        g_cpBuildTries[c.key] = campg::kCampTriesNoKind;   /* never tried again this world load */
        CpLog("camp '" + c.key + "' (" + how + ") NOT BUILT: this game has no town record '" + c.kind + "' - NOT_BUILT sent");
        CpSendNotBuilt(c.key, campl::kNbNoKind);
        return;
    }
    g_cpBuiltHere.insert(c.key);
    { CpLocked l; g_cpBuildQ.push_back(b); }
    CpLog("camp '" + c.key + "' region '" + c.region + "' " + how + " - queued for the engine's next camp pass");
}

void CpEndWait(const char* why)
{
    CpLocked l;
    if (!g_cpWaiting) return;
    g_cpWaiting = 0; g_cpWaitKey.clear(); g_cpWaitSent = 0; g_cpHaveDrawn = 0;
    ++g_cpWaitLost;
    CpLog(std::string("the waiting proposal ended without an answer: ") + why);
}

/* this game's camps are to be read by the next camp pass (why: campgame.h CampScanWhy), for the link generation gen. MAIN THREAD */
void CpAskScan(int why, long gen)
{
    if (why == campg::kScanNone) return;
    CpLocked l; g_cpScanDue = why; g_cpScanGen = gen;
}

/* PART 2 - THE SEED (owner 1376): every camp this game holds, then SEED_END with their number; the world server answers SEEDED and
   sends the whole list (this game's camps, or the list another game's seed made first). A single-player game's camps are its list. Only
   from a scan of the world that still runs (worldGen: the world-load generation the scan read). */
void CpSeed(const std::vector<CpLive>& live, float scale, long gen, int single, long worldGen)
{
    if (worldGen != coop::StoreWorldGenNow() || coop::StoreWorldLoadAgeMs() == sqorigin::kLoadAgeUnknown)
    {
        ++g_cpSeedFail;
        CpLog("the camps read for the seed belong to a world load that is over - not used (the next whole list decides again)");
        return;
    }
    std::vector<campl::Camp> recs; std::vector<void*> towns;
    for (size_t i = 0; i < live.size(); ++i)
    {
        campl::Camp c;
        if (campg::CampFromScan(live[i].kind, live[i].region, live[i].limit, scale, live[i].x, live[i].y, live[i].z, &c)) { recs.push_back(c); towns.push_back(live[i].town); }
    }
    if (single)
    {
        for (size_t i = 0; i < recs.size(); ++i) { g_cpList[recs[i].key] = recs[i]; CpRegSet(recs[i].key, towns[i]); }
        g_cpLoadScanned = 1;
        CpLog("this single-player game's own camps are its list: " + CpN((long long)recs.size()) + " of " + CpN((long long)live.size()) + " camp(s) read");
        return;
    }
    if (gen != coop::StoreNotebookLinkGen() || !coop::StoreCampLinkUp()) { ++g_cpSeedFail; CpLog("the seed was read for a link that is gone - not sent (the next whole list decides again)"); return; }
    unsigned int sent = 0;
    for (size_t i = 0; i < recs.size(); ++i)
    {
        campl::CampMsg m; m.op = campl::kOpSeed; m.rec = recs[i];
        std::vector<char> b;
        if (!campl::CampEncode(&b, m)) continue;
        if (!coop::StoreSendCamp(b)) { ++g_cpSeedFail; CpLog("the seed stopped: the link went down after " + CpN((long long)sent) + " camp(s) - the world server drops it with the connection"); return; }
        ++sent;
    }
    campl::CampMsg e; e.op = campl::kOpSeedEnd; e.count = sent;
    std::vector<char> b;
    if (!campl::CampEncode(&b, e) || !coop::StoreSendCamp(b)) { ++g_cpSeedFail; CpLog("the seed's end could not be sent - the world server drops the seed with the connection"); return; }
    g_cpSeededGen = gen;   /* the whole seed went out on this link */
    ++g_cpSeeds; g_cpSeedRecs += sent;
    CpLog("the world has no camp list: this game's " + CpN((long long)sent) + " camp(s) sent as its seed (" + CpN((long long)live.size()) + " read)");
}

/* PART 3 - THIS GAME'S CAMPS MADE TO MATCH THE LIST (owner 1376): the camps no list camp names are removed (CpRunRemovals), then the
   list camps this game lacks are queued for the engine's pass. MAIN THREAD */
void CpMatch(const std::vector<CpLive>& live)
{
    std::vector<campg::LiveCamp> lc(live.size());
    for (size_t i = 0; i < live.size(); ++i) { lc[i].kind = live[i].kind; lc[i].x = live[i].x; lc[i].z = live[i].z; }
    std::vector<std::string> toBuild, liveKey; std::vector<size_t> toRemove;
    campg::CampMatchLive(g_cpList, lc, &toBuild, &toRemove, &liveKey);
    g_cpRemoveQ.clear(); g_cpMatchBuilds.clear();
    for (size_t i = 0; i < live.size(); ++i) if (!liveKey[i].empty()) CpRegSet(liveKey[i], live[i].town);   /* part 6: this game's camp of each list key */
    for (std::map<std::string, void*>::iterator it = g_cpTownOfKey.begin(); it != g_cpTownOfKey.end(); )
    {
        if (g_cpList.find(it->first) != g_cpList.end()) { ++it; continue; }
        g_cpKeyOfTown.erase(it->second); g_cpTownOfKey.erase(it++);
    }
    for (size_t i = 0; i < toRemove.size(); ++i) { const CpLive& l = live[toRemove[i]]; CpRemove r; r.town = l.town; r.kindGd = l.kindGd; r.kind = l.kind; r.x = l.x; r.z = l.z; r.held = 0; g_cpRemoveQ.push_back(r); }
    for (size_t i = 0; i < toBuild.size(); ++i)
    {
        if (g_cpBuiltHere.find(toBuild[i]) != g_cpBuiltHere.end()) continue;
        const std::map<std::string, int>::const_iterator t = g_cpBuildTries.find(toBuild[i]);
        const int tries = t != g_cpBuildTries.end() ? t->second : 0;
        if (!campg::CampRetryBuild(tries)) { ++g_cpRetryGaveUp; continue; }   /* tried its limit this world load */
        if (tries > 0) ++g_cpRetries;
        g_cpMatchBuilds.push_back(toBuild[i]);
    }
    CpLog("this game's " + CpN((long long)live.size()) + " camp(s) against the list's " + CpN((long long)g_cpList.size()) + ": " + CpN((long long)g_cpRemoveQ.size())
          + " to remove, " + CpN((long long)g_cpMatchBuilds.size()) + " to build");
}
/* the removals whose area is not loaded here, while no camp pass runs (tried, never waited on); a camp in a loaded area waits, read
   again on every tick until its area goes (campgame.h CampRemoveWhen). Then the builds. A removal frees a place in its region: a list
   camp that ended NOT BUILT is tried again by a new match (CampRetryBuild). MAIN THREAD */
void CpRunRemovals()
{
    if (g_cpRemoveQ.empty() && g_cpMatchBuilds.empty()) return;
    /* only while the world runs (the scan's own test): during a load or a quit to the title the area bytes read 0 and the camps are being
       torn down - the queue waits (the world load's start or the teardown clears it) */
    if (!campg::CampScanWorldReady(coop::StoreWorldLoadAgeMs() != sqorigin::kLoadAgeUnknown ? 1 : 0, coop::EngineWritesBlocked() ? 1 : 0)) { ++g_cpRemoveWaitWorld; return; }
    if (!g_cpRemoveQ.empty())
    {
        std::vector<CpRemove> now, held;
        for (size_t i = 0; i < g_cpRemoveQ.size(); ++i)
        {
            CpRemove r = g_cpRemoveQ[i];
            const coop::Sector s = coop::SectorOf(r.x, r.z);
            if (campg::CampRemoveWhen(coop::ZoneKeepSectorTri(s.x, s.y)) == campg::kRemoveNow) { now.push_back(r); continue; }
            if (!r.held) { r.held = 1; ++g_cpRemoveHeld; CpLog("camp '" + campl::CampKey(r.kind, r.x, r.z) + "' (not on the world's list) waits: its area is loaded here - removed once the area unloads"); }
            held.push_back(r);
        }
        int removed = 0;
        if (!now.empty())
        {
            if (!::TryEnterCriticalSection(&g_cpPassMx)) return;   /* a pass runs: the next tick */
            for (size_t i = 0; i < now.size(); ++i)
            {
                const CpRemove& r = now[i];
                g_cpOwnTown = r.town;
                const int got = CpRemovePod(r.town, r.kindGd, r.x, r.z);
                g_cpOwnTown = 0;
                if (got == 1) { ++g_cpRemovedHere; ++removed; CpRegDropTown(r.town); CpLog("camp '" + campl::CampKey(r.kind, r.x, r.z) + "' REMOVED here: the world's list does not hold it"); }
                else if (got == 0) ++g_cpRemoveChanged;
                else { ++g_cpRemoveFault; CpLog("camp '" + campl::CampKey(r.kind, r.x, r.z) + "' NOT removed: the engine's removal faulted or a row is missing"); }
            }
            ::LeaveCriticalSection(&g_cpPassMx);
        }
        g_cpRemoveQ.swap(held);
        if (removed && !g_cpBuildTries.empty())
        {
            int complete = 0; unsigned int state = 0; { CpLocked l; complete = g_cpListComplete; state = g_cpListState; }
            if (complete && state == (unsigned)campl::kListHere) CpAskScan(campg::kScanMatch, coop::StoreNotebookLinkGen());
        }
    }
    for (size_t i = 0; i < g_cpMatchBuilds.size(); ++i)
    {
        const std::map<std::string, campl::Camp>::const_iterator it = g_cpList.find(g_cpMatchBuilds[i]);
        if (it == g_cpList.end() || g_cpBuiltHere.find(it->first) != g_cpBuiltHere.end()) continue;
        ++g_cpMatchQueued;
        CpQueueBuild(it->second, "on the list, not in this game");
    }
    g_cpMatchBuilds.clear();
}
/* PART 4 - camps the engine's cell check removed: GONE for each that is a camp of the list (the world server takes it from the camp's
   area holder only); single player: it leaves the list. MAIN THREAD */
void CpTakeGone(int single, int up)
{
    std::vector<CpGone> gone;
    { CpLocked l; gone.swap(g_cpGoneQ); }
    int complete = 0; { CpLocked l; complete = g_cpListComplete; }
    for (size_t i = 0; i < gone.size(); ++i)
    {
        campg::LiveCamp c; c.kind = gone[i].kind; c.x = gone[i].x; c.z = gone[i].z;
        const std::string key = campg::CampMatchOne(g_cpList, c);
        if (!campg::CampGoneSend(1, 0, key.empty() ? 0 : 1, single, complete, up)) { ++g_cpGoneNoKey; continue; }
        ++g_cpGoneSent;
        CpLog("camp '" + key + "' removed by the engine's cell check - " + (single ? "it leaves this game's list" : "GONE sent"));
        CpRegDropKey(key);
        if (single) { std::map<std::string, campl::Camp>::iterator it = g_cpList.find(key); if (it != g_cpList.end()) { CpLocked l; g_cpRegionFull.erase(it->second.region); } g_cpList.erase(key); continue; }
        campl::CampMsg m; m.op = campl::kOpGone; m.key = key; CpSend(m);
    }
}

}   // namespace


namespace coop {

int CampViewPoint(float* vp, float* dist) { return CpViewPod(vp, dist); }   /* T-1235: see camp.h */

void CampInstall()
{
    ::InitializeCriticalSection(&g_cpLock);
    ::InitializeCriticalSection(&g_cpPassMx);
    ::InterlockedExchange(&g_cpLockReady, 1);
    int ok = 1;
    CpHook(kCpKindRva, (void*)&detour_cpKind, (void**)&orig_cpKind, "the kind picker 0x57F880", &ok);
    CpHook(kCpSpotRva, (void*)&detour_cpSpot, (void**)&orig_cpSpot, "the spot picker 0x8F5480", &ok);
    CpHook(kCpSpawnRva, (void*)&detour_cpSpawn, (void**)&orig_cpSpawn, "the build 0x582970", &ok);
    if (kCpScaleRva == 0) { ErrorLog("[CAMP] CampLimitScale is not in the address table - the camp rule is OFF (the engine's camp pass runs as itself)"); ok = 0; }
    CpHook(kCpPassRva, (void*)&detour_cpPass, (void**)&orig_cpPass, "the camp pass 0x8F6440", &ok);   /* last: the rule turns on only with the three it needs */
    if (orig_cpPass != 0 && ok) ::InterlockedExchange(&g_cpReady, 1);
    int goneOk = 1;   /* part 4: without it no GONE is sent (the rest of the rule stands; a removal then calls the engine's TownDestroy directly) */
    if (kCpNestRetRva == 0) { ErrorLog("[CAMP] CampNestCheckRet is not in the address table - no GONE is sent"); goneOk = 0; }
    else CpHook(kCpDestroyRva, (void*)&detour_cpDestroy, (void**)&orig_cpDestroy, "TownDestroy 0x92A4F0 (camps gone in play)", &goneOk);
    int debrisOk = 1;   /* option A: without it a camp's furniture is laid out around the engine's own point (the rest of the rule stands) */
    CpHook(kCpDebrisRva, (void*)&detour_cpDebris, (void**)&orig_cpDebris, "the camp furniture step 0x937570 (a list camp's furniture around its list spot)", &debrisOk);
    DebugLog(std::string("[CAMP] the world's camp list rule is ") + (g_cpReady ? "ON (one road: draw -> propose -> the list -> build)" : "OFF (a hook or row is missing - the engine's camp pass runs as itself)"));
}

void CampArrive(const std::vector<char>& payload)
{
    if (!CpOnMain() || g_cpLockReady == 0) return;
    campl::CampMsg m;
    if (payload.empty() || !campl::CampDecode(&payload[0], payload.size(), &m)) { ++g_cpMalformed; if (g_cpMalformed <= 5) CpLog("a malformed CAMP from the world server - ignored"); return; }
    switch (m.op)
    {
    case campl::kOpBegin:
        g_cpList.clear(); g_cpInList = 1; g_cpBeginCount = m.count;
        { CpLocked l; g_cpListComplete = 0; g_cpListState = m.flag; }
        return;
    case campl::kOpStored:
        if (g_cpInList) { g_cpList[m.rec.key] = m.rec; return; }
        CpAccepted(m.rec, "accepted by the world server");
        return;
    case campl::kOpEnd:
    {
        const int complete = campg::CampListEndComplete(g_cpInList, g_cpBeginCount, m.count, (unsigned int)g_cpList.size());
        g_cpInList = 0;
        unsigned int state = 0;
        { CpLocked l; g_cpListComplete = complete; state = g_cpListState; }
        if (complete) ++g_cpLists; else ++g_cpListsIncomplete;
        {
            const long gen = coop::StoreNotebookLinkGen();
            CpAskScan(campg::CampScanWhy(0, 0, complete, state, g_cpSeededGen == gen ? 1 : 0), gen);   /* parts 2 / 3: the seed, or this game's camps made to match */
        }
        CpLog("the world's camp list: " + CpN((long long)g_cpList.size()) + " camp(s), state " + CpN((long long)state)
              + (complete ? (state == (unsigned)campl::kListHere ? " - proposals open" : state == (unsigned)campl::kListNone ? " - the world has no list yet (no proposal until its seed)" : " - unreadable on the world server (no proposal this session)")
                          : " - INCOMPLETE (the count disagrees) - no proposal until the next whole list"));
        return;
    }
    case campl::kOpRefused:
    {
        int act = campg::kRefusedStale;
        {
            CpLocked l;
            act = campg::CampRefusedAction(g_cpWaiting, g_cpWaitKey == m.rec.key ? 1 : 0, m.rec.key == "?" ? 1 : 0, m.reason);
            if (act != campg::kRefusedStale) { g_cpWaiting = 0; g_cpWaitKey.clear(); g_cpWaitSent = 0; }
            if (act == campg::kRefusedEndsMarkFull && !m.rec.region.empty()) g_cpRegionFull.insert(m.rec.region);
        }
        if (act == campg::kRefusedStale) { ++g_cpRefusedStale; return; }
        ++g_cpRefused;
        CpLog("proposal '" + m.rec.key + "' REFUSED (reason " + CpN((long long)m.reason) + ")" + (act == campg::kRefusedEndsMarkFull ? " - its region is full: no proposal for it until a camp of it is removed" : ""));
        return;
    }
    case campl::kOpRemoved:
    {
        std::map<std::string, campl::Camp>::iterator it = g_cpList.find(m.key);
        const int held = it != g_cpList.end() ? 1 : 0;
        if (held) { CpLocked l; g_cpRegionFull.erase(it->second.region); }
        if (held) g_cpList.erase(it);
        CpRegDropKey(m.key);
        ++g_cpRemoved;
        if (held)
        {
            /* the camp left the world on its area's holder: this game's copy goes the same way (the match removes what the list no longer holds) */
            int complete = 0; unsigned int state = 0; { CpLocked l; complete = g_cpListComplete; state = g_cpListState; }
            if (complete && state == (unsigned)campl::kListHere) CpAskScan(campg::kScanMatch, coop::StoreNotebookLinkGen());
        }
        return;
    }
    case campl::kOpSeeded:
        ++g_cpSeeded;
        CpLog(std::string("the seed was ") + (m.flag ? "TAKEN: this game's camps are the world's list" : "REFUSED (the world has a list already, it could not be read, or this connection holds no player slot there) - the list as it stands follows when this connection holds a player slot")
              + " (" + CpN((long long)m.count) + " camp(s))");
        return;   /* the whole list follows it */
    default:
        return;
    }
}

void CampWorldLoadBegan()
{
    if (!CpOnMain() || g_cpLockReady == 0) return;
    CpLocked l;
    g_cpBuildQ.clear(); g_cpDoneQ.clear(); g_cpWaiting = 0; g_cpWaitKey.clear(); g_cpWaitSent = 0; g_cpHaveDrawn = 0;
    g_cpRegionFull.clear(); g_cpListComplete = 0; ++g_cpLoadGen;
    g_cpList.clear(); g_cpInList = 0; g_cpBuiltHere.clear();
    g_cpViewWait.clear(); g_cpLastViewRetry = 0; g_cpScanDue = campg::kScanNone; g_cpScanReady = 0; g_cpScanOut.clear(); g_cpGoneQ.clear();
    g_cpRemoveQ.clear(); g_cpMatchBuilds.clear(); g_cpLoadScanned = 0; g_cpSeededGen = -1;
    g_cpScanOver = 0; g_cpOverLogged = 0; g_cpBuildTries.clear(); g_cpTownOfKey.clear(); g_cpKeyOfTown.clear();
    g_cpDestroyedQ.clear(); g_cpDestroyedOver = 0;
}

void CampWorldTeardown()
{
    if (g_cpLockReady == 0) return;
    g_cpTownOfKey.clear(); g_cpKeyOfTown.clear();   /* this game's camps by key name the world that is going */
    g_cpRemoveQ.clear(); g_cpMatchBuilds.clear();   /* ... and so do the removals and the builds a match queued */
    CpLocked l; g_cpDestroyedQ.clear(); g_cpDestroyedOver = 0;
}

void CampTick()
{
    if (!CpOnMain() || g_cpLockReady == 0 || g_cpReady == 0) return;
    CpDrainDestroyed();   /* part 6: towns TownDestroy took leave the key map before anything below reads or sets it */
    const int single = coop::RoleIsSingle() ? 1 : 0;
    const int up = single ? 0 : (coop::StoreCampLinkUp() ? 1 : 0);
    const long gen = coop::StoreNotebookLinkGen();
    if (gen != g_cpLinkGenSeen)
    {
        g_cpLinkGenSeen = gen;
        g_cpInList = 0;
        g_cpRemoveQ.clear(); g_cpMatchBuilds.clear();   /* a match read against the old link's list is not acted on */
        CpLocked l; g_cpListComplete = 0;   /* a new connection's whole list follows */
        if (!single) { g_cpScanDue = campg::kScanNone; g_cpScanReady = 0; }
    }
    /* parts 2 / 3: a single-player game reads its own camps once per world load; a scan's result */
    {
        int due = 0; { CpLocked l; due = g_cpScanDue != campg::kScanNone || g_cpScanReady; }
        if (single && !due) CpAskScan(campg::CampScanWhy(1, g_cpLoadScanned, 0, 0, 0), 0);
        std::vector<CpLive> live; int why = 0; long sgen = 0, sworld = 0; float scale = 0.0f; int have = 0, over = 0;
        {
            CpLocked l;
            if (g_cpScanReady && g_cpScanOutLoad == g_cpLoadGen) { live.swap(g_cpScanOut); why = g_cpScanOutWhy; sgen = g_cpScanOutGen; sworld = g_cpScanOutWorld; scale = g_cpScanScale; have = 1; }
            g_cpScanReady = 0;
            over = g_cpScanOver > 0 ? 1 : 0;
        }
        if (over && single) g_cpLoadScanned = 1;   /* not read again this world load */
        if (over) { if (!g_cpOverLogged) { g_cpOverLogged = 1; CpLog("this game holds more camps than a seed may carry (" + CpN((long long)campl::kCampSeedMax) + ") - no seed or match from it; it keeps its own camps"); } }
        if (have)
        {
            ++g_cpScans;
            if (why == campg::kScanSeed) CpSeed(live, scale, sgen, single, sworld);
            else if (why == campg::kScanMatch && !single && sgen == gen) CpMatch(live);
        }
    }
    CpRunRemovals();
    CpTakeGone(single, up);
    /* the pass's results */
    std::vector<CpDone> done;
    campl::Camp drawn; int haveDrawn = 0, lost = 0;
    {
        CpLocked l;
        done.swap(g_cpDoneQ);
        if (g_cpHaveDrawn) { drawn = g_cpDrawn; haveDrawn = 1; g_cpHaveDrawn = 0; }
        if (!single && g_cpWaiting && g_cpWaitSent && campg::CampWaitLinkGone(up, g_cpWaitGen, gen)) lost = 1;
    }
    for (size_t i = 0; i < done.size(); ++i)
    {
        const CpDone& d = done[i];
        if (d.outcome == campg::kBuiltOk)
        {
            ++g_cpBuilt;
            CpRegSet(d.rec.key, d.town); g_cpBuildTries.erase(d.rec.key);
            /* the list's spot against where the engine put the camp (the same on every game when the build takes the spot as given) */
            std::ostringstream o; o.setf(std::ios::fixed); o.precision(2);
            o << "camp '" << d.rec.key << "' BUILT by the engine's camp pass: list spot " << d.rec.x << "," << d.rec.y << "," << d.rec.z;
            if (d.posOk) { const float dx = d.x - d.rec.x, dy = d.y - d.rec.y, dz = d.z - d.rec.z; o << ", built at " << d.x << "," << d.y << "," << d.z << " (off by " << std::sqrt(dx * dx + dy * dy + dz * dz) << ")"; }
            else o << ", its position after the build did not read";
            CpLog(o.str());
            continue;
        }
        ++g_cpNotBuilt;
        g_cpBuiltHere.erase(d.rec.key);
        const int tries = ++g_cpBuildTries[d.rec.key];
        CpLog("camp '" + d.rec.key + "' NOT BUILT: " + (d.outcome == campg::kBuiltRegionNotReached ? "the engine's pass did not reach its region (it holds its limit here, or it is not in the engine's table)" : "the engine refused the build")
              + " - NOT_BUILT sent" + (campg::CampRetryBuild(tries) ? " (tried again at the next match)" : " (not tried again this world load)"));
        CpSendNotBuilt(done[i].rec.key, campl::kNbEngineRefused);
    }
    if (lost) CpEndWait("the link it went out on is gone");
    if (!haveDrawn) return;
    if (single) { ++g_cpAnsweredHere; CpAccepted(drawn, "drawn by this single-player game"); return; }
    if (!up) { ++g_cpNoLinkDrop; CpEndWait("no world-server link"); return; }
    campl::CampMsg m; m.op = campl::kOpPropose; m.rec = drawn;
    std::vector<char> b;
    if (!campl::CampEncode(&b, m) || !coop::StoreSendCamp(b)) { CpEndWait("the PROPOSE could not be sent"); return; }
    ++g_cpProposed;
    { CpLocked l; if (g_cpWaiting && g_cpWaitKey == drawn.key) { g_cpWaitSent = 1; g_cpWaitGen = gen; } }
    CpLog("proposed camp '" + drawn.key + "' region '" + drawn.region + "' (limit " + CpN((long long)drawn.regionCap) + ")");
}

std::string CampListKeyOfTown(void* town)
{
    if (!CpOnMain() || town == 0 || g_cpLockReady == 0) return std::string();
    CpDrainDestroyed();
    const std::map<void*, std::string>::const_iterator it = g_cpKeyOfTown.find(town);
    const int still = (it != g_cpKeyOfTown.end() && CpRegStill(town, it->second)) ? 1 : 0;
    if (it != g_cpKeyOfTown.end() && !still) ++g_cpHomeKeyStale;
    if (still) return it->second;
    /* no key here: while this game holds the whole list, a home on the engine's camp list is sent as "a camp, key unknown" - never
       left to the kind's sid (the kind's first camp); the camp list is read only while no camp pass runs (tried, never waited on) */
    int complete = 0; unsigned int state = 0; { CpLocked l; complete = g_cpListComplete; state = g_cpListState; }
    const int held = (g_cpReady != 0 && complete && state == (unsigned)campl::kListHere) ? 1 : 0;
    int listed = 0;
    if (held)
    {
        if (::TryEnterCriticalSection(&g_cpPassMx)) { listed = CpListedPod(town) == 1 ? 1 : 0; ::LeaveCriticalSection(&g_cpPassMx); }
        else ++g_cpHomeKeyBusy;
    }
    if (campg::CampKeyToSend(still, held, listed) != campg::kSendKeyUnknown) return std::string();
    ++g_cpHomeKeyUnknown;
    return campg::kCampKeyUnknown;
}
void* CampTownOfListKey(const std::string& key)
{
    if (!CpOnMain() || key.empty() || g_cpLockReady == 0) return 0;
    CpDrainDestroyed();
    const std::map<std::string, void*>::const_iterator it = g_cpTownOfKey.find(key);
    if (it == g_cpTownOfKey.end()) return 0;
    if (!CpRegStill(it->second, key)) { ++g_cpHomeKeyStale; return 0; }
    ++g_cpHomeKeyOk;
    return it->second;
}

std::string CampReportToken()
{
    long long q = 0, skip = 0, alone = 0, draw = 0, build = 0, vw = 0, vwn = 0, scanWait = 0, noRegion = 0, notTown = 0, goneOver = 0, notRunning = 0;
    int waiting = 0;
    if (g_cpLockReady) { CpLocked l; q = (long long)g_cpBuildQ.size(); skip = g_cpPassesSkipped; alone = g_cpPassesAlone; draw = g_cpPassesDraw; build = g_cpPassesBuild; waiting = g_cpWaiting;
                         vw = (long long)g_cpViewWait.size(); vwn = g_cpViewWaits; scanWait = g_cpScanWaits; noRegion = g_cpScanNoRegion; notTown = g_cpScanNotTown; goneOver = g_cpGoneOver; notRunning = g_cpScanNotRunning; }
    return "camps[on=" + CpN((long long)g_cpReady) + " list=" + CpN((long long)g_cpList.size()) + " passes skip/alone/draw/build " + CpN(skip) + "/" + CpN(alone) + "/" + CpN(draw) + "/" + CpN(build)
           + " proposed=" + CpN(g_cpProposed) + " answeredHere=" + CpN(g_cpAnsweredHere) + " accepted=" + CpN(g_cpAccepted) + " others=" + CpN(g_cpOthers) + " known=" + CpN(g_cpKnown)
           + " refused=" + CpN(g_cpRefused) + " waiting=" + CpN((long long)waiting) + " waitLost=" + CpN(g_cpWaitLost) + " queued=" + CpN(q) + " built=" + CpN(g_cpBuilt) + " notBuilt=" + CpN(g_cpNotBuilt)
           + " noKind=" + CpN(g_cpNoKind) + " drawUnreadable=" + CpN(g_cpDrawUnreadable) + " lists=" + CpN(g_cpLists) + "/" + CpN(g_cpListsIncomplete) + " removed=" + CpN(g_cpRemoved)
           + " scans=" + CpN(g_cpScans) + " scanWaits=" + CpN(scanWait) + " noRegion=" + CpN(noRegion) + " notTown=" + CpN(notTown) + " seeds=" + CpN(g_cpSeeds) + "/" + CpN(g_cpSeedRecs)
           + " seedFail=" + CpN(g_cpSeedFail) + " seeded=" + CpN(g_cpSeeded) + " removedHere=" + CpN(g_cpRemovedHere) + " removeChanged=" + CpN(g_cpRemoveChanged) + " removeFault=" + CpN(g_cpRemoveFault)
           + " matchQueued=" + CpN(g_cpMatchQueued) + " goneSent=" + CpN(g_cpGoneSent) + " goneNoKey=" + CpN(g_cpGoneNoKey) + " goneOver=" + CpN(goneOver)
           + " viewWait=" + CpN(vw) + "/" + CpN(vwn) + " viewUnread=" + CpN(g_cpViewUnread) + " scanNotRunning=" + CpN(notRunning) + " removeHeld=" + CpN(g_cpRemoveHeld) + "/" + CpN((long long)g_cpRemoveQ.size())
           + " retries=" + CpN(g_cpRetries) + " retryGaveUp=" + CpN(g_cpRetryGaveUp) + " homeKeys=" + CpN((long long)g_cpTownOfKey.size()) + " homeKeyOk=" + CpN(g_cpHomeKeyOk) + " homeKeyStale=" + CpN(g_cpHomeKeyStale)
           + " homeKeyUnknown=" + CpN(g_cpHomeKeyUnknown) + " homeKeyBusy=" + CpN(g_cpHomeKeyBusy) + " destroyDropped=" + CpN(g_cpDestroyDropped) + " removeWaitWorld=" + CpN(g_cpRemoveWaitWorld)
           + " debris[steps,forced,building,unread,offMain]=" + CpN(g_cpDebrisSteps) + "," + CpN(g_cpDebrisForced) + "," + CpN(g_cpDebrisBuilding) + "," + CpN(g_cpDebrisUnread) + "," + CpN((long long)g_cpDebrisOffMain)
           + coop::CtxCampHomeToken()
           + "]";
}

}   // namespace coop
