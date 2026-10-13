/* src/common/gearwire.h - AN ITEM'S UNIFORM AND CRAFTER, IN WORDS BOTH GAMES UNDERSTAND.
 *
 * A gear item (Gear, Armour, Weapon, Sword, Crossbow, LockedArmour) carries the faction whose uniform it is and the name of the
 * character who crafted it. The faction travels as its wire name (relations' SidOf: an FCS stringID, or "@slot:<n>[:<name>]" for a
 * player faction), the crafter as its text.
 *
 * WIRE: a per-item block, appended only when either field is non-empty (a clean item's bytes are unchanged) -
 *   marker u32 'GEM1' | u32 len <= kGearMaxSid + uniform sid | u32 len <= kGearMaxCrafter + crafter
 * ITEM_MOVE op 0: after the OWN1 block. ITEM_REQUEST (a GIVE) and ITEM_CONFIRM (a TAKE, ok 1): after the optional OWN1 block.
 * CAPTURE_DONE: a taken row's record ends with its optional OWN1 block and then its optional GEM1 block.
 * A LIST TAIL after a list's OWNL tail (the PARITY_BOX items, the BAG1 rows, the CLOTHING 'QLT1' entries), naming only the entries that
 * have marks, by their index in the list:
 *   marker u32 'GEML' | u32 n (1..kGearTailMax, <= the list's count) | n x (u32 index, strictly rising, < count | u32 len + uniform sid |
 *   u32 len + crafter)
 * TEXT: no control byte (0x00-0x1F, 0x7F: it also rides a tab-separated journal line); uniform <= kGearMaxSid bytes, crafter <=
 * kGearMaxCrafter bytes. A field over its bound or with a control byte is sent EMPTY, never cut: EncodeGear and EncodeGearTail report
 * the fields they emptied (`*dropped`) for the caller to count - the list tails' callers pass 0 and count nothing there, because the
 * plugin's reader (items.cpp ItGearOfRaw) has already emptied such a field, counting it on the sends it books (itemGear dropped).
 * A block or tail that is cut, names neither field, or carries bad text is refused (what it rides on decides what that refuses);
 * GearTailSkip finds the end of a refused tail whose shape is whole, for a reader that keeps what follows it (the kit's details).
 * JOURNAL (the offline box-move journal): a "b3" line is the "b1" fields plus five more tab fields - the owner's kind, uid and sid as
 * a "b2" line has them (kind 0 = no owner, with uid 0 and an empty sid) - then the uniform and the crafter.
 * Pure: no engine memory, no Windows. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>
#include "ownerwire.h"   /* the journal's owner fields; the bounds the owner's faction uses */

namespace gearwire {

const unsigned int kGearMarker = 0x314D4547u;       /* 'G','E','M','1' little-endian */
const unsigned int kGearTailMarker = 0x4C4D4547u;   /* 'G','E','M','L' little-endian */
const unsigned int kGearMaxSid = coopmark::kOwnMaxSid;     /* the uniform's wire name: the bound an owner's faction has */
const unsigned int kGearMaxCrafter = 95u;
const unsigned int kGearTailMax = coopmark::kOwnTailMax;   /* the largest list it follows (PARITY_BOX: cooppar::kParityMaxItems) */

const int kGearOk = 0;
const int kGearAbsent = 1;   /* no bytes left, or the next bytes are not this block (the caller decides what that means) */
const int kGearBad = 2;      /* the marker is there but the rest is cut, names neither field, or carries bad text */

struct GearMarks
{
    std::string uniformSid;   /* the uniform's faction by its wire name; "" = none */
    std::string crafter;      /* the crafter's name; "" = none */
};

inline bool GearEmpty(const GearMarks& g) { return g.uniformSid.empty() && g.crafter.empty(); }
inline bool GearEqual(const GearMarks& a, const GearMarks& b) { return a.uniformSid == b.uniformSid && a.crafter == b.crafter; }

/* Text that may travel: at most `cap` bytes and no control byte. */
inline bool GearTextOk(const std::string& s, unsigned int cap)
{
    if (s.size() > cap) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}
/* What a block or a tail entry carries: at least one field, each within its bound. */
inline bool GearValid(const GearMarks& g)
{
    return !GearEmpty(g) && GearTextOk(g.uniformSid, kGearMaxSid) && GearTextOk(g.crafter, kGearMaxCrafter);
}
/* The marks as they travel: a field over its bound or with a control byte is emptied, never cut. Returns the fields emptied. */
inline unsigned int GearClean(const GearMarks& in, GearMarks* out)
{
    unsigned int n = 0;
    GearMarks g;
    if (GearTextOk(in.uniformSid, kGearMaxSid)) g.uniformSid = in.uniformSid; else ++n;
    if (GearTextOk(in.crafter, kGearMaxCrafter)) g.crafter = in.crafter; else ++n;
    *out = g;
    return n;
}

inline void GearPutU32(std::vector<char>* b, unsigned int v)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}
inline void GearPutStr(std::vector<char>* b, const std::string& s)
{
    GearPutU32(b, (unsigned int)s.size());
    b->insert(b->end(), s.begin(), s.end());
}
/* Every test is `size - off < n` with off already <= size. */
inline bool GearGetU32(const char* p, size_t size, size_t* off, unsigned int* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool GearGetStr(const char* p, size_t size, size_t* off, unsigned int cap, std::string* s)
{
    unsigned int len = 0;
    if (!GearGetU32(p, size, off, &len)) return false;
    if (len > cap || size - *off < (size_t)len) return false;
    s->assign(p + *off, len);
    *off += len;
    return true;
}
inline bool GearMarkerAt(const char* p, size_t size, size_t at, unsigned int marker)
{
    if (p == 0 || at > size || size - at < 4) return false;
    unsigned int m = 0;
    std::memcpy(&m, p + at, 4);
    return m == marker;
}

/* ---- the per-item block ---- */
/* Appends the block for the cleaned marks; NOTHING when both fields are empty after cleaning. 1 written, 0 nothing. `*dropped`
   (may be 0): the fields GearClean emptied (the caller counts them). */
inline int EncodeGear(std::vector<char>* b, const GearMarks& g, unsigned int* dropped)
{
    GearMarks c;
    const unsigned int n = GearClean(g, &c);
    if (dropped) *dropped = n;
    if (b == 0 || GearEmpty(c)) return 0;
    GearPutU32(b, kGearMarker);
    GearPutStr(b, c.uniformSid);
    GearPutStr(b, c.crafter);
    return 1;
}
/* 1 when the four bytes at `at` are the GEM1 marker. */
inline bool GearBlockAt(const char* p, size_t size, size_t at) { return GearMarkerAt(p, size, at, kGearMarker); }
/* Reads the block at `at`. kGearAbsent: no bytes or not a GEM1 block; kGearBad: the marker is there but the rest is cut or not valid
   (both *out and *end untouched in either case); kGearOk: *out is the marks and *end the offset after the block. */
inline int DecodeGear(const char* p, size_t size, size_t at, GearMarks* out, size_t* end)
{
    if (!GearBlockAt(p, size, at)) return kGearAbsent;
    size_t off = at + 4;
    GearMarks g;
    if (!GearGetStr(p, size, &off, kGearMaxSid, &g.uniformSid) || !GearGetStr(p, size, &off, kGearMaxCrafter, &g.crafter)) return kGearBad;
    if (!GearValid(g)) return kGearBad;
    if (out) *out = g;
    if (end) *end = off;
    return kGearOk;
}
inline const char* GearDecodeWhy(int why)
{
    return why == kGearOk ? "ok" : why == kGearAbsent ? "absent" : "malformed";
}

/* ---- the list tail ---- */
/* How many entries a tail would carry (non-empty after cleaning; none for a list over kGearTailMax). */
inline unsigned int GearTailMarked(const std::vector<GearMarks>& g)
{
    if (g.size() > (size_t)kGearTailMax) return 0;
    unsigned int n = 0;
    GearMarks c;
    for (size_t i = 0; i < g.size(); ++i) { GearClean(g[i], &c); if (!GearEmpty(c)) ++n; }
    return n;
}
/* Appends the tail for the entries of `g` that have marks after cleaning (index = position in the list). NOTHING when none has, or
   when the list is over kGearTailMax (every entry then travels without). Returns the entries written; `*dropped` (may be 0) the
   fields emptied by cleaning. */
inline unsigned int EncodeGearTail(std::vector<char>* b, const std::vector<GearMarks>& g, unsigned int* dropped)
{
    if (dropped) *dropped = 0;
    if (b == 0 || g.size() > (size_t)kGearTailMax) return 0;
    std::vector<GearMarks> c(g.size());
    unsigned int n = 0;
    for (size_t i = 0; i < g.size(); ++i)
    {
        const unsigned int d = GearClean(g[i], &c[i]);
        if (dropped) *dropped += d;
        if (!GearEmpty(c[i])) ++n;
    }
    if (n == 0) return 0;
    GearPutU32(b, kGearTailMarker);
    GearPutU32(b, n);
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (GearEmpty(c[i])) continue;
        GearPutU32(b, (unsigned int)i);
        GearPutStr(b, c[i].uniformSid);
        GearPutStr(b, c[i].crafter);
    }
    return n;
}
/* 1 when the four bytes at `at` are the GEML marker. */
inline bool GearTailAt(const char* p, size_t size, size_t at) { return GearMarkerAt(p, size, at, kGearTailMarker); }
/* Reads the tail at `at` for a list of `count` entries. kGearAbsent: no bytes or not a GEML marker. kGearBad: the marker is there but
   the tail is cut, n is 0 or over the cap or the count, an index is not strictly rising or not below `count`, or an entry is not
   valid. (*out untouched in both.) kGearOk: *out holds `count` entries (empty where the tail names none), *end the offset after. */
inline int DecodeGearTail(const char* p, size_t size, size_t at, size_t count, std::vector<GearMarks>* out, size_t* end)
{
    if (!GearTailAt(p, size, at)) return kGearAbsent;
    size_t off = at + 4;
    unsigned int n = 0;
    if (!GearGetU32(p, size, &off, &n)) return kGearBad;
    if (n == 0 || n > kGearTailMax || (size_t)n > count) return kGearBad;
    std::vector<GearMarks> v(count);
    long long last = -1;
    for (unsigned int k = 0; k < n; ++k)
    {
        unsigned int idx = 0;
        GearMarks g;
        if (!GearGetU32(p, size, &off, &idx)) return kGearBad;
        if (!GearGetStr(p, size, &off, kGearMaxSid, &g.uniformSid) || !GearGetStr(p, size, &off, kGearMaxCrafter, &g.crafter)) return kGearBad;
        if ((long long)idx <= last || (size_t)idx >= count) return kGearBad;
        if (!GearValid(g)) return kGearBad;
        last = (long long)idx;
        v[idx] = g;
    }
    if (out) out->swap(v);
    if (end) *end = off;
    return kGearOk;
}

/* The offset after a GEML tail at `at`, read by its SHAPE only - the marker, n (1..kGearTailMax), and n entries of an index and two
   strings within their bounds - with nothing else checked (index order and range, the text): a reader that refuses the tail can
   still find what follows it. false (*end untouched) when the shape itself is cut or out of range. */
inline bool GearTailSkip(const char* p, size_t size, size_t at, size_t* end)
{
    if (!GearTailAt(p, size, at)) return false;
    size_t off = at + 4;
    unsigned int n = 0;
    if (!GearGetU32(p, size, &off, &n) || n == 0 || n > kGearTailMax) return false;
    std::string s;
    for (unsigned int k = 0; k < n; ++k)
    {
        unsigned int idx = 0;
        if (!GearGetU32(p, size, &off, &idx) || !GearGetStr(p, size, &off, kGearMaxSid, &s) || !GearGetStr(p, size, &off, kGearMaxCrafter, &s))
            return false;
    }
    if (end) *end = off;
    return true;
}

/* ---- the offline box-move journal's "b3" fields ---- */
/* The five fields after the "b1" ones: kind, uid, sid (a clean owner is "0", "0", ""), uniform, crafter. `g` is already cleaned. */
inline std::string GearJournalTail(const coopmark::OwnerId& o, const GearMarks& g)
{
    return coopmark::OwnerJournalTail(o) + "\t" + g.uniformSid + "\t" + g.crafter;
}
/* The five fields back. Kind 0 must come with uid 0 and no sid; any other kind is read as a "b2" owner. The marks must be valid (a
   "b3" line is written only with marks). */
inline bool GearFromJournal(const std::string& kindF, const std::string& uidF, const std::string& sidF, const std::string& uniF,
                            const std::string& crF, coopmark::OwnerId* owner, GearMarks* gear)
{
    coopmark::OwnerId o;
    if (kindF == "0") { if (uidF != "0" || !sidF.empty()) return false; }
    else if (!coopmark::OwnerFromJournal(kindF, uidF, sidF, &o)) return false;
    GearMarks g;
    g.uniformSid = uniF; g.crafter = crF;
    if (!GearValid(g)) return false;
    if (owner) *owner = o;
    if (gear) *gear = g;
    return true;
}

/* The marks as a print shows them: uniform=<sid|-> crafter='<text>' */
inline std::string GearWords(const GearMarks& g)
{
    return "uniform=" + (g.uniformSid.empty() ? std::string("-") : g.uniformSid) + " crafter='" + g.crafter + "'";
}

}   /* namespace gearwire */
