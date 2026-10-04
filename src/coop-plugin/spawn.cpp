// spawn.cpp - M1 SPAWN.
//
// The engine's own creation path, found offline (F072):
//   RootObjectFactory::create(data, pos, isFromActiveLevelMod, faction, rot, callback,
//                             certainContainer, saveState, invisible, home, age)
// It is SYNCHRONOUS for characters (builds a CreatelistItem and calls process()
// immediately, returning the object), so it can be called straight from the main-thread
// pump - no deferred queue to wait on.
//
// M1 scope (F072, deliberate): spawn BY TEMPLATE. Both installs load identical static
// game data, so a template name resolves to the same content on both sides; what travels
// is {uid, template name, world position}. Transferring serialised state records is a
// serialization problem that belongs to M4/STATE, not to proving the spawn path.
//
// Everything here runs on the MAIN THREAD (called from the command channel / session
// pump). Every engine pointer is validated before use - the same rule that has kept
// every prior spike from turning a wrong offset into a crash (F051).

#include "handoff.h"   /* T-1 B1: SquadActivePlatoonOf - the verified squad reader */
#include "../common/squadwriter.h"   /* M7a3f3 T-425 [m7a3f3-sc0]: SupersedeOwnIdAfterHandover */
#include "spawn.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "playerfaction.h"   // P3: ResolveWireFaction (decision 23)
#include "identity.h"
#include "worldsync.h"
#include "store.h"   // P1: StoreBindPlatoon
namespace coop { void LimbsForgetUid(unsigned int uid); void LimbsWorldTeardown(); }   /* LIMBS: clothing.cpp */
#include "worldgen.h"   // F322: DestroyLocalObject   // F318: TrackForLiveness - P034 must cover BOTH instances   // P031: the roster is keyed by the engine's own handle
#include "appearance.h"
#include "replicate.h"   // F152: ApplyRemoteSpawn now adopts the peer copy as a puppet
#include "ai_spike.h"
#include "soak.h"
#include "medical.h"
#include "stats.h"          // S1: ApplyRemoteStats at the end of both ApplyRemoteSpawn branches
#include "../common/prisonwire.h"   /* arrest2: MSG_PRISON */
#include "../common/treatwire.h"    /* heal1: MSG_TREAT */
#include "../common/namewire.h"     /* names1: MSG_NAME */
#include "../common/uidtable.h"     /* mirror1 (crash T487): the uid table's decisions, offline-tested */
#include "../common/uidlayout.h"    /* M4 (owner 203): a uid is (the making game's slot << 22) | counter */
#include "../common/uidblock.h"     /* M4 fold (store protocol 58): the notebook's uid counter blocks - UID_BLOCK kinds */
#include "../common/ownerroute.h"   /* M7b fold 1: AddrPrisonRefusesEarlier */
#include "../common/slavewire.h"    /* slave1: MSG_SLAVE */
#include "../common/talkwire.h"     /* a conversation's carried-person hand-over: TalkHandOverLetsGo */
#include "../common/saywire.h"      /* capturetest carry name: CarryNameParse */
#include "../common/joblever.h"     /* cagetest: CageTestParse */
#include "speech.h"                 /* capturetest carry name: LeverNearestNamed */
#include "../common/ragdollrest.h"   // R3 (read-ragdoll): the pending rest moves the 0x7D38D0 detour takes
#include "../common/getupcrawl.h"    // T-178 crawl1 (H050): owner up from the ragdoll / copy ready / MOVE drives prone 2
#include "../common/sneakwire.h"     // the owner's stealth mode in STATE bit 7, and what the copy's game does with it
#include "../common/kolook.h"        // the one bound on a copy's first look, and when the knockdown hold is live
#include "../common/bedmatch.h"   /* BED1: which interior piece a bed key names */
#include "../common/standinpurge.h"   /* inv7a: what happens to one stand-in character a loaded world carries */
#include "../common/orphanpurge.h"    /* orphan1: what happens to one character created here in another game's area */
#include "net/session.h"
#include "zones.h"          // P8a: SectorOf / SectorString for the roster's own sector field
#include "../common/removalreason.h"   /* M7a3: an engine unload is never sent as a death - the reason classification, offline-tested */
#include "../common/peergone.h"   /* PlayerGoneTakesRow: a player who leaves takes only the rows kept for that player */
#include "../common/ctxkey.h"     /* the context map's squad-id key: the id and the faction the squad was built under */
#include "towngen.h"        // P8a: TownGenMadeThisPlatoon - the roster's spawnCause=towngen
#include "tags.h"           // tags1: a label per copy of the other player's characters
#include "items.h"          // POSE (read-poses): ObjectPositionKey / ObjectByPositionKey - the bed's building key

#include "coop_log.h"
#include "game/Character.h"
#include "hooks.h"   // R3: coop::AddHook (own MinHook) for the rescue hook 0x7D38D0
#include "game/CharMovement.h"   // twin placement at adoption
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/RootObjectFactory.h"
#include "game/AITaskSystem.h"
#include "game/Faction.h"
#include "game/Platoon.h"   // P1b: Faction::removeSquad(Platoon*)
#include "game/hand.h"
// M-B: TownList::getTownBySID and HandleManager::asBuilding are called by resolver-verified RVA (Town.h /
// HandleManager.h drag in Ogre math headers that are not on this build's include path).
#include <ogre/OgreVector3.h>
#include <ogre/OgreQuaternion.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <map>
#include <set>
#include <vector>
#include <utility>
#include <sstream>
#include <locale>
#include <cctype>
#include <cmath>

// P029 / F231 - declared at GLOBAL SCOPE, as the game's own classes are.
//
// `CombatClassAI::decisionState` (real RVA 0x60BE20) opens with:
//     if (!animation->isActionAnimating()) { ...choose the next move... }
// so the AI's combat decision is gated on the ANIMATION LAYER BEING FREE. A character whose
// animation layer reports "still playing an action" never decides - which is exactly the
// oscillation with a pinned `nextMove` that T068 and T069 measured on the peer, and a driven
// puppet is pushed with steerDirectly every frame, which is a plausible way to keep an
// action animation permanently alive.
//
// The address is from `resolve_stub.py`, NOT from header arithmetic: slot 781 -> 0x5B2A20.
// (I first derived it by subtracting the usual 0x310 from a header RVA, which is exactly the
// habit F037 exists to prevent, and then checked it properly.)
//
// The base subobject sits at offset 0 - already established, because `isIdle` at
// AnimationClassBase+0xA5 reads correctly through this same pointer.
class AnimationClassBase
{
public:
    bool  isActionAnimating();
    float actionBlendWeight();
};

namespace coop {

/* T-304 (3): the stand-in reference for building the other game's characters when this game's watched player is gone -
   defined beside IsPlayerFactionPod, declared here at coop scope so the anonymous namespace below sees THIS function (C2668). */
::Character* SpawnFallbackReference();
long long g_spawnRefFallback = 0, g_spawnBuiltNoRef = 0, g_spawnRefusedNoRef = 0;   /* T-304 (3): [M1] REPORT */

namespace {

// uid -> the object we created for it. The authority map (who OWNS a uid) lives in the
// session layer (commitment 1); this is only "which local object is uid N", which is a
// different question and may legitimately live with the spawner.
std::map<unsigned int, RootObjectBase*> g_spawned;
std::set<unsigned int> g_twinUids;   // H027 twins: THIS engine's own save-loaded characters (H030: never destroyed by UNLOAD)

// Locally-originated uids.
//
// The previous scheme (host mints odd, client mints even) seeded itself from the session
// role at the FIRST spawn - a timing race, and T019 lost it: stage 1 deliberately spawns
// before hosting, so the host took client parity, then the real client minted the same
// uid and the host discarded the incoming spawn as a duplicate.
//
// Fixed by removing the ordering dependency entirely rather than moving the seed: the
// high 8 bits are a per-PROCESS prefix, derived at first use from the process id, so two
// instances mint disjoint uid ranges no matter who hosts, who joins, or what happens
// first. Nothing about allocation now depends on session state or call order.
// Explicit spawn faction (F093/F101). Empty = inherit the reference character's faction,
// which is what made every spawn a permanent squadmate and left ordered attacks unplannable.
std::string g_spawnFactionName;

// F106: whether an explicit-faction spawn also keeps the reference character's platoon as its
// container. Only consulted when a faction is set explicitly - an inherited-faction spawn has
// always kept the platoon and is measured to stay put. Default TRUE, i.e. the OPPOSITE of the
// original F103 behaviour, because null is the variant T029 measured running away.
bool g_spawnKeepContainer = true;

coopuid::UidMinter g_uidMint;   // M4 (owner 203, 2026-09-29): the 22-bit counter (last minted; monotonic, never wraps) and the counted refusals; the slot is read at each mint
long long g_uidRowSkipped = 0, g_uidBlockBadReply = 0, g_uidBlockFailed = 0, g_uidNoSeatAnswers = 0; bool g_uidNoSeatSaid = false;   // M4 fold: minted uids skipped for an existing row (must read 0); UID_BLOCK answers malformed / for another slot; FAILED answers
unsigned int g_uidLastSlot = 0xFFFFFFFFu;   // M4: the slot of the last uid minted - a change is logged once as an [M4] line
DWORD g_uidFirstMintMs = 0;   // T-197 M4 (owner 156): GetTickCount (0 = none yet; 1 if it read 0) at this process's first mint - the report's mint rate counts from it

// Flat mirror of g_spawned for the reverse (object -> uid) lookup used by the combat
// detour. A std::map read is NOT safe from an arbitrary game thread while the main thread
// may be inserting (rebalancing moves nodes), so the detour walks this fixed array of
// plain values instead: writes are single aligned stores from the main thread, and a
// reader sees either the old or the new value, never a torn one.
// F267 blocker 1: this was 64, against P-16 Option A's measured requirement of ~100-130 world
// characters (T078/F265). Past the cap `MirrorAdd` logs `[M1] uid mirror full` and the character
// is simply absent from the reverse lookup - `FindSpawnedUid` returns 0, which is
// INDISTINGUISHABLE from "not one of ours". Combat, hit and medical attribution would have been
// correct for the first 64 characters and silently wrong for the rest: a symptom that behaves like
// a subtle logic bug and would have been very expensive to trace from in-game behaviour.
//
// 512 rather than 130: the array is 12 KB at this size, iteration is bounded and cheap now that
// lookup no longer uses it (below), and a limit that has to be revisited every time the scope grows
// is a limit that will be hit again while nobody is looking.
// mirror1 (crash T487): 2048. The project goal is up to 100 players (build/design-many.md M12, whose
// guess is 2,048 rows); a row is 24 bytes, so the array is 48 KB and the index below 192 KB. Every
// table sized against this one moved with it (combat kMaxCombatWatch/kUpdSlots, soak kMaxWatch,
// ai_spike kMaxGated, replicate kMaxSentSamples, worldsync kMaxAdopted). 512 was reached in T487 by
// TWO players, because rows were never freed - see MirrorRelease; the size alone was not the defect.
// T-354 (M12 audit 2026-09-30): sized for 256 connected players - 4096 rows (96 KB), the index 16384 slots (384 KB). The
// number and why it is fixed (the lock-free index cannot grow under a reader) are in src/common/uidtable.h G, where the
// offline suite fills it. Every table sized against this one moved with it again (combat kMaxCombatWatch/kUpdSlots, soak
// kMaxWatch, ai_spike kMaxGated, replicate kMaxSentSamples, worldsync kMaxAdopted).
const int kMaxMirror = coopuid::kMirrorRows;
// `dead` retires an entry WITHOUT removing it: a streamed-out character keeps its row and comes back
// through MirrorRestore with its uid. It is a separate aligned LONG, so a reader sees either value and
// never a torn one. mirror1: a row IS released (MirrorRelease) once its character is finished -
// despawn applied, or `destroyed` confirmed by the periodic reclaim - and the index leaves a TOMBSTONE,
// never a hole, so the lock-free probe stays correct (src/common/uidtable.h states why).
//
// T083 is why this exists. The authority adopted 136 world characters, the engine destroyed 21 of
// them, P034 DETECTED every one - and nothing acted on it, so `[M5] drift`, the streamer and the
// combat watch went on dereferencing freed objects. One read came back `prone=1065353216`
// (0x3F800000, the bit pattern of 1.0f) with `cc=nobody` and position floats appearing in health
// fields, and the instance crashed 11 seconds later. **Detection without retirement is not a
// safety feature.**
// F328 - `dead` and `destroyed` are NOT the same fact, and conflating them resurrected a uid the
// peer had already been told to remove.
//
//   dead      = withdrawn FOR NOW. P034's handle check failed, which usually means the character
//               streamed out with its region. Reversible by design (F284) - it comes back.
//   destroyed = the ENGINE told us it destroyed this object (P038). **Permanent.** The address may
//               be handed to a completely different character later, and that character is not the
//               one we retired.
//
// T091: the host kept streaming MOVE for two uids the client had already destroyed - 219 messages
// for one of them. `MirrorAdd` un-retires any dead slot whose address matches and **keeps the
// original uid** (F296, written for the streaming case and correct there); when the engine recycles
// the address of a genuinely destroyed character, that resurrects a uid whose copy is gone from the
// peer forever. Until P038 existed there was no way to tell the two cases apart.
struct UidMirror { const void* obj; unsigned int uid; volatile LONG dead; volatile LONG destroyed; };
UidMirror g_mirror[kMaxMirror] = {0};
// F328: address recycled after a real destruction, and re-registered under a NEW uid.
long long g_mirrorRebound = 0;
// F328: a caller asked to un-retire in place and KEEP the old uid, on an address the engine had
// destroyed. Refused - the old uid's copy is gone from the peer and must not be resurrected.
long long g_mirrorRebindRefused = 0;

// F267 blocker 2: the reverse lookup used to be a LINEAR SCAN of the array above, and it runs on
// the hot path off the main thread - the combat-tick detour (22,625 calls in one T066 run), the hit
// detour, and the medical detour. It scans to the END on every MISS, and a world character that is
// not ours is always a miss. Raising the array to 512 would have made the common case eight times
// worse, on a worker thread.
//
// So lookup moves to an open-addressed hash index. The flat array stays, because MirrorSlot/
// MirrorCapacity/MirrorUsed iterate it by index and that is a different access pattern.
//
// WHY THIS IS SAFE OFF-THREAD, and it is the same reasoning that chose a flat array over a
// std::map in the first place: there is no rehashing and no node movement, and there is ONE writer
// (the main thread). A writer publishes `uid` (and cleared flags) first and then the pointer with an
// interlocked store, so a reader either sees an empty slot or a completely-written one - never a torn
// pair. mirror1 (crash T487): entries ARE removed now (MirrorRelease). A removed entry becomes a
// TOMBSTONE (key 1, never an address), which does not end a probe, so no later entry is hidden; a
// tombstone is turned back into empty only when the slot after it is already empty (it cannot then
// sit inside any entry's run), and an insert reuses the first tombstone of its run. The rules are
// coopuid::IxFind / IxInsertAt / IxTrimmable, exercised by the offline suite.
//
// The one transient: a reader probing during an insert can stop at a slot that is about to be
// filled and report a miss. That window already existed with the linear scan and has the same
// consequence (one tick of "not ours"), so it is not a new failure mode.
//
// Power of two, and sized for a load factor under 0.25 at full capacity so probe chains stay short.
const int kIndexSlots = coopuid::kMirrorIndexSlots;   // T-354: 4 x kMaxMirror (16384), so the load stays under 0.25
const int kIndexMask  = kIndexSlots - 1;
UidMirror g_index[kIndexSlots] = {0};
// mirror1: the index's keys, for the coopuid:: probe rules (uidtable.h).
struct IndexKeys { const void* operator()(int i) const { return g_index[i].obj; } };
// mirror1 (crash T487) - the table's occupancy and every refusal and release, in the [M1] REPORT line.
int       g_mirrorUsedNow      = 0;   // rows occupied now (written on the main thread only)
int       g_mirrorHighWater    = 0;   // the most rows ever occupied at once
long long g_mirrorFullRefused  = 0;   // registrations refused: no free row
long long g_mirrorReleased     = 0;   // rows released (despawn applied, stale despawn, reclaim)
long long g_mirrorReclaimed    = 0;   // ...of which by the periodic reclaim of `destroyed` rows
long long g_indexTrimmed       = 0;   // index tombstones turned back into empty slots
long long g_indexOrphanRebound = 0;   // MUST STAY ZERO: the index held an object the array did not
long long g_spawnRefusedFull   = 0;   // a peer copy NOT built because the table could not hold it
long long g_twinRefusedFull    = 0;   // a same-handle twin NOT adopted, same reason
long long g_despawnDroppedStale = 0;  // a DESPAWN with no live registered copy that still found a trace to drop
// T-354: a character of ANOTHER game this table refused is reported to its owner (MSG_NOT_SHOWN, net/session.cpp), and the
// owner counts what it is told. mirrorTestCap is the TEST-ONLY `mirrorcap` lever (0 = off).
int       g_mirrorTestCap        = 0;
long long g_notShownSent         = 0;   // NOT_SHOWN sent to an owner
long long g_notShownSendFailed   = 0;   // ...that found no road (no link up on the road the SPAWN came by)
long long g_notShownIn           = 0;   // owner side: our character is not shown on another game
long long g_notShownInNotMine    = 0;   // owner side: a NOT_SHOWN naming a uid this game does not run (stale) - nothing changes
long long g_notShownInBad        = 0;   // owner side: a NOT_SHOWN that did not decode

inline int IndexHash(const void* obj)
{
    // Objects are heap allocations, so the low 4 bits carry no information. Mix the upper bits
    // down (a Fibonacci-style multiply) so that addresses which differ only in high bits do not
    // collide into one chain.
    return coopuid::IxHome(obj, kIndexMask);   // T-354: the same arithmetic, in uidtable.h for the offline suite
}

bool PendingCtxHasUid(unsigned int uid);   // M4 fold: defined beside g_pendingCtx below, in this same (anonymous) namespace

// M4 fold L2 (review 2026-09-29): a uid is minted only while THIS link's WELCOME stands (StoreWelcomedThisLink) and numbered this
// game. The SEAT it mints under is the one the notebook's last GRANT on this link named (owner 205 A); the seat and the blocks
// are dropped at link-down (store.cpp -> SpawnUidBlocksForget).
bool UidLinkedForMint() { return coop::StoreWelcomedThisLink() != 0 && coop::StoreMySlot() >= 0; }

// One rate-limited line for a refused mint (the first 5 and every 100th), whichever path counted it.
void UidRefusalLog()
{
    const long long refused = g_uidMint.noSlotDeferred + g_uidMint.counterExhausted + g_uidMint.noBlockDeferred + g_uidMint.staleBlockRefused;
    if (coopuid::LogRefusal(refused))
    {
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "[M4] uid NOT minted: " << coopuid::UidMintWords(g_uidMint.last)
           << ". The caller retries later and never uses uid 0 (" << refused
           << " refusals so far; logged for the first 5 and every 100th).";
        ErrorLog(ss.str());
    }
}

unsigned int AllocateUid()
{
    // M4 (owner 203) / OWNER DECISION 205 A (2026-09-29): a uid is (the MAKING game's 10-bit SEAT << 22) | a 22-bit counter
    // (src/common/uidlayout.h). The seat is the one this game holds among the games connected to the notebook now - the
    // notebook's GRANT names it - not the permanent profile slot. The uid is ONLY A NAME: who may write the character is the
    // hand-over record (net/session.cpp), never UidSlot(uid).
    // M4 FOLD (review 2026-09-29 H1): the counter is minted only inside blocks the notebook granted for that seat on this link
    // (store.cpp UidBlockTick asks; src/common/uidblock.h), and the notebook never grants a counter of a seat twice in the
    // world's life - so a restarted game, or a new player on a freed seat, never repeats a uid another game still holds.
    // 0 = NOT MINTED, counted in g_uidMint (uidNoSlotDeferred / uidNoBlockDeferred / uidCounterExhausted on the [M1] REPORT
    // line); every caller treats 0 as "not now" and never uses it. Counter 0 is never granted.
    // DEFENCE IN DEPTH: a minted uid that already has a row here (g_spawned, a shared-save twin, the session's owner records,
    // a pending context) is skipped and counted (uidRowSkipped - must read 0), never used.
    const bool linked = UidLinkedForMint();
    for (int tries = 0; tries < 64; ++tries)
    {
        const unsigned int uid = coopuid::UidMintOnLink(&g_uidMint, linked, coop::StoreNotebookLinkGen());   /* M4 fold 2: blocks of an earlier link refuse (kUidStaleBlock) */
        if (uid == 0) { UidRefusalLog(); return 0; }
        if (g_spawned.find(uid) != g_spawned.end() || g_twinUids.find(uid) != g_twinUids.end()
            || net::UidHasSessionRow(uid) || PendingCtxHasUid(uid))
        {
            ++g_uidRowSkipped;
            if (coopuid::LogRefusal(g_uidRowSkipped))
            {
                std::stringstream ss;
                ss.imbue(std::locale::classic());
                ss << "[M4] minted uid " << uid << " (seat " << coopuid::UidSlot(uid) << ", counter " << coopuid::UidCounter(uid)
                   << ") ALREADY HAS A ROW in this game - skipped, never used (uidRowSkipped=" << g_uidRowSkipped
                   << "; logged for the first 5 and every 100th). The notebook's seat blocks should make this impossible.";
                ErrorLog(ss.str());
            }
            continue;
        }
        if (g_uidFirstMintMs == 0) { g_uidFirstMintMs = ::GetTickCount(); if (g_uidFirstMintMs == 0) g_uidFirstMintMs = 1; }
        if (coopuid::UidSlot(uid) != g_uidLastSlot)
        {
            g_uidLastSlot = coopuid::UidSlot(uid);
            std::stringstream ss;
            ss.imbue(std::locale::classic());
            ss << "[M4] minting uids for seat " << g_uidLastSlot << ": uid " << uid << " (0x" << std::hex
               << std::uppercase << uid << std::dec << ", counter " << coopuid::UidCounter(uid) << ", block " << g_uidMint.curLo
               << ".." << g_uidMint.curHi << "); this seat's uids lie in "
               << (g_uidLastSlot << 22) << ".." << ((g_uidLastSlot << 22) | coopuid::UidCounterMax);
            DebugLog(ss.str());
        }
        return uid;
    }
    ErrorLog("[M4] 64 minted uids in a row already had rows in this game - none used on this call; the caller retries later");
    return 0;
}

std::string S(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

std::string F1(float v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << v;
    return ss.str();
}

std::string P(const void* p)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << std::uppercase << (unsigned long long)p;
    return ss.str();
}

// TWO tiers of pointer guard, because the vtable test only applies to POLYMORPHIC types.
// T018 stage 1 failed on exactly this: `RootObjectFactory` has zero virtual members (its
// field at offset 0 is a `boost::shared_mutex`), so demanding a vtable there could only
// pass by coincidence — the guard, not the engine, refused the spawn.
//
// Rule for callers: use PlausibleObject() only where the header shows `virtual` members;
// use PlausiblePtr() otherwise. Verified polymorphic: GameData (2), Faction (6),
// RootObjectContainer/ActivePlatoon (64). Verified NON-polymorphic: RootObjectFactory (0).

// Range + alignment + first qword is readable. The read is what distinguishes a live
// allocation from an arbitrary in-range integer.
bool PlausiblePtr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Stricter: also requires a vtable pointer into the game image. POLYMORPHIC TYPES ONLY.
bool PlausibleObject(const void* p)
{
    if (!PlausiblePtr(p)) return false;
    uintptr_t vtable = 0;
    __try { vtable = *(uintptr_t*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

// THE TWO HALVES MUST SUCCEED OR FAIL TOGETHER. An adversarial review found that they did not,
// and the consequences were worse than either failing alone:
//
//   * The index was written FIRST and UNCONDITIONALLY, so its occupancy had no relationship to
//     the array's. Past kMaxMirror a character would be indexed but absent from the array -
//     attributable, yet never streamed, never combat-watched and never soak-sampled. Silent
//     partial replication.
//   * The "HASH INDEX full" warning was nested inside the ARRAY-SUCCESS branch, so in the one
//     case that matters most - both full - it could not be reached at all.
//   * The array half had no duplicate check, so a second MirrorAdd for the same pointer left the
//     index holding the OLD uid and the array holding a SECOND row under a NEW one. That is a
//     permanently inconsistent pair, and it makes the peer build a clone that shadows the original.
//
// So: find the array slot first, and only commit to the index once the array is known to have
// room. A character that cannot be fully registered is not half-registered.
volatile LONG g_uidTableFullSeen = 0;   /* area2 fold: a uid mirror / hash index FULL refusal happened - the sweep does not ask the purge while it is set. mirror1 fold (review-mirror1 #3): NOT sticky - rows are released since mirror1, so MirrorRelease and the world teardown clear it */
// mirror1 fold (review-mirror1 #5) - THE ROW LOCK. RetireOnDestroy runs on a thread we do not control, and its "this row
// names obj -> mark it destroyed" was two steps: MirrorRelease + MirrorAdd (main thread) could free and reuse the row
// between them, and the marks then landed on the NEW occupant, which the reclaim released and dropped a tick later. A
// re-check-and-undo can clobber the new occupant's own marks, and a CAS on a generation still leaves the flag writes
// after it; the simplest correct answer is that RetireOnDestroy's check-then-mark and the two writers that free a row
// (MirrorRelease, SpawnWorldTeardown) hold this spin lock. Every holder does a bounded scan with no call out (no log, no
// engine call), so a waiter spins for microseconds. Readers take no lock. MirrorAdd needs none: it fills only FREE rows
// (RetireOnDestroy never matches one), and it clears a row's flags before publishing the pointer.
volatile LONG g_mirrorRowLock = 0;
void MirrorRowLock()   { while (::InterlockedCompareExchange(&g_mirrorRowLock, 1, 0) != 0) YieldProcessor(); }
void MirrorRowUnlock() { ::InterlockedExchange(&g_mirrorRowLock, 0); }
// mirror1 fold (review-mirror1 #6): why the last MirrorAdd refused, so each caller's refusal line says which.
int       g_mirrorLastRefusal     = 0;   // 1 no free row, 2 hash index full, 3 destroyed address asked to keep its uid, 4 row of another uid
long long g_mirrorOtherUidRefused = 0;   // #6: the address was registered under ANOTHER uid - refused, no longer "already registered"
long long g_soakWatchFreed        = 0;   // #1: soak transition-watch rows freed with their uid's mirror row
long long g_gateFreedWithRow      = 0;   // #2: AI gate slots released with their character's mirror row
long long g_adoptRefusedTables    = 0;   // #12: AdoptExisting refusals (the rate limit's own counter)
const char* MirrorRefusalWhy()
{
    switch (g_mirrorLastRefusal)
    {
    case 1:  return "no free row - the uid mirror is full";
    case 2:  return "the uid hash index is full";
    case 3:  return "the address was destroyed and may not keep its old uid";
    case 4:  return "the address is registered under ANOTHER uid";
    default: return "reason not recorded";
    }
}
// mirror1 (crash T487): RETURNS WHETHER THE CHARACTER IS REGISTERED. It used to return nothing, and
// T487's adopter went on to drive 69 characters the table had refused: a puppet with no row is
// invisible to every removal path (DESPAWN, the retired guard), and one of them was driven after its
// memory had become a GameData. Every caller now refuses the character when this answers false.
// T-354: the FULL line names the TEST-ONLY cap when it is the reason.
std::string MirrorCapText()
{
    return g_mirrorTestCap > 0 ? " - TEST CAP " + S((long long)g_mirrorTestCap) + " rows (mirrorcap lever)" : std::string();
}
bool MirrorAdd(unsigned int uid, const void* obj)
{
    // Duplicate check on the ARRAY, which the index already had. FindSpawnedUid answering 0 for an
    // object that is genuinely present (index full, or a transient during insert) is exactly how a
    // caller like AdoptExisting would ask for the same character twice.
    int freeSlot = -1;
    IndexKeys keys;
    for (int i = 0; i < kMaxMirror; ++i)
    {
        if (g_mirror[i].obj == obj)
        {
            // ALREADY REGISTERED - but if it was RETIRED, un-retire it rather than silently doing
            // nothing. A region that streams out and back gives the same character at the same
            // address; without this, MirrorAdd is a no-op, FindSpawnedUid still answers 0 (the slot
            // is dead), AdoptExisting's verify fails, and the sweep reads that as "uid tables full"
            // and SHUTS ITSELF OFF for the session - with a false reason. One stream-out/stream-in
            // was enough to stop world replication permanently.
            // F328 - BUT NOT IF THE ENGINE DESTROYED IT. The paragraph above is about a character
            // that streamed out and came back: same character, same address. A DESTROYED object is
            // gone forever and its address can be handed to a completely different character - and
            // un-retiring then resurrects a uid whose copy the peer has already removed on our own
            // instruction. T091 measured the consequence: 220 MOVE messages for two uids the client
            // had destroyed, 219 of them for one character.
            //
            // The address is REBOUND rather than left dead, so the new occupant is properly
            // registered under its OWN uid. The slot stays occupied, it just belongs to a new
            // generation.
            //
            // uid == 0 means the caller is asking us to un-retire in place and KEEP the old uid
            // (AdoptExisting's came-back path). That is exactly what must not happen here, so it is
            // refused and the caller mints a fresh uid instead.
            // mirror1 fold (review-mirror1 #6): the choice is coopuid::ExistingRowAction (offline-tested). A row of ANOTHER
            // uid is REFUSED - this answered true, so CreateAt / AdoptExistingTwin went on as if the character were
            // registered under their uid while every lookup answered the other one.
            const int act = coopuid::ExistingRowAction(uid, g_mirror[i].uid, g_mirror[i].dead ? 1 : 0, g_mirror[i].destroyed ? 1 : 0);
            if (act == coopuid::kRowRefuseOtherUid) { ++g_mirrorOtherUidRefused; g_mirrorLastRefusal = 4; return false; }
            if (act == coopuid::kRowRefuseRebind)   { ++g_mirrorRebindRefused;  g_mirrorLastRefusal = 3; return false; }
            if (act == coopuid::kRowRebind)
            {
                g_mirror[i].uid = uid;
                InterlockedExchange(&g_mirror[i].destroyed, 0);
                InterlockedExchange(&g_mirror[i].dead, 0);
                const int hs = coopuid::IxFind(keys, kIndexMask, IndexHash(obj), obj);
                if (hs >= 0)
                {
                    UidMirror& sl = g_index[hs];
                    sl.uid = uid;
                    InterlockedExchange(&sl.destroyed, 0);
                    InterlockedExchange(&sl.dead, 0);
                }
                ++g_mirrorRebound;
                return true;
            }
            if (act == coopuid::kRowUnretire)
            {
                InterlockedExchange(&g_mirror[i].dead, 0);
                const int hs = coopuid::IxFind(keys, kIndexMask, IndexHash(obj), obj);
                if (hs >= 0) InterlockedExchange(&g_index[hs].dead, 0);
            }
            return true;
        }
        if (g_mirror[i].obj == 0 && freeSlot < 0) freeSlot = i;
    }

    if (freeSlot >= 0 && coopuid::MirrorCapReached(g_mirrorUsedNow, g_mirrorTestCap)) freeSlot = -1;   // T-354: the TEST-ONLY cap
    if (freeSlot < 0)
    {
        // Loud, and it says what happens NOW. The previous text ended "Nothing further will be
        // adopted", which T487 disproved 69 times: the adopter did not ask. Every caller refuses on
        // `false` since mirror1, so the sentence below is what the code does.
        ::InterlockedExchange(&g_uidTableFullSeen, 1);   /* area2 fold */
        ++g_mirrorFullRefused;
        g_mirrorLastRefusal = 1;   // mirror1 fold (review-mirror1 #6)
        if (coopuid::LogRefusal(g_mirrorFullRefused))
            ErrorLog("[M1] uid mirror FULL (" + S(kMaxMirror) + " rows" + MirrorCapText() + ", " + S(g_mirrorFullRefused)
                     + " refusals so far) - uid " + S(uid) + " was NOT registered, so it is REFUSED:"
                     + " no peer copy is built, an existing character is not adopted, and nothing"
                     + " drives it - it stays with this engine's own AI. Rows free again as characters"
                     + " are despawned or destroyed (mirrorReleased). Logged for the first 5 and every"
                     + " 100th refusal.");
        return false;
    }

    // Index second, and its failure is reported on its own terms. An un-indexed character is the
    // dangerous case: FindSpawnedUid returns 0 forever, so any caller using it as an idempotence
    // guard would retry that character on every pass.
    int existing = -1;
    const int at = coopuid::IxInsertAt(keys, kIndexMask, IndexHash(obj), obj, &existing);
    if (at < 0)
    {
        ::InterlockedExchange(&g_uidTableFullSeen, 1);   /* area2 fold */
        ++g_mirrorFullRefused;
        g_mirrorLastRefusal = 2;   // mirror1 fold (review-mirror1 #6)
        ErrorLog("[M1] uid HASH INDEX FULL (" + S(kIndexSlots) + ") - uid " + S(uid)
                 + " REFUSED, exactly as a full mirror: not built, not adopted, not driven.");
        return false;   // refuse the whole registration rather than leave an un-findable entry
    }
    UidMirror& slot = g_index[at];
    if (existing >= 0)
    {
        // The index held this object with no array row. MirrorRelease clears both together, so this
        // cannot arise; if it does, the entry is rebound to the new uid rather than left answering
        // with an old one, and counted.
        ++g_indexOrphanRebound;
        slot.uid = uid;
        InterlockedExchange(&slot.destroyed, 0);
        InterlockedExchange(&slot.dead, 0);
    }
    else
    {
        // uid and CLEARED FLAGS before the pointer: a reused tombstone still carries the released
        // row's `dead`, and a reader that sees the pointer must see this generation's flags.
        slot.uid = uid;
        InterlockedExchange(&slot.dead, 0);
        InterlockedExchange(&slot.destroyed, 0);
        InterlockedExchangePointer((PVOID volatile*)&slot.obj, (PVOID)obj);
    }

    g_mirror[freeSlot].uid = uid;
    InterlockedExchange(&g_mirror[freeSlot].dead, 0);        // mirror1 fold (review-mirror1 #5): cleared flags before the
    InterlockedExchange(&g_mirror[freeSlot].destroyed, 0);   // pointer, the index's own rule
    InterlockedExchangePointer((PVOID volatile*)&g_mirror[freeSlot].obj, (PVOID)obj);
    if (++g_mirrorUsedNow > g_mirrorHighWater) g_mirrorHighWater = g_mirrorUsedNow;
    return true;
}

// mirror1 (crash T487) A: asked BEFORE a copy is built, so a copy the table cannot hold never exists.
bool MirrorHasRoom()
{
    if (coopuid::MirrorCapReached(g_mirrorUsedNow, g_mirrorTestCap)) return false;   // T-354: the TEST-ONLY cap
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == 0) return true;
    return false;
}

// mirror1 (crash T487) C - RELEASE THE ROW OF A FINISHED CHARACTER. MAIN THREAD (the one writer).
//
// Before this, nothing but a world teardown ever freed a row: a despawned copy, a copy removed when
// the peer's link went down, a character the engine destroyed - each kept its row, `dead`, forever.
// T487's game B filled all 512 after one `leave` + `join direct` in a crowded area with 2 players.
//
// Only the row that carries THIS uid is released (a recycled address may already be registered under
// another uid - F336's case), and only once it is retired (`dead`), so no reader can be holding it as
// live. `retireFirst` retires the matching row first, for the stale-despawn path, whose row may not
// have been retired yet.
//
// Order, per table, for a reader already inside a probe: the array row's pointer goes to 0 BEFORE its
// flags are cleared (a reader checks `dead` first and then the pointer); the index slot becomes a
// TOMBSTONE (not 0), so no later entry's run is broken, and trailing tombstones are trimmed only by
// coopuid::IxTrimmable's rule.
bool MirrorRelease(const void* obj, unsigned int uid, bool retireFirst)
{
    if (obj == 0 || uid == 0) return false;
    MirrorRowLock();   // mirror1 fold (review-mirror1 #5): RetireOnDestroy's check-then-mark cannot straddle a release
    bool released = false;
    for (int i = 0; i < kMaxMirror; ++i)
    {
        if (g_mirror[i].obj != obj || g_mirror[i].uid != uid) continue;
        if (retireFirst) InterlockedExchange(&g_mirror[i].dead, 1);
        if (!g_mirror[i].dead) continue;
        InterlockedExchangePointer((PVOID volatile*)&g_mirror[i].obj, (PVOID)0);
        g_mirror[i].uid = 0;
        InterlockedExchange(&g_mirror[i].destroyed, 0);
        InterlockedExchange(&g_mirror[i].dead, 0);
        released = true;
        --g_mirrorUsedNow;
    }
    if (!released) { MirrorRowUnlock(); return false; }
    IndexKeys keys;
    int hs = coopuid::IxFind(keys, kIndexMask, IndexHash(obj), obj);
    if (hs >= 0 && g_index[hs].uid == uid)
    {
        InterlockedExchange(&g_index[hs].dead, 1);   // a reader matching in the gap reads "retired"
        InterlockedExchangePointer((PVOID volatile*)&g_index[hs].obj, (PVOID)coopuid::Tomb());
        g_index[hs].uid = 0;
        while (coopuid::IxTrimmable(keys, kIndexMask, hs))
        {
            InterlockedExchangePointer((PVOID volatile*)&g_index[hs].obj, (PVOID)0);
            ++g_indexTrimmed;
            hs = (hs - 1) & kIndexMask;
        }
    }
    MirrorRowUnlock();
    ++g_mirrorReleased;
    // mirror1 fold (review-mirror1 #3): a row is free again, so "the table is full" is no longer true and the stand-in
    // purge request may ask again. Full is a state that passes since rows are released.
    ::InterlockedExchange(&g_uidTableFullSeen, 0);
    // mirror1 fold (review-mirror1 #1 #2): what else is keyed to this finished character goes with its row - the soak
    // transition watch (keyed by uid; 2048 distinct uids used to exhaust it for the session) and its AI gate slot (found
    // by the character's ADDRESS recorded at gating; the object is not touched - it may be gone).
    g_soakWatchFreed   += SoakForgetUid(uid);
    g_gateFreedWithRow += ReleaseGateForObject(obj);
    return true;
}

// The shared creation core. Position is absolute world coordinates so both peers place
// the character identically - relative offsets would drift with each side's reference.
// F115: the modifiers are PARAMETERS, not globals read inside. Reading them here made them
// instance-local, so a spawn replicated as a Dust Bandit on the sender and a player-faction
// character on the receiver - same uid, same template, same position, different character.
// A shared creation core must take everything that shapes the character from its caller.
bool g_contextOn = true;    // M-B / H026 switch (`context on|off`), ON BY DEFAULT since AUD 2026-09-17 - a copy is
                            // created with its squad context; `context off` is the control. Defined here so CreateAt
                            // and the apply code share one

bool CreateAt(unsigned int uid, const std::string& templateName,
              const Ogre::Vector3& worldPos, const char* who,
              const std::string& factionName, bool keepContainer,
              RootObjectContainer* forceContainer, Building* homeBuilding, float age, Faction* forceFaction = 0)
{
    if (coop::GameWorldPtr() == 0) { ErrorLog("[M1] spawn refused: no GameWorld"); return false; }

    if (g_spawned.find(uid) != g_spawned.end())
    {
        // Idempotent by design: a replayed or duplicated SPAWN must not create twins.
        DebugLog("[M1] spawn ignored: uid " + S(uid) + " already exists locally");
        return true;
    }

    // Template lookup. Both installs load the same static data, so this name resolves to
    // the same content on both sides - that is what makes template-replication sound.
    // T083/F285: ANIMALS ARE A DIFFERENT GameData TYPE. `CHARACTER` and `RECORD_ANIMAL` are
    // separate enum values, so a Goat, Bonedog, Garru, Pack Beast, Pack Bull or Wild Bull returns 0
    // from a CHARACTER-only lookup - the peer refused 36 of 136 world characters with
    // "no CHARACTER template named 'Goat'", i.e. 26% of the world, every one of them an animal.
    //
    // Falling back locally rather than sending the type: both installs load byte-identical static
    // data (the same premise that lets a template NAME identify the same content on both sides,
    // F072), so the receiver can resolve the type itself and no protocol change is needed.
    GameData* data = coop::GameWorldPtr()->gamedata.findRecordByName(templateName, RECORD_CHARACTER);
    if (data == 0 || !PlausibleObject(data))
        data = coop::GameWorldPtr()->gamedata.findRecordByName(templateName, RECORD_ANIMAL);
    if (data == 0 || !PlausibleObject(data))
    {
        ErrorLog("[M1] spawn refused: no CHARACTER or RECORD_ANIMAL template named '" + templateName
                 + "' (lookup returned " + P(data) + ")");
        return false;
    }

    // NOT PlausibleObject: RootObjectFactory is non-polymorphic (see the guard comment).
    RootObjectFactory* factory = coop::GameWorldPtr()->objectFactory;
    if (!PlausiblePtr(factory))
    {
        // Log the value: T018 could not distinguish "null pointer" from "wrong guard"
        // because the pointer was never printed. Never refuse silently again.
        ErrorLog("[M1] spawn refused: objectFactory failed validation (value " + P(factory) + ")");
        return false;
    }

    // Faction and container come from an existing character so the spawn lands in a real
    // squad rather than in a null owner. The captured target is that reference; without
    // one we refuse instead of guessing.
    // T-304 (3) (run T674: 72 SPAWNs refused after this game's only character was eaten). The reference supplies only what
    // the message does not: the faction when none is named, and the squad when keepContainer asks for the reference's and no
    // context platoon was built. A SPAWN that names its faction and builds into a context platoon (or lets the engine choose)
    // needs no reference at all; otherwise any live player-faction character this game owns stands in for the watched player
    // (SpawnFallbackReference). Counted: spawnRefFallback, spawnBuiltNoRef, spawnRefusedNoRef.
    Character* ref = GetTarget();
    const bool needRef = (factionName.empty() && forceFaction == 0) || (keepContainer && forceContainer == 0);   /* T-556: a forced faction needs no reference */
    bool usedFallback = false, builtNoRef = false;   /* fold 1: counted only once the copy is registered (below MirrorAdd) */
    if (ref == 0 && needRef)
    {
        ref = SpawnFallbackReference();
        if (ref != 0) usedFallback = true;
    }
    // Both ARE polymorphic (Faction: 6 virtuals; ActivePlatoon via RootObjectContainer: 64).
    Faction*       faction   = 0;
    ActivePlatoon* container = 0;
    if (ref != 0) { faction = ref->getOwnerFactionDirect(); container = ref->getSquad(); }
    if (needRef)
    {
        if (ref == 0)
        {
            ++g_spawnRefusedNoRef;
            ErrorLog("[M1] spawn refused: uid " + S(uid) + " needs a reference character ("
                     + std::string(factionName.empty() ? "no faction named" : "keepContainer with no context platoon")
                     + ") and this game has no watched player and no live player-faction character of its own"
                     + " (spawnRefusedNoRef=" + S(g_spawnRefusedNoRef) + ")");
            return false;
        }
        if (!PlausibleObject(faction) || !PlausibleObject(container))
        {
            ErrorLog("[M1] spawn refused: reference faction=" + P(faction)
                     + " platoon=" + P(container) + " failed validation");
            return false;
        }
    }
    else
    {
        if (!PlausibleObject(faction))   faction = 0;     // replaced by the named faction below
        if (!PlausibleObject(container)) container = 0;   // unused: the named faction's branch clears it or the context platoon overrides it
        if (ref == 0) builtNoRef = true;
    }

    // F093/F101: inheriting the reference character's faction made every spawn a permanent
    // squadmate, so an ordered attack was accepted but could never be planned (T027 measured
    // "NOT an enemy" in both directions). An explicitly chosen faction also means the
    // player's squad is the WRONG container - passing it would put a rival faction's
    // character into the player's squad. Null lets the engine place it itself, which is
    // what its own load path does when it has no particular container in mind.
    // F106: passing null was that design decision's untested half, and T029 measured its
    // cost - the container-less spawn was the ONLY one of five that did not stay put
    // (6,296 units in 48.5 s at ~135 units/s, vs 0.0 for an ordinary spawn issued 8.1 s
    // later from the same place). The faction and the container are two separate choices
    // and were coupled in one code path, so nothing could tell them apart. `spawncontainer`
    // splits them: the explicit faction still applies either way.
    RootObjectContainer* useContainer = (RootObjectContainer*)container;
    if (!factionName.empty())
    {
        Faction* chosen = ResolveWireFaction(factionName);   // P3: "@player:<name>" -> the peer faction
        if (!PlausibleObject(chosen))
        {
            ErrorLog("[M1] spawn refused: faction '" + factionName
                     + "' did not resolve (lookup returned " + P(chosen) + ")");
            return false;
        }
        faction = chosen;
        if (!keepContainer)
        {
            useContainer = 0;
            DebugLog("[M1] spawning into faction '" + factionName
                     + "' (" + P(chosen) + "), container=NULL (engine-chosen)");
        }
        else
        {
            DebugLog("[M1] spawning into faction '" + factionName
                     + "' (" + P(chosen) + "), container=reference platoon " + P(container));
        }
    }

    // T-556: a faction handed in by the caller (CreateOwnInSquad: this player's own) is the faction, whatever the reference's.
    if (forceFaction != 0)
    {
        if (!PlausibleObject(forceFaction)) { ErrorLog("[M1] spawn refused: the forced faction " + P(forceFaction) + " failed validation"); return false; }
        faction = forceFaction;
    }

    // M-B / H026: a context-built platoon overrides the reference/NULL container choice above.
    if (forceContainer != 0)
    {
        useContainer = forceContainer;
        DebugLog("[M1] spawning into CONTEXT platoon " + P(forceContainer) + " home building " + P(homeBuilding));
    }

    // mirror1 (crash T487) A - A COPY THE UID TABLE CANNOT HOLD IS NEVER BUILT. T487 built and drove
    // 69 of them; with no row, no DESPAWN and no retired guard could ever find them again.
    if (!MirrorHasRoom())
    {
        ++g_spawnRefusedFull;
        g_mirrorLastRefusal = 1;   // T-354: the reason NOT_SHOWN carries to the owner
        if (coopuid::LogRefusal(g_spawnRefusedFull))   // mirror1 fold (review-mirror1 #12): rate-limited like the FULL line
            ErrorLog("[M1] spawn REFUSED uid " + S(uid) + " - the uid mirror is full (" + S(kMaxMirror)
                     + " rows): no copy is built here, so nothing drives it and nothing can be left behind"
                     + " (spawnRefusedFull=" + S(g_spawnRefusedFull) + "; logged for the first 5 and every 100th).");
        return false;
    }

    Ogre::Quaternion rot = Ogre::Quaternion::IDENTITY;

    RootObjectBase* obj = factory->create(
        data,
        worldPos,
        false,                      // isFromActiveLevelMod
        faction,
        rot,
        0,                          // notifyTarget
        useContainer,
        0,                          // saveState - template spawn (M1 scope, F072)
        false,                      // invisible
        homeBuilding,               // M-B: the copy's home (process's tail sets residency from it)
        age);                       // P1: the owner's age for a remote copy (read-parity3 GAP 1); 0.0 for a local spawn

    if (obj == 0 || !PlausibleObject(obj))   // RootObjectBase is polymorphic
    {
        ErrorLog("[M1] spawn FAILED: create returned " + P(obj));
        return false;
    }

    // mirror1 (crash T487) A: registered FIRST, and a refusal ends the spawn. The room check above makes
    // this reachable only through a full hash index, or (review-mirror1 #6) an address still registered
    // under another uid; the body then exists but is not ours - it is left
    // to this engine's AI, never in g_spawned, never a puppet - and counted.
    if (!MirrorAdd(uid, obj))   // keeps the detour-safe reverse lookup in step
    {
        ++g_spawnRefusedFull;
        if (coopuid::LogRefusal(g_spawnRefusedFull))   // mirror1 fold (review-mirror1 #6 #12): says why; rate-limited
            ErrorLog("[M1] spawn uid " + S(uid) + " built but NOT registered (" + MirrorRefusalWhy() + ") - the body"
                     " is left to this engine's AI and is not driven (spawnRefusedFull=" + S(g_spawnRefusedFull)
                     + "; logged for the first 5 and every 100th).");
        return false;
    }
    g_spawned[uid] = obj;
    if (usedFallback) ++g_spawnRefFallback;   /* T-304 (3) fold 1: a built and registered copy only */
    if (builtNoRef) ++g_spawnBuiltNoRef;

    // F318 - AND REGISTER IT FOR LIVENESS CHECKING, on whichever instance we are.
    //
    // This is the CREATE path, which on the client is every character the authority tells it to
    // build. T087: the host tracked 103 characters' liveness and the client tracked ZERO while
    // holding 62 raw pointers, because rows were only ever recorded by the host's adoption sweep -
    // and the client crashed dereferencing one of them inside `ApplyRemoteHit`.
    //
    // Having asked for a character to exist confers no control over when it stops existing: the
    // moment it is created it is an ordinary world object and the engine's region streaming owns
    // its lifetime.
    TrackForLiveness(uid, (Character*)obj, false);   // created on the peer's instruction: liveness only, not authored (F494)
    P071NotePlacement(uid, obj, 5, worldPos.x, worldPos.y, worldPos.z);   // PROBE P071: floor right after create, position next frame
    // Log BOTH the requested position and the one the object actually holds after creation.
    // T028 found this line printed only `worldPos` - the REQUEST - so no spawn in this
    // project had ever been confirmed to be where the log said. That silently weakened
    // T020's "coordinate-exact replication" claim: comparing two logs that both echoed the
    // same request proved the wire carried the number, not that the characters co-located.
    // A log line that reads as "where it is" must not print "where we asked for".
    Ogre::Vector3 actual = ((Character*)obj)->worldPosition();
    // F108: the first version of this metric was named `placementError` but computed only
    // sqrt(dx^2+dz^2) - it silently excluded Y, exactly the defect F087 had already recorded
    // for drift, reintroduced INSIDE the fix for F105. Name the axes so the label cannot lie.
    float placeErrXZ = sqrtf((actual.x - worldPos.x) * (actual.x - worldPos.x)
                           + (actual.z - worldPos.z) * (actual.z - worldPos.z));
    float placeErrY  = actual.y - worldPos.y;   // signed: ground-snap is expected to be negative

    DebugLog("[M1] spawned uid=" + S(uid) + " '" + templateName + "' obj=" + P(obj)
             + " requested=" + F1(worldPos.x) + "," + F1(worldPos.y) + "," + F1(worldPos.z)
             + " ACTUAL=" + F1(actual.x) + "," + F1(actual.y) + "," + F1(actual.z)
             + " placementErrorXZ=" + F1(placeErrXZ)
             + " placementErrorY=" + F1(placeErrY)
             + " (" + who + ")");

    // PROBE P013 (DIAGNOSTIC, F120) - why do the two instances' copies sometimes get
    // DIFFERENT maximum health? T033 and T034 both measured 100/100 on both sides; T035
    // measured 125 on A and 100 on B from identical inputs (same faction name, same
    // container mode, both logged). So the divergence is intermittent and something not
    // carried by the SPAWN message is shaping the character.
    //
    // The strongest candidate, recorded here as a MEASUREMENT rather than a claim: the
    // reference character is captured per-instance by `watchplayer`, and the container is
    // ITS platoon - so if the two instances captured different squad members, the spawns
    // inherit different containers. T035's placementErrorY also differed per instance
    // (-0.8 vs -1.1), which is consistent with two references standing on different ground.
    // This line prints the reference, the container, and the new character's own ceiling,
    // so the correlation can be read straight off the two logs instead of inferred.
    {
        MedicalSystem* med = (MedicalSystem*)((char*)obj + 0x458);
        std::string maxes, ages, hpms, chances;
        if (PlausiblePtr(med))
        {
            int n = med->countBodyParts();
            if (n > 0 && n <= 32)
                for (int i = 0; i < n; ++i)
                {
                    MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
                    bool ok = PlausiblePtr(p);
                    maxes   += (i ? "," : "") + (ok ? F1(p->maxHealthBase)    : std::string("?"));
                    // F142: T044 caught the divergence AT SPAWN - A 100.0 x7, B 125.0 x7,
                    // on the ONE explicit-faction spawn of four. `maxHealthBase` is set by
                    // `HealthPartStatus::setup(dat, hitchance, MAX, AGE, ...)`, so the two
                    // other setup inputs are printed beside it: if `age` differs, the
                    // difference came in through the creation call; if only `_max` differs,
                    // it came from the template/race resolution.
                    ages    += (i ? "," : "") + (ok ? F1(p->age)           : std::string("?"));
                    hpms    += (i ? "," : "") + (ok ? F1(p->healthScale)        : std::string("?"));
                    chances += (i ? "," : "") + (ok ? F1(p->hitWeight)     : std::string("?"));
                }
            else
                maxes = "partCount=" + S(n);
        }
        else maxes = "medical unreadable";

        // The character's RACE, by name. Kenshi's races carry different health scales, and a
        // 25% difference across every part at once is what a different race - or a different
        // age band of the same race - looks like. Never printed before, so never excluded.
        std::string raceName = "?";
        {
            GameData* gd = obj->getRecordDirect();
            if (PlausibleObject(gd)) raceName = gd->name;
        }

        Ogre::Vector3 rp = (ref != 0) ? ref->worldPosition() : Ogre::Vector3(0.0f, 0.0f, 0.0f);   // T-304 (3): a spawn may need no reference
        DebugLog("[P013] uid=" + S(uid) + " ref=" + P(ref)
                 + " refPos=" + F1(rp.x) + "," + F1(rp.y) + "," + F1(rp.z)
                 + " refPlatoon=" + P(container) + " squadPassed=" + P(useContainer) + " faction=" + P(faction)   /* the reference's squad, and the one handed to create (0 = the engine chooses) */
                 + " factionName='" + (factionName.empty() ? std::string("<inherited>")
                                                           : factionName) + "'"
                 + " keepContainer=" + S(keepContainer ? 1 : 0)
                 + " data='" + raceName + "'"
                 + " maxHealth=" + maxes
                 + " age=" + ages
                 + " hpm=" + hpms
                 + " hitWeight=" + chances);
    }

    // PROBE P019 (F153) - what this character LOOKS like, emitted on EVERY spawn on BOTH
    // instances so the two logs diff without anyone having to ask for it.
    //
    // This is the instrument the project never had. T050's first ten minutes of human
    // attention found that one uid, one template and one spawn command produce a bearded man
    // on A and a woman in a straw hat on B - and no counter here could see it, because every
    // counter measured state and none measured the character. It rides the spawn path rather
    // than a command for the same reason P013 does: a probe you have to remember to run is a
    // probe that is not running (F121's lesson).
    //
    // `building=1` means the body was still being assembled when this was read, so the line
    // is a snapshot and not a verdict - re-read with `look <uid>` once it settles.
    DebugLog("[P019] uid=" + S(uid) + " (" + who + ") "
             + AppearanceString((::Character*)obj));

    return true;
}

} // namespace

// M4 fold (review 2026-09-29 H1, store protocol 58) + owner decision 205 A: the uid minter's seat and blocks - for store.cpp
// (UidBlockTick, UidBlockOnNotebook, the link-down edge) and the adoption sweep. MAIN THREAD.
bool SpawnUidBlockWanted(unsigned int nowMs) { return coopuid::UidBlockWantedOnLink(&g_uidMint, UidLinkedForMint(), coop::StoreNotebookLinkGen(), nowMs, 5000u); }
unsigned int SpawnUidSeat() { return g_uidMint.seat; }
void SpawnUidBlockAsked(unsigned int nowMs) { coopuid::UidBlockAsked(&g_uidMint, nowMs); }
void SpawnUidBlocksForget()
{
    if (g_uidMint.seat != coopuid::UidNoSeat)
    {
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "[M4] uid seat " << g_uidMint.seat << " and its blocks dropped with the link (next counter " << g_uidMint.curNext
           << " of block " << g_uidMint.curLo << ".." << g_uidMint.curHi << ", spare " << g_uidMint.nextLo << ".." << g_uidMint.nextHi
           << ") - the notebook never grants those counters again, so they are wasted, never repeated";
        DebugLog(ss.str());
    }
    coopuid::UidBlocksForget(&g_uidMint);
    g_uidNoSeatSaid = false;
}
void SpawnUidBlockReply(bool decoded, unsigned int kind, unsigned int seat, unsigned int lo, unsigned int hi, long linkGen)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    if (!decoded)
    {
        ++g_uidBlockBadReply;
        ErrorLog("[M4] a malformed UID_BLOCK from the notebook - ignored");
        return;
    }
    if (!UidLinkedForMint())
    {
        ++g_uidBlockBadReply;
        ErrorLog("[M4] a UID_BLOCK answer arrived while this link has no WELCOME - ignored");
        return;
    }
    if (kind == (unsigned int)coopuidblk::kUbGrant)
    {
        const unsigned int was = g_uidMint.seat;
        const int took = coopuid::UidGrantOnLink(&g_uidMint, seat, lo, hi, linkGen);   /* M4 fold 2: the block is bound to the link it arrived on */
        ss << "[M4] uid block " << (took ? "granted" : "REFUSED (stale, malformed, or current and spare both held)") << ": seat " << seat;
        if (took && was != seat) ss << (was == coopuid::UidNoSeat ? " (this game's seat on this link)" : " (MOVED from a spent seat)");
        ss << " counters " << lo << ".." << hi << " (uids " << coopuid::UidMake(seat, lo) << ".." << coopuid::UidMake(seat, hi)
           << "); holding next " << g_uidMint.curNext << " of " << g_uidMint.curLo << ".." << g_uidMint.curHi << ", spare "
           << g_uidMint.nextLo << ".." << g_uidMint.nextHi << " (taken=" << g_uidMint.blocksTaken << " refused=" << g_uidMint.blocksRefused << ")";
        if (took) DebugLog(ss.str()); else ErrorLog(ss.str());
    }
    else if (kind == (unsigned int)coopuidblk::kUbExhausted)
    {
        coopuid::UidNoMoreOnLink(&g_uidMint, seat, linkGen);
        ss << "[M4] the notebook has NO uid counters left for this game: seat " << seat << " has handed out all 4,194,303 in this world's"
           << " life and no free seat has any left - this game mints no more uids on this link (uidCounterExhausted counts each refusal)";
        ErrorLog(ss.str());
    }
    else if (kind == (unsigned int)coopuidblk::kUbNoSeat)
    {
        ++g_uidNoSeatAnswers;
        if (!g_uidNoSeatSaid)
        {
            g_uidNoSeatSaid = true;
            ErrorLog("[M4] the notebook has no free uid seat - 1,024 games are connected - so this game makes no new synced characters"
                     " until a seat frees (asked again every 5 s; uidBlock noSeat counts the answers)");
        }
    }
    else
    {
        ++g_uidBlockFailed;
        /* M4 fold 2 (re-check M-A): an unreadable uid_seats.txt answers FAILED to every ask - the first 5 and every 100th are logged;
           the condition stays on the [M1] REPORT line (uidBlock failed), where a later on-screen notice can read it */
        if (coopuid::LogRefusal(g_uidBlockFailed))
            ErrorLog("[M4] the notebook granted no uid block (it could not write or read uid_seats.txt - its log says which); asked again in 5 s ("
                     + S(g_uidBlockFailed) + " FAILED answers so far; logged for the first 5 and every 100th)");
    }
}
bool SpawnUidMintReady()
{
    int counted = 0;   /* M4 fold 2 (re-check L-B): counted once per stretch of the same reason, not once per sweep tick */
    const int o = coopuid::UidSweepProbe(&g_uidMint, UidLinkedForMint(), coop::StoreNotebookLinkGen(), &counted);
    if (o == coopuid::kUidMinted) return true;
    if (counted) UidRefusalLog();
    return false;
}

bool SpawnTemplate(const std::string& templateName, float dx, float dz,
                   unsigned int uid, bool replicate)
{
    Character* ref = GetTarget();
    if (ref == 0)
    {
        ErrorLog("[M1] spawn refused: no captured reference character - run 'watchplayer' first");
        return false;
    }

    Ogre::Vector3 base = ref->worldPosition();
    Ogre::Vector3 pos(base.x + dx, base.y, base.z + dz);

    if (uid == 0)
    {
        uid = AllocateUid();   // M4 / owner 205 A: (this game's seat << 22) | a counter granted by the notebook; independent of session role and ordering
        if (uid == 0)
        {
            // M4 (owner 203): not minted (counted in AllocateUid). A console command has no later tick to retry on, so it
            // is refused in words before anything is created; run it again once this game's notebook has WELCOMEd it.
            ErrorLog(std::string("[M1] spawn refused: no uid could be minted - ") + coopuid::UidMintWords(g_uidMint.last)
                     + ". Nothing was created; run the command again once this game is linked to a notebook.");
            return false;
        }
    }

    // The local spawn is where the per-instance modifier state is READ - once, here - and
    // from this point it travels explicitly rather than being re-read on the far side.
    if (!CreateAt(uid, templateName, pos, "local", g_spawnFactionName, g_spawnKeepContainer, 0, 0, 0.0f))
        return false;

    // We made it, so we are its authority - recorded unconditionally, session or not.
    net::SetLocalOwner(uid);

    if (replicate)
    {
        // Absolute world coordinates travel, not the dx/dz offset: the peer's reference
        // character is somewhere else entirely. The modifiers travel for the same reason
        // (F115) - the peer's own `spawnfaction` state is not ours and never was.
        net::SendSpawn(uid, templateName, pos.x, pos.y, pos.z,
                       g_spawnFactionName, g_spawnKeepContainer, FindSpawned(uid));

        // H010a (F156): the APPEARANCE cannot ride the SPAWN message. At this point the
        // engine has not applied the roll yet - the values still read as defaults (F157) -
        // so the uid is registered here and the roll is captured and sent by AppearanceTick
        // the moment it settles. Sending it inline would have replicated "no appearance"
        // on both sides and looked exactly like a pass.
        WatchLocalRoll(uid);
    }
    return true;
}

/* P11 (test-only lever capturetest, items.cpp): SpawnTemplate's own road with the anchor and faction passed in. */
bool SpawnTemplateNear(const std::string& templateName, unsigned int anchorUid, float dx, float dz,
                       const std::string& factionName, bool keepContainer, unsigned int* uidOut)
{
    if (uidOut) *uidOut = 0;
    ::Character* anchor = FindSpawned(anchorUid);
    Ogre::Vector3 base(0.0f, 0.0f, 0.0f);
    if (anchor == 0 || !SafeReadPosition(anchor, &base))
    {
        ErrorLog("[M1] spawn (near) refused: uid " + S(anchorUid) + " is not a character with a readable position on this game");
        return false;
    }
    Ogre::Vector3 pos(base.x + dx, base.y, base.z + dz);
    const unsigned int uid = AllocateUid();
    if (uid == 0)
    {
        ErrorLog(std::string("[M1] spawn (near) refused: no uid could be minted - ") + coopuid::UidMintWords(g_uidMint.last)
                 + ". Nothing was created.");
        return false;
    }
    if (!CreateAt(uid, templateName, pos, "local", factionName, keepContainer, 0, 0, 0.0f))
        return false;
    net::SetLocalOwner(uid);
    net::SendSpawn(uid, templateName, pos.x, pos.y, pos.z, factionName, keepContainer, FindSpawned(uid));
    WatchLocalRoll(uid);
    if (uidOut) *uidOut = uid;
    return true;
}

/* T-556 (resurrect.cpp): a new character of this player's own faction made at `pos` into `squad` (the shared creation core,
   the faction from the reference character as `spawn`), owned by this game and NOT announced - the caller writes what it
   carries first and then sends the SPAWN. The new uid is handed back. */
bool CreateOwnInSquad(const std::string& templateName, const Ogre::Vector3& pos, ActivePlatoon* squad, float age, unsigned int* uidOut)
{
    if (uidOut) *uidOut = 0;
    const unsigned int uid = AllocateUid();
    if (uid == 0)
    {
        ErrorLog(std::string("[M1] spawn (own squad) refused: no uid could be minted - ") + coopuid::UidMintWords(g_uidMint.last)
                 + ". Nothing was created.");
        return false;
    }
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0)
    {
        ErrorLog("[M1] spawn (own squad) refused: this game has no player faction");
        return false;
    }
    if (!CreateAt(uid, templateName, pos, "own squad", std::string(), true, (RootObjectContainer*)squad, 0, age, mine))
        return false;
    net::SetLocalOwner(uid);
    if (uidOut) *uidOut = uid;
    return true;
}

// M-B, defined at the end of this file (coop scope - CreateAt above lives in the anonymous namespace).
bool PrepareContextForSpawn(unsigned int uid, const std::string& factionName, const Ogre::Vector3& pos,
                            RootObjectContainer** containerOut, Building** buildingOut, int* memberOut);
void FinishContextForSpawn(unsigned int uid, int memberType);
bool AdoptExistingTwin(unsigned int uid, const Ogre::Vector3& authorityPos);

bool ApplyRemoteSpawn(unsigned int uid, const std::string& templateName,
                      float x, float y, float z,
                      const std::string& factionName, bool keepContainer, float age,
                      const unsigned int* stats44)
{
    // M-B / H026: if this uid's CONTEXT arrived first (the host sends it before SPAWN on the same reliable
    // channel), build or reuse the platoon it names and create the copy INTO it, with its home building.
    // H027 (F435): if this instance ALREADY holds the character by the same handle (the save-derived dozen both
    // machines load), adopt that character as the puppet instead of creating a twin.
    if (g_contextOn && AdoptExistingTwin(uid, Ogre::Vector3(x, y, z)))
    {
        AdoptRemotePuppet(uid);
        if (stats44 != 0) ApplyRemoteStats(uid, stats44, "SPAWN adopt");   // S1: the twin rolled its own stats too
        NameOnCopyReady(uid);   // names1: a name that arrived before the copy existed
        TagsNoteCopy(uid);      // tags1: a label, if this is the other player's own character
        return true;
    }
    RootObjectContainer* ctxContainer = 0; Building* ctxBuilding = 0; int ctxMember = -1; bool ctxUsed = false;
    if (g_contextOn && FindSpawned(uid) == 0) ctxUsed = PrepareContextForSpawn(uid, factionName, Ogre::Vector3(x, y, z), &ctxContainer, &ctxBuilding, &ctxMember);   /* M7a fold F8: a re-sent SPAWN of a copy that exists builds no context */
    if (!CreateAt(uid, templateName, Ogre::Vector3(x, y, z), "remote",
                  factionName, keepContainer, ctxContainer, ctxBuilding, age))
        return false;
    if (ctxUsed) FinishContextForSpawn(uid, ctxMember);

    // F152 - THE LINE THAT WAS MISSING, and it is the whole of that finding.
    //
    // This function created the peer's copy and then left it alone. Only `ApplyRemoteTask`
    // and the explicit `replicate` command ever suppressed anything, so every SPAWN-replicated
    // character ran its OWN ai, locomotion and medicine on the peer - it shared a uid with the
    // authority's character and nothing else. `[M2] REPORT puppets=0` recorded that in every
    // log for weeks and was read as "P-3, a known gap": a LABEL standing in for a question
    // (F155). What it meant in play is F154 - one instance had characters running around
    // chasing while the other had the same characters standing still.
    //
    // Adopting here rather than on the first inbound MOVE closes the window in which the
    // character is suppressed but not yet driven; a suppressed character does not cancel its
    // own movement (F062), so that window is not harmless.
    AdoptRemotePuppet(uid);
    if (stats44 != 0) ApplyRemoteStats(uid, stats44, "SPAWN create");   // S1: init 0x64C0A0 re-rolled them here
    NameOnCopyReady(uid);   // names1: a name that arrived before the copy existed
    TagsNoteCopy(uid);      // tags1: a label, if this is the other player's own character
    return true;
}

// P033 / M7. Register a character THIS INSTANCE DID NOT CREATE as one we own and replicate.
//
// Everything else in this file creates a character and then registers it. World population is the
// opposite case: the engine already made these, and Option A needs them replicated to the peer.
// Registration is exactly the same two lines - the uid map and the detour-safe reverse lookup -
// so this exists to avoid a second, drifting copy of them in worldsync.cpp rather than to add
// anything new.
//
// Returns the uid, or 0 if the character is unreadable or already registered. **Already-registered
// is returned as 0 deliberately**: the caller is a sweep that will see the same character on every
// pass, and "I adopted it" and "it was already adopted" must not both count as work done, or the
// budget drains against characters that were finished long ago.
// mirror1 fold (review-mirror1 #3): MirrorHasRoom for other files (worldsync's refusal step). A separate name: MirrorHasRoom
// lives in this file's anonymous namespace, and a same-name declaration in coop would be ambiguous (C2668).
bool UidTableHasRoom() { return MirrorHasRoom(); }

// T-354 - A CHARACTER THIS GAME CANNOT SHOW IS REPORTED TO ITS OWNER (net/session.cpp OnSpawn / OnNotShown). MAIN THREAD.
long long MirrorPeerRefusals() { return g_spawnRefusedFull + g_twinRefusedFull; }
int MirrorRefusalReasonForWire()
{
    return (g_mirrorLastRefusal >= coopuid::kNsNoRow && g_mirrorLastRefusal <= coopuid::kNsReasonMax) ? g_mirrorLastRefusal : coopuid::kNsNoRow;
}
void NotShownNoteSent(unsigned int uid, unsigned int ownerKey, bool sent)
{
    if (sent) ++g_notShownSent; else ++g_notShownSendFailed;
    if (coopuid::LogRefusal(g_notShownSent + g_notShownSendFailed))
        ErrorLog("[M1] -> NOT_SHOWN uid " + S((long long)uid) + " to its owner (player key " + S((long long)ownerKey) + "): "
                 + (sent ? std::string("sent") : std::string("NOT SENT - no road up")) + " - our table refused its copy ("
                 + MirrorRefusalWhy() + ") (notShownSent=" + S(g_notShownSent) + " notShownSendFailed=" + S(g_notShownSendFailed)
                 + "; logged for the first 5 and every 100th).");
}
void NotShownOnNet(int decoded, unsigned int uid, int reason, unsigned int theirRefusals, unsigned int fromKey, bool uidMine)
{
    if (decoded != coopuid::kNsDecodeOk)
    {
        ++g_notShownInBad;
        if (coopuid::LogRefusal(g_notShownInBad))
            ErrorLog("[M1] <- NOT_SHOWN from player key " + S((long long)fromKey) + " malformed (reason " + S((long long)decoded)
                     + ") - ignored (notShownInBad=" + S(g_notShownInBad) + ")");
        return;
    }
    if (coopuid::NotShownOwnerAction(uidMine ? 1 : 0) == coopuid::kNsOwnerNotMine)
    {
        ++g_notShownInNotMine;
        if (coopuid::LogRefusal(g_notShownInNotMine))
            DebugLog("[M1] <- NOT_SHOWN uid " + S((long long)uid) + " from player key " + S((long long)fromKey)
                     + " names a character this game does not run - counted, nothing changes (notShownInNotMine=" + S(g_notShownInNotMine) + ")");
        return;
    }
    ++g_notShownIn;
    if (coopuid::LogRefusal(g_notShownIn))
        ErrorLog("[M1] <- NOT_SHOWN: our character uid " + S((long long)uid) + " is NOT SHOWN on the game of player key "
                 + S((long long)fromKey) + " - its character table refused it (" + coopuid::NotShownReasonName(reason) + "); that game has"
                 + " refused " + S((long long)theirRefusals) + " so far. Nothing changes here: the character goes on as ours (notShownIn="
                 + S(g_notShownIn) + "; logged for the first 5 and every 100th).");
}
// `mirrorcap <n>` - T-354 TEST-ONLY lever: the table refuses new rows while n or more are occupied (0 = off). Off clears the
// sweep's full mark, as a release does.
std::string MirrorTestCapLever(int cap)
{
    if (cap < 0 || cap > kMaxMirror) return "error mirrorcap range 0.." + S((long long)kMaxMirror);
    g_mirrorTestCap = cap;
    if (cap == 0) ::InterlockedExchange(&g_uidTableFullSeen, 0);
    ErrorLog("[M1] mirrorcap " + S((long long)cap) + (cap == 0 ? std::string(" (off)") : std::string(" - TEST-ONLY: new rows are refused while this many are occupied"))
             + "; occupied now " + S((long long)g_mirrorUsedNow) + "/" + S((long long)kMaxMirror));
    return "ok mirrorcap " + S((long long)cap) + " used " + S((long long)g_mirrorUsedNow);
}

unsigned int AdoptExisting(::Character* c, bool* tablesFull, bool* noUid)
{
    if (tablesFull) *tablesFull = false;
    if (noUid) *noUid = false;
    if (!PlausibleObject(c)) return 0;
    if (FindSpawnedUid(c) != 0) return 0;   // already ours

    // A RETIRED entry for this exact object means the character came back (its region streamed in
    // again). Restore it and report "already ours" - it keeps its original uid, so the peer's
    // existing copy is reused rather than a second one being minted alongside it.
    // F328: `dead` only, never `destroyed`. A destroyed slot means the engine destroyed the
    // character that used to live at this address, so this is a DIFFERENT character wearing a
    // recycled pointer - it must get its OWN uid, and falling through to the normal path below is
    // exactly right. Resurrecting the old uid is what made the host stream 220 MOVE messages for
    // two characters the client had already removed on our own instruction.
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == (const void*)c && g_mirror[i].dead && !g_mirror[i].destroyed)
        {
            MirrorAdd(0, c);   // un-retires in place, keeping the ORIGINAL uid
            return 0;
        }

    // mirror1 fold (review-mirror1 #3): the sweep now goes on after a refusal and retries on later ticks, so a full table
    // is answered BEFORE a uid is minted - a discarded uid per retry would be spent for nothing.
    if (!MirrorHasRoom())
    {
        ++g_mirrorFullRefused;
        g_mirrorLastRefusal = 1;
        ::InterlockedExchange(&g_uidTableFullSeen, 1);
        if (tablesFull) *tablesFull = true;
        return 0;
    }
    unsigned int uid = AllocateUid();
    if (uid == 0) { if (noUid) *noUid = true; return 0; }   /* M4 (owner 203): no slot yet / counter spent - counted in AllocateUid; nothing registered, the caller retries on a later tick */
    g_spawned[uid] = (RootObjectBase*)c;
    const bool took = MirrorAdd(uid, c);   // mirror1: MirrorAdd answers now; the verify below stays

    // VERIFY the registration took, and undo it if not. MirrorAdd refuses outright when either
    // table is full, and an unregistered character is the dangerous case for THIS caller
    // specifically: the world sweep uses `FindSpawnedUid(c) != 0` as its "already done" test, so a
    // character that is in `g_spawned` but not in the lookup would be adopted again on every tick -
    // minting a fresh uid and sending the peer a duplicate SPAWN, forever. An adversarial review
    // priced that at up to 1800 duplicate spawns per second.
    if (!took || FindSpawnedUid(c) != uid)
    {
        g_spawned.erase(uid);
        if (tablesFull) *tablesFull = true;
        ++g_adoptRefusedTables;
        if (coopuid::LogRefusal(g_adoptRefusedTables))   // mirror1 fold (review-mirror1 #3 #12): says why; rate-limited
            ErrorLog("[M1] adopt REFUSED for uid " + S(uid) + " - registration did not take ("
                     + std::string(took ? "registered, but the lookup does not answer this uid" : MirrorRefusalWhy())
                     + "). The character is NOT adopted and the uid is discarded; the sweep skips it and goes on"
                     + " (" + S(g_adoptRefusedTables) + " so far; logged for the first 5 and every 100th).");
        return 0;
    }
    return uid;
}

bool IsRetiredObject(const void* obj);   // defined below, beside MirrorRetire

// F316 - THE ONE SAFE WAY TO READ A CHARACTER'S POSITION, because doing it by hand crashed the
// host on T086 and the knowledge needed to prevent it was already written down twice.
//
// `Character::worldPosition` (0x5CDBF0) DOUBLE-DEREFERENCES the AnimationClass at `Character+0x448`
// with no null check of its own. `PlausibleObject(c)` does not cover that - it validates `c` and
// says nothing about a member pointer inside it, which is exactly the state a character being torn
// down is in.
//
// F205 already cost this project a crash for that omission. The roster, the census, the drive path
// and the world sweep all carry the guard, several with a comment naming F205. `ReportSpawns` did
// not, and it also iterated `g_spawned` DIRECTLY - bypassing `FindSpawned`, the accessor eleven
// lines below, whose entire reason for existing is to refuse retired pointers.
//
// That is why the host survived 891 seconds and then died in a report: the hot MOVE path reads
// through `MirrorSlot`, which hides retired entries, so retirement protected the path that runs a
// thousand times a second and did not protect the one that runs when someone types `report`.
//
// Both halves live here now, in one function, so a future caller cannot get half of it right
// (F125 -> F177: fix the possibility, not the instance).
bool SafeReadPosition(::Character* c, Ogre::Vector3* out)
{
    if (out == 0) return false;
    if (!PlausibleObject(c)) return false;
    if (IsRetiredObject(c)) return false;
    if (!PlausibleObject(*(void**)((char*)c + 0x448))) return false;   // AnimationClass
    *out = c->worldPosition();
    return true;
}

const void* SpawnedRawObject(unsigned int uid)
{
    std::map<unsigned int, RootObjectBase*>::const_iterator it = g_spawned.find(uid);
    return it == g_spawned.end() ? 0 : (const void*)it->second;
}

::Character* FindSpawned(unsigned int uid)
{
    std::map<unsigned int, RootObjectBase*>::const_iterator it = g_spawned.find(uid);
    if (it == g_spawned.end()) return 0;
    // Retired? Then the pointer in the map is not safe to hand out either.
    // mirror1 (crash T487): and NOT REGISTERED is the same answer. The question is "does the uid table
    // hold this object LIVE under THIS uid", which is false for a retired row, a released row, a row
    // rebound to another uid (F328), and a character the table never held. T487's puppet was the last
    // kind: the old test (IsRetiredObject) found no row, answered "not retired", and PlausibleObject
    // then passed a GameData that had taken the freed character's memory.
    if (FindSpawnedUid((const void*)it->second) != uid) return 0;

    // F318 - AND VALIDATE THE POINTER, because "the caller re-validates it anyway" was not true.
    //
    // That is what the comment here used to say, and `ApplyRemoteHit` is the counter-example that
    // killed the CLIENT in T087: it took this return value and called `poseState()` on it,
    // then handed it to an engine function that dereferences it unconditionally. `MirrorSlot`, the
    // OTHER way out of this table, has validated its pointer all along - so the two exits from one
    // registry disagreed about whose job it was, which is the shape F125 -> F177 warns about.
    //
    // A validating accessor cannot be forgotten by a caller. Retirement is SAMPLED, so there is
    // always a window between an object dying and P034 noticing, and this closes it.
    if (!PlausibleObject((const void*)it->second)) return 0;

    // Everything we create through the CHARACTER template path IS a Character; the cast is
    // safe for objects in this registry.
    return (Character*)it->second;
}

// HOT PATH, and it runs off the main thread - the combat-tick detour, the hit detour and the
// medical detour all call it. No allocation, no locks, no engine calls: a probe of a plain array.
//
// F267 blocker 2. This was a linear scan of all 512 array slots, which meant every MISS - and a
// world character that is not ours is always a miss - walked the whole table on a worker thread.
// It now probes the hash index and stops at the first empty slot, so a miss is typically one or
// two reads.
// F322 - THE DESPAWN ANNOUNCEMENT. Called from the `GameWorld::destroy` detour, immediately after
// the pointer has been retired, while the object is still valid.
//
// Deliberately separate from `RetireOnDestroy`: retiring is about OUR safety and must happen for
// every destroyed object we hold, on any thread. Announcing is about the PEER's world and is only
// legal on the main thread and only for a uid we author. Keeping them in one function would have
// meant the safety half inheriting the announcement half's restrictions.
//
// **The thread check is not optional trust.** F321 measured `tidDestroy` as the main thread on both
// instances across a full run, and that is evidence about what the engine did - not a guarantee
// about what it will do under a different load or a different region. A send off the main thread
// touches the transport's queues, which nothing else guards. If it ever happens, the counter says
// so rather than the process dying.
volatile LONG g_mainThreadId = 0;
long long g_despawnSent    = 0;
long long g_despawnOffThread = 0;   // refused because we were not on the main thread
long long g_despawnNotOurs = 0;     // ours to hold, but not ours to announce
long long g_despawnNoUid   = 0;     // F323: destroyed, but no uid resolved - the silent exit
long long g_despawnApplied = 0;     // inbound: our copy removed on the peer's say-so

// R1-a / R1-a-b (review-r1a M1) - the STATE prone write waits until CharacterProneSafe says the
// engine's prone path can read this character (setProneState 0x5C7390 reads +0x448 -> +0xE8 ->
// +0x184, +0x640 and +0x650 unchecked; it never reads the scene node, so a character with no
// visible body is not held back). uid -> the latest authoritative prone value and the peer whose
// STATE carried it; latest wins. Flushed by ProneParkTick, which re-asks the ownership guard (M2).
// Balance: proneParked = appliedAfterPark + droppedRetired + superseded + mooted + droppedNotOwner
// + parkedNow.
// crash1 (T293 / F912): a knockdown of a STANDING copy - the owner's STATE saying unconscious / a wake-up clock (the latch,
// which makes the copy's own engine knock it out, F439), or prone 2..4 - waits while KnockdownMustWait says so: a body
// rebuild queued on the copy (never timed out: the rebuild on a limp body is the crash), or its appearance / worn items not
// applied yet (at most coopkolook::kFirstLookWaitMs from the first wait, g_knockHold). While it waits the latch is held back
// (latchHeld) and the prone write parks (proneParkedRebuild, one per STATE while waiting), so the balance becomes
// proneParked + proneParkedRebuild = the six terms on the right.
struct ProneParkEntry { int prone; unsigned int peer; };
std::map<unsigned int, ProneParkEntry> g_proneParkMap;
long long g_proneParked            = 0;  // a prone write parked because CharacterProneSafe said no
long long g_proneParkedRebuild     = 0;  // crash1: a knockdown parked because KnockdownMustWait said so
long long g_proneLooksTimeout      = 0;  // crash1: knockdowns let through after waiting coopkolook::kFirstLookWaitMs for the looks
long long g_latchHeld              = 0;  // crash1: STATEs whose knock-out latch was held back (review-crash1 MEDIUM-2)
// crash1c (review-crash1b R3c): rebuildSeen/rebuildSince = when the rebuild flag (KnockdownWait 1) was first seen in this
// hold; stuckNoted = the 10 s "rebuild flag stuck" line was written for this hold. The hold itself never ends on time.
struct KnockHold { DWORD since; bool gaveUp; bool rebuildSeen; DWORD rebuildSince; bool stuckNoted; };
const DWORD kKnockHoldStuckMs      = 10000;
long long g_knockHoldStuck         = 0;  // crash1c: holds whose rebuild flag stayed set > kKnockHoldStuckMs (once per hold)
std::map<unsigned int, KnockHold> g_knockHold;   // crash1: uid -> its knockdown has been waiting since; left when it may go
static long long KnockHoldCount();       // defined beside KnockdownMustWait (the report reads them first)
static long long KnockHoldOldestMs();
long long g_proneAppliedAfterPark  = 0;  // flush: safe now, write applied
long long g_proneDroppedRetired    = 0;  // flush or world teardown: FindSpawned == 0, entry erased
long long g_proneParkSuperseded    = 0;  // a newer STATE for that uid arrived; the parked value is discarded
long long g_proneParkMooted        = 0;  // flush: safe now, but already at that prone state or dead - no write
long long g_proneDroppedNotOwner   = 0;  // flush: the ownership guard now refuses that peer (a handoff made us the owner)
long long g_proneFlushBlocked      = 0;  // ProneParkTick passes skipped while EngineWritesBlocked() - not a balance term
long long g_proneParkLogged        = 0;  // "[M4] STATE corrected (parked ...)" lines printed so far
const long long kProneParkLogLimit = 20; // first 20 per session logged, then counted only (proneAppliedAfterPark)
long long g_despawnUnknown = 0;     // inbound for a uid we do not hold - normal, counted

// F332 - OUR OWN DESTRUCTION RE-ENTERING THE HOOK.
//
// `ApplyRemoteDespawn` retires the pointer and then asks the engine to destroy it THROUGH the
// hooked `GameWorld::destroy`, so `detour_destroy` runs on our own call and `NotifyDespawn` is
// invoked a second time for a character we have already dealt with. T091 counted every one of those
// as `despawnNoUid` - **46 of them, exactly `despawnApplied`** - which made the counter built to
// catch F323's silent exit read 46 on a run where that exit never occurred.
//
// F334 - IT HOLDS THE **OBJECT**, NOT A BOOL. A flag set around the call is a WINDOW, and anything
// else destroyed inside that window is swallowed by it: if the engine cascades into destroying an
// owned sub-object while we are inside `destroy`, a bool would attribute that sub-object to us and
// it would never be announced. Whether this engine cascades is **not established** - which is
// exactly why the test names the one object we are responsible for instead of a period of time.
//
// F334 - and it is SAVED AND RESTORED rather than cleared to zero. Nothing on this path throws
// today (there is no `try`/`catch` anywhere between `OnDespawn` and here, so a fault would take the
// process down rather than unwind), but the cost of being wrong is total and silent: a stuck marker
// would stop every despawn announcement and every puppet drop for the rest of the session.
const void* g_selfDespawnObj = 0;
long long g_despawnSelfReentry = 0;
long long g_despawnSendFailed  = 0;   // F332: the transport declined - the last uncounted exit
long long g_despawnUnload      = 0;   // F334: an UNLOAD, not a death - dropped but not announced

void NoteMainThread() { InterlockedExchange(&g_mainThreadId, (LONG)::GetCurrentThreadId()); }

// F323 - takes the UID, not the object. The caller must resolve it BEFORE retiring the pointer,
// because retirement makes `FindSpawnedUid` return 0 by design - which is exactly how the first
// version of this silently never sent anything.
//
// **AND EVERY EXIT IS NOW COUNTED.** The bug was invisible for a whole run because the failing
// branch was the only one that incremented nothing: three counters read zero, the fourth read zero,
// and "no despawns happened" looked identical to "no characters died" - while `destroyOurs=60` sat
// two fields away saying otherwise. An uncounted early return in a probe is a blind spot with a
// number beside it.
/* T-304 (2) (run T674) - THIS GAME'S OWN DESTROY THAT CAME OFF THE MAIN THREAD. The engine destroyed A's eaten corpse twice:
   first on a worker (despawnOffThread 0->1 - the uid was in hand and was dropped by the thread check), then on the main
   thread, where the row was already finished and the lookup answered 0 (despawnNoUid 0->1), so no DESPAWN went out on
   either call. The worker's call now leaves {uid, object, unloaded} in this fixed ring - no allocation, no lock, no log,
   interlocked claims - and the main thread (DrainOffThreadDespawns, from ReplicateTick) runs NotifyDespawn with it. */
namespace {
struct OffThreadDespawn { volatile LONG state; unsigned int uid; const void* obj; LONG unloaded; const char* reason; LONG dead; };   /* M7a3: + the engine's reason literal and the hook's hasDied read */   // state: 0 free, 1 writing, 2 ready
const int kOffThreadDespawnSlots = 64;
OffThreadDespawn g_offThreadDespawn[kOffThreadDespawnSlots];
volatile LONG64 g_despawnQueuedOffThread = 0, g_despawnQueueFull = 0;
long long g_despawnQueueDrained = 0, g_despawnRowFinished = 0;   /* fold 1: despawnRowFinished sees a repeat destroy only while its row
   lives - MirrorReclaimDestroyed frees finished rows every 30 frames; a later repeat finds no row, is not ours, and counts nowhere */
bool OffThreadDespawnPush(unsigned int uid, const void* obj, bool justUnloaded, const char* reason, int dead)
{
    for (int i = 0; i < kOffThreadDespawnSlots; ++i)
    {
        if (::InterlockedCompareExchange(&g_offThreadDespawn[i].state, 1, 0) != 0) continue;
        g_offThreadDespawn[i].uid = uid;
        g_offThreadDespawn[i].obj = obj;
        g_offThreadDespawn[i].unloaded = justUnloaded ? 1 : 0;
        g_offThreadDespawn[i].reason = reason; g_offThreadDespawn[i].dead = (LONG)dead;   /* M7a3 */
        ::InterlockedExchange(&g_offThreadDespawn[i].state, 2);
        ::InterlockedIncrement64(&g_despawnQueuedOffThread);
        return true;
    }
    ::InterlockedIncrement64(&g_despawnQueueFull);
    return false;
}
/* The index row for this address is FINISHED (destroyed) although the hook's entry lookup answered 0: a second destroy of an
   object whose first destroy already retired it, or an address P034 already settled. Nothing is left to announce - that is
   not a missing uid. Address compares only. */
bool IndexRowFinished(const void* obj)
{
    if (obj == 0) return false;
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        const UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0) return false;
        if (slot.obj == obj) return slot.destroyed != 0;
    }
    return false;
}
}   // namespace (T-304 off-thread despawn)

/* M7a3 (manager decision 2026-10-01 on T772): THE ENGINE'S REASON, COUNTED. One row per reason text this game's engine removed
   one of our characters with while its own flag said "not unloaded": how many went to the other games as an UNLOAD, how many as a
   death. The first of each reason and outcome is logged, the rest are counted (removalReasons on the M1 REPORT). MAIN THREAD. */
namespace {
struct RemovalReasonRow { long long unload; long long death; };
std::map<std::string, RemovalReasonRow> g_removalReasons;
long long g_removalReasonsFull = 0, g_despawnUnloadByReason = 0;
int g_drainingOffThread = 0;   /* M7a3f2: 1 while DrainOffThreadDespawns replays a destroy - the object may already be gone, so nothing reads it */
std::string RemovalReasonText(const char* r) { return r != 0 ? std::string(r) : std::string("(none)"); }
void RemovalReasonNote(const char* reason, int unload, int dead)
{
    const std::string key = RemovalReasonText(reason);
    std::map<std::string, RemovalReasonRow>::iterator it = g_removalReasons.find(key);
    if (it == g_removalReasons.end())
    {
        if (g_removalReasons.size() >= 64) { ++g_removalReasonsFull; return; }
        RemovalReasonRow z; z.unload = 0; z.death = 0;
        it = g_removalReasons.insert(std::make_pair(key, z)).first;
    }
    long long& n = (unload != 0) ? it->second.unload : it->second.death;
    if (n++ == 0)
        DebugLog("[M1] engine removal reason='" + key + "' (engine flag false" + std::string(dead == 1 ? ", isDead=1" : (dead == 0 ? ", isDead=0" : ""))
                 + ") -> " + std::string(unload != 0 ? "UNLOAD - the character stays in the engine's world data, not a death"
                                                   : "DEATH - a DESPAWN when ours")
                 + " (first of this reason; the rest are counted in removalReasons)");
}
std::string RemovalReasonsToken()
{
    std::string out;
    for (std::map<std::string, RemovalReasonRow>::const_iterator it = g_removalReasons.begin(); it != g_removalReasons.end(); ++it)
    {
        if (!out.empty()) out += ";";
        out += "'" + it->first + "'=" + S(it->second.unload) + "u/" + S(it->second.death) + "d";
    }
    return "[" + out + "] removalReasonsFull=" + S(g_removalReasonsFull);
}
}   // namespace (M7a3 removal reasons)

void NotifyDespawn(unsigned int uid, const void* obj, bool justUnloaded, const char* reason, int dead)
{
    // F334 - THE THREAD CHECK GOES FIRST, and it did not before. `despawnOffThread` is this
    // project's ONLY live instrument for "the destroy-is-main-thread premise broke", and with the
    // self-re-entry test above it, an off-thread destroy landing while we were inside our own
    // despawn would have been filed under "our own call". That is the F323 shape exactly: a real
    // signal absorbed by a branch that means something else. Nothing below this line is legal off
    // the main thread anyway.
    if (g_mainThreadId == 0 || ::GetCurrentThreadId() != (unsigned long)g_mainThreadId)
    {
        ++g_despawnOffThread;
        if (uid != 0) OffThreadDespawnPush(uid, obj, justUnloaded, reason, dead);   // T-304 (2): the main thread announces it (DrainOffThreadDespawns)
        return;
    }

    ForgetTarget(obj);   // H030: the engine destroyed the watched player (death, quit) - clear the watch before any read

    // F332/F334 - OUR OWN CALL, COMING BACK ROUND, identified by the object rather than by a
    // window. Everything this function would do has already been done by `ApplyRemoteDespawn`: the
    // puppet was dropped, the pointer was retired, and re-announcing a uid we do not author is
    // exactly what commitment 3 forbids.
    if (obj != 0 && obj == g_selfDespawnObj) { ++g_despawnSelfReentry; return; }

    if (uid == 0)
    {
        if (IndexRowFinished(obj)) { ++g_despawnRowFinished; return; }   // T-304 (2): a repeat destroy of a finished row - nothing left to announce
        ++g_despawnNoUid;
        return;
    }

    // F326 - DROP OUR OWN PUPPET FIRST, WHOEVER OWNS THE UID.
    //
    // T090 ended with **12 puppets reading `mv=NOCHAR` and 3 reading `mv=UNREADABLE`**, four of them
    // 4,700-9,900 units out of position - and the same run recorded **`destroyOurs=15` on the
    // CLIENT**. Those are the same fifteen characters: the client's own engine destroyed them,
    // nothing removed the puppet, and the drive went on holding a pointer it could no longer read.
    // They are also invisible to every cohort measurement in the run, because an unreadable puppet
    // is excluded from the parity tables by construction - **a parity failure that the instruments
    // score as absent.**
    //
    // This is deliberately BEFORE the ownership test and not inside it. Announcing is about the
    // peer's world and only the authority may do it; dropping is about OUR state and is required
    // either way. The previous shape tied the two together and the client - which never authors a
    // world uid - therefore did neither.
    DropPuppet(uid);
    FollowOwnForget(uid);   // and, for a character of this game's, its stop watch and sent stop point (the object is going)

    // F334 - AN UNLOAD IS NOT A DEATH. Dropping the puppet above is right either way: this pointer
    // is about to stop being valid whichever it is. **Announcing is not.** A region streaming out
    // would tell the peer to delete a character that is coming back, and because `RetireOnDestroy`
    // marks the slot `destroyed`, the return trip mints a fresh uid rather than restoring - a
    // despawn/respawn churn cycle for every region boundary the player crosses.
    //
    // Before F332 this could not arise: a streamed-out character had already been retired by P034,
    // so no uid resolved and nothing was announced. F332 makes exactly that population resolvable,
    // so the distinction has to be made explicitly now.
    //
    // **The engine DOES set this flag** - T219 measured it (`destroyUnloadedAll` 796 on the client,
    // 648 on the host, and every client-authored character destroyed in that run carried it). The
    // counter that answered the question stays; not announcing a DESPAWN for an unload stands.
    //
    // E9 / decision 35 amended (F524) - BUT NOT ANNOUNCING *ANYTHING* WAS THE OTHER HALF OF T219.
    // Saying nothing here leaves the peer holding a copy forever: the 1 Hz announce pass cannot fix
    // it afterwards, because the destroy that brought us here has already retired the index row
    // (`FindSpawned` returns 0 for a retired object, worldsync.cpp:409-410) and the adopted row keeps
    // no last-known position to judge on. T219's host was left with ~50 townspeople the client had
    // unloaded - ordinary host-side characters carrying client-block uids, which the host can never
    // announce, unload or despawn. So the withdrawal is sent HERE, where the uid is still in hand.
    //
    // An UNLOAD is not a DESPAWN: it retires the peer's copy as "the owner no longer has this
    // loaded", not as a death, and the return trip re-announces the character under a fresh uid -
    // which is what already happened, since `RetireOnDestroy` marked this slot destroyed regardless.
    // M7a3 (manager decision 2026-10-01 on T772) - AND AN UNLOAD IS NOT A DEATH WHATEVER THE FLAG SAYS. The engine passes
    // justUnloaded = false on paths that only put a character away (the squad walk, ~ActivePlatoon, the corpse update - see
    // src/common/removalreason.h), and T772's host sent 89 of them as deaths: the other game, still holding the area, deleted live
    // characters. The engine's reason decides; an ambiguous one is a death only when Character::hasDied said so at the hook.
    if (coopremoval::RemovalIsUnload(justUnloaded ? 1 : 0, coopremoval::EngineRemovalReasonClass(reason), dead) != 0)
    {
        ++g_despawnUnload;
        if (!justUnloaded) { ++g_despawnUnloadByReason; RemovalReasonNote(reason, 1, dead); }
        if (net::IsUidMine(uid))
        {
            if (g_drainingOffThread == 0) P119NotePutAway(uid, obj, reason);   // PROBE P119
            ReleaseOnPutAway(uid, obj, g_drainingOffThread == 0 ? 1 : 0);   /* M7a A1 build 2 [a1b2-sn0] (design 2.5): collected into this frame's RELEASE - no judgement here; its UNLOAD below is then held until the release settles */
            coop::WithdrawAnnouncedOnOwnUnload(uid, kUnloadWhyPutAway);   /* held while the uid is in an open release or its batch (ReleasePendingHas); sent at an asleep settle */
        }
        return;
    }
    RemovalReasonNote(reason, 0, dead);

    // Commitment 3: only the authority may announce. A peer that despawned a uid it does not own
    // would be deleting the other player's character.
    if (!net::IsUidMine(uid)) { ++g_despawnNotOurs; return; }

    // F329 - LOG THE SEND. T091's executor could establish the send only from a counter plus the
    // peer's receipts, because the string `DESPAWN` appeared zero times in the sender's whole log -
    // every other message on this wire logs both directions. A counter says how many; a line says
    // which, and when, and is what lets two logs be lined up against each other.
    if (net::SendDespawn(uid))
    {
        ++g_despawnSent;
        DebugLog("[net] -> DESPAWN uid=" + S(uid) + " (our engine destroyed it, reason='" + RemovalReasonText(reason) + "')");   /* M7a3: the engine's reason on the line */
    }
    // F332 - AND COUNT THE REFUSAL. This was the last exit in this function that recorded nothing,
    // which is the precise shape of the defect the rest of the function was rewritten to remove: a
    // transport that declined every send would leave `despawnSent=0` beside `destroyOurs=46` and
    // look exactly like F323 all over again.
    else ++g_despawnSendFailed;
}

long long DespawnSelfReentryCount() { return g_despawnSelfReentry; }

/* T-304 (2), fold 1 - ONLY THE OWNER REMOVES A CHARACTER, a corpse included (spawn.h). Refused: ONLY the engine's 'eaten' removal
   (MedicalSystem::gettingEaten 0x64F8E0 -> GameWorld::destroy(body, false, "eaten"), build/decomp_64f8e0.txt:192) of a registered
   copy of another game's character, with the link up. Every other reason goes through: the squad walk 0x79BA60 ('wandered out of
   zone', decomp_79ba60.txt:135-161) retries a member until destroy answers 0 and would spin on a refusal, and ~ActivePlatoon
   0x4FE4B0 ('platoon destructor') clears the member's squad (+0x658) before its destroy. A refusal answers FALSE - 0x798F50's
   own 'postponed' answer - and gettingEaten's caller 0x435350 discards the answer. The eat countdown (MedicalSystem+0x15C; the
   body's MedicalSystem is at +0x458 and its +0xE0 names the body) is set back to 1, so the eater retries after five more bites
   instead of on every bite (and the '{1} has been eaten alive.' line, when the engine shows it, repeats at that pace, not per
   bite); 1, not 0, because 0 re-enters the first-bite block (decomp_64f8e0.txt:118, a player-faction body). The write runs on the eating thread itself,
   inside its own call. The body stays, eaten at, until the owner's DESPAWN. */
volatile LONG g_copyDestroyLinked = 0;          // cached by DrainOffThreadDespawns on the main thread (the transport is not any-thread)
volatile LONG64 g_copyDestroyRefused = 0;       // by reason: 'eaten' is the only reason refused
volatile LONG64 g_eatenReArmed = 0, g_eatenLayoutOff = 0;   // the countdown set back / the body's MedicalSystem did not name it (nothing written)
volatile LONG g_copyDestroyRefusedLastUid = 0;
long long g_copyDestroyRefusedSeen = 0, g_copyDestroyRefusedLogged = 0;
static int DestroyReasonIsEaten(const char* r)   /* the engine passes a string literal; compared by value, any thread */
{
    if (r == 0) return 0;
    const char* e = "eaten";
    for (int i = 0; ; ++i) { if (r[i] != e[i]) return 0; if (e[i] == 0) return 1; }
}
static int EatenReArmPod(const void* obj)   /* 1 set back, 0 the layout did not match (nothing written), -1 faulted */
{
    __try
    {
        char* med = (char*)obj + 0x458;
        if (*(const void* const*)(med + 0xE0) != obj) return 0;
        *(int*)(med + 0x15C) = 1;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool RefuseEngineDestroyOfCopy(const void* obj, unsigned int uid, const char* debugInfo)
{
    if (obj == 0 || uid == 0 || DestroyReasonIsEaten(debugInfo) == 0) return false;     // only 'eaten' (fold 1)
    if (::InterlockedCompareExchange(&g_copyDestroyLinked, 0, 0) == 0) return false;   // no link: the peer-gone path owns the copies
    if (net::IsUidMineAnyThread(uid)) return false;                                    // ours: destroyed and announced (NotifyDespawn)
    ::InterlockedIncrement64(&g_copyDestroyRefused);
    ::InterlockedExchange(&g_copyDestroyRefusedLastUid, (LONG)uid);
    if (EatenReArmPod(obj) == 1) ::InterlockedIncrement64(&g_eatenReArmed); else ::InterlockedIncrement64(&g_eatenLayoutOff);
    return true;
}
void DrainOffThreadDespawns()
{
    if (g_mainThreadId == 0 || ::GetCurrentThreadId() != (unsigned long)g_mainThreadId) return;
    ::InterlockedExchange(&g_copyDestroyLinked, coop::PlayersPresent() ? 1 : 0);   /* another player here: the old link, or the roster's IN_WORLD */
    for (int i = 0; i < kOffThreadDespawnSlots; ++i)
    {
        if (::InterlockedCompareExchange(&g_offThreadDespawn[i].state, 2, 2) != 2) continue;
        const unsigned int uid = g_offThreadDespawn[i].uid;
        const void* obj = g_offThreadDespawn[i].obj;
        const bool unl = g_offThreadDespawn[i].unloaded != 0;
        const char* rsn = g_offThreadDespawn[i].reason; const int dd = (int)g_offThreadDespawn[i].dead;   /* M7a3 */
        ::InterlockedExchange(&g_offThreadDespawn[i].state, 0);
        ++g_despawnQueueDrained;
        DebugLog("[M1] T-304: uid=" + S(uid) + " was destroyed by this engine OFF the main thread" + std::string(unl ? " (unloaded)" : "")
                 + " - handled on the main thread now (despawnQueueDrained=" + S(g_despawnQueueDrained) + ")");
        g_drainingOffThread = 1; NotifyDespawn(uid, obj, unl, rsn, dd); g_drainingOffThread = 0;   /* M7a3f2 */
    }
    const long long refused = (long long)::InterlockedCompareExchange64(&g_copyDestroyRefused, 0, 0);
    if (refused != g_copyDestroyRefusedSeen)
    {
        g_copyDestroyRefusedSeen = refused;
        if (g_copyDestroyRefusedLogged < 20)
        {
            ++g_copyDestroyRefusedLogged;
            const unsigned int last = (unsigned int)::InterlockedCompareExchange(&g_copyDestroyRefusedLastUid, 0, 0);
            ErrorLog("[M1] T-304: this engine tried to remove uid=" + S(last) + ", a copy of the other game's character, as 'eaten' - refused;"
                     " it stays until the owner's DESPAWN (copyDestroyRefused[eaten]=" + S(refused) + " eatenReArmed=" + S((long long)g_eatenReArmed)
                     + " eatenLayoutOff=" + S((long long)g_eatenLayoutOff) + "; the first 20 are logged)");
        }
    }
}

// F322 - the authority's engine destroyed this character; remove OUR copy.
//
// MAIN THREAD ONLY (driven from the session pump). Order matters and is not arbitrary:
//
//  1. drop the puppet FIRST, so nothing drives a character that is about to stop existing;
//  2. withdraw the pointer from every table, so no other reader can reach it;
//  3. only then ask the engine to destroy it.
//
// Step 3 goes through the SAME hooked `GameWorld::destroy` the authority went through, which means
// our own P038 detour runs on it - retiring it a second time, harmlessly, and counting it. Calling
// the trampoline directly would have skipped our own bookkeeping to save nothing.
//
// **If the destroy call is refused or unavailable, the character is left standing but is still
// withdrawn from every table.** That is worse for parity - a body the authority no longer has - and
// better than a dangling pointer, and the counters tell the two cases apart rather than leaving a
// reader to assume which happened.
long long g_unloadApplied = 0, g_unloadUnknown = 0;   // M-A step 2
long long g_unloadRecv = 0;   /* M7a3f2: every UNLOAD received (the first 20 logged) */
long long g_unloadWithdrawnTwin = 0, g_unloadWithdrawnPlayer = 0, g_despawnKeptPlayer = 0;   // H030
// review-session S6 - the link-drop cleanup. Counted APART from the despawn/unload numbers above:
// those mean "the authority told us", and a link that died told us nothing. `peerGoneAbsent` is a
// claim with no local copy (never built, or already gone) and is normal, not a failure.
long long g_peerGoneDestroyed = 0, g_peerGoneWithdrawn = 0, g_peerGoneAbsent = 0, g_peerGoneDestroyFailed = 0;
// ... and the context platoons those announcements built (RetirePeerContextPlatoons, at the foot of
// this file beside the rest of the context-platoon code; declared here so the [M1] report can read them).
long long g_peerGoneRecreated = 0, g_peerGoneRetired = 0, g_peerGoneRetireFailed = 0, g_peerGoneRetireUnreadable = 0, g_peerGoneRetireKeptMembers = 0;
// H030: 1 if the character's faction is the player's (Faction+0x250 PlayerInterface* != 0), 0 if not, -1 faulted. POD.
static int IsPlayerFactionPod(::Character* c)
{
    __try
    {
        ::Faction* f = c->getOwnerFactionDirect();
        if (f == 0) return 0;
        return (*(void**)((char*)f + 0x250) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// H030 (T131/F445) - THE ONE TEST FOR "THIS COPY IS WITHDRAWN, NEVER DESTROYED", and it is shared
// deliberately: it had two callers the moment the link-drop cleanup (review-session S6) was written,
// and two copies of a safety predicate is how one of them ends up a version behind the other.
//
// A twin is this engine's OWN save-loaded character; the watched player is this instance's load
// centre (zones.cpp) and the P006 target. T131's client destroyed its own player on the first
// UNLOAD, lost its zone feed, was unloaded wholesale and crashed. `*isPlayer` splits the two
// reasons for the caller's counters; pass 0 if the split is not wanted.
/* T-304 (3): declared above the anonymous namespace. The first live, registered, player-faction character this game owns -
   the stand-in reference when the watched player is gone (eaten, destroyed). MAIN THREAD. */
static int FallbackUsablePod(::Character* c)   /* T-304 (3) fold 1: 1 = alive, not down (prone < 2, as the knock-down test in this file reads it), in a squad (+0x658) */
{
    __try
    {
        if (c->hasDied()) return 0;
        if ((int)c->poseState() >= 2) return 0;
        return (*(void**)((char*)c + 0x658) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
::Character* SpawnFallbackReference()
{
    for (std::map<unsigned int, RootObjectBase*>::const_iterator it = g_spawned.begin(); it != g_spawned.end(); ++it)
    {
        if (it->second == 0 || !net::IsUidMine(it->first)) continue;
        if (IsRetiredObject(it->second) || !PlausibleObject(it->second)) continue;
        ::Character* c = (::Character*)it->second;
        if (IsPlayerFactionPod(c) != 1) continue;
        if (FallbackUsablePod(c) != 1) continue;   /* fold 1: not dead, not down, in a squad */
        return c;
    }
    return 0;
}
static bool WithdrawNotDestroy(unsigned int uid, ::Character* c, bool* isPlayer)
{
    const bool twin   = g_twinUids.count(uid) != 0;
    const bool player = (c == GetTarget()) || IsPlayerFactionPod(c) == 1;
    if (isPlayer) *isPlayer = player;
    return twin || player;
}

void ApplyRemoteUnload(unsigned int uid)
{
    if (g_unloadRecv++ < 20) DebugLog("[M1] <- UNLOAD uid=" + S(uid) + " (the first 20 received are logged, the rest counted in unloadRecv)");   /* M7a3f2 */
    Character* c = FindSpawned(uid);
    if (c == 0) { ++g_unloadUnknown; return; }
    ++g_unloadApplied;
    // Both are WITHDRAWN (the puppet is dropped, the row is kept so a re-announce re-adopts the same
    // body) and never destroyed - see WithdrawNotDestroy.
    bool player = false;
    if (WithdrawNotDestroy(uid, c, &player))
    {
        UnpuppetForOwnership(uid);   // the local AI has it again, as after a handoff
        if (player) ++g_unloadWithdrawnPlayer; else ++g_unloadWithdrawnTwin;
        DebugLog("[M1] UNLOAD uid=" + S(uid) + (player ? " - the watched player / a player-faction character" : " - a shared-save twin")
                 + ": puppet withdrawn, character NOT destroyed, row kept (H030)");
        return;
    }
    DebugLog("[M1] UNLOAD uid=" + S(uid) + " - the owner withdrew it (its engine put it away, or its announcement was withdrawn); retiring our copy (not a death)");   /* M7a3f2: the old text named one cause for every UNLOAD */
    ApplyRemoteDespawn(uid);
}

// THE REMOVAL ITSELF, with NO counter of its own. Split out of `ApplyRemoteDespawn` for the
// link-drop cleanup (review-session S6), which removes the same copies for a different reason: the
// steps below are one mechanism, but `despawnApplied` means "our copy removed on the peer's say-so"
// and a link that died said nothing. Each caller counts its own event, so neither number absorbs
// the other's population.
//
// Returns 1 = destroyed, 0 = withdrawn but the engine refused the destroy, -1 = withdrawn because
// H030 forbids destroying this one.
static int RemoveLocalCopy(unsigned int uid, ::Character* c, const char* what)
{
    DropPuppet(uid);                       // 1
    MirrorRetire(c);                       // 2
    g_spawned.erase(uid);
    NameForgetUid(uid);                    // names1
    SlaveForgetUid(uid);                   // slave1
    coop::LimbsForgetUid(uid);             // LIMBS: a recorded limb block names this copy only
    DeadlookForgetCopy(uid);               // deadlook1 fold (review D3a): the dead-copy marks describe THIS copy only
    MedicalForgetCopy(uid);                // the owner's hunger / knocked-out / dead words for this copy (medical.cpp)
    TagsForgetUid(uid);                    // tags1: its name label goes with it (the registry's one removal path)

    // F332/F334 - name the object we are destroying so the detour can tell OUR destruction from the
    // ENGINE'S. Without it the two are indistinguishable inside `NotifyDespawn`, and T091's client
    // reported 46 unresolved uids that were all just this call coming back round. Saved and
    // restored rather than cleared, so a nested or interrupted call cannot strand the marker.
    // H030 (F445): never destroy the watched player or a player-faction character, whatever the message. The row is
    // already withdrawn above; the body stays and is logged as a parity gap.
    if (c == GetTarget() || IsPlayerFactionPod(c) == 1)
    {
        ++g_despawnKeptPlayer;
        ErrorLog(std::string("[M1] ") + what + " uid=" + S(uid) + " names the watched player / a player-faction character: withdrawn, NOT destroyed (H030)");
        return -1;
    }
    const void* prevSelf = g_selfDespawnObj;
    g_selfDespawnObj = c;
    bool destroyed = DestroyLocalObject(c);   // 3
    g_selfDespawnObj = prevSelf;
    // mirror1 (crash T487) C: the copy is gone, so its row is released (4). AFTER the destroy, because
    // the destroy detour identifies our own destruction through this row. A copy the engine refused to
    // destroy keeps its retired row: its body is still standing at that address.
    if (destroyed) MirrorRelease(c, uid, false);   // 4

    DebugLog(std::string("[M1] ") + what + " applied uid=" + S(uid)
             + (destroyed ? " - copy destroyed, matching the authority"
                          : " - copy WITHDRAWN but NOT destroyed (the engine refused, or the world"
                            " is gone). It is safe to hold, but the peer's world now has a body the"
                            " authority does not (F322)."));
    return destroyed ? 1 : 0;
}

void ApplyRemoteDespawn(unsigned int uid)
{
    NameForgetUid(uid);   // names1: a name held for a copy that will now never be built
    SlaveForgetUid(uid);  // slave1: a slave state held for a copy that will now never be built
    coop::LimbsForgetUid(uid);   // LIMBS: the same for a recorded limb block
    MedicalForgetCopy(uid);   // the owner's medical words for it (medical.cpp): every route below ends this copy, or there was none
    Character* c = FindSpawned(uid);
    // mirror1 (crash T487) B - REMOVAL DOES NOT DEPEND ON THE UID TABLE ALONE. T487: 15 DESPAWNs found
    // no live registered copy and were counted `despawnUnknown`, while the puppet list still held the
    // uid and went on driving it. The map entry and the puppet list are asked too.
    const void* raw = SpawnedRawObject(uid);
    const int route = coopuid::DespawnRoute(c != 0 ? 1 : 0, raw != 0 ? 1 : 0, HasPuppet(uid) ? 1 : 0);
    if (route == coopuid::kRouteUnknown)
    {
        // Normal, not an error: the peer may never have built this one, or it may already be gone.
        ++g_despawnUnknown;
        return;
    }
    if (route == coopuid::kRouteDropStale)
    {
        // Every trace is forgotten WITHOUT touching the object - it may be freed or reused memory.
        // DropPuppet(uid, false) writes nothing through the pointer; the row is retired and released
        // only where it still carries THIS uid.
        if (HasPuppet(uid)) DropPuppet(uid, false);
        if (raw != 0) MirrorRelease(raw, uid, true);
        g_spawned.erase(uid);
        DeadlookForgetCopy(uid);
        TagsForgetUid(uid);
        ++g_despawnDroppedStale;
        DebugLog("[M1] DESPAWN uid=" + S(uid) + " - no live registered copy (retired, released, or never"
                 " registered), so it was dropped WITHOUT touching the object: puppet forgotten, map entry"
                 " erased, row released. A body that still exists is left to this engine"
                 " (despawnDroppedStale).");
        return;
    }
    // Unchanged from before the split: the kept-player case is NOT an applied despawn (the copy is
    // still standing), every other outcome is.
    if (RemoveLocalCopy(uid, c, "DESPAWN") >= 0) ++g_despawnApplied;
}

// review-session S6 - THE PEER'S LINK WENT DOWN AND ITS PUPPETS ARE STILL STANDING HERE.
//
// Same removal as an inbound DESPAWN, for a different reason and with its own counters. The peer
// will never send a DESPAWN for these: it is gone. Everything it owns here was built from ITS
// announcements, so the copies are ours to remove - except the two H030 populations, which are this
// engine's OWN characters that a remote message may only ever withdraw (a twin; the watched player
// or a character of OUR player faction). The peer's own PLAYER characters are peer-faction copies
// here, which is a world faction like any other on this machine (playerfaction.cpp creates it
// through the faction manager, so `Faction+0x250` is null on it) - ordinary puppets, destroyed.
//
// Returns 1 destroyed, 0 withdrawn, -1 we hold no copy for that uid.
int DropPeerOwnedCopy(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0) { ++g_peerGoneAbsent; return -1; }

    bool player = false;
    if (WithdrawNotDestroy(uid, c, &player))
    {
        UnpuppetForOwnership(uid);   // the local AI has it again, as after a handoff
        ++g_peerGoneWithdrawn;
        DebugLog("[M1] peer gone: uid=" + S(uid)
                 + (player ? " - the watched player / a player-faction character" : " - a shared-save twin")
                 + ": puppet withdrawn, character NOT destroyed, row kept (H030)");
        return 0;
    }
    const int r = RemoveLocalCopy(uid, c, "PEER GONE");
    if (r == 1) { ++g_peerGoneDestroyed; return 1; }
    // r == 0: withdrawn from our tables, but the engine refused the destroy - the body is still
    // standing and `peerGoneDestroyed` must not claim it. r == -1: RemoveLocalCopy's own H030 guard
    // fired, which WithdrawNotDestroy above should already have caught - counted, not assumed away.
    ++g_peerGoneDestroyFailed;
    return 0;
}

// E27 / review-p5q HIGH-4. The registry is written only by AdoptExistingTwin and cleared only at teardown,
// both on the main thread; this is a read of the same set WithdrawNotDestroy already asks.
bool IsTwinUid(unsigned int uid)
{
    return uid != 0 && g_twinUids.count(uid) != 0;
}

unsigned int FindSpawnedUid(const void* obj)
{
    if (obj == 0) return 0;
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        const UidMirror& slot = g_index[(h + probe) & kIndexMask];
        // An empty slot terminates the chain. A removed entry is a TOMBSTONE, not a hole (see the
        // note at g_index, mirror1), so no later match is hidden; a tombstone never equals `obj`.
        if (slot.obj == 0)   return 0;
        // A retired entry does NOT terminate the chain - the slot is still occupied, so a later
        // match may sit behind it. Skip and keep probing.
        if (slot.obj == obj)
        {
            // mirror1 fold (review-mirror1 #4): uid and flag first, THEN the key again. A release-then-reuse of this slot
            // between the match and the uid read would otherwise answer the NEW occupant's uid for this address.
            const unsigned int u = *(const volatile unsigned int*)&slot.uid;
            const LONG d = slot.dead;
            if (*(const void* const volatile*)&slot.obj != obj) return 0;
            return d ? 0 : u;
        }
    }
    return 0;
}

// F332 - THE SAME LOOKUP, FOR THE ONE CALLER THAT NEEDS AN IDENTITY RATHER THAN A LICENCE TO
// DEREFERENCE.
//
// `FindSpawnedUid` returns 0 for a retired slot, and that is right for every normal caller: `dead`
// means "do not touch this pointer". But the destroy detour is not asking whether the object is
// safe to read - it is asking **what this address WAS**, so it can drop the puppet and, if we
// authored the uid, tell the peer. Resolving a uid dereferences nothing.
//
// F334 - **what it UNLOCKS does dereference, and that is worth stating plainly rather than hiding
// behind "a uid is just a number".** `DropPuppet` reads `p.ch->movement` to restore the speed it
// changed. That is safe HERE for a structural reason, not a hopeful one: this detour runs BEFORE
// `orig_destroy`, so the object is still whole, and the write is behind `PlausibleObj` plus SEH.
// Any future caller of this function inherits neither guarantee.
//
// **The population this rescues was EMPTY in T091 on both sides** - the host's `despawnNoUid=0`
// and all 46 of the client's were self re-entry - so this is a structural repair, not a measured
// one, and `despawnNoUid` is what will size it.
//
// The distinction that makes this safe is F328's: `dead` is reversible and means "withdrawn",
// `destroyed` is permanent and means "this address's identity is finished". A destroyed slot still
// returns 0, because the engine may have handed the same address to a new object and answering with
// the old uid is how a live character gets despawned on the peer.
//
// Why it matters: P034's sampled liveness check retires a pointer whenever a character streams out,
// which happens BEFORE the engine destroys it. On that path `FindSpawnedUid` answers 0, so
// `DropPuppet` never runs and the authority never announces - the F326 fix silently skipped for
// exactly the characters that produced the `mv=NOCHAR` puppets it was written for.
unsigned int FindSpawnedUidForDestroy(const void* obj)
{
    if (obj == 0) return 0;
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        const UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0)   return 0;
        if (slot.obj == obj)
        {
            // mirror1 fold (review-mirror1 #4): the same re-check as FindSpawnedUid.
            const unsigned int u = *(const volatile unsigned int*)&slot.uid;
            const LONG x = slot.destroyed;
            if (*(const void* const volatile*)&slot.obj != obj) return 0;
            return x ? 0 : u;
        }
    }
    return 0;
}

// P034 drives these. Retirement is not removal: the slot keeps its pointer forever so the hash
// chain stays intact, and only the `dead` flag moves.
//
// Restore exists because a failed handle resolve is NOT proof of destruction - a character streamed
// out with the region resolves again when it streams back, and retiring it permanently would drop a
// live character from replication. So retirement is reversible, and only a handle that resolves to
// a DIFFERENT object is permanent.
// F316. "Has this pointer been withdrawn?" as a named question, so callers that do not go through
// FindSpawned can still ask it. `ReportSpawns` was one such caller and it crashed the host.
// mirror1 (crash T487): asked of the INDEX (the same `dead` flag, written with the row) - a hash probe
// instead of a scan of the whole mirror (4096 rows since T-354), because this runs per puppet per frame. Address compares only, so
// the any-thread rule is unchanged. It answers "retired", and a character the table does not hold
// is NOT retired: the puppet side asks the stronger question (FindSpawnedUid(p.ch) == uid) in
// DrivePuppet and FindSpawned, which is what T487 lacked.
bool IsRetiredObject(const void* obj)
{
    if (obj == 0) return false;
    IndexKeys keys;
    const int s = coopuid::IxFind(keys, kIndexMask, IndexHash(obj), obj);
    return s >= 0 && g_index[s].dead != 0;
}

// mirror1 (crash T487) C - THE PERIODIC RECLAIM. MAIN THREAD, and never while the puppet list is being
// walked (it drops puppets): ReplicateTick calls it before its drive loop.
//
// A row whose identity is finished (`destroyed`: the engine destroyed the object, or P034 proved the
// address holds another one) can never be live again under its uid. Its uid's map entry and puppet are
// forgotten without a dereference, and the row is released. A merely `dead` row (streamed out) is
// kept - it comes back through MirrorRestore. The worldsync liveness row follows by itself: P034
// settles a row whose uid the map no longer names (stale1).
int MirrorReclaimDestroyed()
{
    int n = 0;
    for (int i = 0; i < kMaxMirror; ++i)
    {
        const void* o = g_mirror[i].obj;
        if (!coopuid::ReclaimRow(o != 0 ? 1 : 0, g_mirror[i].dead ? 1 : 0, g_mirror[i].destroyed ? 1 : 0)) continue;
        const unsigned int uid = g_mirror[i].uid;
        std::map<unsigned int, RootObjectBase*>::iterator it = g_spawned.find(uid);
        if (it != g_spawned.end() && (const void*)it->second == o) g_spawned.erase(it);
        DropPuppetIfObject(uid, o);   // no dereference: the object is gone
        if (MirrorRelease(o, uid, false))
        {
            ++g_mirrorReclaimed; ++n;
            // mirror1 fold (review-mirror1 #10): the uid is finished, so the per-uid marks RemoveLocalCopy forgets go too -
            // unless the map still names the uid at ANOTHER object (then the uid lives on there and the marks are its own).
            if (g_spawned.find(uid) == g_spawned.end())
            {
                NameForgetUid(uid);
                SlaveForgetUid(uid);
                coop::LimbsForgetUid(uid);
                DeadlookForgetCopy(uid);
                TagsForgetUid(uid);
                MedicalForgetCopy(uid);
            }
        }
    }
    return n;
}

void MirrorRetire(const void* obj)
{
    if (obj == 0) return;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == obj) InterlockedExchange(&g_mirror[i].dead, 1);
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0) break;
        if (slot.obj == obj) { InterlockedExchange(&slot.dead, 1); break; }
    }
}

// P038 / F319 - called from the `GameWorld::destroy` detour, on a thread we do not control.
//
// Returns true when the object was one of ours, so the hook can count how much of the engine's
// destruction traffic actually concerns us without doing the counting itself.
//
// SAFE OFF-THREAD, and the reasoning is the same as everything else in this table: it compares
// ADDRESSES and never dereferences the object, and it publishes with `InterlockedExchange`. It is
// called while the object is still valid - before the original `destroy` runs - so even the
// address comparison is against live memory.
//
// This is a PERMANENT retirement, not the reversible kind P034 issues. A failed handle resolve
// means "not right now" (a region streamed out, and F284 was careful to allow it back); the engine
// telling us it is destroying the object means "never again".
bool RetireOnDestroy(const void* obj)
{
    if (obj == 0) return false;
    // F328: `destroyed` is published BEFORE `dead` in both tables. A reader that can see `dead` must
    // already be able to see `destroyed`, or `MirrorAdd` could un-retire in the gap between the two
    // stores - which is the exact resurrection this flag exists to prevent.
    // mirror1 fold (review-mirror1 #5): the check and the marks are one step under the row lock (see g_mirrorRowLock), so a
    // row the main thread frees and refills cannot receive this object's marks.
    MirrorRowLock();
    bool ours = false;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == obj)
        {
            InterlockedExchange(&g_mirror[i].destroyed, 1);
            InterlockedExchange(&g_mirror[i].dead, 1);
            ours = true;
        }
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0) break;
        if (slot.obj == obj)
        {
            InterlockedExchange(&slot.destroyed, 1);
            InterlockedExchange(&slot.dead, 1);
            ours = true;
            break;
        }
    }
    MirrorRowUnlock();
    return ours;
}

// F334 - THE IDENTITY IS FINISHED, THOUGH WE ARE NOT THE ONES ENDING IT.
//
// `RetireOnDestroy` was the only writer of `destroyed`, which left P034's two confirmed-stale
// branches setting `dead` alone - and "the address now holds a DIFFERENT object" is precisely the
// permanent case that flag exists for. Two things depended on the distinction and got the wrong
// answer:
//   * `FindSpawnedUidForDestroy` (F332) would answer with the OLD uid for the new occupant, so
//     destroying the new object could announce a despawn for a uid that died another way;
//   * `AdoptExisting`'s F328 guard is `dead && !destroyed`, so a confirmed-recycled address takes
//     the un-retire-in-place path and adopts the NEW object under the OLD uid - the exact
//     resurrection F328 was written to prevent, arriving through the branch F328 did not cover.
//
// Measured, not hypothetical: T091 recorded `stalePointers=9` on the host and 5 on the client.
//
// **F336 - IT TAKES THE UID, AND THAT IS THE WHOLE SAFETY OF IT.** The caller is P034, whose rows
// are long-lived: a row can still name address A under `uid1` **after** the engine destroyed the
// original object AND the address was recycled AND we re-adopted it under `uid2`. P034 then
// discovers its own handle no longer resolves, correctly concludes "this address holds a different
// object", and asks us to mark it - and the row it would mark is the **live `uid2` row**.
//
// The previously shipped behaviour survived that: `MirrorRetire` set `dead` alone, so the next
// adoption sweep took `MirrorAdd`'s un-retire-in-place path and **kept `uid2`**. Marking
// `destroyed` instead makes it permanent - `uid2` stops streaming, a third uid is minted, a
// duplicate SPAWN goes to the peer, and the peer's `uid2` copy is orphaned forever because our
// engine will never destroy that object. **A self-healing collateral hit turned into permanent uid
// churn, in the name of a safety property.**
//
// So the mark is applied only if the slot still carries the uid the caller believes in. A mismatch
// is not an error - it is the re-adoption case, and it is counted so its rate is visible instead of
// inferred from a uid that quietly changed.
long long g_markDestroyedDeclined = 0;
long long g_staleUidRetired       = 0;   // F337: ...and the uid that was retired instead
long long g_staleUidSent          = 0;   // F338: ...of which this many were announced to the peer
long long g_staleUidNotOurs       = 0;   // F338: ...not ours to announce
long long g_staleUidSendFailed    = 0;   // F338: ...the transport declined

bool MirrorMarkDestroyed(const void* obj, unsigned int uid)
{
    if (obj == 0) return false;

    // Decide BEFORE writing anything: a partial mark (mirror yes, index no) would leave the two
    // tables disagreeing about whether an identity is finished, and both F328 and F332 read them.
    bool uidStillMatches = false;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == obj && g_mirror[i].uid == uid) uidStillMatches = true;
    if (!uidStillMatches)
    {
        int h0 = IndexHash(obj);
        for (int probe = 0; probe < kIndexSlots; ++probe)
        {
            const UidMirror& slot = g_index[(h0 + probe) & kIndexMask];
            if (slot.obj == 0) break;
            if (slot.obj == obj) { uidStillMatches = (slot.uid == uid); break; }
        }
    }
    if (!uidStillMatches) { ++g_markDestroyedDeclined; return false; }

    // Same publication order as RetireOnDestroy: `destroyed` before `dead`, so a reader that can
    // see `dead` can already see `destroyed` (F328).
    //
    // F337 - and mark only the rows that CARRY THIS UID. The decision above accepts a match from
    // either table, and an unconditional write would then mark whatever row the other table holds.
    // The two tables cannot disagree today - `MirrorAdd` refuses the whole registration unless both
    // have room, and the rebind path updates both - but that makes the safety property "the tables
    // happen to agree" rather than "the code writes the row it checked". Costs nothing to close.
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == obj && g_mirror[i].uid == uid)
        {
            InterlockedExchange(&g_mirror[i].destroyed, 1);
            InterlockedExchange(&g_mirror[i].dead, 1);
        }
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0) break;
        if (slot.obj == obj && slot.uid == uid)
        {
            InterlockedExchange(&slot.destroyed, 1);
            InterlockedExchange(&slot.dead, 1);
            break;
        }
    }
    return true;
}

// F337 - **THE DECLINE IS NOT A NO-OP, AND TREATING IT AS ONE ALIASED A DEAD UID ONTO A LIVE
// CHARACTER.**
//
// F336 stopped the mark from killing a re-adopted row, and left everything else alone. But the
// reason the mark was being asked for is that `uid1`'s object is genuinely GONE - the address was
// recycled and re-adopted as `uid2`. Doing nothing leaves `g_spawned[uid1]` pointing at that
// address, and the address's mirror slot is now **live** under `uid2` with `dead=0`. So
// `FindSpawned(uid1)` passes all three of its checks and returns **uid2's live character**:
//
//   * an inbound `DESPAWN uid1` from the peer would destroy a character nobody asked to remove;
//   * ~15 other consumers - combat, drive, medical, appearance - would act on uid1 and hit uid2.
//
// **The deployed build never reaches this state** (its retirement was reversible, so the slot
// un-retired keeping uid1 and no second uid existed) and neither did F334 (`dead=1` made uid1
// inert, at the cost of killing uid2). F336 removed the write that was incidentally making uid1
// unusable and put nothing in its place - so the repair introduced a state neither previous build
// had. **A decline must retire the uid it declined to mark.**
//
// Defined below, beside the ring it writes into; declared here because `RetireStaleUid` is the
// only caller and the two read better together than hoisted apart.
void NoteUidPermanentlyLost(unsigned int uid);

// MAIN THREAD ONLY - `ValidateAdopted` is called from the pump, and this sends.
void RetireStaleUid(unsigned int uid)
{
    if (uid == 0) return;
    ++g_staleUidRetired;

    // F342 - REMEMBER IT. This uid is about to have no mirror row of any kind, because the address
    // it lived at now belongs to a different uid. Without this the peer's continuing MOVEs for it
    // are counted as "we never heard of this character" - the exact opposite of what happened.
    NoteUidPermanentlyLost(uid);

    // F338 - do NOT restore the drive speed. The address this puppet was bound to now belongs to
    // another character, and the restore would write our saved value into it.
    DropPuppet(uid, false);     // stop driving something we can no longer identify
    g_spawned.erase(uid);       // and stop FindSpawned resolving it onto the new occupant
    NameForgetUid(uid);         // names1: the last name sent for it describes a character we can no longer identify
    SlaveForgetUid(uid);        // slave1: the same for its last slave state sent
    coop::LimbsForgetUid(uid);  // LIMBS: and a recorded limb block for it

    // The peer still holds a copy of a character that no longer exists on our side, and this is
    // exactly the divergence-by-attrition F322 was written to end. Only the authority may say so.
    //
    // F338 - **ITS OWN COUNTERS, NOT `despawnSent`.** `despawnSent` meant exactly one thing when
    // T091 verified `despawnSent == destroyOurs`: our engine destroyed a character we author and we
    // told the peer. Adding a second, unrelated source to it would put two events behind one
    // correctly-named number - the defect F332 exists to document, committed inside the fix for the
    // fix. The three exits are counted separately and every one of them is counted.
    if (!net::IsUidMine(uid)) { ++g_staleUidNotOurs; return; }
    if (net::SendDespawn(uid))
    {
        ++g_staleUidSent;
        DebugLog("[net] -> DESPAWN uid=" + S(uid) + " (F337: its address was recycled and"
                 " re-adopted under another uid, so this one is gone)");
    }
    else ++g_staleUidSendFailed;
}

long long MarkDestroyedDeclinedCount() { return g_markDestroyedDeclined; }
long long StaleUidRetiredCount()       { return g_staleUidRetired; }
long long StaleUidSentCount()          { return g_staleUidSent; }

void MirrorRestore(const void* obj)
{
    if (obj == 0) return;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj == obj) InterlockedExchange(&g_mirror[i].dead, 0);
    int h = IndexHash(obj);
    for (int probe = 0; probe < kIndexSlots; ++probe)
    {
        UidMirror& slot = g_index[(h + probe) & kIndexMask];
        if (slot.obj == 0) break;
        if (slot.obj == obj) { InterlockedExchange(&slot.dead, 0); break; }
    }
}

int MirrorCapacity() { return kMaxMirror; }

int MirrorUsed()
{
    int n = 0;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].obj != 0) ++n;
    return n;
}

// F341 - **DID WE ONCE HOLD THIS UID?** `FindSpawned` answers 0 for a retired row by design, so
// "no local puppet" currently means both "we never heard of it" and "we had it and the engine took
// it away" - two completely different failures behind one log line. T092 produced 173
// `MOVE for unknown uid` and **172 of them were a single uid** the host never despawned, whose
// trail reads: adopted, driven, drifted 253 -> 373 -> 493 -> 609 units, then
// `[P034] RETIRED - its handle no longer resolves`. It was still alive on the authority; it had
// simply fallen so far behind that it left the CLIENT's streamed region.
//
// Address compares only, retired rows included - that is the entire point.
//
// **F342 - AND A MIRROR SCAN ALONE GETS THE WORST CASE BACKWARDS.** `RetireStaleUid` is reached
// when `MirrorMarkDestroyed` declines, which happens precisely because the address has already been
// rebound to a NEW uid - so after it runs the old uid has **no mirror row at all** and this scan
// answers "never held it". That is the strongest divergence there is: a uid this world permanently
// lost and re-adopted under a different identity, while the peer keeps streaming the old one.
// T092 would have filed all seven of them (`staleUidRetired=7`) under "ordinary".
//
// So permanently-lost uids are remembered explicitly, in a small fixed ring. A ring rather than a
// growing set because this runs on the pump and the population is tiny; if it ever overflows, the
// count says so instead of the oldest entries vanishing silently.
const int kLostUidSlots = 64;
unsigned int g_lostUids[kLostUidSlots] = {0};
int          g_lostUidNext = 0;
long long    g_lostUidOverflow = 0;

void NoteUidPermanentlyLost(unsigned int uid)
{
    if (uid == 0) return;
    for (int i = 0; i < kLostUidSlots; ++i) if (g_lostUids[i] == uid) return;   // already known
    if (g_lostUids[g_lostUidNext] != 0) ++g_lostUidOverflow;   // we are about to forget one
    g_lostUids[g_lostUidNext] = uid;
    g_lostUidNext = (g_lostUidNext + 1) % kLostUidSlots;
}

long long LostUidOverflowCount() { return g_lostUidOverflow; }

bool KnownUidRetired(unsigned int uid)
{
    if (uid == 0) return false;
    for (int i = 0; i < kLostUidSlots; ++i)
        if (g_lostUids[i] == uid) return true;
    for (int i = 0; i < kMaxMirror; ++i)
        if (g_mirror[i].uid == uid && g_mirror[i].obj != 0 && g_mirror[i].dead) return true;
    return false;
}

bool MirrorSlot(int i, unsigned int* uid, Character** out)
{
    if (i < 0 || i >= kMaxMirror) return false;
    // Retired entries are invisible to EVERY iterator - the position streamer, the combat watch and
    // the transition watch all reach characters through here, and a retired entry is one whose
    // pointer must not be dereferenced again.
    if (g_mirror[i].dead) return false;
    const void* o = g_mirror[i].obj;
    if (o == 0 || !PlausibleObject(o)) return false;
    if (uid) *uid = g_mirror[i].uid;
    if (out) *out = (Character*)o;
    return true;
}

// PROBE P009 - DIAGNOSTIC ONLY. The two-attempt cap on "the ordered duel does not happen"
// is reached (F098), so this prints evidence instead of changing behaviour. H006 asks
// whether a factory-created character carries a usable `hand`; `hand` is an index+serial
// reference resolved through a registry, and getHandle() returns this+0x58 with a silent
// static fallback (F099), so an unregistered object hands back garbage rather than failing.
static void DumpOneHandle(const char* label, Character* c)
{
    if (c == 0 || !PlausibleObject(c))
    {
        DebugLog(std::string("[P009] ") + label + ": character unreadable");
        return;
    }

    const hand& h = c->getHandle();
    // Round-trip through the engine's OWN resolver: if the handle is valid, resolving it
    // must give back the same character. That comparison is the actual test - the field
    // values alone could look plausible and still not resolve.
    Character* back = h.getCharacter();

    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[P009] " << label << " obj=" << P(c)
       << " handle='" << h.toString() << "'"
       << " valid=" << (h ? "YES" : "NO")
       << " type=" << S((long long)h.type)
       << " index=" << S((long long)h.index)
       << " serial=" << S((long long)h.serial)
       << " container=" << S((long long)h.container)
       << " resolvesTo=" << P(back)
       << (back == c ? "  <-- ROUND TRIP OK" : "  <-- ROUND TRIP FAILED");
    DebugLog(ss.str());
}

void ReportHandle(unsigned int uid)
{
    Character* spawned = FindSpawned(uid);
    if (spawned == 0)
    {
        DebugLog("[P009] no local object for uid " + S(uid));
        return;
    }
    DumpOneHandle("SPAWNED (factory-created)", spawned);

    // The control: a character the ENGINE created when the save loaded. If its handle
    // round-trips and the spawned one does not, H006 is confirmed and the defect is in
    // the spawn path, not the combat path.
    DumpOneHandle("CAPTURED (save-loaded, control)", GetTarget());
}

bool SetSpawnFaction(const std::string& factionName)
{
    if (factionName.empty())
    {
        g_spawnFactionName.clear();
        DebugLog("[M1] spawn faction cleared - spawns inherit the reference character's"
                 " faction again (they will be allies, F093)");
        return true;
    }
    if (coop::GameWorldPtr() == 0 || !PlausiblePtr(coop::GameWorldPtr()->factionDirectory))
    {
        ErrorLog("[M1] spawn faction refused: faction manager unavailable");
        return false;
    }

    // Resolve NOW, not at spawn time: an unresolvable name should fail where the user can
    // see it, not silently later (live reads at the moment of commitment still apply -
    // CreateAt re-resolves, this is the early rejection).
    Faction* f = coop::GameWorldPtr()->factionDirectory->findFactionByName(factionName);
    if (!PlausibleObject(f))
    {
        ErrorLog("[M1] spawn faction refused: '" + factionName + "' did not resolve"
                 " - use the 'factions' command to list real names");
        return false;
    }

    g_spawnFactionName = factionName;
    DebugLog("[M1] spawn faction set to '" + factionName + "' (" + P(f) + ")");
    return true;
}

void SetSpawnContainer(bool keep)
{
    g_spawnKeepContainer = keep;
    DebugLog(std::string("[M1] explicit-faction spawns will use container=")
             + (keep ? "reference platoon (kept)"
                     : "NULL (engine-chosen) - the T029 runaway variant, F106"));
}

void ListFactions(const std::string& filter, int maxCount)
{
    if (coop::GameWorldPtr() == 0 || !PlausiblePtr(coop::GameWorldPtr()->factionDirectory))
    {
        ErrorLog("[M1] factions: faction manager unavailable");
        return;
    }

    const lektor<Faction*>* all = coop::GameWorldPtr()->factionDirectory->allFactions();
    if (all == 0) { ErrorLog("[M1] factions: allFactions returned null"); return; }

    std::string want;
    for (size_t i = 0; i < filter.size(); ++i) want += (char)tolower((unsigned char)filter[i]);

    int shown = 0, total = 0;
    for (unsigned int i = 0; i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!PlausibleObject(f)) continue;
        ++total;
        const std::string& nm = f->getName();
        if (!want.empty())
        {
            std::string low;
            for (size_t k = 0; k < nm.size(); ++k) low += (char)tolower((unsigned char)nm[k]);
            if (low.find(want) == std::string::npos) continue;
        }
        if (shown < maxCount) { DebugLog("[M1] faction: '" + nm + "'"); ++shown; }
    }
    DebugLog("[M1] factions: " + S(shown) + " shown of " + S(total)
             + (want.empty() ? "" : (" matching '" + filter + "'")));
}

void ListTemplates(const std::string& filter, int maxCount)
{
    if (coop::GameWorldPtr() == 0) { ErrorLog("[M1] templates: no GameWorld"); return; }

    lektor<GameData*> list;
    coop::GameWorldPtr()->gamedata.listRecordsOfType(list, RECORD_CHARACTER);

    std::string want;
    for (size_t i = 0; i < filter.size(); ++i) want += (char)tolower((unsigned char)filter[i]);

    int shown = 0, total = 0;
    for (lektor<GameData*>::iterator it = list.begin(); it != list.end(); ++it)
    {
        GameData* d = *it;
        if (!PlausibleObject(d)) continue;
        ++total;
        const std::string& nm = d->name;
        if (!want.empty())
        {
            std::string low;
            for (size_t i = 0; i < nm.size(); ++i) low += (char)tolower((unsigned char)nm[i]);
            if (low.find(want) == std::string::npos) continue;
        }
        if (shown < maxCount)
        {
            DebugLog("[M1] template: '" + nm + "'");
            ++shown;
        }
    }
    DebugLog("[M1] templates: " + S(shown) + " shown of " + S(total)
             + " CHARACTER entries" + (want.empty() ? "" : (" matching '" + filter + "'")));
}

// F155 - see spawn.h. The missing comparison layer.
std::string BehaviourString(Character* c)
{
    if (c == 0 || !PlausibleObject(c)) return "goal=UNREADABLE";

    std::string goal = "?";
    int orders = -1;
    void* ai = GetCharacterAI(c);
    if (PlausibleObject(ai))
    {
        AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
        if (PlausibleObject(ts))
        {
            goal   = ts->describeCurrentGoal();
            orders = ts->hasPendingOrders() ? 1 : 0;
        }
    }

    Ogre::Vector3 p = c->worldPosition();
    return "goal='" + goal + "' orders=" + S(orders)
         + " spd=" + F1(c->currentMoveSpeed())
         + " pos=" + F1(p.x) + "," + F1(p.y) + "," + F1(p.z);
}

// P024 / F200 - IS THIS CHARACTER FIGHTING, AND DOES IT LOOK LIKE IT?
//
// The user watched the same fights this project scored as passing and reported that on the
// second instance the characters move to roughly the right places but show NO COMBAT
// ANIMATIONS and NO COMBAT INDICATION ON THEIR PORTRAITS. Every number in those runs was
// correct. Nothing measured animation, stance or UI state, so the suite agreed with itself
// and missed it entirely.
//
// This is the missing instrument. It reads the fields the engine itself uses:
//
//   combatModeActive (CombatClass+0x130) - the flag `_isInCombatMode()` returns, and the
//                                          one a combat portrait indicator would read
//   combatState      (CombatClass+0x1F0) - swordStateEnum; WHICH combat animation is chosen
//   nextMove         (+0x1F4)            - the state it is about to move to
//   _isAttacking     (+0x140)            - non-zero while a swing is in progress
//   currentTechnique (+0x150)            - the attack technique, null when not attacking
//   currentComboSection (+0x268)         - how far through that technique
//   isIdle           (AnimationClass+0xA5) - the animation layer's own idea of "doing nothing"
//
// EVERY FIELD IS A PLAIN READ. Nothing is called on the combat object and nothing is written.
// The state number is printed WITH ITS NAME - a bare "state=5" would be one more number whose
// meaning has to be looked up later, and this project has lost runs to exactly that.
const char* SwordStateName(int s)
{
    switch (s)
    {
        case 0:  return "SWORD_SWING";
        case 1:  return "BLOCK";
        case 2:  return "SWORD_REACT_GUARD";
        case 3:  return "SWORD_STARTING";
        case 4:  return "DECISION";
        case 5:  return "CIRCLE_MENACINGLY";
        case 6:  return "WAIT_MENACINGLY";
        case 7:  return "HESITATE";
        case 8:  return "STUMBLE";
        case 9:  return "COMBAT_FINISHED";
        case 10: return "SWORD_APPROACH_START";
        case 11: return "SWORD_APPROACH";
        default: return "UNKNOWN";
    }
}

std::string CombatString(Character* c)
{
    if (c == 0 || !PlausibleObject(c)) return "cc=UNREADABLE";

    // LESSON 3, and this one is not hypothetical. `Character::getCombat` disassembles to
    //     MOV RAX,[RCX+0x648]   ; CharBody* body
    //     MOV RAX,[RAX+0x8]     ; -> CombatClass
    //     RET
    // Three instructions and NO NULL CHECK of its own: a character whose `body` is null faults
    // inside the engine, in a probe, on the main thread. That is precisely the shape that cost
    // this project a crash in F117. So `body` is validated here, before the call - the guard
    // belongs on our side of every engine call that dereferences a member it does not check.
    void* body = *(void**)((char*)c + 0x648);
    if (!PlausibleObject(body)) return "cc=nobody";

    CombatClass* cc = c->getCombat();
    if (!PlausibleObject(cc))
    {
        // A character with no combat object at all is a real answer, not a probe failure -
        // it would explain the absent animations directly. Say which it is.
        return "cc=none mode=- state=- next=- atk=- tech=- combo=-";
    }

    const char* base = (const char*)cc;
    int   mode  = (int)*(const unsigned char*)(base + 0x130);
    int   state = *(const int*)(base + 0x1F0);
    int   next  = *(const int*)(base + 0x1F4);
    float atk   = *(const float*)(base + 0x140);
    const void* tech = *(const void* const*)(base + 0x150);
    int   combo = *(const int*)(base + 0x268);

    std::string idle = "?";
    std::string acting = "?";
    std::string actw = "?";
    void* an = *(void**)((char*)c + 0x448);
    if (PlausibleObject(an))
    {
        idle = S((int)*(const unsigned char*)((const char*)an + 0xA5));

        // P029: the gate `CombatClassAI::decisionState` actually tests. Called once per digest
        // sample per character, never per frame - it walks the animation layers, and a probe
        // that walks a list every frame for every character is the F189 runaway again.
        AnimationClassBase* ab = (AnimationClassBase*)an;
        acting = S(ab->isActionAnimating() ? 1 : 0);
        actw   = F1(ab->actionBlendWeight());
    }

    // F212 - IS THIS COMBAT BLOCK BEING TICKED AT ALL? The question that decides which fix for
    // P-15 is even possible, and it is answerable with two plain reads instead of a run spent
    // guessing.
    //
    // T064 showed the peer frozen at `state=3:SWORD_STARTING` for ten minutes. Two very
    // different things produce that, and they need opposite fixes:
    //
    //   (a) the block IS ticked but never enters combat mode  -> telling it to enter combat
    //       would work, because something is already driving the state machine;
    //   (b) the block is NEVER ticked at all                  -> setting the mode byte would
    //       change a portrait icon and animate nothing, and the fix has to be elsewhere.
    //
    // `frameTIME` (+0x168) is written by `CombatClass::update` every frame it runs, and
    // `stateTimer` (+0x14C) accumulates while a state is held. If either MOVES between two
    // samples on the peer, the block is being ticked and the answer is (a). If both are frozen
    // while the authority's move, it is (b).
    //
    // This is lesson 16 applied on purpose: test the EXPLANATION before building on it.
    float ft   = *(const float*)(base + 0x168);
    float stmr = *(const float*)(base + 0x14C);

    return "cc=ok mode=" + S(mode)
         + " ft=" + F1(ft)
         + " stmr=" + F1(stmr)
         + " state=" + S(state) + ":" + SwordStateName(state)
         + " next=" + S(next) + ":" + SwordStateName(next)
         + " atk=" + F1(atk)
         + " tech=" + S(tech != 0 ? 1 : 0)
         + " combo=" + S(combo)
         + " animIdle=" + idle
         // P029/F231 - `acting` is the flag the AI's own decision function checks BEFORE
         // choosing a move. If it reads 1 on the peer while the authority reads 0, the peer
         // never decides, and that would explain the pinned nextMove without any reference to
         // the suppression gate.
         + " acting=" + acting
         + " actW=" + actw;
}

// P025 / F200 - WHO IS ACTUALLY IN THIS WORLD?
//
// The user reported that the NPCs in the area do not match between the two instances. Nothing
// in this project has ever looked: every parity score to date covers the four to six characters
// we spawn ourselves, out of a populated world. That is a hole in the MEASUREMENT, not only in
// the code, and it is why the suite could report "parity" for sixty runs while the two screens
// showed different crowds.
//
// This walks the engine's own character update list and prints a digest built to be DIFFED
// LINE-FOR-LINE between the two instances: a total, how many of them are ours, and a histogram
// by race and by faction, both sorted so allocation order cannot create a false difference.
//
// It reads only. It does not replicate anything, and it is deliberately a COMMAND rather than a
// per-frame probe - a full world walk every frame is exactly the runaway F189 taught this
// project to stop writing.
void WorldCensus()
{
    if (!PlausiblePtr(coop::GameWorldPtr()))
    {
        ErrorLog("[P025] census: no GameWorld - nothing to count (this is a REFUSAL, not a"
                 " count of zero)");
        return;
    }

    const GameHashSet<Character*>::type& all = coop::GameWorldPtr()->activeCharacters();

    // A world walk is unbounded by nature; refuse an implausible size rather than iterating
    // into whatever a bad read produced.
    size_t n = all.size();
    if (n > 20000)
    {
        ErrorLog("[P025] census: update list size " + S((long long)n)
                 + " is implausible - refusing to walk it");
        return;
    }

    std::map<std::string, int> byRace;
    std::map<std::string, int> byFaction;
    long long total = 0, unreadable = 0, mine = 0;
    /* save1 (user approved a save/reload test, 2026-09-24): the OTHER player's characters in this world (the coop-peer
       faction) and how many of them the mod has registered. After a reload, copies restored from this game's save would
       read as `unregistered` beside the fresh copies the other game announces (investigations/kidnap-cages-save.md 2). */
    long long peerTotal = 0, peerRegistered = 0;

    // WHERE is this instance looking? The list is `&GameWorld[0x750]` - the characters the
    // engine is actively updating - and Kenshi streams the world by region, so two instances
    // standing in different places would legitimately be updating different people. Without a
    // reference position, a census difference could not be told apart from the two cameras
    // simply being somewhere else, and I would have reported a population defect that was only
    // a difference of viewpoint. The centroid of OUR OWN characters is that reference.
    double cx = 0.0, cz = 0.0; long long cn = 0;

    for (GameHashSet<Character*>::type::const_iterator it = all.begin();
         it != all.end(); ++it)
    {
        Character* c = *it;
        if (!PlausibleObject(c)) { ++unreadable; continue; }
        ++total;

        if (FindSpawnedUid(c) != 0)
        {
            ++mine;
            if (PlausibleObject(*(void**)((char*)c + 0x448)))
            {
                Ogre::Vector3 cp = c->worldPosition();
                cx += cp.x; cz += cp.z; ++cn;
            }
        }

        std::string race = "?";
        GameData* gd = c->getRecordDirect();
        if (PlausibleObject(gd)) race = gd->name;
        ++byRace[race];

        std::string fac = "?";
        Faction* f = c->getOwnerFactionDirect();
        if (PlausibleObject(f)) fac = f->getName();
        ++byFaction[fac];
        if (PlausibleObject(f) && IsPeerFaction(f))
        {
            ++peerTotal;
            if (FindSpawnedUid(c) != 0) ++peerRegistered;
        }
    }

    std::string races;
    for (std::map<std::string, int>::const_iterator r = byRace.begin(); r != byRace.end(); ++r)
        races += " " + r->first + "=" + S(r->second);

    std::string facs;
    for (std::map<std::string, int>::const_iterator f = byFaction.begin();
         f != byFaction.end(); ++f)
        facs += " " + f->first + "=" + S(f->second);

    // `mine` is printed beside the total on purpose: a census that counted only our own spawns
    // would look like a healthy number and mean nothing.
    std::string at = "n/a";
    if (cn > 0) at = F1((float)(cx / (double)cn)) + "," + F1((float)(cz / (double)cn));

    DebugLog("[P025] census total=" + S(total)
             + " ours=" + S(mine)
             + " notOurs=" + S(total - mine)
             + " unreadable=" + S(unreadable)
             + " oursCentroidXZ=" + at
             + " peer[total,registered,unregistered]=" + S(peerTotal) + "," + S(peerRegistered) + "," + S(peerTotal - peerRegistered)
             + " races:" + (races.empty() ? std::string(" none") : races));
    DebugLog("[P025] census factions:" + (facs.empty() ? std::string(" none") : facs));
}

// P031 / F261 - WHICH PEOPLE, not how many.
//
// P025 counts by race and faction, and that is exactly as far as it can go: it logs no
// per-character identity, so `Ninja Guard=8` on both instances could still be EIGHT DIFFERENT
// PEOPLE. Three runs of census data were re-read to establish that the stable group never
// diverges - and that stability is proven only IN AGGREGATE. The strongest fact P-16 has is
// therefore weaker than it looks, and nothing should be built on it until identities are
// compared directly.
//
// What this prints is a ROSTER: one line per character, keyed by the engine's own `hand`,
// sorted by that key so the two instances' output diffs line-for-line with no post-processing.
//
// WHY THE HANDLE IS THE RIGHT KEY, and this is the part that makes the probe worth building:
// `identity.h` records that save-loaded objects are re-registered from the save's own handle,
// so the same character in two processes carries the SAME handle - verified across two separate
// launches (T026/T028) - while objects the world creates at RUNTIME take theirs from
// `nextRecordId()` and need not agree. F261 measured a population that splits three ways along
// exactly that line: hand-placed characters never diverge, generated ones always do.
//
// So this roster is a direct test of a claim P-16 Option A's COST depends on. If the
// save-derived population matches by handle, it does not need replicating at all, and the bill
// is only the generated remainder (F261 put that at roughly 40 of ~130) rather than the whole
// world. If it does NOT match, the bill is every character and Option A is a much larger job.
// Either answer is worth having before a line of the fix is written.
//
// READ ONLY, and a COMMAND rather than a per-frame probe - a full world walk every frame is the
// runaway F189 taught this project not to write.
void WorldRoster()
{
    if (!PlausiblePtr(coop::GameWorldPtr()))
    {
        ErrorLog("[P031] roster: no GameWorld - nothing to list (this is a REFUSAL, not an"
                 " empty roster)");
        return;
    }

    const GameHashSet<Character*>::type& all = coop::GameWorldPtr()->activeCharacters();

    size_t n = all.size();
    if (n > 20000)
    {
        ErrorLog("[P031] roster: update list size " + S((long long)n)
                 + " is implausible - refusing to walk it");
        return;
    }

    // Sorted by handle string. Allocation order differs between processes by nature, so an
    // unsorted roster would show every line as "changed" and the real differences would be
    // invisible inside the noise - the same reason P025 sorts its histograms.
    std::map<std::string, std::string> byHandle;
    long long total = 0, unreadable = 0, mine = 0, noHandle = 0;

    for (GameHashSet<Character*>::type::const_iterator it = all.begin();
         it != all.end(); ++it)
    {
        Character* c = *it;
        if (!PlausibleObject(c)) { ++unreadable; continue; }
        ++total;

        ObjId id;
        std::string key;
        if (CaptureObjId(c, &id)) key = ObjIdString(id);
        else { key = "NOHANDLE"; ++noHandle; }

        std::string race = "?";
        GameData* gd = c->getRecordDirect();
        if (PlausibleObject(gd)) race = gd->name;

        std::string fac = "?";
        Faction* f = c->getOwnerFactionDirect();
        if (PlausibleObject(f)) fac = f->getName();

        bool ours = (FindSpawnedUid(c) != 0);
        if (ours) ++mine;

        // Position is read only when the animation object is present - the same guard P025
        // uses, because worldPosition dereferences it (F205 cost a crash by omitting a
        // null check on a double-deref accessor).
        std::string pos = "UNREADABLE";
        if (PlausibleObject(*(void**)((char*)c + 0x448)))
        {
            Ogre::Vector3 p = c->worldPosition();
            pos = F1(p.x) + "," + F1(p.y) + "," + F1(p.z);
        }

        // A duplicate key would silently overwrite, and "two characters share a handle" is
        // itself a finding - so make the collision visible instead of losing a row.
        std::string line = " race=" + race + " fac=" + fac + " pos=" + pos
                         + " ours=" + std::string(ours ? "1" : "0");
        if (byHandle.find(key) != byHandle.end())
            byHandle[key] += "  <-- DUPLICATE KEY, also:" + line;
        else
            byHandle[key] = line;
    }

    DebugLog("[P031] roster n=" + S(total)
             + " ours=" + S(mine)
             + " notOurs=" + S(total - mine)
             + " unreadable=" + S(unreadable)
             + " noHandle=" + S(noHandle)
             + " distinctHandles=" + S((long long)byHandle.size()));

    // `distinctHandles` beside `n` above is the collision check stated as a number: if they
    // disagree, handles are not unique in this world and every conclusion drawn from matching
    // them is void. That has to be visible in the run, not discovered later.
    for (std::map<std::string, std::string>::const_iterator r = byHandle.begin();
         r != byHandle.end(); ++r)
        DebugLog("[P031] r " + r->first + r->second);

    DebugLog("[P031] roster end");
}

void ReportSpawns()
{
    // T022 found this printed only uid:pointer, so a puppet that was NOT being driven had
    // no readable position anywhere - the executor had to re-arm the capture hook onto it
    // just to see where it was. A spawned object's live position is basic evidence; print it.
    std::stringstream ss;
    // F322. `despawnSent` is the AUTHORITY's side, `despawnApplied` the peer's - on a healthy link
    // the second should track the first. `despawnOffThread` is the falsification counter for F321's
    // one-run measurement that destruction is main-thread: if it is ever non-zero, that measurement
    // did not generalise and the send was correctly refused rather than corrupting the transport.
    ss << "[M1] REPORT spawned=" << S((long long)g_spawned.size())
       << " despawnSent=" << S(g_despawnSent)
       << " despawnApplied=" << S(g_despawnApplied)
       << " despawnUnknown=" << S(g_despawnUnknown)
       << " despawnNotOurs=" << S(g_despawnNotOurs)
       << " despawnOffThread=" << S(g_despawnOffThread)
       << " despawnNoUid=" << S(g_despawnNoUid)
       << " despawnRowFinished=" << S(g_despawnRowFinished) << " despawnQueuedOffThread=" << S((long long)g_despawnQueuedOffThread)   /* T-304 (2) */
       << " despawnQueueDrained=" << S(g_despawnQueueDrained) << " despawnQueueFull=" << S((long long)g_despawnQueueFull)
       << " copyDestroyRefused[eaten]=" << S((long long)g_copyDestroyRefused) << " eatenReArmed=" << S((long long)g_eatenReArmed)
       << " eatenLayoutOff=" << S((long long)g_eatenLayoutOff)
       << " spawnRefFallback=" << S(g_spawnRefFallback) << " spawnBuiltNoRef=" << S(g_spawnBuiltNoRef) << " spawnRefusedNoRef=" << S(g_spawnRefusedNoRef)   /* T-304 (3) */
       // F332. `destroyOurs - despawnSelfReentry - despawnUnload` is the number this project has
       // never had: how many characters THIS engine destroyed on its own account. T091 read
       // `destroyOurs=46` on the client and every one was our own call coming back round.
       // **The phenomenon it was cited for is still real** - T090's client recorded 15 with
       // `despawnApplied=0` and `despawnUnknown=0`, so those 15 cannot be self re-entry - but T091
       // did not measure it.
       << " despawnSelfReentry=" << S(g_despawnSelfReentry)
       << " despawnSendFailed=" << S(g_despawnSendFailed)
       << " despawnUnload=" << S(g_despawnUnload) << " despawnUnloadByReason=" << S(g_despawnUnloadByReason) << " removalReasons=" << RemovalReasonsToken()
       << " unloadRecv=" << S(g_unloadRecv) << " unloadApplied=" << S(g_unloadApplied) << " unloadUnknown=" << S(g_unloadUnknown)
       << " unloadWithdrawnTwin=" << S(g_unloadWithdrawnTwin) << " unloadWithdrawnPlayer=" << S(g_unloadWithdrawnPlayer)
       << " despawnKeptPlayer=" << S(g_despawnKeptPlayer)
       // review-session S6 - the link-drop cleanup, its own block. NONE of these is included in the
       // despawn/unload numbers above: those count what the AUTHORITY told us, and a dropped link
       // told us nothing. `peerGoneAbsent` is a claim we held no copy for (normal). `retireFailed`
       // is the one number that means a body or a platoon was left behind.
       << " | peerGoneDestroyed=" << S(g_peerGoneDestroyed)
       << " peerGoneWithdrawn=" << S(g_peerGoneWithdrawn)
       << " peerGoneAbsent=" << S(g_peerGoneAbsent)
       << " peerGoneDestroyFailed=" << S(g_peerGoneDestroyFailed)
       << " peerGoneRetired=" << S(g_peerGoneRetired) << " peerGoneRecreated=" << S(g_peerGoneRecreated)
       << " peerGoneRetireFailed=" << S(g_peerGoneRetireFailed)
       << " peerGoneRetireUnreadable=" << S(g_peerGoneRetireUnreadable)
       << " peerGoneRetireKeptMembers=" << S(g_peerGoneRetireKeptMembers)
       // F338 - the stale-uid path's own block, so `despawnSent` keeps the meaning T091 verified.
       << " | staleUidRetired=" << S(g_staleUidRetired)
       << " staleUidSent=" << S(g_staleUidSent)
       << " staleUidNotOurs=" << S(g_staleUidNotOurs)
       << " staleUidSendFailed=" << S(g_staleUidSendFailed)
       << " mirrorRebound=" << S(g_mirrorRebound)
       << " mirrorRebindRefused=" << S(g_mirrorRebindRefused)
       // mirror1 (crash T487): occupancy, refusals and releases. mirrorFullRefused counts every
       // registration refused for want of a row; spawnRefusedFull / twinRefusedFull / adoptRefusedFull
       // say which caller then refused the character. indexOrphanRebound MUST STAY ZERO.
       << " mirrorUsed=" << S(g_mirrorUsedNow) << "/" << S(kMaxMirror)
       << " mirrorHigh=" << S(g_mirrorHighWater)
       // T-354: a refused copy of another game's character is reported to its owner; the owner counts what it is told.
       << " notShown[sent,sendFailed,in,inNotMine,inBad]=" << S(g_notShownSent) << "," << S(g_notShownSendFailed) << ","
       << S(g_notShownIn) << "," << S(g_notShownInNotMine) << "," << S(g_notShownInBad)
       << " mirrorTestCap=" << S((long long)g_mirrorTestCap)
       // T-197 M4 (owner 156, 2026-09-29): measure the character-id mint rate before choosing the id width. count = ids this
       // process minted; perHour from the first mint (-1 in the first minute); spacePpm = parts per million of the counter used -
       // since M4 (owner 203) the 22-bit counter, 4,194,303 uids per seat for the world's life (coopuid::UidCounterMax).
       << " uidMint[count,perHour,spacePpm]=" << S(g_uidMint.minted) << ","
       << S((g_uidFirstMintMs != 0 && (DWORD)(::GetTickCount() - g_uidFirstMintMs) >= 60000u)
              ? (long long)((unsigned long long)g_uidMint.minted * 3600000ull / (DWORD)(::GetTickCount() - g_uidFirstMintMs)) : -1LL) << ","
       << S((long long)((unsigned long long)g_uidMint.count * 1000000ull / (unsigned long long)coopuid::UidCounterMax))
       // M4 (owner 203 / 205 A): uids NOT minted - not linked / not WELCOMEd on this link yet (retried), the seat spent and no free seat left.
       << " uidNoSlotDeferred=" << S(g_uidMint.noSlotDeferred)
       << " uidCounterExhausted=" << S(g_uidMint.counterExhausted)
       // M4 fold: no uid block on this link yet (retried); minted uids skipped because a row here already held them (must read
       // 0); the blocks: seat (-1 none), next counter, block end, the spare, asks sent, blocks taken / refused, bad answers,
       // FAILED answers, 1 when the notebook said EXHAUSTED, NO_SEAT answers.
       << " uidNoBlockDeferred=" << S(g_uidMint.noBlockDeferred) << " uidRowSkipped=" << S(g_uidRowSkipped)
       << " uidBlock[seat,next,hi,spareLo,spareHi,asks,taken,refused,badReply,failed,exhausted,noSeat]="
       << S(g_uidMint.seat == coopuid::UidNoSeat ? -1LL : (long long)g_uidMint.seat) << "," << S((long long)g_uidMint.curNext) << ","
       << S((long long)g_uidMint.curHi) << "," << S((long long)g_uidMint.nextLo) << "," << S((long long)g_uidMint.nextHi) << ","
       << S(g_uidMint.blockAsks) << "," << S(g_uidMint.blocksTaken) << "," << S(g_uidMint.blocksRefused) << ","
       << S(g_uidBlockBadReply) << "," << S(g_uidBlockFailed) << "," << S((long long)g_uidMint.exhausted) << "," << S(g_uidNoSeatAnswers)
       << " uidStale[refused,dropped]=" << S(g_uidMint.staleBlockRefused) << "," << S(g_uidMint.staleBlocksDropped)   // M4 fold 2: blocks of an earlier notebook link
       << " uidSweepTicksRefused=" << S(g_uidMint.sweepTicksRefused)   // M4 fold 2 (L-B): every sweep tick that could not mint
       << " mirrorFullRefused=" << S(g_mirrorFullRefused)
       << " mirrorOtherUidRefused=" << S(g_mirrorOtherUidRefused)   // mirror1 fold #6
       << " soakWatchFreed=" << S(g_soakWatchFreed) << " gateFreedWithRow=" << S(g_gateFreedWithRow)   // #1 #2
       << " adoptRefusedTables=" << S(g_adoptRefusedTables)   // #12
       << " mirrorReleased=" << S(g_mirrorReleased)
       << " mirrorReclaimed=" << S(g_mirrorReclaimed)
       << " indexTrimmed=" << S(g_indexTrimmed)
       << " indexOrphanRebound=" << S(g_indexOrphanRebound)
       << " spawnRefusedFull=" << S(g_spawnRefusedFull)
       << " twinRefusedFull=" << S(g_twinRefusedFull)
       << " adoptRefusedFull=" << S(AdoptRefusedFullCount())
       << " despawnDroppedStale=" << S(g_despawnDroppedStale)
       // E16 (review-p5i / F528): remote writes this game REFUSED in RemoteMayWrite - the split-brain guard's own number.
       // Non-zero means the peer sent us writes for characters we hold authorship of, or for uids whose claim is not its.
       << " remoteMayWriteRefused=" << S((long long)net::g_remoteMayWriteRefused)
       // O1 / O1-b (review-o1): the worker-thread owned-uid mirror (common/ownedmirror.h, 32768 slots, two buffers).
       // Live should equal the owned set; tombstones/empty are the published table's slots; rebuilds = tables the
       // main thread rebuilt and swapped in; refused = inserts that found no slot, all time (healed by the next
       // tick's rebuild unless live is at 32768 - then IsUidMineAnyThread answers "not mine" for the overflow).
       << " ownedMirrorLive=" << S((long long)net::OwnedMirrorStat(0))
       << " ownedMirrorTombstones=" << S((long long)net::OwnedMirrorStat(1))
       << " ownedMirrorEmpty=" << S((long long)net::OwnedMirrorStat(2))
       << " ownedMirrorRebuilds=" << S((long long)net::OwnedMirrorStat(3))
       << " ownedMirrorRefused=" << S((long long)net::OwnedMirrorStat(4))
       // R1-a / R1-a-b - the readiness wait. proneParked = proneAppliedAfterPark + proneDroppedRetired +
       // proneSuperseded + proneMooted + proneDroppedNotOwner + proneParkedNow. proneFlushBlocked is
       // passes skipped (not entries); appearanceWaitNotBuilt and clothingWaitAppearance count
       // entries entering a wait, once per uid per wait.
       << " ready[proneParked,proneAppliedAfterPark,proneDroppedRetired,proneSuperseded,proneMooted,"
          "proneDroppedNotOwner,proneParkedNow,proneFlushBlocked,appearanceWaitNotBuilt,clothingWaitAppearance]="
       << S(g_proneParked) << "," << S(g_proneAppliedAfterPark) << "," << S(g_proneDroppedRetired)
       << "," << S(g_proneParkSuperseded) << "," << S(g_proneParkMooted) << "," << S(g_proneDroppedNotOwner)
       << "," << S((long long)g_proneParkMap.size()) << "," << S(g_proneFlushBlocked)
       << "," << S(AppearanceWaitNotBuiltCount()) << "," << S(ClothingWaitAppearanceCount())
       // crash1 (T293 / F912): knockdowns parked for a queued body rebuild or the looks (a balance term beside proneParked),
       // and knockdowns written after the bounded looks wait ran out.
       << " crash1[proneParkedRebuild,proneLooksTimeout,latchHeld,knockHoldNow,knockHoldOldestMs]="
       << S(g_proneParkedRebuild) << "," << S(g_proneLooksTimeout) << "," << S(g_latchHeld)
       << "," << S(KnockHoldCount()) << "," << S(KnockHoldOldestMs())
       << " crash1c[knockHoldStuck]=" << S(g_knockHoldStuck);   // crash1c (R3c)
    for (std::map<unsigned int, RootObjectBase*>::const_iterator it = g_spawned.begin();
         it != g_spawned.end(); ++it)
    {
        ss << " | uid=" << S(it->first) << " obj=" << P(it->second);
        // F316 - THIS LOOP CRASHED THE HOST IN T086, at 98 uids, on the seventh `report`.
        // `PlausibleObject(c)` alone was never enough for `worldPosition`, and iterating
        // `g_spawned` directly bypassed the retirement check that `FindSpawned` performs.
        // `SafeReadPosition` does both, and says which of them refused.
        Ogre::Vector3 p;
        if (SafeReadPosition((Character*)it->second, &p))
            ss << " pos=" << F1(p.x) << "," << F1(p.y) << "," << F1(p.z);
        else if (IsRetiredObject(it->second))
            ss << " pos=RETIRED";
        else
            ss << " pos=UNREADABLE";
    }
    DebugLog(ss.str());
}

// --- Per-limb health readout ------------------------------------------------------------
//
// M3's actual acceptance criterion is "a hit on one instance produces matching per-limb
// health on the other", and until now this project has had no way to READ per-limb health -
// three runs were spent engineering a duel while the instrument for the real criterion did
// not exist (F113). This is that instrument.
//
// `MedicalSystem medical` is an inline member at Character+0x458 (class layout, not an RVA,
// so it is not subject to the F020 shift). Parts are enumerated by INDEX rather than by
// name, so nothing depends on guessing a bodypart string (F038).
//
// One line per character, all parts on it, so two instances can be diffed at a glance.
void ReportHealth(unsigned int uid)
{
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c))
    {
        DebugLog("[M3] health: no local object for uid " + S(uid));
        return;
    }

    MedicalSystem* med = (MedicalSystem*)((char*)c + 0x458);
    if (!PlausiblePtr(med))
    {
        DebugLog("[M3] health: medical unreadable for uid " + S(uid) + " (" + P(med) + ")");
        return;
    }

    int count = med->countBodyParts();
    if (count <= 0 || count > 32)
    {
        // Refuse and SAY WHAT WAS REFUSED - T018's lesson (F073): a silent refusal cannot be
        // told apart from a wrong offset.
        DebugLog("[M3] health: implausible part count " + S(count) + " for uid " + S(uid)
                 + " - offset or accessor wrong, nothing read");
        return;
    }

    std::string line = "[M3] health uid=" + S(uid) + " blood=" + F1(med->blood)
                     + " parts=" + S(count);
    for (int i = 0; i < count; ++i)
    {
        MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
        if (!PlausiblePtr(p)) { line += " | " + S(i) + ":UNREADABLE"; continue; }
        line += " | " + S(i)
              + ":t=" + S((int)p->partKind)
              + ",s=" + S((int)p->side)
              + ",f=" + F1(p->flesh)
              // F127: STUN is what the collapse test actually reads - `isCollapse` is
              // (flesh - stun) + jury < 0. It was invisible for six runs while we compared
              // `f=` and wondered why identical values gave opposite knockouts. `coll` is
              // the engine's own expression, precomputed so a reader does not have to.
              + ",stun=" + F1(p->stunDamage)
              + ",jury=" + F1(p->splintLevel)
              + ",coll=" + F1(p->flesh - p->stunDamage + p->splintLevel)
              + ",m=" + F1(p->maxHealthBase)
              // F131: the ceiling the engine actually clamps to is age * m * healthScale - wear.
              // Printed as its FACTORS and as the resulting bound, because six runs compared
              // `m=` and called it "the maximum" when it was one term of three - the same
              // mistake as reading `f=` and calling it the collapse test.
              + ",age=" + F1(p->age)
              + ",hpm=" + F1(p->healthScale)
              + ",ceil=" + F1(p->age * p->maxHealthBase * p->healthScale - p->limbWear)
              + ",pct=" + F1(p->fleshHealthFraction);
    }
    DebugLog(line);
}

// --- Authoritative per-limb health transfer (M3b) -----------------------------------------
//
// F119: replaying the hit EVENT can never make the two copies agree. The engine rolls the
// body part itself, per instance (HealthPartStatus carries hitWeight/hitWeightScale, and
// addWound takes a CutDirection, never a part index) - so T034's eight replicated hits
// wounded DISJOINT sets of parts on the two machines. What has to travel is the injury, not
// the punch.
//
// Design constraint (user, 2026-08-06): the peer must still SEE the attack, and the damage
// must land on the same spot, by a method visually indistinguishable from a natural hit.
// So the peer still PLAYS the hit through the engine - animation, stagger, reaction all
// happen - and the authority's per-limb values are written over the top in the same frame.
// The visible event is the engine's own; only the outcome is corrected.

// F127: the peer's engine RECOMPUTES collapse every tick from the per-part record -
// `MedicalSystem::isCollapse` (RVA 0x643020) is literally
//     (flesh - stunDamage) + splintLevel < 0
// We were sending `flesh` and `maxHealthBase` only, so both machines held BIT-FOR-BIT identical
// flesh and still reached opposite conclusions, because the term that dominates the
// comparison never crossed the wire. In Kenshi stun damage is what knocks a character out;
// cut damage is what kills them - we were replicating the wrong half of the injury.
//
// The design rule this cost us, stated once: REPLICATE THE STATE THE ENGINE READS, NOT THE
// CONCLUSION IT DRAWS. It recomputes the conclusion every tick and it wins.
//
// So the whole record travels, in the engine's own field order:
//   [0] flesh 0x40 · [1] stunDamage 0x44 · [2] bandageLevel 0x48
//   [3] splintLevel 0x4C · [4] limbWear 0x50 · [5] maxHealthBase 0x54
int SnapshotHealth(unsigned int uid, float* parts, int maxParts, float* blood)
{
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return 0;
    MedicalSystem* med = (MedicalSystem*)((char*)c + 0x458);
    if (!PlausiblePtr(med)) return 0;

    int count = med->countBodyParts();
    if (count <= 0 || count > maxParts) return 0;

    for (int i = 0; i < count; ++i)
    {
        MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
        if (!PlausiblePtr(p)) return 0;    // all or nothing - a partial record is worse
        float* o = parts + i * kPartFloats;
        o[0] = p->flesh;
        o[1] = p->stunDamage;
        o[2] = p->bandageLevel;
        o[3] = p->splintLevel;
        o[4] = p->limbWear;
        o[5] = p->maxHealthBase;
        // F131: `maxHealthBase` is only ONE of the three factors in the bound. Decompiling
        // `clampHealth` (0x644B10) shows the ceiling it clamps `flesh` against is
        //     age * maxHealthBase * healthScale - limbWear
        // so replicating `maxHealthBase` alone left two thirds of the bound behind. F120's
        // lesson, seventh occurrence - and the first one caught offline before a run.
        o[6] = p->age;
        o[7] = p->healthScale;
    }
    if (blood) *blood = med->blood;
    return count;
}

// --- PROBE P018 (ships WITH the F139 fix - lesson 7) --------------------------------------
//
// Removing our own `clampHealth()` call from the apply path only makes the two copies
// agree if the ENGINE does not clamp on its own. Whether it does is genuinely unknown (U-10):
// Ghidra shows no CALL xrefs to 0x644B10, but F060's rule is that xrefs must target the
// incremental-link thunk, so absence there proves nothing.
//
// So the question is answered from a live run instead, and by a SPECIFIC signature rather than
// "the value moved" - bleeding, healing and the medical tick all move `flesh` legitimately.
// The signature of THIS clamp is exact: we wrote a value ABOVE the ceiling, and the value now
// sitting there IS the ceiling. Nothing else produces that.
namespace {

const int kAppliedSlots = 16;
struct AppliedRec { unsigned int uid; int n; float flesh[32]; };
AppliedRec g_applied[kAppliedSlots] = {0};

const long long kClampLogLimit = 12;
long long g_clampLogged = 0;

AppliedRec* AppliedFor(unsigned int uid)
{
    int free = -1;
    for (int i = 0; i < kAppliedSlots; ++i)
    {
        if (g_applied[i].uid == uid) return &g_applied[i];
        if (g_applied[i].uid == 0 && free < 0) free = i;
    }
    if (free < 0) return 0;
    g_applied[free].uid = uid;
    g_applied[free].n   = 0;
    return &g_applied[free];
}

// Called at ENTRY, before this apply overwrites the evidence: does what the engine holds now
// differ from what we last wrote, in the specific way a clamp would?
void CheckForEngineClamp(unsigned int uid, MedicalSystem* med, int count)
{
    AppliedRec* rec = AppliedFor(uid);
    if (rec == 0 || rec->n != count) return;          // nothing to compare against yet

    for (int i = 0; i < count && i < 32; ++i)
    {
        MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
        if (!PlausiblePtr(p)) return;
        float ceiling = p->age * p->maxHealthBase * p->healthScale - p->limbWear;
        float wrote   = rec->flesh[i];
        if (wrote <= ceiling + 0.01f) continue;       // we never wrote above the bound

        if (g_clampLogged >= kClampLogLimit) return;
        ++g_clampLogged;
        bool clamped = (p->flesh > ceiling - 0.01f && p->flesh < ceiling + 0.01f);
        DebugLog(std::string("[P018] uid=") + S(uid) + " part=" + S(i)
                 + " we wrote f=" + F1(wrote) + " over ceil=" + F1(ceiling)
                 + "; engine now holds f=" + F1(p->flesh)
                 + (clamped ? "  <-- ENGINE CLAMPED IT (U-10: it does clamp on its own)"
                            : "  <-- UNCHANGED (U-10: the engine does NOT clamp; parity holds)"));
    }
}

// heal1: per copy, the bandageLevel / splintLevel ApplyHealth last WROTE per part (what a local medic's work is measured against),
// and a short hold on values a medic here raised, so the owner's next STATE - sent before it heard - does not erase them.
struct TreatTrack
{
    int n;
    float band[cooptreat::kTreatMaxParts], jury[cooptreat::kTreatMaxParts];
    float holdBand[cooptreat::kTreatMaxParts], holdJury[cooptreat::kTreatMaxParts];
    DWORD holdAt;    // when the hold was set (valid while hold)
    bool hold;
    DWORD sentAt;    // last TREAT for this copy (valid while sentOnce)
    bool sentOnce;
    bool based;      // review-heal1 1: band/jury hold a real baseline (set by a completed ApplyHealth)
};
std::map<unsigned int, TreatTrack> g_treat;
const DWORD kTreatHoldMs = 4000;
const DWORD kTreatMinGapMs = 500;
const float kTreatEps = 0.01f;
long long g_treatSent = 0, g_treatSendFailed = 0, g_treatHeldParts = 0, g_treatHoldCleared = 0, g_treatHoldExpired = 0;
long long g_treatRecv = 0, g_treatApplied = 0, g_treatNoChange = 0, g_treatNotMine = 0, g_treatMismatch = 0, g_treatDropped = 0;
long long g_treatLogged = 0;

void NoteApplied(unsigned int uid, const float* parts, int count)
{
    AppliedRec* rec = AppliedFor(uid);
    if (rec == 0) return;
    rec->n = count < 32 ? count : 32;
    for (int i = 0; i < rec->n; ++i) rec->flesh[i] = parts[i * kPartFloats];
}

} // namespace

bool ApplyHealth(unsigned int uid, const float* parts, int count, float blood)
{
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return false;
    MedicalSystem* med = (MedicalSystem*)((char*)c + 0x458);
    if (!PlausiblePtr(med)) return false;

    int local = med->countBodyParts();
    if (local != count)
    {
        // The two copies disagree about their own anatomy - writing by index would put the
        // wound on the wrong limb, which is worse than not writing it. Refuse and SAY SO.
        ErrorLog("[M3b] health apply refused for uid=" + S(uid) + ": peer has " + S(count)
                 + " parts, we have " + S(local));
        return false;
    }

    // P018 runs BEFORE the write, because the write is what destroys the evidence.
    CheckForEngineClamp(uid, med, count);

    // heal1: the copy's treatment record, and whether a local medic's raise is still held against this STATE
    TreatTrack* tr = 0;
    if (count <= (int)cooptreat::kTreatMaxParts)
    {
        TreatTrack& t = g_treat[uid];
        if (t.n != count) { t.n = count; t.hold = false; t.sentOnce = false; t.based = false; }
        tr = &t;
        if (tr->hold && ::GetTickCount() - tr->holdAt >= kTreatHoldMs) { tr->hold = false; ++g_treatHoldExpired; }
        if (tr->hold)   // the owner has caught up on every part: the hold has done its job
        {
            bool caught = true;
            for (int i = 0; i < count && caught; ++i)
            {
                const float* in = parts + i * kPartFloats;
                if (in[2] + kTreatEps < tr->holdBand[i] || in[3] + kTreatEps < tr->holdJury[i]) caught = false;
            }
            if (caught) { tr->hold = false; ++g_treatHoldCleared; }
        }
    }

    for (int i = 0; i < count; ++i)
    {
        MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
        if (!PlausiblePtr(p)) return false;
        const float* in = parts + i * kPartFloats;
        float band = in[2], jury = in[3];
        if (tr != 0 && tr->hold)
        {
            if (tr->holdBand[i] > band) { band = tr->holdBand[i]; ++g_treatHeldParts; }
            if (tr->holdJury[i] > jury) { jury = tr->holdJury[i]; ++g_treatHeldParts; }
        }
        // review-heal1 1: a medic here raised this part since our last write and TreatTick has not reported it yet - keep the
        // copy's own value, and leave the baseline where it was so TreatTick still sees the raise and sends it.
        const bool rawB = tr != 0 && tr->based && p->bandageLevel > tr->band[i] + kTreatEps;
        const bool rawJ = tr != 0 && tr->based && p->splintLevel > tr->jury[i] + kTreatEps;
        if (rawB && p->bandageLevel > band) { band = p->bandageLevel; ++g_treatHeldParts; }
        if (rawJ && p->splintLevel > jury) { jury = p->splintLevel; ++g_treatHeldParts; }
        // THE WHOLE CEILING FIRST (F120, corrected by F131): a value written above the
        // local maximum is clamped straight back by clampHealth, so every factor
        // in the bound has to land before the value. The bound is
        //     age * maxHealthBase * healthScale - limbWear
        // and until now only `maxHealthBase` travelled, which made the fix for F120 partial in
        // exactly the way F120 itself was partial.
        p->age         = in[6];
        p->healthScale      = in[7];
        p->maxHealthBase  = in[5];
        p->limbWear  = in[4];
        p->flesh       = in[0];
        p->stunDamage   = in[1];
        p->bandageLevel   = band;    // heal1: in[2], or a local medic's raise still held
        p->splintLevel = jury;    // heal1: in[3], likewise
        p->recomputeHealth();
        // review-heal1 3: the baseline is what the part holds AFTER the engine's own recalculation; review-heal1 1: not moved
        // up over an unreported local raise
        if (tr != 0)
        {
            if (!rawB) tr->band[i] = p->bandageLevel;
            if (!rawJ) tr->jury[i] = p->splintLevel;
        }
    }
    if (tr != 0) tr->based = true;
    med->blood = blood;

    // F139 / PARITY P-12 - `clampHealth()` USED TO BE CALLED HERE AND NO LONGER IS.
    //
    // T043 measured instance A holding `f=61.47` on part 5 for ~360 s while B held `f=5.00`,
    // both reporting `m=5.0 ceil=5.0`. A 56.47-point divergence on one limb, the largest this
    // project has ever measured - and WE made it. This call ran on the PEER for every inbound
    // HIT and every STATE, i.e. constantly; on the authority it ran only when the test harness
    // wounded something. So we sent the authority's unclamped value, wrote it here, and then
    // clamped it straight back - manufacturing a permanent difference out of two copies that
    // were agreeing.
    //
    // An invariant enforced on ONE side does not produce consistency. It produces divergence,
    // and it looks like a safety feature the whole time it is doing so.
    //
    // Of the three ways to make it symmetric, this is the least invasive: the peer is a MIRROR,
    // so it should not be held to a rule the authority is not held to. Clamping the authority
    // as well was the alternative, and it was rejected because it would delete 56 points of
    // limb health from the owning player's own character to fix a display difference - changing
    // gameplay to fix a mirror is the wrong way round.
    //
    // F120 is not undone by this: the ceiling still travels, and it still has to, because the
    // ENGINE may clamp on its own (unresolved - U-10; Ghidra shows no CALL xrefs to 0x644B10,
    // but F060's rule says xrefs must target the incremental-link thunk, so absence proves
    // nothing). P018 below is how that question gets answered from a live run instead.
    NoteApplied(uid, parts, count);
    return true;
}

namespace { int GetupIsRagdoll(::Character* c); }   // T-178 crawl1: G1's guarded inRagdoll read (defined below)
namespace { int SneakRead(const void* c); }   // the guarded stealth-mode read (Character +0xD4), defined below

int SnapshotState(unsigned int uid, float* parts, int maxParts, float* blood,
                  int* prone, int* dead, unsigned int* latchBits, float* nextKnockoutAt, float* koTimer)
{
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return 0;
    int n = SnapshotHealth(uid, parts, maxParts, blood);
    if (n == 0) return 0;
    if (prone) *prone = (int)c->poseState();
    if (dead)  *dead  = c->hasDied() ? 1 : 0;
    // F147: the latch block travels with the state it explains.
    float nk = 0.0f, kt = 0.0f;
    unsigned int bits = SnapshotLatch(c, &nk, &kt);
    // T-178 crawl1 (protocol 86): the character's own inRagdoll rides as one more bit of the word (getupcrawl.h
    // kStateOwnerRagdollBit) - set when it reads 1 or cannot be read, so the copy's 'up from the ragdoll' needs
    // positive evidence. ApplyLatch reads only bits 0-5.
    if (GetupIsRagdoll(c) != 0) bits |= coopgetup::kStateOwnerRagdollBit;
    bits = coopsneak::WithSneak(bits, SneakRead(c));   // the stealth mode rides as bit 7 (sneakwire.h); unreadable = not sneaking
    if (latchBits)  *latchBits  = bits;
    if (nextKnockoutAt) *nextKnockoutAt = nk;
    if (koTimer)    *koTimer    = kt;
    return n;
}

// ---- K1 (read-carry 2026-09-22): CARRY REPLICATION - engine facts and the state both halves share ----------------
// T253 (Confirmed): 7 of 9 lost prone copies were owners knocked out and CARRIED on the other game. The carrier's
// STATE names the body it carries; the other game repeats the engine's own pickupObject / dropCarriedObject on its
// copies. OUT OF SCOPE: beds and cages (Character +0x2F8 inSomething: 1 bed, 2 cage) - putting a body in one needs
// both games to agree on WHICH building, and nothing matches buildings for that yet.
unsigned long long kPickupObjectRva = 0; static coop::AddrReg kPickupObjectRva_reg("PickupObject", &kPickupObjectRva);   /* P8h: the address table fills this. Steam_1.0.65 0x5CF500 */   /* Character::pickupObject(Character* who), this = carrier */
unsigned long long kDropCarriedObjectRva = 0; static coop::AddrReg kDropCarriedObjectRva_reg("DropCarriedObject", &kDropCarriedObjectRva);   /* P8h: the address table fills this. Steam_1.0.65 0x5CD750 */   /* Character::dropCarriedObject(bool ragdollHim, bool removeOnly) */

namespace {

const size_t kCarrierFlagOff  = 0x348;   // bool isCarryingSomething (pickupObject sets it, refuses while set)
const size_t kCarryHandIdOff  = 0x388;   // hand carryingObject at +0x380; its five id fields at +0x8..+0x1C
const size_t kOwnHandIdOff    = 0x60;    // hand handle at +0x58; the same five fields
const size_t kHandIdBytes     = 20;      // type, container, containerStamp, index, serial (game/hand.h)
const size_t kBeingCarriedOff = 0x3D4;   // bool _isBeingCarried (getPickedUp 0x5CE300 sets it)
const int    kCarryCapCount   = 3;       // at most 3 pick-ups per carrier ...
const DWORD  kCarryCapMs      = 30000;   // ... per 30 s (carryRateCapped)
const long long kCarryLogLimit = 16;     // the first 16 applies / drops are logged
const long long kCarryBodyMineLogLimit = 8;   // review-k1 item 3: the first 8 skipped bodies are logged

typedef void (*PickupObjectFn)(::Character* carrier, ::Character* who);
typedef void (*DropCarriedObjectFn)(::Character* carrier, bool ragdollHim, bool removeOnly);

struct CarryWant
{
    unsigned int body;    // what the owner's STATE says the copy carries (0 = nothing)
    unsigned int peer;    // who said so - the ownership guard is asked again before acting
    bool notedNotReady;   // one count per wish, not one per frame
    bool notedNoBody;
    bool notedCapped;
    bool notedConflict;   // review-k1 item 2: one carryConflictMine per wish
    bool notedBodyMine;   // review-k1 item 3: one carrySkippedBodyMine per wish
};
std::map<unsigned int, CarryWant>    g_carryWant;   // copy carrier uid -> its owner's word
std::map<unsigned int, unsigned int> g_carryLast;   // owned carrier uid -> non-zero carryingUid last pushed
struct CarryCap { DWORD t[kCarryCapCount]; int n; };
std::map<unsigned int, CarryCap>     g_carryCap;
// arrest1 (docs/design-arrest.md 3): this game's OWN bodies a copy carrier picked up here, body uid -> carrier uid. When
// that carry ends here (the engine let go, or the carrier put it down), OwnCarryWatch tells the carrier's
// game once (MSG_CARRY_BREAK) so its carrier puts its copy down too. A carry the carrier's own game ended (its put-down
// or swap, applied here) is forgotten without a break (review-arrest1 MEDIUM 2).
struct OwnCarry { unsigned int carrier; DWORD awakeSince; };   // awakeSince: no longer used (owner 333 a retired the awake drop); kept 0
std::map<unsigned int, OwnCarry> g_ownCarried;
// arrest1: MSG_CARRY_BREAK for a carrier THIS game drives, carrier uid -> the body uid; CarryBreakApply acts on it.
std::map<unsigned int, unsigned int> g_carryBreakIn;
const size_t kCarryInSomethingOff = 0x2F8;   // Character : int inSomething (1 bed, 2 cage) - not picked up from either

long long g_carrySent = 0;              // owner: a carry edge (pick-up or put-down) pushed as STATE at once
long long g_carryApplied = 0;           // copy: pickupObject left the body carried (+0x3D4 set)
long long g_carryDropped = 0;           // copy: dropCarriedObject called (the owner put it down, or a swap)
long long g_carryNotReady = 0;          // copy: a wish waiting on CharacterBuilt / a local carrier we cannot name
long long g_carryNoLocalBody = 0;       // copy: the named body is not a registered character here
long long g_carryRefusedByEngine = 0;   // copy: pickupObject returned with +0x3D4 still clear
long long g_carryRateCapped = 0;        // copy: a wish held back by the 3-per-30-s cap
long long g_carryConflictMine = 0;      // copy: the carrier to drop / swap / hand over is one THIS game drives - left alone (review-k1 item 2)
long long g_carrySkippedBodyMine = 0;   // copy: the named body is one THIS game drives - no pickupObject (review-k1 item 3)
long long g_carryBodyMineLogged = 0;
long long g_proneStandSkippedUnconscious = 0;   // ApplyState: prone=0 not written over an owner latch 'unconscious, timer > 0' (review-k1 item 6)
long long g_proneSkippedCarried = 0;    // ApplyState / ProneParkTick: prone NOT written while the copy is carried
long long g_carryLogged = 0;
long long g_carryOwnApplied = 0;        // arrest1: this game's own (helpless) body picked up by a copy carrier here
long long g_carryOwnAwake = 0;          // arrest1: an own body NOT picked up - awake, standing, or in a bed / cage here
long long g_carryBreakSent = 0;         // arrest1: MSG_CARRY_BREAK sent (an own body's carry ended here, or refused)
long long g_carryBreakApplied = 0;      // arrest1: our carrier put a copy down on the body's game's word
long long g_carryBreakStale = 0;        // arrest1: a break for a carry that had already ended here - nothing to do
long long g_carryBreakRefused = 0;      // arrest1: a break from a peer that does not drive the body, or for a carrier we do not drive
long long g_carryBreakDropped = 0;      // arrest1: malformed, or arrived while the world was being rebuilt
long long g_carryOwnWokeDropped = 0;    // RETIRED by owner decision 333 a (2026-10-01): nothing puts an awake carried body down any more - always 0, kept so the report token keeps its shape
// A conversation's carried-person hand-over (speech.cpp CarryHandOverNote): this game's own carrier handed a person to another
// game's NPC; when that NPC's owner says the NPC carries the person, the carrier lets go here so the NPC's copy can take it.
std::vector<cooptalk::TalkHandOver> g_handOvers;
long long g_handOverNoted = 0;          // marks recorded
long long g_handOverLetGo = 0;          // our carrier let go and the NPC's copy picked the person up
long long g_handOverExpired = 0;        // marks that ran out unused
long long g_handOverFull = 0;           // the oldest mark dropped at the cap
long long g_handOverEnded = 0;          // marks dropped because our carrier no longer carries the person (the carry ended another way)

bool CarryReadByte(const void* c, size_t off, unsigned char* out)
{
    __try { *out = *((const unsigned char*)c + off); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CarryReadInt(const void* c, size_t off, int* out)
{
    __try { *out = *(const int*)((const char*)c + off); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CarryReadHandId(const void* c, size_t off, unsigned char* out)
{
    __try { std::memcpy(out, (const char*)c + off, kHandIdBytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CarryFlag(::Character* c)
{
    unsigned char b = 0;
    return PlausibleObject(c) && CarryReadByte(c, kCarrierFlagOff, &b) && b != 0;
}

bool BeingCarried(::Character* c)
{
    unsigned char b = 0;
    return PlausibleObject(c) && CarryReadByte(c, kBeingCarriedOff, &b) && b != 0;
}

// The registered character whose own hand id is `id`, 0 = none.
unsigned int UidByHandId(const unsigned char* id, ::Character** out)
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int u = 0;
        ::Character* ch = 0;
        if (!MirrorSlot(i, &u, &ch) || !PlausibleObject(ch)) continue;
        unsigned char own[kHandIdBytes];
        if (!CarryReadHandId(ch, kOwnHandIdOff, own)) continue;
        if (std::memcmp(own, id, kHandIdBytes) == 0) { if (out) *out = ch; return u; }
    }
    return 0;
}

// Does `carrier` carry `body` (flag set and its carryingObject hand names body's own hand)?
bool Carries(::Character* carrier, ::Character* body)
{
    if (!CarryFlag(carrier) || !PlausibleObject(body)) return false;
    unsigned char a[kHandIdBytes], b[kHandIdBytes];
    return CarryReadHandId(carrier, kCarryHandIdOff, a) && CarryReadHandId(body, kOwnHandIdOff, b)
        && std::memcmp(a, b, kHandIdBytes) == 0;
}

// The registered character carrying `body` here, 0 = none we can name.
::Character* LocalCarrierOf(::Character* body, unsigned int* carrierUid)
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int u = 0;
        ::Character* ch = 0;
        if (!MirrorSlot(i, &u, &ch) || ch == body) continue;
        if (Carries(ch, body)) { if (carrierUid) *carrierUid = u; return ch; }
    }
    return 0;
}

// 3 pick-ups per carrier per 30 s. Records the pick-up when it allows one.
bool CarryCapAllows(unsigned int carrier, DWORD now)
{
    std::map<unsigned int, CarryCap>::iterator it = g_carryCap.find(carrier);
    if (it == g_carryCap.end())
    {
        CarryCap z; z.n = 0;
        for (int k = 0; k < kCarryCapCount; ++k) z.t[k] = 0;
        it = g_carryCap.insert(std::make_pair(carrier, z)).first;
    }
    CarryCap& cap = it->second;
    int kept = 0;
    for (int k = 0; k < cap.n; ++k)
        if (now - cap.t[k] < kCarryCapMs) cap.t[kept++] = cap.t[k];
    cap.n = kept;
    if (cap.n >= kCarryCapCount) return false;
    cap.t[cap.n++] = now;
    return true;
}

void CarryLog(const char* what, unsigned int carrier, unsigned int body)
{
    if (g_carryLogged >= kCarryLogLimit) return;
    ++g_carryLogged;
    DebugLog(std::string("[K1] carry ") + what + " carrier uid=" + S(carrier) + " body uid=" + S(body));
}

// arrest1: may a copy carrier pick up this game's OWN body here? Only a helpless one - knocked out, lying or limp (crash1's
// CopyDowned: prone 2..4, ragdoll, unconscious or a wake-up clock) - that is not in a bed or cage. Unreadable = no.
bool OwnBodyMayBeCarried(::Character* body)
{
    int ins = 0;
    if (!CarryReadInt(body, kCarryInSomethingOff, &ins) || ins != 0) return false;
    return CopyDowned(body) == 1;
}

// The engine lets a knocked-out person be picked up out of a bed. An own character that is helpless (as above) and in a BED
// (never a cage) is taken out of it here first - setBedMode off at the safe point (BedOutForCarryDrain), as every bed write
// here - and the copy carrier's pick-up follows on a later tick, once it lies free.
std::set<unsigned int> g_bedOutForCarry;   // own uids a copy carrier wants to pick up out of a bed: taken out at the safe point
long long g_bedOutForCarryQueued = 0;      // a take-out asked for (again each tick while it is still in the bed)
long long g_bedOutForCarryDone = 0;        // taken out (setBedMode off verified)
long long g_bedOutForCarryFailed = 0;      // setBedMode off faulted or left it in the bed
// A take-out that keeps failing is not asked for without end: after kBedOutForCarryTries failures in a row (each within
// kBedOutForCarryForgetMs of the last) the carry falls through to the carry break below; a later carry tries afresh.
const int kBedOutForCarryTries = 5;
const DWORD kBedOutForCarryForgetMs = 60000;
struct BedOutFails { int n; DWORD lastAt; bool counted; };
std::map<unsigned int, BedOutFails> g_bedOutForCarryFails;
long long g_bedOutForCarryGaveUp = 0;      // a take-out failed kBedOutForCarryTries times: the carry is broken instead
bool BedOutForCarryGivenUp(unsigned int uid)
{
    std::map<unsigned int, BedOutFails>::iterator f = g_bedOutForCarryFails.find(uid);
    if (f == g_bedOutForCarryFails.end()) return false;
    if (::GetTickCount() - f->second.lastAt >= kBedOutForCarryForgetMs) { g_bedOutForCarryFails.erase(f); return false; }
    if (f->second.n < kBedOutForCarryTries) return false;
    if (!f->second.counted) { f->second.counted = true; ++g_bedOutForCarryGaveUp; }
    return true;
}
bool OwnBodyInBedMayBeCarried(::Character* body)
{
    int ins = 0;
    if (!CarryReadInt(body, kCarryInSomethingOff, &ins) || ins != 1) return false;
    return CopyDowned(body) == 1;
}

// arrest1: an own body whose carry by a copy carrier has ended here -> MSG_CARRY_BREAK once, then forgotten.
// Owner decision 333 a (2026-10-01; decision 293, the authority rule): while the carry lasts, the mod does nothing - awake
// or not, the body's own game (the engine) decides whether it stays carried; the real game keeps a waking carried
// character carried (the owner). The old review-arrest1 MEDIUM 1 rule (put an own body down 2 s after it woke) is removed:
// it changed the authority game's behaviour.
void OwnCarryWatch(DropCarriedObjectFn drop)
{
    (void)drop;
    std::map<unsigned int, OwnCarry>::iterator it = g_ownCarried.begin();
    while (it != g_ownCarried.end())
    {
        const unsigned int bodyUid = it->first, carrierUid = it->second.carrier;
        ::Character* body = FindSpawned(bodyUid);
        ::Character* cc = FindSpawned(carrierUid);
        const bool mine = net::IsUidMine(bodyUid);
        if (body != 0 && cc != 0 && mine && Carries(cc, body)) { ++it; continue; }   // still carried here: the engine decides (owner 333 a)
        if (body != 0 && mine && net::SendCarryBreak(bodyUid, carrierUid))
        {
            ++g_carryBreakSent;
            CarryLog("ended here (break sent)", carrierUid, bodyUid);
        }
        g_ownCarried.erase(it++);
    }
}

// arrest1: our carrier puts its copy of the body down on the body's game's word (MSG_CARRY_BREAK).
void CarryBreakApply(DropCarriedObjectFn drop)
{
    for (std::map<unsigned int, unsigned int>::iterator it = g_carryBreakIn.begin(); it != g_carryBreakIn.end(); ++it)
    {
        const unsigned int carrierUid = it->first, bodyUid = it->second;
        ::Character* cc = FindSpawned(carrierUid);
        ::Character* body = FindSpawned(bodyUid);
        if (cc == 0 || body == 0 || !PlausibleObject(cc) || !net::IsUidMine(carrierUid) || !Carries(cc, body))
        { ++g_carryBreakStale; continue; }
        drop(cc, false, false);   // review-arrest1 LOW 6: a full drop, no ragdoll - the body is usually awake on its game
        ++g_carryBreakApplied;
        CarryLog("drop (the body's game let go)", carrierUid, bodyUid);
    }
    g_carryBreakIn.clear();
}

// The hand-over mark that lets this game's own `heldBy` give `body` to `taker`'s copy now, -1 none.
int HandOverFind(unsigned int body, unsigned int heldBy, unsigned int taker, DWORD now)
{
    for (size_t i = 0; i < g_handOvers.size(); ++i)
        if (cooptalk::TalkHandOverLetsGo(g_handOvers[i], body, heldBy, taker, (unsigned long)now)) return (int)i;
    return -1;
}

// Marks whose carry already ended another way (the person's own game broke it, the player put it down) go quietly; marks that
// ran out go with a line.
void HandOverSweep(DWORD now)
{
    for (size_t i = 0; i < g_handOvers.size(); )
    {
        ::Character* const cc = FindSpawned(g_handOvers[i].carrier);
        ::Character* const bc = FindSpawned(g_handOvers[i].body);
        if (cc == 0 || bc == 0 || !Carries(cc, bc)) { ++g_handOverEnded; g_handOvers.erase(g_handOvers.begin() + i); continue; }
        if (cooptalk::TalkHandOverExpired(g_handOvers[i], (unsigned long)now))
        {
            ++g_handOverExpired;
            DebugLog("[K1] hand-over mark ran out: body uid=" + S(g_handOvers[i].body) + " carrier uid=" + S(g_handOvers[i].carrier)
                     + " NPC uid=" + S(g_handOvers[i].taker) + " - the NPC's copy never came to carry the person here");
            g_handOvers.erase(g_handOvers.begin() + i);
        }
        else ++i;
    }
}

// ApplyState's half: remember the owner's word; CarryApplyTick acts on it (latest wins).
void NoteCarryWant(unsigned int carrier, ::Character* c, unsigned int body, unsigned int peer)
{
    std::map<unsigned int, CarryWant>::iterator it = g_carryWant.find(carrier);
    if (body == 0 && !CarryFlag(c)) { if (it != g_carryWant.end()) g_carryWant.erase(it); return; }
    if (it == g_carryWant.end())
    {
        CarryWant w; w.body = body; w.peer = peer; w.notedNotReady = w.notedNoBody = w.notedCapped = w.notedConflict = w.notedBodyMine = false;
        g_carryWant.insert(std::make_pair(carrier, w));
        return;
    }
    if (it->second.body != body)
    { it->second.notedNotReady = it->second.notedNoBody = it->second.notedCapped = it->second.notedConflict = it->second.notedBodyMine = false; }
    it->second.body = body;
    it->second.peer = peer;
}

}   // namespace (K1)

// ---- R3 (read-ragdoll 2026-09-22): a downed body's RESTING position is shared --------------------------------------
// A ragdolled character's position lives only in its physics bodies: getPosition 0x5CDBF0 returns AnimationClass +0x98
// while inRagdoll 0x7D08A0. The ragdoll is AnimationClass +0x2E8 (getRagdoll 0x5C4B00): +0x08 its bodies, +0x10
// settled (freeze 0x7D13D0, from the settle detector 0x7D18A0), +0x59 active, +0x5A carried. The ONLY engine call that
// moves a live ragdoll is translate 0x7D1580(ragdoll, const Vector3* delta) - every limb body shifted,
// setRagdollNavmeshSafePos 0x65DC30, +0x10 = 1 - made by the below-ground rescue 0x7D38D0, which runs every tick from
// threadSafeRagdollUpdateUT 0x5B76F0 (inside the engine's ragdoll pass, on the main thread - a crash dump's stack; counted as
// ragdollPass[offMain] when not). Read (read-ragdoll 2026-09-22 + disassembly
// 2026-09-22: 0x7D38D0 is void(RagdollClass*) - rbx = rcx, one argument; 0x7D07B0 is
// AnimationClass::notifyRagdollNeedsAnUpdate(), `if (this+0x2E8) (this+0x2E8)->+0x1C = 1`).
// So: the owner sends where its ragdoll settled (STATE rest block, session 48); the copy's main thread posts the move
// into a lock-free table (src/common/ragdollrest.h); the detour on 0x7D38D0 makes it with the engine's own translate.
unsigned long long kRagdollRescueRva = 0; static coop::AddrReg kRagdollRescueRva_reg("RagdollRescue", &kRagdollRescueRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7D38D0 */   /* RagdollClass below-ground rescue, void(RagdollClass*) - hooked */
unsigned long long kRagdollTranslateRva = 0; static coop::AddrReg kRagdollTranslateRva_reg("RagdollTranslate", &kRagdollTranslateRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7D1580 */   /* RagdollClass translate(const Vector3* delta) */
unsigned long long kNotifyRagdollNeedsAnUpdateRva = 0; static coop::AddrReg kNotifyRagdollNeedsAnUpdateRva_reg("NotifyRagdollNeedsAnUpdate", &kNotifyRagdollNeedsAnUpdateRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7D07B0 */   /* AnimationClass::notifyRagdollNeedsAnUpdate() */
// R3-c (T256): 0x7D38D0 follows its translate with 0x7D3060(ragdoll, 0, *(float*)0x167B308) - the ragdoll tick,
// which copies the root body (0x7D08F0) onto the scene node. 0x167B308 is .rdata, the image's constant 1.0f.
unsigned long long kRagdollTickRva = 0; static coop::AddrReg kRagdollTickRva_reg("RagdollTick", &kRagdollTickRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7D3060 */   /* RagdollClass tick void(RagdollClass*, bool, float) */
unsigned long long kRagdollTickArgRva = 0; static coop::AddrReg kRagdollTickArgRva_reg("RagdollTickArg", &kRagdollTickArgRva);   /* P8h: the address table fills this. Steam_1.0.65 0x167B308 */   /* .rdata float 1.0f - the tick argument 0x7D38D0 passes */

namespace {

const size_t kR3AnimOff        = 0x448;   // Character -> AnimationClass*
const size_t kR3AnimPosOff     = 0x98;    // AnimationClass : Vector3 - getPosition while ragdolled
const size_t kR3AnimRagdollOff = 0x2E8;   // AnimationClass -> RagdollClass* (getRagdoll 0x5C4B00)
const size_t kR3InSomethingOff = 0x2F8;   // Character : int inSomething (1 bed, 2 cage)
const size_t kRagBodiesOff     = 0x08;    // RagdollClass -> its limb bodies (translate walks them)
const size_t kRagSettledOff    = 0x10;    // RagdollClass : bool settled (freeze 0x7D13D0; translate sets it)
const size_t kRagActiveOff     = 0x59;    // RagdollClass : bool active
const size_t kRagCarriedOff    = 0x5A;    // RagdollClass : bool carried
const float  kRestResendDist   = 20.0f;   // owner: a settled body that moved this far is sent again
const float  kRestApplyMin     = 2.0f;    // copy: a move shorter than this is not made
const float  kRestLandTol      = 2.0f;    // copy: within this of the owner's rest = landed
const float  kRestSameEps      = 0.5f;    // copy: the same rest re-sent by a periodic STATE is not a new update
const DWORD  kRestPendingMs    = 5000;    // copy: a move no detour took in 5 s is withdrawn (restMissed)
const DWORD  kRestJudgeMs      = 10000;   // R3-d: a made move whose anim +0x98 is not within 2 by then is MISSED
const long long kRestLogLimit  = 16;      // the first 16 applies are logged

typedef void (*RagdollRescueFn)(void* ragdoll);
typedef void (*RagdollTranslateFn)(void* ragdoll, const float* delta);   // const Ogre::Vector3*
typedef void (*NotifyRagdollFn)(void* anim);
typedef void (*RagdollTickFn)(void* ragdoll, unsigned char flag, float arg);   // 0x7D3060: dl stored as a byte, xmm2

RagdollRescueFn     g_origRagdollRescue = 0;
RagdollTranslateFn  g_ragdollTranslate = 0;
NotifyRagdollFn     g_notifyRagdoll = 0;
RagdollTickFn       g_ragdollTick = 0;         // R3-c: 0x7D3060
const float*        g_ragdollTickArg = 0;      // R3-c: 0x167B308, read at each call as 0x7D38D0 reads it
volatile LONG       g_restHookArmed = 0;
cooprest::RestTable g_restTable;          // static, zero - nothing allocated

struct RagRead { void* anim; void* rag; int carried; int inSomething; int active; int ragCarried; int settled; int hasBodies; float pos[3]; };

struct RestOwn { float x, y, z; };
struct RestWant
{
    float x, y, z;         // the owner's rest position
    unsigned int peer;     // who said so - the ownership guard is asked again before acting
    bool attempted;        // one attempt per owner rest update
    bool queued;           // posted to g_restTable, not yet verified
    void* rag;             // the ragdoll it was posted for
    DWORD queuedAt;
    float deltaLen;
    long steps;            // decision 61: the hook makes the move in this many steps
    bool stale;            // R3-b item 1: the queued move is superseded - withdrawn (confirmed), never judged
    bool dropped;          // R3-b item 1: no wish remains - the record goes once its move is confirmed out of the table
    bool judging;          // R3-d: every step made - anim +0x98 is polled each main-thread frame for the verdict
    DWORD judgeAt;         // R3-d: when judging began (the 10 s timeout)
    long judgeFrames;      // R3-d: main-thread frames polled so far
    bool notedCarried, notedNotReady, notedNotSettled;   // one count per wish, not one per frame
};
std::map<unsigned int, RestOwn>  g_restOwn;    // owned uid -> rest position last sent (present = settled, qualifying)
std::map<unsigned int, RestWant> g_restWant;   // copy uid -> its owner's rest position

long long g_restSent = 0;               // owner: a settle flip / a > 20 move pushed as STATE at once
long long g_restRecorded = 0;           // copy: a new owner rest position recorded
volatile LONG64 g_restApplied = 0;      // copy: the detour made the LAST step of a move with translate 0x7D1580 (physics thread)
volatile LONG64 g_restAbortedCarried = 0;   // R3-b item 2: the detour abandoned a move - the live ragdoll was carried (+0x5A) at a step
volatile LONG64 g_restAbortedInactive = 0;  // R3-b item 2: the detour abandoned a move - the live ragdoll was not active (+0x59) / unreadable
volatile LONG64 g_restNodeRefreshed = 0;    // R3-c: the detour ran the ragdoll tick 0x7D3060 after a translate (physics thread)
long long g_restSlid = 0;               // copy: a finished move that took > 1 step (decision 61: slid, not jumped)
long long g_restLanded = 0;             // copy: AnimationClass +0x98 came within 2 (horizontal) of the owner's rest within 10 s of the last step
long long g_restMissed = 0;             // copy: not within 2 (horizontal) 10 s after the move, or never taken (5 s), or the table was full
long long g_restJudgedGone = 0;         // R3-d: judging ended early - despawned / no longer ours / no longer ragdolled / carried / superseded
long long g_restLandFramesMax = 0;      // R3-d: the most main-thread frames a landing took
long long g_restLandFrames[4] = { 0, 0, 0, 0 };   // R3-d: landings in <= 1, <= 10, <= 60, > 60 frames
long long g_restJudgeLogged = 0;
long long g_restSkippedCarried = 0;     // copy: a wish waiting while the copy is carried (+0x3D4 / +0x5A) or in a bed/cage (+0x2F8)
long long g_restSkippedNotReady = 0;    // copy: a wish waiting on CharacterBuilt / an active ragdoll with bodies
long long g_restSkippedNotSettled = 0;  // copy: a wish waiting for the copy's ragdoll to settle (+0x10)
long long g_restLogged = 0;

bool RestFinite(float v) { return v == v && v > -1.0e7f && v < 1.0e7f; }

float RestDist(const float* p, float x, float y, float z)
{
    const float dx = p[0] - x, dy = p[1] - y, dz = p[2] - z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// R3-d: the horizontal distance (x, z; y is up) - the landed test.
float RestDistXZ(const float* p, float x, float z)
{
    const float dx = p[0] - x, dz = p[2] - z;
    return sqrtf(dx * dx + dz * dz);
}

std::string RestF(float v)
{
    char b[32];
    sprintf_s(b, sizeof(b), "%.2f", v);
    return b;
}

// Every field the rest code reads, behind one fault guard. No engine call. False = unreadable / not finite.
bool ReadRagFields(const void* c, RagRead* o)
{
    o->anim = 0; o->rag = 0;
    o->carried = o->inSomething = o->active = o->ragCarried = o->settled = o->hasBodies = 0;
    o->pos[0] = o->pos[1] = o->pos[2] = 0.0f;
    __try
    {
        o->carried     = *((const unsigned char*)c + kBeingCarriedOff);
        o->inSomething = *(const int*)((const char*)c + kR3InSomethingOff);
        o->anim        = *(void* const*)((const char*)c + kR3AnimOff);
        if (o->anim == 0) return false;
        const float* p = (const float*)((const char*)o->anim + kR3AnimPosOff);
        o->pos[0] = p[0]; o->pos[1] = p[1]; o->pos[2] = p[2];
        o->rag = *(void* const*)((const char*)o->anim + kR3AnimRagdollOff);
        if (o->rag != 0)
        {
            const unsigned char* r = (const unsigned char*)o->rag;
            o->hasBodies  = (*(void* const*)(r + kRagBodiesOff) != 0) ? 1 : 0;
            o->settled    = r[kRagSettledOff] != 0;
            o->active     = r[kRagActiveOff] != 0;
            o->ragCarried = r[kRagCarriedOff] != 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return RestFinite(o->pos[0]) && RestFinite(o->pos[1]) && RestFinite(o->pos[2]);
}

// Physics thread: does `anim` still own `rag`? (A recycled ragdoll address must not notify a stale anim.)
bool AnimHoldsRagdoll(void* anim, void* rag)
{
    __try { return *(void* const*)((const char*)anim + kR3AnimRagdollOff) == rag; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// R3-b (review-r3 item 2), physics thread: 0 = the live ragdoll may be stepped, 1 = carried (+0x5A), 2 = not active
// (+0x59) or unreadable. Asked before EACH step: the owner side of a carry (K1) is never nudged.
int RagStepVeto(const void* rag)
{
    __try
    {
        const unsigned char* r = (const unsigned char*)rag;
        if (r[kRagCarriedOff] != 0) return 1;
        return (r[kRagActiveOff] != 0) ? 0 : 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}

// 0x7D38D0 - the engine's own moment for moving a ragdoll. PHYSICS/UPDATE THREAD: no allocation, no lock, no log.
void detour_ragdollRescue(void* rag)
{
    if (g_restTable.pending != 0 && rag != 0 && g_ragdollTranslate != 0)
    {
        void* anim = 0;
        float step[3] = { 0.0f, 0.0f, 0.0f };
        const int slot = cooprest::RestTake(&g_restTable, rag, &anim, step);   // decision 61: ONE step of the slide
        if (slot >= 0)
        {
            const int veto = RagStepVeto(rag);   // R3-b item 2: re-checked on the live ragdoll before this step
            if (veto != 0)
            {
                cooprest::RestAbandon(&g_restTable, slot);   // the rest of the slide is never made
                InterlockedIncrement64(veto == 1 ? &g_restAbortedCarried : &g_restAbortedInactive);
            }
            else
            {
                g_ragdollTranslate(rag, step);   // every limb body shifted, navmesh-safe pos, +0x10 = 1
                // R3-c: as 0x7D38D0 does after ITS translate - the tick copies the moved root body onto the scene node
                // (a settled ragdoll's node is otherwise not re-copied: T256, 19 of 24 moves left behind).
                if (g_ragdollTick != 0 && g_ragdollTickArg != 0)
                {
                    g_ragdollTick(rag, 0, *g_ragdollTickArg);
                    InterlockedIncrement64(&g_restNodeRefreshed);
                }
                if (anim != 0 && g_notifyRagdoll != 0 && AnimHoldsRagdoll(anim, rag)) g_notifyRagdoll(anim);
                if (cooprest::RestDone(&g_restTable, slot)) InterlockedIncrement64(&g_restApplied);   // the last step
            }
        }
    }
    g_origRagdollRescue(rag);
}

// R3-b (review-r3 item 1): true = no move of this wish is in the table any more (withdrawn, or it finished). False =
// a step is being made right now (RestWithdraw lost to the taker): the record keeps its ragdoll key and its timeout
// and the caller retries on a later tick - the record is never dropped while its move can still be made.
bool RestWithdrawWant(RestWant& w)
{
    if (!w.queued) return true;
    if (cooprest::RestWithdraw(&g_restTable, w.rag) || !cooprest::RestIsPending(&g_restTable, w.rag))
    { w.queued = false; return true; }
    return false;
}

enum { kRestVerdictLanded = 0, kRestVerdictMissed = 1, kRestVerdictGone = 2 };

// R3-d: the one verdict on a made move; the first 16 are logged with the frames polled and the horizontal distance.
void RestJudgeEnd(unsigned int uid, RestWant& w, int verdict, float off, const char* what)
{
    w.judging = false;
    const long long f = (long long)w.judgeFrames;
    if (verdict == kRestVerdictLanded)
    {
        ++g_restLanded;
        if (f > g_restLandFramesMax) g_restLandFramesMax = f;
        ++g_restLandFrames[f <= 1 ? 0 : (f <= 10 ? 1 : (f <= 60 ? 2 : 3))];
    }
    else if (verdict == kRestVerdictMissed) ++g_restMissed;
    else ++g_restJudgedGone;
    if (g_restJudgeLogged >= kRestLogLimit) return;
    ++g_restJudgeLogged;
    DebugLog("[R3] rest judged " + std::string(what) + " uid=" + S(uid) + " frames=" + S(f)
             + (off >= 0.0f ? " off=" + RestF(off) : std::string(" off=?"))
             + " delta=" + RestF(w.deltaLen) + " steps=" + S((long long)w.steps));
}

// ApplyState (main thread): the owner's rest position for `uid`, 0 = none.
void NoteRestWant(unsigned int uid, const float* rest, unsigned int fromPeer)
{
    std::map<unsigned int, RestWant>::iterator it = g_restWant.find(uid);
    const bool have = rest != 0 && RestFinite(rest[0]) && RestFinite(rest[1]) && RestFinite(rest[2]);
    if (!have)
    {
        // R3-b item 1: erased only once its move is confirmed out of the table; otherwise RestApplyTick retries
        if (it != g_restWant.end() && it->second.judging) RestJudgeEnd(uid, it->second, kRestVerdictGone, -1.0f, "GONE (no owner rest any more)");
        if (it != g_restWant.end()) { if (RestWithdrawWant(it->second)) g_restWant.erase(it); else it->second.dropped = true; }
        return;
    }
    if (it != g_restWant.end() && RestDist(rest, it->second.x, it->second.y, it->second.z) < kRestSameEps)
    { it->second.peer = fromPeer; it->second.dropped = false; return; }   // the same rest, re-sent by a periodic STATE
    if (it != g_restWant.end() && it->second.judging) RestJudgeEnd(uid, it->second, kRestVerdictGone, -1.0f, "GONE (superseded by a new owner rest)");
    // R3-b item 1: a move still in the table for the old rest stays on the record (stale) until its withdraw is
    // confirmed; the new rest is posted on a later tick - never a second move for the same ragdoll.
    const bool inFlight = it != g_restWant.end() && !RestWithdrawWant(it->second);
    RestWant w;
    w.x = rest[0]; w.y = rest[1]; w.z = rest[2]; w.peer = fromPeer;
    w.attempted = false; w.queued = false; w.rag = 0; w.queuedAt = 0; w.deltaLen = 0.0f; w.steps = 1;
    w.stale = false; w.dropped = false; w.judging = false; w.judgeAt = 0; w.judgeFrames = 0;
    w.notedCarried = false; w.notedNotReady = false; w.notedNotSettled = false;
    if (inFlight)
    {
        w.queued = true; w.stale = true; w.rag = it->second.rag; w.queuedAt = it->second.queuedAt;
        w.steps = it->second.steps; w.deltaLen = it->second.deltaLen;
    }
    g_restWant[uid] = w;
    ++g_restRecorded;
}

void RestLog(unsigned int uid, const RestWant& w, float off, const char* what)
{
    if (g_restLogged >= kRestLogLimit) return;
    ++g_restLogged;
    DebugLog("[R3] rest " + std::string(what) + " uid=" + S(uid) + " delta=" + RestF(w.deltaLen) + " steps=" + S((long long)w.steps)
             + (off >= 0.0f ? " off=" + RestF(off) : std::string(" off=?")));
}

}   // namespace (R3 private)

// ---- G1 (read-getup 2026-09-23, T261): a copy whose owner got up, still a ragdoll, gets one AI decision pulse -----
// T261: after an owner gets up (STATE prone 4 -> 0, unconscious latch 0, no KO timer) the COPY stayed a ragdoll for
// minutes. Read: setProneState 0x5C7390 and the per-tick 0x5CF620 only QUEUE AI task 0x41 'Getting up' (0x5078F0);
// adopting it needs the decision pass 0x510820, which the copy gate (ai_spike.cpp) lets through only for a pulse. The
// get-up action 0x33C9B0 -> 0x5C5F60 -> 0x5CB2D0(c,0,1) is the only thing that ends a knockout ragdoll. So the copy
// gets PulseCharacterQuiet - one decision pass, the engine's own - at most once per 2 s, up to 5 per get-up episode.
// Never writes the KO timer (C2) or prone; never acts on an owned uid.
// G1-b (review-g1): a pulse only when the copy ITSELF can get up (its own medical +0x161 / +0x164 and prone +0xE0 all 0,
// what 0x5CF620 / canGetUpWakeUp 0x598190 read); a pulse not spent is withdrawn (G1-c: when the state it was armed for
// changes, cap 3 s - G1-b used a 500 ms timer); ~1 s after a spent one, a goal other than 'Getting up' / 'Waking up'
// has the copy's goals cleared (F247 dropGoals, no pass); at most 10 pulses per uid per 60 s; SpawnWorldTeardown
// forgets every watch.
// G1-c (T265: 37 of 56 pulses were withdrawn unspent at 500 ms - the copy's decision pass 0x510820 had not come round;
// uid 335544587 lay 20 s while its owner walked): a pulse stays armed until the detour spends it or the state it was
// armed for changes, read live every tick - the owner no longer up (or up again since: upAt moved), the copy no longer
// a ragdoll, the uid ours - with a 3 s safety cap (under kGetupDoneMs). A pulse withdrawn unspent refunds its try and
// never arms the give-up judgement; the 60 s rate ring keeps it. GetupPulseTick learns of a spend each tick, from
// PulseArmedFor (read-only) reading 0 or WithdrawPulseFor losing the race (0), and buckets the armed-to-spent delay.
// G1-c fold (review-g1c): seven main-thread sites arm ONE flag per AI, so G1 acts only on its own pulse - the slot's
// arm generation, recorded at arming, must be unchanged; if not, G1 settles its pulse 'superseded' (a missing slot:
// 'ungated'), with no refund and no rollback of goals it did not buy, and it never arms over another site's pulse
// (getupBusy). Only a STATE-change withdrawal refunds its try, never the 3 s cap; the 2 s gap is on nextPulseAt, which
// no refund clears; 5 unspent pulses per episode is the budget, and running out is that episode's one getupGaveUp.
// While armed, every arming-time predicate is asked again. Fates: spent + withdrawnState + expired + superseded +
// ungated + watchRemoved = getupPulsed, less any pulse armed at the moment of the report.
namespace {

// T-178 crawl1: no wait after the owner's 'up' STATE (was 1.5 s) - 'up' now includes the owner's own inRagdoll 0
const DWORD     kGetupEveryMs   = 2000;   // at most one pulse per copy per 2 s
const int       kGetupMaxTries  = 5;      // pulses per get-up episode
const DWORD     kGetupDoneMs    = 5000;   // the copy stopped being a ragdoll within 5 s of a pulse = getupDone
const DWORD     kGetupAfterMs   = 1000;   // the AFTER half of a logged pulse is read ~1 s later
const long long kGetupLogLimit  = 16;     // the first 16 pulses are logged
const DWORD     kGetupExpireMs  = 3000;   // G1-c: SAFETY CAP only - a pulse unspent this long after arming is withdrawn
                                         // (a change of the state it was armed for withdraws it sooner); < kGetupDoneMs
const DWORD     kGetupJudgeGapMs = 500;   // G1-c: a spent pulse's goal is read no sooner than this after the spend is seen
const int       kGetupUnspentBudget = 5;  // G1-c fold M1: unspent pulses per episode; running out = that episode's give-up
const DWORD     kGetupRollbackMs = 1000;  // G1-b item 2: the goal a spent pulse chose is read this long after arming
const int       kGetupQuiesceMs = 20;     // G1-b item 2: bounded wait for a pass in flight before dropGoals (combat kNcQuiesceMs)
const long long kGetupClearLogLimit = 8;  // G1-b item 2: the first 8 goal clears are logged
const int       kGetupRateCap   = 10;     // G1-b item 4: at most this many pulses per uid ...
const DWORD     kGetupRateWinMs = 60000;  // ... in any 60 s, whatever the owner's up/down edges do
const size_t    kGetupMedicalOff = 0x458; // G1-b item 1: Character's embedded MedicalSystem (combat.cpp kMedicalOff)

struct GetupWatch
{
    unsigned int peer;     // who sent the STATE - the ownership guard is asked again before a pulse
    int prone;             // the owner's LAST STATE
    bool ownerUp;          // that STATE says up: prone < 3, unconscious latch 0, no KO timer
    DWORD upAt;            // when the owner's STATE first said up, this episode
    int tries;             // pulses this episode
    DWORD lastPulseAt;     // 0 = none outstanding (cleared once getupDone / getupGaveUp is judged)
    bool notedNotGated;    // one getupNotGated per episode
    bool logPending;       // a logged pulse whose AFTER half is still to be printed
    DWORD logAt;
    long long logNo;
    std::string logBefore;
    bool logArmed;         // G1-c: the logged pulse is still armed - its AFTER half waits for its fate
    std::string logFate;   // G1-c: spent(Nms) / withdrawn(why) / expired(Nms) / superseded / ungated / watchRemoved
    bool notedNotReady;    // G1-b: one getupNotReadyLocal per episode
    DWORD armedAt;         // G1-b: 0 = none; else an armed pulse, checked every tick (G1-c) until spent or withdrawn
    DWORD armedUpAt;       // G1-c: upAt when that pulse was armed - a different upAt is a new episode: withdraw
    long armedGen;         // G1-c fold H1: the slot's arm generation when G1 armed - changed = someone else's pulse
    DWORD nextPulseAt;     // G1-c fold M1: no pulse before this (the 2 s gap); 0 = none; no refund clears it
    int unspent;           // G1-c fold M1: pulses this episode that were never seen spent
    bool gaveUp;           // G1-c fold M1: this episode's getupGaveUp is counted
    bool notedBusy;        // G1-c fold H1: one getupBusy per stretch of another site's pulse being armed
    DWORD rollbackAt;      // G1-b: 0 = none; else a SPENT pulse (armed then) whose chosen goal is read ~1 s after arming
    DWORD recent[kGetupRateCap];   // G1-b: the last 10 pulse times for this uid, a ring
    int recentNext;
    bool notedCapped;      // G1-b: one getupRateCapped per cap hit
    bool pulseCrawl;       // T-178 crawl1: the outstanding / last pulse was armed for an owner crawling (prone 2)
};
std::map<unsigned int, GetupWatch> g_getup;   // copy uid -> its owner's last get-up relevant STATE

long long g_getupPulsed = 0;     // copy: PulseCharacterQuiet armed for a copy whose owner is up while it is a ragdoll
long long g_getupDone = 0;       // copy: it stopped being a ragdoll within 5 s of a pulse
long long g_getupGaveUp = 0;     // copy: 5 pulses and still a ragdoll 5 s after the last, or (G1-c fold) 5 unspent; once per episode
long long g_getupNotGated = 0;   // copy: would have pulsed, but its decisions are not gated (PulseCharacterQuiet false)
long long g_getupLogged = 0;
long long g_getupNotReadyLocal = 0;  // G1-b: owner up, copy a ragdoll, but the copy's own medical +0x161/+0x164 or prone +0xE0 not 0 - waited
long long g_getupPulseExpired = 0;   // G1-b / G1-c: a pulse still unspent at the 3 s cap (kGetupExpireMs) - withdrawn
long long g_getupWithdrawnState = 0; // G1-c: withdrawn unspent because its state changed (owner not up / new upAt, not a ragdoll, uid ours)
long long g_getupSpent = 0;          // G1-c: pulses the detour spent, as GetupPulseTick learns it
long long g_getupSpentLe500 = 0;     // G1-c: ... armed-to-spent <= 500 ms (cumulative: Le1500 includes these)
long long g_getupSpentLe1500 = 0;    // G1-c: ... <= 1500 ms
long long g_getupSpentLe3000 = 0;    // G1-c: ... <= 3000 ms (getupSpent - this = spent after the cap, in the race)
long long g_getupSuperseded = 0;     // G1-c fold H1/L1: another arming (or a new slot) since G1's - settled, no refund, no rollback
long long g_getupUngated = 0;        // G1-c fold L3: the copy's AI no longer gated (slot gone) while G1's pulse was armed
long long g_getupWatchRemoved = 0;   // G1-c fold L3: the watch was forgotten (despawn / teardown) with G1's pulse armed
long long g_getupBusy = 0;           // G1-c fold H1: another site's pulse was armed - G1 waited instead of arming
long long g_getupGoalCleared = 0;    // G1-b: ~1 s after a spent pulse the goal was not 'Getting up'/'Waking up' - dropGoals (F247)
long long g_getupRateCapped = 0;     // G1-b: a pulse refused by the 10 per uid per 60 s cap
long long g_getupClearLogged = 0;
long long g_getupCrawlArmed = 0;         // T-178 crawl1: G1 pulses armed for a copy whose owner crawls (prone 2)
long long g_getupCrawlDone = 0;          // ... the copy stopped being a ragdoll within 5 s of such a pulse
long long g_getupCrawlNotReadyLocal = 0; // ... episodes that waited: the copy itself could not get up into prone 2
long long g_getupCrawlRolledBack = 0;    // ... spent crawl pulses whose chosen goal was cleared (not a get-up goal)
long long g_ownRagPushed = 0;            // T-178 crawl1: STATEs pushed at once because an OWNED character's inRagdoll changed
std::map<unsigned int, unsigned char> g_ownRagBit;   // owned uid -> its inRagdoll bit as RestWatchTick last read it

// ApplyState (main thread): the owner's STATE, as far as getting up is concerned.
void NoteGetupState(unsigned int uid, ::Character* c, int prone, unsigned int latchBits, float koTimer, unsigned int fromPeer)
{
    // review-k1 item 6: 'standing but unconscious' (prone 0, latch unconscious / KO timer running) is being carried,
    // not getting up - it never qualifies.
    // T-178 crawl1 (H050): up = the owner's OWN inRagdoll bit 0 (protocol 86), prone 0 or 2, no unconscious latch, no
    // wake-up clock, not carried (the copy follows its owner's carry, K1) and not posed (the owner's pose word, POSE) -
    // coopgetup::OwnerUpFromRagdoll. It replaces 'prone < 3' plus the 1.5 s wait that guessed the owner had got up.
    const bool up = coopgetup::OwnerUpFromRagdoll(prone, (latchBits & coopgetup::kStateOwnerRagdollBit) != 0,
                                                  (latchBits & coop::kLatchUnconcious) != 0, koTimer > 0.0f,
                                                  BeingCarried(c), PoseHoldsCopy(uid));
    std::map<unsigned int, GetupWatch>::iterator it = g_getup.find(uid);
    if (it == g_getup.end())
    {
        GetupWatch n;   // every field set - no reliance on value-initialisation (VS2010)
        n.peer = fromPeer; n.prone = prone; n.ownerUp = false; n.upAt = 0; n.tries = 0; n.lastPulseAt = 0;
        n.notedNotGated = false; n.logPending = false; n.logAt = 0; n.logNo = 0;
        n.notedNotReady = false; n.armedAt = 0; n.rollbackAt = 0; n.recentNext = 0; n.notedCapped = false;
        n.armedUpAt = 0; n.logArmed = false;
        n.armedGen = 0; n.nextPulseAt = 0; n.unspent = 0; n.gaveUp = false; n.notedBusy = false;
        n.pulseCrawl = false;   // T-178 crawl1
        for (int k = 0; k < kGetupRateCap; ++k) n.recent[k] = 0;
        it = g_getup.insert(std::make_pair(uid, n)).first;
    }
    GetupWatch& w = it->second;
    if (up && !w.ownerUp)
    {
        w.upAt = ::GetTickCount(); w.tries = 0; w.lastPulseAt = 0; w.notedNotGated = false; w.notedNotReady = false;
        w.nextPulseAt = 0; w.unspent = 0; w.gaveUp = false; w.notedBusy = false;   // G1-c fold: a new episode
        if (!w.logPending) { w.logAt = 0; w.logNo = 0; }
    }
    w.ownerUp = up; w.prone = prone; w.peer = fromPeer;
}

// inRagdoll 0x7D08A0 double-dereferences the AnimationClass (F357): the pointer is checked and the call SEH-wrapped.
// 1 = ragdoll, 0 = not, -1 = unreadable (no action).
int GetupIsRagdoll(::Character* c)
{
    void* anim = 0;
    __try { anim = *(void* const*)((const char*)c + kR3AnimOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!PlausibleObject(anim)) return -1;
    __try { return c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Character +0x278 isCurrentlyGettingUp (byte); -1 = unreadable.
int GetupFlag(const void* c)
{
    __try { return (int)*((const unsigned char*)c + 0x278); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// G1-b item 1: can the COPY itself get up - what 0x5CF620 / canGetUpWakeUp 0x598190 read before 'Getting up' 0x41 is
// queued: its own medical +0x161 (unconscious) and +0x164 both 0, and its prone (+0xE0) 0. The owner being up is not
// enough. 1 = yes, 0 = not yet, -1 = unreadable.
// T-178 crawl1 (H050): the copy's prone must EQUAL the owner's last STATE prone and be 0 or 2 - a crippled copy
// (prone 2) adopts 'Waking up' 0x34, the order its own 0x5CF620 queues (coopgetup::CopyReadyToGetUp).
int GetupLocalReady(const void* c, int ownerProne)
{
    __try
    {
        const unsigned char uncon = *((const unsigned char*)c + kGetupMedicalOff + 0x161);
        const unsigned char m164  = *((const unsigned char*)c + kGetupMedicalOff + 0x164);   // dead (Character +0x5BC)
        const int prone = *(const int*)((const char*)c + 0xE0);
        return coopgetup::CopyReadyToGetUp(prone, ownerProne, uncon, m164) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// G1-b item 2: the copy's current goal, bare, from BehaviourString; "" = unreadable.
std::string GetupGoal(::Character* c)
{
    const std::string b = BehaviourString(c);
    const std::string head = "goal='";
    if (b.compare(0, head.size(), head) != 0) return std::string();
    const size_t end = b.find("' orders=", head.size());
    if (end == std::string::npos) return std::string();
    const std::string g = b.substr(head.size(), end - head.size());
    return (g == "?") ? std::string() : g;
}

// G1-b item 2: the F247 lever (replicate.cpp ClearAttackOrder) - the engine's own AITaskSytem::dropGoals, with NO
// decision pass: a pass is the opportunity the AI used to choose for itself (T075). Goals only - the orders are
// combat's (ClearAttackOrder), and are not touched here.
bool GetupClearGoals(::Character* c)
{
    void* ai = GetCharacterAI(c);
    if (!PlausibleObject(ai)) return false;
    AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObject(ts)) return false;
    ts->dropGoals();
    return true;
}

// G1-c: record the fate of the armed pulse on the pending log line, once.
void GetupNoteFate(GetupWatch& w, const std::string& fate)
{
    if (w.logPending && w.logArmed) { w.logArmed = false; w.logFate = fate; }
}

// G1-c: WHERE THE CODE FIRST LEARNS A PULSE WAS SPENT - GetupPulseTick (main thread, every frame), when PulseArmedFor
// reads 0 for G1's armed pulse or WithdrawPulseFor loses the race (0), the arm generation unchanged in both. So the
// delay is armed-to-seen, one frame coarse.
void GetupNoteSpent(GetupWatch& w, DWORD now)
{
    const DWORD d = now - w.armedAt;
    ++g_getupSpent;
    if (d <= 500) ++g_getupSpentLe500;
    if (d <= 1500) ++g_getupSpentLe1500;
    if (d <= 3000) ++g_getupSpentLe3000;
    // G1-b item 2 reads what it chose ~1 s after arming; a late spend is read no sooner than kGetupJudgeGapMs after
    // it was seen (G1-b's 500 ms expiry gave that gap by construction).
    w.rollbackAt = (d > kGetupRollbackMs - kGetupJudgeGapMs) ? ((now - (kGetupRollbackMs - kGetupJudgeGapMs)) | 1u)
                                                            : w.armedAt;
    GetupNoteFate(w, "spent(" + S((long long)d) + "ms)");
}

// G1-c fold M1: a pulse settled UNSPENT - it rolls back nothing, arms no give-up judgement (lastPulseAt cleared, unless
// the copy stood up on its own while it was armed: then the rag==0 branch still books getupDone, L3), and draws on the
// episode's budget; running out is the episode's one getupGaveUp.
void GetupSettleUnspent(GetupWatch& w, bool stoodUp)
{
    if (!stoodUp) w.lastPulseAt = 0;
    w.rollbackAt = 0;
    ++w.unspent;
    // recheck-g1c 1: the fifth try settling unspent leaves tries at the cap with no 'outstanding > 5 s' judgement to
    // book it (lastPulseAt was cleared above) - the episode is over either way, so it is its give-up.
    if ((w.unspent >= kGetupUnspentBudget || w.tries >= kGetupMaxTries) && !w.gaveUp) { w.gaveUp = true; ++g_getupGaveUp; }
}

void GetupLogAfter(unsigned int uid, GetupWatch& w, ::Character* c)
{
    w.logPending = false;
    std::string after = "GONE (despawned)";
    if (c != 0)
        after = BehaviourString(c) + " ragdoll=" + S((long long)GetupIsRagdoll(c))
              + " gettingUp(+0x278)=" + S((long long)GetupFlag(c));
    const std::string fate = w.logArmed ? std::string("unresolved") : (w.logFate.empty() ? std::string("?") : w.logFate);
    DebugLog("[G1] getup pulse #" + S(w.logNo) + " uid=" + S(uid) + " BEFORE " + w.logBefore
             + " | AFTER " + S((long long)(::GetTickCount() - w.logAt)) + "ms " + after + " fate=" + fate);
    w.logBefore.clear(); w.logFate.clear(); w.logArmed = false;
}

}   // namespace (G1 private)

// crash1 (T293 / F912): true = a knockdown of this STANDING copy waits. KnockdownWait 1 (a body rebuild queued on the
// copy) always waits; 2 (its appearance or worn items not applied yet) waits until coopkolook::kFirstLookWaitMs after the first
// wait, then goes ahead (counted once) - the looks then wait for the copy to stand instead (appearance.cpp). While it
// holds, a copy whose owner says knocked out takes its first look standing (CopyKnockdownHeld, src/common/kolook.h). A copy that
// is already LIMP (CopyLimpPod != 0 - crash1b: not merely flagged unconscious) never waits here: its looks wait until it stands
// (appearance.cpp), and a body rebuild queued on it by another route is held by the createBody guard while it lies in ragdoll.
static bool KnockdownMustWait(unsigned int uid, Character* c)
{
    // crash1b (T323): only a copy that is ACTUALLY limp skips the wait - a copy merely flagged unconscious can still be
    // standing with its rebuild queued (T323: the flag was set, prone 0, rebuild pending -> crash)
    // PROBE-START: P091 - the branch that decides this knockdown (read and log only; the same reads as below)
    P091NoteKnock(uid, c, CopyLimpPod(c) != 0 ? -1 : KnockdownWait(uid, c));
    // PROBE-END: P091
    if (CopyLimpPod(c) != 0) { g_knockHold.erase(uid); return false; }
    const int w = KnockdownWait(uid, c);
    if (w == 0) { g_knockHold.erase(uid); return false; }
    const DWORD now = GetTickCount();
    std::map<unsigned int, KnockHold>::iterator h = g_knockHold.find(uid);
    if (h == g_knockHold.end())
    {
        KnockHold k; k.since = now; k.gaveUp = false;
        k.rebuildSeen = (w == 1); k.rebuildSince = now; k.stuckNoted = false;
        g_knockHold[uid] = k;
        return true;
    }
    if (w == 1)
    {
        // crash1c (review-crash1b R3c): a rebuild flag that never clears is reported once per hold, and the hold goes on -
        // releasing the knockdown onto a copy with a rebuild queued is the crash.
        if (!h->second.rebuildSeen) { h->second.rebuildSeen = true; h->second.rebuildSince = now; }
        else if (!h->second.stuckNoted && now - h->second.rebuildSince > kKnockHoldStuckMs)
        {
            h->second.stuckNoted = true;
            ++g_knockHoldStuck;
            DebugLog("[M4] knockdown hold: rebuild flag stuck uid=" + S((long long)uid) + " for "
                     + S((long long)(now - h->second.rebuildSince)) + "ms - still holding (knockHoldStuck="
                     + S(g_knockHoldStuck) + ")");
        }
        return true;
    }
    h->second.rebuildSeen = false;   // crash1c: the rebuild flag cleared; a later one starts its own 10 s
    if (h->second.gaveUp) return false;
    if (now - h->second.since < (DWORD)coopkolook::kFirstLookWaitMs) return true;
    h->second.gaveUp = true;
    ++g_proneLooksTimeout;
    NoteKnockLooksGaveUp(uid);   // counted when the copy's first look is still pending (lookKoArrival gaveUp)
    return false;
}

// The knockdown of this copy is held for its looks NOW (KnockdownMustWait's own record, read live). MAIN THREAD.
bool CopyKnockdownHeld(unsigned int uid)
{
    std::map<unsigned int, KnockHold>::const_iterator h = g_knockHold.find(uid);
    if (h == g_knockHold.end()) return false;
    return coopkolook::KnockHoldLive(true, h->second.gaveUp, (unsigned long)(DWORD)(GetTickCount() - h->second.since));
}

static long long KnockHoldCount() { return (long long)g_knockHold.size(); }

// crash1 (review-crash1 LOW-4): the longest current wait, so a copy whose rebuild flags never clear shows up.
static long long KnockHoldOldestMs()
{
    const DWORD now = GetTickCount();
    long long oldest = 0;
    for (std::map<unsigned int, KnockHold>::const_iterator it = g_knockHold.begin(); it != g_knockHold.end(); ++it)
        if (!it->second.gaveUp && (long long)(now - it->second.since) > oldest) oldest = (long long)(now - it->second.since);
    return oldest;
}

// ---- SNEAKING (sneakwire.h): the owner's stealth mode rides STATE bit 7; the copy's game calls the engine's own setter ----
// Character::setStealthMode(bool) 0x5C9F10 is the call the orders panel's sneak button makes (OrdersPanel::toggleStealth
// 0x7208E0 reads isStealthMode and calls it with the opposite). It first calls the AnimationClass at +0x448 (the crouched
// walk), then stores the mode at +0xD4 and, on a change, does the engine's own bookkeeping for it - so the copy is put in
// stealth mode through it and its byte is never written here.
static unsigned long long kSetStealthModeRva = 0; static coop::AddrReg kSetStealthModeRva_reg("Character_setStealthMode", &kSetStealthModeRva);   /* Steam_1.0.65 0x5C9F10 - Character::setStealthMode(bool) */
typedef void (*SetStealthModeFn)(::Character* self, bool on);

namespace {

const size_t kStealthModeOff = 0xD4;   // Character: the stealth-mode byte setStealthMode compares and stores

struct SneakWant
{
    unsigned int peer;   // the game whose STATE said it
    bool want;           // that STATE's bit 7
    bool fresh;          // not acted on yet: one setter call at most per STATE, so the copy's own engine is not fought every frame
    bool notedWait;      // one sneakWaited per word that had to wait for the copy's body
    bool notedHeld;      // one sneakHeld per word that had to wait while the copy was carried or a ragdoll
};
std::map<unsigned int, SneakWant> g_sneakWant;        // copy uid -> its owner's last word on sneaking, until acted on
std::map<unsigned int, bool> g_sneakLastWord;         // copy uid -> the owner's word the copy last matched or was set to
std::map<unsigned int, const void*> g_sneakFaultBody; // copy uid -> the body whose setter faulted: not called again for that body
std::vector<std::pair<unsigned int, bool> > g_sneakTestPend;   // sneaktest: own uid -> on/off, applied at the next SneakApplyTick
std::map<unsigned int, unsigned char> g_ownSneakBit;  // owned uid -> its stealth mode as RestWatchTick last read it

long long g_ownSneakPushed = 0;   // owner: STATEs pushed at once because an owned character's stealth mode changed
long long g_sneakSetOn = 0;       // copy: setStealthMode(true) called
long long g_sneakSetOff = 0;      // copy: setStealthMode(false) called
long long g_sneakMatched = 0;     // copy: a STATE whose word the copy already matched - nothing called
long long g_sneakUnreadable = 0;  // copy: its mode or its AnimationClass could not be read - nothing called
long long g_sneakFault = 0;       // copy: the setter faulted (caught)
long long g_sneakWaited = 0;      // copy: a word that waited for the copy's body to be built
long long g_sneakReverted = 0;    // copy: the owner's word was unchanged but the copy's own engine had changed its mode since
long long g_sneakHeld = 0;        // copy: a word that waited while the copy was carried or a ragdoll
long long g_sneakFaultSkip = 0;   // copy: a word not acted on because the setter faulted on that body before
long long g_sneakLogged = 0;      // [SNEAK] events, owner and copy; coopsneak::SneakLogDue picks the lines printed

// 1 sneaking, 0 not, -1 unreadable.
int SneakRead(const void* c)
{
    if (c == 0) return -1;
    unsigned char v = 0xFF;
    __try { v = *((const unsigned char*)c + kStealthModeOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return (v == 0) ? 0 : ((v == 1) ? 1 : -1);
}

// The setter dereferences the AnimationClass at +0x448 first, so that pointer is checked; the call is SEH-wrapped.
// 1 = called, 0 = the AnimationClass is unreadable (not called), -1 = the call faulted.
int SneakCall(SetStealthModeFn fn, ::Character* c, bool on)
{
    void* anim = 0;
    __try { anim = *(void* const*)((const char*)c + kR3AnimOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!PlausibleObject(anim)) return 0;
    __try { fn(c, on); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return 1;
}

void SneakLog(const std::string& line)
{
    ++g_sneakLogged;
    if (coopsneak::SneakLogDue(g_sneakLogged)) DebugLog("[SNEAK] " + line + " (event " + S(g_sneakLogged) + ")");
}

}   // namespace (sneaking)

// ApplyState (main thread): the owner's word on sneaking, from its STATE's bit 7. SneakApplyTick acts on it.
static void NoteSneakWant(unsigned int uid, unsigned int latchBits, unsigned int fromPeer)
{
    SneakWant w;
    w.peer = fromPeer; w.want = coopsneak::SneakWanted(latchBits); w.fresh = true; w.notedWait = false; w.notedHeld = false;
    g_sneakWant[uid] = w;
}

// sneaktest (TEST-ONLY): this game's own character in or out of sneaking through the same setter, as the sneak button.
static void SneakTestDrain(SetStealthModeFn fn)
{
    for (size_t i = 0; i < g_sneakTestPend.size(); ++i)
    {
        const unsigned int uid = g_sneakTestPend[i].first;
        const bool on = g_sneakTestPend[i].second;
        ::Character* c = FindSpawned(uid);
        std::string why;
        if (c == 0 || !PlausibleObject(c)) why = "not here";
        else if (!net::IsUidMine(uid)) why = "not this game's own";
        else if (!CharacterBuilt(c, 0)) why = "its body is not built";
        if (!why.empty()) { DebugLog("[SNEAK] sneaktest uid=" + S((long long)uid) + " REFUSED - " + why); continue; }
        const int before = SneakRead(c);
        const int r = SneakCall(fn, c, on);
        DebugLog("[SNEAK] sneaktest uid=" + S((long long)uid) + " setStealthMode(" + (on ? "true" : "false") + ") call=" + S((long long)r)
                 + " (1 called, 0 body unreadable, -1 faulted) mode " + S((long long)before) + " -> " + S((long long)SneakRead(c)));
    }
    g_sneakTestPend.clear();
}

void SneakApplyTick()
{
    if (g_sneakWant.empty() && g_sneakTestPend.empty()) return;
    if (kSetStealthModeRva == 0) return;
    if (EngineWritesBlocked()) return;
    SetStealthModeFn fn = (SetStealthModeFn)((uintptr_t)::GetModuleHandleA(0) + kSetStealthModeRva);
    if (!g_sneakTestPend.empty()) SneakTestDrain(fn);
    std::map<unsigned int, SneakWant>::iterator it = g_sneakWant.begin();
    while (it != g_sneakWant.end())
    {
        const unsigned int uid = it->first;
        SneakWant& w = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c)) { g_sneakLastWord.erase(uid); g_sneakFaultBody.erase(uid); g_sneakWant.erase(it++); continue; }
        // A handoff since the STATE may have made this game the driver: then its own player or AI decides.
        if (net::IsUidMine(uid) || !net::RemoteMayWriteStill(uid, w.peer))
        {
            g_sneakLastWord.erase(uid); g_sneakFaultBody.erase(uid); g_sneakWant.erase(it++); continue;
        }
        if (c->hasDied()) { g_sneakWant.erase(it++); continue; }
        if (!CharacterBuilt(c, 0))
        {
            if (!w.notedWait) { w.notedWait = true; ++g_sneakWaited; }
            ++it; continue;
        }
        // Carried or a ragdoll: the body belongs to the engine's physics for now; the word waits until it stands again.
        if (BeingCarried(c) || RagdollActive(c))
        {
            if (!w.notedHeld) { w.notedHeld = true; ++g_sneakHeld; }
            ++it; continue;
        }
        std::map<unsigned int, const void*>::const_iterator fb = g_sneakFaultBody.find(uid);
        if (fb != g_sneakFaultBody.end() && fb->second == (const void*)c) { ++g_sneakFaultSkip; g_sneakWant.erase(it++); continue; }
        const int now = SneakRead(c);
        // The copy matched (or was set to) this same word before and its mode differs now: its own engine changed it.
        std::map<unsigned int, bool>::const_iterator lw = g_sneakLastWord.find(uid);
        if (lw != g_sneakLastWord.end() && lw->second == w.want && now >= 0 && (now == 1) != w.want)
        {
            ++g_sneakReverted;
            SneakLog("copy uid=" + S(uid) + " REVERTED: the owner still says " + (w.want ? "sneaking" : "upright")
                     + " but the copy's own engine changed its mode to " + S((long long)now) + " since");
        }
        const int act = coopsneak::SneakAction(w.want, now);
        if (act == coopsneak::kSneakNone) { ++g_sneakMatched; g_sneakLastWord[uid] = w.want; g_sneakWant.erase(it++); continue; }
        std::string what;
        if (act == coopsneak::kSneakUnreadable) { ++g_sneakUnreadable; what = "its stealth mode is unreadable - nothing called"; }
        else
        {
            const bool on = (act == coopsneak::kSneakSetOn);
            const int r = SneakCall(fn, c, on);
            if (r == 1) { if (on) ++g_sneakSetOn; else ++g_sneakSetOff; what = on ? "setStealthMode(true)" : "setStealthMode(false)"; g_sneakLastWord[uid] = w.want; }
            else if (r == 0) { ++g_sneakUnreadable; what = "its body is unreadable - nothing called"; }
            else { ++g_sneakFault; g_sneakFaultBody[uid] = (const void*)c; what = "setStealthMode faulted - not called again for this body"; }
        }
        SneakLog("copy uid=" + S(uid) + " owner says " + (w.want ? "sneaking" : "upright") + ": " + what
                 + ", mode now " + S((long long)SneakRead(c)));
        g_sneakWant.erase(it++);
    }
}

std::string SneakReportToken()
{
    return " sneak[pushed,setOn,setOff,matched,unreadable,fault,waited,reverted,held,faultSkip]=" + S(g_ownSneakPushed) + "," + S(g_sneakSetOn)
         + "," + S(g_sneakSetOff) + "," + S(g_sneakMatched) + "," + S(g_sneakUnreadable) + "," + S(g_sneakFault)
         + "," + S(g_sneakWaited) + "," + S(g_sneakReverted) + "," + S(g_sneakHeld) + "," + S(g_sneakFaultSkip)
         + " sneakWatched=" + S((long long)g_sneakWant.size());
}

// sneaktest <ownUid> on|off   - TEST-ONLY: queued; SneakApplyTick makes the call at its safe point.
std::string SneakTestCommand(const std::string& arg)
{
    std::istringstream is(arg);
    unsigned int uid = 0;
    std::string mode;
    if (!(is >> uid >> mode) || uid == 0 || (mode != "on" && mode != "off")) return "error sneaktest: usage sneaktest <ownUid> on|off";
    if (kSetStealthModeRva == 0) return "error sneaktest: Character_setStealthMode is not in the address table";
    if (!net::IsUidMine(uid)) return "error sneaktest: uid " + S((long long)uid) + " is not this game's own";
    if (g_sneakTestPend.size() >= 8) return "error sneaktest: 8 already wait";
    g_sneakTestPend.push_back(std::make_pair(uid, mode == "on"));
    return "ok sneaktest uid=" + S((long long)uid) + " " + mode + " queued for the next safe point";
}

// Returns true if PoseState had to be CHANGED. H009: with the full record replicated the
// peer should derive the same collapse itself, so this should stop returning true. The write
// is KEPT as a safety net precisely so its SILENCE is the measurement - a fix that deletes
// its own diagnostic is how a regression hides. Death is never walked back: it is a state we
// do not yet replicate or understand.
bool ApplyState(unsigned int uid, const float* parts, int count, float blood,
                int prone, int* outWasProne, unsigned int latchBits, float nextKnockoutAt, float koTimer,
                unsigned int fromPeer, unsigned int carryingUid, const float* rest)
{
    // R1-a / R1-a-b (review-r1a LOW-1): this STATE is newer than any prone value parked for the
    // uid, so that value is discarded FIRST - before the early returns below, which would otherwise
    // leave the older value to be applied later over this newer STATE. Re-parked below if this
    // one also has to wait (latest wins).
    std::map<unsigned int, ProneParkEntry>::iterator pk = g_proneParkMap.find(uid);
    if (pk != g_proneParkMap.end()) { g_proneParkMap.erase(pk); ++g_proneParkSuperseded; }

    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return false;
    // C2 (read-KO): from here to every return, this thread is applying the owner's STATE - the puppet
    // knockout gate lets startKnockoutTimer / knockout through for it (medical.h ApplyingOwnerState).
    ApplyingOwnerState applyingOwner;
    if (!ApplyHealth(uid, parts, count, blood))
    {
        // crash1c (review-crash1b R3a): the latch below is never reached, so write the owner's word on knocked out to the
        // table here - the same test ApplyLatch writes - or a stale "knocked out" entry would hold the looks forever.
        CopyNoteOwnerKo(uid, (latchBits & kLatchUnconcious) != 0 || (koTimer > 0.0f && koTimer < 3600.0f));
        return false;
    }

    // F147: the latch block FIRST, and prone second. `unconcious` is what the peer's own
    // collapse decision reads, so writing prone before the flag would leave its engine a tick
    // in which to undo us - which is exactly the 9-125 ms undo T047 measured eight times.
    // crash1 (T293 / F912; review-crash1 HIGH-1 / MEDIUM-2): a STATE that would take a standing copy down - the latch
    // (unconscious, or a wake-up clock: the copy's own engine then knocks it out, F439) or a prone of 2..4 - waits while a
    // body rebuild is queued on it, and a bounded time for its looks. While it waits the latch is held back (this STATE's
    // values are dropped; the owner's next STATE carries them again) and the prone write below parks.
    const bool latchDowns = (latchBits & kLatchUnconcious) != 0 || (koTimer > 0.0f && koTimer < 3600.0f);
    const bool holdDown = (latchDowns || prone >= 2) && !c->hasDied() && KnockdownMustWait(uid, c);
    if (holdDown && latchDowns) ++g_latchHeld;
    else ApplyLatch(c, latchBits, nextKnockoutAt, koTimer);

    int before = (int)c->poseState();
    if (outWasProne) *outWasProne = before;
    // K1 (read-carry) item 2: the owner's word on what this copy carries; CarryApplyTick acts on it.
    NoteCarryWant(uid, c, carryingUid, fromPeer);
    // R3 (read-ragdoll) item 2: the owner's rest position; RestApplyTick acts on it.
    NoteRestWant(uid, rest, fromPeer);
    // G1 (read-getup): the owner's word on getting up; GetupPulseTick acts on it.
    NoteGetupState(uid, c, prone, latchBits, koTimer, fromPeer);   // T-178 crawl1: + the copy (carried / posed)
    // The owner's word on sneaking (bit 7); SneakApplyTick puts the copy in or out of stealth mode through the engine.
    NoteSneakWant(uid, latchBits, fromPeer);
    // K1 item 3: while the copy is carried its prone state is NOT written - the owner reads prone=0 while
    // carried. Health and the latch (above) still apply.
    if (before != prone && BeingCarried(c)) { ++g_proneSkippedCarried; return false; }
    // review-k1 item 6: while carried the owner reads 'standing + unconscious' (prone=0, the latch saying unconscious,
    // the wake-up clock running). A knocked-out copy is not stood up for that - it would stand in the moment before
    // its pick-up. Health and the latch (above) still apply.
    if (before != prone && prone == 0 && (latchBits & coop::kLatchUnconcious) != 0 && koTimer > 0.0f)
    { ++g_proneStandSkippedUnconscious; return false; }

    if (before != prone && !c->hasDied())
    {
        // R1-a / R1-a-b (review-r1a M1): health and the latch are applied above; only the prone
        // WRITE waits, and only for what setProneState itself dereferences (CharacterProneSafe) -
        // not for a visible body. Parked writes, with the peer the STATE came from (M2), are
        // applied by ProneParkTick; the return is the not-applied one.
        if (!CharacterProneSafe(c, 0))
        {
            ProneParkEntry e; e.prone = prone; e.peer = fromPeer;
            g_proneParkMap[uid] = e;
            ++g_proneParked;
            return false;
        }
        // crash1 (T293 / F912): not onto a body with a rebuild queued, nor before its looks are applied (bounded).
        if (prone >= 2 && holdDown)
        {
            ProneParkEntry e; e.prone = prone; e.peer = fromPeer;
            g_proneParkMap[uid] = e;
            ++g_proneParkedRebuild;
            return false;
        }
        // F140: tell the transition watch this one is OURS, before it happens. Without this
        // the watch counts an authoritative overwrite as a peer disagreement - T043 had two
        // of them and the only way to spot it was a coincident timestamp on another line.
        NoteAuthoritativeProne(uid, prone);
        coop::NoteProneWrite(uid);   // PROBE P019
        c->setPoseState((PoseState)prone);
        return true;
    }
    return false;
}

// R1-a / R1-a-b - apply the prone writes ApplyState parked, once CharacterProneSafe says yes.
// MAIN THREAD, called every frame beside AppearanceTick. No time expiry: retirement is the exit
// event. Returns at once while EngineWritesBlocked() - the drain's rule (review-r1a LOW-2).
void ProneParkTick()
{
    if (g_proneParkMap.empty()) return;
    if (EngineWritesBlocked()) { ++g_proneFlushBlocked; return; }
    std::map<unsigned int, ProneParkEntry>::iterator it = g_proneParkMap.begin();
    while (it != g_proneParkMap.end())
    {
        const unsigned int uid = it->first;
        const int prone = it->second.prone;
        Character* c = FindSpawned(uid);
        if (c == 0) { g_proneParkMap.erase(it++); ++g_proneDroppedRetired; continue; }
        if (!CharacterProneSafe(c, 0)) { ++it; continue; }
        if (BeingCarried(c)) { g_proneParkMap.erase(it++); ++g_proneParkMooted; ++g_proneSkippedCarried; continue; }   // K1 item 3

        // R1-a-b (review-r1a M2): the write was authorised when its STATE arrived; a handoff while
        // it waited may have made us the owner, so the session guard is asked again - once, here,
        // when the write is about to happen (a refusal is also counted in remoteMayWriteRefused).
        if (!net::RemoteMayWriteStill(uid, it->second.peer))
        { g_proneParkMap.erase(it++); ++g_proneDroppedNotOwner; continue; }

        // The same test and the same sequence as ApplyState: the engine may have reached that
        // state on its own, or the character died, while the write waited.
        const int before = (int)c->poseState();
        // crash1 (T293 / F912): the knockdown waits as ApplyState's does.
        if (before != prone && prone >= 2 && !c->hasDied() && KnockdownMustWait(uid, c)) { ++it; continue; }
        if (before != prone && !c->hasDied())
        {
            NoteAuthoritativeProne(uid, prone);
            coop::NoteProneWrite(uid);   // PROBE P019
            c->setPoseState((PoseState)prone);
            ++g_proneAppliedAfterPark;
            // R1-a-b (review-r1a LOW-4): the same line session.cpp prints for an immediate write.
            if (g_proneParkLogged < kProneParkLogLimit)
            {
                ++g_proneParkLogged;
                DebugLog("[M4] STATE corrected (parked) uid=" + S(uid) + " prone " + S(before)
                         + " -> " + S(prone) + " (authority)");
            }
        }
        else
        {
            ++g_proneParkMooted;
        }
        g_proneParkMap.erase(it++);
    }
}

// ---- K1 (read-carry 2026-09-22): the public half - see spawn.h --------------------------------------------------
unsigned int CarryingUidOf(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !CarryFlag(c)) return 0;
    unsigned char id[kHandIdBytes];
    if (!CarryReadHandId(c, kCarryHandIdOff, id)) return 0;
    return UidByHandId(id, 0);
}

void CarryWatchTick()
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        const bool flag = CarryFlag(c);
        std::map<unsigned int, unsigned int>::iterator last = g_carryLast.find(uid);
        if (!flag && last == g_carryLast.end()) continue;   // the common case: carries nothing, never did
        if (!net::IsUidMine(uid)) { if (last != g_carryLast.end()) g_carryLast.erase(last); continue; }
        const unsigned int now = flag ? CarryingUidOf(uid) : 0;
        const unsigned int was = (last == g_carryLast.end()) ? 0 : last->second;
        if (now == was) continue;
        if (now == 0) g_carryLast.erase(last); else g_carryLast[uid] = now;
        if (StatePush(uid)) ++g_carrySent;   // like prone/dead (F140): at once, not on the round-robin
    }
}

void CarryApplyTick()
{
    if (EngineWritesBlocked()) return;
    if (kPickupObjectRva == 0 || kDropCarriedObjectRva == 0) return;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    PickupObjectFn pickup = (PickupObjectFn)(base + kPickupObjectRva);
    DropCarriedObjectFn drop = (DropCarriedObjectFn)(base + kDropCarriedObjectRva);
    if (!g_ownCarried.empty()) OwnCarryWatch(drop);   // arrest1
    if (!g_carryBreakIn.empty()) CarryBreakApply(drop);   // arrest1
    const DWORD now = ::GetTickCount();
    if (!g_handOvers.empty()) HandOverSweep(now);
    if (g_carryWant.empty()) return;

    std::map<unsigned int, CarryWant>::iterator it = g_carryWant.begin();
    while (it != g_carryWant.end())
    {
        const unsigned int carrierUid = it->first;
        CarryWant& w = it->second;
        ::Character* cc = FindSpawned(carrierUid);
        if (cc == 0 || !PlausibleObject(cc)) { g_carryWant.erase(it++); continue; }
        // The wish was authorised when its STATE arrived; a handoff since may have made us the owner.
        if (!net::RemoteMayWriteStill(carrierUid, w.peer)) { g_carryWant.erase(it++); continue; }
        // review-k1 item 2: a carrier THIS game drives decides its own carry - never dropped, swapped or handed over here.
        if (net::IsUidMine(carrierUid)) { ++g_carryConflictMine; g_carryWant.erase(it++); continue; }

        if (w.body == 0)
        {
            if (CarryFlag(cc))
            {
                unsigned char id[kHandIdBytes];
                ::Character* heldCh = 0;
                const unsigned int held = CarryReadHandId(cc, kCarryHandIdOff, id) ? UidByHandId(id, &heldCh) : 0;
                // review-k1 item 5: the drop waits for CharacterBuilt on the carrier and on what it holds.
                if (!CharacterBuilt(cc, 0) || (heldCh != 0 && !CharacterBuilt(heldCh, 0)))
                {
                    if (!w.notedNotReady) { w.notedNotReady = true; ++g_carryNotReady; }
                    ++it;
                    continue;
                }
                drop(cc, true, false);   // the owner put it down: ragdoll it where it is
                ++g_carryDropped;
                if (held != 0) g_ownCarried.erase(held);   // arrest1 (review MEDIUM 2): the carrier's game ended it - no break
                CarryLog("drop", carrierUid, held);
            }
            g_carryWant.erase(it++);
            continue;
        }

        ::Character* body = FindSpawned(w.body);
        if (body == 0 || !PlausibleObject(body))
        {
            if (!w.notedNoBody) { w.notedNoBody = true; ++g_carryNoLocalBody; }
            ++it;
            continue;
        }
        if (Carries(cc, body)) { g_carryWant.erase(it++); continue; }   // already so
        if (CarryFlag(cc))
        {
            // Carrying something else: put that down first (pickupObject refuses while +0x348 is set);
            // the pick-up follows on a later tick.
            unsigned char id[kHandIdBytes];
            ::Character* heldCh = 0;
            const unsigned int held = CarryReadHandId(cc, kCarryHandIdOff, id) ? UidByHandId(id, &heldCh) : 0;
            // review-k1 item 5: the swap waits for CharacterBuilt on the carrier and on what it holds.
            if (!CharacterBuilt(cc, 0) || (heldCh != 0 && !CharacterBuilt(heldCh, 0)))
            {
                if (!w.notedNotReady) { w.notedNotReady = true; ++g_carryNotReady; }
                ++it;
                continue;
            }
            drop(cc, true, false);
            ++g_carryDropped;
            if (held != 0) g_ownCarried.erase(held);   // arrest1 (review MEDIUM 2)
            CarryLog("drop (swap)", carrierUid, held);
            ++it;
            continue;
        }
        // recheck-k1b: the body-mine check sits AFTER the swap drop, so a copy carrier told to carry this game's own
        // character still puts down the body it held before (otherwise that body's copy stayed carried forever).
        // arrest1 (docs/design-arrest.md 3; closes the K1 "known v2 gap"): a body THIS game drives is picked up on the other
        // game's word only when it is helpless here (OwnBodyMayBeCarried). One that is awake, standing or in a bed / cage
        // is not, and the carrier's game is told once (MSG_CARRY_BREAK) so its carrier lets go too.
        const bool bodyMine = net::IsUidMine(w.body);
        // out of its bed at the safe point first; the pick-up follows. A take-out that keeps failing falls through to the break.
        if (bodyMine && OwnBodyInBedMayBeCarried(body) && !BedOutForCarryGivenUp(w.body))
        {
            if (g_bedOutForCarry.insert(w.body).second) ++g_bedOutForCarryQueued;
            ++it;
            continue;
        }
        if (bodyMine && !OwnBodyMayBeCarried(body))
        {
            if (!w.notedBodyMine)
            {
                w.notedBodyMine = true;
                ++g_carrySkippedBodyMine;
                ++g_carryOwnAwake;
                if (net::SendCarryBreak(w.body, carrierUid)) ++g_carryBreakSent;
                if (g_carryBodyMineLogged < kCarryBodyMineLogLimit)
                {
                    ++g_carryBodyMineLogged;
                    DebugLog("[K1] carry refused: body uid=" + S(w.body) + " is driven by this game and is not helpless here"
                             " (awake, standing, or in a bed / cage); carrier uid=" + S(carrierUid) + " - break sent");
                }
            }
            ++it;
            continue;
        }
        if (!CharacterBuilt(cc, 0) || !CharacterBuilt(body, 0))
        {
            if (!w.notedNotReady) { w.notedNotReady = true; ++g_carryNotReady; }
            ++it;
            continue;
        }
        // Carried by someone else here: name that carrier, and check it may be touched, BEFORE the cap is spent.
        unsigned int otherUid = 0;
        ::Character* other = 0;
        int handOver = -1;
        if (BeingCarried(body))
        {
            other = LocalCarrierOf(body, &otherUid);
            if (other == 0 || !CharacterBuilt(other, 0))   // review-k1 item 5
            {
                if (!w.notedNotReady) { w.notedNotReady = true; ++g_carryNotReady; }
                ++it;
                continue;
            }
            // A carrier THIS game drives never lets go on the other game's word - except to the NPC it handed this very person to
            // in a conversation (the hand-over mark), once that NPC's owner says the NPC carries the person.
            if (net::IsUidMine(otherUid))
            {
                handOver = HandOverFind(w.body, otherUid, carrierUid, now);
                if (handOver < 0)
                {
                    if (!w.notedConflict) { w.notedConflict = true; ++g_carryConflictMine; }
                    ++it;
                    continue;
                }
            }
        }
        if (!CarryCapAllows(carrierUid, now))
        {
            if (!w.notedCapped) { w.notedCapped = true; ++g_carryRateCapped; }
            ++it;
            continue;
        }
        if (other != 0)
        {
            // review-k1 item 1: a FULL drop (ragdoll 0, removeOnly 0). removeOnly=1 skips getDropped (0x5CD750 runs it
            // only when removeOnly == 0), which leaves the body's +0x3D4 set and its carry mode on - pickupObject then
            // refuses and the body is stranded.
            drop(other, false, false);
            ++g_carryDropped;
            CarryLog("drop (handover)", otherUid, w.body);
        }
        pickup(cc, body);
        if (BeingCarried(body))
        {
            ++g_carryApplied;
            if (bodyMine) { ++g_carryOwnApplied; OwnCarry oc; oc.carrier = carrierUid; oc.awakeSince = 0; g_ownCarried[w.body] = oc; }   // arrest1: watched from here on
            CarryLog(bodyMine ? "apply (own body)" : "apply", carrierUid, w.body);
            if (handOver >= 0)
            {
                ++g_handOverLetGo;
                DebugLog("[K1] hand-over: our carrier uid=" + S(otherUid) + " let go of body uid=" + S(w.body) + " and the NPC's copy uid="
                         + S(carrierUid) + " carries it, as the NPC's own game says");
                g_handOvers.erase(g_handOvers.begin() + handOver);
            }
            g_carryWant.erase(it++);
        }
        else
        {
            ++g_carryRefusedByEngine;   // retried on a later tick, inside the cap
            ++it;
        }
    }
}

std::string CarryReportToken()
{
    return " carrySent=" + S(g_carrySent) + " carryApplied=" + S(g_carryApplied)
         + " carryDropped=" + S(g_carryDropped) + " carryNotReady=" + S(g_carryNotReady)
         + " carryNoLocalBody=" + S(g_carryNoLocalBody) + " carryRefusedByEngine=" + S(g_carryRefusedByEngine)
         + " carryRateCapped=" + S(g_carryRateCapped) + " carryWishes=" + S((long long)g_carryWant.size())
         + " proneSkippedCarried=" + S(g_proneSkippedCarried)
         + " carryConflictMine=" + S(g_carryConflictMine) + " carrySkippedBodyMine=" + S(g_carrySkippedBodyMine)
         + " proneStandSkippedUnconscious=" + S(g_proneStandSkippedUnconscious)
         // arrest1 (docs/design-arrest.md 3): own bodies carried by the other game's carrier, and the breaks both ways
         + " arrest1[ownApplied,ownAwake,ownWokeDropped,breakSent,breakApplied,breakStale,breakRefused,breakDropped,ownCarriedNow]="
         + S(g_carryOwnApplied) + "," + S(g_carryOwnAwake) + "," + S(g_carryOwnWokeDropped) + "," + S(g_carryBreakSent)
         + "," + S(g_carryBreakApplied) + "," + S(g_carryBreakStale) + "," + S(g_carryBreakRefused) + "," + S(g_carryBreakDropped)
         + "," + S((long long)g_ownCarried.size())
         + " handOver[noted,letGo,ended,expired,full,now]=" + S(g_handOverNoted) + "," + S(g_handOverLetGo) + "," + S(g_handOverEnded)
         + "," + S(g_handOverExpired) + "," + S(g_handOverFull) + "," + S((long long)g_handOvers.size());
}

void CarryHandOverNote(unsigned int body, unsigned int carrier, unsigned int taker)
{
    if (body == 0 || carrier == 0 || taker == 0) return;
    for (size_t i = 0; i < g_handOvers.size(); ++i)
        if (g_handOvers[i].body == body) { g_handOvers.erase(g_handOvers.begin() + i); break; }   // the newest hand-over of a person wins
    if (g_handOvers.size() >= (size_t)cooptalk::kTalkHandOverCap) { g_handOvers.erase(g_handOvers.begin()); ++g_handOverFull; }
    cooptalk::TalkHandOver h;
    h.body = body; h.carrier = carrier; h.taker = taker; h.ms = (unsigned long)::GetTickCount();
    g_handOvers.push_back(h);
    ++g_handOverNoted;
    DebugLog("[K1] hand-over mark: our carrier uid=" + S(carrier) + " gives body uid=" + S(body) + " to the NPC uid=" + S(taker)
             + " - it lets go when the NPC's copy is told to carry it (within " + S((long long)(cooptalk::kTalkHandOverMs / 1000)) + " s)");
}

// arrest1: see spawn.h. Only the game that drives the body may end its carry, and only for a carrier THIS game drives.
void ApplyRemoteCarryBreak(unsigned int body, unsigned int carrier, unsigned int fromPeer)
{
    if (!net::UidOwnedByPeer(body, fromPeer) || !net::IsUidMine(carrier)) { ++g_carryBreakRefused; return; }
    g_carryBreakIn[carrier] = body;
}

void CarryBreakNoteDropped()
{
    ++g_carryBreakDropped;
}

/* M7b (T761) TEST-ONLY lever (command file only - `capturetest carry` / `capturetest carried`, items.cpp CaptureTestArm). MAIN
   THREAD: the command channel rides the same in-game pump as CarryApplyTick (coop.cpp).
   capturetest carry <carrierUid> <bodyUid>: this game's OWN character <carrierUid> picks up <bodyUid>, the OTHER game's
   character's copy here, through the engine's own Character::pickupObject 0x5CF500 (the call K1 makes on copies). Nothing
   else: the carry edge goes out as STATE (CarryWatchTick); the body's own game keeps the carry as its engine does (owner 333 a:
   waking does not end it) or ends it - awake when the carry arrives, or its engine let go - with MSG_CARRY_BREAK to
   this game, whose carrier then lets go (CarryBreakApply). capturetest carried <uid>: reads only.
   capturetest carry <carrierUid> name <name> (TEST-ONLY, the carried hand-in fixture): the body is the nearest down character with
   that name ('_' = a space) within 300 units of the carrier, either game's - this game's own included; the same pickupObject. */
std::string CarryTestCommand(const std::string& arg)
{
    std::istringstream is(arg);
    std::string word;
    is >> word;
    if (word == "carried")
    {
        unsigned int uid = 0;
        if (!(is >> uid) || uid == 0) return "error capturetest: usage capturetest carried <uid>";
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c)) return "error capturetest carried: uid " + S((long long)uid) + " is not here";
        unsigned int by = 0;
        const bool carried = BeingCarried(c);
        if (carried) LocalCarrierOf(c, &by);
        const std::string line = "[CAPT] capturetest carried uid=" + S((long long)uid) + " own=" + S(net::IsUidMine(uid) ? 1 : 0)
            + " carrying=" + S((long long)CarryingUidOf(uid)) + " beingCarried=" + S(carried ? 1 : 0)
            + " carrier=" + S((long long)by) + " downed=" + S((long long)CopyDowned(c));
        DebugLog(line);
        return "ok " + line.substr(7);
    }
    unsigned int carrier = 0, body = 0;
    std::string bodyKey;
    /* `carry <carrierUid> name <name>`: the body is picked by name and MAY be this game's own (the carried hand-in fixture carries
       the person this game knocked out) */
    const int byName = coopsay::CarryNameParse(arg, &carrier, &bodyKey);
    if (!byName && (word != "carry" || !(is >> carrier >> body) || carrier == 0 || body == 0 || carrier == body))
        return "error capturetest: usage capturetest carry <carrierUid> <bodyUid> | capturetest carry <carrierUid> name <name, '_' = a space> | capturetest carried <uid>";
    if (EngineWritesBlocked()) return "error capturetest carry: engine writes are blocked - try again";
    if (kPickupObjectRva == 0) return "error capturetest carry: PickupObject is not in the address table";
    if (!net::IsUidMine(carrier)) return "error capturetest carry: carrier uid " + S((long long)carrier) + " is not this game's own character";
    if (!byName && net::IsUidMine(body)) return "error capturetest carry: body uid " + S((long long)body) + " is this game's own - the lever carries the other game's character's copy";
    ::Character* cc = FindSpawned(carrier);
    ::Character* bc = 0;
    if (byName)
    {
        if (cc == 0 || !PlausibleObject(cc)) return "error capturetest carry: the carrier is not here";
        float bd = -1.0f;
        std::string bn, why;
        bc = LeverNearestNamed(bodyKey, kLeverPickDowned, cc, 300.0f, &body, &bd, &bn, &why);
        if (bc == 0) return "error capturetest carry name: " + why;
        DebugLog("[CAPT] capturetest carry name '" + bodyKey + "' -> body uid=" + S((long long)body) + " '" + bn + "' own="
                 + S((long long)(body != 0 && net::IsUidMine(body) ? 1 : 0)) + " dist=" + F1(bd));
    }
    else bc = FindSpawned(body);
    if (cc == 0 || bc == 0 || !PlausibleObject(cc) || !PlausibleObject(bc)) return "error capturetest carry: the carrier or the body is not here";
    if (!CharacterBuilt(cc, 0) || !CharacterBuilt(bc, 0)) return "error capturetest carry: the carrier or the body is not built yet - try again";
    if (CarryFlag(cc)) return "error capturetest carry: the carrier already carries uid " + S((long long)CarryingUidOf(carrier));
    if (BeingCarried(bc)) return "error capturetest carry: the body is already carried";
    const int downedBefore = CopyDowned(bc);
    PickupObjectFn pickup = (PickupObjectFn)((uintptr_t)::GetModuleHandleA(0) + kPickupObjectRva);
    pickup(cc, bc);
    const std::string line = "[CAPT] capturetest carry carrier=" + S((long long)carrier) + " body=" + S((long long)body)
        + " bodyDownedBefore=" + S((long long)downedBefore) + " carrying=" + S((long long)CarryingUidOf(carrier))
        + " beingCarried=" + S(BeingCarried(bc) ? 1 : 0)
        + " (TEST-ONLY: the engine's pickupObject on this game's character; the body's own game keeps or breaks the carry)";
    DebugLog(line);
    return "ok " + line.substr(7);
}

// ---- R3 (read-ragdoll 2026-09-22): the public half - see spawn.h ---------------------------------------------------
bool RagdollActive(::Character* c)
{
    RagRead r;
    return PlausibleObject(c) && ReadRagFields(c, &r) && r.rag != 0 && (r.active || r.ragCarried);
}

bool RestOfOwned(unsigned int uid, float* out3)
{
    std::map<unsigned int, RestOwn>::const_iterator it = g_restOwn.find(uid);
    if (it == g_restOwn.end() || out3 == 0) return false;
    out3[0] = it->second.x; out3[1] = it->second.y; out3[2] = it->second.z;
    return true;
}

void RestWatchTick()
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        std::map<unsigned int, RestOwn>::iterator it = g_restOwn.find(uid);
        if (!net::IsUidMine(uid)) { if (it != g_restOwn.end()) g_restOwn.erase(it); g_ownRagBit.erase(uid); g_ownSneakBit.erase(uid); continue; }
        // T-178 crawl1 (protocol 86): the owner's inRagdoll rides every STATE (SnapshotState); a CHANGE of it is pushed at
        // once, like prone/dead (F140), so the copy's G1 pulse follows the owner's own get-up, not the 5 s refresh.
        if (PlausibleObject(c))
        {
            const unsigned char rb = (GetupIsRagdoll(c) != 0) ? 1 : 0;
            std::map<unsigned int, unsigned char>::iterator ob = g_ownRagBit.find(uid);
            if (ob == g_ownRagBit.end()) g_ownRagBit[uid] = rb;
            else if (ob->second != rb) { ob->second = rb; if (StatePush(uid)) ++g_ownRagPushed; }
        }
        // The owner's stealth mode rides every STATE (bit 7, SnapshotState); a change is pushed at once, so the copy
        // crouches or stands with its owner rather than at the next 5 s refresh.
        if (PlausibleObject(c))
        {
            const int sm = SneakRead(c);
            if (sm >= 0)
            {
                const unsigned char sb = (unsigned char)sm;
                std::map<unsigned int, unsigned char>::iterator os = g_ownSneakBit.find(uid);
                if (os == g_ownSneakBit.end()) g_ownSneakBit[uid] = sb;
                else if (os->second != sb)
                {
                    os->second = sb;
                    const bool pushed = StatePush(uid);
                    if (pushed) ++g_ownSneakPushed;
                    SneakLog("owned uid=" + S(uid) + " stealth mode now " + S((long long)sb)
                             + (pushed ? " - STATE pushed" : " - not pushed (not announced / no link)"));
                }
            }
        }
        RagRead r;
        // ragdolled (+0x59), not carried (+0x3D4, +0x5A), not in a bed/cage (+0x2F8), and settled (+0x10)
        const bool resting = PlausibleObject(c) && ReadRagFields(c, &r) && r.rag != 0 && r.active && !r.ragCarried
                             && !r.carried && r.inSomething == 0 && r.settled;
        if (!resting) { if (it != g_restOwn.end()) g_restOwn.erase(it); continue; }
        if (it != g_restOwn.end() && RestDist(r.pos, it->second.x, it->second.y, it->second.z) <= kRestResendDist) continue;
        RestOwn o; o.x = r.pos[0]; o.y = r.pos[1]; o.z = r.pos[2];
        g_restOwn[uid] = o;
        if (StatePush(uid)) ++g_restSent;   // like prone/dead (F140): at once, not on the round-robin
    }
}

void RestApplyTick()
{
    if (g_restWant.empty()) return;
    if (g_restHookArmed == 0) return;
    if (EngineWritesBlocked()) return;
    const DWORD now = ::GetTickCount();
    std::map<unsigned int, RestWant>::iterator it = g_restWant.begin();
    while (it != g_restWant.end())
    {
        const unsigned int uid = it->first;
        RestWant& w = it->second;
        // R3-b item 1: a superseded or unwanted move leaves the table only when RestWithdraw confirms it (or it
        // finished); until then the record, its ragdoll key and its timeout stay and the withdraw is retried each tick.
        if (w.queued && (w.stale || w.dropped))
        {
            if (!RestWithdrawWant(w)) { ++it; continue; }
            w.stale = false;
        }
        if (w.dropped)
        {
            if (w.judging) RestJudgeEnd(uid, w, kRestVerdictGone, -1.0f, "GONE (no owner rest any more)");
            g_restWant.erase(it++); continue;
        }
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c) || !net::RemoteMayWriteStill(uid, w.peer) || net::IsUidMine(uid))
        {
            if (w.judging) RestJudgeEnd(uid, w, kRestVerdictGone, -1.0f, "GONE (despawned / no longer ours)");
            if (RestWithdrawWant(w)) g_restWant.erase(it++);
            else { w.dropped = true; ++it; }
            continue;
        }

        if (w.queued)
        {
            if (cooprest::RestIsPending(&g_restTable, w.rag))
            {
                // a slide stopped part-way by the withdraw is counted missed too (its steps so far stay made);
                // R3-b item 1: a lost withdraw keeps queued + queuedAt, so it is retried on the next tick
                if (now - w.queuedAt > kRestPendingMs && cooprest::RestWithdraw(&g_restTable, w.rag))
                { w.queued = false; ++g_restMissed; RestLog(uid, w, -1.0f, "NOT TAKEN in 5 s, withdrawn"); }
                ++it; continue;
            }
            // Every step taken and made on the physics thread. R3-d (T258, read-anim98): the node follows at once, but
            // anim +0x98 (getPosition) is refreshed only by AnimationClass::update 0x5B5E30 at the character's next
            // Character::update 0x5CE6A0 - every frame on screen, else round-robin (GameWorld::charsUpdate 0x7862F0,
            // ~6 characters a frame). So it is polled each main-thread frame from here, for up to 10 s. No retry.
            w.queued = false;
            w.judging = true; w.judgeAt = now; w.judgeFrames = 0;
            if (w.steps > 1) ++g_restSlid;
        }
        if (w.judging)
        {
            ++w.judgeFrames;
            RagRead r;
            if (!ReadRagFields(c, &r) || r.rag == 0 || r.rag != w.rag || !r.active)
            { RestJudgeEnd(uid, w, kRestVerdictGone, -1.0f, "GONE (no longer ragdolled)"); ++it; continue; }
            if (r.carried || r.ragCarried || r.inSomething != 0)
            { RestJudgeEnd(uid, w, kRestVerdictGone, -1.0f, "GONE (carried)"); ++it; continue; }
            const float off = RestDistXZ(r.pos, w.x, w.z);
            if (off <= kRestLandTol) RestJudgeEnd(uid, w, kRestVerdictLanded, off, "applied, landed");
            else if (now - w.judgeAt >= kRestJudgeMs) RestJudgeEnd(uid, w, kRestVerdictMissed, off, "applied, MISSED (10 s)");
            ++it; continue;
        }
        if (w.attempted) { ++it; continue; }

        RagRead r;
        if (!ReadRagFields(c, &r))
        { if (!w.notedNotReady) { w.notedNotReady = true; ++g_restSkippedNotReady; } ++it; continue; }
        if (r.carried || r.ragCarried || r.inSomething != 0)
        { if (!w.notedCarried) { w.notedCarried = true; ++g_restSkippedCarried; } ++it; continue; }
        if (r.rag == 0 || !r.hasBodies || !r.active || !CharacterBuilt(c, 0))
        { if (!w.notedNotReady) { w.notedNotReady = true; ++g_restSkippedNotReady; } ++it; continue; }
        if (!r.settled)
        { if (!w.notedNotSettled) { w.notedNotSettled = true; ++g_restSkippedNotSettled; } ++it; continue; }

        const float dx = w.x - r.pos[0], dy = w.y - r.pos[1], dz = w.z - r.pos[2];
        const float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len <= kRestApplyMin) { w.attempted = true; ++it; continue; }   // already where the owner's body rests
        w.deltaLen = len;
        w.steps = cooprest::RestStepsFor(len);   // decision 61: clamp(|delta| / 4, 1, 30) - a big correction slides
        const int posted = cooprest::RestPost(&g_restTable, r.rag, r.anim, dx, dy, dz, w.steps);
        // R3-b item 1: that ragdoll still has a move in the table (restPostBusy) - not an attempt; posted on a later tick
        if (posted == cooprest::kRestPostBusy) { ++it; continue; }
        w.attempted = true;                     // one attempt per owner rest update
        if (posted != cooprest::kRestPosted)
        { ++g_restMissed; RestLog(uid, w, -1.0f, "NOT POSTED (table full)"); ++it; continue; }
        w.queued = true; w.rag = r.rag; w.queuedAt = now; w.judging = false;
        ++it;
    }
}

// G1-c fold M2: is the state a pulse was armed for gone? The arming-time predicates of GetupPulseTick, asked again
// every tick while G1's pulse is armed. Positive evidence only - an unreadable field is not a reason. 0 = still holds.
static const char* GetupArmedWhy(unsigned int uid, const GetupWatch& w, ::Character* c)
{
    if (!w.ownerUp || w.upAt != w.armedUpAt) return "ownerNotUp";
    if (net::IsUidMine(uid)) return "mine";
    if (!net::RemoteMayWriteStill(uid, w.peer)) return "control";
    if (GetupIsRagdoll(c) == 0) return "notRagdoll";
    RagRead r;
    if (ReadRagFields(c, &r) && (r.carried || r.ragCarried || r.inSomething != 0)) return "carried";   // +0x3D4 / +0x5A / +0x2F8
    if (BeingCarried(c)) return "carried";
    if (c->hasDied()) return "dead";
    if (PoseHoldsCopy(uid)) return "pose";
    if (GetupLocalReady(c, w.prone) == 0) return "notReadyLocal";   // T-178 crawl1: against the owner's prone
    return 0;
}

// G1 (read-getup) - see spawn.h. MAIN THREAD, every in-game frame. Reads, plus PulseCharacterQuiet / WithdrawPulse and
// (G1-b) the F247 dropGoals lever.
void GetupPulseTick()
{
    if (g_getup.empty()) return;
    if (EngineWritesBlocked()) return;
    const DWORD now = ::GetTickCount();
    std::map<unsigned int, GetupWatch>::iterator it = g_getup.begin();
    while (it != g_getup.end())
    {
        const unsigned int uid = it->first;
        GetupWatch& w = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c))
        {
            if (w.armedAt != 0) { ++g_getupWatchRemoved; GetupNoteFate(w, "watchRemoved"); }   // G1-c fold L3
            if (w.logPending) GetupLogAfter(uid, w, 0);
            g_getup.erase(it++); continue;
        }
        // G1-c: the AFTER half also waits for the pulse's fate (at most the 3 s cap)
        if (w.logPending && !w.logArmed && now - w.logAt >= kGetupAfterMs) GetupLogAfter(uid, w, c);
        // G1-b item 1, G1-c (T265): an armed pulse stays armed until the detour spends it or the state it was armed for
        // changes, read live every tick - the owner no longer up (or up again since: upAt moved), the copy no longer a
        // ragdoll, the uid ours; then the pass it would buy is not the one it was armed for. kGetupExpireMs (3 s) is only
        // the safety cap. The spend is LEARNED here: PulseArmedFor (read-only) reads 0, or WithdrawPulseFor's compare-exchange
        // loses to the detour's own spend (0) - exactly one of the two wins, as in G1-b.
        // G1-c fold H1/L1: only G1's OWN pulse is acted on - the arm generation recorded at arming must be unchanged.
        // Changed (another site armed the shared flag, or the slot is new) = 'superseded'; no slot = 'ungated'. Neither
        // refunds, and neither rolls back goals G1 did not buy.
        if (w.armedAt != 0)
        {
            const int st = PulseArmedFor(c, w.armedGen);
            if (st == 0) { GetupNoteSpent(w, now); w.armedAt = 0; }   // spent: what it chose is read at ~1 s
            else if (st == 2) { ++g_getupSuperseded; GetupNoteFate(w, "superseded"); GetupSettleUnspent(w, false); w.armedAt = 0; }
            else if (st < 0) { ++g_getupUngated; GetupNoteFate(w, "ungated"); GetupSettleUnspent(w, false); w.armedAt = 0; }
            else
            {
                const char* why = GetupArmedWhy(uid, w, c);   // G1-c fold M2: every arming-time predicate
                if (why != 0 || now - w.armedAt >= kGetupExpireMs)
                {
                    const int wd = WithdrawPulseFor(c, w.armedGen);
                    if (wd == 1)
                    {
                        // it never ran: nothing chose anything. Only a STATE-change withdrawal refunds its try (G1-c
                        // fold M1) - the 3 s cap keeps it; the 2 s gap (nextPulseAt) and the 60 s ring keep both.
                        if (why != 0)
                        {
                            ++g_getupWithdrawnState; GetupNoteFate(w, std::string("withdrawn(") + why + ")");
                            if (w.tries > 0) --w.tries;
                        }
                        else { ++g_getupPulseExpired; GetupNoteFate(w, "expired(" + S((long long)(now - w.armedAt)) + "ms)"); }
                        GetupSettleUnspent(w, why != 0 && std::string(why) == "notRagdoll");
                    }
                    else if (wd == 0) GetupNoteSpent(w, now);   // the detour won the race: spent
                    else if (wd == 2) { ++g_getupSuperseded; GetupNoteFate(w, "superseded"); GetupSettleUnspent(w, false); }
                    else { ++g_getupUngated; GetupNoteFate(w, "ungated"); GetupSettleUnspent(w, false); }
                    w.armedAt = 0;
                }
            }
        }
        if (net::IsUidMine(uid)) { w.lastPulseAt = 0; w.rollbackAt = 0; ++it; continue; }   // never act on an owned uid

        const int rag = GetupIsRagdoll(c);
        if (rag < 0) { ++it; continue; }
        // G1-b item 2: ~1 s after a SPENT pulse, a goal other than 'Getting up' / 'Waking up' is the AI choosing for itself
        // with the pass we gave it (T075's failure, through the same door) - its goals are cleared with the F247 lever,
        // guarded as combat guards it: still gated, no pass in flight, the owner still the remote peer.
        if (w.rollbackAt != 0 && now - w.rollbackAt >= kGetupRollbackMs)
        {
            const DWORD armed = w.rollbackAt;
            w.rollbackAt = 0;
            const std::string goal = GetupGoal(c);
            // T-178 crawl1: 'Getting up' (0x41) or 'Waking up' (0x34, the crippled get-up; taskdata-table.md task 52)
            const bool getupGoal = coopgetup::GetupGoalName(goal.c_str());
            if (!goal.empty() && !getupGoal && net::RemoteMayWriteStill(uid, w.peer) && IsSuppressed(c)
                && PulseArmGen(c) == w.armedGen   // G1-c fold H1: no other arming since - the goal is the pass G1 bought
                && WaitForNoAiPassOn(c, kGetupQuiesceMs) != 2 && GetupClearGoals(c))
            {
                ++g_getupGoalCleared;
                if (w.pulseCrawl) ++g_getupCrawlRolledBack;   // T-178 crawl1
                if (g_getupClearLogged < kGetupClearLogLimit)
                {
                    ++g_getupClearLogged;
                    DebugLog("[G1-b] getup goal cleared #" + S(g_getupClearLogged) + " uid=" + S(uid) + " goal='" + goal
                             + "' ragdoll=" + S((long long)rag) + " " + S((long long)(now - armed)) + "ms after the pulse");
                }
            }
        }
        if (rag == 0)
        {
            if (w.lastPulseAt != 0 && now - w.lastPulseAt <= kGetupDoneMs)
            { ++g_getupDone; if (w.pulseCrawl) ++g_getupCrawlDone; }   // T-178 crawl1
            w.lastPulseAt = 0;
            ++it; continue;
        }
        // still a ragdoll: a pulse outstanding more than 5 s is not 'done'; the fifth one is the give-up
        if (w.lastPulseAt != 0 && now - w.lastPulseAt > kGetupDoneMs)
        {
            if (w.tries >= kGetupMaxTries && !w.gaveUp) { w.gaveUp = true; ++g_getupGaveUp; }   // once per episode
            w.lastPulseAt = 0;
        }
        // T-178 crawl1: ownerUp carries the owner's own inRagdoll 0 - no time-based wait after it
        if (!w.ownerUp || w.tries >= kGetupMaxTries || w.unspent >= kGetupUnspentBudget)
            { ++it; continue; }
        if (w.nextPulseAt != 0 && (long)(w.nextPulseAt - now) > 0) { ++it; continue; }   // G1-c fold M1: the 2 s gap
        if (w.armedAt != 0 || w.rollbackAt != 0) { ++it; continue; }   // G1-b: the last pulse is still being judged
        RagRead r;
        if (!ReadRagFields(c, &r) || r.carried || r.ragCarried || r.inSomething != 0) { ++it; continue; }   // +0x3D4 / +0x5A / +0x2F8
        if (BeingCarried(c) || c->hasDied()) { ++it; continue; }
        if (!net::RemoteMayWriteStill(uid, w.peer)) { ++it; continue; }
        if (PoseHoldsCopy(uid)) { ++it; continue; }   // POSE (read-poses): no pulse for a copy in its owner's pose
        // G1-b item 1: the engine queues 'Getting up' 0x41 for THIS character only when its own medical and prone say it
        // can; a pulse before that is a decision opportunity with nothing to adopt. Wait.
        if (GetupLocalReady(c, w.prone) != 1)   // T-178 crawl1: the copy's prone must equal the owner's (0 or 2)
        {
            if (!w.notedNotReady)
            {
                w.notedNotReady = true; ++g_getupNotReadyLocal;
                if (w.prone == coopgetup::kProneCrawling) ++g_getupCrawlNotReadyLocal;   // T-178 crawl1
            }
            ++it; continue;
        }
        // G1-b item 4: 10 pulses per uid in any 60 s, whatever the owner's up/down edges do - the oldest of the last 10
        // must be more than 60 s old.
        const DWORD oldest = w.recent[w.recentNext];
        if (oldest != 0 && now - oldest < kGetupRateWinMs)
        {
            if (!w.notedCapped) { w.notedCapped = true; ++g_getupRateCapped; }
            ++it; continue;
        }

        // G1-c fold H1: another site's pulse is armed on this AI - arming now would share it. Wait.
        if (PulseArmed(c) == 1)
        {
            if (!w.notedBusy) { w.notedBusy = true; ++g_getupBusy; }
            ++it; continue;
        }
        const bool log = !w.logPending && g_getupLogged < kGetupLogLimit;
        const std::string before = log ? BehaviourString(c) + " ownerProne=" + S((long long)w.prone)
                                         + " sinceUp=" + S((long long)(now - w.upAt)) + "ms try=" + S((long long)(w.tries + 1))
                                   : std::string();
        if (!PulseCharacterQuiet(c))
        {
            if (!w.notedNotGated) { w.notedNotGated = true; ++g_getupNotGated; }
            ++it; continue;
        }
        ++w.tries; w.lastPulseAt = now; ++g_getupPulsed;
        w.pulseCrawl = (w.prone == coopgetup::kProneCrawling);   // T-178 crawl1: the copy adopts its own 'Waking up' 0x34
        if (w.pulseCrawl) ++g_getupCrawlArmed;
        w.armedAt = (now | 1u);   // never 0 - 0 means 'none'
        w.armedUpAt = w.upAt;     // G1-c: the episode it was armed for
        w.armedGen = PulseArmGen(c);   // G1-c fold H1: this pulse's generation (main thread: nothing armed since)
        w.nextPulseAt = (now + kGetupEveryMs) | 1u;
        w.notedBusy = false;
        w.recent[w.recentNext] = (now | 1u);
        w.recentNext = (w.recentNext + 1) % kGetupRateCap;
        w.notedCapped = false;
        if (log)
        {
            ++g_getupLogged;
            w.logPending = true; w.logAt = now; w.logNo = g_getupLogged; w.logBefore = before;
            w.logArmed = true; w.logFate.clear();
        }
        ++it;
    }
}

std::string GetupReportToken()
{
    return " getupPulsed=" + S(g_getupPulsed) + " getupDone=" + S(g_getupDone) + " getupGaveUp=" + S(g_getupGaveUp)
         + " getupNotGated=" + S(g_getupNotGated) + " getupWatched=" + S((long long)g_getup.size())
         + " getupNotReadyLocal=" + S(g_getupNotReadyLocal) + " getupPulseExpired=" + S(g_getupPulseExpired)
         + " getupGoalCleared=" + S(g_getupGoalCleared) + " getupRateCapped=" + S(g_getupRateCapped)
         + " getupWithdrawnState=" + S(g_getupWithdrawnState) + " getupSpent=" + S(g_getupSpent)
         + " getupSpentLe500=" + S(g_getupSpentLe500) + " getupSpentLe1500=" + S(g_getupSpentLe1500)
         + " getupSpentLe3000=" + S(g_getupSpentLe3000) + " getupSuperseded=" + S(g_getupSuperseded)
         + " getupUngated=" + S(g_getupUngated) + " getupWatchRemoved=" + S(g_getupWatchRemoved)
         + " getupBusy=" + S(g_getupBusy)
         // T-178 crawl1: the crawl cohort (owner prone 2), and the owner-side inRagdoll edges pushed at once
         + " getupCrawl[armed,done,notReadyLocal,rolledBack]=" + S(g_getupCrawlArmed) + "," + S(g_getupCrawlDone)
         + "," + S(g_getupCrawlNotReadyLocal) + "," + S(g_getupCrawlRolledBack) + " ownRagPushed=" + S(g_ownRagPushed);
}

// ---- POSE (read-poses 2026-09-23, user T262: an owner sat while its copy stood) --------------------------------------
// Bed / cage = Character +0x2F8 (1 bed, 2 cage) with the building's hand at +0x300; setBedMode 0x32E2B0 puts a character
// in (on, bed) or out (0, 0) and the animation picker plays the bed pose by itself while AnimationClass +0x210 is unset.
// Everything else in place (Task_SleepOnFloor 'sleeponfloor', seats through Task_OperateMachine 0x35BC30, machines and
// crafting) replays playAction(name, speed, 0, false) 0x51F920 every tick; the current action is AnimationClass
// (Character +0x448) +0x210 -> AnimationData, name std::string at +0x8. playAction is refused while a stumble (+0x228)
// is set and needs visuals (+0xA8). All Read (read-poses), not Confirmed live.
// POSE-b (review-pose): setBedMode DOES register the copy with the bed as a user (its vtable +0x4F8 call, Read) - a bed can
// list both the owner and its copy; that double occupancy is accepted (v1). Whether the bed already had a user is not
// cheaply readable (no known field), so nothing counts it. No machine is operated: an action pose only replays the animation.
unsigned long long kSetBedModeRva = 0; static coop::AddrReg kSetBedModeRva_reg("SetBedMode", &kSetBedModeRva);   /* the address table fills this. Steam_1.0.65 0x32E2B0 */   /* Character::setBedMode(bool on, UseableStuff* h) */
unsigned long long kPlayActionByNameRva = 0; static coop::AddrReg kPlayActionByNameRva_reg("PlayActionByName", &kPlayActionByNameRva);   /* Steam_1.0.65 0x51F920 */   /* AnimationClass::playAction(const std::string&, float speedMult, float initialWeight, bool isStumble) */
unsigned long long kStopActionRva = 0; static coop::AddrReg kStopActionRva_reg("StopAction", &kStopActionRva);   /* Steam_1.0.65 0x51D510 */   /* bool AnimationClass::stopAction() */
unsigned long long kGetDroppedRva = 0; static coop::AddrReg kGetDroppedRva_reg("GetDropped", &kGetDroppedRva);   /* Steam_1.0.65 0x5CC110 */   /* Character::getDropped(bool, bool) - a carried body put down */
unsigned long long kSetPrisonModeRva = 0; static coop::AddrReg kSetPrisonModeRva_reg("SetPrisonMode", &kSetPrisonModeRva);   /* arrest2: Steam_1.0.65 0x3305C0 */   /* Character::setPrisonMode(bool on, UseableStuff* cage) - prototype per the arrest2 engine read */

// P42 (defined below, before PrisonTick): a prisoner's restraint locks carried to every game
bool RestraintCageRescue(unsigned int uid, bool sawCaged, bool oursOut, bool guardFresh, bool guardArrest, bool pardonLookOver);
void RestraintApplyRemote(const cooprison::PrisonMsg& m, unsigned int fromPeer);
void RestraintSafePointDrain();
void RestraintForget(unsigned int uid);
void RestraintWatch();
std::string RestraintReportToken();

namespace {

typedef void (*SetBedModeFn)(::Character* c, bool on, void* useable);
typedef void (*PlayActionByNameFn)(void* anim, const std::string& name, float speedMult, float initialWeight, bool isStumble);
typedef bool (*StopActionFn)(void* anim);
typedef void (*GetDroppedFn)(::Character* c, bool a, bool b);
typedef void (*SetPrisonModeFn)(::Character* c, bool on, void* cage);   /* arrest2 */

const size_t kPoseInSomethingOff = 0x2F8;   // Character : int (1 bed, 2 cage)
const size_t kPoseBedHandIdOff   = 0x308;   // hand at +0x300; its five id fields at +0x8..+0x1C (type, container, containerStamp, index, serial)
const size_t kPoseAnimOff        = 0x448;   // Character : AnimationClass*
const size_t kPoseActionOff      = 0x210;   // AnimationClass : AnimationData* current action
const size_t kPoseStumbleOff     = 0x228;   // AnimationClass : stumble - playAction refuses while set
const size_t kPoseVisualsOff     = 0xA8;    // AnimationClass : visuals - playAction needs them
const size_t kPoseActNameOff     = 0x8;     // AnimationData : std::string name
const size_t kPoseActLoopedOff   = 0x8A;    // AnimationData : looped flag (POSE-b: only a looped action is a pose)
const size_t kPoseMoveSpeedOff   = 0xB8;    // CharMovement : speedNow (as replicate.cpp reads it)
const float  kPoseOwnerStillSpeed = 0.5f;  // POSE-b: the owner counts as standing still below this (units/s)
const float  kPoseApproachDist   = 1.6f;    // POSE-b: the copy poses only this close (horizontal) to the owner's streamed spot;
                                            // just above the drive's 1.5 arrival radius, where the push stops the copy
const DWORD  kPoseConfirmMs      = 30000;   // POSE-b: a pose no STATE re-confirmed for this long expires on the copy (POSE-c: 30 s - the periodic STATE reaches each of ~400 owned characters only every ~8-10 s, T253/T254)
const DWORD  kPoseRepushMs       = 10000;   // the owner re-sends a held pose this often, apart from the round-robin STATE, so a copy's
                                            // pose never waits for a refresh cycle that many owned characters make longer than kPoseConfirmMs
const long long kPoseLogLimit    = 16;
const DWORD kPoseBedRetryMs      = 1000;    // a copy not (yet) in its bed: resolve + setBedMode at most once a second
const long long kPoseMissLogLimit = 40;    /* BED1: bed-miss lines - their own cap, apart from the apply cap */
const int kPoseLayoutCap          = 512;   /* BED1: interior pieces one walk reads at most (the set is bounded, not trusted) */

// POSE-b (review-pose): only a LOOPED action of an owner standing still is a pose; a one-off is never sent (v1). Of the
// looped ones these are never mirrored (case-insensitive substrings): fighting stances, K1's carry lifts / pickups, G1's
// get-ups, and manning a turret (v1). A stumble is refused by its own flag.
const char* const kPoseExcluded[] = { "combat", "stance", "block", "shoulder", "pickup", "standing up", "getting up", "turret" };
// ... and the idle fidgets: a name that STARTS with 'idle' ('sitting idle', 'kneeling hostage idle' are held poses).
const char* const kPoseIdlePrefix = "idle";

long long g_poseSent = 0;            // owner: a pose change pushed at once
long long g_poseRepushed = 0;        // owner: a held pose re-sent after kPoseRepushMs
long long g_poseExcludedSeen = 0;    // owner: a change whose action was excluded (combat / stumble / turret ...)
long long g_poseAppliedAction = 0;   // copy: a replayed action taken (first play per want)
long long g_poseAppliedBed = 0;      // copy: setBedMode(copy, 1, bed) called
long long g_poseCleared = 0;         // copy: kind 0 undid an applied pose
long long g_poseBedUnresolved = 0;   // copy: the bed's key named nothing on this game (once per want)
long long g_poseBedFaulted = 0;      // copy: setBedMode / stopAction faulted
long long g_poseActionRefused = 0;   // copy: playAction refused (stumble / no visuals) or faulted - edges, not frames
long long g_poseHeldFrames = 0;      // copy: frames its movement drive held it still
long long g_poseOneOffSeen = 0;      // owner: a change whose action was a one-off (not looped) - not sent (POSE-b)
long long g_poseOwnerMovingSeen = 0; // owner: a change whose looped action ran while the owner moved - not sent (POSE-b)
long long g_posePendingApproach = 0; // copy: an action pose waiting for the copy to reach the owner's spot (once per want)
long long g_poseUndoneHandoff = 0;   // copy: an applied pose undone because a handoff dropped its record (POSE-b)
long long g_poseExpired = 0;         // copy: a pose no STATE re-confirmed for 10 s - undone and dropped (POSE-b)
long long g_poseLogged = 0;
long long g_poseBedViaLayout = 0;    // BED1 copy: a bed found among the outer building's interior pieces (once per want)
long long g_poseBedOuterMissing = 0; // BED1 copy: the outer building's key named nothing here yet (once per want; retried)
long long g_poseBedLayoutMiss = 0;   // BED1 copy: the outer building was here and no interior piece matched (once per want)
long long g_poseMissLogged = 0;

struct PoseOwn
{
    coopstate::PoseWire pw;
    unsigned char hand[kHandIdBytes];   // the bed hand the cached key was built from
    bool keyTried;
    DWORD pushedAt;                     // when this game last sent this character's pose
};
std::map<unsigned int, PoseOwn> g_poseOwn;

struct PoseWant
{
    coopstate::PoseWire pw;
    std::string action;
    unsigned int peer;
    int applied;          // 0 nothing, 1 an action is being replayed, 2 put in the bed
    bool held;
    bool counted;         // the apply counter / log line for this want is spent
    bool bedMiss;         // the bed's key resolved to nothing at the last try
    bool bedMissCounted;
    bool refusedNow;      // the last playAction was refused (edge counting)
    DWORD bedAt;          // last resolve + setBedMode try (0 = never)
    bool approachCounted; // posePendingApproach booked for this want
    DWORD confirmedAt;    // the last STATE that carried this want (POSE-b: 10 s without one = expired)
    // BED1: the layout fallback's per-want state - reset whenever the want changes (PoseWantResetBed)
    bool layoutHandSet;   // layoutHand names the interior piece a walk matched; re-found by handle, no re-walk
    unsigned int layoutHand[5];
    bool layoutFinal;     // a walk over a populated interior matched nothing: not re-walked for this want
    bool viaCounted;
    bool outerMissCounted;
    bool layoutMissCounted;
    int missLogged;       // the last miss outcome logged for this want (0 none)
    bool outerHandSet;    // BED1-b (review M2): outerHand is the outer building's own handle at the last walk
    unsigned int outerHand[5];
    DWORD dropAt;         // a limp copy's getDropped posted at (0 = none waiting); the bed follows on a later try
    bool dropGaveUp;      // its request was not carried out in time: no bed for this want while it stays limp
};
std::map<unsigned int, PoseWant> g_poseWant;

// POD reads under SEH (no object with a destructor here - C2712).
int PoseReadPod(const void* c, int* inSomething, unsigned char* hand, char* act, int* actLong, int* stumble, int* looped,
                float* speed)
{
    __try
    {
        *inSomething = *(const int*)((const char*)c + kPoseInSomethingOff);
        std::memcpy(hand, (const char*)c + kPoseBedHandIdOff, kHandIdBytes);
        act[0] = 0; *actLong = 0; *stumble = 0; *looped = 0; *speed = 0.0f;
        const char* mv = (const char*)((const ::Character*)c)->movement;
        if (mv != 0) *speed = *(const float*)(mv + kPoseMoveSpeedOff);
        const char* anim = *(const char* const*)((const char*)c + kPoseAnimOff);
        if (anim == 0) return 1;
        *stumble = (*(void* const*)(anim + kPoseStumbleOff) != 0) ? 1 : 0;
        const char* data = *(const char* const*)(anim + kPoseActionOff);
        if (data == 0) return 1;
        *looped = (*(const unsigned char*)(data + kPoseActLoopedOff) != 0) ? 1 : 0;
        const std::string* name = (const std::string*)(data + kPoseActNameOff);
        const size_t len = name->size();
        if (len > coopstate::kPoseStrMax) { *actLong = 1; return 1; }
        if (len > 0) std::memcpy(act, name->c_str(), len);
        act[len] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int PoseInSomethingPod(const void* c)
{
    __try { return *(const int*)((const char*)c + kPoseInSomethingOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// 1 played, 0 refused (stumble set / no visuals / no AnimationClass), -1 faulted.
int PosePlayPod(PlayActionByNameFn fn, const void* c, const std::string* name)
{
    __try
    {
        char* anim = *(char* const*)((const char*)c + kPoseAnimOff);
        if (anim == 0) return 0;
        if (*(void* const*)(anim + kPoseStumbleOff) != 0) return 0;
        if (*(void* const*)(anim + kPoseVisualsOff) == 0) return 0;
        fn(anim, *name, 1.0f, 0.0f, false);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int PoseStopPod(StopActionFn fn, const void* c)
{
    __try
    {
        char* anim = *(char* const*)((const char*)c + kPoseAnimOff);
        if (anim == 0) return 0;
        if (*(void* const*)(anim + kPoseActionOff) == 0) return 0;
        fn(anim);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int PoseBedPod(SetBedModeFn fn, ::Character* c, bool on, void* bed)
{
    __try { fn(c, on, bed); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Task_PutInSomething's bed branch puts the body down with getDropped(body, 0, 0) 0x5CC110 BEFORE setBedMode
// (decomp_358610.txt:135-167): it clears _isBeingCarried (+0x3D4), resets the carry pose and only POSTS a ragdoll request
// (0x5C7E90 writes 3 bytes at +0x410) that the character's own update carries out later (BedDropPendingPod). So the setBedMode,
// the move and stopAction run on a later pass, once the request is gone. 1 called, -1 faulted, 0 no address.
int BedGetDroppedPod(::Character* c)
{
    if (kGetDroppedRva == 0) return 0;
    const GetDroppedFn fn = (GetDroppedFn)((uintptr_t)::GetModuleHandleA(0) + kGetDroppedRva);
    __try { fn(c, false, false); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// getDropped's ragdoll request: 0x5C7E90 allocates 3 bytes at Character+0x410; the character's update 0x5CE6A0 carries it out
// (_carryMode 0x5CDF30), frees it and nulls the field (decomp_5c7e90.txt, decomp_5ce6a0.txt:25-31). 1 pending, 0 none,
// -1 unreadable.
const size_t kCharRagRequestOff = 0x410;
int BedDropPendingPod(const ::Character* c)
{
    __try { return *(void* const*)((const char*)c + kCharRagRequestOff) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// After getDropped the rest of the bed order waits until the request is gone and the body is no ragdoll: on the owner at
// least kBedDropWaitSafePoints safe points AND kBedDropWaitMs, on a limp copy kBedDropWaitMs; then it gives up.
const int   kBedDropWaitSafePoints = 60;
const DWORD kBedDropWaitMs         = 5000;
const DWORD kBedRecheckMs          = 1000;   // owner: the second look at the bed and the spot before it counts as applied
long long g_bedCopyDropPosted = 0;    // copy: a limp copy's getDropped posted (the first pass)
long long g_bedCopyDropTimeout = 0;   // copy: request still pending or still a ragdoll after kBedDropWaitMs - out of the bed for this want

// arrest2: setPrisonMode 0x3305C0 (c, on, cage). 1 called, -1 faulted, 0 no address.
int PosePrisonPod(::Character* c, bool on, void* cage)
{
    if (kSetPrisonModeRva == 0) return 0;
    const SetPrisonModeFn fn = (SetPrisonModeFn)((uintptr_t)::GetModuleHandleA(0) + kSetPrisonModeRva);
    __try { fn(c, on, cage); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// arrest2 engine read 1-3 (cloud/ANSWERS.md "arrest2 - answers"): setPrisonMode, isFreeSlot and the sentence run at the K2
// safe point 0x7D17E0 (worker paused) - the cage's occupant set is walked and changed by the AI worker. The main-thread code
// below only QUEUES; PrisonSafePointDrain (called from combat.cpp's detour_tsRagdollUpdates) makes the calls.
const size_t kCharSentenceHoursOff = 0x190;  // Character : sentence hours (bm+0xA0, float; the start is bm+0x98)
const size_t kCharSentenceStartOff = 0x188;  // Character : sentence start (bm+0x98, double, game hours)
const size_t kCharPardonFactionOff = 0x138;   // Character : pardon faction (Task_ReleasePrisoner writes the releaser's faction)
const size_t kCharPardonUntilOff   = 0x140;   // Character : pardon until (double, game hours = now + 1.0000001639)
const double kPardonHours          = 1.0000001639;
const float  kCopyHoldSentenceHours = 1.0e6f;  // review-arrest3 H1: a copy caged on its owner's word never ends its sentence here
const float  kOwnSentenceMarginHours = 2.0f;   // user decision 2026-09-25: the arrest's own game frees first; ours is the fallback
struct PrisonOp { unsigned int uid; bool on; void* cage; bool own; bool release; void* relFac; int relMode; bool pardon; int sentence; unsigned int peer;
                  bool bed; float pos[3]; char bedKey[cooprison::kPrisonMaxKey + 1]; };   /* M7b slice 1: peer = the guard's game that asked (a refusal goes back to it); bed: `cage` is a bed, `pos` the spot, `bedKey` its key */
std::vector<PrisonOp> g_prisonOps;
// The owner's bed hand-off over several safe points (BedHandoffDrain): stage 1 getDropped posted, waiting for the request to be
// carried out; stage 2 in the bed on the spot, waiting kBedRecheckMs for the second look.
struct BedHandoff { PrisonOp op; int stage; int safePoints; DWORD since; };
std::vector<BedHandoff> g_bedHandoffs;
const size_t kBedHandoffCap = 64;
long long g_bedDropPosted = 0;        // owner: stage 1 begun (getDropped posted)
long long g_bedDropWaited = 0;        // owner: safe points stage 1 waited, all hand-offs together
long long g_bedDropNotConsumed = 0;   // owner: the request still pending at the bound - refused for now
long long g_bedDropStillRagdoll = 0;  // owner: the request carried out, still a ragdoll at the bound - refused for now
long long g_bedSlidAway = 0;          // owner: the second look found it out of the bed or off the spot - taken out, refused for now
long long g_bedHandoffSuperseded = 0; // owner: a newer request for the same character replaced a waiting one (refused for now)
long long g_bedHandoffFull = 0;       // owner: kBedHandoffCap hand-offs already waiting - refused for now
std::map<unsigned int, bool> g_cageOutQueued;   // arrest3: copy uid -> a cage-out of OURS was queued (its leaving is not a release)
std::map<unsigned int, bool> g_cageQueued;   // copy uid -> PoseApplyCage queued a cage-in (the next "already caged" is ours)
long long g_prisonOpsDropped = 0;            // the queue was full (64) - retried by the caller on a later tick
// review-arrest2 HIGH: copies caged on their OWNER's word (a cage POSE want seen while caged here) - never reported back as a
// guard's caging, even in the frame the want flips away or when a cage-out is lost. Erased when +0x2F8 != 2 is observed.
std::map<unsigned int, bool> g_poseCaged;
// review-arrest3 H1 (user decision 2026-09-25: the game where the arrest happened decides the release): copies THIS game's
// guard caged (PrisonWatchCopies reported them) - only these carry a sentence from this game, so only their release is told.
// jail2: each entry keeps the sentence this game's jailer gave, so a copy that a guard here lifts out and the mod puts back
// keeps that sentence - never the owner's-word hold. The engine's worker writes the cage state a moment BEFORE the sentence
// (setPrisonMode, then notifyStartPrisonSentence), so the value is re-read every time the copy is seen caged under this
// arrest and only a sane one is kept (review-jail2 Q3).
struct GuardArrest { double start; float hours; bool known; int keptOuts; };
typedef std::map<unsigned int, GuardArrest> GuardCagedMap;
GuardCagedMap g_guardCaged;
const int kGuardKeptOutsMax = 3;         // review-jail2 Q2b: after this many put-backs the copy falls back to the owner's word
long long g_guardArrestKept = 0;         // jail2: seen out of the cage and kept as the same arrest (a guard here lifted it out, or a
                                         // release whose pardon was still missing on the second look)
long long g_guardSentenceRestored = 0;   // jail2: put back with no sane sentence left (missing, 0 or the hold) - the saved one written
long long g_guardKeptOutsCapped = 0;     // review-jail2 Q2b: put back kGuardKeptOutsMax times - the arrest was given up here
GuardArrest GuardArrestReadPod(::Character* c)
{
    GuardArrest a;
    a.start = 0.0; a.hours = 0.0f; a.known = false; a.keptOuts = 0;
    __try
    {
        a.start = *(const double*)((const char*)c + kCharSentenceStartOff);
        a.hours = *(const float*)((const char*)c + kCharSentenceHoursOff);
        a.known = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { a.known = false; }
    return a;
}
bool GuardSentenceSane(const GuardArrest& a) { return a.known && a.start > 0.0 && a.hours > 0.0f && a.hours < kCopyHoldSentenceHours; }
// the live sentence, kept only when sane (a caging seen before the engine wrote the sentence saves nothing)
void GuardArrestRefresh(GuardArrest& saved, ::Character* c)
{
    GuardArrest live = GuardArrestReadPod(c);
    if (GuardSentenceSane(live))
    {
        if (live.start != saved.start) saved.keptOuts = 0;   // jail4 (review R3): a new sentence - its lift-outs are counted afresh
        saved.start = live.start; saved.hours = live.hours; saved.known = true;
    }
}
int GuardArrestWritePod(::Character* c, double start, float hours)
{
    __try
    {
        *(double*)((char*)c + kCharSentenceStartOff) = start;
        *(float*)((char*)c + kCharSentenceHoursOff) = hours;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
std::map<unsigned int, int> g_outNoPardon;   // recheck-arrest3 2: out of the cage with no pardon yet - looked at again next tick
long long g_copyHoldSentence = 0;        // H1: copies caged on their owner's word given a never-ending sentence here
long long g_copyHoldSentenceFailed = 0;
long long g_copyLeftNotReleased = 0;     // M1/M2: such a copy left the cage with no guard's fresh pardon - put back, not told
long long g_releaseNoClock = 0;          // the world clock could not be read - no pardon written
long long g_poseCageInExec = 0;              // review-arrest2 4: copy cage-ins actually made at the safe point (a flicker shows here)
long long g_prisonNoHand = 0;                // review-arrest2 MEDIUM: setPrisonMode left +0x2F8 == 2 with no cage hand (tryOperate refused)
// relFac / relMode (release only): the releaser's faction resolved here and how (0 = unknown - the cage's faction, 1 = named,
// 2 = a player freed it). pardon (a copy's take-out after REFUSED): the copy gets the pardon, so this game's guards do not
// arrest it again at once (review-arrest3 M3).
void PrisonQueueOp(unsigned int uid, bool on, void* cage, bool own, bool release = false, void* relFac = 0, int relMode = 0,
                   bool pardon = false, int sentence = cooprison::kPrisonSentenceUnknown, unsigned int peer = 0)
{
    if (g_prisonOps.size() >= 64) { ++g_prisonOpsDropped; return; }
    PrisonOp o; o.uid = uid; o.on = on; o.cage = cage; o.own = own; o.release = release; o.relFac = relFac; o.relMode = relMode;
    o.pardon = pardon; o.sentence = sentence; o.peer = peer;
    o.bed = false; o.pos[0] = 0.0f; o.pos[1] = 0.0f; o.pos[2] = 0.0f; o.bedKey[0] = 0;
    g_prisonOps.push_back(o);
    if (!on && !own) g_cageOutQueued[uid] = true;
}
// Owner: put this game's own character `uid` in `bed` at the safe point, then on the spot the rescuer's game sent.
bool BedQueueOp(unsigned int uid, void* bed, const float* pos, unsigned int peer, const std::string& key)
{
    if (g_prisonOps.size() >= 64) { ++g_prisonOpsDropped; return false; }
    PrisonOp o; o.uid = uid; o.on = true; o.cage = bed; o.own = true; o.release = false; o.relFac = 0; o.relMode = 0;
    o.pardon = false; o.sentence = cooprison::kPrisonSentenceUnknown; o.peer = peer;
    o.bed = true; o.pos[0] = pos[0]; o.pos[1] = pos[1]; o.pos[2] = pos[2];
    std::strncpy(o.bedKey, key.c_str(), cooprison::kPrisonMaxKey); o.bedKey[cooprison::kPrisonMaxKey] = 0;
    g_prisonOps.push_back(o);
    return true;
}

// arrest3 (cloud/ANSWERS.md "arrest3 - answers" 2): the guard's game frees its COPY on its own sentence clock (a guard's
// findPrisonerFreeToGo -> Task_ReleasePrisoner 0x357040). The owner is told once (MSG_PRISON kind RELEASE); the copy is not put
// back in the cage for kReleaseHoldMs while the owner answers (its cage POSE ends when it releases its own character).
std::map<unsigned int, DWORD> g_releaseHold;
const DWORD kReleaseHoldMs = 20000;
long long g_prisonReleaseSent = 0;
long long g_prisonReleaseSendFailed = 0;
// review-arrest3 M2: the releaser's faction travels in the (otherwise empty) cage key - its stringID, or "@player" when a player
// freed it (the engine then clears no bounty), or "" when unreadable (the owner then uses the cage's faction).
int FactionSidPod(const void* f, char* buf, int cap)
{
    const void* gd = 0;
    __try { gd = *(const void* const*)((const char*)f + 0x240); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }   // Faction +0x240 GameData*
    if (!PlausibleObject(gd)) return 0;
    __try   // GameData +0x58 stringID, an MSVC std::string (size +0x10, capacity +0x18, inline below 16)
    {
        const char* str = (const char*)gd + 0x58;
        const size_t len = *(const size_t*)(str + 0x10);
        const size_t res = *(const size_t*)(str + 0x18);
        const char* q = (res >= 16) ? *(const char* const*)str : str;
        if (len == 0 || len > (size_t)(cap - 1)) return 0;   // too long for the wire key: sent empty (cage faction used)
        for (size_t i = 0; i < len; ++i) buf[i] = q[i];
        buf[len] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void PrisonNoteReleasedHere(unsigned int uid, DWORD now, void* relFac)
{
    g_releaseHold[uid] = now | 1u;
    cooprison::PrisonMsg m; m.uid = uid; m.kind = cooprison::kPrisonRelease;
    if (relFac != 0 && PlausibleObject(relFac))
    {
        if (IsPlayerFaction((::Faction*)relFac) || IsPeerFaction((::Faction*)relFac)) m.cageKey = "@player";
        else
        {
            char sid[cooprison::kPrisonMaxKey + 1];
            if (FactionSidPod(relFac, sid, (int)sizeof(sid))) m.cageKey = sid;
        }
    }
    if (net::SendPrison(m)) ++g_prisonReleaseSent; else ++g_prisonReleaseSendFailed;
}
// P42: a rescue here (PoseApplyCage, prisonwire.h CageRescueRelease) - the copy left a cage that stood open here and in its owner's
// word. Told to the owner as a player's release ("@player": no bounty cleared), held out of the cage while the owner answers.
long long g_cageRescueSent = 0;
void PrisonNoteRescuedHere(unsigned int uid, DWORD now)
{
    g_releaseHold[uid] = now | 1u;
    cooprison::PrisonMsg m; m.uid = uid; m.kind = cooprison::kPrisonRelease; m.cageKey = "@player";
    if (net::SendPrison(m)) { ++g_prisonReleaseSent; ++g_cageRescueSent; } else ++g_prisonReleaseSendFailed;
    DebugLog("[LOCK] rescue uid=" + S((long long)uid) + " left a cage open here and in its owner's word - told to the owner as a player's release");
}

// BED HAND-OFF (MSG_PRISON kinds BED IN / BED REFUSED, the cage road's messages). A rescuer here carried another game's
// knocked-out character (our copy) and its Task_PutInSomething put the copy in a bed (setBedMode 0x32E2B0, then the bed spot
// and a teleport 0x5C9BF0). The owner runs that character and heals it, so it is asked to put its own character in its own
// copy of the bed; until it answers the copy stays in the bed here, held still (PoseHoldsCopy).
// Rescuer's side, per copy: the bed key asked about and how the ask stands.
const int kBedAskPending   = 0;   // sent, no answer yet (the copy is held in the bed)
const int kBedAskSettled   = 1;   // the owner's pose says bed: the pose code runs the copy's bed from here
const int kBedAskRefusedIn = 2;   // BED REFUSED arrived: taken out at the next scan
const int kBedAskOut       = 3;   // taken out by this code (refusal or no answer)
struct BedAsk { std::string key; DWORD sentAt; int state; };
std::map<unsigned int, BedAsk> g_bedAsk;
std::map<unsigned int, DWORD> g_bedCarriedAt;            // copy uid -> the last scan that saw it carried (not by another game's character)
std::map<unsigned int, std::string> g_bedRefusedKey;     // copy uid -> a bed key the owner refused for good (full, not a bed, dead): not asked again
// A temporary refusal is not remembered for good, but a rescuer (an NPC's own AI) that puts the character back in the same bed
// again and again is not asked without end: kBedTempRefusalTries refusals of one bed for one character, each within
// kBedCooldownMs of the last, and that bed is taken out without asking for kBedCooldownMs.
const int kBedTempRefusalTries = 3;
const DWORD kBedCooldownMs = 120000;
struct BedTempRefusal { std::string key; int n; DWORD lastAt; DWORD coolAt; };   // coolAt 0 = no cooldown running
std::map<unsigned int, BedTempRefusal> g_bedTempRefused;
long long g_bedCooldownStarted = 0;   // rescuer: a bed's kBedTempRefusalTries-th temporary refusal for one character - cooldown begun
long long g_bedCooldownTakeOut = 0;   // rescuer: a put-down into a cooling bed taken out without asking
const DWORD kBedCarryEdgeMs = 3000;    // "carried, then in a bed" within this is the rescuer's put-down (scans are every 10th frame)
const DWORD kBedNoAnswerMs  = 20000;   // no owner bed pose and no refusal within this: the copy comes out of the bed
long long g_bedSent = 0;              // rescuer: BED IN sent
long long g_bedRecv = 0;              // owner: BED IN received for a character this game drives
long long g_bedApplied = 0;           // owner: own character put in the bed (setBedMode verified) at the safe point
long long g_bedRefused = 0;           // owner: BED REFUSED sent; rescuer: BED REFUSED received for a waiting ask
long long g_bedNoAnswer = 0;          // rescuer: no answer within kBedNoAnswerMs - the copy taken out
long long g_bedLeftUnexplained = 0;   // rescuer: a waiting copy left its bed and none of this code took it out
long long g_bedRefusalStale = 0;      // rescuer: BED REFUSED with no waiting ask, or naming another bed than the waiting ask's - ignored
long long g_bedNotAtSpot = 0;         // owner: in the bed but not within kBedSpotTolerance of the sent spot after the move - undone, refused
long long g_bedCopyMoved = 0;         // a limp copy put in its owner's bed and moved onto the owner's streamed spot
long long g_bedCopyNotMoved = 0;      // the same, with no streamed spot or a failed move
const float kBedSpotTolerance = 20.0f;   // owner: horizontal units (~2 m) between the sent spot and where the move left it
bool BedAskHolds(unsigned int uid)
{
    if (g_bedAsk.empty()) return false;
    std::map<unsigned int, BedAsk>::const_iterator it = g_bedAsk.find(uid);
    return it != g_bedAsk.end() && (it->second.state == kBedAskPending || it->second.state == kBedAskRefusedIn);
}

// review-arrest3 M1: the per-copy prison marks are claims about one copy in one world - forgotten when the copy goes, changes
// owner, or the world is torn down (PrisonWorldTeardown).
void PrisonForgetCopy(unsigned int uid)
{
    g_poseCaged.erase(uid); g_cageOutQueued.erase(uid); g_cageQueued.erase(uid); g_releaseHold.erase(uid); g_guardCaged.erase(uid);
    g_outNoPardon.erase(uid);
    RestraintForget(uid);   /* P42 */
    g_bedAsk.erase(uid); g_bedCarriedAt.erase(uid); g_bedRefusedKey.erase(uid); g_bedTempRefused.erase(uid);
}

bool PoseActionExcluded(const char* name)
{
    char low[coopstate::kPoseStrMax + 1];
    size_t n = 0;
    for (; n < coopstate::kPoseStrMax && name[n] != 0; ++n)
    {
        const char ch = name[n];
        low[n] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    }
    low[n] = 0;
    for (size_t i = 0; i < sizeof(kPoseExcluded) / sizeof(kPoseExcluded[0]); ++i)
        if (std::strstr(low, kPoseExcluded[i]) != 0) return true;
    if (std::strncmp(low, kPoseIdlePrefix, 4) == 0) return true;
    return false;
}

// Owner: the bed's P7n building key from its hand (ids: type, container, containerStamp, index, serial). 1 = built.
// BED1 (T263/T264): `outerOut` also receives the key of the building whose interior LAYOUT made the bed (Building+0x238 ->
// Layout+0x90) - the one the copy's zone-list resolver CAN find - or stays empty when the bed is not layout furniture.
int PoseBedKeyFromHand(const unsigned char* id, char* out, int cap, char* outerOut, int outerCap)
{
    unsigned int f[5];
    std::memcpy(f, id, sizeof(f));
    outerOut[0] = 0;
    if (f[3] == 0 && f[4] == 0) return 0;
    hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
    void* bld = (void*)h.asBuilding();
    if (bld == 0 || !PlausibleObject(bld)) return 0;
    if (ObjectPositionKey(bld, out, cap, 0, 3, 1) == 0) return 0;   // countIt 3: not booked to the box / lever / door populations
    if (LayoutOuterKey(bld, outerOut, outerCap) == 0) outerOut[0] = 0;
    return 1;
}

void* P11CageFromPoseHand(const unsigned char* id);   /* below (the arrest2 block) - the object a bed / cage hand names */
int PoseHandPod(const void* c, unsigned char* hand)
{
    __try { std::memcpy(hand, (const char*)c + kPoseBedHandIdOff, kHandIdBytes); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// the bed (or cage) object the character's +0x300 hand names now, 0 = none / unreadable
void* PoseCurrentBedObject(::Character* c)
{
    unsigned char hd[kHandIdBytes];
    if (!PoseHandPod(c, hd)) return 0;
    return P11CageFromPoseHand(hd);
}

// review-arrest2 MEDIUM: a cage comes off only when the OWNER left it (its want changed) - never on a handoff (the character
// is ours now, a real prisoner) nor on an expiry (a link hiccup must not free a prisoner).
void PoseUndo(::Character* c, PoseWant& w, bool cageOut = true)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (w.applied == 1 && PoseStopPod((StopActionFn)(base + kStopActionRva), c) < 0) ++g_poseBedFaulted;
    if (PoseInSomethingPod(c) == 1 && PoseBedPod((SetBedModeFn)(base + kSetBedModeRva), c, false, 0) < 0) ++g_poseBedFaulted;
    if (cageOut && w.applied == 3 && PoseInSomethingPod(c) == 2) PrisonQueueOp(FindSpawnedUid(c), false, 0, false);   // arrest2: out of the cage (at the safe point)
    w.applied = 0;
    w.held = false;
}

void PoseLogApply(unsigned int uid, const PoseWant& w, int kind)
{
    if (g_poseLogged >= kPoseLogLimit) return;
    ++g_poseLogged;
    DebugLog("[POSE] apply #" + S(g_poseLogged) + " uid=" + S((long long)uid) + " kind=" + S((long long)kind)
             + " action='" + std::string(w.pw.action) + "' bedKey='" + std::string(w.pw.bedKey) + "'"
             + " outerKey='" + std::string(w.pw.outerKey) + "'" + (w.layoutHandSet ? " via=layout" : ""));
}

// BED1: the want's layout-fallback state, cleared whenever the want is created or changes.
void PoseWantResetBed(PoseWant& w)
{
    w.layoutHandSet = false;
    std::memset(w.layoutHand, 0, sizeof(w.layoutHand));
    w.layoutFinal = false; w.viaCounted = false; w.outerMissCounted = false; w.layoutMissCounted = false;
    w.missLogged = 0;
    w.outerHandSet = false;
    std::memset(w.outerHand, 0, sizeof(w.outerHand));
    w.dropAt = 0; w.dropGaveUp = false;
}

// BED1: EVERY MISS OF A WANT IS LOGGED - the bed key and the outer key, with what the fallback saw - under its own
// 40-line cap. One line per want per OUTCOME, not one per 1 s retry: a want that keeps missing the same way is the same
// miss. outcome 1 outer building not loaded here, 2 no interior piece matched, 3 no outer key (not layout furniture on
// the owner, or its key could not be built), 4 two pieces matched and nothing broke the tie, 5 (BED1-b) the walk was
// torn by a zone deactivation or ended early - retried.
void PoseLogBedMiss(unsigned int uid, PoseWant& w, int outcome, int pieces, int keyed, int listed, int truncated,
                    const char* nearestKey, long nearestTenths)
{
    if (w.missLogged == outcome) return;
    w.missLogged = outcome;
    if (g_poseMissLogged >= kPoseMissLogLimit) return;
    ++g_poseMissLogged;
    const char* what = outcome == 1 ? "outerNotLoaded" : outcome == 2 ? "noPieceMatched" : outcome == 3 ? "noOuterKey"
                     : outcome == 5 ? "tornWalk" : "ambiguous";
    std::string tail;
    if (outcome == 2 || outcome == 4 || outcome == 5)
        tail = " piecesRead=" + S((long long)pieces) + " piecesKeyed=" + S((long long)keyed) + " setSize=" + S((long long)listed)
             + " truncated=" + S((long long)truncated) + " nearest='" + std::string(nearestKey != 0 ? nearestKey : "")
             + "' nearestTenths=" + S((long long)nearestTenths) + (w.layoutFinal ? " (final for this want)" : " (retried)");
    DebugLog("[POSE] bed miss #" + S(g_poseMissLogged) + " uid=" + S((long long)uid) + " outcome=" + what
             + " bedKey='" + std::string(w.pw.bedKey) + "' outerKey='" + std::string(w.pw.outerKey) + "'" + tail);
}

// BED1: a piece a walk already matched, re-found through the engine's own handle check and re-keyed. 0 = gone, or the
// handle now names something else - the cache is dropped and the next miss walks again.
void* PoseBedByCachedHand(PoseWant& w)
{
    if (!w.layoutHandSet) return 0;
    const unsigned int* f = w.layoutHand;
    hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
    void* b = (void*)h.asBuilding();
    char k[coopstate::kPoseStrMax + 1];
    k[0] = 0;
    if (b != 0 && PlausibleObject(b) && ObjectPositionKey(b, k, (int)sizeof(k), 0, 3, 1) != 0)
    {
        const char* one[1];
        one[0] = k;
        if (coopbed::PickPieceKey(w.pw.bedKey, one, 1, 0, 0) == 0) return b;
    }
    w.layoutHandSet = false;
    w.layoutFinal = false;
    return 0;
}

// BED1 (T263/T264 poseBedUnresolved): THE BED THROUGH ITS OUTER BUILDING. A bed an interior layout made is not on the
// zone list ItBoxResolve walks; the building that layout belongs to is. So: resolve the owner's outer key, read that
// building's interior piece hands (items.cpp InteriorPieceIds - POD), resolve each through the engine's handle table,
// build its key with the same builder the owner used, and let coopbed::PickPieceKey choose. MAIN THREAD (PoseApplyTick).
// Bounded (kPoseLayoutCap pieces), and not repeated per frame: the caller runs at most once per kPoseBedRetryMs per want,
// a match is cached as a hand, and a populated interior that matched nothing is final for this want. Only a missing
// outer building (or an interior that has no pieces yet) is retried - it may simply not be loaded here yet (principle 4).
void* PoseBedViaLayout(unsigned int uid, PoseWant& w)
{
    if (w.pw.outerKey[0] == 0) { PoseLogBedMiss(uid, w, 3, 0, 0, -1, 0, 0, -1); return 0; }
    void* outer = ObjectByPositionKey(w.pw.outerKey);
    if (outer == 0)
    {
        if (!w.outerMissCounted) { w.outerMissCounted = true; ++g_poseBedOuterMissing; }
        PoseLogBedMiss(uid, w, 1, 0, 0, -1, 0, 0, -1);
        return 0;
    }
    /* BED1-b (review M2): a final miss belongs to ONE loaded instance of the outer building. The same key resolving to
       a building with another handle means it was unloaded and loaded again - a fresh interior, searched again. The
       outer key is resolved on every try for this (the box cache answers it; at most once per kPoseBedRetryMs). */
    {
        unsigned int oh[5];
        if (ObjectHandIds(outer, oh) != 0)
        {
            if (w.outerHandSet && std::memcmp(w.outerHand, oh, sizeof(oh)) != 0)
            { w.layoutFinal = false; w.layoutHandSet = false; }
            std::memcpy(w.outerHand, oh, sizeof(oh));
            w.outerHandSet = true;
        }
    }
    if (w.layoutFinal) return 0;
    static unsigned int ids[kPoseLayoutCap * 5];
    static char keys[kPoseLayoutCap][coopstate::kPoseStrMax + 1];
    static const char* kp[kPoseLayoutCap];
    static void* objs[kPoseLayoutCap];
    static int from[kPoseLayoutCap];
    int listed = -1, truncated = 0, torn = 0;
    const int n = InteriorPieceIds(outer, ids, kPoseLayoutCap, &listed, &truncated, &torn);
    if (torn != 0)   /* BED1-b (review M1): not a miss - the walk is retried on the next bed tick, never final */
    {
        PoseLogBedMiss(uid, w, 5, 0, 0, listed, truncated, 0, -1);
        return 0;
    }
    int m = 0, typed = 0;
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = ids + (size_t)i * 5;
        if (f[0] != 0) continue;                  // BUILDING (0) only - 0x54A220 keeps the same test
        ++typed;
        if (f[3] == 0 && f[4] == 0) continue;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = (void*)h.asBuilding();
        if (b == 0 || !PlausibleObject(b)) continue;
        if (ObjectPositionKey(b, keys[m], (int)sizeof(keys[m]), 0, 3, 1) == 0) continue;
        kp[m] = keys[m]; objs[m] = b; from[m] = i; ++m;
    }
    int nearest = -1;
    long nearestTenths = -1;
    const int pick = coopbed::PickPieceKey(w.pw.bedKey, kp, m, &nearest, &nearestTenths);
    if (pick >= 0)
    {
        std::memcpy(w.layoutHand, ids + (size_t)from[pick] * 5, sizeof(w.layoutHand));
        w.layoutHandSet = true;
        if (!w.viaCounted) { w.viaCounted = true; ++g_poseBedViaLayout; }
        return objs[pick];
    }
    if (!w.layoutMissCounted) { w.layoutMissCounted = true; ++g_poseBedLayoutMiss; }
    /* BED1-b (review M2): final only for a COMPLETE walk - not cut at kPoseLayoutCap, every piece the set lists read,
       and every building piece resolved and keyed. Anything less is retried on the bed tick. */
    const bool complete = truncated == 0 && listed >= 0 && n == listed && m == typed;
    if (m > 0 && complete) w.layoutFinal = true;
    PoseLogBedMiss(uid, w, pick == coopbed::kPickAmbiguous ? 4 : 2, n, m, listed, truncated,
                   nearest >= 0 ? kp[nearest] : 0, nearestTenths);
    return 0;
}

// arrest2: a copy whose owner is in a cage goes into this game's copy of that cage. A copy already caged here (this game's
// own guard put it there, or an earlier apply) is only held. The cage is found as a bed is (cached hand, zone list, layout).
long long g_poseAppliedCage = 0;     // copy: setPrisonMode(copy, 1, cage) called
long long g_poseCageHeld = 0;        // copy: already caged here when its owner's cage arrived (a guard here caged it)

// review-arrest3 H1: a copy caged only on its owner's word has no sentence from this game (none was started here, or an old one
// is over), so a jailer here would free it at once (findPrisonerFreeToGo: now > bm+0x98 + bm+0xA0). Its hours become
// kCopyHoldSentenceHours - only the owner's pose (the arrest's own game) takes it out. 1 written, -1 faulted.
int PrisonHoldSentencePod(::Character* c)
{
    __try { *(float*)((char*)c + kCharSentenceHoursOff) = kCopyHoldSentenceHours; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// review-arrest3 M2: did a guard here just release this copy? Task_ReleasePrisoner 0x357040 always writes the pardon (+0x138 =
// the releaser's faction, +0x140 = now + 1.0000001639); any other way out of the cage (a player carried it out, paid its bounty,
// a respawn outside) leaves no fresh one. 1 = fresh (*facOut = the releaser's faction), 0 = not, -1 unreadable.
int PrisonFreshPardonPod(::Character* c, double nowHours, void** facOut)
{
    *facOut = 0;
    if (nowHours < 0.0) return -1;
    __try
    {
        void* fac = *(void**)((char*)c + kCharPardonFactionOff);
        const double until = *(const double*)((const char*)c + kCharPardonUntilOff);
        if (fac == 0 || !(until > nowHours + 0.5 && until < nowHours + kPardonHours + 0.5)) return 0;
        *facOut = fac;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void PoseApplyCage(unsigned int uid, ::Character* c, PoseWant& w, DWORD now)
{
    const int ins = PoseInSomethingPod(c);
    if (ins == 2)
    {
        g_poseCaged[uid] = true;   // review-arrest2 HIGH: caged on the owner's word - never reported as a guard's caging
        g_outNoPardon.erase(uid);   // review-jail2 Q5c: back in - the next exit gets its own second look
        { GuardCagedMap::iterator ga = g_guardCaged.find(uid); if (ga != g_guardCaged.end()) GuardArrestRefresh(ga->second, c); }
        if (w.applied != 3)
        {
            w.applied = 3;
            std::map<unsigned int, bool>::iterator q = g_cageQueued.find(uid);
            const bool ours = q != g_cageQueued.end();
            if (ours) g_cageQueued.erase(q);
            if (!w.counted) { w.counted = true; if (ours) ++g_poseAppliedCage; else ++g_poseCageHeld; PoseLogApply(uid, w, 3); }
            if (g_guardCaged.find(uid) == g_guardCaged.end())   // H1: not this game's arrest - its clock must not free it
            {
                const int hs = PrisonHoldSentencePod(c);   // again here: a copy caged without our queue (held) is covered too
                if (!ours) { if (hs == 1) ++g_copyHoldSentence; else ++g_copyHoldSentenceFailed; }   // ours: counted at the safe point
            }
        }
        w.held = true;
        return;
    }
    // arrest3: caged here on the owner's word and now out, with no cage-out of ours queued. Told to the owner as a release only
    // when (review-arrest3) this game's guard made the arrest (H1 - the arrest's own game decides, user decision 2026-09-25),
    // this want saw it caged (M1 - not a stale mark from an earlier copy or world) and a guard here just wrote the pardon (M2 -
    // not a player carrying it out, a paid bounty or a respawn). Anything else is put back in the cage - as this game's own arrest
    // when a guard here lifted it out (jail2), otherwise on the owner's word.
    if (g_poseCaged.find(uid) != g_poseCaged.end())
    {
        const bool sawCaged = (w.applied == 3);
        const bool oursOut = g_cageOutQueued.find(uid) != g_cageOutQueued.end();
        const bool guardArrest = g_guardCaged.find(uid) != g_guardCaged.end();
        void* relFac = 0;
        const bool fresh = !oursOut && sawCaged && guardArrest && PrisonFreshPardonPod(c, LocalWorldHours(), &relFac) == 1;
        // recheck-arrest3 2: Task_ReleasePrisoner takes the prisoner out BEFORE it writes the pardon (on the AI worker) - a
        // guard's release with no pardon yet is looked at once more on the next tick; still none, it is kept as the arrest
        // below (jail2) and put back with its sentence - over already, so the jailer frees it again.
        if (!oursOut && !fresh && sawCaged && guardArrest && ++g_outNoPardon[uid] < 2) return;
        // P42: a rescue - the cage stood open here and in its owner's word, and no jailer here is releasing it (no arrest of this
        // game, or the pardon's second look above has run out): told to the owner as a player's release, not put back
        if (RestraintCageRescue(uid, sawCaged, oursOut, fresh, guardArrest, guardArrest && g_outNoPardon[uid] >= 2))
        {
            g_poseCaged.erase(uid); g_cageOutQueued.erase(uid); g_outNoPardon.erase(uid); g_guardCaged.erase(uid);
            w.applied = 0; w.held = false; w.counted = false;
            PrisonNoteRescuedHere(uid, now);
            return;
        }
        // jail2: a guard here lifting this game's own prisoner out (a second guard still chasing him, T310) does not end the
        // arrest - the mark stays, so the put-back keeps the jailer's sentence and the release is still told to the owner.
        bool keepArrest = guardArrest && sawCaged && !oursOut && !fresh;
        if (keepArrest)
        {
            GuardArrest& ga = g_guardCaged[uid];
            if (++ga.keptOuts > kGuardKeptOutsMax) { keepArrest = false; ++g_guardKeptOutsCapped; }
        }
        g_poseCaged.erase(uid); g_cageOutQueued.erase(uid); g_outNoPardon.erase(uid);
        if (!keepArrest) g_guardCaged.erase(uid);
        w.applied = 0; w.held = false; w.counted = false;
        if (oursOut) return;
        if (fresh) { PrisonNoteReleasedHere(uid, now, relFac); return; }
        if (keepArrest) ++g_guardArrestKept; else ++g_copyLeftNotReleased;
        w.bedAt = 0;   // back in at once
    }
    std::map<unsigned int, DWORD>::iterator hold = g_releaseHold.find(uid);
    if (hold != g_releaseHold.end())
    {
        if (now - hold->second < kReleaseHoldMs) return;
        g_releaseHold.erase(hold);   // the owner never answered: its word (still caged) stands again
    }
    if (ins == 1)   // in a bed here: out of it first, the cage on a later try
    {
        const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
        if (PoseBedPod((SetBedModeFn)(base + kSetBedModeRva), c, false, 0) < 0) ++g_poseBedFaulted;
        w.applied = 0;
        return;
    }
    if (w.bedAt != 0 && now - w.bedAt < kPoseBedRetryMs) return;
    w.bedAt = now | 1u;
    void* cage = PoseBedByCachedHand(w);
    if (cage == 0) cage = ObjectByPositionKey(w.pw.bedKey);
    if (cage == 0) cage = PoseBedViaLayout(uid, w);
    if (cage == 0)
    {
        w.bedMiss = true;
        if (!w.bedMissCounted) { w.bedMissCounted = true; ++g_poseBedUnresolved; }
        return;
    }
    w.bedMiss = false;
    g_cageQueued[uid] = true;
    PrisonQueueOp(uid, true, cage, false);   // made at the safe point; the next tick sees +0x2F8 == 2 and holds it
}

}   // namespace (POSE private)

bool PoseOfOwned(unsigned int uid, coopstate::PoseWire* out)
{
    if (out == 0) return false;
    coopstate::PoseClear(out);
    std::map<unsigned int, PoseOwn>::const_iterator it = g_poseOwn.find(uid);
    if (it == g_poseOwn.end() || it->second.pw.kind == coopstate::kPoseNone) return false;
    *out = it->second.pw;
    return true;
}

void PoseWatchTick()
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        std::map<unsigned int, PoseOwn>::iterator it = g_poseOwn.find(uid);
        if (!net::IsUidMine(uid)) { if (it != g_poseOwn.end()) g_poseOwn.erase(it); continue; }
        if (!PlausibleObject(c)) continue;
        int ins = 0, actLong = 0, stumble = 0, looped = 0;
        float speed = 0.0f;
        unsigned char hd[kHandIdBytes];
        char act[coopstate::kPoseStrMax + 1];
        if (!PoseReadPod(c, &ins, hd, act, &actLong, &stumble, &looped, &speed)) continue;

        coopstate::PoseWire pw;
        coopstate::PoseClear(&pw);
        const bool excluded = act[0] != 0 && (stumble != 0 || PoseActionExcluded(act));
        // POSE-b (review-pose item 1): a pose is a LOOPED action of an owner standing still; a one-off is not sent (v1).
        // The bed below is as before (its fallback action follows the same rule).
        const bool oneOff = act[0] != 0 && !excluded && looped == 0;
        const bool moving = act[0] != 0 && !excluded && looped != 0 && !(speed < kPoseOwnerStillSpeed);   // NaN = moving
        const bool actOk = act[0] != 0 && actLong == 0 && !excluded && !oneOff && !moving;
        bool keyTried = false;
        if (ins == 1 || ins == 2)   // a bed, or (arrest2) a cage; the key is built once per hand
        {
            keyTried = true;
            if (it != g_poseOwn.end() && it->second.keyTried && std::memcmp(it->second.hand, hd, kHandIdBytes) == 0)
            {
                std::memcpy(pw.bedKey, it->second.pw.bedKey, sizeof(pw.bedKey));
                std::memcpy(pw.outerKey, it->second.pw.outerKey, sizeof(pw.outerKey));   /* BED1 */
            }
            else if (PoseBedKeyFromHand(hd, pw.bedKey, (int)sizeof(pw.bedKey), pw.outerKey, (int)sizeof(pw.outerKey)) == 0)
            { pw.bedKey[0] = 0; pw.outerKey[0] = 0; }
            if (pw.bedKey[0] != 0) pw.kind = (ins == 2) ? coopstate::kPoseCage : coopstate::kPoseBed;
        }
        if (ins != 2 && actOk)   // arrest2: a caged character sends its cage (kPoseCage above), never an action
        {
            std::memcpy(pw.action, act, sizeof(pw.action));
            if (pw.kind == coopstate::kPoseNone) pw.kind = coopstate::kPoseAction;
        }
        if (pw.kind == coopstate::kPoseNone) { pw.bedKey[0] = 0; pw.outerKey[0] = 0; }   /* a key only travels with a bed (BED1: both keys) */

        coopstate::PoseWire was;
        coopstate::PoseClear(&was);
        if (it != g_poseOwn.end()) was = it->second.pw;
        if (coopstate::PoseEqual(pw, was))
        {
            if (pw.kind != coopstate::kPoseNone && it != g_poseOwn.end() && (DWORD)(::GetTickCount() - it->second.pushedAt) >= kPoseRepushMs)
            {
                it->second.pushedAt = ::GetTickCount();
                if (StatePush(uid)) ++g_poseRepushed;
            }
            // nothing to send; remember a bed hand whose key was just tried, so it is not rebuilt every frame
            if (keyTried && (it == g_poseOwn.end() || !it->second.keyTried || std::memcmp(it->second.hand, hd, kHandIdBytes) != 0))
            {
                PoseOwn o;
                o.pw = pw;
                std::memcpy(o.hand, hd, kHandIdBytes);
                o.keyTried = true;
                o.pushedAt = (it != g_poseOwn.end()) ? it->second.pushedAt : ::GetTickCount();
                g_poseOwn[uid] = o;
            }
            continue;
        }
        if (excluded) ++g_poseExcludedSeen;
        else if (oneOff) ++g_poseOneOffSeen;
        else if (moving) ++g_poseOwnerMovingSeen;
        if (pw.kind == coopstate::kPoseNone && !keyTried) { if (it != g_poseOwn.end()) g_poseOwn.erase(it); }
        else
        {
            PoseOwn o;
            o.pw = pw;
            std::memcpy(o.hand, hd, kHandIdBytes);
            o.keyTried = keyTried;
            o.pushedAt = ::GetTickCount();
            g_poseOwn[uid] = o;
        }
        if (StatePush(uid)) ++g_poseSent;   // like prone/dead (F140): at once, not on the round-robin
    }
}

void NotePoseWant(unsigned int uid, const coopstate::PoseWire& pose, unsigned int fromPeer)
{
    std::map<unsigned int, PoseWant>::iterator it = g_poseWant.find(uid);
    if (it == g_poseWant.end())
    {
        if (pose.kind == coopstate::kPoseNone) return;   // nothing applied, nothing to undo
        PoseWant w;
        w.pw = pose; w.action = pose.action; w.peer = fromPeer; w.applied = 0; w.held = false; w.counted = false;
        w.bedMiss = false; w.bedMissCounted = false; w.refusedNow = false; w.bedAt = 0;
        w.approachCounted = false; w.confirmedAt = ::GetTickCount();
        PoseWantResetBed(w);   /* BED1 */
        g_poseWant[uid] = w;
        return;
    }
    PoseWant& w = it->second;
    w.peer = fromPeer;
    // POSE-b item 6: every STATE re-confirms - the owner's periodic STATE re-sends its pose each cycle (~5 s), so a live
    // pose never expires. STATE carries no sequence or send-time field, so an older STATE cannot be told apart.
    w.confirmedAt = ::GetTickCount();
    if (coopstate::PoseEqual(w.pw, pose)) return;
    w.pw = pose; w.action = pose.action; w.counted = false; w.approachCounted = false;
    w.bedMiss = false; w.bedMissCounted = false; w.refusedNow = false; w.bedAt = 0;
    PoseWantResetBed(w);   /* BED1: a changed want (another bed, another outer building) walks afresh */
}

void PoseApplyTick()
{
    if (g_poseWant.empty()) return;
    if (EngineWritesBlocked()) return;
    if (kSetBedModeRva == 0 || kPlayActionByNameRva == 0 || kStopActionRva == 0) return;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const SetBedModeFn setBed = (SetBedModeFn)(base + kSetBedModeRva);
    const PlayActionByNameFn play = (PlayActionByNameFn)(base + kPlayActionByNameRva);
    const StopActionFn stop = (StopActionFn)(base + kStopActionRva);
    const DWORD now = ::GetTickCount();

    std::map<unsigned int, PoseWant>::iterator it = g_poseWant.begin();
    while (it != g_poseWant.end())
    {
        const unsigned int uid = it->first;
        PoseWant& w = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c)) { PrisonForgetCopy(uid); g_poseWant.erase(it++); continue; }
        // The wish was authorised when its STATE arrived; a handoff since may have made us the owner - then the
        // engine runs the character and nothing here touches it.
        // POSE-b item 4: what we applied is undone first - from here the engine (or the new owner's STATE) runs it.
        if (net::IsUidMine(uid) || !net::RemoteMayWriteStill(uid, w.peer))
        {
            if (w.applied != 0) { PoseUndo(c, w, false); ++g_poseUndoneHandoff; }
            PrisonForgetCopy(uid);   // review-arrest3 M1
            g_poseWant.erase(it++); continue;
        }
        // POSE-b item 6: STATE is unordered and unreliable; a pose no STATE re-confirmed for 10 s is undone and dropped.
        if (now - w.confirmedAt >= kPoseConfirmMs)
        {
            if (w.applied != 0) PoseUndo(c, w, false);
            ++g_poseExpired;
            g_poseWant.erase(it++); continue;
        }

        if (w.pw.kind == coopstate::kPoseNone)
        {
            if (w.applied != 0) { PoseUndo(c, w); ++g_poseCleared; }
            g_releaseHold.erase(uid);   // arrest3
            g_poseWant.erase(it++); continue;
        }
        w.held = false;
        // only copies that stand on their own: not carried, not a ragdoll (knocked out), not dead. arrest2: a cage takes a
        // knocked-out body too (the guard's own Task_PutInSomething cages it limp), so only carried / dead wait for a cage.
        if (w.pw.kind == coopstate::kPoseCage)
        {
            if (BeingCarried(c) || c->hasDied()) { ++it; continue; }
            PoseApplyCage(uid, c, w, now);
            ++it; continue;
        }
        if (w.applied == 3)   // arrest2: the owner left its cage (the want changed) - out of the cage first
        {
            if (PoseInSomethingPod(c) == 2) PrisonQueueOp(uid, false, 0, false);   // at the safe point
            w.applied = 0;
            g_releaseHold.erase(uid);   // arrest3: the owner's cage ended - no hold any more
        }
        // A bed takes a knocked-out body too: Task_PutInSomething's bed branch puts the limp body in (setBedMode, then the
        // spot and a teleport), as its cage branch cages it. So a ragdoll waits only for the other poses, never for a bed.
        const bool rag = RagdollActive(c);
        if (BeingCarried(c) || (rag && w.pw.kind != coopstate::kPoseBed) || c->hasDied()) { ++it; continue; }

        bool wantAction = (w.pw.kind == coopstate::kPoseAction);
        if (w.pw.kind == coopstate::kPoseBed)
        {
            if (w.applied == 1 && !w.bedMiss)   // the bed pose plays only while no action is set
            {
                if (PoseStopPod(stop, c) < 0) ++g_poseBedFaulted;
                w.applied = 0;
            }
            const int ins = PoseInSomethingPod(c);
            if (ins == 1 && w.applied == 2) w.held = true;
            else if (w.bedAt == 0 || now - w.bedAt >= kPoseBedRetryMs)
            {
                w.bedAt = now | 1u;
                // BED1: a piece a layout walk already matched is re-found by its hand; then the zone list as before; then,
                // only on a miss, the outer building's interior pieces (PoseBedViaLayout - bounded, cached per want).
                void* bed = PoseBedByCachedHand(w);
                if (bed == 0) bed = ObjectByPositionKey(w.pw.bedKey);   // re-resolved per try: a held pointer can outlive its zone
                if (bed == 0) bed = PoseBedViaLayout(uid, w);
                if (bed == 0)
                {
                    w.bedMiss = true;
                    if (!w.bedMissCounted) { w.bedMissCounted = true; ++g_poseBedUnresolved; }
                }
                else if (ins == 1 && PoseCurrentBedObject(c) == bed)   // already in that bed (a rescuer here put it there): taken as is
                {
                    w.bedMiss = false; w.applied = 2; w.held = true;
                    if (!w.counted) { w.counted = true; ++g_poseAppliedBed; PoseLogApply(uid, w, 2); }
                }
                else
                {
                    w.bedMiss = false;
                    // a limp copy goes in as the engine puts a limp body in (getDropped, setBedMode, the move, stopAction) and onto
                    // the owner's streamed spot - the bed's - so it does not lie limp on the floor beside the bed. getDropped only
                    // posts a ragdoll request that the copy's own update carries out later, so it runs alone on one try and the rest
                    // waits for a later try (kPoseBedRetryMs apart) once the request is gone and the copy is no ragdoll; after
                    // kBedDropWaitMs the copy stays out of the bed for this want.
                    const bool limp = rag || w.dropAt != 0;
                    if (limp && w.dropAt == 0)
                    {
                        if (!w.dropGaveUp)
                        {
                            if (BedGetDroppedPod(c) < 0) ++g_poseBedFaulted;
                            w.dropAt = now | 1u;
                            ++g_bedCopyDropPosted;
                        }
                    }
                    else if (limp && (BedDropPendingPod(c) != 0 || rag))
                    {
                        if (now - w.dropAt >= kBedDropWaitMs) { w.dropAt = 0; w.dropGaveUp = true; ++g_bedCopyDropTimeout; }
                    }
                    else if (PoseBedPod(setBed, c, true, bed) == 1)
                    {
                        w.applied = 2; w.held = true; w.dropAt = 0;
                        if (limp)
                        {
                            float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                            if (PuppetAuthorityPos(uid, &sx, &sy, &sz) && TeleportCharacter(c, sx, sy, sz)) ++g_bedCopyMoved; else ++g_bedCopyNotMoved;
                            if (PoseStopPod(stop, c) < 0) ++g_poseBedFaulted;
                        }
                        if (!w.counted) { w.counted = true; ++g_poseAppliedBed; PoseLogApply(uid, w, 2); }
                    }
                    else ++g_poseBedFaulted;
                }
            }
            else if (w.applied == 2) w.held = true;
            if (w.bedMiss && w.pw.action[0] != 0 && !rag) wantAction = true;   // the fallback the owner sent with its bed (not on a ragdoll)
        }
        else if (w.applied == 2)   // bed -> something else: out of the bed first
        {
            if (PoseInSomethingPod(c) == 1 && PoseBedPod(setBed, c, false, 0) < 0) ++g_poseBedFaulted;
            w.applied = 0;
        }

        // POSE-b item 2: an action pose is taken only once the copy stands within kPoseApproachDist (horizontal) of the
        // owner's streamed spot; until then it is not held, so the movement drive (push / path) walks it there. The bed's
        // own fallback action and a pose already playing are not gated.
        if (wantAction && w.pw.kind == coopstate::kPoseAction && w.applied != 1)
        {
            const float d = PuppetWindowDriftNow(uid);
            if (!(d >= 0.0f && d <= kPoseApproachDist))
            {
                if (!w.approachCounted) { w.approachCounted = true; ++g_posePendingApproach; }
                ++it; continue;
            }
        }
        if (wantAction && !w.action.empty())
        {
            // re-issued every tick while posed - the engine's own tasks do exactly that
            const int r = PosePlayPod(play, c, &w.action);
            if (r == 1)
            {
                w.refusedNow = false; w.applied = 1; w.held = true;
                if (!w.counted) { w.counted = true; ++g_poseAppliedAction; PoseLogApply(uid, w, 1); }
            }
            else if (!w.refusedNow) { w.refusedNow = true; ++g_poseActionRefused; }
        }
        ++it;
    }
}

bool PoseHoldsCopy(unsigned int uid)
{
    if (BedAskHolds(uid)) return true;   // a rescuer here put it in a bed and its owner has not answered yet
    if (g_poseWant.empty()) return false;
    std::map<unsigned int, PoseWant>::const_iterator it = g_poseWant.find(uid);
    return it != g_poseWant.end() && it->second.held;
}

void PoseNoteHeldFrame() { ++g_poseHeldFrames; }

std::string PoseReportToken()
{
    return " poseSent=" + S(g_poseSent) + " poseRepushed=" + S(g_poseRepushed) + " poseExcludedSeen=" + S(g_poseExcludedSeen)
         + " poseAppliedAction=" + S(g_poseAppliedAction) + " poseAppliedBed=" + S(g_poseAppliedBed)
         + " poseCleared=" + S(g_poseCleared) + " poseBedUnresolved=" + S(g_poseBedUnresolved)
         + " poseBedFaulted=" + S(g_poseBedFaulted) + " poseActionRefused=" + S(g_poseActionRefused)
         + " poseHeldFrames=" + S(g_poseHeldFrames) + " poseWatched=" + S((long long)g_poseWant.size())
         + " poseOneOffSeen=" + S(g_poseOneOffSeen) + " poseOwnerMovingSeen=" + S(g_poseOwnerMovingSeen)
         + " posePendingApproach=" + S(g_posePendingApproach) + " poseUndoneHandoff=" + S(g_poseUndoneHandoff)
         + " poseExpired=" + S(g_poseExpired)
         + " poseBedViaLayout=" + S(g_poseBedViaLayout) + " poseBedOuterMissing=" + S(g_poseBedOuterMissing)
         + " poseBedLayoutMiss=" + S(g_poseBedLayoutMiss)
         + " poseAppliedCage=" + S(g_poseAppliedCage) + " poseCageHeld=" + S(g_poseCageHeld);   // arrest2
}

// ---- arrest2 (docs/design-arrest.md 3 "arrest2"): a guard's game caged the other game's character ------------------------
// (a) THE GUARD'S GAME: its engine put one of its COPIES in a cage (a guard's Task_PutInSomething - nothing of ours cages a
//     copy except PoseApplyCage, whose want this checks). The game that drives that character is told once per cage
//     (MSG_PRISON kind IN, the cage's POSE keys).
// (b) THE OWNER'S GAME: a queued MSG_PRISON for a character it drives - once the body is on the ground (not carried) the cage
//     is found by its keys and the character is caged with setPrisonMode 0x3305C0 after the cage's own isFreeSlot
//     (vt+0x4F0) says there is room, and the cage is locked (DoorLock +0x20), as the guard's task does. Its POSE then
//     reports the cage (kPoseCage) and keeps the guard's game's copy in it.
// The sentence is started as the guard's task starts it (notifyStartPrisonSentence 0x853A80). NOT YET: release / escape
// (arrest3), and a copy the guard caged while its owner could not be caged stays caged (no answer goes back).
namespace {

const DWORD kPrisonGiveUpMs   = 15000;   // an IN that cannot be applied within this is dropped (counted)
const DWORD kPrisonRetryMs    = 1000;
const size_t kUseableIsFreeSlotSlot = 0x4F0 / 8;   // UseableStuff vt+0x4F0 (save1 answer 2; prototype per the arrest2 read)
const size_t kUseableDoorLockOff    = 0x438;       // UseableStuff -> DoorLock* (access1 answer 7)
const size_t kDoorLockLockedOff     = 0x20;        // DoorLock : locked byte
const long long kPrisonLogLimit = 16;

typedef bool (*UseableIsFreeSlotFn)(void* useable, const void* whoHand);   // arrest2 read 2: the character's hand (+0x58)
typedef void* (*GetBountyFactionFn)(void* bountyManager, void* enforcer);      // 0x851140 (crime4 answers)
typedef void  (*NotifyStartPrisonSentenceFn)(void* bountyManager, void* bountyFaction);   // 0x853A80 (arrest2 read 3)
typedef void* (*GetFactionVtFn)(void* obj);                                    // RootObjectBase vt+0x58
const size_t kCharHandOff         = 0x58;    // Character : its own hand
const size_t kCharBountyMgrOff    = 0xF0;    // Character : BountyManager
/* P11 f3 (review M6): the ARRESTING game's own sentence test. Task_PutInSomething 0x358610 reads GetActualBounty 0x8531B0(victim
   +0xF0, the cage's faction) on the game whose guard ran it (decomp_358610.txt:147-148 - Read); MSG_PRISON kind IN carries that
   verdict and the owner follows it (PrisonSentencePod). */
typedef int (*P11BountyFn)(void* bountyManager, void* faction);
static unsigned long long kP11BountyRva = 0; static coop::AddrReg kP11BountyRva_reg("GetActualBounty", &kP11BountyRva);   /* Steam_1.0.65 0x8531B0 (P11 f3: moved up) */
void* P11CageFromPoseHand(const unsigned char* id)   /* the object the pose hand names (the cage), 0 none - as PoseBedKeyFromHand */
{
    unsigned int f[5];
    std::memcpy(f, id, sizeof(f));
    if (f[3] == 0 && f[4] == 0) return 0;
    hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
    void* bld = (void*)h.asBuilding();
    return (bld != 0 && PlausibleObject(bld)) ? bld : 0;
}
int P11ArrestSentencePod(const void* c, void* cage)   /* kPrisonSentenceYes / No; Unknown without the row or a cage, or on a fault */
{
    if (kP11BountyRva == 0 || c == 0 || cage == 0) return cooprison::kPrisonSentenceUnknown;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    __try
    {
        void** vt = *(void***)cage;
        void* fac = ((GetFactionVtFn)vt[0x58 / 8])(cage);
        void* bm = (void*)((const char*)c + kCharBountyMgrOff);
        return (((P11BountyFn)(base + (uintptr_t)kP11BountyRva))(bm, fac) > 0) ? cooprison::kPrisonSentenceYes : cooprison::kPrisonSentenceNo;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return cooprison::kPrisonSentenceUnknown; }
}

struct PrisonIn { cooprison::PrisonMsg m; unsigned int peer; DWORD firstAt; DWORD triedAt; };
std::map<unsigned int, PrisonIn> g_prisonIn;              // own uid -> the cage the other game's guard put its copy in
std::map<unsigned int, std::string> g_prisonReported;     // copy uid -> the cage key already reported
struct BedIn { cooprison::PrisonMsg m; unsigned int peer; DWORD firstAt; DWORD triedAt; };
std::map<unsigned int, BedIn> g_bedIn;                    // own uid -> the bed another game's rescuer put its copy in
int g_prisonScan = 0;

long long g_prisonSent = 0;          // guard's game: a copy caged by this engine, reported
long long g_prisonSendNoKey = 0;     // guard's game: caged, but the cage's key could not be built
long long g_prisonRecv = 0;          // owner: MSG_PRISON received for a character this game drives
long long g_prisonNotMine = 0;       // owner: ... for a character this game does not drive (refused)
long long g_prisonDropped = 0;       // malformed / during a load
long long g_prisonCaged = 0;         // owner: setPrisonMode(own, 1, cage) left it in the cage
long long g_prisonAlready = 0;       // owner: already in a cage here
long long g_prisonNoCage = 0;        // owner: the keys named no cage here within kPrisonGiveUpMs
long long g_prisonNoSlot = 0;        // owner: the cage's isFreeSlot said no
long long g_prisonStillCarried = 0;  // owner: still carried here after kPrisonGiveUpMs
long long g_prisonFailed = 0;        // owner: setPrisonMode faulted / did not take / dead / in a bed
long long g_prisonLocked = 0;        // owner: the cage's lock set
long long g_prisonLogged = 0;
long long g_prisonSentenced = 0;       // owner: notifyStartPrisonSentence called after the caging
long long g_prisonReleaseRecv = 0;     // arrest3 owner: the guard's game released its copy
long long g_prisonReleased = 0;        // arrest3 owner: our own character released (bounty cleared, pardon, unlocked, out)
long long g_prisonReleaseNotCaged = 0; // arrest3 owner: a RELEASE for a character not in a cage here
long long g_prisonReleaseFailed = 0;   // arrest3 owner: no cage from its hand / faulted / still in the cage
long long g_prisonRefusedSent = 0;     // arrest3 owner: told the guard's game we could not cage our character
long long g_prisonRefusedRecv = 0;     // arrest3 guard's game: the owner could not cage it
long long g_prisonRefusedApplied = 0;  // arrest3 guard's game: our copy taken out of its cage on that word
std::map<unsigned int, std::string> g_prisonReleaseIn;   // own uid -> a RELEASE to apply (the releaser's faction as sent)
unsigned long long kClearBountyRva = 0; static coop::AddrReg kClearBountyRva_reg("ClearBounty", &kClearBountyRva);   /* Steam_1.0.65 0x852B20 - void clearBounty(BountyManager*, Faction*) */
typedef void (*ClearBountyFn)(void* bountyManager, void* faction);
// review-arrest3 M4 (arrest3 answer 1): the prisoner's GET_OUT_OF_CAGE_LEGIT order, as Task_ReleasePrisoner issues it -
// issueOrder(P, 0, 0x85, 0, 0, 1, *(qword*)0x142246C68) (Confirmed 0x3572FB-0x357313).
unsigned long long kAddOrderRva = 0; static coop::AddrReg kAddOrderRva_reg("AddOrder", &kAddOrderRva);   /* Steam_1.0.65 0x5D1640 */
unsigned long long kAddOrderArgGlobalRva = 0; static coop::AddrReg kAddOrderArgGlobalRva_reg("AddOrderArgGlobal", &kAddOrderArgGlobalRva);   /* Steam_1.0.65 0x2246C68 - the qword passed as issueOrder's 7th argument */
typedef void (*AddOrderFn)(void* who, void* a2, int taskType, void* target, char playerOrder, char a6, unsigned long long a7);
const int kTaskGetOutOfCageLegit = 0x85;
long long g_releaseFacUnknown = 0;    // a RELEASE named a faction this game does not have - the cage's faction used
long long g_releaseOrdered = 0;        // the walk-out order issued on our released character
long long g_releaseOrderFailed = 0;    // no address / faulted
long long g_refusedPardon = 0;         // review-arrest3 M3: a copy taken out after REFUSED got the pardon here
long long g_ownSentenceMargin = 0;     // own character: the sentence given kOwnSentenceMarginHours more (the arrest's game frees first)
long long g_prisonSentenceFailed = 0;  // owner: no address / faulted
unsigned long long kNotifyStartPrisonSentenceRva = 0; static coop::AddrReg kNotifyStartPrisonSentenceRva_reg("NotifyStartPrisonSentence", &kNotifyStartPrisonSentenceRva);   /* Steam_1.0.65 0x853A80 */
unsigned long long kGetBountyFactionPrRva = 0; static coop::AddrReg kGetBountyFactionPrRva_reg("GetBountyFaction", &kGetBountyFactionPrRva);   /* Steam_1.0.65 0x851140 */

int PrisonFreeSlotPod(void* cage, ::Character* who)   // 1 room, 0 none, -1 unreadable
{
    __try
    {
        void** vt = *(void***)cage;
        const UseableIsFreeSlotFn fn = (UseableIsFreeSlotFn)vt[kUseableIsFreeSlotSlot];
        return fn(cage, (const char*)who + kCharHandOff) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// review-arrest2 MEDIUM: a refused tryOperate leaves +0x2F8 == 2 with no cage hand (answer 1) - 1 = the hand at +0x300 names
// something (index or serial set), 0 = empty, -1 unreadable.
int PrisonHandSetPod(const void* c)
{
    __try
    {
        const unsigned int* f = (const unsigned int*)((const char*)c + kPoseBedHandIdOff);
        return (f[3] != 0 || f[4] != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int PrisonLockSetPod(void* cage, unsigned char locked)   // 1 written, 0 the cage has no lock, -1 unreadable
{
    __try
    {
        unsigned char* lock = *(unsigned char**)((char*)cage + kUseableDoorLockOff);
        if (lock == 0) return 0;
        lock[kDoorLockLockedOff] = locked;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PrisonLockPod(void* cage) { return PrisonLockSetPod(cage, 1); }

void PrisonLog(const std::string& line)
{
    if (g_prisonLogged >= kPrisonLogLimit) return;
    ++g_prisonLogged;
    DebugLog("[ARREST] " + line);
}

// Rescuer's side, every 10th frame per copy: a copy this game's engine put in a bed straight after carrying it is reported to
// its owner once per bed; the owner's bed pose settles it; a refusal or 20 s of silence takes it out again.
void BedTakeOut(unsigned int uid, ::Character* c, SetBedModeFn setBed, const char* why)
{
    if (PoseBedPod(setBed, c, false, 0) < 0) ++g_poseBedFaulted;
    PrisonLog("BED uid=" + S((long long)uid) + " copy taken out of the bed here (" + std::string(why) + ")");
}
void BedWatchCopy(unsigned int uid, ::Character* c, int ins, const unsigned char* hd, DWORD now, SetBedModeFn setBed)
{
    std::map<unsigned int, PoseWant>::const_iterator pw = g_poseWant.find(uid);
    std::map<unsigned int, BedAsk>::iterator ask = g_bedAsk.find(uid);
    if (ins != 1)
    {
        if (ask != g_bedAsk.end())
        {
            if (ask->second.state == kBedAskPending || ask->second.state == kBedAskRefusedIn) ++g_bedLeftUnexplained;
            g_bedAsk.erase(ask);
        }
        // the carry half of the put-down: carried by a character this game drives, or by one not named here (a carry by another
        // game's character is that game's to report)
        if (BeingCarried(c))
        {
            unsigned int by = 0;
            LocalCarrierOf(c, &by);
            if (by != 0 && !net::IsUidMine(by)) g_bedCarriedAt.erase(uid); else g_bedCarriedAt[uid] = now | 1u;
        }
        else
        {
            std::map<unsigned int, DWORD>::iterator ca = g_bedCarriedAt.find(uid);
            if (ca != g_bedCarriedAt.end() && now - ca->second > kBedCarryEdgeMs) g_bedCarriedAt.erase(ca);
        }
        return;
    }
    // the owner's pose says bed: the copy lies there on the owner's word (the bed analogue of cageWant)
    const bool ownerBed = pw != g_poseWant.end() && pw->second.pw.kind == coopstate::kPoseBed;
    char key[coopstate::kPoseStrMax + 1], outer[coopstate::kPoseStrMax + 1];
    if (PoseBedKeyFromHand(hd, key, (int)sizeof(key), outer, (int)sizeof(outer)) == 0) return;
    if (ask != g_bedAsk.end() && ask->second.key == key)
    {
        BedAsk& a = ask->second;
        if (a.state == kBedAskSettled || a.state == kBedAskOut) return;
        if (ownerBed) { a.state = kBedAskSettled; PrisonLog("BED uid=" + S((long long)uid) + " the owner's pose says bed - settled"); return; }
        if (a.state == kBedAskRefusedIn) { a.state = kBedAskOut; BedTakeOut(uid, c, setBed, "the owner refused"); return; }
        if (now - a.sentAt >= kBedNoAnswerMs)   // not remembered: the next put-down in this bed asks again
        {
            a.state = kBedAskOut; ++g_bedNoAnswer;
            BedTakeOut(uid, c, setBed, "no answer from the owner");
        }
        return;
    }
    if (ownerBed) { g_bedCarriedAt.erase(uid); return; }
    std::map<unsigned int, DWORD>::iterator ca = g_bedCarriedAt.find(uid);
    if (ca == g_bedCarriedAt.end()) return;   // in a bed without a carry just before: not a rescuer's put-down here
    if (now - ca->second > kBedCarryEdgeMs) { g_bedCarriedAt.erase(ca); return; }
    std::map<unsigned int, std::string>::const_iterator rk = g_bedRefusedKey.find(uid);
    if (rk != g_bedRefusedKey.end() && rk->second == key)   // the owner already said no to this bed for good: out again, not asked
    {
        g_bedCarriedAt.erase(ca);
        BedTakeOut(uid, c, setBed, "a bed its owner refused before");
        return;
    }
    std::map<unsigned int, BedTempRefusal>::iterator tr = g_bedTempRefused.find(uid);
    if (tr != g_bedTempRefused.end() && tr->second.coolAt != 0 && tr->second.key == key)
    {
        if (now - tr->second.coolAt < kBedCooldownMs)   // refused for now too often: out again, not asked until the cooldown ends
        {
            g_bedCarriedAt.erase(ca);
            ++g_bedCooldownTakeOut;
            BedTakeOut(uid, c, setBed, "its owner refused this bed for now again and again - not asked while the cooldown runs");
            return;
        }
        g_bedTempRefused.erase(tr);
    }
    Ogre::Vector3 at;
    if (!SafeReadPosition(c, &at)) return;   // tried again at the next scan while the put-down is recent
    cooprison::PrisonMsg m;
    m.uid = uid; m.kind = cooprison::kPrisonBedIn; m.cageKey = key; m.outerKey = outer;
    m.pos[0] = at.x; m.pos[1] = at.y; m.pos[2] = at.z;
    if (!net::SendPrison(m)) return;   // the link is down: tried again at the next scan
    g_bedCarriedAt.erase(uid);
    BedAsk a; a.key = key; a.sentAt = now | 1u; a.state = kBedAskPending;
    g_bedAsk[uid] = a;
    ++g_bedSent;
    PrisonLog("-> BED uid=" + S((long long)uid) + " put in a bed here after a carry, bed='" + std::string(key) + "' outer='"
              + std::string(outer) + "' - its own game is asked to put it in that bed");
}

// Rescuer: a temporary BED REFUSED for an ask of ours - counted per character and bed; the kBedTempRefusalTries-th starts the
// cooldown BedWatchCopy honours.
void BedNoteTempRefusal(unsigned int uid, const std::string& key)
{
    const DWORD now = ::GetTickCount();
    std::map<unsigned int, BedTempRefusal>::iterator it = g_bedTempRefused.find(uid);
    if (it == g_bedTempRefused.end())
    {
        BedTempRefusal t; t.key = key; t.n = 0; t.lastAt = 0; t.coolAt = 0;
        it = g_bedTempRefused.insert(std::make_pair(uid, t)).first;
    }
    BedTempRefusal& t = it->second;
    if (t.key != key || t.lastAt == 0 || now - t.lastAt >= kBedCooldownMs) { t.key = key; t.n = 0; t.coolAt = 0; }
    t.lastAt = now | 1u;
    if (++t.n < kBedTempRefusalTries) return;
    t.n = 0; t.coolAt = now | 1u;
    ++g_bedCooldownStarted;
    PrisonLog("BED uid=" + S((long long)uid) + " its owner refused bed '" + key + "' for now " + S((long long)kBedTempRefusalTries)
              + " times - put-downs there are taken out without asking for " + S((long long)(kBedCooldownMs / 1000)) + " s");
}

// (a) Every 10th frame: copies this engine caged.
void PrisonWatchCopies()
{
    if (++g_prisonScan < 10) return;
    g_prisonScan = 0;
    // recheck-arrest3 1: a "this game's guard caged it" mark whose copy is gone or became ours is forgotten here too (not only
    // for copies with a pose want) - a respawned copy must not inherit it and skip its never-ending sentence.
    for (GuardCagedMap::iterator g = g_guardCaged.begin(); g != g_guardCaged.end(); )
    {
        ::Character* gc = FindSpawned(g->first);
        if (gc == 0 || !PlausibleObject(gc) || net::IsUidMine(g->first)) g_guardCaged.erase(g++); else ++g;
    }
    const SetBedModeFn bedSet = (kSetBedModeRva != 0) ? (SetBedModeFn)((uintptr_t)::GetModuleHandleA(0) + kSetBedModeRva) : 0;
    const DWORD bedNow = ::GetTickCount();
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || uid == 0 || net::IsUidMine(uid) || !PlausibleObject(c)) continue;
        int ins = 0, actLong = 0, stumble = 0, looped = 0;
        float speed = 0.0f;
        unsigned char hd[kHandIdBytes];
        char act[coopstate::kPoseStrMax + 1];
        if (!PoseReadPod(c, &ins, hd, act, &actLong, &stumble, &looped, &speed)) continue;
        if (bedSet != 0) BedWatchCopy(uid, c, ins, hd, bedNow, bedSet);   // a rescuer's bed put-down here (the bed hand-off)
        std::map<unsigned int, std::string>::iterator rep = g_prisonReported.find(uid);
        std::map<unsigned int, PoseWant>::const_iterator pw = g_poseWant.find(uid);
        const bool cageWant = pw != g_poseWant.end() && pw->second.pw.kind == coopstate::kPoseCage;
        if (ins != 2)
        {
            if (rep != g_prisonReported.end()) g_prisonReported.erase(rep);
            if (!cageWant) { g_poseCaged.erase(uid); g_cageOutQueued.erase(uid); g_guardCaged.erase(uid); }   // arrest3: PoseApplyCage reads a release
            continue;
        }
        // caged on the owner's word (its POSE says, or said, cage)? then nothing to report (review-arrest2 HIGH)
        if (cageWant) { g_poseCaged[uid] = true; continue; }
        if (g_poseCaged.find(uid) != g_poseCaged.end()) continue;
        char key[coopstate::kPoseStrMax + 1], outer[coopstate::kPoseStrMax + 1];
        if (PoseBedKeyFromHand(hd, key, (int)sizeof(key), outer, (int)sizeof(outer)) == 0)
        {
            if (rep == g_prisonReported.end()) { g_prisonReported[uid] = std::string(); ++g_prisonSendNoKey; }
            continue;
        }
        if (rep != g_prisonReported.end() && rep->second == key) continue;
        cooprison::PrisonMsg m;
        m.uid = uid; m.kind = cooprison::kPrisonIn; m.cageKey = key; m.outerKey = outer;
        m.sentence = (unsigned char)P11ArrestSentencePod(c, P11CageFromPoseHand(hd));   /* P11 f3 (M6): our engine's verdict */
        if (!net::SendPrison(m)) continue;   // the link is down: tried again at the next scan
        g_prisonReported[uid] = key;
        {
            GuardArrest a = GuardArrestReadPod(c);
            if (!GuardSentenceSane(a)) a.known = false;   // review-jail2 Q3: the sentence may not be written yet - refreshed later
            g_guardCaged[uid] = a;   // review-arrest3 H1: this game's arrest - its sentence runs here
        }
        ++g_prisonSent;
        PrisonLog("-> PRISON uid=" + S((long long)uid) + " caged here by this game's engine, cage='" + std::string(key)
                  + "' outer='" + std::string(outer) + "' - its own game is asked to cage it");
    }
}

// arrest3: the owner could not cage its character - the guard's game takes its copy out again (no half-arrest on one screen).
void PrisonSendRefused(unsigned int uid, unsigned int askerPeer)   /* M7b slice 1: back to the guard's game that asked */
{
    cooprison::PrisonMsg m; m.uid = uid; m.kind = cooprison::kPrisonRefused;
    if (net::SendPrison(m, askerPeer)) ++g_prisonRefusedSent;
}
// the owner could not put its character in the bed - the rescuer's game takes its copy out again. The refusal names the bed
// (the rescuer applies it only to its ask about that bed); `permanent`: the bed itself (full, not a bed) or the character's
// death refused it, so the rescuer does not ask about that bed again - any other refusal may pass and the next put-down asks.
void BedSendRefused(unsigned int uid, unsigned int askerPeer, const std::string& bedKey, bool permanent, const char* why)
{
    cooprison::PrisonMsg m; m.uid = uid; m.kind = cooprison::kPrisonBedRefused;
    if (bedKey.size() <= cooprison::kPrisonMaxKey) m.cageKey = bedKey;
    m.permanent = permanent ? 1 : 0;
    if (net::SendPrison(m, askerPeer)) ++g_bedRefused;
    PrisonLog("<- BED uid=" + S((long long)uid) + " NOT put in the bed '" + bedKey + "' (" + std::string(permanent ? "for good" : "for now")
              + "): " + std::string(why));
}

// (b) The owner's side.
void PrisonApplyOwn(DWORD now)
{
    std::map<unsigned int, PrisonIn>::iterator it = g_prisonIn.begin();
    while (it != g_prisonIn.end())
    {
        const unsigned int uid = it->first;
        PrisonIn& p = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c) || !net::IsUidMine(uid) || c->hasDied())
        { ++g_prisonFailed; if (c != 0 && net::IsUidMine(uid)) PrisonSendRefused(uid, p.peer); g_prisonIn.erase(it++); continue; }
        const bool late = now - p.firstAt >= kPrisonGiveUpMs;
        const int ins = PoseInSomethingPod(c);
        if (ins == 2) { ++g_prisonAlready; g_prisonIn.erase(it++); continue; }
        if (ins != 0) { ++g_prisonFailed; PrisonSendRefused(uid, p.peer); g_prisonIn.erase(it++); continue; }   // in a bed here: left alone (v1)
        if (BeingCarried(c))   // the guard's put-down (its STATE) drops it here first
        {
            if (late) { ++g_prisonStillCarried; PrisonSendRefused(uid, p.peer); g_prisonIn.erase(it++); } else ++it;
            continue;
        }
        if (p.triedAt != 0 && now - p.triedAt < kPrisonRetryMs) { ++it; continue; }
        p.triedAt = now | 1u;
        // the cage, found as a bed is: the zone list, then the outer building's interior pieces (BED1)
        PoseWant tw;
        coopstate::PoseClear(&tw.pw);
        tw.pw.kind = coopstate::kPoseCage;
        std::strncpy(tw.pw.bedKey, p.m.cageKey.c_str(), coopstate::kPoseStrMax); tw.pw.bedKey[coopstate::kPoseStrMax] = 0;
        std::strncpy(tw.pw.outerKey, p.m.outerKey.c_str(), coopstate::kPoseStrMax); tw.pw.outerKey[coopstate::kPoseStrMax] = 0;
        tw.peer = p.peer; tw.applied = 0; tw.held = false; tw.counted = false; tw.bedMiss = false; tw.bedMissCounted = false;
        tw.refusedNow = false; tw.bedAt = 0; tw.approachCounted = false; tw.confirmedAt = now;
        PoseWantResetBed(tw);
        void* cage = ObjectByPositionKey(tw.pw.bedKey);
        if (cage == 0 && tw.pw.outerKey[0] != 0) cage = PoseBedViaLayout(uid, tw);
        if (cage == 0 || !PlausibleObject(cage))
        {
            if (late) { ++g_prisonNoCage; PrisonSendRefused(uid, p.peer); PrisonLog("<- PRISON uid=" + S((long long)uid) + " NOT caged: cage '" + p.m.cageKey + "' not found here"); g_prisonIn.erase(it++); }
            else ++it;
            continue;
        }
        PrisonQueueOp(uid, true, cage, true, false, 0, 0, false, (int)p.m.sentence, p.peer);   // isFreeSlot, setPrisonMode, the lock and the sentence (the arresting game's verdict, P11 f3) at the safe point
        g_prisonIn.erase(it++);
    }
}

// The owner's side of the bed hand-off: its own knocked-out character follows the other game's rescuer into the bed. Only a
// helpless one (as OwnBodyMayBeCarried) that is not in a bed or cage here; the rescuer's put-down reaches this game in its
// STATE, so a character still carried here waits up to kPrisonGiveUpMs. The bed is found as a pose bed is (its key, then the
// outer building's interior pieces); setBedMode and the move to the sent spot run at the safe point.
void BedApplyOwn(DWORD now)
{
    std::map<unsigned int, BedIn>::iterator it = g_bedIn.begin();
    while (it != g_bedIn.end())
    {
        const unsigned int uid = it->first;
        BedIn& p = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c) || !net::IsUidMine(uid) || c->hasDied())
        {
            if (c != 0 && net::IsUidMine(uid))
                BedSendRefused(uid, p.peer, p.m.cageKey, PlausibleObject(c), PlausibleObject(c) ? "dead" : "gone");   // plausible here means dead
            g_bedIn.erase(it++); continue;
        }
        const bool late = now - p.firstAt >= kPrisonGiveUpMs;
        if (PoseInSomethingPod(c) != 0) { BedSendRefused(uid, p.peer, p.m.cageKey, false, "already in a bed or cage here"); g_bedIn.erase(it++); continue; }
        if (BeingCarried(c))
        {
            if (late) { BedSendRefused(uid, p.peer, p.m.cageKey, false, "still carried here"); g_bedIn.erase(it++); } else ++it;
            continue;
        }
        if (!OwnBodyMayBeCarried(c)) { BedSendRefused(uid, p.peer, p.m.cageKey, false, "awake here"); g_bedIn.erase(it++); continue; }
        if (p.triedAt != 0 && now - p.triedAt < kPrisonRetryMs) { ++it; continue; }
        p.triedAt = now | 1u;
        PoseWant tw;
        coopstate::PoseClear(&tw.pw);
        tw.pw.kind = coopstate::kPoseBed;
        std::strncpy(tw.pw.bedKey, p.m.cageKey.c_str(), coopstate::kPoseStrMax); tw.pw.bedKey[coopstate::kPoseStrMax] = 0;
        std::strncpy(tw.pw.outerKey, p.m.outerKey.c_str(), coopstate::kPoseStrMax); tw.pw.outerKey[coopstate::kPoseStrMax] = 0;
        tw.peer = p.peer; tw.applied = 0; tw.held = false; tw.counted = false; tw.bedMiss = false; tw.bedMissCounted = false;
        tw.refusedNow = false; tw.bedAt = 0; tw.approachCounted = false; tw.confirmedAt = now;
        PoseWantResetBed(tw);
        void* bed = ObjectByPositionKey(tw.pw.bedKey);
        if (bed == 0 && tw.pw.outerKey[0] != 0) bed = PoseBedViaLayout(uid, tw);
        if (bed == 0 || !PlausibleObject(bed))
        {
            if (late) { BedSendRefused(uid, p.peer, p.m.cageKey, false, "the bed was not found here"); g_bedIn.erase(it++); }
            else ++it;
            continue;
        }
        if (!BedQueueOp(uid, bed, p.m.pos, p.peer, p.m.cageKey)) { ++it; continue; }   // the queue is full: tried again after kPrisonRetryMs
        g_bedIn.erase(it++);
    }
}

// arrest3 (answer 1): what Task_ReleasePrisoner 0x357040 does to the PRISONER, repeated on this game's own character in the
// engine's order - clearBounty(bm, the releaser's faction) BEFORE setPrisonMode off (its second key is the cage faction only
// while +0x2F8 == 2), only when that faction is a law faction (+0x40), as the engine does; the pardon (+0x138 faction, +0x140
// until = now + 1.0000001639; skipped when the clock is unreadable); the cage unlocked; out of the cage; then the
// GET_OUT_OF_CAGE_LEGIT order (review-arrest3 M4). The releaser's faction comes with the message (review-arrest3 M2): relMode
// 1 = named and resolved here, 2 = a player freed it (no bounty cleared), 0 = unknown - the cage's faction is used.
// NOT repeated: the memory resets, (+0x1A0)+0xEA (not identified), the player message. 1 released, 0 no address, -1 faulted.
int PrisonReleaseWritesPod(::Character* c, void* cage, double pardonUntil, void* relFac, int relMode, void* clearFn, bool* orderOut)
{
    *orderOut = true;
    __try
    {
        void** vt = *(void***)cage;
        void* cageFac = ((GetFactionVtFn)vt[0x58 / 8])(cage);
        void* fac = (relMode == 1 && relFac != 0) ? relFac : cageFac;
        const bool law = relMode == 2 ? false : (relMode == 1 ? *((const unsigned char*)fac + 0x40) != 0 : true);
        if (law) ((ClearBountyFn)clearFn)((char*)c + kCharBountyMgrOff, fac);
        *orderOut = law || relMode == 2;   // recheck-arrest3 4: a releaser neither law nor player gets no order from the engine
        if (pardonUntil > 0.0)
        {
            *(void**)((char*)c + kCharPardonFactionOff) = fac;
            *(double*)((char*)c + kCharPardonUntilOff) = pardonUntil;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PrisonWalkOutPod(::Character* c)   // 1 ordered, 0 no address, -1 faulted
{
    if (kAddOrderRva == 0 || kAddOrderArgGlobalRva == 0) return 0;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const AddOrderFn fn = (AddOrderFn)(base + kAddOrderRva);
    __try
    {
        const unsigned long long a7 = *(const unsigned long long*)(base + kAddOrderArgGlobalRva);
        fn(c, 0, kTaskGetOutOfCageLegit, 0, 0, 1, a7);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PrisonReleasePod(::Character* c, void* cage, double pardonUntil, void* relFac, int relMode)
{
    if (kClearBountyRva == 0) return 0;
    const void* clearFn = (const void*)((uintptr_t)::GetModuleHandleA(0) + kClearBountyRva);
    bool order = true;
    if (PrisonReleaseWritesPod(c, cage, pardonUntil, relFac, relMode, (void*)clearFn, &order) != 1) return -1;
    PrisonLockSetPod(cage, 0);
    if (PosePrisonPod(c, false, 0) != 1) return -1;
    if (order)
    {
        const int o = PrisonWalkOutPod(c);
        if (o == 1) ++g_releaseOrdered; else ++g_releaseOrderFailed;
    }
    return 1;
}

// review-arrest3 M3: the pardon on a copy the owner could not cage, written before it comes out (the cage's faction), so this
// game's guards do not arrest it again at once and ask the owner again for the same lasting reason.
int PrisonCopyPardonPod(::Character* c, void* cage, double until)
{
    if (until <= 0.0) return 0;
    __try
    {
        void** vt = *(void***)cage;
        void* fac = ((GetFactionVtFn)vt[0x58 / 8])(cage);
        *(void**)((char*)c + kCharPardonFactionOff) = fac;
        *(double*)((char*)c + kCharPardonUntilOff) = until;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The cage this character is in, from its own hand (+0x300), or 0.
void* PrisonCageOf(::Character* c)
{
    unsigned char hd[kHandIdBytes];
    int ins = 0, actLong = 0, stumble = 0, looped = 0;
    float speed = 0.0f;
    char act[coopstate::kPoseStrMax + 1];
    if (!PoseReadPod(c, &ins, hd, act, &actLong, &stumble, &looped, &speed) || ins != 2) return 0;
    unsigned int f[5];
    std::memcpy(f, hd, sizeof(f));
    if (f[3] == 0 && f[4] == 0) return 0;
    hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
    void* b = (void*)h.asBuilding();
    return (b != 0 && PlausibleObject(b)) ? b : 0;
}

// arrest2 read 3: the sentence as Task_PutInSomething starts it (358610:150-152) - notifyStartPrisonSentence(bm, the cage
// owner's resolved bounty faction). Run after setPrisonMode, so 0x851140 resolves to the cage's faction. 1 set, 0 no address,
// -1 faulted. *hoursOut = the hours it wrote (Character +0x190).
/* P11 (p11-design.md 1g / 2e): Task_PutInSomething 0x358610 starts notifyStartPrisonSentence only when
   GetActualBounty 0x8531B0(victim +0xF0, the cage's faction) > 0 (the call at 0x3589EE, `test eax, eax; jle` - Read,
   disassembly: an int). A slaver, kidnapper or cannibal cage has no bounty -> no sentence (2). No row -> as before. */
/* P11 f3: P11BountyFn / kP11BountyRva are declared above PrisonWatchCopies (the arresting game reads the bounty too). */
long long g_prisonNoBounty = 0;
int PrisonSentencePod(::Character* c, void* cage, float* hoursOut, int verdict)   /* verdict: the arresting game's (P11 f3) */
{
    *hoursOut = -1.0f;
    if (kNotifyStartPrisonSentenceRva == 0 || kGetBountyFactionPrRva == 0) return 0;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const GetBountyFactionFn resolve = (GetBountyFactionFn)(base + kGetBountyFactionPrRva);
    const NotifyStartPrisonSentenceFn notify = (NotifyStartPrisonSentenceFn)(base + kNotifyStartPrisonSentenceRva);
    __try
    {
        void** vt = *(void***)cage;
        void* fac = ((GetFactionVtFn)vt[0x58 / 8])(cage);
        void* bm = (char*)c + kCharBountyMgrOff;
        void* key = resolve(bm, fac);
        if (verdict == cooprison::kPrisonSentenceNo) return 2;   /* P11 f3 (M6): the arresting game's engine started none (its bounty read) */
        if (verdict != cooprison::kPrisonSentenceYes && kP11BountyRva != 0
            && ((P11BountyFn)(base + (uintptr_t)kP11BountyRva))(bm, fac) <= 0) return 2;   /* verdict unknown: our own read, as the engine */
        notify(bm, key);
        *hoursOut = *(const float*)((const char*)c + kCharSentenceHoursOff);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int PrisonSentenceSetPod(::Character* c, float hours)
{
    __try { *(float*)((char*)c + kCharSentenceHoursOff) = hours; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PrisonSentenceAddPod(::Character* c, float add)
{
    __try { *(float*)((char*)c + kCharSentenceHoursOff) += add; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

}   // namespace (arrest2)
long long PrisonNoBountyCount() { return g_prisonNoBounty; }   /* P11: the [CAPTURE] REPORT's cageNoSentence */

/* TEST-ONLY lever crimetest bountyset (crime.cpp) - see spawn.h. */
int BountyKeyForPod(::Character* c, void* faction, void** key)
{
    *key = 0;
    if (kGetBountyFactionPrRva == 0) return 0;
    const GetBountyFactionFn resolve = (GetBountyFactionFn)((uintptr_t)::GetModuleHandleA(0) + kGetBountyFactionPrRva);
    __try { *key = resolve((char*)c + kCharBountyMgrOff, faction); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *key = 0; return -1; }
}

/* inv7a (e47-inv7-replan.md 3.1, gapA path 2; owner decision 2026-09-26) - THE OTHER PLAYER'S STALE COPIES, REMOVED WHEN
   A WORLD LOADS. A save made while linked carries the other player's characters as this game's copies in a stand-in faction;
   loading it showed them beside the fresh copies the other game announces. Once per world generation, on the first frame
   engine writes are allowed and BEFORE the arrival queue applies anything (so before the first peer SPAWN), every
   character in the engine's update list whose faction's RECORD id is coop-p<n> or coop-peer (StandInRecordSlot - the
   stand-in table, and so IsPeerFaction, is empty right after a load) and that no mirror row knows is destroyed by the
   route DropPeerOwnedCopy uses (g_selfDespawnObj + DestroyLocalObject, GameWorld::destroy). Caged / imprisoned ones too:
   they leave the cage first by setPrisonMode(false) at the K2 safe point (the cage's occupant set belongs to the AI
   worker - the arrest2 rule), and are destroyed on a later pass; a carried one is put down by its carrier
   (dropCarriedObject), one in a bed gets up (setBedMode(false)), one carrying something puts it down. What cannot be
   handled safely is skipped, counted and logged (coopsp::Decide). Buildings owned by stand-ins are not touched. */
namespace {
const unsigned long kPurgeCageWaitMs = 5000;   // the longest the arrival queue is held for a cage-out
long  g_purgeDoneGen = -1;   // the world generation the purge last finished for (MAIN THREAD)
long  g_purgeGen = -1;       // the generation the running purge belongs to
DWORD g_purgeT0 = 0;
std::set<const void*> g_purgeSkipped, g_purgeDestroyed, g_purgeCageQueued, g_purgeCarrierTried, g_purgeBedTried, g_purgeOwnDropTried, g_purgePlatoons;
long long g_purgeChars = 0, g_purgeCaged = 0, g_purgeSkips = 0, g_purgeFaulted = 0, g_purgeRefused = 0;
long long g_purgeCarrierDrops = 0, g_purgeBedOuts = 0, g_purgeOwnDrops = 0, g_purgeCageOutCalled = 0, g_purgeCageOutFailed = 0;
int g_purgeRunning = 0, g_purgeWhy = 0, g_purgeReqMask = 0;   /* inv7a2: a pass in progress; what started it; what asked since */
long long g_purgePasses = 0, g_purgeWalked = 0, g_purgeReqWake = 0, g_purgeReqLinkUp = 0;
long long g_purgeReqSweep = 0, g_purgeReqSweepAsked = 0;   /* inv7a-b: stand-ins the P033 sweep met; passes it asked for */
long long g_purgeReqSweepSeen = 0, g_purgeReqSweepFull = 0, g_purgeReqSweepWait = 0, g_purgeSweepCagedLeft = 0;   /* area2 fold: SweepRequestDecide's refusals; caged copies a sweep-only pass left */
DWORD g_purgePassEndAt = 0;   /* area2 fold: when the last pass ENDED (0 = none yet) */
long g_sweepReqGen = -1; std::set<const void*> g_sweepReqAsked;   /* area2 fold: copies the sweep asked for in this world generation - survives PurgeResetSets */
std::vector< ::Character*> g_purgeCageOut;   // pushed on the main thread, drained at the K2 safe point - as g_prisonOps

void* PurgePlatoonPod(::Character* c)
{
    __try { return (void*)c->getSquad(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
Faction* PurgeFactionPod(::Character* c)   /* inv7a2 (review-inv7a LOW): the faction read under its own frame */
{
    __try { return c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int PurgeDestroyPod(::Character* c)
{
    __try { return DestroyLocalObject(c) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PurgeDropPod(DropCarriedObjectFn drop, ::Character* carrier)
{
    if (drop == 0) return 0;
    __try { drop(carrier, false, false); return 1; }   /* a full drop, no ragdoll (CarryBreakApply's form) */
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void PurgeResetSets()
{
    g_purgeSkipped.clear(); g_purgeDestroyed.clear(); g_purgeCageQueued.clear(); g_purgeCarrierTried.clear();
    g_purgeBedTried.clear(); g_purgeOwnDropTried.clear(); g_purgePlatoons.clear();
}
/* K2 SAFE POINT (PrisonSafePointDrain): the cage-outs the purge queued. */
void StandinPurgeCageOutDrain()
{
    if (g_purgeCageOut.empty()) return;
    if (EngineWritesBlocked()) { g_purgeCageOut.clear(); return; }
    for (size_t i = 0; i < g_purgeCageOut.size(); ++i)
    {
        ::Character* c = g_purgeCageOut[i];
        if (!PlausibleObject(c) || PoseInSomethingPod(c) != 2) continue;
        if (PosePrisonPod(c, false, 0) == 1) ++g_purgeCageOutCalled; else ++g_purgeCageOutFailed;
    }
    g_purgeCageOut.clear();
}
/* inv7a3 (review-inv7a2 MEDIUM): the woken stand-in platoons waiting for their members (MAIN THREAD). */
struct WokenStandin { void* faction; std::string id; long gen; DWORD t0; };
std::map<void*, WokenStandin> g_wokenPending;
long long g_wokenReady = 0, g_wokenGone = 0, g_wokenGaveUp = 0, g_wokenUnnamed = 0;
void StandinWokenRecheck(long worldGen)
{
    const DWORD now = ::GetTickCount();
    for (std::map<void*, WokenStandin>::iterator it = g_wokenPending.begin(); it != g_wokenPending.end(); )
    {
        const WokenStandin& w = it->second;
        int d = coopsp::kWokenGone;   /* noted in an older world: gone */
        if (w.gen == worldGen)
        {
            const int awake = (FindAwakePlatoon((::Faction*)w.faction, w.id) == it->first) ? 1 : 0;
            d = coopsp::WokenRecheck(awake, awake != 0 ? StorePlatoonActiveChars(it->first) : -1, (unsigned long)(now - w.t0), coopsp::kWokenCapMs);
        }
        if (d == coopsp::kWokenWait) { ++it; continue; }
        if (d == coopsp::kWokenPurge) { ++g_wokenReady; g_purgeReqMask |= coopsp::kTrigWake; }
        else if (d == coopsp::kWokenGaveUp)
        {
            ++g_wokenGaveUp; g_purgeReqMask |= coopsp::kTrigWake;
            DebugLog("[SPAWN] standin purge: woken stand-in platoon id='" + w.id + "' still has no members after " + S((long long)(now - w.t0))
                     + " ms - gave up waiting (gaveUp=" + S(g_wokenGaveUp) + "); a pass runs anyway");
        }
        else ++g_wokenGone;
        g_wokenPending.erase(it++);
    }
}
}   // namespace (inv7a)

int StandinPurgeTick(long worldGen)
{
    /* inv7a2 (run T412): T412's load pass ran one frame after the load returned, before the loaded platoons woke, found
       chars=0 and never ran again. A pass now starts when the load pass is due OR an event asked for one (a stand-in
       platoon woke - StandinPurgeOnWake; the game link came up - StandinPurgeOnLinkUp). Requests made during a pass
       wait for the next one. */
    if (g_sweepReqGen != worldGen) { g_sweepReqAsked.clear(); g_sweepReqGen = worldGen; }   /* area2 fold: a new world, new copies */
    if (EngineWritesBlocked()) return 0;
    StandinWokenRecheck(worldGen);   /* inv7a3: a woken stand-in platoon asks for a pass once its members exist */
    if (g_purgeRunning == 0 || g_purgeGen != worldGen)
    {
        const int loadDue = coopsp::Due(worldGen, g_purgeDoneGen, 0);
        if (coopsp::PassDue(loadDue, g_purgeReqMask) == 0) return 0;
        g_purgeWhy = (loadDue != 0 ? coopsp::kTrigLoad : 0) | g_purgeReqMask; g_purgeReqMask = 0; g_purgeRunning = 1; ++g_purgePasses;
        g_purgeGen = worldGen; g_purgeT0 = ::GetTickCount();
        PurgeResetSets(); g_purgeCageOut.clear();
        g_purgeChars = g_purgeCaged = g_purgeSkips = g_purgeFaulted = g_purgeRefused = 0;
        g_purgeCarrierDrops = g_purgeBedOuts = g_purgeOwnDrops = g_purgeCageOutCalled = g_purgeCageOutFailed = 0;
    }
    if (!PlausiblePtr(coop::GameWorldPtr())) return 0;   /* asked again next frame */
    const GameHashSet<Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    const size_t n = all.size();
    g_purgeWalked = (long long)n;   /* inv7a2: walked= */
    if (n > 20000)
    {
        ErrorLog("[SPAWN] standin purge at " + std::string(coopsp::PassPrefix(g_purgeWhy)) + ": REFUSED - update list size " + S((long long)n) + " is implausible (gen="
                 + S((long long)worldGen) + ")");
        g_purgeDoneGen = worldGen; g_purgeRunning = 0; PurgeResetSets();
        return 0;
    }
    /* Collect first: GameWorld::destroy may change the update list, so nothing is destroyed while it is walked. */
    std::vector< ::Character*> cands, carriers;
    for (GameHashSet<Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = *it;
        if (!PlausibleObject(c)) continue;
        if (CarryFlag(c)) carriers.push_back(c);
        if (g_purgeSkipped.count(c) != 0 || g_purgeDestroyed.count(c) != 0) continue;
        Faction* f = PurgeFactionPod(c);   /* inv7a2: guarded */
        if (!PlausibleObject(f) || coopsp::IsStandInRecord(StandInRecordSlot(f)) == 0) continue;
        cands.push_back(c);
    }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const DropCarriedObjectFn drop = (kDropCarriedObjectRva != 0) ? (DropCarriedObjectFn)(base + kDropCarriedObjectRva) : 0;
    const SetBedModeFn setBed = (kSetBedModeRva != 0) ? (SetBedModeFn)(base + kSetBedModeRva) : 0;
    const unsigned long elapsedMs = (unsigned long)(::GetTickCount() - g_purgeT0);   /* inv7a2 (review-inv7a LOW): ONE clock reading per pass */
    const int timedOut = elapsedMs >= kPurgeCageWaitMs ? 1 : 0;
    int waiting = 0;
    for (size_t i = 0; i < cands.size(); ++i)
    {
        ::Character* c = cands[i];
        for (int step = 0; step < 6; ++step)   /* each step is decided from a fresh read; the tried-sets make it finish */
        {
            coopsp::Seen s = coopsp::SeenNone();
            Faction* f = PurgeFactionPod(c);   /* inv7a2: guarded */
            s.recordSlot   = PlausibleObject(f) ? StandInRecordSlot(f) : -3;
            s.known        = FindSpawnedUid(c) != 0 ? 1 : 0;
            s.guarded      = (c == GetTarget() || IsPlayerFactionPod(c) == 1) ? 1 : 0;
            s.inSomething  = PoseInSomethingPod(c);
            s.cageAddr     = kSetPrisonModeRva != 0 ? 1 : 0;
            s.cageQueued   = g_purgeCageQueued.count(c) != 0 ? 1 : 0;
            s.cageTimedOut = timedOut;
            s.beingCarried = BeingCarried(c) ? 1 : 0;
            ::Character* carrier = 0;
            if (s.beingCarried != 0)
                for (size_t j = 0; j < carriers.size(); ++j)
                    if (carriers[j] != c && Carries(carriers[j], c)) { carrier = carriers[j]; break; }
            s.carrierFound = carrier != 0 ? 1 : 0;
            s.carrierTried = (g_purgeCarrierTried.count(c) != 0 || drop == 0) ? 1 : 0;
            s.bedTried     = (g_purgeBedTried.count(c) != 0 || setBed == 0) ? 1 : 0;
            s.carrying     = CarryFlag(c) ? 1 : 0;
            s.ownDropTried = (g_purgeOwnDropTried.count(c) != 0 || drop == 0) ? 1 : 0;
            /* area2 fold (review-area2 HIGH): a pass only the sweep asked for never holds the in-queue for a cage wait - a caged
               copy is left to the wake/load passes, as before inv7a-b. */
            if (coopsp::SweepOnlyPass(g_purgeWhy) != 0 && s.inSomething == 2 && s.known == 0 && s.guarded == 0) { g_purgeSkipped.insert(c); ++g_purgeSweepCagedLeft; break; }
            int why = coopsp::kWhyNone;
            const int act = coopsp::Decide(s, &why);
            if (act == coopsp::kActNone) break;
            if (act == coopsp::kActSkip)
            {
                g_purgeSkipped.insert(c); ++g_purgeSkips;
                ErrorLog("[SPAWN] standin purge: a stand-in character (record slot " + S((long long)s.recordSlot)
                         + ", in=" + S((long long)s.inSomething) + ") SKIPPED - left standing: " + coopsp::WhyText(why));
                break;
            }
            if (act == coopsp::kActCageOut) { g_purgeCageQueued.insert(c); g_purgeCageOut.push_back(c); ++waiting; break; }
            if (act == coopsp::kActWaitCage) { ++waiting; break; }
            if (act == coopsp::kActDropCarrier) { g_purgeCarrierTried.insert(c); if (PurgeDropPod(drop, carrier) == 1) ++g_purgeCarrierDrops; continue; }
            if (act == coopsp::kActBedOut) { g_purgeBedTried.insert(c); if (PoseBedPod(setBed, c, false, 0) == 1) ++g_purgeBedOuts; continue; }
            if (act == coopsp::kActDropOwn) { g_purgeOwnDropTried.insert(c); if (PurgeDropPod(drop, c) == 1) ++g_purgeOwnDrops; continue; }
            /* coopsp::kActDestroy - DropPeerOwnedCopy's route (RemoveLocalCopy step 3); no row, no puppet, no uid to forget */
            void* pl = PurgePlatoonPod(c);
            const void* prevSelf = g_selfDespawnObj;
            g_selfDespawnObj = c;
            const int r = PurgeDestroyPod(c);
            g_selfDespawnObj = prevSelf;
            if (r == 1)
            {
                g_purgeDestroyed.insert(c); ++g_purgeChars; g_sweepReqAsked.erase((const void*)c);
                if (pl != 0) g_purgePlatoons.insert(pl);
                if (g_purgeCageQueued.count(c) != 0) ++g_purgeCaged;
            }
            else
            {
                g_purgeSkipped.insert(c);
                if (r < 0) ++g_purgeFaulted; else ++g_purgeRefused;
                ErrorLog(std::string("[SPAWN] standin purge: GameWorld::destroy ") + (r < 0 ? "FAULTED" : "was refused (no hook / no world)")
                         + " for a stand-in character (record slot " + S((long long)s.recordSlot) + ") - left standing");
            }
            break;
        }
    }
    if (coopsp::HoldDrain(waiting, elapsedMs, kPurgeCageWaitMs) != 0) return 1;
    DebugLog("[SPAWN] standin purge at " + std::string(coopsp::PassPrefix(g_purgeWhy)) + ": chars=" + S(g_purgeChars)
             + " platoons=" + S((long long)g_purgePlatoons.size())
             + " caged=" + S(g_purgeCaged)
             + " skipped=" + S(g_purgeSkips)
             + " faulted=" + S(g_purgeFaulted)
             + " refused=" + S(g_purgeRefused)
             + " carrierDrops=" + S(g_purgeCarrierDrops) + " bedOuts=" + S(g_purgeBedOuts) + " ownDrops=" + S(g_purgeOwnDrops)
             + " cageOut[called,failed]=" + S(g_purgeCageOutCalled) + "," + S(g_purgeCageOutFailed)
             + " gen=" + S((long long)worldGen) + " waitMs=" + S((long long)elapsedMs)
             + " walked=" + S(g_purgeWalked) + " pass=" + S(g_purgePasses)
             + " requests[wake,linkUp]=" + S(g_purgeReqWake) + "," + S(g_purgeReqLinkUp)
             + " sweepRequests[met,asked,seen,full,wait,cagedLeft]=" + S(g_purgeReqSweep) + "," + S(g_purgeReqSweepAsked) + "," + S(g_purgeReqSweepSeen)
               + "," + S(g_purgeReqSweepFull) + "," + S(g_purgeReqSweepWait) + "," + S(g_purgeSweepCagedLeft)   /* inv7a-b + area2 fold */
             + " woken[ready,gaveUp,gone,pending,unnamed]=" + S(g_wokenReady) + "," + S(g_wokenGaveUp) + "," + S(g_wokenGone) + "," + S((long long)g_wokenPending.size()) + "," + S(g_wokenUnnamed)
             + " triggers=" + coopsp::TriggerText(g_purgeWhy));
    g_purgeDoneGen = worldGen; g_purgeRunning = 0;
    g_purgeCageOut.clear();
    PurgeResetSets();
    g_purgePassEndAt = ::GetTickCount(); if (g_purgePassEndAt == 0) g_purgePassEndAt = 1;   /* area2 fold: the sweep's 5 s runs from here */
    return 0;
}

/* inv7a2 (run T412): the purge's EVENT triggers. MAIN THREAD (InQueueDrain, before StandinPurgeTick). */
void StandinPurgeOnWake(long count)
{
    if (count <= 0) return;
    g_purgeReqWake += count;   /* inv7a3: counted only - the pass is asked by each woken platoon's re-check (StandinWokenRecheck) */
}
void StandinPurgeOnLinkUp(long linkGen)
{
    g_purgeReqMask |= coopsp::kTrigLinkUp; ++g_purgeReqLinkUp;
    DebugLog("[SPAWN] standin purge requested: the game link came up (session generation " + S((long long)linkGen)
             + ") - one pass runs before anything this link sends is applied");
}
/* inv7a-b (Read 2026-09-27): the purge's fallbacks (the 5 s give-up, a woken squad with no readable id, more than 64 wakes)
   can run before a woken stand-in squad's members exist; the P033 sweep then met the copy. It no longer adopts it and asks
   for a pass here instead. area2 fold (review-area2 HIGH): ONCE per copy per world generation (a copy the purge leaves
   standing asked every 5 s forever, and each such pass could hold the in-queue for a full cage wait), 5 s after the last
   pass ENDED, not while a uid table FULL refusal stands (mirror1 fold, review-mirror1 #3: the flag clears when a row
   frees); the pass it asks for has its own trigger bit (sweep). */
void StandinPurgeRequest(const void* character)
{
    ++g_purgeReqSweep;
    const DWORD now = ::GetTickCount();
    const int d = coopsp::SweepRequestDecide(::InterlockedCompareExchange(&g_uidTableFullSeen, 0, 0) != 0 ? 1 : 0,
                                             g_sweepReqAsked.count(character) != 0 ? 1 : 0, g_purgeRunning,
                                             g_purgePassEndAt != 0 ? 1 : 0, (unsigned long)(now - g_purgePassEndAt), coopsp::kSweepReqGapMs);
    if (d == coopsp::kSweepReqSeen) { ++g_purgeReqSweepSeen; return; }
    if (d == coopsp::kSweepReqFull) { ++g_purgeReqSweepFull; return; }
    if (d == coopsp::kSweepReqWait) { ++g_purgeReqSweepWait; return; }
    g_sweepReqAsked.insert(character); ++g_purgeReqSweepAsked;
    g_purgeReqMask |= coopsp::kTrigSweep;
}
void StandinPurgeNoteWoken(void* platoon, void* faction, const std::string& id, long worldGen)
{
    if (platoon == 0 || faction == 0 || id.empty() || g_wokenPending.size() >= 256)
    { ++g_wokenUnnamed; g_purgeReqMask |= coopsp::kTrigWake; return; }   /* nothing to re-check by: the inv7a2 immediate pass */
    std::map<void*, WokenStandin>::iterator it = g_wokenPending.find(platoon);
    if (it != g_wokenPending.end() && it->second.gen == worldGen && it->second.id == id) return;   /* already waiting */
    WokenStandin w; w.faction = faction; w.id = id; w.gen = worldGen; w.t0 = ::GetTickCount();
    g_wokenPending[platoon] = w;
}

/* orphan1 (T420/T426, decision 37) - PEOPLE THIS GAME CREATED IN ANOTHER GAME'S AREA WHILE THE GAME LINK WAS DOWN.
   The P033 adoption pass notes (OrphanNote) every unregistered non-player character standing in an area another game
   holds; nobody announces those (decision 33) and, before this, nothing removed them. Here, on the main thread after the
   stand-in purge, each noted character that is still in the engine's update list is judged by coopor::Decide and removed
   by the purge's own route (g_selfDespawnObj + DestroyLocalObject); a carried one is put down by its non-player carrier,
   a bedded one gets up, one carrying something puts it down. Never removed: a player-faction character, one carried by a
   player character, a caged one (possibly a player's prisoner), or one near a fight with a player character (300 units,
   a later pass). review-orphan1: nor one within 100 units of a player character, nor before it has been noted
   continuously for 5 s (a gap over 1 s between notes restarts that clock) with the game link up for 10 s. At most
   coopor::kPerPass removals per judging pass, at most one pass per 200 ms. The holder's world wins; there is no
   handover. */
namespace {
std::set<const void*> g_orphanQ;
std::set<std::pair<const void*, int> > g_orphanSkipSeen;
std::set<const void*> g_orphanCarrierTried, g_orphanBedTried, g_orphanOwnDropTried;
long g_orphanGen = -1;
DWORD g_orphanLastMs = 0;
long long g_orphanRemoved = 0, g_orphanSkipped = 0, g_orphanFaulted = 0, g_orphanQueued = 0, g_orphanLines = 0;
long long g_orphanSkipWhy[coopor::kWhyCount] = { 0 };
struct OrphanAgg { std::string faction; int sx, sy, slot; long long n; DWORD t0; unsigned long notedMs; };
std::map<const void*, OrphanAgg> g_orphanAgg;
const float kOrphanFightR = 300.0f;        /* ~30 m (runners cover 45-85 units/s) */
const size_t kOrphanQueueCap = 4096;
const long long kOrphanLineCap = 60;
const float kOrphanNearR = 100.0f;        /* review-orphan1 HIGH: talking, trading, looting need adjacency */
const DWORD kOrphanNoteGapMs = 1000;      /* a gap longer than this between two notes restarts the continuous-noted clock */
struct OrphanNoteT { DWORD first, last; };
std::map<const void*, OrphanNoteT> g_orphanNoteT;
std::set<const void*> g_orphanNotedSeen;   /* people noted this world generation (counted once each) */
long g_orphanLinkGen = -1; int g_orphanWasLinked = 0; DWORD g_orphanLinkUpMs = 0;
/* T-304 (1) (run T674): a stray the engine woke from a squad this game rebuilt while unlinked stood beside the other game's
   live copy of the same animal, within 100 units of this game's player, so the near-player rule kept it for good - it ate
   on one screen only and was later adopted as this game's own. A noted character that DUPLICATES a live copy (same template
   and faction within coopor::kDupCopyR) is removed although a player is near, and every noted character waiting for a
   later pass has its decisions held (SuppressCharacter) until it is removed or stops being noted: an unregistered
   character must not act in an area another game holds. */
std::set<const void*> g_orphanHeld;
const size_t kOrphanHoldCap = 256;   /* leaves the gate table (kMaxGated, 4096 slots since T-354) to the copies */
long long g_orphanDupRemoved = 0, g_orphanHoldN = 0, g_orphanHoldReleased = 0, g_orphanHoldFull = 0;
long long g_orphanHoldRefused = 0, g_orphanHoldNotOurs = 0;   /* fold 1: the gate refused / someone else already gates it */
GameData* OrphanDataPod(::Character* c)
{
    __try { return c->getRecordDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void OrphanHold(::Character* c)
{
    if (g_orphanHeld.count((const void*)c) != 0) return;
    if (g_orphanHeld.size() >= kOrphanHoldCap) { ++g_orphanHoldFull; return; }
    if (IsSuppressed(c)) { ++g_orphanHoldNotOurs; return; }   /* fold 1: gated by someone else - not ours to hold or release */
    SuppressCharacter(c, true);
    if (!IsSuppressed(c)) { ++g_orphanHoldRefused; return; }   /* fold 1: the gate refused (no AI / no free slot) - not held */
    g_orphanHeld.insert((const void*)c); ++g_orphanHoldN;
}
void OrphanRelease(::Character* c)   /* c is in the engine's update list now; a no-op when it is not held */
{
    if (g_orphanHeld.erase((const void*)c) == 0) return;
    ++g_orphanHoldReleased;
    const unsigned int u = FindSpawnedUidForDestroy(c);
    if (u != 0 && !net::IsUidMine(u)) return;   /* it became a copy of the other game's character: the copy keeps its gate */
    SuppressCharacter(c, false);
}

int OrphanCombatPod(::Character* c)   /* CombatClass+0x130, the byte _isInCombatMode returns; the body is checked first (LESSON 3) */
{
    __try
    {
        if (*(void**)((char*)c + 0x648) == 0) return 0;
        void* cc = (void*)c->getCombat();
        if (cc == 0) return 0;
        return *((unsigned char*)cc + 0x130) != 0 ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int OrphanPosPod(::Character* c, float* x, float* z)
{
    __try { Ogre::Vector3 p = c->worldPosition(); *x = p.x; *z = p.z; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int OrphanPos(::Character* c, float* x, float* z)   /* worldPosition dereferences the animation object (F205) */
{
    if (!PlausibleObject(*(void**)((char*)c + 0x448))) return 0;
    return OrphanPosPod(c, x, z);
}
struct OrphanCopy { GameData* data; Faction* faction; float x, z; unsigned int uid; };
void OrphanPeerCopies(std::vector<OrphanCopy>* out)   /* T-304 (1): the other game's live copies, read once per pass that needs them */
{
    for (std::map<unsigned int, RootObjectBase*>::const_iterator it = g_spawned.begin(); it != g_spawned.end(); ++it)
    {
        if (it->second == 0 || net::IsUidMine(it->first) || IsRetiredObject(it->second) || !PlausibleObject(it->second)) continue;
        ::Character* k = (::Character*)it->second;
        OrphanCopy oc; oc.uid = it->first; oc.x = 0.0f; oc.z = 0.0f;
        if (OrphanPos(k, &oc.x, &oc.z) != 1) continue;
        oc.data = OrphanDataPod(k); oc.faction = PurgeFactionPod(k);
        if (oc.data == 0 || oc.faction == 0) continue;
        out->push_back(oc);
    }
}
void OrphanFlush(bool all)
{
    const DWORD now = ::GetTickCount();
    for (std::map<const void*, OrphanAgg>::iterator it = g_orphanAgg.begin(); it != g_orphanAgg.end(); )
    {
        if (!all && now - it->second.t0 < 3000) { ++it; continue; }
        if (g_orphanLines < kOrphanLineCap)
        {
            ++g_orphanLines;
            DebugLog("[ORPHAN] removed " + S(it->second.n) + " people of '" + it->second.faction + "' in " + S((long long)it->second.sx) + ","
                     + S((long long)it->second.sy) + ": unregistered in an area slot " + S((long long)it->second.slot)
                     + " holds (noted " + S((long long)(it->second.notedMs / 1000)) + "s) - the holder's world wins (decision 37)");
            if (g_orphanLines == kOrphanLineCap)
                DebugLog("[ORPHAN] line cap reached (" + S(kOrphanLineCap) + ") - further squads are counted on the [P033] REPORT orphan[] token only");
        }
        g_orphanAgg.erase(it++);
    }
}
}   /* namespace (orphan1) */

void OrphanNote(void* c, float x, float z)
{
    (void)x; (void)z;   /* the area is re-read when the character is judged, not trusted from the note */
    if (c == 0 || g_orphanQ.size() >= kOrphanQueueCap) return;
    g_orphanQ.insert((const void*)c);
    if (g_orphanNotedSeen.size() < 65536 && g_orphanNotedSeen.insert((const void*)c).second) ++g_orphanQueued;   /* review-orphan1 LOW: people, not re-notes */
    /* review-orphan1 MED: the continuous-noted clock. */
    const DWORD now = ::GetTickCount();
    std::map<const void*, OrphanNoteT>::iterator it = g_orphanNoteT.find((const void*)c);
    if (it == g_orphanNoteT.end())
    {
        if (g_orphanNoteT.size() >= 8192) return;
        OrphanNoteT t; t.first = now; t.last = now;
        g_orphanNoteT.insert(std::make_pair((const void*)c, t));
        return;
    }
    if (now - it->second.last > kOrphanNoteGapMs) it->second.first = now;   /* it dropped out of the noted set: the clock restarts */
    it->second.last = now;
}

void OrphanPurgeTick(long worldGen)
{
    {   /* review-orphan1 MED: the link-up clock, kept on every call, before any early return */
        const int oldUp = net::LinkIsUp() ? 1 : 0;
        const int lk = (oldUp != 0 || StoreOtherBeyondLinkInWorld() != 0) ? 1 : 0;   /* M11 C2: or another player in through the world server - one is enough while the old link has never been up */
        const long lg = net::SessionLinkGen();
        static long s_arrCursor = 0;
        static int s_oldWasUp = 0;
        const int arrived = StoreArrivalsSince(kArrServeOrphan, &s_arrCursor, 0);   /* M11 C2: every arrival restarts the clock (two games: the link-up) */
        /* [m11c2f1-2] the clock also restarts at the old link's own up edge (a new link generation included), exactly as before C2: a
           link-up merged with an earlier roster arrival counts no arrival, and the grace must not be spent before the peer's SPAWNs */
        const int oldEdge = (oldUp != 0 && (s_oldWasUp == 0 || lg != g_orphanLinkGen)) ? 1 : 0;
        if (lk != 0 && (g_orphanWasLinked == 0 || arrived > 0 || oldEdge != 0)) g_orphanLinkUpMs = ::GetTickCount();
        g_orphanWasLinked = lk; g_orphanLinkGen = lg; s_oldWasUp = oldUp;
    }
    if (worldGen != g_orphanGen)
    {
        g_orphanGen = worldGen; g_orphanQ.clear(); g_orphanSkipSeen.clear(); g_orphanNoteT.clear(); g_orphanNotedSeen.clear();
        g_orphanCarrierTried.clear(); g_orphanBedTried.clear(); g_orphanOwnDropTried.clear();
        g_orphanHeld.clear();   /* T-304 (1): the world teardown frees the gate table */
        OrphanFlush(true);
    }
    if (!g_orphanAgg.empty()) OrphanFlush(g_orphanQ.empty());
    if (g_orphanQ.empty() && g_orphanHeld.empty()) return;   /* T-304 (1): held ones are released below even with nothing queued */
    if (EngineWritesBlocked()) return;
    const DWORD now = ::GetTickCount();
    if (now - g_orphanLastMs < 200) return;
    g_orphanLastMs = now;
    for (std::map<const void*, OrphanNoteT>::iterator nt = g_orphanNoteT.begin(); nt != g_orphanNoteT.end(); )   /* stale clocks go */
    {
        if (now - nt->second.last > kOrphanNoteGapMs) g_orphanNoteT.erase(nt++); else ++nt;
    }
    if (!PlausiblePtr(coop::GameWorldPtr())) return;
    const GameHashSet<Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) { g_orphanQ.clear(); return; }
    /* Collect first: GameWorld::destroy may change the update list. A noted pointer is acted on only if it is in the
       list NOW - a note whose character is gone is dropped, never dereferenced. */
    std::vector< ::Character*> live, carriers;
    std::vector<float> px, pz;
    std::vector<int> pcombat;
    std::set<const void*> heldLive;   /* T-304 (1) */
    for (GameHashSet<Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = *it;
        if (!PlausibleObject(c)) continue;
        if (CarryFlag(c)) carriers.push_back(c);
        if (g_orphanQ.count((const void*)c) != 0) live.push_back(c);
        if (!g_orphanHeld.empty() && g_orphanHeld.count((const void*)c) != 0) heldLive.insert((const void*)c);   /* T-304 (1) */
        if (c == GetTarget() || IsPlayerFactionPod(c) == 1)
        {
            float x = 0.0f, z = 0.0f;
            if (OrphanPos(c, &x, &z) != 1) continue;
            px.push_back(x); pz.push_back(z); pcombat.push_back(OrphanCombatPod(c) == 1 ? 1 : 0);
        }
    }
    for (std::set<const void*>::iterator h = g_orphanHeld.begin(); h != g_orphanHeld.end(); )   /* T-304 (1): release what no longer waits */
    {
        const void* p = *h; ++h;
        if (heldLive.count(p) == 0) { g_orphanHeld.erase(p); ++g_orphanHoldReleased; ReleaseGateForObject(p); continue; }   /* left the update list: by address */
        if (g_orphanQ.count(p) == 0) OrphanRelease((::Character*)p);   /* no longer noted in another game's area */
    }
    g_orphanQ.clear();   /* rebuilt below from the live ones this pass did not reach; the adoption pass re-notes the rest */
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const DropCarriedObjectFn drop = (kDropCarriedObjectRva != 0) ? (DropCarriedObjectFn)(base + kDropCarriedObjectRva) : 0;
    const SetBedModeFn setBed = (kSetBedModeRva != 0) ? (SetBedModeFn)(base + kSetBedModeRva) : 0;
    const int linked = (net::LinkIsUp() || StoreOtherBeyondLinkInWorld() != 0) ? 1 : 0;   /* another player in this world: the old link, or one or more other slots IN_WORLD on the world server's roster (mparrive::OthersInWorldBeyondLink - a slot that may be the old link's peer counts alone only while that link has never been up) */
    std::set<const void*> gone;
    int removed = 0;
    std::set<std::pair<void*, void*> > t300Emptied;   /* T-300 fix 2: (faction, ActivePlatoon) of each removed person - offered to the store after the pass */
    std::vector<OrphanCopy> copies; bool copiesRead = false;   /* T-304 (1): read on first need */
    for (size_t i = 0; i < live.size(); ++i)
    {
        ::Character* c = live[i];
        if (coopor::PassAllows(removed) == 0) { g_orphanQ.insert((const void*)c); continue; }
        for (int step = 0; step < 6; ++step)   /* each step is decided from a fresh read; the tried-sets make it finish */
        {
            coopor::Seen s = coopor::SeenNone();
            s.linked = linked;
            float x = 0.0f, z = 0.0f;
            const int havePos = OrphanPos(c, &x, &z);
            const Sector sec = SectorOf(x, z);
            s.areaOther = (havePos == 1) ? HeldByOtherTS(sec) : -1;
            Faction* f = PurgeFactionPod(c);
            s.known = FindSpawnedUidForDestroy(c) != 0 ? 1 : 0;   /* review-orphan1 MED: a registered-but-retired copy is known too (P034 liveness gap) */
            s.standIn = PlausibleObject(f) ? coopsp::IsStandInRecord(StandInRecordSlot(f)) : 0;
            s.guarded = (c == GetTarget() || IsPlayerFactionPod(c) != 0) ? 1 : 0;   /* a faulted read (-1) counts as player-tied */
            s.inSomething = PoseInSomethingPod(c);
            s.beingCarried = BeingCarried(c) ? 1 : 0;
            ::Character* carrier = 0;
            if (s.beingCarried != 0)
                for (size_t j = 0; j < carriers.size(); ++j)
                    if (carriers[j] != c && gone.count((const void*)carriers[j]) == 0 && Carries(carriers[j], c)) { carrier = carriers[j]; break; }
            s.carrierFound = carrier != 0 ? 1 : 0;
            s.carrierIsPlayer = (carrier != 0 && (carrier == GetTarget() || IsPlayerFactionPod(carrier) != 0)) ? 1 : 0;
            s.carrierTried = (g_orphanCarrierTried.count((const void*)c) != 0 || drop == 0) ? 1 : 0;
            s.bedTried = (g_orphanBedTried.count((const void*)c) != 0 || setBed == 0) ? 1 : 0;
            s.carrying = CarryFlag(c) ? 1 : 0;
            s.ownDropTried = (g_orphanOwnDropTried.count((const void*)c) != 0 || drop == 0) ? 1 : 0;
            if (havePos == 1 && !px.empty())
            {
                const int mineFight = OrphanCombatPod(c) == 1 ? 1 : 0;
                for (size_t j = 0; j < px.size(); ++j)
                    if (coopor::FightNear(px[j] - x, pz[j] - z, kOrphanFightR, mineFight, pcombat[j]) != 0) { s.fightNear = 1; break; }
            }
            if (havePos == 1)
                for (size_t j = 0; j < px.size(); ++j)
                    if (coopor::NearPlayer(px[j] - x, pz[j] - z, kOrphanNearR) != 0) { s.nearPlayer = 1; break; }
            unsigned int dupUid = 0;   /* T-304 (1) */
            if (s.nearPlayer != 0 && havePos == 1 && s.areaOther == 1 && s.known == 0 && PlausibleObject(f))
            {
                if (!copiesRead) { OrphanPeerCopies(&copies); copiesRead = true; }
                GameData* gd = OrphanDataPod(c);
                for (size_t k = 0; gd != 0 && k < copies.size(); ++k)
                    if (copies[k].data == gd && copies[k].faction == f && coopor::DupNear(copies[k].x - x, copies[k].z - z) != 0)
                    { s.dupOfLiveCopy = 1; dupUid = copies[k].uid; break; }
            }
            {
                std::map<const void*, OrphanNoteT>::const_iterator nt = g_orphanNoteT.find((const void*)c);
                s.notedMs = (nt != g_orphanNoteT.end() && now - nt->second.last <= kOrphanNoteGapMs) ? (unsigned long)(now - nt->second.first) : 0;
                s.linkedMs = linked != 0 ? (unsigned long)(now - g_orphanLinkUpMs) : 0;
            }
            int why = coopor::kWhyNone;
            const int act = coopor::Decide(s, &why);
            if (act == coopor::kActNone) { OrphanRelease(c); break; }   /* T-304 (1): not an orphan now - its decisions are its own again */
            if (act == coopor::kActSkip || act == coopor::kActLater)
            {
                if (coopor::HoldWhilePending(act, why) != 0) OrphanHold(c); else OrphanRelease(c);   /* T-304 (1), fold 1: only while the link or the noted clock is too fresh */
                if (why > 0 && why < coopor::kWhyCount && g_orphanSkipSeen.size() < 65536
                    && g_orphanSkipSeen.insert(std::make_pair((const void*)c, why)).second) { ++g_orphanSkipped; ++g_orphanSkipWhy[why]; }
                break;
            }
            if (act == coopor::kActDropCarrier) { g_orphanCarrierTried.insert((const void*)c); PurgeDropPod(drop, carrier); continue; }
            if (act == coopor::kActBedOut) { g_orphanBedTried.insert((const void*)c); PoseBedPod(setBed, c, false, 0); continue; }
            if (act == coopor::kActDropOwn) { g_orphanOwnDropTried.insert((const void*)c); PurgeDropPod(drop, c); continue; }
            /* coopor::kActDestroy - the stand-in purge's route (DropPeerOwnedCopy's step 3); no row, no puppet, no uid */
            void* pl = PurgePlatoonPod(c);
            const std::string fname = PlausibleObject(f) ? WireFactionName(f) : std::string("?");
            int owner = -2; double age = -1.0;
            AreaProbeTS(sec, &owner, &age);
            const void* prevSelf = g_selfDespawnObj;
            g_selfDespawnObj = c;
            const int r = PurgeDestroyPod(c);
            g_selfDespawnObj = prevSelf;
            ++removed;
            if (r == 1)
            {
                gone.insert((const void*)c); ++g_orphanRemoved;
                if (g_orphanHeld.erase((const void*)c) != 0) ReleaseGateForObject((const void*)c);   /* T-304 (1): by address - the object is gone */
                if (s.dupOfLiveCopy != 0)
                {
                    ++g_orphanDupRemoved;
                    DebugLog("[ORPHAN] T-304: removed an unregistered duplicate of the other game's copy uid=" + S(dupUid) + " ('" + fname
                             + "', same template and faction within " + S((long long)coopor::kDupCopyR) + " units) although a player stood within "
                             + S((long long)kOrphanNearR) + " units (orphanDupRemoved=" + S(g_orphanDupRemoved) + ")");
                }
                if (pl != 0 && PlausibleObject(f)) t300Emptied.insert(std::make_pair((void*)f, pl));   /* T-300 fix 2 */
                const void* key = pl != 0 ? (const void*)pl : (const void*)f;
                std::map<const void*, OrphanAgg>::iterator ag = g_orphanAgg.find(key);
                if (ag == g_orphanAgg.end())
                {
                    OrphanAgg a; a.faction = fname; a.sx = sec.x; a.sy = sec.y; a.slot = owner; a.n = 0; a.t0 = now; a.notedMs = 0;
                    ag = g_orphanAgg.insert(std::make_pair(key, a)).first;
                }
                ++ag->second.n;
                if (s.notedMs > ag->second.notedMs) ag->second.notedMs = s.notedMs;
            }
            else
            {
                ++g_orphanFaulted;
                ErrorLog(std::string("[ORPHAN] GameWorld::destroy ") + (r < 0 ? "FAULTED" : "was refused (no hook / no world)")
                         + " for a character of '" + fname + "' in " + S((long long)sec.x) + "," + S((long long)sec.y) + " - left standing");
            }
            break;
        }
    }
    /* T-300 fix 2: the purge removes people, never their squad. A squad that is another game's file-built squad in an area
       another game holds is thrown away by the store once it is empty, so the next file cannot wake and refill it. */
    for (std::set<std::pair<void*, void*> >::const_iterator ep = t300Emptied.begin(); ep != t300Emptied.end(); ++ep) StoreOrphanEmptiedSquad(ep->first, ep->second);
}

void OrphanReportNumbers(long long* removed, long long* skipped)
{
    if (removed != 0) *removed = g_orphanRemoved;
    if (skipped != 0) *skipped = g_orphanSkipped;
}

std::string OrphanReportDetail()
{
    std::string t = " orphanSkip[";
    for (int w = 1; w < coopor::kWhyCount; ++w) { if (w > 1) t += ","; t += coopor::WhyToken(w); }
    t += "]=";
    for (int w = 1; w < coopor::kWhyCount; ++w) { if (w > 1) t += ","; t += S(g_orphanSkipWhy[w]); }
    t += " orphan[faulted,noted,pending]=" + S(g_orphanFaulted) + "," + S(g_orphanQueued) + "," + S((long long)g_orphanQ.size());
    t += " orphanT304[dupRemoved,held,released,holdFull,heldNow,holdRefused,holdNotOurs]=" + S(g_orphanDupRemoved) + "," + S(g_orphanHoldN)
         + "," + S(g_orphanHoldReleased) + "," + S(g_orphanHoldFull) + "," + S((long long)g_orphanHeld.size())
         + "," + S(g_orphanHoldRefused) + "," + S(g_orphanHoldNotOurs);
    return t;
}

/* TEST-ONLY lever (command file only - `bedtest`, command_channel.cpp). MAIN THREAD (the command pump, as CarryTestCommand).
   bedtest <carrierUid> <bodyUid | name <name, '_' = a space>> [bedKeySubstring] [type=<n>]: this game's OWN character
   <carrierUid>, already carrying the body (e.g. after `capturetest carry`), is given the engine's own order to put the carried
   person in a bed - issueOrder(task type, the bed's hand, the bed's spot) on its order system, the call replicate.cpp InjectOrder
   makes for a player's order, with the bed's hand as the target. The bed: the nearest one within kBedTestRadius of the carrier
   whose name holds "bed" (and whose key holds bedKeySubstring when given) and whose isFreeSlot takes the body. Default task type 99
   PUT_SOMEONE_IN_BED: the type the engine's own right-click on a bed ('Put in Bed') gives a character carrying someone
   (addOrderSelectedCharacters 0x7F9280 -> Character issueOrder 0x5D1640 -> issueOrder with the bed's hand); the carried body is
   not in the order, the engine's task takes it from the carrier. type=<n> gives another (70 makes the engine's blank task,
   98 USE_BED is the carrier's own sleep). The rest is the engine's: its Task_PutInSomething puts the copy in the bed, and
   this game's bed watch (BedWatchCopy) reports the put-down to the body's own game.
   bedtest list [radius] [nearUid]: reads only - the beds near nearUid (default: the first own character with a readable
   position): key, used / max slots, position, distance. A bed is a loaded building, or an interior piece of one (a house's bed),
   whose name holds "bed". */
namespace {
const float  kBedTestRadius      = 600.0f;
const int    kBedTestListCap     = 40;
const int    kBedTestDefaultType = 99;      // PUT_SOMEONE_IN_BED - the engine's right-click 'Put in Bed' order for a carrier
const size_t kBedTestPosOff      = 0x48;    // RootObject : position (the plain read the key builder's podPos 1 makes)
const size_t kUseableMaxOpsOff   = 0x3AC;   // UseableStuff : numOperatorsMax
const size_t kUseableOpsSizeOff  = 0x3E0;   // UseableStuff : the occupant set's size
struct BedTestRow { void* bed; std::string key; std::string name; float pos[3]; int maxOps; int used; float d; };

GameData* BedTestDataPod(void* obj)
{
    __try { return ((::Character*)obj)->getRecordDirect(); }   // RootObject's own virtual, the same slot for a building
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int BedTestPosPod(const void* obj, float* pos)
{
    __try
    {
        const float* p = (const float*)((const char*)obj + kBedTestPosOff);
        pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BedTestSlotsPod(const void* bed, int* maxOps, int* used)
{
    __try
    {
        *maxOps = *(const int*)((const char*)bed + kUseableMaxOpsOff);
        *used = (int)*(const size_t*)((const char*)bed + kUseableOpsSizeOff);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string BedTestLower(const std::string& in)
{
    std::string out(in);
    for (size_t i = 0; i < out.size(); ++i) if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    return out;
}
void BedTestConsider(void* obj, const float* from, float r, std::vector<BedTestRow>* rows)
{
    if (obj == 0 || !PlausibleObject(obj) || rows->size() >= 256) return;
    for (size_t i = 0; i < rows->size(); ++i) if ((*rows)[i].bed == obj) return;
    char key[coopstate::kPoseStrMax + 1];
    if (ObjectPositionKey(obj, key, (int)sizeof(key), 0, 3, 1) == 0) return;   // the key a BED IN would carry (PoseBedKeyFromHand)
    // A bed is named like one ("Bed", "Old Bed", "Camp Bed"...): the key is "<record>@<sector>@<position>" and carries no name.
    GameData* gd = BedTestDataPod(obj);
    if (!PlausibleObject(gd)) return;
    const std::string name = gd->name;
    if (BedTestLower(name).find("bed") == std::string::npos) return;
    BedTestRow row; row.bed = obj; row.key = key; row.name = name; row.maxOps = -1; row.used = -1; row.d = 0.0f;
    if (BedTestPosPod(obj, row.pos) == 0) return;
    const float dx = row.pos[0] - from[0], dz = row.pos[2] - from[2];
    row.d = sqrtf(dx * dx + dz * dz);
    if (row.d > r) return;
    BedTestSlotsPod(obj, &row.maxOps, &row.used);
    rows->push_back(row);
}
// the loaded buildings near `from` and the interior pieces of each, nearest first; false = a zone was unloaded during the walk
bool BedTestCollect(const float* from, float r, std::vector<BedTestRow>* rows)
{
    static void* blds[4096];
    static unsigned int ids[kPoseLayoutCap * 5];
    const long long gen0 = BoxWalkGen();
    const int n = LoadedBuildings(blds, 4096);
    for (int i = 0; i < n; ++i)
    {
        void* b = blds[i];
        float bp[3];
        if (b == 0 || !PlausibleObject(b) || BedTestPosPod(b, bp) == 0) continue;
        const float dx = bp[0] - from[0], dz = bp[2] - from[2];
        if (sqrtf(dx * dx + dz * dz) > r + 500.0f) continue;   // a house's pieces lie near its own position
        BedTestConsider(b, from, r, rows);
        int listed = 0, trunc = 0, torn = 0;
        const int np = InteriorPieceIds(b, ids, kPoseLayoutCap, &listed, &trunc, &torn);
        if (torn != 0 || BoxWalkGen() != gen0) { rows->clear(); return false; }
        for (int k = 0; k < np; ++k)
        {
            const unsigned int* f = ids + k * 5;
            if (f[3] == 0 && f[4] == 0) continue;
            hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
            BedTestConsider((void*)h.asBuilding(), from, r, rows);
        }
    }
    if (BoxWalkGen() != gen0) { rows->clear(); return false; }
    for (size_t i = 1; i < rows->size(); ++i)   // nearest first
        for (size_t j = i; j > 0 && (*rows)[j].d < (*rows)[j - 1].d; --j) { const BedTestRow t = (*rows)[j]; (*rows)[j] = (*rows)[j - 1]; (*rows)[j - 1] = t; }
    return true;
}
std::string BedTestRowText(const BedTestRow& w)
{
    return "key='" + w.key + "' name='" + w.name + "' used=" + S((long long)w.used) + " max=" + S((long long)w.maxOps) + " pos=" + F1(w.pos[0]) + ","
         + F1(w.pos[1]) + "," + F1(w.pos[2]) + " dist=" + F1(w.d);
}
}   // namespace (bedtest)

std::string BedTestCommand(const std::string& arg)
{
    const std::string usage = "error bedtest: usage bedtest <carrierUid> <bodyUid | name <name, '_' = a space>> [bedKeySubstring] [type=<n>] | bedtest list [radius] [nearUid]";
    if (EngineWritesBlocked()) return "error bedtest: engine writes are blocked - try again";
    std::istringstream is(arg);
    std::string word;
    if (!(is >> word)) return usage;
    if (word == "list")
    {
        float r = kBedTestRadius;
        unsigned int nearUid = 0;
        std::string tok;
        if (is >> tok) { std::istringstream rs(tok); float v = 0.0f; if ((rs >> v) && v > 0.0f && v <= 20000.0f) r = v; }
        if (is >> tok) { std::istringstream us(tok); unsigned int v = 0; if (us >> v) nearUid = v; }
        ::Character* nc = 0;
        Ogre::Vector3 at;
        if (nearUid != 0) { nc = FindSpawned(nearUid); if (nc != 0 && (!PlausibleObject(nc) || !SafeReadPosition(nc, &at))) nc = 0; }
        else
            for (int i = 0; i < MirrorCapacity() && nc == 0; ++i)
            {
                unsigned int u = 0; ::Character* ch = 0;
                if (MirrorSlot(i, &u, &ch) && u != 0 && net::IsUidMine(u) && PlausibleObject(ch) && SafeReadPosition(ch, &at)) { nc = ch; nearUid = u; }
            }
        if (nc == 0) return "error bedtest list: no reference character with a readable position here";
        const float from[3] = { at.x, at.y, at.z };
        std::vector<BedTestRow> rows;
        if (!BedTestCollect(from, r, &rows)) return "error bedtest list: a zone was unloaded during the walk - try again";
        DebugLog("[BED] bedtest list near uid=" + S((long long)nearUid) + " radius=" + F1(r) + " beds=" + S((long long)rows.size())
                 + " (reads only)");
        for (size_t i = 0; i < rows.size() && i < (size_t)kBedTestListCap; ++i) DebugLog("[BED] bedtest list bed " + BedTestRowText(rows[i]));
        return "ok bedtest list beds=" + S((long long)rows.size());
    }
    std::istringstream cs(word);
    unsigned int carrier = 0, body = 0;
    if (!(cs >> carrier) || carrier == 0) return usage;
    std::string tok, bodyName, sub;
    if (!(is >> tok)) return usage;
    if (tok == "name") { if (!(is >> bodyName)) return usage; }
    else { std::istringstream bs(tok); if (!(bs >> body) || body == 0) return usage; }
    int type = kBedTestDefaultType;
    while (is >> tok)
    {
        if (tok.compare(0, 5, "type=") == 0) { std::istringstream ts(tok.substr(5)); if (!(ts >> type)) return usage; }
        else sub = BedTestLower(tok);
    }
    if (type <= 0 || type > 1000) return "error bedtest: type " + S((long long)type) + " out of range";
    if (!net::IsUidMine(carrier)) return "error bedtest: carrier uid " + S((long long)carrier) + " is not this game's own character";
    ::Character* cc = FindSpawned(carrier);
    if (cc == 0 || !PlausibleObject(cc)) return "error bedtest: the carrier is not here";
    if (!CarryFlag(cc)) return "error bedtest: the carrier carries nobody (capturetest carry first)";
    const unsigned int carried = CarryingUidOf(carrier);
    if (!bodyName.empty())
    {
        for (size_t i = 0; i < bodyName.size(); ++i) if (bodyName[i] == '_') bodyName[i] = ' ';
        float bd = -1.0f;
        std::string bn, why;
        if (LeverNearestNamed(coopsay::TalkPersonKey(bodyName), kLeverPickAny, cc, 50.0f, &body, &bd, &bn, &why) == 0)
            return "error bedtest name: " + why;
    }
    if (body == 0 || carried != body)
        return "error bedtest: the carrier carries uid " + S((long long)carried) + ", not uid " + S((long long)body);
    ::Character* bc = FindSpawned(body);
    if (bc == 0 || !PlausibleObject(bc)) return "error bedtest: the body is not here";
    Ogre::Vector3 at;
    if (!SafeReadPosition(cc, &at)) return "error bedtest: the carrier's position is unreadable";
    const float from[3] = { at.x, at.y, at.z };
    std::vector<BedTestRow> rows;
    if (!BedTestCollect(from, kBedTestRadius, &rows)) return "error bedtest: a zone was unloaded during the walk - try again";
    int pick = -1;
    for (size_t i = 0; i < rows.size() && pick < 0; ++i)
    {
        if (!sub.empty() && BedTestLower(rows[i].key).find(sub) == std::string::npos) continue;
        if (PrisonFreeSlotPod(rows[i].bed, bc) == 1) pick = (int)i;   // the engine's own slot test for this body
    }
    if (pick < 0)
    {
        DebugLog("[BED] bedtest REFUSED carrier=" + S((long long)carrier) + " body=" + S((long long)body) + ": no bed with a free slot"
                 + (sub.empty() ? std::string() : " matching '" + sub + "'") + " within " + F1(kBedTestRadius) + " (beds seen " + S((long long)rows.size()) + ")");
        return "error bedtest: no free bed near the carrier";
    }
    const BedTestRow& row = rows[(size_t)pick];
    void* ai = GetCharacterAI(cc);
    if (!PlausibleObject(ai)) return "error bedtest: the carrier has no AI";
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObject(orders)) return "error bedtest: the carrier has no order system";
    const hand& bh = *(const hand*)((const char*)row.bed + kCharHandOff);   // RootObjectBase::getHandle is this+0x58 (F099)
    orders->issueOrder((TaskType)type, bh, Ogre::Vector3(row.pos[0], row.pos[1], row.pos[2]), true, false);
    const std::string line = "[BED] bedtest order carrier=" + S((long long)carrier) + " body=" + S((long long)body) + " bodyOwn="
        + S((long long)(net::IsUidMine(body) ? 1 : 0)) + " type=" + S((long long)type) + " target=bed hand (bed+0x58), the bed's position; body from the carrier" + " bed " + BedTestRowText(row)
        + " (TEST-ONLY: the engine's own put-in-bed order on this game's carrier; the bed watch reports the put-down to the body's game)";
    DebugLog(line);
    return "ok " + line.substr(6);
}

// The safe point: own characters a copy carrier is picking up out of a bed leave it (setBedMode off); the carry code then
// sees them lying free and the pick-up proceeds. One that changed since it was queued (awake, caged, gone) is left alone.
void BedOutForCarryDrain()
{
    if (g_bedOutForCarry.empty()) return;
    if (EngineWritesBlocked() || kSetBedModeRva == 0) { g_bedOutForCarry.clear(); return; }
    const SetBedModeFn setBed = (SetBedModeFn)((uintptr_t)::GetModuleHandleA(0) + kSetBedModeRva);
    for (std::set<unsigned int>::const_iterator u = g_bedOutForCarry.begin(); u != g_bedOutForCarry.end(); ++u)
    {
        ::Character* c = FindSpawned(*u);
        if (c == 0 || !PlausibleObject(c) || !net::IsUidMine(*u) || !OwnBodyInBedMayBeCarried(c)) continue;
        if (PoseBedPod(setBed, c, false, 0) == 1 && PoseInSomethingPod(c) == 0)
        {
            ++g_bedOutForCarryDone;
            g_bedOutForCarryFails.erase(*u);
            PrisonLog("BED uid=" + S((long long)*u) + " taken out of its bed here: another game's character is picking it up");
            StatePush(*u);
        }
        else
        {
            ++g_bedOutForCarryFailed;
            const DWORD now = ::GetTickCount();
            std::map<unsigned int, BedOutFails>::iterator f = g_bedOutForCarryFails.find(*u);
            if (f == g_bedOutForCarryFails.end() || now - f->second.lastAt >= kBedOutForCarryForgetMs)
            {
                BedOutFails z; z.n = 0; z.lastAt = 0; z.counted = false;
                g_bedOutForCarryFails[*u] = z;
                f = g_bedOutForCarryFails.find(*u);
            }
            f->second.lastAt = now | 1u;
            if (++f->second.n == kBedOutForCarryTries)
                PrisonLog("BED uid=" + S((long long)*u) + " could not be taken out of its bed " + S((long long)kBedOutForCarryTries)
                          + " times - the carry is broken instead");
        }
    }
    g_bedOutForCarry.clear();
}

// The owner's bed hand-off, stage by stage (the engine's bed order is getDropped, setBedMode, the move, stopAction -
// Task_PutInSomething 0x358610 bed branch, decomp_358610.txt:135-167). Stage 1 (PrisonSafePointDrain): checked, the slot
// checked, getDropped posted. Stage 2, at a LATER safe point once the ragdoll request is carried out and the body is no
// ragdoll: checked again, setBedMode, the move to the rescuer's spot, stopAction, the spot checked. Stage 3, kBedRecheckMs later:
// still in the bed on the spot - only then counted applied. A body that does not settle in time, or slid off, is taken out
// and refused for now (the rescuer may ask again).
bool BedHandoffOnSpot(::Character* c, const PrisonOp& op, float* distOut)
{
    *distOut = -1.0f;
    Ogre::Vector3 at;
    if (!SafeReadPosition(c, &at)) return false;
    const float dx = at.x - op.pos[0], dz = at.z - op.pos[2];
    *distOut = sqrtf(dx * dx + dz * dz);
    return PoseInSomethingPod(c) == 1 && *distOut <= kBedSpotTolerance;
}

void BedHandoffStart(const PrisonOp& op, ::Character* c)
{
    for (size_t i = 0; i < g_bedHandoffs.size(); ++i)
        if (g_bedHandoffs[i].op.uid == op.uid)   // only a stage-1 one can be here (stage 2 is in a bed, which refuses the new op)
        {
            const PrisonOp old = g_bedHandoffs[i].op;
            g_bedHandoffs.erase(g_bedHandoffs.begin() + i);
            ++g_bedHandoffSuperseded;
            BedSendRefused(old.uid, old.peer, std::string(old.bedKey), false, "a later request replaced it");
            break;
        }
    if (g_bedHandoffs.size() >= kBedHandoffCap)
    { ++g_bedHandoffFull; BedSendRefused(op.uid, op.peer, std::string(op.bedKey), false, "too many bed hand-offs waiting here"); return; }
    if (BedGetDroppedPod(c) < 0) ++g_poseBedFaulted;
    BedHandoff h; h.op = op; h.stage = 1; h.safePoints = 0; h.since = ::GetTickCount() | 1u;
    g_bedHandoffs.push_back(h);
    ++g_bedDropPosted;
}

// stage 2: true = in the bed on the spot (stage 3 follows), false = refused (finished)
bool BedHandoffPutIn(BedHandoff& h, ::Character* c, SetBedModeFn setBed, uintptr_t base, DWORD now)
{
    const PrisonOp& op = h.op;
    const std::string bedKey(op.bedKey);
    if (!PlausibleObject(op.cage) || PoseInSomethingPod(c) != 0 || BeingCarried(c) || !OwnBodyMayBeCarried(c))
    { BedSendRefused(op.uid, op.peer, bedKey, false, "no longer free to put in a bed"); return false; }
    const int room = PrisonFreeSlotPod(op.cage, c);
    if (room != 1) { BedSendRefused(op.uid, op.peer, bedKey, room == 0, room == 0 ? "the bed has no free slot" : "the bed's slot was unreadable"); return false; }   // full: for good
    const int r = PoseBedPod(setBed, c, true, op.cage);
    if (r != 1 || PoseInSomethingPod(c) != 1 || PrisonHandSetPod(c) != 1)
    {
        if (r == 1 && PoseInSomethingPod(c) == 1) PoseBedPod(setBed, c, false, 0);   // in the bed mode with no bed: undone
        BedSendRefused(op.uid, op.peer, bedKey, true, "setBedMode did not take");
        return false;
    }
    const bool moved = TeleportCharacter(c, op.pos[0], op.pos[1], op.pos[2]);
    if (kStopActionRva != 0 && PoseStopPod((StopActionFn)(base + kStopActionRva), c) < 0) ++g_poseBedFaulted;
    // still in the bed and on the rescuer's spot after the move - otherwise undone and refused, so neither game shows the
    // character in a bed the other does not
    float dist = -1.0f;
    if (!moved || !BedHandoffOnSpot(c, op, &dist))
    {
        if (PoseInSomethingPod(c) == 1) PoseBedPod(setBed, c, false, 0);
        ++g_bedNotAtSpot;
        BedSendRefused(op.uid, op.peer, bedKey, false, ("not in the bed on the rescuer's spot after the move (moved=" + std::string(moved ? "yes" : "no")
                       + " distance=" + S((long long)dist) + ")").c_str());
        return false;
    }
    h.stage = 2; h.since = now | 1u;
    return true;
}

// stage 3: the second look
void BedHandoffConfirm(const BedHandoff& h, ::Character* c, SetBedModeFn setBed)
{
    const PrisonOp& op = h.op;
    float dist = -1.0f;
    if (!BedHandoffOnSpot(c, op, &dist))
    {
        if (PoseInSomethingPod(c) == 1) PoseBedPod(setBed, c, false, 0);
        ++g_bedSlidAway;
        BedSendRefused(op.uid, op.peer, std::string(op.bedKey), false, ("out of the bed or off the rescuer's spot a second after it was put there (distance "
                       + S((long long)dist) + ")").c_str());
        return;
    }
    ++g_bedApplied;
    PrisonLog("<- BED uid=" + S((long long)op.uid) + " put in the bed on the rescuer's spot (distance " + S((long long)dist)
              + ", still there a second later, " + S((long long)h.safePoints) + " safe points waited for the put-down) - the other game's rescuer put its copy there");
    StatePush(op.uid);   // a STATE at once; the bed itself travels in the next POSE push
}

void BedHandoffDrain()
{
    if (g_bedHandoffs.empty()) return;
    if (EngineWritesBlocked() || kSetBedModeRva == 0) { g_bedHandoffs.clear(); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const SetBedModeFn setBed = (SetBedModeFn)(base + kSetBedModeRva);
    const DWORD now = ::GetTickCount();
    for (size_t i = 0; i < g_bedHandoffs.size(); )
    {
        BedHandoff& h = g_bedHandoffs[i];
        const std::string bedKey(h.op.bedKey);
        ::Character* c = FindSpawned(h.op.uid);
        bool finished = true;
        if (c == 0 || !PlausibleObject(c) || !net::IsUidMine(h.op.uid))
            BedSendRefused(h.op.uid, h.op.peer, bedKey, false, "gone before the bed hand-off ended");
        else if (c->hasDied())
        {
            if (h.stage == 2 && PoseInSomethingPod(c) == 1) PoseBedPod(setBed, c, false, 0);
            BedSendRefused(h.op.uid, h.op.peer, bedKey, true, "dead");
        }
        else if (h.stage == 1)
        {
            ++h.safePoints;
            const int pend = BedDropPendingPod(c);
            if (pend != 0 || RagdollActive(c))
            {
                ++g_bedDropWaited;
                if (h.safePoints < kBedDropWaitSafePoints || now - h.since < kBedDropWaitMs) finished = false;
                else
                {
                    if (pend != 0) ++g_bedDropNotConsumed; else ++g_bedDropStillRagdoll;
                    BedSendRefused(h.op.uid, h.op.peer, bedKey, false, (std::string(pend != 0 ? "the put-down request was not carried out"
                                   : "still a ragdoll after the put-down") + " within " + S((long long)h.safePoints) + " safe points").c_str());
                }
            }
            else finished = !BedHandoffPutIn(h, c, setBed, base, now);
        }
        else if (now - h.since < kBedRecheckMs) finished = false;
        else BedHandoffConfirm(h, c, setBed);
        if (finished) g_bedHandoffs.erase(g_bedHandoffs.begin() + i); else ++i;
    }
}

/* P43 TEST-ONLY lever `cagetest <copyUid> nearest`: this game's engine puts the other game's character's COPY into the nearest free
   cage the way a guard's Task_PutInSomething 0x358610 does - setPrisonMode 0x3305C0 (row SetPrisonMode), the cage's lock, and a
   sentence only when the copy has a bounty with the cage's faction (PrisonSentencePod with no verdict = the task's own bounty
   test). The command only lists the cage candidates (items.cpp CageCandidatesNear) and queues; CageTestSafePointDrain makes the
   engine calls at the K2 safe point with the AI worker paused, asking each cage's isFreeSlot first, nearest first. Nothing here
   tells the owner: PrisonWatchCopies finds a copy in a cage that no cage POSE of its owner put there and sends MSG_PRISON IN with
   this engine's bounty verdict - the message a guard's caging sends. */
namespace {
const int kCageTestCand = 8;
struct CageTestPend { unsigned int uid; int n; void* cand[kCageTestCand]; float dist[kCageTestCand]; std::string key[kCageTestCand]; std::string name[kCageTestCand]; };
std::vector<CageTestPend> g_cageTestPend;

void CageTestSafePointDrain()
{
    if (g_cageTestPend.empty()) return;
    if (EngineWritesBlocked())
    {
        for (size_t i = 0; i < g_cageTestPend.size(); ++i)
            DebugLog("[ARREST] cagetest uid=" + S((long long)g_cageTestPend[i].uid) + " cage='' ok=0 - engine writes are blocked (a world is loading or closing)");
        g_cageTestPend.clear();
        return;
    }
    for (size_t i = 0; i < g_cageTestPend.size(); ++i)
    {
        const CageTestPend& p = g_cageTestPend[i];
        ::Character* c = FindSpawned(p.uid);
        std::string why, rooms;
        int pick = -1;
        if (c == 0 || !PlausibleObject(c) || net::IsUidMine(p.uid)) why = "the copy is gone or is this game's own now";
        else if (PoseInSomethingPod(c) != 0 || BeingCarried(c)) why = "the copy is in a bed or cage already, or carried";
        else
            for (int k = 0; k < p.n; ++k)
            {
                const int r = PlausibleObject(p.cand[k]) ? PrisonFreeSlotPod(p.cand[k], c) : -2;
                rooms += (k != 0 ? "," : "") + S((long long)r);
                if (r == 1) { pick = k; break; }
            }
        if (why.empty() && pick < 0) why = "no candidate cage has a free slot (isFreeSlot nearest first: " + rooms + "; -2 = the cage is gone)";
        if (!why.empty()) { DebugLog("[ARREST] cagetest uid=" + S((long long)p.uid) + " cage='' ok=0 - " + why); continue; }
        void* cage = p.cand[pick];
        const int r = PosePrisonPod(c, true, cage);
        const int ins = PoseInSomethingPod(c);
        const int hd = PrisonHandSetPod(c);
        const bool ok = (r == 1 && ins == 2 && hd == 1);
        int lk = 0, sn = 0;
        float hours = -1.0f;
        if (ok)
        {
            lk = PrisonLockPod(cage);
            sn = PrisonSentencePod(c, cage, &hours, cooprison::kPrisonSentenceUnknown);
        }
        DebugLog("[ARREST] cagetest uid=" + S((long long)p.uid) + " cage='" + p.key[pick] + "' ok=" + S((long long)(ok ? 1 : 0))
                 + " name='" + p.name[pick] + "' dist=" + F1(p.dist[pick]) + " setPrisonMode=" + S((long long)r) + " inSomething=" + S((long long)ins)
                 + " cageHand=" + S((long long)hd) + " lock=" + S((long long)lk) + " sentence=" + S((long long)sn) + " hours=" + F1(hours)
                 + " isFreeSlot=" + rooms
                 + (ok ? " - this engine caged the copy; PrisonWatchCopies tells its owner (MSG_PRISON IN, this engine's bounty verdict)"
                       : " - NOT caged (setPrisonMode 1 called, -1 faulted, 0 no address; inSomething 2 = caged; cageHand 1 = the cage took it)"));
    }
    g_cageTestPend.clear();
}
}   // namespace (cagetest)

std::string CageTestCommand(const std::string& arg)
{
    unsigned int uid = 0;
    if (!joblever::CageTestParse(arg, &uid)) return "error cagetest: usage cagetest <copyUid> nearest";
    if (EngineWritesBlocked()) return "error cagetest: engine writes are blocked - try again";
    if (kSetPrisonModeRva == 0) return "error cagetest: SetPrisonMode is not in the address table";
    if (net::IsUidMine(uid)) return "error cagetest: uid " + S((long long)uid) + " is this game's own - the lever cages the other game's character's copy";
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return "error cagetest: uid " + S((long long)uid) + " is not here";
    if (c->hasDied()) return "error cagetest: uid " + S((long long)uid) + " is dead";
    if (PoseInSomethingPod(c) != 0) return "error cagetest: the copy is in a bed or cage already";
    if (BeingCarried(c)) return "error cagetest: the copy is carried - put it down first";
    if (g_cageTestPend.size() >= 8) return "error cagetest: 8 already wait for the safe point";
    const Ogre::Vector3 pos = c->worldPosition();
    const float from[3] = { pos.x, pos.y, pos.z };
    CageTestPend p;
    p.uid = uid;
    long long scanned = 0, named = 0;
    std::string otherClass;
    p.n = CageCandidatesNear(from, p.cand, p.dist, p.key, p.name, kCageTestCand, &scanned, &named, &otherClass);
    const std::string counts = " buildingsScanned=" + S(scanned) + " namedCage=" + S(named)
        + (otherClass.empty() ? std::string() : " firstNamedNotUseable='" + otherClass + "'");
    if (p.n <= 0)
    {
        const std::string miss = "[ARREST] cagetest uid=" + S((long long)uid) + " cage='' ok=0 - no loaded cage (a building named like a cage, of the UseableStuff class)" + counts;
        DebugLog(miss);
        return "error " + miss.substr(9);
    }
    g_cageTestPend.push_back(p);
    const std::string line = "[ARREST] cagetest uid=" + S((long long)uid) + " queued candidates=" + S((long long)p.n) + " nearest='" + p.key[0]
        + "' nearestName='" + p.name[0] + "' dist=" + F1(p.dist[0]) + counts + " - caged at the next safe point (TEST-ONLY)";
    DebugLog(line);
    return "ok " + line.substr(9);
}

void PrisonSafePointDrain()
{
    StandinPurgeCageOutDrain();   /* inv7a: a stand-in leaving its cage before the purge destroys it */
    BedOutForCarryDrain();
    BedHandoffDrain();   // hand-offs queued at an earlier safe point - a new one below waits for the next
    CageTestSafePointDrain();     /* cagetest: the lever's caging, at this same safe point */
    RestraintSafePointDrain();    /* P42: restraint lock writes (a copy taking its owner's word, the owner opening on a request) */
    if (g_prisonOps.empty()) return;
    if (EngineWritesBlocked()) { g_prisonOps.clear(); return; }
    for (size_t i = 0; i < g_prisonOps.size(); ++i)
    {
        const PrisonOp& op = g_prisonOps[i];
        ::Character* c = FindSpawned(op.uid);
        if (op.bed)   // the bed hand-off: our own character into the bed another game's rescuer used, then onto its spot
        {
            const std::string bedKey(op.bedKey);
            if (c == 0 || !PlausibleObject(c) || !net::IsUidMine(op.uid)) { BedSendRefused(op.uid, op.peer, bedKey, false, "gone before the safe point"); continue; }
            if (c->hasDied()) { BedSendRefused(op.uid, op.peer, bedKey, true, "dead"); continue; }   // it may have died since BedApplyOwn
            if (kSetBedModeRva == 0 || !PlausibleObject(op.cage) || PoseInSomethingPod(c) != 0 || BeingCarried(c) || !OwnBodyMayBeCarried(c))
            { BedSendRefused(op.uid, op.peer, bedKey, false, "no longer free to put in a bed"); continue; }
            const int room = PrisonFreeSlotPod(op.cage, c);
            if (room != 1) { BedSendRefused(op.uid, op.peer, bedKey, room == 0, room == 0 ? "the bed has no free slot" : "the bed's slot was unreadable"); continue; }   // full: for good
            // The engine's bed order (Task_PutInSomething 0x358610 bed branch) is getDropped, setBedMode, the move, stopAction;
            // getDropped only posts a ragdoll request the body's own update carries out later, so here it runs alone and the
            // rest follows at a later safe point (BedHandoffDrain).
            BedHandoffStart(op, c);
            continue;
        }
        if (c == 0 || !PlausibleObject(c)) { if (op.own) ++g_prisonFailed; continue; }
        if (!op.on)
        {
            if (op.release && op.own)   // arrest3: the guard's game released its copy - release our own character the same way
            {
                void* cage = PrisonCageOf(c);
                const double nowH = LocalWorldHours();
                if (nowH < 0.0) ++g_releaseNoClock;
                const int r = cage != 0 ? PrisonReleasePod(c, cage, nowH < 0.0 ? -1.0 : nowH + kPardonHours, op.relFac, op.relMode) : 0;
                if (r == 1 && PoseInSomethingPod(c) == 0)
                {
                    ++g_prisonReleased;
                    PrisonLog("<- PRISON uid=" + S((long long)op.uid) + " RELEASED (releaser " + std::string(op.relMode == 2 ? "a player - no bounty cleared" : (op.relMode == 1 ? "named" : "unknown - cage faction"))
                              + ", pardon, unlocked, walk-out order) - the other game's guard let its copy go");
                    StatePush(op.uid);
                }
                else ++g_prisonReleaseFailed;
                continue;
            }
            if (PoseInSomethingPod(c) == 2)
            {
                if (op.pardon && !op.own)   // review-arrest3 M3
                {
                    void* cage = PrisonCageOf(c);
                    const double nowH = LocalWorldHours();
                    if (cage != 0 && PrisonCopyPardonPod(c, cage, nowH < 0.0 ? -1.0 : nowH + kPardonHours) == 1) ++g_refusedPardon;
                }
                if (PosePrisonPod(c, false, 0) < 0) ++g_poseBedFaulted;
            }
            continue;
        }
        if (!PlausibleObject(op.cage) || PoseInSomethingPod(c) != 0 || BeingCarried(c))
        { if (op.own) { ++g_prisonFailed; PrisonSendRefused(op.uid, op.peer); } else g_cageQueued.erase(op.uid); continue; }
        const int room = PrisonFreeSlotPod(op.cage, c);
        if (room != 1)
        {
            if (op.own)
            {
                ++g_prisonNoSlot;
                PrisonSendRefused(op.uid, op.peer);
                PrisonLog("<- PRISON uid=" + S((long long)op.uid) + " NOT caged: isFreeSlot=" + S((long long)room));
            }
            else g_cageQueued.erase(op.uid);
            continue;
        }
        const int r = PosePrisonPod(c, true, op.cage);
        if (r != 1 || PoseInSomethingPod(c) != 2)
        {
            if (op.own) { ++g_prisonFailed; PrisonSendRefused(op.uid, op.peer); } else { ++g_poseBedFaulted; g_cageQueued.erase(op.uid); }
            continue;
        }
        if (PrisonHandSetPod(c) != 1)   // tryOperate refused: in the cage mode with no cage - no lock, no sentence
        {
            ++g_prisonNoHand;
            if (op.own) PrisonSendRefused(op.uid, op.peer);
            if (op.own) PrisonLog("<- PRISON uid=" + S((long long)op.uid) + " NOT caged: the cage refused it (no hand after setPrisonMode)");
            else g_cageQueued.erase(op.uid);
            continue;
        }
        if (!op.own)   // a copy: PoseApplyCage sees +0x2F8 == 2 on its next tick
        {
            ++g_poseCageInExec;
            GuardCagedMap::iterator ga = g_guardCaged.find(op.uid);
            if (ga == g_guardCaged.end())   // review-arrest3 H1: no jailer here frees it before that tick
            { if (PrisonHoldSentencePod(c) == 1) ++g_copyHoldSentence; else ++g_copyHoldSentenceFailed; }
            else if (GuardSentenceSane(ga->second))   // jail2: this game's arrest put back - its sentence, read live
            {
                // review-jail2 Q3: written back only when the live one is gone (missing, 0 or the hold); a sane live one stands
                const GuardArrest cur = GuardArrestReadPod(c);
                if (cur.known && !GuardSentenceSane(cur) && GuardArrestWritePod(c, ga->second.start, ga->second.hours) == 1)
                    ++g_guardSentenceRestored;
            }
            continue;
        }
        ++g_prisonCaged;
        const int lk = PrisonLockPod(op.cage);
        if (lk == 1) ++g_prisonLocked;
        float hours = -1.0f;
        const int sn = PrisonSentencePod(c, op.cage, &hours, op.sentence);
        if (sn == 1) ++g_prisonSentenced; else if (sn == 2) ++g_prisonNoBounty; else ++g_prisonSentenceFailed;   /* P11: 2 = no bounty with the cage's faction */
        if (sn == 1 && hours >= 0.0f)   // user decision 2026-09-25: the guard's game (the arrest) frees first; this clock is the fallback
        {
            if (PrisonSentenceAddPod(c, kOwnSentenceMarginHours) == 1) { ++g_ownSentenceMargin; hours += kOwnSentenceMarginHours; }
        }
        PrisonLog("<- PRISON uid=" + S((long long)op.uid) + " caged (lock=" + S((long long)lk) + " sentence=" + S((long long)sn)
                  + " hours=" + S((long long)hours) + ") - the other game's guard put its copy there");
        StatePush(op.uid);   // a STATE at once; the cage itself travels in the next frame's POSE push (PoseWatchTick)
    }
    g_prisonOps.clear();
}

void ApplyRemotePrison(const cooprison::PrisonMsg& m, unsigned int fromPeer)
{
    if (cooprison::PrisonIsLockKind(m.kind)) { RestraintApplyRemote(m, fromPeer); return; }   // P42: a restraint lock (word or request)
    if (m.kind == cooprison::kPrisonBedRefused)   // the owner could not put it in the bed - our copy comes out at the next scan
    {
        if (net::IsUidMine(m.uid) || !net::UidOwnedByPeer(m.uid, fromPeer)) { ++g_prisonNotMine; return; }
        std::map<unsigned int, BedAsk>::iterator a = g_bedAsk.find(m.uid);
        // only the waiting ask about the bed it names: a late refusal of an earlier bed does not end a later ask
        if (a == g_bedAsk.end() || a->second.state != kBedAskPending || (!m.cageKey.empty() && m.cageKey != a->second.key))
        { ++g_bedRefusalStale; return; }
        a->second.state = kBedAskRefusedIn; ++g_bedRefused;
        if (m.permanent != 0) g_bedRefusedKey[m.uid] = a->second.key;   // full, not a bed, or dead: that bed is not asked about again
        else BedNoteTempRefusal(m.uid, a->second.key);   // for now: counted toward the cooldown
        return;
    }
    if (m.kind == cooprison::kPrisonBedIn)   // another game's rescuer put its copy of our character in a bed (BedApplyOwn)
    {
        if (!net::IsUidMine(m.uid)) { ++g_prisonNotMine; return; }
        ++g_bedRecv;
        BedIn p; p.m = m; p.peer = fromPeer; p.firstAt = ::GetTickCount(); p.triedAt = 0;
        /* latest wins; a request still waiting from ANOTHER player's game is refused first, as a cage request is */
        std::map<unsigned int, BedIn>::iterator prev = g_bedIn.find(m.uid);
        if (prev != g_bedIn.end() && cooplive::AddrPrisonRefusesEarlier(true, prev->second.peer, fromPeer, coop::LinkPeerSlot()))
            BedSendRefused(m.uid, prev->second.peer, prev->second.m.cageKey, false, "a later request from another game");
        g_bedIn[m.uid] = p;
        return;
    }
    if (m.kind == cooprison::kPrisonRefused)   // arrest3: the owner could not cage it - our copy comes out of its cage
    {
        ++g_prisonRefusedRecv;
        if (net::IsUidMine(m.uid) || !net::UidOwnedByPeer(m.uid, fromPeer)) { ++g_prisonNotMine; return; }
        ::Character* c = FindSpawned(m.uid);
        if (c != 0 && PlausibleObject(c) && PoseInSomethingPod(c) == 2 && g_poseCaged.find(m.uid) == g_poseCaged.end())
        { PrisonQueueOp(m.uid, false, 0, false, false, 0, 0, true); ++g_prisonRefusedApplied; }
        return;
    }
    if (!net::IsUidMine(m.uid)) { ++g_prisonNotMine; return; }
    if (m.kind == cooprison::kPrisonRelease)   // arrest3: applied by PrisonTick / the safe point
    {
        ++g_prisonReleaseRecv;
        g_prisonReleaseIn[m.uid] = m.cageKey;   // review-arrest3 M2: the releaser's faction ("" unknown, "@player", or a stringID)
        return;
    }
    ++g_prisonRecv;
    PrisonIn p; p.m = m; p.peer = fromPeer; p.firstAt = ::GetTickCount(); p.triedAt = 0;
    /* M7b fold 1: latest wins, but a request still waiting from ANOTHER guard's game is refused first - that game takes its copy
       out of the cage instead of waiting on a caging this owner will never answer (ownerroute.h AddrPrisonRefusesEarlier) */
    std::map<unsigned int, PrisonIn>::iterator prev = g_prisonIn.find(m.uid);
    if (prev != g_prisonIn.end() && cooplive::AddrPrisonRefusesEarlier(true, prev->second.peer, fromPeer, coop::LinkPeerSlot()))
        PrisonSendRefused(m.uid, prev->second.peer);
    g_prisonIn[m.uid] = p;   // latest wins
}

void PrisonNoteDropped() { ++g_prisonDropped; }

// ---- heal1 (user T305/T307): a medic's treatment of the other game's character reaches that character ------------------
void TreatNoteDropped() { ++g_treatDropped; }

// Every 0.5 s: a copy whose bandageLevel / splint a medic HERE raised above what its owner's STATE last wrote -> MSG_TREAT with
// the copy's values now, and a hold so the owner's next STATE (sent before it heard) does not erase them.
void TreatTick()
{
    static DWORD s_at = 0;
    const DWORD now = ::GetTickCount();
    if (s_at != 0 && now - s_at < kTreatMinGapMs) return;
    s_at = now == 0 ? 1u : now;
    if (EngineWritesBlocked() || g_treat.empty()) return;
    std::map<unsigned int, TreatTrack>::iterator it = g_treat.begin();
    while (it != g_treat.end())
    {
        const unsigned int uid = it->first;
        TreatTrack& t = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c) || net::IsUidMine(uid)) { g_treat.erase(it++); continue; }
        ++it;
        if (t.sentOnce && now - t.sentAt < kTreatMinGapMs) continue;
        MedicalSystem* med = (MedicalSystem*)((char*)c + 0x458);
        if (!PlausiblePtr(med) || med->countBodyParts() != t.n || t.n <= 0) continue;
        cooptreat::TreatMsg m;
        m.uid = uid; m.n = (unsigned int)t.n;
        bool raised = false, ok = true;
        for (int i = 0; i < t.n; ++i)
        {
            MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
            if (!PlausiblePtr(p)) { ok = false; break; }
            const float cb = p->bandageLevel, cj = p->splintLevel;
            if (!cooptreat::TreatValueOk(cb) || !cooptreat::TreatValueOk(cj)) { ok = false; break; }
            // review-heal1 2: only a RAISED value travels - 0 means "no change" (the owner never lowers)
            m.bandageLevel[i] = cb > t.band[i] + kTreatEps ? cb : 0.0f;
            m.splintLevel[i] = cj > t.jury[i] + kTreatEps ? cj : 0.0f;
            if (m.bandageLevel[i] > 0.0f || m.splintLevel[i] > 0.0f) raised = true;
            // review-heal1 4: the baseline follows the copy's own value down, so a later re-bandage below it is still seen
            if (cb < t.band[i]) t.band[i] = cb;
            if (cj < t.jury[i]) t.jury[i] = cj;
        }
        if (!ok || !raised || !t.based) continue;
        if (!net::SendTreat(m)) { ++g_treatSendFailed; continue; }
        ++g_treatSent;
        // re-check-heal1 Q1: a hold still running keeps the parts an earlier TREAT raised - a second TREAT for another part
        // must not zero them (the owner may not have caught up on them yet)
        const bool live = t.hold && now - t.holdAt < kTreatHoldMs;
        t.sentAt = now; t.sentOnce = true;
        for (int i = 0; i < t.n; ++i)
        {
            if (m.bandageLevel[i] > 0.0f) t.band[i] = m.bandageLevel[i];
            if (m.splintLevel[i] > 0.0f) t.jury[i] = m.splintLevel[i];
            // 0 on untouched parts: nothing new held there; a live hold keeps its own value
            t.holdBand[i] = live && t.holdBand[i] > m.bandageLevel[i] ? t.holdBand[i] : m.bandageLevel[i];
            t.holdJury[i] = live && t.holdJury[i] > m.splintLevel[i] ? t.holdJury[i] : m.splintLevel[i];
        }
        t.hold = true; t.holdAt = now;
        if (g_treatLogged < 12)
        {
            ++g_treatLogged;
            DebugLog("[HEAL] -> TREAT uid=" + S((long long)uid) + " parts=" + S((long long)t.n)
                     + " - a medic here treated this copy; its owner is asked to apply the treatment");
        }
    }
}

// The owner's side: raise our own character's per-part treatment to the medic's values (never lower it), then STATE at once.
void ApplyRemoteTreat(const cooptreat::TreatMsg& m, unsigned int fromPeer)
{
    (void)fromPeer;
    ++g_treatRecv;
    if (!net::IsUidMine(m.uid)) { ++g_treatNotMine; return; }
    ::Character* c = FindSpawned(m.uid);
    if (c == 0 || !PlausibleObject(c)) { ++g_treatNotMine; return; }
    MedicalSystem* med = (MedicalSystem*)((char*)c + 0x458);
    if (!PlausiblePtr(med) || med->countBodyParts() != (int)m.n) { ++g_treatMismatch; return; }
    bool changed = false;
    for (unsigned int i = 0; i < m.n; ++i)
    {
        MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)i);
        if (!PlausiblePtr(p)) { ++g_treatMismatch; return; }
        bool here = false;
        if (m.bandageLevel[i] > p->bandageLevel + kTreatEps) { p->bandageLevel = m.bandageLevel[i]; here = true; }
        if (m.splintLevel[i] > p->splintLevel + kTreatEps) { p->splintLevel = m.splintLevel[i]; here = true; }
        if (here) { p->recomputeHealth(); changed = true; }
    }
    if (!changed) { ++g_treatNoChange; return; }
    ++g_treatApplied;
    if (g_treatLogged < 12)
    {
        ++g_treatLogged;
        DebugLog("[HEAL] <- TREAT uid=" + S((long long)m.uid) + " applied - the other game's medic treated this character");
    }
    StatePush(m.uid);   // the treatment reaches every copy at once, not on the round-robin
}

// ==== names1: a copy carries its owner's character name (MSG_NAME, protocol 64) ================================
// OWNER: every SPAWN this game sends is followed by a NAME (net::SendSpawn -> NameSendWithSpawn - creation, adoption, the
// link-up and reload re-announces all go through SendSpawn). A rename is an EVENT: the hook on Character::setName
// 0x5CB840 (Read: `this+0x18 = name; if (this+0x638 name tag) refresh`) marks an OWNED character's uid from whatever
// thread it runs on (address compares and interlocked writes only); NameTick sends it on the main thread when the name
// differs from the last one sent. RECEIVER: NameNoteRecv sets the name on the copy on the main thread through the same
// (hooked) setter. ECHO: the copy is not owned here, so the hook's owner test alone never marks it; the apply also runs
// under g_nameApplyTid so the hook counts the call as echoSuppressed and returns before any lookup. A NAME for a uid
// with no copy yet is kept (latest per uid) and applied by NameOnCopyReady when ApplyRemoteSpawn registers the copy;
// dropped at despawn / retirement / peer gone.
namespace { bool ReadStdStringPod(const void* s, char* out, int cap); }   // defined below, in the same (anonymous) namespace

static unsigned long long kNameSetRva = 0; static AddrReg kNameSetRva_reg("Character_rename", &kNameSetRva);   /* Steam_1.0.65 0x5CB840 */

namespace {
typedef void (*NameSetterFn)(void* self, const void* name);   // Character::setName(const std::string&): the string is only passed on
NameSetterFn g_nameOrig = 0;
uintptr_t    g_nameEntry = 0;     // base + rva: the HOOKED entry, so an apply passes the detour (and its echo guard)
int          g_nameHook = 0;      // 1 armed, -1 AddHook failed, -2 no table address
volatile LONG g_nameApplyTid = 0;
const int kNameDirtyCap = 64;
volatile LONG g_nameDirty[kNameDirtyCap];
volatile LONG g_nameDirtyOverflow = 0;
volatile LONG64 g_nameHookCalls = 0, g_nameEchoSuppressed = 0, g_nameMarked = 0, g_nameDirtyFull = 0;
long long g_nameSent = 0, g_nameSendFailed = 0, g_nameRecv = 0, g_nameApplied = 0, g_namePendingApplied = 0;
long long g_nameUnknownUid = 0, g_nameTooLong = 0, g_nameUnread = 0, g_nameApplyFailed = 0, g_nameSame = 0;
long long g_nameUnchangedSkip = 0, g_nameBad = 0, g_nameForgotten = 0, g_nameHeldNew = 0;
int g_nameLogged = 0, g_nameSentLogged = 0;
std::map<unsigned int, std::string> g_nameLastSent;   // OWNED uid -> the name last sent (MAIN THREAD)
std::map<unsigned int, std::string> g_namePending;    // peer uid -> the latest name, no copy yet (MAIN THREAD)
std::map<unsigned int, int> g_namePendingFrom;        // peer uid -> the slot of the player whose NAME is held (-1 unknown); one row per g_namePending row

void NameMarkDirty(unsigned int uid)   // ANY THREAD: no allocation, no lock
{
    for (int i = 0; i < kNameDirtyCap; ++i) if ((unsigned int)g_nameDirty[i] == uid) return;
    for (int i = 0; i < kNameDirtyCap; ++i)
        if (::InterlockedCompareExchange(&g_nameDirty[i], (LONG)uid, 0) == 0) { ::InterlockedIncrement64(&g_nameMarked); return; }
    ::InterlockedIncrement64(&g_nameDirtyFull);
    ::InterlockedExchange(&g_nameDirtyOverflow, 1);   // NameTick re-reads every owned name it has sent: never dropped
}

void detour_setName(void* self, const void* name)
{
    g_nameOrig(self, name);
    ::InterlockedIncrement64(&g_nameHookCalls);
    if (g_nameApplyTid != 0 && (DWORD)g_nameApplyTid == ::GetCurrentThreadId()) { ::InterlockedIncrement64(&g_nameEchoSuppressed); return; }
    const unsigned int uid = FindSpawnedUid(self);
    if (uid == 0 || !net::IsUidMineAnyThread(uid)) return;
    NameMarkDirty(uid);
}

bool NameCallSetterPod(void* c, const std::string* name)
{
    __try { ((NameSetterFn)g_nameEntry)(c, (const void*)name); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The character's current name (RootObjectBase +0x18), cut to kNameMax at a UTF-8 boundary. false = unreadable / empty.
bool NameReadCut(const void* c, std::string* out)
{
    char buf[256];
    if (c == 0 || !PlausibleObject(c) || !ReadStdStringPod((const char*)c + 0x18, buf, (int)sizeof(buf))) { ++g_nameUnread; return false; }
    const size_t n = std::strlen(buf);
    if (n == 0) return false;
    const size_t cut = coopname::NameCutUtf8(buf, n, coopname::kNameMax);
    if (cut != n) ++g_nameTooLong;
    out->assign(buf, cut);
    return true;
}

void NameSendFor(unsigned int uid, const void* c, bool force, const char* why)
{
    std::string name;
    if (!NameReadCut(c, &name)) return;
    std::map<unsigned int, std::string>::iterator it = g_nameLastSent.find(uid);
    if (!force && it != g_nameLastSent.end() && it->second == name) { ++g_nameUnchangedSkip; return; }
    if (!net::SendName(uid, name, why != 0 && std::string(why) == "spawn"))   // after a SPAWN: on that SPAWN's road
    {
        ++g_nameSendFailed;
        if (net::SideRoadOpen()) NameMarkDirty(uid);   // a road open, send refused: retried next frame. No road: the next SPAWN carries it
        return;
    }
    ++g_nameSent;
    g_nameLastSent[uid] = name;
    if (g_nameSentLogged < 20)
    {
        ++g_nameSentLogged;
        DebugLog("[NAME] -> uid=" + S((long long)uid) + " name='" + name + "' (" + why + ")");
    }
}

bool NameApplyToCopy(unsigned int uid, ::Character* c, const std::string& name, const char* how)
{
    char old[256]; old[0] = 0;
    if (!ReadStdStringPod((const char*)c + 0x18, old, (int)sizeof(old))) { old[0] = '?'; old[1] = 0; }
    if (g_nameEntry == 0) { ++g_nameApplyFailed; return false; }
    if (name == old) ++g_nameSame;
    ::InterlockedExchange(&g_nameApplyTid, (LONG)::GetCurrentThreadId());
    const bool ok = NameCallSetterPod(c, &name);
    ::InterlockedExchange(&g_nameApplyTid, 0);
    if (!ok) { ++g_nameApplyFailed; ErrorLog("[NAME] setName FAULTED on the copy of uid " + S((long long)uid)); return false; }
    ++g_nameApplied;
    if (g_nameLogged < 20)
    {
        ++g_nameLogged;
        DebugLog("[NAME] <- uid=" + S((long long)uid) + " name='" + name + "' (was '" + old + "')" + how);
    }
    return true;
}
} // namespace (names1)

bool NameOf(const void* c, std::string* out) { return NameReadCut(c, out); }
bool NameSetOwn(::Character* c, const std::string& name)
{
    if (g_nameEntry == 0 || c == 0) return false;
    return NameCallSetterPod(c, &name);
}

void InstallNames()
{
    if (kNameSetRva == 0)
    {
        g_nameHook = -2;
        ErrorLog("[NAME] Character_rename has no address in the table - renames are not sent and names are not applied");
        return;
    }
    g_nameEntry = (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kNameSetRva;
    g_nameHook = (coop::AddHook((void*)g_nameEntry, (void*)&detour_setName, (void**)&g_nameOrig) == coop::SUCCESS) ? 1 : -1;
    DebugLog(g_nameHook == 1 ? "[NAME] Character::setName 0x5CB840 hooked - an owned character's rename is sent"
                             : "[NAME] Character::setName AddHook FAILED - renames are not sent (spawns still carry names)");
}

void NameTick()
{
    if (EngineWritesBlocked()) return;   // marks wait
    for (int i = 0; i < kNameDirtyCap; ++i)
    {
        if (g_nameDirty[i] == 0) continue;
        const unsigned int uid = (unsigned int)::InterlockedExchange(&g_nameDirty[i], 0);
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        NameSendFor(uid, FindSpawned(uid), false, "rename");
    }
    if (g_nameDirtyOverflow != 0 && ::InterlockedExchange(&g_nameDirtyOverflow, 0) != 0)
        for (std::map<unsigned int, std::string>::iterator it = g_nameLastSent.begin(); it != g_nameLastSent.end(); ++it)
            if (net::IsUidMine(it->first)) NameSendFor(it->first, FindSpawned(it->first), false, "rename (overflow)");
    /* review-names1 D2 (manager fold): a held name is also applied when its copy comes back WITHOUT ApplyRemoteSpawn
       (a streamed-out copy restored by MirrorRestore). The held map is one entry per peer character at most, and
       FindSpawned is an index lookup, so this is not a world sweep; an entry stays until its copy resolves or the uid
       is forgotten (NameForgetUid / NameForgetPeer). */
    for (std::map<unsigned int, std::string>::iterator it = g_namePending.begin(); it != g_namePending.end(); )
    {
        ::Character* c = FindSpawned(it->first);
        if (c == 0 || !PlausibleObject(c)) { ++it; continue; }
        const unsigned int u = it->first;
        const std::string name = it->second;
        g_namePending.erase(it++);
        g_namePendingFrom.erase(u);
        if (NameApplyToCopy(u, c, name, " (held until the copy existed)")) ++g_namePendingApplied;
    }
}

void NameSendWithSpawn(unsigned int uid, const void* character)
{
    NameSendFor(uid, character, true, "spawn");
}

void NameNoteRecv(unsigned int uid, const std::string& name, unsigned int fromPeer)
{
    ++g_nameRecv;
    if (net::IsUidMine(uid)) { ++g_nameUnknownUid; return; }   // our own character: only we name it (reported as notMine=, review D1)
    if (name.empty()) return;
    ::Character* c = EngineWritesBlocked() ? 0 : FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c))
    {
        if (g_namePending.find(uid) == g_namePending.end()) ++g_nameHeldNew;
        g_namePending[uid] = name;   // the latest wins; applied when ApplyRemoteSpawn registers the copy
        g_namePendingFrom[uid] = net::PlayerSlotOfKey(fromPeer);   // whose it is: that player leaving drops it
        return;
    }
    g_namePending.erase(uid); g_namePendingFrom.erase(uid);
    NameApplyToCopy(uid, c, name, "");
}

void NameNoteBad(bool tooLong)
{
    ++g_nameBad;
    if (tooLong) ++g_nameTooLong;
}

void NameOnCopyReady(unsigned int uid)
{
    std::map<unsigned int, std::string>::iterator it = g_namePending.find(uid);
    if (it == g_namePending.end()) return;
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return;   // stays held
    const std::string name = it->second;
    g_namePending.erase(it); g_namePendingFrom.erase(uid);
    if (NameApplyToCopy(uid, c, name, " (held until the copy existed)")) ++g_namePendingApplied;
}

void NameForgetUid(unsigned int uid)
{
    if (g_namePending.erase(uid) != 0) ++g_nameForgotten;
    g_namePendingFrom.erase(uid);
    g_nameLastSent.erase(uid);
}

void NameForgetPeer(int slot)
{
    for (std::map<unsigned int, std::string>::iterator it = g_namePending.begin(); it != g_namePending.end(); )
    {
        std::map<unsigned int, int>::const_iterator f = g_namePendingFrom.find(it->first);
        if (!cooppg::PlayerGoneTakesRow(f != g_namePendingFrom.end() ? f->second : -1, slot)) { ++it; continue; }
        g_namePendingFrom.erase(it->first);
        g_namePending.erase(it++);
        ++g_nameForgotten;
    }
}

std::string NameReportLine()
{
    return "[NAME] REPORT sent=" + S(g_nameSent) + " recv=" + S(g_nameRecv) + " applied=" + S(g_nameApplied)
         + " pendingApplied=" + S(g_namePendingApplied) + " echoSuppressed=" + S((long long)g_nameEchoSuppressed)
         + " notMine=" + S(g_nameUnknownUid) + " tooLong=" + S(g_nameTooLong)
         + " | hook=" + S((long long)g_nameHook) + " hookCalls=" + S((long long)g_nameHookCalls) + " marked=" + S((long long)g_nameMarked)
         + " dirtyFull=" + S((long long)g_nameDirtyFull) + " unchangedSkip=" + S(g_nameUnchangedSkip) + " sendFailed=" + S(g_nameSendFailed)
         + " unread=" + S(g_nameUnread) + " same=" + S(g_nameSame) + " applyFailed=" + S(g_nameApplyFailed) + " bad=" + S(g_nameBad)
         + " heldNew=" + S(g_nameHeldNew) + " held=" + S((long long)g_namePending.size()) + " forgotten=" + S(g_nameForgotten);
}
// ==== end names1 ================================================================================================

// ==== slave1: a copy's slave state follows its owner (MSG_SLAVE 52, protocol 65) ================================
// T337/T341 (Confirmed): a slave owned by game A read SlaveStateEnum 1 on A all run while B's copy went to 2 on its own.
// ENGINE (Read, slave1 decompiles): the state is StateBroadcastData +0 (Character +0x1A0 -> sb; sb +0x10 = the character,
// sb +0x8 = the time it last changed). The setter StateBroadcastData::setSlaveState 0x5A3EB0 (sb, s) stamps +0x8 for s > 1
// and runs setSlaveAIJob 0x32E090 (adds / removes the permanent job 0xBB with the owner hand +0x328). A copy's own change
// comes from the AI WORKER: AI::update4Frame 0x595B20 -> AI::updateStateBroadcast 0x5A4050, and AI::periodicUpdate 0x510820
// / _KOed 0x50BD30 -> periodicUpdateStateBroadcast 0x5A4750 -> StateBroadcastData::periodicUpdate 0x5A44C0, both set 2 when
// the slave-owner hand does not resolve here or is far away - on a copy it often does not. update4Frame and _KOed are not
// behind the M2b decision gate, so suppression never stopped this. periodicUpdate also WRITES +0 itself (2 -> 3 after a
// while, 3 -> 0 plus changeSlaveOwner(none) after longer, -> 0 when the stamp is in the future).
// OWNER: both hooks mark an OWNED uid (address compares and interlocked writes only, any thread); SlaveTick sends the new
// value on the main thread when it differs from the last one sent; every SPAWN is followed by the current value.
// COPY (not owned here, twins included): a change the copy's own engine makes is REFUSED - the setter returns without
// calling the original, and a direct write of periodicUpdate is put back (value and stamp) before that detour returns, on
// the thread that wrote it. The owner's value is set at the K2 safe point (worker paused) through the HOOKED setter under
// g_slaveApplyTid, which the setter detour lets through.
static unsigned long long kSlaveSetRva = 0; static AddrReg kSlaveSetRva_reg("SlaveStateSet", &kSlaveSetRva);   /* Steam_1.0.65 0x5A3EB0 StateBroadcastData::setSlaveState */
static unsigned long long kSlavePeriodicRva = 0; static AddrReg kSlavePeriodicRva_reg("SlaveStatePeriodic", &kSlavePeriodicRva);   /* Steam_1.0.65 0x5A44C0 StateBroadcastData::periodicUpdate */
/* P11 (items.cpp capture block, namespace coop): */
void CaptureNoteState(unsigned int uid, int want);                                 // ANY THREAD: H4 - inside a slaver's processing
unsigned int CaptureOwnerUidOf(const void* c);                                     // MAIN THREAD: the slave-owner hand's uid
int CaptureApplyOwnerToCopy(unsigned int uid, ::Character* c, unsigned int ownerUid);   // K2 safe point: the owner's hand; 1 applied or already so

namespace {
typedef void (*SlaveSetFn)(void* sb, int s);
typedef void (*SlavePeriodicFn)(void* sb);
SlaveSetFn      g_slaveSetOrig = 0;
SlavePeriodicFn g_slavePerOrig = 0;
uintptr_t       g_slaveSetEntry = 0;    // base + rva: the HOOKED entry, so an apply passes the detour (and its tid guard)
int             g_slaveSetHook = 0, g_slavePerHook = 0;   // 1 armed, -1 AddHook failed, -2 no table address
volatile LONG   g_slaveApplyTid = 0;
const size_t    kSlaveSbOff = 0x1A0;    // Character -> StateBroadcastData* (getStateBroadcast 0x5E1440, the P084 probe's read)
const size_t    kSlaveMeOff = 0x10;     // StateBroadcastData -> Character* me
const size_t    kSlaveTsOff = 0x8;      // StateBroadcastData -> _slaveStateChangedTime (TimeOfDay, one double)
const int       kSlaveDirtyCap = 64;
volatile LONG   g_slaveDirty[kSlaveDirtyCap];
volatile LONG   g_slaveDirtyOverflow = 0;
volatile LONG64 g_slaveSetCalls = 0, g_slavePerChanges = 0, g_slaveMarked = 0, g_slaveDirtyFull = 0, g_slaveApplyPassed = 0;
volatile LONG64 g_slaveCopyChangeRefused = 0, g_slaveRefusedSetter = 0, g_slaveRefusedDirect = 0, g_slaveRestoreFault = 0;
const int kSlaveLogCap = 20;
struct SlaveRefusal { volatile LONG uid; volatile LONG from; volatile LONG to; volatile LONG how; volatile LONG ready; };
SlaveRefusal  g_slaveRefusal[kSlaveLogCap];   // the first 20 refusals, filled on the worker, logged by SlaveTick
volatile LONG g_slaveRefusalClaimed = 0;
int g_slaveRefusalPrinted = 0;
long long g_slaveSent = 0, g_slaveSendFailed = 0, g_slaveRecv = 0, g_slaveApplied = 0, g_slaveUnknownUid = 0;
long long g_slaveNotMine = 0, g_slaveSame = 0, g_slaveApplyFailed = 0, g_slaveBad = 0, g_slaveUnread = 0;
long long g_slaveUnchangedSkip = 0, g_slaveForgotten = 0;
int g_slaveSentLogged = 0, g_slaveAppliedLogged = 0;
std::map<unsigned int, int> g_slaveLastSent;   // OWNED uid -> the value last sent (MAIN THREAD)
std::map<unsigned int, unsigned int> g_slaveLastOwner;     // P11: OWNED uid -> the owner uid last sent (MAIN THREAD)
std::map<unsigned int, unsigned int> g_slavePendingOwner;  // P11: peer uid -> the owner's latest owner uid, set with the state
std::map<unsigned int, unsigned int> g_slaveOwnerApplied;  // P11 f3 (M7): peer uid -> the owner uid last applied to its copy (a re-announce of it is not re-applied)
std::map<unsigned int, int> g_slavePending;    // peer uid -> the owner's latest value, set at the next K2 safe point (MAIN THREAD)
std::map<unsigned int, int> g_slaveFrom;       // peer uid -> the slot of the player whose SLAVE state arrived last (-1 unknown): that player leaving drops its rows above

void* SlaveSbOfPod(const void* c)
{
    if (c == 0) return 0;
    __try { return *(void* const*)((const char*)c + kSlaveSbOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void* SlaveMeOfPod(const void* sb)
{
    if (sb == 0) return 0;
    __try { return *(void* const*)((const char*)sb + kSlaveMeOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool SlaveReadPod(const void* sb, int* state, double* ts)
{
    if ((uintptr_t)sb < 0x10000) return false;
    __try
    {
        *state = *(const int*)sb;
        if (ts != 0) *ts = *(const double*)((const char*)sb + kSlaveTsOff);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SlaveRestorePod(void* sb, int state, double ts)
{
    __try { *(double*)((char*)sb + kSlaveTsOff) = ts; *(int*)sb = state; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SlaveCallSetterPod(void* sb, int s)
{
    __try { ((SlaveSetFn)g_slaveSetEntry)(sb, s); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void SlaveMarkDirty(unsigned int uid)   // ANY THREAD: no allocation, no lock (NameMarkDirty's shape)
{
    for (int i = 0; i < kSlaveDirtyCap; ++i) if ((unsigned int)g_slaveDirty[i] == uid) return;
    for (int i = 0; i < kSlaveDirtyCap; ++i)
        if (::InterlockedCompareExchange(&g_slaveDirty[i], (LONG)uid, 0) == 0) { ::InterlockedIncrement64(&g_slaveMarked); return; }
    ::InterlockedIncrement64(&g_slaveDirtyFull);
    ::InterlockedExchange(&g_slaveDirtyOverflow, 1);   // SlaveTick re-reads every owned value it has sent: never dropped
}

void SlaveNoteRefusal(unsigned int uid, int from, int to, int how)   // ANY THREAD: counters and a fixed slot, no logging
{
    ::InterlockedIncrement64(&g_slaveCopyChangeRefused);
    ::InterlockedIncrement64(how == 1 ? &g_slaveRefusedSetter : &g_slaveRefusedDirect);
    const LONG i = ::InterlockedIncrement(&g_slaveRefusalClaimed) - 1;
    if (i < 0 || i >= kSlaveLogCap) return;
    g_slaveRefusal[i].uid = (LONG)uid; g_slaveRefusal[i].from = from; g_slaveRefusal[i].to = to; g_slaveRefusal[i].how = how;
    ::InterlockedExchange(&g_slaveRefusal[i].ready, 1);
}

// StateBroadcastData::setSlaveState 0x5A3EB0 - AI worker (updateStateBroadcast / periodicUpdate) or main (tasks, dialogue).
void detour_setSlaveState(void* sb, int s)
{
    ::InterlockedIncrement64(&g_slaveSetCalls);
    if (g_slaveApplyTid != 0 && (DWORD)g_slaveApplyTid == ::GetCurrentThreadId())
    {
        ::InterlockedIncrement64(&g_slaveApplyPassed);   // the owner's value, set by SlaveApplyToCopy
        g_slaveSetOrig(sb, s);
        return;
    }
    const void* c = SlaveMeOfPod(sb);
    const unsigned int uid = (c != 0) ? FindSpawnedUid(c) : 0;
    if (uid == 0) { g_slaveSetOrig(sb, s); return; }   // not a replicated character: the engine's own business
    if (net::IsUidMineAnyThread(uid))
    {
        g_slaveSetOrig(sb, s);
        SlaveMarkDirty(uid);                            // SlaveTick sends it when it differs from the last value sent
        return;
    }
    int cur = -1;
    if (!SlaveReadPod(sb, &cur, 0) || cur == s) { g_slaveSetOrig(sb, s); return; }   // no change: as the engine
    SlaveNoteRefusal(uid, cur, s, 1);                   // a copy's own change: its owner decides, this engine does not
    CaptureNoteState(uid, s);                           // P11 H4: inside a slaver's processing the owner is asked (MSG_CAPTURE)
}

// StateBroadcastData::periodicUpdate 0x5A44C0 - AI worker (periodicUpdateStateBroadcast 0x5A4750). Only states 2 and 3 are
// written directly here; 1 -> 2 goes through the (hooked) setter.
void detour_sbPeriodicUpdate(void* sb)
{
    int before = -1;
    double tsBefore = 0.0;
    if (!SlaveReadPod(sb, &before, &tsBefore) || before <= 1) { g_slavePerOrig(sb); return; }
    g_slavePerOrig(sb);
    int after = -1;
    if (!SlaveReadPod(sb, &after, 0) || after == before) return;
    ::InterlockedIncrement64(&g_slavePerChanges);
    const void* c = SlaveMeOfPod(sb);
    const unsigned int uid = (c != 0) ? FindSpawnedUid(c) : 0;
    if (uid == 0) return;
    if (net::IsUidMineAnyThread(uid)) { SlaveMarkDirty(uid); return; }
    if (SlaveRestorePod(sb, before, tsBefore)) SlaveNoteRefusal(uid, before, after, 2);
    else ::InterlockedIncrement64(&g_slaveRestoreFault);
}

void SlaveSendFor(unsigned int uid, const void* c, bool force, const char* why)
{
    if (c == 0 || !PlausibleObject(c)) return;
    int st = -1;
    if (!SlaveReadPod(SlaveSbOfPod(c), &st, 0) || st < 0 || st > coopslave::kSlaveStateMax) { ++g_slaveUnread; return; }
    std::map<unsigned int, int>::iterator it = g_slaveLastSent.find(uid);
    const int was = (it != g_slaveLastSent.end()) ? it->second : -1;
    const unsigned int ownerNow = CaptureOwnerUidOf(c);   /* P11: the slave-owner hand's uid (0 none, 0xFFFFFFFF not replicated) */
    std::map<unsigned int, unsigned int>::iterator ot = g_slaveLastOwner.find(uid);
    const bool ownerSame = (ot != g_slaveLastOwner.end() && ot->second == ownerNow);
    if (!force && was == st && ownerSame) { ++g_slaveUnchangedSkip; return; }
    if (!net::SendSlave(uid, st, ownerNow, why != 0 && std::string(why) == "spawn"))   // after a SPAWN: on that SPAWN's road
    {
        ++g_slaveSendFailed;
        if (net::SideRoadOpen()) SlaveMarkDirty(uid);   // a road open, send refused: retried next frame. No road: the next SPAWN carries it
        return;
    }
    ++g_slaveSent;
    g_slaveLastSent[uid] = st;
    g_slaveLastOwner[uid] = ownerNow;
    if (g_slaveSentLogged < 20 && (!force || st != 0))
    {
        ++g_slaveSentLogged;
        DebugLog("[SLAVE] -> uid=" + S((long long)uid) + " slave=" + S((long long)st) + " ownerUid=" + S((long long)ownerNow) + " (last sent " + S((long long)was) + "; " + why + ")");
    }
}

void SlaveApplyToCopy(unsigned int uid, ::Character* c, int want)
{
    void* sb = SlaveSbOfPod(c);
    int cur = -1;
    if (!SlaveReadPod(sb, &cur, 0)) { ++g_slaveUnread; return; }
    if (cur == want) { ++g_slaveSame; return; }
    if (g_slaveSetEntry == 0) { ++g_slaveApplyFailed; return; }
    ::InterlockedExchange(&g_slaveApplyTid, (LONG)::GetCurrentThreadId());
    const bool ok = SlaveCallSetterPod(sb, want);
    ::InterlockedExchange(&g_slaveApplyTid, 0);
    if (!ok) { ++g_slaveApplyFailed; ErrorLog("[SLAVE] setSlaveState FAULTED on the copy of uid " + S((long long)uid)); return; }
    ++g_slaveApplied;
    if (g_slaveAppliedLogged < 20)
    {
        ++g_slaveAppliedLogged;
        int now = -1;
        SlaveReadPod(sb, &now, 0);
        DebugLog("[SLAVE] <- uid=" + S((long long)uid) + " slave=" + S((long long)want) + " (copy was " + S((long long)cur)
                 + ", reads " + S((long long)now) + " after the setter at the K2 safe point)");
    }
}
} // namespace (slave1)

/* P11: the slave1 block's doors for items.cpp's capture code (namespace coop). */
void SlaveMarkDirtyAny(unsigned int uid) { SlaveMarkDirty(uid); }   /* ANY THREAD: an owned character's owner hand changed */
bool SlaveSetOwnedState(void* c, int s)                            /* MAIN THREAD: through the HOOKED setter (owner branch marks) */
{
    void* sb = SlaveSbOfPod(c);
    if (sb == 0 || g_slaveSetEntry == 0) return false;
    return SlaveCallSetterPod(sb, s);
}

/* recruit1 fold (review-recruit1 3b): the slave state of any character (own or copy), -1 when unreadable. */
int SlaveStateOfCharacter(const void* c)
{
    int st = -1;
    void* sb = SlaveSbOfPod(c);
    if (sb == 0 || !SlaveReadPod(sb, &st, 0)) return -1;
    return st;
}

void InstallSlaves()
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kSlaveSetRva == 0) g_slaveSetHook = -2;
    else
    {
        g_slaveSetEntry = base + (uintptr_t)kSlaveSetRva;
        g_slaveSetHook = (coop::AddHook((void*)g_slaveSetEntry, (void*)&detour_setSlaveState, (void**)&g_slaveSetOrig) == coop::SUCCESS) ? 1 : -1;
    }
    if (kSlavePeriodicRva == 0) g_slavePerHook = -2;
    else
        g_slavePerHook = (coop::AddHook((void*)(base + (uintptr_t)kSlavePeriodicRva), (void*)&detour_sbPeriodicUpdate, (void**)&g_slavePerOrig) == coop::SUCCESS) ? 1 : -1;
    const std::string line = "[SLAVE] hooks: setSlaveState 0x5A3EB0=" + S((long long)g_slaveSetHook)
        + " StateBroadcastData::periodicUpdate 0x5A44C0=" + S((long long)g_slavePerHook) + " (1 armed, -1 AddHook failed, -2 no table address)";
    if (g_slaveSetHook == 1 && g_slavePerHook == 1) DebugLog(line); else ErrorLog(line + " - copies' slave state is NOT held to the owner's");
}

void SlaveTick()
{
    while (g_slaveRefusalPrinted < kSlaveLogCap && g_slaveRefusal[g_slaveRefusalPrinted].ready != 0)
    {
        const SlaveRefusal& r = g_slaveRefusal[g_slaveRefusalPrinted];
        ++g_slaveRefusalPrinted;
        DebugLog("[SLAVE] refused #" + S((long long)g_slaveRefusalPrinted) + " uid=" + S((long long)(unsigned int)r.uid)
                 + " copy change " + S((long long)r.from) + " -> " + S((long long)r.to)
                 + (r.how == 1 ? " (setSlaveState on a copy not owned here)" : " (periodicUpdate's direct write on a copy, put back)"));
    }
    if (EngineWritesBlocked()) return;   // marks wait
    for (int i = 0; i < kSlaveDirtyCap; ++i)
    {
        if (g_slaveDirty[i] == 0) continue;
        const unsigned int uid = (unsigned int)::InterlockedExchange(&g_slaveDirty[i], 0);
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        SlaveSendFor(uid, FindSpawned(uid), false, "change");
    }
    if (g_slaveDirtyOverflow != 0 && ::InterlockedExchange(&g_slaveDirtyOverflow, 0) != 0)
        for (std::map<unsigned int, int>::iterator it = g_slaveLastSent.begin(); it != g_slaveLastSent.end(); ++it)
            if (net::IsUidMine(it->first)) SlaveSendFor(it->first, FindSpawned(it->first), false, "change (overflow)");
}

void SlaveSendWithSpawn(unsigned int uid, const void* character)
{
    SlaveSendFor(uid, character, true, "spawn");
}

void SlaveNoteRecv(unsigned int uid, int state, unsigned int ownerUid, unsigned int fromPeer)
{
    ++g_slaveRecv;
    if (net::IsUidMine(uid)) { ++g_slaveNotMine; return; }   // our own character: only this game sets it
    g_slaveFrom[uid] = net::PlayerSlotOfKey(fromPeer);
    ::Character* c = EngineWritesBlocked() ? 0 : FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) ++g_slaveUnknownUid;   // no copy here yet: held (the latest wins) until one exists
    g_slavePending[uid] = state;
    std::map<unsigned int, unsigned int>::iterator la = g_slaveOwnerApplied.find(uid);   /* P11 f3 (M7): only a CHANGED owner is applied */
    if (la != g_slaveOwnerApplied.end() && la->second == ownerUid) g_slavePendingOwner.erase(uid);
    else g_slavePendingOwner[uid] = ownerUid;   // P11
}

void SlaveNoteBad()
{
    ++g_slaveBad;
}

void SlaveSafePointDrain()
{
    if (g_slavePending.empty() || EngineWritesBlocked()) return;
    for (std::map<unsigned int, int>::iterator it = g_slavePending.begin(); it != g_slavePending.end(); )
    {
        const unsigned int uid = it->first;
        if (net::IsUidMine(uid)) { ++g_slaveNotMine; g_slavePending.erase(it++); continue; }
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c)) { ++it; continue; }   // held until the copy exists
        const int want = it->second;
        g_slavePending.erase(it++);
        SlaveApplyToCopy(uid, c, want);
        std::map<unsigned int, unsigned int>::iterator ow = g_slavePendingOwner.find(uid);   /* P11: the owner's slave-owner hand */
        if (ow != g_slavePendingOwner.end())
        {
            const unsigned int wantOwner = ow->second;
            g_slavePendingOwner.erase(ow);
            if (CaptureApplyOwnerToCopy(uid, c, wantOwner) == 1) g_slaveOwnerApplied[uid] = wantOwner;   /* P11 f3 (M7) */
        }
    }
}

void SlaveForgetUid(unsigned int uid)
{
    if (g_slavePending.erase(uid) != 0) ++g_slaveForgotten;
    g_slaveLastSent.erase(uid);
    g_slaveLastOwner.erase(uid);      // P11
    g_slavePendingOwner.erase(uid);   // P11
    g_slaveOwnerApplied.erase(uid);   // P11 f3
    g_slaveFrom.erase(uid);
}

void SlaveForgetPeer(int slot)
{
    for (std::map<unsigned int, int>::iterator f = g_slaveFrom.begin(); f != g_slaveFrom.end(); )
    {
        if (!cooppg::PlayerGoneTakesRow(f->second, slot)) { ++f; continue; }
        const unsigned int uid = f->first;
        if (g_slavePending.erase(uid) != 0) ++g_slaveForgotten;
        g_slavePendingOwner.erase(uid);   // P11
        g_slaveOwnerApplied.erase(uid);   // P11 f3
        g_slaveFrom.erase(f++);
    }
}

std::string SlaveReportLine()
{
    return "[SLAVE] REPORT sent=" + S(g_slaveSent) + " recv=" + S(g_slaveRecv) + " applied=" + S(g_slaveApplied)
         + " copyChangeRefused=" + S((long long)g_slaveCopyChangeRefused) + " unknownUid=" + S(g_slaveUnknownUid)
         + " | refusedSetter=" + S((long long)g_slaveRefusedSetter) + " refusedDirect=" + S((long long)g_slaveRefusedDirect)
         + " restoreFault=" + S((long long)g_slaveRestoreFault) + " notMine=" + S(g_slaveNotMine) + " same=" + S(g_slaveSame)
         + " held=" + S((long long)g_slavePending.size()) + " applyFailed=" + S(g_slaveApplyFailed) + " unread=" + S(g_slaveUnread)
         + " bad=" + S(g_slaveBad) + " sendFailed=" + S(g_slaveSendFailed) + " unchangedSkip=" + S(g_slaveUnchangedSkip)
         + " marked=" + S((long long)g_slaveMarked) + " dirtyFull=" + S((long long)g_slaveDirtyFull)
         + " hookSet=" + S((long long)g_slaveSetHook) + " hookPeriodic=" + S((long long)g_slavePerHook)
         + " setCalls=" + S((long long)g_slaveSetCalls) + " periodicChanges=" + S((long long)g_slavePerChanges)
         + " applyPassed=" + S((long long)g_slaveApplyPassed) + " forgotten=" + S(g_slaveForgotten);
}
// ==== end slave1 ================================================================================================

std::string TreatReportToken()
{
    return " heal1[sent,sendFailed,heldParts,holdCleared,holdExpired,recv,applied,noChange,notMine,mismatch,dropped,tracked]="
         + S(g_treatSent) + "," + S(g_treatSendFailed) + "," + S(g_treatHeldParts) + "," + S(g_treatHoldCleared) + ","
         + S(g_treatHoldExpired) + "," + S(g_treatRecv) + "," + S(g_treatApplied) + "," + S(g_treatNoChange) + ","
         + S(g_treatNotMine) + "," + S(g_treatMismatch) + "," + S(g_treatDropped) + "," + S((long long)g_treat.size());
}

// ===== P42: A PRISONER'S RESTRAINT LOCKS - THE SHACKLES IT WEARS, THE CAGE IT SITS IN - THE SAME ON EVERY GAME =====
// Both are a DoorLock (+0x00 lockLevel, +0x20 locked, +0x21 broken - build/read-locks.md 2.1): the shackles' at LockedArmour +0x2F0
// (items.cpp ShackleLockOf), the cage's at UseableStuff +0x438. The engine's lockpick (Task_PickLock 0x359100, cages and shackles
// alike), cutting and breaking write those bytes on the game where they run; the engine has no setter for them (DoorLock has no
// methods), so they are read and written here as bytes, the writes at the K2 safe point.
// The prisoner's OWNER decides (prisonwire.h): every second RestraintWatch reads each replicated character's worn shackles and
// cage lock. On the owner's game a change (or every kRestraintResendMs) is sent to every other game as its word (MSG_PRISON kind
// SHACKLE LOCK / CAGE LOCK); a copy whose lock differs from the word takes the word at the safe point (on first sight and after
// every new word too) - except a copy's lock SEEN equal to a closed word and then OPENED here (a rescuer picked it): that is asked
// of the owner, which opens its own when its own is closed (RestraintOwnerAccepts, keeping its own level); its next word then opens
// every copy. A copy found out of its cage or without its shackles keeps the owner's word and forgets what it saw here. A copy
// that leaves a cage which stood open here and in the owner's word is a rescue (PoseApplyCage -> PrisonNoteRescuedHere): told to
// the owner as a player's release.
namespace {
struct RestraintSide
{
    bool word;                  // owner: a word was sent; copy: the owner's word arrived
    unsigned char wFlags;       // that word: cooprison::kLockFlag*
    int wLevel;
    std::string wKey;           // the shackles' section or the cage's key the word is about
    DWORD wAt;                  // owner: when the word went
    int ask;                    // copy: cooprison::kRestraintAsk*
    DWORD askAt;
    bool openHere;              // the last read here found the lock open
    bool seen;                  // copy: a read here found the lock equal to the current word
    DWORD rescueUntil;          // copy, cage: it left a cage open here and in the word - a rescue until then (PoseApplyCage)
    RestraintSide() : word(false), wFlags(0), wLevel(0), wAt(0), ask(cooprison::kRestraintAskNone), askAt(0), openHere(false), seen(false),
                      rescueUntil(0) {}
};
struct RestraintRec { RestraintSide sh, cage; };
std::map<unsigned int, RestraintRec> g_restraint;
const int kRestraintOpWrite = 0, kRestraintOpRequest = 1, kRestraintOpLeverOpen = 2, kRestraintOpLeverClose = 3, kRestraintOpLeverEscape = 4;
struct RestraintOp { unsigned int uid; unsigned char kind; int op; unsigned char flags; int level; std::string key; };
std::vector<RestraintOp> g_restraintOps;
const size_t kRestraintOpsMax = 64;
const DWORD kRestraintScanMs = 1000;
const DWORD kRestraintResendMs = 15000;
const DWORD kRestraintAskMs = 10000;
const DWORD kRestraintRescueMs = 5000;   // a copy out of an open cage: PoseApplyCage's put-back runs within this
const int kTaskGetOutOfCageEscape = 0xCF;   // TaskType GET_OUT_OF_CAGE_ESCAPE 207 (the menu's "Escape" - decomp_79a730 case 0xcf)
DWORD g_restraintScanAt = 0;
long long g_rsWordSent = 0, g_rsWordSendFailed = 0, g_rsWordRecv = 0, g_rsNotOwner = 0, g_rsAskSent = 0, g_rsAskRecv = 0;
long long g_rsAccepted = 0, g_rsRefused = 0, g_rsRewritten = 0, g_rsWriteFailed = 0, g_rsGone = 0, g_rsKeyMismatch = 0;
long long g_rsOpsDropped = 0, g_rsLever = 0, g_rsLogLines = 0, g_rsDropped = 0;
long long g_rsScans = 0, g_rsScanUsLast = 0, g_rsScanUsMax = 0;   // RestraintWatch: passes, microseconds of the last and the longest
const long long kRestraintLogMax = 300;

void RestraintLog(const std::string& line)
{
    if (++g_rsLogLines > kRestraintLogMax) return;
    DebugLog("[LOCK] " + line);
}
const char* RestraintName(unsigned char kind) { return kind == cooprison::kPrisonShackleLock ? "shackles" : "cage"; }
std::string LockBitsText(unsigned char f, int lv)
{
    return "locked=" + S((long long)((f & cooprison::kLockFlagLocked) ? 1 : 0)) + ",broken=" + S((long long)((f & cooprison::kLockFlagBroken) ? 1 : 0))
         + ",level=" + S((long long)lv) + (cooprison::LockOpen(f, lv) ? "(open)" : "(closed)");
}

int LockBitsReadPod(const void* lock, unsigned char* flags, int* level)   // 1 read, 0 not a lock (level outside 0..100), -1 faulted
{
    __try
    {
        const unsigned char* p = (const unsigned char*)lock;
        const int lv = *(const int*)p;
        if (lv < 0 || lv > cooprison::kLockLevelMax) return 0;
        unsigned char f = 0;
        if (p[0x20] != 0) f |= cooprison::kLockFlagLocked;
        if (p[0x21] != 0) f |= cooprison::kLockFlagBroken;
        *flags = f; *level = lv;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int LockBitsWritePod(void* lock, unsigned char flags, int level)   // 1 written, -1 faulted
{
    __try
    {
        unsigned char* p = (unsigned char*)lock;
        p[0x20] = (flags & cooprison::kLockFlagLocked) ? 1 : 0;
        p[0x21] = (flags & cooprison::kLockFlagBroken) ? 1 : 0;
        *(int*)p = level;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int CageLockPtrPod(const void* cage, void** out)   // 1 read (0 = the cage has no lock), -1 faulted
{
    __try { *out = *(void* const*)((const char*)cage + kUseableDoorLockOff); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int EscapeOrderPod(::Character* c)   // GET_OUT_OF_CAGE_ESCAPE as a player's order (PrisonWalkOutPod's call): 1 ordered, 0 no address, -1 faulted
{
    if (kAddOrderRva == 0 || kAddOrderArgGlobalRva == 0) return 0;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const AddOrderFn fn = (AddOrderFn)(base + kAddOrderRva);
    __try
    {
        const unsigned long long a7 = *(const unsigned long long*)(base + kAddOrderArgGlobalRva);
        fn(c, 0, kTaskGetOutOfCageEscape, 0, 0, 1, a7);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// One restraint of `c`: 1 read (its key, the cage's outer key, the lock and its bits), 0 none (no shackles worn / not in a cage /
// a cage with no lock), -1 unreadable.
int RestraintRead(::Character* c, unsigned char kind, std::string* key, std::string* outer, void** lockOut, unsigned char* flags, int* level)
{
    *lockOut = 0; key->clear(); outer->clear();
    void* lock = 0;
    if (kind == cooprison::kPrisonShackleLock)
    {
        char sec[64];
        const int r = ShackleLockOf(c, sec, (int)sizeof(sec), &lock);
        if (r != 1) return r;
        *key = sec;
    }
    else
    {
        unsigned char hd[kHandIdBytes];
        int ins = 0, actLong = 0, stumble = 0, looped = 0;
        float speed = 0.0f;
        char act[coopstate::kPoseStrMax + 1];
        if (!PoseReadPod(c, &ins, hd, act, &actLong, &stumble, &looped, &speed)) return -1;
        if (ins != 2) return 0;
        void* cage = PrisonCageOf(c);
        if (cage == 0) return -1;
        char k[coopstate::kPoseStrMax + 1], o[coopstate::kPoseStrMax + 1];
        if (PoseBedKeyFromHand(hd, k, (int)sizeof(k), o, (int)sizeof(o)) == 0) return -1;
        if (CageLockPtrPod(cage, &lock) != 1) return -1;
        if (lock == 0) return 0;
        *key = k; *outer = o;
    }
    if (!PlausiblePtr(lock)) return -1;
    if (LockBitsReadPod(lock, flags, level) != 1) return -1;
    *lockOut = lock;
    return 1;
}

bool RestraintSend(unsigned int uid, unsigned char kind, const std::string& key, const std::string& outer, unsigned char flags, int level)
{
    cooprison::PrisonMsg m;
    m.uid = uid; m.kind = kind; m.cageKey = key; m.outerKey = outer; m.lockFlags = flags; m.lockLevel = level;
    return net::SendPrison(m);
}

bool RestraintQueue(unsigned int uid, unsigned char kind, int op, unsigned char flags, int level, const std::string& key)
{
    if (g_restraintOps.size() >= kRestraintOpsMax) { ++g_rsOpsDropped; return false; }
    RestraintOp o; o.uid = uid; o.kind = kind; o.op = op; o.flags = flags; o.level = level; o.key = key;
    g_restraintOps.push_back(o);
    return true;
}

void RestraintWatchOne(unsigned int uid, ::Character* c, bool mine, unsigned char kind, RestraintSide& s, DWORD now)
{
    std::string key, outer;
    void* lock = 0;
    unsigned char f = 0;
    int lv = 0;
    const int r = RestraintRead(c, kind, &key, &outer, &lock, &f, &lv);
    if (r != 1)
    {
        // none worn / not caged: the owner's next sight is sent at once; a copy keeps the owner's word (a word that came before the
        // caging, or a put-back's brief out moment, stays good - the key check judges another cage) and forgets what it saw here:
        // the seen-equal mark, the ask and the open reading - a cage it left open here and in the word stays a rescue for a moment
        if (r == 0)
        {
            if (mine) { s.word = false; return; }
            if (!s.seen && !s.openHere && s.ask == cooprison::kRestraintAskNone) return;
            if (cooprison::RestraintLeftOpenRescue(kind == cooprison::kPrisonCageLock, s.word, s.openHere, s.wFlags, s.wLevel))
                s.rescueUntil = (now + kRestraintRescueMs) | 1u;
            s.seen = false; s.openHere = false; s.ask = cooprison::kRestraintAskNone; s.askAt = 0;
            ++g_rsDropped;
        }
        return;
    }
    s.openHere = cooprison::LockOpen(f, lv);
    if (!mine) s.rescueUntil = 0;   // restrained again: no rescue window
    if (mine)
    {
        if (!cooprison::RestraintWordDue(s.word, s.wKey == key, s.wFlags, s.wLevel, f, lv, (unsigned int)(now - s.wAt), kRestraintResendMs)) return;
        if (!RestraintSend(uid, kind, key, outer, f, lv)) { ++g_rsWordSendFailed; return; }
        const bool change = !s.word || s.wKey != key || s.wFlags != f || s.wLevel != lv;
        s.word = true; s.wFlags = f; s.wLevel = lv; s.wKey = key; s.wAt = now;
        ++g_rsWordSent;
        if (change) RestraintLog("-> word uid=" + S((long long)uid) + " " + RestraintName(kind) + " '" + key + "' " + LockBitsText(f, lv) + " - our prisoner's lock, to every game");
        return;
    }
    if (!s.word || s.wKey != key) return;   // no word yet about THIS restraint (other shackles, another cage)
    if (s.ask == cooprison::kRestraintAskFresh && now - s.askAt >= kRestraintAskMs) s.ask = cooprison::kRestraintAskStale;
    const int d = cooprison::RestraintCopyDecide(true, s.wFlags, s.wLevel, f, lv, s.seen, s.ask);
    if (d == cooprison::kRestraintKeep)
    {
        if (f == s.wFlags && lv == s.wLevel) { s.seen = true; s.ask = cooprison::kRestraintAskNone; }
        return;
    }
    if (d == cooprison::kRestraintAsk)
    {
        if (!RestraintSend(uid, kind, key, outer, f, lv)) return;   // the link is down: asked again at the next scan
        s.ask = cooprison::kRestraintAskFresh; s.askAt = now;
        ++g_rsAskSent;
        RestraintLog("-> ask uid=" + S((long long)uid) + " " + RestraintName(kind) + " '" + key + "' opened here " + LockBitsText(f, lv)
                     + " while the owner's word is " + LockBitsText(s.wFlags, s.wLevel) + " - the owner is asked to open its own");
        return;
    }
    RestraintQueue(uid, kind, kRestraintOpWrite, s.wFlags, s.wLevel, key);
}
}   // namespace (P42)

void RestraintForget(unsigned int uid) { g_restraint.erase(uid); }

// P42, MAIN THREAD (ApplyRemotePrison): a SHACKLE LOCK / CAGE LOCK message. For our own character it is a copy's game's request
// (applied at the safe point when it opens a closed lock here); from the owner of a copy it is the owner's word.
void RestraintApplyRemote(const cooprison::PrisonMsg& m, unsigned int fromPeer)
{
    if (net::IsUidMine(m.uid))
    {
        ++g_rsAskRecv;
        RestraintLog("<- ask uid=" + S((long long)m.uid) + " " + RestraintName(m.kind) + " '" + m.cageKey + "' " + LockBitsText(m.lockFlags, m.lockLevel)
                     + " from game " + S((long long)fromPeer) + " - checked against our own at the safe point");
        RestraintQueue(m.uid, m.kind, kRestraintOpRequest, m.lockFlags, m.lockLevel, m.cageKey);
        return;
    }
    if (!net::UidOwnedByPeer(m.uid, fromPeer)) { ++g_rsNotOwner; return; }
    ++g_rsWordRecv;
    RestraintRec& r = g_restraint[m.uid];
    RestraintSide& s = (m.kind == cooprison::kPrisonShackleLock) ? r.sh : r.cage;
    const bool change = !s.word || s.wFlags != m.lockFlags || s.wLevel != m.lockLevel || s.wKey != m.cageKey;
    if (change)
    {
        s.ask = cooprison::kRestraintAskNone;
        s.seen = false;   // a new word: the copy takes it before any opening here counts as a pick
        RestraintLog("<- word uid=" + S((long long)m.uid) + " " + RestraintName(m.kind) + " '" + m.cageKey + "' " + LockBitsText(m.lockFlags, m.lockLevel)
                     + " - the owner's lock; our copy follows");
    }
    s.word = true; s.wFlags = m.lockFlags; s.wLevel = m.lockLevel; s.wKey = m.cageKey; s.wAt = ::GetTickCount();
}

// P42, MAIN THREAD (PoseApplyCage): the copy left a cage held on its owner's word - is it a rescue (prisonwire.h CageRescueRelease)?
bool RestraintCageRescue(unsigned int uid, bool sawCaged, bool oursOut, bool guardFresh, bool guardArrest, bool pardonLookOver)
{
    std::map<unsigned int, RestraintRec>::iterator it = g_restraint.find(uid);
    if (it == g_restraint.end()) return false;
    RestraintSide& s = it->second.cage;
    const DWORD now = ::GetTickCount();
    const bool window = s.rescueUntil != 0 && (int)(s.rescueUntil - now) > 0;   // RestraintWatch saw it leave the open cage
    const bool openHere = window || s.openHere;
    const bool wordOpen = window || (s.word && cooprison::LockOpen(s.wFlags, s.wLevel));
    const bool r = cooprison::CageRescueRelease(sawCaged, oursOut, guardFresh, guardArrest, pardonLookOver, openHere, wordOpen);
    if (r) s = RestraintSide();
    return r;
}

// P42, MAIN THREAD (PrisonTick): every kRestraintScanMs, each replicated character's shackles and cage lock.
void RestraintWatch()
{
    const DWORD now = ::GetTickCount();
    if (now - g_restraintScanAt < kRestraintScanMs) return;
    g_restraintScanAt = now;
    LARGE_INTEGER t0, t1, fq;
    ::QueryPerformanceCounter(&t0);
    for (std::map<unsigned int, RestraintRec>::iterator it = g_restraint.begin(); it != g_restraint.end(); )
    {
        ::Character* gc = FindSpawned(it->first);
        if (gc == 0 || !PlausibleObject(gc)) g_restraint.erase(it++); else ++it;
    }
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || uid == 0 || !PlausibleObject(c)) continue;
        const bool mine = net::IsUidMine(uid);
        std::map<unsigned int, RestraintRec>::iterator it = g_restraint.find(uid);
        if (it == g_restraint.end())
        {
            // nothing known yet: a record only for a character that wears shackles or sits in a cage
            std::string key, outer; void* lock = 0; unsigned char f = 0; int lv = 0;
            if (RestraintRead(c, cooprison::kPrisonShackleLock, &key, &outer, &lock, &f, &lv) != 1
                && RestraintRead(c, cooprison::kPrisonCageLock, &key, &outer, &lock, &f, &lv) != 1) continue;
            it = g_restraint.insert(std::make_pair(uid, RestraintRec())).first;
        }
        RestraintWatchOne(uid, c, mine, cooprison::kPrisonShackleLock, it->second.sh, now);
        RestraintWatchOne(uid, c, mine, cooprison::kPrisonCageLock, it->second.cage, now);
    }
    ::QueryPerformanceCounter(&t1);
    ::QueryPerformanceFrequency(&fq);
    ++g_rsScans;
    g_rsScanUsLast = (fq.QuadPart > 0) ? (long long)((t1.QuadPart - t0.QuadPart) * 1000000 / fq.QuadPart) : 0;
    if (g_rsScanUsLast > g_rsScanUsMax) g_rsScanUsMax = g_rsScanUsLast;
}

// P42, K2 SAFE POINT (PrisonSafePointDrain): the queued lock writes - a copy taking its owner's word, the owner opening its own on
// a copy's game's request, and the TEST-ONLY lever's writes and escape order. Each is read again here first.
void RestraintSafePointDrain()
{
    if (g_restraintOps.empty()) return;
    if (EngineWritesBlocked()) { g_restraintOps.clear(); return; }
    std::vector<RestraintOp> ops;
    ops.swap(g_restraintOps);
    for (size_t i = 0; i < ops.size(); ++i)
    {
        const RestraintOp& op = ops[i];
        ::Character* c = FindSpawned(op.uid);
        if (c == 0 || !PlausibleObject(c)) { ++g_rsGone; continue; }
        const bool mine = net::IsUidMine(op.uid);
        if (op.op == kRestraintOpLeverEscape)
        {
            const int e = (mine && PoseInSomethingPod(c) == 2) ? EscapeOrderPod(c) : 0;
            RestraintLog("locktest escape uid=" + S((long long)op.uid) + " result=" + (e == 1 ? "ordered" : (e < 0 ? "fault" : "refused (not ours or not caged)"))
                         + " - GET_OUT_OF_CAGE_ESCAPE as a player's order; TEST-ONLY lever P42");
            continue;
        }
        std::string key, outer;
        void* lock = 0;
        unsigned char f = 0;
        int lv = 0;
        if (RestraintRead(c, op.kind, &key, &outer, &lock, &f, &lv) != 1) { ++g_rsGone; continue; }
        unsigned char nf = op.flags;
        int nlv = op.level;
        const char* why = "";
        if (op.op == kRestraintOpLeverOpen || op.op == kRestraintOpLeverClose)
        {
            nf = (op.op == kRestraintOpLeverOpen) ? (unsigned char)(f & ~cooprison::kLockFlagLocked)
                                                  : (unsigned char)((f | cooprison::kLockFlagLocked) & ~cooprison::kLockFlagBroken);
            nlv = lv;
            why = "TEST-ONLY lever P42 (as a pick's result)";
        }
        else
        {
            if (key != op.key) { ++g_rsKeyMismatch; continue; }
            if (op.op == kRestraintOpRequest)
            {
                if (!mine) { ++g_rsNotOwner; continue; }
                if (!cooprison::RestraintOwnerAccepts(f, lv, op.flags, op.level))
                {
                    ++g_rsRefused;
                    RestraintLog("ask uid=" + S((long long)op.uid) + " " + RestraintName(op.kind) + " '" + key + "' REFUSED: ours is " + LockBitsText(f, lv)
                                 + ", asked " + LockBitsText(op.flags, op.level) + " (only a closed lock is opened on a request)");
                    continue;
                }
                nf = cooprison::RestraintOwnerOpened(f, op.flags);   // our own level stays
                nlv = lv;
                why = "a copy's game opened it (a rescuer) - our word follows";
            }
            else
            {
                if (mine) continue;   // the owner's word is written on copies only
                why = "the owner's word";
            }
        }
        if (nf == f && nlv == lv) continue;
        if (LockBitsWritePod(lock, nf, nlv) != 1) { ++g_rsWriteFailed; continue; }
        if (op.op == kRestraintOpRequest) ++g_rsAccepted; else if (op.op == kRestraintOpWrite) ++g_rsRewritten; else ++g_rsLever;
        RestraintLog("set uid=" + S((long long)op.uid) + (mine ? " (ours) " : " (copy) ") + RestraintName(op.kind) + " '" + key + "' "
                     + LockBitsText(f, lv) + " -> " + LockBitsText(nf, nlv) + " - " + why);
    }
}

std::string RestraintReportToken()
{
    return " restraint[wordSent,wordSendFailed,wordRecv,notOwner,askSent,askRecv,accepted,refused,rewritten,writeFailed,gone,keyMismatch,opsDropped,lever,rescueSent,tracked,copyDropped]="
         + S(g_rsWordSent) + "," + S(g_rsWordSendFailed) + "," + S(g_rsWordRecv) + "," + S(g_rsNotOwner) + "," + S(g_rsAskSent) + ","
         + S(g_rsAskRecv) + "," + S(g_rsAccepted) + "," + S(g_rsRefused) + "," + S(g_rsRewritten) + "," + S(g_rsWriteFailed) + ","
         + S(g_rsGone) + "," + S(g_rsKeyMismatch) + "," + S(g_rsOpsDropped) + "," + S(g_rsLever) + "," + S(g_cageRescueSent) + ","
         + S((long long)g_restraint.size()) + "," + S(g_rsDropped)
         + " restraintScan[passes,usLast,usMax]=" + S(g_rsScans) + "," + S(g_rsScanUsLast) + "," + S(g_rsScanUsMax);
}

// P42 TEST-ONLY lever `locktest`:
//   locktest read <uid>                         - one [LOCK] line: the character's shackles and cage lock here, and the word known
//   locktest open|close <uid> shackles|cage     - the lock opened (locked cleared) / closed (locked set, broken cleared) at the safe
//                                                 point, as a pick's result; on a copy that is a rescuer's pick (asked of the owner)
//   locktest escape <uid>                       - our caged character is ordered GET_OUT_OF_CAGE_ESCAPE at the safe point
std::string LockTestCommand(const std::string& arg)
{
    if (coop::GameWorldPtr() == 0) return "error locktest: no GameWorld";
    if (EngineWritesBlocked()) return "error locktest: engine writes are blocked (world loading or tearing down) - try again";
    char mode[16]; mode[0] = 0;
    char what[16]; what[0] = 0;
    unsigned int uid = 0;
    const int got = sscanf_s(arg.c_str(), "%15s %u %15s", mode, (unsigned)sizeof(mode), &uid, what, (unsigned)sizeof(what));
    if (got < 2 || uid == 0) return "error locktest usage: read <uid> | open|close <uid> shackles|cage | escape <uid>";
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) { RestraintLog("locktest " + std::string(mode) + " uid=" + S((long long)uid) + " REFUSED (not on this game)"); return "error locktest: not on this game"; }
    const std::string m(mode);
    if (m == "read")
    {
        std::string line = "locktest read uid=" + S((long long)uid) + " mine=" + S((long long)(net::IsUidMine(uid) ? 1 : 0))
                         + " inSomething=" + S((long long)PoseInSomethingPod(c));
        for (int k = 0; k < 2; ++k)
        {
            const unsigned char kind = (k == 0) ? cooprison::kPrisonShackleLock : cooprison::kPrisonCageLock;
            std::string key, outer; void* lock = 0; unsigned char f = 0; int lv = 0;
            const int r = RestraintRead(c, kind, &key, &outer, &lock, &f, &lv);
            line += std::string(" ") + RestraintName(kind) + "=" + (r == 1 ? "'" + key + "':" + LockBitsText(f, lv) : (r == 0 ? std::string("none") : std::string("unreadable")));
            std::map<unsigned int, RestraintRec>::const_iterator it = g_restraint.find(uid);
            const RestraintSide* s = (it == g_restraint.end()) ? 0 : (k == 0 ? &it->second.sh : &it->second.cage);
            line += std::string(" word=") + ((s != 0 && s->word) ? "'" + s->wKey + "':" + LockBitsText(s->wFlags, s->wLevel) : std::string("none"));
        }
        RestraintLog(line);
        return "ok " + line;
    }
    if (m == "escape")
    {
        if (!RestraintQueue(uid, 0, kRestraintOpLeverEscape, 0, 0, std::string())) return "error locktest: queue full";
        return "ok locktest escape queued";
    }
    if ((m == "open" || m == "close") && (std::string(what) == "shackles" || std::string(what) == "cage"))
    {
        const unsigned char kind = (std::string(what) == "shackles") ? cooprison::kPrisonShackleLock : cooprison::kPrisonCageLock;
        if (!RestraintQueue(uid, kind, m == "open" ? kRestraintOpLeverOpen : kRestraintOpLeverClose, 0, 0, std::string())) return "error locktest: queue full";
        RestraintLog("locktest " + m + " uid=" + S((long long)uid) + " " + what + " queued for the safe point - TEST-ONLY lever P42");
        return "ok locktest " + m + " queued";
    }
    return "error locktest usage: read <uid> | open|close <uid> shackles|cage | escape <uid>";
}

void PrisonTick()
{
    if (EngineWritesBlocked()) return;
    PrisonWatchCopies();
    RestraintWatch();   // P42
    if (!g_prisonIn.empty()) PrisonApplyOwn(::GetTickCount());
    if (!g_bedIn.empty()) BedApplyOwn(::GetTickCount());
    for (std::map<unsigned int, std::string>::iterator it = g_prisonReleaseIn.begin(); it != g_prisonReleaseIn.end(); ++it)   // arrest3
    {
        ::Character* c = FindSpawned(it->first);
        if (c != 0 && PlausibleObject(c) && net::IsUidMine(it->first) && PoseInSomethingPod(c) == 2)
        {
            void* fac = 0;
            int mode = 0;
            if (it->second == "@player") mode = 2;
            else if (!it->second.empty() && coop::GameWorldPtr() != 0 && PlausiblePtr(coop::GameWorldPtr()->factionDirectory))
            {
                fac = coop::GameWorldPtr()->factionDirectory->findFactionById(it->second);
                if (fac != 0 && PlausibleObject(fac)) mode = 1; else { fac = 0; ++g_releaseFacUnknown; }
            }
            PrisonQueueOp(it->first, false, 0, true, true, fac, mode);
        }
        else ++g_prisonReleaseNotCaged;
    }
    g_prisonReleaseIn.clear();
}

int PrisonTestSentence(float hours)
{
    int n = 0;
    for (GuardCagedMap::iterator it = g_guardCaged.begin(); it != g_guardCaged.end(); ++it)
    {
        ::Character* c = FindSpawned(it->first);
        if (c == 0 || !PlausibleObject(c) || net::IsUidMine(it->first) || PoseInSomethingPod(c) != 2) continue;
        if (PrisonSentenceSetPod(c, hours) == 1) { ++n; it->second.hours = hours; }   // jail2: a put-back keeps the test's sentence
    }
    PrisonLog("crimetest sentence " + S((long long)(hours * 100.0f)) + "/100 h on " + S((long long)n) + " copies this game's guard jailed");
    return n;
}

// review-arrest3 M1: every prison mark and queue is a claim about characters of the world that is going.
void PrisonWorldTeardown()
{
    g_poseCaged.clear(); g_cageOutQueued.clear(); g_cageQueued.clear(); g_releaseHold.clear(); g_guardCaged.clear();
    g_outNoPardon.clear(); g_prisonIn.clear(); g_prisonReported.clear(); g_prisonReleaseIn.clear(); g_prisonOps.clear();
    g_bedAsk.clear(); g_bedCarriedAt.clear(); g_bedRefusedKey.clear(); g_bedIn.clear(); g_bedOutForCarry.clear();
    g_bedHandoffs.clear(); g_bedTempRefused.clear(); g_bedOutForCarryFails.clear();
    g_restraint.clear(); g_restraintOps.clear();   /* P42 */
}

std::string PrisonReportToken()
{
    return " arrest2[sent,sendNoKey,recv,notMine,dropped,caged,already,noCage,noSlot,stillCarried,failed,locked,pending,sentenced,sentenceFailed,opsDropped,noHand,copyCageInExec]="
         + S(g_prisonSent) + "," + S(g_prisonSendNoKey) + "," + S(g_prisonRecv) + "," + S(g_prisonNotMine) + ","
         + S(g_prisonDropped) + "," + S(g_prisonCaged) + "," + S(g_prisonAlready) + "," + S(g_prisonNoCage) + ","
         + S(g_prisonNoSlot) + "," + S(g_prisonStillCarried) + "," + S(g_prisonFailed) + "," + S(g_prisonLocked) + ","
         + S((long long)g_prisonIn.size()) + "," + S(g_prisonSentenced) + "," + S(g_prisonSentenceFailed) + "," + S(g_prisonOpsDropped)
         + "," + S(g_prisonNoHand) + "," + S(g_poseCageInExec)
         + " arrest3[releaseSent,releaseSendFailed,releaseRecv,released,releaseNotCaged,releaseFailed,refusedSent,refusedRecv,refusedApplied,releaseHoldNow]="
         + S(g_prisonReleaseSent) + "," + S(g_prisonReleaseSendFailed) + "," + S(g_prisonReleaseRecv) + "," + S(g_prisonReleased)
         + "," + S(g_prisonReleaseNotCaged) + "," + S(g_prisonReleaseFailed) + "," + S(g_prisonRefusedSent) + ","
         + S(g_prisonRefusedRecv) + "," + S(g_prisonRefusedApplied) + "," + S((long long)g_releaseHold.size())
         + " arrest3b[guardCagedNow,copyHoldSentence,copyHoldSentenceFailed,copyLeftNotReleased,releaseNoClock,releaseFacUnknown,releaseOrdered,releaseOrderFailed,refusedPardon,ownSentenceMargin,guardArrestKept,guardSentenceRestored,guardKeptOutsCapped]="
         + S((long long)g_guardCaged.size()) + "," + S(g_copyHoldSentence) + "," + S(g_copyHoldSentenceFailed) + ","
         + S(g_copyLeftNotReleased) + "," + S(g_releaseNoClock) + "," + S(g_releaseFacUnknown) + "," + S(g_releaseOrdered) + ","
         + S(g_releaseOrderFailed) + "," + S(g_refusedPardon) + "," + S(g_ownSentenceMargin) + "," + S(g_guardArrestKept)
         + "," + S(g_guardSentenceRestored) + "," + S(g_guardKeptOutsCapped)
         + " bed[sent,recv,applied,refused,noAnswer,leftUnexplained]=" + S(g_bedSent) + "," + S(g_bedRecv) + "," + S(g_bedApplied)
         + "," + S(g_bedRefused) + "," + S(g_bedNoAnswer) + "," + S(g_bedLeftUnexplained)
         + " bed2[refusalStale,notAtSpot,copyMoved,copyNotMoved,outForCarryQueued,outForCarryDone,outForCarryFailed]="
         + S(g_bedRefusalStale) + "," + S(g_bedNotAtSpot) + "," + S(g_bedCopyMoved) + "," + S(g_bedCopyNotMoved)
         + "," + S(g_bedOutForCarryQueued) + "," + S(g_bedOutForCarryDone) + "," + S(g_bedOutForCarryFailed)
         + " bed3[dropPosted,dropWaited,dropNotConsumed,dropStillRagdoll,slidAway,handoffSuperseded,handoffFull,handoffsNow,copyDropPosted,copyDropTimeout,cooldownStarted,cooldownTakeOut,outForCarryGaveUp]="
         + S(g_bedDropPosted) + "," + S(g_bedDropWaited) + "," + S(g_bedDropNotConsumed) + "," + S(g_bedDropStillRagdoll)
         + "," + S(g_bedSlidAway) + "," + S(g_bedHandoffSuperseded) + "," + S(g_bedHandoffFull) + "," + S((long long)g_bedHandoffs.size())
         + "," + S(g_bedCopyDropPosted) + "," + S(g_bedCopyDropTimeout) + "," + S(g_bedCooldownStarted) + "," + S(g_bedCooldownTakeOut)
         + "," + S(g_bedOutForCarryGaveUp)
         + RestraintReportToken();   /* P42 */
}

void InstallRagdollRest()
{
    if (kRagdollRescueRva == 0 || kRagdollTranslateRva == 0 || kNotifyRagdollNeedsAnUpdateRva == 0)
    {
        ErrorLog("[R3] rest hook NOT installed - RagdollRescue / RagdollTranslate / NotifyRagdollNeedsAnUpdate missing from the address table");
        return;
    }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    g_ragdollTranslate = (RagdollTranslateFn)(base + (uintptr_t)kRagdollTranslateRva);
    g_notifyRagdoll    = (NotifyRagdollFn)(base + (uintptr_t)kNotifyRagdollNeedsAnUpdateRva);
    if (kRagdollTickRva != 0 && kRagdollTickArgRva != 0)   // R3-c
    {
        g_ragdollTick    = (RagdollTickFn)(base + (uintptr_t)kRagdollTickRva);
        g_ragdollTickArg = (const float*)(base + (uintptr_t)kRagdollTickArgRva);
    }
    else ErrorLog("[R3] RagdollTick / RagdollTickArg missing from the address table - a moved ragdoll's node is not refreshed");
    coop::HookStatus st = coop::AddHook((void*)(base + (uintptr_t)kRagdollRescueRva),
                                                  (void*)&detour_ragdollRescue, (void**)&g_origRagdollRescue);
    if (st == coop::SUCCESS && g_origRagdollRescue != 0) InterlockedExchange(&g_restHookArmed, 1);
    DebugLog(std::string("[R3] rest hook: below-ground rescue 0x7D38D0 AddHook ")
             + (st == coop::SUCCESS ? "SUCCESS" : "FAILED")
             + (g_restHookArmed != 0 ? " (armed)" : " (NOT armed - no copy's ragdoll will be moved)"));
}

bool RagdollRestHookArmed()
{
    return g_restHookArmed != 0;
}

std::string RestReportToken()
{
    return " restSent=" + S(g_restSent) + " restRecorded=" + S(g_restRecorded)
         + " restApplied=" + S((long long)g_restApplied) + " restLanded=" + S(g_restLanded)
         + " restLandFramesMax=" + S(g_restLandFramesMax) + " restLandFrames=le1:" + S(g_restLandFrames[0])
         + "/le10:" + S(g_restLandFrames[1]) + "/le60:" + S(g_restLandFrames[2]) + "/gt60:" + S(g_restLandFrames[3])
         + " restNodeRefreshed=" + S((long long)g_restNodeRefreshed)
         + " restMissed=" + S(g_restMissed) + " restJudgedGone=" + S(g_restJudgedGone) + " restSkippedCarried=" + S(g_restSkippedCarried)
         + " restSlid=" + S(g_restSlid)
         + " restSkippedNotReady=" + S(g_restSkippedNotReady) + " restSkippedNotSettled=" + S(g_restSkippedNotSettled)
         + " restWishes=" + S((long long)g_restWant.size()) + " restTableFull=" + S((long long)g_restTable.full)
         + " restHookArmed=" + S((long long)g_restHookArmed)
         + " restPostBusy=" + S((long long)g_restTable.busy)
         + " restAbortedCarried=" + S((long long)g_restAbortedCarried)
         + " restAbortedInactive=" + S((long long)g_restAbortedInactive);
}

// --- P011: order-lifecycle watch (DIAGNOSTIC ONLY, changes no behaviour) --------------
//
// F111: every run so far has read the goal ONLY ~2 minutes after the order - T027 at
// +118 s, T030 at +135 s. The first seconds of an order's life have never been observed
// in this project, and that is exactly the window in which a plannable order is accepted,
// (possibly) substituted by the task metadata table (F081 risk 2), acted on, and cleared.
// A single late sample cannot distinguish "never adopted" from "adopted and finished".
//
// This samples on the main pump and logs ONLY when the goal string or hasPendingOrders changes,
// so a stable goal costs one line, not thousands. The attempt cap for the duel symptom is
// reached (F110), so this run is instrumentation - no fix is being attempted.
namespace {

struct TaskWatch
{
    unsigned int uid;
    unsigned int endTick;
    unsigned int startTick;
    bool         active;
    bool         primed;      // has a first sample been logged yet
    int          lastOrders;
    std::string  lastGoal;
};

TaskWatch g_watch[2];

void WatchSample(TaskWatch& w, const char* why)
{
    Character* c = FindSpawned(w.uid);
    if (c == 0 || !PlausibleObject(c)) return;
    void* ai = GetCharacterAI(c);
    if (!PlausibleObject(ai)) return;
    AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObject(ts)) return;

    std::string goal   = ts->describeCurrentGoal();
    int         orders = ts->hasPendingOrders() ? 1 : 0;
    if (w.primed && goal == w.lastGoal && orders == w.lastOrders) return;

    Ogre::Vector3 p = c->worldPosition();
    unsigned int ms = ::GetTickCount() - w.startTick;
    DebugLog("[P011] uid=" + S(w.uid) + " t+" + S((long long)ms) + "ms " + why
             + " goal='" + goal + "' hasOrders=" + S(orders)
             + " pos=" + F1(p.x) + "," + F1(p.y) + "," + F1(p.z));
    w.lastGoal   = goal;
    w.lastOrders = orders;
    w.primed     = true;
}

} // namespace

void WatchTask(unsigned int uid, int seconds)
{
    if (seconds <= 0) seconds = 60;
    // Slot by uid so re-watching the same character restarts rather than duplicating.
    int slot = -1;
    for (int i = 0; i < 2; ++i) if (g_watch[i].active && g_watch[i].uid == uid) slot = i;
    if (slot < 0) for (int i = 0; i < 2; ++i) if (!g_watch[i].active) { slot = i; break; }
    if (slot < 0)
    {
        ErrorLog("[P011] watch refused: both slots busy - only 2 characters can be watched");
        return;
    }

    TaskWatch& w = g_watch[slot];
    w.uid        = uid;
    w.startTick  = ::GetTickCount();
    w.endTick    = w.startTick + (unsigned int)seconds * 1000u;
    w.active     = true;
    w.primed     = false;
    w.lastOrders = -1;
    w.lastGoal.clear();
    DebugLog("[P011] watching uid=" + S(uid) + " for " + S(seconds)
             + "s - logging every CHANGE of goal/hasOrders (diagnostic only)");
    WatchSample(w, "START");
}

void WatchTaskTick()
{
    for (int i = 0; i < 2; ++i)
    {
        TaskWatch& w = g_watch[i];
        if (!w.active) continue;
        WatchSample(w, "");
        if ((int)(::GetTickCount() - w.endTick) >= 0)
        {
            WatchSample(w, "END");
            DebugLog("[P011] watch on uid=" + S(w.uid) + " finished");
            w.active = false;
        }
    }
}

void ReportTask(unsigned int uid)
{
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c))
    {
        DebugLog("[M1] tasks: no local object for uid " + S(uid));
        return;
    }

    // Character -> AI (getBrain) -> AITaskSytem (AI+0x20) -> describeCurrentGoal().
    // Settles H005 risk 2 by INSPECTION rather than by inference from where the puppet
    // walked: it names the goal the engine actually adopted, including any substitution
    // the task metadata table may have applied (F081).
    void* ai = GetCharacterAI(c);
    if (!PlausibleObject(ai))
    {
        DebugLog("[M1] tasks: AI unreadable for uid " + S(uid));
        return;
    }
    AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObject(ts))
    {
        DebugLog("[M1] tasks: task system unreadable for uid " + S(uid));
        return;
    }

    Ogre::Vector3 p = c->worldPosition();
    DebugLog("[M1] tasks uid=" + S(uid) + " goal='" + ts->describeCurrentGoal() + "'"
             + " pos=" + F1(p.x) + "," + F1(p.y) + "," + F1(p.z)
             + " hasOrders=" + S(ts->hasPendingOrders() ? 1 : 0));
}

/* P11: ReportTask's reads, handed back (capturetest's read-back). */
bool TaskGoalOf(unsigned int uid, std::string* goal, int* hasPendingOrders)
{
    if (goal) goal->clear();
    if (hasPendingOrders) *hasPendingOrders = -1;
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObject(c)) return false;
    void* ai = GetCharacterAI(c);
    if (!PlausibleObject(ai)) return false;
    AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObject(ts)) return false;
    if (goal) *goal = ts->describeCurrentGoal();
    if (hasPendingOrders) *hasPendingOrders = ts->hasPendingOrders() ? 1 : 0;
    return true;
}

// ============================================================================================
// M-B / P064 (H025) - CONTEXT: what a town NPC carries (npc-context.md sections 2, 6) and whether the
// identities resolve on the other instance. Towns and squad templates go by GameData stringID; the home
// building goes as its raw handle (per-process slots: H025 asks whether two processes that loaded the
// same save mint the same ones) plus the building's position so a resolved-but-different building shows.
// ============================================================================================
namespace {

unsigned long long kTownListRva = 0; static coop::AddrReg kTownListRva_reg("TownList", &kTownListRva);   /* P8h: the address table fills this. Steam_1.0.65 0x21330A0 */   // DAT_1421330A0 - a POINTER to the global TownList (T121)
unsigned long long kHandleManagerRva = 0; static coop::AddrReg kHandleManagerRva_reg("HandleManager", &kHandleManagerRva);   /* P8h: the address table fills this. Steam_1.0.65 0x2132F30 */   // HandleManager global (object); +0x20 = zones
unsigned long long kGetTownBySidRva = 0; static coop::AddrReg kGetTownBySidRva_reg("GetTownBySid", &kGetTownBySidRva);   /* P8h: the address table fills this. Steam_1.0.65 0x928330 */    // Town* TownList::getTownBySID(const std::string&) - resolver-verified
unsigned long long kGetBuildingRva = 0; static coop::AddrReg kGetBuildingRva_reg("GetBuilding", &kGetBuildingRva);   /* P8h: the address table fills this. Steam_1.0.65 0xD5F30 */     // Building* HandleManager::asBuilding(const hand&) - resolver-verified
typedef void* (*GetTownBySidFn)(void* townList, const std::string& sid);
typedef void* (*GetBuildingFn)(void* handleMgr, const hand& h);

// H027 - a hand rebuilt LOCALLY from the five fields on the wire (type +8, container +C, containerStamp +10,
// index +14, serial +18): the raw bytes also carry the sending process's vtable pointer, which must not be used.
hand LocalHandFromRaw(const unsigned char* raw)
{
    const unsigned int* w = (const unsigned int*)raw;
    return hand(w[5] /*index*/, w[6] /*serial*/, (itemType)w[2] /*type*/, w[3] /*container*/, w[4] /*containerStamp*/);
}
bool RawHandIsEmpty(const unsigned char* raw)
{
    const unsigned int* w = (const unsigned int*)raw;
    return w[5] == 0 && w[6] == 0;
}
::Character* SafeHandToCharacter(const hand& h)
{
    __try { return h.getCharacter(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// MSVC std::string at `s`: buf[16] | ptr at +0, size at +0x10. POD reader under SEH.
bool ReadStdStringPod(const void* s, char* out, int cap)
{
    __try
    {
        const size_t size = *(const size_t*)((const char*)s + 0x10), res = *(const size_t*)((const char*)s + 0x18);
        if (size >= (size_t)cap) return false;
        const char* p = (res >= 16) ? *(const char* const*)s : (const char*)s;   // review-p4a HIGH-3: heap iff capacity >= 16
        if (p == 0) return false;
        std::memcpy(out, p, size); out[size] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadContextPod(::Character* c, void** platoonOut, void** squadGdOut, int* squadTypeOut, void** townOut,
                    unsigned char* handRaw32, unsigned char* charRaw32, int* memberTypeOut)
{
    __try
    {
        std::memcpy(charRaw32, (const char*)c + 0x58, 32);        // H027: the character's own hand
        ActivePlatoon* ap = (ActivePlatoon*)coop::SquadActivePlatoonOf(c);   /* review M2: +0x658 read and verified (handoff.cpp) - never getSquad 0x790F70, which CREATES a squad when +0x658 is null; this runs from the 1 Hz hand-off tick */
        if (ap == 0) return false;
        char* platoon = *(char**)((char*)ap + 0x78);
        if (platoon == 0) return false;
        *platoonOut   = platoon;
        *squadGdOut   = *(void**)(platoon + 0x108);
        *squadTypeOut = *(int*)(platoon + 0xA8);
        char* own = platoon + 0x148;                          // embedded Ownerships
        *townOut = *(void**)(own + 0x30);
        std::memcpy(handRaw32, own + 0x38, 32);               // hand: vtbl, type, container, containerStamp, index, serial
        void* ai = *(void**)((char*)c + 0x650);
        char* ts = ai ? *(char**)((char*)ai + 0x20) : 0;
        *memberTypeOut = ts ? *(int*)(ts + 0x25C) : -1;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadGameDataSid(const void* gd, char* out, int cap)
{
    if (gd == 0) return false;
    return ReadStdStringPod((const char*)gd + 0x58, out, cap);   // GameData::stringID
}
/* P8a: RENAMED from ReadTownSid.  The body was never about towns - it is a guarded
   RootObjectBase::getRecord() -> GameData::stringID read, and `roster full` asks it about a home BUILDING as
   well as a town.  One body with two names would be the hand this project removes (lesson 11), so the name is
   corrected at its two call sites instead. */
bool ReadObjectSid(void* obj, char* out, int cap)
{
    if (obj == 0) return false;
    void* gd = 0;
    __try { gd = ((RootObjectBase*)obj)->getRecord(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ReadGameDataSid(gd, out, cap);
}
void* ResolveBuilding(const unsigned char* handRaw32)
{
    __try
    {
        const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
        GetBuildingFn fn = (GetBuildingFn)(base + kGetBuildingRva);
        const hand h = LocalHandFromRaw(handRaw32);   // H027: never use the wire's vtable pointer
        return fn((void*)(base + kHandleManagerRva), h);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool ObjectPosition(void* obj, float* x, float* y, float* z)
{
    __try
    {
        Ogre::Vector3 p = ((RootObjectBase*)obj)->getPosition();
        *x = p.x; *y = p.y; *z = p.z;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* TryTownLookup(void* townList, const std::string& sid, bool* faulted)
{
    *faulted = false;
    __try
    {
        GetTownBySidFn fn = (GetTownBySidFn)((uintptr_t)::GetModuleHandleA(0) + kGetTownBySidRva);
        return fn(townList, sid);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = true; return 0; }
}
// T120: the object form faulted and my single SEH frame never reached the pointer form. One frame per attempt.
void* ResolveTown(const std::string& sid, int* how)
{
    *how = 0;
    if (sid.empty()) return 0;
    // T121 (F434): DAT_1421330A0 is a POINTER to the TownList - the object form faulted on every call and is gone.
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    bool f2 = false;
    void* tl2 = 0;
    __try { tl2 = *(void**)(base + kTownListRva); } __except (EXCEPTION_EXECUTE_HANDLER) { tl2 = 0; }
    void* r = 0;
    if (PlausibleObject(tl2)) { r = TryTownLookup(tl2, sid, &f2); if (r) { *how = 2; return r; } }
    *how = f2 ? -2 : 0;   // -2 the lookup faulted, 0 not found
    return 0;
}
unsigned long long kBuildingGetTownRva = 0; static coop::AddrReg kBuildingGetTownRva_reg("BuildingGetTown", &kBuildingGetTownRva);   /* P8h: the address table fills this. Steam_1.0.65 0xF6BE0 */   // TownBase* Building::asTown() - resolver-verified
typedef void* (*BuildingGetTownFn)(void* building);
void* BuildingGetTown(void* building)
{
    if (building == 0) return 0;
    __try { return ((BuildingGetTownFn)((uintptr_t)::GetModuleHandleA(0) + kBuildingGetTownRva))(building); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
long long g_ctxSent = 0, g_ctxSendFailed = 0, g_ctxRecv = 0;
// M-B / H026 - the apply side.
struct PendingContext { std::string squadSid, townSid, platoonId; int squadType, memberType; unsigned char hand[32]; unsigned char chand[32]; float bx, by, bz; };
std::map<unsigned int, PendingContext> g_pendingCtx;
bool PendingCtxHasUid(unsigned int uid) { return g_pendingCtx.find(uid) != g_pendingCtx.end(); }   // M4 fold: AllocateUid's row check (declared above it, same namespace)
std::map<std::string, void*> g_ctxPlatoons;   // key: id|<squad id>|<faction> (ctxkey.h, CtxIdKey); older senders: squadSid|hand index:serial|townSid -> Platoon*
std::map<std::string, int> g_ctxPlatoonSlot;  // the same key -> the slot of the player whose character was last built into it (-1 unknown)
long long g_twinAdopted = 0, g_twinLookupFailed = 0, g_twinPlaced = 0;   // H027
double g_twinPlacedDist = 0.0;
long long g_ctxApplied = 0, g_ctxApplyNoContext = 0, g_ctxApplyNoSquad = 0, g_ctxApplyNoFaction = 0, g_ctxPlatoonsCreated = 0, g_ctxPlatoonsReused = 0,
          g_ctxPlatoonCreateFailed = 0, g_ctxMemberTypeSet = 0, g_ctxTownFromBuilding = 0, g_ctxTownFromSid = 0, g_ctxNoTown = 0;
unsigned long long kSetSquadTypeRva = 0; static coop::AddrReg kSetSquadTypeRva_reg("SetSquadType", &kSetSquadTypeRva);   /* P8h: the address table fills this. Steam_1.0.65 0x9C35E0 */   // void Platoon::setSquadType(SquadType)
unsigned long long kSetHomeTownRva = 0; static coop::AddrReg kSetHomeTownRva_reg("SetHomeTown", &kSetHomeTownRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7EBD30 */   // void Ownerships::setHomeTown(TownBase*, SquadType)
unsigned long long kSetHomeBuildingRva = 0; static coop::AddrReg kSetHomeBuildingRva_reg("SetHomeBuilding", &kSetHomeBuildingRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7EBE40 */  // void Ownerships::setHomeBuilding(const hand&, SquadType)
typedef void (*SetSquadTypeFn)(void* platoon, int st);
typedef void (*SetHomeTownFn)(void* ownerships, void* town, int st);
typedef void (*SetHomeBuildingFn)(void* ownerships, const hand& h, int st);
// P1b (b): after the host takes a squad over, the client's own copy is asleep under the same id. The host's live
// announcement is the truth: the sleeping copy is destroyed and the context copy created fresh (no duplicate).
static int PlatoonAsleepPod(void* platoon)
{
    __try { return (*(void**)((char*)platoon + 0x1D8) == 0 && *(void**)((char*)platoon + 0x1E0) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int DestroyPlatoonPodImpl(Faction* f, void* platoon)
{
    __try { f->removeSquad((Platoon*)platoon); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
long long g_ctxSupersededAsleep = 0, g_ctxSupersedeFailed = 0;
long long g_ctxSupersedeOwnIdRefused = 0; std::set<std::string> g_ctxOwnIdRefusedSeen;   /* inv7a3 (review-inv7a2 HIGH) */
long long g_ctxHandedOverKeptMine = 0; std::set<std::string> g_ctxHandedOverKeptMineSeen;   /* M7a3f4 item 1 [m7a3f4-sc1]: a handed-over squad's own awake copy kept - a person this game runs is in it */
long long g_ctxHandedOverSupersede = 0; std::set<std::string> g_ctxHandedOverSeen;   /* M7a3f3 T-425 [m7a3f3-sc1]: my own id, but a squad I handed over */
std::set<std::string> g_ctxAwakeSameIdSeen; long long g_ctxAwakeSameId = 0, g_ctxRetiredAwake = 0, g_ctxRetireFailed = 0, g_ctxRetireSkippedSession = 0;
/* inv7e2 (T418): the reloaded save's copies of the peer's squads - retired, withdrawn, or kept for an earlier generation */
long long g_staleRetiredReloaded = 0, g_staleWithdrawnReloaded = 0, g_staleKeptEarlierGen = 0; std::set<std::string> g_staleKeptSeen;
long long g_staleKeptNotReloaded = 0, g_staleKeptNoSlot = 0;   /* inv7e2 fold: this game did not reload since its last link-down / its slot is unknown */
/* inv7e2: classify a platoon's session members (standinpurge.h ReloadMemberClass) and decide. uids and nOut: the members read. */
static int ReloadVerdict(void* platoon, int mySlot, unsigned int* uids, int cap, int* nOut)
{
    *nOut = 0;
    const int n = PlatoonSessionMemberUids(platoon, uids, cap);
    if (n < 0) return coopsp::ReloadRetireDecide(0, 0, 0, 0, 0, 0);
    int thisGen = 0, earlier = 0, other = 0; const long cur = StoreWorldGenNow();   /* fold: the world-load generation */
    for (int i = 0; i < n; ++i)
    {
        long gen = -1; const int tracked = SweepAdoptGen(uids[i], &gen);
        const int c = coopsp::ReloadMemberClass(tracked, gen, cur, net::IsUidMine(uids[i]) ? 1 : 0);
        if (c == coopsp::kMemberAdoptedThisGen) ++thisGen; else if (c == coopsp::kMemberEarlierGen) ++earlier; else ++other;
    }
    *nOut = n;
    return coopsp::ReloadRetireDecide(1, mySlot >= 0 ? 1 : 0, coopsp::ReloadedSinceLinkDown(cur, StoreWorldGenAtLinkDown()), thisGen, earlier, other);
}
/* inv7e (run T412) - A CONTEXT SQUAD KEEPS THE OTHER GAME'S ID ACROSS A SAVE. The engine gives a new squad its own
   stringID (`<faction>_<n>`, Platoon+0x78), files it in the squad's state record (Platoon+0x40, stringFields "platoon
   stringID") and restores +0x78 from that entry on load (Platoon::loadStateData 0x7EC550, both arms - read from
   build/decomp_7ec550.txt). The other game's id lived only in the store's bind map, which teardown clears, so after
   leave -> load -> join this game's saved copies came back under their OWN ids and the P1c / Section-13 same-id
   supersede never matched them (T412: awakeSameId:0 on all 763 CONTEXT records; the town NPCs doubled). Carrying
   the id into BOTH places makes the saved copy answer to the other game's id after a load, so the first CONTEXT for
   that squad retires it - an event, no timer. Refused (counted) when another squad of the faction holds the id. */
long long g_staleIdCarried = 0, g_staleIdSkippedClash = 0, g_staleIdSkippedNoState = 0;
void* PlatoonStatePod(void* platoon)
{
    __try { return *(void**)((char*)platoon + 0x40); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int PlatoonIdWritePod(void* platoon, const std::string* id)   /* pointer in, no local objects: no C2712 */
{
    __try { *(std::string*)((char*)platoon + 0x78) = *id; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void CarryPeerPlatoonId(Faction* faction, void* platoon, const std::string& id)
{
    void* awake = FindAwakePlatoon(faction, id);
    const int clash = ((awake != 0 && awake != platoon) || FindSleepingPlatoon(faction, id) != 0) ? 1 : 0;
    GameData* state = (GameData*)PlatoonStatePod(platoon);
    const int d = coopsp::CarryIdDecide(id.empty() ? 1 : 0, PlausibleObject(state) ? 1 : 0, clash);
    if (d == coopsp::kCarrySkipEmpty) return;
    int r = d;
    if (r == coopsp::kCarryYes && PlatoonIdWritePod(platoon, &id) != 1) r = coopsp::kCarrySkipNoState;   /* the live field faulted */
    if (r == coopsp::kCarrySkipClash) ++g_staleIdSkippedClash;
    else if (r == coopsp::kCarrySkipNoState) ++g_staleIdSkippedNoState;
    else { state->stringFields["platoon stringID"] = id; ++g_staleIdCarried; }
    DebugLog("[STALE] context squad " + P(platoon) + " id='" + id + "': "
             + (r == coopsp::kCarryYes ? "carries the other game's id (live field + saved entry)" : (r == coopsp::kCarrySkipClash ? "NOT carried - another squad of the faction holds that id" : "NOT carried - no state record"))
             + " carried=" + S(g_staleIdCarried) + " skippedClash=" + S(g_staleIdSkippedClash) + " skippedNoState=" + S(g_staleIdSkippedNoState)
             + " | same-id supersede so far: retiredAwake=" + S(g_ctxRetiredAwake) + " supersededAsleep=" + S(g_ctxSupersededAsleep)
             + " retireSkippedSession=" + S(g_ctxRetireSkippedSession) + " retireFailed=" + S(g_ctxRetireFailed) + " ownIdRefused=" + S(g_ctxSupersedeOwnIdRefused) + " handedOverSupersede=" + S(g_ctxHandedOverSupersede) + " handedOverKeptMine=" + S(g_ctxHandedOverKeptMine) /* [m7a3f4-sc2] */ /* [m7a3f3-sc2] */
             + " retiredReloaded=" + S(g_staleRetiredReloaded) + " withdrawnReloaded=" + S(g_staleWithdrawnReloaded) + " keptEarlierGen=" + S(g_staleKeptEarlierGen)
             + " keptNotReloaded=" + S(g_staleKeptNotReloaded) + " keptNoSlot=" + S(g_staleKeptNoSlot));
}

void* ReadActivePlatoonPod(void* platoon)
{
    __try { return *(void**)((char*)platoon + 0x1D8); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool ConfigurePlatoonPod(void* platoon, int st, void* town, const unsigned char* handRaw, bool haveBuilding, void** activeOut)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    __try
    {
        *(int*)((char*)platoon + 0x118) = 0;                                   // messageOnActivation: createRandomSquad clears CM_EMPTY
        ((SetSquadTypeFn)(base + kSetSquadTypeRva))(platoon, st);
        void* own = (char*)platoon + 0x148;                                    // embedded Ownerships
        if (town != 0) ((SetHomeTownFn)(base + kSetHomeTownRva))(own, town, st);
        if (haveBuilding) { const hand lh = LocalHandFromRaw(handRaw); ((SetHomeBuildingFn)(base + kSetHomeBuildingRva))(own, lh, st); }
        *activeOut = *(void**)((char*)platoon + 0x1D8);                        // ActivePlatoon*
        return *activeOut != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int DestroyPlatoonPod(Faction* f, void* platoon) { StoreAdminDestroyBegin(); const int rc = DestroyPlatoonPodImpl(f, platoon); StoreAdminDestroyEnd(); return rc; }   // P4e: our destroys are admin, not "gone"
/* F667 (review-p8a H-3) - THE ONE RAW ENGINE CALL IN THIS FILE THAT HAD NO SEH FRAME, AND THE
   POPULATION IT IS MADE OVER WIDENED IN P8a.  Every other raw-RVA call here has its own __try;
   `roster full` copied the one exception rather than the rule, and it makes this call for EVERY
   entry of activeCharacters() - the never-announced, never-vetted town residents the verb
   exists to describe.  PlausibleObject proves only that the first qword points into the game
   image, not that the object is a Platoon, and the callee writes a std::string through the hidden
   return slot: a wrong-but-readable object yields a garbage size/capacity pair inside the engine's
   own CRT.  The out-parameter is a POINTER, so the C2712 rule is satisfied - the shape
   TryTownLookup already proves compiles - and the two call sites share ONE body.
   TWO COUNTERS, NOT ONE: a fault in the roster walk and a fault on the context-send path are
   different events with different repairs, and one number for both is the shape this project has
   already paid for (lesson 1). */
typedef std::string* (*PlatoonSidFn)(const void*, std::string*);
unsigned long long kPlatoonSidRva = 0; static coop::AddrReg kPlatoonSidRva_reg("PlatoonSid", &kPlatoonSidRva);   /* P8h: the address table fills this. Steam_1.0.65 0x384AF0 */
static bool PlatoonIdPod(void* platoon, std::string* out)
{
    __try { ((PlatoonSidFn)((uintptr_t)::GetModuleHandleA(0) + kPlatoonSidRva))(platoon, out); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
long long g_platoonIdFaultRoster = 0, g_platoonIdFaultCtx = 0;
long long g_ctxTownOk = 0, g_ctxTownMiss = 0, g_ctxBuildingOk = 0, g_ctxBuildingMiss = 0, g_ctxBuildingFar = 0, g_ctxSquadOk = 0, g_ctxSquadMiss = 0;

} // namespace

bool SendContextFor(unsigned int uid, ::Character* c)
{
    if (!PlausibleObject(c)) return false;
    void* platoon = 0; void* squadGd = 0; int squadType = -1; void* town = 0; unsigned char hraw[32], craw[32]; int memberType = -1;
    std::memset(hraw, 0, 32); std::memset(craw, 0, 32);
    if (!ReadContextPod(c, &platoon, &squadGd, &squadType, &town, hraw, craw, &memberType)) { ++g_ctxSendFailed; return false; }
    char sq[128], tn[128];
    std::string squadSid = ReadGameDataSid(squadGd, sq, 128) ? std::string(sq) : std::string();
    std::string townSid  = ReadObjectSid(town, tn, 128) ? std::string(tn) : std::string();
    float bx = 0, by = 0, bz = 0;
    void* bld = ResolveBuilding(hraw);
    if (bld == 0 || !ObjectPosition(bld, &bx, &by, &bz)) { bx = by = bz = 0.0f; }
    // P1: the platoon's string id is the squad's world id (the same save loads the same ids on both instances; F433/F436
    // for characters). std::string by value from a raw RVA: hidden return slot in RDX.
    std::string platoonId;
    if (PlausibleObject(platoon) && !PlatoonIdPod(platoon, &platoonId)) { platoonId.clear(); ++g_platoonIdFaultCtx; }   /* F667: the same guarded body as the roster's */
    if (!net::SendContext(uid, squadSid, squadType, townSid, hraw, craw, bx, by, bz, memberType, platoonId)) { ++g_ctxSendFailed; return false; }
    ++g_ctxSent;
    DebugLog("[CTX] -> uid=" + std::string(S(uid)) + " squad='" + squadSid + "' squadType=" + S(squadType) + " town='" + townSid
             + "' building=" + (bld ? "resolved" : "none") + " bpos=" + F1(bx) + "," + F1(by) + "," + F1(bz) + " member=" + S(memberType));
    return true;
}

/* P8a - `roster full`.  WHY IT EXISTS, from build/read-roster-t236.md: T236a measured 130 characters loaded on
   one game and on neither's wire, and the four things needed to classify them - squad, squadType, home town and
   the gate that refused them - exist in this build ONLY on the `[CTX] ->` line, which is written for characters
   that were adopted.  The 130 that mattered therefore had no squad, no squadType, no town and no cause anywhere
   in the logs, and the read had to classify them by character-template name.  This prints all of it per
   character, plus the uid, so the two games' rosters join EXACTLY instead of by position (median error 22 units).
   READ ONLY, and a COMMAND rather than a per-frame probe - a full world walk every frame is the runaway F189
   taught this project not to write.  MAIN THREAD.
   spawnCause CARRIES THREE VALUES AND SAYS SO ON ITS OWN LEGEND: `puppet` (the uid was minted by another game),
   `towngen` (this game's own createRandomUnloadedSquad hook allowed the group) and `unknown` (everything else -
   a save-loaded character, a bar resident, any other engine path).  Bar residents come through createRandomSquad
   0x582F80, which is not hooked, so there is no observation point for them and inventing one would be a number
   that reads as a measurement and is not (F077).  `towngen` against `unknown` is the split the read needed. */
void WorldRosterFull(int page)
{
    if (!PlausiblePtr(coop::GameWorldPtr()))
    {
        ErrorLog("[P031] roster full: no GameWorld - nothing to list (this is a REFUSAL, not an empty roster)");
        return;
    }
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    const size_t n = all.size();
    if (n > 20000)
    {
        ErrorLog("[P031] roster full: update list size " + S((long long)n) + " is implausible - refusing to walk it");
        return;
    }
    if (page < 1) page = 1;
    {
        const int kRosterPage = 64;   /* the same page size boxlist uses, so one habit covers every listing verb */
        std::map<std::string, std::string> byKey;
        long long total = 0, unreadable = 0, mineN = 0, puppetN = 0, noUidN = 0, ctxFail = 0, towngenN = 0, bldResolved = 0;
        for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            ::Character* c = *it;
            if (!PlausibleObject(c)) { ++unreadable; continue; }
            ++total;
            {
                const unsigned int uid = FindSpawnedUid(c);
                const int mine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
                const int puppet = (uid != 0 && mine == 0) ? 1 : 0;
                if (mine) ++mineN; if (puppet) ++puppetN; if (uid == 0) ++noUidN;
                {
                    ObjId oid; std::string handle("NOHANDLE");
                    if (CaptureObjId(c, &oid)) handle = ObjIdString(oid);
                    {
                        void* platoon = 0; void* squadGd = 0; int squadType = -1; void* town = 0; int memberType = -1;
                        unsigned char hraw[32], craw[32];
                        std::memset(hraw, 0, 32); std::memset(craw, 0, 32);
                        std::string squadSid, townSid, platoonId, bldSid("-");
                        if (!ReadContextPod(c, &platoon, &squadGd, &squadType, &town, hraw, craw, &memberType)) ++ctxFail;
                        else
                        {
                            char sq[128], tn[128];
                            if (ReadGameDataSid(squadGd, sq, 128)) squadSid = sq;
                            if (ReadObjectSid(town, tn, 128)) townSid = tn;
                            /* The platoon's id string through the engine's own guarded accessor, exactly as
                               SendContextFor reads it - the roster's towngen test is a string compare on it. */
                            if (PlausibleObject(platoon) && !PlatoonIdPod(platoon, &platoonId))
                            { platoonId.clear(); ++g_platoonIdFaultRoster; }   /* F667: an empty spawnCause is now distinguishable from a fault */
                            {
                                /* The HOME BUILDING behind Ownerships::_homeBuilding, resolved through the
                                   engine's handle manager the way [CTX] does, and printed as its BASE RECORD id
                                   - `-` when the hand resolves nothing, which is what T236a saw for all 56 of
                                   Sho-Battai's residents (F634). */
                                void* bld = ResolveBuilding(hraw);
                                if (bld != 0 && PlausibleObject(bld))
                                {
                                    char bs[128];
                                    if (ReadObjectSid(bld, bs, 128)) { bldSid = bs; ++bldResolved; }
                                    else bldSid = "resolved(no id)";
                                }
                            }
                        }
                        {
                            const int fromTownGen = (!platoonId.empty() && TownGenMadeThisPlatoon(platoonId.c_str()) != 0) ? 1 : 0;
                            if (fromTownGen) ++towngenN;
                            {
                                const char* cause = puppet ? "puppet" : (fromTownGen ? "towngen" : "unknown");
                                std::string race("?"), fac("?"), pos("UNREADABLE"), sect("-");
                                GameData* gd = c->getRecordDirect();
                                if (PlausibleObject(gd)) race = gd->name;
                                Faction* f = c->getOwnerFactionDirect();
                                if (PlausibleObject(f)) fac = f->getName();
                                if (PlausibleObject(*(void**)((char*)c + 0x448)))
                                {
                                    Ogre::Vector3 p = c->worldPosition();
                                    pos = F1(p.x) + "," + F1(p.y) + "," + F1(p.z);
                                    sect = SectorString(SectorOf(p.x, p.z));
                                }
                                {
                                    char ukey[32]; _snprintf(ukey, 31, "%c%010u", uid != 0 ? '1' : '0', uid); ukey[31] = 0;
                                    /* SORTED BY UID, then by handle.  T236a's handles matched 0 of 270 across the
                                       two games (different saves, F631) while every uid matched exactly, so the uid
                                       is the key that makes the two rosters diff line for line. */
                                    const std::string key = std::string(ukey) + "|" + handle;
                                    std::string line = " uid=" + S((long long)uid)
                                                     + " mine=" + S((long long)mine) + " puppet=" + S((long long)puppet)
                                                     + " squad='" + squadSid + "' squadType=" + S((long long)squadType)
                                                     + " town='" + townSid + "' building=" + bldSid
                                                     + " sector=" + sect
                                                     + " spawnCause=" + std::string(cause)
                                                     + " adoptGate=" + std::string(AdoptGateFor(c))
                                                     + " race='" + race + "' fac='" + fac + "' pos=" + pos
                                                     + " handle=" + handle;
                                    if (byKey.find(key) != byKey.end()) byKey[key] += "  <-- DUPLICATE KEY, also:" + line;
                                    else byKey[key] = line;
                                }
                            }
                        }
                    }
                }
            }
        }
        {
            const long long rows = (long long)byKey.size();
            const long long pages = (rows <= 0) ? 1 : ((rows + kRosterPage - 1) / kRosterPage);
            const long long first = (long long)(page - 1) * (long long)kRosterPage;
            long long idx = 0, printed = 0;
            DebugLog("[P031] rf tally page=" + S((long long)page) + " of " + S(pages)
                     + " rows=" + S(rows) + " n=" + S(total) + " mine=" + S(mineN) + " puppet=" + S(puppetN)
                     + " noUid=" + S(noUidN) + " spawnCauseTowngen=" + S(towngenN)
                     + " buildingResolved=" + S(bldResolved) + " contextUnreadable=" + S(ctxFail)
                     + " unreadable=" + S(unreadable) + " rowsAPage=" + S((long long)kRosterPage)
                     + " platoonIdFault[roster,ctx]=" + S(g_platoonIdFaultRoster) + "," + S(g_platoonIdFaultCtx)
                     + "   (THE STOP SIGNAL IS page=N of N. mine=1 means THIS game minted the uid; puppet=1 means"
                       " another game did; both are 0 for a character nothing has announced. spawnCause is one of"
                       " puppet / towngen / unknown - unknown covers save-loaded characters, bar residents and every"
                       " other engine path, because createRandomSquad 0x582F80 is not hooked. adoptGate is the"
                       " world-adoption pass's own predicate asked about this character now, and it moves no"
                       " counter.)");
            for (std::map<std::string, std::string>::const_iterator r = byKey.begin(); r != byKey.end(); ++r, ++idx)
            {
                if (idx < first || idx >= first + (long long)kRosterPage) continue;
                ++printed;
                DebugLog("[P031] rf" + r->second);
            }
            DebugLog("[P031] rf end page=" + S((long long)page) + " of " + S(pages) + " printed=" + S(printed)
                     + " notOnThisPage=" + S(rows - printed));
        }
    }
}

void ApplyRemoteContext(unsigned int uid, const std::string& squadSid, int squadType, const std::string& townSid,
                        const unsigned char* handRaw32, const unsigned char* charRaw32, float bx, float by, float bz, int memberType,
                        const std::string& platoonId)
{
    ::Character* twin = RawHandIsEmpty(charRaw32) ? 0 : SafeHandToCharacter(LocalHandFromRaw(charRaw32));   // H027
    ++g_ctxRecv;
    int how = 0;
    void* town = ResolveTown(townSid, &how);
    if (town) ++g_ctxTownOk; else if (!townSid.empty()) ++g_ctxTownMiss;
    void* bld = (bx != 0.0f || bz != 0.0f) ? ResolveBuilding(handRaw32) : 0;
    float rx = 0, ry = 0, rz = 0; float delta = -1.0f;
    if (bld && ObjectPosition(bld, &rx, &ry, &rz)) { delta = sqrtf((rx - bx) * (rx - bx) + (rz - bz) * (rz - bz)); if (delta < 1.0f) ++g_ctxBuildingOk; else ++g_ctxBuildingFar; }
    else if (bx != 0.0f || bz != 0.0f) ++g_ctxBuildingMiss;
    void* squadGd = 0;
    if (!squadSid.empty() && coop::GameWorldPtr() != 0) { squadGd = coop::GameWorldPtr()->gamedata.getData(squadSid); if (squadGd) ++g_ctxSquadOk; else ++g_ctxSquadMiss; }
    const std::string q(1, (char)34);
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "context" + q
             + "," + q + "uid" + q + ":" + S(uid)
             + "," + q + "townSid" + q + ":" + q + townSid + q + "," + q + "townResolved" + q + ":" + (town ? "1" : "0") + "," + q + "townHow" + q + ":" + S(how)
             + "," + q + "buildingSent" + q + ":" + ((bx != 0.0f || bz != 0.0f) ? "1" : "0") + "," + q + "buildingResolved" + q + ":" + (bld ? "1" : "0")
             + "," + q + "buildingPosDelta" + q + ":" + F1(delta)
             + "," + q + "squadSid" + q + ":" + q + squadSid + q + "," + q + "squadFound" + q + ":" + (squadGd ? "1" : "0")
             + "," + q + "squadType" + q + ":" + S(squadType) + "," + q + "memberType" + q + ":" + S(memberType)
             + "," + q + "twinLocal" + q + ":" + (PlausibleObject(twin) ? "1" : "0") + "}");
    PendingContext pc; pc.squadSid = squadSid; pc.townSid = townSid; pc.platoonId = platoonId; pc.squadType = squadType; pc.memberType = memberType;
    std::memcpy(pc.hand, handRaw32, 32); std::memcpy(pc.chand, charRaw32, 32); pc.bx = bx; pc.by = by; pc.bz = bz;
    g_pendingCtx[uid] = pc;
    /* T-1 B1 restructure (protocol 85): CONTEXT's member type is no longer read as the squad leader - MSG_SQUAD_LEAD carries it (handoff.cpp ApplyRemoteSquadLead). */
}

void SetContextApply(bool on) { g_contextOn = on; DebugLog(std::string("[CTX] context apply ") + (on ? "ON" : "OFF")); }
/* The context map's key for squad id `id` built under `faction`. The faction is in the key because two other players' squads can
   carry the same id (each game numbers its own), and they must be two rows. The address is a safe key: the map is cleared at world
   teardown (ForgetContextPlatoons), before any faction is freed. */
static std::string CtxIdKey(const void* faction, const std::string& id) { return coopctx::CtxKeyMake(id, P(faction)); }
/* M7a3f3 T-425 [m7a3f3-sc3]: 1 = the platoon another game's CONTEXT built here under `faction` for squad id `platoonId` is plausible and awake (its live copy) */
int CtxPlatoonLiveForId(const void* faction, const std::string& platoonId)
{
    if (platoonId.empty()) return 0;
    std::map<std::string, void*>::const_iterator it = g_ctxPlatoons.find(CtxIdKey(faction, platoonId));
    if (it == g_ctxPlatoons.end() || !PlausibleObject(it->second)) return 0;
    return PlatoonAsleepPod(it->second) == 0 ? 1 : 0;
}

namespace { void* ReadPlatoonFactionPod(void* platoon); }   // Platoon+0x10, guarded - defined below (RetirePeerContextPlatoons), in the same (anonymous) namespace

// M-B / H026 - before CreateAt: build (or reuse) the platoon this uid's context names. Returns true when a
// container + home were produced; false leaves the old path (container NULL) untouched and counts why.
bool PrepareContextForSpawn(unsigned int uid, const std::string& factionName, const Ogre::Vector3& pos,
                            RootObjectContainer** containerOut, Building** buildingOut, int* memberOut)
{
    *containerOut = 0; *buildingOut = 0; *memberOut = -1;
    std::map<unsigned int, PendingContext>::iterator it = g_pendingCtx.find(uid);
    if (it == g_pendingCtx.end()) { ++g_ctxApplyNoContext; return false; }
    const PendingContext& pc = it->second;
    if (pc.squadSid.empty() || coop::GameWorldPtr() == 0) { ++g_ctxApplyNoSquad; return false; }
    GameData* squadGd = coop::GameWorldPtr()->gamedata.getData(pc.squadSid);
    if (!PlausibleObject(squadGd)) { ++g_ctxApplyNoSquad; return false; }
    Faction* faction = factionName.empty() ? 0 : ResolveWireFaction(factionName);   // P3: "@player:<name>" -> the peer faction
    if (!PlausibleObject(faction)) { ++g_ctxApplyNoFaction; return false; }
    const bool haveBuilding = (pc.bx != 0.0f || pc.bz != 0.0f);
    void* building = haveBuilding ? ResolveBuilding(pc.hand) : 0;
    void* town = BuildingGetTown(building);
    if (town) ++g_ctxTownFromBuilding;
    else { int how = 0; town = ResolveTown(pc.townSid, &how); if (town) ++g_ctxTownFromSid; else ++g_ctxNoTown; }
    const unsigned int* hw = (const unsigned int*)pc.hand;   // vtbl(8) type(4) container(4) containerStamp(4) index(4) serial(4)
    // P1: one local platoon per announced squad id AND faction when the id is known (older senders: the M-B template|building|town key)
    const std::string key = !pc.platoonId.empty() ? CtxIdKey(faction, pc.platoonId)
                          : pc.squadSid + "|" + (building ? (S(hw[5]) + ":" + S(hw[6])) : std::string("-")) + "|" + pc.townSid;
    void* platoon = 0;
    std::map<std::string, void*>::iterator pit = g_ctxPlatoons.find(key);
    // P1c (F459): the client's own save holds a sleeping platoon with the same id as the host's live squad (both loaded the
    // same save; the host woke it first). A live announcement supersedes THAT copy too, else two platoons share one id.
    if (!pc.platoonId.empty())
    {
        // Section 13 (two saves, one world - the host's): this save's OWN AWAKE copy of the peer's squad (same id, no session
        // member, a world faction) is retired through the engine's unload, then superseded below like any sleeping copy.
        /* inv7a3 (review-inv7a2 HIGH): after inv7e a reloaded game's stale copy answers to the OTHER game's id, and this game
           announces its own NPC squads under the engine id - so the id alone cannot say which squad is the original. The
           number can (decision 31(a)): an id minted in MY block is my own squad's, and the announcement is the other game's
           stale copy of it. Neither the awake retire nor the asleep destroy runs then (the death watch's rule). */
        int mySlot = -1; const int idBlk = StoreIdBlockOwner(pc.platoonId, &mySlot);
        /* M7a3f3 T-425 [m7a3f3-sc4]: ...unless this game HANDED that squad to another game (the handed-over set, handoff.cpp): the announcement is
           then the running squad, and this game's own awake / sleeping copy (its world data rebuilt it) is the stale one - the awake retire
           and the asleep destroy below run (T735: 8 REFUSED lines after the walk-back, two of each person ~40 s). */
        const int ownBlk = coopsp::SupersedeRefuseOwn(idBlk, mySlot) != 0 ? 1 : 0;
        const int handedOver = (ownBlk != 0 && HandedOverHas((void*)faction, pc.platoonId)) ? 1 : 0;
        const bool ownId = coopsquad::SupersedeOwnIdAfterHandover(ownBlk, handedOver) != 0;
        if (handedOver != 0)
        {
            ++g_ctxHandedOverSupersede;
            if (g_ctxHandedOverSeen.insert(pc.platoonId).second)
                DebugLog("[CTX] same-id supersede ALLOWED id='" + pc.platoonId + "': this game minted it (block " + S((long long)idBlk) + " = my slot) but HANDED the squad to another game"
                         " - the announcement is the running squad; this game's own awake / sleeping copy is the stale one (handedOverSupersede=" + S(g_ctxHandedOverSupersede) + ")");
        }
        if (ownId)
        {
            const void* ctxOwn = (pit != g_ctxPlatoons.end()) ? pit->second : 0;
            void* ma = FindAwakePlatoon(faction, pc.platoonId); void* ms = FindSleepingPlatoon(faction, pc.platoonId);
            if ((ma != 0 && ma != ctxOwn) || (ms != 0 && ms != ctxOwn))
            {
                ++g_ctxSupersedeOwnIdRefused;
                if (g_ctxOwnIdRefusedSeen.insert(pc.platoonId).second)
                    DebugLog("[CTX] same-id supersede REFUSED id='" + pc.platoonId + "': this game minted it (block " + S((long long)idBlk) + " = my slot) - its own "
                             + (ma != 0 && ma != ctxOwn ? "AWAKE" : "sleeping") + " squad is the original, the announcement is the other game's copy (supersedeOwnIdRefused=" + S(g_ctxSupersedeOwnIdRefused) + ")");
            }
        }
        void* awakeLocal = ownId ? 0 : FindAwakePlatoon(faction, pc.platoonId);
        if (awakeLocal != 0 && !(pit != g_ctxPlatoons.end() && pit->second == awakeLocal) && !IsPlayerFaction(faction) && !IsPeerFaction(faction))
        {
            const int hasSession = PlatoonHasSessionMembers(awakeLocal);
            /* inv7e2 (T418): session members that are ALL this game's own sweep adoptions of THIS link generation are the
               reloaded save's copies - the peer's continuously running squad wins; they retire and are withdrawn. */
            unsigned int ru[256]; bool ruAnn[256]; int rn = 0; int reload = coopsp::kReloadNoSession;
            if (hasSession > 0) reload = ReloadVerdict(awakeLocal, mySlot, ru, 256, &rn);
            /* M7a3f4 item 1 [m7a3f4-sc0]: the retire runs here ONLY because this game handed the squad over (handedOver) - hand-overs are
               per person, so this platoon may still hold a person this game runs (one never handed, one kept by the receiver, one taken
               back): never retired then, nor when it cannot be read. Logged once per id, counted (handedOverKeptMine). */
            const int hoMine = (handedOver != 0) ? StorePlatoonMineMembers(awakeLocal) : 0;
            const bool hoKeep = coopsquad::HandedOverRetireRefuse(handedOver, hoMine) != 0;
            if (hoKeep)
            {
                ++g_ctxHandedOverKeptMine;
                if (g_ctxHandedOverKeptMineSeen.insert(pc.platoonId).second)
                    DebugLog("[CTX] handed-over squad id='" + pc.platoonId + "': this game's own AWAKE platoon " + P(awakeLocal) + " NOT retired - it holds "
                             + (hoMine < 0 ? std::string("people that could not be read") : S((long long)hoMine) + " person(s) this game runs")
                             + " (handedOverKeptMine=" + S(g_ctxHandedOverKeptMine) + ")");
            }
            else if (hasSession == 0 || reload == coopsp::kReloadRetire)
            {
                for (int i = 0; i < rn; ++i) ruAnn[i] = AnnouncedExplicit(ru[i]);   /* before the retire: the unload-destroy path may withdraw some */
                if (StoreRetireLocalCopy(awakeLocal) == 1)
                {
                    ++g_ctxRetiredAwake; DebugLog("[CTX] this save's own AWAKE platoon " + P(awakeLocal) + " id='" + pc.platoonId + "' retired (engine unload) - the peer's squad replaces it");
                    if (reload == coopsp::kReloadRetire)
                    {
                        int withdrawn = 0, owed = 0, unannounced = 0;
                        for (int i = 0; i < rn; ++i) { const int w = WithdrawReloadedCopy(ru[i], ruAnn[i]); if (w == 1) { ++withdrawn; ++g_staleWithdrawnReloaded; } else if (w == 2) ++owed; else ++unannounced; }
                        ++g_staleRetiredReloaded;
                        DebugLog("[STALE] reloaded copy id='" + pc.platoonId + "' retired: all " + S((long long)rn) + " session members were picked up by this game's sweep from world load " + S((long long)StoreWorldGenNow()) + ", loaded after this game's last link-down"
                                 + " - the peer's continuously running squad wins; withdrawn=" + S((long long)withdrawn) + " owed=" + S((long long)owed) + " neverAnnounced=" + S((long long)unannounced)
                                 + " retiredReloaded=" + S(g_staleRetiredReloaded) + " withdrawnReloaded=" + S(g_staleWithdrawnReloaded) + " keptEarlierGen=" + S(g_staleKeptEarlierGen)
             + " keptNotReloaded=" + S(g_staleKeptNotReloaded) + " keptNoSlot=" + S(g_staleKeptNoSlot));
                    }
                }
                else { ++g_ctxRetireFailed; DebugLog("[CTX] this save's own AWAKE platoon " + P(awakeLocal) + " id='" + pc.platoonId + "' could not be retired"); }
            }
            else
            {
                ++g_ctxRetireSkippedSession;
                if (hasSession < 0 || reload == coopsp::kReloadUnreadable) DebugLog("[CTX] awake platoon " + P(awakeLocal) + " id='" + pc.platoonId + "' unreadable - not retired");
                if (reload == coopsp::kReloadKeepEarlierGen)
                {
                    ++g_staleKeptEarlierGen;
                    if (g_staleKeptSeen.insert(pc.platoonId).second)
                        DebugLog("[STALE] same-id squad id='" + pc.platoonId + "' KEPT: a session member was picked up from an earlier world load (keptEarlierGen=" + S(g_staleKeptEarlierGen) + ")");
                }
                else if (reload == coopsp::kReloadKeepNotReloaded) ++g_staleKeptNotReloaded;   /* fold: no reload since this game's last link-down - the counted duplicate stands, as before inv7e2 */
                else if (reload == coopsp::kReloadKeepNoSlot)
                {
                    ++g_staleKeptNoSlot;
                    if (g_staleKeptSeen.insert(pc.platoonId).second)
                        DebugLog("[STALE] same-id squad id='" + pc.platoonId + "' KEPT: this game's slot is unknown - never retired (fail closed; keptNoSlot=" + S(g_staleKeptNoSlot) + ")");
                }
            }
        }
        for (int guard = 0; !ownId && guard < 4; ++guard)   /* inv7a3: never for an id this game minted */
        {
            void* asleep = FindSleepingPlatoon(faction, pc.platoonId);
            if (asleep == 0) break;
            if (pit != g_ctxPlatoons.end() && pit->second == asleep) break;   // handled by the branch below
            StoreUnbindPlatoon(asleep);
            if (DestroyPlatoonPod(faction, asleep) == 1) { ++g_ctxSupersededAsleep; DebugLog("[CTX] sleeping platoon " + P(asleep) + " id='" + pc.platoonId + "' (this save's own copy) destroyed - superseded by the live announcement (P1c)"); }
            else { ++g_ctxSupersedeFailed; break; }
        }
        // P3 (decision 23, two saves = two worlds): this save may hold its OWN AWAKE squad with the host's id (a town both
        // worlds generated). Counted and named here, not destroyed - destroying a live platoon is unmeasured (F459 class).
        void* awake = FindAwakePlatoon(faction, pc.platoonId);
        if (awake != 0 && !(pit != g_ctxPlatoons.end() && pit->second == awake) && g_ctxAwakeSameIdSeen.insert(pc.platoonId).second)
        { ++g_ctxAwakeSameId; DebugLog("[CTX] this save's own AWAKE platoon " + P(awake) + " shares id='" + pc.platoonId + "' with the peer's squad - a duplicate (counted, not destroyed)"); }
    }
    if (pit != g_ctxPlatoons.end() && PlausibleObject(pit->second) && PlatoonAsleepPod(pit->second) == 1)
    {
        void* old = pit->second;
        StoreUnbindPlatoon(old);
        g_ctxPlatoons.erase(pit); pit = g_ctxPlatoons.end(); g_ctxPlatoonSlot.erase(key);
        /* destroyed through the faction that HOLDS it (Platoon+0x10), not the incoming one: Faction::removeSquad unlinks only from
           the faction it is called on, so any other faction would keep the freed squad on its sleeping list for the save to walk into */
        Faction* oldF = (Faction*)ReadPlatoonFactionPod(old);
        if (!PlausibleObject(oldF)) { ++g_ctxSupersedeFailed; ErrorLog("[CTX] platoon " + P(old) + " key='" + key + "' asleep - its faction is unreadable, NOT destroyed; creating a fresh one"); }
        else if (DestroyPlatoonPod(oldF, old) == 1) { ++g_ctxSupersededAsleep; DebugLog("[CTX] platoon " + P(old) + " key='" + key + "' was ASLEEP here (the peer took the squad over) - destroyed, superseded by the live announcement (P1b)"); }
        else { ++g_ctxSupersedeFailed; ErrorLog("[CTX] platoon " + P(old) + " key='" + key + "' asleep - removeSquad FAULTED; creating a fresh one anyway"); }
    }
    if (pit != g_ctxPlatoons.end() && PlausibleObject(pit->second)) { platoon = pit->second; ++g_ctxPlatoonsReused; g_ctxPlatoonSlot[key] = net::OwnerSlotOf(uid); }
    else
    {
        platoon = faction->spawnEmptySquad(squadGd, false, pos);
        if (!PlausibleObject(platoon)) { ++g_ctxPlatoonCreateFailed; return false; }
        void* active = 0;
        if (!ConfigurePlatoonPod(platoon, pc.squadType, town, pc.hand, building != 0, &active)) { ++g_ctxPlatoonCreateFailed; return false; }
        g_ctxPlatoons[key] = platoon; ++g_ctxPlatoonsCreated;
        g_ctxPlatoonSlot[key] = net::OwnerSlotOf(uid);   /* the owner OnSpawn recorded before this copy is built: that player leaving retires it */
        if (!pc.platoonId.empty() && !IsPlayerFaction(faction) && !IsPeerFaction(faction)) CarryPeerPlatoonId(faction, platoon, pc.platoonId);   /* inv7e: before the bind, so the clash test sees only OTHER squads */
        if (!pc.platoonId.empty()) StoreBindPlatoon(platoon, pc.platoonId);   // P1: world id + persistent (sleeps instead of being destroyed)
        DebugLog("[CTX] platoon created " + P(platoon) + " key='" + key + "' town=" + P(town) + " building=" + P(building) + " squadType=" + S(pc.squadType));
    }
    void* active = ReadActivePlatoonPod(platoon);   // POD helper: this function holds std::strings, so no __try here (C2712)
    if (!PlausibleObject(active)) { ++g_ctxPlatoonCreateFailed; return false; }
    *containerOut = (RootObjectContainer*)active; *buildingOut = (Building*)building; *memberOut = pc.memberType;
    return true;
}

// H027 - the client already holds this character (same handle): register IT as the uid's object, with the same
// bookkeeping CreateAt does after `create`, and let the caller gate it. No twin is created.
bool AdoptExistingTwin(unsigned int uid, const Ogre::Vector3& authorityPos)
{
    if (g_spawned.find(uid) != g_spawned.end()) return false;   // already registered (CreateAt's own idempotency applies)
    std::map<unsigned int, PendingContext>::iterator it = g_pendingCtx.find(uid);
    if (it == g_pendingCtx.end() || RawHandIsEmpty(it->second.chand)) return false;
    ::Character* c = SafeHandToCharacter(LocalHandFromRaw(it->second.chand));
    if (!PlausibleObject(c)) { ++g_twinLookupFailed; return false; }
    if (FindSpawnedUid(c) != 0) return false;                   // already someone's puppet: not a twin
    // mirror1 (crash T487) A: a twin the uid table cannot hold is not adopted - it stays this engine's.
    if (!MirrorAdd(uid, (RootObjectBase*)c))
    {
        ++g_twinRefusedFull;
        if (coopuid::LogRefusal(g_twinRefusedFull))   // mirror1 fold (review-mirror1 #6 #12): says why; rate-limited
            ErrorLog("[M1] twin NOT adopted uid " + S(uid) + " - the uid table refused it (" + MirrorRefusalWhy()
                     + "); the character stays with this engine's AI and is not driven (twinRefusedFull="
                     + S(g_twinRefusedFull) + "; logged for the first 5 and every 100th).");
        return false;
    }
    g_spawned[uid] = (RootObjectBase*)c;
    TrackForLiveness(uid, c, true);   // a twin is our own save's character: authored
    ++g_twinAdopted;
    g_twinUids.insert(uid);
    net::MarkReleasedHere(uid);   /* this game's own save-born body is now another game's copy IN PLACE: the squad holding it is this game's own, not one its engine formed around a copy (store.cpp BlockSquadWrite) */
    // User decision 2026-09-02: the shared character is taken over where THIS instance's AI had walked it during the
    // pre-sync window (same save, same start, ~40 s of independent simulation); place it at the authority's position
    // ONCE, at adoption, with the engine's own placement (the catch-up snap's call), so it does not run in from
    // wherever it wandered. Recorded in docs/parity-register.md as a one-time placement at load.
    Ogre::Vector3 before = c->worldPosition();
    const float dx = before.x - authorityPos.x, dz = before.z - authorityPos.z;
    const float moved = sqrtf(dx * dx + dz * dz);
    if (moved > 1.0f && PlausibleObject(c->movement)) { P071NotePlacement(uid, c, 4, authorityPos.x, authorityPos.y, authorityPos.z); /* PROBE P071 */ c->movement->teleportTo(authorityPos, -1); ++g_twinPlaced; g_twinPlacedDist += moved; }
    Ogre::Vector3 p = c->worldPosition();
    DebugLog("[M1] adopted EXISTING character as uid=" + S(uid) + " obj=" + P(c) + " (same handle as the authority's - no twin) at "
             + F1(p.x) + "," + F1(p.y) + "," + F1(p.z));
    const std::string q(1, (char)34);
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "twin_adopted" + q + "," + q + "uid" + q + ":" + S(uid) + "," + q + "placedFrom" + q + ":" + F1(moved) + "}");
    return true;
}

void FinishContextForSpawn(unsigned int uid, int memberType)
{
    ::Character* c = FindSpawned(uid);
    if (!PlausibleObject(c)) return;
    if (memberType >= 0 && memberType <= 4) { c->setSquadRole((SquadRole)memberType); ++g_ctxMemberTypeSet; }
    ++g_ctxApplied;
    const std::string q(1, (char)34);
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "context_applied" + q + "," + q + "uid" + q + ":" + S(uid)
             + "," + q + "memberType" + q + ":" + S(memberType) + "," + q + "platoonsCreated" + q + ":" + S(g_ctxPlatoonsCreated)
             + "," + q + "platoonsReused" + q + ":" + S(g_ctxPlatoonsReused) + "," + q + "awakeSameId" + q + ":" + S(g_ctxAwakeSameId) + "," + q + "retiredAwake" + q + ":" + S(g_ctxRetiredAwake) + "," + q + "retireFailed" + q + ":" + S(g_ctxRetireFailed) + "," + q + "retireSkippedSession" + q + ":" + S(g_ctxRetireSkippedSession) + "," + q + "supersededAsleep" + q + ":" + S(g_ctxSupersededAsleep) + "," + q + "supersedeFailed" + q + ":" + S(g_ctxSupersedeFailed) + "," + q + "supersedeOwnIdRefused" + q + ":" + S(g_ctxSupersedeOwnIdRefused) + "}");
}

} // namespace coop

namespace coop {
// review-p3b: GameWorld::_clearAndDestroyGameWorldStuff frees every platoon; the context map holds Platoon* across it
void ForgetContextPlatoon(void* platoon)   // review-p3o H1: a single platoon freed through Faction::removeSquad
{
    for (std::map<std::string, void*>::iterator it = g_ctxPlatoons.begin(); it != g_ctxPlatoons.end(); ) { if (it->second == platoon) { g_ctxPlatoonSlot.erase(it->first); g_ctxPlatoons.erase(it++); } else ++it; }
}
/* recruit1 fold (review-recruit1 2a): the context map and the store bind hold the Platoon; getSquad() answers the
   ActivePlatoon at Platoon+0x1D8. The Platoon of this map whose +0x1D8 is `active`, or 0 when the map holds none. */
void* ContextPlatoonForActive(void* active)
{
    if (active == 0) return 0;
    for (std::map<std::string, void*>::const_iterator it = g_ctxPlatoons.begin(); it != g_ctxPlatoons.end(); ++it)
        if (PlausibleObject(it->second) && ReadActivePlatoonPod(it->second) == active) return it->second;
    return 0;
}
/* M7a2 [m7a2-cx2] (T803): 1 = `platoon` (a Platoon*, the store's key) is one this map holds - a squad built here for another
   game's announced squad, whose people arrive by SPAWN. Asleep or awake: the entry is set before the first spawn into it and is
   only removed when the platoon is freed, superseded, retired or emptied by a hire. MAIN THREAD (the map has no lock). */
int IsContextPlatoon(void* platoon)
{
    if (platoon == 0) return 0;
    for (std::map<std::string, void*>::const_iterator it = g_ctxPlatoons.begin(); it != g_ctxPlatoons.end(); ++it) if (it->second == platoon) return 1;
    return 0;
}
void ForgetContextPlatoons()
{
    const size_t n = g_ctxPlatoons.size();
    g_ctxPlatoons.clear(); g_ctxPlatoonSlot.clear(); g_ctxAwakeSameIdSeen.clear();
    DebugLog("[CTX] world teardown: forgot " + S((long long)n) + " context platoons");
}

// review-session S6 - RETIRE THE PLATOONS A DEPARTED PLAYER'S ANNOUNCEMENTS BUILT HERE.
//
// Every entry in `g_ctxPlatoons` was created by `PrepareContextForSpawn` from an inbound CONTEXT for
// the player whose character was built into it (g_ctxPlatoonSlot). When that player leaves, its
// entries go - and only its entries: every other player's platoons stay. Forgetting the map (what world
// teardown does) is not enough here: the world is still alive, so a forgotten platoon stays in its faction's
// active list forever, and after a reconnect the re-announced squad would be built beside it.
// A known slot keeps a platoon that still holds a session member: after that player's copies were
// dropped, a member left is another player's (a squad handed over), and its platoon is not retired
// under it (counted peerGoneRetireKeptMembers). An unknown slot (-1: the old link's peer that never
// announced one) retires every entry, the whole-link rule.
//
// The removal is the store's proven pair plus P1c's destroy, in that order:
//   1. `StoreRetireLocalCopy` = `Platoon::deactivate(0)` + `Faction::deactivatePlatoon` (F476/H039).
//      Half of it - T163 - left an emptied platoon in the ACTIVE list and the faction update
//      crashed, which is why this calls the pair and never `deactivate` alone.
//   2. `Faction::removeSquad` on the SLEEPING platoon, the P1c path.
// platoon-lifecycle.md's "an empty non-persistent platoon is DISCARDED" is the keep-or-discard fork
// inside `Faction::updateActivePlatoons`, NOT something `deactivate` does on its own - and these
// platoons are marked persistent by `StoreBindPlatoon` anyway, so they would sleep rather than be
// discarded. Nothing is left to the engine: the destroy is explicit.
//
// F459: the pointer handed to `removeSquad` is looked up FRESH in the faction's own unloaded list
// whenever the key carries a platoon id; the cached pointer is only used for the pre-P1 keys, which
// name no id to look up. The map is copied and cleared FIRST because `Faction::removeSquad` is
// hooked (store.cpp) and its detour calls `ForgetContextPlatoon`, which erases from this map - an
// iteration over it would walk into an erased node.
namespace {
// POD helper: VS2010 refuses `__try` in a function that needs C++ unwinding, and the caller below
// builds std::strings. Platoon+0x10 is the Faction (F453, re-corrected from F452's reading).
void* ReadPlatoonFactionPod(void* platoon)
{
    __try { return *(void**)((char*)platoon + 0x10); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}

int RetirePeerContextPlatoons(int slot)
{
    std::vector<std::pair<std::string, void*> > rows;
    for (std::map<std::string, void*>::iterator it = g_ctxPlatoons.begin(); it != g_ctxPlatoons.end(); )
    {
        std::map<std::string, int>::const_iterator s = g_ctxPlatoonSlot.find(it->first);
        if (!cooppg::PlayerGoneTakesRow(s != g_ctxPlatoonSlot.end() ? s->second : -1, slot)) { ++it; continue; }
        if (slot >= 0 && PlausibleObject(it->second) && PlatoonHasSessionMembers(it->second) > 0) { ++g_peerGoneRetireKeptMembers; ++it; continue; }
        rows.push_back(*it);
        g_ctxPlatoonSlot.erase(it->first);
        g_ctxPlatoons.erase(it++);   /* out of the map BEFORE the destroy below: the removeSquad detour's ForgetContextPlatoon must not walk into an erased node */
    }
    if (slot < 0) g_ctxAwakeSameIdSeen.clear();

    int retired = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        void* platoon = rows[i].second;
        if (!PlausibleObject(platoon)) { ++g_peerGoneRetireUnreadable; continue; }
        ::Faction* f = (::Faction*)ReadPlatoonFactionPod(platoon);   // Platoon+0x10 (F453)
        if (!PlausibleObject(f)) { ++g_peerGoneRetireUnreadable; continue; }

        const std::string& key = rows[i].first;
        const std::string worldId = coopctx::CtxKeyIdOf(key);   // the squad id alone, without the faction part ("" for an older key)

        const int members = PlatoonHasSessionMembers(platoon);   // review-s6 H2: the guard P1c has - unreadable is not "safe"
        if (members < 0) { ++g_peerGoneRetireUnreadable; continue; }
        if (PlatoonAsleepPod(platoon) != 1)          // awake: put it to sleep the way the engine does
        {
            if (StoreRetireLocalCopy(platoon) != 1)
            {
                ++g_peerGoneRetireFailed;
                ErrorLog("[CTX] peer gone: platoon " + P(platoon) + " key='" + key + "' could not be"
                         " deactivated - it is left in its faction's active list, emptied. A"
                         " re-announce of the same id supersedes it (P1c).");
                continue;
            }
        }
        void* asleep = worldId.empty() ? platoon : FindSleepingPlatoon(f, worldId);
        if (asleep == 0)
        {
            ++g_peerGoneRetireFailed;
            ErrorLog("[CTX] peer gone: platoon id='" + worldId + "' is not in its faction's unloaded"
                     " list after the deactivate pair - NOT destroyed (F459: no write through a"
                     " pointer the engine's own lists do not confirm)");
            continue;
        }
        StoreUnbindPlatoon(asleep);
        if (DestroyPlatoonPod(f, asleep) == 1)
        {
            if (!worldId.empty()) { const int rc = StoreRecreateSleepingFromRecord(worldId); if (rc == 1) ++g_peerGoneRecreated; }   // F492: the world keeps the squad where it last slept
            ++retired; ++g_peerGoneRetired;
            DebugLog("[CTX] peer gone: context platoon " + P(asleep) + " key='" + key + "' retired"
                     " (deactivate pair) and destroyed");
        }
        else
        {
            ++g_peerGoneRetireFailed;
            ErrorLog("[CTX] peer gone: removeSquad FAULTED for " + P(asleep) + " key='" + key
                     + "' - the platoon is asleep and empty, and stays that way");
        }
    }
    return retired;
}

// review-p3o H2 - EVERY ENGINE POINTER THIS MODULE HOLDS, DROPPED BEFORE THE ENGINE FREES IT.
//
// `GameWorld::_clearAndDestroyGameWorldStuff` 0x36B940 destroys every character directly, so none
// of it passes the destroy detour and `NotifyDespawn`/`RetireOnDestroy` never run for it. Called
// from `detour_worldTeardown` (store.cpp) BEFORE `orig_worldTeardown`, on the main thread.
//
// **WHY g_mirror AND g_index ARE CLEARED HERE, AGAINST THE "NEVER REMOVED" NOTE AT THEIR
// DECLARATIONS.** That note is the lock-free reader's safety argument and it is scoped to ONE
// world: never vacating a slot means no tombstones and no chain holes, so a probe that stops at an
// empty slot has genuinely reached the end of its chain. A teardown ends the world those addresses
// belonged to. Every one of them is about to be freed and may be handed to a completely different
// character, so a surviving slot makes `FindSpawnedUid` answer a brand-new character with a uid
// from the PREVIOUS world - F328's failure, applied to every slot at once, and `MirrorMarkDestroyed`
// cannot help because nothing tells it the world went away.
//
// Clearing the WHOLE table cannot open a chain hole: after it there is no later entry for a hole to
// hide. The per-slot order is what makes the transient safe for a reader already inside a probe -
//   1. `uid` first, so a reader that matches the stale pointer in the gap reads 0 and reports a
//      miss, which is the correct answer once the world is going;
//   2. then the pointer, with an interlocked store, so the slot reads empty and terminates chains;
//   3. `dead`/`destroyed` LAST, after the pointer is already gone, so no reader can see a
//      live-looking slot. They MUST be cleared: `MirrorAdd`'s index insert writes only `uid` and
//      `obj`, so a left-over `dead` would make the next world's characters permanently invisible to
//      `FindSpawnedUid`.
// mirror1 (crash T487): rows are also released one at a time now (MirrorRelease: tombstone + trim).
// This still clears EVERYTHING, tombstones included, for the reason above.
void SpawnWorldTeardown()
{
    const size_t spawned = g_spawned.size();
    g_spawned.clear();
    TagsWorldTeardown();   // tags1: every label is destroyed on the next main-thread tick
    coop::LimbsWorldTeardown();   // LIMBS: recorded limb blocks and per-limb marks name this world's copies
    MedicalForgetAllCopies();   // the owner's medical words per copy: every copy of this world is gone
    DeadlookForgetAllCopies();  // the per-copy look marks (first look taken, dead-copy holds, createBody guard) go with them
    g_knockHold.clear();        // and the knockdowns held for those copies' looks

    int mirrorSlots = 0, indexSlots = 0;
    MirrorRowLock();   // mirror1 fold (review-mirror1 #5): no RetireOnDestroy mark lands between these clears
    for (int i = 0; i < kMaxMirror; ++i)
    {
        if (g_mirror[i].obj == 0) continue;
        ++mirrorSlots;
        g_mirror[i].uid = 0;
        InterlockedExchangePointer((PVOID volatile*)&g_mirror[i].obj, (PVOID)0);
        InterlockedExchange(&g_mirror[i].dead, 0);
        InterlockedExchange(&g_mirror[i].destroyed, 0);
    }
    for (int i = 0; i < kIndexSlots; ++i)
    {
        if (g_index[i].obj == 0) continue;
        ++indexSlots;
        g_index[i].uid = 0;
        InterlockedExchangePointer((PVOID volatile*)&g_index[i].obj, (PVOID)0);
        InterlockedExchange(&g_index[i].dead, 0);
        InterlockedExchange(&g_index[i].destroyed, 0);
    }

    MirrorRowUnlock();
    g_mirrorUsedNow = 0;   // mirror1: occupancy of the cleared tables
    g_mirrorTestCap = 0;   // T-354 fold 1: the TEST-ONLY mirrorcap lever is per world - one left on must not refuse the next world's rows
    ::InterlockedExchange(&g_uidTableFullSeen, 0);   // mirror1 fold (review-mirror1 #3): a new world starts with room
    SoakWorldTeardown();   // mirror1 fold (review-mirror1 #1): the transition watch is keyed to this world's uids

    // Uid bookkeeping for the world that is going. Holds no pointer, but it is a claim ABOUT
    // characters that will not exist a moment from now, and `g_spawned` beside it is being emptied.
    const size_t twins = g_twinUids.size();
    g_twinUids.clear();

    // R1-a: every parked prone write belonged to a character this teardown retires.
    g_proneDroppedRetired += (long long)g_proneParkMap.size();
    g_proneParkMap.clear();
    // review-k1 item 4: the carry maps are claims about the same characters.
    g_carryWant.clear();
    g_ownCarried.clear();     // arrest1 (review-arrest1 LOW 4)
    g_ownRagBit.clear();      // T-178 crawl1: owned inRagdoll edges are per world
    g_ownSneakBit.clear();    // owned stealth-mode edges and the owners' words on sneaking are per world
    g_sneakWant.clear();
    g_sneakLastWord.clear();
    g_sneakFaultBody.clear();
    g_sneakTestPend.clear();
    g_carryBreakIn.clear();
    g_carryLast.clear();
    g_carryCap.clear();
    // G1-b (review-g1) item 3: the get-up watches are claims about the same characters.
    // G1-c fold L3: a pulse still armed when its watch is forgotten is booked, so the fates add up to getupPulsed.
    for (std::map<unsigned int, GetupWatch>::const_iterator gw = g_getup.begin(); gw != g_getup.end(); ++gw)
        if (gw->second.armedAt != 0) ++g_getupWatchRemoved;
    g_getup.clear();
    PrisonWorldTeardown();   // review-arrest3 M1
    g_treat.clear();         // heal1: claims about the same characters

    DebugLog("[M1] world teardown: forgot " + S((long long)spawned) + " uid->object rows, "
             + S((long long)mirrorSlots) + " occupied mirror slots, " + S((long long)indexSlots)
             + " occupied index slots and " + S((long long)twins) + " twin uids");
}
}
namespace coop { int TownCharContextPod(void* c, void** platoon, void** squadGd, int* squadType, void** town) { if (c == 0) return 0; unsigned char h[32], r[32]; int mt = -1; return ReadContextPod((::Character*)c, platoon, squadGd, squadType, town, h, r, &mt) ? 1 : 0; } }   /* refill1: roster full's own read, for towngen.cpp's free-recruit count (was probe P086's) */
// snap1 (user decision 2026-09-26): the `snapshot` verb's character facts - items.cpp SnapshotTick is the one caller. Each engine
// read sits in its own POD __try frame (no std::string in any of them, C2712); the town is roster full's own ReadContextPod read.
namespace coop {
namespace {
int SnapPosPod(::Character* c, float* x, float* z)
{
    __try
    {
        if (!PlausibleObject(*(void**)((char*)c + 0x448))) return 0;   /* AnimationClass - the roster's own gate before a position read */
        Ogre::Vector3 p = c->worldPosition();
        *x = p.x; *z = p.z;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SnapDeadPod(::Character* c, int* dead)
{
    __try { *dead = c->hasDied() ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *dead = -1; return 0; }
}
int SnapBloodPod(::Character* c, float* blood)
{
    __try { *blood = ((MedicalSystem*)((char*)c + 0x458))->blood; return 1; }   /* SnapshotHealth's own MedicalSystem at +0x458 */
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SnapHungerPod(::Character* c, float* hunger)   /* par5 (parity P5): the same MedicalSystem, hunger +0x60 */
{
    __try { *hunger = ((MedicalSystem*)((char*)c + 0x458))->hunger; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SnapFactionPod(::Character* c, void** out)
{
    *out = 0;
    __try { *out = (void*)c->getOwnerFactionDirect(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
const size_t kSnapFactionName = 0x1A8;   /* Faction::name, std::string - read as a POD string, no call */
}   /* namespace */
int SnapCharFacts(void* cv, SnapFacts* o)
{
    o->havePos = 0; o->x = 0.0f; o->z = 0.0f; o->dead = -1; o->haveBlood = 0; o->blood = 0.0f; o->playerFac = 0;
    o->haveHunger = 0; o->hunger = 0.0f;
    o->fac[0] = '?'; o->fac[1] = 0; o->town[0] = 0;
    ::Character* c = (::Character*)cv;
    if (!PlausibleObject(c) || IsRetiredObject(c)) return 0;
    o->havePos = SnapPosPod(c, &o->x, &o->z);
    SnapDeadPod(c, &o->dead);
    o->haveBlood = SnapBloodPod(c, &o->blood);
    o->haveHunger = SnapHungerPod(c, &o->hunger);
    {
        void* fv = 0;
        if (SnapFactionPod(c, &fv) != 0 && PlausibleObject(fv))
        {
            ::Faction* f = (::Faction*)fv;
            int slot = -1;
            if (IsPlayerFaction(f)) { o->playerFac = 1; slot = MySlotForWire(); _snprintf(o->fac, sizeof(o->fac) - 1, "@slot:%d", slot); }
            else if ((slot = StandInSlotOf(f)) >= 0) _snprintf(o->fac, sizeof(o->fac) - 1, "@slot:%d", slot);
            else
            {
                char nm[128]; nm[0] = 0;
                if (ReadStdStringPod((const char*)fv + kSnapFactionName, nm, 128))
                {
                    size_t k = 0;
                    for (; nm[k] != 0 && k < sizeof(o->fac) - 1; ++k) o->fac[k] = (nm[k] == ' ' || nm[k] == '\t') ? '_' : nm[k];
                    o->fac[k] = 0;
                    if (k == 0) { o->fac[0] = '-'; o->fac[1] = 0; }
                }
            }
            o->fac[sizeof(o->fac) - 1] = 0;
        }
    }
    {
        void* platoon = 0; void* squadGd = 0; void* town = 0; int squadType = -1, memberType = -1;
        unsigned char hraw[32], craw[32];
        if (ReadContextPod(c, &platoon, &squadGd, &squadType, &town, hraw, craw, &memberType) && town != 0)
            if (!ReadObjectSid(town, o->town, (int)sizeof(o->town))) o->town[0] = 0;
    }
    return 1;
}
}   /* namespace coop */
/* T-392 (owner 335 a): Building::asTown for towngen's residents gate - BuildingGetTown above (resolver-verified 0xF6BE0,
   guarded), reached from outside this file. ANY THREAD. */
namespace coop { void* TownOfBuilding(void* building) { return BuildingGetTown(building); } }
