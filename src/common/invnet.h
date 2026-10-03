/* inv2a (user decision 2026-09-26) - THE INVENTORY SAFETY NET, its PURE part.
   The mod publishes an inventory change only when one of its hooked engine functions sees it. Some engine routes write
   straight into an item (inv1-inventory-change-audit.md: Inventory::takeOneItemOnly 0x749FF0 lowers the SOURCE stack by a
   direct write; kit charges and food bites are direct float writes), so the other game never hears of them. The net keeps,
   per inventory this game is the authority of, the LAST-ANNOUNCED contents (the BASELINE: what the other game was told), and
   every few seconds compares the live contents with it. A difference is sent as the ordinary ITEM_MOVE a normal change
   would have produced.
   THE BASELINE FOLLOWS THE WIRE. Every ITEM_MOVE the plugin publishes is applied to the baseline with ApplyOp - the same op
   the receiver applies to its copy - so a normal, already-sent change is never caught a second time.
   Identity is the slot (section, x, y), exactly as on the wire (items.h ItemMoveMsg). What the wire carries:
     op 0 ADD    - the stack size and the 0x2A fields, CHARGES INCLUDED;
     op 1 REMOVE - the whole stack at the slot;
     op 2 QTY    - the surviving stack's ABSOLUTE quantity;
     op 3 SPLIT  - units taken out of the stack at the slot.
   A charges change on an item that stays put has NO wire representation: Diff reports it (kOpCharges, or chargesChanged on
   a kOpQty) and the plugin counts it as `uncarried` (effort inv2b decides).
   Header-only; the plugin (items.cpp) and the offline suite (coop-test) include it. No C++11. */
#ifndef COOP_INVNET_H
#define COOP_INVNET_H

#include <string>
#include <vector>
#include <algorithm>

namespace coopinv {

struct Slot
{
    std::string section; int x, y;
    std::string sid;       /* the item's base record sid */
    int qty;
    float charges;         /* Item +0x118 */
    Slot() : x(0), y(0), qty(0), charges(0.0f) {}
};

const int kOpAdd = 0;       /* the wire's op numbers, on purpose */
const int kOpRemove = 1;
const int kOpQty = 2;
const int kOpSplit = 3;     /* never produced by Diff (a quantity change is sent absolute); ApplyOp follows it */
const int kOpCharges = 4;   /* items9 (protocol 78): ITEM_MOVE op 4 CHARGES (src/common/itemstatwire.h) - was uncarried */

struct Op
{
    int kind;
    std::string section; int x, y;
    std::string sid;       /* add / qty / charges: the live item's; remove: the baseline's */
    int qtyFrom, qtyTo;
    float chFrom, chTo;
    int chargesChanged;    /* kOpQty: the charges moved as well (not carried by op 2) */
    Op() : kind(0), x(0), y(0), qtyFrom(0), qtyTo(0), chFrom(0.0f), chTo(0.0f), chargesChanged(0) {}
};

/* charges to 0.01, as an integer; a NaN or an absurd value reads as 0 (the same rule as cooppar::ParityQuality100) */
inline int Charges100(float c)
{
    if (!(c == c) || c > 1.0e6f || c < -1.0e6f) return 0;
    const double d = (double)c * 100.0;
    return (int)(d >= 0.0 ? d + 0.5 : d - 0.5);
}
inline int SlotCmp(const std::string& sa, int xa, int ya, const std::string& sb, int xb, int yb)
{
    const int c = sa.compare(sb);
    if (c != 0) return (c < 0) ? -1 : 1;
    if (xa != xb) return (xa < xb) ? -1 : 1;
    if (ya != yb) return (ya < yb) ? -1 : 1;
    return 0;
}
inline bool SlotLess(const Slot& a, const Slot& b) { return SlotCmp(a.section, a.x, a.y, b.section, b.x, b.y) < 0; }
inline int FindSlot(const std::vector<Slot>& v, const std::string& section, int x, int y)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (SlotCmp(v[i].section, v[i].x, v[i].y, section, x, y) == 0) return (int)i;
    return -1;
}
inline Op MakeOp(int kind, const Slot& s, int qFrom, int qTo, float cFrom, float cTo)
{
    Op o;
    o.kind = kind; o.section = s.section; o.x = s.x; o.y = s.y; o.sid = s.sid;
    o.qtyFrom = qFrom; o.qtyTo = qTo; o.chFrom = cFrom; o.chTo = cTo;
    return o;
}

/* BASELINE vs CURRENT -> the ops that make the other game's copy equal to CURRENT. Order independent (both sides are sorted
   by slot first). Emitted removes first, then quantity / charges changes, then adds - so an add into a slot a remove frees,
   and a slot whose item changed kind (remove + add), both apply cleanly on the receiver. */
inline void Diff(const std::vector<Slot>& baseIn, const std::vector<Slot>& curIn, std::vector<Op>* out)
{
    out->clear();
    std::vector<Slot> b(baseIn), c(curIn);
    std::stable_sort(b.begin(), b.end(), SlotLess);
    std::stable_sort(c.begin(), c.end(), SlotLess);
    std::vector<Op> rem, mid, add;
    size_t i = 0, j = 0;
    while (i < b.size() || j < c.size())
    {
        int cmp = 0;
        if (i >= b.size()) cmp = 1;
        else if (j >= c.size()) cmp = -1;
        else cmp = SlotCmp(b[i].section, b[i].x, b[i].y, c[j].section, c[j].x, c[j].y);
        if (cmp < 0) { rem.push_back(MakeOp(kOpRemove, b[i], b[i].qty, 0, b[i].charges, b[i].charges)); ++i; continue; }
        if (cmp > 0) { add.push_back(MakeOp(kOpAdd, c[j], 0, c[j].qty, c[j].charges, c[j].charges)); ++j; continue; }
        const Slot& bs = b[i];
        const Slot& cs = c[j];
        ++i; ++j;
        if (bs.sid != cs.sid)
        {
            rem.push_back(MakeOp(kOpRemove, bs, bs.qty, 0, bs.charges, bs.charges));
            add.push_back(MakeOp(kOpAdd, cs, 0, cs.qty, cs.charges, cs.charges));
            continue;
        }
        const int chg = (Charges100(bs.charges) != Charges100(cs.charges)) ? 1 : 0;
        if (bs.qty != cs.qty)
        {
            Op o = MakeOp(kOpQty, cs, bs.qty, cs.qty, bs.charges, cs.charges);
            o.chargesChanged = chg;
            mid.push_back(o);
        }
        else if (chg != 0) mid.push_back(MakeOp(kOpCharges, cs, bs.qty, cs.qty, bs.charges, cs.charges));
    }
    out->insert(out->end(), rem.begin(), rem.end());
    out->insert(out->end(), mid.begin(), mid.end());
    out->insert(out->end(), add.begin(), add.end());
}

/* ONE PUBLISHED OP APPLIED TO A BASELINE - the receiver's own reading of each op (items.h). `qty` is the message's quantity
   field, `sid` / `charges` matter for an add only (and `charges` for kOpCharges). 1 = the baseline changed or the op was
   an add; 0 = the slot was not in the baseline (nothing to follow). */
inline int ApplyOp(std::vector<Slot>* base, int op, const std::string& section, int x, int y,
                   const std::string& sid, int qty, float charges)
{
    const int k = FindSlot(*base, section, x, y);
    if (op == kOpAdd)
    {
        Slot s; s.section = section; s.x = x; s.y = y; s.sid = sid; s.qty = (qty > 0) ? qty : 1; s.charges = charges;
        if (k >= 0) (*base)[(size_t)k] = s; else base->push_back(s);
        return 1;
    }
    if (k < 0) return 0;
    if (op == kOpRemove) { base->erase(base->begin() + k); return 1; }
    if (op == kOpQty)
    {
        if (qty <= 0) base->erase(base->begin() + k); else (*base)[(size_t)k].qty = qty;
        return 1;
    }
    if (op == kOpSplit)
    {
        const int left = (*base)[(size_t)k].qty - qty;
        if (left <= 0) base->erase(base->begin() + k); else (*base)[(size_t)k].qty = left;
        return 1;
    }
    if (op == kOpCharges) { (*base)[(size_t)k].charges = charges; return 1; }
    return 0;
}

/* A CATCH the net has dealt with (sent, or counted uncarried), folded into the baseline so it is never caught twice.
   Idempotent after a send (the send itself already followed the wire through ApplyOp). */
inline void ApplyCatch(std::vector<Slot>* base, const Op& o)
{
    if (o.kind == kOpRemove) { ApplyOp(base, kOpRemove, o.section, o.x, o.y, o.sid, 0, 0.0f); return; }
    if (o.kind == kOpAdd) { ApplyOp(base, kOpAdd, o.section, o.x, o.y, o.sid, o.qtyTo, o.chTo); return; }
    if (o.kind == kOpQty) ApplyOp(base, kOpQty, o.section, o.x, o.y, o.sid, o.qtyTo, 0.0f);
    ApplyOp(base, kOpCharges, o.section, o.x, o.y, o.sid, 0, o.chTo);
}

/* inv2b (user decision 2026-09-26): THE DIRECT STACK-COUNT WRITERS, their pure halves.
   Inventory::takeOneItemOnly 0x749FF0 lowers a source stack of MORE than one by a direct `qty -= 1` (a stack of one leaves through
   the watched removeItem instead). 1 = the survivor's absolute quantity `after` is published as the ordinary op 2; anything else
   (unchanged, a stack of one, a drop that is not exactly one) is not this write and is never guessed at. */
inline int TakeOneLowered(int before, int after)
{
    return (before > 1 && after == before - 1) ? 1 : 0;
}
/* inv2c: Inventory::buyItem 0x749AF0 (an NPC shopper: Task_Shopping 0x337CC0 -> AI::buySomething 0x9A05E0) makes a one-unit
   clone for the buyer and lowers the SELLER's stack of two or more by a direct `qty -= 1`; a stack of one goes whole through the
   watched removeItem, and every refusal puts the unit back (`qty += 1`). `gotClone` = the call returned an object that is not the
   stack itself. 1 = the stack's absolute quantity `after` is published as the ordinary op 2; anything else is never guessed at. */
inline int BuyLowered(int before, int after, int gotClone)
{
    return (gotClone != 0 && TakeOneLowered(before, after) != 0) ? 1 : 0;
}
/* Research consume 0x82DA30 replayed over the stacks its two getItem calls returned, in call order (gapB 4a): while need > 0 -
   need < qty: that stack is lowered to qty - need by a direct write (nothing rings) and the consume stops; otherwise the whole
   stack goes through the watched removeItemAutoDestroy and need -= qty. Returns the lowered stack's index (its new quantity in
   *after); -1 no stack was lowered (every one went whole, nothing was fetched, or need was met exactly); -2 the list disagrees
   with the engine's loop (a stack fetched after the lowered one) - never publish on a disagreement. */
inline int ConsumePartial(const int* qty, int n, int need, int* after)
{
    if (qty == 0 || after == 0) return -1;
    for (int i = 0; i < n; ++i)
    {
        if (need <= 0) return -1;
        if (need < qty[i])
        {
            if (i != n - 1) return -2;
            *after = qty[i] - need;
            return i;
        }
        need -= qty[i];
    }
    return -1;
}

}   /* namespace coopinv */

#endif
