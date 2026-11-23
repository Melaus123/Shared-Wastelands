/* src/common/nudgefloor.h (T-1422) - THE FIGHT WINDOW'S SIDEWAYS NUDGE KEEPS A COPY ON
 * ITS OWN FLOOR.
 *
 * During a fight window the copy is nudged sideways toward its owner's last report every frame through the movement's set-position
 * (0x65DDE0, build/decomp_65dde0.txt). That call re-places the physics body through 0x145E10(body, pos, key -1), which ends in the
 * walking map's re-placement 0x3A1F70(map, body +0x20, body +0x14, 500.0): a search along a vertical segment of +-500 body units
 * (+-5000 game units) that can land the body on another floor (F1945, F2013). The engine's own per-frame walk (0x147F90) re-places a
 * body with the SAME function but a reach of 4.0 (+-40 game units, 0x1484F4 / 0x1488AB) and the body's own key, so its characters
 * do not change floor (F2015; the tower's floors are ~55 apart, T3026).
 *
 * THE PLACEMENT: every placement of a nudged copy's body is the engine's own SHORT re-placement - the
 * writes 0x145E10 makes (body +0x20 = the point x 0.1 with w 0, +0x14 = the key, then 0x3A1F70 with a short reach; then the mode
 * bookkeeping) - at the setter's own x / z. 0x3A1F70 changes only the height and the key (decomp_3a1f70:142-143), so the x / z stay
 * exactly the nudge's. The floor key is a per-game walking-map face id and cannot be shared; the floor's HEIGHT can, and every MOVE
 * carries the owner's height. So, in order:
 *   ownerFloor     the setter's x / z at the OWNER's height, reach kOwnerReach (+-20 game units), the body's key from before;
 *                  found -> placed on the floor its owner stands on.
 *   fallback       not found there: the setter's x / z at the body's own height from before, the walk's reach (+-40) and key; found
 *                  within kFallbackMaxDy of the body's height -> placed (another piece of the same floor, or a stair).
 *   keyLostKeepXZ  not found (or found too far, FallbackLeftFloor) and the body was already on no piece (key -1) before the step: the
 *                  setter's x / z at the body's own height, key -1 - what the engine's walk does on a miss (decomp_147f90:614-615).
 *   missStay       not found (or found too far) with a key: the copy stays where it was this frame - its body put back at its own
 *                  spot with its own key by the short placement; when that is not found either, every body field the nudge touched
 *                  is written back as it was (raw). The engine's own step stops at an opening's edge the same way.
 *   mode 2   (body +0x88 == 2: the body follows a computed walking path, 0x147F90:184-301, 556-660) - 0x145E10 would set +0x90 = 6,
 *            which makes the next walk frame re-run the +-500 search (0x147F90:107-108, 0x14804C). No placement here asks for it:
 *            +0x90 is written back to its value from before (the setter's own 0x145E10 call set it to 6). Else (any other mode)
 *            +0x40..+0x4C = +0x20..+0x2C as 0x145E10 does. +0x80 = 1 on every placement (the walk then re-places with its own
 *            reach, 0x147F90:610-615).
 * The movement position (+0xC4) is the setter's, unchanged, on every road (sideways unchanged); only missStay keeps the
 * body's own x / z for that frame.
 * A teleport of a copy (teleportTo(pos, -1): 0x65DEB0 -> 0x65DDE0 -> 0x145E10, the +-500 search) is re-placed the same way right
 * after it (RefloorTeleport): the teleport's x / z at the owner's height, +-20, the teleport's key; not found: the engine's result stays.
 *
 * ONE RULE, followplay.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a header,
 * so the offline suite drives it directly. replicate.cpp (NudgeFloorSet, in the reconcile nudge) makes the game calls.
 */
#ifndef NUDGEFLOOR_H_INCLUDED
#define NUDGEFLOOR_H_INCLUDED

namespace nudgefloor {

/* "another floor": over kSameFloorDy off (fix 3's kWinFloorDy; the engine's ground ray starts 20 above the feet) */
const float kSameFloorDy = 20.0f;
/* the check after the nudge: the body ended over kSameFloorDy off its height before, or over kBigBodyMove off it */
const float kBigBodyMove = 50.0f;
/* the body keeps its position x 0.1 (decomp_145e10:43-45: (float)((double)v * DAT_141689318), 0.1 as a double) */
const double kBodyScale = 0.1;
inline float ToBody(float v) { return (float)((double)v * kBodyScale); }
/* the walk's reach as read from the exe (row NavWalkPlaceReach, 4.0 in 1.0.65 and 1.0.68) is used only when it is a number in
   (0, kReachMax]: 5.0 body units = +-50 game units, under the ~55 between the tower's floors (T3026) */
const float kReachMax = 5.0f;

inline bool NfFinite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
inline float NfAbs(float v) { return v < 0.0f ? -v : v; }
inline bool ReachUsable(float r) { return NfFinite(r) && r > 0.0f && r <= kReachMax; }
/* the owner-height placement's reach in body units: 2.0 = +-20 game units, well under the ~55 between the tower's floors (T3026) */
const float kOwnerReach = 2.0f;
/* the fallback's limit in body units: a floor found more than 20 game units above / below the body's own height is another floor */
const float kFallbackMaxDy = 2.0f;

/* RULE 2 (the one decision where the nudge's sideways movement can differ from the engine's set-position): the fallback found a
   floor more than kFallbackMaxDy (20 game units) above / below the body's own height from before -> it counts as a miss, so the body
   stays (or, with no key, keeps x / z at its own height). The engine's own walk step stops at an opening's edge instead of dropping
   over it (decomp_147f90:416-421). Body units in; not a number counts as left. */
inline bool FallbackLeftFloor(float foundY, float bodyY0)
{
    const float d = foundY - bodyY0;
    return !NfFinite(d) || NfAbs(d) > kFallbackMaxDy;
}
/* RULE 1: a body already on no piece (key -1) before the step keeps the setter's x / z at its own height on a miss */
inline bool KeyLost(int key0) { return key0 == -1; }

/* the road: the short placement, or main's plain call alone (no body: the setter places nothing; no rows / reach: the fix cannot
   run; no walking map: the pointer is 0) */
enum { kRoadPlace = 0, kPlainNoBody = 1, kPlainNoRow = 2, kPlainNoMap = 3, kRoadCount = 4 };
inline int Road(bool haveBody, bool rowsOk, bool reachOk, bool mapOk)
{
    if (!haveBody) return kPlainNoBody;
    if (!rowsOk || !reachOk) return kPlainNoRow;
    if (!mapOk) return kPlainNoMap;
    return kRoadPlace;
}
inline const char* RoadWord(int r)
{
    switch (r)
    {
    case kRoadPlace: return "place";
    case kPlainNoBody: return "noBody";
    case kPlainNoRow: return "noRow";
    case kPlainNoMap: return "noMap";
    default: return "?";
    }
}

/* 0x3A1F70's answer (low 32 bits): 1 found (height and key written), 0 nothing found within reach (the height not written; the
   key set to -1 only when the key passed was not a valid piece number, decomp_3a1f70:93-100), 0xFFFFFFFF the map's lock was not
   taken (decomp_3a1f70:89-90, nothing written); anything else is treated as not found. kPlaceFault: the
   call or a body access faulted (replicate.cpp). */
enum { kPlaceFound = 0, kPlaceMiss = 1, kPlaceLocked = 2, kPlaceFault = 3, kPlaceNone = 4 };
inline int PlaceResult(unsigned int r) { return r == 1u ? kPlaceFound : (r == 0xFFFFFFFFu ? kPlaceLocked : kPlaceMiss); }

/* the mode bookkeeping after a found placement: kBookCommit = +0x40..+0x4C from +0x20..+0x2C (0x145E10's else branch);
   kBookPath = mode 2: no copy, +0x90 back to its value from before (no request for the +-500 search) */
enum { kBookCommit = 0, kBookPath = 1 };
const int kModePath = 2;
inline int Book(int mode) { return mode == kModePath ? kBookPath : kBookCommit; }

/* the road the placement took (counted in [FLOOR] REPORT, named on the [WINFLOOR] line) */
enum { kNrOwnerFloor = 0, kNrFallback = 1, kNrKeyLostKeepXZ = 2, kNrMissStay = 3, kNrFault = 4, kNrCount = 5 };
inline const char* NrWord(int r)
{
    switch (r)
    {
    case kNrOwnerFloor: return "ownerFloor";
    case kNrFallback: return "fallback";
    case kNrKeyLostKeepXZ: return "keyLostKeepXZ";
    case kNrMissStay: return "missStay";
    case kNrFault: return "bodyFault";
    default: return "?";
    }
}
inline bool NrStayed(int r) { return r == kNrMissStay || r == kNrFault; }
/* how the put-back held: by the short placement at the body's own spot (kPbShort), or every touched field written back (kPbRaw) */
enum { kPbShort = 0, kPbRaw = 1, kPbCount = 2 };
inline int PutBackKind(int place2) { return place2 == kPlaceFound ? kPbShort : kPbRaw; }

/* THE PLACEMENT STEP (one road for the game and the offline suite). NfSnap = the body's fields read before the nudge;
   NfBody = pointers to the live fields (replicate.cpp points them into the engine's body, the suite into a fake one); NfPlaceFn =
   0x3A1F70 (replicate.cpp: a call through the NavPlaceOnFloor row; the suite: a fake). Nothing here reads engine memory by itself. */
struct NfSnap { int key; float pt[4]; float last[4]; int mode; int req; };
struct NfBody { int* key; float* pt; float* last; unsigned char* flag; int* req; };
typedef unsigned int (*NfPlaceFn)(void* ctx, float* pt, int* key, float reach);
/* the short placement - 0x145E10's writes with the walk's reach: +0x20 = at (w 0), +0x14 = key, the re-placement; found: the mode
   bookkeeping (mode as read AFTER the setter, as 0x145E10 reads +0x88 after its call) and +0x80 = 1. Returns kPlace* */
inline int PlaceShort(const NfBody& b, int mode, int req0, const float* at, int key, float reach, NfPlaceFn fn, void* ctx)
{
    b.pt[0] = at[0]; b.pt[1] = at[1]; b.pt[2] = at[2]; b.pt[3] = 0.0f;
    *b.key = key;
    const int res = PlaceResult(fn(ctx, b.pt, b.key, reach));
    if (res == kPlaceFound)
    {
        if (Book(mode) == kBookCommit) { for (int i = 0; i < 4; ++i) b.last[i] = b.pt[i]; }
        else *b.req = req0;   /* mode 2: 0x145E10 would ask for mode 6 (the long search next walk frame) - not asked */
        *b.flag = 1;
    }
    return res;
}
/* the raw put-back: the key, the spot, the last point and the request back as they were before the nudge (+0x80 stays 1: the walk
   then re-places with its own reach) */
inline void PutBackRaw(const NfBody& b, const NfSnap& s0)
{
    *b.key = s0.key;
    for (int i = 0; i < 4; ++i) { b.pt[i] = s0.pt[i]; b.last[i] = s0.last[i]; }
    *b.req = s0.req;
}
/* rule 1's write: the point at `at` (w 0), key -1, and the bookkeeping of a placement (mode 2: the request from before; else the point
   copied to last; +0x80 = 1 so the walk re-places with its own reach next frame) */
inline void KeepXZKeyLost(const NfBody& b, int mode, int req0, const float* at)
{
    b.pt[0] = at[0]; b.pt[1] = at[1]; b.pt[2] = at[2]; b.pt[3] = 0.0f;
    *b.key = -1;
    if (Book(mode) == kBookCommit) { for (int i = 0; i < 4; ++i) b.last[i] = b.pt[i]; }
    else *b.req = req0;
    *b.flag = 1;
}
/* the whole road after the setter (body units: x, z the setter's, ownerY the owner's height x 0.1). Returns kNr*; *place1 = the
   owner-height placement's kPlace* (kPlaceNone when the owner's height is not a number), *place2 = the fallback's (kPlaceNone when
   not tried), *pb = kPb* of a missStay (-1 otherwise), *fallbackY = the height the fallback found (body units; 0 unless place2 is
   kPlaceFound - on missStay / keyLostKeepXZ it names the floor rule 2 turned down) */
inline int PlaceNudge(const NfBody& b, const NfSnap& s0, int mode1, float x, float z, float ownerY, float reach, NfPlaceFn fn, void* ctx,
                      int* place1, int* place2, int* pb, float* fallbackY)
{
    *place1 = kPlaceNone; *place2 = kPlaceNone; *pb = -1; *fallbackY = 0.0f;
    if (NfFinite(ownerY))
    {
        const float atOwner[3] = { x, ownerY, z };
        *place1 = PlaceShort(b, mode1, s0.req, atOwner, s0.key, kOwnerReach, fn, ctx);
        if (*place1 == kPlaceFound) return kNrOwnerFloor;
    }
    const float atOwn[3] = { x, s0.pt[1], z };
    *place2 = PlaceShort(b, mode1, s0.req, atOwn, s0.key, reach, fn, ctx);
    if (*place2 == kPlaceFound) *fallbackY = b.pt[1];
    if (*place2 == kPlaceFound && !FallbackLeftFloor(b.pt[1], s0.pt[1])) return kNrFallback;
    if (KeyLost(s0.key)) { KeepXZKeyLost(b, mode1, s0.req, atOwn); return kNrKeyLostKeepXZ; }
    const float spot[3] = { s0.pt[0], s0.pt[1], s0.pt[2] };
    const int put = PlaceShort(b, mode1, s0.req, spot, s0.key, reach, fn, ctx);
    *pb = PutBackKind(put);
    if (*pb == kPbRaw) PutBackRaw(b, s0);
    return kNrMissStay;
}

/* A TELEPORT'S RE-PLACEMENT: s1 = the body as the teleport left it (the +-500 search's answer), req0 = the body's request from before
   the teleport, ownerY = the owner's height x 0.1. The short placement at the teleport's x / z and the owner's height, +-20, the
   teleport's key; mode 2: the request back to req0 (no long search next frame). Not found / locked: every field written back as the
   teleport left it (the engine's result stays). Returns kTr*; *place = the placement's kPlace* (kPlaceNone when not tried) */
enum { kTrRefloor = 0, kTrMissKept = 1, kTrFault = 2, kTrNoRow = 3, kTrNoBody = 4, kTrCount = 5 };
inline const char* TrWord(int r)
{
    switch (r)
    {
    case kTrRefloor: return "teleportRefloor";
    case kTrMissKept: return "teleportMissKept";
    case kTrFault: return "teleportFault";
    case kTrNoRow: return "teleportNoRow";
    case kTrNoBody: return "teleportNoBody";
    default: return "?";
    }
}
inline int RefloorTeleport(const NfBody& b, const NfSnap& s1, int req0, float ownerY, NfPlaceFn fn, void* ctx, int* place)
{
    *place = kPlaceNone;
    if (!NfFinite(ownerY)) return kTrMissKept;
    const float at[3] = { s1.pt[0], ownerY, s1.pt[2] };
    *place = PlaceShort(b, s1.mode, req0, at, s1.key, kOwnerReach, fn, ctx);
    if (*place == kPlaceFound) return kTrRefloor;
    PutBackRaw(b, s1);
    return kTrMissKept;
}

/* the window counter: a step whose owner's height is over kSameFloorDy off the body (game units) and no floor at the owner's height
   was found under the setter's x / z (place1 not found) */
inline bool OwnerOffNoFloor(float ownerY, float bodyY0, int place1)
{
    const float d = ownerY - bodyY0;
    return NfFinite(d) && NfAbs(d) > kSameFloorDy && place1 != kPlaceFound;
}

/* the body moved floors: it ended over kSameFloorDy off npY (replicate.cpp passes the body's own height before), or over
   kBigBodyMove off its height before the nudge */
inline bool BigBodyMove(float bodyY0, float bodyY1, float npY)
{
    const float a = bodyY1 - npY, b = bodyY1 - bodyY0;
    return !NfFinite(a) || !NfFinite(b) || NfAbs(a) > kSameFloorDy || NfAbs(b) > kBigBodyMove;
}

/* the [WINFLOOR] line: the first 200 of its kind, then every 500th (n = 1-based count of its kind), and every step that stayed
   (not found / locked / fault) or whose body moved floors */
const long long kLineSampleFirst = 200, kLineSampleEvery = 500;
inline bool LineDue(long long n, bool stayed, bool bigMove)
{
    if (n <= kLineSampleFirst || (n % kLineSampleEvery) == 0) return true;
    return stayed || bigMove;
}

}   /* namespace nudgefloor */

#endif
