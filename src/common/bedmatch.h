/* src/common/bedmatch.h - BED1 (T263/T264 poseBedUnresolved): WHICH INTERIOR PIECE A BED KEY NAMES.
 *
 * The owner names its bed by the P7n position key "<sid>@<sx>,<sy>@<x10>,<z10>,<y10>" (items.cpp ItBoxKeyCompose,
 * through ObjectPositionKey). A bed that a building's interior LAYOUT created is not on the zone list ItBoxResolve
 * walks, so the copy builds the key of every piece of the OUTER building's interior with the same builder and asks
 * this helper which one the owner meant. Pure - no engine, no allocation - so the offline suite runs the code the
 * plugin runs (src/coop-test/test_main.cpp).
 *
 * The rule, in order:
 *   1. the pieces whose key is the SAME STRING as the wanted key: exactly one -> it; two or more -> refused.
 *   2. otherwise the pieces whose "<sid>@<sx>,<sy>" prefix is the same string and whose x and z are each within
 *      kPieceTenthsWindow tenths - the box road's own kBoxKeyTenthsWindow (items.cpp), for the same reason: both
 *      sides round the same way, so one tenth is the whole budget. One survivor -> it. Several: when the wanted key
 *      carried a y, the survivors whose y is within the same window; exactly one -> it. Anything else -> refused.
 *      BED1-b: a lone survivor whose y is more than kPieceFloorTenths from the wanted y is refused too.
 *   A refusal is never a guess: a wrong bed is a copy lying somewhere its owner is not.
 * `nearest` / `nearestTenths` name the same-prefix piece closest in |dx|+|dz| whatever the answer (-1 = none), so a
 * miss can print what it nearly matched (lesson 6a-12: a zero that cannot be interrogated is not a measurement).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <cstddef>

namespace coopbed {

const int  kPickNone          = -1;   /* nothing answers to the key */
const int  kPickAmbiguous     = -2;   /* more than one piece does, and nothing breaks the tie */
const long kPieceTenthsWindow = 1;    /* = items.cpp kBoxKeyTenthsWindow */
/* BED1-b (review LOW-2): a LONE near piece is still refused when both keys carry a height and they differ by more than
   this - it is the bed on the floor above or below, not a rounding difference. 100 tenths = 10 world units: far above
   the one-tenth rounding budget, and taken to be below one storey (Inferred - no storey height was measured). Layout
   furniture is not re-mounted, so the building road's reason for never rejecting a lone candidate on y (the
   upgrade/downgrade recompute) does not apply here. */
const long kPieceFloorTenths  = 100;

/* One optionally-signed decimal integer, advancing *p past it. 1 read, 0 not a number. */
inline int ParseLongAt(const char** p, long* out)
{
    const char* s = *p;
    int neg = 0;
    if (*s == '-') { neg = 1; ++s; }
    if (*s < '0' || *s > '9') return 0;
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (long)(*s - '0'); ++s; }
    *p = s;
    *out = neg ? -v : v;
    return 1;
}

/* "<prefix>@<x>,<z>[,<y>]" - the prefix is everything before the LAST '@', so a record name holding an '@' is still
   one prefix. 1 = split, 0 = malformed. */
inline int SplitPieceKey(const char* key, size_t* prefixLen, long* x, long* z, long* y, int* haveY)
{
    if (key == 0) return 0;
    const char* at = std::strrchr(key, '@');
    if (at == 0 || at == key) return 0;
    *prefixLen = (size_t)(at - key);
    const char* p = at + 1;
    if (ParseLongAt(&p, x) == 0 || *p != ',') return 0;
    ++p;
    if (ParseLongAt(&p, z) == 0) return 0;
    *haveY = 0;
    *y = 0;
    if (*p == ',') { ++p; if (ParseLongAt(&p, y) == 0) return 0; *haveY = 1; }
    return (*p == 0) ? 1 : 0;
}

inline long AbsTenths(long v) { return v < 0 ? -v : v; }

/* Returns the index into `have` of the piece `want` names, kPickNone or kPickAmbiguous. A null entry is skipped. */
inline int PickPieceKey(const char* want, const char* const* have, int n, int* nearest, long* nearestTenths)
{
    if (nearest != 0) *nearest = -1;
    if (nearestTenths != 0) *nearestTenths = -1;
    if (want == 0 || want[0] == 0 || have == 0 || n <= 0) return kPickNone;
    int exact = -1, exactN = 0;
    for (int i = 0; i < n; ++i)
        if (have[i] != 0 && std::strcmp(have[i], want) == 0) { if (exactN == 0) exact = i; ++exactN; }
    size_t wl = 0;
    long wx = 0, wz = 0, wy = 0;
    int wHaveY = 0;
    const int wantOk = SplitPieceKey(want, &wl, &wx, &wz, &wy, &wHaveY);
    int cand = -1, candN = 0, yCand = -1, yN = 0;
    int candFloorAway = 0;   /* BED1-b: the lone survivor's height is a floor away from the wanted one */
    long best = -1;
    if (wantOk != 0)
        for (int i = 0; i < n; ++i)
        {
            if (have[i] == 0) continue;
            size_t hl = 0;
            long hx = 0, hz = 0, hy = 0;
            int hHaveY = 0;
            if (SplitPieceKey(have[i], &hl, &hx, &hz, &hy, &hHaveY) == 0) continue;
            if (hl != wl || std::strncmp(have[i], want, wl) != 0) continue;
            const long dx = AbsTenths(hx - wx), dz = AbsTenths(hz - wz);
            if (best < 0 || dx + dz < best)
            {
                best = dx + dz;
                if (nearest != 0) *nearest = i;
                if (nearestTenths != 0) *nearestTenths = best;
            }
            if (dx > kPieceTenthsWindow || dz > kPieceTenthsWindow) continue;
            ++candN; cand = i;
            candFloorAway = (wHaveY != 0 && hHaveY != 0 && AbsTenths(hy - wy) > kPieceFloorTenths) ? 1 : 0;
            if (wHaveY != 0 && hHaveY != 0 && AbsTenths(hy - wy) <= kPieceTenthsWindow) { ++yN; yCand = i; }
        }
    if (exactN == 1) return exact;
    if (exactN > 1) return kPickAmbiguous;
    if (candN == 1) return (candFloorAway != 0) ? kPickNone : cand;
    if (candN == 0) return kPickNone;
    if (wHaveY != 0 && yN == 1) return yCand;
    return kPickAmbiguous;
}

}   /* namespace coopbed */
