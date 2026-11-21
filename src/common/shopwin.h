#pragma once
/* T-1451 (fix attempt 1, design A): AN OPEN SHOP WINDOW RUN BY ANOTHER PLAYER'S GAME KEEPS ITS GOODS IN PLACE THROUGH A SALE.
   Before: every settled trade rebuilt the trade window's stand-in (the engine's updateInventory 0x951A00 empties it and re-adds every
   counter item in list order - every item moves) and re-asked the holder for every touched counter with the digest from BEFORE the
   sale's own change had landed, so the holder always sent the whole box and the answer was swapped in and rebuilt again (F1573).
   Now (the decisions below, offline-tested):
   - SettleWindowAct: a sale's sold units go into the open window IN PLACE (the window's own add, as single player's sale adds to the
     window's first free spot or merges a stack); a buy's units already left the window at the click (ShopStandInTakePod), so an ok
     buy changes nothing more; only a refused / short buy (whose display copy left at the click) or a caravan's window is rebuilt.
   - InPlaceOk: the in-place add is used only with an empty cursor and when every sold unit went in; else the rebuild, as before.
   - Design 3 (save numbers, below): each touched counter is asked of its holder only once the holder's numbered change to it has
     arrived here, and the open window is rebuilt once when it is stale and nothing of its trades is on its way - never by a timer.
   - AnswerAlreadyHere: an answer whose contents this game's live box already holds is not swapped in and the window not rebuilt.
   Pure: no engine, no game. */
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace shopwin {

/* ---- the settle's window act ---- */
const int kWinNothing = 0;      /* the window stays as it is */
const int kWinAddInPlace = 1;   /* the sold units go into the open window through its own add (no clear, no re-add) */
const int kWinRebuild = 2;      /* the stand-in is rebuilt from the counters (0x951A00) - every item re-laid */
/* sameWindow: the open trade window is still this trade's (its trader, its stand-in). caravan: the stock is a caravan's packs (its
   window follows the holder's pack moves - unchanged, rebuilt). dir: 0 buy, 1 sale. ok: the holder applied it. units: the units the
   holder moved (cf.shop.n). asked: the units this game asked for (the buy's display copy that left at the click held them).
   shownTaken: a buy's display copy left the window at the click (ShopStandInTakePod answered 1). rebuiltSince (fold 2, review): the
   window was rebuilt from the counters between the send and the settle (a deferred rebuild, a mismatch swap) - what it shows already
   holds or lacks this trade, so only a rebuild from the counters is right. forcedRefusal (fold 2): the holder applied the trade but this
   game settles it as a refusal (its save hold could not be taken out) - the window shows the holder's result, by a rebuild.
   reopened (re-check fold 1): the player closed and reopened the trade window between the send and the settle (WindowReopened) - the
   game refilled it from the counters itself, so like rebuiltSince only a rebuild from the counters is right (an add could double it). */
inline int SettleWindowAct(int sameWindow, int caravan, int dir, int ok, int units, int asked, int shownTaken, int rebuiltSince, int forcedRefusal,
                           int reopened)
{
    if (sameWindow == 0) return kWinNothing;
    if (caravan != 0 || rebuiltSince != 0 || forcedRefusal != 0 || reopened != 0) return kWinRebuild;
    if (dir != 0) return (ok != 0 && units > 0) ? kWinAddInPlace : kWinNothing;   /* a refused sale: the escrow went back, the window never changed */
    if (shownTaken == 0) return (ok != 0) ? kWinRebuild : kWinNothing;   /* the window still shows the bought units: an ok takes them by the rebuild */
    if (ok != 0 && units >= asked) return kWinNothing;   /* the bought units left the window at the click */
    return kWinRebuild;   /* a refused (or short) buy: the display copy that left at the click is put back from the counters */
}
/* the in-place add's own outcome: made = the display copy was made; added = ItAddAnywhereCount's answer (1 every unit placed);
   cursorBusy = an item is on the cursor (it may be one of this window's: the window is not touched under it). 1 = done in place;
   0 = the rebuild (or its deferral to an empty cursor), as before T-1451. */
inline int InPlaceOk(int cursorBusy, int made, int added)
{
    return (cursorBusy == 0 && made != 0 && added == 1) ? 1 : 0;
}

/* ---- THE WINDOW AGAINST ITS COUNTERS (design 3: a LOG ONLY compare - SameUnits) ----
   (was fold 1's fourth form, retired 2026-11-20 by design 3 below: the compare decided a rebuild once nothing was counted on its way)
   The open window's units of every item type, summed over stacks, against the counter sections it is built from (its stand-in's
   section map, each section once), read live. Design 3 logs it at each answer and rebuild - it decides nothing. */
const int kCmpEqual = 0;     /* the window shows what the counters hold: nothing */
const int kCmpRebuild = 1;   /* they differ (or did not read): the window is rebuilt from the counters */
typedef std::map<std::string, long long> UnitsByType;
/* equal, a type with 0 units counting as absent */
inline int SameUnits(const UnitsByType& a, const UnitsByType& b)
{
    for (UnitsByType::const_iterator it = a.begin(); it != a.end(); ++it)
    {
        if (it->second == 0) continue;
        UnitsByType::const_iterator f = b.find(it->first);
        if (f == b.end() || f->second != it->second) return 0;
    }
    for (UnitsByType::const_iterator it = b.begin(); it != b.end(); ++it)
    {
        if (it->second == 0) continue;
        UnitsByType::const_iterator f = a.find(it->first);
        if (f == a.end() || f->second != it->second) return 0;
    }
    return 1;
}
inline int WindowVsCounters(int read, const UnitsByType& win, const UnitsByType& counters)
{
    if (read == 0) return kCmpRebuild;
    return (SameUnits(win, counters) != 0) ? kCmpEqual : kCmpRebuild;
}

/* ---- DESIGN 3 (T-1451, design read 2026-11-20; review fold d3b): SAVE NUMBERS - when the holder's change has arrived is KNOWN ----
   The holder numbers every box move it sends, per box (SeqNext 1, 2, 3 ... within its launch; the ITEM_MOVE 'BSQ1' trailer carries its
   launch id and the number, announced by the op byte's bit 6). Its shop answer waits until the sale's moves have left its ring and names,
   per counter, the number just BEFORE the sale's first move and the number AFTER its last (SeqSaleRange: moves rung during the apply or
   sent inside it are the sale's). This game keeps per counter and writer the highest number arrived and, from each ok answer, the
   own range (before, after] it still needs. A landing on a counter of the open window while a trade on that window waits is set ASIDE
   and judged at that trade's answer: inside the answer's range = its own; at or below 'before', above 'after', or on a counter the
   answer does not name = another player's (stale). With nothing waiting, a landing outside an own range is another player's. The
   window is marked STALE also when refilled while something of it is on its way; a refill with nothing on its way clears the mark (the
   window then shows the counters). It is rebuilt ONCE when stale, no trade on it waits and every needed number arrived. A parity answer
   that re-bases a box settles its needed numbers; its set-aside landings stay for the waiting trade's answer (re-check F1604). Closing the window clears the needed numbers, the aside list and the mark; a new
   launch id from a writer clears that writer's numbers. Pure. */
typedef std::pair<std::string, int> SeqId;   /* counter key, writer slot */
const size_t kSeqArrivedCap = 1024;          /* arrived entries kept (a full table drops its first) */
const size_t kSeqAskCap = 32;                /* after-sale asks waiting (a full table drops its first, counted) */
const size_t kSeqAsideCap = 64;              /* landings set aside while a trade waits (one more = stale: too many to judge) */
const int kMoveSeqFlag = 0x40;               /* ITEM_MOVE: the op byte's bit 6 = a 'BSQ1' trailer, last of all */
const unsigned int kSeqTag = 0x31515342u;    /* 'BSQ1' little-endian */
const size_t kSeqTagLen = 12;                /* tag, the writer's launch id, the number */
inline void PutSeqU32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)((v >> (8 * i)) & 0xFFu)); }
inline unsigned int GetSeqU32(const char* p) { unsigned int v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | (unsigned int)(unsigned char)p[i]; return v; }
inline void PutSeqTag(std::vector<char>* b, unsigned int launch, unsigned int seq) { PutSeqU32(b, kSeqTag); PutSeqU32(b, launch); PutSeqU32(b, seq); }
/* 1 = the last 12 bytes of a move of at least 5 bytes before them are a whole 'BSQ1' trailer with a launch id and a number above 0;
   0 = cut, not one, or a 0 (the move is refused) */
inline int TakeSeqTag(const char* p, size_t n, unsigned int* launch, unsigned int* seq)
{
    if (p == 0 || n < kSeqTagLen + 5) return 0;
    const char* q = p + (n - kSeqTagLen);
    if (GetSeqU32(q) != kSeqTag) return 0;
    *launch = GetSeqU32(q + 4);
    *seq = GetSeqU32(q + 8);
    return (*launch != 0u && *seq != 0u) ? 1 : 0;
}
/* the holder: the next number of a box move it sends on `key` (1 for the box's first in this launch, never 0) */
inline unsigned int SeqNext(std::map<std::string, unsigned int>* out, const std::string& key)
{
    unsigned int& n = (*out)[key];
    ++n;
    if (n == 0u) n = 1u;
    return n;
}
inline unsigned int SeqOf(const std::map<std::string, unsigned int>& out, const std::string& key)
{
    std::map<std::string, unsigned int>::const_iterator it = out.find(key);
    return (it != out.end()) ? it->second : 0u;
}
/* the holder: one box move it sent - its number, the ring ticket it was drained from (haveTicket) and the shop apply it was sent inside
   (applyId, 0 = none) */
struct SeqSent { unsigned int num; long ticket; int haveTicket; unsigned int applyId; };
const size_t kSeqHistCap = 32;
inline void SeqNoteSent(std::vector<SeqSent>* h, unsigned int num, long ticket, int haveTicket, unsigned int applyId)
{
    if (h->size() >= kSeqHistCap) h->erase(h->begin());
    SeqSent s; s.num = num; s.ticket = ticket; s.haveTicket = haveTicket; s.applyId = applyId;
    h->push_back(s);
}
/* the sale's range on one counter. snap: the box's number at the apply's start. t0 / t1: the ring tickets at the apply's start / at its
   answer - an entry rung in [t0, t1) is the sale's; one rung before t0 (sent later) is older. A move sent inside the apply (applyId) is
   the sale's. *before = the number before the sale's first move; *after = its last move's (= before when it moved nothing there). */
inline void SeqSaleRange(const std::vector<SeqSent>& h, unsigned int snap, long t0, long t1, unsigned int applyId, unsigned int* before, unsigned int* after)
{
    unsigned int b = snap, a = 0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const SeqSent& s = h[i];
        const int mine = ((applyId != 0u && s.applyId == applyId) || (s.haveTicket != 0 && (long)(s.ticket - t0) >= 0 && (long)(s.ticket - t1) < 0)) ? 1 : 0;
        if (mine != 0) { if (s.num > a) a = s.num; }
        else if (s.haveTicket != 0 && (long)(s.ticket - t0) < 0 && s.num > b) b = s.num;
    }
    *before = b;
    *after = (a > b) ? a : b;
}
/* why the window was first marked stale since its last rebuild (the rebuild's reason) */
const int kStaleNone = 0, kStaleRefill = 1, kStaleOther = 2, kStaleLater = 3, kStaleSettle = 4, kStaleKinds = 5;   /* kStaleLater: set aside, not ours */
/* T-1451 F1624: WHAT A LANDING CHANGED, so another player's change (or an NPC's, a holder's own) is put into the open window IN PLACE
   instead of re-laying it. known: the change read (op and units); applied: it took on this game's counter; op: the ITEM_MOVE op (0 add,
   1 remove, 2 count, 3 split, 4 charges); type: the item's base record (an add's own record, else the cell's item read before the change;
   empty = unread); delta: the units it added (> 0) or took (< 0) on the counter; rec: the add's description, kept by the caller (0 none).
   F1628 (review): desc = the item's FULL description (DescKey: maker, material, colour, quality, charges, function, level, unique - an
   add's own record, else the cell's item read before the change; empty = unread) - what a remove is matched by in the window, as two
   same-type items of another maker or grade are different items; money: the item is money (functionKind 14 - the window never shows
   it); drift: an add that first removed a leftover record from the counter's cell (the window keeps that leftover - not in place). */
struct SeqLanding
{
    int known; int applied; int op; std::string type; long long delta; unsigned int rec; std::string desc; int money; int drift;
    SeqLanding() : known(0), applied(0), op(-1), delta(0), rec(0u), money(0), drift(0) {}
};
/* F1628: AN ITEM'S FULL DESCRIPTION AS ONE KEY - the fields coopshop::SameDesc compares (the sale request's: base record, maker,
   material, colour, quality, charges, function, level, unique), so two keys are equal exactly when SameDesc says the items are the same.
   The floats go in as their bits (no text formatting: no locale), -0 as 0. Empty base record = unread (empty key). */
inline void DescKeyHex(std::string* s, unsigned int v)
{
    static const char kHex[] = "0123456789abcdef";
    char b[9];
    for (int i = 7; i >= 0; --i) { b[i] = kHex[v & 15u]; v >>= 4; }
    b[8] = 0;
    *s += b;
}
inline std::string DescKey(const std::string& base, const std::string& maker, const std::string& material, const std::string& colour, float quality,
                           float charges, int functionKind, int level, int unique)
{
    if (base.empty()) return std::string();
    std::string s = base + "|" + maker + "|" + material + "|" + colour + "|";
    float f[2] = { quality, charges };
    for (int i = 0; i < 2; ++i)
    {
        if (f[i] == 0.0f) f[i] = 0.0f;   /* -0 compares equal to 0 (SameDesc's ==): one key */
        unsigned int u = 0;
        std::memcpy(&u, &f[i], sizeof u);
        DescKeyHex(&s, u);
        s += "|";
    }
    DescKeyHex(&s, (unsigned int)functionKind); s += "|";
    DescKeyHex(&s, (unsigned int)level); s += "|";
    DescKeyHex(&s, (unsigned int)unique);
    return s;
}
/* the landing's units on its cell from its op, its wire quantity and the cell's counts read here before / after (< 0 = not read).
   1 = read (*delta set); 0 = not (the change cannot be put in place from the fact). The cell number itself means nothing in the window. */
inline int LandingDelta(int op, int quantity, int before, int after, long long* delta)
{
    *delta = 0;
    if (op == 0) { if (quantity < 1) return 0; *delta = quantity; return 1; }                       /* ADD: the stack that entered */
    /* F1628 (review NIT): a remove / split is the cell's count after minus before, read here - never the wire's or the before count alone */
    if (op == 1 || op == 3) { if (before < 0 || after < 0 || after > before) return 0; *delta = (long long)after - before; return 1; }   /* REMOVE / SPLIT */
    if (op == 2) { const int a = (after >= 0) ? after : quantity; if (before < 0 || a < 0) return 0; *delta = (long long)a - before; return 1; }   /* QTY: absolute after */
    return 0;
}
/* why an outside landing could not be put in place (each a stale mark, one rebuild when due), or kOutInPlace */
const int kOutInPlace = 0, kOutRefusedHere = 1, kOutNoFacts = 2, kOutNoType = 3, kOutCursor = 4, kOutNoRecord = 5, kOutShort = 6,
          kOutAddRefused = 7, kOutTakeFailed = 8, kOutCaravan = 9, kOutTooMany = 10, kOutCharges = 11,
          kOutDrift = 12,         /* F1628: an add that first removed a leftover from the counter - the window keeps it: stale */
          kOutMoney = 13,         /* F1628: money - the window never shows it (the re-lay skips functionKind 14): nothing, not stale */
          kOutAlreadyStale = 14,  /* F1628: the window is stale already - its one rebuild shows this change: nothing more */
          kOutKinds = 15;
/* 1 = this plan marks the window stale (not in place, not money, not already stale) */
inline int OutsideMarks(int plan) { return (plan != kOutInPlace && plan != kOutMoney && plan != kOutAlreadyStale) ? 1 : 0; }
/* the plan for one outside landing on a counter of the open window. caravan: the window is a caravan's (its packs are not numbered);
   cursorBusy: an item is on the cursor (it may be one of this window's); haveRecord: an add's description is here; stackFound: a window
   stack of the type holds the units a remove takes (PickStack >= 0). kOutInPlace = put in place (a 0 change: nothing to do).
   F1628: windowStale = the window is marked stale already (its rebuild re-lays it); a remove's match is by the full description. */
inline int OutsidePlan(const SeqLanding& l, int caravan, int cursorBusy, int haveRecord, int stackFound, int windowStale = 0)
{
    if (l.money != 0) return kOutMoney;
    if (windowStale != 0) return kOutAlreadyStale;
    if (l.applied == 0) return kOutRefusedHere;
    if (caravan != 0) return kOutCaravan;
    if (l.op == 4) return kOutCharges;
    if (l.known == 0) return kOutNoFacts;
    if (l.drift != 0) return kOutDrift;
    if (l.delta == 0) return kOutInPlace;
    if (l.desc.empty()) return kOutNoType;
    if (cursorBusy != 0) return kOutCursor;
    if (l.delta > 0) return (haveRecord != 0) ? kOutInPlace : kOutNoRecord;
    return (stackFound != 0) ? kOutInPlace : kOutShort;
}
/* THE ITEM MATCH (the window merges every counter: the counter's cell number means nothing there): the window stack to take `units`
   of the item described by `desc` (DescKey) from - a stack of exactly that many first (the counter's whole stack left), else the first
   that holds more. F1628 (review blocker): matched by the FULL description, never the type alone - in a weapon / armour shop same-type
   items of another maker, material or grade are each their own stack, and taking one of those would show the wrong item gone.
   -1 = no stack of exactly that item holds enough (the caller marks the window stale: one rebuild). type: for the log only. */
struct WinStack { std::string type; std::string desc; long long units; WinStack() : units(0) {} };
inline int PickStack(const std::vector<WinStack>& st, const std::string& desc, long long units)
{
    if (desc.empty() || units < 1) return -1;
    for (size_t i = 0; i < st.size(); ++i) if (st[i].desc == desc && st[i].units == units) return (int)i;
    for (size_t i = 0; i < st.size(); ++i) if (st[i].desc == desc && st[i].units > units) return (int)i;
    return -1;
}
struct SeqAside { std::string key; int writer; unsigned int seq; SeqLanding land; };
struct WinSeq
{
    std::map<SeqId, unsigned int> arrived;   /* the highest number arrived, per counter and writer */
    std::map<SeqId, unsigned int> needed;    /* own range end: the number an ok answer named that has not arrived yet */
    std::map<SeqId, unsigned int> from;      /* own range start (exclusive), beside needed */
    std::map<SeqId, unsigned int> ask;       /* after-sale asks (the holder's digest check) waiting for their number */
    std::vector<SeqAside> aside;             /* landings on the window's counters while a trade on it waited, judged at its answer */
    std::map<int, unsigned int> launch;      /* each writer's launch id */
    int stale;                               /* kStale*: 0 = not stale */
    long long askDropped;
    WinSeq() : stale(0), askDropped(0) {}
};
inline unsigned int SeqArrivedOf(const WinSeq& w, const std::string& key, int writer)
{
    std::map<SeqId, unsigned int>::const_iterator it = w.arrived.find(SeqId(key, writer));
    return (it != w.arrived.end()) ? it->second : 0u;
}
inline void SeqDropWriter(std::map<SeqId, unsigned int>* m, int writer)
{
    for (std::map<SeqId, unsigned int>::iterator it = m->begin(); it != m->end(); ) { if (it->first.second == writer) m->erase(it++); else ++it; }
}
/* a move from `writer` carried launch id `launch`: 1 = a new launch of a writer seen before - its numbers restarted, its entries go */
inline int SeqLaunch(WinSeq* w, int writer, unsigned int launch)
{
    if (launch == 0u) return 0;
    std::map<int, unsigned int>::iterator it = w->launch.find(writer);
    if (it != w->launch.end() && it->second == launch) return 0;
    const int reset = (it != w->launch.end()) ? 1 : 0;
    w->launch[writer] = launch;
    if (reset != 0)
    {
        SeqDropWriter(&w->arrived, writer); SeqDropWriter(&w->needed, writer); SeqDropWriter(&w->from, writer); SeqDropWriter(&w->ask, writer);
        std::vector<SeqAside> keep;
        for (size_t i = 0; i < w->aside.size(); ++i) if (w->aside[i].writer != writer) keep.push_back(w->aside[i]);
        w->aside.swap(keep);
    }
    return reset;
}
/* A move numbered `seq` (0 = unnumbered: ignored) from `writer` arrived on `key`, applied or refused. keep: worth remembering (the
   open window is built over the counter, or a trade of this game waits). waiting: trades of this game on the open window built over
   this counter still waiting (0 when the counter is in no open window). 1 = ANOTHER player's change (outside every own range, nothing
   waiting); 0 = this game's own, set aside for the waiting trade's answer, or unnumbered. A reached own range is dropped.
   F1624: land = what the landing changed, kept with a set-aside landing (judged at the answer, then put in place); kSeqArriveTooMany =
   the aside list is full - it may be this game's own, so it is never put in place (stale). */
const int kSeqArriveTooMany = 2;
inline int SeqArrive(WinSeq* w, const std::string& key, int writer, unsigned int seq, int keep, int waiting, const SeqLanding* land = 0)
{
    if (seq == 0u) return 0;
    const SeqId id(key, writer);
    std::map<SeqId, unsigned int>::iterator nd = w->needed.find(id);
    const int have = (nd != w->needed.end()) ? 1 : 0;
    const unsigned int need = (have != 0) ? nd->second : 0u;
    const unsigned int lo = (have != 0 && w->from.count(id) != 0) ? w->from[id] : 0u;
    if (keep != 0 || have != 0 || w->ask.count(id) != 0 || w->arrived.count(id) != 0)
    {
        if (w->arrived.size() >= kSeqArrivedCap && w->arrived.count(id) == 0) w->arrived.erase(w->arrived.begin());
        unsigned int& a = w->arrived[id];
        if (seq > a) a = seq;
        if (have != 0 && a >= need) { w->needed.erase(id); w->from.erase(id); }
    }
    const int inOwn = (have != 0 && seq > lo && seq <= need) ? 1 : 0;
    if (inOwn != 0) return 0;
    if (waiting != 0)
    {
        if (w->aside.size() >= kSeqAsideCap) return kSeqArriveTooMany;   /* too many to judge: stale */
        SeqAside e; e.key = key; e.writer = writer; e.seq = seq;
        if (land != 0) e.land = *land;
        w->aside.push_back(e);
        return 0;
    }
    return 1;
}
/* An answer from `writer` named (before, after] for `key` (after 0 = none named). ok: the holder applied it - the own range is kept
   until `after` arrives (a second range on the same counter widens it). */
inline void SeqAnswer(WinSeq* w, const std::string& key, int writer, unsigned int before, unsigned int after, int ok)
{
    if (after == 0u || ok == 0) return;
    const SeqId id(key, writer);
    if (after <= SeqArrivedOf(*w, key, writer)) return;
    std::map<SeqId, unsigned int>::iterator nd = w->needed.find(id);
    if (nd == w->needed.end()) { w->needed[id] = after; w->from[id] = before; return; }
    if (after > nd->second) nd->second = after;
    if (w->from.count(id) == 0 || before < w->from[id]) w->from[id] = before;
}
/* A trade asked of `writer` settled (answered or lapsed); named: the ranges its answer named, per counter (empty for a lapse). stillWaiting:
   other trades of this game on the window still waiting. Each landing set aside is judged: inside a named range = this trade's own
   (dropped); at or below a named range's start = another player's; anything else waits while another trade waits (it may be that one's),
   else is another player's. Returns how many were another player's; othersOut (F1624) gets them, with what each changed, so the caller
   puts each in place (or marks the window stale when one cannot be). */
/* T-1451 F1642: unnumbered = the counters the answer names with NO number (the numberless form, or a road that still lacks them): a
   landing of `writer` set aside on one of them cannot be told this trade's own or another player's - it is never 'another player's' (never
   put in place: that showed our own sale twice, T2697); it goes to unsureOut and the caller marks the window for one rebuild.
   F1657: OF ANY WRITER - a numberless answer now comes from a holder's shop record after a hand-over (ShopAnswerFromRecord), and the
   trade's own moves were sent by the holder that applied it, not by the one that answers; the answer vouches for no writer's numbers. */
inline int SeqJudgeAside(WinSeq* w, int writer, const std::map<std::string, std::pair<unsigned int, unsigned int> >& named, int stillWaiting,
                         std::vector<SeqAside>* othersOut = 0, const std::set<std::string>* unnumbered = 0, std::vector<SeqAside>* unsureOut = 0)
{
    int others = 0;
    std::vector<SeqAside> keep;
    for (size_t i = 0; i < w->aside.size(); ++i)
    {
        const SeqAside& e = w->aside[i];
        std::map<std::string, std::pair<unsigned int, unsigned int> >::const_iterator f = (e.writer == writer) ? named.find(e.key) : named.end();
        const int found = (f != named.end()) ? 1 : 0;
        if (found != 0 && e.seq > f->second.first && e.seq <= f->second.second) continue;
        const int surely = (found != 0 && e.seq <= f->second.first) ? 1 : 0;
        if (stillWaiting != 0 && surely == 0) { keep.push_back(e); continue; }
        if (found == 0 && unnumbered != 0 && unnumbered->count(e.key) != 0) { if (unsureOut != 0) unsureOut->push_back(e); continue; }   /* F1657: any writer */
        ++others;
        if (othersOut != 0) othersOut->push_back(e);
    }
    w->aside.swap(keep);
    return others;
}
/* F1628 (review 4): AT A LAPSE no answer names the trade's own range, so its own landing (the holder applied the sale; the answer was
   lost) is put in place as another player's. A late ok that then settles the trade (its give-back failed) would add the same units
   again. shown: the landings put in place at that lapse (writer, key, number, units); named: the late answer's ranges. The units of
   those that the answer names as this trade's own (inside (before, after] of their counter, same writer) - already in the window, so
   the late ok adds only the rest. */
inline long long LapseShownUnits(const std::vector<SeqAside>& shown, int writer, const std::map<std::string, std::pair<unsigned int, unsigned int> >& named)
{
    long long n = 0;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const SeqAside& e = shown[i];
        if (e.writer != writer || e.land.delta <= 0) continue;
        std::map<std::string, std::pair<unsigned int, unsigned int> >::const_iterator f = named.find(e.key);
        if (f != named.end() && e.seq > f->second.first && e.seq <= f->second.second) n += e.land.delta;
    }
    return n;
}
/* T-1451 F1642: the counters an ok answer names with no number - the numberless form ('SHC1': no numbers at all, or not one per counter).
   F1643 (review NIT): in the numbered form a 0 is the holder's word that it never numbered a move on that counter and this sale sent
   none there (SeqSaleRange: after = before = 0) - nothing of the sale lands on it, so it is NOT unnumbered (no rebuild per settle). A
   refusal moved nothing: none. */
inline std::set<std::string> SeqUnnumbered(const std::vector<std::string>& pieces, const std::vector<unsigned int>& seqs, int ok)
{
    std::set<std::string> u;
    if (ok == 0 || seqs.size() == pieces.size()) return u;
    for (size_t i = 0; i < pieces.size(); ++i) u.insert(pieces[i]);
    return u;
}
/* T-1451 F1642 (generalises F1628's lapse record, which knew only landings put in place at a LAPSE): A LANDING PUT INTO THE OPEN WINDOW IN
   PLACE AS ANOTHER PLAYER'S - at a trade's lapse (trade = that trade's id), at another trade's answer or as it landed (trade 0) - is kept
   (the last kShownCap), so the settle of the trade that landing really was never shows its units a second time. */
struct ShownRec { SeqAside e; unsigned int trade; };
const size_t kShownCap = 64;
/* F1643 (review SHOULD 1): the writer's game started again (a new launch id - SeqLaunch reset its numbers, which restart at 1): its kept
   landings are dropped with them, so an old number never falls inside a new trade's range (that settle would add a unit fewer, silently).
   Returns how many went. */
inline int ShownDropWriter(std::vector<ShownRec>* v, int writer)
{
    std::vector<ShownRec> keep;
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i].e.writer != writer) keep.push_back((*v)[i]);
    const int n = (int)(v->size() - keep.size());
    v->swap(keep);
    return n;
}
inline void ShownNote(std::vector<ShownRec>* v, const SeqAside& e, unsigned int trade)
{
    if (e.land.delta == 0) return;
    if (v->size() >= kShownCap) v->erase(v->begin());
    ShownRec r; r.e = e; r.trade = trade;
    v->push_back(r);
}
/* At the settle of trade `trade` asked of `writer` (named: its answer's ranges; unnumbered: the counters it names with no number): the units
   ADDED by landings already put in place that are this trade's own - the writer's, inside a named range (any road), or, with no number to
   tell, one put in place at this trade's own lapse on a counter it names with no number. Each counted record is dropped (never counted
   twice), as is every other record of this trade. *rebuild = 1 when the window cannot be made right in place: a counted landing took units
   away (this trade's own remove shown again - a buy), or the answer leaves a landing of the writer on a counter it names with no number
   unjudged (one rebuild shows the counters). */
inline long long TradeShownUnits(std::vector<ShownRec>* v, unsigned int trade, int writer, const std::map<std::string, std::pair<unsigned int, unsigned int> >& named,
                                 const std::set<std::string>& unnumbered, int* rebuild)
{
    long long n = 0;
    std::vector<ShownRec> keep;
    for (size_t i = 0; i < v->size(); ++i)
    {
        const ShownRec& r = (*v)[i];
        const SeqAside& e = r.e;
        /* F1657: a counter the answer names with no number is blind for EVERY writer (a hand-over's answer from the record) */
        std::map<std::string, std::pair<unsigned int, unsigned int> >::const_iterator f = (e.writer == writer) ? named.find(e.key) : named.end();
        const int own = (f != named.end() && e.seq > f->second.first && e.seq <= f->second.second) ? 1 : 0;
        const int blind = (own == 0 && named.find(e.key) == named.end() && unnumbered.count(e.key) != 0) ? 1 : 0;
        if (e.writer != writer && blind == 0) { keep.push_back(r); continue; }
        if (own == 0 && blind == 0) { if (r.trade != trade) keep.push_back(r); continue; }
        if (blind != 0) { if (rebuild != 0) *rebuild = 1; if (r.trade != trade) { keep.push_back(r); continue; } }
        if (e.land.delta > 0) n += e.land.delta;
        else if (rebuild != 0) *rebuild = 1;
    }
    v->swap(keep);
    return n;
}
/* T-1451 F1662 (S2): AN OK ANSWER THAT NAMES NO COUNTERS AT ALL for a home shop's trade on the open window (a holder that could not
   resolve the home's boxes answers from its record with an empty list; any other road that lacks them) vouches for no landing: with no
   counter named nothing marks the window stale, and a landing of this trade set aside earlier would be judged another player's and shown
   - our own sale twice (12 -> 13 -> 14). 1 = the whole window cannot tell: every landing set aside for it is left unjudged, and the
   window is marked stale once and rebuilt when due. A numbered or numberless answer that names a counter is NOT blind (its own rules);
   a refusal moved nothing; a caravan's packs are not numbered; a trade not on the open window has no landings set aside for it. */
inline int SeqAnswerBlind(int sameWindow, int caravan, int ok, int shopAnswer, size_t pieces)
{
    return (sameWindow != 0 && caravan == 0 && ok != 0 && shopAnswer != 0 && pieces == 0) ? 1 : 0;
}
/* the counters a blind answer leaves unjudged: every counter with a landing set aside for the open window, and every counter of a
   landing already put in place (TradeShownUnits then counts this trade's own lapse-shown units and marks the rebuild for the rest) */
inline std::set<std::string> SeqBlindCounters(const WinSeq& w, const std::vector<ShownRec>& shown)
{
    std::set<std::string> u;
    for (size_t i = 0; i < w.aside.size(); ++i) u.insert(w.aside[i].key);
    for (size_t i = 0; i < shown.size(); ++i) u.insert(shown[i].e.key);
    return u;
}
/* 1 = marked now (the first reason is kept until the rebuild) */
inline int SeqMark(WinSeq* w, int why) { if (w->stale != 0 || why <= kStaleNone || why >= kStaleKinds) return 0; w->stale = why; return 1; }
/* the open window was refilled from the counters. waiting: a trade on it waits. 1 = it may lack a change still on its way - to be marked
   stale; -1 = nothing of it is on its way and it was stale: the refill shows the counters, the mark is cleared; 0 = nothing */
inline int SeqRefilled(WinSeq* w, int waiting)
{
    if (waiting != 0 || !w->needed.empty() || !w->aside.empty()) return 1;
    if (w->stale != 0) { w->stale = 0; return -1; }
    return 0;
}
/* the rebuild is due: stale, no trade on the window waiting, every needed number arrived. Returns the stale reason (the mark cleared);
   0 = not now */
inline int SeqRebuildDue(WinSeq* w, int waiting)
{
    if (w->stale == 0 || waiting != 0 || !w->needed.empty()) return 0;
    const int r = w->stale;
    w->stale = 0;
    return r;
}
inline void SeqWindowGone(WinSeq* w) { w->needed.clear(); w->from.clear(); w->aside.clear(); w->stale = 0; }
/* a parity answer re-based `key` (its content is the holder's now): its needed numbers are settled. Returns how many went.
   Re-check F1604 (blocker): its SET-ASIDE landings are NOT settled - which of them are this game's own is known only from the waiting
   trade's answer (its before..after), so they stay and are judged there (SeqJudgeAside): a landing outside that span, or a lapse, marks
   the window stale and the one rebuild is made when due. Dropping them here hid another player's change in the window. */
inline int SeqRebased(WinSeq* w, const std::string& key)
{
    int n = 0;
    for (std::map<SeqId, unsigned int>::iterator it = w->needed.begin(); it != w->needed.end(); ) { if (it->first.first == key) { w->from.erase(it->first); w->needed.erase(it++); ++n; } else ++it; }
    return n;
}
/* how many landings on `key` are set aside, waiting for a trade's answer */
inline int SeqAsideOn(const WinSeq& w, const std::string& key)
{
    int n = 0;
    for (size_t i = 0; i < w.aside.size(); ++i) if (w.aside[i].key == key) ++n;
    return n;
}
/* re-check F1604 (3): a box move the safety net sends on `key` (no ring ticket, outside an apply) while shop answers naming that counter
   are still held for their moves: the newest such answer's apply id - noted with it, the answer's range counts the move as the sale's
   (SeqSaleRange). 0 = none (the move is noted without one, as before). */
struct SeqHeld { unsigned int applyId; const std::vector<std::string>* pieces; };
inline unsigned int SeqNetRoadApply(const std::vector<SeqHeld>& held, const std::string& key)
{
    for (size_t i = held.size(); i-- > 0; )
    {
        if (held[i].applyId == 0u || held[i].pieces == 0) continue;
        for (size_t k = 0; k < held[i].pieces->size(); ++k) if ((*held[i].pieces)[k] == key) return held[i].applyId;
    }
    return 0u;
}
/* the after-sale ask (the holder's digest check): 1 = ask now (no other own trade on that writer waits, and its number arrived or none was
   named); 0 = it waits (SeqAsksDue) */
inline int SeqAskAtSettle(WinSeq* w, const std::string& key, int writer, unsigned int after, int othersWaiting)
{
    if (othersWaiting == 0 && (after == 0u || SeqArrivedOf(*w, key, writer) >= after)) return 1;
    const SeqId id(key, writer);
    if (w->ask.size() >= kSeqAskCap && w->ask.count(id) == 0) { w->ask.erase(w->ask.begin()); ++w->askDropped; }
    unsigned int& n = w->ask[id];
    if (after > n) n = after;
    return 0;
}
/* the waiting asks of `writer` whose number arrived, once no own trade on that writer waits: their keys (they leave the wait) */
inline size_t SeqAsksDue(WinSeq* w, int writer, int othersWaiting, std::vector<std::string>* keys)
{
    keys->clear();
    if (othersWaiting != 0) return 0;
    for (std::map<SeqId, unsigned int>::iterator it = w->ask.begin(); it != w->ask.end(); )
    {
        if (it->first.second == writer && SeqArrivedOf(*w, it->first.first, writer) >= it->second) { keys->push_back(it->first.first); w->ask.erase(it++); }
        else ++it;
    }
    return keys->size();
}
/* fold 1: the window the player has open at the settle is NOT the one open at the send - the game's own trade-window open
   (showTraderInventory 0x6E6350, the open generation g_twOpenGen) ran in between, and it refills the stand-in from the counters
   itself. sentGen / nowGen: the open generation at the send / now. */
inline int WindowReopened(long sentGen, long nowGen) { return (sentGen != nowGen) ? 1 : 0; }

/* ---- fold 2 (re-check 2026-11-20): the kept sale descriptions (request id -> the sold item's full description) ----
   Kept from the send to the settle; a LAPSE whose give-back failed keeps its row for a late ok (g_shopLapsedRow), so its description
   stays with it (SaleDescStays) and goes when that row goes (the late answer, the lapsed slot reused, the world's teardown). At most
   kSaleDescCap: a full table drops its oldest id. */
const size_t kSaleDescCap = 64;
template <class T> void SaleDescKeep(std::map<unsigned int, T>* m, unsigned int id, const T& d)
{
    if (m->size() >= kSaleDescCap && m->count(id) == 0) m->erase(m->begin());
    (*m)[id] = d;
}
/* lapsed: settled with no answer (ShopPendLapseNow); giveBackFailed: ShopSettle's answer (the row is kept for a late ok). 1 = kept */
inline int SaleDescStays(int lapsed, int giveBackFailed) { return (lapsed != 0 && giveBackFailed != 0) ? 1 : 0; }

/* ---- fold 3 (re-check 2026-11-20): a rebuild that waits for the cursor keeps its reason ----
   TradeCaller: 1 = a rebuild the trade itself made ("settle..."), also when it ran late ("deferred:settle..."): its [SHOPWIN] lines go
   to the trade cap. DeferWhy: two rebuilds wait for the SAME window - one rebuild covers both, and the trade's reason is kept over
   another's. DeferredCaller: the name the late rebuild runs under. */
inline int TradeCaller(const char* c)
{
    if (c == 0) return 0;
    if (std::strncmp(c, "deferred:", 9) == 0) c += 9;
    return (std::strncmp(c, "settle", 6) == 0) ? 1 : 0;
}
inline const char* DeferWhy(const char* held, const char* incoming)
{
    return (TradeCaller(held) != 0 && TradeCaller(incoming) == 0) ? held : incoming;
}
inline std::string DeferredCaller(const char* why) { return std::string("deferred:") + ((why != 0 && why[0] != 0) ? why : "?"); }

/* ---- the answer already here ---- */
/* liveRead: the live box's digest was read (ItParityDigest), liveUnread: items that would not read (any -> not trusted). handback /
   pull / owed: a hand-back is waiting on the box, a pull of it is live, or this game's rows on it are owed to the safety net - each
   needs the swap's own bookkeeping, so the swap runs as before. 1 = the live box already holds the answer: no swap, no rebuild. */
inline int AnswerAlreadyHere(int liveRead, unsigned int liveDigest, long long liveCount, long long liveUnread,
                             unsigned int ansDigest, long long ansItems, int handback, int pull, int owed)
{
    if (liveRead == 0 || liveUnread != 0 || handback != 0 || pull != 0 || owed != 0) return 0;
    return (liveDigest == ansDigest && liveCount == ansItems) ? 1 : 0;
}

/* ---- the [SHOPWIN] diagnostic's cap: the first kDumpEvents window changes of a launch (each a before and an after line) ---- */
const int kDumpEvents = 20;
const int kDumpItems = 20;   /* items named per line */
inline int DumpDue(int* used) { if (*used >= kDumpEvents) return 0; ++*used; return 1; }

}   /* namespace shopwin */
