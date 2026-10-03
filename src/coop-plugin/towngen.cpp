// P4 - town squad generation (decision 25; F466/F500; docs/persistence-service.md §17)
#include "towngen.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "zones.h"          // SectorOf, HostHoldsSectorTS, ZonesInitLocks
#include "net/session.h"    // SessionIsHost, SessionLinked
#include "handoff.h"        /* T-1 B1: CopySquadKeepAwake - owner 110, from the 0x6BA810 pre-hook */
#include "store.h"          // StoreRecordCreated (P4e)
#include "config.h"         /* E38 (e) / decision 43: RoleIsSingle - a lone game has no notebook and refuses nothing */
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
#include "../common/townrebuild.h"   /* towns2: the 60% rule, the 5-day timer, the sight test and the group verdict - the SAME header the offline suite compiles */
#include "game/hand.h"   /* towns2: hand::getSquad / hand::asBuilding */
#include "../common/townreoffer.h"   /* T-392 (owner 335 a): the set-aside causes and the re-offer decision - the SAME header the offline suite compiles */
#include "../common/barwire.h"  /* refill1: the shared bar record, the due decision and the per-entry counts - the SAME header the notebook and the offline suite compile */

namespace coop {
/* T-392 (owner 335 a): items.cpp's position key and its resolver (items.h declares the same three) - a building set aside
   is kept by its key and found again by it, never by pointer. */
int  ObjectPositionKey(void* obj, char* out, int cap, const char* suffix, int countIt, int podPos);
void* ObjectByPositionKey(const char* key);
int  ObjectByPositionKeyVerdict();
int  BoxKeyCap();   /* T-392 fold (review H1): items.cpp's kBoxKeyCap - the resolver's own key cap */
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
static void* CreatedTown(void* p, void* town) { (void)town; if (p != 0) ::InterlockedIncrement64(&g_createdSeen); return p; }   /* F507/decision 29: a group created on ice has no people yet - nothing useful to write down; it reaches the notebook at its first sleep after the host thaws it */
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
static bool BarPendOwes(void* town) { return BarPendHas(town); }   /* T-392 fold (owner 317 a): the table cannot overflow, so no town is "owed" by default any more */
static void T515AsideState(void* b, void* tmpl, const char* road);   /* T-515: defined beside the set-aside entries */
static void T515RefusedState(void* b, void* tmpl, int teardown, const char* road);
void* detour_create(void* self, void* faction, void* pos, void* town, unsigned long long p5, void* p6, void* p7,
                    unsigned long long p8, unsigned long long p9, unsigned char p10, void* p11, unsigned long long p12, unsigned int squadType)
{
    ::InterlockedIncrement64(&g_seen);
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
    /* decision 34: the notebook lists living groups for this town that THIS game has not placed yet - the town has people; do not invent more */
    /* E38 (e) / read-e38 Q3 - THE PENDING SET IS A CO-OP FACT. It is filled from the notebook's .meta files at
       install with no link required, so a LONE game with a leftover coop-store folder refused town squad
       generation for towns it had never shared - silently, and for the rest of the process. RoleIsSingle() is the
       precondition, tested here as well as inside StoreTownPeoplePending so the refusal cannot be reached by any
       route. ANY THREAD: this creation detour runs on the engine's workers, and the role is an interlocked long. */
    if (!RoleIsSingle())
    {
        char sidP[128];
        if (TownSidPod(town, sidP, 128) != 0 && StoreTownPeoplePending(sidP))
        {
            ::InterlockedIncrement64(&g_refusedPending);
            const std::string keyP = std::string(sidP) + " (notebook people pending)";
            bool firstP = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); firstP = g_refusedTowns.insert(keyP).second; ++g_refusedByTown[keyP]; ::LeaveCriticalSection(&g_tgLock);
            if (firstP) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + JsonEsc(sidP) + "\",\"type\":" + N((long long)squadType) + ",\"refused\":1,\"cause\":\"notebook-pending\"}");
            T515RefusedState(p6, p7, 0, "notebook-pending");   /* T-515: the building's own state still lands, as in the unmodded game */
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
struct T392ResPend { std::string sid; float x, z; long tries; int stateAtLoad; T392ResPend() : x(0.0f), z(0.0f), tries(0), stateAtLoad(0) {} };   /* T-515: stateAtLoad = the building-state step ran when the squads were set aside */
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
    *heldOut = h;
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
    int dc = townreoffer::kDefNoNotebook;
    if (g_linkedCached) { int h = 0; const int may = T392MayAt(s, &h); dc = townreoffer::DeferCause(1, may, h); }
    if (dc == townreoffer::kDefNone)
    {
        if (T392ResCount() != 0) { char k0[kT392KeyRoom]; if (T392Key(building, k0) != 0) T392ResErase(k0); }   /* answered: it leaves the table */
        g_t392PopAside = 0; g_t392PopSx = s.x; g_t392PopSy = s.y; g_t392PopTid = ::GetCurrentThreadId();   /* T-392 fold: the squads made inside are this building's (T-438: not set aside) */
        return 1;
    }
    char key[kT392KeyRoom];
    if (T392Key(building, key) == 0) { ::InterlockedIncrement64(&g_t392ResNoKey); return 1; }   /* no key = no way to find it again: as today */
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
        if (TownSidPod(town, sidQ, 128) != 0 && StoreTownPeoplePending(sidQ))
        {
            ::InterlockedIncrement64(&g_barFliesDeferredPending);   /* review-p4x: DEFER, do not destroy - the drain (Town+0x320) is IRREVERSIBLE within a loaded world (decomp_935f90: the list is filled once per Town object), so a pending-notebook refusal only holds the fill off until the notebook people are placed; the held-by-other branch below still drains */
            const std::string keyQ = std::string(sidQ) + " (bar residents, notebook people pending)";
            bool firstQ = false; TgLockInit(); ::EnterCriticalSection(&g_tgLock); firstQ = g_refusedTowns.insert(keyQ).second; ++g_refusedByTown[keyQ]; ::LeaveCriticalSection(&g_tgLock);
            if (firstQ) DebugLog(std::string("[VERDICT] {\"ev\":\"towngen\",\"town\":\"") + JsonEsc(sidQ) + " (bar residents)\",\"type\":0,\"refused\":1,\"family\":\"bar-residents\",\"cause\":\"notebook-pending-deferred\"}");
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
        refuse = (may == 0) || (may == kInventTeardown) || (may == -1);   /* DECISION 48 (P8a): no fresh map refuses on BOTH games - one answer about the area, not one per session role */
        if (may == 1) ::InterlockedIncrement64(&g_holderGenerated);
        if (may == 0) { ::InterlockedIncrement64(&g_refusedNotHolder); holderB = (bHeld == 1) ? "other" : "nobody"; }   /* decision 37: nobody holding it is a refusal too - it is not this game's to fill */
        /* T-392 (owner 337 a): the F666 bounded no-map wait is GONE from this gate - "cannot answer yet" always sets the list
           aside. (owner 335 a) So does "nobody holds the area": it is refused (not this game's to fill now) but NOT drained,
           because the drain below is irreversible. Only another game holding the area, or teardown, still drains. The second
           chance is the town's own check-up (T392Reoffer), not a clock. */
        const int dcB = townreoffer::DeferCause(1, may, bHeld);
        if (dcB == townreoffer::kDefNoMap) { cause = "no-map"; holderB = "-"; deferB = true; ::InterlockedIncrement64(&g_refusedNoMap); }   /* DEFERRED, not drained: this is not an answer about the area */
        if (dcB == townreoffer::kDefNobody) deferB = true;
        if (dcB != townreoffer::kDefNone) { t392Cause = townreoffer::DeferCauseName(dcB); t392Sx = bs.x; t392Sy = bs.y; }
        if (may == kInventTeardown) { cause = "teardown"; holderB = "-"; ::InterlockedIncrement64(&g_barFliesTeardown); }
    }
    if (!refuse) { BarPendClear(town); ::InterlockedIncrement64(&g_barFliesAllowed); BarFliesWithRecruitMult(town); return; }   /* recruit3: the engine's roll, plus the hire lists again x (recruitmult - 1) */   /* F666: answered, so it leaves the re-offer list */
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
   towns2 (user decisions 2026-09-26; .modding/investigations/towns2-resident-refill.md "Recommended mechanism"; T377/P087):
   A TOWN'S RESIDENT GROUP THAT HAS LOST 60% IS REBUILT BY THE ENGINE'S OWN SLEEP/WAKE WITH THE REFILL MARK.
   Kenshi has no "add the missing people" routine: a resident group is rebuilt whole from its recipe when it WAKES with the
   refill mark Platoon+0x118 = 1 (ActivePlatoon::setupCheck 0x4FF040 -> createRandomSquad 0x582F80). In a town that stays
   loaded no group ever wakes, so the game that HOLDS the town does, for one group, exactly what the engine does when that
   group leaves and re-enters loaded range:
   - DECIDE in the refill's own Town::periodicTick 0x92BE50 post-hook (main thread, T377): holder only with the zone
     live (the refill's test), every 5 in-game days per town (coopt2::RebuildDue). THE TIMER IS LOCAL TO THE HOLDER
     (g_t2Last): a town first seen by this holder starts its 5 days then, and a holder change restarts them - the smaller
     change (no notebook record, no new wire field). The walk: the town's type-1 (resident) list of its population
     manager Town+0xE0 (vt+0x8 0x94BBA0, node[0] next, node+0x10 the group's hand), each hand resolved by hand::getSquad.
     A group is QUEUED (by a copy of its hand - never a kept pointer) when coopt2::GroupVerdict says so: lost >= 60% of
     its original size +0xA4 (living counted here: awake - members not Character::hasDied; asleep - the stored +0xA0;
     0x925DD0 is NOT called, its helper writes +0xA0), not declared dead (+0x1F0), awake (+0x1D8 with its +0xF0 set),
     not a slave group (+0xD8 canRefresh 0 - the marker never picks them), whole (+0xC0 == RECORD_NONE, the marker's
     own test), no member unique, carried (+0x3D4) or a player's / stand-in's, mark 0 or 1, and UNSEEN.
   - SIGHT: the read names no engine "is visible to a player" test, so a FIXED RADIUS: no player character (this game's own
     or a copy of another player's - RfCharPod) within coopt2::kSightUnits = 5000 of any living member or of the group's
     building (Ownerships+0x38, Platoon+0x148 = Ownerships).
   - UNIQUES: 0x9A7250 (_checkForUniqueCharactersOnUnload) is a WRITER the mod already hooks (worldstate.cpp), not a
     "can this recipe produce a unique" test, and the read does not cover the recipe - so a group is skipped when any
     member (living or dead) is flagged unique by Character::isUnique 0x505ED0 (KillNamedIsUniquePod; a fault skips too).
   - ACT at the START of Faction::updateActivePlatoons 0x6BA810 (pre-hook, main thread, before the engine walks its lists),
     for a queued group of THAT faction (Ownerships+0x70 = Platoon+0x1B8): re-resolve the hand, re-check the holder, the
     60% rule, every refusal and sight LIVE, then in ONE SEH frame: +0x118 = 1, Platoon::deactivate(p, 0) 0x7EE910,
     Faction::deactivatePlatoon(f, p) 0x7F26A0, Platoon::activate(p) 0x7EB250, Faction::activatePlatoon(f, p) 0x7F2A30 -
     the engine's own sleep pair (6BA810:76-77) and wake pair (6B9620:16-17). The engine's loop then updates the new
     ActivePlatoon and setupCheck rebuilds it. TownGenTick then watches the group: mark back to 0 with members = `rebuilt`.
   TEST verb `townres` (alias `towns2`): `townres now <town> [nosight]` skips the 5-day wait for one town (nosight: the
   sight rule is ignored for that walk and its commitment - TEST ONLY), `townres kill <town> <n>` kills n living members
   of the holder's first eligible resident group through the engine's own Character::declareDead 0x7A5660.
   ------------------------------------------------------------------------------------------------------------------ */
unsigned long long kFactionUpdateActivePlatoonsRva = 0; static coop::AddrReg kFactionUpdateActivePlatoonsRva_reg("FactionUpdateActivePlatoons", &kFactionUpdateActivePlatoonsRva);   /* Steam_1.0.65 0x6BA810 */   // void Faction::updateActivePlatoons(float dt) - 0x6BA9B0 passes dt in xmm1 (P087 hooked it with this signature, T377)
unsigned long long kPlatoonDeactivateRva = 0; static coop::AddrReg kPlatoonDeactivateRva_reg("Deactivate", &kPlatoonDeactivateRva);   /* Steam_1.0.65 0x7EE910 - the row store.cpp hooks (detour_deactivate): the call goes through the mod's own sleep record */   // void Platoon::deactivate(GameDataContainer* forceCharacterStates) - the engine's sleep passes 0
unsigned long long kFactionDeactivatePlatoonRva = 0; static coop::AddrReg kFactionDeactivatePlatoonRva_reg("FactionDeactivatePlatoon", &kFactionDeactivatePlatoonRva);   /* Steam_1.0.65 0x7F26A0 */   // void Faction::deactivatePlatoon(Platoon*)
unsigned long long kPlatoonActivateRva = 0; static coop::AddrReg kPlatoonActivateRva_reg("Activate", &kPlatoonActivateRva);   /* Steam_1.0.65 0x7EB250 - the row store.cpp hooks (detour_activate): the call goes through the mod's own wake */   // void Platoon::activate()
unsigned long long kFactionActivatePlatoonRva = 0; static coop::AddrReg kFactionActivatePlatoonRva_reg("FactionActivatePlatoon", &kFactionActivatePlatoonRva);   /* Steam_1.0.65 0x7F2A30 */   // void Faction::activatePlatoon(Platoon*)
typedef void (*T2FactionUpdateFn)(void* faction, float dt);
typedef char* (*T2BucketFn)(void* mgr, int type);
typedef void (*T2PlatoonDeactivateFn)(void* platoon, void* forceCharacterStates);
typedef void (*T2FactionPlatoonFn)(void* faction, void* platoon);
typedef void (*T2PlatoonActivateFn)(void* platoon);
T2FactionUpdateFn orig_t2FactionUpdate = 0;
bool g_t2Hooked = false;
const int kT2Mem = 256, kT2Nodes = 256, kT2Players = 128, kT2HandBytes = 0x20;
const size_t kT2QueueCap = 64;
const DWORD kT2QueueMaxMs = 120000, kT2VerifyMaxMs = 60000, kT2ForcedLineMs = 30000;
const long long kT2LineCap = 400;
enum { kT2xGone = 0, kT2xFault, kT2xNotHolder, kT2xNoFactionTurn, kT2xNoAddr, kT2xStepFault, kT2xRebuildNotSeen, kT2xQueueFull, kT2xN };
const char* const kT2xName[kT2xN] = { "gone", "readFault", "notHolder", "noFactionTurn", "noAddr", "stepFault", "rebuildNotSeen", "queueFull" };
struct T2Q { unsigned char h[kT2HandBytes]; std::string town; int idx; int orig; int living; float x; float z; int nosight; DWORD at; };
struct T2V { unsigned char h[kT2HandBytes]; std::string town; int idx; int origBefore; int livingBefore; DWORD at; };
std::vector<T2Q> g_t2Queue;                 /* MAIN THREAD: groups to rebuild, by a copy of their hand */
std::vector<T2V> g_t2Verify;                /* MAIN THREAD: groups slept and woken, watched until the rebuild is seen */
std::map<std::string, double> g_t2Last;     /* MAIN THREAD: per town stringID, the in-game hour of THIS holder's last walk */
std::map<std::string, int> g_t2Forced;      /* MAIN THREAD: `townres now` targets, lower case stringID or name; 1 = nosight */
std::map<std::string, int> g_t2Kill;        /* MAIN THREAD: `townres kill` targets, lower case stringID or name; n */
std::map<std::string, DWORD> g_t2WhyAt;     /* MAIN THREAD: a forced target's last skip line (rate limit) */
long long g_t2Checks = 0, g_t2Due = 0, g_t2Walks = 0, g_t2WalkFault = 0, g_t2Groups = 0, g_t2Damaged = 0, g_t2Queued = 0, g_t2Asked = 0, g_t2Rebuilt = 0,
          g_t2Kills = 0, g_t2Killed = 0, g_t2Lines = 0, g_t2LinesDropped = 0, g_t2OffThread = 0, g_t2NoClock = 0, g_t2ActFaults = 0;
long long g_t2Verdict[coopt2::kVerdicts];
long long g_t2Skip[kT2xN];

static void T2Line(const std::string& s) { if (g_t2Lines >= kT2LineCap) { ++g_t2LinesDropped; return; } ++g_t2Lines; DebugLog(s); }
static std::string T2GroupLine(const std::string& town, int idx, int living, int orig, const std::string& what)
{
    return "[TOWNS2] town '" + town + "' group " + N((long long)idx) + " living=" + N((long long)living) + "/" + N((long long)orig) + " -> " + what;
}
/* One group, read through a copy of its hand. ok: 1 read, 2 the hand no longer resolves, 3 an implausible member list, 0 fault */
struct T2Grp { int ok; void* platoon; void* faction; void* leader; int members; coopt2::GroupFacts f; int nl; void* live[kT2Mem]; float px[kT2Mem]; float pz[kT2Mem]; int nd; float dx[kT2Mem]; float dz[kT2Mem]; int hasB; float bx; float bz; };   /* nd/dx/dz: dead members (review-towns2 1f) */
static void T2GroupPod(const void* h, T2Grp* G)
{
    std::memset(G, 0, sizeof(*G));
    __try
    {
        void* p = (void*)((const hand*)h)->getSquad();   /* 0x791CF0: types 1 / 0x22 only, index and serial checked (T377) */
        if (p == 0) { G->ok = 2; return; }
        const char* P = (const char*)p;
        G->platoon = p;
        G->f.orig = *(const int*)(P + 0xA4);
        G->f.sepType = *(const int*)(P + 0xC0);
        G->f.canRefresh = *(const unsigned char*)(P + 0xD8);
        G->f.mark = *(const int*)(P + 0x118);
        G->f.dead = *(const unsigned char*)(P + 0x1F0);
        G->f.hasUniques = *(const unsigned char*)(P + 0xAC) != 0 ? 1 : 0;   /* review-towns2 1g */
        G->faction = *(void* const*)(P + 0x148 + 0x70);   /* Ownerships (Platoon+0x148) ::faction +0x70 */
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
                if (KillNamedIsUniquePod(c) != 0) G->f.unique = 1;   /* 1 unique, -1 the test faulted: both skip */
                if (*((const unsigned char*)c + 0x3D4) != 0) G->f.carried = 1;   /* _isBeingCarried (spawn.cpp kBeingCarriedOff) */
                ::Faction* fc = c->getOwnerFactionDirect();
                if (fc != 0 && (IsPlayerFaction(fc) || IsStandInFaction(fc))) G->f.player = 1;
                if (*((const unsigned char*)c + 0x2F8) == 2) G->f.jailed = 1;   /* review-towns2 1g: imprisoned (the engine collects it into Platoon+0x1F1 on deactivate) */
                if (c->hasDied())
                {
                    if (G->nd < kT2Mem) { const Ogre::Vector3 dv = c->worldPosition(); G->dx[G->nd] = dv.x; G->dz[G->nd] = dv.z; ++G->nd; }   /* a corpse someone may be looting counts for sight */
                    continue;
                }
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
/* The town's type-1 (resident) list: copies of up to `cap` group hands. 1 read, 2 no manager, 0 fault. MAIN THREAD */
static int T2NodesPod(void* town, unsigned char (*hs)[kT2HandBytes], int cap, int* total)
{
    *total = 0;
    __try
    {
        char* mgr = *(char* const*)((const char*)town + 0xE0);
        if (mgr == 0) return 2;
        char* b = ((T2BucketFn)((*(void* const* const*)mgr)[1]))(mgr, 1);   /* manager vt+0x8 = 0x94BBA0: manager + 0x10 + type * 0x78 */
        if (b == 0) return 0;
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
        if (RfCharPod(c, &pl, &px, &pz) != 0 && pl != 0) { if (n >= cap) return -1; x[n] = px; z[n] = pz; ++n; }   /* review-towns2 3c: more player characters than the table = treated as seen (np < 0) */
    }
    return n;
}
/* 1 = a player character is within the sight radius of a living member or of the building; no world reads as seen */
static int T2Seen(int np, const float* plx, const float* plz, const T2Grp* G)
{
    if (np < 0) return 1;
    if (coopt2::WithinAny(plx, plz, np, G->px, G->pz, G->nl, coopt2::kSightUnits) != 0) return 1;
    if (coopt2::WithinAny(plx, plz, np, G->dx, G->dz, G->nd, coopt2::kSightUnits) != 0) return 1;   /* review-towns2 1f: corpses */
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
/* TEST ONLY (`townres kill <town> <n>`): n living members of the first awake, whole, non-slave resident group of 3 or more
   with 2 or more living (no unique, player or carried member, and a recipe that cannot make a unique - T394: the walk
   refuses such a group as unique, so killing in it tests nothing) die through the engine's own Character::declareDead
   0x7A5660 (StoreDeclareDeadPod - the platoonkill lever's path). One member always stays alive (the leader when it can),
   so the group is never declared dead. MAIN THREAD, holder only (the caller). */
static void T2Kill(void* town, const std::string& tn, int want)
{
    unsigned char hs[kT2Nodes][kT2HandBytes]; int total = 0;
    const int r = T2NodesPod(town, hs, kT2Nodes, &total);
    if (r != 1) { T2Line("[TOWNS2] kill: town '" + tn + "' - its resident list could not be read (step=" + N((long long)r) + ") - nothing killed (TEST)"); return; }
    for (int i = 0; i < total && i < kT2Nodes; ++i)
    {
        T2Grp G; T2GroupPod(hs[i], &G);
        if (G.ok != 1 || G.f.awake == 0 || G.f.dead != 0 || G.f.canRefresh == 0 || G.f.sepType != coopt2::kNullItemType || G.f.unique != 0 || G.f.hasUniques != 0 || G.f.player != 0
            || G.f.carried != 0 || G.f.orig < 3 || G.f.living < 2 || G.nl < 2) continue;
        int k = want; if (k > G.nl - 1) k = G.nl - 1;
        int killed = 0, faults = 0, refused = 0;   /* par6 fold (review-par6 #3): a kill counts only when the member reads dead after it */
        for (int pass = 0; pass < 2 && killed < k; ++pass)   /* pass 0: everyone but the leader; pass 1: the leader only if still short */
            for (int j = 0; j < G.nl && killed < k; ++j)
            {
                const bool isLeader = (G.live[j] == G.leader);
                if ((pass == 0) == isLeader) continue;
                if (StoreDeclareDeadPod(G.live[j]) != 1) ++faults; else if (StoreIsDeadPod(G.live[j]) == 1) ++killed; else ++refused;
            }
        ++g_t2Kills; g_t2Killed += killed;
        T2Line("[TOWNS2] kill: town '" + tn + "' group " + N((long long)i) + " living=" + N((long long)G.f.living) + "/" + N((long long)G.f.orig) + " -> killed " + N((long long)killed)
               + (faults != 0 ? " faults=" + N((long long)faults) : std::string())
               + (refused != 0 ? " refused=" + N((long long)refused) + " (a copy - the owner decides)" : std::string()) + " through Character::declareDead 0x7A5660 (TEST)");
        return;
    }
    T2Line("[TOWNS2] kill: town '" + tn + "' - no awake whole resident group of 3 or more with 2 or more living (no unique, player or carried member, recipe makes no unique) among "
           + N((long long)total) + " - nothing killed (TEST)");
}
/* The 5-day walk of one town's resident groups: each damaged group is queued or its refusal said. MAIN THREAD */
static void T2Walk(void* town, const std::string& tn, const float* tp, int nosight, bool forced)
{
    unsigned char hs[kT2Nodes][kT2HandBytes]; int total = 0;
    const int r = T2NodesPod(town, hs, kT2Nodes, &total);
    ++g_t2Walks;
    if (r != 1) { ++g_t2WalkFault; if (forced || r == 0) T2Line("[TOWNS2] town '" + tn + "' - its resident list could not be read (step=" + N((long long)r) + ")"); return; }
    float plx[kT2Players], plz[kT2Players];
    const int np = T2Players(plx, plz, kT2Players);
    int damaged = 0;
    for (int i = 0; i < total && i < kT2Nodes; ++i)
    {
        ++g_t2Groups;
        T2Grp G; T2GroupPod(hs[i], &G);
        if (G.ok != 1) { ++g_t2Skip[G.ok == 2 ? kT2xGone : kT2xFault]; continue; }
        G.f.seen = T2Seen(np, plx, plz, &G);
        const int v = coopt2::GroupVerdict(G.f, nosight);
        ++g_t2Verdict[v];
        if (v == coopt2::kNotDamaged) continue;
        ++damaged; ++g_t2Damaged;
        if (v != coopt2::kQueue) { T2Line(T2GroupLine(tn, i, G.f.living, G.f.orig, "skipped(" + std::string(coopt2::VerdictName(v)) + ")")); continue; }
        bool dup = false;
        for (size_t q = 0; q < g_t2Queue.size(); ++q) if (T2SameHand(g_t2Queue[q].h, hs[i])) { dup = true; break; }
        if (dup) continue;
        if (g_t2Queue.size() >= kT2QueueCap) { ++g_t2Skip[kT2xQueueFull]; T2Line(T2GroupLine(tn, i, G.f.living, G.f.orig, "skipped(queueFull)")); continue; }
        T2Q q;
        std::memcpy(q.h, hs[i], kT2HandBytes);
        q.town = tn; q.idx = i; q.orig = G.f.orig; q.living = G.f.living; q.x = tp[0]; q.z = tp[2]; q.nosight = nosight; q.at = ::GetTickCount();
        g_t2Queue.push_back(q); ++g_t2Queued;
        T2Line(T2GroupLine(tn, i, G.f.living, G.f.orig, std::string("queued") + (nosight != 0 ? " (TEST nosight: the sight rule is ignored for this group)" : "")));
    }
    if (forced) T2Line("[TOWNS2] town '" + tn + "' walked (townres now" + std::string(nosight != 0 ? " nosight" : "") + ") groups=" + N((long long)total) + " damaged=" + N((long long)damaged) + " players=" + N((long long)np));
}
/* The 0x92BE50 post-hook's second check (beside RefillCheck): holder only, zone live, every 5 in-game days per town. */
static void Towns2Check(void* town)
{
    if (town == 0 || !g_t2Hooked) return;   /* no act hook = nothing may be queued */
    if (::GetCurrentThreadId() != g_mainThread) { ++g_t2OffThread; return; }
    float tp[3] = { 0, 0, 0 };
    if (TownPosPod(town, tp) == 0) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0 || !coopbar::BarSidOk(sid)) return;
    const std::string key(sid);
    char nm[96]; if (RcTownNamePod(town, nm, 96) == 0) std::strcpy(nm, "?");
    const std::string tn = RfStr(nm);
    std::map<std::string, int>::iterator fi = T2Find(g_t2Forced, key, nm);
    std::map<std::string, int>::iterator ki = T2Find(g_t2Kill, key, nm);
    const int held = T2HeldHere(tp[0], tp[2]);
    if (held != 1)
    {
        g_t2Last.erase(key);   /* the 5 days belong to this holder: a later hold starts them again */
        if (fi != g_t2Forced.end() || ki != g_t2Kill.end())
            T2ForcedSkip(key, tn, held == -2 ? std::string("its zone is not live on this game - only the game that holds it acts")
                                             : "this game does not hold its zone (held=" + N((long long)held) + ") - the holder acts");
        return;
    }
    ++g_t2Checks;
    if (ki != g_t2Kill.end()) { const int want = ki->second; g_t2Kill.erase(ki); T2Kill(town, tn, want); }
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
    T2Walk(town, tn, tp, nosight, forced);
    g_t2Last[key] = now;
}
/* The engine's own sleep pair and wake pair, back to back, in ONE SEH frame. *step says how far it got. */
static int T2SleepWakePod(void* f, void* p, uintptr_t base, int* step)
{
    __try
    {
        *step = 1; *(int*)((char*)p + 0x118) = 1;   /* the refill mark (PlatoonCreationMessage 1) - setupCheck consumes it */
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
   THIS faction is re-read LIVE and, if every rule still holds, marked and slept+woken. Other factions' groups wait. */
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
            T2Line(T2GroupLine(q.town, q.idx, q.living, q.orig, "skipped(noFactionTurn: its faction's update did not come in " + N((long long)(kT2QueueMaxMs / 1000)) + " s)"));
            g_t2Queue.erase(g_t2Queue.begin() + i);
            continue;
        }
        T2Grp G; T2GroupPod(q.h, &G);
        if (G.ok == 1 && G.faction != faction) { ++i; continue; }   /* changed between the two reads: its own faction's turn decides */
        std::string res;
        if (G.ok != 1) { ++g_t2Skip[G.ok == 2 ? kT2xGone : kT2xFault]; res = std::string("skipped(") + (G.ok == 2 ? "gone" : "readFault") + " at commitment)"; }
        else if (T2HeldHere(q.x, q.z) != 1) { ++g_t2Skip[kT2xNotHolder]; res = "skipped(notHolder at commitment)"; }
        else
        {
            if (np == -2) np = T2Players(plx, plz, kT2Players);
            G.f.seen = T2Seen(np, plx, plz, &G);
            const int v = coopt2::GroupVerdict(G.f, q.nosight);
            if (v != coopt2::kQueue) { ++g_t2Verdict[v]; res = "skipped(" + std::string(coopt2::VerdictName(v)) + " at commitment)"; }
            else if (kPlatoonDeactivateRva == 0 || kFactionDeactivatePlatoonRva == 0 || kPlatoonActivateRva == 0 || kFactionActivatePlatoonRva == 0) { ++g_t2Skip[kT2xNoAddr]; res = "skipped(noAddr)"; }
            else
            {
                int step = 0;
                if (T2SleepWakePod(faction, G.platoon, base, &step) == 0)
                {
                    ++g_t2Skip[kT2xStepFault]; ++g_t2ActFaults;
                    res = "skipped(stepFault at step " + N((long long)step) + " of 5 - 1 mark, 2 deactivate, 3 deactivatePlatoon, 4 activate, 5 activatePlatoon)";
                    ErrorLog("[TOWNS2] FAULT in the sleep/wake of town '" + q.town + "' group " + N((long long)q.idx) + " at step " + N((long long)step));
                }
                else
                {
                    ++g_t2Asked;
                    res = "asked (refill mark set, slept and woken by the engine's own four calls; the rebuild runs in this faction's update)";
                    T2V w; std::memcpy(w.h, q.h, kT2HandBytes); w.town = q.town; w.idx = q.idx; w.origBefore = G.f.orig; w.livingBefore = G.f.living; w.at = nowMs;
                    g_t2Verify.push_back(w);
                }
            }
        }
        T2Line(T2GroupLine(q.town, q.idx, G.ok == 1 ? G.f.living : q.living, G.ok == 1 ? G.f.orig : q.orig, res));
        g_t2Queue.erase(g_t2Queue.begin() + i);
    }
}
void detour_t2FactionUpdate(void* faction, float dt)
{
    if (faction != 0 && ::GetCurrentThreadId() == g_mainThread && !g_t2Queue.empty()) Towns2Act(faction);   /* pre: before the engine walks its lists */
    if (faction != 0 && ::GetCurrentThreadId() == g_mainThread) coop::CopySquadKeepAwake(faction);   /* T-1 B1 (owner 110): a copy squad with a member on ground loaded here is not slept by this game (handoff.cpp, 1 Hz per faction) */
    orig_t2FactionUpdate(faction, dt);
}
/* MAIN THREAD (TownGenTick, once a second while any): a woken group is `rebuilt` once its mark is back to 0 with members */
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
        if (G.ok == 1 && G.f.awake != 0 && G.f.mark == 0 && G.members > 0 && G.f.living > 0)
        {
            ++g_t2Rebuilt;
            T2Line(T2GroupLine(v.town, v.idx, G.f.living, G.f.orig, "rebuilt (was " + N((long long)v.livingBefore) + "/" + N((long long)v.origBefore) + "; members=" + N((long long)G.members)
                   + " after " + N((long long)(nowMs - v.at)) + " ms)"));
            g_t2Verify.erase(g_t2Verify.begin() + i);
            continue;
        }
        if (nowMs - v.at > kT2VerifyMaxMs)
        {
            ++g_t2Skip[kT2xRebuildNotSeen];
            T2Line(T2GroupLine(v.town, v.idx, G.f.living, G.f.orig, "skipped(rebuildNotSeen in " + N((long long)(kT2VerifyMaxMs / 1000)) + " s: read=" + N((long long)G.ok) + " awake=" + N((long long)G.f.awake)
                   + " mark=" + N((long long)G.f.mark) + " members=" + N((long long)G.members) + ")"));
            g_t2Verify.erase(g_t2Verify.begin() + i);
            continue;
        }
        ++i;
    }
}
static void Towns2Install(uintptr_t base)
{
    if (kTownPeriodicUpdateRva == 0 || kFactionUpdateActivePlatoonsRva == 0 || kPlatoonDeactivateRva == 0 || kFactionDeactivatePlatoonRva == 0 || kPlatoonActivateRva == 0 || kFactionActivatePlatoonRva == 0)
    { ErrorLog("[TOWNS2] off: the address table lacks a row (TownPeriodicUpdate, FactionUpdateActivePlatoons, Deactivate, FactionDeactivatePlatoon, Activate or FactionActivatePlatoon) - resident groups are not rebuilt"); return; }
    if (coop::AddHook((void*)(base + kFactionUpdateActivePlatoonsRva), (void*)&detour_t2FactionUpdate, (void**)&orig_t2FactionUpdate) != coop::SUCCESS)
    { ErrorLog("[TOWNS2] AddHook Faction::updateActivePlatoons 0x6BA810 failed - resident groups are not rebuilt"); return; }
    g_t2Hooked = true;
    DebugLog("[TOWNS2] hook installed: Faction::updateActivePlatoons 0x6BA810 (pre: a queued resident group is marked and slept+woken before the engine's own loop); the decision rides the refill's Town::periodicTick 0x92BE50 post-hook");
}
static void Towns2Report()
{
    std::string vs, xs;
    for (int v = 1; v < coopt2::kVerdicts; ++v) { if (v > 1) vs += ","; vs += std::string(coopt2::VerdictName(v)) + "=" + N(g_t2Verdict[v]); }
    for (int x = 0; x < kT2xN; ++x) { if (x > 0) xs += ","; xs += std::string(kT2xName[x]) + "=" + N(g_t2Skip[x]); }
    DebugLog("[TOWNS2] REPORT checks=" + N(g_t2Checks) + " due=" + N(g_t2Due) + " walks=" + N(g_t2Walks) + " walkFault=" + N(g_t2WalkFault) + " groups=" + N(g_t2Groups)
             + " damaged=" + N(g_t2Damaged) + " queued=" + N(g_t2Queued) + " asked=" + N(g_t2Asked) + " rebuilt=" + N(g_t2Rebuilt) + " actFaults=" + N(g_t2ActFaults)
             + " verdicts[" + vs + "] other[" + xs + "] queueNow=" + N((long long)g_t2Queue.size()) + " verifyNow=" + N((long long)g_t2Verify.size())
             + " townsTimed=" + N((long long)g_t2Last.size()) + " kills=" + N(g_t2Kills) + " killed=" + N(g_t2Killed) + " forcedPending=" + N((long long)g_t2Forced.size())
             + " killPending=" + N((long long)g_t2Kill.size()) + " noClock=" + N(g_t2NoClock) + " offThread=" + N(g_t2OffThread) + " hook=" + N((long long)(g_t2Hooked ? 1 : 0))
             + " linesDropped=" + N(g_t2LinesDropped));
    for (std::map<std::string, int>::const_iterator f = g_t2Forced.begin(); f != g_t2Forced.end(); ++f)
        DebugLog("[TOWNS2] townres now '" + RfStr(f->first.c_str()) + "'" + (f->second != 0 ? " nosight" : "") + " still pending on this game - no check-up of that town has come here while this game holds it");
    for (std::map<std::string, int>::const_iterator k = g_t2Kill.begin(); k != g_t2Kill.end(); ++k)
        DebugLog("[TOWNS2] townres kill '" + RfStr(k->first.c_str()) + "' " + N((long long)k->second) + " still pending on this game - no check-up of that town has come here while this game holds it");
}
static void Towns2Teardown() { g_t2Queue.clear(); g_t2Verify.clear(); g_t2Last.clear(); g_t2WhyAt.clear(); }   /* hands and holds belong to the world that made them; the TEST targets stay */
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
static void T392ReofferBar(void* town, const std::string& sid)
{
    float tp[3] = { 0, 0, 0 };
    if (TownPosPod(town, tp) == 0) return;   /* unreadable on this pass: the next check-up asks again */
    const Sector s = SectorOf(tp[0], tp[2]);
    int n = 0, mark = 0;
    if (RfCountMarkPod(town, &n, &mark) == 0) return;
    int filledElsewhere = 0;
    { const coopbar::BarTable::const_iterator it = g_rfTable.find(sid); if (it != g_rfTable.end() && it->second.lastFilled >= 0.0) filledElsewhere = 1; }   /* the notebook records a roll: never a second one */
    int held = 0;
    const int may = T392MayAt(s, &held);   /* T-392 fold: the load-time gate's own decision */
    const int d = townreoffer::Decide(T392SectorLive(s.x, s.y) ? 1 : 0, (mark == 1 && n == 0) ? 1 : 0, filledElsewhere,
                                      StoreTownPeoplePending(sid.c_str()) ? 1 : 0, may, held);
    if (d == townreoffer::kReFilled) { BarPendClear(town); ::InterlockedIncrement64(&g_t392ReFilled); return; }   /* a later engine pass used the list */
    if (d == townreoffer::kReWait) { T392ReofferLog("bar", sid, "waiting", ""); return; }
    if (d == townreoffer::kReOther)
    {
        int dropped = 0;
        const int dr = DrainBarFlyListPod(town, &dropped);   /* the gate's own end state for an area another game holds */
        if (dr == 0) { ::InterlockedIncrement64(&g_t392ReFault); return; }   /* the list did not read: kept, asked again next check-up */
        ::InterlockedExchangeAdd64(&g_barFliesDropped, dropped);
        BarPendClear(town); ::InterlockedIncrement64(&g_t392ReOther);
        T392ReofferLog("bar", sid, "answered-other", "");
        return;
    }
    const long long allowed0 = g_barFliesAllowed;
    if (BarReofferPod(town) == 0) { BarPendClear(town); ::InterlockedIncrement64(&g_t392ReFault); T392ReofferLog("bar", sid, "fault", ""); return; }   /* a fault inside the engine is never retried */
    if (BarPendHas(town)) { T392ReofferLog("bar", sid, "waiting", ""); return; }   /* the gate set it aside again */
    if (g_barFliesAllowed == allowed0) { ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("bar", sid, "answered-other", ""); return; }   /* the gate answered for another game (it drained) */
    ::InterlockedIncrement64(&g_t392ReGen);
    T392ReofferLog("bar", sid, "generated", "");
}
/* T-392 fold (review H2): DOES THIS BUILDING ALREADY HAVE RESIDENTS ON THIS GAME - read LIVE at the moment of commitment.
   Two engine records, both written when a group is given its home: createRandomUnloadedSquad calls setHomeBuilding 0x7EBE40
   (57ea30:100), which calls Building::setResidentSquad 0x552780 (answers-loot2b R3, Read).
   (1) the building's own residents hand, Building::residentSquad at +0xD0 (Building.h:195; items.cpp:2765 kBldResidentSquad
       reads its id words from +0xD8) - resolved with hand::getSquad, which answers for a sleeping group too;
   (2) the group's own home-building hand, Ownerships(+0x148)+0x38, on the town's resident list (Town+0xE0 bucket 1, T2NodesPod -
       the walk towns2 already makes, towngen.cpp T2GroupPod).
   A copy of another game's group is created with that building as its home (spawn.cpp:789, RootObjectFactory::create's
   homeBuilding), so the same records cover copies. 1 = has residents; 0 = none; -1 = could not be read (the caller waits). */
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
static int T392BuildingHasResidents(void* town, void* building)
{
    const int own = T392ResidentHandPod(building);
    if (own != 0) return own;
    static unsigned char hs[kT2Nodes][kT2HandBytes];   /* MAIN THREAD only (the re-offer) */
    int total = 0;
    const int r = T2NodesPod(town, hs, kT2Nodes, &total);
    if (r == 0) return -1;
    if (r == 2) return 0;   /* no population manager: there is no list to be on */
    const int n = total < kT2Nodes ? total : kT2Nodes;
    int unknown = (total > kT2Nodes) ? 1 : 0;   /* a list longer than the copy is not a complete answer */
    for (int i = 0; i < n; ++i)
    {
        const int m = T392GroupHomePod(hs[i], building);
        if (m == 1) return 1;
        if (m < 0) unknown = 1;
    }
    return unknown != 0 ? -1 : 0;
}
static void T392ReofferResidents(void* town, const std::string& sid)
{
    void* const factory = g_t392Factory;
    if (factory == 0) return;
    std::vector<std::string> keys; std::vector<T392ResPend> rows;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    {
        /* T-392 fold (review MED): ROUND-ROBIN - start just after the last key this town's check-up looked at and wrap once, so
           every waiting building is looked at however many wait; at most 8 per check-up */
        std::string after;
        { const std::map<std::string, std::string>::const_iterator c = g_t392ResCursor.find(sid); if (c != g_t392ResCursor.end()) after = c->second; }
        const std::map<std::string, T392ResPend>::const_iterator start = after.empty() ? g_t392Res.begin() : g_t392Res.upper_bound(after);
        for (std::map<std::string, T392ResPend>::const_iterator it = start; it != g_t392Res.end() && keys.size() < 8; ++it)
            if (it->second.sid == sid) { keys.push_back(it->first); rows.push_back(it->second); }
        for (std::map<std::string, T392ResPend>::const_iterator it = g_t392Res.begin(); it != start && keys.size() < 8; ++it)
            if (it->second.sid == sid) { keys.push_back(it->first); rows.push_back(it->second); }
        if (!keys.empty()) g_t392ResCursor[sid] = keys.back();
    }
    ::LeaveCriticalSection(&g_tgLock);
    if (keys.empty()) return;
    const int pend = StoreTownPeoplePending(sid.c_str()) ? 1 : 0;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const Sector s = SectorOf(rows[i].x, rows[i].z);
        const std::string extra = " building=" + keys[i];
        int held = 0;
        const int may = T392MayAt(s, &held);   /* T-392 fold: the load-time gate's own decision */
        const int d = townreoffer::Decide(T392SectorLive(s.x, s.y) ? 1 : 0, 0, 0, pend, may, held);
        if (d != townreoffer::kReGenerate && d != townreoffer::kReOther) { T392ReofferLog("residents", sid, "waiting", extra); continue; }
        void* const b = ObjectByPositionKey(keys[i].c_str());
        if (b == 0)
        {
            if (ObjectByPositionKeyVerdict() == 1) { T392ResErase(keys[i].c_str()); ::InterlockedIncrement64(&g_t392ReGone); T392ReofferLog("residents", sid, "gone", extra); }   /* kObjKeyNone: a complete search of the live zones found no such building */
            else T392ReofferLog("residents", sid, "waiting", extra);   /* ambiguous, or an incomplete walk: asked again next check-up */
            continue;
        }
        const int has = T392BuildingHasResidents(town, b);   /* T-392 fold (review H2): the live read, at the moment of commitment */
        if (has > 0) { T392ResErase(keys[i].c_str()); ::InterlockedIncrement64(&g_t392ReHasRes); ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("residents", sid, "answered-other", extra + " why=has-residents"); continue; }   /* T-438: the furniture and faction were set at load (the set-aside run); only the squads were left, and the other game's are here */
        /* T-438 (a) (t438-a-towngen, manager 2026-10-02): another game holds the area - the furniture and faction were set by the
           set-aside run at load, so the entry is erased WITHOUT a second engine call (it would add the town's resident budget again
           and refuse its own group). The other game's residents arrive as copies. */
        if (townreoffer::ReofferRunsPopulate(d) == 0) { T392ResErase(keys[i].c_str()); ::InterlockedIncrement64(&g_t392ReOther); T392ReofferLog("residents", sid, "answered-other", extra + (has < 0 ? " why=other-holds residents=unread ran=no" : " why=other-holds ran=no")); continue; }
        if (has < 0) { T392ReofferLog("residents", sid, "waiting", extra); continue; }   /* could not be read: asked again next check-up */
        int b0n = 0; float b0f = 0.0f; const int b0ok = T392ResBudgetPod(town, &b0n, &b0f);   /* T-438 (b): the town's resident budget right before this SECOND engine call (decomp_57ed90:198-199 adds to it) */
        int rr0 = -1, rd0 = -1, rr1 = -1, rd1 = -1;
        T515RuinPod(b, &rr0, &rd0);
        const long long skip0 = g_t515RerunSkipped;
        int atLoad = 0;   /* T-515: read LIVE under the lock - the step may have marked the entry after this check-up copied its rows */
        TgLockInit(); ::EnterCriticalSection(&g_tgLock);
        { const std::map<std::string, T392ResPend>::const_iterator e = g_t392Res.find(keys[i]); if (e != g_t392Res.end()) atLoad = e->second.stateAtLoad; }
        ::LeaveCriticalSection(&g_tgLock);
        g_t515RerunBld = b; g_t515RerunAtLoad = atLoad;   /* T-515: detour_bldState skips the step for this building when it ran at load */
        const int r = WorldGenRerunPopulate(factory, b);   /* T-392 fold (review LOW): the entry stays until the run actually happened */
        g_t515RerunBld = 0; g_t515RerunAtLoad = 0;
        T515RuinPod(b, &rr1, &rd1);
        const std::string st = " stateAtLoad=" + N(atLoad) + " stateSkipped=" + N(g_t515RerunSkipped - skip0)
                               + " ruin=" + N(rr0) + "->" + N(rr1) + " doors=" + N(rd0) + "->" + N(rd1);
        std::string pa;
        if (r == 1)
        {
            /* T-438 (b), manager 2026-10-02: undo EXACTLY this re-run's own add, so every game keeps its one load-time add (as
               before T-392). Only when the int and the float moved by the same positive amount; otherwise the fields are left alone. */
            int b1n = 0; float b1f = 0.0f;
            if (b0ok == 0 || T392ResBudgetPod(town, &b1n, &b1f) == 0) { ::InterlockedIncrement64(&g_t392RerunPopUnread); pa = " popAdd=unread popUndone=unread"; }
            else
            {
                const long long dn = (long long)b1n - (long long)b0n;
                const double df = (double)b1f - (double)b0f;
                ::InterlockedExchangeAdd64(&g_t392RerunPopAdd, dn);
                const long long u = townreoffer::RerunPopUndo(dn, df);
                if (u > 0 && T392ResBudgetSetPod(town, (int)((long long)b1n - u), b0f) != 0) { ::InterlockedIncrement64(&g_t392RerunPopUndone); ::InterlockedExchangeAdd64(&g_t392RerunPopUndoneSum, u); pa = " popAdd=" + N(dn) + " popUndone=" + N(u); }
                else if (u > 0) { ::InterlockedIncrement64(&g_t392RerunPopUnread); pa = " popAdd=" + N(dn) + " popUndone=unread"; }
                else if (dn == 0 && df == 0.0) pa = " popAdd=0 popUndone=0";   /* nothing was added: nothing to undo */
                else { ::InterlockedIncrement64(&g_t392RerunPopMismatch); pa = " popAdd=" + N(dn) + " popUndone=mismatch"; }
            }
        }
        if (r < 0) { T392ResErase(keys[i].c_str()); ::InterlockedIncrement64(&g_t392ReFault); T392ReofferLog("residents", sid, "fault", extra); continue; }   /* never retried */
        if (r == 0) { T392ReofferLog("residents", sid, "waiting", extra); continue; }   /* refused again: the entry is still there (the gate counted the repeat) */
        T392ResErase(keys[i].c_str());   /* it ran */
        ::InterlockedIncrement64(&g_t392ReGen); T392ReofferLog("residents", sid, "generated", extra + pa + st);   /* T-438 (b): only a GENERATE re-offer reaches the re-run (ReofferRunsPopulate) */
    }
}
static void T392Reoffer(void* town)
{
    if (town == 0 || !g_on || RoleIsSingle()) return;
    if (::GetCurrentThreadId() != g_mainThread) return;
    const bool barOwed = orig_barFlies != 0 && BarPendHas(town);
    const bool resAny = T392ResCount() != 0;
    if (!barOwed && !resAny) return;
    char sid[128];
    if (TownSidPod(town, sid, 128) == 0) return;
    const std::string key(sid);
    if (barOwed) T392ReofferBar(town, key);
    if (resAny) T392ReofferResidents(town, key);
}
static void T392Teardown()
{
    TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_t392Res.clear(); g_t392ResCursor.clear(); g_t392DeferLogged.clear(); g_t392WaitLogged.clear(); g_t392AsideLogged.clear(); ::LeaveCriticalSection(&g_tgLock);
    g_t392Factory = 0; g_t392PopTid = 0; g_t392PopAside = 0;
}
static long long T392BarWaitingNow() { size_t c = 0; TgLockInit(); ::EnterCriticalSection(&g_tgLock); c = g_barPend.size(); ::LeaveCriticalSection(&g_tgLock); return (long long)c; }
void detour_townPeriodic(void* town)
{
    orig_townPeriodic(town);   /* the engine's own check-up first, unchanged */
    T392Reoffer(town);   /* T-392: the set-aside bar list and building residents get their second chance here */
    RefillCheck(town);
    Towns2Check(town);   /* towns2: the same post-hook - resident groups down 60% queued for the engine's own rebuild */
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
void TownGenWorldTeardown() { ZonesSlotsSeenReset("world teardown");   /* review-recruit3 5b: auto counts THIS world's players only */
                             TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_refusedTowns.clear(); g_refusedByTown.clear(); g_allowedTowns.clear(); g_tgCreatedIds.clear(); ::LeaveCriticalSection(&g_tgLock); { TgLockInit(); ::EnterCriticalSection(&g_tgLock); g_barPend.clear(); ::LeaveCriticalSection(&g_tgLock); } /* T-392 */ T392Teardown(); /* PROBE P085 */ for (int p085i = 0; p085i < 4096; ++p085i) { g_p085Squads[p085i] = 0; g_p085Residents[p085i] = 0; } /* refill1 */ RefillTeardown(); /* towns2 */ Towns2Teardown(); }   /* F666: a Town pointer belongs to the world that made it */
int TownGenMadeThisPlatoon(const char* platoonId)
{
    if (platoonId == 0 || platoonId[0] == 0) return 0;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    const int hit = (g_tgCreatedIds.find(std::string(platoonId)) != g_tgCreatedIds.end()) ? 1 : 0;
    ::LeaveCriticalSection(&g_tgLock);
    return hit;
}
void TownGenTick() { ::InterlockedExchange(&g_linkedCached, ((StoreWelcomedThisLink() != 0) && StoreRelayLinked()) ? 1 : 0);   /* F665: THE NOTEBOOK LINK */
                     if (!g_mainThreadFromTick) { g_mainThread = ::GetCurrentThreadId(); g_mainThreadFromTick = true; DebugLog("[TOWN] main thread = " + N((long long)g_mainThread) + " (captured on the first tick)"); } RecruitDrainLines(); /* refill1 */ RefillTick(); /* towns2 */ Towns2Tick(); }
static long long RefusedTownsCount() { TgLockInit(); ::EnterCriticalSection(&g_tgLock); const long long n = (long long)g_refusedTowns.size(); ::LeaveCriticalSection(&g_tgLock); return n; }
static long long TownGenCreatedIdCount() { TgLockInit(); ::EnterCriticalSection(&g_tgLock); const long long n = (long long)g_tgCreatedIds.size(); ::LeaveCriticalSection(&g_tgLock); return n; }
static std::string PerTownCounts()
{
    std::string s;
    TgLockInit(); ::EnterCriticalSection(&g_tgLock);
    for (std::map<std::string, long long>::const_iterator it = g_refusedByTown.begin(); it != g_refusedByTown.end(); ++it) s += " refused['" + it->first + "']=" + N(it->second);
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
             + " pending[refused,barFliesDeferred]=" + N(g_refusedPending) + "," + N(g_barFliesDeferredPending) + " cached[linked]=" + N((long long)g_linkedCached)
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
namespace coop { void TownGenResidentsDone(int ran) { T392ResidentsDone(ran); } }   /* T-392 fold: the building-sector context ends with the original call (T-438: and the set-aside flag; ran = 1 logs the furniture line) */
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
    const std::string usage = "error townres: townres [now <town stringID or name> [nosight] | kill <town stringID or name> <n>]";
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
    if (tok[0] == "kill")
    {
        if (tok.size() < 3) return usage;
        const std::string& ns = tok[tok.size() - 1];
        int n = 0;
        for (size_t i = 0; i < ns.size(); ++i) { if (ns[i] < '0' || ns[i] > '9' || n > 999) return usage; n = n * 10 + (ns[i] - '0'); }
        if (n <= 0) return usage;
        std::string town;
        for (size_t i = 1; i + 1 < tok.size(); ++i) { if (i > 1) town += " "; town += tok[i]; }
        if (town.size() >= 2 && (town[0] == '\'' || town[0] == '"') && town[town.size() - 1] == town[0]) town = town.substr(1, town.size() - 2);
        if (town.empty()) return usage;
        g_t2Kill[RfLower(town)] = n;
        DebugLog("[TOWNS2] townres kill: '" + RfStr(town.c_str()) + "' " + N((long long)n) + " - at its next check-up on the game that holds it (TEST)");
        return "ok townres kill '" + town + "' " + N((long long)n) + " - at its next check-up on the game that holds it";
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
