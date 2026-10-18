// townwake.h - T-936 (owner decision 936, 2026-10-17): a newly founded player outpost is woken at once, as the game wakes a town
// when its area loads, on every game. The engine's founding (TownFoundPlayer) makes the town asleep (Town +0x1AC = 0) and never
// wakes it; the mod queues each new town and wakes it at the next main-thread safe point with the town's own wake-up (its vt+0x228,
// called with the ZoneMap of the area it stands in).
// Pure (only <cmath>): build.cpp and the offline suite include this one header - the queue's bookkeeping, the one-look verdict and
// the check that the ZoneMap the mod located is the area's own (its +0x18 / +0x1C words name that area).
#ifndef TOWNWAKE_H
#define TOWNWAKE_H
#include <cmath>
namespace townwake {
const int kQueueCap = 16;            // towns waiting for their wake at one time (a founding past it is counted dropped)
const unsigned kGiveUpMs = 60000;    // a town not woken by then is dropped: the game wakes it itself when its area loads
const unsigned kLookGapMs = 250;     // the queue is looked at no more often than this
const float kSectorSize = 4608.0f;   // the engine's area grid (poskey.h: 64 x 64 areas of 4608 units)
const float kSectorOrigin = 147456.0f;
const int kGrid = 64;

// one queued town: its handle words (how the file identifies a town), the address the founding returned (only ever COMPARED with
// the player-town list, never read on its own), when it was queued, the world it was founded in, and whether it has waited for
// its area already (counted notLoaded once)
struct Entry { int hand[5]; unsigned long long ptr; unsigned queuedAt; long gen; int waited; };

inline bool HandKnown(const int* h) { for (int i = 0; i < 5; ++i) if (h[i] != 0) return true; return false; }
inline bool HandSame(const int* a, const int* b) { for (int i = 0; i < 5; ++i) if (a[i] != b[i]) return false; return true; }
// the same town: by its handle words when both are known, else by the address the founding returned
inline bool SameTown(const Entry& a, const Entry& b)
{
    if (HandKnown(a.hand) && HandKnown(b.hand)) return HandSame(a.hand, b.hand);
    return a.ptr != 0 && a.ptr == b.ptr;
}
enum { kPushFull = 0, kPushQueued = 1, kPushAgain = 2 };
// kPushQueued = added at the end; kPushAgain = the same town is queued already (nothing changed); kPushFull = no room, or a bad
// queue / count / cap (nothing changed)
inline int Push(Entry* q, int* n, int cap, const Entry& e)
{
    if (q == 0 || n == 0 || *n < 0 || cap <= 0) return kPushFull;
    for (int i = 0; i < *n && i < cap; ++i) if (SameTown(q[i], e)) return kPushAgain;
    if (*n >= cap) return kPushFull;
    q[*n] = e;
    ++*n;
    return kPushQueued;
}
// entry i taken out, the rest kept in order
inline void RemoveAt(Entry* q, int* n, int i)
{
    if (q == 0 || n == 0 || i < 0 || i >= *n) return;
    for (int k = i + 1; k < *n; ++k) q[k - 1] = q[k];
    --*n;
}
// the area (sx, sy) of a world position (x, z), as the engine's own sector arithmetic: UNCLAMPED; 1 = inside the 64 x 64 grid
inline int SectorOfXZ(float x, float z, int* sx, int* sy)
{
    if (!(x > -1e9f && x < 1e9f) || !(z > -1e9f && z < 1e9f)) return 0;   // a garbage read never becomes an area
    *sx = (int)std::floor((kSectorOrigin + x) / kSectorSize);
    *sy = (int)std::floor((kSectorOrigin + z) / kSectorSize);
    return (*sx >= 0 && *sx < kGrid && *sy >= 0 && *sy < kGrid) ? 1 : 0;
}
// 1 = the ZoneMap the mod located for area (sx, sy) names that area in its own +0x18 / +0x1C words (read = 1); anything else
// refuses the wake, so a wrong layout never reaches the engine
inline int AreaRecordMatches(int read, int sx, int sy, int rx, int ry) { return (read == 1 && rx == sx && ry == sy) ? 1 : 0; }

enum
{
    kWake = 1,          // call the town's wake-up now
    kAlready,           // awake already (+0x1AC != 0): not called, taken out
    kWaitTown,          // not in the player-town list yet (or unread): kept
    kWaitArea,          // its area is not loaded here: kept
    kDropTimeout,       // waited kGiveUpMs: taken out (the game wakes it on an area load)
    kDropWorld,         // founded in another world (a load came between): taken out
    kAreaMismatch,      // the located ZoneMap does not name the town's area: refused, taken out
    kNoArea             // its position is off the grid or unread: taken out
};
// one look at one queued town. found: 1 = resolved in the player-town list; posOk: its position read and inside the grid;
// active: its +0x1AC; loaded: ZoneLoadedSectorTri of its area (1 loaded, 0 not, -1 unreadable - waited on as not loaded);
// areaOk: AreaRecordMatches; age: ms since queued; sameWorld: the world it was founded in is this one.
// The area record is only judged once the area is loaded (that is when the wake would be called).
inline int Verdict(int found, int posOk, int active, int loaded, int areaOk, unsigned age, int sameWorld)
{
    if (sameWorld == 0) return kDropWorld;
    if (found == 0) return age >= kGiveUpMs ? kDropTimeout : kWaitTown;
    if (active != 0) return kAlready;
    if (posOk == 0) return kNoArea;
    if (loaded != 1) return age >= kGiveUpMs ? kDropTimeout : kWaitArea;
    if (areaOk == 0) return kAreaMismatch;
    return kWake;
}
// 1 = the verdict takes the entry out of the queue
inline int TakesOut(int v) { return (v == kWaitTown || v == kWaitArea) ? 0 : 1; }
}   // namespace townwake
#endif
