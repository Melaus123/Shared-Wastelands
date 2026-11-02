/* boxaside.h - A STORAGE BOX WHOSE WORLD COPY HOLDS ITEMS THIS GAME CANNOT MAKE, AND A BOX WHOSE UPDATE FAILED. Pure rules: no engine.
 *
 * The world's copy of a box wins over a player's save. When the copy names an item whose template is missing from this game's data
 * (a mod this game lacks), that row is not made here: it is kept aside exactly as the world's copy has it, the update counts as done
 * when every other row went in, and every write of the box from this game puts the kept-aside rows back after the box's own contents,
 * so no player's game erases them. Puts into such a box are refused on this game (a hidden item's cells would otherwise be filled).
 * Any other failed update writes nothing from the box and holds it (moves in and out undone) until a later update succeeds.
 */
#pragma once
#include <set>
#include <string>
#include <vector>
#include "paritywire.h"   /* cooppar::ParityItem (and its pack rows, coopbag::BagRow) */

namespace boxaside {

/* The world copy's rows split by whether this game can make each (present[i]: 1 its template is in this game's data, 0 missing).
   make: the rows laid here; kept: the rows kept aside, unchanged and in order. Returns the kept count; firstSid: the first kept row's
   item id (empty when none). A present list shorter than the rows counts the rows past its end as makeable. */
inline size_t SplitRows(const std::vector<cooppar::ParityItem>& rows, const std::vector<int>& present,
                        std::vector<cooppar::ParityItem>* make, std::vector<cooppar::ParityItem>* kept, std::string* firstSid)
{
    if (make != 0) make->clear();
    if (kept != 0) kept->clear();
    if (firstSid != 0) firstSid->clear();
    size_t k = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int can = (i < present.size()) ? present[i] : 1;
        if (can != 0) { if (make != 0) make->push_back(rows[i]); continue; }
        if (k == 0 && firstSid != 0) *firstSid = rows[i].baseSid;
        ++k;
        if (kept != 0) kept->push_back(rows[i]);
    }
    return k;
}

/* 1 = this game can make the world copy's row: every item id it names is in this game's data - the item's own, its company, material
   and colour (an empty one names nothing), and the same four for every row of a pack's contents. A pack holding one item this game
   cannot make is not made at all (the whole row is kept aside), so a pack is never laid with part of its contents. has(sid): 1 = the
   template is in this game's data. */
template <class Has>
inline int SidsMakeable(const std::string& base, const std::string& company, const std::string& material, const std::string& colour, const Has& has)
{
    if (!base.empty() && has(base) == 0) return 0;
    if (!company.empty() && has(company) == 0) return 0;
    if (!material.empty() && has(material) == 0) return 0;
    if (!colour.empty() && has(colour) == 0) return 0;
    return 1;
}
template <class Has>
inline int RowMakeable(const cooppar::ParityItem& r, const Has& has)
{
    if (SidsMakeable(r.baseSid, r.companySid, r.materialSid, r.colorSid, has) == 0) return 0;
    for (size_t i = 0; i < r.bag.size(); ++i)
        if (SidsMakeable(r.bag[i].baseSid, r.bag[i].companySid, r.bag[i].materialSid, r.bag[i].colorSid, has) == 0) return 0;
    return 1;
}

/* 1 = the two rows are the same item at the same cell (item id, section and cell) */
inline int SameRow(const cooppar::ParityItem& a, const cooppar::ParityItem& b)
{
    return (a.baseSid == b.baseSid && a.section == b.section && a.x == b.x && a.y == b.y) ? 1 : 0;
}

/* The box's contents as this game reads or writes them: its own rows first, then the world copy's rows this game cannot make,
   unchanged. A kept row the list already holds (the same item at the same cell - a list that is the whole world copy, or an item the
   box really holds whose template reads as missing only now) is not appended again, so no row is ever doubled. Returns the rows
   appended. */
inline size_t AppendKept(std::vector<cooppar::ParityItem>* items, const std::vector<cooppar::ParityItem>& worldRows, const std::vector<int>& present)
{
    if (items == 0) return 0;
    const size_t own = items->size();
    size_t n = 0;
    for (size_t i = 0; i < worldRows.size(); ++i)
    {
        if (i >= present.size() || present[i] != 0) continue;
        int held = 0;
        for (size_t j = 0; j < own && held == 0; ++j) held = SameRow((*items)[j], worldRows[i]);
        if (held != 0) continue;
        items->push_back(worldRows[i]);
        ++n;
    }
    return n;
}

/* How the update of a box ended. kFailNone: every row this game can make went in (the update counts as done). */
const int kFailNone = 0;
const int kFailNothingMoved = 1;     /* nothing left the box: over 64 sections, an unreadable item cell, a merged pre-read failed */
const int kFailWontLeave = 2;        /* a merged swap: an item would not leave the box (the items that left were put back) */
const int kFailAddsRefused = 3;      /* a row this game can make was refused (no section, cell occupied, the make failed) */
const int kFailContentsDiffer = 4;   /* every row went in but the box does not read as them */
const int kFailMax = 4;

/* the kind of a failed swap: swapRan 0 = nothing was removed; removeFailed > 0 = a merged swap's item would not leave; anyRefused 1 =
   a row was not laid at its cell */
inline int FailKind(int swapRan, int removeFailed, int anyRefused)
{
    if (removeFailed > 0) return kFailWontLeave;
    if (swapRan == 0) return kFailNothingMoved;
    return (anyRefused != 0) ? kFailAddsRefused : kFailContentsDiffer;
}
inline const char* FailKindName(int kind)
{
    switch (kind)
    {
    case kFailNone: return "none";
    case kFailNothingMoved: return "nothing moved";
    case kFailWontLeave: return "an item would not leave the box";
    case kFailAddsRefused: return "an item this game can make was refused";
    case kFailContentsDiffer: return "the box does not read as the world's copy";
    default: return "unknown";
    }
}
/* What the update's end does to the box. kWrite: written from this game as usual (with the kept-aside rows appended); kHold: nothing
   is written from it, and its moves in and out are undone until a later update succeeds (a newer world copy, or its area loaded again). */
const int kWrite = 0, kHold = 1;
inline int AfterUpdate(int failKind) { return (failKind == kFailNone) ? kWrite : kHold; }

/* 1 = a put into the box is refused here: its world copy, as last laid here, holds rows this game cannot make */
inline int PutRefused(size_t keptRows) { return (keptRows > 0) ? 1 : 0; }
/* The same answer on any thread: on the main thread from the box's own state (its kept-row count); off it from the locked table of
   boxes with kept rows (inTable 1 = the box is in it) - such a box is refused there too, never allowed for want of the main thread. */
inline int PutRefusedAt(int onMain, size_t keptRowsMain, int inTable) { return (onMain != 0) ? PutRefused(keptRowsMain) : ((inTable != 0) ? 1 : 0); }
/* What the drain does with a whole item that landed in a box with kept rows past the put hooks: an engine fill's made item (fill 1)
   is destroyed - it never was in the world - and any other put (an add with no character side) goes back out of the box onto the
   ground at the box, where it is announced as any drop. */
const int kUndoToGround = 0, kUndoDestroyFill = 1;
inline int UndoOfLandedPut(int fill) { return (fill != 0) ? kUndoDestroyFill : kUndoToGround; }

/* Once per key: a player line said once per box per play session (cleared when the world changes), or a refused-put line said once per
   box per player action (cleared at each items tick). First() is 1 the first time a key is seen; the set is bounded (past its cap,
   nothing more is said). */
struct SaidOnce
{
    std::set<std::string> said;
    size_t cap;
    SaidOnce() : cap(4096) {}
    int First(const std::string& key)
    {
        if (said.count(key) != 0 || said.size() >= cap) return 0;
        said.insert(key);
        return 1;
    }
    void Clear() { said.clear(); }
};

/* The player's lines (grey chat). */
inline const char* LineCantMake() { return "A storage box here holds items from a mod you don't have. You can take things out of it, but not put things in."; }
inline const char* LinePutRefused() { return "You can't put things in this box: it holds items from a mod you don't have."; }
inline const char* LineFailHeld() { return "A storage box here couldn't be brought up to date with the world, so you can't change it until you come back to this area."; }

}   /* namespace boxaside */
