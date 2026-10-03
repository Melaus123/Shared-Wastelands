/* src/common/kitcell.h - kit2 (owner decision 102, 2026-09-28): EVERY COPY HOLDS EXACTLY THE OWNER'S INVENTORY.
 * A copy's kit (MSG_CLOTHING) named each item's SECTION but not its CELL, and the copy's apply let the engine's whole-inventory
 * addItem pick the section and the cell - ~30 copies per run held the owner's items in other places (same item count, a
 * different inventory digest). The kit now carries each item's cell, and the copy puts the item at exactly that section and
 * cell; when that cell cannot take it here, the same section's first free cell (counted kitRelocated) - never another section,
 * never a merge; no room in the section at all = kitLost.
 *
 * THE WIRE (protocol 83): MSG_CLOTHING may end with a 'KCL1' block LAST - after the item list, the 'QLT1' details block and
 * its 'OWNL' tail, and the 'BAGK' packs block (each optional):
 *     u32 'KCL1' | u32 n (== the kit's item count, 1..kKitCellMaxItems) | n x (u32 x, u32 y)   (each < kKitCellMax)
 * Written only when every item's cell was read. Absent = an older sender: the copy keeps the old engine-picks placement.
 * Malformed (cut, a count that is not the kit's, a cell out of range) = dropped WHOLE: the garments still apply, placed as
 * from an older sender. The block is found by its marker: a 'BAGK' reader meets 'KCL1' first only when the kit has no packs.
 * Pure: no engine memory, no Windows; the offline suite calls the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <vector>
#include <cstring>
#include "copykeep.h"   /* KitCellTaken, KitFirstFreeCell - the engine's own first-free-cell order */

namespace coopkitcell {

const unsigned int kKitCellMarker   = 0x314C434Bu;   /* 'K','C','L','1' little-endian */
const unsigned int kKitCellMax      = 4096u;         /* a cell coordinate at or past this is not a cell (bagwire's kBagMaxCell) */
const unsigned int kKitCellMaxItems = 256u;          /* clothing.cpp's kMaxItems: a kit never lists more */

const int kKitCellOk     = 0;
const int kKitCellAbsent = 1;   /* no bytes left, or the next block is not 'KCL1' */
const int kKitCellBad    = 2;   /* 'KCL1' is there but the block is not whole and in range: dropped whole */

struct KitCell
{
    unsigned int x, y;
    KitCell() : x(0), y(0) {}
    KitCell(unsigned int ax, unsigned int ay) : x(ax), y(ay) {}
};

inline void KitCellPutU32(std::vector<char>* b, unsigned int v)
{
    char t[4];
    std::memcpy(t, &v, 4);
    b->insert(b->end(), t, t + 4);
}

inline bool KitCellGetU32(const char* p, size_t size, size_t* at, unsigned int* v)
{
    if (p == 0 || *at > size || size - *at < 4) return false;
    std::memcpy(v, p + *at, 4);
    *at += 4;
    return true;
}

/* true = a 'KCL1' block starts at `at`. */
inline bool KitCellsAt(const char* p, size_t size, size_t at)
{
    size_t o = at;
    unsigned int mk = 0;
    return p != 0 && at < size && KitCellGetU32(p, size, &o, &mk) && mk == kKitCellMarker;
}

/* Appends the block for a kit of `kitCount` items. false = NOTHING WAS WRITTEN (no items, a count that is not the kit's, over
   the cap, a cell out of range) - the copy then places as from an older sender. */
inline bool EncodeKitCells(std::vector<char>* b, const std::vector<KitCell>& cells, size_t kitCount)
{
    if (b == 0 || kitCount == 0 || cells.size() != kitCount || kitCount > kKitCellMaxItems) return false;
    for (size_t i = 0; i < cells.size(); ++i)
        if (cells[i].x >= kKitCellMax || cells[i].y >= kKitCellMax) return false;
    KitCellPutU32(b, kKitCellMarker);
    KitCellPutU32(b, (unsigned int)kitCount);
    for (size_t i = 0; i < cells.size(); ++i) { KitCellPutU32(b, cells[i].x); KitCellPutU32(b, cells[i].y); }
    return true;
}

/* Reads the block at `at` for a kit of `kitCount` items. On kKitCellOk `*out` holds one cell per item and `*end` the offset
   just after the block; on anything else `*out` is left untouched. */
inline int DecodeKitCells(const char* p, size_t size, size_t at, size_t kitCount, std::vector<KitCell>* out, size_t* end)
{
    if (!KitCellsAt(p, size, at)) return kKitCellAbsent;
    size_t off = at + 4;
    unsigned int n = 0;
    if (!KitCellGetU32(p, size, &off, &n) || n == 0 || (size_t)n != kitCount || n > kKitCellMaxItems) return kKitCellBad;
    std::vector<KitCell> cells;
    cells.reserve(n);
    for (unsigned int i = 0; i < n; ++i)
    {
        KitCell c;
        if (!KitCellGetU32(p, size, &off, &c.x) || !KitCellGetU32(p, size, &off, &c.y)) return kKitCellBad;
        if (c.x >= kKitCellMax || c.y >= kKitCellMax) return kKitCellBad;
        cells.push_back(c);
    }
    if (out) out->swap(cells);
    if (end) *end = off;
    return kKitCellOk;
}

const int kKitCellNone      = 0;   /* no free cell in the section: kitLost */
const int kKitCellExact     = 1;   /* the owner's own cell */
const int kKitCellRelocated = 2;   /* the section's first free cell, in the engine's order: kitRelocated */

/* THE PLACEMENT RULE for one kit item in its own section (grid sw x sh, item footprint iw x ih): the owner's cell (px,py) when
   it lies on the grid and `taken` says it is free there, else the first free cell in the engine's own order
   (coopkeep::KitFirstFreeCell - y outer, x inner). Never another section: the caller only ever asks about the owner's one. */
inline int KitPlaceCell(int sw, int sh, int iw, int ih, int px, int py, coopkeep::KitCellTaken taken, void* ctx, int* x, int* y)
{
    *x = -1;
    *y = -1;
    if (taken == 0 || sw < 1 || sh < 1 || sw > 256 || sh > 256 || iw < 1 || ih < 1) return kKitCellNone;
    if (px >= 0 && py >= 0 && px + iw <= sw && py + ih <= sh && taken(ctx, px, py) == 0) { *x = px; *y = py; return kKitCellExact; }
    return coopkeep::KitFirstFreeCell(sw, sh, iw, ih, taken, ctx, x, y) != 0 ? kKitCellRelocated : kKitCellNone;
}

} // namespace coopkitcell
