/* src/common/squadorigin.h - WHICH GAME A SQUAD'S OWNERSHIP DECISIONS ARE TAKEN FOR.
 *
 * ONE RULE, the one standinpurge.h keeps: nothing in here reads engine memory, reads a global or calls the operating
 * system. A pure function of its arguments, so the offline suite drives every branch.
 *
 * WHY. A squad id is "<faction name>_<n>", and the number blocks hand <n> out in blocks of 65,536 per relay slot, so
 * n / 65,536 is the slot whose game numbered the squad (store.cpp GroupBlockOwner). That answers "who made it" only for a
 * squad made since this world was loaded. Every squad the SAVE holds was numbered before any block was applied, so its
 * number sits in slot 0's block whoever runs it now, and the game in slot 0 would treat every save squad as its own.
 *
 * THE LOAD FLOOR. Each faction's squad counter (the number the engine hands the next squad) is read once at the first
 * running tick after a load, before any block raise. A number AT OR ABOVE that floor was handed out since the load, so
 * its block is the maker: the number rule stands (the stale-reload guard depends on it). A number BELOW the floor came
 * from the save: the squad is decided by the game that runs it now, when that is known; when it is not, the number rule
 * stands, so nothing is decided worse than before.
 *
 * WHO RUNS IT NOW, in this order (the first that names a slot): the squad was handed to another game (the hand-over
 * index); the squad's members alive here now (a strict majority of the members whose runner is known); the holder of
 * the area at the squad's position; the game that wrote the newest record of it, while that game is in the world.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no lambdas.
 */
#ifndef SQUADORIGIN_H_INCLUDED
#define SQUADORIGIN_H_INCLUDED

namespace sqorigin {

/* where the decision came from: the number rule (at or above the floor), one of the four runner sources, or the number
   rule because nothing better was known (no floor for the faction, no runner known) */
const int kByFloor = 0, kByHandover = 1, kByMembers = 2, kByHolder = 3, kByRecord = 4, kFallback = 5;
const int kWhyCount = 6;
const int kMaxSlots = 16;   /* the number blocks' cap: 16 blocks of 65,536 */

inline const char* WhyWord(int why)
{
    return why == kByFloor ? "number" : why == kByHandover ? "handed over" : why == kByMembers ? "its members"
         : why == kByHolder ? "area holder" : why == kByRecord ? "record writer" : "number, nothing better known";
}

/* a real slot: 0..15 */
inline int SlotOk(int s) { return (s >= 0 && s < kMaxSlots) ? 1 : 0; }

/* the slot that runs a strict majority of the members whose runner is known (counts[s] = members run by slot s);
   -1 = no member known, or no slot holds more than half of them */
inline int MembersMajority(const int* counts, int nSlots)
{
    int total = 0, best = -1, bestN = 0;
    for (int s = 0; s < nSlots && s < kMaxSlots; ++s)
    {
        if (counts[s] <= 0) continue;
        total += counts[s];
        if (counts[s] > bestN) { bestN = counts[s]; best = s; }
    }
    if (total <= 0 || best < 0) return -1;
    return (bestN * 2 > total) ? best : -1;
}

/* the runner from the four sources, in order; -1 = none known. *why = the source that answered (kFallback when none). */
inline int RunBy(int handoverSlot, int membersSlot, int holderSlot, int recordSlot, int* why)
{
    if (SlotOk(handoverSlot)) { *why = kByHandover; return handoverSlot; }
    if (SlotOk(membersSlot))  { *why = kByMembers;  return membersSlot; }
    if (SlotOk(holderSlot))   { *why = kByHolder;   return holderSlot; }
    if (SlotOk(recordSlot))   { *why = kByRecord;   return recordSlot; }
    *why = kFallback; return -1;
}

/* the load floor of a faction name several factions share (a stand-in faction carries another player's faction name):
   the LOWEST counter, so a number is called "from the save" only when it is below every one of them - a squad of the
   higher-counter faction that is wrongly called "made here" keeps the number rule, which is no worse than before */
inline unsigned FloorMerge(int haveFloor, unsigned floorNow, unsigned counter)
{
    return (haveFloor == 0 || counter < floorNow) ? counter : floorNow;
}

/* the slot this game takes a squad's ownership decisions for. number = the id's number (its block is number / 65,536);
   floorKnown / floorValue = the load floor of the id's faction; blockOwner = number / 65,536 (-1 = the id has no number);
   runBySlot = who runs it now (-1 none known). *why = kByFloor, kFallback, or the caller's runner source (runWhy). */
inline int SquadOrigin(unsigned number, int floorKnown, unsigned floorValue, int blockOwner, int runBySlot, int runWhy, int* why)
{
    if (blockOwner < 0) { *why = kFallback; return blockOwner; }
    if (floorKnown == 0) { *why = kFallback; return blockOwner; }
    if (number >= floorValue) { *why = kByFloor; return blockOwner; }
    if (!SlotOk(runBySlot)) { *why = kFallback; return blockOwner; }
    *why = runWhy; return runBySlot;
}

/* the same-id supersede (spawn.cpp PrepareContextForSpawn): an origin named by the judged copy's OWN members (why kByMembers, a save
   squad below the floor) cannot vouch for that copy, so the two games' copies are weighed on the AGE of each game's current world load
   (owner decision 826 a: a reloaded copy is retired; when both copies are fresh the host's - the lower seat's - copy stays).
   ownThisLoad 1: every session member of this game's copy was picked up by this game's sweep from its CURRENT world load (standinpurge.h
   ReloadRetireDecide's member test, without its link-down condition). ownAgeMs / peerAgeMs: milliseconds since this game's current
   world load finished, and since the announcer's (carried on its CONTEXT, brought forward to the same moment); kLoadAgeUnknown =
   unknown. mySeat / peerSeat: this game's seat and the announcer's (-1 unknown).
   kOwnIdLifted: the refusal does not hold and this game's copy is retired as the stale one - this game's load is younger than the
   announcer's by more than kLoadAgeTieMs (it reloaded in game, or restarted after a crash, while the other game ran on), or the two loads
   are within kLoadAgeTieMs of each other and this game's seat is the higher one. kOwnIdKeptLowerSeat: within kLoadAgeTieMs and this
   game's seat is the lower one - its copy stays (refused). kOwnIdKept: anything else - this game's load is the older one, its copy
   holds a person from the session or an earlier load, or an age or a seat is unknown (fail closed: no lift). Never both copies: of two
   games answering each other, at most one lifts. */
const int kOwnIdKept = 0, kOwnIdLifted = 1, kOwnIdKeptLowerSeat = 2;
const unsigned int kLoadAgeUnknown = 0xFFFFFFFFu;
/* Two loads this close are one session start: the two games load the same save side by side and finish seconds apart (machine speed,
   when each player clicked), and the CONTEXT's own trip makes the announcer's age read a little young. An in-game reload, or a crash
   and restart, puts the new load at least a whole load's time (tens of seconds) behind a game that ran on. */
const unsigned int kLoadAgeTieMs = 5000;
inline int OwnIdLiftedByLoadAge(int why, int ownThisLoad, unsigned int ownAgeMs, unsigned int peerAgeMs, int mySeat, int peerSeat)
{
    if (why != kByMembers || ownThisLoad == 0) return kOwnIdKept;
    if (ownAgeMs == kLoadAgeUnknown || peerAgeMs == kLoadAgeUnknown || mySeat < 0 || peerSeat < 0) return kOwnIdKept;
    if (peerAgeMs > ownAgeMs && peerAgeMs - ownAgeMs > kLoadAgeTieMs) return kOwnIdLifted;   /* this game's load is the younger one */
    if (ownAgeMs > peerAgeMs && ownAgeMs - peerAgeMs > kLoadAgeTieMs) return kOwnIdKept;     /* the announcer's load is the younger one */
    if (mySeat > peerSeat) return kOwnIdLifted;
    return mySeat < peerSeat ? kOwnIdKeptLowerSeat : kOwnIdKept;
}
/* a load age read earlier, brought forward by elapsedMs: unknown stays unknown, and a known age never reaches kLoadAgeUnknown */
inline unsigned int LoadAgeAdvance(unsigned int ageMs, unsigned int elapsedMs)
{
    if (ageMs == kLoadAgeUnknown) return kLoadAgeUnknown;
    if (elapsedMs >= kLoadAgeUnknown - 1u - ageMs) return kLoadAgeUnknown - 1u;
    return ageMs + elapsedMs;
}

/* the record-writer source a caller hands SquadOriginSlot: kRecordNone = none, kRecordNewest = this game's newest record of the squad */
const int kRecordNone = -1, kRecordNewest = -2;
/* the sleep write (store.cpp detour_deactivate): a copy that was awake here and is the world's version (copyFresh 1, squadwriter.h
   NpcCopyFresh) is not decided by another game that wrote the record it loaded - that game's copy is asleep and writes nothing. Its
   record source then names this game only (owner = the newest record's writer, -1 none). copyFresh 0: the newest record, as elsewhere. */
inline int SleepRecordSource(int copyFresh, int owner, int mySlot)
{
    if (copyFresh == 0) return kRecordNewest;
    return (mySlot >= 0 && owner == mySlot) ? owner : kRecordNone;
}

/* the [STORE] origin line: the first 40 squads whose origin differs from their block, then every 50th */
inline int OriginLineDue(long long nth)
{
    return (nth <= 40 || nth % 50 == 0) ? 1 : 0;
}

} // namespace sqorigin

#endif
