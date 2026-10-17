/* src/common/celldrift.h - T-172: A COPY'S CELL HELD BY ANOTHER RECORD IS DRIFT, AND IT IS REPAIRED.
 * The owner's "item added at this cell" (ITEM_MOVE op 0) used to be written into the receiving game's copy with the engine's
 * InventorySection::_addItem 0x748BE0, which overwrites the cell pointers without asking: an item the copy already held there
 * stayed in the section's item list with no grid cell answering it - an INVISIBLE LEFTOVER RECORD (cell1's undo rule had to see
 * past them). Now every record the copy holds in that footprint is removed first through the engine's own removal
 * (Inventory::removeItemDontDestroy_returnsItem 0x749E10 -> InventorySection::removeItem 0x748EE0, then the engine's destroy),
 * and once per copy container a sweep removes the leftovers already there.
 *
 * WHY A GRID RESTORE. InventorySection::removeItem 0x748EE0 zeroes EVERY grid cell of the removed row's footprint without asking
 * what the cell points at (decomp_748ee0: the inner loop writes 0 unconditionally). A leftover lies UNDER a visible item by
 * definition, so removing it blanks that visible item's cells - and the visible item would become the next leftover. The grid is
 * therefore copied before each removal and every cell that held ANOTHER item and now reads empty is given that item back
 * (RestoreBlanked) - the exact cells the engine's blanket zero took, nothing else.
 *
 * THE LEFTOVER TEST (cell1's undo rule, ItUndoCellFree: only an item some grid cell answers is a visible occupant): a record is
 * an invisible leftover when NO in-grid cell of its own footprint (its own cell +0xDC/+0xE0, its width/height +0x130/+0x134)
 * answers it. A footprint with no cell inside the grid, or unreadable dimensions, is NOT judged (left alone, counted).
 *
 * REVIEW FOLD (2026-09-30 full review): a cell is given back only to an item STILL LISTED in the section (RestoreBlanked's live
 * list), and after the removal any cell still answering the removed record is emptied (NullStale) - the record is destroyed next,
 * so a cell left pointing at it would answer a freed object. The copy's repair never runs on a character this game owns
 * (SkipForOwner): there the occupant is a real item. A refusal for an unreadable / oversize grid or a record naming another section
 * repeats on every later drift add of that container (RefusalRepeats) and is said once.
 *
 * Pure: no engine memory, no Windows; the grid is an array of opaque pointers (cell (x,y) at index x + y * width, the layout
 * getItemAt 0x745AF0 and _addItem 0x748BE0 use). The offline suite calls the same functions. C++03 (VS2010 v100).
 */
#pragma once

namespace coopdrift {

const int kRowVisible   = 0;   /* some in-grid cell of the footprint answers the record */
const int kRowLeftover  = 1;   /* no in-grid cell of the footprint answers it: an invisible leftover */
const int kRowUnjudged  = 2;   /* unreadable geometry, or no cell of the footprint inside the grid: not judged, left alone */

const int kGridCellCap  = 4096;   /* ItFirstFreeSlot's bound: a section with more cells reads back implausibly */
const int kLiveCap      = 512;    /* the most records a section may list for a removal to be tried (the sweep's bound too) */

/* Why one record was not removed (the repair's refusal reasons). */
const int kWhyNone      = 0;
const int kWhyGrid      = 1;   /* the section's grid is unreadable or over kGridCellCap cells */
const int kWhySection   = 2;   /* the record names another section (the engine's removal finds the section by that name) */
const int kWhyTooMany   = 3;   /* the section lists more than kLiveCap records */
const int kWhyListed    = 4;   /* the engine's removal ran and the record is still listed */
const int kWhyFault     = 5;   /* a fault inside the removal or the repair after it */

/* 1 = the same refusal will meet every later drift add of this copy container (nothing about the grid or the record changes by
   itself), so it is said once with what could re-sync the copy. */
inline int RefusalRepeats(int why) { return (why == kWhyGrid || why == kWhySection) ? 1 : 0; }

/* 1 = the drift repair is skipped: the add names a character (uid not 0) this game owns (mine 1). There the occupant is a real item
   of ours, not a stale copy record - the add is refused instead of removing it (review MED 1). A box (uid 0) is never skipped here:
   a box this game holds is refused before the apply. */
inline int SkipForOwner(unsigned int uid, int mine) { return (uid != 0u && mine != 0) ? 1 : 0; }

/* 1 = `p` is one of the n pointers in `list`. */
inline int InList(const void* const* list, int n, const void* p)
{
    if (list == 0 || p == 0) return 0;
    for (int i = 0; i < n; ++i) if (list[i] == p) return 1;
    return 0;
}

/* 1 = the two footprints share at least one cell. A non-positive width or height shares nothing. */
inline int FootprintsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return 0;
    if (ax + aw <= bx || bx + bw <= ax) return 0;
    if (ay + ah <= by || by + bh <= ay) return 0;
    return 1;
}

/* 1 = a grid of gw x gh cells is one this code will copy and walk. */
inline int GridOk(int gw, int gh)
{
    return (gw > 0 && gh > 0 && (long long)gw * (long long)gh <= (long long)kGridCellCap) ? 1 : 0;
}

/* The leftover test (cell1's grid rule). `item` 0 is never a record. */
inline int RowVisibility(const void* const* grid, int gw, int gh, const void* item, int x, int y, int w, int h)
{
    if (grid == 0 || item == 0 || GridOk(gw, gh) == 0 || w <= 0 || h <= 0 || x < 0 || y < 0) return kRowUnjudged;
    int inGrid = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            const int cx = x + i, cy = y + j;
            if (cx >= gw || cy >= gh) continue;
            inGrid = 1;
            if (grid[cx + cy * gw] == item) return kRowVisible;
        }
    return (inGrid != 0) ? kRowLeftover : kRowUnjudged;
}

/* After the engine's removal of `removed`: every cell that held ANOTHER item before (`before`) and reads empty now is given that
   item back - only when that item is still listed in the section (`live`, nLive records read after the removal); an item no longer
   listed is never written back into the grid. Cells that held `removed`, cells that were empty, and cells the removal did not empty
   are left as they are. Returns the number of cells given back. */
inline int RestoreBlanked(void** grid, const void* const* before, int n, const void* removed, const void* const* live, int nLive)
{
    if (grid == 0 || before == 0 || n <= 0 || n > kGridCellCap || live == 0 || nLive <= 0 || nLive > kLiveCap) return 0;
    int back = 0;
    for (int i = 0; i < n; ++i)
    {
        if (before[i] != 0 && before[i] != removed && grid[i] == 0 && InList(live, nLive, before[i]) != 0)
        {
            grid[i] = const_cast<void*>(before[i]);
            ++back;
        }
    }
    return back;
}

/* After the removal, before the destroy: every cell still answering `gone` is emptied. Returns the number emptied. */
inline int NullStale(void** grid, int n, const void* gone)
{
    if (grid == 0 || gone == 0 || n <= 0 || n > kGridCellCap) return 0;
    int k = 0;
    for (int i = 0; i < n; ++i) if (grid[i] == gone) { grid[i] = 0; ++k; }
    return k;
}

}   /* namespace coopdrift */
