// PROBE-START: P124 - T-416: does a copy of a non-player character run past the point where its owner's character stopped.
// Pure (only <cmath>): the plugin's P124 block and the offline suite include this one header.
// The owner's reports for one character are read as a trip: a report at >= 2 u/s is moving, one under 0.5 u/s is still,
// anything between neither. The first still report after a moving one is the stop; a moving report before the stop's
// measure ends is a resume (the stop is not measured). The stop's measure uses the copy's positions sampled since the
// owner's last moving report: the heading is that report's velocity (or, when it is zero, the line from its position to the
// still position); the overshoot is the copy's farthest reach past the still position along that heading (0 when it never
// passed it); back is how far it then came back from that reach by the last sample; a backtrack is a reach over 0.5 u
// followed by a return of at least 1 u (the P115 rule); endDist is the last sample's distance to the still position.
#ifndef P124STOP_H
#define P124STOP_H
#include <cmath>
namespace p124 {
const float kMovingSpd = 2.0f;   // u/s
const float kStillSpd  = 0.5f;   // u/s
const float kOverCount = 2.0f;   // an overshoot above this is counted

enum { kPhaseIdle = 0, kPhaseMoving = 1, kPhaseStopped = 2 };
enum { kRepNone = 0, kRepMoving = 1, kRepStop = 2, kRepResumed = 3 };
// One owner report of speed `spd`: moves *phase and says what the report means for the measure.
// kRepMoving: a moving report (the caller restarts the copy's samples and keeps this report as the last moving one);
// kRepStop: the stop (the caller keeps the still position and starts the 3 s measure);
// kRepResumed: a moving report while a stop's measure was open (the stop is dropped, then treated as kRepMoving).
inline int OnReport(int* phase, float spd)
{
    if (!(spd == spd)) return kRepNone;
    if (spd >= kMovingSpd)
    {
        const int was = *phase;
        *phase = kPhaseMoving;
        return was == kPhaseStopped ? kRepResumed : kRepMoving;
    }
    if (spd < kStillSpd && *phase == kPhaseMoving) { *phase = kPhaseStopped; return kRepStop; }
    return kRepNone;
}

struct Pt { float x, z; };
struct Measure { int headingOk; int farIndex; float farAlong; float overshoot; float back; int backtrack; float endDist; };
inline Measure MeasureStop(const Pt* s, int n, float lastX, float lastZ, float lastVx, float lastVz, float stillX, float stillZ)
{
    Measure m; m.headingOk = 0; m.farIndex = -1; m.farAlong = 0.0f; m.overshoot = 0.0f; m.back = 0.0f; m.backtrack = 0; m.endDist = -1.0f;
    if (s == 0 || n <= 0) return m;
    const float ex = s[n - 1].x - stillX, ez = s[n - 1].z - stillZ;
    m.endDist = std::sqrt(ex * ex + ez * ez);
    float ux = lastVx, uz = lastVz;
    float len = std::sqrt(ux * ux + uz * uz);
    if (!(len > 0.001f)) { ux = stillX - lastX; uz = stillZ - lastZ; len = std::sqrt(ux * ux + uz * uz); }
    if (!(len > 0.001f)) return m;
    ux /= len; uz /= len;
    m.headingOk = 1;
    float maxAlong = -1.0e30f;
    for (int i = 0; i < n; ++i)
    {
        const float a = (s[i].x - stillX) * ux + (s[i].z - stillZ) * uz;
        if (a > maxAlong) { maxAlong = a; m.farIndex = i; }
    }
    const float endAlong = ex * ux + ez * uz;
    m.farAlong = maxAlong;
    m.overshoot = maxAlong > 0.0f ? maxAlong : 0.0f;
    m.back = maxAlong - endAlong;
    m.backtrack = (maxAlong > 0.5f && m.back >= 1.0f) ? 1 : 0;
    return m;
}
inline bool CountsAsOvershoot(float overshoot) { return overshoot > kOverCount; }
}
#endif
// PROBE-END: P124
