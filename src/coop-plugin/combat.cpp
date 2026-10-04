// combat.cpp - M3 HIT.
//
// The chokepoint was found offline (F083): `Character::hitByMeleeAttack` and
// `Character::iShotYou` are the ONLY routes into `MedicalSystem::addWound` ->
// `applyDamage`. `hitByMeleeAttack` is virtual, so it is per-entity hookable by the same
// mechanism proven three times already (F033/F061/F066).
//
// `CombatClass::_getHit` is NOT the applier despite its name - it sets stagger/stun state
// for the reaction animation (F083). Hooking it would have been the F037 mistake again.
//
// The replication rule, and why it is this way:
//
//   AUTHORITY side - the hit resolves normally, and we emit the RESOLVED `Damages` (24
//   bytes, 6 floats - F085) to the peer.
//
//   PUPPET side - the local hit is SUPPRESSED (we return without calling the original) and
//   the authority's numbers are applied instead. Recomputing locally would produce
//   plausible-but-different results: Kenshi's damage depends on stats, armour, direction,
//   and randomness, none of which is guaranteed identical across machines. Copying the
//   authority's resolved struct is what makes per-limb health match EXACTLY.
//
// M3 scope: melee only. `iShotYou` (ranged) is the same pattern and is deliberately left
// for after the melee path is proven, so a failure localizes.
// P104 (2026-09-30): GunClass::shoot and Character::iShotYou are now COUNTED (measurement only, the
// `rangedtest` lever). P104 fix (protocol 104): a bolt from our character into the other game's character goes to its owner as
// MSG_SHOT and is played there through MedicalSystem::addWound; a copy's bolt makes no hit here - see the P104 block above CombatTick.

#include "combat.h"
#include "hire.h"   /* recruit1: HireSafePointDrain */
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "spawn.h"
#include "ai_spike.h"
#include "replicate.h"   // F239: InjectAttackOrder   // F233: PulseCharacterQuiet - the narrow combat fix
#include "appearance.h"  // T240: AppearanceBodyAttached - is this victim a body the engine can orient?
#include "net/session.h"
#include "relations.h"   /* par24: WorldRelSafePointDrain */
#include "peace.h"   /* T-546 (owner 512): the team pair and its counters, for the ffshow line */
#include "store.h"   // K2: EngineWritesBlocked - no knock is applied while a world loads or tears down
#include "speech.h"  // crimetest: CrimeTestDrain runs at this same safe point
#include "build.h"   /* build1-a: BuildTestDrain runs at this same safe point */
#include "crime.h"   // crime3: CrimeApplyDrain writes the owner's crimes onto copies at this same safe point
#include "../common/shotwire.h"   /* P104 fix: MSG_SHOT - the bolt's record and the Damages iShotYou builds */
#include "effect.h"   /* T-327: InstallEffectHook, EffectSafePointDrain */
namespace coop { void RestockNowDrain(); }   /* par2: items.cpp - the `restocknow` lever runs at this same safe point */
namespace coop { void TraderSpawnDrain(); }   /* traderspawn: items.cpp - the `traderspawn` lever runs at this same safe point */
namespace coop { void SquadLeaderSafePointDrain(); }   /* T-1 B1: handoff.cpp - the copy squad's leader set to the owner's at this safe point */
namespace coop { void SquadCatsSafePointDrain(); }   /* T-1 B3 restructure: handoff.cpp - the squads' money (MSG_SQUAD_LEAD) into the pots */
namespace coop { void LimbSafePointDrain(); }   /* LIMBS: clothing.cpp - the owner's limbs onto its copies, and the limbtest lever */
namespace coop { void BoxTakeDrain(); }      /* loot2c: items.cpp - the `boxtake` lever's move runs at this same safe point */

#include "coop_log.h"
#include "game/Character.h"
#include "game/hand.h"   // P-15: the target handle is taken locally, never sent
#include "game/CombatClass.h"      // T-186 (mig5): these three replace this file's old stand-in declarations;
#include "game/AnimationClass.h"   // CombatClassAI is a pointer type only (game/forward.h)
#include "game/CharBody.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3HitByMeleeAttack = 0; static coop::AddrReg kMig3HitByMeleeAttack_reg("Character_meleeHit", &kMig3HitByMeleeAttack);   /* Steam_1.0.65 0x439150 */
static unsigned long long kMig3CombatClassAiUpdate = 0; static coop::AddrReg kMig3CombatClassAiUpdate_reg("CombatClassAI_update", &kMig3CombatClassAiUpdate);   /* Steam_1.0.65 0x60CC10 */
static unsigned long long kMig3CharBodyEndAction = 0; static coop::AddrReg kMig3CharBodyEndAction_reg("CharBody_finishAction", &kMig3CharBodyEndAction);   /* Steam_1.0.65 0x5C5CE0 */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <intrin.h>   // P058: _ReturnAddress - the whole discriminator is one of these
#include <map>
#include <sstream>
#include <locale>
#include <cstring>
#include <cmath>
#include <float.h>   // P059: _finite guard in F1

// P-15 / F220 - CombatClass is declared at GLOBAL SCOPE (game/CombatClass.h) deliberately.
//
// Each method below is defined in gamecalls.cpp, which calls the game's function at that method's
// address-table row; no other library supplies it. A class nested inside `namespace coop` (or worse,
// inside an anonymous namespace) mangles to a different symbol from those definitions and would not
// link. The mangled names are kept below only as each method's identity in the findings.
//
//   ?currentTarget@CombatClass@@QEBA?AVhand@@XZ         -> real RVA 0x33A2A0
//   ?enterCombat@CombatClass@@QEAA_NAEBVhand@@H_N@Z -> real RVA 0x664F20
//
// `initCombatMode(target, end, focusedTarget)`, from its decompile:
//   end == 0 -> START: sets combatModeActive (+0x130) = 1, stores the target hand,
//                      changes state to DECISION, returns 1
//   end == 2 -> END:   sets +0x130 = 0, clears the block, returns 0
//   otherwise -> no-op, returns 1
//
// F348 - and the two setters the swing needs, resolved the same way:
//   ?setSwordState@CombatClass@@QEAAXW4swordStateEnum@@@Z      -> real RVA 0x60C240
//   ?setTarget@CombatClass@@QEAAXPEAVCharacter@@@Z  -> real RVA 0x664BC0
//
// `setSwordState` is used in preference to a raw store to +0x1F0 because its decompile shows two
// behaviours we want and would otherwise have to reimplement: it REFUSES to overwrite state 8
// (STUMBLE), so injecting a swing cannot step on a stagger, and on the way to DECISION it clears
// the cached technique. A raw store would do neither.
// `swordStateEnum` itself comes from our own `game/Enums.h` (mig4 G1), already in scope here via
// game/Character.h. F348 derived 0 = attack, 5 = circle, 6 = wait from
// `getStateClass` allocating AttackState / CircleStateAI / WaitingStateAI for those three values
// (SWORD_SWING, CIRCLE_MENACINGLY and WAIT_MENACINGLY).
// CombatClass is declared in game/CombatClass.h.

// F351 - the predicate the ENGINE guards its own `combatState = STARTUP` write with.
//
//   ?isActionAnimating@AnimationClassBase@@QEAA_NXZ -> real RVA 0x5B2A20
//
// `whoAttacksYouOrMe` writes `nextMove = SWORD_SWING` unconditionally and then writes
// `combatState = STARTUP` **only if this returns false**, and `CombatClassAI::decisionState` opens
// by returning immediately when it is true (F231). The first version of the swing injection
// guessed at a different field for this and guessed wrong (F351); this is the actual one, called
// rather than approximated.
//
// `CombatClass::animation` (+0x180) is an `AnimationClass*` and the engine passes it straight into
// this function, so the base subobject is at offset 0.
// T-186 (mig5): AnimationClassBase is declared in game/AnimationClass.h.

// P045 / T095 - **THE FUNCTION NO PROBE IN THIS PROJECT HAS EVER COUNTED.**
//
//   CombatClassAI::update(float) -> real RVA 0x60CC10   (resolve_stub, slot 2237)
//   CombatClass::update(float)   -> real RVA 0x60C3A0   (slot 2193)
//
// **DECLARED ON `CombatClassAI` AND NOT ON `CombatClass`, WHICH IS THE ENTIRE POINT.** F356 cost a
// whole feature by declaring `isAI` as a NON-VIRTUAL member on the base shim: a non-virtual
// call binds STATICALLY to whichever class the declaration names. On the base it would
// resolve 0x60C3A0 - a different function, which dispatches a disjoint set of states - and the
// counter would have carried the name of one function and the calls of another. That is the exact
// defect T095 exists to correct, and it was one keyword away from being reproduced by its own
// repair.
// T-186 (mig5): CombatClassAI is only a pointer type here (game/forward.h) - the hook target is the
// address-table row CombatClassAI_update, so no member is declared or called.

// F384 / P055 - **THE FIFTH ATTEMPT'S TWO ENTRY POINTS. THE ENGINE BUILDS THE TASK; WE NEVER DO.**
//
//   ?startAction@CharBody@@QEAA_NW4TaskType@@PEAVRootObject@@@Z -> real RVA 0x5C5BF0
//   ?finishAction@CharBody@@QEAAXXZ                                   -> real RVA 0x5C5CE0
//
// **Called DIRECTLY (gamecalls.cpp's forwarder, through the method's address-table row) rather than
// hand-dispatched through a vtable**, which is the
// safer of the two and available here because `CharBody` is the concrete class - there is no
// derived override for a non-virtual call to bind past (F356's trap needs a base/derived pair, and this is not
// one). `TaskType` comes from our own `game/Enums.h`,
// already in scope via `game/Character.h` exactly as `swordStateEnum` is: **`TASK_MELEE_FOCUSED = 5`**
// (NULL_TASK, MOVE_ON_FREE_WILL, BUILD, PICKUP, MELEE_ATTACK, TASK_MELEE_FOCUSED).
//
// `setCurrentAction(TaskType, RootObject*)` calls the task factory itself, so **no `Tasker` is ever
// constructed by this mod** - which also side-steps the trap F375 recorded on the other overload,
// where one refusal path DELETES the caller's Tasker and another does not. We never hold one.
// T-186 (mig5): CharBody is declared in game/CharBody.h.

namespace coop {

namespace {

// F085: Damages is 24 bytes / 6 floats. Copied verbatim - we never interpret the fields,
// which is precisely why an unknown field meaning cannot cause a divergence.
const size_t kDamagesBytes = 24;

typedef int (*HitFn)(Character*, int, void*, Character*, void*, int);
HitFn orig_hitByMelee = 0;

volatile LONG64 g_hitsSeen       = 0;   // all characters
volatile LONG64 g_hitsEmitted    = 0;   // ours, replicated out
volatile LONG64 g_hitsSuppressed = 0;   // puppet-side local damage refused
volatile LONG64 g_hitsApplied    = 0;   // authority's damage applied to our copy
volatile LONG64 g_hitsNoAttacker = 0;   // F117: inbound hit whose attacker has no local copy
// F349: a puppet's own blow landing on someone we do not own - refused, because the authority
// owns every consequence of a puppet's actions. Only reachable once puppets can swing (F348).
volatile LONG64 g_hitsPuppetThrown = 0;
volatile LONG   g_hitThreadId    = 0;

// PARITY P-2 outcome split. `noAttacker` was one number covering two very different
// situations, and a single number cannot say how much of the gap a fix closed.
volatile LONG64 g_attackerByHandle   = 0;  // no uid, but the engine handle found it here
volatile LONG64 g_attackerNoIdSent   = 0;  // the authority could not name the attacker at all
volatile LONG64 g_attackerIdMissed   = 0;  // an identity arrived and did NOT resolve locally

// T240 - THE VICTIM HAS TO BE A BODY THE ENGINE CAN ORIENT before a hit is played through it.
// Split by reason, because "not played" is not one situation and one number could not say
// which of them a fix closed - the same lesson `noAttacker` taught (F130). Indexed by
// HitBodyReason (appearance.h), so the array's length follows kHitBodySkipCount. C1-b added
// the sixth: the ATTACKER's body, which addWound also orients whenever the victim's gate is set.
volatile LONG64 g_hitPlayed = 0;
volatile LONG64 g_hitPlaySkipped[kHitBodySkipCount] = { 0, 0, 0, 0, 0, 0 };
const char* const kHitBodyReasonWord[kHitBodySkipCount] =
    { "noAnim", "noAppearance", "appearancePending", "noEntity", "detached",
      "attackerNotOrientable" };
// Bounded: a skip is expected to be rare, and if it is not, the counters say so without the
// log becoming the whole file.
const long long kHitSkipLogLimit = 20;
long long g_hitSkipLogged = 0;
bool g_hitPlayProbeLogged = false;

// Re-entrancy guard: when we apply a remote hit we call the ORIGINAL function, which must
// not be mistaken for a locally-originated hit and re-emitted. Without this the two peers
// would bounce a hit back and forth forever. (Spec section 5's `applyingRemote` rule.)
volatile LONG g_applyingRemote = 0;

// --- Outbound hit queue -----------------------------------------------------------------
//
// T024 MEASURED the damage thread: `hitThread` == `aiPerThread` == `tuThread`, i.e. melee
// resolves on the AI/locomotion worker thread, NOT the main loop. That makes sending from
// inside the detour a threading violation (spec section 5: detours enqueue, the main thread
// does I/O) - ENet's host state is touched by the main-thread pump every frame, so an
// inline send from the worker would race it.
//
// A fixed ring, not a std::deque: the producer runs on a thread where an allocation or a
// lock is how you deadlock the game. Single producer, single consumer, power-of-two mask.
struct PendingHit
{
    unsigned int uid;
    unsigned int attackerUid;   // F117 - required by the receiver, not decorative
    int          cutDir;
    int          comboId;
    char         damages[24];
    // P012 (F113): carried for ATTRIBUTION only, never sent. `emitted` is a bare count, and
    // in T031 it read 45 against hitsSeen=83 - 54% of every melee hit in the world landing
    // on our two characters. A flat pointer->uid mirror would produce exactly that if an
    // object address were freed and reused, so the counter is not evidence until an
    // emission names its victim, its attacker, and its damage values. Recorded here by the
    // producer (a pointer copy, no work) and LOGGED BY THE CONSUMER on the main thread -
    // the detour still allocates nothing and logs nothing.
    const void*  victimObj;
    const void*  attackerObj;
    // PARITY P-2: the engine's cross-process name for an attacker that has no uid - i.e.
    // an ambient world NPC, which is exactly the case that produced a damage event with no
    // visible cause on the peer. Captured HERE because the pointer is only valid on this
    // thread at this moment; five aligned loads, no allocation (identity.h).
    ObjId        attackerId;
    // K2 (decision 61 follow-up): 1 = this hit knocked the victim down (applyDamage's +0x161 0 -> 1 edge
    // inside the original call). Known only AFTER the original ran, which is why the entry is published
    // after it.
    unsigned char knock;
};

const unsigned int kHitQueueSize = 64;          // power of two
const unsigned int kHitQueueMask = kHitQueueSize - 1;
PendingHit g_hitQueue[kHitQueueSize];
volatile LONG g_hitWrite = 0;   // producer: AI worker thread
volatile LONG g_hitRead  = 0;   // consumer: main thread
volatile LONG64 g_hitsDropped = 0;
// How full the ring has EVER been. `droppedQ` only speaks once the bound has already been
// exceeded; this says how close it came, which is the number worth having before it matters.
volatile LONG g_hitQueueHigh = 0;

// --- K2 (decision 61 follow-up): the hit that knocked the owner down ----------------------
//
// Kenshi discards knock momentum, so what a fall looks like comes from the pose at the moment it
// starts. The copy's fall must therefore start on the SAME hit. Owner side: the hit hook arms a slot
// keyed by its THREAD ID before running the original; applyDamage (medical.cpp), running inside that
// original on the same thread, marks it when +0x161 goes 0 -> 1. The AI worker runs this: no lock, no
// allocation - a fixed table and interlocked writes only.
struct KnockArm
{
    volatile LONG tid;        // 0 = free; else the thread that armed it
    const void*   victim;     // the Character the armed hit is on
    volatile LONG knocked;    // set by NoteKnockEdgeAnyThread
};
const int kKnockArms = 16;
KnockArm g_knockArm[kKnockArms];          // static, zero
volatile LONG64 g_knockArmsFull = 0;      // owner: no free slot - that hit's knock could not be seen

// Copy side (MAIN THREAD only: ApplyRemoteHit pushes, the safe point drains). Bounded, no allocation.
const int kKnockPendingMax = 32;
unsigned int g_knockPending[kKnockPendingMax];
int g_knockPendingCount = 0;
long long g_knockSent = 0;              // owner: HIT sent with the knock byte set
long long g_knockApplied = 0;           // copy: ragdollMode(copy,1,1) called at the safe point
long long g_knockSkippedGone = 0;       // copy: no local copy, or no longer a copy, or unreadable / world blocked
long long g_knockSkippedAlready = 0;    // copy: already a ragdoll, or already pending
long long g_knockSkippedRebuild = 0;    // crash1 (T293 / F912): copy: a body rebuild queued, or its looks not applied yet
long long g_knockOverflow = 0;          // copy: the pending list was full
long long g_knockNotPlayed = 0;         // copy: knock byte set but the hit was not played, or its health not applied - no fall

// Owner, hit thread. Returns the slot index, or -1 (counted) when every slot is taken.
int KnockArmClaim(const void* victim)
{
    const LONG tid = (LONG)::GetCurrentThreadId();
    for (int i = 0; i < kKnockArms; ++i)
    {
        if (InterlockedCompareExchange(&g_knockArm[i].tid, tid, 0) == 0)
        {
            g_knockArm[i].victim = victim;
            InterlockedExchange(&g_knockArm[i].knocked, 0);
            return i;
        }
    }
    InterlockedIncrement64(&g_knockArmsFull);
    return -1;
}

// Owner, hit thread. Frees the slot; 1 if applyDamage marked a knock while it was armed.
unsigned char KnockArmRelease(int i)
{
    if (i < 0 || i >= kKnockArms) return 0;
    const LONG k = InterlockedExchange(&g_knockArm[i].knocked, 0);
    g_knockArm[i].victim = 0;
    InterlockedExchange(&g_knockArm[i].tid, 0);
    return (unsigned char)(k != 0 ? 1 : 0);
}

// Copy, MAIN THREAD. The id - never the pointer - is kept; the safe point re-finds it.
void KnockPush(unsigned int uid)
{
    for (int i = 0; i < g_knockPendingCount; ++i)
        if (g_knockPending[i] == uid) { ++g_knockSkippedAlready; return; }
    if (g_knockPendingCount >= kKnockPendingMax) { ++g_knockOverflow; return; }
    g_knockPending[g_knockPendingCount++] = uid;
}

// P012 attribution logging, main thread only - bounded so an ambient world cannot drown
// the run this is meant to explain.
// P015 (H008): bounded, and it fires only on an actual transition - a stable state costs
// nothing, so this cannot drown the run it exists to explain.
const long long kProneLogLimit = 16;
long long g_proneLogged = 0;

const long long kEmitLogLimit = 12;
long long g_emitLogged = 0;

// P016 (PARITY P-2): bounded on each side. An hour-long soak in a populated town would
// otherwise turn the identity log into the whole file.
const long long kIdLogLimit = 24;
long long g_idLogged = 0;

std::string N3(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

// P048 - a byte run as hex, for comparing a function prologue against a known pattern by eye.
std::string HexBytes(const unsigned char* p, int n)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < n; ++i)
    {
        if (i) out += ' ';
        out += kDigits[(p[i] >> 4) & 0xF];
        out += kDigits[p[i] & 0xF];
    }
    return out;
}

// P046 - a pointer as hex, because the comparison a reader makes with these is "is this the same
// object as that one", and two 12-digit decimals are the wrong shape for that question.
std::string Ptr(const void* p)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << (unsigned long long)(uintptr_t)p;
    return ss.str();
}

// One decimal place, classic locale. Same shape as spawn.cpp's, which lives in an anonymous
// namespace there and so is not reachable from here.
long long g_f1NonFinite = 0;   // P059: a NaN/INF would print as `1.$` - not a number, not JSON
std::string F1(float v)
{
    if (_finite((double)v) == 0) { ++g_f1NonFinite; return std::string("-2.0"); }
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << v;
    return ss.str();
}

// P058 / F405 - **SIX DECIMALS, BECAUSE THE DECISIVE VALUE OF THIS QUANTITY IS EXACT ZERO.**
//
// The task duration fields are in **in-game hours** (F405), where the interesting magnitudes are
// tiny. `F1` prints one decimal place, so **everything below 0.05 renders as `0.0` - the same
// characters as an exact zero** - while the reading that would settle door 1 is precisely
// `durationMin == 0 && durationFuzz == 0`, which makes the expiry equal to *now* and fires on the
// next evaluation. A formatter that cannot separate "zero" from "small" cannot answer the question
// the field was added for.
long long g_f6NonFinite = 0;
std::string F6(float v)
{
    if (_finite((double)v) == 0) { ++g_f6NonFinite; return std::string("-2.0"); }
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(6);
    ss << v;
    return ss.str();
}

// Reverse lookup: is this character one we hold a uid for? Only replicated characters are
// interesting; the world's own NPCs fighting each other are not our business.
unsigned int UidOf(Character* c)
{
    return FindSpawnedUid(c);
}

// The detour. Thread is UNKNOWN until measured (M3's test records it) - so this is written
// to the same hostile-thread rules as the other detours: no allocation, no locks, and the
// only logging is behind a counter check that fires rarely.
int detour_hitByMelee(Character* self, int cutDir, void* damages,
                      Character* attacker, void* technique, int comboId)
{
    InterlockedIncrement64(&g_hitsSeen);
    if (g_hitThreadId == 0)
        InterlockedExchange(&g_hitThreadId, (LONG)::GetCurrentThreadId());

    // A hit we are applying on the authority's behalf: run it, but never re-emit it.
    if (g_applyingRemote)
        return orig_hitByMelee(self, cutDir, damages, attacker, technique, comboId);

    unsigned int uid = UidOf(self);
    if (uid != 0)
    {
        if (net::IsUidMineAnyThread(uid))   /* O1: this is the AI worker (T024/F095) - never the std::set */
        {
            // OURS: resolve normally, and ENQUEUE the outcome for the main thread to send.
            // Never send inline - this runs on the AI worker thread (T024/F095).
            // K2 (decision 61 follow-up): the entry is FILLED here, while the attacker pointer is valid,
            // but PUBLISHED only after the original has run - whether this hit knocked the victim down
            // is known only then. A stack copy; no allocation.
            PendingHit slot;
            {
                slot.uid     = uid;
                slot.cutDir  = cutDir;
                slot.comboId = comboId;
                std::memcpy(slot.damages, damages, kDamagesBytes);
                slot.victimObj   = self;        // P012 attribution, main thread logs it
                slot.attackerObj = attacker;
                // F117: the peer needs the attacker to apply the hit at all. Resolved here
                // because the pointer is only valid on this thread, at this moment; another
                // flat-array scan, no allocation and no lock, same as the victim's.
                slot.attackerUid = UidOf(attacker);
                // PARITY P-2: only when the attacker has no uid. A replicated character is
                // FACTORY-created on each side and its handle is minted per process, so its
                // handle would name a different object on the peer - the identity is a
                // fallback for objects that came from the save, not a second uid.
                slot.attackerId.index = slot.attackerId.serial = 0;
                slot.attackerId.type  = slot.attackerId.container =
                    slot.attackerId.containerStamp = 0;
                if (slot.attackerUid == 0)
                    CaptureObjId(attacker, &slot.attackerId);
                slot.knock = 0;
            }
            const int arm = KnockArmClaim(self);   // K2: applyDamage marks it on this thread
            const int result = orig_hitByMelee(self, cutDir, damages, attacker, technique, comboId);
            slot.knock = KnockArmRelease(arm);

            LONG w = g_hitWrite;
            if (((w + 1) & kHitQueueMask) == (g_hitRead & kHitQueueMask))
            {
                // Full. Drop and count rather than block: blocking the AI thread is the one
                // outcome worse than losing a hit, and the count makes the loss visible
                // instead of silent.
                InterlockedIncrement64(&g_hitsDropped);
            }
            else
            {
                g_hitQueue[w & kHitQueueMask] = slot;
                // Publish the slot only after it is fully written.
                InterlockedExchange(&g_hitWrite, w + 1);
                InterlockedIncrement64(&g_hitsEmitted);
                LONG depth = (w + 1) - g_hitRead;
                if (depth > g_hitQueueHigh) InterlockedExchange(&g_hitQueueHigh, depth);
            }
            return result;
        }
        else
        {
            // THEIRS: the owner will tell us the outcome. Applying local damage as well
            // would double-count and diverge.
            InterlockedIncrement64(&g_hitsSuppressed);
            return 0;   // HitMaterialType 0 - no material reaction
        }
    }

    // F349 - A PUPPET'S BLOW MUST NOT DAMAGE A WORLD NPC. Introduced by, and shipped with, the
    // swing replication of F348.
    //
    // Until F348 the peer's puppets never swung, so this combination could not arise. Now they do,
    // and a Kenshi swing damages everyone in the attack zone (`calculateTargetsInAttackZone`), not
    // only the declared target. The victim rule above only covers victims that HAVE a uid: a world
    // NPC has none, so without this the peer alone would take damage from a blow the authority's
    // world never dealt.
    //
    // The test is on the ATTACKER, not the victim, and the asymmetry is deliberate:
    //   * attacker is a PUPPET, victim is not ours -> suppress. A puppet simulates nothing of its
    //     own; every consequence of its actions is the authority's to send.
    //   * attacker is a puppet, victim IS ours -> APPLY. The authority does not own our character,
    //     so its own hook suppresses that hit and sends nothing - this local application is the
    //     only way our character can ever be hurt by theirs.
    //   * attacker is a world NPC -> untouched, exactly as before.
    if (attacker != 0)
    {
        unsigned int auid = UidOf(attacker);
        if (auid != 0 && !net::IsUidMineAnyThread(auid))   /* O1: still detour_hitByMelee, still the AI worker */
        {
            InterlockedIncrement64(&g_hitsPuppetThrown);
            return 0;
        }
    }

    // Not a replicated character: untouched.
    return orig_hitByMelee(self, cutDir, damages, attacker, technique, comboId);
}

} // namespace

// K2: see combat.h. Any thread (the AI worker in practice); only THIS thread's armed slot for this
// character is marked, so no other thread's hit can be credited with the knock.
void NoteKnockEdgeAnyThread(const void* character)
{
    const LONG tid = (LONG)::GetCurrentThreadId();
    for (int i = 0; i < kKnockArms; ++i)
        if (g_knockArm[i].tid == tid && g_knockArm[i].victim == character)
            InterlockedExchange(&g_knockArm[i].knocked, 1);
}

void ApplyRemoteHit(unsigned int victimUid, unsigned int attackerUid, int cutDirection,
                    const char* damages24, int comboId,
                    const float* parts, int partCount, float blood,
                    const ObjId& attackerId, bool knock)
{
    Character* victim = FindSpawned(victimUid);
    if (victim == 0)
    {
        if (knock) ++g_knockSkippedGone;   // K2
        DebugLog("[M3] HIT for unknown uid=" + N3(victimUid) + " - no local copy (F079)");
        return;
    }
    if (orig_hitByMelee == 0)
    {
        if (knock) ++g_knockNotPlayed;   // K2
        ErrorLog("[M3] HIT dropped: the combat hook is not installed");
        return;
    }

    // F117 - THE ATTACKER IS NOT OPTIONAL. T033 killed instance B 0.30 s after its first
    // inbound HIT, because this call passed 0 for the attacker. Confirmed offline in the
    // engine's own code (RVA 0x439150): the attacker parameter is dereferenced
    // UNCONDITIONALLY at `attacker[0x8a]` before any null check, so a null attacker is an
    // access violation, not a degraded hit. It now travels with the message.
    // --- 1. PLAY the hit, so the peer SEES the attack --------------------------------
    //
    // F119 + the design constraint: the outcome is authoritative, but the EVENT must still
    // be the engine's own - animation, impact, stagger, reaction - or the peer would watch
    // a character take damage with nothing visibly happening to it. So the hit is still run
    // through the engine here; only its result is overwritten below.
    //
    // THREE things can make a hit unplayable, and all take the same exit: skip the visual, go
    // straight to the authoritative health, count it so the silence is visible.
    //   * an unresolvable ATTACKER - the engine dereferences it (F117);
    //   * a VICTIM the engine cannot orient - T240, tested just below the attacker lookup;
    //   * an ATTACKER the engine cannot orient, when the victim's gate makes addWound read it - C1-b.
    Character* attacker = FindSpawned(attackerUid);

    // PARITY P-2, and this is the whole point of the change: if the attacker is not one of
    // ours, it is very likely still SITTING IN THIS WORLD - both instances loaded the same
    // save. It had no uid because our mirror only covers characters we created, which is a
    // gap in ADDRESSING, not in replication. Ask the engine's own handle registry for it.
    //
    // Deliberately a SECOND attempt and not a replacement: the uid path is exact, this one
    // is best-effort, and the two outcomes are counted apart so the fraction of P-2 that
    // this actually closes is measured rather than assumed. Runtime-created world squads
    // get their handles from a per-process counter and are expected to MISS here.
    const char* attackerSource = "uid";
    if (attacker == 0 && ObjIdValid(attackerId))
    {
        attacker = ResolveObjId(attackerId);
        if (attacker != 0)
        {
            attackerSource = "handle";
            InterlockedIncrement64(&g_attackerByHandle);
        }
        else
        {
            InterlockedIncrement64(&g_attackerIdMissed);
        }
    }
    else if (attacker == 0 && !ObjIdValid(attackerId))
    {
        InterlockedIncrement64(&g_attackerNoIdSent);
    }

    // P016 (ships WITH the fix - lesson 7): name the attacker on BOTH sides. The authority
    // logs the identity it sent and the name it resolves locally; the peer logs what that
    // identity resolved to here. Diffing the two names offline is what proves the handle
    // named the SAME character rather than merely a valid one - a check no counter can make.
    if (ObjIdValid(attackerId) && g_idLogged < kIdLogLimit)
    {
        ++g_idLogged;
        std::string nm = "<unresolved>";
        if (attacker != 0) nm = attacker->getShownNameDirect();
        DebugLog("[P016] in  victim=" + N3(victimUid) + " attackerId='"
                 + ObjIdString(attackerId) + "' resolved="
                 + std::string(attacker != 0 ? "YES" : "NO") + " name='" + nm + "'");
    }

    // T240 - AND THE VICTIM MUST BE ORIENTABLE. B died in MedicalSystem::addWound on a
    // character re-spawned 0.5 s earlier: its appearance change was still in flight, so its
    // entity had no parent scene node and the engine read that node's orientation anyway.
    // Asked immediately before the play, because the answer is only true for this instant.
    const char* notPlayedWhy = "no local attacker (parity P-2)";
    void* hbAnim = 0; void* hbApp = 0; void* hbEnt = 0;
    int   hbReason = kHitBodyAttached;
    int   hbGate = 0;
    void* atAnim = 0; void* atApp = 0; void* atEnt = 0;
    const char* atWhy = "-";
    bool  orientable = false;
    if (attacker != 0)
    {
        orientable =
            (AppearanceBodyAttached(victim, &hbAnim, &hbApp, &hbEnt, &hbReason, &hbGate) != 0);

        // C1-b (review 2026-09-22 H4) - AND SO MUST THE ATTACKER, whenever the victim's gate is
        // set. decomp_64fe40.txt:425-435: inside the victim's +0x88 branch, with no harpoon
        // (always, for melee), addWound walks the ATTACKER's +0x448 -> +0xE8 -> +0xD8 ->
        // getParentSceneNode -> getOrientation with NO gate on the attacker's own +0x88, so the
        // attacker is asked with honourGate = false. A victim whose gate is 0 reads neither node.
        if (orientable && hbGate != 0)
        {
            int atReason = kHitBodyAttached;
            if (AppearanceBodyChain(attacker, false, &atAnim, &atApp, &atEnt, &atReason, 0) == 0)
            {
                orientable = false;
                hbReason   = kHitAttackerNotOrientable;
                atWhy      = kHitBodyReasonWord[atReason];
            }
        }
        if (!orientable)
        {
            notPlayedWhy = kHitBodyReasonWord[hbReason];
            InterlockedIncrement64(&g_hitPlaySkipped[hbReason]);
            if (g_hitSkipLogged < kHitSkipLogLimit)
            {
                ++g_hitSkipLogged;
                DebugLog("[M3] HIT play skipped uid=" + N3(victimUid)
                         + " why=" + std::string(notPlayedWhy)
                         + " anim=" + Ptr(hbAnim) + " appearance=" + Ptr(hbApp)
                         + " entity=" + Ptr(hbEnt) + " gate=" + N3(hbGate)
                         + (hbReason == kHitAttackerNotOrientable
                                ? std::string(" attackerWhy=") + atWhy
                                  + " attackerAnim=" + Ptr(atAnim)
                                  + " attackerAppearance=" + Ptr(atApp)
                                  + " attackerEntity=" + Ptr(atEnt)
                                : std::string())
                         + " - the health outcome is still applied (T240: the engine's"
                           " addWound reads the entity's parent scene node unconditionally)");
            }
        }
    }

    bool played = false;
    if (orientable)
    {
        char dmg[kDamagesBytes];
        std::memcpy(dmg, damages24, kDamagesBytes);

        // `technique` stays 0. It is static game data whose address differs per process, so
        // a pointer cannot travel; whether the engine dereferences it unconditionally too
        // is NOT established - the attacker alone accounted for T033's crash.
        // PROBE P015 (DIAGNOSTIC, H008): T038 measured a peer KNOCKED OUT while its
        // authority stood upright, from a 1.5-unit flesh difference. The suspicion is that
        // the cause is OURS - that the engine's own damage roll during the hit we play
        // drives its collapse check before our health correction runs. Sampling PoseState
        // either side of this ONE call settles it: a transition INSIDE means the mechanism
        // is ours. Changes nothing; competing explanations stay live until this speaks.
        int proneBefore = (int)victim->poseState();

        InterlockedExchange(&g_applyingRemote, 1);
        orig_hitByMelee(victim, cutDirection, dmg, attacker, 0, comboId);
        InterlockedExchange(&g_applyingRemote, 0);

        int proneAfter = (int)victim->poseState();
        if (proneBefore != proneAfter && g_proneLogged < kProneLogLimit)
        {
            ++g_proneLogged;
            DebugLog("[P015] prone changed INSIDE the played hit: uid=" + N3(victimUid)
                     + " " + N3(proneBefore) + " -> " + N3(proneAfter)
                     + " (H008: the engine's own roll, not the authority's)");
        }

        played = true;
        InterlockedIncrement64(&g_hitsApplied);
        InterlockedIncrement64(&g_hitPlayed);

        // The first played hit of the session names the three hops the wound path will walk.
        // A permanent line, not a temporary probe: it is what a readout compares a skip line
        // against, and without it "entity=0" on a skip has nothing to be unusual next to.
        if (!g_hitPlayProbeLogged)
        {
            g_hitPlayProbeLogged = true;
            DebugLog("[M3] hitPlay probe uid=" + N3(victimUid)
                     + " anim=" + Ptr(hbAnim) + " appearance=" + Ptr(hbApp)
                     + " entity=" + Ptr(hbEnt)
                     + " gate=" + N3(hbGate)
                     + " attached=" + N3(hbReason == kHitBodyAttached ? 1 : 0));
        }
    }
    else if (attacker == 0)
    {
        InterlockedIncrement64(&g_hitsNoAttacker);
    }
    // --- 2. OVERWRITE the outcome with the authority's -------------------------------
    //
    // The engine has just rolled its OWN body part for that hit (F119: hitWeight per part,
    // rolled independently per instance) - which is exactly the divergence T034 measured.
    // Writing the authority's per-limb values immediately, in the same frame, means the
    // visible event is the engine's and the injury is the authority's.
    bool corrected = false;
    if (partCount > 0)
        corrected = ApplyHealth(victimUid, parts, partCount, blood);

    // K2 (decision 61 follow-up): this hit knocked the owner down, so the copy's fall starts on it too -
    // from the pose the played hit just put it in. NOT called here: ragdollMode's request queue
    // (Character +0x3E0) has no lock and the worker may be running; the id is queued for the safe point
    // (threadSafeRagdollUpdates 0x7D17E0), which also sets the copy's +0x161 as applyDamage does.
    // review-k2 MEDIUM: only when the owner's health was applied (`corrected`): ApplyState writes the latch
    // (ApplyLatch, +0x161 both ways) only after the same ApplyHealth succeeds, so a copy whose health
    // cannot be applied could never have that flag cleared again. C2 stays: no knockout timer from this.
    if (knock)
    {
        if (played && corrected) KnockPush(victimUid);
        else ++g_knockNotPlayed;   // no played hit, or no applied health: no fall is started
    }

    DebugLog(std::string("[M3b] remote hit uid=") + N3(victimUid)
             + " from attacker uid=" + N3(attackerUid)
             + " dir=" + N3(cutDirection) + " combo=" + N3(comboId)
             + (knock ? std::string(" KNOCK") : std::string())
             + (played ? std::string(" PLAYED (attacker by ") + attackerSource + ")"
                       : std::string(" NOT PLAYED - ") + notPlayedWhy
                         + ", so this hit lands with NO VISUAL on this screen")
             + (corrected ? ", health CORRECTED to authority"
                          : (partCount > 0 ? ", health correction FAILED"
                                           : ", no health block in message")));
}

// =====================================================================================================================
// P104 (2026-09-30) - RANGED COMBAT, MEASURED. COUNTING ONLY: BOTH HOOKS RUN THE ORIGINAL UNCHANGED.
//
// The road (Read, Ghidra 1.0.65 decompiles 4388d0 / 43ab40 / 43a390 / 43ac40 / 4398f0, xrefs):
//   RangedCombatClass::updateT 0x4388D0 chooses the target and raises takeAShotMsg (+0x35);
//   RangedCombatClass::updateMT 0x43AB40 (its one caller: Character::update 0x5CE6A0) runs reloadCheck 0x437940 and
//   then GunClass::shoot 0x43A390(gun, me = rc+0x68, target, stat, aimPos) - shoot's ONLY caller. shoot takes one
//   loaded bolt off the gun (+0x20), takes a projectile (Harpoon) from the engine's pool, aims it with a random
//   deviation, stores its damage on it (+0x40..+0x4C), plays "Attack" and warns the target's AI.
//   The projectile manager's update 0x43AC40 (its one caller: GameWorld::mainLoop 0x787E70) flies every projectile;
//   one that struck a character (state 2) has its shooter found from its handle (+0x20) and calls
//   Character::iShotYou 0x4398F0(victim = harpoon+0x78, shooter, harpoon, onPurpose) - iShotYou's ONLY caller - which
//   builds the Damages from harpoon+0x4C and calls addWound 0x64FE40 -> applyDamage 0x64E870 (medical.cpp C2-b skips
//   that for a copy). A turret fires through the same shoot, from its operator's RangedCombatClass (turret hand +0x8).
// Nothing of a shot crosses the wire: the projectile exists only on the game whose character fired it, and iShotYou
// had no hook, so a bolt from game X's character into game Y's character lands on X's COPY, whose damage C2-b skips,
// and Y is never told (Confirmed T668: A's bolt, dmg 69.4, on B's copy; B's partSum 1414 / blood 75.8 unchanged on both games).
//
// P104 FIX (protocol 104) - THE SHOOTER'S OWNER DECIDES THE HIT, THE VICTIM'S OWNER PLAYS IT.
//   * iShotYou on the shooter's game, OUR shooter, a COPY victim: the engine's hit still runs (C2-b skips its damage) and the
//     main thread sends MSG_SHOT 63 {victim, shooter, the bolt's record +0x40..+0x4C, onPurpose} to the victim's owner.
//   * The victim's owner queues it and, at the K2 safe point (worker paused), calls MedicalSystem::addWound 0x64FE40 on its own
//     character exactly as iShotYou does (decomp_4398f0.txt:34-35): (victim +0x458 = its MedicalSystem, 0, 6, &Damages
//     {0,0,+0x4C,0,2.0,0}, &armour (-1 in), the shooter's copy, &side, harpoon 0). Harpoon 0 is the melee road: addWound then
//     orients on the attacker's body instead of the projectile's node (decomp_64fe40.txt:425-458), so the victim and the
//     shooter's copy must be orientable (T240 / C1-b, the same test as ApplyRemoteHit). applyDamage runs inside (our own
//     character: C2-b lets it through), so wounds, blood, knock-out and death are the engine's; the result reaches the
//     shooter's game in our STATE.
//   * A COPY's bolt (the shooter is not ours) makes no hit on this game: iShotYou returns 1 without the engine's hit. Only the
//     shooter's owner decides hits (T668: a copy never fired, shotsCopy=0 on both games - this is the guard for when one does).
//   * T-311 (pvp1 'Being attacked keeps the standard automatic combat response'): after the wound the owner also plays
//     iShotYou's victim-side reactions with the shooter's COPY as the attacker - rememberCharacter 0x673A10 (ST_TEMPORARY_ENEMY,
//     onPurpose), AI::underRangedAttack 0x998E80 (onPurpose, combat +0x130 clear) and the hit reaction 0x438E50 - see ShotReactOne.
//     underRangedAttack calls the victim's faction relations' slot +0x28, the 'change standing by event' setter
//     (decomp_998e80.txt:38). The victim here is this game's own character, whose faction's relations are the player's
//     PlayerFactionRelations, and its slot +0x28 is empty (H071): the call moves no standing on the victim's game.
//     Still not played: iShotYou's own faction-relation calls behind victim +0x5B9 / +0x5BC, 0x8C5AB0 (victim +0x450) and
//     the "Impact"/"Deflection" sound - T-311 LEFTOVERS.
// =====================================================================================================================
namespace {
unsigned long long kP104IShotYou = 0; static coop::AddrReg kP104IShotYou_reg("Character_shotBy", &kP104IShotYou);   /* Steam_1.0.65 0x4398F0 */
unsigned long long kP104GunShoot = 0; static coop::AddrReg kP104GunShoot_reg("GunClass_shoot", &kP104GunShoot);       /* Steam_1.0.65 0x43A390 */

const size_t kRsCharRanged   = 0x2F0;   /* Character::rangedCombat */
const size_t kRsRcTurretType = 0x10;    /* RangedCombatClass::turret (a hand at +0x8), the hand's type field (+0x8) */
const size_t kRsRcGun        = 0x28;    /* RangedCombatClass::gun (items.cpp kCcRcGun) */
const size_t kRsRcCombatMode = 0x36;    /* RangedCombatClass::combatMode - updateMT's first test */
const size_t kRsGunLoaded    = 0x20;    /* GunClass: loaded bolts, shoot's decrement (items.cpp kCcGunLoaded) */
const size_t kRsHarpoonDmg   = 0x4C;    /* the projectile's damage, iShotYou's Damages third float */
const int    kRsRangedAttackFocused = 263;   /* TaskType RANGED_ATTACK_FOCUSED (tasktype-classification.md) */

typedef void (*P104ShootFn)(void*, ::Character*, void*, int, const void*);
typedef bool (*P104ShotYouFn)(::Character*, ::Character*, void*, bool);
P104ShootFn   orig_p104Shoot   = 0;
P104ShotYouFn orig_p104ShotYou = 0;

/* P104 fix: addWound has no table row. Its entry is the ApplyDamageHitRet row (addWound's own applyDamage call returns there)
   minus 0x7CC; gen_table_1068 carries a ret row only when its function matched 'same' (whole function unchanged, only moved), so
   the offset holds on 1.0.68 too (0x65109C - 0x7CC = 0x6508D0). VERIFIED at install before use: the CALL before that return
   reaches the ApplyDamage row's entry, and iShotYou's own body CALLs the derived entry (through its E9 stub). Otherwise OFF. */
typedef void* (*P104AddWoundFn)(void*, char, int, float*, int*, ::Character*, int*, void*);
unsigned long long kP104ApplyDamage = 0; static coop::AddrReg kP104ApplyDamage_reg("ApplyDamage", &kP104ApplyDamage);   /* Steam_1.0.65 0x64E870 */
unsigned long long kP104AddWoundDmgRet = 0; static coop::AddrReg kP104AddWoundDmgRet_reg("ApplyDamageHitRet", &kP104AddWoundDmgRet);   /* Steam_1.0.65 0x65060C */
const unsigned long long kP104AddWoundRetOff = 0x7CC;   /* 0x65060C - 0x64FE40 (1.0.65) */
const size_t kRsCharMedical  = 0x458;   /* Character -> MedicalSystem: iShotYou's param_1 + 0x8b (8-byte units) */
const size_t kRsHarpoonRec   = 0x40;    /* the bolt's record +0x40..+0x4C, written by GunClass::shoot (decomp_43a390.txt:108-118) */
const int    kRsAttackRanged = 6;       /* addWound's attack type for a bolt (iShotYou's 3rd argument) */
P104AddWoundFn g_p104AddWound = 0;
int g_p104AddWoundCallAt = -1;          /* the offset in iShotYou of the CALL that verified it */
int g_p104AddWoundWhy = 0;              /* 1 verified; 0 a row unbound; -1 the ret's CALL does not reach applyDamage; -2 iShotYou does not call it; -3 unreadable */

volatile LONG64 g_rsShots = 0, g_rsShotsMine = 0, g_rsShotsCopy = 0, g_rsShotsNone = 0, g_rsShotsTurret = 0;
volatile LONG64 g_rsHits = 0, g_rsHitVictimMine = 0, g_rsHitVictimCopy = 0, g_rsHitVictimNone = 0;
volatile LONG64 g_rsHitCross = 0;      /* shooter ours, victim a copy: the engine's damage is skipped here and nothing is sent */
volatile LONG64 g_rsHitFromCopy = 0;   /* shooter a copy: a bolt a copy fired on this game */
volatile LONG64 g_rsDropped = 0;       /* the event ring was full (the counters above still count) */
volatile LONG64 g_rsHitCopyBlocked = 0;   /* P104 fix: a COPY's bolt struck something here - no hit (the shooter's owner decides) */
/* P104 fix, MAIN THREAD only (the drain, the net pump, the K2 safe point) */
long long g_shotSent = 0, g_shotSendFail = 0, g_shotSendBad = 0, g_shotOutLogged = 0, g_rsBlockLogged = 0;
long long g_shotIn = 0, g_shotApplied = 0, g_shotRefusedNotMine = 0, g_shotRefusedShooter = 0, g_shotMalformed = 0, g_shotQDrop = 0;
long long g_shotNoVictim = 0, g_shotNoShooter = 0, g_shotNotOrientable = 0, g_shotBlockedLoad = 0, g_shotFault = 0, g_shotNoAddWound = 0;
long long g_shotInLogged = 0;
/* T-311, MAIN THREAD: the victim-side reactions after a SHOT-IN wound (ShotReactOne) and the follow-up WATCH lines */
long long g_shotReactRuns = 0, g_shotReactRemember = 0, g_shotReactUnderRanged = 0, g_shotReactHitReaction = 0;
long long g_shotReactNoCopy = 0, g_shotReactNoRow = 0, g_shotReactFault = 0, g_shotReactNoAI = 0, g_shotReactNoPart = 0;
long long g_shotReactNotOnPurpose = 0, g_shotReactFlag130 = 0, g_shotReactEnemy = 0, g_shotReactLogged = 0;
long long g_shotReactFlagUnread = 0, g_shotReactNotChar = 0;   /* T-311 fold 1: underRangedAttack skipped - combat +0x130 unread / attacker type != 1 */
long long g_shotWatchLines = 0, g_shotWatchTurned = 0, g_shotWatchGone = 0, g_shotWatchBusy = 0;
struct ShotIn { unsigned int victim; unsigned int shooter; float rec[4]; bool onPurpose; };
std::vector<ShotIn> g_shotInQ;          /* net pump -> K2 safe point, both MAIN THREAD */
const size_t kShotInMax = 256;
volatile LONG   g_rsShootThread = 0, g_rsHitThread = 0;
long long g_rsHooks = 0, g_rsLeverRuns = 0, g_rsShotLogged = 0, g_rsHitLogged = 0;
const long long kRsLogLimit = 80;

// Vyukov's bounded queue: many producers (whatever thread shoots or is hit), one consumer (the main thread).
struct RsEvent { volatile LONG seq; int kind; unsigned int who; unsigned int other; int whoMine; int otherMine; float dmg; int flag; float rec[4]; };
const LONG kRsRing = 64;   /* power of two */
RsEvent g_rsRing[kRsRing];
volatile LONG g_rsEnq = 0;
LONG g_rsDeq = 0;          /* main thread only */
volatile LONG g_rsRingReady = 0;

bool RsPlaus(const void* p)
{
    const uintptr_t v = (uintptr_t)p;
    return v >= ((uintptr_t)1 << 16) && v < ((uintptr_t)1 << 47) && (v & 7) == 0;
}

// 1 = ours, 0 = a copy (replicated, not ours), -1 = not replicated (uid 0). Address compares only - any thread.
int RsOwn(const void* obj, unsigned int* uid)
{
    const unsigned int u = (obj != 0) ? FindSpawnedUid(obj) : 0;
    *uid = u;
    if (u == 0) return -1;
    return net::IsUidMineAnyThread(u) ? 1 : 0;
}

void RsPush(int kind, unsigned int who, int whoMine, unsigned int other, int otherMine, float dmg, int flag, const float* rec)
{
    if (g_rsRingReady == 0) { InterlockedIncrement64(&g_rsDropped); return; }
    for (;;)
    {
        const LONG pos = g_rsEnq;
        RsEvent* e = &g_rsRing[pos & (kRsRing - 1)];
        const LONG dif = e->seq - pos;
        if (dif == 0)
        {
            if (InterlockedCompareExchange(&g_rsEnq, pos + 1, pos) != pos) continue;
            e->kind = kind; e->who = who; e->whoMine = whoMine; e->other = other; e->otherMine = otherMine;
            e->dmg = dmg; e->flag = flag;
            for (int r = 0; r < 4; ++r) e->rec[r] = (rec != 0) ? rec[r] : -1.0f;   /* P104 fix: the bolt's record */
            InterlockedExchange(&e->seq, pos + 1);
            return;
        }
        if (dif < 0) { InterlockedIncrement64(&g_rsDropped); return; }
        /* dif > 0: another producer took this position first - reload and retry */
    }
}

// =====================================================================================================================
// PROBE-START: P112 (T-310, 2026-09-30) - WHY A BOLT AT THE OTHER GAME'S CHARACTER'S COPY MISSES FROM A DISTANCE, AND WHY
// THE SHOOTER HELD FIRE AT POINT-BLANK. READ ONLY: both hooks run the original unchanged; nothing is written.
// (Read, Ghidra 1.0.65: build/decomp_4388d0 / 437730 / 4357a0 / 434330 / 436970 / 437d40 / 43ac40.)
//   Aim check 0x437730(rc, out, target) - its one caller RangedCombatClass::updateT 0x4388D0 (the AI thread). *out:
//     1 FIRE (updateT raises takeAShotMsg +0x35), 0 BLOCKED (an object that is not a character, nearer than the target:
//     updateT walks to get a line), 2 WAIT. rc +0x28 gun, +0x38 aim point (updateMT: 0x436970 = the target's
//     "Bip01 Spine2" bone + a velocity lead), +0x68 the shooter, +0x70 the aim timer (0.4 s, counted down while loaded;
//     the ray is cast only when it runs out).
//   The ray 0x4357A0 is the gun's own ASYNC query: gun +0x80 -> holder, holder +8 -> record (+0x7C done, +4 flag, +7 hit,
//     +8 hit point, +0x20 distance, +0x38 hand type, +0x50 hit kind (2 = terrain), +0x58 object, +0x60 ray origin).
//     Not done -> a static record whose +4 is 1 -> WAIT. A character on the ray -> WAIT unless it is the shooter or one the
//     shooter counts as an enemy (vt +0x3E8). A hit with no object (terrain) -> FIRE: terrain never stops the aim check.
//   Bolt 0x437D40(harpoon) - its one caller the projectile manager 0x43AC40 (GameWorld::mainLoop, the main thread):
//     +0x70 state 1 flying, 2 struck a character (+0x78, then iShotYou), 3 stuck at the record's hit point (terrain /
//     building), 0 spent (range +0x40 run out, or a glancing terrain hit); its own query holder +0x68 (same record; +0x60 =
//     this frame's origin); age +0x60. A character that is not the intended target and not an enemy may be flown through.
// =====================================================================================================================
unsigned long long kP112AimCheck = 0; static coop::AddrReg kP112AimCheck_reg("RangedCombat_aimTest", &kP112AimCheck);   /* Steam_1.0.65 0x437730 */
unsigned long long kP112BoltUpdate = 0; static coop::AddrReg kP112BoltUpdate_reg("Harpoon_update", &kP112BoltUpdate);     /* Steam_1.0.65 0x437D40 */

const size_t kP112RcGun = 0x28, kP112RcAim = 0x38, kP112RcMe = 0x68, kP112RcTimer = 0x70;
const size_t kP112GunQuery = 0x80, kP112HolderRec = 0x8;
const size_t kP112BoltHolder = 0x68, kP112BoltState = 0x70, kP112BoltVictim = 0x78, kP112BoltAge = 0x60, kP112BoltRange = 0x40;
const size_t kP112CharPos = 0x48;                      /* Character logical position (replicate.cpp P071ReadPos) */
const size_t kP112CharAnim = 0x448, kP112AnimPos = 0x98;   /* AnimationClass, its position (P071ReadPos) */
const float  kP112AxisH = 20.0f;   /* the target's body axis: feet to feet + 20 units (Inferred: ~2 m at 10 units a metre) */
const float  kP112Timer0 = 0.4f;   /* 0x3ECCCCCD, the aim timer's reset value */
enum { kP112WhyUnknown = 0, kP112WhyNoTarget, kP112WhyNotReady, kP112WhyNotLoaded, kP112WhyTimer, kP112WhyRayPending,
       kP112WhyFlag4, kP112WhyNonEnemyChar, kP112WhyClear, kP112WhyCharEnemyOrSelf, kP112WhyNoObjOrFar, kP112WhyObjBlocks,
       kP112WhyOdd, kP112WhyCount };
const char* const kP112WhyWord[kP112WhyCount] = { "unknown", "noTarget", "notReady(reload/draw)", "notLoaded(timerReset)",
       "aimTimer", "rayPending", "rayFlag4", "NON-ENEMY-CHARACTER-ON-THE-RAY", "clear", "enemyOrSelfOnTheRay",
       "terrainOrObjectPastTheTarget", "OBJECT-BEFORE-THE-TARGET", "odd" };

struct P112Rec { int ok; int done; int f4; int hit; int kind; int hand; float pos[3]; float dist; const void* obj; float origin[3]; };
struct P112Ev
{
    int kind;                 /* 1 aim check, 2 bolt end, 3 shot */
    int verdict, why, loaded, shotNo, frames;
    unsigned int su, tu, hu;
    int sm, tm, hm;
    float tPre, tPost, age, rangeLeft;
    float me[3], tg[3], aim[3], from[3];
    P112Rec r;
    float minAim, minBody, bodyHoriz, bodyHeight;
    DWORD tick;
};
struct P112Slot { volatile LONG seq; P112Ev ev; };
const LONG kP112Ring = 256;   /* power of two */
P112Slot g_p112Ring[kP112Ring];
volatile LONG g_p112Enq = 0;
LONG g_p112Deq = 0;           /* main thread only */
volatile LONG g_p112Ready = 0;
volatile LONG64 g_p112Dropped = 0, g_p112AimN = 0;
volatile LONG64 g_p112Verdict[3] = { 0, 0, 0 };
volatile LONG64 g_p112Why[kP112WhyCount] = { 0 };
volatile LONG64 g_p112RayChar[3] = { 0, 0, 0 };   /* aim-ray records whose object is a character: a copy / ours / not replicated */
volatile LONG64 g_p112BoltFrames = 0, g_p112BoltTracked = 0, g_p112BoltUntracked = 0, g_p112BoltLeaked = 0;
volatile LONG64 g_p112BoltEnd[4] = { 0, 0, 0, 0 };  /* by end state 0 spent, 1 -, 2 struck a character, 3 stuck */
volatile LONG64 g_p112StruckCopy = 0, g_p112StruckMine = 0;
int g_p112AimHook = 0, g_p112BoltHook = 0;         /* 1 hooked, 0 AddHook failed, -1 no table row */

void P112Push(const P112Ev& v)
{
    if (g_p112Ready == 0) { InterlockedIncrement64(&g_p112Dropped); return; }
    for (;;)
    {
        const LONG pos = g_p112Enq;
        P112Slot* e = &g_p112Ring[pos & (kP112Ring - 1)];
        const LONG dif = e->seq - pos;
        if (dif == 0)
        {
            if (InterlockedCompareExchange(&g_p112Enq, pos + 1, pos) != pos) continue;
            e->ev = v;
            InterlockedExchange(&e->seq, pos + 1);
            return;
        }
        if (dif < 0) { InterlockedIncrement64(&g_p112Dropped); return; }
    }
}

// POD, __try (C2712: no std::string in any of these).
int P112V3(const void* p, float* o)
{
    o[0] = 0.0f; o[1] = 0.0f; o[2] = 0.0f;
    if (p == 0) return 0;
    __try
    {
        const float* f = (const float*)p;
        o[0] = f[0]; o[1] = f[1]; o[2] = f[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { o[0] = 0.0f; o[1] = 0.0f; o[2] = 0.0f; return 0; }
}
int P112CharPos(const void* c, float* o)
{
    if (!RsPlaus(c)) { o[0] = 0.0f; o[1] = 0.0f; o[2] = 0.0f; return 0; }
    return P112V3((const char*)c + kP112CharPos, o);
}
int P112ReadInt(const void* p)
{
    if (p == 0) return -1;
    __try { return *(const int*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
float P112ReadF(const void* p, float def)
{
    if (p == 0) return def;
    __try { return *(const float*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return def; }
}
const void* P112ReadPtr(const void* p)
{
    if (p == 0) return 0;
    __try { return *(const void* const*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int P112ReadRec(const void* holder, P112Rec* r)
{
    std::memset(r, 0, sizeof(*r));
    r->done = -1; r->f4 = -1; r->hit = -1; r->kind = -1; r->hand = -1; r->dist = -1.0f;
    if (!RsPlaus(holder)) return 0;
    __try
    {
        const char* rec = *(const char* const*)((const char*)holder + kP112HolderRec);
        if (!RsPlaus(rec)) return 0;
        r->done = (int)*(const unsigned char*)(rec + 0x7C);
        r->f4 = (int)*(const unsigned char*)(rec + 0x4);
        r->hit = (int)*(const unsigned char*)(rec + 0x7);
        r->pos[0] = *(const float*)(rec + 0x8); r->pos[1] = *(const float*)(rec + 0xC); r->pos[2] = *(const float*)(rec + 0x10);
        r->dist = *(const float*)(rec + 0x20);
        r->hand = *(const int*)(rec + 0x38);
        r->kind = (int)*(const unsigned short*)(rec + 0x50);
        r->obj = *(const void* const*)(rec + 0x58);
        r->origin[0] = *(const float*)(rec + 0x60); r->origin[1] = *(const float*)(rec + 0x64); r->origin[2] = *(const float*)(rec + 0x68);
        r->ok = 1;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { r->ok = 0; return 0; }
}
int P112ReadAimPre(const void* rc, float* timer, const void** me, const void** holder, int* loaded)
{
    *timer = -1.0f; *me = 0; *holder = 0; *loaded = -1;
    if (!RsPlaus(rc)) return 0;
    __try
    {
        *timer = *(const float*)((const char*)rc + kP112RcTimer);
        *me = *(const void* const*)((const char*)rc + kP112RcMe);
        const char* gun = *(const char* const*)((const char*)rc + kP112RcGun);
        if (RsPlaus(gun)) { *loaded = *(const int*)(gun + kRsGunLoaded); *holder = *(const void* const*)(gun + kP112GunQuery); }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Distance from point p to the segment a-b.
float P112SegPoint(const float* a, const float* b, const float* p)
{
    const float d0 = b[0] - a[0], d1 = b[1] - a[1], d2 = b[2] - a[2];
    const float ll = d0 * d0 + d1 * d1 + d2 * d2;
    float t = 0.0f;
    if (ll > 1e-6f) t = ((p[0] - a[0]) * d0 + (p[1] - a[1]) * d1 + (p[2] - a[2]) * d2) / ll;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float e0 = a[0] + t * d0 - p[0], e1 = a[1] + t * d1 - p[1], e2 = a[2] + t * d2 - p[2];
    return std::sqrt(e0 * e0 + e1 * e1 + e2 * e2);
}
// Distance from point q to the target's body axis (feet .. feet + kP112AxisH); the horizontal miss and the height above the feet.
float P112BodyDist(const float* q, const float* feet, float* horiz, float* height)
{
    const float dx = q[0] - feet[0], dz = q[2] - feet[2];
    const float h = q[1] - feet[1];
    const float hz = std::sqrt(dx * dx + dz * dz);
    const float ex = (h < 0.0f) ? -h : ((h > kP112AxisH) ? h - kP112AxisH : 0.0f);
    *horiz = hz;
    *height = h;
    return std::sqrt(hz * hz + ex * ex);
}

// ---- the aim check (AI thread) ----
typedef int* (*P112AimFn)(void*, int*, void*);
P112AimFn orig_p112Aim = 0;
unsigned int g_p112LastSu = 0, g_p112LastHu = 0;
int g_p112LastV = -9, g_p112LastWhy = -9;
long long g_p112SinceLog = 0;
const long long kP112AimEvery = 200;   /* a heartbeat line every 200 checks of one of OUR shooters */

// DETOUR, the AI thread: no allocation, no lock, no log.
int* detour_p112Aim(void* rc, int* out, void* target)
{
    float tPre = -1.0f;
    const void* me = 0;
    const void* holder = 0;
    int loaded = -1;
    P112ReadAimPre(rc, &tPre, &me, &holder, &loaded);
    P112Rec r;
    P112ReadRec(holder, &r);   /* BEFORE the original: a record already done is exactly what the engine is about to read */
    int* res = orig_p112Aim(rc, out, target);
    InterlockedIncrement64(&g_p112AimN);
    const int v = (res != 0) ? P112ReadInt(res) : -1;
    const float tPost = P112ReadF((const char*)rc + kP112RcTimer, -1.0f);
    const bool done = (r.ok != 0 && r.done == 1);
    unsigned int hu = 0;
    const int hm = (done && r.hit != 0 && r.obj != 0) ? RsOwn(r.obj, &hu) : -1;
    const bool charHit = done && r.hit != 0 && (hu != 0 || r.hand == 1);   /* hand type 1 = a character (Inferred) */
    int why = kP112WhyUnknown;
    if (target == 0) why = kP112WhyNoTarget;
    else if (v == 0) why = kP112WhyObjBlocks;
    else if (v == 1) why = !done ? kP112WhyOdd : (r.hit == 0 ? kP112WhyClear : (charHit ? kP112WhyCharEnemyOrSelf : kP112WhyNoObjOrFar));
    else if (v == 2)
    {
        if (tPost == tPre) why = kP112WhyNotReady;
        else if (tPost == kP112Timer0) why = kP112WhyNotLoaded;
        else if (tPost > 0.0f) why = kP112WhyTimer;
        else if (!done) why = kP112WhyRayPending;
        else if (r.f4 != 0) why = kP112WhyFlag4;
        else if (charHit) why = kP112WhyNonEnemyChar;
        else why = kP112WhyOdd;
    }
    if (v >= 0 && v <= 2) InterlockedIncrement64(&g_p112Verdict[v]);
    InterlockedIncrement64(&g_p112Why[why]);
    if (why == kP112WhyNonEnemyChar || why == kP112WhyCharEnemyOrSelf) InterlockedIncrement64(&g_p112RayChar[hm == 0 ? 0 : (hm == 1 ? 1 : 2)]);
    unsigned int su = 0, tu = 0;
    const int sm = RsOwn(me, &su);
    if (sm != 1) return res;   /* lines only for OUR shooters (a copy's AI is off; an NPC's checks are counted above) */
    const int tm = RsOwn(target, &tu);
    ++g_p112SinceLog;
    const bool transient = (why == kP112WhyNotReady || why == kP112WhyNotLoaded || why == kP112WhyTimer || why == kP112WhyRayPending);
    const bool change = !transient && (su != g_p112LastSu || v != g_p112LastV || why != g_p112LastWhy || hu != g_p112LastHu);
    if (v != 1 && !change && g_p112SinceLog < kP112AimEvery) return res;
    if (!transient) { g_p112LastSu = su; g_p112LastV = v; g_p112LastWhy = why; g_p112LastHu = hu; }
    g_p112SinceLog = 0;
    P112Ev e;
    std::memset(&e, 0, sizeof(e));
    e.kind = 1; e.verdict = v; e.why = why; e.loaded = loaded;
    e.su = su; e.tu = tu; e.hu = hu; e.sm = sm; e.tm = tm; e.hm = hm;
    e.tPre = tPre; e.tPost = tPost; e.r = r;
    P112CharPos(me, e.me);
    P112CharPos(target, e.tg);
    P112V3((const char*)rc + kP112RcAim, e.aim);
    e.tick = ::GetTickCount();
    P112Push(e);
    return res;
}

// ---- the shot (P104's GunClass::shoot detour) and each bolt's flight (main thread) ----
struct P112Shot { unsigned int su, tu; int tm; const void* tgt; float aim[3]; float me[3]; LONG shotNo; DWORD tick; };
P112Shot g_p112Shot;
volatile LONG g_p112ShotPending = 0;   /* the number of OUR last shot still waiting for its bolt; 0 none */
volatile LONG g_p112ShotNo = 0;
struct P112Bolt
{
    const void* h; unsigned int su, tu; int tm; const void* tgt; LONG shotNo; int frames;
    float aim[3], me[3], from[3], prev[3];
    float minAim, minBody, bodyHoriz, bodyHeight;
    DWORD t0;
};
const int kP112Bolts = 16;
P112Bolt g_p112Bolt[kP112Bolts];

// Called by detour_p104Shoot before the original (any thread): OUR shooter's aim point at the moment of the shot.
void P112OnShoot(::Character* me, void* target, const void* aim)
{
    unsigned int su = 0, tu = 0;
    if (RsOwn(me, &su) != 1) return;
    const int tm = RsOwn(target, &tu);
    const LONG n = InterlockedIncrement(&g_p112ShotNo);
    g_p112Shot.su = su; g_p112Shot.tu = tu; g_p112Shot.tm = tm; g_p112Shot.tgt = target;
    P112V3(aim, g_p112Shot.aim);
    P112CharPos(me, g_p112Shot.me);
    g_p112Shot.shotNo = n;
    g_p112Shot.tick = ::GetTickCount();
    InterlockedExchange(&g_p112ShotPending, n);
    P112Ev e;
    std::memset(&e, 0, sizeof(e));
    e.kind = 3; e.shotNo = (int)n; e.su = su; e.tu = tu; e.sm = 1; e.tm = tm;
    e.me[0] = g_p112Shot.me[0]; e.me[1] = g_p112Shot.me[1]; e.me[2] = g_p112Shot.me[2];
    e.aim[0] = g_p112Shot.aim[0]; e.aim[1] = g_p112Shot.aim[1]; e.aim[2] = g_p112Shot.aim[2];
    P112CharPos(target, e.tg);
    e.tick = g_p112Shot.tick;
    P112Push(e);
}

void P112Track(P112Bolt* s, const float* q0, const float* q1)
{
    const float da = P112SegPoint(q0, q1, s->aim);
    if (da < s->minAim) s->minAim = da;
    float feet[3];
    if (s->tgt == 0 || FindSpawnedUid(s->tgt) != s->tu || !P112CharPos(s->tgt, feet)) return;
    for (int i = 0; i <= 16; ++i)
    {
        const float t = (float)i / 16.0f;
        float q[3];
        q[0] = q0[0] + t * (q1[0] - q0[0]); q[1] = q0[1] + t * (q1[1] - q0[1]); q[2] = q0[2] + t * (q1[2] - q0[2]);
        float hz = 0.0f, ht = 0.0f;
        const float d = P112BodyDist(q, feet, &hz, &ht);
        if (d < s->minBody) { s->minBody = d; s->bodyHoriz = hz; s->bodyHeight = ht; }
    }
}

typedef void (*P112BoltFn)(void*);
P112BoltFn orig_p112Bolt = 0;

// DETOUR, the main thread (the projectile manager): no allocation, no lock, no log.
void detour_p112Bolt(void* h)
{
    const int pre = P112ReadInt((const char*)h + kP112BoltState);
    orig_p112Bolt(h);
    if (pre != 1) return;
    InterlockedIncrement64(&g_p112BoltFrames);
    P112Rec r;
    if (!P112ReadRec(P112ReadPtr((const char*)h + kP112BoltHolder), &r)) return;
    const DWORD now = ::GetTickCount();
    int k = -1;
    for (int i = 0; i < kP112Bolts; ++i) if (g_p112Bolt[i].h == h) { k = i; break; }
    if (k < 0)
    {
        const LONG pend = g_p112ShotPending;
        const float age = P112ReadF((const char*)h + kP112BoltAge, 99.0f);
        if (pend == 0 || age > 0.5f || (long)(now - g_p112Shot.tick) > 1500
            || InterlockedCompareExchange(&g_p112ShotPending, 0, pend) != pend)
        { InterlockedIncrement64(&g_p112BoltUntracked); return; }
        for (int i = 0; i < kP112Bolts; ++i)
        {
            if (g_p112Bolt[i].h != 0 && (long)(now - g_p112Bolt[i].t0) > 15000) { g_p112Bolt[i].h = 0; InterlockedIncrement64(&g_p112BoltLeaked); }
            if (g_p112Bolt[i].h == 0 && k < 0) k = i;
        }
        if (k < 0) { InterlockedIncrement64(&g_p112BoltUntracked); return; }
        P112Bolt* n = &g_p112Bolt[k];
        std::memset(n, 0, sizeof(*n));
        n->h = h; n->su = g_p112Shot.su; n->tu = g_p112Shot.tu; n->tm = g_p112Shot.tm; n->tgt = g_p112Shot.tgt; n->shotNo = pend;
        for (int j = 0; j < 3; ++j) { n->aim[j] = g_p112Shot.aim[j]; n->me[j] = g_p112Shot.me[j]; n->from[j] = r.origin[j]; n->prev[j] = r.origin[j]; }
        n->minAim = 1e9f; n->minBody = 1e9f; n->bodyHoriz = -1.0f; n->bodyHeight = -1.0f;
        n->t0 = now;
        InterlockedIncrement64(&g_p112BoltTracked);
    }
    P112Bolt* s = &g_p112Bolt[k];
    const int post = P112ReadInt((const char*)h + kP112BoltState);
    P112Track(s, s->prev, r.origin);
    if ((post == 2 || post == 3) && r.hit != 0) P112Track(s, r.origin, r.pos);
    s->prev[0] = r.origin[0]; s->prev[1] = r.origin[1]; s->prev[2] = r.origin[2];
    ++s->frames;
    if (post == 1) return;
    InterlockedIncrement64(&g_p112BoltEnd[post & 3]);
    P112Ev e;
    std::memset(&e, 0, sizeof(e));
    e.kind = 2; e.verdict = post; e.shotNo = (int)s->shotNo; e.frames = s->frames;
    e.su = s->su; e.sm = 1; e.tu = s->tu; e.tm = s->tm; e.r = r;
    const void* struck = (post == 2) ? P112ReadPtr((const char*)h + kP112BoltVictim) : r.obj;
    e.hm = (struck != 0) ? RsOwn(struck, &e.hu) : -1;
    if (post == 2 && e.hm == 0) InterlockedIncrement64(&g_p112StruckCopy);
    if (post == 2 && e.hm == 1) InterlockedIncrement64(&g_p112StruckMine);
    e.age = P112ReadF((const char*)h + kP112BoltAge, -1.0f);
    e.rangeLeft = P112ReadF((const char*)h + kP112BoltRange, -1.0f);
    e.minAim = s->minAim; e.minBody = s->minBody; e.bodyHoriz = s->bodyHoriz; e.bodyHeight = s->bodyHeight;
    for (int j = 0; j < 3; ++j) { e.aim[j] = s->aim[j]; e.me[j] = s->me[j]; e.from[j] = s->from[j]; }
    if (s->tgt != 0 && FindSpawnedUid(s->tgt) == s->tu) P112CharPos(s->tgt, e.tg);
    e.tick = now;
    P112Push(e);
    s->h = 0;
}
// PROBE-END: P112 (block 1 of 2)

// DETOUR, any thread: no allocation, no lock, no log.
void detour_p104Shoot(void* gun, ::Character* me, void* target, int stat, const void* aim)
{
    InterlockedIncrement64(&g_rsShots);
    if (g_rsShootThread == 0) InterlockedExchange(&g_rsShootThread, (LONG)::GetCurrentThreadId());
    unsigned int su = 0, tu = 0;
    const int sm = RsOwn(me, &su);
    const int tm = RsOwn(target, &tu);
    if (sm == 1) InterlockedIncrement64(&g_rsShotsMine);
    else if (sm == 0) InterlockedIncrement64(&g_rsShotsCopy);
    else InterlockedIncrement64(&g_rsShotsNone);
    int turretType = -1;
    if (me != 0)
    {
        const void* rc = *(void* const*)((const char*)me + kRsCharRanged);
        if (RsPlaus(rc)) turretType = *(const int*)((const char*)rc + kRsRcTurretType);
    }
    if (turretType != -1 && turretType != 0 && turretType != 0xB) InterlockedIncrement64(&g_rsShotsTurret);   /* 0xB: a null hand's type (Hand_ctor) */
    RsPush(1, su, sm, tu, tm, 0.0f, turretType, 0);
    P112OnShoot(me, target, aim);   /* PROBE P112: our shooter's aim point at this shot */
    orig_p104Shoot(gun, me, target, stat, aim);
}

// DETOUR, any thread: no allocation, no lock, no log.
bool detour_p104ShotYou(::Character* self, ::Character* attacker, void* harpoon, bool onPurpose)
{
    InterlockedIncrement64(&g_rsHits);
    if (g_rsHitThread == 0) InterlockedExchange(&g_rsHitThread, (LONG)::GetCurrentThreadId());
    unsigned int vu = 0, au = 0;
    const int vm = RsOwn(self, &vu);
    const int am = RsOwn(attacker, &au);
    if (vm == 1) InterlockedIncrement64(&g_rsHitVictimMine);
    else if (vm == 0) InterlockedIncrement64(&g_rsHitVictimCopy);
    else InterlockedIncrement64(&g_rsHitVictimNone);
    if (vm == 0 && am == 1) InterlockedIncrement64(&g_rsHitCross);
    if (am == 0) InterlockedIncrement64(&g_rsHitFromCopy);
    float dmg = -1.0f;
    float rec[4] = { -1.0f, -1.0f, -1.0f, -1.0f };   /* P104 fix: +0x40..+0x4C; rec[3] is +0x4C, iShotYou's damage */
    if (RsPlaus(harpoon))
    {
        std::memcpy(rec, (const char*)harpoon + kRsHarpoonRec, sizeof(rec));
        dmg = *(const float*)((const char*)harpoon + kRsHarpoonDmg);
    }
    /* P104 fix: a COPY's bolt makes no hit on this game - only the shooter's owner decides hits (its MSG_SHOT reaches the victim's
       owner). The engine's hit (wound, damage, reaction, sound) does not run; 1 is iShotYou's answer on every path
       (decomp_4398f0.txt:92), so the projectile manager goes on as after a hit. */
    if (am == 0)
    {
        InterlockedIncrement64(&g_rsHitCopyBlocked);
        RsPush(3, vu, vm, au, am, dmg, onPurpose ? 1 : 0, rec);
        return true;
    }
    RsPush(2, vu, vm, au, am, dmg, onPurpose ? 1 : 0, rec);   /* victim a COPY + shooter ours: the drain sends MSG_SHOT */
    return orig_p104ShotYou(self, attacker, harpoon, onPurpose);
}

const char* RsOwnWord(int m) { return m == 1 ? "mine" : (m == 0 ? "copy" : "none"); }

// MAIN THREAD, POD only (a __try function - no std::string here, C2712).
int RsReadRanged(::Character* c, int* hasRc, int* mode, int* hasGun, int* loaded, int* turretType)
{
    *hasRc = 0; *mode = -1; *hasGun = 0; *loaded = -1; *turretType = -1;
    __try
    {
        const char* rc = *(const char* const*)((const char*)c + kRsCharRanged);
        if (!RsPlaus(rc)) return 1;
        *hasRc = 1;
        *mode = (int)*(const unsigned char*)(rc + kRsRcCombatMode);
        *turretType = *(const int*)(rc + kRsRcTurretType);
        const char* gun = *(const char* const*)(rc + kRsRcGun);
        if (RsPlaus(gun)) { *hasGun = 1; *loaded = *(const int*)(gun + kRsGunLoaded); }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// T-310 TEST-ONLY (the rangedtest order and RECHECK lines). The fields reloadCheck 0x437940 / updateT 0x4388D0 use (decomp_437940.txt,
// decomp_4388d0.txt): RangedCombatClass state +0x0 (0 SHOOTING,
// 1 MOVING, 2 RELOADING - updateT sets it while the gun holds no bolt, and takes no aim check then; 3 WAITING, 4 THINKING),
// reloadTimerMax +0x74, reloadTimer +0x78, _isReloading +0x80; GunClass::ammoType +0x58 (null: reloadCheck loads without a bolt).
// A reload whose timer ran out with no bolt of that type in the inventory keeps _isReloading set and never loads (the no-item
// return skips the reset). MAIN THREAD, POD only, __try (C2712: no std::string here). 1 read, 0 no ranged object / fault.
const size_t kRtRcState       = 0x0;
const size_t kRtRcReloadMax   = 0x74;
const size_t kRtRcReloadTimer = 0x78;
const size_t kRtRcReloading   = 0x80;
const size_t kRtGunAmmoType   = 0x58;
const size_t kRtCharPos       = 0x48;   /* Character logical position (replicate.cpp P071ReadPos) */
int RtReadReload(::Character* c, int* state, int* reloading, float* timer, float* timerMax, int* ammoType)
{
    *state = -1; *reloading = -1; *timer = -1.0f; *timerMax = -1.0f; *ammoType = -1;
    __try
    {
        const char* rc = *(const char* const*)((const char*)c + kRsCharRanged);
        if (!RsPlaus(rc)) return 0;
        *state = *(const int*)(rc + kRtRcState);
        *reloading = (int)*(const unsigned char*)(rc + kRtRcReloading);
        *timer = *(const float*)(rc + kRtRcReloadTimer);
        *timerMax = *(const float*)(rc + kRtRcReloadMax);
        const char* gun = *(const char* const*)(rc + kRsRcGun);
        if (RsPlaus(gun)) *ammoType = (*(const void* const*)(gun + kRtGunAmmoType) != 0) ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Shooter -> target: horizontal distance and height difference, in world units (~10 a metre). 1 read, 0 no character / fault.
int RtDistance(::Character* a, ::Character* b, float* dist, float* dy)
{
    *dist = -1.0f; *dy = 0.0f;
    if (a == 0 || b == 0) return 0;
    __try
    {
        const float* pa = (const float*)((const char*)a + kRtCharPos);
        const float* pb = (const float*)((const char*)b + kRtCharPos);
        const float dx = pb[0] - pa[0], dz = pb[2] - pa[2];
        *dy = pb[1] - pa[1];
        *dist = sqrtf(dx * dx + dz * dz);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
const float kRtPointBlank = 30.0f;   /* 3 m: T689's target 0.8 m away was never fired at in 480 s; T686 at 12 m fired 60 times */
// P104s TEST-ONLY (`rangedtest skill`). The two CharStats fields GunClass::shoot 0x43A390 aims with (decomp_43a390.txt:77-83):
// cone deg = gun +0x10 * (1 - 0.005 * (getStat(crossbows) + getStat(perception))) * rand, clamped >= 0 (getStat 0x883BF0).
const size_t kRtCharStats     = 0x450;   /* Character::stats (game/gamelayout.h Character_stats) */
const size_t kRtStCrossbows   = 0x110;   /* CharStats::bows - getStat case 0x23 (STAT_CROSSBOWS) */
const size_t kRtStPerception  = 0x8C;    /* CharStats::perception ("precision shooting") - getStat case 0x18, plus the int below */
const size_t kRtStPercBonus   = 0x30;    /* CharStats::skillBonusPerception (int, equipment) */
const size_t kRtStDexterity   = 0x88;    /* CharStats::_dexterity - READ ONLY: not in the aim (reloadCheck 0x437940 XP only) */
const size_t kRtRcStat        = 0x30;    /* RangedCombatClass::currentStat - shoot's 4th argument (decomp_43ab40.txt:32) */
const size_t kRtGunRange      = 0x8;     /* GunClass::maxRange (in-range test 0x4364C0) */
const size_t kRtGunDeviation  = 0x10;    /* GunClass::accuracyDeviationBase */
const float  kRtAimSkillScale = 0.005f;  /* DAT_1416d35ac (1.0.65 exe bytes) */
// MAIN THREAD, POD only, __try (C2712: no std::string here). out[7]: crossbows, perception, dexterity, perception bonus,
// rc currentStat (-1 no ranged object), gun maxRange, gun deviation base (-1 no gun drawn). write != 0: crossbows and
// perception := v. 1 read (and written), -1 no stats object, 0 fault.
int RtSkillRW(::Character* c, float v, int write, float* out)
{
    for (int i = 0; i < 7; ++i) out[i] = -1.0f;
    __try
    {
        char* st = *(char* const*)((const char*)c + kRtCharStats);
        if (!RsPlaus(st)) return -1;
        if (write != 0) { *(float*)(st + kRtStCrossbows) = v; *(float*)(st + kRtStPerception) = v; }
        out[0] = *(const float*)(st + kRtStCrossbows);
        out[1] = *(const float*)(st + kRtStPerception);
        out[2] = *(const float*)(st + kRtStDexterity);
        out[3] = (float)*(const int*)(st + kRtStPercBonus);
        const char* rc = *(const char* const*)((const char*)c + kRsCharRanged);
        if (RsPlaus(rc))
        {
            out[4] = (float)*(const int*)(rc + kRtRcStat);
            const char* gun = *(const char* const*)(rc + kRsRcGun);
            if (RsPlaus(gun)) { out[5] = *(const float*)(gun + kRtGunRange); out[6] = *(const float*)(gun + kRtGunDeviation); }
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// P104 fix. POD, __try (C2712: no std::string here): n bytes of code, 0 when unreadable.
int P104ReadCode(uintptr_t at, unsigned char* out, int n)
{
    __try
    {
        for (int i = 0; i < n; ++i) out[i] = *(const unsigned char*)(at + i);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// The target of the rel32 CALL/JMP at `at` (its opcode already checked); 0 when unreadable.
uintptr_t P104Rel32Target(uintptr_t at)
{
    unsigned char b[5];
    if (!P104ReadCode(at, b, 5)) return 0;
    int rel = 0;
    std::memcpy(&rel, b + 1, 4);
    return at + 5 + (intptr_t)rel;
}

// 1 when `target`, or one of up to two E9 stubs it starts, lands on `want`.
int P104Reaches(uintptr_t target, uintptr_t want)
{
    for (int hop = 0; hop < 3 && target != 0; ++hop)
    {
        if (target == want) return 1;
        unsigned char op = 0;
        if (!P104ReadCode(target, &op, 1) || op != 0xE9) return 0;
        target = P104Rel32Target(target);
    }
    return 0;
}

// addWound's entry from the ApplyDamageHitRet row, verified (see kP104AddWoundRetOff). Returns g_p104AddWoundWhy's codes.
int P104DeriveAddWound(uintptr_t iShotYou, uintptr_t dmgRet, uintptr_t applyDamage, uintptr_t* addWound, int* callAt)
{
    *addWound = 0;
    *callAt = -1;
    if (iShotYou == 0 || dmgRet == 0 || applyDamage == 0) return 0;
    unsigned char op = 0;
    if (!P104ReadCode(dmgRet - 5, &op, 1)) return -3;
    if (op != 0xE8 || !P104Reaches(P104Rel32Target(dmgRet - 5), applyDamage)) return -1;
    const uintptr_t aw = dmgRet - (uintptr_t)kP104AddWoundRetOff;
    unsigned char code[0x200];
    if (!P104ReadCode(iShotYou, code, (int)sizeof(code))) return -3;
    for (int i = 0; i + 5 <= (int)sizeof(code); ++i)
    {
        if (code[i] != 0xE8) continue;
        int rel = 0;
        std::memcpy(&rel, code + i + 1, 4);
        const uintptr_t tg = iShotYou + (uintptr_t)i + 5 + (intptr_t)rel;
        if (tg == aw || (tg > iShotYou - 0x4000000 && tg < iShotYou + 0x4000000 && P104Reaches(tg, aw)))
        {
            *addWound = aw;
            *callAt = i;
            return 1;
        }
    }
    return -2;
}

// MAIN THREAD, K2 safe point. POD, __try (C2712): iShotYou's own addWound call; 0 on a fault.
int P104CallAddWound(::Character* victim, ::Character* attacker, float* damages6, int* armour2, int* side2, void** part)
{
    *part = 0;
    __try
    {
        *part = g_p104AddWound((char*)victim + kRsCharMedical, 0, kRsAttackRanged, damages6, armour2, attacker, side2, 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* T-311 (pvp1): the victim-side reactions Character::iShotYou runs around its addWound call (decomp_4398f0.txt:34-35 before it,
   38-45 and 83 after it), played on the victim's OWNER after the MSG_SHOT wound, with the shooter's COPY as the attacker:
     onPurpose:                           Character::rememberCharacter 0x673A10(victim, attacker, 4 = ST_TEMPORARY_ENEMY) - the
                                          victim's squad memory tags the attacker, which isEnemyOf reads (its step 2);
     onPurpose, combat +0x130 clear, the attacker's type (vtable +0x20) 1:
                                          AI::underRangedAttack 0x998E80(victim +0x650 = its AI, attacker, 1);
     always:                              the hit reaction 0x438E50(victim, 6, &Damages, addWound's result, attacker).
   The engine calls rememberCharacter BEFORE addWound; here it runs after it (the brief's order; addWound is not known to read
   squad memory - Inferred). coopshot::ShotReactions is the decision. MAIN THREAD, K2 safe point, POD, one __try each (C2712). */
typedef void (*P311RememberFn)(::Character*, ::Character*, int);
typedef void (*P311UnderRangedFn)(void*, ::Character*, int);
typedef void (*P311HitReactionFn)(::Character*, int, float*, void*, ::Character*);
typedef int  (*P311TypeFn)(::Character*);
typedef char (*P311IsEnemyFn)(::Character*, ::Character*, char);
unsigned long long kP311Remember = 0; static coop::AddrReg kP311Remember_reg("Character_remember", &kP311Remember);   /* Steam_1.0.65 0x673A10 */
unsigned long long kP311UnderRanged = 0; static coop::AddrReg kP311UnderRanged_reg("AI_beingShotAt", &kP311UnderRanged);   /* Steam_1.0.65 0x998E80 */
unsigned long long kP311HitReaction = 0; static coop::AddrReg kP311HitReaction_reg("Character_flinch", &kP311HitReaction);   /* Steam_1.0.65 0x438E50 */
const size_t kRsCharAI           = 0x650;   /* iShotYou's param_1[0xca] (the Character_getBrain row: MOV RAX,[RCX+0x650]) */
const size_t kRsCharBody         = 0x648;   /* the Character_getCombat row: MOV RAX,[RCX+0x648]; MOV RAX,[RAX+8] */
const size_t kRsBodyCombat       = 0x8;
const size_t kRsCombatFlag130    = 0x130;   /* iShotYou / 0x438E50: set -> no underRangedAttack (0x438E50 takes its 0x664A00 branch) */
const size_t kRsCombatTarget     = 0x290;   /* CombatClass currentTarget (kTargetOff below) */
const size_t kRsVtType           = 0x20;    /* iShotYou's attacker test: vtable +0x20 == 1, a character */
const size_t kRsVtIsEnemy        = 0x3E8;   /* Character::isEnemyOf(other, factorInDisguises) - the IsEnemy row */
const int    kRsStTemporaryEnemy = 4;

// *flag130: -1 unread, else 0 / 1. *type: the attacker's vtable +0x20 answer, -1 unread. 1 read, 0 a fault.
int P311ReadGate(::Character* v, ::Character* a, int* flag130, int* type)
{
    *flag130 = -1; *type = -1;
    __try
    {
        const char* body = *(const char* const*)((const char*)v + kRsCharBody);
        if (RsPlaus(body))
        {
            const char* cc = *(const char* const*)(body + kRsBodyCombat);
            if (RsPlaus(cc)) *flag130 = (*(const unsigned char*)(cc + kRsCombatFlag130) != 0) ? 1 : 0;
        }
        const P311TypeFn tf = *(const P311TypeFn*)(*(const char* const*)a + kRsVtType);
        *type = tf(a);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int P311CallRemember(P311RememberFn f, ::Character* v, ::Character* a)
{
    __try { f(v, a, kRsStTemporaryEnemy); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// 1 ran, 0 a fault, -1 the victim or the shooter's copy has no AI (T-311 fold 1)
int P311CallUnderRanged(P311UnderRangedFn f, ::Character* v, ::Character* a)
{
    __try
    {
        void* ai = *(void* const*)((const char*)v + kRsCharAI);
        if (!RsPlaus(ai)) return -1;
        /* T-311 fold 1: underRangedAttack also reads the ATTACKER's AI (decomp_998e80.txt:21 - copy +kRsCharAI, then +0x18, then
           +0xAC): a copy with no AI object is booked as noAI, not as a caught fault. */
        const char* aai = *(const char* const*)((const char*)a + kRsCharAI);
        if (!RsPlaus(aai) || !RsPlaus(*(const void* const*)(aai + 0x18))) return -1;
        f(ai, a, 1);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int P311CallHitReaction(P311HitReactionFn f, ::Character* v, float* damages6, void* part, ::Character* a)
{
    __try { f(v, kRsAttackRanged, damages6, part, a); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// The victim's own isEnemyOf(copy, true) through its vtable: 1 / 0, -1 a fault.
int P311IsEnemy(::Character* v, ::Character* a)
{
    __try
    {
        const P311IsEnemyFn f = *(const P311IsEnemyFn*)(*(const char* const*)v + kRsVtIsEnemy);
        return f(v, a, 1) != 0 ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// The victim's CombatClass currentTarget. 1 read, 0 no combat class / a fault.
int P311ReadTarget(::Character* v, const void** tgt)
{
    *tgt = 0;
    __try
    {
        const char* body = *(const char* const*)((const char*)v + kRsCharBody);
        if (!RsPlaus(body)) return 0;
        const char* cc = *(const char* const*)(body + kRsBodyCombat);
        if (!RsPlaus(cc)) return 0;
        *tgt = *(const void* const*)(cc + kRsCombatTarget);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
struct ShotWatch { unsigned int victim; unsigned int shooter; DWORD due; int stage; };
const int kShotWatchMax = 4;
ShotWatch g_shotWatch[kShotWatchMax];   /* zero-initialised: stage 0 = free */
} // namespace (P104)

std::string RangedTestGive(unsigned int uid, const std::string& sid, int count);   /* items.cpp - P104 */
std::string RangedTestGiveAmmo(unsigned int uid, const std::string& sid, int rounds);   /* items.cpp - P104f1 */

// P104 fix, MAIN THREAD (the drain): our bolt struck the other game's character's copy - tell its owner.
static void ShotSendOne(unsigned int victim, unsigned int shooter, const float* rec, bool onPurpose)
{
    coopshot::ShotMsg m;
    m.victimUid = victim;
    m.shooterUid = shooter;
    for (int r = 0; r < 4; ++r) m.rec[r] = rec[r];
    m.onPurpose = onPurpose;
    const bool ok = coopshot::ShotMsgOk(m);
    const bool sent = ok && net::SendShot(m);
    if (!ok) ++g_shotSendBad;
    else if (sent) ++g_shotSent;
    else ++g_shotSendFail;
    if (g_shotOutLogged < kRsLogLimit)
    {
        ++g_shotOutLogged;
        DebugLog(std::string(sent ? "[P104] SHOT-SENT #" + N3(g_shotSent) : "[P104] SHOT-SEND FAILED")
                 + " victim uid=" + N3(victim) + " (copy) shooter uid=" + N3(shooter) + " (mine) dmg=" + F1(rec[3])
                 + " rec=" + F1(rec[0]) + "," + F1(rec[1]) + "," + F1(rec[2]) + " onPurpose=" + N3(onPurpose ? 1 : 0)
                 + (sent ? " -> MSG_SHOT to the victim's owner"
                         : (ok ? " - the link is down: this hit is LOST (counted shotSendFail)"
                               : " - not sendable (bad uid or damage): this hit is LOST (counted shotSendBad)")));
    }
}

// MAIN THREAD (CombatTick): the shot / ranged-hit lines, bounded per kind.
void RangedEventsDrain()
{
    for (;;)
    {
        RsEvent* e = &g_rsRing[g_rsDeq & (kRsRing - 1)];
        if (e->seq != g_rsDeq + 1) return;
        const int kind = e->kind, whoMine = e->whoMine, otherMine = e->otherMine, flag = e->flag;
        const unsigned int who = e->who, other = e->other;
        const float dmg = e->dmg;
        float rec[4];
        for (int r = 0; r < 4; ++r) rec[r] = e->rec[r];
        InterlockedExchange(&e->seq, g_rsDeq + kRsRing);
        ++g_rsDeq;
        if (kind == 2 && whoMine == 0 && otherMine == 1) ShotSendOne(who, other, rec, flag != 0);   /* P104 fix: every such hit, not only logged ones */
        if (kind == 1 && g_rsShotLogged < kRsLogLimit)
        {
            ++g_rsShotLogged;
            DebugLog("[P104] SHOT #" + N3(g_rsShotLogged) + " shooter uid=" + N3(who) + " (" + RsOwnWord(whoMine) + ")"
                     + " target uid=" + N3(other) + " (" + RsOwnWord(otherMine) + ") turretHandType=" + N3(flag)
                     + " - the projectile exists on THIS game only");
        }
        else if (kind == 2 && g_rsHitLogged < kRsLogLimit)
        {
            ++g_rsHitLogged;
            const char* what = (whoMine == 1) ? "applied HERE (ours); its health reaches the other game in our STATE"
                             : (whoMine == 0) ? (otherMine == 1 ? "a COPY: the engine's damage is SKIPPED here (C2-b); MSG_SHOT carries the hit to its owner (P104 fix)"
                                                                : "a COPY: the engine's damage is SKIPPED here (C2-b); the shooter is not ours, its own game decides")
                             : "not replicated: this game only";
            DebugLog("[P104] RHIT #" + N3(g_rsHitLogged) + " victim uid=" + N3(who) + " (" + RsOwnWord(whoMine) + ")"
                     + " shooter uid=" + N3(other) + " (" + RsOwnWord(otherMine) + ") dmg=" + F1(dmg)
                     + " onPurpose=" + N3(flag) + " -> " + what);
        }
        else if (kind == 3 && g_rsBlockLogged < kRsLogLimit)
        {
            ++g_rsBlockLogged;
            DebugLog("[P104] RHIT-BLOCKED #" + N3(g_rsBlockLogged) + " victim uid=" + N3(who) + " (" + RsOwnWord(whoMine) + ")"
                     + " shooter uid=" + N3(other) + " (copy) dmg=" + F1(dmg) + " onPurpose=" + N3(flag)
                     + " - a COPY's bolt: NO hit on this game (only the shooter's owner decides hits)");
        }
    }
}

std::string RangedCountersLine()
{
    return "[P104] REPORT shots=" + N3(g_rsShots) + " shotsMine=" + N3(g_rsShotsMine) + " shotsCopy=" + N3(g_rsShotsCopy)
         + " shotsNone=" + N3(g_rsShotsNone) + " shotsTurret=" + N3(g_rsShotsTurret)
         + " rhits=" + N3(g_rsHits) + " rhitVictimMine=" + N3(g_rsHitVictimMine) + " rhitVictimCopy=" + N3(g_rsHitVictimCopy)
         + " rhitVictimNone=" + N3(g_rsHitVictimNone) + " rhitCross=" + N3(g_rsHitCross)
         + " rhitCopyBlocked=" + N3(g_rsHitCopyBlocked) + " shotSent=" + N3(g_shotSent) + " shotSendFail=" + N3(g_shotSendFail)
         + " shotSendBad=" + N3(g_shotSendBad) + " shotIn=" + N3(g_shotIn) + " shotApplied=" + N3(g_shotApplied)
         + " shotRefused[notMine,shooter,malformed]=" + N3(g_shotRefusedNotMine) + "," + N3(g_shotRefusedShooter) + "," + N3(g_shotMalformed)
         + " shotNot[victim,shooter,orientable,loading,addWoundOff,fault,qDrop]=" + N3(g_shotNoVictim) + "," + N3(g_shotNoShooter)
         + "," + N3(g_shotNotOrientable) + "," + N3(g_shotBlockedLoad) + "," + N3(g_shotNoAddWound) + "," + N3(g_shotFault) + "," + N3(g_shotQDrop)
         + " addWound=" + (g_p104AddWound != 0 ? std::string("ON") : "OFF(why=" + N3(g_p104AddWoundWhy) + ")")
         + " rhitFromCopy=" + N3(g_rsHitFromCopy) + " dropped=" + N3(g_rsDropped)
         + " shootThread=" + N3(g_rsShootThread) + " hitThread=" + N3(g_rsHitThread)
         + " hooks=" + N3(g_rsHooks) + "/2 leverRuns=" + N3(g_rsLeverRuns)
         + " react[runs,remember,underRanged,hitReaction]=" + N3(g_shotReactRuns) + "," + N3(g_shotReactRemember) + ","
         + N3(g_shotReactUnderRanged) + "," + N3(g_shotReactHitReaction)
         + " reactSkip[noCopy,noRow,notOnPurpose,flag130,flagUnread,notChar,noAI,noPart]=" + N3(g_shotReactNoCopy) + "," + N3(g_shotReactNoRow) + ","
         + N3(g_shotReactNotOnPurpose) + "," + N3(g_shotReactFlag130) + "," + N3(g_shotReactFlagUnread) + "," + N3(g_shotReactNotChar)
         + "," + N3(g_shotReactNoAI) + "," + N3(g_shotReactNoPart)
         + " reactFault=" + N3(g_shotReactFault) + " reactEnemy=" + N3(g_shotReactEnemy)
         + " watch[lines,turned,gone,busy]=" + N3(g_shotWatchLines) + "," + N3(g_shotWatchTurned) + "," + N3(g_shotWatchGone)
         + "," + N3(g_shotWatchBusy);
}

// PROBE-START: P112 (T-310, block 2 of 2) - MAIN THREAD: the lines, the REPORT, `rangedtest watch`, the install.
long long g_p112AimLogged = 0, g_p112BoltLogged = 0, g_p112ShotLogged = 0, g_p112PosLogged = 0;
const long long kP112AimCap = 300, kP112BoltCap = 150, kP112ShotCap = 100;

static std::string P112V(const float* v) { return F1(v[0]) + "," + F1(v[1]) + "," + F1(v[2]); }
static float P112Horiz(const float* a, const float* b)
{
    const float dx = b[0] - a[0], dz = b[2] - a[2];
    return std::sqrt(dx * dx + dz * dz);
}
static std::string P112Who(unsigned int u, int m, const void* p)
{
    if (u != 0) return "uid=" + N3(u) + " (" + RsOwnWord(m) + ")";
    return p != 0 ? "obj=" + Ptr(p) + " (not replicated)" : std::string("none");
}
static std::string P112RecWords(const P112Rec& r)
{
    if (r.ok == 0) return "ray=unread";
    return "ray[done,flag4,hit,kind,hand]=" + N3(r.done) + "," + N3(r.f4) + "," + N3(r.hit) + "," + N3(r.kind) + "," + N3(r.hand)
           + (r.kind == 2 ? std::string(" (kind 2 = terrain)") : std::string()) + " hitAt=" + P112V(r.pos) + " hitDist=" + F1(r.dist);
}
static std::string P112Offset(const float* a, const float* feet)
{
    float d[3];
    d[0] = a[0] - feet[0]; d[1] = a[1] - feet[1]; d[2] = a[2] - feet[2];
    return P112V(d);
}

static void P112Drain()
{
    for (;;)
    {
        P112Slot* sl = &g_p112Ring[g_p112Deq & (kP112Ring - 1)];
        if (sl->seq != g_p112Deq + 1) return;
        const P112Ev e = sl->ev;
        InterlockedExchange(&sl->seq, g_p112Deq + kP112Ring);
        ++g_p112Deq;
        if (e.kind == 1 && g_p112AimLogged < kP112AimCap)
        {
            ++g_p112AimLogged;
            const char* vw = (e.verdict == 1) ? "FIRE" : (e.verdict == 0) ? "BLOCKED" : (e.verdict == 2) ? "WAIT" : "?";
            const int w = (e.why >= 0 && e.why < kP112WhyCount) ? e.why : 0;
            DebugLog("[P112] AIM #" + N3(g_p112AimLogged) + " " + vw + " why=" + kP112WhyWord[w]
                     + " shooter " + P112Who(e.su, e.sm, 0) + " at " + P112V(e.me)
                     + " target " + P112Who(e.tu, e.tm, 0) + " feet at " + P112V(e.tg)
                     + " horiz=" + F1(P112Horiz(e.me, e.tg)) + " dy=" + F1(e.tg[1] - e.me[1])
                     + " aimPt=" + P112V(e.aim) + " aimPt-targetFeet=" + P112Offset(e.aim, e.tg)
                     + " timer " + F1(e.tPre) + "->" + F1(e.tPost) + " loaded=" + N3(e.loaded)
                     + " " + P112RecWords(e.r) + " rayObject=" + P112Who(e.hu, e.hm, e.r.obj)
                     + " tick=" + N3((long long)e.tick));
        }
        else if (e.kind == 3 && g_p112ShotLogged < kP112ShotCap)
        {
            ++g_p112ShotLogged;
            DebugLog("[P112] SHOT-AIM shot#" + N3(e.shotNo) + " shooter " + P112Who(e.su, e.sm, 0) + " at " + P112V(e.me)
                     + " target " + P112Who(e.tu, e.tm, 0) + " feet at " + P112V(e.tg)
                     + " horiz=" + F1(P112Horiz(e.me, e.tg)) + " dy=" + F1(e.tg[1] - e.me[1])
                     + " aimPt=" + P112V(e.aim) + " aimPt-targetFeet=" + P112Offset(e.aim, e.tg)
                     + " (0x436970: the target's spine bone + a velocity lead) tick=" + N3((long long)e.tick));
        }
        else if (e.kind == 2 && g_p112BoltLogged < kP112BoltCap)
        {
            ++g_p112BoltLogged;
            const char* ew = (e.verdict == 2) ? "STRUCK-A-CHARACTER" : (e.verdict == 3) ? "STUCK(terrain/building)"
                           : (e.verdict == 0) ? (e.rangeLeft <= 0.0f ? "SPENT(range out)" : "SPENT(glancing terrain)") : "?";
            DebugLog("[P112] BOLT-END shot#" + N3(e.shotNo) + " " + ew + " struck " + P112Who(e.hu, e.hm, (e.verdict == 2 || e.verdict == 3) ? e.r.obj : 0)
                     + " " + P112RecWords(e.r) + " rangeLeft=" + F1(e.rangeLeft) + " age=" + F1(e.age) + " frames=" + N3(e.frames)
                     + " missToAimPt=" + (e.minAim > 1e8f ? std::string("-") : F1(e.minAim))
                     + " missToBody=" + (e.minBody > 1e8f ? std::string("unread") : F1(e.minBody)
                                         + " (horiz=" + F1(e.bodyHoriz) + " heightAboveFeet=" + F1(e.bodyHeight) + ")")
                     + " shooter " + P112Who(e.su, e.sm, 0) + " at " + P112V(e.me) + " target " + P112Who(e.tu, e.tm, 0)
                     + " feetNow=" + P112V(e.tg) + " aimPt=" + P112V(e.aim) + " boltFrom=" + P112V(e.from)
                     + " tick=" + N3((long long)e.tick));
        }
    }
}

static std::string P112ReportLine()
{
    std::string why;
    for (int i = 0; i < kP112WhyCount; ++i)
    {
        if (g_p112Why[i] == 0) continue;
        why += (why.empty() ? "" : ",") + std::string(kP112WhyWord[i]) + ":" + N3(g_p112Why[i]);
    }
    return "[P112] REPORT hooks[aimCheck,bolt]=" + N3(g_p112AimHook) + "," + N3(g_p112BoltHook) + " (1 on, 0 failed, -1 no table row)"
         + " aimChecks=" + N3(g_p112AimN) + " verdict[blocked,fire,wait]=" + N3(g_p112Verdict[0]) + "," + N3(g_p112Verdict[1]) + "," + N3(g_p112Verdict[2])
         + " why=[" + why + "] rayOnCharacter[copy,mine,notReplicated]=" + N3(g_p112RayChar[0]) + "," + N3(g_p112RayChar[1]) + "," + N3(g_p112RayChar[2])
         + " bolts[frames,tracked,untracked,leaked]=" + N3(g_p112BoltFrames) + "," + N3(g_p112BoltTracked) + "," + N3(g_p112BoltUntracked) + "," + N3(g_p112BoltLeaked)
         + " boltEnd[spent,struck,stuck]=" + N3(g_p112BoltEnd[0]) + "," + N3(g_p112BoltEnd[2]) + "," + N3(g_p112BoltEnd[3])
         + " struck[copy,mine]=" + N3(g_p112StruckCopy) + "," + N3(g_p112StruckMine)
         + " lines[aim,shot,bolt,pos]=" + N3(g_p112AimLogged) + "," + N3(g_p112ShotLogged) + "," + N3(g_p112BoltLogged) + "," + N3(g_p112PosLogged)
         + " dropped=" + N3(g_p112Dropped);
}

static unsigned int g_p112WatchUid[4] = { 0, 0, 0, 0 };
static DWORD g_p112WatchUntil = 0, g_p112WatchNext = 0;
static int g_p112WatchOn = 0;
static int P112Ragdoll(::Character* c)
{
    __try { return c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int P112AnimPos(const void* c, float* o)
{
    o[0] = 0.0f; o[1] = 0.0f; o[2] = 0.0f;
    const void* anim = P112ReadPtr((const char*)c + kP112CharAnim);
    if (!RsPlaus(anim)) return 0;
    return P112V3((const char*)anim + kP112AnimPos, o);
}

// MAIN THREAD (CombatTick): one POS line per watched uid every 2 s. tick = GetTickCount: both games of a bench run share it.
static void P112WatchTick()
{
    if (g_p112WatchOn == 0) return;
    const DWORD now = ::GetTickCount();
    if ((long)(now - g_p112WatchUntil) >= 0) { g_p112WatchOn = 0; DebugLog("[P112] watch ended tick=" + N3((long long)now)); return; }
    if ((long)(now - g_p112WatchNext) < 0) return;
    g_p112WatchNext = now + 2000;
    for (int i = 0; i < 4; ++i)
    {
        const unsigned int u = g_p112WatchUid[i];
        if (u == 0) continue;
        ++g_p112PosLogged;
        ::Character* c = FindSpawned(u);
        if (c == 0) { DebugLog("[P112] POS tick=" + N3((long long)now) + " uid=" + N3(u) + " - no character on this game"); continue; }
        float p[3], a[3];
        P112CharPos(c, p);
        const int ha = P112AnimPos(c, a);
        void* an = 0; void* ap = 0; void* en = 0;
        int reason = kHitBodyAttached, gate = 0;
        const int att = AppearanceBodyAttached(c, &an, &ap, &en, &reason, &gate);
        int hasRc = 0, mode = -1, hasGun = 0, loaded = -1, tt = -1;
        RsReadRanged(c, &hasRc, &mode, &hasGun, &loaded, &tt);
        DebugLog("[P112] POS tick=" + N3((long long)now) + " uid=" + N3(u) + " mine=" + N3(net::IsUidMine(u) ? 1 : 0)
                 + " pos=" + P112V(p) + " anim=" + (ha ? P112V(a) : std::string("-"))
                 + " ragdoll=" + N3(P112Ragdoll(c)) + " bodyAttached=" + N3(att)
                 + " body=" + ((reason >= 0 && reason < kHitBodySkipCount) ? std::string(kHitBodyReasonWord[reason]) : N3(reason))
                 + " gate=" + N3(gate) + " rcCombatMode=" + N3(mode) + " gun=" + N3(hasGun) + " loaded=" + N3(loaded));
    }
}

// rangedtest watch <seconds 1..3600> <uid> [<uid> ...] - up to 4 uids, on the game it is sent to.
static std::string P112WatchLever(std::istringstream& is)
{
    const std::string usage = "error rangedtest watch: usage rangedtest watch <seconds 1..3600> <uid> [<uid> ...] (up to 4)";
    long long secs = 0;
    if (!(is >> secs) || secs <= 0 || secs > 3600) return usage;
    unsigned int u[4] = { 0, 0, 0, 0 };
    int n = 0;
    unsigned int x = 0;
    while (n < 4 && (is >> x)) { if (x != 0) u[n++] = x; }
    if (n == 0) return usage;
    std::string list;
    for (int i = 0; i < 4; ++i)
    {
        g_p112WatchUid[i] = u[i];
        if (u[i] != 0) list += (list.empty() ? "" : ",") + N3(u[i]);
    }
    const DWORD now = ::GetTickCount();
    g_p112WatchUntil = now + (DWORD)(secs * 1000);
    g_p112WatchNext = now;
    g_p112WatchOn = 1;
    const std::string l = "[P112] watch (PROBE) armed for " + N3(secs) + " s: a POS line every 2 s for uid " + list
                        + " (tick = GetTickCount ms, the same clock on both games of one machine) tick=" + N3((long long)now);
    DebugLog(l);
    return "ok rangedtest " + l;
}

static void P112Install()
{
    for (LONG i = 0; i < kP112Ring; ++i) g_p112Ring[i].seq = i;
    InterlockedExchange(&g_p112Ready, 1);
    const intptr_t ac = (kP112AimCheck != 0) ? (intptr_t)coop::AddrAbs(kP112AimCheck) : 0;
    const intptr_t bu = (kP112BoltUpdate != 0) ? (intptr_t)coop::AddrAbs(kP112BoltUpdate) : 0;
    g_p112AimHook = (ac == 0) ? -1
                  : (coop::AddHook((void*)ac, (void*)&detour_p112Aim, (void**)&orig_p112Aim) == coop::SUCCESS ? 1 : 0);
    g_p112BoltHook = (bu == 0) ? -1
                   : (coop::AddHook((void*)bu, (void*)&detour_p112Bolt, (void**)&orig_p112Bolt) == coop::SUCCESS ? 1 : 0);
    DebugLog(std::string("[P112] probe (T-310, read only): aim check (RangedCombat_aimTest) AddHook ")
             + (g_p112AimHook == 1 ? "SUCCESS" : (g_p112AimHook == 0 ? "FAILED" : "NO ROW"))
             + ", bolt update (Harpoon_update) AddHook "
             + (g_p112BoltHook == 1 ? "SUCCESS" : (g_p112BoltHook == 0 ? "FAILED" : "NO ROW"))
             + "; lines [P112] AIM / SHOT-AIM / BOLT-END / POS (rangedtest watch) / REPORT");
}
// PROBE-END: P112 (block 2 of 2)

void RangedReport()
{
    RangedEventsDrain();
    DebugLog(RangedCountersLine());
    P112Drain(); DebugLog(P112ReportLine());   /* PROBE P112 */
}

// P104 fix, MAIN THREAD (session.cpp OnShot). See combat.h.
void ShotOnNet(unsigned int victimUid, unsigned int shooterUid, const float* rec4, bool onPurpose, int refusal)
{
    ++g_shotIn;
    if (refusal != 0 || rec4 == 0)
    {
        const char* why = (refusal == 1) ? "the victim is not driven by this game"
                        : (refusal == 2) ? "the sender does not drive the shooter"
                        : "it did not decode";
        if (refusal == 1) ++g_shotRefusedNotMine;
        else if (refusal == 2) ++g_shotRefusedShooter;
        else ++g_shotMalformed;
        if (g_shotInLogged < kRsLogLimit)
        {
            ++g_shotInLogged;
            DebugLog("[P104] SHOT-IN REFUSED victim uid=" + N3(victimUid) + " shooter uid=" + N3(shooterUid) + " - " + why);
        }
        return;
    }
    if (g_shotInQ.size() >= kShotInMax)
    {
        ++g_shotQDrop;
        if (g_shotInLogged < kRsLogLimit)
        {
            ++g_shotInLogged;
            ErrorLog("[P104] SHOT-IN DROPPED victim uid=" + N3(victimUid) + " - " + N3((long long)kShotInMax)
                     + " already wait for the K2 safe point (is it installed?)");
        }
        return;
    }
    ShotIn s;
    s.victim = victimUid;
    s.shooter = shooterUid;
    for (int r = 0; r < 4; ++r) s.rec[r] = rec4[r];
    s.onPurpose = onPurpose;
    g_shotInQ.push_back(s);
}

// T-311, MAIN THREAD (K2 safe point, inside ShotSafePointDrain): the wound was applied - play iShotYou's victim-side reactions
// (see P311ReadGate above) with the shooter's copy `a` as the attacker, then arm the +3 s / +8 s WATCH lines.
static void ShotReactOne(::Character* v, ::Character* a, const ShotIn& s, float* damages6, void* part)
{
    ++g_shotReactRuns;
    if (kP311Remember == 0 || kP311UnderRanged == 0 || kP311HitReaction == 0)
    {
        ++g_shotReactNoRow;
        if (g_shotReactLogged < kRsLogLimit)
        {
            ++g_shotReactLogged;
            ErrorLog("[P104] SHOT-IN REACT NOT PLAYED victim uid=" + N3(s.victim) + " - a row is unbound (rememberCharacter="
                     + N3(kP311Remember != 0 ? 1 : 0) + " underRangedAttack=" + N3(kP311UnderRanged != 0 ? 1 : 0)
                     + " hitReaction=" + N3(kP311HitReaction != 0 ? 1 : 0) + ")");
        }
        return;
    }
    int flag130 = -1, type = -1;
    const int gateRead = P311ReadGate(v, a, &flag130, &type);
    const int want = coopshot::ShotReactions(s.onPurpose, true, flag130, type);
    /* T-311 fold 1 (the T733 proof): whom the victim targeted just BEFORE the reactions, read as the WATCH lines read it - a
       later WATCH 'TURNED' proves the reactions only when this was not already the shooter's copy. */
    const void* tgtBefore = 0;
    const int tbRead = P311ReadTarget(v, &tgtBefore);
    std::string tb = "NONE";
    if (tgtBefore == (const void*)a) tb = "COPY (the shooter's copy, uid=" + N3(s.shooter) + ")";
    else if (tgtBefore != 0) { const unsigned int tu = UidOf((::Character*)tgtBefore); tb = "OTHER (" + (tu != 0 ? "uid=" + N3(tu) : Ptr(tgtBefore)) + ")"; }
    int rem = -2, ur = -2, hr = -2;   /* -2 not run (the engine would not run it either), 1 ran, 0 a fault, -1 no AI / no part */
    if (want & coopshot::kShotReactRemember)
    {
        rem = P311CallRemember((P311RememberFn)coop::AddrAbs(kP311Remember), v, a);
        if (rem == 1) ++g_shotReactRemember; else ++g_shotReactFault;
    }
    if (want & coopshot::kShotReactUnderRanged)
    {
        ur = P311CallUnderRanged((P311UnderRangedFn)coop::AddrAbs(kP311UnderRanged), v, a);
        if (ur == 1) ++g_shotReactUnderRanged; else if (ur == 0) ++g_shotReactFault; else ++g_shotReactNoAI;
    }
    else
    {
        /* T-311 fold 1: each underRangedAttack skip in its own counter (coopshot::ShotUnderRangedSkip - ShotReactions' conditions) */
        const int urSkip = coopshot::ShotUnderRangedSkip(s.onPurpose, flag130, type);
        if (urSkip == coopshot::kShotUrSkipFlag130) ++g_shotReactFlag130;
        else if (urSkip == coopshot::kShotUrSkipFlagUnread) ++g_shotReactFlagUnread;
        else if (urSkip == coopshot::kShotUrSkipNotChar) ++g_shotReactNotChar;
    }
    if (!s.onPurpose) ++g_shotReactNotOnPurpose;
    if (want & coopshot::kShotReactHitReaction)
    {
        if (part == 0) { hr = -1; ++g_shotReactNoPart; }
        else
        {
            hr = P311CallHitReaction((P311HitReactionFn)coop::AddrAbs(kP311HitReaction), v, damages6, part, a);
            if (hr == 1) ++g_shotReactHitReaction; else ++g_shotReactFault;
        }
    }
    const int enemy = P311IsEnemy(v, a);
    if (enemy == 1) ++g_shotReactEnemy;
    if (g_shotReactLogged < kRsLogLimit)
    {
        ++g_shotReactLogged;
        DebugLog("[P104] SHOT-IN REACT victim uid=" + N3(s.victim) + " (mine) attacker uid=" + N3(s.shooter) + " (the shooter's copy)"
                 + " onPurpose=" + N3(s.onPurpose ? 1 : 0) + " gateRead=" + N3(gateRead) + " combat+0x130=" + N3(flag130)
                 + " attackerType=" + N3(type) + " targetBefore=" + tb + " targetBeforeRead=" + N3(tbRead)
                 + " -> rememberCharacter(ST_TEMPORARY_ENEMY)=" + N3(rem)
                 + " underRangedAttack=" + N3(ur) + " hitReaction=" + N3(hr)
                 + " (1 ran, -2 the engine would not run it, 0 fault caught, -1 no AI / no part) victim treatsAsEnemy(copy)=" + N3(enemy)
                 + " - the victim's reactions ran as iShotYou's; WATCH lines +3 s / +8 s");
    }
    int freeAt = -1;
    for (int i = 0; i < kShotWatchMax; ++i)
    {
        if (g_shotWatch[i].stage != 0 && g_shotWatch[i].victim == s.victim) return;   /* already watched */
        if (g_shotWatch[i].stage == 0 && freeAt < 0) freeAt = i;
    }
    if (freeAt < 0) { ++g_shotWatchBusy; return; }
    g_shotWatch[freeAt].victim = s.victim;
    g_shotWatch[freeAt].shooter = s.shooter;
    g_shotWatch[freeAt].due = ::GetTickCount() + 3000;
    g_shotWatch[freeAt].stage = 1;
}

// T-311, MAIN THREAD (K2 safe point, ShotSafePointDrain, worker paused - T-311 fold 1): +3 s and +8 s after a reacted SHOT-IN - whom the victim now targets, and whether it counts
// the shooter's copy as an enemy. The oracle: target=COPY (the victim turned on the shooter's copy).
static void ShotReactWatchTick()
{
    for (int i = 0; i < kShotWatchMax; ++i)
    {
        ShotWatch& w = g_shotWatch[i];
        if (w.stage == 0 || (long)(::GetTickCount() - w.due) < 0) continue;
        const std::string when = (w.stage == 1) ? "+3s" : "+8s";
        if (w.stage == 1) { w.stage = 2; w.due = ::GetTickCount() + 5000; } else w.stage = 0;
        ::Character* v = FindSpawned(w.victim);
        ::Character* a = FindSpawned(w.shooter);
        ++g_shotWatchLines;
        if (v == 0 || a == 0)
        {
            ++g_shotWatchGone;
            w.stage = 0;
            DebugLog("[P104] SHOT-IN WATCH " + when + " victim uid=" + N3(w.victim) + " shooter copy uid=" + N3(w.shooter)
                     + " -> " + (v == 0 ? "no victim character" : "no copy of the shooter") + " (gone)");
            continue;
        }
        const void* tgt = 0;
        const int tRead = P311ReadTarget(v, &tgt);
        const int enemy = P311IsEnemy(v, a);
        std::string tw = "NONE";
        if (tgt == (const void*)a) { tw = "COPY (the shooter's copy, uid=" + N3(w.shooter) + ")"; ++g_shotWatchTurned; }
        else if (tgt != 0) { const unsigned int tu = UidOf((::Character*)tgt); tw = "OTHER (" + (tu != 0 ? "uid=" + N3(tu) : Ptr(tgt)) + ")"; }
        DebugLog("[P104] SHOT-IN WATCH " + when + " victim uid=" + N3(w.victim) + " shooter copy uid=" + N3(w.shooter)
                 + " targetRead=" + N3(tRead) + " target=" + tw + " treatsAsEnemy(copy)=" + N3(enemy)
                 + (tgt == (const void*)a ? " -> TURNED on the shooter's copy" : " -> not targeting the shooter's copy"));
    }
}

// T-546 (owner 512), READ ONLY: `ffshow <uid1> <uid2>` - how two characters stand towards each other, both directions, on the game it
// is sent to. Taken at the K2 safe point (worker paused: isEnemyOf can insert a default standing row, F617). For each direction
// (a towards b): isAllyOf / isEnemyOf (factorInDisguises 1) through a's own vtable +0x3F0 / +0x3E8, i.e. through peace.cpp's
// detours; the squad memory's marks on b - Character::isTagged 0x677ED0 (a, b, tag) for 4 ST_TEMPORARY_ENEMY and 3 TEMP_ALLY,
// the test isAllyOf / isEnemyOf make; whether b is in a's CombatClass attacker list (lektor<hand> +0x1F8, the list
// 0x666390 adds to: count +0x200, elements +0x208, 0x20 bytes each, the hand's five numbers at +8 compared with b's own handle
// at b +0x60) and in its threats list (lektor<Character*> +0x220); a's combat target (+0x290) and combat mode (+0x130). One [FF]
// line, with the team pair, peace.cpp's team counters and relations.cpp's skipped standing changes.
namespace {
unsigned long long kFfIsTagged = 0; static coop::AddrReg kFfIsTagged_reg("Character_isTagged", &kFfIsTagged);   /* Steam_1.0.65 0x677ED0 */
typedef char (*FfIsTaggedFn)(::Character*, ::Character*, int);
typedef char (*FfAnswerFn)(::Character*, ::Character*, char);
const size_t kFfVtIsAlly = 0x3F0, kFfVtIsEnemy = 0x3E8;
const size_t kFfAttackers = 0x1F8, kFfThreats = 0x220, kFfLektorCount = 0x8, kFfLektorStuff = 0x10, kFfHandStride = 0x20, kFfHandNumbers = 0x8;
const size_t kFfCharHandNumbers = 0x60;   /* the character's own hand (+0x58) past its vtable pointer: type, container, containerSerial, index, serial */
const int kFfTagTemporaryEnemy = 4, kFfTagTemporaryAlly = 3, kFfListCap = 64;
unsigned int g_ffA = 0, g_ffB = 0;
bool g_ffDue = false;
long long g_ffLines = 0;
/* One direction, a towards b. Each field -1 = unread (a fault, or no combat class / row). 1 read, 0 a fault. */
struct FfSide { int ally, enemy, tag4, tag3, inAttackers, attackers, inThreats, threats, mode; const void* target; };
int FfAnswerPod(::Character* a, ::Character* b, size_t slot, int* out)
{
    *out = -1;
    __try { const FfAnswerFn f = *(const FfAnswerFn*)(*(const char* const*)a + slot); *out = f(a, b, 1) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int FfTagPod(FfIsTaggedFn f, ::Character* a, ::Character* b, int tag, int* out)
{
    *out = -1;
    if (f == 0) return 0;
    __try { *out = f(a, b, tag) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int FfCombatPod(::Character* a, ::Character* b, FfSide* s)
{
    s->inAttackers = s->attackers = s->inThreats = s->threats = s->mode = -1; s->target = 0;
    __try
    {
        const char* body = *(const char* const*)((const char*)a + kRsCharBody);
        if (!RsPlaus(body)) return 0;
        const char* cc = *(const char* const*)(body + kRsBodyCombat);
        if (!RsPlaus(cc)) return 0;
        s->mode = *(const unsigned char*)(cc + kRsCombatFlag130) != 0 ? 1 : 0;   /* _isInCombatMode 0x4400B0 reads this byte */
        s->target = *(const void* const*)(cc + kRsCombatTarget);
        unsigned char bHand[20];
        std::memcpy(bHand, (const char*)b + kFfCharHandNumbers, 20);
        const unsigned int na = *(const unsigned int*)(cc + kFfAttackers + kFfLektorCount);
        const char* ea = *(const char* const*)(cc + kFfAttackers + kFfLektorStuff);
        s->attackers = (int)na; s->inAttackers = 0;
        for (unsigned int i = 0; i < na && i < (unsigned int)kFfListCap && RsPlaus(ea); ++i)
            if (std::memcmp(ea + i * kFfHandStride + kFfHandNumbers, bHand, 20) == 0) { s->inAttackers = 1; break; }
        const unsigned int nt = *(const unsigned int*)(cc + kFfThreats + kFfLektorCount);
        const void* const* et = *(const void* const* const*)(cc + kFfThreats + kFfLektorStuff);
        s->threats = (int)nt; s->inThreats = 0;
        for (unsigned int i = 0; i < nt && i < (unsigned int)kFfListCap && RsPlaus(et); ++i)
            if (et[i] == (const void*)b) { s->inThreats = 1; break; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string FfSideText(::Character* a, ::Character* b, unsigned int ua, unsigned int ub)
{
    FfSide s;
    FfAnswerPod(a, b, kFfVtIsAlly, &s.ally);
    FfAnswerPod(a, b, kFfVtIsEnemy, &s.enemy);
    const FfIsTaggedFn tf = kFfIsTagged != 0 ? (FfIsTaggedFn)coop::AddrAbs(kFfIsTagged) : (FfIsTaggedFn)0;
    FfTagPod(tf, a, b, kFfTagTemporaryEnemy, &s.tag4);
    FfTagPod(tf, a, b, kFfTagTemporaryAlly, &s.tag3);
    FfCombatPod(a, b, &s);
    std::string tgt = "NONE";
    if (s.target == (const void*)b) tgt = "THE OTHER (uid=" + N3(ub) + ")";
    else if (s.target != 0) { const unsigned int tu = UidOf((::Character*)s.target); tgt = "OTHER (" + (tu != 0 ? "uid=" + N3(tu) : Ptr(s.target)) + ")"; }
    return N3(ua) + "->" + N3(ub) + " ally=" + N3(s.ally) + " enemy=" + N3(s.enemy) + " tag4=" + N3(s.tag4) + " tag3=" + N3(s.tag3)
         + " inAttackers=" + N3(s.inAttackers) + "/" + N3(s.attackers) + " inThreats=" + N3(s.inThreats) + "/" + N3(s.threats)
         + " combatMode=" + N3(s.mode) + " target=" + tgt;
}
void FfShowRun()
{
    g_ffDue = false;
    ::Character* a = FindSpawned(g_ffA);
    ::Character* b = FindSpawned(g_ffB);
    if (a == 0 || b == 0)
    {
        DebugLog("[FF] ffshow " + N3(g_ffA) + " " + N3(g_ffB) + " -> " + (a == 0 ? "no character uid=" + N3(g_ffA) : "no character uid=" + N3(g_ffB)));
        return;
    }
    ++g_ffLines;
    DebugLog("[FF] ffshow teamPair=" + N3(coop::PeaceTeamPairOfCharacters((void*)a, (void*)b))
             + " | " + FfSideText(a, b, g_ffA, g_ffB) + " | " + FfSideText(b, a, g_ffB, g_ffA)
             + " |" + coop::PeaceTeamTokens() + " teamEventSkipped=" + N3(coop::RelationsTeamEventSkipped())
             + " isTaggedRow=" + N3(kFfIsTagged != 0 ? 1 : 0)
             + " (ally/enemy through the hooked isAllyOf/isEnemyOf; tag4 = a temporary-enemy mark, tag3 = a temporary-ally mark; -1 unread)");
}
} // namespace (T-546 ffshow)

// MAIN THREAD (the command channel): `ffshow <uid1> <uid2>` - the [FF] line at the next K2 safe point. READ ONLY.
std::string FfShowLever(const std::string& args)
{
    std::istringstream is(args);
    unsigned int a = 0, b = 0;
    if (!(is >> a >> b) || a == 0 || b == 0 || a == b) return "error ffshow: usage ffshow <uid1> <uid2> (two different characters)";
    if (FindSpawned(a) == 0) return "error ffshow: no character uid=" + N3(a);
    if (FindSpawned(b) == 0) return "error ffshow: no character uid=" + N3(b);
    g_ffA = a; g_ffB = b; g_ffDue = true;
    return "ok ffshow " + N3(a) + " " + N3(b) + " (the [FF] line at the next safe point)";
}

// P104 fix, K2 safe point (MAIN THREAD, worker paused). See combat.h.
void ShotSafePointDrain()
{
    if (g_ffDue && !EngineWritesBlocked()) FfShowRun();   /* T-546: the ffshow readout, worker paused */
    /* T-311 fold 1: the WATCH lines sample here, worker paused - the victim's treatsAsEnemy can insert a default standing row (F617),
       which the ordinary per-frame tick must not do while the worker runs. Not while a world loads or tears down. */
    if (!EngineWritesBlocked()) ShotReactWatchTick();
    if (g_shotInQ.empty()) return;
    std::vector<ShotIn> q;
    q.swap(g_shotInQ);
    const bool blocked = EngineWritesBlocked();
    for (size_t i = 0; i < q.size(); ++i)
    {
        const ShotIn& s = q[i];
        const char* why = 0;
        ::Character* v = 0;
        ::Character* a = 0;
        if (blocked) { ++g_shotBlockedLoad; why = "a world is loading or tearing down"; }
        else if (g_p104AddWound == 0) { ++g_shotNoAddWound; why = "addWound is OFF (see the ranged probe line)"; }
        else if (!net::IsUidMine(s.victim)) { ++g_shotRefusedNotMine; why = "the victim is no longer driven here"; }
        else if ((v = FindSpawned(s.victim)) == 0) { ++g_shotNoVictim; why = "no character for the victim uid"; }
        else if ((a = FindSpawned(s.shooter)) == 0) { ++g_shotNoShooter; ++g_shotReactNoCopy; why = "no copy of the shooter here (addWound reads the attacker unconditionally) - no wound, no reactions"; }
        if (why == 0)
        {
            void* an = 0; void* ap = 0; void* en = 0;
            int reason = kHitBodyAttached, gate = 0;
            bool orientable = (AppearanceBodyAttached(v, &an, &ap, &en, &reason, &gate) != 0);
            if (orientable && gate != 0)   /* harpoon 0: addWound orients on the attacker's body when the victim's gate is set */
            {
                int atReason = kHitBodyAttached;
                if (AppearanceBodyChain(a, false, &an, &ap, &en, &atReason, 0) == 0) { orientable = false; reason = kHitAttackerNotOrientable; }
            }
            if (!orientable)
            {
                ++g_shotNotOrientable;
                why = (reason >= 0 && reason < kHitBodySkipCount) ? kHitBodyReasonWord[reason] : "notOrientable";
            }
        }
        if (why != 0)
        {
            if (g_shotInLogged < kRsLogLimit)
            {
                ++g_shotInLogged;
                DebugLog("[P104] SHOT-IN NOT APPLIED victim uid=" + N3(s.victim) + " shooter uid=" + N3(s.shooter)
                         + " dmg=" + F1(s.rec[3]) + " - " + why);
            }
            continue;
        }
        float before[32 * kPartFloats];
        float after[32 * kPartFloats];
        float bloodBefore = 0.0f, bloodAfter = 0.0f;
        const int nb = SnapshotHealth(s.victim, before, 32, &bloodBefore);
        float dm[6];
        coopshot::ShotDamages(s.rec[3], dm);
        int armour[2] = { -1, 0 };   /* iShotYou's local_res20: -1 in; >= 0 out = an armour deflected it */
        int side[2] = { 0, 0 };      /* iShotYou's local_res18: addWound writes 1 / 2 */
        void* part = 0;
        if (!P104CallAddWound(v, a, dm, armour, side, &part))
        {
            ++g_shotFault;
            ErrorLog("[P104] SHOT-IN FAULTED inside MedicalSystem::addWound victim uid=" + N3(s.victim) + " shooter uid=" + N3(s.shooter)
                     + " dmg=" + F1(s.rec[3]) + " - caught, not retried");
            continue;
        }
        ++g_shotApplied;
        const int na = SnapshotHealth(s.victim, after, 32, &bloodAfter);
        if (g_shotInLogged < kRsLogLimit)
        {
            ++g_shotInLogged;
            double sb = 0.0, sa = 0.0;
            for (int k = 0; k < nb * kPartFloats; ++k) sb += before[k];
            for (int k = 0; k < na * kPartFloats; ++k) sa += after[k];
            DebugLog("[P104] SHOT-IN #" + N3(g_shotApplied) + " victim uid=" + N3(s.victim) + " (mine) shooter uid=" + N3(s.shooter)
                     + " (copy) dmg=" + F1(s.rec[3]) + " onPurpose=" + N3(s.onPurpose ? 1 : 0)
                     + " -> APPLIED through MedicalSystem::addWound (type 6): part=" + Ptr(part) + " armour=" + N3(armour[0])
                     + " side=" + N3(side[0]) + " partSum " + F1((float)sb) + " -> " + F1((float)sa)
                     + " blood " + F1(bloodBefore) + " -> " + F1(bloodAfter)
                     + ((nb <= 0 || na <= 0) ? std::string(" (health unread)") : std::string())
                     + " - the shooter's game sees it in our STATE");
        }
        ShotReactOne(v, a, s, dm, part);   /* T-311: iShotYou's victim-side reactions, the shooter's copy as the attacker */
    }
}

void InstallRangedProbe()
{
    for (LONG i = 0; i < kRsRing; ++i) g_rsRing[i].seq = i;
    InterlockedExchange(&g_rsRingReady, 1);
    const intptr_t sh = (intptr_t)coop::AddrAbs(kP104GunShoot);
    const intptr_t hy = (intptr_t)coop::AddrAbs(kP104IShotYou);
    const bool okShoot = (sh != 0 && coop::AddHook((void*)sh, (void*)&detour_p104Shoot, (void**)&orig_p104Shoot) == coop::SUCCESS);
    const bool okHit = (hy != 0 && coop::AddHook((void*)hy, (void*)&detour_p104ShotYou, (void**)&orig_p104ShotYou) == coop::SUCCESS);
    g_rsHooks = (okShoot ? 1 : 0) + (okHit ? 1 : 0);
    P112Install();   /* PROBE P112 (T-310): the aim check + bolt update hooks, read only */
    uintptr_t aw = 0;
    {
        int at = -1;
        const uintptr_t ret = (kP104AddWoundDmgRet != 0) ? (uintptr_t)coop::AddrAbs(kP104AddWoundDmgRet) : 0;
        const uintptr_t ad = (kP104ApplyDamage != 0) ? (uintptr_t)coop::AddrAbs(kP104ApplyDamage) : 0;
        g_p104AddWoundWhy = P104DeriveAddWound((uintptr_t)hy, ret, ad, &aw, &at);
        g_p104AddWoundCallAt = at;
        if (g_p104AddWoundWhy == 1) g_p104AddWound = (P104AddWoundFn)aw;
    }
    DebugLog(std::string("[P104] ranged probe: GunClass::shoot AddHook ") + (okShoot ? "SUCCESS" : "FAILED")
             + ", Character::iShotYou AddHook " + (okHit ? "SUCCESS" : "FAILED")
             + "; MSG_SHOT apply (MedicalSystem::addWound) "
             + (g_p104AddWound != 0
                    ? "ON: entry " + Ptr((const void*)aw) + " = ApplyDamageHitRet - 0x7CC, verified (its CALL reaches ApplyDamage;"
                      " iShotYou calls it at +" + N3(g_p104AddWoundCallAt) + ")"
                    : "OFF why=" + N3(g_p104AddWoundWhy) + " (0 row unbound, -1 the ret's CALL is not applyDamage's, -2 iShotYou"
                      " does not call the derived entry, -3 unreadable) - a MSG_SHOT is counted, not applied")
             + " (P104 fix, protocol 104: a hit on a COPY by our shooter goes to its owner as MSG_SHOT; a COPY's bolt makes no hit here)");
    DebugLog(std::string("[P104] T-311 victim reactions after a SHOT-IN wound: rememberCharacter ")
             + (kP311Remember != 0 ? Ptr((const void*)coop::AddrAbs(kP311Remember)) : std::string("NO ROW"))
             + ", AI::underRangedAttack " + (kP311UnderRanged != 0 ? Ptr((const void*)coop::AddrAbs(kP311UnderRanged)) : std::string("NO ROW"))
             + ", hit reaction " + (kP311HitReaction != 0 ? Ptr((const void*)coop::AddrAbs(kP311HitReaction)) : std::string("NO ROW"))
             + " (iShotYou's own conditions; lines SHOT-IN REACT / SHOT-IN WATCH)");
}

// MAIN THREAD (the command channel). TEST-ONLY lever:
//   rangedtest <shooterUid> <targetUid> [crossbowSid [boltSid [boltCount]]]   the shooter must be OURS
//   rangedtest report | rangedtest health <uid>
//   rangedtest skill <uid> <0..100>   P104s: crossbows and precision shooting of an OWNED character (the aim cone)
// P104f1: boltCount is ROUNDS (ammo items hold their rounds in chargesLeft; they do not stack).
// T-310: TWO RECHECK lines (CombatTick), +2 s and +10 s after the order. One reload cycle is ~7 s (T686: the first bolt 7.0 s after
// the order, then one every 7.0 s), so loaded=0 at +2 s is the engine's own reload running, not a fault; +10 s is the answer.
// The gun (+0x28) is re-read by updateMT only while combatMode (+0x36) is on.
static unsigned int g_rtRecheckUid = 0, g_rtRecheckTarget = 0;
static DWORD g_rtRecheckDue = 0;
static int g_rtRecheckStage = 0;          /* 1: the +2 s line is next; 2: the +10 s line is next */
static long long g_rtRecheckShots0 = 0;   /* g_rsShots when the order was given */
static void RangedTestRecheckTick()
{
    if (g_rtRecheckUid == 0 || (long)(::GetTickCount() - g_rtRecheckDue) < 0) return;   /* GetTickCount: VS2010-era headers have no GetTickCount64; wrap-safe difference */
    const unsigned int su = g_rtRecheckUid, tu = g_rtRecheckTarget;
    const std::string when = (g_rtRecheckStage == 1) ? "+2s" : "+10s";
    if (g_rtRecheckStage == 1) { g_rtRecheckStage = 2; g_rtRecheckDue = ::GetTickCount() + 8000; }
    else { g_rtRecheckUid = 0; g_rtRecheckStage = 0; }
    ::Character* s = FindSpawned(su);
    if (s == 0)
    {
        g_rtRecheckUid = 0; g_rtRecheckStage = 0;
        DebugLog("[P104] rangedtest RECHECK " + when + " shooter uid=" + N3(su) + " -> no character (gone)");
        return;
    }
    int hasRc = 0, mode = -1, hasGun = 0, loaded = -1, tt = -1;
    const int rdOk = RsReadRanged(s, &hasRc, &mode, &hasGun, &loaded, &tt);
    int rcState = -1, reloading = -1, ammoType = -1;
    float rt = -1.0f, rtMax = -1.0f;
    const int rlOk = RtReadReload(s, &rcState, &reloading, &rt, &rtMax, &ammoType);
    float dist = -1.0f, dy = 0.0f;
    const int dOk = RtDistance(s, FindSpawned(tu), &dist, &dy);
    const long long shots = (long long)g_rsShots - g_rtRecheckShots0;
    std::string verdict;
    if (!hasRc) verdict = "NO RANGED OBJECT";
    else if (mode == 0)
        verdict = "combat mode OFF: the order was dropped or replaced - look for '[M6] combat mode ON uid=" + N3(su)
                + "' (a melee attacker: in T695 a town guard, 3.4 s after the order)";
    else if (!hasGun) verdict = "combat mode ON but NO GUN: no drawable crossbow found";
    else if (loaded >= 1) verdict = "LOADED";
    else if (reloading == 1 && rt > 0.0f) verdict = "RELOADING (the engine's own reload is running)";
    else if (reloading == 1)
        verdict = "STUCK: the reload timer ran out and no bolt of the gun's ammo type was found (reloadCheck 0x437940"
                  " searches the character's inventory, then 0x5CA890's)";
    else if (ammoType == 1) verdict = "EMPTY, NOT RELOADING (reloadCheck did not start one: the character's vtable +0x178 test said no)";
    else verdict = "EMPTY, NOT RELOADING";
    DebugLog("[P104] rangedtest RECHECK " + when + " shooter uid=" + N3(su) + " target uid=" + N3(tu)
             + " read=" + N3(rdOk) + " rc=" + N3(hasRc) + " rcCombatMode=" + N3(mode) + " gun=" + N3(hasGun)
             + " loaded=" + N3(loaded) + " turretHandType=" + N3(tt)
             + " reloadRead=" + N3(rlOk) + " rcState=" + N3(rcState) + " reloading=" + N3(reloading)
             + " reloadTimer=" + F1(rt) + "/" + F1(rtMax) + "s ammoType=" + N3(ammoType)
             + " dist=" + (dOk ? F1(dist) : std::string("-")) + "u dy=" + (dOk ? F1(dy) : std::string("-")) + "u"
             + " shotsSinceOrder=" + N3(shots) + " shotsSoFar=" + N3(g_rsShots) + " rhitsSoFar=" + N3(g_rsHits)
             + " -> " + verdict);
}

std::string RangedTestLever(const std::string& args)
{
    const std::string usage = "error rangedtest: usage rangedtest <shooterUid> <targetUid> [crossbowSid [boltSid [boltCount]]] | report | health <uid> | skill <uid> <0..100>";
    std::istringstream is(args);
    std::string first;
    if (!(is >> first)) return usage;
    if (first == "report")
    {
        RangedEventsDrain();
        const std::string l = RangedCountersLine();
        DebugLog(l);
        P112Drain(); DebugLog(P112ReportLine());   /* PROBE P112 (rangedtest report) */
        return "ok rangedtest " + l;
    }
    if (first == "watch") return P112WatchLever(is);   /* PROBE P112: rangedtest watch <seconds> <uid> [<uid> ...] */
    if (first == "health")
    {
        unsigned int uid = 0;
        if (!(is >> uid) || uid == 0) return usage;
        float rec[32 * kPartFloats];
        float blood = 0.0f;
        const int n = SnapshotHealth(uid, rec, 32, &blood);
        if (n <= 0) return "error rangedtest health: no health read for uid " + N3(uid);
        double sum = 0.0;
        for (int i = 0; i < n * kPartFloats; ++i) sum += rec[i];
        const std::string l = "[P104] HEALTH uid=" + N3(uid) + " mine=" + N3(net::IsUidMine(uid) ? 1 : 0)
                            + " parts=" + N3(n) + " partSum=" + F1((float)sum) + " blood=" + F1(blood);
        DebugLog(l);
        return "ok rangedtest " + l;
    }
    if (first == "skill")   /* P104s TEST-ONLY: rangedtest skill <uid> <0..100> - an OWNED character only */
    {
        unsigned int uid = 0;
        float v = -1.0f;
        if (!(is >> uid >> v) || uid == 0 || !(v >= 0.0f && v <= 100.0f))
            return "error rangedtest skill: usage rangedtest skill <uid> <0..100>";
        ::Character* c = FindSpawned(uid);
        if (c == 0) return "error rangedtest skill: no character for uid " + N3(uid);
        if (!net::IsUidMine(uid)) return "error rangedtest skill: uid " + N3(uid) + " is a copy here - send it to its own game";
        float b[7], a[7];
        const int r0 = RtSkillRW(c, v, 0, b);
        if (r0 != 1) return "error rangedtest skill: stats not readable (" + N3(r0) + ") for uid " + N3(uid) + " - nothing written";
        const int r1 = RtSkillRW(c, v, 1, a);
        float cone = -1.0f;
        if (r1 == 1 && a[6] >= 0.0f)
        {
            cone = a[6] * (1.0f - kRtAimSkillScale * (a[0] + a[1] + a[3]));
            if (cone < 0.0f) cone = 0.0f;
        }
        const std::string l = "[P104] rangedtest skill (TEST) uid=" + N3(uid) + " write=" + N3(r1)
                            + " crossbows " + F1(b[0]) + " -> " + F1(a[0])
                            + " precisionShooting(perception) " + F1(b[1]) + " -> " + F1(a[1])
                            + " perceptionBonus=" + N3((long long)a[3]) + " dexterity=" + F1(a[2]) + " (read only)"
                            + " rcStat=" + N3((long long)a[4]) + " gunRange=" + F1(a[5]) + " gunDeviationBase=" + F1(a[6])
                            + " maxConeDeg=" + (cone < 0.0f ? std::string("-") : F1(cone))
                            + " (GunClass::shoot: cone = gunDeviationBase * (1 - 0.005 * (crossbows + perception + bonus)), before"
                              " injury penalties, times rand; gun -1 = not drawn yet)";
        DebugLog(l);
        if (r1 != 1) return "error rangedtest skill: the write faulted (" + N3(r1) + ") " + l;
        return "ok rangedtest " + l;
    }
    unsigned int su = 0, tu = 0;
    { std::istringstream fs(first); fs >> su; }
    if (!(is >> tu) || su == 0 || tu == 0) return usage;
    std::string xbow, bolt;
    int boltCount = 30;
    if (is >> xbow) { if (is >> bolt) { int bc = 0; if ((is >> bc) && bc > 0) boltCount = bc; } }
    ::Character* s = FindSpawned(su);
    if (s == 0) return "error rangedtest: no character for shooter uid " + N3(su);
    if (!net::IsUidMine(su)) return "error rangedtest: shooter uid " + N3(su) + " is a copy here - send it to the shooter's own game";
    ::Character* t = FindSpawned(tu);
    if (t == 0) return "error rangedtest: no character for target uid " + N3(tu);
    if (s == t) return "error rangedtest: shooter and target are the same character";
    std::string gave = "none";
    if (!xbow.empty())
    {
        gave = "crossbow:" + RangedTestGive(su, xbow, 1);
        if (!bolt.empty()) gave += ",bolts:" + RangedTestGiveAmmo(su, bolt, boltCount);
    }
    float dist = -1.0f, dy = 0.0f;
    const int dOk = RtDistance(s, t, &dist, &dy);   /* T-310: T689's 0.8 m target was never fired at */
    const bool ordered = InjectAttackOrder(s, t, kRsRangedAttackFocused);
    int hasRc = 0, mode = -1, hasGun = 0, loaded = -1, tt = -1;
    const int rdOk = RsReadRanged(s, &hasRc, &mode, &hasGun, &loaded, &tt);
    ++g_rsLeverRuns;
    const std::string l = "[P104] rangedtest (TEST) shooter uid=" + N3(su) + " target uid=" + N3(tu)
                        + " targetMine=" + N3(net::IsUidMine(tu) ? 1 : 0) + " gave=" + gave
                        + " order=RANGED_ATTACK_FOCUSED(263) ordered=" + N3(ordered ? 1 : 0)
                        + " read=" + N3(rdOk) + " rc=" + N3(hasRc) + " rcCombatMode=" + N3(mode) + " gun=" + N3(hasGun)
                        + " loaded=" + N3(loaded) + " turretHandType=" + N3(tt)
                        + " shotsSoFar=" + N3(g_rsShots) + " rhitsSoFar=" + N3(g_rsHits)
                        + " dist=" + (dOk ? F1(dist) : std::string("-")) + "u dy=" + (dOk ? F1(dy) : std::string("-")) + "u"
                        + ((dOk && dist < kRtPointBlank) ? std::string(" POINT-BLANK (under 3 m: T689 at 0.8 m never fired)") : std::string())
                        + " (gun/loaded at this instant only - the RECHECK lines +2 s and +10 s are the answer; one reload is ~7 s)";
    DebugLog(l);
    g_rtRecheckUid = su; g_rtRecheckTarget = tu; g_rtRecheckDue = ::GetTickCount() + 2000;
    g_rtRecheckStage = 1; g_rtRecheckShots0 = (long long)g_rsShots;
    return "ok " + l;
}

// =====================================================================================================================
// T-310 TEST-ONLY LEVERS groundline / groundfind (owner decision 232 (a), 2026-09-30): IS THE GROUND BETWEEN TWO POINTS
// LEVEL, AND DOES ANYTHING STAND IN THE LINE OF FIRE? Asked BEFORE a crossbow test shoots (rangedtest), because the aim
// check 0x437730 never lets terrain stop a shot (P112: a ray hit with no object -> FIRE) and the bolt then strikes the
// hill. READ ONLY: nothing in the world is written or moved. MAIN THREAD: the levers (the command channel) and
// GroundLineTick (CombatTick, under CommandChannelTick).
// Two engine calls, both static UtilityT functions (Read, Ghidra disassembly of Steam 1.0.65):
//   UtilityT::getTerrainHeightFast(float x, float z, ZoneMap* map) 0x9B2420: x XMM0, z XMM1, map R8, the height in XMM0.
//     map == 0 is what UtilityT::getTerrainHeight 0x9B2840 itself passes (its whole body: xor r8d,r8d; jmp here). It
//     asks the ZoneManager's zone map for (x,z); when none answers it asks the renderer's terrain (an imported Ogre
//     call); when that does not answer either it returns -99.0 (0xC2C60000 at 0x168ADA0). -99.0 = NO TERRAIN THERE.
//   UtilityT::trace(physHit& hit, const Vector3& origin, const Vector3& dir, unsigned groups) 0x9B5BC0: resets hit
//     (0x4CAC30), then - unless hit+5 (doNotAbort) is set - asks the physics scene (0x173F70 on the global at
//     0x21330C8) whether a query may run now; if not it sets hit+4 (traceWasAborted) and returns WITHOUT tracing.
//     Otherwise the PhysX scene's raycastClosestShape (vtable +0x380), all shapes, dir normalised, max distance
//     FLT_MAX. A hit: +7 = 1, +8 the point, +0x20 the distance, +0x38 the hit object's hand type (0 BUILDING,
//     1 CHARACTER, 11 RECORD_NONE = no object), +0x50 the shape's collision group (written only when hit+6 needsGroup
//     is set). No hit: +7 = 0 and the point's y = -99.0. The camera code 0x6AF4A0 calls it on the main thread.
//   groups 0x087F9E07: the mask the aim check's own ray uses (0x4357A0 with its third argument 1, decomp_4357a0.txt).
// The physHit is 0x60 bytes. Its hand at +0x30 keeps a zero vtable: trace and reset write
// only +0x38..+0x4B of it and call nothing through it (Read, the two disassemblies).
// Scale: 10 world units a metre (the project's Inferred scale, as the rangedtest lines). Chest height 14 u: Inferred
// (the aim point is the target's "Bip01 Spine2" bone, 0x436970; its height above the feet was never measured).
// =====================================================================================================================
static unsigned long long kGlTerrainFast = 0; static coop::AddrReg kGlTerrainFast_reg("UtilityT_groundHeight", &kGlTerrainFast);   /* Steam_1.0.65 0x9B2420 */
static unsigned long long kGlTrace = 0; static coop::AddrReg kGlTrace_reg("UtilityT_trace", &kGlTrace);   /* Steam_1.0.65 0x9B5BC0 */

static const float kGlNoTerrain = -99.0f;       /* getTerrainHeightFast's "no terrain answered" and trace's no-hit y */
static const unsigned int kGlGroups = 0x087F9E07u;
static const float kGlUnitsPerM = 10.0f;        /* Inferred scale */
static const float kGlStep = 10.0f;             /* one ground sample every 1 m or closer, both ends included */
static const float kGlChest = 14.0f;            /* the line of fire: ground + 1.4 m at each end (Inferred) */
static const float kGlEndSkip = 10.0f;          /* each chest ray starts 1 m in from its end and must reach 1 m short of the other: the capsules of a shooter and a target standing there are not the answer */
static const float kGlProbeUp = 50.0f;          /* the ground probe starts 5 m above the terrain height ... */
static const float kGlProbeBelow = 10.0f;       /* ... and must hit no lower than 1 m under it (else no collision is loaded there) */
static const float kGlRaised = 5.0f;            /* a probe hit more than 0.5 m above the terrain: something stands there */
static const float kGlTolDefault = 10.0f;       /* level = highest ground - lowest ground <= 1 m */
static const float kGlTolMax = 1000.0f;
static const float kGlLenMin = 20.0f, kGlLenMax = 2000.0f;   /* groundline: 2 m .. 200 m */
static const float kGlFindDistMax = 1000.0f;    /* groundfind: pairs 2 m .. 100 m apart */
static const float kGlRadiusMax = 3000.0f;      /* groundfind: shooter spots within 300 m of the centre */
static const float kGlRing = 20.0f;             /* groundfind: shooter spots on rings 2 m apart, about 2 m apart on each ring */
static const float kGlTwoPi = 6.2831853f;
static const int kGlMaxSamples = 201;           /* kGlLenMax / kGlStep + 1 */
static const int kGlDirs = 8;                   /* groundfind: 8 directions from each shooter spot, 45 degrees apart */
static const int kGlFindPerTick = 8;            /* groundfind: candidate lines per main tick */
static const int kGlFindMaxCandidates = 4000;
static const int kGlBusyTicks = 600;            /* consecutive ticks the engine refused to trace before PHYSICS_BUSY */
/* kGlHitSize: the fields read end at +0x52; the buffer is 0x200 so an engine physHit larger than the fields we know (its full size is not traced) cannot write past it */
static const size_t kGlHitSize = 0x200, kGlHitFallback = 0x0, kGlHitAborted = 0x4, kGlHitNoAbort = 0x5, kGlHitWantGroup = 0x6,
                    kGlHitHit = 0x7, kGlHitPoint = 0x8, kGlHitDist = 0x20, kGlHitType = 0x38, kGlHitGroup = 0x50;

enum { kGlClear = 0, kGlBlocked, kGlNotLevel, kGlNotLoaded, kGlBusy, kGlFault, kGlVerdictCount };
static const char* const kGlVerdictWord[kGlVerdictCount] = { "CLEAR", "BLOCKED", "NOT_LEVEL", "NOT_LOADED", "PHYSICS_BUSY", "FAULT" };

static long long g_glLineJobs = 0, g_glLineVerdict[kGlVerdictCount] = { 0 }, g_glLineStopped = 0;
static long long g_glFindJobs = 0, g_glFindFound = 0, g_glFindNone = 0, g_glFindBusy = 0, g_glFindStopped = 0, g_glCandidates = 0;
static long long g_glTraces = 0, g_glTraceAborted = 0, g_glTraceFault = 0, g_glHeightReads = 0, g_glHeightNone = 0, g_glHeightFault = 0;
static long long g_glRefusedUsage = 0, g_glRefusedNoRows = 0, g_glRefusedNoWorld = 0, g_glRefusedBusy = 0, g_glRefusedNoChar = 0,
                 g_glRefusedRange = 0;

/* 1 a height, 0 no terrain answered there (-99.0), -1 no row / the call faulted. MAIN THREAD. */
static int GlHeight(float x, float z, float* out)
{
    *out = kGlNoTerrain;
    const unsigned long long a = (kGlTerrainFast != 0) ? coop::AddrAbs(kGlTerrainFast) : 0;
    if (a == 0) { ++g_glHeightFault; return -1; }
    typedef float (*Fn)(float x, float z, void* map);
    ++g_glHeightReads;
    __try { *out = ((Fn)a)(x, z, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_glHeightFault; return -1; }
    if (*out == kGlNoTerrain) { ++g_glHeightNone; return 0; }
    return 1;
}

struct GlHit { int hit; float pt[3]; float dist; int type; int group; };

/* 1 traced (h filled), 0 the engine refused to trace now (traceWasAborted: the physics scene is busy), -1 no row /
   the call faulted. MAIN THREAD. */
static int GlTrace(const float* origin, const float* dir, GlHit* h)
{
    h->hit = 0; h->pt[0] = h->pt[1] = h->pt[2] = 0.0f; h->dist = -1.0f; h->type = -1; h->group = -1;
    const unsigned long long a = (kGlTrace != 0) ? coop::AddrAbs(kGlTrace) : 0;
    if (a == 0) { ++g_glTraceFault; return -1; }
    typedef void (*Fn)(void* hit, const float* origin, const float* dir, unsigned int groups);
    unsigned long long raw[kGlHitSize / 8];
    for (size_t i = 0; i < kGlHitSize / 8; ++i) raw[i] = 0;
    unsigned char* b = (unsigned char*)raw;
    *(float*)(b + kGlHitFallback) = kGlNoTerrain;
    b[kGlHitNoAbort] = 0;      /* the engine's own "may a query run now" check stays on */
    b[kGlHitWantGroup] = 1;    /* +0x50 = the hit shape's collision group */
    ++g_glTraces;
    __try { ((Fn)a)(b, origin, dir, kGlGroups); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_glTraceFault; return -1; }
    if (b[kGlHitAborted] != 0) { ++g_glTraceAborted; return 0; }
    h->hit = (b[kGlHitHit] != 0) ? 1 : 0;
    if (h->hit)
    {
        const float* p = (const float*)(b + kGlHitPoint);
        h->pt[0] = p[0]; h->pt[1] = p[1]; h->pt[2] = p[2];
        h->dist = *(const float*)(b + kGlHitDist);
        h->type = *(const int*)(b + kGlHitType);
        h->group = (int)*(const unsigned short*)(b + kGlHitGroup);
    }
    return 1;
}

struct GlLine
{
    float ax, az, bx, bz, tol;   /* the ends (world x,z) and the level tolerance, world units */
    int stage;                   /* 0 nothing read, 1 terrain heights read, 2 ground probed, 3 chest rays cast */
    int samples, noTerrain, probeMissing, probeRaised, fwdBlocks, backBlocks, verdict;
    float len, groundA, groundB, gMin, gMax, maxStep, minClear, reach;
    GlHit fwd, back;
    const char* why;
};

/* One line, start to end, in one call: 1 finished (L.verdict set), 0 the engine refused a trace (physics busy) -
   the caller runs the whole line again on a later tick. prefilter (groundfind): a line whose TERRAIN heights alone are
   not level is refused before any probe is cast. MAIN THREAD. */
static int GlEvaluate(GlLine& L, bool prefilter)
{
    L.stage = 0; L.noTerrain = 0; L.probeMissing = 0; L.probeRaised = 0; L.fwdBlocks = 0; L.backBlocks = 0;
    L.groundA = L.groundB = L.gMin = L.gMax = L.maxStep = L.minClear = L.reach = 0.0f;
    L.verdict = kGlFault; L.why = "";
    const float dx = L.bx - L.ax, dz = L.bz - L.az;
    L.len = sqrtf(dx * dx + dz * dz);
    int n = (int)(L.len / kGlStep) + 1;
    if ((float)(n - 1) * kGlStep < L.len) ++n;
    if (n < 2) n = 2;
    if (n > kGlMaxSamples) n = kGlMaxSamples;
    L.samples = n;
    float hT[kGlMaxSamples], g[kGlMaxSamples];
    for (int i = 0; i < n; ++i)
    {
        const float t = (float)i / (float)(n - 1);
        const int r = GlHeight(L.ax + dx * t, L.az + dz * t, &hT[i]);
        if (r < 0) { L.why = "the terrain height call faulted or its row is empty"; return 1; }
        if (r == 0) ++L.noTerrain;
    }
    L.stage = 1;
    if (L.noTerrain > 0)
    {
        L.verdict = kGlNotLoaded;
        L.why = "no terrain height at some samples: neither the zone map nor the renderer's terrain answered";
        return 1;
    }
    if (prefilter)
    {
        float lo = hT[0], hi = hT[0];
        for (int i = 1; i < n; ++i) { if (hT[i] < lo) lo = hT[i]; if (hT[i] > hi) hi = hT[i]; }
        if (hi - lo > L.tol)
        {
            L.gMin = lo; L.gMax = hi;
            L.verdict = kGlNotLevel; L.why = "terrain heights alone, no probe cast";
            return 1;
        }
    }
    for (int i = 0; i < n; ++i)   /* the physics ground under every sample: what a character stands on and a bolt strikes */
    {
        const float t = (float)i / (float)(n - 1);
        const float o[3] = { L.ax + dx * t, hT[i] + kGlProbeUp, L.az + dz * t };
        const float d[3] = { 0.0f, -1.0f, 0.0f };
        GlHit h;
        const int r = GlTrace(o, d, &h);
        if (r < 0) { L.why = "the trace call faulted or its row is empty"; return 1; }
        if (r == 0) return 0;
        if (h.hit && h.pt[1] >= hT[i] - kGlProbeBelow)
        {
            g[i] = h.pt[1];
            if (h.pt[1] > hT[i] + kGlRaised) ++L.probeRaised;
        }
        else { g[i] = hT[i]; ++L.probeMissing; }
    }
    L.stage = 2;
    L.groundA = g[0]; L.groundB = g[n - 1];
    L.gMin = g[0]; L.gMax = g[0];
    for (int i = 1; i < n; ++i)
    {
        if (g[i] < L.gMin) L.gMin = g[i];
        if (g[i] > L.gMax) L.gMax = g[i];
        const float st = (g[i] > g[i - 1]) ? g[i] - g[i - 1] : g[i - 1] - g[i];
        if (st > L.maxStep) L.maxStep = st;
    }
    if (L.probeMissing > 0)
    {
        L.verdict = kGlNotLoaded;
        L.why = "no collision under some samples: the physics ground is not loaded there (a bolt would fly through it)";
        return 1;
    }
    const float yA = g[0] + kGlChest, yB = g[n - 1] + kGlChest;
    L.minClear = kGlChest;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float)i / (float)(n - 1);
        const float c = yA + (yB - yA) * t - g[i];
        if (c < L.minClear) L.minClear = c;
    }
    const float dy = yB - yA;
    const float len3 = sqrtf(dx * dx + dy * dy + dz * dz);
    const float u[3] = { dx / len3, dy / len3, dz / len3 };
    L.reach = len3 - 2.0f * kGlEndSkip;
    const float of[3] = { L.ax + u[0] * kGlEndSkip, yA + u[1] * kGlEndSkip, L.az + u[2] * kGlEndSkip };
    const float ob[3] = { L.bx - u[0] * kGlEndSkip, yB - u[1] * kGlEndSkip, L.bz - u[2] * kGlEndSkip };
    const float ub[3] = { -u[0], -u[1], -u[2] };
    int r = GlTrace(of, u, &L.fwd);
    if (r < 0) { L.why = "the trace call faulted or its row is empty"; return 1; }
    if (r == 0) return 0;
    r = GlTrace(ob, ub, &L.back);   /* both ways: a ray that starts inside a shape does not report that shape */
    if (r < 0) { L.why = "the trace call faulted or its row is empty"; return 1; }
    if (r == 0) return 0;
    L.stage = 3;
    L.fwdBlocks = (L.fwd.hit && L.fwd.dist < L.reach) ? 1 : 0;
    L.backBlocks = (L.back.hit && L.back.dist < L.reach) ? 1 : 0;
    if (L.fwdBlocks || L.backBlocks) { L.verdict = kGlBlocked; L.why = "the chest-height ray hit something between the ends"; }
    else if (L.minClear < 0.0f) { L.verdict = kGlBlocked; L.why = "the ground rises above the chest-height line"; }
    else if (L.gMax - L.gMin > L.tol) { L.verdict = kGlNotLevel; L.why = "highest ground minus lowest ground is over the tolerance"; }
    else L.verdict = kGlClear;
    return 1;
}

static std::string GlXZ(float x, float z) { return F1(x) + "," + F1(z); }
static std::string GlUM(float u) { return F1(u) + "u(" + F1(u / kGlUnitsPerM) + "m)"; }
static std::string GlRay(const GlHit& h, int blocks)
{
    if (!h.hit) return "clear(no hit)";
    return std::string(blocks ? "HIT" : "clear(first hit past the far end)") + " at " + F1(h.dist) + "u point="
         + F1(h.pt[0]) + "," + F1(h.pt[1]) + "," + F1(h.pt[2]) + " objType=" + N3(h.type) + " group=" + N3(h.group);
}
static std::string GlLineText(const GlLine& L)
{
    std::string s = "from=" + GlXZ(L.ax, L.az) + " to=" + GlXZ(L.bx, L.bz) + " len=" + GlUM(L.len) + " samples=" + N3(L.samples)
                  + " noTerrain=" + N3(L.noTerrain);
    if (L.stage >= 2)
        s += " groundFrom=" + F1(L.groundA) + " groundTo=" + F1(L.groundB) + " maxRise=" + GlUM(L.gMax - L.gMin)
           + " maxStep=" + F1(L.maxStep) + "u probes[missing,raised]=" + N3(L.probeMissing) + "," + N3(L.probeRaised);
    else if (L.stage == 1 && L.verdict == kGlNotLevel)
        s += " maxRise(terrainOnly)=" + GlUM(L.gMax - L.gMin);
    s += " tol=" + GlUM(L.tol);
    if (L.stage >= 3)
        s += " chest=" + F1(kGlChest) + "u minClearance=" + GlUM(L.minClear) + " reach=" + F1(L.reach) + "u ray="
           + GlRay(L.fwd, L.fwdBlocks) + " backRay=" + GlRay(L.back, L.backBlocks);
    s += " verdict=" + std::string(kGlVerdictWord[L.verdict]);
    if (L.why != 0 && L.why[0] != 0) s += " (" + std::string(L.why) + ")";
    return s;
}

struct GlJob
{
    int kind;                     /* 0 none, 1 groundline, 2 groundfind */
    long long id;
    GlLine line;
    float cx, cz, dist, radius, tol;
    int ring, point, pointsInRing, dir, tried, busyTicks;
    int tally[kGlVerdictCount];
};
static GlJob g_glJob;
static long long g_glNextId = 0;

/* THIS game's last [GROUNDFIND] FOUND line (the ends, world x,z), for `playerteleport groundpick shooter|target`.
   Set only by a FOUND; a later NONE_FOUND / PHYSICS_BUSY / STOPPED leaves it as it was. MAIN THREAD. */
struct GlPick { long long id; float sx, sz, tx, tz; };
static GlPick g_glPick = { 0, 0.0f, 0.0f, 0.0f, 0.0f };
static long long g_glPickReads = 0, g_glPickNone = 0;

// MAIN THREAD (the command channel). TEST-ONLY. false = no groundfind has FOUND a line on this game yet.
bool GroundFindLastPick(bool target, float* x, float* z, long long* id)
{
    if (g_glPick.id == 0) { ++g_glPickNone; return false; }
    ++g_glPickReads;
    *x = target ? g_glPick.tx : g_glPick.sx;
    *z = target ? g_glPick.tz : g_glPick.sz;
    *id = g_glPick.id;
    return true;
}

// P26lvl maxdy (TEST-ONLY, speech.cpp TsNearCheck): the terrain height at (x,z) - GlHeight, the same SEH-wrapped
// UtilityT::getTerrainHeightFast call (map 0) groundline uses (it counts in the GROUNDLINE REPORT heightReads).
// 1 a height in *out, 0 no terrain answered there (-99.0), -1 no address row / the call faulted. MAIN THREAD.
int GroundTerrainHeightAt(float x, float z, float* out)
{
    return GlHeight(x, z, out);
}

static int GlPointsInRing(int ring)
{
    if (ring == 0) return 1;
    const int n = (int)(kGlTwoPi * (float)ring) + 1;   /* spots about kGlRing apart on a ring of radius ring * kGlRing */
    return (n < 8) ? 8 : n;
}
static void GlSetCandidate(GlJob& j)
{
    const float rr = (float)j.ring * kGlRing;
    const float pa = kGlTwoPi * (float)j.point / (float)j.pointsInRing;
    const float da = kGlTwoPi * (float)j.dir / (float)kGlDirs;
    j.line.ax = j.cx + rr * cosf(pa); j.line.az = j.cz + rr * sinf(pa);
    j.line.bx = j.line.ax + j.dist * cosf(da); j.line.bz = j.line.az + j.dist * sinf(da);
    j.line.tol = j.tol;
}
/* false = every shooter spot within the radius has been tried */
static bool GlAdvance(GlJob& j)
{
    if (++j.dir < kGlDirs) return true;
    j.dir = 0;
    if (++j.point < j.pointsInRing) return true;
    j.point = 0;
    ++j.ring;
    if ((float)j.ring * kGlRing > j.radius) return false;
    j.pointsInRing = GlPointsInRing(j.ring);
    return true;
}
static std::string GlTally(const GlJob& j)
{
    return "rejected[blocked,notLevel,notLoaded,fault]=" + N3(j.tally[kGlBlocked]) + "," + N3(j.tally[kGlNotLevel]) + ","
         + N3(j.tally[kGlNotLoaded]) + "," + N3(j.tally[kGlFault]);
}

// MAIN THREAD (CombatTick). A groundline job runs its line once (again on a later tick while the engine refuses to trace);
// a groundfind job tries kGlFindPerTick candidate lines a tick, nearest shooter spots first, and stops at the first CLEAR.
// The search order is fixed by the arguments alone, so the same request on both games walks the same candidates.
static void GroundLineTick()
{
    GlJob& j = g_glJob;
    if (j.kind == 0) return;
    const std::string tag = (j.kind == 1) ? "[GROUNDLINE]" : "[GROUNDFIND]";
    if (EngineWritesBlocked())
    {
        if (j.kind == 1) ++g_glLineStopped; else ++g_glFindStopped;
        DebugLog(tag + " id=" + N3(j.id) + " STOPPED: no world is running any more (loading, tearing down or not in gameplay)"
                 + " - nothing more was read");
        j.kind = 0;
        return;
    }
    if (j.kind == 1)
    {
        const int r = GlEvaluate(j.line, false);
        if (r == 0)
        {
            if (++j.busyTicks < kGlBusyTicks) return;
            j.line.verdict = kGlBusy;
            j.line.why = "the engine refused to trace (physics scene busy) on every tick of the wait";
        }
        ++g_glLineVerdict[j.line.verdict];
        DebugLog("[GROUNDLINE] id=" + N3(j.id) + " " + GlLineText(j.line) + " busyTicks=" + N3(j.busyTicks));
        j.kind = 0;
        return;
    }
    for (int k = 0; k < kGlFindPerTick; ++k)
    {
        GlSetCandidate(j);
        const int r = GlEvaluate(j.line, true);
        if (r == 0)
        {
            if (++j.busyTicks < kGlBusyTicks) return;   /* the same candidate again next tick */
            ++g_glFindBusy;
            DebugLog("[GROUNDFIND] id=" + N3(j.id) + " PHYSICS_BUSY: the engine refused to trace on " + N3(j.busyTicks)
                     + " ticks in a row; tried=" + N3(j.tried) + " " + GlTally(j));
            j.kind = 0;
            return;
        }
        j.busyTicks = 0;
        ++j.tried; ++g_glCandidates; ++j.tally[j.line.verdict];
        if (j.line.verdict == kGlClear)
        {
            ++g_glFindFound;
            g_glPick.id = j.id;
            g_glPick.sx = j.line.ax; g_glPick.sz = j.line.az; g_glPick.tx = j.line.bx; g_glPick.tz = j.line.bz;
            DebugLog("[GROUNDLINE] id=" + N3(j.id) + " (groundfind pick) " + GlLineText(j.line));
            DebugLog("[GROUNDFIND] id=" + N3(j.id) + " FOUND shooter=" + GlXZ(j.line.ax, j.line.az) + " target="
                     + GlXZ(j.line.bx, j.line.bz) + " centre=" + GlXZ(j.cx, j.cz) + " dist=" + GlUM(j.dist) + " radius="
                     + GlUM(j.radius) + " tried=" + N3(j.tried) + " " + GlTally(j)
                     + " (playerteleport groundpick shooter|target moves this game's watched player to an end)");
            j.kind = 0;
            return;
        }
        const bool capped = (j.tried >= kGlFindMaxCandidates);
        if (capped || !GlAdvance(j))
        {
            ++g_glFindNone;
            DebugLog("[GROUNDFIND] id=" + N3(j.id) + " NONE_FOUND centre=" + GlXZ(j.cx, j.cz) + " dist=" + GlUM(j.dist)
                     + " radius=" + GlUM(j.radius) + " tol=" + GlUM(j.tol) + " tried=" + N3(j.tried)
                     + (capped ? " (stopped at the candidate cap " + N3(kGlFindMaxCandidates) + ")" : std::string(" (every spot in the radius)"))
                     + " " + GlTally(j));
            j.kind = 0;
            return;
        }
    }
}

static std::string GroundLineReportLine()
{
    return "[GROUNDLINE] REPORT lineJobs=" + N3(g_glLineJobs)
         + " lineVerdicts[clear,blocked,notLevel,notLoaded,busy,fault]=" + N3(g_glLineVerdict[kGlClear]) + ","
         + N3(g_glLineVerdict[kGlBlocked]) + "," + N3(g_glLineVerdict[kGlNotLevel]) + "," + N3(g_glLineVerdict[kGlNotLoaded]) + ","
         + N3(g_glLineVerdict[kGlBusy]) + "," + N3(g_glLineVerdict[kGlFault]) + " lineStopped=" + N3(g_glLineStopped)
         + " findJobs=" + N3(g_glFindJobs) + " found=" + N3(g_glFindFound) + " none=" + N3(g_glFindNone)
         + " findBusy=" + N3(g_glFindBusy) + " findStopped=" + N3(g_glFindStopped)
         + " running=" + N3(g_glJob.kind) + " candidates=" + N3(g_glCandidates)
         + " pick[id,reads,none]=" + N3(g_glPick.id) + "," + N3(g_glPickReads) + "," + N3(g_glPickNone)
         + " heightReads=" + N3(g_glHeightReads) + " heightNone=" + N3(g_glHeightNone) + " heightFault=" + N3(g_glHeightFault)
         + " traces=" + N3(g_glTraces) + " traceAborted=" + N3(g_glTraceAborted) + " traceFault=" + N3(g_glTraceFault)
         + " refused[usage,noRows,noWorld,busy,noChar,range]=" + N3(g_glRefusedUsage) + "," + N3(g_glRefusedNoRows) + ","
         + N3(g_glRefusedNoWorld) + "," + N3(g_glRefusedBusy) + "," + N3(g_glRefusedNoChar) + "," + N3(g_glRefusedRange);
}
void GroundLineReport() { DebugLog(GroundLineReportLine()); }

static bool GlNum(const std::string& w, float* v)
{
    std::istringstream ts(w);
    ts.imbue(std::locale::classic());
    float f = 0.0f;
    char c = 0;
    if (!(ts >> f) || (ts >> c) || _finite((double)f) == 0) return false;
    *v = f;
    return true;
}
/* the optional [tolUnits], then nothing more */
static bool GlTail(std::istringstream& is, float* tol)
{
    *tol = kGlTolDefault;
    std::string w;
    if (is >> w)
    {
        float t = -1.0f;
        if (!GlNum(w, &t) || !(t > 0.0f && t <= kGlTolMax)) return false;
        *tol = t;
        if (is >> w) return false;
    }
    return true;
}
static bool GlReadXZ(::Character* c, float* x, float* z)
{
    __try
    {
        const float* p = (const float*)((const char*)c + kRtCharPos);
        *x = p[0]; *z = p[2];
        return _finite((double)*x) != 0 && _finite((double)*z) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool GlCharXZ(unsigned int uid, float* x, float* z)
{
    ::Character* c = FindSpawned(uid);
    return c != 0 && GlReadXZ(c, x, z);
}
/* "" = the job may start */
static std::string GlRefuse(const char* verb)
{
    if (kGlTerrainFast == 0 || kGlTrace == 0)
    {
        ++g_glRefusedNoRows;
        return std::string("error ") + verb + ": the address rows UtilityT_groundHeight / UtilityT_trace are not loaded";
    }
    if (EngineWritesBlocked())
    {
        ++g_glRefusedNoWorld;
        return std::string("error ") + verb + ": no world is running here (loading, tearing down or not in gameplay)";
    }
    if (g_glJob.kind != 0)
    {
        ++g_glRefusedBusy;
        return std::string("error ") + verb + ": job id=" + N3(g_glJob.id) + " is still running";
    }
    return std::string();
}

// MAIN THREAD (the command channel). TEST-ONLY, READ ONLY. World units (10 a metre):
//   groundline <x1> <z1> <x2> <z2> [tolUnits]      the line from (x1,z1) to (x2,z2)
//   groundline uid <uidFrom> <uidTo> [tolUnits]    the line between two characters' positions (x,z only)
//   groundline report                              the counters line
// The answer is the [GROUNDLINE] id=N line on the next main tick (later while the engine refuses to trace).
std::string GroundLineLever(const std::string& args)
{
    const std::string usage = "error groundline: usage groundline <x1> <z1> <x2> <z2> [tolUnits] | groundline uid <uidFrom> <uidTo> [tolUnits] | groundline report";
    std::istringstream is(args);
    std::string first;
    if (!(is >> first)) { ++g_glRefusedUsage; return usage; }
    if (first == "report")
    {
        const std::string l = GroundLineReportLine();
        DebugLog(l);
        return "ok groundline " + l;
    }
    GlLine L;
    L.ax = L.az = L.bx = L.bz = 0.0f;
    float tol = kGlTolDefault;
    unsigned int ua = 0, ub = 0;
    if (first == "uid")
    {
        if (!(is >> ua >> ub) || ua == 0 || ub == 0 || ua == ub || !GlTail(is, &tol)) { ++g_glRefusedUsage; return usage; }
    }
    else
    {
        std::string w2, w3, w4;
        if (!(is >> w2 >> w3 >> w4) || !GlNum(first, &L.ax) || !GlNum(w2, &L.az) || !GlNum(w3, &L.bx) || !GlNum(w4, &L.bz)
            || !GlTail(is, &tol)) { ++g_glRefusedUsage; return usage; }
    }
    const std::string no = GlRefuse("groundline");
    if (!no.empty()) return no;
    if (ua != 0)
    {
        if (!GlCharXZ(ua, &L.ax, &L.az)) { ++g_glRefusedNoChar; return "error groundline: no character position for uid " + N3(ua); }
        if (!GlCharXZ(ub, &L.bx, &L.bz)) { ++g_glRefusedNoChar; return "error groundline: no character position for uid " + N3(ub); }
    }
    const float dx = L.bx - L.ax, dz = L.bz - L.az;
    const float len = sqrtf(dx * dx + dz * dz);
    if (!(len >= kGlLenMin && len <= kGlLenMax))
    {
        ++g_glRefusedRange;
        return "error groundline: the line is " + F1(len) + "u long; " + F1(kGlLenMin) + ".." + F1(kGlLenMax) + "u is accepted";
    }
    L.tol = tol;
    g_glJob = GlJob();
    g_glJob.kind = 1;
    g_glJob.id = ++g_glNextId;
    g_glJob.line = L;
    ++g_glLineJobs;
    return "ok groundline queued id=" + N3(g_glJob.id) + " from=" + GlXZ(L.ax, L.az) + " to=" + GlXZ(L.bx, L.bz) + " len="
         + GlUM(len) + " tol=" + GlUM(tol) + " (the [GROUNDLINE] id=" + N3(g_glJob.id) + " line follows on the main tick)";
}

// MAIN THREAD (the command channel). TEST-ONLY, READ ONLY. World units (10 a metre):
//   groundfind <x> <z> <dist> <radius> [tolUnits]     a shooter spot within radius of (x,z) and a target spot dist
//   groundfind uid <uid> <dist> <radius> [tolUnits]   from it, the line between them CLEAR (as groundline says)
// The answer is the [GROUNDFIND] id=N FOUND / NONE_FOUND / PHYSICS_BUSY line, a few ticks later.
std::string GroundFindLever(const std::string& args)
{
    const std::string usage = "error groundfind: usage groundfind <x> <z> <dist> <radius> [tolUnits] | groundfind uid <uid> <dist> <radius> [tolUnits]";
    std::istringstream is(args);
    std::string first;
    if (!(is >> first)) { ++g_glRefusedUsage; return usage; }
    float cx = 0.0f, cz = 0.0f, dist = 0.0f, radius = 0.0f, tol = kGlTolDefault;
    unsigned int uid = 0;
    std::string w1, w2, w3;
    if (first == "uid")
    {
        if (!(is >> uid >> w2 >> w3) || uid == 0 || !GlNum(w2, &dist) || !GlNum(w3, &radius) || !GlTail(is, &tol))
        { ++g_glRefusedUsage; return usage; }
    }
    else
    {
        if (!(is >> w1 >> w2 >> w3) || !GlNum(first, &cx) || !GlNum(w1, &cz) || !GlNum(w2, &dist) || !GlNum(w3, &radius)
            || !GlTail(is, &tol)) { ++g_glRefusedUsage; return usage; }
    }
    if (!(dist >= kGlLenMin && dist <= kGlFindDistMax) || !(radius >= 0.0f && radius <= kGlRadiusMax))
    {
        ++g_glRefusedRange;
        return "error groundfind: dist " + F1(kGlLenMin) + ".." + F1(kGlFindDistMax) + "u and radius 0.." + F1(kGlRadiusMax)
             + "u are accepted (got dist=" + F1(dist) + " radius=" + F1(radius) + ")";
    }
    const std::string no = GlRefuse("groundfind");
    if (!no.empty()) return no;
    if (uid != 0 && !GlCharXZ(uid, &cx, &cz)) { ++g_glRefusedNoChar; return "error groundfind: no character position for uid " + N3(uid); }
    g_glJob = GlJob();
    g_glJob.kind = 2;
    g_glJob.id = ++g_glNextId;
    g_glJob.cx = cx; g_glJob.cz = cz; g_glJob.dist = dist; g_glJob.radius = radius; g_glJob.tol = tol;
    g_glJob.pointsInRing = GlPointsInRing(0);
    ++g_glFindJobs;
    return "ok groundfind queued id=" + N3(g_glJob.id) + " centre=" + GlXZ(cx, cz) + " dist=" + GlUM(dist) + " radius="
         + GlUM(radius) + " tol=" + GlUM(tol) + " (the [GROUNDFIND] id=" + N3(g_glJob.id) + " line follows within a few ticks)";
}
// T-310 groundline / groundfind: END

void CombatTick()
{
    RangedEventsDrain();   /* P104: the shot / ranged-hit lines, main thread */
    RangedTestRecheckTick();   /* P104f1: the lever's +2 s re-read, main thread */
    /* T-311 fold 1: ShotReactWatchTick runs in ShotSafePointDrain now (the K2 safe point, worker paused - F617) */
    GroundLineTick();   /* T-310: the groundline / groundfind jobs, main thread */
    P112Drain(); P112WatchTick();   /* PROBE P112: the aim / shot / bolt lines, the watch POS lines */
    // Drain the queue the AI worker thread filled. MAIN THREAD ONLY - this is the side
    // that owns the transport.
    while (g_hitRead != g_hitWrite)
    {
        const PendingHit& h = g_hitQueue[g_hitRead & kHitQueueMask];

        // P012 (F113): log the first few emissions in full so `emitted` can be ATTRIBUTED
        // rather than believed. Bounded, because an ambient world produces these steadily
        // and an unbounded log would drown the run it is meant to explain. Main thread.
        if (g_emitLogged < kEmitLogLimit)
        {
            ++g_emitLogged;
            const float* d = (const float*)h.damages;
            std::stringstream ss;
            ss.imbue(std::locale::classic());
            ss << "[P012] emit #" << N3(g_emitLogged) << " uid=" << N3((long long)h.uid)
               << " victimObj=" << h.victimObj << " attackerObj=" << h.attackerObj
               << " cutDir=" << h.cutDir << " comboId=" << h.comboId << " damages=";
            for (int i = 0; i < 6; ++i) ss << (i ? "," : "") << d[i];
            // The decisive cross-check: does the pointer the ENGINE handed us still map to
            // this uid now, on a different thread, some frames later? A recycled address
            // would not survive this.
            ss << " recheckUid=" << N3((long long)FindSpawnedUid(h.victimObj));
            DebugLog(ss.str());
        }

        // F119: snapshot the victim's per-limb health HERE, on the main thread, after the
        // engine has already applied this hit locally - so what travels is the authority's
        // actual outcome, not a prediction of it. If several hits queued between drains
        // they all carry the latest state; the peer converges either way.
        // P016 (PARITY P-2), authority side. Resolving the identity we are about to send
        // through OUR OWN registry does two jobs at once: it proves the handle round-trips
        // where it was captured, and it gives a NAME to diff against the peer's line. The
        // attacker POINTER is not touched here - by now it may have been freed, and this
        // whole module already learned that lesson once (F117).
        if (ObjIdValid(h.attackerId) && g_idLogged < kIdLogLimit)
        {
            ++g_idLogged;
            Character* local = ResolveObjId(h.attackerId);
            DebugLog("[P016] out victim=" + N3((long long)h.uid) + " attackerId='"
                     + ObjIdString(h.attackerId) + "' localResolve="
                     + std::string(local != 0 ? "YES" : "NO") + " name='"
                     + (local != 0 ? local->getShownNameDirect() : std::string("<unresolved>")) + "'");
        }

        float rec[32 * kPartFloats];
        float blood = 0.0f;
        int nParts = SnapshotHealth(h.uid, rec, 32, &blood);

        const bool sent = net::SendHit(h.uid, h.attackerUid, h.cutDir, h.damages, h.comboId,
                                       rec, nParts, blood, h.attackerId, h.knock);
        if (sent && h.knock) ++g_knockSent;   // K2
        InterlockedExchange(&g_hitRead, g_hitRead + 1);
    }
}

void ReportCombat()
{
    std::stringstream ss;
    ss << "[M3] REPORT hitsSeen=" << N3(g_hitsSeen)
       << " emitted=" << N3(g_hitsEmitted)
       << " suppressed=" << N3(g_hitsSuppressed)
       << " puppetThrownRefused=" << N3(g_hitsPuppetThrown)
       << " applied=" << N3(g_hitsApplied)
       << " droppedQ=" << N3(g_hitsDropped)
       << " queueHigh=" << N3(g_hitQueueHigh) << "/" << N3(kHitQueueSize)
       << " noAttacker=" << N3(g_hitsNoAttacker)
       << " byHandle=" << N3(g_attackerByHandle)
       << " idMissed=" << N3(g_attackerIdMissed)
       << " noIdSent=" << N3(g_attackerNoIdSent)
       << " hitThread=" << N3(g_hitThreadId)
       << " hitPlay[played,skippedNoAnim,skippedNoAppearance,skippedAppearancePending,"
          "skippedNoEntity,skippedDetached,skippedAttacker]="
       << N3(g_hitPlayed)
       << "," << N3(g_hitPlaySkipped[kHitNoAnim])
       << "," << N3(g_hitPlaySkipped[kHitNoAppearance])
       << "," << N3(g_hitPlaySkipped[kHitAppearancePending])
       << "," << N3(g_hitPlaySkipped[kHitNoEntity])
       << "," << N3(g_hitPlaySkipped[kHitDetached])
       << "," << N3(g_hitPlaySkipped[kHitAttackerNotOrientable])
       // K2 (decision 61 follow-up): the knock byte and the safe point that applies it.
       << " knockSent=" << N3(g_knockSent)
       << " knockApplied=" << N3(g_knockApplied)
       << " knockSkippedGone=" << N3(g_knockSkippedGone)
       << " knockSkippedAlready=" << N3(g_knockSkippedAlready)
       << " knockSkippedRebuild=" << N3(g_knockSkippedRebuild)
       << " knockOverflow=" << N3(g_knockOverflow)
       << " knockNotPlayed=" << N3(g_knockNotPlayed)
       << " knockArmsFull=" << N3(g_knockArmsFull)
       << " knockPending=" << N3(g_knockPendingCount);
    DebugLog(ss.str());
    // Stated in words, because `noAttacker` sat in this line for six runs being read as a
    // safety statistic rather than as "how often a player watched damage happen to someone
    // with nothing hitting them" (F130).
    DebugLog("[M3] PARITY P-2: hits that landed with NO VISUAL on this screen = "
             + N3(g_hitsNoAttacker) + "; rescued by engine handle = "
             + N3(g_attackerByHandle));
    ReportPhysicsGuard();   // T-211 / H054: beside the knock counters it exists for
    RangedReport();         /* P104: the ranged shot / hit counters */
    GroundLineReport();     /* T-310: the groundline / groundfind counters */
}

void InstallCombatHook()
{
    DebugLog("[M3] resolving Character::hitByMeleeAttack ...");
    intptr_t fn = (intptr_t)coop::AddrAbs(kMig3HitByMeleeAttack);
    if (fn == 0)
    {
        ErrorLog("[M3] hitByMeleeAttack resolved to 0 - combat hook NOT installed");
        return;
    }

    coop::HookStatus st =
        coop::AddHook((void*)fn, (void*)&detour_hitByMelee, (void**)&orig_hitByMelee);
    DebugLog(st == coop::SUCCESS ? "[M3] hitByMeleeAttack AddHook SUCCESS"
                                      : "[M3] hitByMeleeAttack AddHook FAILED");

    InstallRangedProbe();      /* P104: GunClass::shoot + Character::iShotYou, counting only */
    InstallEffectHook();       /* T-327: Character::gettingEaten - a copy eaten by our eater is a request to its owner */
    InstallKnockSafePoint();   // K2: the copy side's safe point for its knock
    InstallPhysicsGuard();     // T-211 / H054: the AI-thread box query and the physics-thread actor release never overlap
}


// ===========================================================================================
// P-15 / F220 - COMBAT MODE replication. See combat.h for why this replicates the CAUSE.
// ===========================================================================================

namespace {

// The two CombatClass methods this section calls (declared in game/CombatClass.h, defined in
// gamecalls.cpp through their address-table rows - see the P-15 / F220 note at the top of this file):
//
//   ?currentTarget@CombatClass@@QEBA?AVhand@@XZ         -> real RVA 0x33A2A0
//   ?enterCombat@CombatClass@@QEAA_NAEBVhand@@H_N@Z -> real RVA 0x664F20
//
// `initCombatMode(target, end, focusedTarget)`, from its decompile:
//   end == 0 -> START:  sets combatModeActive (+0x130) = 1, stores the target hand,
//                       changes state to DECISION, returns 1
//   end == 2 -> END:    sets +0x130 = 0, clears the block, returns 0
//   otherwise -> no-op, returns 1
// Range + alignment + a vtable pointer inside the game image. Same rule as everywhere else in
// this project (F051): every engine pointer is validated before use.
bool PlausibleObject(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    uintptr_t vtable = 0;
    __try { vtable = *(uintptr_t*)v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

// Confirmed by DISASSEMBLY rather than by header: `_isInCombatMode` at 0x4400B0 is
// MOVZX EAX,byte ptr [RCX+0x130] / RET. It is also the exact byte the portrait reads (F204).
const size_t kCombatModeActiveOff = 0x130;

// The rest of the CombatClass fields the swing path touches.
//
// **F351 - TWO OF THESE WERE ONCE GUESSED, AND BOTH GUESSES WERE WRONG.** F348 derived five of
// them from decompiles; the two it did not derive were checked field by field afterwards. F037's rule
// ("resolve every address") applies to STRUCTURE FIELDS as much as to functions.
const size_t kCombatStateOff   = 0x1F0;  // swordStateEnum combatState  - what it is DOING now
const size_t kNextMoveOff      = 0x1F4;  // swordStateEnum nextMove     - what STARTUP will begin
const size_t kTechniqueOff     = 0x150;  // CombatTechniqueData* currentTechnique, 0 = none
const size_t kAnimationOff     = 0x180;  // AnimationClass* animation
// `lektor<Character*> threats` is at 0x220 and `lektor` is {allocator(8), count, maxSize, T*} =
// 0x18 bytes (confirmed twice by the header's own strides: 0x1F8->0x210 and 0x220->0x238). So this
// is `threats.count`, a THREAT TALLY - **not the "cannot act latch" the first version of this code
// called it and gated the whole feature on.**
const size_t kThreatCountOff   = 0x228;
// `float MEI_MIN` / `float MEI_MAX` - the engagement-distance BAND, not a weapon reach. Printed as
// a pair, because one bound of a range on its own cannot say whether a gap is inside it.
const size_t kMeiMinOff        = 0x278;
const size_t kMeiMaxOff        = 0x27C;
const size_t kMovementOff      = 0x170;  // CharMovement* movement
const size_t kTargetOff        = 0x290;  // Character* currentTarget (its handle is at +0x298)
// `bool targetTraceBlocked` - and it is LOAD-BEARING, not decoration. `CombatClassAI::startupState`
// converts `nextMove/STARTUP` into **`combatState = 10` (SWORD_APPROACH_START)** whenever this
// is set, i.e. it turns an injected swing into the puppet WALKING somewhere under its own engine.
// That is the outcome class that cost T071/T075/T076 their runs, reached by a different door.
const size_t kTargetTraceBlockedOff = 0x2C0;
// `CharMovement::combatMover` is at CharMovement+0x118 and `CombatMovementController::hasForcedWP`
// is at +0x50 of that, so CharMovement+0x168 is `hasForcedWP` (both from the shipped headers).
// The engine's own attack branch refuses to start a swing while it is set, and
// `CombatClassAI::update` returns outright on it.
const size_t kHasForcedWPOff   = 0x168;  // relative to CharMovement, not CombatClass

// ---------------------------------------------------------------------------------------------
// P045 / T095 - THE TWO TICK COUNTERS THE ENGINE MAINTAINS. **SECONDARY. NOTHING CONCLUDES FROM
// THESE, AND THE DESIGN ARGUED FOR IN THE NEXT SEVENTY LINES WAS ABANDONED - READ TO THE END.**
//
// The primary measurement is the detour on 0x60CC10 that COUNTS THE CALLS (`g_updSlots`,
// `detour_ccUpdate`). The argument below is kept because it is why these two fields are still read
// at all, and because the reason it FAILED is the most useful thing in this file - but a reader who
// skims the first paragraph and stops will take an abandoned design for the current one, which in a
// project that reasons from its own comments is a defect in the comment.
//
// T094 left one candidate untested and it is the leading one: *the peer's combat state machine may
// never be ticked at all.* F220's 22,625 calls - the measurement behind "the controller is running
// and is simply never told to fight", load-bearing since T066 - counted
// `CombatClass::periodicTick` (0x60C9E0), which only ages out attack slots. **A different
// function drives the states, and no probe here has ever counted it.**
//
// The obvious way to count it is a detour, the way P026 counted periodicUpdate. It is not needed,
// because both update functions write a field on their way past, and both fields are named by the
// shipped header:
//
//   `CombatClass::update` (0x60C3A0), the BASE, near the top and unconditionally:
//       stateTimer (+0x14C, float) -= frameTIME
//   `CombatClassAI::update` (0x60CC10), the DERIVED, unconditionally and before any exit:
//       targetLastChangedTimer (+0x2F0, double) += frameTIME
//   verified at the INSTRUCTION level, because the decompile's
//   `param_1[0x5e] = (longlong)((double)param_2 + (double)param_1[0x5e])` could have been an
//   integer-truncating accumulator, which would have broken the measurement outright. It is not:
//       cvtps2pd xmm0, xmm7 / addsd xmm0, [rdi+0x2f0] / movsd [rdi+0x2f0], xmm0
//
// **Reading whether those values CHANGE between two frames is a direct measurement that the
// function ran** - not a proxy for it, and not an inference from state. T065 tried to answer this
// question from `frameTIME` (+0x168) and got 0.0 at all 197 samples on BOTH instances, including an
// authority where combat unambiguously ran: a zero that was exercised and still carried no signal.
// These two are different in kind, because the writes are in the decompile of the functions
// themselves.
//
// **THE AI FIELD IS A SAWTOOTH, NOT A CLOCK, AND THAT IS WHY THE TEST IS "CHANGED" AND NOT
// "INCREASED".** An exhaustive scan of the image for stores to +0x2F0 found four writers on
// `CombatClassAI`: the constructor (initialises to 0), `update` (the accumulate above),
// `chooseAttackTarget` 0x666450 (**resets to 0** when it re-evaluates), and `enterCombat`
// 0x667750 (**resets to 0** when its third argument is 0). `update` itself can call
// `chooseAttackTarget` a few instructions BEFORE the accumulate, so a single tick can both reset
// and add. A test written on "the number went up" would have read a reset as "did not run".
//
// Two consequences to carry into the reading, neither of which a direction test would have:
//   * **0.0 on a single sample is ambiguous** - a fresh object and one that has never ticked look
//     identical. Only the CHANGE between two consecutive frames is evidence, which is what is
//     counted here, and `ccSamples` is the denominator that makes a low ratio visible instead of
//     mistakable for a zero.
//   * a reset landing exactly between two samples could in principle reproduce the earlier value
//     and read as frozen. `frameTIME` is a measured float, so an exact double collision is
//     vanishingly unlikely - and it would depress the ratio rather than zero it, on the control
//     character as much as on the puppet.
//
// **THE PAIR IS WHAT MAKES IT A LOCALIZATION, and either alone would not be.** 0x60CC10 calls
// 0x60C3A0 at its tail, and the dispatch that matters -
//     `if (combatState == 3) (*(vtable + 0x50))(this)`   i.e. startupState, the one our injected
// swing depends on - sits in 0x60CC10 **AFTER** that call. So:
//
//   both change      -> the whole AI chain ran, and the state-3 dispatch executed
//   only +0x14C      -> something ticks the BASE directly and the derived never runs, so the
//                       state-3 dispatch NEVER HAPPENS however correct our writes are
//   only +0x2F0      -> the derived RAN and took its early return. 0x60CC10 bails outright when
//                       `CharMovement::combatMover.hasForcedWP` is set, and that return is BEFORE
//                       the call to the base and before every tail dispatch - so the AI timer
//                       moves, the base timer does not, and no state is dispatched at all. A third
//                       distinguishable outcome, and `TrySwing` already refuses to write in it
//                       (SW_GATE_FORCED_WP), so seeing this is a self-consistency check as well.
//   neither changes  -> nothing ticks this combat object at all
//
// Compare against direction, not magnitude: `changeState` also writes stateTimer and the AI timer
// may be reset elsewhere, so a decrease or a jump is still evidence the code RAN. **The test is
// "did the bits change", on the raw bits, so a NaN cannot read as "unchanged" through an == that
// is false for everything.**
//
// COST OF BEING WRONG, AND WHAT CATCHES IT: if either field is not what the header says, the
// control catches it in the same run - **an OWNED character's counts are the control**, and an
// owned character whose combat unambiguously runs must show both ticking. If the control reads
// frozen too, the instrument is wrong and the detour is the follow-up. That is why the control is
// a different CHARACTER and not a switch position (T094's `LANDED` control was 0 by arithmetic).
// ---------------------------------------------------------------------------------------------
// **AND THE PARAGRAPHS ABOVE ARE WHY THIS PROBE IS A DETOUR AFTER ALL. THE FREE COUNTER IS BLIND
// IN EXACTLY THE POPULATION UNDER TEST, AND ITS CONTROL CERTIFIES THE BLINDNESS.**
//
// `chooseAttackTarget` (0x666450) opens like this:
//
//     target = currentTarget;
//     if (target != 0) {
//         if (targetLastChangedTimer < THRESH && ...) return target;   // early out, NO reset
//         if (targetLastChangedTimer < THRESH2 && ...) return target;  // early out, NO reset
//     }
//     target = 0;
//     targetLastChangedTimer = 0;                                     // <== UNCONDITIONAL
//
// **Both early outs that skip the reset are inside `if (currentTarget != 0)`.** And
// `CombatClassAI::startupState` (0x60C620) calls it precisely when `currentTarget == 0`:
//
//     if (currentTarget == 0) { t = chooseAttackTarget(); setAttackTarget(t); ... }
//
// So a puppet in **state 3 with a null currentTarget - which is 16 of 16 of T094's decline lines,
// the measured population this whole run is about** - does `0 -> 0+dt -> 0` every single frame,
// and a once-per-frame sampler reads exactly 0.0 forever. `aiTicked` would have stayed 0 and the
// banner would have printed THE AI UPDATE NEVER RAN, **which is the answer this study is looking
// for, manufactured by the update having run.**
//
// **AND THE CONTROL COULD NOT HAVE CAUGHT IT.** An owned character has a non-null `currentTarget`,
// takes the early out, is never reset, and ticks normally. The control passes, and its passing is
// what would have made the false reading believable. That is `measure-the-right-LAYER` in its worst
// form: not a number that is wrong, but a whole instrument that is blind, agreeing with itself.
//
// A second, independent objection lands on the same design: both fields move by `+/- frameTIME`,
// and T065 measured the argument `update` is called with (`+0x168`) as **0.0 at 197 of 197 samples
// on both instances**. If that reading is sound, both writes are bit-preserving no-ops and every
// character reads frozen. Two unrelated ways for a proxy to be silent is one too many.
//
// So the primary measurement is now what the question actually asks: **COUNT THE CALLS.** The two
// fields are still read, because they cost nothing and disagreement between them and the call count
// is itself informative - but nothing concludes from them, and no banner keys off them.
const size_t kStateTimerOff        = 0x14C;  // float  stateTimer            - written by the BASE
const size_t kTargetLastChangedOff = 0x2F0;  // double targetLastChangedTimer - written by the AI
                                             //        (CombatClassAI only - needs the isAI guard)

// P046 / P047 - `hand currentTargetHandle` at +0x298 and `hand focusedTarget` at +0x2C8.
//
// A `hand` is 0x20 bytes: **a VPTR at +0x0** then five uint32 at +0x8..+0x18. Confirmed by TWO
// independent strides - 0x298 + 0x20 = 0x2B8, which the header gives as the end of CombatClass; and
// 0x2C8 + 0x20 = 0x2E8, which the header names `reFocusableMode`. Corroborated a third time by
// `setTarget`'s own decompile, which writes 0x2A0/0x2A4/0x2A8/0x2AC/0x2B0.
//
// The first version of this comment called +0x0 "a container pointer", which is wrong: `hand.h`
// declares `virtual bool operator==`, so it is a vtable pointer, and the setter's early-return
// guard `(**(code**)(*(longlong*)(this+0x298) + 8))(...)` is **`hand::operator==`** dispatched
// through it. The offsets used below were all correct; the NAME was not, and F037's rule is about
// naming what an address actually is rather than about getting the arithmetic right.
const size_t kTgtHandTypeOff      = 0x2A0;
const size_t kTgtHandContOff      = 0x2A4;
const size_t kTgtHandContSerOff   = 0x2A8;
const size_t kTgtHandIndexOff     = 0x2AC;
const size_t kTgtHandSerialOff    = 0x2B0;
const size_t kFocHandTypeOff      = 0x2D0;
const size_t kFocHandIndexOff     = 0x2DC;
const size_t kFocHandSerialOff    = 0x2E0;
const size_t kReFocusableOff      = 0x2E8;
// The five fields `setTarget` COPIES FROM, on the Character. Read from its decompile
// (`+0x2A0 = *(u32*)(target+0x60)` and so on), not guessed - which is the whole of F037's rule
// applied to a field rather than to an address.
const size_t kCharHandTypeOff     = 0x60;
const size_t kCharHandIndexOff    = 0x6C;
const size_t kCharHandSerialOff   = 0x70;
// The engine's own null-hand marker. `setTarget(NULL)` writes 0xB into the type, and
// `CombatClassAI::update` tests `focusedTarget.type != 0xB` before it will use the focused
// target at all. Two sites, same constant, so a handle reading 0xB was NEVER WRITTEN rather than
// written badly - and those need opposite fixes.
const int    kNullHandType        = 0xB;
// And the only two types the resolver will act on. Disassembled: 0x7974F0 reads the handle's type,
// compares it against 1 and against 0x5B, and for **anything else does `xor eax,eax; ret`** - so a
// handle whose type is neither resolves to null by construction, not by failing to find anything.
// `currentTarget` is therefore a DERIVED value that only exists while a code path re-resolves it.
const int    kCharacterHandType   = 0x1;

// ---------------------------------------------------------------------------------------------
// P050 / T096 - **THE CURRENT TASK, WHICH F367 SAYS IS THE ACTUAL GATE ON THE WHOLE MACHINE.**
//
// **Denominator for all three task counters is `ccSamples - ccNoBodyFrames`**, not `ccSamples`:
// they increment inside the body-plausible branch. (`ccGoFtNonZero` and the P054 target counters do
// NOT - they are off `cc`, not `body`.) Harmless while `noBody` is 0, which is expected - **and if
// it is ever non-zero, that is precisely when a reader needs this right.**
//
// The call graph has only one route into `CombatClassAI::update`, and it is not a per-frame
// tick. `Character::threadedUpdate` -> `CharBody::update` -> **whatever Tasker is installed
// at CharBody+0x68** -> (only if that is a melee task) `CombatClass::go` -> `update`.
// `update` has no code callers at all, `go` is its only dispatcher, and `go`'s only two
// dispatchers are `Task_MeleeAttack::update` and `Task_FocusedMeleeAttack::update`.
//
// **So the machine runs only while a melee-attack TASK is the current action** - and T095's own
// host data says so independently: one owned character sat at 948 calls while `inCombatFrames`
// climbed to 93,211. Combat mode is not the gate. The task is.
//
// This counts the frames where it is true, per character, on both instances. **`anyTask` is
// counted separately from `meleeTask` on purpose**: "no task at all" and "a task that is not a
// melee attack" are different situations with different fixes, and one counter for both would hide
// the first inside the second - the F351 shape.
const size_t kCharBodyOff      = 0x648;   // Character -> CharBody (the same field CombatOf checks)
const size_t kCurrentTaskOff   = 0x68;    // CharBody  -> Tasker* current action
// P053 / F375 - **THE PRECONDITION THAT DECIDES WHETHER INSTALLING A TASK IS WORTH BUILDING AT
// ALL, AND IT COSTS ONE FIELD READ.**
//
// `CharBody::update` opens with `if (getSquad() == NULL) return;` - **before it even writes
// `frameTIME`**, and long before it dispatches the current action. So a puppet whose update returns
// early would never run an installed task no matter how correctly we installed it, and the entire
// direct-install approach would be pointless.
//
// **THE OBVIOUS TEST IS WRONG, AND IT IS WRONG IN THE DIRECTION THAT ENDS THE WORKSTREAM.**
// "Did `frameTIME` CHANGE between frames" looks like the natural probe. It is not:
//
//   * `CharBody+0x60` is an **assignment of the frame delta, not an accumulator** -
//     `movss [rbx+0x60], xmm6` where xmm6 is the incoming `_time`;
//   * that argument is `Character::frameTIME` (+0xC4) = `g_dt + offscreenFrameTime`;
//   * and **`g_dt` is ceiling-clamped to 0.05** (`if (g_dt > 0.05f) g_dt = 0.05f`, constant at RVA
//     0x1695C74). **Below 20 FPS it is bit-identical frame after frame.**
//
// **This study runs TWO Kenshi instances on one machine.** So "never changed" is exactly what a
// perfectly-running update looks like under load, and it is the reading that would have been acted
// on as *the approach is dead*. Same shape as F361: a proxy reading zero for a reason that has
// nothing to do with the question.
//
// **THE SOUND TEST IS "IS IT NON-ZERO".** `update` writes it with the frame delta. **Two claims
// that used to sit here have since been read rather than asserted, and both were wrong as written:**
//
//   * *"the delta is always > 0"* - **F379: the clamp has TWO CEILINGS AND NO FLOOR**, so
//     `g_dt == 0.0f` is not structurally excluded. The LATCH survives it (one non-zero write in the
//     whole window is enough), so it fails only under a **full pause** - which is why the test plan
//     carries "do not pause" as a procedural constraint rather than this comment carrying a claim.
//   * *"the field is 0 at construction"* - **F380: `CharBody::create` never touches +0x60.** Its
//     only 0x60 displacements are `[rsp+0x60]` stack spills, so the initial value comes from the
//     ALLOCATION and a recycled block can hold something plausible. That fails toward a false
//     NON-zero - *"go build it"* - which is the recoverable direction. **`lastBodyFtBits` and
//     `bodyFtMoved` discriminate it:** a stale value is frozen and arbitrary, a live one either
//     varies or sits on 0x3d4ccccd.
//
// So:
//
//   bodyFtNonZero > 0  -> the update HAS run for this character. Precondition satisfied.
//   bodyFtNonZero == 0 -> the field is still its constructed 0. It has never run. **Dead.**
//   bodyFtMoved        -> kept as colour: it separates "runs with a varying dt" from "runs pinned
//                         at the 0.05 clamp", and it is NOT the decision.
//
// The last observed value is printed too, because `0.05` and `0.0` are the two readings that
// matter and a reader should not have to infer which one produced a zero counter.
const size_t kCharBodyFrameTimeOff = 0x60;   // float  CharBody::frameTIME - written by update
// Verified from the shipped exe: slot +0x10 of each resolves through its thunk to 0x34C9E0 and
// 0x33C1F0, and RTTI names them `Task_MeleeAttack` and `Task_FocusedMeleeAttack`. An RTTI sweep of
// all 3,055 complete-object-locators found **no third vtable** reaching either body, so the
// equality test cannot miss a derived task.
//
// **COUNTED SEPARATELY, BECAUSE ONLY ONE OF THEM ACTUALLY DISPATCHES `go`.**
// F367 said both did. That came from attributing an indirect call at **0x34CB18** to
// `Task_MeleeAttack::update` - and `.pdata` gives that function as **0x34C9E0..0x34CAF0**, so
// 0x34CB18 belongs to the *next* function (0x34CAF0..0x34CC77), which is not a vtable target at
// all. `Task_MeleeAttack::update`'s own 272 bytes contain no `[reg+0x18]` call; it reads like the
// approach half. **`Task_FocusedMeleeAttack::update` is the only task body that calls
// `CombatClass::go`.**
//
// **This is F037's second clause again - resolve the address, then ask which FUNCTION'S BODY it is
// in - and it is the third time this session that a boundary error nearly became a finding.** One
// merged counter would have made `meleeTaskFrames=N, updCalls=0` unreadable: N frames of the
// variant that never calls `go` is not the same result as N frames of the variant that does.
static unsigned long long kVtblMeleeAttack = 0; static coop::AddrReg kVtblMeleeAttack_reg("VtblMeleeAttack", &kVtblMeleeAttack);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x16BD448 */
static unsigned long long kVtblFocusedMeleeAttack = 0; static coop::AddrReg kVtblFocusedMeleeAttack_reg("VtblFocusedMeleeAttack", &kVtblFocusedMeleeAttack);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x16BE9E8 */

// P051 - `CombatClass+0x168`, the frameTIME `go` stores on its way past.
//
// **THIS IS HERE TO ARGUE AGAINST F367, WHICH IS WHY IT SHIPS IN THE SAME BUILD.** If the chain is
// right, a host character whose `updCalls` is rising must have a non-zero value here. **F215
// measured this field as 0.0 at all 197 samples on BOTH instances, including the authority during
// live combat** - so either that probe read the wrong thing (it already called the field "dead as
// a signal"), or `go` is not running on the host either and those 9,275 calls arrive by some
// route the call-graph walk missed. One counter settles which, for free, in a run being taken
// anyway. `ship-probes-with-fixes`, pointed at a finding instead of a fix.
const size_t kGoFrameTimeOff   = 0x168;   // relative to CombatClass (NOT CharMovement's +0x168)

// Per-uid last known combat mode on the authority, so only EDGES are sent.
//
// SIZED TO THE MIRROR, and the previous comment ("the mirror holds at most 64") became false the
// moment kMaxMirror grew to 512 for P-16 Option A. That is exactly the defect F267 was written
// about, relocated: raising one cap and not the tables sized against it would have made combat-mode
// replication silently stop at the 65th character, with NO log line to find it by, while everything
// else about that character kept working. Keep this equal to kMaxMirror.
const int kMaxCombatWatch = 4096;   // T-354: kMaxMirror is 4096 (mirror1 (crash T487) made it 2048)
struct CombatWatch
{
    unsigned int uid;
    int          lastOn;
    unsigned int lastTarget;
    int          lastState;    // P027/F223 - swordStateEnum at the previous frame
    int          lastPeerMode; // P030/F246 - combatModeActive as the PEER last saw it
    int          weWantOn;     // what the authority last told us: 1 = fight, 0 = stop
    // H015 / P059
    int          nativeOn;         // 1 while this puppet's AI is released for the fight
    long long    nativeOpenTick;
    long long    nativeSwings;     // SWORD_SWING entries inside the current window
    unsigned int nativeTarget;
    float        nativeOpenDrift;  // combat2 (F920): horizontal gap to the owner when the window opened (-1 unknown)
    // F416 - the body's frame-to-frame visible displacement while in combat, on BOTH instances. On the
    // authority it is the baseline the peer's no-hop oracle compares against (the game's own knockback
    // and going-down motion is not a hop); emitted as a host_move record on the OFF edge.
    float mvLastX, mvLastY, mvLastZ;
    int   mvPrimed;
    float mvVisMax;      // 3D, per frame
    float mvVisMaxRate;  // units/s, STANDING frames
    float mvVisMaxRateDowned;  // units/s on prone/ragdoll frames
    double mvLastAt;
    long long mvFrames;

    // P041 / F348 - THE SWING OUTCOME, tracked per uid on the PEER.
    //
    // `AttackState::initialiseAttack` fails SILENTLY when no technique matches the current gap:
    // it returns 0, `startupState` leaves the state at 3, and nothing anywhere says why. That is
    // the one precondition of this fix we cannot satisfy by construction, so the build that
    // ships it also ships the instrument that tells the three outcomes apart -
    // "we never wrote", "we wrote and the engine declined", "the swing played"
    // (`ship-probes-with-fixes`). Without it a run that produced no swings would be
    // indistinguishable from a run where the message never arrived.
    // F351 - a SEPARATE pending flag, not a zero sentinel on the tick. `g_cmPulseTick` starts at 0,
    // so a swing applied before the first CombatModeTick recorded as "nothing pending" and was
    // never scored - the same class of defect as F333's sentinel collision, in a build whose whole
    // justification is that its instrument is trustworthy.
    int          swingPending;
    long long    swingWroteTick;   // tick at which we wrote nextMove=0/state=STARTUP
    unsigned int swingTarget;      // who that write was aimed at, for the decline diagnostic
    int          swingReported;    // 1 once a decline has been printed for this uid (print once)

    // F354 - the INTENT, retried every frame until a gate opens or the window closes.
    int          swingIntent;
    long long    swingIntentTick;
    unsigned int swingIntentTarget;
    int          swingIntentGate;  // the gate that was closed at the LAST attempt
    // F358 - the tick of the last ACTUAL attempt. Expiry is driven by the global tick, but an
    // intent is only SERVICED on ticks where the mirror yields the puppet and its CombatClass
    // reads - so without this, an intent that expired because the puppet was invisible would be
    // booked to whichever gate happened to be shut the last time anyone looked, minutes ago.
    long long    swingIntentLastTry;

    // P045 / T095 - IS THIS CHARACTER'S COMBAT STATE MACHINE BEING TICKED?
    //
    // Sampled ONLY on frames where this character is in combat mode, and that restriction is the
    // measurement rather than a convenience: outside combat the engine has no obligation to tick a
    // combat class at all, so frozen samples from outside combat would dilute the one population
    // the question is about (T094's declines all happened in combat mode) into noise. `ccSamples`
    // is printed beside the tick counts so the denominator is visible and "frozen" and "never
    // looked at" cannot be confused - which is the shape of defect F220 turned out to be.
    //
    // Stored as RAW BITS, compared with ==, so the test is "did these bytes change" and not a
    // float comparison. A NaN in either field would otherwise read as CHANGED on every frame
    // through an == that is false even against itself, and would fabricate the pass.
    long long    ccSamples;       // frames sampled while in combat mode
    long long    ccBaseTicks;     // ... of those, frames where stateTimer (+0x14C) changed
    long long    ccAiTicks;       // ... of those, frames where targetLastChangedTimer (+0x2F0) changed
    long long    ccAiUnreadable;  // ... of those, frames where isAI was false so +0x2F0 was not read
    unsigned int ccLastStateTimerBits;
    unsigned long long ccLastAiTimerBits;
    int          ccPrimed;        // 0 until the FIRST sample, which has no predecessor to compare
                                  // against and must therefore be counted as neither ticked nor
                                  // frozen. Without this every character contributes one free
                                  // "frozen" and a character seen twice reads 50% frozen.
    // **THE SAMPLE THIS ONE IS COMPARED AGAINST MUST BE THE PREVIOUS FRAME, NOT THE PREVIOUS
    // SAMPLE, AND THOSE ARE NOT THE SAME THING.** Sampling stops whenever the character leaves
    // combat mode or the mirror stops yielding it, and the timers keep moving while we are not
    // looking - so the first sample after any gap would compare against bits from seconds ago and
    // be counted TICKED almost regardless of the truth. A false "ticked" on a puppet is precisely
    // the reading that would wrongly close this investigation, so a gap re-primes instead of
    // comparing. Same shape as F356's "age first, attempt second": the stale comparison is the
    // defect, not the stale value.
    long long    ccLastSampleTick;
    const void*  ccLastObj;       // and the same argument for the OBJECT: a character that streams
                                  // out and back can be rebuilt at a different address, and the
                                  // bits of a different object are not a predecessor of these.
    long long    ccReprimed;      // discarded samples. Printed, because a uid whose reprimes swamp
                                  // its samples was never observed on two consecutive frames and
                                  // its ratio is not a measurement of anything.

    // **THE PRIMARY MEASUREMENT.** Calls to `CombatClassAI::update` for this character's combat
    // object, counted by the detour. Immune to every reset path that makes the two timer fields
    // blind, and immune to `frameTIME == 0`, because it counts the CALL rather than an effect of
    // the call.
    void*        ccUpdSlot;       // UpdSlot* for the CURRENT combat object (main thread only)
    long long    ccUpdCarried;    // calls banked from PREVIOUS combat objects for this uid, so a
                                  // character that streams out and is rebuilt does not silently
                                  // restart its count at zero and read as never ticked.
    long long    ccUpdAtPrime;    // the slot's count when we first saw THIS object, so the figure
                                  // reported is calls-while-we-were-watching rather than calls
                                  // since the object was born.
    long long    ccUpdLastSeen;   // and the slot's count at the LAST frame we saw it. Reported
                                  // instead of the live slot, so a retired uid cannot go on
                                  // accruing a stranger's calls through a reused address.
    // **HOW MANY TIMES THIS uid SWITCHED COMBAT OBJECT, AND WHY IT HAS TO BE PRINTED.**
    //
    // `updCalls` can only follow ONE slot at a time, and every switch re-snaps the baseline. For a
    // character rebuilt once by streaming that is right - we were not watching, so we do not claim
    // those calls. But `MirrorAdd` dedups on the OBJECT, not the uid, so **two mirror rows can
    // share a uid and hand this record a different object on alternate ticks** - and then every
    // visit re-snaps, every interval is discarded, and `updCalls` reads **0 forever while the
    // engine ticks normally.** That is the study's expected answer, produced by our own bookkeeping.
    //
    // It cannot be fixed by following both slots without a per-uid slot set, so it is **made
    // visible instead**: past a couple of rebinds the row stops reporting a number and says it is
    // unmeasurable. A row that refuses to answer is recoverable; a row that answers 0 is not.
    long long    ccUpdRebinds;

    // P050 / P051 - see kCharBodyOff and kGoFrameTimeOff. Same denominator as ccSamples.
    long long    ccFocusedTaskFrames; // ... where it was Task_FocusedMeleeAttack - THE ONLY TASK
                                      //     BODY THAT DISPATCHES CombatClass::go. This is the
                                      //     number F367 stands or falls on.
    long long    ccMeleeTaskFrames;  // ... where it was Task_MeleeAttack, which does NOT dispatch
                                     //     go. Non-zero here with updCalls=0 is CONSISTENT, not a
                                     //     contradiction - which is exactly why it is not merged.
    long long    ccAnyTaskFrames;    // ... where there was any current action at all
    long long    ccNoBodyFrames;     // ... where the CHARACTER or its CharBody would not read.
                                     // **NOT the task pointer** - an unreadable task falls through
                                     // to neither task counter and shows up in `noTaskFrames`.
                                     // Expect 0: `CombatOf` validated both on this same frame and
                                     // thread a moment earlier, so a non-zero here means something
                                     // changed underneath us, and a ZERO here proves nothing.
    // P053 - see kCharBodyFrameTimeOff. **Denominator is ccSamples MINUS ccNoBodyFrames**:
    // these live inside the body-plausible branch. (`ccGoFtNonZero` below does NOT - it is off
    // `cc`, not `body`, so its denominator really is ccSamples. Two counters, two denominators,
    // printed on one line - which is why both are stated rather than implied.)
    // **THE DECISION COUNTER.** Frames where CharBody+0x60 was NON-ZERO, i.e. `update` has
    // written it at least once. Zero here across a long run is what kills the direct-install
    // approach; see kCharBodyFrameTimeOff for why "changed" is NOT the test.
    long long    ccBodyFtNonZero;
    long long    ccBodyFtMoved;      // frames where it CHANGED. Colour, not the decision: the
                                     // engine clamps its delta to 0.05, so a machine running two
                                     // instances can hold this at 0 with the update running fine.
    unsigned int ccLastBodyFtSeen;   // the last raw value, printed as BITS so 0x0 (never written)
                                     // and 0x3d4ccccd (pinned at the 0.05 clamp) are unmistakable
    int          ccBodyFtPrimed;     // its OWN prime flag - see the body block. Sharing `ccPrimed`
                                     // meant sharing a flag that is provably 1 there.
    unsigned int ccLastBodyFtBits;   // raw bits, same reason as the other timer comparisons
    // P054 / F375(c) - **HOW OFTEN DOES THE ENGINE CHURN ITS OWN COMBAT TARGET?**
    //
    // `Task_FocusedMeleeAttack`'s vtable +0x28 (0x345BB0) runs EVERY FRAME as the tail of its
    // `update`. It reads the character's enemy lists and can call `setTarget` with a different
    // candidate - **or with NULL.** That is autonomous target selection INSIDE the task, and the
    // zero-decision-passes argument that protects the planned fix from T071/T075/T076 **does not
    // reach it.** F375 named it the failure most likely to bite.
    //
    // **It can be quantified NOW, for free, on the HOST** - which already has characters running
    // focused-melee tasks. Counting how often `currentTarget` (+0x290) changes under a running task
    // measures the churn the fifth attempt would have to live with, **before a line of it is
    // written.** Pure reads of a field this function already reads.
    long long    ccTargetChanges;    // frames where currentTarget differed from the previous sample
    long long    ccTargetCleared;    // ... of those, where it became NULL (the branch F375 flagged)
    const void*  ccLastTarget;
    int          ccTargetPrimed;     // its own flag - see F378 on sharing ccPrimed
    long long    ccGoFtNonZero;      // ... where CombatClass+0x168 (go's frameTIME) was non-zero.
                                     // **A LATCH, NOT A RATE.** Nothing clears +0x168 - `go` only
                                     // ever writes it - so once `go` runs with a non-zero dt this
                                     // climbs on every later sample whether or not `go` ran again.
                                     // Printed beside `inCombatFrames` it would read as a rate and
                                     // is not one. **Only the ALWAYS-ZERO direction carries
                                     // information**, which is the direction P051 exists for.
    // P056 - see WeaponInHandsOf. **Denominator is `ccSamples`**, not `ccSamples - ccNoBodyFrames`:
    // these are read off the CHARACTER, not off its CharBody, so a null CharBody does not suppress
    // them. `wepUnreadable` is its own counter rather than folded into `wepFrames`'s complement,
    // because *"no weapon drawn"* and *"we could not tell"* are different answers and merging them
    // would let a broken read masquerade as the study's expected result - F351's shape.
    long long    ccWepFrames;        // samples where a weapon was drawn AND the pointer looked like
                                     // an object. **Validated before counting, not after** - a
                                     // non-null garbage field is the hazard on a half-streamed
                                     // puppet and it would count as a drawn weapon.
    long long    ccWepUnreadable;    // ... where the getter did not match the field-read pattern,
                                     // OR the character itself would not read
    long long    ccWepImplausible;   // ... where the field was non-null but did NOT look like an
                                     // object. Its own counter, because "garbage" and "no weapon"
                                     // are different answers and so are "garbage" and "a weapon"
    uintptr_t    ccWepLastVt;        // **LAST-SEEN**, not a summary - a character that fought with a
                                     // sword for 4,000 samples and drew a crossbow for the last ten
                                     // reports as a crossbow. Identifies the weapon offline; it does
                                     // not characterise the row
    long long    ccUnconFrames;      // medical.unconcious set
    long long    ccDeadFrames;       // medical.dead set
    long long    ccNoArmsFrames;     // BOTH rightArmUsable and leftArmUsable clear
    long long    ccAnimalFrames;     // the row is a CharacterAnimal (weapon field at +0x708).
                                     // **The engine skips the entire medical block for an animal**,
                                     // so the three counters above do not gate readiness there
    long long    ccReadyPartial;     // a weapon is drawn AND - unless this is an animal - not
                                     // unconscious AND not dead AND at least one arm is OK.
                                     // **THREE TESTS OVER FOUR MEDICAL BYTES, PLUS THE WEAPON** -
                                     // stated exactly, because an earlier version of this line said
                                     // "all four field clauses" and the block above said "five",
                                     // and neither matched the code.
                                     // **THE ARM CLAUSE IS NO LONGER A GUESS.** It was labelled one
                                     // here until the engine's test was read: `hasOneWorkingArm`
                                     // (0x6436C0) is literally `[med+0x165] || [med+0x166]`, i.e.
                                     // `rightArmUsable || leftArmUsable` - the disjunction this line
                                     // already used, confirmed rather than assumed.
                                     // **A NECESSARY CONDITION, NOT THE VERDICT** - the predicate's
                                     // prone/crouch/crippled tail and the crossbow rejection are not
                                     // covered here.
                                     // **AND IT IS DERIVED FROM `ccWepFrames`, SO IT INHERITS ALL
                                     // THREE OF THAT FIELD'S CONDITIONS.** It only increments when
                                     // the weapon read SUCCEEDED, so zero means refused for certain
                                     // **only** where `ccSamples > 0`, `ccWepUnreadable == 0` and
                                     // `ccWepImplausible == 0`. **The last is the dangerous one:**
                                     // an implausible pointer means the field WAS non-null, so the
                                     // engine would have seen a weapon and PASSED while this reads
                                     // zero. A non-zero value never means it would pass.
    int          ccMine;          // ownership at the last sample - the control's label
    int          ccIsAI;          // isAI at the last sample
};
CombatWatch g_cmWatch[kMaxCombatWatch] = {0};

long long g_cmSent      = 0;   // edges sent by the authority
long long g_cmApplied   = 0;   // edges applied on the peer
long long g_cmNoTarget  = 0;   // entered combat, but against someone we do not replicate
long long g_cmNoCombat  = 0;   // uid had no readable CombatClass
long long g_cmRefused   = 0;   // apply refused (unknown uid, unresolvable target, bad pointer)
long long g_cmPulses    = 0;   // F233: decision passes let through while a puppet is fighting
long long g_cmOrders    = 0;   // F239: attack orders injected into a puppet's task system
long long g_cmOrderFailed = 0; // F239: injections refused (unreadable AI or task system)
long long g_cmOrdersCleared = 0; // F242: orders retracted when the authority left combat
long long g_cmPulseTick = 0;   // frame counter for the pulse cadence

// F348 / P041 - swing replication counters. Deliberately one counter per REFUSAL REASON rather
// than one "failed" total: `verify-what-numbers-measure` - every early return gets a counter, or
// a run that produces zero swings cannot say which gate closed.
long long g_swSent            = 0;  // authority: transitions into the attack state, sent
long long g_swSendNoTargetObj = 0;  // authority: in the attack state with NO resolvable target
long long g_swSendNotReplicated = 0;// authority: target is a character we do not replicate (P-16)
long long g_swSendFailed      = 0;  // authority: transport refused
long long g_swRecv          = 0;  // peer: messages received
long long g_swDisabled      = 0;  // peer: arrived while `swingrepl off` - the A/B arm
long long g_swNoWatch       = 0;  // peer: **no MIRROR SLOT resolved this uid**, so the swing was
                                  //       DROPPED. It used to mean "the watch table was full",
                                  //       which F391 made impossible - the table cannot fill,
                                  //       because it is indexed by mirror slot. **The name and the
                                  //       printed label `droppedNoWatchSlot` were left describing
                                  //       a condition that no longer exists**; corrected here
                                  //       rather than left to be read as the old meaning.
                                  //       (F356: no write happens on this path any more -
                                  //        the intent lives in the watch record)
long long g_swRewrite       = 0;  // peer: a new swing written while the previous had no verdict yet
long long g_swNoChar        = 0;  // peer: uid has no local copy (F079 pre-session spawn)
long long g_swNoCombat      = 0;  // peer: no readable CombatClass
long long g_swNoMode        = 0;  // peer: not in combat mode - the engine requires it (F348 #1)
long long g_swNoTarget      = 0;  // peer: target uid does not resolve here
long long g_swStumble       = 0;  // peer: in STUMBLE; setSwordState would refuse anyway
long long g_swAlready       = 0;  // peer: already swinging - the write would restart the blow
long long g_swBlocking      = 0;  // peer: F351 - mid-block; injecting would cancel a visible parry
long long g_swTraceBlocked  = 0;  // peer: F351 - startupState would turn this into a WALK
long long g_swNoMovement    = 0;  // peer: F351 - CharMovement unreadable
long long g_swForcedWP      = 0;  // peer: F351 - the combat mover is committed to a waypoint

// P046 / T095 - **DID OUR OWN SETTER TAKE?** Read immediately after `setTarget`
// returns, before anything else can run.
//
// T094 measured `curTarget=0` on 16 of 16 decline lines and the obvious reading - that combat mode
// never wrote a focused target - **is about a different field** (+0x2D0, not +0x290) and does not
// follow. What T094 could not tell apart is "the setter never wrote it" from "something cleared it
// between our write and the verdict". These four counters separate them, and the split comes
// straight out of `setTarget`'s decompile rather than from a guess about it:
//
//     if (target != 0 && handle.operator==(target->handle)) return;                     // <-- (1)
//     currentTarget = resolveHand(&currentTargetHandle);   // STORE SITE #1, from the OLD handle
//     ... copy target's +0x60..+0x70 into currentTargetHandle ...
//     currentTarget = resolveHand(&currentTargetHandle);                                 // <-- (2)
//
// **Store site #1 was missing from the first version of this comment**, which is the "a guard was
// applied at the later store and not at the earlier one" pattern F357 was written about, in a
// comment rather than in code. It does not change what this probe reads - site #2 wins and we read
// after the call returns - but it is exactly the kind of omission someone later reasons from.
//
// (1) is an EARLY RETURN THAT WRITES NOTHING - not a documented behaviour we chose to rely on, a
// branch at the top of the function we are calling. If the handle already names the target, the
// call is a **no-op**, and `currentTarget` keeps whatever it held, which T094 measured as null.
// (2) can independently return 0 if the peer cannot resolve the handle, which is a completely
// different defect with a completely different fix.
//
// Told apart by whether the HANDLE BYTES CHANGED across the call, which distinguishes the branch
// that wrote nothing from the branch that wrote a handle that would not resolve.
long long g_swSetOk           = 0;  // +0x290 == the target we asked for. The setter worked.
long long g_swSetEarlyReturn  = 0;  // handle unchanged AND already names the target -> branch (1)
long long g_swSetResolveFail  = 0;  // handle now names the target, +0x290 still null -> branch (2)
long long g_swSetWrongTarget  = 0;  // +0x290 non-null and NOT our target - resolved to a stranger
// Counted on its OWN axis, across every outcome: the handle was already correct and unchanged, so
// the call did nothing. **This is not the same question as what +0x290 holds afterwards**, and the
// first version of this probe conflated them - see the classification for what that cost.
//
// One caveat that is honest to state and cannot be measured away: `hand::operator==` is a virtual
// dispatched through the handle's own vtable, so "the bytes did not change" cannot distinguish the
// guard returning early from the write path writing byte-identical values. **Both mean the same
// thing for this run** - the handle already named the target and +0x290 was not repaired - and the
// fifth attempt would treat them the same way, so they are counted together rather than one being
// guessed into the other's bucket.
long long g_swSetNoOp         = 0;
long long g_swSetOkAlias      = 0;  // +0x290 is a DIFFERENT POINTER naming the same uid. A success,
                                    // counted apart because if it is ever non-zero then every
                                    // `got == target` test in this file is comparing the wrong two
                                    // things and the pointer identity assumption has to go.
long long g_swSetReported     = 0;  // failure detail lines printed (bounded; counters stay exact)
long long g_swSetOkReported   = 0;  // ... and the good outcome's own budget, kept separate

// P045's OWN isAI fault sink, kept out of `g_swIsAIFaulted`. See CombatIsAI for why a shared
// counter would have made the swing report's own diagnostic unreadable for the whole run.
long long g_ccIsAIFaulted     = 0;

// P050 - resolved once at hook-install time, for a value that cannot change.
//
// The first version justified this as avoiding "a PEB walk on the hot path" - which is contradicted
// by the code three lines away, where `PlausibleObject` calls `GetModuleHandleA(0)` on every one of
// its three calls per character per frame. The caching is fine; **the reason given for it was
// false, and a false rationale is worse than none because the next person reasons from it.**
uintptr_t g_imageBase = 0;

// P048 - **DID THE TRAMPOLINE ACTUALLY ARM?** Set once at install from the prologue read-back, and
// consulted by every line that would otherwise blame the probe for a zero.
//
// **T096 disclosed why this global has to exist.** That run proved the hook armed (`patched=1`) and
// still printed *"THE HOOK NEVER FIRED - this is the PROBE failing, not the engine"* on the client,
// plus `THE PROBE DID NOT WORK FOR THIS ROW` on every row and `hookLive=0` on every decline line -
// **all from the same DLL, in the same log, contradicting its own startup line.** Every one of them
// keyed on `g_updTotal == 0` alone, which was the only signal available before P048 existed.
//
// A reader following the log's own instruction would have **voided a valid result** - which is the
// exact failure T095 was built to end, reappearing as TEXT in the run that ended it. **A diagnostic
// that can contradict itself is worse than one that says nothing**, because the wrong half is
// written in the more alarming voice.
int g_updPatched = -1;   // -1 = install not reached, 0 = prologue unchanged, 1 = patched

// ---------------------------------------------------------------------------------------------
// P045 - THE CALL COUNTER. Detour on `CombatClassAI::update` (0x60CC10), counted per object.
//
// **The detour must do NO work that can block, allocate, log or scan**, because the thread it runs
// on is not known until this run measures it (P026 found the sibling combat tick on the AI worker,
// not the main loop). So: a direct-mapped table, hashed on the object pointer, four-slot linear
// probe, one interlocked add. No lock, no allocation, no logging, no linear scan of the mirror.
//
// **KEYS ARE WRITTEN ONLY BY THE MAIN THREAD** (from `SampleCombatTick`, which already walks the
// mirror) and **COUNTS ONLY BY THE DETOUR.** That is what makes it race-free without a lock: there
// is exactly one writer of each word. A detour that finds no slot bumps one global instead of
// claiming one, so a busy object can never evict a registered one and silently zero its count -
// which would fabricate the study's expected answer a second time, by a different mechanism.
// How many combat-object BINDINGS a row may have before its `updCalls` stops being reported as a
// number.
//
// **BINDINGS, NOT REBINDINGS, AND THE DIFFERENCE IS ONE OFF-BY-ONE AWAY FROM A WRONG LABEL.** The
// first sighting of a character binds it, so a perfectly ordinary row that never changed object
// reads **1**, not 0. Two therefore allows the normal case plus one genuine stream-out-and-back; a
// third means something is alternating and every interval is being discarded. The counter is
// printed as `binds=` rather than `rebinds=` so nobody reads the ordinary 1 as a fault.
const int kUpdRebindTrust = 2;

// F392 - **HOW MANY WATCH ROWS CARRY THIS UID.** It should always be 1, and the slot-indexed
// rewrite made it possible for it to be 2 in a way that LOOKS FINE, which is why this exists.
//
// **`MirrorAdd` dedups on the OBJECT, not the uid** (F363 §1 says so outright), so two mirror
// slots can legitimately hold the same uid pointing at different `CombatClassAI` objects. Before
// the rewrite that produced **one** watch row being handed object X then object Y within the same
// tick: `ccLastObj` disagreed twice per frame, `ccUpdRebinds` blew past `kUpdRebindTrust`, and the
// row printed `updCalls=UNMEASURABLE` and was excluded from the arm totals. **The duplicate was
// caught by accident, but it was caught.**
//
// Indexed by mirror slot, the two slots get **two separate rows**, each seeing one stable object.
// Neither rebinds, `ccUpdRebinds` stays at 1 on both, and **both rows print a confident number
// covering roughly half the character's frames** - while the arm denominators count the character
// twice. F364's rule was *"a row that refuses to answer is recoverable; a row that answers 0 is
// not"*; **a row that answers a plausible fraction is worse than either.**
//
// Report-time only. O(n) per row, O(n²) per report, which is nothing at report cadence - and it
// deliberately avoids maintaining a second index that could fall out of step with the first, which
// is the class of thing F267 was about.
int WatchRowsForUid(unsigned int uid)
{
    if (uid == 0) return 0;
    int n = 0;
    for (int i = 0; i < kMaxCombatWatch; ++i)
        if (g_cmWatch[i].uid == uid) ++n;
    return n;
}

/* M7a (T-197 piece 7a): THE CATCH-UP'S COMBAT MODE (the row walk; coop::CombatModeResendOwned below this namespace calls it).
   The tick sends COMBATMODE only on an edge, so a game that comes to have the area mid-fight would never hear it: the row's
   last sampled edge (on, against a replicated target) is said again. MAIN THREAD. */
bool CombatModeResendRow(unsigned int uid)
{
    if (uid == 0 || !net::IsUidMine(uid)) return false;
    for (int i = 0; i < kMaxCombatWatch; ++i)
        if (g_cmWatch[i].uid == uid && g_cmWatch[i].lastOn == 1 && g_cmWatch[i].lastTarget != 0)
            return net::SendCombatMode(uid, true, g_cmWatch[i].lastTarget);
    return net::SendCombatMode(uid, false, 0);   /* M7a fold (review 2026-09-30 F2 a): not fighting - OFF, so a fight that ended in the gap ends on the asker too */
}

// **ONE PREDICATE, ONE PLACE, BECAUSE TWO COPIES ALREADY DRIFTED ONCE.**
//
// The report has two loops over the same rows - a pre-pass that computes the arm totals, and the
// row loop that prints them. Each had its own local called `measurable`, and when `uidRows > 1` was
// added as a disqualifier it went into **one of them**. The result was a report that contradicted
// itself: the summary line declared those rows unmeasurable and excluded them, while the row line
// printed a confident `updCalls` and, worse, was still eligible for the banner
// **"THE AI UPDATE WAS NEVER CALLED FOR THIS CHARACTER"** - the study's expected conclusion, in
// capitals, on a row the same report had just disqualified.
//
// **That is F362 / F363 §3 / F364 §2 for the fourth time**, and it arrived inside the fix for the
// third. Two variables of the same name in one function, over the same data, with different rules.
// A shared helper is the only version of this that cannot drift again.
// **`uidRows` IS A PARAMETER, NOT AN INTERNAL SCAN.** Every caller already has the number in hand,
// and computing it inside as well meant two scans of the same array per row and a coupling that
// held only because both scans happened to see an unchanging table. Passing it makes the dependency
// explicit and leaves exactly one place where the rule itself is written down.
bool RowMeasurable(const CombatWatch* w, int uidRows)
{
    return (w->ccUpdRebinds <= kUpdRebindTrust) && (uidRows <= 1);
}
const int kUpdSlots = 16384;              // power of two; 4 x the mirror (4096 rows since T-354)
const int kUpdMask  = kUpdSlots - 1;
const int kUpdProbe = 4;
struct UpdSlot
{
    const void*     obj;       // main thread writes; detour only reads
    volatile LONG64 calls;     // detour writes; main thread only reads
};
UpdSlot g_updSlots[kUpdSlots] = {0};
volatile LONG64 g_updTotal    = 0;   // every call, whoever it was for - the world-wide denominator
volatile LONG64 g_updUnknown  = 0;   // calls for an object we never registered (not in the mirror)
volatile LONG64 g_updNoSlot   = 0;   // registered-but-full probe chain. Must be 0, or a count could
                                     // be missing rather than absent, and those read identically.
volatile LONG   g_updThreadId = 0;   // which thread ticks combat - unknown until this run says so

int UpdHash(const void* p)
{
    // >>4 because these are 16-byte-aligned allocations; the low bits carry no entropy.
    return (int)(((uintptr_t)p >> 4) & (uintptr_t)kUpdMask);
}

void (*orig_ccUpdate)(CombatClassAI*, float) = 0;

void detour_ccUpdate(CombatClassAI* self, float frameTIME)
{
    InterlockedIncrement64(&g_updTotal);
    if (g_updThreadId == 0)
        InterlockedExchange(&g_updThreadId, (LONG)::GetCurrentThreadId());

    const int h = UpdHash(self);
    bool found = false;
    for (int i = 0; i < kUpdProbe; ++i)
    {
        UpdSlot* s = &g_updSlots[(h + i) & kUpdMask];
        if (s->obj == (const void*)self) { InterlockedIncrement64(&s->calls); found = true; break; }
    }
    if (!found) InterlockedIncrement64(&g_updUnknown);

    orig_ccUpdate(self, frameTIME);
}

// Main thread ONLY. Returns the slot for this object, claiming a free one on first sight, and
// writes the count to subtract from later readings into `*primeOut`.
//
// **`calls` IS NEVER ZEROED, AND THAT IS WHAT MAKES THIS RACE-FREE WITHOUT A BARRIER.**
//
// The first version zeroed it and then published the key, reasoning that publishing last closes the
// window. It does not close it completely: `calls` is `volatile` (release on MSVC x64) but `obj` is
// not, so nothing stops the plain store to `obj` being hoisted ABOVE the volatile store to `calls`
// - release semantics keep earlier operations from sinking below, not later ones from rising above.
// One such interleaving and a call that landed between them is erased by the zeroing. Tiny, and
// **an UNDERCOUNT - biased toward exactly the conclusion this run must not jump to.**
//
// So the count is left alone and a BASELINE is taken instead, read **before the key is published**.
// While `obj` is still 0 the detour cannot match this slot and therefore cannot be writing `calls`,
// so the read needs no ordering guarantee at all. Slots are never released, so a claimed key is
// stable for the process lifetime and the baseline stays meaningful.
UpdSlot* UpdSlotFor(const void* obj, long long* primeOut)
{
    const int h = UpdHash(obj);
    for (int i = 0; i < kUpdProbe; ++i)
    {
        UpdSlot* s = &g_updSlots[(h + i) & kUpdMask];
        if (s->obj == obj) { *primeOut = (long long)s->calls; return s; }
        if (s->obj == 0)
        {
            *primeOut = (long long)s->calls;   // safe: the key is unpublished, so nobody writes it
            s->obj    = obj;
            return s;
        }
    }
    ++g_updNoSlot;
    *primeOut = 0;
    return 0;
}
long long g_swNoAnim        = 0;  // peer: F351 - AnimationClass unreadable
long long g_swMidAction     = 0;  // peer: F351 - an action animation is still playing (the engine's
                                  //        own guard on this exact write)
long long g_swNotOurs       = 0;  // peer: F351 - a SWING naming a uid WE author. Refused.
long long g_swNotAI         = 0;  // peer: F354 - not a CombatClassAI, so +0x2C0 is out of bounds
long long g_swGateUnknown   = 0;  // peer: F354 - an expiry with no gate recorded. Should be 0.
// F354 - the INTENT lifecycle. `intents = wrote + intentExpired + intentSuperseded + pendingNow`.
long long g_swIntents        = 0; // messages that became a retryable intent
long long g_swIntentExpired  = 0; // ... and never found an open gate within kSwingIntentTicks
long long g_swIntentSuperseded = 0; // ... replaced by a newer blow before they got their chance
long long g_swIntentAbsorbed = 0; // ... dropped because the peer was ALREADY swinging (F356)
long long g_swAbsorbedByOurs   = 0; // F358: ...by an arc WE injected
long long g_swAbsorbedByItsOwn = 0; // F358: ...by one the peer started on its own
long long g_swIntentNotServiced = 0; // F358: expired without ever being ATTEMPTED (puppet unseen)
long long g_swIsAIFaulted    = 0;  // F358: the vtable slot could not be read or was out of image
long long g_swIntentStale    = 0; // ... expired at an age that means it never aged while streamed out
long long g_swWroteFirstTry  = 0; // F356: the retry was not needed for this one
long long g_swWroteAfterWait = 0; // F356: the retry IS what got this swing through
long long g_swWrote         = 0;  // peer: the three writes were made
long long g_swLanded        = 0;  // peer: and the state actually reached SWORD_SWING afterwards
long long g_swDeclined      = 0;  // peer: it did NOT - initialiseAttack refused (see the probe)
long long g_swUnscoredStale = 0;  // peer: F351 - pending far too long to score honestly (streamed
                                  //        out and back); counted, never booked as a decline

// How long the peer waits for its own engine to turn a written STARTUP into an actual swing
// before calling it a decline. The engine needs one `CombatClassAI::update` to run `startupState`,
// so anything above a couple of frames is generous; 30 frames (~0.25 s at 118 fps) is chosen so a
// single stalled frame or a paused instant cannot be mistaken for a refusal.
const long long kSwingVerdictTicks = 30;

// F351 - beyond this a pending write is not scored at all. A puppet that streams out is invisible
// to the scoring loop (`MirrorSlot` hides retired entries) and the watch slot is never released, so
// on stream-back an ancient write would otherwise be booked as a fresh DECLINED against state read
// minutes later. ~30 s at 118 fps: far beyond any honest verdict, far short of a session.
const long long kSwingStaleTicks = 3600;

// F354 - how long an INTENT keeps re-attempting before it is abandoned.
//
// 45 frames is ~0.38 s at 118 fps, deliberately under half of a Kenshi swing. Long enough to ride
// out an attack recovery or a hit reaction - the states that close `isActionAnimating`
// and that a follow-up blow lands in - and short enough that a swing injected at the end of the
// window is still part of the same exchange rather than an echo of one that finished. The
// authority's next blow supersedes it anyway, and supersessions are counted.
const long long kSwingIntentTicks = 45;

// Which gate was closed at the last attempt. Reported at expiry, one counter each, because
// "we refused" is not an answer and "which of seven predicates was false" is.
enum SwingGate
{
    SW_GATE_NONE = 0,
    SW_GATE_NO_COMBAT_CLASS,
    SW_GATE_NOT_IN_COMBAT_MODE,
    SW_GATE_NO_TARGET,
    SW_GATE_STUMBLE,
    SW_GATE_ALREADY_SWINGING,
    SW_GATE_BLOCKING,
    SW_GATE_NOT_AI,
    SW_GATE_TRACE_BLOCKED,
    SW_GATE_NO_MOVEMENT,
    SW_GATE_FORCED_WP,
    SW_GATE_NO_ANIMATION,
    SW_GATE_MID_ACTION
};

// F233 - **DEFAULT OFF as of T071/F236.** It shipped ON, it was measured, and it is worse than
// the defect it fixes.
//
// T071: the peer DID swing - all four characters, 529 transitions against T070's zero, so the
// diagnosis was right. But every one of them ran away. `maxDriftXZ` reached **366-491 units**
// against the 1.4-1.5 of a gated puppet, the mod's own detector raised
// `[M2] puppet LOST` **69 times**, and one character was still swinging **102 seconds after the
// authority had stopped**.
//
// The mechanism is visible in the goals: the peer adopted `Self preservation` 24 times and
// `Protect allies` 5 - **its own** goals, never the authority's `Attacking target`. A decision
// pass does not just advance the combat state machine; it lets the AI CHOOSE A GOAL, and a goal
// outlives the pulse. `Self preservation` is fleeing, which is precisely 491 units of drift.
//
// So this is left in the build, switchable, and OFF - a measured failure is worth keeping
// runnable, and turning it on is one command for whoever tests the next idea.
bool g_combatPulse = false;

// F251 - **ORDER INJECTION IS NOW OFF BY DEFAULT, AND THIS IS AN ATTEMPT CAP.**
//
// Three variants have been built and measured, and all three end the same way:
//   1. inject order + one pulse                      (T073) - 756 post-fight swings
//   2. ... + retract with a pulse                    (T075) - peer re-raised combat in 73 ms
//   3. ... + retract by clearing the GOAL, no pulse  (T076) - **1935 rogue swings, mode=0**
//
// T076 is the one that settles it. The retraction worked PERFECTLY - the peer never once raised
// combat mode on its own - and it made no difference, because **combat mode is not what
// authorizes an attack.** All 1935 swings ran `4:DECISION -> 0:SWORD_SWING` with `mode=0`, and
// on two characters the peer swung BEFORE the authority did, by 3.06 s and 1.19 s.
//
// The through-line across all three: **a gated puppet swings ZERO times (T070). The moment it is
// given a single decision pass it becomes permanently autonomous** - and one pass is the minimum
// this approach can possibly cost. There is no smaller version of it left to try.
//
// The cost is not cosmetic. T075 measured a peer character acquiring **Prisoner Shackles** and
// losing items - its own AI let it be captured - while every failure counter read zero.
//
// So: OFF. Combat-mode replication stays ON, because it is separately verified and safe
// (T067/T072) and it is what drives the portrait marker the user reported missing. What it does
// NOT do is make the peer swing, and that half is now a recorded dead end rather than a pending
// idea. `combatorders on` for whoever tests a genuinely different approach.
bool g_combatOrders = false;

// F348 - SWING REPLICATION. **ON by default, and it is a different shape from the three attempts
// above rather than a fourth variant of them.**
//
// All three of those opened the decision gate in some form and then tried to contain what else the
// AI decided with it; T070 measured the cost directly and it is not containable, because one
// decision pass lets the AI choose a GOAL and the goal outlives the pulse.
//
// This never opens the gate. F348 read the engine end to end and found that the swing is
// authorized inside `CombatClass::whoAttacksYouOrMe` by three field writes -
// `nextMove = 0`, `combatState = STARTUP`, `setTarget(target)` - and that
// `decisionState` cannot start a swing at all: it only ever chooses 1, 5, 6 or 10. So the peer's
// AI stays exactly as suppressed as it is today and the engine's own
// `startupState -> AttackState -> attackState` chain does the rest.
//
// ON by default because the whole point of the next run is to measure it; `swingrepl off` puts the
// build back to the current behaviour in one command, because a fix that cannot be turned off
// cannot be measured against its own absence.
bool g_swingRepl = true;

// F384 / P055 - **THE FIFTH ATTEMPT. INSTALL THE TASK DIRECTLY, WITH ZERO AI DECISION PASSES.**
//
// Licensed by measurement, not by another idea: **F369** named the cause (the peer's puppets have
// no current action, and the combat state machine is reachable ONLY from one), and **T097/F383**
// confirmed the precondition (`CharBody::update` DOES run for puppets - 59 of 59 rows, live
// varying frame time - so the platoon gate is not the blocker).
//
// **WHY THIS IS NOT A FIFTH VARIANT OF THE FIRST FOUR.** Every previous attempt reached the machine
// by getting the AI to install a task, and every one paid for it with a decision pass - **which is
// a window in which the AI chooses its OWN goal.** F236 measured 491 units of drift and a
// `Self preservation` goal; F247 measured a 73 ms self-restart through a pulse added to a
// RETRACTION; F252 concluded *"one pass is the minimum this approach can cost."* **F371 then found
// T074 and T075 ran byte-identical code and disagreed - so the cost is not merely one pass, it is
// a COIN FLIP.**
//
// `CharBody::startAction(TaskType, RootObject*)` installs the task itself, calling the
// engine's own factory. **The AI is written to (a goal at priority 3) but never ASKED** - no GOAP,
// no `chooseGoal`, no `periodicUpdate`. **Zero passes means there is no window to lose.**
//
// **DEFAULT OFF**, and switchable at runtime, for the reason F236 states in its own words: *a fix
// that cannot be turned off cannot be measured against its own absence.* The test plan turns it on
// explicitly so the switch state is in the log rather than assumed.
bool g_taskInstall = false;

// P057 - **DEFAULT OFF, and separately switchable from `taskinstall`**, so the three arms are
// distinguishable in the log rather than inferred: (off,off) control · (on,off) *does the weapon
// get drawn at all?* · (on,on) *does the melee task install once it is armed?*
bool g_weaponTask = false;

// H015 / P059 - NATIVE COMBAT WINDOW. Not attempt seven of suppress-and-drive: the opposite lever.
// Six attempts tried to make a GATED puppet act; T071 measured that an UNGATED one fights (529
// swings) and that its cost is containment - own goals, drift, swinging after the authority
// stopped. M3 already makes a puppet's own blows harmless (a hit on a character we do not own is
// refused; the authority's damage is what lands), so the swing is visual only and health parity
// is untouched. This bounds the release to the authority's ON..OFF edges and closes it hard:
// re-gate FIRST, then orders and goals cleared with no decision pass, then place the body.
// combat1 (user decision 2026-09-25, F914/F916/F918): ON BY DEFAULT. Without it a character driven by one game can never
// hurt a character driven by the other: a blow on a copy is dropped on the attacker's screen (the victim's own game
// decides damage) and the copy-side swing replay is declined by the engine (T295: 1,453 of 1,453). T298/T299 ran with it on:
// blows landed across games, a knockout and arrest1's carry followed, 0 dumps. The user asked to monitor it and keep the
// experience close to single player: see the parity[...] block on the [NC] REPORT line. `nativecombat off` still turns it off.
bool g_nativeCombat = true;
// combat1: how closely a copy fighting through a window tracked its owner - summed over closed windows (main thread).
long long g_ncParityWindows = 0;       // windows closed with a drift measurement
double    g_ncParityMaxDriftSum = 0.0; // sum of each window's worst horizontal drift (units)
float     g_ncParityMaxDriftWorst = 0.0f;
double    g_ncParityDriftMeanSum = 0.0; // sum of each window's mean drift (reconcile's own mean)
long long g_ncParityOver5 = 0;         // windows whose worst drift passed 5 units (a gap a player could see)
long long g_ncParityNoSample = 0;      // review-combat1: windows closed with no drift sample at all - not averaged in as 0
// combat2 (F920: B's windows averaged a 26.8-unit peak): WHERE the drift comes from. The gap a window opened with (summed
// over windows whose opening gap was read), and how many windows' peak grew more than 5 units past that opening gap (drift
// made during the fight, not inherited from the walk-up).
double    g_ncParityOpenDriftSum = 0.0;
long long g_ncParityOpenDriftN = 0;
long long g_ncParityGrew5 = 0;
// Not in the aggregate (review-combat1, older paths): windows cut off by a world teardown or a rebind orphan.
long long g_ncWindowsOpened      = 0;
long long g_ncWindowsClosed      = 0;
long long g_ncRetargeted         = 0;   // ON edge arrived while a window was already open
long long g_ncOpenNoWatch        = 0;   // could not open: no watch row for the uid
long long g_ncOrderFailed        = 0;   // InjectAttackOrder returned false at open/retarget
long long g_ncWindowSwings       = 0;   // peer SWORD_SWING entries inside a native window
long long g_ncSwingsWhileAuthOff = 0;   // peer SWORD_SWING entries while the authority last said OFF
long long g_ncSnapDone           = 0;
long long g_ncSnapDeclined       = 0;   // ragdoll / prone / unreadable - see SnapPuppetToAuthority
long long g_ncOrphanedByRebind   = 0;   // watch row reused while a window was open (re-gated by uid)
long long g_ncSwitchRefused      = 0;
long long g_ncCloseWaitedForPass = 0;   // re-gated with a decision pass in flight; waited it out
long long g_ncCloseQuiesceTimedOut = 0; // ... or gave up waiting (the clears then raced the pass)
long long g_ncRetargetQuiesceTimedOut = 0;
long long g_ncOpenWasNotGated    = 0;   // released an AI that was not gated (adopt refused a slot?)
long long g_ncOpenNoPuppet       = 0;   // no Puppet row for the uid at open - drive not suspended
long long g_ncClosedBySwitch     = 0;   // nativecombat off closed it
long long g_ncClosedByDrop       = 0;   // DropPuppet closed it (re-gated)
long long g_ncClosedByDropNoRegate = 0; // DropPuppet closed it and could NOT re-gate
long long g_ncCloseNoChar        = 0;   // an edge arrived for a uid with no local character
long long g_ncSnapReason[9]      = {0, 0, 0, 0, 0, 0, 0, 0, 0};   // index = SnapPuppetToAuthority result; 7 = character unresolved; 8 = not needed (drift below kNcSnapOnlyAbove)
const float kNcSnapOnlyAbove = 2.0f;
long long g_ncGateRefusedOnClose = 0;   // SuppressCharacter(true) did not take at a close (slots full / AI unreadable)
long long g_ncGateRefusedOnRetarget = 0;
long long g_ncGateRefusedOnOrphan = 0;
long long g_ncOpenRefusedNoOrder = 0;   // the order could not be written, so the gate was NOT released
long long g_ncDropNoRow          = 0;   // DropPuppet reported a window but no watch row had it
long long g_ncCloseNoCharSwitch  = 0;   // switch-off close: FindSpawned refused the uid
long long g_ncRetargetOrderFailed = 0;  // retarget write failed: window stays open, AI released, order stale
long long g_ncRetargetRefusedWrite = 0; // review 4 item 6: gate refused or pass in flight -> the write was REFUSED (target stays stale)

const int kNcQuiesceMs = 20;

static double CmNowSeconds()
{
    LARGE_INTEGER f, t; f.QuadPart = 0;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t);
    return (f.QuadPart > 0) ? (double)t.QuadPart / (double)f.QuadPart : 0.0;
}


long long g_wpAttempts     = 0;  // we ran the TASK_DRAW_WEAPON install for a puppet
long long g_wpAlreadyArmed = 0;  // ... and it already had a weapon, so nothing was installed
long long g_wpNowArmed     = 0;  // ... **weaponInHands was NULL before and is NON-NULL after**
long long g_wpStillEmpty   = 0;  // ... the task ran and the field is still NULL - a REAL refusal
long long g_wpUnreadable   = 0;  // the BEFORE read declined - nothing was installed
long long g_wpAfterUnreadable = 0; // the AFTER read declined. **NOT the same thing:** we already
                                 // installed a task, so this is the one case where we changed the
                                 // world and cannot report what happened
long long g_wpFactoryDeclined = 0; // the task factory returned NULL - startAction never ran
long long g_wpReqRefused   = 0;  // TASK_DRAW_WEAPON's OWN requirements refused - startAction never ran
long long g_wpNoBody       = 0;
// **THE EIGHT `g_wpMemo*` COUNTERS ARE GONE WITH THE EVICTION THEY MEASURED (F407).**
// `memoCleared`, `memoFreedAfterInstall`, `memoFreedNoInstall`, `memoAlreadyEmpty`,
// `memoNotQuiesced`, `memoReadbackFailed`, `memoNoAi` and the `g_wpNoAi` read counted a destructive
// call this build no longer makes. **Leaving them in place would print eight zeros that a reader
// would take as evidence the eviction ran and found nothing** - which is F378's shape (a counter
// that cannot move) with a false story attached rather than a blank one.

// P057 / U-13 / U-14 - **THE TWO NUMBERS THAT SETTLE "IS THE REFUSAL STICKY", AND THEY ARE PURE
// READS.**
//
// The design that was rejected for T099 needed a detour on a hot AI function to count evaluations.
// **It is not needed.** `_isRequirementsComplete` inserts into the memo on BOTH exits, so
// **`_Mysize` growing across our install call IS an evaluation, and it not growing IS a memo hit.**
// One field read either side of a call we already make.
//
// **And it repairs U-14's fatal caveat at the same time.** The `failedOn` global is stale on a
// memo hit, so a refusal code read blindly would look exactly like an answer. Read it only when
// `memoGrew` says an evaluation actually happened.
long long g_tiMemoGrew   = 0;   // install attempts where the memo gained an entry = EVALUATED
long long g_tiMemoHit    = 0;   // ... where it did not, AND the check was actually reached =
                                // the cached answer came back. **THIS is what confirms U-13.**
long long g_tiMemoNotAsked = 0; // ... where the check was never reached at all (the factory
                                // declined, or the installer saw the same task already current and
                                // returned without asking). **Scored separately because "the memo
                                // did not grow" is not "the memo answered"**
long long g_tiMemoUnread = 0;   // ... where we could not read the map at all
// **NAMED `Unverified` ON PURPOSE.** Read only on an actual refusal (the current-action field came
// back NULL), because the SUCCESS exit also grows the memo and writes nothing here. Even then it is
// a racy read: four other call sites pass the same global, all under `AI::periodicUpdate`, which
// runs continuously on the worker thread. Valid only alongside `refusedInsideInstaller > 0`.
int       g_tiLastFailedOnUnverified = -1;
long long g_tiFailedOnReads = 0;

// P055 counters. **One per refusal reason, because a run that installs nothing must be able to say
// WHICH gate closed** - the rule F348 was written under, and the reason T094 could attribute its
// 206 failures at all.
long long g_tiAttempts   = 0;  // we called setCurrentAction
// **NOT "it returned true".** That is exactly the misreading the call site is headed
// "DO NOT TRUST THAT RETURN VALUE" to prevent, and this declaration is 1,300 lines away - which is
// where a reader interpreting `ok=` in a log actually goes. **`ok` means CharBody+0x68 holds a
// Task_FocusedMeleeAttack AFTER the call.** `okAlready` is the subset where it held one BEFORE too,
// i.e. our call is not evidenced to have done anything.
long long g_tiOk         = 0;
long long g_tiOkAlready  = 0;
long long g_tiRefused    = 0;  // nothing is installed after the call - see g_tiRefusedInside
long long g_tiRefusedInside = 0; // ... of those, where the call RETURNED TRUE and installed nothing
                                 // - i.e. the refusal happened inside the installer, which the
                                 // return value cannot report. See the call site.
long long g_tiInstalledPlainMelee = 0; // Task_MeleeAttack - installed, and it is the variant that
                                       // does NOT dispatch go (F368). Its own counter.
long long g_tiTaskUnreadable = 0;// the field is non-null and would not read
long long g_tiInstalledOther = 0;// a different, READABLE task is the current action
long long g_tiNoBody     = 0;  // CharBody unreadable
// **DISJOINT, not nested** - the "... of those" phrasing that is correct for `refusedInside` was
// borrowed here and is wrong: exactly one of these fires per plausible body.
long long g_tiEnded      = 0;  // a retraction that actually destroyed a Task
long long g_tiEndedEmpty = 0;  // a retraction against an already-empty field. **Renamed from
                               // `OursNotEnded`, which became actively misleading the moment the
                               // authority guard shipped: nothing reaching that line is ours.**
long long g_tiEndNoBody  = 0;  // CharBody unreadable on the OFF path
long long g_cmNotOurs    = 0;  // F385: a COMBATMODE naming a uid THIS instance authors, refused

// Every 6th frame at ~118 fps is ~20 Hz - often enough that the combat state machine is not
// starved, rare enough to stay a long way from "the gate is off". Deliberately NOT every frame:
// a puppet that gets a decision every frame is an ungated puppet with extra steps, and T070
// measured what that costs (maxDriftXZ 101.8, its own goals, 236 hits generated and discarded).
const long long kPulseEveryTicks = 6;

// ---------------------------------------------------------------------------------------------
// F391 / F352 - **THE COMBAT WATCH IS INDEXED BY MIRROR SLOT. THE PER-FRAME LOOKUP IS O(1).**
//
// **What was here was O(n²) per frame.** `CombatModeTick` walks the mirror by index and used to
// call `CombatWatchFor(uid)`, which linear-scanned this table until it found the uid **or ran off
// the end** - and it did not stop at a free slot, so **a MISS cost the full 512-entry scan.**
// F352 priced it: ~131,000 comparisons per frame at today's 512-row cap, ~15 M/s at 118 fps, and
// **~990 M/s at the 4096 the objective wants.** It is the largest fixed per-frame cost the mod has
// and it has never been isolated from the whole-frame figure.
//
// Mirror slots are hashed on the character's object address and are stable for that character's
// life, and `CombatModeTick` already holds the index. So the row for slot `i` lives at index `i`.
//
// **THIS RETIRES F267 FOR THIS TABLE.** A table indexed by mirror slot cannot be the wrong size for
// the mirror, so *"keep this equal to kMaxMirror"* stops being a rule a human has to remember -
// and the one-time check in `CombatModeTick` makes the remaining assumption LOUD instead of latent.
// **Scoped deliberately: `replicate.cpp` and `soak.cpp` carry the same hand-maintained rule and are
// NOT covered by anything here.**
//
// **A CORRECTION TO MY OWN CLAIM, KEPT WHERE THE CLAIM WAS.** This block previously said the old
// scan *"returned row 0, silently handing out another character's watch row"* when called with
// uid 0. **That is false, and a reviewer caught it.** The old compare was
// `g_cmWatch[i].uid == uid`, so with uid 0 it returned the first row whose uid was 0 - which is by
// definition an **unused** row. It could not return an occupied one.
//
// **The real old defect was milder and different:** every `uid == 0` caller shared one unused row
// that never went through the init block, so it carried `lastOn = 0` and `weWantOn = 0` instead of
// the `-1` sentinels - a *first-read-looks-like-an-edge* bug, not a cross-character one. The guard
// below is still right; the reason given for it was not. **I wrote a stronger claim than the code
// supported and put it in a commit message and a finding.**
//
// **WHAT CHANGED IN BEHAVIOUR, STATED PLAINLY RATHER THAN CALLED A REFACTOR:** a row is now
// **re-initialised when its mirror slot is reused under a different uid.** That is more correct
// (F364: a carried-over row imports a stranger's counters) but it means **a per-uid number is a
// WINDOW, not a lifetime**, on any run where it happens. `watchRebound` is counted and printed for
// exactly that reason - `updRebinds` already exists for the same hazard one level down.
long long g_cmWatchRebound  = 0;   // rows re-initialised because their mirror slot changed uid
long long g_cmWatchNoMirror = 0;   // a uid-only lookup found no mirror slot for that uid
long long g_cmWatchBadSlot  = 0;   // slot index outside the table - must stay 0
long long g_cmWatchZeroUid  = 0;   // a mirror row carried uid 0 - skipped, and now counted rather
                                   // than silently dropped. Believed unreachable; so was badSlot
long long g_cmWatchReboundBlindedP030 = 0;  // rebinds that discarded a live `weWantOn` (see the
                                   // reset function - the P030 probe cannot fire until the next
                                   // inbound COMBATMODE edge, and its silence reads as absence)
long long g_swIntentDroppedTeardown = 0, g_swUnscoredTeardown = 0;   // review-p3s M2: the world teardown's terms in the two identities
long long g_visPeakLinesLocal = 0;   // PROBE P019: side=local lines emitted (process-lifetime; printed on the [NC] REPORT line)
static int ReadCharFrameTimesPodC(const void* ch, float* oft, float* ft) { __try { *oft = *(const float*)((const char*)ch + 0xC0); *ft = *(const float*)((const char*)ch + 0xC4); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }   // PROBE P019 (F502)
long long g_swIntentDroppedRebind = 0;  // swing intents destroyed by a rebind - a TERM in the
                                        // `intents = …` identity, not a diagnostic
long long g_swUnscoredRebind      = 0;  // swing writes destroyed by a rebind - a TERM in the
                                        // `wrote = …` identity

void ResetCombatWatchRow(CombatWatch* w, unsigned int uid)
{
    // **A REBIND DESTROYS OUTSTANDING WORK, AND THE REPORT PRINTS TWO IDENTITIES THAT WOULD STOP
    // CLOSING IF IT VANISHED SILENTLY.**
    //
    //   intents = wrote + intentExpired + intentSuperseded + intentAbsorbed + intentsPendingNow
    //   wrote   = LANDED + DECLINED + unscoredRewritten + unscoredStale + writesPendingNow
    //
    // The `…PendingNow` terms exist (F354) precisely because an intent or write on a puppet that
    // streams out and never returns "is scored by nothing at all". Zeroing the two flags here
    // without a term would remove the item from `pendingNow` with nothing to compensate, and both
    // identities would fail to close with **no way for the reader to attribute the gap** - the
    // F343 class of failure the identity lines were written to make impossible.
    //
    // **Rebinds were impossible before the slot-indexed rewrite, so this leak never existed.** It
    // is created by that change and is counted by it in the same commit.
    if (w->swingIntent)  ++g_swIntentDroppedRebind;
    if (w->swingPending) ++g_swUnscoredRebind;

    // H015 / P059 - a rebind while the window is open would leave the old character's AI RELEASED
    // with no row to close it. Re-gate by the OLD uid - this runs BEFORE `w->uid` is overwritten
    // (review finding 1: the first version ran after it and gated the NEW uid's character).
    if (w->nativeOn)
    {
        const unsigned int oldUid = w->uid;
        ::Character* old = FindSpawned(oldUid);
        if (PlausibleObject(old)) { SuppressCharacter(old, true); if (!IsSuppressed(old)) ++g_ncGateRefusedOnOrphan; }
        SetPuppetNativeWindow(oldUid, false);
        ++g_ncOrphanedByRebind;
        ErrorLog("[NC] window ORPHANED by rebind oldUid=" + N3(oldUid) + " newUid=" + N3(uid)
                 + " - old character re-gated by uid (P059)");
    }
    w->nativeOn = 0; w->nativeOpenTick = 0; w->nativeSwings = 0; w->nativeTarget = 0;
    w->mvLastX = w->mvLastY = w->mvLastZ = 0.0f; w->mvPrimed = 0; w->mvVisMax = 0.0f; w->mvVisMaxRate = 0.0f; w->mvVisMaxRateDowned = 0.0f; w->mvLastAt = 0.0; w->mvFrames = 0;
    w->uid        = uid;
    w->lastOn     = -1;   // never sampled, so the first read is not an edge
    w->lastTarget = 0;
    // **-1 IS LOAD-BEARING HERE FOR THE SAME REASON `lastOn` USES IT, AND THE SWING EDGE TEST HAD
    // TO BE TAUGHT ABOUT IT.** A rebind mid-swing leaves `lastState = -1`, and the send condition
    // is `st == SWORD_SWING && lastState != SWORD_SWING` - which is TRUE at -1, so the authority
    // would announce a blow that had already started. See the `lastState != -1` guard at the send.
    w->lastState  = -1;
    w->lastPeerMode = -1;
    // **P030/F246 GOES BLIND UNTIL THE NEXT INBOUND EDGE, AND ITS SILENCE LOOKS LIKE ABSENCE.**
    // `weWantOn` is only ever written by an inbound COMBATMODE message, and the probe requires it
    // to be 0 or 1 - at -1 neither arm can fire. The probe reports through an `ErrorLog` rather
    // than a counter, so a blinded probe is indistinguishable from one that found nothing, which
    // is the one-sided-detector failure F248 exists to prevent. Counted rather than assumed rare.
    if (w->weWantOn != -1) ++g_cmWatchReboundBlindedP030;
    w->weWantOn     = -1;
    w->swingPending   = 0;
    w->swingIntent    = 0;
    w->swingIntentTick = 0;
    w->swingIntentTarget = 0;
    w->swingIntentGate = 0;
    w->swingIntentLastTry = 0;
    w->swingWroteTick = 0;
    w->swingTarget    = 0;
    // P045 - reset here too, even though `g_cmWatch` starts zeroed and no slot is ever released,
    // so this block is currently unreachable for these fields. It is written anyway because the
    // ONE change F352 requires - reclaiming watch slots so `CombatModeTick` can become O(1) - turns
    // every field omitted here into a silent stale-data bug on the day it lands, in a table whose
    // whole job is to be believed. Cheaper now than as the finding after that run.
    w->ccSamples            = 0;
    w->ccBaseTicks          = 0;
    w->ccAiTicks            = 0;
    w->ccAiUnreadable       = 0;
    w->ccLastStateTimerBits = 0;
    w->ccLastAiTimerBits    = 0;
    w->ccPrimed             = 0;
    w->ccMine               = -1;
    w->ccIsAI               = -1;
    w->ccLastSampleTick     = 0;
    w->ccLastObj            = 0;
    w->ccReprimed           = 0;
    w->ccUpdSlot            = 0;
    w->ccUpdCarried         = 0;
    w->ccUpdAtPrime         = 0;
    w->ccUpdLastSeen        = 0;
    w->ccFocusedTaskFrames  = 0;
    w->ccMeleeTaskFrames    = 0;
    w->ccAnyTaskFrames      = 0;
    w->ccNoBodyFrames       = 0;
    w->ccGoFtNonZero        = 0;
    w->ccBodyFtMoved        = 0;
    w->ccBodyFtNonZero      = 0;
    w->ccLastBodyFtSeen     = 0;
    w->ccBodyFtPrimed       = 0;
    w->ccTargetChanges      = 0;
    w->ccTargetCleared      = 0;
    w->ccLastTarget         = 0;
    w->ccTargetPrimed       = 0;
    w->ccLastBodyFtBits     = 0;
    w->ccUpdRebinds         = 0;
    w->ccWepFrames          = 0;
    w->ccWepUnreadable      = 0;
    w->ccWepImplausible     = 0;
    w->ccWepLastVt          = 0;
    w->ccUnconFrames        = 0;
    w->ccDeadFrames         = 0;
    w->ccNoArmsFrames       = 0;
    w->ccAnimalFrames       = 0;
    w->ccReadyPartial       = 0;
    w->swingReported  = 0;
}

// O(1). `CombatModeTick` holds the mirror slot index from its own walk and passes it straight in.
CombatWatch* CombatWatchAt(int slot, unsigned int uid)
{
    if (slot < 0 || slot >= kMaxCombatWatch)
    {
        // **MUST STAY ZERO.** Reachable only if the mirror is bigger than this table, which the
        // one-time check in `CombatModeTick` shouts about at startup.
        ++g_cmWatchBadSlot;
        static bool s_said = false;
        if (!s_said)
        {
            s_said = true;
            ErrorLog("[M6] combat watch slot " + N3((long long)slot) + " is outside the table ("
                     + N3((long long)kMaxCombatWatch) + ") - uid " + N3((long long)uid)
                     + " gets NO combat-mode edges, and neither does any later slot this high."
                     " **The table must be at least MirrorCapacity().** Reported once.");
        }
        return 0;
    }
    // 0 is the empty-row marker, not a uid. Counted rather than returned silently: a mirror row
    // holding uid 0 with a live object would get no combat-mode edges and no P045/P056 row, and
    // "unreachable today" is the same state `watchBadSlot` was given a counter for three lines up.
    if (uid == 0) { ++g_cmWatchZeroUid; return 0; }

    CombatWatch* w = &g_cmWatch[slot];
    if (w->uid == uid) return w;     // the ordinary case, and it is one compare

    // Either a fresh slot (uid 0) or one the engine reused for a different character. The second
    // is a REBIND and is counted, because it narrows every per-uid number on that row.
    if (w->uid != 0) ++g_cmWatchRebound;
    ResetCombatWatchRow(w, uid);
    return w;
}

// The uid-only entry point, kept for the two callers that have a uid and no slot - an inbound
// COMBATMODE message and the swing-intent path. **It resolves the uid to a MIRROR slot, not to a
// free one**, so the one-row-per-mirror-slot invariant holds no matter which door was used.
//
// It is O(n), and that is deliberate: **both callers are per-message, not per-frame.** Making this
// O(1) too would mean a second index to keep in step with the first, which is the class of thing
// F267 was about.
//
// **The alternative - making this lookup-only and refusing to create - was considered and
// rejected.** Both call sites already tolerate a null, so it would have compiled and run; it would
// also have silently dropped `weWantOn` and the swing intent for any character whose first
// COMBATMODE arrives before `CombatModeTick` has ever walked it. That is a behaviour change
// wearing a refactor's clothes, in exactly the window where combat mode is most likely to arrive.
CombatWatch* CombatWatchFor(unsigned int uid)
{
    if (uid == 0) { ++g_cmWatchZeroUid; return 0; }
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int slotUid = 0;
        if (!MirrorSlot(i, &slotUid, 0)) continue;
        if (slotUid == uid) return CombatWatchAt(i, uid);
    }
    // Not an error: a uid can be retired or destroyed between the message arriving and this walk.
    // Counted so that "the watch never saw it" is a number rather than an absence.
    ++g_cmWatchNoMirror;
    return 0;
}

// Character -> CombatClass, with the guard the engine does not do for us. F205:
// getCombat is MOV RAX,[RCX+0x648]; MOV RAX,[RAX+8]; RET - it dereferences CharBody
// without checking it, so CharBody is validated here before the call.
CombatClass* CombatOf(::Character* c)
{
    if (!PlausibleObject(c)) return 0;
    void* body = *(void**)((char*)c + 0x648);
    if (!PlausibleObject(body)) return 0;
    CombatClass* cc = (CombatClass*)c->getCombat();
    if (!PlausibleObject(cc)) return 0;
    return cc;
}

// ---------------------------------------------------------------------------------------------
// P056 / F390 - **THE ACTOR'S READINESS. WHAT THE REQUIREMENTS CHECK ACTUALLY ASKS FOR.**
//
// T098 measured 36 install attempts and 36 refusals, all located inside
// `CharBody::startAction(Tasker*)` at `TaskData::_isRequirementsComplete` (0x60EEB0).
// The requirement that can refuse names the actor's readiness, and its first hard clause is
// `AI::hasWeaponEquipped` - which is **not** a faction test, a distance test or anything about the
// target. It asks whether the character has a weapon **drawn into its hands**.
//
// **THIS PROBE READS THAT FIELD AND NOTHING ELSE. NO ENGINE CALL, NO BEHAVIOUR CHANGE.**
//
// #### THE FIELD IS AT A DIFFERENT OFFSET ON EACH SUBCLASS, AND THAT IS THE WHOLE DIFFICULTY
//
// `CharacterHuman::weaponInHands` is at **+0x6D8**; `CharacterAnimal::weaponInHands` is at
// **+0x708** (both from the shipped headers). **Reading 0x6D8 on an animal reads the wrong field**
// and would produce a confident number about the wrong memory. The engine avoids this by
// dispatching the pure virtual `Character::getCurrentWeapon`, vtable slot **+0x3C0** - confirmed
// from `AI::hasWeaponEquipped`'s own bytes at 0x595FA0:
//
//     48 8b 89 f8 02 00 00    mov rcx,[rcx+0x2F8]      ; AI -> Character
//     48 8b 01                mov rax,[rcx]            ; its vtable
//     ff 90 c0 03 00 00       call qword ptr [rax+0x3C0]
//
// #### AND WE STILL DO NOT CALL IT
//
// Both overrides are **plain field reads**, verified by reading the exe bytes rather than a
// decompile:
//
//     CharacterHuman::getCurrentWeapon  0x5C8D00:  48 8b 81 d8 06 00 00 c3   mov rax,[rcx+0x6D8]; ret
//     CharacterAnimal::getCurrentWeapon 0x5E14E0:  48 8b 81 08 07 00 00 c3   mov rax,[rcx+0x708]; ret
//
// So instead of calling, this **decodes the offset out of the getter and reads the field
// directly.** It follows the vtable slot, follows an incremental-link thunk if there is one
// (F264), and then **requires the body to be exactly `mov rax,[rcx+imm32]; ret` before trusting
// it.** If the pattern does not match - a subclass we have not seen, a hooked slot, anything -
// it reports UNREADABLE rather than reading a byte it cannot account for.
//
// **That is P053's standard applied a second time.** There, a virtual call was dropped because it
// could not be certified side-effect-free. Here the call CAN be certified - and once it is
// certified as a field read, the call is not needed. The certification and the read are the same
// eight bytes.
//
// Returns: the Weapon* (0 = no weapon drawn), or `kWeaponUnreadable` if any step declined.
const void* const kWeaponUnreadable = (const void*)~(uintptr_t)0;
const size_t kVfGetCurrentWeapon = 0x3C0;   // Character vtable slot, from 0x595FA0's own bytes

// **AND THE OFFSET IT DECODES IS ALSO THE ANIMAL TEST, FOR FREE.**
//
// The audit of the engine's readiness predicate turned up something this probe needed and did not
// have: **`isReadyForAction` skips the whole medical block for an animal** - `isAnimal()` non-null
// jumps past the unconscious / dead / arms checks entirely. So scoring an animal against those
// bytes measures clauses the engine never consults, and `readyPartial` would read 0 for an animal
// that the engine would have accepted.
//
// The subclass is already in our hands: **+0x6D8 is `CharacterHuman`, +0x708 is `CharacterAnimal`,
// and those are the only two.** Verified against the engine's own test by reading both bodies:
//
//     CharacterAnimal::isAnimal  0x5E14C0:  48 8b c1 c3      mov rax,rcx ; ret   -> returns THIS
//     Character::isAnimal        0x5E4EC0:  33 c0 c3         xor eax,eax ; ret   -> returns NULL
//
// So `off == 0x708` is exactly `isAnimal() != 0`, with no call. `offOut` carries it out.
const void* WeaponInHandsOf(const void* c, unsigned int* offOut)
{
    if (offOut) *offOut = 0;
    if (!PlausibleObject(c)) return kWeaponUnreadable;

    const uintptr_t base = g_imageBase;
    if (base == 0) return kWeaponUnreadable;
    const uintptr_t lo = base, hi = base + 0x4000000;   // the same window PlausibleObject uses

    __try
    {
        const unsigned char* const* vt = *(const unsigned char* const* const*)c;
        if ((uintptr_t)vt <= lo || (uintptr_t)vt >= hi) return kWeaponUnreadable;

        const unsigned char* f = vt[kVfGetCurrentWeapon / sizeof(void*)];
        if ((uintptr_t)f < lo || (uintptr_t)f >= hi) return kWeaponUnreadable;

        // F264 - the exe is incrementally linked, so a vtable slot may point at a JMP thunk.
        if (f[0] == 0xE9) f = f + 5 + *(const int*)(f + 1);
        if ((uintptr_t)f < lo || (uintptr_t)f >= hi) return kWeaponUnreadable;

        // mov rax,[rcx+imm32] ; ret   - and nothing else. Anything else is not a field read.
        if (!(f[0] == 0x48 && f[1] == 0x8B && f[2] == 0x81 && f[7] == 0xC3))
            return kWeaponUnreadable;

        // **THE TWO OFFSETS THAT EXIST, AND NOTHING ELSE.**
        //
        // This was `off < 0x100 || off > 0x2000`, described in its own comment as "outside any
        // plausible layout". **The comment was false over most of the range it permitted:**
        // `Character` is 0x6D8 long and `CharacterHuman` ends around 0x700, so every value from
        // ~0x720 to 0x2000 is **past the end of the object** - and a read that lands on mapped
        // memory does not fault, so the `__except` below would not have caught it. It would have
        // become a `wepDrawn` count off ~6 KB of somebody else's memory.
        //
        // `CharacterHuman::weaponInHands` is +0x6D8 and `CharacterAnimal::weaponInHands` is +0x708.
        // **Those are the only two `: public Character` subclasses in the tree.** Naming them turns
        // the heuristic into the exact certification the block above claims to make.
        const unsigned int off = *(const unsigned int*)(f + 3);
        if (off != 0x6D8 && off != 0x708) return kWeaponUnreadable;

        // **THE OUT-PARAMETER IS WRITTEN AFTER THE READ THAT CAN FAULT, NOT BEFORE.** Written
        // first, a fault in the field read would leave `*offOut` set while the function returns
        // UNREADABLE - so that sample would count in `animal` AND in `wepUnreadable`. It could not
        // mislabel a human (the offset came from a genuine `CharacterAnimal` getter body) or relax
        // `readyPartial` (`wepOk` stays 0), but a counter that increments on a path the function
        // reports as failed is exactly the kind of thing read as data later.
        const void* const wep = *(const void* const*)((const char*)c + off);
        if (offOut) *offOut = off;
        return wep;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return kWeaponUnreadable;
    }
}

// P056 - the medical half of the readiness predicate: **THREE tests over these four bytes**, all
// inside `Character::medical`, a MedicalSystem VALUE member at +0x458. Offsets from the shipped
// `MedicalSystem.h`: `unconcious` +0x161, `dead` +0x164, `rightArmUsable` +0x165, `leftArmUsable` +0x166.
// The arms are ONE test, not two - the engine's `hasOneWorkingArm` (0x6436C0) is
// `[med+0x165] || [med+0x166]`, read from its own bytes.
//
// **AND ON AN ANIMAL ROW THIS PROBE COVERS NONE OF THEM.** `isReadyForAction` jumps past the whole
// medical block when `isAnimal()` is non-null (verified: the jump target is the null-hand
// construction immediately before the `hasWeaponEquipped` call, so it lands ON the weapon check,
// not past it). See `isAnimalRow` at the sample site.
//
// **The probe does not cover the whole predicate either.** The readiness function's tail also
// consults prone state, a crouching/lying test and `isCrippled()` - three virtual calls this probe
// deliberately does not make - and `hasWeaponEquipped` additionally rejects a **crossbow**.
//
// So `readyPartial` below is a NECESSARY-CONDITION counter. **And it is DERIVED from `wepDrawn`,
// so it inherits all three of that field's conditions:** zero means refused for certain **only**
// where `inCombatFrames > 0`, `wepUnreadable = 0` and `wepImplausible = 0`. A non-zero value never
// means it would pass. Named `partial` so no reader can take it for the verdict.
// ---------------------------------------------------------------------------------------------
// P057 / F394 - **THE SIXTH ATTEMPT: LET THE ENGINE DRAW THE WEAPON, THEN THROW AWAY ITS "NO".**
//
// T099 measured the cause: **the peer's copy of a fighting character has NO WEAPON IN ITS HANDS**
// (0 of 118,171 client samples; 129,631 of 129,631 on the host). The readiness requirement that
// gates a melee task tests exactly that.
//
// **AND THE ENGINE HAS A TASK FOR IT THAT COSTS NOTHING TO RUN.** `TASK_DRAW_WEAPON` does all its work
// in `startAction`, which `setCurrentAction` calls **synchronously at install time**; its
// `runAction` and `endAction` are both a bare `ret` (0x334900 / 0x334910, `c2 00 00`, read from the
// exe). **So it needs no AI pass, no `periodicUpdate`, and no per-frame driver** - which is the
// entire reason the first five attempts failed. `needsTarget = 0`, so a null target is legal; its
// two requirements are *not swimming* and *one working arm*; and **it applies the crossbow
// exclusion itself**, which is the one weapon class the readiness check rejects.
//
// **THE ENUM CONFIRMS 6 BY POSITION, INDEPENDENTLY OF THE TASK TABLE** the offline pass decoded:
// NULL_TASK, MOVE_ON_FREE_WILL, BUILD, PICKUP, MELEE_ATTACK, TASK_MELEE_FOCUSED, **TASK_DRAW_WEAPON**.
//
// #### AND ON ITS OWN IT WOULD DO NOTHING, WHICH IS THE HALF THAT IS EASY TO MISS
//
// `TaskData::_isRequirementsComplete` consults a memo at **`AI+0x238`** BEFORE evaluating anything,
// inserts on failure, and is cleared only by `AI::periodicUpdate` - **which this mod suppresses for
// puppets.** The key is rebuilt byte-identically on every re-issue, so **the cached `false` answers
// forever.** Arming the character and re-asking would change nothing at all.
//
// **PAST TENSE FROM HERE - THE EVICTION DESCRIBED BELOW IS DELETED (F407). Kept because it is the
// clearest statement of what the cache DOES, which is still true and still what `MemoSizeOf` reads.**
// It evicted with `std::map::clear` at **0x515FF0**, which walks and frees the nodes into Ogre's pool,
// restores the sentinel and zeroes `_Mysize`. **It touches nothing outside the map object** - no
// destructors (both key and value are trivially destructible), no callbacks, no back-pointer to the
// `AI`. `_isRequirementsComplete` is the sole live reader and writer of that map **- which is true
// and was still MISREAD (F401): one function, but reached from TWO ungated worker-thread routes,
// so "only one function touches it" never meant "only one thread does" -** so a cleared
// entry perturbs exactly one function. Clearing THIS map alone is also the safe direction: an entry
// can be *derived from* a target-finder entry, so the unsafe order is the other way round.
//
// **THE KNOWN RISK, STATED BEFORE THE RUN RATHER THAN AFTER IT.** `startAction` calls `drawWeapon`,
// which reaches the Ogre skeleton attachment path, the animation class and the audio engine -
// synchronously, on **our** thread. The engine calls it from its own character update. This is the
// first thing this mod has driven that touches the renderer, and it is why the switch defaults OFF.
const size_t kAiOffInBody      = 0x50;    // CharBody -> AI
const size_t kReqCacheOff      = 0x238;   // AI -> resultsCache.requirementsCache (std::map)
const size_t kMapSizeOff       = 0x10;    // std::map -> _Mysize (map+0x8 is _Myhead)
// `kMapClearRva` (0x515FF0, `std::map<TaskMatch,bool>::clear`) is DELETED with the eviction it
// served - F407.
//
// **AND A CLAIM MADE WHEN IT WAS DELETED IS RETRACTED HERE: "nothing in this mod calls into the
// game's allocator any more" IS FALSE.** Every melee install this mod drives still does three, on
// the main thread:
//   1. the task factory allocates the `Tasker` - an `operator new` import;
//   2. `finishAction` runs the previous action's **scalar deleting destructor** (`mov edx,1;
//      call [rax]` at 0x5C5D0C) - an `operator delete`, and it zeroes `CharBody+0x68` AFTER the
//      free, not before;
//   3. `TaskData::_isRequirementsComplete` inserts into the map at `AI+0x238` - **the same
//      red-black tree F401 is about.** `[P057] evaluated=N` is literally a count of those
//      insertions: T101's 100 were 100 main-thread inserts into a tree the worker also inserts
//      into, every frame, ungated.
//
// **So deleting the eviction NARROWED F401's hazard; it did not retire it.** What is gone is the
// `clear` that freed every node at once. What remains is a concurrent insert - and, per (2), a
// concurrent free of a `Tasker` the worker latches into a register and holds across four nested
// calls (0x50E740). **Both still want the gate at `AITaskSytem::setCurrentTask` (0x50CAB0).**
// **THE REFUSAL CODE IS A FIXED GLOBAL, NOT A CALLER LOCAL** - the installer passes its address in
// the 7th argument slot (`LEA RCX,[0x14212ea6c]`). So it is readable without constructing a
// `Tasker`, and F375's warning about that overload's opposite ownership rules need not be paid.
// **It is SHARED SCRATCH written by five different functions, and neither the memo-hit path nor the
// unresolved-subtarget path writes it at all** - so a value read here is trustworthy ONLY on a
// frame where the memo size actually grew. That is what `memoGrew` below is for.
unsigned long long kFailedOnRva = 0; static coop::AddrReg kFailedOnRva_reg("FailedOn", &kFailedOnRva);   /* P8h: the address table fills this. Steam_1.0.65 0x212EA6C */

const size_t kMedicalOff       = 0x458;
const size_t kMedUnconciousOff = kMedicalOff + 0x161;
const size_t kMedDeadOff       = kMedicalOff + 0x164;
const size_t kMedRightArmOff   = kMedicalOff + 0x165;
const size_t kMedLeftArmOff    = kMedicalOff + 0x166;

// P057 - the requirements memo's entry count, from `CharBody`. **`(size_t)-1` means UNREADABLE**,
// which is a third answer and is kept distinct from 0: an empty memo and a memo we could not reach
// are opposite facts, and merging them would let a failed read read as *nothing was cached*.
const size_t kMemoUnreadable = (size_t)-1;

// P057 - the refusal code, in its own function because `__try` cannot live in one that unwinds
// C++ objects (MSVC C2712), which is what the apply path is full of. Returns -1 if unreadable.
int ReadFailedOn()
{
    if (g_imageBase == 0) return -1;
    __try { return *(const int*)(g_imageBase + kFailedOnRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

size_t MemoSizeOf(const void* body)
{
    if (!PlausibleObject(body)) return kMemoUnreadable;
    __try
    {
        const void* ai = *(const void* const*)((const char*)body + kAiOffInBody);
        if (!PlausibleObject(ai)) return kMemoUnreadable;
        return *(const size_t*)((const char*)ai + kReqCacheOff + kMapSizeOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kMemoUnreadable; }
}

// **`ClearMemoOf` WAS HERE AND IS DELETED. F407 / F401.**
//
// It called `std::map::clear` (0x515FF0) through a raw function pointer, on a map located by
// hand-computed offset, into the game's own allocator - the most dangerous thing this mod has ever
// run. **It is gone, not switched off, because it does not work AND is not safe:**
//
//   * **It does not work.** T101 measured `evaluated=100 servedFromCache=28 neverAsked=0` against
//     `attempts=128`. **78% of install attempts evaluated the requirements FRESH and refused
//     anyway**, so evicting a cache they never consulted could not have changed the outcome.
//     P057's founding claim - *"the cached `false` answers forever"* - is measurably false.
//   * **It is not safe, and the guard cannot be extended to make it so.** F401:
//     `AI::frameTick4` -> `AITaskSytem::update4Frame` -> `setCurrentTask` ->
//     `CharBody::startAction(Tasker*)` -> `TaskData::_isRequirementsComplete` **inserts
//     into that same red-black tree on three exit paths, every frame, on the AI worker thread,
//     needing no pulse.** `IsQuiescedForMemoryAccess` covers `AI::periodicUpdate` only, and its
//     soundness argument - *no new pass can BEGIN, because every site that arms a pulse is
//     main-thread* - does not extend to a function that needs no pulse at all.
//
// **T100 ran it for ~1,550 s without crashing. That is a narrow window that was not hit, not a
// safety record.** The repair that was on the table - a new gate on `AITaskSytem::setCurrentTask`
// (0x50CAB0), a hot path, unbuilt and unreviewed - **is not needed. Deleting code that does not
// work is strictly better than guarding it.**
//
// `MemoSizeOf` above is KEPT: a pure read, no call, and it is the instrument that settled U-13.

// ---------------------------------------------------------------------------------------------
// P058 / F399 - **THE TASKER'S OWN `taskData`, READ ON THE INSTALL FRAME.**
//
// F399 left three candidates for why an installed melee task is gone before the next sample, and
// **this read settles the second one on the frame it happens rather than a frame later.**
//
// Candidate 2 is that the `Tasker` is real but HOLLOW. The factory fills `Tasker+0x70` from the
// global task map at `0x1CE80F0` through an `operator[]`-style lookup, and that form **inserts a
// zero-valued node on a miss instead of failing** - so a map not yet populated on this instance
// yields a non-null `Tasker` carrying a **null `TaskData`**, which `CharBody::update` then reaps
// on its next pass. **A vtable check cannot see that state:** the object is exactly the right type
// and its contents are empty, which is precisely what T100's `ok` counter reported as success.
//
// `taskData+0x44` is the TaskType key - the same field `isSameTask` compares and the same one the
// memo comparator at 0x50BA30 orders by (F399). Two loads.
//
// **AND IT IS RUN ON BOTH INSTALLS ON PURPOSE - BUT THE CONTROL IS ACROSS ARMS, NOT WITHIN ONE.**
// `TASK_DRAW_WEAPON` demonstrably works (T100: `nowArmed = 26`) and `TASK_MELEE_FOCUSED` demonstrably
// does not, through the same factory on the same instance. If the equip task's `taskData` reads back
// populated and the melee task's reads NULL, that is candidate 2 confirmed with a control rather
// than by argument.
//
// **WHAT THAT CONTROL IS NOT, STATED HERE BECAUSE THE FIRST VERSION OF THIS COMMENT CLAIMED IT.**
// It is **not** the same character on the same frame. `taskweapon` fills the equip column and
// `taskinstall` fills the melee one, and **`(on, on)` IS FORBIDDEN** (transient-`Tasker`
// double-free), so **the two columns can never be non-zero in the same arm.** The comparison is
// between arms of one run over the same character population - weaker than a per-frame pairing, and
// it is the strongest thing available while that switch combination stays barred. A claim of
// same-frame pairing would have been read as controlling for the character's state, which it does
// not.
const size_t kTaskerTaskDataOff = 0x70;   // Tasker   -> TaskData*
const size_t kTaskDataKeyOff    = 0x44;   // TaskData -> TaskType key
// F402 / F404 - **DOOR 1, AND THE FIRST VERSION OF THIS COMMENT HAD IT EXACTLY BACKWARDS.**
//
// `AITaskSytem::update4Frame` has TWO paths to `bodyTaskComplete`, and F399 only had one. The first:
//
//     isDurationBased(+0x20) && timeOfDayHasPassed(AITaskSytem+0x288) && endsAfterTime(+0x21)
//
// **THE RETRACTED CLAIM, KEPT WHERE IT WAS MADE.** This said: *the expiry check returns true when
// the expiry is UNSET, and we never set one, so our task is reaped next frame by construction.*
// **The premise is false and the conclusion inverts.** `AITaskSytem::setTaskExpiryTimer` (0x50C6A0)
// writes `+0x288 = now + rand*durationFuzz + durationMin` **and is called from inside
// `CharBody::startAction(Tasker*)` at 0x5C672A**, on the success path, three instructions
// before `MOV AL,1`. Our install goes through that function. **So we DO set the expiry** - verified
// by decoding the `E9` thunk at that call site down to 0x50C6A0 by hand.
//
// **Which means `isDurationBased == 1` implies the expiry was just pushed INTO THE FUTURE, and door
// 1 is BLOCKED for the duration rather than open on the next frame.** A log line telling the
// operator the opposite, in bold, ahead of everything else, would have closed the investigation on
// a mechanism the binary says cannot fire yet.
//
// **So the two bytes are necessary and not sufficient, and the floats are what make them mean
// anything.** `durationMin` (+0x18) and `durationFuzz` (+0x1c) are the window: a small sum is a task
// that expires almost immediately and door 1 becomes a real explanation of T100's 1-11 frame
// lifetimes; a large one rules door 1 out for the observed window. They sit four bytes before the
// bytes this probe already reads, at the same site, for free.
//
// Offsets confirmed against the binary rather than the header - `TaskData::setDurationBased`
// disassembles to `movss [rcx+0x18],xmm1 / movss [rcx+0x1c],xmm2 / mov byte [rcx+0x20],1 /
// mov byte [rcx+0x21],r9b / ret`. **And its HEADER RVA (0x317690) is INT3 padding in this build;
// the resolver's 0x317B00 is the real one** - F397's rule, biting for the third time this week.
const size_t kTaskDataDurMinOff    = 0x18;  // TaskData -> float durationMin
const size_t kTaskDataDurFuzzOff   = 0x1C;  // TaskData -> float durationFuzz
const size_t kTaskDataDurationOff  = 0x20;  // TaskData -> bool  isDurationBased
const size_t kTaskDataEndsAfterOff = 0x21;  // TaskData -> bool  endsAfterTime
// **A COUNT IS NOT A BOUND, AND THIS CONSTANT WAS BOTH.**
//
// The first version read `253`, taken from F399's count of task-type REGISTRATION CALLS at load
// time (141 with `hasActionFunc` + 112 without). Using it as a maximum key value silently assumes
// the registered keys are contiguous `0..252` - which F399 never claims and nothing verifies.
//
// **`enum TaskType` has 291 members**, so the legal range is `0..290`
// (game/gamelayout.h TaskType_max). Everything from 253 up is
// an ordinary game task - `FOLLOW_URGENT_ESCAPE` is 253, `TASK_BREAK_GATE_ORDER` is 290. A puppet
// whose field comes back holding the AI's own job routinely carries a key in that range.
//
// **So the old bound made the alarm fire on correct readings**: a legal high-numbered task scored
// as `wild`, and the reading line told the operator every number on that line was meaningless when
// the offset was right and the answer was sitting there. Exactly backwards for an alarm.
const int    kTaskTypeEnumCount = 291;    // enum members, i.e. legal keys are 0..290

// **A STATUS RETURN AND AN OUT-PARAMETER, NOT A SENTINEL KEY.** Folding "unreadable" and "null" into
// negative key values would collide with a genuinely negative key read out of a wrong pointer - and
// a wild pointer is exactly the case this probe exists to notice. Two of this file's defects this
// week were sentinel collisions of that shape (the deleted `kMemoReadbackFailed` was one), so the
// pattern is
// avoided here rather than repeated and caught later.
enum TdStatus { TD_OK = 0, TD_NULLDATA = 1, TD_UNREADABLE = 2 };

TdStatus TaskDataKeyOf(const void* tasker, int* keyOut, int* durOut, int* endsOut,
                       float* durMinOut, float* durFuzzOut)
{
    // **-1.0f, NOT 0.0f.** A zero duration is the most interesting value this can take - it is what
    // would make door 1 fire immediately - so it must not be the same reading as "never read".
    //
    // **AND -1.0f IS NOT PROVABLY IMPOSSIBLE AS A REAL VALUE.** `setDurationBased` writes whatever
    // its caller passes, and its callers cannot be enumerated: there is **no direct `call` to it
    // anywhere in `.text`** (nor to `setCharacteristics`), so every task registration inlines it and
    // the constants are not reachable by an xref walk. A negative duration would be meaningless and
    // nothing suggests one exists - but that is an argument from plausibility, not a proof, and it
    // is recorded as such rather than asserted away.
    if (durMinOut)  *durMinOut  = -1.0f;
    if (durFuzzOut) *durFuzzOut = -1.0f;
    if (keyOut)  *keyOut  = 0;
    // **-1, NOT 0.** These are booleans, so 0 is a legitimate value and would be indistinguishable
    // from "never read" - which is how a task type that is NOT duration-based would look identical
    // to a failed read, and door 1 would be dismissed on the strength of an unread field.
    if (durOut)  *durOut  = -1;
    if (endsOut) *endsOut = -1;
    if (!PlausibleObject(tasker)) return TD_UNREADABLE;
    __try
    {
        const void* td = *(const void* const*)((const char*)tasker + kTaskerTaskDataOff);
        if (td == 0) return TD_NULLDATA;
        // **DELIBERATELY NOT `PlausibleObject`.** That predicate requires the first eight bytes to
        // be a pointer into the game image, i.e. a vtable - and `TaskData` is a plain registration
        // record whose first field is not required to be one. Using it here would reject every
        // VALID `TaskData` and report candidate 2's opposite. Bound the pointer and let SEH cover
        // the load instead.
        const uintptr_t v = (uintptr_t)td;
        if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull || (v & 0x7)) return TD_UNREADABLE;
        const int k = *(const int*)((const char*)td + kTaskDataKeyOff);
        if (keyOut) *keyOut = k;
        // F402 door 1. Read inside the same `__try` and AFTER the key, so a fault on either leaves
        // the whole result declined rather than half-filled.
        if (durOut)  *durOut  = *(const unsigned char*)((const char*)td + kTaskDataDurationOff);
        if (endsOut) *endsOut = *(const unsigned char*)((const char*)td + kTaskDataEndsAfterOff);
        // The window. Without these the two bytes above say door 1 is POSSIBLE and cannot say WHEN,
        // and "when" is the entire question after F404.
        if (durMinOut)  *durMinOut  = *(const float*)((const char*)td + kTaskDataDurMinOff);
        if (durFuzzOut) *durFuzzOut = *(const float*)((const char*)td + kTaskDataDurFuzzOff);
        return TD_OK;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return TD_UNREADABLE; }
}

// One set of counters per install path, so the two can be compared rather than summed.
struct TaskDataProbe
{
    long long nullData;   // **the Tasker is real and its taskData is NULL** - candidate 2
    long long unread;     // the Tasker or the record would not read
    long long wild;       // a key outside 0..252, i.e. that pointer is not a TaskData at all
    long long keyMelee;   // 5  - TASK_MELEE_FOCUSED
    long long keyEquip;   // 6  - TASK_DRAW_WEAPON
    long long keyOther;   // a readable, in-range key that is neither
    int       lastKey;    // for the report; -1 = never read one
    int       lastWild;   // the most recent out-of-range value, so a wrong offset is visible
    // F402 door 1. **-1 = never read**, because both are booleans and 0 is meaningful.
    // Constant per task type, so the LAST value read is the value for that type.
    int       lastDuration;
    int       lastEndsAfter;
    // F404 - **RENAMED FROM `door1Armed`, WHICH NAMED A TWO-THIRDS PREDICATE AFTER THE WHOLE THING.**
    // Door 1 is `isDurationBased && timeOfDayHasPassed(expiry) && endsAfterTime`, and this probe
    // reads neither the expiry nor the clock. It counts the two CONSTANT bytes only, so it says door
    // 1 is *possible for this task type*, never that it fired or is about to.
    long long door1Eligible;
    float     lastDurMin;    // -1.0f = never read. **0.0f is the interesting value, not the empty one**
    float     lastDurFuzz;
};
TaskDataProbe g_p58Melee = { 0, 0, 0, 0, 0, 0, -1, 0, -1, -1, 0, -1.0f, -1.0f };
TaskDataProbe g_p58Equip = { 0, 0, 0, 0, 0, 0, -1, 0, -1, -1, 0, -1.0f, -1.0f };

// **THE TASK THAT WAS ALREADY THERE GETS ITS OWN STRUCT, NOT A SHARE OF THE MELEE ONE.**
// `ScoreTaskData` is fed whatever `CharBody+0x68` holds after the call, and that is three different
// situations: the task OUR call installed, a task that was already in the field and survived
// (`okAlready`), and some other task entirely. Only the first is evidence about our install.
// Scored together, a run where the field already held a melee task would report `keyMelee > 0` -
// **"the record is populated, so candidate 2 is dead"** - on a Tasker this mod never created.
// The split is by object identity against the pre-call read, which is exact and needs no inference.
TaskDataProbe g_p58Preexisting = { 0, 0, 0, 0, 0, 0, -1, 0, -1, -1, 0, -1.0f, -1.0f };

// **AND A FOURTH, BECAUSE THE IDENTITY SPLIT ALONE IS TYPE-BLIND.**
//
// Splitting on `nowTask != wasTask` says our call changed the field. It does **not** say the field
// now holds a TASK_MELEE_FOCUSED `Tasker` - `installedPlainMelee` (TaskType 4, which this file
// keeps a dedicated counter for because it happens), `installedOther`, and an unreadable non-null
// pointer all satisfy it. Pooled, they would land in the MELEE column, whose reading line asserts
// *"a real `Tasker` **of the right type**"*.
//
// **And that assertion is unfalsifiable by the very measurement that makes it:** when `taskData` is
// NULL, the key is precisely the field that cannot be read. So `nullData` can never carry type
// information from the record - it has to come from the vtable, which is already computed at the
// call site and was simply not used for attribution.
//
// **The failure this prevents is the one F399 is about.** If the ungated `AI::frameTick4`
// replaces our task between our call returning and our read, `nowTask != wasTask` holds, and the
// AI's own job with a null `taskData` would have scored as `MELEE NULLTASKDATA` - read as
// *candidate 2 confirmed, the melee factory returned a hollow Tasker* - when the truth is
// **candidate 1**, the other candidate entirely, pointing at a different fix.
TaskDataProbe g_p58OtherNew = { 0, 0, 0, 0, 0, 0, -1, 0, -1, -1, 0, -1.0f, -1.0f };

// **ONE FORMATTER, FOUR COLUMNS. THE HAND-REPEATED VERSION HAD ALREADY DRIFTED TWICE.**
//
// The columns were four copies of the same field list, and both times a field was added it reached
// some copies and not others - `key6`/`lastWild` missing from OTHER-NEW, then the three door-1
// fields printed for MELEE alone while all four columns collected them. That is the same defect
// F392 recorded at a different scale: two copies of one rule, and the fix lands in one of them.
// A helper makes the next field impossible to add asymmetrically.
//
// `emphasise` wraps the door-1 group in asterisks for the MELEE column only - the ONE thing that
// legitimately differs between columns, and it changes no field name, so every column stays
// greppable by the same token.
std::string TaskDataColumn(const char* label, const TaskDataProbe& p, bool emphasise)
{
    const char* e = emphasise ? "**" : "";
    return std::string("  ||  ") + label
         + " key5=" + N3(p.keyMelee)
         + " key6=" + N3(p.keyEquip)
         + " keyOther=" + N3(p.keyOther)
         + " " + e + "nullTaskData=" + N3(p.nullData) + e
         + " unreadable=" + N3(p.unread)
         + " wildKey=" + N3(p.wild)
         + " lastKey=" + N3((long long)p.lastKey)
         + " lastWild=" + N3((long long)p.lastWild)
         + " " + e + "isDurationBased=" + N3((long long)p.lastDuration)
         + " endsAfterTime=" + N3((long long)p.lastEndsAfter)
         + " door1Eligible=" + N3(p.door1Eligible)
         // **THE UNIT IS IN THE NAME. F405, and this project's own rule.** These are in-game HOURS,
         // not seconds and not frames - `setTaskExpiryTimer` adds them to a value it takes straight
         // from `GameWorld::getTimeStamp_inGameHours`. Named `durationMin`/`durationFuzz`, a reader
         // invited to compare them against "1-11 frame lifetimes" would read them as seconds, which
         // is the natural default and is wrong by an unknown factor.
         + " durationMinGameHours=" + F6(p.lastDurMin)
         + " durationFuzzGameHours=" + F6(p.lastDurFuzz) + e;
}

void ScoreTaskData(TaskDataProbe* p, const void* tasker)
{
    int k = 0, dur = -1, ends = -1;
    float durMin = -1.0f, durFuzz = -1.0f;
    const TdStatus st = TaskDataKeyOf(tasker, &k, &dur, &ends, &durMin, &durFuzz);
    if (st == TD_UNREADABLE) { ++p->unread;   return; }
    if (st == TD_NULLDATA)   { ++p->nullData; return; }
    if (k < 0 || k >= kTaskTypeEnumCount) { ++p->wild; p->lastWild = k; return; }
    p->lastKey        = k;
    p->lastDuration   = dur;
    p->lastEndsAfter  = ends;
    p->lastDurMin     = durMin;
    p->lastDurFuzz    = durFuzz;
    // F402/F404 - the two CONSTANT bytes of door 1. Recorded as a count rather than only as the last
    // pair, so a mixed population (two task types reaching one column) cannot be hidden by whichever
    // was read last. **`Eligible`, not `Armed`** - the third term is a clock this probe never reads.
    if (dur == 1 && ends == 1) ++p->door1Eligible;
    if      (k == 5) ++p->keyMelee;
    else if (k == 6) ++p->keyEquip;
    else             ++p->keyOther;
}

// ---------------------------------------------------------------------------------------------
// P058 / F399 - **WHO ENDS THE TASK. THE ONE MEASUREMENT THAT NAMES THE CAUSE INSTEAD OF RANKING
// CANDIDATES.**
//
// `CharBody::finishAction` (0x5C5CE0) has **no direct code callers** - every removal of a current
// action in the whole binary funnels through vtable slot +0x60 into this function. So the caller's
// return address is not *a* clue, it is *the* answer, and it costs one intrinsic.
//
// **DELIBERATELY NOT PRE-BUCKETED INTO THE THREE CANDIDATES.** F399 predicts `0x50CD4x` (the ungated
// `AITaskSytem::bodyTaskComplete`), `0x5C63xx` (a null `taskData`) and `0x5C64xx` (an install
// evicting it). Writing those three ranges into the probe would mean the probe can only ever return
// one of my three guesses, and **a fourth caller - which is exactly the thing five attempts kept
// missing - would land in an "other" bucket carrying no information at all.** So this records the
// DISTINCT return-address RVAs it actually sees, with counts, and the reading happens in the log.
// The candidate ranges are printed in the READING IT line as an aid, not applied as a filter.
//
// **THREADING: THE AI WORKER THREAD, ESTABLISHED (F403) RATHER THAN ASSUMED.** `finishAction` is
// reached from `AI::frameTick4`, which F403 traced to `Character::threadedUpdate4` on the
// thread F062 MEASURED as the AI worker. So this detour does interlocked counters and aligned
// pointer compares ONLY - no logging, no allocation, no locks, no engine calls. That rule is what
// `ai_spike.cpp` was built under, and here it is a requirement rather than a precaution.
const int kMaxEndSites = 24;
struct EndSite
{
    volatile LONG64 rva;          // return-address RVA, 0 = free slot
    volatile LONG64 hits;         // calls from this site, every character in the world
    volatile LONG64 watchedHits;  // ... of those, on a CharBody we installed a melee task into
};
EndSite g_endSites[kMaxEndSites] = {0};
volatile LONG64 g_endSiteOverflow = 0;   // calls from a site that found no free slot
// **AND THE SAME THING FOR A WATCHED BODY, BECAUSE THAT ONE IS THE ANSWER BEING THROWN AWAY.**
// The 24 slots are claimed first-come by every character in the world; the site we care about fires
// only on the handful of frames where a melee install happens. If ambient callers take all 24 first,
// every later call from OUR site lands in the overflow and its `watchedHits` stays 0 forever - which
// reads as *nothing in the engine removed our task*, the exact false negative the `endCallsAll = 0`
// warning exists to prevent, arriving through a different door. The flag is already computed one
// line up; dropping it on this path made the failure silent.
volatile LONG64 g_endSiteOverflowWatched = 0;
// P058 - **WHICH THREAD. NOW ESTABLISHED OFFLINE, AND STILL RECORDED.**
//
// This detour's restrictions were originally justified with *"threading is assumed hostile because
// it is not yet established"* - while the probe collected nothing that would establish it.
// **F403 then settled it from the binary:** all 3,029 `call [reg+0x18]` sites were scanned and
// exactly one loads `Character+0x650`, inside `Character::threadedUpdate4`, dispatched from the
// same function that drains `threadedUpdate` - **the thread F062 MEASURED as the AI worker.** So
// the restrictions were right, and they are no longer an assumption.
//
// **Kept anyway, because offline and in-process are different claims.** F403 says which call site
// exists; this says which thread actually ran, in this process, on this build. The two have
// disagreed before in this project, and a one-line interlocked exchange is the cheapest possible
// way to notice.
volatile LONG   g_endThreadId = 0;
volatile LONG64 g_endCallsAll     = 0;   // **every call. A zero here means the HOOK did not arm**
volatile LONG64 g_endCallsWatched = 0;
// **THIS IS THE CALIBRATION CHANNEL, NOT A LEFTOVER BUCKET.**
//
// The whole probe rests on `_ReturnAddress()` inside a detour giving the ENGINE's call site, which
// is true for a jump-patch hook and false for anything that wraps the call in another frame. That
// assumption is worth exactly nothing unverified - and it does not have to be, because **this mod
// calls `CharBody::finishAction` itself, from exactly one place** (the combat-exit retraction),
// and those calls arrive through the same patch with a return address inside OUR DLL, i.e. outside
// the game image.
//
// So: `retOutsideImage` should equal `ended + endedEmpty` from the [P055] line. **If it reads 0
// while those are non-zero, `_ReturnAddress()` is not returning the caller and EVERY RVA in the site
// table is meaningless** - that is the first thing to check in the log, ahead of any conclusion.
volatile LONG64 g_endNoImageBase  = 0;   // and it is the calibration count - see above
volatile LONG64 g_endLastOutsideRet = 0; // one such address, so a human can see it is in our DLL
// **SPLIT OUT, AND THE REASON MATTERS MORE THAN THE COUNTER.** A call arriving before the image base
// was cached would also fail the in-image test, and folding it into the count above would let *the
// probe not being ready* read as *our own DLL called it* - which is the calibration signal itself.
// Two meanings in one counter is the defect this file has already paid for twice.
// **It should be structurally 0** (both install functions run at plugin start, long before any
// `CharBody` exists), and it is printed anyway: "impossible in practice" is exactly the reasoning
// that produces a silent conflation, and a counter that reads 0 forever costs nothing.
volatile LONG64 g_endBaseNotReady = 0;

// The bodies we installed a melee task into. **Compared, never dereferenced** - so a stale entry
// for a character that has since been freed cannot fault. It can, in principle, alias a NEW object
// allocated at the same address and mis-attribute one call; the ring is small and the run is short,
// and the alternative (dereferencing engine objects on an unknown thread to validate them) is a far
// worse trade. **Stated here rather than discovered in a review.**
const int kMaxEndWatch = 64;
void* volatile g_endWatch[kMaxEndWatch] = {0};
volatile LONG  g_endWatchNext  = 0;   // ring cursor
volatile LONG  g_endWatchAny   = 0;   // 0 until the first stamp, so the scan is skipped entirely
// **`wrapped` MEANS A DISTINCT BODY WAS EVICTED - AND IT ONLY MEANS THAT BECAUSE OF THE DEDUPE.**
// Without the scan below, this incremented on every stamp past the 64th even when the slot it
// overwrote held the SAME pointer, which for a small puppet population is nearly every stamp: a
// 300-install run against one puppet would have printed `watchRingWrapped = 237` and told the reader
// history had been lost, when the ring held that one body throughout and the count was exact. The
// line above already says `bodiesWatched` counts STAMPS not distinct bodies; this quantity is
// derived from stamps and inherited none of it.
volatile LONG64 g_endWatchWrapped = 0;
volatile LONG64 g_endWatchRestamp = 0; // stamps for a body already in the ring - no slot consumed

// Main thread only, from the melee install site.
void WatchBodyForEndAction(void* body)
{
    if (body == 0) return;
    // **DEDUPE FIRST.** Main thread, 64 aligned compares, once per install - free at this rate, and
    // it is what makes both counters mean what their reading lines say.
    for (int i = 0; i < kMaxEndWatch; ++i)
    {
        if (g_endWatch[i] == body) { InterlockedIncrement64(&g_endWatchRestamp); return; }
    }
    const LONG i = InterlockedIncrement(&g_endWatchNext) - 1;
    const int slot = (int)(((unsigned long)i) % (unsigned long)kMaxEndWatch);
    if (g_endWatch[slot] != 0) InterlockedIncrement64(&g_endWatchWrapped);
    InterlockedExchangePointer((PVOID volatile*)&g_endWatch[slot], body);
    InterlockedExchange(&g_endWatchAny, 1);
}

bool IsWatchedBody(const void* body)
{
    // **A PLAIN READ, NOT AN INTERLOCKED ONE.** This was `InterlockedCompareExchange(&flag, 0, 0)`,
    // which implements a *read* as a `lock cmpxchg` - an unconditional locked read-modify-write on
    // one shared cache line, for every call from every character on every thread. The flag exists to
    // let this function skip work; that implementation gave back most of what it was for. An aligned
    // `volatile LONG` load is atomic on x64, which is what the detour already relies on one line
    // earlier when it reads `g_imageBase`.
    if (g_endWatchAny == 0) return false;
    for (int i = 0; i < kMaxEndWatch; ++i)
        if (g_endWatch[i] == body) return true;
    return false;
}

void (*orig_endAction)(CharBody*) = 0;

void detour_endAction(CharBody* self)
{
    // **WHY THIS WORKS - AND IT IS NOT ABOUT STATEMENT ORDER.** `AddHook` patches the target's
    // prologue with a jump, so a call reaches this detour through nothing but that jump: no
    // intervening frame, and the return address on the stack is the ORIGINAL caller's. (A call from
    // this DLL goes through gamecalls.cpp's forwarder, so its return address is in this DLL either
    // way - the outside-the-image branch below.)
    //
    // An earlier version of this comment said the intrinsic had to be the first statement or the
    // result could change. **That is false** - `_ReturnAddress()` resolves against the frame layout
    // the compiler already knows, and under `/O2 /GL` it is scheduled wherever the optimiser likes.
    // Harmless in itself, but it stated a rule that is not true, which invites a future session to
    // preserve it at the cost of a real change.
    const void* ret = _ReturnAddress();

    InterlockedIncrement64(&g_endCallsAll);
    if (g_endThreadId == 0)
        InterlockedExchange(&g_endThreadId, (LONG)::GetCurrentThreadId());

    const uintptr_t base = g_imageBase;
    if (base == 0)
    {
        // Not the same event as the one below, and kept apart - see the declaration.
        InterlockedIncrement64(&g_endBaseNotReady);
        orig_endAction(self);
        return;
    }
    if ((uintptr_t)ret <= base || (uintptr_t)ret >= base + 0x4000000)
    {
        // A return address outside the game image is not attributable to an engine call site - it
        // is our own DLL, and that is the calibration signal (see the declaration). Counted rather
        // than recorded, so it cannot consume a site slot and crowd out a real engine caller.
        InterlockedIncrement64(&g_endNoImageBase);
        InterlockedExchange64(&g_endLastOutsideRet, (LONG64)(uintptr_t)ret);
        orig_endAction(self);
        return;
    }

    const LONG64 rva     = (LONG64)((uintptr_t)ret - base);
    const bool   watched = IsWatchedBody(self);
    if (watched) InterlockedIncrement64(&g_endCallsWatched);

    for (int i = 0; i < kMaxEndSites; ++i)
    {
        LONG64 have = InterlockedCompareExchange64(&g_endSites[i].rva, rva, 0);
        // `have == 0` means the slot was free and we just claimed it; `have == rva` means it was
        // already ours. Any other value is somebody else's site - keep scanning.
        if (have == 0 || have == rva)
        {
            InterlockedIncrement64(&g_endSites[i].hits);
            if (watched) InterlockedIncrement64(&g_endSites[i].watchedHits);
            orig_endAction(self);
            return;
        }
    }
    // **A RACE CAN PUT THE SAME RVA IN TWO SLOTS** (two threads both find the table full of other
    // sites and both claim different frees). That costs a duplicate row in the report, which is
    // legible; it cannot lose a call. Nothing here needs a lock.
    InterlockedIncrement64(&g_endSiteOverflow);
    if (watched) InterlockedIncrement64(&g_endSiteOverflowWatched);   // the answer being discarded
    orig_endAction(self);
}

const char* SwordState(int s)
{
    switch (s)
    {
        case -1: return "none";
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

int CombatModeOf(const CombatClass* cc)
{
    return (int)*(const unsigned char*)((const char*)cc + kCombatModeActiveOff);
}

// F356 - **A NON-VIRTUAL DECLARATION BINDS TO ONE CLASS'S BODY, AND THAT IS NOT A NAMING QUIRK.**
//
// F354 added an `isAI` test so that reading `targetTraceBlocked` (a `CombatClassAI` member, eight
// bytes past the end of a `CombatClass`) would stop being correct-by-accident. It declared
// `bool isAI() const` on the `CombatClass` shim - which binds STATICALLY to
// `CombatClass::isAI` and **never** reaches `CombatClassAI`'s override. Disassembled in this
// build:
//
//     CombatClass::isAI    0x60BA60   XOR AL,AL ; RET      -> ALWAYS FALSE
//     CombatClassAI::isAI  0x60C0C0   MOV AL,1  ; RET      -> always true
//
// So the gate would have been shut for **every puppet, every frame, for the whole session**, the
// feature would have been completely inert, and every refusal would have been booked to a counter
// named after a condition that is false for every live object. That is the F351 headline defect
// reproduced by its own repair, through a different door - the address was resolved, the BINDING
// was not.
//
// The virtual is at **vtable slot +0x8** (header annotation, corroborated by
// `CircleStateAI::update`'s own `(**(code **)(*combat + 8))()` call). Dispatched by hand
// because the shim's vtable layout is not the engine's and cannot be made to be.
// **THE FAULT COUNTER IS A PARAMETER, AND MAKING IT ONE IS NOT TIDINESS.**
//
// Before P045 this function ran only on a swing attempt or a once-per-uid decline print - a
// handful of calls. P045 calls it **every frame, for every character in combat mode, including the
// ones this instance OWNS**, a population it had never touched. `g_swIsAIFaulted` is printed in the
// `[M6]` report right beside `gateNotAI` and is read as a swing-path diagnostic; one character
// tripping the range check would climb it by ~60/second and leave a five-figure number next to a
// single-digit `wrote=`. A reader checking the `[M6]` identities would conclude the swing dispatch
// is broken in a run where the swing path never faulted once.
//
// That is `verify-what-numbers-MEASURE` arriving through a call-site population rather than through
// a wrong offset: the counter stays correct about its own increments and stops answering the
// question its name asks. Sampling gets its own sink.
bool CombatIsAI(const CombatClass* cc, long long* faults = &g_swIsAIFaulted)
{
    // `PlausibleObject(cc)` has already checked that the vtable pointer lands inside the game
    // image; the slot itself is checked here for the same reason, because a garbage function
    // pointer is the one thing this call cannot survive.
    //
    // **THIS DISPATCHES AN ENGINE VIRTUAL, AND P045 WIDENED ITS CALL-SITE POPULATION TO EVERY
    // IN-COMBAT CHARACTER ON EVERY FRAME - INCLUDING OWNED ONES, WHICH IT HAD NEVER TOUCHED. SO
    // "SLOT +0x8 IS SIDE-EFFECT-FREE" IS NOW A CAP-COMPLIANCE CLAIM AND IS VERIFIED, NOT READ:**
    // the `CombatClassAI` vtable at RVA 0x16F5688 slot +0x8 holds a thunk to **0x60C0C0**, whose
    // whole body is `B0 01 C3` (`mov al,1 ; ret`). The base's is 0x60BA60 = `32 C0 C3`
    // (`xor al,al ; ret`). Register-only, no memory access, no side effect, at any frequency.
    // (Read straight out of the shipped `kenshi_x64.exe` - the header annotation and F356's
    // corroboration from `CircleStateAI` agree, and neither would have been enough on its own for a
    // claim the no-behaviour-change rule now rests on.)
    void** vt = *(void***)cc;
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    uintptr_t fn = 0;
    __try { fn = (uintptr_t)vt[1]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++*faults; return false; }
    // F358 - counted apart from a genuine "not an AI combat class". This was one counter for three
    // situations, and the test plan reads a non-zero `gateNotAI` as "the dispatch is wrong" - true
    // in spirit, but this says HOW in the same run instead of costing another one.
    if (fn <= base || fn >= base + 0x4000000) { ++*faults; return false; }
    typedef bool (*IsAIFn)(const CombatClass*);
    return ((IsAIFn)fn)(cc);
}

// P045 / T095 - SAMPLE THE TWO ENGINE-MAINTAINED TICK COUNTERS FOR ONE CHARACTER, ONE FRAME.
//
// Runs for OWNED characters as well as puppets, and that is the point: the owned characters are
// the control, and they are a control that can produce a non-zero reading (T094's `LANDED` control
// could not, and its headline comparison was therefore not a test).
//
// Nothing here writes engine memory and nothing here depends on `swingrepl`, so this measurement
// is taken in every arm of every run including the control arm.
void SampleCombatTick(CombatWatch* w, const ::Character* c, const CombatClass* cc,
                      bool mine, int inCombatMode)
{
    if (!inCombatMode) return;   // see `ccSamples` in CombatWatch for why the population is bounded

    // Raw bits, not the float/double values - see the CombatWatch comment. `stateTimer` is on the
    // BASE and readable off any CombatClass; `targetLastChangedTimer` is a CombatClassAI member
    // 0x38 bytes past the end of a base CombatClass and is read ONLY behind the same isAI test that
    // guards `targetTraceBlocked` (F358). An unguarded read there is the "in bounds by an accident
    // of the class hierarchy" pattern F354 set out to remove.
    const unsigned int stBits = *(const unsigned int*)((const char*)cc + kStateTimerOff);
    const bool isAI = CombatIsAI(cc, &g_ccIsAIFaulted);   // NOT g_swIsAIFaulted - see CombatIsAI
    unsigned long long aiBits = 0;
    if (isAI) aiBits = *(const unsigned long long*)((const char*)cc + kTargetLastChangedOff);

    w->ccMine = mine ? 1 : 0;
    w->ccIsAI = isAI ? 1 : 0;

    // Compare only against the IMMEDIATELY PRECEDING FRAME's sample of the SAME object. Any gap,
    // and any change of object, re-primes instead - see `ccLastSampleTick` in CombatWatch for why a
    // comparison across a gap would manufacture a "ticked" reading almost regardless of the truth.
    const bool contiguous = w->ccPrimed
                         && w->ccLastSampleTick == g_cmPulseTick - 1
                         && w->ccLastObj == (const void*)cc;
    // The call counter is maintained on EVERY sample including a re-prime, because it is a running
    // total and not a frame-to-frame comparison - the whole reason it survives the reset paths that
    // make the two timers blind. Only the timer comparison needs a contiguous predecessor.
    if (w->ccLastObj != (const void*)cc)
    {
        // A different combat object for the same uid. Bank what the old slot counted so a stream
        // out and back does not silently restart the total at zero - which would read as "never
        // ticked" for a character that had been ticking all along.
        // **THE LIVE SLOT IS ONLY SAFE TO BANK IF WE SAW THIS OBJECT A FRAME AGO, AND THE PREVIOUS
        // VERSION OF THIS LINE ASSUMED THAT INSTEAD OF TESTING IT.**
        //
        // Its comment said "we are switching away right now and are still sampling, so the live
        // count is current" — false in the case the code actually runs for. A puppet streams out at
        // frame 500, its combat object is destroyed, **the slot key is never released**, the
        // allocator hands that address to somebody else's `CombatClassAI`, and the detour happily
        // climbs that slot for a stranger. The puppet streams back at frame 5000 with a new object,
        // this line banks `slot->calls - atPrime`, and **thousands of a stranger's calls land in a
        // puppet's row.**
        //
        // **That is a false NON-ZERO, and it is the one direction nothing else in this build
        // guards.** Every other check here is pointed at a false zero. A contaminated puppet row
        // reads as *the state machine IS ticked on the peer* and would retire the leading
        // hypothesis on an artefact — the worst outcome available from this run, because it is the
        // one nobody would go back and re-check.
        //
        // So the freshness is now a condition rather than an assumption: live only when the last
        // sample was this frame or the previous one (the object was demonstrably alive then),
        // snapshot otherwise. And the change it replaced bought nothing anyway — the alternating
        // two-rows case it was written for crosses the bind limit within about two frames and
        // reports UNMEASURABLE regardless.
        if (w->ccUpdSlot != 0)
        {
            const bool fresh = (w->ccLastSampleTick >= g_cmPulseTick - 1);
            const long long end = fresh
                ? (long long)((const UpdSlot*)w->ccUpdSlot)->calls
                : w->ccUpdLastSeen;
            w->ccUpdCarried += end - w->ccUpdAtPrime;
        }
        long long prime = 0;
        UpdSlot* s      = UpdSlotFor((const void*)cc, &prime);
        w->ccUpdSlot    = (void*)s;
        w->ccUpdAtPrime = prime;
        w->ccUpdLastSeen = prime;
        ++w->ccUpdRebinds;
    }
    // **SNAPSHOT AT SAMPLE TIME, AND REPORT THE SNAPSHOT - NOT THE LIVE SLOT.**
    //
    // Slot keys are never released, so an address freed and reused by a different character keeps
    // the same slot, and a retired uid whose watch record still points there would go on
    // accumulating a stranger's calls right up to the moment the report is printed. Reading the
    // slot at REPORT time would attribute those to it. This reports calls as of the last frame we
    // actually saw this character, which is the only interval the row can honestly speak for.
    else if (!contiguous && w->ccUpdSlot != 0)
    {
        // **THE LAST WAY A STRANGER'S CALLS COULD REACH A PUPPET'S ROW, AND IT IS THE ONE A
        // POINTER TEST CANNOT SEE.**
        //
        // The rebind branch above catches a replacement combat object at a NEW address. If the
        // allocator hands the replacement the SAME address, `ccLastObj == cc` is true, no rebind is
        // recorded, `binds` stays 1 - the row still reads "measurable" - and the interval we did
        // not watch is imported whole, stranger's calls and all. F364 fixed this direction for
        // every case where the address changes, which is most of them, and left this one.
        //
        // A gap in sampling is the tell, and it is available without knowing anything about object
        // identity: **if we were not looking, we do not claim the interval.** Bank what we actually
        // observed and re-baseline to now.
        //
        // This under-counts, and under-counting is toward the study's expected answer - so it is
        // the wrong direction to bias by default. It is still the right trade here, because the
        // two errors are not symmetric: an under-count shows up as a puppet reading low against a
        // control that read high, which invites another look. **An over-count reads as "the state
        // machine IS ticked on the peer", retires the leading hypothesis, and nobody goes back to
        // check a result that looks like progress.** And gaps are rare - a character in combat and
        // in the mirror is sampled every frame.
        w->ccUpdCarried += w->ccUpdLastSeen - w->ccUpdAtPrime;
        w->ccUpdAtPrime  = (long long)((const UpdSlot*)w->ccUpdSlot)->calls;
    }

    if (w->ccUpdSlot != 0)
        w->ccUpdLastSeen = (long long)((const UpdSlot*)w->ccUpdSlot)->calls;

    if (!contiguous)
    {
        w->ccPrimed             = 1;
        w->ccLastSampleTick     = g_cmPulseTick;
        w->ccLastObj            = (const void*)cc;
        w->ccLastStateTimerBits = stBits;
        w->ccLastAiTimerBits    = aiBits;
        // F381 - re-primed WITH its siblings. The comment below used to claim the body read was
        // "hoisted above the contiguity test" so the baseline could be seeded live. **It was not,
        // and no such hoist exists** - so without this line a post-gap sample compared against bits
        // from before the gap. Clearing the flag is the same effect for one line and no hoist.
        w->ccBodyFtPrimed       = 0;
        ++w->ccReprimed;
        return;                      // no usable predecessor for the TIMER comparison
    }

    ++w->ccSamples;
    w->ccLastSampleTick = g_cmPulseTick;

    // P050 - THE CURRENT ACTION. Counted on the same frames as `ccSamples`, so `meleeTaskFrames`
    // and `updCalls` share a denominator and can be compared directly.
    //
    // Three pointer hops, each guarded, because a puppet mid-stream can have any of them null and
    // a fault here would take down a run that costs 25 minutes and two instances. Nothing is
    // written; `PlausibleObject` is the same validator every other read in this file uses.
    {
        const char* body = 0;
        if (PlausibleObject(c))
            body = *(const char* const*)((const char*)c + kCharBodyOff);
        if (!PlausibleObject(body)) { ++w->ccNoBodyFrames; }
        else
        {
            // P053 - THE PRECONDITION. **ONE PURE READ, AND THE CALL THAT WAS HERE IS GONE.**
            //
            // The first version also hand-dispatched `CharBody::getSquad()` (vtable slot +0x58)
            // to count null returns, on the theory that a null platoon is what makes
            // `CharBody::update` return early. **Disassembled before shipping, that function is
            // not an accessor:**
            //
            //     0x5C5BC0:  sub rsp,0x28 / mov rcx,[rcx+0x18] / cmp qword [rcx+0x658],0
            //                je +0xe / call ...
            //
            // It dereferences the Character and **makes a call** on one branch. **I cannot certify
            // that as side-effect-free, so under a rule that forbids behaviour changes it does not
            // ship** - not because it is known to be harmful, but because "probably fine" is not
            // the standard when the alternative costs nothing.
            //
            // **The measurement does not need it.** `platoonNull` was the suspected CAUSE;
            // `frameTIME` is the EFFECT, and the effect is what decides the question - if
            // `update` returns early for any reason, no installed task can ever run. A pure
            // field read answers that; a virtual call would only have named which reason.
            // **THE DECISION COUNTER. `bodyFtNonZero`, NOT `bodyFtMoved`.**
            //
            // The field is 0 at construction and `CharBody::update` assigns the frame delta to
            // it, so **non-zero means the update got past its platoon gate at least once.** That is
            // a LATCH and it is deliberately the decision, because **the precondition is a
            // STRUCTURAL question** - *can* an installed task ever run for a puppet - and a latch
            // answers a structural question exactly. It cannot answer *"is it running right now"*,
            // and nothing else on this line pretends to either.
            //
            // **"Did it CHANGE" is unsound and is kept only as colour.** `g_dt` is ceiling-clamped
            // to 0.05, so below 20 FPS - which two Kenshi instances on one machine reach easily -
            // the value is bit-identical every frame while the update runs perfectly.
            const unsigned int bodyFtBits =
                *(const unsigned int*)(body + kCharBodyFrameTimeOff);
            if (bodyFtBits != 0u) ++w->ccBodyFtNonZero;
            w->ccLastBodyFtSeen = bodyFtBits;

            // Colour. **The flag is CLEARED on a re-prime** (see the `!contiguous` branch), so the
            // first sample after a gap seeds a baseline instead of comparing across the unobserved
            // interval. An earlier version seeded a zero SENTINEL, which guaranteed a false move
            // after every gap; a version after that claimed the read was "hoisted above the
            // contiguity test" so the baseline could be seeded live - **there was never any such
            // hoist, and the claim sat in a commit message and a finding for an hour.**
            //
            // And the guard is `bodyFtPrimed`, a flag of its own. It was `ccPrimed` - which is
            // provably 1 here, since the `!contiguous` branch returns above this block - so it
            // suppressed nothing. Swapping it for `contiguous` would have been the same no-op by a
            // different name: **the free "moved" came from the zero baseline, not from a missing
            // guard, and my first diagnosis of it was inverted.**
            if (w->ccBodyFtPrimed && bodyFtBits != w->ccLastBodyFtBits) ++w->ccBodyFtMoved;
            w->ccLastBodyFtBits = bodyFtBits;
            w->ccBodyFtPrimed   = 1;

            const void* task = *(const void* const*)(body + kCurrentTaskOff);
            if (!PlausibleObject(task)) { /* no current action at all - counted by omission */ }
            else
            {
                ++w->ccAnyTaskFrames;
                // The Tasker's vtable pointer IS its identity here; comparing it against the two
                // known melee-task vtables is exact, where a name or an RTTI walk would be neither
                // cheap nor safe on this path.
                const uintptr_t vt = (uintptr_t)*(const void* const*)task;
                if      (vt == g_imageBase + kVtblFocusedMeleeAttack) ++w->ccFocusedTaskFrames;
                else if (vt == g_imageBase + kVtblMeleeAttack)        ++w->ccMeleeTaskFrames;
            }
        }

        // P054 - the engine's own target churn, on the same frames as everything else. Sampled
        // off `cc`, so its denominator is `ccSamples` and NOT `ccSamples - noBody`.
        {
            const void* tgt = *(const void* const*)((const char*)cc + kTargetOff);
            if (w->ccTargetPrimed && tgt != w->ccLastTarget)
            {
                ++w->ccTargetChanges;
                if (tgt == 0) ++w->ccTargetCleared;
            }
            w->ccLastTarget   = tgt;
            w->ccTargetPrimed = 1;
        }

        // P051 - the field `go` writes, read on the same frame. See kGoFrameTimeOff: this
        // exists to ARGUE AGAINST F367, and a host row with rising updCalls and this always zero
        // reopens the chain.
        const float goFt = *(const float*)((const char*)cc + kGoFrameTimeOff);
        if (goFt != 0.0f) ++w->ccGoFtNonZero;

        // P056 - **THE ACTOR'S READINESS, ON THE SAME FRAMES AS EVERYTHING ELSE.**
        //
        // Read off `c`, so the denominator is `ccSamples` and NOT `ccSamples - noBody` - a puppet
        // whose CharBody will not read still has a Character whose weapon field will.
        if (!PlausibleObject(c))
        {
            // **EVERY EARLY RETURN GETS A COUNTER.** Without this line the seven P056 fields are
            // skipped in silence and the row prints `wepDrawn=0 wepUnreadable=0` - which the report
            // states means the character genuinely had no weapon. It would be a fact about our read
            // wearing the costume of a fact about the character.
            ++w->ccWepUnreadable;
        }
        else
        {
            unsigned int wepOff = 0;
            const void* wep = WeaponInHandsOf(c, &wepOff);
            // See WeaponInHandsOf: +0x708 is `CharacterAnimal`, and that is exactly the engine's
            // own `isAnimal()` test. It matters because the engine SKIPS the medical block for an
            // animal, so scoring one against those bytes measures clauses it never consults.
            const bool isAnimalRow = (wepOff == 0x708);
            if (isAnimalRow) ++w->ccAnimalFrames;
            int wepOk = 0;
            if (wep == kWeaponUnreadable) ++w->ccWepUnreadable;
            else if (wep != 0)
            {
                // **THE POINTER IS VALIDATED BEFORE IT IS COUNTED, NOT AFTER.**
                //
                // This used to increment on `wep != 0` alone and validate one line later, only to
                // decide whether to capture the vtable. So a `weaponInHands` field holding non-null
                // garbage - the exact hazard on a half-streamed puppet - counted as *a weapon is
                // drawn*. **That is the direction that ends the workstream by accident:** the test
                // plan's own table reads `client wepDrawn > 0` as *the weapon is not the blocker,
                // go look elsewhere*, and nobody re-checks a result that looks like progress.
                if (PlausibleObject(wep))
                {
                    ++w->ccWepFrames;
                    wepOk = 1;
                    // The weapon's own vtable, so a CROSSBOW - the one drawn weapon that still
                    // fails the engine's check - is identifiable from the log without a second run.
                    const uintptr_t wvt = (uintptr_t)*(const void* const*)wep;
                    if (wvt > g_imageBase) w->ccWepLastVt = wvt - g_imageBase;
                }
                else ++w->ccWepImplausible;
            }

            const unsigned char uncon  = *(const unsigned char*)((const char*)c + kMedUnconciousOff);
            const unsigned char dead   = *(const unsigned char*)((const char*)c + kMedDeadOff);
            const unsigned char rArm   = *(const unsigned char*)((const char*)c + kMedRightArmOff);
            const unsigned char lArm   = *(const unsigned char*)((const char*)c + kMedLeftArmOff);
            if (uncon)            ++w->ccUnconFrames;
            if (dead)             ++w->ccDeadFrames;
            if (!(rArm || lArm))  ++w->ccNoArmsFrames;

            // **THE MEDICAL CLAUSES DO NOT APPLY TO AN ANIMAL, AND SCORING THEM AGAINST ONE WOULD
            // BE A COUNTER THAT MEASURES CLAUSES THE ENGINE SKIPS.** `isReadyForAction` jumps past
            // the whole unconscious/dead/arms block when `isAnimal()` is non-null; the weapon
            // clause below it still applies to both.
            if (wepOk && (isAnimalRow || (!uncon && !dead && (rArm || lArm)))) ++w->ccReadyPartial;
        }
    }
    if (stBits != w->ccLastStateTimerBits) ++w->ccBaseTicks;
    if (!isAI)                             ++w->ccAiUnreadable;
    else if (aiBits != w->ccLastAiTimerBits) ++w->ccAiTicks;

    w->ccLastStateTimerBits = stBits;
    w->ccLastAiTimerBits    = aiBits;
}

// F354 - ONE ATTEMPT at the injected swing. Returns true when the three writes were made; on
// false, `*gate` names the predicate that was closed so the retry's expiry can report it.
//
// Every gate here is one the ENGINE re-evaluates every frame, which is why this is a function
// called on a tick rather than a block run once when a message arrives.
bool TrySwing(unsigned int uid, ::Character* c, CombatClass* cc, unsigned int targetUid, int* gate)
{
    *gate = SW_GATE_NONE;

    // Precondition 1 (F348): `whoAttacksYouOrMe` returns immediately when +0x130 is 0, so the
    // engine itself treats combat mode as required. Combat-mode replication is separately verified
    // (T067/F222) and normally arrives first; when it has not, waiting for it is exactly right.
    if (!CombatModeOf(cc)) { *gate = SW_GATE_NOT_IN_COMBAT_MODE; return false; }

    ::Character* target = FindSpawned(targetUid);
    if (!PlausibleObject(target)) { *gate = SW_GATE_NO_TARGET; return false; }

    const int st = *(const int*)((const char*)cc + kCombatStateOff);

    // STUMBLE is the engine staggering this character; `setSwordState` refuses to overwrite it
    // anyway. Refusing here as well makes the reason countable instead of silent.
    if (st == SWORD_STAGGER) { *gate = SW_GATE_STUMBLE; return false; }

    // Already mid-blow. Rewriting STARTUP now would restart the swing from the beginning, which on
    // screen is the stutter this whole workstream exists to remove.
    if (st == SWORD_SWING) { *gate = SW_GATE_ALREADY_SWINGING; return false; }

    // F351 - the same argument applies to a BLOCK. Clobbering `nextMove` and forcing STARTUP over
    // a running block cancels a parry the peer is in the middle of showing, so the authority
    // displays a block and the peer displays a swing - a new visible divergence introduced by a
    // fix for a visible divergence. The trade is that the peer keeps ITS block instead of adopting
    // the authority's swing; they already differed, and an interrupted animation is the worse of
    // the two pictures. `skipBlocking` is what makes the trade reviewable.
    if (st == SWORD_GUARD || st == SWORD_REACT_GUARD) { *gate = SW_GATE_BLOCKING; return false; }

    // F354 - `targetTraceBlocked` is a `CombatClassAI` member, EIGHT BYTES PAST the end of a
    // `CombatClass`. Reading it off a bare `CombatClass*` is in-bounds only because
    // `CombatClassPlayer` is abstract in this build. Tested rather than assumed.
    if (!CombatIsAI(cc)) { *gate = SW_GATE_NOT_AI; return false; }

    // F351 - REFUSE WHEN THE ENGINE WOULD TURN THIS INTO A WALK. `startupState` checks
    // `targetTraceBlocked` before it looks at `nextMove` at all, and when it is set writes
    // `combatState = 10` (SWORD_APPROACH_START) and returns - handing the puppet to the
    // engine's own movement code. Not the AI decision gate, same outcome as opening it.
    if (*(const unsigned char*)((const char*)cc + kTargetTraceBlockedOff) != 0)
    { *gate = SW_GATE_TRACE_BLOCKED; return false; }

    // F351 - the engine's OWN first gate on the attack branch.
    // `CombatMovementController::hasForcedWP` says the combat mover is committed to a waypoint,
    // and `CombatClassAI::update` returns outright when it is set.
    const char* mvp = *(const char* const*)((const char*)cc + kMovementOff);
    if (!PlausibleObject(mvp)) { *gate = SW_GATE_NO_MOVEMENT; return false; }
    if (*(const unsigned char*)(mvp + kHasForcedWPOff) != 0)
    { *gate = SW_GATE_FORCED_WP; return false; }

    // F351 - THE ENGINE'S OWN GUARD ON THIS EXACT WRITE, called rather than approximated.
    AnimationClassBase* anim = *(AnimationClassBase**)((char*)cc + kAnimationOff);
    if (!PlausibleObject(anim)) { *gate = SW_GATE_NO_ANIMATION; return false; }
    if (anim->isActionAnimating()) { *gate = SW_GATE_MID_ACTION; return false; }

    // The three writes, in the engine's own order as read from `whoAttacksYouOrMe`:
    // nextMove, then combatState, then the target handle. Nothing runs between them here, so the
    // order is not load-bearing - it is matched so the code stays checkable against the finding.
    *(int*)((char*)cc + kNextMoveOff) = SWORD_SWING;
    cc->setSwordState(SWORD_STARTING);

    // P046 / T095 - READ THE RESULT OF OUR OWN SETTER, IN THE SAME BREATH AS THE CALL.
    //
    // Nothing runs between the capture and the call, so a null read here cannot be blamed on the
    // engine clearing the field later - which is precisely the distinction T094 could not make.
    // The handle bytes are captured because they are what tells the setter's early return apart
    // from its resolve failing; see the g_swSet* counters for the branch this is reading.
    unsigned int hBefore[5];
    hBefore[0] = *(const unsigned int*)((const char*)cc + kTgtHandTypeOff);
    hBefore[1] = *(const unsigned int*)((const char*)cc + kTgtHandContOff);
    hBefore[2] = *(const unsigned int*)((const char*)cc + kTgtHandContSerOff);
    hBefore[3] = *(const unsigned int*)((const char*)cc + kTgtHandIndexOff);
    hBefore[4] = *(const unsigned int*)((const char*)cc + kTgtHandSerialOff);

    cc->setTarget(target);

    unsigned int hAfter[5];
    hAfter[0] = *(const unsigned int*)((const char*)cc + kTgtHandTypeOff);
    hAfter[1] = *(const unsigned int*)((const char*)cc + kTgtHandContOff);
    hAfter[2] = *(const unsigned int*)((const char*)cc + kTgtHandContSerOff);
    hAfter[3] = *(const unsigned int*)((const char*)cc + kTgtHandIndexOff);
    hAfter[4] = *(const unsigned int*)((const char*)cc + kTgtHandSerialOff);

    const void* got = *(const void* const*)((const char*)cc + kTargetOff);
    const bool handleChanged = (::memcmp(hBefore, hAfter, sizeof(hBefore)) != 0);
    // Only the three fields the setter copies which actually IDENTIFY a character are compared.
    // Type/index/serial are the handle's identity; container and containerStamp say where it
    // lives, and a match on those alone would not mean the handle names this target.
    const bool namesTarget =
        hAfter[0] == *(const unsigned int*)((const char*)target + kCharHandTypeOff)   &&
        hAfter[3] == *(const unsigned int*)((const char*)target + kCharHandIndexOff)  &&
        hAfter[4] == *(const unsigned int*)((const char*)target + kCharHandSerialOff);

    // **THE ALIAS TEST EXISTS BECAUSE A POINTER COMPARE IS AN ASSUMPTION ABOUT THE CLASS
    // HIERARCHY, AND THIS PROBE MUST NOT REST ON ONE.** `+0x290` is `Character* currentTarget` by
    // the header, and the engine's own `setAttackTarget` compares it directly against a
    // `Character*`, so `got == target` should hold. But if `Character` ever had a base subobject at
    // a non-zero offset, the resolver could hand back an address that IS this character and is not
    // this pointer - and every single call would then be booked `wrongTarget`. `FindSpawnedUid` is
    // a pointer-keyed hash probe that never dereferences, so asking it is free and safe on any
    // value, and it turns a silent 100% false alarm into a labelled one.
    const bool alias = (got != 0) && (got != (const void*)target)
                    && (FindSpawnedUid(got) == targetUid);

    // **`wasNoOp` IS AN INDEPENDENT FLAG, NOT A BRANCH OF THE OUTCOME, AND THAT IS A CORRECTION.**
    //
    // The first version made "the call wrote nothing" an ALTERNATIVE to "currentTarget is correct",
    // so the commonest case in a real fight - the handle already names the target AND +0x290 is
    // already right, because `TrySwing` retries the same target for up to 45 frames against a
    // target the authority keeps swinging at - was booked `ok` and never counted as a no-op. A run
    // could have reported `ok=180 earlyReturn=0` and been read as "the setter always works", when
    // most of those calls wrote nothing and merely found the field already correct.
    //
    // They are orthogonal questions: `outcome` says what +0x290 holds AFTER the call, `wasNoOp`
    // says whether the call DID anything. Counting them on one axis loses one of them.
    const bool wasNoOp = !handleChanged && namesTarget;
    if (wasNoOp) ++g_swSetNoOp;

    int outcome;
    if (got == (const void*)target)      { ++g_swSetOk;          outcome = 0; }
    else if (alias)                      { ++g_swSetOkAlias;     outcome = 4; }
    else if (wasNoOp)                    { ++g_swSetEarlyReturn; outcome = 1; }
    else if (namesTarget)                { ++g_swSetResolveFail; outcome = 2; }
    else                                 { ++g_swSetWrongTarget; outcome = 3; }

    // Bounded detail, because the counters above are already exact and a fight produces hundreds
    // of these.
    //
    // **TWO SEPARATE BUDGETS, and one shared budget would have been a defect rather than a tidier
    // version of this.** The good outcome needs lines too - a run where the setter always worked
    // has to be able to SHOW a worked example, not just a count - but it is also the outcome that
    // can occur hundreds of times a minute. On one budget the successes would spend it before the
    // first failure and the run would report the failure count with no example of it, which is the
    // half of the evidence that a fifth attempt would actually be built from.
    if (outcome == 0 && g_swSetOkReported < 2)
    {
        ++g_swSetOkReported;
        ErrorLog("[P046] setTarget uid=" + N3(uid) + " target=" + N3(targetUid)
                 + " outcome=0:OK - currentTarget (+0x290) is the character we asked for"
                   " immediately after the call. curTarget=" + Ptr(got)
                 + " handleChanged=" + N3(handleChanged ? 1 : 0)
                 + " wasNoOp=" + N3(wasNoOp ? 1 : 0)
                 + " (worked example, printed at most twice. wasNoOp=1 here means the field was"
                   " ALREADY correct and our call did nothing - a pass for the run, not evidence"
                   " that the setter works.)");
    }
    else if (outcome != 0 && g_swSetReported < 12)
    {
        ++g_swSetReported;
        ErrorLog("[P046] setTarget uid=" + N3(uid) + " target=" + N3(targetUid)
                 + " outcome=" + N3(outcome)
                 + (outcome == 4 ? ":OK_BY_ALIAS(currentTarget names the uid we asked for through a"
                                   " DIFFERENT pointer. The setter worked - but every pointer"
                                   " comparison against a Character* in this file is then suspect,"
                                   " and that is the finding, not the swing.)"
                  : outcome == 1 ? ":EARLY_RETURN(wrote nothing - `hand::operator==` at the top of"
                                   " the setter found the handle already naming this target, so"
                                   " the call was a NO-OP and currentTarget kept its old value."
                                   " THE MECHANISM THAT PRODUCES THIS: setAttackTarget(NULL)"
                                   " (0x664AF0) nulls +0x290 and NEVER touches the handle, so"
                                   " after one such tick the handle still names the target while"
                                   " currentTarget is null - and this setter is then permanently"
                                   " unable to restore it. Self-healing is impossible on this"
                                   " path.)"
                  : outcome == 2 ? ":RESOLVE_FAILED(the handle now names the target and"
                                   " currentTarget is still not it - the peer cannot resolve this"
                                   " handle)"
                                 : ":WRONG_TARGET(currentTarget resolved to something that is not"
                                   " the character we asked for)")
                 + " curTargetAfter=" + Ptr(got)
                 + " expected=" + Ptr((const void*)target)
                 + " handleChanged=" + N3(handleChanged ? 1 : 0)
                 + " namesTarget=" + N3(namesTarget ? 1 : 0)
                 + " handBefore=" + N3(hBefore[0]) + "/" + N3(hBefore[3]) + "/" + N3(hBefore[4])
                 + " handAfter="  + N3(hAfter[0])  + "/" + N3(hAfter[3])  + "/" + N3(hAfter[4])
                 + " charHand="   + N3(*(const unsigned int*)((const char*)target + kCharHandTypeOff))
                 + "/" + N3(*(const unsigned int*)((const char*)target + kCharHandIndexOff))
                 + "/" + N3(*(const unsigned int*)((const char*)target + kCharHandSerialOff))
                 + "  (type/index/serial; type=" + N3(kNullHandType) + " is the engine's NULL hand,"
                   " and the resolver 0x7974F0 returns null for ANY type that is not "
                 + N3(kCharacterHandType) + " or 91 - so a type outside those two is a null"
                   " currentTarget by construction, not a failed lookup)");
    }
    return true;
}

void CountSwingGate(int gate)
{
    switch (gate)
    {
        case SW_GATE_NOT_IN_COMBAT_MODE: ++g_swNoMode;     break;
        case SW_GATE_NO_TARGET:          ++g_swNoTarget;   break;
        case SW_GATE_STUMBLE:            ++g_swStumble;    break;
        case SW_GATE_ALREADY_SWINGING:   ++g_swAlready;    break;
        case SW_GATE_BLOCKING:           ++g_swBlocking;   break;
        case SW_GATE_NOT_AI:             ++g_swNotAI;      break;
        case SW_GATE_TRACE_BLOCKED:      ++g_swTraceBlocked; break;
        case SW_GATE_NO_MOVEMENT:        ++g_swNoMovement; break;
        case SW_GATE_FORCED_WP:          ++g_swForcedWP;   break;
        case SW_GATE_NO_ANIMATION:       ++g_swNoAnim;     break;
        case SW_GATE_MID_ACTION:         ++g_swMidAction;  break;
        default:                         ++g_swGateUnknown; break;
    }
}

} // namespace

/* M7a: the catch-up's combat mode (worldsync.cpp WorldsyncCatchupAsk) - the rows live in the namespace above. */
bool CombatModeResendOwned(unsigned int uid) { return CombatModeResendRow(uid); }

// P045 / T095 - THE COUNTER ON THE FUNCTION THAT DISPATCHES STARTUP.
//
// A separate entry point rather than a few lines inside `InstallCombatHook`, because everything it
// touches lives in the combat-mode anonymous namespace further down this file and reaching back up
// would mean moving declarations around for no reason.
//
// **Its failure is loud and is NOT fatal to the rest of the build.** A probe that silently fails to
// install reports `updCalls=0` for every character - which is *exactly* the answer this study is
// looking for. The one thing this must never do is fail quietly. The expected address is printed
// beside the resolved one for the same reason F037 exists: two runs were spent hooking a function
// chosen by name that turned out to be a float setter, and a printed expectation turns that into a
// one-line check in the log instead of a wasted session.
void InstallCombatUpdateProbe()
{
    DebugLog("[P045] resolving CombatClassAI::update ...");
    intptr_t up = (intptr_t)coop::AddrAbs(kMig3CombatClassAiUpdate);
    const uintptr_t imgBase = (uintptr_t)::GetModuleHandleA(0);
    g_imageBase = imgBase;   // P050 - cached for the per-frame task-vtable compare
    DebugLog("[P045] CombatClassAI::update target = " + Ptr((void*)up)
             + " rva=" + Ptr((void*)(up ? (uintptr_t)up - imgBase : 0))
             + " = the loaded table's CombatClassAI_update row (table '" + coop::AddrTableName() + "'). Whether the"
               " table describes this executable is AddrInit's gate, which checked the row's bytes before any hook.");
    if (up == 0)
    {
        ErrorLog("[P045] resolved to 0 - THE TICK COUNTER IS NOT INSTALLED. Every uid will report"
                 " updCalls=0, and that is THE PROBE FAILING, not the engine failing to tick."
                 " Do not read this run as an answer.");
        return;
    }
    // P048 / T096 - **THE PROLOGUE, BEFORE AND AFTER. THE ONE CHECK THAT NEEDS NO FIGHT, NO
    // CONTROL ARM AND NO RUN.**
    //
    // T095 measured `updCallsAll = 0` and `tickThread = 0` on the peer - the detour body never
    // executed once - **with `AddHook SUCCESS` printed above it.** That leaves two explanations the
    // whole run could not separate: the function is genuinely never called there, or **this
    // trampoline never armed.** `AddHook`'s own return value cannot tell them apart, and the
    // sibling P026 detour firing 2.7 M times on the same instance only proves MinHook works in
    // general, not that THIS patch landed.
    //
    // Reading back the first eight bytes does tell them apart, at startup, on both instances,
    // before anything else happens. **Unhooked, 0x60CC10 begins `40 57 48 83 EC 50` (`push rdi` /
    // `sub rsp,0x50`) - read out of the shipped exe.** A MinHook-family patch replaces the head
    // with `E9 rel32`, `FF 25`, or `EB` on its patch-above path. **The test is a plain memcmp and is
    // byte-agnostic**, so none of those forms can produce a false `patched=0` - the byte list is
    // here to help a human read the hex, not to define the predicate. So: same bytes before and
    // after = **the patch did not land**, and
    // every zero downstream is the probe, not the engine.
    unsigned char before[8];
    ::memcpy(before, (const void*)up, sizeof(before));

    coop::HookStatus us =
        coop::AddHook((void*)up, (void*)&detour_ccUpdate, (void**)&orig_ccUpdate);

    unsigned char after[8];
    ::memcpy(after, (const void*)up, sizeof(after));
    const bool patched = (::memcmp(before, after, sizeof(before)) != 0);
    g_updPatched = patched ? 1 : 0;

    DebugLog("[P048] 0x60cc10 prologue before=" + HexBytes(before, 8)
             + " after=" + HexBytes(after, 8)
             + " patched=" + N3(patched ? 1 : 0)
             + "  - unhooked this function begins 40 57 48 83 ec 50; a landed patch begins e9 or"
               " ff 25 (or eb, on the patch-above path). **before == after means the trampoline"
               " did NOT arm**, whatever AddHook"
               " returned, and every updCalls=0 below is the probe rather than the engine.");

    if (us == coop::SUCCESS && patched)
        DebugLog("[P045] CombatClassAI::update AddHook SUCCESS (and the prologue is patched)");
    else if (us == coop::SUCCESS && !patched)
        ErrorLog("[P048] **AddHook RETURNED SUCCESS AND THE PROLOGUE IS UNCHANGED.** The hook is"
                 " NOT armed. Every updCalls figure in this run is 0 by construction and says"
                 " NOTHING about whether the engine ticks this function. This is the exact"
                 " ambiguity T095 could not resolve, and it is resolved here at startup.");
    else
        ErrorLog("[P045] AddHook FAILED (status not SUCCESS) - prologue read-back says patched="
                 + N3((long long)g_updPatched) + ". **If that reads 1 the two signals disagree:"
                 " trust the prologue, because it is the byte in memory rather than a return"
                 " value.** Otherwise every uid will report updCalls=0 and that is THE PROBE"
                 " FAILING, not the engine - do not read this run as an answer.");
}

// P058 / F399 - install the return-address recorder on `CharBody::finishAction`.
//
// **Its own entry point, and its failure is loud and non-fatal.** This is an instrument; if it does
// not arm, `endCalls=0` is printed and the run is void for P058 alone - it must not take the rest of
// the build down with it, and it must not be mistaken for the engine never ending a task.
//
// **The prologue read-back is not optional here and is the same check P048 exists for.** T095 spent
// a whole run on a detour whose body never executed while `AddHook SUCCESS` sat above it in the log.
// A `0` from this probe with an unverified hook would read as *nothing removes the task*, which is
// the exact opposite of the truth and would end this workstream on a false negative.
//
// **`g_imageBase` is set by `InstallCombatUpdateProbe`, which coop.cpp calls FIRST.** This function
// re-reads it rather than depending on that ordering, because a detour that fires before the base is
// cached can attribute nothing and its calls land in `endNoImageBase` - a silent hole that would
// look like light traffic.
void InstallEndActionProbe()
{
    const uintptr_t imgBase = (uintptr_t)::GetModuleHandleA(0);
    if (g_imageBase == 0) g_imageBase = imgBase;

    DebugLog("[P058] resolving CharBody::finishAction ...");
    intptr_t ea = (intptr_t)coop::AddrAbs(kMig3CharBodyEndAction);
    DebugLog("[P058] CharBody::finishAction target = " + Ptr((void*)ea)
             + " rva=" + Ptr((void*)(ea ? (uintptr_t)ea - imgBase : 0))
             + " = the loaded table's CharBody_finishAction row (table '" + coop::AddrTableName() + "'; resolve_stub slot"
               " 1958).");
    if (ea == 0)
    {
        ErrorLog("[P058] resolved to 0 - THE RETURN-ADDRESS RECORDER IS NOT INSTALLED. endCalls will"
                 " read 0 and that is THE PROBE FAILING, not the engine declining to end tasks."
                 " P058 is void for this run; the [P055]/[P057] arms are unaffected.");
        return;
    }

    unsigned char before[8];
    ::memcpy(before, (const void*)ea, sizeof(before));

    coop::HookStatus st =
        coop::AddHook((void*)ea, (void*)&detour_endAction, (void**)&orig_endAction);

    unsigned char after[8];
    ::memcpy(after, (const void*)ea, sizeof(after));
    const bool patched = (::memcmp(before, after, sizeof(before)) != 0);

    DebugLog("[P058] 0x5c5ce0 prologue before=" + HexBytes(before, 8)
             + " after=" + HexBytes(after, 8)
             + " patched=" + N3(patched ? 1 : 0)
             + "  - **before == after means the trampoline did NOT arm**, whatever AddHook returned,"
               " and every P058 return-address line below is 0 by construction.");

    if (st == coop::SUCCESS && patched)
        DebugLog("[P058] CharBody::finishAction AddHook SUCCESS (and the prologue is patched)");
    else if (st == coop::SUCCESS && !patched)
        ErrorLog("[P058] **AddHook RETURNED SUCCESS AND THE PROLOGUE IS UNCHANGED.** The recorder is"
                 " NOT armed. Read no conclusion from the site table this run.");
    else
        ErrorLog("[P058] AddHook FAILED (status not SUCCESS) - prologue read-back says patched="
                 + N3(patched ? 1 : 0) + ". **If that reads 1 the two signals disagree: trust the"
                 " prologue, it is the byte in memory rather than a return value.**");
}

void CombatModeTick()
{
    ++g_cmPulseTick;

    // F391 / F267 - **THE ONE ASSUMPTION THE INDEXING REWRITE STILL RESTS ON, CHECKED OUT LOUD.**
    // The watch is indexed by mirror slot, so the table must be at least as large as the mirror.
    // F267 made that a comment asking a human to keep two constants equal; a comment cannot fire.
    // This runs once and says so if it is ever false - which is what a raise of either cap would
    // otherwise turn into a silent loss of every character above the smaller number.
    {
        static bool s_checked = false;
        if (!s_checked)
        {
            s_checked = true;
            if (MirrorCapacity() > kMaxCombatWatch)
                ErrorLog("[M6] **COMBAT WATCH IS SMALLER THAN THE MIRROR** - mirror capacity "
                         + N3((long long)MirrorCapacity()) + " vs watch " + N3((long long)kMaxCombatWatch)
                         + ". Every mirror slot at or above " + N3((long long)kMaxCombatWatch)
                         + " will get NO combat-mode edges and no P045/P056 row. Raise"
                         " kMaxCombatWatch to match kMaxMirror.");
        }
    }

    // No explicit link check: SendCombatMode already refuses when the transport is not up,
    // and duplicating that here would be a second copy of a rule that can drift out of step.
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;

        CombatClass* cc = CombatOf(c);
        if (cc == 0) { ++g_cmNoCombat; continue; }

        // F391 - O(1). `i` IS the row index; the uid is checked inside and a mismatch rebinds.
        CombatWatch* w = CombatWatchAt(i, uid);
        if (w == 0) continue;

        int on = CombatModeOf(cc) ? 1 : 0;

        // P027 / F223 - LOG THE STATE TRANSITIONS, DO NOT SAMPLE THEM.
        //
        // T067 asked whether the peer ever reaches the swing state and could not answer it:
        // the digest samples every 15 s and the fight lasted about 36 s, so each character was
        // observed roughly THREE times. "The peer never showed SWORD_SWING" is what a
        // three-sample window shows, not an absence.
        //
        // This runs EVERY FRAME on BOTH instances and prints only when the value CHANGES, so a
        // state that exists for one frame is caught and a state that persists costs one line.
        // Same reasoning as F140's transition watch, which exists because a knockout lasting
        // 9 ms cannot be found by a poll.
        const bool mine = net::IsUidMine(uid);
        const int  st   = *(const int*)((const char*)cc + kCombatStateOff);

        // P045 / T095 - taken BEFORE anything below can write to this combat object, and taken on
        // BOTH instances for BOTH owned characters and puppets. The owned characters are the
        // control and they are the reason this is not a switch position: a control arm has to be
        // capable of a non-zero reading (T094).
        SampleCombatTick(w, c, cc, mine, on);

        // F416 - per-frame visible displacement while in combat (both instances). Reset when combat
        // starts so a row carries one fight's number; the authority emits it at the OFF edge.
        if (on)
        {
            Ogre::Vector3 vp;
            const double nowS = CmNowSeconds();
            if (SafeReadPosition(c, &vp) && _finite((double)vp.x) && _finite((double)vp.y) && _finite((double)vp.z))
            {
                if (w->mvPrimed)
                {
                    const float mx = vp.x - w->mvLastX, my = vp.y - w->mvLastY, mz = vp.z - w->mvLastZ;
                    const float m = sqrtf(mx * mx + my * my + mz * mz);
                    const double dtS = nowS - w->mvLastAt;
                    if (_finite((double)m))
                    {
                        if (m > w->mvVisMax) w->mvVisMax = m;
                        if (dtS > 0.0005)
                        {
                            float oftB = -1.0f, ftB = -1.0f; ReadCharFrameTimesPodC(c, &oftB, &ftB);   // decision 27: the same rule on the host's own bodies
                            const double bankedFtL = (ftB > (float)dtS && ftB < 5.0f) ? (double)ftB : dtS;
                            const float rate = m / (float)bankedFtL;
                            const bool downed = IsDownedCharacter(c);   // review 4 item 12: the peer's predicate, not a prone-only proxy
                            if (downed) { if (rate > w->mvVisMaxRateDowned) w->mvVisMaxRateDowned = rate; }
                            else
                            {
                                // PROBE-START: P019 - vis_peak on a LOCALLY SIMULATED body (mine): the baseline the verdict's hop bound is built from.
                                // review-p019 CRITICAL: this block runs for puppets too on both instances - the label was "host" and the
                                // peer logged its puppets twice; only owned bodies are logged here (puppets are logged in DrivePuppet).
                                if (mine && rate > w->mvVisMaxRate && rate > 90.0f)
                                {
                                    char tqpc[32]; _snprintf(tqpc, 31, "%.3f", nowS); tqpc[31] = 0; ++g_visPeakLinesLocal;
                                    float oftL = -1.0f, ftL = -1.0f; ReadCharFrameTimesPodC(c, &oftL, &ftL);
                                    DebugLog("[PROBE] vis_peak side=local uid=" + N3((long long)w->uid) + " tqpc=" + std::string(tqpc) + " frameDt=" + F1((float)(dtS * 1000.0)) + "ms from=" + F1(w->mvLastX) + "," + F1(w->mvLastY) + "," + F1(w->mvLastZ)
                                             + " to=" + F1(vp.x) + "," + F1(vp.y) + "," + F1(vp.z) + " step=" + F1(m) + " rate=" + F1(rate) + " ft=" + F1(ftL * 1000.0f) + "ms oft=" + F1(oftL * 1000.0f) + "ms");   // prone is 0 by construction on this branch; ft/oft = Character::frameTIME/offscreenFrameTime (F502)
                                }
                                // PROBE-END: P019
                                if (rate > w->mvVisMaxRate) w->mvVisMaxRate = rate;
                            }
                        }
                    }
                    ++w->mvFrames;
                }
                w->mvLastX = vp.x; w->mvLastY = vp.y; w->mvLastZ = vp.z; w->mvLastAt = nowS; w->mvPrimed = 1;
            }
        }
        else w->mvPrimed = 0;
        {
            if (w->lastState != st)
            {
                DebugLog("[M6] state uid=" + N3(uid)
                         + " " + N3(w->lastState) + ":" + SwordState(w->lastState)
                         + " -> " + N3(st) + ":" + SwordState(st)
                         + " mode=" + N3(on)
                         + " mine=" + N3(mine ? 1 : 0));

                // P059 - one machine-readable record per swing START, both instances. The verdict
                // tool counts host swings from mine=1 rows and peer swings from mine=0 rows; no
                // regex over prose. `-1` guard: a rebound row has no predecessor state.
                if (st == 0 && w->lastState != -1)
                {
                    int native = 0;
                    if (!mine)
                    {
                        if (w->nativeOn) { native = 1; ++w->nativeSwings; ++g_ncWindowSwings; }
                        else if (w->weWantOn == 0) ++g_ncSwingsWhileAuthOff;
                    }
                    const std::string q(1, (char)34);
                    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "swing" + q
                             + "," + q + "uid" + q + ":" + N3(uid)
                             + "," + q + "mine" + q + ":" + N3(mine ? 1 : 0)
                             + "," + q + "native" + q + ":" + N3(native)
                             + "," + q + "authWantsOn" + q + ":" + N3(w->weWantOn)
                             + "," + q + "tick" + q + ":" + N3(g_cmPulseTick) + "}");
                }

                // F348 - THE SWING, SENT ON THE TRANSITION INTO THE ATTACK STATE.
                //
                // Not on the transition into STARTUP with nextMove==0, which is one frame earlier
                // and is what the engine actually writes: STARTUP is an INTENTION, and
                // `AttackState::initialiseAttack` can decline it silently. Entering state 0 is the
                // engine's own confirmation that a blow began, which is the event the user
                // reported missing. One frame of extra latency buys a message that never reports
                // a swing that did not happen.
                // **`lastState != -1` IS PART OF THE EDGE TEST, NOT A TIDY-UP.** A rebind resets
                // `lastState` to -1, and -1 != SWORD_SWING, so a character rebound mid-swing would
                // have its already-started blow announced as a fresh one - one fabricated
                // `swingSent` inflating the numerator of the P041 arithmetic. `lastOn` was given
                // exactly this treatment when it was written ("never sampled, so the first read is
                // not an edge"); `lastState` was not, and rebinds did not exist to expose it.
                if (mine && g_swingRepl && st == SWORD_SWING
                    && w->lastState != SWORD_SWING && w->lastState != -1)
                {
                    // F351 - RESOLVE THE TARGET THROUGH ITS HANDLE, not through the raw
                    // `currentTarget` pointer at +0x290. A `hand` is index+serial resolved by the
                    // engine's registry, so a target that has died resolves to null; a raw pointer
                    // whose address has been reused resolves to a STRANGER, and `FindSpawnedUid`
                    // would then hand a perfectly valid uid for the wrong character to the peer.
                    // The combat-mode send 140 lines below already did it this way - two
                    // mechanisms for one job in one function, and the new one was the unsafe one.
                    unsigned int tgtUid = 0;
                    hand t = cc->currentTarget();
                    ::Character* tc = t.getCharacter();
                    const bool haveTarget = PlausibleObject(tc);
                    if (haveTarget) tgtUid = FindSpawnedUid(tc);

                    // Same bound as combat mode, and for the same reason (P-16): a puppet swinging
                    // at a world NPC would have the peer alone take the damage, because that
                    // victim has no uid and the hit falls through to the real handler. Refused
                    // here rather than discovered as a divergence later.
                    //
                    // F351 - the two reasons are counted SEPARATELY. "The engine gave us no
                    // target" is a bug signal; "the target is a character we do not replicate" is
                    // this bound working as designed. One counter for both would hide the first
                    // inside the second, and the first is precisely what a bad target read looks
                    // like.
                    if (!haveTarget)      ++g_swSendNoTargetObj;
                    else if (tgtUid == 0) ++g_swSendNotReplicated;
                    else if (net::SendSwing(uid, tgtUid)) ++g_swSent;
                    else                  ++g_swSendFailed;
                }

                w->lastState = st;
            }
        }

        // ---- PEER SIDE: F233, the narrow fix -------------------------------------------
        //
        // T070 settled this with a controlled experiment: one puppet ungated, three left gated,
        // same fight, same machine. The ungated one produced 51 swings across 8 states; the
        // three gated ones produced ZERO, using only the two deciding states. The decision gate
        // is what stops the peer acting.
        //
        // But the same run also showed ungating is NOT the fix, twice over:
        //   * ungating ALONE did nothing - the character sat in DECISION for 31 s and only
        //     acted 0.32 s after combat mode arrived. Decisions AND combat mode are both
        //     required;
        //   * an ungated puppet is a free-running clone, exactly as F152 says: it drifted to
        //     101.8 units, adopted its own goal ('Self preservation'), and generated 236 hits
        //     of its own that were all discarded.
        //
        // So the gate is not removed. A single decision pass is let through, on a cadence, and
        // ONLY while this puppet is actually in combat mode. Outside combat it stays fully
        // suppressed and nothing about F152 changes.
        if (!mine)
        {
            // P041 / F348 - THE SWING VERDICT. Resolved here rather than in ApplyRemoteSwing
            // because the answer only exists a frame or more LATER: the write sets an intention
            // and the engine's own `startupState` turns it into a swing, or silently does not.
            // F354 - RE-ATTEMPT THE INTENT. This is the recurrence that stops a gated swing from
            // being silently lost; see ApplyRemoteSwing for why a one-shot was wrong.
            if (w->swingIntent)
            {
                const long long iage = g_cmPulseTick - w->swingIntentTick;

                // F356 - **AGE FIRST, ATTEMPT SECOND.** The first version attempted the write and
                // only checked expiry on failure, so an intent belonging to a puppet that streamed
                // OUT never aged (the scoring block is unreachable while `MirrorSlot` hides a
                // retired entry) and, on stream-in minutes later, the very first thing that
                // happened was a write. A phantom blow for a swing the authority threw minutes ago
                // - which then reaches SWORD_SWING within 30 frames BECAUSE WE JUST CAUSED IT, and
                // is counted LANDED. That is F354's own headline defect, one level up, on the
                // state F354 introduced.
                if (iage > kSwingIntentTicks)
                {
                    w->swingIntent = 0;
                    ++g_swIntentExpired;

                    // F358 - **DO NOT BLAME A GATE FOR AN EXPIRY THAT NOBODY WATCHED.**
                    //
                    // Expiry runs off the global tick, but an intent is only ATTEMPTED on ticks
                    // where `MirrorSlot` yields this puppet and its CombatClass reads. A puppet
                    // that streams out (or whose CharBody read fails) is skipped by three
                    // `continue`s above, so its intent ages without being tried - and the first
                    // version booked the gate from the last attempt, which could be seconds old
                    // and about a completely different frame. The whole point of the gate counters
                    // is to answer "why did the peer not swing"; an expiry caused by ABSENCE is a
                    // different answer, and it now has its own bucket instead of polluting one.
                    if (g_cmPulseTick - w->swingIntentLastTry > 1) ++g_swIntentNotServiced;
                    else CountSwingGate(w->swingIntentGate);

                    if (iage > kSwingStaleTicks) ++g_swIntentStale;   // ...and it never even aged
                }
                else
                {
                    int gate = SW_GATE_NONE;
                    w->swingIntentLastTry = g_cmPulseTick;   // F358 - an ACTUAL attempt
                    if (TrySwing(uid, c, cc, w->swingIntentTarget, &gate))
                    {
                        w->swingIntent = 0;
                        ++g_swWrote;
                        // F356 - was the RETRY load-bearing, or would a one-shot have done? The
                        // entire justification of F354 is that deferring beats dropping, and the
                        // first version shipped no number that could tell them apart.
                        if (iage <= 1) ++g_swWroteFirstTry; else ++g_swWroteAfterWait;
                        if (w->swingPending) ++g_swRewrite;
                        w->swingPending   = 1;
                        w->swingWroteTick = g_cmPulseTick;
                        w->swingTarget    = w->swingIntentTarget;
                    }
                    // F356 - **AND DO NOT WAIT OUT A BLOW THAT IS ALREADY BEING THROWN.**
                    //
                    // Retrying past `ALREADY_SWINGING` is the one gate where the retry is actively
                    // harmful: the peer's own arc finishes, the gate opens, and we inject a SECOND
                    // arc for one authority blow. That is not cosmetic - a puppet's arc that
                    // connects with a character we own is applied locally AND sent to the authority
                    // as authoritative (F349's middle row), so an extra arc is extra damage in a
                    // world the authority never dealt it. The peer is already showing a blow, which
                    // is the whole user-visible goal, so the intent is absorbed rather than queued.
                    else if (gate == SW_GATE_ALREADY_SWINGING)
                    {
                        w->swingIntent = 0;
                        // F358 - WHOSE arc absorbed it? `swingPending` says we wrote one recently.
                        // Without the split, a run where the peer is swinging on its OWN and a run
                        // where the injection is working look identical in this counter - and since
                        // T067 the peer's state machine does move on its own.
                        if (w->swingPending) ++g_swAbsorbedByOurs;
                        else                 ++g_swAbsorbedByItsOwn;
                        ++g_swIntentAbsorbed;
                    }
                    else w->swingIntentGate = gate;
                }
            }

            if (w->swingPending)
            {
                const long long age = g_cmPulseTick - w->swingWroteTick;
                // F354 - **LANDED IS BOUNDED BY THE VERDICT WINDOW, AND IT IS TESTED AFTER THE
                // STALENESS GUARD, NOT BEFORE IT.**
                //
                // The first repair added the staleness bound and then left `LANDED` unguarded and
                // FIRST, so a puppet that streamed out with a write pending and came back seconds
                // later - already swinging under its own engine, which T075 measured it doing -
                // would credit that arrival to a write made half a minute earlier. The headline
                // metric could be fabricated by exactly the scenario the guard was added for, and
                // the honesty line printed beside it ("within 30 frames of our write") was false.
                if (st == SWORD_SWING && age <= kSwingVerdictTicks)
                {
                    ++g_swLanded;
                    w->swingPending = 0;
                }
                // F351 - A PUPPET THAT STREAMED OUT AND BACK MUST NOT SCORE A STALE VERDICT.
                //
                // The scoring block is unreachable while `MirrorSlot` hides a retired entry, and
                // the watch slot is never released, so a pending write could sit for minutes and
                // then be booked as a fresh DECLINED - with a state, a mode and a gap read
                // minutes after the write it claims to describe. That is a fabricated data point
                // in the one instrument this feature is being judged by.
                else if (age > kSwingStaleTicks)
                {
                    ++g_swUnscoredStale;
                    w->swingPending = 0;
                }
                else if (age > kSwingVerdictTicks)
                {
                    ++g_swDeclined;
                    w->swingPending = 0;

                    // Print the diagnosis ONCE per uid, not once per swing: a fight produces
                    // dozens of these and a per-swing line would bury the run's other output.
                    // The counter above stays exact either way.
                    if (!w->swingReported)
                    {
                        w->swingReported = 1;

                        // Snapshot the volatile ONCE for this whole line. The gate below and the
                        // `hookFired=` it prints were two separate reads of a counter the detour
                        // increments from another thread, so a bump landing between them could
                        // print `UNMEASURABLE(... hookFired=1)` - one line disagreeing with itself,
                        // which is exactly the defect class this pass exists to close.
                        const long long goFired = (long long)g_updTotal;
                        // Hoisted for the same discipline the line above establishes, and because
                        // this is a FIGHT-TIME path: it was two O(512) scans per decline log.
                        // (They could not have disagreed - `uid` is written only on this thread -
                        // but one read is cheaper and the rule is worth keeping uniform.)
                        const int uidRowsHere = WatchRowsForUid(w->uid);

                        float gap = -1.0f;
                        ::Character* tc = FindSpawned(w->swingTarget);
                        Ogre::Vector3 a, b;
                        if (SafeReadPosition(c, &a) && PlausibleObject(tc)
                            && SafeReadPosition(tc, &b))
                        {
                            const float dx = a.x - b.x, dz = a.z - b.z;
                            gap = ::sqrtf(dx * dx + dz * dz);
                        }

                        ErrorLog("[M6] SWING DECLINED BY THE ENGINE uid=" + N3(uid)
                                 + " target=" + N3(w->swingTarget)
                                 + " - we wrote nextMove=0/STARTUP and the state did not reach"
                                   " SWORD_SWING WITHIN " + N3(kSwingVerdictTicks) + " frames."
                                   " (state below may itself read SWORD_SWING: arriving LATE is"
                                   " still a decline by this measure, and that is deliberate.)"
                                   " state=" + N3(st) + ":" + SwordState(st)
                                 + " next=" + N3(*(const int*)((const char*)cc + kNextMoveOff))
                                 + " mode=" + N3(on)
                                 + " technique=" + N3((long long)(*(const void* const*)
                                        ((const char*)cc + kTechniqueOff) ? 1 : 0))
                                 // F354 - **THE FIELD `startupState` ACTUALLY BAILS ON**, which
                                 // the first probe omitted: with `currentTarget` (+0x290) null it
                                 // writes COMBAT_FINISHED and returns, so a decline with
                                 // `curTarget=0` has a completely different cause from one with a
                                 // target and a gap outside the MEI band. We set it via
                                 // `setTarget`; the engine can clear it between then
                                 // and the verdict.
                                 + " curTarget=" + N3((long long)(*(const void* const*)
                                        ((const char*)cc + kTargetOff) ? 1 : 0))
                                 + " threatCount=" + N3(*(const int*)((const char*)cc + kThreatCountOff))
                                 // F358 - GUARDED, like its sibling in `TrySwing`. +0x2C0 is a
                                 // `CombatClassAI` member, eight bytes past the end of a base
                                 // `CombatClass`; reading it unguarded here was the one remaining
                                 // "in bounds by an accident of the class hierarchy" site that
                                 // F354 set out to remove, left behind when its sibling was fixed.
                                 + " traceBlocked=" + (CombatIsAI(cc)
                                       ? N3((long long)*(const unsigned char*)
                                              ((const char*)cc + kTargetTraceBlockedOff))
                                       : std::string("n/a"))
                                 // F351 - the MEI band as a PAIR. The first version printed only
                                 // MEI_MIN and called it `reach`, which is one bound of a range
                                 // under the name of a different quantity: unusable for deciding
                                 // whether `gapXZ` is inside the band, which is the whole question.
                                 + " meiMin=" + F1(*(const float*)((const char*)cc + kMeiMinOff))
                                 + " meiMax=" + F1(*(const float*)((const char*)cc + kMeiMaxOff))
                                 + " gapXZ=" + F1(gap)
                                 // P047 / T095 - THE WHOLE COMBAT BLOCK, ON THE LINE THAT REPORTS
                                 // THE FAILURE. T094's decline line carried `curTarget=0` and
                                 // nothing that could say WHY, so three candidate causes all
                                 // predicted the same single digit and the run separated none of
                                 // them. These fields are what tell them apart:
                                 //
                                 //   handle type == 11 -> the handle was NEVER WRITTEN (11 is the
                                 //     engine's own null-hand marker, used by
                                 //     `setTarget(NULL)` and tested by
                                 //     `CombatClassAI::update`);
                                 //   handle holds real values while curTarget is null -> the
                                 //     handle FAILED TO RESOLVE on this instance;
                                 //   focused handle type == 11 -> F356's prediction, and only with
                                 //     this field printed is it evidence rather than a guess;
                                 //   ticks -> **whether this character's state machine was being
                                 //     run at all** while all of the above was true. Without it
                                 //     every other field on this line is a reading taken from a
                                 //     machine that may never have executed.
                                 + " curTargetPtr=" + Ptr(*(const void* const*)
                                        ((const char*)cc + kTargetOff))
                                 + " tgtHand=" + N3(*(const unsigned int*)
                                        ((const char*)cc + kTgtHandTypeOff))
                                 + "/" + N3(*(const unsigned int*)((const char*)cc + kTgtHandContOff))
                                 + "/" + N3(*(const unsigned int*)((const char*)cc + kTgtHandContSerOff))
                                 + "/" + N3(*(const unsigned int*)((const char*)cc + kTgtHandIndexOff))
                                 + "/" + N3(*(const unsigned int*)((const char*)cc + kTgtHandSerialOff))
                                 + " (type/container/containerSerial/index/serial; type="
                                 + N3(kNullHandType) + " = NEVER WRITTEN, and only type "
                                 + N3(kCharacterHandType) + " or 91 can resolve to a character at"
                                   " all)"
                                 // Guarded exactly like `traceBlocked` above: focusedTarget,
                                 // reFocusableMode and targetLastChangedTimer are all CombatClassAI
                                 // members past the end of a base CombatClass (F358).
                                 + (CombatIsAI(cc)
                                     ? " focHand=" + N3(*(const unsigned int*)
                                            ((const char*)cc + kFocHandTypeOff))
                                       + "/" + N3(*(const unsigned int*)
                                            ((const char*)cc + kFocHandIndexOff))
                                       + "/" + N3(*(const unsigned int*)
                                            ((const char*)cc + kFocHandSerialOff))
                                       + " reFocusable=" + N3((long long)*(const unsigned char*)
                                            ((const char*)cc + kReFocusableOff))
                                     : std::string(" focHand=n/a reFocusable=n/a"))
                                 + " stateTimer=" + F1(*(const float*)
                                        ((const char*)cc + kStateTimerOff))
                                 // P045, ON THIS LINE. **`updCalls` and `inCombatFrames` are NOT
                                 // over the same interval and their ratio means nothing:** the
                                 // detour counts every call whether or not the character is in
                                 // combat mode, so updCalls spans first-sighting to last-sighting
                                 // INCLUDING any out-of-combat frames between, while
                                 // inCombatFrames counts only contiguous in-combat pairs. updCalls
                                 // MAY therefore exceed it - by how much depends on whether the
                                 // engine ticks this function outside combat at all, which nothing
                                 // here establishes and this run is the first to measure.
                                 // `baseTicked` is
                                 // `CombatClass::update` running, `aiTicked` is
                                 // `CombatClassAI::update` running - and the state-3 dispatch
                                 // this whole feature depends on lives ONLY in the latter.
                                 + "  ticks: inCombatFrames=" + N3(w->ccSamples)
                                 // Same refusal as the report row, and its absence here was the
                                 // report's own defect one line louder: THIS is an ErrorLog printed
                                 // during the fight, hours before the report, so a bare `0` here
                                 // for a row the report will decline to score is the study's
                                 // conclusion arriving first and unqualified.
                                 // Read ONCE into a local: the gate and the printed value came
                                 // from two separate reads of a volatile the detour increments, so
                                 // a concurrent bump could print `UNMEASURABLE(... hookFired=1)` -
                                 // a line disagreeing with itself, which is the whole defect class
                                 // this pass is closing.
                                 // **`uidRows > 1` REFUSES TO SCORE, AND IT IS THE REASON
                                 // `WatchRowsForUid` EXISTS.** Two mirror slots holding one uid
                                 // used to blow the rebind limit and land here by accident; under
                                 // slot indexing they are two calm rows each covering part of the
                                 // character. Without this term both would print a confident
                                 // number and neither would mention the other.
                                 + (!RowMeasurable(w, uidRowsHere)
                                    || (goFired == 0 && g_updPatched != 1)
                                    || w->ccUpdSlot == 0
                                      ? " updCalls=UNMEASURABLE(binds=" + N3(w->ccUpdRebinds)
                                        + " uidRows=" + N3((long long)uidRowsHere)
                                        + " hookPatched=" + N3((long long)g_updPatched)
                                        + " hookFired=" + N3(goFired > 0 ? 1 : 0)
                                        + " slot=" + (w->ccUpdSlot != 0
                                              ? std::string("yes") : std::string("NONE")) + ")"
                                      : " updCalls=" + N3(w->ccUpdCarried
                                            + (w->ccUpdSlot != 0
                                                 ? w->ccUpdLastSeen - w->ccUpdAtPrime : 0)))
                                 + " binds=" + N3(w->ccUpdRebinds)
                                 + " [2nd baseTicked=" + N3(w->ccBaseTicks)
                                 + " aiTicked=" + N3(w->ccAiTicks)
                                 + " aiUnreadable=" + N3(w->ccAiUnreadable) + "]"
                                 // Keyed on the CALL COUNT, like its sibling in the report. The
                                 // first version fired on `aiTicked == 0`, which a perfectly
                                 // running engine produces for a puppet in exactly this state
                                 // (state 3, null target -> startupState -> chooseAttackTarget
                                 // resets the timer every frame), and which our own isAI read
                                 // failing would produce too. The run's headline sentence is the
                                 // last place to assert something the code does not establish.
                                 // `goFired` and the rebind bound are here for the same
                                 // reason they are on the sibling banner in the report, and their
                                 // absence here was a defect: THIS line is an ErrorLog printed
                                 // DURING the fight, long before anyone types `report`, so a probe
                                 // that failed to install would have shouted the study's
                                 // conclusion in capitals with nothing beside it to check against.
                                 + (w->ccSamples > 0 && w->ccUpdCarried == 0 && w->ccUpdSlot != 0
                                    // `goFired`, not `g_updTotal` - a THIRD read of the same
                                    // volatile on this one line, left behind when the other two
                                    // were snapshotted. The UNMEASURABLE gate and this banner could
                                    // then disagree WITH EACH OTHER on a single line, which is the
                                    // self-contradiction class this whole pass is closing, showing
                                    // up inside the fix for it.
                                    && (goFired > 0 || g_updPatched == 1)
                                    // **THE SAME PREDICATE AS THE GATE ABOVE, NOT A SECOND
                                    // HAND-WRITTEN COPY OF HALF OF IT.** This line open-coded only
                                    // the rebind half, so a duplicate-uid row printed
                                    // `updCalls=UNMEASURABLE(... uidRows=2 ...)` and then, in the
                                    // SAME concatenation, the banner below in capitals. The comment
                                    // four lines up describes exactly that failure and this line was
                                    // the instance of it - **the fifth time this defect has landed,
                                    // and the fourth time inside the fix for the previous one.**
                                    // And this is the WORSE venue: it is a fight-time line, hours
                                    // before the report, so an unqualified verdict here is the
                                    // study's conclusion arriving first.
                                    && RowMeasurable(w, uidRowsHere)
                                    && w->ccUpdLastSeen - w->ccUpdAtPrime == 0
                                      ? "  <== THE AI UPDATE WAS NEVER CALLED FOR THIS CHARACTER"
                                        " WHILE IT WAS IN COMBAT. The state-3 dispatch is in that"
                                        " function, so no write we make could ever have become a"
                                        " swing."
                                      : "")
                                 + (st == SWORD_APPROACH_START || st == SWORD_APPROACH
                                      ? "  <== IT BECAME A WALK, NOT A REFUSAL: startupState"
                                        " converted the injection into SWORD_APPROACH."
                                      : "")
                                 + "  (P041/F348 - the likeliest cause is that initialiseAttack"
                                   " found no technique for this gap; compare gapXZ against the"
                                   " MEI band. Reported once per uid.)");
                    }
                }
            }

            // P030 / F246 - WHO ENDED THIS FIGHT?
            //
            // T074: the peer stopped swinging at 222.8 s and the authority carried on to 287.5
            // - 30 swings with no counterpart. The retraction is NOT what stopped it: the first
            // `on=0` arrived 5.2 s LATER, and three of the four applies found the flag already
            // down (`combatModeActive 0 -> 0`). **Something on the peer ended its own combat.**
            //
            // So the peer now says when its combat mode falls while the authority still wants it
            // fighting. That distinguishes "we told it to stop" from "it stopped by itself",
            // which are different defects with different fixes, and neither is guessable from a
            // count of swings.
            if (w->lastPeerMode != on)
            {
                // F248 - BOTH ARMS. The first version of this probe only asked "did it stop
                // when we wanted it fighting". T075 found the opposite happening in the same
                // run on a different character - the peer's engine STARTED combat by itself,
                // 73 ms after we cleared it, and the probe was structurally blind to it.
                //
                // A one-sided detector on a two-sided question is not a weak instrument, it is
                // a misleading one: it reports only the half it can see, and the half it cannot
                // see looks like absence.
                if (w->lastPeerMode == 1 && on == 0 && w->weWantOn == 1)
                {
                    ErrorLog("[M6] peer combat ENDED BY ITS OWN ENGINE uid=" + N3(uid)
                             + " - the authority last told us ON and we never applied OFF."
                               " prone=" + N3(*(const int*)((const char*)c + 0xE0))
                             + " (P030/F246)");
                }
                else if (w->lastPeerMode == 0 && on == 1 && w->weWantOn == 0)
                {
                    ErrorLog("[M6] peer combat STARTED BY ITS OWN ENGINE uid=" + N3(uid)
                             + " - the authority last told us OFF and we never applied ON."
                               " prone=" + N3(*(const int*)((const char*)c + 0xE0))
                             + " (P030/F248)");
                }
                w->lastPeerMode = on;
            }

            if (g_combatPulse && on != 0 && (g_cmPulseTick % kPulseEveryTicks) == 0)
            {
                if (PulseCharacterQuiet(c)) ++g_cmPulses;
            }
            continue;
        }

        // SENDING is AUTHORITY ONLY. A peer must never announce a combat mode it was told to
        // enter - that is how one-way replication turns into a feedback loop. The state
        // transition log above deliberately runs on BOTH sides, because comparing them is the
        // entire point of it.

        // Resolve the target only while in combat: `currentTarget` builds a hand, and there
        // is no reason to pay for that every frame for every idle character.
        unsigned int targetUid = 0;
        if (on)
        {
            hand t = cc->currentTarget();
            ::Character* tc = t.getCharacter();
            if (PlausibleObject(tc)) targetUid = FindSpawnedUid(tc);
        }

        // Send on any EDGE: the mode changed, or the target changed while still fighting. The
        // second matters - a character that switches opponents would otherwise keep swinging
        // at the wrong one on the peer.
        bool edge = (w->lastOn != on) || (on != 0 && targetUid != w->lastTarget);
        if (!edge) continue;

        if (on == 0 && w->lastOn == 1)
        {
            const std::string q(1, (char)34);
            DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "host_move" + q
                     + "," + q + "uid" + q + ":" + N3(uid)
                     + "," + q + "frames" + q + ":" + N3(w->mvFrames)
                     + "," + q + "visMax" + q + ":" + F6(w->mvVisMax)
                     + "," + q + "visMaxRate" + q + ":" + F1(w->mvVisMaxRate)
                     + "," + q + "visMaxRateDowned" + q + ":" + F1(w->mvVisMaxRateDowned)
                     + "," + q + "tick" + q + ":" + N3(g_cmPulseTick) + "}");
            w->mvVisMax = 0.0f; w->mvVisMaxRate = 0.0f; w->mvVisMaxRateDowned = 0.0f; w->mvFrames = 0; w->mvPrimed = 0;
        }
        else if (on == 1 && w->lastOn != 1) { w->mvVisMax = 0.0f; w->mvVisMaxRate = 0.0f; w->mvVisMaxRateDowned = 0.0f; w->mvFrames = 0; w->mvPrimed = 0; }

        w->lastOn     = on;
        w->lastTarget = targetUid;

        // BOUNDED ON PURPOSE (v1): only replicate combat against a target we also replicate.
        //
        // A puppet in combat mode swinging at a REPLICATED victim is safe - `detour_hitByMelee`
        // returns 0 for a victim we do not own, so damage cannot be double-applied. A puppet
        // swinging at a WORLD NPC is not: that victim has no uid, the hit falls through to the
        // real handler, and the peer alone takes damage. With P-16 measured - the two worlds
        // hold different people - that is a live route to divergence, so it is refused here
        // rather than discovered in a log later.
        if (on != 0 && targetUid == 0)
        {
            ++g_cmNoTarget;
            DebugLog("[M6] combat mode NOT replicated for uid=" + N3(uid)
                     + " - its target is not a character we replicate. Sending it would let the"
                       " peer swing at a world NPC and apply damage on one machine only"
                       " (P-16). Refused deliberately.");
            continue;
        }

        if (net::SendCombatMode(uid, on != 0, targetUid))
        {
            ++g_cmSent;
            DebugLog("[M6] combat mode " + std::string(on ? "ON" : "OFF") + " uid=" + N3(uid)
                     + " target=" + N3(targetUid) + " -> peer");
        }
    }
}

// P059 - the tail every close shares: place the body, record the window, clear the row. The GATE
// and the order/goal clears happen BEFORE this, at the call site, in that order.
void FinishNativeClose(unsigned int uid, CombatWatch* w, const char* reason, bool charResolved)
{
    float before = -1.0f, sameCall = -1.0f;
    // Never place a body through a pointer the registry has refused (review round 2, B3).
    // And never teleport a body that is already close: with reconciliation the drift at close is
    // ~1-2 units (T104), and a 2-unit snap is itself the hop the visible metric cannot see (it lands
    // between windows). Below kNcSnapOnlyAbove the ordinary drive closes the gap at walking pace.
    const float driftNow = PuppetWindowDriftNow(uid);
    const bool snapNeeded = !(driftNow >= 0.0f && driftNow < kNcSnapOnlyAbove);
    const int snap = !charResolved ? 7 : (snapNeeded ? SnapPuppetToAuthority(uid, &before, &sameCall) : 8);
    if (snap >= 0 && snap < 9) ++g_ncSnapReason[snap];
    if (snap == 0) ++g_ncSnapDone; else if (snap != 8) ++g_ncSnapDeclined;
    const float windowMax = PuppetWindowMaxDrift(uid);
    ReconcileStats rs; PuppetReconcileStats(uid, &rs);
    // combat1: the parity aggregate. review-combat1: a window whose drift was never sampled (driftMean -1: its max stays at
    // the 0 it opened with) is counted apart, not averaged in as a perfect 0; a NaN or unread drift is skipped.
    if (!(rs.driftMean >= 0.0f && rs.driftMean < 100000.0f)) ++g_ncParityNoSample;
    else if (windowMax >= 0.0f && windowMax < 100000.0f)
    {
        ++g_ncParityWindows;
        g_ncParityMaxDriftSum += windowMax;
        if (windowMax > g_ncParityMaxDriftWorst) g_ncParityMaxDriftWorst = windowMax;
        g_ncParityDriftMeanSum += rs.driftMean;
        if (windowMax > 5.0f) ++g_ncParityOver5;
        const float od = w->nativeOpenDrift;
        if (od >= 0.0f && od < 100000.0f)
        {
            g_ncParityOpenDriftSum += od;
            ++g_ncParityOpenDriftN;
            if (windowMax > od + 5.0f) ++g_ncParityGrew5;
        }
    }
    const long long rcSteps = rs.steps; const float rcMax = rs.maxStep, rcSum = rs.sumStep, rcReq = rs.reqMax, rcVis = rs.visMaxStep;
    SetPuppetNativeWindow(uid, false);

    const long long frames = g_cmPulseTick - w->nativeOpenTick;
    ++g_ncWindowsClosed;
    const std::string q(1, (char)34);
    DebugLog("[NC] window CLOSE uid=" + N3(uid) + " reason=" + reason + " frames=" + N3(frames)
             + " swings=" + N3(w->nativeSwings) + " openDriftXZ=" + F1(w->nativeOpenDrift) + " maxDriftXZ=" + F1(windowMax)
             + " rcSteps=" + N3(rcSteps) + " rcMaxStep=" + F6(rcMax) + " rcReqMax=" + F6(rcReq) + " rcVisMaxStep3D=" + F6(rcVis)
             + " rcVisMaxRate=" + F1(rs.visMaxRate) + " rcLayerGapMax=" + F6(rs.layerGapMax) + " rcDriftMean=" + F1(rs.driftMean) + " rcSum=" + F1(rcSum)
             + " driftBeforeSnap=" + F1(before) + " driftSameCall=" + F1(sameCall)
             + " snapReason=" + N3(snap)
             + " (0 placed, 1 noPuppet, 2 noChar, 3 noAnim, 4 noMovement, 5 ragdoll, 6 prone, 7 unresolved, 8 notNeeded;"
               " the drift AFTER the snap is the next tick's snap_scored record)");
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "window_close" + q
             + "," + q + "uid" + q + ":" + N3(uid)
             + "," + q + "reason" + q + ":" + q + reason + q
             + "," + q + "frames" + q + ":" + N3(frames)
             + "," + q + "swings" + q + ":" + N3(w->nativeSwings)
             + "," + q + "maxDriftXZ" + q + ":" + F1(windowMax)
             + "," + q + "rcSteps" + q + ":" + N3(rcSteps)
             + "," + q + "rcMaxStep" + q + ":" + F6(rcMax)
             + "," + q + "rcReqMax" + q + ":" + F6(rcReq)
             + "," + q + "rcVisMaxStep" + q + ":" + F6(rcVis)
             + "," + q + "rcVisMaxRate" + q + ":" + F1(rs.visMaxRate)
             + "," + q + "rcVisMaxRateDowned" + q + ":" + F1(rs.visMaxRateDowned)
             + "," + q + "rcVisMaxRateAny" + q + ":" + F1(rs.visMaxRateAny)
             + "," + q + "rcLayerGapMax" + q + ":" + F6(rs.layerGapMax)
             + "," + q + "rcDriftMean" + q + ":" + F1(rs.driftMean)
                 + "," + q + "rcSat" + q + ":" + N3(rs.satSteps) + "," + q + "rcBoosted" + q + ":" + N3(rs.boostedSteps)
             + "," + q + "driftAtClose" + q + ":" + F1(driftNow)
             + "," + q + "rcSum" + q + ":" + F1(rcSum)
             + "," + q + "driftBeforeSnap" + q + ":" + F1(before)
             + "," + q + "driftSameCall" + q + ":" + F1(sameCall)
             + "," + q + "snapReason" + q + ":" + N3(snap)
             + "," + q + "snapped" + q + ":" + N3(snap == 0 ? 1 : 0)
             + "," + q + "tick" + q + ":" + N3(g_cmPulseTick) + "}");
    w->nativeOn = 0; w->nativeSwings = 0; w->nativeTarget = 0; w->nativeOpenTick = 0;
}

// P059 - DropPuppet forgot a puppet that had a window open. The row must not stay a zombie.
void NoteNativeWindowDropped(unsigned int uid, bool regated)
{
    if (regated) ++g_ncClosedByDrop; else ++g_ncClosedByDropNoRegate;
    bool found = false;
    for (int i = 0; i < kMaxCombatWatch; ++i)
        if (g_cmWatch[i].uid == uid && g_cmWatch[i].nativeOn)
        {
            found = true;
            ++g_ncWindowsClosed;
            g_cmWatch[i].nativeOn = 0; g_cmWatch[i].nativeSwings = 0;
            g_cmWatch[i].nativeTarget = 0; g_cmWatch[i].nativeOpenTick = 0;
        }
    if (!found) ++g_ncDropNoRow;
}

long long CountOpenNativeWindows()
{
    long long n = 0;
    for (int i = 0; i < kMaxCombatWatch; ++i)
        if (g_cmWatch[i].uid != 0 && g_cmWatch[i].nativeOn) ++n;
    return n;
}

// P059 - `nativecombat off` closes every open window NOW (review finding 5): an open window with the
// switch off was reachable state, and a later `taskinstall on` would then install into an UNGATED
// puppet. Gate first, wait for the in-flight pass, clear orders+goals, then the shared tail.
void CloseAllNativeWindows(const char* reason)
{
    // Per window: gate, wait for THAT AI's pass (per-AI wait - review round 3), clear orders and
    // goals, then the shared tail. Combat mode is deliberately LEFT ON - the deployed
    // gated-puppet-in-combat state - and the authority's OFF edge ends it through the normal path.
    // The AI's own installed task is NOT ended here (round 3, B-3): ending it from this thread is
    // the F408 race, the per-frame task system reinstalls one next frame anyway, and the OFF edge
    // already carries that one accepted instance of the risk.
    for (int i = 0; i < kMaxCombatWatch; ++i)
    {
        CombatWatch* w = &g_cmWatch[i];
        if (w->uid == 0 || !w->nativeOn) continue;
        ::Character* ch = FindSpawned(w->uid);
        const bool resolved = PlausibleObject(ch);
        if (resolved)
        {
            SuppressCharacter(ch, true);
            if (!IsSuppressed(ch)) ++g_ncGateRefusedOnClose;
            else
            {
                const int qs = WaitForNoAiPassOn(ch, kNcQuiesceMs);
                if (qs == 1) ++g_ncCloseWaitedForPass; else if (qs == 2) ++g_ncCloseQuiesceTimedOut;
            }
            ClearAttackOrder(ch);
        }
        else ++g_ncCloseNoCharSwitch;
        ++g_ncClosedBySwitch;
        FinishNativeClose(w->uid, w, reason, resolved);
    }
}

void ApplyRemoteCombatMode(unsigned int uid, bool on, unsigned int targetUid)
{
    // **F385 - THE AUTHORITY CHECK ITS SIBLING HAS HAD SINCE F351, AND THIS DID NOT.**
    //
    // `ApplyRemoteSwing` opens with exactly this line, added with the note *"OnDespawn has one and
    // this did not"*. The same reasoning applies here verbatim and was never carried across - and
    // `OnCombatMode` is also the only inbound handler besides SWING that does not call
    // `RemoteMayWrite`.
    //
    // **It became load-bearing the moment P055 shipped.** Before, a misdirected COMBATMODE cost an
    // `initCombatMode(end)` and a `dropAllOrders`. Now the OFF branch calls `finishAction`, which
    // **ends and DELETES whatever Task is installed regardless of who installed it** - so on a
    // collision it destroys the current action of a character THIS machine is the authority for.
    // That is the "the engine installed it and we destroyed it" class that killed T075 and T076.
    //
    // The collision WAS not hypothetical: the old uid scheme was `(pidPrefix << 24) | counter` with an 8-bit
    // process-derived prefix, so two instances collided about 1 in 256 with both counters starting at 1. Since M4 a uid
    // is (the making game's seat << 22) | a counter the notebook grants in blocks that never repeat for the seat
    // (src/common/uidlayout.h, uidblock.h), so two games cannot mint one uid; this test stays as the ownership rule.
    if (net::IsUidMine(uid)) { ++g_cmNotOurs; return; }

    ::Character* c = FindSpawned(uid);
    if (c == 0)
    {
        ++g_cmRefused;
        DebugLog("[M6] COMBATMODE for unknown uid=" + N3(uid) + " - no local copy (F079)");
        // P059 - a window cannot be closed properly without the character; do not leave the row open.
        CombatWatch* zw = CombatWatchFor(uid);
        if (zw != 0 && zw->nativeOn) { ++g_ncCloseNoChar; FinishNativeClose(uid, zw, "no_character", false); }
        return;
    }

    CombatClass* cc = CombatOf(c);
    if (cc == 0)
    {
        ++g_cmRefused;
        ErrorLog("[M6] COMBATMODE uid=" + N3(uid) + " - no readable CombatClass, refused");
        return;
    }

    int before = CombatModeOf(cc);

    // P030/F246: remember what we were TOLD, so the peer-side watcher can tell "we stopped it"
    // from "it stopped itself".
    {
        CombatWatch* w = CombatWatchFor(uid);
        if (w != 0) w->weWantOn = on ? 1 : 0;
    }

    if (on)
    {
        ::Character* target = FindSpawned(targetUid);
        if (!PlausibleObject(target))
        {
            // Refuse rather than enter combat against nothing. An engine told to fight a target
            // it cannot resolve is a worse state than one standing still, and it is the kind of
            // thing that ends in a crash rather than in a wrong picture.
            ++g_cmRefused;
            ErrorLog("[M6] COMBATMODE uid=" + N3(uid) + " target uid=" + N3(targetUid)
                     + " does not resolve here - refused");
            return;
        }

        // The target's own handle, taken LOCALLY. No hand crosses the wire: handles are
        // per-process, so the authority's would name a different object on this machine.
        const hand& h = target->getHandle();
        cc->enterCombat(h, 0, true);

        // F239 - AND GIVE IT THE ORDER. T072 measured what was missing and it is one line of
        // the digest: with `mode=1` on both sides, the authority reads
        // `goal='Attacking target' orders=1` while the peer reads `goal='Aimless' orders=0`.
        // **Combat mode arrived; no goal and no orders came with it.**
        //
        // 5 = TASK_MELEE_FOCUSED, the same order the authority's own fighter is given.
        // `InjectAttackOrder` hands it over and allows exactly ONE decision pass so a
        // suppressed puppet can adopt it - which is what PulseCharacter was built for
        // (F081/F082) - and does NOT un-suppress, because T071 measured what that costs:
        // 491 units of drift and a self-chosen 'Self preservation' goal.
        // P059 (review round 2, B6): with a native window OPEN this AI is free-running, and an
        // order must not be written into it here. The NC retarget block below gates, waits, writes
        // and releases instead.
        CombatWatch* ow = CombatWatchFor(uid);
        const bool windowOpenNow = (ow != 0 && ow->nativeOn != 0);
        bool earlyOrdered = false;
        if (g_combatOrders && !windowOpenNow)
        {
            earlyOrdered = InjectAttackOrder(c, target, 5);
            if (earlyOrdered) ++g_cmOrders; else ++g_cmOrderFailed;
        }

        // P055 / F384 - **AND THE FIFTH ATTEMPT: HAND IT THE TASK ITSELF, WITH NO DECISION PASS.**
        //
        // Deliberately alongside the order injection rather than replacing it, and independently
        // switchable, so the two can be measured apart. They are different mechanisms: an order is
        // a request the AI must adopt; **a current action is the thing `CharBody::update`
        // actually dispatches** (F367/F369), and nothing in between has to run.
        // P057 - **ARM THE CHARACTER FIRST, THEN THROW AWAY THE ENGINE'S CACHED "NO".**
        // Ordered before the melee install deliberately: the requirements check reads the weapon,
        // and it answers from the memo before it reads anything at all, so both have to happen
        // before the attempt or the attempt is answered by a stale entry.
        if (g_weaponTask)
        {
            ++g_wpAttempts;
            CharBody* wbody = (CharBody*)*(void* const*)((const char*)c + kCharBodyOff);
            if (!PlausibleObject(wbody)) ++g_wpNoBody;
            else
            {
                // **READ BEFORE AND AFTER, and the BEFORE decides whether we install at all.**
                // Without the before-read, "it has a weapon now" cannot be told from "it had one
                // all along" - and on the host every character already does, so an ON arm would
                // report success on the control population without having done anything.
                unsigned int offBefore = 0;
                const void* wepBefore = WeaponInHandsOf(c, &offBefore);

                bool installed = false;
                if (wepBefore == kWeaponUnreadable) ++g_wpUnreadable;
                else if (wepBefore != 0) ++g_wpAlreadyArmed;
                else
                {
                    // TASK_DRAW_WEAPON. `needsTarget = 0`, so a NULL target is legal and is what the
                    // engine's own callers pass. All the work happens inside this call.
                    installed = true;
                    const void* wasTask =
                        *(const void* const*)((const char*)wbody + kCurrentTaskOff);

                    const bool okEquip =
                        wbody->startAction(TASK_DRAW_WEAPON, (RootObject*)0);

                    // **THREE OUTCOMES USED TO COLLAPSE INTO `stillEmpty`, AND THE ADJACENT MELEE
                    // BLOCK HAS A HEADING THAT SAYS NOT TO DO THIS.** The factory can decline (no
                    // task, `startAction` never runs); the requirements check can refuse TaskType 6
                    // itself (`startAction` never runs, and the field is reset to 0); or the task
                    // can run and find nothing to draw. **Only the third is "the engine tried and
                    // there was no weapon"** - the others are the install failing, which is a
                    // different question with a different fix.
                    //
                    // The field read separates the RAN case from the DID-NOT-RUN cases.
                    //
                    // **AND THE RETURN VALUE SEPARATES THE OTHER TWO - WHICH SHARPENS F385 RATHER
                    // THAN CONTRADICTING IT.** F385 is quoted throughout this file as
                    // *"`startAction` returns `mov al,1` unconditionally"*. Read from the
                    // exe, that is true only of the path **past the factory check**:
                    //
                    //     0x5C5C6B  call    <task factory>
                    //     0x5C5C70  mov     rsi,rax
                    //     0x5C5C73  test    rax,rax
                    //     0x5C5C76  jnz     0x5C5C88          ; got a Tasker -> carry on
                    //     0x5C5C78  ...epilogue...  ret       ; **AL IS NOT SET HERE**
                    //     0x5C5CCC  mov     al,1              ; the unconditional one
                    //     0x5C5CCE  ...epilogue...  ret
                    //
                    // On the factory-NULL path AL is never written - and RAX is **0**, which is why
                    // that branch was taken, so AL is 0 and the function genuinely returns FALSE.
                    // **So `false` means the factory declined, and `true` says nothing about
                    // whether the install then succeeded**, which is exactly what F385 warns about.
                    // Both statements are true and they are about different halves of the function.
                    const void* nowTask =
                        *(const void* const*)((const char*)wbody + kCurrentTaskOff);

                    // P058 - **THE CONTROL HALF OF THE CANDIDATE-2 TEST.** This install works
                    // (T100: `nowArmed = 26`), so whatever its `taskData` reads is what a WORKING
                    // install looks like on this instance. Scored only when a task is actually
                    // present: a null field here is the ordinary refusal outcome, and folding it
                    // into `unread` would make a routine result look like a failed instrument.
                    if (nowTask != 0) ScoreTaskData(&g_p58Equip, nowTask);

                    unsigned int offAfter = 0;
                    const void* wepAfter = WeaponInHandsOf(c, &offAfter);

                    if (wepAfter == kWeaponUnreadable) ++g_wpAfterUnreadable;
                    else if (wepAfter != 0)           ++g_wpNowArmed;
                    // **`nowTask == 0` ALREADY IMPLIES THE TASK NEVER RAN**, so the
                    // `&& wasTask == 0` this used to carry only misclassified: a puppet that DID
                    // hold a task and then had the requirements refuse (which zeroes the field)
                    // fell through to `stillEmpty` - *"it RAN and drew nothing"* - when
                    // `startAction` never ran and we had just destroyed its previous action.
                    else if (nowTask == 0)
                    {
                        // No task installed AFTER the call, which means the task never ran.
                        if (!okEquip) ++g_wpFactoryDeclined;
                        else          ++g_wpReqRefused;
                    }
                    else ++g_wpStillEmpty;   // it RAN and drew nothing
                }

                // ---------------------------------------------------------------------------
                // **THE MEMO EVICTION USED TO LIVE HERE. IT IS GONE, AND IT WAS DELETED RATHER THAN
                // MADE SAFE. F407.**
                //
                // P057 bundled two things into `taskweapon`: the `TASK_DRAW_WEAPON` install above, which
                // WORKS (T100: `nowArmed = 26`), and a `std::map::clear` on the requirements memo at
                // `AI+0x238`, which does not.
                //
                // **The premise the eviction was built on is measurably false.** P057's own comment
                // read: *"the requirements check answers from the memo before it reads anything at
                // all... the cached `false` answers forever. Arming the character and re-asking
                // would change nothing at all."* **T101 measured `evaluated=100 servedFromCache=28
                // neverAsked=0` against `attempts=128`** - 78% of attempts evaluated the
                // requirements FRESH and refused anyway. **Evicting the memo could not have helped
                // the 100 attempts that never consulted it.**
                //
                // **And it was never safe.** F401: `IsQuiescedForMemoryAccess` guards against
                // `AI::periodicUpdate` and nothing else, while `AI::frameTick4` ->
                // `AITaskSytem::update4Frame` -> `setCurrentTask` ->
                // `CharBody::startAction(Tasker*)` -> `TaskData::_isRequirementsComplete`
                // **inserts into that same red-black tree on three exit paths, every frame, on the
                // AI worker thread, needing no pulse.** Bracketing it with the same in-flight count
                // would not have fixed it either: that guard is sound only because every site that
                // arms a pulse is main-thread, and `update4Frame` needs no pulse.
                //
                // **T100 ran this for ~1,550 s and did not crash. That is not evidence of safety -
                // it is a narrow window that was not hit.**
                //
                // So the fix that was on the table - a new gate on `AITaskSytem::setCurrentTask`
                // (0x50CAB0), on a hot path, unbuilt and unreviewed - **is not needed. Deleting
                // code that does not work is strictly better than guarding it.** (F407)
                //
                // `ClearMemoOf`, `kMapClearRva` and the eight `g_wpMemo*` counters go with it.
                // `MemoSizeOf` STAYS: it is a pure read, it is what measured U-13, and the
                // `[P057] MEMO across the melee install` line below still needs it.
                // ---------------------------------------------------------------------------
            }
        }

        if (g_taskInstall)
        {
            ++g_tiAttempts;
            CharBody* body = (CharBody*)*(void* const*)((const char*)c + kCharBodyOff);
            if (!PlausibleObject(body)) ++g_tiNoBody;
            else
            {
                // TASK_MELEE_FOCUSED. The enum's own position confirms 5 independently of the
                // factory dispatch table F375 read.
                // **READ BEFORE AS WELL AS AFTER.** Reading only afterwards cannot tell
                // *we installed it* from *it was already there* - and this whole probe exists to
                // stop inferring. The OFF branch four blocks down already reads-before for exactly
                // this reason; the ON branch did not, which is the same asymmetry twice in one
                // function.
                const void* wasTask = *(const void* const*)((const char*)body + kCurrentTaskOff);

                // P058 - **STAMPED BEFORE THE CALL, NOT AFTER IT.** `startAction` ends the
                // previous action on its way in, and the requirements-refusal path ends the new one
                // on its way out - so a stamp placed after the call would miss every removal that
                // happened INSIDE it, which is two of F399's three candidates. Stamping first costs
                // nothing and makes those sites attributable.
                //
                // **Only the melee path is watched.** The equip install ends actions too, and a
                // body watched for both would report one set of sites for two different questions.
                WatchBodyForEndAction((void*)body);

                // P057 / U-13 - **THE MEMO SIZE ACROSS THE CALL. THIS IS THE WHOLE STICKINESS
                // MEASUREMENT AND IT IS TWO FIELD READS.** `_isRequirementsComplete` inserts on
                // BOTH exits, so the map growing means it EVALUATED and the map standing still
                // means it answered from cache. A detour on a hot AI function was designed for
                // this and is not needed.
                const size_t memoBefore = MemoSizeOf(body);

                const bool okInstall =
                    body->startAction(TASK_MELEE_FOCUSED, (RootObject*)target);

                const size_t memoAfter = MemoSizeOf(body);
                const void* nowTaskForMemo =
                    *(const void* const*)((const char*)body + kCurrentTaskOff);

                if (memoBefore == kMemoUnreadable || memoAfter == kMemoUnreadable)
                    ++g_tiMemoUnread;
                else if (memoAfter > memoBefore) ++g_tiMemoGrew;
                else if (nowTaskForMemo == wasTask && nowTaskForMemo != 0)
                {
                    // **"THE MEMO DID NOT GROW" IS NOT "THE MEMO ANSWERED", AND THE DIFFERENCE
                    // DECIDES WHETHER U-13 IS CONFIRMED.** The requirements check is never even
                    // reached when the factory declines, or when the installer finds the incoming
                    // task is the SAME as the current one - it deletes the new Tasker and returns
                    // without asking. Both leave the map untouched and would have been scored as
                    // *served from cache*.
                    ++g_tiMemoNotAsked;
                }
                else ++g_tiMemoHit;

                // P057 / U-14 - **READ THE REFUSAL CODE ONLY ON AN ACTUAL REFUSAL**, not merely on
                // an evaluation.
                //
                // The first version gated it on `memoGrew`, which is **true on SUCCESS too** - the
                // success exit inserts into the memo and writes NOTHING through the out-parameter.
                // So the moment this fix works, the field named "the refusal StateType" would have
                // held a stale value from somebody else's decision.
                //
                // **And even here it is a racy read.** Four other call sites pass that same global,
                // all of them under `AI::periodicUpdate`, which runs continuously on the AI worker
                // thread for every host-owned and world character. `memoGrew` says which check
                // evaluated; it cannot say which thread wrote last. Hence the name.
                if (nowTaskForMemo == 0)
                {
                    const int code = ReadFailedOn();
                    if (code != -1) { g_tiLastFailedOnUnverified = code; ++g_tiFailedOnReads; }
                }

                // **DO NOT TRUST THAT RETURN VALUE. READ THE FIELD.**
                //
                // Disassembled, `startAction(TaskType, RootObject*)` ends
                // `call [rax+0x18] / mov al,1 / ret` - **it DISCARDS the inner installer's result
                // and returns true unconditionally.** The only `false` it can produce is the task
                // factory declining. So every refusal INSIDE the installer - including the
                // requirements check, which is the one most likely to reject a puppet - would have
                // been counted as a success.
                //
                // This is P046's lesson, which this same file already learned about
                // `setTarget` and which I did not apply here: **read the result of your
                // own call at the site of the call.** The vtable constants for the comparison were
                // already in this file for P050.
                const void* nowTask = *(const void* const*)((const char*)body + kCurrentTaskOff);
                uintptr_t   nowVt   = 0;
                if (PlausibleObject(nowTask)) nowVt = (uintptr_t)*(const void* const*)nowTask;

                // P058 - **SCORED ON EVERY PLAUSIBLE TASK, NOT ONLY THE MELEE ONE.** Restricting it
                // to the melee branch would leave the `installedOther` case - a readable task that
                // is not ours - unmeasured, and that bucket is one of the two ways candidate 3
                // ("the field never held a melee task at all") shows up. Reading the key tells us
                // WHICH task is sitting there, which is strictly more than "not ours".
                //
                // **SPLIT THREE WAYS, AND BOTH SPLITS ARE LOAD-BEARING.**
                //   * identity against the pre-call read - did OUR call put it there at all;
                //   * the vtable - is it actually a TASK_MELEE_FOCUSED `Tasker`.
                // Only the intersection is evidence about our melee install. The MELEE column's
                // reading line asserts *"a real Tasker of the right type"*, and the identity split
                // alone cannot see the type - `installedPlainMelee`, `installedOther` and an
                // unreadable non-null pointer all pass it. See `g_p58OtherNew` for the failure
                // this ordering prevents; it is F399's candidate 1 masquerading as candidate 2.
                //
                // `nowVt` is the one computed three lines above for the outcome counters, reused
                // rather than recomputed - two copies of a classification rule drifting apart is a
                // defect this file has paid for twice (F392: two loops, two locals both called
                // `measurable`, and a new disqualifier landed in only one of them).
                if (nowTask != 0)
                {
                    TaskDataProbe* which =
                        (nowTask == wasTask)                              ? &g_p58Preexisting
                      : (nowVt == g_imageBase + kVtblFocusedMeleeAttack)  ? &g_p58Melee
                                                                          : &g_p58OtherNew;
                    ScoreTaskData(which, nowTask);
                }

                if (nowVt == g_imageBase + kVtblFocusedMeleeAttack)
                {
                    ++g_tiOk;
                    // Was it already there before we asked? Then `ok` is not evidence the call did
                    // anything.
                    if (wasTask == nowTask) ++g_tiOkAlready;
                }
                // **THE OTHER MELEE VARIANT GETS ITS OWN COUNTER**, because this file's own rule
                // (see kVtblMeleeAttack) is that merging them makes a reading unintelligible:
                // `Task_MeleeAttack` does NOT dispatch `go`, so "installed, and it is the
                // variant that never runs the machine" is arguably the most informative outcome
                // this feature can produce - and it would have landed in a bucket labelled
                // "something is there and it is not ours".
                else if (nowVt == g_imageBase + kVtblMeleeAttack) ++g_tiInstalledPlainMelee;
                else if (nowTask == 0)
                {
                    // Nothing installed. With `okInstall` true this is the requirements check
                    // rejecting it INSIDE the installer (it destroys the previous action on its way
                    // out) - positively identified rather than inferred from a return value that
                    // cannot report it.
                    ++g_tiRefused;
                    if (okInstall) ++g_tiRefusedInside;
                }
                // A pointer that is non-null and would not READ lands here too - `nowVt` stays 0.
                // Counted apart, because "a different task is installed" and "the field holds
                // something unreadable" are different situations and one of them is a red flag.
                else if (nowTask != 0 && nowVt == 0) ++g_tiTaskUnreadable;
                else ++g_tiInstalledOther;   // a different, readable task is the current action
            }
        }
        // H015 / P059 - OPEN THE NATIVE WINDOW. Order FIRST, gate SECOND: the first free decision
        // pass must find the authority's order waiting, or it chooses for itself (T071's failure).
        if (g_nativeCombat)
        {
            CombatWatch* w = CombatWatchFor(uid);
            if (w == 0) ++g_ncOpenNoWatch;
            else if (w->nativeOn)
            {
                // RETARGET inside an open window. An order must never be written into a FREE-RUNNING
                // AI (review finding 3; replicate.cpp's own rule at ApplyRemoteTask): gate, wait out
                // the pass in flight, write, release. `combatorders on` already wrote one - skip.
                ++g_ncRetargeted;
                w->nativeTarget = targetUid;
                {
                    SuppressCharacter(c, true);
                    const bool gated = IsSuppressed(c);
                    if (!gated) ++g_ncGateRefusedOnRetarget;
                    const int qs = WaitForNoAiPassOn(c, kNcQuiesceMs);
                    if (qs == 2) ++g_ncRetargetQuiesceTimedOut;
                    // Review 4 item 6: both failures were counted and then IGNORED. A refused gate or a pass
                    // still in flight now refuses the write, exactly as the OPEN path refuses.
                    if (!gated || qs == 2) ++g_ncRetargetRefusedWrite;
                    else if (!InjectAttackOrder(c, target, 5)) { ++g_ncOrderFailed; ++g_ncRetargetOrderFailed; }
                    SuppressCharacter(c, false);
                }
            }
            else
            {
                // OPEN. The order is written while the AI is still gated (it is), then the gate opens.
                // A free AI with NO order is T071's failure, so a failed write refuses the open.
                const bool ordered = earlyOrdered || (!g_combatOrders && InjectAttackOrder(c, target, 5));   // the OUTCOME, not the switch (round 3, B-1)
                if (!ordered) { ++g_ncOrderFailed; ++g_ncOpenRefusedNoOrder; }
                else
                {
                    if (!IsSuppressed(c)) ++g_ncOpenWasNotGated;   // release below is then a no-op
                    // Stop the stale push BEFORE the gate opens, so the AI's first movement write is
                    // not clobbered by our zero vector (review round 2, B8).
                    if (!SetPuppetNativeWindow(uid, true)) ++g_ncOpenNoPuppet;
                    SuppressCharacter(c, false);
                    w->nativeOn = 1; w->nativeOpenTick = g_cmPulseTick;
                    w->nativeSwings = 0; w->nativeTarget = targetUid;
                    w->nativeOpenDrift = PuppetWindowDriftNow(uid);   // combat2 (F920): the gap the window starts with
                    ++g_ncWindowsOpened;
                    const std::string q(1, (char)34);
                    DebugLog("[NC] window OPEN uid=" + N3(uid) + " target=" + N3(targetUid)
                             + " openDriftXZ=" + F1(w->nativeOpenDrift)
                             + " - AI released, authority's attack order queued, drive suspended");
                    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "window_open" + q
                             + "," + q + "uid" + q + ":" + N3(uid)
                             + "," + q + "target" + q + ":" + N3(targetUid)
                             + "," + q + "tick" + q + ":" + N3(g_cmPulseTick) + "}");
                }
            }
        }
    }
    else
    {
        // H015 / P059 - CLOSE THE NATIVE WINDOW, GATE FIRST. Everything below (clear orders, clear
        // goals, leave combat, end action) must run with the decision gate already shut, or one
        // pass slips through and the AI picks `Self preservation` (T075: 73 ms).
        CombatWatch* ncw = CombatWatchFor(uid);
        const bool wasNative = (ncw != 0 && ncw->nativeOn != 0);
        if (wasNative)
        {
            SuppressCharacter(c, true);
            if (!IsSuppressed(c)) ++g_ncGateRefusedOnClose;
            // The gate stops NEW passes. The one already running on the worker finishes on its own
            // schedule; the clears below must not race it (review finding 2). Bounded wait.
            const int qs = WaitForNoAiPassOn(c, kNcQuiesceMs);
            if (qs == 1) ++g_ncCloseWaitedForPass; else if (qs == 2) ++g_ncCloseQuiesceTimedOut;
        }

        // end == 2 is the engine's own "leave combat mode" branch. The hand is required by the
        // signature and unused there, so the character's own is passed.
        const hand& h = c->getHandle();
        cc->enterCombat(h, 2, false);

        // F242 - AND TAKE THE ORDER BACK. Leaving combat mode is not enough on its own: T073
        // cleared the mode and left the order, and the peer kept fighting for 87 seconds after
        // the authority stopped - 756 of its 947 swings happened after the fight was over.
        // Cleared even when injection is off: a build that stops injecting must still be able
        // to clean up after one that did, and clearing orders nobody set is harmless.
        if (ClearAttackOrder(c)) ++g_cmOrdersCleared;

        // P055 - **AND TAKE THE TASK BACK.** F242's rule, which this project has now paid for
        // twice: *anything we SET on a puppet, we must be able to UNSET.* An order left behind
        // kept a peer fighting for 87 seconds after the authority stopped, and a task is a
        // stronger level than an order - **nothing in the engine ever removes one on its own**
        // (F375: "finished" is a flag the AI reads on its next pass, and the AI is gated).
        //
        // Retracted even when installation is switched OFF, for the same reason orders are: a
        // build that stops installing must still clean up after one that did, and ending an
        // action nobody set is harmless.
        {
            CharBody* body = (CharBody*)*(void* const*)((const char*)c + kCharBodyOff);
            if (!PlausibleObject(body)) ++g_tiEndNoBody;   // every early return gets a counter
            else
            {
                // Read BEFORE, so `ended` counts what was actually ended rather than how often we
                // asked. A retraction against an empty field is not the same event as one that
                // destroyed a task, and one counter for both hides the difference.
                const bool had = (*(const void* const*)((const char*)body + kCurrentTaskOff) != 0);
                body->finishAction();
                if (had) ++g_tiEnded; else ++g_tiEndedEmpty;
            }
        }

        if (wasNative) FinishNativeClose(uid, ncw, "authority_off", true);
    }

    int after = CombatModeOf(cc);
    ++g_cmApplied;

    // BEFORE and AFTER, not just "applied". A call that returns without changing the byte is
    // the failure this feature is most likely to have, and it must be visible in the log rather
    // than inferred from a screenshot later - F193's lesson, applied up front this time.
    DebugLog("[M6] COMBATMODE applied uid=" + N3(uid) + " on=" + N3(on ? 1 : 0)
             + " target=" + N3(targetUid)
             + " combatModeActive " + N3(before) + " -> " + N3(after)
             + (on && after == 0 ? "  <== ASKED FOR COMBAT AND THE BYTE DID NOT CHANGE" : ""));
}

// F348 - APPLY A SWING ON THE PEER, WITH THE AI UNINVOLVED.
//
// The engine's own attack path (`whoAttacksYouOrMe` -> `startupState` ->
// `AttackState::initialiseAttack` -> `attackState`) begins at three field writes, and
// `decisionState` is not on that path at all - it cannot start a swing, since it only ever writes
// nextMove = 1, 5, 6 or 10. So the puppet's decision gate is never opened, which is the difference
// between this and the three attempts recorded above, all of which had to open it and all of which
// produced a free-running clone (T071/T075/T076).
//
// **F354 - THE SWING IS AN INTENT THAT RETRIES, NOT A ONE-SHOT THAT IS DROPPED.**
//
// The first version applied seven gates once, at message-arrival time, and abandoned the blow if
// any of them was closed. Every one of those gates is copied from a predicate the engine itself
// re-evaluates **every frame** inside `CombatClassAI::update` - `isActionAnimating`
// in particular is true through the whole of an attack recovery and a hit reaction, which is
// precisely when a follow-up blow arrives. A one-shot gate there silently loses swings, and two of
// the gates can LATCH (`hasForcedWP` is cleared by a movement update a `steerDirectly`-driven
// puppet bypasses; `targetTraceBlocked` is an async trace result on a puppet whose decisions are
// suppressed), so a latched one would refuse **every** swing for that puppet for the rest of the
// session - the same outcome class as the F351 defect, reached by a different door.
//
// That is the modding skill's design principle 4 in as many words: *a gated action that defers
// must not consume its trigger; either the tick re-attempts until the action completes or a
// recurring event re-arms it, and the deferral logs so silence is diagnosable.* So the message
// records an INTENT, `CombatModeTick` re-attempts it every frame, and what is counted at expiry is
// **which gate was still closed** - which is a far more useful number than "we refused once".
//
// MAIN THREAD ONLY - it touches the engine's combat object, same as ApplyRemoteCombatMode.
void ApplyRemoteSwing(unsigned int uid, unsigned int targetUid)
{
    ++g_swRecv;
    if (!g_swingRepl) { ++g_swDisabled; return; }

    // F351 - AUTHORITY CHECK. `OnDespawn` has one and this did not, so a SWING naming a uid WE
    // author would have forced our own character to swing on a remote peer's say-so. The send is
    // gated on `mine` at the other end, so this needs a buggy or hostile peer to reach - which is
    // exactly the condition an authority check exists for, and it is one line. It also closes a
    // hole in the arithmetic: the scoring block only runs for puppets.
    if (net::IsUidMine(uid)) { ++g_swNotOurs; return; }

    // The two conditions that can never become true by waiting are still resolved here. Everything
    // that CAN change from one frame to the next is left to the retry.
    ::Character* c = FindSpawned(uid);
    if (c == 0) { ++g_swNoChar; return; }
    if (CombatOf(c) == 0) { ++g_swNoCombat; return; }

    CombatWatch* w = CombatWatchFor(uid);
    if (w == 0) { ++g_swNoWatch; return; }

    if (w->swingIntent) ++g_swIntentSuperseded;   // the previous blow never got its chance
    w->swingIntent       = 1;
    w->swingIntentTick   = g_cmPulseTick;
    w->swingIntentTarget = targetUid;
    w->swingIntentGate   = SW_GATE_NONE;
    w->swingIntentLastTry = g_cmPulseTick;
    ++g_swIntents;
}

// P055 / P057 / F408 - **THE FORBIDDEN COMBINATION IS NOW AN INTERLOCK, NOT A PARAGRAPH.**
//
// `(taskweapon on, taskinstall on)` has been barred since T100 for a transient-`Tasker` hazard, and
// the bar lived entirely in comments and test plans. **Two switches that must never both be on, with
// nothing in the code that stops it, is one typo away at any point in any run** - and the failure it
// guards is a use-after-free on two live instances, not a bad number in a log.
//
// **A deferral was built to retire the hazard and was REVERTED (F408): a delay cannot close a race it
// does not synchronise.** The free runs on the main thread; `AITaskSytem::update4Frame` latches
// `CharBody+0x68` into a register at 0x50E740 and holds it across four nested calls on the AI worker
// thread; there is no lock, no epoch and no quiescence signal covering that path. **Only the gate at
// `AITaskSytem::setCurrentTask` (0x50CAB0) can lift this, and it is unbuilt.**
//
// **The refusal is COUNTED and LOUD**, not silent: an operator who types the second switch must see
// why it did not take effect, or they will conclude the switch is broken.
long long g_switchInterlockRefused = 0;

bool RefuseIfBothTaskSwitches(const char* asking)
{
    if (!g_taskInstall || !g_weaponTask) return false;
    ++g_switchInterlockRefused;
    ErrorLog(std::string("[P055/P057] **REFUSED: `") + asking + "` would leave BOTH taskinstall AND"
             " taskweapon ON, which is FORBIDDEN.** One apply would install the equip task and then,"
             " microseconds later, end and DELETE it to install the melee task - in `CharBody+0x68`,"
             " which the AI worker thread latches into a register and holds across four nested calls"
             " every frame. That is a use-after-free, not a bad measurement."
             "  |  **A six-tick deferral was built to fix this and was reverted (F408)** - a delay"
             " cannot close a race it does not synchronise, and it made the SAFER of the two frees"
             " more dangerous by giving the worker six frames to latch the object first."
             "  |  **The switch you just sent is NOT in effect.** Turn the other one off first."
             " Lifting this needs the gate at AITaskSytem::setCurrentTask (0x50CAB0), which is"
             " unbuilt and unreviewed.");
    return true;
}

void SetNativeCombat(bool on)
{
    if (on && (g_taskInstall || g_weaponTask))
    {
        ++g_ncSwitchRefused;
        ErrorLog("[NC] REFUSED: nativecombat on while taskinstall/taskweapon is on. Those install"
                 " tasks into a GATED puppet; this releases the gate. Turn them off first.");
        return;
    }
    g_nativeCombat = on;
    if (!on) CloseAllNativeWindows("switch_off");
    if (on)
        ErrorLog("[NC] NATIVE COMBAT ON (H015/P059) - on the authority's combat-ON edge a puppet's"
                 " AI is RELEASED with the authority's attack order queued and the position drive"
                 " suspended; on the OFF edge it is re-gated, orders+goals cleared, and placed at"
                 " the authority's position. Its own blows stay refused by M3. Windows currently"
                 " open keep running until their OFF edge.");
    else
        DebugLog("[NC] native combat OFF - every open window was closed by the switch (see CLOSE lines with reason=switch_off).");
}

void SetTaskInstall(bool on)
{
    if (on && (g_nativeCombat || CountOpenNativeWindows() > 0)) { ++g_ncSwitchRefused; ErrorLog("[NC] REFUSED: taskinstall on while nativecombat is on or a window is open."); return; }
    // **CHECKED BEFORE THE WRITE, and the write is skipped on refusal.** Setting the flag and then
    // complaining would leave the forbidden state live for the rest of the run.
    if (on && g_weaponTask) { RefuseIfBothTaskSwitches("taskinstall on"); return; }
    g_taskInstall = on;
    if (on)
        ErrorLog("[P055] TASK INSTALL ON - puppets entering combat mode will be handed a"
                 " TASK_MELEE_FOCUSED Task directly as their current action, with NO AI decision"
                 " pass (F384). This is the FIFTH attempt at P-15 and the first that does not route"
                 " through the decision pass that broke the other four. Retraction on combat-mode"
                 " OFF is unconditional.");
    else
        DebugLog("[P055] task install OFF - the pre-F384 behaviour. Retraction still runs, because"
                 " a build that stops installing must clean up after one that did.");
}

void SetWeaponTask(bool on)
{
    if (on && (g_nativeCombat || CountOpenNativeWindows() > 0)) { ++g_ncSwitchRefused; ErrorLog("[NC] REFUSED: taskweapon on while nativecombat is on or a window is open."); return; }
    if (on && g_taskInstall) { RefuseIfBothTaskSwitches("taskweapon on"); return; }
    g_weaponTask = on;
    if (on)
        ErrorLog("[P057] WEAPON TASK ON - a puppet entering combat mode with EMPTY HANDS will be"
                 " handed the engine's own TASK_DRAW_WEAPON task. **AND THAT IS NOW THE ONLY THING THIS"
                 " SWITCH DOES: the memo eviction it used to carry is DELETED (F407), not disabled.**"
                 " It evicted a cache that 78% of install attempts never consulted (T101:"
                 " evaluated=100 servedFromCache=28 of attempts=128), and it raced an ungated"
                 " worker-thread writer into the same red-black tree (F401)."
                 "  |  **THIS BUILD STILL CALLS drawWeapon, WHICH REACHES THE OGRE SKELETON"
                 " ATTACHMENT PATH, THE ANIMATION CLASS AND THE AUDIO ENGINE, SYNCHRONOUSLY ON THIS"
                 " THREAD** - that risk is unchanged and is the reason the switch defaults OFF."
                 " T099 measured why it is needed at all: 0 of 118,171 client samples had a weapon"
                 " in hand against 129,631 of 129,631 on the host.");
    else
        DebugLog("[P057] weapon task OFF - no TASK_DRAW_WEAPON install. (There is no longer a memo"
                 " eviction to switch off; it was deleted in the F407 build.) This is the control"
                 " arm, and unlike a switch that only gates a counter it genuinely changes what the"
                 " engine is asked to do.");
}

void SetSwingReplication(bool on)
{
    g_swingRepl = on;
    DebugLog(std::string("[M6] swing replication ") + (on ? "ON" : "OFF")
             + " - F348: the peer is told WHEN a blow starts and its own engine picks the"
               " technique. The AI decision gate is not touched either way.");
}

void SetCombatOrders(bool on)
{
    g_combatOrders = on;
    DebugLog(std::string("[M6] combat ORDER injection ") + (on ? "ON" : "OFF")
             + " - F251: three variants were measured and all three made the peer autonomous."
               " ON is for testing a different approach, not for shipping.");
}

void SetCombatPulse(bool on)
{
    g_combatPulse = on;
    DebugLog(std::string("[M6] combat decision pulse ") + (on ? "ON" : "OFF")
             + " - a puppet in combat mode gets one decision pass every "
             + N3(kPulseEveryTicks) + " frames (F233)");
}

void ReportCombatMode()
{
    {
        long long openNow = 0;
        for (int i = 0; i < kMaxCombatWatch; ++i)
            if (g_cmWatch[i].uid != 0 && g_cmWatch[i].nativeOn) ++openNow;
        const std::string q(1, (char)34);
        DebugLog("[NC] REPORT nativeCombat=" + N3(g_nativeCombat ? 1 : 0) + " visPeakLinesLocal=" + N3(g_visPeakLinesLocal)
                 + " windowsOpened=" + N3(g_ncWindowsOpened) + " windowsClosed=" + N3(g_ncWindowsClosed)
                 + " openNow=" + N3(openNow) + " retargeted=" + N3(g_ncRetargeted)
                 + " openNoWatch=" + N3(g_ncOpenNoWatch) + " orderFailed=" + N3(g_ncOrderFailed)
                 + " windowSwings=" + N3(g_ncWindowSwings)
                 // combat1 (user: monitor the default-on window and keep it close to single player): per closed window, the
                 // average and worst of its largest horizontal gap to the owner, the average of reconcile's mean gap, and how
                 // many windows passed 5 units.
                 + " parity[windows,maxDriftAvg,maxDriftWorst,meanDriftAvg,over5,noSample]=" + N3(g_ncParityWindows)
                 + "," + F1(g_ncParityWindows > 0 ? (float)(g_ncParityMaxDriftSum / (double)g_ncParityWindows) : 0.0f)
                 + "," + F1(g_ncParityMaxDriftWorst)
                 + "," + F1(g_ncParityWindows > 0 ? (float)(g_ncParityDriftMeanSum / (double)g_ncParityWindows) : 0.0f)
                 + "," + N3(g_ncParityOver5) + "," + N3(g_ncParityNoSample)
                 // combat2: the average gap a window OPENED with, and windows whose peak grew >5 units past it
                 + " parityOrigin[openDriftAvg,openDriftN,grewOver5]="
                 + F1(g_ncParityOpenDriftN > 0 ? (float)(g_ncParityOpenDriftSum / (double)g_ncParityOpenDriftN) : 0.0f)
                 + "," + N3(g_ncParityOpenDriftN) + "," + N3(g_ncParityGrew5)
                 + " peerSwingsWhileAuthorityOff=" + N3(g_ncSwingsWhileAuthOff)
                 + " snapDone=" + N3(g_ncSnapDone) + " snapDeclined=" + N3(g_ncSnapDeclined)
                 + " orphanedByRebind=" + N3(g_ncOrphanedByRebind)
                 + " switchRefused=" + N3(g_ncSwitchRefused)
                 + " closedBySwitch=" + N3(g_ncClosedBySwitch) + " closedByDrop=" + N3(g_ncClosedByDrop)
                 + " closedByDropNoRegate=" + N3(g_ncClosedByDropNoRegate) + " closeNoChar=" + N3(g_ncCloseNoChar)
                 + " closeWaitedForPass=" + N3(g_ncCloseWaitedForPass) + " closeQuiesceTimedOut=" + N3(g_ncCloseQuiesceTimedOut)
                 + " retargetQuiesceTimedOut=" + N3(g_ncRetargetQuiesceTimedOut)
                 + " openWasNotGated=" + N3(g_ncOpenWasNotGated) + " openNoPuppet=" + N3(g_ncOpenNoPuppet)
                 + " snapReasons[placed,noPuppet,noChar,noAnim,noMovement,ragdoll,prone,unresolved,notNeeded]=" + N3(g_ncSnapReason[0]) + "," + N3(g_ncSnapReason[1])
                 + "," + N3(g_ncSnapReason[2]) + "," + N3(g_ncSnapReason[3]) + "," + N3(g_ncSnapReason[4]) + "," + N3(g_ncSnapReason[5]) + "," + N3(g_ncSnapReason[6]) + "," + N3(g_ncSnapReason[7]) + "," + N3(g_ncSnapReason[8])
                 + " gateRefused[close,retarget,orphan]=" + N3(g_ncGateRefusedOnClose) + "," + N3(g_ncGateRefusedOnRetarget) + "," + N3(g_ncGateRefusedOnOrphan)
                 + " openRefusedNoOrder=" + N3(g_ncOpenRefusedNoOrder) + " dropNoRow=" + N3(g_ncDropNoRow) + " closeNoCharSwitch=" + N3(g_ncCloseNoCharSwitch)
                 + " f1NonFinite=" + N3(g_f1NonFinite) + " f6NonFinite=" + N3(g_f6NonFinite)
                 + " retargetOrderFailed=" + N3(g_ncRetargetOrderFailed) + " retargetRefusedWrite=" + N3(g_ncRetargetRefusedWrite) + " aiPassOverlapSeen=" + N3((long long)AiPassOverlapSeen())
                 + "  |  identity: opened == closed + openNow + orphanedByRebind + dropNoRow  (closed includes switch/drop/noChar closes)");
        DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "nc_report" + q
                 + "," + q + "nativeCombat" + q + ":" + N3(g_nativeCombat ? 1 : 0)
                 + "," + q + "opened" + q + ":" + N3(g_ncWindowsOpened)
                 + "," + q + "closed" + q + ":" + N3(g_ncWindowsClosed)
                 + "," + q + "openNow" + q + ":" + N3(openNow)
                 + "," + q + "retargeted" + q + ":" + N3(g_ncRetargeted)
                 + "," + q + "orderFailed" + q + ":" + N3(g_ncOrderFailed)
                 + "," + q + "windowSwings" + q + ":" + N3(g_ncWindowSwings)
                 + "," + q + "peerSwingsWhileAuthorityOff" + q + ":" + N3(g_ncSwingsWhileAuthOff)
                 + "," + q + "snapDone" + q + ":" + N3(g_ncSnapDone)
                 + "," + q + "snapDeclined" + q + ":" + N3(g_ncSnapDeclined)
                 + "," + q + "orphanedByRebind" + q + ":" + N3(g_ncOrphanedByRebind)
                 + "," + q + "closedBySwitch" + q + ":" + N3(g_ncClosedBySwitch)
                 + "," + q + "closedByDrop" + q + ":" + N3(g_ncClosedByDrop)
                 + "," + q + "closedByDropNoRegate" + q + ":" + N3(g_ncClosedByDropNoRegate)
                 + "," + q + "closeNoChar" + q + ":" + N3(g_ncCloseNoChar)
                 + "," + q + "closeWaitedForPass" + q + ":" + N3(g_ncCloseWaitedForPass)
                 + "," + q + "closeQuiesceTimedOut" + q + ":" + N3(g_ncCloseQuiesceTimedOut)
                 + "," + q + "retargetQuiesceTimedOut" + q + ":" + N3(g_ncRetargetQuiesceTimedOut)
                 + "," + q + "openWasNotGated" + q + ":" + N3(g_ncOpenWasNotGated)
                 + "," + q + "openNoPuppet" + q + ":" + N3(g_ncOpenNoPuppet)
                 + "," + q + "f1NonFinite" + q + ":" + N3(g_f1NonFinite)
                 + "," + q + "gateRefusedOnClose" + q + ":" + N3(g_ncGateRefusedOnClose)
                 + "," + q + "gateRefusedOnRetarget" + q + ":" + N3(g_ncGateRefusedOnRetarget)
                 + "," + q + "gateRefusedOnOrphan" + q + ":" + N3(g_ncGateRefusedOnOrphan)
                 + "," + q + "openRefusedNoOrder" + q + ":" + N3(g_ncOpenRefusedNoOrder)
                 + "," + q + "dropNoRow" + q + ":" + N3(g_ncDropNoRow)
                 + "," + q + "snapUnresolved" + q + ":" + N3(g_ncSnapReason[7])
                 + "," + q + "closeNoCharSwitch" + q + ":" + N3(g_ncCloseNoCharSwitch)
                 + "," + q + "openNoWatch" + q + ":" + N3(g_ncOpenNoWatch)
                 + "," + q + "switchRefused" + q + ":" + N3(g_ncSwitchRefused)
                 + "," + q + "retargetOrderFailed" + q + ":" + N3(g_ncRetargetOrderFailed)
                 + "," + q + "retargetRefusedWrite" + q + ":" + N3(g_ncRetargetRefusedWrite)
                 + "," + q + "aiPassOverlapSeen" + q + ":" + N3((long long)AiPassOverlapSeen())
                 + "," + q + "puppetThrownRefused" + q + ":" + N3(g_hitsPuppetThrown)
                 + "," + q + "hitsSuppressed" + q + ":" + N3(g_hitsSuppressed)
                 + "," + q + "tick" + q + ":" + N3(g_cmPulseTick) + "}");
    }
    DebugLog("[M6] REPORT combatModeSent=" + N3(g_cmSent)
             + " applied=" + N3(g_cmApplied)
             + " refusedNoReplicatedTarget=" + N3(g_cmNoTarget)
             + " noCombatClass=" + N3(g_cmNoCombat)
             + " refused=" + N3(g_cmRefused)
             + " ordersInjected=" + N3(g_cmOrders)
             + " orderFailed=" + N3(g_cmOrderFailed)
             + " ordersCleared=" + N3(g_cmOrdersCleared)
             + " refusedNotOurs=" + N3(g_cmNotOurs)
             + " combatPulses=" + N3(g_cmPulses)
             + " pulseEnabled=" + N3(g_combatPulse ? 1 : 0)
             + " ordersEnabled=" + N3(g_combatOrders ? 1 : 0));

    // P041 / F348. The arithmetic is closed on purpose so a reader can check it rather than trust
    // it: sent + skipNoTarget + sendFailed = every transition into the attack state on the
    // authority, and recv = wrote + noChar + noCombat + noMode + noTarget + stumble + already
    // + blocked + disabled, with wrote = LANDED + DECLINED + noWatch + rewrite once the verdicts
    // settle (the last two are writes that no verdict will ever score, and they are counted so the
    // identity closes instead of quietly failing to).
    // F354 - the two OUTSTANDING counts, without which neither identity can ever balance. An
    // intent or a write on a puppet that streams out and never returns is scored by nothing at
    // all; counting what is still in flight turns that permanent leak into a visible term.
    long long intentsPending = 0, writesPending = 0;
    for (int i = 0; i < kMaxCombatWatch; ++i)
    {
        if (g_cmWatch[i].uid == 0) continue;
        if (g_cmWatch[i].swingIntent)  ++intentsPending;
        if (g_cmWatch[i].swingPending) ++writesPending;
    }

    DebugLog("[M6] REPORT swingEnabled=" + N3(g_swingRepl ? 1 : 0)
             + " swingSent=" + N3(g_swSent)
             + " sendNoTargetObj=" + N3(g_swSendNoTargetObj)
             + " sendTargetNotReplicated=" + N3(g_swSendNotReplicated)
             + " sendFailed=" + N3(g_swSendFailed)
             + " swingRecv=" + N3(g_swRecv)
             + " skipDisabled=" + N3(g_swDisabled)
             + " skipNotOurs=" + N3(g_swNotOurs)
             + " noChar=" + N3(g_swNoChar)
             + " noCombatClass=" + N3(g_swNoCombat)
             + " droppedNoWatchSlot=" + N3(g_swNoWatch)
             + " intents=" + N3(g_swIntents)
             + " intentSuperseded=" + N3(g_swIntentSuperseded)
             + " intentExpired=" + N3(g_swIntentExpired)
             + " intentAbsorbed=" + N3(g_swIntentAbsorbed)
             + " absorbedByOurs=" + N3(g_swAbsorbedByOurs)
             + " absorbedByItsOwn=" + N3(g_swAbsorbedByItsOwn)
             + " intentNotServiced=" + N3(g_swIntentNotServiced)
             + " isAIFaulted=" + N3(g_swIsAIFaulted)
             + " intentStale=" + N3(g_swIntentStale)
             + " intentsPendingNow=" + N3(intentsPending)
             // F392 - **A TERM, NOT A DIAGNOSTIC.** A rebind zeroes `swingIntent`, and without
             // this the item would leave `intentsPendingNow` with nothing to replace it and the
             // identity below would silently stop closing. Rebinds were impossible before the
             // slot-indexed rewrite, so this leak is created by that change and closed here.
             + " intentDroppedByRebind=" + N3(g_swIntentDroppedRebind) + " intentDroppedByTeardown=" + N3(g_swIntentDroppedTeardown) + " writeDroppedByTeardown=" + N3(g_swUnscoredTeardown)
             // F354 - the gate that was STILL SHUT when an intent expired. These sum to
             // `intentExpired`, and they are the answer to "why did the peer not swing".
             + " gateNotInCombatMode=" + N3(g_swNoMode)
             + " gateNoTarget=" + N3(g_swNoTarget)
             + " gateStumble=" + N3(g_swStumble)
             + " gateAlreadySwinging=" + N3(g_swAlready)
             + " gateBlocking=" + N3(g_swBlocking)
             + " gateNotAI=" + N3(g_swNotAI)
             + " gateTraceBlocked=" + N3(g_swTraceBlocked)
             + " gateNoMovement=" + N3(g_swNoMovement)
             + " gateForcedWP=" + N3(g_swForcedWP)
             + " gateNoAnimation=" + N3(g_swNoAnim)
             + " gateMidAction=" + N3(g_swMidAction)
             + " gateUnknown=" + N3(g_swGateUnknown)
             + " wrote=" + N3(g_swWrote)
             + " wroteFirstTry=" + N3(g_swWroteFirstTry)
             + " wroteAfterWait=" + N3(g_swWroteAfterWait)
             + " LANDED=" + N3(g_swLanded)
             + " DECLINED=" + N3(g_swDeclined)
             + " unscoredRewritten=" + N3(g_swRewrite)
             + " unscoredStale=" + N3(g_swUnscoredStale)
             + " writesPendingNow=" + N3(writesPending)
             // F392 - the `wrote = …` identity's matching term. Same reason as the one above.
             + " writeDroppedByRebind=" + N3(g_swUnscoredRebind));

    // F354 - THE TWO IDENTITIES, WRITTEN OUT SO A READER CHECKS THEM RATHER THAN TRUSTING THEM.
    // The first repair asserted an identity its code did not maintain (the F343 class), because a
    // write on a puppet that streamed out and never returned was scored by nothing. Both leaks are
    // now closed by the two `…PendingNow` terms above.
    DebugLog("[M6] NOTE swing identities to check:"
             "  recv = disabled + notOurs + noChar + noCombatClass + noWatchSlot + intents"
             "  |  intents = wrote + intentExpired + intentSuperseded + intentAbsorbed + intentsPendingNow + intentDroppedByRebind + intentDroppedByTeardown"
             "  |  intentExpired = sum(gate*) + intentNotServiced  (intentStale is a SUBSET of intentExpired, not a term)  |  wrote = wroteFirstTry + wroteAfterWait"
             "  |  wrote = LANDED + DECLINED + unscoredRewritten + unscoredStale + writesPendingNow + writeDroppedByRebind + writeDroppedByTeardown"
             "  |  NOTE noWatchSlot now means NO MIRROR SLOT RESOLVED THE UID - the watch table"
             " cannot fill any more, so its old meaning is gone even though the label is not.");

    // F351 - **LANDED IS NOT PROOF OF CAUSATION, AND SAYING SO HERE IS CHEAPER THAN A SESSION
    // SPENT BELIEVING IT.** The verdict is "did this uid reach SWORD_SWING within 30 frames of our
    // write", and since T067 the peer's combat state machine is no longer pinned - it moves on its
    // own. Any self-initiated arrival inside that window is credited to us. The A/B arm
    // (`swingrepl off`) is what distinguishes them; a large LANDED on its own does not.
    // **A FIRST PASS, PURELY SO THE VERDICT CAN PRINT ABOVE THE ROWS IT IS A VERDICT ON.**
    //
    // Moving the world-totals line to the top fixed one of three inversions and the plan then
    // claimed all three were fixed. The two that mattered most were still wrong: CONTROL ARM NOT
    // EXERCISED and CONTROL ARM UNMEASURABLE - the lines that say the puppet rows are
    // uninterpretable - printed AFTER the puppet rows. A reader scrolling top-down met the zeros
    // first, which is the whole thing this ordering work exists to prevent.
    //
    // Two passes over 512 mostly-empty slots costs nothing and is the honest fix; the alternative
    // was to tell the reader to scroll back up, i.e. to fix the document instead of the artefact.
    long long anySamples = 0, ownedRows = 0, ownedCalls = 0, puppetRows = 0, puppetCalls = 0;
    long long ownedMeas = 0, puppetMeas = 0;
    // P056 - counted over the SAME rows and in the SAME pass, so the readiness summary and the ARMS
    // line can never disagree about which characters they describe.
    long long ownedWepRows = 0, puppetWepRows = 0;
    long long ownedReadyRows = 0, puppetReadyRows = 0, wepUnreadableRows = 0;
    long long ownedImplausibleRows = 0, puppetImplausibleRows = 0;
    long long ownedUnreadableRows = 0, puppetUnreadableRows = 0;
    // **THE DENOMINATOR IS SAMPLED ROWS, NOT ROWS.** `SampleCombatTick` returns before it counts a
    // sample when the character is not in combat mode - and the P056 block sits after that return,
    // so **a row with `ccSamples == 0` is FORCED to `wepDrawn=0 wepUnreadable=0`** without a single
    // read being attempted. Printed against `puppetRows` that is indistinguishable from *"we looked
    // and there was no weapon"*, which is the study's expected answer.
    //
    // **THIS IS NOT HYPOTHETICAL: T098 had 7 of 23 rows at `inCombatFrames = 0`, and they were the
    // puppets the install was attempted on** (F388 §2). A `0 of 12` built from twelve rows of which
    // seven were never sampled would have read as twelve measurements.
    //
    // F361's shape exactly - a proxy reading zero for a reason unrelated to the question, biased
    // toward the expected answer. The line already gated the OTHER false-zero mechanism (a declined
    // read) and not this one; that asymmetry is what made it dangerous.
    long long ownedSampledRows = 0, puppetSampledRows = 0;
    long long dupUidRows = 0;   // rows sharing a uid with another row - see WatchRowsForUid
    for (int i = 0; i < kMaxCombatWatch; ++i)
    {
        const CombatWatch* w = &g_cmWatch[i];
        if (w->uid == 0) continue;
        const UpdSlot* s = (const UpdSlot*)w->ccUpdSlot;
        const long long calls = w->ccUpdCarried
                              + (s != 0 ? w->ccUpdLastSeen - w->ccUpdAtPrime : 0);
        if (w->ccSamples == 0 && w->ccReprimed == 0 && calls == 0) continue;
        anySamples += w->ccSamples;
        // **A DUPLICATE UID IS AS DISQUALIFYING AS TOO MANY REBINDS, AND FOR THE SAME REASON:**
        // two rows for one character each hold part of its frames, so **neither `updCalls` is that
        // character's**. See `WatchRowsForUid`.
        //
        // **WHAT THIS DOES AND DOES NOT FIX. THIS IS THE THIRD RATIONALE WRITTEN FOR THESE THREE
        // LINES AND THE FIRST TWO WERE BOTH WRONG.** Recorded that way on purpose: F-numbered rule
        // in this file - *a false rationale is worse than none, because the next person reasons
        // from it.*
        //
        //   - claimed first: this stops duplicates "inflating every arm denominator". **It does
        //     not.** `measurable` governs `ownedMeas`/`puppetMeas` and the call totals only, so
        //     **`updCalls` is protected and nothing else is.**
        //   - claimed second: the row counts double-count harmlessly because "both halves report
        //     the same weapon state". **Also false, and inverted.** `MirrorAdd` dedups on `obj`,
        //     and `obj` **IS** the `Character*` - so two mirror slots sharing a uid necessarily
        //     hold **two DIFFERENT Character objects**, and every P056 column is read off the
        //     Character.
        //
        // **What is actually true**, and it does not resolve tidily:
        //   * If both Characters read alive, the two rows agree and the ratio goes 1-of-1 → 2-of-2.
        //   * **If one is stale, it reads NO WEAPON** (`PlausibleObject` is a vtable-range
        //     heuristic, not proof of liveness) **or garbage into `wepImplausible`, and the ratio
        //     goes 1-of-1 → 1-of-2.** *"The puppet had no weapon drawn"* is P056's EXPECTED answer,
        //     so **the skew, when it happens, points at the study's own conclusion** - the one
        //     direction F361/F364 say must never be biased silently.
        //   * If only one row was ever sampled (combat mode is set through `FindSpawned`, which
        //     resolves to ONE object), the other is skipped by the guard above and contributes
        //     nothing to any count - **yet still disqualifies its twin's `updCalls`**, and
        //     `dupUidRows` reports 1 while two rows exist.
        //
        // The over-refusal is the safe direction and is kept. **`watchDupUid` is printed so the
        // condition is a number rather than an absence, and the ErrorLog states the skew.**
        const int uidRows = WatchRowsForUid(w->uid);
        if (uidRows > 1) ++dupUidRows;
        const bool measurable = RowMeasurable(w, uidRows);   // ONE predicate, one place
        if (w->ccWepUnreadable > 0) ++wepUnreadableRows;
        const bool sampled = (w->ccSamples > 0);
        if (w->ccMine == 1)
        {
            ++ownedRows;  if (measurable) { ++ownedMeas;  ownedCalls  += calls; }
            if (sampled) ++ownedSampledRows;
            if (w->ccWepFrames      > 0) ++ownedWepRows;
            if (w->ccReadyPartial   > 0) ++ownedReadyRows;
            if (w->ccWepImplausible > 0) ++ownedImplausibleRows;
            // **SPLIT BY ARM LIKE ITS SIBLINGS.** The unsplit total was the one counter the line
            // calls "gates the whole line" and the only one that could not be attributed to an
            // arm - in a run whose entire result IS the cross-arm contrast.
            if (w->ccWepUnreadable  > 0) ++ownedUnreadableRows;
        }
        else if (w->ccMine == 0)
        {
            ++puppetRows; if (measurable) { ++puppetMeas; puppetCalls += calls; }
            if (sampled) ++puppetSampledRows;
            if (w->ccWepFrames      > 0) ++puppetWepRows;
            if (w->ccReadyPartial   > 0) ++puppetReadyRows;
            if (w->ccWepImplausible > 0) ++puppetImplausibleRows;
            if (w->ccWepUnreadable  > 0) ++puppetUnreadableRows;
        }
    }

    // **PRINTED FIRST, BECAUSE IT GATES EVERYTHING UNDER IT.**
    //
    // These were at the END of the block, which put the two numbers that say whether any per-uid
    // zero MEANS anything after the rows they gate - so a reader scrolling top-down met the zeros
    // before the line qualifying them. The plan's reading order and the log's print order have to
    // be the same order, or the plan is asking the reader to work against the artefact.
    // **THIS PROSE HAD TO CHANGE WITH THE PREDICATES, AND ALMOST DID NOT.**
    //
    // It used to say `updCallsAll` "is what says THE HOOK IS LIVE; if it is 0 after a fight has
    // happened, every per-uid zero below means nothing." **P048 retracted exactly that**, and this
    // line is the one the code itself labels READ THIS FIRST - so in T096's configuration it told
    // the reader to discard the rows the branch four lines below calls the result. The wrong half
    // printed first, carrying the code's own instruction to trust it.
    //
    // **A grep for `g_updTotal` in conditionals does not find a sentence.** Four predicate consumers
    // were updated and this fifth, prose one was missed - the "fixed three of four sites" shape,
    // one site wider than the search that was used to look for it. **When a signal is retired,
    // search the STRINGS as well as the branches.**
    DebugLog("[P045] world totals: updCallsAll=" + N3((long long)g_updTotal)
             + " hookPatched=" + N3((long long)g_updPatched)
             + " unregistered=" + N3((long long)g_updUnknown)
             + " noSlot=" + N3((long long)g_updNoSlot)
             + " tickThread=" + N3((long long)g_updThreadId)
             + "  - READ THIS LINE BEFORE THE ROWS BELOW IT. **hookPatched (from [P048]) is what"
               " says THE HOOK IS LIVE. updCallsAll says whether it FIRED, which is a different"
               " question.** So: hookPatched=1 with updCallsAll=0 after a fight means the engine"
               " genuinely never called the function, and the per-uid rows below ARE the result -"
               " go read anyTaskFrames in them. hookPatched=0 or -1 means the probe never armed and"
               " every zero below means nothing. unregistered counts calls for objects we never"
               " registered - normally LARGE (the whole world), and only diagnostic when the"
               " registered rows read zero. noSlot MUST be 0: a non-zero there is a count that is"
               " MISSING rather than ABSENT, and those two read identically.");

    // F391 - **THE INDEXING REWRITE'S OWN THREE NUMBERS, AND ONE OF THEM CHANGES HOW THE PER-UID
    // ROWS BELOW MUST BE READ.**
    //
    // `watchRebound` counts rows re-initialised because their mirror slot was reused under a
    // different uid. **On a run where it is non-zero, the per-uid counters below are WINDOWS, not
    // lifetimes** - a row's numbers start from the rebind, and nothing else on the line says so.
    // The old table never released a slot, so this could not happen and no reader had to know;
    // it can now, which is why it is printed rather than left as an internal detail. Same hazard
    // `updRebinds` covers one level down (F364).
    //
    // `watchNoMirror` is a uid-only lookup that found no mirror slot - ordinary if a character was
    // retired between a message arriving and the walk, and worth watching if it is large.
    // **`watchBadSlot` MUST be 0**: non-zero means the mirror is larger than the watch table and
    // characters are being dropped silently at the top end.
    DebugLog("[P045] watch index: watchRebound=" + N3(g_cmWatchRebound)
             + " watchNoMirror=" + N3(g_cmWatchNoMirror)
             + " watchBadSlot=" + N3(g_cmWatchBadSlot)
             + " watchZeroUid=" + N3(g_cmWatchZeroUid)
             + " watchDupUid=" + N3(dupUidRows)
             + " reboundBlindedP030=" + N3(g_cmWatchReboundBlindedP030)
             // **THE TWO REBIND TERMS ARE NOT RESTATED HERE, AND THAT IS THE POINT.** They were,
             // under `intentsDroppedByRebind` / `writesDroppedByRebind` - one letter different from
             // the `intentDroppedByRebind` / `writeDroppedByRebind` the `[M6] REPORT` line and the
             // identity string use. **Two counters, four spellings, two log blocks** - inside the
             // very change whose purpose was to make those two numbers legible AS IDENTITY TERMS.
             // An identity written in a string only helps if every term in it appears verbatim
             // exactly once among the printed fields.
             + " (the two rebind terms are printed with the swing identities, NOT here;"
               " rebound>0 means the per-uid rows below are WINDOWS, not lifetimes;"
               " badSlot MUST be 0)");
    // **`watchDupUid` IS A NUMBER SO THAT THE CONDITION IS NOT AN ABSENCE.** Two mirror slots can
    // hold one uid (`MirrorAdd` dedups on the OBJECT, F363 §1). Those rows are excluded from the
    // arm totals and print `updCalls=UNMEASURABLE(... uidRows=N ...)`, but nothing else on the
    // report would say the two lines are one character.
    if (dupUidRows > 0)
        ErrorLog("[P045] " + N3(dupUidRows) + " ROW(S) share a uid with another row - two mirror"
                 " slots holding one character (MirrorAdd dedups on the OBJECT, not the uid)."
                 " **Those rows are one character split in two:** each covers only part of its"
                 " frames, so each is excluded from the arm CALL totals and prints"
                 " updCalls=UNMEASURABLE(... uidRows=N ...). Do not add two same-uid rows together"
                 " and do not read either one as that character's figure."
                 " **This counts ROWS, so one duplicated character contributes 2** - and a duplicate"
                 " that was never sampled contributes 0 here while still disqualifying its twin."
                 " **ONLY updCalls IS PROTECTED.** The row counts (rows, sampled, wep, ready,"
                 " implausible) still count such a character twice, and the two rows watch"
                 " DIFFERENT Character objects, so they can DISAGREE: if one is stale it reads no"
                 " weapon, and the P056 ratio goes 1-of-1 to 1-of-2 rather than 2-of-2."
                 " **That skew points at this run's own expected answer**, so treat any P056 ratio"
                 " on a run with watchDupUid>0 as suspect and read the per-uid rows directly.");
    if (g_cmWatchReboundBlindedP030 > 0)
        ErrorLog("[P045] " + N3(g_cmWatchReboundBlindedP030) + " rebind(s) discarded a live"
                 " weWantOn, so the P030 probe cannot fire for those characters until the next"
                 " inbound COMBATMODE edge. **That probe reports through a log line, not a counter,"
                 " so its silence is indistinguishable from having found nothing.**");

    // P056 - **THE HEADLINE, AND IT IS A CROSS-ARM COMPARISON RATHER THAN A NUMBER.**
    //
    // A puppet-only reading of "no weapon drawn" proves nothing on its own: plenty of characters
    // stand around unarmed, on either instance. **The reading that carries information is the
    // CONTRAST** - owned characters in combat carrying a drawn weapon while puppets in combat do
    // not. That is the same discipline the ARMS line above applies to the call count, and it is
    // why both halves are counted in one pass over one row set.
    //
    // **`wepUnreadableRows` IS A FLAG TO GO CHECK THOSE ROWS, NOT A VERDICT ON THE RUN.** If our
    // read declined on a row, that row's zero is a fact about the probe and not about the
    // character, so it is printed separately rather than being allowed to sit inside a `0 of N`.
    // **It used to say "GATES THE WHOLE LINE", which is stricter than the truth** - one bad sample
    // in thousands sets it, and voiding a good run on that basis is its own failure mode. The
    // `ErrorLog` below carries the same qualifier, and the test plan was corrected to match the
    // artefact rather than the other way round.
    //
    // The owned/puppet split is printed beside the total, and **`total` is the SUM, not a third
    // bucket** - it is a free invariant check: `owned + puppet != total` would mean a row survived
    // the row-loop `continue` with no ownership label, which is currently impossible.
    // **EVERY RATIO ON THIS LINE IS OVER SAMPLED ROWS.** An unsampled row is not a measurement of
    // zero; see the ownedSampledRows block above for why that distinction decides the run.
    DebugLog("[P056] readiness: owned rows with a weapon drawn=" + N3(ownedWepRows)
             + " of " + N3(ownedSampledRows) + " SAMPLED"
             + "  |  puppet rows with a weapon drawn=" + N3(puppetWepRows)
             + " of " + N3(puppetSampledRows) + " SAMPLED"
             + "  |  readyPartial owned=" + N3(ownedReadyRows) + " of " + N3(ownedSampledRows)
             + " puppet=" + N3(puppetReadyRows) + " of " + N3(puppetSampledRows)
             + "  |  rows where OUR READ DECLINED owned=" + N3(ownedUnreadableRows)
             + " puppet=" + N3(puppetUnreadableRows)
             + " total(=owned+puppet)=" + N3(wepUnreadableRows)
             + "  |  rows with an IMPLAUSIBLE weapon pointer owned=" + N3(ownedImplausibleRows)
             + " puppet=" + N3(puppetImplausibleRows));
    // The excluded rows are printed as their own number rather than left as a subtraction the
    // reader has to notice. **T098 had seven of them and they were the population under test.**
    if ((ownedRows - ownedSampledRows) > 0 || (puppetRows - puppetSampledRows) > 0)
        ErrorLog("[P056] EXCLUDED FROM EVERY RATIO ABOVE: owned="
                 + N3(ownedRows - ownedSampledRows) + " puppet="
                 + N3(puppetRows - puppetSampledRows) + " row(s) recorded ZERO samples"
                 " (inCombatFrames=0), so the sampler never reached the P056 block for them and"
                 " their wepDrawn/wepUnreadable/readyPartial are FORCED zeros, not measurements."
                 " **Do not read those rows as 'no weapon drawn'.** T098 had seven such rows and"
                 " they were the exact puppets the install was attempted on.");
    if (wepUnreadableRows > 0)
        ErrorLog("[P056] " + N3(wepUnreadableRows) + " row(s) had at least one sample where the"
                 " character or the weapon getter would not read, so their wepDrawn counts are"
                 " incomplete. **Those zeros are about the PROBE, not the character.** Read"
                 " wepUnreadable on each row before drawing anything from its wepDrawn."
                 " (One bad sample in thousands fires this; it is a flag to check the rows, not a"
                 " verdict on the run.)");
    if (ownedImplausibleRows > 0 || puppetImplausibleRows > 0)
        ErrorLog("[P056] a weapon pointer was NON-NULL but did not look like an object on some"
                 " rows. Those samples are counted in wepImplausible and NOT in wepDrawn -"
                 " deliberately, because counting them would have read as 'the weapon is not the"
                 " blocker' and that conclusion is the one nobody goes back to re-check.");
    DebugLog("[P056] READING IT: this measures the weapon clause of the readiness requirement - the"
             " first clause that tests the ACTOR, and the one the installer's refusal is attributed"
             " to. **It is NOT the first thing the engine checks:** two requirements about the"
             " TARGET (swimming, imprisoned) are evaluated before it, and on a non-animal the"
             " medical block runs before the weapon too. It does NOT measure the whole predicate"
             " either - the prone/crouching/crippled tail and the crossbow rejection are not"
             " covered, which is why the per-row field is named readyPartial."
             " **readyPartial=0 means an install would be refused for certain ONLY on a row with"
             " inCombatFrames>0, wepUnreadable=0 and wepImplausible=0. readyPartial is DERIVED from"
             " wepDrawn and inherits all three of its conditions** - a zero-sample row, a declined"
             " read, or a non-null-but-implausible weapon pointer each produce readyPartial=0"
             " without the character being refused at all. **The third case is the dangerous one:"
             " the engine would have SEEN a weapon there and passed.** A non-zero value does NOT"
             " mean the install would be accepted.");

    // **TRIAGED ON THE PRIMARY MEASUREMENT'S OWN DENOMINATOR.**
    //
    // The first version keyed this whole chain on `anySamples`, which counts CONTIGUOUS TIMER
    // frames - the denominator of the SECONDARY measurement, and one the call count explicitly does
    // not need (`SampleCombatTick` maintains it on every sample including a re-prime). So a run in
    // which duplicate mirror rows re-primed forever - the case the row loop below prints a banner
    // for -
    // would have reported "This run says NOTHING", suppressed the CONTROL ARM check AND the
    // owned-vs-puppet ARMS line the report itself calls "the comparison IS the measurement", and
    // discarded a perfectly good `updCalls` reading.
    //
    // **That is the primary result gated behind the blind proxy's denominator - the same inversion
    // the whole rewrite existed to remove, one level up, inside the triage written after it.**
    if (ownedRows == 0 && puppetRows == 0)
    {
        ErrorLog("[P045] NO ROWS - not one watched character was seen in combat mode on this"
                 " instance. This run says NOTHING about whether the state machine is ticked;"
                 " it is not a negative result. Get a fight and repeat."
                 " (updCallsAll=" + N3((long long)g_updTotal) + " is NOT a probe verdict here:"
                 " with no fight there may be nothing for the engine to tick, so a zero is"
                 " expected and says nothing about the hook. It is only a probe verdict AFTER a"
                 " fight has happened.)");
    }
    else if (g_updTotal == 0 && g_updPatched == 1)
    {
        // **THE READING T096 PRODUCED, AND THE BUILD THAT PRODUCED IT CALLED IT A PROBE FAILURE.**
        // A zero total with the prologue verifiably patched is not the probe failing - it is the
        // engine not calling the function, which is the answer this whole workstream is after.
        DebugLog("[P045] updCallsAll=0 AND THE PROLOGUE IS PATCHED ([P048] patched=1) - so the hook"
                 " IS armed and CombatClassAI::update was genuinely never called for ANY object"
                 " on this instance. **THIS IS A RESULT, NOT A PROBE FAILURE.** Read the per-uid"
                 " rows below: anyTaskFrames=0 across them means no character here ever had a"
                 " current action for the machine to be dispatched from.");
    }
    else if (g_updTotal == 0)
    {
        // Reachable only once a watched character HAS been in combat, which is what makes this a
        // verdict on the probe rather than on the absence of a fight. The first version fired on
        // `g_updTotal == 0` alone, so a checkpoint `report` typed before or between fights - which
        // the plan explicitly invites - would have declared the probe broken while the startup log
        // said AddHook SUCCESS. Two log lines in flat contradiction, and the louder one wrong.
        ErrorLog("[P045] THE HOOK NEVER FIRED - characters WERE in combat mode on this instance,"
                 " updCallsAll is 0, AND the prologue read-back did not confirm a patch"
                 " (g_updPatched=" + N3((long long)g_updPatched) + ", 1 would mean armed)."
                 " Check the [P048] line: this is the PROBE failing, not the engine. Every per-uid"
                 " zero in this report means nothing. **If [P048] says patched=1 and you are"
                 " reading this line, the two disagree and the build has a defect - trust P048.**");
    }
    else if (ownedRows > 0 && ownedMeas == 0)
    {
        // Distinct from "no owned rows": the control WAS exercised and its bookkeeping declined to
        // answer. Different cause, different fix, and a shared message would have sent whoever
        // reads it looking for a fight that already happened.
        ErrorLog("[P045] CONTROL ARM UNMEASURABLE - " + N3(ownedRows) + " owned row(s) were in"
                 " combat mode and EVERY ONE exceeded the bind limit, so none contributes a count."
                 " The control was exercised; the bookkeeping could not follow it. Puppet rows are"
                 " still uninterpretable. Look for two mirror rows sharing one uid.");
    }
    // **THE CONTROL IS AN ARM, SO ITS DENOMINATOR HAS TO BE CHECKED SEPARATELY.** An earlier
    // version checked a GLOBAL sample total, which is satisfied by the puppets alone - so an
    // instance that watched four puppets and zero owned characters would print four "NEVER CALLED"
    // banners and no warning that the thing they are measured against was absent. That is T094's
    // own third defect (spending an arm without checking it was exercised), recurring one level up,
    // inside the check written to prevent it.
    else if (ownedRows == 0)
    {
        ErrorLog("[P045] CONTROL ARM NOT EXERCISED - " + N3(puppetRows) + " puppet row(s) and ZERO"
                 " owned characters observed in combat mode on this instance. A puppet reading of"
                 " updCalls=0 is UNINTERPRETABLE without an owned character showing what a running"
                 " combat object looks like on the same instance in the same run. Get a fight that"
                 " involves a character THIS instance owns, and repeat.");
    }
    else
    {
        // F373 - the gap between the live world total and the sum of the per-row snapshots is
        // SNAPSHOT LAG, printed here so nobody re-derives it. Each row reports its count as of that
        // row's last sample; `updCallsAll` is read live when this prints, so every actively-ticking
        // row is a few calls behind. **It GROWS with the number of active rows and stays a tiny
        // fraction of the total; a CONSTANT gap would be the worrying reading, because that is what
        // a real leak looks like.** It is the price of reporting the snapshot rather than the live
        // slot, which is what stops a retired uid accruing a stranger's calls through a reused
        // address - a bounded lag traded for an unbounded contamination.
        DebugLog("[P045] snapshot lag: updCallsAll=" + N3((long long)g_updTotal)
                 + " minus attributed=" + N3(ownedCalls + puppetCalls)
                 + " = " + N3((long long)g_updTotal - (ownedCalls + puppetCalls))
                 + " - EXPECTED and not a leak while `unregistered` is 0: rows report their count as"
                   " of their last sample, this total is read now. It should GROW with active rows"
                   " and stay a tiny fraction. A constant gap would be a leak (F373).");

        DebugLog("[P045] ARMS: owned rows=" + N3(ownedRows) + " (measurable " + N3(ownedMeas)
                 + ") updCalls=" + N3(ownedCalls)
                 + "  |  puppet rows=" + N3(puppetRows) + " (measurable " + N3(puppetMeas)
                 + ") updCalls=" + N3(puppetCalls)
                 // The measurable counts are printed because `ownedRows > 0` is what satisfies the
                 // CONTROL ARM check, and a control arm whose every row is UNMEASURABLE satisfies
                 // it while contributing updCalls=0 - which would read as the control failing when
                 // it is the bookkeeping declining to answer. Two numbers, so the difference is
                 // visible rather than inferred.
                 + "  - the comparison IS the measurement. Owned non-zero with puppet zero names"
                   " the cause; both zero means the probe is not working and says nothing about"
                   " the engine."
                 + (anySamples == 0
                      ? "  NOTE contiguousTimerFrames=0, so the [2nd ...] columns BELOW are empty"
                        " for every row - that degrades the SECONDARY reading only. updCalls needs"
                        " no contiguity and is unaffected."
                      : ""));
    }

    for (int i = 0; i < kMaxCombatWatch; ++i)
    {
        const CombatWatch* w = &g_cmWatch[i];
        if (w->uid == 0) continue;
        // A row that never got a contiguous pair still has a CALL COUNT, and dropping it because
        // its timer samples are empty would silently hide a uid the run cared about. `MirrorAdd`
        // dedups on the object and not the uid, so two rows CAN share a uid and re-prime each other
        // forever - which under the old condition made the uid disappear from the report with no
        // line saying so. Fail visible, not silent.
        const UpdSlot* s = (const UpdSlot*)w->ccUpdSlot;
        // ccUpdLastSeen, NOT s->calls - see SampleCombatTick for why reading the live slot here
        // would let a retired uid accrue a stranger's calls through a reused address.
        const long long calls = w->ccUpdCarried
                              + (s != 0 ? w->ccUpdLastSeen - w->ccUpdAtPrime : 0);
        if (w->ccSamples == 0 && w->ccReprimed == 0 && calls == 0) continue;
        // Tallied in the pre-pass above, not here - see the comment there for why the verdict has
        // to print before these rows. Unmeasurable rows are excluded from the ARMS totals rather
        // than contributing a 0 that would drag a puppet arm toward the study's expected answer.
        // **THE SAME PREDICATE THE PRE-PASS USED — see RowMeasurable.** This was a second local
        // with the same name and a weaker rule, and the divergence let a disqualified row print
        // the run's headline conclusion.
        const int  uidRows    = WatchRowsForUid(w->uid);
        const bool measurable = RowMeasurable(w, uidRows);

        DebugLog("[P045] uid=" + N3(w->uid)
                 + " mine=" + N3(w->ccMine)
                 + " isAI=" + N3(w->ccIsAI)
                 + " inCombatFrames=" + N3(w->ccSamples)
                 // THE PRIMARY NUMBER, and it is first among the tick columns for that reason.
                 // **A ROW THAT CANNOT MEASURE REFUSES TO ANSWER RATHER THAN ANSWERING ZERO.**
                 // Past a couple of rebinds every interval has been discarded (see `ccUpdRebinds`)
                 // and the honest number is unavailable, not 0 - and 0 here is the study's expected
                 // conclusion. A row that says UNMEASURABLE is recoverable; one that says 0 is not.
                 // `uidRows > 1` disqualifies here for the same reason it does in the pre-pass:
                 // two mirror slots holding one uid give two rows that each cover part of the
                 // character, so neither number is that character's.
                 + (!RowMeasurable(w, uidRows)
                      ? " updCalls=UNMEASURABLE(binds=" + N3(w->ccUpdRebinds)
                        + " uidRows=" + N3((long long)uidRows)
                        + ", every rebind discards an interval, and uidRows>1 means two mirror rows"
                          " ARE sharing this uid - this row holds only part of the character)"
                      : " updCalls=" + N3(calls))
                 + " binds=" + N3(w->ccUpdRebinds)
                 + " (slot=" + (s != 0 ? std::string("yes") : std::string("NONE"))
                 + " carried=" + N3(w->ccUpdCarried) + ")"
                 // P050 / P051 - **THE GATE F367 SAYS IS THE REAL ONE.** Denominator is
                 // `inCombatFrames - noBody` for the task counters (they sit inside the
                 // body-plausible branch) and `inCombatFrames` for `goFtEverSet` and the P054 target
                 // counters. `noBody` prints alongside, so both are recoverable.
                 //
                 // **`anyTaskFrames` IS A SUPERSET, NOT A SIBLING** - it counts every frame the
                 // task pointer read at all, INCLUDING both melee variants, so
                 // `focusedTaskFrames + meleeTaskFrames <= anyTaskFrames <= inCombatFrames`. The
                 // previous version of this comment described it as "a task that is not a melee
                 // attack", which is a different quantity
                 // (`anyTaskFrames - focusedTaskFrames - meleeTaskFrames`) and is not printed.
                 // Adding the columns on that reading gives more task frames than there are frames.
                 //
                 // `focusedTaskFrames` is the one F367 stands or falls on - `Task_MeleeAttack` does
                 // NOT dispatch `go` (F368), so `meleeTaskFrames > 0` with `updCalls = 0` is
                 // consistent rather than contradictory. `goFtEverSet` exists to ARGUE AGAINST
                 // F367: a host row with rising updCalls and that flag never set reopens the whole
                 // chain. It is a LATCH - only the never-set direction means anything.
                 + " focusedTaskFrames=" + N3(w->ccFocusedTaskFrames)
                 + " meleeTaskFrames=" + N3(w->ccMeleeTaskFrames)
                 + " anyTaskFrames=" + N3(w->ccAnyTaskFrames)
                 // Stated, not left to be derived at 2am after a 25-minute session. The comment
                 // claiming these two are "distinguishable" was only true by a subtraction that
                 // appeared nowhere a reader would see it.
                 + " noTaskFrames=" + N3(w->ccSamples - w->ccNoBodyFrames - w->ccAnyTaskFrames)
                 + " noBody=" + N3(w->ccNoBodyFrames)
                 // P053 - THE PRECONDITION. **Denominator is `inCombatFrames - noBody`**, not
                 // `inCombatFrames`: these only increment inside the body-plausible branch. `noBody`
                 // prints immediately above, so the subtraction is available.
                 //
                 // **`bodyFtNonZero` IS THE DECISION. `bodyFtMoved` IS NOT.** The field is 0 at
                 // construction and only `CharBody::update` writes it, so non-zero means the
                 // update ran past its platoon gate. "Changed" would seem more natural and is
                 // unsound: the engine clamps its frame delta to 0.05, so below 20 FPS - which two
                 // instances on one machine can easily produce - the value is bit-identical every
                 // frame while the update runs perfectly. `lastBodyFt` is printed so 0.0 (never
                 // ran) and 0.05 (pinned at the clamp) are told apart by eye.
                 // **BOOLEAN FIRST, then the count** - the same shape `goFtEverSet` uses two lines
                 // down, and for the same reason its comment gives: printed bare beside
                 // `inCombatFrames` a count reads as a rate. **A middle value here is ordinary**
                 // (the field is an assignment, not an accumulator, so a puppet sampled before its
                 // first update reads zero for that prefix), which is exactly why the boolean is
                 // the reading and the count is context.
                 + " bodyFtEverNonZero=" + N3(w->ccBodyFtNonZero > 0 ? 1 : 0)
                 + "/" + N3(w->ccBodyFtNonZero)
                 + " bodyFtMoved=" + N3(w->ccBodyFtMoved)
                 // **RAW BITS, not a formatted float.** `F1` rounds to one decimal, which prints
                 // the clamp value 0.05 as "0.1" - and the two readings this field exists to tell
                 // apart are 0.0 and 0.05. It also dodges type-punning a uint through a float* on
                 // a /GL build. `0x00000000` = never written; `0x3d4ccccd` = pinned at the 0.05
                 // clamp; anything else = a live varying delta.
                 + " lastBodyFtBits=" + Ptr((const void*)(uintptr_t)w->ccLastBodyFtSeen)
                 // P054 - denominator is inCombatFrames (off `cc`, not `body`).
                 + " targetChanges=" + N3(w->ccTargetChanges)
                 + " targetCleared=" + N3(w->ccTargetCleared)
                 + " goFtEverSet=" + N3(w->ccGoFtNonZero > 0 ? 1 : 0)
                 + "/" + N3(w->ccGoFtNonZero)
                 // P056 - **THE ACTOR'S READINESS. THE FIRST FIELD IS THE ONE THIS RUN EXISTS FOR.**
                 //
                 // `wepDrawn` counts samples where the character had a weapon in its hands, read
                 // through the subclass-correct offset (see WeaponInHandsOf). The refusal path the
                 // installer takes tests exactly this, first, before anything about the target.
                 //
                 // **THREE THINGS HAVE TO BE TRUE BEFORE `wepDrawn=0` MEANS "NO WEAPON DRAWN",
                 // AND AN EARLIER VERSION OF THIS COMMENT NAMED ONLY ONE OF THEM:**
                 //
                 //   1. `inCombatFrames > 0`. The sampler returns before it counts a sample when
                 //      the character is not in combat mode, and this block sits after that
                 //      return - so a row at `inCombatFrames=0` has **FORCED** zeros here and no
                 //      read was ever attempted. **T098 had seven such rows and they were the
                 //      puppets the install was attempted on.**
                 //   2. `wepUnreadable = 0`. Otherwise our read declined on those samples and the
                 //      row says nothing about weapons at all.
                 //   3. `wepImplausible = 0`. Otherwise the field WAS non-null and did not look
                 //      like an object - which is neither "no weapon" nor "a weapon".
                 //
                 // Only with all three does `wepDrawn=0` mean the character genuinely had no
                 // weapon drawn. **The version of this rule that named only (2) would have
                 // certified the study's expected answer on roughly a third of T098's rows.**
                 + " wepDrawn=" + N3(w->ccWepFrames)
                 + " wepUnreadable=" + N3(w->ccWepUnreadable)
                 + " wepImplausible=" + N3(w->ccWepImplausible)
                 // **LAST-SEEN, AND IT DOES NOT CHARACTERISE THE ROW.** A character that fought
                 // with a sword for 4,000 samples and drew a crossbow for the last ten prints as a
                 // crossbow. It identifies A weapon offline - the crossbow being the one drawn
                 // weapon the engine's check still rejects - and nothing more. It is also **not**
                 // implied by `wepDrawn > 0`: if the vtable read fell below the image base it stays
                 // 0x0 while the count climbs, so `wepDrawn=5000 wepVtRva=0x0` is a real row.
                 + " wepVtRva=" + Ptr((const void*)w->ccWepLastVt)
                 + " uncon=" + N3(w->ccUnconFrames)
                 + " dead=" + N3(w->ccDeadFrames)
                 + " noArms=" + N3(w->ccNoArmsFrames)
                 // **AN ANIMAL IS NOT SCORED ON THE THREE COUNTERS TO THE LEFT**, because the
                 // engine's predicate skips the whole medical block for one. Printed so a reader
                 // can see why `uncon`/`dead`/`noArms` did not gate `readyPartial` on this row.
                 + " animal=" + N3(w->ccAnimalFrames)
                 // **A NECESSARY CONDITION, NOT THE VERDICT, AND THE NAME SAYS SO.** It is a
                 // weapon plus THREE tests over four medical bytes. The engine's predicate also
                 // consults prone state, a crouching/lying test and `isCrippled()` - three virtual
                 // calls this probe does not make - and rejects a crossbow. **The arm clause here
                 // was labelled a guess and is not one any more:** the engine's `hasOneWorkingArm`
                 // (0x6436C0) is `rightArmUsable || leftArmUsable`, exactly the disjunction used here.
                 //
                 // **AND IT INHERITS ALL THREE OF `wepDrawn`'s CONDITIONS**, because it is derived
                 // from it - `readyPartial` only increments when the weapon read SUCCEEDED. So
                 // `readyPartial=0` means *refused for certain* only when `inCombatFrames > 0`,
                 // `wepUnreadable = 0` AND `wepImplausible = 0`. **The third is the dangerous one:
                 // an implausible pointer means the field was non-null, so the engine would have
                 // seen a weapon and passed** while this column reads zero. A non-zero value still
                 // does NOT mean it would have passed.
                 + " readyPartial=" + N3(w->ccReadyPartial)
                 // Secondary. Kept because they cost nothing and because disagreeing with updCalls
                 // is itself informative - NOT because anything concludes from them. See the
                 // kStateTimerOff block for the two independent reasons they can read frozen while
                 // the function is running perfectly.
                 + " [2nd baseTicked=" + N3(w->ccBaseTicks)
                 + " aiTicked=" + N3(w->ccAiTicks)
                 + " aiUnreadable=" + N3(w->ccAiUnreadable)
                 + " reprimed=" + N3(w->ccReprimed) + "]"
                 // **THE BANNER KEYS OFF THE CALL COUNT AND NOTHING ELSE.** The previous version
                 // fired on `aiTicked == 0` without checking `aiUnreadable`, so a run in which OUR
                 // OWN isAI read failed on every sample would have asserted a fact about the engine
                 // that was really a fact about our read - the F358 conflation, in the run's
                 // headline sentence. And `aiTicked == 0` is now known to be produceable by a
                 // perfectly running engine.
                 // **GATED ON THE PROBE WORKING, not merely on the count being zero.** Two things
                 // produce `calls == 0` with the engine calling the function perfectly: the hook
                 // never installed (`g_updTotal == 0`), and this object never got a table slot
                 // (`ccUpdSlot == 0`, counted in `noSlot`). `InstallCombatUpdateProbe`'s own
                 // comment says a silent non-install "reports updCalls=0 for every character -
                 // which is exactly the answer this study is looking for"; printing that sentence
                 // per row anyway, control rows included, is that failure arriving through the
                 // report instead of the install. `noSlot`'s comment says a missing count and an
                 // absent one "read identically" - so the banner must not call one the other.
                 + (calls == 0 && w->ccSamples > 0 && (g_updTotal > 0 || g_updPatched == 1)
                    && s != 0 && measurable
                      ? "   <== THE AI UPDATE WAS NEVER CALLED FOR THIS CHARACTER WHILE IT WAS IN"
                        " COMBAT. The state-3 dispatch is in that function, so no write we make"
                        " could ever have become a swing." : "")
                 + (calls == 0 && w->ccSamples > 0
                    && ((g_updTotal == 0 && g_updPatched != 1) || s == 0 || !measurable)
                      ? "   <== updCalls=0 BUT THE PROBE DID NOT WORK FOR THIS ROW"
                        " (hook not armed, no table slot, or too many rebinds). This says NOTHING"
                        " about the engine." : "")
                 + (w->ccSamples == 0 && w->ccReprimed > 0
                      ? "   <== NEVER SEEN ON TWO CONSECUTIVE FRAMES. The timer columns are"
                        " meaningless here; updCalls is not. (**This used to say duplicate mirror"
                        " rows cause it — that was the PRE-SLOT-INDEXING signature.** Duplicates"
                        " now read ccSamples>0 with reprimed=0 and are named by uidRows= instead,"
                        " so this banner can no longer fire for that cause.)"
                      : "")
                 // **THE BANNER ABOVE INVITED THE ERROR IT WAS MEANT TO PREVENT.** Saying "the
                 // TIMER columns are meaningless; updCalls is not" tells a reader that the
                 // non-timer columns can be trusted - and `wepDrawn` is a non-timer column that is
                 // a FORCED zero on exactly this row shape, because the sampler returns before it
                 // counts a sample and the P056 block sits after that return.
                 + (w->ccSamples == 0
                      ? "   <== ZERO SAMPLES, SO EVERY P056 FIELD ON THIS ROW IS A FORCED ZERO,"
                        " NOT A MEASUREMENT. wepDrawn / wepUnreadable / wepImplausible / uncon /"
                        " dead / noArms / animal / readyPartial were never read for this"
                        " character. **DO NOT read wepDrawn=0 or readyPartial=0 here as facts"
                        " about the character.**"
                      : ""));
    }

    DebugLog("[P045] NOTE baseTicked counts frames where stateTimer (+0x14C) changed - that field"
             " is decremented by CombatClass::update (0x60C3A0). aiTicked counts frames where"
             " targetLastChangedTimer (+0x2F0) changed - that field is incremented at the TOP of"
             " CombatClassAI::update (0x60CC10). 0x60CC10 calls 0x60C3A0, and the dispatch"
             " `if (combatState == 3) startupState()` that an injected swing depends on is in"
             " 0x60CC10 AFTER that call. So: both climbing = the whole chain runs; only baseTicked"
             " = the derived never runs and no injected swing can ever be dispatched; only aiTicked"
             " = the derived ran and took its hasForcedWP early return, which is before the base"
             " call and before every dispatch; neither = nothing ticks this combat object."
             " Denominator is inCombatFrames - outside combat mode nothing is sampled,"
             " deliberately, and a gap in sampling re-primes rather than comparing across it.");
    DebugLog("[P045] sampling isAI faults=" + N3(g_ccIsAIFaulted)
             + " - kept OUT of the [M6] isAIFaulted counter deliberately. This call site runs every"
               " frame for every character in combat mode including owned ones, so one bad object"
               " would climb a shared counter by ~60/second and make the swing report's own"
               " diagnostic unreadable for the whole run.");
    DebugLog("[P045] NOTE the AI field SAWTOOTHS - chooseAttackTarget (0x666450) and"
             " initCombatMode (0x667750) both RESET it to 0, and update can call the former a"
             " few instructions before its own accumulate. That is why this counts CHANGED frames"
             " and not increases: a direction test would have read every reset as `did not run`."
             " It also means a single sample of 0.0 says nothing - a fresh object and one that has"
             " never ticked are identical - so only aiTicked/inCombatFrames is evidence here.");

    // P055 / F384 - THE FIFTH ATTEMPT'S OWN VERDICT.
    // P057 / F394 - THE SIXTH ATTEMPT'S OWN VERDICT.
    //
    // **`nowArmed` IS THE ONE NUMBER THIS ARM EXISTS FOR**, and it is decided by reading the field
    // before AND after: `alreadyArmed` is counted separately precisely because on the HOST every
    // character is already armed, so a build that reported success without the before-read would
    // certify itself on the control population.
    DebugLog("[P057] REPORT weaponTask=" + N3(g_weaponTask ? 1 : 0)
             + " attempts=" + N3(g_wpAttempts)
             + " nowArmed=" + N3(g_wpNowArmed)
             + " stillEmpty=" + N3(g_wpStillEmpty)
             + " factoryDeclined=" + N3(g_wpFactoryDeclined)
             + " reqRefused=" + N3(g_wpReqRefused)
             + " alreadyArmed=" + N3(g_wpAlreadyArmed)
             + " beforeReadDeclined=" + N3(g_wpUnreadable)
             + " afterReadDeclined=" + N3(g_wpAfterUnreadable)
             + " noBody=" + N3(g_wpNoBody)
             + "  |  **THE MEMO EVICTION IS GONE FROM THIS BUILD (F407)** - seven `memo*` columns"
             " used to print here and have been removed rather than left reading zero, because eight"
             " zeros read as *the eviction ran and found nothing*. `taskweapon` now does exactly one"
             " thing: install TASK_DRAW_WEAPON.");
    DebugLog("[P057] READING IT: **nowArmed>0 means a puppet that had NOTHING in its hands now has"
             " a weapon** - the state T099 measured as missing on 118,171 of 118,171 client"
             " samples. **stillEmpty means the task RAN and drew nothing**; factoryDeclined and"
             " reqRefused mean it never ran at all, which is a different question with a different"
             " fix - they used to be one counter. alreadyArmed is NOT success: on the host every"
             " character is armed already, so it is the control arm's expected reading and says"
             " nothing about the fix. afterReadDeclined is the one case where we changed the world"
             " and cannot report what happened."
             "  |  (This line used to explain how to read memoFreedNoInstall, memoFreedAfterInstall"
             " and memoNotQuiesced. **Those counters and the eviction they measured are DELETED**"
             " - F407 - and the explanation went with them. The columns are gone from the line"
             " above, not printing zero.)");
    // P057 / U-13 - the stickiness verdict. Printed beside the install counters rather than with
    // the weapon ones, because it is a fact about the MELEE install, not about the weapon.
    DebugLog("[P057] MEMO across the melee install: evaluated=" + N3(g_tiMemoGrew)
             + " servedFromCache=" + N3(g_tiMemoHit)
             + " neverAsked=" + N3(g_tiMemoNotAsked)
             + " unreadable=" + N3(g_tiMemoUnread)
             + " failedOnReads=" + N3(g_tiFailedOnReads)
             + " lastFailedOnUnverified=" + N3((long long)g_tiLastFailedOnUnverified));
    DebugLog("[P057] READING IT: the requirements check inserts on BOTH exits, so the map growing"
             " means it EVALUATED. **A map that did NOT grow has two causes and only one of them"
             " is U-13**: servedFromCache is a real memo hit; neverAsked is the check never being"
             " reached (the factory declined, or the installer saw the same task already current"
             " and returned without asking). **Only servedFromCache confirms stickiness.**"
             "  |  **THE ARM RESTRICTION THAT USED TO BE HERE IS GONE, AND SO IS ITS REASON.** This"
             " line said the measurement was STRUCTURALLY ZERO with `taskweapon` on, because the"
             " eviction emptied the memo immediately before the melee install. **There is no"
             " eviction (F407), so `servedFromCache` is now measurable in EVERY arm** - and a plan"
             " that still restricts U-13 to one arm is constraining itself on a build that no"
             " longer exists."
             "  |  **lastFailedOnUnverified is named that for two reasons.** It is read only on an"
             " actual refusal, because the SUCCESS exit also grows the memo and writes nothing"
             " there. And it is still racy: four other call sites pass the same global, all under"
             " AI::periodicUpdate, which runs continuously on the worker thread. **Trust it only"
             " alongside refusedInsideInstaller>0.** Expect 0x25 (isReadyForAction) - but 0x8C"
             " (target swimming) and 0x4E (target imprisoned) are evaluated FIRST and are real"
             " possible values, so do not assume the actor was the problem.");

    // F408 - the interlock's own counter. **Printed even at zero**, because a run where the operator
    // never tried the forbidden combination and a run where the build silently allowed it look
    // identical without it.
    DebugLog("[P055/P057] switch interlock: refusedBothOn=" + N3(g_switchInterlockRefused)
             + "  |  `(taskweapon on, taskinstall on)` is refused IN CODE, not by convention."
             " Non-zero means an operator asked for it and the second switch did NOT take effect -"
             " the run continued in whatever single-switch arm was already set. **Check which.**");
    DebugLog("[P055] REPORT taskInstall=" + N3(g_taskInstall ? 1 : 0)
             + " attempts=" + N3(g_tiAttempts)
             + " ok=" + N3(g_tiOk)
             + " okButAlreadyThere=" + N3(g_tiOkAlready)
             + " refused=" + N3(g_tiRefused)
             + " refusedInsideInstaller=" + N3(g_tiRefusedInside)
             + " installedPlainMelee=" + N3(g_tiInstalledPlainMelee)
             + " installedOther=" + N3(g_tiInstalledOther)
             + " taskUnreadable=" + N3(g_tiTaskUnreadable)
             + " noBody=" + N3(g_tiNoBody)
             + " ended=" + N3(g_tiEnded)
             + " endedEmpty=" + N3(g_tiEndedEmpty)
             + " endNoBody=" + N3(g_tiEndNoBody)
             + "  |  identity: attempts = ok + refused + installedPlainMelee +"
               " installedOther + taskUnreadable + noBody. **The previous commit fixed this"
               " identity by REMOVING a dead term, then two live branches were added without"
               " adding them here - the same defect from the other direction, one commit"
               " apart.**"
               "  |  **okButAlreadyThere is a SUBSET of ok**: the field held a focused-melee"
               " task BEFORE our call too, so ok is not evidence our call did anything."
               "  |  **installedPlainMelee is NOT a failure** - a task installed and it is"
               " the variant that does not dispatch go (F368). That is the most"
               " informative outcome this feature can produce, which is why it is not merged"
               " into installedOther."
               "  |  **There is deliberately no `noTarget` term.** An unresolvable target is"
               " refused EARLIER in ApplyRemoteCombatMode and counted as `refused=` (`g_cmRefused`, shared"
               " with unknown-uid and no-CombatClass), which RETURNS before this block - **NOT as"
               " `refusedNoReplicatedTarget`, which is the SENDER-side bound and cannot move for"
               " this reason.** So a `noTarget`"
               " counter here would be dead by construction, printing 0 forever and making the"
               " identity close on a term that can never move. It was written that way and removed"
               " on inspection (F378's shape: a counter declared, printed, never incremented)."
               "  |  **ok / refused / installedOther are decided by READING CharBody+0x68 after"
               " the call, NOT by its return value** - which is `mov al,1` unconditionally and"
               " reports only whether the task FACTORY declined (F385). Every refusal inside the"
               " installer returns true."
               "  |  **refusedInsideInstaller is a SUBSET of refused**, not a term: the call"
               " returned true and installed nothing, which is the requirements check rejecting it"
               " on its way out. That is the likeliest way a puppet is turned down."
               "  |  **ok>0 with the per-uid focusedTaskFrames still 0 means the task WAS installed"
               " and did not survive** - which is exactly what T100 measured (seven installs `ok`,"
               " zero melee task frames on any row). **The next line is where that goes now.**"
               "  |  **CORRECTION, KEPT WHERE THE WRONG ADVICE WAS.** This line used to send the"
               " reader to `hasActionFunc`, quoting F375's claim that `taskData[0x134]` is runtime"
               " data that cannot be read statically. **F399 disassembled it and F375 was wrong:**"
               " it is written in exactly two places, both at LOAD time, and TaskType 5's"
               " registration passes **1** (141 of 253 types do). It cannot reap a genuine melee"
               " task and it is dead as a candidate. **That wrong note survived as the leading"
               " suspect for four attempts**, which is the cost of writing an inference in the"
               " voice of a measurement."
               "  |  **BUT THE CORRECTION HAS A CONDITION, AND THE WORD `genuine` WAS CARRYING IT"
               " SILENTLY.** `hasAction` lives at **`TaskData+0x134`** - a field OF THE VERY RECORD"
               " the [P058] lines below hypothesise is NULL. So *it cannot reap the task* holds for"
               " a `Tasker` whose `taskData` is populated, and says nothing about a hollow one."
               " **Read the two together: `MELEE key5 > 0` is what licenses this paragraph.**"
               "  |  (F399's *141 of 253* is a count of REGISTRATION CALLS, not of task types."
               " `enum TaskType` has 291 members. The same number was briefly used as a range bound"
               " in [P058] and made its alarm fire on correct readings.)"
               "  |  See the [P058] lines below for what to read instead.");

    // P058 / F399 - **IS THE INSTALLED TASK HOLLOW? THE MELEE INSTALL BESIDE ITS OWN CONTROL.**
    DebugLog(std::string("[P058] taskData at install:")
             + TaskDataColumn("MELEE",                    g_p58Melee,       true)
             + TaskDataColumn("EQUIP",                    g_p58Equip,       false)
             + TaskDataColumn("OTHER-NEW(ours,wrong type)", g_p58OtherNew,  false)
             + TaskDataColumn("PRE-EXISTING(not ours)",   g_p58Preexisting, false));
    DebugLog("[P058] **DOOR 1 (F402/F404) - AND THE FIRST VERSION OF THIS LINE HAD IT BACKWARDS,"
             " IN BOLD, AT THE TOP.** `AITaskSytem::update4Frame` has TWO paths to"
             " `bodyTaskComplete` and F399 only had one. The first needs no combat state, no"
             " animation, no requirements check and no memo:"
             " `isDurationBased && timeOfDayHasPassed(AITaskSytem+0x288) && endsAfterTime`."
             "  |  **THE RETRACTED CLAIM:** this said *we never set an expiry, so a task with both"
             " bytes is reaped next frame by construction*. **The premise is false.**"
             " `AITaskSytem::setTaskExpiryTimer` (0x50c6a0) writes that field as"
             " `now + rand*durationFuzz + durationMin`, and it is called from inside"
             " `CharBody::startAction(Tasker*)` at 0x5c672a - on the success path, three"
             " instructions before `MOV AL,1`. **Our install goes through that function, so WE SET"
             " THE EXPIRY.**"
             "  |  **WHICH INVERTS THE READING.** `isDurationBased == 1` means the expiry was just"
             " pushed INTO THE FUTURE, so **door 1 is BLOCKED for the duration window rather than"
             " open on the next frame.** The old line would have closed this investigation on a"
             " mechanism the binary says cannot fire yet."
             "  |  **SO READ THE DURATION FLOATS - BUT THEY ARE IN IN-GAME HOURS (F405), NOT"
             " SECONDS AND NOT FRAMES.** `setTaskExpiryTimer` adds them straight to a value taken"
             " from `GameWorld::getTimeStamp_inGameHours`, so that is what they are by construction."
             " **The game-hours-per-real-second factor has NOT been traced**, so they CANNOT be"
             " converted to frames here, and any sentence comparing them to T100's 1-11 frame"
             " lifetimes is unsupported - an earlier version of this line did exactly that."
             "  |  **WHAT THEY CAN SETTLE, AND IT IS THE WHOLE QUESTION: EXACT ZERO.** With"
             " `durationMinGameHours == 0` and `durationFuzzGameHours == 0` the expiry is set to"
             " *now*, `timeOfDayHasPassed` is true immediately, and **door 1 fires on the next"
             " evaluation** - which needs no conversion factor to interpret. **That is why these"
             " print to SIX decimals:** `F1` renders everything below 0.05 as `0.0`, identical to a"
             " true zero, and would have hidden the one reading that answers this."
             "  |  Non-zero values are a **relative** comparison between columns and task types."
             " Turning one into a frame count needs the conversion factor and is separate work."
             "  |  **AND THEY ARE MEANINGLESS UNLESS `isDurationBased=1`.** `setDurationBased` is the"
             " only writer of those two floats, so a task type that never called it shows whatever"
             " its registration left there - and `0.000000` on such a row is NOT a zero window."
             "  |  **`door1Eligible` IS NAMED THAT DELIBERATELY - IT WAS `DOOR1ARMED`.** Door 1 has"
             " THREE terms and this probe reads the two CONSTANT ones. It never reads the expiry or"
             " the clock, so it cannot say the door is armed, only that this task type is eligible"
             " for it. **A two-thirds predicate named after the whole thing is how a partial"
             " measurement gets quoted as a complete one.**"
             "  |  **AND IT MAY BE TESTING THE WRONG TASKER.** Inside `update4Frame`, when the"
             " current action is NOT duration-based and a sub-tasker with a subTask exists, door 1"
             " evaluates the SUB-TASKER's bytes instead (the same substitution"
             " `getCurrentDurationTimedTask` 0x50c560 makes). This probe reads only the `Tasker` in"
             " `CharBody+0x68`, so **`door1Eligible == 0` does NOT exclude door 1.**"
             "  |  `isDurationBased`/`endsAfterTime` print **-1 for NEVER READ**, and the floats"
             " print **-1.0 for never read** - because 0 and 0.0 are both meaningful values here,"
             " and a zero duration is the single most interesting reading on the line."
             "  |  **The return-address probe cannot separate the two DOORS** (both call"
             " `finishAction` from the same instruction inside `bodyTaskComplete`, so both return"
             " 0x50cda3). It CAN separate the callers - see the caller table below, which was also"
             " wrong and is also corrected.");
    DebugLog("[P058] READING IT: **`MELEE NULLTASKDATA > 0` CONFIRMS F399's CANDIDATE 2** - the task"
             " factory returned a real `Tasker` of the right type carrying a NULL `TaskData`, which"
             " `CharBody::update` reaps on its next pass. That state is invisible to a vtable"
             " check, so every `ok=` T100 printed is compatible with it."
             "  |  **`MELEE key5 > 0` KILLS candidate 2** for those installs: the record is"
             " populated and it is TASK_MELEE_FOCUSED, so the task was whole when we left it and"
             " something else removed it - which leaves F399's candidate 1, the ungated"
             " `AI::frameTick4`."
             "  |  **THE EQUIP COLUMN IS THE CONTROL, AND IT IS AN ACROSS-ARM ONE - NOT A"
             " SAME-FRAME PAIRING.** That install demonstrably works (T100: nowArmed=26) through the"
             " same factory on this instance. **But `taskweapon` fills the equip column and"
             " `taskinstall` fills the melee one, and `(on,on)` is FORBIDDEN - so these two columns"
             " CANNOT both be non-zero in one arm.** Compare them across arms of the same run over"
             " the same character population; do not read it as controlling for the character's"
             " state at the moment of the call, because it does not."
             "  |  **Equip populated + melee null = candidate 2 confirmed.** Both null = the global"
             " task map is unpopulated on this instance generally, which is a bigger and different"
             " finding. Both populated = the hollow-Tasker theory is dead and candidate 1 carries"
             " the whole explanation."
             "  |  **`OTHER-NEW` IS OUR CALL LANDING SOMETHING THAT IS NOT A FOCUSED MELEE TASK** -"
             " the plain-melee variant, another task entirely, or a non-null pointer that would not"
             " read. It is split out because MELEE's reading asserts *a Tasker of the RIGHT TYPE*,"
             " and identity-against-the-pre-call-read cannot see type. **The dangerous case it"
             " catches is F399's candidate 1 wearing candidate 2's clothes:** if the ungated"
             " `AI::frameTick4` swaps our task for the AI's own job between our call returning"
             " and our read, that job's null `taskData` would have scored as `MELEE NULLTASKDATA`"
             " and been read as the melee factory returning a hollow Tasker."
             "  |  **`wildKey` IS AN ALARM AND IT IS ONE-SIDED - READ WHAT IT DOES AND DOES NOT"
             " CLEAR.** Legal keys are **0..290** (`enum TaskType` has 291 members with no explicit"
             " assignments; `FOLLOW_URGENT_ESCAPE` is 253 and `TASK_BREAK_GATE_ORDER` is 290)."
             " **An earlier build bounded this at 252** - F399's count of REGISTRATION CALLS used as"
             " a maximum value - so a puppet holding an ordinary high-numbered task scored as `wild`"
             " and this line told the operator to discard the whole row when the offset was right"
             " and the answer was sitting in it. **A count is not a bound.**"
             "  |  **AND `wildKey == 0` DOES NOT CLEAR THE OFFSET.** Most of 0..290 is small"
             " integers, and small integers are the commonest thing in memory - a wrong"
             " `Tasker+0x70` would land in `keyOther` far more often than here. Non-zero is strong"
             " evidence of a bad offset; zero is weak evidence of a good one. `lastWild` prints the"
             " value so a wrong offset is identifiable rather than merely flagged."
             "  |  **THE THIRD COLUMN IS NOT A SUBSET OF THE FIRST - IT IS EVERYTHING THE FIRST"
             " MUST NOT CONTAIN.** `PRE-EXISTING` holds the rows where the field came back holding"
             " the SAME `Tasker` object it held before our call, so our call demonstrably did not"
             " put it there. Its `key5` is the dangerous number: pooled into MELEE it would read as"
             " *the record is populated, candidate 2 is dead* on a `Tasker` this mod never created."
             " It is split by object identity against the pre-call read, which needs no inference."
             "  |  **AND IT IS NOT EQUAL TO `okButAlreadyThere`, THOUGH AN EARLIER VERSION OF THIS"
             " LINE SAID IT SHOULD BE AND CALLED ANY GAP A MISCOUNT.** This column is routed on"
             " OBJECT IDENTITY ALONE, so it admits a surviving task of ANY type; `okButAlreadyThere`"
             " additionally requires the focused-melee VTABLE. So **the column total is >="
             " `okButAlreadyThere`, and the gap is pre-existing NON-MELEE tasks** - the routine case"
             " for an AI-driven puppet, not a defect."
             "  |  **The comparison that IS informative is `okButAlreadyThere` against this column's"
             " `key5`, and it runs the other way:** `okButAlreadyThere > key5` means a surviving"
             " melee `Tasker` whose `taskData` did not read back as TaskType 5 - i.e. **candidate 2"
             " on a task we did not install.** The old wording told the reader to dismiss exactly"
             " that as a counting bug."
             "  |  **Both columns are zero when their switch is off**, and `taskinstall` and"
             " `taskweapon` are independently SWITCHABLE but not independently USABLE - `(on,on)`"
             " is barred, as three clauses above say. So check the [P055]/[P057] switch states"
             " before reading a zero here as evidence of anything.");

    // P058 / F399 - **WHO ENDED THE TASK. THE RETURN ADDRESSES, AS RECORDED.**
    {
        std::string sites;
        int shown = 0;
        for (int i = 0; i < kMaxEndSites; ++i)
        {
            const LONG64 rva = g_endSites[i].rva;
            if (rva == 0) continue;
            ++shown;
            sites += "  callerRva=" + Ptr((void*)(uintptr_t)rva)
                   + " hits=" + N3((long long)g_endSites[i].hits)
                   + " watchedHits=" + N3((long long)g_endSites[i].watchedHits);
        }
        if (shown == 0) sites = "  (NO SITES RECORDED)";

        DebugLog(std::string("[P058] finishAction callers: endCallsAll=")
                 + N3((long long)g_endCallsAll)
                 + " endCallsOnWatchedBodies=" + N3((long long)g_endCallsWatched)
                 + " siteRowsUsed=" + N3((long long)shown)
                 + " siteTableOverflow=" + N3((long long)g_endSiteOverflow)
                 + " **siteOverflowWATCHED=" + N3((long long)g_endSiteOverflowWatched) + "**"
                 + " endThread=" + N3((long long)g_endThreadId)
                 + " retOutsideImage(CALIBRATION)=" + N3((long long)g_endNoImageBase)
                 + " lastOutsideRet=" + Ptr((void*)(uintptr_t)g_endLastOutsideRet)
                 + " ourOwnEndCalls=" + N3(g_tiEnded + g_tiEndedEmpty)
                 + " baseNotReady=" + N3((long long)g_endBaseNotReady)
                 + " bodiesWatched=" + N3((long long)g_endWatchNext)
                 + " watchRingWrapped=" + N3((long long)g_endWatchWrapped)
                 + " watchRestamped=" + N3((long long)g_endWatchRestamp)
                 + "  ||" + sites);
        DebugLog("[P058] **CHECK THE CALIBRATION FIRST, BEFORE ANY OTHER NUMBER ON THESE LINES.**"
                 " This whole probe assumes `_ReturnAddress()` inside a detour yields the ENGINE's"
                 " call site - true for a jump-patch hook, false for anything that adds a frame."
                 " **It does not have to be assumed.** This mod calls `finishAction` itself from"
                 " exactly ONE place (the combat-exit retraction), and those arrive through the same"
                 " patch with a return address inside our own DLL. So"
                 " **`retOutsideImage(CALIBRATION)` should equal `ourOwnEndCalls`**, and"
                 " `lastOutsideRet` should be an address in SharedWastelands.dll, nowhere near the game"
                 " image. **If retOutsideImage is 0 while ourOwnEndCalls is NON-ZERO, the intrinsic"
                 " is not returning the caller and EVERY callerRva below is meaningless - stop"
                 " there.**"
                 "  |  **BOTH ZERO IS `UNTESTED`, NOT `PASSED`, AND THAT DISTINCTION IS THE WHOLE"
                 " VALUE OF THE CHECK.** Our one call sits on the combat-mode EXIT edge and is"
                 " puppet-side, so it moves only when a replicated character LEAVES combat on this"
                 " instance. A host log - or a client arm in which no fight ended - reads 0 and 0,"
                 " which says nothing whatever about the intrinsic. **Do not record a run as"
                 " calibrated on that basis:** either point at an arm where a fight ended, or state"
                 " that the calibration was never exercised."
                 "  |  (`ourOwnEndCalls` is `ended + endedEmpty` from the [P055] line, restated here"
                 " so the comparison does not need two log lines side by side.)"
                 "  |  **`baseNotReady` is a SEPARATE counter and should be 0** - it counts calls"
                 " that arrived before the image base was cached, which would otherwise have been"
                 " added to the calibration count and made *the probe not being ready* look like"
                 " *our own DLL called it*. Non-zero invalidates the calibration, not the sites.");
        DebugLog("[P058] READING IT: **`watchedHits` IS THE COLUMN THAT ANSWERS THE QUESTION.** It"
                 " counts removals on a CharBody we had just installed a melee task into, and"
                 " `finishAction` has no direct callers - so the caller RVA beside it IS the code"
                 " path that took our task away, read rather than ranked."
                 "  |  **THE RVAs ABOVE PRINT IN LOWERCASE HEX** (this file's `Ptr` does not"
                 " uppercase, unlike ai_spike.cpp's same-named helper), so the candidates below are"
                 " written lowercase to match. Searching a log for an uppercase form finds nothing."
                 "  |  **THE EXACT RETURN ADDRESSES. THE FIRST VERSION OF THIS TABLE CALLED ITSELF"
                 " COMPLETE AND OMITTED THE ONE THAT MATTERS MOST.** Every `call [reg+0x60]` site"
                 " with a CharBody receiver, re-enumerated:"
                 "  ||  **0x50cda3 = `AITaskSytem::bodyTaskComplete`. IT DOES NOT NAME CANDIDATE 1,"
                 " AND AN EARLIER VERSION OF THIS LINE SAID IT DID.** That function has **FIVE**"
                 " producers, all reaching this same instruction and therefore this same return"
                 " address - the probe records the caller of `finishAction`, not the caller of"
                 " `bodyTaskComplete`:"
                 "  |  0x50e878 (door 1) and 0x50e8f8 (door 2), both in the ungated"
                 " `AITaskSytem::update4Frame` - **these two are candidate 1**;"
                 "  |  **0x50cecb in `AITaskSytem::update` (0x50ce90) - A THIRD, UNGATED PATH THAT IS"
                 " NEITHER DOOR.** `_notifyBodyTaskComplete` (0x50bca0) is a two-instruction latch"
                 " (`mov byte [rcx+0x26c],1 ; ret`); `AITaskSytem::update` reads and clears that byte"
                 " and calls `bodyTaskComplete` if it was set. It is reached from"
                 " **`Character::threadedUpdate`** - the LOCOMOTION pass, vtable +0xD8, which"
                 " F403 names as the thing a gate on the AI slot deliberately does NOT touch;"
                 "  |  0x50cf35 (a TAIL JMP, not a call) and 0x50d0ba, both in"
                 " `AITaskSytem::currentActionChecks` - reached via `periodicUpdate`, **which this"
                 " mod DOES gate**, so they should be absent for a suppressed puppet."
                 "  |  **SO THE HONEST READING OF 0x50cda3 IS `bodyTaskComplete ENDED IT`, AND WHICH"
                 " OF ITS FIVE CALL SITES IS NOT DETERMINED BY THIS PROBE.** Recording it as"
                 " *candidate 1 confirmed* would put a Confirmed label on evidence that admits a"
                 " different function on a different vtable slot. **The separating probe is one"
                 " counter on `_notifyBodyTaskComplete` (0x50bca0, a two-instruction leaf) plus a"
                 " read of `AITaskSytem+0x26c` at the install site - see F406.**"
                 "  |  **0x5c6406 = `CharBody::update` - CANDIDATE 2, AND IT WAS MISSING.** That"
                 " function reads `currentAction->taskData` and reaps the task when it is NULL (or"
                 " when `hasActionFunc` is 0), calling vtable +0x60 at 0x5c63fd. **It is neither"
                 " hooked nor gated by this mod.** Decoded by hand from the bytes"
                 " `48 8b 03 48 8b cb ff 50 60` at 0x5c63fd - nine bytes, so the return is 0x5c6406."
                 "  |  **0x5c64c8 = inside `CharBody::startAction(Tasker*)` - ONE OF OUR OWN"
                 " install calls.**"
                 "  |  0x5106d0 = `AITaskSytem::periodicUpdate` - **already gated by this mod**, so"
                 " it should be absent for a suppressed puppet and its presence is itself a finding."
                 "  |  Others, all engine-driven: 0x5c78e0 `reThinkCurrentAIAction` · 0x5ce141"
                 " `_carryMode` · 0x5ce4e2 `getPickedUp` · 0x5d049a `_ragdollMode` · 0x794cd1"
                 " `ActivePlatoon::removeObject` · 0x7a56b6 `declareDead` · 0x7f5a16"
                 " `PlayerInterface::stopCharactersMovement`."
                 "  ||  **THREE CORRECTIONS, TWO OF THEM TO MY OWN PREVIOUS CORRECTIONS.**"
                 " (1) **There is no 0x5c63xx RETURN bucket** - `startAction(TaskType,"
                 " RootObject*)` contains no `call [reg+0x60]` at all and reaches `finishAction`"
                 " only indirectly. F399's guess was right about the FUNCTION and wrong about the"
                 " address."
                 " (2) **`candidates 2 and 3 are NOT separable by return address` WAS FALSE.**"
                 " Candidate 2 has its own site at 0x5c6406 and IS separable - which is the single"
                 " capability this probe was built for, and my correction had removed it."
                 " (3) **NEVER READ A TWO-NIBBLE WINDOW OFF THIS TABLE.** 0x5c6406 falls inside"
                 " `0x5c64xx`, which the old guidance labelled *one of our own install calls* -"
                 " so **candidate 2 confirmed would have been recorded as THE MOD REMOVING ITS OWN"
                 " TASK**, moving the whole workstream back inside our code. **Match exact"
                 " addresses.**"
                 "  |  **`CharBody::endAction()` (0x50bba0) is a TAIL JUMP, not a call.** If it ever"
                 " fires, the captured address is ITS caller's and this table mis-attributes"
                 " silently. It has zero references in the binary today, so the signature to watch"
                 " for is a captured address matching none of the eleven above."
                 "  |  **`endCallsAll = 0` MEANS THE HOOK DID NOT ARM** - check the [P058] prologue"
                 " line at startup before reading a single number here. The engine ends tasks"
                 " constantly for every character in the world; a genuine zero is not possible in a"
                 " running game."
                 "  |  **`siteOverflowWATCHED > 0` IS AN ANSWER THAT WAS MEASURED AND THROWN AWAY,"
                 " AND IT IS THE SECOND WAY THIS RUN CAN PRODUCE A FALSE NEGATIVE.** The 24 site"
                 " slots are claimed first-come by every character in the world, while the site that"
                 " matters fires only on the few frames a melee install happens. If ambient callers"
                 " take all 24 first, our removals land in the overflow and every `watchedHits`"
                 " stays 0 - which reads exactly like *nothing in the engine removed our task*."
                 " **Non-zero here means raise `kMaxEndSites` and re-run; it does NOT mean the"
                 " engine is innocent.** (Cross-check: the `watchedHits` column should sum to"
                 " `endCallsOnWatchedBodies` minus this number.)"
                 "  |  **`bodiesWatched` counts STAMPS, not distinct bodies.** `watchRestamped` is"
                 " the stamps for a body already in the ring, which for a small puppet population is"
                 " most of them - the stamp is deduped, so those consume no slot."
                 "  |  **`watchRingWrapped > 0` means a DISTINCT body was evicted**, and it means"
                 " that only because of the dedupe: without it, a 300-install run against one puppet"
                 " printed ~237 and told the reader history was lost while the ring held that one"
                 " body throughout. When it IS non-zero, `watchedHits` is a FLOOR rather than a"
                 " total."
                 "  |  **`siteRowsUsed` counts ROWS, not distinct call sites** - a race can put one"
                 " RVA in two slots (see the site-claim loop), which costs a duplicate row and never"
                 " a lost call. Add the `hits` of same-RVA rows before comparing anything."
                 "  |  `endThread` is the first thread this detour ran on. **It is recorded because"
                 " the restrictions on this detour were justified by the threading NOT being"
                 " established** - and a probe that assumes a hazard while collecting nothing about"
                 " it leaves the next session to repeat the run."
                 "  |  Bodies are stamped BEFORE the install call, so removals that happen INSIDE"
                 " `startAction` are attributed too - that is deliberate and is why the"
                 " 0x5C6xxx sites are expected to appear at all."
                 "  |  **Entries are compared as raw addresses and never dereferenced**, so a freed"
                 " character cannot fault the recorder - but a new object reusing that address would"
                 " be mis-attributed. Small ring, short run; the alternative was dereferencing"
                 " engine objects on a thread whose identity is not established.");
    }

    // P046 / T095 - THE SETTER'S OWN VERDICT. These four are exhaustive over every call we make to
    // `setTarget`, so `ok + earlyReturn + resolveFailed + wrongTarget` must equal
    // `wrote` - one more identity a reader can check rather than trust.
    DebugLog("[P046] REPORT setTarget: ok=" + N3(g_swSetOk)
             + " okByAlias=" + N3(g_swSetOkAlias)
             + " earlyReturn=" + N3(g_swSetEarlyReturn)
             + " resolveFailed=" + N3(g_swSetResolveFail)
             + " wrongTarget=" + N3(g_swSetWrongTarget)
             + "  ||  wroteNothing=" + N3(g_swSetNoOp)
             + "  |  identity: ok + okByAlias + earlyReturn + resolveFailed + wrongTarget = wrote."
               " **That identity is STRUCTURAL - the chain ends in a bare else, so it cannot fail"
               " and it verifies nothing this run is uncertain about.** It catches a miscount, never"
               " a misclassification, and misclassification is the risk here."
               "  |  wroteNothing is a SEPARATE AXIS crossing all five outcomes: the handle already"
               " named the target and the call did nothing. A large wroteNothing beside a large ok"
               " means the field was already right, NOT that our setter works.");

    DebugLog("[M6] NOTE swing LANDED counts arrivals in the attack state within "
             + N3(kSwingVerdictTicks) + " frames of our write - the bound is now enforced (F354),"
               " but it is still an arrival WITHIN the window, NOT one CAUSED by our write."
               " Since T067 the peer's state machine moves on its own, so compare against a"
               " `swingrepl off` arm before concluding anything from it.");
}

// review-p3o H2 - EVERY ENGINE ADDRESS THIS MODULE CACHES, DROPPED BEFORE THE ENGINE FREES IT.
//
// Called from `detour_worldTeardown` (store.cpp) BEFORE `orig_worldTeardown`. **MAIN THREAD**, and
// two of the three tables below are also touched from the combat worker, so each is cleared with
// the discipline its own writers already use rather than with one blanket rule.
//
//   * `g_cmWatch` - main thread only (`CombatWatchAt`/`CombatWatchFor`/`CombatModeTick` and
//     `NoteNativeWindowDropped`, which reaches it from `DropPuppet` on the main thread). The
//     detour never touches it. So a plain zero of the whole table is legal here, and zeroing is the
//     right shape: every row is keyed to a MIRROR SLOT, `SpawnWorldTeardown` empties the mirror in
//     the same call, and a row left behind would be re-bound against the next world's occupant of
//     that slot - inflating `watchRebound`, whose stated meaning is "the engine reused this slot
//     for a different character", with events that are nothing of the kind. It carries three
//     engine addresses per row: `ccLastObj`, `ccLastTarget` and `ccUpdSlot` (the last points into
//     `g_updSlots`, i.e. our own memory, but it names a combat object that no longer exists).
//     `ResetCombatWatchRow` is deliberately NOT reused: it counts a discarded `weWantOn` as
//     `watchReboundBlindedP030`, a P030 blind-spot counter, and a teardown is not that event.
//
//   * `g_updSlots` - `obj` is written by the main thread and READ by `detour_ccUpdate` on the
//     combat worker (`tickThread`), so the clear publishes with an interlocked store, exactly as
//     the spawn mirror does. A cleared slot simply stops matching, and calls for the freed object
//     land in `updUnknown` instead - the correct answer once the object is gone.
//     **`calls` IS NOT ZEROED.** The note at `UpdSlotFor` is explicit that never zeroing it is what
//     makes the claim race-free without a barrier, and a monotonic counter stays correct across
//     this: the next claimant reads the live value as its baseline and reports calls since its own
//     prime. Zeroing it would reintroduce exactly the race that note rejected.
//
//   * `g_endWatch` - a 64-entry ring of `CharBody*` we installed a melee task into, written from
//     the main thread and scanned by `detour_endAction` on an unknown thread. Its declaration
//     already names the hazard this clear closes: a stale entry "can, in principle, alias a NEW
//     object allocated at the same address and mis-attribute one call". Across a world teardown
//     that stops being a principle - every address in the ring is freed at once. `g_endWatchAny`
//     goes back to 0 last, so the scan is skipped entirely until something is stamped again.
//     (Not in the review's H2 table; same class, found while reading this file.)
void CombatWorldTeardown()
{
    g_knockPendingCount = 0;   // K2 (review-k2 LOW): pending knocks name uids of the world being destroyed
    int watched = 0, windows = 0;
    for (int i = 0; i < kMaxCombatWatch; ++i)
    {
        if (g_cmWatch[i].uid == 0) continue;
        ++watched;
        if (g_cmWatch[i].nativeOn) ++windows;
    }
    // review-p3s M2: the in-flight intents/writes these rows hold are terms of the two swing identities (F354/F343): book them
    for (int i = 0; i < kMaxCombatWatch; ++i) { if (g_cmWatch[i].uid == 0) continue; if (g_cmWatch[i].swingIntent) ++g_swIntentDroppedTeardown; if (g_cmWatch[i].swingPending) ++g_swUnscoredTeardown; }
    memset((void*)g_cmWatch, 0, sizeof(g_cmWatch));
    // review-p3r M2: an unclaimed row's sentinels are -1 (ResetCombatWatchRow sets them at claim; a 0 weWantOn would be booked as
    // watchReboundBlindedP030 on the next claim, which a teardown is not)
    for (int i = 0; i < kMaxCombatWatch; ++i) { g_cmWatch[i].lastOn = -1; g_cmWatch[i].lastState = -1; g_cmWatch[i].lastPeerMode = -1; g_cmWatch[i].weWantOn = -1; g_cmWatch[i].ccMine = -1; g_cmWatch[i].ccIsAI = -1; }

    int slots = 0;
    for (int i = 0; i < kUpdSlots; ++i)
    {
        if (g_updSlots[i].obj == 0) continue;
        ++slots;
        InterlockedExchangePointer((PVOID volatile*)&g_updSlots[i].obj, (PVOID)0);
    }

    int bodies = 0;
    for (int i = 0; i < kMaxEndWatch; ++i)
    {
        if (g_endWatch[i] == 0) continue;
        ++bodies;
        InterlockedExchangePointer((PVOID volatile*)&g_endWatch[i], (PVOID)0);
    }
    InterlockedExchange(&g_endWatchNext, 0);
    InterlockedExchange(&g_endWatchAny, 0);

    DebugLog("[M6] world teardown: forgot " + N3((long long)watched) + " combat watch rows ("
             + N3((long long)windows) + " with the AI released), " + N3((long long)slots)
             + " combat-object update slots and " + N3((long long)bodies)
             + " watched bodies (call counts kept - they are monotonic baselines, never zeroed)");
}

// P1 (read-parity3 GAP 1). No C++ object with a destructor lives in this frame (C2712).
static int ReadAgeFloatPod(const void* c, float* v)
{
    __try { *v = *(const float*)((const char*)c + 0x700); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool ReadAnimalAge01(const void* c, float* out)
{
    if (out) *out = 0.0f;
    if (c == 0) return false;
    unsigned int off = 0;
    WeaponInHandsOf(c, &off);   // PlausibleObject + the getter body; off is written only after +0x708 read cleanly
    if (off != 0x708) return false;   // a human (0x6D8) or unreadable (0)
    float v = 0.0f;
    if (!ReadAgeFloatPod(c, &v)) return false;
    if (out) *out = v;
    return true;
}

// ===========================================================================================
// K2 (decision 61 follow-up) - THE SAFE POINT FOR RAGDOLL REQUESTS FROM OUR CODE.
//
// ragdollMode 0x5CB2D0 only QUEUES a request on Character +0x3E0, a queue with NO lock. The engine
// drains it in exactly one place, GameWorld::threadSafeRagdollUpdates 0x7D17E0 (void(GameWorld*): it
// walks the world's two character sets), called from the main loop at 0x7880B9 while the worker thread
// is paused (0x78800E..0x78816B). Our per-frame code and ApplyRemoteHit run while the worker may be
// running, so calling ragdollMode there is a race. The copy's knock is therefore applied HERE, just
// before the original drains the queue: each pending id is re-found (live), and skipped if the copy is
// gone, is no longer a copy, or is already a ragdoll.
// ===========================================================================================
unsigned long long kThreadSafeRagdollUpdatesRva = 0; static coop::AddrReg kThreadSafeRagdollUpdatesRva_reg("ThreadSafeRagdollUpdates", &kThreadSafeRagdollUpdatesRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7D17E0 */   /* GameWorld::threadSafeRagdollUpdates void(GameWorld*) - hooked */
unsigned long long kRagdollModeRva = 0; static coop::AddrReg kRagdollModeRva_reg("RagdollMode", &kRagdollModeRva);   /* P8h: the address table fills this. Steam_1.0.65 0x5CB2D0 */   /* Character ragdollMode(Character*, bool on, int part) - called, never hooked */

namespace {

typedef void (*TsRagdollUpdatesFn)(void* gameWorld);
typedef void (*RagdollModeFn)(::Character* c, bool on, int part);
TsRagdollUpdatesFn g_origTsRagdollUpdates = 0;
RagdollModeFn      g_ragdollMode = 0;
const long long    kKnockLogLimit = 16;
long long          g_knockLogged = 0;

// 1 = ragdoll, 0 = not, -1 = unreadable. inRagdoll 0x7D08A0 double-dereferences the AnimationClass
// (F357), so the pointer is checked first and the call SEH-wrapped. No C++ object with a destructor
// lives in this frame (C2712).
int KnockIsRagdoll(::Character* c)
{
    void* anim = 0;
    __try { anim = *(void* const*)((const char*)c + 0x448); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!PlausibleObject(anim)) return -1;
    __try { return c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// review-k2 MEDIUM: the copy's own `unconcious` (MedicalSystem +0x161), set the way applyDamage sets it right
// before its ragdollMode call (0x64EAAF, then 0x64EAB6). Without it the copy lies limp while its engine and G1
// read it as conscious - 0x5CF620 can queue 'Getting up' and G1-b's GetupLocalReady passes. Only the byte:
// no knockout timer (C2). The owner's next STATE rewrites it either way (ApplyLatch). 1 = written.
int KnockSetUnconscious(::Character* c)
{
    __try { *((unsigned char*)c + kMedUnconciousOff) = 1; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// MAIN THREAD, worker paused (inside the main loop's 0x7880B9 call).
void KnockDrain()
{
    const bool blocked = EngineWritesBlocked();
    for (int i = 0; i < g_knockPendingCount; ++i)
    {
        const unsigned int uid = g_knockPending[i];
        if (blocked || g_ragdollMode == 0) { ++g_knockSkippedGone; continue; }
        ::Character* c = FindSpawned(uid);
        if (c == 0 || !PlausibleObject(c) || net::IsUidMine(uid)) { ++g_knockSkippedGone; continue; }
        const int rag = KnockIsRagdoll(c);
        if (rag > 0) { ++g_knockSkippedAlready; continue; }
        if (rag < 0) { ++g_knockSkippedGone; continue; }
        // crash1 (T293 / F912): no ragdoll while a body rebuild is queued on the copy (it would run on the limp body) or
        // before its looks are applied; the owner's STATE carries the knockdown next, and M4 applies it once it may.
        if (KnockdownWait(uid, c) != 0) { ++g_knockSkippedRebuild; continue; }
        if (!KnockSetUnconscious(c)) { ++g_knockSkippedGone; continue; }
        g_ragdollMode(c, true, 1);   // the call applyDamage makes at 0x64EAB6 / 0x64EB0A
        ++g_knockApplied;
        if (g_knockLogged < kKnockLogLimit)
        {
            ++g_knockLogged;
            DebugLog("[K2] knock applied #" + N3(g_knockApplied) + " uid=" + N3((long long)uid)
                     + " - unconscious(+0x161)=1 and ragdollMode(copy,1,1) at the safe point 0x7D17E0,"
                       " on the hit that knocked its owner down");
        }
    }
    g_knockPendingCount = 0;
}

void detour_tsRagdollUpdates(void* gameWorld)
{
    if (g_knockPendingCount > 0) KnockDrain();
    coop::CrimeTestDrain();   // crimetest (test-only lever): a BountyManager write, here because the worker is paused
    coop::TalkTestDrain();    /* P26 stage 0 talktest (test-only lever): one Dialogue::sendEvent, with the worker paused */
    coop::BuildTestDrain();   /* build1-a (test-only lever): createBuilding / vt+0x230 / vt+0x248 with the worker paused */
    coop::BuildCopyDrain();   /* build1-b: the other game's PLACE -> a coop-peer copy (createBuilding, the Safe call recipe) */
    coop::CrimeApplyDrain();  // crime3: the other game's crimes written onto its copies - the same BountyManager calls
    coop::PrisonSafePointDrain();   // arrest2: setPrisonMode / isFreeSlot / the sentence, with the worker paused (engine read 1)
    coop::SlaveSafePointDrain();    /* slave1: the owner's slave state set on its copy (setSlaveAIJob touches the AI's job list) */
    coop::CaptureSafePointDrain();  /* P11: MSG_CAPTURE / MSG_CAPTURE_PLACED on our own character, MSG_CAPTURE_DONE's items into our slaver */
    coop::ShotSafePointDrain();     /* P104 fix: MSG_SHOT - the other game's bolt played on our own character through addWound, worker paused */
    /* T-327: coop::EffectSafePointDrain runs AFTER the original below (review fold M2) */
    coop::HireSafePointDrain();     /* recruit1: owner check / take + recruit / setFaction + release, with the worker paused */
    coop::WorldRelSafePointDrain(); /* par24: the notebook's world-vs-world rows written (and the once-a-world seed walk), with the worker paused */
    coop::RestockNowDrain();        /* par2 (test-only lever): the engine's own shop restock for one sector, holder squads only */
    coop::TraderSpawnDrain();       /* traderspawn (test-only lever): one wandering trader caravan through the engine's own createRandomSquad */
    coop::SquadLeaderSafePointDrain();   /* T-1 B1: ActivePlatoon::setSquadLeader on a copy squad, with the worker paused (1 Hz inside) */
    coop::SquadCatsSafePointDrain();   /* T-1 B3 restructure: copy pots written, taken squads adopted, with the worker paused */
    coop::BoxTakeDrain();           /* loot2c (test-only lever): one shown research copy moved from the open box into this player's first character */
    {
        /* While the engine's ragdoll pass runs, no copy's body is rebuilt (appearance.cpp, the copy body-rebuild guard): the
           get-up blend's last update reaches AppearanceBase::update from inside the pass. The depth goes back to its saved value
           when the pass returns; a fault inside the pass ends the process, so there is no other way out. */
        const long savedPassDepth = coop::RagdollPassEnter();
        g_origTsRagdollUpdates(gameWorld);
        coop::RagdollPassLeave(savedPassDepth);
    }
    /* T-327 review fold M2: AFTER the engine's ragdoll pass and still inside the main loop's 0x7880B9 call (the worker is still
       paused). A DONE bite ends in GameWorld::destroy 'eaten' (0x798F50); run before the original, 0x7D17E0 would then walk the
       world's two character sets (+0x720, +0x768) and call 0x5D21A0 on each entry right after that destroy. */
    coop::EffectSafePointDrain();   /* MSG_EFFECT EAT: the other game's eater's bites on our own character; the eatbite lever */
    coop::LimbSafePointDrain();     /* LIMBS: amputate / setRobotLimbItem / limb item destroys, after the ragdoll pass, worker still paused */
}

} // namespace (K2)

void InstallKnockSafePoint()
{
    if (kThreadSafeRagdollUpdatesRva == 0 || kRagdollModeRva == 0)
    {
        ErrorLog("[K2] ThreadSafeRagdollUpdates / RagdollMode not in the address table - knock safe point NOT installed"
                 " (nothing drains: knockPending stops at 32 and every later knock counts knockOverflow)");
        return;
    }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    coop::HookStatus st = coop::AddHook((void*)(base + (uintptr_t)kThreadSafeRagdollUpdatesRva),
                                                  (void*)&detour_tsRagdollUpdates, (void**)&g_origTsRagdollUpdates);
    if (st == coop::SUCCESS)
        g_ragdollMode = (RagdollModeFn)(base + (uintptr_t)kRagdollModeRva);
    DebugLog(std::string("[K2] knock safe point: threadSafeRagdollUpdates 0x7D17E0 AddHook ")
             + (st == coop::SUCCESS ? "SUCCESS" : "FAILED")
             + (st == coop::SUCCESS
                    ? " (a copy's knock calls ragdollMode 0x5CB2D0 there, before the engine drains the queue)"
                    : " (nothing drains: knockPending stops at 32 and every later knock counts knockOverflow)"));
}

// ===========================================================================================
// T-211 / H054 fix 1 - THE PHYSICS GUARD: a box query and a PhysX actor release never overlap.
//
// The crash (T556 game A, dump read): the AI worker's GameWorld::getObjectsWithinBox 0x7857A0 (the combat
// attack-zone scan, calculateTargetsInAttackZone 0x607D10) called [shape vt+8] on a PhysX shape whose actor
// was mid-release - a knocked-down character's old click hull, handed to the physics destroy list by the
// ragdoll switch and released on the PHYSICS thread (threadJunkPreBT 0x4CBB90: the hull deletes -> the
// PhysicsHullT destructor 0x4CEBB0 -> _destroy, and the actor list -> _destroy directly), which runs while
// the next AI pass runs. The engine has no lock between the two (Inferred, H054).
//
// One process-wide SRWLOCK, taken EXCLUSIVE by both hooks, each around the WHOLE call:
//  - getObjectsWithinBox: the PhysX overlap query (scene vt+0x398) and the loop over its results
//    ([shape vt+8], the actor's userData handle -> object lookup 0x7E42C0, object vt+0x20 getDataType).
//    Exclusive, not shared: the query writes a process-wide static result buffer (DAT_142131e00 in
//    1.0.65), so two queries must not overlap either.
//  - PhysicsActual::_destroy 0x7DB0E0: frees the actor's userData, then scene vt+0x40 releases the actor.
//    Every caller found routes through it (PreBT's two destroy loops, the hull destructors 0x4CEBB0 /
//    0x4CAF20, the hull-creation refusal in 0x7E8F10); PreBT's hull creation and terrain work stay outside.
// No deadlock (Read, decompiles of 0x7857A0, 0x7DB0E0, 0x2676A0 and its lookups): neither body calls the
// other, re-enters itself, or waits on another thread; the lookups are hash-map reads and getDataType is
// a type getter. The lock is always the outer one (taken before any PhysX call, never inside one).
// Counters: a waiter is classed by the kind of the CURRENT OR MOST RECENT holder (the holder writes its
// kind right after acquiring and does not clear it), so a wait that starts in the instant between one
// holder's release and the next one's write is classed by the one before - counters, not control.
// ===========================================================================================
unsigned long long kGetObjectsWithinBoxRva = 0; static coop::AddrReg kGetObjectsWithinBoxRva_reg("GameWorld_objectsInBox", &kGetObjectsWithinBoxRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7857A0 */   /* GameWorld::getObjectsWithinBox(lektor<RootObject*>&, pos, size, rot, itemType, int max, RootObject* skip) - hooked (H054) */
unsigned long long kPhysicsActualDestroyRva = 0; static coop::AddrReg kPhysicsActualDestroyRva_reg("PhysicsActual_teardown", &kPhysicsActualDestroyRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7DB0E0 */   /* PhysicsActual::_destroy(NxActor*) - hooked (H054) */

namespace {

typedef void (*BoxQueryFn)(void* gameWorld, void* results, const void* pos, const void* size, const void* rot,
                           int type, int maxNumber, void* skip);
typedef void (*PhysDestroyFn)(void* physicsActual, void* actor);

BoxQueryFn g_origBoxQuery = 0;
PhysDestroyFn g_origPhysDestroy = 0;

SRWLOCK g_physGuard = SRWLOCK_INIT;
enum { kPgQuery = 1, kPgDestroy = 2 };
volatile LONG g_physGuardHolder = 0;   // written only while holding g_physGuard; see the counters note above

volatile LONG64 g_pgQueries = 0;            // box queries run under the guard
volatile LONG64 g_pgDestroys = 0;           // _destroy calls run under the guard
volatile LONG64 g_pgDestroyWaited = 0;      // a destroy had to wait for a query
volatile LONG64 g_pgQueryWaited = 0;        // a query had to wait for a destroy
volatile LONG64 g_pgQueryWaitedQuery = 0;   // a query had to wait for another query
volatile LONG64 g_pgMaxWaitUs = 0;          // the longest wait of any kind, microseconds

void PhysGuardAcquire(LONG kind)
{
    if (!TryAcquireSRWLockExclusive(&g_physGuard))
    {
        const LONG holder = g_physGuardHolder;
        LARGE_INTEGER t0, t1, freq;
        QueryPerformanceCounter(&t0);
        AcquireSRWLockExclusive(&g_physGuard);
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&freq);
        const LONG64 us = (LONG64)((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart);
        if (kind == kPgDestroy && holder == kPgQuery) InterlockedIncrement64(&g_pgDestroyWaited);
        else if (kind == kPgQuery && holder == kPgDestroy) InterlockedIncrement64(&g_pgQueryWaited);
        else if (kind == kPgQuery && holder == kPgQuery) InterlockedIncrement64(&g_pgQueryWaitedQuery);
        LONG64 cur = g_pgMaxWaitUs;
        while (us > cur)
        {
            const LONG64 prev = InterlockedCompareExchange64(&g_pgMaxWaitUs, us, cur);
            if (prev == cur) break;
            cur = prev;
        }
    }
    g_physGuardHolder = kind;
}

// __finally: the lock is released however the original leaves (a C++ exception or an SEH unwind through
// it), so a caught fault inside the engine cannot leave every later query and destroy blocked for good.
void detour_boxQuery(void* gameWorld, void* results, const void* pos, const void* size, const void* rot,
                     int type, int maxNumber, void* skip)
{
    PhysGuardAcquire(kPgQuery);
    InterlockedIncrement64(&g_pgQueries);
    __try { g_origBoxQuery(gameWorld, results, pos, size, rot, type, maxNumber, skip); }
    __finally { ReleaseSRWLockExclusive(&g_physGuard); }
}

void detour_physDestroy(void* physicsActual, void* actor)
{
    PhysGuardAcquire(kPgDestroy);
    InterlockedIncrement64(&g_pgDestroys);
    __try { g_origPhysDestroy(physicsActual, actor); }
    __finally { ReleaseSRWLockExclusive(&g_physGuard); }
}

} // namespace (H054)

void InstallPhysicsGuard()
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    bool boxOk = false;
    bool desOk = false;
    // The destroy hook goes in only after the box hook: either alone guards nothing.
    if (kGetObjectsWithinBoxRva != 0 && kPhysicsActualDestroyRva != 0)
    {
        boxOk = coop::AddHook((void*)(base + (uintptr_t)kGetObjectsWithinBoxRva),
                              (void*)&detour_boxQuery, (void**)&g_origBoxQuery) == coop::SUCCESS;
        if (boxOk)
            desOk = coop::AddHook((void*)(base + (uintptr_t)kPhysicsActualDestroyRva),
                                  (void*)&detour_physDestroy, (void**)&g_origPhysDestroy) == coop::SUCCESS;
    }
    if (boxOk && desOk)
    {
        DebugLog("[H054] physics guard hooks: box / destroy SUCCESS (getObjectsWithinBox 0x7857A0 and"
                 " PhysicsActual::_destroy 0x7DB0E0 share one exclusive lock)");
        return;
    }
    std::string why;
    if (kGetObjectsWithinBoxRva == 0) why += " box (GameWorld_objectsInBox not in the address table)";
    else if (kPhysicsActualDestroyRva != 0 && !boxOk) why += " box (AddHook)";
    if (kPhysicsActualDestroyRva == 0) why += " destroy (PhysicsActual_teardown not in the address table)";
    else if (boxOk) why += " destroy (AddHook - the box hook is in, so queries still serialise with each other)";
    else why += " destroy (not attempted without the box hook)";
    ErrorLog("[H054] physics guard hooks: FAILED" + why + " - the AI-thread query vs physics-thread release race is unguarded");
}

void ReportPhysicsGuard()
{
    DebugLog("[H054] REPORT physGuard[queries,destroys,destroyWaited,queryWaited,queryWaitedQuery,maxWaitUs]="
             + N3(g_pgQueries) + "," + N3(g_pgDestroys) + "," + N3(g_pgDestroyWaited) + ","
             + N3(g_pgQueryWaited) + "," + N3(g_pgQueryWaitedQuery) + "," + N3(g_pgMaxWaitUs));
}

} // namespace coop
