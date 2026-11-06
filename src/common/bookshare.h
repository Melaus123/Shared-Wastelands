#pragma once
/* src/common/bookshare.h - T-1242 (design 3, chosen by the manager 2026-11-05): RESEARCH BOOKS IN A TEAMMATE'S BENCH.
 *
 * The owner's rule (teamresearch.h:5-13): research is shared by the team. The engine counts a research's Books only in benches of the
 * LOCAL player faction (0x82D880: the 'nearest building' search, kind 8, faction PlayerInterface+0x2A0, one map cell around the view
 * point - decomp_82d880 :20-33, decomp_9fb580 :14-32, decomp_8c9700 :24-25), so a teammate's bench - under its owner's stand-in here -
 * never counts (run T2047, Confirmed). What the plugin does (store.cpp, the T-1242 block):
 *   COUNT  the bench list 0x82D880, when the book count 0x82D940 asks for it, also holds the CURRENT teammates' research benches in the
 *          same range (the teammates read at that moment: PolicyTeammateFactionList), so the Add button and canPay see them.
 *   SPEND  the pay 0x8334F0 (the only place the engine takes Books, through 0x832050 -> 0x82DA30) first asks canPay with the counts
 *          recorded per cost row (PlanShares). Own Books enough = the engine's own road, unchanged. Otherwise nothing is paid or queued
 *          now: each game whose bench holds the missing Books is ASKED to take its share from its own bench through the engine's own
 *          take (0x82DA30 over that one bench) and answers. Only after every yes does this game queue through the engine's own
 *          addToQueue, its count asking its own benches for need - remote (CommitNeed), so the engine pays the own share and writes its
 *          own 'already paid' record. A no, a lost link or no answer within kAnswerWaitMs: refused, and every game that may have taken
 *          is told to GIVE BACK. The holder takes at the ask (the reservation: a second ask for the same Book finds it gone).
 *   Single player and own Books are the engine's own road.
 *
 * MSG_RESEARCH_BOOK (88, game-to-game protocol 211), to one player by slot, RELIABLE:
 *   u8 op | u32 reqId | u16 askerSlot | u8 why | u8 len + tech sid | u8 n | n x (u8 len + bench key | u8 len + item sid | i32 count)
 *   ASK      asker -> holder: take these counts from these benches of yours (n >= 1, count >= 1).
 *   ANSWER   holder -> asker: why 0 = yes, rows = what was taken; else no (kWhy*), rows empty.
 *   GIVEBACK asker -> holder: put back what you took for reqId (rows empty: the holder gives back its own record).
 *   DONE     asker -> holder: queued; forget reqId (rows empty).
 *   ACK      holder -> asker: the GIVEBACK or DONE for reqId arrived (why = that op, rows empty) - sent for every one, a repeat too.
 * THE OWED MESSAGES (review fold 3): the asker keeps every GIVEBACK and DONE it sends (OwedList) until the holder's ACK, and sends them
 * again at its next WELCOME and when the holder's slot comes back into the world; both are idempotent on the holder (HolderClose: a
 * repeat finds no record and gives nothing). The holder keeps its record until a GIVEBACK or DONE, or until its ASKER LEAVES THE
 * SESSION (fold 7, HolderAskerLeft): then each record no DONE has reached is given back and cleared (a DONE already cleared its own),
 * because an asker that leaves may never send its owed GIVEBACK (the owed list lives only in its running game). The one risk kept: a
 * DONE sent but lost in the very link drop that ends the session - the holder then gives back Books the research was paid with (a
 * duplicate); accepted, because the other way loses a player's Book for good. An asker that rejoins and sends its owed GIVEBACK / DONE
 * again finds no record and is acknowledged with nothing given back. The record also ends with this game's world: the teardown (the
 * load's resetGame) drops it, so a GIVEBACK or DONE that comes after a load finds no record and is acknowledged with nothing given
 * back - the loaded save decides that world's benches. A game that crashes holding an open record leaves those Books spent.
 * Pure: no engine, no Windows; the offline suite compiles it. C++03 (VS2010 v100). */
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstring>

namespace bookshare {

const unsigned int kMsgResearchBook = 88;      /* net/transport.h MSG_RESEARCH_BOOK, liveenvelope.h kInnerResearchBook */
const unsigned int kGameProtocolBooks = 211;   /* the game-to-game protocol that carries it */

const unsigned char kOpAsk = 1, kOpAnswer = 2, kOpGiveBack = 3, kOpDone = 4, kOpAck = 5;
const unsigned char kWhyYes = 0, kWhyShort = 1, kWhyNoBench = 2, kWhyNotOwn = 3, kWhyNoResearch = 4, kWhyBlocked = 5,
                    kWhyTakeFault = 6, kWhyNotMate = 7, kWhyUnknownItem = 8;
inline const char* WhyName(unsigned char w)
{
    switch (w)
    {
    case kWhyYes: return "yes";
    case kWhyShort: return "short (the bench holds fewer)";
    case kWhyNoBench: return "no such bench loaded";
    case kWhyNotOwn: return "the bench is not this game's own";
    case kWhyNoResearch: return "no research object";
    case kWhyBlocked: return "world loading or tearing down";
    case kWhyTakeFault: return "the engine's take did not take it all (given back)";
    case kWhyNotMate: return "the asker is not a teammate";
    case kWhyUnknownItem: return "unknown item record";
    default: return "?";
    }
}

const int kMaxRows = 16;
const int kMaxCount = 1000;
const unsigned long kAnswerWaitMs = 15000;     /* the asker gives up a hold no answer completed within this */

struct Row { std::string benchKey, itemSid; int count; Row() : count(0) {} Row(const std::string& k, const std::string& s, int c) : benchKey(k), itemSid(s), count(c) {} };
struct Msg { unsigned char op; unsigned int reqId; unsigned int askerSlot; unsigned char why; std::string tech; std::vector<Row> rows;
             Msg() : op(0), reqId(0), askerSlot(0), why(0) {} };

inline void PutU8(std::vector<char>* b, unsigned int v) { b->push_back((char)(unsigned char)v); }
inline void PutU16(std::vector<char>* b, unsigned int v) { PutU8(b, v & 0xFF); PutU8(b, (v >> 8) & 0xFF); }
inline void PutU32(std::vector<char>* b, unsigned int v) { PutU16(b, v & 0xFFFF); PutU16(b, (v >> 16) & 0xFFFF); }
inline bool PutStr8(std::vector<char>* b, const std::string& s)
{
    if (s.size() > 255) return false;
    PutU8(b, (unsigned int)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return true;
}
inline bool StrOk(const std::string& s) { return !s.empty() && s.find('\t') == std::string::npos && s.find('\n') == std::string::npos && s.find('\r') == std::string::npos; }

/* false (nothing usable in *b) = a field out of its bounds: an op outside 1..5, a slot over 0xFFFF, an empty / over-long string, an
   ASK with no row, a row count over kMaxRows, a count outside 1..kMaxCount, rows on GIVEBACK / DONE / ACK / a no, an ACK of
   anything but a GIVEBACK or a DONE */
inline bool AckOk(const Msg& m) { return m.op != kOpAck || m.why == kOpGiveBack || m.why == kOpDone; }
inline bool Encode(const Msg& m, std::vector<char>* b)
{
    b->clear();
    if (m.op < kOpAsk || m.op > kOpAck || m.askerSlot > 0xFFFFu || !StrOk(m.tech) || m.rows.size() > (size_t)kMaxRows || !AckOk(m)) return false;
    if (m.op == kOpAsk && m.rows.empty()) return false;
    if ((m.op == kOpGiveBack || m.op == kOpDone || m.op == kOpAck || (m.op == kOpAnswer && m.why != kWhyYes)) && !m.rows.empty()) return false;
    PutU8(b, m.op); PutU32(b, m.reqId); PutU16(b, m.askerSlot); PutU8(b, m.why);
    if (!PutStr8(b, m.tech)) { b->clear(); return false; }
    PutU8(b, (unsigned int)m.rows.size());
    for (size_t i = 0; i < m.rows.size(); ++i)
    {
        const Row& r = m.rows[i];
        if (!StrOk(r.benchKey) || !StrOk(r.itemSid) || r.count < 1 || r.count > kMaxCount || !PutStr8(b, r.benchKey) || !PutStr8(b, r.itemSid)) { b->clear(); return false; }
        PutU32(b, (unsigned int)r.count);
    }
    return true;
}
struct Rd
{
    const unsigned char* p; size_t n, at; bool ok;
    Rd(const char* q, size_t len) : p((const unsigned char*)q), n(len), at(0), ok(q != 0 || len == 0) {}
    unsigned int U8() { if (!ok || at + 1 > n) { ok = false; return 0; } return p[at++]; }
    unsigned int U16() { const unsigned int a = U8(); const unsigned int b = U8(); return a | (b << 8); }
    unsigned int U32() { const unsigned int a = U16(); const unsigned int b = U16(); return a | (b << 16); }
    std::string Str8() { const unsigned int l = U8(); if (!ok || at + l > n) { ok = false; return std::string(); } std::string s((const char*)p + at, l); at += l; return s; }
};
/* true = exactly one well-formed message (the same bounds as Encode; no trailing byte) */
inline bool Decode(const char* p, size_t n, Msg* m)
{
    *m = Msg();
    Rd r(p, n);
    m->op = (unsigned char)r.U8(); m->reqId = r.U32(); m->askerSlot = r.U16(); m->why = (unsigned char)r.U8();
    m->tech = r.Str8();
    const unsigned int rows = r.U8();
    if (!r.ok || m->op < kOpAsk || m->op > kOpAck || !StrOk(m->tech) || rows > (unsigned int)kMaxRows || !AckOk(*m)) return false;
    for (unsigned int i = 0; i < rows; ++i)
    {
        Row row; row.benchKey = r.Str8(); row.itemSid = r.Str8(); row.count = (int)r.U32();
        if (!r.ok || !StrOk(row.benchKey) || !StrOk(row.itemSid) || row.count < 1 || row.count > kMaxCount) return false;
        m->rows.push_back(row);
    }
    if (r.at != n) return false;
    if (m->op == kOpAsk && m->rows.empty()) return false;
    if ((m->op == kOpGiveBack || m->op == kOpDone || m->op == kOpAck || (m->op == kOpAnswer && m->why != kWhyYes)) && !m->rows.empty()) return false;
    return true;
}

/* ---- THE ASKER'S PLAN: one cost row of the tech as the engine's count met it (0x82D940's data and need), with this game's own
   benches' total of that item and each teammate bench in range with its total ---- */
struct MateBench { int slot; std::string key; long long have; MateBench() : slot(-1), have(0) {} MateBench(int s, const std::string& k, long long h) : slot(s), key(k), have(h) {} };
struct CostRow { std::string sid; int need; long long own; std::vector<MateBench> mates; CostRow() : need(0), own(0) {} };
struct Share { int slot; Row row; Share() : slot(-1) {} };
const int kPlanOwn = 0, kPlanShare = 1, kPlanShort = 2;
/* kPlanOwn: every row's own total covers its need (or no row at all: the tech costs nothing or is already paid) - the engine's own
   road. kPlanShare: *out = the teammates' shares (own Books first, then each teammate bench in the order met, each Book counted once
   even when two rows name the same item and bench). kPlanShort: the counts do not cover a row (out empty). */
inline int PlanShares(const std::vector<CostRow>& rows, std::vector<Share>* out)
{
    out->clear();
    std::map<std::string, long long> used;   /* bench key + '|' + sid -> already planned */
    std::map<std::string, long long> ownUsed;
    bool any = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const CostRow& c = rows[i];
        if (c.need <= 0 || c.sid.empty()) continue;
        const long long ownLeft = (c.own > 0 ? c.own : 0) - ownUsed[c.sid];
        if (ownLeft >= c.need) { ownUsed[c.sid] += c.need; continue; }
        long long left = c.need - (ownLeft > 0 ? ownLeft : 0);
        if (ownLeft > 0) ownUsed[c.sid] += ownLeft;
        any = true;
        for (size_t k = 0; k < c.mates.size() && left > 0; ++k)
        {
            const MateBench& mb = c.mates[k];
            if (mb.slot < 0 || mb.key.empty()) continue;
            const std::string uk = mb.key + "|" + c.sid;
            const long long avail = mb.have - used[uk];
            if (avail <= 0) continue;
            const long long take = avail < left ? avail : left;
            Share s; s.slot = mb.slot; s.row = Row(mb.key, c.sid, (int)take);
            out->push_back(s);
            used[uk] += take; left -= take;
        }
        if (left > 0) { out->clear(); return kPlanShort; }
    }
    return any ? kPlanShare : kPlanOwn;
}
/* the games asked, each once, in the order first met */
inline std::vector<int> SlotsOf(const std::vector<Share>& s)
{
    std::vector<int> v;
    for (size_t i = 0; i < s.size(); ++i) { bool seen = false; for (size_t k = 0; k < v.size(); ++k) if (v[k] == s[i].slot) seen = true; if (!seen) v.push_back(s[i].slot); }
    return v;
}
inline std::vector<Row> RowsFor(const std::vector<Share>& s, int slot)
{
    std::vector<Row> v;
    for (size_t i = 0; i < s.size(); ++i) if (s[i].slot == slot) v.push_back(s[i].row);
    return v;
}
/* the Books of `sid` the teammates took (their confirmed rows) */
inline int RemoteOf(const std::vector<Row>& taken, const std::string& sid)
{
    long long n = 0;
    for (size_t i = 0; i < taken.size(); ++i) if (taken[i].itemSid == sid) n += taken[i].count;
    return n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n;
}
/* THE COMMIT: the engine's count asks this game's own benches for what the teammates did not take */
inline int CommitNeed(int need, int remote) { const int n = need - (remote > 0 ? remote : 0); return n < 0 ? 0 : n; }

/* ---- THE ASKER'S HOLD (one at a time) ---- */
struct Hold
{
    bool on; unsigned int reqId; std::string tech; unsigned long startMs;
    std::set<int> asked, yes, no;
    std::map<int, std::vector<Row> > taken;
    Hold() : on(false), reqId(0), startMs(0) {}
};
const int kHoldWait = 0, kHoldCommit = 1, kHoldRefuse = 2, kHoldIgnored = 3;
/* an ANSWER: ignored unless it is this hold's, from a game asked and not yet answered; a no refuses at once; the last yes commits */
inline int HoldAnswer(Hold* h, int slot, unsigned int reqId, bool yes, const std::vector<Row>& rows)
{
    if (!h->on || reqId != h->reqId || h->asked.count(slot) == 0 || h->yes.count(slot) != 0 || h->no.count(slot) != 0) return kHoldIgnored;
    if (yes) { h->yes.insert(slot); h->taken[slot] = rows; }
    else h->no.insert(slot);
    if (!h->no.empty()) return kHoldRefuse;
    return h->yes.size() == h->asked.size() ? kHoldCommit : kHoldWait;
}
inline bool HoldTimedOut(const Hold& h, unsigned long nowMs) { return h.on && (unsigned long)(nowMs - h.startMs) >= kAnswerWaitMs; }
/* on a refusal (or a failed commit): every asked game that did not say no is told to give back - a yes took, and an unanswered one
   may take before the GIVEBACK reaches it (one ordered road per game, so the GIVEBACK comes after the ASK there) */
inline std::vector<int> GiveBackSlots(const Hold& h)
{
    std::vector<int> v;
    for (std::set<int>::const_iterator it = h.asked.begin(); it != h.asked.end(); ++it) if (h.no.count(*it) == 0) v.push_back(*it);
    return v;
}
/* every row the teammates confirmed */
inline std::vector<Row> TakenAll(const Hold& h)
{
    std::vector<Row> v;
    for (std::map<int, std::vector<Row> >::const_iterator it = h.taken.begin(); it != h.taken.end(); ++it) v.insert(v.end(), it->second.begin(), it->second.end());
    return v;
}
/* a yes for no current hold (it ended first): what was taken is given back */
inline bool LateYesGivesBack(const Hold& h, unsigned int reqId, bool yes) { return yes && (!h.on || h.reqId != reqId); }
/* the asker's request ids: this game's slot in the top byte, below it a counter whose first value comes from the clock, so a game that
   restarts does not reuse an id its earlier run left in a holder's record (records now live until a GIVEBACK or DONE); never 0 below */
inline unsigned int NextReqId(int mySlot, unsigned int* counter, unsigned long seedMs)
{
    if (*counter == 0) *counter = (unsigned int)(seedMs & 0xFFFFFFul);
    *counter = (*counter + 1u) & 0xFFFFFFu;
    if (*counter == 0) *counter = 1;
    return ((unsigned int)(mySlot & 0xFF) << 24) | *counter;
}

/* ---- THE ASKER'S OWED MESSAGES: each GIVEBACK and DONE it sends, until the holder's ACK ---- */
struct Owed { int slot; unsigned int reqId; unsigned char op; std::string tech; Owed() : slot(-1), reqId(0), op(0) {} };
typedef std::vector<Owed> OwedList;
const size_t kMaxOwed = 32;
/* adds one (the same slot, request and op already owed: nothing added); false = the list was full and its oldest was dropped (copied
   to *dropped when given, so the caller can name it) */
inline bool OwedAdd(OwedList* l, int slot, unsigned int reqId, unsigned char op, const std::string& tech, Owed* dropped = 0)
{
    for (size_t i = 0; i < l->size(); ++i) if ((*l)[i].slot == slot && (*l)[i].reqId == reqId && (*l)[i].op == op) return true;
    bool room = true;
    if (l->size() >= kMaxOwed) { if (dropped != 0) *dropped = l->front(); l->erase(l->begin()); room = false; }
    Owed o; o.slot = slot; o.reqId = reqId; o.op = op; o.tech = tech;
    l->push_back(o);
    return room;
}
/* the holder's ACK of `op` for reqId: true = an owed message ended (a repeated ACK ends nothing) */
inline bool OwedAck(OwedList* l, int slot, unsigned int reqId, unsigned char op)
{
    for (size_t i = 0; i < l->size(); ++i)
        if ((*l)[i].slot == slot && (*l)[i].reqId == reqId && (*l)[i].op == op) { l->erase(l->begin() + i); return true; }
    return false;
}

/* ---- THE HOLDER ---- */
/* the asked rows with the same bench and item summed, in the order first met (a Book is counted once against its bench) */
inline std::vector<Row> GroupRows(const std::vector<Row>& rows)
{
    std::vector<Row> v;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        bool merged = false;
        for (size_t k = 0; k < v.size(); ++k)
            if (v[k].benchKey == rows[i].benchKey && v[k].itemSid == rows[i].itemSid) { v[k].count += rows[i].count; merged = true; break; }
        if (!merged) v.push_back(rows[i]);
    }
    return v;
}
/* one grouped row on the holder: bench found (1 own, 0 none, -1 another's), the item record known, the total it holds now */
inline unsigned char RowVerdict(int bench, int itemKnown, long long have, int want)
{
    if (bench == 0) return kWhyNoBench;
    if (bench < 0) return kWhyNotOwn;
    if (!itemKnown) return kWhyUnknownItem;
    return have >= (long long)want ? kWhyYes : kWhyShort;
}
/* what the holder took, per asker and request, until DONE or GIVEBACK (or this game's world teardown) */
typedef std::map<std::pair<int, unsigned int>, std::vector<Row> > Book;
inline void BookPut(Book* b, int asker, unsigned int reqId, const std::vector<Row>& rows) { if (!rows.empty()) (*b)[std::make_pair(asker, reqId)] = rows; }
/* removes and returns the record (empty = none: a second GIVEBACK or a DONE'd request gives nothing back) */
inline std::vector<Row> BookTake(Book* b, int asker, unsigned int reqId)
{
    std::vector<Row> v;
    Book::iterator it = b->find(std::make_pair(asker, reqId));
    if (it != b->end()) { v = it->second; b->erase(it); }
    return v;
}
/* a GIVEBACK or DONE on the holder: the record removed, *back = its rows for a GIVEBACK (nothing for a DONE). 1 = a record was found;
   0 = none (a repeat, the other op came first, or this game's world was loaded since): nothing is given back. Acknowledged either way. */
inline int HolderClose(Book* b, int asker, unsigned int reqId, unsigned char op, std::vector<Row>* back)
{
    back->clear();
    const std::vector<Row> rows = BookTake(b, asker, reqId);
    if (op == kOpGiveBack) *back = rows;
    return rows.empty() ? 0 : 1;
}
/* fold 7: the asker left the session (store.cpp StoreBookTick: its slot out of this game's world - the leave event the code used before
   fold 3). Every record still held for it is one no DONE has reached (a DONE removes its record at once), so each is removed and
   returned to be given back: *reqIds and *rows in step, the count returned. Other askers' records are untouched; a second call, or a
   late GIVEBACK / DONE (HolderClose), finds no record and gives nothing. */
inline size_t HolderAskerLeft(Book* b, int asker, std::vector<unsigned int>* reqIds, std::vector<std::vector<Row> >* rows)
{
    reqIds->clear(); rows->clear();
    for (Book::iterator it = b->begin(); it != b->end();)
    {
        if (it->first.first == asker) { reqIds->push_back(it->first.second); rows->push_back(it->second); b->erase(it++); }
        else ++it;
    }
    return reqIds->size();
}

/* a bench in range: its map cell within one of the view point's (the engine's search, decomp_9fb580 :24-27) */
inline bool CellNear(int vx, int vy, int bx, int by) { return bx >= vx - 1 && bx <= vx + 1 && by >= vy - 1 && by <= vy + 1; }
/* the map cell a position key names: "<sid>@<x>,<y>@..." (poskey.h); false = no such part */
inline bool KeyCell(const std::string& key, int* x, int* y)
{
    const size_t a = key.find('@');
    if (a == std::string::npos) return false;
    const size_t c = key.find(',', a + 1), b = key.find('@', a + 1);
    if (c == std::string::npos || b == std::string::npos || c > b || c == a + 1 || b == c + 1 || b - a > 12) return false;
    int v[2] = { 0, 0 };
    const size_t from[2] = { a + 1, c + 1 }, to[2] = { c, b };
    for (int k = 0; k < 2; ++k)
        for (size_t i = from[k]; i < to[k]; ++i)
        {
            if (key[i] < '0' || key[i] > '9') return false;
            v[k] = v[k] * 10 + (key[i] - '0');
        }
    *x = v[0]; *y = v[1];
    return true;
}

}   /* namespace bookshare */
