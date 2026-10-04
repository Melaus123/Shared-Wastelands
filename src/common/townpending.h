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
