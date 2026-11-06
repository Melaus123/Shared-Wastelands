/* src/common/spotsync.h - T-1214 design D1 (owner decision 1451 a): A BUILDING'S ITEM SPOTS ARE ROLLED BY THE AREA'S HOLDER ONLY.
 *
 * The small items on town tables and shelves are made by the building's item spots (BuildingItemSpot): the spot update
 * (1.0.65 0x5493D0 / 1.0.68 0x549E60) calls the roll (0x547440 / 0x547ED0) for an empty spot whose respawn time has come, and the
 * roll picks the kind with plain rand() - so every game made different items (run T1938: 88 of 91). Now:
 *   - the holder of the area (the ground road's holder) rolls as the game does, and announces the item as a GROUND ADD that ends
 *     with an 'SPT1' block naming the spot;
 *   - every other game withholds the roll (the spot stays empty and due, so the engine asks again on its next update) and adopts
 *     the holder's item: the ADD places it as the ground road places any item, and the spot takes it when it next updates
 *     (attach = what the roll and the area file's restore 0x5562A0 write: item +0x188 = the spot, the item's hand words
 *     +0x60..+0x70 into the spot's +0x148..+0x158);
 *   - the holder's ground catch-up serves every item its spots hold the same way (its own allowance, outside the listing gate).
 * Only a firm 'mine' rolls (SpotHolderFirm), and an SPT1 block is taken only from the area's mapped holder (SpotFromHolderOk).
 * The holder's area file stays the stored home of spot items (manager 2026-11-03); the live ADDs only deliver its changes.
 *
 * THE SPT1 BLOCK (game-to-game protocol 203), on ITEM_MOVE op 0 after the GEM1 block and before a BAGP block:
 *     marker u32 'SPT1' | i32 x10 | i32 y10 | i32 z10          (the spot's position +0x48, in tenths of a world unit, rounded)
 * Both games read a building's spots from the same file bytes, so the position names the spot on every game.
 *
 * Pure: no engine memory, no Windows; the offline suite (test spot_sync) hits every branch. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace spotsync {

const unsigned int kSpotMarker = 0x31545053u;   /* 'S','P','T','1' little-endian */
const size_t kSpotBlockLen = 16;                /* the marker and three i32 */
const int kSpotTenthsMax = 100000000;           /* |coordinate| <= 1e7 units */
const int kSpotNearTenths = 2;                  /* a waiting item takes the spot within 0.2 units on each axis (rounding only) */
const int kSpotWaitCap = 512;                   /* items waiting for their spot; the oldest is dropped (counted) */
const int kSpotServeCap = 512;                  /* spot items in one catch-up answer, apart from the answer's 256 (T1938: 143 rolls in one area) */
const unsigned int kSpotHandNone = 0xBu;        /* the hand type of an empty hand (groundsync.h kSpotHandNone) */

struct SpotMark { int has; int x10, y10, z10; };
inline void SpotMarkClear(SpotMark* m) { m->has = 0; m->x10 = 0; m->y10 = 0; m->z10 = 0; }

/* one coordinate in tenths, rounded half away from zero. 0 = not finite or out of range (*out untouched) */
inline int SpotTenths(float v, int* out)
{
    if (!(v == v)) return 0;
    const double t = (double)v * 10.0;
    if (!(t >= -(double)kSpotTenthsMax && t <= (double)kSpotTenthsMax)) return 0;
    *out = (int)(t >= 0.0 ? t + 0.5 : t - 0.5);
    return 1;
}
/* the mark of a spot at `pos` (x, y, z). 1 = made (has 1); 0 = a coordinate would not convert (the mark is cleared) */
inline int SpotMarkFromPos(const float* pos, SpotMark* m)
{
    SpotMarkClear(m);
    int x = 0, y = 0, z = 0;
    if (pos == 0 || SpotTenths(pos[0], &x) == 0 || SpotTenths(pos[1], &y) == 0 || SpotTenths(pos[2], &z) == 0) return 0;
    m->has = 1; m->x10 = x; m->y10 = y; m->z10 = z;
    return 1;
}
inline int SpotAbsDiff(int a, int b) { return a >= b ? a - b : b - a; }
/* 1 = both marks name the same spot (every axis within kSpotNearTenths) */
inline int SpotSame(const SpotMark& a, const SpotMark& b)
{
    if (a.has == 0 || b.has == 0) return 0;
    return (SpotAbsDiff(a.x10, b.x10) <= kSpotNearTenths && SpotAbsDiff(a.y10, b.y10) <= kSpotNearTenths
            && SpotAbsDiff(a.z10, b.z10) <= kSpotNearTenths) ? 1 : 0;
}

/* ---- the wire ---- */
const int kSpotOk = 0, kSpotAbsent = 1, kSpotBad = 2;
inline void SpotPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline unsigned int SpotGetU32(const char* p) { unsigned int v = 0; std::memcpy(&v, p, 4); return v; }
/* 1 when the four bytes at `at` are the SPT1 marker */
inline bool SpotBlockAt(const char* p, size_t size, size_t at)
{
    return p != 0 && at + 4 <= size && SpotGetU32(p + at) == kSpotMarker;
}
inline int SpotInRange(int v) { return (v >= -kSpotTenthsMax && v <= kSpotTenthsMax) ? 1 : 0; }
/* 1 written; 0 nothing to write (has 0); -1 a coordinate out of range (nothing written) */
inline int EncodeSpot(std::vector<char>* b, const SpotMark& m)
{
    if (m.has == 0) return 0;
    if (b == 0 || !SpotInRange(m.x10) || !SpotInRange(m.y10) || !SpotInRange(m.z10)) return -1;
    SpotPutU32(b, kSpotMarker);
    SpotPutU32(b, (unsigned int)m.x10); SpotPutU32(b, (unsigned int)m.y10); SpotPutU32(b, (unsigned int)m.z10);
    return 1;
}
/* the block at `at`: kSpotOk (*out has 1, *end after it), kSpotAbsent (no marker there), kSpotBad (the marker, then cut or out of
   range). *out / *end are written only on kSpotOk. */
inline int DecodeSpot(const char* p, size_t size, size_t at, SpotMark* out, size_t* end)
{
    if (!SpotBlockAt(p, size, at)) return kSpotAbsent;
    if (at + kSpotBlockLen > size) return kSpotBad;
    const int x = (int)SpotGetU32(p + at + 4), y = (int)SpotGetU32(p + at + 8), z = (int)SpotGetU32(p + at + 12);
    if (!SpotInRange(x) || !SpotInRange(y) || !SpotInRange(z)) return kSpotBad;
    out->has = 1; out->x10 = x; out->y10 = y; out->z10 = z;
    *end = at + kSpotBlockLen;
    return kSpotOk;
}

/* ---- the roll ---- */
const int kRollEngine = 0;    /* not a spot update's roll on the main thread (build mode, an unseen caller): the engine's own */
const int kRollAttach = 1;    /* an item announced for this spot is waiting here: it takes the spot, nothing is rolled */
const int kRollHolder = 2;    /* this game holds the area: it rolls as the game does and announces the item */
const int kRollWithheld = 3;  /* another game holds the area (or nobody can say yet): no roll; the spot stays due and asks again */
inline int SpotRollDecide(int inUpdate, int onMain, int waitingAttached, int holderMine)
{
    if (inUpdate == 0 || onMain == 0) return kRollEngine;
    if (waitingAttached != 0) return kRollAttach;
    return holderMine != 0 ? kRollHolder : kRollWithheld;
}
/* the `holderMine` SpotRollDecide takes: 1 = this game may roll at the spot. A game with no other game in its world (single player,
   or nobody else in the world) rolls as the plain game. Otherwise only a FIRM 'mine' rolls: the area verdict says mine, the area map
   is fresh and its row names this game - not the load window's 'mine', a stale map's ladder or the arrival presumption, which every
   game near the spot can answer at once (each would roll and announce its own item) */
inline int SpotHolderFirm(int verdictMine, int noOtherGame, int mapFresh, int areaRowMine)
{
    if (noOtherGame != 0) return 1;
    return (verdictMine != 0 && mapFresh != 0 && areaRowMine != 0) ? 1 : 0;
}
/* the receive side: 1 = an ADD's SPT1 block is taken - its sender is the game the area map names as the area's holder; 0 = the block
   is dropped (counted) and the item lies as a plain ground item: no wait, no attach - only when the sender and the holder are both
   known and differ; a sender or a holder not yet known is taken (a stale map or a link before the sender's slot arrives), as boxes are */
inline int SpotFromHolderOk(int senderSlot, int holderSlot)
{
    return (senderSlot >= 0 && holderSlot >= 0 && senderSlot != holderSlot) ? 0 : 1;   /* dropped only when both are known and differ - as boxes (ItBoxSenderIsWriter) */
}

/* ---- the attach: what the spot holds now against the waiting item ---- */
const int kAttachSkip = 0;       /* the item is no longer on the ground (taken, or unreadable): the waiting row is dropped */
const int kAttachAlready = 1;    /* the spot already names this item: only the item's link is written */
const int kAttachEmpty = 2;      /* the spot is empty: linked */
const int kAttachReplace = 3;    /* the spot holds another item lying at it (a roll of this game's own, an older file's item): that
                                    item is removed here, then linked - only the holder's roll stands at a spot */
const int kAttachOverwrite = 4;  /* the spot names an item that is not lying at it (picked up, gone, unreadable): linked over it, as
                                    the spot's own release would clear it */
inline int SpotAttachDecide(int itemOnGround, unsigned int spotHandType, int handNamesItem, int oldLiesAtSpot)
{
    if (itemOnGround != 1) return kAttachSkip;
    if (spotHandType == kSpotHandNone) return kAttachEmpty;
    if (handNamesItem != 0) return kAttachAlready;
    return oldLiesAtSpot != 0 ? kAttachReplace : kAttachOverwrite;
}

/* ---- the waiting items: an ADD's item whose spot has not taken it yet (its building's inside not shown, or not updated yet) ----
   Bounded (kSpotWaitCap, the oldest dropped), one row per spot (a newer ADD for the spot replaces the row) and per item; dropped
   when the item is taken by its spot, is no longer on the ground, and at world teardown. The item is named by its hand's five
   words, resolved again through the engine's own lookup at each use - never a kept pointer. */
struct SpotWait { SpotMark at; unsigned int hand[5]; unsigned int since; unsigned int holdSince; };   /* holdSince: SpotOverwriteHold's start (0 none) */
struct SpotWaitList { std::vector<SpotWait> rows; long long dropped; };
inline void SpotWaitInit(SpotWaitList* q) { q->rows.clear(); q->dropped = 0; }
inline int SpotHandSame(const unsigned int* a, const unsigned int* b) { return std::memcmp(a, b, 5 * sizeof(unsigned int)) == 0 ? 1 : 0; }
/* adds (or replaces) the row; returns 1 added, 2 replaced a row of the same spot or item, 3 added after dropping the oldest, 0 not
   added (no mark, or the hand is none) */
inline int SpotWaitAdd(SpotWaitList* q, const SpotMark& at, const unsigned int* hand, unsigned int now)
{
    if (at.has == 0 || hand == 0 || hand[0] == kSpotHandNone) return 0;
    int replaced = 0;
    for (size_t i = 0; i < q->rows.size(); )
    {
        if (SpotSame(q->rows[i].at, at) != 0 || SpotHandSame(q->rows[i].hand, hand) != 0) { q->rows.erase(q->rows.begin() + (long)i); replaced = 1; continue; }
        ++i;
    }
    int cut = 0;
    if (q->rows.size() >= (size_t)kSpotWaitCap) { q->rows.erase(q->rows.begin()); ++q->dropped; cut = 1; }
    SpotWait w; w.at = at; std::memcpy(w.hand, hand, sizeof w.hand); w.since = now; w.holdSince = 0;
    q->rows.push_back(w);
    return replaced != 0 ? 2 : (cut != 0 ? 3 : 1);
}
/* the row waiting for the spot at `at` (rows are one per spot), -1 none */
inline int SpotWaitFind(const SpotWaitList& q, const SpotMark& at)
{
    for (size_t i = 0; i < q.rows.size(); ++i) if (SpotSame(q.rows[i].at, at) != 0) return (int)i;
    return -1;
}
/* 1 = the item with these hand words waits for its spot (an asker's catch-up leaves it out of its listing: the holder serves it as
   its spot's item and never pairs it, so a listed one would come back as a GONE and be built again) */
inline int SpotWaitHasHand(const SpotWaitList& q, const unsigned int* hand)
{
    if (hand == 0 || hand[0] == kSpotHandNone) return 0;
    for (size_t i = 0; i < q.rows.size(); ++i) if (SpotHandSame(q.rows[i].hand, hand) != 0) return 1;
    return 0;
}
inline void SpotWaitDrop(SpotWaitList* q, int i) { if (i >= 0 && (size_t)i < q->rows.size()) q->rows.erase(q->rows.begin() + (long)i); }
/* teardown: every row names the world that is going. Returns how many were dropped. */
inline int SpotWaitClear(SpotWaitList* q) { const int n = (int)q->rows.size(); q->rows.clear(); return n; }

/* ---- T-1214 D1 height fix: an attached item is placed as the engine's own roll places it ----
   The roll 0x547440 places its item with the item vtable slot +0x2D0: (item, &spot+0x48 position, &spot+0xB0 rotation, 1)
   (build/decomp_547440.txt:18). Item::activate 0x75CE70 calls that same slot with its own position, rotation and fixed-position
   flag before it makes the item's body (decomp_75ce70.txt:6); the slot's body stores the three (+0x48, +0xB0, +0x1D9 -
   decomp_75cb20.txt, read as that slot by its shape). An arriving item was activated as a dropped one - flag 0, the identity
   rotation - so its body fell or was pushed (run T1946: 49 of 91 shelf items at another height). Now an SPT1 item is activated
   pinned (flag 1) where the holder's ADD puts it, and once a spot takes it, its body is made again pinned at the spot's own
   position and rotation (at GroundDrain, outside the engine's spot update). */
const float kSpotPlaceNear = 0.05f;    /* within this on every axis the item stands at its spot */
const float kSpotRotNear = 0.001f;     /* and its rotation's four numbers within this (q and -q are one rotation) */
const int kSpotPlaceCap = 512;         /* placements queued for GroundDrain; the oldest dropped (counted) */
inline float SpotAbs(float v) { return v < 0.0f ? -v : v; }
/* 1 = the item must be placed again: not pinned (its fixed byte is not 1), or off its spot's position or rotation (a value that is
   not a number counts as off); 0 = it stands as the roll would place it, or it could not be read (an unreadable item is not touched) */
inline int SpotPlaceNeeded(int readOk, const float* itemPos, const float* itemRot, int fixedByte, const float* spotPos, const float* spotRot)
{
    if (readOk == 0) return 0;
    if (fixedByte != 1) return 1;
    for (int i = 0; i < 3; ++i) if (!(SpotAbs(itemPos[i] - spotPos[i]) <= kSpotPlaceNear)) return 1;
    int same = 1, flip = 1;
    for (int i = 0; i < 4; ++i)
    {
        if (!(SpotAbs(itemRot[i] - spotRot[i]) <= kSpotRotNear)) same = 0;
        if (!(SpotAbs(itemRot[i] + spotRot[i]) <= kSpotRotNear)) flip = 0;
    }
    return (same != 0 || flip != 0) ? 0 : 1;
}
/* ---- T-1214 D1 double check: the overwrite road holds while the spot's own item is not lying there yet ----
   An item the area file restored into a spot whose building's inside is not shown is off (the spot's switch 0x547560 turns the item
   it holds with it), so the ground walk does not see it and the holder's ADD for that spot builds a fresh item that waits (run T1946,
   B after the holder change: 139 of 169). Linking the waiting item over a spot item that exists but does not lie there yet would leave
   that copy in the world beside the holder's - a double. So while the item the spot names exists, is in no inventory and is linked to
   this spot (+0x188), the row is kept and the spot's next update asks again (once it lies there: replace, which removes it); after
   kSpotHoldMs of holding, the waiting item is linked over it as before (counted). */
const unsigned int kSpotHoldMs = 5000;
inline int SpotOverwriteHold(int oldExists, int oldLinkedHere, int oldInInventory, unsigned int heldForMs)
{
    if (oldExists == 0 || oldLinkedHere == 0 || oldInInventory != 0) return 0;
    return heldForMs < kSpotHoldMs ? 1 : 0;
}

/* ---- the log lines: the first kSpotLogFirst of a kind, then at most one per kSpotLogGapMs (the rest counted quiet) ---- */
const long kSpotLogFirst = 40;
const unsigned int kSpotLogGapMs = 5000;
inline int SpotLogDue(long said, unsigned int now, unsigned int lastAt)
{
    if (said < kSpotLogFirst) return 1;
    return (unsigned int)(now - lastAt) >= kSpotLogGapMs ? 1 : 0;
}

}   /* namespace spotsync */
