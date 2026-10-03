#pragma once
// towns2 (user decisions 2026-09-26): TOWN RESIDENT GROUPS DOWN 60% ARE REBUILT BY THE ENGINE'S OWN SLEEP/WAKE.
// The pure half: the 60% rule, the 5-day due timer, the sight rule's distance test and the per-group verdict. ONE header
// compiled into the plugin (towngen.cpp) and the offline suite, so the two cannot hold different ideas of one decision.
//
// - THE 60% RULE (the engine's own, the marker 0x929150: missing >= 0.6 x original, towns2-resident-refill.md T2): a group
//   whose original size is Platoon+0xA4 and whose living members number `living` is due for a rebuild when
//   (orig - living) >= 0.6 x orig. Integer form: 10 x (orig - living) >= 6 x orig, exact for every size.
// - DUE: every 5 in-game days per town (120 in-game hours on the engine clock every game keeps in step), timed on the
//   game that holds the town; a town first seen by this holder starts its wait then (a holder change restarts it).
// - SIGHT: no player character (this game's own, or a copy of another player's) within kSightUnits of any living member
//   or of the group's building - a fixed radius, because the read names no engine "is visible to a player" test.
#include <cstddef>

namespace coopt2 {

const double kRebuildPeriodHours = 120.0;   // 5 in-game days
const float  kSightUnits = 5000.0f;         // the sight rule's radius (x/z plane, as the refill's nearby rule)
const int    kNullItemType = 11;            // itemType RECORD_NONE: Platoon+0xC0, the isSeparatedSquad hand's type, when the group is whole

// 1 = the group has lost at least 60% of its original size. orig <= 0, living < 0 or living > orig = 0 (nothing to measure).
inline int LostEnough(int living, int orig)
{
    if (orig <= 0 || living < 0 || living > orig) return 0;
    return (10 * (orig - living) >= 6 * orig) ? 1 : 0;
}

// -1 = no clock; 1 = due (or forced by the TEST verb); 0 = not yet; 2 = the clock is behind the stamp (a world loaded from an
// older save) - the caller re-stamps; 3 = never stamped on this holder - the caller stamps and the 5 days start now.
inline int RebuildDue(double nowHours, double lastHours, int forced)
{
    if (nowHours < 0.0) return -1;
    if (forced != 0) return 1;
    if (lastHours < 0.0) return 3;
    if (nowHours < lastHours) return 2;
    return (nowHours - lastHours >= kRebuildPeriodHours) ? 1 : 0;
}

// 1 = some point of a is within r of some point of b (x/z plane, squared compare, the edge counts as within)
inline int WithinAny(const float* ax, const float* az, int na, const float* bx, const float* bz, int nb, float r)
{
    const float r2 = r * r;
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < nb; ++j)
        {
            const float dx = ax[i] - bx[j], dz = az[i] - bz[j];
            if (dx * dx + dz * dz <= r2) return 1;
        }
    return 0;
}

// What the plugin read of one group (all 0/1 flags except the counts, sepType and mark)
struct GroupFacts
{
    int orig, living;   // Platoon+0xA4; living members (awake: members not dead; asleep: the stored count +0xA0)
    int awake;          // Platoon+0x1D8 set and its ActivePlatoon+0xF0 set
    int dead;           // Platoon+0x1F0 (the group declared dead)
    int canRefresh;     // Platoon+0xD8 (0 on slave groups - the marker never picks them)
    int sepType;        // Platoon+0xC0 (kNullItemType when whole - the marker's own test)
    int mark;           // Platoon+0x118 (0 none, 1 refill mark; 3 / 4 have other meanings in setupCheck)
    int unique;         // a member (living or dead) is a unique character, or the test faulted
    int carried;        // a member is being carried (Character+0x3D4)
    int player;         // a member belongs to this game's player faction or a stand-in
    int seen;           // a player character is within kSightUnits of a member (living OR dead - review-towns2 1f: a looted corpse) or the building
    int jailed;         // review-towns2 1g: a member is imprisoned (Character+0x2F8 == 2)
    int hasUniques;     // review-towns2 1g: Platoon+0xAC - the recipe can produce a unique (a unique that left the group would be made twice)
};

enum Verdict { kQueue = 0, kNotDamaged, kDead, kAsleep, kSlave, kSeparated, kUnique, kCarried, kPlayerGroup, kMarkOther, kSeen, kNoneAlive, kJailed, kVerdicts };

inline const char* VerdictName(int v)
{
    static const char* const n[kVerdicts] = { "queue", "notDamaged", "dead", "asleep", "slave", "separated", "unique", "carried", "playerGroup", "markOther", "seen", "noneAlive", "jailed" };
    return (v >= 0 && v < kVerdicts) ? n[v] : "?";
}

// The decision for one group, in this order: the 60% rule first (a whole group says nothing), then every refusal.
// ignoreSight 1 = the TEST-only `nosight` variant.
inline int GroupVerdict(const GroupFacts& g, int ignoreSight)
{
    if (LostEnough(g.living, g.orig) == 0) return kNotDamaged;
    if (g.living <= 0) return kNoneAlive;   /* review-towns2 1e: a group with nobody alive is treated as wiped out by the mod's death watch - a sleep would delete it */
    if (g.dead != 0) return kDead;
    if (g.awake == 0) return kAsleep;
    if (g.canRefresh == 0) return kSlave;
    if (g.sepType != kNullItemType) return kSeparated;
    if (g.unique != 0 || g.hasUniques != 0) return kUnique;
    if (g.jailed != 0) return kJailed;
    if (g.carried != 0) return kCarried;
    if (g.player != 0) return kPlayerGroup;
    if (g.mark != 0 && g.mark != 1) return kMarkOther;
    if (ignoreSight == 0 && g.seen != 0) return kSeen;
    return kQueue;
}

}  // namespace coopt2
