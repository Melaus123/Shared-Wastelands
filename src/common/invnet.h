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
#include <map>

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
/* T-608 / T-534 (F1042): AN NPC PURCHASE RUNS ONLY WHERE IT CAN BE PUBLISHED. Inventory::buyItem is the whole purchase (the unit,
   the cats on both sides); a purchase from a box this game does not write cannot be published (the drain drops it and the box
   differs for good), and a copy's purchase belongs to its own game. Returning 0 without running the engine's purchase is the
   engine's own refusal: nothing moves, no money changes.
   NpcBuyBoxClass names the seller: `keyed` = a box this mod can name (a keyed building, linked game); `mine` = its verdict is
   "this game writes it"; `offMainWait` = the verdict is the off-main-thread "owner not readable here" wait (ItBoxVerdict's
   UnreadRung defer); `ownerUnread` = the box's owner class was never read on the main thread. */
/* T-608 (owner decision 971 b): `peerOwned` = another player's counter whose own game writes it (on the main thread: the owner rung
   says held by its owner; off it: the owner class last read is another player's) - the purchase is SENT to that game, which makes
   the sale (kBuyBoxPeerOwner). */
const int kBuyBoxNone = 0, kBuyBoxMine = 1, kBuyBoxNotMine = 2, kBuyBoxUnread = 3, kBuyBoxPlayerWait = 4, kBuyBoxPeerOwner = 5;
inline int NpcBuyBoxClass(int keyed, int mine, int offMainWait, int ownerUnread, int peerOwned = 0)
{
    if (keyed == 0) return kBuyBoxNone;
    if (mine != 0) return kBuyBoxMine;
    if (peerOwned != 0) return kBuyBoxPeerOwner;
    if (offMainWait == 0) return kBuyBoxNotMine;   /* another game writes it, or nobody does now: refused on every game */
    return (ownerUnread != 0) ? kBuyBoxUnread : kBuyBoxPlayerWait;
}
/* kBuyRun: the engine's purchase runs. kBuyRefuseCopy: the buyer is a copy of another game's character. kBuyRefuseNotWriter: the
   box is not this game's to write. kBuyRefuseLearn: refused, and the main thread is asked to read the box's owner class (a
   refused purchase rings nothing that would read it, so without the ask the box would be refused on every game for good).
   kBuyRunPlayerBox: a player-owned box off the main thread - its writer is decided by the owner rung, which reads only on the
   main thread; the purchase runs and the drain decides, as before this gate (counted apart). The mod's own applies always run. */
/* kBuySendOwner (T-608, decision 971 b): the local purchase is refused and the purchase is sent to the counter owner's game. */
const int kBuyRun = 0, kBuyRefuseCopy = 1, kBuyRefuseNotWriter = 2, kBuyRefuseLearn = 3, kBuyRunPlayerBox = 4, kBuySendOwner = 5;
inline int NpcBuyGate(int applying, int copyBuyer, int boxClass)
{
    if (applying != 0) return kBuyRun;
    if (copyBuyer != 0) return kBuyRefuseCopy;
    if (boxClass == kBuyBoxNone || boxClass == kBuyBoxMine) return kBuyRun;
    if (boxClass == kBuyBoxUnread) return kBuyRefuseLearn;
    if (boxClass == kBuyBoxPlayerWait) return kBuyRunPlayerBox;
    if (boxClass == kBuyBoxPeerOwner) return kBuySendOwner;
    return kBuyRefuseNotWriter;
}
inline int NpcBuyRefused(int gate) { return (gate == kBuyRefuseCopy || gate == kBuyRefuseNotWriter || gate == kBuyRefuseLearn || gate == kBuySendOwner) ? 1 : 0; }
/* T-608: ONE VISIT, ONE COUNT. A refused purchase sends the shopper on to another item (AI::buySomething bumps its `bought` before
   the purchase, so one visit makes several tries). A try is a NEW visit unless the same buyer was refused at the same box within
   `windowMs` (wrap-safe ticks). */
inline int NpcBuyNewVisit(int sameBuyer, int sameKey, unsigned int lastMs, unsigned int nowMs, unsigned int windowMs)
{
    if (sameBuyer == 0 || sameKey == 0) return 1;
    return ((unsigned int)(nowMs - lastMs) >= windowMs) ? 1 : 0;
}
/* T-608: ONE PURCHASE IN FLIGHT PER NPC. busy[0..cap) holds the buyers with a purchase sent and not yet answered (0 = a free slot).
   >= 0: admitted into that slot; kNbAdmitSame: this buyer already has one in flight (its other tries are refused quietly);
   kNbAdmitFull: no slot free (refused, counted). */
const int kNbAdmitSame = -1, kNbAdmitFull = -2;
inline int NpcBuyAdmit(const void* const* busy, int cap, const void* buyer)
{
    int freeAt = kNbAdmitFull;
    for (int i = 0; i < cap; ++i)
    {
        if (busy[i] == buyer && buyer != 0) return kNbAdmitSame;
        if (busy[i] == 0 && freeAt < 0) freeAt = i;
    }
    return freeAt;
}
/* T-608: THE BUYING GAME'S MAIN-THREAD CHECK before the request goes. Sent only when the counter resolves here, the link is up,
   the owner rung says the counter's OWNER's game writes it (heldByOwner) and its owner is another player (peerOwned). Anything
   else - owner offline, area not loaded there, no owner resolved - is refused: never a local sale that pays a stand-in. */
/* fold 2 (review M1): the engine's own two checks run HERE, before the owner sells, as Inventory::buyItem runs them before anything
   moves - budgetRead: the NPC's money read (it rides the request as its budget; the owner refuses a unit price above it); roomOk:
   1 the NPC's inventory takes one unit, 0 it does not ("No room"), -1 the item is no longer on this game's copy of the counter. */
const int kNbSend = 0, kNbNoCounter = 1, kNbNoLink = 2, kNbNotOwnerGame = 3, kNbBuyerGone = 4, kNbNoBudget = 5, kNbNoRoom = 6,
          kNbNotOnCounter = 7, kNbReasons = 8;
inline int NpcBuySendDecide(int buyerLive, int counterFound, int linked, int heldByOwner, int peerOwned, int budgetRead = 1, int roomOk = 1)
{
    if (buyerLive == 0) return kNbBuyerGone;
    if (counterFound == 0) return kNbNoCounter;
    if (linked == 0) return kNbNoLink;
    if (heldByOwner == 0 || peerOwned == 0) return kNbNotOwnerGame;
    if (budgetRead == 0) return kNbNoBudget;
    if (roomOk < 0) return kNbNotOnCounter;
    if (roomOk == 0) return kNbNoRoom;
    return kNbSend;
}
/* A refusal storm stays readable: say refusal number `n` (1-based) when it is one of the first `first`, then every `every`th. */
inline int RefusalSay(long long n, long long first, long long every)
{
    if (n <= 0) return 0;
    if (n <= first) return 1;
    return (every > 0 && n % every == 0) ? 1 : 0;
}
/* The per-box re-ask limiter (ItParityTick step 3, which every dropped box change now goes through): 1 = a key last asked at
   `last` (have = 0: never asked) may be asked again at `now`. Millisecond ticks, wrap-safe; it ages out by time alone. */
inline int ReaskDue(int have, unsigned int last, unsigned int now, unsigned int gapMs)
{
    return (have == 0 || (unsigned int)(now - last) >= gapMs) ? 1 : 0;
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

/* A hand-back answered with contents: the holder's rows go in and the moves this game made on the box while the hand-back
   waited are laid on top, never erased. Per item record (sid) the local difference is its count now less its count in the
   baseline saved when the hand-back asked.
   - more now: this game's rows of the record that the baseline has not at that cell go in first (each at its own cell, up to the
     difference); what is left grows a holder row of the record; with none, this game's other rows of the record are used.
   - fewer now: taken from the holder's rows of the record, last rows first, never below zero (a record the holder has not is
     simply not there to take).
   out, in laying order: the holder's rows (from 0, index into holder) with their counts first - one layout, so they fit together;
   a holder row taken to zero is left out - then this game's rows (from 1, index into now), each laid at its own cell or, when that
   is taken, a free one (HandbackLayout). 1 = there was a local difference, 0 = none (out is the holder's rows as they are). */
struct MergeRow { int from; size_t index; int qty; };
inline int HandbackMerge(const std::vector<Slot>& holder, const std::vector<Slot>& base, const std::vector<Slot>& now,
                         std::vector<MergeRow>* out)
{
    out->clear();
    std::map<std::string, long long> delta;
    for (size_t j = 0; j < now.size(); ++j) delta[now[j].sid] += now[j].qty;
    for (size_t i = 0; i < base.size(); ++i) delta[base[i].sid] -= base[i].qty;
    std::vector<int> hq(holder.size());
    for (size_t i = 0; i < holder.size(); ++i) hq[i] = holder[i].qty;
    std::vector<MergeRow> adds;
    int changed = 0;
    for (std::map<std::string, long long>::const_iterator d = delta.begin(); d != delta.end(); ++d)
    {
        if (d->second == 0) continue;
        changed = 1;
        if (d->second < 0)
        {
            long long take = -d->second;
            for (size_t k = holder.size(); k > 0 && take > 0; --k)
            {
                const size_t i = k - 1;
                if (holder[i].sid != d->first || hq[i] <= 0) continue;
                const int t = (take < (long long)hq[i]) ? (int)take : hq[i];
                hq[i] -= t; take -= t;
            }
            continue;
        }
        long long give = d->second;
        for (int pass = 0; pass < 2 && give > 0; ++pass)
        {
            for (size_t j = 0; j < now.size() && give > 0; ++j)
            {
                if (now[j].sid != d->first || now[j].qty <= 0) continue;
                int inBase = 0;
                for (size_t i = 0; i < base.size(); ++i)
                    if (base[i].sid == now[j].sid && base[i].section == now[j].section && base[i].x == now[j].x && base[i].y == now[j].y) { inBase = 1; break; }
                if ((pass == 0) != (inBase == 0)) continue;   /* the first pass takes rows new since the ask, the second the others */
                const int g = (give < (long long)now[j].qty) ? (int)give : now[j].qty;
                MergeRow r; r.from = 1; r.index = j; r.qty = g;
                adds.push_back(r); give -= g;
            }
            if (pass == 0)
                for (size_t i = 0; i < holder.size() && give > 0; ++i)
                    if (holder[i].sid == d->first && hq[i] > 0) { hq[i] += (int)give; give = 0; }
        }
    }
    for (size_t i = 0; i < holder.size(); ++i)
        if (hq[i] > 0) { MergeRow r; r.from = 0; r.index = i; r.qty = hq[i]; out->push_back(r); }
    out->insert(out->end(), adds.begin(), adds.end());
    return changed;
}

/* A move this game made on a box before it saved the box's baseline for a hand-back is already in that baseline; it is taken back
   out here, so the merge and the safety net see it as a move made during the wait. Each op as the drain's ring holds it:
     op 0 ADD     - the row at the cell (same record) leaves the baseline;
     op 1 REMOVE  - the removed stack (record, count) comes back at its cell, which must be free in the baseline;
     op 2 QTY     - the row's count goes back by the change (qtyBefore = the stack's count before it, 0 = not read);
     op 3 SPLIT   - the units taken out go back onto the row;
     op 4 CHARGES - nothing to take back (the merge counts items, not charges).
   1 = taken back; 0 = it could not be (the caller then does not trust the baseline for a merge). */
inline int UnapplyOp(std::vector<Slot>* base, int op, const std::string& section, int x, int y, const std::string& sid, int qty,
                     int qtyBefore)
{
    const int k = FindSlot(*base, section, x, y);
    if (op == kOpAdd)
    {
        if (k < 0 || (*base)[(size_t)k].sid != sid) return 0;
        base->erase(base->begin() + k);
        return 1;
    }
    if (op == kOpRemove)
    {
        if (k >= 0 || sid.empty() || qty <= 0) return 0;
        Slot s; s.section = section; s.x = x; s.y = y; s.sid = sid; s.qty = qty;
        base->push_back(s);
        return 1;
    }
    if (op == kOpQty)
    {
        if (k < 0 || qtyBefore <= 0) return 0;
        const int q = (*base)[(size_t)k].qty - qty + qtyBefore;
        if (q <= 0) return 0;
        (*base)[(size_t)k].qty = q;
        return 1;
    }
    if (op == kOpSplit)
    {
        if (k < 0 || qty <= 0) return 0;
        (*base)[(size_t)k].qty += qty;
        return 1;
    }
    return (op == kOpCharges) ? 1 : 0;
}

/* THE MERGED BOX LAID OUT BEFORE ANYTHING IS REMOVED. secs: the box's sections with their grids (w x h cells); holder: the holder's
   rows at their own cells; ours: this game's rows in laying order, each with its footprint (w x h). The holder's rows go in first
   and must all fit where they are (one layout); each of ours then goes at its own cell when that is free, else at the first free
   cell of its section in the engine's own order (y outer, x inner), and its x,y are set to where it goes.
   1 = the whole set fits; 0 = it does not (a row off its grid, a section the box has not, an overlap among the holder's rows, or
   no free cell for one of ours) - the caller removes nothing. */
struct LaySec { std::string name; int w, h; LaySec() : w(0), h(0) {} };
struct LayItem { std::string section; int x, y, w, h; LayItem() : x(0), y(0), w(0), h(0) {} };
inline int LaySecIndex(const std::vector<LaySec>& secs, const std::string& name)
{
    for (size_t s = 0; s < secs.size(); ++s)
        if (secs[s].name == name) return (int)s;
    return -1;
}
inline int LayFree(const std::vector<char>& g, int gw, int gh, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > gw || y + h > gh) return 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (g[(size_t)((y + j) * gw + (x + i))] != 0) return 0;
    return 1;
}
inline void LayMark(std::vector<char>* g, int gw, int x, int y, int w, int h)
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) (*g)[(size_t)((y + j) * gw + (x + i))] = 1;
}
inline int HandbackLayout(const std::vector<LaySec>& secs, const std::vector<LayItem>& holder, std::vector<LayItem>* ours)
{
    std::vector<std::vector<char> > grid(secs.size());
    for (size_t s = 0; s < secs.size(); ++s)
    {
        if (secs[s].w < 1 || secs[s].h < 1 || secs[s].w > 256 || secs[s].h > 256 || secs[s].w * secs[s].h > 4096) return 0;
        grid[s].assign((size_t)(secs[s].w * secs[s].h), 0);
    }
    for (size_t i = 0; i < holder.size(); ++i)
    {
        const LayItem& it = holder[i];
        const int s = LaySecIndex(secs, it.section);
        if (s < 0) return 0;
        const LaySec& sc = secs[(size_t)s];
        if (LayFree(grid[(size_t)s], sc.w, sc.h, it.x, it.y, it.w, it.h) == 0) return 0;
        LayMark(&grid[(size_t)s], sc.w, it.x, it.y, it.w, it.h);
    }
    for (size_t i = 0; i < ours->size(); ++i)
    {
        LayItem& o = (*ours)[i];
        const int s = LaySecIndex(secs, o.section);
        if (s < 0) return 0;
        const LaySec& sc = secs[(size_t)s];
        int fx = -1, fy = -1;
        if (LayFree(grid[(size_t)s], sc.w, sc.h, o.x, o.y, o.w, o.h) != 0) { fx = o.x; fy = o.y; }
        for (int y = 0; fx < 0 && y < sc.h; ++y)
            for (int x = 0; fx < 0 && x < sc.w; ++x)
                if (LayFree(grid[(size_t)s], sc.w, sc.h, x, y, o.w, o.h) != 0) { fx = x; fy = y; }
        if (fx < 0) return 0;
        o.x = fx; o.y = fy;
        LayMark(&grid[(size_t)s], sc.w, fx, fy, o.w, o.h);
    }
    return 1;
}

}   /* namespace coopinv */

#endif
