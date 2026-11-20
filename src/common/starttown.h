/* src/common/starttown.h - T-1365 stage 2 (owner decision 1675, world-server protocol 157): A NEW GAME'S START TOWN -> ITS AREAS.

   WHY. A NEW GAME is held under the loading screen until the world server's records are in (store.cpp PreloadHoldsNewGame). The game
   knows its start record before the load, and that record's 'town' reference names the town's record (T2393, Confirmed) - but no
   position: a town record carries none, the engine places it when the world is built. The world server does know where each town
   stands: every area file a game writes carries the "built with" note (townverwire.h kTvNoteId), whose keys are the placed towns of
   that area as "<town record>@<x>,<z>" (TvKey: the town's own record and its live position in whole metres, under the writer's mod
   list - so a modded town's id and position come out as that world places it). So the game's INDEX_ASK names the start record's
   town, and the world server answers on its first INDEX_PAGE with the areas holding a placement of that town: the area of each key's
   own x,z (never the note's area field), every placement of a town placed more than once. The game then fetches the records within
   cooppre::kPreRing of those areas (and every record with no area); a town no note names gives no areas, and the game fetches every
   area record (preload.h PreNewGameClass - the owner's fallback).

   THE INDEX (world server, store_main.cpp StartTownAreas): per area record its note's placed-town keys, read again only when that
   record's file time changed, a record no longer held dropped - asked only at a NEW GAME's first index page.

   Pure: no Windows call, no file touched; every read bounds-checked (zonekeep.h ParseContainer). Header-only. No C++11. */
#ifndef COOP_COMMON_STARTTOWN_H
#define COOP_COMMON_STARTTOWN_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include "zonekeep.h"      /* ParseContainer (and poskey.h SectorOf) */
#include "townverwire.h"   /* kTvNoteId, kTvNoteCap */

namespace starttown {

const int kGrid = 64;

/* A PLACED-TOWN KEY (townverwire.h TvKey: "<sid>@<x>,<z>", whole metres, either sign): 1 and its parts, 0 = not that form. The sid is
   everything before the LAST '@' (a mod's record id may hold any other character). */
inline int ParsePlacedKey(const std::string& key, std::string* sid, int* x, int* z)
{
    const size_t at = key.rfind('@');
    if (at == std::string::npos || at == 0) return 0;
    const size_t comma = key.find(',', at + 1);
    if (comma == std::string::npos) return 0;
    long v[2] = { 0, 0 };
    const size_t from[2] = { at + 1, comma + 1 }, to[2] = { comma, key.size() };
    for (int k = 0; k < 2; ++k)
    {
        size_t i = from[k];
        int neg = 0;
        if (i < to[k] && key[i] == '-') { neg = 1; ++i; }
        if (i >= to[k] || to[k] - i > 7) return 0;
        long n = 0;
        for (; i < to[k]; ++i) { if (key[i] < '0' || key[i] > '9') return 0; n = n * 10 + (key[i] - '0'); }
        v[k] = neg ? -n : n;
    }
    *sid = key.substr(0, at); *x = (int)v[0]; *z = (int)v[1];
    return 1;
}
/* the area of a placed key's own position (poskey.h SectorOf, the engine's formula) */
inline void AreaOfPlaced(int x, int z, int* ax, int* ay) { poskey::SectorOf((float)x, (float)z, ax, ay); }

/* THE NOTE OF ONE AREA RECORD (its file's bytes): 1 and the placed-town keys it names (k0 .. k<n-1>, n = its count field, at most
   kTvNoteCap), 0 = no note (an EMPTY mark, a file written before notes, an area with no town, or a note record whose "file" flag is
   not "1" - a record left in memory from before, townverwire.h), -1 = a file that does not read. A file that does not carry the note's
   id anywhere is not parsed. */
inline int NoteKeysOf(const unsigned char* b, size_t n, std::vector<std::string>* keys)
{
    keys->clear();
    const std::string id(cooptv::kTvNoteId);
    if (b == 0 || n < id.size()) return 0;
    bool seen = false;
    for (size_t i = 0; i + id.size() <= n && !seen; ++i) if (b[i] == (unsigned char)id[0] && memcmp(b + i, id.data(), id.size()) == 0) seen = true;
    if (!seen) return 0;
    zonekeep::Container c; std::string why;
    if (!zonekeep::ParseContainer(b, n, &c, &why)) return -1;
    for (size_t r = 0; r < c.recs.size(); ++r)
    {
        if (c.recs[r].sid != id) continue;
        const std::map<std::string, std::string>::const_iterator ff = c.recs[r].strs.find("file"), nf = c.recs[r].strs.find("n");
        if (ff == c.recs[r].strs.end() || ff->second != "1") return 0;   /* review L2: not a note written with this file */
        const int count = nf == c.recs[r].strs.end() ? -1 : cooptv::TvNoteCountParse(nf->second);
        for (int i = 0; i < count; ++i)
        {
            char kk[16]; _snprintf(kk, sizeof(kk) - 1, "k%d", i); kk[sizeof(kk) - 1] = 0;
            const std::map<std::string, std::string>::const_iterator f = c.recs[r].strs.find(kk);
            if (f != c.recs[r].strs.end() && !f->second.empty()) keys->push_back(f->second);
        }
        return 1;
    }
    return 0;
}

/* THE INDEX: per area record id, the file time its note was read at and the placed-town keys it named. */
struct Entry { long long stamp; int read; std::vector<std::string> keys; Entry() : stamp(0), read(0) {} };
struct Index { std::map<std::string, Entry> rec; };
/* 1 = the record's note must be read (never read, or its file time changed) */
inline int NeedsRead(const Index& ix, const std::string& id, long long stamp)
{
    const std::map<std::string, Entry>::const_iterator it = ix.rec.find(id);
    return (it == ix.rec.end() || it->second.read == 0 || it->second.stamp != stamp) ? 1 : 0;
}
inline void Set(Index* ix, const std::string& id, long long stamp, const std::vector<std::string>& keys)
{
    Entry& e = ix->rec[id]; e.stamp = stamp; e.read = 1; e.keys = keys;
}
/* drops every record not in `held` (the area records the world server holds now) */
inline void KeepOnly(Index* ix, const std::set<std::string>& held)
{
    for (std::map<std::string, Entry>::iterator it = ix->rec.begin(); it != ix->rec.end(); ) { if (held.count(it->first) == 0) ix->rec.erase(it++); else ++it; }
}
/* THE START TOWN'S AREAS: x,y pairs (each area once, in grid order), from every placement of `town` any note names; *placements =
   the distinct placed keys of it. An empty town names nothing. */
inline void TownAreas(const Index& ix, const std::string& town, std::vector<int>* xy, int* placements)
{
    xy->clear(); *placements = 0;
    if (town.empty()) return;
    std::set<std::string> keys; std::set<int> areas;
    for (std::map<std::string, Entry>::const_iterator it = ix.rec.begin(); it != ix.rec.end(); ++it)
        for (size_t k = 0; k < it->second.keys.size(); ++k)
        {
            std::string sid; int x = 0, z = 0;
            if (ParsePlacedKey(it->second.keys[k], &sid, &x, &z) != 1 || sid != town) continue;
            keys.insert(it->second.keys[k]);
            int ax = 0, ay = 0; AreaOfPlaced(x, z, &ax, &ay);
            if (ax >= 0 && ax < kGrid && ay >= 0 && ay < kGrid) areas.insert(ax * kGrid + ay);
        }
    for (std::set<int>::const_iterator a = areas.begin(); a != areas.end(); ++a) { xy->push_back(*a / kGrid); xy->push_back(*a % kGrid); }
    *placements = (int)keys.size();
}

}   /* namespace starttown */

#endif
