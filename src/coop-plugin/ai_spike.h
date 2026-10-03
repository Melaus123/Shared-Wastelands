// ai_spike - PROBE P006: per-entity AI suppression spike (hypothesis H004, was H001).
// See ai_spike.cpp for the full rationale and threading rules.
#pragma once

#include <string>   // F330: FreezeDiagnosis returns one

class Character;

namespace coop {

// Install hooks: Character::threadedUpdate (locomotion, AI thread),
// Character::update (main thread), AI::periodicUpdate (decisions - F059/F060).
void InstallAiSpike();

// Arm capture with decision suppression pre-enabled: the next character captured has its
// AI::periodicUpdate skipped (no new goals chosen) while it keeps executing and moving.
void ArmSuppression();

// Capture a target but leave its decisions intact - baseline for the same character.
void ArmWatch();

// Capture the first PLAYER-SQUAD character (known mobile; the real co-op puppet case).
void ArmWatchPlayer();

// Toggle DECISION suppression (AI::periodicUpdate gate) on an already-captured target.
void SetSuppress(bool on);

// Toggle the LOCOMOTION freeze (Character::threadedUpdate gate, the F058 lever) on an
// already-captured target. Independent of decision suppression.
void SetFreeze(bool on);

// Order the captured target to walk to (current position + dx, dz). Returns false if
// there is no target or the reconstructed movement pointer fails validation.
bool MoveTarget(float dx, float dz);

// Drive the target directly for N frames using STEER_BY_DIRECTION (audit sec.12 puppet lever).
// Unlike MoveTarget this is a CONTINUOUS command re-applied every frame.
bool DriveTarget(float dx, float dz, int frames);

// Log current counters (called from the command channel).
void ReportSuppression();

// F145: total engine activity, for the freeze watch in soak.cpp.
//
// T046 caught instance A's world simulation STOPPING mid-run - `aiTotal`, `tuTotal` and
// `upTotal` byte-identical across four samples spanning >=346 s - while frames kept advancing
// at 9-10 ms, the mod pump kept sending, and nothing looked wrong. The executor nearly reported
// it as "the two engines derive prone differently from identical health", which would have been
// a major and entirely false finding. **A frozen engine derives nothing.**
//
// It was only caught because they happened to take extra `netstat` samples. An unattended
// 45-60 minute soak would have produced an hour of confident nonsense.
long long WorldActivityTotal();

// F330 - what to print WHEN the world freezes. T091's host froze for 440 s and the eight repeated
// "still frozen" lines carried no evidence at all. This reports whether the engine's worker threads
// are ALIVE (blocked/idle) or EXITED - different failures needing different investigations.
// Main thread, once per freeze edge. Never on a hot path.
std::string FreezeDiagnosis();

// P26 stage 0 fold 1 (T631): which engine thread this file has recorded a thread id is - "aiThread(...)", "locomotionThread(...)",
// ... - or "unnamed" (none of them, or that detour has not run yet). Plain compares; any thread.
const char* EngineThreadNameOf(unsigned long tid);

// Stop suppressing.
void ReleaseSuppression();

// The currently captured target (0 if none). Main thread only.
::Character* GetTarget();
// H030: clear the watch if `obj` is the target (call from the destroy detour). Main thread only.
void ForgetTarget(const void* obj);

// M2b: suppress decisions for a SPECIFIC character (not just the captured target), so
// several puppets can be gated at once. Idempotent.
void SuppressCharacter(::Character* c, bool on);
// mirror1 fold (review-mirror1 #2): release the gate slot(s) opened for the character at this ADDRESS, never touching the
// object (it may be gone). Called with the character's mirror row. Returns the slots freed. MAIN THREAD.
int ReleaseGateForObject(const void* ch);

// M2b: let exactly ONE AI::periodicUpdate through for a suppressed character, so it can
// consume the needGOAP flag an injected order raised (F081/F082) and adopt that order,
// then go back to being suppressed. Returns false if the character is not suppressed.
bool PulseCharacter(::Character* c);

// Same, but SILENT. The combat path (F233) pulses on a cadence while a puppet is in combat
// mode, and the chatty version would write a line every few frames per character - the F189
// runaway in log form. The count is reported instead, which is the information that matters.
bool PulseCharacterQuiet(::Character* c);

// G1-b (review-g1): take back a pulse that the detour has NOT spent yet. 1 = it was still armed and is now
// withdrawn, 0 = nothing armed (already spent, or never armed), -1 = not gated. MAIN THREAD. The
// compare-exchange races the detour's own spend of the same flag: exactly one of the two wins. Disarms only -
// it never arms, so it adds no pulse-arming site.
int WithdrawPulse(::Character* c);

// G1-c: a READ-ONLY look at the same flag. 1 = a pulse is still armed (not spent), 0 = none armed (spent, withdrawn
// or never armed), -1 = not gated. MAIN THREAD. It neither arms nor disarms, so it adds no pulse-arming site and
// takes no part in the WithdrawPulse / detour compare-exchange race.
int PulseArmed(::Character* c);

// G1-c fold (review-g1c H1 / L1): seven main-thread sites arm the SAME per-slot flag, so a pulse is told apart by its
// arm generation - every arming (PulseCharacter / PulseCharacterQuiet) takes a new one, and so does every new gate
// slot. MAIN THREAD, like every arming site. 0 = not gated.
long PulseArmGen(::Character* c);
// Is the pulse armed with generation `gen` still armed? 1 = yes, 0 = spent (flag 0, generation unchanged),
// 2 = superseded (armed again since, or a new slot), -1 = not gated. Read-only.
int PulseArmedFor(::Character* c, long gen);
// WithdrawPulse for the pulse armed with `gen` only: 1 = withdrawn, 0 = spent (the detour won the same compare-exchange
// race as WithdrawPulse), 2 = superseded (nothing touched), -1 = not gated.
int WithdrawPulseFor(::Character* c, long gen);

// Is this character currently decision-suppressed by us?
bool IsSuppressed(::Character* c);

// P059 (review finding 2) - re-gating stops NEW decision passes; it does not stop the one already
// running on the AI worker. Call this AFTER SuppressCharacter(c, true) and BEFORE touching that AI's
// orders/goals. It waits, bounded, until no pass is in flight for ANY gated AI (over-strict: other
// AIs' passes also count - that only makes it wait longer, never return early).
// Returns 0 = nothing was in flight, 1 = waited and it cleared, 2 = timed out (caller counts it).
int  WaitForNoAiPassOn(::Character* c, int maxMs);
// How often two passes were observed in flight at once. Non-zero means the AI worker is NOT a single
// thread and the per-AI wait above cannot be trusted; printed in the NC report.
long AiPassOverlapSeen();


// P057 - **DO NOT USE THIS AS A GUARD FOR TOUCHING AI MEMORY. IT DOES NOT DO THAT. F401.**
//
// It answers exactly one question: *is this character gated AND is no pulse armed*, i.e. is
// **`AI::periodicUpdate`** barred right now. It was written when that was believed to be the only
// worker-thread route into the AI's memory.
//
// **It is not.** `AI::frameTick4` (0x595B20, the vtable slot ADJACENT to the one this mod
// gates) reaches `TaskData::_isRequirementsComplete`, which inserts into the requirements map at
// `AI+0x238` on three exit paths - **every frame, on the AI worker thread, needing no pulse and
// obeying no gate this mod owns.** T101 measured 1,562,804 such calls on suppressed puppets in one
// run.
//
// **AND THE OBVIOUS EXTENSION DOES NOT WORK EITHER.** This function is sound for what it claims only
// because of a specific argument - *once both reads are zero on the main thread no new pass can
// BEGIN, since every site that arms a pulse is itself main-thread.* **That argument does not extend
// to a function that needs no pulse**, so bracketing `frameTick4` with the same in-flight
// count would leave a TOCTOU race against something running at frame rate.
//
// **The only thing that would make main-thread access to AI memory safe is a gate at the engine's
// own choke point, `AITaskSytem::setCurrentTask` (0x50CAB0) - unbuilt, unreviewed (F403).**
//
// Kept because the pulse question is still worth asking on its own terms. **Its one previous caller,
// the requirements-memo eviction, is deleted (F407).**
bool IsQuiescedForMemoryAccess(::Character* c);

// The character's AI object, via the engine's own accessor, SEH-guarded. Returned as void*
// because no AI class is declared here. Caller validates before dereferencing.
void* GetCharacterAI(::Character* c);

// review-p3o H2 - drop the watched target (and its AI), the armed direct drive and every AI gate
// slot before GameWorld::_clearAndDestroyGameWorldStuff frees the objects they name. ForgetTarget
// cannot cover that path: it hangs off the destroy detour, which the teardown does not go through.
// All stores are interlocked, because the AI worker reads these. MAIN THREAD, and only from the
// store's world-teardown hook.
void AiSpikeWorldTeardown();

} // namespace coop
