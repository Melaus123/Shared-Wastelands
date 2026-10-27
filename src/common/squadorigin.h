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
 * running tick after a load, before any block raise, and kept under the faction's stable internal name (FloorKey: its
 * game-data stringID), never under the name it shows: a faction renamed after the load keeps its floor, and a player's
 * faction showing an NPC faction's name keeps its own. A number AT OR ABOVE that floor was handed out since the load, so
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

#include <string>
#include "factionkey.h"   /* SidUsable: a stringID usable as a key */

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

/* the load floor of a key several factions share (two factions whose stringIDs cannot be read and that show one name):
   the LOWEST counter, so a number is called "from the save" only when it is below every one of them - a squad of the
   higher-counter faction that is wrongly called "made here" keeps the number rule, which is no worse than before */
inline unsigned FloorMerge(int haveFloor, unsigned floorNow, unsigned counter)
{
    return (haveFloor == 0 || counter < floorNow) ? counter : floorNow;
}

/* THE LOAD FLOOR'S KEY: a faction's game-data stringID - the same in every game and every language, and unchanged when the
   faction is renamed. A stringID that cannot be used (unread, empty, too long: factionkey.h SidUsable) falls back to the name
   the faction shows, with a leading '@' so that it never equals a stringID. */
inline std::string FloorKey(const std::string& sid, const std::string& name)
{
    return factionkey::SidUsable(sid) ? sid : std::string("@") + name;
}
/* the shown-name index, for an asker that holds only a squad id and no faction (the id's name part is the name its faction
   showed when the squad was numbered): a name one key shows names that key; a name factions of different keys show names
   none (the empty key - no floor, so the number rule decides). haveEntry 0: the name's first faction. */
inline std::string NameIndexMerge(int haveEntry, const std::string& keyNow, const std::string& key)
{
    if (haveEntry == 0) return key;
    return keyNow == key ? keyNow : std::string();
}
/* the key a squad's load floor is looked up by: its faction's (factionKey) when the asker holds the faction and its key was
   read; otherwise the shown-name index's entry for the id's name part (nameKey); empty = no floor known */
inline std::string FloorLookupKey(int haveFaction, const std::string& factionKey, int haveName, const std::string& nameKey)
{
    if (haveFaction != 0) return factionKey;
    return haveName != 0 ? nameKey : std::string();
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

/* A SQUAD THIS GAME'S SAVE SPLIT OFF ANOTHER GAME'S PEOPLE. The engine can split a person into a squad of their own (a cage step
   does), and a save written then holds that squad with a body standing on another game's registered copy. The orphan clean-up removes
   the body (orphanpurge.h Decide, the duplicate rule); the squad it leaves behind is no one's record, so no other road removes it and
   it would go into the next save. It is removed (the duplicate-wake road: on ice, then destroyed under the admin flag) only when every
   input below says it is such a squad; anything unread or uncertain keeps it. The same rule on every seat: no slot number is special.
   Read when the orphan clean-up has removed people of the squad (the moment its last member goes). */
struct SaveSplitSeen
{
    int readable;       /* 1 = the squad, its members and the removed people's spot were read */
    int worldFaction;   /* 1 = a world faction's squad (not a player's, a peer's or a stand-in record's) */
    int context;        /* 1 = a context platoon (another game's squad shown here) */
    int bound;          /* 1 = the store's binding map holds it */
    int recorded;       /* 1 = the store holds a record under its id */
    int handedOver;     /* 1 = the hand-over index names it */
    int queued;         /* 1 = already on the context removal road */
    int members;        /* members still in it, the people just removed not counted */
    int uidMembers;     /* of those, members the session knows by a uid */
    int saveMade;       /* 1 = its number is below its faction's load floor (the save made it) */
    int mySlot;         /* this game's slot, -1 unknown */
    int blockOwner;     /* the block of its number (number / 65,536), -1 no number */
    int originSlot;     /* SquadOrigin's answer at the removed people's spot */
    int originWhy;      /* the source of that answer (kBy...) */
    int heldByOther;    /* the area map there: 1 another game holds it, 0 not, -1 no fresh map, -2 its holder is not in the world */
    int holderSlot;     /* the area holder's slot there, -1 unknown */
    int holderInWorld;  /* 1 = the roster lists that slot in the world */
    int asleep;         /* 1 = the squad is asleep (found in its faction's sleeping list) */
    int sleepCount;     /* asleep: the "char count" of its own sleep record, -1 unreadable */
};
/* the answer: kSplitDrop removes it; every other value keeps it and names the first input that did */
const int kSplitDrop = 0, kSplitUnread = 1, kSplitNotWorld = 2, kSplitContext = 3, kSplitBound = 4, kSplitRecorded = 5,
          kSplitHandedOver = 6, kSplitQueued = 7, kSplitUidMember = 8, kSplitNotEmpty = 9, kSplitNotSave = 10, kSplitNotMyBlock = 11,
          kSplitNotByHolder = 12, kSplitOriginMine = 13, kSplitNoMap = 14, kSplitNotOtherHeld = 15, kSplitHolderAway = 16, kSplitSleepMembers = 17, kSplitSleepUnread = 18, kSplitWhyCount = 19;
/* every input set to the values that remove the squad (this game in slot 1, the holder in slot 0) - the tests change one at a time */
inline SaveSplitSeen SaveSplitSeenDroppable()
{
    SaveSplitSeen s;
    s.readable = 1; s.worldFaction = 1; s.context = 0; s.bound = 0; s.recorded = 0; s.handedOver = 0; s.queued = 0;
    s.members = 0; s.uidMembers = 0; s.saveMade = 1; s.mySlot = 1; s.blockOwner = 1; s.originSlot = 0; s.originWhy = kByHolder;
    s.heldByOther = 1; s.holderSlot = 0; s.holderInWorld = 1;
    s.asleep = 0; s.sleepCount = 0;
    return s;
}
inline int SaveSplitDrop(const SaveSplitSeen& s)
{
    if (s.readable == 0) return kSplitUnread;
    if (s.worldFaction == 0) return kSplitNotWorld;
    if (s.context != 0) return kSplitContext;
    if (s.bound != 0) return kSplitBound;
    if (s.recorded != 0) return kSplitRecorded;
    if (s.handedOver != 0) return kSplitHandedOver;
    if (s.queued != 0) return kSplitQueued;
    if (s.uidMembers != 0) return kSplitUidMember;      /* a member the session knows (or an unread count) - never */
    if (s.members != 0) return kSplitNotEmpty;          /* someone is still in it: asked again when the next one is removed */
    if (s.asleep != 0 && s.sleepCount != 0) return s.sleepCount < 0 ? kSplitSleepUnread : kSplitSleepMembers;   /* asleep: only a sleep record read as 0 people */
    if (s.saveMade == 0) return kSplitNotSave;
    if (SlotOk(s.mySlot) == 0 || s.blockOwner != s.mySlot) return kSplitNotMyBlock;
    if (s.originWhy != kByHolder) return kSplitNotByHolder;   /* handed over, its members, a record, the number: not this rule's */
    if (SlotOk(s.originSlot) == 0 || s.originSlot == s.mySlot) return kSplitOriginMine;
    if (s.heldByOther == -1) return kSplitNoMap;
    if (s.heldByOther != 1) return kSplitNotOtherHeld;
    if (SlotOk(s.holderSlot) == 0 || s.holderSlot == s.mySlot || s.holderSlot != s.originSlot || s.holderInWorld != 1) return kSplitHolderAway;
    return kSplitDrop;
}
/* At the removal tick, read there: awake - its member count; asleep - its sleep record's count (-1 unreadable). 1 = keep it now. */
inline int SaveSplitTickKeep(int awake, int activeChars, int sleepCount)
{
    if (awake != 0) return activeChars != 0 ? 1 : 0;
    return sleepCount != 0 ? 1 : 0;
}
inline const char* SplitWhyToken(int why)
{
    switch (why)
    {
    case kSplitUnread:       return "unread";
    case kSplitNotWorld:     return "notWorld";
    case kSplitContext:      return "context";
    case kSplitBound:        return "bound";
    case kSplitRecorded:     return "recorded";
    case kSplitHandedOver:   return "handedOver";
    case kSplitQueued:       return "queued";
    case kSplitUidMember:    return "uidMember";
    case kSplitNotEmpty:     return "notEmpty";
    case kSplitNotSave:      return "notSave";
    case kSplitNotMyBlock:   return "notMyBlock";
    case kSplitNotByHolder:  return "notByHolder";
    case kSplitOriginMine:   return "originMine";
    case kSplitNoMap:        return "noMap";
    case kSplitNotOtherHeld: return "notOtherHeld";
    case kSplitHolderAway:   return "holderAway";
    case kSplitSleepMembers: return "sleepMembers";
    case kSplitSleepUnread:  return "sleepUnread";
    default:                 return "?";
    }
}

} // namespace sqorigin

#endif
