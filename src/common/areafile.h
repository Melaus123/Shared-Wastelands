#pragma once
/* THIS GAME'S AREA FILE - the pure half (store.cpp writes, reads and measures the file).
   Each game keeps, in its kept mirror folder (mirrorkeep.h: mirror-k<hash of the world id and the player id>), a small file naming
   where its own people are: the areas of its player characters (the list it sends in AREAS, the followed character's area first)
   and the areas its engine has loaded, each area once. At its next join of the same world it reads the file back, so that the
   records near its people can be asked for before the game has loaded anything. Today the file is only written, read back and
   measured against the live lists: nothing is filtered, skipped or held back by it.
   THE FILE (areas-kept.txt) is written whole, by rename, as records-kept.txt is: the line "swareas 1 <worldId> <idHash> <stamp>"
   (stamp = the writer's clock, 100 ns since 1601), one row per area - "p <x> <y>" for a player-character area, in list order, then
   "l <x> <y>" for a loaded area that is not also a player area - then "end <rows> <FNV-64 hex of every byte above this line>".
   Tabs between fields. A file of another world or another player, or a torn or damaged one (its count, its checksum, a row that
   does not read, an area off the 64 x 64 grid, an area named twice) is refused whole.
   Pure: no files, no Windows calls. C++03. */
#include <string>
#include <vector>
#include "mirrorkeep.h"

namespace areafile {

const char* const kFile = "areas-kept.txt";
const char* const kMagic = "swareas";
const unsigned int kVersion = 1u;
const int kGrid = 64;
/* the ring of areas round the file's areas that a join's ask would cover: 2, one wider than the live ring (liverelay.h), because
   the file can be a save or a quit old when it is read */
const int kRing = 2;
const size_t kMaxAreas = 4096;

struct Content { std::vector<int> player, loaded; };   /* x,y pairs */

inline int OnGrid(int x, int y) { return (x >= 0 && x < kGrid && y >= 0 && y < kGrid) ? 1 : 0; }
inline int HasPair(const std::vector<int>& xy, int x, int y)
{
    for (size_t k = 0; k + 1 < xy.size(); k += 2) if (xy[k] == x && xy[k + 1] == y) return 1;
    return 0;
}
inline int SameContent(const Content& a, const Content& b) { return (a.player == b.player && a.loaded == b.loaded) ? 1 : 0; }

/* THE CONTENT OF ONE TICK: the player-character areas in their order (the followed character's first), each once; then the loaded
   areas not already listed, each once, in their order. Areas off the grid are left out; at most kMaxAreas areas in all. */
inline void Build(const int* playerXY, int playerCount, const int* loadedXY, int loadedCount, Content* out)
{
    out->player.clear(); out->loaded.clear();
    std::vector<unsigned char> seen((size_t)(kGrid * kGrid), (unsigned char)0);
    size_t n = 0;
    for (int i = 0; playerXY != 0 && i < playerCount && n < kMaxAreas; ++i)
    {
        const int x = playerXY[2 * i], y = playerXY[2 * i + 1];
        if (OnGrid(x, y) == 0 || seen[(size_t)(x * kGrid + y)] != 0) continue;
        seen[(size_t)(x * kGrid + y)] = 1; out->player.push_back(x); out->player.push_back(y); ++n;
    }
    for (int i = 0; loadedXY != 0 && i < loadedCount && n < kMaxAreas; ++i)
    {
        const int x = loadedXY[2 * i], y = loadedXY[2 * i + 1];
        if (OnGrid(x, y) == 0 || seen[(size_t)(x * kGrid + y)] != 0) continue;
        seen[(size_t)(x * kGrid + y)] = 1; out->loaded.push_back(x); out->loaded.push_back(y); ++n;
    }
}

/* the rows alone (what a write compares with the last one written); *rows = how many */
inline std::string Rows(const Content& c, unsigned long long* rows)
{
    std::string t; unsigned long long n = 0;
    for (size_t k = 0; k + 1 < c.player.size(); k += 2) { t += "p\t" + mirrorkeep::I64Text(c.player[k]) + "\t" + mirrorkeep::I64Text(c.player[k + 1]) + "\n"; ++n; }
    for (size_t k = 0; k + 1 < c.loaded.size(); k += 2) { t += "l\t" + mirrorkeep::I64Text(c.loaded[k]) + "\t" + mirrorkeep::I64Text(c.loaded[k + 1]) + "\n"; ++n; }
    if (rows != 0) *rows = n;
    return t;
}

/* THE FILE. false = not written: no world id or identity, or one with a tab or line break. */
inline bool Format(const std::string& worldId, const std::string& idHash, unsigned long long stamp, const Content& c, std::string* out)
{
    if (worldId.empty() || idHash.empty() || !mirrorkeep::FieldOk(worldId) || !mirrorkeep::FieldOk(idHash)) return false;
    unsigned long long n = 0;
    std::string t = std::string(kMagic) + "\t" + mirrorkeep::U64Text(kVersion) + "\t" + worldId + "\t" + idHash + "\t" + mirrorkeep::U64Text(stamp) + "\n";
    t += Rows(c, &n);
    t += "end\t" + mirrorkeep::U64Text(n) + "\t" + mirrorkeep::Hex16(mirrorkeep::Fnv64(t)) + "\n";
    *out = t;
    return true;
}

enum { kOk = 0, kBadHeader = 1, kOtherWorld = 2, kOtherPlayer = 3, kTorn = 4 };
inline const char* ParseName(int v)
{
    switch (v)
    {
    case kOk: return "ok";
    case kBadHeader: return "its first line is not an area-file header";
    case kOtherWorld: return "it belongs to another world";
    case kOtherPlayer: return "it belongs to another player";
    case kTorn: return "it is torn or damaged";
    }
    return "?";
}
/* Read a file for (worldId, idHash). Anything but kOk leaves *out empty: the whole file is refused. */
inline int Parse(const std::string& text, const std::string& worldId, const std::string& idHash, Content* out, unsigned long long* stamp)
{
    out->player.clear(); out->loaded.clear();
    if (stamp != 0) *stamp = 0ULL;
    const size_t nl = text.find('\n');
    if (nl == std::string::npos) return kBadHeader;
    std::vector<std::string> h;
    mirrorkeep::SplitTabs(text.substr(0, nl), &h);
    unsigned long long st = 0ULL;
    if (h.size() != 5 || h[0] != kMagic || h[1] != mirrorkeep::U64Text(kVersion) || !mirrorkeep::ParseU64(h[4], &st)) return kBadHeader;
    if (worldId.empty() || h[2] != worldId) return kOtherWorld;
    if (idHash.empty() || h[3] != idHash) return kOtherPlayer;
    const std::string::size_type endAt = text.rfind("\nend\t");
    if (endAt == std::string::npos || endAt + 1 <= nl) return kTorn;
    const std::string endLine = text.substr(endAt + 1);
    if (endLine.empty() || endLine[endLine.size() - 1] != '\n') return kTorn;
    std::vector<std::string> e;
    mirrorkeep::SplitTabs(endLine.substr(0, endLine.size() - 1), &e);
    unsigned long long count = 0;
    if (e.size() != 3 || e[0] != "end" || !mirrorkeep::ParseU64(e[1], &count)) return kTorn;
    if (e[2] != mirrorkeep::Hex16(mirrorkeep::Fnv64(text.substr(0, endAt + 1)))) return kTorn;
    Content got;
    std::vector<unsigned char> seen((size_t)(kGrid * kGrid), (unsigned char)0);
    unsigned long long lines = 0;
    size_t at = nl + 1;
    while (at < endAt + 1)
    {
        const size_t lf = text.find('\n', at);
        const std::string line = text.substr(at, lf - at);
        at = lf + 1;
        ++lines;
        std::vector<std::string> f;
        mirrorkeep::SplitTabs(line, &f);
        unsigned long long x = 0, y = 0;
        if (f.size() != 3 || (f[0] != "p" && f[0] != "l") || !mirrorkeep::ParseU64(f[1], &x) || !mirrorkeep::ParseU64(f[2], &y)) return kTorn;
        if (x >= (unsigned long long)kGrid || y >= (unsigned long long)kGrid) return kTorn;
        const size_t cell = (size_t)(x * (unsigned long long)kGrid + y);
        if (seen[cell] != 0) return kTorn;
        seen[cell] = 1;
        std::vector<int>& to = f[0] == "p" ? got.player : got.loaded;
        to.push_back((int)x); to.push_back((int)y);
    }
    if (lines != count) return kTorn;
    if (stamp != 0) *stamp = st;
    out->player.swap(got.player); out->loaded.swap(got.loaded);
    return kOk;
}

/* every area of the content, the player areas first */
inline void AllAreas(const Content& c, std::vector<int>* xy)
{
    *xy = c.player;
    xy->insert(xy->end(), c.loaded.begin(), c.loaded.end());
}
/* how many pairs of a are not pairs of b (the measure's "missing from the file" when a = the live player areas, b = the file's) */
inline long long CountNotIn(const std::vector<int>& a, const std::vector<int>& b)
{
    long long n = 0;
    for (size_t k = 0; k + 1 < a.size(); k += 2) if (HasPair(b, a[k], a[k + 1]) == 0) ++n;
    return n;
}
/* how many area-squares (Chebyshev, no wrap) (x, y) lies from the nearest pair of xy; -1 = xy is empty */
inline int RingDistance(int x, int y, const std::vector<int>& xy)
{
    int best = -1;
    for (size_t k = 0; k + 1 < xy.size(); k += 2)
    {
        int dx = xy[k] - x, dy = xy[k + 1] - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        const int d = dx > dy ? dx : dy;
        if (best < 0 || d < best) best = d;
        if (best == 0) break;
    }
    return best;
}
inline int InRing(int x, int y, const std::vector<int>& xy, int ring) { const int d = RingDistance(x, y, xy); return (d >= 0 && d <= ring) ? 1 : 0; }
/* how many pairs of a lie farther than `ring` from every pair of b (all of a when b is empty) */
inline long long CountBeyondRing(const std::vector<int>& a, const std::vector<int>& b, int ring)
{
    long long n = 0;
    for (size_t k = 0; k + 1 < a.size(); k += 2) if (InRing(a[k], a[k + 1], b, ring) == 0) ++n;
    return n;
}

/* WHY a write was made (the report counts each) */
enum { kWriteChange = 0, kWriteSave = 1, kWriteTeardown = 2, kWriteQuit = 3, kWriteCount = 4 };
inline const char* WriteName(int w)
{
    switch (w)
    {
    case kWriteChange: return "the player or loaded areas changed";
    case kWriteSave: return "a save finished";
    case kWriteTeardown: return "the world is torn down";
    case kWriteQuit: return "the game is quitting";
    }
    return "?";
}

}   /* namespace areafile */
