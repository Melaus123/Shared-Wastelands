/* src/common/floorplace.h (T-424) - A COPY IS LIFTED (OR LOWERED) ONTO ITS OWNER'S FLOOR WHEN WALKING CANNOT GET IT THERE.
 *
 * The engine sets a character's floor from a ray cast DOWN from kFloorGapY units above the character's own feet on each
 * movement update, so a copy standing under its owner on a lower floor never finds the upper one by walking: its drive's
 * arrival and progress tests are flat (x/z only), so a copy directly under its owner reads "arrived" and idles, and a copy
 * whose owner climbed a short stair or ledge beside it can stop closing on the stop point for good. This header decides when
 * such a copy is placed at the owner's stop point AT THE OWNER'S HEIGHT (the engine's own ray then sets the floor there).
 *
 * ONE RULE, followplay.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a
 * header, so the offline suite drives it directly. replicate.cpp (FloorStep, in the copy's drive just before its arrival
 * test) calls these.
 *
 *   place when    = the owner is stopped (a follow copy: its owner's stop point is held) AND the copy arrived flat (within
 *                   its arrival radius in x/z) or stopped closing on the stated stop point (followplay's stall watch) AND
 *                   the height gap |owner y - copy y| is over kFloorGapY AND the copy arrived or is within kFloorReach
 *                   flat of the point - a copy further away is still the walking drive's (and the far snap's) to close.
 *   give up when  = kFloorStrikes placements were already made at the same stop point and the gap is open again: every
 *                   placement at one stop point counts, so a copy that falls back down after a placement (a floor the
 *                   copy's game does not have) is placed at most kFloorStrikes times there; a new stop point starts the
 *                   count again (FloorWatchStop).
 *   not a number  = never places.
 */
#ifndef FLOORPLACE_H_INCLUDED
#define FLOORPLACE_H_INCLUDED

namespace floorplace {

const float kFloorGapY     = 20.0f;   /* the engine's ray starts this far above the character's feet: a gap over it is a floor */
const float kFloorReach    = 40.0f;   /* flat distance within which a copy that stopped closing on the stop point is placed */
const int   kFloorStrikes  = 2;       /* placements at one stop point before it gives up on that point */
const float kFloorSameStop = 1.0f;    /* a stop point within this flat distance of the last one is the same stop */

const int kFloorNone   = 0;   /* nothing to do */
const int kFloorPlace  = 1;   /* place the copy at the stop point, at the owner's height */
const int kFloorGaveUp = 2;   /* it would be placed, but kFloorStrikes placements were already made at this stop point */

inline bool FlFinite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
inline float FlAbs(float v) { return v < 0.0f ? -v : v; }

/* The decision, every judged frame of the copy's drive. ownerStopped: the owner stands (a follow copy: its stop point is
   held). stopHeld: a stop point is stated - the stall watch (stalled) is about closing on it, so it counts only then.
   arrivedFlat: the drive's own flat arrival test passed. flatDist: x/z distance to the point it would be placed at.
   dY: owner height - copy height. strikes: placements made at this stop point so far. */
inline int FloorPlace(bool ownerStopped, bool stopHeld, bool arrivedFlat, bool stalled, float flatDist, float dY, int strikes)
{
    if (!FlFinite(dY) || !FlFinite(flatDist)) return kFloorNone;
    if (!ownerStopped) return kFloorNone;
    if (!(arrivedFlat || (stalled && stopHeld))) return kFloorNone;
    if (!(FlAbs(dY) > kFloorGapY)) return kFloorNone;
    if (!arrivedFlat && !(flatDist <= kFloorReach)) return kFloorNone;
    return strikes >= kFloorStrikes ? kFloorGaveUp : kFloorPlace;
}

/* T-986 g: WHEN A PLACEMENT'S RESULT IS JUDGED. A copy placed with the engine's teleport has not moved two frames later; T-986 g3:
   its x/z lands at once and its HEIGHT ~1 s later (T1434 - Confirmed: uid 19 moved 25.9 flat at once and was judged 'again' at
   0.08 s on that movement while the placement held). So the result is judged on the height alone: 'ok' as soon as the copy
   stands within kFloorGapY of the height it was placed at (at least 2 frames on); 'again' ONLY once kFloorJudgeWaitSec of
   running game has passed with the height still apart. 1.5 s: the height was seen to land by ~1 s; the margin keeps a slow
   landing from being read as a failure. A placement reached late (the copy's drive skipped it for a while) is judged on the
   height it has then. framesSince: frames since the placement; secSince: seconds of running game since it (not a number =
   judge now); heightApart: the copy's height is still over kFloorGapY from the height placed at, or unreadable. */
const double kFloorJudgeWaitSec = 1.5;
const int kFloorJudgeWait     = 0;   /* still pending: no decision, no strike, no second placement */
const int kFloorJudgeLanded   = 1;   /* the height is within the gap: judged 'ok' */
const int kFloorJudgeTimedOut = 2;   /* the bound has passed: judged on the height now ('again' when still apart) */
inline int FloorJudgeDue(long long framesSince, double secSince, bool heightApart)
{
    if (framesSince >= 2 && !heightApart) return kFloorJudgeLanded;
    if (!(secSince < kFloorJudgeWaitSec)) return kFloorJudgeTimedOut;
    return kFloorJudgeWait;
}

/* T-986 g2: FloorPlace's inputs for one copy. An owner that arrived without walking (a teleport, a load, being carried) sends
   no stop (T1433: a copy stayed downstairs under such an owner for good), so a follow copy's owner counts stopped on a held stop
   OR when it is not moving now (ownerMovingNow: the drive's own fresh answer, expiring at the velocity horizon); a non-follow
   copy's owner as before (not moving now). A follow copy counts stalled on the stop point's watch (only with a held stop - it is
   about closing on that point) OR on the drive's own stall count (driveStalledWindows >= 1: windows in which it went nowhere or
   did not close); a non-follow copy is placed only when arrived, as before. Each stall input carries its own condition, so
   FloorPlace's stopHeld gate is passed as satisfied. */
inline int FloorPlaceCopy(bool follow, bool stopHeld, bool ownerMovingNow, bool arrivedFlat, bool stopStalled, int driveStalledWindows,
                          float flatDist, float dY, int strikes)
{
    const bool ownerStopped = follow ? (stopHeld || !ownerMovingNow) : !ownerMovingNow;
    const bool stalled = follow && ((stopHeld && stopStalled) || driveStalledWindows >= 1);
    return FloorPlace(ownerStopped, true, arrivedFlat, stalled, flatDist, dY, strikes);
}

/* The next judged frame after a placement: the gap is still over kFloorGapY (or unreadable) - the placement's result is 'again'. */
inline bool FloorStillApart(float dY) { return !FlFinite(dY) || FlAbs(dY) > kFloorGapY; }

/* One copy's floor watch, keyed on the stop point it is judged against. */
struct FloorWatch
{
    int have; float sx, sz;   /* the stop point the strikes belong to */
    int strikes;              /* placements made at it */
    int gaveUpSaid;           /* the give-up at it was logged and counted */
    int gatedSaid;            /* a gated placement at it was counted */
    int faultSaid;            /* an unreadable position at it was counted */
};

inline void FloorWatchReset(FloorWatch* w) { w->have = 0; w->sx = w->sz = 0.0f; w->strikes = 0; w->gaveUpSaid = 0; w->gatedSaid = 0; w->faultSaid = 0; }

/* The stop point this frame judges against: a different one (or the first) starts the count again. 1 = a new stop. */
inline int FloorWatchStop(FloorWatch* w, float x, float z)
{
    if (w->have)
    {
        const float dx = x - w->sx, dz = z - w->sz;
        const float d2 = dx * dx + dz * dz;
        if (FlFinite(d2) && d2 <= kFloorSameStop * kFloorSameStop) return 0;
    }
    FloorWatchReset(w);
    w->have = 1; w->sx = x; w->sz = z;
    return 1;
}

}   /* namespace floorplace */

#endif
