#pragma once
// T-580: WHICH NOTEBOOK RECORDS HOLD A TOWN'S CREATIONS BACK (decision 34, "notebook people pending"). ONE header compiled into
// the plugin (store.cpp publishes, towngen.cpp asks) and the offline suite, so the two cannot hold different ideas of one rule.
//
// Decision 34 refuses a town's new squads while the notebook lists living groups of that town that THIS game has not placed:
// the town has people, so the engine must not invent more. The engine asks for new squads by counting the squads registered to
// the town in THIS game's world (F500: 0x8F5B70 against the budget), so a group the notebook holds in an area this game never
// loads is not in that count and never will be until that area loads. Such a group cannot be placed here, so it does not hold
// the town: it is placed if and when its area loads here. A group whose area IS loaded here, or is in or beside the town's own
// area (the town's centre position; the creation's spawn position when the town's will not read - the engine is loading that
// area now), is about to be placed or is another game's live group there; it still holds the town, so the refusal never invents
// people a record already holds. A group whose position is not known here, or a town whose area cannot be read, cannot be shown
// to be elsewhere: it holds, as decision 34 always did.
//
// A note holds only while some game can still place it (Placeable), and the game that RUNS the note's area places it, whatever
// block numbered it: this game's engine holds a group of that id (asleep, or awake and not yet bound - it is placed when its area
// loads here); or the area is this game's to run (no other game holds it, read exactly) and the note's record is here and usable -
// this game creates it from that record once the area is loaded here (the world's version, decision 547), and it holds until then;
// or another game holds the area and that game is in the world - it places it there, and its people reach this game by that
// game's announcements. A note in an area this game runs with no usable record here (or whose creation failed), or in an area held
// by a game that is not in the world, is placed by no game: it never holds, wherever it is. A note whose area cannot be read here
// (no position, off the map, no fresh area map), whose area's holder cannot be named on the roster, or whose engine answer is not
// in yet, cannot be told: it holds. The answer follows the state - a holder who comes back into the world makes the note hold again.
namespace townpending {

// NoteRole: what one notebook note is to its home town's pending mark (store, main thread, every publish).
const int kNoteNoTown = 0;    // names no home town ("" unknown, or the sender said it has none)
const int kNoteDeleted = 1;   // the group is deleted (decision 30): nothing of it is kept
const int kNotePlaced = 2;    // this game has its platoon (bound here): the engine already counts it
const int kNoteNoPos = 3;     // listed with NO position known here (no record here, or (0,0)): it holds its town wherever asked
const int kNoteListed = 4;    // listed under its town with its area; the asker decides whether that area holds the town
inline int NoteRole(int hasTown, int deleted, int placedHere, int hasPos)
{
    if (hasTown == 0) return kNoteNoTown;
    if (deleted != 0) return kNoteDeleted;
    if (placedHere != 0) return kNotePlaced;
    if (hasPos == 0) return kNoteNoPos;
    return kNoteListed;
}

// Placeable: can some game place one note this game has not placed? engineHere = this game's engine holds a group of that id,
// asleep or awake (1), does not (0), not asked yet (-1); areaHeld = the note's area is held by another game (1), is not (0), cannot
// be read here (-1: no position here, off the map, or no fresh area map); holderInWorld = the slot holding that area is IN_WORLD on
// this link's roster (1), is not (0), cannot be told (-1); recordUsable = its record is here with a position, a file, a squad and a
// faction; placeFailed = this game's creation of it from that record failed in this world.
const int kPlaceYes = 1;          // this game's engine holds it, or the game holding its area is in the world: it holds as a listed note does
const int kPlaceNoGame = 2;       // its area is this game's to run and this game cannot place it (no usable record here, or its creation failed)
const int kPlaceNoAbsent = 3;     // its area is held by a game that is not in the world, and this game's engine does not hold it
const int kPlaceByThisGame = 4;   // its area is this game's to run and its record is usable here: this game places it from the record (it holds until then)
const int kPlaceUnknown = 5;      // cannot be told yet: it holds as a listed note does
inline int Placeable(int engineHere, int areaHeld, int holderInWorld, int recordUsable, int placeFailed)
{
    if (engineHere == 1) return kPlaceYes;                   // held here: placed when its area loads here
    if (areaHeld == 0)
    {
        if (engineHere != 0) return kPlaceUnknown;           // the engine not asked yet: it may hold the group
        return (recordUsable != 0 && placeFailed == 0) ? kPlaceByThisGame : kPlaceNoGame;
    }
    if (areaHeld == 1)
    {
        if (holderInWorld == 1) return kPlaceYes;            // that game places it in the area it runs
        return (holderInWorld == 0 && engineHere == 0) ? kPlaceNoAbsent : kPlaceUnknown;
    }
    return kPlaceUnknown;                                    // who runs its area cannot be read
}
// NoPlacer: no game places a note with this Placeable answer - it holds nothing back.
inline int NoPlacer(int place) { return (place == kPlaceNoGame || place == kPlaceNoAbsent) ? 1 : 0; }

// NeedsLookup: is the engine's answer needed for Placeable? Only in an area this game runs, or in one held by a game not in the
// world; anywhere else whether the note holds is the same whatever the engine holds, so the engine is not walked for it.
inline int NeedsLookup(int areaHeld, int holderInWorld) { return (areaHeld == 0 || (areaHeld == 1 && holderInWorld == 0)) ? 1 : 0; }

// PlaceNow: does this game create a kPlaceByThisGame note from its record now? Only with every answer read at that moment: the
// engine asked afresh does not hold it (freshEngine 0), no other game holds its area (areaHeldNow 0) and whether the area is loaded
// here can be read (loadedHereNow 0 or 1 - a group is created asleep and wakes when its area loads). The engine holding it, or any
// answer that cannot be read, creates nothing.
inline int PlaceNow(int place, int freshEngine, int areaHeldNow, int loadedHereNow)
{
    return (place == kPlaceByThisGame && freshEngine == 0 && areaHeldNow == 0 && (loadedHereNow == 0 || loadedHereNow == 1)) ? 1 : 0;
}
// RaiseAfterAttempt: is the faction's group counter raised past a placed note's number? Only for a number of this game's own
// block (blockSlot = n / 65536 of "<faction>_<n>", -1 none; mySlot -1 = not known) - another game's numbers are never minted here.
inline int RaiseAfterAttempt(int blockSlot, int mySlot) { return (blockSlot >= 0 && mySlot >= 0 && blockSlot == mySlot) ? 1 : 0; }
// CounterFloor: a faction's counter floor after one record number n - one past n when n is in this game's own block (mySlot),
// never lower than floorNow; another block's number, or no block known, leaves it.
inline unsigned CounterFloor(unsigned n, int mySlot, unsigned floorNow)
{
    if (mySlot < 0 || (int)(n >> 16) != mySlot) return floorNow;
    return (n + 1u > floorNow) ? n + 1u : floorNow;
}

// NoteRole with the placer: a note that would be listed (kNoteNoPos / kNoteListed) but that no game can place is kNoteUnplaceable
// - it is not listed, and holds nothing back.
const int kNoteUnplaceable = 5;
inline int NoteRole(int hasTown, int deleted, int placedHere, int hasPos, int place)
{
    const int r = NoteRole(hasTown, deleted, placedHere, hasPos);
    if ((r == kNoteNoPos || r == kNoteListed) && NoPlacer(place) != 0) return kNoteUnplaceable;
    return r;
}

// Holds: does one listed note hold its town's creation back? (recSx, recSy) = the note's area, -1 when its position is not known
// here; recLoadedHere = 1 when that area is in this game's loaded set; (askSx, askSy) = the area asked for (the town's own, or the
// creation's spawn position when the town's will not read), -1 when neither reads.
inline int Holds(int recSx, int recSy, int recLoadedHere, int askSx, int askSy)
{
    if (recSx < 0 || recSy < 0) return 1;     // position unknown here: cannot be shown to be elsewhere
    if (recLoadedHere != 0) return 1;
    if (askSx < 0 || askSy < 0) return 1;     // the asked area is unread: the old, safe answer
    const int dx = recSx > askSx ? recSx - askSx : askSx - recSx;
    const int dy = recSy > askSy ? recSy - askSy : askSy - recSy;
    return (dx <= 1 && dy <= 1) ? 1 : 0;
}

// Pending: the whole rule for one note, as the store and the gate apply it together. 1 = it holds the town's creation back.
inline int Pending(int hasTown, int deleted, int placedHere, int hasPos, int recSx, int recSy, int recLoadedHere, int askSx, int askSy)
{
    const int role = NoteRole(hasTown, deleted, placedHere, hasPos);
    if (role == kNoteNoPos) return 1;
    if (role != kNoteListed) return 0;
    return Holds(recSx, recSy, recLoadedHere, askSx, askSy);
}
// Pending with the placer (`place` = Placeable): a note no game can place never holds.
inline int Pending(int hasTown, int deleted, int placedHere, int hasPos, int recSx, int recSy, int recLoadedHere, int askSx, int askSy, int place)
{
    if (NoteRole(hasTown, deleted, placedHere, hasPos, place) == kNoteUnplaceable) return 0;
    return Pending(hasTown, deleted, placedHere, hasPos, recSx, recSy, recLoadedHere, askSx, askSy);
}

// TownAnswer: the store's answer for one town (StoreTownPeoplePending) - `listedAreas` areas hold listed notes of the town,
// `heldAreas` of them hold its creation back (Holds).
const int kTownNotListed = 0;   // no listed note names the town
const int kTownElsewhere = 1;   // listed notes, all in areas that do not hold it: the creation goes on to the other gates
const int kTownHeld = 2;        // at least one listed note holds it: the creation is refused (decision 34)
inline int TownAnswer(int listedAreas, int heldAreas)
{
    if (listedAreas <= 0) return kTownNotListed;
    return heldAreas > 0 ? kTownHeld : kTownElsewhere;
}

// RerunKeep: a residents re-offer whose engine re-run happened keeps its entry (waits for the next check-up) when the re-run made
// no squad and the notebook-pending rule refused at least one; any squad made, or nothing refused (the engine chose none), ends it.
inline int RerunKeep(long long made, long long refusedPending)
{
    return (made <= 0 && refusedPending > 0) ? 1 : 0;
}

// AskArea: which area decision 34 is asked for, first that reads: the town's own (its centre); the building context's area (a
// residents re-run's building, or the load-time building context on this thread - the area the re-offer itself falls back to);
// the creation's spawn position; else -1 (every listed note holds). One cascade for the creation gate and the re-offer.
inline void AskArea(int townOk, int tsx, int tsy, int ctxOk, int csx, int csy, int spawnOk, int ssx, int ssy, int* ax, int* ay)
{
    if (townOk != 0) { *ax = tsx; *ay = tsy; return; }
    if (ctxOk != 0) { *ax = csx; *ay = csy; return; }
    if (spawnOk != 0) { *ax = ssx; *ay = ssy; return; }
    *ax = -1; *ay = -1;
}

// InRun: a creation counts for a residents re-run only on the re-run's own thread, while its slot is raised (runTid != 0) - the
// re-run is one synchronous engine call on that thread, so nothing another thread creates can count for it.
inline int InRun(unsigned long runTid, unsigned long curTid)
{
    return (runTid != 0 && runTid == curTid) ? 1 : 0;
}

// KeptGiveUp: a building whose re-run is kept waiting by RerunKeep is re-run at most kMaxKeptReruns times in a session; the
// re-run that reaches the bound gives the entry up (it is not offered again until the next session).
const int kMaxKeptReruns = 5;
inline int KeptGiveUp(int keptSoFar)
{
    return keptSoFar >= kMaxKeptReruns ? 1 : 0;
}

// LogDue: the refusal log for one town - the first refusal, then the first refusal after every 5 minutes, so a town that stays
// refused is visible in a player's log however long it lasts. GetTickCount milliseconds; unsigned subtraction survives the wrap.
const unsigned kRelogMs = 300000;
inline int LogDue(int loggedBefore, unsigned lastMs, unsigned nowMs)
{
    if (loggedBefore == 0) return 1;
    return (unsigned)(nowMs - lastMs) >= kRelogMs ? 1 : 0;
}

}   // namespace townpending
