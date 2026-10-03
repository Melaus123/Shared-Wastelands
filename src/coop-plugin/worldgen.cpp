// worldgen.cpp - P032. See worldgen.h for why this exists and why these four hooks.
//
// THREADING. `ZoneManager::spawnChecksUpdateThreaded` is named "Threaded", and F262 established
// that MSVC's `rand()` keeps PER-THREAD state - so world generation demonstrably runs somewhere
// other than the main thread at least some of the time, and which thread each of these four
// functions uses is UNKNOWN until this run measures it. Every detour therefore does the minimum:
// interlocked counters and nothing else. No allocation, no locks, no logging on the hot path.
// A logging call on a game's worker thread is exactly how this project would deadlock the engine.
// Exception (P4z, 2026-09-03): createRandomCharacter's leaf gate takes zones' g_heldLock through HeldByOtherTS - a bounded memset/loop lock with no engine call inside (review-p4z audited every holder).
//
// Each detour records the thread it first saw, so "what thread" stops being a guess.

#include "worldgen.h"
#include "playerfaction.h"   /* T-230: IsPlayerFaction - the player's own starting squad passes the leaf gate */
#include "../common/orphanpurge.h"   /* orphan1: the leaf gate's unlinked arm (LeafUnlinked) */
#include "items.h"      /* PROBE P072: ObjectPositionKey (any thread, no allocation) */
#include "store.h"      /* PROBE P072: StoreMainThreadId; M11 C1: PlayersPresent, StorePresenceTick */
#include "../common/presence.h"   /* M11 C1: is another player in this world - the old link or the roster (PresenceDecide) */
#include "addresses.h"  /* PROBE P072: AddrReg */
namespace coop {
void P072Install(unsigned long long imgBase);   /* PROBE P072: from InstallWorldGen, two log-only detours */
std::string P072ReportToken();                  /* PROBE P072: counters for the [P032] REPORT line */
}
#include "spawn.h"
#include "worldsync.h"   // F322: WorldPtr   // P038 / F319: RetireOnDestroy
#include "zones.h"        /* P4z: SectorOf / HeldByOtherTS - the decision-33 area question */
#include "../common/removalreason.h"   /* M7a3: the engine's removal reason - an unload is never sent as a death */
#include "towngen.h"      /* T-392 (owner 335 a): TownGenResidentsGate - the residents gate */
#include "../common/townreoffer.h"   /* T-438: PopulateMode - the SAME header the offline suite compiles */
#include "handoff.h"      /* PROBE P116: ReadSquadAt - the guarded squad read */
#include <intrin.h>       /* PROBE P116: _ReturnAddress - the engine caller of a refused creation */
#include "net/session.h"  /* P4z: SessionLinked - a game with no session is its own world.  P8e (audit C4):
                             SessionIsHost is NOT read here any more.  P97 (p97-leaf, owner 334 a /
                             337 a): with no fresh map every game refuses; the bounded wait is retired. */

#include "coop_log.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3CreateRandomCharacter = 0; static coop::AddrReg kMig3CreateRandomCharacter_reg("RootObjectFactory_createRandomCharacter", &kMig3CreateRandomCharacter);   /* Steam_1.0.65 0x582C50 */
static unsigned long long kMig3CreateCharacterForBuilding = 0; static coop::AddrReg kMig3CreateCharacterForBuilding_reg("RootObjectFactory_createCharacterForBuilding", &kMig3CreateCharacterForBuilding);   /* Steam_1.0.65 0x585A80 */
static unsigned long long kMig3PopulateBuilding = 0; static coop::AddrReg kMig3PopulateBuilding_reg("RootObjectFactory_populateBuilding", &kMig3PopulateBuilding);   /* Steam_1.0.65 0x57ED90 */
static unsigned long long kMig3CheckForRepopulateTown = 0; static coop::AddrReg kMig3CheckForRepopulateTown_reg("ZoneManager_refillTownCheck", &kMig3CheckForRepopulateTown);   /* Steam_1.0.65 0x9FE6D0 */
static unsigned long long kMig3WorldDestroy = 0; static coop::AddrReg kMig3WorldDestroy_reg("WorldDestroy", &kMig3WorldDestroy);   /* Steam_1.0.65 0x798F50 */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
// The REAL Vector3, not a forward declaration: `createRandomCharacter` takes it BY VALUE, and an
// incomplete type cannot be passed by value. It also has to be the real one for a second reason -
// a 12-byte class is passed by hidden pointer under the x64 ABI, and only the actual definition
// lets the compiler generate a detour whose calling convention matches the engine's.
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>
#include <sstream>
#include <locale>

// T-186 (mig5): the game classes this file hooks come from our own game/ declarations (RE_Kenshi migration
// stage 4) - RootObjectFactory and GameWorld from their headers, ZoneManager and Town as forward declarations
// in game/forward.h. The three RootObjectFactory members and ZoneManager::checkForRepopulateTown that the old
// stand-ins declared were never called: the detours reach the engine through their orig_* pointers and the
// hook targets are address-table rows (above), so only the pointer types are needed here.
#include "game/RootObjectFactory.h"
#include "game/GameWorld.h"

// P038 / F319 - THE DESTRUCTION EVENT. `?destroy@GameWorld@@QEAA_NPEAVRootObject@@_NPEBD@Z`,
// real RVA 0x798F50, verified against the symbol resolver rather than the header (F037).
//
// Three consecutive runs died dereferencing a character the engine had already destroyed, and the
// guards this project relies on CANNOT see that: a freed block keeps a plausible vtable pointer, so
// `PlausibleObject` passes, and `PlausiblePtr` on an inline member can only check an address range.
// T088's host faulted inside `MedicalSystem::partAt` on an object whose part COUNT read as a small
// positive number while its parts ARRAY read as 8.
//
// P034 does have a real liveness test - the engine's own handle registry - but it SAMPLES, two rows
// per tick, so a character can be dead for a fraction of a second before anything notices, and a
// dereference in that window is a crash. **The project's own design rule is events over timers**;
// this is the event.
// GameWorld::destroy (the event) is declared in game/GameWorld.h and defined in gamecalls.cpp (T-186, mig5).

namespace {

// Counters. LONG64 + Interlocked because these are written off unknown threads.
volatile LONG64 g_charCreated   = 0;   // createRandomCharacter, calls that went through
volatile LONG64 g_charForBld    = 0;
volatile LONG64 g_populateBld   = 0;
volatile LONG64 g_repopTown     = 0;

// Suppressed counts - how often the gate actually refused something. A gate whose refusal count
// is zero did not run, whatever the toggle says, and that has to be visible as a number rather
// than inferred from a population that failed to change.
volatile LONG64 g_blkChar       = 0;   // the leaf gate - F269. This is the one that matters.
volatile LONG64 g_charPlayerPass = 0;  // T-230: player-faction characters let through where the leaf gate would have refused
volatile LONG64 g_blkCharForBld = 0;
volatile LONG64 g_blkPopulate   = 0;
volatile LONG64 g_blkRepop      = 0;
volatile LONG   g_repopForceOn  = 0;   // P18 fold 2 `townrepop on|off` (TEST-ONLY): every town the engine asks about is rebuilt
volatile LONG64 g_repopForced   = 0;   // answers turned to "rebuild" by that lever

// First thread id seen per hook. 0 = never called.
volatile LONG g_tidChar   = 0;
volatile LONG g_tidBld    = 0;
volatile LONG g_tidPop    = 0;
volatile LONG g_tidRepop  = 0;

// THREE states, not two (F273). `off` gates all four hooks; `leaf` gates ONLY the leaf. They are
// different experiments and mixing them makes a failure unattributable: "the leaf refused every
// character" and "an upstream path was skipped" need completely different responses, and F271
// already showed the upstream gate diverts the engine onto a path it would not otherwise take.
// `leaf` is also the arm that matches what Option A would actually ship.
const LONG kGenOn   = 1;   // engine generates its world exactly as today
const LONG kGenOff  = 0;   // all four gated
const LONG kGenLeaf = 2;   // ONLY createRandomCharacter gated - the clean arm. Since P4z `leaf` is
                           // AREA-GATED: characters are created where this game holds the area and
                           // refused where another player holds it (decisions 25/31(b) (creation); with no fresh map every game refuses - P97 retired decision 33's no-map fallback).
// AUD 2026-09-17: LEAF IS THE DEFAULT ON EVERY GAME (decision 48: the game that holds the area generates it).
// The harness used to send `worldgen leaf` to the client only - a host-centred leftover. A game with no
// session is still its own world: the `g_wgLinkedCached == 0` arm in LeafGatedAt below lets everything
// through, so single-player is unchanged. `worldgen on` is the ungated control.
volatile LONG g_worldGenOn = kGenLeaf;

// The leaf is gated in both suppressing modes; the upstream three only in `off`.
bool LeafGated()     { return g_worldGenOn == kGenOff || g_worldGenOn == kGenLeaf; }

// decisions 25/31(b) (creation) (2026-09-03, F518; decision 33's no-map fallback retired by P97): in leaf mode the character factory is allowed where THIS game holds the
// area and refused where another player holds it, decided live at the call. With no fresh map EVERY game
// refuses (decision 48 - no role decides it) for as long as the map is missing (P97, owner 334 a / 337 a: the bounded
// wait is retired here as it was at the town gates in T-392). `off` still refuses everything. Counted so the
// outcome is a number.
//
// WHICH SECTOR, and why. `createRandomCharacter` carries the spawn position as its own
// `Ogre::Vector3 position` parameter (by value, see the declaration above), so rule (a) applies: the area
// is the one the character is about to be placed in, coop::SectorOf(position.x, position.z).
// WHERE THE PLAYER IS *DOES* NOW REACH THIS GATE, and the old blanket prohibition here no longer described it
// (verify-p5t MEDIUM-3). What is still forbidden is what it was written to forbid: choosing the AREA by the
// player's position. The area is the one the character is about to be placed in, full stop - a squad waking four
// sectors from the camera is judged where it wakes, and MyPlayerSector() must never be substituted for that.
// What the gate now READS, on top of the area's own row, is two ring-1 facts ABOUT that area:
//   `wRing1`     - is this area within one sector of THIS game's player (E13 attempt 2, H047: the engine streams
//                  only the ~3x3 around the player, so an unclaimed area beside them is being loaded by this game);
//   `wPeerRing1` - is it within one sector of ANOTHER game's player, from the notebook process's player-sector
//                  table (E25 re-designed) - which is what makes the presumption yield instead of doubling a town.
// Both are answers about the sector `s` below, taken in the same locked read as the rest of the view. Neither
// changes which sector is judged, which is the rule this comment exists to hold.
//
// Names are coop::-qualified because this anonymous namespace sits at GLOBAL scope in this file (the
// engine class declarations above it have to mangle without a namespace), not inside `namespace coop`.
/* P8e (build/read-host-audit.md C4, F669): g_leafAllowedNoMapHost is RETIRED.  P97 (owner 334 a / 337 a): the
   bounded no-map wait's counter that replaced it is RETIRED too - with no fresh area map every game refuses, so
   g_leafGatedNoMap (printed as leafRefusedNoMap) is "no fresh map, refused" and nothing else, and the report
   token allowedNoMapFallback is gone: a readout writes ABSENT for it and not zero. */
volatile LONG64 g_leafAllowedArea = 0, g_leafGatedHeld = 0, g_leafGatedNoMap = 0, g_leafAllowedUnlinked = 0;
/* P6j (verify-p6c MEDIUM-2): the leaf gate's teardown refusal, which used to land in gatedNotMine below - the engine
   freeing the world reported as "no player holds this area", in the one counter that is supposed to size how often
   decision 37 refuses a genuinely unclaimed area. Its own number. */
volatile LONG64 g_leafGatedTeardown = 0;
/* orphan1 (T420/T426, decision 37): refused while the game link was down because the LIVE notebook map names another game
   the holder of the area. Its own number - allowedUnlinked keeps meaning "no link, allowed". */
volatile LONG64 g_leafGatedHeldUnlinked = 0;
volatile LONG64 g_leafGatedOtherLoaded = 0;   /* decision 37 (was decision 35): gated because this game does NOT hold the area, while no other player holds it either - a different fact from "another player holds it", and it must not hide inside that number. The identifier keeps its P5f name so an old log still reads; the report label is gatedNotMine. */
volatile long g_wgLinkedCached = 0;   /* P5c: net::SessionLinked is a virtual call on a transport the main thread deletes (towngen.cpp:40); workers read this, refreshed on the tick.  P8e (audit C4): g_wgHostCached went with the gate's host term - it had one reader and it was that gate, and a dead role read left beside the lines being rewritten is the review-p8a M-4 mistake. */
// PROBE-START: P116 - leaf no-map refusal follow-up
/* .modding/04-probes.md P116 (Related: P97).  QUESTION: does a squad woken empty during a notebook outage get
   refilled after the area map returns?  The leaf gate's no-map refusal runs on any thread (the character factory
   runs on worker threads), so P116OnRefusal only COPIES the first 8 refusals of each world load into a fixed
   table - no log, no lock, no allocation.  P116Tick (MAIN THREAD, from WorldGenTick) logs each one with the
   engine caller's address and the container the character was being made into (createRandomCharacter's
   `owner`: the Platoon, .modding/03-systems/npc-context.md step 5), waits until that sector's area map answers
   again, and 60 s later logs the squad's member count (ActivePlatoon +0x58, read by coop::ReadSquadAt - the
   guarded squad read and its Platoon/ActivePlatoon back-pointer check) or "gone".  Log only.
   P97 review MED (manager fold): ordinary loads already refuse ~58 creations per game with no map (T783/T784/T787), so
   the 8 rows are split - rows 0-3 for refusals within 120 s of the world's first refusal (phase=load), rows 4-7 for
   later ones (phase=later: an outage).  Known probe-only race (review LOW): a refusal racing the world change can
   reuse a row; every pointer read stays under SEH, so the worst case is one odd line.
   A player-faction creation refused here still passes after the gate (worldgen.cpp createRandomCharacter detour), so
   a P116 row can name a creation that went ahead - read membersAtLog / members with that in mind. */
const int kP116Cap = 8;
const int kP116Half = 4;
const unsigned long kP116LoadMs = 120000UL;
const unsigned long kP116FollowMs = 60000UL;
struct P116Row { volatile LONG ready; LONG world; LONG nth; void* owner; unsigned long long rva; int sx, sy; unsigned long tid; int stage; unsigned long freshAt; };
P116Row g_p116[kP116Cap];
volatile LONG g_p116Claimed = 0;      /* load-phase rows claimed in the world load g_p116ClaimWorld names */
volatile LONG g_p116ClaimedLater = 0; /* later-phase rows (an outage) claimed in that world load */
volatile LONG g_p116WorldStartMs = 0; /* GetTickCount at that world's first refusal */
volatile LONG g_p116ClaimWorld = -1;
volatile LONG g_p116Refusals = 0;     /* every no-map refusal in that world load, the 8 kept and the rest */
void P116OnRefusal(const coop::Sector& s, void* owner, void* caller)   /* ANY THREAD: copies only */
{
    const LONG w = (LONG)coop::StoreWorldGenNow();
    const LONG cw = ::InterlockedCompareExchange(&g_p116ClaimWorld, 0, 0);
    if (cw != w && ::InterlockedCompareExchange(&g_p116ClaimWorld, w, cw) == cw)
    {
        ::InterlockedExchange(&g_p116Claimed, 0); ::InterlockedExchange(&g_p116ClaimedLater, 0); ::InterlockedExchange(&g_p116Refusals, 0);
        ::InterlockedExchange(&g_p116WorldStartMs, (LONG)::GetTickCount());
    }
    const LONG n = ::InterlockedIncrement(&g_p116Refusals);
    const bool later = (unsigned long)(::GetTickCount() - (unsigned long)::InterlockedCompareExchange(&g_p116WorldStartMs, 0, 0)) > kP116LoadMs;
    const LONG k = ::InterlockedIncrement(later ? &g_p116ClaimedLater : &g_p116Claimed) - 1;
    if (k < 0 || k >= kP116Half) return;
    const LONG i = later ? kP116Half + k : k;
    P116Row& r = g_p116[i];
    ::InterlockedExchange(&r.ready, 0);
    r.world = w; r.nth = n; r.owner = owner;
    r.rva = (caller != 0) ? (unsigned long long)((uintptr_t)caller - (uintptr_t)::GetModuleHandleA(0)) : 0ULL;
    r.sx = s.x; r.sy = s.y; r.tid = ::GetCurrentThreadId(); r.stage = 0; r.freshAt = 0;
    ::InterlockedExchange(&r.ready, 1);
}
int P116ApOfPlatoonPod(void* platoon, void** ap)   /* Platoon+0x1D8 -> ActivePlatoon under SEH (handoff.cpp's own form) */
{
    *ap = 0;
    if (platoon == 0) return 0;
    __try { *ap = *(void**)((char*)platoon + 0x1D8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ap = 0; return -1; }
}
int P116Members(void* owner)   /* MAIN THREAD: the squad's member count, or -1 = gone / not a squad */
{
    coop::SquadView sv;
    if (owner == 0) return -1;
    if (coop::ReadSquadAt(owner, &sv) == 1) return sv.count;   /* the container is the ActivePlatoon itself */
    void* ap = 0;
    if (P116ApOfPlatoonPod(owner, &ap) == 1 && ap != 0 && coop::ReadSquadAt(ap, &sv) == 1 && sv.platoon == owner) return sv.count;
    return -1;
}
void P116Tick()   /* MAIN THREAD (WorldGenTick) */
{
    const LONG w = (LONG)coop::StoreWorldGenNow();
    const unsigned long now = ::GetTickCount();
    const unsigned long mainTid = ::GetCurrentThreadId();
    for (int i = 0; i < kP116Cap; ++i)
    {
        P116Row& r = g_p116[i];
        if (::InterlockedCompareExchange(&r.ready, 1, 1) != 1 || r.world != w || r.stage >= 3) continue;
        if (r.stage == 0)
        {
            const int m = P116Members(r.owner);
            std::ostringstream o;
            o << "[PROBE] P116 refusal phase=" << (i >= kP116Half ? "later" : "load") << " n=" << (long)r.nth << " world=" << (long)w << " sector=" << r.sx << "," << r.sy
              << " callerRva=0x" << std::hex << r.rva << " owner=0x" << (unsigned long long)(uintptr_t)r.owner << std::dec
              << " membersAtLog=";
            if (m < 0) o << "none"; else o << m;
            o << " thread=" << (r.tid == mainTid ? "main" : "worker");
            DebugLog(o.str());
            r.stage = 1;
        }
        if (r.stage == 1)
        {
            coop::Sector s; s.x = r.sx; s.y = r.sy;
            if (coop::HeldByOtherTS(s) < 0) continue;
            r.freshAt = now; r.stage = 2;
            std::ostringstream o;
            o << "[PROBE] P116 map back n=" << (long)r.nth << " world=" << (long)w << " sector=" << r.sx << "," << r.sy << " followUpIn=60s";
            DebugLog(o.str());
        }
        if (r.stage == 2 && (unsigned long)(now - r.freshAt) >= kP116FollowMs)
        {
            const int m = P116Members(r.owner);
            std::ostringstream o;
            o << "[PROBE] P116 follow-up phase=" << (i >= kP116Half ? "later" : "load") << " n=" << (long)r.nth << " world=" << (long)w << " sector=" << r.sx << "," << r.sy
              << " owner=0x" << std::hex << (unsigned long long)(uintptr_t)r.owner << std::dec << " members=";
            if (r.owner == 0) o << "noOwner"; else if (m < 0) o << "gone"; else o << m;
            o << " refusalsThisWorld=" << (long)::InterlockedCompareExchange(&g_p116Refusals, 0, 0);
            DebugLog(o.str());
            r.stage = 3;
        }
    }
}
// PROBE-END: P116
static bool LeafGatedAt(const coop::Sector& s, void* p116Owner = 0, void* p116Caller = 0)   /* PROBE P116: the two trailing parameters */
{
    if (g_worldGenOn == kGenOff) return true;
    if (g_worldGenOn != kGenLeaf) return false;
    if (g_wgLinkedCached == 0)
    {
        /* orphan1 (T420/T426, decision 37): "no game link" is NOT "no notebook". T420: A created 65 people in B-held 43,11
           while B reloaded, with the notebook up and naming B the holder the whole time; after link-up nobody announced
           them (decision 33) and nothing removed them. The shortcut now asks the notebook map first, through the SAME
           thread-safe cache the linked arm reads: HeldByOtherTS = zones.cpp's g_hostHeld grid under g_heldLock, filled by
           the notebook's AREAMAP and fresh for 5 s. -1 (no live map: true solo / offline play) and 0 (unclaimed, or
           this game's) keep the old answer - allowed; 1 (another game holds it) is refused. */
        if (coopor::LeafUnlinked(coop::HeldByOtherTS(s)) == coopor::kLeafGatedHeldUnlinked) { InterlockedIncrement64(&g_leafGatedHeldUnlinked); return true; }
        InterlockedIncrement64(&g_leafAllowedUnlinked); return false;   /* review-p4z HIGH-1: a game with no session is its own world (P5c: the cached flag, this runs on worker threads) */
    }
    /* decision 37: the gate is "does the relay name THIS game the holder of this area". An area nobody holds is refused too -
       it is claimed by the first game to report it, and inventing into it from a distance is what duplicated a town (F514c). */
    /* review-p5g Q5: ONE locked read - the gate's decision and the counter that says WHY come from the same view (the cause
       used to be a second HeldByOtherTS call, which an AREAMAP could invalidate between the two). decision 37 amended:
       `wLoadedHere` is the first-there presumption for an area nobody has claimed yet. */
    /* E13 attempt 2 (H047): the fifth input, `ring1` - an unclaimed area within ring 1 of this game's own player is
       this game's to invent in, because the engine streams only the ~3x3 around the player and nobody else is loading
       it. The leaf gate and the town gate must answer the same question with the same `ring1`, or a town's squads and
       its individual characters would come from two different rules. */
    /* E25 (review-p5p HIGH-1): and the sixth and seventh - is the OTHER player also within ring 1 of it, and does
       this game's relay slot outrank theirs. The leaf gate and the town gate must yield on the same terms as well
       as presume on the same terms, or a town's squads and its individual characters come from two different rules
       at exactly the moment two players walk into it together. */
    int wHeld = 0, wMine = 0, wLoadedHere = 0, wRing1 = 0, wPeerRing1 = 0, wPeerLow = -2, wPresumedEmpty = 0, wPeerLowFresh = 1;
    coop::AreaViewTS(s, &wHeld, &wMine, 0, &wLoadedHere, &wRing1, &wPeerRing1, &wPeerLow, &wPresumedEmpty, &wPeerLowFresh);
    const int may = coop::MayInventFromView(wHeld, wMine, wLoadedHere, wRing1, wPeerRing1, coop::MySlotLower(wPeerLow), wPresumedEmpty, wPeerLowFresh);
    /* P6j (verify-p6c MEDIUM-2): FIRST, because kInventTeardown is negative and the no-map arm further down would
       otherwise take it - and before P6j the bare 0 it used to be was taken by the not-mine arm on the next line.
       The gate still refuses; only the number it is filed under changes. */
    if (may == coop::kInventTeardown) { InterlockedIncrement64(&g_leafGatedTeardown); return true; }
    if (may == 0) { if (wHeld == 1) InterlockedIncrement64(&g_leafGatedHeld); else InterlockedIncrement64(&g_leafGatedOtherLoaded); return true; }
    /* P8e (audit C4, F669) took the session role out of this arm; P97 (owner 334 a / 337 a) takes the bounded
       wait out of it too.  With no fresh area map EVERY game refuses, whatever its slot and however long the map
       has been missing; world population freezes during a notebook outage and resumes when the map is back.  A
       caller skips a refused member (F269); whether a squad woken empty is refilled at its next wake is Inferred
       and PROBE P116 measures it.  The answer is coopor::LeafLinked (src/common/orphanpurge.h), swept offline. */
    if (coopor::LeafLinked(may) == coopor::kLeafLinkedNoMap)
    {
        InterlockedIncrement64(&g_leafGatedNoMap);
        P116OnRefusal(s, p116Owner, p116Caller);   /* PROBE P116 */
        return true;
    }
    InterlockedIncrement64(&g_leafAllowedArea); return false;
}
bool UpstreamGated() { return g_worldGenOn == kGenOff; }

// P038 / F319 - the destruction hook's own state.
bool (*orig_destroy)(GameWorld*, RootObject*, bool, const char*) = 0;
volatile LONG64 g_destroySeen    = 0;   // every destroy the engine performed
volatile LONG64 g_destroyOurs    = 0;   // ...that was a character WE hold a pointer to
volatile LONG64 g_destroyUnloadedAll  = 0;  // F334/F336: justUnloaded=true, ANY object in the world
volatile LONG64 g_destroyUnloadedOurs = 0;  // F336: ...and it was one of OURS. Same scope as
                                            // destroyOurs, so this is the one that may be subtracted
volatile LONG g_tidDestroy       = 0;

void NoteThread(volatile LONG* slot)
{
    if (*slot == 0) InterlockedCompareExchange((LONG volatile*)slot, (LONG)::GetCurrentThreadId(), 0);
}

std::string S(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

// F270. Print addresses in HEX, in the same base as every source they will be compared against -
// resolve_stub.py, Ghidra, the findings file. The first version of this probe printed the resolved
// RVA in decimal beside a hand-typed decimal "expected", and three of the four hand conversions
// were wrong. Every hook was on the correct function; the LABEL said otherwise, and a run reported
// its own results as void because the check designed to catch a wrong address was itself wrong.
// A verification step that requires arithmetic by hand is not a verification step.
std::string H(unsigned long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << "0x" << std::hex << std::uppercase << v;
    return ss.str();
}

} // namespace

RootObject* (*orig_createRandomCharacter)(RootObjectFactory*, Faction*, Ogre::Vector3,
                                          RootObjectContainer*, GameData*, Building*, float) = 0;
void (*orig_createCharacterForBuilding)(RootObjectFactory*, Building*) = 0;
void (*orig_populateBuilding)(RootObjectFactory*, Building*) = 0;
bool (*orig_checkForRepopulateTown)(ZoneManager*, Town*) = 0;

// THE GATE. This is the leaf every world character passes through, and F269 established that
// suppressing it here is something the engine ALREADY HANDLES.
//
// An earlier version of this file left it un-gated, on the reasoning that its callers were "not
// known to tolerate null" and F117 shows this engine dereferencing such results unconditionally
// elsewhere. That was the right default while the callers were unread. They have now been read,
// and the answer is unambiguous:
//
//   RootObjectFactory::createRandomSquad  - all SIX sites guard with `if (result != 0)`
//   ActivePlatoon::restoreSquad           - both sites guard (one `goto`, one `if`)
//
// Eight of eight. There is no unguarded call site, so a null here is an outcome the engine's own
// code expects rather than one it might survive. That is the H010b shape - use a path the engine
// supports, do not fight one it merely tolerates - and it is the distinction between the one
// approach that worked in this project and the four that were capped.
//
// AND IT DOES NOT TOUCH OUR OWN SPAWNS, structurally rather than by a test:
//
//   createRandomCharacter -> RootObjectFactory::create        (the world's path, gated here)
//   our own spawn         -> RootObjectFactory::create        (enters BELOW the gate)
//
// So there is no "is this one ours?" check to get wrong, no flag, and no ordering assumption. The
// separation is a property of the call graph.
//
// `charCreated` still counts every call that went THROUGH, so it remains a clean time series, and
// `blockedChar` counts refusals separately - a gate whose refusal count is zero did not run,
// whatever the toggle says.
RootObject* detour_createRandomCharacter(RootObjectFactory* self, Faction* faction,
                                         Ogre::Vector3 position, RootObjectContainer* owner,
                                         GameData* characterTemplate, Building* home, float age)
{
    NoteThread(&g_tidChar);
    /* T-230 (T565 crash, H055): the leaf gate governs WORLD population. The engine's new game creates the player's own starting
       squad through this same function (newGameWithCharEdit 0x871F30 -> createRandomSquad -> here); a joiner that starts a new game
       while linked stands in an area the host holds, and a refusal left the character editor with no characters (initCharacters
       then read list[0] of an empty list). The player's own faction always passes. IsPlayerFaction is a guarded POD read (any thread). */
    if (LeafGatedAt(coop::SectorOf(position.x, position.z), (void*)owner, _ReturnAddress() /* PROBE P116 */))
    {
        /* P97: coopor::LeafCreationRefused is this rule as a pure function, so the offline suite shows the player's
           own faction still passes a no-map refusal. */
        if (coopor::LeafCreationRefused(1, coop::IsPlayerFaction(faction) ? 1 : 0) != 0) { InterlockedIncrement64(&g_blkChar); return 0; }
        InterlockedIncrement64(&g_charPlayerPass);
    }
    InterlockedIncrement64(&g_charCreated);
    return orig_createRandomCharacter(self, faction, position, owner, characterTemplate, home, age);
}

// P038 / F319. THE DESTRUCTION EVENT, and this detour is deliberately about as small as a detour
// can be: retire the pointer, then call the original.
//
// It runs BEFORE the engine frees anything, so at this instant the object is still valid and
// `MirrorRetire` only needs to compare addresses - it does no dereference at all, and it publishes
// with `InterlockedExchange`, which is what makes it legal here when the calling thread is unknown.
//
// NOTHING ELSE HAPPENS HERE. No logging, no allocation, no engine calls, no lock. That is the
// standing rule for every detour in this file and it matters more here than anywhere else: a
// destroy can happen on a worker thread during region streaming, and a logging call on a game's
// worker thread is exactly how this project would deadlock the engine.
//
// This CLOSES the window P034 cannot close. P034 samples two rows per tick through the engine's
// handle registry, so a character stays "alive" in our tables for a fraction of a second after it
// dies - and three consecutive runs crashed inside that window. An event has no window.
bool detour_destroy(GameWorld* self, RootObject* obj, bool justUnloaded, const char* debugInfo)
{
    NoteThread(&g_tidDestroy);
    InterlockedIncrement64(&g_destroySeen);

    // F323 - RESOLVE THE UID **BEFORE** RETIRING. `RetireOnDestroy` sets the slot's `dead` flag,
    // and `FindSpawnedUid` returns 0 for a dead slot BY DESIGN - that is the property F316/F318/F319
    // all depend on. So retiring first made the uid unfindable one line later, and T090 measured the
    // result exactly: `destroyOurs=60` and `despawnSent=0`, with all five despawn counters at zero
    // because the failing exit was the one branch that counted nothing.
    // F332 - and use the lookup that answers for a RETIRED pointer. F323 fixed the ordering so the
    // uid is resolved before THIS function retires it; it did not help the other retirement path.
    // P034's sampled liveness check withdraws a pointer when a character streams out, which happens
    // BEFORE the engine destroys it - and `FindSpawnedUid` answers 0 for a withdrawn slot by design.
    // So on that path the uid was unresolvable, `DropPuppet` never ran, and the authority never
    // announced: the F326 fix skipped exactly the characters it was written for.
    unsigned int uid = (obj != 0) ? coop::FindSpawnedUidForDestroy(obj) : 0;

    // T-304 (2), fold 1 - ONLY THE OWNER REMOVES A CHARACTER. The engine's 'eaten' removal of a registered copy of the other
    // game's character (run T674: B's copy of A's corpse, eaten by an animal A never had) is refused and answered FALSE, the
    // engine's own 'postponed' answer; every other reason goes through (RefuseEngineDestroyOfCopy says why). Any thread.
    if (uid != 0 && coop::RefuseEngineDestroyOfCopy(obj, uid, debugInfo)) return false;

    // F334 - **AND SPLIT UNLOAD FROM DEATH.** The hooked signature is
    // `destroy(RootObject*, bool justUnloaded, const char*)` and this detour ignored that parameter
    // entirely. It matters now in a way it did not before: until F332 a character P034 had already
    // retired resolved uid 0, so nothing was announced; F332 makes exactly that population
    // resolvable, and **that population is the one that streams out.** Announcing a despawn for a
    // region unload would tell the peer to delete a character that is coming back, and
    // `RetireOnDestroy` marks the slot `destroyed`, so `AdoptExisting` would mint a FRESH uid on
    // return - a despawn/respawn churn cycle, which is close to the create->destroy loop STATUS
    // records as deliberately not built.
    //
    // **Whether the engine actually calls this with `justUnloaded=true` is established (T219:
    // destroyUnloadedAll 796 client / 648 host)** - the parameter name is read, not
    // measured, and the counter below is what measured it. It stays both counted and obeyed: an unload
    // is still treated as not-a-death, which is the conservative direction - a peer keeping a body a
    // moment too long is a parity error; deleting a live character is a broken world. What P5h adds
    // is the missing half: the peer is told to RETIRE its copy (an UNLOAD, not a DESPAWN) from
    // `NotifyDespawn`, because after this destroy nothing can name the character any more.
    // F336 - TWO COUNTERS, BECAUSE THEY COUNT TWO POPULATIONS. This one sees EVERY destroy in the
    // world - buildings, items, everything - and answers "does the engine ever set this flag at
    // all", which nothing has ever measured. It must never appear in an arithmetic with
    // `destroyOurs`, which counts only characters we hold: a region unloading 400 objects of which
    // 3 are ours would make `destroyOurs - destroyUnloadedAll` read -397 for a quantity that is 0.
    coop::ItemsLootNoteDestroy(obj, justUnloaded ? 1 : 0);   /* loot2c fold (3c): the engine's just-unloaded flag rides along - an unload is never a touch; a registered research copy the engine destroys (the cursor item on closeInventory, a swap) is retired here - pointer compare only, any thread */
    if (justUnloaded) InterlockedIncrement64(&g_destroyUnloadedAll);

    if (obj != 0 && coop::RetireOnDestroy(obj))
    {
        InterlockedIncrement64(&g_destroyOurs);
        if (justUnloaded) InterlockedIncrement64(&g_destroyUnloadedOurs);   // F336: the same scope

        // F322 - AND TELL THE PEER. This is the moment F290 said did not exist: retirement stopped
        // US dereferencing a dead pointer and the peer kept its copy forever, so the two worlds
        // diverged by attrition - without crashing, which is worse than it sounds because nothing
        // reported it.
        //
        // Safe to send from here for a measured reason, not a hoped-for one: F321 recorded
        // `tidDestroy` as the MAIN thread on both instances. `NotifyDespawn` re-checks that and
        // refuses off-thread rather than trusting it - one run is evidence about what the engine
        // did, not a guarantee about what it will do. (F334: and `NoteThread` records only the
        // FIRST caller, so `tidDestroy` is evidence about one call, not about a whole run -
        // `despawnOffThread` is the counter that actually watches this.)
        //
        // F334 - `obj` is passed so the re-entry test can name the OBJECT rather than a window.
        // A bool set around our own destroy call would also swallow any NESTED destroy the engine
        // performs inside it - an owned sub-object dying in the same call would be silently
        // attributed to us and never announced.
        /* M7a3: for an AMBIGUOUS reason the engine's own hasDied decides, read HERE while the object is certainly whole (any thread, SEH) */
        const int removalDead = (!justUnloaded && coopremoval::EngineRemovalReasonClass(debugInfo) == coopremoval::kReasonAmbiguous) ? coop::StoreIsDeadPod((void*)obj) : -1;
        coop::NotifyDespawn(uid, obj, justUnloaded, debugInfo, removalDead);
    }
    return orig_destroy(self, obj, justUnloaded, debugInfo);
}

void detour_createCharacterForBuilding(RootObjectFactory* self, Building* b)
{
    NoteThread(&g_tidBld);
    if (UpstreamGated()) { InterlockedIncrement64(&g_blkCharForBld); return; }
    InterlockedIncrement64(&g_charForBld);
    orig_createCharacterForBuilding(self, b);
}

/* T-392 (owner 335 a): a building's residents are made once, at the first load of a never-saved zone - so a gate that cannot
   answer yet (no notebook link, no fresh map, nobody holds the area) SETS THEM ASIDE instead of letting every squad inside be
   refused. towngen.cpp keeps them by the building's position key and re-offers them from the town's own check-up. */
static volatile LONG64 g_deferPopulate = 0;
void detour_populateBuilding(RootObjectFactory* self, Building* building)
{
    NoteThread(&g_tidPop);
    if (UpstreamGated()) { InterlockedIncrement64(&g_blkPopulate); return; }
    /* T-438 (t438-worldgen): a SET-ASIDE building still runs the engine call - it switches the interior on (the furniture
       layout: bar, shop counters, storage) and sets the owner faction before it makes any squad (decomp_57ed90:77-79, 106-123).
       The gate raised the per-thread "squads set aside" flag with its building-sector context, so towngen's creation detour
       refuses the residents inside (own counter); the entry stays and the town's check-up re-offers it. */
    const int mode = townreoffer::PopulateMode(1, coop::TownGenResidentsGate((void*)self, (void*)building));
    if (mode == townreoffer::kPopRunAside) InterlockedIncrement64(&g_deferPopulate); else InterlockedIncrement64(&g_populateBld);
    __try { orig_populateBuilding(self, building); }
    __finally { coop::TownGenResidentsDone(AbnormalTermination() ? 0 : 1); }   /* t438-r1 (review LOW): a fault inside the engine call is not counted as a run */   /* T-392 fold / T-438: the context and the set-aside flag end with the call */
}
namespace coop {
/* T-392 (owner 335 a): the re-offer's call, MAIN THREAD (towngen's post-hook on Town::periodicTick) - the same gate and
   the same original as the detour above, inside a crash guard. 1 = the original ran; 0 = set aside again (or `off`); -1 = no
   original, or a fault inside the engine call. */
int WorldGenRerunPopulate(void* factory, void* building)
{
    if (orig_populateBuilding == 0 || factory == 0 || building == 0) return -1;
    __try
    {
        if (UpstreamGated()) { InterlockedIncrement64(&g_blkPopulate); return 0; }
        /* T-438: the furniture was loaded when the building was set aside at load, so a re-offer the gate sets aside AGAIN does
           not run the engine call a second time (kPopSkip); the gate raised the flag, Done(0) lowers it */
        if (townreoffer::PopulateMode(0, TownGenResidentsGate(factory, building)) == townreoffer::kPopSkip) { TownGenResidentsDone(0); InterlockedIncrement64(&g_deferPopulate); return 0; }
        InterlockedIncrement64(&g_populateBld);
        __try { orig_populateBuilding((RootObjectFactory*)factory, (Building*)building); }
        __finally { TownGenResidentsDone(AbnormalTermination() ? 0 : 1); }   /* T-392 fold; t438-r1 */
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { TownGenResidentsDone(0); return -1; }
}
}   /* namespace coop (T-392) */

// Returns FALSE when suppressed. F272 - an earlier version returned `true` and had it exactly
// backwards, which is why T079's gated instance called `populateBuilding` 25 times while the
// ungated control called it 0.
//
// The meaning is taken from the CALLER, which is the only place a boolean return has one.
// `ZoneMapContent::_activate` (0x9FEC00) is the sole caller:
//
//     if ((lVar16 != 0) && (cVar3 = thunk_FUN_1409fe6d0(DAT_142133960,lVar16), cVar3 != '\0')) {
//         ...  // loads the building list and does the repopulation work
//     }
//
// Non-zero ENTERS the work block. So true means "yes, repopulate this town" - the opposite of
// suppression. The previous comment claimed its value was "read from the decompile, not guessed",
// and it was: I read an early `return 1` inside the callee and assigned it a meaning from the
// function's NAME. The name is not the specification; the caller is.
bool detour_checkForRepopulateTown(ZoneManager* self, Town* t)
{
    NoteThread(&g_tidRepop);
    if (UpstreamGated()) { InterlockedIncrement64(&g_blkRepop); return false; }
    InterlockedIncrement64(&g_repopTown);
    const bool r = orig_checkForRepopulateTown(self, t);
    if (r || t == 0 || InterlockedCompareExchange(&g_repopForceOn, 0, 0) == 0) return r;
    InterlockedIncrement64(&g_repopForced);   /* townrepop (TEST-ONLY): the engine's own rebuild branch of _activate runs for this town */
    return true;
}

namespace coop {

// P5c - MAIN THREAD. Refresh the session flags the leaf gate reads. `LeafGatedAt` runs on the
// engine's character-factory worker threads, and net::SessionLinked() is a virtual
// call on a transport the main thread deletes (towngen.cpp:40 pays for the same lesson). Same
// shape as TownGenTick: an interlocked write here, a plain read there.
void WorldGenTick()
{
    StorePresenceTick();   /* M11 C1: the roster's presence answers, copied for every thread, once a frame (before the line below reads them) */
    /* M11 C1 (T-197, to-do M11): g_wgLinkedCached = ANOTHER PLAYER IS IN THIS WORLD - the old link up, or this link's PLAYERS roster
       shows another slot IN_WORLD. With the old link alone a world-road game took LeafGatedAt's lone-game arm and invented towns
       beside another game doing the same. No roster and no old link = alone, as before. */
    ::InterlockedExchange(&g_wgLinkedCached, coop::PlayersPresent(mppresence::kSiteWorldGen) ? 1 : 0);
    /* P8e (audit C4): the SessionIsHost refresh is gone with the gate term that read it. */
    P116Tick();   /* PROBE P116 */
}
/* orphan1: the leaf gate's unlinked refusals, for the [P033] REPORT orphan[] token (declared in spawn.h). */
long long WorldGenGatedHeldUnlinked() { return (long long)g_leafGatedHeldUnlinked; }

// F322 - destroy a local object through the engine's own path.
//
// It goes through the HOOKED function deliberately, so P038's detour runs on it: the pointer is
// retired again (harmlessly - `RetireOnDestroy` is idempotent) and the destruction is counted, so
// our own removals appear in `destroySeen`/`destroyOurs` beside the engine's. Calling
// `orig_destroy` would have skipped our bookkeeping and saved nothing.
//
// `NotifyDespawn` inside that detour refuses to announce a uid we do not author, so a peer removing
// its copy cannot echo a despawn back at the authority.
//
// MAIN THREAD ONLY. Returns false when the hook never installed or the world is not available -
// the caller reports that case rather than assuming the object went away.
bool DestroyLocalObject(void* obj)
{
    if (obj == 0) return false;
    if (orig_destroy == 0) return false;   // hook never installed; do not fabricate a destruction
    GameWorld* w = (GameWorld*)coop::WorldPtr();
    if (w == 0) return false;
    // F337 - the layout fact this line rests on, stated rather than warned about. (F336 put a
    // warning here about a "properly typed conversion" applying a base-class offset; that hazard is
    // not expressible - the parameter is `void*`, from which every cast form is a reinterpret.)
    //
    // What is actually assumed: `class Character : public RootObject, public GeneralAllocatedObject`
    // - `RootObject` is the FIRST, non-virtual base, so its subobject sits at offset 0 and this
    // address is valid as both. T091 corroborates it by measurement rather than by reading the
    // header: `destroyOurs=46` requires the `RootObject*` the engine hands `detour_destroy` to
    // compare equal to the `Character*` in our mirror. F332's identity test depends on the same
    // equality.
    w->destroy((RootObject*)obj, false, "kenshi-coop despawn (F322)");
    return true;
}

/* P18 fold 2 - TEST LEVER `townrepop on|off|show` (cloud T737): while on, every town ZoneManager::checkForRepopulateTown is asked
   about answers "rebuild" - ZoneMapContent::_activate (0x9FEC00) then runs the engine's own town rebuild (its saved-building drop
   0x9FD470, its loose-item list cleared) as the zone loads. Off by default; never sent. */
std::string TownRepopCommand(const std::string& arg)
{
    if (arg == "on" || arg == "off") InterlockedExchange(&g_repopForceOn, arg == "on" ? 1 : 0);
    else if (!arg.empty() && arg != "show") return "error townrepop: usage `townrepop on|off|show`";
    const std::string s = std::string("ok townrepop ") + (InterlockedCompareExchange(&g_repopForceOn, 0, 0) != 0 ? "on" : "off")
                        + " forced=" + S(g_repopForced) + " asked=" + S(g_repopTown);
    DebugLog("[P032] " + s);
    return s;
}

void SetWorldGenMode(int mode)
{
    InterlockedExchange(&g_worldGenOn, (LONG)mode);
    const char* what =
        (mode == kGenOn)   ? "ON - the engine generates its own world exactly as it does normally (the control; LEAF is the default)" :
        (mode == kGenLeaf) ? "LEAF - area-gated (decisions 25/31(b) (creation) with decision 33's no-map fallback): createRandomCharacter creates where this"
                             " game holds the area and refuses where another player holds it; the three"
                             " upstream hooks run"
                             " untouched. This is the clean arm, the DEFAULT since AUD 2026-09-17, and the one Option A would ship." :
                             "OFF - ALL FOUR hooks refuse. Note the upstream three change WHICH code"
                             " paths run, not just what they produce (F271), so a failure in this"
                             " mode is harder to attribute than one in LEAF.";
    DebugLog(std::string("[P032] worldgen ") + what
             + " | NOTE: characters that already exist are NOT removed by this.");
}

void ReportWorldGen()
{
    DebugLog(std::string("[P032] REPORT mode=")
             + (g_worldGenOn == kGenOn ? "on" : (g_worldGenOn == kGenLeaf ? "leaf" : "off"))
             + " charCreated=" + S(g_charCreated)
             + " charForBuilding=" + S(g_charForBld)
             + " populateBuilding=" + S(g_populateBld)
             + " repopulateTown=" + S(g_repopTown) + " repopForced=" + S(g_repopForced)
             + " | blockedChar=" + S(g_blkChar) + " charPlayerPass=" + S(g_charPlayerPass)
             + " leafArea[allowed,gatedHeld,gatedNotMine,leafRefusedNoMap,allowedUnlinked,gatedTeardown]=" + S(g_leafAllowedArea)
             + "," + S(g_leafGatedHeld) + "," + S(g_leafGatedOtherLoaded) + "," + S(g_leafGatedNoMap)
             + "," + S(g_leafAllowedUnlinked) + "," + S(g_leafGatedTeardown)
             + " blockedCharForBuilding=" + S(g_blkCharForBld)
             + " blockedPopulate=" + S(g_blkPopulate)
             + " deferredPopulate=" + S(g_deferPopulate)   /* T-392: residents set aside by the gate (each re-offer that sets it aside again counts again). T-438: at load the engine call still ran (faction, furniture) with its squads set aside */
             + " blockedRepop=" + S(g_blkRepop)
             + P072ReportToken()   /* PROBE P072 */
             // P038 / F319. `destroySeen` climbing proves the hook is live at all; `destroyOurs`
             // is the number that matters - how many of the engine's destructions concerned a
             // character we were still holding a pointer to. If `destroyOurs` is 0 across a whole
             // run in which characters demonstrably came and went, the hook is on the wrong
             // function or the engine removes characters by another route, and the crash class is
             // NOT closed. That has to be readable as a number rather than inferred from silence.
             + " | destroySeen=" + S(g_destroySeen)
             + " destroyOurs=" + S(g_destroyOurs)
             // F334 - `destroyOurs` alone reads as "the engine destroyed this many of ours", and
             // T091 proved that reading wrong: 46 of the client's 46 were our own despawn call
             // re-entering the hook. The correction lived in a DIFFERENT log line ([M1]), so the
             // number was still misleading where it is actually read. Its companions now sit
             // beside it.
             //
             // **THE ONLY VALID SUBTRACTION IS `destroyOurs - destroySelf - destroyUnloadedOurs`**,
             // and F336 is why the scope suffix is on the name rather than left to a reader: the
             // first version of this line subtracted an ALL-OBJECTS unload count from an
             // OURS-ONLY total and could go negative. `destroyUnloadedAll` is printed because it
             // answers a different and genuinely open question - does this engine ever set the
             // flag - and it is named so it cannot be dropped into the arithmetic by mistake.
             + " destroySelf=" + S(coop::DespawnSelfReentryCount())
             + " destroyUnloadedOurs=" + S(g_destroyUnloadedOurs)
             + " destroyUnloadedAll=" + S(g_destroyUnloadedAll)
             // F337 - `markDestroyedDeclined` used to be printed here, three lines below a comment
             // complaining that a correction printed in a different log line from the number it
             // corrects is still misleading. Its siblings (`stalePointers`, `retired`, `falseGone`)
             // are all in `[P034]`, so it lives there now.
             + " tidDestroy=" + S(g_tidDestroy)
             + " | threads char=" + S(g_tidChar)
             + " bld=" + S(g_tidBld)
             + " pop=" + S(g_tidPop)
             + " repop=" + S(g_tidRepop)
             + " main=" + S((long long)::GetCurrentThreadId()));
    DebugLog("[P032]   |  P8e (build/read-host-audit.md C4): the leaf gate's no-map arm no longer asks the session role.  gatedNoMap is printed as leafRefusedNoMap and allowedNoMapHost is RETIRED - a readout should write ABSENT for it and not zero.  P97 (owner 334 a / 337 a): allowedNoMapFallback is RETIRED with the bounded no-map wait - a readout should write ABSENT for it and not zero.  leafRefusedNoMap is every creation refused because there was no fresh area map, on EVERY game whatever its slot (the player's own faction still passes, counted in charPlayerPass); world population freezes during an outage and resumes when the map is back.");
}

void InstallWorldGen()
{
    uintptr_t imgBase = (uintptr_t)::GetModuleHandleA(0);

    // Resolve first, print what was got, and compare against the address resolve_stub.py gives
    // offline. F037 cost two in-game runs to a target picked by NAME that resolved elsewhere; the
    // expected RVA is printed beside the actual so a wrong resolution is VISIBLE in the run rather
    // than assumed away.
    intptr_t cc = (intptr_t)coop::AddrAbs(kMig3CreateRandomCharacter);
    intptr_t cb = (intptr_t)coop::AddrAbs(kMig3CreateCharacterForBuilding);
    intptr_t pb = (intptr_t)coop::AddrAbs(kMig3PopulateBuilding);
    intptr_t rt = (intptr_t)coop::AddrAbs(kMig3CheckForRepopulateTown);
    intptr_t dy = (intptr_t)coop::AddrAbs(kMig3WorldDestroy);   // P038 / F319

    struct { const char* name; intptr_t got; unsigned long long expect; } t[5] = {
        { "createRandomCharacter     ", cc, kMig3CreateRandomCharacter },       /* stage 7/9: expected = the table row */
        { "createCharacterForBuilding", cb, kMig3CreateCharacterForBuilding },
        { "populateBuilding          ", pb, kMig3PopulateBuilding },
        { "checkForRepopulateTown    ", rt, kMig3CheckForRepopulateTown },
        { "GameWorld::destroy        ", dy, kMig3WorldDestroy }
    };
    for (int i = 0; i < 5; ++i)
    {
        unsigned long long rva = ((uintptr_t)t[i].got > imgBase)
                               ? (unsigned long long)((uintptr_t)t[i].got - imgBase) : 0ull;
        // gog1 fold: the address IS the table row (expect), so a MATCH/MISMATCH verdict here could never fail; whether
        // the table describes this executable is AddrInit's gate. The line names the row's value and the table.
        DebugLog(std::string("[P032] ") + t[i].name + " = " + H((unsigned long long)t[i].got)
                 + " rva=" + H(rva) + " (the loaded table's row " + H(t[i].expect) + ", table '" + coop::AddrTableName() + "')");
    }

    // Each hook installs independently and a failure is reported rather than fatal - one missing
    // instrument must not take the other three down with it (the P026 rule). A counter that reads
    // zero because its hook never installed looks identical to one that reads zero because the
    // engine never called it, so the install result has to be in the log.
    if (cc != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)cc, (void*)&detour_createRandomCharacter,
                                                    (void**)&orig_createRandomCharacter);
        DebugLog(s == coop::SUCCESS ? "[P032] createRandomCharacter AddHook SUCCESS"
                                         : "[P032] createRandomCharacter AddHook FAILED");
    }
    else ErrorLog("[P032] createRandomCharacter resolved to 0 - charCreated will read zero for a"
                  " reason that has NOTHING to do with the engine");

    // P038 / F319 - installed FIRST among the rest, because it is the one that keeps the process
    // alive rather than the one that measures the world.
    if (dy != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)dy, (void*)&detour_destroy,
                                                    (void**)&orig_destroy);
        DebugLog(s == coop::SUCCESS ? "[P038] GameWorld::destroy AddHook SUCCESS - a destroyed"
                                           " character is now withdrawn AT THE MOMENT it dies,"
                                           " instead of whenever the sampled handle check next"
                                           " reaches its row"
                                         : "[P038] GameWorld::destroy AddHook FAILED - the crash"
                                           " class from T086/T087/T088 is NOT closed, and the only"
                                           " protection left is the sampled check that already"
                                           " failed to prevent three crashes");
    }
    else ErrorLog("[P038] GameWorld::destroy resolved to 0 - NOT HOOKED. Dead characters will only"
                  " be withdrawn by P034's sampled check, which has a window and has already been"
                  " crashed through three times.");

    if (cb != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)cb, (void*)&detour_createCharacterForBuilding,
                                                    (void**)&orig_createCharacterForBuilding);
        DebugLog(s == coop::SUCCESS ? "[P032] createCharacterForBuilding AddHook SUCCESS"
                                         : "[P032] createCharacterForBuilding AddHook FAILED");
    }
    else ErrorLog("[P032] createCharacterForBuilding resolved to 0");

    if (pb != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)pb, (void*)&detour_populateBuilding,
                                                    (void**)&orig_populateBuilding);
        DebugLog(s == coop::SUCCESS ? "[P032] populateBuilding AddHook SUCCESS"
                                         : "[P032] populateBuilding AddHook FAILED");
    }
    else ErrorLog("[P032] populateBuilding resolved to 0");

    if (rt != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)rt, (void*)&detour_checkForRepopulateTown,
                                                    (void**)&orig_checkForRepopulateTown);
        DebugLog(s == coop::SUCCESS ? "[P032] checkForRepopulateTown AddHook SUCCESS"
                                         : "[P032] checkForRepopulateTown AddHook FAILED");
    }
    else ErrorLog("[P032] checkForRepopulateTown resolved to 0");

    P072Install((unsigned long long)imgBase);   /* PROBE P072 */
}

} // namespace coop

// PROBE-START: P072 (T263 runtime furniture; REMOVE once the runtime-furniture question is answered -
// .modding/04-probes.md). Log only: both detours call the original unchanged. Building::setupLevelData
// 0x5516B0 (arg3 = the resident squad GameData, arg4 = its 'layout interior', arg5 = its 'layout exterior' -
// Town::_setMainResident 0x92D8C2..0x92D933) and the interior load switch 0x561750 (rcx = Building+0x1F0,
// dl = on; on=1 loads the layout named at interior+0x98 only while interior+0x10 is 0). The zone-loading
// thread may call both, so a detour only copies bytes into a fixed lock-free ring (no allocation, no log,
// no lock); P072Tick drains it on the main thread. 400 lines per run.
namespace {
unsigned long long kP072SetupRva = 0; static coop::AddrReg kP072SetupRva_reg("Building_prepareLevel", &kP072SetupRva);   /* Steam_1.0.65 0x5516B0 */
unsigned long long kP072LoadRva  = 0; static coop::AddrReg kP072LoadRva_reg("BuildingInterior_toggleLoaded", &kP072LoadRva);   /* Steam_1.0.65 0x561750 */

typedef void (*P072SetupFn)(void* bld, void* faction, void* squad, const void* layoutInt, const void* layoutExt);
typedef void (*P072LoadFn)(void* interior, bool on);
P072SetupFn orig_p072Setup = 0;
P072LoadFn  orig_p072Load  = 0;

const int kP072Ring = 128;          /* power of two */
const int kP072Cap  = 400;
const int kP072Str  = 64;
const int kP072Key  = 64;           /* kBoxKeyCap is 48 */
const int kP072Map  = 2048;         /* interior -> building, filled by the setup detour (power of two) */
struct P072Rec
{
    volatile LONG seq;
    int kind;                        /* 1 setup, 2 load */
    int on, already, mapped, haveKey, havePos;
    unsigned long tid;
    float px, pz;
    void* bld;
    void* interior;
    char key[kP072Key];
    char a[kP072Str];                /* setup: arg4 'layout interior'; load: interior+0x98 */
    char b[kP072Str];                /* setup: arg5 'layout exterior' */
    char squad[kP072Str];            /* setup: arg3 GameData stringID (+0x58) */
};
P072Rec g_p072[kP072Ring];
volatile LONG g_p072Head = 0;
LONG g_p072Tail = 0;                 /* main thread only */
volatile LONG g_p072Reserved = 0;    /* lines reserved at record time, so the cap is exact */
volatile LONG64 g_p072Setup = 0, g_p072Load = 0, g_p072Dropped = 0, g_p072Unmapped = 0;
volatile LONG g_p072Ready = 0;
void* volatile g_p072MapI[kP072Map];
void* volatile g_p072MapB[kP072Map];

int P072ReadStr(const void* s, char* out, int cap)   /* MSVC std::string: buf/ptr +0, size +0x10, res +0x18 */
{
    out[0] = 0;
    if (s == 0) return -1;
    int n = -1;
    __try
    {
        const unsigned long long sz  = *(const unsigned long long*)((const char*)s + 0x10);
        const unsigned long long res = *(const unsigned long long*)((const char*)s + 0x18);
        if (sz <= 4096 && res >= sz)
        {
            const char* p = (res >= 16) ? *(const char* const*)s : (const char*)s;
            const int k = ((int)sz < cap - 1) ? (int)sz : cap - 1;
            for (int i = 0; i < k; ++i)
            {
                const char c = p[i];
                out[i] = (c >= 32 && c < 127 && c != '\'') ? c : '?';
            }
            out[k] = 0;
            n = (int)sz;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; n = -1; }
    return n;
}
int P072ReadPos(void* bld, float* x, float* z)       /* the plain +0x48/+0x50 field the key's mode 1 reads */
{
    int ok = 0;
    __try { *x = *(const float*)((const char*)bld + 0x48); *z = *(const float*)((const char*)bld + 0x50); ok = 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    return ok;
}
void* P072ReadPtr(void* base, int off)
{
    void* v = 0;
    __try { v = *(void* const*)((const char*)base + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}
unsigned int P072Hash(void* p)
{
    unsigned long long v = (unsigned long long)p;
    v ^= v >> 17;
    v *= 0x9E3779B1ull;
    return (unsigned int)(v >> 7) & (unsigned int)(kP072Map - 1);
}
void P072MapPut(void* interior, void* bld)
{
    if (interior == 0) return;
    unsigned int h = P072Hash(interior);
    for (int i = 0; i < 16; ++i, h = (h + 1) & (unsigned int)(kP072Map - 1))
    {
        void* cur = g_p072MapI[h];
        if (cur == 0) cur = InterlockedCompareExchangePointer((PVOID volatile*)&g_p072MapI[h], interior, 0);
        if (cur == 0 || cur == interior)
        {
            InterlockedExchangePointer((PVOID volatile*)&g_p072MapB[h], bld);
            return;
        }
    }
}
void* P072MapGet(void* interior)
{
    unsigned int h = P072Hash(interior);
    for (int i = 0; i < 16; ++i, h = (h + 1) & (unsigned int)(kP072Map - 1))
    {
        void* cur = g_p072MapI[h];
        if (cur == interior) return g_p072MapB[h];
        if (cur == 0) return 0;
    }
    return 0;
}
/* A bounded multi-producer ring (per-slot sequence): a producer owns slot pos once its CAS moves the head;
   the main thread reads a slot only after its seq reads pos + 1. Full = dropped, never overwritten. */
P072Rec* P072Claim(LONG* posOut)
{
    if (g_p072Ready == 0 || g_p072Reserved >= kP072Cap) return 0;
    if (InterlockedIncrement(&g_p072Reserved) > kP072Cap) return 0;
    for (;;)
    {
        const LONG pos = g_p072Head;
        P072Rec* r = &g_p072[pos & (kP072Ring - 1)];
        const LONG seq = r->seq;
        if (seq == pos)
        {
            if (InterlockedCompareExchange(&g_p072Head, pos + 1, pos) == pos) { *posOut = pos; return r; }
        }
        else if (seq - pos < 0)
        {
            InterlockedIncrement64(&g_p072Dropped);
            InterlockedDecrement(&g_p072Reserved);
            return 0;
        }
    }
}
void P072Fill(P072Rec* r, int kind, void* bld, void* interior)
{
    r->kind = kind;
    r->bld = bld;
    r->interior = interior;
    r->tid = ::GetCurrentThreadId();
    r->key[0] = 0; r->a[0] = 0; r->b[0] = 0; r->squad[0] = 0;
    r->on = 0; r->already = 0; r->mapped = 0; r->px = 0; r->pz = 0;
    /* ObjectPositionKey: ANY THREAD, no allocation (items.h). countIt 3 books nothing; podPos 1 reads the
       plain +0x48 field (same bytes as the virtual mode, by construction). */
    r->haveKey = (bld != 0) ? coop::ObjectPositionKey(bld, r->key, kP072Key, "", 3, 1) : 0;
    r->havePos = (bld != 0) ? P072ReadPos(bld, &r->px, &r->pz) : 0;
}
void detour_p072Setup(void* bld, void* faction, void* squad, const void* layoutInt, const void* layoutExt)
{
    InterlockedIncrement64(&g_p072Setup);
    LONG pos = 0;
    P072Rec* r = P072Claim(&pos);
    if (r != 0)
    {
        P072Fill(r, 1, bld, 0);
        P072ReadStr(layoutInt, r->a, kP072Str);
        P072ReadStr(layoutExt, r->b, kP072Str);
        if (squad != 0) P072ReadStr((const char*)squad + 0x58, r->squad, kP072Str);
    }
    orig_p072Setup(bld, faction, squad, layoutInt, layoutExt);
    void* interior = (bld != 0) ? P072ReadPtr(bld, 0x1F0) : 0;
    P072MapPut(interior, bld);
    if (r != 0)
    {
        r->interior = interior;
        MemoryBarrier();
        InterlockedExchange(&r->seq, pos + 1);
    }
}
void detour_p072Load(void* interior, bool on)
{
    InterlockedIncrement64(&g_p072Load);
    LONG pos = 0;
    P072Rec* r = P072Claim(&pos);
    if (r != 0)
    {
        void* bld = (interior != 0) ? P072MapGet(interior) : 0;
        const int mapped = (bld != 0 && P072ReadPtr(bld, 0x1F0) == interior) ? 1 : 0;
        if (mapped == 0) { bld = 0; InterlockedIncrement64(&g_p072Unmapped); }
        P072Fill(r, 2, bld, interior);
        r->mapped = mapped;
        r->on = on ? 1 : 0;
        r->already = (interior != 0 && P072ReadPtr(interior, 0x10) != 0) ? 1 : 0;
        if (interior != 0) P072ReadStr((const char*)interior + 0x98, r->a, kP072Str);
        MemoryBarrier();
        InterlockedExchange(&r->seq, pos + 1);
    }
    orig_p072Load(interior, on);
}
std::string P072Zone(const P072Rec* r)
{
    if (r->havePos == 0) return "?";
    const coop::Sector s = coop::SectorOf(r->px, r->pz);
    return S((long long)s.x) + "," + S((long long)s.y);
}
} // namespace

namespace coop {
void P072Install(unsigned long long imgBase)
{
    for (int i = 0; i < kP072Ring; ++i) g_p072[i].seq = i;
    InterlockedExchange(&g_p072Ready, 1);
    if (kP072SetupRva != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)(imgBase + (uintptr_t)kP072SetupRva),
                                                    (void*)&detour_p072Setup, (void**)&orig_p072Setup);
        DebugLog(s == coop::SUCCESS ? "[P072] setupLevelData AddHook SUCCESS"
                                         : "[P072] setupLevelData AddHook FAILED");
    }
    else ErrorLog("[P072] Building_prepareLevel not in the address table - NOT HOOKED");
    if (kP072LoadRva != 0)
    {
        coop::HookStatus s = coop::AddHook((void*)(imgBase + (uintptr_t)kP072LoadRva),
                                                    (void*)&detour_p072Load, (void**)&orig_p072Load);
        DebugLog(s == coop::SUCCESS ? "[P072] interior load switch AddHook SUCCESS"
                                         : "[P072] interior load switch AddHook FAILED");
    }
    else ErrorLog("[P072] BuildingInterior_toggleLoaded not in the address table - NOT HOOKED");
}
void P072Tick()
{
    if (g_p072Ready == 0) return;
    const unsigned long mainTid = StoreMainThreadId();
    for (int n = 0; n < 64; ++n)
    {
        P072Rec* r = &g_p072[g_p072Tail & (kP072Ring - 1)];
        if (r->seq != g_p072Tail + 1) break;
        MemoryBarrier();
        const std::string thr = std::string(mainTid == 0 ? "unknown" : (r->tid == mainTid ? "main" : "other"))
                              + "(" + S((long long)r->tid) + ")";
        const std::string key = (r->haveKey != 0) ? std::string(r->key) : std::string("-");
        std::string line;
        if (r->kind == 1)
            line = "[P072] setup key=" + key + " interior='" + r->a + "' exterior='" + r->b + "' zone=" + P072Zone(r)
                 + " thread=" + thr + " squad='" + r->squad + "' bld=" + H((unsigned long long)r->bld)
                 + " int=" + H((unsigned long long)r->interior);
        else
            line = "[P072] load key=" + key + " on=" + S((long long)r->on) + " zone=" + P072Zone(r)
                 + " thread=" + thr + " layout='" + r->a + "' already=" + S((long long)r->already)
                 + " mapped=" + S((long long)r->mapped) + " int=" + H((unsigned long long)r->interior);
        DebugLog(line);
        InterlockedExchange(&r->seq, g_p072Tail + kP072Ring);
        ++g_p072Tail;
    }
}
std::string P072ReportToken()
{
    const LONG lines = (g_p072Reserved < kP072Cap) ? g_p072Reserved : kP072Cap;
    return " | P072 p072Setup=" + S((long long)g_p072Setup) + " p072Load=" + S((long long)g_p072Load)
         + " p072Dropped=" + S((long long)g_p072Dropped) + " p072Unmapped=" + S((long long)g_p072Unmapped)
         + " p072Lines=" + S((long long)lines);
}
} // namespace coop
// PROBE-END: P072
