/* src/common/kitdelta.h - T-1449 fix (design (a), F1799): A COPY IS RE-DRESSED BY CHANGES ONLY.
 * The copy's kit apply (clothing.cpp ApplyGarmentsBody) used to destroy every item the copy held and build the owner's kit new.
 * A destroyed item whose look was still waiting for its colour texture leaves that look on the engine's black stand-in, and every
 * later item of that look name is handed the stuck look (F1782 / F1784; 16 natural stuck looks on the joining game in T2741, F1798).
 * The apply now matches the copy's own items against the owner's kit rows and touches only what differs:
 *   KEEP   - same section, base, maker, material, level and colour: the item stays; the remaining details (quality, charges, stack,
 *            function, unique, stolen mark, uniform, crafter, a pack's contents) are written onto it in place; MOVED when only the
 *            cell differs (cells known).
 *   REMOVE - a current item no row keeps.
 *   ADD    - a row no current item matches: built new, as before.
 * Matching is one to one (two identical rows keep two identical items). Rows are taken in kit order; an item at the row's own cell
 * is preferred (pass 1), then any matching item (pass 2). A current item marked not usable (its identity could not be read) is
 * never kept. A pair is not kept when the item carries a stolen mark or a uniform / crafter the row does not: the owner and gear
 * calls the apply has can write a mark, never clear one, so such an item is rebuilt clean (counted notKeptMark).
 * Pure: no engine memory, no Windows; the offline suite calls the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include "garmentdetail.h"   /* GarmentDetail, OwnerEqual, GearEqual, GearEmpty */
#include "bagwire.h"   /* BagRow - a kept pack's contents compared with the kit's (review fold 3) */

namespace coopkitdelta {

/* One item's identity as the kit compares it: a current item of the copy, or a kit row. */
struct KitKey
{
    std::string section, base, maker, material, colour;
    int level;
    unsigned int x, y;          /* the cell in its section */
    unsigned char usable;       /* current items only: 0 = never kept (identity or details unreadable) */
    unsigned char ownerMark;    /* 1 = a stolen mark (owner kind != none) */
    unsigned char gearMark;     /* 1 = a uniform or crafter */
    KitKey() : level(0), x(0), y(0), usable(1), ownerMark(0), gearMark(0) {}
};

inline bool SameKit(const KitKey& a, const KitKey& b)
{
    return a.section == b.section && a.base == b.base && a.maker == b.maker && a.material == b.material
        && a.level == b.level && a.colour == b.colour;
}

/* May current item `it` be kept for row `row`? Same identity, usable, and no mark on the item that the row lacks. */
inline bool Keepable(const KitKey& it, const KitKey& row)
{
    if (it.usable == 0 || !SameKit(it, row)) return false;
    if (it.ownerMark != 0 && row.ownerMark == 0) return false;
    if (it.gearMark != 0 && row.gearMark == 0) return false;
    return true;
}

const int kRemove = -1;   /* itemRow: no row keeps this item */
const int kAdd    = -1;   /* rowItem: no item matches this row - built new */
const int kSkip   = -2;   /* rowItem: the row is outside this apply's scope (a dead copy's non-worn section) */

struct KitPlan
{
    std::vector<int> itemRow;            /* per current item: the row it is kept for, or kRemove */
    std::vector<int> rowItem;            /* per row: the current item kept for it, kAdd or kSkip */
    std::vector<unsigned char> rowMove;  /* per row: 1 = its kept item moves to the row's cell */
    int kept, moved, removed, added, skipped, notKeptMark;
    KitPlan() : kept(0), moved(0), removed(0), added(0), skipped(0), notKeptMark(0) {}
};

/* items: the copy's current items (in scope); rows: the kit; rowInScope[i] 0 = row i is skipped (empty = every row in scope);
   cellsKnown: the kit carried every item's cell (only then is a cell compared and a move planned). */
inline KitPlan PlanKit(const std::vector<KitKey>& items, const std::vector<KitKey>& rows, const std::vector<unsigned char>& rowInScope,
                       bool cellsKnown)
{
    KitPlan p;
    p.itemRow.assign(items.size(), kRemove);
    p.rowItem.assign(rows.size(), kAdd);
    p.rowMove.assign(rows.size(), 0);
    for (size_t r = 0; r < rows.size(); ++r)
        if (!rowInScope.empty() && (r >= rowInScope.size() || rowInScope[r] == 0)) { p.rowItem[r] = kSkip; ++p.skipped; }
    /* pass 1: an item already at the row's own cell */
    if (cellsKnown)
        for (size_t r = 0; r < rows.size(); ++r)
        {
            if (p.rowItem[r] != kAdd) continue;
            for (size_t k = 0; k < items.size(); ++k)
                if (p.itemRow[k] == kRemove && Keepable(items[k], rows[r]) && items[k].x == rows[r].x && items[k].y == rows[r].y)
                { p.itemRow[k] = (int)r; p.rowItem[r] = (int)k; break; }
        }
    /* pass 2: any matching item; moved when the cells are known and differ */
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if (p.rowItem[r] != kAdd) continue;
        for (size_t k = 0; k < items.size(); ++k)
            if (p.itemRow[k] == kRemove && Keepable(items[k], rows[r]))
            {
                p.itemRow[k] = (int)r; p.rowItem[r] = (int)k;
                if (cellsKnown && (items[k].x != rows[r].x || items[k].y != rows[r].y)) p.rowMove[r] = 1;
                break;
            }
    }
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if (p.rowItem[r] >= 0) { ++p.kept; if (p.rowMove[r] != 0) ++p.moved; }
        else if (p.rowItem[r] == kAdd) ++p.added;
    }
    for (size_t k = 0; k < items.size(); ++k)
    {
        if (p.itemRow[k] != kRemove) continue;
        ++p.removed;
        /* notKeptMark: it would have been kept but for a mark the row lacks (an unmatched row of the same identity is left) */
        if (items[k].usable == 0) continue;
        for (size_t r = 0; r < rows.size(); ++r)
            if (p.rowItem[r] == kAdd && SameKit(items[k], rows[r])) { ++p.notKeptMark; break; }
    }
    return p;
}

/* What the in-place write of a KEPT item does (bits): */
const int kWriteFields = 1;   /* quality / charges / stack / function / unique differ: WriteItemDetailPod */
const int kWriteOwner  = 2;   /* the row's stolen mark differs and the row has one: the owner call */
const int kWriteGear   = 4;   /* the row's uniform / crafter differ and the row has some: the gear call */
inline int InPlaceWrites(const coopgarment::GarmentDetail& have, const coopgarment::GarmentDetail& want)
{
    int w = 0;
    if (have.quality != want.quality || have.charges != want.charges || have.quantity != want.quantity
        || have.functionKind != want.functionKind || (have.unique != 0) != (want.unique != 0)) w |= kWriteFields;
    if (want.owner.kind != coopmark::kOwnNone && !coopmark::OwnerEqual(have.owner, want.owner)) w |= kWriteOwner;
    if (!gearwire::GearEmpty(want.gear) && !gearwire::GearEqual(have.gear, want.gear)) w |= kWriteGear;
    return w;
}

/* review fold 1: a REMOVED item and an ADDED row of the same section and base - a real change of that slot (the joining game
   picked its own variant of the item). Paired one to one in row order. */
inline std::vector<std::pair<int, int> > SameSlotPairs(const std::vector<KitKey>& items, const std::vector<KitKey>& rows, const KitPlan& p)
{
    std::vector<std::pair<int, int> > out;
    std::vector<unsigned char> used(items.size(), 0);
    for (size_t r = 0; r < rows.size() && r < p.rowItem.size(); ++r)
    {
        if (p.rowItem[r] != kAdd) continue;
        for (size_t k = 0; k < items.size() && k < p.itemRow.size(); ++k)
            if (used[k] == 0 && p.itemRow[k] == kRemove && items[k].section == rows[r].section && items[k].base == rows[r].base)
            { used[k] = 1; out.push_back(std::make_pair((int)k, (int)r)); break; }
    }
    return out;
}
/* review fold 1: which part of the identity kept that pair apart - "maker,material,level,colour,stolenMark,uniformOrCrafter,
   unreadable" (in that order, only those that apply; "none" = nothing found) */
inline std::string DiffWords(const KitKey& it, const KitKey& row)
{
    std::string w;
    if (it.maker != row.maker) w += ",maker";
    if (it.material != row.material) w += ",material";
    if (it.level != row.level) w += ",level";
    if (it.colour != row.colour) w += ",colour";
    if (it.ownerMark != 0 && row.ownerMark == 0) w += ",stolenMark";
    if (it.gearMark != 0 && row.gearMark == 0) w += ",uniformOrCrafter";
    if (it.usable == 0) w += ",unreadable";
    return w.empty() ? std::string("none") : w.substr(1);
}
const int kSameSlotLineCap = 40;   /* review fold 1: same-slot lines a life; the rest counted on the REPORT (kitSameSlot) */

/* review fold 3: is a kept pack's content already the kit's? Rows equal field by field (cell, stack, identity, details, marks,
   nesting); without nesting the order does not matter (one to one), with nesting the order must be the same (a row names its
   parent by index). */
inline bool BagRowSame(const coopbag::BagRow& a, const coopbag::BagRow& b)
{
    return a.section == b.section && a.x == b.x && a.y == b.y && a.qty == b.qty && a.baseSid == b.baseSid && a.companySid == b.companySid
        && a.materialSid == b.materialSid && a.colorSid == b.colorSid && a.quality == b.quality && a.charges == b.charges
        && a.functionKind == b.functionKind && a.level == b.level && (a.unique != 0) == (b.unique != 0) && a.parent == b.parent
        && coopmark::OwnerEqual(a.owner, b.owner) && gearwire::GearEqual(a.gear, b.gear);
}
inline bool BagRowsSame(const std::vector<coopbag::BagRow>& have, const std::vector<coopbag::BagRow>& want)
{
    if (have.size() != want.size()) return false;
    bool nested = false;
    for (size_t i = 0; i < have.size(); ++i) if (have[i].parent != 0 || want[i].parent != 0) nested = true;
    if (nested)
    {
        for (size_t i = 0; i < have.size(); ++i) if (!BagRowSame(have[i], want[i])) return false;
        return true;
    }
    std::vector<unsigned char> used(have.size(), 0);
    for (size_t i = 0; i < want.size(); ++i)
    {
        bool found = false;
        for (size_t j = 0; j < have.size() && !found; ++j)
            if (used[j] == 0 && BagRowSame(have[j], want[i])) { used[j] = 1; found = true; }
        if (!found) return false;
    }
    return true;
}

/* the redress line's token: kept counts every kept item (changedInPlace and moved are among them) */
inline std::string DeltaToken(int kept, int changedInPlace, int moved, int removed, int added)
{
    char b[160];
    std::sprintf(b, " delta[kept,changedInPlace,moved,removed,added]=%d,%d,%d,%d,%d", kept, changedInPlace, moved, removed, added);
    return std::string(b);
}

}   /* namespace coopkitdelta */
