/* src/common/bedmatch.h - BED1 (T263/T264 poseBedUnresolved): WHICH INTERIOR PIECE A BED KEY NAMES.
 *
 * The owner names its bed by the P7n position key "<sid>@<sx>,<sy>@<x10>,<z10>,<y10>" (src/common/poskey.h, through items.cpp
 * ObjectPositionKey). A bed that a building's interior LAYOUT created is not on the zone list ItBoxResolve
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

/* T-860: WHICH ROAD NAMES A PIECE'S OUTER BUILDING (items.cpp LayoutOuterKey, the owner's side of every bed / cage key).
 * Road 1, the layout: Building+0x238 (the layout the piece was made from) -> Layout+0x90 (that layout's building). Only a
 * piece the engine furnished from a layout has it; a piece made from a saved AREA FILE has none (createBuilding 0x57C1E0
 * takes its host from the saved "isInsideBuilding" hand instead, decomp_57c1e0 :203-219), so before T-860 such a cage or
 * bed was named with an empty outer key and the other game's layout road (spawn.cpp PoseBedViaLayout) could never find it.
 * Road 2, the piece's own inside-building hand: Building+0x80, five words at +0x88 (type, container, containerStamp,
 * index, serial - type first; kHandTypeNone = stands inside nothing; finding F1024), resolved to the building it names.
 * Only a hand of type kHandTypeBuilding with a non-zero index or serial is resolved; a hand naming the piece itself is
 * refused. No road -> kOuterRoadNone (an empty outer key, as before). The engine reads are the Reader's (the plugin's
 * guarded pods, a fake one offline):
 *   void* LayoutHost(void* piece)                  the building road 1 reaches, 0 = none / unreadable
 *   int   InsideIds(void* piece, unsigned int* f)  the five words at +0x88 into f: 1 read, 0 faulted
 *   void* Resolve(const unsigned int* f)           the building those words name, 0 = none / not plausible */
const int kOuterRoadNone   = 0;
const int kOuterRoadLayout = 1;
const int kOuterRoadInside = 2;
const unsigned int kHandTypeBuilding = 0;     /* itemType BUILDING */
const unsigned int kHandTypeNone     = 0xB;   /* the inside-building hand of a piece that stands inside nothing */

template <class Reader>
inline int PickOuterRoad(Reader& r, void* piece, void** host)
{
    *host = 0;
    if (piece == 0) return kOuterRoadNone;
    void* h = r.LayoutHost(piece);
    if (h != 0 && h != piece) { *host = h; return kOuterRoadLayout; }
    unsigned int f[5] = { kHandTypeNone, 0, 0, 0, 0 };
    if (r.InsideIds(piece, f) == 0) return kOuterRoadNone;
    if (f[0] != kHandTypeBuilding || (f[3] == 0 && f[4] == 0)) return kOuterRoadNone;
    h = r.Resolve(f);
    if (h == 0 || h == piece) return kOuterRoadNone;
    *host = h;
    return kOuterRoadInside;
}

}   /* namespace coopbed */
