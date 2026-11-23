/* src/common/kohold.h - T-540 fix D1: a knocked-out character's COPY stays down until its OWNER's game says it is up.
 *
 * The copy (this game's stand-in for a character another game drives) runs its own engine's medical tick. The tick counts the
 * wake clock (MedicalSystem +0xA0) down by the frame time and, at 0, the engine's collapse check (0x648890) stops keeping the
 * character unconscious; the get-up decision (Character 0x5CF620, threadedUpdatePeriodic) then stands the limp body, or plays
 * dead when 'safe to get up' (0x5C99C0) answers no. Both were the copy's own decisions (F1882 / F1883): it stood before its
 * owner, and while its owner played dead it stood again after every STATE.
 *
 *   HoldRaise         - before the engine's tick on a copy: while the owner's latest word says knocked out and the copy is in
 *                       a running knockout (clock > 0), the clock is raised so this tick's countdown leaves it above 0. It
 *                       never STARTS a knockout (clock <= 0 = nothing), so the knockdown guards stay the only road down.
 *   SafeToGetUpAnswer - the copy's 'safe to get up' answer: its owner playing dead (prone 3) = not safe (the copy plays
 *                       dead too); otherwise the engine's own answer.
 *
 * Pure decisions, no engine types: the plugin (medical.cpp) and the offline suite compile this same header. C++03.
 */
#ifndef SW_KOHOLD_H
#define SW_KOHOLD_H

namespace kohold {

const float kHoldFloor    = 0.1f;      /* the clock left after the tick while held: the copy wakes at most this long after the owner's awake word */
const float kClockMax     = 3600.0f;   /* ApplyLatch's own range guard for a wake clock */
const float kFrameTimeMax = 60.0f;     /* a frame time outside 0..60 s is not trusted: no raise */
const int   kPronePlayingDead = 3;     /* Character +0xE0 PLAYING_DEAD */

/* true = write *raiseTo into the copy's wake clock before the engine's tick. isCopy: a replicated character this game does
   not drive (checked by the caller first). */
inline bool HoldRaise(bool isCopy, bool ownerSaysKo, bool dead, float clock, float frameTime, float* raiseTo)
{
    if (!isCopy || !ownerSaysKo || dead) return false;
    if (!(clock > 0.0f) || !(clock < kClockMax)) return false;                  /* not in a running knockout: never start one */
    if (!(frameTime >= 0.0f) || !(frameTime <= kFrameTimeMax)) return false;   /* NaN / absurd frame time: leave the engine alone */
    const float need = frameTime + kHoldFloor;
    if (clock >= need) return false;                                            /* this tick leaves it above the floor anyway */
    if (raiseTo != 0) *raiseTo = need;
    return true;
}

/* The answer 'safe to get up' gives for this character. ownerProne: the owner's latest STATE prone (0 = none heard). */
inline bool SafeToGetUpAnswer(bool isCopy, bool engineSafe, int ownerProne)
{
    if (!isCopy) return engineSafe;
    if (ownerProne == kPronePlayingDead) return false;
    return engineSafe;
}

}   /* namespace kohold */

#endif
