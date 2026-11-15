/* squadbirth.h - a squad's birth persistence and what follows from it: whether the game keeps the squad when its area unloads,
   whether its world record goes when the game discards it, and whether a world record made before the flag existed is retired.

   THE ENGINE (Read): RootObjectFactory::createRandomUnloadedSquad 0x57EA30 writes its byte argument into Platoon+0x115 (the kept
   byte, decomp_57ea30.txt:61). A town's residents are born kept (1); its roaming squads are born not kept (0) and the engine
   destroys them when their area unloads, while their camp makes new ones. Platoon::_NV_serialiseEverything 0x372DB0 saves the
   byte as the state record's bool "persistent" and loadStateData 0x7EC550 reads it back, so a squad from a save load carries its
   own kept byte.

   Pure functions only: the plugin, the world server and the offline suite compile this same header. */
#ifndef SQUADBIRTH_H
#define SQUADBIRTH_H

#include <string>
#include <set>
#include <vector>

namespace sqbirth {

const int kBirthUnknown = -1;   /* no birth flag: an older record or message, or a squad never asked */
const int kBirthDiscard = 0;    /* born not kept: the game discards it when its area unloads */
const int kBirthKeep    = 1;    /* born kept: the game puts it to sleep when its area unloads */

/* ---- the RECORD flags byte (factionkey.h kRecFacCode is bit 0) ---- */
const unsigned int kRecBirthKnown   = 2u;   /* bit 1: the squad's birth flag is known */
const unsigned int kRecBornDiscard  = 4u;   /* bit 2: born not kept (meaningful only with bit 1) */
const unsigned int kRecBirthMask    = kRecBirthKnown | kRecBornDiscard;

inline unsigned int FlagsOfBirth(int birth)
{
    return birth == kBirthKeep ? kRecBirthKnown : (birth == kBirthDiscard ? kRecBirthMask : 0u);
}
inline int BirthOfFlags(unsigned int flags)
{
    if ((flags & kRecBirthKnown) == 0) return kBirthUnknown;
    return (flags & kRecBornDiscard) != 0 ? kBirthDiscard : kBirthKeep;
}
/* a write of a record keeps the birth bits held for it unless the arriving write knows them; every other bit is the arriving write's */
inline unsigned int FlagsMerge(unsigned int stored, unsigned int arriving)
{
    const unsigned int birth = (arriving & kRecBirthKnown) != 0 ? (arriving & kRecBirthMask) : (stored & kRecBirthMask);
    return (arriving & ~kRecBirthMask) | birth;
}
/* a birth flag that arrives replaces the one held only when it is known */
inline int BirthMerge(int stored, int arriving) { return arriving >= 0 ? arriving : stored; }
/* the birth flag a keep question uses, in this order: one noted at the squad's creation, copy or record (noted); its bound record's
   known flag (recordBirth - one a game published from a creation); one read off the squad's own kept byte (liveRead, *fromLive = 1).
   -1 none. The record comes before the squad's byte because the engine clears a resident's byte at an unload and a save keeps it. */
inline int BirthChoose(int noted, int recordBirth, int liveRead, int* fromLive)
{
    if (fromLive != 0) *fromLive = 0;
    if (noted >= 0) return noted;
    if (recordBirth >= 0) return recordBirth;
    if (liveRead >= 0) { if (fromLive != 0) *fromLive = 1; return liveRead; }
    return kBirthUnknown;
}
/* the birth flag a game publishes for a squad (its record, the index line, CONTEXT): only one noted at its creation, copy or record is
   definite; one read off its own kept byte goes as unknown - in an older world that byte may be this mod's own forcing, or the
   engine's clearing of a resident's byte at an earlier unload */
inline int BirthToPublish(int birth, int fromLive) { return fromLive != 0 ? kBirthUnknown : birth; }

/* ---- the index line's optional field 16 (storemeta.h) ---- */
inline std::string BirthField(int birth) { return birth == kBirthKeep ? std::string("p") : (birth == kBirthDiscard ? std::string("d") : std::string()); }
inline int BirthOfField(const std::string& f) { return f == "p" ? kBirthKeep : (f == "d" ? kBirthDiscard : kBirthUnknown); }
/* the game's own index reader (store.cpp ParseMetaLine) splits a line on tabs: field 16 of a v7 line, its line end stripped; -1 when
   the line has none or is not v7 */
inline int BirthOfLineFields(const std::vector<std::string>& f)
{
    if (f.size() < 16 || f[0] != "v7") return kBirthUnknown;
    std::string s = f[15];
    while (!s.empty() && (s[s.size() - 1] == '\r' || s[s.size() - 1] == '\n')) s.erase(s.size() - 1);
    return BirthOfField(s);
}

/* ---- CONTEXT's trailing word (the copy message) ---- */
const unsigned int kWireBirthUnknown = 0xFFFFFFFFu;
inline unsigned int WireOfBirth(int birth) { return birth == kBirthKeep ? 1u : (birth == kBirthDiscard ? 0u : kWireBirthUnknown); }
inline int BirthOfWire(unsigned int w) { return w == 1u ? kBirthKeep : (w == 0u ? kBirthDiscard : kBirthUnknown); }

/* ---- the keep question (Platoon::isPersistentSquad, asked of a squad bound to a world record) ----
   living: living members (0 = none, -2 = a squad read whole with no awake members - emptied by hiring or merging, -1 = not read - a
   fault, an implausible member list or more than 512 members); birth: the squad's birth
   flag (BirthChoose); fromLive: 1 when the flag was read off the squad's own kept byte rather than noted at its creation, copy or
   record or carried by its record; hasHome: the squad has a home building (its record's home key or its live one - a town's
   resident); playerHeld: the squad's faction is a player's or a stand-in's, or it holds such a character (a member, or one a member
   carries); otherHeld: another game holds the squad's area now; templateRoaming (T-999 fold 2b): the squad's template is listed in its
   bound record's town's "roaming squads" (1), not listed (0), not readable (-1) - asked only of a kept flag read off the squad's own
   byte with no home: that byte is not proof of a kept birth (this mod writes 1 on a copy or a record-made squad whose flag is unknown),
   so such a squad goes the protected road only when its template is a roaming one, and is kept otherwise. */
enum { kKeepDeadRelease = 0, kKeepEngine = 1, kKeepBorn = 2, kKeepUnknown = 3, kKeepPlayerHeld = 4, kKeepOtherHolder = 5, kKeepHome = 6, kKeepNotRead = 7 };
inline int KeepDecide(int living, int birth, int fromLive, int hasHome, int playerHeld, int otherHeld, int templateRoaming)
{
    if (living == 0 || living == -2) return kKeepDeadRelease;   /* nobody living, or no members at all: the game's own answer */
    if (living < 0) return kKeepNotRead;   /* T-999 fold 2: the member list not read - kept, the answer from before the birth flag (the dead road is taken on a read only) */
    if (hasHome == 1) return kKeepHome;   /* a town's resident: kept whatever its byte says (the engine clears a knocked-out resident's) */
    if (birth == kBirthKeep && fromLive == 0) return kKeepBorn;
    if (birth != kBirthKeep && birth != kBirthDiscard) return kKeepUnknown;   /* no flag readable: kept, as a recorded squad always was */
    if (birth == kBirthKeep && templateRoaming != 1) return kKeepUnknown;   /* fold 2b: a kept byte read off a squad not shown to be a roaming one: kept */
    /* born not kept, or a kept byte read off a roaming-listed squad with no home (the byte may be this mod's own write): kept only
       while protected, else the game decides - and the engine's own test keeps a campaign's squad */
    if (playerHeld == 1) return kKeepPlayerHeld;
    if (otherHeld == 1) return kKeepOtherHolder;
    return kKeepEngine;
}
/* T-850 fold 1 (T2198): the keep question's otherHeld input - another game HOLDS the squad's area, or (the reverse case) has it LOADED now
   by its latest area report (it shows the squad's people as copies). A runner leaving the area while another game still has it loaded
   keeps the squad (it sleeps here with its record; its people are offered to that game at the put-away) instead of the engine's
   discard deleting a group the other game still shows. heldByOther / otherLoaded: 1 yes, 0 no, -1 no fresh area map (-1 = no, as before). */
inline int OtherShowsArea(int heldByOther, int otherLoaded) { return (heldByOther == 1 || otherLoaded == 1) ? 1 : 0; }
/* T-1046 fix 2 (owner 1591 a, T2299 / T2301): ONE KEEP RULE FOR BOTH QUESTIONS. The engine asks "keep this squad?" of a squad put away
   (awake, 0x6BABD7) and of a squad asleep (its wake test, 0x7EE833); the mod gave two answers (asleep the engine's "no", awake "kept for
   the other game" from the bare loaded bit), so a squad in the edge strip of an area the other game had loaded was woken and put back to
   sleep every ~3.2 s. Now "kept for another game" only where the hand-over's own receiver test (handoff.cpp ReceiversFor ->
   liveowner.h EngineKeepsAt, the same 250 margin) names a game whose engine would keep the squad at its spot - asleep, only when this
   game's records also show the squad's people wholly taken by a game that test names (RelAccept / fold 1b: the squad index's given);
   everywhere else the engine's own answer. receivers: ReceiversFor's k at the squad's spot (-1 hold: no fresh map, a row stale, no slot
   of this game's own, the link down; 0 nobody; >= 1 that many games named); takenByNamed: 1 = the squad's people were wholly taken by a
   game the order names (asked only asleep); staleShows: today's answer for a stale map (OtherShowsArea over HeldByOtherTS and
   OtherLoadedTS read live - with the map stale both read -1, so the engine's answer), used for BOTH questions so they never disagree.
   T-1046 attempt 2 (design a', manager 2026-11-14; T2369): keptPart = the people of the squad this game keeps un-offered (the squad
   index's keptHere + asleepHere + the not-yet-indexed kept list - not the held, undecided put-aways, whose first offer is still to come;
   handoff.cpp HandoffSquadKeptPart), CHECKED FIRST: while it is > 0 whole or none refuses every later offer of the squad (KeptAsleepOf),
   so "kept for that game" can never come true - both questions, and a stale map alike, get the engine's own answer (single-player
   parity: the engine discards a squad no game takes at its area edge; on a refusal the sender keeps the squad under its own rules). The
   one exception (T-1046 fold 2, the code's own rule): an asleep squad whose own sleepers that game already took (takenByNamed 1 -
   leftovers kept here that are not among its sleepers do not block it) keeps the answer it had.
   Returns 1 = kept (the mod answers keep), 0 = the engine's own answer. */
inline int OneKeepKeptOut(int asleep, int takenByNamed, int keptPart) { return (keptPart > 0 && !(asleep != 0 && takenByNamed == 1)) ? 1 : 0; }
inline int OneKeepRule(int asleep, int receivers, int takenByNamed, int staleShows, int keptPart)
{
    if (OneKeepKeptOut(asleep, takenByNamed, keptPart) != 0) return 0;   /* T-1046 attempt 2: kept here un-offered - no game takes it: the engine's own answer */
    if (receivers < 0) return staleShows == 1 ? 1 : 0;   /* stale or missing map: today's answer, the same asleep and awake */
    if (receivers == 0) return 0;                          /* no game's engine keeps it at this spot: the engine's own answer */
    if (asleep != 0 && takenByNamed != 1) return 0;        /* asleep, its people not taken by a game that keeps it: the engine's answer (it wakes, is put away and offered) */
    return 1;
}
/* the counter for one answer (store.cpp oneKeep[...] on the birth REPORT) */
enum { kOkKeptAwake = 0, kOkKeptAsleep = 1, kOkNoGameAwake = 2, kOkNoGameAsleep = 3, kOkNotTakenAsleep = 4, kOkStaleKept = 5, kOkStaleEngine = 6,
       kOkKeptOutAwake = 7, kOkKeptOutAsleep = 8, kOkVerdicts = 9 };   /* T-1046 attempt 2: kept-out - this game keeps people of it un-offered */
inline int OneKeepVerdict(int asleep, int receivers, int takenByNamed, int staleShows, int keptPart)
{
    if (OneKeepKeptOut(asleep, takenByNamed, keptPart) != 0) return asleep != 0 ? (int)kOkKeptOutAsleep : (int)kOkKeptOutAwake;
    if (receivers < 0) return staleShows == 1 ? (int)kOkStaleKept : (int)kOkStaleEngine;
    if (receivers == 0) return asleep != 0 ? (int)kOkNoGameAsleep : (int)kOkNoGameAwake;
    if (asleep != 0) return takenByNamed == 1 ? (int)kOkKeptAsleep : (int)kOkNotTakenAsleep;
    return kOkKeptAwake;
}
/* 1 = the mod answers 'keep'; 0 = the game's own answer is returned */
inline int KeepAnswers(int verdict) { return verdict >= kKeepBorn ? 1 : 0; }
/* the kept byte a squad made here from another game's message or from a world record is given: its birth flag; unknown = kept */
inline int MadeKeptByte(int birth) { return birth == kBirthDiscard ? 0 : 1; }

/* ---- the delete a destroy of a squad sends ----
   bound: the squad is bound to a world record here; diedOut: the death watch marked it wiped out; discarding: the last keep
   answer for it was the game's own while it had living members; livingAtDestroy: its living members as the destroy fired (> 0 = the
   unload road - a squad emptied by hiring or merging has none (-2), and its destroy stays the plain delete; -1, its members not read,
   stays on the unload road: a squad marked discarding had living members at its last keep question); idShared: another squad here
   is bound to the same id, whose record the discard of this one must not delete (nothing is sent). */
enum { kDelNone = 0, kDelPlain = 1, kDelDiscard = 2, kDelModKept = 3 };   /* kDelModKept: DestroyDeleteMod */
/* fold 3: 1 = the keep question's 'no living members' answer erases the squad's discard mark - everyone dead (0), or an empty squad
   (-2) the engine's own unload removals did not empty (hiring, merging). An unload-emptied one keeps the mark for its destroy: at an
   area unload the squad walk asks with the members there, removes them all ('wandered out of zone'), and the faction update asks again
   with the squad empty before it destroys it. */
inline int DiscardMarkErased(int living, int emptiedByUnload) { return (living == 0 || (living == -2 && emptiedByUnload == 0)) ? 1 : 0; }
/* 1 = a destroy with this living count is on the unload road (living members, or not read; fold 3: or none (-2) when the engine's own
   unload removals emptied it - emptiedByUnload) */
inline int LivingUnloadRoad(int livingAtDestroy, int emptiedByUnload) { return (livingAtDestroy > 0 || livingAtDestroy == -1 || (livingAtDestroy == -2 && emptiedByUnload != 0)) ? 1 : 0; }
inline int DestroyDelete(int bound, int diedOut, int discarding, int livingAtDestroy, int emptiedByUnload, int idShared)
{
    if (bound == 0) return kDelNone;
    if (diedOut == 0 && discarding != 0 && LivingUnloadRoad(livingAtDestroy, emptiedByUnload) != 0) return idShared != 0 ? kDelNone : kDelDiscard;
    return kDelPlain;
}
/* T-1080: the mod-removal mark. npcSquad: the squad may carry it (the orphan purge asks it of world NPC squads only; a removal of a
   person through ModRemovalNote, of any recorded squad); removing: this game's own removal took or is taking people out of it (the
   purge's g_npcPurged, or the removal under way); living: its living members after that removal (> 0 living, 0 only bodies left, -2
   no members, -1 not read). Marked when nobody living is left - bodies do not count - or the members cannot be read: a failed read
   never deletes a world record. 1 = mark. */
inline int ModRemovedMark(int npcSquad, int removing, int living)
{
    return (npcSquad != 0 && removing != 0 && living <= 0) ? 1 : 0;
}
/* T-1080: the delete the destroy of a squad sends when this game's own removal of people may have emptied it. modRemoved: the squad
   carries the mark (store.cpp g_modRemoved - set by ModRemovedMark, and when this game makes the squad's content from a world record:
   CreateUnknownSquad, a wake's reload, the engine's read of the record at a wake; erased only when this game sees a living member of
   its own in the squad (ModMarkClearedBy), at the world teardown, and taken by its destroy). A removal of people this game makes (the
   orphan purge in an area another game holds, the stand-in purge, the removal of another game's copies at its link loss, its
   departure or its withdrawal), or a squad this game made from a record that never had a living member here, is this game's own
   doing; it does not end the squad: a plain delete becomes no delete, here or on the world server, and the record stays
   (kDelModKept). A squad the death watch marked wiped out (diedOut) is deleted as before; the discard delete of a squad born not
   kept and the same-id keep stay DestroyDelete's answers. */
inline int DestroyDeleteMod(int modRemoved, int bound, int diedOut, int discarding, int livingAtDestroy, int emptiedByUnload, int idShared)
{
    const int del = DestroyDelete(bound, diedOut, discarding, livingAtDestroy, emptiedByUnload, idShared);
    return (del == kDelPlain && diedOut == 0 && modRemoved != 0) ? kDelModKept : del;
}
/* T-1080: 1 = the mark goes - this game sees `living` (LivingMembersPod: > 0 living, 0 only bodies, -2 none, -1 not read) living
   members of the squad's own here. A squad that had living members here which all died is then unmarked and deletes as before. */
inline int ModMarkClearedBy(int living) { return living > 0 ? 1 : 0; }
/* T-1080: 1 = the destroy (del: DestroyDeleteMod's answer) kept the record of a squad this game made from that record (madeMark:
   CreateUnknownSquad, a wake's reload or the engine's read of the record) that never had a living member of its own here. Such a
   squad is not placed from the record again in this world (store.cpp g_notePlaceFailed, as a creation that failed): placing it again
   would build the same empty squad from the same record, lose it again and repeat every placement pass. A squad only this game's
   removal of people emptied (the purge) is still placed from its record when its area is this game's to run. */
inline int MadeEmptyPlaceFailed(int del, int madeMark) { return (del == kDelModKept && madeMark != 0) ? 1 : 0; }
/* takes one squad's mark: 1 = it was marked; the mark is gone afterwards (a destroy uses it once) */
template <class K> inline int ModRemovedTake(std::set<K>* marks, const K& key) { return (marks != 0 && marks->erase(key) != 0) ? 1 : 0; }

/* T-850: ANOTHER GAME RUNS LIVING PEOPLE THIS SQUAD LOST HERE. A squad's copy here can be emptied by the engine's own area unload while
   its people are another game's: this game's copies of them (puppets - the other game drives them) are put away, the squad reads empty
   and the engine destroys it. That is no end of the squad - its runner still has the people and writes the record when it puts them
   away. The mark: unloadRemoval - the engine's unload removal of a member of the squad (not a death); runElsewhere - the person is
   another game's puppet here at that moment (who runs it NOW, not who made its uid); deadRead - Character::hasDied read at the removal
   (1 dead, 0 alive, -1 not read). Only a copy read ALIVE marks: a dead copy's body removed at an unload does not keep a dead squad's
   record (a ghost). 1 = mark. */
inline int OtherRunRemovedMark(int unloadRemoval, int runElsewhere, int deadRead)
{
    return (unloadRemoval != 0 && runElsewhere != 0 && deadRead == 0) ? 1 : 0;
}
/* T-850: kDelRunKept - the destroy of a squad that lost living people another game runs keeps the record: nothing is deleted here or on
   the world server (no discard delete, no plain delete), and - unlike kDelModKept - the squad is placed from its record again when its
   area is this game's to run. kDelRunHeld (fold 1, M4) - one of those people was this game's own, offered to another game at its
   put-away, and the offer has not settled: no delete yet; the squad's delete waits for the hand-over's outcome (RunHeldResolve). */
enum { kDelRunKept = 4, kDelRunHeld = 5 };
/* T-850 fold 1: what a squad's copy here noted about a living person it lost (per uid, store.cpp g_otherRunRemoved): kLostRunsThere - the
   engine's unload put away this game's puppet of another game's person; kLostAwayThere - another game's message removed the copy of a
   living person (its UNLOAD: put away alive or withdrawn; PEER GONE; a stale copy retired); kLostHandedOff - this game's own person, offered
   to another game at its put-away (an open release). A DESPAWN (the runner says it died) notes nothing. */
enum { kLostNone = 0, kLostRunsThere = 1, kLostAwayThere = 2, kLostHandedOff = 3 };
/* T-850 fold 1: the person's answer NOW, at the squad's destroy (live facts, not the note's age): diedReported - the person's runner has
   said it died (a DESPAWN since the note); releasePending - its release is still open; takenByOther - another game owns it now (its
   owner record; this game's own no longer). 1 runs elsewhere alive, 0 not, 2 not known yet (the hand-over is open). */
enum { kRunNo = 0, kRunYes = 1, kRunPending = 2 };
inline int PersonRunsElsewhere(int kind, int diedReported, int releasePending, int takenByOther)
{
    if (kind == kLostNone || diedReported != 0) return kRunNo;
    if (kind == kLostHandedOff) return releasePending != 0 ? (int)kRunPending : (takenByOther != 0 ? (int)kRunYes : (int)kRunNo);
    return kRunYes;   /* another game ran it, or put it away alive: its runner holds it (and writes its record) */
}
/* the squad's answer from its people's: any one living elsewhere keeps it; else an open hand-over waits; else nobody */
inline int SquadRunAnswer(int anyYes, int anyPending) { return anyYes != 0 ? (int)kRunYes : (anyPending != 0 ? (int)kRunPending : (int)kRunNo); }
/* T-850: the delete a destroy sends, with the squad's 'another game runs its people' answer (otherRun: kRunNo / kRunYes / kRunPending) as
   one more input. kRunYes turns the discard delete and the plain delete - and the mod-removal keep, whose not-placed-again rule must not
   apply to people still alive elsewhere - into kDelRunKept; kRunPending holds them (kDelRunHeld). A wipe-out the death watch saw
   (diedOut) is deleted as before, and the same-id keep (kDelNone) stays. */
inline int DestroyDeleteRun(int otherRun, int modRemoved, int bound, int diedOut, int discarding, int livingAtDestroy, int emptiedByUnload, int idShared)
{
    const int del = DestroyDeleteMod(modRemoved, bound, diedOut, discarding, livingAtDestroy, emptiedByUnload, idShared);
    if (otherRun == kRunNo || diedOut != 0) return del;
    if (del != kDelDiscard && del != kDelPlain && del != kDelModKept) return del;
    return otherRun == kRunPending ? (int)kDelRunHeld : (int)kDelRunKept;
}
/* T-850 fold 1: a held squad delete (kDelRunHeld) at each look: answer is the squad's SquadRunAnswer now; was - the delete the destroy
   would have sent (DestroyDeleteMod). kDelRunKept once a person is taken by another game, `was` once every hand-over settled with nobody
   taking one, kDelRunHeld while one is open. */
inline int RunHeldResolve(int answer, int was) { return answer == kRunYes ? (int)kDelRunKept : (answer == kRunPending ? (int)kDelRunHeld : was); }
/* T-850 fold 2 (T2217): THE COPY THAT WAS NEVER THE SQUAD HERE. The engine destroys an emptied copy and the destroy's answer is the plain
   delete (kDelPlain: the permanent deleted mark, here and on the world server). That ends the squad only when its people died. A copy whose
   wake this game refused (another game runs the squad, or the list hold held it), a copy of a squad another game runs now (the world
   server's runner note), a copy that never had a living member of this game's own here (an empty shell woken beside another game's people)
   and a copy whose id another copy here is still bound to (a same-id pair: the other copy holds the squad's record) are not that end - the
   plain delete becomes no delete (kDelShellKept): nothing is sent and the record stays. reallyDied keeps the delete in every case: the death
   watch marked the squad wiped out (diedOut), or its members are all bodies and some were this game's own (ReallyDiedOut). */
enum { kDelShellKept = 6 };
/* 1 = the squad really died out here: the death watch's wipe-out mark, or no living member left (livingAtDestroy 0: members, all dead)
   while this game ran some of them (ownMembers: members now whose session uid is this game's or none; ownLivedHere: the death watch saw a
   living member of this game's own in this copy) */
inline int ReallyDiedOut(int diedOut, int livingAtDestroy, int ownMembers, int ownLivedHere)
{
    if (diedOut != 0) return 1;
    return (livingAtDestroy == 0 && (ownMembers > 0 || ownLivedHere != 0)) ? 1 : 0;
}
/* the destroy's answer (del, DestroyDeleteRun's) with the copy's facts: wakeRefused - this game refused the copy's wake (store.cpp
   g_wakeDup); otherRuns - the world server says another live game runs the squad (SwOtherRunner); ownLivedHere - the copy had a living
   member of this game's own here (seen by the death watch, or living now); idShared - another platoon here is bound to the same id */
inline int ShellKeep(int del, int reallyDied, int wakeRefused, int otherRuns, int ownLivedHere, int idShared)
{
    if (del != kDelPlain || reallyDied != 0) return del;
    return (wakeRefused != 0 || otherRuns != 0 || ownLivedHere == 0 || idShared != 0) ? (int)kDelShellKept : del;
}
/* T-850 fold 1b: a squad here binds a held delete's id again (it lives here) - 1 = the hold is dropped, nothing deleted */
inline int RunHeldOnRebind(int held) { return held != 0 ? 1 : 0; }
/* T-850 fold 1 (M1): the note goes when the squad here has living members again at the keep question (the person lost was a straggler,
   the squad lives on here; its later destroy is decided by what empties it then) and at its sleep. 1 = forget the squad's notes. */
inline int OtherRunNotesCleared(int livingAtKeepQuestion) { return livingAtKeepQuestion > 0 ? 1 : 0; }
/* T-850: the received RECORD_GONE's permanent deleted mark. A discard (the sender's copy was put away, born not kept) is no death:
   the record goes, no mark is set - a living copy elsewhere is written again at its put-away. Any other gone (a plain or wipe-out
   delete) sets the mark. 1 = set the mark. */
inline int GoneSetsDeletedMark(int isDiscard) { return isDiscard != 0 ? 0 : 1; }
/* T-850 fold 1 (L2): 1 = a received gone is older than the record here and ignored. A discard loses to a write in its own second too
   (stamps are whole seconds: a runner's sleep write in that second is the squad living on); a death delete only to a newer write. */
inline int GoneIgnoredAsOlder(int isDiscard, long long recordWrittenAt, long long goneStamp) { return isDiscard != 0 ? (recordWrittenAt >= goneStamp ? 1 : 0) : (recordWrittenAt > goneStamp ? 1 : 0); }

/* ---- the world server's judgement of a discard delete ----
   holderId: the player the area map names as the holder of the record's area ("" = nobody); senderId: the sending game's player;
   otherLoaded: how many games other than the sender have the area loaded now (their last area report within the grace). A discard
   delete waits while ANOTHER game holds or has the area loaded - that game still shows the squad, and the sender's own area update
   can arrive after its delete; the game that drops the area last sends the delete that lands. A plain delete is never judged here. */
enum { kSrvApply = 0, kSrvRefuseOtherHolds = 1, kSrvRefuseOtherLoaded = 2 };
inline int ServerDiscardJudge(int isDiscard, const std::string& holderId, const std::string& senderId, int otherLoaded)
{
    if (isDiscard == 0) return kSrvApply;
    if (!holderId.empty() && holderId != senderId) return kSrvRefuseOtherHolds;
    if (otherLoaded > 0) return kSrvRefuseOtherLoaded;
    return kSrvApply;
}

/* ---- a refused discard delete, held on the world server and judged again (T-999 fold 2) ----
   The world server holds a refused discard delete (record id -> its stamp, sender and area) and judges it again when the area's
   holder changes, when a game's report drops the area and when a game's link closes. recordHere: the record is still held;
   recordStamp: its last write's stamp; pendingStamp: the held delete's stamp. Dropped when the record is gone (deleted another way)
   or was written again after the delete (newer stamp - the squad lives on somewhere); else ServerDiscardJudge decides. */
const size_t kPendingDiscardCap = 1024;
enum { kPdKeep = 0, kPdApply = 1, kPdDrop = 2 };
/* fold 2c: senderReturned (PendingDiscardSenderReturned) - the sending game is back in the area after the server saw it leave: it
   remade the squad from the record, which must stay (dropped) */
inline int PendingDiscardStep(int recordHere, long long recordStamp, long long pendingStamp, const std::string& holderId, const std::string& senderId, int otherLoaded, int senderReturned)
{
    if (recordHere == 0) return kPdDrop;
    if (recordStamp > pendingStamp) return kPdDrop;
    if (senderReturned != 0) return kPdDrop;
    return ServerDiscardJudge(1, holderId, senderId, otherLoaded) == kSrvApply ? kPdApply : kPdKeep;
}
/* 1 = the sender is back: the server saw it leave the area (senderLeft: a report of its slot without the area, or its link closed)
   and it now holds the area or its latest report lists it (senderThere). Before a leave is seen its own listing is ignored - its area
   update can land after its delete. */
inline int PendingDiscardSenderReturned(int senderLeft, int senderThere) { return (senderLeft != 0 && senderThere != 0) ? 1 : 0; }
/* 1 = a write of the record with writeStamp drops the held discard delete (it is newer than the delete) */
inline int PendingDiscardDropOnWrite(long long pendingStamp, long long writeStamp) { return writeStamp > pendingStamp ? 1 : 0; }
/* 1 = a refused discard delete is held: one already held for the id is replaced; a new one only below the cap */
inline int PendingDiscardAdmit(size_t held, size_t cap, int alreadyHeld) { return (alreadyHeld != 0 || held < cap) ? 1 : 0; }

/* ---- a world record from before the birth flag, at its placing: retired instead of made again ----
   ALL must hold: no birth flag; no home building key; its faction is not a player's or a stand-in's; its squad template is listed
   in its town's "roaming squads" (1; 0 not listed, -1 not readable); the saved squad names no player's or stand-in's faction
   (0; 1 it does, -1 not readable). */
inline int RetireJudge(int birth, int homeEmpty, int playersFaction, int templateRoaming, int holdsPlayer)
{
    return (birth == kBirthUnknown && homeEmpty != 0 && playersFaction == 0 && templateRoaming == 1 && holdsPlayer == 0) ? 1 : 0;
}

/* does `bytes` contain `needle` (a byte search; an empty needle is never found) */
inline int BytesHold(const char* bytes, size_t n, const std::string& needle)
{
    if (bytes == 0 || needle.empty() || n < needle.size()) return 0;
    for (size_t i = 0; i + needle.size() <= n; ++i)
    {
        size_t j = 0;
        while (j < needle.size() && bytes[i + j] == needle[j]) ++j;
        if (j == needle.size()) return 1;
    }
    return 0;
}

}   /* namespace sqbirth */

#endif
