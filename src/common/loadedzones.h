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
}   // namespace loadedzones
#endif
