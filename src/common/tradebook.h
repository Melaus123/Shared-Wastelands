// tradebook.h - T-1167 piece 3 (owner 1633 / 1633 addition / 1635 "the swap's commit through the world server"; design A, decided
// by the manager): THE WORLD SERVER'S TRADE BOOK - THE ONE DECISION, WRITTEN ONCE.
// Pure: no disk, no clock, no engine; the world server (store_main.cpp) and the offline suite compile it. The messages are
// tradewire.h's (TRADE 94, kinds 8-13, world-server protocol 155).
//
// A trade enters the book at READY (both sides accepted the same offer serials, tradewire.h ServerStep kSrvReady): LOCKING, and
// LOCK_NOW goes to both games, naming the serials READY named. Each game escrows exactly that offer and answers LOCKED (its objects'
// bytes, its money and a transfer number reserved from its own money ledger) or LOCK_REFUSED. Then, ONCE:
//   both LOCKED with the serials (and the money) READY named         -> COMMITTED
//   a LOCK_REFUSED, a CANCEL, the game leaving (PLAYER_GONE), a LOCKED of another offer, or kLockMs without both LOCKED -> ABORTED
// The decision is taken only while the trade is undecided; the first one written to trade.txt wins and never changes; a repeat of any
// message changes nothing. The caller (store_main.cpp) works on a COPY of the book and writes the copy to trade.txt, through to the
// disk, BEFORE it sends any answer the step produced (MoneyCommit's pattern); a copy that cannot be written is dropped (nothing
// answered, nothing decided).
//   COMMITTED: each side's money is put in the other side's money inbox (moneywire.h BookSend, under the transfer number its LOCKED
//     reserved - money.txt's seen set makes a second try a repeat); Booked marks it. COMMIT goes to a side once the OTHER side's money
//     is booked (or was none): it carries the other side's objects and the inbox number the money landed under. A COMMITTED trade stays
//     in the book until both sides said APPLIED, across links: the WELCOME push (Welcome) sends COMMIT again to a side that has not.
//   ABORTED: ABORT tells both sides to give their own escrow back; it too stays until both said APPLIED (pushed at each WELCOME).
//   At the world server's start every undecided trade becomes ABORTED (StartAbort): nothing was answered for it before it was on disk.
#pragma once

#include "tradewire.h"
#include <string>
#include <vector>
#include <map>
#include <utility>

namespace tradeb {

const long long kLockMs = 10000;             /* LOCK_NOW to both LOCKED at most this long, else ABORTED (timeout) */
const char* const kFileHeader = "SWTRADE 1";
const char* const kFileHeaderStem = "SWTRADE ";   /* a header of another version: the file is not this build's (FileRead refuses it whole) */

enum Phase { kPhOpen = 0, kPhLocking = 1, kPhCommitted = 2, kPhAborted = 3 };   /* kPhOpen: not in the book (the relay's offers / accepts) */
inline const char* PhaseName(int p)
{
    switch (p) { case kPhOpen: return "open"; case kPhLocking: return "locking"; case kPhCommitted: return "committed"; case kPhAborted: return "aborted"; default: return 0; }
}
struct Side
{
    int slot; std::string prof;            /* the player's slot and profile key (the money inbox's key) */
    unsigned serial, cats;                 /* the offer READY named: its serial and its money */
    int locked; unsigned long long transfer; std::vector<std::string> bodies;   /* its LOCKED: the reserved transfer number, the objects */
    int booked; unsigned long long inbox;  /* its money is in the OTHER side's money inbox under `inbox` (booked with cats 0: none to book) */
    int applied;                           /* its APPLIED came */
    unsigned want;                         /* trade.txt load only: the objects its line declares */
    Side() : slot(-1), serial(0), cats(0), locked(0), transfer(0), booked(0), inbox(0), applied(0), want(0) {}
};
struct Rec
{
    unsigned long long id; int phase; unsigned why; int bySlot; unsigned row;   /* why / bySlot / row: an ABORTED's AbortWhy, who, which row */
    long long lockAtMs;                    /* when LOCK_NOW went (memory only: an undecided trade does not outlive a restart) */
    int bodiesShort;                       /* trade.txt load: a side's objects are not all on file - its COMMIT is held, never sent short */
    Side side[2];                          /* 0 = the asker, 1 = the asked (tradewire.h Trade's sides) */
    std::vector<unsigned> rows;            /* build 3: a no-room ABORTED's rows (memory only - trade.txt keeps the reason, not the rows) */
    Rec() : id(0), phase(kPhOpen), why(0), bySlot(-1), row(tradew::kNoRow), lockAtMs(0), bodiesShort(0) {}
};
struct Book { std::map<unsigned long long, Rec> recs; };

enum Res { kBkNone = 0, kBkLocking = 1, kBkLockedOne = 2, kBkCommitted = 3, kBkAborted = 4, kBkRepeat = 5, kBkDecided = 6, kBkUnknown = 7,
           kBkBad = 8, kBkApplied = 9, kBkDone = 10, kBkBooked = 11 };
const int kBkCount = 12;
inline const char* ResName(int r)
{
    switch (r)
    {
    case kBkNone: return "none"; case kBkLocking: return "LOCKING"; case kBkLockedOne: return "locked-one-side"; case kBkCommitted: return "COMMITTED";
    case kBkAborted: return "ABORTED"; case kBkRepeat: return "repeat"; case kBkDecided: return "already-decided"; case kBkUnknown: return "unknown-trade";
    case kBkBad: return "bad"; case kBkApplied: return "applied"; case kBkDone: return "done"; case kBkBooked: return "money-booked"; default: return "?";
    }
}
/* the steps that changed the book (the caller writes trade.txt before it answers) */
inline bool Changes(int r) { return r == kBkLocking || r == kBkLockedOne || r == kBkCommitted || r == kBkAborted || r == kBkApplied || r == kBkDone || r == kBkBooked; }

inline int SideOf(const Rec& r, int slot) { return slot < 0 ? -1 : (r.side[0].slot == slot ? 0 : (r.side[1].slot == slot ? 1 : -1)); }
/* a profile key trade.txt can hold (moneywire.h KeyOk: no control byte, the tab included) */
inline bool ProfOk(const std::string& k)
{
    if (k.empty() || k.size() > 512) return false;
    for (size_t i = 0; i < k.size(); ++i) if ((unsigned char)k[i] < 0x20 || k[i] == 0x7F) return false;
    return true;
}

/* ---- what goes to the games ---- */
inline tradew::Msg LockNowMsg(const Rec& r, int x)
{
    tradew::Msg m; m.kind = tradew::kLockNow; m.id = r.id;
    m.serialMine = r.side[x].serial; m.serialTheirs = r.side[1 - x].serial; m.slot = (unsigned)r.side[1 - x].slot; m.cats = r.side[x].cats;
    return m;
}
/* COMMIT goes to side x only once the other side's money is booked (or was none) and every object of the other side is in hand */
inline bool CommitReady(const Rec& r, int x) { return r.phase == kPhCommitted && r.side[1 - x].booked != 0 && r.bodiesShort == 0; }
inline tradew::Msg CommitMsg(const Rec& r, int x)
{
    const Side& o = r.side[1 - x];
    tradew::Msg m; m.kind = tradew::kCommit; m.id = r.id;
    m.serialMine = r.side[x].serial; m.serialTheirs = o.serial; m.slot = (unsigned)o.slot; m.cats = o.cats; m.inbox = o.cats > 0 ? o.inbox : 0; m.bodies = o.bodies;
    return m;
}
inline tradew::Msg AbortMsg(const Rec& r)
{
    tradew::Msg m; m.kind = tradew::kAbort; m.id = r.id; m.code = r.why; m.slot = r.bySlot < 0 ? tradew::kNoSlot : (unsigned)r.bySlot; m.row = r.row;
    if (r.why == tradew::kAbNoRoom) m.rows = r.rows;   /* build 3: the rows the refusing game had no room for */
    return m;
}
/* the decision, to side x, as it stands (nothing while undecided or not ready) */
inline bool DecisionFor(const Rec& r, int x, tradew::Msg* m)
{
    if (r.phase == kPhAborted) { *m = AbortMsg(r); return true; }
    if (CommitReady(r, x)) { *m = CommitMsg(r, x); return true; }
    return false;
}

/* ---- the steps ---- */
/* ABORTED, only while undecided: both sides told to give their escrow back. The objects are not kept (no one builds them). */
inline bool Abort(Rec* r, unsigned why, int bySlot, unsigned row, std::vector<tradew::Out>* outs, const std::vector<unsigned>* rows = 0)
{
    if (r->phase != kPhLocking) return false;
    r->phase = kPhAborted; r->why = why; r->bySlot = bySlot; r->row = row;
    r->rows.clear();
    if (why == tradew::kAbNoRoom && rows != 0) r->rows = *rows;
    for (int x = 0; x < 2; ++x) r->side[x].bodies.clear();
    const tradew::Msg m = AbortMsg(*r);
    for (int x = 0; x < 2; ++x) outs->push_back(tradew::Out(r->side[x].slot, m));
    return true;
}
/* READY: the relay's trade `t` (both accepted the same serials) enters the book LOCKING; LOCK_NOW to both. prof: each side's profile. */
inline int Begin(Book* b, const tradew::Trade& t, const std::string prof[2], long long nowMs, std::vector<tradew::Out>* outs)
{
    if (!tradew::Ready(t) || t.slot[0] < 0 || t.slot[1] < 0 || t.slot[0] == t.slot[1] || !ProfOk(prof[0]) || !ProfOk(prof[1]) || prof[0] == prof[1]) return kBkBad;
    if (b->recs.count(t.id) != 0) return kBkRepeat;   /* a trade id is decided once: never begun again */
    Rec r; r.id = t.id; r.phase = kPhLocking; r.lockAtMs = nowMs;
    for (int x = 0; x < 2; ++x) { r.side[x].slot = t.slot[x]; r.side[x].prof = prof[x]; r.side[x].serial = t.offer[x].serial; r.side[x].cats = t.offer[x].cats; }
    b->recs[t.id] = r;
    for (int x = 0; x < 2; ++x) outs->push_back(tradew::Out(r.side[x].slot, LockNowMsg(r, x)));
    return kBkLocking;
}
inline bool SameLocked(const Side& s, const tradew::Msg& m) { return m.serialMine == s.serial && m.cats == s.cats && m.transfer == s.transfer && m.bodies == s.bodies; }
/* LOCKED from slot `from` */
inline int Locked(Book* b, int from, const tradew::Msg& m, long long nowMs, std::vector<tradew::Out>* outs)
{
    std::map<unsigned long long, Rec>::iterator it = b->recs.find(m.id);
    if (it == b->recs.end()) return kBkUnknown;
    Rec& r = it->second;
    const int x = SideOf(r, from);
    if (x < 0) return kBkUnknown;
    if (r.phase != kPhLocking)
    {   /* decided: nothing changes; the decision is said again to that side (its LOCKED crossed it) */
        tradew::Msg d;
        if (r.side[x].applied == 0 && DecisionFor(r, x, &d)) outs->push_back(tradew::Out(from, d));
        return kBkDecided;
    }
    Side& s = r.side[x];
    if (s.locked) return SameLocked(s, m) ? kBkRepeat : kBkBad;
    if (nowMs - r.lockAtMs >= kLockMs) { Abort(&r, tradew::kAbTimeout, -1, tradew::kNoRow, outs); return kBkAborted; }
    if (m.serialMine != s.serial || m.cats != s.cats || !tradew::BodiesOk(m.bodies) || (s.cats > 0 && m.transfer == 0))
    {
        Abort(&r, tradew::kAbStale, from, tradew::kNoRow, outs);
        return kBkAborted;
    }
    s.locked = 1; s.transfer = s.cats > 0 ? m.transfer : 0; s.bodies = m.bodies;
    if (!r.side[1 - x].locked) return kBkLockedOne;
    r.phase = kPhCommitted;
    for (int k = 0; k < 2; ++k) { r.side[k].booked = r.side[k].cats == 0 ? 1 : 0; r.side[k].inbox = 0; }
    for (int k = 0; k < 2; ++k) if (CommitReady(r, k)) outs->push_back(tradew::Out(r.side[k].slot, CommitMsg(r, k)));
    return kBkCommitted;
}
/* LOCK_REFUSED from slot `from`: the offer is not all there (m.row) - or (build 3, m.code kRefNoRoom) that game has no room for the
   other side's items (m.rows): ABORTED no-room, the rows told to both; the caller (store_main.cpp) puts the trade back on the relay
   with both accepts cleared (tradewire.h Reopen) - the windows stay open (owner 1641) */
inline int Refused(Book* b, int from, const tradew::Msg& m, std::vector<tradew::Out>* outs)
{
    std::map<unsigned long long, Rec>::iterator it = b->recs.find(m.id);
    if (it == b->recs.end()) return kBkUnknown;
    Rec& r = it->second;
    if (SideOf(r, from) < 0) return kBkUnknown;
    if (r.phase != kPhLocking) return kBkDecided;
    if (m.code == tradew::kRefNoRoom) Abort(&r, tradew::kAbNoRoom, from, tradew::kNoRow, outs, &m.rows);
    else Abort(&r, tradew::kAbRefused, from, m.row, outs);
    return kBkAborted;
}
/* CANCEL from slot `from` for a trade in the book (kBkUnknown: not the book's - the relay's own CANCEL) */
inline int Cancel(Book* b, int from, unsigned long long id, std::vector<tradew::Out>* outs)
{
    std::map<unsigned long long, Rec>::iterator it = b->recs.find(id);
    if (it == b->recs.end() || SideOf(it->second, from) < 0) return kBkUnknown;
    if (it->second.phase != kPhLocking) return kBkDecided;
    Abort(&it->second, tradew::kAbCancel, from, tradew::kNoRow, outs);
    return kBkAborted;
}
/* the game of `slot` left (PLAYER_GONE): its undecided trades ABORTED; a decided trade waits for its WELCOME. The number aborted. */
inline int Gone(Book* b, int slot, std::vector<tradew::Out>* outs)
{
    int n = 0;
    for (std::map<unsigned long long, Rec>::iterator it = b->recs.begin(); it != b->recs.end(); ++it)
        if (it->second.phase == kPhLocking && SideOf(it->second, slot) >= 0 && Abort(&it->second, tradew::kAbGone, slot, tradew::kNoRow, outs)) ++n;
    return n;
}
/* the clock: an undecided trade kLockMs after its LOCK_NOW is ABORTED. The number aborted. */
inline int Tick(Book* b, long long nowMs, std::vector<tradew::Out>* outs)
{
    int n = 0;
    for (std::map<unsigned long long, Rec>::iterator it = b->recs.begin(); it != b->recs.end(); ++it)
        if (it->second.phase == kPhLocking && nowMs - it->second.lockAtMs >= kLockMs && Abort(&it->second, tradew::kAbTimeout, -1, tradew::kNoRow, outs)) ++n;
    return n;
}
/* APPLIED from slot `from`: that side applied the decision; both -> the trade leaves the book (kBkDone) */
inline int Applied(Book* b, int from, unsigned long long id)
{
    std::map<unsigned long long, Rec>::iterator it = b->recs.find(id);
    if (it == b->recs.end()) return kBkUnknown;
    Rec& r = it->second;
    const int x = SideOf(r, from);
    if (x < 0) return kBkUnknown;
    if (r.phase == kPhLocking) return kBkBad;   /* nothing decided: nothing to apply */
    if (r.phase == kPhCommitted && !CommitReady(r, x)) return kBkBad;   /* its COMMIT has not gone yet */
    if (r.side[x].applied) return kBkRepeat;
    r.side[x].applied = 1;
    if (r.side[1 - x].applied) { b->recs.erase(it); return kBkDone; }
    return kBkApplied;
}
/* side x's money is in the other side's money inbox under `inbox`: COMMIT to the other side when it is ready */
inline int Booked(Book* b, unsigned long long id, int x, unsigned long long inbox, std::vector<tradew::Out>* outs)
{
    std::map<unsigned long long, Rec>::iterator it = b->recs.find(id);
    if (it == b->recs.end() || x < 0 || x > 1) return kBkUnknown;
    Rec& r = it->second;
    if (r.phase != kPhCommitted || r.side[x].cats == 0) return kBkBad;
    if (r.side[x].booked) return kBkRepeat;
    r.side[x].booked = 1; r.side[x].inbox = inbox;
    if (CommitReady(r, 1 - x)) outs->push_back(tradew::Out(r.side[1 - x].slot, CommitMsg(r, 1 - x)));
    return kBkBooked;
}
/* the money the world server still has to book: (trade id, side) of each COMMITTED side with money not yet in the other's inbox */
inline std::vector<std::pair<unsigned long long, int> > ToBook(const Book& b)
{
    std::vector<std::pair<unsigned long long, int> > v;
    for (std::map<unsigned long long, Rec>::const_iterator it = b.recs.begin(); it != b.recs.end(); ++it)
        if (it->second.phase == kPhCommitted)
            for (int x = 0; x < 2; ++x) if (it->second.side[x].cats > 0 && !it->second.side[x].booked) v.push_back(std::make_pair(it->first, x));
    return v;
}
/* THE WELCOME PUSH for the game of `slot` (beside the money inbox's): every decision it has not applied, again. An undecided trade of
   that slot is ABORTED first (a game admitted anew holds no lock state of this link). The number aborted (the caller writes first). */
inline int Welcome(Book* b, int slot, std::vector<tradew::Out>* outs)
{
    int n = 0;
    for (std::map<unsigned long long, Rec>::iterator it = b->recs.begin(); it != b->recs.end(); ++it)
    {
        Rec& r = it->second;
        const int x = SideOf(r, slot);
        if (x < 0) continue;
        if (r.phase == kPhLocking) { if (Abort(&r, tradew::kAbGone, slot, tradew::kNoRow, outs)) ++n; continue; }   /* its ABORT is in outs */
        tradew::Msg d;
        if (r.side[x].applied == 0 && DecisionFor(r, x, &d)) outs->push_back(tradew::Out(slot, d));
    }
    return n;
}
/* the world server's start: every undecided trade is ABORTED (restart); its games hear it at their WELCOME. The number. */
inline int StartAbort(Book* b)
{
    int n = 0;
    std::vector<tradew::Out> none;
    for (std::map<unsigned long long, Rec>::iterator it = b->recs.begin(); it != b->recs.end(); ++it)
        if (it->second.phase == kPhLocking && Abort(&it->second, tradew::kAbRestart, -1, tradew::kNoRow, &none)) ++n;
    return n;
}
/* build 3: the trade's lock ended for no room (the relay holds it again, both accepts cleared) */
inline bool NoRoom(const Rec& r) { return r.phase == kPhAborted && r.why == tradew::kAbNoRoom; }
/* the slot is in an undecided trade: it can neither ask nor be asked (the relay answers busy) */
inline bool Busy(const Book& b, int slot)
{
    for (std::map<unsigned long long, Rec>::const_iterator it = b.recs.begin(); it != b.recs.end(); ++it)
        if (it->second.phase == kPhLocking && SideOf(it->second, slot) >= 0) return true;
    return false;
}

/* ---- trade.txt: the header line, then tab-separated lines ----
   t <id> <phase> <why> <bySlot> <row>, then per side (0 then 1): <slot> <prof> <serial> <cats> <locked> <transfer> <booked> <inbox> <applied> <objects>
   o <id> <side> <index> <hex of the object's bytes>      (one per object, in order)
   A line that does not read is refused and counted; a trade whose objects are not all on file is kept with bodiesShort (its COMMIT is
   held, never sent short - a person reads the log). A header of another version: the whole file is refused (not rewritten). */
inline std::string U64(unsigned long long v) { std::string s; do { s.insert(s.begin(), (char)('0' + (int)(v % 10ULL))); v /= 10ULL; } while (v != 0); return s; }
inline std::string I64(long long v) { return v < 0 ? "-" + U64((unsigned long long)(-(v + 1)) + 1ULL) : U64((unsigned long long)v); }
inline bool U64Of(const std::string& s, unsigned long long* v)
{
    if (s.empty() || s.size() > 20) return false;
    unsigned long long x = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        const unsigned long long d = (unsigned long long)(s[i] - '0');
        if (x > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) return false;
        x = x * 10ULL + d;
    }
    *v = x; return true;
}
inline bool U32Of(const std::string& s, unsigned* v) { unsigned long long x = 0; if (!U64Of(s, &x) || x > 0xFFFFFFFFULL) return false; *v = (unsigned)x; return true; }
inline bool SlotOf(const std::string& s, int* v)
{
    if (s == "-1") { *v = -1; return true; }
    unsigned long long x = 0;
    if (!U64Of(s, &x) || x > 0x7FFFFFFFULL) return false;
    *v = (int)x; return true;
}
inline std::string Hex(const std::string& b)
{
    static const char d[] = "0123456789abcdef";
    std::string s; s.reserve(b.size() * 2);
    for (size_t i = 0; i < b.size(); ++i) { const unsigned char c = (unsigned char)b[i]; s += d[c >> 4]; s += d[c & 15]; }
    return s;
}
inline int HexDigit(char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; }
inline bool Unhex(const std::string& s, std::string* out)
{
    if (s.size() % 2 != 0) return false;
    std::string o; o.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2)
    {
        const int hi = HexDigit(s[i]), lo = HexDigit(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        o += (char)((hi << 4) | lo);
    }
    out->swap(o); return true;
}
inline std::string FileText(const Book& b)
{
    std::string t = std::string(kFileHeader) + "\n";
    for (std::map<unsigned long long, Rec>::const_iterator it = b.recs.begin(); it != b.recs.end(); ++it)
    {
        const Rec& r = it->second;
        if (PhaseName(r.phase) == 0 || r.phase == kPhOpen || !ProfOk(r.side[0].prof) || !ProfOk(r.side[1].prof)) continue;   /* never written: Begin refuses such */
        t += "t\t" + U64(r.id) + "\t" + PhaseName(r.phase) + "\t" + U64(r.why) + "\t" + I64(r.bySlot) + "\t" + U64(r.row);
        for (int x = 0; x < 2; ++x)
        {
            const Side& s = r.side[x];
            t += "\t" + I64(s.slot) + "\t" + s.prof + "\t" + U64(s.serial) + "\t" + U64(s.cats) + "\t" + U64((unsigned)s.locked) + "\t" + U64(s.transfer)
               + "\t" + U64((unsigned)s.booked) + "\t" + U64(s.inbox) + "\t" + U64((unsigned)s.applied) + "\t" + U64(s.bodies.size());
        }
        t += "\n";
        for (int x = 0; x < 2; ++x)
            for (size_t i = 0; i < r.side[x].bodies.size(); ++i) t += "o\t" + U64(r.id) + "\t" + U64((unsigned)x) + "\t" + U64(i) + "\t" + Hex(r.side[x].bodies[i]) + "\n";
    }
    return t;
}
enum LineKind { kLineOk = 0, kLineHeader = 1, kLineBad = 2, kLineOtherVersion = 3 };
inline std::vector<std::string> Fields(const std::string& line)
{
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) { const size_t t = line.find('\t', at); f.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at)); if (t == std::string::npos) break; at = t + 1; }
    return f;
}
inline int ParseLine(const std::string& raw, Book* b)
{
    std::string line = raw;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    if (line == kFileHeader) return kLineHeader;
    if (line.compare(0, std::string(kFileHeaderStem).size(), kFileHeaderStem) == 0) return kLineOtherVersion;
    const std::vector<std::string> f = Fields(line);
    unsigned long long id = 0;
    if (f[0] == "t" && f.size() == 26 && U64Of(f[1], &id))
    {
        Rec r; r.id = id; r.phase = -1;
        for (int p = kPhLocking; p <= kPhAborted; ++p) if (f[2] == PhaseName(p)) r.phase = p;
        if (r.phase < 0 || b->recs.count(id) != 0 || !U32Of(f[3], &r.why) || !SlotOf(f[4], &r.bySlot) || !U32Of(f[5], &r.row)) return kLineBad;
        if (r.phase == kPhAborted && (r.why == 0 || r.why > tradew::kAbortMax)) return kLineBad;
        for (int x = 0; x < 2; ++x)
        {
            const size_t k = 6 + (size_t)x * 10;
            Side& s = r.side[x];
            unsigned locked = 0, booked = 0, applied = 0;
            if (!SlotOf(f[k], &s.slot) || s.slot < 0 || !ProfOk(f[k + 1]) || !U32Of(f[k + 2], &s.serial) || !U32Of(f[k + 3], &s.cats) || s.cats > tradew::kMaxCats
                || !U32Of(f[k + 4], &locked) || locked > 1 || !U64Of(f[k + 5], &s.transfer) || !U32Of(f[k + 6], &booked) || booked > 1
                || !U64Of(f[k + 7], &s.inbox) || !U32Of(f[k + 8], &applied) || applied > 1 || !U32Of(f[k + 9], &s.want) || s.want > tradew::kMaxBodies)
                return kLineBad;
            s.prof = f[k + 1]; s.locked = (int)locked; s.booked = (int)booked; s.applied = (int)applied;
        }
        if (r.side[0].slot == r.side[1].slot || r.side[0].prof == r.side[1].prof) return kLineBad;
        b->recs[id] = r;
        return kLineOk;
    }
    unsigned x = 0, idx = 0;
    if (f[0] == "o" && f.size() == 5 && U64Of(f[1], &id) && U32Of(f[2], &x) && x <= 1 && U32Of(f[3], &idx))
    {
        std::map<unsigned long long, Rec>::iterator it = b->recs.find(id);
        if (it == b->recs.end()) return kLineBad;
        Side& s = it->second.side[x];
        std::string body;
        if (idx != s.bodies.size() || idx >= s.want || !Unhex(f[4], &body) || body.empty() || body.size() > tradew::kMaxBody) return kLineBad;
        s.bodies.push_back(body);
        return kLineOk;
    }
    return kLineBad;
}
/* the whole file: *bad = the lines refused, *shortTrades = trades whose objects are not all on file (bodiesShort). false = a header of
   another version (the file is not this build's: nothing taken, *b left empty). */
inline bool FileRead(const std::string& text, Book* b, long long* bad, long long* shortTrades)
{
    *b = Book(); *bad = 0; *shortTrades = 0;
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        const std::string line = text.substr(at, e == std::string::npos ? std::string::npos : e - at);
        at = e == std::string::npos ? text.size() : e + 1;
        if (line.empty() || line == "\r") continue;
        const int k = ParseLine(line, b);
        if (k == kLineOtherVersion) { *b = Book(); return false; }
        if (k == kLineBad) ++*bad;
    }
    for (std::map<unsigned long long, Rec>::iterator it = b->recs.begin(); it != b->recs.end(); ++it)
    {
        Rec& r = it->second;
        for (int x = 0; x < 2; ++x) if (r.side[x].bodies.size() != r.side[x].want) r.bodiesShort = 1;
        if (r.bodiesShort) ++*shortTrades;
    }
    return true;
}

/* ---- words for the log (slots, ids, numbers only) ---- */
inline std::string RecText(const Rec& r)
{
    std::string s = "trade " + U64(r.id) + " " + (PhaseName(r.phase) ? PhaseName(r.phase) : "?");
    if (r.phase == kPhAborted) s += std::string(" (") + tradew::AbortName(r.why) + (r.bySlot >= 0 ? ", s" + I64(r.bySlot) : std::string()) + (r.row != tradew::kNoRow ? ", row " + U64(r.row) : std::string()) + ")";
    for (int x = 0; x < 2; ++x)
    {
        const Side& d = r.side[x];
        s += " | s" + I64(d.slot) + " serial " + U64(d.serial) + " cats " + U64(d.cats) + (d.locked ? " locked " + U64(d.bodies.size()) + " object(s)" : std::string(" not locked"))
           + (d.cats > 0 && d.booked ? " money inbox " + U64(d.inbox) : std::string()) + (d.applied ? " APPLIED" : "");
    }
    return s + (r.bodiesShort ? " [objects not all on file: COMMIT held]" : "");
}

}   // namespace tradeb
