/* src/common/windowfollow.h - T-1422 fix 2 (F1412 / F1414; owner 1771 a/b): INSIDE A NATIVE FIGHT WINDOW THE COPY FOLLOWS ITS
 * AUTHORITY'S RECORDED ROUTE, CORRECTED SMOOTHLY.
 *
 * Inside a window the engine's fight routine (CombatClassAI::update) owns the copy's legs and chases this game's copy of its
 * target; a fight on stairs then settles where the local copies are, floors away from the owner. Fix 2 stops that walking
 * after every fight update (combat.cpp detour_ccUpdate) and places the copy each frame toward its authority's route replayed
 * ~0.3 s back (followplay::TargetAt on the FollowFrame clock); see FIX 2b below for the height. (The ground check's ray
 * starts above the walking body; its offset DAT_141682188 is unread here.) Blow outcomes are the authorities' (copy blows are ignored), so the
 * copy's position serves nothing but the picture.
 *
 * FIX 2b / 2c (reviews of a68289722 and b8a52a4c0): the step is FLAT - gap, deadband and cap on x and z along the owner's
 * recorded route; the copy's height is never set. The engine's per-frame ground check (CharMovement::update 0x65E6D0) applies
 * the ground hit to the height whether or not the copy is kept updated (decomp_65e6d0.txt:152-160 - both branches reach the
 * hit), and the byte fix 2b gated on (ray object +0x18) is a per-request result byte, not a switch: a height written by the
 * mod would be put back on the surface under the copy. Floors come only from walking the owner's route (the flat follow over
 * the stairs) and, for a copy already more than half a floor from a fresh owner report, from the engine's route finder walking
 * it to the owner's point (FloorWalkStarts / FloorWalkEnds; the request is renewed on the AI worker after each fight pass);
 * no placement, no teleport.
 * The aim (RouteTarget) is FollowFrame's: the owner's stated stop, else the route replayed on the follow clock, else - behind
 * schedule (followplay::BehindScheduleStep) - the owner's newest report.
 *
 * PURE C++03 (VS2010 v100), header-only, no <cmath> (square root by Newton steps). Swept by the offline suite
 * (t1422_window_follow_step). */
#ifndef WINDOWFOLLOW_H_INCLUDED
#define WINDOWFOLLOW_H_INCLUDED

#include "followplay.h"

namespace windowfollow {

/* The reconcile's tuning, passed in (replicate.cpp owns the values: kReconcileTau, kReconcileMaxRate, kReconcileBoostErr,
   kReconcileBoostSlope, kReconcileMaxRateBoost, kReconcileMaxStepAbs, kReconcileDeadband, kReconcileMaxDt). */
struct Tuning { float tau, baseRate, boostErr, boostSlope, maxRateBoost, maxStepAbs, deadband; double maxDt; };

enum { kStepBad = -1, kStepDeadband = 0, kStepMoved = 1, kStepZero = 2 };

struct Step { float x, y, z; float err, requested, step, rate; bool sat, boosted; };

inline bool WfFinite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
inline bool WfFiniteD(double v) { return v == v && v < 1.0e300 && v > -1.0e300; }

inline float WfSqrt(float v2)
{
    if (!(v2 > 0.0f) || !WfFinite(v2)) return 0.0f;
    float v = v2 > 1.0f ? v2 : 1.0f;
    for (int j = 0; j < 40; ++j) v = 0.5f * (v + v2 / v);
    return v;
}

/* e^-x for x >= 0, to float precision: e^-x = (e^-(x/2^k))^(2^k), the inner by a short series. */
inline float WfExpNeg(float x)
{
    if (!(x > 0.0f)) return 1.0f;
    if (x > 60.0f) return 0.0f;
    int k = 0;
    while (x > 0.03125f) { x *= 0.5f; ++k; }
    float r = 1.0f - x + x * x * 0.5f - x * x * x / 6.0f + x * x * x * x / 24.0f;
    while (k-- > 0) r *= r;
    return r;
}

/* The pull rate in units per second for an error: the owner's replayed speed (not a number or below 0 reads as 0) plus the
   reconcile's rate - its base, and past boostErr the boost (boostSlope per unit of error, the whole rate held at maxRateBoost). */
inline float PullRate(float err, float ownerSpeed, const Tuning& t, bool* boosted)
{
    float rate = t.baseRate;
    *boosted = false;
    if (err > t.boostErr)
    {
        rate = t.baseRate + (err - t.boostErr) * t.boostSlope;
        if (rate > t.maxRateBoost) rate = t.maxRateBoost;
        *boosted = true;
    }
    const float own = (WfFinite(ownerSpeed) && ownerSpeed > 0.0f) ? ownerSpeed : 0.0f;
    return rate + own;
}

/* One frame's placement from the copy's position (cx, cy, cz) toward the route point (tx, ty, tz). FLAT: the gap, the deadband
   and the cap are on x and z; the height is the copy's own (the engine's ground check owns it). dt: the frame interval (held at
   t.maxDt - elapsed time is never banked into one step). The step is err x (1 - exp(-dt / tau)), capped at the pull rate x dt
   and at maxStepAbs plus the owner's own distance this frame (a ceiling that still lets the copy keep pace). Writes the new
   point to *out on kStepMoved. */
inline int StepToward(float cx, float cy, float cz, float tx, float ty, float tz, double dt, float ownerSpeed, const Tuning& t, Step* out)
{
    out->x = cx; out->y = cy; out->z = cz; out->err = 0.0f; out->requested = 0.0f; out->step = 0.0f; out->rate = 0.0f;
    out->sat = false; out->boosted = false;
    if (!WfFinite(cx) || !WfFinite(cy) || !WfFinite(cz) || !WfFinite(tx) || !WfFinite(ty) || !WfFinite(tz) || !WfFiniteD(dt)) return kStepBad;
    const float ex = tx - cx, ez = tz - cz;
    const float e2 = ex * ex + ez * ez;
    if (!WfFinite(e2)) return kStepBad;   /* an overflowing gap is no reading */
    const float err = WfSqrt(e2);
    out->err = err;
    if (err <= t.deadband) return kStepDeadband;
    double d = dt;
    if (d < 0.0) d = 0.0;
    if (d > t.maxDt) d = t.maxDt;
    const float requested = err * (1.0f - WfExpNeg((float)(d / (double)t.tau)));
    bool boosted = false;
    const float rate = PullRate(err, ownerSpeed, t, &boosted);
    const float own = (WfFinite(ownerSpeed) && ownerSpeed > 0.0f) ? ownerSpeed : 0.0f;
    float cap = rate * (float)d;
    const float ceiling = t.maxStepAbs + own * (float)d;
    if (cap > ceiling) cap = ceiling;
    float step = requested;
    bool sat = false;
    if (step > cap) { step = cap; sat = true; }
    out->requested = requested; out->rate = rate; out->boosted = boosted;
    if (!(step > 0.0f)) return kStepZero;
    const float f = step / err;
    out->x = cx + ex * f; out->z = cz + ez * f;
    out->step = step; out->sat = sat;
    return kStepMoved;
}

/* The floor walk (fix 2b, review BLOCKER 1b): a floor is ~91-103 high in the tower of T2590 (F1388), so half a floor is 50. */
const float kFloorHalf = 50.0f;
const float kFloorWalkEnd = 25.0f;   /* the walk ends within a quarter floor of the owner's height (the owner's floor number is not on
                                        the wire, so its height stands in) ... */
const float kFloorWalkReach = 20.0f; /* ... and within the follow's flat reach of the owner's point */
/* starts: a fresh owner report over half a floor from the copy */
inline bool FloorWalkStarts(bool fresh, float dy) { return fresh && WfFinite(dy) && (dy > kFloorHalf || dy < -kFloorHalf); }
/* ends: on a fresh owner report, within a quarter floor and within the follow's flat reach */
inline bool FloorWalkEnds(bool fresh, float dy, float flat)
{
    return fresh && WfFinite(dy) && dy <= kFloorWalkEnd && dy >= -kFloorWalkEnd && WfFinite(flat) && flat <= kFloorWalkReach;
}
/* the walk's goal moves only when the owner's newest point is on another floor band than the current goal */
inline bool GoalBandChanged(float goalY, float ownerY) { const float d = ownerY - goalY; return WfFinite(d) && (d > kFloorHalf || d < -kFloorHalf); }
/* fix 2d: ... or more than twice the follow's reach flat from it (the owner walked off on the same floor: the copy must not park) */
inline bool GoalMoves(float gx, float gy, float gz, float ox, float oy, float oz)
{
    if (GoalBandChanged(gy, oy)) return true;
    const float dx = ox - gx, dz = oz - gz;
    const float d2 = dx * dx + dz * dz;
    return WfFinite(d2) && d2 > (2.0f * kFloorWalkReach) * (2.0f * kFloorWalkReach);
}

/* fix 2f (F1424, H-T1422-walkpass): WHICH ROAD ISSUES THE FLOOR WALK'S ROUTE REQUEST. The AI worker renews it after each fight
   pass (the pass sets the movement mode and so cancels a route, F1414); when the copy's own engine has ended its fighting no pass
   runs and the walk froze (T2602). So the main thread issues it on a frame where no fight pass ran for the copy since its
   previous frame (the slot's pass count unchanged - an exact state, no timer). fold 2g (review of e4f40e48d): the worker renews
   after EVERY pass - a pass cancels the route, so a pass with no renewal after it left the walk stopped; a second same-goal request
   in a frame the main thread also issued in is harmless (setPathGoal 0x6607E0 returns early near the same destination). */
enum { kWalkRoadNone = 0, kWalkRoadWorker = 1, kWalkRoadMain = 2 };
/* the AI worker, after a pass: renews whenever the copy is walking and its walk has not failed */
inline int WalkRoadAfterPass(bool walking, bool failed)
{
    return (walking && !failed) ? kWalkRoadWorker : kWalkRoadNone;
}
/* the main thread, once a frame: issues only when no pass ran since its previous frame (passesPrev < 0: no previous read yet) */
inline int WalkRoadMainFrame(bool walking, bool failed, long long passesNow, long long passesPrev)
{
    if (!walking || failed || passesPrev < 0) return kWalkRoadNone;
    return passesNow == passesPrev ? kWalkRoadMain : kWalkRoadNone;
}

/* The aim, as FollowFrame takes it: kAimStop - the owner's stated stop (sx, sy, sz); kAimRoute - the route replayed at tPlay;
   kAimBehind - the replayed point has fallen behind schedule against the owner's newest report (nx, ny, nz)
   (followplay::BehindScheduleStep, *behind carried frame to frame), so the newest report; kAimNone - no recording and no stop
   (the outputs are left as given). *speed: the owner's speed at the aim (0 at a stop). */
enum { kAimNone = 0, kAimRoute = 1, kAimStop = 2, kAimBehind = 3 };
inline int RouteTarget(const followplay::Track& k, double tPlay, double followD, bool stopHeld, float sx, float sy, float sz,
                       float nx, float ny, float nz, bool* behind, float* x, float* y, float* z, float* speed)
{
    if (stopHeld) { *behind = false; *x = sx; *y = sy; *z = sz; *speed = 0.0f; return kAimStop; }
    followplay::Sample t;
    if (followplay::TargetAt(k, tPlay, followplay::kExtrapMaxSec, &t) == followplay::kModeEmpty) { *behind = false; return kAimNone; }
    if (!WfFinite(t.x) || !WfFinite(t.y) || !WfFinite(t.z)) { *behind = false; return kAimNone; }
    float hx = 0.0f, hz = 0.0f, v = 0.0f;
    if (!followplay::RouteHeading(k, &hx, &hz, &v)) v = 0.0f;
    *behind = followplay::BehindScheduleStep(*behind, t.x, t.z, nx, nz, v, followD);
    if (*behind) { *x = nx; *y = ny; *z = nz; *speed = v; return kAimBehind; }
    *x = t.x; *y = t.y; *z = t.z;
    *speed = t.still ? 0.0f : WfSqrt(t.vx * t.vx + t.vz * t.vz);
    return kAimRoute;
}

/* T-1422 fix 3 (F1447, design A): THE WINDOW STEP'S FLOOR. The movement's set-position (0x65DDE0) re-places the physics body with
   no floor key (-1); the engine's body placement (0x145E10 -> 0x3A1F70) then searches the whole walking map along a vertical line
   and can pick the floor above. So the step first asks the walking map for its nearest point to the step's point (the walking map's
   own closest-point query 0x3A2E60, as the engine's setDestination does) and places the copy there, the body with that point's
   piece key. What the step does:
     kFloorStepPlace  a point and its piece's key were found: set the movement at the point, then re-place the body with the key;
     kFloorStepPlain  the movement has no physics body: the set-position touches no body and no floor - the old call, unchanged;
     kFloorStepHold   nothing found (no point, no key, no walking map, or the query faulted): the copy stays where it is this
                      frame - never the old keyless set, which is the call that picks the wrong floor.
   query: 1 found, 0 nothing found, -1 no walking map, -2 the walking map was busy (the query's lock was not taken: it answers
   0xFFFFFFFF, decomp_3a21c0), -3 the query faulted. */
enum { kFloorStepPlace = 0, kFloorStepPlain = 1, kFloorStepHold = 2 };
inline int FloorStepDecide(bool haveBody, int query, int key)
{
    if (!haveBody) return kFloorStepPlain;
    return (query == 1 && key != -1) ? kFloorStepPlace : kFloorStepHold;
}
enum { kFloorQueryFound = 1, kFloorQueryNone = 0, kFloorQueryNoMap = -1, kFloorQueryBusy = -2, kFloorQueryFault = -3 };
/* the walking map query's answer (the low 32 bits of its return): 0xFFFFFFFF busy, 0 nothing found, anything else found */
inline int FloorQueryResult(unsigned int r) { return r == 0xFFFFFFFFu ? kFloorQueryBusy : (r != 0 ? kFloorQueryFound : kFloorQueryNone); }
const float kWinFloorDy = 20.0f;
/* review S1: the keyed body placement lost the piece - the body's key after it is not the found key, or the body's height after
   it is over 20 from the found point (the keyed call fell into the whole-map search: decomp_cf2cf0, the piece test failed) */
inline bool FloorKeyLost(int keyFound, int keyAfter, float bodyYAfter, float foundY)
{
    if (keyAfter != keyFound) return true;
    const float d = bodyYAfter - foundY;
    return !WfFinite(d) || d > kWinFloorDy || d < -kWinFloorDy;
}
/* the [WINFLOOR] line: the first 200 steps, then every 500th (n = the step's 1-based number), every step whose found point is
   more than 20 high from the step's height (or whose height is not a number), and every step whose piece was lost (FloorKeyLost) */
const long long kWinFloorSampleFirst = 200, kWinFloorSampleEvery = 500;
inline bool WinFloorLineDue(long long n, bool found, float dy, bool keyLost)
{
    if (n <= kWinFloorSampleFirst || (n % kWinFloorSampleEvery) == 0) return true;
    return found && (keyLost || !WfFinite(dy) || dy > kWinFloorDy || dy < -kWinFloorDy);
}
/* the sideways pull: how far the found point is from the step's point on x / z */
inline float FlatPull(float ax, float az, float bx, float bz) { const float dx = bx - ax, dz = bz - az; return WfSqrt(dx * dx + dz * dz); }

}   /* namespace windowfollow */

#endif
