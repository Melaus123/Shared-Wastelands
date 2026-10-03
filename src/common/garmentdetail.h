/* src/common/garmentdetail.h - quality1 (investigations/items-loot-quality.md 1; F907): THE PER-ITEM DETAILS OF A COPY'S KIT.
 *
 * MSG_CLOTHING (clothing.cpp, sent once when a copy settles) rebuilt the copy's whole inventory from five fields per item -
 * section, base sid, company sid, material sid, level - so every item on a copy had the factory's default quality,
 * charges and colour and a stack size of 1 (Read, clothing.cpp; F530(i)). The live item moves (ITEM_MOVE / REQUEST /
 * CONFIRM) already carry these fields. This block rides AFTER the existing item list, one entry per item in the same
 * order:
 *
 *   marker u32 'QLT1' | per item { quality f32 | charges f32 | quantity u32 (1..kGarmentMaxQuantity) |
 *                                  functionKind i32 | unique u8 | len u32 (0..kGarmentMaxColor) + color sid }
 *
 * inv6 phase 2 (protocol 73): an optional 'OWNL' owner tail (ownerwire.h) follows the entries, naming the marked items by index.
 * A reader that predates it stops after the list (nothing followed the list before), so the block is optional on the
 * wire; the session protocol still moves with it (mixed pairs refused, as for every behaviour change). Pure: no engine
 * memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>
#include "ownerwire.h"   /* inv6 phase 2: the kit's owner tail */

namespace coopgarment {

const unsigned int kGarmentDetailMarker = 0x31544C51u;   /* 'Q','L','T','1' little-endian */
const unsigned int kGarmentMaxQuantity = 1000000u;
const unsigned int kGarmentMaxColor = 96u;

const size_t kEntryFixed = 21;   /* quality 4 + charges 4 + quantity 4 + functionKind 4 + unique 1 + len 4 */

const int kDetailOk      = 0;
const int kDetailAbsent  = 1;   /* nothing after the list, or a different marker: an older sender */
const int kDetailBadOwner = 3;  /* inv6 phase 2: the entries are whole but the OWNL tail after them is cut or out of range - the
                                   details are KEPT (the quality1 rule protects them), only the marks are dropped */
const int kDetailBad     = 2;   /* the marker is there but an entry is cut short or out of range - the block is refused */

struct GarmentDetail
{
    float quality;
    float charges;
    unsigned int quantity;
    int functionKind;          /* Item +0x124 (review-quality1 3: ItCreateItem writes it too) */
    unsigned char unique;      /* Item +0x12A */
    std::string colorSid;
    coopmark::OwnerId owner;   /* inv6 phase 2 (R6): the item's stolen mark - in the OWNL tail, not in the entry */
    GarmentDetail() : quality(0.0f), charges(0.0f), quantity(1), functionKind(0), unique(0), colorSid() {}
};

inline bool DetailFloatOk(float f)
{
    unsigned int b = 0;
    std::memcpy(&b, &f, 4);
    return (b & 0x7F800000u) != 0x7F800000u;   /* not NaN, not infinite (by the bits: /fp:fast) */
}

/* Appends the block. An entry out of range is written clamped (quantity 1.., colour cut to kGarmentMaxColor, a non-finite
   float as 0), so the block always decodes. */
inline void EncodeGarmentDetails(std::vector<char>* b, const std::vector<GarmentDetail>& d)
{
    if (b == 0) return;
    const size_t at0 = b->size();
    b->resize(at0 + 4);
    std::memcpy(&(*b)[at0], &kGarmentDetailMarker, 4);
    for (size_t i = 0; i < d.size(); ++i)
    {
        const float q = DetailFloatOk(d[i].quality) ? d[i].quality : 0.0f;
        const float c = DetailFloatOk(d[i].charges) ? d[i].charges : 0.0f;
        unsigned int n = d[i].quantity;
        if (n < 1) n = 1;
        if (n > kGarmentMaxQuantity) n = kGarmentMaxQuantity;
        const std::string col = d[i].colorSid.size() > kGarmentMaxColor ? d[i].colorSid.substr(0, kGarmentMaxColor) : d[i].colorSid;
        const unsigned int len = (unsigned int)col.size();
        const size_t at = b->size();
        b->resize(at + kEntryFixed + len);
        std::memcpy(&(*b)[at], &q, 4);
        std::memcpy(&(*b)[at + 4], &c, 4);
        std::memcpy(&(*b)[at + 8], &n, 4);
        std::memcpy(&(*b)[at + 12], &d[i].functionKind, 4);
        (*b)[at + 16] = (char)(d[i].unique ? 1 : 0);
        std::memcpy(&(*b)[at + 17], &len, 4);
        if (len > 0) std::memcpy(&(*b)[at + kEntryFixed], col.data(), len);
    }
    std::vector<coopmark::OwnerId> own(d.size());
    for (size_t i = 0; i < d.size(); ++i) own[i] = d[i].owner;
    coopmark::EncodeOwnerTail(b, own, 0);   /* inv6 phase 2 (R6): nothing when no item is marked */
}

/* Reads the block at `at` for `count` items. Every test is `size - off < n` with off already <= size.
   bag23 part 1 (inv3 phase 2): `*end` is the offset just after the block AND its OWNL tail on kDetailOk, and `at` itself on
   kDetailAbsent (the kit's packs block then follows the item list directly); on kDetailBad / kDetailBadOwner the end is not
   known and `*end` stays `at` - a caller must not look for anything after a block it could not read. */
inline int DecodeGarmentDetailsEnd(const char* p, size_t size, size_t at, size_t count, std::vector<GarmentDetail>* out, size_t* end)
{
    if (end) *end = at;
    if (p == 0 || at > size || size - at < 4) return kDetailAbsent;
    unsigned int marker = 0;
    std::memcpy(&marker, p + at, 4);
    if (marker != kGarmentDetailMarker) return kDetailAbsent;
    size_t off = at + 4;
    std::vector<GarmentDetail> d;
    for (size_t i = 0; i < count; ++i)
    {
        if (size - off < kEntryFixed) return kDetailBad;
        GarmentDetail g;
        unsigned int len = 0;
        std::memcpy(&g.quality, p + off, 4);
        std::memcpy(&g.charges, p + off + 4, 4);
        std::memcpy(&g.quantity, p + off + 8, 4);
        std::memcpy(&g.functionKind, p + off + 12, 4);
        g.unique = (unsigned char)(p[off + 16] ? 1 : 0);
        std::memcpy(&len, p + off + 17, 4);
        off += kEntryFixed;
        if (!DetailFloatOk(g.quality) || !DetailFloatOk(g.charges)) return kDetailBad;
        if (g.quantity < 1 || g.quantity > kGarmentMaxQuantity) return kDetailBad;
        if (len > kGarmentMaxColor || size - off < (size_t)len) return kDetailBad;
        g.colorSid.assign(p + off, len);
        off += len;
        d.push_back(g);
    }
    if (count != 0 && off < size && coopmark::OwnerTailAt(p, size, off))   /* inv6 phase 2 (R6): the owner tail */
    {
        std::vector<coopmark::OwnerId> own; size_t oe = 0;
        if (coopmark::DecodeOwnerTail(p, size, off, d.size(), &own, &oe) != coopmark::kOwnOk) { if (out) out->swap(d); return kDetailBadOwner; }
        for (size_t i = 0; i < own.size(); ++i) d[i].owner = own[i];
        off = oe;
    }
    if (out) out->swap(d);
    if (end) *end = off;
    return kDetailOk;
}

/* The original reader (no end offset), unchanged in what it answers. */
inline int DecodeGarmentDetails(const char* p, size_t size, size_t at, size_t count, std::vector<GarmentDetail>* out)
{
    size_t e = 0;
    return DecodeGarmentDetailsEnd(p, size, at, count, out, &e);
}

/* inv6 phase 2: how many entries would travel marked (the OWNL tail's entry count). */
inline unsigned int GarmentMarked(const std::vector<GarmentDetail>& d)
{
    std::vector<coopmark::OwnerId> own(d.size());
    for (size_t i = 0; i < d.size(); ++i) own[i] = d[i].owner;
    return coopmark::OwnerTailMarked(own);
}
/* review-inv6p2 LOW: how many of those travel as "owner unknown" (ownerSenderUnknown). */
inline unsigned int GarmentUnknown(const std::vector<GarmentDetail>& d)
{
    std::vector<coopmark::OwnerId> own(d.size());
    for (size_t i = 0; i < d.size(); ++i) own[i] = d[i].owner;
    return coopmark::OwnerTailUnknown(own);
}

} // namespace coopgarment
