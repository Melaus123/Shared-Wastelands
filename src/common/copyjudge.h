/* src/common/copyjudge.h - T-573: WHEN A COPY'S PLACEMENT MAY BE JUDGED, AND WHEN A COPY THE PLACEMENT GAVE UP ON IS TRIED
 * AGAIN.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. replicate.cpp (ReplicateTick, DrivePuppet) calls these.
 *
 * THE JUDGE IS HELD WHILE THIS GAME'S SIMULATION IS STOPPED. The drive's progress window, the strike watch and the
 * did-the-placement-work score are counted in drive frames, and the drive runs every frame whether or not the engine is
 * simulating. While the engine is stopped (its pause flag, or a zero speed multiplier - the P039 rule) no character
 * moves, so every window judged then reads "went nowhere" and every placement scores "moved 0.0": a pause spent beside
 * copies that are out of place gave every one of them up. While held, nothing is judged, placed or driven; when the
 * simulation runs again EVERY clock a copy's judgement or grace reads is moved on by the held span - the frame clocks
 * by the frames held (HoldShift / ShiftClock), the real-time ones by the seconds held (HoldShiftSec / ShiftSince) - so
 * a window, a grace or a hold counts running time only.
 * Held only on POSITIVE evidence: a snapshot that could not be taken, or a non-finite multiplier, leaves the judge as
 * it was.
 *
 * A COPY THAT WAS GIVEN UP ON IS TRIED AGAIN, A BOUNDED NUMBER OF TIMES. A corrective that does not work must stop
 * (F189: a snap that moved nothing fired 22,375 times), so a give-up still stops all placement of that copy - but no
 * longer for the rest of its life: after kRearmWaitWindows[n] judged windows it is re-armed (its tries and strikes
 * cleared) and the ordinary placement rules apply again. At most kMaxRearms re-arms per copy; the give-up after the last
 * one stands. Worst case per copy: (1 + kMaxRearms) give-ups' worth of placements, each give-up itself capped (3 tries
 * or 3 strikes) and every placement under the 5-per-60-s rate cap.
 *
 * T-986 floorplace: A GIVE-UP ALSO ENDS WHEN THE OWNER MOVES. The owner's point is stored at the give-up; once it has moved
 * kCatchupEnter (20) units or more from there, the copy is tried again (GiveUpLift) - a new target is a new question, and
 * a timer alone left a copy 300 units behind its walking owner (T1431). That lift spends no timed re-arm, and it applies
 * after the last one too, so the count above bounds a STANDING owner's copy only. T-986 floorplace fold: for a moving
 * owner, the distance doubles with each such lift since a placement of the copy last held (MoveLiftDist: 20, 40, 80, 160,
 * then 160), so an owner pacing in a small area stops lifting a copy whose placements keep failing, and an owner walking
 * away lifts it at most once per 160 units walked; every placement still keeps the 5-per-60-s rate cap (F189's guard).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COMMON_COPYJUDGE_H
#define COMMON_COPYJUDGE_H

namespace copyjudge {

/* The engine's stopped state as the drive reads it, once per frame. snapshotOk = the pause flag and the speed multiplier
   were read this frame; speedFinite = the multiplier is a finite number. Held = read, finite, and the flag set or the
   multiplier zero. */
inline bool SimulationHeld(bool snapshotOk, bool pausedFlag, float speedMul, bool speedFinite)
{
    if (!snapshotOk || !speedFinite) return false;
    return pausedFlag || speedMul == 0.0f;
}

/* One copy's held span. *heldFrom is the frame the hold began, -1 when not held. Called once per frame for every copy:
   while held it marks the first held frame and returns 0; on the first running frame after a hold it returns the number
   of frames held (what the copy's window clocks move on by) and clears the mark; otherwise 0. */
inline long long HoldShift(bool heldNow, long long tick, long long* heldFrom)
{
    if (heldNow)
    {
        if (*heldFrom < 0) *heldFrom = tick;
        return 0;
    }
    if (*heldFrom < 0) return 0;
    const long long s = tick - *heldFrom;
    *heldFrom = -1;
    return s > 0 ? s : 0;
}

/* A window clock that has started (non-zero; 0 = not started yet) moves on by the held span. */
inline long long ShiftClock(long long clock, long long shift)
{
    if (clock == 0 || shift <= 0) return clock;
    return clock + shift;
}

/* HoldShift's twin for the real-time clocks (seconds): *heldFromSec is when the hold began, negative when not held. */
inline double HoldShiftSec(bool heldNow, double now, double* heldFromSec)
{
    if (heldNow)
    {
        if (*heldFromSec < 0.0) *heldFromSec = now;
        return 0.0;
    }
    if (*heldFromSec < 0.0) return 0.0;
    const double s = now - *heldFromSec;
    *heldFromSec = -1.0;
    return s > 0.0 ? s : 0.0;
}

/* A real-time clock, a "since" or an "until" (0 = not set), moves on by the held span in seconds. */
inline double ShiftSince(double t, double shiftSec)
{
    if (t == 0.0 || !(shiftSec > 0.0)) return t;
    return t + shiftSec;
}

const int kMaxRearms = 3;

/* Judged windows (~2 s of running game each) a given-up copy waits before re-arm number rearmsDone + 1: ~30 s, ~2 min,
   ~8 min. -1 = no re-arm left (rearmsDone >= kMaxRearms, or a negative count). */
inline int RearmWaitWindows(int rearmsDone)
{
    if (rearmsDone == 0) return 15;
    if (rearmsDone == 1) return 60;
    if (rearmsDone == 2) return 240;
    return -1;
}

/* A given-up copy is re-armed once it has waited its windows and a re-arm is left. */
inline bool RearmDue(int rearmsDone, int windowsSinceGaveUp)
{
    const int w = RearmWaitWindows(rearmsDone);
    return w >= 0 && windowsSinceGaveUp >= w;
}

/* Whether a give-up being made now will be tried again (said in its log line). */
inline bool RearmLeft(int rearmsDone)
{
    return RearmWaitWindows(rearmsDone) >= 0;
}

/* T-986 floorplace: how a given-up copy's give-up ends at a judged window edge. ownerPointStored = the owner's point was
   stored at the give-up (an owner sample had been heard); ownerMovedSq = squared distance from that point to the owner's
   point now; liftDist = how far the owner must have moved (MoveLiftDist). The owner's move comes first, so a timed re-arm
   is spent only on a standing owner. A distance that is not a finite number never lifts by the owner's move.
   T-986 floorplace fold: the distance is 3D on purpose - an owner who climbed or went down a floor at the same x,z puts its
   copy's placement target on another floor, which is a new question (T-986's subject), not a standing owner. */
const int kLiftNone = 0;
const int kLiftTimer = 1;
const int kLiftOwnerMoved = 2;
inline int GiveUpLift(int rearmsDone, int windowsSinceGaveUp, bool ownerPointStored, float ownerMovedSq, float liftDist)
{
    if (ownerPointStored && liftDist > 0.0f && ownerMovedSq < 3.0e38f && ownerMovedSq >= liftDist * liftDist)
        return kLiftOwnerMoved;
    if (RearmDue(rearmsDone, windowsSinceGaveUp)) return kLiftTimer;
    return kLiftNone;
}

/* T-986 floorplace fold: how far the owner must move for the next owner-moved lift. It doubles with each owner-moved lift
   since a placement of the copy last held (moveLifts; zeroed when a placement's strike watch closes with no strike):
   base, 2x, 4x, 8x, then 8x. Capped at 8x (160 for base 20): below the far placement's 250-unit distance, so an owner who
   walks off far enough to leave its copy a far-placement case always lifts it (T1431's 300-unit loss), however many lifts
   failed before; uncapped, a copy whose placements failed a few times would wait on its owner walking kilometres. With the
   cap, a copy whose placements always fail is placed at most 3 times per 160 units its owner walks (plus the timed re-arms). */
const int kMoveLiftMaxDoublings = 3;
inline float MoveLiftDist(int moveLifts, float baseDist)
{
    float d = baseDist;
    for (int i = 0; i < moveLifts && i < kMoveLiftMaxDoublings; ++i) d *= 2.0f;
    return d;
}

/* T-986 s: A PLACED COPY'S DRIVE WAITS FOR THE PLACEMENT TO SHOW. The engine's teleport shows a few frames later (P157, T1436:
   f5-f13), and a drive that steered from the stale pre-teleport position in between pushed the copy past the point it was
   placed at - landing it as far beyond as it had been short (Confirmed). So after a catch-up placement the drive is held until
   the copy's flat position has moved more than kPlaceLandedFlat from its pre-placement spot (x/z shows at once; the height
   later - T1434), or kPlaceHoldSec of running game has passed (a placement that moves it less, or does not show).
   posOk: the position was read; movedFlatSq: squared flat distance from the pre-placement spot (not a number = not moved);
   secSince: seconds of running game since the placement (not a number = release now). */
const float  kPlaceLandedFlat = 0.5f;
const double kPlaceHoldSec    = 1.0;
const int kPlaceHeld     = 0;   /* hold: no steer, no path issue, no window, no strike */
const int kPlaceLanded   = 1;   /* it moved: release, the progress window starts from here */
const int kPlaceTimedOut = 2;   /* the bound passed: release */
inline int PlaceHold(bool posOk, float movedFlatSq, double secSince)
{
    if (posOk && movedFlatSq < 3.0e38f && movedFlatSq > kPlaceLandedFlat * kPlaceLandedFlat) return kPlaceLanded;
    if (!(secSince < kPlaceHoldSec)) return kPlaceTimedOut;
    return kPlaceHeld;
}

} // namespace copyjudge

#endif
