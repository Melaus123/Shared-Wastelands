/* src/common/itemstatwire.h - items9 (investigations/item-stats-parity.md gaps 1-3, test levers 4): THE ITEM STATS THAT DID NOT
 * REACH THE OTHER GAME, AS BYTES AND TEXT.
 *
 * 1. USES SPENT IN PLACE (gap 1, F970). A medkit's uses and a half-eaten food item's bites (Item +0x118 charges) are written raw
 *    by the engine (drainMedkit 0x6437C0, eatItem 0x5CFCF0) while the item stays in its slot. The inventory safety net (invnet.h)
 *    already sees every such change on what this game writes (its own characters, the boxes it holds) as a kOpCharges catch; it
 *    now goes out as ITEM_MOVE op 4 CHARGES (protocol 78):
 *        the usual head {u32 uid, u8 op 4, str section, u32 x, u32 y, u32 quantity} [+ box key, box id when uid 0]
 *        then THIS tail: f32 charges | str base sid                                                   (str = u32 len + bytes)
 *    The receiver writes +0x118 on the item at that slot only when its base sid matches (a wrong-slot write is refused).
 * 2-3. THE GROUND (gaps 2 and 3). The area holder's GROUND ADD (ITEM_MOVE op 0 with a ground key) now carries, after the box key
 *    and id, an optional BAG1 block (bagwire.h - a dropped backpack's contents, rows + OWNL tail) and then the optional OWN1
 *    block (ownerwire.h - the stolen mark), both only when there is something to say. The same tail is legal on every op 0.
 * 4. `snapshot ... detail` item lines gain the stats (DetailStats) and `itemtest set` reads its arguments here (ParseSetArgs).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "bagwire.h"
#include "ownerwire.h"

namespace coopistat {

const int kMoveOpCharges = 4;    /* ITEM_MOVE op 4 - equals coopinv::kOpCharges on purpose (ItNetSendMove follows the wire) */
const float kChargesMax = 1.0e7f;

const int kTailOk = 0;
const int kTailCut = 1;         /* the bytes end inside the tail */
const int kTailBadValue = 2;    /* charges not finite / negative / huge, or an empty base sid */
const int kTailBadBag = 3;      /* a BAG1 block that is there but not whole and in range */
const int kTailBadOwner = 4;    /* an OWN1 block that is cut or out of range */
const int kTailTrailing = 5;    /* bytes left after the last block */

inline const char* TailWhy(int why)
{
    return why == kTailOk ? "ok" : why == kTailCut ? "cut" : why == kTailBadValue ? "a bad value"
         : why == kTailBadBag ? "a bad backpack block" : why == kTailBadOwner ? "a bad owner block"
         : why == kTailTrailing ? "trailing bytes" : "unknown";
}

inline bool ChargesOk(float f) { return f == f && f >= 0.0f && f <= kChargesMax; }

/* op 4's tail. false = NOTHING WAS WRITTEN (bad charges, empty or over-long sid). */
inline bool EncodeChargesTail(std::vector<char>* b, float charges, const std::string& sid)
{
    if (b == 0 || !ChargesOk(charges) || sid.empty() || sid.size() > coopbag::kBagMaxSid) return false;
    unsigned int u = 0;
    std::memcpy(&u, &charges, 4);
    coopbag::BagPutU32(b, u);
    coopbag::BagPutStr(b, sid);
    return true;
}
/* op 4's tail at `at`; it is the LAST thing in the message. On kTailOk `*charges`, `*sid`, `*end` are set. */
inline int DecodeChargesTail(const char* p, size_t size, size_t at, float* charges, std::string* sid, size_t* end)
{
    if (p == 0) return kTailCut;
    size_t off = at;
    unsigned int u = 0;
    std::string s;
    if (!coopbag::BagGetU32(p, size, &off, &u)) return kTailCut;
    if (!coopbag::BagGetStr(p, size, &off, coopbag::kBagMaxSid, &s)) return kTailCut;
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    if (!ChargesOk(f) || s.empty()) return kTailBadValue;
    if (off != size) return kTailTrailing;
    *charges = f; *sid = s; *end = off;
    return kTailOk;
}

/* op 0's tail: [BAG1 rows] [OWN1]. `*bagSent` 1 rows written / 0 none to write / -1 the rows would not encode (nothing written:
   the item travels, its contents do not - the caller counts it); `*ownSent` the same for the owner. Always returns true. */
inline bool EncodeAddTail(std::vector<char>* b, const std::vector<coopbag::BagRow>& bag, const coopmark::OwnerId& owner,
                          int* bagSent, int* ownSent)
{
    *bagSent = 0; *ownSent = 0;
    if (!bag.empty())
    {
        std::vector<char> bb;
        if (coopbag::EncodeBagRows(&bb, bag)) { b->insert(b->end(), bb.begin(), bb.end()); *bagSent = 1; }
        else *bagSent = -1;
    }
    if (owner.kind != 0)
    {
        std::vector<char> ob;
        if (coopmark::EncodeOwner(&ob, owner)) { b->insert(b->end(), ob.begin(), ob.end()); *ownSent = 1; }
        else *ownSent = -1;
    }
    return true;
}
/* op 0's tail at `at` (right after the box key/id, or the item record for a character). Nothing left = kTailOk with nothing set.
   `bag` / `owner` are written only on kTailOk. */
inline int DecodeAddTail(const char* p, size_t size, size_t at, std::vector<coopbag::BagRow>* bag, coopmark::OwnerId* owner,
                         size_t* end)
{
    size_t off = at;
    std::vector<coopbag::BagRow> rows;
    coopmark::OwnerId o;
    if (p != 0 && off < size && !coopmark::OwnerBlockAt(p, size, off) && !coopbag::BagPathAt(p, size, off))   /* bag23 part 2: a BAGP block is the caller's */
    {
        size_t be = 0;
        if (coopbag::DecodeBagRows(p, size, off, &rows, &be) != coopbag::kBagOk) return kTailBadBag;
        off = be;
    }
    if (p != 0 && off < size && !coopbag::BagPathAt(p, size, off))
    {
        size_t oe = 0;
        const int ow = coopmark::DecodeOwner(p, size, off, &o, &oe);
        if (ow == coopmark::kOwnBad) return kTailBadOwner;
        if (ow != coopmark::kOwnOk) return kTailTrailing;
        off = oe;
    }
    if (off != size && !coopbag::BagPathAt(p, size, off)) return kTailTrailing;   /* bag23 part 2: a trailing BAGP (pack path) is left for the caller; `*end` stops before it */
    bag->swap(rows); *owner = o; *end = off;
    return kTailOk;
}

/* ---- `snapshot <tag> detail <uid>`: the stats appended to each item line (both games print the same text for the same item) ---- */
inline std::string StatSid(const char* s) { return (s == 0 || s[0] == 0) ? std::string("-") : std::string(s); }
inline std::string DetailStats(float q, float ch, const char* company, const char* material, const char* color, int level,
                               int function, int unique, const coopmark::OwnerId& o)
{
    char b[96];
    std::sprintf(b, " q=%.2f ch=%.2f", (double)q, (double)ch);
    std::string s(b);
    s += " co=" + StatSid(company) + " mat=" + StatSid(material) + " col=" + StatSid(color);
    std::sprintf(b, " lvl=%d fn=%d uq=%d", level, function, unique != 0 ? 1 : 0);
    s += b;
    if (o.kind == 0) s += " stolen=0";
    else
    {
        std::sprintf(b, " stolen=%u:%u:", (unsigned int)o.kind, o.uid);
        s += b;
        s += o.factionSid.empty() ? std::string("-") : o.factionSid;
    }
    return s;
}

/* ---- `itemtest set <item ref> [q=<0-100>] [ch=<n>]` (TEST-ONLY) ----
   The item ref is `any`, a base sid (the first such item in the general section), or a slot `<section>:<x>,<y>`. */
struct SetArgs
{
    std::string ref;
    int isSlot; std::string section; int x, y;
    int haveQ, haveCh;
    float q, ch;
    SetArgs() : isSlot(0), x(0), y(0), haveQ(0), haveCh(0), q(0.0f), ch(0.0f) {}
};
inline bool SetNum(const std::string& s, float lo, float hi, float* v)
{
    if (s.empty() || s.size() > 24) return false;
    char* e = 0;
    const double d = std::strtod(s.c_str(), &e);
    if (e == 0 || *e != 0 || !(d == d) || d < (double)lo || d > (double)hi) return false;
    *v = (float)d;
    return true;
}
inline bool SetInt(const std::string& s, int* v)
{
    if (s.empty() || s.size() > 5) return false;
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; n = n * 10 + (s[i] - '0'); }
    *v = n;
    return true;
}
/* 1 = parsed; 0 = usage error (`*err` says which part). */
inline int ParseSetArgs(const std::string& line, SetArgs* out, std::string* err)
{
    SetArgs a;
    std::vector<std::string> tok;
    {
        std::string t;
        for (size_t i = 0; i <= line.size(); ++i)
        {
            const char c = (i < line.size()) ? line[i] : ' ';
            if (c == ' ' || c == '\t') { if (!t.empty()) tok.push_back(t); t.clear(); }
            else t += c;
        }
    }
    if (tok.size() < 2 || tok.size() > 3) { *err = "want <item ref> and q= and/or ch="; return 0; }
    a.ref = tok[0];
    const size_t colon = a.ref.find(':');
    if (colon != std::string::npos)
    {
        const size_t comma = a.ref.find(',', colon);
        if (colon == 0 || comma == std::string::npos || colon >= coopbag::kBagMaxSection
            || !SetInt(a.ref.substr(colon + 1, comma - colon - 1), &a.x) || !SetInt(a.ref.substr(comma + 1), &a.y))
        { *err = "a slot is <section>:<x>,<y>"; return 0; }
        a.isSlot = 1; a.section = a.ref.substr(0, colon);
    }
    else if (a.ref.size() > coopbag::kBagMaxSid) { *err = "the item ref is too long"; return 0; }
    for (size_t i = 1; i < tok.size(); ++i)
    {
        const std::string& t = tok[i];
        if (t.compare(0, 2, "q=") == 0 && a.haveQ == 0)
        { if (!SetNum(t.substr(2), 0.0f, 100.0f, &a.q)) { *err = "q= is a number 0-100"; return 0; } a.haveQ = 1; }
        else if (t.compare(0, 3, "ch=") == 0 && a.haveCh == 0)
        { if (!SetNum(t.substr(3), 0.0f, 100000.0f, &a.ch)) { *err = "ch= is a number 0-100000"; return 0; } a.haveCh = 1; }
        else { *err = "unknown or repeated argument '" + t + "'"; return 0; }
    }
    *out = a;
    return 1;
}

}   /* namespace coopistat */
