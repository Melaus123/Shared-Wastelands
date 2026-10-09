// medical.h - M4 STATE. Step 1 is MEASUREMENT ONLY; nothing here changes behaviour yet.
#pragma once

#include <string>

class Character;

namespace coop {
bool CopyOwnerSaysDead(unsigned int uid);   // T-303: the owner's latest word (its SPAWN or its STATE) says dead. ANY THREAD.
unsigned int SpawnOwnerFlags(const void* character);   // T-303 OWNER: coopspawn::kSpawnFlagDead / kSpawnFlagKo for its SPAWN. MAIN THREAD.
void SpawnNoteFlagsSent(unsigned int flags);   // T-303 fold 1 OWNER: counts spawnFlagDeadSent / KoSent for a SPAWN the send accepted. MAIN THREAD.
void CopyNoteSpawnFlags(unsigned int uid, unsigned int flags);   // T-303 fold 1 COPY: a present flags byte is the owner's whole word (dead, KO). MAIN THREAD.
bool SpawnDeathLookWanted(unsigned int uid);   // T-303 fold 1 (decision 223): a SPAWN-dead copy still waits for its first APPEARANCE. MAIN THREAD.
void SpawnDeathNoteLookApplied(unsigned int uid, bool ok);   // T-303 fold 1 (decision 223): the look apply ran for uid. MAIN THREAD.
void ApplyOwnerDeathAtSpawn(unsigned int uid);   // T-303 COPY: the SPAWN says dead - kill the new copy now (ApplyOwnerDeath). MAIN THREAD.
void SpawnDeathForgetCopy(unsigned int uid);   // a stale copy made again: the old body's waiting SPAWN-death entry goes. MAIN THREAD.
void SpawnDeathRetryTick();   // T-303 COPY: retries a SPAWN death that could not land at once, ~0.25 s, up to 30 s. MAIN THREAD.
bool CopyOwnerSaysKo(unsigned int uid);   // crash1b: the owner's last STATE said knocked out
// par5 (parity P5): the owner's {hunger, fed} from its STATE. has 0 = not carried. Written on the copy at once, and
// held before each of the copy's own medical ticks (detour_medicalUpdate), so its own hunger clock cannot starve it or
// knock it out on one screen only. MAIN THREAD.
void ApplyOwnerHunger(unsigned int uid, int has, float hunger, float fed);
// par5 TEST-ONLY lever (`hungerset <uid> <value>`): set a character THIS game drives to that hunger and push its STATE.
std::string HungerSetOwned(unsigned int uid, float value);
void CopyNoteOwnerKo(unsigned int uid, bool ko);   // crash1c (R3a): write the owner's word to that table. ANY THREAD.
// The per-copy word tables (the owner's hunger, knocked-out and dead words per copy uid): uid's words are forgotten - its copy
// was removed here, or its ownership moved to or from this game. MAIN THREAD.
void MedicalForgetCopy(unsigned int uid);
// ... every copy's words are forgotten - world teardown, session end. MAIN THREAD.
void MedicalForgetAllCopies();
// par6 (parity P6): death is the owner's. ANY THREAD - the declareDead hook (worldstate.cpp) asks this first: true lets
// the engine's Character::declareDead run; false refuses a copy's own death (the owner's latest STATE does not say dead,
// or a body rebuild is in flight), counted, logged from the main thread, with MedicalSystem::dead put back to 0 when the
// call came from medicalUpdate 0x651570 (par6b: that flag IS hasDied's byte, so the gate knows the copy was alive by the
// caller - medicalUpdate calls only on a live character - not by hasDied; armed on 1.0.65 Steam only). par6 fold (review-par6): `callerRet` is the hook's _ReturnAddress() - a refused kill from a one-shot
// event caller (Inventory::deathCheck 0x75DCA0, Character::unequipItem 0x5DB880) is queued as a death request to
// the owner (#1); `worldRecordKill` is worldstate.cpp's scoped flag around the notebook's twin kill, which passes (#2).
bool CopyDeathAllowed(void* character, unsigned long long callerRet, bool worldRecordKill);
// par6 fold (review-par6 #1): MSG_PRISON kind 4 DEATH from the copy's game - a player's one-shot kill of our character's
// copy. MAIN THREAD (OnPrison). Applied through Character::declareDead only when `uid` is driven here and alive.
void ApplyOwnerDeathRequest(unsigned int uid, const std::string& caller, unsigned int fromPeer);
// par6: the owner's `dead` from its STATE (MAIN THREAD, OnState). dead=1 on a live copy -> the engine's own death steps
// (MedicalSystem::dead = 1, Character::declareDead 0x7A5660); on a dead copy a no-op; dead=0 never resurrects. par6b: counted
// applied only when declareDead went through - refused by the gate or faulted, the flag is cleared and it counts deferred.
void ApplyOwnerDeath(unsigned int uid, int dead);
// par6 TEST-ONLY lever (`kill <uid>`): the engine's death steps on a loaded replicated character. Owned: dies, STATE pushed.
// A copy: the copy gate must refuse it, and the lever puts the flag (= hasDied's byte) back (par6b).
std::string KillLever(unsigned int uid);
// T-306 TEST-ONLY lever (`destroybody <uid>`): a DEAD character THIS game owns (not a copy of another game's) has its
// body destroyed through GameWorld::destroy 0x798F50 with justUnloaded=false and reason "eaten" - the exact call
// MedicalSystem 0x64F8E0 makes when an animal has eaten a body - so the mod's destroy hook sees what it sees then.
// Refuses (and says why) a uid not present here, not ours, or not dead. MAIN THREAD (the command channel).
std::string DestroyBodyLever(unsigned int uid);
// A copy's dead body never rots on its own. For each DEAD copy of another game's character in the copy-control list, the game's corpse rot start (Character+0xD0) is held so the body is never more than 11.75 of the game's 12
// in-game hours into its rot (src/common/corpsedecay.h): the owner's DESPAWN (or the owner's departure) is what removes it. Every frame, skipped while EngineWritesBlocked().
// [DECAY] pin lines (the first 20), decay[pinned,uids,unread] on the [P014] REPORT line. MAIN THREAD.
void CorpseDecayTick();
// TEST-ONLY lever (`decaysoon <uid> <hours>`): a DEAD body on this game (own or a copy) has its rot start set so the
// game rots it after `hours` in-game hours (0 <= hours < 12); logs the start before and after. A copy's start is held again
// by CorpseDecayTick. MAIN THREAD (the command channel).
std::string DecaySoonLever(unsigned int uid, float hours);
// P10 TEST-ONLY lever (`bodydown`): the engine's own MedicalSystem::knockout on a character THIS game drives, then its
// STATE at once. 1 unconscious after the call, 0 called but not unconscious yet (P10 fold 1 (T633): the call only sets
// the wake-up clock; the lever checks again after a tick), -1 not ours / no character / unreadable / knockout unresolved.
// MAIN THREAD.
int KnockOutOwned(unsigned int uid);
// P11 fold 1 (T652) TEST-ONLY (`koself`): hold an owned character's wake-up clock at least `seconds` (never shorter),
// then push its STATE. The clock after, -1 = not ours / no character / unreadable. MAIN THREAD.
float KnockClockAtLeastOwned(unsigned int uid, float seconds);

// PROBE P017 (H011 / F134) - DIAGNOSTIC ONLY, changes nothing.
//
// `MedicalSystem::getCollapseStage` does NOT decide from the per-part record alone. It reads a
// block of LATCHED booleans that are not a function of anything we replicate - and one of them,
// `lowHealthKnockout` (+0x162), is both TESTED AND SET by that same function, so it carries HISTORY.
// That is the standing candidate for why the peer computes NORMAL where the authority computes
// KO with 35 of 35 per-part cells matching to 0.1 (F129), and for T042's six unshared knockout
// episodes (F135, register P-11).
//
// It is printed and NOT replicated on purpose. Measure first: a fix that lands before the
// measurement cannot be attributed, and this project has paid for that twice (F102, F120).
//
// One compact field, cheap enough to hang off an every-frame transition line.
std::string LatchString(::Character* c);

// F140 / register P-11: push one owned character's state RIGHT NOW, because something just
// happened to it. MAIN THREAD.
//
// Why this exists. The periodic push is not the ~1 Hz it looks like: `NextOwnedUid` round-robins
// ONE character per tick, so with four replicated characters each one is revisited about every
// FOUR SECONDS.
//
// **AND THAT NUMBER NOW UNDERSTATES IT BY THIRTY-FOLD.** P-16 Option A replicates the whole world
// population - T083 adopted 136 characters - so the round-robin revisits any one of them roughly
// **every two minutes**. Recorded here rather than "fixed", because the round-robin is the BACKSTOP,
// not the mechanism: F144's event-driven push sends immediately on a prone or dead transition
// (median follow latency 1.0 ms), and that is what carries anything a player would see. What the
// slow backstop loses is slow DRIFT in values nothing transitions on - and whether that is visible
// is unmeasured at this scale. Raising the rate is still the band-aid it always was. T042 recorded six knockout episodes lasting 9-127 ms and `corrections = 0` on
// both instances - not agreement, and not disagreement either. The channel simply never looked.
//
// Raising the poll rate is the band-aid: it scales with character count and still loses anything
// shorter than the new period. The evidence says event-driven is the real answer - the HIT path
// is already event-driven, and it is precisely the path on which the two instances entered a
// knockout **1 ms apart** (T043), the only genuinely shared episode ever recorded.
//
// Returns true if something was actually sent (owned, resolvable, link up).
bool StatePush(unsigned int uid);

// F147 - THE LATCH BLOCK NOW TRAVELS, and this is attempt 3 at "the peer should derive prone
// itself". Recorded as attempt 3 rather than dressed up as something else.
//
// What changed to justify it: T047 produced the first LIVE-vs-LIVE prone disagreement - both
// engines demonstrably simulating, health agreeing to 0.011, state arriving in ~3 ms so latency
// is no longer an explanation - and the logs name one field.
//
//   A: prone=4 unc=1        B: prone=0 unc=0        (paired digests, same character)
//
//   All ELEVEN times the peer undid a knockout, its own `unconcious` was 0.
//   Both times it entered one unaided, its own `unconcious` was 1.
//
// The peer is not failing to derive. It is deriving correctly from a flag we never sent it.
//
// Why T043/T044 saw these fields agree 96 times running: neither run ever produced a live-vs-live
// disagreement, so the fields were never sampled in the state that matters (the exercised-check
// lesson, in its purest form).
//
// And the compensation it replaces was VISIBLY failing, not merely inelegant: we wrote prone=4,
// the peer's engine undid it within 9-125 ms, and we rewrote it every ~6 s - so that character
// dropped and stood up EIGHT TIMES in 38 seconds on one screen and not the other.
//
// `dead` was deliberately NOT carried at the time of F147. par6 (parity P6) carries it: the owner's `dead` kills the
// copy through the engine's own path (ApplyOwnerDeath) and a copy's own declareDead is refused (CopyDeathAllowed).
const unsigned int kLatchCrippled   = 1u << 0;
const unsigned int kLatchUnconcious = 1u << 1;
const unsigned int kLatchSub50KO    = 1u << 2;
const unsigned int kLatchBloodTrauma= 1u << 3;
const unsigned int kLatchRightArmOk = 1u << 4;
const unsigned int kLatchLeftArmOk  = 1u << 5;

// H012: stop characters we do NOT own from running their own medical simulation. Default OFF,
// so one session can measure the same fight with it off and then on.
void SetMedicalGate(bool on);

// H029: `koTimer` = MedicalSystem::knockoutClock (+0xA0), the INPUT the peer's reassessCollapseMode derives
// `unconcious` from (medical.md, 'The knockout revert, localised'). nextKnockoutAt (+0x158) is read by nobody.
unsigned int SnapshotLatch(::Character* c, float* nextKnockoutAt, float* koTimer);
void         ApplyLatch(::Character* c, unsigned int bits, float nextKnockoutAt, float koTimer);

// C2 (read-KO 2026-09-22): held by ApplyState while it applies the owner's STATE to a puppet. For that
// thread, for that long, the puppet knockout gate (medical.cpp: startKnockoutTimer 0x643D40 / knockout
// 0x643EF0) lets the engine start a clock; anywhere else a puppet's clock starts only if the owner's latest
// STATE says knocked out. ApplyLatch calls neither function (it writes the fields directly); the guard is
// for ApplyHealth and the prone write, whose engine callees are not mapped.
struct ApplyingOwnerState
{
    ApplyingOwnerState();
    ~ApplyingOwnerState();
private:
    long prev_;
    ApplyingOwnerState(const ApplyingOwnerState&);
    ApplyingOwnerState& operator=(const ApplyingOwnerState&);
};

// PROBE P014 (DIAGNOSTIC ONLY, F123): hook MedicalSystem::medicalUpdate and count calls,
// attributed per entity, recording the thread it runs on. Passes every call through
// untouched - it must not change a value, or the drift rate it exists to measure would be
// the drift rate of our own interference. Installed once at startup.
void InstallMedicalProbe();

// M4 step 3 (F126): push the authority's full state for one owned character per call,
// round-robin, from the main-thread pump. Periodic rather than on-change so that ANY
// divergence self-heals within a second without needing to know its cause.
void StateTick();

// Counters + the measured thread id (extends the report command).
void ReportMedical();

// M4 state readout: PoseState (POSE_KNOCKED_OUT = 4), hasDied, blood, bleed rate, and whether we own
// the character. This is what answers "does KO already follow on the puppet from the
// per-limb health we replicate today?" - the question that decides how much M4 needs.
void ReportKO(unsigned int uid);

// TEST HARNESS ONLY (F128). Damage one part directly on a character we OWN, so the KO
// question can be asked deterministically instead of waiting for a brawl to land a lucky
// blow on the right limb - four runs produced a knockout twice, in opposite directions,
// and not at all the other two. Refuses on any uid we do not own.
bool WoundPart(unsigned int uid, int part, float stun, float cut);

// T-556: how Character::declareDead was reached, from the caller's return address (swfallen::kCause*). ANY THREAD.
int DeathCallerCause(unsigned long long ret);

} // namespace coop
