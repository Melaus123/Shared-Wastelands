/* src/common/handrec.h - T-889 step 2 (owner decision 862, approved; owner decision 896 (b)): ONE DURABLE RECORD PER ITEM HAND-OVER.
 *
 * An item handed between two players (a TAKE from another player's character or box, a pickup off ground another game holds, a
 * GIVE onto another player's character or box) used to live only in memory on the game holding it while the hand-over was open,
 * and an edge (a save start, a load, a leave, the session end) handed an open escrow back to its owner (D6). A game that died in
 * the middle lost it (T1296, T-5) or the hand-back doubled it (T1289). Now each side writes ONE LINE per hand-over to its own
 * records folder (pp.handover), keyed by the TRANSFER ID (asker slot, request id, holder slot - request ids restart from a random
 * base each launch, so the id alone names nothing across launches):
 *
 *   the HOLDING side (role src - the owner of a TAKE, the giver of a GIVE) writes `held`, item bytes included, BEFORE the item can
 *   appear on the other game; the RECEIVING side (role dst - the taker, the owner a GIVE builds on) writes `in` with E, a records
 *   sequence number reserved at the placement, and the key of the squad record that holds it. A live answer settles the line
 *   (held -> closed / returned; the receiver's line is dropped on the holder's CLOSED). After either game came back, the holder asks
 *   (QUERY) and the receiver answers (VERDICT): 1 holds (its loaded own record is at or past E, or the save was asked for at or after
 *   E), 0 not placed (no `in` line with the records writer on and the file readable), 2 unknown. The first after-load answer is
 *   FROZEN into the receiver's line, so a later record cannot flip it.
 *
 * The line, in the outage journal's q1 shape (queuejournal.h; the ledger's pattern, ownrec.h LedgerLineFormat):
 *   q1 <TAB> 17 <TAB> p<other slot> <TAB> <request id> <TAB> <unix time> <TAB> <payload hex>
 * payload: x2|role|state|askerSlot|holderSlot|kind|where|section|x|y|squadKey|seqE|frozen|itemHex|bag|placed
 *   role src|dst; state held|in|closed|returned; kind take|give|ground|box; where c:S=<serial>;T=<type> (a character, by the engine's
 *   own handle serial and type - save-stable, the squad records' form; T-889 fold 3, F1) | c:? (a character whose id could not be
 *   read when the line was written: it never resolves - the line holds) | b:<box key> | g:<ground key>; section the inventory
 *   section (a ground line: the position "x,y,z"); x,y the square; squadKey the receiver's squad record
 *   key ("" none); seqE the receiver's reserved sequence (0 on a holder line); frozen - (none) | 0 | 1 | 2; itemHex the item's
 *   bytes (an ITEM_CONFIRM body, net::EncodeItemConfirmBody), "" none; bag the pack path the item sat in (T-889 fold 1, review 9):
 *   - (none) | <x>,<y>,<pack's section>. where / section / squadKey / the pack's section escape '%', '|' and control characters
 *   as %XX. Family 17 is neither an outage-queue family (0-2) nor the ledger's (16): each parser refuses the others.
 *   T-889 fold 1 (review 3): a HOLDER line carries E_h too - a sequence reserved when it was written, right after the lift - and the
 *   key of the holder's squad record (a character); after a reload they tell whether the holder's own loaded state still has the item.
 *   T-889 fold 3 (re-check 2 of fold 2): placed - (none: a holder line, or a receiver's placement not written down yet) | <pre>,<got>:
 *   the receiver's placement as it really ended - x,y then name the square it ended on (an add may merge onto a stack or move to the
 *   first free square), got the stack there after it, pre what that square held before (0 empty). A line of an earlier shape (x1, a
 *   u:<uid> where - a uid is minted fresh at every load and names nothing after one) does not parse: it is kept on file and held, never
 *   settled.
 *
 * Pure: no engine, no file, no network. Tested offline (src/coop-test, t889_*).
 */
#pragma once
/* T-889 fold 1 (review 2026-10-17): the mark rule in the verdict, the holder's own state after a reload (E_h), the pack path, returned
   lines kept until acknowledged (ACK), CLOSED 3 / 4, the parked give / take, the settle retry. */

#include <string>
#include <vector>
#include <cstddef>

namespace hovrec {

const int          kHandFamily   = 17;
const unsigned int kHandCap      = 256;    /* lines the file keeps; only SETTLED lines (closed / returned) are dropped to fit */
inline std::string HandFile() { return "pp.handover"; }
const unsigned int kGameProtocol = 180;    /* the game-to-game protocol of this build (net/session.cpp kProtocolVersion asserts it; 180: every
                                              ITEM_REQUEST ends with the 'ASK1' trailer, coopbrec::AskTail) */

const unsigned int kMsgHandover  = 79;     /* MSG_ITEM_HANDOVER (net/transport.h, liveenvelope.h kInnerItemHandover assert it) */

const int kRoleSrc = 0, kRoleDst = 1;
const int kStHeld = 0, kStIn = 1, kStClosed = 2, kStReturned = 3;
const int kKindTake = 0, kKindGive = 1, kKindGround = 2, kKindBox = 3;
const int kVNone = -1, kVNot = 0, kVHolds = 1, kVUnknown = 2;

struct HandLine
{
    int                role;
    int                state;
    int                kind;
    int                askerSlot, holderSlot;   /* with reqId: the transfer id */
    int                otherSlot;               /* the other player of this line (the one QUERY / VERDICT / CLOSED go to) */
    unsigned int       reqId;
    long long          atUnix;
    std::string        where;                   /* u:<uid> | b:<box key> | g:<ground key> */
    std::string        section;
    int                x, y;
    std::string        squadKey;
    unsigned long long seqE;
    int                frozen;                  /* kVNone, else the frozen verdict */
    std::string        item;                    /* raw bytes */
    int                bagHas;                  /* T-889 fold 1 (review 9 / 14): the item sat inside a pack - the pack's section and square */
    std::string        bagSection;
    int                bagX, bagY;
    int                plHas, plPre, plGot;     /* T-889 fold 3 (F2): a receiver's placement as it really ended - x,y its square, plGot the
                                                   stack there after it, plPre what that square held before (0 empty); plHas 0 = not known */
    HandLine() : role(kRoleSrc), state(kStHeld), kind(kKindTake), askerSlot(-1), holderSlot(-1), otherSlot(-1), reqId(0), atUnix(0),
                 x(0), y(0), seqE(0), frozen(kVNone), bagHas(0), bagX(0), bagY(0), plHas(0), plPre(0), plGot(0) {}
};

/* ---- small text helpers ---- */
inline void HrPutU(std::string* s, unsigned long long v)
{
    char b[24];
    int n = 0;
    do { b[n++] = (char)('0' + (int)(v % 10ULL)); v /= 10ULL; } while (v != 0ULL && n < 24);
    while (n > 0) *s += b[--n];
}
inline void HrPutI(std::string* s, long long v)
{
    if (v < 0) { *s += '-'; HrPutU(s, (unsigned long long)(-(v + 1)) + 1ULL); }
    else HrPutU(s, (unsigned long long)v);
}
/* decimal digits only, no leading zero (except "0" itself), at most `mx` */
inline int HrGetU(const std::string& t, unsigned long long mx, unsigned long long* out)
{
    if (t.empty() || t.size() > 20 || (t.size() > 1 && t[0] == '0')) return 0;
    unsigned long long v = 0ULL;
    for (std::string::size_type i = 0; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9') return 0;
        const unsigned long long d = (unsigned long long)(t[i] - '0');
        if (v > (mx - d) / 10ULL) return 0;
        v = v * 10ULL + d;
    }
    *out = v;
    return 1;
}
inline int HrGetInt(const std::string& t, int* out)
{
    if (t.empty()) return 0;
    const bool neg = (t[0] == '-');
    unsigned long long m = 0ULL;
    if (HrGetU(neg ? t.substr(1) : t, neg ? 2147483648ULL : 2147483647ULL, &m) == 0) return 0;
    if (neg && m == 0ULL) return 0;   /* "-0" is not a number this file writes */
    *out = neg ? (int)(-(long long)m) : (int)m;
    return 1;
}
inline char HrHexDigit(int v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }
inline int HrHexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline std::string HrToHex(const std::string& b)
{
    std::string s;
    for (std::string::size_type i = 0; i < b.size(); ++i)
    {
        const int v = (int)(unsigned char)b[i];
        s += HrHexDigit((v >> 4) & 0x0F);
        s += HrHexDigit(v & 0x0F);
    }
    return s;
}
inline int HrFromHex(const std::string& h, std::string* out)
{
    if ((h.size() % 2) != 0) return 0;
    std::string b;
    for (std::string::size_type i = 0; i + 1 < h.size(); i += 2)
    {
        const int hi = HrHexValue(h[i]), lo = HrHexValue(h[i + 1]);
        if (hi < 0 || lo < 0) return 0;
        b += (char)((hi << 4) | lo);
    }
    *out = b;
    return 1;
}
inline std::string HrEsc(const std::string& t)
{
    std::string s;
    for (std::string::size_type i = 0; i < t.size(); ++i)
    {
        const int v = (int)(unsigned char)t[i];
        if (t[i] == '%' || t[i] == '|' || v < 0x20 || v == 0x7F) { s += '%'; s += HrHexDigit((v >> 4) & 0x0F); s += HrHexDigit(v & 0x0F); }
        else s += t[i];
    }
    return s;
}
/* 0 for a cut escape, a bad hex digit, or a raw character HrEsc would have escaped */
inline int HrUnesc(const std::string& t, std::string* out)
{
    std::string s;
    for (std::string::size_type i = 0; i < t.size(); ++i)
    {
        const int v = (int)(unsigned char)t[i];
        if (t[i] == '|' || v < 0x20 || v == 0x7F) return 0;
        if (t[i] != '%') { s += t[i]; continue; }
        if (i + 2 >= t.size()) return 0;   /* a cut escape */
        const int hi = HrHexValue(t[i + 1]), lo = HrHexValue(t[i + 2]);
        if (hi < 0 || lo < 0) return 0;
        s += (char)((hi << 4) | lo);
        i += 2;
    }
    *out = s;
    return 1;
}
inline const char* RoleName(int r) { return r == kRoleSrc ? "src" : (r == kRoleDst ? "dst" : 0); }
inline const char* StateName(int s) { return s == kStHeld ? "held" : s == kStIn ? "in" : s == kStClosed ? "closed" : s == kStReturned ? "returned" : 0; }
inline const char* KindName(int k) { return k == kKindTake ? "take" : k == kKindGive ? "give" : k == kKindGround ? "ground" : k == kKindBox ? "box" : 0; }
inline int NameIndex(const std::string& t, const char* (*nameOf)(int), int n)
{
    for (int i = 0; i < n; ++i) { const char* s = nameOf(i); if (s != 0 && t == s) return i; }
    return -1;
}
/* a line's role and state agree: a holder's line is held / closed / returned, a receiver's is in */
inline int RoleStateOk(int role, int state)
{
    if (role == kRoleSrc) return (state == kStHeld || state == kStClosed || state == kStReturned) ? 1 : 0;
    if (role == kRoleDst) return (state == kStIn) ? 1 : 0;
    return 0;
}
/* T-889 fold 3 (F1): A CHARACTER IS NAMED BY ITS SAVE-STABLE ID - the engine's own handle serial and type, "c:S=<serial>;T=<type>" (the
   squad records' S=<serial>;T=<type>, ownrec.h HandId), never by a uid (minted fresh at every load and on a region reload). "c:?" = the
   id could not be read when the line was written: it never resolves, so every look answers "not loaded" and every rebuild waits. */
inline std::string CharWhere(int serial, int type)
{
    std::string s = "c:S=";
    HrPutI(&s, (long long)serial);
    s += ";T=";
    HrPutI(&s, (long long)type);
    return s;
}
inline std::string CharWhereUnknown() { return "c:?"; }
/* 1 = "c:S=<int>;T=<int>" (*serial, *type may be 0); 0 for anything else, "c:?" included */
inline int CharIdParse(const std::string& w, int* serial, int* type)
{
    if (w.size() < 9 || w.compare(0, 4, "c:S=") != 0) return 0;
    const std::string::size_type sc = w.find(";T=", 4);
    if (sc == std::string::npos) return 0;
    int s = 0, ty = 0;
    if (HrGetInt(w.substr(4, sc - 4), &s) == 0 || HrGetInt(w.substr(sc + 3), &ty) == 0) return 0;
    if (serial != 0) *serial = s;
    if (type != 0) *type = ty;
    return 1;
}
/* c: (a character id, or c:?), b: (a box key), g: (a ground key). An earlier u:<uid> is refused: such a line names nothing after a load. */
inline int WhereOk(const std::string& w)
{
    if (w.size() < 3 || w[1] != ':') return 0;
    if (w[0] == 'b' || w[0] == 'g') return 1;
    if (w[0] == 'c') return (w == CharWhereUnknown() || CharIdParse(w, 0, 0) != 0) ? 1 : 0;
    return 0;
}
/* F2: a placement written down - only on a receiver's line, a square that held 0 or more before and more after */
inline int PlacedOk(int role, int plHas, int pre, int got)
{
    if (plHas == 0) return 1;
    return (role == kRoleDst && pre >= 0 && got > pre) ? 1 : 0;
}
inline int IsOpen(int state) { return (state == kStHeld || state == kStIn) ? 1 : 0; }

/* 0 (and *out untouched) for a role / state / kind out of range or in disagreement, a slot below 0, a negative time, a bad frozen
   value or a `where` that is not u: / b: / g:. The newline IS included. */
inline int HandLineFormat(const HandLine& l, std::string* out)
{
    if (out == 0 || RoleStateOk(l.role, l.state) == 0 || KindName(l.kind) == 0 || WhereOk(l.where) == 0) return 0;
    if (l.askerSlot < 0 || l.holderSlot < 0 || l.otherSlot < 0 || l.atUnix < 0 || l.frozen < kVNone || l.frozen > kVUnknown) return 0;
    if (l.bagHas != 0 && l.bagSection.empty()) return 0;   /* a pack path names the pack's section */
    if (PlacedOk(l.role, l.plHas, l.plPre, l.plGot) == 0) return 0;
    std::string p = "x2|";
    p += RoleName(l.role); p += '|';
    p += StateName(l.state); p += '|';
    HrPutU(&p, (unsigned long long)l.askerSlot); p += '|';
    HrPutU(&p, (unsigned long long)l.holderSlot); p += '|';
    p += KindName(l.kind); p += '|';
    p += HrEsc(l.where); p += '|';
    p += HrEsc(l.section); p += '|';
    HrPutI(&p, (long long)l.x); p += '|';
    HrPutI(&p, (long long)l.y); p += '|';
    p += HrEsc(l.squadKey); p += '|';
    HrPutU(&p, l.seqE); p += '|';
    if (l.frozen == kVNone) p += '-'; else HrPutU(&p, (unsigned long long)l.frozen);
    p += '|';
    p += HrToHex(l.item);
    p += '|';
    if (l.bagHas == 0) p += '-';
    else { HrPutI(&p, (long long)l.bagX); p += ','; HrPutI(&p, (long long)l.bagY); p += ','; p += HrEsc(l.bagSection); }
    p += '|';
    if (l.plHas == 0) p += '-';
    else { HrPutI(&p, (long long)l.plPre); p += ','; HrPutI(&p, (long long)l.plGot); }
    std::string s = "q1\t";
    HrPutU(&s, (unsigned long long)kHandFamily);
    s += "\tp";
    HrPutU(&s, (unsigned long long)l.otherSlot);
    s += '\t';
    HrPutU(&s, (unsigned long long)l.reqId);
    s += '\t';
    HrPutU(&s, (unsigned long long)l.atUnix);
    s += '\t';
    s += HrToHex(p);
    s += '\n';
    *out = s;
    return 1;
}

/* 0 on ANY malformed field (never half-read). A trailing CR / LF is stripped. */
inline int HandLineParse(const std::string& line, HandLine* out)
{
    if (out == 0) return 0;
    std::vector<std::string> f;
    std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type tb = line.find('\t', at);
        if (tb == std::string::npos) { f.push_back(line.substr(at)); break; }
        f.push_back(line.substr(at, tb - at));
        at = tb + 1;
    }
    if (f.size() != 6 || f[0] != "q1") return 0;
    std::string hx = f[5];
    while (!hx.empty() && (hx[hx.size() - 1] == '\r' || hx[hx.size() - 1] == '\n')) hx.erase(hx.size() - 1);
    unsigned long long fam = 0ULL, id = 0ULL, when = 0ULL, other = 0ULL;
    if (HrGetU(f[1], 1000ULL, &fam) == 0 || fam != (unsigned long long)kHandFamily) return 0;
    if (f[2].size() < 2 || f[2][0] != 'p' || HrGetU(f[2].substr(1), 2147483647ULL, &other) == 0) return 0;
    if (HrGetU(f[3], 0xFFFFFFFFULL, &id) == 0) return 0;
    if (HrGetU(f[4], 0x7FFFFFFFFFFFFFFFULL, &when) == 0) return 0;
    std::string p;
    if (hx.empty() || HrFromHex(hx, &p) == 0) return 0;
    std::vector<std::string> g;
    at = 0;
    for (;;)
    {
        const std::string::size_type b = p.find('|', at);
        if (b == std::string::npos) { g.push_back(p.substr(at)); break; }
        g.push_back(p.substr(at, b - at));
        at = b + 1;
    }
    if (g.size() != 16 || g[0] != "x2") return 0;   /* T-889 fold 3: an earlier shape (x1, 15 fields) is never half-read */
    HandLine l;
    l.role = NameIndex(g[1], RoleName, 2);
    l.state = NameIndex(g[2], StateName, 4);
    if (l.role < 0 || l.state < 0 || RoleStateOk(l.role, l.state) == 0) return 0;
    unsigned long long a = 0ULL, h = 0ULL, e = 0ULL;
    if (HrGetU(g[3], 2147483647ULL, &a) == 0 || HrGetU(g[4], 2147483647ULL, &h) == 0) return 0;
    l.kind = NameIndex(g[5], KindName, 4);
    if (l.kind < 0) return 0;
    if (HrUnesc(g[6], &l.where) == 0 || WhereOk(l.where) == 0) return 0;
    if (HrUnesc(g[7], &l.section) == 0) return 0;
    if (HrGetInt(g[8], &l.x) == 0 || HrGetInt(g[9], &l.y) == 0) return 0;
    if (HrUnesc(g[10], &l.squadKey) == 0) return 0;
    if (HrGetU(g[11], 0xFFFFFFFFFFFFFFFFULL, &e) == 0) return 0;
    if (g[12] == "-") l.frozen = kVNone;
    else if (g[12] == "0") l.frozen = kVNot;
    else if (g[12] == "1") l.frozen = kVHolds;
    else if (g[12] == "2") l.frozen = kVUnknown;
    else return 0;
    if (HrFromHex(g[13], &l.item) == 0) return 0;
    if (g[14] != "-")
    {   /* <x>,<y>,<escaped section>: the section may hold commas (escaped text) - only the first two split */
        const std::string::size_type c1 = g[14].find(',');
        const std::string::size_type c2 = (c1 == std::string::npos) ? std::string::npos : g[14].find(',', c1 + 1);
        if (c2 == std::string::npos) return 0;
        if (HrGetInt(g[14].substr(0, c1), &l.bagX) == 0 || HrGetInt(g[14].substr(c1 + 1, c2 - c1 - 1), &l.bagY) == 0) return 0;
        if (HrUnesc(g[14].substr(c2 + 1), &l.bagSection) == 0 || l.bagSection.empty()) return 0;
        l.bagHas = 1;
    }
    if (g[15] != "-")
    {   /* T-889 fold 3 (F2): <pre>,<got> */
        const std::string::size_type c = g[15].find(',');
        if (c == std::string::npos) return 0;
        if (HrGetInt(g[15].substr(0, c), &l.plPre) == 0 || HrGetInt(g[15].substr(c + 1), &l.plGot) == 0) return 0;
        l.plHas = 1;
        if (PlacedOk(l.role, l.plHas, l.plPre, l.plGot) == 0) return 0;
    }
    l.askerSlot = (int)a; l.holderSlot = (int)h; l.otherSlot = (int)other;
    l.reqId = (unsigned int)id; l.atUnix = (long long)when; l.seqE = e;
    *out = l;
    return 1;
}

inline int SameTransfer(const HandLine& l, int asker, unsigned int reqId, int holder)
{
    return (l.askerSlot == asker && l.reqId == reqId && l.holderSlot == holder) ? 1 : 0;
}

/* The file's text -> every non-empty line, KEPT AS IT IS (a line this build cannot parse may be a later format); *bad counts those. */
inline void HandSplit(const std::string& text, std::vector<std::string>* all, int* bad)
{
    if (bad != 0) *bad = 0;
    std::string cur;
    for (std::string::size_type i = 0; i <= text.size(); ++i)
    {
        if (i < text.size() && text[i] != '\n') { cur += text[i]; continue; }
        while (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1);
        if (!cur.empty())
        {
            HandLine l;
            if (HandLineParse(cur, &l) == 0 && bad != 0) ++*bad;
            if (all != 0) all->push_back(cur);
        }
        cur.clear();
    }
}
/* the index of the line of that transfer and role, -1 none */
inline int HandFind(const std::vector<std::string>& lines, int asker, unsigned int reqId, int holder, int role, HandLine* out)
{
    for (size_t i = 0; i < lines.size(); ++i)
    {
        HandLine l;
        if (HandLineParse(lines[i], &l) == 0 || l.role != role || SameTransfer(l, asker, reqId, holder) == 0) continue;
        if (out != 0) *out = l;
        return (int)i;
    }
    return -1;
}
/* THE CAP: the oldest CLOSED lines go first; open lines, RETURNED lines (T-889 fold 1, review 10: kept until the receiver acknowledges
   their CLOSED 2 - a lost CLOSED 2 is asked about again) and lines this build cannot read are never dropped.
   1 = the file fits `cap`; 0 = it does not (only lines that are kept would be left over it). *dropped = lines removed. */
inline int CapDrops(int state) { return (state == kStClosed) ? 1 : 0; }
inline int HandFit(std::vector<std::string>* lines, unsigned int cap, int* dropped)
{
    if (dropped != 0) *dropped = 0;
    for (size_t i = 0; i < lines->size() && lines->size() > (size_t)cap; )
    {
        HandLine l;
        if (HandLineParse((*lines)[i], &l) != 0 && CapDrops(l.state) != 0)
        {
            lines->erase(lines->begin() + (std::vector<std::string>::difference_type)i);
            if (dropped != 0) ++*dropped;
            continue;
        }
        ++i;
    }
    return (lines->size() <= (size_t)cap) ? 1 : 0;
}
/* The line of the same transfer and role replaced, or appended; then the cap. 1 done; 0 the line would not format; -1 it does not
   fit (the vector is then as it was). */
inline int HandUpsert(std::vector<std::string>* lines, const HandLine& l, unsigned int cap)
{
    std::string s;
    if (HandLineFormat(l, &s) == 0) return 0;
    while (!s.empty() && (s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r')) s.erase(s.size() - 1);
    std::vector<std::string> v = *lines;
    const int at = HandFind(v, l.askerSlot, l.reqId, l.holderSlot, l.role, 0);
    if (at >= 0) v[(size_t)at] = s; else v.push_back(s);
    if (HandFit(&v, cap, 0) == 0) return -1;
    lines->swap(v);
    return 1;
}
/* 1 removed, 0 there was none */
inline int HandRemove(std::vector<std::string>* lines, int asker, unsigned int reqId, int holder, int role)
{
    const int at = HandFind(*lines, asker, reqId, holder, role, 0);
    if (at < 0) return 0;
    lines->erase(lines->begin() + (std::vector<std::string>::difference_type)at);
    return 1;
}
/* the largest seqE any line carries (the records sequence restarts from the index at open; a reserved E must stay below every later record) */
inline unsigned long long HandMaxSeq(const std::vector<std::string>& lines)
{
    unsigned long long m = 0ULL;
    for (size_t i = 0; i < lines.size(); ++i) { HandLine l; if (HandLineParse(lines[i], &l) != 0 && l.seqE > m) m = l.seqE; }
    return m;
}

/* ---- the decisions ---- */
/* AFTER A LOAD: does the receiver hold the item? Its loaded own squad record (appliedSeq: OwnOverlayAppliedSeq of the line's squad key)
   was written at or after E, or the save was ASKED FOR at or after E (markSeq: the save mark, g_ownSeq at the save request). E is
   reserved (++seq) at the placement, so a save asked for after the placement and before any record has mark == E: it counts. E 0 =
   none known: 0. The P14 fold 1 pattern (store.cpp OwnOverlayAppliedSeq; build.cpp BdMatsLedgerApply). */
inline int HoldsAfterLoad(unsigned long long appliedSeq, unsigned long long markSeq, unsigned long long E)
{
    if (E == 0ULL) return 0;
    return (appliedSeq >= E || markSeq >= E) ? 1 : 0;
}
/* THE RECEIVER'S ANSWER to a QUERY about one transfer.
   haveIn: an `in` line of that transfer; frozen: its frozen verdict (kVNone none); liveSinceLoad: the line was written in this process
   since its last load (a live placement); holdsAfterLoad: HoldsAfterLoad for it; squadKnown: the line names a squad record (a box has
   none - its record is not this game's); markRule: the last load ran under the MARK rule (T-889 fold 1, review 2: only then does a
   save mark below E mean the save predates the placement - a load by the quicksave-time rule or the all rule has no mark and skips
   records older than the save, so the save itself may hold the item: unknown); writerOn / fileReadable: the records writer is on and
   pp.handover could be read. */
inline int Verdict(int haveIn, int frozen, int liveSinceLoad, int holdsAfterLoad, int squadKnown, int markRule, int writerOn, int fileReadable)
{
    if (fileReadable == 0) return kVUnknown;
    if (haveIn != 0)
    {
        if (frozen == kVHolds || frozen == kVNot) return frozen;
        if (liveSinceLoad != 0) return kVHolds;
        if (squadKnown == 0) return kVUnknown;
        if (holdsAfterLoad != 0) return kVHolds;
        return (markRule != 0) ? kVNot : kVUnknown;
    }
    return (writerOn != 0) ? kVNot : kVUnknown;
}
/* T-889 fold 1 (review 7): an `in` line's unknown answer is settled by looking: live 1 = the item is at the line's place in the
   receiver's loaded character / box now (holds), 0 = that character / box is loaded and the item is not in it (not placed), -1 = not
   loaded here (still unknown - asked again). Any other answer, or no `in` line, is left as it is. */
inline int ResolveUnknownLive(int verdict, int haveIn, int live)
{
    if (verdict != kVUnknown || haveIn == 0) return verdict;
    if (live == 1) return kVHolds;
    if (live == 0) return kVNot;
    return kVUnknown;
}
/* T-889 fold 1 (review 6): a receiver with NO line because its records writer is off answers from memory: placed live in this process
   since its last load -> holds; otherwise it truly cannot know -> unknown. */
inline int VerdictNoLines(int placedLiveThisLoad) { return (placedLiveThisLoad != 0) ? kVHolds : kVUnknown; }
/* The first after-load look at an `in` line FREEZES a 0 / 1 into it: a record written later cannot flip it. A live answer (the line
   written since the last load) is not frozen - it is the placement itself. */
inline int VerdictFreezes(int haveIn, int frozen, int liveSinceLoad, int verdict)
{
    return (haveIn != 0 && frozen == kVNone && liveSinceLoad == 0 && (verdict == kVHolds || verdict == kVNot)) ? 1 : 0;
}
/* T-889 fold 1 (review 3): DOES THE HOLDER'S OWN STATE STILL HAVE THE ITEM? E_h was reserved right after the lift, so a record of the
   holder's squad at or past E_h was written after the lift (it lacks the item), and a save asked for at or after E_h too.
   lineLive: the line was written in this process since its last load (no reload since the lift: the live state lacks it).
   squadKnown: the line names the holder's squad record (a character; a box / the ground has no record of this game's here).
   Under the mark rule a record below E_h, or the save itself (mark below E_h), predates the lift: HAS. Under the other rules a
   record that was applied is newer than the save, so one below E_h HAS; with none applied the save decides, and it is unknown. */
const int kHsLacks = 0, kHsHas = 1, kHsUnknown = 2;
inline int HolderStateAfterLoad(int lineLive, unsigned long long eh, int squadKnown, unsigned long long appliedSeq, int markRule, unsigned long long markSeq)
{
    if (lineLive != 0) return kHsLacks;
    if (eh == 0ULL || squadKnown == 0) return kHsUnknown;
    if (appliedSeq >= eh) return kHsLacks;
    if (markRule != 0) return (markSeq >= eh) ? kHsLacks : kHsHas;
    return (appliedSeq != 0ULL) ? kHsHas : kHsUnknown;
}
/* THE HOLDER'S ACTION on a verdict for its `held` line. haveLiveObject: a row still keeps the object (in no inventory: no reload since);
   holderState: HolderStateAfterLoad; liveHere: -1 the line's place is not loaded here, 0 the item is not there, 1 it is (looked at
   only when it decides). Holds: close - after removing the holder's own reloaded copy when its state HAS it (wait while it is not
   loaded), or the copy found at the line's very square when its state is unknown (wait while not loaded). Not placed: restore the live object; the state
   HAS it - close with no rebuild; LACKS it - rebuild; unknown - rebuild only when it is not found at the line's place (wait while
   not loaded). Unknown verdict: keep. */
const int kActKeep = 0, kActClose = 1, kActRestoreLive = 2, kActRebuild = 3, kActRemoveThenClose = 4, kActCloseNoRebuild = 5;
inline int HolderAction(int lineState, int verdict, int haveLiveObject, int holderState, int liveHere)
{
    if (lineState != kStHeld || (verdict != kVHolds && verdict != kVNot)) return kActKeep;
    if (haveLiveObject != 0) return (verdict == kVHolds) ? kActClose : kActRestoreLive;
    if (verdict == kVHolds)
    {
        if (holderState == kHsLacks) return kActClose;
        if (liveHere < 0) return kActKeep;   /* its place is not loaded: removed (or found absent) when it is */
        return (liveHere != 0) ? kActRemoveThenClose : kActClose;
    }
    if (holderState == kHsHas) return kActCloseNoRebuild;
    if (holderState == kHsLacks) return kActRebuild;
    if (liveHere < 0) return kActKeep;
    return (liveHere != 0) ? kActCloseNoRebuild : kActRebuild;
}
/* CLOSED's value: 1 closed (the holder destroyed its copy); 2 returned (the holder has the item back); T-889 fold 1: 3 the holder keeps
   NO record lines (its records writer is off - review 6: the receiver's line has nothing to settle against); 4 unknown (the holder's
   writer is on but it has no such line and nothing in memory - review 10: never taken as "placed"). */
const int kClosedPlaced = 1, kClosedReturned = 2, kClosedNoLines = 3, kClosedUnknown = 4;
/* THE RECEIVER ON A CLOSED. liveCopy: the receiver's copy is placed (a live placement, or a frozen "holds"). A returned TAKE / ground
   / box with a copy here: the copy goes (it is provisional). A GIVE's builder never removes on CLOSED - the giver's REVOKE removes
   exactly the object built (and a give with a line returns only on a "not placed" or a live refusal: there is no copy). Unknown:
   the line is kept (asked again). */
const int kRcNone = 0, kRcDrop = 1, kRcRemoveCopy = 2, kRcKeep = 3;
inline int ReceiverOnClosed(int haveIn, int value, int liveCopy, int kind)
{
    if (haveIn == 0) return kRcNone;
    if (value == kClosedUnknown) return kRcKeep;
    if (value == kClosedReturned && liveCopy != 0 && kind != kKindGive) return kRcRemoveCopy;
    return kRcDrop;
}
/* T-889 fold 5 (2026-10-17): A CLOSED 1 IS ACKNOWLEDGED ONLY ONCE THE ITEM IS IN A COMMITTED RECORD OF THIS GAME. The holder gives up its
   copy at PLACED ok=1 and lets its closed line go at our ACK; our own squad record holding the item is queued later (ItOwnHandOver ->
   OwnMarkCharDirtyNow -> OwnTick -> the writer thread). DURABLE: the squad's last committed record has a seq at or past E (E is reserved
   before the add, so every record handed to the writer after it - built after the add - has a larger seq), or a write of that squad at or
   after E was skipped as the same bytes as its reference record (the queued job, else the last committed one) and that reference has
   committed - it equals what is on disk. kDurNo not yet; kDurCommit by a commit; kDurSameBytes by a same-bytes skip. E 0: none known. */
const int kDurNo = 0, kDurCommit = 1, kDurSameBytes = 2;
inline int InRecordDurable(int haveCommit, unsigned long long committedSeq, unsigned long long E, int haveSkip, unsigned long long skipAtSeq,
                           unsigned long long skipRefSeq)
{
    if (E == 0ULL) return kDurNo;
    if (haveCommit != 0 && committedSeq >= E) return kDurCommit;
    if (haveSkip != 0 && skipAtSeq >= E && haveCommit != 0 && committedSeq >= skipRefSeq) return kDurSameBytes;
    return kDurNo;
}
/* THE RECEIVER ON CLOSED 1 WITH ITS IN LINE. live: placed in this process since its last load. A live line with no squad record to wait
   for (a box - its record is not this game's - or no E): dropped at once, as before (kCpDropNoRecord). A live line waits - no drop, no
   ACK - until durable (kCpWait). After a load (not live) a commit proves nothing - a record written after the reload lacks an item the
   crash lost - so the receiver's own after-load verdict decides: holds -> drop; not / unknown -> KEPT, no ACK (the holder keeps its
   closed line and answers the next QUERY with CLOSED 1 again). */
const int kCpDrop = 0, kCpWait = 1, kCpKeep = 2, kCpDropNoRecord = 3;
inline int ClosedPlacedRoad(int live, int squadKnown, unsigned long long E, int durable, int verdictAfterLoad)
{
    if (live != 0)
    {
        if (squadKnown == 0 || E == 0ULL) return kCpDropNoRecord;
        return (durable != kDurNo) ? kCpDrop : kCpWait;
    }
    return (verdictAfterLoad == kVHolds) ? kCpDrop : kCpKeep;
}
/* T-889 fold 5b (2026-10-17): THE TAKER'S CLOSED 1 AFTER A LOAD. The holder destroyed its copy at PLACED ok=1, so when the reloaded
   receiver lacks the item nothing else gives it back: the receiver rebuilds it from its own line's item bytes - only when it DEFINITELY
   lacks it. TakerLackLook: receiverLook - its own look at the line's square (HovLiveCopy wide 0, G3 when the square does not show the
   placement: 1 the stack its placement made, 0 not placed, -1 not loaded / unreadable / unknown); searchRead / exactFound - the exact
   search (record, quality, level, stack size) of its whole character / box, every pack in it. kTlkLacks only for a loaded, readable
   container where neither finds it. */
const int kTlkLacks = 0, kTlkFound = 1, kTlkUnknown = -1;
inline int TakerLackLook(int receiverLook, int searchRead, int exactFound)
{
    if (receiverLook > 0) return kTlkFound;
    if (receiverLook < 0 || searchRead == 0) return kTlkUnknown;
    return (exactFound != 0) ? kTlkFound : kTlkLacks;
}
/* THE DECISION: held -> drop and ACK; not held AND lacks -> rebuild (then the live road of fold 5: ACK once its own record holding the
   rebuilt item commits); not held but found here or not lookable, or unknown -> wait (kept, no ACK; owner 897 a). */
const int kTcDrop = 0, kTcRebuild = 1, kTcWait = 2;
inline int TakerClosedAfterLoad(int verdictAfterLoad, int lackLook)
{
    if (verdictAfterLoad == kVHolds) return kTcDrop;
    if (verdictAfterLoad == kVNot && lackLook == kTlkLacks) return kTcRebuild;
    return kTcWait;
}
/* AFTER THE REBUILD. rb: the rebuild's answer (1 rebuilt, 2 found on a character gone for good, 0 failed, -1 wait); toGround: it went on
   the ground at the feet of a character gone for good; rewritten: the line was rewritten (a new E reserved before the rebuild, its frozen
   verdict cleared, its placement where the rebuild ended). Ground -> drop and ACK now (no squad record of this game's holds a ground
   item); rewritten -> live: the fold-5 durable road decides (wait for the commit, then drop and ACK); rebuilt but not rewritten -> kept
   (the rewrite tried again, never a second rebuild in this load); anything else -> kept (wait). */
const int kTnKeep = 0, kTnLive = 1, kTnDropNow = 2;
inline int TakerRebuildNext(int rb, int toGround, int rewritten)
{
    if (rb != 1) return kTnKeep;
    if (toGround != 0) return kTnDropNow;
    return (rewritten != 0) ? kTnLive : kTnKeep;
}
/* THE HOLDER ON A RECEIVER'S QUERY. 0 = send nothing (its line is held, or a live row still settles it); else the CLOSED value.
   memValue: this process settled that transfer with no line on file (1 / 2, 0 none); rowLive: a row of that transfer is still open
   here; writerOn: this game keeps record lines. A missing line is never answered "closed" (review 10). */
inline int HolderAnswer(int haveLine, int lineState, int memValue, int rowLive, int writerOn)
{
    if (haveLine != 0)
    {
        if (lineState == kStHeld) return 0;
        return (lineState == kStReturned) ? kClosedReturned : kClosedPlaced;
    }
    if (memValue == kClosedPlaced || memValue == kClosedReturned) return memValue;
    if (rowLive != 0) return 0;
    return (writerOn != 0) ? kClosedUnknown : kClosedNoLines;
}
/* review 1: a holder line that could not be rewritten closed / returned is NOT announced; the rewrite and its CLOSED are tried again at
   every hand-over tick, at most kSettleTries times (then this process stops trying and never acts on that line again; the next launch
   asks again). */
const int kSettleTries = 24;
inline int SettleRetryDue(int tries) { return (tries < kSettleTries) ? 1 : 0; }
/* review 4: a GIVE with a held line does not refund at its 10 s lapse - it is PARKED (its item kept in no inventory) and settled by the
   owner's verdict (0 put back, 1 the escrow destroyed) or by a late answer. A priced give or a consume keeps the old lapse road. */
inline int GiveLapseParks(int hasLine, int trade, int takeOneConsume) { return (hasLine != 0 && trade == 0 && takeOneConsume == 0) ? 1 : 0; }
/* review 5: an escrowed TAKE / box / ground row with a held line whose asker is away is PARKED out of the 64 owner rows - the line, not
   the row, settles it - and no longer holds its character's / box's syncing. */
inline int ParkTakeRow(int dir, int escrow, int hasLine, int askerHere) { return (dir == 0 && escrow != 0 && hasLine != 0 && askerHere == 0) ? 1 : 0; }
/* re-asked every 60 s; the first ask waits kQueryFirstMs after the line was first seen in this process (a live answer comes first) */
const unsigned int kQueryEveryMs = 60000u, kQueryFirstMs = 10000u;
inline int QueryDue(unsigned int firstSeen, int sentOnce, unsigned int lastAt, unsigned int now)
{
    if ((unsigned int)(now - firstSeen) < kQueryFirstMs) return 0;
    return (sentOnce == 0 || (unsigned int)(now - lastAt) >= kQueryEveryMs) ? 1 : 0;
}

/* A2: THE WRITER HOLD. A character has an item move not yet published: the ring holds a move out of it (kKindChar) or an undrained
   pack move (kKindBag - pending for every character until the drain resolves it), or the split slot holds one of those (the first
   two tests of items.cpp InvNetCharPending). Its squad's record is not written then (a record caught mid-move is T1289's double). */
inline int CharUnpublished(int ringHasChar, int ringHasBag, int splitHasCharOrBag)
{
    return (ringHasChar != 0 || ringHasBag != 0 || splitHasCharOrBag != 0) ? 1 : 0;
}

/* CHANGE B (owner 896 (b)): an owner row holds its character's / box's syncing - except a GIVE row (dir 1, not escrow) whose giver
   is not here: a GIVE's build rang and published, so the row carries no unpublished move. */
inline int GiveRowHoldsSync(int dir, int escrow, int giverHere)
{
    return (dir == 1 && escrow == 0 && giverHere == 0) ? 0 : 1;
}
/* T-889 fold 1 (review 5): the same, and an escrowed TAKE row with a held line whose asker is away does not hold it either */
inline int OwnerRowHoldsSync(int dir, int escrow, int otherHere, int hasLine)
{
    if (GiveRowHoldsSync(dir, escrow, otherHere) == 0) return 0;
    return (ParkTakeRow(dir, escrow, hasLine, otherHere) != 0) ? 0 : 1;
}

/* ---- T-889 fold 2 (re-check 2026-10-17) ---- */
/* N1: IS THE ITEM AT A LINE'S PLACE THE LINE'S ITEM? Every field of the line's stored item bytes that tells one stack from another
   must be the same: the record (base sid), the stack size, the quality (within a thousandth, as givelate.h LateGiveQualitySame) and
   the level. Another item of the same record put there is NOT the line's item. allowLarger: a removal search over the whole
   inventory also takes a larger stack of the very same item (units stacked onto it); a look that decides a verdict never does.
   An unreadable quality or level never matches. */
inline int LineItemMatch(int sidSame, int haveQty, int wantQty, int qualityRead, float haveQ, float wantQ, int levelRead, int haveLevel,
                         int wantLevel, int allowLarger)
{
    if (sidSame == 0 || wantQty <= 0 || qualityRead == 0 || levelRead == 0) return 0;
    if (haveQty != wantQty && !(allowLarger != 0 && haveQty > wantQty)) return 0;
    const float d = haveQ - wantQ;
    if (!(d < 0.001f && d > -0.001f)) return 0;
    return (haveLevel == wantLevel) ? 1 : 0;
}
/* N1: THE LOOK AT A LINE'S OWN SQUARE (section, x, y - through its pack path). -1 unknown: the character / box is not loaded here, or
   the square could not be read; 1 the line's item is there (LineItemMatch); 0 the container is loaded and readable and the square holds
   nothing, or another item (another quality, stack size or level - or the item sits on another square): NOT this item. */
inline int SquareLook(int loaded, int squareRead, int itemThere, int itemMatches)
{
    if (loaded == 0 || squareRead == 0) return -1;
    return (itemThere != 0 && itemMatches != 0) ? 1 : 0;
}
/* (a): DOES THE HOLDER LOOK AT ITS OWN PLACE before acting on a verdict with no live object? Holds: unless its state surely lacks
   the item (no reload since the lift, or a record written after it) - a character, a box AND the ground alike (a reload of a save
   from before a ground pickup puts the item back on the ground). Not placed: when its state is unknown (found there: close with no
   rebuild). */
inline int HolderNeedsLook(int verdict, int holderState)
{
    if (verdict == kVHolds) return (holderState != kHsLacks) ? 1 : 0;
    if (verdict == kVNot) return (holderState == kHsUnknown) ? 1 : 0;
    return 0;
}
/* N2: verdict 1 and the holder's own copy was found - the removal's answer (1 removed, 0 not there any more, -1 not this game's to
   write now: the character is not ours / the box's or ground's patch of map is not held here, 2 the removal did not take). Only a
   removal that ran, or a copy that is really gone, closes the line; otherwise the line stays open and the next verdict tries again. */
const int kRmWait = 0, kRmClose = 1;
inline int HolderRemoveOutcome(int removal) { return (removal == 1 || removal == 0) ? kRmClose : kRmWait; }
/* N3: WHERE A CHARACTER LINE'S ITEM IS REBUILT on a verdict 0 with no live object. spawned: the line's character is loaded here;
   dead 1 / 0 / -1 unreadable; mine: this game runs it; factionRead; ownFaction: its faction is this game's player faction;
   otherPlayer: its faction is another player's. Gone for good (dead, dismissed - no longer in our faction - or now another player's):
   rebuilt on the ground at its feet. Ours and run here: into it. Not loaded, unreadable, or ours but run by another game for now:
   wait (asked again in 60 s). */
const int kRebWait = 0, kRebInto = 1, kRebGround = 2;
inline int CharRebuildRoute(int spawned, int dead, int mine, int factionRead, int ownFaction, int otherPlayer)
{
    if (spawned == 0 || dead < 0) return kRebWait;
    if (dead != 0) return kRebGround;
    if (factionRead == 0) return kRebWait;
    if (otherPlayer != 0) return kRebGround;
    if (ownFaction != 0) return (mine != 0) ? kRebInto : kRebWait;
    return kRebGround;
}
/* N5: A VERDICT ON A PARKED TAKE ROW always consumes it. None parked: nothing; parked before a load (its object went with that world):
   dropped; else back into the owner table when a row is free, or settled IN PLACE on the spare row when the table is full - never
   left parked beside a settle (a later PLACED ok=0 would put the parked object back as well). */
const int kPsNone = 0, kPsDrop = 1, kPsTable = 2, kPsSpare = 3;
inline int ParkedSettleRoad(int haveParked, int sameLoad, int rowFree)
{
    if (haveParked == 0) return kPsNone;
    if (sameLoad == 0) return kPsDrop;
    return (rowFree != 0) ? kPsTable : kPsSpare;
}

/* ---- T-889 fold 3 (re-check 2 of fold 2, 2026-10-17) ---- */
/* F1: A LINE WHOSE CHARACTER ID CANNOT BE READ AT ITS WRITE. canRefuse: nothing has moved yet (the holder before its ok CONFIRM, the
   taker before its add) - the hand-over is refused and the item stays where it is. Otherwise (the giver's request is already out, the
   owner already built the give) the line is written naming no character (c:?): it never resolves, so it is held - never settled by a
   look or a rebuild - and the item stays held off both characters until a live answer settles it. */
const int kCidWrite = 0, kCidRefuse = 1, kCidUnknown = 2;
inline int CharIdWriteRoad(int idRead, int canRefuse)
{
    if (idRead != 0) return kCidWrite;
    return (canRefuse != 0) ? kCidRefuse : kCidUnknown;
}
/* F1: the line's character NOW is the one loaded character whose own handle has the line's serial and type: none or more than one
   (an id two characters answer to) is "not loaded here" */
inline int CharMatchUse(int matches) { return (matches == 1) ? 1 : 0; }

/* F2: THE LOOK AT A RECEIVER LINE'S SQUARE, as its placement really ended (the add may merge onto a stack or move to the first free
   square). placedKnown: the line carries that end (its square, pre, got); itemThere: something is on the square; kindRead: its record
   (and, when it is the line's record, its quality, level and stack size) could be read; sameKind: the line's item's record, quality and
   level; haveQty: its stack size. 1 holds (the stack the placement made); 0 not placed (the square holds exactly what it held before, or
   nothing of this item); -1 unknown (not loaded, unreadable, a stack of this item of any other size). Before the placement was written
   down (a crash between the add and its rewrite) it is -1, never 0. */
inline int PlacedLook(int loaded, int squareRead, int placedKnown, int itemThere, int kindRead, int sameKind, int haveQty, int pre, int got)
{
    if (loaded == 0 || squareRead == 0 || placedKnown == 0) return -1;
    if (itemThere == 0) return 0;
    if (kindRead == 0) return -1;
    if (sameKind == 0) return 0;
    if (haveQty == got) return 1;
    if (pre > 0 && haveQty == pre) return 0;
    return -1;
}
/* F2: WHERE THE ADD ENDED - the section's stacks of the line's item (record, quality, level; any size) before and after the add: id (the
   object, compared only), square and size. Exactly one change, and nothing else: a new object of `qty` units (pre 0), or one object on
   its square grown by exactly `qty` (merged onto: pre its size before). 1 = *x, *y, *pre, *got set; 0 = it cannot be told (none, two,
   a stack gone or moved, any other size) - the line's look then stays unknown. */
struct PlStack { unsigned long long id; int x, y, qty; };
inline int PlacedWhere(const std::vector<PlStack>& before, const std::vector<PlStack>& after, int qty, int* x, int* y, int* pre, int* got)
{
    if (qty <= 0) return 0;
    int found = 0, bad = 0, fx = 0, fy = 0, fp = 0, fg = 0;
    for (size_t i = 0; i < after.size(); ++i)
    {
        const PlStack& a = after[i];
        const PlStack* b = 0;
        for (size_t j = 0; j < before.size(); ++j) if (before[j].id == a.id) { b = &before[j]; break; }
        if (b == 0)
        {
            if (a.qty == qty) { ++found; fx = a.x; fy = a.y; fp = 0; fg = a.qty; } else ++bad;
            continue;
        }
        if (b->x != a.x || b->y != a.y) { ++bad; continue; }
        if (a.qty == b->qty) continue;
        if (a.qty == b->qty + qty) { ++found; fx = a.x; fy = a.y; fp = b->qty; fg = a.qty; } else ++bad;
    }
    for (size_t j = 0; j < before.size(); ++j)
    {
        int still = 0;
        for (size_t i = 0; i < after.size(); ++i) if (after[i].id == before[j].id) { still = 1; break; }
        if (still == 0) ++bad;
    }
    if (found != 1 || bad != 0) return 0;
    *x = fx; *y = fy; *pre = fp; *got = fg;
    return 1;
}

/* F3: A GROUND HOLDER'S REMOVAL of its reloaded copy - GrRemoveLocal's answer: 1 removed, 0 not removed (no key to claim, or not / not
   surely on the ground), -1 deferred (its claim is busy). GROUND GONE goes only after a removal that took (claim and remove first);
   anything else sends nothing and the look answers 2 (the removal did not take): the line stays open (holderRemoveWait). */
inline int GroundRemoveSendsGone(int removal) { return (removal == 1) ? 1 : 0; }
inline int GroundRemoveLook(int removal) { return (removal == 1) ? 1 : 2; }

/* F4: BEFORE A GROUND REBUILD for a loaded character (dead, dismissed, captured - its faction neither ours nor another player's - or now
   another player's): the exact search of its whole inventory, every pack in it, for the line's item. searchRead 0: unreadable - wait;
   found: its reloaded copy still carries it - closed with no rebuild (verdict 0: returned, this game has it); else the ground rebuild. */
const int kGrbRebuild = 0, kGrbHolds = 1, kGrbWait = 2;
inline int GroundRebuildCheck(int searchRead, int found)
{
    if (searchRead == 0) return kGrbWait;
    return (found != 0) ? kGrbHolds : kGrbRebuild;
}

/* ---- T-889 fold 4 (re-check 3, 2026-10-17) ---- */
/* G1: THE TAKER'S LINE AROUND ITS ADD. Every add of a taken item - the live confirm's and the late one's (a confirm that came after the
   taker's own 10 s) - goes through one road: the in line first (HovTakerIn: 0 refused, 1 no line kept - the records writer is off, 2
   written), then the add, only when the line was not refused; a written line then gets its placed step (or goes with an add that
   failed); with no line kept, a placement is remembered for this process. */
const int kTlNone = 0, kTlStep = 1, kTlMemory = 2;
inline int TakerMayAdd(int inAnswer) { return (inAnswer != 0) ? 1 : 0; }
inline int TakerLineStep(int inAnswer, int added)
{
    if (inAnswer == 2) return kTlStep;
    if (inAnswer == 1 && added != 0) return kTlMemory;
    return kTlNone;
}
/* G2: THE HOLDER'S LOOK BEFORE A REBUILD (verdict 0 with its own state unknown) at a character or box loaded here. pathResolved: the
   line's square was reached through its pack path; squareLook: SquareLook's answer there (1 / 0 / -1); searchRead: its whole inventory,
   every pack in it, could be read; foundElsewhere: the line's exact item (record, quality, level, stack size) is in it. 1 holds (closed
   with no rebuild); -1 unknown (wait: a square or a search that could not be read, or a pack path that no longer resolves - the pack
   moved or was taken off, and the copy may sit in it); 0 not there (rebuild) only for a readable square AND a readable search that found
   nothing. */
inline int HolderNotPlacedLook(int pathResolved, int squareLook, int searchRead, int foundElsewhere)
{
    if (pathResolved != 0 && squareLook != 0) return (squareLook > 0) ? 1 : -1;
    if (searchRead == 0) return -1;
    if (foundElsewhere != 0) return 1;
    return (pathResolved != 0) ? 0 : -1;
}
/* G3: A RECEIVER'S LOOK when its square does not show the stack its placement made (PlacedLook answered 0): the whole box / character
   inventory, every pack in it, is searched. gotElsewhere: a stack of the line's item of exactly the size the placement made (record,
   quality, level, size == got); kindAnywhere: any stack of the line's item in it. 0 (not placed) only when that search read and found no
   stack of size got AND either the square holds exactly the stack it held before, or no stack of this item is anywhere in the container;
   anything else is -1 (unknown: held, asked again) - never 0 on a guess. */
inline int PlacedLookWide(int squareLook, int preAtSquare, int searchRead, int gotElsewhere, int kindAnywhere)
{
    if (squareLook != 0) return squareLook;
    if (searchRead == 0 || gotElsewhere != 0) return -1;
    if (preAtSquare != 0) return 0;
    return (kindAnywhere == 0) ? 0 : -1;
}
/* G4: THE SEARCH BEFORE A GROUND REBUILD looks at the line's own square (through its pack path) first, then the whole inventory. A match
   found only elsewhere still counts as the character carrying it (an identical twin stack would be taken for it - the trade-off); it is
   counted (twinElsewhere). */
const int kGsNone = 0, kGsSquare = 1, kGsElsewhere = 2;
inline int GroundSearchHit(int squareHit, int treeHit)
{
    if (squareHit != 0) return kGsSquare;
    return (treeHit != 0) ? kGsElsewhere : kGsNone;
}

/* ---- the wire: MSG_ITEM_HANDOVER (79, RELIABLE, game to game by slot) ----
   u8 kind (1 QUERY, 2 VERDICT, 3 CLOSED, 4 ACK) | u32 askerSlot | u32 reqId | u32 holderSlot | u8 value - little-endian, 14 bytes.
   value: QUERY the asking line's role (0 the holder asks, 1 the receiver asks); VERDICT 0 / 1 / 2; CLOSED 1 closed / 2 returned /
   3 no lines / 4 unknown; ACK (T-889 fold 1, review 10: the receiver got that CLOSED - the holder may drop its settled line) the
   CLOSED value it acknowledges. */
const int kMsgQuery = 1, kMsgVerdict = 2, kMsgClosed = 3, kMsgAck = 4;
const size_t kMsgLen = 14;
struct HandMsg
{
    int kind;
    unsigned int asker, reqId, holder;
    int value;
    HandMsg() : kind(0), asker(0), reqId(0), holder(0), value(0) {}
};
inline int MsgValueOk(int kind, int value)
{
    if (kind == kMsgQuery) return (value == 0 || value == 1) ? 1 : 0;
    if (kind == kMsgVerdict) return (value >= 0 && value <= 2) ? 1 : 0;
    if (kind == kMsgClosed || kind == kMsgAck) return (value >= kClosedPlaced && value <= kClosedUnknown) ? 1 : 0;
    return 0;
}
inline void HrPutLe(std::vector<char>* b, unsigned int v)
{
    b->push_back((char)(v & 0xFF)); b->push_back((char)((v >> 8) & 0xFF));
    b->push_back((char)((v >> 16) & 0xFF)); b->push_back((char)((v >> 24) & 0xFF));
}
inline unsigned int HrGetLe(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8)
         | ((unsigned int)(unsigned char)p[2] << 16) | ((unsigned int)(unsigned char)p[3] << 24);
}
/* 0 for a kind / value out of range (nothing written) */
inline int EncodeMsg(const HandMsg& m, std::vector<char>* out)
{
    if (out == 0 || MsgValueOk(m.kind, m.value) == 0) return 0;
    out->clear();
    out->push_back((char)(unsigned char)m.kind);
    HrPutLe(out, m.asker); HrPutLe(out, m.reqId); HrPutLe(out, m.holder);
    out->push_back((char)(unsigned char)m.value);
    return 1;
}
/* 0 for any length but 14, an unknown kind, a value out of range, or a slot above 2^31-1 */
inline int DecodeMsg(const char* p, size_t n, HandMsg* m)
{
    if (p == 0 || m == 0 || n != kMsgLen) return 0;
    HandMsg r;
    r.kind = (int)(unsigned char)p[0];
    r.asker = HrGetLe(p + 1); r.reqId = HrGetLe(p + 5); r.holder = HrGetLe(p + 9);
    r.value = (int)(unsigned char)p[13];
    if (MsgValueOk(r.kind, r.value) == 0 || r.asker > 0x7FFFFFFFu || r.holder > 0x7FFFFFFFu) return 0;
    *m = r;
    return 1;
}

}   /* namespace hovrec */
