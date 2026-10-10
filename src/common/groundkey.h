#pragma once
/* inv5 (docs: .modding/investigations/inv5-ground-items-design.md section 2) - THE GROUND KEY.
   A ground item has no instance id and its hand is minted per process, so neither names it across two games.
   It is named by content, the way P7n's box key names a box:

       G|<base sid>@<sector x>,<sector y>@<x*10>,<z*10>,<y*10>        (tenths of a world unit)

   x, y, z are RootObjectBase::pos +0x48/+0x4C/+0x50 (y is height). Quantity and quality are NOT in the key (a partial
   pickup changes quantity in place and must keep the name); they ride beside it as a cross-check.
   Same key twice (two identical drops from one spot) = a small bag of interchangeable items, matched by quantity
   first, then quality rounded to 0.01, then any (GroundBagPick). A near miss (no exact key) = the nearest item of the
   same sid within 0.5 units (GroundKeyNear) and is counted as `fuzzy`.
   PURE: no CRT, no locale, no allocation - callable from a detour on any thread, and swept by src/coop-test.
   The sector is the CALLER's (zones.cpp SectorOf) so this header needs nothing from the plugin. */

namespace coopground {

const int kGroundKeyCap = 128;          /* room for any key this header composes (the sid is hashed past kGroundSidRoom-ish) */
const int kGroundSidRoom = 96;
const long kGroundNearTenths = 5;       /* 0.5 world units */

/* Tenths of a unit, rounded half away from zero. *ok 0 = not a plausible world coordinate (NaN included). */
inline long GroundTenths(float v, int* ok)
{
    if (!(v > -1.0e7f && v < 1.0e7f)) { *ok = 0; return 0; }
    const double t = (double)v * 10.0;
    *ok = 1;
    return (long)(t >= 0.0 ? (t + 0.5) : (t - 0.5));
}
/* FNV-1a 32 over the sid's bytes - used only when the whole key would not fit. */
inline unsigned int GroundSidHash32(const char* s)
{
    unsigned int h = 2166136261u;
    for (; *s != 0; ++s) { h ^= (unsigned int)(unsigned char)*s; h *= 16777619u; }
    return h;
}
/* Appenders: return the new length or -1 (carried forward), so one test at the end covers a compose. */
inline int GroundAppendStr(char* out, int cap, int at, const char* s)
{
    if (at < 0) return -1;
    while (*s != 0) { if (at >= cap - 1) return -1; out[at++] = *s++; }
    out[at] = 0;
    return at;
}
inline int GroundAppendLong(char* out, int cap, int at, long v)
{
    if (at < 0) return -1;
    char tmp[24];
    int n = 0;
    unsigned long u = (v < 0) ? (unsigned long)(0 - (unsigned long)v) : (unsigned long)v;
    if (v < 0) { at = GroundAppendStr(out, cap, at, "-"); if (at < 0) return -1; }
    do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u != 0 && n < 20);
    while (n > 0) { if (at >= cap - 1) return -1; out[at++] = tmp[--n]; }
    out[at] = 0;
    return at;
}
inline int GroundAppendHex8(char* out, int cap, int at, unsigned int v)
{
    if (at < 0) return -1;
    const char* const hex = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) { if (at >= cap - 1) return -1; out[at++] = hex[(v >> (i * 4)) & 0xFu]; }
    out[at] = 0;
    return at;
}

struct GroundKeyParts
{
    char sid[kGroundSidRoom];   /* the base record sid, or "#<8 hex>" when it had to be hashed */
    int sx, sy;
    long x10, z10, y10;
};

inline void GroundCopySid(char* dst, int cap, const char* src)
{
    int i = 0;
    for (; src[i] != 0 && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

/* Compose the key. 1 = composed with the sid as is, 2 = composed with the sid hashed ("#" + 8 hex), 0 = not
   composable (implausible position, empty sid, or no room at all). `parts` (optional) receives the pieces. */
inline int GroundKeyCompose(char* out, int cap, const char* sid, int sx, int sy, float x, float y, float z,
                            GroundKeyParts* parts)
{
    if (out == 0 || cap <= 0) return 0;
    out[0] = 0;
    if (sid == 0 || sid[0] == 0) return 0;
    int okx = 0, oky = 0, okz = 0;
    GroundKeyParts p;
    p.x10 = GroundTenths(x, &okx);
    p.y10 = GroundTenths(y, &oky);
    p.z10 = GroundTenths(z, &okz);
    if (okx == 0 || oky == 0 || okz == 0) return 0;
    p.sx = sx; p.sy = sy;
    int result = 1;
    char hashed[16];
    const char* use = sid;
    int len = 0;
    while (sid[len] != 0 && len < kGroundSidRoom) ++len;
    int pass = 0;
    if (len >= kGroundSidRoom)
    {   /* a sid the parts cannot hold is hashed at once, so every key this composes parses back */
        int hat = 0;
        hat = GroundAppendStr(hashed, (int)sizeof hashed, hat, "#");
        hat = GroundAppendHex8(hashed, (int)sizeof hashed, hat, GroundSidHash32(sid));
        if (hat < 0) return 0;
        use = hashed; result = 2; pass = 1;
    }
    for (; pass < 2; ++pass)
    {
        int at = 0;
        at = GroundAppendStr(out, cap, at, "G|");
        at = GroundAppendStr(out, cap, at, use);
        at = GroundAppendStr(out, cap, at, "@");
        at = GroundAppendLong(out, cap, at, (long)sx);
        at = GroundAppendStr(out, cap, at, ",");
        at = GroundAppendLong(out, cap, at, (long)sy);
        at = GroundAppendStr(out, cap, at, "@");
        at = GroundAppendLong(out, cap, at, p.x10);
        at = GroundAppendStr(out, cap, at, ",");
        at = GroundAppendLong(out, cap, at, p.z10);
        at = GroundAppendStr(out, cap, at, ",");
        at = GroundAppendLong(out, cap, at, p.y10);
        if (at >= 0)
        {
            GroundCopySid(p.sid, kGroundSidRoom, use);
            if (parts != 0) *parts = p;
            return result;
        }
        if (use == hashed) break;
        int hat = 0;
        hat = GroundAppendStr(hashed, (int)sizeof hashed, hat, "#");
        hat = GroundAppendHex8(hashed, (int)sizeof hashed, hat, GroundSidHash32(sid));
        if (hat < 0) break;
        use = hashed;
        result = 2;
    }
    out[0] = 0;
    return 0;
}

/* Parse a key back into its pieces. 1 = parsed, 0 = malformed. */
inline int GroundParseLong(const char* s, int* i, long* out, char stop)
{
    int neg = 0;
    if (s[*i] == '-') { neg = 1; ++(*i); }
    if (s[*i] < '0' || s[*i] > '9') return 0;
    unsigned long long v = 0;
    int digits = 0;
    while (s[*i] >= '0' && s[*i] <= '9') { v = v * 10u + (unsigned long long)(s[*i] - '0'); ++(*i); if (++digits > 10) return 0; }
    if (s[*i] != stop) return 0;
    /* review-inv5p0 LOW: `long` is 32 bits on this compiler - a number outside it is MALFORMED, never wrapped */
    if (v > (neg ? 2147483648ULL : 2147483647ULL)) return 0;
    *out = neg ? (long)(0 - (long long)v) : (long)v;
    return 1;
}
inline int GroundKeyParse(const char* key, GroundKeyParts* out)
{
    if (key == 0 || out == 0 || key[0] != 'G' || key[1] != '|') return 0;
    int i = 2, n = 0;
    while (key[i] != 0 && key[i] != '@') { if (n >= kGroundSidRoom - 1) return 0; out->sid[n++] = key[i++]; }
    out->sid[n] = 0;
    if (n == 0 || key[i] != '@') return 0;
    ++i;
    long sx = 0, sy = 0;
    if (!GroundParseLong(key, &i, &sx, ',')) return 0; ++i;
    if (!GroundParseLong(key, &i, &sy, '@')) return 0; ++i;
    if (!GroundParseLong(key, &i, &out->x10, ',')) return 0; ++i;
    if (!GroundParseLong(key, &i, &out->z10, ',')) return 0; ++i;
    if (!GroundParseLong(key, &i, &out->y10, 0)) return 0;
    out->sx = (int)sx; out->sy = (int)sy;
    return 1;
}

inline int GroundSidEqual(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] != 0 && b[i] != 0; ++i) if (a[i] != b[i]) return 0;
    return a[i] == b[i] ? 1 : 0;
}
/* 1 = the same key (every component equal). */
inline int GroundKeySame(const GroundKeyParts& a, const GroundKeyParts& b)
{
    return (GroundSidEqual(a.sid, b.sid) && a.sx == b.sx && a.sy == b.sy && a.x10 == b.x10 && a.z10 == b.z10 && a.y10 == b.y10) ? 1 : 0;
}
/* The squared distance in tenths when the sid is the same, else -1. The sector is NOT compared: two items either
   side of a sector line 0.1 units apart are near. */
inline long long GroundKeyDist2(const GroundKeyParts& a, const GroundKeyParts& b)
{
    if (!GroundSidEqual(a.sid, b.sid)) return -1;
    const long long dx = (long long)a.x10 - b.x10, dz = (long long)a.z10 - b.z10, dy = (long long)a.y10 - b.y10;
    return dx * dx + dz * dz + dy * dy;
}
/* ground5 fold 2 (T629): the HORIZONTAL distance squared in tenths (x, z) between two keys of the same sid, -1 for another sid. A
   dropped item's height changes as it falls and settles (T629: a dismantle refund placed at its piece's height 3065.5 landed at
   1532.9), so the drop road's address match does not compare heights. */
inline long long GroundKeyDist2XZ(const GroundKeyParts& a, const GroundKeyParts& b)
{
    if (!GroundSidEqual(a.sid, b.sid)) return -1;
    const long long dx = (long long)a.x10 - b.x10, dz = (long long)a.z10 - b.z10;
    return dx * dx + dz * dz;
}
/* The height difference in tenths between two keys (their y components), never negative. */
inline long long GroundKeyDy(const GroundKeyParts& a, const GroundKeyParts& b)
{
    const long long d = (long long)a.y10 - (long long)b.y10;
    return d < 0 ? -d : d;
}
/* 1 = a near miss: same sid, NOT the same key, within kGroundNearTenths (0.5 units). */
inline int GroundKeyNear(const GroundKeyParts& a, const GroundKeyParts& b)
{
    if (GroundKeySame(a, b)) return 0;
    const long long d2 = GroundKeyDist2(a, b);
    return (d2 >= 0 && d2 <= (long long)kGroundNearTenths * kGroundNearTenths) ? 1 : 0;
}
/* Quality to hundredths, for the bag match (quality is a float in 0..~1.x). */
inline int GroundQ100(float q)
{
    if (!(q > -1.0e6f && q < 1.0e6f)) return -1;
    const double t = (double)q * 100.0;
    return (int)(t >= 0.0 ? (t + 0.5) : (t - 0.5));
}
/* THE MATCH RANK of a candidate against a wanted quantity and quality (hundredths): 0 = both equal, 1 = the quantity equal,
   2 = the quality equal, 3 = neither. */
inline int GroundBagRank(int qty, int q100, int wantQty, int wantQ100)
{
    const int sq = (qty == wantQty) ? 1 : 0, sl = (q100 == wantQ100) ? 1 : 0;
    return (sq && sl) ? 0 : sq ? 1 : sl ? 2 : 3;
}
/* THE BAG MATCH under one key: among n candidates (already known to share the key), the index of the one to use -
   first equal quantity AND quality, then equal quantity, then equal quality, then the first; -1 when n <= 0.
   `taken[i]` != 0 skips a candidate already matched (0 = none taken). */
inline int GroundBagPick(const int* qty, const int* q100, const int* taken, int n, int wantQty, int wantQ100)
{
    int best = -1, bestRank = 99;
    for (int i = 0; i < n; ++i)
    {
        if (taken != 0 && taken[i] != 0) continue;
        const int rank = GroundBagRank(qty[i], q100[i], wantQty, wantQ100);
        if (rank < bestRank) { bestRank = rank; best = i; }
    }
    return best;
}
/* THE NEAR ORDER among items of one sid lying within kGroundNearTenths of a wanted key measured flat: 1 = candidate A (match
   rank, height difference, flat distance squared) is a better match than B - the better match rank (GroundBagRank) first, then
   the smaller height difference (GroundKeyDy), then the smaller flat distance. */
inline int GroundNearBetter(int rankA, long long dyA, long long d2A, int rankB, long long dyB, long long d2B)
{
    if (rankA != rankB) return rankA < rankB ? 1 : 0;
    if (dyA != dyB) return dyA < dyB ? 1 : 0;
    return d2A < d2B ? 1 : 0;
}

/* inv5 phase 1: the position a receiver places a created ground item at - the key's own tenths. No float rides on the wire;
   a float rebuilt from its tenths rounds back to the same tenths for any plausible world coordinate (offline test), so both
   games name the item the same. */
inline void GroundKeyPos(const GroundKeyParts& p, float* xyz)
{
    xyz[0] = (float)((double)p.x10 / 10.0);
    xyz[1] = (float)((double)p.y10 / 10.0);
    xyz[2] = (float)((double)p.z10 / 10.0);
}
/* inv5 phase 1 (protocol 72): ITEM_CONFIRM ok 0 may end with {u32 kGroundReasonTag, u8 reason} - why a ground request was
   refused. The tag is the bytes 'G','N','D','1' as a little-endian u32. */
const unsigned int kGroundReasonTag = 0x31444E47u;
const int kGroundReasonNotHolder = 1, kGroundReasonNotFound = 2;
/* inv5p1 fold (review-inv5p1 6/7): 3 = the holder is handing that item over (or picking it up) right now - ask again later;
   4 = the holder has the item but cannot read its fields or pack. Neither ever removes the requester's copy. */
const int kGroundReasonBusy = 3, kGroundReasonUnreadable = 4;
/* 5 = no item on the holder's ground carries the TAKE's name and the holder does not know that name left (a game started
   again keeps no names) - the requester asks again and then keeps its copy; it never removes it (coopgshow::SettleAction). */
const int kGroundReasonUnknownName = 5;

/* P2/P3/P4 (inv5 phase 2): every engine road that puts an item on the ground goes through the ONE drop road (items.cpp GrNoteDrop ->
   the drain's GrOnDropFound). `from` names the road, for the counters and the log; the route is the same for all of them. */
const int kGroundFromPlayer = 0, kGroundFromAnimal = 1, kGroundFromSpill = 2, kGroundFromRefund = 3, kGroundFromOverflow = 4;
const int kGroundFromCount = 5;
const int kGroundRouteIgnore = 0, kGroundRouteAdd = 1, kGroundRoutePut = 2, kGroundRouteRemoveLocal = 3;
/* a building source (box/building spill 0x54DBA0, dismantle refund 0x29DBD0): 0 this game's own piece (its player faction owns it),
   1 a copy of another game's piece (a stand-in faction owns it), 2 a copy while the mod itself applies another game's change to it
   (a REMOVE / STATE), 3 (ground5 fold 1, F2) anything else - an NPC's box, a town building, a piece nobody owns: every game runs it,
   so only the area holder's own spill of it is announced */
const int kGroundSrcOwn = 0, kGroundSrcCopy = 1, kGroundSrcCopyApplying = 2, kGroundSrcWorld = 3;
inline int GroundFromBuilding(int from) { return (from == kGroundFromSpill || from == kGroundFromRefund) ? 1 : 0; }
inline int GroundBuildingSource(int isCopy, int isOwn, int modApplying)
{
    if (isCopy != 0) return modApplying != 0 ? kGroundSrcCopyApplying : kGroundSrcCopy;
    return isOwn != 0 ? kGroundSrcOwn : kGroundSrcWorld;
}
/* ground5 fold 1 (F5): the spill detour's source - an items apply (the mod applying another game's item change) marks only a COPY's
   spill as the mod's own; this game's own box, or an NPC's, keeps its source */
inline int GroundSpillSource(int bldSrc, int itemsApplying) { return (bldSrc == kGroundSrcCopy && itemsApplying != 0) ? kGroundSrcCopyApplying : bldSrc; }
/* ground5 fold 1 (F3): ANY copy's spill is removed here (its owner's game spills the real items) - a wall run's deferred flush, or the
   engine taking a copy apart, spills outside the mod's own REMOVE apply too */
inline int GroundSrcRemovedHere(int bldSrc) { return (bldSrc == kGroundSrcCopy || bldSrc == kGroundSrcCopyApplying) ? 1 : 0; }
/* the character roads (CharacterHuman::dropItem 0x5C9CB0, CharacterAnimal::dropItem 0x5C9A10): an item that was NOT listed in
   the dropper's own inventory before the drop is the full-inventory overflow - giveItem (vt +0x168, 0x5CA970) with dropOnFail ->
   Inventory::addItem 0x745140 -> Inventory::dropItem 0x745250 -> the owner's dropItem (build/decomp_5ca970.txt, decomp_745140.txt) */
inline int GroundCharDropFrom(int animal, int wasInOwnInv)
{
    if (wasInOwnInv == 0) return kGroundFromOverflow;
    return animal != 0 ? kGroundFromAnimal : kGroundFromPlayer;
}
/* the one route. holderMine: this game holds the item's area. own: the source is this game's own (its own character; a building
   its player faction owns). selfApply: the item was spilled by a copy (ground5 fold 1 F3: during the mod's own apply of another
   game's change or not) - the owner's game spills the real one and routes it, so this game's spill is removed here and never
   announced or PUT. world (ground5 fold 1 F2): an NPC's / the world's building. worldRemovable (ground5 fold 2, the re-check of
   fold 1 HIGH a/b): the holder spills that same item too - ANOTHER game definitely holds the area AND it is the engine taking the
   building apart (the spill road, the piece dismantled or destroyed); only then is this game's spill removed. Every other world
   spill - a player's drag out of an NPC box or a full NPC box's overflow (the same spill road, the piece whole), a world piece's
   dismantle refund, ground nobody holds or no answer - is one only this game made: PUT with no taker (worst case a duplicate,
   never a loss). */
inline int GroundNewItemRoute(int holderMine, int own, int selfApply, int world, int worldRemovable)
{
    if (selfApply != 0) return kGroundRouteRemoveLocal;
    if (holderMine != 0) return kGroundRouteAdd;
    if (world != 0) return worldRemovable != 0 ? kGroundRouteRemoveLocal : kGroundRoutePut;
    return own != 0 ? kGroundRoutePut : kGroundRouteIgnore;
}
/* ground5 fold 2: the area verdict as items.cpp's ItAreaVerdictAt gives it (kBoxMine 1, kBoxHeld 2, kBoxUnowned 3, kBoxNoAnswer 4;
   items.cpp asserts that the values match) */
const int kGroundAreaMine = 1, kGroundAreaHeld = 2, kGroundAreaUnowned = 3, kGroundAreaNoAnswer = 4;
/* ground5 fold 2: the drop road's whole decision from the verdict. fromSpill: the box / building spill road (0x54DBA0); takenApart:
   the piece was dismantled or destroyed around that spill (its +0x162 / +0x1A1, read before and after the engine's drop) */
inline int GroundDropRoute(int area, int own, int selfApply, int world, int fromSpill, int takenApart)
{
    const int removable = (area == kGroundAreaHeld && fromSpill != 0 && takenApart != 0) ? 1 : 0;
    return GroundNewItemRoute(area == kGroundAreaMine ? 1 : 0, own, selfApply, world, removable);
}
/* ground5 fold 1 (F1): the holder's request handler. A ground PUT (dir 1) naming NO taker comes from a building's spill / refund on a
   game that does not hold the area - it skips the taker-ownership check (uid 0 is nobody's, so that check refused it every time)
   and the area test in the serve decides. A ground request that names a taker keeps the check. */
inline int GroundPutNamesNoTaker(int dir, unsigned int takerUid) { return (dir == 1 && takerUid == 0) ? 1 : 0; }
/* A granted ground TAKE that is undone (the taker refused it or never answered) puts the item back on the holder's ground. The
   other games hear GROUND ADD for it only when they were told GROUND GONE at the grant, the item is back on the ground, and this
   game still holds the area - a game never told GONE still shows the item, and an ADD there would make a second one. 1 = send. */
inline int GroundUndoTell(int goneTold, int putBack, int holderNow) { return (goneTold != 0 && putBack != 0 && holderNow != 0) ? 1 : 0; }
/* A RECEIVER'S SHORT MEMORY OF GROUND KEYS IT APPLIED - one for keys added by GROUND ADD, one for keys removed by GROUND GONE -
   so a repeat from a second announcer (the area's holder changed between the two) neither builds a second copy nor removes a
   neighbour. An entry counts for kGroundRecentMs; the oldest entry is overwritten past kGroundRecentCap (counted overwritten when
   it was still inside the window). Times are GetTickCount values (unsigned differences, so the wrap is harmless). */
const unsigned int kGroundRecentMs = 30000;
const int kGroundRecentCap = 128;
struct GroundRecentKeys
{
    char key[kGroundRecentCap][kGroundKeyCap];
    unsigned int at[kGroundRecentCap];
    int used[kGroundRecentCap];
    long long overwritten;
};
inline void GroundRecentClear(GroundRecentKeys* r)
{
    for (int i = 0; i < kGroundRecentCap; ++i) { r->used[i] = 0; r->at[i] = 0; r->key[i][0] = 0; }
    r->overwritten = 0;
}
inline int GroundRecentFind(const GroundRecentKeys& r, const char* key)
{
    for (int i = 0; i < kGroundRecentCap; ++i) if (r.used[i] != 0 && GroundSidEqual(r.key[i], key)) return i;
    return -1;
}
/* 1 = `key` was noted within kGroundRecentMs of `now` */
inline int GroundRecentHas(const GroundRecentKeys& r, const char* key, unsigned int now)
{
    const int i = GroundRecentFind(r, key);
    return (i >= 0 && now - r.at[i] < kGroundRecentMs) ? 1 : 0;
}
inline void GroundRecentForget(GroundRecentKeys* r, const char* key)
{
    const int i = GroundRecentFind(*r, key);
    if (i >= 0) { r->used[i] = 0; r->key[i][0] = 0; }
}
/* `key` noted at `now`: its own entry renewed, else a free or expired entry, else the oldest overwritten. A key too long to keep
   whole is not noted. */
inline void GroundRecentNote(GroundRecentKeys* r, const char* key, unsigned int now)
{
    int n = 0;
    while (key[n] != 0) { if (++n >= kGroundKeyCap) return; }
    int i = GroundRecentFind(*r, key);
    if (i < 0)
    {
        int oldest = 0;
        for (int k = 0; k < kGroundRecentCap && i < 0; ++k)
            if (r->used[k] == 0 || now - r->at[k] >= kGroundRecentMs) i = k;
            else if (now - r->at[k] > now - r->at[oldest]) oldest = k;
        if (i < 0) { i = oldest; ++r->overwritten; }
        for (int c = 0; c <= n; ++c) r->key[i][c] = key[c];
    }
    r->used[i] = 1;
    r->at[i] = now;
}
/* What a receiver does with an arriving GROUND ADD (op 0) / GONE (op 1). addedRecently: its exact key was applied here by an ADD
   within the window; present: an item of that key still lies here (the flat match - it settles at its own height); removedRecently:
   its exact key was removed here by a GONE within the window. */
enum { kGroundRecentApply = 0, kGroundRecentKeepAdd = 1, kGroundRecentIgnoreGone = 2 };
inline int GroundRecentDecide(int op, int addedRecently, int present, int removedRecently)
{
    if (op == 0) return (addedRecently != 0 && present != 0) ? kGroundRecentKeepAdd : kGroundRecentApply;
    if (op == 1) return removedRecently != 0 ? kGroundRecentIgnoreGone : kGroundRecentApply;
    return kGroundRecentApply;
}

}   /* namespace coopground */
