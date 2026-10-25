#pragma once
// T-924 FAULT 2 - A TEAMMATE'S OUTPOST MOVED ON ITS OWNER'S GAME: THIS GAME'S COPY OF IT IS MOVED TO THE SENT CENTRE.
// The pure half (plugin build.cpp BdShowMoveCopy and the offline suite). The engine moves a player town's centre in place on its
// owner's game (~9 units, report 2026-10-22_1562871838055342132); the copy here stayed at the old spot, so this game was told "too
// close to another town" near it and could found a second copy. When the owner's list is applied, Decide says what is done with one
// matched copy: moved, left (the same spot, too far, engine writes blocked, or another town's radius holds the target) or never
// touched (a town that is not the mod's own copy). T-924 FOLD 1 (review D1, T1453): a gap over kMaxGap is the owner's outpost
// founded again far away (a new town handle, its pieces relinked): the mod's own copy is retired and a new one founded at the sent
// centre (kRefound; BdShowApply does it with the take-out and found roads it already has). C++03.
#include <cmath>

namespace townmove {

const int kStepKeep = 1, kStepTake = 2;   // = baseshow::kRowKeep / kRowTake (the offline suite checks it)
const float kMinGap = 1.0f;               // x/z distance live centre -> sent centre: over this the copy moves
const float kMaxGap = 25.0f;              // at most this; a larger gap: the copy retired and founded again (kRefound, fold 1)
const float kNoGround = -99.0f;           // the ground lookup's "no ground" answer (DAT_14168ADA0, Steam 1.0.65 and 1.0.68)
const long long kLineCap = 40;            // [SHOW] T-924 move lines per session; the rest only counted
const float kMapEdge = 147456.0f;         // = poskey::kSectorOrigin: the map's 64 x 64 cells span [-kMapEdge, kMapEdge) on x and z
const float kFloatMax = 3.402823466e+38F; // FLT_MAX: a value past it (or a NaN) is not a number here

enum { kNotOurs = 0, kSame = 1, kMove = 2, kTooFar = 3, kBlocked = 4, kGuarded = 5, kRefound = 6, kOffMap = 7, kRepeat = 8, kKindN = 9 };   // fold 2: kOffMap, kRepeat (Refusal)

inline const char* KindName(int k)
{
    switch (k)
    {
    case kNotOurs: return "notOurs";
    case kSame:    return "same";
    case kMove:    return "move";
    case kTooFar:  return "tooFar";
    case kBlocked: return "blocked";
    case kGuarded: return "guarded";
    case kRefound: return "refound";
    case kOffMap:  return "offMap";
    case kRepeat:  return "repeat";
    default:       return "?";
    }
}

// the x/z distance (for the log line)
inline float GapXZ(float dx, float dz) { return (float)std::sqrt((double)dx * dx + (double)dz * dz); }

// step: the apply's step for the row (kStepKeep = the copy found by its handle, kStepTake = a town standing at the centre taken);
// ours: 1 = the mod's own copy (founded, taken up, or carrying the mark) - a kept outpost's town that stood already is never moved;
// dx / dz: sent centre minus live centre; sameFactionNear: 1 = another live town of the copy's faction (or this player's) holds the
// target inside its radius (the load sweep would remove the copy there), or that could not be read; writesBlocked: 1 = engine writes
// are blocked, not the main thread, or an address row is missing (for kRefound: the found or take-out row).
// FOLD 1: a gap over kMaxGap that reads as a number is kRefound (the guard and blocked hold it as they hold a move); a gap that is
// not a number (a NaN, an infinity, or a square past a float) is kTooFar - never written. A town not the mod's own: kNotOurs, left.
inline int Decide(int step, int ours, float dx, float dz, int sameFactionNear, int writesBlocked)
{
    if ((step != kStepKeep && step != kStepTake) || ours == 0) return kNotOurs;
    const float d2 = dx * dx + dz * dz;
    if (d2 <= kMinGap * kMinGap) return kSame;
    if (!(d2 <= kFloatMax)) return kTooFar;   // a NaN or an infinity: never written
    if (writesBlocked != 0) return kBlocked;
    if (sameFactionNear != 0) return kGuarded;
    return (d2 <= kMaxGap * kMaxGap) ? kMove : kRefound;
}

// the load sweep's test (0x929FB0: x/z squared distance strictly below the other town's radius squared); a radius that is not a
// number counts as inside (the copy is not moved)
inline int InsideRadius(float x, float z, float ox, float oz, float radius)
{
    if (!(radius == radius)) return 1;
    const float ddx = ox - x, ddz = oz - z;
    return (ddx * ddx + ddz * ddz < radius * radius) ? 1 : 0;
}

// 1 = a number (not a NaN, not an infinity)
inline int Finite(float v) { return (v >= -kFloatMax && v <= kFloatMax) ? 1 : 0; }

// review D3: 1 = (x, z) is off the map (outside [-kMapEdge, kMapEdge) on x or z, or not a number). poskey::SectorOf clamps such a
// spot into an edge cell and the engine's zone lookup 0xA07C10 answers none there: no area is added (0x92C860 not called)
inline int OffMap(float x, float z) { return (x >= -kMapEdge && x < kMapEdge && z >= -kMapEdge && z < kMapEdge) ? 0 : 1; }

// review D4: the height written to Town+0x4C - the ground lookup's answer; on its no-ground answer the sent height when it is a
// number, else the copy's current height (cur)
inline float HeightOf(float ground, float sent, float cur)
{
    if (Finite(ground) == 0) return cur;   // fold 2: a ground answer that is not a number (a NaN, an infinity) is never written
    if (ground != kNoGround) return ground;
    return Finite(sent) != 0 ? sent : cur;
}

// T-924 FOLD 2: the refusals after Decide, before the guard and any write. A move or a refound whose sent centre (x, z) is off the
// map (OffMap) is kOffMap: nothing written, no copy taken out - the copy is left as it is. A refound of a base refounded
// once already this world (refoundedBefore = 1) is kRepeat: left as it is, so a new copy whose centre landed
// over kMaxGap from the sent centre is not refounded at every apply. Off the map is answered first; every other kind as it came.
inline int Refusal(int kind, float x, float z, int refoundedBefore)
{
    if (kind != kMove && kind != kRefound) return kind;
    if (OffMap(x, z) != 0) return kOffMap;
    return (kind == kRefound && refoundedBefore != 0) ? kRepeat : kind;
}

// A base whose re-found waits for its old copy's area to unload (BdShowApply's pending record): the old copy's store must empty,
// so the link pass does not link into it a copied piece that does not name it and stands NEARER the owner's sent (new) centre
// (nx, nz) than the old copy's centre (ox, oz) - a piece of the new site would fill the old copy's store with its model while the
// friend plays there; it is linked to the new copy once that is founded. Equidistant, a spot that did not read (spotRead 0), or a
// gap that is not a number: linked as before. x, z: the piece's spot. 1 = leave the piece unlinked.
inline int PendingLinkSkip(int pending, int namesIt, int spotRead, float x, float z, float ox, float oz, float nx, float nz)
{
    if (pending == 0 || namesIt != 0 || spotRead == 0) return 0;
    const float odx = x - ox, odz = z - oz, ndx = x - nx, ndz = z - nz;
    return (ndx * ndx + ndz * ndz < odx * odx + odz * odz) ? 1 : 0;
}
// A pending re-found's record is kept only while BdShowMoveCopy still answers kRefound for its base; any other answer (founded
// by another road, the copy at the sent centre, refused) drops it. 1 = drop.
inline int PendingDrop(int kind) { return kind != kRefound ? 1 : 0; }

}   // namespace townmove
