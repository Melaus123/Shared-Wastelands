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
#include <string>

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

// T-4: the game no longer calls this - the town's clock is its world-server record (towntime.h TtClockStep); kept for its tests.
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

// T-640 D1: what the engine's own version test answered for a town's other versions (coop::TownVerAskOthers, townver.cpp)
const int kVaNone = 0;       // every other version the engine could resolve was tested: none would apply now
const int kVaApplies = 1;    // at least one other version's world states all hold: the chooser would switch it at the next activation
const int kVaUnread = -1;    // not asked (no chooser seen yet, a save loading, the town listed in this load batch, a fault)
// 1 = the town's rebuild walk waits for the town's version to change first. No other version -> never waits. Asked: waits only
// when another version would apply at the next load (the world server's record names one, or the engine's own test says one holds);
// that wait is asked again at every check-up and ends when a later load really switches the town (or the states change back) - being
// seen out of range does not end it; another player's town (stand-in) and a player-owned town answer kVaNone and never wait.
// Unread: the safe side is the stand-in that ran before D1 (VersionMayChange - a named character's state changed since the town was
// last out of range; that wait ends when the town is seen out of loaded range), so the walk never rebuilds a version the engine may
// be about to replace because the test could not run.
inline int VersionAskWait(int hasVersions, int ask, long long genNow, long long genAtInactive)
{
    if (hasVersions == 0) return 0;
    if (ask == kVaApplies) return 1;
    if (ask == kVaNone) return 0;
    return VersionMayChange(hasVersions, genNow, genAtInactive);
}

// TEST-ONLY lever (T-640 run T2333): the option words of `townres kill <town> <n> [res|pat] [regen|noregen] [all|leader]`, read from
// the right. `leader` (DEV build only): only the group's leader dies and its other living members stay - the group needs a living
// leader and one more living member; the base game's own leader-death road then decides the group. 1 = an option (folded into
// *flags; of regen / noregen and of all / leader the leftmost wins, as the words are read right to left), 0 = not an option (the
// count), -1 = `leader` refused (not a DEV build).
const int kKillPat = 1 << 12, kKillRegen = 1 << 13, kKillNoRegen = 1 << 14, kKillAll = 1 << 15, kKillLeader = 1 << 16, kKillCountMask = 0xFFF;
inline int KillOptWord(const std::string& lowerWord, int devBuild, int* flags)
{
    if (lowerWord == "res") { *flags &= ~kKillPat; return 1; }
    if (lowerWord == "pat") { *flags |= kKillPat; return 1; }
    if (lowerWord == "regen") { *flags = (*flags & ~kKillNoRegen) | kKillRegen; return 1; }
    if (lowerWord == "noregen") { *flags = (*flags & ~kKillRegen) | kKillNoRegen; return 1; }
    if (lowerWord == "all") { *flags = (*flags & ~kKillLeader) | kKillAll; return 1; }
    if (lowerWord == "leader") { if (devBuild == 0) return -1; *flags = (*flags & ~kKillAll) | kKillLeader; return 1; }
    return 0;
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

// T-4 fold 2 (owner rule 1560): the one refusal that makes a group OWED - a player sees it. The period walk still finishes (due =
// now + 120 h); the group goes on the town record's owed list and is rebuilt at a later check-up once nobody sees it. Every other
// refusal is a final skip of that group for the period (a living unique member: never rebuilt). Asleep is not owed (it would loop:
// asleep -> wakes when a player comes near -> seen -> the player leaves -> asleep): see AsleepAct.
inline int IsDeferral(int v) { return (v == kSeen) ? 1 : 0; }
// AN ASLEEP PICK. The engine's marker 0x929150 marks asleep groups too (it counts an asleep group's living from the stored
// Platoon+0xA0 - 0x7923F0 - and never tests the wake state, decomp_92926a.txt:24-37, 45-58), and a marked group is rebuilt whole
// from its recipe when it next wakes (setupCheck 0x4FF040, F956). So an asleep pick gets the mark, as the engine's own walk gives
// it, and no sleep/wake pair - unless what cannot be read while it sleeps could hide a refusal: kQueue = set the mark; else the
// final-skip verdict, in GroupVerdict's order (dead, none alive, unique template, sub-squads, jailed, carried, player group, a
// member or the building outside this game's held zones). recipeStep / recipeUniques: the recipe read (kRecipeRead) and its unique templates (any unique template is a
// final skip - its members cannot be read asleep, so a living unique cannot be ruled out); subsBlock: the sub-squad refusal.
inline int AsleepAct(const GroupFacts& g, int recipeStep, int recipeUniques, int subsBlock)
{
    if (g.dead != 0) return kDead;
    if (g.living <= 0 && EngineKeepsDeadGroup(g.regenerates, g.homeTown, g.homeTownDead) == 0) return kNoneAlive;
    if (recipeStep != kRecipeRead || recipeUniques != 0) return kUniqueUnknown;
    if (subsBlock != 0) return kSubSquads;
    if (g.jailed != 0) return kJailed;          // fold 3: every final refusal that applies to an asleep group comes before the mark -
    if (g.carried != 0) return kCarried;        // only sight is not tested for it (the base game rebuilds it at its own wake)
    if (g.player != 0) return kPlayerGroup;
    if (g.held == 0) return kNotHeld;
    return kQueue;
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

// ---- T-4 design (d): a town's walk, kill and 'dead' verdict act only on resident groups THIS game runs ----
// T2323 (Confirmed): on a game that did not make a town's residents, the engine's own check-up set the population manager's dead
// byte (+8) while that game's resident list was empty for a moment (the other game's copies withdrawn), and nothing cleared it.
//
// RunsHere: the writer rule's runner test (store.cpp HbRunnerOfPlatoon - the slot running a strict majority of a group's living
// people; -1 none, split, asleep or unread) and, when it names no slot, the world server's word that another live game runs the
// squad (SwOtherRunner; -1 none); fold 1 (LOW-1): and, when neither names a slot, the list hold (SwListHeld - this link's list of who
// runs which squad has not ended and the squad has a record here: store.cpp SwOtherOrHeld's semantics). 1 = this game runs the group's
// people (or no other game does: an asleep or split group in the area this game holds is woken and written here); 0 = OWED - NOT RUN
// HERE (a copy asleep here before LIST END is not run here - the list may still name its runner).
inline int RunsHere(int runner, int mySlot, int otherRunner, int listHeld)
{
    if (runner >= 0) return runner == mySlot ? 1 : 0;
    if (otherRunner >= 0 && otherRunner != mySlot) return 0;
    if (listHeld != 0) return 0;
    return 1;
}
// The walk's step for a group this game does not run - it is never marked, queued or ended as final: kNrKeepMarked - on the town's
// marked list, it stays there; kNrOwe - on the owed list, or picked in a period walk: carried on the owed list to a later check-up;
// kNrLeave - nothing to carry (not picked, or an owed walk's group that is not on its list).
enum NotRunStep { kNrLeave = 0, kNrOwe, kNrKeepMarked };
inline int NotRunStepOf(int onMarked, int onOwed, int periodWalk, int picked)
{
    if (onMarked != 0) return kNrKeepMarked;
    if (onOwed != 0) return kNrOwe;
    return (periodWalk != 0 && picked != 0) ? kNrOwe : kNrLeave;
}
// A living resident group: not declared dead (Platoon+0x1F0) and a living member (awake: counted; asleep: the stored Platoon+0xA0).
inline int LivingGroup(int dead, int living) { return (dead == 0 && living > 0) ? 1 : 0; }
// The town's verdict at a check-up, from its resident list as THIS game reads it (own groups and copies alike - the list the walk reads):
// kTvAlive - a living resident group, whatever the byte says; or no living resident group with the byte clear (the engine's own
// check did not call it dead - its early exits; fold 1 (MEDIUM-2): an EMPTY list with the byte clear too - the engine's own early
// exit, single player runs its marker then and the patrols are rebuilt, so the town is walked); kTvDead - resident groups, none
// living, and the byte set; kTvNoResidents - the list is empty AND the byte set: NO VERDICT - the walk does not run, the due does not
// move, nothing is ended (T2323, Confirmed: B's list was empty because A's copies had been withdrawn and the byte was set at that very
// check-up - trusting it there is the bug this rule fixes); kTvUnread - the list or a group could not be read and no living group
// was found: no verdict either.
// KNOWN LIMIT: a town truly dead on the game that runs its residents, whose resident list then empties (the engine drops dead groups
// the town no longer keeps), stays without a verdict - its due does not move and its owed and marked lists are kept - until the
// owner's separate decision on the byte (clearing it, or trusting it on the game that runs the residents).
// T-1341 (owner 1614 a): the decision is to clear it on the game that runs the residents when a resident group is intact
// (DeadMarkStepOf below, run before this verdict at every check-up).
enum TownVerdict { kTvAlive = 0, kTvDead, kTvNoResidents, kTvUnread };
inline int TownVerdictOf(int listRead, int residents, int livingGroups, int unreadGroups, int byteDead)
{
    if (listRead == 0) return kTvUnread;
    if (livingGroups > 0) return kTvAlive;
    if (unreadGroups > 0) return kTvUnread;
    if (residents <= 0) return byteDead != 0 ? kTvNoResidents : kTvAlive;   /* fold 1 (MEDIUM-2): empty + clear = the engine's early exit */
    return byteDead != 0 ? kTvDead : kTvAlive;
}
// fold 1 (LOW-2): a dead town's walk ends an owed or marked id as FINAL only when it is a resident group read here that this game
// runs; a patrol id, a not-run id, or an id not read in the resident list stays on its list (owed stays owed, marked stays marked) -
// a later check-up on the game that runs it decides.
inline int DeadTownEndsFinal(int onResidentList, int runsHere) { return (onResidentList != 0 && runsHere != 0) ? 1 : 0; }
inline const char* TownVerdictName(int v) { return v == kTvAlive ? "alive" : v == kTvDead ? "dead" : v == kTvNoResidents ? "noResidents" : v == kTvUnread ? "unread" : "?"; }
// 1 = the engine's byte and the living count disagree (the byte set with a living resident group, or clear over resident groups none
// of them living); a list that gives no verdict does not disagree, nor does an empty list with the byte clear (they agree: nothing
// living, and the engine's early exit did not call it dead).
inline int TownByteDisagrees(int listRead, int residents, int livingGroups, int unreadGroups, int byteDead)
{
    const int v = TownVerdictOf(listRead, residents, livingGroups, unreadGroups, byteDead);
    if (v == kTvNoResidents || v == kTvUnread) return 0;
    if (residents <= 0) return 0;
    return ((byteDead != 0) != (livingGroups == 0)) ? 1 : 0;
}

// ---- T-1341 design (a) (owner 1614 a): the game that runs a town's people clears the engine's wrong 'dead' mark at the check-up ----
// The population manager's byte +8 (Town+0xE0 -> +8) is set to 1 only by 0x926570 and never cleared by the engine; once set, the
// engine's own check-up 0x8F6BB0 exits early and never judges the town again. Straight after the engine's own check-up, the game that
// holds the town's area, where at least one resident group answers RunsHere = 1 (a group this game runs, or one no runner is known for:
// no runner slot, no world-server note, no list hold), clears the byte when at least one resident group, its own or a copy, is intact
// by the facts the engine's own intact test (Platoon 0x7ABC10) uses - GroupIntact below, read only, the engine's test never called (it
// can declare a group dead on an awake group). A truly dead town - no intact resident group - is never cleared, so the engine re-sets
// the byte only where this rule would not clear it (the re-set counter watches for a loop).
// The engine's intact facts (0x7ABC10): not declared dead (Platoon+0x1F0); awake (an ActivePlatoon): a living member; asleep: the
// cached intact byte Platoon+0x100.
inline int GroupIntact(int dead, int awake, int living, int cachedIntact)
{
    if (dead != 0) return 0;
    if (awake != 0) return living > 0 ? 1 : 0;
    return cachedIntact != 0 ? 1 : 0;
}
enum DeadMarkStep { kDmByteClear = 0, kDmUnread, kDmNotHeld, kDmRunsNone, kDmNoIntact, kDmClear };
// runsHereGroups: resident groups with RunsHere = 1 (run here, or no runner known); intactOwn / intactCopy: intact groups among them / not
inline int DeadMarkStepOf(int byteDead, int listRead, int held, int runsHereGroups, int intactOwn, int intactCopy)
{
    if (byteDead == 0) return kDmByteClear;
    if (listRead == 0) return kDmUnread;
    if (held == 0) return kDmNotHeld;
    if (runsHereGroups <= 0) return kDmRunsNone;
    if (intactOwn + intactCopy <= 0) return kDmNoIntact;
    return kDmClear;
}
inline const char* DeadMarkStepName(int s)
{
    return s == kDmByteClear ? "byteClear" : s == kDmUnread ? "unread" : s == kDmNotHeld ? "notHeld" : s == kDmRunsNone ? "runsNone" : s == kDmNoIntact ? "noIntact" : s == kDmClear ? "clear" : "?";
}
// 1 = the byte is found set again at a check-up of a town this game cleared earlier (the loop watch: counted deadReSet)
inline int DeadMarkReSet(int byteDead, int clearedBefore) { return (byteDead != 0 && clearedBefore != 0) ? 1 : 0; }

}  // namespace coopt2
