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
enum { kWhyCamera = 1, kWhyPlayer = 2, kWhyTown = 4, kWhyPlayerLeaving = 8, kWhyKept = 16 };
// decision 1556 (T-1285 restored): WHAT 'LOADED' MEANS - THE ENGINE STILL KEEPS THE AREA. Each ZoneMap holds three keep-alive countdowns,
// camera +0xC0, player character +0xC4, town +0xC8 (the engine's writer: ZoneMap::activate 0xA0D6A0 writes [+0xC0 + type * 4] with the
// caller's length, or with the default table DAT_1417477a0[type] when the length is not positive - decomp_a0d6a0.txt:17-24; row
// ZoneActivationDefaults, read-only 4.0 / 6.0 / 5.0 s in both builds). The area update takes the frame time off each positive one and
// unloads the area when none is left positive (decomp_a0a960.txt:14-34); nothing renews a countdown once its reason is gone, so a left
// area falls 1 s a second from its full value (T2203, Confirmed). While ZoneManager +0x1681D8 >= 2 (kZoneMgrLoadingAt) the update counts
// nothing and unloads nothing (decomp_a0a960.txt:14; 1.0.68 0xA0B865 `cmp [rax+0x1681D8],1 / jg`): the game is LOADING (T2203: every
// teleport and walk-in load, 1-44 s, Confirmed).
// KEPT = not loading and one countdown RENEWED: the engine wrote it back to its renewal value at its last update, which a left area's
// countdown never gets - it only falls (fold 3). The camera's and the player's full values are the table's (T2203 / T2210: kept camera
// areas read 4.0, kept player areas 5.9-6.0, Confirmed). THE TOWN'S IS NOT THE TABLE'S 5.0 (P265 defaults camera=4.0 player=6.0
// town=5.0, T2210 A:967 - the table entry is used only when the copy's source is 0, Read; no town countdown read 5.0 in T2210): the town
// update (FUN_140928c50, build/decomp_928cf0.txt:28-57) renews the town countdown of every zone of a town with the LARGEST camera / player
// countdown (ZoneMap::getDeactivationCountdown 0xA084B0, max(+0xC0, +0xC4)) among the town's loaded zones (0xA08490: +0xB0 or +0xB1 set),
// so a town zone kept by its town carries a copy of the camera's 4.0 or the player's 6.0 (T2210 A: town 4.0 606 reads, 6.0 157, Confirmed).
// Its full value is therefore TownFullSec = the smaller of the camera's and the player's (4.0). The other town writer (FUN_14092cce0)
// writes 1.0 s (DAT_14167b308, read from the 1.0.65 image) - below every full value (TownWriter2Held).
// fold 3 (T2210's 90 kept-check mismatches): THE CONSTRUCTOR'S FILL IS 'NEVER RENEWED', NOT 'UNKNOWN'. The ZoneMap constructor
// (FUN_140a0bc20, build/decomp_a0bc20.txt:45-47) fills all three countdowns with 0xECECECEC = -2.29e27, and only ZoneMap::activate ever
// writes them, so every reason that never kept an area keeps that fill (P265 prints it as -999.0: T2210 A:4083 19,31 '1.0/-999.0/-999.0').
// zones.cpp took a countdown outside +-1e6 as unreadable and the whole area as kept, as before decision 1556 - so every area with one
// never-used reason (nearly all of them) was listed kept however far its live countdown had fallen: the 90 mismatches (A:3883-4214
// 3.1 -> 1.0 -> -0.0), not the frame slack (T2210's reports stayed 1.0 s apart across each freeze's end, A:3797 / A:3879, so the frames
// there were short - Read from the log). CountdownsReadable now fails only on NaN or a large POSITIVE value; the fill is not renewed.
// fold 3: RENEWED = at full now AND not fallen since this area's previous read (CountdownRenewed):
//   at full: countdown >= full - kKeptSlackSec (0.5 s - the kept player's 5.9);
//   held:    countdown >= min(previous read, full) - kHeldSlackSec (0.25 s) - reads are one report (>= 1.0 s) apart, a left area falls
//            1 s a second, so it fails at its first read after it left; a frozen left area (the update counts nothing while loading)
//            fails at the first read after the freeze ends unless the freeze ended under 0.25 s before it (one extra report). The previous
//            value is capped at full so a town copy LANDING on the camera's 4.0 from a falling player copy counts as held: a town copy
//            above 4.0 (a copy of a player countdown) can fall while a renewed camera copy floors it at 4.0 - the two cannot be told apart
//            until it passes 4.0, so such a copy reads kept until it is under 3.75 (up to about 2 s after the player left; Read).
// fold 3 (review M1 kept): A SLOW FRAME. No frame timer: the engine's own lag. Every kept camera countdown is renewed every frame, so
// this read's FRESHEST camera countdown shows how far the engine's renewed countdowns sit below full right now (RenewLagSec: camera full
// - the largest camera countdown, 0..kRenewLagMaxSec). In T2203 / T2210 it is 0 at every read - kept camera areas read 4.0 even at the
// first read after a freeze (A:3879, A:34470; the engine renews after it subtracts the frame - Inferred from those reads). Should a
// long frame ever leave the renewed countdowns below full at the read, the lag widens both tests by exactly that much, so a kept area is
// not dropped; a left area then reads kept for at most that one report.
const float kKeptSlackSec = 0.5f;
const float kHeldSlackSec = 0.25f;
const float kRenewLagMaxSec = 1.0f;   // a lag above this is not a frame (no camera countdown is being renewed): capped
const float kTownWriter2Sec = 1.0f;
const float kCountdownUnreadableAt = 1.0e6f;   // a countdown at or above this, or NaN, is not a value the engine writes
// 1 = all three countdowns are values the engine writes or its constructor's fill (-2.29e27, 'never renewed'); 0 = a NaN or a huge
// positive value (a bad read) - the area is then taken as kept, as before decision 1556
inline int CountdownsReadable(const float* cd)
{
    for (int k = 0; k < 3; ++k) if (!(cd[k] == cd[k]) || cd[k] >= kCountdownUnreadableAt) return 0;
    return 1;
}
// how far the engine's renewed countdowns sit below full at this read: camera full - the largest camera countdown of the read, 0..cap
inline float RenewLagSec(float maxCamera, float cameraFull)
{
    if (!(cameraFull > 0.0f) || !(maxCamera > 0.0f)) return 0.0f;
    const float d = cameraFull - maxCamera;
    if (!(d > 0.0f)) return 0.0f;
    return d < kRenewLagMaxSec ? d : kRenewLagMaxSec;
}
const int kZoneMgrLoadingAt = 2;   // ZoneManager +0x1681D8 at or above this: the area update is frozen - LOADING
const unsigned long long kZoneMgrLoadingField = 0x1681D8;
const unsigned long long kZoneMapCountdownAt = 0xC0;   // ZoneMap + 0xC0 + type * 4 (type 0 camera, 1 player character, 2 town)
inline int ZonesLoading(int fieldKnown, int field) { return (fieldKnown != 0 && field >= kZoneMgrLoadingAt) ? 1 : 0; }
inline float TownFullSec(float cameraFull, float playerFull) { return cameraFull < playerFull ? cameraFull : playerFull; }
// 1 = this countdown was renewed by the engine: at its full value now (full > 0) and not fallen since this area's previous read
// (prevKnown 1: read at the previous report); lag = RenewLagSec of this read
inline int CountdownRenewed(float countdown, float full, int prevKnown = 0, float prev = 0.0f, float lag = 0.0f)
{
    if (!(full > 0.0f) || !(countdown >= full - kKeptSlackSec - lag)) return 0;
    if (prevKnown != 0)
    {
        const float ref = (prev < full) ? prev : full;   /* the fill (-2.29e27) never fails it; NaN is taken as full */
        if (countdown < ref - kHeldSlackSec - lag) return 0;
    }
    return 1;
}
// 1 = a town countdown held at the second writer's 1.0 s on this read and the previous one (prevKnown 1), moving under kHeldSlackSec
inline int TownWriter2Held(float town, int prevKnown, float prevTown)
{
    if (prevKnown == 0) return 0;
    const float lo = kTownWriter2Sec - kKeptSlackSec, hi = kTownWriter2Sec + 0.05f;
    const float d = town > prevTown ? town - prevTown : prevTown - town;
    return (town > 0.0f && town >= lo && town <= hi && prevTown >= lo && prevTown <= hi && d < kHeldSlackSec) ? 1 : 0;
}
// THE BITS FOR ONE ENGINE AREA: `why` (the reason test's camera / player / town bits), plus
//   kWhyKept          - not loading and one countdown renewed (CountdownRenewed / TownWriter2Held); or the countdowns unreadable (known 0:
//                       no default table or a faulted read; or a NaN / huge positive value - as before decision 1556);
//   kWhyPlayerLeaving - the player reason is set, the countdowns known, and the PLAYER countdown (+0xC4, per type - not max(camera,
//                       player)) is not renewed: no player character of this game is there any more (T-1285 (d)).
// cd / full = camera, player, town (full[2] = TownFullSec). Loading never sets kWhyKept: a loading report is marked and lists every
// loaded area (zones.cpp). prevKnown / prev = this area's three countdowns at the previous report's read; lag = RenewLagSec.
inline int WhyFromCountdowns(int why, int known, int loading, const float* cd, const float* full, int prevKnown = 0, const float* prev = 0, float lag = 0.0f)
{
    why &= (kWhyCamera | kWhyPlayer | kWhyTown);
    if (known == 0 || cd == 0 || full == 0 || CountdownsReadable(cd) == 0) return why | kWhyKept;
    const int pk = (prevKnown != 0 && prev != 0) ? 1 : 0;
    const float p0 = pk ? prev[0] : 0.0f, p1 = pk ? prev[1] : 0.0f, p2 = pk ? prev[2] : 0.0f;
    if ((why & kWhyPlayer) != 0 && CountdownRenewed(cd[1], full[1], pk, p1, lag) == 0) why |= kWhyPlayerLeaving;
    if (loading == 0 && (CountdownRenewed(cd[0], full[0], pk, p0, lag) != 0 || CountdownRenewed(cd[1], full[1], pk, p1, lag) != 0
                         || CountdownRenewed(cd[2], full[2], pk, p2, lag) != 0 || TownWriter2Held(cd[2], pk, p2) != 0)) why |= kWhyKept;
    return why;
}
// 1 = an area marked for a player character (why has kWhyPlayer) whose mark is HELD, i.e. a player character of this game is there now
inline int PlayerMarkHeld(int why) { return ((why & kWhyPlayer) != 0 && (why & kWhyPlayerLeaving) == 0) ? 1 : 0; }
// WHAT THE AREAS REPORT NAMES: while loading, every loaded area (the report carries the loading mark and the world server changes no
// listing and no charge on it - the list serves only where AREA traffic goes); otherwise only the KEPT areas.
inline int ReportsArea(int why, int loading) { return (loading != 0 || (why & kWhyKept) != 0) ? 1 : 0; }
struct Area { int x, y, why; };
struct Sources { int total, camera, player, town, leaseOnly, outsideRing; };

inline int InGrid(int x, int y) { return (x >= 0 && x < kGrid && y >= 0 && y < kGrid) ? 1 : 0; }
// fold 1 (review M2): THIS GAME'S KEPT GRID - what its own "nobody holds it and I have it, so it is mine" presumption reads (zones.cpp
// AreaViewTS `loadedHere`), never the engine's loaded set: an area the engine is unloading is not this game's (the world server let it go
// at this game's first report without it). grid = kGrid x kGrid bytes, index x * kGrid + y. Not loading: cleared, then 1 at each area
// with kWhyKept - exactly what this tick's report names (less a test-only `areaquiet` area); loading: left as it was, as the world
// server leaves this game's listing as its last unmarked report said. Returns 1 when the grid was refilled.
inline int KeptGridFill(const std::vector<Area>& v, int loading, unsigned char* grid)
{
    if (grid == 0 || loading != 0) return 0;
    for (int i = 0; i < kGrid * kGrid; ++i) grid[i] = 0;
    for (size_t i = 0; i < v.size(); ++i)
        if ((v[i].why & kWhyKept) != 0 && InGrid(v[i].x, v[i].y) != 0) grid[v[i].x * kGrid + v[i].y] = 1;
    return 1;
}

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
// THE CREATION GATES' AREA ANSWER (zones' MayMakeFromView; owner decision 1531, T-1261). Only the game the world server names the
// area's holder makes a town's people: 1 = make it (`mine` 1, nobody else holds it); 0 = another game holds it (`held` 1) or nobody
// holds it yet; -1 = no fresh area map (`held` < 0). What happens to work refused while nobody holds the area depends on its road:
// a building's residents and a town's bar crowd are set aside and the town's check-up offers them again once the area is named; a
// town's roaming / military group is asked for again by the engine's own spawner and town update; a sleeping group's wake waits for
// the naming (WakeWaitsForNaming below); a single character is skipped by the engine's caller. Loaded here and ring 1 are not asked:
// the game that makes the people while nobody holds the area was not always the game the server then named (H-T1261-preholder: a
// joiner's trader group made before the naming, refused by the named host).
inline int HolderMakes(int held, int mine)
{
    if (held == 1) return 0;
    if (held < 0) return -1;
    return mine == 1 ? 1 : 0;
}
// T-1268 (owner decision 1531): THE BOX / DOOR / GROUND / RESTOCK AREA ANSWER over a fresh area map (items.cpp ItAreaVerdictAt's tail;
// the load window, a single-player game and the outage ladder answer before it). The named holder only - the ring-1 presumption that
// let a game beside an unheld town write its boxes, doors, ground and restocks before the naming is retired: two games arriving
// together both wrote one box and refused each other's change. kHvMine the map names this game; kHvHeld another game holds the area;
// kHvNoAnswer no fresh row (held and mine both < 0, a frozen area too); kHvUnowned the fresh map names nobody yet - the caller waits,
// and the map that names this game ends the wait. Values equal items.cpp's kBox* (asserted there).
const int kHvMine = 1, kHvHeld = 2, kHvUnowned = 3, kHvNoAnswer = 4;
inline int HolderVerdict(int held, int mine)
{
    if (mine == 1) return kHvMine;
    if (held == 1) return kHvHeld;
    if (held < 0 && mine < 0) return kHvNoAnswer;
    return kHvUnowned;
}
// T-1261 fold (review MED 1): A SLEEPING GROUP'S WAKE WAITS FOR THE NAMING. The engine wakes a sleeping group only inside a zone loaded
// on this game (F518: predicate 0x7EB390, the zone's loaded byte), and the wake's people are made afterwards through the character
// gate (ActivePlatoon::setupCheck 0x4FF040 -> createRandomSquad 0x582F80 -> createRandomCharacter 0x582C50), which refuses them
// while nobody holds the area (HolderMakes 0) - a first-time or regrow-marked group would come up with nobody in it (F956) and
// nothing asks again until the area reloads. So while the area is loaded here and the fresh map names nobody (`held` 0, `mine`
// not 1) the wake is held, and the next update after the naming decides: this game named the holder - the wake runs and makes its
// people; another game named - the wake's existing rules (store.cpp detour_unloadedUpdate: on the store authority the engine's own
// update runs; on the other games the wake is skipped while the fresh map names another game - OtherHasSector). 1 = hold the wake;
// 0 = those rules decide (no fresh map, a confirmed other holder, this game named, or not loaded here - the engine does not wake a
// group there).
// T-1261 re-check fold (MED): another game named but not yet confirmed (`held` 1, `holderLoaded` not 1 - zones' HolderLoadedTS: the
// named game does not have the area in its own loaded bits) is held the same way: the server may still pass the area on (T-1285: a
// holder whose latest report no longer lists the area passes it at once to a game that lists it), and the people a wake made in that
// window would be refused here (HolderMakes 0) and lost.
inline int WakeWaitsForNaming(int loadedHere, int held, int mine, int holderLoaded)
{
    if (loadedHere != 1) return 0;
    if (held == 1) return holderLoaded != 1 ? 1 : 0;
    return (held == 0 && mine != 1) ? 1 : 0;
}
// THE RING-1 GRANT (zones' MayInventFromView, its last rule). Next to any of this game's player characters and nobody else beside the
// area, or someone is and this game's slot is lower. A ring 1 that only a non-followed character gives is judged the same way: every
// game sends the areas of all its player characters, so the other game sees this game's party beside the area and the lowest slot
// wins on both sides.
// T-1285 fold 1 (review M3), decision 1556: AN UNHELD AREA THIS GAME HAS LOADED. The world server names its holder at the first report
// that lists it KEPT - the first or only game with it loaded (areaclaim.h AreaChargeDecide) - so the presumption only bridges the second
// or two before that map lands (longer while this game's engine is loading: its reports then carry the loading mark and name nothing). While another player's character is within one area of it
// (peerRing1 1) the answer is "not mine" until the server names a holder - two games arriving together never both act as holder.
// Nobody else beside it: this game presumes it (1).
inline int LoadedHereGrants(int peerRing1)
{
    return peerRing1 == 1 ? 0 : 1;
}
inline int Ring1Grants(int ring1, int peerRing1, int mySlotLower)
{
    if (ring1 != 1) return 0;
    if (peerRing1 != 1) return 1;
    return mySlotLower != 0 ? 1 : 0;
}
// THIS GAME'S PLAYER-CHARACTER AREAS FOR THE WORLD SERVER (the AREAS player-sector list). The followed character's area (fx, fy; off
// the grid = unknown) first, then - when useMarks is 1 - every other area the engine's active-zone list keeps for a player character
// (kWhyPlayer) with the mark held (T-1285 (d), PlayerMarkHeld: its player countdown at its renewal value, not one counting down after
// the character left - WhyFromCountdowns), each once, at most `cap`
// in all. *xy = x,y pairs. Returns how many marked areas did not fit (0 = none left out).
inline int PlayerSectorList(int fx, int fy, const std::vector<Area>& areas, int useMarks, int cap, std::vector<int>* xy)
{
    xy->clear();
    if (cap > 0 && InGrid(fx, fy) != 0) { xy->push_back(fx); xy->push_back(fy); }
    if (useMarks == 0) return 0;
    int left = 0;
    for (size_t i = 0; i < areas.size(); ++i)
    {
        const Area& a = areas[i];
        if (PlayerMarkHeld(a.why) == 0 || InGrid(a.x, a.y) == 0) continue;   /* T-1285 (d): only a mark the engine is still renewing */
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
                if (PlayerMarkHeld(areas[i].why) != 0 && areas[i].x == x && areas[i].y == y) { marked = 1; break; }
            if (marked == 0) continue;
        }
        grid[x * kGrid + y] = 1;
    }
}

// T-959: an area's first-time flag as createBuilding 0x57C1E0 reads it (Steam_1.0.65 decomp_57c1e0.txt:243-264, 360, 513-518):
// 0xA07C10(zoneManager, sx, sy) answers the ZoneMap zoneManager + 200 (0xC8) + (sx*64 + sy)*0x168, or none off the 64x64 grid
// (decomp_a07c10.txt); the engine returns no piece when that is none (:259) or when the pointer AT the ZoneMap is null (:262-264,
// local_490 = *local_438); the flag is the byte at that pointer + 0xA8 (:360, :513, :518). Not the byte at ZoneMap + 0xA8.
const unsigned long long kZoneMapArea = 0;             // ZoneMap + 0: the area object createBuilding works with
const unsigned long long kAreaFirstTimeFlag = 0xA8;    // area + 0xA8: the first-time flag (non-zero = set)
// the ZoneMap's address for area sx, sy (0xA07C10's sum); 0 = no zone manager or off the grid
inline unsigned long long ZoneMapAddr(unsigned long long zoneManager, int sx, int sy)
{
    if (zoneManager == 0 || InGrid(sx, sy) == 0) return 0;
    return zoneManager + kZoneArrayAt + ((unsigned long long)sx * (unsigned long long)kGrid + (unsigned long long)sy) * kZoneMapSize;
}
// the flag read's answer: 0 / 1 = the flag; kFlagNoZone = no zone manager or off the grid; kFlagNoArea = the area pointer is null
// (createBuilding would return no piece there); kFlagUnread = a read faulted
enum { kFlagNoZone = -3, kFlagNoArea = -2, kFlagUnread = -1 };
inline int AreaFlagOf(int zoneMapOk, int areaReadOk, unsigned long long area, int byteReadOk, int byteVal)
{
    if (zoneMapOk == 0) return kFlagNoZone;
    if (areaReadOk == 0) return kFlagUnread;
    if (area == 0) return kFlagNoArea;
    if (byteReadOk == 0) return kFlagUnread;
    return byteVal != 0 ? 1 : 0;
}
/* T-1442: THE HOLDER MOVES OF ONE APPLIED AREA MAP (zones.cpp ApplyRelayAreaMap -> HolderMovesStep). HolderSeedOnLink: 1 when this
   map is the first on its link (linkGen differs from the one the grid was last filled on - the grid is then seeded, no lines), and the
   grid's link is taken. HolderMovesDiff: seen / owner are n*n grids (owner < 0 = nobody, stored as -1); every area whose holder differs
   from seen appends x, y, before, after, and seen takes the new holder; seed 1 takes the holders and appends nothing. Returns the moves. */
inline int HolderSeedOnLink(long* seenLinkGen, long linkGen)
{
    const int seed = *seenLinkGen != linkGen ? 1 : 0;
    *seenLinkGen = linkGen;
    return seed;
}
inline int HolderMovesDiff(int* seen, const int* owner, int n, int seed, std::vector<int>* moves)
{
    int c = 0;
    for (int x = 0; x < n; ++x)
        for (int y = 0; y < n; ++y)
        {
            const int now = owner[x * n + y] >= 0 ? owner[x * n + y] : -1;
            int& was = seen[x * n + y];
            if (seed == 0 && was != now) { moves->push_back(x); moves->push_back(y); moves->push_back(was); moves->push_back(now); ++c; }
            was = now;
        }
    return c;
}
/* T-1442 step 1b (F1421): THE EARLY AREA REPORT - a game reports its loaded areas when an area becomes loaded or stops being loaded, not
   only on its 1 s clock. The engine's per-frame area update (0xA0A960, zones.cpp detour_ZoneMapUpdate) copies the area's 'loaded' byte
   +0xB3; its value before and after the original, and the original's answer (0 = the area was unloaded and removed), make the event: */
enum { kAreaEvNone = 0, kAreaEvLoaded = 1, kAreaEvUnloaded = 2 };
const unsigned long long kZoneMapLoadedByte = 0xB3;   // ZoneMap + 0xB3: the loaded byte (the copy routine 0xA1C8D0 copies +0xB1 onto it, F1421)
inline int AreaUpdateEvent(int before, int after, int ret)
{
    if (ret == 0 || (before != 0 && after == 0)) return kAreaEvUnloaded;
    if (before == 0 && after != 0) return kAreaEvLoaded;
    return kAreaEvNone;
}
/* the early path runs only when an event is owed, the 1 s clock is not due (the regular report runs then and clears the mark), and a
   regular report of this world was sent (its judgements are what the early report carries) */
inline int EarlyReportRuns(int owed, int clockDue, int haveRegular) { return (owed != 0 && clockDue == 0 && haveRegular != 0) ? 1 : 0; }
/* THE MERGE (the fold 1c BLOCKER): an early read judges nothing it can avoid. last = the last REGULAR read's judged areas; now = the
   engine's loaded areas at this read, each with a FIRST SIGHT's judgement (WhyFromCountdowns with no previous read, the last regular
   read's loading mark). out = every area of now, carrying last's judgement when last lists it (its 'kept' etc. unchanged) and now's
   first-sight judgement only for an area last did not list; an area of last that now lacks is dropped. Both inputs Normalise'd; out is.
   *added / *removed count the areas new to / gone from last.
   fold 1b-3: lastJudged 0 = the last regular read was taken while LOADING, so its judgements carry no 'kept' at all (loading never sets
   it) - they are then not carried: an area both list takes now's first-sight judgement (under an unmarked early report the loading
   read's judgements would drop every area - the fold 1c BLOCKER the other way round). */
inline void EarlyMerge(const std::vector<Area>& last, const std::vector<Area>& now, std::vector<Area>* out, int* added, int* removed, int lastJudged = 1)
{
    out->clear(); int a = 0, r = 0;
    size_t i = 0, j = 0;
    while (i < last.size() || j < now.size())
    {
        if (j >= now.size() || (i < last.size() && AreaBefore(last[i], now[j]))) { ++r; ++i; continue; }
        if (i >= last.size() || AreaBefore(now[j], last[i])) { out->push_back(now[j]); ++a; ++j; continue; }
        out->push_back(lastJudged != 0 ? last[i] : now[j]); ++i; ++j;   /* the same area: the regular read's judgement (when it judged) */
    }
    if (added) *added = a;
    if (removed) *removed = r;
}
/* 1 = the merged read shows every named event's latest state: a loaded area in it, an unloaded one not (an area named twice counts by
   its LAST event). ev = n triples x, y, kind in the order raised. */
inline int EarlyReflects(const std::vector<Area>& merged, const int* ev, int n)
{
    for (int k = 0; k < n; ++k)
    {
        int later = 0;
        for (int m = k + 1; m < n; ++m) if (ev[m * 3] == ev[k * 3] && ev[m * 3 + 1] == ev[k * 3 + 1]) { later = 1; break; }
        if (later != 0) continue;
        int in = 0;
        for (size_t i = 0; i < merged.size(); ++i) if (merged[i].x == ev[k * 3] && merged[i].y == ev[k * 3 + 1]) { in = 1; break; }
        if (ev[k * 3 + 2] == kAreaEvLoaded && in == 0) return 0;
        if (ev[k * 3 + 2] == kAreaEvUnloaded && in != 0) return 0;
    }
    return 1;
}
/* what an early read does: kEarlySend = its list differs from the last report on the wire - send it; kEarlySame = the wire already
   says this; and the owed mark is cleared (EarlyClears) only when the read reflects every named event and the wire carries it (sent
   now, or the last report equal). A read that does not reflect an event keeps the mark (the next frame reads again; the next regular
   report clears it). One read a frame at most - ZonesTick runs once a frame; no time limit. */
enum { kEarlySend = 1, kEarlySame = 2 };
inline int EarlyAction(const std::vector<int>& xy, int loading, const std::vector<int>& lastSentXY, int lastSentLoading)
{
    return (xy == lastSentXY && (loading != 0) == (lastSentLoading != 0)) ? kEarlySame : kEarlySend;
}
// fold 1b-2 (review SHOULD 2): an UNNAMED event owed (the unload hook off the main thread or outside the area update, an unplaced area,
// past the named list's cap) is never cleared by an early read - only by a regular report read after it.
inline int EarlyClears(int reflects, int action, int sent, int unnamedOwed) { return (reflects != 0 && unnamedOwed == 0 && (action == kEarlySame || sent != 0)) ? 1 : 0; }
}   // namespace loadedzones
#endif
