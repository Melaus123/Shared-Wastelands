/* src/common/bagwire.h - inv3a (investigations/inv3-backpack-design.md D5, phase 1): WHAT IS INSIDE A BACKPACK, AS ROWS.
 *
 * A backpack (ContainerItem) is an item with its own small inventory at +0x290. When a pack is handed from one player to the
 * other (the E22b/E22c request pair), the receiving game builds a NEW pack from the item's record fields - and until this
 * block that new pack arrived EMPTY while the original, with everything in it, was destroyed. The pack's contents now ride
 * the same message as the pack, as a trailing block:
 *
 *   marker u32 'BAG1' | n u32 (<= kBagMaxRows) | n x row
 *   row: str section | u32 x | u32 y | u32 qty | str base | str company | str material | str color |
 *        f32 quality | f32 charges | i32 function | i32 level | u8 unique              (str = u32 len + bytes)
 *
 * inv6 phase 2 (protocol 73): an optional 'OWNL' owner tail (ownerwire.h) follows the rows, naming the marked rows by index - absent
 * when none is marked; the pack's own OWN1 block (if any) comes after it. BagRowsDigest leaves the marks out.
 *
 * It rides MSG_ITEM_REQUEST (a GIVE: the giver's pack) and MSG_ITEM_CONFIRM (a TAKE: the owner's pack), after the trade
 * block, and only when the pack holds something. THE CAP IS A REFUSAL, NEVER A TRUNCATION: an encoder handed more than
 * kBagMaxRows rows, or a row out of range, writes nothing and says so, and a decoder that meets one refuses the block - a
 * pack delivered with part of its contents is exactly the item loss this block exists to end. Pure: no engine memory, no
 * Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ownerwire.h"   /* inv6 phase 2: the rows' owner tail */

namespace coopbag {

const unsigned int kBagMarker = 0x31474142u;    /* 'B','A','G','1' little-endian */
const unsigned int kBagMaxRows = 256u;          /* D5: 256 rows, about 38 KB at worst */
const unsigned int kBagMaxSection = 63u;        /* the plugin's section names are < 32 (kSecCap) */
const unsigned int kBagMaxSid = 95u;            /* the plugin's sids are < 48 (kSidCap) */
const unsigned int kBagMaxQty = 1000000u;
const unsigned int kBagMaxCell = 4096u;

const int kBagOk      = 0;
const int kBagAbsent  = 1;   /* nothing at all after the message's last fixed field */
const int kBagBad     = 2;   /* bytes are there but they are not a whole, in-range BAG1 block (wrong marker, cut, a bad row) */
const int kBagOverCap = 3;   /* the count says more than kBagMaxRows */
const int kBagBadOwner = 4;  /* inv6 phase 2: the rows are whole but the OWNL tail after them is cut or out of range */

struct BagRow
{
    std::string section;       /* the INNER section, e.g. "backpack_content" */
    unsigned int x, y;         /* the cell inside it */
    unsigned int qty;
    std::string baseSid, companySid, materialSid, colorSid;
    float quality, charges;
    int functionKind, level;
    unsigned char unique;
    coopmark::OwnerId owner;   /* inv6 phase 2 (R5): the row item's stolen mark - in the OWNL tail, not in the row */
    BagRow() : x(0), y(0), qty(1), quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0) {}
};

inline bool BagFloatOk(float f)
{
    unsigned int b = 0;
    std::memcpy(&b, &f, 4);
    return (b & 0x7F800000u) != 0x7F800000u;   /* not NaN, not infinite (by the bits: /fp:fast) */
}

/* The one aliveStamp rule both ends apply. */
inline bool BagRowValid(const BagRow& r)
{
    if (r.section.empty() || r.section.size() > kBagMaxSection) return false;
    if (r.baseSid.empty() || r.baseSid.size() > kBagMaxSid) return false;
    if (r.companySid.size() > kBagMaxSid || r.materialSid.size() > kBagMaxSid || r.colorSid.size() > kBagMaxSid) return false;
    if (r.x >= kBagMaxCell || r.y >= kBagMaxCell) return false;
    if (r.qty < 1 || r.qty > kBagMaxQty) return false;
    if (!BagFloatOk(r.quality) || !BagFloatOk(r.charges)) return false;
    return true;
}

inline void BagPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void BagPutStr(std::vector<char>* b, const std::string& s)
{
    BagPutU32(b, (unsigned int)s.size());
    if (!s.empty()) b->insert(b->end(), s.begin(), s.end());
}

/* Appends the block. false = NOTHING WAS WRITTEN: more than kBagMaxRows rows, or a row BagRowValid refuses. */
inline bool EncodeBagRows(std::vector<char>* b, const std::vector<BagRow>& rows)
{
    if (b == 0 || rows.size() > kBagMaxRows) return false;
    for (size_t i = 0; i < rows.size(); ++i) if (!BagRowValid(rows[i])) return false;
    BagPutU32(b, kBagMarker);
    BagPutU32(b, (unsigned int)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const BagRow& r = rows[i];
        BagPutStr(b, r.section);
        BagPutU32(b, r.x); BagPutU32(b, r.y); BagPutU32(b, r.qty);
        BagPutStr(b, r.baseSid); BagPutStr(b, r.companySid); BagPutStr(b, r.materialSid); BagPutStr(b, r.colorSid);
        const size_t at = b->size();
        b->resize(at + 17);
        std::memcpy(&(*b)[at], &r.quality, 4);
        std::memcpy(&(*b)[at + 4], &r.charges, 4);
        std::memcpy(&(*b)[at + 8], &r.functionKind, 4);
        std::memcpy(&(*b)[at + 12], &r.level, 4);
        (*b)[at + 16] = (char)(r.unique ? 1 : 0);
    }
    std::vector<coopmark::OwnerId> own(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) own[i] = rows[i].owner;
    coopmark::EncodeOwnerTail(b, own, 0);   /* inv6 phase 2: nothing when no row is marked */
    return true;
}

/* Every test is `size - off < n` with off already <= size. */
inline bool BagGetU32(const char* p, size_t size, size_t* off, unsigned int* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool BagGetStr(const char* p, size_t size, size_t* off, unsigned int cap, std::string* s)
{
    unsigned int len = 0;
    if (!BagGetU32(p, size, off, &len)) return false;
    if (len > cap || size - *off < (size_t)len) return false;
    s->assign(p + *off, len);
    *off += len;
    return true;
}

/* Reads the block at `at`. On kBagOk `*end` is the offset just after it and `*out` holds the rows; on anything else `*out`
   is untouched. kBagAbsent only when there is not one byte left - a peer that speaks this protocol and has nothing to say
   sends nothing, so bytes that are not a good block are refused rather than read as "no contents". */
inline int DecodeBagRows(const char* p, size_t size, size_t at, std::vector<BagRow>* out, size_t* end)
{
    if (p == 0 || at >= size) return kBagAbsent;
    size_t off = at;
    unsigned int marker = 0, n = 0;
    if (!BagGetU32(p, size, &off, &marker) || marker != kBagMarker) return kBagBad;
    if (!BagGetU32(p, size, &off, &n)) return kBagBad;
    if (n > kBagMaxRows) return kBagOverCap;
    std::vector<BagRow> rows;
    for (unsigned int i = 0; i < n; ++i)
    {
        BagRow r;
        if (!BagGetStr(p, size, &off, kBagMaxSection, &r.section)) return kBagBad;
        if (!BagGetU32(p, size, &off, &r.x) || !BagGetU32(p, size, &off, &r.y) || !BagGetU32(p, size, &off, &r.qty)) return kBagBad;
        if (!BagGetStr(p, size, &off, kBagMaxSid, &r.baseSid) || !BagGetStr(p, size, &off, kBagMaxSid, &r.companySid)
            || !BagGetStr(p, size, &off, kBagMaxSid, &r.materialSid) || !BagGetStr(p, size, &off, kBagMaxSid, &r.colorSid))
            return kBagBad;
        if (off > size || size - off < 17) return kBagBad;
        std::memcpy(&r.quality, p + off, 4);
        std::memcpy(&r.charges, p + off + 4, 4);
        std::memcpy(&r.functionKind, p + off + 8, 4);
        std::memcpy(&r.level, p + off + 12, 4);
        r.unique = (unsigned char)(p[off + 16] ? 1 : 0);
        off += 17;
        if (!BagRowValid(r)) return kBagBad;
        rows.push_back(r);
    }
    if (off < size && coopmark::OwnerTailAt(p, size, off))   /* inv6 phase 2: the rows' owner tail */
    {
        std::vector<coopmark::OwnerId> own; size_t oe = 0;
        if (n == 0 || coopmark::DecodeOwnerTail(p, size, off, rows.size(), &own, &oe) != coopmark::kOwnOk) return kBagBadOwner;
        for (size_t i = 0; i < own.size(); ++i) rows[i].owner = own[i];
        off = oe;
    }
    if (out) out->swap(rows);
    if (end) *end = off;
    return kBagOk;
}

inline const char* BagDecodeWhy(int why)
{
    return why == kBagOk ? "ok" : why == kBagAbsent ? "absent" : why == kBagOverCap ? "over the 256-row cap" : why == kBagBadOwner ? "carrying a malformed owner tail" : "malformed";
}

/* inv6 phase 2: how many rows would travel marked (the OWNL tail's entry count). */
inline unsigned int BagRowsMarked(const std::vector<BagRow>& rows)
{
    std::vector<coopmark::OwnerId> own(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) own[i] = rows[i].owner;
    return coopmark::OwnerTailMarked(own);
}
/* review-inv6p2 LOW: how many of those travel as "owner unknown" (ownerSenderUnknown). */
inline unsigned int BagRowsUnknown(const std::vector<BagRow>& rows)
{
    std::vector<coopmark::OwnerId> own(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) own[i] = rows[i].owner;
    return coopmark::OwnerTailUnknown(own);
}

/* `bagtest list`'s digest: ORDER-FREE (the rows are sorted first), over section, cell, quantity, the four sids and (give1)
   quality, charges (their bit patterns), function, level and unique - every field a handed-over pack must keep. FNV-1a 32.
   Two lists with the same rows in any order give the same number. It is computed only on the GIVING game (at send and at
   confirm, compared there) and printed by `bagtest list`; it never travels, so no two games have to agree on it in code. */
inline unsigned int BagRowsDigest(const std::vector<BagRow>& rows)
{
    std::vector<std::string> keys;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const BagRow& r = rows[i];
        char num[48];
        std::sprintf(num, "|%u|%u|%u|", r.x, r.y, r.qty);
        unsigned int qb = 0, cb = 0;
        std::memcpy(&qb, &r.quality, 4);
        std::memcpy(&cb, &r.charges, 4);
        char more[96];
        std::sprintf(more, "|%08x|%08x|%d|%d|%u", qb, cb, r.functionKind, r.level, (unsigned int)r.unique);
        keys.push_back(r.section + num + r.baseSid + "|" + r.companySid + "|" + r.materialSid + "|" + r.colorSid + more);
    }
    std::sort(keys.begin(), keys.end());
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        for (size_t k = 0; k < keys[i].size(); ++k) { h ^= (unsigned char)keys[i][k]; h *= 16777619u; }
        h ^= 0x0Au; h *= 16777619u;
    }
    return h;
}

/* review-inv3a 1b: a GIVE of a pack is completed (the giver removes its own pack) only when the object at the recorded slot
   is STILL THE SAME OBJECT, the slot could be read, and its contents' digest still equals the digest of the rows that were
   SENT. Anything else cancels the give: the pack stays with the giver and the receiver is told to discard its copy. */
inline bool BagGiveUnchanged(bool readOk, bool sameObject, bool rowsReadOk, unsigned int sentDigest, unsigned int nowDigest)
{
    return readOk && sameObject && rowsReadOk && sentDigest == nowDigest;
}

/* review-inv3a 1c/1d: ALL OR NOTHING. A new pack is kept only when EVERY row was created AND placed inside it (its exact
   cell or anywhere in the pack). One row short and the whole handover is refused - nothing is spilled beside the pack and
   nothing is ever counted lost, because the original pack stays whole with its owner. */
inline bool BagFillComplete(size_t rows, size_t created, size_t placedInPack)
{
    return created == rows && placedInPack == rows;
}

/* bag23 part 1 (inv3 phase 2, investigations/inv3-backpack-design.md D3(b)/D5; protocol 81): THE KIT FORM. A character's top-level
   packs (worn in backpack_attach, or in any other section of its own inventory) with what is inside them, so a copy spawned or
   re-kitted gets its packs FULL. It rides MSG_CLOTHING LAST - after the item list, and after the QLT1 details block and that
   block's OWNL tail:
     marker u32 'BAGK' | packs u32 (1..the kit's item count) | packs x { u32 kitIndex (< the kit's item count, rising) | BAG1 block }
   Each BAG1 block is the phase-1 block above, its OWNL tail included, so a pack's stolen rows travel marked. Only packs that hold
   something are listed, and nothing at all is written when none does. An older reader stops before it (DeserialiseGarments
   ignores trailing bytes). THE 256-ROW CAP STAYS A REFUSAL PER PACK: a pack over it is not listed (the capture leaves it out and
   counts it) - its copy is empty, never partly filled. The pack count is bounded by the kit's own item count, nothing else. */
const unsigned int kKitBagMarker = 0x4B474142u;   /* 'B','A','G','K' little-endian */

const int kKitBagOk     = 0;
const int kKitBagAbsent = 1;   /* not one byte left after the details block */
const int kKitBagBad    = 2;   /* bytes are there but they are not a whole, in-range kit block: the reader drops it WHOLE */

struct KitBag
{
    unsigned int kitIndex;       /* which kit item (GarmentSet index) is the pack */
    std::vector<BagRow> rows;    /* what is inside it - never empty on the wire */
    KitBag() : kitIndex(0) {}
};

/* Appends the block. true with nothing written when `bags` is empty. false = NOTHING WAS WRITTEN: more packs than kit items, an
   index out of range or not rising, an empty pack, or a pack EncodeBagRows refuses (over the cap, a bad row). */
inline bool EncodeKitBags(std::vector<char>* b, const std::vector<KitBag>& bags, size_t kitCount)
{
    if (b == 0) return false;
    if (bags.empty()) return true;
    if (bags.size() > kitCount) return false;
    std::vector<char> t;
    BagPutU32(&t, kKitBagMarker);
    BagPutU32(&t, (unsigned int)bags.size());
    for (size_t i = 0; i < bags.size(); ++i)
    {
        if ((size_t)bags[i].kitIndex >= kitCount) return false;
        if (i > 0 && bags[i].kitIndex <= bags[i - 1].kitIndex) return false;
        if (bags[i].rows.empty()) return false;
        BagPutU32(&t, bags[i].kitIndex);
        if (!EncodeBagRows(&t, bags[i].rows)) return false;
    }
    b->insert(b->end(), t.begin(), t.end());
    return true;
}

/* Reads the block at `at` for a kit of `kitCount` items. On kKitBagOk `*end` is the offset just after it and `*out` holds the
   packs; on anything else `*out` is untouched and `*rowWhy` says what the failing BAG1 block answered (kBagOk when the fault is
   in the kit wrapper itself). */
inline int DecodeKitBags(const char* p, size_t size, size_t at, size_t kitCount, std::vector<KitBag>* out, size_t* end, int* rowWhy)
{
    if (rowWhy) *rowWhy = kBagOk;
    if (p == 0 || at >= size) return kKitBagAbsent;
    size_t off = at;
    unsigned int marker = 0, n = 0;
    if (!BagGetU32(p, size, &off, &marker) || marker != kKitBagMarker) return kKitBagBad;
    if (!BagGetU32(p, size, &off, &n) || n == 0 || (size_t)n > kitCount) return kKitBagBad;
    std::vector<KitBag> bags;
    for (unsigned int i = 0; i < n; ++i)
    {
        KitBag kb;
        if (!BagGetU32(p, size, &off, &kb.kitIndex) || (size_t)kb.kitIndex >= kitCount) return kKitBagBad;
        if (i > 0 && kb.kitIndex <= bags.back().kitIndex) return kKitBagBad;
        size_t e = 0;
        const int r = DecodeBagRows(p, size, off, &kb.rows, &e);
        if (r != kBagOk) { if (rowWhy) *rowWhy = r; return kKitBagBad; }
        if (kb.rows.empty()) return kKitBagBad;
        off = e;
        bags.push_back(kb);
    }
    if (out) out->swap(bags);
    if (end) *end = off;
    return kKitBagOk;
}

/* bag23 part 2 (inv3 phase 3, investigations/inv3-backpack-design.md D2 / D3(a); protocol 81): THE PATH TO A PACK. A move INSIDE a
   pack names the pack by where it sits - the parent's (a character uid or a box key, carried by the message itself) section and
   cell - and the message's own section / x / y name the cell inside the pack. It rides LAST on MSG_ITEM_MOVE (after everything
   else: op 0 after its BAG1 / OWN1 tail, op 2 after its base sid) and on MSG_ITEM_REQUEST (after its OWN1 block):
     marker u32 'BAGP' | str section (1..kBagMaxSection) | u32 x | u32 y (each < kBagMaxCell) | str sid (0..kBagMaxSid)
   `sid` is the record at the inner cell when the sender knows it (a request's TAKE: S2-66's same-record search); empty on a move.
   Absent = the move is not inside a pack. Cut or out of range: the reader refuses the message - never a guess at a path. */
const unsigned int kBagPathMarker = 0x50474142u;   /* 'B','A','G','P' little-endian */

struct BagPath
{
    int has;                   /* 0 = no path (nothing on the wire) */
    std::string section;       /* the PARENT's section the pack sits in, e.g. "backpack_attach" */
    unsigned int x, y;         /* the pack's cell there */
    std::string sid;           /* the record at the inner cell (a request only; may be empty) */
    BagPath() : has(0), x(0), y(0) {}
};

inline bool BagPathAt(const char* p, size_t size, size_t at)
{
    size_t o = at;
    unsigned int mk = 0;
    return p != 0 && at < size && BagGetU32(p, size, &o, &mk) && mk == kBagPathMarker;
}

/* true with nothing written when `p.has` is 0. false = NOTHING WAS WRITTEN (an empty or long section, a long sid, a cell out of range). */
inline bool EncodeBagPath(std::vector<char>* b, const BagPath& p)
{
    if (b == 0) return false;
    if (p.has == 0) return true;
    if (p.section.empty() || p.section.size() > kBagMaxSection || p.sid.size() > kBagMaxSid || p.x >= kBagMaxCell || p.y >= kBagMaxCell)
        return false;
    BagPutU32(b, kBagPathMarker);
    BagPutStr(b, p.section);
    BagPutU32(b, p.x);
    BagPutU32(b, p.y);
    BagPutStr(b, p.sid);
    return true;
}

/* kBagOk (`*out` has = 1, `*end` just after the block), kBagAbsent (no byte at `at`), kBagBad (a wrong marker, cut, out of range). */
inline int DecodeBagPath(const char* p, size_t size, size_t at, BagPath* out, size_t* end)
{
    if (p == 0 || at >= size) return kBagAbsent;
    size_t off = at;
    unsigned int mk = 0;
    BagPath r;
    if (!BagGetU32(p, size, &off, &mk) || mk != kBagPathMarker) return kBagBad;
    if (!BagGetStr(p, size, &off, kBagMaxSection, &r.section) || r.section.empty()) return kBagBad;
    if (!BagGetU32(p, size, &off, &r.x) || !BagGetU32(p, size, &off, &r.y) || r.x >= kBagMaxCell || r.y >= kBagMaxCell) return kBagBad;
    if (!BagGetStr(p, size, &off, kBagMaxSid, &r.sid)) return kBagBad;
    r.has = 1;
    if (out) *out = r;
    if (end) *end = off;
    return kBagOk;
}

} // namespace coopbag
