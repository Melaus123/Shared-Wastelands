/* src/common/runboost.h - T-189 runboost (H051, 2026-09-28): A COPY TRAILING A RUNNING OWNER MAY EXCEED ITS RUN SPEED
 * IN PROPORTION TO ITS LAG, AND WHEN THAT RAISE ENDS.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. replicate.cpp (ApplyDriveSpeed, RunBoostSettle) calls these; the
 * engine reads and writes are stats.cpp's RunCeilingRead / RunCeilingWrite.
 *
 * WHY (T-189, T537): the engine caps every character at its own run speed - Havok is given min(desiredSpeed +0xBC,
 * speedCap +0xB4) (CharMovement::update 0x65F510), +0xB4 is copied each periodicUpdate (0x660330) from
 * AnimationClass +0x19C, and MedicalSystem (0x644840 / 0x645DD0) sets +0x19C through applySpeedCap 0x51BED0 to the
 * character's run speed (CharStats +0x17C). A copy's run speed mirrors its owner's (F966), so a gap a running copy
 * picks up (a stall, a far snap's halt) is preserved until the owner stops. Decision 60: no visible hops - a SPEED fix.
 *
 *   running    = the owner's commanded speed is at or above the copy's own run speed (RUN = 999, or a slow character
 *                commanded at or above its run speed). runboost2 (T-195 R4): a JOG (55) under the run speed is NOT
 *                boosted. The commanded speed picks the animation (F419: isRunning = speedNow > speedCap x K, K
 *                unread; the walk/jog/run switch inside the animation class is unread), so no command above the
 *                owner's own can be shown to keep the jog cycle - review-walk1 item 3 (no bonus for a jogger) stands.
 *   base       = the owner's effective speed: min(its commanded speed, the copy's run speed) = the run speed.
 *   boost      = base x (1 + (kRbMaxMul - 1) x clamp(lag / kRbFullLag, 0, 1)) - runboost2 (T-195): 1.125x at 10
 *                behind, 1.25x at 20, 1.375x at 30, the 1.5x cap at 40. Was base x (1 + clamp(lag / (2 s x base),
 *                0, 0.5)): 1.05x at 10, 1.11x at 20 - T539 halved the >= 20 gaps but did not close them.
 *   ease       = a raise that ends in the drive steps the ceiling down to the run speed at kRbEaseRate (EaseCeiling),
 *                not in one frame (T-195 R1: applySpeedCap also clamps the leg rate, F310).
 *   enter      = running, a usable owner heading, and more than kRbEnterLag BEHIND (signed lag along the owner's travel).
 *   keep       = running, a usable heading, and at least kRbLeaveLag behind; otherwise (under 3, AHEAD, the owner
 *                stopped or no longer running, the lever off) the raise ends and the engine's own ceiling comes back.
 *   ceiling    = raised only when boost exceeds the copy's run speed (a jogger's boost fits under it).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_RUNBOOST_H
#define COOP_COMMON_RUNBOOST_H

namespace cooprunboost {

const float kRbFullLag  = 40.0f;   /* runboost2: units behind at which the boost reaches the cap (was kRbTau 2 s) */
const float kRbMaxMul   = 1.5f;    /* at most 1.5 x the owner's effective speed */
const float kRbEnterLag = 10.0f;   /* units behind before the raise starts */
const float kRbLeaveLag = 3.0f;    /* units behind under which it ends (hysteresis: no flicker at 10) */
const float kRbEaseRate = 200.0f;  /* runboost2 R1: u/s per second the ceiling falls when a raise ends (the 1.5x cap of
                                      a 93.46 runner, 46.7 over, in ~0.23 s = ~14 frames at 60 fps) */
const float kRbEaseMinDt = 1.0f / 240.0f;   /* an ease step's dt is clamped to [1/240, 1/20] s: it always progresses */
const float kRbEaseMaxDt = 0.05f;

enum Step { kRbIdle = 0, kRbEnter = 1, kRbKeep = 2, kRbRestore = 3 };

inline bool RbFinite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }

/* The owner's commanded speed is a run for this copy: at or above the copy's run speed (CharStats +0x17C). runboost2
   (T-195 R4): a jog under the run speed is not - its copy is never commanded above the owner's own gait. */
inline bool OwnerRuns(float authDesired, float copyRun)
{
    if (!RbFinite(authDesired) || !(authDesired > 0.0f)) return false;
    return RbFinite(copyRun) && copyRun > 0.0f && authDesired >= copyRun;
}

/* The owner's effective speed: its command, clamped by the copy's run speed (the engine's own ceiling). 0 = unusable. */
inline float EffectiveSpeed(float authDesired, float copyRun)
{
    if (!RbFinite(authDesired) || !(authDesired > 0.0f)) return 0.0f;
    if (!RbFinite(copyRun) || !(copyRun > 0.0f)) return 0.0f;
    return authDesired < copyRun ? authDesired : copyRun;
}

/* The raised speed for a copy `lag` units behind (lag <= 0 gives base). base <= 0 gives 0. */
inline float BoostSpeed(float base, float lag)
{
    if (!RbFinite(base) || !(base > 0.0f)) return 0.0f;
    float x = RbFinite(lag) ? lag / kRbFullLag : 0.0f;   /* runboost2: the fraction of the way to the cap */
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return base * (1.0f + (kRbMaxMul - 1.0f) * x);
}

/* This frame's step. enabled = the lever; boosted = the copy's ceiling is ours right now; dirOk = the owner has a usable
   heading (moving faster than 2 u/s), without which the lag is unsigned and the owner is not travelling. */
inline int Decide(bool enabled, bool boosted, bool running, bool dirOk, float lag)
{
    const bool ok = enabled && running && dirOk && RbFinite(lag);
    if (!ok) return boosted ? kRbRestore : kRbIdle;
    if (boosted) return lag < kRbLeaveLag ? kRbRestore : kRbKeep;
    return lag > kRbEnterLag ? kRbEnter : kRbIdle;
}

/* The ceiling must be raised above the engine's own (the copy's run speed) for this boost to reach Havok. */
inline bool NeedsCeiling(float boost, float copyRun)
{
    return RbFinite(boost) && RbFinite(copyRun) && copyRun > 0.0f && boost > copyRun;
}

/* runboost2 (T-195 R1): one frame of the eased end - the ceiling `cur` stepped down toward `run` at kRbEaseRate over dt
   seconds (clamped to [kRbEaseMinDt, kRbEaseMaxDt], so a zero or huge dt still takes a bounded step). Returns `run`
   when the step reaches it or anything is unusable: the caller then restores the engine's value exactly. */
inline float EaseCeiling(float cur, float run, float dt)
{
    if (!RbFinite(cur) || !RbFinite(run) || !(run > 0.0f)) return run;
    float d = RbFinite(dt) ? dt : 0.0f;
    if (d < kRbEaseMinDt) d = kRbEaseMinDt;
    if (d > kRbEaseMaxDt) d = kRbEaseMaxDt;
    const float next = cur - kRbEaseRate * d;
    return next > run ? next : run;
}

}   /* namespace cooprunboost */

#endif
