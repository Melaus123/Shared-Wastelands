#pragma once
/* src/common/groundsync.h - T-697 STEP 2: WHAT A GAME DOES WITH AN AREA'S GROUND RECORD (groundrec.h).
 *
 * The area's holder writes the area's whole ground as one record; a game keeps the newest record of each area it is told of; the
 * holder of a loaded, settled area applies the stored record to its ground before it writes (after a restart the record is the only
 * memory of which items lay there). These are the decisions, without the engine:
 *
 *   GsKeepArrived    - an arriving record replaces the kept copy unless its generation is lower
 *   GsApplyNeeded    - the stored record is newer than the ground this game has (applied or written)
 *   GsWriteChoice    - seed (this game holds no copy) or update (known generation + 1)
 *   GsRefuseAnswer   - what a game does with the world server's GROUND_REFUSE (message 71)
 *   GsStoredWait     - wait for the stored record the world server named, or give the area up after a bounded wait (fold 1, L1)
 *   GsApplyPlan      - per record row: keep / take over / build; per local item no row lists: remove (only after a whole walk of a
 *                      record that lists every item; never a guarded item)
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <string>
#include <vector>
#include "groundrec.h"

namespace coopgsync {

/* ---- generations ---- */
/* 1 = the arriving record (generation newGen) is kept: this game has no copy of the area's record, or newGen is not lower than the
   kept copy's (an equal generation is the writer's own echo, or the same record again) */
inline int GsKeepArrived(int haveCopy, unsigned int copyGen, unsigned int newGen)
{
    if (haveCopy == 0) return 1;
    return newGen >= copyGen ? 1 : 0;
}
/* 1 = the stored record must be applied to this game's ground: one is stored, and this game's ground is not already that record or a
   newer one (haveMine 0: nothing applied or written here since the start - a restarted game always applies) */
inline int GsApplyNeeded(int haveStored, unsigned int storedGen, int haveMine, unsigned int mineGen)
{
    if (haveStored == 0) return 0;
    if (haveMine == 0) return 1;
    return storedGen > mineGen ? 1 : 0;
}
/* The next write: a SEED (generation 1) when this game knows of no record of the area, else an UPDATE at known + 1. Returns 1 = seed,
   0 = update; *genOut the generation to write. */
const unsigned int kGsSeedGen = 1u;
inline int GsWriteChoice(int haveCopy, unsigned int knownGen, unsigned int* genOut)
{
    if (haveCopy == 0) { if (genOut) *genOut = kGsSeedGen; return 1; }
    if (genOut) *genOut = knownGen + 1u;
    return 0;
}

/* ---- the world server's refusal (GROUND_REFUSE) ---- */
/* fold 1 (M3): an OLDER or SAME-GENERATION refusal means another holder's record is stored past this game's ground - it is applied
   (waited for when not here yet) before this game writes again; re-sending stored + 1 from this game's own ground would erase that
   holder's changes. There is no "write again" answer any more. */
const int kGsActApplyStored = 2;  /* a seed but a record exists, older, or the same generation with other bytes: apply the stored
                                     record first (wait for it), then write an update */
const int kGsActSeed = 3;         /* an update, but no record exists: forget the copy, write a seed */
const int kGsActDrop = 4;         /* not the holder, or a write that can never be taken: nothing is resent */
inline int GsRefuseAnswer(int reason, unsigned int storedGen, unsigned int* knownOut)
{
    if (knownOut) *knownOut = storedGen;
    if (reason == coopgrec::kGrRefuseOlder || reason == coopgrec::kGrRefuseSameGen || reason == coopgrec::kGrRefuseSeedExists) return kGsActApplyStored;
    if (reason == coopgrec::kGrRefuseNoRecord) return kGsActSeed;
    return kGsActDrop;   /* kGrRefuseNotHolder, and the reasons a correct writer never earns (id, bytes, area, writer slot, position) */
}
inline const char* GsActName(int act)
{
    return act == kGsActApplyStored ? "the stored record is applied first, then an update is written"
         : act == kGsActSeed ? "the copy is forgotten and a seed is written"
         : act == kGsActDrop ? "dropped (nothing resent)" : "?";
}

/* ---- waiting for the stored record (fold 1, L1) ---- */
/* The holder must apply the record the world server named (knownGen) before it writes; it waits while that record has not arrived
   here (none kept, or the kept one is older). A record that never arrives - it was unreadable here, or its message was lost - would
   hold the area forever, so the wait is BOUNDED: past kGsStoredWaitMs the area is given up (no apply, no write - writing past a record
   this game cannot read would erase its contents) until a readable record arrives, the link is renewed or the world is torn down.
   Returns kGsWaitNo (nothing to wait for), kGsWaitYes, kGsWaitGiveUp. */
const unsigned int kGsStoredWaitMs = 30000u;
const int kGsWaitNo = 0, kGsWaitYes = 1, kGsWaitGiveUp = 2;
inline int GsStoredWait(int knownHave, unsigned int knownGen, int haveMine, unsigned int mineGen, int srvHave, unsigned int srvGen,
                        unsigned int waitedMs)
{
    if (knownHave == 0 || knownGen <= (haveMine != 0 ? mineGen : 0u)) return kGsWaitNo;
    if (srvHave != 0 && srvGen >= knownGen) return kGsWaitNo;
    return waitedMs >= kGsStoredWaitMs ? kGsWaitGiveUp : kGsWaitYes;
}

/* ---- fold 2 (item 4): the same generation, other bytes ---- */
/* A record arrives at the very generation this game's ground is (mineGen) but with rows other than this game's last write of it
   (lastTail, kept through a link loss): that write was lost and another holder stored the same number. Without this the stored
   record would never be applied (stored > mine is false) and this game's next write would replace it. 1 = apply it (forced). */
inline int GsSameGenOther(int haveMine, unsigned int mineGen, unsigned int newGen, const std::vector<char>& lastTail, const std::vector<char>& newTail)
{
    if (haveMine == 0 || newGen != mineGen || lastTail.empty()) return 0;
    return newTail == lastTail ? 0 : 1;
}
/* ---- fold 2 (item 7): one write in flight per area ---- */
/* A write is answered by the world server's RECORD_SEQ echo (taken) or GROUND_REFUSE; until then (bounded by kGsFlightMaxMs - a write
   the server could not store gets neither) the area is not written again, so a second write cannot undo a refusal's answer. 1 = held. */
const unsigned int kGsFlightMaxMs = 10000u;
inline int GsFlightHolds(int inFlight, unsigned int ageMs) { return (inFlight != 0 && ageMs < kGsFlightMaxMs) ? 1 : 0; }

/* ---- fold 3 (MED-1): an area written with items left out while unconfirmed stays owed ---- */
/* A write that left out notConfirmed items keeps the area marked, so the next write (no sooner than kGsUnconfRetryMs) carries them
   once they settle. Bounded: past kGsUnconfMaxMs (longer than the drop re-send and claim limits) an item that never settles stops
   re-marking the area (a real change still marks it). sinceMs: how long items have been left out. 1 = keep the area marked. */
const unsigned int kGsUnconfRetryMs = 3000u;
const unsigned int kGsUnconfMaxMs = 90000u;
inline int GsUnconfKeep(unsigned int notConfirmed, unsigned int sinceMs)
{
    if (notConfirmed == 0) return 0;
    return sinceMs < kGsUnconfMaxMs ? 1 : 0;
}
/* fold 4 (item 2): the clock restarts when the count of left-out items FALLS (one settled - the rest are newer or still settling);
   a rise keeps it. 1 = restart (and a give-up is lifted). */
inline int GsUnconfRestart(unsigned int prevCount, unsigned int nowCount) { return nowCount < prevCount ? 1 : 0; }
/* fold 4 (item 1): how an unconfirmed item is written. Before the give-up: left out and counted in the cut (an apply of the record
   removes nothing). After it (kGsUnconfMaxMs): an item that is LEAVING this ground (a removal waiting for its claim, an item held for a
   TAKE) is left out WITHOUT a cut - it is going, and a row would bring it back on a later apply; any other (a drop never confirmed -
   it lies on this holder's ground) is written as a normal row, so the record is whole again and applies can remove. */
const int kGsUnconfCut = 1, kGsUnconfOmit = 2, kGsUnconfRow = 3;
inline int GsUnconfHow(int gaveUp, int leaving)
{
    if (gaveUp == 0) return kGsUnconfCut;
    return leaving != 0 ? kGsUnconfOmit : kGsUnconfRow;
}

/* ---- the apply plan ---- */
const int kGsGuardSpill = 1;    /* a spill / copy item still to be removed here: never kept, taken over or removed by the plan */
const int kGsGuardOwn = 2;      /* this game's own unconfirmed or pending drop, a removal waiting for its claim, an item held for a TAKE:
                                   never taken over or removed (a name it already carries still keeps it) */
const int kGsGuardRecent = 4;   /* fold 1 (M6): added to (fold 2: or changed on) this ground within the last ~10 s (a record from around a
                                   holder change can be older than it): never removed, its quantity never set (it may still be kept or taken over) */
struct GsLocal
{
    std::string name;   /* the name the item carries here, "" = none */
    std::string key;    /* its current key */
    int qty, q100, guard;
    GsLocal() : qty(1), q100(0), guard(0) {}
};
struct GsRow
{
    std::string name;   /* the record row's name */
    std::string key;    /* the key the row's item has at the row's position (the caller composes it), "" = none */
    int qty, q100;
    int gone;           /* fold 1 (M6): the row's name left this ground recently (the removed-name memory): never built */
    int made;           /* the row's item was made during play (groundrec.h kGrMade): not built back when it is missing here */
    GsRow() : qty(1), q100(0), gone(0), made(0) {}
};
const int kGsKeep = 1, kGsTakeOver = 2, kGsBuild = 3, kGsSkipGone = 4;   /* kGsSkipGone: no item here, and its name left recently - not built */
const int kGsSkipMade = 5;   /* no item here, and the row's item was made during play - the game's own area close dropped it: not built */
struct GsPlan
{
    std::vector<int> act;            /* per row: kGsKeep / kGsTakeOver / kGsBuild / kGsSkipGone / kGsSkipMade */
    std::vector<int> local;          /* per row: the local item it keeps or takes over, -1 = built or skipped */
    std::vector<int> fixQty;         /* fold 1 (M5): per row, 1 = the kept / taken-over item's quantity is not the row's - set it to the row's */
    std::vector<size_t> remove;      /* local items no row lists, to remove */
    int kept, taken, built, removeHeld, guarded, skippedGone, skippedMade, qtyFixes, qtyGuarded;   /* removeHeld: unlisted items left alone because the walk or the record was not whole; qtyGuarded: quantity fixes left out on a guarded item */
    GsPlan() : kept(0), taken(0), built(0), removeHeld(0), guarded(0), skippedGone(0), skippedMade(0), qtyFixes(0), qtyGuarded(0) {}
};
/* the candidate among locals[] (not used, matching `want`) best for the row: same quantity and quality, then same quantity, then the
   first. -1 = none. mode 0: carries the row's name; 1: carries no name and its key is the row's name; 2: carries no name, its key is
   the row's key, and it is not guarded. A spill is never a candidate. */
inline int GsPick(const std::vector<GsLocal>& locals, const std::vector<int>& used, const GsRow& row, int mode)
{
    int best = -1, bestRank = -1;
    for (size_t i = 0; i < locals.size(); ++i)
    {
        const GsLocal& l = locals[i];
        if (used[i] != 0 || (l.guard & kGsGuardSpill) != 0) continue;
        if (mode == 0) { if (l.name.empty() || l.name != row.name) continue; }
        else if (mode == 1) { if (!l.name.empty() || l.key != row.name) continue; }
        else { if (!l.name.empty() || (l.guard & kGsGuardOwn) != 0 || row.key.empty() || l.key != row.key) continue; }
        const int rank = (l.qty == row.qty) ? ((l.q100 == row.q100) ? 2 : 1) : 0;
        if (rank > bestRank) { best = (int)i; bestRank = rank; }
    }
    return best;
}
/* The plan for `rows` (one record) over `locals` (every keyed ground item of the area here). wholeWalk: the walk of this game's ground
   was not cut; recordCut: the record's header says rows were left out. Names win over keys: every row first keeps an item carrying
   its name, then an unnamed item whose key is its name, then takes over an unnamed item of its key; the rest are built, except a row
   whose name just left this ground (skipped) and a row whose item was made during play (skipped: the game's own area close drops
   such an item, so it stays gone, as in the normal game). A kept or taken-over item whose quantity is not the row's is marked to be set to the
   row's. An item no row used is removed only when the walk and the record are whole, and never when guarded (counted in `guarded`). */
inline void GsApplyPlan(const std::vector<GsRow>& rows, const std::vector<GsLocal>& locals, int wholeWalk, unsigned int recordCut, GsPlan* out)
{
    GsPlan p;
    p.act.assign(rows.size(), kGsBuild);
    p.local.assign(rows.size(), -1);
    p.fixQty.assign(rows.size(), 0);
    std::vector<int> used(locals.size(), 0);
    for (int mode = 0; mode < 3; ++mode)
        for (size_t r = 0; r < rows.size(); ++r)
        {
            if (p.local[r] >= 0) continue;
            const int k = GsPick(locals, used, rows[r], mode);
            if (k < 0) continue;
            used[(size_t)k] = 1;
            p.local[r] = k;
            p.act[r] = (mode == 2) ? kGsTakeOver : kGsKeep;
        }
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if (p.act[r] == kGsBuild && rows[r].gone != 0) p.act[r] = kGsSkipGone;   /* M6: never rebuild a name that just left this ground */
        else if (p.act[r] == kGsBuild && rows[r].made != 0) p.act[r] = kGsSkipMade;   /* made during play and not here: the area close dropped it */
        if (p.act[r] == kGsKeep) ++p.kept;
        else if (p.act[r] == kGsTakeOver) ++p.taken;
        else if (p.act[r] == kGsSkipGone) ++p.skippedGone;
        else if (p.act[r] == kGsSkipMade) ++p.skippedMade;
        else ++p.built;
        if (p.local[r] >= 0 && locals[(size_t)p.local[r]].qty != rows[r].qty && rows[r].qty > 0)
        {   /* M5; fold 2 (item 1): never for a guarded item - one held for a TAKE or a pick in flight, this game's own unconfirmed drop, or
               one added or changed here in the last seconds (a record older than that change would put picked units back) */
            if (locals[(size_t)p.local[r]].guard != 0) ++p.qtyGuarded;
            else { p.fixQty[r] = 1; ++p.qtyFixes; }
        }
    }
    for (size_t i = 0; i < locals.size(); ++i)
    {
        if (used[i] != 0) continue;
        if (locals[i].guard != 0) { ++p.guarded; continue; }
        if (wholeWalk == 0 || recordCut != 0) { ++p.removeHeld; continue; }
        p.remove.push_back(i);
    }
    if (out) *out = p;
}

}   /* namespace coopgsync */
