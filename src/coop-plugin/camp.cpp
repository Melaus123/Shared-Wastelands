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
#include "../common/camplaid.h"   /* T-1429 camp note design: the step's key and decision, the camp note, the camp pieces record */
#include "../common/squadorigin.h"   /* kLoadAgeUnknown: the world has not run yet */
#include <Windows.h>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coop { void CampHomeBuilt(const std::string& campKey, void* town); std::string CtxCampHomeToken(); }   /* spawn.cpp (spawn.h): the groups owed their home camp */
namespace coop { int ObjectPositionKey(void* obj, char* out, int cap, const char* suffix, int countIt, int podPos); }   /* items.cpp (items.h): the one position-key builder - a nest building's key (T-1429 design b) */
namespace coop { std::string GroundAroundSay(float x, float z, float r, int sx, int sy, int maxKeys, int* n); }
namespace coop { int BdBuildingSetTownPod(void* b, void* t, int* cycle); int BdBuildingTopLevelPod(void* b); std::string BdTownName(void* t); int CampPiecePlayerOwned(void* b); int BdBuildingTownHandPod(void* b, int* hand); int BdTownOfWordsPod(const int* hand, void** town); int BdTownReadPod(void* t, float* pos, int* hand, void** faction); }   /* build.cpp (namespace coop): the engine's set-town road for a standing building (town words, floor store, inside pieces, power), its top level, a town's name, a piece's own stored town words (+0x1D8), the town those words name, a town's own words - T-1429 F1648 re-link, F1659 B1 (never the engine's nearest-town name lookup) */
namespace coop { void* CampPieceMake(const std::string& sid, const float* pos, const float* rot, void* town, void* nest, int fact, int* why, int* inside); int CampPieceOrientPod(void* b, float* q); void CampPieceMakeCounts(long long* oldGuess, long long* oldGuessInside, long long* nestUnread); void* LayoutOuterBuilding(void* piece); }   /* build.cpp: a camp piece made as the camp maker makes one; a piece's orientation (T-1429 camp note design) */   /* items.cpp (items.h), T-1429 s15: the ground around a camp, for log lines (MAIN THREAD) */

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
unsigned long long kCpNeedsSaveRva = 0; static coop::AddrReg kCpNeedsSaveRva_reg("BuildingNeedsSaving", &kCpNeedsSaveRva);   /* T-1429 A1: Steam_1.0.65 0x54F690 - a building's 'needs saving' check (bld, std::string* save-file name) (decomp_54f690); optional:campkeep */
unsigned long long kCpMakeRva = 0;      static coop::AddrReg kCpMakeRva_reg("CampPieceMake", &kCpMakeRva);                 /* T-1429 A1: Steam_1.0.65 0x92F250 - the camp maker (kind, &centre, r8, r9, count, town, hand, list) (decomp_92f250); optional:campkeep */

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
typedef unsigned long long (*CpNeedsSaveFn)(void* bld, void* name);
CpNeedsSaveFn orig_cpNeedsSave = 0;
/* review B1: the 3rd / 4th arguments are FLOATS (the spread), passed in xmm2 / xmm3 - 1.0.65 0x14092F2CB movss [rbp-0x54],xmm3 / 0x14092F2D0 movaps xmm15,xmm2;
   1.0.68 0x14093019B / 0x1409301A0 the same; r8 / r9 are overwritten before any read */
typedef void (*CpMakeFn)(void* kind, float* centre, float spreadMin, float spreadMax, unsigned long long count, void* town, void* hand, void* list);
CpMakeFn orig_cpMake = 0;
/* T-1429 A1 (campgame.h CampPieceSaveAnswer; the step: design b, camplaid.h): the camp keep - on only with both of its hooks and the furniture step's */
volatile LONG g_cpKeepOn = 0;
int g_cpStepSkipping = 0;                   /* MAIN THREAD: inside a skipped furniture step (the camp maker's count is 0) */
volatile LONG64 g_cpSaveYes = 0, g_cpSaveDestroyed = 0, g_cpSaveEngineYes = 0;   /* any thread: camp pieces the keep saved / left out destroyed / the engine saved itself */
long long g_cpSaveYesMain = 0, g_cpSaveDestroyedMain = 0, g_cpSaveEngineYesMain = 0;   /* MAIN THREAD: the same for the area save under way (CampAreaSaveBegin / End) */
/* review SHOULD: why the engine refused each piece the keep saved (decomp_54f690:29-37): slot +0x2F0 not 8, the first +0x3D8 entry of type 0xB
   (no object) or the list empty, both, or unread */
volatile LONG64 g_cpWhySlot = 0, g_cpWhyEntry = 0, g_cpWhyBoth = 0, g_cpWhyUnread = 0;
long long g_cpWhySlotMain = 0, g_cpWhyEntryMain = 0, g_cpWhyBothMain = 0, g_cpWhyUnreadMain = 0;
int g_cpWhyLogBudget = 20;
long long g_cpStepsSkipped = 0, g_cpStepsRan = 0, g_cpStepsUnread = 0, g_cpMakesZeroed = 0, g_cpKeepLinesCut = 0;
int g_cpKeepLogBudget = 150;
/* T-1429 camp note design (camplaid.h): each area's camp pieces record (camplaid.<sx>.<sy>) - the accepted record, the write in flight,
   the refusal's generation floor, a 'not the holder' refusal waiting for this game to hold the area; the entries this game laid and has
   not yet seen stored (id -> key -> pieces); the areas whose record is applied once they are loaded here (an arrival, a note read). MAIN THREAD. */
struct ClOut { long long stamp; camplaid::CampLaidRec rec; ClOut() : stamp(0) {} };   /* F1627 item 4: one write out, its stamp and record */
struct ClKeep { int haveAcc; camplaid::CampLaidRec acc; int inflight; long long stamp; camplaid::CampLaidRec sent; unsigned int floorGen; int waitHolder; std::map<unsigned int, ClOut> out; int haveSent; unsigned int hiSent;
                ClKeep() : haveAcc(0), inflight(0), stamp(0), floorGen(0), waitHolder(0), haveSent(0), hiSent(0) {} };   /* F1627 item 4: out = every write still out, by its generation (inflight = any); hiSent = the highest sent (never reused) */   /* F1617 item 4: waitHolder ends at the world server's HELD (no timer) */
std::map<std::string, ClKeep> g_clMark;
std::map<std::string, std::map<std::string, std::vector<camplaid::CampPiece> > > g_clToWrite;
std::set<std::string> g_clApplyDue;   /* T-1429 F1671: the SQUARES due an apply, each named by its camplaid id (a read of the square, a record with pieces in it arriving) */
std::map<int, std::set<std::string> > g_clSqIdx;   /* T-1429 F1671: square (x * 64 + y) -> the camp pieces records with pieces in it (camplaid.h CampLaidIndexAdd; built as records arrive) */
long long g_csApplied = 0, g_csMade = 0, g_csMadeAway = 0, g_csDup = 0, g_csFail = 0, g_csSkipNote = 0, g_csWaitOwn = 0, g_csNoted = 0;   /* T-1429 F1671: the per-square apply (campSquare token) */
/* T-1429 review fold F1612 item 7: records with an entry whose town is not here yet - applied again at the next furniture step or area read
   (the town's own setup); the fold's counters (the campFold token). MAIN THREAD unless marked. */
std::set<std::string> g_clWaitTown, g_clWaitTownSaid;
/* T-1429 F1753 design (a): each camp piece's inside fact - read on the camp's game (CpPieceReadPod), used by the apply. g_clWaitNest: squares
   with an entry whose piece is marked inside its nest building and the nest is not here - tried again at an event only (a camp step here,
   an area file read here, a square settled here), or for a nest building that did not read (review fold LOW 1). The one 'nest gone' rule
   (camplaid.h CampNestGone, re-check fold MEDIUM): g_ciStepSeq numbers this game's keyed camp steps (written on the main thread, read at an
   area-file read on any thread); g_ciTownStepSeq: a key's town part -> the number of its last camp step here. MAIN THREAD unless marked. */
std::set<std::string> g_clWaitNest, g_ciGoneSaid;
volatile LONG g_ciStepSeq = 0;   /* any thread reads */
std::map<std::string, long> g_ciTownStepSeq;
long long g_ciUnreadRetry = 0;   /* re-check fold LOW: entries left out of the note because their nest building did not read (each try) */
long long g_ciReadOut = 0, g_ciReadNest = 0, g_ciReadUnknown = 0, g_ciMismatch = 0, g_ciWaitNest = 0, g_ciNestGone = 0, g_ciMismatchSaid = 0;
long long g_cfLateLayer = 0, g_cfSelfLayer = 0, g_cfNoAnswerOwed = 0, g_cfWaitTown = 0, g_cfOutOfBound = 0, g_cfUnreadZeroed = 0, g_cfHeld = 0, g_cfInside = 0, g_cfNestMissing = 0, g_cfAskCleared = 0;
volatile LONG g_cfOffMainZeroed = 0;   /* any thread */
DWORD g_cpOffTls = TLS_OUT_OF_INDEXES;   /* F1612 item 5: set around a furniture step off the main thread (TlsAlloc, not __declspec(thread): this DLL loads late) */
/* the camp note of each area (zone.<sx>.<sy> -> keys), the world server's layer answer waiting for the area's load and the layer of the
   load in force; the areas read since the last tick. Under g_cnLock (a note read can come on the loading thread). */
CRITICAL_SECTION g_cnLock;
volatile LONG g_cnLockReady = 0;
struct CnLocked { CnLocked() { ::EnterCriticalSection(&g_cnLock); } ~CnLocked() { ::LeaveCriticalSection(&g_cnLock); } };
std::map<std::string, std::set<std::string> > g_cnNote;
std::map<std::string, int> g_cnLayerWaiting, g_cnLayerAtLoad;
std::set<std::string> g_cnReadSince;
std::map<std::string, long> g_cnReadGen;   /* T-1429 F1617 item 1: each area's file reads so far (a kept step belongs to the read before it) */
/* T-1429 re-check fold F1617 item 1 (THE BLOCKER, design (b) - camplaid.h CampKeptDecide): AN OWED STEP KEPT - the step's exact arguments
   (town, template record, point as handed to the engine, building), its key, its area and the area read it belongs to - run later through
   the engine's own step at the event that makes this game the area's camp layer (the world server's HELD, a late answer naming this game),
   dropped when its key arrives (the note, the record), its town is destroyed or the area is read again. MAIN THREAD. */
struct CkStep { void* town; void* rec; float pt[4]; unsigned long long hand; std::string key, zone, id; long readGen; long useq; };   /* F1627 item 3: useq = the area's unload count when kept */
std::map<std::string, std::vector<CkStep> > g_ckKept;   /* zone -> kept steps */
/* F1617 item 3: the pieces an unbuilt step laid - the keep leaves them out of the save (MAIN THREAD). F1622 item 3: an address can be
   reused by a later piece, so each entry carries its area, record and position: dropped at its area's unload (the objects go with it)
   and, at the needs-saving check, when the object there is not the same record within 1 of the same spot */
struct CpUnsaved { std::string zone; void* gd; float p[3]; long useq; CpUnsaved() : gd(0), useq(0) { p[0] = p[1] = p[2] = 0.0f; } };   /* F1627 item 3: useq as CkStep's */
std::map<void*, CpUnsaved> g_cpUnsaved;
long long g_ckDropUnload = 0, g_cfUnsavedStale = 0, g_cfUnsavedUnload = 0, g_cfReadNowLayer = 0, g_cfWritePiped = 0;   /* F1622 */
long long g_ckKeptN = 0, g_ckRun = 0, g_ckDropKey = 0, g_ckDropGone = 0, g_ckDropRead = 0, g_ckHeldIn = 0, g_cfUnbuilt = 0, g_cfUnbuiltPieces = 0, g_cfDup = 0, g_cfWaitEnded = 0;
void* g_cpDupBuf[4096];   /* F1617 item 6: the apply's duplicate walk (MAIN THREAD) */
/* T-1429 re-check fold F1627 item 3: EVERY UNLOAD OF AN AREA is taken from the engine's own unload hook (items.cpp detour_zoneDeactivate)
   on every road - main thread or not, inside the area update or not: the area's unload count rises (any thread, interlocked, after the
   unload's own area save; one counter per area, so nothing is capped or dropped) and the main thread takes it (CuDrain): what belongs to
   the load before that unload ends. g_cnAskUseq / g_cnReadUseq (under g_cnLock): the area's unload count at this game's last ask / file
   read of it - equal to the count now = this load's. */
volatile LONG g_cuSeq[4096];
LONG g_cuDone[4096];   /* MAIN THREAD */
volatile LONG g_cuUnnamed = 0;   /* an unload whose area did not read (any thread) */
LONG g_cuUnnamedSaid = 0;
long long g_cuApplied = 0;
std::map<std::string, long> g_cnAskUseq, g_cnReadUseq;
std::map<std::string, long> g_cnReadStepSeq;   /* T-1429 F1753 re-check fold (MEDIUM): g_ciStepSeq at the area's last file read (under g_cnLock) */
std::map<std::string, unsigned> g_cnReadMapSeq;   /* F1630 item 1: the area map number at the area's last file read */
std::map<std::string, long> g_cnStartUseq;   /* F1634: the unload count at the area's last load start */
std::map<std::string, unsigned> g_cnStartMapSeq;   /* F1634: the area map number at the area's last load start */
volatile LONG g_cuUnloadMapSeq[4096];   /* F1634: the area map number at the area's last unload (ANY THREAD, the unload hook) */
long long g_cfLoadStarts = 0, g_cfMarkStart = 0, g_cfMarkUnload = 0, g_cfHeldNoRead = 0;   /* F1634 */
long long g_cfMarkNever = 0;   /* F1638 SHOULD: steps whose mark was the unload fallback with no unload recorded (strict: no map 'after' it) */
long long g_cfMapBeforeRead = 0;   /* F1630 item 1: a holder's no-ask step that waited - its map came before the read */
long long g_cfStepHoldKept = 0, g_cfStepSelfLay = 0, g_cfTickRun = 0, g_cfNotHolderKept = 0, g_cfOutDropped = 0;   /* F1627 */
int CuKey(int x, int y) { return (x >= 0 && x < 64 && y >= 0 && y < 64) ? x * 64 + y : -1; }
long CuSeqOf(int x, int y) { const int k = CuKey(x, y); return k < 0 ? 0 : (long)g_cuSeq[k]; }
int CpZoneXY(const std::string& z, int* x, int* y) { return (z.size() > 5 && z.compare(0, 5, "zone.") == 0 && std::sscanf(z.c_str() + 5, "%d.%d", x, y) == 2 && CuKey(*x, *y) >= 0) ? 1 : 0; }
/* F1627 items 2 / 5: THE LIVE HOLDING FACT - 1 = this game's fresh area map names it the area's holder, 0 = another game, -1 = not known
   (no fresh map, the link down, no slot). MAIN THREAD. */
int CpHoldsNow(int x, int y, unsigned* mapSeq = 0)   /* F1630 item 1: *mapSeq = the arrival number of the map the answer came from */
{
    if (mapSeq != 0) *mapSeq = 0;
    if (!coop::StoreCampLinkUp()) return -1;
    coop::Sector s; s.x = x; s.y = y; int fresh = 0;
    const int h = coop::AreaHolderSlotSeqTS(s, &fresh, mapSeq);
    const int me = coop::StoreMySlot();
    if (fresh == 0 || me < 0) return -1;
    return h == me ? 1 : 0;
}
/* 1 = this load asked the world server about the area (the ask came after the area's last unload). Under g_cnLock. */
int CnAskedThisLoad(const std::string& zone, int x, int y) { const std::map<std::string, long>::const_iterator a = g_cnAskUseq.find(zone); return (a != g_cnAskUseq.end() && a->second == CuSeqOf(x, y)) ? 1 : 0; }
/* F1630 item 1 / F1634: 1 = the map numbered mapSeq arrived after this load's mark - its last read of the area's file, else its start, else
   the area's last unload (camplaid.h CampLoadMark / CampMapAfterRead); *which = the mark used (camplaid kMark*). Under g_cnLock. */
int CnMapAfterLoad(const std::string& zone, int x, int y, unsigned mapSeq, int* which, int* never = 0)
{
    const long cur = CuSeqOf(x, y);
    const std::map<std::string, long>::const_iterator r = g_cnReadUseq.find(zone);
    const std::map<std::string, unsigned>::const_iterator m = g_cnReadMapSeq.find(zone);
    const int readThis = (r != g_cnReadUseq.end() && r->second == cur && m != g_cnReadMapSeq.end()) ? 1 : 0;
    const std::map<std::string, long>::const_iterator su = g_cnStartUseq.find(zone);
    const std::map<std::string, unsigned>::const_iterator sm = g_cnStartMapSeq.find(zone);
    const int startThis = (su != g_cnStartUseq.end() && su->second == cur && sm != g_cnStartMapSeq.end()) ? 1 : 0;
    const int k = CuKey(x, y);
    unsigned mark = 0;
    const int w = camplaid::CampLoadMark(readThis, readThis ? m->second : 0u, startThis, startThis ? sm->second : 0u, k >= 0 ? (unsigned)g_cuUnloadMapSeq[k] : 0u, &mark);
    if (which != 0) *which = w;
    const int unloadedEver = (k >= 0 && g_cuSeq[k] != 0) ? 1 : 0;   /* F1638 SHOULD: no unload recorded - the fallback mark is strict */
    if (never != 0) *never = (w == camplaid::kMarkUnload && unloadedEver == 0) ? 1 : 0;
    return camplaid::CampMapAfterMark(w, unloadedEver, mark, mapSeq);
}
/* F1634: 1 = the load layer on record for the area belongs to THIS load (its read or its start since the area's last unload). Under g_cnLock. */
int CnEntryThisLoad(const std::string& zone, int x, int y)
{
    const long cur = CuSeqOf(x, y);
    const std::map<std::string, long>::const_iterator r = g_cnReadUseq.find(zone);
    const std::map<std::string, long>::const_iterator su = g_cnStartUseq.find(zone);
    return ((r != g_cnReadUseq.end() && r->second == cur) || (su != g_cnStartUseq.end() && su->second == cur)) ? 1 : 0;
}
long long g_cnReads = 0, g_cnReadsFound = 0, g_cnReadsOff = 0, g_cnReadBad = 0, g_cnSaved = 0, g_cnSaveFail = 0, g_cnOver = 0, g_cnLayerYou = 0, g_cnLayerOther = 0, g_cnLayerNone = 0;
int g_cnLineBudget = 200;
long long g_clNoteHad = 0, g_clLaid = 0, g_clOwed = 0, g_clNoKey = 0, g_clMarkWrites = 0, g_clMarkTaken = 0, g_clMarkRefused = 0, g_clMarkArrived = 0, g_clMarkBad = 0, g_clMarkNotSent = 0, g_clToWriteDropped = 0, g_clCapped = 0;
long long g_clApplyEntries = 0, g_clApplyMade = 0, g_clApplyFail = 0, g_clApplySkipped = 0, g_clPieceUnread = 0;
const float kCpPieceNearLog = 250.0f;   /* the step line's 'now within' count only - no decision reads it (design b) */
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
struct CpDone { campl::Camp rec; int outcome; void* town; int posOk; float x, y, z; std::string groundBefore; };   /* town / position: what the engine built (BUILT only); groundBefore: T-1429 s15, the ground around the spot as the pass began (empty: not read) */
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
/* T-1429 s15 (F1356 / F1369): one line per furniture step and the ground around each camp build, each with its own cap so the [CAMP]
   lines above keep theirs (permanent lines; the counters go on past the caps: campLines[stepCut,groundCut] on the REPORT line) */
int g_cpStepLogBudget = 150, g_cpGroundLogBudget = 120;
long long g_cpStepLinesCut = 0, g_cpGroundLinesCut = 0;
const float kCpGroundR = 600.0f;   /* the ground read around a camp's list spot and a furniture step's point (F1369: the eggs lay within ~40 of their nest) */
struct CpGroundLater { std::string key; float x, z; DWORD due; };   /* GetTickCount: this toolset has no GetTickCount64; compared by difference, so the wrap is safe */
std::vector<CpGroundLater> g_cpGroundLater;   /* camps BUILT here, their ground read again 30 s after the build (at most 32 waiting) */
void* g_cpBldBuf[4096];   /* the furniture step line's area walk (MAIN THREAD) */

std::string CpN(long long v) { std::ostringstream o; o << v; return o.str(); }
void CpLog(const std::string& s) { if (g_cpLogBudget <= 0) return; --g_cpLogBudget; DebugLog("[CAMP] " + s + (g_cpLogBudget == 0 ? " (log cap reached - counters only from here)" : "")); }
void CpGroundSay(const std::string& s) { if (g_cpGroundLogBudget <= 0) { ++g_cpGroundLinesCut; return; } --g_cpGroundLogBudget; DebugLog("[CAMP] " + s + (g_cpGroundLogBudget == 0 ? " (camp ground line cap reached - counted in campLines[groundCut] from here)" : "")); }
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
/* T-1429 F1688 D1: the every-town array beside its count (+0x58 / +0x60, decomp_92a4f0 80-163), the wilderness town and a town's state (dead = 8) as build.cpp BdWorldTownsPod */
const size_t kCpTlAllArr = 0x60, kCpTlWild = 0x180F8, kCpTownState = 0xD8; const int kCpTownDead = 8;
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
/* T-1429 F1688 D1: every live town of the engine's every-town list (not the wilderness town, not a dead one) into out, at most cap; the
   count, -1 = the list does not read, -2 = more live towns than cap. MAIN THREAD */
int CpAllTownsPod(void** out, int cap)
{
    void* tl = CpTownListPod();
    if (tl == 0) return -1;
    __try
    {
        const char* c = (const char*)tl;
        void* const wild = *(void* const*)(c + kCpTlWild);
        const unsigned n = *(const unsigned*)(c + kCpTlAllN);
        void* const* arr = *(void* const* const*)(c + kCpTlAllArr);
        if (n > 0 && arr == 0) return -1;
        int k = 0;
        for (unsigned i = 0; i < n; ++i)
        {
            void* t = arr[i];
            if (t == 0 || t == wild || *(const int*)((const char*)t + kCpTownState) == kCpTownDead) continue;
            if (k >= cap) return -2;
            out[k++] = t;
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
/* T-1429 A1: a building's camp-piece byte (+0x241, decomp_92f250:294) and destroyed byte (+0x1A1, 551890:150-152); 0 = a fault */
int CpPiecePod(void* b, int* camp, int* destroyed)
{
    if (b == 0) return 0;
    __try { *camp = *((const unsigned char*)b + 0x241) != 0 ? 1 : 0; *destroyed = *((const unsigned char*)b + 0x1A1) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* review SHOULD: 0x54F690's two conditions for a camp piece (decomp_54f690:29-37) - slot +0x2F0's answer (8 passes) and the type of the
   first +0x3D8 entry's hand (+0x3E0 the count; 0 = the engine's fixed empty entry; 0xB = no object, refuses). 0 = a fault. */
int CpWhyPod(void* b, int* slot, int* entryType, int* listEmpty)
{
    if (b == 0) return 0;
    __try
    {
        typedef int (*SlotFn)(void*);
        *slot = ((SlotFn)((*(void***)b)[0x2F0 / 8]))(b);
        const unsigned long long n = *(const unsigned long long*)((const char*)b + 0x3E0);
        *listEmpty = n == 0 ? 1 : 0; *entryType = -1;
        if (n != 0) { const char* first = **(const char* const* const*)((const char*)b + 0x3D8); *entryType = *(const int*)(first + 0x18 + 8); }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the save-file name handed to 'needs saving' (an MSVC std::string: its size at +0x10, decomp_54f690:28 / decomp_36dca0:133); 0 = a fault */
int CpNameEmptyPod(const void* name, int* empty)
{
    if (name == 0) return 0;
    __try { *empty = *(const size_t*)((const char*)name + 0x10) == 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* an area save's sector: ZoneMapContent+0xD0 -> ZoneMap, its +0x18 / +0x1C (decomp_36dca0:135-141); 0 = a fault */
int CpContentSectorPod(void* content, int* sx, int* sy)
{
    if (content == 0) return 0;
    __try { const char* zm = *(const char* const*)((const char*)content + 0xD0); if (zm == 0) return 0; *sx = *(const int*)(zm + 0x18); *sy = *(const int*)(zm + 0x1C); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-1429 A1: the camp pieces (not destroyed) standing within kCpPieceNearLog of a point (the step line only), over every loaded area (a camp can straddle
   two); fills g_cpBldBuf. Returns the count, -1 = the walk was cut or did not read. `furthest` = the furthest counted piece; `pieces` =
   every camp piece within kCpPieceWide (the 'ran' line's before / after difference, so a new piece beyond the near distance is seen). MAIN THREAD. */
const float kCpPieceWide = 2000.0f;
int CpPiecesNear(float px, float pz, float* furthest, std::set<void*>* pieces)
{
    int trunc = 0;
    const int n = coop::LoadedBuildings(g_cpBldBuf, 4096, 0, &trunc);
    if (n < 0 || trunc != 0) return -1;
    int c = 0; float far2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        int camp = 0, dead = 0; float bp[3] = { 0, 0, 0 };
        if (!CpPiecePod(g_cpBldBuf[i], &camp, &dead) || !camp || dead || !CpPosPod(g_cpBldBuf[i], bp)) continue;
        if (pieces != 0 && campg::CampPieceNear(px, pz, bp[0], bp[2], kCpPieceWide)) pieces->insert(g_cpBldBuf[i]);
        if (!campg::CampPieceNear(px, pz, bp[0], bp[2], kCpPieceNearLog)) continue;
        ++c;
        const float d2 = (bp[0] - px) * (bp[0] - px) + (bp[2] - pz) * (bp[2] - pz);
        if (d2 > far2) far2 = d2;
    }
    if (furthest != 0) *furthest = far2 > 0.0f ? sqrtf(far2) : 0.0f;
    return c;
}
void CpKeepSay(const std::string& s) { if (g_cpKeepLogBudget <= 0) { ++g_cpKeepLinesCut; return; } --g_cpKeepLogBudget; DebugLog("[CAMP] " + s + (g_cpKeepLogBudget == 0 ? " (camp keep line cap reached - counted in campKeep[linesCut] from here)" : "")); }
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
    if (over) { g_cpDestroyDropped += (long long)g_cpTownOfKey.size(); g_cpTownOfKey.clear(); g_cpKeyOfTown.clear(); }
    else for (size_t i = 0; i < d.size(); ++i) if (g_cpKeyOfTown.find(d[i]) != g_cpKeyOfTown.end()) { CpRegDropTown(d[i]); ++g_cpDestroyDropped; }
    /* F1617 item 1: a kept step whose town the engine destroyed ends (all of them when the queue overflowed - which towns went is not known) */
    const std::set<void*> gone(d.begin(), d.end());
    for (std::map<std::string, std::vector<CkStep> >::iterator z = g_ckKept.begin(); z != g_ckKept.end(); )
    {
        for (size_t i = 0; i < z->second.size(); )
        {
            if (!over && gone.count(z->second[i].town) == 0) { ++i; continue; }
            ++g_ckDropGone;
            CpKeepSay("kept step key=" + z->second[i].key + " area " + z->first + " DROPPED - its town was destroyed" + (over ? std::string(" (the destroy queue overflowed)") : std::string()) + " (dropGone=" + CpN(g_ckDropGone) + ")");
            z->second.erase(z->second.begin() + i);
        }
        if (z->second.empty()) g_ckKept.erase(z++); else ++z;
    }
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

/* ---- T-1429 design b (camplaid.h): the step's key, the spawn queue, the mark ---- */
std::string CpZoneId(int x, int y) { return "zone." + CpN((long long)x) + "." + CpN((long long)y); }
/* one nest entry's key: <town>|<template id>|<building position key or centre> (camplaid.h CampLaidKey); "" = a part did not read.
   town: the list camp's key when the step's town is this game's camp of a list key, else the town's position key (ObjectPositionKey -
   record id, area, position in tenths: two towns of one type in one area square differ; F1605 / F1608 Q5) */
std::string CpStepKey(void* town, void* rec, unsigned long long hand, const std::string& listKey)
{
    std::string tp = listKey;
    if (tp.empty())
    {
        char tk[200]; tk[0] = 0;
        if (coop::ObjectPositionKey(town, tk, (int)sizeof tk, "", 3, 1) == 0) return std::string();
        tp = tk;
    }
    char rsid[campl::kCampStrMax + 1];
    if (!CpSidPod(rec, rsid, (int)sizeof rsid)) return std::string();
    std::string bk;
    if (hand != 0)
    {
        char k[200]; k[0] = 0;
        if (coop::ObjectPositionKey((void*)(uintptr_t)hand, k, (int)sizeof k, "", 3, 1) == 0) return std::string();
        bk = k;
    }
    return camplaid::CampLaidKey(tp, rsid, bk);
}
/* T-1429 F1753 re-check fold (MEDIUM): a keyed camp step of town part tp ran here - it takes the next step number (g_ciStepSeq) and the
   town remembers it. MAIN THREAD */
void CiTownStep(const std::string& tp)
{
    const long n = (long)::InterlockedIncrement(&g_ciStepSeq);
    if (!tp.empty()) g_ciTownStepSeq[tp] = n;
}
/* the one 'nest gone' rule's facts for nest building key bk of town part tp (camplaid.h CampNestGone): the nest's square (from its key)
   loaded here now, its file read belongs to this load, and the town's last step number against the step count stamped at that read.
   1 = gone. MAIN THREAD */
int CiNestGoneNow(const std::string& bk, const std::string& tp)
{
    int x = -1, y = -1;
    if (!camplaid::CampNestSquareOfKey(bk, &x, &y)) return 0;
    const int loaded = coop::ZoneLoadedSectorTri(x, y) == 1 ? 1 : 0;
    const std::string zone = CpZoneId(x, y);
    int readThis = 0; long readStep = 0;
    {
        CnLocked l;
        const std::map<std::string, long>::const_iterator r = g_cnReadUseq.find(zone);
        readThis = (r != g_cnReadUseq.end() && r->second == CuSeqOf(x, y)) ? 1 : 0;
        const std::map<std::string, long>::const_iterator rs = g_cnReadStepSeq.find(zone);
        if (rs != g_cnReadStepSeq.end()) readStep = rs->second;
    }
    const std::map<std::string, long>::const_iterator t = g_ciTownStepSeq.find(tp);
    return camplaid::CampNestGone(0, loaded, readThis, t == g_ciTownStepSeq.end() ? 0L : t->second, readStep);
}
/* F1753: the REPORT token " campF1753[readOut,readNest,readUnknown,mismatch,waitNest,nestGone,oldGuess,oldGuessInside,nestUnread,unreadRetry]=..",
   built in parts (VC2010's compiler fails on one very long join, F1746) */
std::string CiCountsToken()
{
    long long og = 0, ogi = 0, nu = 0;
    coop::CampPieceMakeCounts(&og, &ogi, &nu);
    std::string t = " campF1753[readOut,readNest,readUnknown,mismatch,waitNest,nestGone,oldGuess,oldGuessInside,nestUnread,unreadRetry]=";
    t += CpN(g_ciReadOut) + "," + CpN(g_ciReadNest) + "," + CpN(g_ciReadUnknown) + "," + CpN(g_ciMismatch);
    t += "," + CpN(g_ciWaitNest) + "," + CpN(g_ciNestGone) + "," + CpN(og) + "," + CpN(ogi) + "," + CpN(nu) + "," + CpN(g_ciUnreadRetry);
    return t;
}
/* a camp piece as the record carries it: its record id, position and orientation, and (F1753 design a) its inside fact read from the piece
   itself - top level = outdoors (0); inside a building that IS the step's nest building `nest` (items.cpp LayoutOuterBuilding: its
   layout's owner or its own inside hand at Building+0x80) = inside (1); inside any other building, or a parent that does not read = not
   known (2), a mismatch logged (first 8) and counted - never guessed (camplaid.h CampPieceInsideFact). 0 = a part did not read. countIt:
   1 for a laid step's pieces (the record's) - the campF1753 read counters and mismatch lines; 0 for an unbuilt step's (review fold NIT) */
int CpPieceReadPod(void* b, camplaid::CampPiece* pc, void* nest, int countIt)
{
    void* gd = 0; char sid[camplaid::kClSidMax + 1]; float p[3] = { 0, 0, 0 }; float q[4] = { 1, 0, 0, 0 };
    if (!CpKindPod(b, &gd) || !CpSidPod(gd, sid, (int)sizeof sid) || !CpPosPod(b, p) || !coop::CampPieceOrientPod(b, q)) return 0;
    pc->sid = sid; pc->x = p[0]; pc->y = p[1]; pc->z = p[2]; pc->qw = q[0]; pc->qx = q[1]; pc->qy = q[2]; pc->qz = q[3];
    const int top = coop::BdBuildingTopLevelPod(b);
    void* parent = top == 0 ? coop::LayoutOuterBuilding(b) : 0;
    int mismatch = 0;
    pc->inside = camplaid::CampPieceInsideFact(top, (parent != 0 && parent == nest) ? 1 : 0, &mismatch);
    if (countIt == 0) return camplaid::CampPieceValid(*pc) ? 1 : 0;   /* an unbuilt step's piece: not in the record - not counted */
    if (pc->inside == camplaid::kClInsideOut) ++g_ciReadOut; else if (pc->inside == camplaid::kClInsideNest) ++g_ciReadNest; else ++g_ciReadUnknown;
    if (mismatch != 0 || top < 0)
    {
        ++g_ciMismatch;
        if (++g_ciMismatchSaid <= 8)
        {
            char pk[200]; pk[0] = 0; char nk[200]; nk[0] = 0;
            if (parent != 0) coop::ObjectPositionKey(parent, pk, (int)sizeof pk, "", 3, 1);
            if (nest != 0) coop::ObjectPositionKey(nest, nk, (int)sizeof nk, "", 3, 1);
            CpKeepSay(std::string("camp piece ") + sid + " at " + CpN((long long)p[0]) + "," + CpN((long long)p[2]) + ": its parent "
                      + (top < 0 ? std::string("did not read") : parent == 0 ? std::string("is a building no road names") : "is '" + std::string(pk[0] ? pk : "(no key)") + "'")
                      + ", the step's nest building is " + (nest == 0 ? std::string("none") : "'" + std::string(nk[0] ? nk : "(no key)") + "'")
                      + " - recorded inside=2 (not known), not guessed (F1753; mismatch=" + CpN(g_ciMismatch) + ", first 8 said)");
        }
    }
    return camplaid::CampPieceValid(*pc) ? 1 : 0;
}
const char* CnLayerName(int l) { return l == camplaid::kLayerYou ? "this game" : l == camplaid::kLayerOther ? "another game" : "not said"; }
/* T-1429 F1645 (2): a layer answer's text - 'other' is also the world server's answer when NO game holds the area; this game's fresh area
   map tells the two apart (camplaid.h CampAnswerLayerText). Log text only: the decision is the answer's layer, unchanged. MAIN THREAD. */
std::string CnAnswerText(int x, int y, int layer)
{
    int fresh = 0, h = -1;
    if (layer == camplaid::kLayerOther) { coop::Sector s; s.x = x; s.y = y; h = coop::AreaHolderSlotSeqTS(s, &fresh, 0); }
    return camplaid::CampAnswerLayerText(layer, fresh, h, coop::StoreMySlot());
}
/* the area's note gains a key (a step laid it here, or its record entry was applied here) */
/* F1617 item 1: kept steps of `zone` whose key is `key` (or every key, key empty) end - the key arrived here */
void CkDropKey(const std::string& zone, const std::string& key, const char* why)
{
    std::map<std::string, std::vector<CkStep> >::iterator z = g_ckKept.find(zone);
    if (z == g_ckKept.end()) return;
    for (size_t i = 0; i < z->second.size(); )
    {
        if (!key.empty() && z->second[i].key != key) { ++i; continue; }
        ++g_ckDropKey;
        CpKeepSay("kept step key=" + z->second[i].key + " area " + zone + " DROPPED - " + why + " (F1617 item 1; dropKey=" + CpN(g_ckDropKey) + ")");
        z->second.erase(z->second.begin() + i);
    }
    if (z->second.empty()) g_ckKept.erase(z);
}
void CnNoteAdd(const std::string& zone, const std::string& key)
{
    if (g_cnLockReady == 0) return;
    {
        CnLocked l;
        std::set<std::string>& n = g_cnNote[zone];
        if ((int)n.size() >= camplaid::kCnNoteMax && n.count(key) == 0) { ++g_cnOver; return; }
        n.insert(key);
    }
    CkDropKey(zone, key, "its key is in the area's note now");
}
/* T-1429 F1688 D1: the town a position key names, over the engine's EVERY-TOWN list (TownList +0x58 / +0x60): only towns whose record id fits
   the key's have their own position key built (as CpStepKey builds it), and only an exact match counts; two matches or a read fault = none.
   A town found here that is not on the camp array (an ordinary town - the old lookup's blind spot, T2706) is said once per key. MAIN THREAD */
void* g_cpAllTowns[16384];   /* main thread only */
long long g_caFound = 0, g_caNotCamp = 0, g_caTwins = 0, g_caNone = 0, g_caFault = 0, g_caOverCap = 0, g_caLinesCut = 0;
int g_caLogBudget = 40;
std::set<std::string> g_caSaid;
void CaSay(const std::string& key, const std::string& s)
{
    if (g_caSaid.count(key) != 0) return;
    if (g_caLogBudget <= 0) { ++g_caLinesCut; return; }
    --g_caLogBudget; g_caSaid.insert(key);
    DebugLog("[CAMP] " + s + (g_caLogBudget == 0 ? " (every-town line cap reached - counted in campAllTown[linesCut] from here)" : ""));
}
void* CpTownOfPosKeyAll(const std::string& tp)
{
    camplaid::CampAllTownFind f;
    if (camplaid::CampAllTownFindStart(tp, &f) == 0) return 0;
    const int n = CpAllTownsPod(g_cpAllTowns, (int)(sizeof g_cpAllTowns / sizeof g_cpAllTowns[0]));
    if (n < 0)
    {
        if (n == -2) ++g_caOverCap; else ++g_caFault;
        CaSay("!" + tp, "every-town lookup key=" + tp + " found NO town: the every-town list " + (n == -2 ? std::string("holds more live towns than the walk's room") : std::string("did not read")) + " (F1688 D1)");
        return 0;
    }
    std::string townSid;
    for (int i = 0; i < n; ++i)
    {
        void* gd = 0; char sid[camplaid::kClSidMax + 1];
        if (!CpKindPod(g_cpAllTowns[i], &gd) || !CpSidPod(gd, sid, (int)sizeof sid) || !camplaid::CampAllTownIdFits(f, sid)) continue;
        char k[200]; k[0] = 0;
        if (coop::ObjectPositionKey(g_cpAllTowns[i], k, (int)sizeof k, "", 3, 1) == 0) k[0] = 0;
        camplaid::CampAllTownSee(&f, i, k, tp);
        if (townSid.empty()) townSid = sid;
    }
    const int r = camplaid::CampAllTownResult(f);
    if (r == camplaid::kAllTownTwins)
    {
        ++g_caTwins;
        CaSay("2" + tp, "every-town lookup key=" + tp + " REFUSED: " + CpN((long long)f.matches) + " live towns of record " + townSid + " give this exact key - none is taken (F1688 D1)");
        return 0;
    }
    if (r < 0) { ++g_caNone; return 0; }
    void* const t = g_cpAllTowns[r];
    ++g_caFound;
    if (CpListedPod(t) == 0)
    {
        ++g_caNotCamp;
        CaSay("1" + tp, "every-town lookup key=" + tp + " FOUND town of record " + townSid + " in the every-town list; it is not on the camp array (the road the old lookup never saw; F1688 D1; notOnCampList=" + CpN(g_caNotCamp) + ")");
    }
    return t;
}
/* the town a key's town part names here: a list camp by its CampKey, any other by its position key over the engine's every-town list; 0 = none.
   T-1429 F1645 (3): the form is told by its real shape (camplaid.h CampTownKeyForm), not by an '@' (both forms hold one) */
void* CpTownOfKeyPart(const std::string& tp)
{
    const int form = camplaid::CampTownKeyForm(tp);   /* F1645 (3): both forms hold '@' - told apart by their real shape */
    if (form == camplaid::kTownKeyList) return coop::CampTownOfListKey(tp);
    if (form != camplaid::kTownKeyPosition) return 0;
    return CpTownOfPosKeyAll(tp);   /* F1688 D1: over the engine's every-town list (an ordinary town is on no camp array) */
}
/* T-1429 review fold F1612 item 6: the nest building a key's building part names (its position key), over the buildings loaded in the
   record's square; 0 = none (the town's own entry, or not found here). MAIN THREAD */
void* CpNestOfKeyPart(const std::string& bk, int sx, int sy)
{
    if (bk.empty()) return 0;
    for (int d = 0; d < 10; ++d)
    {   /* F1617 NIT: the record's square first, then the squares beside it (a nest building at a square's edge); F1622 item 2: d 0 own, d 1..9 the 3x3 (d 9 = +1,+1) */
        const int qx = sx + (d == 0 ? 0 : ((d - 1) % 3) - 1), qy = sy + (d == 0 ? 0 : ((d - 1) / 3) - 1);
        if (d == 5 || qx < 0 || qx > 63 || qy < 0 || qy > 63) continue;   /* d 5 is (0, 0) again */
        int trunc = 0;
        const int n = coop::LoadedBuildingsInSector(qx, qy, g_cpBldBuf, 4096, &trunc);
        for (int i = 0; i < n; ++i)
        {
            char k[200]; k[0] = 0;
            if (g_cpBldBuf[i] != 0 && coop::ObjectPositionKey(g_cpBldBuf[i], k, (int)sizeof k, "", 3, 1) != 0 && bk == k) return g_cpBldBuf[i];
        }
    }
    return 0;
}
/* F1617 item 6: the loaded buildings of one square as the duplicate check reads them - record id and position */
struct CpLoadedB { std::string sid; float p[3]; };
void CpLoadedOfSector(int sx, int sy, std::vector<CpLoadedB>* out)
{
    out->clear();
    int trunc = 0;
    const int n = coop::LoadedBuildingsInSector(sx, sy, g_cpDupBuf, 4096, &trunc);
    for (int i = 0; i < n; ++i)
    {
        void* gd = 0; char sid[camplaid::kClSidMax + 1]; CpLoadedB b;
        if (g_cpDupBuf[i] == 0 || !CpKindPod(g_cpDupBuf[i], &gd) || !CpSidPod(gd, sid, (int)sizeof sid) || !CpPosPod(g_cpDupBuf[i], b.p)) continue;
        b.sid = sid; out->push_back(b);
    }
}
/* the pieces write of area record `id`: the accepted record with the entries still to write, one write in flight at a time; a write not
   made stays to write and goes at the next event (the stored record's arrival, this write's answer, this game becoming the holder) */
void ClWriteNow(const std::string& id)
{
    std::map<std::string, std::map<std::string, std::vector<camplaid::CampPiece> > >::iterator w = g_clToWrite.find(id);
    if (w == g_clToWrite.end()) return;
    ClKeep& k = g_clMark[id];
    int sx = 0, sy = 0;
    if (camplaid::CampLaidIdParse(id, &sx, &sy) == 0 || w->second.empty() || (k.haveAcc && camplaid::CampLaidCoversKeys(k.acc, w->second))) { g_clToWrite.erase(w); return; }
    /* F1622 item 5: a write in flight does not hold the next one back - each goes when this game lays (the next generation, on top of the
       one in flight), so all of the layer's writes reach the world server ahead of the area report that passes the holder (one ordered line) */
    if (k.inflight && camplaid::CampLaidCoversKeys(k.sent, w->second)) return;   /* the write in flight carries them all */
    camplaid::CampLaidRec base;
    if (k.inflight) base = k.sent; else if (k.haveAcc) base = k.acc;
    camplaid::CampLaidRec r; int capped = 0;
    const int added = camplaid::CampLaidWithEntries(base, w->second, &r, &capped);
    g_clCapped += capped;
    if (added == 0) { g_clToWrite.erase(w); return; }   /* nothing new fits: the stored record stands */
    const int me = coop::StoreMySlot();
    const int piped = k.inflight;
    r.generation = camplaid::CampLaidPipedGen(k.haveSent, k.hiSent, k.haveAcc, k.acc.generation, k.floorGen); r.writerSlot = me; r.sx = sx; r.sy = sy;   /* F1627 item 4: above every generation sent */
    std::vector<char> b; long long stamp = 0;
    if (me < 0 || !camplaid::EncodeCampLaidRec(&b, r) || !coop::StoreSendCampLaidRecord(id, b, &stamp))
    {
        ++g_clMarkNotSent;
        CpKeepSay("camp pieces write " + id + " not sent (slot " + CpN((long long)me) + ", " + CpN((long long)w->second.size()) + " keys to write - kept for the next event; notSent=" + CpN(g_clMarkNotSent) + ")");
        return;
    }
    { ClOut o; o.stamp = stamp; o.rec = r; k.out[r.generation] = o; }   /* F1627 item 4: each write out by its own generation */
    k.inflight = 1; k.stamp = stamp; k.sent = r; k.haveSent = 1; if (r.generation > k.hiSent) k.hiSent = r.generation; ++g_clMarkWrites; if (piped) ++g_cfWritePiped;
    std::string ks;
    for (std::map<std::string, std::vector<camplaid::CampPiece> >::const_iterator i = w->second.begin(); i != w->second.end(); ++i) ks += (ks.empty() ? "" : ", ") + i->first + " (" + CpN((long long)i->second.size()) + " pieces)";
    CpKeepSay("camp pieces write " + id + " gen " + CpN((long long)r.generation) + ": " + CpN((long long)r.laid.size()) + " keys in all; new: " + ks + (capped ? " (" + CpN((long long)capped) + " left out - the record is full)" : std::string()));
}
/* ---- T-1429 F1648 design (B): THE RE-LINK (camplaid.h CampRelinkGate / CampRelinkPick / CampRelinkPiece) ----
   A game that does not hold the area keeps the holder's camp pieces from the area file; each piece's saved town link holds the holder's own
   town numbers (F1646), so the engine linked it to its default town ('Nowhere'). Each piece of key K - found by the area's camp pieces
   record's own list for K (record id and spot, the maker's duplicate test CampPieceDuplicate) - is moved to THIS game's camp town of K through
   the mod's set-town road (build.cpp BdBuildingSetTownPod: the engine's town words, its floor-store records, the pieces inside it, power).
   EVENTS, no timer: the pass for an area runs when the second of these comes - the area's pieces loaded (its record applied, ClApplySquare)
   and this game's camp town of K here (built by the camp pass, matched to the list, or its camp step ran). A key whose town is not here
   waits (g_rlWait) for that town; a key's pass runs once per load (g_rlDone; both end at the area's next read or unload). Never in single
   player. One line per pass per area under its own cap (kRlLineCap). MAIN THREAD.
   REVIEW FOLD F1654: (1) no holder gate - every game's piece is judged by its own town; (2) a key with a piece not found (or a cut building
   read) stays waiting and is passed again at the next event; (3) only a piece in the engine's default town 'Nowhere' (or no live town) and
   owned by no player is moved - a piece in any other town is left alone (camplaid.h CampRelinkPiece / CampRelinkKeyAfter).
   REVIEW FOLD F1659: (B1) each piece is judged from its STORED town words (+0x1D8: camplaid.h CampRelinkLinkOf / CampRelinkPieceOfLink),
   never the engine's name lookup, which names the nearest town for a piece in 'Nowhere'; a piece whose words do not read is not moved,
   counted (linkUnread) and its key waits; (S1) a key waiting for a piece of a square not loaded yet is passed again when that square's area
   file is read and the square is loaded (RlSquareLoaded, looked up by the square); (N1) a key with a faulted move waits too, at most
   kRlFaultTriesMax passes in a world (then gaveUp, said once). */
const int kRlLineCap = 60;
int g_rlLines = 0, g_rlRetryDue = 0;
long long g_rlPasses = 0, g_rlMoved = 0, g_rlRight = 0, g_rlNotFound = 0, g_rlInside = 0, g_rlInsideOther = 0, g_rlFault = 0, g_rlWaitTown = 0, g_rlLeft = 0, g_rlUnmarked = 0, g_rlLinesCut = 0;
long long g_rlWaitPieces = 0, g_rlReadCut = 0, g_rlPlayerOwned = 0;   /* F1654 items 2 / 3 / NIT */
long long g_rlUnread = 0, g_rlWaitFault = 0, g_rlGaveUp = 0, g_rlSqRetry = 0;   /* F1659 B1 / N1 / N1 / S1 */
std::map<std::string, int> g_rlFaultTries;   /* zone|key -> this world's passes with a faulted move or an unread piece (F1659 N1) */
std::set<std::string> g_rlGaveUpSaid;   /* zone|key given up and said, this world (F1659 N1: said once) */
std::map<int, std::set<std::string> > g_rlWaitSq;   /* square (x * 64 + y) -> camplaid ids with a key waiting for a piece of that square (F1659 S1) */
std::map<std::string, std::string> g_rlLastSaid;   /* zone|key -> the key's last state said (a pass that changes nothing says nothing) */
std::map<std::string, std::set<std::string> > g_rlDone;   /* zone -> keys whose pass ran this load */
std::map<std::string, std::set<std::string> > g_rlWait;   /* camplaid id -> keys waiting for this game's town of them */
void RlSay(const std::string& s) { if (g_rlLines >= kRlLineCap) { ++g_rlLinesCut; return; } ++g_rlLines; DebugLog("[CAMP] " + s + (g_rlLines == kRlLineCap ? " (camp re-link line cap reached - counted in campRelink[linesCut] from here)" : "")); }
std::string RlCounts()
{
    return "campRelink[passes,moved,right,notFound,inside,insideOther,fault,waitTown,left,unmarked,linesCut,waitPieces,readCut,playerOwned,linkUnread,waitFault,gaveUp,sqRetry]=" + CpN(g_rlPasses) + "," + CpN(g_rlMoved) + "," + CpN(g_rlRight) + "," + CpN(g_rlNotFound)
           + "," + CpN(g_rlInside) + "," + CpN(g_rlInsideOther) + "," + CpN(g_rlFault) + "," + CpN(g_rlWaitTown) + "," + CpN(g_rlLeft) + "," + CpN(g_rlUnmarked) + "," + CpN(g_rlLinesCut)
           + "," + CpN(g_rlWaitPieces) + "," + CpN(g_rlReadCut) + "," + CpN(g_rlPlayerOwned) + "," + CpN(g_rlUnread) + "," + CpN(g_rlWaitFault) + "," + CpN(g_rlGaveUp) + "," + CpN(g_rlSqRetry);
}
void RlForgetSaid(const std::string& zone) { const std::string pre = zone + "|"; for (std::map<std::string, std::string>::iterator i = g_rlLastSaid.lower_bound(pre); i != g_rlLastSaid.end() && i->first.compare(0, pre.size(), pre) == 0; ) g_rlLastSaid.erase(i++); }   /* a new load says its keys again */
/* the loaded buildings of one square with their objects - record id and position (as CpLoadedOfSector) */
struct CpLoadedO { void* b; std::string sid; float p[3]; };
int CpLoadedObjOfSector(int sx, int sy, std::vector<CpLoadedO>* out)   /* 1 = the read was cut (4096) or did not read (F1654 NIT) */
{
    out->clear();
    int trunc = 0;
    const int n = coop::LoadedBuildingsInSector(sx, sy, g_cpDupBuf, 4096, &trunc);
    const int cut = (n < 0 || trunc != 0) ? 1 : 0;
    for (int i = 0; i < n; ++i)
    {
        void* gd = 0; char sid[camplaid::kClSidMax + 1]; CpLoadedO o;
        if (g_cpDupBuf[i] == 0 || !CpKindPod(g_cpDupBuf[i], &gd) || !CpSidPod(gd, sid, (int)sizeof sid) || !CpPosPod(g_cpDupBuf[i], o.p)) continue;
        o.b = g_cpDupBuf[i]; o.sid = sid; out->push_back(o);
    }
    return cut;
}
void RlArea(const std::string& id, const char* why)
{
    int sx = -1, sy = -1;
    if (camplaid::CampLaidIdParse(id, &sx, &sy) == 0 || g_cnLockReady == 0) return;
    const std::map<std::string, ClKeep>::const_iterator m = g_clMark.find(id);
    const int have = (m != g_clMark.end() && m->second.haveAcc) ? 1 : 0;
    const int single = coop::RoleIsSingle() ? 1 : 0;
    const int loaded = coop::ZoneLoadedSectorTri(sx, sy) == 1 ? 1 : 0;
    if (camplaid::CampRelinkGate(single, have, loaded) != camplaid::kRlGateRun) return;
    const std::string zone = CpZoneId(sx, sy);
    const int holds = CpHoldsNow(sx, sy);   /* said only: F1654 item 1 - no holder gate */
    std::set<std::string> note;
    { CnLocked l; const std::map<std::string, std::set<std::string> >::const_iterator nt = g_cnNote.find(zone); if (nt != g_cnNote.end()) note = nt->second; }
    std::set<std::string>& done = g_rlDone[zone];
    const camplaid::CampLaidRec& acc = m->second.acc;
    const std::vector<int> pick = camplaid::CampRelinkPick(acc, note, done);
    if (pick.empty()) return;
    std::map<int, std::vector<CpLoadedO> > loadedOf;
    std::map<int, int> cutOf;
    std::string per;
    int keysDone = 0, keysWait = 0, said = 0;
    long long aMoved = 0, aRight = 0, aNf = 0, aLeft = 0, aUnread = 0;
    for (size_t i = 0; i < pick.size(); ++i)
    {
        const camplaid::CampLaidEntry& e = acc.laid[pick[i]];
        void* town = CpTownOfKeyPart(camplaid::CampLaidKeyTown(e.key));
        long long moved = 0, right = 0, nf = 0, in = 0, inOther = 0, fault = 0, unmarked = 0, left = 0, owned = 0, unread = 0;
        int readCut = 0;
        std::string from;
        std::set<int> missSq;   /* F1659 S1: the squares of the pieces not found */
        int th[5] = { 0, 0, 0, 0, 0 }, townRead = 0;   /* F1659 B1: this game's camp town of K, its own words (all five, exact) */
        if (town != 0) { float tpos[3] = { 0.0f, 0.0f, 0.0f }; void* tfac = 0; townRead = coop::BdTownReadPod(town, tpos, th, &tfac); }
        if (town != 0)
        {
            std::vector<void*> insideObjs;
            for (size_t j = 0; j < e.pieces.size(); ++j)
            {
                const camplaid::CampPiece& pc = e.pieces[j];
                const coop::Sector ps = coop::SectorOf(pc.x, pc.z);
                void* b = 0;
                int sk = -1;
                if (ps.x >= 0 && ps.x < 64 && ps.y >= 0 && ps.y < 64)
                {
                    sk = ps.x * 64 + ps.y;
                    std::map<int, std::vector<CpLoadedO> >::iterator lo = loadedOf.find(sk);
                    if (lo == loadedOf.end()) { lo = loadedOf.insert(std::make_pair(sk, std::vector<CpLoadedO>())).first; cutOf[sk] = CpLoadedObjOfSector(ps.x, ps.y, &lo->second); if (cutOf[sk]) ++g_rlReadCut; }
                    if (cutOf[sk]) readCut = 1;
                    for (size_t q = 0; q < lo->second.size() && b == 0; ++q)
                        if (camplaid::CampPieceDuplicate(lo->second[q].sid == pc.sid ? 1 : 0, lo->second[q].p[0] - pc.x, lo->second[q].p[1] - pc.y, lo->second[q].p[2] - pc.z)) b = lo->second[q].b;
                }
                int camp = 0, dead = 0;
                if (b != 0 && CpPiecePod(b, &camp, &dead) && camp == 0) ++unmarked;   /* the record's spot names it; its camp mark (+0x241) is counted, not required */
                const int top = b != 0 ? coop::BdBuildingTopLevelPod(b) : 1;
                int ph[5] = { 0, 0, 0, 0, 0 };
                void* was = 0;   /* the town the piece's STORED words name (0 = none, a dead town or not read) */
                const int pRead = b != 0 ? coop::BdBuildingTownHandPod(b, ph) : 0;
                const int kind = pRead != 0 ? coop::BdTownOfWordsPod(ph, &was) : -1;
                const int link = camplaid::CampRelinkLinkOf(pRead, ph, townRead, th, kind);   /* F1659 B1: the stored link, never the nearest-town lookup */
                const int pOwned = b != 0 ? coop::CampPiecePlayerOwned(b) : 0;
                const int d = camplaid::CampRelinkPieceOfLink(b != 0 ? 1 : 0, link, pOwned, top != 0 ? 1 : 0);   /* a fault on top (-1) is tried: the set-town road reports it */
                if (d == camplaid::kRlNotFound) { ++nf; if (sk >= 0) missSq.insert(sk); continue; }
                if (d == camplaid::kRlUnread) { ++unread; continue; }   /* F1659 B1: not moved, not 'right' - the key waits */
                if (d == camplaid::kRlRight) { ++right; continue; }
                if (d == camplaid::kRlLeave) { ++left; if (pOwned) ++owned; continue; }
                if (d == camplaid::kRlInside) { insideObjs.push_back(b); continue; }
                const int r = coop::BdBuildingSetTownPod(b, town, 0);
                if (r == 1) { ++moved; if (from.empty()) from = was != 0 ? coop::BdTownName(was) : std::string("no town"); }
                else if (r == -1) insideObjs.push_back(b);
                else ++fault;
            }
            for (size_t j = 0; j < insideObjs.size(); ++j)   /* after the hosts moved: an inside piece follows its host - judged by its own STORED words (F1659 B1) */
            {
                int ih[5] = { 0, 0, 0, 0, 0 };
                void* it = 0;
                const int iRead = coop::BdBuildingTownHandPod(insideObjs[j], ih);
                const int il = camplaid::CampRelinkLinkOf(iRead, ih, townRead, th, iRead != 0 ? coop::BdTownOfWordsPod(ih, &it) : -1);
                if (il == camplaid::kRlLinkRight) ++in; else if (il == camplaid::kRlLinkUnread) ++unread; else ++inOther;
            }
        }
        const std::string lk = zone + "|" + e.key;
        const long long fu = fault + unread;
        const std::map<std::string, int>::const_iterator ft = g_rlFaultTries.find(lk);
        const int triesBefore = ft != g_rlFaultTries.end() ? ft->second : 0;
        const int st = camplaid::CampRelinkKeyAfter(town != 0 ? 1 : 0, nf, readCut, fu, triesBefore);   /* F1659 N1: a fault or an unread piece keeps the key waiting, bounded per world */
        if (fu > 0) ++g_rlFaultTries[lk];
        if (st == camplaid::kRlKeyDone || st == camplaid::kRlKeyGaveUp)
        {
            done.insert(e.key); ++keysDone;
            if (st == camplaid::kRlKeyGaveUp) ++g_rlGaveUp;
            std::map<std::string, std::set<std::string> >::iterator w = g_rlWait.find(id);
            if (w != g_rlWait.end()) { w->second.erase(e.key); if (w->second.empty()) g_rlWait.erase(w); }
        }
        else
        {
            ++keysWait;
            if (g_rlWait[id].insert(e.key).second) { if (st == camplaid::kRlKeyWaitTown) ++g_rlWaitTown; else if (st == camplaid::kRlKeyWaitFault) ++g_rlWaitFault; else ++g_rlWaitPieces; }
            if (st == camplaid::kRlKeyWaitPieces) for (std::set<int>::const_iterator q = missSq.begin(); q != missSq.end(); ++q) g_rlWaitSq[*q].insert(id);   /* F1659 S1: passed again when that square loads */
        }
        g_rlMoved += moved; g_rlRight += right; g_rlNotFound += nf; g_rlInside += in; g_rlInsideOther += inOther; g_rlFault += fault; g_rlUnmarked += unmarked; g_rlLeft += left; g_rlPlayerOwned += owned; g_rlUnread += unread;
        aMoved += moved; aRight += right; aNf += nf; aLeft += left; aUnread += unread;
        const std::string stText = st == camplaid::kRlKeyDone ? std::string("settled") : st == camplaid::kRlKeyWaitTown ? std::string("WAITS for this game's town of it (not here yet)")
            : st == camplaid::kRlKeyWaitFault ? "WAITS - a move faulted or a piece's town words did not read (pass " + CpN((long long)triesBefore + 1) + " of " + CpN((long long)camplaid::kRlFaultTriesMax) + " in this world) - passed again at the next event"
            : st == camplaid::kRlKeyGaveUp ? "GAVE UP - a move faulted or a piece's town words did not read on " + CpN((long long)camplaid::kRlFaultTriesMax) + " passes in this world (settled for this load; said once)"
            : std::string("WAITS for its pieces (not all found here, or the building read was cut) - passed again at the next event (and when a missing piece's square loads)");
        const std::string seg = town == 0 ? (" key '" + e.key + "' " + stText + ";")
            : (" key '" + e.key + "' -> '" + coop::BdTownName(town) + "': moved " + CpN(moved) + (from.empty() ? std::string() : " (from '" + from + "')") + ", already right " + CpN(right) + ", not found " + CpN(nf)
               + ", left in another town " + CpN(left) + (owned ? " (" + CpN(owned) + " a player's)" : std::string()) + ", inside " + CpN(in) + (inOther ? " (+" + CpN(inOther) + " inside a building of another town)" : std::string())
               + (fault ? ", faulted " + CpN(fault) : std::string()) + (unread ? ", town words not read " + CpN(unread) : std::string()) + (unmarked ? ", " + CpN(unmarked) + " without the camp mark" : std::string()) + (readCut ? ", the building read was cut" : std::string())
               + " of " + CpN((long long)e.pieces.size()) + " - " + stText + ";");
        const std::string sig = CpN((long long)st) + "/" + CpN(nf) + "/" + CpN(readCut) + "/" + CpN(fu);
        const int sayKey = st == camplaid::kRlKeyGaveUp ? (g_rlGaveUpSaid.insert(lk).second ? 1 : 0) : ((moved != 0 || fault != 0 || g_rlLastSaid[lk] != sig) ? 1 : 0);   /* F1659 N1: a give-up is said once a world */
        if (sayKey) { g_rlLastSaid[lk] = sig; per += seg; said = 1; }   /* a pass that changes nothing for a key says nothing */
    }
    if (aMoved != 0 || aRight != 0 || aLeft != 0) ++g_rlPasses;
    if (!said) return;
    RlSay("camp re-link area " + zone + " at " + why + " (holder now " + (holds == 1 ? "this game" : holds == 0 ? "another game" : "not known") + "): " + CpN((long long)keysDone) + " keys settled, " + CpN((long long)keysWait) + " waiting -"
          + per + " totals moved " + CpN(aMoved) + " / already right " + CpN(aRight) + " / not found " + CpN(aNf) + " / left in another town " + CpN(aLeft) + " / town words not read " + CpN(aUnread) + " (F1648 design B, F1654, F1659: each piece judged by its stored town words; " + RlCounts() + ")");
}
/* the keys waiting for their town or their pieces are tried again (an event: this game's camp town built, its camps matched to the list, a camp step ran) */
void RlWaitRetry(const char* why)
{
    if (g_rlWait.empty()) return;
    std::vector<std::string> ids;
    for (std::map<std::string, std::set<std::string> >::const_iterator w = g_rlWait.begin(); w != g_rlWait.end(); ++w) ids.push_back(w->first);
    for (size_t i = 0; i < ids.size(); ++i) RlArea(ids[i], why);
}
/* REVIEW FOLD F1659 S1: square (sx, sy) loaded here with its area file read (the apply's event, MAIN THREAD) - the keys waiting for a piece of
   that square are passed again, found by the square (g_rlWaitSq), no sweep of every key; `except` is the area the apply passes itself */
void RlSquareLoaded(int sx, int sy, const std::string& except)
{
    if (sx < 0 || sx >= 64 || sy < 0 || sy >= 64) return;
    const std::map<int, std::set<std::string> >::iterator w = g_rlWaitSq.find(sx * 64 + sy);
    if (w == g_rlWaitSq.end()) return;
    const std::set<std::string> ids = w->second;
    g_rlWaitSq.erase(w);   /* a key still waiting after this pass is listed again by its pass */
    for (std::set<std::string>::const_iterator i = ids.begin(); i != ids.end(); ++i)
        if (*i != except && g_rlWait.count(*i) != 0) { ++g_rlSqRetry; RlArea(*i, "a square of its pieces loaded here (its area file read)"); }
}
struct ClSqItem { std::string rid; int entry; void* town; int cx, cy, here; };   /* T-1429 F1671: one entry ready in a square's apply (its record, town, record square, pieces here) */
/* T-1429 F1671 THE PER-SQUARE APPLY (MAIN THREAD, the square loaded here with its area file read - the apply loop's gate): `id` names the
   SQUARE (its camplaid id). Each camp pieces record with pieces in this square (the index g_clSqIdx, camplaid.h CampLaidIndexAdd) is
   picked against THIS square's note (camplaid.h CampLaidSquarePick): an entry whose key the note has is skipped - the square's own file
   carries its pieces here (pieces destroyed here stay gone); an entry it lacks has the pieces STANDING IN THIS SQUARE made, piece by piece
   (the maker's duplicate check CampPieceDuplicate: a loaded building of the same record within 1 of the spot is kept, none made), then its
   key goes into this square's note - so the square's next save carries the pieces and the key, and nothing doubles. The record's own
   square also takes the key of an entry with no piece in it (the camp step reads the centre's note). Kept from F1612: an entry whose town
   is not here waits for it (item 7); in another square it waits for the record's own square too (its town and nest building stand there:
   camplaid.h CampLaidSquareReady); a piece outside the record's square and the squares beside it is never made (item 8, counted at the
   record's own square); a piece the camp maker put inside its nest building is made inside it (item 6); at most kClApplyTickPieces pieces
   a tick (item 8) - returns 1 when entries remain for the next tick. F1648: the re-link runs for the square's own record before the
   apply and, after it, for each record with an entry applied here (its key may be in the centre's note only now). *settledHere = 1 when
   the square has records and none of its entries waits (the apply loop then tries the waiting squares again: a record's own square may
   be loaded now) */
int ClApplySquare(const std::string& id, int* settledHere)
{
    *settledHere = 0;
    int qx = -1, qy = -1;
    if (camplaid::CampLaidIdParse(id, &qx, &qy) == 0 || g_cnLockReady == 0) return 0;
    const int sq = camplaid::CampSqKey(qx, qy);
    const std::string zone = CpZoneId(qx, qy);
    RlArea(id, "the area's pieces loaded (its record applied here)");   /* T-1429 F1648: the entries the note had came with the area file - re-linked to this game's town */
    const std::map<int, std::set<std::string> >::const_iterator ix = g_clSqIdx.find(sq);
    if (ix == g_clSqIdx.end()) return 0;
    std::set<std::string> note;
    { CnLocked l; const std::map<std::string, std::set<std::string> >::const_iterator nt = g_cnNote.find(zone); if (nt != g_cnNote.end()) note = nt->second; }
    std::vector<ClSqItem> ready; std::vector<int> counts;
    std::map<std::string, void*> townOf;
    long long skipped = 0; int waiting = 0, waitOwn = 0, nestWaiting = 0, unreadWaiting = 0;
    std::string skippedKeys;
    for (std::set<std::string>::const_iterator r = ix->second.begin(); r != ix->second.end(); ++r)
    {
        const std::map<std::string, ClKeep>::const_iterator m = g_clMark.find(*r);
        int cx = -1, cy = -1;
        if (m == g_clMark.end() || !m->second.haveAcc || camplaid::CampLaidIdParse(*r, &cx, &cy) == 0) continue;
        const camplaid::CampLaidRec& acc = m->second.acc;
        std::vector<int> skip;
        const std::vector<int> pick = camplaid::CampLaidSquarePick(acc, qx, qy, note, &skip);
        for (size_t i = 0; i < skip.size(); ++i)
        {
            ++skipped;
            if (skipped <= 4) skippedKeys += (skippedKeys.empty() ? "" : ", ") + acc.laid[skip[i]].key + " (" + CpN((long long)camplaid::CampLaidPiecesInSquare(acc.laid[skip[i]], sq)) + " pieces here, record " + *r + ")";
        }
        const int own = (cx == qx && cy == qy) ? 1 : 0;
        const int ownLoaded = own ? 1 : (coop::ZoneLoadedSectorTri(cx, cy) == 1 ? 1 : 0);
        for (size_t i = 0; i < pick.size(); ++i)
        {
            const std::string& key = acc.laid[pick[i]].key;
            const std::string tp = camplaid::CampLaidKeyTown(key);
            void* town = 0;
            const std::map<std::string, void*>::const_iterator tc = townOf.find(tp);
            if (tc != townOf.end()) town = tc->second; else { town = CpTownOfKeyPart(tp); townOf[tp] = town; }
            const int rd = camplaid::CampLaidSquareReady(town != 0 ? 1 : 0, own, ownLoaded);
            if (rd != camplaid::kSqReady)
            {
                ++waiting; if (rd == camplaid::kSqWaitOwn) { ++waitOwn; ++g_csWaitOwn; } else ++g_cfWaitTown;
                if (g_clWaitTownSaid.insert(id + "|" + key).second)
                    CpKeepSay("pieces apply square " + zone + " key=" + key + " (record " + *r + ") WAITS for "
                              + (rd == camplaid::kSqWaitOwn ? std::string("the record's own square (its town and nest building stand there; not loaded here)") : std::string("its town (not here yet)"))
                              + " - applied at the next furniture step or square applied here (F1612 item 7: no piece without its town; F1671)");
                continue;
            }
            ClSqItem it; it.rid = *r; it.entry = pick[i]; it.town = town; it.cx = cx; it.cy = cy; it.here = camplaid::CampLaidPiecesInSquare(acc.laid[pick[i]], sq);
            ready.push_back(it); counts.push_back(it.here);
        }
    }
    if (waiting != 0) g_clWaitTown.insert(id); else *settledHere = 1;
    const int take = camplaid::CampApplyShareCount(counts, camplaid::kClApplyTickPieces);
    if (take == (int)ready.size()) { g_clApplySkipped += skipped; g_csSkipNote += skipped; }
    long long madeAll = 0, dupAll = 0, failAll = 0;
    std::set<std::string> touched;   /* records with an entry applied here: re-linked after */
    std::vector<CpLoadedB> loadedHere;   /* F1617 item 6: this square's loaded buildings, read once per apply call (before any piece of it is made) */
    int loadedRead = 0;
    for (int i = 0; i < take; ++i)
    {
        const ClSqItem& it = ready[i];
        const camplaid::CampLaidEntry& e = g_clMark.find(it.rid)->second.acc.laid[it.entry];   /* in the index and picked above: there */
        const std::string bk = camplaid::CampLaidKeyBuilding(e.key);
        void* nest = it.here > 0 ? CpNestOfKeyPart(bk, it.cx, it.cy) : 0;
        /* T-1429 F1753 review / re-check fold (MEDIUM): THE ONE 'NEST GONE' RULE (camplaid.h CampNestGone) - whatever this game saw before:
           the nest's square (from its key) LOADED here, read this load, and the entry's town has run a camp step since that read, and the nest
           is still not found -> gone */
        const int nestGone = (it.here > 0 && nest == 0 && !bk.empty()) ? CiNestGoneNow(bk, camplaid::CampLaidKeyTown(e.key)) : 0;
        camplaid::CampNestPlan plan;
        if (!camplaid::CampEntryNestPlan(e, sq, nest != 0 ? 1 : 0, nestGone, &plan))
        {   /* T-1429 F1753 design (a): a piece the camp's game recorded inside its nest building WAITS for that building here - the whole entry
               (no piece of it made in this square, its key not noted), tried again at an event (a camp step here, an area file read here, a
               square settled here) - never made outdoors, no timer (camplaid.h CampEntryNestPlan) */
            ++nestWaiting; ++g_ciWaitNest;
            if (g_clWaitTownSaid.insert(id + "|" + e.key + "|nest").second)
                CpKeepSay("pieces apply square " + zone + " key=" + e.key + " (record " + it.rid + ") WAITS for its nest building '" + bk + "': " + CpN((long long)plan.wait)
                          + " of its pieces here were made inside it on the camp's game and it is not standing here yet - applied at the next camp step, area file read or square settled here (F1753; waitNest="
                          + CpN(g_ciWaitNest) + ")");
            continue;
        }
        if (it.here > 0 && nest == 0 && !bk.empty()) ++g_cfNestMissing;
        int made = 0, fail = 0, why1 = 0, out = 0, inside = 0, dup = 0, gone = 0, oldN = 0, nestUnread = 0;
        for (size_t j = 0; j < e.pieces.size(); ++j)
        {
            const camplaid::CampPiece& pc = e.pieces[j];
            const int psq = camplaid::CampPieceSquare(pc);
            if (psq != sq)
            {   /* another square's piece: made by that square's own apply - only one beyond the record's bound is counted, at the record's own square */
                if (it.cx == qx && it.cy == qy && (psq < 0 || camplaid::CampPieceInBound(it.cx, it.cy, psq / 64, psq % 64) == 0)) { ++out; ++fail; }
                continue;
            }
            if (!loadedRead) { CpLoadedOfSector(qx, qy, &loadedHere); loadedRead = 1; }
            int same = 0;   /* F1617 item 6: the camp maker's duplicate check - a loaded building of the same record already standing there is kept, no piece made */
            for (size_t b = 0; b < loadedHere.size() && !same; ++b) same = camplaid::CampPieceDuplicate(loadedHere[b].sid == pc.sid ? 1 : 0, loadedHere[b].p[0] - pc.x, loadedHere[b].p[1] - pc.y, loadedHere[b].p[2] - pc.z);
            if (same) { ++dup; ++g_cfDup; continue; }
            const float pos[3] = { pc.x, pc.y, pc.z };
            const float rot[4] = { pc.qw, pc.qx, pc.qy, pc.qz };
            int why = 0, in = 0;
            const int road = camplaid::CampPieceMakeRoad(pc.inside, nest != 0 ? 1 : 0, nestGone);   /* F1753: wait was decided above */
            if (road == camplaid::kMakeNestGone) { ++gone; continue; }   /* review fold (MEDIUM): its building no longer exists - NOT made (never floating where it stood) */
            if (road == camplaid::kMakeOldGuess) ++oldN;
            const int fact = road == camplaid::kMakeInNest ? camplaid::kClInsideNest : road == camplaid::kMakeOldGuess ? camplaid::kClInsideUnknown : camplaid::kClInsideOut;
            if (coop::CampPieceMake(pc.sid, pos, rot, it.town, nest, fact, &why, &in) != 0 && why == 0) { ++made; inside += in; } else { ++fail; if (why1 == 0) why1 = why; if (why == 5) ++nestUnread; }
        }
        if (gone > 0)
        {   /* F1753 review fold (MEDIUM): the nest building is gone by the one rule - its inside pieces are not made; the entry's outdoor pieces are, and it counts settled */
            g_ciNestGone += gone;
            if (g_ciGoneSaid.insert(e.key).second)
                CpKeepSay("pieces apply square " + zone + " key=" + e.key + ": its nest building '" + bk + "' is GONE (its square is loaded here, read this load, the town ran a camp step after that read, and it is not found) - "
                          + CpN((long long)gone) + " pieces the camp's game made inside it are NOT made (they lived in a building that no longer exists); its outdoor pieces are made (F1753 review fold; nestGone="
                          + CpN(g_ciNestGone) + ", said once a camp)");
        }
        if (!camplaid::CampEntryNoteAfterMake(nestUnread))
        {   /* review fold LOW 1: an inside piece was not made because its nest building did not read (why 5) - the key stays OUT of the note, so
               the square's next event (a camp step, an area read, a square settled here) tries the entry again; the pieces made now stand (the
               duplicate check keeps them) - no timer */
            ++unreadWaiting; ++g_ciUnreadRetry;   /* re-check fold LOW: its own counter - not an applied / noted entry */
            if (g_clWaitTownSaid.insert(id + "|" + e.key + "|unread").second)
                CpKeepSay("pieces apply square " + zone + " key=" + e.key + " (record " + it.rid + "): " + CpN((long long)nestUnread) + " inside pieces not made - its nest building '" + bk
                          + "' did not read (why 5); the key is NOT noted - tried again at the next camp step, area file read or square settled here (F1753 review fold; nestUnread counted in campF1753)");
        }
        else CnNoteAdd(zone, e.key);
        touched.insert(it.rid);
        if (nestUnread == 0) ++g_clApplyEntries;   /* re-check fold LOW: an entry left out of the note is not counted applied */
        g_clApplyMade += made; g_clApplyFail += fail; madeAll += made; dupAll += dup; failAll += fail; g_cfOutOfBound += out; g_cfInside += inside;
        g_csMade += made; if (it.cx != qx || it.cy != qy) g_csMadeAway += made; g_csDup += dup; g_csFail += fail; if (nestUnread == 0) ++g_csNoted;
        CpKeepSay("pieces apply square " + zone + " key=" + e.key + " (record " + it.rid + "): made=" + CpN((long long)made) + " already standing=" + CpN((long long)dup) + " failed=" + CpN((long long)fail)
                  + " of " + CpN((long long)it.here) + " standing in this square (" + CpN((long long)e.pieces.size()) + " in the camp; inside its nest building " + CpN((long long)inside)
                  + (it.here > 0 && nest == 0 && !bk.empty() ? ", its nest building not found here" + (gone > 0 ? " (gone: " + CpN((long long)gone) + " inside pieces not made)" : std::string()) : std::string())
                  + (oldN > 0 ? ", " + CpN((long long)oldN) + " from an old record (inside not known) placed by the point lookup" : std::string()) + ")"
                  + (fail ? " (first why " + CpN((long long)why1) + ": 1 no row / factory, 2 not a building record, 3 a fault, 4 null, 5 its nest building did not read"
                            + (out ? "; " + CpN((long long)out) + " outside the record's square and the squares beside it - not made" : std::string()) + ")" : std::string())
                  + (nestUnread ? std::string(" - the key is NOT in this square's note (its nest building did not read: tried again at the next event)") : std::string(" - the key is in this square's note (F1671: each piece belongs to the square that holds it)")));
    }
    if (nestWaiting > 0 || unreadWaiting > 0) { g_clWaitNest.insert(id); *settledHere = 0; }   /* F1753: this square waits for a nest building (or its read) - not settled */
    const int more = take < (int)ready.size() ? 1 : 0;
    if (take > 0) ++g_csApplied;
    for (std::set<std::string>::const_iterator t = touched.begin(); t != touched.end(); ++t) RlArea(*t, "a square of its pieces applied here (F1671)");
    if (take > 0 || skipped > 0 || waiting > 0 || nestWaiting > 0 || unreadWaiting > 0)
        CpKeepSay("pieces apply square " + zone + ": entries applied " + CpN((long long)(take - nestWaiting - unreadWaiting)) + ", made=" + CpN(madeAll) + " already standing=" + CpN(dupAll) + " failed=" + CpN(failAll)
                  + ", skipped=" + CpN(skipped) + " (this square's note had them" + (skipped ? ": " + skippedKeys + (skipped > 4 ? ", +" + CpN(skipped - 4) + " more" : std::string()) : std::string()) + ")"
                  + (nestWaiting ? ", " + CpN((long long)nestWaiting) + " waiting for their nest building (F1753)" : std::string())
                  + (unreadWaiting ? ", " + CpN((long long)unreadWaiting) + " not noted - their nest building did not read (tried again at the next event)" : std::string())
                  + (waiting ? ", " + CpN((long long)waiting) + " waiting" + (waitOwn ? " (" + CpN((long long)waitOwn) + " for the record's own square)" : std::string()) : std::string())
                  + (more ? ", " + CpN((long long)ready.size() - (long long)take) + " more next tick (" + CpN((long long)camplaid::kClApplyTickPieces) + " pieces a tick)" : std::string()));
    return more;
}

/* the pieces a step made around `at` (camp pieces within the keep's reach not there before); *unread = those that did not read. nest: the
   step's nest building (its building hand; 0 for the town's own entry) - each piece's inside fact is read against it (F1753); countIt: a
   laid step's pieces are counted in campF1753, an unbuilt step's not (review fold NIT) */
void CpMadeSince(const float* at, const std::set<void*>& before, std::vector<camplaid::CampPiece>* made, std::vector<void*>* objs, long long* unread, void* nest, int countIt)
{
    std::set<void*> after;
    CpPiecesNear(at[0], at[2], 0, &after);
    *unread = 0;
    for (std::set<void*>::const_iterator q = after.begin(); q != after.end(); ++q)
    {
        if (before.count(*q) != 0) continue;
        if (objs != 0) objs->push_back(*q);
        camplaid::CampPiece pc;
        if (CpPieceReadPod(*q, &pc, nest, countIt)) made->push_back(pc); else ++*unread;
    }
}
/* a laid step's bookkeeping (the step itself or a kept step run later): its pieces, the key into the note and, online, into the record */
std::string CpLaidAfter(const float* at, const std::set<void*>& before, const std::string& key, const std::string& zone, const std::string& id, int single, int link, void* nest)
{
    std::vector<camplaid::CampPiece> made; long long unread = 0;
    CpMadeSince(at, before, &made, 0, &unread, nest, 1);
    g_clPieceUnread += unread;
    std::string t = ": laid - pieces " + CpN((long long)made.size() + unread) + (unread ? " (" + CpN(unread) + " did not read - not in the record)" : std::string());
    CnNoteAdd(zone, key);
    std::set<int> sqs;   /* T-1429 F1671: the key goes into the note of every square a made piece stands in (each square's file carries its own pieces) */
    camplaid::CampPiecesSquares(made, &sqs);
    int others = 0;
    for (std::set<int>::const_iterator q = sqs.begin(); q != sqs.end(); ++q) { const std::string qz = CpZoneId(*q / 64, *q % 64); if (qz != zone) { CnNoteAdd(qz, key); ++others; } }
    const std::string sqText = others ? " (and the notes of " + CpN((long long)others) + " more squares its pieces stand in - F1671)" : std::string();
    if (!single && link && !id.empty()) { g_clToWrite[id][key] = made; ClWriteNow(id); t += "; the key is in the note" + sqText + " and goes into " + id; }
    else t += "; the key is in the note" + sqText;
    return t;
}
/* F1617 item 3: an unbuilt step's pieces - laid by the engine, left out of the save (g_cpUnsaved; the 'needs saving' check keeps the
   engine's own 'no' for them), so the next load lays them again and nothing doubles */
std::string CpUnbuiltAfter(const float* at, const std::set<void*>& before, const std::string& zone, void* nest)
{
    std::vector<camplaid::CampPiece> made; std::vector<void*> objs; long long unread = 0;
    CpMadeSince(at, before, &made, &objs, &unread, nest, 0);   /* review fold NIT: an unbuilt step's pieces are not the record's - not counted */
    for (size_t i = 0; i < objs.size(); ++i)
    {   /* F1622 item 3: what stands at the address now - its area, record and position */
        CpUnsaved u; u.zone = zone;
        { int ux = -1, uy = -1; if (CpZoneXY(zone, &ux, &uy)) u.useq = CuSeqOf(ux, uy); }   /* F1627 item 3 */
        if (!CpKindPod(objs[i], &u.gd) || !CpPosPod(objs[i], u.p)) u.gd = 0;
        g_cpUnsaved[objs[i]] = u;
    }
    g_cfUnbuiltPieces += (long long)objs.size();
    return ": UNBUILT key (a part did not read) - the engine laid it, pieces " + CpN((long long)objs.size()) + " left out of the save (F1617 item 3: nothing doubles, nobody is left without a camp; unbuilt="
           + CpN(g_cfUnbuilt) + " unbuiltPieces=" + CpN(g_cfUnbuiltPieces) + ")";
}
/* F1617 item 1: the step's 4th point word (the queue item's +0x3C) - copied with the point; 0 = a fault */
int CpPt4Pod(const float* p, float* w) { __try { *w = p[3]; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { *w = 0.0f; return 0; } }
long CnReadGenOf(const std::string& zone)
{
    if (g_cnLockReady == 0) return 0;
    CnLocked l;
    const std::map<std::string, long>::const_iterator g = g_cnReadGen.find(zone);
    return g != g_cnReadGen.end() ? g->second : 0;
}
/* F1617 item 1: an owed step KEPT with its exact arguments (the point as it was handed to the engine) */
std::string CkKeep(void* town, void* rec, const float* xyz, const float* pt, unsigned long long hand, const std::string& key, const std::string& zone, const std::string& id)
{
    std::vector<CkStep>& v = g_ckKept[zone];
    for (size_t i = 0; i < v.size(); ++i) if (v[i].key == key) return " - KEPT already (its key once)";
    CkStep s; s.town = town; s.rec = rec; s.pt[0] = xyz[0]; s.pt[1] = xyz[1]; s.pt[2] = xyz[2]; CpPt4Pod(pt, &s.pt[3]); s.hand = hand; s.key = key; s.zone = zone; s.id = id; s.readGen = CnReadGenOf(zone);
    { int kx = -1, ky = -1; s.useq = CpZoneXY(zone, &kx, &ky) ? CuSeqOf(kx, ky) : 0; }   /* F1627 item 3: ends at the area's next unload */
    v.push_back(s); ++g_ckKeptN;
    return " - KEPT: run through the engine's own step if this game becomes the area's camp layer (the world server's HELD, an answer naming it), dropped when the key arrives (kept=" + CpN(g_ckKeptN) + ")";
}
/* F1617 item 1: the event that makes this game the area's camp layer - every kept step of `zone` is decided (camplaid.h CampKeptDecide) and
   run through the engine's own step with its kept arguments when its key is neither in the note nor in the record and its town and
   building are the same live objects (each found on the engine's lists by its position key, the very pointer kept). MAIN THREAD. */
void CkRunArea(const std::string& zone, const char* why, int needHold)
{
    std::map<std::string, std::vector<CkStep> >::iterator z = g_ckKept.find(zone);
    if (z == g_ckKept.end() || orig_cpDebris == 0) return;
    if (needHold != 0 && !coop::RoleIsSingle() && coop::StoreCampLinkUp())
    {   /* F1627 item 2: an answer naming this game is decided again from the live holding fact - not the holder now (or not known): the
           steps stay kept (the HELD its holding brings, or the tick once its live area map names this game) */
        int hx = -1, hy = -1;
        if (!CpZoneXY(zone, &hx, &hy) || CpHoldsNow(hx, hy) != 1)
        {
            ++g_cfNotHolderKept;
            CpKeepSay("kept steps of area " + zone + " at " + why + ": this game is not the area's holder now (its live area map) - they stay kept (F1627 item 2; notHolderKept=" + CpN(g_cfNotHolderKept) + ")");
            return;
        }
    }
    std::vector<CkStep> v; v.swap(z->second); g_ckKept.erase(z);
    std::set<std::string> note;
    if (g_cnLockReady != 0) { CnLocked l; const std::map<std::string, std::set<std::string> >::const_iterator nt = g_cnNote.find(zone); if (nt != g_cnNote.end()) note = nt->second; }
    const int single = coop::RoleIsSingle() ? 1 : 0, link = coop::StoreCampLinkUp() ? 1 : 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const CkStep& s = v[i];
        const std::map<std::string, ClKeep>::const_iterator m = g_clMark.find(s.id);
        const int inRec = (m != g_clMark.end() && m->second.haveAcc && camplaid::CampLaidFind(m->second.acc, s.key) >= 0) ? 1 : 0;
        int sx = -1, sy = -1; camplaid::CampLaidIdParse(s.id, &sx, &sy);
        const int townSame = CpTownOfKeyPart(camplaid::CampLaidKeyTown(s.key)) == s.town ? 1 : 0;
        const std::string bk = camplaid::CampLaidKeyBuilding(s.key);
        const int bldSame = (s.hand == 0 && bk.empty()) ? 1 : (s.hand != 0 && !bk.empty() && CpNestOfKeyPart(bk, sx, sy) == (void*)(uintptr_t)s.hand) ? 1 : 0;
        const int d = camplaid::CampKeptDecide(note.count(s.key) != 0 ? 1 : 0, inRec, townSame, bldSame);
        if (d != camplaid::kKeptRun)
        {
            if (d == camplaid::kKeptDropKeyHere) ++g_ckDropKey; else ++g_ckDropGone;
            CpKeepSay("kept step key=" + s.key + " area " + zone + " at " + why + ": " + camplaid::CampKeptName(d) + " (town same " + CpN((long long)townSame) + ", building same " + CpN((long long)bldSame) + ")");
            continue;
        }
        std::set<void*> before;
        CpPiecesNear(s.pt[0], s.pt[2], 0, &before);
        float pt[4] = { s.pt[0], s.pt[1], s.pt[2], s.pt[3] };
        g_cpStepSkipping = 0;
        orig_cpDebris(s.town, s.rec, pt, s.hand);   /* the engine's own step, the kept arguments */
        ++g_ckRun; ++g_clLaid; ++g_cpStepsRan;
        CpKeepSay("kept step key=" + s.key + " area " + zone + " RUN at " + why + " (the deferred step: the engine's own furniture step with its kept town, record, point and building)" + CpLaidAfter(s.pt, before, s.key, zone, s.id, single, link, (void*)(uintptr_t)s.hand) + " (run=" + CpN(g_ckRun) + ")");
    }
}
/* T-1429 re-check fold F1627 item 3: area x, y was unloaded (its unload count reached seq): what belongs to a load before that unload ends -
   the read in force (unless a read came after it), an answer waiting for a read (unless asked after it - F1627 NIT), the kept steps and
   the unsaved pieces of earlier loads (their objects go with the area). MAIN THREAD. */
void CuUnloaded(int x, int y, long seq)
{
    const std::string zone = CpZoneId(x, y);
    int hadRead = 0, answerDropped = 0;
    {
        CnLocked l;
        const std::map<std::string, long>::const_iterator ru = g_cnReadUseq.find(zone);
        const std::map<std::string, long>::const_iterator su = g_cnStartUseq.find(zone);   /* F1634: a load started after the unload stands too */
        if (g_cnLayerAtLoad.find(zone) != g_cnLayerAtLoad.end() && (ru == g_cnReadUseq.end() || ru->second < seq) && (su == g_cnStartUseq.end() || su->second < seq)) { g_cnLayerAtLoad.erase(zone); hadRead = 1; }
        const std::map<std::string, long>::const_iterator au = g_cnAskUseq.find(zone);
        if (au == g_cnAskUseq.end() || au->second < seq) answerDropped = g_cnLayerWaiting.erase(zone) != 0 ? 1 : 0;
    }
    long long kept = 0, unsaved = 0;
    const std::map<std::string, std::vector<CkStep> >::iterator z = g_ckKept.find(zone);
    if (z != g_ckKept.end())
    {
        for (size_t i = 0; i < z->second.size(); )
        {
            if (z->second[i].useq >= seq) { ++i; continue; }
            ++kept; ++g_ckDropUnload;
            CpKeepSay("kept step key=" + z->second[i].key + " area " + zone + " DROPPED - the area was unloaded here (F1627 item 3: the engine's unload hook; dropUnload=" + CpN(g_ckDropUnload) + ")");
            z->second.erase(z->second.begin() + i);
        }
        if (z->second.empty()) g_ckKept.erase(z);
    }
    for (std::map<void*, CpUnsaved>::iterator u = g_cpUnsaved.begin(); u != g_cpUnsaved.end(); ) { if (u->second.zone == zone && u->second.useq < seq) { g_cpUnsaved.erase(u++); ++unsaved; } else ++u; }
    g_cfUnsavedUnload += unsaved; ++g_cuApplied;
    g_rlDone.erase(zone); g_rlWait.erase(camplaid::MakeCampLaidId(x, y)); RlForgetSaid(zone);   /* T-1429 F1648: the re-link belongs to the load */
    if (hadRead || kept || unsaved || answerDropped)
        CpKeepSay("area " + zone + " UNLOADED here (the engine's unload hook) - its load ends (read in force " + CpN((long long)hadRead) + ", kept steps dropped " + CpN(kept) + ", unsaved pieces forgotten "
                  + CpN(unsaved) + ", waiting answer dropped " + CpN((long long)answerDropped) + "; F1627 item 3; unloadsTaken=" + CpN(g_cuApplied) + ")");
}
/* F1627 item 3: every area unload the hook counted since the last drain is taken (MAIN THREAD - the tick, and before each decision that
   reads a load: a furniture step, an answer, a HELD) */
void CuDrain()
{
    if (!CpOnMain()) return;
    for (int k = 0; k < 4096; ++k)
    {
        const LONG s = g_cuSeq[k];
        if (s == g_cuDone[k]) continue;
        g_cuDone[k] = s;
        CuUnloaded(k / 64, k % 64, (long)s);
    }
    const LONG un = g_cuUnnamed;
    if (un != g_cuUnnamedSaid) { g_cuUnnamedSaid = un; CpKeepSay("an area unload whose area did not read (the unload hook's content) - counted, unnamed=" + CpN((long long)un) + " (F1627 item 3)"); }
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
/* T-1429 A1 (campgame.h CampPieceSaveAnswer) - the buildings' 'needs saving' check 0x54F690: the engine's answer first; in an area save
   a camp piece that is not destroyed answers 'yes' too. Any thread (an area save may run off the main thread): the totals are
   interlocked; the per-save count (CampAreaSaveEnd's line) is the main thread's. */
unsigned long long detour_cpNeedsSave(void* bld, void* name)
{
    const unsigned long long r = orig_cpNeedsSave(bld, name);
    if (g_cpKeepOn == 0 || !CpOnMain()) return r;   /* T-1429 camp note design: the keep saves camp pieces only in a main-thread save - the only save that writes the camp note too */
    if (!g_cpUnsaved.empty())
    {   /* F1617 item 3: an unbuilt step's piece - the engine's own answer (left out of the save); F1622 item 3: only while the object there is
           the same piece (its record, within 1 of its spot) - a later piece at a reused address is saved as any other */
        const std::map<void*, CpUnsaved>::iterator us = g_cpUnsaved.find(bld);
        if (us != g_cpUnsaved.end())
        {
            void* gd = 0; float p[3] = { 0, 0, 0 };
            const int same = (us->second.gd != 0 && CpKindPod(bld, &gd) && gd == us->second.gd && CpPosPod(bld, p)
                              && camplaid::CampPieceDuplicate(1, p[0] - us->second.p[0], p[1] - us->second.p[1], p[2] - us->second.p[2])) ? 1 : 0;
            if (same) return r;
            g_cpUnsaved.erase(us); ++g_cfUnsavedStale;
        }
    }
    int camp = 0, dead = 0, empty = 0;
    if (!CpPiecePod(bld, &camp, &dead) || !camp || !CpNameEmptyPod(name, &empty) || !empty) return r;
    const int engine = (r & 0xFFULL) != 0 ? 1 : 0;
    const int onMain = CpOnMain() ? 1 : 0;
    if (engine) { ::InterlockedIncrement64(&g_cpSaveEngineYes); if (onMain) ++g_cpSaveEngineYesMain; return r; }
    if (campg::CampPieceSaveAnswer(1, empty, camp, dead, engine) == 0) { ::InterlockedIncrement64(&g_cpSaveDestroyed); if (onMain) ++g_cpSaveDestroyedMain; return r; }
    ::InterlockedIncrement64(&g_cpSaveYes); if (onMain) ++g_cpSaveYesMain;
    {   /* review SHOULD: which of the engine's two conditions refused it */
        int slot = -1, et = -1, le = 0;
        const int rd = CpWhyPod(bld, &slot, &et, &le);
        const int bySlot = (rd && slot != 8) ? 1 : 0, byEntry = (rd && (le || et == 0xB)) ? 1 : 0;
        if (!rd) { ::InterlockedIncrement64(&g_cpWhyUnread); if (onMain) ++g_cpWhyUnreadMain; }
        else if (bySlot && byEntry) { ::InterlockedIncrement64(&g_cpWhyBoth); if (onMain) ++g_cpWhyBothMain; }
        else if (bySlot) { ::InterlockedIncrement64(&g_cpWhySlot); if (onMain) ++g_cpWhySlotMain; }
        else if (byEntry) { ::InterlockedIncrement64(&g_cpWhyEntry); if (onMain) ++g_cpWhyEntryMain; }
        if (onMain && g_cpWhyLogBudget > 0)
        {
            --g_cpWhyLogBudget;
            float bp[3] = { 0, 0, 0 };
            const int pOk = CpPosPod(bld, bp);
            std::ostringstream w; w.setf(std::ios::fixed); w.precision(1);
            w << "camp keep: piece saved at " << bp[0] << "," << bp[2] << (pOk ? "" : " (position unread)") << " - the engine's conditions: ";
            if (!rd) w << "unread";
            else
            {
                w << "slot +0x2F0 = " << slot << (bySlot ? " (refuses)" : " (passes)") << ", first +0x3D8 entry ";
                if (le) w << "none - empty list (refuses)"; else if (et == 0xB) w << "type 0xB, no object (refuses)"; else w << "type " << et << " (passes)";
                if (!bySlot && !byEntry) w << " - neither refused it";
            }
            CpKeepSay(w.str() + (g_cpWhyLogBudget == 0 ? " (piece line cap reached - counted in campKeepWhy from here)" : ""));
        }
    }
    return (r & ~0xFFULL) | 1ULL;
}
/* T-1429 A1 (campgame.h CampMakeCount) - the camp maker 0x92F250: inside a skipped furniture step its count is 0 (it makes nothing and
   frees its list as usual); every other call is the engine's own. */
void detour_cpMake(void* kind, float* centre, float spreadMin, float spreadMax, unsigned long long count, void* town, void* hand, void* list)
{
    const int skipping = ((g_cpStepSkipping != 0 && CpOnMain()) || (g_cpOffTls != TLS_OUT_OF_INDEXES && ::TlsGetValue(g_cpOffTls) != 0)) ? 1 : 0;   /* F1612 item 5: a step off the main thread too */
    if (skipping) ++g_cpMakesZeroed;
    orig_cpMake(kind, centre, spreadMin, spreadMax, campg::CampMakeCount(skipping, count), town, hand, list);
}
/* OPTION A - A CAMP'S FURNITURE (campgame.h CampDebrisPoint). A list camp's furniture step is handed a copy of its point with the
   list spot's x / z (the engine's height kept); the caller's point is untouched, and every other step gets the engine's own point. */
void detour_cpDebris(void* town, void* rec, float* pt, unsigned long long hand)
{
    const int onMain = CpOnMain() ? 1 : 0;
    if (!onMain)
    {
        ::InterlockedIncrement(&g_cpDebrisOffMain);
        if (g_cpKeepOn != 0 && g_cpOffTls != TLS_OUT_OF_INDEXES)
        {   /* T-1429 review fold F1612 item 5 (root rule: no saved piece without its note entry): off the main thread no key is noted - the camp
               maker makes nothing (no run has shown such a step; counted in campFold[offMainZeroed]) */
            ::InterlockedIncrement(&g_cfOffMainZeroed);
            ::TlsSetValue(g_cpOffTls, (LPVOID)1); orig_cpDebris(town, rec, pt, hand); ::TlsSetValue(g_cpOffTls, 0);
            return;
        }
        orig_cpDebris(town, rec, pt, hand); return;
    }
    float eng[3] = { 0, 0, 0 };
    if (g_cpLockReady == 0 || town == 0 || pt == 0 || !CpSpotPod(pt, eng))
    {
        ++g_cpDebrisUnread;
        if (g_cpKeepOn != 0)
        {   /* F1612 item 5: a step that did not read has no key to note - the camp maker makes nothing while the keep is on */
            ++g_cfUnreadZeroed; g_cpStepSkipping = 1; orig_cpDebris(town, rec, pt, hand); g_cpStepSkipping = 0;
            CpKeepSay("step unread (town / point did not read): the camp maker made nothing - no key to note (F1612 item 5; unreadZeroed=" + CpN(g_cfUnreadZeroed) + ")");
            return;
        }
        orig_cpDebris(town, rec, pt, hand); return;
    }
    ++g_cpDebrisSteps;
    CuDrain();   /* F1627 item 3: an unload before this step ends its load first */
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
    /* T-1429 s15 (F1356: no line showed the engine's furniture steps): one line per step on the main thread - its town, record, point,
       building, whether its point was the list's, and the buildings listed in the point's area and the ground items within kCpGroundR
       of it before and after the engine's step (the step's own work, read around it; read only) */
    const int say = g_cpStepLogBudget > 0 ? 1 : 0;
    coop::Sector sec; sec.x = -1; sec.y = -1;
    int bld0 = -1, gr0 = -1, trunc = 0;
    if (say)
    {
        sec = coop::SectorOf(eng[0], eng[2]);
        bld0 = coop::LoadedBuildingsInSector(sec.x, sec.y, g_cpBldBuf, 4096, &trunc); if (trunc != 0) bld0 = -1;
        coop::GroundAroundSay(eng[0], eng[2], kCpGroundR, -1, -1, 0, &gr0);
    }
    else ++g_cpStepLinesCut;
    /* T-1429 camp note design (camplaid.h CampNoteStepDecide): the nest entry's key, the area's camp note (read with the area file, before
       this step - camplaid.h) and this load's camp layer decide - note-had (the copy loaded here carries this camp: make nothing), laid
       (this game lays: the engine's step runs, the key goes into the note and, online, into the area's camp pieces record) or owed (the
       world server named another game the layer: make nothing - that game's record brings the pieces) */
    int clStep = camplaid::kStepKeepOff, clKeyOk = 0, clInNote = 0, clNoteKnown = 0, clLayer = camplaid::kLayerUnknown, clSingle = 0, clLink = 0, clAsked = 0, clHolds = -1, clLoadLayer = camplaid::kLayerUnknown, clLive = 0, clAfter = 0;
    std::string clKey, clZone, clId;
    std::set<void*> keepBefore;
    const float* clAt = forced ? use : eng;
    if (g_cpKeepOn != 0)
    {
        const coop::Sector asec = coop::SectorOf(clAt[0], clAt[2]);
        clZone = CpZoneId(asec.x, asec.y); clId = camplaid::MakeCampLaidId(asec.x, asec.y);
        clKey = CpStepKey(town, rec, hand, listCamp ? it->second : std::string());
        clKeyOk = clKey.empty() ? 0 : 1;
        if (g_cnLockReady != 0)
        {
            CnLocked l;
            const std::map<std::string, std::set<std::string> >::const_iterator nt = g_cnNote.find(clZone);
            if (nt != g_cnNote.end()) { clNoteKnown = 1; clInNote = (clKeyOk && nt->second.count(clKey) != 0) ? 1 : 0; }
            const std::map<std::string, int>::const_iterator la = g_cnLayerAtLoad.find(clZone);
            if (la != g_cnLayerAtLoad.end()) clLayer = la->second;
            clAsked = CnAskedThisLoad(clZone, asec.x, asec.y);
        }
        clSingle = coop::RoleIsSingle() ? 1 : 0;
        clLink = coop::StoreCampLinkUp() ? 1 : 0;
        if (!clSingle && clLink)
        {   /* T-1429 re-check fold F1627 items 2 / 5: the layer is decided HERE from the live holding fact (camplaid.h CampLayerAtStep) - an
               answer naming this game is not taken back by itself when the area passes to another game during a long load */
            unsigned clMapSeq = 0;
            clLive = 1; clLoadLayer = clLayer; clHolds = CpHoldsNow(asec.x, asec.y, &clMapSeq);
            int clMark = 0, clNever = 0;
            if (g_cnLockReady != 0) { CnLocked l; clAfter = CnMapAfterLoad(clZone, asec.x, asec.y, clMapSeq, &clMark, &clNever); }   /* F1630 item 1 / F1634 */
            if (clMark == camplaid::kMarkStart) ++g_cfMarkStart; else if (clMark == camplaid::kMarkUnload) { ++g_cfMarkUnload; if (clNever) ++g_cfMarkNever; }   /* F1638 SHOULD */
            clLayer = camplaid::CampLayerAtStep(clLoadLayer, clHolds, clAsked, clAfter);
            if (clHolds == 1 && clLoadLayer == camplaid::kLayerUnknown && clAsked == 0 && clAfter == 0) ++g_cfMapBeforeRead;
            if (clLayer == camplaid::kLayerYou && clLoadLayer != camplaid::kLayerYou) ++g_cfStepSelfLay;
            else if (clLayer != camplaid::kLayerYou && clLoadLayer == camplaid::kLayerYou) ++g_cfStepHoldKept;
        }
        clStep = camplaid::CampNoteStepDecide(1, onMain, clKeyOk, clInNote, clSingle, clLink, clLayer);
        if (!camplaid::CampLaidStepMakes(clStep)) g_cpStepSkipping = 1;
        if (clStep == camplaid::kStepNoteHad) { ++g_clNoteHad; ++g_cpStepsSkipped; } else if (clStep == camplaid::kStepLaid) { ++g_clLaid; ++g_cpStepsRan; } else if (clStep == camplaid::kStepOwed) { ++g_clOwed; ++g_cpStepsSkipped; if (clLayer != camplaid::kLayerOther) ++g_cfNoAnswerOwed; }
        else if (clStep == camplaid::kStepNoKey) ++g_cpStepsSkipped;
        else if (clStep == camplaid::kStepUnbuilt) { ++g_cfUnbuilt; ++g_cpStepsRan; }
        if (!clKeyOk) ++g_clNoKey;
        if (clStep == camplaid::kStepLaid || clStep == camplaid::kStepUnbuilt) CpPiecesNear(clAt[0], clAt[2], 0, &keepBefore);
    }
    const long long zeroed0 = g_cpMakesZeroed;
    if (!forced) orig_cpDebris(town, rec, pt, hand);
    else { ++g_cpDebrisForced; orig_cpDebris(town, rec, use, hand); }
    g_cpStepSkipping = 0;
    if (g_cpKeepOn != 0 && clStep != camplaid::kStepKeepOff)
    {
        std::ostringstream k; k.setf(std::ios::fixed); k.precision(1);
        k << "step #" << g_cpDebrisSteps << " " << camplaid::CampLaidStepName(clStep) << " key '" << (clKeyOk ? clKey : std::string("(not built - a part did not read)")) << "' area " << clZone
          << " at " << clAt[0] << "," << clAt[2] << " (note " << (clNoteKnown ? (clInNote ? "had it" : "lacks it") : "none read") << ", layer " << CnLayerName(clLayer)
          << (clSingle ? ", single player" : "") << (clLink ? "" : ", link down");
        if (clLive) k << "; this load's answer " << CnLayerName(clLoadLayer) << (clAsked ? " (asked)" : " (not asked)") << ", holder now " << (clHolds == 1 ? "this game" : clHolds == 0 ? "another game" : "not known")
                      << (clHolds == 1 ? (clAfter ? " (a map after this read)" : " (its map came before this read: the next map decides)") : "")
                      << " - F1627 items 2 / 5: selfLay=" << g_cfStepSelfLay << " holdKept=" << g_cfStepHoldKept << " mapBeforeRead=" << g_cfMapBeforeRead;
        k << ")";
        if (clStep == camplaid::kStepLaid) k << CpLaidAfter(clAt, keepBefore, clKey, clZone, clId, clSingle, clLink, (void*)(uintptr_t)hand);
        else if (clStep == camplaid::kStepUnbuilt) k << CpUnbuiltAfter(clAt, keepBefore, clZone, (void*)(uintptr_t)hand);
        else
        {
            k << ": camp maker calls zeroed " << (g_cpMakesZeroed - zeroed0);
            if (clStep == camplaid::kStepOwed && clLayer == camplaid::kLayerOther) k << " - owed: the world server named another game this area's camp layer; its camp pieces record brings the pieces";
            else if (clStep == camplaid::kStepOwed) k << " - owed: NO layer answer for this load while the link is up (F1612 item 1: never two layers) - the layer's record, or the world server's HELD, decides";
            if (clStep == camplaid::kStepOwed) k << CkKeep(town, rec, clAt, pt, hand, clKey, clZone, clId);   /* F1617 item 1 (BLOCKER): never zero layers */
            else if (clStep == camplaid::kStepNoKey) k << " - off the main thread: nothing made (F1612 item 5: a piece with no note entry is never saved)";
            else k << " - note had it: the copy loaded here carries this camp";
        }
        CpKeepSay(k.str());
    }
    if (g_cpKeepOn != 0 && clKeyOk) CiTownStep(camplaid::CampLaidKeyTown(clKey));   /* F1753 re-check fold (MEDIUM): this town's camp step takes the next step number (the one 'nest gone' rule) */
    if (!g_clWaitTown.empty()) { g_clApplyDue.insert(g_clWaitTown.begin(), g_clWaitTown.end()); g_clWaitTown.clear(); }   /* F1612 item 7: a town is being set up here - entries waiting for their town are tried again */
    if (!g_clWaitNest.empty()) { g_clApplyDue.insert(g_clWaitNest.begin(), g_clWaitNest.end()); g_clWaitNest.clear(); }   /* F1753: a camp's nest building is being set up here (the step's building) - entries waiting for their nest are tried again */
    if (!g_rlWait.empty()) g_rlRetryDue = 1;   /* T-1429 F1648 / F1654: a camp step ran - keys waiting for their town or pieces are tried at the next tick (not inside the engine's step) */
    if (!say) return;
    trunc = 0;
    int bld1 = coop::LoadedBuildingsInSector(sec.x, sec.y, g_cpBldBuf, 4096, &trunc); if (trunc != 0) bld1 = -1;
    int gr1 = -1;
    const std::string grSay1 = coop::GroundAroundSay(eng[0], eng[2], kCpGroundR, -1, -1, 8, &gr1);
    char tsid[campl::kCampStrMax + 1], rsid[campl::kCampStrMax + 1]; void* tk = 0; float tp[3] = { 0, 0, 0 };
    const int tOk = (CpKindPod(town, &tk) && CpSidPod(tk, tsid, (int)sizeof tsid)) ? 1 : 0;
    const int rOk = CpSidPod(rec, rsid, (int)sizeof rsid) ? 1 : 0;
    const int pOk = CpPosPod(town, tp) ? 1 : 0;
    std::ostringstream o; o.setf(std::ios::fixed); o.precision(1);
    o << "furniture step #" << g_cpDebrisSteps << ": town '" << (tOk ? tsid : "?") << "'";
    if (pOk) o << " at " << tp[0] << "," << tp[2]; else o << " (its position did not read)";
    if (listCamp) o << " (list camp '" << it->second << "')"; else o << " (not a list camp)";
    o << ", record '" << (rOk ? rsid : "?") << "', point " << eng[0] << "," << eng[1] << "," << eng[2] << ", hand 0x" << std::hex << hand << std::dec
      << (hand == 0 ? " (the town's own point)" : " (one of its buildings)") << ", point forced to the list spot=" << forced
      << " - area " << sec.x << "." << sec.y << " buildings listed before -> after " << bld0 << " -> " << bld1
      << ", ground within " << (int)kCpGroundR << " before -> after " << gr0 << " -> " << gr1 << " (after: " << grSay1 << ")"
      << " (-1: the walk was cut or did not read)";
    --g_cpStepLogBudget;
    DebugLog("[CAMP] " + o.str() + (g_cpStepLogBudget == 0 ? " (furniture step line cap reached - counted in campLines[stepCut] from here)" : ""));
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
    std::string groundBefore;   /* T-1429 s15 (F1369): the ground around a forced camp's spot as its pass begins */
    if (mode == campg::kPassBuild && g_cpGroundLogBudget > 0)
        groundBefore = CpOnMain() ? coop::GroundAroundSay(g_cpForced.rec.x, g_cpForced.rec.z, kCpGroundR, -1, -1, 12, 0) : std::string("not read (the pass ran off the main thread)");
    ::InterlockedExchange(&g_cpPassTid, (LONG)::GetCurrentThreadId());
    orig_cpPass(grid);
    ::InterlockedExchange(&g_cpPassTid, 0);
    CpLocked l;
    if (mode == campg::kPassBuild)
    {
        CpDone d; d.rec = g_cpForced.rec; d.outcome = campg::CampBuildOutcome(g_cpKindGiven, g_cpNearView, g_cpSpotGiven, g_cpSpawned);
        d.town = g_cpSpawnTown; d.posOk = g_cpSpawnPosOk; d.x = g_cpSpawnPos[0]; d.y = g_cpSpawnPos[1]; d.z = g_cpSpawnPos[2];
        d.groundBefore = groundBefore;
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

/* T-1429 camp note design: see camp.h */
void CampNoteRead(const std::string& zoneId, int fileOk, int found, int unreadable, const std::vector<std::string>& keys, int onMain, const std::string& path)
{
    (void)path;   /* not logged: a file path names the player's folders */
    if (g_cnLockReady == 0) return;
    int kept = -1, layer = camplaid::kLayerUnknown, say = 0;
    const unsigned readMapSeq = coop::AreaMapApplySeqTS();   /* F1630 item 1: before the lock - the map number at this read */
    {
        CnLocked l;
        ++g_cnReads; if (!onMain) ++g_cnReadsOff; if (found) ++g_cnReadsFound; g_cnReadBad += unreadable;
        if (fileOk)
        {
            kept = camplaid::CampNoteOnRead(&g_cnNote[zoneId], fileOk, keys);
            ++g_cnReadGen[zoneId];   /* F1617 item 1: a new read - steps kept from an earlier read of the area end at the next tick */
            int rx = -1, ry = -1; const int rxy = CpZoneXY(zoneId, &rx, &ry);
            const std::map<std::string, int>::const_iterator old = g_cnLayerAtLoad.find(zoneId);
            const int keepOld = (rxy && old != g_cnLayerAtLoad.end() && CnEntryThisLoad(zoneId, rx, ry)) ? 1 : 0;   /* F1634: this load's layer so far (its start, a HELD, an answer taken) */
            const int oldLayer = keepOld ? old->second : (int)camplaid::kLayerUnknown;
            if (rxy) { g_cnReadUseq[zoneId] = CuSeqOf(rx, ry); g_cnReadMapSeq[zoneId] = readMapSeq; g_cnReadStepSeq[zoneId] = (long)g_ciStepSeq; }   /* F1627 item 3: this read belongs to the load after the area's last unload; F1630 item 1: only a map after it can make a no-ask load the layer */
            const std::map<std::string, int>::const_iterator w = g_cnLayerWaiting.find(zoneId);
            layer = camplaid::CampLayerAtRead((w != g_cnLayerWaiting.end() && (rxy == 0 || CnAskedThisLoad(zoneId, rx, ry))) ? w->second : -1, keepOld, oldLayer);   /* F1634 */   /* kept until the area is loaded: the load's later reads take it too; F1627 NIT: only an answer to this load's ask */
            g_cnLayerAtLoad[zoneId] = layer;
            g_cnReadSince.insert(zoneId);
            if (g_cnLineBudget > 0 && (found || unreadable || kept > 0 || layer != camplaid::kLayerUnknown)) { --g_cnLineBudget; say = 1; }
        }
    }
    if (say)
        DebugLog("[CAMP] note read area=" + zoneId + " keys=" + CpN((long long)kept) + (found ? std::string() : std::string(" (no note in the file: emptied)"))
                 + (unreadable ? " unreadable=" + CpN((long long)unreadable) : std::string()) + " layer=" + CnLayerName(layer) + (onMain ? std::string() : std::string(" (off the main thread)")));
}
void CampNoteKeysForSave(const std::string& zoneId, std::vector<std::string>* keys, int* over)
{
    keys->clear(); *over = 0;
    if (g_cnLockReady == 0) return;
    CnLocked l;
    const std::map<std::string, std::set<std::string> >::const_iterator nt = g_cnNote.find(zoneId);
    if (nt == g_cnNote.end()) return;
    for (std::set<std::string>::const_iterator i = nt->second.begin(); i != nt->second.end(); ++i) { if ((int)keys->size() >= camplaid::kCnNoteMax) { ++*over; continue; } keys->push_back(*i); }
}
void CampNoteSaved(const std::string& zoneId, int keys, int over, const char* fail)
{
    if (keys < 0) ++g_cnSaveFail; else ++g_cnSaved;
    g_cnOver += over;
    if (g_cnLineBudget <= 0) return;
    --g_cnLineBudget;
    DebugLog("[CAMP] note saved area=" + zoneId + (keys < 0 ? std::string(" NOT written - ") + (fail != 0 ? fail : "?") : " keys=" + CpN((long long)keys)) + (over ? " (" + CpN((long long)over) + " over the cap left out)" : std::string()));
}
void CampLayerAnswer(int x, int y, int layer, int kind)
{
    if (g_cnLockReady == 0 || x < 0 || y < 0) return;
    if (layer != camplaid::kLayerYou && layer != camplaid::kLayerOther) { ++g_cnLayerNone; return; }   /* F1612 item 1: no layer named - not an answer about the layer; the answer this load has stands */
    CuDrain();   /* F1627 item 3 */
    const std::string zone = CpZoneId(x, y);
    const std::string ansText = CnAnswerText(x, y, layer);   /* F1645 (2): 'no holder yet' when no game holds the area - log text only */
    if (coop::ZoneLoadedSectorTri(x, y) == 1)
    {   /* F1612 item 4: the area is loaded here (ZoneMap+0xB1 is set after its file read) - a late answer for a load already done: dropped,
           never taken by the area's next load */
        ++g_cfLateLayer;
        { CnLocked l; g_cnLayerWaiting.erase(zone); if (layer == camplaid::kLayerYou) g_cnLayerAtLoad[zone] = layer; }
        if (g_cnLineBudget > 0) { --g_cnLineBudget; DebugLog("[CAMP] layer answer area=" + zone + " kind=" + CpN((long long)kind) + " layer=" + ansText + " LATE - the area is loaded here already: not kept for its next load"
                                                            + (layer == camplaid::kLayerYou ? std::string("; it names this game - this load's kept steps run now (F1617 item 1)") : std::string()) + " (lateLayer=" + CpN(g_cfLateLayer) + ")"); }
        if (layer == camplaid::kLayerYou) CkRunArea(zone, "a late answer naming this game", 1);
        return;
    }
    int readNow = 0;
    {   /* F1622 NIT: the area's file was read for this load (its read is on record since its last unload or ask) and the engine has not
           flagged it loaded yet - the answer is THIS read's: taken now (its kept steps run when it names this game), not kept for a next read */
        CnLocked l;
        if (g_cnLayerAtLoad.find(zone) != g_cnLayerAtLoad.end()) { g_cnLayerAtLoad[zone] = layer; g_cnLayerWaiting.erase(zone); readNow = 1; }
        else g_cnLayerWaiting[zone] = layer;
    }
    if (readNow)
    {
        ++g_cfReadNowLayer;
        if (g_cnLineBudget > 0) { --g_cnLineBudget; DebugLog("[CAMP] layer answer area=" + zone + " kind=" + CpN((long long)kind) + " layer=" + ansText + " for the read already made (not yet flagged loaded) - taken by this read"
                                                            + (layer == camplaid::kLayerYou ? std::string("; it names this game - this read's kept steps run now") : std::string()) + " (F1622 NIT; readNowLayer=" + CpN(g_cfReadNowLayer) + ")"); }
        if (layer == camplaid::kLayerYou) CkRunArea(zone, "an answer naming this game for this read", 1);
        return;
    }
    if (layer == camplaid::kLayerYou) ++g_cnLayerYou; else if (layer == camplaid::kLayerOther) ++g_cnLayerOther; else ++g_cnLayerNone;
    if (g_cnLineBudget > 0) { --g_cnLineBudget; DebugLog("[CAMP] layer answer area=" + zone + " kind=" + CpN((long long)kind) + " layer=" + ansText); }
}
/* F1612 item 4: this game asks the world server about the area for a new load (its first ask, or a settle ask) - an answer kept from
   before belongs to an earlier load: dropped, so the load takes only its own answer */
void CampLayerAsked(int x, int y)
{
    if (g_cnLockReady == 0 || x < 0 || y < 0) return;
    const std::string zone = CpZoneId(x, y);
    int had = 0;
    const int loadedNow = coop::ZoneLoadedSectorTri(x, y) == 1 ? 1 : 0;
    { CnLocked l; had = g_cnLayerWaiting.erase(zone) != 0 ? 1 : 0; if (!loadedNow && !CnEntryThisLoad(zone, x, y)) g_cnLayerAtLoad.erase(zone); g_cnAskUseq[zone] = CuSeqOf(x, y); }   /* F1634: this load's own layer (its start or read) stands */   /* F1627: this load's ask (after the area's last unload) */   /* F1622 item 4: a new load of an area not loaded here - no read of it is in force */
    if (!had) return;
    ++g_cfAskCleared;
    if (g_cnLineBudget > 0) { --g_cnLineBudget; DebugLog("[CAMP] layer answer area=" + zone + " dropped - a new ask of the area: it answered an earlier load (askCleared=" + CpN(g_cfAskCleared) + ")"); }
}
/* T-1429 re-check fold F1634: the engine's 'load decided' for area x, y (store.cpp detour_afZoneMapActivate, after this load's ask): the load
   is in force from now, file read or not - a never-saved area hands the engine no file and records no read (store.cpp CampNoteAfterLoad runs
   only from the engine's area-file load). Its layer so far is not known (an answer kept for it is taken: its ask came after the area's last
   unload), and the map number now is its mark (CnMapAfterLoad). MAIN THREAD. */
void CampAreaLoadStart(int x, int y)
{
    if (g_cnLockReady == 0 || CuKey(x, y) < 0) return;
    CuDrain();   /* the area's last unload ends its earlier load first */
    const unsigned seq = coop::AreaMapApplySeqTS();
    const std::string zone = CpZoneId(x, y);
    int layer = camplaid::kLayerUnknown;
    {   /* the layer so far: this load's own (a read or a start of it came first) stands, a HELD of it above all; else its kept answer */
        CnLocked l;
        const std::map<std::string, int>::const_iterator old = g_cnLayerAtLoad.find(zone);
        const int keepOld = (old != g_cnLayerAtLoad.end() && CnEntryThisLoad(zone, x, y)) ? 1 : 0;
        const std::map<std::string, int>::const_iterator w = g_cnLayerWaiting.find(zone);
        layer = camplaid::CampLayerAtRead((w != g_cnLayerWaiting.end() && CnAskedThisLoad(zone, x, y)) ? w->second : -1, keepOld, keepOld ? old->second : (int)camplaid::kLayerUnknown);
        g_cnLayerAtLoad[zone] = layer;
        g_cnStartUseq[zone] = CuSeqOf(x, y); g_cnStartMapSeq[zone] = seq;
    }
    ++g_cfLoadStarts;
    if (g_cfLoadStarts <= 150) { DebugLog("[CAMP] load start area=" + zone + " layer=" + CnLayerName(layer) + " map#" + CpN((long long)seq) + " - this load is in force from now, file read or not (F1634; the first 150 logged, its own cap; loadStarts=" + CpN(g_cfLoadStarts) + ")"); }
}
/* T-1429 re-check fold F1617 items 1 / 4: the world server's HELD - this game holds the area (its own holder change, or this game listed it
   again) and no other game is its camp layer; the area's stored camp pieces record came just before it on the same line. This load of the
   area is laid here from now (its later steps lay), its kept owed steps the note and the record lack run through the engine's own step, and
   a camp pieces write kept after a 'not the holder' refusal goes now (F1617 item 4: no 10 s re-arm). MAIN THREAD. */
void CampAreaHeld(int x, int y)
{
    if (g_cnLockReady == 0 || x < 0 || y < 0 || x > 63 || y > 63) return;
    ++g_ckHeldIn;
    CuDrain();   /* F1627 item 3 */
    const std::string zone = CpZoneId(x, y), id = camplaid::MakeCampLaidId(x, y);
    int loadedHere = 0;
    const int engineLoaded = coop::ZoneLoadedSectorTri(x, y);   /* F1622 item 4: the engine's own loaded byte (-1 unread) */
    { CnLocked l; const int hadEntry = g_cnLayerAtLoad.find(zone) != g_cnLayerAtLoad.end() ? 1 : 0; if (hadEntry || engineLoaded == 1) { g_cnLayerAtLoad[zone] = camplaid::kLayerYou; loadedHere = engineLoaded == 1 ? 1 : 0; if (!hadEntry) ++g_cfHeldNoRead; } }   /* F1634: a loaded area with no layer on record (no read, its start not seen) is this load too */   /* read since its last unload: it lays from now; F1627 NIT: an unreadable loaded flag (-1) is not 'loaded' - its kept steps run at the tick once the engine flags it */
    const size_t kept = g_ckKept.count(zone) != 0 ? g_ckKept[zone].size() : 0;
    std::map<std::string, ClKeep>::iterator m = g_clMark.find(id);
    const int waited = (m != g_clMark.end() && m->second.waitHolder) ? 1 : 0;
    CpKeepSay("HELD area " + zone + " - the world server: this game holds the area and no other game is its camp layer" + (loadedHere ? std::string(" (this load lays from now)") : std::string(" (the area is not loaded here - nothing for a load)"))
              + "; kept steps " + CpN((long long)kept) + (waited ? std::string("; the camp pieces write kept after a 'not the holder' refusal goes now (write-now, F1617 item 4)") : std::string()) + " (heldIn=" + CpN(g_ckHeldIn) + ")");
    if (m != g_clMark.end()) m->second.waitHolder = 0;
    if (loadedHere) CkRunArea(zone, "the world server's HELD", 0);
    if (g_clToWrite.count(id) != 0) ClWriteNow(id);
}
/* T-1429 re-check fold F1627 item 3: see camp.h - the engine's unload hook (items.cpp detour_zoneDeactivate), ANY THREAD */
int CampAreaUnloadSector(void* content, int* sx, int* sy) { *sx = -1; *sy = -1; return (CpContentSectorPod(content, sx, sy) && CuKey(*sx, *sy) >= 0) ? 1 : 0; }
void CampAreaUnloaded(int named, int sx, int sy) { const int k = named != 0 ? CuKey(sx, sy) : -1; if (k >= 0) { ::InterlockedExchange(&g_cuUnloadMapSeq[k], (LONG)coop::AreaMapApplySeqTS()); ::InterlockedIncrement(&g_cuSeq[k]); } else ::InterlockedIncrement(&g_cuUnnamed); }   /* F1634: the map number at the unload, before the count moves */
void CampLaidArrive(const std::string& id, const std::vector<char>& bytes)
{
    camplaid::CampLaidRec r;
    const int d = bytes.empty() ? (int)camplaid::kClShort : camplaid::DecodeCampLaidRec(&bytes[0], bytes.size(), &r);
    int sx = -1, sy = -1;
    if (d != camplaid::kClOk || camplaid::CampLaidIdParse(id, &sx, &sy) == 0 || r.sx != sx || r.sy != sy)
    { ++g_clMarkBad; if (g_clMarkBad <= 10) CpKeepSay("camp pieces record " + id + " from the world server not taken - " + camplaid::CampLaidDecodeWhy(d) + " (bad=" + CpN(g_clMarkBad) + ")"); return; }
    ClKeep& k = g_clMark[id];
    if (k.haveAcc && (r.generation < k.acc.generation || (r.generation == k.acc.generation && r.writerSlot == k.acc.writerSlot && camplaid::CampLaidRecHasAll(k.acc, r)))) return;   /* F1627 item 1: the world server's grown copy of a write (keys it lacked) is taken */
    k.haveAcc = 1; k.acc = r; ++g_clMarkArrived;
    while (!k.out.empty() && k.out.begin()->first <= r.generation) { k.out.erase(k.out.begin()); ++g_cfOutDropped; }   /* F1627 item 4: writes at or below the stored generation are answered by it */
    k.inflight = k.out.empty() ? 0 : 1;
    long long pieces = 0; for (size_t i = 0; i < r.laid.size(); ++i) pieces += (long long)r.laid[i].pieces.size();
    if (g_clMarkArrived <= 40) CpKeepSay("camp pieces record " + id + " arrived: gen " + CpN((long long)r.generation) + ", writer " + CpN((long long)r.writerSlot) + ", " + CpN((long long)r.laid.size()) + " keys, " + CpN(pieces) + " pieces - applied once the area is loaded here (arrived=" + CpN(g_clMarkArrived) + ", first 40 logged)");
    {   /* T-1429 F1671: the record is indexed under every square its pieces stand in, and each of those squares is due its apply (once loaded here) */
        std::set<int> sqs;
        camplaid::CampLaidSquaresOf(r, &sqs);
        camplaid::CampLaidIndexAdd(&g_clSqIdx, id, r);
        for (std::set<int>::const_iterator q = sqs.begin(); q != sqs.end(); ++q) { const std::string qid = camplaid::MakeCampLaidId(*q / 64, *q % 64); if (!qid.empty()) g_clApplyDue.insert(qid); }
    }
    {   /* F1617 item 1: a kept step whose key the record carries ends - the layer's pieces are here (applied once the area is loaded) */
        const std::map<std::string, std::vector<CkStep> >::const_iterator z = g_ckKept.find(CpZoneId(sx, sy));
        std::vector<std::string> ks;
        if (z != g_ckKept.end()) for (size_t i = 0; i < z->second.size(); ++i) if (camplaid::CampLaidFind(r, z->second[i].key) >= 0) ks.push_back(z->second[i].key);
        for (size_t i = 0; i < ks.size(); ++i) CkDropKey(CpZoneId(sx, sy), ks[i], "its key arrived in the area's camp pieces record");
    }
    ClWriteNow(id);
}
void CampLaidEcho(const std::string& id, int hasStamp, long long stamp)
{
    std::map<std::string, ClKeep>::iterator m = g_clMark.find(id);
    if (!hasStamp || m == g_clMark.end() || m->second.out.empty()) return;
    ClKeep& k = m->second;
    std::map<unsigned int, ClOut>::iterator o = k.out.begin();
    while (o != k.out.end() && o->second.stamp != stamp) ++o;
    if (o == k.out.end()) return;
    const camplaid::CampLaidRec got = o->second.rec;
    k.out.erase(k.out.begin(), ++o);   /* F1627 item 4: this write and the earlier ones (answered in order on one line) end; a later one stays out */
    k.inflight = k.out.empty() ? 0 : 1;
    if (!k.haveAcc || got.generation >= k.acc.generation) { k.haveAcc = 1; k.acc = got; camplaid::CampLaidIndexAdd(&g_clSqIdx, id, got); }   /* T-1429 F1671: the layer's own record indexed by its squares (this game made the pieces: no apply due) */
    k.waitHolder = 0; ++g_clMarkTaken;
    CpKeepSay("camp pieces record " + id + " gen " + CpN((long long)m->second.acc.generation) + " stored by the world server (" + CpN((long long)m->second.acc.laid.size()) + " keys; taken=" + CpN(g_clMarkTaken) + ")");
    ClWriteNow(id);
}
void CampLaidRefused(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen)
{
    ++g_clMarkRefused;
    ClKeep& k = g_clMark[id];
    if (storedGen > k.floorGen) k.floorGen = storedGen;
    k.out.erase(sentGen); k.inflight = k.out.empty() ? 0 : 1;   /* F1627 item 4: only the refused write ends - one still out stays (no generation is used twice: CampLaidPipedGen) */
    const int notHolder = reason == coopbrec::kBrRefuseNotWriter ? 1 : 0;
    const int held = reason == coopbrec::kBrRefuseHeld ? 1 : 0;   /* F1612 item 2: the stored file is held by another program - written again at the next event */
    if (held) ++g_cfHeld;
    const int keep = camplaid::CampLaidRefusalSendsStored(reason) || notHolder || held;
    if (notHolder) k.waitHolder = 1;   /* F1617 item 4: written again at the world server's HELD for the area (sent on its own holder change) - no timer */
    std::map<std::string, std::map<std::string, std::vector<camplaid::CampPiece> > >::iterator w = g_clToWrite.find(id);
    long long dropped = 0;
    if (!keep && w != g_clToWrite.end()) { dropped = (long long)w->second.size(); g_clToWriteDropped += dropped; g_clToWrite.erase(w); }
    CpKeepSay("camp pieces write " + id + " gen " + CpN((long long)sentGen) + " REFUSED - " + coopbrec::BoxRefuseWhy(reason) + " (stored gen " + CpN((long long)storedGen) + "; "
              + (notHolder ? std::string("kept - written again at the world server's HELD (this game holds the area)") : held ? std::string("kept - the stored file is held on the world server; written again at the area's next save or the record's next arrival")
                 : keep ? std::string("the stored record comes with the answer - written again on top of it") : "its " + CpN(dropped) + " keys dropped") + "; refused=" + CpN(g_clMarkRefused) + ")");
}

/* T-1429 A1: see camp.h */
void CampAreaSaveBegin() { if (!CpOnMain()) return; g_cpSaveYesMain = 0; g_cpSaveDestroyedMain = 0; g_cpSaveEngineYesMain = 0; g_cpWhySlotMain = 0; g_cpWhyEntryMain = 0; g_cpWhyBothMain = 0; g_cpWhyUnreadMain = 0; }
void CampAreaSaveEnd(void* content)
{
    if (!CpOnMain() || g_cpKeepOn == 0) return;
    {   /* F1612 item 2: the area's save is an event - a camp pieces write kept after a refusal (held) goes again now */
        int ex = -1, ey = -1;
        if (CpContentSectorPod(content, &ex, &ey))
        {
            const std::string eid = camplaid::MakeCampLaidId(ex, ey);
            const std::map<std::string, ClKeep>::const_iterator ek = g_clMark.find(eid);
            if (!eid.empty() && g_clToWrite.count(eid) != 0 && (ek == g_clMark.end() || (!ek->second.inflight && !ek->second.waitHolder))) ClWriteNow(eid);
        }
    }
    if (g_cpSaveYesMain == 0 && g_cpSaveDestroyedMain == 0 && g_cpSaveEngineYesMain == 0) return;   /* no camp piece in this area */
    int sx = -1, sy = -1;
    if (!CpContentSectorPod(content, &sx, &sy)) { sx = -1; sy = -1; }
    CpKeepSay("camp keep: area save " + CpN(sx) + "." + CpN(sy) + ": " + CpN(g_cpSaveYesMain) + " camp pieces saved by the keep (the engine saved "
              + CpN(g_cpSaveEngineYesMain) + " itself; " + CpN(g_cpSaveDestroyedMain) + " destroyed left out; the engine had refused the kept ones by slot +0x2F0 "
              + CpN(g_cpWhySlotMain) + ", by the first +0x3D8 entry " + CpN(g_cpWhyEntryMain) + ", by both " + CpN(g_cpWhyBothMain) + ", unread " + CpN(g_cpWhyUnreadMain) + ")");
}

void CampInstall()
{
    ::InitializeCriticalSection(&g_cpLock);
    ::InitializeCriticalSection(&g_cpPassMx);
    ::InitializeCriticalSection(&g_cnLock); ::InterlockedExchange(&g_cnLockReady, 1);   /* T-1429 camp note design */
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
    /* T-1429 A1: the camp keep - both rows are optional:campkeep; with either missing (or the furniture step unhooked) it stays off
       and the engine saves and lays camp pieces as itself (a saved set without the step's skip would be laid twice) */
    int keepOk = (debrisOk && orig_cpDebris != 0) ? 1 : 0;
    if (kCpNeedsSaveRva == 0 || kCpMakeRva == 0) { DebugLog(std::string("[CAMP] camp keep is off (row ") + (kCpNeedsSaveRva == 0 ? "BuildingNeedsSaving" : "CampPieceMake") + " not found)"); keepOk = 0; }
    else if (keepOk)
    {
        CpHook(kCpMakeRva, (void*)&detour_cpMake, (void**)&orig_cpMake, "the camp maker 0x92F250 (camp keep)", &keepOk);
        if (keepOk) CpHook(kCpNeedsSaveRva, (void*)&detour_cpNeedsSave, (void**)&orig_cpNeedsSave, "the buildings' needs-saving check 0x54F690 (camp keep)", &keepOk);
    }
    if (keepOk) { g_cpOffTls = ::TlsAlloc(); if (g_cpOffTls == TLS_OUT_OF_INDEXES) { DebugLog("[CAMP] camp keep is off (no thread slot for a step off the main thread - F1612 item 5)"); keepOk = 0; } }
    if (keepOk && orig_cpMake != 0 && orig_cpNeedsSave != 0) ::InterlockedExchange(&g_cpKeepOn, 1);
    DebugLog(std::string("[CAMP] camp keep is ") + (g_cpKeepOn ? "ON (camp pieces go into the area save; each furniture step is decided by its key, the area's camp note and the camp layer - the camp note design)" : "OFF (the engine saves and lays camp pieces as itself)"));
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
    g_cpGroundLater.clear();   /* T-1429 s15: their spots belong to the world that is going */
    g_cpRemoveQ.clear(); g_cpMatchBuilds.clear();   /* ... and so do the removals and the builds a match queued */
    g_clMark.clear(); g_clToWrite.clear(); g_clApplyDue.clear(); g_clSqIdx.clear(); g_clWaitTown.clear(); g_clWaitTownSaid.clear(); g_clWaitNest.clear(); g_ciGoneSaid.clear(); g_ciTownStepSeq.clear();
    g_rlDone.clear(); g_rlWait.clear(); g_rlLastSaid.clear(); g_rlRetryDue = 0; g_rlLines = 0; g_rlFaultTries.clear(); g_rlGaveUpSaid.clear(); g_rlWaitSq.clear();   /* F1659 N1 / S1: the retry bound and the square index are the world's */   /* F1654 NIT: the line cap starts again with each world */   /* T-1429 F1648: the re-link is the world's */   /* T-1429 camp note design: the camp pieces records and notes are the world's */
    g_ckKept.clear(); g_cpUnsaved.clear();   /* F1617: the kept steps and the unsaved pieces name the world that is going */
    if (g_cnLockReady != 0) { CnLocked n; g_cnNote.clear(); g_cnLayerWaiting.clear(); g_cnLayerAtLoad.clear(); g_cnReadSince.clear(); g_cnReadGen.clear(); }
    CpLocked l; g_cpDestroyedQ.clear(); g_cpDestroyedOver = 0;
}

void CampTick()
{
    if (!CpOnMain() || g_cpLockReady == 0) return;
    if (g_cpKeepOn != 0 && g_cnLockReady != 0)   /* F1612 item 3: the keep's part runs whenever the keep is on - with or without the camp list rule (g_cpReady) */
    {
        CuDrain();   /* F1627 item 3: the unloads the engine's unload hook counted */   /* T-1429 camp note design: an area read since the last tick - its record is applied once it is loaded here; a record that arrived waits likewise */
        std::set<std::string> rs;
        { CnLocked l; rs.swap(g_cnReadSince); }
        if (!rs.empty() && !g_clWaitNest.empty()) { g_clApplyDue.insert(g_clWaitNest.begin(), g_clWaitNest.end()); g_clWaitNest.clear(); }   /* F1753: an area file was read here - its buildings (a nest among them) came with it */
        for (std::set<std::string>::const_iterator z = rs.begin(); z != rs.end(); ++z)
        {
            int zx = -1, zy = -1;
            if (z->size() > 5 && std::sscanf(z->c_str() + 5, "%d.%d", &zx, &zy) == 2) { const std::string cid = camplaid::MakeCampLaidId(zx, zy); if (!cid.empty()) g_clApplyDue.insert(cid); }
            {   /* F1617 item 1 / NIT: the area was read again - steps kept from an earlier read end (their load is over), and so does an earlier
                   load's wait for a town (the apply runs again for this read) */
                const long gen = CnReadGenOf(*z);
                std::map<std::string, std::vector<CkStep> >::iterator kz = g_ckKept.find(*z);
                if (kz != g_ckKept.end())
                {
                    for (size_t i = 0; i < kz->second.size(); )
                    {
                        if (kz->second[i].readGen == gen) { ++i; continue; }
                        ++g_ckDropRead;
                        CpKeepSay("kept step key=" + kz->second[i].key + " area " + *z + " DROPPED - the area was read again (its load is over; this read's own steps decide) (dropRead=" + CpN(g_ckDropRead) + ")");
                        kz->second.erase(kz->second.begin() + i);
                    }
                    if (kz->second.empty()) g_ckKept.erase(kz);
                }
                const std::string cid = camplaid::MakeCampLaidId(zx, zy);
                g_rlDone.erase(*z); g_rlWait.erase(cid); RlForgetSaid(*z);   /* T-1429 F1648: a new read - its re-link runs again for this read */
                int ended = 0, endedNest = 0;
                for (std::set<std::string>::iterator w = g_clWaitTownSaid.begin(); w != g_clWaitTownSaid.end(); )
                {
                    if (cid.empty() || w->compare(0, cid.size() + 1, cid + "|") != 0) { ++w; continue; }
                    const size_t wl = w->size();   /* F1753 review fold NIT: '|nest' / '|unread' lines are nest waits, not town waits */
                    if ((wl > 5 && w->compare(wl - 5, 5, "|nest") == 0) || (wl > 7 && w->compare(wl - 7, 7, "|unread") == 0)) ++endedNest; else ++ended;
                    g_clWaitTownSaid.erase(w++);
                }
                if (endedNest != 0) CpKeepSay("pieces apply " + cid + ": " + CpN((long long)endedNest) + " entries waiting for their nest building (or its read) - tried again for this read (the area was read again; F1753)");
                if (ended != 0) { g_cfWaitEnded += ended; CpKeepSay("pieces apply " + cid + ": " + CpN((long long)ended) + " entries waiting for their town - the wait ENDS, the area was read again (its apply runs again for this read; waitEnded=" + CpN(g_cfWaitEnded) + ")"); }
            }
        }
        int sqApplied = 0;
        for (std::set<std::string>::iterator a = g_clApplyDue.begin(); a != g_clApplyDue.end(); )
        {
            int ax = -1, ay = -1;
            if (camplaid::CampLaidIdParse(*a, &ax, &ay) == 0) { g_clApplyDue.erase(a++); continue; }
            if (coop::ZoneLoadedSectorTri(ax, ay) != 1) { ++a; continue; }
            { CnLocked l; g_cnLayerWaiting.erase(CpZoneId(ax, ay)); }   /* the load took its answer */
            const std::string id = *a;
            int settled = 0;
            if (ClApplySquare(id, &settled) != 0) { ++a; break; }   /* F1612 item 8: one tick's share - the rest of this square next tick (one square a tick while it has more) */
            if (settled) sqApplied = 1;
            RlSquareLoaded(ax, ay, id);   /* T-1429 F1659 S1: keys of other areas waiting for a piece of this square (F1671: after this square's own pieces were made) */
            g_clApplyDue.erase(a++);
        }
        if (sqApplied && !g_clWaitNest.empty()) { g_clApplyDue.insert(g_clWaitNest.begin(), g_clWaitNest.end()); g_clWaitNest.clear(); }   /* F1753: as the town wait below */
        if (sqApplied && !g_clWaitTown.empty()) { g_clApplyDue.insert(g_clWaitTown.begin(), g_clWaitTown.end()); g_clWaitTown.clear(); }   /* T-1429 F1671: a square with records settled here (a record's own square may be loaded now) - squares waiting are tried at the next tick (a waiting square never sets it: no loop) */
        if (g_rlRetryDue) { g_rlRetryDue = 0; RlWaitRetry("a camp step of this game (its town is set up here)"); }   /* T-1429 F1648 */
        /* F1617 item 4: a 'not the holder' refusal's write goes again at the world server's HELD (CampAreaHeld) - no map polling, no timer */
        if (!g_ckKept.empty() && !coop::RoleIsSingle() && coop::StoreCampLinkUp())
        {   /* F1627 items 2 / 5: a loaded area's kept steps run once the live fact makes this game its layer (camplaid.h CampLayerAtStep): its
               area map names it the holder with this load's answer or HELD naming it, or (item 5) a holder's own load that asked nothing (no
               answer and no HELD comes for it). A holder whose answer named another game waits for its HELD (the record settles first). */
            std::vector<std::string> zs;
            for (std::map<std::string, std::vector<CkStep> >::const_iterator z = g_ckKept.begin(); z != g_ckKept.end(); ++z) zs.push_back(z->first);
            for (size_t i = 0; i < zs.size(); ++i)
            {
                int zx = -1, zy = -1;
                if (!CpZoneXY(zs[i], &zx, &zy) || coop::ZoneLoadedSectorTri(zx, zy) != 1) continue;
                int have = 0, layerLoad = camplaid::kLayerUnknown, asked = 0, after = 0;
                unsigned mapSeq = 0;
                const int holds = CpHoldsNow(zx, zy, &mapSeq);   /* F1630 item 1: with the arrival number of its map */
                { CnLocked l; const std::map<std::string, int>::const_iterator la = g_cnLayerAtLoad.find(zs[i]); if (la != g_cnLayerAtLoad.end()) { have = 1; layerLoad = la->second; } asked = CnAskedThisLoad(zs[i], zx, zy); after = CnMapAfterLoad(zs[i], zx, zy, mapSeq, 0); }
                if (!have || camplaid::CampLayerAtStep(layerLoad, holds, asked, after) != camplaid::kLayerYou) continue;
                ++g_cfTickRun;
                CkRunArea(zs[i], "the live area map naming this game the holder (F1627 items 2 / 5)", 0);
            }
        }
    }
    if (!g_cpGroundLater.empty())
    {   /* T-1429 s15: a camp BUILT here 30 s ago - its ground read again */
        const DWORD now = ::GetTickCount();
        for (size_t i = 0; i < g_cpGroundLater.size(); )
        {
            if ((LONG)(now - g_cpGroundLater[i].due) < 0) { ++i; continue; }
            const CpGroundLater gl = g_cpGroundLater[i];
            g_cpGroundLater.erase(g_cpGroundLater.begin() + i);
            CpGroundSay("camp '" + gl.key + "' ground within " + CpN((long long)kCpGroundR) + " of its list spot 30 s after its build: " + coop::GroundAroundSay(gl.x, gl.z, kCpGroundR, -1, -1, 12, 0));
        }
    }
    if (g_cpReady == 0) return;   /* F1612 item 3: the camp list rule's part below needs all its hooks */
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
            RlWaitRetry("this game's camps named by their list keys (the seed / match)");   /* T-1429 F1648 */
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
    int builtAny = 0;
    for (size_t i = 0; i < done.size(); ++i)
    {
        const CpDone& d = done[i];
        if (d.outcome == campg::kBuiltOk)
        {
            ++g_cpBuilt; builtAny = 1;
            CpRegSet(d.rec.key, d.town); g_cpBuildTries.erase(d.rec.key);
            /* the list's spot against where the engine put the camp (the same on every game when the build takes the spot as given) */
            std::ostringstream o; o.setf(std::ios::fixed); o.precision(2);
            o << "camp '" << d.rec.key << "' BUILT by the engine's camp pass: list spot " << d.rec.x << "," << d.rec.y << "," << d.rec.z;
            if (d.posOk) { const float dx = d.x - d.rec.x, dy = d.y - d.rec.y, dz = d.z - d.rec.z; o << ", built at " << d.x << "," << d.y << "," << d.z << " (off by " << std::sqrt(dx * dx + dy * dy + dz * dz) << ")"; }
            else o << ", its position after the build did not read";
            CpLog(o.str());
            /* T-1429 s15 (F1369: a nest built on the joiner made none of the host's eggs): the ground around the list spot before and after
               the build, and again 30 s later (CampTick) */
            if (g_cpGroundLogBudget > 0)
            {
                CpGroundSay("camp '" + d.rec.key + "' ground within " + CpN((long long)kCpGroundR) + " of its list spot - before the build: " + (d.groundBefore.empty() ? std::string("not read") : d.groundBefore)
                            + "; after: " + coop::GroundAroundSay(d.rec.x, d.rec.z, kCpGroundR, -1, -1, 12, 0));
                if (g_cpGroundLater.size() < 32) { CpGroundLater gl; gl.key = d.rec.key; gl.x = d.rec.x; gl.z = d.rec.z; gl.due = ::GetTickCount() + 30000UL; g_cpGroundLater.push_back(gl); }
            }
            else ++g_cpGroundLinesCut;
            continue;
        }
        ++g_cpNotBuilt;
        g_cpBuiltHere.erase(d.rec.key);
        const int tries = ++g_cpBuildTries[d.rec.key];
        CpLog("camp '" + d.rec.key + "' NOT BUILT: " + (d.outcome == campg::kBuiltRegionNotReached ? "the engine's pass did not reach its region (it holds its limit here, or it is not in the engine's table)" : "the engine refused the build")
              + " - NOT_BUILT sent" + (campg::CampRetryBuild(tries) ? " (tried again at the next match)" : " (not tried again this world load)"));
        CpSendNotBuilt(done[i].rec.key, campl::kNbEngineRefused);
    }
    if (builtAny) RlWaitRetry("this game's camp town built (the camp pass)");   /* T-1429 F1648: the town-built event */
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
           + " campLines[stepCut,groundCut]=" + CpN(g_cpStepLinesCut) + "," + CpN(g_cpGroundLinesCut)
           + " campKeep[on,saved,engineSaved,destroyedLeft,stepsSkipped,stepsRan,stepsUnread,makesZeroed,linesCut]=" + CpN((long long)g_cpKeepOn) + "," + CpN((long long)g_cpSaveYes) + "," + CpN((long long)g_cpSaveEngineYes)
             + "," + CpN((long long)g_cpSaveDestroyed) + "," + CpN(g_cpStepsSkipped) + "," + CpN(g_cpStepsRan) + "," + CpN(g_cpStepsUnread) + "," + CpN(g_cpMakesZeroed) + "," + CpN(g_cpKeepLinesCut)
           + " campKeepWhy[slot,entry,both,unread]=" + CpN((long long)g_cpWhySlot) + "," + CpN((long long)g_cpWhyEntry) + "," + CpN((long long)g_cpWhyBoth) + "," + CpN((long long)g_cpWhyUnread)
           + " campNote[noteHad,laid,owed,noKey,reads,readsFound,readsOff,readBad,saved,saveFail,over,layerYou,layerOther,layerNone]=" + CpN(g_clNoteHad) + "," + CpN(g_clLaid) + "," + CpN(g_clOwed)
             + "," + CpN(g_clNoKey) + "," + CpN(g_cnReads) + "," + CpN(g_cnReadsFound) + "," + CpN(g_cnReadsOff) + "," + CpN(g_cnReadBad) + "," + CpN(g_cnSaved) + "," + CpN(g_cnSaveFail) + "," + CpN(g_cnOver)
             + "," + CpN(g_cnLayerYou) + "," + CpN(g_cnLayerOther) + "," + CpN(g_cnLayerNone)
           + " campPieces[writes,taken,refused,arrived,bad,notSent,dropped,capped,pieceUnread,applyEntries,applyMade,applyFail,applySkipped]=" + CpN(g_clMarkWrites) + "," + CpN(g_clMarkTaken) + "," + CpN(g_clMarkRefused)
             + "," + CpN(g_clMarkArrived) + "," + CpN(g_clMarkBad) + "," + CpN(g_clMarkNotSent) + "," + CpN(g_clToWriteDropped) + "," + CpN(g_clCapped) + "," + CpN(g_clPieceUnread)
             + "," + CpN(g_clApplyEntries) + "," + CpN(g_clApplyMade) + "," + CpN(g_clApplyFail) + "," + CpN(g_clApplySkipped)
           + " campFold[lateLayer,selfLayer,askCleared,noAnswerOwed,waitTown,outOfBound,inside,nestMissing,unreadZeroed,offMainZeroed,held]=" + CpN(g_cfLateLayer) + "," + CpN(g_cfSelfLayer) + "," + CpN(g_cfAskCleared)
             + "," + CpN(g_cfNoAnswerOwed) + "," + CpN(g_cfWaitTown) + "," + CpN(g_cfOutOfBound) + "," + CpN(g_cfInside) + "," + CpN(g_cfNestMissing) + "," + CpN(g_cfUnreadZeroed) + "," + CpN((long long)g_cfOffMainZeroed) + "," + CpN(g_cfHeld)
           + " campRecheck[kept,run,dropKey,dropGone,dropRead,heldIn,unbuilt,unbuiltPieces,dup,waitEnded,keptNow]=" + CpN(g_ckKeptN) + "," + CpN(g_ckRun) + "," + CpN(g_ckDropKey) + "," + CpN(g_ckDropGone) + "," + CpN(g_ckDropRead)
             + "," + CpN(g_ckHeldIn) + "," + CpN(g_cfUnbuilt) + "," + CpN(g_cfUnbuiltPieces) + "," + CpN(g_cfDup) + "," + CpN(g_cfWaitEnded) + "," + CpN((long long)g_ckKept.size())
           + " campF1622[dropUnload,unsavedUnload,unsavedStale,readNowLayer,writePiped]=" + CpN(g_ckDropUnload) + "," + CpN(g_cfUnsavedUnload) + "," + CpN(g_cfUnsavedStale) + "," + CpN(g_cfReadNowLayer) + "," + CpN(g_cfWritePiped)
           + " campF1627[selfLay,holdKept,tickRun,notHolderKept,outDropped,unloadsTaken,unloadUnnamed]=" + CpN(g_cfStepSelfLay) + "," + CpN(g_cfStepHoldKept) + "," + CpN(g_cfTickRun) + "," + CpN(g_cfNotHolderKept)
             + "," + CpN(g_cfOutDropped) + "," + CpN(g_cuApplied) + "," + CpN((long long)g_cuUnnamed)
           + " campF1630[mapBeforeRead]=" + CpN(g_cfMapBeforeRead)
           + " campF1634[loadStarts,markStart,markUnload,heldNoRead]=" + CpN(g_cfLoadStarts) + "," + CpN(g_cfMarkStart) + "," + CpN(g_cfMarkUnload) + "," + CpN(g_cfHeldNoRead)
           + " campF1638[markNever]=" + CpN(g_cfMarkNever) + " " + RlCounts()
           + " campAllTown[found,notOnCampList,twins,none,fault,overCap,linesCut]=" + CpN(g_caFound) + "," + CpN(g_caNotCamp) + "," + CpN(g_caTwins) + "," + CpN(g_caNone)
             + "," + CpN(g_caFault) + "," + CpN(g_caOverCap) + "," + CpN(g_caLinesCut)
           + " campSquare[applied,noted,made,madeAway,already,failed,skipNote,waitOwn]=" + CpN(g_csApplied) + "," + CpN(g_csNoted) + "," + CpN(g_csMade) + "," + CpN(g_csMadeAway) + "," + CpN(g_csDup)
             + "," + CpN(g_csFail) + "," + CpN(g_csSkipNote) + "," + CpN(g_csWaitOwn)
           + CiCountsToken()
           + coop::CtxCampHomeToken()
           + "]";
}

}   // namespace coop
