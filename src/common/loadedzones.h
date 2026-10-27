// loadedzones.h - the areas this game's engine has loaded, as the zone tick reads them from the engine's own active-zone list.
// Pure (only <vector> and <algorithm>): zones.cpp and the offline suite include this one header.
// The engine keeps one ZoneMap per area in a fixed 64x64 array inside its ZoneManager, at +0xC8, 0x168 bytes each, indexed
// sx * 64 + sy (ZoneManager::isZoneLoadedT 0xA0D560 locates a zone exactly this way). ZoneManager::getAllActiveZones hands back
// pointers to the ZoneMaps of its active set; SectorOfZoneAt turns one of those pointers back into its area.
#ifndef LOADEDZONES_H
#define LOADEDZONES_H
#include <vector>
#include <algorithm>
namespace loadedzones {
const unsigned long long kZoneArrayAt = 0xC8;    // ZoneManager + 0xC8: the first ZoneMap
const unsigned long long kZoneMapSize = 0x168;   // one ZoneMap
const int kGrid = 64;                            // areas per side

// Why the engine keeps an area (ZoneMap::isActivationType 0 camera, 1 player character, 2 town); 0 = no reason holds now
// (the keep-alive lease has run out but the engine has not unloaded the area yet).
enum { kWhyCamera = 1, kWhyPlayer = 2, kWhyTown = 4 };

struct Area { int x, y, why; };
struct Sources { int total, camera, player, town, leaseOnly, outsideRing; };

inline int InGrid(int x, int y) { return (x >= 0 && x < kGrid && y >= 0 && y < kGrid) ? 1 : 0; }

// 1 = `zone` is a ZoneMap of the array that starts at zoneManager + kZoneArrayAt (on a ZoneMap boundary, inside the 64x64
// array), and *sx / *sy are its area; 0 = it is not, and *sx / *sy are left alone.
inline int SectorOfZoneAt(unsigned long long zoneManager, unsigned long long zone, int* sx, int* sy)
{
    const unsigned long long base = zoneManager + kZoneArrayAt;
    if (zone < base) return 0;
    const unsigned long long off = zone - base;
    if (off % kZoneMapSize != 0) return 0;
    const unsigned long long idx = off / kZoneMapSize;
    if (idx >= (unsigned long long)(kGrid * kGrid)) return 0;
    *sx = (int)(idx / (unsigned long long)kGrid);
    *sy = (int)(idx % (unsigned long long)kGrid);
    return 1;
}

inline bool AreaBefore(const Area& a, const Area& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }

// Row order (y, then x) and one entry per area: a repeated area keeps the reasons of every copy.
inline void Normalise(std::vector<Area>* v)
{
    std::sort(v->begin(), v->end(), AreaBefore);
    std::vector<Area> out;
    for (size_t i = 0; i < v->size(); ++i)
    {
        if (!out.empty() && out.back().x == (*v)[i].x && out.back().y == (*v)[i].y) { out.back().why |= (*v)[i].why; continue; }
        out.push_back((*v)[i]);
    }
    v->swap(out);
}

// Both lists name the same areas (reasons are not compared). Both must be Normalise'd.
inline bool SameAreas(const std::vector<Area>& a, const std::vector<Area>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
    return true;
}

// How many areas each reason keeps (an area can count under several), how many have no reason now, and how many lie outside
// the (2 * ring + 1) square around (cx, cy) - every area when cx or cy is negative (no centre).
inline void CountSources(const std::vector<Area>& v, int cx, int cy, int ring, Sources* s)
{
    s->total = (int)v.size(); s->camera = 0; s->player = 0; s->town = 0; s->leaseOnly = 0; s->outsideRing = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const Area& a = v[i];
        if (a.why & kWhyCamera) ++s->camera;
        if (a.why & kWhyPlayer) ++s->player;
        if (a.why & kWhyTown) ++s->town;
        if ((a.why & (kWhyCamera | kWhyPlayer | kWhyTown)) == 0) ++s->leaseOnly;
        const int dx = a.x > cx ? a.x - cx : cx - a.x, dy = a.y > cy ? a.y - cy : cy - a.y;
        if (cx < 0 || cy < 0 || dx > ring || dy > ring) ++s->outsideRing;
    }
}

// T-1076 (owner decision 1076 c): RING 1 - is area (sx, sy) within one area of ANY of this game's player characters.
// `fx, fy` = the followed character's area (-1 = unset); `party` = kGrid x kGrid bytes, index x * kGrid + y, 1 where the
// engine's active-zone list keeps the area for a player character of this game (kWhyPlayer). *partyOnly = 1 when only the
// party grid answered (the followed character is not beside it). Off the grid: 0.
inline int Ring1Of(int fx, int fy, const unsigned char* party, int sx, int sy, int* partyOnly)
{
    if (partyOnly != 0) *partyOnly = 0;
    if (InGrid(sx, sy) == 0) return 0;
    if (fx >= 0 && fy >= 0)
    {
        const int dx = sx > fx ? sx - fx : fx - sx, dy = sy > fy ? sy - fy : fy - sy;
        if (dx <= 1 && dy <= 1) return 1;
    }
    if (party == 0) return 0;
    for (int ox = -1; ox <= 1; ++ox)
        for (int oy = -1; oy <= 1; ++oy)
        {
            const int x = sx + ox, y = sy + oy;
            if (InGrid(x, y) != 0 && party[x * kGrid + y] != 0) { if (partyOnly != 0) *partyOnly = 1; return 1; }
        }
    return 0;
}
// THE RING-1 GRANT (zones' MayInventFromView, its last rule). Next to any of this game's player characters and nobody else beside the
// area, or someone is and this game's slot is lower. A ring 1 that only a non-followed character gives is judged the same way: every
// game sends the areas of all its player characters, so the other game sees this game's party beside the area and the lowest slot
// wins on both sides.
inline int Ring1Grants(int ring1, int peerRing1, int mySlotLower)
{
    if (ring1 != 1) return 0;
    if (peerRing1 != 1) return 1;
    return mySlotLower != 0 ? 1 : 0;
}
// THIS GAME'S PLAYER-CHARACTER AREAS FOR THE WORLD SERVER (the AREAS player-sector list). The followed character's area (fx, fy; off
// the grid = unknown) first, then - when useMarks is 1 - every other area the engine's active-zone list keeps for a player character
// (kWhyPlayer), each once, at most `cap` in all. *xy = x,y pairs. Returns how many marked areas did not fit (0 = none left out).
inline int PlayerSectorList(int fx, int fy, const std::vector<Area>& areas, int useMarks, int cap, std::vector<int>* xy)
{
    xy->clear();
    if (cap > 0 && InGrid(fx, fy) != 0) { xy->push_back(fx); xy->push_back(fy); }
    if (useMarks == 0) return 0;
    int left = 0;
    for (size_t i = 0; i < areas.size(); ++i)
    {
        const Area& a = areas[i];
        if ((a.why & kWhyPlayer) == 0 || InGrid(a.x, a.y) == 0) continue;
        int have = 0;
        for (size_t k = 0; k + 1 < xy->size(); k += 2) if ((*xy)[k] == a.x && (*xy)[k + 1] == a.y) { have = 1; break; }
        if (have != 0) continue;
        if ((int)(xy->size() / 2) >= cap) { ++left; continue; }
        xy->push_back(a.x); xy->push_back(a.y);
    }
    return left;
}
// THE PARTY HALO FROM THE LIST SENT. grid = kGrid x kGrid bytes (index x * kGrid + y): cleared, then 1 at each on-grid entry of xy
// (PlayerSectorList's pairs) - the same capped list the world server gets, so this game is never beside an area by a party the other
// games were not told of. followedFirst = 1 when xy's first pair is the followed character's area; skipFollowed = 1 (orphaned: that
// area is only the last known position) leaves it out, unless an area of `areas` marked kWhyPlayer names it too (a live party there).
inline void PartyHaloFromList(const std::vector<int>& xy, int followedFirst, int skipFollowed, const std::vector<Area>& areas, unsigned char* grid)
{
    if (grid == 0) return;
    for (int i = 0; i < kGrid * kGrid; ++i) grid[i] = 0;
    for (size_t k = 0; k + 1 < xy.size(); k += 2)
    {
        const int x = xy[k], y = xy[k + 1];
        if (InGrid(x, y) == 0) continue;
        if (k == 0 && followedFirst != 0 && skipFollowed != 0)
        {
            int marked = 0;
            for (size_t i = 0; i < areas.size(); ++i)
                if ((areas[i].why & kWhyPlayer) != 0 && areas[i].x == x && areas[i].y == y) { marked = 1; break; }
            if (marked == 0) continue;
        }
        grid[x * kGrid + y] = 1;
    }
}
}   // namespace loadedzones
#endif
