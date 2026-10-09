#pragma once
// towns2: THE MOD'S 5-DAY REBUILD OF A LOADED TOWN'S GROUPS, MADE TO PICK EXACTLY THE GROUPS THE GAME'S OWN ARRIVAL MARKER PICKS.
// The pure half, ONE header compiled into the plugin (towngen.cpp, store.cpp) and the offline suite.
//
// - THE MARKER (MarkerPass) is the game's own 0x929150, statement for statement: it runs when a town comes into loaded range
//   and sets the rebuild mark Platoon+0x118 = 1 on the groups it picks; a marked group is rebuilt whole from its recipe when
//   it next wakes. Two passes over the town's population manager: the resident list (type 1), then the patrol list (type 2).
//   `total` starts as the living count of BOTH lists (0x8F5B70 over types 2 and 1: every node whose handle is type 1 or 0x22
//   and resolves to a group, whatever else it is). A group is a candidate when it is whole (Platoon+0xC0 == 11) and not a
//   slave group (Platoon+0xD8 != 0); missing = Platoon+0xA4 - living (0x7923F0). It is marked when missing > 0 and
//     residents: (orig x 0.6 <= missing AND missing <= (int)(budget - (float)total)) OR the recipe regenerates (Platoon+0xD9)
//     patrols:    orig x 0.6 <= missing AND missing <= (int)(budget - (float)total)
//   and every mark adds its missing count to `total`, so later groups see a smaller budget. budget = the list's float +0xC.
//   The 0.6 is the engine's double constant 0x3FE3333333333333 and the comparisons are done in the engine's own types.
// - A group the engine itself already marked (Platoon+0x118 == 1) is rebuilt too: the game rebuilds every marked group at its
//   wake, however the mark got there. Two engine calls set that mark: Platoon::declareDead 0x7964F0 on a regenerating group of
//   a living town when its leader dies, and the population manager's remove 0x795F90 (manager, group). That remove finds the
//   group in the list of its squad type (Platoon+0xA8); a group whose recipe regenerates (Platoon+0xD9) is marked and stays
//   listed, any other is erased from the list and, while the list's budget +0xC is above the engine's floor (the float at
//   0x141680B38), its living count (0x7923F0) comes off that budget.
// - THE TOWN: a dead town (its population manager's byte +8, Town vt+0x288 0x9265A0) is never marked - 0x92CCE0 skips the
//   marker for it.
// - A GROUP WITH NOBODY ALIVE is one the engine kept for its rebuild only when its recipe regenerates, it has a home town and
//   that town is not dead (0x7964F0:30-36); otherwise the engine took it out of the town's lists (0x795F90) and declared it
//   dead. EngineKeepsDeadGroup is that test; the mod's wipe-out watch (store.cpp) uses it too, so a kept group is never
//   deleted from the world as wiped out.
// - THE RECIPE'S UNIQUES: a rebuild makes its members from the group's squad template (Platoon+0x108) through createCharacter
//   0x582C50, whose own check 0x591720 refuses a template flagged "unique" only when this game's unique-state entry for it
//   holds the template (+8: set when this game made or loaded it, and when another game's DEAD for it arrives). A unique
//   another game made and holds alive has no such entry here, so a group whose recipe can make a unique with no filled entry
//   on this game is not rebuilt (RecipeUniqueBlocks; verdict uniqueUnknown). The recipe's member lists are the six that
//   createRandomSquad 0x582F80 makes people from (IsMemberList). createRandomSquad also runs itself again on the squad
//   template of every "slaves" / "prisoners" entry (IsSubSquadList); each such template's member lists join the recipe's, and
//   a recipe with a sub-squad whose template was not found or read, or that has sub-squads of its own, is not rebuilt
//   (SubSquadsBlock; verdict subSquads). A recipe read that did not finish names why (RecipeStep).
// - DUE: every 5 in-game days per town (120 in-game hours on the clock every game keeps in step), timed on the game that holds
//   the town; a town first seen by this holder starts its wait then (a holder change restarts it).
// - SIGHT: no player character (this game's own, or a copy of another player's) within kSightUnits of any member (living or
//   dead) or of the group's building - a fixed radius, because the game has no "is visible to a player" test.
#include <cstddef>

namespace coopt2 {

const double kRebuildPeriodHours = 120.0;   // 5 in-game days
const float  kSightUnits = 5000.0f;         // the sight rule's radius (x/z plane, as the refill's nearby rule)
const int    kNullItemType = 11;            // itemType RECORD_NONE: Platoon+0xC0 when the group is whole (the marker's own test)
const double kMarkerShare = 0.6;            // the marker's double at 0x141689248 (bytes 0x3FE3333333333333)

// One node of a population list, as the marker sees it
struct MarkIn
{
    int counted;       // the node's handle is type 1 or 0x22 and resolves to a group (0x8F5B70 counts its living)
    int whole;         // Platoon+0xC0 == kNullItemType
    int canRefresh;    // Platoon+0xD8 != 0 (0 on slave groups)
    int regenerates;   // Platoon+0xD9 != 0
    int orig;          // Platoon+0xA4
    int living;        // 0x7923F0: awake - members not dead; asleep - the stored count Platoon+0xA0
};

// The marker's decision for one list, given the running total. 1 = marked (and *total grows by the missing count).
inline int MarkOne(const MarkIn& g, float budget, int allowRegen, int* total)
{
    if (g.counted == 0 || g.whole == 0 || g.canRefresh == 0) return 0;
    const int missing = g.orig - g.living;
    if (!(0 < missing)) return 0;
    const bool byShare = ((double)g.orig * kMarkerShare <= (double)missing) && (missing <= (int)(budget - (float)*total));
    if (!(byShare || (allowRegen != 0 && g.regenerates != 0))) return 0;
    *total += missing;
    return 1;
}

// The whole marker: residents (nr nodes, budget br) then patrols (np nodes, budget bp). Writes 0/1 per node into
// markRes / markPat and returns how many it marked. townDead != 0 = the town is dead: nothing is marked.
inline int MarkerPass(const MarkIn* res, int nr, float br, const MarkIn* pat, int np, float bp, int townDead, unsigned char* markRes, unsigned char* markPat)
{
    for (int i = 0; i < nr; ++i) markRes[i] = 0;
    for (int i = 0; i < np; ++i) markPat[i] = 0;
    if (townDead != 0) return 0;
    int total = 0;
    for (int i = 0; i < np; ++i) if (pat[i].counted != 0) total += pat[i].living;
    for (int i = 0; i < nr; ++i) if (res[i].counted != 0) total += res[i].living;
    int marked = 0;
    for (int i = 0; i < nr; ++i) if (MarkOne(res[i], br, 1, &total) != 0) { markRes[i] = 1; ++marked; }
    for (int i = 0; i < np; ++i) if (MarkOne(pat[i], bp, 0, &total) != 0) { markPat[i] = 1; ++marked; }
    return marked;
}

// 1 = the engine keeps a group whose leader died, marked for its rebuild (Platoon::declareDead 0x7964F0:30-36)
inline int EngineKeepsDeadGroup(int regenerates, int hasHomeTown, int homeTownDead)
{
    return (regenerates != 0 && hasHomeTown != 0 && homeTownDead == 0) ? 1 : 0;
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

// 1 = the town's version may differ from the one the engine chose when the town last came into loaded range: the town has
// replacement versions ("override town") and a named character's state changed on this game since the town was last seen
// out of loaded range (the engine evaluates versions only at that activation, from those states). hasVersions -1 = unread.
inline int VersionMayChange(int hasVersions, long long genNow, long long genAtInactive)
{
    if (hasVersions == 0) return 0;
    if (hasVersions < 0) return 1;
    return (genNow != genAtInactive) ? 1 : 0;
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

// 1 = a reference list of a squad template that createRandomSquad 0x582F80 makes members from: its six createCharacter
// sites read "leader", "squad", "squad2", "animals", "animals2" and "choosefrom list". `len` is the name's length.
inline int IsMemberList(const char* name, unsigned long long len)
{
    static const char* const k[6] = { "leader", "squad", "squad2", "animals", "animals2", "choosefrom list" };
    if (name == 0) return 0;
    for (int i = 0; i < 6; ++i)
    {
        unsigned long long n = 0;
        while (k[i][n] != 0) ++n;
        if (n != len) continue;
        unsigned long long j = 0;
        while (j < n && name[j] == k[i][j]) ++j;
        if (j == n) return 1;
    }
    return 0;
}

// 1 = a reference list of a squad template whose entries createRandomSquad 0x582F80 runs itself again on, each entry naming
// a squad template: "slaves" (decomp_582f80:1162-1181, 1266) and "prisoners" (:1300, 1417). `len` is the name's length.
inline int IsSubSquadList(const char* name, unsigned long long len)
{
    static const char* const k[2] = { "slaves", "prisoners" };
    if (name == 0) return 0;
    for (int i = 0; i < 2; ++i)
    {
        unsigned long long n = 0;
        while (k[i][n] != 0) ++n;
        if (n != len) continue;
        unsigned long long j = 0;
        while (j < n && name[j] == k[i][j]) ++j;
        if (j == n) return 1;
    }
    return 0;
}

// 1 = the recipe's sub-squads stop the rebuild: it has sub-squad entries (subs) and not every one's squad template was found
// and read (read), or those templates have sub-squads of their own (nested - a level the check does not follow). A sub-squad
// template that was read adds its member lists to the recipe's, judged by RecipeUniqueBlocks like the rest.
inline int SubSquadsBlock(int subs, int read, int nested)
{
    if (subs <= 0) return 0;
    if (read != subs) return 1;
    return (nested != 0) ? 1 : 0;
}

// What one recipe read did: only kRecipeRead lets the recipe be judged; every other code refuses the rebuild and the log
// names which - a fault or a malformed entry range, no squad template, more entries than the plugin's list holds, or an
// unresolved stringID too long for its copy.
enum RecipeStep { kRecipeFault = 0, kRecipeRead, kRecipeNoTemplate, kRecipeOverCap, kRecipeLongSid, kRecipeSteps };

inline const char* RecipeStepName(int s)
{
    static const char* const n[kRecipeSteps] = { "fault", "read", "noTemplate", "overCap", "longSid" };
    return (s >= 0 && s < kRecipeSteps) ? n[s] : "?";
}

// 1 = one template of the recipe stops the rebuild. templateUnique: the template's own "unique" flag (1 / 0 / -1 the read
// faulted); slotHere: this game's unique-state entry for it (1 its template slot is filled - 0x591720 refuses to make it
// again; 0 no entry or an empty slot - the game would make it; -1 the read faulted). Only a filled slot lets a unique through.
inline int RecipeUniqueBlocks(int templateUnique, int slotHere)
{
    if (templateUnique == 0) return 0;
    if (templateUnique < 0) return 1;
    return (slotHere == 1) ? 0 : 1;
}

// What the plugin read of one group (all 0/1 flags except the counts)
struct GroupFacts
{
    int marked;         // the marker picks it this pass, or Platoon+0x118 is already 1
    int living;         // as MarkIn::living
    int awake;          // Platoon+0x1D8 set and its ActivePlatoon+0xF0 set
    int dead;           // Platoon+0x1F0 (the group declared dead)
    int regenerates;    // Platoon+0xD9
    int homeTown;       // Ownerships+0x30 set
    int homeTownDead;   // that town's population manager byte +8
    int uniqueAlive;    // a LIVING member is a unique character (Character::isUnique 0x505ED0), or the test faulted
    int recipeUnique;   // the recipe can make a unique that this game's own check would let it make, or the recipe could not be read
    int carried;        // a member is being carried (Character+0x3D4)
    int player;         // a member belongs to this game's player faction or a stand-in
    int seen;           // a player character is within kSightUnits of a member (living or dead) or the building
    int jailed;         // a member is imprisoned (Character+0x2F8 == 2)
    int held;           // every member (living and dead) stands in a zone this game holds with its zone live
    int subSquads;      // the recipe's sub-squads ("slaves" / "prisoners") stop the rebuild (SubSquadsBlock)
};

enum Verdict { kQueue = 0, kNotMarked, kDead, kAsleep, kNoneAlive, kUniqueAlive, kUniqueUnknown, kSubSquads, kJailed, kCarried, kPlayerGroup, kNotHeld, kSeen, kVerdicts };

inline const char* VerdictName(int v)
{
    static const char* const n[kVerdicts] = { "queue", "notMarked", "dead", "asleep", "noneAlive", "uniqueAlive", "uniqueUnknown", "subSquads", "jailed", "carried", "playerGroup", "notHeld", "seen" };
    return (v >= 0 && v < kVerdicts) ? n[v] : "?";
}

// The decision for one group: the marker's pick first (an unmarked group says nothing), then the refusals that keep the
// sleep and wake safe in a loaded, shared world. ignoreSight 1 = the TEST-only `nosight` variant.
inline int GroupVerdict(const GroupFacts& g, int ignoreSight)
{
    if (g.marked == 0) return kNotMarked;
    if (g.dead != 0) return kDead;
    if (g.awake == 0) return kAsleep;
    if (g.living <= 0 && EngineKeepsDeadGroup(g.regenerates, g.homeTown, g.homeTownDead) == 0) return kNoneAlive;
    if (g.uniqueAlive != 0) return kUniqueAlive;
    if (g.recipeUnique != 0) return kUniqueUnknown;
    if (g.subSquads != 0) return kSubSquads;
    if (g.jailed != 0) return kJailed;
    if (g.carried != 0) return kCarried;
    if (g.player != 0) return kPlayerGroup;
    if (g.held == 0) return kNotHeld;
    if (ignoreSight == 0 && g.seen != 0) return kSeen;
    return kQueue;
}

}  // namespace coopt2
