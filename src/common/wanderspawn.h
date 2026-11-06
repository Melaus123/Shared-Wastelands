/* wanderspawn.h - T-1235: the [SPAWN] wander line's pure parts - how far a new wandering group is from every player's characters,
   and whether the line prints - and THE KEEP-AWAY decision (below: SpotVerdict, owner decision 1478 b). The SAME header towngen.cpp and the offline suite compile. C++03, no engine types.

   THE QUESTION (T-1235, .modding/investigations/spawn-near-players-2026-11-04.md). The engine keeps a new wandering group at least
   1382.4 units from THIS game's camera centre (decomp_8f4200.txt:67-73) and from nothing else; in this mod only the game holding an
   area makes the wandering groups there (towngen.cpp detour_create). The line says, for each wandering group this game makes, how near
   it is to the characters of every player - this game's own and each other player's copies here.

   THE CAP: a group whose nearest player character is within kNearDist always prints, at most kPrintCap lines a session (the rest are
   counted); a group farther away, or with no player character known, is only counted. */
#pragma once
#include <cmath>

namespace wanderspawn {

const float kNearDist = 3000.0f;   /* a group nearer than this to any player character prints */
const long kPrintCap = 200;        /* printed lines per game process; the rest are counted */
const int kMaxPcs = 512;           /* player characters the main thread's list keeps (every other player's copies FIRST, then this
                                      game's own - so a large own roster cannot push the other players out; overflow is counted) */
const int kMaxSlots = 8;           /* distinct players one line names */

/* one player character: where it stands on the ground and whose it is (slot = the player's slot; own = this game's own; dead = it
   has died - T-1235's keep-away ignores it) */
struct Pc { float x, z; int slot; int own; int dead; Pc() : x(0.0f), z(0.0f), slot(-1), own(0), dead(0) {} };

/* the distance on the ground (x, z), the measure the line uses for characters */
inline float GroundDist(float ax, float az, float bx, float bz)
{
    const float dx = ax - bx, dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}
/* the engine's own camera measure (0x8F4200: Ogre squaredDistance, 3-D) */
inline float Dist3(const float* a, const float* b)
{
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/* For a group at (x, z): the nearest character of each player, one row per distinct (slot, own) pair in first-seen order, at most
   `cap` rows (more players are left out and counted in *leftOut). *nearest = the smallest distance of all (-1 when there is no
   character); *nearestRow = its row (-1 none). Returns the number of rows. A character whose distance is not finite is skipped. */
inline int NearestByPlayer(const Pc* pcs, int n, float x, float z, int* slots, int* owns, float* dists, int cap,
                           float* nearest, int* nearestRow, int* leftOut)
{
    int rows = 0, lost = 0;
    float best = -1.0f; int bestRow = -1;
    for (int i = 0; i < n; ++i)
    {
        const float d = GroundDist(pcs[i].x, pcs[i].z, x, z);
        if (!(d >= 0.0f && d < 1.0e30f)) continue;   /* NaN / infinity */
        int r = -1;
        for (int k = 0; k < rows; ++k) if (slots[k] == pcs[i].slot && owns[k] == pcs[i].own) { r = k; break; }
        if (r < 0)
        {
            if (rows >= cap) { ++lost; continue; }
            r = rows++; slots[r] = pcs[i].slot; owns[r] = pcs[i].own; dists[r] = d;
        }
        else if (d < dists[r]) dists[r] = d;
    }
    for (int k = 0; k < rows; ++k) if (best < 0.0f || dists[k] < best) { best = dists[k]; bestRow = k; }
    if (nearest != 0) *nearest = best;
    if (nearestRow != 0) *nearestRow = bestRow;
    if (leftOut != 0) *leftOut = lost;
    return rows;
}

/* what happens to one group's line: printed, counted as near but over the cap, counted as far, or counted as having no player
   character to measure from. `printedBefore` = lines this process already printed. */
enum { kPrint = 1, kNearOverCap = 2, kFar = 3, kNoCharacter = 4 };
inline int Decide(float nearest, long printedBefore)
{
    if (!(nearest >= 0.0f)) return kNoCharacter;
    if (!(nearest < kNearDist)) return kFar;
    return printedBefore < kPrintCap ? kPrint : kNearOverCap;
}

/* is the creation a WANDERING group (ZoneManager 0x8F7190's call into createRandomUnloadedSquad, decomp_8f7190.txt:222-223)? Its
   arguments: no building (param 6 = 0) and the spawn region's record as param 9 (param_2+0x28, never 0 there). Every other caller
   passes param 9 = 0: the nest passes (decomp_8f7870 / 8f7a90:248-262), 0x9C3280 (decomp_9c3280.txt:82) - and populateBuilding
   passes a building. */
inline int IsWanderCreation(int hasBuilding, int hasParam9) { return (!hasBuilding && hasParam9) ? 1 : 0; }

/* T-1235 THE KEEP-AWAY (owner decision 1478 b, 2026-11-04). The game holding an area keeps every NEW wandering group at least kKeepDist
   from every live player character - this game's own player-faction characters and every other player's characters as this game sees
   them (their copies) - and out of every other player's base. A spot that fails is refused in the creation hook (towngen.cpp
   detour_create), the way the no-map arm refuses.
   WHY 2-D (x, z). The engine meant 1382.4 on the ground: 0x8F4200 keeps a draft spot 1382.4 (0x172F4F0) from its view point, but it
   measures in 3-D to a spot still at the "no ground" placeholder height -99 (decomp_8f4200.txt:48-71; the real height is filled in
   only after the spot is accepted, :124) - so on ground higher than ~1283 its circle shrinks to nothing and the push-out never fires
   (.modding/investigations/t1235-pushout-design-2026-11-04.md). Measuring on the ground plane gives the circle the engine meant, on
   any ground height.
   THE OTHER PLAYERS' BASES. The spawner refuses a spot inside the nearest PLAYER-FACTION town (decomp_8f7190.txt:214-215: 0x926F70
   takes only a town whose faction's +0x250 PlayerInterface is set, then 0x925E80 at scale 1.0 - .rdata 0x167B308): the ground distance
   from the town's centre (+0x48, +0x50) under its radius (vt+0x2A0), never for a dead town (+0xD8 == 8). Another player's base on
   this game is a town of that player's STAND-IN faction, which holds no PlayerInterface - so the engine leaves it unprotected here.
   The same test is made for each such base (InBase). The engine asks only the NEAREST player town within its range; asking every base
   can only refuse more, and only inside a base.
   THE SNAPSHOT. The hook runs off the main thread (ZoneManager's threaded spawn check, caller 0x8F85B6). It reads only the snapshot the
   main thread fills every frame (towngen.cpp WanderSnapFill, under g_tgLock). A CHARACTER snapshot that is not usable - never filled
   since the game started, older than kStaleMs, or holding no live player character at all (right after a load, before any is placed) -
   REFUSES the spot: a wandering group not made now is asked for again by the spawner's next round, while one made blind can stand on a
   player. The BASE list (refreshed once a second) is judged apart: when it is not usable (never read, or older than kStaleMs) only the
   base check is skipped (counted) - the characters still decide, so a town list that stops reading cannot stop every wandering group. */
const float kKeepDist = 1382.4f;          /* 0x172F4F0 - the engine's own keep-away distance */
const unsigned long kStaleMs = 2000;      /* a snapshot older than this is not used */
const int kMaxBases = 64;                 /* other players' bases the snapshot keeps */
const long kRefusePrintCap = 200;         /* printed refusal lines per game process; the rest are counted */

/* one other player's base on this game: its town's centre on the ground, its radius (vt+0x2A0) and its owner's slot */
struct Base { float x, z, r; int slot; Base() : x(0.0f), z(0.0f), r(0.0f), slot(-1) {} };

/* 0x925E80 at scale 1.0: the ground distance from the centre under the radius (a radius not above 0, or not finite, holds nothing) */
inline int InBase(const Base& b, float x, float z)
{
    if (!(b.r > 0.0f && b.r < 1.0e30f)) return 0;
    const float dx = b.x - x, dz = b.z - z;
    return (dx * dx + dz * dz < b.r * b.r) ? 1 : 0;
}

/* the live characters in the snapshot */
inline int LiveCount(const Pc* pcs, int n)
{
    int k = 0;
    for (int i = 0; i < n; ++i) if (pcs[i].dead == 0) ++k;
    return k;
}

/* why a character snapshot is not usable: 0 usable; kStaleNever never filled since the game started; kStaleOld older than kStaleMs;
   kStaleNoLive no live player character in it */
enum { kStaleNever = 1, kStaleOld = 2, kStaleNoLive = 3 };
inline int StaleWhy(int filled, unsigned long ageMs, int liveCount)
{
    if (filled == 0) return kStaleNever;
    if (ageMs > kStaleMs) return kStaleOld;
    if (liveCount <= 0) return kStaleNoLive;
    return 0;
}
/* is the snapshot usable: filled since the game started, at most kStaleMs old, and holding at least one live player character */
inline int SnapUsable(int filled, unsigned long ageMs, int liveCount) { return StaleWhy(filled, ageMs, liveCount) == 0 ? 1 : 0; }
/* is the base list usable: read at least once, at most kStaleMs old */
inline int BasesUsable(int readOnce, unsigned long ageMs) { return (readOnce != 0 && ageMs <= kStaleMs) ? 1 : 0; }

/* THE DECISION for a new wandering group at (x, z). kAllow: made. kRefuseNear: a live player character within kKeepDist on the ground
   (*idx = that character's index in pcs, the nearest one). kRefuseBase: inside another player's base (*idx = the base's index).
   kRefuseStale: the character snapshot is not usable (SnapUsable; ageMs is the CHARACTERS' age). *nearest = the ground distance to the
   nearest LIVE character (-1 none, or stale). A dead character never refuses; a character whose distance is not finite is skipped.
   Order: stale, characters, bases. basesUsable 0 (BasesUsable): the base check is skipped and *baseSkipped set (may be 0). */
enum { kAllow = 0, kRefuseNear = 1, kRefuseBase = 2, kRefuseStale = 3 };
inline int SpotVerdictEx(const Pc* pcs, int n, const Base* bases, int nb, int basesUsable, int filled, unsigned long ageMs, float x, float z,
                         float* nearest, int* idx, int* baseSkipped)
{
    float best = -1.0f; int bi = -1;
    if (nearest != 0) *nearest = -1.0f;
    if (idx != 0) *idx = -1;
    if (baseSkipped != 0) *baseSkipped = 0;
    if (SnapUsable(filled, ageMs, LiveCount(pcs, n)) == 0) return kRefuseStale;
    for (int i = 0; i < n; ++i)
    {
        if (pcs[i].dead != 0) continue;
        const float d = GroundDist(pcs[i].x, pcs[i].z, x, z);
        if (!(d >= 0.0f && d < 1.0e30f)) continue;
        if (best < 0.0f || d < best) { best = d; bi = i; }
    }
    if (nearest != 0) *nearest = best;
    if (bi >= 0 && best < kKeepDist) { if (idx != 0) *idx = bi; return kRefuseNear; }
    if (basesUsable == 0) { if (baseSkipped != 0) *baseSkipped = 1; return kAllow; }
    for (int k = 0; k < nb; ++k) if (InBase(bases[k], x, z) != 0) { if (idx != 0) *idx = k; return kRefuseBase; }
    return kAllow;
}
/* the same with a usable base list */
inline int SpotVerdict(const Pc* pcs, int n, const Base* bases, int nb, int filled, unsigned long ageMs, float x, float z,
                       float* nearest, int* idx)
{
    return SpotVerdictEx(pcs, n, bases, nb, 1, filled, ageMs, x, z, nearest, idx, 0);
}
/* the stale-snapshot run's lines (towngen.cpp WanderStaleEdge, main thread): a run STARTS at the first stale-snapshot refusal and STOPS
   when none came for kStaleQuietMs; at most kStaleLineCap lines a process. Returns 1 start, 2 stop, 0 nothing; updates the state. */
const unsigned long kStaleQuietMs = 10000;
const int kStaleLineCap = 8;
struct StaleRun { long long seen; int inRun; unsigned long lastAt, startAt; long long startN; StaleRun() : seen(0), inRun(0), lastAt(0), startAt(0), startN(0) {} };
inline int StaleEdge(StaleRun* r, long long count, unsigned long now)
{
    if (count != r->seen)
    {
        int ev = 0;
        if (r->inRun == 0) { r->inRun = 1; r->startAt = now; r->startN = r->seen; ev = 1; }
        r->lastAt = now; r->seen = count;
        return ev;
    }
    if (r->inRun != 0 && now - r->lastAt >= kStaleQuietMs) { r->inRun = 0; return 2; }
    return 0;
}

}   /* namespace wanderspawn */
