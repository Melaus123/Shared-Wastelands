#ifndef SW_COMMON_TWINSLEEP_H
#define SW_COMMON_TWINSLEEP_H
#include <math.h>   /* sqrtf: StripPoint's direction */
/* A SAVE-BORN TWIN THAT NO GAME DRIVES HERE. Pure: no engine, no network - the offline suite tests every decision here.

   Every game that loaded the same save holds its own body of each of the save's people (a twin). Hits are settled by the game on
   the owner record, so a twin body awake on a game that does not run it and that is not the puppet of the game that does can be
   hurt by nobody. A twin that is not a player or player-faction character is therefore awake on a game only while that game runs
   it or while it is the puppet of its runner. When a withdrawal ends the puppet (spawn.cpp ApplyRemoteUnload) it is queued. A
   member nobody runs is taken at once where this game's engine keeps its squad's spot (WithdrawBranch). Otherwise the queued
   members of one squad share ONE timer - started when the first of them was queued (SquadAgeSec) - and go together, by the
   withdrawal kinds among them (SquadMode):
   - kKindRunnerRuns (an UNLOAD whose UnloadClearsGiven is 0; the return check's NOT ANNOUNCED): its runner still runs it. The squad
     is put to sleep at the runner's last streamed spot when this game's engine does NOT keep that spot; otherwise it waits, AI off,
     for the runner's SPAWN (the runner's announce pass re-sends it once it reads this game's area as held) until the shared timer
     reaches kAwaitSec. Then it is put to sleep just outside this game's kept area on the line from the squad toward the runner's
     last spot (StripPoint) - or, with no spot known, just outside the kept area away from this game's player (StripPointAround
     with no player);
   - kKindNobodyRuns (an UNLOAD whose UnloadClearsGiven is 1; the roster's withdrawals; a member a release dropped): the squad is put
     to sleep where it stands; still awake when the timer reaches kAwaitSec: taken (owner record and AI together);
   - kKindNeverTake (a REVOKE that names no winner: the giver keeps the squad asleep in its world data, so a body awake or woken here
     would later stand beside the one that world data wakes): put to sleep where it stands when this engine does NOT keep that spot,
     else (or still awake at kAwaitSec) just outside the kept area away from this game's player.
   A body that is not a twin, and a player or player-faction twin, keep the withdrawal unchanged.

   The sleep waits at a gate, re-checked every frame: no member this game runs, no member still a puppet, no member carried (an
   unreadable member waits too). A member in a cage, bed, seat or building slot does not hold it: the engine's own squad sleep
   (Platoon::deactivate) only notes such a member. A squad still awake kRecheckSec after its sleep call is called once more, then
   given up (the late roads above, at once). The strip sleep is called once; its gate may stay shut kStripGateSec. No body ends
   frozen (owner rule 750): a strip sleep whose gate stays shut, whose point cannot be found or written, or that is refused or does
   not take ends in this game's take with an Error (spawn.cpp TwinLastResortTake), as does a member whose squad cannot be read
   (NoSquadDecide). Every road ends within about kBoundSec of the first queuing. */

namespace twinsleep {

enum { kKindRunnerRuns = 0, kKindNobodyRuns = 1, kKindNeverTake = 2 };
enum { kUnchanged = 0, kSleep = 1, kTake = 2, kAwaitRunner = 3 };

/* One member's own branch at its withdrawal and each frame. isTwin, isPlayer: 1 / 0. kind: kKind* (any other value reads as
   kKindNobodyRuns). keepsHere: this game's engine keeps the spot the branch is judged at - kKindRunnerRuns: the runner's last
   streamed spot; kKindNobodyRuns: the squad's own - 1 yes, 0 no, -1 unreadable or no spot known (never taken: the hand-over's take
   test refuses an unreadable spot). kTake is the one answer acted on alone; the squad's road is SquadMode's. */
inline int WithdrawBranch(int isTwin, int isPlayer, int kind, int keepsHere)
{
    if (isTwin == 0 || isPlayer != 0) return kUnchanged;
    if (kind == kKindNeverTake) return kSleep;
    if (kind == kKindRunnerRuns) return keepsHere == 0 ? kSleep : kAwaitRunner;
    return keepsHere == 1 ? kTake : kSleep;
}

const double kAwaitSec = 30.0;      /* the squad's shared timer: its first roads, and the wait for the runner's SPAWN */
const double kRecheckSec = 10.0;    /* a squad still awake this long after its sleep call is called again or given up */
const double kStripGateSec = 30.0;  /* the strip sleep's gate may stay shut this long, then the last resort */
const double kBoundSec = 70.0;      /* kAwaitSec + kStripGateSec + kRecheckSec: every queued uid is asleep, back or taken by then */
const int kMaxCalls = 2;            /* the first call and one more */

/* The squad's shared timer: the age in seconds of the OLDEST of its n queued members' queue stamps (GetTickCount values; the
   subtraction wraps as the tick count does). n <= 0: 0. */
inline double SquadAgeSec(const unsigned long* queuedAt, int n, unsigned long now)
{
    double best = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = (double)(unsigned long)(now - queuedAt[i]) / 1000.0;
        if (a > best) best = a;
    }
    return best;
}

enum { kModeHere = 0, kModeRunnerSpot = 1, kModeWait = 2, kModeStrip = 3, kModeTake = 4 };

/* The squad's road. anyRunner / anyNever: a queued member of that kind (1 / 0). late: the shared timer reached kAwaitSec, or its
   sleep calls were given up. hasSpot: its runner's last streamed spot is known. keepsSpot / keepsBody: this game's engine keeps the
   runner's spot / the squad's own - 1, 0, -1 unreadable. */
inline int SquadMode(int anyRunner, int anyNever, int late, int hasSpot, int keepsSpot, int keepsBody)
{
    if (anyRunner != 0)
    {
        if (late == 0) return (hasSpot != 0 && keepsSpot == 0) ? kModeRunnerSpot : kModeWait;
        return kModeStrip;   /* late: always the strip road, which is bounded (its gate wait, then the last resort) */
    }
    if (anyNever != 0) return (late == 0 && keepsBody == 0) ? kModeHere : kModeStrip;
    return late == 0 ? kModeHere : kModeTake;
}

enum { kGateGo = 0, kGateWait = 1, kGateDone = 2 };

/* awake: the squad is awake here - 1 yes, 0 no (nothing to put to sleep), -1 unreadable. anyMine / anyPuppet / anyCarried:
   1 yes, 0 no, -1 unreadable. Anything but a clear "no" waits for the next check. */
inline int SleepGate(int awake, int anyMine, int anyPuppet, int anyCarried)
{
    if (awake == 0) return kGateDone;
    if (awake != 1) return kGateWait;
    if (anyMine != 0 || anyPuppet != 0 || anyCarried != 0) return kGateWait;
    return kGateGo;
}

enum { kSgCall = 0, kSgWait = 1, kSgLastResort = 2 };

/* The strip sleep at its gate. gate: SleepGate's answer for the squad. shutSec: how long its gate has been shut on the strip sleep
   (0 the first time). A shut gate waits kStripGateSec, never less, then the last resort. */
inline int StripGate(int gate, double shutSec)
{
    if (gate == kGateGo) return kSgCall;
    return shutSec < kStripGateSec ? kSgWait : kSgLastResort;
}

enum { kCallAwait = 0, kCallAgain = 1, kCallGiveUp = 2 };

/* A squad still awake `sinceSec` after the last of its `calls` sleep calls (calls >= 1). */
inline int AfterCall(double sinceSec, int calls)
{
    if (sinceSec < kRecheckSec) return kCallAwait;
    return calls < kMaxCalls ? kCallAgain : kCallGiveUp;
}

enum { kNsWait = 0, kNsTake = 1, kNsLastResort = 2 };

/* A queued uid whose squad cannot be read (a faulted read, or no squad): nothing can be put to sleep. Nobody runs it: taken once
   `sinceSec` (since it was queued) reaches kAwaitSec. Otherwise it waits kAwaitSec + kStripGateSec, then the last resort. */
inline int NoSquadDecide(int kind, double sinceSec)
{
    if (kind == kKindNobodyRuns) return sinceSec < kAwaitSec ? kNsWait : kNsTake;
    return sinceSec < kAwaitSec + kStripGateSec ? kNsWait : kNsLastResort;
}

const float kStripStep = 64.0f;   /* units between the points tried */
const int kStripMaxSteps = 160;   /* 10240 units: a little over two areas (4608 each) */

/* The keep question StripPoint asks at each point: 1 = this game's engine keeps (x, z), 0 = not, -1 = unreadable. */
typedef int (*KeepAtFn)(void* ctx, float x, float z);

/* The nearest point outside this game's kept area on the line from (bx, bz) toward (tx, tz) - the line goes on past it - tried
   every kStripStep units, kStripMaxSteps at most. 1 = found (*ox, *oz); 0 = none within the bound, no direction (the two points
   within 1 unit), or an unreadable keep on the way (never read as "outside"). */
inline int StripPoint(float bx, float bz, float tx, float tz, KeepAtFn keeps, void* ctx, float* ox, float* oz)
{
    float dx = tx - bx, dz = tz - bz;
    const float len = sqrtf(dx * dx + dz * dz);
    if (keeps == 0 || ox == 0 || oz == 0 || !(len > 1.0f)) return 0;
    dx /= len; dz /= len;
    for (int i = 1; i <= kStripMaxSteps; ++i)
    {
        const float x = bx + dx * kStripStep * (float)i, z = bz + dz * kStripStep * (float)i;
        const int k = keeps(ctx, x, z);
        if (k < 0) return 0;
        if (k == 0) { *ox = x; *oz = z; return 1; }
    }
    return 0;
}

/* With no direction to go by: the nearest of the points StripPoint finds along +x, -x, +z and -z from (bx, bz). 1 = found. */
inline int StripPointAround(float bx, float bz, KeepAtFn keeps, void* ctx, float* ox, float* oz)
{
    const float ax[4] = { 1.0f, -1.0f, 0.0f, 0.0f };
    const float az[4] = { 0.0f, 0.0f, 1.0f, -1.0f };
    int found = 0; float best = 0.0f;
    if (ox == 0 || oz == 0) return 0;
    for (int d = 0; d < 4; ++d)
    {
        float x = 0.0f, z = 0.0f;
        if (StripPoint(bx, bz, bx + ax[d] * 1000.0f, bz + az[d] * 1000.0f, keeps, ctx, &x, &z) != 1) continue;
        const float dd = (x - bx) * (x - bx) + (z - bz) * (z - bz);
        if (found == 0 || dd < best) { found = 1; best = dd; *ox = x; *oz = z; }
    }
    return found;
}

}   /* namespace twinsleep */

#endif
