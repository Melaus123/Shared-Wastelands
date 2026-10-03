/* src/common/followplay.h - EVERY COPY FOLLOWS BEHIND ITS OWNER: the copy of a character another game owns (a player's own
 * character, its squad, or any NPC) replays its owner's reported route a small fixed delay behind, never past the furthest
 * point the owner is known to have reached, and stops on the exact spot the owner's own stop message names.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. replicate.cpp (ApplyRemoteMove, ApplyRemoteMoveStop, FollowDriveStep,
 * ApplyDriveSpeed, the owner's per-frame stop check) calls these.
 *
 *   the recording  = the owner's last kTrackLen reports, each stamped with the OWNER's clock (MOVE's stamp), oldest first;
 *                    a report older than the newest is dropped (MOVE rides the unreliable road and can arrive out of order).
 *   the clock      = the smallest (arrival - stamp) over the last kClockWindowSec: the fastest delivery, so a later report
 *                    reads late by exactly its jitter. Owner time now = arrival time - that offset.
 *   the delay D    = clamp(kDelayIntervals x the measured report interval, kDelayMinSec, kDelayMaxSec): two intervals, so
 *                    one lost report still leaves a real report on either side of the replayed moment.
 *   the target T   = the recording at owner time (now - offset - D), on a straight line between the two reports either side;
 *                    past the newest report it moves on along that report's velocity for at most kExtrapMaxSec, then holds.
 *   N, h           = the newest reported point (the furthest the owner is KNOWN to have reached) and the direction of the
 *                    newest moving report's velocity (unsmoothed).
 *   owner moving   = the newest report is a moving one AND it arrived within the velocity's horizon (OwnerMovingNow): an
 *                    owner that falls silent mid-walk - a character its game stopped streaming without a word - stands
 *                    where it was last reported, so its copy walks there and goes idle.
 *   destination    = the engine's pathfinder walks the copy to: the owner's stop point once it is stated; else N on a
 *                    copy the engine updates only in turns (it moves in big saved-up steps, which must end at a reported
 *                    point), on the copy of a character outside every player stand-in's faction (an NPC copy), or while
 *                    the owner stands; else - a player's copy - the nearer of the owner's Move-order goal G and the
 *                    keep-walking point K = N + h x KeepWalkDist(owner speed), so a fresh path always lands before the old
 *                    one ends.
 *   path or push   = a player's copy takes the path from its owner's first moving report; an NPC copy only beyond the
 *                    path-entry distance from its replay target or after a stalled window (FollowPathEnter), and a hidden
 *                    NPC copy re-plans to N only after N moved kNpcHiddenReplanDist (NewestReplanDist).
 *   path service   = the per-frame path budget serves player copies first - their stop issues, then their other issues -
 *                    and NPC copies after them, oldest waiting first within each (PathServeRank); an NPC copy's issue
 *                    always waits for that queue (PathTakesLeftover).
 *   never past     = a copy updated every frame halts the frame it is level with N (or the stop point) along h.
 *   speed          = the error e = (T - copy) . h: behind raises the speed (the catch-up levers, fed e), ahead by more than
 *                    kPaceDeadErr lowers it by up to kPaceMaxCut.
 *   the stop       = the owner sends one stop for any character it streams that moved, the frame the engine's own
 *                    moving flag reads 0 and the body has stood still kStopStillFrames frames running. A player's copy
 *                    arrives within kStopArriveDist of it; an NPC copy, or any copy that has stopped closing on it for
 *                    kStopStallSec (a crowd round the spot), within kStopArriveWide (StopArriveRadius).
 *   idle           = a copy standing on its owner's standing point, on no path, whose replay has passed the owner's last
 *                    moving report: only the arrival stop acts on it, so its replay is not run (ReplayIdle).
 *   behind schedule= no stop is stated and the target T lies further from the owner's newest reported point O (the last
 *                    report that reached this game, recorded or not) than the delay allows (BehindScheduleStep). Then the
 *                    recording no longer says where the owner is: the copy aims at O, its destination is O, its catch-up is
 *                    fed by its distance to O and the never-past halt is off (its path and push end at O itself) - the way
 *                    the drive before the follow drive closed a gap. Every snap of a follow copy places it on O, or on the
 *                    stated stop point (OwnerPoint), never on a target the recording left behind.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef FOLLOWPLAY_H_INCLUDED
#define FOLLOWPLAY_H_INCLUDED

namespace followplay {

const int    kTrackLen        = 16;      /* 16 reports at ~0.1 s = 1.6 s: the delay plus a lost burst */
const double kDelayDefaultSec = 0.2;     /* the delay while no report interval has been measured */
const double kDelayMinSec     = 0.15;    /* covers the newest report's largest measured age (0.12 s) */
const double kDelayMaxSec     = 0.30;    /* a slow owner's longer interval never pushes the delay past this */
const double kDelayIntervals  = 2.0;     /* the delay in report intervals */
const double kExtrapMaxSec    = 0.05;    /* the furthest the target is moved on past the newest report */
const double kClockWindowSec  = 5.0;     /* the clock offset is the smallest gap over this long */
const float  kKeepWalkSec     = 0.75f;   /* the keep-walking point lies this many seconds of the owner's speed beyond N ... */
const float  kKeepWalkPad     = 2.0f;    /* ... plus this many units ... */
const float  kKeepWalkMin     = 8.0f;    /* ... at least this ... */
const float  kKeepWalkMax     = 60.0f;   /* ... at most this */
const float  kPastSlack       = 0.5f;    /* a copy less than this short of the limit along h is level with it */
const float  kPastRange       = 30.0f;   /* the never-past check applies only this close to the limit */
const float  kRestartAhead    = 6.0f;    /* a halted copy starts again once N is this far ahead of it along h */
const float  kBoostEnterLag   = 4.0f;    /* the run raise starts this far behind the target ... */
const float  kBoostLeaveLag   = 1.0f;    /* ... and ends under this */
const float  kPaceDeadErr     = 1.5f;    /* ahead of the target by more than this: slowed */
const float  kPaceFullErr     = 10.0f;   /* the full cut at this far ahead */
const float  kPaceMaxCut      = 0.3f;    /* at most 30% off the speed */
const float  kStopStillMove   = 0.05f;   /* the body moved less than this since the previous frame: still */
const int    kStopStillFrames = 2;       /* still this many frames running (with the engine's flag 0) = stopped */
const int    kStopStillPasses = 2;       /* an unreadable flag: still on this many send passes running = stopped */
const float  kStopArriveDist  = 0.5f;    /* the arrival radius of the last push onto a stated stop point */
const float  kGoalSameDist    = 2.0f;    /* a goal within this of the one a stop used up is that goal again */
const float  kStopSameDist    = 0.5f;    /* a moving report this close to the stop point is the stop's own trailing report */
const float  kStopLeaveDist   = 2.0f;    /* any report this far from the stop point, stamped after it, ends the stop */
const double kReplaySettleSec = 1.0;     /* the replay has passed the last moving report this long after delay D */
const float  kStopArriveWide  = 1.5f;    /* the stop's arrival radius for an NPC copy, or a copy that stopped closing on it:
                                            the general arrival radius, which NPC copies stopped within before the follow drive */
const float  kStopProgressMin = 0.25f;   /* closing on the stop point = a new nearest distance by at least this much ... */
const double kStopStallSec    = 1.0;     /* ... within this long; none = the copy stopped closing (blocked by bodies round the
                                            spot). A walking copy closes by several units a second; four 0.25 u steps at most
                                            fit between the two radii, so a jittering body cannot hold the narrow one long */
const double kBehindPadSec   = 0.1;    /* behind schedule: T further from O than the owner's speed x (D + this) ... */
const float  kBehindPadDist  = 4.0f;   /* ... + this. A healthy recording keeps T within speed x D of O (the newest report's
                                          owner time is never later than the owner time the copy reads now, and T is D before
                                          that). The pad is one report interval of the owner's motion - the reported speed
                                          is one 0.1 s sample's and an owner speeding up outruns it - plus kBoostEnterLag, the
                                          lag at which the follow catch-up starts anyway: a walker (27 u/s, D 0.2) is behind
                                          past 12.1 u against 5.4 u on schedule, a runner (73 u/s) past 25.9 u against 14.6 */
const float  kNpcHiddenReplanDist = 12.0f;   /* a hidden NPC copy re-plans to N once N moved this far from its destination:
                                            3 x the visible re-plan distance, a third of the path requests of a running NPC
                                            nobody sees; under half a second of a 27 u/s run, and well inside the never-past
                                            range and the teleport distance */

/* One MOVE sample's own verdict (the owner's MeasureVelocity). */
const int kSampleNone   = 0;
const int kSampleMoving = 1;
const int kSampleStill  = 2;

enum { kModeEmpty = 0, kModeInterp = 1, kModeExtrap = 2, kModeHeld = 3 };
enum { kDestNone = 0, kDestStop = 1, kDestGoal = 2, kDestKeep = 3, kDestNewest = 4 };
enum { kMoveApply = 0, kMoveStale = 1, kMoveClearsStop = 2 };

inline bool FpFinite(double v) { return v == v && v < 1.0e300 && v > -1.0e300; }
inline bool FpFiniteF(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }

struct Sample { float x, y, z, vx, vz; double t; bool still; };
struct Track { Sample s[kTrackLen]; int first; int n; };

inline void TrackClear(Track* k) { k->first = 0; k->n = 0; }
/* i = 0 the oldest .. n - 1 the newest */
inline const Sample& TrackGet(const Track& k, int i) { return k.s[(k.first + i) % kTrackLen]; }
inline const Sample& TrackNewest(const Track& k) { return TrackGet(k, k.n - 1); }

/* 1 = added, 2 = it replaced the newest (the same owner time), 0 = dropped (older than the newest, or not a number). */
inline int Push(Track* k, const Sample& s)
{
    if (!FpFinite(s.t) || !FpFiniteF(s.x) || !FpFiniteF(s.z) || !FpFiniteF(s.vx) || !FpFiniteF(s.vz)) return 0;
    if (k->n > 0)
    {
        const Sample& nw = TrackNewest(*k);
        if (s.t < nw.t) return 0;
        if (s.t == nw.t) { k->s[(k->first + k->n - 1) % kTrackLen] = s; return 2; }
    }
    if (k->n < kTrackLen) { k->s[(k->first + k->n) % kTrackLen] = s; ++k->n; return 1; }
    k->s[k->first] = s;   /* full: the oldest slot takes the newest */
    k->first = (k->first + 1) % kTrackLen;
    return 1;
}

/* The recording at owner time tPlay. Before the oldest report: the oldest. Between two: a straight line. Past the newest:
   on along its velocity (a still report stays put) for at most maxExtrapSec - kModeExtrap - then held there - kModeHeld. */
inline int TargetAt(const Track& k, double tPlay, double maxExtrapSec, Sample* out)
{
    if (k.n <= 0 || !FpFinite(tPlay)) return kModeEmpty;
    const Sample& o = TrackGet(k, 0);
    if (tPlay <= o.t) { *out = o; return kModeInterp; }
    for (int i = 0; i + 1 < k.n; ++i)
    {
        const Sample& a = TrackGet(k, i);
        const Sample& b = TrackGet(k, i + 1);
        if (tPlay > b.t) continue;
        const double span = b.t - a.t;
        const float u = span > 0.0 ? (float)((tPlay - a.t) / span) : 1.0f;
        out->x = a.x + (b.x - a.x) * u; out->y = a.y + (b.y - a.y) * u; out->z = a.z + (b.z - a.z) * u;
        out->vx = a.vx + (b.vx - a.vx) * u; out->vz = a.vz + (b.vz - a.vz) * u;
        out->t = tPlay; out->still = a.still && b.still;
        return kModeInterp;
    }
    const Sample& nw = TrackNewest(k);
    const double over = tPlay - nw.t;
    const double e = over < maxExtrapSec ? over : maxExtrapSec;
    *out = nw;
    if (!nw.still) { out->x = nw.x + nw.vx * (float)e; out->z = nw.z + nw.vz * (float)e; }
    out->t = tPlay;
    return over <= maxExtrapSec ? kModeExtrap : kModeHeld;
}

/* The owner moves now: its newest report is a moving one and it arrived no more than maxAgeSec ago (newestAgeSec, the
   velocity's horizon - the expiry the copy's arrival stop uses). An owner game does not announce every character it stops
   streaming, and silence means stop: an expired moving report is the owner standing where it was last reported. A value
   that is not a number is not moving. */
inline bool OwnerMovingNow(const Track& k, double newestAgeSec, double maxAgeSec)
{
    if (k.n <= 0 || TrackNewest(k).still) return false;
    return FpFinite(newestAgeSec) && FpFinite(maxAgeSec) && newestAgeSec <= maxAgeSec;
}

/* h: the unit direction of the newest MOVING report's velocity, and that report's speed. false = none in the recording. */
inline bool RouteHeading(const Track& k, float* hx, float* hz, float* speed)
{
    for (int i = k.n - 1; i >= 0; --i)
    {
        const Sample& s = TrackGet(k, i);
        if (s.still) continue;
        const float v2 = s.vx * s.vx + s.vz * s.vz;
        if (!(v2 > 1.0e-6f) || !FpFiniteF(v2)) continue;
        float v = v2 > 1.0f ? v2 : 1.0f;   /* the square root by Newton steps: no <cmath> in a pure header */
        for (int j = 0; j < 40; ++j) v = 0.5f * (v + v2 / v);
        *hx = s.vx / v; *hz = s.vz / v; *speed = v;
        return true;
    }
    return false;
}

/* lagT's half: the owner time at which the recorded route passes nearest (x, z) - the copy is where the owner was then.
   false = no recording. */
inline bool TrackTimeNear(const Track& k, float x, float z, double* t)
{
    if (k.n <= 0) return false;
    if (k.n == 1) { *t = TrackGet(k, 0).t; return true; }
    float best = -1.0f; double bt = 0.0;
    for (int i = 0; i + 1 < k.n; ++i)
    {
        const Sample& a = TrackGet(k, i);
        const Sample& b = TrackGet(k, i + 1);
        const float sx = b.x - a.x, sz = b.z - a.z;
        const float l2 = sx * sx + sz * sz;
        float u = l2 > 1.0e-6f ? ((x - a.x) * sx + (z - a.z) * sz) / l2 : 0.0f;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        const float px = a.x + sx * u - x, pz = a.z + sz * u - z;
        const float d2 = px * px + pz * pz;
        if (best < 0.0f || d2 < best) { best = d2; bt = a.t + (b.t - a.t) * u; }
    }
    *t = bt;
    return true;
}

/* The keep-walking distance beyond N for an owner moving at v (its reported speed, never the copy's). */
inline float KeepWalkDist(float v)
{
    const float u = (v > 0.0f && FpFiniteF(v)) ? v : 0.0f;
    float d = u * kKeepWalkSec + kKeepWalkPad;
    if (d < kKeepWalkMin) d = kKeepWalkMin;
    if (d > kKeepWalkMax) d = kKeepWalkMax;
    return d;
}

/* The playback delay for a measured report interval (<= 0 = not measured yet). */
inline double PlaybackDelay(double intervalSec)
{
    if (!(intervalSec > 0.0) || !FpFinite(intervalSec)) return kDelayDefaultSec;
    double d = intervalSec * kDelayIntervals;
    if (d < kDelayMinSec) d = kDelayMinSec;
    if (d > kDelayMaxSec) d = kDelayMaxSec;
    return d;
}

/* The owner's clock, per owner game: the stamp (u32 milliseconds) unwrapped against the last one, and the offset. */
struct ClockOff { bool have; double curMin, prevMin, bucketAt; bool haveStamp; double lastMs; };
inline void ClockReset(ClockOff* c) { c->have = false; c->curMin = c->prevMin = 0.0; c->bucketAt = 0.0; c->haveStamp = false; c->lastMs = 0.0; }
/* A u32 millisecond stamp as a continuous count of milliseconds: the wrap at 2^32 is taken against the last stamp seen. */
inline double StampUnwrapMs(ClockOff* c, unsigned int stamp)
{
    const double kWrap = 4294967296.0;
    double v = (double)stamp;
    if (c->haveStamp)
    {
        const double cycles = (double)(long long)(c->lastMs / kWrap);   /* whole wraps in the count so far */
        v = cycles * kWrap + (double)stamp;
        if (v < c->lastMs - kWrap * 0.5) v += kWrap;
        else if (v > c->lastMs + kWrap * 0.5) v -= kWrap;
    }
    if (!c->haveStamp || v > c->lastMs) c->lastMs = v;
    c->haveStamp = true;
    return v;
}
/* One report's (arrival, owner time) in seconds: the offset = the smallest arrival - owner time over the last 2 half-windows
   (kClockWindowSec in all). Returns the offset to use now. */
inline double ClockOffsetUpdate(ClockOff* c, double arrival, double ownerT)
{
    const double gap = arrival - ownerT;
    if (!FpFinite(gap)) return c->have ? (c->curMin < c->prevMin ? c->curMin : c->prevMin) : 0.0;
    if (!c->have) { c->have = true; c->curMin = gap; c->prevMin = gap; c->bucketAt = arrival; }
    else if (arrival - c->bucketAt >= kClockWindowSec * 0.5 || arrival < c->bucketAt)
    {
        c->prevMin = c->curMin; c->curMin = gap; c->bucketAt = arrival;
    }
    else if (gap < c->curMin) c->curMin = gap;
    return c->curMin < c->prevMin ? c->curMin : c->prevMin;
}

/* e = (T - copy) . h: positive = the copy is behind the target along the owner's route. */
inline float AlongError(float tx, float tz, float cx, float cz, float hx, float hz) { return (tx - cx) * hx + (tz - cz) * hz; }

/* A point never past the limit (lx, lz) along h: one beyond it is moved back along h onto the limit's crossing line. */
inline void ClampToLimit(float tx, float tz, float lx, float lz, float hx, float hz, float* ox, float* oz)
{
    const float over = (tx - lx) * hx + (tz - lz) * hz;
    if (over > 0.0f) { *ox = tx - hx * over; *oz = tz - hz * over; }
    else { *ox = tx; *oz = tz; }
}

/* The never-past check: the copy at (cx, cz), within kPastRange of the limit, is level with it along h (less than
   kPastSlack short of it, or past it) - it halts. */
inline bool NeverPastHalt(float cx, float cz, float lx, float lz, float hx, float hz)
{
    const float dx = cx - lx, dz = cz - lz;
    const float d2 = dx * dx + dz * dz;
    if (!FpFiniteF(d2) || d2 > kPastRange * kPastRange) return false;
    return dx * hx + dz * hz > -kPastSlack;
}
/* A halted copy starts again once N is more than kRestartAhead ahead of it along h. */
inline bool NeverPastRestart(float cx, float cz, float nx, float nz, float hx, float hz)
{
    return (nx - cx) * hx + (nz - cz) * hz > kRestartAhead;
}

/* THE DESTINATION the engine's pathfinder walks the copy to. */
struct DestIn
{
    bool stopHeld; float sx, sz;          /* the owner's stated stop point */
    bool hidden;                          /* the engine updates this copy only in turns (Character +0xE4 == 0) */
    bool ownerMoving;                     /* the owner's newest report is a moving one */
    bool haveN; float nx, nz;             /* N */
    bool haveH; float hx, hz, v;          /* h and the owner's reported speed */
    bool haveG; float gx, gz;             /* the owner's Move-order goal */
    float cx, cz;                         /* the copy */
    bool playerCopy;                      /* the character is in a player stand-in's faction */
};
/* An NPC copy - whose owner gives it no Move order, so no goal ends a keep-walking path - walks only to N, as a hidden copy
   does: never to a point past where its owner is known to be. */
inline int FollowDest(const DestIn& in, float* dx, float* dz)
{
    if (in.stopHeld) { *dx = in.sx; *dz = in.sz; return kDestStop; }
    if (!in.haveN) return kDestNone;
    *dx = in.nx; *dz = in.nz;
    if (!in.ownerMoving || !in.haveH) return kDestNewest;
    const float alongN = (in.nx - in.cx) * in.hx + (in.nz - in.cz) * in.hz;
    const float alongG = in.haveG ? (in.gx - in.cx) * in.hx + (in.gz - in.cz) * in.hz : -1.0f;
    const bool gAhead = in.haveG && FpFiniteF(alongG) && alongG >= 0.0f;   /* behind the copy: passed, so not this trip's */
    if (in.hidden || !in.playerCopy)
    {
        if (gAhead && alongG < alongN) { *dx = in.gx; *dz = in.gz; return kDestGoal; }
        return kDestNewest;
    }
    const float kw = KeepWalkDist(in.v);
    if (gAhead && alongG < alongN + kw) { *dx = in.gx; *dz = in.gz; return kDestGoal; }
    *dx = in.nx + in.hx * kw; *dz = in.nz + in.hz * kw;
    return kDestKeep;
}

/* THE PATH OR THE PUSH, entry (a copy not on a path): beyond enterDist from its aim (the replay target, or the stop point),
   or after a stalled window still leaveDist or more from it; and a player's copy also from its owner's first moving report
   (with h), so its planning overlaps the delay. An NPC copy keeps the push near its target, as before the follow drive: the
   path budget is shared by every copy in the area. A distance that is not a number enters nothing. */
inline bool FollowPathEnter(bool playerCopy, bool ownerMoving, bool haveH, float distA, float enterDist, float leaveDist, int stalledWindows)
{
    if (!FpFiniteF(distA)) return false;
    if (distA > enterDist) return true;
    if (playerCopy && ownerMoving && haveH) return true;
    return stalledWindows >= 1 && distA >= leaveDist;
}

/* The distance N must move from a live destination at N before the copy re-plans to it: baseDist, or kNpcHiddenReplanDist
   for a hidden NPC copy (nobody sees it; the engine moves it in saved-up steps anyway). */
inline float NewestReplanDist(bool playerCopy, bool hidden, float baseDist)
{
    return (hidden && !playerCopy) ? kNpcHiddenReplanDist : baseDist;
}

/* THE PATH BUDGET's order (lower first; oldest waiting first within a rank): a player's copy's stop issue (0), its other
   issues (1), an NPC copy's stop issue (2), its other issues (3). Every copy that is not a player's is an NPC copy here. */
inline int PathServeRank(bool playerCopy, bool priority)
{
    return (playerCopy ? 0 : 2) + (priority ? 0 : 1);
}
/* Whether an issue that fell due this frame may take budget the queue left over: a player's copy, or a row the follow drive
   does not drive (its old rule). An NPC follow copy always queues, one frame at least, so a player's copy due this frame is
   never refused because an NPC copy earlier in the map took the last of it. */
inline bool PathTakesLeftover(bool followCopy, bool playerCopy)
{
    return !followCopy || playerCopy;
}

/* THE STOP's arrival radius: kStopArriveDist for a player's copy still closing on the stop point; kStopArriveWide for an NPC
   copy, or a copy that stopped closing (StopProgressStep). */
inline float StopArriveRadius(bool playerCopy, bool stalled)
{
    return (playerCopy && !stalled) ? kStopArriveDist : kStopArriveWide;
}
/* Closing on the stop point, per drive frame while a stop is held: d = the copy's distance to it. A new nearest distance by
   at least kStopProgressMin restarts the watch; none for kStopStallSec = stalled (true). Reset when no stop is held or a new
   one is stated. A value that is not a number changes nothing and is not stalled. */
struct StopProgress { bool armed; float best; double bestAt; };
inline void StopProgressReset(StopProgress* s) { s->armed = false; s->best = 0.0f; s->bestAt = 0.0; }
inline bool StopProgressStep(StopProgress* s, float d, double now)
{
    if (!FpFiniteF(d) || !FpFinite(now)) return false;
    if (!s->armed || d < s->best - kStopProgressMin || now < s->bestAt) { s->armed = true; s->best = d; s->bestAt = now; return false; }
    return now - s->bestAt >= kStopStallSec;
}

/* The run raise for a follow copy: runboost.h's Decide with the follow thresholds (lag = e). */
inline int BoostDecide(bool enabled, bool boosted, bool running, bool dirOk, float lag, int idle, int enter, int keep, int restore)
{
    const bool ok = enabled && running && dirOk && FpFiniteF(lag);
    if (!ok) return boosted ? restore : idle;
    if (boosted) return lag < kBoostLeaveLag ? restore : keep;
    return lag > kBoostEnterLag ? enter : idle;
}

/* The slow-down for a copy AHEAD of its target (e < -kPaceDeadErr): 1 - kPaceMaxCut x clamp(-e / kPaceFullErr, 0, 1). */
inline float PaceFactor(float e)
{
    if (!FpFiniteF(e) || !(e < -kPaceDeadErr)) return 1.0f;
    float x = -e / kPaceFullErr;
    if (x > 1.0f) x = 1.0f;
    return 1.0f - kPaceMaxCut * x;
}
inline float PaceSlowSpeed(float base, float e) { return base * PaceFactor(e); }

/* THE OWNER'S STOP (sender). armed: a character this game owns and streams produced a moving report, whatever its faction
   or task. due: armed, and the engine's moving flag (1 / 0, -1 unreadable) reads 0 with the body still kStopStillFrames
   frames running; an unreadable flag falls back to kStopStillPasses still send passes running. */
inline bool OwnerStopArm(bool armed, int sample)
{
    if (sample == kSampleMoving) return true;
    return armed;
}
inline int StillFramesStep(int prev, float moved) { return (FpFiniteF(moved) && moved < kStopStillMove) ? prev + 1 : 0; }
inline int StillPassesStep(int prev, int sample) { return sample == kSampleMoving ? 0 : (sample == kSampleStill ? prev + 1 : prev); }
inline bool OwnerStopDue(bool armed, int engineMoving, int stillFrames, int stillPasses)
{
    if (!armed || engineMoving == 1) return false;
    if (engineMoving == 0) return stillFrames >= kStopStillFrames;
    return stillPasses >= kStopStillPasses;
}

/* (x, z) is more than r from (sx, sz). Not a number: not further. */
inline bool FarFrom(float x, float z, float sx, float sz, float r)
{
    const float dx = x - sx, dz = z - sz;
    const float d2 = dx * dx + dz * dz;
    return FpFiniteF(d2) && d2 > r * r;
}

/* THE OWNER'S STOP (sender), after a stop went out: the owner's velocity is the distance since its previous report, so the
   first report after the body stopped still reads moving while it sits on the stop point. A moving report within
   kStopSameDist of the stop point sent (sx, sz) is that trailing report and arms nothing; one further away is the
   character walking on. */
inline bool MovedOffSentStop(float x, float z, float sx, float sz)
{
    const float dx = x - sx, dz = z - sz;
    const float d2 = dx * dx + dz * dz;
    return !(FpFiniteF(d2) && d2 <= kStopSameDist * kStopSameDist);
}

/* THE OWNER'S STOP (copy), ordered by the owner's own clock. A MOVE stamped before the stop is a report from before it
   that arrived late: dropped. A moving MOVE stamped after it at (mx, mz) more than kStopSameDist from the stop point
   (sx, sz) is the owner walking again: the stop ends. A moving one closer than that is the stop's own trailing report (the
   owner's velocity is the distance since its previous report): dropped. A still one after it keeps the stop. */
inline int MoveVsStop(bool stopHeld, double stopT, double moveT, bool moving, float mx, float mz, float sx, float sz)
{
    if (!stopHeld) return kMoveApply;
    if (moveT < stopT) return kMoveStale;
    if (moving) return (moveT > stopT && FarFrom(mx, mz, sx, sz, kStopSameDist)) ? kMoveClearsStop : kMoveStale;
    return kMoveApply;
}
/* A stop that arrives after a moving report stamped later than itself is already over (the owner walked on) - unless that
   report lies within kStopSameDist of the stop point (sx, sz): then it is the stop's own trailing report. */
inline bool StopAlreadyOver(bool haveNewest, double newestT, bool newestStill, float nx, float nz, double stopT, float sx, float sz)
{
    return haveNewest && !newestStill && newestT > stopT && FarFrom(nx, nz, sx, sz, kStopSameDist);
}
/* A held stop the owner left without a moving report (a prone, crawling, ragdolled or carried body reports no velocity):
   an applied report stamped after the stop more than kStopLeaveDist from the stop point (sx, sz), or an owner teleport,
   ends it. */
inline bool StopLeftBy(bool stopHeld, double stopT, double moveT, float mx, float mz, float sx, float sz, bool jumped)
{
    if (!stopHeld) return false;
    if (jumped) return true;
    return moveT > stopT && FarFrom(mx, mz, sx, sz, kStopLeaveDist);
}

/* BEHIND SCHEDULE, per replay frame (no stop stated): the target (tx, tz) against the owner's newest reported point (ox, oz),
   for an owner reported at speed v (the newest moving report's; 0 = none) and the delay D. Enters when T is further from O
   than v x (D + kBehindPadSec) + kBehindPadDist; leaves at v x (D + kBehindPadSec / 2) + kBehindPadDist / 2 or nearer, so
   a gap on the line does not flip the copy's destination every frame. A speed or delay that is not a usable number counts as
   0 / the default delay; a position that is not a number keeps nothing behind (the drive stays as designed). */
inline bool BehindScheduleStep(bool behind, float tx, float tz, float ox, float oz, float v, double delaySec)
{
    const float dx = ox - tx, dz = oz - tz;
    const float d2 = dx * dx + dz * dz;
    if (!FpFiniteF(d2)) return false;
    const float u = (v > 0.0f && FpFiniteF(v)) ? v : 0.0f;
    const double d = (delaySec > 0.0 && FpFinite(delaySec)) ? delaySec : kDelayDefaultSec;
    const float lim = behind ? u * (float)(d + kBehindPadSec * 0.5) + kBehindPadDist * 0.5f
                             : u * (float)(d + kBehindPadSec) + kBehindPadDist;
    return d2 > lim * lim;
}

/* The owner's point a snap places a follow copy on and a copy behind schedule aims at: the stated stop point while a stop is
   held (the owner stands there), else the owner's newest reported point. */
inline void OwnerPoint(bool stopHeld, float sx, float sz, float ox, float oz, float* px, float* pz)
{
    if (stopHeld) { *px = sx; *pz = sz; }
    else { *px = ox; *pz = oz; }
}

/* AN IDLE COPY (the copy's drive, every frame, before the replay): its owner does not move (ownerMoving: a fresh moving
   report or a moving newest report), the copy is within arriveDist of the owner's point (dist) and walks no path, and the
   last moving report arrived more than the delay plus kReplaySettleSec ago (sinceMovingSec; never one = a large number).
   Such a copy takes only the arrival stop, which reads nothing the replay computes, so the replay is skipped: a world area's
   standing NPCs cost the follow drive nothing. A value that is not a number is not idle (the replay runs). */
inline bool ReplayIdle(bool ownerMoving, bool pathMode, float dist, float arriveDist, double sinceMovingSec, double delaySec)
{
    if (ownerMoving || pathMode) return false;
    if (!(FpFiniteF(dist) && dist <= arriveDist)) return false;
    return FpFinite(sinceMovingSec) && FpFinite(delaySec) && sinceMovingSec > delaySec + kReplaySettleSec;
}

}   /* namespace followplay */

#endif
