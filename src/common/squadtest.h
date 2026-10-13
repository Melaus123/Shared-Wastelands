// statstest: the argument shape of a TEST-ONLY lever and the stat levels it writes. Pure; offline tests in
// src/coop-test/test_main.cpp.
//
//   statstest squad <level> | statstest show <uid>   (stats.cpp StatsTestLever)
//
// statstest squad: every character of this game's own squad gets its 39 skills and attributes plus attack and defence (the
// first kStatsLevelCount values of the 44-value stats block, src/common/statswire.h) set to level (1..kStatsLevelMax);
// warrior spirit, xp and the free attribute points are left as they are. statstest show: a read of any character's main
// combat values here.
#pragma once

#include "statswire.h"
#include <cstring>
#include <sstream>
#include <string>

namespace swsquad {

const int kStatsLevelCount = 41;    // block indices 0..38 (skills and attributes), 39 attack, 40 defence
const int kStatsLevelMax = 100;

// A decimal uid 1..0xFFFFFFFF, or 0 when the text is not one.
inline unsigned int SquadUidText(const std::string& t)
{
    if (t.empty() || t.size() > 10) return 0;
    unsigned long long wide = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9') return 0;
        wide = wide * 10 + (unsigned long long)(t[i] - '0');
    }
    return (wide == 0 || wide > 0xFFFFFFFFull) ? 0 : (unsigned int)wide;
}

// A small decimal integer lo..hi, or -1.
inline int SquadIntText(const std::string& t, int lo, int hi)
{
    if (t.empty() || t.size() > 3) return -1;
    int v = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9') return -1;
        v = v * 10 + (t[i] - '0');
    }
    return (v < lo || v > hi) ? -1 : v;
}

enum { kStatsTestBad = 0, kStatsTestSquad = 1, kStatsTestShow = 2 };

// `squad <level>` -> kStatsTestSquad with *level; `show <uid>` -> kStatsTestShow with *uid; else kStatsTestBad (outputs
// untouched).
inline int StatsTestParse(const std::string& args, int* level, unsigned int* uid)
{
    std::istringstream is(args);
    std::string what, arg, extra;
    if (!(is >> what) || !(is >> arg) || (is >> extra)) return kStatsTestBad;
    if (what == "squad")
    {
        const int v = SquadIntText(arg, 1, kStatsLevelMax);
        if (v < 0) return kStatsTestBad;
        if (level != 0) *level = v;
        return kStatsTestSquad;
    }
    if (what == "show")
    {
        const unsigned int u = SquadUidText(arg);
        if (u == 0) return kStatsTestBad;
        if (uid != 0) *uid = u;
        return kStatsTestShow;
    }
    return kStatsTestBad;
}

// Block indices statstest show prints (CharStats::updateStats 0x64AC10 reads them by these names).
const int kStatsIdxStrength = 0, kStatsIdxDexterity = 2, kStatsIdxToughness = 4, kStatsIdxKatana = 30, kStatsIdxAttack = 39,
          kStatsIdxDefence = 40;

// Writes level as a float into block values 0..kStatsLevelCount-1 of raw[44]; the rest are left as they are.
inline void StatsLevelApply(unsigned int* raw, int level)
{
    const float f = (float)level;
    unsigned int bits = 0;
    std::memcpy(&bits, &f, 4);
    for (int i = 0; i < kStatsLevelCount && i < coopstats::kStatsCount; ++i) raw[i] = bits;
}

} // namespace swsquad
