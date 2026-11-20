/* src/common/deadevent.h - T-1442 step 3 (F1435 design B): A WIPED-OUT NPC GROUP IS DECIDED AT ITS LAST MEMBER'S DEATH, AND ITS DELETE
 * GOES THEN - NOT WHEN ITS AREA LATER PUTS IT TO SLEEP.
 *
 * ONE RULE, limbevent.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a header, so the
 * offline suite drives it directly (the interlocked operations are compiler intrinsics, declared below - not calls).
 *
 * WHY IT EXISTS. Before this step the death watch (store.cpp DeathWatchTick, every 118-frame block) MARKED a group with nobody living
 * as wiped out and sent nothing; the delete went only when the area put the group to sleep or the engine destroyed it - with no upper
 * bound, so a wipe then a quit or crash before the area unloaded brought the group back alive (T-887). Now the death hook
 * (worldstate.cpp detour_declareDead, Character::declareDead - main OR worker thread, where nothing may allocate, lock or log) NOTES
 * the dead character's address here after the engine's call returns, and the main thread DRAINS the notes every frame
 * (store.cpp DeadEventDrainTick): it finds the awake world group whose member list holds that address (an address compare - the
 * character is never read through), runs the ONE judge the death watch uses (WipeVerdict below, fed from the same reads), and on
 * "wiped out" marks it and sends its delete at once. The death watch stays as the backstop: a group it marks that the event road did
 * not is logged '(backstop)' and deleted the same way. The later sleep / destroy finds the id already deleted and sends nothing.
 *
 * SHAPE. kDeadEvSlots fixed slots, each one address (0 = empty) and the tick of the death; nothing allocated. Add: an address already
 * queued is not queued again (cheap: a scan); otherwise the first empty slot is taken by compare-exchange; no empty slot = overflow,
 * counted - the death watch is the backstop and still decides that group on its own clock. Drain: each slot is emptied by exchange.
 * The tick is a LOG value only (the decision line's "after the last death"): it is written before the slot is claimed, so a race
 * can at worst show a neighbouring death's time.
 *
 * THREADING CONTRACT. Any number of adders (any thread: DeadEvAdd), ONE drainer (the main thread: DeadEvDrain, DeadEvReset).
 *
 * C++03 (VS2010 v100, x64): no auto, no nullptr, no range-for, no <atomic>.
 */
#ifndef COOP_COMMON_DEADEVENT_H
#define COOP_COMMON_DEADEVENT_H

#if defined(_MSC_VER)
extern "C" __int64 _InterlockedCompareExchange64(__int64 volatile* Destination, __int64 Exchange, __int64 Comparand);
extern "C" __int64 _InterlockedExchange64(__int64 volatile* Target, __int64 Value);
extern "C" long _InterlockedExchange(long volatile* Target, long Value);
extern "C" long _InterlockedIncrement(long volatile* Addend);
extern "C" long _InterlockedDecrement(long volatile* Addend);
#pragma intrinsic(_InterlockedCompareExchange64, _InterlockedExchange64, _InterlockedExchange, _InterlockedIncrement, _InterlockedDecrement)
#endif

namespace coopdeadev {

enum { kDeadEvSlots = 128 };
enum { kDeadEvOverflow = 0, kDeadEvNoted = 1, kDeadEvAlready = 2, kDeadEvNoAddr = -1 };

struct DeadEvTable
{
    volatile __int64 addr[kDeadEvSlots];   /* a noted character's address (0 = empty) */
    volatile long tick[kDeadEvSlots];      /* the death's tick (milliseconds, the caller's clock) - a log value */
    volatile long pending;                 /* addresses in the table now - the drain's fast path */
    volatile long noted;                   /* adds that took a slot, all time */
    volatile long already;                 /* adds whose address was already queued, all time */
    volatile long overflow;                /* adds that found no empty slot, all time (the death watch decides those) */
};

/* MAIN THREAD. Empties one slot by exchange; `pending` goes down only for a slot that held an address. */
inline void DeadEvResetSlot(DeadEvTable* t, int i)
{
    if (t == 0 || i < 0 || i >= (int)kDeadEvSlots) return;
    if (_InterlockedExchange64(&t->addr[i], 0) != 0) _InterlockedDecrement(&t->pending);
}
/* MAIN THREAD (the world teardown; the suite). Empties every slot - the notes are dropped, the all-time counters are kept. A static
   DeadEvTable starts empty. */
inline void DeadEvReset(DeadEvTable* t)
{
    int i;
    if (t == 0) return;
    for (i = 0; i < (int)kDeadEvSlots; ++i) DeadEvResetSlot(t, i);
}

/* ANY THREAD (the death hook: no allocation, no lock). kDeadEvNoted, kDeadEvAlready (that address waits in the table now),
   kDeadEvOverflow (no empty slot - counted) or kDeadEvNoAddr (address 0). */
inline int DeadEvAdd(DeadEvTable* t, unsigned long long addr, unsigned long tickMs)
{
    int i;
    if (t == 0 || addr == 0) return kDeadEvNoAddr;
    const __int64 v = (__int64)addr;
    for (i = 0; i < (int)kDeadEvSlots; ++i)   /* fold 3b: the duplicate scan by plain (volatile) reads - a miss only costs a second slot, which the drain judges once */
        if (t->addr[i] == v) { _InterlockedIncrement(&t->already); return kDeadEvAlready; }
    for (i = 0; i < (int)kDeadEvSlots; ++i)
    {
        if (t->addr[i] != 0) continue;
        _InterlockedExchange(&t->tick[i], (long)tickMs);   /* before the claim: the drain reads it after taking the address */
        if (_InterlockedCompareExchange64(&t->addr[i], v, 0) != 0) continue;
        _InterlockedIncrement(&t->pending);
        _InterlockedIncrement(&t->noted);
        return kDeadEvNoted;
    }
    _InterlockedIncrement(&t->overflow);
    return kDeadEvOverflow;
}

/* MAIN THREAD. Empties the table into out[0..max) (and each death's tick into ticks[], when given); returns how many were taken. A
   slot past `max` stays for the next drain. An address taken twice (two adders raced past the scan) comes out twice: the caller
   judges its group once per drain, and a group already marked is 'already decided'. */
inline int DeadEvDrain(DeadEvTable* t, unsigned long long* out, unsigned long* ticks, int max)
{
    int i, n = 0;
    if (t == 0 || out == 0 || max <= 0 || t->pending == 0) return 0;
    for (i = 0; i < (int)kDeadEvSlots && n < max; ++i)
    {
        if (t->addr[i] == 0) continue;
        const __int64 v = _InterlockedExchange64(&t->addr[i], 0);
        if (v == 0) continue;
        _InterlockedDecrement(&t->pending);
        if (ticks != 0) ticks[n] = (unsigned long)t->tick[i];
        out[n++] = (unsigned long long)v;
    }
    return n;
}

/* ---- THE ONE JUDGE: is this awake world group wiped out, and is it this game's to declare so? -------------------------------- */
/* The death watch's per-group test (store.cpp, moved here unchanged in order and meaning; both the watch and the event drain call it
   through store.cpp WipeJudge, which reads the inputs). Inputs, all plain values:
     lent         - a raid copy lent to this game (RaidLentHas): its lender ends it, never a wipe-out mark here
     living       - LivingMembersPod: > 0 living members, 0 only bodies, -1 not read, -2 not awake here
     haveId       - the group has a world id (WorldIdOf)
     marked       - the id is already marked wiped out (g_deadPending)
     mySlot       - this game's relay slot (-1 unknown)
     blk          - the squad's origin slot (SquadOriginSlot: its number's block, or for a save squad the game that runs it; -1 unknown)
     run          - the slot that ran a strict majority of its people (MembersRunBySlot; -1 none)
     holderWrite  - the writer rule's answer is "the area's holder writes it" (coopsquad::kNpcWriteHolder)
     strayRefused - the writer rule's answer is "a stray copy" (coopsquad::kNpcWriteRefuseStray)
     podOk        - the member list was read (the 'mine' count below is meaningful)
     mine         - members this game owns (MembersOwnership)
     engineKeeps  - 1 = the engine keeps this group for its rebuild (WatchEngineKeepsPod; -1 unreadable = not kept)
     npc          - a world NPC squad (not a context platoon)
     fresh        - this copy is the world's version (NpcRecordVsCopy: 0 = another game's newer record wins) */
enum { kWipeLent = 0, kWipeAlive = 1, kWipeUnknown = 2, kWipeNoId = 3, kWipeAlready = 4, kWipeOtherBlock = 5, kWipeNotMine = 6,
       kWipeEngineKeeps = 7, kWipeStray = 8, kWipeNotFresh = 9, kWipeYes = 10, kWipeVerdicts = 11 };
struct WipeIn
{
    int lent, living, haveId, marked, mySlot, blk, run, holderWrite, strayRefused, podOk, mine, engineKeeps, npc, fresh;
};
inline WipeIn WipeInNone()
{
    WipeIn w;
    w.lent = 0; w.living = -1; w.haveId = 0; w.marked = 0; w.mySlot = -1; w.blk = -1; w.run = -1; w.holderWrite = 0; w.strayRefused = 0;
    w.podOk = 0; w.mine = 0; w.engineKeeps = -1; w.npc = 1; w.fresh = 1;
    return w;
}
/* the area's holder marks a squad numbered in another game's block (the watch's dwHolderOtherBlock) */
inline int WipeHolderOtherBlock(const WipeIn& w)
{
    return (w.holderWrite != 0 && w.mySlot >= 0 && w.blk >= 0 && w.blk != w.mySlot) ? 1 : 0;
}
/* this game ran a strict majority of the squad's people (the watch's dwRunsHere) */
inline int WipeRunsHere(const WipeIn& w) { return (w.mySlot >= 0 && w.run == w.mySlot) ? 1 : 0; }
/* the counter the watch keeps once per squad: this game marks a squad of another block because it ran its people */
inline int WipeRunnerPastBlock(const WipeIn& w)
{
    return (WipeHolderOtherBlock(w) == 0 && WipeRunsHere(w) != 0 && w.mySlot >= 0 && w.blk >= 0 && w.blk != w.mySlot) ? 1 : 0;
}
inline int WipeVerdict(const WipeIn& w)
{
    if (w.lent != 0) return kWipeLent;
    if (w.living > 0) return kWipeAlive;
    if (w.living < 0) return kWipeUnknown;
    if (w.haveId == 0) return kWipeNoId;
    if (w.marked != 0) return kWipeAlready;
    if (WipeHolderOtherBlock(w) == 0 && WipeRunsHere(w) == 0 && w.mySlot >= 0 && w.blk >= 0 && w.blk != w.mySlot) return kWipeOtherBlock;
    {
        const int mineByBlock = (w.mySlot >= 0 && w.blk == w.mySlot) ? 1 : 0;
        if (w.podOk != 0 && mineByBlock == 0 && w.mine == 0) return kWipeNotMine;
    }
    if (w.engineKeeps == 1) return kWipeEngineKeeps;
    if (w.strayRefused != 0) return kWipeStray;
    if (w.npc != 0 && w.fresh == 0) return kWipeNotFresh;
    return kWipeYes;
}

/* ---- the roads after the decision --------------------------------------------------------------------------------------------- */
/* Which road made the decision: the event drain (the frame after the last death) or the death watch (its 118-frame block - the
   backstop, for a group the event road did not decide: an overflowed note, a death the hook did not note, a group seen dead at load). */
enum { kRoadEvent = 0, kRoadBackstop = 1 };
/* fold 3b: a decision (the mark AND its delete) is taken only with the store on, its folder ready and the engine's writes allowed (not
   during a load or a teardown) - the old sleep send stopped at the same gate. Held, the group is decided later: the event drain drops
   its notes (counted), the death watch judges it again at its next block. 1 = decide now. */
inline int DecideNow(int storeOn, int dirReady, int engineWritesBlocked) { return (storeOn != 0 && dirReady != 0 && engineWritesBlocked == 0) ? 1 : 0; }
/* 1 = the decision sends the delete now. Both roads do: the mark and the delete are one act since this step. */
inline int DecisionSends(int verdict) { return verdict == kWipeYes ? 1 : 0; }
/* The later sleep / destroy of a group deleted at its decision: 1 = it sends a delete (only when the decision did not). */
inline int LaterRoadSends(int deletedAtDecision) { return deletedAtDecision != 0 ? 0 : 1; }
/* fold 3b: THE SLEEP OF A GROUP, by its two notes - marked (g_deadPending) and deletedAtDecision (g_deadEarly). kSleepDelete: marked and
   not yet deleted - the delete goes now (a mark from before this step's roads; none today). kSleepShellOnly: deleted at its decision -
   nothing is sent and nothing refused is counted; only its shell is destroyed next tick. kSleepOrdinary: neither - the ordinary sleep
   (a delete the world server refused is undone with both notes, store.cpp SwUndoLocalDelete, so the squad is written as it lives). */
enum { kSleepOrdinary = 0, kSleepDelete = 1, kSleepShellOnly = 2 };
inline int SleepRoad(int marked, int deletedAtDecision)
{
    if (deletedAtDecision != 0) return kSleepShellOnly;
    return marked != 0 ? kSleepDelete : kSleepOrdinary;
}
/* fold 3b / 3d: the world server refused this game's DELETE of the squad (another game writes it): the deleted mark is taken back AND
   the decision's two notes (marked, deletedAtDecision) - so no later road sends the delete again, and the death watch judges the squad
   afresh (the writer gate then refuses it). A refused DISCARD leaves them: a discard is sent only for a squad that did not die out
   (squadbirth.h DestroyDelete: kDelDiscard needs diedOut == 0), so it is never the decision's send. 1 = the notes go. */
inline int RefusalClearsDecision(int refusedKindIsDelete, int marked, int deletedAtDecision)
{
    return (refusedKindIsDelete != 0 && (marked != 0 || deletedAtDecision != 0)) ? 1 : 0;
}
/* fold 3d: THE SLEEP JUDGES TOO. A decision the gate held (DecideNow 0: the last death came during a load or with the store not ready)
   is retried by the death watch only while the group is awake; if its area unloads first, the sleep itself judges the group (the same
   WipeJudge, before the engine's sleep destroys the members) behind the sleep road's own gate - the store on, its folder ready, this
   game's own sleep of the squad (not the engine building a sleeping squad from its record), not a lent raid copy - so no wipe-out slips
   past its sleep whatever the timing. 1 = judge at this sleep. */
inline int SleepJudges(int storeOn, int dirReady, int ownSleep, int lent)
{
    return (storeOn != 0 && dirReady != 0 && ownSleep != 0 && lent == 0) ? 1 : 0;
}
/* fold 3d: the sleep's judge said wiped out (WipeVerdict kWipeYes) - the group is marked at its sleep and the sleep road deletes it
   (SleepRoad(1, 0) = kSleepDelete). 1 = mark. */
inline int SleepMarks(int verdict) { return verdict == kWipeYes ? 1 : 0; }

/* the event decision line's and the backstop line's caps per launch */
const long long kDeadEvLogCap = 100;
const long long kDeadBackstopLogCap = 40;

}   // namespace coopdeadev

#endif
