// tradelock.h - T-1167 piece 3 build 2 (owner 1635 'the swap's commit through the world server'; design A): WHAT LOCK_NOW AND ABORT DO
// TO THIS GAME'S SIDE OF A PLAYER TRADE. Pure: no disk, no clock, no engine; the game plugin (tradewin.cpp) and the offline suite
// compile it.
//
// THE LOCK (tradewin.cpp LockTry, MAIN THREAD, only while money may move - StoreMoneyReady: no save in flight, no restore waiting):
//   1. LockNowCheck: the LOCK_NOW names this game's open trade, both offers' current serials and this side's money - else refused.
//   2. LockPlan: every unit of every offer row is in the character now (the rows' things, SameThing, taken from the stacks in the
//      inventory's order: whole stacks while the units left reach a stack's count, else a SPLIT of the units left off that stack), and
//      the money is in the purse. Else LOCK_REFUSED naming the FIRST row not all there (or kNoRow: the money).
//   3. The units lifted into no inventory (items.cpp ItemsTradeLift, the shop's lift: takeItemOut, a split for part of a stack); ONE
//      `held` line per lifted object written to pp.handover in ONE write (handrec.h HandTradeLine / HandTradeAddAll), on disk when it
//      returns; then the money moved into a held row of pp.money (moneywire.h HoldRow, version 3) in ONE write with the balance; LOCKED
//      {bodies, cats, transfer} once that write is on disk. Any step failing puts back what was lifted, closes its lines and refuses.
// THE GIVE-BACK (ABORT): each lifted object back into the character (else at its feet), the lines closed (one write), the held money
//   back in the purse with the row's removal (one write), APPLIED once that write is on disk. A repeated ABORT changes nothing and
//   sends APPLIED again (AbortStep).
#pragma once

#include "tradewire.h"
#include <vector>

namespace tradel {

/* one stack of this character's own items, as the lock reads them (items.cpp ItemsTradeRows): the thing and its count; v3 (owner 1677):
   obj - the stack's own object (compared only, never read through here) */
struct Stack { tradew::Item thing; unsigned qty; const void* obj; Stack() : qty(0), obj(0) {} };
/* one lift: `units` off stack `stack` for offer row `row`; whole = the stack's whole count (the same object leaves), else a split */
struct Take { unsigned row; size_t stack; unsigned units; int whole; Take() : row(0), stack(0), units(0), whole(0) {} };

enum Plan { kPlanOk = 0, kPlanMissing = 1, kPlanMoney = 2, kPlanBad = 3 };
inline const char* PlanName(int p)
{
    switch (p)
    {
    case kPlanOk: return "ok"; case kPlanMissing: return "missing"; case kPlanMoney: return "money-short"; case kPlanBad: return "bad-offer"; default: return "?";
    }
}
/* THE LOCK DECISION TABLE. o: this side's offer; inv: the character's stacks now; purse: its money now (-1 unreadable). kPlanOk with
   *takes the lifts (in row order, each row's stacks in the inventory's order); kPlanMissing with *row the first row not all there;
   kPlanMoney (*row kNoRow): the money is not all there; kPlanBad: not an offer this header would send. Rows are distinct things
   (OfferOk), so no stack is ever taken for two rows. */
inline int LockPlan(const tradew::Offer& o, const std::vector<Stack>& inv, long long purse, std::vector<Take>* takes, unsigned* row)
{
    takes->clear(); *row = tradew::kNoRow;
    if (!tradew::OfferOk(o)) return kPlanBad;
    for (size_t r = 0; r < o.items.size(); ++r)
    {
        unsigned left = o.items[r].count;
        for (size_t s = 0; s < inv.size() && left > 0; ++s)
        {
            if (inv[s].qty == 0 || !tradew::SameThing(inv[s].thing, o.items[r])) continue;
            Take t; t.row = (unsigned)r; t.stack = s; t.units = inv[s].qty <= left ? inv[s].qty : left; t.whole = t.units == inv[s].qty ? 1 : 0;
            left -= t.units;
            takes->push_back(t);
        }
        if (left > 0) { takes->clear(); *row = (unsigned)r; return kPlanMissing; }
    }
    if (o.cats > 0 && (purse < 0 || (long long)o.cats > purse)) { takes->clear(); *row = tradew::kNoRow; return kPlanMoney; }
    return kPlanOk;
}

/* LOCK_NOW against this game's state. t / side: this game's trade (side -1 none); held: the id of the lock this game holds now (0 none).
   kNowOk: lock it. kNowRepeat: the lock held now is this one - LOCKED again (once sent), nothing else. Anything else: LOCK_REFUSED
   (kNoRow) - another lock is held, the trade is not this game's open one, or a serial / the money is not this side's current one. */
enum NowCheck { kNowOk = 0, kNowRepeat = 1, kNowOtherLock = 2, kNowNotOpen = 3, kNowSerial = 4, kNowCats = 5 };
inline const char* NowName(int k)
{
    switch (k)
    {
    case kNowOk: return "ok"; case kNowRepeat: return "repeat"; case kNowOtherLock: return "another-lock-held"; case kNowNotOpen: return "not-this-game's-open-trade";
    case kNowSerial: return "serials-not-current"; case kNowCats: return "money-not-this-side's"; default: return "?";
    }
}
inline int LockNowCheck(const tradew::Trade& t, int side, const tradew::Msg& m, unsigned long long held)
{
    if (held != 0 && held == m.id) return kNowRepeat;
    if (held != 0) return kNowOtherLock;
    if (side < 0 || side > 1 || t.id != m.id || t.state != tradew::kStOpen) return kNowNotOpen;
    if (m.serialMine != t.offer[side].serial || m.serialTheirs != t.offer[1 - side].serial) return kNowSerial;
    if (m.cats != t.offer[side].cats) return kNowCats;
    return kNowOk;
}

/* the lock's life on this game (tradewin.cpp g_lock) */
enum Phase { kLkNone = 0, kLkWaitDisk = 1, kLkLocked = 2, kLkBackPending = 3, kLkBackWaitDisk = 4, kLkGivenBack = 5,
             kLkCommitPending = 6, kLkCommitWaitDisk = 7, kLkCommitted = 8 };   /* 6-8: build 3, COMMIT applied */
inline const char* PhaseName(int p)
{
    switch (p)
    {
    case kLkNone: return "none"; case kLkWaitDisk: return "written-waiting-for-disk"; case kLkLocked: return "locked";
    case kLkBackPending: return "giving-back"; case kLkBackWaitDisk: return "given-back-waiting-for-disk"; case kLkGivenBack: return "given-back";
    case kLkCommitPending: return "applying-commit"; case kLkCommitWaitDisk: return "committed-waiting-for-disk"; case kLkCommitted: return "committed"; default: return "?";
    }
}
/* the window takes no change while a lock is open (the mock-up's 'waiting...' on both panes) */
inline bool PhaseOpen(int p) { return p == kLkWaitDisk || p == kLkLocked || p == kLkBackPending || p == kLkBackWaitDisk || p == kLkCommitPending || p == kLkCommitWaitDisk; }
/* ABORT for trade `id`: kAbGive - the live lock's escrow goes back (the first ABORT for it); kAbAgain - already given back (or being
   given back): nothing changes, APPLIED again once on disk; kAbNoLock - no live lock of that id here (refused before any lift, or a
   reload since): only a held money row of that trade is given back - its lifted objects, if any, are the after-reload rule's
   (handrec.h TradeHolderAction), and APPLIED goes only when no open line of that trade is left. */
enum AbortAct { kAbGive = 0, kAbAgain = 1, kAbNoLock = 2 };
inline int AbortStep(unsigned long long lockId, int phase, unsigned long long id)
{
    if (lockId == 0 || lockId != id || phase == kLkNone) return kAbNoLock;
    if (phase == kLkWaitDisk || phase == kLkLocked) return kAbGive;
    return kAbAgain;
}


/* ---- T-1167 piece 3 build 3: COMMIT, the room check, and the road after a reload ----
   COMMIT for trade `id`: kCmApply - the live lock's (the first COMMIT for it: build theirs, close this side's lines, the money, APPLIED);
   kCmAgain - already applied or being applied: nothing changes, APPLIED again once on disk; kCmNoLock - no live lock of that id here
   (a reload / restart since the lift): the after-reload road (tradewin.cpp RecoverTick, the lines and the held row on file). */
enum CommitAct { kCmApply = 0, kCmAgain = 1, kCmNoLock = 2 };
inline int CommitStep(unsigned long long lockId, int phase, unsigned long long id)
{
    if (lockId == 0 || lockId != id || phase == kLkNone) return kCmNoLock;
    if (phase == kLkWaitDisk || phase == kLkLocked) return kCmApply;
    if (phase == kLkCommitPending || phase == kLkCommitWaitDisk || phase == kLkCommitted) return kCmAgain;
    return kCmNoLock;   /* a give-back under way or done for that id: never applied over it (one decision per lock) */
}
/* THE ROOM CHECK's refusal (owner 1641): the rows (indexes of the other side's offer) any unit of which did not fit (shopwire.h FitList
   *left, one entry per row in the offer's order) */
inline std::vector<unsigned> NoRoomRows(const std::vector<long long>& left)
{
    std::vector<unsigned> v;
    for (size_t i = 0; i < left.size(); ++i) if (left[i] > 0) v.push_back((unsigned)i);
    return v;
}
/* AFTER A RELOAD, THE OBJECTS THIS GAME RECEIVED (handrec.h's receiving line, closed with E when COMMIT was applied):
   kRvBuild   COMMIT not applied yet here (the receiving line still held): build every object of theirs
   kRvNone    applied, and this state has them: the line was written in this process since its last load (live), or a frozen check
              said so, or a record of the squad at or past E was applied (it was written after the build), or the save mark is at or
              past E (the save was asked for after the build)
   kRvRebuild applied, and this state LACKS them for sure: the squad's record is known and the last load ran under the mark rule with
              both its record and the save mark below E (both predate the build) - build them again
   kRvLook    applied, and this state cannot tell: look for each object in the character (its exact item); build only what is not found
   kRvGivenBack the line was closed with no E (the trade was given back here) - nothing to build */
enum RecvAct { kRvBuild = 0, kRvNone = 1, kRvRebuild = 2, kRvLook = 3, kRvGivenBack = 4 };
inline int RecvAfterLoad(int lineOpen, unsigned long long e, int live, int frozenHolds, int squadKnown, unsigned long long applied, int markRule, unsigned long long markSeq)
{
    if (lineOpen != 0) return kRvBuild;
    if (e == 0ULL) return kRvGivenBack;
    if (live != 0 || frozenHolds != 0) return kRvNone;
    if (applied >= e || markSeq >= e) return kRvNone;
    if (squadKnown != 0 && markRule != 0) return kRvRebuild;
    return kRvLook;
}

/* ---- T-1167 piece 3 fold 1 (reviews p3a H1 / M1 / M2 / L2) ----
   A-H1 THE APPLIED GATE. APPLIED - after which the world server forgets the trade and never sends its decision again - goes only once
   what it claims is on disk: the money write (pp.money), and THIS GAME'S OWN SAVED RECORD of the trading character's squad holding the
   result - T-889's hand-over rule (handrec.h InRecordDurable over ownstore OwnStoreCommittedSeq): the squad's last COMMITTED record at or
   past the E reserved before the last engine change of this trade (E / E_i for the objects received, E_h for the lifted ones, E_b for a
   give-back), or a same-bytes skip at or past it. Nothing to wait for: the records writer is off (no record is kept at all), or no own
   squad record names the character (T-889's kCpDropNoRecord). Only then are the trade's lines closed (a crash before leaves them for the
   after-reload rule, and the decision comes again at the next WELCOME), then APPLIED. */
inline int RecordHolds(int writerOn, int squadKnown, int durable) { return (writerOn == 0 || squadKnown == 0 || durable != 0) ? 1 : 0; }
enum Gate { kGateWaitMoney = 0, kGateWaitRecord = 1, kGateCloseLines = 2, kGateSend = 3 };
inline int AppliedGate(int moneyOnDisk, int recordHolds, int linesOpen)
{
    if (moneyOnDisk == 0) return kGateWaitMoney;
    if (recordHolds == 0) return kGateWaitRecord;
    return linesOpen != 0 ? kGateCloseLines : kGateSend;
}
/* fold 2 (re-check p3f1 MEDIUM): the gate's E after one engine change of the trade (E reserved just before it): raised only when the
   change was made (and its character marked to be written) - a one-way trade's removal after a reload then waits for a record past it */
inline unsigned long long GateERaise(unsigned long long eMax, unsigned long long e, int changed) { return (changed != 0 && eMax < e) ? e : eMax; }
/* A-M1 ONE RECEIVED OBJECT'S BUILD, tracked per object: kBdTodo not built yet (built - again - at the next round, a second later);
   kBdBuilt in the character; kBdFeet partly at its feet; kBdHere this reloaded state already has it; kBdWait the record cannot settle
   whether it has it (looked at again, never guessed); kBdGiveUp its bytes will not decode - it can NEVER be built here: given up with an
   error line naming the trade and the object (the world server's decision stands - the other side's object is gone there and nothing
   is given back for it; waiting would hold the trade open for ever). APPLIED only once every object is settled (BuildsDone). */
enum Build { kBdTodo = 0, kBdBuilt = 1, kBdFeet = 2, kBdHere = 3, kBdWait = 4, kBdGiveUp = 5 };
inline int BuildAfterTry(int decodes, int result)
{
    if (decodes == 0) return kBdGiveUp;
    return result == 1 ? kBdBuilt : (result == 2 ? kBdFeet : kBdTodo);
}
inline int BuildsDone(const std::vector<int>& st)
{
    for (size_t i = 0; i < st.size(); ++i) if (st[i] == kBdTodo || st[i] == kBdWait) return 0;
    return 1;
}
/* A-M2 AFTER A RELOAD, ONE RECEIVED OBJECT, from WHERE ITS BUILD LANDED (handrec.h's placed mark: E_i reserved just before its build and
   the mark written in the same main-thread call, so no record of this game carries the object without the mark on disk, and every
   record at or past E_i carries it). recvOpen / recvE: the receiving line (open: COMMIT not applied yet; closed with E 0: given back);
   no placed mark on an applied COMMIT: never built with its mark - build it. Else RecvAfterLoad on E_i. Never a search for the
   object's identity and count anywhere in the character. Fold 2 (re-check p3f1 HIGH): recvDone - the receiving line marked DONE when the
   APPLIED gate closed the lines (handrec.h HandTradeCloseAll, which also takes the placed marks out): nothing, ever (a COMMIT sent again
   after a crash would otherwise read "no placed mark" as "never built"). */
inline int RecvObjAct(int recvOpen, unsigned long long recvE, int recvDone, int havePlaced, unsigned long long ei, int live, int squadKnown, unsigned long long applied,
                      int markRule, unsigned long long markSeq)
{
    if (recvOpen != 0) return kRvBuild;
    if (recvE == 0ULL) return kRvGivenBack;
    if (recvDone != 0) return kRvNone;
    if (havePlaced == 0 || ei == 0ULL) return kRvBuild;
    return RecvAfterLoad(0, ei, live, 0, squadKnown, applied, markRule, markSeq);
}
/* kRvLook settled ONLY at the square the placed mark names (handrec.h PlacedLook, through items.cpp HovLiveCopy wide 0): 1 the stack the
   build made is there - nothing built; 0 not placed - built; -1 the record cannot settle it - WAIT (logged), never a guess */
inline int RecvLookState(int look) { return look > 0 ? kBdHere : (look == 0 ? kBdTodo : kBdWait); }
inline int RecvActState(int act) { return act == kRvNone ? kBdHere : ((act == kRvBuild || act == kRvRebuild) ? kBdTodo : kBdWait); }
/* A-L2 THE 'SENT YOU' LINE LEFT OUT for a trade's money: while this trade's lock is open here (LOCKED .. COMMIT applied, not after) and
   the delivery is the other side's player and amount, or - at any time - by the inbox number COMMIT named */
inline int QuietMoney(int lockOpen, int slotSame, int amountSame, int inboxSame)
{
    if (inboxSame != 0) return 1;
    return (lockOpen != 0 && slotSame != 0 && amountSame != 0) ? 1 : 0;
}

/* ---- T-1167 v3 (owner 1677, mock-up v3 section 2): THE EXACT ITEMS ----
   Each offer row keeps the item OBJECTS it came from and the units taken from each. The wire still carries things and counts (an
   offer row = one thing, its count = the units of all its objects); the objects stay on this game. A whole-object drag (or a Shift /
   Ctrl right-click) sets that object's units to its whole count - an object already wholly offered adds nothing; a one-unit drag (or
   a plain right-click) adds one unit from THAT object, never past its count. Two objects of one thing are one row with two objects;
   a different quality is a different thing (tradewire.h SameThing), so a different row. Every read of the character's items re-checks
   each object (the pointer AND its thing) and trims what is gone; the lock lifts exactly those objects and units. */
struct ExObj { const void* obj; unsigned units; ExObj() : obj(0), units(0) {} ExObj(const void* o, unsigned u) : obj(o), units(u) {} };
/* one offer row: its thing (name: the engine's own), the game's grade facts for the row's shown name (type, level, the weapon's model),
   a row id that never changes while the row lives (the list's click picks by it), and its objects */
struct ExRow
{
    tradew::Item thing; int type, level; std::string model; unsigned id; std::vector<ExObj> objs;
    ExRow() : type(-1), level(0), id(0) {}
};
/* one object the character holds now: the object, its thing, its count */
struct ExHeld { const void* obj; tradew::Item thing; unsigned qty; ExHeld() : obj(0), qty(0) {} };
inline unsigned ExCount(const ExRow& r)
{
    unsigned long long n = 0;
    for (size_t i = 0; i < r.objs.size(); ++i) n += r.objs[i].units;
    return n > tradew::kMaxCount ? tradew::kMaxCount : (unsigned)n;
}
inline int ExFindRow(const std::vector<ExRow>& rows, const tradew::Item& thing)
{
    for (size_t i = 0; i < rows.size(); ++i) if (tradew::SameThing(rows[i].thing, thing)) return (int)i;
    return -1;
}
inline int ExFindId(const std::vector<ExRow>& rows, unsigned id)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return (int)i;
    return -1;
}
/* THE ONE ADD (a drag or a right-click): object `obj` of `thing`, holding `qty` now; `units` asked (a plain drag / right-click 1),
   whole: the whole object. `proto` gives a new row its grade facts; *nextId the next row id. 1 changed, 0 nothing to add (already
   offered, or nothing held), -1 refused (a bad thing, a new row past kMaxItems, a count past kMaxCount) */
inline int ExactAdd(std::vector<ExRow>* rows, const ExRow& proto, const void* obj, unsigned qty, unsigned units, bool whole, unsigned* nextId)
{
    if (obj == 0 || !tradew::ThingOk(proto.thing)) return -1;
    if (qty == 0) return 0;
    int at = ExFindRow(*rows, proto.thing);
    unsigned cur = 0; int oi = -1;
    if (at >= 0)
        for (size_t k = 0; k < (*rows)[(size_t)at].objs.size(); ++k) if ((*rows)[(size_t)at].objs[k].obj == obj) { oi = (int)k; cur = (*rows)[(size_t)at].objs[k].units; }
    unsigned want = whole ? qty : cur + (units > 0 ? units : 1u);
    if (want < cur || want > qty) want = qty;   /* never past the object's count (and no wrap) */
    if (want <= cur) return 0;
    if (at < 0)
    {
        if (rows->size() >= tradew::kMaxItems) return -1;
        ExRow r = proto; r.thing.count = 0; r.objs.clear(); r.id = (*nextId)++;
        if (*nextId == 0) *nextId = 1;
        rows->push_back(r);
        at = (int)rows->size() - 1;
    }
    ExRow& row = (*rows)[(size_t)at];
    if ((unsigned long long)ExCount(row) - cur + want > tradew::kMaxCount) return -1;
    if (oi >= 0) row.objs[(size_t)oi].units = want;
    else row.objs.push_back(ExObj(obj, want));
    return 1;
}
/* RE-CHECK every object against what the character holds now (the pointer AND its thing, as the row has it): an object gone or no
   longer that thing leaves its row, an object holding fewer units comes down to them, a row with no object left goes. 1 trimmed. */
inline int ExactTrim(std::vector<ExRow>* rows, const std::vector<ExHeld>& held)
{
    int n = 0;
    for (size_t r = rows->size(); r-- > 0; )
    {
        ExRow& row = (*rows)[r];
        for (size_t k = row.objs.size(); k-- > 0; )
        {
            unsigned have = 0; bool found = false;
            for (size_t h = 0; h < held.size() && !found; ++h)
                if (held[h].obj == row.objs[k].obj && tradew::SameThing(held[h].thing, row.thing)) { found = true; have = held[h].qty; }
            if (!found || have == 0) { row.objs.erase(row.objs.begin() + (long)k); ++n; }
            else if (have < row.objs[k].units) { row.objs[k].units = have; ++n; }
        }
        if (row.objs.empty()) { rows->erase(rows->begin() + (long)r); ++n; }
    }
    return n > 0 ? 1 : 0;
}
/* the TEST-ONLY lever's set (`tradetest offer <key|name> <n>`): the row of `proto.thing` set to n units taken from the held objects of
   that thing in the inventory's order (0 takes the row out). 1 changed, 0 the same, -1 refused (fewer held than n, or ExactAdd's) */
inline int ExactSetFromHeld(std::vector<ExRow>* rows, const ExRow& proto, unsigned n, const std::vector<ExHeld>& held, unsigned* nextId)
{
    std::vector<ExRow> next = *rows;
    const int at = ExFindRow(next, proto.thing);
    const unsigned before = at >= 0 ? ExCount(next[(size_t)at]) : 0u;
    if (at >= 0) next.erase(next.begin() + at);
    unsigned left = n;
    for (size_t h = 0; h < held.size() && left > 0; ++h)
    {
        if (held[h].qty == 0 || !tradew::SameThing(held[h].thing, proto.thing)) continue;
        const unsigned u = held[h].qty <= left ? held[h].qty : left;
        if (ExactAdd(&next, proto, held[h].obj, held[h].qty, u, u == held[h].qty, nextId) < 0) return -1;
        left -= u;
    }
    if (left > 0) return -1;
    if (at >= 0 && n > 0)
    {   /* the row keeps its place and its id */
        const int now = ExFindRow(next, proto.thing);
        if (now >= 0) { ExRow r = next[(size_t)now]; r.id = (*rows)[(size_t)at].id; next.erase(next.begin() + now); next.insert(next.begin() + at, r); }
    }
    const bool same = before == n;
    *rows = next;
    return same ? 0 : 1;
}
/* THE DRAGGED OBJECT after the engine put the cursor item back (MouseReturnItem): `before` / `after` the character's objects either side of
   the put-back, cursorObj the item that was on the cursor (thing: its thing), onCursor its count. A whole-object drag carried the object
   itself: it is back - *whole 1 only when it holds exactly the cursor's count (fold 1, review LOW-1: anything the engine merged into it
   since makes it a units add of the cursor's count from that object, *whole 0). A one-unit drag carried a copy that merged back onto a
   stack of that thing: the stack that grew by the cursor's count is the one (*whole 0; the first that grew, if none grew by exactly
   that). beforeRead false (fold 1, review LOW-2: the read BEFORE the put-back failed - every stack would look grown): 0. 0 = not found
   (nothing is offered). */
inline const void* ExReturned(const std::vector<ExHeld>& before, bool beforeRead, const std::vector<ExHeld>& after, const void* cursorObj, const tradew::Item& thing, unsigned onCursor, unsigned* qtyNow, int* whole)
{
    *qtyNow = 0; *whole = 0;
    if (!beforeRead) return 0;
    for (size_t a = 0; a < after.size(); ++a)
        if (cursorObj != 0 && after[a].obj == cursorObj && tradew::SameThing(after[a].thing, thing)) { *qtyNow = after[a].qty; *whole = after[a].qty == onCursor ? 1 : 0; return cursorObj; }
    const void* first = 0; unsigned firstQ = 0;
    for (size_t a = 0; a < after.size(); ++a)
    {
        if (!tradew::SameThing(after[a].thing, thing)) continue;
        unsigned was = 0;
        for (size_t b = 0; b < before.size(); ++b) if (before[b].obj == after[a].obj) { was = before[b].qty; break; }
        if (after[a].qty <= was) continue;
        if (after[a].qty - was == onCursor) { *qtyNow = after[a].qty; return after[a].obj; }
        if (first == 0) { first = after[a].obj; firstQ = after[a].qty; }
    }
    *qtyNow = firstQ;
    return first;
}
/* fold 1 (review LOW-3): object `obj` of `thing` among the character's objects now - 1 held (*qty its count), 0 not held (nothing is
   offered: an object the character's own list does not name never goes on the wire) */
inline int ExHeldQty(const std::vector<ExHeld>& held, const void* obj, const tradew::Item& thing, unsigned* qty)
{
    *qty = 0;
    if (obj == 0) return 0;
    for (size_t h = 0; h < held.size(); ++h)
        if (held[h].obj == obj && held[h].qty > 0 && tradew::SameThing(held[h].thing, thing)) { *qty = held[h].qty; return 1; }
    return 0;
}
/* THE LOCK'S PLAN FROM THE EXACT OBJECTS: each offer row's own objects (rows[r] names the same thing as o.items[r]; its units sum to
   the row's count), each found in `inv` by its object AND its thing with at least its units; never another stack, never one stack
   twice. kPlanOk with *takes (row order, each row's objects in its order; whole when the units are the stack's whole count);
   kPlanMissing with *row the first row not all there; kPlanMoney; kPlanBad. */
inline int LockPlanExact(const tradew::Offer& o, const std::vector<ExRow>& rows, const std::vector<Stack>& inv, long long purse, std::vector<Take>* takes, unsigned* row)
{
    takes->clear(); *row = tradew::kNoRow;
    if (!tradew::OfferOk(o)) return kPlanBad;
    std::vector<char> used(inv.size(), 0);
    for (size_t r = 0; r < o.items.size(); ++r)
    {
        const bool rowOk = r < rows.size() && tradew::SameThing(rows[r].thing, o.items[r]) && ExCount(rows[r]) == o.items[r].count;
        bool all = rowOk;
        for (size_t k = 0; all && k < rows[r].objs.size(); ++k)
        {
            const ExObj& e = rows[r].objs[k];
            size_t s = 0;
            for (; s < inv.size(); ++s) if (used[s] == 0 && inv[s].obj == e.obj && e.obj != 0 && tradew::SameThing(inv[s].thing, o.items[r])) break;
            if (s >= inv.size() || e.units == 0 || inv[s].qty < e.units) { all = false; break; }
            used[s] = 1;
            Take t; t.row = (unsigned)r; t.stack = s; t.units = e.units; t.whole = e.units == inv[s].qty ? 1 : 0;
            takes->push_back(t);
        }
        if (!all) { takes->clear(); *row = (unsigned)r; return kPlanMissing; }
    }
    if (o.cats > 0 && (purse < 0 || (long long)o.cats > purse)) { takes->clear(); *row = tradew::kNoRow; return kPlanMoney; }
    return kPlanOk;
}

}   /* namespace tradel */
