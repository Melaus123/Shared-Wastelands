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

} // namespace copyjudge

#endif
