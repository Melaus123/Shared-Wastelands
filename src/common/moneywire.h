// moneywire.h - T-810: PLAYERS GIVE EACH OTHER MONEY (owner decisions 858 and 861). Pure: no disk, no clock, no engine; the
// game plugin (money.cpp, store.cpp, playerstab.cpp), the world server (store_main.cpp) and the offline suite compile it.
//
// THE ROAD, every step on disk before the next:
//   1. The sender's SEND takes the money with the game's own take call (refused when short) and writes the new balance AND an
//      outbox row {transfer number, receiver slot, amount} in ONE own record (pp.money, the record format below). Only when that
//      write is on disk is MONEY_SEND sent; every outbox row not yet acknowledged is sent again on every new world-server link,
//      and a row leaves the outbox only on MONEY_SENT_OK (or, refunded, on a final MONEY_REFUSE).
//   2. The world server records each transfer by (sender, transfer number) in money.txt (written through to the disk) and puts
//      it in the receiver's inbox under the receiver's next inbox number; a transfer number it has seen is acknowledged and not
//      recorded twice. The inbox goes to the receiver's game at its WELCOME and whenever a row lands while it is connected.
//   3. The receiver adds the money once its own records restore has run, writes the balance together with "the highest inbox
//      number applied" in ONE own record, and only when that write is on disk sends MONEY_APPLIED; an inbox number at or below
//      the applied one is not added again. The world server drops inbox rows only up to the number MONEY_APPLIED names.
// Transfer and inbox numbers are (seconds since 1970 << 16) or the next number after the last one, whichever is larger
// (NextNumber): they keep rising even when the file or record that held the last one is lost.
// THE LOAD: the ledger always comes from the same write as the balance (LedgerFrom) - the pp.money record the restore laid the
// balance from, or, when the balance is the save's, the save's own ledger (multiplayer.ledger, SaveLedgerText), taken at the save
// request while no money can move (the game moves none from a save's request until it lands); neither there: the newest pp.money.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <sstream>
#include <cstring>

namespace moneyw {

/* ---- message numbers and the world-server protocol (store_main.cpp and store.cpp check these at compile time) ---- */
const unsigned kProtocol = 108;       /* the world-server protocol that carries the five (99 when T-810 added them; 100 since T-811's SHOW 78; 101 since TOWNS SHOW KEYS) */
const unsigned kMsgSend = 73;         /* game -> server: {u64 transfer, u32 receiver slot, i32 amount} */
const unsigned kMsgSentOk = 74;       /* server -> sender: {u64 transfer} - recorded (or already recorded) */
const unsigned kMsgDeliver = 75;      /* server -> receiver: {u64 inbox number, u32 sender slot (0xFFFFFFFF unknown), i32 amount} */
const unsigned kMsgApplied = 76;      /* receiver -> server: {u64 the highest inbox number applied and on disk} */
const unsigned kMsgRefuse = 77;       /* server -> sender: {u64 transfer, u32 reason} - not recorded */

enum Refusal { kRefuseNone = 0, kRefuseNoPlayer = 1, kRefuseSelf = 2, kRefuseAmount = 3, kRefuseNotSaved = 4 };
/* a final refusal gives the money back to the sender; not-saved (money.txt could not be written) keeps the row, which is sent
   again at the next link */
inline bool RefuseFinal(unsigned why) { return why != (unsigned)kRefuseNotSaved; }
inline const char* RefuseName(unsigned why)
{
    switch (why)
    {
    case kRefuseNoPlayer: return "no-such-player";
    case kRefuseSelf:     return "self";
    case kRefuseAmount:   return "amount";
    case kRefuseNotSaved: return "not-saved";
    default:              return "?";
    }
}

/* ---- numbers ---- */
inline unsigned long long NextNumber(unsigned long long stored, unsigned long long nowSec)
{
    const unsigned long long t = (nowSec & 0x0000FFFFFFFFFFFFULL) << 16;
    return stored > t ? stored : t;
}

/* ---- the wire: little-endian, fixed sizes ---- */
inline void PutU32(std::vector<char>* v, unsigned x) { for (int i = 0; i < 4; ++i) v->push_back((char)((x >> (8 * i)) & 0xFFu)); }
inline void PutU64(std::vector<char>* v, unsigned long long x) { PutU32(v, (unsigned)(x & 0xFFFFFFFFULL)); PutU32(v, (unsigned)(x >> 32)); }
struct Cur { const unsigned char* p; size_t n; size_t at; bool bad; };
inline Cur CurOf(const void* p, size_t n) { Cur c; c.p = (const unsigned char*)p; c.n = p ? n : 0; c.at = 0; c.bad = false; return c; }
inline unsigned GetU32(Cur* c)
{
    if (c->bad || c->at > c->n || c->n - c->at < 4) { c->bad = true; return 0; }
    unsigned x = 0;
    for (int i = 0; i < 4; ++i) x |= (unsigned)c->p[c->at + (size_t)i] << (8 * i);
    c->at += 4;
    return x;
}
inline unsigned long long GetU64(Cur* c) { const unsigned lo = GetU32(c); const unsigned hi = GetU32(c); return ((unsigned long long)hi << 32) | lo; }
inline bool CurEnd(const Cur& c) { return !c.bad && c.at == c.n; }

struct Send { unsigned long long transfer; unsigned toSlot; int amount; Send() : transfer(0), toSlot(0), amount(0) {} };
inline void EncodeSend(std::vector<char>* b, const Send& s) { b->clear(); PutU64(b, s.transfer); PutU32(b, s.toSlot); PutU32(b, (unsigned)s.amount); }
inline bool DecodeSend(const void* p, size_t n, Send* s)
{
    Cur c = CurOf(p, n); Send o;
    o.transfer = GetU64(&c); o.toSlot = GetU32(&c); o.amount = (int)GetU32(&c);
    if (!CurEnd(c)) return false;
    *s = o; return true;
}
inline void EncodeU64(std::vector<char>* b, unsigned long long x) { b->clear(); PutU64(b, x); }   /* SENT_OK {transfer}, APPLIED {upTo} */
inline bool DecodeU64(const void* p, size_t n, unsigned long long* x)
{
    Cur c = CurOf(p, n);
    const unsigned long long v = GetU64(&c);
    if (!CurEnd(c)) return false;
    *x = v; return true;
}
struct Deliver { unsigned long long inbox; unsigned fromSlot; int amount; Deliver() : inbox(0), fromSlot(0xFFFFFFFFu), amount(0) {} };
inline void EncodeDeliver(std::vector<char>* b, const Deliver& d) { b->clear(); PutU64(b, d.inbox); PutU32(b, d.fromSlot); PutU32(b, (unsigned)d.amount); }
inline bool DecodeDeliver(const void* p, size_t n, Deliver* d)
{
    Cur c = CurOf(p, n); Deliver o;
    o.inbox = GetU64(&c); o.fromSlot = GetU32(&c); o.amount = (int)GetU32(&c);
    if (!CurEnd(c) || o.amount <= 0 || o.inbox == 0) return false;
    *d = o; return true;
}
struct RefuseMsg { unsigned long long transfer; unsigned why; RefuseMsg() : transfer(0), why(0) {} };
inline void EncodeRefuse(std::vector<char>* b, const RefuseMsg& r) { b->clear(); PutU64(b, r.transfer); PutU32(b, r.why); }
inline bool DecodeRefuse(const void* p, size_t n, RefuseMsg* r)
{
    Cur c = CurOf(p, n); RefuseMsg o;
    o.transfer = GetU64(&c); o.why = GetU32(&c);
    if (!CurEnd(c)) return false;
    *r = o; return true;
}

/* ---- the amount the player types ---- */
enum Amount { kAmtOk = 0, kAmtEmpty = 1, kAmtNotDigits = 2, kAmtZero = 3, kAmtTooMuch = 4 };
const size_t kAmountDigitsMax = 10;   /* the box takes at most this many digits */
/* digits only, at most kAmountDigitsMax of them: what the box keeps of what was typed */
inline std::string DigitsOnly(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < kAmountDigitsMax; ++i) if (s[i] >= '0' && s[i] <= '9') o += s[i];
    return o;
}
/* kAmtOk with *out the amount (1..have); no limit beyond what the player has (owner 861) */
inline int ParseAmount(const std::string& s, int have, int* out)
{
    *out = 0;
    if (s.empty()) return kAmtEmpty;
    long long v = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return kAmtNotDigits;
        if (v <= 2147483647LL) v = v * 10 + (s[i] - '0');
    }
    if (v == 0) return kAmtZero;
    if (v > 2147483647LL || v > (long long)(have < 0 ? 0 : have)) return kAmtTooMuch;
    *out = (int)v;
    return kAmtOk;
}

/* ---- the words (owner 861: the box and the two chat lines exactly as approved; the refusal hints and the line for money that
   comes back are the builder's proposals) ---- */
/* money as Kenshi's money bar writes it: c.12,500 (the same as fallentab.h Money and panelstatus.h OptMoneyText) */
inline std::string Cats(long long v)
{
    std::ostringstream o; o << (v < 0 ? 0LL : v);
    const std::string d = o.str();
    std::string r;
    for (size_t i = 0; i < d.size(); ++i) { if (i > 0 && (d.size() - i) % 3 == 0) r += ','; r += d[i]; }
    return "c." + r;
}
inline const char* GiveWord()    { return "GIVE MONEY"; }   /* the PLAYERS tab's button and the box's title */
inline const char* CancelWord()  { return "CANCEL"; }
inline const char* SendWord()    { return "SEND"; }
inline const char* AmountWord()  { return "Amount:"; }
inline const char* DigitsHint()  { return "(digits only)"; }
inline std::string BoxText(const std::string& name, int have) { return "Send money to " + name + ". You have " + Cats(have) + "."; }
inline std::string SentLine(const std::string& name, int amount) { return "You sent " + name + " " + Cats(amount); }
inline std::string GotLine(const std::string& name, int amount)  { return name + " sent you " + Cats(amount); }
inline std::string BackLine(const std::string& name, int amount) { return "Your " + Cats(amount) + " to " + name + " came back."; }
/* the box's hint after a refused SEND: 0 = an amount problem (ParseAmount's code in `amt`), 1 = this game cannot send now */
inline const char* RefusedHint(int notNow, int amt)
{
    if (notNow) return "Can't send right now.";
    return amt == kAmtTooMuch ? "You don't have that much." : "Type an amount.";
}
/* every line the box's hint can hold (the box measures them all for its layout); 0 past the last */
inline const char* HintWords(int i)
{
    switch (i)
    {
    case 0:  return DigitsHint();
    case 1:  return RefusedHint(1, kAmtOk);
    case 2:  return RefusedHint(0, kAmtTooMuch);
    case 3:  return RefusedHint(0, kAmtEmpty);
    default: return 0;
    }
}
/* THE HINT'S PLACE in the GIVE MONEY box. W = the box's client width, the text starts at textLeft, the amount box ends at
   editRight, the amount row's top is rowY and its line height lh; need = the widest hint line's width with its inset. Right of
   the amount box when it fits there inside the box's 8 px margin; else on a row of its own under the amount box, from the text's
   left edge, and the box grows by that row (grow). */
struct HintSpot { int x, y, w, h, grow, below; HintSpot() : x(0), y(0), w(0), h(0), grow(0), below(0) {} };
inline HintSpot PlaceHint(int W, int textLeft, int editRight, int rowY, int lh, int need)
{
    HintSpot s; s.h = lh;
    s.x = editRight + 8; s.y = rowY; s.w = W - 8 - s.x;
    if (need <= s.w) return s;
    s.below = 1; s.x = textLeft; s.y = rowY + lh + 6; s.w = W - 8 - textLeft; s.grow = lh + 6;
    return s;
}

/* ---- the sender's and receiver's own ledger, carried in the pp.money record ---- */
struct OutRow { unsigned long long transfer; unsigned toSlot; int amount; OutRow() : transfer(0), toSlot(0), amount(0) {} };
struct Ledger
{
    unsigned long long nextTransfer;   /* the lowest transfer number the next SEND may take (NextNumber) */
    unsigned long long applied;        /* the highest inbox number whose money is in the balance beside it */
    std::vector<OutRow> out;           /* sent money the world server has not acknowledged yet, oldest first */
    Ledger() : nextTransfer(0), applied(0) {}
};
const size_t kOutMax = 64;   /* outbox rows at most; a SEND past it is refused (not now) */
inline bool LedgerAddOut(Ledger* l, unsigned long long nowSec, unsigned toSlot, int amount, OutRow* made)
{
    if (l->out.size() >= kOutMax || amount <= 0) return false;
    OutRow r; r.transfer = NextNumber(l->nextTransfer, nowSec); r.toSlot = toSlot; r.amount = amount;
    l->nextTransfer = r.transfer + 1;
    l->out.push_back(r);
    *made = r;
    return true;
}
inline bool LedgerDropOut(Ledger* l, unsigned long long transfer, OutRow* gone)
{
    for (size_t i = 0; i < l->out.size(); ++i)
        if (l->out[i].transfer == transfer) { if (gone) *gone = l->out[i]; l->out.erase(l->out.begin() + (long)i); return true; }
    return false;
}

/* THE pp.money RECORD. Version 1 (mmo3, ownrec.h EncodeMoney): the tag "om3m" (int32 length + bytes) and the int32 balance.
   Version 2 (T-810) adds, after the balance: int32 2, u64 nextTransfer, u64 applied, u32 rows, rows * {u64 transfer, u32
   receiver slot, i32 amount}. A version-1 record reads as an empty ledger. */
const int kRecVersion = 2;
inline void EncodeRec(int money, const Ledger& l, std::vector<char>* out)
{
    out->clear();
    PutU32(out, 4u); out->insert(out->end(), "om3m", "om3m" + 4);
    PutU32(out, (unsigned)money);
    PutU32(out, (unsigned)kRecVersion);
    PutU64(out, l.nextTransfer); PutU64(out, l.applied);
    PutU32(out, (unsigned)l.out.size());
    for (size_t i = 0; i < l.out.size(); ++i) { PutU64(out, l.out[i].transfer); PutU32(out, l.out[i].toSlot); PutU32(out, (unsigned)l.out[i].amount); }
}
/* *version: 1 (no ledger: *l empty) or 2. false = not a money record of either version. */
inline bool DecodeRec(const std::vector<char>& b, int* money, Ledger* l, int* version)
{
    Cur c = CurOf(b.empty() ? 0 : &b[0], b.size());
    if (GetU32(&c) != 4u || c.bad || c.n - c.at < 4 || std::memcmp(c.p + c.at, "om3m", 4) != 0) return false;
    c.at += 4;
    const int m = (int)GetU32(&c);
    if (c.bad) return false;
    Ledger o;
    if (CurEnd(c)) { *money = m; *l = o; *version = 1; return true; }
    if ((int)GetU32(&c) != kRecVersion) return false;
    o.nextTransfer = GetU64(&c); o.applied = GetU64(&c);
    const unsigned rows = GetU32(&c);
    if (c.bad || rows > (unsigned)kOutMax) return false;
    for (unsigned i = 0; i < rows; ++i)
    {
        OutRow r; r.transfer = GetU64(&c); r.toSlot = GetU32(&c); r.amount = (int)GetU32(&c);
        if (c.bad || r.amount <= 0) return false;
        o.out.push_back(r);
    }
    if (!CurEnd(c)) return false;
    *money = m; *l = o; *version = 2;
    return true;
}

/* ---- the receiver: what a delivery does ---- */
enum InKind { kInApply = 0, kInRepeat = 1 };
inline int InDecide(unsigned long long applied, unsigned long long inbox) { return inbox > applied ? kInApply : kInRepeat; }
/* The deliveries waiting (ascending inbox numbers) that are applied now: rows at or below `applied` are repeats (counted in
   *repeats, never added); the rest are taken in order while the balance stays within an int - the first that would not fit
   and everything after it wait. *sum = the money to add, *upTo = the new applied number (= applied when nothing is taken). */
inline size_t ApplyPlan(const std::vector<Deliver>& waiting, unsigned long long applied, int money, long long* sum, unsigned long long* upTo, size_t* repeats)
{
    *sum = 0; *upTo = applied; *repeats = 0;
    size_t taken = 0;
    for (size_t i = 0; i < waiting.size(); ++i)
    {
        if (InDecide(applied, waiting[i].inbox) == kInRepeat) { ++*repeats; continue; }
        if ((long long)money + *sum + (long long)waiting[i].amount > 2147483647LL) break;
        *sum += waiting[i].amount;
        if (waiting[i].inbox > *upTo) *upTo = waiting[i].inbox;
        ++taken;
    }
    return taken;
}

/* ---- what came on the world-server link ---- */
/* Deliveries waiting, SENT_OK acknowledgements and final refusals belong to the WELCOME - the profile - they came under. `edge` is
   the number the game raises at every WELCOME, link going down and profile pick: a new one drops them all (the world server sends
   the inbox again at the next WELCOME and answers a resent transfer again). true = the edge moved. */
struct Inbound
{
    long edge;
    std::map<unsigned long long, Deliver> waiting;   /* by inbox number */
    std::set<unsigned long long> acked;              /* SENT_OK transfers */
    std::map<unsigned long long, unsigned> refused;  /* final REFUSE: transfer -> reason */
    Inbound() : edge(-1) {}
};
inline bool InboundSync(Inbound* in, long edge)
{
    if (in->edge == edge) return false;
    in->edge = edge; in->waiting.clear(); in->acked.clear(); in->refused.clear();
    return true;
}

/* ---- the world server's ledger (money.txt) ---- */
struct InRow
{
    std::string to;                 /* the receiver's profile */
    unsigned long long inbox;       /* the receiver's inbox number */
    std::string from;               /* the sender's profile */
    int amount;
    unsigned long long transfer;    /* the sender's transfer number */
    InRow() : inbox(0), amount(0), transfer(0) {}
};
struct Book
{
    std::map<std::string, unsigned long long> next;      /* each receiver's next inbox number */
    std::map<std::string, unsigned long long> applied;   /* each receiver's highest MONEY_APPLIED */
    std::set<std::pair<std::string, unsigned long long> > seen;   /* every (sender, transfer number) recorded */
    std::vector<InRow> rows;                             /* the inboxes, in the order the rows were recorded */
};
enum SendKind { kSendNew = 0, kSendRepeat = 1, kSendNoPlayer = 2, kSendSelf = 3, kSendAmount = 4 };
inline unsigned SendRefusal(int k)
{
    return k == kSendNoPlayer ? (unsigned)kRefuseNoPlayer : k == kSendSelf ? (unsigned)kRefuseSelf : k == kSendAmount ? (unsigned)kRefuseAmount : (unsigned)kRefuseNone;
}
/* A transfer from `from`: kSendNew (a row made, *made) / kSendRepeat (already recorded: acknowledged, nothing made) / a refusal.
   `to` = the receiver's profile ("" = the slot names nobody). */
inline int BookSend(Book* b, const std::string& from, unsigned long long transfer, const std::string& to, int amount, unsigned long long nowSec, InRow* made)
{
    if (b->seen.count(std::make_pair(from, transfer)) != 0) return kSendRepeat;
    if (to.empty()) return kSendNoPlayer;
    if (to == from) return kSendSelf;
    if (amount <= 0) return kSendAmount;
    unsigned long long base = b->next[to];
    const unsigned long long ap = b->applied.count(to) ? b->applied[to] : 0;
    if (base <= ap) base = ap + 1;
    for (size_t i = 0; i < b->rows.size(); ++i) if (b->rows[i].to == to && b->rows[i].inbox >= base) base = b->rows[i].inbox + 1;   /* never a number its inbox holds */
    InRow r; r.to = to; r.inbox = NextNumber(base, nowSec); r.from = from; r.amount = amount; r.transfer = transfer;
    b->next[to] = r.inbox + 1;
    b->seen.insert(std::make_pair(from, transfer));
    b->rows.push_back(r);
    *made = r;
    return kSendNew;
}
/* MONEY_APPLIED from `to`: its rows up to `upTo` leave the inbox; the number dropped */
inline size_t BookApplied(Book* b, const std::string& to, unsigned long long upTo)
{
    unsigned long long& a = b->applied[to];
    if (upTo > a) a = upTo;
    size_t dropped = 0;
    for (size_t i = b->rows.size(); i > 0; --i)
        if (b->rows[i - 1].to == to && b->rows[i - 1].inbox <= upTo) { b->rows.erase(b->rows.begin() + (long)(i - 1)); ++dropped; }
    return dropped;
}
/* a receiver's inbox, ascending */
inline std::vector<InRow> BookRowsFor(const Book& b, const std::string& to)
{
    std::vector<InRow> o;
    for (size_t i = 0; i < b.rows.size(); ++i) if (b.rows[i].to == to) o.push_back(b.rows[i]);
    for (size_t i = 1; i < o.size(); ++i)
        for (size_t j = i; j > 0 && o[j - 1].inbox > o[j].inbox; --j) { InRow t = o[j - 1]; o[j - 1] = o[j]; o[j] = t; }
    return o;
}

/* money.txt: a header line, then tab-separated lines - n <receiver> <next>, a <receiver> <applied>, s <sender> <transfer>,
   r <receiver> <inbox> <sender> <amount> <transfer>. A profile key with a control byte is never written (KeyOk). */
const char* const kFileHeader = "SWMONEY 1";
inline bool KeyOk(const std::string& k)
{
    if (k.empty() || k.size() > 512) return false;
    for (size_t i = 0; i < k.size(); ++i) if ((unsigned char)k[i] < 0x20 || k[i] == 0x7F) return false;
    return true;
}
inline std::string U64Text(unsigned long long v) { std::ostringstream o; o << v; return o.str(); }
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
inline std::string BookFile(const Book& b)
{
    std::string t = std::string(kFileHeader) + "\n";
    for (std::map<std::string, unsigned long long>::const_iterator it = b.next.begin(); it != b.next.end(); ++it)
        if (KeyOk(it->first)) t += "n\t" + it->first + "\t" + U64Text(it->second) + "\n";
    for (std::map<std::string, unsigned long long>::const_iterator it = b.applied.begin(); it != b.applied.end(); ++it)
        if (KeyOk(it->first)) t += "a\t" + it->first + "\t" + U64Text(it->second) + "\n";
    for (std::set<std::pair<std::string, unsigned long long> >::const_iterator it = b.seen.begin(); it != b.seen.end(); ++it)
        if (KeyOk(it->first)) t += "s\t" + it->first + "\t" + U64Text(it->second) + "\n";
    for (size_t i = 0; i < b.rows.size(); ++i)
    {
        const InRow& r = b.rows[i];
        if (KeyOk(r.to) && KeyOk(r.from)) t += "r\t" + r.to + "\t" + U64Text(r.inbox) + "\t" + r.from + "\t" + U64Text((unsigned long long)r.amount) + "\t" + U64Text(r.transfer) + "\n";
    }
    return t;
}
enum LineKind { kLineOk = 0, kLineHeader = 1, kLineBad = 2 };
inline int BookParseLine(const std::string& raw, Book* b)
{
    std::string line = raw;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    if (line == kFileHeader) return kLineHeader;
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) { const size_t t = line.find('\t', at); f.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at)); if (t == std::string::npos) break; at = t + 1; }
    unsigned long long x = 0, y = 0, z = 0;
    if ((f[0] == "n" || f[0] == "a" || f[0] == "s") && f.size() == 3 && KeyOk(f[1]) && U64Of(f[2], &x))
    {
        if (f[0] == "n") { if (x > b->next[f[1]]) b->next[f[1]] = x; }
        else if (f[0] == "a") { if (x > b->applied[f[1]]) b->applied[f[1]] = x; }
        else b->seen.insert(std::make_pair(f[1], x));
        return kLineOk;
    }
    if (f[0] == "r" && f.size() == 6 && KeyOk(f[1]) && KeyOk(f[3]) && U64Of(f[2], &x) && U64Of(f[4], &y) && U64Of(f[5], &z) && y > 0 && y <= 2147483647ULL && x > 0)
    {
        InRow r; r.to = f[1]; r.inbox = x; r.from = f[3]; r.amount = (int)y; r.transfer = z;
        b->rows.push_back(r);
        b->seen.insert(std::make_pair(r.from, r.transfer));
        if (r.inbox + 1 > b->next[r.to]) b->next[r.to] = r.inbox + 1;   /* a lost n line never lets a number be given twice */
        return kLineOk;
    }
    return kLineBad;
}

/* ---- THE LOAD'S LEDGER: from the same write as the balance ---- */
/* The balance restored from the pp.money record this load -> that record's ledger. The balance left as the save's -> the save's
   own ledger (multiplayer.ledger) when it is there and names this records store, world and profile. Neither -> the store's
   newest pp.money (a save made before the save's ledger existed, or whose ledger file did not land). */
/* T-810 re-check fold (finding 1): a save whose ledger file says "no ledger was taken at this save" (SaveLedgerNoneText, its reason
   below) is never paired with another write's ledger: unless this load's restore lays the balance from a record, no money moves that
   load and the money record is neither read for it nor written (kFromNone). One reason is the exception: "unreadable" - the world that
   save was made in would itself have paired its balance with the newest pp.money once that read, so that is the pairing the save's
   balance had. A save with no usable ledger file at all (made before the save ledger existed, or an earlier save's file left behind)
   keeps the newest record's, as before. */
enum NoneWhy { kNoneNo = 0, kNoneHeld = 1, kNoneUnreadable = 2, kNoneUndecided = 3, kNoneUnwritable = 4 };
enum LedgerFromKind { kFromRecord = 0, kFromSave = 1, kFromIndex = 2, kFromNone = 3, kFromHeld = 4 };
inline int LedgerFrom(bool restoredFromRecord, bool saveLedgerMatches, int saveNoneWhy = kNoneNo, bool held = false)
{
    if (held) return (int)kFromHeld;   /* T-810 fold 3 (F1): a held money record - no base at all (HeldEnds, below) */
    if (restoredFromRecord) return (int)kFromRecord;
    if (saveLedgerMatches) return (int)kFromSave;
    if (saveNoneWhy != kNoneNo && saveNoneWhy != kNoneUnreadable) return (int)kFromNone;
    return (int)kFromIndex;
}
/* T-810 fold 3 (finding F1): THE MONEY RECORD HELD after a restore refused for a passing reason (the player not there yet, a read fault,
   a claim of the previous load still busy). While held there is no ledger base (kFromHeld): nothing pairs the save's balance with the
   save's ledger or any other, the money record is never written, no money moves, and a save made meanwhile says "none (held)"
   (SaveLedgerPick). The record's bytes are kept and laid on a later restore tick; the hold ends when they are laid - or, for a
   claim-busy load whose record is read late, when that read finds the record is not to be laid at all (the final answer the load
   itself would have given: not newer than the save, absent, another world's, unusable). */
enum HeldEndKind { kHeldStays = 0, kHeldEndsLaid = 1, kHeldEndsNotNewer = 2 };
/* arm: the late read of a claim-busy load's record this tick (1 = its bytes in hand, 0 = final - not to be laid, -1 = not now or not
   asked); laid: the record's balance was laid this tick */
inline int HeldEnds(int arm, bool laid)
{
    if (laid) return (int)kHeldEndsLaid;
    if (arm == 0) return (int)kHeldEndsNotNewer;
    return (int)kHeldStays;
}
/* THE BALANCE LAID FROM A HELD RECORD: the record's balance plus whatever the purse moved in this world since the hold's first read
   (base) - the game's own trades meanwhile are neither lost nor doubled (base == live: the record's balance as it is). T-810 fold 4
   (finding M1): a result below zero is laid as 0 and the shortfall handed back in *shortfall (else 0) - the record is never held for ever
   and a purse is never negative. false = above a purse's range: never laid (nothing set; the record stays held). */
inline bool HeldLayValue(int recBalance, int base, int live, int* out, long long* shortfall)
{
    const long long v = (long long)recBalance + ((long long)live - (long long)base);
    if (v > 2147483647LL) return false;
    *shortfall = v < 0 ? -v : 0;
    *out = v < 0 ? 0 : (int)v; return true;
}
/* T-810 fold 3 (finding F2): a save whose ledger file says "none" for this store is loaded with its money record laid WHATEVER ITS AGE
   (the load rule's "all" for money): the record wins over the save's balance (owner 898: a player cannot go back to an older save), its
   ledger pairs with the balance it laid, and the next save takes a real ledger instead of inheriting "none" for ever. */
inline bool MoneyRecordAnyAge(int saveNoneWhy) { return saveNoneWhy != kNoneNo; }
/* T-810 fold 4 (finding M1): A SAVE MADE WHILE THE MONEY RECORD WAS HELD carries, in its "none (held)" ledger file, the hold's starting
   purse (base) and the held record's sequence number (seq), so the purse's own move during the hold survives that save's load: when the
   record laid at that load is the same record (same seq) the balance laid is the record's + (the save's balance - base); another record
   (a newer one: it already holds what followed) is laid alone. ok(): both known - a hold whose record or starting purse was not in hand
   writes no mark, and its save's load lays the record alone (as before). */
struct HeldMark { int base; unsigned long long seq; HeldMark() : base(-1), seq(0) {} bool ok() const { return base >= 0 && seq != 0; } };
enum HeldMarkUse { kMarkNone = 0, kMarkCarried = 1, kMarkNewer = 2 };
inline int HeldMarkApplies(const HeldMark& hm, unsigned long long recSeq)
{
    if (!hm.ok()) return (int)kMarkNone;
    return hm.seq == recSeq ? (int)kMarkCarried : (int)kMarkNewer;
}
/* multiplayer.ledger, beside a save: the header, then s <store>, w <world>, p <profile>, t <nextTransfer>, a <applied>, one per
   line in that order, then o <transfer> <receiver slot> <amount> per outbox row (tab-separated). A name with a control byte, or
   more rows than an outbox holds, is never written (""). */
const char* const kLedgerFileHeader = "SWLEDGER 1";
inline bool LedgerNameOk(const std::string& k)
{
    if (k.size() > 512) return false;
    for (size_t i = 0; i < k.size(); ++i) if ((unsigned char)k[i] < 0x20 || k[i] == 0x7F) return false;
    return true;
}
inline std::string SaveLedgerText(const std::string& store, const std::string& world, const std::string& prof, const Ledger& l)
{
    if (!LedgerNameOk(store) || !LedgerNameOk(world) || !LedgerNameOk(prof) || l.out.size() > kOutMax) return std::string();
    std::string t = std::string(kLedgerFileHeader) + "\n";
    t += "s\t" + store + "\n" + "w\t" + world + "\n" + "p\t" + prof + "\n";
    t += "t\t" + U64Text(l.nextTransfer) + "\n" + "a\t" + U64Text(l.applied) + "\n";
    for (size_t i = 0; i < l.out.size(); ++i)
    {
        if (l.out[i].amount <= 0) return std::string();
        t += "o\t" + U64Text(l.out[i].transfer) + "\t" + U64Text((unsigned long long)l.out[i].toSlot) + "\t" + U64Text((unsigned long long)l.out[i].amount) + "\n";
    }
    return t;
}
/* false = not a whole ledger file of this version (nothing set) */
inline bool SaveLedgerParse(const std::string& text, std::string* store, std::string* world, std::string* prof, Ledger* l)
{
    std::vector<std::string> ln;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t e = text.find('\n', at);
        if (e == std::string::npos) return false;   /* the last line ends with a line end: a cut file is refused */
        std::string s = text.substr(at, e - at);
        if (!s.empty() && s[s.size() - 1] == '\r') s.erase(s.size() - 1);
        ln.push_back(s);
        at = e + 1;
    }
    if (ln.size() < 6 || ln[0] != kLedgerFileHeader) return false;
    const char tags[5] = { 's', 'w', 'p', 't', 'a' };
    std::string names[3]; Ledger o;
    for (int i = 0; i < 5; ++i)
    {
        const std::string& s = ln[1 + (size_t)i];
        if (s.size() < 2 || s[0] != tags[i] || s[1] != '\t') return false;
        const std::string v = s.substr(2);
        if (i < 3) { if (!LedgerNameOk(v)) return false; names[i] = v; continue; }
        unsigned long long x = 0;
        if (!U64Of(v, &x)) return false;
        if (i == 3) o.nextTransfer = x; else o.applied = x;
    }
    for (size_t i = 6; i < ln.size(); ++i)
    {
        std::vector<std::string> f;
        size_t a = 0;
        for (;;) { const size_t tb = ln[i].find('\t', a); f.push_back(ln[i].substr(a, tb == std::string::npos ? std::string::npos : tb - a)); if (tb == std::string::npos) break; a = tb + 1; }
        unsigned long long tr = 0, sl = 0, am = 0;
        if (f.size() != 4 || f[0] != "o" || !U64Of(f[1], &tr) || !U64Of(f[2], &sl) || !U64Of(f[3], &am) || sl > 0xFFFFFFFFULL || am == 0 || am > 2147483647ULL)
            return false;
        if (o.out.size() >= kOutMax) return false;
        OutRow r; r.transfer = tr; r.toSlot = (unsigned)sl; r.amount = (int)am;
        o.out.push_back(r);
    }
    *store = names[0]; *world = names[1]; *prof = names[2]; *l = o;
    return true;
}

/* ---- T-810 re-check fold: THE SAVE'S LEDGER IS NEVER MISSING (finding 1), NEVER LATE (finding 2), NEVER AN EARLIER SAVE'S (finding 4) ---- */
/* "no ledger was taken at this save", beside a save: the header, then s <store>, w <world>, p <profile>, n <reason> (tab-separated).
   Names that cannot be written are left empty (the file then names no store, and reads as no ledger file at all). */
const char* const kLedgerNoneHeader = "SWLEDGER 1 NONE";
inline const char* NoneWhyName(int w)
{
    switch (w)
    {
    case kNoneHeld:       return "held";
    case kNoneUnreadable: return "unreadable";
    case kNoneUndecided:  return "undecided";
    case kNoneUnwritable: return "unwritable";
    default:              return 0;
    }
}
/* T-810 fold 4 (M1): a held save's file then b <hold's starting purse>, q <held record's seq> (HeldMark; only when ok()) */
inline std::string SaveLedgerNoneText(const std::string& store, const std::string& world, const std::string& prof, int why, const HeldMark* hm = 0)
{
    const char* w = NoneWhyName(why);
    if (w == 0) return std::string();
    const bool ok = LedgerNameOk(store) && LedgerNameOk(world) && LedgerNameOk(prof);
    std::string t = std::string(kLedgerNoneHeader) + "\n" + "s\t" + (ok ? store : std::string()) + "\n" + "w\t" + (ok ? world : std::string()) + "\n"
         + "p\t" + (ok ? prof : std::string()) + "\n" + "n\t" + w + "\n";
    if (why == kNoneHeld && hm != 0 && hm->ok()) t += "b\t" + U64Text((unsigned long long)hm->base) + "\n" + "q\t" + U64Text(hm->seq) + "\n";
    return t;
}
/* false = not a whole "none" file (nothing set). *hm (when asked): the held save's mark, or none (not ok) for a file without one */
inline bool SaveLedgerNoneParse(const std::string& text, std::string* store, std::string* world, std::string* prof, int* why, HeldMark* hm = 0)
{
    std::vector<std::string> ln;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t e = text.find('\n', at);
        if (e == std::string::npos) return false;   /* a cut file is refused */
        std::string s = text.substr(at, e - at);
        if (!s.empty() && s[s.size() - 1] == '\r') s.erase(s.size() - 1);
        ln.push_back(s);
        at = e + 1;
    }
    if ((ln.size() != 5 && ln.size() != 7) || ln[0] != kLedgerNoneHeader) return false;   /* T-810 fold 4: 7 = a held save's mark */
    const char tags[4] = { 's', 'w', 'p', 'n' };
    std::string v[4];
    for (int i = 0; i < 4; ++i)
    {
        const std::string& s = ln[1 + (size_t)i];
        if (s.size() < 2 || s[0] != tags[i] || s[1] != '\t') return false;
        v[i] = s.substr(2);
        if (!LedgerNameOk(v[i])) return false;
    }
    int w = kNoneNo;
    for (int k = kNoneHeld; k <= kNoneUnwritable; ++k) if (v[3] == NoneWhyName(k)) w = k;
    if (w == kNoneNo) return false;
    HeldMark h;
    if (ln.size() == 7)
    {   /* T-810 fold 4 (M1): only a held save carries a mark, whole (b then q), a starting purse in a purse's range, a record seq */
        unsigned long long b = 0, q = 0;
        if (w != kNoneHeld || ln[5].size() < 2 || ln[5][0] != 'b' || ln[5][1] != '\t' || ln[6].size() < 2 || ln[6][0] != 'q' || ln[6][1] != '\t'
            || !U64Of(ln[5].substr(2), &b) || !U64Of(ln[6].substr(2), &q) || b > 2147483647ULL || q == 0)
            return false;
        h.base = (int)b; h.seq = q;
    }
    *store = v[0]; *world = v[1]; *prof = v[2]; *why = w;
    if (hm != 0) *hm = h;
    return true;
}
/* WHAT A SAVE REQUEST'S LEDGER FILE HOLDS. loaded: this world's ledger is in memory - it is the one every pp.money write pairs with the
   balance from then on (kPickLive). Not loaded yet (no money has moved in this world - every move needs it): the ledger the world's
   balance came with, in LedgerFrom's order - the pp.money record this load's restore laid the balance from (kPickRecord), the loaded
   save's own ledger (kPickSave); a world that itself loaded a "none" save keeps that save's reason; the money record held after a
   refused restore has no ledger that pairs (kNoneHeld); else the store's own answer, base: 1 = read now from the newest pp.money
   (kPickLive), 0 = that record cannot be read (kNoneUnreadable), -1 = not yet (the records writer busy). restorePending: this load's
   restore has not run - its record may still lay the balance. Not decided yet -> kPickWait, asked again every frame while the save runs
   (no money moves meanwhile) until it lands (landed), then kNoneUndecided. */
enum SavePick { kPickLive = 0, kPickRecord = 1, kPickSave = 2, kPickWait = 3, kPickNone = 4 };
inline int SaveLedgerPick(bool loaded, bool restorePending, bool restored, bool saveMatches, int worldNoneWhy, bool held, int base, bool landed, int* noneWhy)
{
    *noneWhy = kNoneNo;
    if (loaded) return kPickLive;
    if (!restorePending)
    {
        if (restored) return kPickRecord;
        if (held) { *noneWhy = kNoneHeld; return kPickNone; }   /* T-810 fold 3 (F1): before the save's own ledger - a held world's balance is older than its record */
        if (saveMatches) return kPickSave;
        if (worldNoneWhy != kNoneNo && worldNoneWhy != kNoneUnreadable) { *noneWhy = worldNoneWhy; return kPickNone; }
        if (base == 1) return kPickLive;
        if (base == 0) { *noneWhy = kNoneUnreadable; return kPickNone; }
    }
    if (!landed) return kPickWait;
    *noneWhy = kNoneUndecided; return kPickNone;
}
/* the store asked only when its answer can decide (SaveLedgerPick reads base only past every other step) */
inline bool SaveLedgerAskBase(bool loaded, bool restorePending, bool restored, bool saveMatches, int worldNoneWhy, bool held)
{
    return !loaded && !restorePending && !restored && !saveMatches && (worldNoneWhy == kNoneNo || worldNoneWhy == kNoneUnreadable) && !held;
}
/* WHICH LEDGER FILE IS THE LOADED SAVE'S. The times are file write times (FILETIME, 100 ns; 0 = no such file). The file beside the save
   is written only once the save has landed, so it is the save's when it was written at or after the save's quick.save; an older one is
   an earlier save's, left behind (finding 4: a save whose name could not be read removed nothing) and is not read. Else the copy in the
   records store, written at the save's request before anything was removed (finding 2: a game that ended between the save landing and
   its ledger being written), when it was written before that quick.save and at most kPendingFitFt before it - one copy per save
   destination, replaced at each request and removed once the ledger beside the save is written. No quick.save time: the file beside
   the save, as before. */
enum SaveLedgerUse { kUseNone = 0, kUseFolder = 1, kUsePending = 2 };
const unsigned long long kPendingFitFt = 30ULL * 60ULL * 10000000ULL;   /* 30 minutes: a save lands well inside it */
inline int SaveLedgerChoose(unsigned long long folderFt, unsigned long long pendingFt, unsigned long long saveFt)
{
    if (saveFt == 0) return folderFt != 0 ? (int)kUseFolder : (int)kUseNone;
    if (folderFt != 0 && folderFt >= saveFt) return (int)kUseFolder;
    if (pendingFt != 0 && pendingFt <= saveFt && saveFt - pendingFt <= kPendingFitFt) return (int)kUsePending;
    return (int)kUseNone;
}

/* T-810 re-check fold (finding 8; owner 878: "The hint must fit inside the box"): THE HINT'S PLACE WITH THE ROOM THE SCREEN LEAVES.
   roomH / roomW = how much taller / wider the box may still grow inside its area. Right of the amount box when it fits there (PlaceHint);
   else on its own row under the amount box when the box can grow by that row and the row holds the widest line; else the box grows
   WIDER by what the hint lacks right of the amount box (widen); else the box widened as far as it may and the hint placed in it again -
   fits = 0 only when even that leaves the hint's row taller or wider than the room (an area smaller than the box with its hint). */
struct HintFit { HintSpot spot; int widen, fits; HintFit() : widen(0), fits(1) {} };
inline HintFit PlaceHintFit(int W, int textLeft, int editRight, int rowY, int lh, int need, int roomH, int roomW)
{
    HintFit f;
    if (roomH < 0) roomH = 0;
    if (roomW < 0) roomW = 0;
    f.spot = PlaceHint(W, textLeft, editRight, rowY, lh, need);
    if (!f.spot.below) return f;
    if (f.spot.grow <= roomH && need <= f.spot.w) return f;
    const int lack = need - (W - 8 - (editRight + 8));
    if (lack > 0 && lack <= roomW) { f.widen = lack; f.spot = PlaceHint(W + lack, textLeft, editRight, rowY, lh, need); return f; }
    f.widen = roomW;
    f.spot = PlaceHint(W + roomW, textLeft, editRight, rowY, lh, need);
    if (f.spot.below && (f.spot.grow > roomH || need > f.spot.w)) f.fits = 0;
    return f;
}

}   /* namespace moneyw */
