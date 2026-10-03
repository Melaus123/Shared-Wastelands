/* src/common/paritywire.h - par1 (docs/design-loot2.md rev 2, section 0.4 "Step B - pull"; T345: the two games' indoor boxes
 * held different loot - Scraphouse A 513 items / B 23). Box parity, PULL half: the first time a game that does NOT hold an area
 * sees its boxes, it sends MSG_PARITY_REQ (53) with one {position key, digest, item count} row per box; the holder answers one
 * MSG_PARITY_BOX (54) per box - EQUAL, NOT FOUND, NOT HOLDER, UNREADABLE, or the box's whole contents (the ITEM_MOVE add fields
 * plus section / x / y / quantity), which the asker swaps in for its own. Both reliable.
 *
 *   PARITY_REQ  i32 sx | i32 sy | u16 n (<= kParityMaxBoxes) | n x (u8 len + key, u32 digest, u32 itemCount)
 *               | ['PRQK' u8 kind - absent for a plain ask]
 *               | ['PRQG' u16 m (<= kParityMaxGroundKeys) | u16 left | m x (u8 len + ground key, i32 quantity, i32 quality in
 *                  hundredths, u8 road) - the asker's ground items in the sector, for the holder's catch-up (ParityGroundPlan);
 *                  road 1 = an item the ground road put down there, 0 = any other; left = items the asker left out at the cap
 *                  (the holder then sends no ADD); absent = not a ground ask, m may be 0]
 *   PARITY_BOX  u8 status | u8 len + key | u32 holder digest | u16 n (<= kParityMaxItems; 0 unless status CONTENTS)
 *               | n x (u8 len + section, i32 x, i32 y, i32 quantity, u8 len + base sid, u8 len + company sid,
 *                      u8 len + material sid, u8 len + color sid, f32 quality, f32 charges, i32 item function,
 *                      i32 level, u8 unique)
 *               | [inv6 phase 2, protocol 73: an 'OWNL' owner tail (ownerwire.h) naming the marked items by index; absent when none is]
 *
 * THE DIGEST (section 0.4): an ORDER-FREE hash - the 32-bit SUM of one hash per item over (section, x, y, base sid, quantity,
 * quality to 0.01), the fields ITEM_MOVE already carries. An empty box is 0. Both games compute it with ParityItemHash, so the
 * digest of the same contents is the same number on both.
 * THE STOLEN MARK IS NOT IN THE DIGEST (inv6 phase 2 decision): an owner difference alone never makes a box read different, so a
 * box whose items differ only by their marks is answered EQUAL and the asker keeps its own marks (phase 3 may add OwnerDigestTerm).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "ownerwire.h"   /* inv6 phase 2: the owner tail after the items */
#include "bagwire.h"    /* bag23 part 2: packs' contents after the owner tail (the BAGK form) */
#include "groundkey.h"  /* the ground listing's keys, paired by ParityGroundPlan */

namespace cooppar {

const size_t       kParityStrMax   = 255;    /* every string rides behind a u8 length */
const unsigned int kParityMaxBoxes = 1024;   /* rows in one REQ; the sender splits a larger sector */
const unsigned int kParityMaxItems = 1024;   /* items in one BOX; a larger box is answered UNREADABLE */
const unsigned int kParityMaxGroundKeys = 256;   /* ground keys in one REQ's PRQG listing; the asker leaves the rest out (counted) */

const int kParityStatusContents   = 0;   /* the digests differ: the holder's whole contents follow */
const int kParityStatusEqual      = 1;   /* the holder's digest and count match the asker's */
const int kParityStatusNotFound   = 2;   /* the holder could not resolve the key: the asker keeps its own */
const int kParityStatusNotHolder  = 3;   /* the answering game does not hold that box's area */
const int kParityStatusUnreadable = 4;   /* an item could not be read, or the box is over kParityMaxItems */
const int kParityStatusMax        = 4;
/* P109 (game-to-game protocol 116): WHAT AN ASK IS FOR, carried by an optional PRQK tail on MSG_PARITY_REQ (absent = plain,
   so a plain ask is byte for byte the 113 ask). The decoder stamps the ask's kind on every row it returns. */
const int kParityAskPlain = 0;   /* an arrival listing or a re-ask: only the holder answers it */
const int kParityAskOpen  = 1;   /* the box is on screen at the asker: the holder may lift research items first (R5) */
const int kParityAskPull  = 2;   /* the asker HOLDS the box but never had its contents: the previous holder answers it (R1/R4) */

const int kParityDecodeOk        = 0;
const int kParityDecodeTooShort  = 1;   /* the bytes end inside a field */
const int kParityDecodeBadStatus = 2;
const int kParityDecodeTooMany   = 3;   /* n above the cap */
const int kParityDecodeTrailing  = 4;   /* bytes left over after the last field */
const int kParityDecodeBadKey    = 5;   /* an empty key */
const int kParityDecodeBadOwner  = 6;   /* inv6 phase 2: an OWNL owner tail that is there but cut or out of range */
const int kParityDecodeBadKind   = 8;   /* P109: a PRQK tail naming no known kind */
const int kParityDecodeBadBag    = 7;   /* bag23 part 2: a BAGK packs block that is there but cut, out of range or naming no item */
const int kParityDecodeBadFlag   = 9;   /* a PRQG listing row whose road flag is neither 0 nor 1 */

struct ParityBoxRow { std::string key; unsigned int digest; unsigned int items; int kind; ParityBoxRow() : digest(0), items(0), kind(0) {} };   /* P109: kind is not on the wire per row - the decoder stamps the ask's */
/* one ground item in a sector (a `G|` key): a row of the asker's listing, or one of the holder's own items for ParityGroundPlan.
   q100 = quality in hundredths (coopground::GroundQ100); road 1 = an item the ground road put down there, 0 = the world's own
   loot or anything else the game cannot show came by the road. */
struct ParityGroundRow { std::string key; int qty; int q100; int road; ParityGroundRow() : qty(0), q100(0), road(0) {} };
struct ParityReq
{
    int sx, sy; int kind; std::vector<ParityBoxRow> boxes;   /* P109: kind = kParityAsk* (the PRQK tail) */
    int ground; std::vector<ParityGroundRow> groundKeys;     /* 1 = a ground ask (the PRQG listing, which may hold no key) */
    unsigned int groundLeft;                                 /* ground items the asker left out of the listing at its cap */
    ParityReq() : sx(0), sy(0), kind(0), ground(0), groundLeft(0) {}
};
struct ParityItem
{
    std::string section; int x, y, quantity;
    std::string baseSid, companySid, materialSid, colorSid;
    float quality, charges; int functionKind, level, unique;
    coopmark::OwnerId owner;   /* inv6 phase 2 (R4): the stolen mark - in the OWNL tail, NOT in the digest */
    std::vector<coopbag::BagRow> bag;   /* bag23 part 2: a pack's contents - in the trailing BAGK block, NOT in the digest; empty = none */
    ParityItem() : x(0), y(0), quantity(0), quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0) {}
};
struct ParityBox { int status; std::string key; unsigned int digest; std::vector<ParityItem> items; ParityBox() : status(0), digest(0) {} };

/* Quality to 0.01, as an integer both games round the same way. A NaN or an absurd value reads as 0. */
inline int ParityQuality100(float q)
{
    if (!(q == q) || q > 1.0e6f || q < -1.0e6f) return 0;
    return (int)std::floor((double)q * 100.0 + 0.5);
}
inline unsigned int ParityFnv(unsigned int h, const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}
/* One item's term. The final mix spreads the FNV state so that a SUM of terms does not cancel on near-equal inputs. */
inline unsigned int ParityItemHash(const std::string& section, int x, int y, const std::string& baseSid, int quantity, float quality)
{
    unsigned int h = 2166136261u;
    const char bar = '|';
    const int q100 = ParityQuality100(quality);
    h = ParityFnv(h, section.data(), section.size()); h = ParityFnv(h, &bar, 1);
    h = ParityFnv(h, &x, 4); h = ParityFnv(h, &y, 4);
    h = ParityFnv(h, baseSid.data(), baseSid.size()); h = ParityFnv(h, &bar, 1);
    h = ParityFnv(h, &quantity, 4); h = ParityFnv(h, &q100, 4);
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
/* inv6 phase 2: the items' marks as one list, index for index (the owner tail's input). */
inline std::vector<coopmark::OwnerId> ParityOwners(const std::vector<ParityItem>& items)
{
    std::vector<coopmark::OwnerId> o(items.size());
    for (size_t i = 0; i < items.size(); ++i) o[i] = items[i].owner;
    return o;
}
inline unsigned int ParityMarkedCount(const std::vector<ParityItem>& items) { return coopmark::OwnerTailMarked(ParityOwners(items)); }
/* P94 / P109 (owner 219): MAY THE HOLDER ANSWER AN ASKED BOX FROM ITS OWN CONTENTS? Not a non-player box of a sector this
   game once had loaded as the NON-holder (the engine never stocked its copy there) when it has neither received the holder's
   contents of that box nor stocked it itself since. 1 = answer from the contents, 0 = the contents are unknown here. */
inline int ParityServeTrusted(int playerBox, int wasNonHolder, int received, int filledHere)
{
    if (playerBox != 0 || wasNonHolder == 0) return 1;
    return (received != 0 || filledHere != 0) ? 1 : 0;
}
/* P109 R5: only an OPEN ask (the box on screen at the asker) lifts research items at the holder - an arrival row, a re-ask or
   a pull never does (P94 review F6: with the arrival ask on, every arrival row lifted every box in town). */
inline int ParityLiftOnServe(int kind) { return (kind == kParityAskOpen) ? 1 : 0; }
/* P109 R1/R4: a game that does NOT write the box answers it from its own contents only for the holder's PULL, only when the
   asker holds the box's area (heldByOtherArea: the area rule, never a player box) and only from a trusted copy
   (ParityServeTrusted). Every other ask to a non-writer stays `not holder`. */
inline int ParityPullAnswer(int kind, int heldByOtherArea, int trusted)
{
    return (kind == kParityAskPull && heldByOtherArea != 0 && trusted != 0) ? 1 : 0;
}
/* P109: a refusal (ParityServeTrusted said no) pulls the contents from the asker - unless the refused ask was itself a pull,
   so two games that both think they hold a box never pull from each other back and forth. */
inline int ParityPullOnRefusal(int refusedKind) { return (refusedKind != kParityAskPull) ? 1 : 0; }
/* P109: an answer for a box this game writes now is not swapped in (ours stands) - unless it answers this game's own pull. */
inline int ParityApplyOverOwn(int writerMine, int pulling) { return (writerMine == 0 || pulling != 0) ? 1 : 0; }
/* P109 R3: A SECTOR'S MARK (this game's copies there are the non-holder's, never stocked by the engine). Owned = this game had
   the sector as its own (verdict mine) at an arrival before any arrival marked it - its copies there are real, and a later
   return as the non-holder keeps them trusted. Holding a sector after it was marked never makes its copies real (a gave-up
   mark that clears, ParityGaveUpClears, leaves the sector unmarked and NOT owned - owned only by a later arrival, as if it
   had never been marked - P109 fold 1b). */
inline int ParitySectorOwned(int wasOwned, int wasMarked, int mine)
{
    if (wasOwned != 0) return 1;
    return (wasMarked == 0 && mine != 0) ? 1 : 0;
}
/* 1 = marked: kept for the world; an arrival held by the other game marks; one whose verdict never settled (gave up) marks
   too (decided 2026-09-30: the asker then keeps its own copy, the safer side); a sector owned before is never marked. */
inline int ParitySectorMark(int wasMarked, int ownedBefore, int held, int gaveUp)
{
    if (wasMarked != 0) return 1;
    if (ownedBefore != 0) return 0;
    return (held != 0 || gaveUp != 0) ? 1 : 0;
}
/* P109 R1: a marked sector this game has loaded pulls its never-received boxes ONCE per holding - when its verdict reads
   mine and this LOAD of the sector has not pulled yet (pulled is cleared only when the sector leaves; a re-holding within the
   same load is left to the refusal pull, ParityPullOnRefusal - P109 fold 1, F3). */
inline int ParityPullSector(int marked, int inZone, int pulled, int mine)
{
    return (marked != 0 && inZone != 0 && pulled == 0 && mine != 0) ? 1 : 0;
}
/* P109 fold 1 (F2, manager ruling 2026-09-30): a mark made because an arrival's verdict never settled (gave up) does not outlive a
   holding where this game holds the sector ALONE - this game's verdict reads mine and the notebook's fresh loaded map shows no
   other game with the sector loaded (otherLoaded 0; -1 = no fresh map, 1 = another game has it: kept). The mark is then
   cleared - the sector is simply no longer marked; it is NOT made owned (fold 1b, manager ruling revised 2026-09-30: owned
   only by the normal rules, ParitySectorOwned, as before the gave-up mark existed). A mark made by a held verdict is never
   cleared. */
inline int ParityGaveUpClears(int gaveUpMark, int mine, int otherLoaded)
{
    return (gaveUpMark != 0 && mine != 0 && otherLoaded == 0) ? 1 : 0;
}
inline unsigned int ParityUnknownCount(const std::vector<ParityItem>& items) { return coopmark::OwnerTailUnknown(ParityOwners(items)); }   /* review-inv6p2 LOW */
/* The whole box, from its items - the same sum the plugin builds while walking a live inventory. */
inline unsigned int ParityDigestOf(const std::vector<ParityItem>& items)
{
    unsigned int d = 0;
    for (size_t i = 0; i < items.size(); ++i)
        d += ParityItemHash(items[i].section, items[i].x, items[i].y, items[i].baseSid, items[i].quantity, items[i].quality);
    return d;
}

/* ---- bytes ---- */
inline void ParityPut(std::vector<char>* b, const void* p, size_t n) { const char* c = (const char*)p; b->insert(b->end(), c, c + n); }
inline void ParityPutU32(std::vector<char>* b, unsigned int v) { ParityPut(b, &v, 4); }
inline void ParityPutI32(std::vector<char>* b, int v) { ParityPut(b, &v, 4); }
inline void ParityPutF32(std::vector<char>* b, float v) { ParityPut(b, &v, 4); }
inline bool ParityPutStr(std::vector<char>* b, const std::string& s)
{
    if (s.size() > kParityStrMax) return false;
    b->push_back((char)(unsigned char)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return true;
}
struct ParityCur
{
    const char* p; size_t size; size_t at;
    ParityCur(const char* p_, size_t s_) : p(p_), size(s_), at(0) {}
    bool Get(void* out, size_t n) { if (p == 0 || size - at < n || at > size) return false; std::memcpy(out, p + at, n); at += n; return true; }
    bool U8(int* v) { unsigned char c = 0; if (!Get(&c, 1)) return false; *v = (int)c; return true; }
    bool U16(unsigned int* v) { unsigned short s = 0; if (!Get(&s, 2)) return false; *v = s; return true; }
    bool U32(unsigned int* v) { return Get(v, 4); }
    bool I32(int* v) { return Get(v, 4); }
    bool F32(float* v) { return Get(v, 4); }
    bool Str(std::string* s) { int n = 0; if (!U8(&n) || size - at < (size_t)n) return false; s->assign(p + at, (size_t)n); at += (size_t)n; return true; }
};

/* false (nothing appended) when a box or ground key is empty or too long, or there are more than kParityMaxBoxes rows or
   kParityMaxGroundKeys ground keys. The ground listing's left-out count is sent capped at 65535. */
inline bool EncodeParityReq(std::vector<char>* b, const ParityReq& r)
{
    if (b == 0 || r.boxes.size() > kParityMaxBoxes) return false;
    std::vector<char> o;
    ParityPutI32(&o, r.sx); ParityPutI32(&o, r.sy);
    const unsigned short n = (unsigned short)r.boxes.size();
    ParityPut(&o, &n, 2);
    for (size_t i = 0; i < r.boxes.size(); ++i)
    {
        if (r.boxes[i].key.empty() || !ParityPutStr(&o, r.boxes[i].key)) return false;
        ParityPutU32(&o, r.boxes[i].digest); ParityPutU32(&o, r.boxes[i].items);
    }
    if (r.kind != kParityAskPlain)   /* P109: the PRQK kind tail - absent for a plain ask */
    {
        if (r.kind != kParityAskOpen && r.kind != kParityAskPull) return false;
        o.push_back('P'); o.push_back('R'); o.push_back('Q'); o.push_back('K'); o.push_back((char)(unsigned char)r.kind);
    }
    if (r.ground != 0)   /* the PRQG ground listing, after the kind tail - absent for an ask that is not a ground ask */
    {
        if (r.groundKeys.size() > kParityMaxGroundKeys) return false;
        o.push_back('P'); o.push_back('R'); o.push_back('Q'); o.push_back('G');
        const unsigned short gn = (unsigned short)r.groundKeys.size();
        ParityPut(&o, &gn, 2);
        const unsigned short gl = (unsigned short)(r.groundLeft > 0xFFFFu ? 0xFFFFu : r.groundLeft);
        ParityPut(&o, &gl, 2);
        for (size_t i = 0; i < r.groundKeys.size(); ++i)
        {
            if (r.groundKeys[i].key.empty() || !ParityPutStr(&o, r.groundKeys[i].key)) return false;
            ParityPutI32(&o, r.groundKeys[i].qty);
            ParityPutI32(&o, r.groundKeys[i].q100);
            o.push_back((char)(r.groundKeys[i].road != 0 ? 1 : 0));
        }
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}
/* Nothing is written on a refusal. */
inline int DecodeParityReq(const char* p, size_t size, ParityReq* out)
{
    if (out == 0) return kParityDecodeTooShort;
    ParityCur c(p, size);
    ParityReq r;
    unsigned int n = 0;
    if (!c.I32(&r.sx) || !c.I32(&r.sy) || !c.U16(&n)) return kParityDecodeTooShort;
    if (n > kParityMaxBoxes) return kParityDecodeTooMany;
    for (unsigned int i = 0; i < n; ++i)
    {
        ParityBoxRow row;
        if (!c.Str(&row.key) || !c.U32(&row.digest) || !c.U32(&row.items)) return kParityDecodeTooShort;
        if (row.key.empty()) return kParityDecodeBadKey;
        r.boxes.push_back(row);
    }
    if (c.at < size && size - c.at >= 5 && std::memcmp(p + c.at, "PRQK", 4) == 0)   /* P109: the kind tail (any other trailing bytes stay refused) */
    {
        const int k = (int)(unsigned char)p[c.at + 4];
        if (k != kParityAskOpen && k != kParityAskPull) return kParityDecodeBadKind;
        r.kind = k; c.at += 5;
        for (size_t i = 0; i < r.boxes.size(); ++i) r.boxes[i].kind = k;
    }
    if (c.at < size && size - c.at >= 8 && std::memcmp(p + c.at, "PRQG", 4) == 0)   /* the ground listing, after the kind tail */
    {
        c.at += 4;
        unsigned int gn = 0, gl = 0;
        if (!c.U16(&gn) || !c.U16(&gl)) return kParityDecodeTooShort;
        if (gn > kParityMaxGroundKeys) return kParityDecodeTooMany;
        for (unsigned int i = 0; i < gn; ++i)
        {
            ParityGroundRow g;
            int rf = 0;
            if (!c.Str(&g.key) || !c.I32(&g.qty) || !c.I32(&g.q100) || !c.U8(&rf)) return kParityDecodeTooShort;
            if (g.key.empty()) return kParityDecodeBadKey;
            if (rf > 1) return kParityDecodeBadFlag;
            g.road = rf;
            r.groundKeys.push_back(g);
        }
        r.ground = 1;
        r.groundLeft = gl;
    }
    if (c.at != size) return kParityDecodeTrailing;
    *out = r;
    return kParityDecodeOk;
}

/* THE GROUND CATCH-UP PLAN (holder side). `holder`: the holder's ground items in the sector (road 1 = an item the ground road
   put down there, 0 = the world's own loose loot, which each game places for itself); `asked`: the asker's listing - every ground
   item it has there, each flagged road or not (a game started again knows none of its earlier road items, so an item it got by
   the road and saved is listed with road 0). Pairing, the listed road rows before the other listed rows at each step: (1) the same
   key, with any unpaired holder item - among several, the better quantity and quality match (coopground::GroundBagRank), then a
   road item; (2) an unpaired holder ROAD item of the same sid within coopground::kGroundNearTenths measured flat (a dropped item
   settles at a different height on each game), the best by coopground::GroundNearBetter - quantity and quality match, then the
   smaller height difference, then the flat distance; (3) the same, with the holder's other items. *add: the holder's road items
   nothing listed pairs with (each is sent as a GROUND ADD; world loot never is) - left empty when `listingCut` != 0 (the asker
   left items out at its cap, so an unpaired holder item may be one it has); *gone: the listed ROAD rows nothing of the holder's
   pairs with (each is sent as a GROUND GONE; a listed row that is not road never is). A key that does not parse as a ground key
   is in neither list. Returns the holder road items left unpaired that were kept out of *add because the listing was cut. */
inline size_t ParityGroundPlan(const std::vector<ParityGroundRow>& holder, const std::vector<ParityGroundRow>& asked, int listingCut,
                               std::vector<size_t>* add, std::vector<size_t>* gone)
{
    if (add == 0 || gone == 0) return 0;
    add->clear(); gone->clear();
    const size_t hn = holder.size(), an = asked.size();
    std::vector<coopground::GroundKeyParts> hp(hn), ap(an);
    std::vector<int> hOk(hn, 0), aOk(an, 0), hUsed(hn, 0), aUsed(an, 0);
    for (size_t i = 0; i < hn; ++i) { std::memset(&hp[i], 0, sizeof hp[i]); hOk[i] = (coopground::GroundKeyParse(holder[i].key.c_str(), &hp[i]) == 1) ? 1 : 0; }
    for (size_t j = 0; j < an; ++j) { std::memset(&ap[j], 0, sizeof ap[j]); aOk[j] = (coopground::GroundKeyParse(asked[j].key.c_str(), &ap[j]) == 1) ? 1 : 0; }
    std::vector<size_t> order;   /* the listed road rows first, then the others, each in listing order */
    order.reserve(an);
    for (size_t j = 0; j < an; ++j) if (asked[j].road != 0) order.push_back(j);
    for (size_t j = 0; j < an; ++j) if (asked[j].road == 0) order.push_back(j);
    const long long nearMax = (long long)coopground::kGroundNearTenths * (long long)coopground::kGroundNearTenths;
    for (int step = 0; step < 3; ++step)   /* 0: the same key, any holder item; 1: near, the holder's road items; 2: near, the rest */
    {
        for (size_t o = 0; o < order.size(); ++o)
        {
            const size_t j = order[o];
            if (aOk[j] == 0 || aUsed[j] != 0) continue;
            size_t best = hn;
            int bestRank = 0, bestRoad = 0;
            long long bestDy = 0, bestD = 0;
            for (size_t i = 0; i < hn; ++i)
            {
                if (hOk[i] == 0 || hUsed[i] != 0) continue;
                const int rk = coopground::GroundBagRank(holder[i].qty, holder[i].q100, asked[j].qty, asked[j].q100);
                const int isRoad = (holder[i].road != 0) ? 1 : 0;
                if (step == 0)
                {
                    if (coopground::GroundKeySame(hp[i], ap[j]) == 0) continue;
                    if (best == hn || rk < bestRank || (rk == bestRank && isRoad > bestRoad)) { best = i; bestRank = rk; bestRoad = isRoad; }
                    continue;
                }
                if ((step == 1) != (isRoad != 0)) continue;
                const long long d = coopground::GroundKeyDist2XZ(hp[i], ap[j]);   /* -1 = another sid */
                if (d < 0 || d > nearMax) continue;
                const long long dy = coopground::GroundKeyDy(hp[i], ap[j]);
                if (best == hn || coopground::GroundNearBetter(rk, dy, d, bestRank, bestDy, bestD) != 0) { best = i; bestRank = rk; bestDy = dy; bestD = d; }
            }
            if (best < hn) { hUsed[best] = 1; aUsed[j] = 1; }
        }
    }
    size_t held = 0;
    for (size_t i = 0; i < hn; ++i)
        if (hOk[i] != 0 && hUsed[i] == 0 && holder[i].road != 0) { if (listingCut != 0) ++held; else add->push_back(i); }
    for (size_t j = 0; j < an; ++j) if (aOk[j] != 0 && aUsed[j] == 0 && asked[j].road != 0) gone->push_back(j);
    return held;
}

/* false (nothing appended) for a bad status, an empty or long key, a long string, too many items, or items on a
   status other than CONTENTS. */
inline bool EncodeParityBox(std::vector<char>* b, const ParityBox& m)
{
    if (b == 0 || m.status < 0 || m.status > kParityStatusMax || m.key.empty()) return false;
    if (m.items.size() > kParityMaxItems) return false;
    if (m.status != kParityStatusContents && !m.items.empty()) return false;
    std::vector<char> o;
    o.push_back((char)(unsigned char)m.status);
    if (!ParityPutStr(&o, m.key)) return false;
    ParityPutU32(&o, m.digest);
    const unsigned short n = (unsigned short)m.items.size();
    ParityPut(&o, &n, 2);
    for (size_t i = 0; i < m.items.size(); ++i)
    {
        const ParityItem& it = m.items[i];
        if (!ParityPutStr(&o, it.section)) return false;
        ParityPutI32(&o, it.x); ParityPutI32(&o, it.y); ParityPutI32(&o, it.quantity);
        if (!ParityPutStr(&o, it.baseSid) || !ParityPutStr(&o, it.companySid)
            || !ParityPutStr(&o, it.materialSid) || !ParityPutStr(&o, it.colorSid)) return false;
        ParityPutF32(&o, it.quality); ParityPutF32(&o, it.charges);
        ParityPutI32(&o, it.functionKind); ParityPutI32(&o, it.level);
        o.push_back((char)(unsigned char)(it.unique != 0 ? 1 : 0));
    }
    coopmark::EncodeOwnerTail(&o, ParityOwners(m.items), 0);   /* inv6 phase 2: nothing when no item is marked */
    {   /* bag23 part 2 (protocol 81): packs' contents LAST, in the kit's BAGK form keyed by item index - nothing when no pack holds
           anything. A block that will not encode (a pack over the cap, a bad row) refuses the whole answer: a box whose pack would
           be rebuilt EMPTY is the loss this block exists to end. The digest is unchanged (packs' rows are not in it). */
        std::vector<coopbag::KitBag> kb;
        for (size_t i = 0; i < m.items.size(); ++i)
            if (!m.items[i].bag.empty()) { coopbag::KitBag k; k.kitIndex = (unsigned int)i; k.rows = m.items[i].bag; kb.push_back(k); }
        if (!coopbag::EncodeKitBags(&o, kb, m.items.size())) return false;
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}
/* Nothing is written on a refusal. An item with an empty base sid is refused as short: nothing can be made from it. */
inline int DecodeParityBox(const char* p, size_t size, ParityBox* out)
{
    if (out == 0) return kParityDecodeTooShort;
    ParityCur c(p, size);
    ParityBox m;
    unsigned int n = 0;
    if (!c.U8(&m.status)) return kParityDecodeTooShort;
    if (m.status > kParityStatusMax) return kParityDecodeBadStatus;
    if (!c.Str(&m.key) || !c.U32(&m.digest) || !c.U16(&n)) return kParityDecodeTooShort;
    if (m.key.empty()) return kParityDecodeBadKey;
    if (n > kParityMaxItems) return kParityDecodeTooMany;
    if (m.status != kParityStatusContents && n != 0) return kParityDecodeTooMany;
    for (unsigned int i = 0; i < n; ++i)
    {
        ParityItem it; int u = 0;
        if (!c.Str(&it.section) || !c.I32(&it.x) || !c.I32(&it.y) || !c.I32(&it.quantity)
            || !c.Str(&it.baseSid) || !c.Str(&it.companySid) || !c.Str(&it.materialSid) || !c.Str(&it.colorSid)
            || !c.F32(&it.quality) || !c.F32(&it.charges) || !c.I32(&it.functionKind) || !c.I32(&it.level) || !c.U8(&u))
            return kParityDecodeTooShort;
        if (it.baseSid.empty()) return kParityDecodeTooShort;
        it.unique = u;
        m.items.push_back(it);
    }
    if (n != 0 && c.at < size && coopmark::OwnerTailAt(p, size, c.at))   /* inv6 phase 2: the owner tail */
    {
        std::vector<coopmark::OwnerId> own; size_t oe = 0;
        if (coopmark::DecodeOwnerTail(p, size, c.at, m.items.size(), &own, &oe) != coopmark::kOwnOk) return kParityDecodeBadOwner;
        for (size_t i = 0; i < own.size(); ++i) m.items[i].owner = own[i];
        c.at = oe;
    }
    if (n != 0 && c.at < size)   /* bag23 part 2: the optional BAGK packs block, LAST (absent = no pack holds anything) */
    {
        size_t o2 = c.at;
        unsigned int mk = 0;
        if (coopbag::BagGetU32(p, size, &o2, &mk) && mk == coopbag::kKitBagMarker)
        {
            std::vector<coopbag::KitBag> kb;
            size_t ke = 0;
            if (coopbag::DecodeKitBags(p, size, c.at, m.items.size(), &kb, &ke, 0) != coopbag::kKitBagOk) return kParityDecodeBadBag;
            for (size_t i = 0; i < kb.size(); ++i) m.items[kb[i].kitIndex].bag.swap(kb[i].rows);
            c.at = ke;
        }
    }
    if (c.at != size) return kParityDecodeTrailing;
    *out = m;
    return kParityDecodeOk;
}

/* THE GROUND SETTLE STEP: one zone poll's ground item count for a sector into its settle record. *state: 0 = not counted since
   the sector became active here, 1 = counted once (*last holds the count), 2 = settled (the same count on two consecutive polls;
   it stays settled until the record is cleared). A zone's saved ground items are placed some time after the sector appears, so a
   ground listing or a catch-up answer read before the count holds still misses some of them. Returns 1 when settled. */
inline int GroundSettleStep(int* last, unsigned char* state, int count)
{
    if (last == 0 || state == 0) return 0;
    if (*state == 2) return 1;
    if (*state == 1 && *last == count) { *state = 2; return 1; }
    *last = count; *state = 1;
    return 0;
}

} // namespace cooppar
