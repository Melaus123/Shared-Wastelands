#pragma once
// T-795 step 4 (test-tool row): `townnear` - THE NEAREST TOWN TO THIS GAME'S PLAYER CHARACTER AND ITS DISTANCE, one reply line.
// The pure half, ONE header compiled into the plugin (zones.cpp TownNearCommand, over TownDistances) and the offline suite. The
// harness's `town-far <minDist>` check (tools/run-T102.ps1 Test-SceneReply) reads `dist=<u>` from the reply. Read only: nothing
// in the game is changed. A town whose record name did not read, or whose distance is not a number, is left out; there is no
// distance cap (fallenwire.h NearestTown's 3 km place rule is a different question).
#include <string>
#include <vector>
#include <utility>
#include <cstdio>

namespace swtownnear {

// The index of the nearest named town with a usable distance, or -1 when there is none.
inline int Nearest(const std::vector<std::pair<std::string, float> >& rows)
{
    int best = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].first.empty() || !(rows[i].second >= 0.0f)) continue;
        if (best < 0 || rows[i].second < rows[(size_t)best].second) best = (int)i;
    }
    return best;
}

// The reply: `ok townnear nearest='<name>' dist=<whole world units> towns=<n> faults=<f> from=<x>,<z>`, or `error townnear: ...`
// when no town is named. A ' in a name is written as ` so the quoted name ends where it should.
inline std::string Reply(const std::vector<std::pair<std::string, float> >& rows, unsigned faults, float x, float z)
{
    char tail[96]; tail[sizeof(tail) - 1] = 0;
    _snprintf(tail, sizeof(tail) - 1, " towns=%u faults=%u from=%d,%d", (unsigned)rows.size(), faults, (int)x, (int)z);
    const int k = Nearest(rows);
    if (k < 0) return "error townnear: no named town in the town list" + std::string(tail);
    std::string name = rows[(size_t)k].first;
    for (size_t i = 0; i < name.size(); ++i) if (name[i] == '\'') name[i] = '`';
    char d[32]; d[sizeof(d) - 1] = 0;
    _snprintf(d, sizeof(d) - 1, "%lld", (long long)rows[(size_t)k].second);
    return "ok townnear nearest='" + name + "' dist=" + d + tail;
}

}  // namespace swtownnear
