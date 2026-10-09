// P4 - town squad generation (decision 25; F466/F500; docs/persistence-service.md §17)
#include "towngen.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "zones.h"          // SectorOf, HostHoldsSectorTS, ZonesInitLocks
#include "net/session.h"    // SessionIsHost, SessionLinked
#include "handoff.h"        /* T-1 B1: CopySquadKeepAwake - owner 110, from the 0x6BA810 pre-hook */
#include "store.h"          // StoreRecordCreated (P4e)
#include "config.h"         /* E38 (e) / decision 43: RoleIsSingle - a lone game has no notebook and refuses nothing */
#include "ai_spike.h"       /* GetTarget: this game's player character (the TEST-ONLY `townfill force` lever) */
#include "../common/recruitmult.h"   /* recruit3: the recruitmult values, the multiplier and the hire-list test - the SAME header the notebook and the offline suite compile */
#include "coop_log.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "game/RootObjectBase.h"
#include <Windows.h>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <intrin.h>   // _ReturnAddress: the caller of each creation (first-only per RVA)
#include "game/Character.h"   /* refill1: the character update list walk (as items.cpp captlist / spawn.cpp roster full) */
#include "game/GameWorld.h"   /* refill1 */
#include "playerfaction.h"      /* refill1: the nearby rule's player test (this game's own and every other player's stand-in faction) */
#include "../common/townrebuild.h"   /* towns2: the game's marker rule, the 5-day timer, the version wait, the sight test and the group verdict - the SAME header the offline suite compiles */
#include "worldstate.h"                /* towns2: WorldStateGeneration - a named character's state changed */
#include "game/hand.h"   /* towns2: hand::getSquad / hand::asBuilding */
#include "../common/townpending.h"   /* T-580: which notebook notes hold a town's creation back - the SAME header the store and the offline suite compile */
#include "../common/townreoffer.h"   /* T-392 (owner 335 a): the set-aside causes and the re-offer decision - the SAME header the offline suite compiles */
#include "../common/owedpop.h"   /* T-581: the owed town populations kept on the world server - the SAME header the world server and the offline suite compile */
#include "../common/barwire.h"  /* refill1: the shared bar record, the due decision and the per-entry counts - the SAME header the notebook and the offline suite compile */
#include "../common/townrefill.h"   /* each town's recorded losses, the record tally and the one-refill decision - the SAME header the world server and the offline suite compile */
#include "../common/emptytown.h"   /* the unexplained-empty-town facts, verdict, once-per-town rule and line - the SAME header the offline suite compiles */
#include "../common/areadrop.h"    /* the area map's frozen answer (kAreaFrozen), told apart from "not held" */

namespace coop {
/* T-392 (owner 335 a): items.cpp's position key and its resolver (items.h declares the same three) - a building set aside
   is kept by its key and found again by it, never by pointer. */
int  ObjectPositionKey(void* obj, char* out, int cap, const char* suffix, int countIt, int podPos);
void* ObjectByPositionKey(const char* key);
int  ObjectByPositionKeyVerdict();
int  BoxKeyCap();   /* T-392 fold (review H1): items.cpp's kBoxKeyCap - the resolver's own key cap */
int  PlatoonSetHomeBuildingPod(void* platoon, void* building);   /* spawn.cpp: Ownerships::setHomeBuilding for a group, guarded - 1 called, 0 no address or a fault */
int  BuildRowPendingFor(void* building);   /* build.cpp: another game's PLACE or STATE for this piece still waits here - 1 yes, 0 no, -1 key unreadable */
namespace {

unsigned long long kCreateRandomUnloadedSquadRva = 0; static coop::AddrReg kCreateRandomUnloadedSquadRva_reg("CreateRandomUnloadedSquad", &kCreateRandomUnloadedSquadRva);   /* P8h: the address table fills this. Steam_1.0.65 0x57EA30 */   // RootObjectFactory::createRandomUnloadedSquad(Faction*, Vector3, ..., TownBase* home (param 4), ..., SquadType (param 13))

// The engine's 13-parameter signature, kept opaque: the first four ride in registers, the rest on the stack, all 8-byte slots.
typedef void* (*CreateRandomUnloadedSquadFn)(void* self, void* faction, void* pos, void* town, unsigned long long p5, void* p6, void* p7,
                                             unsigned long long p8, unsigned long long p9, unsigned char p10, void* p11, unsigned long long p12, unsigned int squadType);
CreateRandomUnloadedSquadFn orig_create = 0;
/* T-515: the step createRandomUnloadedSquad takes first for a squad made for a building (decomp_57ea30:34, then decomp_57e760): from
   the squad template it writes the building's "building designation" (Building+0xC4, only while 0), "public day" (+0xC8) and
   "building ruined" (setDestroyed through vt+0x340 - a ruin destroys every door), and adds one entry per "nest" reference to the
   town's nest list (0x928470 on Town vt+0x268 = the town itself; it appends, so a second call adds the entry twice). */
unsigned long long kBuildingSquadStateRva = 0; static coop::AddrReg kBuildingSquadStateRva_reg("BuildingSquadState", &kBuildingSquadStateRva);   /* Steam_1.0.65 0x57E760 */
typedef void (*BuildingSquadStateFn)(void* building, void* squadTemplate);
BuildingSquadStateFn orig_bldState = 0;
long long g_t515Applied = 0, g_t515Fault = 0, g_t515NoTemplate = 0, g_t515NoHook = 0, g_t515RerunSkipped = 0, g_t515RerunRan = 0;
long long g_t515RefusedApplied = 0;   /* of g_t515Applied: steps taken for a building squad refused by another residents road */
volatile LONG g_t515Lines = 0, g_t515ChangeLines = 0;
const LONG kT515LineCap = 24;          /* "[TOWN] building state" lines for steps that changed neither ruin nor doors, per game process */
const LONG kT515ChangeLineCap = 200;   /* lines for steps that changed the ruin flag or the door count, per game process */
void* volatile g_t515RerunBld = 0;   /* MAIN THREAD: the building the re-offer's populateBuilding is running for, 0 outside it */
volatile long g_t515RerunAtLoad = 0;   /* that building's entry stateAtLoad */
long long g_createdSeen = 0, g_createdRecorded = 0;
static void T580NoteMade(void* p);   /* T-580: defined beside T392PopSector */
static void T581NoteReached();   /* T-581: defined beside T580NoteMade */
static void* CreatedTown(void* p, void* town) { (void)town; if (p != 0) ::InterlockedIncrement64(&g_createdSeen); T580NoteMade(p); T581NoteReached(); return p; }   /* F507/decision 29: a group created on ice has no people yet - nothing useful to write down; it reaches the notebook at its first sleep after the host thaws it */
unsigned long long kSpawnTheBarFliesRva = 0; static coop::AddrReg kSpawnTheBarFliesRva_reg("SpawnTheBarFlies", &kSpawnTheBarFliesRva);   /* P8h: the address table fills this. Steam_1.0.65 0x9FBE50 */   // void Town::spawnTheBarFlies() - bar residents via createRandomSquad 0x582F80 (F500's other creation path)
typedef void (*SpawnTheBarFliesFn)(void* town);
SpawnTheBarFliesFn orig_barFlies = 0;
long long g_barFliesSeen = 0, g_barFliesRefused = 0, g_barFliesRefusedUnlinked = 0, g_barFliesAllowed = 0, g_barFliesDeferred = 0, g_barFliesNoTown = 0, g_barFliesLeverOff = 0, g_barFliesOff = 0, g_barFliesDrainFault = 0, g_barFliesDropped = 0, g_barFliesReadFault = 0, g_barFliesDrainOdd = 0;
// decomp_9fbe50: the function ends with *(int*)(Town+0x320) = 0 (param_1 is longlong*, +100 = +0x320) - it consumes its entry list
// (0x98-byte entries at +0x328, read-only in the function; the engine leaves the array allocated with the count zeroed - review-p4f HIGH-2).
// A refusal drains the count the same way: the engine's own end state, nothing leaked. The entries are DESTROYED, not deferred (no other reader).
static int DrainBarFlyListPod(void* town, int* dropped) { __try { const int n = *(int*)((char*)town + 0x320); *(int*)((char*)town + 0x320) = 0; *dropped = (n < 0 || n > 4096) ? 0 : n; return (n < 0 || n > 4096) ? 2 : 1; }   /* review-p4j: a readable count is always zeroed (a refused list must not survive); an implausible one is counted as 0 and flagged 2 */ __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }

typedef void* (*GetPositionFn)(void* obj, float* out);   // TownBase (RootObjectBase) vtbl+0x40: Vector3 out (zones.cpp, F485)

bool g_on = true;
/* F665 (review-p8a H-1) - THIS IS THE NOTEBOOK LINK, NOT THE PEER LINK, AND THE DIFFERENCE WAS A
   REGRESSION FOR REAL PLAY.  P8a's rule is "while there is no notebook link, NOBODY invents" and the
   code tested net::SessionLinked() - the GAME-to-GAME transport, which is LINK_UP only when a PEER has
   connected.  So the ordinary opening of a live session (one person, the notebook up, no friend yet)
   refused every town-carrying creation this game held, and the same latch fired again when a peer
   left.  The load gate P8a's own justification leans on asks the notebook link, and so does this now:
   StoreWelcomedThisLink() && StoreRelayLinked(), which is LinkUp()'s own main-thread-refreshed cache
   (store.cpp) and therefore safe to read from the worker threads these gates run on.  The cause string
   says `no-notebook` for the same reason - `pre-link` named the wrong link. */
volatile long g_linkedCached = 0;   // review-p4b MEDIUM-1: refreshed on the tick; workers read this rather than dereferencing a link object the main thread deletes
/* DECISION 48 (P8a), THE THREE NUMBERS THAT ANSWER IT, each spanning BOTH creation gates (town squads and bar
   residents) so "did the holder generate, and who refused what" is three numbers and not six role-shaped ones.
   g_hostCached and g_clientMode are GONE with the roles they served (build/read-host-audit.md C1/C3). */
long long g_holderGenerated = 0, g_refusedNotHolder = 0, g_refusedNoMap = 0;
/* P97 (p97-towngen): towngenFallbackLowerSlot is RETIRED - it had no writer since T-392 (owner 337 a) - so a
   readout writes ABSENT for it and not zero. */
/* P8e-b (review-p8e C-2) - THE RE-OFFER PASS IS GONE; THIS TABLE ONLY COUNTS NOW.
   F666 added a 1 Hz pass that re-offered a DEFERRED bar list by calling detour_barFlies on a Town*
   cached for up to 32 s, which reached Town::spawnTheBarFlies 0x9FBE50 with no SEH around it, on the
   MAIN thread, with no test that the town's zone was the one being activated.  The engine's own and
   only caller (ZoneMapContent::_activate -> populateAllTheBuildings, on the loadPhase2 thread)
   resolves the Town through a HANDLE every pass and calls only for the zone it is activating.
   TOWN LIFETIME IS THE UNKNOWN (review-p4x: whether Town objects are ever destroyed is not
   established), and a main-thread call through a cached pointer cannot be made safe by SEH - a
   __try around a call into an object that has been freed protects the access violation, not the
   half-executed engine work behind it.  So the re-offer is left to the engine's own zone
   re-activation, which is where it came from, and the DEFERRALS ARE COUNTED instead: the next run
   measures how often a deferral actually had to wait, which is what decides whether this is worth
   any further engineering at all.
   NOTHING IN THIS TABLE IS EVER DEREFERENCED.  A Town* is stored and compared as an identity token
   and no read, virtual call or engine call is made through it; it is cleared at every world teardown
   for the same reason it always was - a pointer belongs to the world that made it, and a recycled
   address must not be counted as the same town. */
/* T-392 fold (owner 317 a, 2026-10-01): the table GROWS as needed and never refuses. It was 64 fixed rows, and one overflow
   made BarPendOwes answer "owed" for every town for the rest of the world. Town* (an identity token, as above) -> tries,
   under g_tgLock (the bar gate may run on any thread). */
std::map<void*, long> g_barPend;
long long g_barPendOverflow = 0;   /* T-392 fold: nothing overflows any more - it stays 0, kept so a readout does not take an absent field for a zero (F172) */
/* THE DEFERRAL MEASUREMENT.  `towns` is how many distinct towns deferred at least once, `repeat` how
   many deferrals landed on a town that was already waiting, and `answered` how many waiting towns a
   LATER pass - the engine's own, since nothing else calls the gate now - went on to answer.  A run
   with towns > 0 and answered == 0 is the case the deleted re-offer existed for, and it would be the
   evidence for building a safe one; the three are separate because they are three different events
   (6a lesson 1). */
long long g_barDeferredTowns = 0, g_barDeferredRepeat = 0, g_barDeferredAnswered = 0;
/* P8a (the roster's spawnCause): the group ids THIS game's town-generation hook allowed, matched by STRING
   because the returned pointer is not established to be the object ActivePlatoon+0x78 names and a recycled
   address would answer for the wrong character.  Bounded, and cleared at every world teardown beside the keys. */
const size_t kTownGenIdCap = 4096;
std::set<std::string> g_tgCreatedIds; long long g_tgIdOverflow = 0;
/* PROBE-START: P085 - allowed town creations per sector (x*64+y), all types and type 2 (residents); cleared at teardown */
volatile LONG g_p085Squads[4096]; volatile LONG g_p085Residents[4096];
/* PROBE-END: P085 */
long long g_refusedOff = 0, g_allowedOff = 0, g_noLeaseOff = 0, g_readFaultOff = 0, g_refusedUnlinkedOff = 0, g_refusedNoLease = 0, g_refusedNoLeaseOff = 0;   // review-p4b HIGH-2: the off-thread share of each lease bucket
long long g_seen = 0, g_refused = 0, g_refusedUnlinked = 0, g_allowed = 0, g_noTown = 0, g_noLease = 0, g_readFault = 0, g_offThread = 0, g_unlinkedAllowed = 0, g_leverOff = 0;
CRITICAL_SECTION g_tgLock; bool g_tgLockInit = false;
unsigned long long g_callerRvas[16]; int g_callerCount = 0; long long g_callerOverflow = 0;   /* review-p4h/p4j: CALLS whose key was absent after the table filled (calls, not distinct keys) */   // first-only per (caller RVA, town != 0, thread) - review-p4b MEDIUM-2
void TgLockInit() { if (!g_tgLockInit) { ::InitializeCriticalSection(&g_tgLock); g_tgLockInit = true; } }
static std::string JsonEsc(const std::string& s) { std::string o; o.reserve(s.size() + 4); for (size_t i = 0; i < s.size(); ++i) { const char c = s[i]; if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; } return o; }   /* review-p4p M: verdict.py drops malformed JSON silently */
std::set<std::string> g_allowedTowns;   /* P4p: towns whose first ALLOWED creation was recorded (with the new group's id) */
long long g_refusedPending = 0, g_barFliesDeferredPending = 0;   /* decision 34 */
// PROBE-START: P025
/* P025 (F527/E13): DebugLog is not thread-safe and this detour runs on worker threads, so the probe is latched the way the
   refusal log beside it already is - a bounded number of lines, claimed with an interlocked increment. */
volatile LONG g_p025Refused = 0, g_p025Allowed = 0;
// PROBE-END: P025
std::set<std::string> g_refusedTowns;   // the refusal KEYS (town x cause: '', ' (stale lease)', ' (pre-link)', ' (bar residents)'), first-only log per key per world
std::map<std::string, long long> g_refusedByTown;   // per-town refusal counts (the report), under g_tgLock
/* T-580: the notebook-pending refusals are logged at the first and then at the first after every 5 minutes, per key (townpending::LogDue);
   towns the notebook lists only from areas that do not hold them are counted per town (pendingElsewhere[...] in the report). Under g_tgLock. */
std::map<std::string, DWORD> g_pendingLogAt; std::map<std::string, long long> g_elsewhereByTown; long long g_pendingElsewhere = 0;
// P6j (verify-p6c MEDIUM-2): the teardown refusals, at both gates. Both are SPANS inside the refusal counters they
// sit in (g_refused and g_barFliesRefused), not new buckets, so the [TG] identity below is unchanged - they say how
// many of those refusals were the engine freeing the world rather than an answer about the area.
long long g_refusedTeardown = 0, g_barFliesTeardown = 0;
// PROBE-START: P027
/* P027 (verify-p6c HIGH-2): the bar-fly refusal's inputs. The budget is per TOWN and it is NOT cleared by
   TownGenWorldTeardown, unlike the refusal keys beside it - eight lines about one town is a measurement of that
   town's arrival window, and a world reload must not silently hand it another eight. */
std::map<std::string, long long> g_p027ByTown;
// PROBE-END: P027
DWORD g_mainThread = 0; bool g_mainThreadFromTick = false;   // review-p4a HIGH-2: set on the first TownGenTick (the game's main loop), not at preload

std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return b; }

int TownPosPod(void* town, float* pos)
{
    __try { void** vt = *(void***)town; float v[3] = { 0, 0, 0 }; ((GetPositionFn)vt[8])(town, v); if (v[0] == 0.0f && v[2] == 0.0f) return 0; pos[0] = v[0]; pos[1] = v[1]; pos[2] = v[2]; return 1; }   // (0,0) = no write (review-p4a MEDIUM)
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SpawnPosPod(const void* pos, float* out);   /* T-580: defined below, beside the T-392 counters */
static int T392PopSector(int* sx, int* sy);   /* T-580: defined below */
/* T-580: A RESIDENTS RE-RUN'S OWN SLOT - its thread (raised for the one synchronous engine call, townpending::InRun), its building's
   area (the fallback the re-offer asked with) and what that call made / had refused notebook-pending. Only the re-run's own
   thread writes the counts, so no other building fill can count for it; the shared building-context slot is not used. */
volatile DWORD g_t580RunTid = 0; int g_t580RunSx = -1, g_t580RunSy = -1; long long g_t580RunMade = 0, g_t580RunPending = 0;
/* T-580 - DECISION 34'S QUESTION FOR ONE TOWN (ANY THREAD). The area asked for (townpending::AskArea): the town's own (its centre,
   TownPosPod); when that will not read, the building context's area on this thread (a re-run's building, else T392PopSector); then
   the creation's spawn position (`pos`, 0 for a bar roll); else -1, and every listed note holds the town (townpending::Holds).
   Returns townpending's answer; a town listed only from areas that do not hold it is counted per town and logged once per town -
   its creation goes on to the area gates. */
static int TownPendingAsk(void* town, const void* pos, const char* sid, char* holder, int holderCap)
{
    float tp[3] = { 0, 0, 0 }; int ax = -1, ay = -1, tSx = -1, tSy = -1, cSx = -1, cSy = -1, sSx = -1, sSy = -1, cOk = 0, sOk = 0;
    const int tOk = TownPosPod(town, tp) != 0 ? 1 : 0;
    if (tOk != 0) { const Sector s = SectorOf(tp[0], tp[2]); tSx = s.x; tSy = s.y; }
    else
    {
        if (townpending::InRun(g_t580RunTid, ::GetCurrentThreadId()) != 0) { cOk = 1; cSx = g_t580RunSx; cSy = g_t580RunSy; }
        else cOk = T392PopSector(&cSx, &cSy);
        if (cOk == 0 && pos != 0 && SpawnPosPod(pos, tp) != 0) { const Sector s = SectorOf(tp[0], tp[2]); sOk = 1; sSx = s.x; sSy = s.y; }
    }
    townpending::AskArea(tOk, tSx, tSy, cOk, cSx, cSy, sOk, sSx, sSy, &ax, &ay);
    const int a = StoreTownPeoplePending(sid, ax, ay, holder, holderCap);
    if (a == townpending::kTownElsewhere)
    {
        ::InterlockedIncrement64(&g_pendingElsewhere);
        bool first = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); first = g_elsewhereByTown.find(sid) == g_elsewhereByTown.end(); ++g_elsewhereByTown[sid]; ::LeaveCriticalSection(&g_tgLock);
        if (first) DebugLog(std::string("[TG] town '") + sid + "': the notebook lists groups of this town that this game has not placed, none in an area loaded here or in or beside the town's area - decision 34 does not hold its creations back; they go on to the area gates (first only per town; pendingElsewhere[...] in the report counts them)");
    }
    return a;
}
/* T-580: books one notebook-pending refusal under `key`; true = log it now (the first, then the first after every 5 minutes). */
static bool PendingRefusalBook(const std::string& key, bool* firstOut, long long* countOut)
{
    const DWORD now = ::GetTickCount(); bool due = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    *firstOut = g_refusedTowns.insert(key).second; *countOut = ++g_refusedByTown[key];
    { const std::map<std::string, DWORD>::iterator lt = g_pendingLogAt.find(key);
      due = townpending::LogDue(lt != g_pendingLogAt.end() ? 1 : 0, lt != g_pendingLogAt.end() ? (unsigned)lt->second : 0u, (unsigned)now) != 0;
      if (due) g_pendingLogAt[key] = now; }
    ::LeaveCriticalSection(&g_tgLock);
    return due;
}
/* T-392 (owner decisions 2026-10-01: 335 a, 336 a, 337 a) - NO TOWN STAYS EMPTY. The counters of the whole change.
   sectorFromSpawn / sectorFromTown: which position judged a town creation (336 a: where the squad APPEARS; the town's own
   position only when the spawn position will not read). Interlocked where a worker can reach them. */
long long g_t392SecSpawn = 0, g_t392SecTown = 0;
long long g_t392DeferBar = 0, g_t392DeferRes = 0, g_t392ReGen = 0, g_t392ReOther = 0, g_t392ReFilled = 0, g_t392ReGone = 0, g_t392ReFault = 0;
long long g_t392ResSeen = 0, g_t392ResNoTown = 0, g_t392ResNoKey = 0, g_t392SecBuilding = 0, g_t392ReHasRes = 0;
/* T-392 fold (review LOW): INSIDE a populateBuilding call the residents gate let through, the squads it creates belong to the
   BUILDING, so they are judged by the sector the gate judged the building by - not by their own spawn points, or a building
   at a sector border would be used up with nothing created. Set by the gate on the thread it answered on; cleared by
   TownGenResidentsDone when the original returns (worldgen.cpp, __finally). */
volatile DWORD g_t392PopTid = 0; volatile long g_t392PopSx = -1, g_t392PopSy = -1;
/* T-438 (t438-towngen): the gate's "squads set aside" flag - raised with the building-sector context when the gate SETS ASIDE a
   building's residents, because populateBuilding still runs (interior/furniture, owner faction); lowered by TownGenResidentsDone.
   Read only on the thread the context names. The rest feeds the once-per-building line Done writes. */
volatile long g_t392PopAside = 0, g_t392PopAsideN = 0, g_t392PopCnt0 = 0, g_t392PopCnt0Ok = 0;
void* volatile g_t392PopBld = 0; void* volatile g_t392PopTown = 0;
/* the residents gate's area answer (MayInventFromView) for the building its context names - 1 when the gate let the
   building through to be made here; read with that context on the same thread (T591PendingAside), lowered by TownGenResidentsDone.
   pendAside = buildings set aside when one of their squads was refused notebook-pending; pendAsideNoKey = such refusals whose
   building would not read (a plain refusal, as before). */
volatile long g_t591GateMay = 0; volatile LONG g_t591Lines = 0;
long long g_t591PendAside = 0, g_t591PendAsideNoKey = 0;
/* re-offer waits on a holding note; buildings answered "has residents" by a group of their template at the
   building, and at-the-building reads that did not complete. g_t591WaitLines: the named wait lines (first 20) */
long long g_t591WaitOnNote = 0, g_t591HomeGivenAtCheck = 0, g_t591KeylessWaits = 0; volatile LONG g_t591WaitLines = 0;
char g_t392PopSid[128] = { 0 }; char g_t392PopKey[128] = { 0 };
long long g_t392SquadsAside = 0, g_t392AsideRuns = 0, g_t392AsidePopAdd = 0, g_t392RerunPopAdd = 0;
long long g_t392RerunPopUndone = 0, g_t392RerunPopUndoneSum = 0, g_t392RerunPopMismatch = 0, g_t392RerunPopUnread = 0;   /* t438-b-towngen: the generate re-run's own budget add, undone */
std::set<std::string> g_t392AsideLogged;   /* building keys: the once-per-building furniture line; cleared at teardown */
static int T392PopSector(int* sx, int* sy)
{
    if (g_t392PopTid == 0 || g_t392PopTid != ::GetCurrentThreadId()) return 0;
    *sx = (int)g_t392PopSx; *sy = (int)g_t392PopSy;
    return 1;
}
/* T-580: re-runs kept waiting on townpending::RerunKeep, re-runs given up at the bound (KeptGiveUp), kept re-runs per building key
   this session (under g_tgLock). A squad made on a re-run's own thread counts for it (T580NoteMade). */
long long g_t580RerunKept = 0, g_t580RerunGivenUp = 0; std::map<std::string, int> g_t580KeptByKey;
static void T580NoteMade(void* p) { if (p != 0 && townpending::InRun(g_t580RunTid, ::GetCurrentThreadId()) != 0) ++g_t580RunMade; }
/* T-581: on the re-run's own thread, every creation the re-run asked for and every one that reached the engine - asked - reached =
   refused by any creation gate, any cause (owedpop::AfterRerun) */
long long g_t581RunAsked = 0, g_t581RunReached = 0;
static void T581NoteReached() { if (townpending::InRun(g_t580RunTid, ::GetCurrentThreadId()) != 0) ++g_t581RunReached; }
/* T-392 (owner 336 a): ANY THREAD. createRandomUnloadedSquad's own spawn position - its third argument, an Ogre::Vector3 by
   pointer (populateBuilding passes the building's spawn point, decomp_57ed90:83-95, 166, 195; the world-squad path 0x8F7190 its own).
   1 = read and plausible; 0 = absent, faulted, not finite, or (0,0) - the caller falls back to the town's position. */
int SpawnPosPod(const void* pos, float* out)
{
    if (pos == 0) return 0;
    __try
    {
        const float* v = (const float*)pos;
        const float x = v[0], y = v[1], z = v[2];
        if (!(x > -1.0e7f && x < 1.0e7f) || !(y > -1.0e7f && y < 1.0e7f) || !(z > -1.0e7f && z < 1.0e7f)) return 0;   /* NaN and infinity fail these too */
        if (x == 0.0f && z == 0.0f) return 0;
        out[0] = x; out[1] = y; out[2] = z;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int TownSidPod(void* town, char* out, int cap)
{
    __try
    {
        void* gd = ((RootObjectBase*)town)->getRecord();
        if (gd == 0) return 0;
        const char* s = (const char*)gd + 0x58;   // GameData::stringID (a std::string; VS2010 layout: buffer/pointer at +0, size at +0x10, capacity at +0x18)
        const size_t size = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
        if (size >= (size_t)cap) return 0;
        const char* p = (res >= 16) ? *(const char* const*)s : s;   // review-p4a HIGH-3: heap iff capacity >= 16 (a shortened long string keeps its heap buffer)
        if (p == 0) return 0;
        std::memcpy(out, p, size); out[size] = 0; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* T-581: THE OWED TABLE AS THIS GAME SEES IT (src/common/owedpop.h). g_owed: the world server's rows - a WELCOME empties it, ROWS
   fill and change it, GONE removes a row; each row's claimant is owedpop::kClaim* as seen here. g_owedBars: the bar rolls THIS game
   set aside in this world, by town stringID, with the link generation their ADD went out on (0 = not sent; -1 = not sendable).
   g_owedBarSettled: towns whose owed bar roll the world server removed (made or seen elsewhere) - this game's own list then settles
   as the gate settles it. g_owedDoneQ: DONEs decided off the main thread (the bar roll's own thread) or not sendable yet, sent by
   the main thread. All under g_tgLock. */
struct OwedBarLocal { float x, z; long sentGen; int acked; OwedBarLocal() : x(0.0f), z(0.0f), sentGen(0), acked(0) {} };   /* acked = its row was seen in the world server's table */
struct OwedDone { unsigned kind; std::string key; unsigned why; };
owedpop::Table g_owed;
std::map<std::string, OwedBarLocal> g_owedBars;
std::set<std::string> g_owedBarSettled;
std::vector<OwedDone> g_owedDoneQ;
owedpop::SettleSet g_owedSettle;   /* rows whose people this game made and no finished save of its own holds yet (owedpop::SettleNote); under g_tgLock */
struct OwedDoneOwed { unsigned kind; std::string key; long sentGen; OwedDoneOwed() : kind(0), sentGen(0) {} };
std::map<std::string, OwedDoneOwed> g_owedDoneOwed;   /* rows a finished save settled: DONE made owed until the world server's GONE (owedpop::DoneStep); under g_tgLock */
long long g_owedSettleNoted = 0, g_owedSettled = 0, g_owedSettleDropped = 0, g_owedReclaims = 0, g_owedNotThisSave = 0;
volatile long g_owedLever = 0;   /* TEST-ONLY `owedtest aside on`: this game sets aside what it would make, and its check-up makes nothing */
long long g_owedAdds = 0, g_owedClaims = 0, g_owedGrants = 0, g_owedMade = 0, g_owedDoneHas = 0, g_owedDoneGone = 0, g_owedDoneFault = 0,
          g_owedReleases = 0, g_owedGoneIn = 0, g_owedRowsIn = 0, g_owedBad = 0, g_owedSendFailed = 0, g_owedAsideLoad = 0, g_owedLeverAside = 0, g_owedDoneEmpty = 0, g_owedRefusedFull = 0;
volatile LONG g_owedLines = 0;
const LONG kOwedLineCap = 400;   /* [OWED] lines per game process (the REPORT line is not counted) */
static void OwedLine(const std::string& s) { if (::InterlockedIncrement(&g_owedLines) <= kOwedLineCap) DebugLog(s); }
/* is the first bar roll of the town `sid` owed - an owed row, or set aside by this game? ANY THREAD */
static bool OwedBarHas(const char* sid)
{
    bool has = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    if (!g_owed.empty() || !g_owedBars.empty())
        has = g_owed.find(owedpop::TableKey(owedpop::kKindBar, sid)) != g_owed.end() || g_owedBars.find(sid) != g_owedBars.end();
    ::LeaveCriticalSection(&g_tgLock);
    return has;
}
static bool OwedBarHasTown(void* town) { char sid[128]; return town != 0 && TownSidPod(town, sid, 128) != 0 && OwedBarHas(sid); }
/* a bar roll set aside: this game's own owed work until it is made or settled (its ADD goes out from the main thread, OwedFlush).
   ANY THREAD (the bar gate's) */
static void OwedBarNoteAside(void* town)
{
    char sid[128]; float tp[3] = { 0, 0, 0 };
    if (town == 0 || TownSidPod(town, sid, 128) == 0 || TownPosPod(town, tp) == 0) return;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    if (g_owedBars.find(sid) == g_owedBars.end()) { OwedBarLocal b; b.x = tp[0]; b.z = tp[2]; g_owedBars[sid] = b; }
    ::LeaveCriticalSection(&g_tgLock);
}
/* P8e-b: BarFlyCountPod is DELETED with the re-offer pass that was its only caller.  It read
   Town+0x320 through a cached pointer, which is the read the C-2 repair removes. */
void BarPendAdd(void* town)
{
    if (town == 0) return;
    bool fresh = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    { std::map<void*, long>::iterator it = g_barPend.find(town); if (it != g_barPend.end()) ++it->second; else { g_barPend[town] = 1; fresh = true; } }
    ::LeaveCriticalSection(&g_tgLock);
    ::InterlockedIncrement64(fresh ? &g_barDeferredTowns : &g_barDeferredRepeat);
    OwedBarNoteAside(town);   /* T-581: and kept as owed work (the world server keeps it once linked) */
}
void BarPendClear(void* town)
{
    size_t n = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); n = g_barPend.erase(town); ::LeaveCriticalSection(&g_tgLock);
    if (n != 0) ::InterlockedIncrement64(&g_barDeferredAnswered);
}
/* refill1 fold 2: is this Town waiting on a bar roll the mod DEFERRED (detour_barFlies: notebook-pending, pre-link, no map -
   every deferral path records it with BarPendAdd)? An identity compare only, as above - never dereferenced. (T-392 fold, owner
   317 a: the table grows, so every deferral is recorded and BarPendOwes is exactly BarPendHas.) */
static bool BarPendHas(void* town)
{
    if (town == 0) return false;
    bool has = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); has = g_barPend.find(town) != g_barPend.end(); ::LeaveCriticalSection(&g_tgLock);
    return has;
}
static bool BarPendOwes(void* town) { return BarPendHas(town) || OwedBarHasTown(town); }   /* T-581: or the town's first roll is owed work (a zone saved before it was made, here or on another game) */   /* T-392 fold (owner 317 a): the table cannot overflow, so no town is "owed" by default any more */
static void T515AsideState(void* b, void* tmpl, const char* road);   /* T-515: defined beside the set-aside entries */
static void T515RefusedState(void* b, void* tmpl, int teardown, const char* road);
static int T591PendingAside(const char* sid, const char* holder);   /* defined after the residents gate */
void* detour_create(void* self, void* faction, void* pos, void* town, unsigned long long p5, void* p6, void* p7,
                    unsigned long long p8, unsigned long long p9, unsigned char p10, void* p11, unsigned long long p12, unsigned int squadType)
{
    ::InterlockedIncrement64(&g_seen);
    if (townpending::InRun(g_t580RunTid, ::GetCurrentThreadId()) != 0) ++g_t581RunAsked;   /* T-581: every creation a residents re-run asks for */
    const bool offThread = ::GetCurrentThreadId() != g_mainThread;
    if (offThread) ::InterlockedIncrement64(&g_offThread);
    // the caller, first-only per return address (F503: which path creates with a town, on which thread)
    {
        const uintptr_t rva = (uintptr_t)_ReturnAddress() - (uintptr_t)::GetModuleHandleA(0);
        const unsigned long long ckey = ((unsigned long long)rva << 2) | (town != 0 ? 2ULL : 0ULL) | (offThread ? 1ULL : 0ULL);
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        bool known = false; for (int i = 0; i < g_callerCount; ++i) if (g_callerRvas[i] == ckey) { known = true; break; }
        bool inserted = false; if (!known) { if (g_callerCount < 16) { g_callerRvas[g_callerCount++] = ckey; inserted = true; } else ++g_callerOverflow; }
        ::LeaveCriticalSection(&g_tgLock);
        // review-p4d HIGH-1: a full table logs nothing more (the probe stays first-only)
        if (inserted) { char b[160]; _snprintf(b, 159, "[TOWN] creation caller rva=0x%llX town=%d type=%u thread=%s (first only per caller+town+thread)", (unsigned long long)rva, town != 0 ? 1 : 0, squadType, offThread ? "other" : "main"); b[159] = 0; DebugLog(b); }
    }
    if (!g_on) { ::InterlockedIncrement64(&g_leverOff); return CreatedTown(orig_create(self, faction, pos, town, p5, p6, p7, p8, p9, p10, p11, p12, squadType), town); }
    if (town == 0) { ::InterlockedIncrement64(&g_noTown); return CreatedTown(orig_create(self, faction, pos, town, p5, p6, p7, p8, p9, p10, p11, p12, squadType), town); }
    /* T-438: inside a populateBuilding whose residents the gate SET ASIDE (this thread's building-sector context, with the flag
       raised), the squad is refused as SET ASIDE - before any other gate, so it is never booked as a not-holder, no-map,
       no-notebook or pending refusal. It is judged by the building's context, never by its own spawn point (area edge). */
    {
        int asx = -1, asy = -1;
        const int inB = T392PopSector(&asx, &asy);
        if (townreoffer::InBuildingSquad(inB, inB != 0 ? (int)g_t392PopAside : 0) == townreoffer::kSqSetAside)
        { ::InterlockedIncrement64(&g_t392SquadsAside); ::InterlockedIncrement(&g_t392PopAsideN); T515AsideState(p6, p7, "set-aside"); return 0; }   /* T-515: the building's own state now, its people later */
    }
    /* decision 34: the notebook lists living groups for this town that THIS game has not placed yet, in an area this game has loaded or in
       or beside the town's own area (T-580, townpending::Holds) - the town has people about to be placed; do not invent more. Groups
       sleeping in areas this game does not load do not hold it (they are placed when their area loads). */
    /* E38 (e) / read-e38 Q3 - THE PENDING SET IS A CO-OP FACT. It is filled from the notebook's .meta files at
       install with no link required, so a LONE game with a leftover coop-store folder refused town squad
       generation for towns it had never shared - silently, and for the rest of the process. RoleIsSingle() is the
       precondition, tested here as well as inside StoreTownPeoplePending so the refusal cannot be reached by any
       route. ANY THREAD: this creation detour runs on the engine's workers, and the role is an interlocked long. */
    if (!RoleIsSingle())
    {
        char sidP[128]; char holderP[192];
        if (TownSidPod(town, sidP, 128) != 0 && TownPendingAsk(town, pos, sidP, holderP, 192) == townpending::kTownHeld)
        {
            ::InterlockedIncrement64(&g_refusedPending);
            if (townpending::InRun(g_t580RunTid, ::GetCurrentThreadId()) != 0) ++g_t580RunPending;   /* T-580: on a residents re-run's own thread - that re-run reads it */
            const std::string keyP = std::string(sidP) + " (notebook people pending)";
            bool firstP = false; long long nP = 0;
            const bool dueP = PendingRefusalBook(keyP, &firstP, &nP);
            if (firstP) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + JsonEsc(sidP) + "\",\"type\":" + N((long long)squadType) + ",\"refused\":1,\"cause\":\"notebook-pending\",\"holder\":\"" + JsonEsc(holderP) + "\"}");
            if (dueP) DebugLog(std::string("[TG] town '") + sidP + "' type=" + N((long long)squadType) + " refused as notebook-pending: refusal " + N(nP) + " for this town, held by note " + holderP + " (logged at the first refusal, then at most once per 5 minutes)");
            /* inside a building made here the building is set aside for the town's check-up (its state step as a set-aside
               building's, so the re-offer does not repeat it); elsewhere a plain refusal whose building state still lands */
            if (T591PendingAside(sidP, holderP) != 0) T515AsideState(p6, p7, "notebook-pending-set-aside");
            else T515RefusedState(p6, p7, 0, "notebook-pending");
            return 0;
        }
    }
    /* DECISION 48 (P8a, F630) - PRE-LINK NOBODY INVENTS.  The old arm refused only a DECLARED CLIENT, so the host
       populated a town and then refused to publish it into an area the notebook had given the other player: 130
       residents that existed on one machine only.  There is no host here any more.  With no notebook link there is
       no area map, and a game that invents from no map is the F514c duplicate waiting to happen.
       THE ONE EXEMPTION IS A LONE GAME (role=single): it has no notebook and never will, so refusing here would
       empty a single-player world's towns for the life of the process (decision 43).  ANY THREAD - the role is an
       interlocked long, read the same way the notebook-pending gate above reads it. */
    if (!g_linkedCached)
    {
        if (!RoleIsSingle())
        {
            ::InterlockedIncrement64(&g_refusedUnlinked); if (offThread) ::InterlockedIncrement64(&g_refusedUnlinkedOff);
            char sid4[128]; const std::string key4 = (TownSidPod(town, sid4, 128) != 0 ? std::string(sid4) : std::string("?")) + " (no-notebook)";   /* review-p4k: every refusal family booked per town */
            bool first4 = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); first4 = g_refusedTowns.insert(key4).second; ++g_refusedByTown[key4]; ::LeaveCriticalSection(&g_tgLock);
            if (first4) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + key4 + "\",\"type\":" + N((long long)squadType) + ",\"refused\":1,\"cause\":\"no-notebook\"}");
            T515RefusedState(p6, p7, 0, "no-notebook");   /* T-515 */
            return 0;
        }
        ::InterlockedIncrement64(&g_unlinkedAllowed);   /* P8a: reachable ONLY on role=single from here on */
        return CreatedTown(orig_create(self, faction, pos, town, p5, p6, p7, p8, p9, p10, p11, p12, squadType), town);
    }
    // linked: THE HOLDER RULE, decided on ANY thread through the locked sector snapshot (F503). Same rule on both games.
    /* T-392 (owner 336 a): a squad is judged by where it APPEARS, not by its home town - a patrol of a town in a held area
       that walks out into another area is that area's. The town-keyed notebook-pending gate above is unchanged. */
    float tp[3] = { 0, 0, 0 };
    int popSx = -1, popSy = -1;
    const int inBuilding = T392PopSector(&popSx, &popSy);   /* T-392 fold: inside a populateBuilding the residents gate let through */
    const int fromSpawn = inBuilding != 0 ? 0 : SpawnPosPod(pos, tp);
    ::InterlockedIncrement64(inBuilding != 0 ? &g_t392SecBuilding : (fromSpawn != 0 ? &g_t392SecSpawn : &g_t392SecTown));
    if (inBuilding == 0 && fromSpawn == 0 && !TownPosPod(town, tp)) { ::InterlockedIncrement64(&g_readFault); if (offThread) ::InterlockedIncrement64(&g_readFaultOff); return CreatedTown(orig_create(self, faction, pos, town, p5, p6, p7, p8, p9, p10, p11, p12, squadType), town); }
    Sector s = SectorOf(tp[0], tp[2]);
    if (inBuilding != 0) { s.x = popSx; s.y = popSy; }   /* T-392 fold: the building's sector */
    /* review-p5g Q5: ONE locked read of the area; the decision below and the refusal's CAUSE both come out of it. The cause
       used to be a second query (HeldByOtherTS) taken after the decision, so an AREAMAP landing between the two could label a
       refusal with a map that no longer existed. decision 37 amended: `aLoadedHere` is the first-there presumption. */
    int aHeld = 0, aMine = 0, aLoadedHere = 0, aRing1 = 0, aPeerRing1 = 0, aPeerLow = -2, aPresumedEmpty = 0, aPeerLowFresh = 1;
    AreaViewTS(s, &aHeld, &aMine, 0, &aLoadedHere, &aRing1, &aPeerRing1, &aPeerLow, &aPresumedEmpty, &aPeerLowFresh);   /* E25 re-designed: the slot to compare against comes out of the SAME locked read as the answer it decides */   /* P6j (verify-p6c MEDIUM-4): and so does the empty-table offer, which is counted only if the grant below is reached */
    const int may = MayInventFromView(aHeld, aMine, aLoadedHere, aRing1, aPeerRing1, MySlotLower(aPeerLow), aPresumedEmpty, aPeerLowFresh);
    /* decision 31(b): symmetric; decision 37 (amended): the relay names THIS game the holder, or nobody holds it and this game has it loaded, or `ring1` says it is unclaimed and next to this game's own player (E13 attempt 2 - this gate is the one T224 caught refusing its own town). E25: and if the OTHER player is next to it too, the lower slot invents and the other yields, so one town does not get two crowds. */
    /* P6j (verify-p6c MEDIUM-2): kInventTeardown is NEGATIVE, so it has to be excluded here or the engine freeing the
       world would be reported as a stale lease. It falls through to the ordinary refusal path below, which gives it
       its own cause string - the refusal line and the verdict both carry `teardown` rather than `not-mine`. */
    if (may == -1)
    {
        /* DECISION 48 (P8a) - ONE ANSWER FOR BOTH GAMES, AND IT IS REFUSE.  The pump, and with it the area map,
           stops for a whole save load (2.25 s measured, T192).  A game that invents while it cannot see the map is
           the F514c duplicate: two games standing apart each populated one town because neither could be told the
           other held it.  The old arm refused a declared CLIENT and let everybody else generate, which is a session
           role deciding a question about an area.  `noLease` counted the creations that arm let through, and it can
           only read 0 from this build on - stated and kept, not deleted, so a readout does not take a silent zero
           for a measurement (F172).  Its off-thread sibling `noLeaseOff` reads 0 for the same reason. */
        /* F666 / DECISION 42's SECOND HALF - THE WAIT IS BOUNDED NOW, so the paragraph above is true
           only for the first twenty seconds of a silence and this block is the rest of it.  Refusing on a stale map is
           still right for the first seconds of a hiccup: two games that cannot see each other's
           claims and both invent are F514c.  What was missing is an END: after the wait, the game
           holding the notebook's lowest slot generates so a town does not stay empty for the life
           of an outage, and every other game keeps refusing so nothing is duplicated. */
        /* T-392 (owner 337 a): THE BOUNDED WAIT ABOVE IS REMOVED. After twenty seconds the lowest notebook slot used to
           generate here - a slot, not the area's holder, deciding about an area. No fresh map now refuses for as long as it
           lasts. The second chances: the engine asks again for a world squad (every 2 s); a building's residents never
           reach this refusal, because TownGenResidentsGate sets them aside first and the town's own check-up re-offers them. */
        {
        ::InterlockedIncrement64(&g_refusedNoLease); if (offThread) ::InterlockedIncrement64(&g_refusedNoLeaseOff);
        ::InterlockedIncrement64(&g_refusedNoMap);
        char sid2[128]; const std::string key2 = (TownSidPod(town, sid2, 128) != 0 ? std::string(sid2) : std::string("?")) + " (no-map)";   // review-p4g MEDIUM-2: per-town bookkeeping
        bool first2 = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); first2 = g_refusedTowns.insert(key2).second; ++g_refusedByTown[key2]; ::LeaveCriticalSection(&g_tgLock);
        if (first2) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + key2 + "\",\"type\":" + N((long long)squadType) + ",\"refused\":1,\"cause\":\"no-map\"}");
        return 0;
        }
    }
    /* F666: ONE grant block, reached only when the notebook names this game the holder (T-392 retired the
       bounded-wait lowest-slot grant; P97 retired the wait itself). */
    if (may == 1)   /* T-392 (owner 337 a): the notebook naming this game the holder is the only grant */
    {
        // PROBE-START: P025
        if (::InterlockedIncrement(&g_p025Allowed) <= 3)
        {
            int ownerP = -2; double ageP = -1.0;
            AreaProbeTS(s, &ownerP, &ageP);
            char sidP[128]; const int haveP = TownSidPod(town, sidP, 128);
            char bP[320];
            _snprintf(bP, 319, "[PROBE] P025 allowed: town='%s' sector %d,%d held=%d mine=%d loadedHere=%d ring1=%d peerRing1=%d heldAge=%.2f mySlot=%d owner=%d",
                      haveP != 0 ? sidP : "?", s.x, s.y, aHeld, aMine, aLoadedHere, aRing1, aPeerRing1, ageP, StoreMySlot(), ownerP);
            bP[319] = 0; DebugLog(bP);
        }
        // PROBE-END: P025
        ::InterlockedIncrement64(&g_allowed); if (offThread) ::InterlockedIncrement64(&g_allowedOff);
        if (may == 1) ::InterlockedIncrement64(&g_holderGenerated);   /* DECISION 48 (P8a): this game HOLDS the area and generated - NOT the F666 fallback, which has its own number */
        void* np = CreatedTown(orig_create(self, faction, pos, town, p5, p6, p7, p8, p9, p10, p11, p12, squadType), town);
        /* PROBE-START: P085 */
        if (np != 0 && s.x >= 0 && s.x < 64 && s.y >= 0 && s.y < 64)
        { ::InterlockedIncrement(&g_p085Squads[s.x * 64 + s.y]); if (squadType == 2) ::InterlockedIncrement(&g_p085Residents[s.x * 64 + s.y]); }
        /* PROBE-END: P085 */
        /* P4p (F512): the first allowed creation per town is recorded WITH the new group's id, so decision 31(a)'s naming is checkable */
        char sidA[128]; const std::string keyA = TownSidPod(town, sidA, 128) != 0 ? std::string(sidA) : std::string("?");
        /* review-p4p H1: the id is read FIRST (a guarded engine call, no shared map); the town's one-shot slot is spent only on a successful read */
        const std::string idA = np != 0 ? StoreWorldIdOf(np) : std::string();
        bool firstA = false;
        /* P8a: and the group's id is remembered, so `roster full` can say spawnCause=towngen for the people this
           game's own town generation put in the world - the split build/read-roster-t236.md 4c needed and could not
           get from any line in T236a.  Bounded; the overflow is a NUMBER, not a silence. */
        if (!idA.empty())
        {
            TgLockInit(); ::EnterCriticalSection(&g_tgLock);
            firstA = g_allowedTowns.insert(keyA).second;
            if (g_tgCreatedIds.size() < kTownGenIdCap) g_tgCreatedIds.insert(idA); else ++g_tgIdOverflow;
            ::LeaveCriticalSection(&g_tgLock);
        }
        if (firstA) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + JsonEsc(keyA) + "\",\"type\":" + N((long long)squadType) + ",\"refused\":0,\"id\":\"" + JsonEsc(idA) + "\",\"sector\":\"" + N(s.x) + "," + N(s.y) + "\"}");
        return np;
    }
    /* decision 37: a refusal here has TWO causes and the verdict must tell them apart - another player HOLDS the area
       (decision 31(b), as before), or NOBODY holds it, which is now a refusal too: an unheld area is claimed by the first
       game to report it, so inventing there is what duplicated a town's crowd from a distance (F514c). */
    /* P6j (verify-p6c MEDIUM-2): A TEARDOWN IS NOT A "NOT-MINE". During the engine's teardown aHeld is not 1, so this
       refusal was labelled `not-mine` and printed a P025 line whose inputs (held=0 mine=0 loadedHere=1 ring1=1) are
       the exact F527/T224 shape that probe exists to detect - a real refusal wearing another refusal's evidence.
       The cause, the per-town key and the verdict all carry `teardown` now, and P025 is not spent on it. */
    const bool teardownR = (may == kInventTeardown);
    if (teardownR) ::InterlockedIncrement64(&g_refusedTeardown); else ::InterlockedIncrement64(&g_refusedNotHolder);
    const bool heldByOther = !teardownR && aHeld == 1;   /* review-p5g Q5: off the SAME locked view as `may`, not a second query */
    /* DECISION 48 (P8a): ONE cause, because there is now one rule - the holder generates.  WHICH non-holder state
       it was rides on the same record as `holder`, so both facts the old `held-by-other` / `not-mine` spellings
       carried are still machine-readable off one line and neither is a second spelling of the other. */
    const char* causeR = teardownR ? "teardown" : "not-holder";
    const char* holderR = teardownR ? "-" : (heldByOther ? "other" : "nobody");
    // PROBE-START: P025
    /* P025 (F527/E13): the client is refused throughout its own town, before AND after the relay names it holder, while the
       code reads correctly. Print the INPUTS this decision was taken from - the same locked view, plus the owner slot the
       relay's map actually carried for this area, the age of that map, and my own slot - for the first 8 not-mine refusals. */
    if (!teardownR && !heldByOther && ::InterlockedIncrement(&g_p025Refused) <= 8)
    {
        int ownerP = -2; double ageP = -1.0;
        AreaProbeTS(s, &ownerP, &ageP);
        char sidP[128]; const int haveP = TownSidPod(town, sidP, 128);
        char bP[320];
        _snprintf(bP, 319, "[PROBE] P025 refused not-mine: town='%s' sector %d,%d held=%d mine=%d loadedHere=%d ring1=%d peerRing1=%d heldAge=%.2f mySlot=%d owner=%d",
                  haveP != 0 ? sidP : "?", s.x, s.y, aHeld, aMine, aLoadedHere, aRing1, aPeerRing1, ageP, StoreMySlot(), ownerP);
        bP[319] = 0; DebugLog(bP);
    }
    // PROBE-END: P025
    ::InterlockedIncrement64(&g_refused); if (offThread) ::InterlockedIncrement64(&g_refusedOff);
    char sid[128]; const bool haveSid = TownSidPod(town, sid, 128) != 0;
    const std::string key = (haveSid ? std::string(sid) : std::string("?")) + (teardownR ? " (teardown)" : (heldByOther ? " (not-holder: other)" : " (not-holder: nobody)"));   /* the refusal keys are town x CAUSE, and P8a gives the two non-holder states their own keys rather than leaving one of them unmarked */
    bool first = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); first = g_refusedTowns.insert(key).second; ++g_refusedByTown[key]; ::LeaveCriticalSection(&g_tgLock);
    if (first)
    {
        DebugLog("[TOWN] refused squad generation for '" + key + "' type=" + N((long long)squadType) + " sector=" + N(s.x) + "," + N(s.y) + " (" + std::string(causeR) + ") - first only per town; the count is in [TG]");
        DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + key + "\",\"type\":" + N((long long)squadType) + ",\"refused\":1,\"cause\":\"" + std::string(causeR) + "\",\"holder\":\"" + std::string(holderR) + "\",\"sector\":\"" + N(s.x) + "," + N(s.y) + "\"}");   // first only per town (T191: 1576 lines otherwise)
    }
    T515RefusedState(p6, p7, teardownR ? 1 : 0, heldByOther ? "not-holder-other" : "not-holder-nobody");   /* T-515: the building's own state still lands on the game that is not the holder */
    return 0;
}

/* recruit3 (user decision 2026-09-26, 02-project-rules "Recruits"): BAR HIRE LISTS x PLAYERS, cap x4, host option
   `recruitmult` (auto|1..4; auto = the player slots seen in this world).  THE HOOK IS THIS DETOUR'S ALLOWED PATH
   (Town::spawnTheBarFlies 0x9FBE50), not the list builder 0x934E40: 0x9FBE50 is already hooked, the engine calls it
   only for a zone with no save file yet (ZoneMapContent+0xA8, recruit1 R1), and this path runs only on the game
   decision 48 lets generate - so the extra people are the holder's and reach the other game by the town-squad path
   every bar squad already takes.  THE ENGINE ROLLS EVERY EXTRA SQUAD ITSELF (recruit1 R2):
   1. Town::initialiseResidentData 0x935F90 fills the list - the engine's own first act in 0x9FBE50, which returns
      at once when Town+0x110 is set (935f90:23) - and the list is read: entries 0x98 apart at +0x328, count +0x320;
      entry +0x0 = the squad template's GameData, +0x88 = count, +0x8C = chance % (decomp_9fbe50).
   2. The engine's spawnTheBarFlies runs once over every list, exactly as vanilla.
   3. For each extra multiple the list is re-armed with every NON-hire entry's count at 0 and spawnTheBarFlies runs
      again: its own 0x935F90 call returns at once (flag set), so only the hire lists roll, each with its own count
      and chance - an independent roll per pass, so the expected hires are x mult (a raised count would not be: the
      roll stops at the first failed d100).  A count-0 entry creates nothing (9fbe50: `uVar20 < count`).
   4. Every entry gets its own count back and the list count is left 0 - the engine's own end state.
   A HIRE LIST is a template whose FCS name contains "recruit" (coopr::IsHireListName, common/recruitmult.h).
   Unique recruits: nothing here creates a person; a unique the engine picks from a generic hire list is still
   subject to the engine's own once-per-world check (Inferred). */
unsigned long long kTownInitResidentsRva = 0; static coop::AddrReg kTownInitResidentsRva_reg("TownInitialiseResidentData", &kTownInitResidentsRva);   /* the address table fills this. Steam_1.0.65 0x935F90 */   // void Town::initialiseResidentData(Town*)
typedef void (*TownInitResidentsFn)(void* town);
volatile long g_recruitMultCode = 0;        /* 0 auto, 1..4: written on the main thread from the notebook's OPTIONS map, read on the load thread */
volatile long g_recruitMultSeenInMap = 0;   /* the OPTIONS map being read named `recruitmult` (main thread only) */
long long g_rcTowns = 0, g_rcMultiplied = 0, g_rcX1 = 0, g_rcNoHire = 0, g_rcEmpty = 0, g_rcHireLists = 0, g_rcOtherLists = 0,
          g_rcBase = 0, g_rcTarget = 0, g_rcExtraPasses = 0, g_rcFault = 0, g_rcTooMany = 0, g_rcNoFn = 0, g_rcLinesDropped = 0;
const int kRcMaxEntries = 64;
std::vector<std::string> g_rcLines;         /* g_tgLock: the [RECRUIT] lines, printed on the MAIN thread by TownGenTick (this runs on the load thread) */
const size_t kRcLineCap = 64;

// GameData::name (+0x28, a VS2010 std::string: buffer/pointer at +0, size +0x10, capacity +0x18) - the caller holds the SEH frame
static int RcNamePod(const char* gd, char* out, int cap)
{
    out[0] = 0;
    if (gd == 0) return 0;
    const char* s = gd + 0x28;
    const size_t size = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
    if (size >= (size_t)cap) return 0;
    const char* p = (res >= 16) ? *(const char* const*)s : s;
    if (p == 0) return 0;
    std::memcpy(out, p, size); out[size] = 0; return 1;
}
static int RcTownNamePod(void* town, char* out, int cap)
{
    __try { return RcNamePod((const char*)((RootObjectBase*)town)->getRecord(), out, cap); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
/* 1 = read, 0 = fault, 2 = more entries than the table holds (the roll then goes ahead unmultiplied) */
static int RcFillAndReadPod(void* town, TownInitResidentsFn init, int* nOut, int* counts, unsigned char* hire, int cap)
{
    __try
    {
        init(town);
        const int n = *(const int*)((const char*)town + 0x320);
        if (n < 0 || n > 4096) return 0;
        *nOut = n;
        if (n > cap) return 2;
        const char* arr = *(const char* const*)((const char*)town + 0x328);
        if (n > 0 && arr == 0) return 0;
        for (int i = 0; i < n; ++i)
        {
            const char* e = arr + (size_t)i * 0x98;
            counts[i] = *(const int*)(e + 0x88);
            char nm[128];
            hire[i] = (RcNamePod(*(const char* const*)e, nm, 128) != 0 && coopr::IsHireListName(nm)) ? 1 : 0;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* hireOnly 1: every non-hire entry's count 0, the hire entries' own counts, list count n (an extra pass).
   hireOnly 0: every entry's own count back, list count 0 (the engine's own end state). */
static int RcRearmPod(void* town, int n, const int* counts, const unsigned char* hire, int hireOnly)
{
    __try
    {
        char* arr = *(char**)((char*)town + 0x328);
        if (n > 0 && arr == 0) return 0;
        for (int i = 0; i < n; ++i) *(int*)(arr + (size_t)i * 0x98 + 0x88) = (hireOnly != 0 && hire[i] == 0) ? 0 : counts[i];
        *(int*)((char*)town + 0x320) = (hireOnly != 0) ? n : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void RcQueueLine(const std::string& s)
{
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    if (g_rcLines.size() < kRcLineCap) g_rcLines.push_back(s); else ++g_rcLinesDropped;
    ::LeaveCriticalSection(&g_tgLock);
}
void RecruitDrainLines()   // MAIN THREAD
{
    std::vector<std::string> v;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); v.swap(g_rcLines); ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < v.size(); ++i) DebugLog(v[i]);
}
/* refill1: the towns THIS game rolled fresh (the engine's first roll of a new zone, allowed here) since the world loaded.
   The bar's usual size is recorded from such a town at its first check-up (from=firstRoll). Keyed by stringID - never a
   Town pointer; under g_tgLock because the roll's thread is not this file's to assume. */
std::set<std::string> g_rfFresh;
static void RefillNoteFreshRoll(void* town)
{
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0) return;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); if (g_rfFresh.size() < 4096) g_rfFresh.insert(std::string(sid)); ::LeaveCriticalSection(&g_tgLock);
}
/* review-refill1 item 1: THE REMEMBERED LIST LENGTH, per town stringID. The engine's roll zeroes only the list count +0x320;
   the entries stay in the array at +0x328 (decomp 9fbe50: the array is only read, the one write is the count at :294;
   recruit-refill-read R2 / review-p4f HIGH-2). The refill re-arms that array with the length remembered here and NEVER
   clears the fill mark +0x110 to make initialiseResidentData 0x935F90 fill it again: that function has a second caller, the
   AI package SlaverPrisonerShipping_AnywhereTownsIncluded 0x289F50 -> 0x9381C0 (Inferred on the AI worker thread), and it
   returns early only while +0x110 is 1. Captured before the engine's roll zeroes the count (detour_barFlies,
   BarFliesWithRecruitMult) and after the refill's own init; cleared with the world (RefillTeardown). Under g_tgLock. */
std::map<std::string, int> g_rfListLen;
static int RfCountMarkPod(void* town, int* n, int* mark)
{
    __try { *n = *(const int*)((const char*)town + 0x320); *mark = (int)*(const unsigned char*)((const char*)town + 0x110); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void RfRememberLen(void* town, int n)
{
    if (n <= 0 || n > 4096) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0) return;
    const std::string key(sid);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    if (g_rfListLen.size() < 4096 || g_rfListLen.find(key) != g_rfListLen.end()) g_rfListLen[key] = n;
    ::LeaveCriticalSection(&g_tgLock);
}
static int RfRememberedLen(const std::string& key)
{
    int n = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    const std::map<std::string, int>::const_iterator it = g_rfListLen.find(key);
    if (it != g_rfListLen.end()) n = it->second;
    ::LeaveCriticalSection(&g_tgLock);
    return n;
}
// the holder's allowed bar roll (detour_barFlies below), on the engine's own load thread
void BarFliesWithRecruitMult(void* town)
{
    ::InterlockedIncrement64(&g_rcTowns);
    const long code = g_recruitMultCode;
    const int slots = ZonesSlotsSeen();
    const int mult = coopr::RecruitMultEffective((int)code, slots);
    if (kTownInitResidentsRva == 0) { ::InterlockedIncrement64(&g_rcNoFn); orig_barFlies(town); return; }
    const TownInitResidentsFn init = (TownInitResidentsFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kTownInitResidentsRva);
    int n = 0; int counts[kRcMaxEntries]; unsigned char hire[kRcMaxEntries];
    const int rd = RcFillAndReadPod(town, init, &n, counts, hire, kRcMaxEntries);
    if (rd != 1) { ::InterlockedIncrement64(rd == 2 ? &g_rcTooMany : &g_rcFault); orig_barFlies(town); return; }
    RfRememberLen(town, n);   /* review-refill1 item 1: the list length, before the engine's roll zeroes the count (the entries stay) */
    int hireLists = 0, otherLists = 0, base = 0;
    for (int i = 0; i < n; ++i) { if (hire[i] != 0) { ++hireLists; if (counts[i] > 0) base += counts[i]; } else ++otherLists; }
    orig_barFlies(town);   /* the engine's own roll of every list, exactly as vanilla */
    if (n == 0) { ::InterlockedIncrement64(&g_rcEmpty); return; }   /* a list already spent (a later activation of the same Town): nothing to roll */
    RefillNoteFreshRoll(town);   /* refill1: a real first roll - its first check-up records the bar's usual size */
    int passes = 0; bool fault = false;
    if (mult > 1 && base > 0)
    {
        for (int p = 1; p < mult; ++p)
        {
            if (RcRearmPod(town, n, counts, hire, 1) == 0) { fault = true; break; }
            orig_barFlies(town);   /* hire lists only: every other entry's count is 0 for this pass */
            ++passes;
        }
        if (RcRearmPod(town, n, counts, hire, 0) == 0) fault = true;
    }
    if (fault) ::InterlockedIncrement64(&g_rcFault);
    if (base == 0) ::InterlockedIncrement64(&g_rcNoHire); else if (passes > 0) ::InterlockedIncrement64(&g_rcMultiplied); else ::InterlockedIncrement64(&g_rcX1);
    ::InterlockedExchangeAdd64(&g_rcHireLists, hireLists); ::InterlockedExchangeAdd64(&g_rcOtherLists, otherLists);
    ::InterlockedExchangeAdd64(&g_rcBase, base); ::InterlockedExchangeAdd64(&g_rcTarget, (long long)base * (passes + 1)); ::InterlockedExchangeAdd64(&g_rcExtraPasses, passes);
    char sid[128]; if (TownSidPod(town, sid, 128) == 0) { sid[0] = '?'; sid[1] = 0; }
    char nm[128]; if (RcTownNamePod(town, nm, 128) == 0) { nm[0] = '?'; nm[1] = 0; }
    char b[512];
    _snprintf(b, 511, "[RECRUIT] bar town=%s (%s) hireSquads base=%d x%d -> %d (hireLists=%d otherLists=%d extraPasses=%d option=%s slotsSeen=%d fault=%d; base = the hire lists' own counts, the most one engine roll can make)",
              nm, sid, base, passes + 1, base * (passes + 1), hireLists, otherLists, passes, coopr::RecruitMultName((int)code), slots, fault ? 1 : 0);
    b[511] = 0; RcQueueLine(b);
}
void RecruitReport()   // MAIN THREAD
{
    RecruitDrainLines();
    const long code = g_recruitMultCode; const int slots = ZonesSlotsSeen();
    DebugLog("[RECRUIT] REPORT option=" + std::string(coopr::RecruitMultName((int)code)) + " slotsSeen=" + N(slots) + " effective=x" + N(coopr::RecruitMultEffective((int)code, slots))
             + " towns=" + N(g_rcTowns) + " multiplied=" + N(g_rcMultiplied) + " x1=" + N(g_rcX1) + " noHireLists=" + N(g_rcNoHire) + " spent=" + N(g_rcEmpty)
             + " hireLists=" + N(g_rcHireLists) + " otherLists=" + N(g_rcOtherLists) + " hireBase=" + N(g_rcBase) + " hireTarget=" + N(g_rcTarget)
             + " extraPasses=" + N(g_rcExtraPasses) + " fault=" + N(g_rcFault) + " tooMany=" + N(g_rcTooMany) + " noInitFn=" + N(g_rcNoFn) + " linesDropped=" + N(g_rcLinesDropped));
}

/* T-392 (owner 335 a): THE SET-ASIDE WORK. A bar list set aside stays in g_barPend (an identity token, never dereferenced -
   above). A building's residents set aside are kept here BY THE BUILDING'S POSITION KEY (items.cpp's P7n key: base record,
   sector, position in tenths - ObjectPositionKey), never by pointer, with the stringID of the building's town and its x/z.
   Both are offered again from the engine's own per-town check-up (T392Reoffer, beside RefillCheck). Cleared at every world
   teardown, beside g_barPend. Under g_tgLock. */
struct T392ResPend { std::string sid; float x, z; long tries; int stateAtLoad; long sentGen; int acked; int pendAside; T392ResPend() : x(0.0f), z(0.0f), tries(0), stateAtLoad(0), sentGen(0), acked(0), pendAside(0) {} };   /* pendAside = set aside because a note of the town held (T591PendingAside) */   /* sentGen = the link generation this entry's ADD went out on (0 = not sent; -1 = not sendable) */   /* stateAtLoad = the building-state step ran when the squads were set aside */
std::map<std::string, T392ResPend> g_t392Res;   /* T-392 fold (owner 317 a): grows as needed, never refuses */
std::map<std::string, std::string> g_t392ResCursor;   /* T-392 fold (review MED): town sid -> the last key its check-up looked at (round-robin) */
std::set<std::string> g_t392DeferLogged, g_t392WaitLogged;   /* "<kind>|<town sid>": the first-per-town lines */
void* g_t392Factory = 0;   /* the engine's one RootObjectFactory (chooseResidents passes the global DAT_142133550); cleared at teardown, set again by the next populateBuilding */
static void T392DeferLog(const char* kind, const std::string& sid, int sx, int sy, const char* cause, const char* bkey)
{
    ::InterlockedIncrement64(std::strcmp(kind, "bar") == 0 ? &g_t392DeferBar : &g_t392DeferRes);
    bool first = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); first = g_t392DeferLogged.insert(std::string(kind) + "|" + sid).second; ::LeaveCriticalSection(&g_tgLock);
    if (!first) return;
    DebugLog("[TOWN] deferred kind=" + std::string(kind) + " town=" + sid + " sector=" + N(sx) + "," + N(sy) + " cause=" + std::string(cause)
             + (bkey != 0 ? " building=" + std::string(bkey) : std::string()) + " next=town-checkup");
}
static size_t T392ResCount() { size_t c = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); c = g_t392Res.size(); ::LeaveCriticalSection(&g_tgLock); return c; }
/* the entries waiting now that were set aside because a note of the town held (the report) */
static long long T591WaitingNow() { long long c = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); for (std::map<std::string, T392ResPend>::const_iterator it = g_t392Res.begin(); it != g_t392Res.end(); ++it) if (it->second.pendAside != 0) ++c; ::LeaveCriticalSection(&g_tgLock); return c; }
static void T392ResErase(const char* key)
{
    const std::string k(key);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_t392Res.erase(k); ::LeaveCriticalSection(&g_tgLock);
}
/* new, or a repeat of a key already waiting (T-392 fold, owner 317 a: the table grows, nothing is refused) */
static void T392ResRecord(const char* key, const char* sid, float x, float z)
{
    const std::string k(key);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    std::map<std::string, T392ResPend>::iterator it = g_t392Res.find(k);
    if (it != g_t392Res.end()) ++it->second.tries;
    else { T392ResPend p; p.sid = sid; p.x = x; p.z = z; p.tries = 1; g_t392Res[k] = p; }
    ::LeaveCriticalSection(&g_tgLock);
}
typedef char T581CausesAgree[(owedpop::kCauseOwed == townreoffer::kDefOwed && owedpop::kCauseTestLever == townreoffer::kDefTestLever && owedpop::kCauseNone == townreoffer::kDefNone) ? 1 : -1];   /* a compile error here = owedpop.h and townreoffer.h disagree */
/* T-581: where one item of set-aside work stands with the world server: 1 = an owed row (*claim = owedpop::kClaim* as seen here);
   2 = this game's own, its ADD sent on this link and not back yet; 0 = only this game knows it. ANY THREAD. */
static int OwedStored(unsigned kind, const std::string& key, int* claim)
{
    const long gen = (long)StoreLinkGen();
    int r = 0, c = (int)owedpop::kClaimNone;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    const owedpop::Table::const_iterator it = g_owed.find(owedpop::TableKey(kind, key));
    if (it != g_owed.end()) { r = 1; c = it->second.claimant; }
    else
    {
        long sent = 0;
        if (kind == owedpop::kKindResidents) { const std::map<std::string, T392ResPend>::const_iterator l = g_t392Res.find(key); if (l != g_t392Res.end()) sent = l->second.sentGen; }
        else { const std::map<std::string, OwedBarLocal>::const_iterator l = g_owedBars.find(key); if (l != g_owedBars.end()) sent = l->second.sentGen; }
        if (sent > 0 && sent == gen) r = 2;
    }
    ::LeaveCriticalSection(&g_tgLock);
    if (claim != 0) *claim = c;
    return r;
}
static size_t OwedRowCount() { size_t c = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); c = g_owed.size(); ::LeaveCriticalSection(&g_tgLock); return c; }
/* any owed row, or any set-aside work of this game's own (sent or not) - the load gates' cheap first test. ANY THREAD */
static bool OwedAnyWork() { bool a = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); a = !g_owed.empty() || !g_owedBars.empty() || !g_t392Res.empty(); ::LeaveCriticalSection(&g_tgLock); return a; }
static bool OwedSendDone(unsigned kind, const std::string& key, unsigned why, bool queueOnFail = true);   /* defined with the re-offer below */
/* this game made an owed row's people. They exist only in this game's memory until a save of its own holds them, so the row
   is not DONE yet: it stays on the world server, and this game's, until this game's next profile save asked for after this moment
   has finished (TownGenOwedSaveFinished sends DONE made then). ANY THREAD. */
static void OwedSettleNote(unsigned kind, const std::string& key)
{
    const long seq = StoreSaveRequestSeq();
    int held = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    { const owedpop::Table::const_iterator it = g_owed.find(owedpop::TableKey(kind, key)); held = (it != g_owed.end() && it->second.claimant == (int)owedpop::kClaimYou) ? 1 : 0; }
    owedpop::SettleNote(&g_owedSettle, kind, key, seq, held);
    ++g_owedSettleNoted;
    ::LeaveCriticalSection(&g_tgLock);
    OwedLine(std::string("[OWED] made kind=") + owedpop::KindName(kind) + (kind == owedpop::kKindBar ? " town=" : " building=") + key + " held=" + N((long long)held)
             + " - the row stays on the world server, this game's, until this game's next save (asked for after save request " + N((long long)seq) + ") has finished");
}
/* is this row held back here - made by this game and no finished save of its own holds it yet, or settled by such a save with its
   DONE not yet answered by the world server's GONE? ANY THREAD */
static int OwedSettling(unsigned kind, const std::string& key)
{
    int r = 0;
    const std::string tk = owedpop::TableKey(kind, key);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); r = (g_owedSettle.find(tk) != g_owedSettle.end() || g_owedDoneOwed.find(tk) != g_owedDoneOwed.end()) ? 1 : 0; ::LeaveCriticalSection(&g_tgLock);
    return r;
}
/* this game's gate let a town's bar roll through (the engine rolled it): this game's own record of it ends, and an owed row
   for it settles at this game's next finished save (OwedSettleNote). ANY THREAD. */
static void OwedBarMade(void* town)
{
    char sid[128];
    if (town == 0 || TownSidPod(town, sid, 128) == 0) return;
    const int st = OwedStored(owedpop::kKindBar, sid, 0);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    g_owedBars.erase(sid);
    ::LeaveCriticalSection(&g_tgLock);
    if (st != 0) OwedSettleNote(owedpop::kKindBar, sid);
}
/* T-581: the world's own object factory - the one chooseResidents passes populateBuilding (GameWorld+0x4A0, DAT_142133550) - for a
   check-up in a world where no populateBuilding has run (every zone loaded from its save). 0 = unreadable. */
static void* OwedWorldFactoryPod()
{
    __try { ::GameWorld* w = GameWorldPtr(); return w != 0 ? (void*)w->objectFactory : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-515: the building's ruin flag (+0x1A1, setDestroyed 0x5567F0) and door count (+0x1C0, getDoor 0xF7040); -1 = unread */
static void T515RuinPod(void* b, int* ruin, int* doors)
{
    *ruin = -1; *doors = -1;
    if (b == 0) return;
    __try { *ruin = (int)*(const unsigned char*)((const char*)b + 0x1A1); *doors = (int)*(const unsigned int*)((const char*)b + 0x1C0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ruin = -1; *doors = -1; }
}
static int T515BldTemplatePod(void* b, void** out)
{
    *out = 0;
    __try { *out = *(void* const*)((const char*)b + 0xF0); return 1; }   /* the building's resident template (decomp_57ea30:28) */
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
static int T515StatePod(void* b, void* t)
{
    __try { orig_bldState(b, t); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void T515Log(int changed, const std::string& line)
{
    if (::InterlockedIncrement(changed != 0 ? &g_t515ChangeLines : &g_t515Lines) <= (changed != 0 ? kT515ChangeLineCap : kT515LineCap)) DebugLog(line);
}
const int kT392KeyRoom = 128;
static int T392Key(void* building, char* out);   /* T-515: defined with the residents gate below */
/* T-515: A SQUAD SET ASIDE STILL TAKES THE ENGINE'S BUILDING-STATE STEP, at the moment and on the thread the engine itself takes
   it (inside createRandomUnloadedSquad, before the squad is made - decomp_57ea30:34). The Hub's walls carry "building ruined" in
   their (empty) resident template: without this step they stand whole until the re-offer runs it, and that late ruin deletes a
   gate's doors while the gate pass may hold a finished batch for them (getDoor() null, written without a check). Off the main
   thread setDestroyed re-posts itself as message 16 (decomp_5567f0), exactly as for the engine's own call here. The entry is
   marked so the re-offer's populateBuilding does not take the step a second time (detour_bldState). Called by detour_create's
   set-aside branch, on the thread whose building-sector context names the building (g_t392PopKey). */
static void T515AsideState(void* b, void* tmpl, const char* road)
{
    if (orig_bldState == 0) { ::InterlockedIncrement64(&g_t515NoHook); return; }
    void* bt = 0;
    if (b != 0 && tmpl == 0 && T515BldTemplatePod(b, &bt) == 0) { ::InterlockedIncrement64(&g_t515Fault); return; }
    const int which = townreoffer::AsideStateTemplate(b != 0 ? 1 : 0, tmpl != 0 ? 1 : 0, bt != 0 ? 1 : 0);
    if (which == townreoffer::kStateNone) { ::InterlockedIncrement64(&g_t515NoTemplate); return; }
    int r0 = -1, d0 = -1, r1 = -1, d1 = -1;
    T515RuinPod(b, &r0, &d0);
    if (T515StatePod(b, which == townreoffer::kStateArg ? tmpl : bt) == 0) { ::InterlockedIncrement64(&g_t515Fault); return; }
    ::InterlockedIncrement64(&g_t515Applied);
    T515RuinPod(b, &r1, &d1);
    /* only a SET-ASIDE building has a waiting entry, and only then does g_t392PopKey name this building (the gate writes it on
       its set-aside answer); a building the gate let through has no entry (the gate erased it), so nothing re-runs its step */
    const int aside = (g_t392PopAside != 0) ? 1 : 0;
    std::string key;
    if (aside != 0)
    {
        key = g_t392PopKey;
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        { std::map<std::string, T392ResPend>::iterator it = g_t392Res.find(key); if (it != g_t392Res.end()) it->second.stateAtLoad = 1; }
        ::LeaveCriticalSection(&g_tgLock);
    }
    else
    {
        ::InterlockedIncrement64(&g_t515RefusedApplied);
        char k[kT392KeyRoom];
        key = (T392Key(b, k) != 0) ? std::string(k) : std::string("?");
    }
    T515Log((r0 != r1 || d0 != d1) ? 1 : 0, "[TOWN] building state at load road=" + std::string(road) + " building=" + key + " template=" + (which == townreoffer::kStateArg ? "squad" : "building")
            + " ruin=" + N(r0) + "->" + N(r1) + " doors=" + N(d0) + "->" + N(d1)
            + " thread=" + (::GetCurrentThreadId() == g_mainThread ? "main" : "other (a ruin lands at the engine's message 16)"));
}
/* T-515: A BUILDING SQUAD REFUSED BY ANOTHER RESIDENTS ROAD (not the holder, notebook people pending, no notebook) is refused
   before the engine's own function runs, so its building-state step never ran either: on the game that is not the holder The
   Hub's walls stayed whole while the holder's were ruins. The step is taken here whenever the refusal is inside a building's
   populateBuilding on this thread (townreoffer::RefusedSquadTakesState) - never while the engine frees the world. No waiting
   entry exists for such a building (the gate let its populateBuilding through), so the re-offer never repeats the step. */
static void T515RefusedState(void* b, void* tmpl, int teardown, const char* road)
{
    int sx = -1, sy = -1;
    const int inB = T392PopSector(&sx, &sy);
    if (townreoffer::RefusedSquadTakesState(inB, b != 0 ? 1 : 0, teardown) == 0) return;
    T515AsideState(b, tmpl, road);
}
/* T-515: THE ENGINE'S BUILDING-STATE STEP, hooked for one decision: inside the re-offer's populateBuilding (MAIN THREAD) for a
   building whose step already ran when its squads were set aside, the step is skipped - the ruin and both fields would be written
   the same again, but the town's nest list would get each "nest" entry a second time. Everything else runs the original. */
void detour_bldState(void* b, void* tmpl)
{
    const int mine = (b != 0 && b == g_t515RerunBld && ::GetCurrentThreadId() == g_mainThread) ? 1 : 0;
    if (townreoffer::RerunStateSkips(mine, mine != 0 ? (int)g_t515RerunAtLoad : 0) != 0) { ::InterlockedIncrement64(&g_t515RerunSkipped); return; }
    if (mine != 0) ::InterlockedIncrement64(&g_t515RerunRan);
    orig_bldState(b, tmpl);
}
/* T-392 (owner 335 a): THE RESIDENTS GATE, asked by worldgen.cpp's populateBuilding detour before the original runs.
   1 = run the original now: this game may invent there, or another game holds the area (its squads are then refused one by
   one inside, exactly as today), or the engine is freeing the world, or there is nothing to decide with.
   0 = SET ASIDE: no notebook link, no fresh map, or nobody holds the area - none of them is an answer about the area, and
   chooseResidents runs once per never-saved zone, so a refusal here used to empty the buildings for the visit.
   The building is judged where it STANDS (its own position). Skipping the original and running it later does the same work:
   chooseResidents reads nothing populateBuilding produced (decomp_936200:144-175), and populateBuilding re-checks its own
   preconditions when it runs (decomp_57ed90:55-71). MAIN THREAD in practice (chooseResidents <- loadPhase2 <-
   updateMainThread); the table is under g_tgLock regardless. */
/* T-392 fold (review H1): every T-392 key is built with the resolver's OWN cap (items.cpp kBoxKeyCap, read through
   BoxKeyCap), so a key whose plain form does not fit is hashed here exactly as the resolver's walk hashes it - the round
   trip is byte-identical by construction. `out` holds kT392KeyRoom bytes. 1 = built. */
static int T392Key(void* building, char* out)
{
    out[0] = 0;
    const int cap = BoxKeyCap();
    if (cap <= 0 || cap > kT392KeyRoom) return 0;
    return ObjectPositionKey(building, out, cap, "", 3, 0);
}
/* T-392 fold (review LOW): the load-time gate's OWN question - MayInventFromView over one locked AreaViewTS read, with the
   loaded-here and next-to-the-player presumptions - asked by the residents gate and by the re-offer alike. *heldOut = held. */
static int T392MayAt(const Sector& s, int* heldOut)
{
    int h = 0, m = 0, l = 0, r1 = 0, pr1 = 0, low = -2, pe = 0, lf = 1;
    AreaViewTS(s, &h, &m, 0, &l, &r1, &pr1, &low, &pe, &lf);
    const int may = MayInventFromView(h, m, l, r1, pr1, MySlotLower(low), pe, lf);
    if (heldOut != 0) *heldOut = h;
    return may;
}
/* T-438: the town's resident-bucket count - the int at bucket+8 that populateBuilding's tail adds the building's expected
   headcount to (decomp_57ed90:197-199; the bucket is the town population manager's vt+0x8 with type 1, as T2NodesPod reads it).
   Read before and after an engine call so a run can see what each call added. 1 read; 0 none or fault. ANY THREAD */
typedef char* (*T438BucketFn)(void* mgr, int type);
static int T392ResCountPod(void* town, int* out)
{
    *out = 0;
    if (town == 0) return 0;
    __try
    {
        char* mgr = *(char* const*)((const char*)town + 0xE0);
        if (mgr == 0) return 0;
        char* b = ((T438BucketFn)((*(void* const* const*)mgr)[1]))(mgr, 1);
        if (b == 0) return 0;
        *out = *(const int*)(b + 8);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-438 (b): the type-1 bucket's budget pair - the +8 int and the +0xC float populateBuilding's tail adds the same expected
   headcount to (decomp_57ed90:198-199); +0xC is the regrow budget the marker 0x929150 reads (decomp_92926a:33). 1 read; 0 none
   or fault. MAIN THREAD (the re-offer) */
static int T392ResBudgetPod(void* town, int* n, float* f)
{
    *n = 0; *f = 0.0f;
    if (town == 0) return 0;
    __try
    {
        char* mgr = *(char* const*)((const char*)town + 0xE0);
        if (mgr == 0) return 0;
        char* b = ((T438BucketFn)((*(void* const* const*)mgr)[1]))(mgr, 1);
        if (b == 0) return 0;
        *n = *(const int*)(b + 8);
        *f = *(const float*)(b + 0xC);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-438 (b) / t438-r1: write the budget pair back - the int to `n` (after minus the re-run's add) and the float to `f` (its exact value
   before the re-run), townreoffer::RerunPopUndo. 1 written. MAIN THREAD */
static int T392ResBudgetSetPod(void* town, int n, float f)
{
    if (town == 0) return 0;
    __try
    {
        char* mgr = *(char* const*)((const char*)town + 0xE0);
        if (mgr == 0) return 0;
        char* b = ((T438BucketFn)((*(void* const* const*)mgr)[1]))(mgr, 1);
        if (b == 0) return 0;
        *(int*)(b + 8) = n;
        *(float*)(b + 0xC) = f;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-438: the building's interior after the engine call - 0x561750 loads the layout only while interior+0x10 is 0 (worldgen.cpp
   P072 notes, decomp_561750:14-16), so +0x10 set reads as the layout loaded. -1 fault; 0 no interior (Building+0x1F0 empty);
   1 on but the layout pointer not set; 2 set (townreoffer::FurnitureName). ANY THREAD */
static int T392InteriorPod(void* building)
{
    if (building == 0) return -1;
    __try
    {
        const char* in = *(const char* const*)((const char*)building + 0x1F0);
        if (in == 0) return 0;
        return *(void* const*)(in + 0x10) != 0 ? 2 : 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-438: the end of the gate's building-sector context (TownGenResidentsDone). If the gate SET ASIDE this building and the engine
   call ran (ran = 1), one line per building says the furniture loaded while the residents wait. The flag is lowered either way. */
static void T392ResidentsDone(int ran)
{
    const int mine = (g_t392PopTid != 0 && g_t392PopTid == ::GetCurrentThreadId()) ? 1 : 0;
    if (mine != 0 && g_t392PopAside != 0 && ran != 0)
    {
        ::InterlockedIncrement64(&g_t392AsideRuns);
        int c1 = 0; long long add = 0; int addOk = 0;
        if (g_t392PopCnt0Ok != 0 && T392ResCountPod(g_t392PopTown, &c1) != 0) { add = (long long)c1 - (long long)g_t392PopCnt0; addOk = 1; ::InterlockedExchangeAdd64(&g_t392AsidePopAdd, add); }
        const char* furn = townreoffer::FurnitureName(T392InteriorPod(g_t392PopBld));
        const std::string key(g_t392PopKey);
        bool first = false;
        TgLockInit(); ::EnterCriticalSection(&g_tgLock); first = g_t392AsideLogged.insert(key).second; ::LeaveCriticalSection(&g_tgLock);
        if (first)
            DebugLog("[TOWN] deferred kind=residents town=" + std::string(g_t392PopSid) + " sector=" + N((long long)g_t392PopSx) + "," + N((long long)g_t392PopSy)
                     + " building=" + key + " furniture=" + furn + " squadsSetAside=" + N((long long)g_t392PopAsideN)
                     + " popAdd=" + (addOk != 0 ? N(add) : std::string("unread")) + " next=town-checkup");
    }
    g_t392PopTid = 0;
    g_t392PopAside = 0;
    g_t591GateMay = 0;
}
static int T392ResidentsGate(void* factory, void* building)
{
    if (factory != 0) g_t392Factory = factory;
    if (!g_on || building == 0 || RoleIsSingle()) return 1;
    ::InterlockedIncrement64(&g_t392ResSeen);
    float bp[3] = { 0, 0, 0 };
    if (TownPosPod(building, bp) == 0) { ::InterlockedIncrement64(&g_t392ResNoKey); return 1; }   /* no position: as today */
    char sid[128];
    void* const t = TownOfBuilding(building);
    if (t == 0 || TownSidPod(t, sid, 128) == 0) { ::InterlockedIncrement64(&g_t392ResNoTown); return 1; }   /* populateBuilding returns at once with no town (decomp_57ed90:65-68) */
    const Sector s = SectorOf(bp[0], bp[2]);
    int dc = townreoffer::kDefNoNotebook, mayG = 0;
    if (g_linkedCached) { int h = 0; mayG = T392MayAt(s, &h); dc = townreoffer::DeferCause(1, mayG, h); }
    char key[kT392KeyRoom]; key[0] = 0;
    int keyOk = -1;   /* -1 = not built yet */
    /* T-581: residents this game would make now are set aside when they are an owed row this game does not hold yet (they are made
       under a claim, from the town's check-up), or while the TEST-ONLY lever is on */
    if (dc == townreoffer::kDefNone && mayG == 1 && (g_owedLever != 0 || OwedAnyWork()))
    {
        keyOk = T392Key(building, key);
        int cl = 0;
        const int st = (keyOk != 0) ? OwedStored(owedpop::kKindResidents, key, &cl) : 0;
        dc = owedpop::LoadCause(dc, mayG, (st == 2 || (st == 1 && cl != (int)owedpop::kClaimYou)) ? 1 : 0, (int)g_owedLever);   /* a row given to this game runs (the re-offer's own call) */
        if (dc == townreoffer::kDefOwed) ::InterlockedIncrement64(&g_owedAsideLoad);
        else if (dc == townreoffer::kDefTestLever) ::InterlockedIncrement64(&g_owedLeverAside);
    }
    if (dc == townreoffer::kDefNone)
    {
        if (T392ResCount() != 0) { char k0[kT392KeyRoom]; if (T392Key(building, k0) != 0) T392ResErase(k0); }   /* answered: it leaves the table */
        g_t591GateMay = mayG; g_t392PopBld = building; g_t392PopTown = t; std::strncpy(g_t392PopSid, sid, 127); g_t392PopSid[127] = 0;   /* a squad refused inside for a pending note sets this building aside (T591PendingAside) */
        g_t392PopAside = 0; g_t392PopSx = s.x; g_t392PopSy = s.y; g_t392PopTid = ::GetCurrentThreadId();   /* T-392 fold: the squads made inside are this building's (T-438: not set aside) */
        return 1;
    }
    if (keyOk < 0) keyOk = T392Key(building, key);
    if (keyOk == 0) { ::InterlockedIncrement64(&g_t392ResNoKey); return 1; }   /* no key = no way to find it again: as today */
    T392ResRecord(key, sid, bp[0], bp[2]);
    T392DeferLog("residents", std::string(sid), s.x, s.y, townreoffer::DeferCauseName(dc), key);
    /* T-438: populateBuilding still runs (interior/furniture, owner faction - at this load event); its squads are refused as SET
       ASIDE inside the creation detour, judged by THIS building-sector context. The caller lowers the flag (TownGenResidentsDone). */
    g_t392PopAside = 1; g_t392PopAsideN = 0; g_t392PopBld = building; g_t392PopTown = t;
    { int c0 = 0; g_t392PopCnt0Ok = T392ResCountPod(t, &c0); g_t392PopCnt0 = c0; }
    std::strncpy(g_t392PopSid, sid, 127); g_t392PopSid[127] = 0; std::strncpy(g_t392PopKey, key, 127); g_t392PopKey[127] = 0;
    g_t392PopSx = s.x; g_t392PopSy = s.y; g_t392PopTid = ::GetCurrentThreadId();
    return 0;
}
/* A BUILDING'S RESIDENTS REFUSED BECAUSE A NOTE OF THEIR TOWN HOLDS ARE SET ASIDE, NOT LOST. The engine fills a building
   once per never-saved zone, and the notes of the town are placed after its area loads (store.cpp, PlaceUnplacedNotes), so a
   plain refusal here would leave the building empty for the visit. Asked by detour_create's notebook-pending refusal (`holder` = the note that
   holds). Inside a populateBuilding the residents gate let through to be made here, outside a residents re-run
   (townreoffer::PendingSetsAside), the building is recorded by its position key like the gate's own set-aside entries and its
   remaining squads are refused as SET ASIDE (the flag the creation detour reads first). The town's check-up re-offers it only
   once no note of the town holds (townreoffer::Decide) and never fills a building whose home residents live here
   (T392BuildingHasResidents, owedpop::Commit). 1 = set aside (the caller takes the set-aside state step); 0 = a plain refusal.
   The thread the gate's context names. */
static int T591PendingAside(const char* sid, const char* holder)
{
    int sx = -1, sy = -1;
    const int inB = T392PopSector(&sx, &sy);
    const int inRun = townpending::InRun(g_t580RunTid, ::GetCurrentThreadId());
    if (townreoffer::PendingSetsAside(inB, inB != 0 ? (int)g_t392PopAside : 0, inRun, inB != 0 ? (int)g_t591GateMay : 0) == 0) return 0;
    void* const b = g_t392PopBld;
    float bp[3] = { 0, 0, 0 };
    char key[kT392KeyRoom]; key[0] = 0;
    if (b == 0 || TownPosPod(b, bp) == 0 || T392Key(b, key) == 0) { ::InterlockedIncrement64(&g_t591PendAsideNoKey); return 0; }
    /* the entry is the BUILDING's town's (the gate's context, g_t392PopSid), as the gate's own entries are -
       that town's check-up re-offers it and asks that town's notes; `sid` (the creating group's town) only names the refusal */
    const std::string bsid(g_t392PopSid[0] != 0 ? g_t392PopSid : sid);
    T392ResRecord(key, bsid.c_str(), bp[0], bp[2]);
    { TgLockInit(); ::EnterCriticalSection(&g_tgLock); const std::map<std::string, T392ResPend>::iterator e = g_t392Res.find(std::string(key)); if (e != g_t392Res.end()) e->second.pendAside = 1; ::LeaveCriticalSection(&g_tgLock); }   /* counted on the report while it waits */
    ::InterlockedIncrement64(&g_t591PendAside);
    T392DeferLog("residents", bsid, sx, sy, townreoffer::DeferCauseName(townreoffer::kDefPending), key);
    { int c0 = 0; g_t392PopCnt0Ok = T392ResCountPod(g_t392PopTown, &c0); g_t392PopCnt0 = c0; }   /* the town's resident budget is added at the end of the engine call: the building's line reads its add against this */
    std::strncpy(g_t392PopKey, key, 127); g_t392PopKey[127] = 0;
    g_t392PopAsideN = 1;   /* this squad (booked as a notebook-pending refusal); the rest are booked as set aside */
    g_t392PopAside = 1;
    if (::InterlockedIncrement(&g_t591Lines) <= 20)
        DebugLog("[TOWN] set aside kind=residents town=" + bsid + (bsid != sid ? " squadTown=" + std::string(sid) : std::string()) + " sector=" + N((long long)sx) + "," + N((long long)sy) + " building=" + std::string(key)
                 + " cause=notebook-pending holder=" + std::string(holder != 0 ? holder : "?") + " next=town-checkup once no note of the town holds (first 20 lines; pending[...residentsSetAside] counts all)");
    return 1;
}
// the same decision for the bar flies, on the town itself (this)
void detour_barFlies(void* town)
{
    ::InterlockedIncrement64(&g_barFliesSeen);
    const bool offThread = ::GetCurrentThreadId() != g_mainThread;
    if (offThread) ::InterlockedIncrement64(&g_barFliesOff);
    if (!g_on) { ::InterlockedIncrement64(&g_barFliesLeverOff); orig_barFlies(town); return; }
    if (town == 0) { ::InterlockedIncrement64(&g_barFliesNoTown); orig_barFlies(town); return; }
    { int n0 = 0, mk0 = 0; if (RfCountMarkPod(town, &n0, &mk0) != 0 && n0 > 0) RfRememberLen(town, n0); }   /* review-refill1 item 1: a list already armed when the roll is asked for - its length, before any roll zeroes the count */
    /* E38 (e): the bar-residents half of the same defect - a lone game DEFERRED its bar crowd on a leftover
       index. Same RoleIsSingle() precondition, same reason. */
    if (!RoleIsSingle())
    {
        char sidQ[128];
        char holderQ[192];
        if (TownSidPod(town, sidQ, 128) != 0 && TownPendingAsk(town, 0, sidQ, holderQ, 192) == townpending::kTownHeld)
        {
            ::InterlockedIncrement64(&g_barFliesDeferredPending);   /* review-p4x: DEFER, do not destroy - the drain (Town+0x320) is IRREVERSIBLE within a loaded world (decomp_935f90: the list is filled once per Town object), so a pending-notebook refusal only holds the fill off until the notebook people are placed; the held-by-other branch below still drains */
            const std::string keyQ = std::string(sidQ) + " (bar residents, notebook people pending)";
            bool firstQ = false; long long nQ = 0;
            const bool dueQ = PendingRefusalBook(keyQ, &firstQ, &nQ);
            if (firstQ) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + JsonEsc(sidQ) + " (bar residents)\",\"type\":0,\"refused\":1,\"family\":\"bar-residents\",\"cause\":\"notebook-pending-deferred\",\"holder\":\"" + JsonEsc(holderQ) + "\"}");
            if (dueQ) DebugLog(std::string("[TG] town '") + sidQ + "' bar residents deferred as notebook-pending: deferral " + N(nQ) + " for this town, held by note " + holderQ + " (logged at the first deferral, then at most once per 5 minutes)");
            BarPendAdd(town);   /* refill1 fold 2: every deferral path records the town (the refill's owed test, BarPendOwes); cleared when answered (BarPendClear) */
            return;
        }
    }
    const long linkedNow = g_linkedCached;   /* read once: the decision and its booking use the same value (review-p4m) */
    bool refuse = false;
    /* P8a (build/read-host-audit.md C2) - A REFUSAL THAT IS NOT AN ANSWER MUST NOT CONSUME THE LIST.  Town+0x320
       is filled once per Town object and the drain at the bottom of this function is irreversible for the life of
       the world, so the two states that mean "I cannot answer yet" - pre-link and no-map - now DEFER: they leave
       the list alone and the engine's next call can succeed, exactly as the notebook-pending refusal above already
       did.  A refusal that IS an answer about the area (another player holds it, nobody holds it, the engine is
       freeing the world) still drains, which is the engine's own end state. */
    bool deferB = false;
    const char* cause = "not-holder";   /* DECISION 48 (P8a): one rule, one cause; `holder` below says which non-holder state it was */
    const char* holderB = "other";
    /* P6q (review-p6j MEDIUM-4) - THE DECLARATION IS OUTSIDE THE PROBE MARKERS. These six are the decision's own
       inputs, not probe state: AreaViewTS fills them below, MayInventFromView is called with them, and the
       `not-mine` cause test reads one. references/probe-management.md tells a reader to delete from
       `// PROBE-START: <ID>` through `// PROBE-END: <ID>` inclusive, with "no judgment needed and no chance of
       taking adjacent real code with it" - and following that here would have deleted six declarations three later
       statements use, breaking the build. They are hoisted out of the linked arm for the probe's sake, which is why
       the explanation stays inside the markers below; the hoist itself is now ordinary code. */
    int bHeld = -3, bMine = -3, bLoadedHere = -3, bRing1 = -3, bPeerRing1 = -3, bPeerLow = -3;
    // PROBE-START: P027
    /* P027: the view is declared just above, OUTSIDE these markers, so the probe at the refusal can print the
       inputs the decision was actually taken from. -3 means "never read" - the pre-link arm takes no view of the
       area at all, and that is a different fact from an area that answered 0. If P027 is removed, the -3
       initialisation is harmless: the pre-link arm never calls the gate, so nothing decides on it. */
    // PROBE-END: P027
    /* DECISION 48 (P8a): pre-link NOBODY invents, on either game; a lone game (role=single) is exempt because it has
       no notebook and never will.  The same arm as in detour_create - and here the refusal DEFERS. */
    const char* t392Cause = 0; int t392Sx = -1, t392Sy = -1;   /* T-392: set when the linked arm below sets the list aside */
    if (!linkedNow) { refuse = !RoleIsSingle(); deferB = refuse; cause = "no-notebook"; holderB = "-"; }   /* F665: the NOTEBOOK link, and the cause says which link it is */
    else
    {
        float tp[3] = { 0, 0, 0 };
        if (!TownPosPod(town, tp)) { BarPendClear(town); ::InterlockedIncrement64(&g_barFliesReadFault); orig_barFlies(town); return; }   // review-p4f Q2: a read fault allows, counted (never drains)
        const Sector bs = SectorOf(tp[0], tp[2]);
        bHeld = 0; bMine = 0; bLoadedHere = 0; bRing1 = 0; bPeerRing1 = 0; bPeerLow = -2;
        int bPresumedEmpty = 0, bPeerLowFresh = 1;
        AreaViewTS(bs, &bHeld, &bMine, 0, &bLoadedHere, &bRing1, &bPeerRing1, &bPeerLow, &bPresumedEmpty, &bPeerLowFresh);   /* review-p5g Q5: one locked read for the decision AND the cause */   /* E25 re-designed: and for the slot the tie-break compares */   /* P6j: and the empty-table offer */
        const int may = MayInventFromView(bHeld, bMine, bLoadedHere, bRing1, bPeerRing1, MySlotLower(bPeerLow), bPresumedEmpty, bPeerLowFresh);
        /* decision 37 (amended): bar residents are people too - invented where the relay names this game the holder, where nobody holds the area and this game has it loaded, or (E13 attempt 2, `ring1`) where nobody holds it and it is next to this game's own player. The bar list is drained IRREVERSIBLY on a refusal, so this gate is the one that must not refuse during the ~10-12 s window H047 names. */
        /* P6j (verify-p6c MEDIUM-2): kInventTeardown refuses on BOTH sides (it is not an answer about the area, so the
           client/host split does not apply to it) and it is separated from the no-map arm, which is negative as well.
           The outcome is exactly what P6c already produced here - refuse, and drain as the engine would have - so
           nothing about this gate's behaviour changes; only the label on it, and this is the gate whose refusals
           destroy content, so the label is the whole difference between a legible log and a misleading one. */
        /* T-581: a roll this game would make now waits when it is an owed row this game does not hold (made under a claim, from the
           town's check-up), or while the TEST-ONLY lever is on - set aside, never drained */
        int owedB = townreoffer::kDefNone;
        if (may == 1 && (g_owedLever != 0 || OwedAnyWork()))
        {
            char sidO[128]; int clO = 0;
            const int stO = (TownSidPod(town, sidO, 128) != 0) ? OwedStored(owedpop::kKindBar, sidO, &clO) : 0;
            owedB = owedpop::LoadCause(townreoffer::kDefNone, may, (stO == 2 || (stO == 1 && clO != (int)owedpop::kClaimYou)) ? 1 : 0, (int)g_owedLever);
        }
        refuse = (may == 0) || (may == kInventTeardown) || (may == -1) || (owedB != townreoffer::kDefNone);   /* DECISION 48 (P8a): no fresh map refuses on BOTH games - one answer about the area, not one per session role */
        if (may == 1 && owedB == townreoffer::kDefNone) ::InterlockedIncrement64(&g_holderGenerated);
        if (may == 0) { ::InterlockedIncrement64(&g_refusedNotHolder); holderB = (bHeld == 1) ? "other" : "nobody"; }   /* decision 37: nobody holding it is a refusal too - it is not this game's to fill */
        /* T-392 (owner 337 a): the F666 bounded no-map wait is GONE from this gate - "cannot answer yet" always sets the list
           aside. (owner 335 a) So does "nobody holds the area": it is refused (not this game's to fill now) but NOT drained,
           because the drain below is irreversible. Only another game holding the area, or teardown, still drains. The second
           chance is the town's own check-up (T392Reoffer), not a clock. */
        const int dcB = townreoffer::DeferCause(1, may, bHeld);
        if (dcB == townreoffer::kDefNoMap) { cause = "no-map"; holderB = "-"; deferB = true; ::InterlockedIncrement64(&g_refusedNoMap); }   /* DEFERRED, not drained: this is not an answer about the area */
        if (dcB == townreoffer::kDefNobody) deferB = true;
        if (dcB != townreoffer::kDefNone) { t392Cause = townreoffer::DeferCauseName(dcB); t392Sx = bs.x; t392Sy = bs.y; }
        if (owedB != townreoffer::kDefNone)   /* T-581 */
        {
            cause = townreoffer::DeferCauseName(owedB); holderB = "-"; deferB = true; t392Cause = cause; t392Sx = bs.x; t392Sy = bs.y;
            ::InterlockedIncrement64(owedB == townreoffer::kDefOwed ? &g_owedAsideLoad : &g_owedLeverAside);
        }
        if (may == kInventTeardown) { cause = "teardown"; holderB = "-"; ::InterlockedIncrement64(&g_barFliesTeardown); }
    }
    if (!refuse) { BarPendClear(town); ::InterlockedIncrement64(&g_barFliesAllowed); BarFliesWithRecruitMult(town); OwedBarMade(town); return; }   /* an owed roll made here settles at this game's next finished save (OwedBarMade) */   /* recruit3: the engine's roll, plus the hire lists again x (recruitmult - 1) */   /* F666: answered, so it leaves the re-offer list */
    ::InterlockedIncrement64(linkedNow ? &g_barFliesRefused : &g_barFliesRefusedUnlinked);
    // PROBE-START: P027
    /* P027 (verify-p6c HIGH-2): THE PATH THAT DESTROYS CONTENT HAD NO PROBE ON IT. P025 prints the inputs of a
       town-squad refusal, which is recoverable - the engine retries the creation. This gate is the irreversible one:
       Town+0x320 is filled once per Town object, and the drain three lines below zeroes it for the life of the
       world. Same fields as P025 plus the cause and the tie-break slot, so a teardown refusal, an own-row echo
       (mySlot=-1 with peerRing1=1) and a genuine yield to the other player are told apart from one line.
       Capped at 8 per TOWN per session, claimed under g_tgLock like the per-town counters beside it, because this
       runs on worker threads and DebugLog is not thread-safe. */
    {
        char sidP7[128];
        const std::string keyP7 = TownSidPod(town, sidP7, 128) != 0 ? std::string(sidP7) : std::string("?");
        long long nP7 = 0;
        TgLockInit(); ::EnterCriticalSection(&g_tgLock); nP7 = ++g_p027ByTown[keyP7]; ::LeaveCriticalSection(&g_tgLock);
        if (nP7 <= 8)
        {
            char bP7[384];
            _snprintf(bP7, 383, "[PROBE] P027 barflies refused: town='%s' cause=%s holder=%s deferred=%d held=%d mine=%d loadedHere=%d ring1=%d peerRing1=%d peerLowSlot=%d mySlot=%d linked=%d n=%lld",
                      keyP7.c_str(), cause, holderB, deferB ? 1 : 0, bHeld, bMine, bLoadedHere, bRing1, bPeerRing1, bPeerLow, StoreMySlot(), (int)linkedNow, nP7);
            bP7[383] = 0; DebugLog(bP7);
        }
    }
    // PROBE-END: P027
    {
        char sid3[128]; const std::string key3 = (TownSidPod(town, sid3, 128) != 0 ? std::string(sid3) : std::string("?")) + " (bar residents)";   /* review-p4i: per-town, the irreversible one */
        bool first3 = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); first3 = g_refusedTowns.insert(key3).second; ++g_refusedByTown[key3]; ::LeaveCriticalSection(&g_tgLock);
        if (first3) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + key3 + "\",\"type\":0,\"refused\":1,\"family\":\"bar-residents\",\"cause\":\"" + std::string(cause) + "\",\"holder\":\"" + std::string(holderB) + "\",\"deferred\":" + N(deferB ? 1 : 0) + "}");
    }
    /* P8a (C2): DEFERRED means the list SURVIVES.  The engine calls spawnTheBarFlies again, and once this game can
       answer about the area the residents are made or refused for a reason.  Nothing else about this path changes. */
    /* F666: A DEFERRAL NOW HAS A RECURRENCE.  The list survives on the Town object, and this game
       re-offers it on its own tick instead of waiting for a zone activation that may never come
       while the player is standing in the town. */
    if (deferB && t392Cause != 0) { char sidT[128]; T392DeferLog("bar", TownSidPod(town, sidT, 128) != 0 ? std::string(sidT) : std::string("?"), t392Sx, t392Sy, t392Cause, 0); }   /* T-392: first per town */
    if (deferB) { BarPendAdd(town); ::InterlockedIncrement64(&g_barFliesDeferred); return; }
    int dropped = 0;
    const int dr = DrainBarFlyListPod(town, &dropped);
    if (dr == 0) ::InterlockedIncrement64(&g_barFliesDrainFault); else { if (dr == 2) ::InterlockedIncrement64(&g_barFliesDrainOdd); ::InterlockedExchangeAdd64(&g_barFliesDropped, dropped); }   // consumed as the engine would have; the entries are gone (review-p4f MEDIUM-1)
}

/* P8e-b (review-p8e C-2): BarReofferPass IS DELETED.  It was the tree's one call into the engine on
   a path the engine did not start, and it made it through a Town* cached for up to 32 s, from the
   main thread, with no SEH and no test that the town's zone was the one being activated - while the
   engine's own single caller resolves the Town through a handle on the loadPhase2 thread and calls
   only for the zone it is activating.  Town lifetime is not established (review-p4x), and that is
   exactly the fact SEH cannot stand in for.  The re-offer goes back to the engine's own zone
   re-activation; barDeferred[...] in the [TG] line is what says how often that costs anything. */

/* ------------------------------------------------------------------------------------------------------------------
   refill1 (docs/design-refill1.md s2-s4, user-approved 2026-09-26): BARS REFILL EVERY 5 IN-GAME DAYS.
   Kenshi rolls a town's bar once, the first time its zone is created. This post-hook on the engine's own per-town
   check-up, Town::periodicTick 0x92BE50 (main thread, one town per pass, every loaded town - recruit-refill-live.md
   L2), tops a bar back up to its usual size once 5 in-game days have passed since it was last filled:
   - ONLY on the game the relay names as the holder of the town's area (MineHeldTS == 1). The bar roll's own gate
     (MayInventFromView) also lets an unclaimed loaded area through; a timed top-up waits for a named holder instead,
     so two games can never both top one bar up.
   - THE USUAL SIZE (free recruits, hire squads) is counted at the first check-up after the town's first roll under
     the mod (from=firstRoll) or, for a town first seen already rolled, from its live count (from=live, usualFromLive),
     and lives in the notebook's town_bars.txt keyed by the town's stringID (TOWN_BAR 47, store protocol 50,
     src/common/barwire.h) because the holder changes between players. The size reported is max(that count, the list's
     EXPECTED roll - coopbar::BarExpectedSquads x the multiplier x people per squad), because one chance roll can come
     out small, and the notebook keeps the LARGEST size ever reported (review-refill1 item 3).
     refill2 (T374: The Hub usual=1 from=live, expectedSquads=-): a size from a LIVE count first makes the list known the
     way the top-up does (RfLiveListPod: mark 0 -> the engine's own fill on the main thread, its count put back to 0;
     mark 1 with a count and no deferral -> that count; else the remembered length); a list not known yet records
     nothing and waits for a later check-up (usualWaitList - coopbar::BarUsualFromLive).
   - DUE: 5 in-game days (LocalWorldHours, the engine clock every game keeps in step with the notebook's CLOCK) since
     the shared last-filled time; `refill now <town>` skips the wait only.
   - NEARBY RULE (design s3): no top-up while any player character - this game's own or a copy of another player's
     (IsPlayerFaction / IsPeerFaction) - is within 250 units of the bar building, found with the engine's own pick
     0x38AEC0 exactly as spawnTheBarFlies calls it (9fbe50:200). No bar found = skipped, counted noBar. A wait is not
     stamped.
   - THE TOP-UP: one SEH-guarded POD step re-arms the town's bar list and NEVER writes its fill mark +0x110 (review-refill1
     item 1, RfTopUpPod): a Town whose list is not filled this session (a zone loaded from its save - T371) is filled once
     by the engine's own 0x935F90 on the main thread and that list is used; otherwise the roll left the entries in
     place at count 0 (recruit-refill-read R2) and the length remembered for the town re-arms them. It sets each asked
     hire entry's chance to 100 (item 2: the roll adds exactly what is asked), each HIRE entry's count from the
     missing people (coopbar::RefillEntryCounts: ceil(missing / (usual people / usual squads)) squads spread over the hire
     entries, each at most its own count x the recruit multiplier), every other entry to 0 (uniques and non-hire squads
     are never rolled), calls the ENGINE'S spawnTheBarFlies once through its trampoline (orig_barFlies - NOT
     detour_barFlies, whose BarFliesWithRecruitMult would multiply a second time), and the caller restores every count
     and chance and the list count 0 even after a fault. Never a kept Town pointer: the town is the one the engine just handed over.
   - A list count +0x320 != 0 with the fill mark 0 is never touched (owed). With the mark 1 it is owed (untouched, only its
     length remembered - owedDeferred) when the mod deferred that town's roll (g_barPend: every deferral path records it);
     otherwise it is used as it stands (armedNoPend - not expected: a roll leaves the count 0).
   - A 30 s settle after this game starts holding a town comes before any live count (a zone loaded from its save has
     no characters in memory at first - T371), and a walk that did not top up waits 30 s before the next.
     The settle starts over only when a check-up finds this game NOT holding the town (or no fresh map) - never because
     two check-ups were far apart (review-refill1 item 4).
   ------------------------------------------------------------------------------------------------------------------ */
unsigned long long kTownPeriodicUpdateRva = 0; static coop::AddrReg kTownPeriodicUpdateRva_reg("TownPeriodicUpdate", &kTownPeriodicUpdateRva);   /* the address table fills this. Steam_1.0.65 0x92BE50 */   // void Town::periodicTick()
unsigned long long kBarBuildingPickRva = 0; static coop::AddrReg kBarBuildingPickRva_reg("BarBuildingPick", &kBarBuildingPickRva);   /* Steam_1.0.65 0x38AEC0 - spawnTheBarFlies' own bar-building pick (9fbe50:200; a random building of the town's registry entry, 0 = none) */
unsigned long long kBuildingRegistryGlobalRva = 0; static coop::AddrReg kBuildingRegistryGlobalRva_reg("BuildingRegistryGlobal", &kBuildingRegistryGlobalRva);   /* Steam_1.0.65 0x2133568 - the pick's first argument, a global pointer */
unsigned long long kNullHandStaticRva = 0; static coop::AddrReg kNullHandStaticRva_reg("NullHandStatic", &kNullHandStaticRva);   /* Steam_1.0.65 0x1E395F8 - the pick's third argument, a static null hand (its address is passed) */
typedef void (*TownPeriodicFn)(void* town);
TownPeriodicFn orig_townPeriodic = 0;
typedef void* (*BarPickFn)(void* registry, void* town, void* filterHand, void* p4);
bool g_rfHooked = false;
long long g_rfChecks = 0, g_rfDue = 0, g_rfWaitedNearby = 0, g_rfNoBar = 0, g_rfToppedUp = 0, g_rfAddedPeople = 0, g_rfFaults = 0,
          g_rfUsualRecorded = 0, g_rfUsualFromLive = 0, g_rfRecordSendFailed = 0, g_rfOffThread = 0, g_rfNoClock = 0, g_rfOwed = 0,
          g_rfSettling = 0, g_rfFull = 0, g_rfLiveEmpty = 0, g_rfNoList = 0, g_rfSquadsAsked = 0, g_rfLines = 0, g_rfLinesDropped = 0,
          g_rfRowsIn = 0, g_rfRowsBad = 0, g_rfResent = 0, g_rfNoListLen = 0, g_rfArmedNoPend = 0, g_rfOwedDeferred = 0, g_rfUsualWaitList = 0;
const long long kRfLineCap = 400;
const DWORD kRfSettleMs = 30000, kRfRewalkMs = 30000, kRfNearbyMs = 10000, kRfFaultMs = 300000;
const int kRfSquadCap = 64, kRfBarCap = 4;
struct RfSeen { DWORD first; DWORD last; DWORD nextWalk; int holding; };   /* holding 1: every check-up since `first` found this game holding the town (review-refill1 item 4) */
std::map<std::string, RfSeen> g_rfSeen;                  /* MAIN THREAD: per town stringID, how long this game has held it without a gap */
coopbar::BarTable g_rfTable;                            /* MAIN THREAD: the notebook's town_bars rows (and this game's own writes) */
std::map<std::string, coopbar::BarRow> g_rfUnsent;      /* MAIN THREAD: rows written here that could not go to the notebook yet */
std::set<std::string> g_rfForced;                       /* MAIN THREAD: `refill now` targets, lower case, stringID or name */
struct RfForcedNote { DWORD lastLine; int logged; int matched; DWORD whyAt; std::string why; std::string nm; RfForcedNote() : lastLine(0), logged(0), matched(0), whyAt(0) {} };
std::map<std::string, RfForcedNote> g_rfForcedWhy;       /* MAIN THREAD: refill3 (T382) - per forced town (stringID): why its last check-up did not act */

static void RfLine(const std::string& s) { if (g_rfLines >= kRfLineCap) { ++g_rfLinesDropped; return; } ++g_rfLines; DebugLog(s); }
static std::string RfStr(const char* s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) if (o[i] == '\'') o[i] = '`'; return o; }
static std::string RfLower(const std::string& s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a'); return o; }
static int RfGdNamePod(void* gd, char* out, int cap)
{
    __try { return RcNamePod((const char*)gd, out, cap); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
/* (RfListCountPod: replaced by RfCountMarkPod above - the count alone no longer answers "owed", review-refill1 item 1) */
/* 1 = read; *isPlayer 1 = this game's own player faction or any player's stand-in (the copies of other players' characters) */
static int RfCharPod(void* c, int* isPlayer, float* x, float* z)
{
    __try
    {
        ::Character* ch = (::Character*)c;
        ::Faction* f = ch->getOwnerFactionDirect();
        *isPlayer = (f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f))) ? 1 : 0;
        if (*isPlayer != 0) { const Ogre::Vector3 p = ch->worldPosition(); *x = p.x; *z = p.z; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* review-refill1 item 5: 1 = the character's faction is neither this game's player faction nor any stand-in (IsStandInFaction:
   coop-p<n>, or an old save's coop-peer); 0 = it is one; -1 = fault */
static int RfFreeFactionPod(void* c)
{
    __try { ::Faction* f = ((::Character*)c)->getOwnerFactionDirect(); return (f != 0 && (IsPlayerFaction(f) || IsStandInFaction(f))) ? 0 : 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* the engine's own bar pick, called the way spawnTheBarFlies calls it. 1 = a bar and its position, 2 = none, 3 = no position, 0 = fault */
static int RfBarPickPod(void* town, float* x, float* z, void** who)
{
    __try
    {
        const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
        void* registry = *(void* const*)(base + (uintptr_t)kBuildingRegistryGlobalRva);
        *who = 0;
        if (registry == 0) return 2;
        void* b = ((BarPickFn)(base + (uintptr_t)kBarBuildingPickRva))(registry, town, (void*)(base + (uintptr_t)kNullHandStaticRva), 0);
        *who = b;
        if (b == 0) return 2;
        void** vt = *(void***)b; float v[3] = { 0, 0, 0 };
        ((GetPositionFn)vt[8])(b, v);
        if (v[0] == 0.0f && v[2] == 0.0f) return 3;
        *x = v[0]; *z = v[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the town's bar buildings: the pick is random among them, so it is asked kRfBarCap times and the distinct ones kept.
   Returns how many, 0 = none, -1 = a fault or no pick function. */
static int RfBars(void* town, float* bx, float* bz)
{
    if (kBarBuildingPickRva == 0 || kBuildingRegistryGlobalRva == 0 || kNullHandStaticRva == 0) return -1;
    void* seen[kRfBarCap]; int nb = 0;
    for (int k = 0; k < kRfBarCap; ++k)
    {
        float x = 0, z = 0; void* who = 0;
        const int r = RfBarPickPod(town, &x, &z, &who);
        if (r == 0) return -1;
        if (r != 1) { if (k == 0) return 0; continue; }
        int d = 0; for (d = 0; d < nb; ++d) if (seen[d] == who) break;
        if (d == nb) { seen[nb] = who; bx[nb] = x; bz[nb] = z; ++nb; }
    }
    return nb;
}
struct RfCount { int people; int squads; int overflow; int nearPlayer; void* nearWho; float nearX; float nearZ; void* sq[kRfSquadCap]; };   /* refill3: nearWho/X/Z - the player character the nearby rule saw */
/* refill3: a character's own record name (the name the census prints), for the nearby line. 1 = read */
static int RfCharNamePod(void* c, char* out, int cap)
{
    __try { return RcNamePod((const char*)((::Character*)c)->getRecordDirect(), out, cap); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
/* ONE walk of the engine's character update list (P086's walk, confirmed T371): the free recruits of this town - home town
   == town, a squad template that passes coopr::IsHireListName (a hired person leaves the template squad) and a faction
   that is no player's and no stand-in (review-refill1 item 5) - and their
   distinct squads; and, with bars given, whether any player character is within the nearby distance of one.
   Returns the characters walked, -1 = no world / an implausible list. MAIN THREAD. */
static long long RfWalk(void* town, const float* bx, const float* bz, int nb, RfCount* R)
{
    std::memset(R, 0, sizeof(*R));
    if (coop::GameWorldPtr() == 0) return -1;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return -1;
    long long walked = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        void* c = (void*)*it;
        if (c == 0) continue;
        ++walked;
        if (nb > 0 && R->nearPlayer == 0)
        {
            int pl = 0; float x = 0, z = 0;
            if (RfCharPod(c, &pl, &x, &z) != 0 && pl != 0 && coopbar::RefillNearAny(bx, bz, nb, x, z, coopbar::kRefillNearUnits) != 0) { R->nearPlayer = 1; R->nearWho = c; R->nearX = x; R->nearZ = z; }
        }
        void* platoon = 0; void* gd = 0; int st = -1; void* home = 0;
        if (TownCharContextPod(c, &platoon, &gd, &st, &home) == 0 || home != town || gd == 0) continue;
        char nm[96];
        if (RfGdNamePod(gd, nm, 96) == 0 || !coopr::IsHireListName(nm)) continue;
        if (RfFreeFactionPod(c) != 1) continue;   /* review-refill1 item 5: a player's or a stand-in's squad is nobody's free recruit */
        ++R->people;
        int s = 0; for (s = 0; s < R->squads; ++s) if (R->sq[s] == platoon) break;
        if (s == R->squads) { if (R->squads < kRfSquadCap) R->sq[R->squads++] = platoon; else ++R->overflow; }
    }
    return walked;
}
struct RfList { int n; int armed; int wrote; int src; int asked; int orig[kRcMaxEntries]; int want[kRcMaxEntries]; int chance[kRcMaxEntries]; unsigned char hire[kRcMaxEntries]; };
/* THE ONE GUARDED STEP: arm the town's bar list with the missing hire squads and roll it once. It never writes the fill
   mark +0x110 itself:
   - mark 0 (the list not filled this session - a zone loaded from its save: T371 listInit=0 at populate post-orig):
     a count already set = 2 (owed: no init behind it, untouched); otherwise the engine's own initialiseResidentData
     0x935F90 once, on the MAIN thread (the caller's guard). It fills the list and sets the mark itself; the list it just
     filled is used directly (no roll is owed - the refill did the init), and its count is the refill's (wrote = 1).
   - mark 1, count +0x320 != 0: 2 = OWED, untouched, when the mod deferred this town's roll (owedIfArmed = BarPendOwes).
     Otherwise the list is used as it stands (src 2 - the caller counts armedNoPend: a roll leaves the count 0, so a run
     shows whether this ever happens).
   - mark 1, count 0: the roll zeroed only the count; the entries stay at +0x328 (decomp 9fbe50), so the length remembered
     for the town (knownLen) re-arms them. None remembered = 6 (noListLen): the town is skipped.
   The length is bounded by kRcMaxEntries and by the list's capacity +0x324 (94f9c0:15-20 on Town+0x318: count +8 = +0x320,
   capacity +0xC = +0x324, entries 0x98 apart at +0x10 = +0x328).
   Each asked hire entry's chance +0x8C is set to 100 (review-refill1 item 2): every d100 passes (R4), so the roll adds
   exactly the squads asked. `armed` is set just before the first entry write and `wrote` when the count is the refill's
   (its own init's, or the arming write); the caller puts back exactly those (RfRestorePod), fault or not, and nothing else.
   1 = rolled, 0 = fault, 2 = owed, 3 = the length is past the table or the capacity, 4 = the town has no bar list,
   5 = nothing to ask, 6 = no remembered length.
   *lenSeen = a length this step read from the engine (0 = none) - the caller remembers it. */
static int RfTopUpPod(void* town, TownInitResidentsFn init, SpawnTheBarFliesFn roll, int wanted, int mult, int knownLen, int owedIfArmed, RfList* L, int* lenSeen)
{
    L->n = 0; L->armed = 0; L->wrote = 0; L->src = 0; L->asked = 0; *lenSeen = 0;
    __try
    {
        char* t = (char*)town;
        int n = *(const int*)(t + 0x320);
        if (*(const unsigned char*)(t + 0x110) == 0)
        {
            if (n != 0) return 2;
            init(town);   /* main thread = the engine's own load-step thread (T371 P086 offThread=0 over 18 populate calls) */
            n = *(const int*)(t + 0x320);
            if (n <= 0) return 4;
            L->wrote = 1; L->src = 1;
            *lenSeen = n;
        }
        else if (n != 0)
        {
            if (owedIfArmed != 0) { if (n > 0) *lenSeen = n; return 2; }
            L->src = 2;
            if (n > 0) *lenSeen = n;
        }
        else if (knownLen > 0) n = knownLen;
        else return 6;
        const int cap = *(const int*)(t + 0x324);
        if (n <= 0 || n > kRcMaxEntries || n > cap) return 3;
        char* arr = *(char**)(t + 0x328);
        if (arr == 0) return 4;
        L->n = n;
        for (int i = 0; i < n; ++i)
        {
            const char* e = arr + (size_t)i * 0x98;
            L->orig[i] = *(const int*)(e + 0x88);
            L->chance[i] = *(const int*)(e + 0x8C);
            char nm[128];
            L->hire[i] = (RcNamePod(*(const char* const*)e, nm, 128) != 0 && coopr::IsHireListName(nm)) ? 1 : 0;
        }
        L->asked = coopbar::RefillEntryCounts(wanted, L->orig, L->hire, n, mult, L->want);
        if (L->asked <= 0) return 5;
        L->armed = 1;
        for (int i = 0; i < n; ++i)
        {
            char* e = arr + (size_t)i * 0x98;
            *(int*)(e + 0x88) = L->want[i];
            if (L->want[i] > 0) *(int*)(e + 0x8C) = 100;   /* review-refill1 item 2: every d100 passes - exactly the squads asked */
        }
        L->wrote = 1;
        *(int*)(t + 0x320) = n;
        roll(town);   /* the engine's spawnTheBarFlies, once: its own init returns at once (mark 1); it adds squads per entry and never touches the people already there (R2) */
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* every count and chance back as read (armed), the list count 0 (wrote - the engine's own end state); nothing the step did not write */
static int RfRestorePod(void* town, const RfList* L)
{
    __try
    {
        if (L->armed != 0)
        {
            char* arr = *(char**)((char*)town + 0x328);
            if (L->n > 0 && arr == 0) return 0;
            for (int i = 0; i < L->n; ++i) { char* e = arr + (size_t)i * 0x98; *(int*)(e + 0x88) = L->orig[i]; *(int*)(e + 0x8C) = L->chance[i]; }
        }
        if (L->wrote != 0) *(int*)((char*)town + 0x320) = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* review-refill1 item 3: the list's EXPECTED hire squads for ONE engine roll (coopbar::BarExpectedSquads per hire entry,
   its own count and chance as the list holds them - after the first roll every count is back). 1 = read, 0 = fault */
static int RfExpectedPod(void* town, int n, double* out)
{
    *out = 0.0;
    if (n <= 0 || n > kRcMaxEntries) return 0;
    __try
    {
        const char* arr = *(const char* const*)((const char*)town + 0x328);
        if (arr == 0) return 0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const char* e = arr + (size_t)i * 0x98;
            char nm[128];
            if (RcNamePod(*(const char* const*)e, nm, 128) == 0 || !coopr::IsHireListName(nm)) continue;
            sum += coopbar::BarExpectedSquads(*(const int*)(e + 0x88), *(const int*)(e + 0x8C));
        }
        *out = sum;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* refill2 (T374): make the town's bar list KNOWN before a usual size is recorded from a LIVE count, the way RfTopUpPod
   does, and read its expected roll (RfExpectedPod). It never writes the fill mark +0x110:
   - mark 0, count 0 (a zone loaded from its save): the engine's own initialiseResidentData 0x935F90 once, on the MAIN
     thread (the caller's guard); it fills the list and sets the mark itself. The count it set is the refill's (*wrote = 1)
     and the caller puts it back to 0 (RfRestorePod), fault or not - as after a top-up.
   - mark 0, count != 0: 2 (owed, untouched - the caller returns before this).
   - mark 1, count != 0 (not deferred - the caller returns for a deferred one): that count.
   - mark 1, count 0: the length remembered for the town (knownLen); none = 6 (not known yet).
   No init function = 6. The length is bounded by kRcMaxEntries and the capacity +0x324.
   1 = *expected read, 0 = fault, 3 = past the table or the capacity, 4 = no bar list.
   *lenSeen = a length this step read from the engine (0 = none) - the caller remembers it. */
static int RfLiveListPod(void* town, TownInitResidentsFn init, int knownLen, double* expected, int* lenSeen, int* wrote)
{
    *expected = 0.0; *lenSeen = 0; *wrote = 0;
    int n = 0;
    __try
    {
        char* t = (char*)town;
        n = *(const int*)(t + 0x320);
        if (*(const unsigned char*)(t + 0x110) == 0)
        {
            if (n != 0) return 2;
            if (init == 0) return 6;
            init(town);   /* main thread = the engine's own load-step thread (T371), as RfTopUpPod */
            n = *(const int*)(t + 0x320);
            if (n <= 0) return 4;
            *wrote = 1;
            *lenSeen = n;
        }
        else if (n != 0) { if (n > 0) *lenSeen = n; }
        else if (knownLen > 0) n = knownLen;
        else return 6;
        const int cap = *(const int*)(t + 0x324);
        if (n <= 0 || n > kRcMaxEntries || n > cap) return 3;
        if (*(char* const*)(t + 0x328) == 0) return 4;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return RfExpectedPod(town, n, expected) == 1 ? 1 : 0;
}
static bool RfForced(const std::string& sid, const char* name)
{
    if (g_rfForced.empty()) return false;
    return g_rfForced.find(RfLower(sid)) != g_rfForced.end() || g_rfForced.find(RfLower(name)) != g_rfForced.end();
}
static void RfForcedErase(const std::string& sid, const char* name) { g_rfForced.erase(RfLower(sid)); g_rfForced.erase(RfLower(name)); g_rfForcedWhy.erase(sid); }
/* refill3 (T382: `refill now` reached no check-up and nothing said why): a forced town's check-up that does not act says
   why, rate-limited per town (coopbar::ForcedSkipLineDue); the REPORT repeats the last reason for every pending target */
static const char* const kRfOwedWhy = "its bar roll is still owed (the engine's own roll has not run yet) - untouched (owed)";
static void RfForcedSkip(const std::string& sid, const char* nm, const std::string& why)
{
    const DWORD nowMs = ::GetTickCount();
    RfForcedNote& F = g_rfForcedWhy[sid];
    F.why = why; F.whyAt = nowMs; F.nm = nm;
    if (coopbar::ForcedSkipLineDue(nowMs, F.lastLine, F.logged) != 1) return;
    F.lastLine = nowMs; F.logged = 1;
    RfLine("[REFILL] refill now: town '" + RfStr(nm) + "' (" + RfStr(sid.c_str()) + ") skipped at its check-up - " + why);
}
static void RfForcedMatched(const std::string& sid, const char* nm)
{
    RfForcedNote& F = g_rfForcedWhy[sid];
    F.nm = nm; F.why.clear();
    if (F.matched != 0) return;   /* once per request; a later check-up that waits says so through RfForcedSkip */
    F.matched = 1;
    RfLine("[REFILL] refill now: town '" + RfStr(nm) + "' matched at its check-up - due");
}
/* the local table first, then the notebook; a row the notebook could not take waits in g_rfUnsent for RefillTick */
static void RfWrite(const std::string& sid, const coopbar::BarRow& row)
{
    coopbar::BarMerge(&g_rfTable, sid, row);
    const coopbar::BarRow merged = g_rfTable[sid];
    std::vector<coopbar::BarWireRow> one; coopbar::BarWireRow w; w.sid = sid; w.row = merged; one.push_back(w);
    std::vector<char> b;
    if (coopbar::EncodeBarRows(&b, one) && StoreSendTownBar(b)) { g_rfUnsent.erase(sid); return; }
    ++g_rfRecordSendFailed;
    g_rfUnsent[sid] = merged;
}
static void RefillRecordUsual(void* town, const std::string& sid, const char* nm, double now, DWORD nowMs, RfSeen* S)
{
    int n = 0, mark = 0;
    if (RfCountMarkPod(town, &n, &mark) == 0) return;
    if (n != 0 && mark == 0) { ++g_rfOwed; S->nextWalk = nowMs + kRfRewalkMs; return; }   /* a count with no init behind it: not ours to read */
    if (n == 0 && mark == 0 && BarPendOwes(town)) { ++g_rfOwed; ++g_rfOwedDeferred; S->nextWalk = nowMs + kRfRewalkMs; return; }   /* review-refill2: a roll the mod deferred leaves mark 0 / count 0 like a saved zone - filling it here would spend the owed roll */
    if (n != 0)   /* refill1 fold 2: mark 1 - owed iff the mod deferred its roll (the bar is measured after it); otherwise its length, and the bar as it stands */
    {
        RfRememberLen(town, n);
        if (BarPendOwes(town)) { ++g_rfOwed; ++g_rfOwedDeferred; S->nextWalk = nowMs + kRfRewalkMs; return; }
        ++g_rfArmedNoPend;
    }
    bool fresh = false;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); fresh = g_rfFresh.find(sid) != g_rfFresh.end(); ::LeaveCriticalSection(&g_tgLock);
    if (!fresh && nowMs - S->first < kRfSettleMs) { ++g_rfSettling; S->nextWalk = S->first + kRfSettleMs; return; }
    RfCount R;
    if (RfWalk(town, 0, 0, 0, &R) < 0) return;
    if (!fresh && R.people == 0) { ++g_rfLiveEmpty; S->nextWalk = nowMs + kRfRewalkMs; return; }   /* nothing to measure: a live 0 would record the bar as empty for good */
    /* review-refill1 item 3: one chance roll can come out small - the size reported is max(this count, the list's expected
       roll x the multiplier), when the list's length is known */
    double ex = -1.0, e1 = 0.0;
    const int mult = coopr::RecruitMultEffective((int)g_recruitMultCode, ZonesSlotsSeen());
    unsigned up = 0, us = 0;
    if (fresh)
    {
        const int len = RfRememberedLen(sid);
        if (len > 0 && RfExpectedPod(town, len, &e1) == 1) ex = e1 * (double)(mult < 1 ? 1 : mult);
        coopbar::BarUsualFromRoll(R.people, R.squads, ex, &up, &us);
    }
    else
    {
        /* refill2 (T374: The Hub usual=1 from=live, expectedSquads=-): the list is made known first (RfLiveListPod), and a
           list not known yet records nothing - a later check-up asks again (usualWaitList) */
        const TownInitResidentsFn init = (kTownInitResidentsRva != 0) ? (TownInitResidentsFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kTownInitResidentsRva) : 0;
        int lenSeen = 0, wrote = 0;
        const int lk = RfLiveListPod(town, init, RfRememberedLen(sid), &e1, &lenSeen, &wrote);
        bool fault = (lk == 0);
        if (wrote != 0) { RfList Z; Z.n = 0; Z.armed = 0; Z.wrote = 1; if (RfRestorePod(town, &Z) == 0) fault = true; }   /* the count the fill set goes back to 0, as after a top-up; nothing else was written */
        if (lenSeen > 0) RfRememberLen(town, lenSeen);
        if (lk == 1 && !fault) ex = e1 * (double)(mult < 1 ? 1 : mult);
        if (coopbar::BarUsualFromLive(R.people, R.squads, ex, &up, &us) == 0)
        {
            ++g_rfUsualWaitList;
            if (fault) { ++g_rfFaults; S->nextWalk = nowMs + kRfFaultMs; } else S->nextWalk = nowMs + kRfRewalkMs;
            if (g_rfUsualWaitList <= 20 || fault)
                RfLine("[REFILL] usual size for town '" + RfStr(nm) + "' waits: its bar list is not known yet (step=" + N(lk) + (fault ? " fault" : "") + ") counted="
                       + N(R.people) + "/" + N(R.squads) + " - not recorded (usualWaitList)");
            return;
        }
    }
    coopbar::BarRow row; row.usualSet = 1; row.usualPeople = up; row.usualSquads = us; row.lastFilled = now;
    RfWrite(sid, row);
    ++g_rfUsualRecorded; if (!fresh) ++g_rfUsualFromLive;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_rfFresh.erase(sid); ::LeaveCriticalSection(&g_tgLock);
    char eb[32]; if (ex >= 0.0) _snprintf(eb, 31, "%.2f", ex); else std::strcpy(eb, "-"); eb[31] = 0;
    RfLine("[REFILL] usual size recorded town '" + RfStr(nm) + "' people=" + N((long long)up) + " squads=" + N((long long)us) + " counted=" + N(R.people) + "/" + N(R.squads)
           + " expectedSquads=" + std::string(eb) + " from=" + std::string(fresh ? "firstRoll" : "live"));
}
static void RefillCheck(void* town)
{
    if (town == 0 || orig_barFlies == 0) return;
    if (::GetCurrentThreadId() != g_mainThread) { ++g_rfOffThread; return; }
    float tp[3] = { 0, 0, 0 };
    if (TownPosPod(town, tp) == 0) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0 || !coopbar::BarSidOk(sid)) return;
    const std::string key(sid);
    bool zoneLive = false;   /* manager fold: a zone that unloaded and came back must settle again (T371: its people are not in memory at load) */
    {
        int ax[64], ay[64]; const int an = ActiveZoneSectors(ax, ay, 64); const Sector sc = SectorOf(tp[0], tp[2]);   /* par1's reader (final code inside the P085 markers - keep it when P085 goes) */
        for (int i = 0; i < an && i < 64; ++i) if (ax[i] == sc.x && ay[i] == sc.y) { zoneLive = true; break; }
    }
    const int heldTS = zoneLive ? MineHeldTS(SectorOf(tp[0], tp[2])) : 0;   /* refill3: kept for the forced town's skip line */
    if (!zoneLive || heldTS != 1)   /* holder only, zone live */
    {
        if (!g_rfForced.empty())   /* refill3 (T382: The Hub's zone was not live on either game when `refill now` came) */
        {
            char fn[96]; if (RcTownNamePod(town, fn, 96) == 0) std::strcpy(fn, "?");
            if (RfForced(key, fn))
                RfForcedSkip(key, fn, !zoneLive ? std::string("its zone is not live on this game (its map patch is not loaded here) - only the game that holds it with its zone live refills it")
                                                : "this game does not hold its zone (held=" + N(heldTS) + ") - the holder refills it");
        }
        /* review-refill1 item 4: THE ONLY SETTLE RESET - a check-up that finds this game not holding the town (0) or the
           notebook's map stale (-1). Check-ups far apart (a slow pass, a paused game) are not a lost hold. */
        const std::map<std::string, RfSeen>::iterator g = g_rfSeen.find(key);
        if (g != g_rfSeen.end()) g->second.holding = 0;
        return;
    }
    ++g_rfChecks;
    const DWORD nowMs = ::GetTickCount();
    RfSeen& S = g_rfSeen[key];
    if (S.holding == 0) { S.holding = 1; S.first = nowMs; S.nextWalk = 0; }   /* holding since now: the settle starts */
    S.last = nowMs;
    if (S.nextWalk != 0 && (int)(nowMs - S.nextWalk) < 0)
    {
        if (!g_rfForced.empty())
        {
            char fn[96]; if (RcTownNamePod(town, fn, 96) == 0) std::strcpy(fn, "?");
            if (RfForced(key, fn)) RfForcedSkip(key, fn, "its next look is in " + N((long long)((S.nextWalk - nowMs) / 1000)) + " s (a wait its last check-up set)");
        }
        return;
    }
    const double now = LocalWorldHours();
    if (now < 0.0) { ++g_rfNoClock; return; }
    char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?");
    const coopbar::BarTable::const_iterator it = g_rfTable.find(key);
    if (it == g_rfTable.end() || it->second.usualSet == 0)
    {
        if (RfForced(key, nm)) RfForcedSkip(key, nm, "its usual bar size is not recorded yet - it is recorded first; the refill follows at a later check-up");
        RefillRecordUsual(town, key, nm, now, nowMs, &S); return;
    }
    const coopbar::BarRow row = it->second;
    const bool forced = RfForced(key, nm);
    if (coopbar::RefillDue(now, row.lastFilled, forced ? 1 : 0) != 1) return;
    ++g_rfDue;
    if (forced) RfForcedMatched(key, nm);
    if (nowMs - S.first < kRfSettleMs)
    {
        ++g_rfSettling; S.nextWalk = S.first + kRfSettleMs;
        if (forced) RfForcedSkip(key, nm, "settling: held " + N((long long)((nowMs - S.first) / 1000)) + " s of 30 since this game took it");
        return;
    }
    int owed = 0, mark = 0;
    if (RfCountMarkPod(town, &owed, &mark) == 0) { if (forced) RfForcedSkip(key, nm, "its bar list could not be read (fault)"); return; }
    if (owed != 0 && mark == 0) { ++g_rfOwed; S.nextWalk = nowMs + kRfRewalkMs; if (forced) RfForcedSkip(key, nm, kRfOwedWhy); return; }   /* a count with no init behind it: owed, untouched */
    if (owed == 0 && mark == 0 && BarPendOwes(town)) { ++g_rfOwed; ++g_rfOwedDeferred; S.nextWalk = nowMs + kRfRewalkMs; if (forced) RfForcedSkip(key, nm, kRfOwedWhy); return; }   /* review-refill2: a deferred roll (mark 0, count 0) is owed - the top-up must not fill it either */
    if (owed != 0 && BarPendOwes(town)) { RfRememberLen(town, owed); ++g_rfOwed; ++g_rfOwedDeferred; S.nextWalk = nowMs + kRfRewalkMs; if (forced) RfForcedSkip(key, nm, kRfOwedWhy); return; }   /* refill1 fold 2: mark 1 and the mod deferred this town's roll - OWED, untouched (RfTopUpPod) */
    float bx[kRfBarCap], bz[kRfBarCap];
    const int nb = RfBars(town, bx, bz);
    if (nb <= 0) { ++g_rfNoBar; S.nextWalk = nowMs + kRfRewalkMs; if (forced) RfLine("[REFILL] refill now: town '" + RfStr(nm) + "' - no bar building found" + (nb < 0 ? " (the pick faulted or is not in the address table)" : "") + "; skipped (noBar)"); return; }
    RfCount R;
    if (RfWalk(town, bx, bz, nb, &R) < 0) { if (forced) RfForcedSkip(key, nm, "the character list could not be walked"); return; }
    if (R.nearPlayer != 0)   /* not stamped: retried on a later check-up */
    {
        ++g_rfWaitedNearby; S.nextWalk = nowMs + kRfNearbyMs;
        char pn[96]; if (R.nearWho == 0 || RfCharNamePod(R.nearWho, pn, 96) != 1) std::strcpy(pn, "?");
        char pb[64]; _snprintf(pb, 63, "%.0f,%.0f", R.nearX, R.nearZ); pb[63] = 0;
        const std::string why = "the nearby rule: player character '" + RfStr(pn) + "' at " + std::string(pb) + " is within " + N((long long)coopbar::kRefillNearUnits)
                                + " of its bar - retried in " + N((long long)(kRfNearbyMs / 1000)) + " s (waitedNearby)";
        if (forced) RfForcedSkip(key, nm, why); else if (g_rfWaitedNearby <= 20) RfLine("[REFILL] town '" + RfStr(nm) + "' waits: " + why);
        return;
    }
    const int wanted = coopbar::RefillSquadsWanted((int)row.usualPeople, (int)row.usualSquads, R.people);
    if (wanted <= 0)
    {
        ++g_rfFull; S.nextWalk = nowMs + kRfRewalkMs;
        if (forced) { RfForcedErase(key, nm); RfLine("[REFILL] refill now: town '" + RfStr(nm) + "' is full usual=" + N((long long)row.usualPeople) + " free=" + N(R.people) + " - nothing added"); }
        return;
    }
    if (kTownInitResidentsRva == 0) { ++g_rfFaults; S.nextWalk = nowMs + kRfFaultMs; if (forced) RfForcedSkip(key, nm, "the address table has no engine fill (fault)"); return; }
    const TownInitResidentsFn init = (TownInitResidentsFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kTownInitResidentsRva);
    const int mult = coopr::RecruitMultEffective((int)g_recruitMultCode, ZonesSlotsSeen());
    RfList L; int lenSeen = 0;
    const int rc = RfTopUpPod(town, init, orig_barFlies, wanted, mult, RfRememberedLen(key), BarPendOwes(town) ? 1 : 0, &L, &lenSeen);   /* main thread (guard above), as the engine's own load step (T371) */
    if (L.src == 2) ++g_rfArmedNoPend;
    if (lenSeen > 0) RfRememberLen(town, lenSeen);
    bool fault = (rc == 0);
    if (L.armed != 0 || L.wrote != 0) { if (RfRestorePod(town, &L) == 0) fault = true; }   /* refill1 fold 2: what the step wrote goes back (counts and chances; the list count 0 only if it wrote it) - even after a fault; a step that wrote nothing is left as found */
    if (fault)
    {
        ++g_rfFaults; S.nextWalk = nowMs + kRfFaultMs;
        RfLine("[REFILL] FAULT town '" + RfStr(nm) + "' step=" + N(rc) + " armed=" + N(L.armed) + " - the list was put back; the timer is not stamped");
        return;
    }
    if (rc == 2) { ++g_rfOwed; if (BarPendHas(town)) ++g_rfOwedDeferred; S.nextWalk = nowMs + kRfRewalkMs; if (forced) RfForcedSkip(key, nm, kRfOwedWhy); return; }
    if (rc == 6) { ++g_rfNoListLen; S.nextWalk = nowMs + kRfFaultMs; RfLine("[REFILL] town '" + RfStr(nm) + "' - its bar list is initialised but its length was never seen this session - skipped (noListLen)"); return; }
    if (rc != 1)
    {
        ++g_rfNoList; S.nextWalk = nowMs + kRfFaultMs;
        RfLine("[REFILL] town '" + RfStr(nm) + "' has no hire list to roll (step=" + N(rc) + " entries=" + N(L.n) + ") - nothing added");
        return;
    }
    RfCount R2;
    const long long w2 = RfWalk(town, 0, 0, 0, &R2);
    const int added = (w2 >= 0) ? R2.people - R.people : 0;
    ++g_rfToppedUp; if (added > 0) g_rfAddedPeople += added; g_rfSquadsAsked += L.asked;
    coopbar::BarRow st = row; st.lastFilled = now;
    RfWrite(key, st);
    S.nextWalk = nowMs + kRfRewalkMs;
    if (forced) RfForcedErase(key, nm);
    RfLine("[REFILL] town '" + RfStr(nm) + "' usual=" + N((long long)row.usualPeople) + " free=" + N(R.people) + " added=" + N(added) + " squads=" + N(L.asked));
}
/* ------------------------------------------------------------------------------------------------------------------
   towns2: A LOADED TOWN'S GROUPS ARE REBUILT EVERY 5 IN-GAME DAYS EXACTLY AS THE GAME REBUILDS THEM WHEN A PLAYER COMES BACK.
   The game rebuilds a town group in two steps: when the town comes into loaded range its marker 0x929150 sets the rebuild
   mark Platoon+0x118 = 1 on the groups it picks, and a marked group is rebuilt whole from its recipe when it next wakes
   (ActivePlatoon::setupCheck 0x4FF040 -> createRandomSquad 0x582F80). In a town that stays loaded neither step happens, so
   the game that HOLDS the town does both, every 5 in-game days:
   - DECIDE in the refill's own Town::periodicTick 0x92BE50 post-hook (main thread): holder only with the zone live, every
     120 in-game hours per town (coopt2::RebuildDue; the timer is this holder's own, g_t2Last). Skipped whole when the town is
     dead (population manager byte +8 - the marker's own skip) or its version may have changed since it came into range
     (coopt2::VersionMayChange: the town has "override town" versions and a named character's state changed
     since - the game would pick the version again first, so the old version's groups are never rebuilt). The walk reads
     BOTH lists of the town's population manager Town+0xE0 (vt+0x8 0x94BBA0: residents type 1, patrols type 2) and runs
     coopt2::MarkerPass - the marker's own rule and budget arithmetic - on what it read (living as 0x7923F0: awake, members
     not Character::hasDied; asleep, the stored +0xA0). A group is a pick when the marker picks it or its mark is already 1
     (the engine's own Platoon::declareDead marks a regenerating group whose leader died). No mark is written here: a
     pick is QUEUED (by a copy of its hand - never a kept pointer) when coopt2::GroupVerdict lets it through the refusals that
     keep the sleep and wake safe in a shared world: declared dead (+0x1F0), asleep, nobody alive in a group the engine did
     not keep, a LIVING unique member, a recipe unique this game would make (below), a jailed, carried or player member, a member in a zone this game does not hold, and
     sight (no player character within coopt2::kSightUnits of a member, living or dead, or of the group's building).
   - UNIQUES: the rebuild itself cannot make a unique that was ever made or loaded on this game: createCharacter 0x582C50
     asks 0x591720, which refuses any unique whose state-map entry holds its template (+8; set when it is made and at load,
     never cleared at death), and builds its "unique replacement spawn" instead; a DEAD state arriving from another game marks
     the entry the same way (worldstate.cpp WsApplyPod). So a dead unique never comes back through a rebuild. A unique another
     game made and holds alive has no filled entry here, so a group whose recipe (the squad template Platoon+0x108, its six
     member lists) holds a template flagged "unique" without a filled entry on this game is not rebuilt (T2RecipeUnique,
     coopt2::RecipeUniqueBlocks: verdict uniqueUnknown). The squad template of every "slaves" / "prisoners" entry, which
     createRandomSquad 0x582F80 also runs, adds its member lists; one not found or read, or with sub-squads of its own,
     refuses (coopt2::SubSquadsBlock: verdict subSquads).
   - ACT at the START of Faction::updateActivePlatoons 0x6BA810 (pre-hook, main thread, before the engine walks its lists),
     for a queued group of THAT faction (Ownerships+0x70): re-resolve the hand, re-check every refusal and sight LIVE, then
     in ONE SEH frame: +0x118 = 1, Platoon::deactivate(p, 0) 0x7EE910, Faction::deactivatePlatoon(f, p) 0x7F26A0,
     Platoon::activate(p) 0x7EB250, Faction::activatePlatoon(f, p) 0x7F2A30 - the engine's own sleep pair (6BA810:76-77) and
     wake pair (6B9620:16-17). The engine's loop then updates the new ActivePlatoon and setupCheck rebuilds it. TownGenTick
     then watches the group: mark back to 0 with living members = `rebuilt`.
   TEST verb `townres` (alias `towns2`): `townres now <town> [nosight]` skips the 5-day wait for one town (nosight: the sight
   rule is ignored for that walk and its commitment), `townres kill <town> <n> [res|pat] [regen|noregen] [all]` kills n
   living members of the holder's first matching group through the engine's own Character::declareDead 0x7A5660,
   `townres show <town>` counts the town's awake groups per list on the holder. Every group line names the group's world id.
   ------------------------------------------------------------------------------------------------------------------ */
unsigned long long kFactionUpdateActivePlatoonsRva = 0; static coop::AddrReg kFactionUpdateActivePlatoonsRva_reg("FactionUpdateActivePlatoons", &kFactionUpdateActivePlatoonsRva);   /* Steam_1.0.65 0x6BA810 */   // void Faction::updateActivePlatoons(float dt) - 0x6BA9B0 passes dt in xmm1
unsigned long long kPlatoonDeactivateRva = 0; static coop::AddrReg kPlatoonDeactivateRva_reg("Deactivate", &kPlatoonDeactivateRva);   /* Steam_1.0.65 0x7EE910 - the row store.cpp hooks (detour_deactivate): the call goes through the mod's own sleep record */   // void Platoon::deactivate(GameDataContainer* forceCharacterStates) - the engine's sleep passes 0
unsigned long long kFactionDeactivatePlatoonRva = 0; static coop::AddrReg kFactionDeactivatePlatoonRva_reg("FactionDeactivatePlatoon", &kFactionDeactivatePlatoonRva);   /* Steam_1.0.65 0x7F26A0 */   // void Faction::deactivatePlatoon(Platoon*)
unsigned long long kPlatoonActivateRva = 0; static coop::AddrReg kPlatoonActivateRva_reg("Activate", &kPlatoonActivateRva);   /* Steam_1.0.65 0x7EB250 - the row store.cpp hooks (detour_activate): the call goes through the mod's own wake */   // void Platoon::activate()
unsigned long long kFactionActivatePlatoonRva = 0; static coop::AddrReg kFactionActivatePlatoonRva_reg("FactionActivatePlatoon", &kFactionActivatePlatoonRva);   /* Steam_1.0.65 0x7F2A30 */   // void Faction::activatePlatoon(Platoon*)
typedef void (*T2FactionUpdateFn)(void* faction, float dt);
typedef char* (*T2BucketFn)(void* mgr, int type);
typedef void (*T2PlatoonDeactivateFn)(void* platoon, void* forceCharacterStates);
typedef void (*T2FactionPlatoonFn)(void* faction, void* platoon);
typedef void (*T2PlatoonActivateFn)(void* platoon);
typedef const char* (*T2TownGameDataFn)(void* town);
T2FactionUpdateFn orig_t2FactionUpdate = 0;
bool g_t2Hooked = false;
const int kT2Mem = 256, kT2Nodes = 256, kT2Players = 128, kT2HandBytes = 0x20;
const size_t kT2QueueCap = 64;
const DWORD kT2QueueMaxMs = 120000, kT2VerifyMaxMs = 60000, kT2ForcedLineMs = 30000;
const long long kT2LineCap = 400;
const int kT2KillPat = 1 << 12, kT2KillRegen = 1 << 13, kT2KillNoRegen = 1 << 14, kT2KillAll = 1 << 15, kT2KillCountMask = 0xFFF;
enum { kT2xGone = 0, kT2xFault, kT2xNotHolder, kT2xNoFactionTurn, kT2xNoAddr, kT2xStepFault, kT2xRebuildNotSeen, kT2xQueueFull, kT2xN };
const char* const kT2xName[kT2xN] = { "gone", "readFault", "notHolder", "noFactionTurn", "noAddr", "stepFault", "rebuildNotSeen", "queueFull" };
struct T2Q { unsigned char h[kT2HandBytes]; std::string town; std::string list; std::string wid; int idx; int orig; int living; int nosight; DWORD at; };
struct T2V { unsigned char h[kT2HandBytes]; std::string town; std::string list; std::string wid; int idx; int origBefore; int livingBefore; DWORD at; };
std::vector<T2Q> g_t2Queue;                 /* MAIN THREAD: groups to rebuild, by a copy of their hand */
std::vector<T2V> g_t2Verify;                /* MAIN THREAD: groups slept and woken, watched until the rebuild is seen */
std::map<std::string, double> g_t2Last;     /* MAIN THREAD: per town stringID, the in-game hour of THIS holder's last walk */
std::map<std::string, long long> g_t2GenOut;   /* MAIN THREAD: per town stringID, the named-character state generation when the town was last seen out of loaded range */
long long g_t2GenWorld = 0;                 /* MAIN THREAD: the generation when the last world ended (the stamp of a town never seen out of range) */
std::map<std::string, int> g_t2Forced;      /* MAIN THREAD: `townres now` targets, lower case stringID or name; 1 = nosight */
std::map<std::string, int> g_t2Kill;        /* MAIN THREAD: `townres kill` targets, lower case stringID or name; count | kT2Kill* flags */
std::map<std::string, int> g_t2Show;        /* MAIN THREAD: `townres show` targets, lower case stringID or name */
std::map<std::string, DWORD> g_t2WhyAt;     /* MAIN THREAD: a forced target's last skip line (rate limit) */
long long g_t2Checks = 0, g_t2Due = 0, g_t2Walks = 0, g_t2WalkFault = 0, g_t2Groups = 0, g_t2Picked = 0, g_t2PickedMarker = 0, g_t2PickedEngine = 0, g_t2Queued = 0, g_t2Asked = 0, g_t2Rebuilt = 0,
          g_t2Kills = 0, g_t2Killed = 0, g_t2Lines = 0, g_t2LinesDropped = 0, g_t2OffThread = 0, g_t2NoClock = 0, g_t2ActFaults = 0, g_t2TownDead = 0, g_t2VersionWait = 0;
long long g_t2Verdict[coopt2::kVerdicts];
long long g_t2Skip[kT2xN];

static void T2Line(const std::string& s) { if (g_t2Lines >= kT2LineCap) { ++g_t2LinesDropped; return; } ++g_t2Lines; DebugLog(s); }
static std::string T2GroupLine(const std::string& town, const std::string& list, int idx, const std::string& wid, int living, int orig, const std::string& what)
{
    return "[TOWNS2] town '" + town + "' " + list + " " + N((long long)idx) + " id=" + wid + " living=" + N((long long)living) + "/" + N((long long)orig) + " -> " + what;
}
/* The group's world id (Platoon+0x78, the store's StoreWorldIdOf); "?" when unreadable */
static std::string T2Wid(void* platoon) { const std::string s = coop::StoreWorldIdOf(platoon); return s.empty() ? std::string("?") : s; }
/* One group, read through a copy of its hand. ok: 1 read, 2 the hand no longer resolves, 3 an implausible member list, 0 fault */
struct T2Grp
{
    int ok; void* platoon; void* faction; void* leader; int members; int orig; int sepType; int canRefresh; int mark; coopt2::GroupFacts f;
    int nl; void* live[kT2Mem]; float px[kT2Mem]; float pz[kT2Mem]; int nd; float dx[kT2Mem]; float dz[kT2Mem]; int hasB; float bx; float bz;
};
static void T2GroupPod(const void* h, T2Grp* G)
{
    std::memset(G, 0, sizeof(*G));
    __try
    {
        void* p = (void*)((const hand*)h)->getSquad();   /* 0x791CF0: types 1 / 0x22 only, index and serial checked */
        if (p == 0) { G->ok = 2; return; }
        const char* P = (const char*)p;
        G->platoon = p;
        G->orig = *(const int*)(P + 0xA4);
        G->sepType = *(const int*)(P + 0xC0);
        G->canRefresh = *(const unsigned char*)(P + 0xD8);
        G->mark = *(const int*)(P + 0x118);
        G->f.dead = *(const unsigned char*)(P + 0x1F0);
        G->f.regenerates = *(const unsigned char*)(P + 0xD9) != 0 ? 1 : 0;
        G->faction = *(void* const*)(P + 0x148 + 0x70);   /* Ownerships (Platoon+0x148) ::faction +0x70 */
        const char* ht = *(const char* const*)(P + 0x148 + 0x30);   /* Ownerships::homeTown +0x30 (Platoon::declareDead reads it) */
        if (ht != 0)
        {
            G->f.homeTown = 1;
            const char* hm = *(const char* const*)(ht + 0xE0);
            G->f.homeTownDead = (hm != 0 && *(const unsigned char*)(hm + 8) != 0) ? 1 : 0;   /* TownBase vt+0x288 0x9265A0 */
        }
        const char* ap = *(const char* const*)(P + 0x1D8);
        if (ap != 0 && *(const unsigned char*)(ap + 0xF0) != 0)
        {
            G->f.awake = 1;
            G->leader = *(void* const*)(ap + 0xA0);
            const unsigned n = *(const unsigned*)(ap + 0x58);
            ::Character* const* mem = *(::Character* const* const*)(ap + 0x60);
            if (n > 512 || (n != 0 && mem == 0)) { G->ok = 3; return; }
            G->members = (int)n;
            for (unsigned i = 0; i < n; ++i)
            {
                ::Character* c = mem[i];
                if (c == 0) continue;
                if (*((const unsigned char*)c + 0x3D4) != 0) G->f.carried = 1;   /* _isBeingCarried (spawn.cpp kBeingCarriedOff) */
                ::Faction* fc = c->getOwnerFactionDirect();
                if (fc != 0 && (IsPlayerFaction(fc) || IsStandInFaction(fc))) G->f.player = 1;
                if (*((const unsigned char*)c + 0x2F8) == 2) G->f.jailed = 1;   /* imprisoned (the engine collects it into Platoon+0x1F1 on deactivate) */
                if (c->hasDied())
                {
                    if (G->nd < kT2Mem) { const Ogre::Vector3 dv = c->worldPosition(); G->dx[G->nd] = dv.x; G->dz[G->nd] = dv.z; ++G->nd; }   /* a corpse someone may be looting counts for sight */
                    continue;
                }
                if (KillNamedIsUniquePod(c) != 0) G->f.uniqueAlive = 1;   /* 1 unique, -1 the test faulted: both refuse */
                ++G->f.living;
                if (G->nl < kT2Mem) { const Ogre::Vector3 v = c->worldPosition(); G->live[G->nl] = c; G->px[G->nl] = v.x; G->pz[G->nl] = v.z; ++G->nl; }
            }
        }
        else G->f.living = *(const int*)(P + 0xA0);
        void* b = (void*)((const hand*)(P + 0x148 + 0x38))->asBuilding();   /* the resident building's hand, Ownerships+0x38 */
        if (b != 0)
        {
            void** vt = *(void***)b; float v[3] = { 0, 0, 0 };
            ((GetPositionFn)vt[8])(b, v);
            if (!(v[0] == 0.0f && v[2] == 0.0f)) { G->hasB = 1; G->bx = v[0]; G->bz = v[2]; }
        }
        G->ok = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { G->ok = 0; }
}
/* Only the group's faction (Ownerships+0x70), for the per-faction match: 1 read, 2 the hand no longer resolves, 0 fault */
static int T2FactionOfPod(const void* h, void** f)
{
    *f = 0;
    __try
    {
        void* p = (void*)((const hand*)h)->getSquad();
        if (p == 0) return 2;
        *f = *(void* const*)((const char*)p + 0x148 + 0x70);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* One list (type 1 residents, type 2 patrols) of the town's population manager: copies of up to `cap` node hands and the
   list's float budget (+0xC). 1 read, 2 no manager, 0 fault. MAIN THREAD */
static int T2NodesPod(void* town, int type, unsigned char (*hs)[kT2HandBytes], int cap, int* total, float* budget)
{
    *total = 0; *budget = 0.0f;
    __try
    {
        char* mgr = *(char* const*)((const char*)town + 0xE0);
        if (mgr == 0) return 2;
        char* b = ((T2BucketFn)((*(void* const* const*)mgr)[1]))(mgr, type);   /* manager vt+0x8 = 0x94BBA0: manager + 0x10 + type * 0x78 */
        if (b == 0) return 0;
        *budget = *(const float*)(b + 0xC);
        if (*(const long long*)(b + 0x58) == 0) return 1;
        const char* const* arr = *(const char* const* const*)(b + 0x70);
        const char* node = arr[*(const long long*)(b + 0x50)];
        int n = 0;
        while (node != 0 && n < 4096)
        {
            if (n < cap) std::memcpy(hs[n], node + 0x10, kT2HandBytes);
            ++n;
            node = *(const char* const*)node;
        }
        *total = n;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The town's own flags: *active = Town+0x1AC (set by _NV_activate, cleared by deActivationCheck), *dead = its population
   manager's byte +8 (TownBase vt+0x288 0x9265A0). 1 read, 0 fault */
static int T2TownFlagsPod(void* town, int* active, int* dead)
{
    *active = 0; *dead = 0;
    __try
    {
        *active = *((const unsigned char*)town + 0x1AC) != 0 ? 1 : 0;
        const char* mgr = *(const char* const*)((const char*)town + 0xE0);
        *dead = (mgr != 0 && *(const unsigned char*)(mgr + 8) != 0) ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 = the town's game data (Town vt+0x18 _NV_getGameData 0x9510A0: its current version's record) has a non-empty
   "override town" list - the versions checkForRepopulateTown 0x9FE6D0 chooses from; 0 = none; -1 = unreadable.
   The list map at GameData+0x2B8: count +0x20, buckets +0x38 from +0x18; node: next +0, name +0x10, entries +0x38..+0x40. */
static int T2TownHasVersionsPod(void* town)
{
    __try
    {
        const char* gd = ((T2TownGameDataFn)((*(void* const* const*)town)[0x18 / 8]))(town);
        if (gd == 0) return -1;
        const char* m = gd + 0x2B8;
        if (*(const unsigned long long*)(m + 0x20) == 0) return 0;
        const char* node = *(const char* const*)(*(const char* const*)(m + 0x38) + *(const long long*)(m + 0x18) * 8);
        for (int guard = 0; node != 0 && guard < 4096; ++guard, node = *(const char* const*)node)
        {
            const char* s = node + 0x10;
            const unsigned long long len = *(const unsigned long long*)(s + 0x10), cap = *(const unsigned long long*)(s + 0x18);
            const char* txt = (cap > 15) ? *(const char* const*)s : s;
            if (len == 13 && std::memcmp(txt, "override town", 13) == 0)
                return (*(const char* const*)(node + 0x40) != *(const char* const*)(node + 0x38)) ? 1 : 0;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* Player characters' positions: this game's own and every copy of another player's (RfCharPod). -1 = no world. MAIN THREAD */
static int T2Players(float* x, float* z, int cap)
{
    if (coop::GameWorldPtr() == 0) return -1;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return -1;
    int n = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        void* c = (void*)*it;
        if (c == 0) continue;
        int pl = 0; float px = 0, pz = 0;
        if (RfCharPod(c, &pl, &px, &pz) != 0 && pl != 0) { if (n >= cap) return -1; x[n] = px; z[n] = pz; ++n; }   /* more player characters than the table = treated as seen (np < 0) */
    }
    return n;
}
/* 1 = a player character is within the sight radius of a member (living or dead) or of the building; no world reads as seen */
static int T2Seen(int np, const float* plx, const float* plz, const T2Grp* G)
{
    if (np < 0) return 1;
    if (coopt2::WithinAny(plx, plz, np, G->px, G->pz, G->nl, coopt2::kSightUnits) != 0) return 1;
    if (coopt2::WithinAny(plx, plz, np, G->dx, G->dz, G->nd, coopt2::kSightUnits) != 0) return 1;
    if (G->hasB != 0 && coopt2::WithinAny(plx, plz, np, &G->bx, &G->bz, 1, coopt2::kSightUnits) != 0) return 1;
    return 0;
}
/* 1 = this game holds the area at x,z with its zone live (RefillCheck's test); -2 = the zone is not live here; else MineHeldTS */
static int T2HeldHere(float x, float z)
{
    int ax[64], ay[64]; const int an = ActiveZoneSectors(ax, ay, 64); const Sector sc = SectorOf(x, z);
    bool live = false;
    for (int i = 0; i < an && i < 64; ++i) if (ax[i] == sc.x && ay[i] == sc.y) { live = true; break; }
    if (!live) return -2;
    return MineHeldTS(sc);
}
/* 1 = every member of the group (living and dead) stands where this game holds the zone with it live - a patrol can be far
   from its town; a group with no member read is judged by its building, else by the town's position */
static int T2AllHeld(const T2Grp* G, const float* tp)
{
    int ax[64], ay[64]; const int an = ActiveZoneSectors(ax, ay, 64);
    int points = 0;
    for (int k = 0; k < 2; ++k)
    {
        const int n = (k == 0) ? G->nl : G->nd;
        const float* xs = (k == 0) ? G->px : G->dx; const float* zs = (k == 0) ? G->pz : G->dz;
        for (int i = 0; i < n; ++i)
        {
            ++points;
            const Sector sc = SectorOf(xs[i], zs[i]);
            bool live = false;
            for (int a = 0; a < an && a < 64; ++a) if (ax[a] == sc.x && ay[a] == sc.y) { live = true; break; }
            if (!live || MineHeldTS(sc) != 1) return 0;
        }
    }
    if (points != 0) return 1;
    if (G->hasB != 0) return T2HeldHere(G->bx, G->bz) == 1 ? 1 : 0;
    if (tp == 0) return 0;
    return T2HeldHere(tp[0], tp[2]) == 1 ? 1 : 0;
}
static bool T2SameHand(const unsigned char* a, const unsigned char* b) { return std::memcmp(a + 8, b + 8, kT2HandBytes - 8) == 0; }   /* type, container, serials, index - not the vtable word */
static std::map<std::string, int>::iterator T2Find(std::map<std::string, int>& m, const std::string& sid, const char* nm)
{
    std::map<std::string, int>::iterator it = m.find(RfLower(sid));
    if (it == m.end()) it = m.find(RfLower(nm));
    return it;
}
static void T2ForcedSkip(const std::string& key, const std::string& tn, const std::string& why)
{
    const DWORD nowMs = ::GetTickCount();
    std::map<std::string, DWORD>::iterator w = g_t2WhyAt.find(key);
    if (w != g_t2WhyAt.end() && nowMs - w->second < kT2ForcedLineMs) return;
    g_t2WhyAt[key] = nowMs;
    T2Line("[TOWNS2] town '" + tn + "' (a `townres` TEST target) skipped at its check-up - " + why);
}
static const char* T2ListName(int type) { return type == 2 ? "patrol" : "resident"; }
/* The group's recipe: its squad template Platoon+0x108 (ActivePlatoon::setupCheck 0x4FF040 hands it to createRandomSquad
   0x582F80). A template's reference lists live in its map +0x2B8 (node: next +0, name +0x10, entries +0x38..+0x40); an entry
   is 0x40 bytes - the referenced stringID at +0x10 and the resolved record at +0x38, 0 until the engine's 0xB6470 first
   resolves it by that stringID. T2TplPod walks one template: every entry of its member lists (coopt2::IsMemberList) is added
   at ptr[*n] - the resolved record, or 0 with the stringID copied into sid[*n]; every entry of its sub-squad lists
   (coopt2::IsSubSquadList) is added the same way at sub[*ns], or, when sub is 0, only counted into *ns. Returns a
   coopt2::RecipeStep: kRecipeRead; kRecipeNoTemplate (tpl 0); kRecipeOverCap (more entries than cap or scap);
   kRecipeLongSid (an unresolved stringID of kT2SidChars characters or more); kRecipeFault (a fault or a malformed entry
   range). MAIN THREAD */
const int kT2Recipe = 64, kT2SidChars = 96, kT2Subs = 16;
static int T2TplPod(const char* tpl, void** ptr, char (*sid)[kT2SidChars], int cap, int* n, void** sub, char (*ssid)[kT2SidChars], int scap, int* ns)
{
    if (tpl == 0) return coopt2::kRecipeNoTemplate;
    __try
    {
        const char* m = tpl + 0x2B8;
        if (*(const unsigned long long*)(m + 0x20) == 0) return coopt2::kRecipeRead;
        const char* node = *(const char* const*)(*(const char* const*)(m + 0x38) + *(const long long*)(m + 0x18) * 8);
        for (int guard = 0; node != 0 && guard < 4096; ++guard, node = *(const char* const*)node)
        {
            const char* s = node + 0x10;
            const unsigned long long len = *(const unsigned long long*)(s + 0x10), sc = *(const unsigned long long*)(s + 0x18);
            const char* txt = (sc > 15) ? *(const char* const*)s : s;
            const int member = coopt2::IsMemberList(txt, len);
            if (member == 0 && coopt2::IsSubSquadList(txt, len) == 0) continue;
            const char* b = *(const char* const*)(node + 0x38);
            const char* e = *(const char* const*)(node + 0x40);
            if (e < b || ((e - b) % 0x40) != 0) return coopt2::kRecipeFault;
            if (member == 0 && sub == 0) { *ns += (int)((e - b) / 0x40); continue; }
            void** P = (member != 0) ? ptr : sub;
            char (*S)[kT2SidChars] = (member != 0) ? sid : ssid;
            int* c = (member != 0) ? n : ns;
            const int lim = (member != 0) ? cap : scap;
            for (const char* el = b; el < e; el += 0x40)
            {
                if (*c >= lim) return coopt2::kRecipeOverCap;
                P[*c] = *(void* const*)(el + 0x38);
                S[*c][0] = 0;
                if (P[*c] == 0)
                {
                    const char* es = el + 0x10;
                    const unsigned long long el2 = *(const unsigned long long*)(es + 0x10), ec = *(const unsigned long long*)(es + 0x18);
                    if (el2 >= (unsigned long long)kT2SidChars) return coopt2::kRecipeLongSid;
                    const char* et = (ec > 15) ? *(const char* const*)es : es;
                    std::memcpy(S[*c], et, (size_t)el2);
                    S[*c][el2] = 0;
                }
                ++*c;
            }
        }
        return coopt2::kRecipeRead;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return coopt2::kRecipeFault; }
}
/* The group's own recipe: its squad template read and walked by T2TplPod, its sub-squad entries collected. MAIN THREAD */
static int T2RecipePod(const void* platoon, void** ptr, char (*sid)[kT2SidChars], int cap, int* n, void** sub, char (*ssid)[kT2SidChars], int scap, int* ns)
{
    *n = 0; *ns = 0;
    const char* tpl = 0;
    __try { tpl = *(const char* const*)((const char*)platoon + 0x108); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return coopt2::kRecipeFault; }
    return T2TplPod(tpl, ptr, sid, cap, n, sub, ssid, scap, ns);
}
/* What T2RecipeUnique read: step (a coopt2::RecipeStep, the group's own template), tpls the member entries read (the
   sub-squads' included), uniq those flagged unique (or unreadable), open those that stop it; subs the sub-squad entries,
   subRead those whose template was found and read, subNested the sub-squad entries those templates have of their own,
   subStep the first sub-squad read that did not finish (kRecipeRead when none did). */
struct T2Recipe { int step, tpls, uniq, open, subs, subRead, subNested, subStep; };
/* 1 = the group is not rebuilt for its recipe's uniques: a template of its member lists is flagged "unique" and this game's
   unique-state entry for it is missing or has an empty template slot (0x591720 would let the rebuild make it, though another
   game may hold it alive), or a template or the group's own recipe could not be read. 2 = not rebuilt for its sub-squads:
   createRandomSquad 0x582F80 also runs itself on the squad template of every "slaves" / "prisoners" entry
   (decomp_582f80:1162-1181, 1266, 1300, 1417); each such template is found (its resolved record, else by its stringID) and
   walked once, its member lists joining the recipe's, and coopt2::SubSquadsBlock refuses when one was not found or read or
   has sub-squads of its own (a level this check does not follow). 0 = the recipe lets the rebuild go on. MAIN THREAD */
static int T2RecipeUnique(void* platoon, T2Recipe* r)
{
    static void* ptr[kT2Recipe];
    static char sid[kT2Recipe][kT2SidChars];
    static void* sub[kT2Subs];
    static char ssid[kT2Subs][kT2SidChars];
    int n = 0, ns = 0;
    std::memset(r, 0, sizeof(*r));
    r->subStep = coopt2::kRecipeRead;
    r->step = T2RecipePod(platoon, ptr, sid, kT2Recipe, &n, sub, ssid, kT2Subs, &ns);
    if (r->step != coopt2::kRecipeRead) return 1;
    r->subs = ns;
    for (int k = 0; k < ns; ++k)
    {
        void* gd = (sub[k] != 0) ? sub[k] : coop::WsTemplateBySid(ssid[k]);
        int nested = 0;
        const int st = T2TplPod((const char*)gd, ptr, sid, kT2Recipe, &n, 0, 0, 0, &nested);
        if (st == coopt2::kRecipeRead) { ++r->subRead; r->subNested += nested; }
        else if (r->subStep == coopt2::kRecipeRead) r->subStep = st;
    }
    r->tpls = n;
    for (int i = 0; i < n; ++i)
    {
        void* gd = (ptr[i] != 0) ? ptr[i] : coop::WsTemplateBySid(sid[i]);
        const int u = (gd != 0) ? coop::WsTemplateIsUnique(gd) : -1;
        if (u != 0) ++r->uniq;
        const int slot = (u == 1) ? coop::WsUniqueSlotPod(gd) : 0;
        if (coopt2::RecipeUniqueBlocks(u, slot) != 0) ++r->open;
    }
    if (r->open != 0) return 1;
    return (coopt2::SubSquadsBlock(r->subs, r->subRead, r->subNested) != 0) ? 2 : 0;
}
static std::string T2RecipeWhy(const T2Recipe& r)
{
    return ": recipe step=" + N((long long)r.step) + "(" + coopt2::RecipeStepName(r.step) + ") templates=" + N((long long)r.tpls) + " unique=" + N((long long)r.uniq)
           + " notMadeHere=" + N((long long)r.open) + " subSquads=" + N((long long)r.subs) + " subRead=" + N((long long)r.subRead) + " subNested=" + N((long long)r.subNested)
           + (r.subStep != coopt2::kRecipeRead ? " subStep=" + N((long long)r.subStep) + "(" + coopt2::RecipeStepName(r.subStep) + ")" : std::string());
}
/* TEST ONLY (`townres show <town>`): per list, how many groups there are, how many are awake, and how many of the awake the
   kill lever can take (whole, not a slave group, not declared dead, no living unique, player or carried member).
   MAIN THREAD, holder only (the caller). */
static void T2Show(void* town, const std::string& tn)
{
    static unsigned char hs[kT2Nodes][kT2HandBytes];   /* main thread only */
    std::string s = "[TOWNS2] show: town '" + tn + "'";
    for (int type = 1; type <= 2; ++type)
    {
        int total = 0, awake = 0, killable = 0; float budget = 0.0f;
        const int r = T2NodesPod(town, type, hs, kT2Nodes, &total, &budget);
        if (r == 1)
            for (int i = 0; i < total && i < kT2Nodes; ++i)
            {
                T2Grp G; T2GroupPod(hs[i], &G);
                if (G.ok != 1 || G.f.awake == 0) continue;
                ++awake;
                if (G.f.dead == 0 && G.canRefresh != 0 && G.sepType == coopt2::kNullItemType && G.f.uniqueAlive == 0 && G.f.player == 0 && G.f.carried == 0) ++killable;
            }
        s += std::string(type == 1 ? " residents=" : " patrols=") + N((long long)total) + " awake=" + N((long long)awake) + " killable=" + N((long long)killable)
             + (r != 1 ? " (step=" + N((long long)r) + ")" : std::string());
    }
    T2Line(s + " (TEST)");
}
/* TEST ONLY (`townres kill <town> <n> [res|pat] [regen|noregen] [all]`): in the first awake, whole, non-slave group of the
   chosen list (residents by default) that matches the recipe filter (regen: Platoon+0xD9 set, noregen: not set) and has no
   living unique, player or carried member, n living members die through the engine's own Character::declareDead 0x7A5660
   (StoreDeclareDeadPod - the platoonkill lever's path). Without `all` one member always stays alive (the leader when it can)
   and the group needs 3 or more original and 2 or more living; with `all` everyone dies, the leader last, and the engine's
   own Platoon::declareDead then keeps a regenerating group marked or takes any other out of the town's lists - the line
   says which (mark / dead after). MAIN THREAD, holder only (the caller). */
static void T2Kill(void* town, const std::string& tn, int spec)
{
    const int want = spec & kT2KillCountMask, type = (spec & kT2KillPat) != 0 ? 2 : 1, all = (spec & kT2KillAll) != 0 ? 1 : 0;
    unsigned char hs[kT2Nodes][kT2HandBytes]; int total = 0; float budget = 0.0f;
    const int r = T2NodesPod(town, type, hs, kT2Nodes, &total, &budget);
    if (r != 1) { T2Line("[TOWNS2] kill: town '" + tn + "' - its " + T2ListName(type) + " list could not be read (step=" + N((long long)r) + ") - nothing killed (TEST)"); return; }
    int awake = 0;   /* the list's awake groups, said when nothing matched */
    for (int i = 0; i < total && i < kT2Nodes; ++i)
    {
        T2Grp G; T2GroupPod(hs[i], &G);
        if (G.ok == 1 && G.f.awake != 0) ++awake;
        if (G.ok != 1 || G.f.awake == 0 || G.f.dead != 0 || G.canRefresh == 0 || G.sepType != coopt2::kNullItemType || G.f.uniqueAlive != 0 || G.f.player != 0 || G.f.carried != 0) continue;
        if ((spec & kT2KillRegen) != 0 && G.f.regenerates == 0) continue;
        if ((spec & kT2KillNoRegen) != 0 && G.f.regenerates != 0) continue;
        if (all != 0 ? (G.nl < 1) : (G.orig < 3 || G.f.living < 2 || G.nl < 2)) continue;
        const std::string wid = T2Wid(G.platoon);   /* read before the kills, which can end the group */
        int k = want; if (all != 0) k = G.nl; else if (k > G.nl - 1) k = G.nl - 1;
        int killed = 0, faults = 0, refused = 0;   /* a kill counts only when the member reads dead after it */
        for (int pass = 0; pass < 2 && killed < k; ++pass)   /* pass 0: everyone but the leader; pass 1: the leader */
            for (int j = 0; j < G.nl && killed < k; ++j)
            {
                const bool isLeader = (G.live[j] == G.leader);
                if ((pass == 0) == isLeader) continue;
                if (StoreDeclareDeadPod(G.live[j]) != 1) ++faults; else if (StoreIsDeadPod(G.live[j]) == 1) ++killed; else ++refused;
            }
        ++g_t2Kills; g_t2Killed += killed;
        T2Grp A; T2GroupPod(hs[i], &A);
        T2Line("[TOWNS2] kill: town '" + tn + "' " + T2ListName(type) + " " + N((long long)i) + " id=" + wid + " living=" + N((long long)G.f.living) + "/" + N((long long)G.orig)
               + " regenerates=" + N((long long)G.f.regenerates) + " -> killed " + N((long long)killed)
               + (faults != 0 ? " faults=" + N((long long)faults) : std::string())
               + (refused != 0 ? " refused=" + N((long long)refused) + " (a copy - the owner decides)" : std::string())
               + " through Character::declareDead 0x7A5660; after: mark=" + (A.ok == 1 ? N((long long)A.mark) : std::string("?")) + " dead=" + (A.ok == 1 ? N((long long)A.f.dead) : std::string("?"))
               + " living=" + (A.ok == 1 ? N((long long)A.f.living) : std::string("?")) + " (TEST)");
        return;
    }
    T2Line("[TOWNS2] kill: town '" + tn + "' - no matching awake whole " + std::string(T2ListName(type)) + " group among " + N((long long)total) + " (awake " + N((long long)awake) + ") - nothing killed (TEST)");
}
/* The 5-day walk of one town: both lists read, the game's marker rule run on them, each pick queued or its refusal said.
   townDead: the town's own flag as its check-up read it. MAIN THREAD */
static void T2Walk(void* town, const std::string& tn, const float* tp, int nosight, bool forced, int townDead)
{
    ++g_t2Walks;
    static unsigned char hr[kT2Nodes][kT2HandBytes], hp[kT2Nodes][kT2HandBytes];   /* main thread only */
    int nr = 0, np = 0; float br = 0.0f, bp = 0.0f;
    const int r1 = T2NodesPod(town, 1, hr, kT2Nodes, &nr, &br);
    const int r2 = (r1 == 1) ? T2NodesPod(town, 2, hp, kT2Nodes, &np, &bp) : 0;
    if (r1 != 1 || r2 != 1 || nr > kT2Nodes || np > kT2Nodes)
    {
        ++g_t2WalkFault;
        T2Line("[TOWNS2] town '" + tn + "' - its population lists could not be read whole (residents step=" + N((long long)r1) + " n=" + N((long long)nr) + ", patrols step=" + N((long long)r2) + " n=" + N((long long)np) + ") - not walked");
        return;
    }
    static coopt2::MarkIn mr[kT2Nodes], mp[kT2Nodes];
    static unsigned char kr[kT2Nodes], kp[kT2Nodes];
    T2Grp G;
    for (int k = 0; k < 2; ++k)
    {
        const int n = (k == 0) ? nr : np;
        for (int i = 0; i < n; ++i)
        {
            T2GroupPod(k == 0 ? hr[i] : hp[i], &G);
            coopt2::MarkIn& m = (k == 0) ? mr[i] : mp[i];
            std::memset(&m, 0, sizeof(m));
            if (G.ok == 0 || G.ok == 3)
            {
                ++g_t2Skip[kT2xFault]; ++g_t2WalkFault;
                T2Line("[TOWNS2] town '" + tn + "' - " + std::string(k == 0 ? "resident" : "patrol") + " " + N((long long)i) + " could not be read (step=" + N((long long)G.ok) + ") - the budget cannot be counted, not walked");
                return;
            }
            if (G.ok != 1) continue;   /* a handle that no longer resolves: the marker skips it too */
            m.counted = 1; m.whole = (G.sepType == coopt2::kNullItemType) ? 1 : 0; m.canRefresh = G.canRefresh != 0 ? 1 : 0;
            m.regenerates = G.f.regenerates; m.orig = G.orig; m.living = G.f.living;
        }
    }
    coopt2::MarkerPass(mr, nr, br, mp, np, bp, townDead, kr, kp);   /* townDead: the check-up's own read of the town */
    int livR = 0, livP = 0;
    for (int i = 0; i < nr; ++i) if (mr[i].counted != 0) livR += mr[i].living;
    for (int i = 0; i < np; ++i) if (mp[i].counted != 0) livP += mp[i].living;
    float plx[kT2Players], plz[kT2Players];
    const int npl = T2Players(plx, plz, kT2Players);
    int picked = 0;
    for (int k = 0; k < 2; ++k)
    {
        const int n = (k == 0) ? nr : np;
        const std::string list = (k == 0) ? "resident" : "patrol";
        for (int i = 0; i < n; ++i)
        {
            const coopt2::MarkIn& m = (k == 0) ? mr[i] : mp[i];
            if (m.counted == 0) { ++g_t2Skip[kT2xGone]; continue; }
            ++g_t2Groups;
            const int byMarker = (k == 0) ? kr[i] : kp[i];
            T2GroupPod(k == 0 ? hr[i] : hp[i], &G);
            if (G.ok != 1) { ++g_t2Skip[G.ok == 2 ? kT2xGone : kT2xFault]; continue; }
            G.f.marked = (byMarker != 0 || G.mark == 1) ? 1 : 0;
            if (G.f.marked == 0) { ++g_t2Verdict[coopt2::kNotMarked]; continue; }
            ++picked; ++g_t2Picked; if (byMarker != 0) ++g_t2PickedMarker; else ++g_t2PickedEngine;
            const std::string wid = T2Wid(G.platoon);
            G.f.held = T2AllHeld(&G, tp);
            G.f.seen = T2Seen(npl, plx, plz, &G);
            T2Recipe rc; const int rv = T2RecipeUnique(G.platoon, &rc);
            G.f.recipeUnique = (rv == 1) ? 1 : 0; G.f.subSquads = (rv == 2) ? 1 : 0;
            const int v = coopt2::GroupVerdict(G.f, nosight);
            ++g_t2Verdict[v];
            const std::string why = std::string(byMarker != 0 ? (G.orig - G.f.living > 0 && m.regenerates != 0 && k == 0 ? "picked by the marker rule (regenerates or 60%)" : "picked by the marker rule (60% within the budget)")
                                                              : "already marked by the engine (its leader died)");
            if (v != coopt2::kQueue) { T2Line(T2GroupLine(tn, list, i, wid, G.f.living, G.orig, why + "; skipped(" + std::string(coopt2::VerdictName(v)) + (v == coopt2::kUniqueUnknown || v == coopt2::kSubSquads ? T2RecipeWhy(rc) : std::string()) + ")")); continue; }
            const unsigned char* hh = (k == 0) ? hr[i] : hp[i];
            bool dup = false;
            for (size_t q = 0; q < g_t2Queue.size(); ++q) if (T2SameHand(g_t2Queue[q].h, hh)) { dup = true; break; }
            if (dup) continue;
            if (g_t2Queue.size() >= kT2QueueCap) { ++g_t2Skip[kT2xQueueFull]; T2Line(T2GroupLine(tn, list, i, wid, G.f.living, G.orig, why + "; skipped(queueFull)")); continue; }
            T2Q q;
            std::memcpy(q.h, hh, kT2HandBytes);
            q.town = tn; q.list = list; q.wid = wid; q.idx = i; q.orig = G.orig; q.living = G.f.living; q.nosight = nosight; q.at = ::GetTickCount();
            g_t2Queue.push_back(q); ++g_t2Queued;
            T2Line(T2GroupLine(tn, list, i, wid, G.f.living, G.orig, why + "; queued" + (nosight != 0 ? " (TEST nosight: the sight rule is ignored for this group)" : "")));
        }
    }
    if (forced || picked != 0)
        T2Line("[TOWNS2] town '" + tn + "' walked" + std::string(forced ? (nosight != 0 ? " (townres now nosight)" : " (townres now)") : "") + " residents=" + N((long long)nr) + " patrols=" + N((long long)np)
               + " budget[residents,patrols]=" + N((long long)br) + "," + N((long long)bp) + " living[residents,patrols]=" + N((long long)livR) + "," + N((long long)livP) + " picked=" + N((long long)picked) + " players=" + N((long long)npl));
}
/* The 0x92BE50 post-hook's second check (beside RefillCheck): holder only, zone live, every 5 in-game days per town. */
static void Towns2Check(void* town)
{
    if (town == 0 || !g_t2Hooked) return;   /* no act hook = nothing may be queued */
    if (::GetCurrentThreadId() != g_mainThread) { ++g_t2OffThread; return; }
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0 || !coopbar::BarSidOk(sid)) return;
    const std::string key(sid);
    int townActive = 0, townDead = 0;
    const int flagsOk = T2TownFlagsPod(town, &townActive, &townDead);
    if (flagsOk != 0 && townActive == 0) g_t2GenOut[key] = coop::WorldStateGeneration();   /* out of loaded range: the version is chosen again when it comes back */
    float tp[3] = { 0, 0, 0 };
    if (TownPosPod(town, tp) == 0) return;
    char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?");
    const std::string tn = RfStr(nm);
    std::map<std::string, int>::iterator fi = T2Find(g_t2Forced, key, nm);
    std::map<std::string, int>::iterator ki = T2Find(g_t2Kill, key, nm);
    std::map<std::string, int>::iterator si = T2Find(g_t2Show, key, nm);
    const int held = T2HeldHere(tp[0], tp[2]);
    if (held != 1)
    {
        g_t2Last.erase(key);   /* the 5 days belong to this holder: a later hold starts them again */
        ++g_t2Skip[kT2xNotHolder];   /* a check-up of a town this game does not hold with its zone live */
        if (fi != g_t2Forced.end() || ki != g_t2Kill.end() || si != g_t2Show.end())
            T2ForcedSkip(key, tn, held == -2 ? std::string("its zone is not live on this game - only the game that holds it acts")
                                             : "this game does not hold its zone (held=" + N((long long)held) + ") - the holder acts");
        return;
    }
    ++g_t2Checks;
    if (si != g_t2Show.end()) { g_t2Show.erase(si); T2Show(town, tn); }
    if (ki != g_t2Kill.end()) { const int spec = ki->second; g_t2Kill.erase(ki); T2Kill(town, tn, spec); }
    const double now = LocalWorldHours();
    if (now < 0.0) { ++g_t2NoClock; return; }
    const bool forced = (fi != g_t2Forced.end());
    const int nosight = forced ? fi->second : 0;
    std::map<std::string, double>::iterator li = g_t2Last.find(key);
    const int due = coopt2::RebuildDue(now, li == g_t2Last.end() ? -1.0 : li->second, forced ? 1 : 0);
    if (due == 3 || due == 2) { g_t2Last[key] = now; return; }   /* first seen by this holder, or the clock went back: the 5 days start now */
    if (due != 1) return;
    ++g_t2Due;
    if (forced) { g_t2Forced.erase(fi); g_t2WhyAt.erase(key); }
    g_t2Last[key] = now;
    if (flagsOk == 0 || townDead != 0)
    {
        ++g_t2TownDead;
        T2Line("[TOWNS2] town '" + tn + "' " + (flagsOk == 0 ? std::string("- its flags could not be read") : std::string("is dead (its population manager says so)")) + " - the game's marker never runs for it; not walked");
        return;
    }
    std::map<std::string, long long>::const_iterator gi = g_t2GenOut.find(key);
    const long long genOut = (gi == g_t2GenOut.end()) ? g_t2GenWorld : gi->second;
    const int hasVersions = T2TownHasVersionsPod(town);
    if (coopt2::VersionMayChange(hasVersions, coop::WorldStateGeneration(), genOut) != 0)
    {
        ++g_t2VersionWait;
        T2Line("[TOWNS2] town '" + tn + "' has other versions (override town=" + N((long long)hasVersions) + ") and a named character's state changed since it came into range (generation "
               + N(genOut) + " -> " + N(coop::WorldStateGeneration()) + ") - the game chooses its version again first; not walked until it has been out of range");
        return;
    }
    T2Walk(town, tn, tp, nosight, forced, townDead);
}
/* The engine's own sleep pair and wake pair, back to back, in ONE SEH frame. *step says how far it got. */
static int T2SleepWakePod(void* f, void* p, uintptr_t base, int* step)
{
    __try
    {
        *step = 1; *(int*)((char*)p + 0x118) = 1;   /* the rebuild mark (PlatoonCreationMessage 1) - setupCheck consumes it */
        *step = 2; ((T2PlatoonDeactivateFn)(base + (uintptr_t)kPlatoonDeactivateRva))(p, 0);
        *step = 3; ((T2FactionPlatoonFn)(base + (uintptr_t)kFactionDeactivatePlatoonRva))(f, p);
        *step = 4; ((T2PlatoonActivateFn)(base + (uintptr_t)kPlatoonActivateRva))(p);
        *step = 5; ((T2FactionPlatoonFn)(base + (uintptr_t)kFactionActivatePlatoonRva))(f, p);
        *step = 6;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The start of Faction::updateActivePlatoons for `faction` (MAIN THREAD, before the engine's loop): every queued group of
   THIS faction is re-read LIVE and, if every refusal still passes, marked and slept+woken. Other factions' groups wait.
   The walk's pick stands, as the game's mark stands until the group wakes. */
static void Towns2Act(void* faction)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const DWORD nowMs = ::GetTickCount();
    int np = -2; float plx[kT2Players], plz[kT2Players];
    for (size_t i = 0; i < g_t2Queue.size(); )
    {
        const T2Q q = g_t2Queue[i];
        void* pf = 0;
        if (T2FactionOfPod(q.h, &pf) == 1 && pf != faction)   /* another faction's group: it waits for its own faction's update */
        {
            if (nowMs - q.at <= kT2QueueMaxMs) { ++i; continue; }
            ++g_t2Skip[kT2xNoFactionTurn];
            T2Line(T2GroupLine(q.town, q.list, q.idx, q.wid, q.living, q.orig, "skipped(noFactionTurn: its faction's update did not come in " + N((long long)(kT2QueueMaxMs / 1000)) + " s)"));
            g_t2Queue.erase(g_t2Queue.begin() + i);
            continue;
        }
        T2Grp G; T2GroupPod(q.h, &G);
        if (G.ok == 1 && G.faction != faction) { ++i; continue; }   /* changed between the two reads: its own faction's turn decides */
        std::string res;
        if (G.ok != 1) { ++g_t2Skip[G.ok == 2 ? kT2xGone : kT2xFault]; res = std::string("skipped(") + (G.ok == 2 ? "gone" : "readFault") + " at commitment)"; }
        else
        {
            if (np == -2) np = T2Players(plx, plz, kT2Players);
            G.f.marked = 1;
            G.f.held = T2AllHeld(&G, 0);
            G.f.seen = T2Seen(np, plx, plz, &G);
            T2Recipe rc; const int rv = T2RecipeUnique(G.platoon, &rc);
            G.f.recipeUnique = (rv == 1) ? 1 : 0; G.f.subSquads = (rv == 2) ? 1 : 0;
            const int v = coopt2::GroupVerdict(G.f, q.nosight);
            if (v != coopt2::kQueue) { ++g_t2Verdict[v]; res = "skipped(" + std::string(coopt2::VerdictName(v)) + (v == coopt2::kUniqueUnknown || v == coopt2::kSubSquads ? T2RecipeWhy(rc) : std::string()) + " at commitment)"; }
            else if (kPlatoonDeactivateRva == 0 || kFactionDeactivatePlatoonRva == 0 || kPlatoonActivateRva == 0 || kFactionActivatePlatoonRva == 0) { ++g_t2Skip[kT2xNoAddr]; res = "skipped(noAddr)"; }
            else
            {
                int step = 0;
                if (T2SleepWakePod(faction, G.platoon, base, &step) == 0)
                {
                    ++g_t2Skip[kT2xStepFault]; ++g_t2ActFaults;
                    res = "skipped(stepFault at step " + N((long long)step) + " of 5 - 1 mark, 2 deactivate, 3 deactivatePlatoon, 4 activate, 5 activatePlatoon)";
                    ErrorLog("[TOWNS2] FAULT in the sleep/wake of town '" + q.town + "' " + q.list + " " + N((long long)q.idx) + " at step " + N((long long)step));
                }
                else
                {
                    ++g_t2Asked;
                    res = "asked (rebuild mark set, slept and woken by the engine's own four calls; the rebuild runs in this faction's update)";
                    T2V w; std::memcpy(w.h, q.h, kT2HandBytes); w.town = q.town; w.list = q.list; w.wid = q.wid; w.idx = q.idx; w.origBefore = G.orig; w.livingBefore = G.f.living; w.at = nowMs;
                    g_t2Verify.push_back(w);
                }
            }
        }
        T2Line(T2GroupLine(q.town, q.list, q.idx, q.wid, G.ok == 1 ? G.f.living : q.living, G.ok == 1 ? G.orig : q.orig, res));
        g_t2Queue.erase(g_t2Queue.begin() + i);
    }
}
void detour_t2FactionUpdate(void* faction, float dt)
{
    if (faction != 0 && ::GetCurrentThreadId() == g_mainThread && !g_t2Queue.empty()) Towns2Act(faction);   /* pre: before the engine walks its lists */
    if (faction != 0 && ::GetCurrentThreadId() == g_mainThread) coop::CopySquadKeepAwake(faction);   /* a copy squad with a member on ground loaded here is not slept by this game (handoff.cpp, 1 Hz per faction) */
    orig_t2FactionUpdate(faction, dt);
}
/* MAIN THREAD (TownGenTick, once a second while any): a woken group is `rebuilt` once its mark is back to 0 with living members */
static void Towns2Tick()
{
    if (g_t2Verify.empty()) return;
    static DWORD last = 0; const DWORD nowMs = ::GetTickCount();
    if (nowMs - last < 1000) return;
    last = nowMs;
    for (size_t i = 0; i < g_t2Verify.size(); )
    {
        const T2V v = g_t2Verify[i];
        T2Grp G; T2GroupPod(v.h, &G);
        if (G.ok == 1 && G.f.awake != 0 && G.mark == 0 && G.members > 0 && G.f.living > 0)
        {
            ++g_t2Rebuilt;
            T2Line(T2GroupLine(v.town, v.list, v.idx, v.wid, G.f.living, G.orig, "rebuilt (was " + N((long long)v.livingBefore) + "/" + N((long long)v.origBefore) + "; members=" + N((long long)G.members)
                   + " after " + N((long long)(nowMs - v.at)) + " ms)"));
            g_t2Verify.erase(g_t2Verify.begin() + i);
            continue;
        }
        if (nowMs - v.at > kT2VerifyMaxMs)
        {
            ++g_t2Skip[kT2xRebuildNotSeen];
            T2Line(T2GroupLine(v.town, v.list, v.idx, v.wid, G.f.living, G.orig, "skipped(rebuildNotSeen in " + N((long long)(kT2VerifyMaxMs / 1000)) + " s: read=" + N((long long)G.ok) + " awake=" + N((long long)G.f.awake)
                   + " mark=" + N((long long)G.mark) + " members=" + N((long long)G.members) + ")"));
            g_t2Verify.erase(g_t2Verify.begin() + i);
            continue;
        }
        ++i;
    }
}
static void Towns2Install(uintptr_t base)
{
    if (kTownPeriodicUpdateRva == 0 || kFactionUpdateActivePlatoonsRva == 0 || kPlatoonDeactivateRva == 0 || kFactionDeactivatePlatoonRva == 0 || kPlatoonActivateRva == 0 || kFactionActivatePlatoonRva == 0)
    { ErrorLog("[TOWNS2] off: the address table lacks a row (TownPeriodicUpdate, FactionUpdateActivePlatoons, Deactivate, FactionDeactivatePlatoon, Activate or FactionActivatePlatoon) - town groups are not rebuilt"); return; }
    if (coop::AddHook((void*)(base + kFactionUpdateActivePlatoonsRva), (void*)&detour_t2FactionUpdate, (void**)&orig_t2FactionUpdate) != coop::SUCCESS)
    { ErrorLog("[TOWNS2] AddHook Faction::updateActivePlatoons 0x6BA810 failed - town groups are not rebuilt"); return; }
    g_t2Hooked = true;
    DebugLog("[TOWNS2] hook installed: Faction::updateActivePlatoons 0x6BA810 (pre: a queued town group is marked and slept+woken before the engine's own loop); the decision rides the refill's Town::periodicTick 0x92BE50 post-hook");
}
static void Towns2Report()
{
    std::string vs, xs;
    for (int v = 1; v < coopt2::kVerdicts; ++v) { if (v > 1) vs += ","; vs += std::string(coopt2::VerdictName(v)) + "=" + N(g_t2Verdict[v]); }
    for (int x = 0; x < kT2xN; ++x) { if (x > 0) xs += ","; xs += std::string(kT2xName[x]) + "=" + N(g_t2Skip[x]); }
    DebugLog("[TOWNS2] REPORT checks=" + N(g_t2Checks) + " due=" + N(g_t2Due) + " walks=" + N(g_t2Walks) + " walkFault=" + N(g_t2WalkFault) + " townDead=" + N(g_t2TownDead) + " versionWait=" + N(g_t2VersionWait)
             + " groups=" + N(g_t2Groups) + " picked=" + N(g_t2Picked) + " pickedMarker=" + N(g_t2PickedMarker) + " pickedEngineMark=" + N(g_t2PickedEngine)
             + " queued=" + N(g_t2Queued) + " asked=" + N(g_t2Asked) + " rebuilt=" + N(g_t2Rebuilt) + " actFaults=" + N(g_t2ActFaults)
             + " verdicts[" + vs + "] other[" + xs + "] queueNow=" + N((long long)g_t2Queue.size()) + " verifyNow=" + N((long long)g_t2Verify.size())
             + " townsTimed=" + N((long long)g_t2Last.size()) + " stateGen=" + N(coop::WorldStateGeneration()) + " kills=" + N(g_t2Kills) + " killed=" + N(g_t2Killed) + " forcedPending=" + N((long long)g_t2Forced.size())
             + " killPending=" + N((long long)g_t2Kill.size()) + " showPending=" + N((long long)g_t2Show.size()) + " noClock=" + N(g_t2NoClock) + " offThread=" + N(g_t2OffThread) + " hook=" + N((long long)(g_t2Hooked ? 1 : 0))
             + " linesDropped=" + N(g_t2LinesDropped));
    for (std::map<std::string, int>::const_iterator f = g_t2Forced.begin(); f != g_t2Forced.end(); ++f)
        DebugLog("[TOWNS2] townres now '" + RfStr(f->first.c_str()) + "'" + (f->second != 0 ? " nosight" : "") + " still pending on this game - no check-up of that town has come here while this game holds it");
    for (std::map<std::string, int>::const_iterator k = g_t2Kill.begin(); k != g_t2Kill.end(); ++k)
        DebugLog("[TOWNS2] townres kill '" + RfStr(k->first.c_str()) + "' " + N((long long)(k->second & kT2KillCountMask)) + " still pending on this game - no check-up of that town has come here while this game holds it");
}
static void Towns2Teardown() { g_t2Queue.clear(); g_t2Verify.clear(); g_t2Last.clear(); g_t2WhyAt.clear(); g_t2GenOut.clear(); g_t2GenWorld = coop::WorldStateGeneration(); }   /* hands, holds and range sightings belong to the world that made them; the TEST targets stay */
/* T-392 (owner 335 a): THE SECOND CHANCE. Run from the post-hook on the engine's own per-town check-up, after the engine's
   check-up and before RefillCheck, so a bar settled here is then topped up by the refill's own rules. MAIN THREAD.
   WHY THIS IS NOT THE RE-OFFER P8e-b DELETED (review-p8e C-2): that pass called the engine through a Town* cached for up to
   32 s. This one acts only on the Town* the ENGINE just handed its own check-up, and g_barPend is still only compared with
   it. A building is never kept: it is found again by its position key (ObjectByPositionKey) in this game's loaded zones at
   the moment of the call. The engine calls are the ones the gates make - spawnTheBarFlies through detour_barFlies (the top-up
   already rolls from this same hook, RfTopUpPod), populateBuilding through worldgen's gate - each inside a crash guard. */
static int BarReofferPod(void* town)
{
    __try { detour_barFlies(town); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static bool T392SectorLive(int sx, int sy)
{
    int ax[64], ay[64]; const int an = ActiveZoneSectors(ax, ay, 64);
    for (int i = 0; i < an && i < 64; ++i) if (ax[i] == sx && ay[i] == sy) return true;
    return false;
}
static void T392ReofferLog(const char* kind, const std::string& sid, const char* result, const std::string& extra)
{
    if (std::strcmp(result, "waiting") == 0)
    {
        bool first = false;
        TgLockInit(); ::EnterCriticalSection(&g_tgLock); first = g_t392WaitLogged.insert(std::string(kind) + "|" + sid).second; ::LeaveCriticalSection(&g_tgLock);
        if (!first) return;   /* waiting: first per town */
    }
    DebugLog(std::string("[TOWN] reoffer kind=") + kind + " town=" + sid + " result=" + result + extra);
}
/* T-581: THE OWED ROWS, MAIN THREAD. Sends this game's own set-aside work (ADD) once per link and the DONEs queued off the main
   thread; CLAIM / RELEASE go once per link per row until the row changes. */
std::map<std::string, DWORD> g_owedAsked, g_owedReleased;   /* MAIN THREAD: table keys a CLAIM / RELEASE went for, and when (cleared when the row changes) */
static std::string OwedText(unsigned kind, const std::string& key, const std::string& sid)
{
    return std::string("kind=") + owedpop::KindName(kind) + " town=" + sid + (kind == owedpop::kKindResidents ? " building=" + key : std::string());
}
static bool OwedSendDone(unsigned kind, const std::string& key, unsigned why, bool queueOnFail)
{
    std::vector<char> b;
    if (!owedpop::EncodeDone(&b, owedpop::kOpDone, kind, key, why)) return false;
    std::string sid = (kind == owedpop::kKindBar) ? key : std::string("?");
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    { const owedpop::Table::const_iterator it = g_owed.find(owedpop::TableKey(kind, key)); if (it != g_owed.end()) sid = it->second.sid; }
    ::LeaveCriticalSection(&g_tgLock);
    if (!StoreSendOwed(b))
    {
        ++g_owedSendFailed;
        /* the link is down: the row this game held is nobody's once the world server sees it gone, so only "the work is there" is
           still true to say on the next link - made becomes has; gone and fault are dropped (the next holder finds them itself) */
        if (queueOnFail && (why == owedpop::kWhyMade || why == owedpop::kWhyHas))
        { OwedDone d; d.kind = kind; d.key = key; d.why = owedpop::kWhyHas; TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_owedDoneQ.push_back(d); ::LeaveCriticalSection(&g_tgLock); }
        return false;
    }
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    if (owedpop::CountedWhy(why) != 0)   /* handed back: the world server counts it and keeps the row until the third report (owedpop::TableDone) */
    { const owedpop::Table::iterator it = g_owed.find(owedpop::TableKey(kind, key)); if (it != g_owed.end()) it->second.claimant = (int)owedpop::kClaimNone; }
    else g_owed.erase(owedpop::TableKey(kind, key));
    ::LeaveCriticalSection(&g_tgLock);
    g_owedAsked.erase(owedpop::TableKey(kind, key));
    ::InterlockedIncrement64(why == owedpop::kWhyMade ? &g_owedMade : why == owedpop::kWhyHas ? &g_owedDoneHas : why == owedpop::kWhyGone ? &g_owedDoneGone : why == owedpop::kWhyEmpty ? &g_owedDoneEmpty : &g_owedDoneFault);
    OwedLine("[OWED] done " + OwedText(kind, key, sid) + " why=" + owedpop::WhyName(why) + " by=this-game"
             + (owedpop::CountedWhy(why) != 0 ? std::string(" (handed back - the world server keeps it until the third report)") : std::string()));
    return true;
}
static void OwedSendKey(unsigned op, unsigned kind, const std::string& key, const std::string& why)
{
    const std::string tk = owedpop::TableKey(kind, key);
    std::map<std::string, DWORD>& sent = (op == owedpop::kOpClaim) ? g_owedAsked : g_owedReleased;
    const DWORD nowMs = ::GetTickCount();
    { const std::map<std::string, DWORD>::const_iterator a = sent.find(tk); if (a != sent.end() && owedpop::AskAgain(op == owedpop::kOpClaim ? 1 : 0, (unsigned)(nowMs - a->second)) == 0) return; }
    std::vector<char> b;
    if (!owedpop::EncodeKey(&b, op, kind, key) || !StoreSendOwed(b)) { ++g_owedSendFailed; return; }
    sent[tk] = nowMs;
    if (op == owedpop::kOpRelease)   /* nobody's here the moment it is handed back: a holder flicker must not make it while the world server gives it to another game */
    {
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        { const owedpop::Table::iterator r = g_owed.find(tk); if (r != g_owed.end()) r->second.claimant = (int)owedpop::kClaimNone; }
        ::LeaveCriticalSection(&g_tgLock);
        g_owedAsked.erase(tk);
    }
    std::string sid = (kind == owedpop::kKindBar) ? key : std::string("?");
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    { const owedpop::Table::const_iterator it = g_owed.find(tk); if (it != g_owed.end()) sid = it->second.sid; }
    ::LeaveCriticalSection(&g_tgLock);
    ++(op == owedpop::kOpClaim ? g_owedClaims : g_owedReleases);
    OwedLine(std::string("[OWED] ") + (op == owedpop::kOpClaim ? "claim " : "release ") + OwedText(kind, key, sid) + why);
}
static void OwedSendClaim(unsigned kind, const std::string& key) { OwedSendKey(owedpop::kOpClaim, kind, key, ""); }
/* the DONE made a finished save owes goes out once per link until the world server's GONE (owedpop::DoneStep); MAIN THREAD */
static void OwedDoneOwedFlush()
{
    if (RoleIsSingle() || g_linkedCached == 0) return;
    const long gen = (long)StoreLinkGen();
    if (gen <= 0) return;
    std::vector<OwedDoneOwed> send; std::vector<std::string> settledAway;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (std::map<std::string, OwedDoneOwed>::iterator it = g_owedDoneOwed.begin(); it != g_owedDoneOwed.end(); )
    {
        const int step = owedpop::DoneStep(it->second.sentGen, gen, g_owed.find(it->first) != g_owed.end() ? 1 : 0);
        if (step == owedpop::kDoneSend) send.push_back(it->second);
        if (step == owedpop::kDoneDrop) { settledAway.push_back(it->first); g_owedDoneOwed.erase(it++); } else ++it;
    }
    ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < settledAway.size(); ++i) OwedLine("[OWED] settled " + settledAway[i] + " (its DONE was owed; the row left the world server while this game was away)");
    for (size_t i = 0; i < send.size(); ++i)
    {
        if (!OwedSendDone(send[i].kind, send[i].key, owedpop::kWhyMade, false)) continue;   /* not sent: owed on the next link */
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        { const std::map<std::string, OwedDoneOwed>::iterator e = g_owedDoneOwed.find(owedpop::TableKey(send[i].kind, send[i].key)); if (e != g_owedDoneOwed.end()) e->second.sentGen = gen; }
        ::LeaveCriticalSection(&g_tgLock);
    }
}
/* this game's own set-aside work goes up (ADD) once per link; the queued DONEs follow */
static void OwedFlush()
{
    if (RoleIsSingle() || g_linkedCached == 0) return;
    const long gen = (long)StoreLinkGen();
    if (gen <= 0) return;
    std::vector<owedpop::Row> adds; std::vector<int> inTable; std::vector<OwedDone> dones;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    /* owedpop::FlushStep: an entry whose row was in the table on an earlier link and is absent from this link's opening push was
       made or seen there while this game was away (the removal was missed) - settled, dropped here, never sent again */
    std::vector<std::string> settledRes, settledBars;
    for (std::map<std::string, T392ResPend>::iterator it = g_t392Res.begin(); it != g_t392Res.end(); ++it)
    {
        const int step = owedpop::FlushStep(it->second.sentGen, gen, g_owed.find(owedpop::TableKey(owedpop::kKindResidents, it->first)) != g_owed.end() ? 1 : 0, it->second.acked);
        if (step == owedpop::kFlushMark) { it->second.sentGen = gen; it->second.acked = 1; }
        else if (step == owedpop::kFlushSettle) settledRes.push_back(it->first);
        else if (step == owedpop::kFlushSend)
        {
            owedpop::Row r; r.kind = owedpop::kKindResidents; r.key = it->first; r.sid = it->second.sid; r.x = it->second.x; r.z = it->second.z;
            adds.push_back(r); inTable.push_back(0);
        }
    }
    for (std::map<std::string, OwedBarLocal>::iterator it = g_owedBars.begin(); it != g_owedBars.end(); ++it)
    {
        const int step = owedpop::FlushStep(it->second.sentGen, gen, g_owed.find(owedpop::TableKey(owedpop::kKindBar, it->first)) != g_owed.end() ? 1 : 0, it->second.acked);
        if (step == owedpop::kFlushMark) { it->second.sentGen = gen; it->second.acked = 1; }
        else if (step == owedpop::kFlushSettle) settledBars.push_back(it->first);
        else if (step == owedpop::kFlushSend)
        {
            owedpop::Row r; r.kind = owedpop::kKindBar; r.key = it->first; r.sid = it->first; r.x = it->second.x; r.z = it->second.z;
            adds.push_back(r); inTable.push_back(0);
        }
    }
    for (size_t i = 0; i < settledRes.size(); ++i) g_t392Res.erase(settledRes[i]);
    for (size_t i = 0; i < settledBars.size(); ++i) { g_owedBars.erase(settledBars[i]); g_owedBarSettled.insert(settledBars[i]); }
    dones.swap(g_owedDoneQ);
    ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < settledRes.size(); ++i) OwedLine("[OWED] settled kind=residents building=" + settledRes[i] + " (its row left the world server while this game was away)");
    for (size_t i = 0; i < settledBars.size(); ++i) OwedLine("[OWED] settled kind=bar town=" + settledBars[i] + " (its row left the world server while this game was away)");
    for (size_t i = 0; i < adds.size(); ++i)
    {
        const owedpop::Row& r = adds[i];
        long mark = gen;
        if (inTable[i] == 0)
        {
            std::vector<char> b;
            if (!owedpop::EncodeAdd(&b, r)) { mark = -1; ++g_owedBad; ErrorLog("[OWED] set-aside work " + OwedText(r.kind, r.key, r.sid) + " is not a valid row - kept on this game only"); }
            else if (!StoreSendOwed(b)) { ++g_owedSendFailed; continue; }
            else { ++g_owedAdds; char pb[64]; _snprintf(pb, 63, "%.0f,%.0f", r.x, r.z); pb[63] = 0; OwedLine("[OWED] stored " + OwedText(r.kind, r.key, r.sid) + " at=" + pb + " (kept on the world server until it is made)"); }
        }
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        if (r.kind == owedpop::kKindResidents) { const std::map<std::string, T392ResPend>::iterator l = g_t392Res.find(r.key); if (l != g_t392Res.end()) l->second.sentGen = mark; }
        else { const std::map<std::string, OwedBarLocal>::iterator l = g_owedBars.find(r.key); if (l != g_owedBars.end()) l->second.sentGen = mark; }
        ::LeaveCriticalSection(&g_tgLock);
    }
    OwedDoneOwedFlush();
    for (size_t i = 0; i < dones.size(); ++i) OwedSendDone(dones[i].kind, dones[i].key, dones[i].why);
}
static void T392ReofferBar(void* town, const std::string& sid)
{
    float tp[3] = { 0, 0, 0 };
    if (TownPosPod(town, tp) == 0) return;   /* unreadable on this pass: the next check-up asks again */
    const Sector s = SectorOf(tp[0], tp[2]);
    int n = 0, mark = 0;
    if (RfCountMarkPod(town, &n, &mark) == 0) return;
    int filledElsewhere = 0;
    { const coopbar::BarTable::const_iterator it = g_rfTable.find(sid); if (it != g_rfTable.end() && it->second.lastFilled >= 0.0) filledElsewhere = 1; }   /* the notebook records a roll: never a second one */
    bool settled = false;   /* T-581: the world server removed this town's owed roll (made or seen elsewhere) */
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); settled = g_owedBarSettled.find(sid) != g_owedBarSettled.end(); ::LeaveCriticalSection(&g_tgLock);
    if (g_owedLever != 0) { T392ReofferLog("bar", sid, "waiting", " why=test-lever"); return; }
    int held = 0;
    const int may = T392MayAt(s, &held);   /* T-392 fold: the load-time gate's own decision */
    const int filled = (mark == 1 && n == 0) ? 1 : 0;   /* a later engine pass used the list on this game */
    const int live = T392SectorLive(s.x, s.y) ? 1 : 0;
    const int d = townreoffer::Decide(live, 0, 0, StoreTownPeoplePending(sid.c_str(), s.x, s.y, 0, 0) == townpending::kTownHeld ? 1 : 0, may, held);
    int claim = 0;
    const int stored = OwedStored(owedpop::kKindBar, sid, &claim);
    const int settling = OwedSettling(owedpop::kKindBar, sid);   /* rolled here and not settled yet - left alone */
    /* T-581: the town's first roll is owed work (owedpop::Commit): made only by the game the world server gave the row to */
    const int act = owedpop::Checkup(settling, d, 1, owedpop::BarHas(filled, live, may, filledElsewhere, settled ? 1 : 0), stored, claim);   /* a fill elsewhere counts only with the zone live, as before */
    const std::string ow = stored != 0 ? " owed=" + N(stored) + " claim=" + owedpop::ClaimName(claim) + (settling != 0 ? std::string(" settling=made-here") : std::string()) : std::string();
    if (act == owedpop::kActWait) { T392ReofferLog("bar", sid, "waiting", ow); return; }
    if (act == owedpop::kActClaim) { OwedSendClaim(owedpop::kKindBar, sid); T392ReofferLog("bar", sid, "waiting", ow + " asked=claim"); return; }
    if (act == owedpop::kActForget || act == owedpop::kActDoneHas || act == owedpop::kActDoneGone)
    {
        if (act == owedpop::kActDoneHas) OwedSendDone(owedpop::kKindBar, sid, owedpop::kWhyHas);
        TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_owedBars.erase(sid); ::LeaveCriticalSection(&g_tgLock);
        if (filled != 0) { BarPendClear(town); ::InterlockedIncrement64(&g_t392ReFilled); return; }   /* a later engine pass used the list */
        int dropped = 0;
        const int dr = DrainBarFlyListPod(town, &dropped);   /* the gate's own end state for an area another game holds, or a roll made elsewhere */
        if (dr == 0) { ::InterlockedIncrement64(&g_t392ReFault); return; }   /* the list did not read: kept, asked again next check-up */
        ::InterlockedExchangeAdd64(&g_barFliesDropped, dropped);
        BarPendClear(town); ::InterlockedIncrement64(&g_t392ReOther);
        T392ReofferLog("bar", sid, "answered-other", (filledElsewhere != 0 || settled) ? ow + " why=filled-elsewhere" : ow);
        return;
    }
    /* kActMake: this game holds the area with the zone live, nothing shows the roll made, and the row is this game's (or only this
       game knows the work). A town loaded from its save has an unfilled list (mark 0): the engine's roll fills it first, as at load. */
    const long long allowed0 = g_barFliesAllowed;
    if (BarReofferPod(town) == 0)   /* a fault inside the engine: an owed row is handed back and tried again, spaced (owedpop::TableDone); work only this game knows is not retried */
    {
        BarPendClear(town); ::InterlockedIncrement64(&g_t392ReFault);
        if (stored != 0) OwedSendDone(owedpop::kKindBar, sid, owedpop::kWhyFault);
        TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_owedBars.erase(sid); ::LeaveCriticalSection(&g_tgLock);
        T392ReofferLog("bar", sid, "fault", ow); return;
    }
    if (BarPendHas(town)) { T392ReofferLog("bar", sid, "waiting", ow); return; }   /* the gate set it aside again */
    if (g_barFliesAllowed == allowed0) { ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("bar", sid, "answered-other", ow); return; }   /* the gate answered for another game (it drained) */
    ::InterlockedIncrement64(&g_t392ReGen);
    T392ReofferLog("bar", sid, "generated", ow);   /* the gate's allowed road queued the owed row's DONE made (OwedBarMade) */
}
/* T-392 fold (review H2): DOES THIS BUILDING ALREADY HAVE RESIDENTS ON THIS GAME - read LIVE at the moment of commitment.
   Two engine records, both written when a group is given its home: createRandomUnloadedSquad calls setHomeBuilding 0x7EBE40
   (57ea30:100), which calls Building::setResidentSquad 0x552780 (answers-loot2b R3, Read).
   (1) the building's own residents hand, Building::residentSquad at +0xD0 (Building.h:195; items.cpp:2765 kBldResidentSquad
       reads its id words from +0xD8) - resolved with hand::getSquad, which answers for a sleeping group too;
   (2) the group's own home-building hand, Ownerships(+0x148)+0x38, on the town's resident list (Town+0xE0 bucket 1, T2NodesPod -
       the walk towns2 already makes, towngen.cpp T2GroupPod).
   A copy of another game's group is created with that building as its home (spawn.cpp:789, RootObjectFactory::create's
   homeBuilding), so the same records cover copies. A group made from its world record is given its home building from that record
   (store.cpp CreateUnknownSquad; StoreGiveOwedHomes when the building was not loaded then - below), so the first read finds it too.
   1 = has residents; 0 = none; -1 = could not be read (the caller waits). */
static int T392ResidentHandPod(void* building)
{
    __try { return ((const hand*)((const char*)building + 0xD0))->getSquad() != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int T392GroupHomePod(const unsigned char* h, void* building)
{
    __try
    {
        void* p = (void*)((const hand*)h)->getSquad();
        if (p == 0) return 0;
        return ((void*)((const hand*)((const char*)p + 0x148 + 0x38))->asBuilding() == building) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* THE GROUP'S HOME BUILDING, CARRIED ON ITS WORLD RECORD. A group made from its record (store.cpp CreateUnknownSquad - another game's
   residents placed from their records, or this game's own after a crash-style restart) is built with no home building, so neither read above
   names it and a residents re-offer would fill its building a second time. Its record carries the building's position key - read off
   the live group whenever its record is written (T591HomeKey: the key T392Key makes, built with plain field reads) - and the group is
   given that home when it is made: Ownerships::setHomeBuilding (spawn.cpp PlatoonSetHomeBuildingPod), the call
   createRandomUnloadedSquad makes for a new group, which sets the building's residents hand too. That hand is read back
   (T591ResidentIsPod), so a call that did not set it is counted rather than assumed. */
static int T591HomeBuildingPod(const void* platoon, void** out)
{
    *out = 0;
    __try { *out = (void*)((const hand*)((const char*)platoon + 0x148 + 0x38))->asBuilding(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
static int T591ResidentIsPod(void* building, void* platoon)
{
    __try { return ((void*)((const hand*)((const char*)building + 0xD0))->getSquad() == platoon) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int T591HomeKey(const void* platoon, char* out, int cap)
{
    if (out == 0 || cap <= 0) return -1;
    out[0] = 0;
    void* b = 0;
    if (platoon == 0 || T591HomeBuildingPod(platoon, &b) == 0) return -1;
    if (b == 0) return 0;   /* no home building */
    const int kc = BoxKeyCap();
    if (kc <= 0 || kc > cap) return -1;
    if (ObjectPositionKey(b, out, kc, "", 3, 1) == 0) { out[0] = 0; return -1; }   /* 3: not booked as a box key; 1: the position by plain field read */
    for (const char* c = out; *c != 0; ++c) if (*c == '\t' || *c == '\r' || *c == '\n') { out[0] = 0; return -1; }   /* a record line could not hold it */
    return 1;
}
static int T591GiveHomeTo(void* platoon, void* building)
{
    if (platoon == 0 || building == 0) return 0;
    if (PlatoonSetHomeBuildingPod(platoon, building) != 1) return 0;
    return T591ResidentIsPod(building, platoon) == 1 ? 1 : 2;
}
/* GROUPS MADE HERE FROM A RECORD WITH NO HOME KEY (store.cpp StoreKeylessRecordGroups - records written before
   the key existed) have no home building, so the reads below do not find them. A building whose residents check would answer "none"
   answers "could not be read - wait" while such a group of its town is of its resident template and its record places it within
   kT591KeylessRadius of the building (townreoffer::KeylessHoldsBuilding). The wait ends when the group is gone, or when a new-build game
   rewrites its record with the key - its home is then owed and given at this building's next check (StoreGiveOwedHomes, above). */
const float kT591KeylessRadius = 30.0f;   /* the distance piece A's removed test used - this code reads no building bounds */
static int T591GroupTemplatePod(void* p, void** out)
{
    *out = 0;
    __try { *out = *(void* const*)((const char*)p + 0x108); return 1; }   /* Platoon+0x108: the squad template (the read piece A made) */
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
static int T591KeylessHolds(void* town, void* building)
{
    char ksid[128];
    if (TownSidPod(town, ksid, 128) == 0) ksid[0] = 0;   /* not read: every keyless group is looked at */
    KeylessGroup kg[32];
    const int kn = StoreKeylessRecordGroups(std::string(ksid), kg, 32);
    if (kn == 0) return 0;
    int holds = 0;
    if (kn < 0) holds = 1;   /* more than the copy holds: not a complete answer */
    else
    {
        void* bt = 0;
        if (T515BldTemplatePod(building, &bt) == 0) holds = 1;
        else if (bt != 0)
        {
            float bp[3] = { 0, 0, 0 };
            const int bpOk = TownPosPod(building, bp);
            for (int i = 0; i < kn && holds == 0; ++i)
            {
                void* gt = 0;
                const int tr = T591GroupTemplatePod(kg[i].platoon, &gt);
                holds = townreoffer::KeylessHoldsBuilding(tr, gt, bt, (bpOk != 0 && kg[i].posKnown != 0) ? 1 : 0, kg[i].x - bp[0], kg[i].z - bp[2], kT591KeylessRadius);
            }
        }
    }
    if (holds != 0) ++g_t591KeylessWaits;
    return holds;
}
static int T392BuildingHasResidents(void* town, void* building)
{
    {   /* a group made from its record while this building was not loaded here is given its home now, before the building's hand is read */
        char k[kT392KeyRoom];
        if (T392Key(building, k) != 0 && StoreGiveOwedHomes(k, building) > 0) ::InterlockedIncrement64(&g_t591HomeGivenAtCheck);
    }
    const int own = T392ResidentHandPod(building);
    if (own != 0) return own;
    static unsigned char hs[kT2Nodes][kT2HandBytes];   /* MAIN THREAD only (the re-offer) */
    int total = 0;
    float budget = 0.0f;
    const int r = T2NodesPod(town, 1, hs, kT2Nodes, &total, &budget);   /* 1 = the residents' list */
    if (r == 0) return -1;
    if (r == 2) return T591KeylessHolds(town, building) != 0 ? -1 : 0;   /* no population manager: there is no list to be on (a keyless record-made group still holds it) */
    const int n = total < kT2Nodes ? total : kT2Nodes;
    int unknown = (total > kT2Nodes) ? 1 : 0;   /* a list longer than the copy is not a complete answer */
    for (int i = 0; i < n; ++i)
    {
        const int m = T392GroupHomePod(hs[i], building);
        if (m == 1) return 1;
        if (m < 0) unknown = 1;
    }
    if (unknown != 0) return -1;
    return T591KeylessHolds(town, building) != 0 ? -1 : 0;   /* "none" only when no keyless record-made group holds the building */
}
/* a set-aside building waits while a note of its town holds, and a note can hold for a long time (it has no
   position, its area is not read here, or it sleeps in another game's area) - no timer ends the wait. Every wait on a note is counted
   (pending[...reofferWaitsOnNote]) and the first 20 name the note. MAIN THREAD */
static void T591WaitLine(const std::string& sid, const std::string& key, const char* holder, int pendAside)
{
    ::InterlockedIncrement64(&g_t591WaitOnNote);
    if (::InterlockedIncrement(&g_t591WaitLines) > 20) return;
    DebugLog("[TOWN] reoffer kind=residents town=" + sid + " result=waiting why=note-holds holder=" + std::string((holder != 0 && holder[0] != 0) ? holder : "?") + " building=" + key
             + " setAsideBy=" + std::string(pendAside != 0 ? "notebook-pending" : "gate") + " (first 20 lines; pending[...reofferWaitsOnNote] counts every wait)");
}
/* ONE RE-RUN OF A BUILDING'S RESIDENTS, MAIN THREAD - the re-offer's and the town refill's one copy: Kenshi's populateBuilding
   through WorldGenRerunPopulate inside this run's own slot (its made / pending and asked / reached counts, readable
   until the next run), the building-state step skipped when it already ran for the building (atLoad 1), and this run's
   own add to the town's resident budget undone. Returns WorldGenRerunPopulate's answer (1 ran, 0 set aside, -1 no
   original or a fault); *st and *pa name the state step and the budget for the caller's line. */
static int T392RerunBuilding(void* town, void* factory, void* b, const Sector& s, int atLoad, std::string* st, std::string* pa)
{
    int b0n = 0; float b0f = 0.0f; const int b0ok = T392ResBudgetPod(town, &b0n, &b0f);   /* the town's resident budget right before this SECOND engine call (decomp_57ed90:198-199 adds to it) */
    int rr0 = -1, rd0 = -1, rr1 = -1, rd1 = -1;
    T515RuinPod(b, &rr0, &rd0);
    const long long skip0 = g_t515RerunSkipped;
    g_t515RerunBld = b; g_t515RerunAtLoad = atLoad;   /* detour_bldState skips the step for this building when it ran at load */
    g_t580RunMade = 0; g_t580RunPending = 0; g_t581RunAsked = 0; g_t581RunReached = 0; g_t580RunSx = s.x; g_t580RunSy = s.y; g_t580RunTid = ::GetCurrentThreadId();   /* this re-run's own slot */
    const int r = WorldGenRerunPopulate(factory, b);   /* the entry stays until the run actually happened */
    g_t580RunTid = 0;
    g_t515RerunBld = 0; g_t515RerunAtLoad = 0;
    T515RuinPod(b, &rr1, &rd1);
    *st = " stateAtLoad=" + N(atLoad) + " stateSkipped=" + N(g_t515RerunSkipped - skip0)
                           + " ruin=" + N(rr0) + "->" + N(rr1) + " doors=" + N(rd0) + "->" + N(rd1);
    if (r == 1)
    {
        /* undo EXACTLY this re-run's own add, so every game keeps its one load-time add.
           Only when the int and the float moved by the same positive amount; otherwise the fields are left alone. */
        int b1n = 0; float b1f = 0.0f;
        if (b0ok == 0 || T392ResBudgetPod(town, &b1n, &b1f) == 0) { ::InterlockedIncrement64(&g_t392RerunPopUnread); *pa = " popAdd=unread popUndone=unread"; }
        else
        {
            const long long dn = (long long)b1n - (long long)b0n;
            const double df = (double)b1f - (double)b0f;
            ::InterlockedExchangeAdd64(&g_t392RerunPopAdd, dn);
            const long long u = townreoffer::RerunPopUndo(dn, df);
            if (u > 0 && T392ResBudgetSetPod(town, (int)((long long)b1n - u), b0f) != 0) { ::InterlockedIncrement64(&g_t392RerunPopUndone); ::InterlockedExchangeAdd64(&g_t392RerunPopUndoneSum, u); *pa = " popAdd=" + N(dn) + " popUndone=" + N(u); }
            else if (u > 0) { ::InterlockedIncrement64(&g_t392RerunPopUnread); *pa = " popAdd=" + N(dn) + " popUndone=unread"; }
            else if (dn == 0 && df == 0.0) *pa = " popAdd=0 popUndone=0";   /* nothing was added: nothing to undo */
            else { ::InterlockedIncrement64(&g_t392RerunPopMismatch); *pa = " popAdd=" + N(dn) + " popUndone=mismatch"; }
        }
    }
    return r;
}
static void T392ReofferResidents(void* town, const std::string& sid)
{
    void* factory = g_t392Factory;
    if (factory == 0) factory = OwedWorldFactoryPod();   /* T-581: a world whose zones all loaded from their saves ran no populateBuilding */
    if (factory == 0) return;
    std::vector<std::string> keys; std::vector<T392ResPend> rows;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    {
        /* T-581: this game's own set-aside entries for the town, and every owed row for it this game has no entry for (a zone saved
           before its residents were made, here or on another game). An owed row's building-state step ran when its zone was first
           loaded on this game - set aside, refused or made - so the re-run never repeats it (stateAtLoad 1). */
        std::map<std::string, T392ResPend> view;
        for (std::map<std::string, T392ResPend>::const_iterator it = g_t392Res.begin(); it != g_t392Res.end(); ++it) if (it->second.sid == sid) view[it->first] = it->second;
        for (owedpop::Table::const_iterator o = g_owed.begin(); o != g_owed.end(); ++o)
            if (o->second.kind == owedpop::kKindResidents && o->second.sid == sid && view.find(o->second.key) == view.end()
                && townpending::KeptGiveUp(g_t580KeptByKey.count(o->second.key) != 0 ? g_t580KeptByKey[o->second.key] : 0) == 0)   /* T-580's give-up holds for the session: the row was handed back */
            { T392ResPend p; p.sid = sid; p.x = o->second.x; p.z = o->second.z; p.stateAtLoad = 1; view[o->second.key] = p; }
        /* T-392 fold (review MED): ROUND-ROBIN - start just after the last key this town's check-up looked at and wrap once, so
           every waiting building is looked at however many wait; at most 8 per check-up */
        std::string after;
        { const std::map<std::string, std::string>::const_iterator c = g_t392ResCursor.find(sid); if (c != g_t392ResCursor.end()) after = c->second; }
        const std::map<std::string, T392ResPend>::const_iterator start = after.empty() ? view.begin() : view.upper_bound(after);
        for (std::map<std::string, T392ResPend>::const_iterator it = start; it != view.end() && keys.size() < 8; ++it) { keys.push_back(it->first); rows.push_back(it->second); }
        for (std::map<std::string, T392ResPend>::const_iterator it = view.begin(); it != start && keys.size() < 8; ++it) { keys.push_back(it->first); rows.push_back(it->second); }
        if (!keys.empty()) g_t392ResCursor[sid] = keys.back();
    }
    ::LeaveCriticalSection(&g_tgLock);
    if (keys.empty()) return;
    /* T-580: decision 34 is asked for the TOWN's area, as the creation gate the re-run goes through asks it (one rule, one area);
       the building's area stands in only when the town's will not read, as the gate falls back to the spawn position. */
    float tpR[3] = { 0, 0, 0 }; int rSx = -1, rSy = -1;
    if (TownPosPod(town, tpR) != 0) { const Sector ts = SectorOf(tpR[0], tpR[2]); rSx = ts.x; rSy = ts.y; }
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const Sector s = SectorOf(rows[i].x, rows[i].z);
        char holderW[192]; holderW[0] = 0;   /* the note that holds, named on the wait line */
        const int pend = StoreTownPeoplePending(sid.c_str(), rSx >= 0 ? rSx : s.x, rSx >= 0 ? rSy : s.y, holderW, 192) == townpending::kTownHeld ? 1 : 0;
        holderW[191] = 0;
        std::string extra = " building=" + keys[i];
        if (g_owedLever != 0) { T392ReofferLog("residents", sid, "waiting", extra + " why=test-lever"); continue; }
        int held = 0;
        const int may = T392MayAt(s, &held);   /* T-392 fold: the load-time gate's own decision */
        const int d = townreoffer::Decide(T392SectorLive(s.x, s.y) ? 1 : 0, 0, 0, pend, may, held);
        if (d != townreoffer::kReGenerate && d != townreoffer::kReOther)
        {
            if (pend != 0) T591WaitLine(sid, keys[i], holderW, rows[i].pendAside);   /* each wait on a note is counted, the first 20 named */
            T392ReofferLog("residents", sid, "waiting", extra); continue;
        }
        int claim = 0;
        const int stored = OwedStored(owedpop::kKindResidents, keys[i], &claim);
        if (stored != 0) extra += " owed=" + N(stored) + " claim=" + owedpop::ClaimName(claim);
        const int settling = OwedSettling(owedpop::kKindResidents, keys[i]);   /* made here and not settled yet - left alone */
        if (settling != 0) extra += " settling=made-here";
        void* const b = ObjectByPositionKey(keys[i].c_str());
        const int found = (b != 0) ? 1 : (ObjectByPositionKeyVerdict() == 1 ? 0 : -1);   /* 0 = kObjKeyNone: a complete search of the live zones found no such building */
        const int has = (b != 0) ? T392BuildingHasResidents(town, b) : -1;   /* T-392 fold (review H2): the live read, at the moment of commitment */
        /* T-581: owedpop::Commit - an owed row is made only by the game the world server gave it to */
        const int act = owedpop::Checkup(settling, d, found, has, stored, claim);
        if (act == owedpop::kActWait) { T392ReofferLog("residents", sid, "waiting", extra); continue; }
        if (act == owedpop::kActClaim) { OwedSendClaim(owedpop::kKindResidents, keys[i]); T392ReofferLog("residents", sid, "waiting", extra + " asked=claim"); continue; }
        if (act == owedpop::kActDoneGone || (act == owedpop::kActForget && found == 0))
        {
            T392ResErase(keys[i].c_str());
            if (act == owedpop::kActDoneGone) OwedSendDone(owedpop::kKindResidents, keys[i], owedpop::kWhyGone);
            ::InterlockedIncrement64(&g_t392ReGone); T392ReofferLog("residents", sid, "gone", extra); continue;
        }
        if (act == owedpop::kActDoneHas || (act == owedpop::kActForget && has > 0))   /* T-438: the furniture and faction were set at load; only the squads were left, and they are here */
        {
            T392ResErase(keys[i].c_str());
            if (act == owedpop::kActDoneHas) OwedSendDone(owedpop::kKindResidents, keys[i], owedpop::kWhyHas);
            ::InterlockedIncrement64(&g_t392ReHasRes); ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("residents", sid, "answered-other", extra + " why=has-residents"); continue;
        }
        /* T-438 (a) (t438-a-towngen, manager 2026-10-02): another game holds the area - the furniture and faction were set by the
           set-aside run at load, so the entry is erased WITHOUT a second engine call (it would add the town's resident budget again
           and refuse its own group). The other game's residents arrive as copies. */
        if (act == owedpop::kActForget) { T392ResErase(keys[i].c_str()); ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("residents", sid, "answered-other", extra + (has < 0 ? " why=other-holds residents=unread ran=no" : " why=other-holds ran=no")); continue; }
        /* kActMake: this game holds the area with the zone live, the building has no residents, and the row is this game's (or
           only this game knows the work) */
        int atLoad = 1;   /* T-515: read LIVE under the lock - the step may have marked the entry after this check-up copied its rows; an owed row with no entry here: 1 (above) */
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        { const std::map<std::string, T392ResPend>::const_iterator e = g_t392Res.find(keys[i]); if (e != g_t392Res.end()) atLoad = e->second.stateAtLoad; }
        ::LeaveCriticalSection(&g_tgLock);
        std::string st, pa;
        const int r = T392RerunBuilding(town, factory, b, s, atLoad, &st, &pa);   /* the entry stays until the run actually happened */
        if (r < 0) { T392ResErase(keys[i].c_str()); if (stored != 0) OwedSendDone(owedpop::kKindResidents, keys[i], owedpop::kWhyFault); ::InterlockedIncrement64(&g_t392ReFault); T392ReofferLog("residents", sid, "fault", extra); continue; }   /* an owed row is handed back and tried again, spaced; work only this game knows is not retried */
        if (r == 0) { T392ReofferLog("residents", sid, "waiting", extra); continue; }   /* refused again: the entry is still there (the gate counted the repeat) */
        {
            /* T-580's per-run slot says what this re-run made; owedpop::AfterRerun says what the owed row does with it (T-581) */
            const long long madeR = g_t580RunMade, pendR = g_t580RunPending;
            const int keep = townpending::RerunKeep(madeR, pendR);
            int kept = 0;
            if (keep != 0) { TgLockInit(); ::EnterCriticalSection(&g_tgLock); kept = ++g_t580KeptByKey[keys[i]]; ::LeaveCriticalSection(&g_tgLock); ++g_t580RerunKept; }
            const long long refusedR = g_t581RunAsked - g_t581RunReached;
            const int after = owedpop::AfterRerun(madeR, keep, keep != 0 ? townpending::KeptGiveUp(kept) : 0, stored, refusedR);
            if (after == owedpop::kRunRelease)   /* nothing made, refused for another cause: the entry stays, the row is handed back like a lost hold */
            {
                OwedSendKey(owedpop::kOpRelease, owedpop::kKindResidents, keys[i], " why=refused-in-rerun refused=" + N(refusedR));
                T392ReofferLog("residents", sid, "waiting", extra + " why=refused-in-rerun made=0 refused=" + N(refusedR));
                continue;
            }
            if (after == owedpop::kRunKeep)   /* T-580: no residents made - the entry stays, up to the bound; an owed row stays this game's (no DONE) */
            {
                if (kept == 1) DebugLog("[TOWN] reoffer kind=residents town=" + sid + " result=waiting why=notebook-pending-in-rerun" + extra + " made=0 refused=" + N(pendR) + pa + st
                                        + " (the entry stays; first only per building; given up after " + N((long long)townpending::kMaxKeptReruns) + " such re-runs this session)");
                continue;
            }
            if (after == owedpop::kRunGiveUpRelease || after == owedpop::kRunGiveUpLocal)
            {
                T392ResErase(keys[i].c_str()); ++g_t580RerunGivenUp;
                /* T-581: the owed row is NOT removed - it is handed back (RELEASE) so another holder, or a later session, offers it
                   again; this game skips it for the rest of the session (g_t580KeptByKey, cleared with the world) */
                if (after == owedpop::kRunGiveUpRelease) OwedSendKey(owedpop::kOpRelease, owedpop::kKindResidents, keys[i], " why=notebook-pending-in-rerun given up this session");
                DebugLog("[TOWN] reoffer kind=residents town=" + sid + " result=given-up why=notebook-pending-in-rerun" + extra + " reruns=" + N((long long)kept)
                         + " - the building's residents are not offered again this session" + (after == owedpop::kRunGiveUpRelease ? std::string(" (owed row handed back)") : std::string()));
                continue;
            }
            T392ResErase(keys[i].c_str());   /* it ran: a squad was made, or the engine chose none and nothing refused it */
            if (after == owedpop::kRunDoneMade) OwedSettleNote(owedpop::kKindResidents, keys[i]);   /* made once, here (the run's made count > 0): DONE made goes at this game's next finished save */
            else if (after == owedpop::kRunDoneEmpty) OwedSendDone(owedpop::kKindResidents, keys[i], owedpop::kWhyEmpty);   /* the engine chose no residents and nothing refused one */
            extra += " made=" + N(madeR);
        }
        ::InterlockedIncrement64(&g_t392ReGen); T392ReofferLog("residents", sid, "generated", extra + pa + st);   /* T-438 (b): only a GENERATE re-offer reaches the re-run (ReofferRunsPopulate) */
    }
}
static void T392Reoffer(void* town)
{
    if (town == 0 || !g_on || RoleIsSingle()) return;
    if (::GetCurrentThreadId() != g_mainThread) return;
    OwedFlush();   /* T-581: this game's set-aside work reaches the world server before anything is decided about it */
    const size_t owedRows = OwedRowCount();
    const bool barLocal = orig_barFlies != 0 && BarPendHas(town);
    const bool resAny = T392ResCount() != 0 || owedRows != 0;
    if (!barLocal && !resAny) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0) return;
    const std::string key(sid);
    const bool barOwed = orig_barFlies != 0 && (barLocal || OwedBarHas(sid));   /* T-581: or the town's first roll is an owed row */
    if (barOwed) T392ReofferBar(town, key);
    if (resAny) T392ReofferResidents(town, key);
    OwedFlush();   /* T-581: the DONEs this check-up queued */
}
static void T392Teardown()
{
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_t392Res.clear(); g_t392ResCursor.clear(); g_t392DeferLogged.clear(); g_t392WaitLogged.clear(); g_t392AsideLogged.clear(); g_owedBars.clear(); g_owedBarSettled.clear(); ::LeaveCriticalSection(&g_tgLock);   /* T-581: this game's own owed work belongs to the world that set it aside; the world server's rows stay */
    g_t392Factory = 0; g_t392PopTid = 0; g_t392PopAside = 0;
    /* the people this world made are gone with it unless a save asked for after their make is still being written
       (owedpop::SettleKeep: that save settles them when it finishes, or OwedTick drops them when it does not); a dropped row stays
       owed (a held one is handed back by OwedTick once this game no longer holds its area live) and is made again */
    size_t dropped = 0, kept = 0;
    const long inFlight = StoreSaveInFlightSeq(0);
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (owedpop::SettleSet::iterator it = g_owedSettle.begin(); it != g_owedSettle.end(); )
    {
        if (it->second.closed == 0) { it->second.closed = 1; it->second.closeSeq = inFlight; }   /* a make kept from an earlier close keeps its own bound */
        if (owedpop::SettleKeep(1, it->second.seq, StoreSaveInFlightSeq(it->second.closeSeq)) != 0) { ++kept; ++it; } else { ++dropped; g_owedSettle.erase(it++); }
    }
    g_owedSettleDropped += (long long)dropped;
    ::LeaveCriticalSection(&g_tgLock);
    if (dropped != 0 || kept != 0)
        OwedLine("[OWED] world closed with " + N((long long)dropped) + " made row(s) no save holds - they stay owed and are made again by the game holding the area; "
                 + N((long long)kept) + " kept for the save still being written (asked for as request " + N((long long)inFlight) + ")");
}
/* T-581: MAIN THREAD (TownGenTick), once a second. This game's own work goes up; a row this game holds is handed back the moment
   this game no longer holds its area with the zone live (owedpop::KeepClaim), so the game that does can make it. */
static void OwedTick()
{
    if (RoleIsSingle()) return;
    static DWORD last = 0; const DWORD now = ::GetTickCount();
    if (now - last < 1000) return;
    last = now;
    OwedFlush();
    std::vector<std::string> lapsed;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (owedpop::SettleSet::iterator it = g_owedSettle.begin(); it != g_owedSettle.end(); )   /* a closed world's make whose save ended without settling it - link up or not */
    {
        if (owedpop::SettleKeep(it->second.closed, it->second.seq, it->second.closed != 0 ? StoreSaveInFlightSeq(it->second.closeSeq) : 0) != 0) ++it;
        else { lapsed.push_back(it->first); ++g_owedSettleDropped; g_owedSettle.erase(it++); }
    }
    ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < lapsed.size(); ++i) OwedLine("[OWED] " + lapsed[i] + ": made in a world since closed, and the save asked for after it ended without settling it - owed again");
    if (g_linkedCached == 0) return;
    std::vector<owedpop::Row> mine; std::vector<int> mineSettling; std::vector<owedpop::Settle> again;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (owedpop::Table::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it)
        if (it->second.claimant == (int)owedpop::kClaimYou) { mine.push_back(it->second); mineSettling.push_back(g_owedSettle.find(it->first) != g_owedSettle.end() ? 1 : 0); }
    for (owedpop::SettleSet::const_iterator it = g_owedSettle.begin(); it != g_owedSettle.end(); ++it)   /* a made row read nobody's (the link came back) is asked for again */
    { const owedpop::Table::const_iterator r = g_owed.find(it->first); if (r != g_owed.end() && owedpop::SettleStep(1, it->second.held, r->second.claimant) == owedpop::kSetReclaim) again.push_back(it->second); }
    ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < again.size(); ++i) { ++g_owedReclaims; OwedSendKey(owedpop::kOpClaim, again[i].kind, again[i].key, " why=made-here-not-saved"); }
    for (size_t i = 0; i < mine.size(); ++i)
    {
        if (owedpop::SettleStep(mineSettling[i], 1, (int)owedpop::kClaimYou) == owedpop::kSetHold) continue;   /* made here and not saved - kept wherever the player goes */
        const Sector s = SectorOf(mine[i].x, mine[i].z);
        int h = 0; const int may = T392MayAt(s, &h);
        const int live = T392SectorLive(s.x, s.y) ? 1 : 0;
        if (owedpop::KeepClaim(live, may, (int)g_owedLever) != 0) continue;
        OwedSendKey(owedpop::kOpRelease, mine[i].kind, mine[i].key, " zoneLive=" + N(live) + " may=" + N(may) + " lever=" + N((long long)g_owedLever));
    }
}
static void OwedReset()
{
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_owed.clear(); ::LeaveCriticalSection(&g_tgLock);
    g_owedAsked.clear(); g_owedReleased.clear();
}
static void OwedArrive(const std::vector<char>& payload)
{
    owedpop::Msg m;
    if (owedpop::Decode(payload.empty() ? 0 : &payload[0], payload.size(), &m) == 0 || (m.op != owedpop::kOpRows && m.op != owedpop::kOpGone))
    { ++g_owedBad; ErrorLog("[OWED] malformed OWED from the world server - ignored"); return; }
    if (m.op == owedpop::kOpRows)
    {
        long long mineN = 0;
        for (size_t i = 0; i < m.rows.size(); ++i)
        {
            const owedpop::Row& r = m.rows[i];
            const std::string tk = owedpop::TableKey(r.kind, r.key);
            bool grant = false;
            TgLockInit(); ::EnterCriticalSection(&g_tgLock);
            { const owedpop::Table::const_iterator p = g_owed.find(tk); grant = r.claimant == (int)owedpop::kClaimYou && (p == g_owed.end() || p->second.claimant != (int)owedpop::kClaimYou); }
            g_owed[tk] = r;
            if (r.kind == owedpop::kKindResidents) { const std::map<std::string, T392ResPend>::iterator l = g_t392Res.find(r.key); if (l != g_t392Res.end()) l->second.acked = 1; }
            else { const std::map<std::string, OwedBarLocal>::iterator l = g_owedBars.find(r.key); if (l != g_owedBars.end()) l->second.acked = 1; }
            ::LeaveCriticalSection(&g_tgLock);
            g_owedAsked.erase(tk); g_owedReleased.erase(tk);
            ++g_owedRowsIn;
            if (r.claimant == (int)owedpop::kClaimYou) ++mineN;
            if (grant) { ++g_owedGrants; OwedLine("[OWED] given " + OwedText(r.kind, r.key, r.sid) + " to this game (it makes it at its next town check-up while it holds the area)"); }
        }
        OwedLine("[OWED] rows in=" + N((long long)m.rows.size()) + " thisGame's=" + N(mineN) + " table=" + N((long long)OwedRowCount()));
        return;
    }
    const std::string tk = owedpop::TableKey(m.kind, m.key);
    std::string sid = (m.kind == owedpop::kKindBar) ? m.key : std::string("?");
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    { const owedpop::Table::const_iterator it = g_owed.find(tk); if (it != g_owed.end()) sid = it->second.sid; }
    g_owed.erase(tk);
    const bool wasSettling = g_owedSettle.erase(tk) != 0;   /* the row is gone - nothing is left to settle */
    g_owedDoneOwed.erase(tk);   /* its DONE is answered */
    if (owedpop::GoneKeepsLocal(m.why) != 0)   /* the world server refused to store it (its cap): this game makes it as before - never sent again */
    {
        if (m.kind == owedpop::kKindResidents) { const std::map<std::string, T392ResPend>::iterator l = g_t392Res.find(m.key); if (l != g_t392Res.end()) l->second.sentGen = -1; }
        else { const std::map<std::string, OwedBarLocal>::iterator l = g_owedBars.find(m.key); if (l != g_owedBars.end()) l->second.sentGen = -1; }
    }
    else if (m.kind == owedpop::kKindResidents) g_t392Res.erase(m.key);   /* made, or seen there, by some game: this game's own entry ends too */
    else { g_owedBars.erase(m.key); g_owedBarSettled.insert(m.key); }
    ::LeaveCriticalSection(&g_tgLock);
    g_owedAsked.erase(tk); g_owedReleased.erase(tk);
    if (owedpop::GoneKeepsLocal(m.why) != 0) ++g_owedRefusedFull; else ++g_owedGoneIn;
    OwedLine("[OWED] removed " + OwedText(m.kind, m.key, sid) + " why=" + owedpop::WhyName(m.why)
             + (wasSettling ? std::string(" (made here and not saved yet - removed on another game's report)") : std::string()));
}
/* MAIN THREAD (store.cpp, once a profile save's own quick.save is on disk) - this game's own profile save `saveName`, asked for as
   save request `reqSeq`, has finished. Every make noted before that request (owedpop::SettleDue) is in that save: its row's DONE
   made is owed from now until the world server's GONE (OwedDoneOwedFlush - at once when the link is up, else on the next link). */
static void OwedSaveFinished(const std::string& saveName, long reqSeq)
{
    std::vector<owedpop::Settle> due; size_t left = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    due = owedpop::SettleDue(&g_owedSettle, reqSeq); left = g_owedSettle.size(); g_owedSettled += (long long)due.size();
    for (size_t i = 0; i < due.size(); ++i) { OwedDoneOwed d; d.kind = due[i].kind; d.key = due[i].key; g_owedDoneOwed[owedpop::TableKey(d.kind, d.key)] = d; }
    ::LeaveCriticalSection(&g_tgLock);
    OwedDoneOwedFlush();
    if (!due.empty() || left != 0)
        OwedLine("[OWED] save '" + saveName + "' finished (save request " + N((long long)reqSeq) + "): " + N((long long)due.size()) + " made row(s) settled, "
                 + N((long long)left) + " made after it was asked for still wait");
}
static std::string OwedLever(const std::string& arg)
{
    if (arg == "on" || arg == "off")
    {
        ::InterlockedExchange(&g_owedLever, arg == "on" ? 1 : 0);
        DebugLog("[OWED] owedtest aside " + arg + (arg == "on" ? " - this game sets aside every building's residents and bar roll it would make, and its town check-up makes none (TEST-ONLY)" : " - this game makes its owed work again"));
        return "ok owedtest aside " + arg;
    }
    std::vector<owedpop::Row> rows; long long localRes = 0, localBars = 0, mineN = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (owedpop::Table::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it) rows.push_back(it->second);
    localRes = (long long)g_t392Res.size(); localBars = (long long)g_owedBars.size();
    ::LeaveCriticalSection(&g_tgLock);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].claimant == (int)owedpop::kClaimYou) ++mineN;
        if (i < 64) { char pb[64]; _snprintf(pb, 63, "%.0f,%.0f", rows[i].x, rows[i].z); pb[63] = 0; DebugLog("[OWED] row " + OwedText(rows[i].kind, rows[i].key, rows[i].sid) + " at=" + pb + " claim=" + owedpop::ClaimName(rows[i].claimant)); }
    }
    long long settlingN = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); settlingN = (long long)g_owedSettle.size(); ::LeaveCriticalSection(&g_tgLock);
    const std::string sum = "rows=" + N((long long)rows.size()) + " thisGame's=" + N(mineN) + " localResidents=" + N(localRes) + " localBars=" + N(localBars) + " lever=" + N((long long)g_owedLever)
                            + " settling=" + N(settlingN);
    DebugLog("[OWED] show " + sum);
    return "ok owedtest " + sum;
}
static std::string OwedReport()
{
    long long rows = 0, mineN = 0, localRes = 0, localBars = 0, queued = 0, settling = 0, doneOwed = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    rows = (long long)g_owed.size(); settling = (long long)g_owedSettle.size(); doneOwed = (long long)g_owedDoneOwed.size();
    for (owedpop::Table::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it) if (it->second.claimant == (int)owedpop::kClaimYou) ++mineN;
    localRes = (long long)g_t392Res.size(); localBars = (long long)g_owedBars.size(); queued = (long long)g_owedDoneQ.size();
    ::LeaveCriticalSection(&g_tgLock);
    return " owed[rows,thisGame's,localResidents,localBars,doneQueued,added,claims,given,made,doneHas,doneGone,doneFault,released,removedIn,rowsIn,bad,sendFailed,asideOwed,asideLever,lever,doneEmpty,refusedFull,settling,settleNoted,settledAtSave,settleDropped,reclaims,doneOwed,notThisSave]="
           + N(rows) + "," + N(mineN) + "," + N(localRes) + "," + N(localBars) + "," + N(queued) + "," + N(g_owedAdds) + "," + N(g_owedClaims) + "," + N(g_owedGrants)
           + "," + N(g_owedMade) + "," + N(g_owedDoneHas) + "," + N(g_owedDoneGone) + "," + N(g_owedDoneFault) + "," + N(g_owedReleases) + "," + N(g_owedGoneIn)
           + "," + N(g_owedRowsIn) + "," + N(g_owedBad) + "," + N(g_owedSendFailed) + "," + N(g_owedAsideLoad) + "," + N(g_owedLeverAside) + "," + N((long long)g_owedLever) + "," + N(g_owedDoneEmpty) + "," + N(g_owedRefusedFull)
           + "," + N(settling) + "," + N(g_owedSettleNoted) + "," + N(g_owedSettled) + "," + N(g_owedSettleDropped) + "," + N(g_owedReclaims) + "," + N(doneOwed) + "," + N(g_owedNotThisSave);
}
static long long T392BarWaitingNow() { size_t c = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); c = g_barPend.size(); ::LeaveCriticalSection(&g_tgLock); return (long long)c; }
/* ------------------------------------------------------------------------------------------------------------------
   AN EMPTY TOWN THAT NOTHING ACCOUNTS FOR IS REFILLED ONCE, BUILDING BY BUILDING, BY THE GAME THAT HOLDS ITS AREA; A TOWN WITH A
   RECORDED LOSS STAYS WIPED.
   The world server keeps, per town, how many of its groups were deleted and whether a game has refilled it (src/common/townrefill.h;
   TOWN_LOSS rows at WELCOME and on change, g_erLoss). At a town's check-up (main thread, one look per town per 30 s) the game that
   holds the town's area with its zone live and the town in loaded range, held for the refill's settle time, with the opening push
   and this world's record snapshot in, reads the town's residents list live and, when it is empty, the town's world records
   (StoreTownRecordTally): townrefill::RefillDecide. When it says refill and the empty-town verdict right after it (EmptyTownCheck)
   says nothing accounts for the town, the town's loaded buildings are listed by their position keys in g_tfRun - the only refill
   state, THIS game's own, dropped when the town's refill ends, when this game no longer holds the area, when the town's row shows a
   loss or a refill, and at teardown. TownFillTick decides each building at the moment of acting, a few per frame
   (townrefill::FillBuilding: the holder, the row, this game's own deletes, the building's owner, whether Kenshi's town population
   fills it, whether a loaded group or a living world record names it as home) and makes a building's residents through the
   re-offer's own call (T392RerunBuilding: populateBuilding, gated and crash-guarded, the building's state step not repeated, the
   town's resident budget kept). Kenshi's whole-town rebuild is never used for it. When no building is left to handle and one was
   filled, the town is marked refilled here and REFILLED goes to the world server (retried every tick until it is sent), so no other
   game and no restart refills it again.
   ------------------------------------------------------------------------------------------------------------------ */
const DWORD kErLookMs = 30000;
const long long kErLineCap = 40;
townrefill::Table g_erLoss;                          /* MAIN THREAD: the world server's rows */
std::map<std::string, std::string> g_erUnsent;       /* MAIN THREAD: towns refilled here whose REFILLED is not sent yet -> the world they were refilled in (townrefill::WorldStamp) */
struct ErSeen { DWORD first; DWORD nextLook; int holding; int nothing; ErSeen() : first(0), nextLook(0), holding(0), nothing(0) {} };   /* nothing = 1: a refill found nothing to fill in the town while this hold lasts */
std::map<std::string, ErSeen> g_erSeen;              /* MAIN THREAD, by town stringID */
std::set<std::string> g_erLossLogged;                /* MAIN THREAD: towns whose recorded-loss refusal was logged in this world */
/* THE TOWNS THIS GAME IS REFILLING, by town stringID (MAIN THREAD): the town's area, its name for the lines, and the position keys of
   its buildings still to handle (each with its calls so far and when it may be tried again) */
struct TfBld { std::string key; int tries; int waits; int setAside; DWORD nextTry; TfBld() : tries(0), waits(0), setAside(0), nextTry(0) {} };   /* tries = engine calls; waits = looks put off (townrefill::FillWaitStep); setAside = a call of this refill was set aside by the residents gate */
struct TfTown { std::string name; int sx, sy; std::vector<TfBld> left; size_t cursor; int filled; long long groups; int force; TfTown() : sx(0), sy(0), cursor(0), filled(0), groups(0), force(0) {} };   /* force = started by the TEST-ONLY lever */
std::map<std::string, TfTown> g_tfRun;
static void TfEndTown(const std::map<std::string, TfTown>::iterator& it, const std::string& why);   /* logs the end of a town's refill and drops it */
std::string g_tfCursor;                              /* MAIN THREAD: the town the tick handled last (round robin over the towns) */
DWORD g_tfLeverUntil = 0; float g_tfLeverPos[3] = { 0, 0, 0 };   /* MAIN THREAD: TEST-ONLY `townfill force` - until when, and where this game's player stood */
const int kTfStepsPerTick = 6;                       /* buildings looked at per frame, of one town */
const int kTfRunsPerTick = 1;                        /* engine calls per frame */
const DWORD kTfRetryMs = 10000;                      /* a building that waits, or whose call is to be repeated, is looked at again after this */
const int kTfBuildingCap = 512;                      /* buildings listed per town */
long long g_erDecide[townrefill::kRdN];
long long g_tfVerdict[townrefill::kFbN];
long long g_erLooks = 0, g_erSent = 0, g_erSendFailed = 0, g_erRowsIn = 0, g_erRowsBad = 0, g_erOwnerUnread = 0, g_erLines = 0, g_erLinesDropped = 0;
long long g_tfTowns = 0, g_tfBuildings = 0, g_tfSkippedOwned = 0, g_tfSkippedHome = 0, g_tfSkippedNotPop = 0, g_tfRetried = 0, g_tfDone = 0,
          g_tfGroups = 0, g_tfRanEmpty = 0, g_tfGivenUp = 0, g_tfGone = 0, g_tfStopped = 0, g_tfNothing = 0, g_tfListFull = 0, g_tfNoFactory = 0, g_tfFillLines = 0;
long long g_tfWaitGivenUp = 0, g_tfFilledViaReoffer = 0, g_tfStopSent = 0, g_tfBuildPending = 0, g_erUnsentOtherWorld = 0;
static void ErLine(const std::string& s) { if (g_erLines >= kErLineCap) { ++g_erLinesDropped; return; } ++g_erLines; DebugLog(s); }
/* ONE LOOK AT A TOWN, SHARED. TownRefillCheck fills it at its look (one per town per 30 s) and EmptyTownCheck reads it
   right after, so the two checks share one read of the town's gates, its residents list and its record tally; the refill's start
   (TownFillStart) reads both answers. looked = 0: no look at this check-up (off, off the main thread, an unreadable id, or inside
   the 30 s). */
struct ErFacts
{
    int looked; std::string key; int mine; int zoneLive; int flagsOk; int active; int dead; int sx, sy; DWORD heldMs;
    townrefill::RefillIn in; int listRead; int listTotal; int tallyRead; int decide;
    ErFacts() : looked(0), mine(0), zoneLive(0), flagsOk(0), active(0), dead(0), sx(0), sy(0), heldMs(0), listRead(0), listTotal(0), tallyRead(0), decide(-1) {}
};
static unsigned char g_erHands[kT2Nodes][kT2HandBytes];   /* MAIN THREAD only: the residents list's hands, read once per look for both checks */
std::map<std::string, std::string> g_etSaid;          /* MAIN THREAD, by town stringID: the answers of the town's last [EMPTYTOWN] line ("" or absent = armed); cleared per world */
long long g_etLines = 0;                              /* MAIN THREAD: [EMPTYTOWN] lines in this world (emptytown::kLineCap) */
long long g_etChecks = 0, g_etSilent = 0, g_etSettling = 0, g_etInFlight = 0, g_etNotEmpty = 0, g_etAccounted = 0, g_etSaidN = 0, g_etDropped = 0;
/* 1 = read: *player = the town's owner faction is this game's player faction, another player's, or a stand-in */
static int ErTownPlayerPod(void* town, int* player)
{
    *player = 0;
    __try { ::Faction* f = ((::RootObjectBase*)town)->getOwnerFactionDirect(); *player = (f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f) || IsStandInFaction(f))) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void ErSendUnsent()
{
    if (g_erUnsent.empty() || ::InterlockedCompareExchange(&g_linkedCached, 0, 0) == 0) return;
    for (std::map<std::string, std::string>::iterator it = g_erUnsent.begin(); it != g_erUnsent.end(); )
    {
        std::vector<char> b;
        if (!townrefill::EncodeRefilled(&b, it->first)) { g_erUnsent.erase(it++); continue; }
        if (!StoreSendTownLoss(b)) { ++g_erSendFailed; return; }   /* the link is down: every row waits for a later tick */
        ++g_erSent;
        g_erUnsent.erase(it++);
    }
}
static void TownRefillTeardown()
{
    if (!g_tfRun.empty()) ErLine("[TOWNFILL] world closed with " + N((long long)g_tfRun.size()) + " town refill(s) unfinished - dropped (nothing is kept between worlds)");
    g_tfRun.clear(); g_tfCursor.clear();
    ErSendUnsent();   /* a town refilled here whose REFILLED is not sent yet is KEPT (by town id, with its world's stamp) and sent once the link is up again - TownLossResetImpl keeps it over the next world server push when that push is for the same world */
    g_erSeen.clear(); g_erLossLogged.clear();
    g_etSaid.clear(); g_etLines = 0;   /* the next world's towns are named once again, under a fresh cap */
}
/* AT A WELCOME (store.cpp's drain, after the WELCOME's world is decided): the rows are emptied for the push. A town refilled here whose
   REFILLED is not sent yet stays refilled when it was refilled in the world this WELCOME is for (townrefill::SameWorld); one refilled
   in another world is dropped (town ids recur across worlds), counted unsentOtherWorld, the first drop logged. */
static void TownLossResetImpl()
{
    g_erLoss.clear();
    const std::string now = StoreWorldStamp();
    long long dropped = 0; std::string first;
    for (std::map<std::string, std::string>::iterator it = g_erUnsent.begin(); it != g_erUnsent.end(); )
    {
        if (townrefill::SameWorld(it->second, now) == 0) { if (first.empty()) first = it->first; ++dropped; g_erUnsent.erase(it++); continue; }
        townrefill::MarkRefilled(&g_erLoss, it->first);   /* not taken yet: still refilled here */
        ++it;
    }
    if (dropped != 0 && g_erUnsentOtherWorld == 0)
        ErLine("[REFILL] " + N(dropped) + " unsent REFILLED dropped at the WELCOME: refilled in another world (first " + first + ") - not sent to this one (first drop logged; emptyRefill unsentOtherWorld counts all)");
    g_erUnsentOtherWorld += dropped;
}
static void TownLossNoteRowsImpl(const std::vector<char>& payload)
{
    std::vector<townrefill::WireRow> in;
    if (townrefill::DecodeRows(payload.empty() ? 0 : &payload[0], payload.size(), &in) == 0) { ++g_erRowsBad; ErrorLog("[REFILL] malformed TOWN_LOSS from the world server - ignored"); return; }
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_erRowsIn;
        townrefill::Merge(&g_erLoss, in[i].sid, in[i].row);
        if (in[i].row.refilled != 0) g_erUnsent.erase(in[i].sid);   /* the world server has it */
        const townrefill::Table::const_iterator m = g_erLoss.find(in[i].sid);
        const std::map<std::string, TfTown>::iterator r = g_tfRun.find(in[i].sid);
        if (r != g_tfRun.end() && m != g_erLoss.end() && (m->second.losses != 0 || m->second.refilled != 0))
        {   /* the town's row shows a loss, or another game's refill: this game's refill of it ends here, nothing is sent */
            ++g_tfStopped;
            TfEndTown(r, m->second.losses != 0 ? "stopped: the world server's row now shows a loss (losses=" + N((long long)m->second.losses) + ")"
                                               : std::string("stopped: the world server's row now shows it refilled"));
        }
    }
}
static void TownRefillCheck(void* town, ErFacts* F)
{
    if (town == 0 || !g_on || RoleIsSingle()) return;
    if (::GetCurrentThreadId() != g_mainThread) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0 || !townrefill::SidOk(sid)) return;
    const std::string key(sid);
    const DWORD nowMs = ::GetTickCount();
    townrefill::RefillIn in;
    float tp[3] = { 0, 0, 0 };
    bool zoneLive = false;
    const int posOk = TownPosPod(town, tp);
    if (posOk != 0)
    {
        int ax[64], ay[64]; const int an = ActiveZoneSectors(ax, ay, 64); const Sector sc = SectorOf(tp[0], tp[2]);
        for (int i = 0; i < an && i < 64; ++i) if (ax[i] == sc.x && ay[i] == sc.y) { zoneLive = true; break; }
    }
    int active = 0, dead = 0;
    const int flagsOk = T2TownFlagsPod(town, &active, &dead);
    const int mine = zoneLive ? MineHeldTS(SectorOf(tp[0], tp[2])) : 0;   /* kept whole, so the detector can tell a frozen area from one not held */
    in.held = (zoneLive && flagsOk != 0 && active != 0 && mine == 1) ? 1 : 0;
    ErSeen& S = g_erSeen[key];
    if (in.held == 0) { S.holding = 0; S.nothing = 0; }   /* a later hold may find a building to fill */
    else if (S.holding == 0) { S.holding = 1; S.first = nowMs; }
    if (S.nextLook != 0 && (int)(nowMs - S.nextLook) < 0) return;   /* one look per town per 30 s */
    S.nextLook = nowMs + kErLookMs;
    ++g_erLooks;
    in.settled = (in.held != 0 && nowMs - S.first >= kRfSettleMs) ? 1 : 0;
    in.ready = (StoreWelcomePushDone() != 0 && StoreSnapshotApplied() != 0 && StoreRelayLinked()) ? 1 : 0;
    int player = 0;
    if (ErTownPlayerPod(town, &player) == 0) { ++g_erOwnerUnread; player = 1; }   /* the owner could not be read: treated as a player's - never refilled */
    in.playerTown = player;
    if (in.held != 0 && in.settled != 0 && in.ready != 0 && player == 0)
    {
        int total = 0; float budget = 0.0f;
        const int r = T2NodesPod(town, 1, g_erHands, kT2Nodes, &total, &budget);   /* 1 = the residents' list, read live - its hands kept for the empty-town detector */
        in.residents = (r == 1) ? total : -1;
        F->listRead = r; F->listTotal = total;
        if (in.residents == 0) { StoreTownRecordTally(key, &in.tally); in.goneHere = StoreTownGoneThisLaunch(key) + StoreTownGoneUnsent(key); F->tallyRead = 1; }   /* this game's own death-road deletes of the town, which the tally cannot see */
    }
    const townrefill::Table::const_iterator lr = g_erLoss.find(key);
    if (lr != g_erLoss.end()) { in.losses = lr->second.losses; in.refilled = lr->second.refilled; }
    F->looked = 1; F->key = key; F->mine = mine; F->zoneLive = zoneLive ? 1 : 0; F->flagsOk = flagsOk; F->active = active; F->dead = dead;   /* this look, for EmptyTownCheck */
    { const Sector fs = SectorOf(tp[0], tp[2]); F->sx = fs.x; F->sy = fs.y; }
    F->heldMs = (in.held != 0) ? nowMs - S.first : 0;
    F->in = in;
    const int d = townrefill::RefillDecide(in);
    ++g_erDecide[d];
    F->decide = d;
    if (d == townrefill::kRdRefuseLoss && g_erLossLogged.insert(key).second)
    {
        char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?");
        ErLine("[REFILL] empty town '" + RfStr(nm) + "' (" + key + ") is NOT refilled: a group of it was lost (wiped out) - a town with a recorded loss stays wiped"
               " records[living,deleted]=" + N((long long)in.tally.living) + "," + N((long long)in.tally.deleted) + " losses=" + N((long long)in.losses) + " refilled=" + N((long long)in.refilled)
               + " (first per town; emptyRefillDecide counts every refusal)");
    }
}
/* ------------------------------------------------------------------------------------------------------------------
   AN EMPTY TOWN THAT NOTHING ACCOUNTS FOR IS NAMED IN ONE LOG LINE - LOG ONLY, NOTHING IS REPAIRED.
   Right after the refill check's look at a town (main thread, the town's own check-up), when the gates pass - this game holds
   the town's area, the area is not frozen, the zone is live and the town in loaded range, held for the settle time, the opening
   push and the record snapshot in, the town no player's - the town's two lists, their groups and the character list are read.
   A town with no living resident and no living person of its own home town is checked for what could account for it: work in
   flight (an owed row, this game's own set-aside work, the bar list waiting, a note holding its creation, a group rebuild
   queued), its world records, its deaths, its people in a player's faction, a recorded loss, this game refilling it. When nothing
   does, ONE [EMPTYTOWN] line names the town, the mod's link time stamp and both protocol numbers, and every answer
   (emptytown::Line). emptytown::ShouldSay keeps it to once per town per world, again only when the answers change.
   ------------------------------------------------------------------------------------------------------------------ */
/* the mod's own link time stamp (its PE header's FileHeader.TimeDateStamp, through its module base); 0 = unreadable */
static unsigned EtModStampPod()
{
    __try
    {
        HMODULE m = 0;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&EtModStampPod, &m) || m == 0) return 0;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)m;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)m + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return (unsigned)nt->FileHeader.TimeDateStamp;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* One town list's groups, read through copies of their hands: living members, entries whose group no longer resolves, and dead
   members (a woken group's dead members; a sleeping group's shortfall from its original size). 1 = every group read, 0 = a fault
   or an implausible member list. MAIN THREAD */
static int EtGroups(unsigned char (*hs)[kT2HandBytes], int n, int* living, int* gone, int* dead)
{
    static T2Grp G;   /* MAIN THREAD only: kT2Mem-sized arrays */
    for (int i = 0; i < n; ++i)
    {
        T2GroupPod(hs[i], &G);
        if (G.ok == 2) { ++*gone; continue; }
        if (G.ok != 1) return 0;
        *living += G.f.living;
        if (G.f.awake != 0) *dead += G.nd;
        else if (G.orig > G.f.living) *dead += G.orig - G.f.living;
    }
    return 1;
}
/* ONE walk of the engine's character update list (RfWalk's walk) for the people whose group's home town is this town: living
   ones of no player and no stand-in (*homeLiving), and living ones of a player's or a stand-in's faction (*copies). The dead are
   left out - a corpse keeps its group, and its group keeps its home town. Returns the characters walked, -1 = no world or an
   implausible list. MAIN THREAD */
static long long EtHomeWalk(void* town, int* homeLiving, int* copies)
{
    *homeLiving = 0; *copies = 0;
    if (coop::GameWorldPtr() == 0) return -1;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return -1;
    long long walked = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        void* c = (void*)*it;
        if (c == 0) continue;
        ++walked;
        void* platoon = 0; void* gd = 0; int st = -1; void* home = 0;
        if (TownCharContextPod(c, &platoon, &gd, &st, &home) == 0 || home != town) continue;
        if (StoreIsDeadPod(c) != 0) continue;   /* dead, or its dead flag unreadable */
        const int fr = RfFreeFactionPod(c);
        if (fr == 1) ++*homeLiving; else if (fr == 0) ++*copies;
    }
    return walked;
}
/* the owed rows naming the town (either kind, the world server's table) and this game's own set-aside work for it (building
   residents set aside, its first bar roll set aside). Under g_tgLock, as those tables are kept. */
static void EtOwedCounts(const std::string& sid, int* rows, int* aside)
{
    *rows = 0; *aside = 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (owedpop::Table::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it) if (it->second.sid == sid) ++*rows;
    for (std::map<std::string, T392ResPend>::const_iterator it = g_t392Res.begin(); it != g_t392Res.end(); ++it) if (it->second.sid == sid) ++*aside;
    if (g_owedBars.find(sid) != g_owedBars.end()) ++*aside;
    ::LeaveCriticalSection(&g_tgLock);
}
/* the town's groups queued, or watched, for the engine's group rebuild (towns2 keeps them by the town's printed name). MAIN THREAD */
static int EtRebuildQueued(const std::string& tn)
{
    int n = 0;
    for (size_t q = 0; q < g_t2Queue.size(); ++q) if (g_t2Queue[q].town == tn) ++n;
    for (size_t v = 0; v < g_t2Verify.size(); ++v) if (g_t2Verify[v].town == tn) ++n;
    return n;
}
static void EtCount(int v)
{
    if (v == emptytown::kSettling) ++g_etSettling;
    else if (emptytown::IsSilent(v)) ++g_etSilent;
    else if (v == emptytown::kInFlight) ++g_etInFlight;
    else if (v == emptytown::kNotEmpty) ++g_etNotEmpty;
    else if (emptytown::IsAccounted(v)) ++g_etAccounted;
}
static int EmptyTownCheck(void* town, const ErFacts& F)
{
    if (town == 0 || F.looked == 0) return -1;   /* no look at this check-up: the refill check's own gates and its one look per 30 s */
    ++g_etChecks;
    emptytown::Facts f;
    f.held = (F.mine == 1) ? 1 : 0;
    f.frozen = (F.mine == coopdrop::kAreaFrozen) ? 1 : 0;
    f.live = (F.zoneLive != 0 && F.flagsOk != 0 && F.active != 0) ? 1 : 0;
    f.settled = F.in.settled; f.pushDone = F.in.ready; f.playerTown = F.in.playerTown;
    const int gate = emptytown::GateVerdict(f);
    if (gate != emptytown::kGatesPass) { EtCount(gate); return gate; }
    static unsigned char hp[kT2Nodes][kT2HandBytes];   /* MAIN THREAD only: the patrols list's hands */
    int ok = (F.listRead == 1 && F.listTotal <= kT2Nodes) ? 1 : 0;   /* the residents list was read at this look (the same gates) */
    if (ok != 0) { f.listed = F.listTotal; ok = EtGroups(g_erHands, F.listTotal, &f.listLiving, &f.gone, &f.deadMembers); }
    if (ok != 0)
    {
        int pt = 0; float pb = 0.0f;
        const int pr = T2NodesPod(town, 2, hp, kT2Nodes, &pt, &pb);   /* 2 = the patrols list */
        ok = (pr == 1 && pt <= kT2Nodes) ? 1 : 0;
        if (ok != 0) { f.listed += pt; ok = EtGroups(hp, pt, &f.patrolLiving, &f.gone, &f.deadMembers); }
    }
    if (ok != 0 && EtHomeWalk(town, &f.homeLiving, &f.copies) < 0) ok = 0;
    f.readOk = ok;
    if (ok == 0) { const int u = emptytown::Decide(f); EtCount(u); return u; }
    f.townDead = F.dead;
    townrefill::Tally t = F.in.tally;
    if (F.tallyRead == 0) StoreTownRecordTally(F.key, &t);   /* the refill check reads it only for a town with an empty residents list */
    f.recLiving = t.living; f.recPlacedHere = t.placedHere; f.recWaitingHere = t.waitingHere; f.recElsewhere = t.elsewhere;
    f.recUnplaceable = t.unplaceable; f.recCreateFailed = t.createFailed; f.recDeleted = t.deleted;
    f.goneThisLaunch = StoreTownGoneThisLaunch(F.key);
    EtOwedCounts(F.key, &f.owedRows, &f.aside);
    f.barPend = BarPendHas(town) ? 1 : 0;
    f.noteHeld = (StoreTownPeoplePending(F.key.c_str(), F.sx, F.sy, 0, 0) == townpending::kTownHeld) ? 1 : 0;
    char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?");
    f.rebuildQueued = EtRebuildQueued(RfStr(nm));
    f.loss = (F.in.losses > 0) ? 1 : 0;
    f.refilling = g_tfRun.count(F.key) != 0 ? 1 : 0;   /* this game is refilling it building by building */
    const int v = emptytown::Decide(f);
    EtCount(v);
    const std::map<std::string, std::string>::iterator si = g_etSaid.find(F.key);
    const std::string prev = (si != g_etSaid.end()) ? si->second : std::string();
    std::string next;
    const int say = emptytown::ShouldSay(prev, emptytown::Sig(f), v, g_etLines, &next);
    if (next.empty()) { if (si != g_etSaid.end()) g_etSaid.erase(si); }
    else g_etSaid[F.key] = next;
    if (say == emptytown::kSayDropped) { ++g_etDropped; return v; }
    if (say != emptytown::kSaySay) return v;
    static unsigned stamp = 0;
    if (stamp == 0) stamp = EtModStampPod();
    ++g_etLines; ++g_etSaidN;
    DebugLog(emptytown::Line(nm, F.key, F.sx, F.sy, (long long)(F.heldMs / 1000), stamp, net::SessionProtocolVersion(), townrefill::kProtocol, f));
    return v;
}
/* THE REFILL'S START, right after the town's two checks (MAIN THREAD): the refill decision said refill and the empty-town verdict says
   nothing accounts for the town (townrefill::FillStart). Every loaded building whose town (Building::asTown 0xF6BE0) is this town is
   listed by its position key; each is decided only when the tick reaches it. A list that could not hold every loaded building waits
   for a later look. */
static void TownFillStart(void* town, const ErFacts& F, int verdict)
{
    if (town == 0 || F.looked == 0) return;
    int force = 0;
    if (g_tfLeverUntil != 0)
    {   /* TEST-ONLY `townfill force`: the first town near where the player stood that this game holds, ready, no player's, not refilling */
        float tp[3] = { 0, 0, 0 };
        if ((int)(::GetTickCount() - g_tfLeverUntil) > 0) { g_tfLeverUntil = 0; ErLine("[TOWNFILL] townfill force: no town held here near the player within 120 s - nothing started"); }
        else if (F.in.held != 0 && F.in.ready != 0 && F.in.playerTown == 0 && g_tfRun.count(F.key) == 0 && TownPosPod(town, tp) != 0
                 && (tp[0] - g_tfLeverPos[0]) * (tp[0] - g_tfLeverPos[0]) + (tp[2] - g_tfLeverPos[2]) * (tp[2] - g_tfLeverPos[2]) < 4000.0f * 4000.0f) { force = 1; g_tfLeverUntil = 0; }
    }
    if (force == 0 && townrefill::FillStart(F.decide, verdict == emptytown::kUnexplained ? 1 : 0, g_tfRun.count(F.key) != 0 ? 1 : 0) == 0) return;
    ErSeen& S = g_erSeen[F.key];
    if (force == 0 && S.nothing != 0) return;   /* this hold already found nothing to fill */
    static void* list[4096];   /* MAIN THREAD only */
    int trunc = 0;
    const int n = LoadedBuildings(list, 4096, 0, &trunc);
    if (trunc != 0 || n >= 4096) { ++g_tfListFull; return; }
    TfTown T; T.sx = F.sx; T.sy = F.sy; T.force = force;
    { char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?"); T.name = RfStr(nm); }
    for (int i = 0; i < n; ++i)
    {
        if (TownOfBuilding(list[i]) != town) continue;
        char k[kT392KeyRoom];
        if (T392Key(list[i], k) == 0) continue;   /* no key: it cannot be found again */
        if ((int)T.left.size() >= kTfBuildingCap) { ++g_tfListFull; return; }
        TfBld b; b.key = k; T.left.push_back(b);
    }
    g_tfRun[F.key] = T; ++g_tfTowns;
    ErLine("[TOWNFILL] town " + F.key + " ('" + T.name + "') refill started on this game (it holds the area): " + (force != 0 ? std::string("TEST-ONLY townfill force (the empty-town verdict and the buildings' home answers are not asked)") : std::string("no residents, nothing accounts for it, no recorded loss, never refilled")) + " - "
           + N((long long)T.left.size()) + " building(s) of it loaded here, each decided when it is reached");
}
/* 1 = this game holds the area at sector (sx, sy) and its zone is live here */
static int TfAreaHeld(int sx, int sy)
{
    int ax[64], ay[64];
    const int an = ActiveZoneSectors(ax, ay, 64);
    for (int i = 0; i < an && i < 64; ++i)
        if (ax[i] == sx && ay[i] == sy) { Sector s; s.x = sx; s.y = sy; return MineHeldTS(s) == 1 ? 1 : 0; }
    return 0;
}
/* ONE BUILDING'S OWNER AND WHETHER KENSHI'S TOWN POPULATION FILLS IT. The test is ZoneMapContent::populateAllTheBuildings 0x9FCD00's
   (decomp_9fcd00: a building (vt+0x20 = 0) with its +0xC0 byte clear and a town of its own that is not the TownList's own town -
   decomp_9fcd00:64 `town != FUN_140926470(DAT_1421330a0)`, and decomp_926470: that function returns *(TownList + 0x180F8) - whose owner faction (vt+0x58) has no
   player interface (Faction+0x250), with an interior (+0x1F0) or a gate (vt+0x3D8), and vt+0x2C8 answering 0) plus what
   populateBuilding 0x57ED90 itself needs (decomp_57ed90: the building's GameData +0xF0). *owned = 1 when the owner is this game's
   player faction, another player's or a stand-in (a house a player bought: Building::buyMeCallback sets the buyer's faction, and the
   other games set the buyer's stand-in) or there is no owner. *pop = 1 / 0. Returns 1 read, 0 a fault or the TownList unread. MAIN THREAD */
unsigned long long kTfTownListRva = 0; static coop::AddrReg kTfTownListRva_reg("TownList", &kTfTownListRva);   /* the address table fills this. Steam_1.0.65 0x21330A0: a POINTER to the TownList */
static int TfBuildingPod(void* b, void* town, int* owned, int* pop)
{
    typedef int (*TfTypeFn)(void*);
    typedef void* (*TfPtrFn)(void*);
    *owned = 1; *pop = 0;
    __try
    {
        void** vt = *(void***)b;
        if (((TfTypeFn)vt[0x20 / 8])(b) != 0) { *owned = 0; return 1; }   /* not a building */
        void* f = ((TfPtrFn)vt[0x58 / 8])(b);
        *owned = (f == 0 || IsPlayerFaction((::Faction*)f) || IsPeerFaction((::Faction*)f) || IsStandInFaction((::Faction*)f)) ? 1 : 0;
        if (f == 0 || *(void* const*)((const char*)f + 0x250) != 0) return 1;
        if (*(const unsigned char*)((const char*)b + 0xC0) != 0) return 1;
        if (town == 0 || TownOfBuilding(b) != town) return 1;
        if (kTfTownListRva == 0) return 0;   /* the TownList unknown: the building waits */
        void* const tl = *(void* const*)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kTfTownListRva);
        if (tl == 0) return 0;
        if (*(void* const*)((const char*)tl + 0x180F8) == town) return 1;   /* the TownList's own town: populateAllTheBuildings skips its buildings */
        if (*(void* const*)((const char*)b + 0x1F0) == 0 && ((TfPtrFn)vt[0x3D8 / 8])(b) == 0) return 1;
        if (((TfPtrFn)vt[0x2C8 / 8])(b) != 0) return 1;
        if (*(void* const*)((const char*)b + 0xF0) == 0) return 1;
        *pop = 1;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *owned = 1; *pop = 0; return 0; }
}
static void TfEndTown(const std::map<std::string, TfTown>::iterator& it, const std::string& why)
{
    ErLine("[TOWNFILL] town " + it->first + " ('" + it->second.name + "') refill " + why + " - " + N((long long)it->second.filled) + " building(s) filled here, "
           + N(it->second.groups) + " group(s) made");
    g_tfRun.erase(it);
}
/* the town is refilled here: marked in this game's copy of its row, and REFILLED waits to go (ErSendUnsent) with this world's stamp */
static void TfMarkRefilled(const std::string& sid)
{
    townrefill::MarkRefilled(&g_erLoss, sid);
    g_erUnsent[sid] = StoreWorldStamp();
}
/* the town's refill stops on verdict v (townrefill::FillStops). A stop because this game no longer holds the area, after a building
   was filled, marks the town refilled and sends REFILLED (townrefill::FillStopDone): those buildings' people exist. */
static void TfStopTown(const std::map<std::string, TfTown>::iterator& it, int v)
{
    ++g_tfStopped;
    if (townrefill::FillStopDone(v, it->second.filled) == townrefill::kTdSend)
    {
        ++g_tfStopSent;
        TfMarkRefilled(it->first);
        TfEndTown(it, std::string("stopped: ") + townrefill::FillName(v) + " after filling - REFILLED goes to the world server, so no game refills it again");
        return;
    }
    TfEndTown(it, std::string("stopped: ") + townrefill::FillName(v));
}
/* one more put-off for building B (townrefill::FillWaitStep): 1 = it is looked at again after the retry gap, 0 = its put-offs are used
   up and the caller drops it, handled without being filled */
static int TfPutOff(const std::string& sid, TfBld& B, DWORD now, const char* why)
{
    ++B.waits;
    if (townrefill::FillWaitStep(B.waits) == townrefill::kFwAgain) { B.nextTry = now + kTfRetryMs; return 1; }
    ++g_tfWaitGivenUp;
    ErLine("[TOWNFILL] town " + sid + " building " + B.key + " not filled: put off " + N((long long)B.waits) + " times (last: " + why + ") - handled without being filled"
           + (B.setAside != 0 ? std::string(" (a call of this refill was set aside: the re-offer still has the building)") : std::string()));
    return 0;
}
/* THE REFILL'S WORK, MAIN THREAD, every frame (TownGenTick): one town per frame (round robin), up to kTfStepsPerTick of its buildings
   looked at and kTfRunsPerTick engine calls. Each building is decided NOW (townrefill::FillBuilding) from live reads: the town's
   area held here, the world server's row, this game's own deletes of the town's groups, the building found by its key, its owner and
   whether Kenshi's town population fills it, decision 34's answer at the building's own area, whether a loaded group (the building's
   residents hand, the town's residents list, a record-made group owed the home or with no key) or a living world record names it as
   home. A building to fill gets its residents through T392RerunBuilding - Kenshi's populateBuilding, which gives the groups it makes
   the building as their home; each record this game writes for them carries that home's key (TownGenPlatoonHomeKey). */
static void TownFillTick()
{
    if (g_tfRun.empty() || !g_on || ::GetCurrentThreadId() != g_mainThread || EngineWritesBlocked()) return;
    std::map<std::string, TfTown>::iterator it = g_tfRun.upper_bound(g_tfCursor);
    if (it == g_tfRun.end()) it = g_tfRun.begin();
    g_tfCursor = it->first;
    const std::string sid = it->first;
    TfTown& T = it->second;
    townrefill::FillIn base;
    base.held = TfAreaHeld(T.sx, T.sy);
    { const townrefill::Table::const_iterator lr = g_erLoss.find(sid); if (lr != g_erLoss.end()) { base.losses = lr->second.losses; base.refilled = lr->second.refilled; } }
    base.goneHere = StoreTownGoneThisLaunch(sid) + StoreTownGoneUnsent(sid);
    {
        const int v0 = townrefill::FillBuilding(base);   /* the town's own facts first: a stop needs no building */
        if (townrefill::FillStops(v0)) { ++g_tfVerdict[v0]; TfStopTown(it, v0); return; }
    }
    void* factory = g_t392Factory;
    if (factory == 0) factory = OwedWorldFactoryPod();
    const DWORD now = ::GetTickCount();
    int runs = 0;
    for (int step = 0; step < kTfStepsPerTick && !T.left.empty() && runs < kTfRunsPerTick; ++step)
    {
        if (T.cursor >= T.left.size()) T.cursor = 0;
        TfBld& B = T.left[T.cursor];
        if (B.nextTry != 0 && (int)(now - B.nextTry) < 0) { ++T.cursor; continue; }
        void* const b = ObjectByPositionKey(B.key.c_str());
        if (b == 0)
        {
            if (ObjectByPositionKeyVerdict() == 1) { ++g_tfGone; T.left.erase(T.left.begin() + T.cursor); continue; }   /* a complete search found no such building here */
            ++g_tfVerdict[townrefill::kFbWait];
            if (TfPutOff(sid, B, now, "key-unresolved") != 0) ++T.cursor; else T.left.erase(T.left.begin() + T.cursor);
            continue;
        }
        void* const town = TownOfBuilding(b);
        townrefill::FillIn in = base;
        int active = 0, dead = 0;
        char tsid[128];
        if (town == 0 || TownSidPod(town, tsid, 128) == 0 || sid != tsid) { ++g_tfGone; T.left.erase(T.left.begin() + T.cursor); continue; }   /* the key now names no town's building, or another town's */
        if (T2TownFlagsPod(town, &active, &dead) == 0 || active == 0) in.held = 0;   /* the town is out of loaded range here */
        if (TfBuildingPod(b, town, &in.owner, &in.populatable) == 0) in.populatable = -1;
        float bp[3] = { 0, 0, 0 };
        Sector bs; bs.x = T.sx; bs.y = T.sy;
        if (in.owner == 0 && in.populatable == 1)
        {
            if (TownPosPod(b, bp) != 0) bs = SectorOf(bp[0], bp[2]);
            in.mayHere = T392MayAt(bs, 0) != 0 ? 1 : 0;
            in.buildPending = BuildRowPendingFor(b);   /* another game's purchase or build row for it, not applied here yet */
            if (T.force != 0) { in.home = 0; in.recordsHome = 0; }   /* TEST-ONLY `townfill force`: the home answers are not asked */
            else { in.home = T392BuildingHasResidents(town, b); in.recordsHome = StoreRecordsHomedAt(B.key); }
        }
        const int v = townrefill::FillBuilding(in);
        ++g_tfVerdict[v];
        if (townrefill::FillStops(v)) { TfStopTown(it, v); return; }
        if (townrefill::FillSkips(v))
        {
            if (townrefill::FillSkipFilled(v, B.setAside) != 0) { ++T.filled; ++g_tfBuildings; ++g_tfFilledViaReoffer; }   /* this refill's set-aside call was made by the re-offer */
            else if (v == townrefill::kFbSkipOwned) { ++g_tfSkippedOwned; if (in.buildPending > 0) ++g_tfBuildPending; }
            else if (v == townrefill::kFbSkipHome) ++g_tfSkippedHome; else ++g_tfSkippedNotPop;
            T.left.erase(T.left.begin() + T.cursor);
            continue;
        }
        if (v == townrefill::kFbWait)
        {
            const char* why = in.populatable < 0 ? "building-unread" : (in.home < 0 ? "residents-unread-or-keyless-group" : (in.buildPending < 0 ? "build-key-unread" : "area-not-this-game's"));
            if (TfPutOff(sid, B, now, why) != 0) ++T.cursor; else T.left.erase(T.left.begin() + T.cursor);
            continue;
        }
        if (factory == 0) { ++g_tfNoFactory; if (TfPutOff(sid, B, now, "no-factory") != 0) ++T.cursor; else T.left.erase(T.left.begin() + T.cursor); continue; }   /* no RootObjectFactory known yet */
        ++runs; ++B.tries;
        std::string st, pa;
        const int r = T392RerunBuilding(town, factory, b, bs, 1, &st, &pa);   /* 1: the building's state step ran when its zone first loaded - not repeated */
        const long long made = g_t580RunMade;
        const long long refused = (g_t581RunAsked - g_t581RunReached) + g_t580RunPending;
        const int after = townrefill::FillAfterRun(r, made, refused, B.tries);
        if (r == 0) B.setAside = 1;   /* the residents gate set this call aside: the re-offer makes the building's residents later */
        if (after == townrefill::kFrRetry)
        {
            ++g_tfRetried;
            if (TfPutOff(sid, B, now, r == 0 ? "set-aside" : (r < 0 ? "fault" : "refused")) != 0) ++T.cursor; else T.left.erase(T.left.begin() + T.cursor);
            continue;
        }
        if (after == townrefill::kFrGiveUp)
        {
            ++g_tfGivenUp;
            ErLine("[TOWNFILL] town " + sid + " building " + B.key + " given up: the engine call faulted " + N((long long)B.tries) + " time(s)");
            T.left.erase(T.left.begin() + T.cursor);
            continue;
        }
        if (made > 0) { ++T.filled; ++g_tfBuildings; } else ++g_tfRanEmpty;
        T.groups += made; g_tfGroups += made;
        if (made > 0 && ++g_tfFillLines <= 40)
        {
            const int homeNow = T392BuildingHasResidents(town, b);   /* the made groups name the building as home: its residents hand, or the town's list */
            DebugLog("[TOWNFILL] town " + sid + " building " + B.key + " refilled (" + N(made) + " groups) home=" + (homeNow > 0 ? "named" : (homeNow == 0 ? "not-named" : "unread"))
                     + " refused=" + N(refused) + (pa.empty() ? std::string(" popAdd=none popUndone=none") : pa) + st + " (first 40; townFill[...] counts all)");
        }
        T.left.erase(T.left.begin() + T.cursor);
    }
    if (!T.left.empty()) return;
    const int done = townrefill::TownFillDone(T.filled);
    if (done == townrefill::kTdSend)
    {
        TfMarkRefilled(sid);
        ++g_tfDone;
        TfEndTown(it, "done: every building handled - REFILLED goes to the world server, so no game refills it again");
        return;
    }
    ++g_tfNothing;
    g_erSeen[sid].nothing = 1;
    TfEndTown(it, "found nothing to fill (every building a player's, homed, not one Kenshi fills, put off too long, or made no one) - nothing is sent");
}
static void TownRefillTick() { TownFillTick(); ErSendUnsent(); }
static std::string EmptyTownReport()
{
    return " emptyTown[checks,silent,settling,inFlight,notEmpty,accounted,said,dropped]=" + N(g_etChecks) + "," + N(g_etSilent) + "," + N(g_etSettling)
           + "," + N(g_etInFlight) + "," + N(g_etNotEmpty) + "," + N(g_etAccounted) + "," + N(g_etSaidN) + "," + N(g_etDropped);
}
static std::string TownRefillReport()
{
    long long lossTowns = 0, refilledTowns = 0, leftNow = 0;
    for (townrefill::Table::const_iterator it = g_erLoss.begin(); it != g_erLoss.end(); ++it) { if (it->second.losses != 0) ++lossTowns; if (it->second.refilled != 0) ++refilledTowns; }
    for (std::map<std::string, TfTown>::const_iterator it = g_tfRun.begin(); it != g_tfRun.end(); ++it) leftNow += (long long)it->second.left.size();
    std::string names, counts, fnames, fcounts;
    for (int i = 0; i < townrefill::kRdN; ++i) { if (i != 0) { names += ","; counts += ","; } names += townrefill::DecideName(i); counts += N(g_erDecide[i]); }
    for (int i = 0; i < townrefill::kFbN; ++i) { if (i != 0) { fnames += ","; fcounts += ","; } fnames += townrefill::FillName(i); fcounts += N(g_tfVerdict[i]); }
    return " emptyRefill[looks,sent,sendFailed,unsent,rowsIn,rowsBad,lossTowns,refilledTowns,ownerUnread]="
           + N(g_erLooks) + "," + N(g_erSent) + "," + N(g_erSendFailed) + "," + N((long long)g_erUnsent.size()) + "," + N(g_erRowsIn) + "," + N(g_erRowsBad)
           + "," + N(lossTowns) + "," + N(refilledTowns) + "," + N(g_erOwnerUnread)
           + " emptyRefillDecide[" + names + "]=" + counts
           + " townFill[towns,buildings,skippedOwned,skippedHome,retried,done]=" + N(g_tfTowns) + "," + N(g_tfBuildings) + "," + N(g_tfSkippedOwned) + "," + N(g_tfSkippedHome)
           + "," + N(g_tfRetried) + "," + N(g_tfDone)
           + " townFillMore[running,buildingsLeft,groups,skippedNotPopulated,ranEmpty,givenUp,gone,stopped,nothing,listFull,noFactory,linesDropped]="
           + N((long long)g_tfRun.size()) + "," + N(leftNow) + "," + N(g_tfGroups) + "," + N(g_tfSkippedNotPop) + "," + N(g_tfRanEmpty) + "," + N(g_tfGivenUp) + "," + N(g_tfGone)
           + "," + N(g_tfStopped) + "," + N(g_tfNothing) + "," + N(g_tfListFull) + "," + N(g_tfNoFactory) + "," + N(g_erLinesDropped)
           + " townFillVerdict[" + fnames + "]=" + fcounts
           + " townFillCap[waitGivenUp,filledViaReoffer,stopSent,buildRowPending,unsentOtherWorld]=" + N(g_tfWaitGivenUp) + "," + N(g_tfFilledViaReoffer)
           + "," + N(g_tfStopSent) + "," + N(g_tfBuildPending) + "," + N(g_erUnsentOtherWorld) + StoreGoneTownReport() + EmptyTownReport();
}
/* TEST-ONLY lever `townfill force | show` (command_channel.cpp), MAIN THREAD. force: within 120 s, the first town whose check-up looks
   at it within 4000 units of where this game's player character stands now - this game holding its area, the opening push in, the town
   no player's - starts its refill WITHOUT the empty-town verdict, and its buildings' home answers are not asked (a building that has
   residents gets the residents Kenshi first gave it once more). Every other answer is asked as always: the holder, the world server's
   row, this game's own deletes, a player's house, a building Kenshi's town population does not fill. show: the refills running here
   and the counts (read only). */
static std::string TownFillLeverImpl(const std::string& arg)
{
    if (arg == "show")
    {
        std::string o = "ok townfill running=" + N((long long)g_tfRun.size());
        for (std::map<std::string, TfTown>::const_iterator it = g_tfRun.begin(); it != g_tfRun.end(); ++it)
            o += " [" + it->first + " '" + it->second.name + "' left=" + N((long long)it->second.left.size()) + " filled=" + N((long long)it->second.filled) + " groups=" + N(it->second.groups) + " force=" + N((long long)it->second.force) + "]";
        o += TownRefillReport();
        DebugLog("[TOWNFILL] " + o);
        return o;
    }
    if (arg != "force") return "error townfill: usage `townfill force` (TEST-ONLY) | `townfill show`";
    if (::GetCurrentThreadId() != g_mainThread) return "error townfill: not on the main thread";
    void* const pc = (void*)GetTarget();
    float p[3] = { 0, 0, 0 };
    if (pc == 0 || TownPosPod(pc, p) == 0) return "error townfill force: no player character position";
    g_tfLeverPos[0] = p[0]; g_tfLeverPos[1] = p[1]; g_tfLeverPos[2] = p[2];
    g_tfLeverUntil = ::GetTickCount() + 120000;
    const std::string o = "ok townfill force: the next town looked at within 4000 units of (" + N((long long)p[0]) + ", " + N((long long)p[2]) + ") that this game holds starts its refill (TEST-ONLY; within 120 s)";
    DebugLog("[TOWNFILL] " + o);
    return o;
}
void detour_townPeriodic(void* town)
{
    orig_townPeriodic(town);   /* the engine's own check-up first, unchanged */
    T392Reoffer(town);   /* T-392: the set-aside bar list and building residents get their second chance here */
    RefillCheck(town);
    Towns2Check(town);   /* towns2: the same post-hook - the game's marker rule picks the town's groups for the engine's own rebuild */
    ErFacts ef;   /* one look at the town, shared by the refill check and the empty-town detector */
    TownRefillCheck(town, &ef);   /* the refill decision for an empty town */
    const int verdict = EmptyTownCheck(town, ef);   /* an empty town nothing accounts for is named in one log line */
    TownFillStart(town, ef, verdict);   /* both say so: this game, holding the area, starts refilling it building by building */
}
static void RefillInstall(uintptr_t base)
{
    if (kTownPeriodicUpdateRva == 0) { ErrorLog("[REFILL] off: the address table has no TownPeriodicUpdate row - bars do not refill"); return; }
    if (coop::AddHook((void*)(base + kTownPeriodicUpdateRva), (void*)&detour_townPeriodic, (void**)&orig_townPeriodic) != coop::SUCCESS)
    { ErrorLog("[REFILL] AddHook Town::periodicTick 0x92BE50 failed - bars do not refill"); return; }
    g_rfHooked = true;
    DebugLog("[REFILL] hook installed: Town::periodicTick 0x92BE50 (post: on the game that holds a town, its bar is topped up to its usual size every 5 in-game days)");
}
/* MAIN THREAD (TownGenTick): rows the notebook could not take are sent again once it is linked */
static void RefillTick()
{
    if (g_rfUnsent.empty()) return;
    static DWORD last = 0; const DWORD now = ::GetTickCount();
    if (now - last < 5000) return;
    last = now;
    if (!StoreLinkIsUp()) return;
    std::vector<coopbar::BarWireRow> rows;
    for (std::map<std::string, coopbar::BarRow>::const_iterator it = g_rfUnsent.begin(); it != g_rfUnsent.end() && rows.size() < coopbar::kBarMaxRows; ++it)
    { coopbar::BarWireRow w; w.sid = it->first; w.row = it->second; rows.push_back(w); }
    std::vector<char> b;
    if (!coopbar::EncodeBarRows(&b, rows) || !StoreSendTownBar(b)) return;
    for (size_t i = 0; i < rows.size(); ++i) g_rfUnsent.erase(rows[i].sid);
    g_rfResent += (long long)rows.size();
}
static void RefillReport()
{
    DebugLog("[REFILL] REPORT checks=" + N(g_rfChecks) + " due=" + N(g_rfDue) + " waitedNearby=" + N(g_rfWaitedNearby) + " noBar=" + N(g_rfNoBar)
             + " toppedUp=" + N(g_rfToppedUp) + " addedPeople=" + N(g_rfAddedPeople) + " faults=" + N(g_rfFaults) + " noListLen=" + N(g_rfNoListLen) + " usualRecorded=" + N(g_rfUsualRecorded)
             + " usualFromLive=" + N(g_rfUsualFromLive) + " usualWaitList=" + N(g_rfUsualWaitList) + " recordSendFailed=" + N(g_rfRecordSendFailed)
             + " squadsAsked=" + N(g_rfSquadsAsked) + " full=" + N(g_rfFull) + " owed=" + N(g_rfOwed) + " owedDeferred=" + N(g_rfOwedDeferred) + " armedNoPend=" + N(g_rfArmedNoPend) + " settling=" + N(g_rfSettling) + " liveEmpty=" + N(g_rfLiveEmpty)
             + " noList=" + N(g_rfNoList) + " noClock=" + N(g_rfNoClock) + " offThread=" + N(g_rfOffThread) + " rows=" + N((long long)g_rfTable.size())
             + " rowsIn=" + N(g_rfRowsIn) + " rowsBad=" + N(g_rfRowsBad) + " unsent=" + N((long long)g_rfUnsent.size()) + " resent=" + N(g_rfResent)
             + " forcedPending=" + N((long long)g_rfForced.size()) + " hook=" + N(g_rfHooked ? 1 : 0) + " linesDropped=" + N(g_rfLinesDropped));
    /* refill3 (T382: forcedPending=1 for 400 s and nothing said why): every pending `refill now` target, with the last reason */
    int shown = 0;
    for (std::set<std::string>::const_iterator f = g_rfForced.begin(); f != g_rfForced.end() && shown < 8; ++f, ++shown)
    {
        std::map<std::string, RfForcedNote>::const_iterator w = g_rfForcedWhy.begin();
        for (; w != g_rfForcedWhy.end(); ++w) if (RfLower(w->first) == *f || RfLower(w->second.nm) == *f) break;
        if (w == g_rfForcedWhy.end())
            DebugLog("[REFILL] refill now: '" + RfStr(f->c_str()) + "' still pending on this game - no check-up of a town by that stringID or name has come to the refill here");
        else if (w->second.why.empty())
            DebugLog("[REFILL] refill now: '" + RfStr(f->c_str()) + "' still pending on this game - town '" + RfStr(w->second.nm.c_str()) + "' was matched as due; its top-up has not finished");
        else
            DebugLog("[REFILL] refill now: '" + RfStr(f->c_str()) + "' still pending on this game - town '" + RfStr(w->second.nm.c_str()) + "' last skipped "
                     + N((long long)((::GetTickCount() - w->second.whyAt) / 1000)) + " s ago: " + w->second.why);
    }
}
static void RefillTeardown()
{
    g_rfSeen.clear();   /* a hold belongs to the world that loaded the town */
    g_rfForcedWhy.clear();   /* refill3: the reasons belong to that world too (the targets stay, as before) */
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_rfFresh.clear(); g_rfListLen.clear(); ::LeaveCriticalSection(&g_tgLock);
}

} // namespace

void InstallTownGen()
{
    g_mainThread = ::GetCurrentThreadId();   // provisional (preload); replaced on the first tick
    TgLockInit(); ZonesInitLocks();   // review-p4b HIGH-1: the two critical sections initialised here, once, before any worker can reach them
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    RefillInstall(base);   /* refill1: the post-hook on Town::periodicTick 0x92BE50 */
    Towns2Install(base);   /* towns2: the pre-hook on Faction::updateActivePlatoons 0x6BA810 (the act); the decision rides the refill's post-hook */
    coop::HookStatus h = coop::AddHook((void*)(base + kCreateRandomUnloadedSquadRva), (void*)&detour_create, (void**)&orig_create);
    if (h != coop::SUCCESS) { ErrorLog("[TOWN] AddHook createRandomUnloadedSquad failed - town generation is NOT gated"); return; }
    /* T-515: the building-state step (set-aside squads take it at load; the re-offer does not repeat it) */
    if (kBuildingSquadStateRva == 0) ErrorLog("[TOWN] BuildingSquadState not in the address table - a set-aside building's state waits for the re-offer");
    else if (coop::AddHook((void*)(base + kBuildingSquadStateRva), (void*)&detour_bldState, (void**)&orig_bldState) != coop::SUCCESS)
    { orig_bldState = 0; ErrorLog("[TOWN] AddHook building-state step 0x57E760 failed - a set-aside building's state waits for the re-offer"); }
    else DebugLog("[TOWN] hook installed: building-state step 0x57E760 (T-515: taken when squads are set aside, not repeated by the re-offer)");
    coop::HookStatus h2 = coop::AddHook((void*)(base + kSpawnTheBarFliesRva), (void*)&detour_barFlies, (void**)&orig_barFlies);
    if (h2 != coop::SUCCESS) ErrorLog("[TOWN] AddHook Town::spawnTheBarFlies failed - bar residents are NOT gated");
    else DebugLog("[TOWN] hook installed: Town::spawnTheBarFlies 0x9FBE50 (bar residents, the same decision)");
    DebugLog("[TOWN] hook installed: createRandomUnloadedSquad 0x57EA30 (DECISION 48: the game that HOLDS the area"
             " generates its town's people; there is no client declaration and no host exemption any more)");
}
void SetTownGenOn(bool on) { g_on = on; DebugLog(std::string("[TOWN] towngen ") + (on ? "on" : "off")); }
/* P8a: the world's groups are gone with the world, so the id set goes with the refusal keys beside it - a recycled
   pointer or a re-used id from a previous world must never answer `towngen` for a character in this one. */
void TownGenWorldTeardown() { TownRefillTeardown(); ZonesSlotsSeenReset("world teardown");   /* review-recruit3 5b: auto counts THIS world's players only */
                             TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_refusedTowns.clear(); g_refusedByTown.clear(); g_pendingLogAt.clear(); g_elsewhereByTown.clear(); g_t580KeptByKey.clear(); g_allowedTowns.clear(); g_tgCreatedIds.clear(); ::LeaveCriticalSection(&g_tgLock); { TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_barPend.clear(); ::LeaveCriticalSection(&g_tgLock); } /* T-392 */ T392Teardown(); /* PROBE P085 */ for (int p085i = 0; p085i < 4096; ++p085i) { g_p085Squads[p085i] = 0; g_p085Residents[p085i] = 0; } /* refill1 */ RefillTeardown(); /* towns2 */ Towns2Teardown(); }   /* F666: a Town pointer belongs to the world that made it */
int TownGenMadeThisPlatoon(const char* platoonId)
{
    if (platoonId == 0 || platoonId[0] == 0) return 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    const int hit = (g_tgCreatedIds.find(std::string(platoonId)) != g_tgCreatedIds.end()) ? 1 : 0;
    ::LeaveCriticalSection(&g_tgLock);
    return hit;
}
void TownGenTick() { ::InterlockedExchange(&g_linkedCached, ((StoreWelcomedThisLink() != 0) && StoreRelayLinked() && StoreWelcomePushDone() != 0) ? 1 : 0);   /* T-581: and the opening push is in - the owed rows are known before anything is made */   /* F665: THE NOTEBOOK LINK */
                     if (!g_mainThreadFromTick) { g_mainThread = ::GetCurrentThreadId(); g_mainThreadFromTick = true; DebugLog("[TOWN] main thread = " + N((long long)g_mainThread) + " (captured on the first tick)"); } RecruitDrainLines(); /* refill1 */ RefillTick(); /* towns2 */ Towns2Tick(); OwedTick(); /* the empty-town refill: a few buildings per frame, REFILLED sent */ TownRefillTick(); }
static long long RefusedTownsCount() { TgLockInit(); ::EnterCriticalSection(&g_tgLock); const long long n = (long long)g_refusedTowns.size(); ::LeaveCriticalSection(&g_tgLock); return n; }
static long long TownGenCreatedIdCount() { TgLockInit(); ::EnterCriticalSection(&g_tgLock); const long long n = (long long)g_tgCreatedIds.size(); ::LeaveCriticalSection(&g_tgLock); return n; }
static std::string PerTownCounts()
{
    std::string s;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (std::map<std::string, long long>::const_iterator it = g_refusedByTown.begin(); it != g_refusedByTown.end(); ++it) s += " refused['" + it->first + "']=" + N(it->second);
    if (g_pendingElsewhere != 0) s += " pendingElsewhere=" + N(g_pendingElsewhere);   /* T-580: town checks (squad creations and bar-resident rolls) decision 34 let through because every listed note of the town has a known position in an area that does not hold it */
    if (g_t580RerunKept != 0) s += " rerunKeptPending=" + N(g_t580RerunKept) + " rerunGivenUpPending=" + N(g_t580RerunGivenUp);   /* T-580: residents re-runs that made nothing because decision 34 refused (kept, or given up at the bound) */
    for (std::map<std::string, long long>::const_iterator it = g_elsewhereByTown.begin(); it != g_elsewhereByTown.end(); ++it) s += " pendingElsewhere['" + it->first + "']=" + N(it->second);
    ::LeaveCriticalSection(&g_tgLock);
    return s;
}
void ReportTownGen()
{
    RecruitReport();   /* recruit3: [RECRUIT] REPORT */
    RefillReport();   /* refill1: [REFILL] REPORT */
    Towns2Report();   /* towns2: [TOWNS2] REPORT */
    DebugLog("[TG] towngen=" + std::string(g_on ? "on" : "off")
             + " towngenHolderGenerated=" + N(g_holderGenerated)
             + " towngenRefusedNotHolder=" + N(g_refusedNotHolder)
             + " towngenRefusedNoMap=" + N(g_refusedNoMap)
             + " townGenIds=" + N((long long)TownGenCreatedIdCount()) + " townGenIdOverflow=" + N(g_tgIdOverflow)
             + " seen=" + N(g_seen) + " refused=" + N(g_refused) + " refusedUnlinked=" + N(g_refusedUnlinked)
             + " allowed=" + N(g_allowed) + " unlinkedAllowed=" + N(g_unlinkedAllowed) + " noTown=" + N(g_noTown) + " noLease=" + N(g_noLease) + " readFault=" + N(g_readFault)
             + " leverOff=" + N(g_leverOff) + " offThread=" + N(g_offThread) + " refusalKeys(town x cause)=" + N((long long)RefusedTownsCount()) + " created[seen,recorded]=" + N(g_createdSeen) + "," + N(g_createdRecorded) + " callers=" + N((long long)g_callerCount) + " callerOverflowCalls=" + N(g_callerOverflow)
             + " refusedNoLease=" + N(g_refusedNoLease) + " off[refused,allowed,noLease,readFault,refusedUnlinked,refusedNoLease]=" + N(g_refusedOff) + "," + N(g_allowedOff) + "," + N(g_noLeaseOff) + "," + N(g_readFaultOff) + "," + N(g_refusedUnlinkedOff) + "," + N(g_refusedNoLeaseOff)
             + " teardown[refused,barFlies]=" + N(g_refusedTeardown) + "," + N(g_barFliesTeardown) + "   (P6j: SPANS inside refused and barFlies refused, not extra buckets - how many refusals were the engine freeing the world rather than an answer about the area)"
             + " barDeferred[towns,repeat,answered,pendOverflow]=" + N(g_barDeferredTowns) + "," + N(g_barDeferredRepeat) + "," + N(g_barDeferredAnswered) + "," + N(g_barPendOverflow)
             + " t392[deferredBar,deferredResidents,reofferGenerated,reofferOther,reofferFilled,reofferGone,reofferFault,waitingNowBar,waitingNowResidents,resSeen,resNoTown,resNoKey,reofferHasResidents,sectorFromSpawn,sectorFromTown,sectorFromBuilding,squadsSetAside,asideRuns,asidePopAdd,rerunPopAdd,rerunPopUndone,rerunPopUndoneSum,rerunPopMismatch,rerunPopUnread]="
               + N(g_t392DeferBar) + "," + N(g_t392DeferRes) + "," + N(g_t392ReGen) + "," + N(g_t392ReOther) + "," + N(g_t392ReFilled) + "," + N(g_t392ReGone) + "," + N(g_t392ReFault)
               + "," + N(T392BarWaitingNow()) + "," + N((long long)T392ResCount()) + "," + N(g_t392ResSeen) + "," + N(g_t392ResNoTown) + "," + N(g_t392ResNoKey) + "," + N(g_t392ReHasRes)
               + "," + N(g_t392SecSpawn) + "," + N(g_t392SecTown) + "," + N(g_t392SecBuilding)
               + "," + N(g_t392SquadsAside) + "," + N(g_t392AsideRuns) + "," + N(g_t392AsidePopAdd) + "," + N(g_t392RerunPopAdd)
               + "," + N(g_t392RerunPopUndone) + "," + N(g_t392RerunPopUndoneSum) + "," + N(g_t392RerunPopMismatch) + "," + N(g_t392RerunPopUnread)
             + " t515[stateApplied,stateFault,stateNoTemplate,stateNoHook,rerunStateSkipped,rerunStateRan,hooked,stateAppliedRefused]=" + N(g_t515Applied) + "," + N(g_t515Fault)
               + "," + N(g_t515NoTemplate) + "," + N(g_t515NoHook) + "," + N(g_t515RerunSkipped) + "," + N(g_t515RerunRan) + "," + N(orig_bldState != 0 ? 1 : 0) + "," + N(g_t515RefusedApplied)
             + OwedReport() + TownRefillReport()
             + " pending[refused,barFliesDeferred,residentsSetAside,residentsSetAsideNoKey,residentsSetAsideWaitingNow,reofferWaitsOnNote,homesGivenAtCheck]=" + N(g_refusedPending) + "," + N(g_barFliesDeferredPending) + "," + N(g_t591PendAside) + "," + N(g_t591PendAsideNoKey) + "," + N(T591WaitingNow()) + "," + N(g_t591WaitOnNote) + "," + N(g_t591HomeGivenAtCheck) + StoreHomeReport() + " residentsKeylessWaits=" + N(g_t591KeylessWaits) + " cached[linked]=" + N((long long)g_linkedCached)
             + " mainThread=" + N((long long)g_mainThread) + (g_mainThreadFromTick ? "(tick)" : "(preload)") + " barFlies[seen=leverOff+noTown+readFault+allowed+refused+refusedUnlinked; off; drainFault; drainOddCount; budgetEntriesDropped(each 0..N squads)]=" + N(g_barFliesSeen) + "=" + N(g_barFliesLeverOff) + "+" + N(g_barFliesNoTown) + "+" + N(g_barFliesReadFault) + "+" + N(g_barFliesAllowed) + "+" + N(g_barFliesRefused) + "+" + N(g_barFliesRefusedUnlinked) + " deferredNotDrained=" + N(g_barFliesDeferred) + ";" + N(g_barFliesOff) + ";" + N(g_barFliesDrainFault) + ";" + N(g_barFliesDrainOdd) + ";" + N(g_barFliesDropped) + PerTownCounts()
             + "  |  seen = leverOff + noTown + refusedUnlinked + unlinkedAllowed + readFault + noLease + refusedNoLease + allowed + refused + t392 squadsSetAside (T-438); offThread spans ALL buckets, off[] only the six lease ones; sum(refused[...]) = refused + refusedNoLease + refusedUnlinked + barFlies refused + barFlies refusedUnlinked (creations and bar passes mixed; keys reset per world, totals never)."
               "  DECISION 48 (P8a): the three towngen* numbers are the whole rule, and THEIR UNIT IS CREATION DECISIONS,"
               " NOT PEOPLE - a town-squad creation and one whole bar-resident pass each count 1, and the two gates are"
               " mixed in all three, exactly as sum(refused[...]) below already mixes them. towngenHolderGenerated is every"
               " decision this game took to generate because the notebook says it holds the area; towngenRefusedNotHolder"
               " every one it refused because another game holds it or nobody does; towngenRefusedNoMap every one it refused"
               " because there was no fresh area map to ask.  RETIRED THIS BUILD, so a readout writes ABSENT and not zero (F172): clientMode,"
               " hostSeen, the barFlies host= span, the host field of cached[], and the two causes not-mine and"
               " held-by-other, which are now one not-holder cause carrying holder=other|nobody.  stale-lease is now"
               " no-map and fires on BOTH games.  noLease and noLeaseOff can only read 0 from this build on: they counted"
               " creations allowed with no area map, which decision 48 does not permit.  deferredNotDrained is the bar"
               " list this game refused WITHOUT consuming it, which is the C2 repair - those residents can still be made."
               "  P97: towngenFallbackLowerSlot is RETIRED (it had no writer since T-392, owner 337 a), so a readout"
               " writes ABSENT and not zero.  P8e-b (review-p8e C-2): THE 1 Hz"
               " BAR RE-OFFER IS DELETED and a deferral waits for the engine's own zone re-activation again -"
               " re-offering meant calling Town::spawnTheBarFlies through a Town pointer cached for up to 32 s from"
               " the main thread, and Town lifetime is not established (review-p4x).  barDeferred[towns,repeat,"
               "answered,pendOverflow] measures what that costs: `towns` distinct towns that deferred at least once,"
               " `repeat` deferrals onto a town already waiting, `answered` waiting towns a later engine pass went on"
               " to answer, `pendOverflow` deferrals past the 64-town table.  towns > 0 with answered == 0 is the"
               " case the deleted pass existed for, and it is the evidence a safe replacement would need."
               "  barResidentsReoffered and barReoffer[dropped,goneStale] are RETIRED with it, so a readout writes"
               " ABSENT and not zero (F172).  The pre-link cause is now `no-notebook` and it asks the NOTEBOOK"
               " link (F665); `pre-link` is RETIRED, so a readout writes ABSENT and not zero.");
}
} // namespace coop
namespace coop { int TownGenTownSid(void* town, char* out, int cap) { return TownSidPod(town, out, cap); } }
namespace coop { int TownGenResidentsGate(void* factory, void* building) { return T392ResidentsGate(factory, building); } }   /* T-392 (owner 335 a): see towngen.h */
namespace coop { int TownGenPlatoonHomeKey(const void* platoon, char* out, int cap) { return T591HomeKey(platoon, out, cap); } }   /* see towngen.h */
namespace coop { int TownGenGiveHomeTo(void* platoon, void* building) { return T591GiveHomeTo(platoon, building); } }   /* see towngen.h */
namespace coop { int TownGenGiveHomeByKey(void* platoon, const char* key) { if (platoon == 0 || key == 0 || key[0] == 0) return 0; void* const b = ObjectByPositionKey(key); return b == 0 ? 3 : T591GiveHomeTo(platoon, b); } }   /* see towngen.h */
namespace coop { void TownGenResidentsDone(int ran) { T392ResidentsDone(ran); } }
namespace coop { std::string TownFillLever(const std::string& arg) { return TownFillLeverImpl(arg); } }   /* see towngen.h */
namespace coop { void TownLossReset() { TownLossResetImpl(); } void TownLossNoteRows(const std::vector<char>& payload) { TownLossNoteRowsImpl(payload); } }   /* see towngen.h */
namespace coop { void TownGenOwedReset() { OwedReset(); } }   /* T-581: see towngen.h */
namespace coop { void TownGenOwedArrive(const std::vector<char>& payload) { OwedArrive(payload); } }
namespace coop { void TownGenOwedSaveFinished(const std::string& saveName, long reqSeq) { OwedSaveFinished(saveName, reqSeq); } }   /* see towngen.h */
namespace coop { void TownGenOwedNotThisSave() { ++g_owedNotThisSave; } }   /* see towngen.h */
namespace coop { int TownGenOwedCanSend() { return g_linkedCached != 0 ? 1 : 0; } }   /* see towngen.h */
namespace coop { int TownGenOwedSettling() { int n = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); n = (int)g_owedSettle.size(); ::LeaveCriticalSection(&g_tgLock); return n; } }   /* see towngen.h */
namespace coop { std::string TownGenOwedLever(const std::string& arg) { return OwedLever(arg); } }   /* T-392 fold: the building-sector context ends with the original call (T-438: and the set-aside flag; ran = 1 logs the furniture line) */
/* recruit3: the host option `recruitmult`, from the notebook's OPTIONS map (MAIN THREAD, store.cpp's drain) */
namespace coop {
void RecruitMultMapBegin() { g_recruitMultSeenInMap = 0; }
void RecruitMultOption(const std::string& v)
{
    g_recruitMultSeenInMap = 1;
    const int c = coopr::RecruitMultCode(v);
    if (c < 0) { ErrorLog("[RECRUIT] OPTIONS: the notebook sent recruitmult='" + v + "', which this build cannot read - IGNORED, this game keeps '" + std::string(coopr::RecruitMultName((int)g_recruitMultCode)) + "'"); return; }
    const long was = ::InterlockedExchange(&g_recruitMultCode, (long)c);
    if (was != (long)c) DebugLog("[RECRUIT] OPTIONS: the world's bar hire lists are rolled x" + std::string(coopr::RecruitMultName(c)) + " (was " + std::string(coopr::RecruitMultName((int)was)) + "; auto = the player slots seen, cap 4) - applies to towns first rolled from now on, on the game that holds each area");
}
void RecruitMultMapEnd(bool complete)
{
    if (!complete || g_recruitMultSeenInMap != 0) return;
    const long was = ::InterlockedExchange(&g_recruitMultCode, 0);
    if (was != 0) DebugLog("[RECRUIT] OPTIONS: the world's map names no recruitmult - back to 'auto'");
}
std::string RecruitMultStatus()
{
    const long code = g_recruitMultCode; const int slots = ZonesSlotsSeen();
    return "option=" + std::string(coopr::RecruitMultName((int)code)) + " slotsSeen=" + N(slots) + " effective=x" + N(coopr::RecruitMultEffective((int)code, slots));
}
}
/* refill1: the TEST verb and the notebook's rows (MAIN THREAD) */
namespace coop {
std::string RefillCommand(const std::string& args)
{
    std::string a(args);
    while (!a.empty() && (a[0] == ' ' || a[0] == '\t')) a.erase(0, 1);
    while (!a.empty() && (a[a.size() - 1] == ' ' || a[a.size() - 1] == '\t' || a[a.size() - 1] == '\r' || a[a.size() - 1] == '\n')) a.erase(a.size() - 1);
    if (a.empty())
        return "ok refill checks=" + N(g_rfChecks) + " toppedUp=" + N(g_rfToppedUp) + " addedPeople=" + N(g_rfAddedPeople) + " usualRecorded=" + N(g_rfUsualRecorded)
               + " rows=" + N((long long)g_rfTable.size()) + " forcedPending=" + N((long long)g_rfForced.size()) + " hook=" + N(g_rfHooked ? 1 : 0);
    if (a.compare(0, 3, "now") != 0 || (a.size() > 3 && a[3] != ' ' && a[3] != '\t')) return "error refill: refill [now <town stringID or name>]";
    std::string town = a.substr(3);
    while (!town.empty() && (town[0] == ' ' || town[0] == '\t')) town.erase(0, 1);
    if (town.size() >= 2 && (town[0] == '\'' || town[0] == '"') && town[town.size() - 1] == town[0]) town = town.substr(1, town.size() - 2);
    if (town.empty()) return "error refill: refill now <town stringID or name>";
    g_rfForced.insert(RfLower(town));
    DebugLog("[REFILL] refill now: '" + RfStr(town.c_str()) + "' is due at its next check-up on the game that holds it (TEST: the 5-day wait is skipped; the nearby rule still applies)");
    return "ok refill now '" + town + "' - due at its next check-up on the game that holds it (the nearby rule still applies)";
}
void RefillTableReset() { g_rfTable.clear(); for (std::map<std::string, coopbar::BarRow>::const_iterator it = g_rfUnsent.begin(); it != g_rfUnsent.end(); ++it) coopbar::BarMerge(&g_rfTable, it->first, it->second); }   /* the WELCOME push re-sends every row of THIS notebook's world; this game's unsent rows stay */
void RefillNoteRows(const std::vector<char>& payload)
{
    std::vector<coopbar::BarWireRow> in;
    if (coopbar::DecodeBarRows(payload.empty() ? 0 : &payload[0], payload.size(), &in) == 0) { ++g_rfRowsBad; ErrorLog("[REFILL] malformed TOWN_BAR from the notebook - ignored"); return; }
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_rfRowsIn;
        coopbar::BarMerge(&g_rfTable, in[i].sid, in[i].row);
        std::map<std::string, coopbar::BarRow>::iterator u = g_rfUnsent.find(in[i].sid);
        if (u != g_rfUnsent.end() && in[i].row.usualSet >= u->second.usualSet && in[i].row.usualPeople >= u->second.usualPeople && in[i].row.lastFilled >= u->second.lastFilled) g_rfUnsent.erase(u);   /* the notebook already has it */
    }
}
}
/* towns2: the TEST verb `townres` / `towns2` (MAIN THREAD) */
namespace coop {
std::string Towns2Command(const std::string& args)
{
    std::vector<std::string> tok;
    {
        std::string cur;
        for (size_t i = 0; i <= args.size(); ++i)
        {
            const char ch = (i < args.size()) ? args[i] : ' ';
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
            else cur += ch;
        }
    }
    if (tok.empty())
        return "ok townres checks=" + N(g_t2Checks) + " walks=" + N(g_t2Walks) + " queued=" + N(g_t2Queued) + " asked=" + N(g_t2Asked) + " rebuilt=" + N(g_t2Rebuilt)
               + " queueNow=" + N((long long)g_t2Queue.size()) + " forcedPending=" + N((long long)g_t2Forced.size()) + " killPending=" + N((long long)g_t2Kill.size()) + " hook=" + N((long long)(g_t2Hooked ? 1 : 0));
    const std::string usage = "error townres: townres [now <town stringID or name> [nosight] | kill <town stringID or name> <n> [res|pat] [regen|noregen] [all] | show <town stringID or name>]";
    if (tok[0] == "now")
    {
        size_t end = tok.size();
        int nosight = 0;
        if (end >= 3 && RfLower(tok[end - 1]) == "nosight") { nosight = 1; --end; }
        std::string town;
        for (size_t i = 1; i < end; ++i) { if (i > 1) town += " "; town += tok[i]; }
        if (town.size() >= 2 && (town[0] == '\'' || town[0] == '"') && town[town.size() - 1] == town[0]) town = town.substr(1, town.size() - 2);
        if (town.empty()) return usage;
        g_t2Forced[RfLower(town)] = nosight;
        DebugLog("[TOWNS2] townres now: '" + RfStr(town.c_str()) + "' is due at its next check-up on the game that holds it (TEST: the 5-day wait is skipped"
                 + std::string(nosight != 0 ? "; nosight: the sight rule is IGNORED for that walk - TEST ONLY" : "; the sight rule still applies") + ")");
        return "ok townres now '" + town + "'" + (nosight != 0 ? " nosight" : "") + " - due at its next check-up on the game that holds it";
    }
    if (tok[0] == "show")
    {
        std::string town;
        for (size_t i = 1; i < tok.size(); ++i) { if (i > 1) town += " "; town += tok[i]; }
        if (town.size() >= 2 && (town[0] == '\'' || town[0] == '"') && town[town.size() - 1] == town[0]) town = town.substr(1, town.size() - 2);
        if (town.empty()) return usage;
        g_t2Show[RfLower(town)] = 1;
        DebugLog("[TOWNS2] townres show: '" + RfStr(town.c_str()) + "' - its groups are counted at its next check-up on the game that holds it (TEST)");
        return "ok townres show '" + town + "' - counted at its next check-up on the game that holds it";
    }
    if (tok[0] == "kill")
    {
        size_t end = tok.size();
        int flags = 0;
        while (end > 3)
        {
            const std::string o = RfLower(tok[end - 1]);
            if (o == "res") { flags &= ~kT2KillPat; --end; }
            else if (o == "pat") { flags |= kT2KillPat; --end; }
            else if (o == "regen") { flags = (flags & ~kT2KillNoRegen) | kT2KillRegen; --end; }
            else if (o == "noregen") { flags = (flags & ~kT2KillRegen) | kT2KillNoRegen; --end; }
            else if (o == "all") { flags |= kT2KillAll; --end; }
            else break;
        }
        if (end < 3) return usage;
        const std::string& ns = tok[end - 1];
        int n = 0;
        for (size_t i = 0; i < ns.size(); ++i) { if (ns[i] < '0' || ns[i] > '9' || n > 999) return usage; n = n * 10 + (ns[i] - '0'); }
        if (n <= 0 || n > kT2KillCountMask) return usage;
        std::string town;
        for (size_t i = 1; i + 1 < end; ++i) { if (i > 1) town += " "; town += tok[i]; }
        if (town.size() >= 2 && (town[0] == '\'' || town[0] == '"') && town[town.size() - 1] == town[0]) town = town.substr(1, town.size() - 2);
        if (town.empty()) return usage;
        g_t2Kill[RfLower(town)] = n | flags;
        const std::string what = N((long long)n) + ((flags & kT2KillPat) != 0 ? " pat" : " res") + ((flags & kT2KillRegen) != 0 ? " regen" : ((flags & kT2KillNoRegen) != 0 ? " noregen" : "")) + ((flags & kT2KillAll) != 0 ? " all" : "");
        DebugLog("[TOWNS2] townres kill: '" + RfStr(town.c_str()) + "' " + what + " - at its next check-up on the game that holds it (TEST)");
        return "ok townres kill '" + town + "' " + what + " - at its next check-up on the game that holds it";
    }
    return usage;
}
}
// PROBE-START: P085
namespace coop { void TownGenP085Sector(int x, int y, long long* residents, long long* squads)
{
    if (residents != 0) *residents = 0;
    if (squads != 0) *squads = 0;
    if (x < 0 || x > 63 || y < 0 || y > 63) return;
    if (residents != 0) *residents = (long long)g_p085Residents[x * 64 + y];
    if (squads != 0) *squads = (long long)g_p085Squads[x * 64 + y];
} }
// PROBE-END: P085   /* decision 34 */
