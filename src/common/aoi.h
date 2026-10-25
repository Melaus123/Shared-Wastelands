/* T-985 step 2A (owner decision 1023): AREA OF INTEREST - THE MEASURING SLICE.

   A game should, in a later step, get whole world records only for the areas it has loaded plus a ring of areas around them. This
   step changes nothing any game receives: the world server only COUNTS, for every RECORD it sends a game, whether that record lies
   inside the game's interest set for a ring of 1, 2 and 3 areas, outside it (what a filter would hold back), or has no usable
   position. Pure: no engine, no Windows call, no ENet. Header-only. No C++11.

   - AreaOfPos: a record's area key (cooplive::AreaKey of poskey::SectorOf - the engine's own sector formula, clamped into the
     64 x 64 grid), or kNoArea when the position is not usable: not finite, beyond +-1e7, or exactly 0,0,0 (a record that never
     carried a position).
   - RingDistance: how many area-squares (Chebyshev, no wrap at the map's edge) an area lies from the nearest loaded area.
     "Inside the interest set for ring K" = RingDistance <= K - the same set cooplive::AreaDilate(loaded, K) builds for LIVE.
   - Classify / Tally: per road (preload / feed / forward) and ring, records and bytes in and out; noArea; unknown (the game has
     sent no area report yet, or its last one was empty - it is not in a world).
   - Ledger: which record ids one game has been sent, at which stamp, on which road first and with how many bytes, capped; a new
     id past the cap is counted, not kept. Keyed by IdHash (64-bit FNV-1a of the id), not the id's text.
   - PreSent: the preload road's sends of one game, kept apart until its first non-empty area report scores them.
   - ScoreDeferred: the join download scored later, against the joiner's first non-empty area report (the preload runs before
     the game reports any areas, so its live count is almost all "unknown").
   - AreaIndex: area -> record ids; kNoArea is the bucket for records with no usable position. */
#ifndef AOI_H_T985S2A
#define AOI_H_T985S2A

#include <stddef.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "liverelay.h"
#include "poskey.h"

namespace aoi {

const int kNoArea = -1;
const int kNotIndexed = -2;                       /* AreaIndex::AreaOf: an id the index does not hold */
const int kRoadPreload = 0, kRoadFeed = 1, kRoadForward = 2, kRoadCount = 3;
const int kRingMax = 3;                           /* rings 1, 2 and 3 are counted side by side */
const size_t kLedgerCap = 100000;                 /* ids per game */

inline bool PosUsable(float v) { return v > -1.0e7f && v < 1.0e7f; }   /* a NaN fails both comparisons */

inline int AreaOfPos(float x, float y, float z)
{
    if (!PosUsable(x) || !PosUsable(y) || !PosUsable(z)) return kNoArea;
    if (x == 0.0f && y == 0.0f && z == 0.0f) return kNoArea;
    int sx = 0, sy = 0;
    poskey::SectorOf(x, z, &sx, &sy);
    const int k = cooplive::AreaKey(sx, sy);
    return k >= 0 ? k : kNoArea;
}

/* -1 = a bad area key, or no valid loaded area */
inline int RingDistance(int area, const std::set<int>& loaded)
{
    if (area < 0 || area >= cooplive::kAreaKeyCount) return -1;
    const int x = cooplive::AreaKeyX(area), y = cooplive::AreaKeyY(area);
    int best = -1;
    for (std::set<int>::const_iterator it = loaded.begin(); it != loaded.end(); ++it)
    {
        if (*it < 0 || *it >= cooplive::kAreaKeyCount) continue;
        int dx = cooplive::AreaKeyX(*it) - x, dy = cooplive::AreaKeyY(*it) - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        const int d = dx > dy ? dx : dy;
        if (best < 0 || d < best) best = d;
        if (best == 0) break;
    }
    return best;
}

enum { kIn = 0, kOut = 1, kNoAreaClass = 2, kUnknown = 3 };

/* loaded = the game's last reported loaded areas, or 0 when it has reported none */
inline int Classify(int area, const std::set<int>* loaded, int ring)
{
    if (area < 0) return kNoAreaClass;
    if (loaded == 0 || loaded->empty()) return kUnknown;
    const int d = RingDistance(area, *loaded);
    return (d >= 0 && d <= ring) ? kIn : kOut;
}

struct Cell
{
    long long recs, bytes;
    Cell() : recs(0), bytes(0) {}
    void Add(size_t b) { ++recs; bytes += (long long)b; }
};

struct Tally
{
    Cell in[kRoadCount][kRingMax], out[kRoadCount][kRingMax], noArea[kRoadCount], unknown[kRoadCount];
    long long total;   /* records counted - the report line's "changed" test */
    Tally() : total(0) {}
    void Count(int road, int area, const std::set<int>* loaded, size_t bytes)
    {
        if (road < 0 || road >= kRoadCount) return;
        ++total;
        for (int k = 0; k < kRingMax; ++k)
        {
            const int c = Classify(area, loaded, k + 1);
            if (c == kNoAreaClass) { noArea[road].Add(bytes); return; }
            if (c == kUnknown) { unknown[road].Add(bytes); return; }
            if (c == kIn) in[road][k].Add(bytes); else out[road][k].Add(bytes);
        }
    }
    void Merge(const Tally& o)
    {
        total += o.total;
        for (int r = 0; r < kRoadCount; ++r)
        {
            for (int k = 0; k < kRingMax; ++k)
            {
                in[r][k].recs += o.in[r][k].recs; in[r][k].bytes += o.in[r][k].bytes;
                out[r][k].recs += o.out[r][k].recs; out[r][k].bytes += o.out[r][k].bytes;
            }
            noArea[r].recs += o.noArea[r].recs; noArea[r].bytes += o.noArea[r].bytes;
            unknown[r].recs += o.unknown[r].recs; unknown[r].bytes += o.unknown[r].bytes;
        }
    }
};

/* T-985 step 2A review fold (MED 1): the ledger keys a record by a 64-bit FNV-1a hash of its id, not the id's text (a full ledger
   of id strings was some 15-17 MB per game). Two ids that hash alike would share one row: for a MEASUREMENT that is a tolerable
   and very rare miscount, never a decision about what a game is sent. */
inline unsigned long long IdHash(const std::string& id)
{
    unsigned long long h = 14695981039346656037ULL;
    for (size_t i = 0; i < id.size(); ++i) { h ^= (unsigned char)id[i]; h *= 1099511628211ULL; }
    return h;
}

struct LedgerEntry
{
    unsigned long long stamp;   /* the stamp of the last RECORD sent for the id */
    int road;                   /* the road it was FIRST sent on */
    long long bytes;            /* bytes sent for it on every road */
    LedgerEntry() : stamp(0), road(-1), bytes(0) {}
};

struct Ledger
{
    std::map<unsigned long long, LedgerEntry> sent;   /* IdHash(id) -> its row */
    long long adds, updates, overflow;
    Ledger() : adds(0), updates(0), overflow(0) {}
    /* 1 = a new id, 2 = an id it held (its stamp replaced), 0 = a new id refused: `cap` ids are held already (counted) */
    int Note(const std::string& id, unsigned long long stamp, size_t cap, int road, size_t b)
    {
        const unsigned long long h = IdHash(id);
        std::map<unsigned long long, LedgerEntry>::iterator it = sent.find(h);
        if (it != sent.end()) { it->second.stamp = stamp; it->second.bytes += (long long)b; ++updates; return 2; }
        if (sent.size() >= cap) { ++overflow; return 0; }
        LedgerEntry& e = sent[h];
        e.stamp = stamp; e.road = road; e.bytes = (long long)b;
        ++adds;
        return 1;
    }
    const LedgerEntry* Find(const std::string& id) const
    {
        std::map<unsigned long long, LedgerEntry>::const_iterator it = sent.find(IdHash(id));
        return it == sent.end() ? 0 : &it->second;
    }
    void Clear() { sent.clear(); adds = 0; updates = 0; overflow = 0; }
};

/* (MED 2) one preload-road send of one game: kept in the game's own list until its first non-empty area report scores it
   (ScoreDeferred), then the list is freed - the score never walks the whole ledger */
struct PreSent
{
    std::string id;
    long long bytes;
    PreSent(const std::string& i, long long b) : id(i), bytes(b) {}
};

struct AreaIndex
{
    std::map<int, std::set<std::string> > byArea;   /* kNoArea = the no-position bucket */
    std::map<std::string, int> areaOf;
    void Put(const std::string& id, int area)
    {
        if (area < 0) area = kNoArea;
        std::map<std::string, int>::iterator it = areaOf.find(id);
        if (it != areaOf.end())
        {
            if (it->second == area) return;
            DropFrom(it->second, id);
            it->second = area;
        }
        else areaOf[id] = area;
        byArea[area].insert(id);
    }
    void Erase(const std::string& id)
    {
        std::map<std::string, int>::iterator it = areaOf.find(id);
        if (it == areaOf.end()) return;
        DropFrom(it->second, id);
        areaOf.erase(it);
    }
    int AreaOf(const std::string& id) const
    {
        std::map<std::string, int>::const_iterator it = areaOf.find(id);
        return it == areaOf.end() ? kNotIndexed : it->second;
    }
    size_t CountIn(int area) const
    {
        std::map<int, std::set<std::string> >::const_iterator b = byArea.find(area < 0 ? kNoArea : area);
        return b == byArea.end() ? 0 : b->second.size();
    }
    size_t Ids() const { return areaOf.size(); }
    size_t Areas() const { return byArea.size() - (byArea.count(kNoArea) != 0 ? 1 : 0); }   /* areas with a position */
    void DropFrom(int area, const std::string& id)
    {
        std::map<int, std::set<std::string> >::iterator b = byArea.find(area);
        if (b == byArea.end()) return;
        b->second.erase(id);
        if (b->second.empty()) byArea.erase(b);
    }
};

/* THE DEFERRED SCORE (the join download against the joiner's first non-empty area report): every preload send in `pre` is
   classified by its record's CURRENT area in `ix` (an id the index no longer holds - deleted since - counts as noArea) against
   `loaded`, rings 1-3, with its bytes, into `out` under kRoadPreload. Returns the sends scored; `loaded` empty scores nothing.
   A record fetched twice is two sends. The caller frees `pre` afterwards. */
inline long long ScoreDeferred(const std::vector<PreSent>& pre, const AreaIndex& ix, const std::set<int>& loaded, Tally* out)
{
    if (out == 0 || loaded.empty()) return 0;
    for (size_t i = 0; i < pre.size(); ++i)
    {
        int area = ix.AreaOf(pre[i].id);
        if (area < 0) area = kNoArea;
        out->Count(kRoadPreload, area, &loaded, (size_t)pre[i].bytes);
    }
    return (long long)pre.size();
}

}   /* namespace aoi */

#endif
