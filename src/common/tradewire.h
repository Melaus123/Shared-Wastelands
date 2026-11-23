// tradewire.h - T-1167 piece 2 (owner 1633 / 1633 addition / 1635): THE PLAYER TRADE'S OFFERS AND ACCEPTS, THROUGH THE WORLD SERVER.
// Pure: no disk, no clock, no engine; the game plugin (tradewin.cpp), the world server (store_main.cpp) and the offline suite compile it.
//
// ONE MESSAGE, TRADE (94, world-server protocol 154; 155 adds the lock kinds 8-13, below), with a kind byte. Every one goes through the world server, which relays it to the
// other player's game and itself keeps each open trade's state (this header's Trade), so it - not either game - says when both sides
// accepted the same offers (ServerStep). The game-to-game protocol does not change.
//   up ASK      {id, the other player's slot, my character's uid, their character's uid}  - open a trade with that character's player
//   down ASKED  {id, the asker's slot, the asker's character's uid, my character's uid}      - the other game opens its own window, or
//   up ANSWER   {id, answer}  -> down ANSWER {id, answer} to the asker                        refuses: busy / gone / out of range / not
//                                                                                              a player's character (Answer)
//   up OFFER    {id, serial, cats, items}  -> down OFFER (the same) to the other side         - one side's whole offer, with its serial
//   up ACCEPT   {id, my serial, their serial} -> down ACCEPTS to both sides                   - the accept names the serials it accepts
//   down ACCEPTS {id, your serial, their serial, you accepted, they accepted, ready}          - the server's state after each OFFER/ACCEPT
//   up CANCEL   {id, why}  -> down CANCEL {id, why} to the other side                         - the window closed (Why)
// THE RULES (SetOffer / Accept): each side's offer carries a serial that rises with every change; ANY change to either offer clears
// BOTH accepts; an ACCEPT counts only when the serials it names are both offers' current serials; both accepted = both accepted the
// same serials (a change in between cleared them) = READY for the lock. PIECE 3 hooks the lock / commit / undo at READY (ServerStep
// kSrvReady; the game's ACCEPTS with ready = 1); PIECE 4 adds the cancels (range, hostility, knockout, death, PLAYER_GONE's words).
// PIECE 3 (world-server protocol 155, owner 1635 'the swap's commit through the world server'; design A): THE LOCK AND THE ONE DECISION.
// The world server's trade book (tradebook.h) and trade.txt hold it; every kind below rides TRADE (94):
//   down LOCK_NOW     {id, your serial, their serial, their slot, your cats}   - at READY: escrow exactly the offer of `your serial`
//   up   LOCKED       {id, my serial, my cats, transfer, bodies}               - escrowed and written down: each object's bytes (opaque here),
//                                                                                the money and a transfer number reserved from the game's ledger
//   up   LOCK_REFUSED {id, row}                                                - the offer is not all there (row: the offer row, kNoRow = money)
//   down COMMIT       {id, your serial, their serial, their slot, their cats, inbox, their bodies}  - the decision: build their objects; their
//                                                                                money is in YOUR money inbox under `inbox` (0: they gave none)
//   down ABORT        {id, why, by slot, row}                                  - the decision: give your escrow back (AbortWhy)
//   up   APPLIED      {id}                                                     - the decision applied here and on disk
// The world server writes COMMITTED or ABORTED ONCE, to trade.txt on disk, before it answers anything; the first decision written wins.
// AN ITEM IN AN OFFER (T-1167 piece 2 fold 1, review MEDIUM 1): ONE ROW PER DISTINCT THING - its record key (the base string id),
// its maker, material and colour records (company / material / colour sid, "" when it has none), its exact quality (the float's
// bits), its charges (the float's bits: ammo's per-item rounds, a kit's uses - items.cpp chargesLeft +0x118; fold 2) and its unique
// flag (fold 2; the mod's own SameDesc, shopwire.h, compares both), so two items of one record that differ in grade, maker, colour,
// charges or uniqueness are never one row and piece 3 can move exactly those. An
// item carries no id the other game can name (the mod names items by these fields everywhere: shopwire.h Desc, the bag match), so
// stacks of the very same thing are one row with their counts added. The row's shown name is the engine's own name for the item
// (items.cpp ItemsTradeRows: RootObjectBase getName, vtable +0x08, the one the item classes all keep); the same thing twice in one
// offer is refused (OfferOk).
#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <locale>

namespace tradew {

const unsigned kProtocol = 177u;      /* the world-server protocol that carries TRADE (store_main.cpp and store.cpp check it at compile time); 155: the lock kinds 8-13; 156: LOCK_REFUSED's reason and ABORT's rows (no room, owner 1641) */
const unsigned kMsgTrade = 94u;       /* TRADE: 94 was free (90-93 and 97-103 are taken; 94-96 unused before; no effort branch takes it) */

enum Kind { kAsk = 1, kAsked = 2, kAnswer = 3, kOffer = 4, kAccept = 5, kAccepts = 6, kCancel = 7,
            kLockNow = 8, kLocked = 9, kLockRefused = 10, kCommit = 11, kAbort = 12, kApplied = 13 };   /* 8-13: piece 3, protocol 155 */
enum Answer { kAnsOpen = 0, kAnsBusy = 1, kAnsGone = 2, kAnsRange = 3, kAnsNotPlayers = 4 };
enum Why { kWhyPressed = 1, kWhyLeft = 2, kWhyWorld = 3, kWhyWindow = 4, kWhyGone = 5 };
const unsigned kAnswerMax = 4u, kWhyMax = 5u;
/* piece 3: why a lock ended ABORTED (the world server's one decision) */
enum AbortWhy { kAbRefused = 1, kAbCancel = 2, kAbGone = 3, kAbTimeout = 4, kAbRestart = 5, kAbStale = 6, kAbNotSaved = 7, kAbNoRoom = 8 };
const unsigned kAbortMax = 8u;
/* build 3 (protocol 156, owner 1641): why a LOCK_REFUSED refused - the offer is not all there (row), or the RECEIVING game has no room
   for the other side's items (rows: the other side's offer rows that did not fit, as the offer lists them) */
enum RefuseWhy { kRefMissing = 0, kRefNoRoom = 1 };
const unsigned kRefuseMax = 1u;
const unsigned kNoRow = 0xFFFFFFFFu, kNoSlot = 0xFFFFFFFFu;
const unsigned kMaxBodies = 256u;          /* objects one side's LOCKED / COMMIT carries (a row may be several stacks) */
const unsigned kMaxBody = 262144u;         /* one object's bytes (an item with a full pack) */
const unsigned kMaxBodyBytes = 1048576u;   /* all of one side's objects' bytes: bigger messages go through CHUNK (88) on the way */

const unsigned kMaxItems = 64u;            /* rows in one offer */
const unsigned kMaxKey = 95u;              /* a record key (factionkey.h kMaxSid: the same bound) */
const unsigned kMaxName = 128u;            /* an item's shown name */
const unsigned kMaxCount = 1000000u;       /* one row's count */
const unsigned kMaxCats = 2147483647u;     /* money: what a purse (an int) can hold */

inline const char* KindName(unsigned k)
{
    switch (k)
    {
    case kAsk: return "ASK"; case kAsked: return "ASKED"; case kAnswer: return "ANSWER"; case kOffer: return "OFFER";
    case kAccept: return "ACCEPT"; case kAccepts: return "ACCEPTS"; case kCancel: return "CANCEL";
    case kLockNow: return "LOCK_NOW"; case kLocked: return "LOCKED"; case kLockRefused: return "LOCK_REFUSED"; case kCommit: return "COMMIT";
    case kAbort: return "ABORT"; case kApplied: return "APPLIED"; default: return "?";
    }
}
inline const char* AnswerName(unsigned a)
{
    switch (a)
    {
    case kAnsOpen: return "open"; case kAnsBusy: return "busy"; case kAnsGone: return "gone"; case kAnsRange: return "out-of-range";
    case kAnsNotPlayers: return "not-a-player's-character"; default: return "?";
    }
}
inline const char* WhyName(unsigned w)
{
    switch (w)
    {
    case kWhyPressed: return "CANCEL-pressed"; case kWhyLeft: return "left"; case kWhyWorld: return "world-left"; case kWhyWindow: return "window-failed";
    case kWhyGone: return "player-gone"; default: return "?";
    }
}
inline const char* AbortName(unsigned w)
{
    switch (w)
    {
    case kAbRefused: return "lock-refused"; case kAbCancel: return "cancelled"; case kAbGone: return "player-gone"; case kAbTimeout: return "timeout";
    case kAbRestart: return "world-server-restart"; case kAbStale: return "locked-another-offer"; case kAbNotSaved: return "not-recorded";
    case kAbNoRoom: return "no-room"; default: return "?";
    }
}

/* ---- an offer ---- */
struct Item
{
    std::string key, company, material, colour; unsigned q, charges, unique; std::string name; unsigned count;   /* q / charges: the floats' bits; unique 0/1 */
    Item() : q(0), charges(0), unique(0), count(0) {}
    Item(const std::string& k, const std::string& n, unsigned c) : key(k), q(0), charges(0), unique(0), name(n), count(c) {}
};
/* the same thing: every identity field equal (the name and count are not identity) */
inline bool SameThing(const Item& a, const Item& b)
{
    return a.key == b.key && a.company == b.company && a.material == b.material && a.colour == b.colour && a.q == b.q
        && a.charges == b.charges && a.unique == b.unique;   /* fold 2 (re-check MEDIUM): charges and unique, as SameDesc */
}
inline bool ThingOk(const Item& it)
{
    return !it.key.empty() && it.key.size() <= kMaxKey && it.company.size() <= kMaxKey && it.material.size() <= kMaxKey && it.colour.size() <= kMaxKey
        && it.name.size() <= kMaxName && it.unique <= 1;
}
struct Offer { unsigned serial; unsigned cats; std::vector<Item> items; Offer() : serial(0), cats(0) {} };
inline bool SameGoods(const Offer& a, const Offer& b)
{
    if (a.cats != b.cats || a.items.size() != b.items.size()) return false;
    for (size_t i = 0; i < a.items.size(); ++i)
        if (!SameThing(a.items[i], b.items[i]) || a.items[i].name != b.items[i].name || a.items[i].count != b.items[i].count) return false;
    return true;
}
inline bool OfferOk(const Offer& o)
{
    if (o.items.size() > kMaxItems || o.cats > kMaxCats) return false;
    for (size_t i = 0; i < o.items.size(); ++i)
    {
        const Item& it = o.items[i];
        if (!ThingOk(it) || it.count == 0 || it.count > kMaxCount) return false;
        for (size_t k = 0; k < i; ++k) if (SameThing(o.items[k], it)) return false;
    }
    return true;
}
inline int FindThing(const Offer& o, const Item& thing)
{
    for (size_t i = 0; i < o.items.size(); ++i) if (SameThing(o.items[i], thing)) return (int)i;
    return -1;
}
/* the goods of an offer changed in place (the serial is the caller's): the row of `thing` set to `count` (0 takes it out; a new row
   takes thing's name). 1 changed, 0 the same, -1 refused (a bad key, record or name, a count over kMaxCount, a new row past kMaxItems) */
inline int SetCount(Offer* o, const Item& thing, unsigned count)
{
    if (!ThingOk(thing) || count > kMaxCount) return -1;
    const int at = FindThing(*o, thing);
    if (count == 0)
    {
        if (at < 0) return 0;
        o->items.erase(o->items.begin() + at);
        return 1;
    }
    if (at >= 0)
    {
        if (o->items[(size_t)at].count == count) return 0;
        o->items[(size_t)at].count = count;
        return 1;
    }
    if (o->items.size() >= kMaxItems) return -1;
    Item row = thing; row.count = count;
    o->items.push_back(row);
    return 1;
}
/* a click on a row of this character's items: that stack's count added to the offer's row for the same thing, never past what the
   character holds of that thing (`held`, every stack of it). 1 changed, 0 nothing to add, -1 refused (SetCount's) */
inline int AddStack(Offer* o, const Item& thing, unsigned qty, unsigned held)
{
    const int at = FindThing(*o, thing);
    const unsigned have = at >= 0 ? o->items[(size_t)at].count : 0u;
    unsigned want = have + qty;
    if (want < have || want > held) want = held;   /* never past what is held (and no wrap) */
    if (want == have) return 0;
    return SetCount(o, thing, want);
}
/* T-1167 piece 2 fold 1 (review LOW): the offer trimmed to what the character still holds - held(row) for each row (a row no longer
   held goes, a row held in part comes down to it; only when the items were read) and the money to `purse` (purse < 0: not read, the
   money is left). 1 something trimmed, 0 the same. */
template <class HeldFn> inline int TrimToHeld(Offer* o, HeldFn held, bool itemsRead, long long purse)
{
    int n = 0;
    if (itemsRead)
        for (size_t i = o->items.size(); i-- > 0; )
        {
            const unsigned h = held(o->items[i]);
            if (h == 0) { o->items.erase(o->items.begin() + (long)i); ++n; }
            else if (h < o->items[i].count) { o->items[i].count = h; ++n; }
        }
    if (purse >= 0 && (long long)o->cats > purse) { o->cats = (unsigned)purse; ++n; }
    return n > 0 ? 1 : 0;
}

/* ---- one trade (both the world server's and each game's copy) ---- */
enum State { kStAsked = 1, kStOpen = 2 };
struct Trade
{
    unsigned long long id; int state; int slot[2]; unsigned uid[2]; Offer offer[2]; int accepted[2];   /* side 0 = the asker, 1 = the asked */
    Trade() : id(0), state(0) { slot[0] = slot[1] = -1; uid[0] = uid[1] = 0; accepted[0] = accepted[1] = 0; }
};
enum Step { kStepChanged = 1, kStepRepeat = 2, kStepNotOpen = 3, kStepBad = 4, kStepAccepted = 5, kStepReady = 6, kStepStale = 7 };
inline bool Ready(const Trade& t) { return t.state == kStOpen && t.accepted[0] != 0 && t.accepted[1] != 0; }
/* a side's whole new offer: taken only with a serial above the one held (an older or repeated one changes nothing); ANY change
   clears BOTH accepts */
inline int SetOffer(Trade* t, int side, const Offer& o)
{
    if (side < 0 || side > 1) return kStepBad;
    if (t->state != kStOpen) return kStepNotOpen;
    if (!OfferOk(o)) return kStepBad;
    if (o.serial <= t->offer[side].serial) return kStepRepeat;
    t->offer[side] = o;
    t->accepted[0] = 0; t->accepted[1] = 0;
    return kStepChanged;
}
/* an ACCEPT names the serials it accepts: counted only when they are both offers' current serials */
inline int Accept(Trade* t, int side, unsigned mine, unsigned theirs)
{
    if (side < 0 || side > 1) return kStepBad;
    if (t->state != kStOpen) return kStepNotOpen;
    if (mine != t->offer[side].serial || theirs != t->offer[1 - side].serial) return kStepStale;
    t->accepted[side] = 1;
    return Ready(*t) ? kStepReady : kStepAccepted;
}
/* a game's own offer replaced by `next` (its goods): the serial rises by one; 0 the same goods (nothing to send), else SetOffer's */
inline int OwnChange(Trade* t, int side, const Offer& next)
{
    if (side < 0 || side > 1) return kStepBad;
    if (SameGoods(t->offer[side], next)) return 0;
    Offer o = next; o.serial = t->offer[side].serial + 1;
    if (t->state != kStOpen)
    {   /* still asked: kept here and sent when the trade opens (the server refuses an OFFER before) */
        if (!OfferOk(o)) return kStepBad;
        t->offer[side] = o;
        return kStepChanged;
    }
    return SetOffer(t, side, o);
}
/* a trade id: the asker's slot, the second it was asked and a running number - unique per asker */
inline unsigned long long MakeId(int slot, unsigned long long nowSec, unsigned counter)
{
    return ((unsigned long long)(slot < 0 ? 0xFFFF : (slot & 0xFFFF)) << 48) | ((nowSec & 0xFFFFFFFFULL) << 16) | (unsigned long long)(counter & 0xFFFFu);
}

/* ---- the wire: little-endian; strings {u16 length, bytes}; every message read to its end ---- */
struct Msg
{
    unsigned kind; unsigned long long id;
    unsigned slot, uidFrom, uidTo;     /* ASK: to slot, my uid, their uid; ASKED: from slot, the asker's uid, my uid */
    unsigned code;                     /* ANSWER: Answer; CANCEL: Why */
    Offer offer;                       /* OFFER */
    unsigned serialMine, serialTheirs; /* ACCEPT: mine, theirs; ACCEPTS: yours, theirs */
    unsigned you, they, ready;         /* ACCEPTS */
    /* piece 3: LOCK_NOW {serialMine, serialTheirs, slot = their slot, cats = mine}; LOCKED {serialMine, cats, transfer, bodies};
       LOCK_REFUSED {row}; COMMIT {serialMine, serialTheirs, slot = their slot, cats = theirs, inbox, bodies = theirs};
       ABORT {code = AbortWhy, slot = the slot that refused / cancelled / left (kNoSlot none), row}.
       build 3 (protocol 156): LOCK_REFUSED {row, code = RefuseWhy, rows}; ABORT {code, slot, row, rows} - rows: with kRefNoRoom / kAbNoRoom
       only, the OTHER side's offer rows (indexes) the refusing game had no room for; empty otherwise */
    unsigned cats, row; unsigned long long transfer, inbox; std::vector<std::string> bodies; std::vector<unsigned> rows;
    Msg() : kind(0), id(0), slot(0), uidFrom(0), uidTo(0), code(0), serialMine(0), serialTheirs(0), you(0), they(0), ready(0), cats(0), row(kNoRow), transfer(0), inbox(0) {}
};
inline void PutU8(std::vector<char>* v, unsigned x) { v->push_back((char)(x & 0xFFu)); }
inline void PutU16(std::vector<char>* v, unsigned x) { PutU8(v, x); PutU8(v, x >> 8); }
inline void PutU32(std::vector<char>* v, unsigned x) { for (int i = 0; i < 4; ++i) v->push_back((char)((x >> (8 * i)) & 0xFFu)); }
inline void PutU64(std::vector<char>* v, unsigned long long x) { PutU32(v, (unsigned)(x & 0xFFFFFFFFULL)); PutU32(v, (unsigned)(x >> 32)); }
inline void PutStr(std::vector<char>* v, const std::string& s) { PutU16(v, (unsigned)s.size()); v->insert(v->end(), s.begin(), s.end()); }
struct Cur { const unsigned char* p; size_t n; size_t at; bool bad; };
inline unsigned GetN(Cur* c, int bytes)
{
    if (c->bad || c->at > c->n || c->n - c->at < (size_t)bytes) { c->bad = true; return 0; }
    unsigned x = 0;
    for (int i = 0; i < bytes; ++i) x |= (unsigned)c->p[c->at + (size_t)i] << (8 * i);
    c->at += (size_t)bytes;
    return x;
}
inline unsigned long long GetU64(Cur* c) { const unsigned lo = GetN(c, 4); const unsigned hi = GetN(c, 4); return ((unsigned long long)hi << 32) | lo; }
inline void GetStr(Cur* c, std::string* out, unsigned cap)
{
    const unsigned len = GetN(c, 2);
    if (c->bad || len > cap || c->n - c->at < (size_t)len) { c->bad = true; return; }
    out->assign((const char*)c->p + c->at, (size_t)len);
    c->at += len;
}
/* piece 3: one side's objects - at most kMaxBodies, each 1..kMaxBody bytes, kMaxBodyBytes in all */
inline bool BodiesOk(const std::vector<std::string>& v)
{
    if (v.size() > kMaxBodies) return false;
    size_t all = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].empty() || v[i].size() > kMaxBody) return false;
        all += v[i].size();
        if (all > kMaxBodyBytes) return false;
    }
    return true;
}
inline void PutBodies(std::vector<char>* b, const std::vector<std::string>& v)
{
    PutU16(b, (unsigned)v.size());
    for (size_t i = 0; i < v.size(); ++i) { PutU32(b, (unsigned)v[i].size()); b->insert(b->end(), v[i].begin(), v[i].end()); }
}
inline void GetBodies(Cur* c, std::vector<std::string>* v)
{
    const unsigned k = GetN(c, 2);
    if (c->bad || k > kMaxBodies) { c->bad = true; return; }
    size_t all = 0;
    for (unsigned i = 0; i < k && !c->bad; ++i)
    {
        const unsigned len = GetN(c, 4);
        if (c->bad || len == 0 || len > kMaxBody || c->n - c->at < (size_t)len) { c->bad = true; return; }
        all += len;
        if (all > kMaxBodyBytes) { c->bad = true; return; }
        v->push_back(std::string((const char*)c->p + c->at, (size_t)len));
        c->at += len;
    }
}
/* build 3: a no-room refusal's rows - at most kMaxItems, each an offer row index (below kMaxItems); `allowed` 0 = must be empty */
inline bool RowsOk(const std::vector<unsigned>& v, bool allowed)
{
    if (!allowed) return v.empty();
    if (v.size() > kMaxItems) return false;
    for (size_t i = 0; i < v.size(); ++i) if (v[i] >= kMaxItems) return false;
    return true;
}
inline void PutRows(std::vector<char>* b, const std::vector<unsigned>& v) { PutU16(b, (unsigned)v.size()); for (size_t i = 0; i < v.size(); ++i) PutU32(b, v[i]); }
inline void GetRows(Cur* c, std::vector<unsigned>* v)
{
    const unsigned k = GetN(c, 2);
    if (c->bad || k > kMaxItems) { c->bad = true; return; }
    for (unsigned i = 0; i < k && !c->bad; ++i) v->push_back(GetN(c, 4));
}
/* false = a message this header would not send (a bad kind, an answer or reason out of range, a bad offer) - nothing written */
inline bool Encode(const Msg& m, std::vector<char>* out)
{
    std::vector<char> b;
    PutU8(&b, m.kind); PutU64(&b, m.id);
    switch (m.kind)
    {
    case kAsk: case kAsked: PutU32(&b, m.slot); PutU32(&b, m.uidFrom); PutU32(&b, m.uidTo); break;
    case kAnswer: if (m.code > kAnswerMax) return false; PutU32(&b, m.code); break;
    case kCancel: if (m.code == 0 || m.code > kWhyMax) return false; PutU32(&b, m.code); break;
    case kOffer:
        if (!OfferOk(m.offer)) return false;
        PutU32(&b, m.offer.serial); PutU32(&b, m.offer.cats); PutU16(&b, (unsigned)m.offer.items.size());
        for (size_t i = 0; i < m.offer.items.size(); ++i)
        {
            const Item& it = m.offer.items[i];
            PutStr(&b, it.key); PutStr(&b, it.company); PutStr(&b, it.material); PutStr(&b, it.colour); PutU32(&b, it.q); PutU32(&b, it.charges); PutU8(&b, it.unique);
            PutStr(&b, it.name); PutU32(&b, it.count);
        }
        break;
    case kAccept: PutU32(&b, m.serialMine); PutU32(&b, m.serialTheirs); break;
    case kAccepts:
        if (m.you > 1 || m.they > 1 || m.ready > 1) return false;
        PutU32(&b, m.serialMine); PutU32(&b, m.serialTheirs); PutU8(&b, m.you); PutU8(&b, m.they); PutU8(&b, m.ready); break;
    case kLockNow:
        if (m.cats > kMaxCats) return false;
        PutU32(&b, m.serialMine); PutU32(&b, m.serialTheirs); PutU32(&b, m.slot); PutU32(&b, m.cats); break;
    case kLocked:
        if (m.cats > kMaxCats || !BodiesOk(m.bodies) || (m.cats > 0 && m.transfer == 0)) return false;
        PutU32(&b, m.serialMine); PutU32(&b, m.cats); PutU64(&b, m.transfer); PutBodies(&b, m.bodies); break;
    case kLockRefused:
        if (m.code > kRefuseMax || !RowsOk(m.rows, m.code == kRefNoRoom)) return false;
        PutU32(&b, m.row); PutU32(&b, m.code); PutRows(&b, m.rows); break;
    case kCommit:
        if (m.cats > kMaxCats || !BodiesOk(m.bodies) || (m.cats > 0 && m.inbox == 0)) return false;
        PutU32(&b, m.serialMine); PutU32(&b, m.serialTheirs); PutU32(&b, m.slot); PutU32(&b, m.cats); PutU64(&b, m.inbox); PutBodies(&b, m.bodies); break;
    case kAbort:
        if (m.code == 0 || m.code > kAbortMax || !RowsOk(m.rows, m.code == kAbNoRoom)) return false;
        PutU32(&b, m.code); PutU32(&b, m.slot); PutU32(&b, m.row); PutRows(&b, m.rows); break;
    case kApplied: break;
    default: return false;
    }
    out->swap(b);
    return true;
}
inline bool Decode(const void* p, size_t n, Msg* out)
{
    Cur c; c.p = (const unsigned char*)p; c.n = p ? n : 0; c.at = 0; c.bad = false;
    Msg m;
    m.kind = GetN(&c, 1); m.id = GetU64(&c);
    if (c.bad) return false;
    switch (m.kind)
    {
    case kAsk: case kAsked: m.slot = GetN(&c, 4); m.uidFrom = GetN(&c, 4); m.uidTo = GetN(&c, 4); break;
    case kAnswer: m.code = GetN(&c, 4); if (!c.bad && m.code > kAnswerMax) return false; break;
    case kCancel: m.code = GetN(&c, 4); if (!c.bad && (m.code == 0 || m.code > kWhyMax)) return false; break;
    case kOffer:
    {
        m.offer.serial = GetN(&c, 4); m.offer.cats = GetN(&c, 4);
        const unsigned k = GetN(&c, 2);
        if (c.bad || k > kMaxItems) return false;
        for (unsigned i = 0; i < k && !c.bad; ++i)
        {
            Item it; GetStr(&c, &it.key, kMaxKey); GetStr(&c, &it.company, kMaxKey); GetStr(&c, &it.material, kMaxKey); GetStr(&c, &it.colour, kMaxKey);
            it.q = GetN(&c, 4); it.charges = GetN(&c, 4); it.unique = GetN(&c, 1); GetStr(&c, &it.name, kMaxName); it.count = GetN(&c, 4);
            m.offer.items.push_back(it);
        }
        if (!c.bad && !OfferOk(m.offer)) return false;
        break;
    }
    case kAccept: m.serialMine = GetN(&c, 4); m.serialTheirs = GetN(&c, 4); break;
    case kAccepts:
        m.serialMine = GetN(&c, 4); m.serialTheirs = GetN(&c, 4); m.you = GetN(&c, 1); m.they = GetN(&c, 1); m.ready = GetN(&c, 1);
        if (!c.bad && (m.you > 1 || m.they > 1 || m.ready > 1)) return false;
        break;
    case kLockNow:
        m.serialMine = GetN(&c, 4); m.serialTheirs = GetN(&c, 4); m.slot = GetN(&c, 4); m.cats = GetN(&c, 4);
        if (!c.bad && m.cats > kMaxCats) return false;
        break;
    case kLocked:
        m.serialMine = GetN(&c, 4); m.cats = GetN(&c, 4); m.transfer = GetU64(&c); GetBodies(&c, &m.bodies);
        if (!c.bad && (m.cats > kMaxCats || (m.cats > 0 && m.transfer == 0))) return false;
        break;
    case kLockRefused:
        m.row = GetN(&c, 4); m.code = GetN(&c, 4); GetRows(&c, &m.rows);
        if (!c.bad && (m.code > kRefuseMax || !RowsOk(m.rows, m.code == kRefNoRoom))) return false;
        break;
    case kCommit:
        m.serialMine = GetN(&c, 4); m.serialTheirs = GetN(&c, 4); m.slot = GetN(&c, 4); m.cats = GetN(&c, 4); m.inbox = GetU64(&c); GetBodies(&c, &m.bodies);
        if (!c.bad && (m.cats > kMaxCats || (m.cats > 0 && m.inbox == 0))) return false;
        break;
    case kAbort:
        m.code = GetN(&c, 4); m.slot = GetN(&c, 4); m.row = GetN(&c, 4); GetRows(&c, &m.rows);
        if (!c.bad && (m.code == 0 || m.code > kAbortMax || !RowsOk(m.rows, m.code == kAbNoRoom))) return false;
        break;
    case kApplied: break;
    default: return false;
    }
    if (c.bad || c.at != c.n) return false;
    *out = m;
    return true;
}

/* ---- THE WORLD SERVER'S DECISION (store_main.cpp OnTrade; the offline suite drives it as a table) ----
   from: the sending game's slot (the server's own, never the message's); connected: the slots of the admitted games now. Each
   answer is one Out {slot, message}; the server sends it to that slot's game. */
struct Out { int slot; Msg m; Out() : slot(-1) {} Out(int s, const Msg& x) : slot(s), m(x) {} };
struct Book { std::map<unsigned long long, Trade> trades; };
enum Srv { kSrvAsked = 1, kSrvRefused = 2, kSrvRepeat = 3, kSrvOpened = 4, kSrvAnswerNo = 5, kSrvRelayed = 6, kSrvStaleOffer = 7,
           kSrvAccepted = 8, kSrvAcceptStale = 9, kSrvReady = 10, kSrvCancelled = 11, kSrvUnknown = 12, kSrvBad = 13, kSrvNotOpen = 14 };
const int kSrvCount = 15;
inline const char* SrvName(int r)
{
    switch (r)
    {
    case kSrvAsked: return "asked"; case kSrvRefused: return "refused"; case kSrvRepeat: return "repeat"; case kSrvOpened: return "opened";
    case kSrvAnswerNo: return "answer-no"; case kSrvRelayed: return "relayed"; case kSrvStaleOffer: return "stale-offer"; case kSrvAccepted: return "accepted";
    case kSrvAcceptStale: return "accept-stale"; case kSrvReady: return "READY"; case kSrvCancelled: return "cancelled"; case kSrvUnknown: return "unknown-trade";
    case kSrvBad: return "bad"; case kSrvNotOpen: return "not-open"; default: return "?";
    }
}
inline int SideOf(const Trade& t, int slot) { return t.slot[0] == slot ? 0 : (t.slot[1] == slot ? 1 : -1); }
inline bool InAnyTrade(const Book& b, int slot)
{
    for (std::map<unsigned long long, Trade>::const_iterator it = b.trades.begin(); it != b.trades.end(); ++it) if (SideOf(it->second, slot) >= 0) return true;
    return false;
}
inline Msg AcceptsFor(const Trade& t, int side)
{
    Msg m; m.kind = kAccepts; m.id = t.id;
    m.serialMine = t.offer[side].serial; m.serialTheirs = t.offer[1 - side].serial;
    m.you = t.accepted[side] != 0 ? 1u : 0u; m.they = t.accepted[1 - side] != 0 ? 1u : 0u; m.ready = Ready(t) ? 1u : 0u;
    return m;
}
inline Msg AnswerMsg(unsigned long long id, unsigned a) { Msg m; m.kind = kAnswer; m.id = id; m.code = a; return m; }
inline Msg CancelMsg(unsigned long long id, unsigned w) { Msg m; m.kind = kCancel; m.id = id; m.code = w; return m; }
/* every trade `slot` is in ends; the other side is told CANCEL `why` (a game that left, or asks anew: it holds one window only) */
inline int EndTradesOf(Book* b, int slot, unsigned why, std::vector<Out>* outs)
{
    int n = 0;
    for (std::map<unsigned long long, Trade>::iterator it = b->trades.begin(); it != b->trades.end(); )
    {
        const int side = SideOf(it->second, slot);
        if (side < 0) { ++it; continue; }
        outs->push_back(Out(it->second.slot[1 - side], CancelMsg(it->first, why)));
        b->trades.erase(it++);
        ++n;
    }
    return n;
}
inline int ServerStep(Book* b, int from, const Msg& in, const std::set<int>& connected, std::vector<Out>* outs)
{
    if (from < 0) return kSrvBad;
    if (in.kind == kAsk)
    {
        const int to = in.slot <= 0x7FFFFFFFu ? (int)in.slot : -1;
        std::map<unsigned long long, Trade>::const_iterator have = b->trades.find(in.id);
        if (have != b->trades.end()) return have->second.slot[0] == from ? kSrvRepeat : kSrvBad;
        if (to < 0 || to == from) { outs->push_back(Out(from, AnswerMsg(in.id, kAnsNotPlayers))); return kSrvRefused; }
        EndTradesOf(b, from, kWhyLeft, outs);   /* the asker holds one window: an older trade of its own is over */
        if (connected.count(to) == 0) { outs->push_back(Out(from, AnswerMsg(in.id, kAnsGone))); return kSrvRefused; }
        if (InAnyTrade(*b, to)) { outs->push_back(Out(from, AnswerMsg(in.id, kAnsBusy))); return kSrvRefused; }
        Trade t; t.id = in.id; t.state = kStAsked; t.slot[0] = from; t.slot[1] = to; t.uid[0] = in.uidFrom; t.uid[1] = in.uidTo;
        b->trades[in.id] = t;
        Msg a; a.kind = kAsked; a.id = in.id; a.slot = (unsigned)from; a.uidFrom = in.uidFrom; a.uidTo = in.uidTo;
        outs->push_back(Out(to, a));
        return kSrvAsked;
    }
    std::map<unsigned long long, Trade>::iterator it = b->trades.find(in.id);
    if (it == b->trades.end()) return kSrvUnknown;
    Trade& t = it->second;
    const int side = SideOf(t, from);
    if (side < 0) return kSrvUnknown;
    switch (in.kind)
    {
    case kAnswer:
        if (side != 1 || t.state != kStAsked) return kSrvNotOpen;
        outs->push_back(Out(t.slot[0], AnswerMsg(t.id, in.code)));
        if (in.code == kAnsOpen) { t.state = kStOpen; return kSrvOpened; }
        b->trades.erase(it);
        return kSrvAnswerNo;
    case kOffer:
    {
        const int r = SetOffer(&t, side, in.offer);
        if (r == kStepNotOpen) return kSrvNotOpen;
        if (r == kStepBad) return kSrvBad;
        if (r == kStepRepeat) return kSrvStaleOffer;
        Msg o; o.kind = kOffer; o.id = t.id; o.offer = t.offer[side];
        outs->push_back(Out(t.slot[1 - side], o));
        outs->push_back(Out(t.slot[0], AcceptsFor(t, 0)));
        outs->push_back(Out(t.slot[1], AcceptsFor(t, 1)));
        return kSrvRelayed;
    }
    case kAccept:
    {
        const int r = Accept(&t, side, in.serialMine, in.serialTheirs);
        if (r == kStepNotOpen) return kSrvNotOpen;
        if (r == kStepBad) return kSrvBad;
        outs->push_back(Out(t.slot[0], AcceptsFor(t, 0)));
        outs->push_back(Out(t.slot[1], AcceptsFor(t, 1)));
        return r == kStepReady ? kSrvReady : (r == kStepAccepted ? kSrvAccepted : kSrvAcceptStale);   /* PIECE 3: the lock begins at kSrvReady */
    }
    case kCancel:
        outs->push_back(Out(t.slot[1 - side], CancelMsg(t.id, in.code)));
        b->trades.erase(it);
        return kSrvCancelled;
    default:
        return kSrvBad;
    }
}

/* T-1167 piece 3 build 3 (owner 1641: "if no room the trade doesn't happen, the window stays open"): a lock ABORTED for no room puts
   the trade back on the relay as it was at READY - both offers and their serials kept, BOTH accepts cleared - and tells both games
   the accept state (ACCEPTS). `frozen`: the relay's trade as READY took it (store_main.cpp keeps it while the lock is undecided).
   1 back open; 0 the relay already holds that id (nothing changed); -1 not a trade that was READY. */
inline int Reopen(Book* b, const Trade& frozen, std::vector<Out>* outs)
{
    if (frozen.id == 0 || frozen.state != kStOpen || frozen.slot[0] < 0 || frozen.slot[1] < 0) return -1;
    if (b->trades.count(frozen.id) != 0) return 0;
    Trade t = frozen;
    t.accepted[0] = 0; t.accepted[1] = 0;
    b->trades[t.id] = t;
    outs->push_back(Out(t.slot[0], AcceptsFor(t, 0)));
    outs->push_back(Out(t.slot[1], AcceptsFor(t, 1)));
    return 1;
}

/* ---- the game's side: what a TRADE that arrived does to this game's one trade (tradewin.cpp) ---- */
enum Arrive { kArrNone = 0, kArrOpened = 1, kArrRefused = 2, kArrTheirOffer = 3, kArrAccepts = 4, kArrAcceptsOld = 5, kArrCancelled = 6, kArrOther = 7 };
/* t: this game's trade (side `mine`); m: a message for it (the same id - the caller checks). The window acts on the answer. */
inline int GameApply(Trade* t, int mine, const Msg& m)
{
    switch (m.kind)
    {
    case kAnswer:
        if (mine != 0 || t->state != kStAsked) return kArrOther;
        if (m.code != kAnsOpen) return kArrRefused;
        t->state = kStOpen;
        return kArrOpened;
    case kOffer:
        return SetOffer(t, 1 - mine, m.offer) == kStepChanged ? kArrTheirOffer : kArrOther;
    case kAccepts:
        if (m.serialMine != t->offer[mine].serial || m.serialTheirs != t->offer[1 - mine].serial) return kArrAcceptsOld;   /* an older state: a newer one follows */
        t->accepted[mine] = m.you != 0 ? 1 : 0; t->accepted[1 - mine] = m.they != 0 ? 1 : 0;
        return kArrAccepts;
    case kCancel:
        return kArrCancelled;
    default:
        return kArrOther;
    }
}

/* ---- words for the logs and the lever (never shown in the window) ---- */
inline std::string N(unsigned long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
inline std::string OfferText(const Offer& o)
{
    std::string s = "serial=" + N(o.serial) + " cats=" + N(o.cats) + " items=[";
    for (size_t i = 0; i < o.items.size(); ++i)
    {
        const Item& it = o.items[i];
        s += (i ? "," : "") + it.key + (it.company.empty() ? std::string() : "/" + it.company) + (it.material.empty() ? std::string() : "/" + it.material)
           + (it.colour.empty() ? std::string() : "/" + it.colour) + "#q" + N(it.q) + "#c" + N(it.charges) + (it.unique ? "#unique" : "") + " '" + it.name + "':" + N(it.count);
    }
    return s + "]";
}
/* the money box's text against the purse (owner 1633 addition; the GIVE MONEY box's words, moneywire.h RefusedHint): an empty box
   offers no money; digits up to what the purse holds are offered; more than the purse holds is refused "You don't have that much.";
   anything the box cannot hold as an amount (not digits, or past what a purse can hold) is refused "Type an amount.". */
enum Amount { kAmtOk = 0, kAmtTooMuch = 1, kAmtBad = 2 };
inline int AmountCheck(const std::string& text, long long have, unsigned* cats)
{
    *cats = 0;
    unsigned long long v = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9') return kAmtBad;
        v = v * 10 + (unsigned long long)(text[i] - '0');
        if (v > kMaxCats) return kAmtBad;
    }
    if ((long long)v > (have < 0 ? 0 : have)) return kAmtTooMuch;
    *cats = (unsigned)v;
    return kAmtOk;
}

}   // namespace tradew
