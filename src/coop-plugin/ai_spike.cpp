// ai_spike - PROBE P006: the project's #1 architectural risk (hypothesis H004, was H001).
//
// QUESTION: can we suppress ONE character's AI decision-making while that character still
// EXECUTES its current task locally (moves, animates, fights)?
//
// F058/F059/F060 settled WHERE decisions live (all verified by offline decompile + xrefs):
//   * Character::threadedUpdate  = LOCOMOTION (movement integration + position writeback).
//     Gating it freezes the body, not the mind. Kept here as the "freeze" lever.
//   * AI::periodicUpdate         = THE decision funnel. Its sole callee chain
//     (AITaskSytem::periodicUpdate -> chooseGoal -> runGoals -> runGOAP) is the ONLY
//     route to goal choosing in the whole binary. Gating it per-entity = the character
//     stops deciding but keeps executing. THIS is the co-op suppression seam (H004).
//
// This is the seam the entire co-op design rests on: on the puppet machine, remotely-owned
// characters must stop deciding for themselves but keep walking out the decisions
// replicated from the authority.
//
// THREADING (audit sec.18, and why this file looks the way it does):
//   * threadedUpdate and (per its name) threadedUpdatePeriodic run on Kenshi's AI
//     backthread. Their hooks do the MINIMUM: pointer compares and interlocked counters.
//     NO logging, NO allocation, NO locks - a logging call or mutex here is exactly how
//     you deadlock a game's AI thread. The AI* to match is pre-resolved at capture time
//     on the main thread, so the hot path derefs nothing unverified.
//   * update runs on the main thread. ALL logging happens there, throttled.
//   * Target pointers are single aligned pointer writes => atomic on x64. No lock needed.
//
// Targets were resolved offline and verified in Ghidra before hooking (F038):
//   Character::threadedUpdate -> RVA 0x5C71A0 (locomotion - F058)
//   Character::update         -> RVA 0x5CE6A0
//   AI::periodicTick        -> RVA 0x510820 (decision funnel entry - F059/F060)

#include "ai_spike.h"
#include "spawn.h"      // FindSpawnedUid - P026 asks whether a combat block is ours
#include "worldsync.h"  // P039: PauseSnapshot - the other half of "what stopped"
#include "config.h"    // AUD: ConfigRole / kRoleSingle (ANY THREAD) - the mod arms its own watch when the role is a session role
#include "store.h"     // AUD: EngineWritesBlocked (interlocked reads, any thread)

#include "coop_log.h"
#include "game/Character.h"
#include "game/CharMovement.h"

// `AI` and `CombatClass` are opaque here: game/forward.h (via game/Character.h) declares both names, and
// this file only ever holds and compares their pointers. The hook targets are our own address-table rows
// (AI_periodicTick, AI_frameTick4, CombatClass_periodicTick, below), so no member of either class is
// declared or called. We never touch AI members - the detour compares the pointer identity only.
// P058 / F399 / F400 - **THE SLOT NEXT DOOR, AND THE REASON THIS FILE NOW HOOKS TWO AI METHODS.**
//
// `AI::frameTick4` (0x595B20) and `AI::periodicTick` (0x510820) are **adjacent vtable
// slots** (0x16F92D0 / 0x16F92D8) and **adjacent export slots** (141 / 142) - two independent
// orderings that put them side by side. This mod has gated the second for five attempts. The first
// runs every frame, ungated, and F399 read its callee chain out of the binary:
//
//     0x595B20 AI::frameTick4 -> 0x50E620 AITaskSytem::update4Frame
//         reads AI+0x270 -> CharBody, then CharBody+0x68 -> the current action
//         if empty                    -> 0x50CAB0 AITaskSytem::setCurrentTask  (the AI's OWN job)
//         else if the task is done    -> 0x50CD40 AITaskSytem::bodyTaskComplete
//                                          -> CharBody vtable +0x60 = finishAction
//                                          -> then setCurrentTask, re-installing the AI's own job
//
// **F400: those last two are the engine's own names for them.** The mechanism F399 decoded from
// bytes is called `bodyTaskComplete` and `setCurrentTask` in the shipped symbol table.
//
// **THIS HOOK BUYS A COUNTER, NOT A GATE.** It is measurement only: the premise that this
// function runs for our suppressed puppets at all has been READ from the binary and never observed
// in game, and every previous attempt in this workstream was built on an unobserved premise.
// **"We gated the decision funnel" was never the same claim as "nothing else touches the current
// action" - and the second claim is the one five attempts actually needed.**

// P026 / F214 - IS THE PEER'S COMBAT BLOCK BEING TICKED AT ALL?
//
// T065 asked this with two field reads and could not answer it. `frameTIME` (+0x168) read 0.0
// on BOTH instances at all 197 samples - including on the authority, where combat unambiguously
// ran - so my assumption that `CombatClass::update` writes it every frame was simply wrong: a
// zero that WAS exercised and still carried no signal. And `stateTimer` only advances while
// combat mode is already on, so it cannot distinguish "never ticked" from "ticked but never
// entered combat". Those two need OPPOSITE fixes, so the question has to be settled.
//
// Reading state to infer whether code ran is inference. COUNTING THE CALLS IS MEASUREMENT.
// The hook target is our table row CombatClass_periodicTick (CombatClass::periodicTick). No
// CombatClass member is touched except `me` (+0x188), read by offset to ask whether this combat object
// belongs to a character we replicate.
#include <ogre/OgreVector3.h>
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
#include "addresses.h"
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3CharThreadedUpdate = 0; static coop::AddrReg kMig3CharThreadedUpdate_reg("Character_workerTick", &kMig3CharThreadedUpdate);   /* Steam_1.0.65 0x5C71A0 */
static unsigned long long kMig3CharUpdate = 0; static coop::AddrReg kMig3CharUpdate_reg("Character_update", &kMig3CharUpdate);   /* Steam_1.0.65 0x5CE6A0 */
static unsigned long long kMig3AiPeriodicUpdate = 0; static coop::AddrReg kMig3AiPeriodicUpdate_reg("AI_periodicTick", &kMig3AiPeriodicUpdate);   /* Steam_1.0.65 0x510820 */
static unsigned long long kMig3CombatPeriodicUpdate = 0; static coop::AddrReg kMig3CombatPeriodicUpdate_reg("CombatClass_periodicTick", &kMig3CombatPeriodicUpdate);   /* Steam_1.0.65 0x60C9E0 */
static unsigned long long kMig3AiUpdate4Frame = 0; static coop::AddrReg kMig3AiUpdate4Frame_reg("AI_frameTick4", &kMig3AiUpdate4Frame);   /* Steam_1.0.65 0x595B20 */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cmath>
#include <cstring>   // P058: memcpy/memcmp for the prologue read-back
#include <sstream>
#include <locale>
#include <iomanip>   // P039: setprecision on the frame speed multiplier
#include <float.h>   // F336: _finite - a NaN multiplier must not read as a running engine

namespace coop {

namespace {

void (*orig_threadedUpdate)(Character*) = 0;
void (*orig_update)(Character*)         = 0;
void (*orig_aiPeriodic)(AI*, float)     = 0;

// Written by the main thread, read by the AI thread. Aligned pointer => atomic on x64.
volatile Character* g_target      = 0;
long long g_targetForgotten = 0;   // H030: the watched target was destroyed and the watch cleared
long long g_targetAutoCaptured = 0;   // AUD: the mod armed its own watch (role host/client, no target held)
long long g_targetVerbCaptured = 0;   // AUD: captured because `watchplayer`/`watch` had armed the capture
volatile AI*        g_targetAI    = 0;   // target->getBrain(), resolved at capture (main thread)
volatile LONG       g_armCapture  = 0;   // 1 = capture the next character we see
volatile LONG       g_captureBusy = 0;   /* AUD-b (review-aud M-1): one capture body at a time, whichever path armed it */
volatile LONG       g_wantPlayer  = 0;   // 1 = only capture a player-squad character

// Direct-drive state (audit sec.12: STEER_BY_DIRECTION is "the puppet lever"). Unlike a
// destination, a direction must be re-applied EVERY FRAME - it is a continuous command,
// not a one-shot order, which is exactly why T011/T012's setDestination went inert.
volatile LONG   g_driveFrames = 0;       // frames of direct drive remaining
float           g_driveX      = 0.0f;
float           g_driveZ      = 0.0f;
volatile LONG       g_suppressOn  = 0;   // 1 = skip AI::periodicUpdate for the target (DECISIONS)
volatile LONG       g_freezeOn    = 0;   // 1 = skip threadedUpdate for the target (LOCOMOTION, F058)

volatile LONG64 g_tuTotal      = 0;      // threadedUpdate calls, all characters
volatile LONG64 g_tuTarget     = 0;      // threadedUpdate calls for the target
volatile LONG64 g_upTotal      = 0;      // update calls, all characters
volatile LONG64 g_upTarget     = 0;      // update calls for the target (NOT suppressed)
volatile LONG64 g_tuSkipped    = 0;      // locomotion steps ACTUALLY skipped (return path)
volatile LONG64 g_aiTotal      = 0;      // AI::periodicUpdate calls, all AIs
volatile LONG64 g_aiTarget     = 0;      // AI::periodicUpdate calls for the target's AI
volatile LONG64 g_aiSkipped    = 0;      // decisions ACTUALLY skipped (return path)
volatile LONG64 g_aiPulsed     = 0;      // M2b: single passes deliberately let through
volatile LONG   g_tuThreadId   = 0;
volatile LONG   g_upThreadId   = 0;
volatile LONG   g_aiThreadId   = 0;      // which thread runs AI::periodicUpdate (unknown pre-T014)

long long g_logTick = 0;

std::string Num(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

static bool LooksLikeObject(const void* p);
// Reads the target's position. MAIN THREAD ONLY - never call this from the AI thread.
std::string TargetPos()
{
    Character* t = (Character*)g_target;
    if (t == 0) return "n/a";
    // H030 (T131/F445): getPosition double-dereferences Character+0x448 (F316); a destroyed target read -1 here.
    if (!LooksLikeObject(*(void**)((char*)t + 0x448))) return "unreadable";
    Ogre::Vector3 p = t->worldPosition();
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << p.x << "," << p.y << "," << p.z;
    return ss.str();
}

std::string Ptr(const void* p)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << std::uppercase << (unsigned long long)p;
    return ss.str();
}

// Validates a reconstructed CharMovement* before we call through it. Struct offsets are read, not
// guaranteed correct for every build, so we
// check the pointer looks like a real heap object whose vtable lies inside the game image.
static bool LooksLikeObject(const void* p)
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

// Re-applies the direct-movement vector for one frame. Main thread only.
static void ApplyDirectDrive(Character* self)
{
    if (g_driveFrames <= 0) return;

    CharMovement* mv = self->movement;
    if (!LooksLikeObject(mv)) { InterlockedExchange(&g_driveFrames, 0); return; }

    Ogre::Vector3 dir(g_driveX, 0.0f, g_driveZ);

    // F310, second call site. The argument is a DISTANCE, square-rooted into a per-frame
    // displacement cap - not a speed scalar - and `1.0f` capped this spike at 1.0 world unit per
    // frame just as it capped the replication drive. Vanilla passes the squared distance to the
    // target so the cap means "do not overshoot"; this command has no target to overshoot, it is a
    // raw "walk this way for N frames", so the honest value is NO CAP. sqrtf(1e12) = 1e6 units per
    // frame, which nothing can reach.
    const float kNoDisplacementCap = 1.0e12f;
    mv->steerDirectly(dir, kNoDisplacementCap);

    LONG left = InterlockedDecrement(&g_driveFrames);
    if (left == 0)
        DebugLog("[P006] drive: finished");
}

// SEH cannot coexist with C++ object unwinding in one function, so the guarded call
// lives here on its own.
static bool SafeIsPlayerCharacter(Character* c)
{
    __try { return c->isPlayerControlled(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Resolve the character's AI object once, on the main thread, so the periodicUpdate
// detour needs only a pointer compare. Same SEH-isolation rule as above.
static AI* SafeGetAI(Character* c)
{
    __try { return c->getBrain(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// --- AI backthread. Keep this as close to free as possible. ---
void detour_threadedUpdate(Character* self)
{
    InterlockedIncrement64(&g_tuTotal);
    if (g_tuThreadId == 0)
        InterlockedExchange(&g_tuThreadId, (LONG)::GetCurrentThreadId());

    if (self == g_target)
    {
        InterlockedIncrement64(&g_tuTarget);
        if (g_freezeOn)
        {
            // Counted ON THE RETURN PATH so it measures skips actually taken, not merely
            // entries observed. T010 flagged that tuTarget alone cannot prove the skip.
            InterlockedIncrement64(&g_tuSkipped);
            return;   // LOCOMOTION freeze (F058): body stops, mind keeps deciding.
        }
    }

    orig_threadedUpdate(self);
}

// M2b multi-puppet suppression. A fixed array, not a std::map: this is read on the AI
// worker thread (F062) where an allocation or a lock is exactly how you deadlock the game.
// Writes happen only on the main thread; each slot is a single aligned pointer, so a
// concurrent reader sees either the old or the new value, never a torn one.
// SIZED TO kMaxMirror. T083 is the THIRD time in this project a cap was raised while a table sized
// against it was left behind (F267 raised the mirror 64->512; F277 caught kMaxCombatWatch and
// kMaxWatch still at 64). This one was missed by both passes and it was the binding limit at scale:
// 32 slots filled in 115 MILLISECONDS, then 68 `gate refused` lines, so 68 of 100 replicated
// characters ran the PEER'S OWN AI and walked away. Position parity collapsed from 10/10 within
// 2.0 units at ten characters to 30/100 at a hundred, degrading monotonically to a worst drift of
// 4880 units.
//
// The cost of the raise is a longer pointer scan in FindSlot on the AI worker thread. Measured
// scale: **~1,013/s** of AI entries (T083's raw 162,939 over ~600 s reads ~270/s, but that used
// `aiEntries` - GATE HITS - where `aiTotal` - ALL CALLS - applies; the figure is 3.7x higher, and a
// baseline run gives 1,131/s). So 512 compares per miss is ~520k aligned compares per second -
// still negligible, and far cheaper than an unsuppressed puppet. **The corrected number is written
// here because this is the figure a future reader reaches for when pricing anything on this path**,
// and the stale one understates it by nearly 4x.
// mirror1 (crash T487): 2048, with the mirror. T-354: 4096, with the mirror - a full scan would be 4096 compares per miss,
// ~4.6M aligned compares per second at the same ~1,131 entries/s. T-354 fold 1: FindSlot scans only [0, g_gatedHigh) - the
// highest slot ever claimed this world + 1 - so a miss costs as many compares as slots have been in use, not the table's
// size (Inferred, not measured).
const int kMaxGated = 4096;   // T-354: = kMaxMirror (4096)
struct GateSlot { AI* ai; volatile LONG pulses; volatile LONG armGen; const void* ch; };   // armGen: G1-c fold, see PulseArmGen. ch: mirror1 fold (review-mirror1 #2) - the character the slot gates, an ADDRESS only (never dereferenced), so the slot can go with that character's mirror row

// G1-c fold: the next arm generation - never 0. Main thread (arming and slot creation); interlocked anyway.
volatile LONG g_pulseGenNext = 0;
LONG NextPulseGen()
{
    LONG g = InterlockedIncrement(&g_pulseGenNext);
    if (g == 0) g = InterlockedIncrement(&g_pulseGenNext);
    return g;
}

// P057 / F395 / F396 - **THE COUNT OF AI DECISION PASSES CURRENTLY EXECUTING, ANYWHERE.**
//
// `pulses` is **anti-correlated with the hazard it looks like it guards**: the detour consumes the
// pulse *before* running the pass, so for the whole execution of `AI::periodicUpdate` - the exact
// interval in which it clears and repopulates the requirements memo at `AI+0x238` on the worker
// thread - `pulses` reads **0**. A caller asking *"is a pulse armed?"* is told **no** precisely
// while the dangerous pass runs.
//
// **AND THE OBVIOUS REPAIR - A PER-SLOT `inPass` - IS WRONG TWICE, WHICH IS WHY THIS IS GLOBAL:**
//   * **it goes NEGATIVE with no exception involved.** `SuppressCharacter(c,false)` zeroes the slot
//     while a worker pass is outstanding; the worker's decrement then lands on a zeroed field, and
//     a re-claimed slot inherits `-1` - **a permanent silent refusal for a character that is
//     perfectly quiescent**, indistinguishable from "busy";
//   * **it FAILS UNSAFE across a release/re-suppress cycle.** `inPass` is keyed to the SLOT; the
//     hazard is keyed to the **AI**. Release AI *X* while its pass runs, re-suppress it into a
//     different slot, and the new slot reads 0 while `periodicUpdate` is still executing for *X*.
//
// A global cannot be unbalanced by slot lifecycle because nothing else writes it, and it is
// **over-strict in the right direction**: it refuses while ANY pass runs. Refusing an eviction
// costs a counter; admitting one during a pass costs the heap.
volatile LONG g_aiPassesInFlight = 0;
// P059 (review round 3) - the AI whose periodicUpdate is running RIGHT NOW. The worker is one thread
// (F403/T101: afThread == aiPerThread on both instances), so one pointer suffices; if that ever
// stops being true, `g_aiPassOverlapSeen` climbs and the per-AI wait is known to be unreliable.
volatile PVOID g_aiPassInFlightAi = 0;
volatile LONG  g_aiPassOverlapSeen = 0;
GateSlot g_gated[kMaxGated] = {0};
// T-354 fold 1: the HIGH-WATER MARK - every slot at or above it has never been claimed this world, so FindSlot stops there.
// Written by the main thread only (RaiseGatedHigh at the claim; 0 at AiSpikeWorldTeardown); read once per FindSlot.
// ORDER (why the AI worker may read it while the main thread claims): the claim raises it with an interlocked op BEFORE
// it publishes the slot's `ai` (InterlockedExchangePointer); both are full barriers, so the two stores are seen in that
// order. A worker that reads the OLD mark and so skips slot i read it before the raise, hence before `ai` was published:
// that AI was not gated yet at the moment it looked - the same answer the old full scan gave when it reached slot i before
// the publication (one pass let through, as before). The mark is never lowered during a world, so a slot released and
// re-claimed below it is always scanned; teardown zeroes it only AFTER every slot is cleared.
volatile LONG g_gatedHigh = 0;

// T-354 fold 1: raise the high-water mark to at least `want` (a max, never lowers). Main thread.
void RaiseGatedHigh(LONG want)
{
    for (;;)
    {
        const LONG cur = g_gatedHigh;
        if (cur >= want) return;
        if (InterlockedCompareExchange(&g_gatedHigh, want, cur) == cur) return;
    }
}

// Returns the slot for this AI, or 0. AI-thread safe: pointer compares only. T-354 fold 1: bounded by the high-water mark,
// read ONCE into a local (see g_gatedHigh for why that is safe while the main thread claims).
GateSlot* FindSlot(AI* ai)
{
    LONG hi = g_gatedHigh;
    if (hi > kMaxGated) hi = kMaxGated;
    for (int i = 0; i < hi; ++i)
        if (g_gated[i].ai == ai) return &g_gated[i];
    return 0;
}

// --- AI::periodicUpdate detour. Runs on the AI worker thread (F062), so it does pointer
// compares and interlocked counters ONLY - no logging, no allocation, no locks.
// P026 counters. `ours` is the one that answers the question: a peer whose replicated
// characters' combat blocks are never ticked will read ourTicks=0 while allTicks climbs.
volatile LONG64 g_ccAllTicks = 0;    // CombatClass::periodicUpdate, every character in the world
volatile LONG64 g_ccOurTicks = 0;    // ... only for characters we replicate
volatile LONG64 g_ccNoMe     = 0;    // combat objects whose `me` (+0x188) did not look valid

void (*orig_ccPeriodic)(CombatClass*, float) = 0;

// Runs on whatever thread the engine ticks combat on - UNKNOWN until this run measures it. So:
// no allocation, no locks, no logging. Interlocked counters and a flat-array scan only, exactly
// like the hit detour (which established that the flat mirror is the safe way to ask "is this
// one of ours" off the main thread).
void detour_ccPeriodic(CombatClass* self, float frametime)
{
    InterlockedIncrement64(&g_ccAllTicks);

    if (LooksLikeObject(self))
    {
        // CombatClass::me, +0x188 - the character this combat block belongs to.
        ::Character* me = *(::Character**)((char*)self + 0x188);
        if (LooksLikeObject(me))
        {
            if (FindSpawnedUid(me) != 0) InterlockedIncrement64(&g_ccOurTicks);
        }
        else InterlockedIncrement64(&g_ccNoMe);
    }

    orig_ccPeriodic(self, frametime);
}

// P058 / F399 - **DOES THE UNGATED SLOT ACTUALLY RUN FOR OUR PUPPETS? COUNT IT.**
//
// Read from the binary, `AI::frameTick4` is called every frame for every AI and is not gated
// by anything this mod does. **That has never been observed.** `afTarget` is the number that matters:
// it counts calls for an AI this mod has a gate slot for - i.e. a suppressed puppet - and if it
// climbs while `aiSKIPPED` also climbs, then **the same character is having its decisions suppressed
// and its task field serviced every frame by two different functions**, which is F399's whole claim
// stated as a measurement rather than a decompile.
//
// **No gate here, deliberately.** This build only counts. Whether skipping this call for a puppet is
// safe depends on everything else the function does, and that is being established separately -
// shipping the lever in the same commit as the first evidence it is needed would repeat the pattern
// that produced five attempts against an unmeasured premise.
//
// Same thread rules as the detour below: interlocked counters and a pointer scan, nothing else.
volatile LONG64 g_afTotal    = 0;   // every AI in the world
volatile LONG64 g_afTarget   = 0;   // ... only those with a gate slot (suppressed puppets)
volatile LONG   g_afThreadId = 0;   // **which thread runs it - not established before this build**
void (*orig_aiUpdate4Frame)(AI*, float) = 0;

void detour_aiUpdate4Frame(AI* self, float frametime)
{
    InterlockedIncrement64(&g_afTotal);
    if (g_afThreadId == 0)
        InterlockedExchange(&g_afThreadId, (LONG)::GetCurrentThreadId());

    if (self != 0 && FindSlot(self) != 0) InterlockedIncrement64(&g_afTarget);

    orig_aiUpdate4Frame(self, frametime);
}

void detour_aiPeriodic(AI* self, float frametime)
{
    InterlockedIncrement64(&g_aiTotal);
    if (g_aiThreadId == 0)
        InterlockedExchange(&g_aiThreadId, (LONG)::GetCurrentThreadId());

    // M2b per-puppet path FIRST. Order matters: if a character were both gate-slotted and
    // the legacy single target, checking legacy first would return early and silently
    // swallow its pulses. The specific mechanism wins over the general one.
    if (self != 0)
    {
        GateSlot* slot = FindSlot(self);
        if (slot != 0)
        {
            InterlockedIncrement64(&g_aiTarget);
            // A pending pulse spends itself here and lets exactly one call through, so
            // the puppet can consume the needGOAP an injected order raised (F081/F082).
            if (InterlockedCompareExchange(&slot->pulses, 0, 1) == 1)
            {
                InterlockedIncrement64(&g_aiPulsed);
                // **BRACKET THE PASS.** The pulse is already spent by the line above, so from
                // here until the pass returns nothing else records that this AI is being mutated on
                // the worker thread.
                //
                // **`__finally`, NOT RAII.** This builds `/EHsc`, under which the compiler emits no
                // unwind funclets for SEH - **destructors do not run when an access violation
                // unwinds a frame**, so an RAII guard here would look correct and protect nothing
                // against the failure that matters. `__try`/`__finally` covers a C++ throw *and* a
                // fault, and this function holds no object needing unwinding, so no C2712.
                if (InterlockedIncrement(&g_aiPassesInFlight) > 1) InterlockedIncrement(&g_aiPassOverlapSeen);
                InterlockedExchangePointer(&g_aiPassInFlightAi, (PVOID)self);
                __try     { orig_aiPeriodic(self, frametime); }
                __finally { InterlockedExchangePointer(&g_aiPassInFlightAi, 0); InterlockedDecrement(&g_aiPassesInFlight); }
                return;
            }
            InterlockedIncrement64(&g_aiSkipped);
            return;
        }
    }

    // Legacy single-target path (the T014 spike commands still use it).
    if (self == g_targetAI && self != 0)
    {
        InterlockedIncrement64(&g_aiTarget);
        if (g_suppressOn)
        {
            InterlockedIncrement64(&g_aiSkipped);
            return;
        }
    }

    // **THE UNGATED BRACKET IS LOAD-BEARING, NOT BELT-AND-BRACES. DO NOT DROP IT ON COST
    // GROUNDS** - the arithmetic says it is free (two more locked RMWs on a path that already pays
    // one unconditionally, ~0.007% of one core), but that is not the reason it is here.
    //
    // **Without it, the release/re-suppress hole reopens THROUGH this path:**
    //   1. main releases AI X       -> X is momentarily UNGATED
    //   2. worker enters for X, `FindSlot` returns 0, and runs the pass HERE - X's memo at
    //      `AI+0x238` is being cleared and repopulated right now
    //   3. main re-suppresses X (adoption does this) -> a fresh slot, `pulses = 0`
    //   4. main asks "is X quiesced?" -> slot found, pulses 0, and with only the SLOT path
    //      bracketed the count reads 0 -> **TRUE, while the pass is still executing.**
    //
    // A "gated passes only" count inherits the exact failure of the per-slot field it replaced:
    // keyed to gate membership while the hazard is keyed to the **AI**, failing unsafe and silently
    // across a release/re-suppress cycle. The count must mean *any pass, anywhere*.
    //
    // **AND THE INCREMENT MUST STAY OUTSIDE THE `__try`.** Moved inside, any unwind that reaches
    // `__finally` without it having run decrements below zero - and the guard then refuses forever,
    // silently, which is the stuck condition this design exists to eliminate.
    if (InterlockedIncrement(&g_aiPassesInFlight) > 1) InterlockedIncrement(&g_aiPassOverlapSeen);
    InterlockedExchangePointer(&g_aiPassInFlightAi, (PVOID)self);
    __try     { orig_aiPeriodic(self, frametime); }
    __finally { InterlockedExchangePointer(&g_aiPassInFlightAi, 0); InterlockedDecrement(&g_aiPassesInFlight); }
}

// --- Main thread. Logging lives here. ---
void detour_update(Character* self)
{
    InterlockedIncrement64(&g_upTotal);
    if (g_upThreadId == 0)
        InterlockedExchange(&g_upThreadId, (LONG)::GetCurrentThreadId());

    // Capture happens HERE (main thread), not on the AI thread: T011 showed an arbitrary
    // AI-thread character can be immobile, and isPlayerControlled() is only safe to call
    // from the main thread. Selecting a player-squad member also matches the real co-op
    // case - it is the player's own characters that become puppets on the remote machine.
    // AUD 2026-09-17: THE MOD ARMS ITS OWN WATCH. No non-verb path ever set g_armCapture, so without
    // `watchplayer` the load centre stayed empty and nothing was ever shared. Whenever no target is held
    // and the configured role is a session role (host or client), the next player-squad character seen
    // becomes the load centre. The auto path CAPTURES ONLY: it never touches g_suppressOn, g_freezeOn or
    // the measurement counters ArmCommon resets. The gate is re-evaluated on every character update, so a
    // target lost to ForgetTarget or to a world teardown is picked up again by itself.
    if (g_target == 0)
    {
        const int armed = (g_armCapture != 0) ? 1 : 0;
        int autoWatch = 0;
        if (!armed && coop::ConfigRole() != coop::kRoleSingle && !coop::EngineWritesBlocked())
            autoWatch = 1;
        if (armed || autoWatch)
        {
        bool acceptable = autoWatch ? SafeIsPlayerCharacter(self)
                                    : ((g_wantPlayer == 0) || SafeIsPlayerCharacter(self));
        if (acceptable && (autoWatch ? (InterlockedCompareExchange(&g_captureBusy, 1, 0) == 0)
                                     : (InterlockedCompareExchange(&g_armCapture, 0, 1) == 1)))
        {
            if (autoWatch && g_target != 0) { InterlockedExchange(&g_captureBusy, 0); } else {
            AI* ai = SafeGetAI(self);
            if (LooksLikeObject(ai))
            {
                InterlockedExchangePointer((PVOID volatile*)&g_targetAI, ai);
            }
            else
            {
                InterlockedExchangePointer((PVOID volatile*)&g_targetAI, (PVOID)0);
                ErrorLog("[P006] getBrain() failed validation (" + Ptr(ai)
                         + ") - decision gate will be INERT for this target");
            }
            InterlockedExchangePointer((PVOID volatile*)&g_target, self);
            if (autoWatch) ++g_targetAutoCaptured; else ++g_targetVerbCaptured;
            DebugLog(std::string("[P006] captured target ") + Ptr(self)
                     + " ai=" + Ptr((const void*)g_targetAI)
                     + (autoWatch ? " (player-squad)"
                                  : (g_wantPlayer ? " (player-squad)" : " (any character)"))
                     + (autoWatch ? " (auto: role host/client, no player load centre was held)"
                                  : " (by verb)"));
            if (autoWatch) InterlockedExchange(&g_captureBusy, 0); }
        }
        }
    }

    if (self == g_target)
    {
        InterlockedIncrement64(&g_upTarget);
        ApplyDirectDrive(self);
    }

    orig_update(self);

    // Throttled status line. Character::update runs once per character per frame, so this
    // fires roughly every few seconds depending on population.
    if (++g_logTick % 20000 == 0)
    {
        std::stringstream ss;
        ss << "[P006] target=" << Ptr((const void*)g_target)
           << " ai=" << Ptr((const void*)g_targetAI)
           << " aiTotal=" << Num(g_aiTotal)
           << " aiEntries=" << Num(g_aiTarget)
           << " aiSKIPPED=" << Num(g_aiSkipped)
           << " tuEntries=" << Num(g_tuTarget)
           << " tuSKIPPED=" << Num(g_tuSkipped)
           << " upTarget(RUNNING)=" << Num(g_upTarget)
           << " pos=" << TargetPos();
        DebugLog(ss.str());
    }
}

} // namespace

static void ArmCommon(int suppress)
{
    InterlockedExchangePointer((PVOID volatile*)&g_target, 0);
    InterlockedExchangePointer((PVOID volatile*)&g_targetAI, 0);
    InterlockedExchange64(&g_tuTarget, 0);
    InterlockedExchange64(&g_upTarget, 0);
    InterlockedExchange64(&g_tuSkipped, 0);
    InterlockedExchange64(&g_aiTarget, 0);
    InterlockedExchange64(&g_aiSkipped, 0);
    InterlockedExchange(&g_suppressOn, suppress);
    InterlockedExchange(&g_freezeOn, 0);
    InterlockedExchange(&g_armCapture, 1);
    DebugLog(suppress
        ? "[P006] armed SUPPRESS: next captured character stops DECIDING (AI::periodicUpdate gated)"
        : "[P006] armed WATCH: next captured character left intact (baseline)");
}

void ArmSuppression() { InterlockedExchange(&g_wantPlayer, 0); ArmCommon(1); }
void ArmWatch()       { InterlockedExchange(&g_wantPlayer, 0); ArmCommon(0); }
void ArmWatchPlayer() { InterlockedExchange(&g_wantPlayer, 1); ArmCommon(0); }

// Flip an already-captured target between watch and suppress WITHOUT re-capturing,
// so the same character can be measured both ways - closing T009 caveat 2.
void SetSuppress(bool on)
{
    InterlockedExchange(&g_suppressOn, on ? 1 : 0);
    DebugLog(on ? "[P006] DECISION suppression ON for existing target (no new goals)"
                : "[P006] DECISION suppression OFF for existing target (still watching)");
}

void SetFreeze(bool on)
{
    InterlockedExchange(&g_freezeOn, on ? 1 : 0);
    DebugLog(on ? "[P006] LOCOMOTION freeze ON for existing target (F058 lever: body stops)"
                : "[P006] LOCOMOTION freeze OFF for existing target");
}

// F330 - WHEN THE WORLD FREEZES, SAY WHAT STOPPED.
//
// T091's host froze at ~593 s and never resumed. It voided the run's entire parity arm, and the
// eight `WORLD STILL FROZEN` lines that followed said nothing beyond "still frozen" - so the run
// ended with a 440-second failure and no evidence about it at all.
//
// **The question that splits the possibilities is whether the engine's worker threads are still
// alive.** A thread that has EXITED is a different failure from one that is blocked, and they need
// completely different investigations. We already record the thread ids our detours ran on, so this
// costs an `OpenThread` and a `GetExitCodeThread` per thread, once, on the freeze edge.
//
// Safe: `OpenThread` with `THREAD_QUERY_LIMITED_INFORMATION` touches no engine state, and the
// handle is closed immediately. Main thread, once per freeze - never on a hot path.
std::string ThreadState(const char* name, LONG tid)
{
    if (tid == 0) return std::string(" ") + name + "=never-ran";
    HANDLE h = ::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)tid);
    if (h == 0)
    {
        // A thread that has exited AND been reaped cannot be opened at all. That is itself the
        // answer, so it is reported as one rather than as a failure to measure.
        return std::string(" ") + name + "=GONE(tid " + Num((long long)tid)
             + ", OpenThread failed err=" + Num((long long)::GetLastError()) + ")";
    }
    DWORD code = 0;
    BOOL ok = ::GetExitCodeThread(h, &code);
    ::CloseHandle(h);
    if (!ok) return std::string(" ") + name + "=UNREADABLE(tid " + Num((long long)tid) + ")";
    return std::string(" ") + name + (code == STILL_ACTIVE ? "=ALIVE" : "=EXITED")
         + "(tid " + Num((long long)tid) + ")";
}

// P039 / F331 - the pause half of the same question, formatted for the freeze line.
//
// A thread report alone cannot distinguish "the engine was stopped on purpose" from "a worker
// died", and those two need opposite investigations.
//
// F333 - **STOPPED IS `paused || multiplier == 0`, WHICH IS THE ENGINE'S OWN DEFINITION.**
// `togglePause` computes the byte as `arg | (multiplier == 0)`, and `setFrameSpeedMultiplier`
// writes the multiplier without touching the byte. The first version of this branched on the byte
// alone, so a freeze caused by the speed being driven to zero would have printed "NOT paused, so a
// deliberate pause does NOT explain this freeze" - killing the correct explanation in the one case
// where it was right. Both fields are printed either way, so a reader can always see which of the
// two produced the verdict.
std::string PauseState()
{
    bool  paused = false;
    float mul    = 0.0f;
    if (!coop::PauseSnapshot(&paused, &mul))
        return std::string("  |  PAUSE STATE: NO-GAMEPLAY (the in-game main loop is not running"
                           " this tick - at the title screen, before a load or after a quit) -"
                           " not the same thing as 'not paused'.");
    // F336 - a non-finite multiplier compares false against 0.0f, so without this the freeze line
    // would report a NOT-FINITE speed as a running engine. This is the line where a confident
    // wrong answer costs the most; it says "cannot tell" instead.
    if (!_finite(mul))
    {
        std::stringstream sn;
        sn.imbue(std::locale::classic());
        sn << "  |  PAUSE STATE: pausedFlag=" << (paused ? 1 : 0)
           << " frameSpeedMultiplier=NOT-FINITE - the stopped test CANNOT be evaluated. This is"
              " itself a finding: the engine's speed multiplier should never be NaN or infinite.";
        return sn.str();
    }

    bool stopped = paused || (mul == 0.0f);
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "  |  PAUSE STATE: paused=" << (paused ? 1 : 0)
       << " frameSpeedMultiplier=" << std::fixed << std::setprecision(2) << mul
       << (stopped ? (paused ? " - THE ENGINE IS PAUSED (flag set). Frames keep drawing and the"
                               " pump keeps sending while simulation is held, which is this exact"
                               " freeze signature. The question becomes WHAT stopped it."
                             : " - THE ENGINE IS STOPPED BY A ZERO SPEED MULTIPLIER, with the"
                               " paused flag CLEAR. The engine treats that as paused; a reader"
                               " looking only at the flag would have called this 'not paused'.")
                   : " - running: the flag is clear AND the multiplier is non-zero, so being"
                     " stopped on purpose does NOT explain this freeze.");
    return ss.str();
}

std::string FreezeDiagnosis()
{
    return std::string("  ENGINE THREADS AT THE MOMENT OF THE FREEZE:")
         + ThreadState("aiThread", g_aiThreadId)
         + ThreadState("locomotionThread", g_tuThreadId)
         + ThreadState("updateThread", g_upThreadId)
         + "  |  ALIVE means the thread exists and is blocked or idle - a different failure from"
           " EXITED, and they need different investigations. `never-ran` means our detour on that"
           " path was never called at all, so its id was never captured."
         + PauseState();
}

// P26 stage 0 fold 1 (T631): a name for a thread id among the engine threads recorded above (each detour's first call sets its
// id). The first match wins when two are the same thread.
const char* EngineThreadNameOf(unsigned long tid)
{
    if (tid == 0) return "none";
    if (tid == (unsigned long)g_aiThreadId) return "aiThread(AI::periodicUpdate)";
    if (tid == (unsigned long)g_afThreadId) return "aiFrameThread(AI::update4Frame)";
    if (tid == (unsigned long)g_tuThreadId) return "locomotionThread(Character::threadedUpdate)";
    if (tid == (unsigned long)g_upThreadId) return "updateThread(Character::update, main)";
    return "unnamed";
}

long long WorldActivityTotal()
{
    // Sum of the three per-frame engine counters. Any one of them advancing means the world is
    // still simulating; all three standing still is the F145 signature.
    return (long long)g_aiTotal + (long long)g_tuTotal + (long long)g_upTotal;
}

void ReportSuppression()
{
    std::stringstream ss;
    ss << "[P006] REPORT target=" << Ptr((const void*)g_target)
       << " ai=" << Ptr((const void*)g_targetAI)
       << " watch[autoCaptured,byVerb,forgotten]=" << Num(g_targetAutoCaptured)
                                          << "," << Num(g_targetVerbCaptured)
                                          << "," << Num(g_targetForgotten)
       << " aiTotal=" << Num(g_aiTotal)
       << " aiEntries=" << Num(g_aiTarget)
       << " aiSKIPPED=" << Num(g_aiSkipped)
       << " aiPULSED=" << Num(g_aiPulsed)
       << " tuTotal=" << Num(g_tuTotal)
       << " tuEntries=" << Num(g_tuTarget)
       << " tuSKIPPED=" << Num(g_tuSkipped)
       << " upTotal=" << Num(g_upTotal)
       << " upTarget(RUNNING)=" << Num(g_upTarget)
       // P026 / F214 - the counters that settle whether the peer's combat block runs at all.
       // ccOurTicks is the decisive one: a peer whose replicated characters are never ticked
       // reads ccOurTicks=0 while ccAllTicks climbs. If BOTH are zero the hook did not install
       // and the number says nothing about the engine - which is why both are printed.
       << " ccAllTicks=" << Num(g_ccAllTicks)
       << " ccOurTicks=" << Num(g_ccOurTicks)
       << " ccNoMe=" << Num(g_ccNoMe)
       // P058 / F399 - **THE SLOT NEXT DOOR. `afTarget` BESIDE `aiSKIPPED` IS THE WHOLE POINT.**
       // Both are counted for the SAME population - AIs this mod holds a gate slot for. A large
       // aiSKIPPED with a large afTarget means the same puppets are having their decisions
       // suppressed and their current-action field serviced every frame, by two adjacent vtable
       // slots, one of which this mod has never touched. **afTotal=0 means the hook did not arm -
       // check the [P058] prologue line at startup before reading either number.**
       << " afTotal=" << Num(g_afTotal)
       << " afTarget(UNGATED,RUNNING)=" << Num(g_afTarget)
       << " afThread=" << Num(g_afThreadId)
       << " pos=" << TargetPos()
       << " decisions=" << (g_suppressOn ? "SUPPRESSED" : "live")
       << " locomotion=" << (g_freezeOn ? "FROZEN" : "live")
       << " tuThread=" << Num(g_tuThreadId)
       << " aiPerThread=" << Num(g_aiThreadId)
       << " mainThread=" << Num(g_upThreadId);
    DebugLog(ss.str());
}


bool DriveTarget(float dx, float dz, int frames)
{
    Character* t = (Character*)g_target;
    if (t == 0) { DebugLog("[P006] drive: no target captured"); return false; }

    CharMovement* mv = t->movement;
    if (!LooksLikeObject(mv))
    {
        ErrorLog("[P006] drive REFUSED: movement pointer failed validation");
        return false;
    }

    // STEER_BY_DIRECTION is the engine's own animation-driven motion path - the audit's
    // prescribed way to drive a puppet without fighting the pathfinder.
    mv->setSteeringMode(STEER_BY_DIRECTION);

    g_driveX = dx;
    g_driveZ = dz;
    InterlockedExchange(&g_driveFrames, frames);

    Ogre::Vector3 pos = t->worldPosition();
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(1);
    ss << "[P006] drive: STEER_BY_DIRECTION set, dir=" << dx << "," << dz
       << " frames=" << frames << " from=" << pos.x << "," << pos.y << "," << pos.z;
    DebugLog(ss.str());
    return true;
}

bool MoveTarget(float dx, float dz)
{
    Character* t = (Character*)g_target;
    if (t == 0) { DebugLog("[P006] move: no target captured"); return false; }

    CharMovement* mv = t->movement;
    if (!LooksLikeObject(mv))
    {
        ErrorLog("[P006] move REFUSED: Character::movement did not validate (" + Ptr(mv)
                 + ") - reconstructed offset 0x640 may be wrong for this build");
        return false;
    }

    Ogre::Vector3 pos = t->worldPosition();
    Ogre::Vector3 dest(pos.x + dx, pos.y, pos.z + dz);

    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(1);
    ss << "[P006] move: " << pos.x << "," << pos.y << "," << pos.z
       << " -> " << dest.x << "," << dest.y << "," << dest.z
       << " (movement=" << Ptr(mv) << ")";
    DebugLog(ss.str());

    mv->setPathGoal(dest, PATH_PRIORITY_MEDIUM, true);

    // Read the destination back. If it matches what we just set, the pointer really is a
    // CharMovement and the call took effect - which distinguishes "wrong offset" from
    // "destination set but nothing consumes it".
    Ogre::Vector3 rb = mv->getDestination();
    std::stringstream ss2;
    ss2.imbue(std::locale::classic());
    ss2.setf(std::ios::fixed); ss2.precision(1);
    ss2 << "[P006] move: setDestination returned; readback dest=" << rb.x << "," << rb.y << "," << rb.z
        << (fabs(rb.x - dest.x) < 1.0f && fabs(rb.z - dest.z) < 1.0f
              ? " MATCHES (movement object is real, order accepted)"
              : " MISMATCH (offset 0x640 suspect, or destination overwritten)")
        << " speed=" << mv->speedNow << "/" << mv->desiredSpeed;
    DebugLog(ss2.str());
    return true;
}

::Character* GetTarget()
{
    return (Character*)g_target;
}

// H030: called from the destroy detour for EVERY destruction; clears the watch when it names the target, so no
// later read (TargetPos, zones.cpp's load centre, spawn's reference character) touches a freed object.
void ForgetTarget(const void* obj)
{
    if (obj == 0 || obj != (const void*)g_target) return;
    InterlockedExchangePointer((PVOID volatile*)&g_target, 0);
    InterlockedExchangePointer((PVOID volatile*)&g_targetAI, 0);
    ++g_targetForgotten;
    ErrorLog(std::string("[P006] watched target ") + Ptr(obj) + " was destroyed - watch cleared (H030); until re-armed the zone tick keeps reporting the areas loaded around the last known position (T-306 orphan mode) - when the configured role is host or client the mod re-arms itself on the next player-squad character it sees (AUD)");
}

// --- M2b per-character gating (main thread only) ---

void SuppressCharacter(::Character* c, bool on)
{
    if (c == 0) return;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai))
    {
        ErrorLog("[M2b] gate refused: getBrain() failed validation for character " + Ptr(c));
        return;
    }

    if (on)
    {
        // mirror1 fold (review-mirror1 #2): a slot records WHICH character it gates, and follows its AI to the character
        // that holds it now (an AI address the engine reused belongs to that character).
        GateSlot* had = FindSlot(ai);
        if (had != 0) { had->ch = c; return; }               // already gated
        // A slot naming this character under ANOTHER AI is stale (a character has one AI): release it, or it holds a slot -
        // and an AI address the engine may reuse - until the world ends.
        for (int j = 0; j < kMaxGated; ++j)
            if (g_gated[j].ai != 0 && g_gated[j].ch == (const void*)c)
            {
                InterlockedExchangePointer((PVOID volatile*)&g_gated[j].ai, (PVOID)0);
                InterlockedExchange(&g_gated[j].pulses, 0);
                g_gated[j].ch = 0;
            }
        for (int i = 0; i < kMaxGated; ++i)
        {
            if (g_gated[i].ai == 0)
            {
                g_gated[i].ch = c;
                g_gated[i].pulses = 0;
                g_gated[i].armGen = NextPulseGen();   // G1-c fold L1: a new slot is never an old pulse's
                RaiseGatedHigh((LONG)(i + 1));   // T-354 fold 1: the mark covers slot i BEFORE `ai` is published
                InterlockedExchangePointer((PVOID volatile*)&g_gated[i].ai, ai);
                DebugLog("[M2b] decisions SUPPRESSED for character " + Ptr(c)
                         + " ai=" + Ptr(ai) + " (slot " + Num(i) + ")");
                return;
            }
        }
        ErrorLog("[M2b] gate refused: all " + Num(kMaxGated) + " puppet slots in use");
    }
    else
    {
        GateSlot* slot = FindSlot(ai);
        if (slot == 0) return;
        InterlockedExchangePointer((PVOID volatile*)&slot->ai, 0);
        slot->pulses = 0;
        slot->ch = 0;
        DebugLog("[M2b] decisions RELEASED for character " + Ptr(c));
    }
}

// mirror1 fold (review-mirror1 #2) - RELEASE THE GATE OF A FINISHED CHARACTER, BY ADDRESS. MAIN THREAD.
//
// Called with the character's mirror row (MirrorRelease: despawn applied, stale despawn, the reclaim of a destroyed row) -
// the moment its identity is finished. Before this only SuppressCharacter(c,false) and the world teardown freed a slot, so
// every copy removed or destroyed in a session kept one, keyed by an AI address the engine may hand to a stranger.
// NOT at DropPuppet: H015 re-gates a puppet dropped mid-window precisely so that a body which outlives its puppet (the
// engine refused the destroy) is not a free-running clone; its slot goes when its row does. The object is NOT touched:
// the slot is found by the address recorded at gating, and only this table is written (the same stores as a release).
int ReleaseGateForObject(const void* ch)
{
    if (ch == 0) return 0;
    int n = 0;
    for (int i = 0; i < kMaxGated; ++i)
    {
        if (g_gated[i].ai == 0 || g_gated[i].ch != ch) continue;
        InterlockedExchangePointer((PVOID volatile*)&g_gated[i].ai, (PVOID)0);
        InterlockedExchange(&g_gated[i].pulses, 0);
        g_gated[i].ch = 0;
        ++n;
    }
    return n;
}

bool PulseCharacterQuiet(::Character* c)
{
    if (c == 0) return false;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return false;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return false;      // not suppressed: nothing to let through
    slot->armGen = NextPulseGen();   // G1-c fold H1: every arming takes a new generation
    InterlockedExchange(&slot->pulses, 1);
    return true;
}

// G1-b (review-g1) - see ai_spike.h.
int WithdrawPulse(::Character* c)
{
    if (c == 0) return -1;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return -1;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return -1;
    return (InterlockedCompareExchange(&slot->pulses, 0, 1) == 1) ? 1 : 0;
}

// G1-c - see ai_spike.h. Reads the flag with a no-op compare-exchange; never changes it.
int PulseArmed(::Character* c)
{
    if (c == 0) return -1;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return -1;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return -1;
    return (InterlockedCompareExchange(&slot->pulses, 0, 0) != 0) ? 1 : 0;
}

// G1-c fold - see ai_spike.h.
long PulseArmGen(::Character* c)
{
    if (c == 0) return 0;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return 0;
    GateSlot* slot = FindSlot(ai);
    return (slot == 0) ? 0 : slot->armGen;
}

int PulseArmedFor(::Character* c, long gen)
{
    if (c == 0) return -1;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return -1;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return -1;
    if (slot->armGen != gen) return 2;
    return (InterlockedCompareExchange(&slot->pulses, 0, 0) != 0) ? 1 : 0;
}

int WithdrawPulseFor(::Character* c, long gen)
{
    if (c == 0) return -1;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return -1;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return -1;
    if (slot->armGen != gen) return 2;   // armGen only changes on the main thread, which is this one
    return (InterlockedCompareExchange(&slot->pulses, 0, 1) == 1) ? 1 : 0;   // the G1-b race with the detour, unchanged
}

bool PulseCharacter(::Character* c)
{
    if (c == 0) return false;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return false;

    GateSlot* slot = FindSlot(ai);
    if (slot == 0)
    {
        DebugLog("[M2b] pulse ignored: character " + Ptr(c) + " is not suppressed");
        return false;
    }
    slot->armGen = NextPulseGen();   // G1-c fold H1: every arming takes a new generation
    InterlockedExchange(&slot->pulses, 1);
    DebugLog("[M2b] pulse armed for character " + Ptr(c)
             + " - one decision pass will be allowed through");
    return true;
}

bool IsSuppressed(::Character* c)
{
    if (c == 0) return false;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return false;
    return FindSlot(ai) != 0;
}

// P057 / F395 - **SUPPRESSED *AND* NO PULSE ARMED, WHICH IS A STRICTLY STRONGER QUESTION THAN
// `IsSuppressed` AND IS THE ONE A CALLER TOUCHING THIS AI's MEMORY HAS TO ASK.**
//
// `IsSuppressed` says a gate slot exists. It does **not** say the worker thread is currently barred:
// `PulseCharacter`/`PulseCharacterQuiet` set `pulses = 1`, and the detour then lets **exactly one**
// `AI::periodicUpdate` through. That function clears the requirements memo at `AI+0x238` **at entry
// and again at exit**, on the AI worker thread - so a main-thread caller that frees that same map
// during the pulse window is two threads inside one red-black tree walk, both freeing into
// nedmalloc.
//
// **The pulse is not hypothetical for this caller:** the combat apply path calls
// `InjectAttackOrder` -> `PulseCharacterQuiet` on the same AI, microseconds earlier, in the same
// function that wants to evict.
bool IsQuiescedForMemoryAccess(::Character* c)
{
    if (c == 0) return false;
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return false;
    GateSlot* slot = FindSlot(ai);
    if (slot == 0) return false;                 // not gated at all - its AI runs at full rate
    // **BOTH, AND THE IN-FLIGHT COUNT IS THE ONE THAT MATTERS.** `pulses` catches the
    // armed-but-not-yet-run window; the global count catches a pass actually executing, which is
    // the window `pulses` reads
    // ZERO in. The first version tested only `pulses` and was therefore **anti-correlated with the
    // hazard** - it excluded the safe case and admitted the dangerous one, and its counter would
    // have read like a working guard.
    //
    // **Once both read zero on the main thread, no new pass of THIS FUNCTION can BEGIN before we
    // finish**, because every site that arms a pulse is itself main-thread (seven of them: six under
    // `CommandChannelTick`, and G1's `GetupPulseTick` beside it in `detour_mainLoop` - still the main
    // thread) - so we are the only thread that could start one, and we are here.
    //
    // **AND THAT SENTENCE IS THE WHOLE LIMIT OF THIS GUARD - F401.** It is true of
    // `AI::periodicUpdate` and of nothing else. `AI::frameTick4` - the vtable slot next door -
    // reaches the same AI's requirements map every frame on the worker thread, **needs no pulse, and
    // is gated by nothing this mod owns.** So a caller who reads `true` here and then touches
    // `AI+0x238` is racing a function this guard cannot see, and **extending the in-flight count to
    // cover it would NOT fix that**: the argument above depends on us being the only thread that can
    // start a pass, which is false for a function that starts itself at frame rate.
    //
    // **This returning `true` does not mean the AI's memory is safe to touch. See the header.**
    if (InterlockedCompareExchange(&slot->pulses, 0, 0) != 0) return false;
    if (InterlockedCompareExchange(&g_aiPassesInFlight, 0, 0) != 0) return false;
    return true;
}

int WaitForNoAiPassOn(::Character* c, int maxMs)
{
    // Wait only while THIS character's AI is the one in its pass. The world-wide count
    // (`g_aiPassesInFlight`) is rarely zero in a populated world, so waiting on it timed out
    // almost every time (review round 3, B-2); this returns as soon as OUR pass ends.
    AI* ai = SafeGetAI(c);
    if (!LooksLikeObject(ai)) return 0;
    if (InterlockedCompareExchangePointer(&g_aiPassInFlightAi, 0, 0) != (PVOID)ai) return 0;
    LARGE_INTEGER f, t0, t;
    f.QuadPart = 0;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const LONGLONG limit = (f.QuadPart * (LONGLONG)maxMs) / 1000;
    for (;;)
    {
        Sleep(0);
        if (InterlockedCompareExchangePointer(&g_aiPassInFlightAi, 0, 0) != (PVOID)ai) return 1;
        QueryPerformanceCounter(&t);
        if (t.QuadPart - t0.QuadPart > limit) return 2;
    }
}

long AiPassOverlapSeen() { return InterlockedCompareExchange(&g_aiPassOverlapSeen, 0, 0); }

void* GetCharacterAI(::Character* c)
{
    if (c == 0) return 0;
    return (void*)SafeGetAI(c);
}

void ReleaseSuppression()
{
    InterlockedExchangePointer((PVOID volatile*)&g_target, 0);
    InterlockedExchangePointer((PVOID volatile*)&g_targetAI, 0);
    InterlockedExchange(&g_suppressOn, 0);
    InterlockedExchange(&g_freezeOn, 0);
    DebugLog("[P006] released: no target, no suppression, no freeze");
}

void InstallAiSpike()
{
    DebugLog("[P006] resolving Character::threadedUpdate ...");
    intptr_t tu = (intptr_t)coop::AddrAbs(kMig3CharThreadedUpdate);
    DebugLog("[P006] threadedUpdate target = " + Ptr((void*)tu));

    DebugLog("[P006] resolving Character::update ...");
    intptr_t up = (intptr_t)coop::AddrAbs(kMig3CharUpdate);
    DebugLog("[P006] update target = " + Ptr((void*)up));

    DebugLog("[P006] resolving AI::periodicTick ...");
    intptr_t ap = (intptr_t)coop::AddrAbs(kMig3AiPeriodicUpdate);
    DebugLog("[P006] AI::periodicUpdate target = " + Ptr((void*)ap));

    // P026. Resolved and reported separately, and its failure is NOT fatal to the spike - this
    // is an instrument, and a missing instrument must not take the suppression gate down with
    // it. Expected real RVA 0x60C9E0 (resolve_stub slot 2157); the log prints what it got so a
    // wrong resolution is visible in the run rather than assumed away (F037).
    DebugLog("[P026] resolving CombatClass::periodicTick ...");
    intptr_t cp = (intptr_t)coop::AddrAbs(kMig3CombatPeriodicUpdate);
    uintptr_t imgBase = (uintptr_t)::GetModuleHandleA(0);
    DebugLog("[P026] CombatClass::periodicUpdate target = " + Ptr((void*)cp)
             + " rva=" + Ptr((void*)((uintptr_t)cp > imgBase ? (uintptr_t)cp - imgBase : 0))
             + " EXPECTED rva 0x60C9E0");

    if (tu == 0 || up == 0 || ap == 0)
    {
        ErrorLog("[P006] a target resolved to 0 - spike NOT installed");
        return;
    }

    coop::HookStatus a =
        coop::AddHook((void*)tu, (void*)&detour_threadedUpdate, (void**)&orig_threadedUpdate);
    coop::HookStatus b =
        coop::AddHook((void*)up, (void*)&detour_update, (void**)&orig_update);
    coop::HookStatus c =
        coop::AddHook((void*)ap, (void*)&detour_aiPeriodic, (void**)&orig_aiPeriodic);

    DebugLog(a == coop::SUCCESS ? "[P006] threadedUpdate AddHook SUCCESS"
                                     : "[P006] threadedUpdate AddHook FAILED");
    DebugLog(b == coop::SUCCESS ? "[P006] update AddHook SUCCESS"
                                     : "[P006] update AddHook FAILED");
    DebugLog(c == coop::SUCCESS ? "[P006] AI::periodicUpdate AddHook SUCCESS"
                                     : "[P006] AI::periodicUpdate AddHook FAILED");

    // P058 / F399 / F400 - the slot next door. **Resolved, checked and reported on its own line, and
    // its failure is NOT fatal**: this is an instrument, and a missing instrument must not take the
    // suppression gate down with it. The expected RVA is compared rather than merely printed, for
    // the reason P045 learned the hard way - a counter measuring a different function carries the
    // name of one and the calls of another.
    DebugLog("[P058] resolving AI::frameTick4 ...");
    intptr_t af = (intptr_t)coop::AddrAbs(kMig3AiUpdate4Frame);
    DebugLog("[P058] AI::frameTick4 target = " + Ptr((void*)af)
             + " rva=" + Ptr((void*)((uintptr_t)af > imgBase ? (uintptr_t)af - imgBase : 0))
             + " = the loaded table's AI_frameTick4 row (table '" + coop::AddrTableName() + "'; resolve_stub slot 141 -"
               " the slot ADJACENT to AI::periodicTick, which is slot 142)");
    if (af == 0)
        ErrorLog("[P058] AI::frameTick4 resolved to 0 - afTotal/afTarget will read 0 and that"
                 " is THE PROBE FAILING, not the engine declining to run the function. The"
                 " suppression gate is unaffected.");
    else
    {
        unsigned char afBefore[8];
        ::memcpy(afBefore, (const void*)af, sizeof(afBefore));
        coop::HookStatus e = coop::AddHook((void*)af, (void*)&detour_aiUpdate4Frame,
                                                    (void**)&orig_aiUpdate4Frame);
        unsigned char afAfter[8];
        ::memcpy(afAfter, (const void*)af, sizeof(afAfter));
        const bool afPatched = (::memcmp(afBefore, afAfter, sizeof(afBefore)) != 0);
        DebugLog(std::string("[P058] AI::frameTick4 AddHook ")
                 + (e == coop::SUCCESS ? "SUCCESS" : "FAILED")
                 + " prologuePatched=" + Num(afPatched ? 1 : 0)
                 + "  - **patched=0 means the trampoline did NOT arm**, whatever AddHook returned,"
                   " and afTotal=0 below is the probe rather than the engine.");
        if (e == coop::SUCCESS && !afPatched)
            ErrorLog("[P058] **AddHook RETURNED SUCCESS AND THE PROLOGUE IS UNCHANGED** for"
                     " AI::frameTick4. Read no conclusion from afTotal/afTarget this run.");
    }

    if (cp != 0)
    {
        coop::HookStatus d = coop::AddHook((void*)cp, (void*)&detour_ccPeriodic,
                                                    (void**)&orig_ccPeriodic);
        DebugLog(d == coop::SUCCESS ? "[P026] CombatClass::periodicUpdate AddHook SUCCESS"
                                         : "[P026] CombatClass::periodicUpdate AddHook FAILED");
    }
    else ErrorLog("[P026] CombatClass::periodicUpdate resolved to 0 - the combat tick counter"
                  " will read zero for a reason that has NOTHING to do with the engine");
}

// review-p3o H2 - THE WATCHED TARGET AND THE GATE TABLE, DROPPED BEFORE THE ENGINE FREES THEM.
//
// Called from `detour_worldTeardown` (store.cpp) BEFORE `orig_worldTeardown`, on the main thread.
//
// `g_target` / `g_targetAI` are `volatile` because the AI worker reads them while the main thread
// writes, so both are published with `InterlockedExchangePointer` - the same store `ForgetTarget`
// makes. `ForgetTarget` itself cannot cover a teardown: it hangs off the destroy detour, which
// `GameWorld::_clearAndDestroyGameWorldStuff` does not go through, and `NotifyDespawn` returns
// early off the main thread before ever reaching it (spawn.cpp:908). Everything downstream of the
// watch - `TargetPos`, zones' load centre, spawn's reference character - dereferences it.
//
// `g_gated` holds `AI*` per suppressed puppet, scanned by `FindSlot` on the AI worker thread with
// pointer compares only. Slots ARE released in normal operation (`SuppressCharacter(c,false)`
// zeroes one; mirror1 fold: so does ReleaseGateForObject, with a finished character's mirror row), so clearing them is the existing operation applied in bulk, not a new invariant.
// It matters because the gate is keyed on the ADDRESS: a surviving entry can match an AI object the
// engine allocates at the same address in the next world and silently suppress a stranger's
// decisions - and nothing would ever release it, because the character it was opened for is gone.
//
// `g_aiPassInFlightAi` is NOT touched: it is not a cache, it is the AI the worker is executing
// RIGHT NOW, and only the worker may clear it.
void AiSpikeWorldTeardown()
{
    const bool hadTarget = (g_target != 0);
    InterlockedExchangePointer((PVOID volatile*)&g_target, (PVOID)0);
    InterlockedExchangePointer((PVOID volatile*)&g_targetAI, (PVOID)0);
    // The direct drive is a per-frame command aimed at that target; leaving frames armed would
    // push against a pointer that is now 0 for the rest of the countdown.
    InterlockedExchange(&g_driveFrames, 0);

    int gated = 0;
    for (int i = 0; i < kMaxGated; ++i)
    {
        if (g_gated[i].ai == 0) continue;
        ++gated;
        InterlockedExchangePointer((PVOID volatile*)&g_gated[i].ai, (PVOID)0);
        InterlockedExchange(&g_gated[i].pulses, 0);
        g_gated[i].ch = 0;   // mirror1 fold (review-mirror1 #2)
    }
    InterlockedExchange(&g_gatedHigh, 0);   // T-354 fold 1: AFTER the clear - the next world's claims start the mark again

    DebugLog(std::string("[P006] world teardown: forgot the watched target (")
             + (hadTarget ? "one was armed" : "none was armed") + ") and " + Num((long long)gated)
             + " AI gate slots; this instance has no player load centre until re-armed - when the configured"
               " role is host or client the mod re-arms itself once a world runs again (AUD)");
}

} // namespace coop
