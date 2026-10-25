// buildnear.h - T-923 (owner decision 951 a, 2026-10-19): the engine's "Can't build too close to another town." decision, as the
// build cursor's preview makes it (PreviewBuilding vt+0xC8 = 0x4DAFB0, Steam 1.0.65; Read from decomp_4dafb0.txt), so the TEST-ONLY
// lever `buildtest canbuild` (build.cpp BdTestCanBuild) can re-ask it at a spot and say which leg decided.
// The engine's steps, in its order:
//   1. t = the nearest town of any faction and any kind but dead (8) (TownListNearest 0x927040, faction 0, kind 10);
//   2. inside = the spot is in t (TownContainsPoint 0x925E80) at the scale 2.5 (1.0 when t's kind, town+0xD8, is 4 or 9), or
//      t's centre (town+0x48 / +0x50) is nearer than 2000 units on the ground plane;
//   3. (a second search over non-player towns, 0x9279C0, can swap t - not modelled here; the lever says so);
//   4. t is let go when inside is 0, or when t's faction (its vt+0x58) is non-null and a PLAYER faction (Faction+0x250, its
//      PlayerInterface*, non-zero) - the only exemption;
//   5. when t is still held and the piece's game data "creates player town" is false, inside is asked again at the scale 1.0
//      (the centre distance does not count here) and t is let go when it is 0.
//   The message shows while t is held.
// Pure (no includes): build.cpp and the offline suite include this one header.
#ifndef BUILDNEAR_H
#define BUILDNEAR_H
namespace buildnear {
const float kScaleWide = 2.5f;       // step 2's scale for most towns
const float kScaleNarrow = 1.0f;     // step 2's scale for kinds 4 and 9, and step 5's scale
const float kCentreRange = 2000.0f;  // step 2: a centre nearer than this counts as inside
const int kKindNarrowA = 4, kKindNarrowB = 9;

// step 2's scale for a town of this kind (town+0xD8)
inline float ScaleForKind(int kind) { return (kind == kKindNarrowA || kind == kKindNarrowB) ? kScaleNarrow : kScaleWide; }
// step 2: in the town's area at its scale, or near its centre
inline int InsideFirst(int insideAtScale, float centreDist) { return (insideAtScale != 0 || centreDist < kCentreRange) ? 1 : 0; }
// the verdict: 1 refused ("Can't build too close to another town."), 0 allowed, -1 unknown (createsPlayerTown unread, -1, and
// the step-5 answer would depend on it). insideFirst is step 2's answer, insideAtOne step 5's.
inline int Refused(int hasTown, int insideFirst, int factionIsPlayer, int createsPlayerTown, int insideAtOne)
{
    if (hasTown == 0 || insideFirst == 0 || factionIsPlayer != 0) return 0;
    if (createsPlayerTown > 0) return 1;            // step 5 skipped: held at step 2's scale
    if (createsPlayerTown == 0) return insideAtOne != 0 ? 1 : 0;
    return insideAtOne != 0 ? 1 : -1;               // unread: refused either way when inside at 1.0, else unknown
}

// T-923 (decisions 967 a + 973 a): the placement confirm's town pick (0x4D6300's call 0x927040 for this game's player faction; build.cpp
// BdTownPickRedirect) in three zones:
//   inside one of this player's own towns (ownInside 1)          - the engine's own answer stands (the piece is this player's);
//   inside a CURRENT teammate's outpost (mateZone kZoneInside)   - the teammate's town;
//   in that outpost's 'too close' ring (mateZone kZoneRing)      - the teammate's town only when the engine's own pick keeps no own
//                                                                  town (ownKept 0: none within 5000 - it would found 'Your Outpost');
//   elsewhere                                                    - the engine's own answer.
// "Inside" = the engine's own test TownContainsPoint 0x925E80 / 0x926D50 (Read: not a dead town, and the ground-plane distance from the
// town's centre +0x48/+0x50 below its radius vt+0x2A0 times the scale) at the scale 1.0; "the ring" = inside by the near-town rule's own
// test (InsideFirst: at ScaleForKind's scale - 2.5, or 1.0 for kinds 4 / 9 - or the centre nearer than 2000) and not inside at 1.0.
// An answer that did not read (-1) is never a yes: an unread mate zone does not take it, an unread own test keeps the engine's answer.
// The caller (0x4D6300) keeps the answer only within 5000 of the spot (TownKept); past that it drops it and goes on to this player's
// own building within 2500, then founds 'Your Outpost' (Read, decomp_4d6300) - so a teammate's town past 5000, or whose distance did
// not read, is never the answer (it would only lose an own town the engine kept). An own town whose distance did not read is not
// known to be dropped: the ring does not take the teammate's town over it.
// 1 = the teammate's town, 0 = the engine's own answer stands.
const int kZoneOut = 0, kZoneRing = 1, kZoneInside = 2;
// the confirm keeps the town it picked only within this distance (0x4D642F, the float 5000.0 at 0x16BECB0, Steam 1.0.65)
const float kTownKeepRange = 5000.0f;
// the caller's keep test on a town at this ground-plane distance: 1 kept (within 5000), 0 none or past it, -1 a town whose distance
// did not read (dist < 0)
inline int TownKept(int haveTown, float dist)
{
    if (haveTown == 0) return 0;
    if (dist < 0.0f) return -1;
    return dist <= kTownKeepRange ? 1 : 0;
}
// why the pick does or does not take the teammate's town (TownPickWhy)
const int kPickTaken = 0, kPickNoMate = 1, kPickOwnInside = 2, kPickOut = 3, kPickMatePast = 4, kPickOwnKept = 5, kPickOwnUnread = 6;
inline int TownPickWhy(int haveMate, int mateSlot, int mateZone, int ownInside, int ownKept, int mateKept)
{
    if (haveMate == 0 || mateSlot < 0) return kPickNoMate;
    if (ownInside != 0) return kPickOwnInside;
    if (mateZone != kZoneInside && mateZone != kZoneRing) return kPickOut;
    if (mateKept != 1) return kPickMatePast;
    if (mateZone == kZoneInside) return kPickTaken;
    if (ownKept < 0) return kPickOwnUnread;
    return ownKept != 0 ? kPickOwnKept : kPickTaken;
}
inline int MateZone(int insideAtOne, int insideFirst)
{
    if (insideAtOne < 0 || insideFirst < 0) return -1;
    if (insideAtOne != 0) return kZoneInside;
    return insideFirst != 0 ? kZoneRing : kZoneOut;
}
inline int TownPickTakesMate(int haveMate, int mateSlot, int mateZone, int ownInside, int ownKept, int mateKept)
{
    return TownPickWhy(haveMate, mateSlot, mateZone, ownInside, ownKept, mateKept) == kPickTaken ? 1 : 0;
}
}   // namespace buildnear
#endif
