/* src/coop-test/suite_part6.cpp - THE OFFLINE SUITE, part 6 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"

namespace suite {

void t_t546c_fold3()
{
    using namespace coopwq;
    if (QuitSaveRoute(true, false) != kQuitSaveHere || QuitSaveRoute(true, true) != kQuitSaveHere) Fail("a quit on the loop's own thread saves there, at start-up or in the loop");
    if (QuitSaveRoute(false, true) != kQuitSaveAskLoop) Fail("a quit on another thread asks the running loop");
    if (QuitSaveRoute(false, false) != kQuitSaveNone) Fail("a quit on another thread before the loop starts touches nothing of the loop's");
    if (!QuitSaveFile(true, false)) Fail("unsaved changes are saved at quit");
    if (QuitSaveFile(false, false)) Fail("nothing unsaved: nothing written at quit");
    if (QuitSaveFile(true, true) || QuitSaveFile(false, true)) Fail("a file unreadable at start is never rewritten, not even at quit");
    if (swteam::SideOutcome(-3) != swteam::kSideRetry) Fail("not writable now (no world, an address missing, the entry unread): tried again");
    if (swteam::SideOutcome(-1) != swteam::kSideNothing) Fail("only a faction the list provably lacks is nothing to write");
    if (swteam::SideOutcome(-2) != swteam::kSideRetry || swteam::SideOutcome(0) != swteam::kSideRetry) Fail("the setter raised, or any other failure: tried again");
}
void t_t546c_npc_entry_roads()
{
    using namespace swteam;
    const int me = 1, x = 2, s = 3, y = 4;   /* this game's player faction, an NPC faction, another player's stand-in, a second NPC faction */
    /* the engine's own roads towards this game's player faction: every one moves X -> me (dir 1, the entry this game reads) */
    if (NpcDirOfCall(relside::kAffect, false, x, me, me, true) != 1) Fail("a witnessed crime (X's relations towards me) moves X -> me");
    if (NpcDirOfCall(relside::kAffectEvent, false, x, me, me, true) != 1) Fail("a fight (X's relations towards me, by event) moves X -> me");
    const int onMine[] = { relside::kAffect, relside::kAffectEvent, relside::kDeclareWar, relside::kNoLongerEnemies, relside::kSetEnemy, relside::kAffectTrust };
    for (int i = 0; i < 6; ++i)
        if (NpcDirOfCall(onMine[i], true, me, x, me, true) != 1) Fail("a changer on this player's relations towards X (war, peace, trust) moves X -> me, never the unread mine -> X");
    /* setRelation writes the object's own table: the mod's team writes name their entry outright */
    if (NpcDirOfCall(relside::kSetRelation, true, me, x, me, true) != 0) Fail("setRelation on this player's relations writes mine -> X (read only on the other games)");
    if (NpcDirOfCall(relside::kSetRelation, false, x, me, me, true) != 1) Fail("setRelation on X's relations towards me writes X -> me");
    if (NpcDirOfCall(relside::kNotAChanger, true, me, x, me, true) != 0 || NpcDirOfCall(relside::kNotAChanger, false, x, me, me, true) != 1) Fail("a read names its pair as called");
    /* another player's faction as it is held HERE (a stand-in: a plain relations object, never this game's): nothing of this
       game's NPC standing moves - that player's own game owns and marks it */
    for (int i = 0; i < 6; ++i)
    {
        if (NpcDirOfCall(onMine[i], false, s, x, me, true) != kDirNone) Fail("a changer on a stand-in's relations is not this game's standing");
        if (NpcDirOfCall(onMine[i], false, x, s, me, true) != kDirNone) Fail("X's relations towards a stand-in is not this game's standing");
    }
    if (NpcDirOfCall(relside::kSetRelation, false, s, x, me, true) != kDirNone) Fail("the stand-in's own table is that player's");
    /* not an NPC counterpart, or no side of mine: nothing marked as the team's NPC standing */
    if (NpcDirOfCall(relside::kDeclareWar, true, me, s, me, false) != kDirNone) Fail("towards another player's faction: not an NPC standing (the stance road)");
    if (NpcDirOfCall(relside::kAffect, false, x, y, me, true) != kDirNone) Fail("world-vs-world is not this game's standing");
    if (NpcDirOfCall(relside::kAffect, true, me, me, me, true) != kDirNone) Fail("the player's self entry is no NPC standing");
    if (NpcDirOfWritten(true, false, true) != 0 || NpcDirOfWritten(false, true, true) != 1 || NpcDirOfWritten(false, false, true) != kDirNone || NpcDirOfWritten(true, false, false) != kDirNone)
        Fail("NpcDirOfWritten: 0 mine -> X, 1 X -> mine, else none");
    /* the team's writes of one record row: X -> me whenever the row holds it, mine -> X whenever it holds that */
    if (NpcEntriesOf(kHaveFwd | kHaveBack) != (unsigned)(kWriteItMine | kWriteMineIt)) Fail("a row with both directions writes both entries");
    if (NpcEntriesOf(kHaveBack) != (unsigned)kWriteItMine) Fail("a row with only X -> team writes only the entry read here");
    if (NpcEntriesOf(kHaveFwd) != (unsigned)kWriteMineIt) Fail("a row with only team -> X writes only the stand-in's table entry");
    if (NpcEntriesOf(0) != 0u) Fail("a row with nothing writes nothing");
    /* a member's crime reaches the record on the side that moved: dir 1 moves relBack and leaves rel */
    TeamRec r; RecRow row; row.sid = "17-gamedata.base"; row.rel = 5.0f; row.relBack = 10.0f; row.have = kHaveFwd | kHaveBack; r.rows.push_back(row);
    Delta d; d.sid = row.sid; d.dir = (unsigned)NpcDirOfCall(relside::kAffect, false, x, me, me, true); d.dRel = -20.0f; d.rel = -10.0f; d.flags = 0;
    if (!ApplyDelta(&r, d) || r.rows[0].relBack != -10.0f || r.rows[0].rel != 5.0f) Fail("a crime's change moves the record's X -> team, never team -> X");
    /* a war on a member's own relations (the record has no X -> team yet): the member's value is taken on that side */
    TeamRec r2; RecRow row2; row2.sid = "18-gamedata.base"; row2.rel = 0.0f; row2.have = kHaveFwd; r2.rows.push_back(row2);
    Delta w; w.sid = row2.sid; w.dir = (unsigned)NpcDirOfCall(relside::kDeclareWar, true, me, x, me, true); w.dRel = -35.0f; w.rel = -35.0f; w.flags = 2;
    if (!ApplyDelta(&r2, w) || r2.rows[0].relBack != -35.0f || (r2.rows[0].have & kHaveBack) == 0 || r2.rows[0].rel != 0.0f || r2.rows[0].flagsBack != 2u)
        Fail("a war's change lands on X -> team with its flags");
    if (NpcEntriesOf(r2.rows[0].have) != (unsigned)(kWriteItMine | kWriteMineIt)) Fail("after it, both entries are written on every member's game");
}

/* ---- T-563: which entry a faction-relations changer wrote (src/common/relside.h) ---- */
void t_t563_written_pair()
{
    using namespace relside;
    const int me = 1, x = 2, s = 3;
    int o = 0, t = 0;
    const int lookups[] = { kAffect, kAffectEvent, kDeclareWar, kNoLongerEnemies, kSetEnemy, kAffectTrust };
    for (int i = 0; i < 6; ++i)
    {
        WrittenPair(lookups[i], true, me, x, &o, &t);
        if (o != x || t != me) Fail("a changer that finds its entry through the lookup, on the player's relations, wrote X -> player");
        WrittenPair(lookups[i], true, me, s, &o, &t);
        if (o != s || t != me) Fail("towards another player's faction it wrote that faction's side towards this player");
        WrittenPair(lookups[i], false, x, me, &o, &t);
        if (o != x || t != me) Fail("on an NPC faction's relations the pair is as called (the crime and fight roads)");
        WrittenPair(lookups[i], true, me, me, &o, &t);
        if (o != me || t != me) Fail("the player's own self entry is not swapped");
    }
    WrittenPair(kSetRelation, true, me, x, &o, &t);
    if (o != me || t != x) Fail("setRelation writes the object's own table: the pair as called");
    WrittenPair(kNotAChanger, true, me, x, &o, &t);
    if (o != me || t != x) Fail("a read of the pair as named (the snapshot) is never swapped");
    if (WritesOtherSide(kDeclareWar, false, false)) Fail("not the player's relations: no swap");
    if (!WritesOtherSide(kAffectTrust, true, false)) Fail("affectTrust reaches its entry through the lookup");
    if (WritesOtherSide(kSetRelation, true, false) || WritesOtherSide(kNotAChanger, true, false) || WritesOtherSide(99, true, false))
        Fail("setRelation, a read and an unknown changer are never swapped");
}

/* T-563 fold 1: where a forwarded change goes - the written pair's road, the moment, the snapshot's player-side rows */
void t_t563_forward_roads()
{
    using namespace relside;
    Side me = { true, false, false }, npc = { false, false, true }, npc2 = { false, false, true }, other = { false, true, false };
    /* a changer on the player's relations towards an NPC faction: swapped to X -> me, which this game owns and sends */
    { int o = 0, t = 0; WrittenPair(kDeclareWar, true, 1, 2, &o, &t); if (o != 2 || t != 1) Fail("on the player's relations towards X: X -> me written"); }
    if (PairRoad(npc, me) != kRoadSend) Fail("swap -> X -> me is this game's: sent");
    /* towards another player's stand-in: swapped to S -> me, that player's side: put back */
    { int o = 0, t = 0; WrittenPair(kNoLongerEnemies, true, 1, 3, &o, &t); if (o != 3 || t != 1) Fail("towards a stand-in: S -> me written"); }
    if (PairRoad(other, me) != kRoadPutBack) Fail("swap toward a stand-in -> put back (the other player's side)");
    /* the roads as before for unswapped pairs */
    if (PairRoad(me, npc) != kRoadSend) Fail("my own table: sent");
    if (PairRoad(me, other) != kRoadSend) Fail("mine -> another player: mine, sent");
    if (PairRoad(npc, me) != kRoadSend) Fail("X -> me (the crime / fight road): sent");
    if (PairRoad(npc, other) != kRoadPutBack) Fail("X -> another player: that player's, put back");
    if (PairRoad(other, npc) != kRoadPutBack) Fail("another player's table: put back");
    if (PairRoad(npc, npc2) != kRoadWorldTable) Fail("world-vs-world: the world server's table");
    Side carried = { false, false, false };   /* a coop-p<n> record naming this game's own slot, or unreadable: not a world faction */
    if (PairRoad(npc, carried) != kRoadPutBack || PairRoad(carried, npc) != kRoadPutBack) Fail("a faction neither world nor a player's: no owner here, put back");
    if (!OwnsPair(me, other) || OwnsPair(other, me) || !OwnsPair(npc, me) || OwnsPair(npc, other)) Fail("OwnsPair: one owner per direction");
    /* the moment */
    if (ForwardMoment(true, true, false) != kMomentEcho || ForwardMoment(true, false, true) != kMomentEcho) Fail("the mod's own write: no echo");
    if (ForwardMoment(false, true, false) != kMomentQueue) Fail("a worker thread: queued");
    if (ForwardMoment(false, true, true) != kMomentNow) Fail("the main thread: now");
    if (ForwardMoment(false, false, false) != kMomentNow) Fail("before the main thread is known: now");
}

/* T-573: src/common/copyjudge.h - the judge held while this game's simulation is stopped, and the bounded re-arm. */
void t_t573_simulation_held()
{
    const float muls[] = { 0.0f, 1.0f, 5.0f };
    for (int ok = 0; ok < 2; ++ok)
        for (int pz = 0; pz < 2; ++pz)
            for (int m = 0; m < 3; ++m)
                for (int fin = 0; fin < 2; ++fin)
                {
                    const bool want = ok != 0 && fin != 0 && (pz != 0 || muls[m] == 0.0f);
                    if (copyjudge::SimulationHeld(ok != 0, pz != 0, muls[m], fin != 0) != want)
                        Fail("SimulationHeld ok=" + I(ok) + " paused=" + I(pz) + " mul#" + I(m) + " finite=" + I(fin));
                }
}

void t_t573_hold_shift()
{
    long long from = -1;
    if (copyjudge::HoldShift(false, 50, &from) != 0 || from != -1) Fail("running, never held: no shift, no mark");
    if (copyjudge::HoldShift(true, 100, &from) != 0 || from != 100) Fail("the first held frame is marked");
    if (copyjudge::HoldShift(true, 101, &from) != 0 || from != 100) Fail("a later held frame keeps the first mark");
    if (copyjudge::HoldShift(true, 104, &from) != 0 || from != 100) Fail("still held: no shift yet");
    if (copyjudge::HoldShift(false, 105, &from) != 5 || from != -1) Fail("the first running frame returns the held span and clears");
    if (copyjudge::HoldShift(false, 106, &from) != 0 || from != -1) Fail("the shift is paid once");
    from = 200;
    if (copyjudge::HoldShift(false, 200, &from) != 0 || from != -1) Fail("a zero span shifts nothing and clears");
    from = 300;
    if (copyjudge::HoldShift(false, 250, &from) != 0 || from != -1) Fail("a span that went backwards shifts nothing and clears");
}

void t_t573_shift_clock()
{
    if (copyjudge::ShiftClock(0, 40) != 0) Fail("a clock not started stays 0");
    if (copyjudge::ShiftClock(1000, 0) != 1000) Fail("no span: unchanged");
    if (copyjudge::ShiftClock(1000, -5) != 1000) Fail("a negative span: unchanged");
    if (copyjudge::ShiftClock(1000, 40) != 1040) Fail("a started clock moves on by the span");
}

/* The reporter's pause, frame by frame: a copy that goes nowhere while held is never judged during the hold, and its
   first window after the hold is judged only once it has run kWin frames - the windows count running frames only. */
void t_t573_window_counts_running_frames()
{
    const long long kWin = 240;
    for (int heldLen = 0; heldLen <= 2000; heldLen += 250)
        for (int heldAt = 1; heldAt < 300; heldAt += 37)
        {
            long long clock = 1, from = -1, ran = 0;
            int rollsHeld = 0, rolls = 0;
            for (long long tick = 1; tick < 4000; ++tick)
            {
                const bool held = tick >= heldAt && tick < heldAt + heldLen;
                clock = copyjudge::ShiftClock(clock, copyjudge::HoldShift(held, tick, &from));
                if (held) continue;   /* DrivePuppet returns before the judge */
                ++ran;
                if (tick - clock >= kWin)
                {
                    ++rolls;
                    if (ran - (rolls - 1) * kWin < kWin) ++rollsHeld;
                    clock = tick;
                }
            }
            if (rollsHeld != 0) Fail("a window was judged before it had run its frames, held " + I(heldLen) + " at " + I(heldAt));
            if (rolls < 1) Fail("no window was judged at all, held " + I(heldLen));
        }
}

void t_t573_hold_shift_sec()
{
    double from = -1.0;
    if (copyjudge::HoldShiftSec(false, 5.0, &from) != 0.0 || from != -1.0) Fail("running, never held: no shift, no mark");
    if (copyjudge::HoldShiftSec(true, 10.0, &from) != 0.0 || from != 10.0) Fail("the first held second is marked");
    if (copyjudge::HoldShiftSec(true, 12.0, &from) != 0.0 || from != 10.0) Fail("a later held frame keeps the mark");
    if (copyjudge::HoldShiftSec(false, 14.5, &from) != 4.5 || from != -1.0) Fail("the first running frame returns the held seconds and clears");
    if (copyjudge::HoldShiftSec(false, 15.0, &from) != 0.0) Fail("the shift is paid once");
    from = 20.0;
    if (copyjudge::HoldShiftSec(false, 19.0, &from) != 0.0 || from != -1.0) Fail("a span that went backwards shifts nothing and clears");
}

void t_t573_shift_since()
{
    if (copyjudge::ShiftSince(0.0, 4.0) != 0.0) Fail("a clock not set stays 0");
    if (copyjudge::ShiftSince(7.0, 0.0) != 7.0) Fail("no span: unchanged");
    if (copyjudge::ShiftSince(7.0, -1.0) != 7.0) Fail("a negative span: unchanged");
    if (copyjudge::ShiftSince(7.0, 4.0) != 11.0) Fail("a set clock moves on by the span");
    /* The review's case: a route issued at 10.0 with a 2 s pending grace, the game stopped 10.5 - 14.5. Shifted, the
       first running frame is 0.5 s into the grace, not 4.5 s past it; an "until" already past stays past. */
    double from = -1.0;
    copyjudge::HoldShiftSec(true, 10.5, &from);
    const double ss = copyjudge::HoldShiftSec(false, 14.5, &from);
    const double issueAt = copyjudge::ShiftSince(10.0, ss);
    if (!(14.5 - issueAt < 2.0) || 14.5 - issueAt != 0.5) Fail("the grace counts running time only");
    const double holdUntil = copyjudge::ShiftSince(9.0, ss);
    if (!(14.5 >= holdUntil)) Fail("an expired hold stays expired");
    const double liveHold = copyjudge::ShiftSince(11.0, ss);
    if (liveHold - 14.5 != 0.5) Fail("a live hold keeps its remaining 0.5 s");
}

void t_t573_rearm_schedule()
{
    if (copyjudge::kMaxRearms != 3) Fail("three re-arms per copy");
    const int want[] = { -1, 15, 60, 240, -1, -1, -1 };
    for (int r = -1; r <= 5; ++r)
    {
        if (copyjudge::RearmWaitWindows(r) != want[r + 1]) Fail("RearmWaitWindows " + I(r));
        if (copyjudge::RearmLeft(r) != (want[r + 1] >= 0)) Fail("RearmLeft " + I(r));
        for (int w = 0; w <= 300; ++w)
            if (copyjudge::RearmDue(r, w) != (want[r + 1] >= 0 && w >= want[r + 1])) Fail("RearmDue r=" + I(r) + " w=" + I(w));
    }
    for (int r = 1; r < copyjudge::kMaxRearms; ++r)
        if (copyjudge::RearmWaitWindows(r) <= copyjudge::RearmWaitWindows(r - 1)) Fail("each wait is longer than the one before");
}

/* THE BOUND (F189): a copy that gives up on every chance it gets is re-armed at most kMaxRearms times however long it
   lives, and after the last give-up it is never re-armed again. */
void t_t573_rearm_bound()
{
    int rearms = 0, windows = 0, giveUps = 1, lifted = 0;   /* given up at window 0 */
    bool gaveUp = true;
    for (int win = 1; win <= 100000; ++win)
    {
        if (!gaveUp) { gaveUp = true; ++giveUps; windows = 0; continue; }   /* gives up again at once */
        ++windows;
        if (copyjudge::RearmDue(rearms, windows)) { ++rearms; ++lifted; gaveUp = false; windows = 0; }
    }
    if (lifted != copyjudge::kMaxRearms) Fail("re-armed " + I(lifted) + " times, expected " + I(copyjudge::kMaxRearms));
    if (giveUps != copyjudge::kMaxRearms + 1) Fail("gave up " + I(giveUps) + " times, expected " + I(copyjudge::kMaxRearms + 1));
    if (!gaveUp || copyjudge::RearmLeft(rearms)) Fail("the last give-up stands");
}

/* T-986 floorplace: copyjudge::GiveUpLift - a give-up ends when the owner's point has moved liftDist (20) from where it was at
   the give-up, before and without spending a timed re-arm; the timed re-arms are unchanged for a standing owner. */
void t_t986_giveup_lift_rule()
{
    const float L = 20.0f;
    if (copyjudge::GiveUpLift(0, 1, true, 19.9f * 19.9f, L) != copyjudge::kLiftNone) Fail("T-986 floorplace: under 20 units, not lifted");
    if (copyjudge::GiveUpLift(0, 1, true, 20.0f * 20.0f, L) != copyjudge::kLiftOwnerMoved) Fail("T-986 floorplace: 20 units lifts");
    if (copyjudge::GiveUpLift(0, 1, true, 300.0f * 300.0f, L) != copyjudge::kLiftOwnerMoved) Fail("T-986 floorplace: 300 units lifts");
    if (copyjudge::GiveUpLift(0, 1, false, 300.0f * 300.0f, L) != copyjudge::kLiftNone) Fail("T-986 floorplace: no stored point, no move lift");
    if (copyjudge::GiveUpLift(0, 15, false, 300.0f * 300.0f, L) != copyjudge::kLiftTimer) Fail("T-986 floorplace: no stored point, the timer still lifts");
    if (copyjudge::GiveUpLift(0, 15, true, 400.0f, L) != copyjudge::kLiftOwnerMoved) Fail("T-986 floorplace: both due, the move lifts (no re-arm spent)");
    if (copyjudge::GiveUpLift(0, 15, true, 0.0f, L) != copyjudge::kLiftTimer) Fail("T-986 floorplace: a standing owner, the timer lifts");
    if (copyjudge::GiveUpLift(3, 100000, true, 0.0f, L) != copyjudge::kLiftNone) Fail("T-986 floorplace: no re-arm left, standing owner: stands");
    if (copyjudge::GiveUpLift(3, 1, true, 400.0f, L) != copyjudge::kLiftOwnerMoved) Fail("T-986 floorplace: no re-arm left, a moved owner still lifts");
    if (copyjudge::GiveUpLift(0, 1, true, 400.0f, 0.0f) != copyjudge::kLiftNone) Fail("T-986 floorplace: a zero lift distance never lifts by move");
    volatile float zero = 0.0f;
    const float fNan = zero / zero, fInf = 1.0f / zero;
    if (copyjudge::GiveUpLift(0, 1, true, fNan, L) != copyjudge::kLiftNone) Fail("T-986 floorplace: a NaN distance does not lift");
    if (copyjudge::GiveUpLift(0, 1, true, fInf, L) != copyjudge::kLiftNone) Fail("T-986 floorplace: an infinite distance does not lift");
    if (copyjudge::GiveUpLift(0, 15, true, fNan, L) != copyjudge::kLiftTimer) Fail("T-986 floorplace: a NaN distance leaves the timer");
    for (int r = -1; r <= 5; ++r)
        for (int w = 0; w <= 300; ++w)
            if ((copyjudge::GiveUpLift(r, w, true, 0.0f, L) == copyjudge::kLiftTimer) != copyjudge::RearmDue(r, w))
                Fail("T-986 floorplace: a standing owner's lifts are the timed re-arms r=" + I(r) + " w=" + I(w));
}

/* T-986 floorplace fold: a copy whose EVERY placement fails (T1431's sideways landing: a strike per placement, a give-up
   after 3), judged every 2 s window, its owner moving. mode 0 = pacing 20 units, 2 = pacing 30 units (5 windows each way),
   1 = walking straight at 10 units a window. Placements obey the 5-in-60 s ring replicate.cpp keeps (snapTimes); lifts are
   GiveUpLift with MoveLiftDist (no placement ever holds, so moveLifts is never zeroed). */
struct T986Sim { int placements; int moveLifts; int timerLifts; bool rateOk; };
T986Sim t986_sim_moving_owner(int mode, int windows)
{
    T986Sim r; r.placements = 0; r.moveLifts = 0; r.timerLifts = 0; r.rateOk = true;
    double ring[5]; for (int i = 0; i < 5; ++i) ring[i] = -1.0e9;
    int next = 0;
    double hist[6]; int hn = 0;
    bool gaveUp = false; int strikes = 0, rearms = 0, gaveUpWindows = 0, moveLifts = 0;
    float sx = 0.0f;
    for (int w = 0; w < windows; ++w)
    {
        const double now = w * 2.0;
        const float ox = mode == 1 ? 10.0f * (float)w : (((w / 5) % 2) != 0 ? (mode == 0 ? 20.0f : 30.0f) : 0.0f);
        if (gaveUp)
        {
            ++gaveUpWindows;
            const float d = ox - sx;
            const int l = copyjudge::GiveUpLift(rearms, gaveUpWindows, true, d * d, copyjudge::MoveLiftDist(moveLifts, 20.0f));
            if (l == copyjudge::kLiftOwnerMoved) { ++moveLifts; ++r.moveLifts; gaveUp = false; strikes = 0; }
            else if (l == copyjudge::kLiftTimer) { ++rearms; ++r.timerLifts; gaveUp = false; strikes = 0; }
            continue;
        }
        if (now - ring[next] < 60.0) continue;   /* the rate ring: the 5th placement back is under 60 s old */
        ring[next] = now; next = (next + 1) % 5;
        hist[hn % 6] = now;
        if (hn >= 5 && now - hist[(hn - 5) % 6] < 60.0) r.rateOk = false;
        ++hn;
        ++r.placements;
        if (++strikes >= 3) { gaveUp = true; gaveUpWindows = 0; sx = ox; }
    }
    return r;
}

void t_t986_giveup_lift_moving_owner()
{
    const float want[] = { 20.0f, 20.0f, 40.0f, 80.0f, 160.0f, 160.0f, 160.0f };
    for (int n = -1; n <= 5; ++n)
        if (copyjudge::MoveLiftDist(n, 20.0f) != want[n + 1]) Fail("T-986 floorplace fold: MoveLiftDist " + I(n));
    if (copyjudge::GiveUpLift(3, 1, true, 300.0f * 300.0f, copyjudge::MoveLiftDist(50, 20.0f)) != copyjudge::kLiftOwnerMoved)
        Fail("T-986 floorplace fold: a 300-unit move lifts however many lifts failed before (T1431)");
    const int W = 100000;   /* ~55 hours of 2 s windows */
    for (int mode = 0; mode < 3; ++mode)
    {
        const T986Sim r = t986_sim_moving_owner(mode, W);
        if (!r.rateOk) Fail("T-986 floorplace fold: more than 5 placements in 60 s, mode " + I(mode));
        if (r.timerLifts > copyjudge::kMaxRearms) Fail("T-986 floorplace fold: timed re-arms over the bound, mode " + I(mode));
        if (mode != 1)
        {
            /* a pacing owner: the doubling outgrows the pacing - the lifts die out and the placements stop */
            if (r.moveLifts < 1 || r.moveLifts > 2) Fail("T-986 floorplace fold: pacing owner lifts " + I(r.moveLifts) + ", mode " + I(mode));
            if (r.placements > 3 * (1 + copyjudge::kMaxRearms + 2)) Fail("T-986 floorplace fold: pacing owner placements " + I(r.placements));
            const T986Sim r2 = t986_sim_moving_owner(mode, W / 2);
            if (r2.placements != r.placements || r2.moveLifts != r.moveLifts) Fail("T-986 floorplace fold: the lifts did not die out, mode " + I(mode));
        }
        else
        {
            /* a walking owner: at most one owner-moved lift per 160 units walked once the distance is capped */
            if (r.moveLifts > (int)(10.0 * W / 160.0) + 4) Fail("T-986 floorplace fold: walking owner lifts " + I(r.moveLifts));
            if (r.placements > 3 * (r.moveLifts + r.timerLifts + 1)) Fail("T-986 floorplace fold: walking owner placements " + I(r.placements));
        }
    }
}

/* T-986 g: floorplace::FloorJudgeDue - T-986 g3: a floor placement is judged 'ok' once its HEIGHT is within the gap
   (at least 2 frames on) and 'again' only once 1.5 s has passed; x/z movement never decides. */
void t_t986g_floor_judge_due()
{
    using namespace floorplace;
    /* T1434 uid 19: x/z moved 25.9 at once, the height not - at 0.08 s (5 frames) it waits (was judged 'again') */
    if (FloorJudgeDue(5, 0.08, true) != kFloorJudgeWait) Fail("T-986 g3: x/z landed, height not, 0.08 s: wait");
    if (FloorJudgeDue(0, 0.0, false) != kFloorJudgeWait || FloorJudgeDue(1, 0.01, false) != kFloorJudgeWait) Fail("T-986 g3: under 2 frames waits");
    if (FloorJudgeDue(2, 0.03, false) != kFloorJudgeLanded) Fail("T-986 g3: height within the gap at 2 frames: ok");
    if (FloorJudgeDue(60, 0.95, false) != kFloorJudgeLanded) Fail("T-986 g3: height landed at ~1 s: ok");
    if (FloorJudgeDue(90, 1.49, true) != kFloorJudgeWait) Fail("T-986 g3: still apart under 1.5 s: wait, never 'again'");
    if (FloorJudgeDue(90, 1.5, true) != kFloorJudgeTimedOut) Fail("T-986 g3: still apart at 1.5 s: judged ('again')");
    if (FloorJudgeDue(90, 1.5, false) != kFloorJudgeLanded) Fail("T-986 g3: within the gap at the bound: ok");
    /* reached late (a road skipped the step for 71 s - T1434 uid 17): judged on the height it has then */
    if (FloorJudgeDue(8000, 71.3, false) != kFloorJudgeLanded) Fail("T-986 g3: late, landed: ok");
    if (FloorJudgeDue(8000, 71.3, true) != kFloorJudgeTimedOut) Fail("T-986 g3: late, apart: judged ('again')");
    volatile float zero = 0.0f;
    const double dNan = (double)(zero / zero);
    if (FloorJudgeDue(5, dNan, true) != kFloorJudgeTimedOut) Fail("T-986 g3: an unreadable clock judges now");
    /* never 'again' before the bound, whatever the frames */
    for (int f = 0; f <= 200; ++f)
        if (f * 0.007 < 1.5 && FloorJudgeDue(f, f * 0.007, true) != kFloorJudgeWait) Fail("T-986 g3: 'again' before the bound at frame " + I(f));
    /* T1431 / T1434 shape: ~16 ms frames, the height still apart until it lands at ~0.9 s - judged 'ok' on that frame, never before */
    int judgedAt = -1, how = -1;
    for (int f = 0; f <= 200 && judgedAt < 0; ++f)
    {
        const double sec = f * 0.016;
        const int d = FloorJudgeDue(f, sec, sec < 0.9);
        if (d != kFloorJudgeWait) { judgedAt = f; how = d; }
    }
    if (judgedAt < 0 || how != kFloorJudgeLanded || judgedAt * 0.016 < 0.9 || (judgedAt - 1) * 0.016 >= 0.9)
        Fail("T-986 g3: judged at frame " + I(judgedAt) + ", not 'ok' on the landing");
}

/* T-986 g2: floorplace::FloorPlaceCopy - a follow copy whose owner stands still without a stop message is placed on its
   owner's floor; an owner moving is never placed under; non-follow copies and held stops decide as before. */
void t_t986g2_floor_place_copy()
{
    using namespace floorplace;
    /* follow, no stop, owner still, arrived under it, a floor apart: placed (T1433's teleported owner) */
    if (FloorPlaceCopy(true, false, false, true, false, 0, 0.0f, 66.0f, 0) != kFloorPlace) Fail("T-986 g2: follow, no stop, owner still, apart: place");
    if (FloorPlaceCopy(true, false, false, true, false, 0, 0.0f, -66.0f, 0) != kFloorPlace) Fail("T-986 g2: owner downstairs: place");
    /* owner moving: never */
    if (FloorPlaceCopy(true, false, true, true, false, 0, 0.0f, 66.0f, 0) != kFloorNone) Fail("T-986 g2: owner moving, arrived: none");
    if (FloorPlaceCopy(true, false, true, false, false, 3, 10.0f, 66.0f, 0) != kFloorNone) Fail("T-986 g2: owner moving, stalled: none");
    /* same height: never */
    if (FloorPlaceCopy(true, false, false, true, false, 0, 0.0f, 5.0f, 0) != kFloorNone) Fail("T-986 g2: same floor: none");
    /* not arrived: the drive's stall counts, within kFloorReach only; walking (no stall) is the drive's */
    if (FloorPlaceCopy(true, false, false, false, false, 1, 30.0f, 66.0f, 0) != kFloorPlace) Fail("T-986 g2: drive-stalled within reach: place");
    if (FloorPlaceCopy(true, false, false, false, false, 0, 30.0f, 66.0f, 0) != kFloorNone) Fail("T-986 g2: walking, not stalled: none");
    if (FloorPlaceCopy(true, false, false, false, false, 2, 60.0f, 66.0f, 0) != kFloorNone) Fail("T-986 g2: stalled beyond reach: none");
    /* the stop watch's stall counts only with a held stop */
    if (FloorPlaceCopy(true, false, false, false, true, 0, 30.0f, 66.0f, 0) != kFloorNone) Fail("T-986 g2: stop watch without a stop: none");
    /* strikes still give up */
    if (FloorPlaceCopy(true, false, false, true, false, 0, 0.0f, 66.0f, kFloorStrikes) != kFloorGaveUp) Fail("T-986 g2: strikes give up");
    /* the held-stop road and the non-follow road decide exactly as FloorPlace did before */
    for (int sh = 0; sh < 2; ++sh) for (int mv = 0; mv < 2; ++mv) for (int ar = 0; ar < 2; ++ar) for (int ss = 0; ss < 2; ++ss)
    {
        const float flat = 30.0f, dY = 66.0f;
        if (sh != 0)
        {
            const int was = FloorPlace(true, true, ar != 0, ss != 0, flat, dY, 0);
            if (FloorPlaceCopy(true, true, mv != 0, ar != 0, ss != 0, 0, flat, dY, 0) != was) Fail("T-986 g2: a held stop decides as before");
        }
        const int wasNf = FloorPlace(mv == 0, sh != 0, ar != 0, false, flat, dY, 0);
        if (FloorPlaceCopy(false, sh != 0, mv != 0, ar != 0, ss != 0, 3, flat, dY, 0) != wasNf) Fail("T-986 g2: a non-follow copy decides as before");
    }
}

/* T-986 s: copyjudge::PlaceHold - a placed copy's drive is held until its flat position moved or 1 s passed. */
void t_t986s_place_hold()
{
    using namespace copyjudge;
    /* T1436's mirror case: ~16 ms frames, the copy still at its pre-snap spot until f13, then 2.7 flat on - held to f13, released there */
    int releasedAt = -1, how = -1;
    for (int f = 0; f <= 100 && releasedAt < 0; ++f)
    {
        const double sec = f * 0.016;
        const float moved = f < 13 ? 0.0f : 2.7f;
        const int h = PlaceHold(true, moved * moved, sec);
        if (h != kPlaceHeld) { releasedAt = f; how = h; }
    }
    if (releasedAt != 13 || how != kPlaceLanded) Fail("T-986 s: the mirror case is held until f13 and released as landed, got f" + I(releasedAt));
    if (PlaceHold(true, 0.4f * 0.4f, 0.1) != kPlaceHeld) Fail("T-986 s: moved 0.4 is not shown");
    if (PlaceHold(true, 0.6f * 0.6f, 0.1) != kPlaceLanded) Fail("T-986 s: moved 0.6 is shown");
    if (PlaceHold(true, 0.0f, 0.99) != kPlaceHeld) Fail("T-986 s: not moved under 1 s: held");
    if (PlaceHold(true, 0.0f, 1.0) != kPlaceTimedOut) Fail("T-986 s: not moved at 1 s: released (the bound)");
    if (PlaceHold(false, 100.0f, 0.5) != kPlaceHeld) Fail("T-986 s: an unreadable position is not a move");
    if (PlaceHold(false, 100.0f, 1.0) != kPlaceTimedOut) Fail("T-986 s: an unreadable position is released at the bound");
    volatile float zero = 0.0f;
    const float fNan = zero / zero, fInf = 1.0f / zero;
    if (PlaceHold(true, fNan, 0.5) != kPlaceHeld || PlaceHold(true, fInf, 0.5) != kPlaceHeld) Fail("T-986 s: a distance that is not a number is not a move");
    if (PlaceHold(true, 0.0f, (double)fNan) != kPlaceTimedOut) Fail("T-986 s: an unreadable clock releases now");
    for (int f = 0; f < 62; ++f)
        if (PlaceHold(true, 0.0f, f * 0.016) != kPlaceHeld) Fail("T-986 s: released early at f" + I(f));
}

/* T-986 floorplace: THE BOUND for a standing owner is unchanged (t573_rearm_bound through GiveUpLift). */
void t_t986_giveup_lift_standing_bound()
{
    int rearms = 0, windows = 0, giveUps = 1, lifted = 0;
    bool gaveUp = true;
    for (int win = 1; win <= 100000; ++win)
    {
        if (!gaveUp) { gaveUp = true; ++giveUps; windows = 0; continue; }
        ++windows;
        const int l = copyjudge::GiveUpLift(rearms, windows, true, 0.0f, 20.0f);
        if (l == copyjudge::kLiftOwnerMoved) Fail("T-986 floorplace: a standing owner lifted by move");
        if (l == copyjudge::kLiftTimer) { ++rearms; ++lifted; gaveUp = false; windows = 0; }
    }
    if (lifted != copyjudge::kMaxRearms || giveUps != copyjudge::kMaxRearms + 1 || !gaveUp) Fail("T-986 floorplace: the standing bound changed");
}

/* T-580: decision 34 - a town is held back only by notes this game is about to place (an area loaded here, or in or beside the town) */
void t_t580_note_role()
{
    for (int t = 0; t < 2; ++t) for (int d = 0; d < 2; ++d) for (int p = 0; p < 2; ++p) for (int q = 0; q < 2; ++q)
    {
        const int r = townpending::NoteRole(t, d, p, q);
        const int want = t == 0 ? townpending::kNoteNoTown : d != 0 ? townpending::kNoteDeleted : p != 0 ? townpending::kNotePlaced : q == 0 ? townpending::kNoteNoPos : townpending::kNoteListed;
        if (r != want) Fail("NoteRole(" + I(t) + "," + I(d) + "," + I(p) + "," + I(q) + ") = " + I(r) + ", expected " + I(want));
    }
}
void t_t580_holds()
{
    if (townpending::Holds(10, 10, 1, 40, 40) != 1) Fail("a note in an area loaded here holds the town, wherever the town is");
    if (townpending::Holds(10, 10, 0, 40, 40) != 0) Fail("a note sleeping in an area not loaded here, far from the town, does not hold it");
    if (townpending::Holds(40, 40, 0, 40, 40) != 1) Fail("a note in the town's own area holds it (that area is loading)");
    if (townpending::Holds(41, 39, 0, 40, 40) != 1) Fail("a note beside the town (ring 1) holds it");
    if (townpending::Holds(42, 40, 0, 40, 40) != 0) Fail("a note two areas off, not loaded here, does not hold it");
    if (townpending::Holds(40, 42, 0, 40, 40) != 0) Fail("two areas off on the other axis does not hold it");
    if (townpending::Holds(40, 40, 0, -1, -1) != 1) Fail("an unread asked area holds the town (the old, safe answer)");
    if (townpending::Holds(-1, -1, 0, 40, 40) != 1) Fail("a note with no position here holds the town");
    if (townpending::Holds(-1, -1, 0, -1, -1) != 1) Fail("no position and no asked area: held");
    if (townpending::Holds(40, 40, 1, -1, -1) != 1) Fail("an unread town area and a note loaded here: held");
    if (townpending::Holds(0, 0, 0, 1, 1) != 1 || townpending::Holds(63, 63, 0, 62, 62) != 1) Fail("ring 1 at the map edges");
    /* T-985 C2b fold 2 (re-check LOW 2): a group row the load left for the feed holds a town only from the asked area or its ring 1 */
    if (townpending::HoldsNear(10, 10, 40, 40) != 0) Fail("a skipped group far from the town holds it");
    if (townpending::HoldsNear(40, 40, 40, 40) != 1 || townpending::HoldsNear(41, 39, 40, 40) != 1) Fail("a skipped group in or beside the town's area does not hold it");
    if (townpending::HoldsNear(42, 40, 40, 40) != 0 || townpending::HoldsNear(40, 38, 40, 40) != 0) Fail("a skipped group two areas off holds the town");
    if (townpending::HoldsNear(10, 10, -1, -1) != 1) Fail("an unread asked area does not hold (the safe answer)");
    if (townpending::HoldsNear(-1, -1, 40, 40) != 1) Fail("a skipped group with no position does not hold");
    /* T-985 C2b fold 3 (re-check LOW, thread race): the load's town wait's 60 s bound on GetTickCount's wrapping tick */
    if (townpending::BoundPassed(1000u, 60999u, 60000u) != 0 || townpending::BoundPassed(1000u, 61000u, 60000u) != 1) Fail("fold 3: the town wait's bound is not 60 s");
    if (townpending::BoundPassed(0xFFFFFF00u, 59000u, 60000u) != 0 || townpending::BoundPassed(0xFFFFFF00u, 60000u, 60000u) != 1) Fail("fold 3: the town wait's bound is wrong across the tick's wrap");
}
void t_t580_pending_combinations()
{
    /* every combination: deleted / placed here / loaded here / beside the town, with and without a town and a position */
    for (int t = 0; t < 2; ++t) for (int d = 0; d < 2; ++d) for (int p = 0; p < 2; ++p) for (int q = 0; q < 2; ++q) for (int l = 0; l < 2; ++l) for (int b = 0; b < 2; ++b)
    {
        const int rx = b != 0 ? 21 : 5, ry = b != 0 ? 19 : 5;   /* beside the town at 20,20, or 15 areas away */
        const int got = townpending::Pending(t, d, p, q, rx, ry, l, 20, 20);
        const int want = (t != 0 && d == 0 && p == 0 && (q == 0 || l != 0 || b != 0)) ? 1 : 0;   /* no position here holds, as before T-580 */
        if (got != want) Fail("Pending(town=" + I(t) + " deleted=" + I(d) + " placed=" + I(p) + " pos=" + I(q) + " loaded=" + I(l) + " beside=" + I(b) + ") = " + I(got) + ", expected " + I(want));
    }
    if (townpending::Pending(1, 1, 0, 1, 20, 20, 1, 20, 20) != 0) Fail("a deleted group never holds its town, even in a loaded area");
    if (townpending::Pending(1, 0, 1, 1, 20, 20, 1, 20, 20) != 0) Fail("a group placed here never holds its town (the engine counts it)");
    if (townpending::Pending(1, 0, 0, 1, 5, 5, 0, 20, 20) != 0) Fail("a group asleep where this game does not load never holds its town");
    if (townpending::Pending(1, 0, 0, 1, 5, 5, 1, 20, 20) != 1) Fail("a group not placed yet in a loaded area holds its town (no duplicates)");
    if (townpending::Pending(1, 0, 0, 0, 5, 5, 0, 20, 20) != 1) Fail("a group with no position here holds its town (cannot be shown elsewhere)");
    if (townpending::Pending(1, 0, 0, 1, 5, 5, 0, -1, -1) != 1) Fail("an unread town area: a far group holds (the old answer)");
    if (townpending::Pending(1, 0, 1, 0, 5, 5, 0, 20, 20) != 0) Fail("placed here never holds, position or not");
}
void t_t580_rerun_keep()
{
    if (townpending::RerunKeep(0, 1) != 1) Fail("nothing made, a notebook-pending refusal: the entry stays");
    if (townpending::RerunKeep(1, 1) != 0) Fail("a squad made: the entry ends");
    if (townpending::RerunKeep(2, 0) != 0) Fail("squads made, nothing refused: the entry ends");
    if (townpending::RerunKeep(0, 0) != 0) Fail("nothing made, nothing refused (the engine chose none): the entry ends");
}
void t_t580_ask_area()
{
    int ax = 0, ay = 0;
    townpending::AskArea(1, 10, 11, 1, 20, 21, 1, 30, 31, &ax, &ay); if (ax != 10 || ay != 11) Fail("the town's area first");
    townpending::AskArea(0, 10, 11, 1, 20, 21, 1, 30, 31, &ax, &ay); if (ax != 20 || ay != 21) Fail("then the building context's area (the re-offer's fallback)");
    townpending::AskArea(0, 10, 11, 0, 20, 21, 1, 30, 31, &ax, &ay); if (ax != 30 || ay != 31) Fail("then the spawn position");
    townpending::AskArea(0, 10, 11, 0, 20, 21, 0, 30, 31, &ax, &ay); if (ax != -1 || ay != -1) Fail("nothing reads: -1 (held)");
    if (townpending::InRun(0, 0) != 0 || townpending::InRun(0, 7) != 0) Fail("no re-run raised: nothing counts");
    if (townpending::InRun(7, 7) != 1) Fail("the re-run's own thread counts");
    if (townpending::InRun(7, 8) != 0) Fail("another thread's building fill never counts for the re-run");
}
void t_t580_kept_bound()
{
    int reruns = 0; bool gaveUp = false;
    for (int kept = 1; kept <= 100 && !gaveUp; ++kept) { ++reruns; if (townpending::KeptGiveUp(kept)) gaveUp = true; }
    if (!gaveUp || reruns != townpending::kMaxKeptReruns) Fail("kept re-runs end at " + I(reruns) + ", expected " + I(townpending::kMaxKeptReruns));
    if (townpending::KeptGiveUp(townpending::kMaxKeptReruns - 1) != 0) Fail("below the bound the entry waits");
}
void t_t1099_creation_ask()
{
    if (townpending::CreationAsk(1, 1) != townpending::kAskBuilding) Fail("a residents re-run naming its building: the building's own question");
    if (townpending::CreationAsk(1, 0) != townpending::kAskTown) Fail("a re-run naming no building (the town refill's): the town's question");
    if (townpending::CreationAsk(0, 1) != townpending::kAskTown) Fail("another thread while a re-run names a building: the town's question");
    if (townpending::CreationAsk(0, 0) != townpending::kAskTown) Fail("an ordinary creation: the town's question");
    if (townpending::CreationAsk(townpending::InRun(7, 8), 1) != townpending::kAskTown) Fail("InRun false off the re-run's thread: the town's question");
    if (townpending::CreationAsk(townpending::InRun(7, 7), 1) != townpending::kAskBuilding) Fail("InRun true on the re-run's thread: the building's question");
    /* the report's case: a note homed at another building (area 7,44) holds the town but not this building (6,45) */
    const int bldHeld = townpending::NoteHoldsBuilding(0, "OTHER-KEY", "THIS-KEY", 1, 1, 1, 1, 0.0f, 0.0f, 30.0f);
    if (bldHeld != 0) Fail("a note homed at another building held this building");
}
void t_t1099_park_stays()
{
    const std::string held("Spiders_21 in area 7,44 (in or beside the town's area; homed here)");
    if (townpending::NoteIdOfHolder(held) != "Spiders_21") Fail("the holder text's note is its first word");
    if (townpending::NoteIdOfHolder("Spiders_21") != "Spiders_21") Fail("a holder text of one word is the note");
    if (townpending::NoteIdOfHolder("") != "") Fail("no holder text: no note");
    if (townpending::ParkStays(townpending::kTownHeld, "Spiders_21", held) != 1) Fail("the parking note still holds the building: it stays parked");
    if (townpending::ParkStays(townpending::kTownNotListed, "Spiders_21", "") != 0) Fail("the note gone (nothing listed): un-parked");
    if (townpending::ParkStays(townpending::kTownElsewhere, "Spiders_21", "") != 0) Fail("the note no longer holds the building (listed elsewhere): un-parked");
    if (townpending::ParkStays(townpending::kTownHeld, "Spiders_21", "Hivers_3 in area 6,45 (loaded here; homed here)") != 0) Fail("another note holds it now: un-parked (the decision waits on that one)");
    if (townpending::ParkStays(townpending::kTownHeld, "", held) != 1) Fail("a park naming no note: stays while held");
    if (townpending::ParkStays(townpending::kTownNotListed, "", "") != 0) Fail("a park naming no note: un-parked when nothing holds");
    /* the bound still counts kept re-runs before a park */
    if (townpending::KeptGiveUp(townpending::kMaxKeptReruns) != 1 || townpending::KeptGiveUp(townpending::kMaxKeptReruns - 1) != 0) Fail("the park comes at the kept bound");
}
void t_t580_town_answer()
{
    if (townpending::TownAnswer(0, 0) != townpending::kTownNotListed) Fail("no listed area = not listed");
    if (townpending::TownAnswer(3, 0) != townpending::kTownElsewhere) Fail("listed areas none of which hold = elsewhere");
    if (townpending::TownAnswer(3, 1) != townpending::kTownHeld) Fail("one holding area = held");
    if (townpending::TownAnswer(1, 1) != townpending::kTownHeld) Fail("the only area holds = held");
}
void t_t580_log_due()
{
    if (townpending::LogDue(0, 0u, 12345u) != 1) Fail("the first refusal is logged");
    if (townpending::LogDue(1, 1000u, 1000u + townpending::kRelogMs - 1u) != 0) Fail("inside 5 minutes: not logged again");
    if (townpending::LogDue(1, 1000u, 1000u + townpending::kRelogMs) != 1) Fail("at 5 minutes: logged again");
    if (townpending::LogDue(1, 0xFFFFFF00u, 0xFFFFFF00u + townpending::kRelogMs) != 1) Fail("across the 49-day wrap: logged again at 5 minutes");
    if (townpending::LogDue(1, 0xFFFFFF00u, 0x00000010u) != 0) Fail("across the wrap, a few ms later: not logged");
    /* a stuck town refused every 2 s for an hour logs 12 lines (the first and one per 5 minutes) */
    int logged = 0, seen = 0; unsigned last = 0;
    for (unsigned ms = 0; ms < 3600000u; ms += 2000u) { if (townpending::LogDue(seen, last, ms)) { ++logged; last = ms; } seen = 1; }
    if (logged != 12) Fail("an hour of refusals every 2 s logged " + I(logged) + " lines, expected 12");
}

/* decision 34: a note holds its town only while some game can place it - the game that runs its area (townpending::Placeable) */
void t_t587_placeable()
{
    using namespace townpending;
    /* the whole table: engine 1 / 0 / not asked; the area held by another game / not / unread; that game in the world / not /
       unread; a usable record here or not; a failed creation or not */
    for (int e = -1; e <= 1; ++e) for (int a = -1; a <= 1; ++a) for (int h = -1; h <= 1; ++h) for (int u = 0; u < 2; ++u) for (int f = 0; f < 2; ++f)
    {
        int want = kPlaceUnknown;
        if (e == 1) want = kPlaceYes;
        else if (a == 0) want = (e != 0) ? kPlaceUnknown : (u != 0 && f == 0) ? kPlaceByThisGame : kPlaceNoGame;
        else if (a == 1) want = (h == 1) ? kPlaceYes : (h == 0 && e == 0) ? kPlaceNoAbsent : kPlaceUnknown;
        const int got = Placeable(e, a, h, u, f);
        if (got != want) Fail("Placeable(engine=" + I(e) + " areaHeld=" + I(a) + " holderInWorld=" + I(h) + " usable=" + I(u) + " failed=" + I(f) + ") = " + I(got) + ", expected " + I(want));
        if (NoPlacer(got) != ((got == kPlaceNoGame || got == kPlaceNoAbsent) ? 1 : 0)) Fail("NoPlacer disagrees for " + I(got));
        const int need = (a == 0 || (a == 1 && h == 0)) ? 1 : 0;
        if (NeedsLookup(a, h) != need) Fail("NeedsLookup(areaHeld=" + I(a) + " holderInWorld=" + I(h) + ") != " + I(need));
        /* a lookup the rule does not need never changes whether the note holds */
        if (need == 0 && (NoPlacer(Placeable(-1, a, h, u, f)) != NoPlacer(Placeable(0, a, h, u, f)) || NoPlacer(Placeable(1, a, h, u, f)) != NoPlacer(Placeable(0, a, h, u, f))))
            Fail("an unneeded lookup changed whether the note holds (areaHeld=" + I(a) + " holderInWorld=" + I(h) + ")");
        if (got == kPlaceByThisGame && (a != 0 || e != 0)) Fail("this game places only notes of an area it runs, with the engine answering 0");
    }
    if (Placeable(0, 0, -1, 1, 0) != kPlaceByThisGame) Fail("an area this game runs, its record usable here: this game places it, whatever block numbered it");
    if (Placeable(0, 0, -1, 0, 0) != kPlaceNoGame) Fail("an area this game runs, no usable record here: placed by no game");
    if (Placeable(0, 0, -1, 1, 1) != kPlaceNoGame) Fail("its creation failed here: placed by no game (not tried again)");
    if (Placeable(-1, 0, -1, 1, 0) != kPlaceUnknown || Placeable(-1, 0, -1, 0, 0) != kPlaceUnknown) Fail("the engine not asked yet: it holds until the engine answers");
    if (Placeable(1, 0, -1, 0, 0) != kPlaceYes) Fail("a group this game's world holds asleep: placed when its area loads here");
    if (Placeable(0, 1, 1, 0, 0) != kPlaceYes) Fail("another game holds its area and is in the world: it places it there");
    if (Placeable(0, 1, 1, 1, 0) != kPlaceYes) Fail("an area another game holds is never placed by this game, even with a usable record here");
    if (Placeable(0, 1, 0, 1, 0) != kPlaceNoAbsent) Fail("its area held by a game not in the world, not held here: placed by no game");
    if (Placeable(0, 1, -1, 1, 0) != kPlaceUnknown) Fail("the area's holder cannot be named on the roster: it holds");
    if (Placeable(0, -1, -1, 1, 0) != kPlaceUnknown || Placeable(0, -1, 0, 0, 0) != kPlaceUnknown) Fail("an area that cannot be read is not this game's: it holds");
}
void t_t587_note_role_placer()
{
    using namespace townpending;
    for (int t = 0; t < 2; ++t) for (int d = 0; d < 2; ++d) for (int p = 0; p < 2; ++p) for (int q = 0; q < 2; ++q) for (int pl = kPlaceYes; pl <= kPlaceUnknown; ++pl)
    {
        const int base = NoteRole(t, d, p, q);
        const int want = ((base == kNoteNoPos || base == kNoteListed) && (pl == kPlaceNoGame || pl == kPlaceNoAbsent)) ? kNoteUnplaceable : base;
        if (NoteRole(t, d, p, q, pl) != want) Fail("NoteRole(" + I(t) + "," + I(d) + "," + I(p) + "," + I(q) + ", place " + I(pl) + ") != " + I(want));
    }
    if (NoteRole(1, 0, 0, 1, kPlaceYes) != kNoteListed) Fail("a placeable note with a position is listed");
    if (NoteRole(1, 0, 0, 1, kPlaceUnknown) != kNoteListed) Fail("a note that cannot be told yet is listed");
    if (NoteRole(1, 0, 0, 0, kPlaceNoGame) != kNoteUnplaceable) Fail("a position-less note no game can place is not listed");
    if (NoteRole(1, 0, 1, 1, kPlaceNoGame) != kNotePlaced) Fail("a note placed here stays placed");
    if (NoteRole(1, 1, 0, 1, kPlaceNoAbsent) != kNoteDeleted) Fail("a deleted note stays deleted");
    if (NoteRole(1, 0, 0, 1, kPlaceByThisGame) != kNoteListed) Fail("a note this game is about to place from its record stays listed (it holds until placed)");
}
void t_t587_town_people()
{
    using namespace townpending;
    /* A group listed under its town in the town's own area 14,23 (loaded here), never placed by any game, in an area this game
       runs - whatever block numbered it. With its record here this game places it, and it holds until then (the placed group is
       then counted by the engine, kNotePlaced); with no usable record or a failed creation it holds nothing. */
    const int withRecord = Placeable(0, 0, -1, 1, 0);
    if (withRecord != kPlaceByThisGame) Fail("the group of an area this game runs, its record here, is placed by this game");
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, withRecord) != 1) Fail("until it is placed it holds the town (no people made beside it)");
    if (Pending(1, 0, 1, 1, 14, 23, 1, 14, 23, withRecord) != 0) Fail("once placed (bound here) it holds nothing - the engine counts it");
    const int noRecord = Placeable(0, 0, -1, 0, 0);
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, noRecord) != 0) Fail("a note of an area this game runs with no usable record must not hold the town in its own loaded area");
    if (Pending(1, 0, 0, 1, 14, 23, 0, 14, 24, noRecord) != 0) Fail("nor beside the town");
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(0, 0, -1, 1, 1)) != 0) Fail("nor after its creation failed");
    /* its record here, its area this game's but not loaded yet: it holds only where Holds says (the town's own area being loaded) */
    if (Pending(1, 0, 0, 1, 14, 23, 0, 14, 23, withRecord) != 1) Fail("the town's own area loading: the note there holds it");
    if (Pending(1, 0, 0, 1, 5, 5, 0, 14, 23, withRecord) != 0) Fail("a note far from the town, in an area not loaded here, does not hold it");
    /* a group this game's world holds asleep in the town's area is about to be placed - it holds */
    const int asleep = Placeable(1, 0, -1, 0, 0);
    if (Pending(1, 0, 0, 1, 21, 17, 1, 21, 17, asleep) != 1) Fail("a group this game's world holds asleep in a loaded area holds its town (no duplicates)");
    if (Pending(1, 0, 0, 1, 14, 23, 0, 14, 24, asleep) != 1) Fail("a group this game's world holds beside the town holds it");
    if (Pending(1, 0, 0, 1, 5, 5, 0, 14, 23, asleep) != 0) Fail("far away it does not hold");
    /* a group in an area another game runs: holds while that game is in the world; stops when it leaves; holds again when it returns */
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(0, 1, 1, 1, 0)) != 1) Fail("a group in an area a present game runs holds the town");
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(0, 1, 0, 1, 0)) != 0) Fail("once that game is gone, and this world does not hold it, it does not");
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(0, 1, 1, 1, 0)) != 1) Fail("that game back in the world: it holds again (the state decides, not a time)");
    /* what cannot be told holds */
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(-1, 0, -1, 1, 0)) != 1) Fail("the engine not asked yet: holds");
    if (Pending(1, 0, 0, 1, 14, 23, 1, 14, 23, Placeable(0, -1, -1, 1, 0)) != 1) Fail("its area unreadable: holds");
    /* the placer form agrees with the form without it for every note that holds */
    for (int q = 0; q < 2; ++q) for (int l = 0; l < 2; ++l) for (int bx = 0; bx < 3; ++bx) for (int pl = kPlaceYes; pl <= kPlaceUnknown; ++pl)
    {
        if (NoPlacer(pl) != 0) continue;
        if (Pending(1, 0, 0, q, 13 + bx * 5, 23, l, 14, 23, pl) != Pending(1, 0, 0, q, 13 + bx * 5, 23, l, 14, 23)) Fail("a note some game can place answers as a listed note does (place " + I(pl) + ")");
    }
}
/* decision 34: this game creates a note from its record only with every answer read at that moment; the counter is raised for
   this game's own numbers only */
void t_t587_place_now()
{
    using namespace townpending;
    for (int p = kPlaceYes; p <= kPlaceUnknown; ++p) for (int e = -1; e <= 1; ++e) for (int a = -1; a <= 1; ++a) for (int l = -1; l <= 1; ++l)
    {
        const int want = (p == kPlaceByThisGame && e == 0 && a == 0 && l >= 0) ? 1 : 0;
        if (PlaceNow(p, e, a, l) != want) Fail("PlaceNow(place=" + I(p) + " fresh=" + I(e) + " areaHeld=" + I(a) + " loadedHere=" + I(l) + ") != " + I(want));
    }
    if (PlaceNow(kPlaceByThisGame, 1, 0, 1) != 0) Fail("the engine holds it when asked afresh: not created (no second copy)");
    if (PlaceNow(kPlaceByThisGame, 0, 1, 1) != 0) Fail("another game holds the area now: not created");
    if (PlaceNow(kPlaceByThisGame, 0, -1, 1) != 0) Fail("the area's holder cannot be read now: not created");
    if (PlaceNow(kPlaceByThisGame, 0, 0, 0) != 1) Fail("its area not loaded here: created asleep (it wakes when the area loads)");
    if (PlaceNow(kPlaceByThisGame, 0, 0, -1) != 0) Fail("whether its area is loaded here cannot be read: not created");
    if (RaiseAfterAttempt(6, 6) != 1) Fail("a number of this game's own block raises the counter");
    if (RaiseAfterAttempt(3, 6) != 0 || RaiseAfterAttempt(-1, 6) != 0 || RaiseAfterAttempt(6, -1) != 0) Fail("another block's number, an unnumbered id or an unknown block raises nothing");
}
/* decision 31(a) / 34: the per-world counter floor - one past this game's highest own-block record number per faction */
void t_t587_counter_floor()
{
    using namespace townpending;
    const unsigned nums[5] = { 393216u + 5u, 196608u + 900u, 393216u + 70u, 393216u + 12u, 7u };
    unsigned w = 0;
    for (int i = 0; i < 5; ++i) w = CounterFloor(nums[i], 6, w);
    if (w != 393216u + 71u) Fail("the floor is one past the highest own-block number, got " + I((int)w));
    if (CounterFloor(196608u + 900u, 6, 0u) != 0u) Fail("another block's number never raises the counter");
    if (CounterFloor(5u, -1, 0u) != 0u) Fail("no block known: no raise");
    int cap = -1;
    if (CounterFloor(65535u, 0, 0u, &cap) != 65535u || cap != 1) Fail("slot 0's highest number: the floor left the block or the cap was not reported (T-642 review 4)");
    if (CounterFloor(393216u + 65535u, 6, 0u, &cap) != 393216u + 65535u || cap != 1) Fail("slot 6's highest number: the floor reached the next block");
    if (CounterFloor(393216u + 70u, 6, 0u, &cap) != 393216u + 71u || cap != 0) Fail("a cap reported below the block's last number");
    if (CounterFloor(393216u + 3u, 6, 393216u + 50u) != 393216u + 50u) Fail("a lower number never lowers the floor");
    if (CounterFloor(458752u, 6, 0u) != 0u) Fail("the next block's first number is not this block's");
}

/* ---- src/common/factionkey.h: an NPC faction travels by its stringID, a player's faction by its chosen name ---- */
void t_t593_faction_key()
{
    using namespace factionkey;
    if (KeyOf(true, "12-gamedata.base", "Shinobi Thieves") != "12-gamedata.base") Fail("an NPC faction travels by its stringID, not its displayed name");
    if (KeyOf(true, "45676-Dialogue.mod", "Voleurs Shinobi") != "45676-Dialogue.mod") Fail("a mod's NPC faction travels by its stringID too");
    if (KeyOf(false, "204-gamedata.base", "My Crew") != "My Crew") Fail("a player's faction travels by its chosen name, never its record id");
    if (KeyOf(true, "", "Drifters") != "Drifters") Fail("an NPC faction whose stringID could not be read travels by its name");
    if (KeyOf(true, "@slot:1", "Drifters") != "Drifters") Fail("an id that looks like a slot name is never sent as an id");
    if (KeyOf(true, std::string(96, 'x'), "Drifters") != "Drifters") Fail("an over-long id is not sent as one");
    if (KeyOf(true, std::string(95, 'x'), "Drifters") != std::string(95, 'x')) Fail("an id of exactly kMaxSid bytes is sent");
    if (KeyOf(true, "12-gamedata\tbase", "N") != "N" || KeyOf(true, "12-gamedata\x7F", "N") != "N") Fail("a control byte in the id: sent by name");
    if (KeyOf(true, "7-Mod \xC3\xA9t\xC3\xA9.mod", "N") != "7-Mod \xC3\xA9t\xC3\xA9.mod") Fail("a mod file name with spaces or non-ASCII bytes is still an id");
}
void t_t593_faction_lookup()
{
    using namespace factionkey;
    if (!TryAsSid("12-gamedata.base")) Fail("a stringID key is tried as an id");
    if (!TryAsSid("Shinobi Thieves")) Fail("a name key is tried as an id too (no faction has that id; the name lookup follows)");
    if (TryAsSid("") || TryAsSid("@slot:2:Bob")) Fail("an empty key or a slot name is never tried as an id");
    if (!TakeIdMatch(true, false)) Fail("an NPC faction found by id is the answer");
    if (TakeIdMatch(true, true)) Fail("a player's faction is never matched by its record id - the name decides");
    if (TakeIdMatch(false, false) || TakeIdMatch(false, true)) Fail("nothing found by id: the name lookup decides");
}
void t_t593_faction_rekey()
{
    using namespace factionkey;
    if (!Rekey(true, "Shinobi Thieves", "12-gamedata.base")) Fail("a row naming an NPC faction by its displayed name takes the stringID");
    if (Rekey(true, "12-gamedata.base", "12-gamedata.base")) Fail("a row already keyed by the stringID is left");
    if (Rekey(false, "My Crew", "204-gamedata.base")) Fail("a player's faction row keeps its chosen name");
    if (Rekey(true, "Drifters", "") || Rekey(true, "Drifters", "@slot:1")) Fail("no usable stringID: the row keeps its key");
    if (Rekey(false, "Drifters", "Vagabonds")) Fail("a faction whose code could not be read (FactionKey gave its name): never re-keyed to a name");
}
void t_t594_zone_sector()
{
    using namespace loadedzones;
    const unsigned long long zm = 0x10000000ull, base = zm + 0xC8;
    int x = -7, y = -7;
    if (SectorOfZoneAt(zm, base, &x, &y) != 1 || x != 0 || y != 0) Fail("the first ZoneMap is area 0,0");
    if (SectorOfZoneAt(zm, base + 0x168, &x, &y) != 1 || x != 0 || y != 1) Fail("the next ZoneMap is area 0,1 (index = x * 64 + y)");
    if (SectorOfZoneAt(zm, base + 64ull * 0x168, &x, &y) != 1 || x != 1 || y != 0) Fail("index 64 is area 1,0");
    if (SectorOfZoneAt(zm, base + (20ull * 64 + 32) * 0x168, &x, &y) != 1 || x != 20 || y != 32) Fail("the Hub's ZoneMap is area 20,32");
    if (SectorOfZoneAt(zm, base + 4095ull * 0x168, &x, &y) != 1 || x != 63 || y != 63) Fail("the last ZoneMap is area 63,63");
    x = -7; y = -7;
    if (SectorOfZoneAt(zm, base + 4096ull * 0x168, &x, &y) != 0) Fail("past the array is refused");
    if (SectorOfZoneAt(zm, base - 8, &x, &y) != 0) Fail("before the array is refused");
    if (SectorOfZoneAt(zm, base + 0x168 + 8, &x, &y) != 0) Fail("a pointer inside a ZoneMap is refused");
    if (x != -7 || y != -7) Fail("a refusal leaves the area alone");
    if (InGrid(0, 0) != 1 || InGrid(63, 63) != 1 || InGrid(64, 0) != 0 || InGrid(0, -1) != 0) Fail("InGrid is 0..63 on both axes");
}
void t_t594_normalise_same()
{
    using namespace loadedzones;
    std::vector<Area> v;
    Area a;
    a.x = 21; a.y = 19; a.why = kWhyPlayer; v.push_back(a);
    a.x = 20; a.y = 32; a.why = kWhyCamera; v.push_back(a);
    a.x = 19; a.y = 32; a.why = 0; v.push_back(a);
    a.x = 20; a.y = 32; a.why = kWhyTown; v.push_back(a);
    Normalise(&v);
    if (v.size() != 3) Fail("a repeated area is listed once");
    else
    {
        if (v[0].x != 21 || v[0].y != 19 || v[1].x != 19 || v[1].y != 32 || v[2].x != 20 || v[2].y != 32) Fail("row order: y, then x");
        if (v[2].why != (kWhyCamera | kWhyTown)) Fail("a repeated area keeps the reasons of every copy");
    }
    std::vector<Area> w(v);
    w[0].why = 0;
    if (!SameAreas(v, w)) Fail("reasons are not compared");
    w[0].x = 22;
    if (SameAreas(v, w)) Fail("a different area is a change");
    w.pop_back();
    if (SameAreas(v, w)) Fail("a missing area is a change");
    std::vector<Area> e1, e2;
    if (!SameAreas(e1, e2)) Fail("two empty sets are the same");
}
void t_t594_sources()
{
    using namespace loadedzones;
    std::vector<Area> v;
    Area a;
    a.x = 43; a.y = 11; a.why = kWhyCamera | kWhyPlayer; v.push_back(a);   /* the watched character's area */
    a.x = 46; a.y = 14; a.why = kWhyPlayer; v.push_back(a);                /* the 7x7's corner */
    a.x = 20; a.y = 32; a.why = kWhyPlayer | kWhyTown; v.push_back(a);     /* a second character far away */
    a.x = 21; a.y = 32; a.why = 0; v.push_back(a);                         /* its lease running out */
    Sources s;
    CountSources(v, 43, 11, 3, &s);
    if (s.total != 4 || s.camera != 1 || s.player != 3 || s.town != 1 || s.leaseOnly != 1) Fail("counts by reason");
    if (s.outsideRing != 2) Fail("two areas lie outside the 7x7 around 43,11");
    CountSources(v, -1, -1, 3, &s);
    if (s.outsideRing != 4) Fail("with no centre every area is outside");
    std::vector<Area> e;
    CountSources(e, 43, 11, 3, &s);
    if (s.total != 0 || s.player != 0 || s.outsideRing != 0) Fail("an empty set counts nothing");
}
void t_t595_repeat_spawn()
{
    using namespace stalecopy;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (RepeatSpawnDecide(0, 0, 1, 12524.1f, 0) != kSpawnRetire) Fail("a copy 12,524 units from a SPAWN whose spot is not loaded here is retired and booked");
    if (RepeatSpawnDecide(0, 0, 1, 12524.1f, 1) != kSpawnRebuild) Fail("a stale copy whose SPAWN spot is loaded here is made again there");
    if (RepeatSpawnDecide(0, 0, 1, 12524.1f, -1) != kSpawnIgnore) Fail("a load test that could not be asked leaves the copy");
    if (RepeatSpawnDecide(0, 0, 1, 40.0f, 0) != kSpawnIgnore || RepeatSpawnDecide(0, 0, 1, kStaleDist, 1) != kSpawnIgnore) Fail("a copy within the far-snap distance is a repeat: ignored");
    if (RepeatSpawnDecide(0, 0, 1, kStaleDist + 0.5f, 1) != kSpawnRebuild) Fail("just past the distance is stale");
    if (RepeatSpawnDecide(0, 0, 1, -1.0f, 0) != kSpawnIgnore || RepeatSpawnDecide(0, 0, 1, nan, 0) != kSpawnIgnore) Fail("an unreadable copy position never retires it");
    if (RepeatSpawnDecide(1, 0, 1, 9000.0f, 0) != kSpawnIgnore) Fail("this game's own character is never retired by a SPAWN");
    if (RepeatSpawnDecide(0, 1, 1, 9000.0f, 0) != kSpawnIgnore) Fail("a twin is never retired by a SPAWN");
    if (RepeatSpawnDecide(0, 0, 0, 9000.0f, 0) != kSpawnIgnore) Fail("no live copy: not this rule's case");
}

void t_t595_far_unloaded()
{
    using namespace stalecopy;
    if (FarUnloadedRetire(1, 0, 1, 0) != 1) Fail("a placeable copy whose owner's target is not loaded here is retired");
    if (FarUnloadedRetire(1, 1, 1, 0) != 0) Fail("a loaded target is placed by the far snap, not retired");
    if (FarUnloadedRetire(1, -1, 1, 0) != 0) Fail("a load test that could not be asked never retires");
    if (FarUnloadedRetire(0, 0, 1, 0) != 0) Fail("a copy the far snap would not place (catchup off, prone, ragdolled, given up, mid-window, placement pending) is not retired");
    if (FarUnloadedRetire(1, 0, 0, 0) != 0) Fail("a stale owner sample never retires");
    if (FarUnloadedRetire(1, 0, 1, 1) != 0) Fail("a carried / caged / bedded copy, or one whose state could not be read, is not retired");
}

void t_t595_stale_keep()
{
    using namespace stalecopy;
    if (StaleCopyKeep(1, 0, 0, 0) != kKeepNone) Fail("a standing copy nothing holds may be retired");
    if (StaleCopyKeep(0, 0, 0, 0) != kKeepDown) Fail("a knocked-down copy is kept");
    if (StaleCopyKeep(1, 1, 0, 0) != kKeepRagdoll) Fail("a ragdolled copy is kept");
    if (StaleCopyKeep(1, 0, 1, 0) != kKeepGaveUp) Fail("a copy the puppet drive gave up placing is kept");
    if (StaleCopyKeep(1, 0, 0, 1) != kKeepAttached) Fail("a carried / caged / bedded copy, or one whose state could not be read, is kept");
    if (StaleCopyKeep(0, 1, 1, 1) != kKeepDown || StaleCopyKeep(1, 1, 1, 1) != kKeepRagdoll || StaleCopyKeep(1, 0, 1, 1) != kKeepGaveUp)
        Fail("the first reason that holds is named");
    for (int k = kKeepNone; k <= kKeepAttached; ++k)
        if (StaleCopyKeepName(k) == 0 || StaleCopyKeepName(k)[0] == 0) Fail("every keep reason has a name");
    if (std::string(StaleCopyKeepName(99)) != "unknown reason") Fail("an unknown reason is named as unknown");
    /* rule 1's order (RepeatSpawnStep): decided first (RepeatSpawnDecide), then the fresh check, then protection, then a keep reason */
    const int act = RepeatSpawnDecide(0, 0, 1, 12524.1f, 1);
    if (act != kSpawnRebuild) Fail("a stale copy whose SPAWN spot is loaded here is decided Rebuild");
    if (RepeatSpawnStep(kSpawnIgnore, 1, 1, kKeepDown) != kStepIgnore) Fail("a repeat SPAWN asks nothing further");
    if (RepeatSpawnStep(act, 1, 1, kKeepDown) != kStepFreshKept) Fail("a fresh sample beside the copy is asked first");
    if (RepeatSpawnStep(act, 0, 1, kKeepDown) != kStepProtected) Fail("a protected copy is never counted kept");
    if (RepeatSpawnStep(act, 0, 0, StaleCopyKeep(0, 0, 0, 0)) != kStepKept) Fail("a knocked-down stale copy nothing protects is kept");
    if (RepeatSpawnStep(act, 0, 0, StaleCopyKeep(1, 0, 0, 0)) != kStepAct) Fail("a standing stale copy nothing protects or keeps is made again");
    if (RepeatSpawnStep(RepeatSpawnDecide(0, 0, 1, 12524.1f, 0), 0, 0, kKeepNone) != kStepAct) Fail("one whose spot is not loaded here is retired");
}

void t_t595_fresh_sample()
{
    using namespace stalecopy;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (StaleCopyFreshKept(1, 30.0f, 12524.1f) != 1) Fail("a copy 12,524 units from a late SPAWN's spot but 30 from its owner's fresh sample is kept");
    if (StaleCopyFreshKept(1, kStaleDist, 9000.0f) != 1) Fail("within the far-snap distance of the fresh sample is kept");
    if (StaleCopyFreshKept(1, kStaleDist + 0.5f, 9000.0f) != 0) Fail("a copy far from its fresh sample too is stale");
    if (StaleCopyFreshKept(0, 30.0f, 9000.0f) != 0) Fail("no fresh sample: the SPAWN decides");
    if (StaleCopyFreshKept(1, -1.0f, 9000.0f) != 0 || StaleCopyFreshKept(1, nan, 9000.0f) != 0) Fail("an unreadable distance to the sample never keeps");
    if (StaleCopyFreshKept(1, 30.0f, kStaleDist) != 0 || StaleCopyFreshKept(1, 30.0f, 40.0f) != 0 || StaleCopyFreshKept(1, 30.0f, nan) != 0)
        Fail("a copy not stale by the SPAWN is not this check's case");
}

void t_t596_first_sighting()
{
    using namespace coopdoor;
    if (DoorChangeReportsIfNotHolder(-1, 0, 0) != 0) Fail("a first sighting was queued as an actor report");
    if (DoorChangeReportsIfNotHolder(kDoorClosed, 0, 0) != 1 || DoorChangeReportsIfNotHolder(kDoorOpen, 0, 0) != 1
        || DoorChangeReportsIfNotHolder(kDoorOpening, 0, 0) != 1 || DoorChangeReportsIfNotHolder(kDoorClosing, 0, 0) != 1)
        Fail("a change from a known state stopped being an actor report");
    /* what the drain makes of each: a non-holder's first sighting goes nowhere, a holder's is its word */
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(-1, 0, 0)) != kDoorSendRefuse) Fail("a non-holder's first sighting was sent");
    if (DoorDrainDecide(1, DoorChangeReportsIfNotHolder(-1, 0, 0)) != kDoorSendHolder) Fail("a holder's first sighting was not published");
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(kDoorOpening, 0, 0)) != kDoorSendActor) Fail("a non-holder's real change was not reported");
    /* an unsent first sighting is a re-offer: kept until sent */
    if (DoorUnsentDecide(kDoorOriginHolder, DoorChangeReportsIfNotHolder(-1, 0, 0)) != kDoorUnsentRetryUntilSent) Fail("an unsent first sighting was dropped");
}
void t_t596_key_sector()
{
    using namespace coopdoor;
    int x = -7, y = -7;
    if (!DoorKeySector("2934-Walls2.mod@18,35@-637784,169667,4942gate", &x, &y) || x != 18 || y != 35) Fail("a gate key's sector was misread");
    x = -7; y = -7;
    if (!DoorKeySector("3796-TwoStorey.mod@18,35@-623129,165055,3681", &x, &y) || x != 18 || y != 35) Fail("a door key's sector was misread");
    x = -7; y = -7;
    if (!DoorKeySector("a@0,63@1,2,3", &x, &y) || x != 0 || y != 63) Fail("sector 0,63 was misread");
    x = -7; y = -7;
    if (DoorKeySector("no-sector-here", &x, &y) || DoorKeySector("a@18;35@1", &x, &y) || DoorKeySector("a@,35@1", &x, &y)
        || DoorKeySector("a@18,@1", &x, &y) || DoorKeySector("a@18,35", &x, &y) || DoorKeySector("a@12345,1@1", &x, &y)
        || DoorKeySector("a@-1,3@1", &x, &y) || DoorKeySector(0, &x, &y) || DoorKeySector("a@1,2@", 0, &y))
        Fail("a malformed key was read as a sector");
    if (x != -7 || y != -7) Fail("a refused key wrote its outputs");
}
void t_t596_sector_serve()
{
    using namespace coopdoor;
    if (!DoorServesSectorAsk(1, 18, 35, 18, 35, kDoorClosed) || !DoorServesSectorAsk(1, 18, 35, 18, 35, kDoorOpen)) Fail("a resting door of the asked sector was not served");
    if (DoorServesSectorAsk(1, 18, 35, 18, 35, kDoorOpening) || DoorServesSectorAsk(1, 18, 35, 18, 35, kDoorClosing)) Fail("a swinging door was served");
    if (DoorServesSectorAsk(1, 18, 35, 17, 35, kDoorOpen) || DoorServesSectorAsk(1, 18, 35, 18, 34, kDoorOpen)) Fail("a door of another sector was served");
    if (DoorServesSectorAsk(0, 18, 35, 18, 35, kDoorOpen)) Fail("a key whose sector could not be read was served");
    if (DoorServesSectorAsk(1, 18, 35, 18, 35, -1)) Fail("a door in no known state was served");
}
void t_t596_gen_forget()
{
    using namespace coopdoor;
    if (DoorGenForgetOnArrival(-1, 2, 2)) Fail("an entry with no record was counted as forgotten");
    if (!DoorGenForgetOnArrival(500, 2, 2)) Fail("the arriving player's record was kept");
    if (DoorGenForgetOnArrival(500, 2, 3)) Fail("another player's record was forgotten on a named arrival");
    if (!DoorGenForgetOnArrival(500, 2, -1) || !DoorGenForgetOnArrival(500, -1, -1)) Fail("an unnamed arrival kept a record");
    /* the restarted counter: refused while the old record stands, taken once it is forgotten */
    if (!DoorGenStale(77, 500, 77, 1)) Fail("the old record did not refuse the restarted counter (the case this fixes)");
    if (DoorGenStale(-1, -1, 77, 1)) Fail("a forgotten record still refused the restarted counter");
    /* E1: the publisher on record is a player key, 0x80000000 | slot - negative as a long; -1 is the empty marker */
    if (!DoorHeldPublisherKnown((long)0x80000002u) || !DoorHeldPublisherKnown((long)0x8000FFFFu))
        Fail("a player key (negative as a long) was read as no publisher - its record would never be forgotten on arrival");
    if (!DoorHeldPublisherKnown(3)) Fail("a session peer's raw id was read as no publisher");
    if (DoorHeldPublisherKnown(-1)) Fail("the empty marker was read as a publisher");
}
void t_t596_giveup_rearm()
{
    using namespace coopdoor;
    if (!DoorWordReArmsGiveUp(1, 1)) Fail("an accepted word did not re-arm a given-up row");
    if (DoorWordReArmsGiveUp(0, 1)) Fail("a refused word re-armed a given-up row");
    if (DoorWordReArmsGiveUp(1, 0)) Fail("a row that had not given up was counted as re-armed");
}
void t_t596_held_evict()
{
    using namespace coopdoor;
    if (!DoorHeldEvictable(1, 0, 0)) Fail("an entry for a door not loaded here was not evictable");
    if (DoorHeldEvictable(1, 1, 0)) Fail("an entry for a live door was evicted");
    if (DoorHeldEvictable(1, 0, 1)) Fail("an entry with a report waiting was evicted");
    if (DoorHeldEvictable(0, 0, 0)) Fail("an unused entry was counted as an eviction");
    if (!DoorHeldOlder(9, 0, 0)) Fail("the first candidate was not taken");
    if (!DoorHeldOlder(3, 5, 1)) Fail("an older entry lost to a newer one");
    if (DoorHeldOlder(7, 5, 1) || DoorHeldOlder(5, 5, 1)) Fail("a newer or equal entry replaced the oldest");
    if (sizeof(unsigned long) == 4)
    {
        if (!DoorHeldOlder(0xFFFFFFF0ul, 5ul, 1)) Fail("an entry stamped before the counter wrapped was not older");
        if (DoorHeldOlder(5ul, 0xFFFFFFF0ul, 1)) Fail("an entry stamped after the counter wrapped was taken as older");
    }
}
void t_t596_reload_sequence()
{
    using namespace coopdoor;
    /* B reloads in a town A holds; A's gate is open.  B's own load registers the gate closed (a first sighting): not reported, so
       A keeps it open.  A answers B's sector ask with OPEN as its word; B applies it on its registered, untouched copy. */
    int x = 0, y = 0;
    const int parsed = DoorKeySector("2934-Walls2.mod@18,35@-614444,160142,3821gate", &x, &y);
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(-1, 0, 0)) != kDoorSendRefuse) Fail("B's load-time closed state was reported to A");
    if (!DoorServesSectorAsk(parsed, x, y, 18, 35, kDoorOpen)) Fail("A did not serve its open gate to B's sector ask");
    if (DoorDrainDecide(1, 0) != kDoorSendHolder) Fail("A's served gate did not go as the holder's word");
    if (DoorRecvDecide(kDoorOriginHolder, 0) != kDoorRecvStore) Fail("B did not store A's word");
    if (DoorHolderWordDecide(kDoorAreaOther) != kDoorWordStore || DoorHolderWordDecide(kDoorAreaUnanswerable) != kDoorWordStoreUnanswerable)
        Fail("B refused A's word (or lost it while its copy of the gate had no row yet)");
    {
        DoorDecideIn in;
        in.liveState = kDoorClosed; in.holderState = kDoorOpen; in.localWrite = DoorSightingIsLocalWrite(-1);
        in.forcedOpen = 0; in.weHold = -1; in.gaveUp = 0; in.gaveUpChurn = 0;
        if (DoorDecide(in) != kDoorActApply) Fail("B did not open its gate to A's word");
    }
}
void t_t596_report_decision()
{
    using namespace coopdoor;
    const int was[5] = { -1, kDoorClosed, kDoorOpen, kDoorOpening, kDoorClosing };
    for (int w = 0; w < 5; ++w)
        for (int lw = 0; lw < 2; ++lw)
            for (int ac = 0; ac < 2; ++ac)
            {
                const int want = (lw != 0) ? 0 : ((ac != 0) ? 1 : ((was[w] < 0) ? 0 : 1));
                if (DoorChangeReportsIfNotHolder(was[w], lw, ac) != want) Fail("the (was, load writer, entry point) decision is wrong for a case");
            }
    /* R12: a load-time write on a gate the game already knew (open -> closed) is not an actor report */
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(kDoorOpen, 1, 0)) != kDoorSendRefuse) Fail("a non-holder reported a load-time write on a known door");
    if (DoorDrainDecide(1, DoorChangeReportsIfNotHolder(kDoorOpen, 1, 0)) != kDoorSendHolder) Fail("a holder's load-time write was not published as its word");
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(kDoorOpen, 1, DoorEntryIsAction("closeDoor"))) != kDoorSendRefuse) Fail("a load-time writer's call into closeDoor was reported");
    /* a lock press on a door whose row was dropped while loaded (was -1) reaches the holder as an actor report */
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(-1, 0, DoorEntryIsAction("lockButton"))) != kDoorSendActor) Fail("a lock press on a new row was not reported");
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(-1, 0, DoorEntryIsAction("openDoor"))) != kDoorSendActor) Fail("an openDoor on a new row was not reported");
    if (DoorDrainDecide(0, DoorChangeReportsIfNotHolder(-1, 0, DoorEntryIsAction("funnel"))) != kDoorSendRefuse) Fail("a funnel first sighting was reported");
    if (!DoorEntryIsAction("openDoor") || !DoorEntryIsAction("closeDoor") || !DoorEntryIsAction("lockButton")) Fail("an act entry point was not an action");
    if (DoorEntryIsAction("funnel") || DoorEntryIsAction("lockDoor") || DoorEntryIsAction("apply") || DoorEntryIsAction("play")
        || DoorEntryIsAction("relock") || DoorEntryIsAction("") || DoorEntryIsAction(0) || DoorEntryIsAction("openDoorX")
        || DoorEntryIsAction("openDoo") || DoorEntryIsAction("opendoor"))
        Fail("a non-act entry point was taken as an action");
    {
        const unsigned long long wr[3] = { 0x29D1D2ull, 0ull, 0x55347Eull };
        if (!DoorRetIsLoadWriter(0x29D1D2ull, wr, 3) || !DoorRetIsLoadWriter(0x55347Eull, wr, 3)) Fail("a listed load writer was not recognised");
        if (DoorRetIsLoadWriter(0x29D1D3ull, wr, 3)) Fail("an unlisted return address was taken as a load writer");
        if (DoorRetIsLoadWriter(0ull, wr, 3)) Fail("an unreadable return address (0) matched a row the table did not fill");
        if (DoorRetIsLoadWriter(0x29D1D2ull, wr, 0) || DoorRetIsLoadWriter(0x29D1D2ull, 0, 3)) Fail("an empty writer list matched");
    }
}
void t_t596_catchup_serve()
{
    using namespace coopdoor;
    const int xs[3] = { 17, 18, 19 }, ys[3] = { 35, 35, 36 };
    if (!DoorServesCatchup(1, 18, 35, xs, ys, 3, kDoorOpen) || !DoorServesCatchup(1, 19, 36, xs, ys, 3, kDoorClosed)) Fail("a resting door of an asked sector was not served");
    if (DoorServesCatchup(1, 18, 36, xs, ys, 3, kDoorOpen)) Fail("a door of a sector not asked was served (x and y must come from one sector)");
    if (DoorServesCatchup(1, 18, 35, xs, ys, 3, kDoorOpening) || DoorServesCatchup(1, 18, 35, xs, ys, 3, kDoorClosing)) Fail("a swinging door was served");
    if (DoorServesCatchup(0, 18, 35, xs, ys, 3, kDoorOpen)) Fail("a key whose sector could not be read was served");
    if (DoorServesCatchup(1, 18, 35, xs, ys, 0, kDoorOpen) || DoorServesCatchup(1, 18, 35, 0, ys, 3, kDoorOpen)
        || DoorServesCatchup(1, 18, 35, xs, 0, 3, kDoorOpen))
        Fail("an empty ask served a door");
}
void t_t596_word_refind()
{
    using namespace coopdoor;
    if (DoorDropMarksReFind(-1)) Fail("a dropped row with no stored word was marked");
    if (!DoorDropMarksReFind(kDoorClosed) || !DoorDropMarksReFind(kDoorOpen)) Fail("a dropped row holding the holder's word was not marked");
    if (DoorHeldResolveDue(0, 0)) Fail("a held entry with nothing due was looked up");
    if (!DoorHeldResolveDue(1, 0) || !DoorHeldResolveDue(0, 1) || !DoorHeldResolveDue(1, 1)) Fail("a due look-up was skipped");
}
void t_t596_serve_norow()
{
    using namespace coopdoor;
    const int xs[2] = { 17, 18 }, ys[2] = { 35, 36 };
    if (!DoorKeyInAsk(1, 18, 36, xs, ys, 2) || !DoorKeyInAsk(1, 17, 35, xs, ys, 2)) Fail("a key of an asked sector was not in the ask");
    if (DoorKeyInAsk(1, 18, 35, xs, ys, 2)) Fail("x and y of two different asked sectors were taken as one");
    if (DoorKeyInAsk(0, 17, 35, xs, ys, 2) || DoorKeyInAsk(1, 17, 35, xs, ys, 0) || DoorKeyInAsk(1, 17, 35, 0, ys, 2)
        || DoorKeyInAsk(1, 17, 35, xs, 0, 2))
        Fail("an unparsed key or an empty ask matched");
    if (!DoorServeMarksHeld(1, 0)) Fail("a key known only by its held entry, in an asked sector, was not marked");
    if (DoorServeMarksHeld(1, 1) || DoorServeMarksHeld(0, 0)) Fail("a key with a row, or outside the ask, was marked");
    if (DoorHeldLookupDue(0, 0, 0)) Fail("a held entry with nothing due was looked up");
    if (!DoorHeldLookupDue(0, 0, 1) || !DoorHeldLookupDue(1, 0, 0) || !DoorHeldLookupDue(0, 1, 0)) Fail("a due look-up was skipped");
    if (DoorHeldVisited(-1, 0, 0)) Fail("an entry with no word, report or mark was visited");
    if (!DoorHeldVisited(-1, 0, 1) || !DoorHeldVisited(-1, 1, 0) || !DoorHeldVisited(0, 0, 0)) Fail("an entry with work was not visited");
}
void t_t596_word_line_budget()
{
    using namespace coopdoor;
    if (!DoorKeyNamesGate("Wall@18,35@12,4,9gate") || !DoorKeyNamesGate("Gate") || !DoorKeyNamesGate("xGaTex")) Fail("a gate key was not recognised");
    if (DoorKeyNamesGate("gat") || DoorKeyNamesGate("") || DoorKeyNamesGate(0) || DoorKeyNamesGate("door@1,2@3")) Fail("a key without the word was taken for a gate");
    if (!DoorWordLinePrints(1, 4, 40, 0, 4, 40) || DoorWordLinePrints(1, 5, 1, 0, 4, 40)) Fail("the per-key budget was not kept");
    if (DoorWordLinePrints(1, 1, 41, 0, 4, 40)) Fail("a non-gate line past the session total was printed");
    if (!DoorWordLinePrints(1, 1, 999, 1, 4, 40)) Fail("a gate line within its own budget was refused by the session total");
    if (DoorWordLinePrints(1, 5, 0, 1, 4, 40)) Fail("a gate line past its own budget was printed");
    if (!DoorWordLinePrints(0, 0, 40, 1, 4, 40) || DoorWordLinePrints(0, 0, 41, 1, 4, 40)) Fail("a key with no held entry did not follow the session total");
}
void t_t596_held_victim()
{
    using namespace coopdoor;
    if (!DoorHeldBetterVictim(1, 9, 0, 0, 0)) Fail("the first candidate was not taken");
    if (!DoorHeldBetterVictim(0, 9, 1, 3, 1)) Fail("an entry holding a word was kept over a newer one holding none");
    if (DoorHeldBetterVictim(1, 3, 0, 9, 1)) Fail("an entry holding a word replaced one holding none");
    if (!DoorHeldBetterVictim(0, 3, 0, 5, 1) || DoorHeldBetterVictim(0, 7, 0, 5, 1)) Fail("among entries holding no word the oldest was not taken");
    if (!DoorHeldBetterVictim(1, 3, 1, 5, 1) || DoorHeldBetterVictim(1, 5, 1, 5, 1)) Fail("among entries holding a word the oldest was not taken");
}
void t_t593_position_key()
{
    using namespace factionkey;
    if (!PositionTakesKey("Shinobi Thieves", false, "12-gamedata.base", true)) Fail("a row naming its faction by a displayed name takes the marked code");
    if (!PositionTakesKey("United Cities", false, "defaultEmpireFactionSID", true)) Fail("a marked code of any shape is taken (the UC citizens' code has no digits)");
    if (!PositionTakesKey("Dust Bandits", false, "11624-Dialogue (10).mod", true)) Fail("a marked code with spaces and parentheses is taken");
    if (PositionTakesKey("Shinobi Thieves", false, "12-gamedata.base", false)) Fail("an unmarked key never replaces the row's faction");
    if (PositionTakesKey("12-gamedata.base", false, "Shinobi Thieves", false) || PositionTakesKey("defaultEmpireFactionSID", true, "United Cities", false)) Fail("never code -> name");
    if (PositionTakesKey("12-gamedata.base", false, "12-gamedata.base", true)) Fail("the same code: nothing to change");
    if (PositionTakesKey("@slot:2:Bob", false, "12-gamedata.base", true) || PositionTakesKey("@slot:2", false, "12-gamedata.base", true)) Fail("never a player's slot name");
    if (PositionTakesKey("My Crew", false, "My Other Crew", false)) Fail("a player's faction is never sent marked, so its name is never replaced");
    if (PositionTakesKey("", false, "12-gamedata.base", true)) Fail("a row with no faction field is not re-keyed");
    if (PositionTakesKey("Shinobi Thieves", false, "", true) || PositionTakesKey("Shinobi Thieves", false, "@slot:1", true) || PositionTakesKey("Shinobi Thieves", false, "12-a\tb", true)) Fail("a marked key that is not usable as a code is refused");
    if (kRecFacCode != 1u) Fail("the flags byte's bit 0 marks the code");
    if (PositionTakesKey("12-gamedata.base", true, "13-gamedata.base", true) || PositionTakesKey("defaultEmpireFactionSID", true, "12-gamedata.base", true)) Fail("a row known to hold a code is never swapped for another code");
    if (!PositionTakesKey("12-gamedata.base", false, "13-gamedata.base", true)) Fail("a field not known to be a code takes a marked code");
}
/* ---- A LOST COPY COMES BACK (src/common/lostcopy.h) ------------------------------------------------------------------------ */
void t_t586_resend_wire()
{
    using namespace lostcopy;
    std::vector<unsigned char> raw;
    std::vector<unsigned int> u; std::vector<int> v;
    u.push_back(4194305u); v.push_back(kRvAsk);
    if (!EncodeResend(&raw, kRsAsk, u, v) || raw.size() != 7u) { Fail("a one-uid ASK encodes to 7 bytes"); return; }
    int kind = 0; std::vector<unsigned int> du; std::vector<int> dv;
    if (DecodeResend(&raw[0], raw.size(), &kind, &du, &dv) != kRsDecodeOk || kind != kRsAsk || du.size() != 1u || du[0] != 4194305u || dv[0] != kRvAsk)
        Fail("a one-uid ASK round-trips");
    u.clear(); v.clear();
    u.push_back(1u); v.push_back(kRvSent); u.push_back(0xFFFFFFFFu); v.push_back(kRvHeld); u.push_back(70000u); v.push_back(kRvNoRoad);
    if (!EncodeResend(&raw, kRsAnswer, u, v) || raw.size() != 17u) { Fail("a three-uid ANSWER encodes to 17 bytes"); return; }
    if (DecodeResend(&raw[0], raw.size(), &kind, &du, &dv) != kRsDecodeOk || kind != kRsAnswer || du != u || dv != v) Fail("a three-uid ANSWER round-trips");
    raw.push_back(9); if (DecodeResend(&raw[0], raw.size(), &kind, &du, &dv) != kRsDecodeOk) Fail("bytes past the entries are ignored");
    std::vector<unsigned char> keep = raw;
    if (EncodeResend(&raw, 3, u, v)) Fail("a bad kind is not encoded");
    if (raw != keep) Fail("a refused encode writes nothing");
    std::vector<int> wrongAsk(3, kRvSent);
    if (EncodeResend(&raw, kRsAsk, u, wrongAsk)) Fail("an ASK carries kRvAsk only");
    std::vector<int> wrongAnswer(3, kRvAsk);
    if (EncodeResend(&raw, kRsAnswer, u, wrongAnswer)) Fail("an ANSWER never carries kRvAsk");
    std::vector<int> high(3, kRvMax + 1);
    if (EncodeResend(&raw, kRsAnswer, u, high)) Fail("a verdict past kRvMax is not encoded");
    std::vector<unsigned int> withZero(u); withZero[1] = 0u;
    if (EncodeResend(&raw, kRsAnswer, withZero, v)) Fail("uid 0 is not encoded");
    std::vector<unsigned int> none; std::vector<int> noneV;
    if (EncodeResend(&raw, kRsAsk, none, noneV)) Fail("an empty ask is not encoded");
    std::vector<unsigned int> many(kResendMax + 1, 5u); std::vector<int> manyV(kResendMax + 1, kRvAsk);
    if (EncodeResend(&raw, kRsAsk, many, manyV)) Fail("more than kResendMax uids are not encoded");
    many.pop_back(); manyV.pop_back();
    if (!EncodeResend(&raw, kRsAsk, many, manyV) || raw.size() != 2u + 5u * kResendMax) Fail("kResendMax uids are encoded");
    std::vector<int> shortV(2, kRvSent);
    if (EncodeResend(&raw, kRsAnswer, u, shortV)) Fail("uids and verdicts of different sizes are not encoded");
    const unsigned char shortMsg[1] = { 1 };
    if (DecodeResend(shortMsg, 1, &kind, &du, &dv) != kRsDecodeShort) Fail("one byte is short");
    const unsigned char badKind[7] = { 9, 1, 1, 0, 0, 0, 0 };
    if (DecodeResend(badKind, 7, &kind, &du, &dv) != kRsDecodeBadKind) Fail("kind 9 is refused");
    const unsigned char zeroN[2] = { 1, 0 };
    if (DecodeResend(zeroN, 2, &kind, &du, &dv) != kRsDecodeBadCount) Fail("n 0 is refused");
    const unsigned char bigN[2] = { 1, 33 };
    if (DecodeResend(bigN, 2, &kind, &du, &dv) != kRsDecodeBadCount) Fail("n 33 is refused");
    const unsigned char cut[6] = { 1, 1, 1, 0, 0, 0 };
    if (DecodeResend(cut, 6, &kind, &du, &dv) != kRsDecodeShort) Fail("an entry cut short is refused");
    const unsigned char uid0[7] = { 1, 1, 0, 0, 0, 0, 0 };
    if (DecodeResend(uid0, 7, &kind, &du, &dv) != kRsDecodeBadUid) Fail("uid 0 is refused");
    const unsigned char askSent[7] = { 1, 1, 5, 0, 0, 0, 1 };
    if (DecodeResend(askSent, 7, &kind, &du, &dv) != kRsDecodeBadVerdict) Fail("an ASK with a verdict is refused");
    const unsigned char answerAsk[7] = { 2, 1, 5, 0, 0, 0, 0 };
    if (DecodeResend(answerAsk, 7, &kind, &du, &dv) != kRsDecodeBadVerdict) Fail("an ANSWER with kRvAsk is refused");
    const unsigned char answerHigh[7] = { 2, 1, 5, 0, 0, 0, 9 };
    if (DecodeResend(answerHigh, 7, &kind, &du, &dv) != kRsDecodeBadVerdict) Fail("an ANSWER verdict past kRvMax is refused");
    kind = 77; du.assign(1, 42u);
    if (DecodeResend(answerHigh, 7, &kind, &du, &dv) == kRsDecodeOk || kind != 77 || du.size() != 1u || du[0] != 42u) Fail("a refused decode writes nothing");
    for (int i = kRvAsk; i <= kRvMax; ++i) if (std::string(ResendVerdictName(i)) == "?") Fail("every verdict has a name");
}
void t_t586_owner_verdict()
{
    using namespace lostcopy;
    if (ResendOwnerVerdict(0, 0, 1, 1) != kRvNotMine) Fail("not ours: NOT MINE");
    if (ResendOwnerVerdict(0, 1, 0, 0) != kRvNotMine) Fail("not ours beats everything");
    if (ResendOwnerVerdict(1, 1, 1, 1) != kRvHeld) Fail("a hand-over in flight: HELD");
    if (ResendOwnerVerdict(1, 1, 0, 0) != kRvHeld) Fail("HELD before NOT ANNOUNCED / NO BODY");
    if (ResendOwnerVerdict(1, 0, 0, 1) != kRvNotAnnounced) Fail("not announced: the announce pass decides");
    if (ResendOwnerVerdict(1, 0, 1, 0) != kRvNoBody) Fail("no readable body: NO BODY");
    if (ResendOwnerVerdict(1, 0, 1, 1) != kRvSent) Fail("ours, announced, readable, no hand-over: SENT");
    /* FAILS BEFORE: the owner answered NOT IN AREA from its own map of the asking game, which can lag that game; the asker parked the
       row for its visit and a character standing still was never sent. The asker's SPAWN check decides by its live engine. */
    if (ResendOwnerVerdict(1, 0, 1, 1) != kRvSent) Fail("wherever the character stands: SENT (the asker's SPAWN check decides)");
    if (std::string(ResendVerdictName(kRvNotInArea)) == "?") Fail("NOT IN AREA has no name");
}
void t_t586_lost_book()
{
    using namespace lostcopy;
    /* who may be booked: another game's uid with an owner on record whose SPAWN made a row here */
    for (int m = 0; m < 2; ++m) for (int k = 0; k < 2; ++k) for (int rf = 0; rf < 2; ++rf)
        if (LostNoteAllowed(m, k, rf) != ((m == 0 && k == 1 && rf == 0) ? 1 : 0)) { Fail("booked only: not ours, owner known, not refused by this table"); break; }
    LostBook b;
    if (LostNote(&b, 0u, 1u, 1, 0, 0, 0) != -1 || !b.rows.empty()) Fail("uid 0 is not booked");
    if (LostNote(&b, 7u, 100u, 0, 0, 0, 0) != 1 || b.rows.size() != 1u || b.rows[0].hasPos != 0 || b.rows[0].state != kLcWaiting) Fail("a new row starts WAITING");
    if (LostNote(&b, 7u, 100u, 1, 1.0f, 2.0f, 3.0f) != 0 || b.rows.size() != 1u || b.rows[0].hasPos != 1 || b.rows[0].z != 3.0f) Fail("a refresh takes the spot");
    b.rows[0].state = kLcRefused; b.rows[0].asksThisVisit = 2; b.rows[0].loadedPrev = 1;
    LostNote(&b, 7u, 100u, 0, 0, 0, 0);
    if (b.rows[0].state != kLcRefused || b.rows[0].hasPos != 1 || b.rows[0].x != 1.0f || b.rows[0].asksThisVisit != 2) Fail("a refresh keeps the state, the spot and the visit's count");
    b.rows[0].state = kLcHere;
    LostNote(&b, 7u, 100u, 1, 4.0f, 0.0f, 4.0f);
    if (b.rows[0].state != kLcWaiting || b.rows[0].asksThisVisit != 2 || b.rows[0].loadedPrev != 1) Fail("a copy lost again: WAITING, same visit, same count");
    LostNote(&b, 7u, 200u, 0, 0, 0, 0);
    if (b.rows[0].ownerKey != 200u || b.rows[0].state != kLcWaiting || b.rows[0].asksThisVisit != 0 || b.rows[0].loadedPrev != 0) Fail("a new owner starts the row again");
    for (unsigned int u = 1000u; u < 1000u + (unsigned int)kLostBookMax + 5u; ++u) LostNote(&b, u, 1u, 0, 0, 0, 0);
    if (b.rows.size() != kLostBookMax || b.full != 6 || LostFind(b, 7u) != -1 || LostFind(b, 1000u + (unsigned int)kLostBookMax + 4u) < 0) Fail("the book is bounded; the oldest rows go");
    /* answers count only from the owner on record */
    if (LostAnswerFromOwner(5u, 5u) != 1 || LostAnswerFromOwner(5u, 6u) != 0 || LostAnswerFromOwner(0u, 0u) != 0) Fail("an answer counts from the row's owner only");
    /* the per-uid log budget */
    for (long long n = 1; n <= 12; ++n) if (LostUidLogThis(n) != 1) Fail("a uid's first 12 events are logged");
    if (LostUidLogThis(0) != 0 || LostUidLogThis(13) != 0 || LostUidLogThis(99) != 0 || LostUidLogThis(100) != 1 || LostUidLogThis(200) != 1) Fail("then every 100th");
}
void t_t586_lost_look()
{
    using namespace lostcopy;
    LostBook b;
    LostNote(&b, 9u, 1u, 1, 5.0f, 0.0f, 5.0f);
    LostRow& r = b.rows[0];
    if (r.backThisVisit != 0) Fail("a new row has not come back");
    int a = LostLookDecide(0, 1, 0, r);
    if (a != kLaWait) Fail("a spot not loaded here is never asked for");
    LostLookApply(&r, 0, a, 0);
    a = LostLookDecide(0, 1, 1, r);
    if (a != kLaAsk) Fail("the spot loads: ask");
    LostLookApply(&r, 1, a, 1);
    if (r.state != kLcAsked || r.asksThisVisit != 1 || r.asksTotal != 1 || r.loadedPrev != 1) Fail("an ask sent: ASKED, counted");
    int asks = 0;
    for (int i = 0; i < 600; ++i) { a = LostLookDecide(0, 1, 1, r); if (a == kLaAsk) ++asks; LostLookApply(&r, 1, a, a == kLaAsk ? 1 : 0); }
    if (asks != 0 || r.asksTotal != 1) Fail("while the answer is owed, 600 looks ask nothing more");
    /* the spot sleeps and loads again while the answer is still owed: the new visit asks again */
    a = LostLookDecide(0, 1, 0, r); LostLookApply(&r, 0, a, 0);
    a = LostLookDecide(0, 1, 1, r);
    if (a != kLaAsk) Fail("a new visit asks again for an owed answer (an answer lost on the way)");
    LostLookApply(&r, 1, a, 1);
    if (r.state != kLcAsked || r.loadedPrev != 1 || r.asksThisVisit != 1) Fail("the new visit's ask: ASKED, counted from 1");
    LostAnswerApply(&r, kRvSent);
    if (r.state != kLcAnswered) Fail("SENT: ANSWERED");
    /* SENT and no copy here: no further ask in this visit */
    for (int i = 0; i < 10; ++i) { a = LostLookDecide(0, 1, 1, r); if (a == kLaAsk) { Fail("an answered row is not asked again in the same visit"); break; } LostLookApply(&r, 1, a, 0); }
    /* the copy is back on loaded ground: the row stays HERE for the visit */
    a = LostLookDecide(1, 1, 1, r);
    if (a != kLaHere) Fail("the copy is back while its spot is loaded: the row stays");
    LostLookApply(&r, 1, a, 0);
    if (r.state != kLcHere || r.backThisVisit != 1) Fail("HERE marks the visit");
    LostAnswerApply(&r, kRvHeld);
    if (r.state != kLcHere) Fail("a late answer does not move a HERE row");
    /* THE LOOP: the engine puts the copy away again and again on the same loaded ground - no ask in the visit it came back in */
    int loopAsks = 0;
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        LostNote(&b, 9u, 1u, 1, 5.0f, 0.0f, 5.0f);   /* lost again */
        if (r.state != kLcWaiting) { Fail("a copy lost again: WAITING"); break; }
        a = LostLookDecide(0, 1, 1, r);
        if (a == kLaAsk) ++loopAsks;
        LostLookApply(&r, 1, a, a == kLaAsk ? 1 : 0);
        a = LostLookDecide(1, 1, 1, r);              /* back by the owner's own paths */
        LostLookApply(&r, 1, a, 0);
    }
    if (loopAsks != 0 || r.asksTotal != 2) Fail("a copy put away again in the visit it came back in is not asked for in that visit");
    /* the visit ends (the spot sleeps) while the copy is here: the row goes */
    if (LostLookDecide(1, 1, 0, r) != kLaDrop) Fail("the copy here and the visit over: dropped");
    /* a copy lost and back in one visit, then a new visit: asked again */
    {
        LostRow v = r; v.state = kLcWaiting; v.backThisVisit = 1; v.loadedPrev = 1;
        if (LostLookDecide(0, 1, 1, v) != kLaWait) Fail("back in this visit: no ask");
        a = LostLookDecide(0, 1, 0, v); LostLookApply(&v, 0, a, 0);
        a = LostLookDecide(0, 1, 1, v);
        if (a != kLaAsk) Fail("the next visit asks again");
        LostLookApply(&v, 1, a, 1);
        if (v.backThisVisit != 0 || v.state != kLcAsked || v.asksThisVisit != 1) Fail("a new visit clears the mark and counts from 1");
    }
    /* a row lost on unloaded ground, then loaded again: a new visit */
    LostRow n = r; n.state = kLcWaiting; n.loadedPrev = 0; n.asksTotal = 0;
    a = LostLookDecide(0, 1, 1, n);
    if (a != kLaAsk) Fail("a new visit asks again");
    LostLookApply(&n, 1, a, 1);
    if (n.asksThisVisit != 1 || n.state != kLcAsked) Fail("a new visit counts from 1");
    LostAnswerApply(&n, kRvNotAnnounced);
    if (n.state != kLcRefused) Fail("a refusal: REFUSED");
    for (int i = 0; i < 10; ++i) { a = LostLookDecide(0, 1, 1, n); if (a == kLaAsk) Fail("a refused row is not asked again in the same visit"); LostLookApply(&n, 1, a, 0); }
    a = LostLookDecide(0, 1, 0, n); LostLookApply(&n, 0, a, 0);
    a = LostLookDecide(0, 1, 1, n);
    if (a != kLaAsk) Fail("a refused row is asked again in the next visit");
    /* asks that find no road: one road outage counts once, however many looks it lasts; the next outage counts again */
    n.noRoad = 0; n.loadedPrev = 1; n.asksThisVisit = 0; n.state = kLcWaiting; n.backThisVisit = 0;   /* inside a visit, nothing asked yet */
    const int before = n.asksThisVisit;
    for (int i = 0; i < 50; ++i)
    {
        a = LostLookDecide(0, 1, 1, n);
        if (a != kLaAsk) { Fail("an outage alone never spends the visit's budget"); break; }
        if (LostNoRoadFirst(n) != (i == 0 ? 1 : 0)) { Fail("only the first no-road ask of an outage is its first"); break; }
        LostLookApply(&n, 1, a, 0);
    }
    if (n.asksThisVisit != before + 1 || n.state != kLcWaiting || n.asksTotal != 1 || n.noRoad != 1) Fail("one outage counts once and is not counted as sent");
    a = LostLookDecide(0, 1, 1, n); LostLookApply(&n, 1, a, 1);   /* the road is back: the ask goes */
    if (n.noRoad != 0 || n.state != kLcAsked || n.asksThisVisit != before + 2) Fail("an ask that goes out ends the outage");
    LostAnswerApply(&n, kRvSent);
    if (LostLookDecide(0, 1, 1, n) != kLaWait) Fail("SENT: no ask in the same visit");
    a = LostLookDecide(0, 1, 0, n); LostLookApply(&n, 0, a, 0);
    a = LostLookDecide(0, 1, 1, n); LostLookApply(&n, 1, a, 0);     /* the next visit, a second outage */
    if (a != kLaAsk || n.asksThisVisit != 1 || n.noRoad != 1) Fail("a second outage counts again");
    /* a copy back (HERE) is trusted again after kLostHereMovesReset of its owner's MOVEs */
    {
        LostRow h = n; h.hasPos = 1; h.loadedPrev = 1; h.asksThisVisit = kLostAsksPerVisit;
        a = LostLookDecide(1, 1, 1, h); LostLookApply(&h, 1, a, 0);
        if (h.state != kLcHere || h.hereMoves != 0 || h.backThisVisit != 1) Fail("coming back starts the MOVE count at 0");
        {
            LostRow e = h; e.state = kLcWaiting; e.asksThisVisit = 0;
            if (LostLookDecide(0, 1, 1, e) != kLaWait) Fail("a loss before the reset, in the visit it came back in: no ask");
        }
        int resets = 0;
        for (int i = 0; i < kLostHereMovesReset - 1; ++i) resets += LostHereMoveSeen(&h);
        if (resets != 0 || h.asksThisVisit != kLostAsksPerVisit || h.backThisVisit != 1) Fail("fewer than kLostHereMovesReset MOVEs: the count stands");
        if (LostHereMoveSeen(&h) != 1 || h.asksThisVisit != 0 || h.hereMoves != 0 || h.backThisVisit != 0) Fail("the kLostHereMovesReset-th MOVE resets the visit's count and mark");
        LostRow w = h; w.state = kLcWaiting;   /* lost again much later in the same stay */
        if (LostLookDecide(0, 1, 1, w) != kLaAsk) Fail("a real loss after the reset is asked for");
        if (LostHereMoveSeen(&w) != 0 || w.hereMoves != h.hereMoves) Fail("MOVEs count only while the copy is HERE");
        if (LostHereMoveSeen(0) != 0) Fail("no row, no count");
    }
    /* the owner went */
    if (LostLookDecide(0, 0, 1, n) != kLaDrop || LostLookDecide(1, 0, 1, n) != kLaDrop) Fail("no owner on record: dropped");
    /* a row with no spot is never asked for */
    LostRow p = n; p.hasPos = 0; p.loadedPrev = 0; p.state = kLcWaiting; p.asksThisVisit = 0;
    if (LostLookDecide(0, 1, 1, p) != kLaWait) Fail("a row with no spot waits");
    LostLookApply(&p, 1, kLaWait, 0);
    if (p.loadedPrev != 0) Fail("a row with no spot starts no visit");
    if (LostLookDecide(1, 1, 1, p) != kLaDrop) Fail("a row with no spot whose copy is here goes");
}
/* A lost copy is asked for only where this game's engine keeps it: its area loaded and its spot not within the edge strip of a
   side whose neighbour area is not loaded. */
void t_lost_spot_kept()
{
    using namespace lostcopy;
    for (int zl = -1; zl <= 1; ++zl) for (int ek = -1; ek <= 1; ++ek)
        if (LostSpotKept(zl, ek) != ((zl == 1 && ek == 1) ? 1 : 0)) { Fail("kept only when the area is loaded and the engine keeps the spot; -1 is not kept"); break; }
    if (LostSpotKept(2, 1) != 0 || LostSpotKept(1, 2) != 0) Fail("only 1 reads as yes");
    /* a new row on loaded ground in the edge strip: it waits and starts no visit; it is asked for once it stands further in */
    {
        LostBook b;
        LostNote(&b, 11u, 1u, 1, 5.0f, 0.0f, 5.0f);
        LostRow& r = b.rows[0];
        if (r.edgeWaitLine != 0) Fail("a new row has written no edge-strip line");
        for (int i = 0; i < 50; ++i)
        {
            const int w = LostLookDecide(0, 1, LostSpotKept(1, 0), r);
            if (w != kLaWait) { Fail("loaded but not kept: no ask"); break; }
            LostLookApply(&r, LostSpotKept(1, 0), w, 0);
        }
        if (r.loadedPrev != 0 || r.asksThisVisit != 0 || r.asksTotal != 0 || r.state != kLcWaiting) Fail("a spot not kept starts no visit and asks nothing");
        LostNote(&b, 11u, 1u, 1, 900.0f, 0.0f, 900.0f);   /* the owner's updates place it further in, the same sector */
        const int k = LostLookDecide(0, 1, LostSpotKept(1, 1), r);
        if (k != kLaAsk) Fail("the spot reads kept: ask");
        LostLookApply(&r, 1, k, 1);
        if (r.state != kLcAsked || r.asksThisVisit != 1 || r.loadedPrev != 1) Fail("the first kept look starts the visit and counts its ask");
    }
    /* back in its visit, put away again: while its spot reads kept no ask (the loop rule); a stretch not kept, then kept again in
       the same sector, is a new visit - the ask goes out, the visit's count and its came-back mark start again */
    {
        LostBook b;
        LostNote(&b, 12u, 1u, 1, 5.0f, 0.0f, 5.0f);
        LostRow& r = b.rows[0];
        int c = LostLookDecide(0, 1, 1, r);
        LostLookApply(&r, 1, c, 1);
        LostAnswerApply(&r, kRvSent);
        if (LostArrived(&r) != 1) Fail("the SPAWN makes the copy: BACK");
        c = LostLookDecide(1, 1, 1, r);
        LostLookApply(&r, 1, c, 0);
        if (c != kLaHere || r.backThisVisit != 1 || r.loadedPrev != 1) Fail("back on kept ground: HERE, the visit marked");
        /* HERE and the spot reads not kept (the owner moved it into the strip): the row goes */
        if (LostLookDecide(1, 1, LostSpotKept(1, 0), r) != kLaDrop) Fail("a HERE row whose spot is not kept: dropped");
        if (LostLookDecide(1, 1, LostSpotKept(1, 1), r) != kLaHere) Fail("a HERE row whose spot is kept: stays");
        r.asksThisVisit = kLostAsksPerVisit;
        LostNote(&b, 12u, 1u, 1, 100.0f, 0.0f, 100.0f);   /* lost again in its visit, the same sector */
        if (r.state != kLcWaiting || r.backThisVisit != 1 || r.loadedPrev != 1) Fail("lost again in the visit: WAITING, still marked");
        if (LostLookDecide(0, 1, LostSpotKept(1, 1), r) != kLaWait) Fail("lost again on kept ground in its visit: no ask");
        c = LostLookDecide(0, 1, LostSpotKept(1, 0), r);
        if (c != kLaWait) Fail("lost again in the edge strip: no ask");
        LostLookApply(&r, LostSpotKept(1, 0), c, 0);
        if (r.loadedPrev != 0) Fail("a look not kept ends the visit");
        LostNote(&b, 12u, 1u, 1, 900.0f, 0.0f, 900.0f);   /* further in, the same sector */
        c = LostLookDecide(0, 1, LostSpotKept(1, 1), r);
        if (c != kLaAsk) Fail("kept again after a stretch not kept: a new visit asks");
        LostLookApply(&r, 1, c, 1);
        if (r.asksThisVisit != 1 || r.backThisVisit != 0 || r.state != kLcAsked || r.asksTotal != 2) Fail("the new visit counts from 1 and clears the came-back mark");
    }
}
/* T-834: a copy that came back in this visit and is lost again in the same visit is not asked for while that visit lasts - every
   look waits, whatever the looks and ticks; a look whose spot is not kept ends the visit and the next visit asks. Pins the rule as it
   stands (lostcopy.h LostLookDecide). */
void t_t834_lost_again_waits()
{
    using namespace lostcopy;
    LostBook b;
    LostNote(&b, 21u, 1u, 1, 900.0f, 0.0f, 900.0f);
    LostRow& r = b.rows[0];
    int a = LostLookDecide(0, 1, 1, r);
    if (a != kLaAsk) Fail("a new visit asks");
    LostLookApply(&r, 1, a, 1, 1);
    LostAnswerApply(&r, kRvSent);
    if (LostArrived(&r) != 1) Fail("the SPAWN makes the copy: BACK");
    a = LostLookDecide(1, 1, 1, r); LostLookApply(&r, 1, a, 0, 1);
    if (a != kLaHere || r.backThisVisit != 1 || r.loadedPrev != 1) Fail("back in its visit: HERE, the visit marked");
    LostNote(&b, 21u, 1u, 1, 950.0f, 0.0f, 950.0f);   /* removed here again, the same sector */
    if (r.state != kLcWaiting || r.backThisVisit != 1 || r.loadedPrev != 1) Fail("lost again in the visit: WAITING, still marked");
    for (int i = 0; i < 600; ++i)
    {
        a = LostLookDecide(0, 1, 1, r);
        if (a != kLaWait) { Fail("lost again in the visit it came back in: no ask while that visit lasts"); break; }
        LostLookApply(&r, 1, a, 0, i % 2);
    }
    if (r.asksTotal != 1 || r.asksThisVisit != 1 || r.backThisVisit != 1 || r.state != kLcWaiting) Fail("the waits ask nothing and keep the mark");
    a = LostLookDecide(0, 1, 0, r); LostLookApply(&r, 0, a, 0, 1);   /* the spot not kept: the visit ends */
    if (a != kLaWait || r.loadedPrev != 0) Fail("a look not kept waits and ends the visit");
    a = LostLookDecide(0, 1, 1, r);
    if (a != kLaAsk) Fail("the next visit asks");
    LostLookApply(&r, 1, a, 1, 1);
    if (r.backThisVisit != 0 || r.asksThisVisit != 1 || r.asksTotal != 2 || r.state != kLcAsked) Fail("the new visit clears the mark and counts from 1");
}
/* T-735: an answer that sent nothing for a passing reason backs off and asks again within the visit's budget; NOT ANNOUNCED, NOT
   MINE and SENT wait for the next visit; an answer that never comes is taken as lost after kLostAnswerLooks once-a-second looks. */
void t_t735_lost_backoff()
{
    using namespace lostcopy;
    /* FAILS BEFORE: every answer but SENT parked the row for the rest of its visit, and an owed answer that never came waited for the
       visit to end - a character standing still on kept ground was never asked for again. */
    const int passing[5] = { kRvHeld, kRvNoBody, kRvFailed, kRvNoRoad, kRvNotInArea };
    for (int pi = 0; pi < 5; ++pi)
    {
        LostBook b;
        LostNote(&b, 40u, 2u, 1, 5.0f, 0.0f, 5.0f);
        LostRow& r = b.rows[0];
        int asks = 0;
        for (int look = 0; look < 200; ++look)
        {
            const int a = LostLookDecide(0, 1, 1, r);
            LostLookApply(&r, 1, a, a == kLaAsk ? 1 : 0, 1);
            if (a == kLaAsk) { ++asks; LostAnswerApply(&r, passing[pi]); }
        }
        if (asks != kLostAsksPerVisit || r.asksThisVisit != kLostAsksPerVisit) { Fail("a passing refusal is asked again within the visit's budget, and no further"); break; }
    }
    {
        LostBook b;
        LostNote(&b, 41u, 2u, 1, 5.0f, 0.0f, 5.0f);
        LostRow& r = b.rows[0];
        int a = LostLookDecide(0, 1, 1, r);
        LostLookApply(&r, 1, a, 1, 1);
        LostAnswerApply(&r, kRvHeld);
        if (r.state != kLcBackoff || r.waitLooks != 0) Fail("HELD: backing off from 0");
        for (int i = 0; i < kLostBackoffLooks; ++i)
        {
            if (LostLookDecide(0, 1, 1, r) != kLaWait) { Fail("asked again before the back-off ran out"); break; }
            LostLookApply(&r, 1, kLaWait, 0, 1);
        }
        if (LostLookDecide(0, 1, 1, r) != kLaAsk) Fail("not asked again when the back-off ran out");
        LostRow e = r; e.waitLooks = 0;
        for (int i = 0; i < 50; ++i) LostLookApply(&e, 1, kLaWait, 0, 0);
        if (LostLookDecide(0, 1, 1, e) != kLaWait) Fail("looks at MOVEs (not the once-a-second pass) ran the back-off down");
        LostRow h = r; h.backThisVisit = 1;
        if (LostLookDecide(0, 1, 1, h) != kLaWait) Fail("a back-off asked after the copy came back in its visit");
    }
    {
        const int park[3] = { kRvNotAnnounced, kRvNotMine, kRvSent };
        for (int pi = 0; pi < 3; ++pi)
        {
            LostBook b;
            LostNote(&b, 42u, 2u, 1, 5.0f, 0.0f, 5.0f);
            LostRow& r = b.rows[0];
            int a = LostLookDecide(0, 1, 1, r);
            LostLookApply(&r, 1, a, 1, 1);
            LostAnswerApply(&r, park[pi]);
            int asks = 0;
            for (int i = 0; i < 100; ++i) { a = LostLookDecide(0, 1, 1, r); if (a == kLaAsk) ++asks; LostLookApply(&r, 1, a, a == kLaAsk ? 1 : 0, 1); }
            if (asks != 0) { Fail("NOT ANNOUNCED / NOT MINE / SENT asked again in the same visit"); break; }
            a = LostLookDecide(0, 1, 0, r); LostLookApply(&r, 0, a, 0, 1);
            if (LostLookDecide(0, 1, 1, r) != kLaAsk) { Fail("the next visit did not ask"); break; }
        }
        if (LostAnswerParks(kRvNotAnnounced) != 1 || LostAnswerParks(kRvNotMine) != 1 || LostAnswerParks(kRvHeld) != 0 || LostAnswerParks(kRvNoRoad) != 0
            || LostAnswerParks(kRvNotInArea) != 0) Fail("the verdicts that wait for the next visit");
    }
    {
        LostBook b;
        LostNote(&b, 43u, 2u, 1, 5.0f, 0.0f, 5.0f);
        LostRow& r = b.rows[0];
        int asks = 0, secondAt = -1;
        for (int look = 0; look < 100; ++look)
        {
            const int a = LostLookDecide(0, 1, 1, r);
            if (a == kLaAsk) { ++asks; if (asks == 2) secondAt = look; }
            LostLookApply(&r, 1, a, a == kLaAsk ? 1 : 0, 1);
        }
        if (asks != kLostAsksPerVisit || secondAt != kLostAnswerLooks + 1) Fail("an unanswered ask is asked again after kLostAnswerLooks looks, within the budget");
        LostAnswerApply(&r, kRvSent);
        if (r.state != kLcAnswered) Fail("the answer to a repeated ask did not land");
    }
}
/* T-735: the lost-copy reads keep by the engine's own edge rule (160 units), not the hand-over's margin */
void t_t735_lost_keep_edge()
{
    /* FAILS BEFORE: the SPAWN gate and the book's look added the hand-over's 250-unit margin to the engine's 160, so a character
       standing 160..410 units from an unloaded side was booked and never asked for, though this game's engine keeps it there. */
    using namespace cooplo;
    const float sz = kAreaSizeUnits, minX = AreaMinUnits(19), minZ = AreaMinUnits(32), cz = minZ + sz * 0.5f, m = lostcopy::kLostKeepMarginUnits;
    if (m != 0.0f) Fail("the lost-copy margin is not the engine's own rule (0)");
    if (EngineKeepsAt(minX + 160.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 1) Fail("160 from an unloaded west side: not kept");
    if (EngineKeepsAt(minX + 159.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 0) Fail("159 from an unloaded west side: kept");
    if (EngineKeepsAt(minX + 300.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 1) Fail("300 from an unloaded side: not kept");
    if (EngineKeepsAt(minX + 300.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, kKeepMarginUnits) != 0) Fail("the hand-over's margin changed");
    if (lostcopy::SpawnKeepDecide(0, 0, lostcopy::LostSpawnSpotRead(1, 1, EngineKeepsAt(minX + 300.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m))) != lostcopy::kSkMake)
        Fail("a SPAWN 300 from an unloaded side was booked");
    if (lostcopy::SpawnKeepDecide(0, 0, lostcopy::LostSpawnSpotRead(1, 1, EngineKeepsAt(minX + 100.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m))) != lostcopy::kSkBook)
        Fail("a SPAWN 100 from an unloaded side was made");
}
/* T-735: the book holds 2048 rows; a full book makes room from the oldest HERE row first, else the oldest row */
void t_t735_lost_book_room()
{
    /* FAILS BEFORE: 256 rows, the oldest always evicted - booked SPAWNs filled the book and pushed out rows whose copy was not here */
    using namespace lostcopy;
    if (kLostBookMax < 2048) Fail("the book holds fewer than 2048 rows");
    LostBook b;
    LostNote(&b, 1u, 9u, 1, 0.0f, 0.0f, 0.0f);
    b.rows[0].state = kLcHere;
    for (unsigned int u = 2u; u <= (unsigned int)kLostBookMax; ++u) LostNote(&b, u, 9u, 1, 0.0f, 0.0f, 0.0f);
    if (b.rows.size() != kLostBookMax || b.full != 0) Fail("the book did not fill to its bound");
    LostNote(&b, 100000u, 9u, 1, 0.0f, 0.0f, 0.0f);
    if (b.rows.size() != kLostBookMax || b.full != 1 || LostFind(b, 1u) != -1 || LostFind(b, 2u) != 0) Fail("a full book did not make room from the HERE row");
    LostNote(&b, 100001u, 9u, 1, 0.0f, 0.0f, 0.0f);
    if (b.full != 2 || LostFind(b, 2u) != -1 || LostFind(b, 100001u) < 0) Fail("with no HERE row, the oldest did not make room");
    /* a withdrawal drops the withdrawing game's held MOVEs, not another sender's */
    cooplive::MoveHoldBook h;
    std::vector<char> mv(36, 0);
    cooplive::MoveHoldPut(&h, 5u, 7u, mv, 0.0);
    if (cooplive::MoveHoldWithdraw(&h, 5u, 8u) || h.rows.size() != 1u) Fail("another game's withdrawal dropped the hold");
    if (!cooplive::MoveHoldWithdraw(&h, 5u, 7u) || !h.rows.empty()) Fail("the streaming game's withdrawal left the hold");
    std::vector<cooplive::MoveHoldGone> gone;
    if (cooplive::MoveHoldExpire(&h, 100.0, cooplive::kMoveHoldSec, &gone) != 0 || !gone.empty()) Fail("a withdrawn hold expired and was booked");
}
/* The copy arrives by a SPAWN: HERE at once, BACK for every booked uid, and no ask after it in that visit. */
void t_t586_lost_arrive()
{
    using namespace lostcopy;
    /* the whole table: no copy here, the spot loaded - an ask goes only from a row that does not await an answer, at a new
       visit (whatever it awaits), or from a WAITING row not back in this visit within the budget */
    const int states[5] = { kLcWaiting, kLcAsked, kLcAnswered, kLcRefused, kLcHere };
    for (int si = 0; si < 5; ++si) for (int back = 0; back < 2; ++back) for (int prev = 0; prev < 2; ++prev) for (int k = 0; k <= kLostAsksPerVisit; ++k)
    {
        LostRow t; t.uid = 5u; t.ownerKey = 1u; t.x = t.y = t.z = 0.0f; t.hasPos = 1; t.state = states[si]; t.asksThisVisit = k;
        t.loadedPrev = prev; t.asksTotal = k; t.noRoad = 0; t.hereMoves = 0; t.backThisVisit = back;
        const int want = (!prev || (t.state == kLcWaiting && !back && k < kLostAsksPerVisit)) ? kLaAsk : kLaWait;
        if (LostLookDecide(0, 1, 1, t) != want) { Fail("the ask table: owed answers wait, new visits ask, a visit asks only from WAITING not back"); break; }
        if (LostLookDecide(1, 1, 1, t) == kLaAsk) { Fail("a copy here is never asked for"); break; }
    }
    /* T1027's sequence: asked, the SPAWN makes the copy, the answer arrives, the engine puts the copy away again at once */
    LostBook b;
    LostNote(&b, 122u, 7u, 1, 1.0f, 0.0f, 1.0f);
    LostRow& r = b.rows[0];
    int a = LostLookDecide(0, 1, 1, r); LostLookApply(&r, 1, a, 1);
    if (a != kLaAsk || r.state != kLcAsked) Fail("booked on loaded ground: asked once");
    if (LostArrived(&r) != 1 || r.state != kLcHere || r.backThisVisit != 1 || r.hereMoves != 0) Fail("the SPAWN's copy: HERE at once, BACK written");
    if (LostArrived(&r) != 0) Fail("a second SPAWN for a copy already here writes no second BACK");
    LostAnswerApply(&r, kRvSent);
    if (r.state != kLcHere) Fail("the answer after the SPAWN leaves the row HERE");
    if (LostLookDecide(1, 1, 1, r) != kLaHere) Fail("the look in the frame of the SPAWN sees the copy here");
    if (LostLookDecide(0, 1, 1, r) != kLaWait) Fail("the copy gone without a booking: no ask in this visit");
    LostNote(&b, 122u, 7u, 1, 1.0f, 0.0f, 1.0f);   /* the engine put it away again */
    if (LostLookDecide(0, 1, 1, r) != kLaWait || r.asksTotal != 1) Fail("put away again in the visit it came back in: no second ask");
    if (LostArrived(&r) != 1) Fail("back again: BACK again");
    /* a booked uid that comes back by its owner's ordinary SPAWN, never asked for: BACK all the same */
    LostNote(&b, 200u, 7u, 1, 1.0f, 0.0f, 1.0f);
    const int i = LostFind(b, 200u);
    if (i < 0 || LostArrived(&b.rows[(size_t)i]) != 1 || b.rows[(size_t)i].asksTotal != 0) Fail("BACK for a booked uid never asked for");
    if (LostArrived(0) != 0) Fail("no row, no BACK");
    /* an answer counts only for the row that awaits it */
    LostRow w; w.uid = 3u; w.ownerKey = 1u; w.x = w.y = w.z = 0.0f; w.hasPos = 1; w.state = kLcWaiting; w.asksThisVisit = 0;
    w.loadedPrev = 1; w.asksTotal = 0; w.noRoad = 0; w.hereMoves = 0; w.backThisVisit = 0;
    LostAnswerApply(&w, kRvSent);
    if (w.state != kLcWaiting) Fail("an answer for a row not ASKED changes nothing");
    w.state = kLcAsked; LostAnswerApply(&w, kRvNotAnnounced);
    if (w.state != kLcRefused) Fail("an ASKED row takes a refusal");
    /* the sector rule: sector = floor((147456 + c) / 4608) */
    if (LostSectorOf(-46120.0f) != 21 || LostSectorOf(-46080.0f) != 22 || LostSectorOf(-50978.0f) != 20 || LostSectorOf(2932.0f) != 32
        || LostSectorOf(0.0f) != 32 || LostSectorOf(-147456.0f) != 0 || LostSectorOf(-147457.0f) != -1) Fail("the sector of a coordinate");
    /* lost and back in one visit on the edge of the loaded area; the owner moves it into another sector: a new visit, asked again */
    LostBook e;
    LostNote(&e, 77u, 7u, 1, -46120.0f, 0.0f, 2932.0f);
    LostRow& q = e.rows[0];
    int a2 = LostLookDecide(0, 1, 1, q); LostLookApply(&q, 1, a2, 1);
    LostAnswerApply(&q, kRvSent);
    LostArrived(&q);
    LostNote(&e, 77u, 7u, 1, -46121.0f, 0.0f, 2932.0f);   /* put away again where it stood */
    if (q.loadedPrev != 1 || LostLookDecide(0, 1, 1, q) != kLaWait) Fail("the same sector: the same visit, no ask");
    LostNote(&e, 77u, 7u, 1, -50878.0f, 0.0f, 2932.0f);   /* the owner's MOVE in another sector */
    if (q.loadedPrev != 0) Fail("another sector: the next loaded look starts a new visit");
    a2 = LostLookDecide(0, 1, 1, q);
    if (a2 != kLaAsk) Fail("a new visit in another sector asks");
    LostLookApply(&q, 1, a2, 1);
    if (q.state != kLcAsked || q.backThisVisit != 0 || q.asksThisVisit != 1 || q.asksTotal != 2) Fail("the new visit's ask is counted from 1");
    LostNote(&e, 77u, 7u, 1, -46120.0f, 0.0f, 2932.0f);   /* moved to another sector while the ask is owed */
    if (LostLookDecide(0, 1, 1, q) != kLaAsk) Fail("an owed ask is asked again at the next visit (a lost answer is covered)");
    LostLookApply(&q, 1, kLaAsk, 1);
    if (LostLookDecide(0, 1, 1, q) != kLaWait) Fail("inside that visit the owed ask waits");
    LostNote(&e, 77u, 7u, 0, 0.0f, 0.0f, 0.0f);
    if (q.x != -46120.0f || q.hasPos != 1) Fail("a booking with no spot keeps the spot");
}
void t_t586_stale_row()
{
    using namespace lostcopy;
    /* which creation refusals keep a uid from being booked: only those a re-send would meet again */
    if (CreateRefusalLasting(kCrNone) != 0 || CreateRefusalLasting(kCrNoTemplate) != 1 || CreateRefusalLasting(kCrFactionUnknown) != 1
        || CreateRefusalLasting(kCrTableTestCap) != 1 || CreateRefusalLasting(99) != 0) Fail("lasting: template, faction not in the data, the test cap - nothing else");
    if (WireFactionMayAppear("@slot:1:Nameless") != 1 || WireFactionMayAppear("@player:x") != 1 || WireFactionMayAppear("Holy Nation") != 0
        || WireFactionMayAppear("") != 0 || WireFactionMayAppear(0) != 0) Fail("a player faction (\"@...\") may appear later; a data faction does not");
    for (int k = 0; k < 64; ++k)
    {
        const int asked = k & 1, m = (k >> 1) & 1, ret = (k >> 2) & 1, res = (k >> 3) & 1, run = (k >> 4) & 1, ld = (k >> 5) & 1;
        const int want = (asked && !m && ret && !res && !run && ld) ? 1 : 0;
        if (StaleRowGivesWay(asked, m, ret, res, run, ld) != want)
        { Fail("a row gives way only for an asked SPAWN of another game's uid, retired, its handle not resolving, not running, the spot loaded"); return; }
    }
}

/* T-601 D1: a wake-up clock held with a parked knockdown goes on less the time it waited, never below 0; a value ApplyLatch
   does not write (no clock) comes back unchanged. */
/* T-595: a knock-out pose onto a standing, non-ragdoll copy whose STATE carries the unconscious flag or clock is left to the
   copy's own engine once; a ragdoll, a limp or an unreadable copy, a second time, no latch, or another pose is written. */
void t_t595_ko_pose_left_to_engine()
{
    using namespace coopkolook;
    if (!LeaveKoPoseToEngine(4, true, 0, 0, false)) Fail("T-595: standing, latch, first time -> left to the engine");
    if (LeaveKoPoseToEngine(4, true, 0, 0, true)) Fail("T-595: left once and still standing -> the next STATE writes it");
    if (LeaveKoPoseToEngine(4, false, 0, 0, false)) Fail("T-595: no unconscious flag or clock -> written");
    if (LeaveKoPoseToEngine(4, true, 1, 0, false)) Fail("T-595: already a ragdoll -> written");
    if (LeaveKoPoseToEngine(4, true, 0, 1, false)) Fail("T-595: already limp -> written");
    if (LeaveKoPoseToEngine(4, true, -1, 0, false)) Fail("T-595: ragdoll unreadable -> written");
    if (LeaveKoPoseToEngine(2, true, 0, 0, false)) Fail("T-595: a crippled pose (2) -> written");
    if (LeaveKoPoseToEngine(0, true, 0, 0, false)) Fail("T-595: standing up (0) -> written");
}

void t_t601_held_ko_timer()
{
    using namespace coopkolook;
    if (HeldKoTimerLeft(37.0f, 0) != 37.0f) Fail("T-601: no wait -> the owner's clock as sent");
    const float a = HeldKoTimerLeft(37.0f, 140);
    if (a < 36.859f || a > 36.861f) Fail("T-601: 140 ms held -> 36.86 s left");
    if (HeldKoTimerLeft(37.0f, 37000) != 0.0f) Fail("T-601: held exactly the clock -> 0");
    if (HeldKoTimerLeft(2.0f, 5000) != 0.0f) Fail("T-601: held longer than the clock -> 0, never below");
    if (HeldKoTimerLeft(0.0f, 1000) != 0.0f) Fail("T-601: no clock running (0) stays 0");
    if (HeldKoTimerLeft(-1.0f, 1000) != -1.0f) Fail("T-601: a negative clock comes back unchanged (ApplyLatch does not write it)");
    if (HeldKoTimerLeft(3600.0f, 1000) != 3600.0f || HeldKoTimerLeft(9999.0f, 1000) != 9999.0f)
        Fail("T-601: a clock at or past 3600 comes back unchanged (ApplyLatch does not write it)");
    const float b = HeldKoTimerLeft(3599.0f, 1000);
    if (b < 3597.99f || b > 3598.01f) Fail("T-601: the top of the written range is reduced too");
    float prev = 37.0f;
    for (unsigned long ms = 0; ms <= 40000; ms += 250)
    {
        const float v = HeldKoTimerLeft(37.0f, ms);
        if (v < 0.0f || v > prev) { Fail("T-601: the clock left never rises with the wait and never goes below 0"); return; }
        prev = v;
    }
}

/* T-619: a town's table survives the wire exactly; a cut, a trailing byte, another tag, an empty or bad row is refused whole */
void t_t619_town_table_wire()
{
    townprice::Table t; t.gen = 7; t.town = "1082-gamedata.base";
    t.rows.push_back(std::make_pair(std::string("1230-gamedata.base"), 0.9912f));
    t.rows.push_back(std::make_pair(std::string("579-gamedata.base"), 1.0475f));
    std::vector<char> b;
    if (townprice::Encode(&b, t) != 1) { Fail("a two-row town table encodes"); return; }
    townprice::Table d;
    if (townprice::Decode(&b[0], b.size(), &d) != 1 || d.gen != 7 || d.town != t.town || d.rows.size() != 2
        || d.rows[0].first != "1230-gamedata.base" || d.rows[0].second != 0.9912f || d.rows[1].second != 1.0475f) Fail("a town table round-trips exactly");
    { std::vector<char> c(b); c.push_back(0); if (townprice::Decode(&c[0], c.size(), &d) != 0) Fail("a trailing byte is refused"); }
    { std::vector<char> c(b.begin(), b.end() - 1); if (townprice::Decode(&c[0], c.size(), &d) != 0) Fail("a cut table is refused"); }
    { std::vector<char> c(b); c[0] = 'X'; if (townprice::Decode(&c[0], c.size(), &d) != 0) Fail("another tag is refused"); }
    { townprice::Table e; e.town = "x"; std::vector<char> c; if (townprice::Encode(&c, e) != 0 || !c.empty()) Fail("a table with no rows is not encoded"); }
    { townprice::Table e(t); e.rows[1].second = 0.0f; std::vector<char> c; if (townprice::Encode(&c, e) != 0 || !c.empty()) Fail("a zero multiplier is not encoded"); }
    { townprice::Table e(t); e.town = ""; std::vector<char> c; if (townprice::Encode(&c, e) != 0) Fail("a table with no town is not encoded"); }
    { std::vector<char> c(b); const size_t at = c.size() - 4; const float z = 0.0f; std::memcpy(&c[at], &z, 4); if (townprice::Decode(&c[0], c.size(), &d) != 0) Fail("a zero multiplier on the wire is refused"); }
}
/* T-619: a game that is not the source answers with the source's number only when the source's table for that town names the item; a
   new world of the sender, or another sender, replaces the whole book (two sources' tables never merge); Clear empties it */
void t_t619_town_table_choice()
{
    if (townprice::Choose(1, 1) != townprice::kUseSource) Fail("town and item in the source's table: the source's number");
    if (townprice::Choose(1, 0) != townprice::kUseEngine) Fail("the item missing from the source's town table: the engine's own");
    if (townprice::Choose(0, 0) != townprice::kUseEngine) Fail("no source table for the town: the engine's own");
    townprice::Book k;
    townprice::Table a; a.gen = 3; a.town = "A"; a.rows.push_back(std::make_pair(std::string("i1"), 1.25f));
    townprice::Table b; b.gen = 3; b.town = "B"; b.rows.push_back(std::make_pair(std::string("i2"), 0.8f));
    if (k.Apply(5, a) != 1 || k.Apply(5, b) != 0 || k.Towns() != 2 || k.Sender() != 5) Fail("two towns of one sender's world share one book");
    int ht = 0; float v = 0.0f;
    if (k.Lookup("A", "i1", &ht, &v) != 1 || ht != 1 || v != 1.25f) Fail("a listed item answers with the source's number");
    if (k.Lookup("A", "i2", &ht, &v) != 0 || ht != 1) Fail("an item the town's table lacks: the town is known, the item is not");
    if (k.Lookup("C", "i1", &ht, &v) != 0 || ht != 0) Fail("a town the source did not send is unknown");
    townprice::Table a2(a); a2.gen = 4; a2.rows[0].second = 0.75f;
    const unsigned long v0 = k.Version();
    if (k.Apply(5, a2) != 1 || k.Towns() != 1 || k.Gen() != 4 || k.Version() == v0) Fail("a new world of the sender replaces the whole book");
    if (k.Lookup("B", "i2", &ht, &v) != 0 || ht != 0) Fail("a town of the sender's older world is gone");
    if (k.Lookup("A", "i1", &ht, &v) != 1 || v != 0.75f) Fail("the sender's new world's number answers");
    townprice::Table c; c.gen = 4; c.town = "C"; c.rows.push_back(std::make_pair(std::string("i1"), 2.0f));
    if (k.Apply(7, c) != 1 || k.Towns() != 1 || k.Sender() != 7) Fail("another sender's table of the same world number replaces the book");
    if (k.Lookup("A", "i1", &ht, &v) != 0 || ht != 0) Fail("two senders' tables never merge");
    k.Clear();
    if (k.Have() != 0 || k.Towns() != 0 || k.Lookup("C", "i1", &ht, &v) != 0 || ht != 0) Fail("a cleared book answers nothing");
}
/* T-619: one price source per world by one rule every game shares - the lowest IN_WORLD roster slot whenever a roster is read, the
   session host only with no roster - and which received table is taken (only the source's; the source takes none) */
void t_t619_price_source()
{
    /* session play only (no roster): the host is the source; the joiner takes the host's tables from the link */
    if (townprice::PriceSource(0, -1, -1, 1, 1) != 1 || townprice::PriceSource(0, -1, -1, 1, 0) != 0) Fail("session only: the host is the source, the joiner is not");
    if (townprice::PriceSource(0, -1, -1, 0, 1) != 0) Fail("no roster and no link: no source");
    const long sj = townprice::SourceKey(0, -1, 1, 0, 9);
    if (sj != townprice::SessionKey(9) || townprice::SourceKey(0, -1, 1, 1, 9) != townprice::kSrcThisGame || townprice::SourceKey(0, -1, 0, 0, 9) != townprice::kSrcNone)
        Fail("session only: the source keys");
    if (townprice::TakeFrom(0, sj, townprice::SenderKey(0, 0, -1, -1, 9), 0, 1, -1) != townprice::kTake) Fail("session only: the joiner takes the host's link table");
    /* re-check 2: no roster, link up, this game the joiner and its partner's slot known - the host's link table has the session key */
    if (townprice::SenderKey(0, 0, -1, 3, 9) != sj || townprice::TakeFrom(0, sj, townprice::SenderKey(0, 0, -1, 3, 9), 0, 1, 3) != townprice::kTake)
        Fail("session only: with the partner's slot known the joiner still takes the host's link table");
    if (townprice::SenderKey(1, 0, -1, 3, 9) != 3 || townprice::SenderKey(1, 0, -1, -1, 9) != townprice::SessionKey(9) || townprice::SenderKey(0, 1, 2, 3, 9) != 2)
        Fail("with a roster a link table has the partner's slot; a relayed one its origin slot either way");
    if (townprice::TakeFrom(0, sj, townprice::SenderKey(0, 0, -1, -1, 8), 0, 1, -1) != townprice::kTakeNotSource) Fail("a table of an earlier session link is not the source's");
    if (townprice::TakeFrom(1, townprice::kSrcThisGame, townprice::SenderKey(0, 0, -1, -1, 9), 0, 1, -1) != townprice::kTakeOnSource) Fail("session only: the host takes none");
    /* world door only (a roster, no session link): the lowest IN_WORLD slot */
    if (townprice::PriceSource(1, 0, 0, 0, 0) != 1 || townprice::PriceSource(1, 0, 2, 0, 0) != 0) Fail("world door only: the lowest IN_WORLD slot is the source");
    if (townprice::PriceSource(1, -1, 0, 0, 0) != 0 || townprice::PriceSource(1, 0, -1, 0, 0) != 0) Fail("no IN_WORLD row, or no slot of this game's own: not the source");
    if (townprice::SourceKey(1, 0, 0, 0, 9) != 0 || townprice::SourceKey(1, -1, 0, 0, 9) != townprice::kSrcNone) Fail("world door only: the source key is the slot");
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 1, 0, -1, 9), 1, 0, -1) != townprice::kTake) Fail("world door only: a relayed table from the lowest slot is taken");
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 1, 2, -1, 9), 1, 0, -1) != townprice::kTakeNotSource) Fail("world door only: a relayed table from another slot is refused");
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 1, -1, -1, 9), 1, 0, -1) != townprice::kTakeNotSource) Fail("a relayed table with no origin slot is refused");
    if (townprice::TakeFrom(1, 0, townprice::SenderKey(1, 1, 2, -1, 9), 1, 0, -1) != townprice::kTakeOnSource) Fail("world door only: the source takes none");
    /* mixed: C (slot 0) through the world door only, A (slot 1) the session host, B (slot 2) its session joiner - C is THE source for all three */
    if (townprice::PriceSource(1, 0, 0, 0, 0) != 1) Fail("mixed: C, the lowest slot, is the source");
    if (townprice::PriceSource(1, 0, 1, 1, 1) != 0) Fail("mixed: the session host is not the source when a lower world-door slot is in the world");
    if (townprice::PriceSource(1, 0, 2, 1, 0) != 0) Fail("mixed: the session joiner is not the source");
    const long ka = townprice::SourceKey(1, 0, 1, 1, 9), kb = townprice::SourceKey(1, 0, 1, 0, 9);
    if (ka != 0 || kb != 0) Fail("mixed: A and B both name slot 0");
    if (townprice::TakeFrom(0, ka, townprice::SenderKey(1, 1, 0, 2, 9), 1, 1, 2) != townprice::kTake) Fail("mixed: A takes C's relayed table");
    if (townprice::TakeFrom(0, kb, townprice::SenderKey(1, 1, 0, 1, 9), 1, 1, 1) != townprice::kTake) Fail("mixed: B takes C's relayed table");
    if (townprice::TakeFrom(0, kb, townprice::SenderKey(1, 0, -1, 1, 9), 0, 1, 1) != townprice::kTakeNotSource) Fail("mixed: B refuses the session host's link table");
    if (townprice::TakeFrom(0, kb, townprice::SenderKey(1, 0, -1, -1, 9), 0, 1, -1) != townprice::kTakeNotSource) Fail("mixed: B refuses a link table from an unslotted peer");
    /* the source is a session member: A (slot 0) hosts B (slot 1), C (slot 2) comes by the world door. B takes A's table from the
       link (a relayed copy from A is the other road); C takes A's relayed table */
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 0, -1, 0, 9), 0, 1, 0) != townprice::kTake) Fail("B takes the source's link table");
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 1, 0, 0, 9), 1, 1, 0) != townprice::kTakeOtherRoad) Fail("B counts a relayed copy from its session peer as the other road");
    if (townprice::TakeFrom(0, 0, townprice::SenderKey(1, 1, 0, -1, 9), 1, 0, -1) != townprice::kTake) Fail("C takes the source's relayed table");
    /* the same table twice replaces, never adds */
    townprice::Book k;
    townprice::Table a; a.gen = 2; a.town = "A"; a.rows.push_back(std::make_pair(std::string("i1"), 1.5f));
    if (k.Apply(1, a) != 1 || k.Apply(1, a) != 0 || k.Towns() != 1) Fail("the same table twice is one town");
    int ht = 0; float v = 0.0f;
    if (k.Lookup("A", "i1", &ht, &v) != 1 || v != 1.5f) Fail("the same table twice answers its one number");
}
/* T-619 re-check 3/4: each road of the source's send is due once per (epoch, world); a failed road is re-attempted only on its own
   next ready edge (never after frames), at most kRoadTries times, and a new (epoch, world) or Clear starts it afresh */
void t_t619_road()
{
    townprice::Road r;
    if (r.Due(0, 1, 2, 7) != 0) Fail("road: a road that cannot carry the send is not due");
    if (r.Due(1, 1, 2, 7) != 1 || r.Retrying() != 0) Fail("road: a ready road is due");
    if (r.Result(0, 7) != townprice::kRoadSent || r.Due(1, 1, 2, 8) != 0) Fail("road: every table went - not due again for that (epoch, world), even on a new edge");
    if (r.Due(1, 2, 2, 8) != 1) Fail("road: a new epoch makes the road due again");
    if (r.Result(3, 8) != townprice::kRoadWait) Fail("road: a failed send waits");
    for (int f = 0; f < 50; ++f) if (r.Due(1, 2, 2, 8) != 0) { Fail("road: no re-attempt before the road's next ready edge, however many frames pass"); return; }
    long long e = 9;
    int waits = 0;
    for (;;)
    {
        if (r.Due(1, 2, 2, e) != 1 || r.Retrying() != 1) { Fail("road: each new ready edge re-arms the failed road"); return; }
        const int res = r.Result(1, e);
        ++e;
        if (res == townprice::kRoadGaveUp) break;
        if (res != townprice::kRoadWait || ++waits > townprice::kRoadTries) { Fail("road: the re-attempts are bounded"); return; }
    }
    if (waits != townprice::kRoadTries - 1 || r.Due(1, 2, 2, e + 5) != 0) Fail("road: kRoadTries re-attempts, then it gives up for that (epoch, world)");
    if (r.Due(1, 3, 2, e) != 1 || r.Tries() != 0) Fail("road: a new (epoch, world) starts afresh");
    r.Result(1, e);
    r.Clear();
    if (r.Due(1, 3, 2, e) != 1 || r.Retrying() != 0) Fail("road: Clear makes the road due again");
    townprice::Road sr, lr;
    sr.Due(1, 1, 1, 0); lr.Due(1, 1, 1, 0);
    sr.Result(2, 0); lr.Result(0, 0);
    if (sr.Due(1, 1, 1, 0) != 0 || lr.Due(1, 1, 1, 0) != 0 || sr.Due(1, 1, 1, 1) != 1 || lr.Due(1, 1, 1, 1) != 0)
        Fail("road: only the failed road is re-attempted, on its own edge");
}
/* T-619: a table refused while this game names another source (or none) is kept - the last one per (sender, town), the sender's newest
   world only, at most kKeptCap - and handed back once the source answer names its sender, for the book */
void t_t619_kept()
{
    if (townprice::KeepRefused(townprice::kTakeNotSource, 0) != 1) Fail("kept: a table refused as not the source's is kept");
    if (townprice::KeepRefused(townprice::kTake, 0) != 0 || townprice::KeepRefused(townprice::kTakeOtherRoad, 0) != 0) Fail("kept: a taken or other-road table is not kept");
    if (townprice::KeepRefused(townprice::kTakeOnSource, 0) != 1 || townprice::KeepRefused(townprice::kTakeOnSource, townprice::kSrcNone) != 0)
        Fail("kept: a table that reached the source is kept when its sender has a key");
    {   /* re-check 1: slot 1's tables reach this game (slot 0) one frame before the source answer moves to slot 1 (slot 0 left the
           world): refused as having reached the source, kept, and filed when the source becomes their sender */
        const long s1 = townprice::SenderKey(1, 1, 1, -1, 9);
        const int tk = townprice::TakeFrom(1, townprice::SourceKey(1, 0, 0, 0, 9), s1, 1, 0, -1);
        townprice::Kept ko;
        townprice::Table x; x.gen = 3; x.town = "X"; x.rows.push_back(std::make_pair(std::string("i1"), 1.25f));
        std::vector<townprice::Table> got;
        if (tk != townprice::kTakeOnSource || townprice::KeepRefused(tk, s1) != 1 || ko.Keep(s1, x) != 1) Fail("kept: a lower slot's table on the source is kept");
        if (ko.TakeFor(townprice::SourceKey(1, 1, 0, 0, 9), &got) != 1 || got[0].town != "X") Fail("kept: filed when the source changes to its sender");
    }
    if (townprice::KeepRefused(townprice::kTakeNotSource, townprice::kSrcNone) != 0) Fail("kept: a table with no sender key is not kept");
    /* C's source answer is still unknown when the lowest slot's tables arrive relayed: refused and kept; then it names slot 0 and
       the kept tables are filed in its book */
    const long s0 = townprice::SenderKey(1, 1, 0, -1, 9);
    const int take = townprice::TakeFrom(0, townprice::kSrcNone, s0, 1, 0, -1);
    if (take != townprice::kTakeNotSource) Fail("kept: with no source named the table is refused");
    townprice::Kept kp;
    townprice::Table a; a.gen = 4; a.town = "A"; a.rows.push_back(std::make_pair(std::string("i1"), 1.5f));
    townprice::Table b; b.gen = 4; b.town = "B"; b.rows.push_back(std::make_pair(std::string("i2"), 2.0f));
    townprice::Table a2 = a; a2.rows[0].second = 1.75f;
    if (townprice::KeepRefused(take, s0) != 1 || kp.Keep(s0, a) != 1 || kp.Keep(s0, b) != 1 || kp.Keep(s0, a2) != 1) Fail("kept: three tables kept");
    if (kp.Size() != 2) Fail("kept: one table per (sender, town) - the later replaces the earlier");
    townprice::Table c; c.gen = 1; c.town = "A"; c.rows.push_back(std::make_pair(std::string("i1"), 3.0f));
    if (kp.Keep(2, c) != 1 || kp.Size() != 3) Fail("kept: another sender's table of the same town is kept beside it");
    std::vector<townprice::Table> out;
    if (kp.TakeFor(townprice::kSrcNone, &out) != 0 || kp.TakeFor(townprice::kSrcThisGame, &out) != 0 || kp.TakeFor(1, &out) != 0)
        Fail("kept: no source, this game, or a sender with nothing kept hands back nothing");
    if (kp.TakeFor(townprice::SourceKey(1, 0, 0, 0, 9), &out) != 2 || kp.Size() != 1) Fail("kept: the named source's two tables are handed back and forgotten");
    townprice::Book bk;
    for (size_t i = 0; i < out.size(); ++i) bk.Apply(s0, out[i]);
    int ht = 0; float v = 0.0f;
    if (bk.Lookup("A", "i1", &ht, &v) != 1 || v != 1.75f) Fail("kept: the filed book answers town A's last table");
    if (bk.Lookup("B", "i2", &ht, &v) != 1 || v != 2.0f) Fail("kept: the filed book answers town B");
    if (kp.TakeFor(s0, &out) != 0) Fail("kept: filed once - nothing left for the same sender");
    /* a table of the sender's newer world drops its older kept tables */
    townprice::Table a5 = a; a5.gen = 5;
    kp.Keep(s0, a); kp.Keep(s0, b);
    if (kp.Keep(s0, a5) != 1 || kp.TakeFor(s0, &out) != 1 || out[0].gen != 5) Fail("kept: a table of the sender's newer world drops its older ones");
    kp.Clear();
    if (kp.Size() != 0 || kp.TakeFor(2, &out) != 0) Fail("kept: Clear forgets every table");
    /* bounded: at most kKeptCap tables; a replacement still goes in at the cap */
    townprice::Table t; t.gen = 1;
    for (size_t i = 0; i < townprice::kKeptCap; ++i)
    {
        char nb[32]; std::sprintf(nb, "T%u", (unsigned)i); t.town = nb;
        if (kp.Keep(7, t) != 1) { Fail("kept: under the cap a table is kept"); return; }
    }
    t.town = "beyond";
    if (kp.Keep(7, t) != 0 || kp.Size() != townprice::kKeptCap) Fail("kept: a new (sender, town) beyond the cap is refused");
    t.town = "T0";
    if (kp.Keep(7, t) != 1 || kp.Size() != townprice::kKeptCap) Fail("kept: at the cap a table replaces its own town's");
}
/* T-619: the hook asks the book only on a game that is not the source, with its world running and a book held - the source ignores
   its own book */
void t_t619_hook_uses_book()
{
    if (townprice::HookUsesBook(0, 0, 1) != 1) Fail("a running game that is not the source asks its book");
    if (townprice::HookUsesBook(1, 0, 1) != 0) Fail("the source ignores its own book");
    if (townprice::HookUsesBook(0, 1, 1) != 0) Fail("with the world loading or tearing down the engine's own number answers");
    if (townprice::HookUsesBook(0, 0, 0) != 0) Fail("no book: the engine's own number");
}
/* T-619: the holder's price-leg test - equal legs agree, a culture of x5 against x1 disagrees with the price it gives, a rounding-sized
   difference agrees, an unread leg is unread */
void t_t619_price_legs()
{
    int e = 0;
    if (townprice::LegsVerdict(21, 1.0f, 1.0475f, 1.0f, 1.0475f, &e) != townprice::kLegsAgree || e != 21) Fail("equal legs agree");
    if (townprice::LegsVerdict(21, 1.0f, 1.0475f, 5.0f, 0.9912f, &e) != townprice::kLegsDisagree || e != 99) Fail("x5 culture here against x1 asked disagrees (99 cats)");
    if (townprice::LegsVerdict(21, 1.0f, 1.0f, 1.0f, 1.04f, &e) != townprice::kLegsAgree) Fail("a difference under one cat is rounding");
    if (townprice::LegsVerdict(642, 1.0f, 1.0f, 1.0f, 1.01f, &e) != townprice::kLegsDisagree || e != 648) Fail("six cats on 642 is beyond rounding");
    if (townprice::LegsVerdict(21, 0.0f, 1.0f, 1.0f, 1.0f, &e) != townprice::kLegsUnread || e != -1) Fail("an unsent leg is unread");
    if (townprice::LegsVerdict(-1, 1.0f, 1.0f, 1.0f, 1.0f, &e) != townprice::kLegsUnread) Fail("an unread price is unread");
}
/* T-619: a shop request with its two legs is an 'SHR4' block and round-trips them; without legs, or with a leg that is NaN or out of
   range, it is the 'SHR3' block (the trade goes on); a cut SHR4, or one with a NaN or out-of-range leg, is refused whole on the wire */
void t_t619_shop_req_legs()
{
    coopshop::ShopReq r; r.has = 1; r.keeperUid = 84; r.homeKey = "3384-TwoStorey.mod@21,31@-505887,-12252,10734"; r.sid = "1230-gamedata.base";
    r.n = 3; r.unitPrice = 99; r.entry = 0; r.epoch = 5; r.stock = coopshop::kStockHome; r.cult = 5.0f; r.local = 0.9912f;
    std::vector<char> b;
    if (coopshop::EncodeReq(&b, r) != 1) { Fail("a request with legs encodes"); return; }
    if (coopshop::ReqTagAt(&b[0], b.size(), 0) != 1 || coopshop::TagAt(&b[0], b.size(), 0, coopshop::kReqTagV4) != 1) Fail("a request with legs is an SHR4 block");
    coopshop::ShopReq d; size_t at = 0;
    if (coopshop::DecodeReq(&b[0], b.size(), &at, &d) != 1 || at != b.size() || d.cult != 5.0f || d.local != 0.9912f || d.unitPrice != 99 || d.stock != coopshop::kStockHome)
        Fail("an SHR4 block round-trips its legs");
    coopshop::ShopReq r3(r); r3.cult = 0.0f; r3.local = 0.0f;
    std::vector<char> b3;
    if (coopshop::EncodeReq(&b3, r3) != 1 || coopshop::TagAt(&b3[0], b3.size(), 0, coopshop::kReqTag) != 1 || b3.size() + 8 != b.size()) Fail("a request without legs is the SHR3 block");
    coopshop::ShopReq d3; d3.cult = 7.0f; at = 0;
    if (coopshop::DecodeReq(&b3[0], b3.size(), &at, &d3) != 1 || d3.cult != 0.0f || d3.local != 0.0f) Fail("an SHR3 block decodes with no legs");
    coopshop::ShopReq bad(r); bad.cult = -1.0f;
    std::vector<char> bb;
    if (coopshop::EncodeReq(&bb, bad) != 1 || coopshop::TagAt(&bb[0], bb.size(), 0, coopshop::kReqTag) != 1 || bb.size() != b3.size())
        Fail("a negative leg falls back to the SHR3 block - the trade goes on");
    { coopshop::ShopReq nl(r); const unsigned int nb = 0x7FC00000u; std::memcpy(&nl.local, &nb, 4); std::vector<char> x;
      if (coopshop::EncodeReq(&x, nl) != 1 || coopshop::TagAt(&x[0], x.size(), 0, coopshop::kReqTag) != 1) Fail("a NaN leg falls back to the SHR3 block"); }
    { coopshop::ShopReq big(r); big.local = 100.0f; std::vector<char> x;
      if (coopshop::EncodeReq(&x, big) != 1 || coopshop::TagAt(&x[0], x.size(), 0, coopshop::kReqTag) != 1) Fail("a leg of 100 or more falls back to the SHR3 block"); }
    { std::vector<char> x(b.begin(), b.end() - 1); coopshop::ShopReq y; size_t ya = 0; if (coopshop::DecodeReq(&x[0], x.size(), &ya, &y) != 0) Fail("a cut SHR4 block is refused"); }
    { std::vector<char> x(b.begin(), b.end() - 5); coopshop::ShopReq y; size_t ya = 0; if (coopshop::DecodeReq(&x[0], x.size(), &ya, &y) != 0) Fail("an SHR4 block cut inside its legs is refused"); }
    { std::vector<char> x(b); const unsigned int nb = 0x7FC00000u; std::memcpy(&x[x.size() - 8], &nb, 4); coopshop::ShopReq y; size_t ya = 0;
      if (coopshop::DecodeReq(&x[0], x.size(), &ya, &y) != 0) Fail("a NaN culture leg on the wire is refused whole"); }
    { std::vector<char> x(b); const float z = 0.0f; std::memcpy(&x[x.size() - 4], &z, 4); coopshop::ShopReq y; size_t ya = 0;
      if (coopshop::DecodeReq(&x[0], x.size(), &ya, &y) != 0) Fail("a zero local leg on the wire is refused whole"); }
    { std::vector<char> x(b); const float h = 150.0f; std::memcpy(&x[x.size() - 4], &h, 4); coopshop::ShopReq y; size_t ya = 0;
      if (coopshop::DecodeReq(&x[0], x.size(), &ya, &y) != 0) Fail("a local leg of 150 on the wire is refused whole"); }
}

/* ONE WRITER RULE FOR A WORLD NPC SQUAD (NpcSquadWriter), the world's version (NpcCopyFresh / NpcRecordOtherNewer), the wake's skipped
   load (NpcWakeSkipLoad), the stamp forgotten only for a copy the orphan purge emptied (NpcPurgeForgetStamp), "another game's file" by
   block (OtherGameFile) and an own-block record left asleep in another game's area (OwnBlockRecvHold) - every branch, then the reported
   chain. */
void t_t632_npc_squad_writer()
{
    using namespace coopsquad;
    /* npcSquad, livePos, mineHeld, heldByOther, mine, releasedHere, otherNewer */
    if (NpcSquadWriter(1, 1, 1, 0, 3, 0, 0) != kNpcWriteHolder) Fail("I hold the area and run the people: written, whatever block numbered the squad");
    if (NpcSquadWriter(1, 1, 1, -1, 1, 0, 0) != kNpcWriteHolder) Fail("I hold the area (the other answer unknown) and run one of the people: written");
    if (NpcSquadWriter(1, 1, 1, 0, 0, 0, 0) != kNpcWriteToday) Fail("I hold the area but run none of the people: the block and member rules decide");
    if (NpcSquadWriter(1, 1, 0, 1, 0, 0, 0) != kNpcWriteRefuseStray) Fail("another game holds the area and none of the people is mine: never written (a stray copy)");
    if (NpcSquadWriter(1, 1, -1, 1, 0, 0, 0) != kNpcWriteRefuseStray) Fail("another game holds the area (my own answer unknown), none mine: never written");
    if (NpcSquadWriter(1, 1, 0, 1, 2, 0, 0) != kNpcWriteToday) Fail("another game holds the area but I run some of the people: today's rules");
    if (NpcSquadWriter(1, 1, 0, 0, 0, 0, 0) != kNpcWriteToday) Fail("nobody holds the area, none mine: today's rules");
    if (NpcSquadWriter(1, 1, 0, 0, 4, 0, 0) != kNpcWriteToday) Fail("nobody holds the area, all mine: today's rules");
    if (NpcSquadWriter(1, 1, -1, -1, 0, 0, 0) != kNpcWriteToday) Fail("no fresh map, none mine: today's rules");
    if (NpcSquadWriter(1, 1, -1, -1, 3, 0, 0) != kNpcWriteToday) Fail("no fresh map, all mine: today's rules");
    if (NpcSquadWriter(1, 0, 1, 0, 3, 0, 0) != kNpcWriteToday) Fail("no member position read: today's rules (not the holder's write)");
    if (NpcSquadWriter(1, 0, 0, 1, 0, 0, 1) != kNpcWriteToday) Fail("no member position read: today's rules (not a stray refusal)");
    if (NpcSquadWriter(0, 1, 1, 0, 3, 0, 0) != kNpcWriteToday) Fail("not a world NPC squad (player / peer / stand-in / context / zone), held by me: today's rules");
    if (NpcSquadWriter(0, 1, 0, 1, 0, 0, 1) != kNpcWriteToday) Fail("not a world NPC squad, held by another, none mine: today's rules");
    /* people released in place here */
    if (NpcSquadWriter(1, 1, 0, 1, 0, 2, 0) != kNpcWriteToday) Fail("another game holds the area, none mine, people released in place here and no newer record of another game: not a stray - the block rules decide");
    if (NpcSquadWriter(1, 1, 0, 1, 0, 2, 1) != kNpcWriteRefuseStray) Fail("people released in place here, but another game's record is newer than this copy: that game writes it - a stray here");
    if (NpcSquadWriter(1, 1, 0, 1, 0, 0, 1) != kNpcWriteRefuseStray) Fail("none released here, another game's newer record: a stray");
    if (NpcSquadWriter(1, 1, 1, 0, 2, 1, 1) != kNpcWriteHolder) Fail("the holder running the people: written by the writer rule (the freshness rule is asked next)");
    { int holder = 0, stray = 0;
      for (int n = 0; n <= 1; ++n) for (int lp = 0; lp <= 1; ++lp) for (int mh = -1; mh <= 1; ++mh) for (int ho = -1; ho <= 1; ++ho) for (int m = 0; m <= 2; ++m)
      for (int rel = 0; rel <= 1; ++rel) for (int on = 0; on <= 1; ++on)
      {
          const int w = NpcSquadWriter(n, lp, mh, ho, m, rel, on);
          const int wantHolder = (n != 0 && lp != 0 && mh == 1 && m > 0) ? 1 : 0;
          const int wantStray = (n != 0 && lp != 0 && mh != 1 && ho == 1 && m == 0 && (rel == 0 || on != 0)) ? 1 : 0;
          if (w == kNpcWriteHolder) ++holder; else if (w == kNpcWriteRefuseStray) ++stray; else if (w != kNpcWriteToday) Fail("the table: only Today, Holder or RefuseStray");
          if ((w == kNpcWriteHolder) != (wantHolder != 0)) Fail("the table: the holder writes exactly when it is an NPC squad, a position was read, I hold the area and run some of the people");
          if ((w == kNpcWriteRefuseStray) != (wantStray != 0)) Fail("the table: refused exactly when it is an NPC squad, a position was read, I do not hold the area, another game does, none of the people is mine, and none was released here or another game's record is newer");
      }
      if (holder != 24) Fail("the table: 3 other-holder answers x 2 mine counts x 4 released / newer answers = 24 holder rows");
      if (stray != 6) Fail("the table: my-holder answers 0 and -1 x 3 released / newer answers that refuse = 6 stray rows"); }
    /* the wake's skipped load */
    if (NpcWakeSkipLoad(1, 1) != 1) Fail("woken while another game holds the area: the record is not applied");
    if (NpcWakeSkipLoad(1, 0) != 0 || NpcWakeSkipLoad(1, -1) != 0) Fail("woken while nobody / no fresh map says another holds it: the record applies");
    if (NpcWakeSkipLoad(0, 1) != 0) Fail("not a world NPC squad: the record applies");
    /* the stamp: npcSquad, purgeTouched, members */
    if (NpcPurgeForgetStamp(1, 1, 0) != 1) Fail("the orphan purge emptied the copy: its stamp is forgotten");
    if (NpcPurgeForgetStamp(1, 1, 3) != 0) Fail("the purge took people out but the copy still holds some: kept");
    if (NpcPurgeForgetStamp(1, 1, -1) != 0) Fail("member count unread: kept");
    if (NpcPurgeForgetStamp(1, 0, 0) != 0) Fail("an empty copy the purge never touched: kept (an older record must not bring the dead back)");
    if (NpcPurgeForgetStamp(0, 1, 0) != 0) Fail("not a world NPC squad: kept");
    { int forgets = 0; for (int n = 0; n <= 1; ++n) for (int t = 0; t <= 1; ++t) for (int m = -1; m <= 2; ++m) forgets += NpcPurgeForgetStamp(n, t, m);
      if (forgets != 1) Fail("the stamp table: forgotten in exactly one row (NPC squad, touched by the purge, empty)"); }
    /* the world's version: mySlot, haveRecord, owner, recordAt, haveStamp, stamp */
    if (NpcCopyFresh(0, 0, -1, 0, 0, 0) != 1) Fail("no record: allowed");
    if (NpcCopyFresh(0, 1, 0, 500, 0, 0) != 1) Fail("the newest record is my own: allowed (even with no stamp on this copy)");
    if (NpcCopyFresh(0, 1, 1, 500, 1, 500) != 1) Fail("this copy loaded the newest record (stamp equal): allowed");
    if (NpcCopyFresh(0, 1, 1, 500, 1, 501) != 1) Fail("this copy wrote past it: allowed");
    if (NpcCopyFresh(0, 1, 1, 500, 1, 499) != 0) Fail("another game's record newer than this copy's stamp: refused");
    if (NpcCopyFresh(0, 1, 1, 500, 0, 0) != 0) Fail("another game's record, no stamp on this copy: refused");
    if (NpcCopyFresh(0, 1, -1, 500, 0, 0) != 0) Fail("a record with no writer slot, no stamp: refused (it loads at the next wake)");
    if (NpcCopyFresh(-1, 1, 1, 500, 0, 0) != 1) Fail("no slot of my own: allowed");
    { int allowed = 0;
      for (int hr = 0; hr <= 1; ++hr) for (int ow = -1; ow <= 1; ++ow) for (int hs = 0; hs <= 1; ++hs) for (long long st = 499; st <= 501; ++st)
      {
          const int f = NpcCopyFresh(0, hr, ow, 500, hs, hs != 0 ? st : 0);
          const int want = (hr == 0 || ow == 0 || (hs != 0 && st >= 500)) ? 1 : 0;
          if (f != want) Fail("the freshness table: allowed exactly for no record, my own record, or a stamp at or past the record");
          allowed += f;
      }
      if (allowed != 18 + 6 + 4) Fail("the freshness table: 18 no-record rows + 6 own-record rows + 4 stamped rows of the other two owners = 28 allowed"); }
    if (NpcRecordOtherNewer(0, 1, 1, 500, 1, 499) != 1) Fail("another game's record newer than this copy's stamp: other-newer");
    if (NpcRecordOtherNewer(0, 1, 1, 500, 0, 0) != 1) Fail("another game's record, no stamp: other-newer");
    if (NpcRecordOtherNewer(0, 1, 1, 500, 1, 500) != 0) Fail("this copy loaded it: not other-newer");
    if (NpcRecordOtherNewer(0, 1, 0, 500, 0, 0) != 0) Fail("my own record: not other-newer");
    if (NpcRecordOtherNewer(0, 0, 1, 500, 0, 0) != 0) Fail("no record: not other-newer");
    if (NpcRecordOtherNewer(0, 1, -1, 500, 0, 0) != 0) Fail("no writer slot: not another game's");
    if (NpcRecordOtherNewer(-1, 1, 1, 500, 0, 0) != 0) Fail("no slot of my own: no answer");
    /* "another game's file": mySlot, owner, blockOwner */
    if (OtherGameFile(-1, 1, 1) != 0) Fail("no slot of my own: no answer");
    if (OtherGameFile(0, -1, 1) != 0) Fail("a record with no writer slot: not another game's");
    if (OtherGameFile(0, 0, 1) != 0) Fail("my own write: not another game's");
    if (OtherGameFile(0, 1, 1) != 1) Fail("another game's write of its own squad: another game's file");
    if (OtherGameFile(0, 1, 2) != 1) Fail("the holder's write of a third game's squad: another game's file");
    if (OtherGameFile(0, 1, -1) != 1) Fail("another game's write, block unknown: another game's file");
    if (OtherGameFile(0, 1, 0) != 0) Fail("the holder's write of MY squad: still mine - my copy is kept");
    /* an own-block record in another game's area: mySlot, owner, blockOwner, heldByOther, worldFaction (no player-count input: the same
       verdict whoever else is in the world) */
    if (OwnBlockRecvHold(0, 1, 0, 1, 1) != 1) Fail("another game's record of my squad, asleep in an area another game holds: left asleep where it is");
    if (OwnBlockRecvHold(0, 0, 0, 1, 1) != 0) Fail("my own record: the ordinary path");
    if (OwnBlockRecvHold(0, -1, 0, 1, 1) != 0) Fail("a record with no writer slot: the ordinary path");
    if (OwnBlockRecvHold(0, 1, 1, 1, 1) != 0) Fail("another block's squad: the other-game-file refusal decides");
    if (OwnBlockRecvHold(0, 1, -1, 1, 1) != 0) Fail("block unknown: the ordinary path");
    if (OwnBlockRecvHold(0, 1, 0, -2, 1) != 1) Fail("a frozen area (its holder away - what a host alone reads for another player's area): left asleep, as held");
    if (OwnBlockRecvHold(0, 1, 0, 0, 1) != 0 || OwnBlockRecvHold(0, 1, 0, -1, 1) != 0) Fail("nobody else holds the area / no fresh map: the ordinary path");
    if (OwnBlockRecvHold(0, 1, 0, 1, 0) != 0) Fail("not a world faction's squad: the ordinary path");
    if (OwnBlockRecvHold(-1, 1, -1, 1, 1) != 0) Fail("no slot of my own (an unknown block must not match it): the ordinary path");
    /* the reported chain (slot 0 = A, the village's numbering; slot 1 = B, the area's holder) */
    if (NpcSquadWriter(1, 1, 1, 0, 5, 0, 0) != kNpcWriteHolder) Fail("chain: B holds the area and runs A's village squad - the writer rule lets B write it");
    if (NpcCopyFresh(1, 1, 0, 100, 1, 100) != 1) Fail("chain: B's copy loaded A's record at its wake - B may write it");
    if (OwnBlockRecvHold(0, 1, 0, 1, 1) != 1) Fail("chain: B's record of A's sleeping squad while B holds the area - A leaves it asleep (not moved in and woken)");
    if (NpcSquadWriter(1, 1, 0, 1, 0, 0, 1) != kNpcWriteRefuseStray) Fail("chain: A's own squad woken in B's area, none of its people A's - not published");
    if (OtherGameFile(0, 1, 0) != 0) Fail("chain: B's record of A's squad does not make A throw its copy away");
    if (NpcWakeSkipLoad(1, 1) != 1) Fail("chain: A's copy woke in B's area - the record is not applied");
    if (NpcPurgeForgetStamp(1, 1, 0) != 1) Fail("chain: the purge emptied A's copy - its stamp is forgotten");
    if (NpcCopyFresh(0, 1, 1, 200, 0, 0) != 0) Fail("chain: A's emptied copy, stamp forgotten, B's record newest - A writes nothing of it");
    if (NpcCopyFresh(0, 1, 1, 200, 1, 150) != 0) Fail("chain: A's stale copy adopted when B lets go (A now the holder) - refused until its wake loads B's record");
    if (NpcWakeSkipLoad(1, 0) != 0) Fail("chain: A returns after B left - the wake loads the world's record");
    if (NpcCopyFresh(0, 1, 1, 200, 1, 200) != 1) Fail("chain: after loading B's record A may write it again");
}

/* T-587: the store authority's sleeping-position walk (squadwriter.h SleepPosPublish). */
void t_t587_sleep_pos_publish()
{
    using namespace coopsquad;
    if (SleepPosPublish(0, 1, 1, 1) != kSleepPosStale) Fail("a copy another game's newer record supersedes sends nothing, however far it lies");
    if (SleepPosPublish(0, 0, 0, 0) != kSleepPosStale) Fail("stale is asked first");
    if (SleepPosPublish(1, 0, 1, 1) != kSleepPosUnusable) Fail("a position the engine never wrote is never sent over a record");
    if (SleepPosPublish(1, 0, 0, 0) != kSleepPosUnusable) Fail("no record is made from no position");
    if (SleepPosPublish(1, 1, 0, 0) != kSleepPosSend) Fail("a squad with no record here is sent");
    if (SleepPosPublish(1, 1, 1, 1) != kSleepPosSend) Fail("a fresh copy that moved far from its record is sent");
    if (SleepPosPublish(1, 1, 1, 0) != kSleepPosKeep) Fail("a fresh copy near its record is not sent");
    /* the copy's freshness as the walk asks it (NpcCopyFresh): made from slot 1's record stamped 493, slot 1 then wrote 546 */
    if (SleepPosPublish(NpcCopyFresh(0, 1, 1, 546, 1, 493), SquadPosUsable(0.0f, 0.0f, 0.0f), 1, 1) != kSleepPosStale) Fail("a copy made from an older record of another game sends nothing");
    if (SleepPosPublish(NpcCopyFresh(0, 1, 0, 546, 0, 0), 1, 1, 1) != kSleepPosSend) Fail("this game's own record: its copy sends its travel");
}

/* THE STEAM LINK'S SPLIT AND REJOIN (src/common/relaysplit.h): every size the relay test sends travels in the right pieces and comes back
   whole, an unreliable frame may arrive between the pieces of a reliable one, and every malformed sequence is refused. */
std::vector<char> RsPattern(size_t n, unsigned int seed)
{
    std::vector<char> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (char)((i * 131u + seed * 7u + (i >> 11)) & 255u);
    return v;
}

int RsTake(relaysplit::Rejoin* r, const std::vector<char>& m, bool reliable, std::vector<char>* f)
{
    size_t pieces = 0;
    const char* why = 0;
    return relaysplit::Take(r, m.empty() ? 0 : &m[0], m.size(), reliable, f, &pieces, &why);
}

void t_steamlink_split_sizes()
{
    const size_t sizes[]  = { 0, 1, 255 * 1024, 256 * 1024, 256 * 1024 + 1, 2635601, 6013373, (size_t)32 << 20 };
    const size_t pieces[] = { 1, 1, 1, 1, 2, 11, 23, 128 };
    size_t k;
    for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k)
    {
        const std::vector<char> f = RsPattern(sizes[k], (unsigned int)k);
        std::vector<std::vector<char> > m;
        if (!relaysplit::Split(f.empty() ? 0 : &f[0], f.size(), true, &m)) { Fail("a frame within 32 MiB is refused"); continue; }
        if (m.size() != pieces[k] || relaysplit::PieceCount(sizes[k]) != pieces[k]) Fail("the piece count of a size");
        relaysplit::Rejoin r;
        std::vector<char> out;
        bool shape = true;
        size_t i;
        for (i = 0; i < m.size(); ++i)
        {
            if (m[i].empty() || m[i].size() > relaysplit::kPieceBytes + 1 || m[i].size() > 524288u) { shape = false; continue; }
            const int want = m.size() == 1 ? (int)relaysplit::kWhole : (i + 1 < m.size() ? (int)relaysplit::kMore : (int)relaysplit::kLast);
            if ((int)(unsigned char)m[i][0] != want) shape = false;
            size_t pc = 0;
            const char* why = 0;
            const int t = relaysplit::Take(&r, &m[i][0], m[i].size(), true, &out, &pc, &why);
            if (t != (i + 1 < m.size() ? (int)relaysplit::kTakePiece : (int)relaysplit::kTakeFrame)) shape = false;
            if (i + 1 == m.size() && pc != m.size()) shape = false;
        }
        if (!shape) Fail("a message's kind, size or rejoin step");
        if (out != f) Fail("the rejoined frame differs from the sent one");
        if (r.open) Fail("the rejoin stays open after the last piece");
    }
    std::vector<char> big(((size_t)32 << 20) + 1, 'x');
    std::vector<std::vector<char> > m;
    if (relaysplit::Split(&big[0], big.size(), true, &m) || !m.empty()) Fail("a frame one byte over 32 MiB is split");
    if (relaysplit::SendVerdict(big.size(), true) != relaysplit::kSendTooBig) Fail("SendVerdict over 32 MiB");
    if (!relaysplit::Split(&big[0], relaysplit::kPieceBytes, false, &m) || m.size() != 1) Fail("an unreliable frame of one piece is refused");
    m.clear();
    if (relaysplit::Split(&big[0], relaysplit::kPieceBytes + 1, false, &m) || !m.empty()) Fail("an unreliable frame over one piece is split");
    if (relaysplit::SendVerdict(relaysplit::kPieceBytes + 1, false) != relaysplit::kSendUnreliableTooBig) Fail("SendVerdict unreliable");
}

void t_steamlink_split_interleave()
{
    const std::vector<char> f = RsPattern(600000, 9);
    std::vector<std::vector<char> > m;
    if (!relaysplit::Split(&f[0], f.size(), true, &m) || m.size() != 3) { Fail("600,000 bytes are not three pieces"); return; }
    const std::vector<char> u1 = RsPattern(100, 1);
    std::vector<std::vector<char> > um;
    if (!relaysplit::Split(&u1[0], u1.size(), false, &um) || !relaysplit::Split(0, 0, false, &um) || um.size() != 2) { Fail("two unreliable frames"); return; }
    relaysplit::Rejoin r;
    std::vector<char> out;
    if (RsTake(&r, m[0], true, &out) != relaysplit::kTakePiece) Fail("the first piece");
    if (RsTake(&r, um[0], false, &out) != relaysplit::kTakeFrame || out != u1) Fail("an unreliable frame between two pieces is not handed on whole");
    if (!r.open) Fail("an unreliable frame closed the open split frame");
    if (RsTake(&r, m[1], true, &out) != relaysplit::kTakePiece) Fail("the second piece");
    if (RsTake(&r, um[1], false, &out) != relaysplit::kTakeFrame || !out.empty()) Fail("an empty unreliable frame between two pieces");
    if (RsTake(&r, m[2], true, &out) != relaysplit::kTakeFrame || out != f) Fail("the split frame around two unreliable frames differs");
    const std::vector<char> g = RsPattern(5, 3);
    std::vector<std::vector<char> > w;
    if (!relaysplit::Split(&g[0], g.size(), true, &w) || RsTake(&r, w[0], true, &out) != relaysplit::kTakeFrame || out != g)
        Fail("a whole reliable frame after a split one");
}

void t_steamlink_split_malformed()
{
    relaysplit::Rejoin r;
    std::vector<char> out;
    const std::vector<char> empty;
    if (RsTake(&r, empty, true, &out) != relaysplit::kTakeBad) Fail("an empty message");
    std::vector<char> k3(2, 'a'); k3[0] = 3;
    if (RsTake(&r, k3, true, &out) != relaysplit::kTakeBad) Fail("an unknown kind byte");
    std::vector<char> more(10, 'b'); more[0] = (char)relaysplit::kMore;
    std::vector<char> last(10, 'c'); last[0] = (char)relaysplit::kLast;
    std::vector<char> whole(10, 'd'); whole[0] = (char)relaysplit::kWhole;
    if (RsTake(&r, more, false, &out) != relaysplit::kTakeBad) Fail("a piece on the unreliable lane");
    if (RsTake(&r, last, true, &out) != relaysplit::kTakeBad) Fail("a last piece with no first");
    if (RsTake(&r, more, true, &out) != relaysplit::kTakePiece) Fail("a first piece");
    if (RsTake(&r, whole, true, &out) != relaysplit::kTakeBad) Fail("a whole reliable frame inside a split one");
    if (r.open || !r.buf.empty()) Fail("a refused sequence leaves the rejoin open");
    const std::vector<char> bare(1, (char)relaysplit::kMore);
    if (RsTake(&r, bare, true, &out) != relaysplit::kTakeBad) Fail("an empty piece");
    std::vector<char> fat(relaysplit::kPieceBytes + 2, 'e'); fat[0] = (char)relaysplit::kWhole;
    if (RsTake(&r, fat, true, &out) != relaysplit::kTakeBad) Fail("a whole message longer than one piece");
    std::vector<char> piece(relaysplit::kPieceBytes + 1, 'f'); piece[0] = (char)relaysplit::kMore;
    int t = relaysplit::kTakeBad;
    int i;
    for (i = 0; i < 128; ++i) t = RsTake(&r, piece, true, &out);
    if (t != relaysplit::kTakePiece || r.buf.size() != relaysplit::kRejoinMax) Fail("32 MiB of pieces are not kept");
    std::vector<char> one(2, 'g'); one[0] = (char)relaysplit::kLast;
    if (RsTake(&r, one, true, &out) != relaysplit::kTakeOver) Fail("a split frame over 32 MiB is not refused");
    if (r.open || !r.buf.empty()) Fail("the over-cap frame stays open");
    if (RsTake(&r, whole, true, &out) != relaysplit::kTakeFrame || out.size() != 9) Fail("a whole frame after a refusal");
}

/* THE STEAM LINK'S LOG FACTS AND GRACEFUL-CLOSE BUFFER (relaysplit.h): the relayed test, the data-centre codes in Valve's packing,
   the state words, and the linger buffer's cap. */
void t_steamlink_log_facts()
{
    if (relaysplit::LinkRelayed(0, 0)) Fail("a direct link is relayed");
    if (!relaysplit::LinkRelayed(16, 0)) Fail("the Relayed flag alone");
    if (!relaysplit::LinkRelayed(0, 1)) Fail("a relay data centre alone");
    if (relaysplit::LinkRelayed(1 | 2 | 4 | 8 | 32, 0)) Fail("other flags are not the Relayed flag");
    char t[16];
    relaysplit::PopText(0, t);
    if (std::string(t) != "none") Fail("POP 0");
    const unsigned int abcd = ((unsigned int)'d' << 24) | ((unsigned int)'a' << 16) | ((unsigned int)'b' << 8) | (unsigned int)'c';
    relaysplit::PopText(abcd, t);
    if (std::string(t) != "abcd") Fail("Valve's example: abcd has d in the top byte");
    const unsigned int iad = ((unsigned int)'i' << 16) | ((unsigned int)'a' << 8) | (unsigned int)'d';
    relaysplit::PopText(iad, t);
    if (std::string(t) != "iad") Fail("a three-letter code");
    relaysplit::PopText(16909060u, t);
    if (std::string(t) != "0x01020304") Fail("an unprintable id in hex");
    relaysplit::PopText(4294967295u, t);
    if (std::string(t) != "0xffffffff") Fail("all bits in hex");
    if (std::string(relaysplit::LinkStateWord(2)) != "finding-route" || std::string(relaysplit::LinkStateWord(3)) != "connected"
        || std::string(relaysplit::LinkStateWord(99)) != "other") Fail("state words");
}

void t_steamlink_linger_buffer()
{
    const long long normal = 4194304, cap = 41943040;
    if (relaysplit::LingerBufferBytes(0, normal, cap) != normal) Fail("an empty queue keeps the normal buffer");
    if (relaysplit::LingerBufferBytes(6013373, normal, cap) != 6013373 + normal) Fail("a 6 MB record fits on top of the normal buffer");
    if (relaysplit::LingerBufferBytes(cap, normal, cap) != cap) Fail("the cap holds");
    if (relaysplit::LingerBufferBytes(-5, normal, cap) != normal) Fail("a negative queue count");
}

/* THE STEAM LINK'S SEND RATE (relaysplit.h NextSendRate, owner decision 594): each branch, the floor and the ceiling, unknown
   quality, no waiting data. */
void t_steamlink_send_rate()
{
    const int start = relaysplit::kSendRateStartBytes, low = relaysplit::kSendRateFloorBytes, top = relaysplit::kSendRateCeilingBytes;
    if (start != 524288 || low != 131072 || top != 4194304) Fail("the start, floor and ceiling");
    const unsigned int lots = 1000000u;
    /* halve: either quality known and below 0.97 */
    if (relaysplit::NextSendRate(start, 0.96f, 1.0f, lots) != start / 2) Fail("local loss halves");
    if (relaysplit::NextSendRate(start, 1.0f, 0.5f, 0) != start / 2) Fail("remote loss halves, data waiting or not");
    if (relaysplit::NextSendRate(start, 0.5f, -1.0f, lots) != start / 2) Fail("local loss halves while remote is unknown");
    if (relaysplit::NextSendRate(start, -1.0f, 0.9f, lots) != start / 2) Fail("remote loss halves while local is unknown");
    if (relaysplit::NextSendRate(200000, 0.0f, 0.0f, 0) != low) Fail("halving stops at the floor");
    if (relaysplit::NextSendRate(low, 0.1f, 0.1f, 0) != low) Fail("the floor holds");
    /* raise: both known and at least 0.99, more than a quarter of the rate waiting */
    if (relaysplit::NextSendRate(start, 0.99f, 1.0f, (unsigned int)(start / 4) + 1u) != start + start / 4) Fail("clean delivery with data waiting raises a quarter");
    if (relaysplit::NextSendRate(start, 1.0f, 1.0f, (unsigned int)(start / 4)) != start) Fail("exactly a quarter waiting keeps");
    if (relaysplit::NextSendRate(start, 1.0f, 1.0f, 0) != start) Fail("no waiting data keeps");
    if (relaysplit::NextSendRate(4000000, 1.0f, 1.0f, lots * 4) != top) Fail("raising stops at the ceiling");
    if (relaysplit::NextSendRate(top, 1.0f, 1.0f, 4294967295u) != top) Fail("the ceiling holds");
    /* keep: quality from 0.97 to under 0.99, or not known */
    if (relaysplit::NextSendRate(start, 0.98f, 1.0f, lots) != start) Fail("0.98 keeps");
    if (relaysplit::NextSendRate(start, 0.97f, 0.97f, lots) != start) Fail("exactly 0.97 keeps");
    if (relaysplit::NextSendRate(start, -1.0f, -1.0f, lots) != start) Fail("both unknown keeps");
    if (relaysplit::NextSendRate(start, -1.0f, 1.0f, lots) != start) Fail("local unknown does not raise");
    if (relaysplit::NextSendRate(start, 1.0f, -1.0f, lots) != start) Fail("remote unknown does not raise");
    if (relaysplit::NextSendRate(start, 1.5f, 1.0f, lots) != start) Fail("an out-of-range quality is unknown");
    if (relaysplit::SendQualityKnown(-1.0f) || !relaysplit::SendQualityKnown(0.0f) || !relaysplit::SendQualityKnown(1.0f))
        Fail("which qualities are known");
}

/* THE STEAM LINK'S LOGS NAME NO STEAM ID (relaysplit.h HideIdNumbers): a 17-digit id in Steam's text is hidden, shorter numbers stay. */
void t_steamlink_hide_ids()
{
    if (relaysplit::HideIdNumbers("#2349873 P2P steamid:76561198012345678 vport 0") != "#2349873 P2P steamid:<id> vport 0")
        Fail("a Steam id in Steam's description");
    if (relaysplit::HideIdNumbers("127.0.0.1:27200 #12345678901234") != "127.0.0.1:27200 #12345678901234") Fail("shorter numbers stay");
    if (relaysplit::HideIdNumbers("123456789012345") != "<id>") Fail("15 digits are hidden");
    if (relaysplit::HideIdNumbers("a 76561198012345678") != "a <id>") Fail("an id at the end");
    if (!relaysplit::HideIdNumbers("").empty()) Fail("an empty text");
    if (relaysplit::HideIdNumbers("remote 203.0.113.7:27015 relayed") != "remote <ip>:27015 relayed") Fail("an IPv4 address and its port");
    if (relaysplit::HideIdNumbers("candidates 10.0.0.2,192.168.1.20") != "candidates <ip>,<ip>") Fail("two private IPv4 addresses");
    if (relaysplit::HideIdNumbers("ipv6 [2001:db8::1]:27015") != "ipv6 [<ip>]:27015") Fail("an IPv6 address");
    if (relaysplit::HideIdNumbers("::ffff:198.51.100.4") != "<ip>") Fail("an IPv4-mapped IPv6 address");
    if (relaysplit::HideIdNumbers("v1.2.3 build 4") != "v1.2.3 build 4") Fail("a version with three parts is not an address");
    if (relaysplit::HideIdNumbers("time 12:30") != "time 12:30") Fail("one colon is not an IPv6 address");
}

void t_steamlink_relaytest_cfg()
{
    long long bad = -1;
    coopcfg::CfgFields f;
    coopcfg::CfgParseText("relaytest=listen:27200\r\nrelaylag=60\r\nrelayloss=1\r\n", &f, 0, 0, 0, &bad);
    if (bad != 0 || f.relayTestMode != coopcfg::kRelayTestListen || f.relayTestPort != 27200 || f.relayLagMs != 60 || f.relayLossPct != 1)
        Fail("listen with lag and loss");
    coopcfg::CfgFields d;
    bad = -1;
    coopcfg::CfgParseText("relaytest=Dial:127.0.0.1:27200\r\n", &d, 0, 0, 0, &bad);
    if (bad != 0 || d.relayTestMode != coopcfg::kRelayTestDial || d.relayTestPort != 27200) Fail("dial");
    coopcfg::CfgFields p;
    bad = -1;
    coopcfg::CfgParseText("relaytest=pair\r\n", &p, 0, 0, 0, &bad);
    if (bad != 0 || p.relayTestMode != coopcfg::kRelayTestPair || p.relayTestPort != 0) Fail("pair");
    const char* const refused[] = { "relaytest=dial:10.0.0.2:27200", "relaytest=listen:0", "relaytest=listen:70000", "relaytest=dial:127.0.0.1:",
                                    "relaytest=on", "relaylag=1001", "relayloss=51", "relaylag=-1" };
    size_t i;
    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
        coopcfg::CfgFields x;
        long long b = -1;
        coopcfg::CfgParseText(std::string(refused[i]) + "\r\n", &x, 0, 0, 0, &b);
        if (b != 1 || x.relayTestMode != coopcfg::kRelayTestNone || x.relayLagMs != 0 || x.relayLossPct != 0)
            Fail(std::string("not refused: ") + refused[i]);
    }
    const coopcfg::CfgFields none;
    if (none.relayTestMode != coopcfg::kRelayTestNone || none.relayTestPort != 0 || none.relayLagMs != 0 || none.relayLossPct != 0) Fail("the defaults");
    const std::string written = coopcfg::CfgFormat(f);
    if (written.find("relaytest") != std::string::npos || written.find("relaylag") != std::string::npos || written.find("relayloss") != std::string::npos)
        Fail("a writer carries a relay TEST key");
}

/* STEAM FRIENDS FIND AND JOIN A HOST (src/common/steamfriends.h): the connect string a host sets and the joining game reads back from
   Steam's callback or its own command line. */
void t_steamfriends_connect_roundtrip()
{
    const unsigned long long ids[] = { 1ULL, 76561198000000001ULL, 18446744073709551615ULL };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        const std::string s = steamfriends::ConnectString(ids[i]);
        unsigned long long back = 0;
        if (!steamfriends::ParseConnect(s, &back) || back != ids[i]) Fail("the connect string does not read back: " + s);
        if (s.compare(0, 8, "+mpjoin ") != 0) Fail("the connect string starts with the verb: " + s);
    }
    if (steamfriends::ConnectString(76561198000000001ULL) != "+mpjoin steam:76561198000000001") Fail("the connect string's exact form");
    if (!steamfriends::ConnectString(0).empty()) Fail("no id, no connect string");
    unsigned long long id = 0;
    /* on a command line: after the program and other arguments, quoted or not, tabs between */
    if (!steamfriends::ParseConnect("\"C:\\Games\\Kenshi\\kenshi_x64.exe\" -nolauncher +mpjoin steam:42", &id) || id != 42) Fail("a launch command line");
    id = 0;
    if (!steamfriends::ParseConnect("kenshi_x64.exe \"+mpjoin steam:43\"", &id) || id != 43) Fail("a quoted connect string");
    id = 0;
    if (!steamfriends::ParseConnect("+mpjoin\tsteam:44 -other", &id) || id != 44) Fail("a tab after the verb");
    id = 0;
    if (!steamfriends::ParseConnect("+mpjoin steam:x +mpjoin steam:45", &id) || id != 45) Fail("a later good connect string after a bad one");
    const char* const bad[] = { "", "+mpjoin", "+mpjoin ", "+mpjoin steam:", "+mpjoin steam:0", "+mpjoin steam:12a", "+mpjoinx steam:5",
                                "x+mpjoin steam:5", "+mpjoin 81.2.3.4:7777", "+mpjoin steam:99999999999999999999999", "+connect_lobby 5",
                                "kenshi_x64.exe -nolauncher" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        id = 7;
        if (steamfriends::ParseConnect(bad[i], &id)) Fail(std::string("a connect string read from: ") + bad[i]);
        if (id != 7) Fail(std::string("the id was written on a no: ") + bad[i]);
    }
}

/* The rich presence keys for each host state, and a friend's row read back from them: password on / off, the player count, the version
   test (the protocol), the connect key naming the friend itself, and no Steam id in the log's text. */
void t_steamfriends_presence_keys()
{
    steamfriends::HostState h;
    h.selfId = 76561198000000123ULL; h.world = "Dustlands"; h.players = 3; h.password = 1; h.protocol = 152;
    if (!steamfriends::PresenceKeys(h).empty()) Fail("keys while not hosting");
    h.hosting = 1;
    steamfriends::KeyList k = steamfriends::PresenceKeys(h);
    if (k.size() != 5) Fail("five keys while hosting");
    else
    {
        if (k[0].first != "connect" || k[0].second != "+mpjoin steam:76561198000000123") Fail("the connect key");
        if (k[1].first != "mp_world" || k[1].second != "Dustlands") Fail("the world key");
        if (k[2].first != "mp_players" || k[2].second != "3") Fail("the players key");
        if (k[3].first != "mp_password" || k[3].second != "1") Fail("the password key, on");
        if (k[4].first != "mp_protocol" || k[4].second != "152") Fail("the protocol key");
    }
    const std::string logText = steamfriends::KeysForLog(k);
    if (logText.find("76561198000000123") != std::string::npos || logText.find("steam:") != std::string::npos) Fail("the log line carries the Steam id");
    if (logText.find("mp_players='3'") == std::string::npos || logText.find("connect=<set>") == std::string::npos) Fail("the log line's keys");
    if (steamfriends::KeysForLog(steamfriends::KeyList()) != "none (cleared)") Fail("the cleared log text");
    h.password = 0; h.players = 0;
    k = steamfriends::PresenceKeys(h);
    if (k.size() != 5 || k[3].second != "0") Fail("the password key, off");
    if (k.size() != 5 || k[2].second != "1") Fail("an unknown player count says 1 (the host)");
    h.players = 7;
    k = steamfriends::PresenceKeys(h);
    if (k.size() != 5 || k[2].second != "7") Fail("the player count follows a join");
    h.selfId = 0;
    if (!steamfriends::PresenceKeys(h).empty()) Fail("keys with no own id");
    h.selfId = 76561198000000123ULL;
    h.world = std::string(300, 'w');
    k = steamfriends::PresenceKeys(h);
    if (k.size() != 5 || k[1].second.size() != steamfriends::kValueMax) Fail("a long world name is cut to Steam's value limit");
    /* a friend's row read back from the keys */
    h.world = "Hub #1"; h.players = 4; h.password = 1; h.protocol = 152;
    k = steamfriends::PresenceKeys(h);
    steamfriends::FriendRow r;
    if (!steamfriends::RowFromKeys(h.selfId, "Scott", k[0].second, k[1].second, k[2].second, k[3].second, k[4].second, 152, &r)) Fail("a host's keys read back");
    if (r.id != h.selfId || r.name != "Scott" || r.world != "Hub #1" || r.players != 4 || r.password != 1 || r.sameVersion != 1) Fail("the row's values");
    if (steamfriends::WorldCell(r) != "Hub ##1" || steamfriends::PlayersCell(r) != "4" || std::string(steamfriends::PasswordCell(r)) != "Yes") Fail("the row's cells");
    if (!steamfriends::RowFromKeys(h.selfId, "Alex", k[0].second, "Hub Run", "1", "0", "151", 152, &r) || r.sameVersion != 0) Fail("a different protocol");
    if (steamfriends::WorldCell(r) != "Hub Run (different version)" || std::string(steamfriends::PasswordCell(r)) != "No") Fail("a different version's cells");
    if (steamfriends::RowFromKeys(h.selfId + 1, "Mallory", k[0].second, "w", "1", "0", "152", 152, &r)) Fail("a connect key naming another player");
    if (steamfriends::RowFromKeys(h.selfId, "Alex", k[0].second, "w", "1", "0", "", 152, &r)) Fail("no protocol key");
    if (steamfriends::RowFromKeys(h.selfId, "Alex", "", "w", "1", "0", "152", 152, &r)) Fail("no connect key");
    if (!steamfriends::RowFromKeys(h.selfId, "Alex", k[0].second, "w", "lots", "0", "152", 152, &r) || r.players != 1) Fail("a bad player count reads as 1");
    if (steamfriends::PressLineText(1, "Connecting to steam:76561198000000001...") != "Connecting...") Fail("a friend's join says Connecting... only");
    if (steamfriends::PressLineText(1, "Connecting to Scott's game...") != "Connecting...") Fail("a friend's join never names the friend");
    if (steamfriends::PressLineText(0, "Connecting to 81.2.3.4:7777...") != "Connecting to 81.2.3.4:7777...") Fail("a typed address keeps its line");
    if (std::string(steamfriends::kFriendConnectingText) != "Connecting...") Fail("the friend's joining line, exactly");
    if (steamfriends::InWorldRefuseText("") != "You can't join that game while you are in a world.") Fail("the in-world CAN'T JOIN line with no name");
    if (steamfriends::InWorldRefuseText("A#b") != "You can't join A##b's game while you are in a world.") Fail("the in-world line's name is screen text");
    if (!steamfriends::ConnectNamesFriend(42, "+mpjoin steam:42")) Fail("a connect naming the friend Steam named");
    if (steamfriends::ConnectNamesFriend(42, "+mpjoin steam:43")) Fail("a connect naming another player");
    if (steamfriends::ConnectNamesFriend(0, "+mpjoin steam:42") || steamfriends::ConnectNamesFriend(42, "") || steamfriends::ConnectNamesFriend(42, "steam:42"))
        Fail("no friend, no connect, or no verb names nobody");
    if (steamfriends::JoinBoxPrefillOk("steam:76561198000000001") || steamfriends::JoinBoxPrefillOk("")) Fail("a steam:<id> or nothing is never pre-filled");
    if (!steamfriends::JoinBoxPrefillOk("81.2.3.4:7777") || !steamfriends::JoinBoxPrefillOk("myhost.example:7777")) Fail("a typed address is pre-filled");
    if (steamfriends::InWorldRefuseText("Scott") != "You can't join Scott's game while you are in a world.") Fail("the in-world CAN'T JOIN line");
}

/* What shows in place of the friends list: no Steam DLL (GOG), Steam not running, nobody hosting, or the list; the exact lines; the
   TEST-ONLY sample rows and the lever's arguments. */
void t_steamfriends_list_line()
{
    if (steamfriends::ListShows(0, 0, 0) != steamfriends::kShowNoSteamApi || steamfriends::ListShows(0, 1, 3) != steamfriends::kShowNoSteamApi)
        Fail("no Steam API wins");
    if (steamfriends::ListShows(1, 0, 0) != steamfriends::kShowSteamNotRunning || steamfriends::ListShows(1, 0, 2) != steamfriends::kShowSteamNotRunning)
        Fail("Steam not running");
    if (steamfriends::ListShows(1, 1, 0) != steamfriends::kShowNobodyHosting) Fail("nobody hosting");
    if (steamfriends::ListShows(1, 1, 1) != steamfriends::kShowList) Fail("the list");
    if (std::string(steamfriends::ListLine(steamfriends::kShowNobodyHosting)) != "No Steam friends are hosting right now.")
        Fail("the nobody-hosting line");
    if (std::string(steamfriends::ListLine(steamfriends::kShowSteamNotRunning)) != "Steam isn't running, so friends' games can't be shown.")
        Fail("the Steam-not-running line");
    if (std::string(steamfriends::ListLine(steamfriends::kShowNoSteamApi)) != "Friends' games are shown only in the Steam version of Kenshi.")
        Fail("the no-Steam line");
    if (std::string(steamfriends::ListLine(steamfriends::kShowList)) != "") Fail("no line with the list");
    const std::vector<steamfriends::FriendRow> f = steamfriends::FakeRows(3);
    if (f.size() != 3 || f[0].password != 1 || f[0].sameVersion != 1 || f[1].sameVersion != 0 || f[1].password != 0 || f[0].name != "Scott" || f[1].world != "Hub Run")
        Fail("the sample rows: the first with a password, the second a different version");
    if (steamfriends::FakeRows(20).size() != (size_t)steamfriends::kFakeMax) Fail("the sample rows' cap");
    int n = -1;
    if (!steamfriends::FakeArgs("fake 3", &n) || n != 3) Fail("fake 3");
    if (!steamfriends::FakeArgs(" off ", &n) || n != 0) Fail("off");
    const char* const badArgs[] = { "", "fake", "fake 0", "fake 9", "fake x", "on", "fake -1" };
    for (size_t i = 0; i < sizeof(badArgs) / sizeof(badArgs[0]); ++i) if (steamfriends::FakeArgs(badArgs[i], &n)) Fail(std::string("lever args taken: ") + badArgs[i]);
    if (std::string(steamfriends::SourceWord(steamfriends::kFromList)) != "list" || std::string(steamfriends::SourceWord(steamfriends::kFromCallback)) != "callback"
        || std::string(steamfriends::SourceWord(steamfriends::kFromLaunch)) != "launch" || std::string(steamfriends::SourceWord(steamfriends::kFromLever)) != "lever")
        Fail("the join request's source words");
    /* JOIN GAME's rows fit the panel: the list, REFRESH and JOIN BY ADDRESS between PLAYER NAME and IP ADDRESS, the bands in that order */
    const int lays[] = { coopui::kLayJoin, coopui::kLayJoinHint };
    for (int k = 0; k < 2; ++k)
    {
        if (coopui::PanelLayoutHalves(lays[k]) > coopui::kPanelMaxHalves) Fail("JOIN GAME is taller than the panel's rows, layout " + I(lays[k]));
        const coopui::PanelBand nb = coopui::PanelSlotBand(lays[k], coopui::kSlotNameRow, 24);
        const coopui::PanelBand lb = coopui::PanelSlotBand(lays[k], coopui::kSlotList, 24);
        const coopui::PanelBand kb = coopui::PanelSlotBand(lays[k], coopui::kSlotListBtns, 24);
        const coopui::PanelBand ab = coopui::PanelSlotBand(lays[k], coopui::kSlotJoinByAddr, 24);
        const coopui::PanelBand rb = coopui::PanelSlotBand(lays[k], coopui::kSlotAddrRow, 24);
        if (lb.h <= 0 || kb.h <= 0 || ab.h <= 0 || rb.h <= 0) Fail("a JOIN GAME row is missing, layout " + I(lays[k]));
        if (!(nb.top < lb.top && lb.top + lb.h == kb.top && kb.top + kb.h == ab.top && ab.top + ab.h == rb.top))
            Fail("JOIN GAME's rows are not name, list, REFRESH, JOIN BY ADDRESS, IP ADDRESS in order, layout " + I(lays[k]));
    }
}

/* INVITE FRIENDS' DECISIONS (src/common/steamfriends.h): who is listed, each friend's STATUS, the list's order, the sample rows, the
   approved lines; HOSTING's line with every Steam place taken; the steamjoin lever's two argument forms; the two layouts. */
void t_t290s4b_invite()
{
    if (steamfriends::InviteListed(0) || steamfriends::InviteListed(7) || !steamfriends::InviteListed(1) || !steamfriends::InviteListed(3)
        || !steamfriends::InviteListed(6)) Fail("an offline / invisible friend listed, or an online one not");
    if (steamfriends::InviteStatusOf(3, 233860) != steamfriends::kInvitePlaying || steamfriends::InviteStatusOf(1, 0) != steamfriends::kInviteOnline
        || steamfriends::InviteStatusOf(2, 440) != steamfriends::kInviteOnline || steamfriends::InviteStatusOf(3, 0) != steamfriends::kInviteAway
        || steamfriends::InviteStatusOf(4, 0) != steamfriends::kInviteAway || steamfriends::InviteStatusOf(6, 0) != steamfriends::kInviteOnline)
        Fail("a friend's STATUS");
    if (std::string(steamfriends::InviteStatusText(steamfriends::kInvitePlaying)) != "Playing Kenshi"
        || std::string(steamfriends::InviteStatusText(steamfriends::kInviteOnline)) != "Online"
        || std::string(steamfriends::InviteStatusText(steamfriends::kInviteAway)) != "Away") Fail("the STATUS words");
    const std::vector<steamfriends::InviteRow> f = steamfriends::InviteFakeRows(3);
    if (f.size() != 3 || f[0].name != "Sam" || f[0].status != steamfriends::kInvitePlaying || f[1].name != "Alex" || f[1].status != steamfriends::kInviteOnline
        || f[2].name != "Jordan" || f[2].status != steamfriends::kInviteAway || f[0].sample != 1 || f[2].sample != 1) Fail("the sample rows");
    if (steamfriends::InviteFakeRows(20).size() != (size_t)steamfriends::kFakeMax) Fail("the sample rows' cap");
    {
        std::vector<steamfriends::InviteRow> in(4);
        in[0].name = "A"; in[0].status = steamfriends::kInviteAway;
        in[1].name = "B"; in[1].status = steamfriends::kInviteOnline;
        in[2].name = "C"; in[2].status = steamfriends::kInvitePlaying;
        in[3].name = "D"; in[3].status = steamfriends::kInviteOnline;
        const std::vector<steamfriends::InviteRow> o = steamfriends::InviteOrdered(in);
        if (o.size() != 4 || o[0].name != "C" || o[1].name != "B" || o[2].name != "D" || o[3].name != "A") Fail("the list's order");
    }
    if (steamfriends::InviteSentText("Sam") != "Invite sent to Sam." || steamfriends::InviteSentText("a#b") != "Invite sent to a##b.") Fail("the sent line");
    if (std::string(steamfriends::kInviteNoneText) != "None of your Steam friends are online."
        || std::string(steamfriends::kInviteFailedText) != "Couldn't send the invite."
        || std::string(steamfriends::kSteamRowOffText) != "Steam isn't running, so friends can only join by address.") Fail("the approved lines");
    if (steamfriends::HostingSteamFullText("Hosting.", 1, 32) != "Hosting. 32 players have joined through Steam; more can join by address."
        || steamfriends::HostingSteamFullText("Hosting.", 0, 32) != "Hosting."
        || steamfriends::HostingSteamFullText("The world couldn't start. Try again.", 1, 32) != "The world couldn't start. Try again.")
        Fail("HOSTING's line with every Steam place taken");
    const std::string cs = steamfriends::ConnectString(76561198000000042ULL);
    std::string got;
    if (!steamfriends::LeverConnect(cs, &got) || got != cs) Fail("steamjoin: the whole connect string");
    got.clear();
    const std::string word = cs.substr(cs.find(' ') + 1);
    if (!steamfriends::LeverConnect(" " + word + " ", &got) || got != cs) Fail("steamjoin: the steam:<id> word alone");
    const char* const bad[] = { "", "steam:", "steam:x", "+mpjoin", "mpjoin steam:", "fake 3" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) if (steamfriends::LeverConnect(bad[i], &got)) Fail(std::string("steamjoin taken: ") + bad[i]);
    /* INVITE FRIENDS is screen 9 with its own title; HOSTING's rows in the approved order - STEAM (two rows, INVITE FRIENDS on it
       at the right), PLAYERS, LOCAL ADDRESS (the VPN row under it while one was found), INTERNET ADDRESS, the status area, BACK -
       with no JOIN BY ADDRESS heading and no router help; 22 half-rows at the ceilings, the status ceiling 6 (4 with the VPN row) */
    if (coopui::PanelLayoutOf(9, 0) != coopui::kLayInvite || coopui::PanelScreenTitle(9, 0, 1, "", "") != "INVITE FRIENDS") Fail("INVITE FRIENDS' layout / title");
    if (coopui::PanelLayoutHalves(coopui::kLayInvite) > coopui::kPanelMaxHalves) Fail("INVITE FRIENDS is taller than the panel's rows");
    const int lays[] = { coopui::kLayHosting, coopui::kLayHostingVpn };
    for (int k = 0; k < 2; ++k)
    {
        const coopui::PanelBand sb = coopui::PanelSlotBand(lays[k], coopui::kSlotSteamRow, 24);
        const coopui::PanelBand pb = coopui::PanelSlotBand(lays[k], coopui::kSlotPlayers, 24);
        const coopui::PanelBand hb = coopui::PanelSlotBand(lays[k], coopui::kSlotHomeAddr, 24);
        const coopui::PanelBand vb = coopui::PanelSlotBand(lays[k], coopui::kSlotVpnAddr, 24);
        const coopui::PanelBand nb = coopui::PanelSlotBand(lays[k], coopui::kSlotNetAddr, 24);
        const coopui::PanelBand tb = coopui::PanelSlotBand(lays[k], coopui::kSlotStatus, 24);
        const coopui::PanelBand bb = coopui::PanelSlotBand(lays[k], coopui::kSlotButtons, 24);
        const coopui::PanelBand ab = k == 1 ? vb : hb;   /* the row INTERNET ADDRESS sits under */
        if (sb.top != 0 || sb.h != 48 || pb.top != sb.top + sb.h || hb.top != pb.top + pb.h || (k == 1 && vb.top != hb.top + hb.h)
            || nb.top != ab.top + ab.h || tb.top != nb.top + nb.h || bb.top != tb.top + tb.h)
            Fail("HOSTING's rows are not STEAM, PLAYERS, LOCAL ADDRESS, INTERNET ADDRESS, the status area, BACK in order, layout " + I(lays[k]));
        if (coopui::PanelSlotBand(lays[k], coopui::kSlotJoinByAddr, 24).h != 0)
            Fail("HOSTING has a JOIN BY ADDRESS row, layout " + I(lays[k]));
        if (coopui::PanelLayoutHalves(lays[k]) != 22 || tb.h != (k == 0 ? 72 : 48))
            Fail("HOSTING is not 22 half-rows with its status ceiling, layout " + I(lays[k]));
    }
    /* the STEAM row at 1280x720 and 1920x1080: INVITE FRIENDS at the right, at least its caption wide; the line between the label
       column and it, two rows tall less PanelSteamLineDy, which puts its first line inside the button's row */
    {
        const int sizes[][2] = { { 1280, 720 }, { 1920, 1080 } };
        for (int z = 0; z < 2; ++z)
        {
            const coopui::PanelGeom g = coopui::PanelGeomIn(sizes[z][0], sizes[z][1]);
            const int invW = coopui::PanelInviteBtnW(g.innerW);
            const int gap = coopui::PanelGapOf(g.rowH);
            const int dy = coopui::PanelSteamLineDy(g.rowH);
            const coopui::PanelPairRect sr = coopui::PanelLabelBoxIn(g.innerW, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotSteamRow, g.rowH),
                                                                     0, invW + gap);
            if (invW < 14 * coopui::kPanelTextCharW || sr.boxX + sr.boxW != g.innerW - invW - gap || sr.boxW <= 0
                || sr.h != 2 * g.rowH || dy <= 0 || dy >= g.rowH / 2)
                Fail("the STEAM row's geometry at " + I(sizes[z][0]) + "x" + I(sizes[z][1]));
        }
    }
    /* the STEAM row follows Steam: one row (its label and INVITE FRIENDS) while Steam runs, two while the no-Steam line shows;
       PLAYERS starts straight under it, so HOSTING is one row shorter while Steam runs */
    if (coopui::PanelSteamRowHalves(1) != 2 || coopui::PanelSteamRowHalves(0) != 4) Fail("the STEAM row's half-rows by Steam state");
    for (int k = 0; k < 2; ++k)
    {
        coopui::PanelLive on, off;
        on.steam = coopui::PanelSteamRowHalves(1);
        off.steam = coopui::PanelSteamRowHalves(0);
        const coopui::PanelBand so = coopui::PanelSlotBand(lays[k], coopui::kSlotSteamRow, 24, on);
        const coopui::PanelBand po = coopui::PanelSlotBand(lays[k], coopui::kSlotPlayers, 24, on);
        const coopui::PanelBand sf = coopui::PanelSlotBand(lays[k], coopui::kSlotSteamRow, 24, off);
        if (so.top != 0 || so.h != 24 || po.top != 24 || sf.h != 48
            || coopui::PanelLayoutHalves(lays[k], on) != coopui::PanelLayoutHalves(lays[k]) - 2
            || coopui::PanelLayoutHalves(lays[k], off) != coopui::PanelLayoutHalves(lays[k]))
            Fail("the STEAM row's height does not follow Steam, layout " + I(lays[k]));
    }
}

/* THE STEAM LISTENER'S DECISIONS (src/common/steamlisten.h): the cap and its test override, the verdict on an arrival and the end reason
   a refused one is closed with, the relay-only route rule, the callback pump's per-frame rule, the relay-only option's read-back, a
   remote id taken only when Steam authenticated it. */
void t_steamlink_listen_verdict()
{
    if (steamlisten::kLinkCap != 32) Fail("the cap is the owner's 32");
    if (steamlisten::CapFrom(0) != 32 || steamlisten::CapFrom(-1) != 32 || steamlisten::CapFrom(33) != 32) Fail("an absent or out-of-range cap is 32");
    if (steamlisten::CapFrom(1) != 1 || steamlisten::CapFrom(3) != 3 || steamlisten::CapFrom(32) != 32) Fail("a cap of 1-32 is kept");
    if (steamlisten::ArrivalVerdict(0, 3, true) != steamlisten::kArriveAccept || steamlisten::ArrivalVerdict(2, 3, true) != steamlisten::kArriveAccept)
        Fail("below the cap is accepted");
    if (steamlisten::ArrivalVerdict(3, 3, true) != steamlisten::kArriveFull || steamlisten::ArrivalVerdict(4, 3, true) != steamlisten::kArriveFull)
        Fail("at the cap is full");
    if (steamlisten::ArrivalVerdict(31, 0, true) != steamlisten::kArriveAccept || steamlisten::ArrivalVerdict(32, 0, true) != steamlisten::kArriveFull)
        Fail("the 33rd is refused at the default cap");
    if (steamlisten::ArrivalVerdict(-1, 3, true) != steamlisten::kArriveAccept) Fail("a negative count reads as none");
    if (steamlisten::ArrivalVerdict(0, 3, false) != steamlisten::kArriveClosed) Fail("a closed listener takes nothing");
    if (steamlisten::CloseReasonFor(steamlisten::kArriveFull) != 1005 || steamlisten::CloseReasonFor(steamlisten::kArriveNoRoom) != 1006
        || steamlisten::CloseReasonFor(steamlisten::kArriveClosed) != 1006 || steamlisten::CloseReasonFor(steamlisten::kArriveAccept) != 0)
        Fail("the end reasons");
    const int reasons[4] = { steamlisten::kEndListenerFull, steamlisten::kEndListenerNoRoom, steamlisten::kEndNotRelayed, steamlisten::kEndHostServerDown };
    for (int i = 0; i < 4; ++i)
        if (reasons[i] < 1005 || reasons[i] > 1999) Fail("an end reason outside Steam's application range or on the transport's 1001-1004");
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (reasons[i] == reasons[j]) Fail("two end reasons are the same");
    if (!steamlisten::RouteAllowed(true, true) || steamlisten::RouteAllowed(true, false)) Fail("a P2P link must be relayed");
    if (!steamlisten::RouteAllowed(false, false) || !steamlisten::RouteAllowed(false, true)) Fail("the relay test's plain UDP is not judged");
    if (!steamlisten::PumpThisTick(true, false, true, false, false)) Fail("the pump runs once Steam is in use");
    if (steamlisten::PumpThisTick(false, false, true, false, false)) Fail("no pump before Steam is in use");
    if (steamlisten::PumpThisTick(true, true, true, false, false)) Fail("no pump after it faulted");
    if (steamlisten::PumpThisTick(true, false, false, false, false)) Fail("no pump without the function");
    if (steamlisten::PumpThisTick(true, false, true, true, false)) Fail("no second pump in a frame the relay wait pumped");
    if (steamlisten::PumpThisTick(true, false, true, false, true)) Fail("no pump after any Steam fault");
    if (!steamlisten::IceOffConfirmed(1, 1, 4, 0) || !steamlisten::IceOffConfirmed(2, 1, 4, 0)) Fail("ICE off read back as an int32 0");
    if (steamlisten::IceOffConfirmed(1, 1, 4, -1) || steamlisten::IceOffConfirmed(1, 1, 4, 1) || steamlisten::IceOffConfirmed(1, 1, 4, 7))
        Fail("ICE on (the default -1, or any candidate kind) read back");
    if (steamlisten::IceOffConfirmed(-1, 1, 4, 0) || steamlisten::IceOffConfirmed(-2, 1, 4, 0) || steamlisten::IceOffConfirmed(-3, 1, 4, 0)
        || steamlisten::IceOffConfirmed(0, 1, 4, 0) || steamlisten::IceOffConfirmed(3, 1, 4, 0))
        Fail("a read Steam did not answer OK");
    if (steamlisten::IceOffConfirmed(1, 3, 4, 0) || steamlisten::IceOffConfirmed(1, 1, 8, 0) || steamlisten::IceOffConfirmed(1, 1, 0, 0))
        Fail("a read of another type or size");
    if (!steamlisten::IdAuthenticated(0) || !steamlisten::IdAuthenticated(16) || steamlisten::IdAuthenticated(1)
        || steamlisten::IdAuthenticated(1 | 16))
        Fail("a remote id is taken only when Steam authenticated the connection");
}

/* THE STEAM LISTENER'S LINK TABLE (steamlisten.h): places and ids, the cap, a handle held once, a freed place reused under a NEW id, a
   full table of 32 refusing the 33rd, and 1,000 rounds of one link leaving and one arriving. */
void t_steamlink_listen_table()
{
    steamlisten::LinkTable t;
    steamlisten::Reset(&t);
    unsigned int id = 0;
    if (steamlisten::Add(&t, 101, 3, &id) != 0 || id != 1 || t.count != 1) Fail("the first link");
    if (steamlisten::Add(&t, 102, 3, &id) != 1 || id != 2) Fail("the second link");
    if (steamlisten::Add(&t, 102, 3, &id) != -1) Fail("a handle held twice");
    if (steamlisten::Add(&t, 0, 3, &id) != -1) Fail("handle 0");
    if (steamlisten::Add(&t, 103, 3, &id) != 2 || id != 3) Fail("the third link");
    if (steamlisten::Add(&t, 104, 3, &id) != -1 || t.count != 3) Fail("the 4th past a cap of 3");
    if (steamlisten::SlotOfId(t, 2) != 1 || steamlisten::SlotOfHandle(t, 103) != 2) Fail("the lookups");
    if (steamlisten::SlotOfId(t, 0) != -1 || steamlisten::SlotOfId(t, 9) != -1 || steamlisten::SlotOfHandle(t, 0) != -1
        || steamlisten::SlotOfHandle(t, 999) != -1)
        Fail("a lookup of nothing");
    if (!steamlisten::RemoveSlot(&t, 1) || t.count != 2 || steamlisten::SlotOfId(t, 2) != -1 || steamlisten::SlotOfHandle(t, 102) != -1)
        Fail("a link removed");
    if (steamlisten::RemoveSlot(&t, 1)) Fail("a place freed twice");
    if (steamlisten::RemoveSlot(&t, -1) || steamlisten::RemoveSlot(&t, steamlisten::kLinkCap)) Fail("a place out of range");
    if (steamlisten::Add(&t, 104, 3, &id) != 1 || id != 4 || t.count != 3) Fail("a freed place is reused under a new id");
    steamlisten::LinkTable f;
    steamlisten::Reset(&f);
    for (unsigned int h = 1; h <= 32; ++h)
        if (steamlisten::Add(&f, 1000 + h, 0, &id) < 0 || id != h) { Fail("32 links at the default cap"); break; }
    if (steamlisten::Add(&f, 2000, 0, &id) != -1 || f.count != 32) Fail("the 33rd at the default cap");
    unsigned int last = 32;
    for (int k = 0; k < 1000; ++k)
    {
        const int s = k % 32;
        if (!steamlisten::RemoveSlot(&f, s) || steamlisten::Add(&f, 5000 + (unsigned int)k, 0, &id) != s || id != last + 1 || f.count != 32)
        {
            Fail("one leaves, one arrives");
            break;
        }
        last = id;
    }
}

/* relaytest=many:<port>:<cap>:<dials> (cfgtext): read with its cap and dials, refused out of range or malformed, never written. */
void t_steamlink_relaytest_many_cfg()
{
    long long bad = -1;
    coopcfg::CfgFields f;
    coopcfg::CfgParseText("relaytest=many:27200:3:5\r\n", &f, 0, 0, 0, &bad);
    if (bad != 0 || f.relayTestMode != coopcfg::kRelayTestMany || f.relayTestPort != 27200 || f.relayTestCap != 3 || f.relayTestDials != 5)
        Fail("many with a cap of 3 and 5 dials");
    coopcfg::CfgFields g;
    bad = -1;
    coopcfg::CfgParseText("relaytest=MANY:27200:32:36\r\n", &g, 0, 0, 0, &bad);
    if (bad != 0 || g.relayTestMode != coopcfg::kRelayTestMany || g.relayTestCap != 32 || g.relayTestDials != 36) Fail("many at its limits");
    if (coopcfg::kRelayManyCapMax != steamlisten::kLinkCap) Fail("the test's cap limit is the listener's cap");
    coopcfg::CfgFields p;
    bad = -1;
    coopcfg::CfgParseText("relaytest=pair\r\n", &p, 0, 0, 0, &bad);
    if (bad != 0 || p.relayTestCap != 0 || p.relayTestDials != 0) Fail("pair carries no cap or dials");
    const char* const refused[] = { "relaytest=many:27200", "relaytest=many:27200:3", "relaytest=many:27200:0:5", "relaytest=many:27200:33:5",
                                    "relaytest=many:27200:3:0", "relaytest=many:27200:3:37", "relaytest=many:0:3:5", "relaytest=many:27200:3:5:1",
                                    "relaytest=many:27200:x:5", "relaytest=many:27200:-1:5", "relaytest=many:27200:003:5", "relaytest=many:" };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
        coopcfg::CfgFields x;
        long long b = -1;
        coopcfg::CfgParseText(std::string(refused[i]) + "\r\n", &x, 0, 0, 0, &b);
        if (b != 1 || x.relayTestMode != coopcfg::kRelayTestNone || x.relayTestCap != 0 || x.relayTestDials != 0)
            Fail(std::string("not refused: ") + refused[i]);
    }
    if (coopcfg::CfgFormat(f).find("relaytest") != std::string::npos) Fail("a writer carries relaytest=many");
}

/* T-290 S2 - THE STEAM ROUTE (src/common/relaybridge.h): the steam:<id> address and the address rule, the route a dial takes and its
   connect budget, when the host's bridge is open, the bridge's limits, and the test keys. */
void t_t290s2_steam_address()
{
    unsigned long long id = 0;
    if (!relaybridge::SteamAddressId("steam:76561197960287930", &id) || id != 76561197960287930ULL) Fail("a 17-digit id");
    if (!relaybridge::SteamAddressId("STEAM:1", &id) || id != 1ULL) Fail("any case, one digit");
    if (!relaybridge::SteamAddressId("steam:18446744073709551615", &id) || id != 18446744073709551615ULL) Fail("the largest 64-bit id");
    const char* const bad[] = { "steam:", "steam:0", "steam:00", "steam:18446744073709551616", "steam:12a", "steam: 1", "stean:1", "steam:-1",
                                "76561197960287930", "steam:765611979602879300000000", "127.0.0.1", "" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (relaybridge::IsSteamAddress(bad[i])) Fail(std::string("taken as a Steam address: ") + bad[i]);
    if (relaybridge::SteamAddressText(76561197960287930ULL) != "steam:76561197960287930" || relaybridge::SteamAddressText(1ULL) != "steam:1")
        Fail("the dial form");
    std::string a;
    unsigned short p = 0;
    if (!relaybridge::SplitSteamValue("Steam:0076561197960287930", &a, &p) || a != "steam:76561197960287930" || p != relaybridge::kSteamRoutePort)
        Fail("case and leading zeros go in the dial form, with the route port");
    if (relaybridge::AddrForLog("steam:76561197960287930") != "steam:<id>" || relaybridge::AddrForLog("127.0.0.1") != "127.0.0.1")
        Fail("a log line never shows the id");
    if (!coopcfg::CfgSplitAddr("steam:76561197960287930", &a, &p) || a != "steam:76561197960287930" || p != relaybridge::kSteamRoutePort)
        Fail("CfgSplitAddr takes a whole steam:<id>");
    if (!coopcfg::CfgSplitAddr("steam:76561197960287930:7777", &a, &p) || a != "steam:76561197960287930" || p != 7777)
        Fail("steam:<id>:<port> keeps the last-colon rule");
    if (!coopcfg::CfgSplitAddr("192.168.1.20:7777", &a, &p) || a != "192.168.1.20" || p != 7777) Fail("an ordinary address is unchanged");
    std::string why;
    if (!coopcfg::CfgAddrFieldOk("  steam:76561197960287930 ", "address", &a, &p, &why) || a != "steam:76561197960287930" || p != relaybridge::kSteamRoutePort)
        Fail("the JOIN box takes steam:<id>");
    if (coopcfg::CfgAddrFieldOk("steam:0", "address", &a, &p, &why)) Fail("the JOIN box refuses steam:0");
    coopcfg::CfgFields f;
    long long b = -1;
    coopcfg::CfgParseText("role=client\r\nhost=steam:76561197960287930\r\n", &f, 0, 0, 0, &b);
    if (b != 0 || f.hostAddr != "steam:76561197960287930" || f.hostPort != relaybridge::kSteamRoutePort) Fail("host=steam:<id> in the settings file");
    coopcfg::CfgFields g;
    b = -1;
    coopcfg::CfgParseText(coopcfg::CfgFormat(f), &g, 0, 0, 0, &b);
    if (b != 0 || g.hostAddr != f.hostAddr || g.hostPort != f.hostPort) Fail("the writer's text reads back the same steam:<id>");
}

void t_t290s2_route()
{
    if (relaybridge::RouteFor("steam:76561197960287930", 0) != relaybridge::kRouteSteam) Fail("steam:<id> takes Steam");
    if (relaybridge::RouteFor("127.0.0.1", 0) != relaybridge::kRouteDirect) Fail("an IP address takes the direct route");
    if (relaybridge::RouteFor("example.org", 0) != relaybridge::kRouteDirect) Fail("a host name takes the direct route");
    if (relaybridge::RouteFor("127.0.0.1", 27210) != relaybridge::kRouteSteamIpTest || relaybridge::RouteFor("steam:1", 27210) != relaybridge::kRouteSteamIpTest)
        Fail("relayvia=ip sends every dial over Steam's plain UDP");
    if (relaybridge::ConnectBudgetMs(relaybridge::kRouteDirect, 5000) != 5000) Fail("a direct dial keeps its budget");
    if (relaybridge::ConnectBudgetMs(relaybridge::kRouteSteam, 5000) != 12000 || relaybridge::ConnectBudgetMs(relaybridge::kRouteSteamIpTest, 5000) != 12000)
        Fail("a Steam dial outlasts Steam's own 10 s first-connect timeout");
    if (relaybridge::ConnectBudgetMs(relaybridge::kRouteSteam, 15000) != 15000) Fail("a longer budget is kept");
    if (std::string(relaybridge::RouteToken(relaybridge::kRouteSteam)) != "steam" || std::string(relaybridge::RouteToken(relaybridge::kRouteSteamIpTest)) != "steamIp"
        || std::string(relaybridge::RouteToken(relaybridge::kRouteDirect)) != "direct")
        Fail("the route tokens");
}

void t_t290s2_bridge_want()
{
    const char* const loop[] = { "127.0.0.1", "127.1.2.3", "LocalHost", "127.255.255.255" };
    for (size_t i = 0; i < sizeof(loop) / sizeof(loop[0]); ++i)
        if (!relaybridge::IsLoopbackHost(loop[i])) Fail(std::string("not taken as this PC: ") + loop[i]);
    const char* const notLoop[] = { "", "127.0.0", "127.0.0.1.5", "128.0.0.1", "127.0.0.1:7777", "127..0.1", "127.0.0.1234", "192.168.1.20",
                                    "steam:1", "localhost2", "127.0.0.", "0.0.0.0" };
    for (size_t i = 0; i < sizeof(notLoop) / sizeof(notLoop[0]); ++i)
        if (relaybridge::IsLoopbackHost(notLoop[i])) Fail(std::string("taken as this PC: ") + notLoop[i]);
    if (relaybridge::BridgeWant(true, true, 7777, false, 0, relaybridge::kSteamReady) != relaybridge::kWantOpen) Fail("a host of a world server here, with Steam, opens it");
    if (relaybridge::BridgeWant(false, true, 7777, false, 0, relaybridge::kSteamReady) != relaybridge::kWantClosed) Fail("a joiner opens no bridge");
    if (relaybridge::BridgeWant(true, false, 7777, false, 0, relaybridge::kSteamReady) != relaybridge::kWantClosed) Fail("a world server elsewhere is not bridged");
    if (relaybridge::BridgeWant(true, true, 0, false, 0, relaybridge::kSteamReady) != relaybridge::kWantClosed) Fail("no port, no bridge");
    if (relaybridge::BridgeWant(true, true, 7777, true, 0, relaybridge::kSteamReady) != relaybridge::kWantClosed) Fail("relaybridge=off");
    if (relaybridge::BridgeWant(true, true, 7777, false, 0, relaybridge::kSteamNotYet) != relaybridge::kWantWait) Fail("it waits for Steam's look-up");
    if (relaybridge::BridgeWant(true, true, 7777, false, 0, relaybridge::kSteamAbsent) != relaybridge::kWantNoSteam) Fail("no Steam");
    if (relaybridge::BridgeWant(true, true, 7777, false, 7777, relaybridge::kSteamReady) != relaybridge::kWantBadPort) Fail("the test listener cannot take the world server's port");
    if (relaybridge::BridgeWant(true, true, 7777, false, 27210, relaybridge::kSteamReady) != relaybridge::kWantOpen) Fail("the test listener on its own port");
}

void t_t290s2_backpressure()
{
    if (relaybridge::SlowNext(false, relaybridge::kSlowAtBytes - 1) || !relaybridge::SlowNext(false, relaybridge::kSlowAtBytes)) Fail("slow from 4 MB waiting");
    if (!relaybridge::SlowNext(true, relaybridge::kFastAtBytes + 1) || relaybridge::SlowNext(true, relaybridge::kFastAtBytes)) Fail("every frame again at 1 MB");
    if (!relaybridge::SlowNext(true, relaybridge::kSlowAtBytes - 1)) Fail("between the marks a slow link stays slow");
    if (!relaybridge::ReadLocalNow(false, 0) || relaybridge::ReadLocalNow(true, relaybridge::kSlowPollMs - 1) || !relaybridge::ReadLocalNow(true, relaybridge::kSlowPollMs))
        Fail("the read pace");
    if (relaybridge::kSlowPollMs >= 5000u) Fail("a slow read comes well inside ENet's 5 s minimum timeout");
    if (relaybridge::FarBehind(relaybridge::kFarBehindBytes) || !relaybridge::FarBehind(relaybridge::kFarBehindBytes + 1)) Fail("far behind past 64 MB");
    if (relaybridge::kFarBehindBytes < 2 * (long long)relaysplit::kRejoinMax) Fail("one largest message always fits");
    if (!(relaybridge::kFastAtBytes < relaybridge::kSlowAtBytes && relaybridge::kSlowAtBytes < relaybridge::kFarBehindBytes)) Fail("the marks are in order");
    if (relaybridge::LocalConnectLate(relaybridge::kLocalConnectMs - 1) || !relaybridge::LocalConnectLate(relaybridge::kLocalConnectMs)) Fail("the world-server connect budget");
    if (relaybridge::StallWorthALine(relaybridge::kStallLineMs, 0) || relaybridge::StallWorthALine(relaybridge::kStallLineMs - 1, 1)
        || !relaybridge::StallWorthALine(relaybridge::kStallLineMs, 1)) Fail("the stall line");
    if (!relaybridge::LogThis(1) || !relaybridge::LogThis(5) || relaybridge::LogThis(6) || !relaybridge::LogThis(100) || relaybridge::LogThis(0)) Fail("the first 5 and every 100th");
}

/* T-290 S2 - the Steam route: a log line never shows what follows "steam:", the player never sees the route's stand-in
   port, the end reason and words a held arrival is refused with, and how long closing the whole bridge waits gracefully. */
void t_t290s2_bridge_fold()
{
    const char* const hidden[] = { "steam:123", "STEAM:123:7777", "steam:abc", "steam:", "Steam:76561197960287930", "sTeAm:1:1" };
    for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i)
        if (relaybridge::AddrForLog(hidden[i]) != "steam:<id>") Fail(std::string("a log line shows what follows steam: in ") + hidden[i]);
    if (relaybridge::AddrForLog("1.2.3.4") != "1.2.3.4" || relaybridge::AddrForLog("localhost") != "localhost" || relaybridge::AddrForLog("") != ""
        || relaybridge::AddrForLog("steam") != "steam" || relaybridge::AddrForLog("steamy:1") != "steamy:1" || relaybridge::AddrForLog(" steam:1") != " steam:1")
        Fail("an address that does not start with steam: is logged as given");
    if (relaybridge::AddrPortForPlayer("steam:76561197960287930", relaybridge::kSteamRoutePort) != "steam:76561197960287930")
        Fail("a steam:<id> address shows without the route's stand-in port");
    if (relaybridge::AddrPortForPlayer("1.2.3.4", 7777) != "1.2.3.4:7777" || relaybridge::AddrPortForPlayer("example.org", 65535) != "example.org:65535"
        || relaybridge::AddrPortForPlayer("steam:abc", 1) != "steam:abc:1")
        Fail("any other address shows with its port");
    if (coopui::PanelAddrText("steam:76561197960287930", relaybridge::kSteamRoutePort) != "steam:76561197960287930"
        || coopui::PanelAddrText("1.2.3.4", 7777) != "1.2.3.4:7777" || coopui::PanelAddrText("steam:1", 0) != "none")
        Fail("the panel's address text");
    if (steamlisten::kEndHostServerDown != 1008 || std::string(steamlisten::EndText(steamlisten::kEndHostServerDown)) != "the host's world server is not answering")
        Fail("a held arrival whose world server does not answer is refused with 1008 and its words");
    if (steamlisten::EndText(0) != 0 || steamlisten::EndText(1001) != 0 || steamlisten::EndText(steamlisten::kEndListenerFull) == 0
        || steamlisten::EndText(steamlisten::kEndListenerNoRoom) == 0 || steamlisten::EndText(steamlisten::kEndNotRelayed) == 0)
        Fail("the listener's end reasons carry a text, no other does");
    if (relaybridge::kCloseAllGracefulMs != 300u || relaybridge::kCloseAllBudgetMs != 600u)
        Fail("closing the whole bridge: a graceful close waits 300 ms at most, the whole close has 600 ms");
    if (!relaybridge::CloseGracefully(0) || !relaybridge::CloseGracefully(relaybridge::kCloseAllBudgetMs - relaybridge::kCloseAllGracefulMs)
        || relaybridge::CloseGracefully(relaybridge::kCloseAllBudgetMs - relaybridge::kCloseAllGracefulMs + 1)
        || relaybridge::CloseGracefully(60000) || relaybridge::CloseGracefully(0xFFFFFFFFu))
        Fail("a graceful close starts only while at least 300 ms of the whole close's 600 ms remain");
}

/* T-290 S2 - a joiner's world-server side while its Steam link comes up (relaybridge.h JoinerSideStep), and a notebook dial
   reason that names a steam:<id> host without its id. */
void t_t290s2_joiner_wait()
{
    if (relaybridge::JoinerSideStep(false, false) != relaybridge::kJoinerDialling || relaybridge::JoinerSideStep(false, true) != relaybridge::kJoinerDialling)
        Fail("a held arrival: its world-server connection is read every frame and what it hands up waits");
    if (relaybridge::JoinerSideStep(true, false) != relaybridge::kJoinerHold) Fail("accepted, its Steam link still connecting: what comes waits");
    if (relaybridge::JoinerSideStep(true, true) != relaybridge::kJoinerPass) Fail("accepted and up: what waited goes first, then everything goes on");
    int sw = -1; std::string why;
    if (coopstore::NotebookDialFromWelcome("127.0.0.1", "", "steam:76561197960287930", "", &sw, &why) != "steam:76561197960287930" || sw != 1)
        Fail("a loopback notebook becomes the steam:<id> host the join was dialled at");
    if (why.find("76561197960287930") != std::string::npos || why.find("steam:<id>") == std::string::npos)
        Fail("the dial reason names a steam:<id> host without its id");
    why.clear();
    coopstore::NotebookDialFromWelcome("127.0.0.1", "192.168.1.20", "", "", &sw, &why);
    if (why.find("192.168.1.20") == std::string::npos) Fail("the dial reason names an IP host as it is");
}

struct T290s2Cfg { const char* text; int mode; unsigned short bport; unsigned short via; int cap; long long bad; };
void t_t290s2_cfg_keys()
{
    const T290s2Cfg cases[] = {
        { "relaybridge=off\r\n", coopcfg::kRelayBridgeOff, 0, 0, 0, 0 },
        { "relaybridge=on\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 0 },
        { "relaybridge=ip:27210\r\n", coopcfg::kRelayBridgeIp, 27210, 0, 0, 0 },
        { "relaybridge=IP:27210\r\n", coopcfg::kRelayBridgeIp, 27210, 0, 0, 0 },
        { "relayvia=ip:27210\r\n", coopcfg::kRelayBridgeOn, 0, 27210, 0, 0 },
        { "relaycap=1\r\n", coopcfg::kRelayBridgeOn, 0, 0, 1, 0 },
        { "relaycap=32\r\n", coopcfg::kRelayBridgeOn, 0, 0, 32, 0 },
        { "relaybridge=ip:0\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaybridge=ip:\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaybridge=ip:70000\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaybridge=steam\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relayvia=ip:0\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relayvia=127.0.0.1:27210\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relayvia=on\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaycap=0\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaycap=33\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaycap=x\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 },
        { "relaycap=001\r\n", coopcfg::kRelayBridgeOn, 0, 0, 0, 1 }
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        coopcfg::CfgFields f;
        long long b = -1;
        coopcfg::CfgParseText(cases[i].text, &f, 0, 0, 0, &b);
        if (b != cases[i].bad || f.relayBridgeMode != cases[i].mode || f.relayBridgePort != cases[i].bport || f.relayViaPort != cases[i].via
            || f.relayCap != cases[i].cap)
            Fail(std::string("relay key case: ") + cases[i].text);
    }
    if (coopcfg::kRelayCapMax != steamlisten::kLinkCap) Fail("the test cap's limit is the listener's cap");
    coopcfg::CfgFields w;
    long long b = -1;
    coopcfg::CfgParseText("role=host\r\nrelaybridge=ip:27210\r\nrelayvia=ip:27210\r\nrelaycap=1\r\n", &w, 0, 0, 0, &b);
    const std::string txt = coopcfg::CfgFormat(w);
    if (b != 0 || txt.find("relaybridge") != std::string::npos || txt.find("relayvia") != std::string::npos || txt.find("relaycap") != std::string::npos)
        Fail("a writer carries a relay test key");
}

/* the UTF-8 <-> UTF-16 pair the plugin's wide file calls convert through (src/common/u8path.h). */
std::wstring T602W(const unsigned short* u, size_t n) { std::wstring w; for (size_t i = 0; i < n; ++i) w.push_back((wchar_t)u[i]); return w; }
void T602Round(const char* what, const std::string& s, const unsigned short* want, size_t n)
{
    std::wstring w;
    if (!u8path::Utf8Valid(s)) Fail(std::string(what) + ": well-formed UTF-8 is called malformed");
    if (!u8path::Utf8ToUtf16(s, &w)) Fail(std::string(what) + ": the decode reported a bad byte");
    if (w != T602W(want, n)) Fail(std::string(what) + ": the UTF-16 units differ");
    if (u8path::Utf16ToUtf8(w) != s) Fail(std::string(what) + ": UTF-16 back to UTF-8 is not the same bytes");
}
void T602Bad(const char* what, const std::string& s, const unsigned short* want, size_t n)
{
    std::wstring w;
    if (u8path::Utf8Valid(s)) Fail(std::string(what) + ": malformed bytes are called UTF-8");
    if (u8path::Utf8ToUtf16(s, &w)) Fail(std::string(what) + ": the decode did not report the bad byte");
    if (w != T602W(want, n)) Fail(std::string(what) + ": each bad byte is not one U+FFFD");
}
void t_t602_utf8_utf16()
{
    { const unsigned short u[] = { 'a', 'b', 'c' }; T602Round("ascii", "abc", u, 3); }
    { const unsigned short u[] = { 0x042F }; T602Round("Cyrillic YA", "\xD0\xAF", u, 1); }
    { const unsigned short u[] = { 'C', ':', '\\', 'U', 's', 'e', 'r', 's', '\\', 0x042F, '\\', 's', 'a', 'v', 'e' };
      T602Round("a save folder under a Cyrillic user name", "C:\\Users\\\xD0\xAF\\save", u, 15); }
    { const unsigned short u[] = { 0x4E2D, 0x6587 }; T602Round("CJK", "\xE4\xB8\xAD\xE6\x96\x87", u, 2); }
    { const unsigned short u[] = { 0xD83D, 0xDE00, '!' }; T602Round("emoji (surrogate pair)", "\xF0\x9F\x98\x80!", u, 3); }
    { const unsigned short u[] = { 0x00E9 }; T602Round("Latin-1 letter", "\xC3\xA9", u, 1); }
    { const unsigned short u[] = { 0xFFFD }; T602Bad("a lone ANSI (cp1251) YA byte", "\xDF", u, 1); }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD }; T602Bad("an ANSI (cp1251) name", "\xC8\xE2\xE0\xED", u, 4); }
    { const unsigned short u[] = { 'a', 0xFFFD, 0xFFFD, 'b' }; T602Bad("overlong", "a\xC0\xAF" "b", u, 4); }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD }; T602Bad("encoded surrogate", "\xED\xA0\x80", u, 3); }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD }; T602Bad("cut short", "\xE4\xB8", u, 2); }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD }; T602Bad("above U+10FFFF", "\xF4\x90\x80\x80", u, 4); }
    { const unsigned short u[] = { 0xFFFD, 'x' }; T602Bad("stray continuation byte", "\x80x", u, 2); }
    /* UTF-16 that is not well-formed: an unpaired surrogate becomes EF BF BD */
    { const unsigned short u[] = { 0xD83D, 'x' }; if (u8path::Utf16ToUtf8(T602W(u, 2)) != "\xEF\xBF\xBDx") Fail("an unpaired high surrogate is not U+FFFD"); }
    { const unsigned short u[] = { 0xDE00 }; if (u8path::Utf16ToUtf8(T602W(u, 1)) != "\xEF\xBF\xBD") Fail("a lone low surrogate is not U+FFFD"); }
    if (!u8path::Utf8Valid(std::string())) Fail("the empty string is well-formed");
}

/* a squad's record key (coopown::SquadKey of the engine's UTF-8 squad id) and the restore's lookup (SquadKey of
   the stem a wide find loop returns, converted back to UTF-8) agree for any name; reserved characters stay out. */
void t_t602_squad_key_unicode()
{
    const char* const sids[] = {
        "Holy Nation Outlaws_28",
        "\xD0\x9E\xD1\x82\xD1\x80\xD1\x8F\xD0\xB4 1",          /* Cyrillic */
        "\xE5\xB0\x8F\xE9\x98\x9F_3",                          /* CJK */
        "\xF0\x9F\x98\x80 squad",                              /* emoji */
        "a<b>c:d\"e/f\\g|h?i*j\x01k"                           /* every reserved character and a control byte */
    };
    for (size_t i = 0; i < sizeof(sids) / sizeof(sids[0]); ++i)
    {
        const std::string sid(sids[i]);
        std::wstring w;
        u8path::Utf8ToUtf16(sid, &w);
        const std::string stem = u8path::Utf16ToUtf8(w);      /* what U8FindNextFile hands the scan for <sid>.platoon */
        const std::string writer = coopown::SquadKey(sid), lookup = coopown::SquadKey(stem);
        if (writer.empty() || writer != lookup) Fail("the backup's key and the restore's lookup differ for '" + writer + "'");
        for (size_t k = 0; k < writer.size(); ++k)
        {
            const unsigned char c = (unsigned char)writer[k];
            if (c < 0x20 || c >= 0x7F || std::strchr("<>:\"/\\|?* ", (int)c) != 0) Fail("a key holds a character a file name must not: " + writer);
        }
    }
    /* an A-call find loop's stem (the ANSI code page, cp1251 here) for the same Cyrillic squad gives another key - why the scan is wide */
    if (coopown::SquadKey("\xCE\xF2\xF0\xFF\xE4 1") == coopown::SquadKey(sids[1])) Fail("the cp1251 stem should not match the UTF-8 key");
    /* a ten-letter Cyrillic squad name's key: ten letters (two UTF-8 bytes each) and a space give 21 '_' before "0" */
    const std::string cyr = coopown::SquadKey("\xD0\x9E\xD1\x82\xD1\x80\xD1\x8F\xD0\xB4\xD0\x92\xD0\xBE\xD0\xBB\xD0\xBA\xD0\xB0 0");
    if (cyr.compare(0, 9 + 22 + 1, "pp.squad." + std::string(21, '_') + "0.") != 0) Fail("a ten-letter Cyrillic name's key shape is not the UTF-8 name's: " + cyr);
    if (coopown::SquadKey("a b") == coopown::SquadKey("a_b")) Fail("two ids cleaned to the same text share a key");
}

/* the edges of each UTF-8 length: the lowest and highest code point that takes 1..4 bytes round-trip; lead bytes that
   can never start a sequence (F5..FF) and 3- and 4-byte overlong forms are refused, one U+FFFD per byte. */
void t_t602_utf8_boundaries()
{
    { const unsigned short u[] = { 0x0080 }; T602Round("U+0080", "\xC2\x80", u, 1); }
    { const unsigned short u[] = { 0x07FF }; T602Round("U+07FF", "\xDF\xBF", u, 1); }
    { const unsigned short u[] = { 0x0800 }; T602Round("U+0800", "\xE0\xA0\x80", u, 1); }
    { const unsigned short u[] = { 0xFFFF }; T602Round("U+FFFF", "\xEF\xBF\xBF", u, 1); }
    { const unsigned short u[] = { 0xD800, 0xDC00 }; T602Round("U+10000", "\xF0\x90\x80\x80", u, 2); }
    { const unsigned short u[] = { 0xDBFF, 0xDFFF }; T602Round("U+10FFFF", "\xF4\x8F\xBF\xBF", u, 2); }
    for (int b = 0xF5; b <= 0xFF; ++b)
    {
        const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD };
        const std::string what = std::string("lead byte ") + "0123456789ABCDEF"[b >> 4] + "0123456789ABCDEF"[b & 15];
        T602Bad(what.c_str(), std::string(1, (char)b) + "\x80\x80\x80", u, 4);
    }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD }; T602Bad("3-byte overlong (E0 80 80)", "\xE0\x80\x80", u, 3); }
    { const unsigned short u[] = { 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD }; T602Bad("4-byte overlong (F0 80 80 80)", "\xF0\x80\x80\x80", u, 4); }
}

/* a 260-character path (C:\ and 257 Cyrillic YA) is 517 UTF-8 bytes and 260 UTF-16 units, and comes back the same bytes */
void t_t602_long_cyrillic_path()
{
    std::string s("C:\\");
    std::vector<unsigned short> u;
    u.push_back('C'); u.push_back(':'); u.push_back('\\');
    for (int i = 0; i < 257; ++i) { s += "\xD0\xAF"; u.push_back(0x042F); }
    if (s.size() != 517) Fail("the 260-character Cyrillic path is not 517 UTF-8 bytes");
    T602Round("260-character Cyrillic path", s, &u[0], u.size());
    std::wstring w;
    u8path::Utf8ToUtf16(s, &w);
    if (w.size() != 260) Fail("the 260-character Cyrillic path is not 260 UTF-16 units");
}

/* ---- T-636: AN EMPTY TOWN THAT NOTHING ACCOUNTS FOR IS NAMED IN ONE LOG LINE (src/common/emptytown.h): every silent gate, a town
   with people, each in-flight bit, each accounting reason, the unexplained verdict, the once-per-town rule and the line ---- */
void t_t636_empty_town()
{
    using namespace emptytown;
    Facts base; base.held = 1; base.live = 1; base.settled = 1; base.pushDone = 1; base.readOk = 1;
    if (GateVerdict(base) != kGatesPass) Fail("the gates did not pass for a held, live, settled town with the push done");
    if (Decide(base) != kUnexplained) Fail("an empty town with every answer zero is not unexplained");
    /* silent */
    { Facts f = base; f.held = 0; if (Decide(f) != kSilentNotHeld || !IsSilent(Decide(f))) Fail("a town this game does not hold was judged"); }
    { Facts f = base; f.frozen = 1; if (Decide(f) != kSilentFrozen || !IsSilent(Decide(f))) Fail("a frozen area's town was judged"); }
    { Facts f = base; f.frozen = 1; f.held = 0; if (Decide(f) != kSilentFrozen) Fail("a frozen area was not told apart from one not held"); }
    { Facts f = base; f.live = 0; if (Decide(f) != kSilentNotLive || !IsSilent(Decide(f))) Fail("a town whose zone is not live was judged"); }
    { Facts f = base; f.settled = 0; if (Decide(f) != kSettling || IsSilent(Decide(f)) || IsAccounted(Decide(f))) Fail("a town still settling was judged, or counted as silent"); }
    { Facts f = base; f.pushDone = 0; if (Decide(f) != kSilentPush || !IsSilent(Decide(f))) Fail("a town was judged before the push and the snapshot were in"); }
    { Facts f = base; f.playerTown = 1; if (Decide(f) != kSilentPlayer || !IsSilent(Decide(f))) Fail("a player's town was judged"); }
    { Facts f = base; f.readOk = 0; if (Decide(f) != kSilentUnread || !IsSilent(Decide(f))) Fail("a town whose lists could not be read was judged"); }
    { Facts f = base; f.readOk = 0; f.listLiving = 3; if (Decide(f) != kSilentUnread) Fail("an unread town was judged on its half-read lists"); }
    /* not empty */
    { Facts f = base; f.listLiving = 1; if (Decide(f) != kNotEmpty) Fail("a living resident did not make the town not empty"); }
    { Facts f = base; f.homeLiving = 1; if (Decide(f) != kNotEmpty) Fail("a living person of the home town did not make the town not empty"); }
    { Facts f = base; f.patrolLiving = 4; if (Decide(f) != kUnexplained) Fail("a living patrol counted as a resident"); }
    { Facts f = base; f.listLiving = 1; f.owedRows = 1; f.recLiving = 1; if (Decide(f) != kNotEmpty) Fail("work in flight or a record outranked living people"); }
    /* in flight, each bit */
    { Facts f = base; f.owedRows = 2; if (Decide(f) != kInFlight || InFlightBits(f) != kInOwedRow) Fail("an owed row was not work in flight"); }
    { Facts f = base; f.aside = 1; if (Decide(f) != kInFlight || InFlightBits(f) != kInAside) Fail("set-aside work was not work in flight"); }
    { Facts f = base; f.barPend = 1; if (Decide(f) != kInFlight || InFlightBits(f) != kInBarPend) Fail("a waiting bar list was not work in flight"); }
    { Facts f = base; f.noteHeld = 1; if (Decide(f) != kInFlight || InFlightBits(f) != kInNoteHeld) Fail("a note holding the creation was not work in flight"); }
    { Facts f = base; f.rebuildQueued = 1; if (Decide(f) != kInFlight || InFlightBits(f) != kInRebuild) Fail("a queued group rebuild was not work in flight"); }
    { Facts f = base; f.barPend = 1; f.recLiving = 1; f.loss = 1; if (Decide(f) != kInFlight) Fail("an accounting reason outranked work in flight"); }
    /* accounted, each reason */
    { Facts f = base; f.recLiving = 1; f.recPlacedHere = 1; if (Decide(f) != kAccRecords || !IsAccounted(Decide(f))) Fail("a living record placed here did not account for the town"); }
    { Facts f = base; f.recLiving = 1; f.recUnplaceable = 1; if (Decide(f) != kAccRecords) Fail("an unplaceable living record did not account for the town"); }
    { Facts f = base; f.recLiving = 1; f.recCreateFailed = 1; if (Decide(f) != kAccRecords) Fail("a record whose creation failed did not account for the town"); }
    { Facts f = base; f.recLiving = 1; f.recElsewhere = 1; if (Decide(f) != kAccRecords) Fail("a record in another game's area did not account for the town"); }
    { Facts f = base; f.recDeleted = 1; if (Decide(f) != kAccDeaths) Fail("a deleted record did not account for the town"); }
    { Facts f = base; f.gone = 1; if (Decide(f) != kAccDeaths) Fail("a gone list entry did not account for the town"); }
    { Facts f = base; f.deadMembers = 5; if (Decide(f) != kAccDeaths) Fail("dead members did not account for the town"); }
    { Facts f = base; f.townDead = 1; if (Decide(f) != kAccDeaths) Fail("the town's dead flag did not account for the town"); }
    { Facts f = base; f.goneThisLaunch = 1; if (Decide(f) != kAccDeaths) Fail("a group deleted this launch did not account for the town"); }
    { Facts f = base; f.copies = 2; if (Decide(f) != kAccCopies || !IsAccounted(Decide(f))) Fail("its people in a player's faction did not account for the town"); }
    { Facts f = base; f.loss = 1; if (Decide(f) != kAccLoss || !IsAccounted(Decide(f))) Fail("a recorded loss did not account for the town"); }
    { Facts f = base; f.refilling = 1; if (Decide(f) != kAccRefilling || !IsAccounted(Decide(f))) Fail("a refill in progress did not account for the town"); }
    { Facts f = base; f.listed = 3; f.patrolLiving = 2; if (Decide(f) != kUnexplained || IsAccounted(Decide(f)) || IsSilent(Decide(f))) Fail("listed groups with no dead and no gone accounted for the town"); }
    for (int v = 0; v < kVerdictN; ++v)
    {
        if (std::string(emptytown::VerdictName(v)) == "?") Fail("a verdict has no name");
        for (int w = v + 1; w < kVerdictN; ++w) if (std::string(emptytown::VerdictName(v)) == emptytown::VerdictName(w)) Fail("two verdicts share a name");
    }
    if (std::string(emptytown::VerdictName(kVerdictN)) != "?" || std::string(emptytown::VerdictName(-1)) != "?") Fail("an out-of-range verdict has a name");
    /* once per town; again only when the answers change; re-armed; the cap */
    {
        std::string next;
        const std::string s1 = Sig(base);
        Facts c = base; c.patrolLiving = 2; const std::string s2 = Sig(c);
        if (s1.empty() || s1 == s2) Fail("the answers' signature does not tell two looks apart");
        if (ShouldSay("", s1, kUnexplained, 0, &next) != kSaySay || next != s1) Fail("the first unexplained look was not said");
        if (ShouldSay(s1, s1, kUnexplained, 1, &next) != kSayNo || next != s1) Fail("the same answers were said twice");
        if (ShouldSay(s1, s2, kUnexplained, 1, &next) != kSaySay || next != s2) Fail("changed answers were not said again");
        if (ShouldSay(s1, s2, kSettling, 1, &next) != kSayNo || next != s1) Fail("a settling look re-armed or changed the town");
        if (ShouldSay(s1, s2, kSilentNotHeld, 1, &next) != kSayNo || next != s1) Fail("a silent look re-armed or changed the town");
        if (ShouldSay(s1, s2, kInFlight, 1, &next) != kSayNo || next != s1) Fail("an in-flight look re-armed or changed the town");
        if (ShouldSay(s1, s1, kNotEmpty, 1, &next) != kSayNo || !next.empty()) Fail("a town seen with people was not re-armed");
        { const std::string armed(next); if (ShouldSay(armed, s1, kUnexplained, 1, &next) != kSaySay) Fail("a re-armed town was not said again"); }
        if (ShouldSay(s1, s1, kAccLoss, 1, &next) != kSayNo || !next.empty()) Fail("a town accounted for was not re-armed");
        if (ShouldSay("", s1, kUnexplained, kLineCap - 1, &next) != kSaySay) Fail("the last line under the cap was not said");
        if (ShouldSay("", s1, kUnexplained, kLineCap, &next) != kSayDropped || next != s1) Fail("a line past the cap was not dropped once");
        if (ShouldSay(s1, s1, kUnexplained, kLineCap, &next) != kSayNo) Fail("a dropped line was dropped again with the same answers");
    }
    /* the line */
    {
        Facts f = base; f.listLiving = 11; f.homeLiving = 12; f.patrolLiving = 13; f.listed = 14; f.gone = 15; f.deadMembers = 16; f.townDead = 1;
        f.recLiving = 17; f.recPlacedHere = 18; f.recElsewhere = 19; f.recUnplaceable = 20; f.recCreateFailed = 21; f.loss = 1; f.refilling = 1;
        f.owedRows = 22; f.aside = 23; f.barPend = 1; f.copies = 24; f.noteHeld = 1; f.rebuildQueued = 25; f.recWaitingHere = 26; f.recDeleted = 27; f.goneThisLaunch = 28;
        const std::string ln = Line("Squin's\nTown", "12345-rf.mod", 3, -4, 42, 0x65D604D7u, 7u, 90u, f);
        const char* const want[] = { "[EMPTYTOWN] unexplained empty town 'Squin`s Town' (12345-rf.mod) area 3,-4 held 42s mod=65D604D7 sp=7 ws=90",
                                     " | living list=11 home=12 patrols=13", " | groups listed=14 gone=15 dead=16 townDead=1",
                                     " | records living=17 placedHere=18 elsewhere=19 unplaceable=20 createFailed=21", " | loss=1 refilling=1",
                                     " | owed rows=22 aside=23 barPend=1", " | copies=24", "noteHeld=1", "rebuild=25", "waitingHere=26", "deleted=27", "goneLaunch=28" };
        const std::string prefix("[EMPTYTOWN] unexplained empty town '");
        if (ln.compare(0, prefix.size(), prefix) != 0) Fail("the line does not start with its prefix");
        for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) if (ln.find(want[i]) == std::string::npos) Fail(std::string("the line lacks: ") + want[i]);
        if (ln.find_first_of("\r\n") != std::string::npos) Fail("the line holds a newline");
    }
}

/* ---- KEEPING A WORLD'S PLAYERS (src/common/worldkeep.h): the slots.txt shrink check against its .1 backup, the world export
   manifest and its verification, and the alias table the HELLO is resolved through ---- */
void t_t612_world_keep()
{
    using namespace worldkeep;
    const std::string A = "0123456789abcdef0123456789abcdef", B = "fedcba9876543210fedcba9876543210", C = "00000000000000000000000000000001",
                      E = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    {
        const std::string server = T313ReadFile("../coop-store/store_main.cpp");
        if (server.empty()) Fail("could not read the world server's store_main.cpp");
        else
        {
            /* each rewrite of slots.txt / profiles.txt is preceded, in the same function, by its .1 backup */
            const char* const backupOf[2] = { "BackupBeforeRewrite(SlotsFile()", "BackupBeforeRewrite(ProfilesFile()" };
            const char* const writeOf[2] = { "WriteWholeFile(SlotsFile()", "WriteWholeFile(ProfilesFile()" };
            for (int k = 0; k < 2; ++k)
            {
                const size_t b = server.find(backupOf[k]), w = server.find(writeOf[k]);
                const size_t end = b == std::string::npos ? std::string::npos : server.find("\n}", b);
                if (b == std::string::npos || w == std::string::npos || b > w || end == std::string::npos || w > end)
                    Fail(std::string(writeOf[k]) + " is not preceded, in the same function, by its .1 backup");
            }
            /* the operator's id (both branches of LoadOwner, after LoadAliases) and a research take's player id go through the alias table */
            {   /* the game's offline DELETE (HOST GAME -> CHANGE): profiles.txt is backed up to profiles.txt.1 before it is rewritten, and
                   the delete, the profile list and CHANGE's list read the id through the picked world's aliases.txt */
                const std::string game = T313ReadFile("../coop-plugin/store.cpp"), ui = T313ReadFile("../coop-plugin/ui.cpp");
                const size_t fn = game.find("int StoreProfileDeleteOffline("), fnEnd = fn == std::string::npos ? std::string::npos : game.find("\n}", fn);
                const size_t cp = game.find("U8CopyFile(path.c_str(), bakTmp.c_str(), FALSE)", fn), wf = game.find("WriteFileBytes(path, ", fn);
                if (game.empty() || fn == std::string::npos || fnEnd == std::string::npos || cp == std::string::npos || wf == std::string::npos
                    || cp > wf || wf > fnEnd)
                    Fail("the game's offline DELETE does not back profiles.txt up to profiles.txt.1 before rewriting it");
                const size_t da = game.find("coopprof::DeleteApply(&rows, me, num, 0)", fn);
                if (da == std::string::npos || da > fnEnd || game.find("const std::string me = StoreWorldPlayerId(folder);", fn) > da)
                    Fail("the game's offline DELETE does not read the id through the world's aliases.txt");
                if (ui.empty() || ui.find("PersonActiveRows(text, ConfigPlayerId())") != std::string::npos
                    || ui.find("PersonActiveRows(text, StoreWorldPlayerId(r->folder))") == std::string::npos
                    || ui.find("PersonActiveRows(text, StoreWorldPlayerId(g_hostProfWorld))") == std::string::npos)
                    Fail("HOST GAME's profile row or CHANGE's list does not read the id through the world's aliases.txt");
            }
            const size_t lo = server.find("void LoadOwner()"), loEnd = lo == std::string::npos ? std::string::npos : server.find("\n}", lo);
            int ownerResolves = 0;
            for (size_t at = lo; lo != std::string::npos && (at = server.find("worldkeep::AliasResolve(g_aliases, g_ownerId)", at)) != std::string::npos && at < loEnd; ++at)
                ++ownerResolves;
            if (ownerResolves < 2) Fail("LoadOwner does not resolve the operator's id through the alias table in both branches (--owner and owner.txt)");
            const size_t la = server.find("LoadAliases();   /*"), lw = server.find("\n    LoadOwner();");
            if (la == std::string::npos || lw == std::string::npos || la > lw) Fail("the alias table is not read before the operator is");
            const size_t rt = server.find("void OnResearchTake(");
            const size_t rr = rt == std::string::npos ? std::string::npos : server.find("in[i].player = worldkeep::AliasResolveKey(g_aliases, in[i].player)", rt);
            const size_t rc = rt == std::string::npos ? std::string::npos : server.find("me->second != in[i].player", rt);
            if (rr == std::string::npos || rc == std::string::npos || rr > rc)
                Fail("a research take's player id is not resolved through the alias table before it is compared with the sender's");
            if (server.find("worldkeep::ExportPathOk(paths[i])") == std::string::npos) Fail("the export does not check its paths the way the import's manifest reader does");
            const size_t al = server.find("worldkeep::AliasResolve(g_aliases, playerId)"), dup = server.find("B13-b (review-b13 H-2) - ONE ID, ONE GAME");
            if (al == std::string::npos || dup == std::string::npos || al > dup) Fail("the HELLO's id is not resolved through the alias table before the same-person test");
        }
    }
    if (BackupOf("w\\slots.txt") != "w\\slots.txt.1") Fail("the backup is not <name>.1");
    {
        const char* bytes = "123456789";
        if (coopstore::Crc32(bytes, 9) != 0xCBF43926u) Fail("the manifest's checksum is not CRC-32 (IEEE)");
    }
    /* the shrink check */
    {
        std::vector<std::string> l;
        l.push_back(coopstore::SlotsLineFormat(A, 0, 10)); l.push_back(coopstore::SlotsLineFormat(B, 1, 10));
        l.push_back(coopstore::SlotsLineFormat(A, 2, 10)); l.push_back(coopstore::SlotsLineFormat(C, 1, 10)); l.push_back("junk");
        l.push_back(coopstore::SlotsLineFormat(A + ".2", 3, 10));
        std::map<std::string, int> m; SlotRowsParse(l, &m);
        if (m.size() != 3 || m[A] != 0 || m[B] != 1 || m[A + ".2"] != 3) Fail("slots.txt's lines are not read the way LoadSlots reads them (first line stands)");
        std::map<std::string, int> cur(m), none;
        ShrinkReport r;
        if (SlotsShrinkDecide(0, none, cur, 0, &r) != kShrinkNoBackup) Fail("no backup: refused or compared");
        r = ShrinkReport(); if (SlotsShrinkDecide(1, m, cur, 0, &r) != kShrinkNone) Fail("an unchanged slots.txt was refused");
        std::map<std::string, int> grown(m); grown[E] = 4;
        r = ShrinkReport(); if (SlotsShrinkDecide(1, m, grown, 0, &r) != kShrinkNone) Fail("a slots.txt that gained a player was refused");
        r = ShrinkReport(); if (SlotsShrinkDecide(1, grown, m, 0, &r) != kShrinkRefuse || r.lost != 1 || r.moved != 0 || r.firstLost != E) Fail("a lost player was not refused");
        r = ShrinkReport(); if (SlotsShrinkDecide(1, grown, m, 1, &r) != kShrinkAccepted || r.lost != 1) Fail("--accept-slots-shrink did not let a lost player through");
        std::map<std::string, int> moved(m); moved[B] = 7;
        r = ShrinkReport(); if (SlotsShrinkDecide(1, m, moved, 0, &r) != kShrinkRefuse || r.moved != 1 || r.lost != 0) Fail("a renumbered player was not refused");
        r = ShrinkReport(); if (SlotsShrinkDecide(1, m, none, 0, &r) != kShrinkRefuse || r.lost != 3 || r.currentRows != 0 || r.backupRows != 3) Fail("an empty or unreadable slots.txt beside a backup was not refused");
        r = ShrinkReport(); if (SlotsShrinkDecide(1, none, none, 0, &r) != kShrinkNone) Fail("an empty backup was refused");
    }
    /* which files travel */
    if (!ExportTakes("slots.txt", 0) || !ExportTakes("profiles.txt", 0) || !ExportTakes("abc.platoon", 0) || !ExportTakes("slots.txt.1", 0)
        || !ExportTakes("repair", 1) || !ExportTakes("repair/7/abc.platoon", 0) || !ExportTakes("deleted.x.bits", 0) || !ExportTakes("aliases.txt", 0))
        Fail("a world file is left out of the export");
    if (ExportTakes("server.lock", 0) || ExportTakes("shared-wastelands-server.log", 0) || ExportTakes("coop-store.log", 0) || ExportTakes("slots.txt.tmp", 0)
        || ExportTakes("recycle", 1) || ExportTakes("RECYCLE", 1) || ExportTakes("manifest.txt", 0))
        Fail("the lock, a log, a temp file, the recycle folder or a stray manifest travels");
    if (!ExportPathOk("a.txt") || !ExportPathOk("repair/7/a.platoon") || ExportPathOk("") || ExportPathOk("../a") || ExportPathOk("a/../b") || ExportPathOk("/a")
        || ExportPathOk("a/") || ExportPathOk("a//b") || ExportPathOk("c:/a") || ExportPathOk("a\\b") || ExportPathOk("a\tb") || ExportPathOk("./a"))
        Fail("a manifest path rule is wrong");
    /* the manifest */
    {
        std::vector<FileRow> rows;
        FileRow f; f.path = "slots.txt"; f.size = 12; f.crc = 0xdeadbeefu; rows.push_back(f);
        f.path = "repair/7/a.platoon"; f.size = 0; f.crc = 0; rows.push_back(f);
        const std::string text = ManifestFormat(rows);
        std::vector<FileRow> back; std::string why;
        if (!ManifestParse(text, &back, &why) || back.size() != 2 || back[1].path != "repair/7/a.platoon" || back[0].crc != 0xdeadbeefu || back[0].size != 12)
            Fail("a manifest does not read back as written: " + why);
        std::string crlfText; for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '\n') crlfText += '\r'; crlfText += text[i]; }
        back.clear(); if (!ManifestParse(crlfText, &back, &why) || back.size() != 2) Fail("a manifest with CRLF lines was refused");
        const std::string head = std::string("v1\t") + kManifestKind + "\n";
        back.clear(); if (ManifestParse(text.substr(0, text.find("v1\tend")), &back, &why)) Fail("a manifest cut before its end line was taken");
        back.clear(); if (ManifestParse(head + "v1\tend\t1\n", &back, &why)) Fail("an end line with the wrong count was taken");
        back.clear(); if (ManifestParse(text + "v1\tfile\tx\t1\t00000000\n", &back, &why)) Fail("a line after the end was taken");
        back.clear(); if (ManifestParse(head + "v1\tfile\t../x\t1\t00000000\nv1\tend\t1\n", &back, &why)) Fail("a path out of the folder was taken");
        back.clear(); if (ManifestParse(head + "v1\tfile\ta\t1\t00000000\nv1\tfile\tA\t1\t00000000\nv1\tend\t2\n", &back, &why)) Fail("a file named twice was taken");
        back.clear(); if (ManifestParse(head + "v1\tfile\ta\t-1\t00000000\nv1\tend\t1\n", &back, &why)) Fail("a bad size was taken");
        back.clear(); if (ManifestParse(head + "v1\tfile\ta\t1\t0000000G\nv1\tend\t1\n", &back, &why)) Fail("a bad checksum was taken");
        back.clear(); if (ManifestParse("v1\tsomething-else\nv1\tend\t0\n", &back, &why)) Fail("another kind of file was taken as a manifest");
        std::string which;
        std::vector<FileRow> there(rows);
        there[0].path = "SLOTS.TXT";
        if (ManifestVerify(rows, there, &which) != kManOk || !which.empty()) Fail("a matching folder was refused");
        std::vector<FileRow> t2(rows); t2.pop_back();
        if (ManifestVerify(rows, t2, &which) != kManMissing || which != "repair/7/a.platoon") Fail("a missing file was not refused");
        t2 = rows; t2[0].size = 13; if (ManifestVerify(rows, t2, &which) != kManSize) Fail("a file of another size was not refused");
        t2 = rows; t2[0].crc ^= 1u; if (ManifestVerify(rows, t2, &which) != kManCrc) Fail("a file with another checksum was not refused");
        t2 = rows; f.path = "extra.txt"; t2.push_back(f); if (ManifestVerify(rows, t2, &which) != kManExtra || which != "extra.txt") Fail("a file the manifest does not name was not refused");
        if (ManifestVerify(std::vector<FileRow>(), std::vector<FileRow>(), &which) != kManEmpty) Fail("an empty manifest was taken");
    }
    /* the alias table */
    {
        std::map<std::string, int> slots; slots[A] = 0; slots[C + ".2"] = 1;
        std::map<std::string, Alias> t;
        if (AliasResolve(t, B) != B) Fail("an id with no alias is not itself");
        if (AliasAddDecide(t, B, A, slots) != kAliasOk) Fail("a new id onto a known one was refused");
        if (AliasAddDecide(t, B, B, slots) != kAliasSame) Fail("an alias onto itself was taken");
        if (AliasAddDecide(t, B, "xyz", slots) != kAliasBadId || AliasAddDecide(t, A.substr(0, 31) + "G", A, slots) != kAliasBadId) Fail("a malformed id was taken");
        if (AliasAddDecide(t, B, E, slots) != kAliasOldUnknown) Fail("an alias onto an id the world never named was taken");
        if (AliasAddDecide(t, B, C, slots) != kAliasOk) Fail("a person known only by a later profile's key was not known");
        Alias a; a.from = B; a.to = A; a.setUnix = 5; t[B] = a;
        if (AliasResolve(t, B) != A || AliasResolve(t, A) != A) Fail("the alias does not resolve");
        if (AliasAddDecide(t, A, B, slots) != kAliasLoop) Fail("a loop was taken");
        if (AliasAddDecide(t, E, B, slots) != kAliasOk) Fail("an alias onto an aliased id (which leads to a known one) was refused");
        a.from = E; a.to = B; t[E] = a;
        if (AliasResolve(t, E) != A) Fail("a chain of aliases does not lead to the known id");
        Alias p; if (!AliasLineParse(AliasLineFormat(t[B]), &p) || p.from != B || p.to != A || p.setUnix != 5) Fail("an alias line does not read back");
        std::vector<std::string> lines;
        lines.push_back(AliasLineFormat(t[B])); lines.push_back("junk"); lines.push_back("v1\t" + C + "\t" + C + "\t1");
        lines.push_back("v1\t" + B + "\t" + C + "\t1");   /* B a second time */
        lines.push_back("v1\t" + A + "\t" + B + "\t1");   /* closes A -> B -> A */
        lines.push_back(""); lines.push_back(AliasLineFormat(t[E]));
        std::map<std::string, Alias> loaded; int bad = 0; AliasTableLoad(lines, &loaded, &bad);
        if (loaded.size() != 2 || bad != 4 || AliasResolve(loaded, E) != A) Fail("aliases.txt's bad, repeated or looping lines are not dropped (" + D((double)bad) + ")");
        std::map<std::string, Alias> loop; a.from = A; a.to = B; loop[A] = a; a.from = B; a.to = A; loop[B] = a;
        if (AliasResolve(loop, A) != A) Fail("a hand-made loop does not resolve to the id itself");
        std::vector<std::string> again; { const std::string ft = AliasTableFormat(loaded); size_t from = 0, e; while ((e = ft.find('\n', from)) != std::string::npos) { again.push_back(ft.substr(from, e - from)); from = e + 1; } }
        std::map<std::string, Alias> reread; bad = 0; AliasTableLoad(again, &reread, &bad);
        if (reread.size() != 2 || bad != 0) Fail("aliases.txt does not read back as written");
        std::string f1, f2;
        if (!AliasArgParse(B + "=" + A, &f1, &f2) || f1 != B || f2 != A || AliasArgParse(B + A, &f1, &f2) || AliasArgParse(B + "=x", &f1, &f2)) Fail("--alias's <new>=<old> is read wrongly");
    }
    if (kServerExitSlotsShrunk == swformat::kServerExitFormatRefused) Fail("the shrink refusal's exit code is the format refusal's");
    if (kServerExitWorldAside == kServerExitSlotsShrunk || kServerExitWorldAside == swformat::kServerExitFormatRefused)
        Fail("the missing-world-beside-an-import refusal's exit code is another refusal's");
    if (!WorldAsideRefuses(false, true, true) || WorldAsideRefuses(true, true, true) || WorldAsideRefuses(false, true, false)
        || WorldAsideRefuses(false, false, true) || WorldAsideRefuses(false, false, false))
        Fail("a missing world folder is refused other than when an import's staging copy and its set-aside world are both beside it");
    {
        std::map<std::string, Alias> k; Alias al; al.from = "newperson"; al.to = "oldperson"; k[al.from] = al;
        if (AliasResolveKey(k, "newperson") != "oldperson" || AliasResolveKey(k, "newperson.3") != "oldperson.3"
            || AliasResolveKey(k, "someone.2") != "someone.2")
            Fail("a research take's slot key does not follow the alias table by its person");
    }
    {   /* the WELCOME's player id: the world server sends the id it knows the player by; the game follows it everywhere but the HELLO */
        const std::string server = T313ReadFile("../coop-store/store_main.cpp"), game = T313ReadFile("../coop-plugin/store.cpp");
        if (server.empty() || game.empty()) Fail("could not read store_main.cpp or store.cpp");
        else
        {
            if (server.find("wel.hasPerson = true; wel.person = coopprof::PersonOfKey(playerId);") == std::string::npos)
                Fail("the world server does not put the id it knows the player by in the WELCOME");
            if (game.find("std::string StorePlayerKey() { return g_profPickedNum >= 1 ? coopprof::SlotKey(MyPersonId(), g_profPickedNum) : MyPersonId(); }") == std::string::npos)
                Fail("StorePlayerKey does not use MyPersonId()");
            if (game.find("g_profPickedId = coopprof::ProfileId(MyPersonId(), num);") == std::string::npos) Fail("ProfilePick does not use MyPersonId()");
            if (game.find("const std::string playerId = ConfigPlayerId();") == std::string::npos) Fail("the HELLO no longer sends this install's own id");
            const size_t ww = game.find("if (!StoreWelcomeWorld(proto, m.payload)) continue;");
            const size_t wp = ww == std::string::npos ? std::string::npos : game.find("StoreWelcomePerson(m.payload);", ww);
            const char* const later[4] = { "StoreArmWorldIndex(\"storeLink\")", "coop::ResearchTableReset();", "ProfileSaveFolderAtWelcome(m.payload)",
                                           "if (protoOk) WorldGenOnWelcome(m.payload, auth != 0);" };
            if (ww == std::string::npos || wp == std::string::npos) Fail("StoreWelcomePerson does not follow StoreWelcomeWorld in the WELCOME handler");
            else
                for (int k = 0; k < 4; ++k)
                {
                    const size_t at = game.find(later[k], ww);
                    if (at == std::string::npos || at < wp) Fail(std::string("StoreWelcomePerson does not run before ") + later[k]);
                }
            if (game.find("tw.personMalformed") == std::string::npos) Fail("the WELCOME handler does not treat a torn player id field as a torn WELCOME");
            const size_t lt = game.find("void StoreLeftAtTitle()"), ltEnd = lt == std::string::npos ? std::string::npos : game.find("\n}", lt);
            const size_t cl = lt == std::string::npos ? std::string::npos : game.find("g_worldPersonId.clear();", lt);
            if (lt == std::string::npos || cl == std::string::npos || ltEnd == std::string::npos || cl > ltEnd) Fail("StoreLeftAtTitle does not clear the world's player id");
        }
    }
}

/* ---- moving a world and repairing an id, the world server's side (src/common/worldkeep.h): --unalias re-points the aliases that led
   through the removed id, --alias drops the new id's slots.txt rows only when it holds nothing and the shrink check leaves those rows
   out, exit 34's refusal is written beside the missing world folder, an export is imported only under its own name, and a deleted
   world's saves are found by the id its aliases.txt gave this player ---- */
void t_t612_world_keep_moves()
{
    using namespace worldkeep;
    const std::string A = "0123456789abcdef0123456789abcdef", B = "fedcba9876543210fedcba9876543210", C = "00000000000000000000000000000001",
                      E = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    {   /* --unalias */
        std::map<std::string, Alias> t; Alias a; a.setUnix = 7;
        a.from = A; a.to = B; t[A] = a; a.from = B; a.to = C; t[B] = a;   /* A -> B -> C */
        std::vector<std::string> moved;
        if (!AliasRemoveApply(&t, B, &moved) || t.count(B) || t.size() != 1 || t[A].to != C || moved.size() != 1 || moved[0] != A || AliasResolve(t, A) != C)
            Fail("removing the middle of A -> B -> C does not leave A -> C");
        if (t[A].setUnix != 7) Fail("a re-pointed alias lost its time");
        moved.clear();
        if (!AliasRemoveApply(&t, A, &moved) || !t.empty() || !moved.empty()) Fail("removing a leaf alias is not just that alias going");
        a.from = E; a.to = C; t[E] = a;
        if (AliasRemoveApply(&t, B, &moved) || t.size() != 1 || !moved.empty() || t[E].to != C) Fail("removing an id with no alias changed the table");
        a.from = A; a.to = B; t[A] = a; a.from = B; a.to = C; t[B] = a; a.from = E; a.to = B; t[E] = a;   /* A and E both lead through B */
        moved.clear();
        if (!AliasRemoveApply(&t, B, &moved) || moved.size() != 2 || AliasResolve(t, A) != C || AliasResolve(t, E) != C)
            Fail("two aliases through the removed id are not both re-pointed");
    }
    {   /* --alias: the new id's slots.txt rows */
        if (!AliasNewIdDropRows(1, 0, 0, 0) || !AliasNewIdDropRows(2, 0, 0, 0)) Fail("an id holding only slots.txt rows keeps them");
        if (AliasNewIdDropRows(0, 0, 0, 0)) Fail("an id with no rows has rows dropped");
        if (AliasNewIdDropRows(1, 1, 0, 0) || AliasNewIdDropRows(1, 0, 3, 0) || AliasNewIdDropRows(1, 0, 0, 1))
            Fail("an id holding a profile or a record (or an unreadable record that may be its) loses its rows");
        std::vector<std::string> lines;
        lines.push_back(coopstore::SlotsLineFormat(A, 0, 5)); lines.push_back(coopstore::SlotsLineFormat(B, 1, 6)); lines.push_back("junk");
        lines.push_back(coopstore::SlotsLineFormat(B + ".2", 2, 7)); lines.push_back(coopstore::SlotsLineFormat(C, 3, 8));
        int dropped = 0, shadowed = 0;
        const std::string body = SlotLinesWithoutPerson(lines, B, &dropped, &shadowed);
        std::vector<std::string> back; { size_t from = 0, e; while ((e = body.find('\n', from)) != std::string::npos) { back.push_back(body.substr(from, e - from)); from = e + 1; } }
        std::map<std::string, int> rows; SlotRowsParse(back, &rows);
        if (dropped != 2 || shadowed != 0 || rows.size() != 2 || !rows.count(A) || !rows.count(C) || rows[C] != 3 || back.size() != 3 || back[1] != "junk")
            Fail("dropping a person's slots.txt rows does not leave every other line as it was");
        {   /* the first line for an id or a number stands: a later line naming either is left out, so it never takes effect after a drop */
            std::vector<std::string> dl;
            dl.push_back(coopstore::SlotsLineFormat(B, 1, 6)); dl.push_back(coopstore::SlotsLineFormat(E, 1, 7));
            dl.push_back(coopstore::SlotsLineFormat(C, 3, 8)); dl.push_back(coopstore::SlotsLineFormat(C, 4, 9)); dl.push_back("junk");
            int d2 = 0, s2 = 0;
            const std::string b2 = SlotLinesWithoutPerson(dl, B, &d2, &s2);
            std::vector<std::string> bk; { size_t from = 0, e; while ((e = b2.find('\n', from)) != std::string::npos) { bk.push_back(b2.substr(from, e - from)); from = e + 1; } }
            std::map<std::string, int> r2; SlotRowsParse(bk, &r2);
            if (d2 != 1 || s2 != 2 || bk.size() != 2 || r2.size() != 1 || r2.count(E) || !r2.count(C) || r2[C] != 3 || bk[1] != "junk")
                Fail("a later slots.txt line naming a dropped row's number, or an id an earlier line names, is kept and would take effect");
        }
        /* the shrink check: rows gone from slots.txt whose id is aliased are left out only when they hold nothing; a row not aliased is lost */
        std::map<std::string, int> bak, cur; bak[A] = 0; bak[B] = 1; bak[B + ".2"] = 2; bak[C] = 3; cur[A] = 0; cur[C] = 3;
        std::map<std::string, Alias> t; Alias al; al.from = B; al.to = A; t[B] = al;
        if (!ShrinkRowAliasedGone(B, cur, t) || !ShrinkRowAliasedGone(B + ".2", cur, t) || ShrinkRowAliasedGone(A, cur, t) || ShrinkRowAliasedGone(C, cur, t))
            Fail("the rows asked whether they hold something are not exactly the aliased ones gone from slots.txt");
        std::map<std::string, int> holds; holds[B] = 0; holds[B + ".2"] = 0;
        std::map<std::string, int> kept; ShrinkReport r; int held = 0;
        if (ShrinkBackupLessAliased(bak, cur, t, holds, &kept, &held) != 2 || held != 0 || kept.size() != 2 || SlotsShrinkDecide(1, kept, cur, 0, &r) != kShrinkNone)
            Fail("the shrink check counts an aliased id's dropped rows that hold nothing as lost");
        holds[B] = 1; kept.clear(); held = 0; ShrinkReport rh;
        if (ShrinkBackupLessAliased(bak, cur, t, holds, &kept, &held) != 1 || held != 1 || !kept.count(B) || kept.count(B + ".2")
            || SlotsShrinkDecide(1, kept, cur, 0, &rh) != kShrinkRefuse || rh.lost != 1 || rh.firstLost != B)
            Fail("an aliased id's missing row that still holds a profile or records is left out of the shrink check");
        std::map<std::string, int> unasked; kept.clear(); held = 0; ShrinkReport ru;
        if (ShrinkBackupLessAliased(bak, cur, t, unasked, &kept, &held) != 0 || held != 2 || SlotsShrinkDecide(1, kept, cur, 0, &ru) != kShrinkRefuse)
            Fail("an aliased row with no answer about what it holds is left out instead of kept");
        holds[B] = 0; cur.erase(C); kept.clear(); held = 0; ShrinkReport r2;
        if (ShrinkBackupLessAliased(bak, cur, t, holds, &kept, &held) != 2 || SlotsShrinkDecide(1, kept, cur, 0, &r2) != kShrinkRefuse || r2.firstLost != C)
            Fail("the shrink check no longer refuses a lost id that is not aliased");
        std::map<std::string, Alias> none; kept.clear(); held = 0; ShrinkReport r3;
        if (ShrinkBackupLessAliased(bak, cur, none, holds, &kept, &held) != 0 || held != 0 || SlotsShrinkDecide(1, kept, cur, 0, &r3) != kShrinkRefuse)
            Fail("with no aliases a dropped row is not lost");
    }
    if (WorldAsideRefusedDir("D:\\w\\Ridge", true) != "D:\\w\\Ridge.importing" || WorldAsideRefusedDir("D:\\w\\Ridge", false) != "D:\\w"
        || !WorldAsideRefusedDir("Ridge", false).empty())
        Fail("exit 34's refusal is not written in the import's folder, or else beside the world folder");
    {
        const std::string server = T313ReadFile("../coop-store/store_main.cpp"), ui = T313ReadFile("../coop-plugin/ui.cpp"), game = T313ReadFile("../coop-plugin/store.cpp");
        if (server.empty() || ui.empty() || game.empty()) Fail("could not read store_main.cpp, ui.cpp or store.cpp");
        else
        {
            const size_t w = server.find("if (worldkeep::WorldAsideRefuses("), e = w == std::string::npos ? std::string::npos : server.find("return worldkeep::kServerExitWorldAside;", w);
            if (w == std::string::npos || e == std::string::npos) Fail("the exit-34 refusal is not found");
            else
            {
                const std::string blk = server.substr(w, e - w);
                if (blk.find("AppendRefused(worldkeep::WorldAsideRefusedDir(g_dir, KeepIsDir(staging)), say);") == std::string::npos)
                    Fail("exit 34 does not write its refusal into the import's folder");
                if (blk.find("CreateDirectory") != std::string::npos || blk.find("Mkdir") != std::string::npos || blk.find("kRefusedFile") != std::string::npos)
                    Fail("exit 34 makes a folder or writes into the missing world folder");
            }
            const size_t ar = server.find("void AppendRefused("), arEnd = ar == std::string::npos ? std::string::npos : server.find("\n}", ar);
            if (ar == std::string::npos || arEnd == std::string::npos || server.substr(ar, arEnd - ar).find("if (folder.empty() || !KeepIsDir(folder)) return;") == std::string::npos
                || server.substr(ar, arEnd - ar).find("CreateDirectory") != std::string::npos || server.substr(ar, arEnd - ar).find("Mkdir") != std::string::npos)
                Fail("the refusal writer may write where no folder is, or make one");
            const size_t im = server.find("int KeepImport("), st = im == std::string::npos ? std::string::npos : server.find("U8CreateDirectory(staging.c_str()", im);
            const size_t nm = im == std::string::npos ? std::string::npos : server.find("if (exported != g_worldName && !coopworld::WorldNamesCollide(exported, g_worldName))", im);
            const size_t nr = nm == std::string::npos ? std::string::npos : server.find("return 6;", nm);
            if (nm == std::string::npos || st == std::string::npos || nm > st || nr == std::string::npos || nr > st)
                Fail("the import does not refuse an export under another world's name before it writes anything");
            const size_t wx = im == std::string::npos ? std::string::npos : server.find("if (!g_worldName.empty() && KeepExists(from + \"\\\\world.txt\"))", im);
            const size_t wu = wx == std::string::npos ? std::string::npos : server.find("|| !coopworld::WorldTxtParse(wt, &exported, &created))", wx);
            const size_t wu6 = wu == std::string::npos ? std::string::npos : server.find("return 6;", wu);
            const size_t wn = wx == std::string::npos ? std::string::npos : server.find("if (!coopworld::WorldNameOk(exported, &whyNot))", wx);
            const size_t wn6 = wn == std::string::npos ? std::string::npos : server.find("return 6;", wn);
            if (wx == std::string::npos || wu6 == std::string::npos || wn6 == std::string::npos || wu6 > nm || wn6 > nm || nm == std::string::npos)
                Fail("an export whose world.txt is there but unreadable, or names no world, is not refused with exit 6 before the name compare");
            const size_t ka = server.find("int KeepAliasEdit("), aw = ka == std::string::npos ? std::string::npos : server.find("WriteWholeFile(AliasFile()", ka);
            const size_t sb = aw == std::string::npos ? std::string::npos : server.find("if (!BackupBeforeRewrite(SlotsFile(), true))", aw);
            const size_t sw = sb == std::string::npos ? std::string::npos : server.find("else if (!WriteWholeFile(SlotsFile(), body, true))", sb);
            const size_t kaEnd = ka == std::string::npos ? std::string::npos : server.find("\n}", ka);
            if (aw == std::string::npos || sb == std::string::npos || sw == std::string::npos || kaEnd == std::string::npos || sw > kaEnd)
                Fail("--alias does not drop the new id's rows after aliases.txt is written, and only when slots.txt.1 was made first");
            const size_t ug = ka == std::string::npos ? std::string::npos : server.find("if (ReadWholeFileLines(worldkeep::BackupOf(SlotsFile()), &bl)) worldkeep::SlotRowsParse(bl, &bak);", ka);
            const size_t uw = ug == std::string::npos ? std::string::npos : server.find("if (unaliasGone > 0)", ug);
            const size_t ux = uw == std::string::npos ? std::string::npos : server.find("REFUSES with exit 33 (slots.txt shrunk); start it once with --accept-slots-shrink", uw);
            if (ug == std::string::npos || uw == std::string::npos || ux == std::string::npos || kaEnd == std::string::npos || ux > kaEnd
                || server.find("if (!strcmp(argv[i], \"--accept-slots-shrink\"))") == std::string::npos)
                Fail("--unalias does not say that slots.txt.1's dropped rows make the next start refuse with exit 33 until --accept-slots-shrink");
            const size_t sc = server.find("int SlotsShrinkCheck("), scEnd = sc == std::string::npos ? std::string::npos : server.find("\n}", sc);
            const std::string scb = sc == std::string::npos || scEnd == std::string::npos ? std::string() : server.substr(sc, scEnd - sc);
            if (scb.find("KeepRecordsOfSlots(one, &unread)") == std::string::npos || scb.find("KeepProfileRowsOf(coopprof::PersonOfKey(it->first)) > 0 || recs > 0 || unread > 0") == std::string::npos
                || scb.find("worldkeep::ShrinkBackupLessAliased(b, c, t, holds, &kept, &held)") == std::string::npos)
                Fail("the start-up shrink check does not ask each aliased missing row whether it holds a profile or records");
            const size_t pd = ui.find("static void PanelDeleteConfirmed()"), me = pd == std::string::npos ? std::string::npos : ui.find("const std::string me = StoreWorldPlayerId(folder);", pd);
            const size_t lv = pd == std::string::npos ? std::string::npos : ui.find("StoreWorldDeleteLeftovers(name, worldId, me, &left);", pd);
            const size_t dt = lv == std::string::npos ? std::string::npos : ui.find("StoreDeleteTree(path, StoreTopDir(),", lv);
            if (me == std::string::npos || lv == std::string::npos || dt == std::string::npos || me > lv)
                Fail("a deleted world's leftovers are not found by this player's id read through its aliases.txt before anything is deleted");
            if (game.find("swdel::WorldDeleteFind(seen, worldName, worldId, keep, person.empty() ? ConfigPlayerId() : person,") == std::string::npos)
                Fail("WorldDeleteFind is not given the id the world knew this player by");
        }
    }
}

/* ---- the WELCOME's player id field (store protocol 91, src/common/storelink.h): right after the world id field, before the mods ---- */
void t_t612_welcome_person_round_trip()
{
    using namespace coopstore;
    const std::string P = "0123456789abcdef0123456789abcdef";
    coopmods::ModList w; w.known = 1; w.active.push_back("A|1|00000001");
    StoreWelcome a; a.protocol = kStoreWelcomePersonSince; a.authority = 1; a.recordCount = 4; a.slot = 2; a.hasWorld = true; a.world = "Ridge";
    a.hasWorldId = true; a.worldId = "1791234567-3fa9c01e"; a.hasPerson = true; a.person = P;
    a.hasMods = true; a.modsVerdict = kModsDiffers; a.modsActive = 1; a.modsPlugins = 0; a.modsWorld = w;
    a.hasWorldGen = true; a.worldTail.push_back('T'); a.worldTail.push_back('Z');
    std::vector<char> b; StoreWelcomeEncode(a, &b);
    StoreWelcome r;
    if (!StoreWelcomeDecode(b, &r) || !r.hasPerson || r.person != P || r.personMalformed || !r.hasWorldId || r.worldId != a.worldId)
        Fail("at 91 the player id rides right after the world id");
    if (!r.hasMods || r.modsVerdict != (unsigned)kModsDiffers || !r.hasModsWorld || !coopmods::ModListSame(r.modsWorld, w)
        || !r.hasWorldGen || r.worldTail.size() != 2 || r.worldTail[0] != 'T' || r.worldTail[1] != 'Z')
        Fail("after the player id: the mods verdict, the world's list and the guard tail, intact");
    const size_t size91 = b.size();
    StoreWelcome o = a; o.protocol = kStoreWelcomePersonSince - 1; StoreWelcomeEncode(o, &b);
    if (b.size() + 4 + P.size() != size91) Fail("a protocol-90 WELCOME is written with a player id field");
    if (!StoreWelcomeDecode(b, &r) || r.hasPerson || r.personMalformed || !r.hasWorldId || r.worldId != a.worldId || !r.hasModsWorld
        || !r.hasWorldGen || r.worldTail.size() != 2 || r.worldTail[0] != 'T')
        Fail("a protocol-90 WELCOME carries no player id field and reads as before");
    a.hasPerson = false; a.person.clear(); StoreWelcomeEncode(a, &b);
    if (!StoreWelcomeDecode(b, &r) || r.hasPerson || r.personMalformed || !r.hasModsWorld || !r.hasWorldGen || r.worldTail.size() != 2)
        Fail("no player id: an empty field, and everything after it still read");
    a.hasPerson = true; a.person = std::string(kStoreWelcomePersonMax + 10, 'p'); StoreWelcomeEncode(a, &b);
    if (!StoreWelcomeDecode(b, &r) || r.person.size() != kStoreWelcomePersonMax || r.personMalformed) Fail("a player id over the cap is not cut to it when written");
    a.person = P; a.hasMods = false; a.hasWorldGen = false; StoreWelcomeEncode(a, &b);
    std::vector<char> torn(b.begin(), b.end() - 2);
    if (!StoreWelcomeDecode(torn, &r) || !r.personMalformed || r.hasPerson || r.hasMods || !r.hasWorldId) Fail("a torn player id field is caught and nothing after it read");
    const size_t idEnd = 20 + a.world.size() + 8 + a.worldId.size();
    std::vector<char> big(b.begin(), b.begin() + idEnd); StoreWelcomePutU32(&big, kStoreWelcomePersonMax + 1); big.resize(big.size() + 80, 'x');
    if (!StoreWelcomeDecode(big, &r) || !r.personMalformed || r.hasPerson || r.hasMods || r.hasWorldGen) Fail("a player id longer than the cap is not read, nor anything after it");
    std::vector<char> noLen(b.begin(), b.begin() + idEnd + 2);
    if (!StoreWelcomeDecode(noLen, &r) || !r.personMalformed || r.hasPerson) Fail("a WELCOME cut inside the player id's length is caught");
}
/* ---- what a game does with the WELCOME's player id (src/common/worldkeep.h WelcomePersonDecide) ---- */
void t_t612_welcome_person_decide()
{
    using namespace worldkeep;
    const std::string OWN(32, 'a'), OLD(32, 'b'), OTHER(32, 'c');
    if (WelcomePersonDecide(OWN, false, "", false, "", false) != kPersonSame || WelcomePersonDecide(OWN, true, OWN, true, "", true) != kPersonSame
        || WelcomePersonDecide(OWN, false, "", false, OLD, true) != kPersonSame)
        Fail("no id, or this install's own id with nothing adopted: same");
    if (WelcomePersonDecide(OWN, true, "xyz", false, "", false) != kPersonBad || WelcomePersonDecide(OWN, true, "xyz", false, OLD, true) != kPersonBad)
        Fail("an id that fails the check: bad");
    if (WelcomePersonDecide(OWN, true, OLD, true, "", false) != kPersonAdopt) Fail("an aliased id with nothing adopted yet and no world in play: adopt");
    if (WelcomePersonDecide(OWN, true, OLD, true, "", true) != kPersonHeld) Fail("an aliased id arriving while a world is in play under this install's own id: held");
    if (WelcomePersonDecide(OWN, true, OLD, true, OLD, true) != kPersonSame || WelcomePersonDecide(OWN, true, OLD, true, OLD, false) != kPersonSame)
        Fail("a re-link naming the id already adopted: same");
    if (WelcomePersonDecide(OWN, true, OTHER, true, OLD, false) != kPersonAdopt || WelcomePersonDecide(OWN, true, OWN, true, OLD, false) != kPersonAdopt)
        Fail("another id with no world in play: adopt");
    if (WelcomePersonDecide(OWN, true, OTHER, true, OLD, true) != kPersonHeld || WelcomePersonDecide(OWN, true, OWN, true, OLD, true) != kPersonHeld)
        Fail("another id while a world is in play under the adopted one: held");
}

/* ---- resetting a world (src/common/worldreset.h; world.txt line 3 in src/common/worlddir.h): which entry of a world folder is kept,
   the world's state or an area record, and world.txt's new id with every id the world had before ---- */
void t_t36_reset_world()
{
    using namespace worldreset;
    {   /* the entries of a world folder */
        const char* keep[] = { "world.txt", "World.TXT", "world.txt.1", "world.txt.tmp", "format.txt", "slots.txt", "slots.txt.1", "profiles.txt",
                               "profiles.txt.1", "aliases.txt", "aliases.txt.1", "owner.txt", "options.txt", "mods.txt", "uid_seats.txt", "fallen.txt",
                               "teams.txt", "own_high.txt", "own_high.txt.tmp", "world.gen", "world.gen.tmp", "server.lock", "server-refused.txt",
                               "shared-wastelands-server.log", "shared-wastelands-server.log.1", "auth.txt", "auth.txt.tmp", "access.txt", "access.txt.1" };
        for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); ++i)
            if (ResetClassify(keep[i], 0) != kResetKeep) Fail(std::string("reset: '") + keep[i] + "' is not kept (" + ResetKindName(ResetClassify(keep[i], 0)) + ")");
        const char* move[] = { "uniques.txt", "world_relations.txt", "town_losses.txt", "town_bars.txt", "owed_people.txt", "research_boxes.txt",
                               "research_takes.txt", "clock.txt", "clock.txt.tmp", "areas.txt", "pendingpos.txt", "repair.txt", "queue.txt",
                               "deleted.Holy_Nation.bits", "deleted.204-gamedata.base.bits", "12345-gamedata.base_7.platoon", "12345-gamedata.base_7.meta",
                               "12345-gamedata.base_7.platoon.prev", "12345-gamedata.base_7.meta.prev", "12345-gamedata.base_7.platoon.refused",
                               "12345-gamedata.base_7.platoon.tmp", "x.meta.tmp", "zone.platoon",
                               "piece.3.41.#0a1b2c3d.1003.-205.100.platoon", "piece.3.41.#0a1b2c3d.1003.-205.100.meta", "piece.3.41.#0a1b2c3d.1003.-205.100.platoon.prev" };
        for (size_t i = 0; i < sizeof(move) / sizeof(move[0]); ++i)
            if (ResetClassify(move[i], 0) != kResetMove) Fail(std::string("reset: '") + move[i] + "' is not moved (" + ResetKindName(ResetClassify(move[i], 0)) + ")");
        const char* area[] = { "zone.12.-3.platoon", "zone.12.-3.meta", "zone.12.-3.platoon.prev", "zone.12.-3.meta.prev", "zone.12.-3.platoon.refused",
                               "ZONE.1.2.platoon", "zone.1.2.platoon.tmp" };
        for (size_t i = 0; i < sizeof(area) / sizeof(area[0]); ++i)
            if (ResetClassify(area[i], 0) != kResetArea) Fail(std::string("reset: '") + area[i] + "' is not an area record (" + ResetKindName(ResetClassify(area[i], 0)) + ")");
        const char* unknown[] = { "manifest.txt", "notes.txt", "readme.md", "deleted..bits", ".platoon", ".meta", "repair", "before-reset-1700000000" };
        for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i)
            if (ResetClassify(unknown[i], 0) != kResetUnknown) Fail(std::string("reset: the file '") + unknown[i] + "' is not left alone (" + ResetKindName(ResetClassify(unknown[i], 0)) + ")");
        if (ResetClassify("repair", 1) != kResetMove || ResetClassify("Recycle", 1) != kResetMove) Fail("reset: the repair and recycle folders are not moved");
        if (ResetClassify("before-reset-1700000000", 1) != kResetUnfinished || ResetClassify("Before-Reset-12", 1) != kResetUnfinished
            || ResetClassify("before-reset-", 1) != kResetUnknown || ResetClassify("before-reset-x1", 1) != kResetUnknown)
            Fail("reset: an earlier reset's before-reset folder is not an unfinished delete");
        if (ResetClassify("keep-buildings-1700000000", 1) != kResetKeep || ResetClassify("keep-buildings-", 1) != kResetUnknown
            || ResetClassify("keep-buildings-1700000000", 0) != kResetUnknown)
            Fail("reset: a keep-buildings-<time> folder is not kept");
        if (ResetClassify("mirror-4242", 1) != kResetMove || ResetClassify("MIRROR-7", 1) != kResetMove || ResetClassify("mirror-", 1) != kResetUnknown
            || ResetClassify("mirror-x", 1) != kResetUnknown || ResetClassify("mirror-4242", 0) != kResetUnknown)
            Fail("reset: a host game's mirror-<pid> folder is not reset");
        if (!ResetIsMirror("Mirror-12") || ResetIsMirror("mirror-") || ResetIsMirror("mirrors-1")) Fail("reset: a mirror folder name is misread");
        if (std::string(ResetKindName(kResetUnfinished)) != "unfinished") Fail("reset: the unfinished kind has no name");
        if (ResetClassify("keep-buildings", 1) != kResetUnknown || ResetClassify("other", 1) != kResetUnknown || ResetClassify("world.txt", 1) != kResetUnknown)
            Fail("reset: an unknown folder is not left alone");
    }
    {   /* world.txt: a new id, born; the earlier ids on line 3 */
        using namespace coopworld;
        const std::string a = "1700000000-0000abcd", b = "1800000000-12345678", c = "1900000000-deadbeef";
        const std::string t0 = WorldTxtFormatWithId("My World", 1600000000, a, true);
        std::string t1, t2, was, n, id; long long cr = 0, at = 0; bool up = true, kept = false; std::vector<std::string> e;
        if (WorldTxtAfterReset(t0, b, 1800000000, true, &t1, &was) != kResetTxtOk || was != a) Fail("reset: world.txt is not rewritten, or the old id not given");
        if (!WorldTxtParse(t1, &n, &cr) || n != "My World" || cr != 1600000000) Fail("reset: world.txt line 1 changed");
        if (!WorldTxtParseId(t1, &id, &up) || id != b || up) Fail("reset: world.txt line 2 is not the new id, born");
        if (!WorldTxtParseEarlier(t1, &e, &at, &kept) || e.size() != 1 || e[0] != a || at != 1800000000 || !kept) Fail("reset: line 3 does not hold the old id, the time and the choice");
        if (!WorldIdIsEarlier(t1, a) || WorldIdIsEarlier(t1, b) || WorldIdIsEarlier(t1, c)) Fail("reset: an earlier id is not known as one");
        if (WorldDataDirDecide(t1, b) != kDataDirOwn) Fail("reset: a three-line world.txt is not read by the folder rule as before");
        if (WorldTxtAfterReset(t1, c, 1900000000, false, &t2, &was) != kResetTxtOk || was != b) Fail("reset: a second reset fails");
        if (!WorldTxtParseEarlier(t2, &e, &at, &kept) || e.size() != 2 || e[0] != a || e[1] != b || at != 1900000000 || kept)
            Fail("reset: a second reset does not keep both earlier ids, oldest first, with its own time and choice");
        if (!WorldTxtParseId(t2, &id, &up) || id != c || !WorldIdIsEarlier(t2, a)) Fail("reset: a player who missed two resets is not known by the first id");
        if (WorldTxtAfterReset(t1, b, 1, true, &t2, &was) != kResetTxtSameId || WorldTxtAfterReset(t1, a, 1, true, &t2, &was) != kResetTxtSameId)
            Fail("reset: an id the world has had is taken again");
        if (WorldTxtAfterReset("garbage", c, 1, true, &t2, &was) != kResetTxtUnreadable) Fail("reset: an unreadable world.txt is rewritten");
        if (WorldTxtAfterReset(t0, "nope", 1, true, &t2, &was) != kResetTxtBadNewId) Fail("reset: a new id that is not an id is written");
        const std::string p0 = WorldTxtFormat("Old", 1500000000);
        if (WorldTxtAfterReset(p0, c, 1, false, &t2, &was) != kResetTxtOk || !was.empty() || WorldTxtParseEarlier(t2, 0, 0, 0) || !WorldTxtParseId(t2, &id, &up) || id != c)
            Fail("reset: a world.txt from before ids does not get the new id alone");
        std::string crlf;
        for (size_t i = 0; i < t1.size(); ++i) { if (t1[i] == '\n') crlf += '\r'; crlf += t1[i]; }
        if (!WorldTxtParseEarlier(crlf, &e, 0, 0) || e.size() != 1 || !WorldTxtParseId(crlf, &id, &up) || id != b) Fail("reset: a CRLF world.txt is not read");
        const std::string bad = t0 + "earlier\tnot-an-id\t5\tkept-buildings\n";
        if (WorldTxtParseEarlier(bad, 0, 0, 0)) Fail("reset: a line 3 naming no id is read");
        if (WorldTxtAfterReset(bad, b, 9, false, &t2, &was) != kResetTxtOk || !WorldTxtParseEarlier(t2, &e, 0, 0) || e.size() != 1 || e[0] != a)
            Fail("reset: an unreadable line 3 does not start the list again from the current id");
        std::string l3;
        if (!WorldTxtLine3Unreadable(bad, &l3) || l3 != "earlier\tnot-an-id\t5\tkept-buildings") Fail("reset: an unreadable line 3 is not reported");
        if (!WorldTxtLine3Unreadable(t0 + "junk\r\n", &l3) || l3 != "junk") Fail("reset: a line 3 of other text is not reported");
        if (WorldTxtLine3Unreadable(t0, 0) || WorldTxtLine3Unreadable(t1, 0) || WorldTxtLine3Unreadable(crlf, 0) || WorldTxtLine3Unreadable(t0 + "\r\n", 0)
            || WorldTxtLine3Unreadable(p0, 0))
            Fail("reset: a readable, empty or absent line 3 is reported as unreadable");
        std::string t = t0, last;
        const int K = (int)kWorldEarlierIdsMax + 5;
        for (int k = 0; k < K; ++k)
        {
            char nb[40]; sprintf(nb, "%d-%08x", 1000 + k, (unsigned)(k + 1));
            std::string nt;
            if (WorldTxtAfterReset(t, nb, 2000 + k, false, &nt, &was) != kResetTxtOk) { Fail("reset: a repeated reset fails"); break; }
            if (k == K - 2) last = nb;
            t = nt;
        }
        if (!WorldTxtParseEarlier(t, &e, 0, 0) || e.size() != kWorldEarlierIdsMax || e.back() != last) Fail("reset: the earlier ids are not capped to the newest");
    }
}
/* ---- splitting a world at its reset (src/common/worldreset.h): the old world's name and its free-name pick, this world's reset count,
   what the reset world gets of each entry, and world.txt under the old world's name (src/common/worlddir.h WorldTxtRenamed) ---- */
void t_t36_reset_split()
{
    using namespace worldreset;
    {   /* the old world's name: "<name> - before reset <n>", at most 48 characters */
        if (coopcfg::kCfgWorldFieldMax != 48) Fail("split: the world name's limit is not 48");
        if (ResetOldWorldName("My World", 1) != "My World - before reset 1" || ResetOldWorldName("My World", 2) != "My World - before reset 2"
            || ResetOldWorldName("My World", 10) != "My World - before reset 10")
            Fail("split: a short name's old-world name is not '<name> - before reset <n>'");
        if (!ResetOldWorldName("My World", 0).empty() || !ResetOldWorldName("", 1).empty() || !ResetOldWorldName("bad/name", 1).empty()
            || !ResetOldWorldName("con", 1).empty() || !ResetOldWorldName(" lead", 1).empty())
            Fail("split: an old-world name is made for n 0 or for a name that is not a world name");
        const std::string long48(48, 'a');
        const std::string o1 = ResetOldWorldName(long48, 1), o2 = ResetOldWorldName(long48, 2), o10 = ResetOldWorldName(long48, 10);
        if (o1 != std::string(31, 'a') + " - before reset 1" || o2 != std::string(31, 'a') + " - before reset 2" || o10 != std::string(30, 'a') + " - before reset 10"
            || o1.size() != 48 || o10.size() != 48)
            Fail("split: a long name is not cut just enough to fit 48 characters with its suffix");
        const std::string fit(31, 'b');
        if (ResetOldWorldName(fit, 1) != fit + " - before reset 1" || ResetOldWorldName(fit + "c", 1) != fit + " - before reset 1"
            || ResetOldWorldName(fit, 10) != std::string(30, 'b') + " - before reset 10")
            Fail("split: a name that just fits is cut, or one character over is not cut by exactly one");
        if (ResetOldWorldName(std::string(30, 'c') + " dddd", 1) != std::string(30, 'c') + " - before reset 1")
            Fail("split: a cut that ends in a space keeps the space (two spaces in a row are no world name)");
        for (unsigned int k = 1; k < 2000000; k = k * 3 + 1)
        {
            const std::string o = ResetOldWorldName(long48, k);
            if (o.size() > 48 || !coopworld::WorldNameOk(o, 0) || ResetOldWorldNameOf(o, long48) != k) { Fail("split: an old-world name is too long, invalid or misread"); break; }
        }
        if (ResetOldWorldNameOf("My World - before reset 3", "My World") != 3 || ResetOldWorldNameOf("My World", "My World") != 0
            || ResetOldWorldNameOf("My World - before reset 03", "My World") != 0 || ResetOldWorldNameOf("My World - before reset 0", "My World") != 0
            || ResetOldWorldNameOf("Other - before reset 3", "My World") != 0 || ResetOldWorldNameOf("my world - before reset 3", "My World") != 0
            || ResetOldWorldNameOf("My World - before reset 3x", "My World") != 0
            || ResetOldWorldNameOf("My World - before reset 1 - before reset 2", "My World - before reset 1") != 2)
            Fail("split: an old-world name is misread");
    }
    {   /* a name already taken beside the world folder moves n up */
        std::vector<std::string> taken;
        unsigned int used = 0;
        if (ResetOldWorldPick("My World", 1, taken, &used) != "My World - before reset 1" || used != 1) Fail("split: a free name is not used as it is");
        taken.push_back("my world - before reset 1"); taken.push_back("My World - Before Reset 2"); taken.push_back("my world"); taken.push_back("my world.resetting");
        if (ResetOldWorldPick("My World", 1, taken, &used) != "My World - before reset 3" || used != 3) Fail("split: a taken name (any case) does not move n up to the first free one");
        if (ResetOldWorldPick("My World", 2, taken, &used) != "My World - before reset 3" || used != 3) Fail("split: a taken n 2 does not move up to 3");
        if (ResetOldWorldPick("My World", 7, taken, &used) != "My World - before reset 7" || used != 7) Fail("split: a free n 7 is moved");
        std::vector<std::string> all;
        for (unsigned int k = 5; k < 5 + kOldWorldNameTries; ++k) all.push_back(ResetOldWorldName("W", k));
        used = 77;
        if (!ResetOldWorldPick("W", 5, all, &used).empty() || used != 77) Fail("split: a name is given when every name tried is taken");
        all.pop_back();
        if (ResetOldWorldPick("W", 5, all, &used) != ResetOldWorldName("W", 4 + kOldWorldNameTries) || used != 4 + kOldWorldNameTries)
            Fail("split: the last name tried is not given when it alone is free");
        if (!ResetOldWorldPick("bad/name", 1, taken, &used).empty()) Fail("split: a name is picked for a name that is not a world name");
    }
    {   /* this world's reset count, from world.txt line 3; world.txt under the old world's name */
        using namespace coopworld;
        const std::string a = "1700000000-0000abcd", b = "1800000000-12345678", c = "1900000000-deadbeef";
        const std::string t0 = WorldTxtFormatWithId("My World", 1600000000, a, false);
        std::string t1, t2, was;
        if (ResetCountNext(t0) != 1 || ResetCountNext(WorldTxtFormat("Old", 1)) != 1 || ResetCountNext("garbage") != 1)
            Fail("split: a world never reset does not give n = 1");
        if (WorldTxtAfterReset(t0, b, 2, false, &t1, &was) != kResetTxtOk || WorldTxtAfterReset(t1, c, 3, true, &t2, &was) != kResetTxtOk) Fail("split: the resets fail");
        if (ResetCountNext(t1) != 2 || ResetCountNext(t2) != 3) Fail("split: a world reset once / twice does not give n = 2 / 3");
        if (ResetCountNext(t0 + "earlier\tnot-an-id\t5\tkept-buildings\n") != 1) Fail("split: an unreadable line 3 does not give n = 1");
        std::string tk = t0;
        for (int k = 0; k < (int)kWorldEarlierIdsMax + 3; ++k)
        {
            char nb[40]; sprintf(nb, "%d-%08x", 1000 + k, (unsigned)(k + 1));
            std::string nt;
            if (WorldTxtAfterReset(tk, nb, 2000 + k, false, &nt, &was) != kResetTxtOk) { Fail("split: a repeated reset fails"); break; }
            tk = nt;
        }
        if (ResetCountNext(tk) != (unsigned int)kWorldEarlierIdsMax + 1) Fail("split: past the cap of earlier ids n is not kWorldEarlierIdsMax + 1");
        const std::string r = WorldTxtRenamed(t2, "My World - before reset 3");
        std::string nm, id; long long cr = 0; bool up = true; std::vector<std::string> e;
        if (!WorldTxtParse(r, &nm, &cr) || nm != "My World - before reset 3" || cr != 1600000000 || !WorldTxtParseId(r, &id, &up) || id != c || up
            || !WorldTxtParseEarlier(r, &e, 0, 0) || e.size() != 2 || r.substr(r.find('\n')) != t2.substr(t2.find('\n')))
            Fail("split: world.txt under the old world's name changes more than line 1's name");
        std::string crlf;
        for (size_t i = 0; i < t0.size(); ++i) { if (t0[i] == '\n') crlf += '\r'; crlf += t0[i]; }
        const std::string rc = WorldTxtRenamed(crlf, "X");
        if (rc != "v1\tX\t1600000000\r\nid\t" + a + "\tborn\r\n") Fail("split: a CRLF world.txt is not renamed byte for byte");
        if (WorldTxtRenamed(WorldTxtFormat("Old", 1500000000), "New") != WorldTxtFormat("New", 1500000000)) Fail("split: a world.txt from before ids is not renamed");
        if (WorldTxtRenamed("v1\tOld\t0001500000000", "New") != "v1\tNew\t0001500000000") Fail("split: line 1's created time is not kept as written");
        if (!WorldTxtRenamed("garbage", "X").empty() || !WorldTxtRenamed(t0, "bad/name").empty() || !WorldTxtRenamed(t0, "").empty())
            Fail("split: an unreadable world.txt or a name that is not a world name is renamed");
        if (WorldTxtRenamed(t0, "My World") != t0) Fail("split: renaming to its own name changes world.txt");
    }
    {   /* what the reset world gets of each entry */
        if (std::string(authproof::kAuthFile) != "auth.txt" || std::string(accessrule::kAccessFile) != "access.txt")
            Fail("split: the password file or the player list is not the name the reset keeps");
        const char* copy[] = { "slots.txt", "slots.txt.1", "profiles.txt", "aliases.txt", "owner.txt", "options.txt", "mods.txt", "uid_seats.txt", "fallen.txt",
                               "teams.txt", "own_high.txt", "world.gen", "format.txt", "server-refused.txt", "shared-wastelands-server.log", "auth.txt",
                               "Auth.TXT", "access.txt", "access.txt.1" };
        for (size_t i = 0; i < sizeof(copy) / sizeof(copy[0]); ++i)
        {
            const int k = ResetClassify(copy[i], 0);
            if (k != kResetKeep || ResetWorldGets(copy[i], k, 0) != kResetGetCopy || ResetWorldGets(copy[i], k, 1) != kResetGetCopy)
                Fail(std::string("split: the reset world does not get a copy of '") + copy[i] + "'");
        }
        if (ResetWorldGets("keep-buildings-1700000000", ResetClassify("keep-buildings-1700000000", 1), 0) != kResetGetCopy)
            Fail("split: the reset world does not get a copy of an earlier reset's keep-buildings folder");
        const char* fresh[] = { "world.txt", "World.TXT", "world.txt.1", "world.txt.tmp" };
        for (size_t i = 0; i < sizeof(fresh) / sizeof(fresh[0]); ++i)
            if (ResetWorldGets(fresh[i], ResetClassify(fresh[i], 0), 1) != kResetGetFresh || ResetWorldGets(fresh[i], ResetClassify(fresh[i], 0), 0) != kResetGetFresh)
                Fail(std::string("split: '") + fresh[i] + "' is copied into the reset world (it would carry the old world's id)");
        if (ResetWorldGets("server.lock", ResetClassify("server.lock", 0), 1) != kResetGetNone) Fail("split: the world lock is copied");
        const char* fileNone[] = { "uniques.txt", "clock.txt", "areas.txt", "queue.txt", "deleted.Holy_Nation.bits", "12345-gamedata.base_7.platoon",
                                   "12345-gamedata.base_7.meta.prev", "notes.txt", "manifest.txt" };
        for (size_t i = 0; i < sizeof(fileNone) / sizeof(fileNone[0]); ++i)
            if (ResetWorldGets(fileNone[i], ResetClassify(fileNone[i], 0), 1) != kResetGetNone)
                Fail(std::string("split: the reset world gets '") + fileNone[i] + "' (the world's state or an unknown file)");
        const char* dirNone[] = { "repair", "recycle", "mirror-4242", "before-reset-1700000000", "other" };
        for (size_t i = 0; i < sizeof(dirNone) / sizeof(dirNone[0]); ++i)
            if (ResetWorldGets(dirNone[i], ResetClassify(dirNone[i], 1), 1) != kResetGetNone)
                Fail(std::string("split: the reset world gets the folder '") + dirNone[i] + "'");
        const char* area[] = { "zone.12.-3.platoon", "zone.12.-3.meta.prev", "ZONE.1.2.platoon" };
        for (size_t i = 0; i < sizeof(area) / sizeof(area[0]); ++i)
        {
            const int k = ResetClassify(area[i], 0);
            if (ResetWorldGets(area[i], k, 0) != kResetGetNone || ResetWorldGets(area[i], k, 1) != kResetGetBuildings)
                Fail(std::string("split: the area record '") + area[i] + "' is not copied into keep-buildings only when the buildings are kept");
        }
        if (std::string(kResetStagingSuffix) != ".resetting" || std::string(kKeptBuildingsPrefix) != "keep-buildings-" || kServerExitResetHalfway != 35
            || kServerExitResetHalfway == swformat::kServerExitWorldAside || kServerExitResetHalfway == swformat::kServerExitSlotsShrunk)
            Fail("split: the staging folder, the buildings folder or the half-way exit code changed");
    }
}
/* ---- carrying a player into the reset world (src/common/worldreset.h): Kenshi's import flags, which save is imported, the records
   store's re-key, and the save number the reset world's first save takes (src/common/restoreguard.h ReserveGen) ---- */
void t_t36_reset_carry()
{
    using namespace worldreset;
    {   /* the import's flags: research only, plus the screen's own 2 */
        const unsigned int f = ResetImportFlags();
        if (f != 0x0Au) Fail("carry: the import flags are not 0x0A");
        if ((f & kImportResearch) == 0) Fail("carry: research is not imported");
        if ((f & (kImportResetPositions | kImportBuildings | kImportDeadNpcs | kImportRelations)) != 0) Fail("carry: positions, buildings, dead NPCs or relations are imported");
        const unsigned int all[] = { kImportResetPositions, kImportScreenBit, kImportBuildings, kImportResearch, kImportDeadNpcs, kImportRelations };
        unsigned int seen = 0;
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
        {
            if (all[i] == 0 || (all[i] & (all[i] - 1)) != 0 || (seen & all[i]) != 0) Fail("carry: the import flags are not six single bits");
            seen |= all[i];
        }
    }
    const std::string a = coopworld::WorldIdMake(1790000000LL, 0x0a1b2c3du), b = coopworld::WorldIdMake(1791000000LL, 0x1b2c3d4eu);
    const std::string c = coopworld::WorldIdMake(1792000000LL, 0x2c3d4e5fu), other = coopworld::WorldIdMake(1789000000LL, 0x3d4e5f60u);
    {   /* the previous id, from world.txt as two resets leave it: a -> b -> c */
        std::string t = coopworld::WorldTxtFormatWithId("Ridge", 1790000000LL, a, false), t2, t3, oldId;
        if (coopworld::WorldTxtAfterReset(t, b, 1791000000LL, false, &t2, &oldId) != coopworld::kResetTxtOk || oldId != a) Fail("carry: the first reset");
        if (coopworld::WorldTxtAfterReset(t2, c, 1792000000LL, true, &t3, &oldId) != coopworld::kResetTxtOk || oldId != b) Fail("carry: the second reset");
        std::vector<std::string> e;
        if (!coopworld::WorldTxtParseEarlier(t3, &e, 0, 0) || e.size() != 2 || e[0] != a || e[1] != b) Fail("carry: the earlier ids are not a then b");
        if (!coopworld::WorldIdIsEarlier(t3, a) || !coopworld::WorldIdIsEarlier(t3, b) || coopworld::WorldIdIsEarlier(t3, c) || coopworld::WorldIdIsEarlier(t3, other))
            Fail("carry: an earlier id is not told from the current one or another world's");
        std::vector<ResetCarryCand> v(4);
        v[0].worldId = other; v[0].quickSave = true; v[0].saveTime = 900ULL;   /* a same-name world that was never this one */
        v[1].worldId = a; v[1].quickSave = true; v[1].saveTime = 500ULL;
        v[2].worldId = b; v[2].quickSave = true; v[2].saveTime = 100ULL;       /* the id the latest reset replaced: taken though older */
        v[3].worldId = b; v[3].quickSave = false; v[3].saveTime = 0ULL;
        if (ResetCarryPick(false, e, v) != 2) Fail("carry: the save of the id the latest reset replaced is not the one imported");
        if (ResetCarryPick(true, e, v) != -1) Fail("carry: a folder of the reset world's own id was found and an import is still chosen");
        if (ResetCarryPick(false, std::vector<std::string>(), v) != -1) Fail("carry: a world with no earlier ids imports");
        std::vector<ResetCarryCand> w(1, v[0]);
        if (ResetCarryPick(false, e, w) != -1) Fail("carry: another world's save of the same name is imported");
        std::vector<ResetCarryCand> x(1, v[3]);
        if (ResetCarryPick(false, e, x) != -1) Fail("carry: a folder with no quick.save is imported");
        std::vector<ResetCarryCand> y(2, v[2]);
        y[1].saveTime = 200ULL;
        if (ResetCarryPick(false, e, y) != 1) Fail("carry: of two saves of one earlier id the newer is not taken");
        y[1].saveTime = 100ULL;
        if (ResetCarryPick(false, e, y) != 0) Fail("carry: of two equal saves the lower index is not taken");
    }
    {   /* the records store's re-key */
        if (ResetRekeyDecide("mp-ridge-00000001", "mp-ridge-00000002", true, false, false) != kRekeyCopy) Fail("carry: the store is not copied");
        if (ResetRekeyDecide("mp-ridge-00000001", "mp-ridge-00000002", false, false, false) != kRekeyNoOld) Fail("carry: a missing old store");
        if (ResetRekeyDecide("mp-ridge-00000001", "mp-ridge-00000002", true, true, false) != kRekeyNewThere) Fail("carry: the reset world's store is overwritten");
        if (ResetRekeyDecide("mp-ridge-00000001", "mp-ridge-00000002", true, false, true) != kRekeyOldOpen) Fail("carry: an open store is copied");
        if (ResetRekeyDecide("", "mp-ridge-00000002", true, false, false) != kRekeyNoName || ResetRekeyDecide("MP-Ridge-1", "mp-ridge-1", true, false, false) != kRekeyNoName)
            Fail("carry: a store with no name, or onto itself");
        if (coopown::PlayerStoreName("Ridge", "p.1", a) == coopown::PlayerStoreName("Ridge", "p.1", b)) Fail("carry: two ids name one store");
    }
    {   /* the save number: the reset keeps world.gen and each profile's number, so the reset world's first save is numbered above them */
        unsigned int reserved = 0;
        const unsigned int profileGen = 57, hostHigh = 60;
        const unsigned int first = restoreguard::ReserveGen(&reserved, profileGen > hostHigh ? profileGen : hostHigh, 0);
        if (first <= profileGen || first <= hostHigh || first != 61u) Fail("carry: the first save after the import is not above the profile's and the host's numbers");
        unsigned int r2 = 70;
        if (restoreguard::ReserveGen(&r2, 60, 0) != 71u) Fail("carry: a number already taken this run is reused");
    }
}
/* ---- T-36: the world as it was keeps its id under a new name - its saves and its players' records stores are found by the id, the
   name only shown; the reset world's carry copies a player's store; the automatic load loads in the old world and imports in the
   reset world (src/common/profiles.h, permdelete.h, ownrec.h, worldreset.h, worlddir.h) ---- */
void t_t36_byid()
{
    using namespace coopprof;
    const std::string P(32, 'b');
    const std::string P1 = ProfileId(P, 1), P2 = ProfileId(P, 2);
    const std::string A = coopworld::WorldIdMake(1790000000LL, 0x0a1b2c3du), B = coopworld::WorldIdMake(1791000000LL, 0x1b2c3d4eu);
    const std::string other = coopworld::WorldIdMake(1789000000LL, 0x3d4e5f60u);
    const std::string oldName = worldreset::ResetOldWorldName("Ridge", 1);
    if (oldName != "Ridge - before reset 1") Fail("byid: the old world's name");
    if (worldreset::ResetOldWorldBase(oldName) != "Ridge" || worldreset::ResetOldWorldBase("Ridge") != "" || worldreset::ResetOldWorldBase("Ridge - before reset 01") != "")
        Fail("byid: the name before ' - before reset <n>'");
    {   /* a world as it was that was itself reset: every ' - before reset <n>' taken off in turn */
        const std::string twice = worldreset::ResetOldWorldName(oldName, 1);
        const std::vector<std::string> b2 = worldreset::ResetOldWorldBases(twice), b1 = worldreset::ResetOldWorldBases(oldName);
        if (twice != "Ridge - before reset 1 - before reset 1" || b2.size() != 2 || b2[0] != oldName || b2[1] != "Ridge")
            Fail("byid: the names before ' - before reset <n>', taken off in turn");
        if (b1.size() != 1 || b1[0] != "Ridge" || !worldreset::ResetOldWorldBases("Ridge").empty()
            || !worldreset::ResetOldWorldBases("Ridge - before reset 01").empty())
            Fail("byid: the names before ' - before reset <n>' of a name reset once, or of no old world's name");
    }
    if (!coopworld::SaveIdSameWorld(A, A) || coopworld::SaveIdSameWorld(A, B) || coopworld::SaveIdSameWorld("", "") || coopworld::SaveIdSameWorld("", A))
        Fail("byid: the same world by its id");
    std::vector<SaveDirSeen> d;
    d.push_back(T490Dir("Ridge - Multiplayer - Bob", "Ridge", P1, A, 1790000500LL));      /* 0: saved in the world as it was, under its name then */
    d.push_back(T490Dir("elsewhere", "Ridge", P2, A, 1790000600LL));                      /* 1: another profile of that world */
    d.push_back(T490Dir("Dune - Multiplayer - Bob", "Dune", P1, other, 1790000700LL));    /* 2: another world */
    WorldIdent oldW; oldW.id = A;
    WorldIdent newW; newW.id = B;
    int owned = 9, otherN = 9;
    /* the old world, hosted under its new name: its save is found by the id, and the automatic load LOADS it */
    const int atOld = SaveFolderScanChoose(d, oldName, P1, 1, &owned, &oldW, &otherN);
    if (atOld != 0 || owned != 1 || otherN != 0) Fail("byid: the old world does not find its own save under its new name");
    if (!SaveDirIsProfiles(d[0], oldName, P1, 1, &oldW) || SaveDirIsProfiles(d[1], oldName, P1, 1, &oldW) || SaveDirIsProfiles(d[2], oldName, P1, 1, &oldW))
        Fail("byid: the id match takes another profile's or another world's folder");
    if (SaveDirIsProfiles(d[0], oldName, P1, 1)) Fail("byid: without an identity the name alone decides, as before");
    const bool quickOld = atOld >= 0 && d[(size_t)atOld].saveTime != 0ULL;
    if (AutoLoadDecide(atOld >= 0, quickOld, false) != kAutoLoadLoad) Fail("byid: the old world's automatic load does not load its save");
    /* the reset world, under the usual name: the old world's save is not its own (counted as another world's) - the carry IMPORTS it */
    const int atNew = SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &newW, &otherN);
    if (atNew != -1 || owned != 0 || otherN != 1) Fail("byid: the reset world takes the old world's save as its own");
    std::vector<std::string> earlier(1, A);
    std::vector<worldreset::ResetCarryCand> cands;
    std::vector<size_t> where;
    for (size_t i = 0; i < d.size(); ++i)
        if (SaveDirIsProfilesWithId(d[i], P1, 1))
        {
            worldreset::ResetCarryCand c; c.worldId = d[i].worldId; c.saveTime = d[i].saveTime; c.quickSave = d[i].saveTime != 0ULL;
            cands.push_back(c); where.push_back(i);
        }
    const int pick = worldreset::ResetCarryPick(atNew >= 0, earlier, cands);
    if (pick < 0 || where[(size_t)pick] != 0) Fail("byid: the reset world does not import the old world's save");
    if (worldreset::ResetCarryPick(atNew >= 0, std::vector<std::string>(), cands) != -1) Fail("byid: a world listing no earlier id imports");
    if (worldreset::ResetCarryPick(atOld >= 0, earlier, cands) != -1) Fail("byid: the old world, its own save found, imports");
    {   /* a save of the world as it was that already carries its later name is still a carry candidate; no id, no candidate */
        const SaveDirSeen renamed = T490Dir("x", oldName.c_str(), P1, A, 1790000800LL);
        SaveDirSeen noId = renamed; noId.worldId.clear();
        if (!SaveDirIsProfilesWithId(renamed, P1, 1) || SaveDirIsProfilesWithId(renamed, P2, 2) || SaveDirIsProfilesWithId(noId, P1, 1))
            Fail("byid: the carry's candidate test");
    }
    /* not imported yet = no folder of the reset world's own id: after the import's first save it is found, in that world only */
    d.push_back(T490Dir("Ridge - Multiplayer - Bob 2", "Ridge", P1, B, 1791000500LL));    /* 3: the reset world's own */
    if (SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &newW) != 3) Fail("byid: the reset world's own folder is not found");
    if (SaveFolderScanChoose(d, oldName, P1, 1, &owned, &oldW) != 0) Fail("byid: the old world takes the reset world's folder");
    /* a deleted world takes its saves by id whatever name they carry, never the other world's */
    if (!swdel::SaveDirGoesWithWorld(d[0], oldName, A) || swdel::SaveDirGoesWithWorld(d[3], oldName, A) || swdel::SaveDirGoesWithWorld(d[0], "Ridge", B)
        || swdel::SaveDirGoesWithWorld(d[2], oldName, A) || !swdel::SaveDirGoesWithProfile(d[0], oldName, A, P1, 1))
        Fail("byid: a deleted world's saves");
    {
        const std::vector<std::string> keep;
        const WorldLeftovers lo = swdel::WorldDeleteFind(d, oldName, A, keep, P, 2);
        if (lo.folders.size() != 2 || lo.folders[0] != 0 || lo.folders[1] != 1) Fail("byid: the old world's delete does not take its two saves");
        const std::vector<std::string> st = swdel::StoreNamesToDelete(lo.worldKeys, lo.profileIds, A);
        if (std::find(st.begin(), st.end(), coopown::PlayerStoreName("Ridge", P1, A)) == st.end()) Fail("byid: the old world's delete misses its store under its earlier name");
        if (std::find(st.begin(), st.end(), coopown::PlayerStoreName("Ridge", P1, B)) != st.end()) Fail("byid: the old world's delete takes the reset world's store");
    }
    /* the records store: the old world finds the store named for the name its saves were made under; the reset world's is another */
    const std::string cur = coopown::PlayerStoreName(oldName, P1, A), was = coopown::PlayerStoreName("Ridge", P1, A);
    const std::string legacyOld = coopown::PlayerStoreName(oldName, P1);
    std::vector<std::string> keys; keys.push_back("Ridge"); keys.push_back(oldName); keys.push_back("Ridge");
    const std::vector<std::string> byId = coopown::PlayerStoreNamesById(oldName, keys, P1, A);
    if (byId.size() != 1 || byId[0] != was) Fail("byid: the store names by id (each once, never the current name's)");
    if (!coopown::PlayerStoreNamesById(oldName, keys, P1, "").empty()) Fail("byid: a world with no id looks for a store by id");
    if (coopown::PlayerStoreName("Ridge", P1, B) == was) Fail("byid: the reset world's copy is named as the old world's store");
    {   /* the mark's store is taken only when it is named for this world: a name its id was saved under, with the id (or with no id
           for a world upgraded to an id) */
        std::vector<std::string> mk(1, std::string("Ridge"));
        if (!coopown::MarkStoreTakeById(was, P1, P1, cur, legacyOld, mk, A, false) || coopown::MarkStoreTakeById(was, P2, P1, cur, legacyOld, mk, A, false)
            || coopown::MarkStoreTakeById(cur, P1, P1, cur, legacyOld, mk, A, false) || coopown::MarkStoreTakeById("mytest", P1, P1, cur, legacyOld, mk, A, false)
            || coopown::MarkStoreTakeById(legacyOld, P1, P1, cur, legacyOld, mk, A, false) || coopown::MarkStoreTakeById(was, P1, "", cur, legacyOld, mk, A, false))
            Fail("byid: the store a save's mark names");
        if (coopown::MarkStoreTakeById(coopown::PlayerStoreName("Dune", P1, A), P1, P1, cur, legacyOld, mk, A, false))
            Fail("byid: a mark naming a store of a name this world never had is taken");
        if (coopown::MarkStoreTakeById(coopown::PlayerStoreName("Ridge", P1, B), P1, P1, cur, legacyOld, mk, A, false))
            Fail("byid: a mark naming another id's store is taken");
        if (coopown::MarkStoreTakeById(was, P1, P1, cur, legacyOld, std::vector<std::string>(), A, false))
            Fail("byid: a mark is taken with no name of this world to check it by");
        const std::string preId = coopown::PlayerStoreName("Ridge", P1);
        if (coopown::MarkStoreTakeById(preId, P1, P1, cur, legacyOld, mk, A, false) || !coopown::MarkStoreTakeById(preId, P1, P1, cur, legacyOld, mk, A, true))
            Fail("byid: a store from before ids under an earlier name is taken only by an upgraded world");
    }
    if (!coopown::IdentityMatchesWorld("Ridge", P1, oldName, P1, true) || coopown::IdentityMatchesWorld("Ridge", P1, oldName, P1, false)
        || coopown::IdentityMatchesWorld("Ridge", P2, oldName, P1, true) || coopown::IdentityMatchesWorld("", P1, oldName, P1, true)
        || !coopown::IdentityMatchesWorld(oldName, P1, oldName, P1, false))
        Fail("byid: the store's identity line under an earlier name");
    /* the carry COPIES: the decision is a copy, and both stores are there afterwards (the old one never named for the reset world) */
    if (worldreset::ResetRekeyDecide(was, coopown::PlayerStoreName("Ridge", P1, B), true, false, false) != worldreset::kRekeyCopy
        || std::string(worldreset::ResetRekeyName(worldreset::kRekeyCopy)) != "copied")
        Fail("byid: the carry does not copy");
    {   /* the carry names the old store from the carried save's key-file world name first, then by the old id under every other name */
        const std::string C = coopworld::WorldIdMake(1792000000LL, 0x2c3d4e5fu);
        const std::string old2 = worldreset::ResetOldWorldName("Ridge", 2);
        std::vector<std::string> others; others.push_back("Ridge"); others.push_back(old2);
        const std::vector<std::string> n1 = coopown::CarryOldStoreNames("Ridge", others, P1, B);
        if (n1.size() != 2 || n1[0] != coopown::PlayerStoreName("Ridge", P1, B) || n1[1] != coopown::PlayerStoreName(old2, P1, B))
            Fail("carry: the old store's names (the carried save's name first, each once)");
        if (std::find(n1.begin(), n1.end(), coopown::PlayerStoreName("Ridge", P1, C)) != n1.end()) Fail("carry: the reset world's own store is taken as the old one");
        /* a player who first played the old world under its later name: the save's key file holds that name, so that store comes first */
        std::vector<std::string> o2(1, std::string("Ridge"));
        const std::vector<std::string> n2 = coopown::CarryOldStoreNames(oldName, o2, P1, A);
        if (n2.size() != 2 || n2[0] != coopown::PlayerStoreName(oldName, P1, A) || n2[1] != was)
            Fail("carry: a store made under the old world's later name is not looked for first, or its earlier name not after it");
        if (!coopown::CarryOldStoreNames("Ridge", others, P1, "").empty()) Fail("carry: an old store is looked for with no old id");
    }
    {   /* a leftover .resetting folder is used only when it is this world's reset; finishing also needs the world as it was beside it */
        const std::string wt = coopworld::WorldTxtFormatWithId("Ridge", 1790000000LL, A, false);
        std::string st, oid;
        if (coopworld::WorldTxtAfterReset(wt, B, 1791000000LL, false, &st, &oid) != coopworld::kResetTxtOk || oid != A) Fail("stage: the reset world's world.txt");
        if (!worldreset::ResetStageIsThisReset(st, wt)) Fail("stage: this world's own reset stage is not used");
        const std::string oldTxt = coopworld::WorldTxtRenamed(wt, oldName);
        if (!worldreset::ResetStageIsThisReset(st, oldTxt)) Fail("stage: a reset stopped after world.txt took the old name does not use its stage");
        if (worldreset::ResetStageIsThisReset("", wt) || worldreset::ResetStageIsThisReset(wt, wt)) Fail("stage: a stage with no world.txt or no line 3 is used");
        const std::string otherWt = coopworld::WorldTxtFormatWithId("Ridge", 1789000000LL, other, false);
        if (worldreset::ResetStageIsThisReset(st, otherWt)) Fail("stage: another world's stage is used");
        if (worldreset::ResetStageIsThisReset(st, st)) Fail("stage: an earlier reset's stage is used (the world now holds the id that reset made)");
        if (worldreset::ResetStageIsThisReset(st, coopworld::WorldTxtFormat("Ridge", 1790000000LL))) Fail("stage: a world.txt with no id accepts a stage");
        if (!worldreset::ResetStageFinishes(st, oldTxt)) Fail("stage: the world as it was beside the stage is not recognised");
        if (worldreset::ResetStageFinishes(st, wt)) Fail("stage: a world still named as the reset world is taken for the world as it was");
        if (worldreset::ResetStageFinishes(st, coopworld::WorldTxtRenamed(otherWt, oldName))) Fail("stage: another id's folder is taken for the world as it was");
        if (worldreset::ResetStageFinishes(st, coopworld::WorldTxtRenamed(wt, "Dune - before reset 1"))) Fail("stage: another world's old folder is taken");
        if (worldreset::ResetStageFinishes("", oldTxt)) Fail("stage: a stage with no world.txt is finished");
        /* a world with no id line: its reset's stage has an id and the world's own earlier ids (none here) */
        const std::string nw = coopworld::WorldTxtFormat("Ridge", 1790000000LL);
        std::string ns, noid;
        if (coopworld::WorldTxtAfterReset(nw, B, 1791000000LL, false, &ns, &noid) != coopworld::kResetTxtOk || !noid.empty())
            Fail("stage: the reset world's world.txt of a world with no id");
        if (!worldreset::ResetStageIsThisReset(ns, nw)) Fail("stage: a world with no id does not use its own reset's stage");
        if (!worldreset::ResetStageIsThisReset(ns, coopworld::WorldTxtRenamed(nw, oldName)))
            Fail("stage: a world with no id, stopped after world.txt took the old name, does not use its stage");
        if (!worldreset::ResetStageFinishes(ns, coopworld::WorldTxtRenamed(nw, oldName))) Fail("stage: the world as it was of a world with no id is not recognised");
        if (worldreset::ResetStageIsThisReset(nw, nw)) Fail("stage: a stage with no id line is used by a world with no id");
        if (worldreset::ResetStageIsThisReset(ns, wt) || worldreset::ResetStageIsThisReset(st, nw))
            Fail("stage: a stage of a world with no id and one of a world with an id are taken for each other");
        if (worldreset::ResetStageIsThisReset(ns, "")) Fail("stage: a stage is used with no world.txt to match");
        if (worldreset::ResetStaleName("C:\\w\\Ridge.resetting", 1790000000LL, 0) != "C:\\w\\Ridge.resetting.stale-1790000000"
            || worldreset::ResetStaleName("C:\\w\\Ridge.resetting", 1790000000LL, 2) != "C:\\w\\Ridge.resetting.stale-1790000000-2")
            Fail("stage: the name a stale stage is moved aside to");
        /* the renames: tried again only while Windows refuses for a moment, at most kResetRenameTries tries in all, short waits */
        if (!worldreset::ResetRenameRetryWanted(5ul, 1) || !worldreset::ResetRenameRetryWanted(32ul, worldreset::kResetRenameTries - 1)
            || !worldreset::ResetRenameRetryWanted(33ul, 1))
            Fail("rename: a refusal for a moment is not tried again");
        if (worldreset::ResetRenameRetryWanted(5ul, worldreset::kResetRenameTries) || worldreset::ResetRenameRetryWanted(183ul, 1)
            || worldreset::ResetRenameRetryWanted(2ul, 1) || worldreset::ResetRenameRetryWanted(3ul, 1))
            Fail("rename: tried again past the last try, or on a refusal that will not pass (already there, not found)");
        unsigned int total = 0;
        for (unsigned int k = 1; k < worldreset::kResetRenameTries; ++k) total += worldreset::ResetRenameWaitMs(k);
        if (worldreset::kResetRenameTries < 3 || worldreset::ResetRenameWaitMs(1) == 0 || total > 5000) Fail("rename: the waits are not short and bounded");
    }
}
/* ---- T-36: the reset world's import passes the profile-folder load gate and nothing else does; the carry is decided again at every
   start until the reset world's own folder is saved, and the save right after the import is armed once its world runs; DELETE WORLD
   of the world as it was keeps the save a reset world still imports from, and deletes it once that world has a save of its own ---- */
void t_t36_import_gate_redecide_delete()
{
    using namespace coopprof;
    const std::string P(32, 'c');
    const std::string P1 = ProfileId(P, 1), P2 = ProfileId(P, 2);
    const std::string A = coopworld::WorldIdMake(1790000000LL, 0x0a1b2c3du), B = coopworld::WorldIdMake(1791000000LL, 0x1b2c3d4eu);
    const std::string other = coopworld::WorldIdMake(1789000000LL, 0x3d4e5f60u);
    const std::string from = "Ridge - Multiplayer - Bob", to = "Ridge - Multiplayer - Bob 2";
    /* the gate: the profile rule alone refuses the import (as it did); the reset's own import passes; nothing else does */
    if (ProfileLoadDecide(false, to, kProfReqImport, from, false) != kProfLoadRefuse) Fail("gate: the profile rule alone no longer refuses an import");
    if (!ProfileResetImportPass(kProfReqImport, from, to, true, from, to, B, B)) Fail("gate: the reset world's import is refused");
    if (!ProfileResetImportPass(kProfReqImport, "C:/saves/" + from + "/", to, true, from, to, B, B)) Fail("gate: the reset world's import given as a path is refused");
    if (!ProfileResetImportPass(kProfReqImport, "ridge - multiplayer - bob", to, true, from, to, B, B)) Fail("gate: the reset world's import in another case is refused");
    if (ProfileResetImportPass(kProfReqImport, "Dune - Multiplayer - Bob", to, true, from, to, B, B)) Fail("gate: an unrelated import passes");
    if (ProfileResetImportPass(kProfReqImport, to, to, true, from, to, B, B)) Fail("gate: an import of the decided folder itself passes");
    if (ProfileResetImportPass(kProfReqLoad, from, to, true, from, to, B, B)) Fail("gate: a load of the carried folder passes");
    if (ProfileResetImportPass(kProfReqNewGame, from, to, true, from, to, B, B)) Fail("gate: NEW GAME passes as the import");
    if (ProfileResetImportPass(kProfReqImport, from, to, false, from, to, B, B)) Fail("gate: an import passes with no carry decided for this pick");
    if (ProfileResetImportPass(kProfReqImport, from, "Ridge - Multiplayer - Ann", true, from, to, B, B)) Fail("gate: the import passes while another folder is decided");
    if (ProfileResetImportPass(kProfReqImport, from, to, true, from, to, B, other)) Fail("gate: the import passes in another world");
    if (ProfileResetImportPass(kProfReqImport, from, to, true, "", to, B, B) || ProfileResetImportPass(kProfReqImport, from, to, true, from, to, "", "")
        || ProfileResetImportPass(kProfReqImport, from, "", true, from, "", B, B))
        Fail("gate: an import passes with no carry folder, no world id or no decided folder");
    if (ProfileLoadDecide(false, to, kProfReqLoad, from, true) != kProfLoadRefuse || ProfileLoadDecide(false, to, kProfReqLoad, "s1", false) != kProfLoadRefuse)
        Fail("gate: an unrelated load is no longer refused");
    /* the re-decide: no folder of the reset world's id, or one whose save never finished - the carry is decided again and imports the
       save of the world as it was; once the reset world's folder is saved it is not */
    std::vector<SaveDirSeen> d;
    d.push_back(T490Dir(from.c_str(), "Ridge", P1, A, 1790000500LL));   /* 0: the save of the world as it was */
    WorldIdent newW; newW.id = B;
    const std::vector<std::string> earlier(1, A);
    int owned = 0;
    int at = SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &newW);
    if (at != -1 || !worldreset::ResetCarryWanted(at >= 0, false)) Fail("redecide: the reset world's folder never saved - the carry is not decided");
    d.push_back(T490Dir(to.c_str(), "Ridge", P1, B, 0));                /* 1: the reset world's folder, its save never finished */
    at = SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &newW);
    if (at != 1 || !worldreset::ResetCarryWanted(true, d[(size_t)at].saveTime != 0ULL)) Fail("redecide: a reset world folder with no quick.save stops the import");
    {
        std::vector<worldreset::ResetCarryCand> cands;
        std::vector<size_t> where;
        for (size_t i = 0; i < d.size(); ++i)
            if (SaveDirIsProfilesWithId(d[i], P1, 1))
            {
                worldreset::ResetCarryCand c; c.worldId = d[i].worldId; c.saveTime = d[i].saveTime; c.quickSave = d[i].saveTime != 0ULL;
                cands.push_back(c); where.push_back(i);
            }
        const int pick = worldreset::ResetCarryPick(false, earlier, cands);
        if (pick < 0 || where[(size_t)pick] != 0) Fail("redecide: the next start does not import the save of the world as it was again");
    }
    d[1] = T490Dir(to.c_str(), "Ridge", P1, B, 1791000600LL);           /* the reset world's folder saved */
    at = SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &newW);
    if (at != 1 || worldreset::ResetCarryWanted(true, d[(size_t)at].saveTime != 0ULL)) Fail("redecide: the import runs again after the reset world's folder is saved");
    /* the records store's copy met a second time never copies over the store the first carry made */
    if (worldreset::ResetRekeyDecide(coopown::PlayerStoreName("Ridge", P1, A), coopown::PlayerStoreName("Ridge", P1, B), true, true, false) != worldreset::kRekeyNewThere)
        Fail("redecide: a second carry copies over the reset world's records store");
    /* the save right after the import: armed once the import's world runs, then started by the save after character creation's step */
    if (!worldreset::ResetImportSaveArm(true, true, 4, 6, true)) Fail("arm: the import's world runs and its save is not armed");
    if (worldreset::ResetImportSaveArm(true, true, 4, 4, true)) Fail("arm: armed before the import built its world");
    if (worldreset::ResetImportSaveArm(true, true, 4, 6, false)) Fail("arm: armed while the world is still loading");
    if (worldreset::ResetImportSaveArm(false, true, 4, 6, true) || worldreset::ResetImportSaveArm(true, false, 4, 6, true))
        Fail("arm: armed with no import let through, or for another pick");
    /* ...and only once every awake player squad's characters are read in; none awake = not built yet */
    if (!worldreset::ResetImportSquadsRead(true, 1, 1, 0) || !worldreset::ResetImportSquadsRead(true, 2, 2, 0)) Fail("squads read: every awake squad read and the save waits");
    if (worldreset::ResetImportSquadsRead(true, 1, 0, 0) || worldreset::ResetImportSquadsRead(true, 2, 1, 0)) Fail("squads read: armed before every awake squad is read");
    if (worldreset::ResetImportSquadsRead(true, 0, 0, 0)) Fail("squads read: armed with no awake player squad");
    if (worldreset::ResetImportSquadsRead(true, 1, 1, 1) || worldreset::ResetImportSquadsRead(false, 1, 1, 0)) Fail("squads read: armed on an unreadable squad or list");
    {   /* the squads read in since the import, as the load hook notes them */
        worldreset::SquadReadSet rs = worldreset::SquadReadSet();
        int sa = 0, sb = 0; void* pa = &sa; void* pb = &sb;
        if (worldreset::SquadReadNote(&rs, pa) != 1 || worldreset::SquadReadNote(&rs, pa) != 0 || !worldreset::SquadReadHas(&rs, pa) || worldreset::SquadReadHas(&rs, pb))
            Fail("squad read set: a load noted once, another squad not");
        worldreset::SquadReadForget(&rs, pa);
        if (worldreset::SquadReadHas(&rs, pa) || rs.n != 0) Fail("squad read set: a freed platoon is still marked read in");
        worldreset::SquadReadNote(&rs, pa); worldreset::SquadReadNote(&rs, pb); worldreset::SquadReadClear(&rs);
        if (worldreset::SquadReadHas(&rs, pa) || worldreset::SquadReadHas(&rs, pb)) Fail("squad read set: a mark survives the import or a world load");
        if (worldreset::SquadReadNote(&rs, 0) != 0 || rs.n != 0) Fail("squad read set: no platoon noted");
        char many[worldreset::kSquadReadCap + 1];
        for (int i = 0; i < worldreset::kSquadReadCap; ++i) worldreset::SquadReadNote(&rs, &many[i]);
        if (worldreset::SquadReadNote(&rs, &many[worldreset::kSquadReadCap]) != -1 || rs.full != 1 || !worldreset::SquadReadHas(&rs, &many[0])
            || worldreset::SquadReadHas(&rs, &many[worldreset::kSquadReadCap]))
            Fail("squad read set: a full list overwrote an entry or did not count the refusal");
        worldreset::SquadReadForget(&rs, &many[0]);
        if (worldreset::SquadReadHas(&rs, &many[0]) || !worldreset::SquadReadHas(&rs, &many[worldreset::kSquadReadCap - 1]) || rs.n != worldreset::kSquadReadCap - 1)
            Fail("squad read set: forgetting one platoon dropped another");
    }
    {
        coopown::CreatedSave cs; cs.pending = 1; cs.worldArmed = 6;
        if (coopown::CreatedPendingStep(cs, 6, false, true) != coopown::kCrStartNow || coopown::CreatedPendingStep(cs, 6, false, false) != coopown::kCrWait
            || coopown::CreatedPendingStep(cs, 7, false, true) != coopown::kCrDropWorld)
            Fail("arm: the armed save does not start, wait and drop as the save after character creation's");
    }
    /* DELETE WORLD of the world as it was keeps the save the reset world still imports from (and its profile's records stores) */
    const std::string oldName = worldreset::ResetOldWorldName("Ridge", 1);
    std::vector<SaveDirSeen> e;
    e.push_back(T490Dir(from.c_str(), "Ridge", P1, A, 1790000500LL));               /* 0: profile 1's save of the world as it was */
    e.push_back(T490Dir("Ridge - Multiplayer - Cy", "Ridge", P2, A, 1790000600LL)); /* 1: profile 2's */
    e.push_back(T490Dir("Ridge - Multiplayer - Dee", "Ridge", P1, A, 0));           /* 2: a folder of that world with no quick.save */
    std::vector<swdel::ResetChildWorld> kids(1);
    kids[0].id = B; kids[0].earlier.push_back(other); kids[0].earlier.push_back(A);
    const std::vector<std::string> keep;
    std::vector<size_t> carried;
    WorldLeftovers lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, &kids, &carried);
    if (lo.folders.size() != 1 || lo.folders[0] != 2 || lo.playing.size() != 2 || carried.size() != 2 || carried[0] != 0 || carried[1] != 1)
        Fail("delete: the saves the reset world still imports from are not kept (or a save with no quick.save is)");
    {
        const std::vector<std::string> kept = swdel::StoreNamesToDelete(lo.worldKeys, lo.playingIds, A);
        if (std::find(kept.begin(), kept.end(), coopown::PlayerStoreName("Ridge", P1, A)) == kept.end()) Fail("delete: the kept save's records store is not kept with it");
    }
    e.push_back(T490Dir(to.c_str(), "Ridge", P1, B, 0));                          /* 3: profile 1's reset world folder, never saved */
    carried.clear();
    lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, &kids, &carried);
    if (carried.size() != 2) Fail("delete: a reset world folder with no quick.save counts as its own save");
    e[3] = T490Dir(to.c_str(), "Ridge", P1, B, 1791000500LL);                      /* profile 1 saved in the reset world */
    carried.clear();
    lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, &kids, &carried);
    if (lo.folders.size() != 2 || lo.folders[0] != 0 || lo.folders[1] != 2 || carried.size() != 1 || carried[0] != 1)
        Fail("delete: a save the reset world no longer needs is kept, or the other profile's is not");
    carried.clear();
    lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, 0, &carried);
    if (lo.folders.size() != 3 || !carried.empty()) Fail("delete: with no reset world everything of the world goes");
    std::vector<swdel::ResetChildWorld> otherKids(1);
    otherKids[0].id = B; otherKids[0].earlier.push_back(other);
    lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, &otherKids, &carried);
    if (lo.folders.size() != 3 || !carried.empty()) Fail("delete: a world reset from another world keeps this world's saves");
    /* a world folder whose world.txt is there but could not be read may be a world reset from this one: every save that could be an
       import source stays (a folder with no quick.save still goes) */
    std::vector<swdel::ResetChildWorld> unsure(otherKids);
    unsure.push_back(swdel::ResetChildWorld());
    unsure.back().unread = true;
    carried.clear();
    lo = swdel::WorldDeleteFind(e, oldName, A, keep, P, 2, &unsure, &carried);
    if (lo.folders.size() != 1 || lo.folders[0] != 2 || carried.size() != 2 || carried[0] != 0 || carried[1] != 1)
        Fail("delete: an unreadable world.txt lets a save a reset world may import from go (or keeps a save with no quick.save)");
    if (swdel::ResetChildWorld().unread) Fail("delete: a reset world read from its world.txt starts as unread");
    /* profile delete in the world as it was: the profile's save stays while the reset world has no save of its own for that profile,
       and goes once it has one; an unreadable world.txt keeps it */
    {
        std::vector<SaveDirSeen> f;
        f.push_back(T490Dir(from.c_str(), "Ridge", P1, A, 1790000500LL));   /* 0: profile 1's save of the world as it was */
        f.push_back(T490Dir(to.c_str(), "Ridge", P1, B, 0));                /* 1: profile 1's reset world folder, never saved */
        if (!swdel::SaveDirGoesWithProfile(f[0], oldName, A, P1, 1) || !swdel::SaveDirCarriedOnward(f, 0, kids))
            Fail("profile delete: the save the reset world still imports from is not kept");
        if (swdel::SaveDirGoesWithProfile(f[1], oldName, A, P1, 1)) Fail("profile delete: the reset world's folder goes with the world as it was");
        f[1] = T490Dir(to.c_str(), "Ridge", P1, B, 1791000500LL);           /* profile 1 saved in the reset world */
        if (swdel::SaveDirCarriedOnward(f, 0, kids)) Fail("profile delete: the save is kept after the reset world has a save of its own");
        if (!swdel::SaveDirCarriedOnward(f, 0, unsure)) Fail("profile delete: an unreadable world.txt lets the save go");
    }
    /* the carry ends once the reset world's own folder holds its save: the gate then refuses the import of the save of the world as it was */
    if (worldreset::ResetCarryEnds(true, false, B, B)) Fail("end: the carry ends before the reset world's folder is saved");
    if (!worldreset::ResetCarryEnds(true, true, B, B) || !worldreset::ResetCarryEnds(true, true, "", B)) Fail("end: the reset world's own save does not end the carry");
    if (worldreset::ResetCarryEnds(true, true, other, B) || worldreset::ResetCarryEnds(false, true, B, B))
        Fail("end: a save of another world id ends the carry, or a carry that is not live ends");
    {
        const bool live = !worldreset::ResetCarryEnds(true, true, B, B);
        if (ProfileResetImportPass(kProfReqImport, from, to, live, from, to, B, B)) Fail("end: after the reset world's save the gate lets the old save's import through again");
    }
}
/* ---- a newer record goes into a waking squad once (src/common/squadwriter.h WakeLoadRoad): the store reloads only when the
   sleeping container's "already read" byte is not 0; with the byte at 0 (the first wake after a save load) the record is left to the
   engine's read ---- */
void t_t676_wake_load_road()
{
    using namespace coopsquad;
    if (WakeLoadRoad(true, false, false, false, 1) != kWakeRoadLoad) Fail("wake: a container already read takes the store's reload");
    if (WakeLoadRoad(true, false, false, false, -1) != kWakeRoadLoad) Fail("wake: a byte that could not be read keeps the reload");
    if (WakeLoadRoad(true, false, false, false, 0) != kWakeRoadSwap) Fail("wake: a container whose 'already read' byte is 0 is left to the engine's read (no reload)");
    if (WakeLoadRoad(false, false, false, false, 0) != kWakeRoadNone || WakeLoadRoad(false, false, false, false, 1) != kWakeRoadNone) Fail("wake: a record not newer is not applied");
    if (WakeLoadRoad(true, true, false, false, 0) != kWakeRoadNone || WakeLoadRoad(true, true, false, false, 1) != kWakeRoadNone) Fail("wake: a gone record is not applied");
    if (WakeLoadRoad(true, false, false, true, 0) != kWakeRoadSkipCtx || WakeLoadRoad(true, false, false, true, 1) != kWakeRoadSkipCtx) Fail("wake: a context platoon never takes a record");
    if (WakeLoadRoad(true, false, true, false, 0) != kWakeRoadSkipHeld || WakeLoadRoad(true, false, true, false, 1) != kWakeRoadSkipHeld) Fail("wake: a squad whose area another game holds waits");
    if (WakeLoadRoad(true, false, true, true, 1) != kWakeRoadSkipCtx) Fail("wake: the context rule is decided before the held-area rule");
    if (WakeLoadRoad(true, false, true, false, -1) != kWakeRoadSkipHeld || WakeLoadRoad(true, false, false, true, -1) != kWakeRoadSkipCtx) Fail("wake: a byte that could not be read changes neither the held-area nor the context rule");
    if (WakeLoadRoad(true, true, false, true, 0) != kWakeRoadNone || WakeLoadRoad(true, true, false, true, -1) != kWakeRoadNone) Fail("wake: a gone record is not applied to a context platoon either");
    if (WakeLoadRoad(false, false, false, true, 0) != kWakeRoadNone || WakeLoadRoad(false, false, false, true, 1) != kWakeRoadNone) Fail("wake: a record not newer is not applied to a context platoon either");
}
/* ---- THE WORLD SERVER'S ENTRY RULES (src/common/storegate.h): before a connection has joined only HELLO (and PROFILES from the
   lobby) is acted on; the field bounds; the HELLO / PROFILES allowance never refuses a joining game's ordinary burst; the recycle
   folder's names and its keep ---- */
void t_t605_store_gate()
{
    using namespace storegate;
    {
        const std::string server = T313ReadFile("../coop-store/store_main.cpp");
        if (server.empty()) Fail("could not read the world server's store_main.cpp");
        else if (server.find("MSG_STORE_HELLO = 29") == std::string::npos || server.find("MSG_PROFILES = 45") == std::string::npos || kMsgHello != 29u || kMsgProfiles != 45u)
            Fail("storegate.h's HELLO / PROFILES numbers are not the server's 29 / 45");
    }
    if (StageOf(0, 0) != kStageNew || StageOf(0, 1) != kStageLobby || StageOf(1, 0) != kStageJoined || StageOf(1, 1) != kStageJoined) Fail("a connection's stage is read wrongly");
    for (unsigned t = 0; t < 256; ++t)
    {
        const int n = AdmitDecide(t, kStageNew), l = AdmitDecide(t, kStageLobby), j = AdmitDecide(t, kStageJoined);
        if (j != kAdmit) Fail("message " + D((double)t) + " from a joined game was refused");
        if (t == 29u) { if (n != kAdmit || l != kAdmit) Fail("HELLO was refused before joining"); }
        else if (t == 45u) { if (n != kRefuseNotJoined || l != kAdmit) Fail("PROFILES: taken before any HELLO, or refused from the lobby"); }
        else if (n != kRefuseNotJoined || l != kRefuseNotJoined) Fail("message " + D((double)t) + " was acted on from a connection that has not joined");
    }
    /* the one packet the hole let through: DELETED_BITS (33), UNIQUE_STATE (36), CLOCK (39), RECORD_GONE (32), a BUNDLE's inner message */
    if (AdmitDecide(33u, kStageNew) == kAdmit || AdmitDecide(36u, kStageNew) == kAdmit || AdmitDecide(39u, kStageNew) == kAdmit || AdmitDecide(32u, kStageLobby) == kAdmit)
        Fail("a world-changing message was taken before joining");
    /* text */
    if (!TextOk("", 10) || !TextOk("abc", 3) || TextOk("abcd", 3)) Fail("TextOk's length bound is wrong");
    if (TextOk(std::string("a\tb"), 10) || TextOk(std::string("a\nb"), 10) || TextOk(std::string(1, '\0'), 10) || TextOk(std::string(1, (char)0x7F), 10)) Fail("a control byte passed TextOk");
    if (!TextOk("\xD0\x9C\xD0\xB8\xD1\x80", 10)) Fail("UTF-8 text (a Cyrillic word) was refused");
    if (IdOk("") || !IdOk(std::string(kIdMax, 'x')) || IdOk(std::string(kIdMax + 1, 'x'))) Fail("IdOk's bounds are wrong");
    if (!IdOk("2849-gamedata.base") || !IdOk("Holy Nation_65537") || !IdOk("45493-Dialogue.mod")) Fail("an ordinary id was refused");
    {
        std::string a("Holy\tNation\x7F"), b("plain");
        if (CleanText(&a) != 2 || a != "Holy?Nation?" || CleanText(&b) != 0 || b != "plain") Fail("CleanText did not replace exactly the control bytes with '?'");
    }
    /* numbers */
    {
        float fi = 0.0f, fn = 0.0f; unsigned ui = 0x7F800000u, un = 0x7FC00000u; memcpy(&fi, &ui, 4); memcpy(&fn, &un, 4);
        if (!FiniteF32(0.0f) || !FiniteF32(-123456.5f) || !FiniteF32(3.0e38f) || FiniteF32(fi) || FiniteF32(-fi) || FiniteF32(fn)) Fail("FiniteF32 is wrong");
        if (!RecordOk("Holy Nation_12", "1234-gamedata.base", "Holy Nation", "", -5000.0f, 1200.0f, 33000.0f)) Fail("an ordinary record was refused");
        if (RecordOk("Holy Nation_12", "s", "f", "", fn, 0.0f, 0.0f) || RecordOk("Holy Nation_12", "s", "f", "", 0.0f, 0.0f, fi) || RecordOk("", "s", "f", "", 0.0f, 0.0f, 0.0f))
            Fail("a record with no id or no real position passed");
    }
    if (!DeletedBitsFactionOk("Holy Nation") || DeletedBitsFactionOk("") || DeletedBitsFactionOk("a\nb") || kBitsBytesMax != 1048576u / 8u) Fail("a deleted-groups list's faction key is judged wrongly");
    if (BitsBytesUsed(0) != 0 || BitsBytesUsed(100) != 100 || BitsBytesUsed(kBitsBytesMax) != kBitsBytesMax || BitsBytesUsed(kBitsBytesMax + 1) != kBitsBytesMax
        || BitsBytesUsed(2000000u) != kBitsBytesMax) Fail("a long deleted-groups list (a world from before the 1,048,576 bound) is not cut to kBitsBytesMax");
    if (!AreasCountOk(0) || !AreasCountOk(4096) || AreasCountOk(4097) || AreasCountOk(0xFFFFFFFFu)) Fail("AreasCountOk's bounds are wrong");
    if (!HelloTextOk("save 1", "") || !HelloTextOk("", "my world") || HelloTextOk(std::string(kTextMax + 1, 'x'), "") || HelloTextOk("a\nb", "")) Fail("HelloTextOk's bounds are wrong");
    if (!RecordGoneOk("Holy Nation_12") || RecordGoneOk("") || !UniqueIdOk("2849-gamedata.base") || UniqueIdOk("a\rb")) Fail("RECORD_GONE / UNIQUE_STATE ids are judged wrongly");
    if (kUniqueIdMax != 4096 || !UniqueIdOk(std::string(4096, 'x')) || UniqueIdOk(std::string(4097, 'x'))) Fail("a named character's id is not bounded at the game's 4096");
    /* the allowance: a joining game's ordinary burst (a lobby HELLO, a pick, a refused pick and a second pick, inside a second) is never refused */
    {
        Allowance a;
        for (int i = 0; i < 4; ++i) if (!AllowanceTake(&a, 100.0 + i * 0.1, kHelloBurst, kHelloPerSec)) Fail("a joining game's fourth HELLO in a second was refused");
        Allowance b; int taken = 0;
        for (int i = 0; i < 1000; ++i) if (AllowanceTake(&b, 5.0, kHelloBurst, kHelloPerSec)) ++taken;
        if (taken != 20) Fail("a flood of HELLOs at one moment took " + D((double)taken) + ", not the 20 allowed");
        if (AllowanceTake(&b, 5.5, kHelloBurst, kHelloPerSec) || !AllowanceTake(&b, 7.0, kHelloBurst, kHelloPerSec)) Fail("the HELLO allowance did not refill one every 2 s");
        Allowance c; AllowanceTake(&c, 0.0, kProfilesBurst, kProfilesPerSec); taken = 0;
        for (int i = 0; i < 1000; ++i) if (AllowanceTake(&c, 100000.0, kProfilesBurst, kProfilesPerSec)) ++taken;
        if (taken != 40) Fail("a long quiet spell let more than the PROFILES burst through");
        if (!RateLimited(29u) || !RateLimited(45u) || RateLimited(28u) || RateLimited(53u) || RateLimited(59u)) Fail("the allowance covers the wrong messages (RECORD, LIVE and BUNDLE are never limited)");
    }
    /* the recycle folder */
    {
        const std::string nm = RecycleName(1760000000LL, 4321UL, 7ULL, "Holy Nation_12.platoon");
        if (nm != "1760000000_4321_7_Holy Nation_12.platoon" || RecycleStampOf(nm) != 1760000000LL) Fail("a recycle name does not read back its stamp: " + nm);
        if (RecycleStampOf("Holy Nation_12.platoon") != -1 || RecycleStampOf("123_x") != -1 || RecycleStampOf("123_4_5_") != -1 || RecycleStampOf("_1_2_a") != -1
            || RecycleStampOf("123_4_a") != -1 || RecycleStampOf("123__5_a") != -1) Fail("a name that is not a recycle name read as one");
        std::vector<RecycleEntry> in;
        RecycleEntry e;
        e.stamp = 1000; e.bytes = 10; in.push_back(e);         /* 0: old */
        e.stamp = 900000; e.bytes = 300; in.push_back(e);      /* 1: recent, big */
        e.stamp = -1; e.bytes = 999999; in.push_back(e);       /* 2: not ours */
        e.stamp = 900500; e.bytes = 300; in.push_back(e);      /* 3: newest */
        std::vector<size_t> go = RecyclePrune(in, 1000000LL, 500000LL, 1000LL);
        if (go.size() != 1 || go[0] != 0) Fail("the keep window did not remove exactly the old file");
        go = RecyclePrune(in, 1000000LL, 500000LL, 400LL);
        if (go.size() != 2 || go[0] != 0 || go[1] != 1) Fail("the byte cap did not remove the oldest first and stop when the rest fit");
        go = RecyclePrune(in, 1000000LL, 5000000LL, 100000LL);
        if (!go.empty()) Fail("a file inside both keeps was removed");
    }
}
/* CopyDecayPin - a copy's body is held at (decay - margin) in-game hours; everything else is left alone. */
void t_t653_copy_decay_pin()
{
    using corpsedecay::CopyDecayPin;
    const float D = corpsedecay::kDecayHours, M = corpsedecay::kCopyMarginHours;
    if (D != 12.0f) Fail("the decay limit is the engine's 12.0 in-game hours");
    float ns = -1.0f;
    if (CopyDecayPin(0, 1, 80.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("own body: never written");
    if (CopyDecayPin(1, 0, 80.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("alive: never written");
    if (CopyDecayPin(1, -1, 80.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("unreadable dead state: never written");
    if (CopyDecayPin(1, 1, 0.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("rot not started (start 0): never written");
    if (CopyDecayPin(1, 1, 90.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("10 h old, under 11.75: never written");
    if (CopyDecayPin(1, 1, 88.25f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("exactly at the held age: never written");
    if (CopyDecayPin(1, 1, 120.0f, 100.0, D, M, &ns) != 0 || ns != -1.0f) Fail("clock gone backwards: never written");
    if (CopyDecayPin(1, 1, 80.0f, 100.0, D, D, &ns) != 0 || ns != -1.0f) Fail("margin == decay: no held age, never written");
    if (CopyDecayPin(1, 1, 80.0f, 100.0, D, 13.0f, &ns) != 0 || ns != -1.0f) Fail("margin > decay: never written");
    if (CopyDecayPin(1, 1, 80.0f, 100.0, D, M, &ns) != 1 || ns != 88.25f) Fail("20 h old: held at exactly 11.75 h (88.25)");
    ns = -1.0f;
    if (CopyDecayPin(1, 1, 88.0f, 100.0, D, M, &ns) != 1 || ns != 88.25f) Fail("12 h old: held at exactly 11.75 h");
    ns = -1.0f;
    if (CopyDecayPin(1, 1, 10.0f, 10000.0, D, M, &ns) != 1 || ns != 9988.25f) Fail("a big forward jump: held at exactly 11.75 h");
    if (CopyDecayPin(1, 1, 80.0f, 100.0, D, M, 0) != 1) Fail("no out pointer: still answers 1");
}

/* ---- src/common/uniqslot.h ---- */
void t_uniq_first_sight_alive()
{
    using namespace swuniq;
    if (FirstSightAlive(0, 0, kFsOwnMine, 1) != kFirstSightDefer) Fail("before the opening data is applied an alive first sight must wait");
    if (FirstSightAlive(0, 1, kFsOwnOther, -1) != kFirstSightDefer) Fail("before the opening data nothing else is asked: defer");
    if (FirstSightAlive(1, 0, kFsOwnMine, 1) != kFirstSightPublish) Fail("own, living, unknown to the world server: publish");
    if (FirstSightAlive(1, 1, kFsOwnMine, 1) != kFirstSightLearn) Fail("the world server already holds a state for it: learn silently");
    if (FirstSightAlive(1, 0, kFsOwnOther, 1) != kFirstSightLearn) Fail("a copy of another game's character is not ours to publish");
    if (FirstSightAlive(1, 0, kFsOwnMine, 0) != kFirstSightLearn) Fail("a dead carrier is not published as alive");
    if (FirstSightAlive(1, 0, kFsOwnMine, -1) != kFirstSightDefer) Fail("an unreadable carrier is asked again");
    if (FirstSightAlive(1, 0, kFsOwnUnknown, 1) != kFirstSightDefer) Fail("a carrier not registered yet is asked again (never learned silently)");
    if (FirstSightAlive(1, 0, kFsOwnNone, -1) != kFirstSightDefer) Fail("no carrier here: asked again when one is loaded");
    if (FirstSightAlive(1, 1, kFsOwnUnknown, 1) != kFirstSightDefer) Fail("owner unknown: wait even when the world server holds a state");
}
void t_uniq_elsewhere_set()
{
    using namespace swuniq;
    if (ElsewhereMember(1, 1, 1, 0) != 1) Fail("alive, received from another game, nobody here carries it: in the set");
    if (ElsewhereMember(1, 2, 1, 0) != 1) Fail("imprisoned, received from another game, nobody here carries it: in the set");
    if (ElsewhereMember(1, 1, 0, 0) != 0) Fail("a local ALIVE row with no carrier (a respawn's clear, a first sight) is NOT in the set");
    if (ElsewhereMember(1, 2, 0, 0) != 0) Fail("a local IMPRISONED row is not in the set");
    if (ElsewhereMember(0, 1, 1, 0) != 0 || ElsewhereMember(0, 2, 1, 0) != 0) Fail("before the opening data is applied the set is empty");
    if (ElsewhereMember(1, 0, 1, 0) != 0) Fail("a dead row is never in the elsewhere set");
    if (ElsewhereMember(1, 3, 1, 0) != 0 || ElsewhereMember(1, -1, 1, 0) != 0) Fail("a state outside 1..2 is never in the set");
    if (ElsewhereMember(1, 1, 1, 1) != 0 || ElsewhereMember(1, 2, 1, 1) != 0) Fail("a character here carries it: not in the set");
    /* the row's latest write decides: a remote ALIVE, then this game's own change to the same row (remote 0) */
    int remote = 1;
    if (ElsewhereMember(1, 1, remote, 0) != 1) Fail("the remote row is in the set");
    remote = 0;
    if (ElsewhereMember(1, 1, remote, 0) != 0) Fail("a local change after a remote one takes the template out of the set");
}
void t_uniq_dead_set()
{
    using namespace swuniq;
    if (DeadSetMember(0, 1, 0, 0) != 1) Fail("a dead row, not brought back, no living own carrier: in the dead set");
    if (DeadSetMember(1, 1, 0, 0) != 0 || DeadSetMember(2, 1, 0, 0) != 0) Fail("an alive or imprisoned row is never in the dead set");
    if (DeadSetMember(0, 1, 1, 0) != 0) Fail("a character brought back from the dead is not in the dead set");
    if (DeadSetMember(0, 1, 0, 1) != 0) Fail("a living character this game owns carries it: not in the dead set");
    if (DeadSetMember(0, 1, 1, 1) != 0) Fail("brought back and carried alive here: not in the dead set");
    if (DeadSetMember(0, 0, 0, 0) != 0) Fail("a DEAD read from this game's own table that no game confirmed is never in the dead set (a living character put away must not be skipped)");
}
/* ---- two living bodies of one named character (uniqslot.h DupDecide) ---- */
static swuniq::DupBody DupB(unsigned int uid, int mine, int owner, int runner, int prot, int table, int engineSquad)
{
    swuniq::DupBody b; b.uid = uid; b.mine = mine; b.owner = owner; b.runner = runner; b.prot = prot; b.tableHolds = table; b.engineSquad = engineSquad;
    return b;
}
void t_uniq_dup_decide()
{
    using namespace swuniq;
    if (kProtectedIsKept != 1) Fail("the protected-body rule is not 'a protected body is the kept one' (owner decision 597, recommended b)");
    DupBody b[4]; int rm[4]; int keep = -9, why = -9;
    /* seen from B (slot 1): A's body (slot 0, area run by 0) and B's own (area run by 0) - A's is kept, B's own waits: a game
       removes only a body in an area it runs */
    b[0] = DupB(100, 0, 0, 0, 0, 0, 1); b[1] = DupB(200, 1, 1, 0, 0, 1, 1);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupWait || keep != 0 || why != kKeepRunner || rm[0] != 0 || rm[1] != 0) Fail("B: its own body in an area A runs removed here");
    /* the same with B running its own body's area: B removes it */
    b[1] = DupB(200, 1, 1, 1, 0, 1, 1);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupCrossGame || rm[1] != 0) Fail("both held, in areas run by different games: not cross-game");
    b[0] = DupB(100, 0, 0, 1, 0, 0, 1); b[1] = DupB(200, 1, 1, 1, 0, 0, 1);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupOthersOwn || keep != 1 || rm[0] != 0 || rm[1] != 0) Fail("B runs the area: A's copy there removed, or B's runner-held body not kept");
    /* a protected COPY (another game's body, protected only here) is never kept in place of this game's own: wait */
    b[0] = DupB(100, 0, 0, 1, 1, 0, 1); b[1] = DupB(200, 1, 1, 1, 0, 1, 1);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupWait || rm[0] != 0 || rm[1] != 0) Fail("a protected copy was kept and this game's own body removed");
    b[0] = DupB(100, 1, 1, 1, 0, 0, 1); b[1] = DupB(200, 0, 0, 1, 0, 0, 1);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupOthersOwn || keep != 0 || rm[0] != 0 || rm[1] != 0) Fail("B runs the area: its own body not kept, or A's body there removed by B");
    /* a body this game owns in an area another game runs is never removed here, even with another body runner-held */
    b[0] = DupB(700, 0, 1, 1, 0, 0, 1); b[1] = DupB(701, 1, 0, 1, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[0] != 0 || rm[1] != 0) Fail("own body in an area another game runs: removed here");
    b[0] = DupB(700, 0, 1, 1, 1, 0, 0); b[1] = DupB(701, 1, 0, 1, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[1] != 0) Fail("another player carries the copy, own body in that player's area: removed here (H1)");
    /* the same bodies seen from A (slot 0): its own runner-held body stays, B's copy is never removed here */
    b[0] = DupB(100, 1, 0, 0, 0, 0, 1); b[1] = DupB(200, 0, 1, 0, 0, 0, 0);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupOthersOwn || keep != 0 || rm[0] != 0 || rm[1] != 0) Fail("A: another game's copy removed here");
    /* leg 2: two own bodies, both in areas this game runs - the table's body stays, else the engine's squad, else the lower uid */
    b[0] = DupB(300, 1, 0, 0, 0, 0, 1); b[1] = DupB(301, 1, 0, 0, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupRemove || keep != 1 || why != kKeepTable || rm[0] != 1 || rm[1] != 0) Fail("two own bodies: the one the table names not kept");
    b[0] = DupB(300, 1, 0, 0, 0, 0, 0); b[1] = DupB(301, 1, 0, 0, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupRemove || keep != 1 || why != kKeepEngineSquad || rm[0] != 1) Fail("two own bodies: the engine's squad not kept over a stand-in squad");
    b[0] = DupB(301, 1, 0, 0, 0, 0, 1); b[1] = DupB(300, 1, 0, 0, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupRemove || keep != 1 || why != kKeepLowerUid || rm[0] != 1 || rm[1] != 0) Fail("two own bodies alike: the lower uid not kept");
    b[0] = DupB(300, 1, 0, 0, 0, 0, 1); b[1] = DupB(301, 1, 0, 0, 0, 0, 1); b[2] = DupB(302, 1, 0, 0, 0, 1, 0);
    if (DupDecide(b, 3, 0, 1, &keep, &why, rm) != kDupRemove || keep != 2 || rm[0] != 1 || rm[1] != 1 || rm[2] != 0) Fail("three own bodies: not every other own body marked");
    /* the same two seen from B: both A's, both runner-held - A decides, B removes nothing */
    b[0] = DupB(300, 0, 0, 0, 0, 0, 0); b[1] = DupB(301, 0, 0, 0, 0, 0, 0);
    if (DupDecide(b, 2, 1, 1, &keep, &why, rm) != kDupOthersOwn || rm[0] != 0 || rm[1] != 0) Fail("two of another game's bodies: removed here");
    /* (c) bodies held in areas run by different games: nothing (the two-game case is not built) */
    b[0] = DupB(400, 1, 0, 0, 0, 1, 1); b[1] = DupB(401, 0, 1, 1, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupCrossGame || rm[0] != 0 || rm[1] != 0) Fail("cross-game: something removed");
    /* (d) none held by its area's runner, an unknown runner, an unknown owner, no slot of this game's own: wait */
    b[0] = DupB(500, 1, 0, 1, 0, 0, 1); b[1] = DupB(501, 0, 1, 2, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[0] != 0) Fail("none runner-held: not waiting");
    b[0] = DupB(500, 1, 0, 0, 0, 0, 1); b[1] = DupB(501, 1, 0, -1, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[0] != 0 || rm[1] != 0) Fail("no fresh area map: not waiting");
    b[0] = DupB(500, 1, 0, 0, 0, 0, 1); b[1] = DupB(501, 0, -1, 0, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[0] != 0) Fail("an unknown owner: not waiting");
    b[0] = DupB(500, 1, 0, 0, 0, 0, 1); b[1] = DupB(501, 1, 0, 1, 0, 0, 1);
    if (DupDecide(b, 2, -1, 1, &keep, &why, rm) != kDupWait) Fail("this game's slot unknown: not waiting");
    if (DupDecide(b, 1, 0, 1, &keep, &why, rm) != kDupNone || keep != -1) Fail("one body: not 'none'");
    /* protected bodies, kProtectedIsKept = 1: a protected body is the kept one; two protected - nothing */
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 1, 0, 0, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupRemove || keep != 0 || why != kKeepProtected || rm[0] != 0 || rm[1] != 1) Fail("protected kept: the other own unprotected body not removed");
    b[0] = DupB(600, 0, 1, 0, 1, 0, 0); b[1] = DupB(601, 1, 0, 0, 0, 1, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[1] != 0 || rm[0] != 0) Fail("another player's body, protected only here, was kept and this game's own runner-held body removed (it must wait)");
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 0, 1, 0, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupOthersOwn || rm[0] != 0 || rm[1] != 0) Fail("own protected body and another game's copy: something removed here");
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 1, 0, 0, 1, 1, 1); b[2] = DupB(602, 1, 0, 0, 0, 0, 1);
    if (DupDecide(b, 3, 0, 1, &keep, &why, rm) != kDupProtectedKept || rm[0] != 0 || rm[1] != 0 || rm[2] != 0) Fail("two protected bodies: something removed");
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 1, 0, 1, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[1] != 0) Fail("protected kept: the other own body in an area another game runs removed here");
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 1, 0, -1, 0, 0, 1);
    if (DupDecide(b, 2, 0, 1, &keep, &why, rm) != kDupWait || rm[1] != 0) Fail("protected kept, but no fresh area map: not waiting");
    /* the alternative rule (0): any protected body removes nothing */
    b[0] = DupB(600, 1, 0, 0, 1, 0, 1); b[1] = DupB(601, 1, 0, 0, 0, 1, 1);
    if (DupDecide(b, 2, 0, 0, &keep, &why, rm) != kDupProtectedKept || rm[0] != 0 || rm[1] != 0) Fail("rule 0: a body removed beside a protected one");
    b[0] = DupB(600, 1, 0, 0, 0, 0, 1); b[1] = DupB(601, 1, 0, 0, 0, 1, 1);
    if (DupDecide(b, 2, 0, 0, &keep, &why, rm) != kDupRemove || keep != 1 || rm[0] != 1) Fail("rule 0 with nothing protected: not the runner rule");
    /* never: another game's body, or a protected one, marked - every combination of two bodies under both rules */
    for (int rule = 0; rule < 2; ++rule)
    for (int m0 = 0; m0 < 2; ++m0) for (int m1 = 0; m1 < 2; ++m1) for (int p0 = 0; p0 < 2; ++p0) for (int p1 = 0; p1 < 2; ++p1)
    for (int r0 = -1; r0 < 2; ++r0) for (int r1 = -1; r1 < 2; ++r1) for (int t0 = 0; t0 < 2; ++t0)
    {
        b[0] = DupB(10, m0, m0 ? 0 : 1, r0, p0, t0, 1); b[1] = DupB(11, m1, m1 ? 0 : 1, r1, p1, 1 - t0, 0);
        const int r = DupDecide(b, 2, 0, rule, &keep, &why, rm);
        for (int i = 0; i < 2; ++i) if (rm[i] != 0 && (b[i].mine == 0 || b[i].prot != 0 || b[i].runner != 0)) Fail("a copy, a protected body or one in an area another game runs marked for removal");
        if (rm[0] != 0 && rm[1] != 0) Fail("both bodies marked for removal");
        if ((r == kDupRemove) != (rm[0] != 0 || rm[1] != 0)) Fail("the result and the marks disagree");
        if (r == kDupRemove && (keep < 0 || rm[keep] != 0)) Fail("a removal with no kept body, or the kept one marked");
    }
}
void t_uniq_member_verdict()
{
    using namespace swuniq;
    if (MemberVerdict(1, 1, 0, 0) != kMemberSkip) Fail("a living member of a unique that is dead in this world is skipped");
    if (MemberVerdict(1, 1, 0, 1) != kMemberMake) Fail("a dead body of that unique still loads (its loot)");
    if (MemberVerdict(1, 1, 1, 1) != kMemberSkip) Fail("first time: the member is made fresh from its template - no body, skipped");
    if (MemberVerdict(1, 1, 1, -1) != kMemberSkip) Fail("first time: the record is not read, so its fault does not matter");
    if (MemberVerdict(0, 1, 1, 0) != kMemberMake) Fail("first time, not in the dead set: made");
    if (MemberVerdict(1, 0, 1, 0) != kMemberMake) Fail("first time, a template that is not unique: made");
    if (MemberVerdict(0, 1, 0, 0) != kMemberMake) Fail("not in the dead set: made");
    if (MemberVerdict(1, 0, 0, 0) != kMemberMake) Fail("a template that is not unique is never skipped");
    if (MemberVerdict(-1, 1, 0, 0) != kMemberMake) Fail("the dead set could not be asked: made");
    if (MemberVerdict(1, -1, 0, 0) != kMemberMake) Fail("the unique flag faulted: made");
    if (MemberVerdict(1, 1, 0, -1) != kMemberMake) Fail("the member's own record faulted: made");
}

/* ===== in-game text chat (src/common/chatwire.h) ===== */
void t_t625_chat_wire()
{
    using namespace chatwire;
    std::vector<char> b;
    int kind = -1, target = -2;
    std::string text;
    if (!Encode(kPrivate, 5, "on my way", &b) || b.size() != 6 + 9 || (unsigned char)b[0] != 1 || b[1] != 2 || b[2] != 5 || b[3] != 0
        || (unsigned char)b[4] != 9 || b[5] != 0)
        Fail("chat: the encoded layout is not version 1 | kind | u16 target LE | u16 n LE | text");
    if (!Decode(&b[0], b.size(), &kind, &target, &text) || kind != kPrivate || target != 5 || text != "on my way") Fail("chat: a round trip lost the kind, the target or the text");
    {
        std::vector<char> c;
        if (!Encode(kPrivate, 300, "x", &c) || (unsigned char)c[2] != 44 || c[3] != 1 || !Decode(&c[0], c.size(), &kind, &target, &text) || target != 300)
            Fail("chat: a target past 255 did not round trip");
    }
    {
        std::vector<char> c;
        if (!Encode(kEveryone, 7, "hi", &c) || (unsigned char)c[2] != 0xFF || (unsigned char)c[3] != 0xFF) Fail("chat: an everyone line does not carry target 0xFFFF");
        if (!Decode(&c[0], c.size(), &kind, &target, &text) || kind != kEveryone || target != -1) Fail("chat: an everyone line's target is not -1");
        if (!Encode(kFaction, -1, "hi", &c) || (unsigned char)c[2] != 0xFF || !Decode(&c[0], c.size(), &kind, &target, &text) || kind != kFaction || target != -1)
            Fail("chat: a faction line's target is not 0xFFFF / -1");
        std::vector<char> d = c; d[2] = 3; d[3] = 0;
        if (Decode(&d[0], d.size(), &kind, &target, &text)) Fail("chat: a faction line naming a player was accepted");
    }
    { std::vector<char> c = b; c[2] = (char)0xFF; c[3] = (char)0xFF; if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: a private line naming no player was accepted"); }
    if (Encode(kPrivate, -1, "x", &b) || Encode(kPrivate, 0xFFFF, "x", &b)) Fail("chat: a private line without a player's slot was encoded");
    Encode(kPrivate, 5, "on my way", &b);
    { std::vector<char> c = b; c[0] = 2; if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: a wrong version was accepted"); }
    { std::vector<char> c = b; c[1] = 3; if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: an unknown kind was accepted"); }
    { std::vector<char> c = b; c.push_back('x'); if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: a frame longer than n was accepted"); }
    { std::vector<char> c = b; c.pop_back(); if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: a frame shorter than n was accepted"); }
    { const char z[7] = { 1, 0, (char)0xFF, (char)0xFF, 0, 0, 'a' }; if (Decode(z, 6, &kind, &target, &text) || Decode(z, 7, &kind, &target, &text)) Fail("chat: n = 0 was accepted"); }
    if (Decode(0, 0, &kind, &target, &text)) Fail("chat: no bytes were accepted");
    if (Encode(kEveryone, -1, "", &b)) Fail("chat: an empty text was encoded");
    if (Encode(3, -1, "x", &b)) Fail("chat: an unknown kind was encoded");
    { std::string t = "a"; t += '\n'; if (Encode(kEveryone, -1, t, &b)) Fail("chat: a control character was encoded"); }
    {
        std::vector<char> c;
        Encode(kEveryone, -1, "abc", &c);
        const char bad[3] = { 0, 0x1B, 0x7F };
        for (int k = 0; k < 3; ++k) { std::vector<char> d = c; d[7] = bad[k]; if (Decode(&d[0], d.size(), &kind, &target, &text)) Fail("chat: a NUL or control byte was accepted"); }
    }
    {
        std::string s800;
        for (int i = 0; i < 200; ++i) s800 += "\xF0\x9F\x98\x80";
        if (s800.size() != 800 || !Encode(kEveryone, -1, s800, &b) || !Decode(&b[0], b.size(), &kind, &target, &text) || text != s800) Fail("chat: 800 bytes were refused");
        if (Encode(kEveryone, -1, s800 + "a", &b)) Fail("chat: 801 bytes were encoded");
        std::vector<char> c(6 + 801, 'a');
        c[0] = 1; c[1] = 0; c[2] = (char)0xFF; c[3] = (char)0xFF; c[4] = (char)(801 & 0xFF); c[5] = (char)(801 >> 8);
        if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: 801 bytes were decoded");
    }
    if (CleanTyped("  hi\tthere  ") != "hithere") Fail("chat: CleanTyped kept a control character or the outer spaces");
    if (!CleanTyped("   ").empty()) Fail("chat: only spaces is not empty");
    {
        std::string s;
        for (int i = 0; i < 250; ++i) s += "\xC3\xA9";
        const std::string c = CleanTyped(s);
        if (Utf8Chars(c) != 200 || c.size() != 400) Fail("chat: CleanTyped did not cut to 200 characters");
        std::string t(199, 'a');
        t += "\xE2\x82\xAC";
        t += "b";
        const std::string d = CleanTyped(t);
        if (Utf8Chars(d) != 200 || d.size() != 202 || d.substr(199) != "\xE2\x82\xAC") Fail("chat: CleanTyped split a character or cut in the wrong place");
    }
}
void t_t625_chat_words()
{
    using namespace chatwire;
    if (LineText(kEveryone, kIn, "Bob", "heading to the Hub, need anything?") != "Bob: heading to the Hub, need anything?") Fail("chat: the everyone line");
    if (LineText(kFaction, kIn, "Anna", "bring bread") != "Anna (faction): bring bread") Fail("chat: the faction line");
    if (LineText(kPrivate, kIn, "Bob", "can you hold the gate?") != "From Bob: can you hold the gate?") Fail("chat: the private line received");
    if (LineText(kPrivate, kOut, "Bob", "on my way") != "To Bob: on my way") Fail("chat: the private line sent");
    if (JoinLine("Carl") != "Carl joined the world.") Fail("chat: the join line");
    if (LeaveLine("Carl") != "Carl left the world.") Fail("chat: the leave line");
    if (GoneLine("Bob") != "Bob isn't in the world any more.") Fail("chat: the player-gone line");
    if (NotConnectedLine() != std::string("Not connected - your message wasn't sent.")) Fail("chat: the not-connected line");
    if (std::string(WindowTitle()) != "CHAT" || std::string(ToLabel()) != "TO" || std::string(EveryoneWord()) != "EVERYONE"
        || std::string(FactionWord()) != "MY FACTION" || std::string(SendWord()) != "SEND" || std::string(OpacityWord()) != "OPACITY"
        || std::string(TypeHint()) != "type a message..." || std::string(SearchHint()) != "search players..." || std::string(MessageWord()) != "MESSAGE")
        Fail("chat: a window word differs from the approved mock-up");
    {
        std::string a, n, p;
        LineParts(kPrivate, kOut, "Bob", "on my way", &a, &n, &p);
        if (a != "To " || n != "Bob" || p != ": on my way") Fail("chat: the name is not its own part of the line");
    }
    if (LineColour(kLineTalk, kEveryone) != kWhite || LineColour(kLineTalk, kFaction) != kGreen || LineColour(kLineTalk, kPrivate) != kPink
        || LineColour(kLineJoin, kEveryone) != kGrey || LineColour(kLineLeave, kEveryone) != kGrey || LineColour(kLineGone, kPrivate) != kGrey
        || LineColour(kLineNotConnected, kEveryone) != kGrey)
        Fail("chat: a line's colour is not white / green / pink / grey");
    for (int i = 1; i <= kSampleCount; ++i) if (std::string(Sample(i)).empty()) Fail("chat: a sample sentence is empty");
    if (!std::string(Sample(0)).empty() || !std::string(Sample(kSampleCount + 1)).empty()) Fail("chat: a sample outside 1..4 is not empty");
}
void t_t625_chat_tab()
{
    using namespace chatwire;
    if (TabNext(kToEveryone, -1) != kToFaction || TabNext(kToFaction, -1) != kToEveryone) Fail("chat: Tab with no player messaged");
    if (TabNext(kToFaction, 5) != 5 || TabNext(5, 5) != kToEveryone || TabNext(7, 5) != kToEveryone) Fail("chat: Tab through the last player messaged");
}
void t_t625_chat_search()
{
    using namespace chatwire;
    if (!SearchMatch("Bob", "") || !SearchMatch("Bob", "bo") || !SearchMatch("Bob", "OB") || !SearchMatch("Anna", "nna")) Fail("chat: a search missed a name");
    if (SearchMatch("Bob", "x") || SearchMatch("Bo", "Bob")) Fail("chat: a search matched a name it should not");
}
void t_t625_chat_ring()
{
    using namespace chatwire;
    Ring r;
    for (int i = 0; i < 150; ++i) { Entry e; e.text = "x"; e.ms = i; r.Push(e); }
    if (r.lines.size() != kKeepLines || r.lines.front().seq != 51 || r.lines.back().seq != 150) Fail("chat: the ring does not keep the last 100 lines in order");
    Entry e; e.kind = kFaction; e.name = "Anna"; e.text = "bring bread";
    if (EntryText(e) != "Anna (faction): bring bread") Fail("chat: a kept talk line's text");
    Entry g; g.lineKind = kLineJoin; g.name = "Carl"; g.text = JoinLine("Carl");
    if (EntryText(g) != "Carl joined the world.") Fail("chat: a kept join line's text");
}
void t_t625_chat_fade()
{
    using namespace chatwire;
    if (FadeAlpha(0) != 1.0f || FadeAlpha(-5) != 1.0f || FadeAlpha(10000) != 1.0f) Fail("chat: a line faded before 10 s");
    const float h = FadeAlpha(10500);
    if (h < 0.45f || h > 0.55f) Fail("chat: a line is not half faded half a second after 10 s");
    if (FadeAlpha(11000) != 0.0f || FadeAlpha(60000) != 0.0f) Fail("chat: a line still shows after 11 s");
}
void t_t625_chat_cfg()
{
    using namespace chatwire;
    Cfg c; c.x = 1500; c.y = 900; c.w = 600; c.h = 300; c.opacity = 40;
    Cfg r;
    if (!CfgParse(CfgFormat(c), &r) || r.x != 1500 || r.y != 900 || r.w != 600 || r.h != 300 || r.opacity != 40) Fail("chat.cfg: a round trip changed the place");
    const Cfg big = FitRect(c, 1920, 1080);
    if (big.x != 1320 || big.y != 780 || big.w != 600 || big.h != 300) Fail("chat.cfg: FitRect at 1920x1080");
    const Cfg small = FitRect(c, 1280, 720);
    if (small.x != 680 || small.y != 420 || small.w != 600 || small.h != 300) Fail("chat.cfg: FitRect at 1280x720 does not bring the window on screen");
    Cfg huge; huge.x = -50; huge.y = -50; huge.w = 5000; huge.h = 5000;
    const Cfg f = FitRect(huge, 1280, 720);
    if (f.x != 0 || f.y != 0 || f.w != 1280 || f.h != 720) Fail("chat.cfg: FitRect of a window larger than the screen");
    Cfg tiny; tiny.x = 10; tiny.y = 10; tiny.w = 5; tiny.h = 5;
    const Cfg t = FitRect(tiny, 1280, 720);
    if (t.w != kMinW || t.h != kMinH) Fail("chat.cfg: FitRect let the window be smaller than its least size");
    for (int k = 0; k < 2; ++k)
    {
        const int vw = k == 0 ? 1280 : 1920, vh = k == 0 ? 720 : 1080;
        const Cfg d = DefaultRect(vw, vh);
        if (d.x < 0 || d.y < 0 || d.x + d.w > vw || d.y + d.h > vh || d.w < kMinW || d.h < kMinH || d.opacity != kDefaultOpacity) Fail("chat.cfg: the first place is off screen");
    }
    if (CfgParse("x=1\r\ny=2\r\nw=3\r\n", &r)) Fail("chat.cfg: a file without h was read");
    if (!CfgParse("x=1\ny=2\nw=400\nh=300\nopacity=150\n", &r) || r.opacity != kDefaultOpacity) Fail("chat.cfg: an opacity past 100 was taken");
    if (CfgParse("x=a\ny=2\nw=3\nh=4\n", &r)) Fail("chat.cfg: a non-number was read");
}
/* the chat window's layout at its least size and at sizes it takes on common screens, with short, long and tiny words: no two
   controls overlap, all inside the window, the typing box at least 55% of the width, the slider at least 80, the lines' view tall.
   T-815: the header's OPTIONS button between the slider and the X, and the least width (kMinW) that holds it. */
void t_t625_chat_layout()
{
    static const int kSizes[5][2] = { { chatwire::kMinW, chatwire::kMinH }, { chatwire::kMinW, 297 }, { 480, 230 }, { 576, 324 }, { 640, 420 } };
    static const int kWords[3][5] = { { 40, 70, 75, 80, 40 }, { 60, 95, 95, 300, 60 }, { 30, 60, 50, 20, 20 } };   /* CHAT, OPACITY, OPTIONS, TO caption, SEND */
    for (int s = 0; s < 5; ++s)
        for (int v = 0; v < 3; ++v)
        {
            const int W = kSizes[s][0], H = kSizes[s][1];
            chatwire::WinLayout L;
            chatwire::WindowLayout(W, H, kWords[v][0], kWords[v][1], kWords[v][2], kWords[v][3], kWords[v][4], &L);
            const std::string at = " at " + I(W) + "x" + I(H) + " words " + I(v);
            const chatwire::WinBox c[11] = { L.title, L.opWord, L.opBar, L.opt, L.close, L.view, L.toWord, L.toBtn, L.edit, L.send, L.grip };
            static const char* const kName[11] = { "title", "OPACITY", "slider", "OPTIONS", "close X", "lines", "TO", "TO button", "typing box", "SEND", "grip" };
            for (int i = 0; i < 11; ++i)
            {
                if (c[i].w <= 0 || c[i].h <= 0 || c[i].x < 0 || c[i].y < 0 || c[i].x + c[i].w > W || c[i].y + c[i].h > H)
                    Fail(std::string("chat layout: the ") + kName[i] + " is not inside the window" + at);
                for (int j = i + 1; j < 11; ++j)
                    if (chatwire::WinBoxesOverlap(c[i], c[j])) Fail(std::string("chat layout: the ") + kName[i] + " and the " + kName[j] + " overlap" + at);
            }
            for (int i = 0; i < 5; ++i)
                if (c[i].y + c[i].h > chatwire::kWinHeadH) Fail(std::string("chat layout: the ") + kName[i] + " is not in the header" + at);
            if (!(L.opBar.x + L.opBar.w <= L.opt.x && L.opt.x + L.opt.w <= L.close.x)) Fail("chat layout: the header is not slider, OPTIONS, X" + at);
            if (L.opt.w < kWords[v][2] + 24) Fail("chat layout: OPTIONS is narrower than its word" + at);
            if (L.hint.x < L.edit.x || L.hint.x + L.hint.w > L.edit.x + L.edit.w) Fail("chat layout: the typing hint is not over the typing box" + at);
            if (L.edit.w * 100 < W * 55) Fail("chat layout: the typing box is under 55% of the width" + at);
            if (L.opBar.w < chatwire::kSliderLeast) Fail("chat layout: the slider is under its least width" + at);
            if (L.opBar.w > chatwire::kSliderMost) Fail("chat layout: the slider stretches past its most width" + at);
            if (chatwire::kMinW != 400 || chatwire::kSliderLeast != 40 || chatwire::kSliderMost != 120) Fail("chat layout: owner 879's numbers (least width 400, slider 40..120) changed");
            if (L.view.h <= 0) Fail("chat layout: the lines have no height" + at);
            if (L.toBtn.w < 90 || L.toBtn.w > W / 2 - chatwire::kWinPad) Fail("chat layout: the TO button's width is out of its bounds" + at);
            if (L.send.w < 64 || L.send.w < kWords[v][4] + 32) Fail("chat layout: SEND is narrower than its word" + at);
            if (L.edit.y != L.send.y || L.toBtn.y + L.toBtn.h > L.edit.y || L.view.y + L.view.h > L.toBtn.y) Fail("chat layout: the rows are not lines, TO, then typing" + at);
        }
    if (chatwire::kLineBandAlpha <= 0.0f || chatwire::kLineBandAlpha >= 1.0f) Fail("chat: the line band is not translucent");
}
/* T-815 (owner 865 / 870 / 871): which sound a received message plays, its switches, and chat.cfg's sound keys */
void t_t815_chat_sounds()
{
    using namespace chatwire;
    if (std::string(SoundEventOf(kEveryone)) != "Prospect_Click" || std::string(SoundEventOf(kFaction)) != "Prospect_Click"
        || std::string(SoundEventOf(kPrivate)) != "Prospect_Click" || SoundEventOf(3) != 0 || SoundEventOf(-1) != 0)
        Fail("chat sounds: a kind plays the wrong event");
    Sounds on;
    for (int k = 0; k < kSoundKinds; ++k)
    {
        if (on.on[k] != 1) Fail("chat sounds: a switch is not on at first");
        const char* ev = SoundToPlay(on, k, false);
        if (ev == 0 || std::string(ev) != SoundEventOf(k)) Fail("chat sounds: a switched-on kind with the window closed plays nothing");
        if (SoundToPlay(on, k, true) != 0) Fail("chat sounds: a sound plays while the chat window is open");
        Sounds off = on; off.on[k] = 0;
        if (SoundToPlay(off, k, false) != 0) Fail("chat sounds: a switched-off kind still plays");
        for (int j = 0; j < kSoundKinds; ++j)
            if (j != k && SoundToPlay(off, j, false) == 0) Fail("chat sounds: one kind's switch silences another kind");
    }
    if (SoundToPlay(on, 3, false) != 0 || SoundToPlay(on, -1, false) != 0) Fail("chat sounds: an unknown kind plays");
    if (std::string(SoundKey(kEveryone)) != "sound_everyone" || std::string(SoundKey(kFaction)) != "sound_faction" || std::string(SoundKey(kPrivate)) != "sound_direct")
        Fail("chat sounds: chat.cfg's keys");
    if (std::string(SoundLabel(kEveryone)) != "Sound for EVERYONE messages" || std::string(SoundLabel(kFaction)) != "Sound for MY FACTION messages"
        || std::string(SoundLabel(kPrivate)) != "Sound for private messages" || std::string(OptionsWord()) != "OPTIONS"
        || std::string(OptionsTitle()) != "CHAT OPTIONS" || std::string(CloseWord()) != "CLOSE")
        Fail("chat options: a word is not the approved one");
    /* the round trip: place and switches in one file, each read back */
    Cfg c; c.x = 100; c.y = 200; c.w = 500; c.h = 300; c.opacity = 35;
    Sounds mix; mix.on[kEveryone] = 0; mix.on[kFaction] = 1; mix.on[kPrivate] = 0;
    const std::string body = CfgFormat(c, mix);
    Cfg rc; Sounds rs;
    if (!CfgParse(body, &rc) || rc.x != 100 || rc.y != 200 || rc.w != 500 || rc.h != 300 || rc.opacity != 35) Fail("chat.cfg: the sound keys spoil the place");
    SoundsParse(body, &rs);
    if (rs.on[kEveryone] != 0 || rs.on[kFaction] != 1 || rs.on[kPrivate] != 0) Fail("chat.cfg: a round trip changed the sound switches");
    if (body.find("sound_everyone=0\r\n") == std::string::npos || body.find("sound_faction=1\r\n") == std::string::npos || body.find("sound_direct=0\r\n") == std::string::npos)
        Fail("chat.cfg: the sound keys are not written as key=0/1 lines");
    /* a missing or damaged window key throws the place away, never the switches */
    Sounds a;
    SoundsParse("x=1\r\ny=2\r\nw=3\r\nsound_everyone=0\r\nsound_direct=0\r\n", &a);
    if (CfgParse("x=1\r\ny=2\r\nw=3\r\nsound_everyone=0\r\nsound_direct=0\r\n", &rc)) Fail("chat.cfg: a file without h was read");
    if (a.on[kEveryone] != 0 || a.on[kFaction] != 1 || a.on[kPrivate] != 0) Fail("chat.cfg: a missing window key lost the sound switches");
    Sounds d;
    SoundsParse("x=zz\ny=\nsound_faction=0\n\x01garbage=\nsound_private=0\n", &d);
    if (d.on[kEveryone] != 1 || d.on[kFaction] != 0 || d.on[kPrivate] != 1) Fail("chat.cfg: a damaged window key lost a switch, or a wrong key was read");
    /* a damaged switch value is on (the default), an empty or unreadable file all on */
    Sounds e;
    SoundsParse("sound_everyone=2\nsound_faction=no\nsound_direct=\n", &e);
    if (e.on[0] != 1 || e.on[1] != 1 || e.on[2] != 1) Fail("chat.cfg: a damaged switch value was not taken as on");
    Sounds f; f.on[0] = 0; f.on[1] = 0; f.on[2] = 0;
    SoundsParse("", &f);
    if (f.on[0] != 1 || f.on[1] != 1 || f.on[2] != 1) Fail("chat.cfg: no file is not all on");
    Sounds g;
    SoundsParse("sound_faction=0", &g);   /* the last line without its line end */
    if (g.on[kFaction] != 0) Fail("chat.cfg: the last switch without a line end was not read");
    /* the old writer's file (place only) leaves every switch on */
    Sounds h;
    SoundsParse(CfgFormat(c), &h);
    if (h.on[0] != 1 || h.on[1] != 1 || h.on[2] != 1) Fail("chat.cfg: a file from before the switches is not all on");
}
/* review S2/S3 (2026-10-17): the plugin uses the tested rule - RowCallOnly and the hook guard both decide through
   AddrUsesAllCallOnly, and coop::AddHook asks the guard before MinHook touches the target. */
void t_rk1_hook_guard_pins()
{
    const std::string a = T313ReadFile("../coop-plugin/addresses.cpp");
    const std::string h = T313ReadFile("../coop-plugin/hooks.cpp");
    if (a.empty() || h.empty()) { Fail("could not read addresses.cpp / hooks.cpp"); return; }
    const size_t row = a.find("int RowCallOnly(const char* name)");
    const size_t guard = a.find("const char* AddrHookRefusedAt(unsigned long long target)");
    if (row == std::string::npos || a.find("AddrUsesAllCallOnly(uses, n)", row) == std::string::npos || a.find("AddrUsesAllCallOnly(uses, n)", row) - row > 600)
        Fail("addresses.cpp: RowCallOnly does not decide through AddrUsesAllCallOnly (S2)");
    if (guard == std::string::npos || a.find("AddrUsesAllCallOnly(uses, n)", guard) == std::string::npos || a.find("AddrUsesAllCallOnly(uses, n)", guard) - guard > 700)
        Fail("addresses.cpp: AddrHookRefusedAt does not decide through AddrUsesAllCallOnly (S3)");
    const size_t ask = h.find("AddrHookRefusedAt((unsigned long long)(uintptr_t)target)");
    const size_t create = h.find("MH_CreateHookEx(ident, target, detour, original)");
    if (ask == std::string::npos || create == std::string::npos || ask > create) Fail("hooks.cpp: AddHook does not ask AddrHookRefusedAt before MH_CreateHookEx (S3)");
    if (h.find("return FAIL;", ask) == std::string::npos || h.find("return FAIL;", ask) > create) Fail("hooks.cpp: a call-only target is not refused before MH_CreateHookEx (S3)");
}
/* T-815: the plugin's sound road - the row it binds is in both address tables, and the sound follows the kept line */
void t_t815_chat_sound_pins()
{
    const std::string c = T313ReadFile("../coop-plugin/chat.cpp");
    if (c.empty()) { Fail("could not read chat.cpp"); return; }
    if (c.find("AddrReg kSoundPostEventRva_reg(\"SoundPostEvent\", &kSoundPostEventRva, kAddrUseCallOnly);") == std::string::npos) Fail("chat.cpp: the SoundPostEvent row is not bound call-only (rk1)");
    if (c.find("AddrReg kSoundSetSwitchRva_reg(\"SoundSetSwitch\", &kSoundSetSwitchRva, kAddrUseCallOnly);") == std::string::npos) Fail("chat.cpp: the SoundSetSwitch row is not bound call-only (rk1)");
    const size_t kept = c.find("DebugLog(std::string(\"[CHAT] in \") + KindLog(kind)");
    const size_t play = c.find("PlayMessageSound(kind, now);");
    if (kept == std::string::npos || play == std::string::npos || play < kept || play - kept > 400) Fail("chat.cpp: the sound is not played after the received line is kept");
    if (c.find("SoundToPlay(g_sounds, kind, g_st == kStOpen)") == std::string::npos) Fail("chat.cpp: the sound does not ask the switches and the window");
    static const char* const kTables[2] = { "../coop-plugin/addresses/65D604D7-0232C000-B914BEE3-02303600.txt", "../coop-plugin/addresses/6602D59D-0232D000-BBA641C8-02304800.txt" };
    static const char* const kRows[2] = { "code 00A860C0 00A860C0 ", "code 00A86F90 00A86F90 " };   /* 1.0.65, 1.0.68 */
    for (int k = 0; k < 2; ++k)
    {
        const std::string t = T313ReadFile(kTables[k]);
        if (t.empty()) { Fail(std::string("could not read ") + kTables[k]); continue; }
        const size_t at = t.find("\nSoundPostEvent ");
        const size_t row = at == std::string::npos ? std::string::npos : t.find(kRows[k], at);
        if (row == std::string::npos || row - at > 60) Fail(std::string("the SoundPostEvent row is not in ") + kTables[k]);
    }
}
void t_t625_chat_plan()
{
    using namespace chatwire;
    if (kRouteWorldNo != (int)cooplive::kRouteWorld || kRouteSlotNo != (int)cooplive::kRouteSlot) Fail("chat: the route numbers are not liveenvelope.h's");
    std::vector<Route> r;
    std::vector<int> none, mates;
    mates.push_back(3); mates.push_back(4);
    if (SendPlan(kEveryone, kToEveryone, 0, none, false, true, true, &r) != kPlanSend || r.size() != 1 || r[0].route != kRouteWorldNo || r[0].target != 0) Fail("chat: everyone");
    if (SendPlan(kFaction, kToFaction, 2, mates, false, true, true, &r) != kPlanSend || r.size() != 2 || r[0].route != kRouteSlotNo || r[0].target != 3 || r[1].target != 4) Fail("chat: faction with members");
    if (SendPlan(kFaction, kToFaction, 0, none, false, true, true, &r) != kPlanSelfOnly || !r.empty()) Fail("chat: a faction of one is not self only");
    if (SendPlan(kFaction, kToFaction, 0, none, false, false, false, &r) != kPlanNotConnected || SendPlan(kFaction, kToFaction, 0, none, false, true, false, &r) != kPlanNotConnected)
        Fail("chat: a faction send with the link down (its faction table forgotten, so no mates counted) closed as if sent");
    if (SendPlan(kFaction, kToFaction, 0, none, false, false, true, &r) != kPlanNotConnected) Fail("chat: a faction send with the faction table not known yet closed as self only");
    if (SendPlan(kFaction, kToFaction, 2, none, false, true, true, &r) != kPlanSelfOnly || !r.empty()) Fail("chat: a faction with no other member in the world is not self only");
    if (SendPlan(kFaction, kToFaction, 2, none, false, true, false, &r) != kPlanNotConnected || !r.empty()) Fail("chat: a faction with members and the link down closed as if sent");
    if (SendPlan(kFaction, kToFaction, 2, none, false, false, true, &r) != kPlanNotConnected || !r.empty()) Fail("chat: a faction send with the player list unknown is not refused as not connected");
    if (SendPlan(kPrivate, 5, 0, none, false, true, true, &r) != kPlanGone || !r.empty()) Fail("chat: a player not in the world is not gone");
    if (SendPlan(kPrivate, 5, 0, none, true, true, true, &r) != kPlanSend || r.size() != 1 || r[0].route != kRouteSlotNo || r[0].target != 5) Fail("chat: private");
    if (SendPlan(kPrivate, 5, 0, none, false, true, false, &r) != kPlanNotConnected || !r.empty()) Fail("chat: a private send with the link down says gone, not not-connected");
    if (SendPlan(kPrivate, 5, 0, none, false, false, true, &r) != kPlanNotConnected || !r.empty()) Fail("chat: a private send with the player list unknown (just after the link came back) says gone");
    if (SendPlan(kEveryone, kToEveryone, 0, none, false, true, false, &r) != kPlanNotConnected || SendPlan(kPrivate, 5, 0, none, true, true, false, &r) != kPlanNotConnected
        || SendPlan(kFaction, kToFaction, 2, mates, false, true, false, &r) != kPlanNotConnected)
        Fail("chat: a send with the link down is not refused as not connected");
}
void t_t625_chat_utf8()
{
    using namespace chatwire;
    static const char* const kGood[] = { "a", "\xC2\x80", "\xC3\xA9", "\xE2\x82\xAC", "\xED\x9F\xBF", "\xEE\x80\x80", "\xF0\x9F\x98\x80", "\xF4\x8F\xBF\xBF" };
    static const char* const kBadUtf8[] = { "\x80", "a\x80", "\xBF", "\xFF", "\xFE", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xE0\x9F\xBF",
        "\xF0\x80\x80\xAF", "\xF0\x8F\xBF\xBF", "\xED\xA0\x80", "\xED\xBF\xBF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xC3", "\xC3" "a",
        "\xE2\x82", "\xE2\x82\xAC\x80", "\xF0\x9F\x98" };
    for (size_t i = 0; i < sizeof(kGood) / sizeof(kGood[0]); ++i)
    {
        const std::string s = kGood[i];
        std::vector<char> b;
        int kind = -1, target = 0;
        std::string text;
        if (!Utf8Valid(s.data(), s.size()) || !Encode(kEveryone, -1, s, &b) || !Decode(&b[0], b.size(), &kind, &target, &text) || text != s)
            Fail("chat: a well-formed UTF-8 character was refused");
        if (CleanTyped(s) != s) Fail("chat: CleanTyped changed a well-formed character");
    }
    for (size_t i = 0; i < sizeof(kBadUtf8) / sizeof(kBadUtf8[0]); ++i)
    {
        const std::string s = std::string("x") + kBadUtf8[i];
        if (Utf8Valid(s.data(), s.size())) Fail("chat: malformed UTF-8 was called well formed");
        std::vector<char> b;
        int kind = -1, target = 0;
        std::string text;
        if (Encode(kEveryone, -1, s, &b)) Fail("chat: malformed UTF-8 was encoded");
        std::vector<char> c;
        c.push_back(1); c.push_back(0); c.push_back((char)0xFF); c.push_back((char)0xFF);
        c.push_back((char)(s.size() & 0xFF)); c.push_back((char)(s.size() >> 8));
        c.insert(c.end(), s.begin(), s.end());
        if (Decode(&c[0], c.size(), &kind, &target, &text)) Fail("chat: malformed UTF-8 was decoded");
        const std::string t = CleanTyped(s);
        if (!Utf8Valid(t.data(), t.size())) Fail("chat: CleanTyped left malformed UTF-8");
    }
    if (CleanTyped("a\xC3" "b\xED\xA0\x80" "c\xF0\x9F\x98\x80") != "abc\xF0\x9F\x98\x80") Fail("chat: CleanTyped did not drop exactly the malformed bytes");
    {
        std::string s(199, 'a');
        s += "\x80\xF0\x9F\x98\x80" "b";
        const std::string t = CleanTyped(s);
        if (t.size() != 203 || !Utf8Valid(t.data(), t.size()) || Utf8Chars(t) != 200) Fail("chat: CleanTyped's cut after a dropped byte");
    }
}
void t_t625_chat_accept()
{
    using namespace chatwire;
    if (AcceptIn(kEveryone, -1, 2, false) != kInTake) Fail("chat: an everyone line was dropped");
    if (AcceptIn(kFaction, -1, 2, true) != kInTake || AcceptIn(kFaction, -1, 2, false) != kInNotMate) Fail("chat: a faction line's sender check");
    if (AcceptIn(kPrivate, 2, 2, false) != kInTake) Fail("chat: a private line for this player was dropped");
    if (AcceptIn(kPrivate, 3, 2, false) != kInNotMine || AcceptIn(kPrivate, 3, 2, true) != kInNotMine || AcceptIn(kPrivate, 2, -1, false) != kInNotMine
        || AcceptIn(kPrivate, -1, -1, false) != kInNotMine)
        Fail("chat: a private line for another slot was kept");
    InRate q;
    int kept = 0;
    for (int i = 0; i < 20; ++i) if (RateTake(&q, 1000 + i * 10)) ++kept;
    if (kept != kInPerSecond) Fail("chat: not exactly 5 lines in a second were kept from one sender");
    if (!RateTake(&q, 2000)) Fail("chat: a sender was still held back a second later");
}
void t_t625_chat_pins()
{
    const std::string lr = T313ReadFile("../common/liverelay.h");
    if (lr.empty()) Fail("could not read liverelay.h");
    else if (lr.find("const unsigned int kInnerChat = 205;") == std::string::npos || lr.find("|| innerType == kInnerChat;") == std::string::npos
             || cooplive::kInnerChat != 205u || !cooplive::LiveInnerRoadLocal(cooplive::kInnerChat))
        Fail("liverelay.h: kInnerChat is not 205 on the local road");
    const std::string ui = T313ReadFile("../coop-plugin/ui.cpp");
    if (ui.find("if (panel != 0 && ChatTakesEscape() != 0) return 0;") == std::string::npos) Fail("ui.cpp: the pause menu's show does not ask the chat first");
    const std::string c = T313ReadFile("../coop-plugin/chat.cpp");
    if (c.empty()) { Fail("could not read chat.cpp"); return; }
    /* no log call carries a typed or received word: the text, the search line, the edit box's caption or a sample - the
       whole call, over every line it takes */
    static const char* const kBad[] = { "+ text", "text +", "e.text", ".text +", "+ g_search", "g_search +", "Sample(", "getCaption", "letters", "EntryText", "LineText" };
    static const char* const kCalls[2] = { "DebugLog(", "ErrorLog(" };
    int logs = 0;
    for (int w = 0; w < 2; ++w)
    {
        const std::string call = kCalls[w];
        size_t at = 0;
        while ((at = c.find(call, at)) != std::string::npos)
        {
            size_t i = at + call.size();
            int depth = 1;
            while (i < c.size() && depth > 0)
            {
                const char ch = c[i];
                if (ch == '"' || ch == '\'')
                {
                    ++i;
                    while (i < c.size() && c[i] != ch) { if (c[i] == '\\') ++i; ++i; }
                }
                else if (ch == '(') ++depth;
                else if (ch == ')') --depth;
                ++i;
            }
            const std::string whole = c.substr(at, i - at);
            ++logs;
            for (size_t k = 0; k < sizeof(kBad) / sizeof(kBad[0]); ++k)
                if (whole.find(kBad[k]) != std::string::npos) Fail(std::string("chat.cpp: a log call may carry chat words (") + kBad[k] + ")");
            at += call.size();
        }
    }
    if (logs < 10) Fail("chat.cpp: fewer log lines than expected - the pin read the wrong file");
}

/* A FINAL LEAVE'S NPCs ARE TAKEN OVER BY THE AREA'S RECEIVER ORDER (src/common/peergone.h) */
void t_t606_gone_take_over()
{
    using namespace cooppg;
    if (GoneTakeOverDecide(1, 1, 2, 3, 3, 0) != kGtDrop) Fail("a leaver's player character is taken over");
    if (GoneTakeOverDecide(-1, 1, 2, 3, 3, 0) != kGtDrop) Fail("a copy of unreadable kind is taken over");
    if (GoneTakeOverDecide(0, 0, 2, 3, 3, 0) != kGtDrop) Fail("a row with no copy here is kept");
    if (GoneTakeOverDecide(0, 1, 2, 3, 3, 0) != kGtTake) Fail("the first of the receiver order does not take the NPC");
    if (GoneTakeOverDecide(0, 1, 2, 1, 3, 0) != kGtWait) Fail("a later receiver does not keep its copy for the taker");
    if (GoneTakeOverDecide(0, 1, -1, -1, 3, 0) != kGtHold) Fail("a stale area map does not hold");
    if (GoneTakeOverDecide(0, 1, 2, 3, -1, 0) != kGtHold) Fail("a game with no slot yet does not hold");
    if (GoneTakeOverDecide(0, 1, 0, -1, 3, 0) != kGtDrop) Fail("an NPC no game has the area of is kept");
    if (GoneTakeOverDecide(0, 1, 2, 1, 3, kGoneTakeLooksMax) != kGtNext) Fail("a first receiver that does not take is not passed over at the bound");
    if (GoneTakeOverDecide(0, 1, -1, -1, 3, kGoneTakeLooksMax) != kGtDrop) Fail("the hold is not bounded");
    if (GoneTakeOverDecide(0, 1, 2, 3, 3, kGoneTakeLooksMax) != kGtTake) Fail("the first of the order stops taking at the bound");
    {   /* the order asked in the leaver's place (slot 2), each game on its own rows: game 3's map still lists the leaver, game 1's has
           dropped it and lists the games in another order; each game counts itself in world through its own roster, like every other */
        cooplo::AreaRow on3[3], on1[2];
        on3[0].slot = 1; on3[0].inWorld = 1; on3[0].loaded = 1;
        on3[1].slot = 2; on3[1].inWorld = 0; on3[1].loaded = 1;
        on3[2].slot = 3; on3[2].inWorld = 1; on3[2].loaded = 1;
        on1[0].slot = 3; on1[0].inWorld = 1; on1[0].loaded = 1;
        on1[1].slot = 1; on1[1].inWorld = 1; on1[1].loaded = 1;
        int o3[4] = { -1, -1, -1, -1 }, o1[4] = { -1, -1, -1, -1 }, niw = 0;
        const int k3 = cooplo::AreaReceiverOrder(on3, 3, 1, 3, 2, 0, 0, o3, 4, &niw);
        const int k1 = cooplo::AreaReceiverOrder(on1, 2, 1, 3, 2, 0, 0, o1, 4, &niw);
        if (k3 != 2 || o3[0] != 3 || o3[1] != 1) Fail("the leaver's area order is not the holder first, then the lower slot, without the leaver");
        if (k1 != k3 || o1[0] != o3[0] || o1[1] != o3[1]) Fail("two games' own rows give two orders");
        if (GoneTakeOverDecide(0, 1, k3, o3[0], 3, 0) != kGtTake || GoneTakeOverDecide(0, 1, k1, o1[0], 1, 0) != kGtWait) Fail("the two games do not agree on one taker");
        {   /* a game its own roster does not show in world is no receiver */
            cooplo::AreaRow off[2]; off[0] = on1[0]; off[1] = on1[1]; off[0].inWorld = 0;
            int oo[4] = { -1, -1, -1, -1 };
            if (cooplo::AreaReceiverOrder(off, 2, 1, 3, 2, 0, 0, oo, 4, &niw) != 1 || oo[0] != 1) Fail("a game out of the world by its roster is still a receiver");
        }
        /* the first receiver does not take within the wait: it is passed over once, the order is asked again, the next in line takes it */
        std::vector<int> tried;
        if (GoneTakeOverDecide(0, 1, k1, o1[0], 1, kGoneTakeLooksMax) != kGtNext || GoneTakeTriedAdd(&tried, o1[0]) != 1 || GoneTakeTriedAdd(&tried, o1[0]) != 0)
            Fail("the silent first receiver is not passed over once");
        const int k1b = cooplo::AreaReceiverOrder(on1, 2, 1, 3, 2, &tried[0], (int)tried.size(), o1, 4, &niw);
        if (k1b != 1 || o1[0] != 1 || GoneTakeOverDecide(0, 1, k1b, o1[0], 1, 0) != kGtTake) Fail("the next game in line does not take it");
        if (GoneTakeOverDecide(0, 1, k1b, o1[0], 3, 0) != kGtWait) Fail("the passed-over game does not wait for the next taker");
        GoneTakeTriedAdd(&tried, 1);
        const int k1c = cooplo::AreaReceiverOrder(on1, 2, 1, 3, 2, &tried[0], (int)tried.size(), o1, 4, &niw);
        if (k1c != 0 || GoneTakeOverDecide(0, 1, k1c, -1, 1, 0) != kGtDrop) Fail("an exhausted order does not drop the copy");
        const int k2 = cooplo::AreaReceiverOrder(on3, 3, 1, -1, 2, 0, 0, o3, 4, &niw);
        if (k2 != 2 || o3[0] != 1) Fail("with no holder the lower slot is not first");
    }
    {   /* settled: re-keyed by the taker's OWNER_MOVED, taken here, or gone */
        OwnerMap o; UidSet l; const unsigned int gone = cooplive::RelayPeerId(2);
        o[10] = gone; o[11] = cooplive::RelayPeerId(3); o[12] = gone; l.insert(12);
        if (GoneTakeSettled(o, l, 10, gone)) Fail("a row still keyed to the leaver is settled");
        if (!GoneTakeSettled(o, l, 11, gone) || !GoneTakeSettled(o, l, 12, gone) || !GoneTakeSettled(o, l, 13, gone)) Fail("a re-keyed, taken or gone row is not settled");
    }
}

/* A SESSION'S END KEEPS THE WORLD ROAD'S ROWS WHILE THE WORLD LINK IS UP (src/common/peergone.h SessionEndKeepsRow) */
void t_t606_session_end_rows()
{
    using namespace cooppg;
    const unsigned int world = cooplive::RelayPeerId(4), raw = 7u;
    if (!SessionEndKeepsRow(true, false, world)) Fail("a world-road player's row is wiped by the session's end while the world link is up");
    if (SessionEndKeepsRow(false, false, world)) Fail("a world-road row outlives the world link");
    if (SessionEndKeepsRow(true, false, raw)) Fail("a session-link row (raw key) outlives the session");
    if (SessionEndKeepsRow(true, true, world)) Fail("this game's own row stays in the per-session view");
}

/* A COPY LEFT BEHIND WHILE THIS GAME WAS OFF THE WORLD LINK IS ASKED OF ITS OWNER (src/common/lostcopy.h ReturnLookDecide) */
void t_t657_return_check()
{
    using namespace lostcopy;
    if (ReturnEdge(0, 5) || ReturnEdge(5, 5) || ReturnEdge(5, 0) || !ReturnEdge(5, 6)) Fail("the return edge is read wrongly");
    ReturnRow r; r.uid = 9; r.ownerKey = 0x10001u; r.state = kRtWaiting; r.asks = 0; r.looksWaiting = 0; r.verdict = 0; r.unsent = 0;
    if (ReturnLookDecide(0, 1, 0, 1, r) != kRtaDrop || ReturnLookDecide(1, 0, 0, 1, r) != kRtaDrop) Fail("a copy gone or re-owned keeps its row");
    if (ReturnLookDecide(1, 1, 1, 1, r) != kRtaKeepHeard) Fail("a copy its owner streams is asked about");
    if (ReturnLookDecide(1, 1, 0, 0, r) != kRtaWait) Fail("an ask goes with no road");
    if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaAsk) Fail("a silent copy is not asked");
    ReturnLookApply(&r, kRtaAsk, 1, 1);
    if (r.state != kRtAsked || r.asks != 1) Fail("a sent ask is not booked");
    if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaWait) Fail("an ask awaiting its answer is asked again at once");
    ReturnAnswerApply(&r, kRvHeld);
    if (r.state != kRtWaiting) Fail("a HELD answer does not ask again");
    ReturnLookApply(&r, kRtaAsk, 1, 1);
    for (int i = 0; i < kReturnAnswerLooks; ++i)
    {
        if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaWait) Fail("an owed answer is not waited for");
        ReturnLookApply(&r, kRtaWait, 0, 1);
    }
    if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaAsk) Fail("an unanswered ask is not asked again");
    ReturnLookApply(&r, kRtaAsk, 0, 1);
    if (r.state != kRtWaiting || r.asks != kReturnAsksMax - 1 || r.unsent != 1) Fail("an ask that found no road is counted as an ask");
    if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaAsk) Fail("an ask that found no road is not tried again");
    ReturnLookApply(&r, kRtaAsk, 1, 1);
    if (r.state != kRtAsked || r.asks != kReturnAsksMax) Fail("a sent ask is not counted");
    for (int i = 0; i < kReturnAnswerLooks; ++i) ReturnLookApply(&r, kRtaWait, 0, 1);
    if (ReturnLookDecide(1, 1, 0, 1, r) != kRtaGiveUp) Fail("the asks are not bounded");
    {
        ReturnRow u = r; u.asks = 0; u.unsent = 0; u.state = kRtWaiting;
        for (int i = 0; i < kReturnUnsentMax; ++i)
        {
            if (ReturnLookDecide(1, 1, 0, 1, u) != kRtaAsk) Fail("an ask that found no road is not tried again within its bound");
            ReturnLookApply(&u, kRtaAsk, 0, 1);
        }
        if (u.asks != 0 || ReturnLookDecide(1, 1, 0, 1, u) != kRtaGiveUp) Fail("the asks that find no road are not bounded");
    }
    /* at the end of the asks: an owner out of the world roster past the world server's hold left while this game was away */
    if (ReturnGiveUpOwnerGone(0, cooppg::kPlayerGoneHoldSec, cooppg::kPlayerGoneHoldSec) != 1) Fail("an owner away past the hold is not handled as gone");
    if (ReturnGiveUpOwnerGone(0, cooppg::kPlayerGoneHoldSec - 1.0, cooppg::kPlayerGoneHoldSec) != 0) Fail("an owner away within the hold (a blip) is handled as gone");
    if (ReturnGiveUpOwnerGone(1, 100.0, cooppg::kPlayerGoneHoldSec) != 0 || ReturnGiveUpOwnerGone(-1, 100.0, cooppg::kPlayerGoneHoldSec) != 0)
        Fail("an owner in the world, or one read with no roster, is handled as gone");
    if (cooppg::PlayerGoneHoldDecide(0.0, cooppg::kPlayerGoneHoldSec, cooppg::kPlayerGoneHoldSec, false) != cooppg::kPgHoldSend) Fail("the games' hold differs from the world server's rule");
    if (ReturnGiveUpOwnerGone(0, cooppg::kPlayerGoneHoldSec + 1.0, cooppg::kPlayerGoneHoldSec + cooppg::kReturnAwayMarginSec) != 0)
        Fail("an owner away past the hold but within its margin is handled as gone");
    /* the return check's own people: only a LIVE answer the dual-run rule yields to gives a person up; NOT-LIVE or MOVED never does */
    if (ReturnOwnYield(1, 1) != 1) Fail("a person another game runs now at a higher generation is not given up");
    if (ReturnOwnYield(0, 1) != 0 || ReturnOwnYield(0, 0) != 0) Fail("a NOT-LIVE or MOVED answer gives up a person this game runs");
    if (ReturnOwnYield(1, 0) != 0) Fail("a LIVE answer the dual-run rule keeps here gives the person up");
    ReturnRow a = r; a.state = kRtAsked; a.asks = 1;
    ReturnAnswerApply(&a, kRvNotAnnounced);
    if (ReturnLookDecide(1, 1, 0, 1, a) != kRtaWithdraw) Fail("a NOT ANNOUNCED copy is not withdrawn");
    /* FAILS BEFORE: an (older build's) NOT IN AREA answer withdrew a copy that stands here */
    ReturnRow na = r; na.state = kRtAsked; na.asks = 1; na.unsent = 0; na.looksWaiting = 0;
    ReturnAnswerApply(&na, kRvNotInArea);
    if (na.state != kRtWaiting || ReturnLookDecide(1, 1, 0, 1, na) != kRtaAsk) Fail("a NOT IN AREA answer withdrew the copy, or was not asked again");
    if (ReturnLookDecide(1, 1, 1, 1, a) != kRtaKeepHeard) Fail("a copy its owner streams after the answer is withdrawn");
    ReturnRow s = r; s.state = kRtAsked; ReturnAnswerApply(&s, kRvSent);
    ReturnRow m = r; m.state = kRtAsked; ReturnAnswerApply(&m, kRvNotMine);
    if (ReturnLookDecide(1, 1, 0, 1, s) != kRtaKeepAnswered || ReturnLookDecide(1, 1, 0, 1, m) != kRtaKeepAnswered) Fail("a SENT / NOT MINE copy is withdrawn");
    ReturnRow w = r; w.state = kRtWaiting; ReturnAnswerApply(&w, kRvNotAnnounced);
    if (w.state != kRtWaiting) Fail("an answer nobody asked for is taken");
    ReturnRow q = r; q.state = kRtAsked; q.looksWaiting = 0; ReturnLookApply(&q, kRtaWait, 0, 0);
    if (q.looksWaiting != 0) Fail("a look with the road down counts toward the answer's wait");
}

/* UNLOAD AND THE PEER-GONE REMOVAL TAKE DESPAWN'S ROUTE (src/common/uidtable.h DespawnRoute) */
void t_t668_unload_route()
{
    /* every removal (DESPAWN, UNLOAD, the peer-gone removal) asks this one route: a live copy is removed; with none, a held raw trace or
       puppet entry is dropped - the hole UNLOAD and the peer-gone removal had, where the old route answered "unknown" and left the
       puppet listed; nothing held is unknown */
    for (int live = 0; live < 2; ++live) for (int raw = 0; raw < 2; ++raw) for (int pup = 0; pup < 2; ++pup)
    {
        const int want = live ? coopuid::kRouteRemove : ((raw || pup) ? coopuid::kRouteDropStale : coopuid::kRouteUnknown);
        if (coopuid::DespawnRoute(live, raw, pup) != want) Fail("a removal is routed wrongly");
        if (!live && (raw || pup) && coopuid::DespawnRouteLegacy(live, raw, pup) != coopuid::kRouteUnknown) Fail("the old route's hole is mis-stated");
    }
}

/* ---- A GUARD'S CAGING IS ALWAYS ANSWERED; A MEDIC'S TREATMENT IS CONFIRMED BY THE OWNER (cageask.h, treatwire.h) ---------- */
void t_t607_cage_ask_step()
{
    if (cageask::kCageAskOwnerStates != 3 || cageask::kCageAskSendsMax != 3) Fail("the cage ask's bounds moved");
    if (cageask::CageAskStep(true, true, true, 9, 9) != cageask::kCageAskRefused) Fail("a refusal does not end the ask first");
    if (cageask::CageAskStep(false, false, false, 9, 9) != cageask::kCageAskGone) Fail("a copy out of the cage still asks");
    if (cageask::CageAskStep(false, true, false, 0, 1) != cageask::kCageAskGone) Fail("a copy out of the cage is called settled");
    if (cageask::CageAskStep(true, true, false, 0, 1) != cageask::kCageAskSettled) Fail("the owner's word showing the cage does not settle the ask");
    if (cageask::CageAskStep(true, true, false, 9, 9) != cageask::kCageAskSettled) Fail("a late caging word is given up");
    for (int st = 0; st < cageask::kCageAskOwnerStates; ++st)
        if (cageask::CageAskStep(true, false, false, st, 1) != cageask::kCageAskWait) { Fail("the ask is resent before the owner's STATEs could show the cage"); break; }
    for (int s = 1; s < cageask::kCageAskSendsMax; ++s)
        if (cageask::CageAskStep(true, false, false, cageask::kCageAskOwnerStates, s) != cageask::kCageAskResend) { Fail("an unanswered ask is not sent again"); break; }
    if (cageask::CageAskStep(true, false, false, cageask::kCageAskOwnerStates, cageask::kCageAskSendsMax) != cageask::kCageAskGiveUp) Fail("the ask is never given up");
    /* the whole road: each send waits its own STATEs; the give-up comes after kOwnerStates x kSendsMax unanswered STATEs, never sooner */
    int states = 0, sends = 1, seen = 0, step = cageask::kCageAskWait;
    while (seen < 100)
    {
        step = cageask::CageAskStep(true, false, false, states, sends);
        if (step == cageask::kCageAskResend) { ++sends; states = 0; continue; }
        if (step == cageask::kCageAskGiveUp) break;
        ++states; ++seen;
    }
    if (step != cageask::kCageAskGiveUp || sends != cageask::kCageAskSendsMax || seen != cageask::kCageAskOwnerStates * cageask::kCageAskSendsMax)
        Fail("the ask's road is not bounded by the owner's STATEs");
}
void t_t607_prison_in_keeps_first_at()
{
    for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) for (int c = 0; c < 2; ++c)
    {
        const bool want = a && b && c;
        if (cageask::PrisonInKeepsFirstAt(a != 0, b != 0, c != 0) != want)
        { Fail(want ? "a resend restarts the owner's own limit" : "another game's or another cage's request inherits the waiting one's age"); break; }
    }
}
/* ---- THE OWNER ENTERS THE BED FROM THE CARRY, AS THE ENGINE DOES (bedhandoff.h) ------------------------------------------- */
void t_t724_bed_own_step()
{
    using namespace bedhandoff;
    /* args: inSomething, beingCarried, carriedByAskerCopy, mayBeCarried, ragdollActive, recarryPosted, carrierCarriesBody, carrierCarriesOther */
    if (BedOwnStep(true, true, true, true, true, true, true, true) != kBedOwnRefuseInSomething) Fail("a body in a bed or cage is not refused first");
    if (BedOwnStep(true, false, false, true, false, false, false, false) != kBedOwnRefuseInSomething) Fail("a lying body in a bed or cage is not refused");
    if (BedOwnStep(false, true, true, true, false, false, false, false) != kBedOwnFromCarry) Fail("a body the asking game's copy carries is not put in from the carry");
    if (BedOwnStep(false, true, true, true, true, false, false, false) != kBedOwnFromCarry) Fail("a carried body's ragdoll read stops the put-in from the carry");
    if (BedOwnStep(false, true, false, true, false, false, false, false) != kBedOwnWait) Fail("a body a third game's copy carries does not wait");
    if (BedOwnStep(false, true, true, false, false, false, false, false) != kBedOwnRefuseAwake) Fail("an awake carried body is put in from the carry");
    if (BedOwnStep(false, false, false, false, true, false, false, false) != kBedOwnRefuseAwake) Fail("an awake lying body is put in");
    if (BedOwnStep(false, false, false, true, false, false, false, false) != kBedOwnPutIn) Fail("a still lying body is not put in at once");
    if (BedOwnStep(false, false, false, true, true, false, false, false) != kBedOwnRecarry) Fail("a fallen body is not picked up again");
    if (BedOwnStep(false, false, false, true, true, false, false, true) != kBedOwnRefuseCarrierBusy) Fail("a carrier copy carrying something else is told to pick it up");
    /* after the pick-up again was posted */
    if (BedOwnStep(false, true, false, true, false, true, true, false) != kBedOwnFromCarry) Fail("the pick-up again does not lead into the bed");
    if (BedOwnStep(false, true, true, true, false, true, false, false) != kBedOwnRefuseCarrierBusy) Fail("someone else carrying it after the pick-up again is not refused");
    if (BedOwnStep(false, true, false, false, false, true, true, false) != kBedOwnRefuseAwake) Fail("a body that woke in the carry again is put in");
    if (BedOwnStep(false, false, false, false, true, true, false, false) != kBedOwnRefuseAwake) Fail("a body that woke before the pick-up again is picked up");
    if (BedOwnStep(false, false, false, true, false, true, false, false) != kBedOwnRecarry) Fail("a pick-up again the engine refused is not tried again");
    if (BedOwnStep(false, false, false, true, true, true, false, true) != kBedOwnRefuseCarrierBusy) Fail("a carrier copy that took up something else is not refused");
    if (BedOwnStep(true, true, false, true, false, true, true, false) != kBedOwnRefuseInSomething) Fail("a body in a bed or cage after the pick-up again is not refused");
    /* carried by a third game's copy (or this game's own character): never into the bed from that carry, whatever else is read */
    for (int m = 0; m < 16; ++m)
    {
        const int st = BedOwnStep(false, true, false, (m & 1) != 0, (m & 2) != 0, false, (m & 4) != 0, (m & 8) != 0);
        if (st == kBedOwnFromCarry || st == kBedOwnRecarry || st == kBedOwnPutIn) { Fail("a body a third game's copy carries is taken from that carry"); break; }
    }
    /* every input gives one of the seven steps */
    for (int m = 0; m < 256; ++m)
    {
        const int st = BedOwnStep((m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0, (m & 16) != 0, (m & 32) != 0, (m & 64) != 0, (m & 128) != 0);
        if (st < kBedOwnWait || st > kBedOwnRefuseCarrierBusy) { Fail("a bed step outside the seven"); break; }
    }
    if (kBedRecarryPickupsMax != 3) Fail("the pick-up again bound moved");
    if (!BedRecarryMayPickUp(0) || !BedRecarryMayPickUp(kBedRecarryPickupsMax - 1) || BedRecarryMayPickUp(kBedRecarryPickupsMax))
        Fail("the pick-ups again are not bounded at kBedRecarryPickupsMax");
    if (!CarryEndHoldsDrop(true, true)) Fail("an own body with a bed request pending is ragdoll-dropped");
    if (CarryEndHoldsDrop(true, false) || CarryEndHoldsDrop(false, true) || CarryEndHoldsDrop(false, false)) Fail("a carry end with no own body's bed request is held");
}
void t_t607_treat_ask_step()
{
    if (cooptreat::kTreatOwnerStates != 2 || cooptreat::kTreatSendsMax != 3) Fail("the treatment ask's bounds moved");
    if (cooptreat::TreatAskStep(false, 9, 9) != cooptreat::kTreatAskNone) Fail("nothing pending still steps");
    for (int st = 0; st < cooptreat::kTreatOwnerStates; ++st)
        if (cooptreat::TreatAskStep(true, st, 1) != cooptreat::kTreatAskWait) { Fail("a treatment is resent before the owner's STATEs could carry it"); break; }
    for (int s = 1; s < cooptreat::kTreatSendsMax; ++s)
        if (cooptreat::TreatAskStep(true, cooptreat::kTreatOwnerStates, s) != cooptreat::kTreatAskResend) { Fail("an unconfirmed treatment is not sent again"); break; }
    if (cooptreat::TreatAskStep(true, cooptreat::kTreatOwnerStates, cooptreat::kTreatSendsMax) != cooptreat::kTreatAskGiveUp) Fail("an unconfirmed treatment is held forever");
}
void t_t607_treat_confirmed_by()
{
    const float eps = 0.01f;
    if (!cooptreat::TreatConfirmedBy(0.8f, 0.8f, eps)) Fail("the owner's equal value does not confirm");
    if (!cooptreat::TreatConfirmedBy(0.795f, 0.8f, eps)) Fail("a value within the tolerance does not confirm");
    if (!cooptreat::TreatConfirmedBy(0.9f, 0.8f, eps)) Fail("a higher owner value does not confirm");
    if (cooptreat::TreatConfirmedBy(0.5f, 0.8f, eps)) Fail("an older, lower owner value confirms the treatment");
    if (cooptreat::TreatConfirmedBy(0.0f, 0.8f, eps)) Fail("an untreated owner value confirms the treatment");
    if (!cooptreat::TreatConfirmedBy(0.0f, 0.0f, eps) || !cooptreat::TreatConfirmedBy(0.3f, 0.0f, eps)) Fail("a part not raised here blocks the confirmation");
}
bool T702Near(float a, float b) { return a - b < 1.0e-5f && b - a < 1.0e-5f; }
/* A medic's work on a copy, per part: a visible rise travels at the copy's value and uses up the hook's amount; a part only the hook
   saw (a robotic repair) travels as the amount alone; nothing, or an amount within the tolerance, travels nothing and keeps it. */
void t_t702_treat_part_decide()
{
    const float eps = 0.01f;
    cooptreat::TreatPartSend d = cooptreat::TreatPartDecide(0.2f, 0.3f, 0.5f, eps);
    if (d.kind != cooptreat::kTreatPartVisible || !T702Near(d.value, 0.5f) || !T702Near(d.amountUsed, 0.3f))
        Fail("a flesh part's visible bandage does not travel at the copy's value with the hook's amount used up");
    d = cooptreat::TreatPartDecide(0.2f, 0.0f, 0.7f, eps);
    if (d.kind != cooptreat::kTreatPartVisible || !T702Near(d.value, 0.7f) || d.amountUsed != 0.0f) Fail("a raise with no hook amount (treattest) does not travel");
    d = cooptreat::TreatPartDecide(0.0f, 0.05f, 0.0f, eps);
    if (d.kind != cooptreat::kTreatPartHookOnly || !T702Near(d.value, 0.05f) || !T702Near(d.amountUsed, 0.05f))
        Fail("a robotic repair only the hook saw does not travel as its amount alone");
    d = cooptreat::TreatPartDecide(0.3f, 0.05f, 0.1f, eps);
    if (d.kind != cooptreat::kTreatPartHookOnly || !T702Near(d.value, 0.05f)) Fail("a robotic part's baseline or leftover bandage travels with the repair");
    d = cooptreat::TreatPartDecide(0.2f, 0.0f, 0.2f, eps);
    if (d.kind != cooptreat::kTreatPartNone || d.value != 0.0f || d.amountUsed != 0.0f) Fail("an untouched part travels");
    d = cooptreat::TreatPartDecide(0.2f, 0.005f, 0.2f, eps);
    if (d.kind != cooptreat::kTreatPartNone || d.value != 0.0f || d.amountUsed != 0.0f) Fail("an amount within the tolerance is sent or used up instead of kept for later");
    d = cooptreat::TreatPartDecide(0.4f, 0.0f, 0.1f, eps);
    if (d.kind != cooptreat::kTreatPartNone || d.value != 0.0f) Fail("a lowered value travels");
}
/* After a send, what it carried is subtracted from the hook's running amount - an addition made since is kept, never below 0. */
void t_t702_treat_added_after_send()
{
    if (cooptreat::TreatAddedAfterSend(0.3f, 0.3f) != 0.0f) Fail("a fully sent amount is not used up");
    if (!T702Near(cooptreat::TreatAddedAfterSend(0.45f, 0.3f), 0.15f)) Fail("an amount added after the read is lost");
    if (cooptreat::TreatAddedAfterSend(0.1f, 0.3f) != 0.0f) Fail("the running amount goes below 0");
    if (!T702Near(cooptreat::TreatAddedAfterSend(0.2f, 0.0f), 0.2f)) Fail("a part the send did not carry loses its amount");
}
/* The treatment hook builds no string (it can run on an AI worker) and credits only this game's own medic; TreatTick sends only
   raised values, subtracts what it sent, never holds a hook-only part and frees stale hook slots; the owner's STATE confirms the held
   bandage itself. */
void t_t702_treat_source()
{
    const std::string med = T313ReadFile("../coop-plugin/medical.cpp");
    const size_t a = med.find("int TreatPartsRead(");
    const size_t b = med.find("void InstallCopyTreatCatch()");
    if (a == std::string::npos || b == std::string::npos || b < a) { Fail("medical.cpp: the treatment hook is not where it is looked for"); return; }
    const std::string hook = med.substr(a, b - a);
    if (hook.find("std::string") != std::string::npos || hook.find("DebugLog") != std::string::npos || hook.find("ErrorLog") != std::string::npos
        || hook.find(" new ") != std::string::npos)
        Fail("the treatment hook builds a string, logs or allocates");
    if (hook.find("coop::TreatNoteAdded(") == std::string::npos || hook.find("OwnedMirrorExactAnyThread()") == std::string::npos)
        Fail("the treatment hook does not record through TreatNoteAdded behind the owned-mirror test");
    if (med.find("InstallCopyTreatCatch();") == std::string::npos) Fail("the treatment hook is never installed");
    const std::string sp = T313ReadFile("../coop-plugin/spawn.cpp");
    const size_t c = sp.find("void TreatTick()");
    const size_t d = sp.find("void ApplyRemoteTreat(");
    if (c == std::string::npos || d == std::string::npos || d < c) { Fail("spawn.cpp: TreatTick is not where it is looked for"); return; }
    const std::string tick = sp.substr(c, d - c);
    const size_t send = tick.find("net::SendTreat(m)");
    const size_t use = tick.find("TreatAddUse(uid, used, t.n)");
    if (tick.find("const cooptreat::TreatPartSend pd = cooptreat::TreatPartDecide(t.band[i], added[i], cb, kTreatEps);") == std::string::npos
        || tick.find("m.bandageLevel[i] = pd.value;") == std::string::npos
        || tick.find("m.splintLevel[i] = cj > t.jury[i] + kTreatEps ? cj : 0.0f;") == std::string::npos
        || tick.find("if (!ok || !raised || !t.based) continue;") == std::string::npos)
        Fail("TreatTick no longer sends only raised values");
    if (send == std::string::npos || use == std::string::npos || use < send) Fail("TreatTick does not subtract what a send carried, after the send");
    if (tick.find("TreatAddClear") != std::string::npos) Fail("a send zeroes the hook's amounts");
    if (sp.find("if (!cooptreat::TreatConfirmedBy(in[2], tr->holdBand[i], kTreatEps)") == std::string::npos || sp.find("sentFlesh") != std::string::npos)
        Fail("the owner's STATE is not confirmed by the held bandage alone");
    // a hook-only part never goes into the hold: the owner's robotic bandage drains at once, and a re-send would repair it again
    if (tick.find("const float holdB = hookOnly ? 0.0f : m.bandageLevel[i];") == std::string::npos
        || tick.find("if (!keepOld) t.noResend[i] = used[i] > 0.0f;") == std::string::npos
        || tick.find("r.bandageLevel[i] = t.noResend[i] ? 0.0f : t.holdBand[i];") == std::string::npos
        || tick.find("t.holdBand[i] = keepOld ? t.holdBand[i] : holdB;") == std::string::npos
        || tick.find("if (held) { t.pend = true; t.pendStates = 0; t.sends = 1; }") == std::string::npos)
        Fail("TreatTick holds a hook-only part, or a send of hook-only parts starts a hold");
    size_t h = 0;
    int holdWrites = 0;
    while ((h = tick.find("t.holdBand[i] =", h)) != std::string::npos) { ++holdWrites; ++h; }
    if (holdWrites != 1) Fail("TreatTick writes the bandage hold in more than one place");
    const size_t sweep = tick.find("TreatAddSweep();");
    const size_t empty = tick.find("if (g_treat.empty()) return;");
    if (sweep == std::string::npos || empty == std::string::npos || empty < sweep) Fail("stale hook slots are not freed when no copy is tracked");
    if (hook.find("return (m != 0 && net::IsUidMineAnyThread(m)) ? 1 : 0;") == std::string::npos) Fail("a medic with no uid counts as this game's own");
}
/* REQUEST and ANSWER carry the talker's purse (-1 unknown), required (a cut before it is Trunc); TalkMoneyCond is the engine's money
   condition (0x670AA0): compare 1 = purse < price, any other compare = price <= purse. */
void t_t677_talk_purse()
{
    using namespace cooptalk;
    const int purses[3] = { 0, 1234, -1 };
    for (int k = 0; k < 3; ++k)
    {
        TalkMsg q;
        q.kind = kTalkRequest; q.npcUid = 5; q.targetUid = 6; q.reqId = 9; q.event = 1; q.purse = purses[k];
        std::vector<char> b;
        TalkMsg r;
        if (!EncodeTalk(&b, q) || DecodeTalk(&b[0], b.size(), &r) != kTalkDecodeOk || r.purse != purses[k] || r.reqId != 9 || r.event != 1)
            Fail("a REQUEST must round-trip its purse " + I(purses[k]));
        if (b.size() >= 4)
        {
            TalkMsg o;
            if (DecodeTalk(&b[0], b.size() - 4, &o) != kTalkDecodeTrunc) Fail("a REQUEST without the purse (the older layout) must decode as Trunc");
        }
        TalkMsg a;
        a.kind = kTalkAnswer; a.convId = 3; a.npcUid = 5; a.targetUid = 6; a.result = kTalkAnsReply; a.index = 2; a.replyId = "r9"; a.purse = purses[k];
        std::vector<char> ab;
        TalkMsg ra;
        if (!EncodeTalk(&ab, a) || DecodeTalk(&ab[0], ab.size(), &ra) != kTalkDecodeOk || ra.purse != purses[k] || ra.replyId != "r9" || ra.index != 2)
            Fail("an ANSWER must round-trip its purse " + I(purses[k]));
        if (ab.size() >= 4)
        {
            TalkMsg o;
            if (DecodeTalk(&ab[0], ab.size() - 4, &o) != kTalkDecodeTrunc) Fail("an ANSWER without the purse (the older layout) must decode as Trunc");
        }
    }
    TalkMsg d;
    if (d.purse != -1) Fail("a new TalkMsg's purse must be -1 (unknown)");
    /* compare 0 (at least): price <= purse */
    if (TalkMoneyCond(0, 100, 99)) Fail("at least 100 with 99: false");
    if (!TalkMoneyCond(0, 100, 100)) Fail("at least 100 with 100: true (an equal purse passes)");
    if (!TalkMoneyCond(0, 100, 101)) Fail("at least 100 with 101: true");
    /* compare 1 (less than): purse < price */
    if (!TalkMoneyCond(1, 100, 99)) Fail("less than 100 with 99: true");
    if (TalkMoneyCond(1, 100, 100)) Fail("less than 100 with 100: false");
    if (TalkMoneyCond(1, 100, 101)) Fail("less than 100 with 101: false");
    /* any other compare answers as compare 0 */
    if (TalkMoneyCond(2, 100, 99) || !TalkMoneyCond(2, 100, 100)) Fail("compare 2 answers as at least");
    if (!TalkMoneyCond(0, 0, 0) || TalkMoneyCond(1, 0, 0)) Fail("price 0 with an empty purse");
}

/* TalkPurseAfterAct: the talker's purse after its game applies an ACT - TAKE_MONEY (8) off, GIVE_MONEY (9) on; the whole ACT refused
   (unchanged) when the TAKE_MONEY total is over the purse; a hire's price (8 with 3 / 18) and 10 PAY_BOUNTY not counted. */
void t_t677_purse_after_act()
{
    using namespace cooptalk;
    std::vector<int> ty, va;
    if (TalkPurseAfterAct(250, ty, va) != 250) Fail("no actions: unchanged");
    ty.push_back(8); va.push_back(100);
    if (TalkPurseAfterAct(250, ty, va) != 150) Fail("take 100 from 250: 150");
    if (TalkPurseAfterAct(100, ty, va) != 0) Fail("take 100 from 100: 0 (an equal purse pays)");
    if (TalkPurseAfterAct(99, ty, va) != 99) Fail("take 100 from 99: refused whole, unchanged");
    if (TalkPurseAfterAct(-1, ty, va) != -1) Fail("unknown purse stays unknown");
    ty.push_back(9); va.push_back(30);
    if (TalkPurseAfterAct(250, ty, va) != 180) Fail("take 100, give 30 from 250: 180");
    if (TalkPurseAfterAct(99, ty, va) != 99) Fail("take 100, give 30 from 99: refused whole, the give not counted either");
    ty.push_back(8); va.push_back(150);
    if (TalkPurseAfterAct(249, ty, va) != 249) Fail("two takes 250 total from 249: refused whole");
    if (TalkPurseAfterAct(250, ty, va) != 30) Fail("two takes 250 total and give 30 from 250: 30");
    std::vector<int> g1(1, 9), g1v(1, 40);
    if (TalkPurseAfterAct(250, g1, g1v) != 290) Fail("give 40 to 250: 290");
    if (TalkPurseAfterAct(0x7FFFFFF0, g1, g1v) != 0x7FFFFFFF) Fail("a give over the int range: its top");
    std::vector<int> gn(1, -500);
    if (TalkPurseAfterAct(250, g1, gn) != -1) Fail("a negative give below 0: -1 (unknown)");
    std::vector<int> h; std::vector<int> hv;
    h.push_back(3); hv.push_back(0); h.push_back(8); hv.push_back(100);
    if (TalkPurseAfterAct(250, h, hv) != 250) Fail("a hire's price is paid later on the hire road: unchanged now");
    if (TalkPurseAfterAct(99, h, hv) != 99) Fail("a hire's price over the purse: refused, unchanged");
    h[0] = 18;
    if (TalkPurseAfterAct(250, h, hv) != 250) Fail("JOIN_SQUAD_FAST's price: unchanged now");
    std::vector<int> b(1, 10), bv(1, 500);
    if (TalkPurseAfterAct(250, b, bv) != 250) Fail("PAY_BOUNTY: the reward is not known to the sender, unchanged");
    std::vector<int> r(1, 4), rv(1, 50);
    if (TalkPurseAfterAct(250, r, rv) != 250) Fail("AFFECT_RELATIONS moves no money");
    std::vector<int> two; two.push_back(8); two.push_back(8);
    std::vector<int> one(1, 100);
    if (TalkPurseAfterAct(250, two, one) != 150) Fail("more types than values: only the pairs count");
}

/* ===== the duplicate report (src/common/dupsquad.h) ===== */
struct DupW
{
    std::vector<unsigned char> b;
    void I(int v) { unsigned char t[4]; std::memcpy(t, &v, 4); b.insert(b.end(), t, t + 4); }
    void F(float v) { unsigned char t[4]; std::memcpy(t, &v, 4); b.insert(b.end(), t, t + 4); }
    void S(const std::string& v) { I((int)v.size()); b.insert(b.end(), v.begin(), v.end()); }
};
typedef std::vector<std::pair<std::string, int> > DupInts;
typedef std::vector<std::pair<std::string, std::string> > DupStrs;
/* One record in the engine's layout: bools and ints as given, a Skin Tone when skin >= 0, strings, a race
   reference when race is not empty, and one instance per entry (the squad's member entries). */
void DupRec(DupW& w, int type, const std::string& sid, const DupInts& bools, const DupInts& ints, float skin, const DupStrs& strs,
            const std::string& race, const std::vector<std::vector<std::string> >& entries)
{
    size_t k;
    w.I((int)entries.size()); w.I(type); w.I(0); w.S("0"); w.S(sid); w.I(0);
    w.I((int)bools.size()); for (k = 0; k < bools.size(); ++k) { w.S(bools[k].first); w.b.push_back((unsigned char)bools[k].second); }
    w.I(0);
    w.I((int)ints.size()); for (k = 0; k < ints.size(); ++k) { w.S(ints[k].first); w.I(ints[k].second); }
    if (skin >= 0.0f) { w.I(1); w.S("Skin Tone"); w.F(skin); w.F(skin); w.F(skin); } else w.I(0);
    w.I(0);
    w.I((int)strs.size()); for (k = 0; k < strs.size(); ++k) { w.S(strs[k].first); w.S(strs[k].second); }
    w.I(0);
    if (!race.empty()) { w.I(1); w.S("race"); w.I(1); w.S(race); w.I(0); w.I(0); w.I(0); } else w.I(0);
    w.I((int)entries.size());
    for (k = 0; k < entries.size(); ++k)
    {
        w.S("e"); w.S("template"); for (int f = 0; f < 7; ++f) w.F(0.0f);
        w.I((int)entries[k].size()); for (size_t q = 0; q < entries[k].size(); ++q) w.S(entries[k][q]);
    }
}
void DupHandle(DupInts* ints, const std::string& pre, int s, int i)
{
    ints->push_back(std::make_pair(pre + "TYPE", 1)); ints->push_back(std::make_pair(pre + "C", 92));
    ints->push_back(std::make_pair(pre + "CS", 7)); ints->push_back(std::make_pair(pre + "S", s)); ints->push_back(std::make_pair(pre + "I", i));
}
/* A member: its character (36) and appearance (66) records; returns its entry (the two record ids). */
std::vector<std::string> DupMember(DupW& w, int no, const std::string& name, int portrait, const std::string& head, int hs, int hi, bool leader,
                                   const std::string& slaverOf, int slS, int slI)
{
    char a[32], b[32]; std::sprintf(a, "%d--INGAME", 100 + 2 * no); std::sprintf(b, "%d--INGAME", 101 + 2 * no);
    DupInts bools; bools.push_back(std::make_pair(std::string("is leader"), leader ? 1 : 0));
    DupInts ints; ints.push_back(std::make_pair(std::string("portrait_serial"), portrait)); DupHandle(&ints, "handle", hs, hi);
    DupHandle(&ints, "slaver", slaverOf.empty() ? 0 : slS, slaverOf.empty() ? 0 : slI);
    DupStrs strs; strs.push_back(std::make_pair(std::string("name"), name));
    DupRec(w, 36, a, bools, ints, -1.0f, strs, "", std::vector<std::vector<std::string> >());
    DupInts ab; ab.push_back(std::make_pair(std::string("sex female"), 0));
    DupStrs as; as.push_back(std::make_pair(std::string("head"), head)); as.push_back(std::make_pair(std::string("hair style"), std::string("2172-gamedata.base")));
    DupRec(w, 66, b, ab, DupInts(), 0.5f, as, "5276-chareditor.mod", std::vector<std::vector<std::string> >());
    std::vector<std::string> e; e.push_back(a); e.push_back(b); return e;
}
/* A whole file: the header, the given records, then the squad record. */
std::vector<unsigned char> DupFile(DupW& recs, int nrecs, const std::vector<std::vector<std::string> >& entries)
{
    DupW sq; DupRec(sq, 30, "99--INGAME", DupInts(), DupInts(), -1.0f, DupStrs(), "", entries);
    DupW f; f.I(15); f.I(200); f.I(nrecs + 1);
    f.b.insert(f.b.end(), recs.b.begin(), recs.b.end()); f.b.insert(f.b.end(), sq.b.begin(), sq.b.end());
    return f.b;
}
/* A member with all six records (character 36, AI 67, inventory 41, medical 57, stats 25, appearance 66) and one
   item (42) in its inventory, owned by the member's own handle. `at` is its place in the squad (its record ids and
   handle index), `who` picks one of four invented people; copies of one person differ only in those. */
std::vector<std::string> DupMember6(DupW& w, int at, int who, bool leader)
{
    char id[7][32];
    for (int k = 0; k < 7; ++k) std::sprintf(id[k], "%d--INGAME", 300 + 10 * at + k);
    const char* names[4] = { "Test Person A", "Test Person B", "Test Person C", "Test Person D" };
    char head[32]; std::sprintf(head, "%d-test.mod", 10 + who);
    const std::vector<std::vector<std::string> > none;
    DupInts bools; bools.push_back(std::make_pair(std::string("is leader"), leader ? 1 : 0));
    DupInts ints; ints.push_back(std::make_pair(std::string("portrait_serial"), 1000 + who)); DupHandle(&ints, "handle", 4000 + who, at + 1);
    DupHandle(&ints, "slaver", 0, 0);
    DupStrs strs; strs.push_back(std::make_pair(std::string("name"), std::string(names[who])));
    strs.push_back(std::make_pair(std::string("owner faction ID"), std::string("1-test.mod")));
    DupRec(w, 36, id[0], bools, ints, -1.0f, strs, "", none);
    DupInts ai; ai.push_back(std::make_pair(std::string("jobs"), 1));
    DupRec(w, 67, id[1], ai, DupInts(), -1.0f, DupStrs(), "", none);
    std::vector<std::vector<std::string> > bag(1, std::vector<std::string>(1, std::string(id[6])));
    DupRec(w, 41, id[2], DupInts(), DupInts(), -1.0f, DupStrs(), "", bag);
    DupInts med; med.push_back(std::make_pair(std::string("dead"), 0));
    DupRec(w, 57, id[3], med, DupInts(), -1.0f, DupStrs(), "", none);
    DupRec(w, 25, id[4], DupInts(), DupInts(), -1.0f, DupStrs(), "", none);
    DupInts ab; ab.push_back(std::make_pair(std::string("sex female"), who == 1 ? 1 : 0));
    DupStrs as; as.push_back(std::make_pair(std::string("head"), std::string(head))); as.push_back(std::make_pair(std::string("hair style"), std::string("2-test.mod")));
    DupRec(w, 66, id[5], ab, DupInts(), 0.25f + 0.1f * (float)who, as, "3-test.mod", none);
    DupInts item; DupHandle(&item, "ownedby", 4000 + who, at + 1);
    DupStrs is; is.push_back(std::make_pair(std::string("base data sid"), std::string("4-test.mod")));
    DupRec(w, 42, id[6], DupInts(), item, -1.0f, is, "", none);
    std::vector<std::string> e;
    for (int k = 0; k < 6; ++k) e.push_back(id[k]);   /* in the engine's order: 36, 67, 41, 57, 25, 66 */
    return e;
}
void t_t676_dup_report()
{
    using namespace dupsquad;
    /* The doubled shape: 4 people written twice, the copies interleaved at (0,1), (2,5), (3,6), (4,7) - so keeping
       the first half would be wrong - and only copy 0 of the first pair is the squad's leader. Each member has all
       six records, and its inventory holds an item owned by the member itself (not a link from elsewhere). */
    std::vector<unsigned char> fx;
    {
        const int person[8] = { 0, 0, 1, 2, 3, 1, 2, 3 };
        DupW w; std::vector<std::vector<std::string> > e;
        for (int at = 0; at < 8; ++at) e.push_back(DupMember6(w, at, person[at], at == 0));
        fx = DupFile(w, 8 * 7, e);
    }
    Report r;
    Analyse(&fx[0], fx.size(), &r);
    if (r.state != kSquad) { Fail("dup: the doubled squad did not read"); return; }
    if (r.members != 8 || r.groups != 4 || r.wouldRemove != 4 || r.cleanK != 2 || r.largest != 2)
        Fail("dup: the doubled squad must read members=8 groups=4 would remove=4 clean 2x");
    if (r.leaderLinks != 1 || r.linkedCopies != 1 || r.linkedRemoved != 0 || r.handleLinks != 0 || r.idLinks != 0)
        Fail("dup: the doubled squad's one link is its leader copy, which a trim can keep");
    Totals t; Add(&t, r);
    std::string line;
    if (!RecordLine("Test_Squad", r, &line) || line.find("[DUP] Test_Squad: members=8 groups=4 would remove=4 shape=clean 2x referred=0") != 0)
        Fail("dup: the record line is " + line);
    SummaryLine(t, &line);
    if (line.find("[DUP] report: 1 squad records read, 1 have duplicate members (1 clean (2x: 1), 0 partial), 4 members a trim would remove,"
                  " 0 of them referred to inside their own record, 0 unreadable") != 0)
        Fail("dup: the summary line is " + line);
    /* Any damage reads as unreadable and reports nothing. */
    Analyse(&fx[0], fx.size() - 1, &r);
    if (r.state != kUnreadable || r.members != 0 || r.groups != 0) Fail("dup: a buffer one byte short was not unreadable");
    Analyse(&fx[0], fx.size() / 2, &r);
    if (r.state != kUnreadable) Fail("dup: a buffer cut in half was not unreadable");
    { std::vector<unsigned char> more(fx); more.push_back(0); Analyse(&more[0], more.size(), &r); if (r.state != kUnreadable) Fail("dup: a byte left over was not unreadable"); }
    Analyse(&fx[0], 0, &r);
    if (r.state != kUnreadable) Fail("dup: an empty buffer was not unreadable");
    {
        /* a zone file's closing list (a count, then that many ints) is read; a list one int short is not */
        DupW z; z.b = fx; z.I(3); z.I(1); z.I(2); z.I(3);
        Analyse(&z.b[0], z.b.size(), &r);
        if (r.state != kSquad || r.groups != 4) Fail("dup: a file with a zone's closing list did not read");
        Analyse(&z.b[0], z.b.size() - 4, &r);
        if (r.state != kUnreadable) Fail("dup: a closing list one int short was not unreadable");
    }
    Add(&t, r);
    if (t.unreadable != 1 || t.squads != 1) Fail("dup: an unreadable record was counted as read");
    if (!RecordLine("x", r, &line) || line.find("unreadable") == std::string::npos) Fail("dup: an unreadable record has no line");

    /* Two different people sharing one portrait_serial never group. */
    {
        DupW w; std::vector<std::vector<std::string> > e;
        e.push_back(DupMember(w, 0, "Ana", 555, "5345-chareditor.mod", 10, 1, false, "", 0, 0));
        e.push_back(DupMember(w, 1, "Bo", 555, "5345-chareditor.mod", 10, 2, false, "", 0, 0));
        e.push_back(DupMember(w, 2, "Ana", 555, "5338-chareditor.mod", 10, 3, false, "", 0, 0));
        const std::vector<unsigned char> b = DupFile(w, 6, e);
        Analyse(&b[0], b.size(), &r);
        if (r.state != kSquad || r.members != 3) Fail("dup: the synthetic squad did not read");
        else if (r.groups != 0 || r.wouldRemove != 0) Fail("dup: people sharing only a portrait_serial (or a name and portrait) were grouped");
        if (RecordLine("s", r, &line)) Fail("dup: a record with no duplicates has a line");
    }
    /* A partial shape, and the links: B's slaver handle names A's second copy; then A's first copy leads. */
    for (int lead = 0; lead < 2; ++lead)
    {
        DupW w; std::vector<std::vector<std::string> > e;
        e.push_back(DupMember(w, 0, "Ana", 555, "5345-chareditor.mod", 10, 1, lead == 1, "", 0, 0));
        e.push_back(DupMember(w, 1, "Ana", 555, "5345-chareditor.mod", 10, 2, false, "", 0, 0));
        e.push_back(DupMember(w, 2, "Bo", 777, "5345-chareditor.mod", 10, 3, false, "slaver", 10, 2));
        const std::vector<unsigned char> b = DupFile(w, 6, e);
        Analyse(&b[0], b.size(), &r);
        if (r.state != kSquad || r.members != 3 || r.groups != 1 || r.wouldRemove != 1 || r.cleanK != 0) Fail("dup: a 2+1 squad is not one group, partial");
        if (r.handleLinks != 1) Fail("dup: a slaver handle naming a copy was not seen");
        if (lead == 0 && (r.linkedCopies != 1 || r.linkedRemoved != 0)) Fail("dup: one linked copy - a trim keeps it");
        if (lead == 1 && (r.linkedCopies != 2 || r.linkedRemoved != 1 || r.leaderLinks != 1)) Fail("dup: two linked copies - a trim must remove one");
        if (lead == 1 && (!RecordLine("p", r, &line) || line.find("shape=partial referred=1") == std::string::npos)) Fail("dup: the partial line is " + line);
    }
    /* A record id named by another record links; a readable file with no squad record is not a squad. */
    {
        DupW w; std::vector<std::vector<std::string> > e;
        e.push_back(DupMember(w, 0, "Ana", 555, "5345-chareditor.mod", 10, 1, false, "", 0, 0));
        e.push_back(DupMember(w, 1, "Ana", 555, "5345-chareditor.mod", 10, 2, false, "", 0, 0));
        DupStrs st; st.push_back(std::make_pair(std::string("carrier"), std::string("102--INGAME")));
        DupRec(w, 42, "150--INGAME", DupInts(), DupInts(), -1.0f, st, "", std::vector<std::vector<std::string> >());
        const std::vector<unsigned char> b = DupFile(w, 5, e);
        Analyse(&b[0], b.size(), &r);
        if (r.state != kSquad || r.cleanK != 2 || r.idLinks != 1 || r.linkedCopies != 1) Fail("dup: a record id naming a copy was not seen");
        DupW n; n.I(15); n.I(1); n.I(1);
        DupRec(n, 35, "1--INGAME", DupInts(), DupInts(), -1.0f, DupStrs(), "", std::vector<std::vector<std::string> >());
        Analyse(&n.b[0], n.b.size(), &r);
        if (r.state != kNotSquad) Fail("dup: a building record read as a squad");
        DupW z; z.I(15); z.I(2); z.I(2);
        DupRec(z, 4, "1--INGAME", DupInts(), DupInts(), -1.0f, DupStrs(), "", std::vector<std::vector<std::string> >());
        DupRec(z, 30, "2--INGAME", DupInts(), DupInts(), -1.0f, DupStrs(), "", std::vector<std::vector<std::string> >(1, std::vector<std::string>(1, "1--INGAME")));
        Analyse(&z.b[0], z.b.size(), &r);
        if (r.state != kNotSquad) Fail("dup: a squad record naming no character read as a squad");
    }
}

/* T-599 PaceDecide: only a SPAWN that makes a new copy is ever held; never before the frame's first copy; held once the
   frame's drain time reaches the budget. */
void t_t599_pace_decide()
{
    using namespace spawnpace;
    if (PaceDecide(0, 5, 99999, kFrameBudgetUs) != kPaceGo) Fail("a repeat SPAWN or another message is never held");
    if (PaceDecide(1, 0, 99999, kFrameBudgetUs) != kPaceGo) Fail("the frame's first copy is always made, however long the frame");
    if (PaceDecide(1, 1, kFrameBudgetUs - 1, kFrameBudgetUs) != kPaceGo) Fail("under the budget: made");
    if (PaceDecide(1, 1, kFrameBudgetUs, kFrameBudgetUs) != kPaceWait) Fail("at the budget: held");
    if (PaceDecide(1, 3, kFrameBudgetUs + 500, kFrameBudgetUs) != kPaceWait) Fail("over the budget: held");
    if (PaceDecide(1, 1, 0, -5) != kPaceWait) Fail("a negative budget reads as 0: one copy a frame");
    if (kFrameBudgetUs != 4000) Fail("the budget is 4 ms");
}

/* T-599 the burst book: FrameNoteSpawn classifies; a burst opens on a made copy, holds while entries wait, ends on the first
   empty frame without counting it, and counts a trailing frame that applied only repeats. */
void t_t599_pace_burst()
{
    using namespace spawnpace;
    PaceFrame f; FrameReset(&f);
    FrameNoteSpawn(&f, 0, 1); FrameNoteSpawn(&f, 1, 1); FrameNoteSpawn(&f, 0, 0); FrameNoteSpawn(&f, 0, 1);
    if (f.applied != 4 || f.made != 2 || f.repeat != 1 || f.notMade != 1) Fail("FrameNoteSpawn tallies");
    Burst b; BurstReset(&b);
    PaceFrame idle; FrameReset(&idle);
    if (BurstFrame(&b, idle) != 0 || b.open != 0) Fail("an idle frame opens nothing");
    PaceFrame rep; FrameReset(&rep); FrameNoteSpawn(&rep, 1, 1);
    if (BurstFrame(&b, rep) != 0 || b.open != 0) Fail("a frame of repeats alone opens nothing");
    f.usedUs = 4100; f.waited = 1; f.left = 1;
    if (BurstFrame(&b, f) != 0 || b.open != 1 || b.frames != 1 || b.waits != 1 || b.worstUs != 4100) Fail("a frame that made copies and held one opens the burst");
    PaceFrame g; FrameReset(&g); g.left = 1; g.usedUs = 300;
    if (BurstFrame(&b, g) != 0 || b.frames != 2) Fail("a frame with entries still queued keeps it open");
    PaceFrame h; FrameReset(&h); FrameNoteSpawn(&h, 0, 1); h.usedUs = 5000;
    if (BurstFrame(&b, h) != 0 || b.made != 3 || b.worstUs != 5000) Fail("the last copy's frame");
    if (BurstFrame(&b, idle) != 1 || b.frames != 3) Fail("the first empty frame ends it, uncounted");
    if (b.applied != 5 || b.repeat != 1 || b.notMade != 1) Fail("burst totals");
    BurstReset(&b);
    PaceFrame one; FrameReset(&one); FrameNoteSpawn(&one, 0, 1);
    if (BurstFrame(&b, one) != 0) Fail("one copy, queue empty: open until the next frame");
    if (BurstFrame(&b, rep) != 1 || b.frames != 2 || b.repeat != 1) Fail("a trailing frame of repeats that empties the queue ends it, counted");
}

/* T-599 the held list, simulated: 830 SPAWNs arrive in one frame with a MOVE for an already-made copy among them, a STATE and
   a DESPAWN for a held character, a catch-up END after the last SPAWN, and one MOVE for the made copy every later frame. Each copy
   costs 2 ms. The MOVEs apply in the frame they arrive (before the held SPAWNs); the STATE and DESPAWN apply right after their
   character's SPAWN, in the same frame; the END applies after every SPAWN that came before it; every copy is made, at least
   one a frame. */
struct T599Msg { int kind; unsigned int uid; };
struct T599Ev { int frame; int kind; unsigned int uid; };
void t_t599_pace_held()
{
    using namespace spawnpace;
    enum { kSpawn = 0, kMove = 1, kState = 2, kDespawn = 3, kEnd = 4 };
    std::vector<T599Msg> first;
    unsigned int u;
    { T599Msg m; m.kind = kMove; m.uid = 1; first.push_back(m); }
    for (u = 1001; u <= 1830; ++u)
    {
        T599Msg m; m.kind = kSpawn; m.uid = u; first.push_back(m);
        if (u == 1100) { T599Msg mv; mv.kind = kMove; mv.uid = 1; first.push_back(mv); }
        if (u == 1600) { T599Msg st; st.kind = kState; st.uid = 1500; first.push_back(st); T599Msg de; de.kind = kDespawn; de.uid = 1500; first.push_back(de); }
    }
    { T599Msg m; m.kind = kEnd; m.uid = 0; first.push_back(m); }
    { T599Msg m; m.kind = kMove; m.uid = 1; first.push_back(m); }
    std::map<unsigned int, int> exists; exists[1] = 1;
    std::vector<T599Msg> store; std::vector<T599Ev> log;
    HeldBook book;
    int frame = 0, made = 0, frameNoCopyWhileHeld = 0;
    long long worst = 0;
    for (frame = 1; frame <= 2000; ++frame)
    {
        long long used = 0, madeNow = 0;
        HeldPassState st;
        size_t i = 0;
        while (i < book.rows.size())
        {
            const HeldRow r = book.rows[i];
            if (HeldStep(book, r, &st, madeNow, used, kFrameBudgetUs) != kHeldApply) { ++i; continue; }
            HeldTake(&book, i);
            const T599Msg m = store[(size_t)r.ticket];
            if (m.kind == kSpawn) { exists[m.uid] = 1; used += 2000; ++madeNow; ++made; }
            if (m.kind == kDespawn) exists.erase(m.uid);
            T599Ev e; e.frame = frame; e.kind = m.kind; e.uid = m.uid; log.push_back(e);
        }
        std::vector<T599Msg> in;
        if (frame == 1) in = first;
        else if (frame <= 500) { T599Msg m; m.kind = kMove; m.uid = 1; in.push_back(m); }
        for (i = 0; i < in.size(); ++i)
        {
            const T599Msg m = in[i];
            const int newCopy = (m.kind == kSpawn && exists.find(m.uid) == exists.end()) ? 1 : 0;
            const int route = Route(book, newCopy, m.kind == kEnd ? 0u : m.uid, m.kind == kEnd ? 1 : 0, madeNow, used, kFrameBudgetUs);
            if (route != kRouteApply)
            {
                store.push_back(m);
                HeldPut(&book, route == kRouteHoldSpawn ? kKindSpawn : (route == kRouteFollow ? kKindFollow : kKindBarrier), m.kind == kEnd ? 0u : m.uid, (long long)store.size() - 1);
                continue;
            }
            if (m.kind == kSpawn) { exists[m.uid] = 1; used += 2000; ++madeNow; ++made; }
            if (m.kind == kDespawn) exists.erase(m.uid);
            T599Ev e; e.frame = frame; e.kind = m.kind; e.uid = m.uid; log.push_back(e);
        }
        if (madeNow < 1 && book.spawns > 0) frameNoCopyWhileHeld = 1;
        if (used > worst) worst = used;
        if (frame > 500 && book.rows.empty()) break;
    }
    if (made != 830) Fail("every copy made: " + I(made));
    if (!book.rows.empty()) Fail("nothing left held");
    if (frameNoCopyWhileHeld) Fail("a frame made no copy while SPAWNs were held");
    if (worst > kFrameBudgetUs) Fail("a frame over the budget at 2 ms a copy: " + I((int)worst));
    int moves = 0, lastSpawnAt = -1, endAt = -1, spawn1500At = -1, state1500At = -1, despawn1500At = -1, lastSpawnFrame = 0, endFrame = 0, s1500Frame = 0, d1500Frame = 0;
    size_t k;
    for (k = 0; k < log.size(); ++k)
    {
        const T599Ev& e = log[k];
        if (e.kind == kMove)
        {
            ++moves;
            if (e.frame == 1 && moves <= 3) {}
            else if (e.frame != moves - 2) Fail("a MOVE for the made copy waited: move " + I(moves) + " applied in frame " + I(e.frame));
        }
        if (e.kind == kSpawn) { lastSpawnAt = (int)k; lastSpawnFrame = e.frame; }
        if (e.kind == kSpawn && e.uid == 1500) { spawn1500At = (int)k; s1500Frame = e.frame; }
        if (e.kind == kState) state1500At = (int)k;
        if (e.kind == kDespawn) { despawn1500At = (int)k; d1500Frame = e.frame; }
        if (e.kind == kEnd) { endAt = (int)k; endFrame = e.frame; }
    }
    if (moves != 3 + 499) Fail("every MOVE applied: " + I(moves));
    if (!(spawn1500At >= 0 && state1500At == spawn1500At + 1 && despawn1500At == spawn1500At + 2)) Fail("the held character's STATE then DESPAWN apply right after its SPAWN");
    if (d1500Frame != s1500Frame) Fail("...in the same frame as its SPAWN");
    if (exists.find(1500) != exists.end()) Fail("the DESPAWN removed the copy its SPAWN made");
    if (!(endAt > lastSpawnAt)) Fail("the END applies after every earlier SPAWN");
    if (endFrame != lastSpawnFrame) Fail("the END applies in the frame of the last SPAWN: " + I(endFrame) + " vs " + I(lastSpawnFrame));
    if (lastSpawnFrame != 415) Fail("830 copies at 2 a frame: the last in frame 415, got " + I(lastSpawnFrame));
    /* a barrier with nothing held applies at once; a SPAWN after a waiting barrier waits for it */
    HeldBook b2;
    if (Route(b2, 0, 0, 1, 0, 0, kFrameBudgetUs) != kRouteApply) Fail("a barrier with nothing held applies");
    HeldPut(&b2, kKindSpawn, 7, 0); HeldPut(&b2, kKindBarrier, 0, 1); HeldPut(&b2, kKindSpawn, 8, 2); HeldPut(&b2, kKindFollow, 9, 3);
    if (Route(b2, 1, 10, 0, 0, 0, kFrameBudgetUs) != kRouteHoldSpawn) Fail("a new SPAWN behind held SPAWNs is held (oldest first)");
    if (Route(b2, 0, 7, 0, 0, 0, kFrameBudgetUs) != kRouteFollow) Fail("a message naming a held character follows it");
    if (Route(b2, 0, 11, 0, 5, 99999, kFrameBudgetUs) != kRouteApply) Fail("a message for a copy that exists applies at once");
    HeldPassState s2;
    if (HeldStep(b2, b2.rows[0], &s2, 1, kFrameBudgetUs, kFrameBudgetUs) != kHeldKeep) Fail("over the budget: the SPAWN is kept");
    if (HeldStep(b2, b2.rows[1], &s2, 1, 0, kFrameBudgetUs) != kHeldKeep || s2.barrierKept != 1) Fail("the barrier waits for the kept SPAWN");
    if (HeldStep(b2, b2.rows[2], &s2, 0, 0, kFrameBudgetUs) != kHeldKeep) Fail("a SPAWN after a waiting barrier waits");
    if (HeldStep(b2, b2.rows[3], &s2, 0, 0, kFrameBudgetUs) != kHeldApply) Fail("a held message whose character is no longer held applies");
    HeldTake(&b2, 0);
    if (b2.spawns != 1 || b2.uids.find(7) != b2.uids.end()) Fail("HeldTake releases the character");
}

/* T-599 MessageSubjects. FROM THE SHARED ENCODERS: SPAWN, STATE, STATS, SAY, NAME, SLAVE, INSIDE, PRISON, TREAT, SHOT, CRIME,
   BOUNTY, HIRE, TALK, EFFECT, CAPTURE / CAPTURE_DONE / CAPTURE_PLACED, OWNER_MOVED, DESPAWN and UNLOAD (both fed the WITHDRAW
   encoder's bytes), RELEASE, RELEASE_ACK, SQUAD_LEAD, ROSTER HASH / CHECK / ANSWER. HAND-BUILT here to the layout the test author
   read in the plugin's sender (net/session.cpp), so a change to that sender is NOT caught: HIT, SWING, CARRY_BREAK, COMBATMODE,
   INTENT, MOVESTOP, MOVE, TASK, APPEARANCE, CLOTHING, CONTEXT, ITEM_MOVE, ITEM_REQUEST, ITEM_CONFIRM / PLACED / REVOKE, XFER and
   XFER_ACK. For each message: the characters read are exactly the ones it carries; with any one of them held the message follows
   that SPAWN and applies once the SPAWN is applied; with none held it applies at once. */
typedef char T599LayoutsMatch[(spawnpace::kSubjRelHead == cooplo::kReleaseHead && spawnpace::kSubjRelRow == cooplo::kReleaseRowSize
    && spawnpace::kSubjRelAckHead == cooplo::kReleaseAckHead && spawnpace::kSubjRelMax == (unsigned int)cooplo::kReleaseMaxMembers
    && spawnpace::kSubjLeadHead == coopsquad::kSquadLeadHead && spawnpace::kSubjLeadMax == coopsquad::kSquadLeadMaxMembers
    && (int)spawnpace::kSubjRosterCheck == (int)cooplo::kRosterCheck && (int)spawnpace::kSubjRosterAnswer == (int)cooplo::kRosterAnswer
    && (size_t)spawnpace::kSubjCheckMax == cooplo::kRosterCheckPerChunk && (size_t)spawnpace::kSubjAnswerMax == cooplo::kRosterAnswerPerChunk) ? 1 : -1];
void T599PutU32(std::vector<char>* b, size_t at, unsigned int v)
{
    if (b->size() < at + 4) b->resize(at + 4, 0);
    std::memcpy(&(*b)[at], &v, 4);
}
void T599Expect(const char* what, unsigned int type, const std::vector<char>& b, unsigned int u1, unsigned int u2)
{
    using namespace spawnpace;
    std::vector<unsigned int> want; if (u1 != 0) want.push_back(u1); if (u2 != 0 && u2 != u1) want.push_back(u2);
    std::vector<unsigned int> got;
    const int n = b.empty() ? MessageSubjects(type, 0, 0, &got) : MessageSubjects(type, (const unsigned char*)&b[0], b.size(), &got);
    if (n != (int)want.size() || got != want) { Fail(std::string(what) + ": the characters read are not the ones it carries (" + I(n) + " read)"); return; }
    size_t k;
    for (k = 0; k < want.size(); ++k)
    {
        HeldBook bk;
        std::vector<unsigned int> one(1, want[k]);
        HeldPutNames(&bk, kKindSpawn, kScopeNone, 5u, one, 0);
        if (RouteNames(bk, 0, got, kScopeNone, 6u, 0, 0, kFrameBudgetUs) != kRouteFollow) { Fail(std::string(what) + ": with character " + I((int)k) + " held, it does not follow"); continue; }
        HeldPutNames(&bk, kKindFollow, kScopeNone, 6u, got, 1);
        HeldPassState st;
        if (HeldStep(bk, bk.rows[0], &st, 1, kFrameBudgetUs, kFrameBudgetUs) != kHeldKeep) Fail(std::string(what) + ": its SPAWN is not kept over the budget");
        if (HeldStep(bk, bk.rows[1], &st, 0, 0, kFrameBudgetUs) != kHeldKeep) Fail(std::string(what) + ": not kept while its SPAWN is held");
        HeldTake(&bk, 0);
        HeldPassState st2;
        if (HeldStep(bk, bk.rows[0], &st2, 0, 0, kFrameBudgetUs) != kHeldApply) Fail(std::string(what) + ": not applied once its SPAWN is");
    }
    HeldBook other;
    std::vector<unsigned int> stranger(1, 0x7FFF0001u);
    HeldPutNames(&other, kKindSpawn, kScopeNone, 6u, stranger, 0);
    if (RouteNames(other, 0, got, kScopeNone, 6u, 9, 999999, kFrameBudgetUs) != kRouteApply) Fail(std::string(what) + ": held although none of its characters is");
}
void T599Sender(const char* what, unsigned int type, const std::vector<char>& b)
{
    std::vector<unsigned int> got;
    const int n = b.empty() ? spawnpace::MessageSubjects(type, 0, 0, &got) : spawnpace::MessageSubjects(type, (const unsigned char*)&b[0], b.size(), &got);
    if (n != spawnpace::kSubjectsSender || !got.empty()) Fail(std::string(what) + ": not scoped to its sender");
}
void t_t599_pace_subjects_encoders()
{
    using namespace spawnpace;
    const unsigned int A = 4194308u, B = 4194359u;
    { std::vector<char> b; coopspawn::EncodeSpawn(&b, A, 1.5f, -2.25f, 300.0f, "Bonedog", "Bounty Hunters", false, 0.625f); T599Expect("SPAWN", kMsgSpawn, b, A, 0); }
    { float parts[2 * 8]; for (int i = 0; i < 16; ++i) parts[i] = 1.0f;
      std::vector<char> b; coopstate::EncodeState(&b, A, parts, 2u, 8u, 12.5f, 4, 0, 0x21u, 3.25f, 9.0f, B);
      T599Expect("STATE (the carried body is applied by the carry tick once both copies exist)", kMsgState, b, A, 0); }
    { unsigned int raw[coopstats::kStatsCount]; for (int i = 0; i < coopstats::kStatsCount; ++i) raw[i] = 0x41200000u;
      std::vector<char> b; coopstats::EncodeStatsMsg(&b, A, raw); T599Expect("STATS", kMsgStats, b, A, 0); }
    { std::vector<char> b; if (!coopsay::EncodeSay(&b, A, "hi", 2)) Fail("SAY encode"); T599Expect("SAY", kMsgSay, b, A, 0); }
    { std::vector<char> b; if (!coopname::EncodeName(&b, A, "Bob")) Fail("NAME encode"); T599Expect("NAME", kMsgName, b, A, 0); }
    { std::vector<char> b; if (!coopslave::EncodeSlave(&b, A, 1, B)) Fail("SLAVE encode"); T599Expect("SLAVE", kMsgSlave, b, A, B); }
    { std::vector<char> b; if (!p25inside::EncodeInside(&b, A, 1, "abc", 3)) Fail("INSIDE encode"); T599Expect("INSIDE", kMsgInside, b, A, 0); }
    { cooprison::PrisonMsg m; m.uid = A; m.kind = cooprison::kPrisonSlaveAsk; m.slaveFrom = 1; m.slaveWant = 2;
      std::vector<char> b; if (!cooprison::EncodePrison(&b, m)) Fail("PRISON encode"); T599Expect("PRISON", kMsgPrison, b, A, 0); }
    { cooptreat::TreatMsg m; m.uid = A; m.n = 1; m.bandageLevel[0] = 5.0f; m.splintLevel[0] = 0.0f;
      std::vector<char> b; if (!cooptreat::EncodeTreat(&b, m)) Fail("TREAT encode"); T599Expect("TREAT", kMsgTreat, b, A, 0); }
    { coopshot::ShotMsg m; m.victimUid = A; m.shooterUid = B; for (int i = 0; i < 4; ++i) m.rec[i] = 1.0f; m.onPurpose = false;
      std::vector<char> b; if (!coopshot::EncodeShot(&b, m)) Fail("SHOT encode"); T599Expect("SHOT", kMsgShot, b, A, B); }
    { coopcrime::CrimeState c; c.crime = 3; c.expiry = 20.0f; c.victimUid = B; c.factionSid = "17-gamedata.base";
      std::vector<char> b; if (!coopcrime::EncodeCrime(&b, A, c)) Fail("CRIME encode"); T599Expect("CRIME", kMsgCrime, b, A, B); }
    { coopbounty::BountyList l; coopbounty::BountyEntry e; e.sid = "17-gamedata.base"; e.amount = 300; e.crimes = 8; e.claimed = 0; e.time = 1.0; l.push_back(e);
      std::vector<char> b; if (!coopbounty::EncodeBounty(&b, A, coopbounty::kBountyKindList, l)) Fail("BOUNTY encode"); T599Expect("BOUNTY", kMsgBounty, b, A, 0); }
    { coophire::HireMsg m; m.kind = coophire::kHireReq; m.reqId = 7; m.uid = A; m.hirerUid = B; m.price = 100; m.joinType = 18; m.faction = "Drifters";
      std::vector<char> b; if (!coophire::EncodeHire(&b, m)) Fail("HIRE encode"); T599Expect("HIRE", kMsgHire, b, A, B); }
    { cooptalk::TalkMsg p; p.kind = cooptalk::kTalkPrompt; p.convId = 7; p.npcUid = A; p.targetUid = B; p.event = 1;
      p.lineSid = "1234-rebirth.mod"; p.npcText = "Hand over your stuff."; p.replyIds.push_back("r1"); p.replyTexts.push_back("No."); p.deadlineMs = 30000;
      std::vector<char> b; if (!cooptalk::EncodeTalk(&b, p)) Fail("TALK encode"); T599Expect("TALK", kMsgTalk, b, A, B); }
    { const cooffect::EffectMsg m = T327Req();
      std::vector<char> b; if (!cooffect::EncodeEffect(&b, m)) Fail("EFFECT encode"); T599Expect("EFFECT", kMsgEffect, b, m.targetUid, m.actorUid); }
    { coopcapture::CaptureReq q; q.reqId = 11; q.victimUid = A; q.slaverUid = B; q.taskType = 166; q.flags = coopcapture::kCapFlagChain;
      std::vector<char> b; if (!coopcapture::EncodeCapture(&b, q)) Fail("CAPTURE encode"); T599Expect("CAPTURE", kMsgCapture, b, A, B); }
    { coopcapture::CaptureDone d; d.reqId = 11; d.victimUid = A; d.result = coopcapture::kCapApplied; d.stateNow = 1; d.ownerUidNow = 249; d.shavedNow = 0;
      d.chainedNow = 0; d.shacklesLocked = 0;
      std::vector<char> b; if (!coopcapture::EncodeCaptureDone(&b, d)) Fail("CAPTURE_DONE encode"); T599Expect("CAPTURE_DONE", kMsgCaptureDone, b, A, 0); }
    { coopcapture::CapturePlaced pl; pl.reqId = 7; pl.victimUid = A; pl.nRows = 2; pl.placedMask = 1u;
      std::vector<char> b; if (!coopcapture::EncodeCapturePlaced(&b, pl)) Fail("CAPTURE_PLACED encode"); T599Expect("CAPTURE_PLACED", kMsgCapturePlaced, b, A, 0); }
    { std::vector<char> b; cooplo::OwnerMovedEncode16(&b, A, 3, 4, 5); T599Expect("OWNER_MOVED (slots are not characters)", kMsgOwnerMoved, b, A, 0); }
    { cooplo::WithdrawMsg w; w.uid = A; w.seq = 9; w.gen = 3; w.sectorKey = 715; w.expect.push_back(4); w.why = cooplo::kWdWhyAnnounce;
      std::vector<char> b; if (!cooplo::WithdrawEncode(&b, w)) Fail("DESPAWN encode");
      T599Expect("DESPAWN", kMsgDespawn, b, A, 0); T599Expect("UNLOAD", kMsgUnload, b, A, 0); }
    /* the session-only layouts */
    { std::vector<char> b(48, 0); T599PutU32(&b, 0, A); T599PutU32(&b, 4, B); T599Expect("HIT", kMsgHit, b, A, B); }
    { std::vector<char> b; T599PutU32(&b, 0, A); T599PutU32(&b, 4, B); T599Expect("SWING", kMsgSwing, b, A, B); T599Expect("CARRY_BREAK", kMsgCarryBreak, b, A, B); }
    { std::vector<char> b; T599PutU32(&b, 0, A); T599PutU32(&b, 4, 1); T599PutU32(&b, 8, B); T599Expect("COMBATMODE", kMsgCombatMode, b, A, B); }
    { std::vector<char> b(28, 0); T599PutU32(&b, 0, A); T599PutU32(&b, 4, 7); T599PutU32(&b, 8, B); T599Expect("INTENT", kMsgIntent, b, A, B); }
    { std::vector<char> b(20, 0); T599PutU32(&b, 0, A); T599Expect("MOVESTOP", kMsgMoveStop, b, A, 0); }
    { std::vector<char> b(36, 0); T599PutU32(&b, 0, A); T599Expect("MOVE", kMsgMove, b, A, 0); T599Expect("TASK", kMsgTask, b, A, 0);
      T599Expect("APPEARANCE", kMsgAppearance, b, A, 0); T599Expect("CLOTHING", kMsgClothing, b, A, 0); T599Expect("CONTEXT", kMsgContext, b, A, 0); }
    { std::vector<char> b; T599PutU32(&b, 0, A); b.push_back(0); T599PutU32(&b, 5, 4); b.insert(b.end(), "main", "main" + 4);
      T599Expect("ITEM_MOVE", kMsgItemMove, b, A, 0); }
    { std::vector<char> b; T599PutU32(&b, 0, 77); b.push_back(0); T599PutU32(&b, 5, A); T599PutU32(&b, 9, 4); b.insert(b.end(), "main", "main" + 4);
      const size_t at = b.size(); T599PutU32(&b, at, 1); T599PutU32(&b, at + 4, 2); T599PutU32(&b, at + 8, 3); T599PutU32(&b, at + 12, B);
      T599Expect("ITEM_REQUEST", kMsgItemRequest, b, A, B); }
    { std::vector<char> b(13, 0); T599PutU32(&b, 0, A);
      T599Expect("ITEM_CONFIRM (a request id only)", kMsgItemConfirm, b, 0, 0); T599Expect("ITEM_PLACED", kMsgItemPlaced, b, 0, 0); T599Expect("ITEM_REVOKE", kMsgItemRevoke, b, 0, 0); }
    /* XFER: leader | reason | count | members of 48 bytes, uid at 0; the intent subject at 28 is not read */
    { std::vector<char> b(12 + 2 * 48, 0); T599PutU32(&b, 0, A); T599PutU32(&b, 8, 2); T599PutU32(&b, 12, A); T599PutU32(&b, 12 + 48, B); T599PutU32(&b, 12 + 48 + 28, 0x00400009u);
      std::vector<unsigned int> got; const int n = MessageSubjects(kMsgXfer, (const unsigned char*)&b[0], b.size(), &got);
      if (n != 2 || got[0] != A || got[1] != B) Fail("XFER: the leader and its members, not an intent's subject (the taker skips an intent whose subject has no copy)");
      std::vector<char> cut(b.begin(), b.end() - 1);
      if (MessageSubjects(kMsgXfer, (const unsigned char*)&cut[0], cut.size(), &got) != kSubjectsSender) Fail("a cut XFER is scoped to its sender"); }
    { std::vector<char> b; T599PutU32(&b, 0, A); T599PutU32(&b, 4, 2); T599PutU32(&b, 8, B); T599PutU32(&b, 12, 0x0040000Au);
      std::vector<unsigned int> got; const int n = MessageSubjects(kMsgXferAck, (const unsigned char*)&b[0], b.size(), &got);
      if (n != 3 || got[2] != 0x0040000Au) Fail("XFER_ACK: the leader and every taken uid");
      T599PutU32(&b, 4, 300);
      if (MessageSubjects(kMsgXferAck, (const unsigned char*)&b[0], b.size(), &got) != kSubjectsSender) Fail("an XFER_ACK whose list cannot be read is scoped to its sender"); }
    /* the fixed-offset lists, from the shared encoders; a cut list or one counting more than its type allows is scoped to its sender */
    { cooplo::ReleaseMsg m; m.id = 5; m.keyUid = A; m.flags = cooplo::kRelFlagPutAway;
      cooplo::ReleaseRow r1; r1.uid = A; r1.gen = 3; cooplo::ReleaseRow r2; r2.uid = B; r2.gen = 4; m.rows.push_back(r1); m.rows.push_back(r2);
      std::vector<char> b; if (!cooplo::ReleaseEncode(&b, m)) Fail("RELEASE encode");
      T599Expect("RELEASE (the key character and each row's)", kMsgRelease, b, A, B);
      T599Sender("a cut RELEASE", kMsgRelease, std::vector<char>(b.begin(), b.end() - 1));
      std::vector<char> big = b; big[11] = (char)65; T599Sender("a RELEASE counting 65 rows", kMsgRelease, big);
      m.keyUid = 0x00400077u; std::vector<char> k; if (!cooplo::ReleaseEncode(&k, m)) Fail("RELEASE encode 2");
      std::vector<unsigned int> got; if (MessageSubjects(kMsgRelease, (const unsigned char*)&k[0], k.size(), &got) != 3 || got[0] != 0x00400077u) Fail("RELEASE: the key character first, then the rows"); }
    { cooplo::ReleaseAckMsg m; m.id = 5; m.adopted.push_back(A); m.deferred.push_back(B);
      std::vector<char> b; if (!cooplo::ReleaseAckEncode(&b, m)) Fail("RELEASE_ACK encode");
      T599Expect("RELEASE_ACK (the adopted, dropped and deferred uids)", kMsgReleaseAck, b, A, B);
      T599Sender("a cut RELEASE_ACK", kMsgReleaseAck, std::vector<char>(b.begin(), b.end() - 1));
      std::vector<char> big = b; T599PutU32(&big, 4, 70u); T599Sender("a RELEASE_ACK counting 70 uids", kMsgReleaseAck, big); }
    { coopsquad::SquadLeadMsg m; m.squadKey = 9; m.acting = A; m.formal = 0; m.seq = 1; m.members.push_back(B);
      std::vector<char> b; if (!coopsquad::EncodeSquadLead(&b, m)) Fail("SQUAD_LEAD encode");
      T599Expect("SQUAD_LEAD (acting, formal and the members)", kMsgSquadLead, b, A, B);
      T599Sender("a cut SQUAD_LEAD", kMsgSquadLead, std::vector<char>(b.begin(), b.end() - 1)); }
    { std::vector<cooplo::CheckRow> rows(2); rows[0].uid = A; rows[0].gen = 1; rows[1].uid = B; rows[1].gen = 2;
      std::vector<std::vector<char> > ch;
      if (!cooplo::RosterCheckEncode(&ch, 7, cooplo::kRosterListed, rows) || ch.size() != 1) Fail("ROSTER CHECK encode");
      else { T599Expect("ROSTER CHECK", kMsgRoster, ch[0], A, B); T599Sender("a cut ROSTER CHECK", kMsgRoster, std::vector<char>(ch[0].begin(), ch[0].end() - 1)); } }
    { std::vector<cooplo::AnswerRow> rows(2); rows[0].uid = A; rows[0].status = cooplo::kAnsLive; rows[0].gen = 1; rows[1].uid = B; rows[1].status = cooplo::kAnsLive; rows[1].gen = 2;
      std::vector<std::vector<char> > ch;
      if (!cooplo::RosterAnswerEncode(&ch, 7, 3, rows) || ch.size() != 1) Fail("ROSTER ANSWER encode");
      else { T599Expect("ROSTER ANSWER", kMsgRoster, ch[0], A, B); T599Sender("a cut ROSTER ANSWER", kMsgRoster, std::vector<char>(ch[0].begin(), ch[0].end() - 1)); } }
    { std::vector<cooplo::HashRow> rows(1); rows[0].sectorKey = 715; rows[0].count = 2; rows[0].hash = 99;
      std::vector<std::vector<char> > ch;
      if (!cooplo::RosterHashEncode(&ch, 4, rows) || ch.size() != 1) Fail("ROSTER HASH encode");
      else T599Sender("a ROSTER HASH (it sums its sender's characters by area)", kMsgRoster, ch[0]); }
    { static const unsigned int kNone[] = { 1u, 2u, 3u, 4u, 20u, 23u, 24u, 32u, 33u, 42u, 50u, 53u, 54u, 56u, 64u, 71u, 73u, 74u, 75u, 201u, 205u };
      std::vector<char> b(40, 0); T599PutU32(&b, 0, A); std::vector<unsigned int> got;
      for (size_t i = 0; i < sizeof(kNone) / sizeof(kNone[0]); ++i) if (MessageSubjects(kNone[i], (const unsigned char*)&b[0], b.size(), &got) != 0 || !got.empty()) Fail("type " + I((int)kNone[i]) + ": names no character"); }
    { std::vector<char> b(3, 0); std::vector<unsigned int> got;
      if (MessageSubjects(kMsgName, (const unsigned char*)&b[0], b.size(), &got) != 0) Fail("a message too short for its character reads nothing"); }
}

/* T-599 sender scope: a sender-scoped message waits only for its own sender's held SPAWNs; in the held pass a sender's kept
   barrier holds that sender's later SPAWNs and nobody else's. */
void t_t599_pace_sender_scope()
{
    using namespace spawnpace;
    HeldBook b;
    std::vector<unsigned int> one(1, 500u), none;
    HeldPutNames(&b, kKindSpawn, kScopeNone, 1u, one, 0);
    if (RouteNames(b, 0, none, kScopeSender, 1u, 0, 0, kFrameBudgetUs) != kRouteBarrier) Fail("the same sender's message waits for its held SPAWN");
    if (RouteNames(b, 0, none, kScopeSender, 2u, 0, 0, kFrameBudgetUs) != kRouteApply) Fail("another sender's message is not held behind it");
    if (RouteNames(b, 0, none, kScopeAll, 2u, 0, 0, kFrameBudgetUs) != kRouteBarrier) Fail("a message waiting for every SPAWN waits");
    HeldPassState st; st.spawnKeptFrom.insert(1u); st.anySpawnKept = 1;
    HeldRow r1; r1.kind = kKindBarrier; r1.scope = kScopeSender; r1.sender = 1u; r1.ticket = 1;
    HeldRow r2 = r1; r2.sender = 2u;
    if (HeldStep(b, r1, &st, 0, 0, kFrameBudgetUs) != kHeldKeep) Fail("a barrier behind its sender's kept SPAWN is kept");
    if (HeldStep(b, r2, &st, 0, 0, kFrameBudgetUs) != kHeldApply) Fail("another sender's barrier applies");
    HeldRow s1; s1.kind = kKindSpawn; s1.scope = kScopeNone; s1.sender = 1u; s1.names.push_back(600u); s1.ticket = 2;
    HeldRow s2 = s1; s2.sender = 2u; s2.names[0] = 601u;
    if (HeldStep(b, s1, &st, 0, 0, kFrameBudgetUs) != kHeldKeep) Fail("a SPAWN after its sender's kept barrier waits");
    if (HeldStep(b, s2, &st, 0, 0, kFrameBudgetUs) != kHeldApply) Fail("another sender's SPAWN is made under the budget");
    HeldTake(&b, 0);
    if (SpawnsFrom(b, 1u) != 0 || b.spawns != 0 || !b.spawnsFrom.empty()) Fail("HeldTake forgets the sender's count");
}

/* One frame over a held book as the plugin works it (store.cpp PaceHeldPass): the hand-over sweep, then the full pass; each copy
   costs 2 ms. Returns the tickets applied, in order. */
void T599Frame(spawnpace::HeldBook* b, long long startUs, long long startMade, std::vector<long long>* applied)
{
    using namespace spawnpace;
    long long used = startUs, made = startMade;
    for (int sweep = 0; sweep < 2; ++sweep)
    {
        if (sweep == 0 && b->urgentNamed.empty()) continue;
        HeldPassState st;
        size_t i = 0;
        while (i < b->rows.size())
        {
            if (sweep == 0 && st.budgetHit != 0) break;
            const HeldRow r = b->rows[i];
            const int step = sweep == 0 ? HeldUrgentStep(*b, r, &st, made, used, kFrameBudgetUs) : HeldStep(*b, r, &st, made, used, kFrameBudgetUs);
            if (step != kHeldApply) { ++i; continue; }
            HeldTake(b, i);
            if (r.kind == kKindSpawn) { used += 2000; ++made; }
            applied->push_back(r.ticket);
        }
    }
}
bool T599Empty(const spawnpace::HeldBook& b)
{
    return b.rows.empty() && b.uids.empty() && b.spawnsFrom.empty() && b.spawns == 0 && b.named.empty() && b.urgentNamed.empty() && b.senderRows.empty();
}

/* T-599: the SPAWNs a held hand-over names are made first, ahead of older SPAWNs, under the same budget (at least one copy a
   frame), and the hand-over applies in that frame; a row before such a SPAWN that must apply first still does. */
void t_t599_pace_handover_first()
{
    using namespace spawnpace;
    HeldBook b;
    unsigned int u;
    for (u = 100; u < 110; ++u) { std::vector<unsigned int> one(1, u); HeldPutRow(&b, kKindSpawn, kScopeNone, 1u, one, (long long)u, 0); }
    { std::vector<unsigned int> n(1, 103u); HeldPutRow(&b, kKindFollow, kScopeNone, 1u, n, 1103, 0); }
    { std::vector<unsigned int> n; n.push_back(108u); n.push_back(105u); HeldPutRow(&b, kKindFollow, kScopeNone, 1u, n, 2000, UrgentType(kMsgXfer)); }
    if (!UrgentSpawnRow(b, b.rows[8]) || !UrgentSpawnRow(b, b.rows[5]) || UrgentSpawnRow(b, b.rows[0])) Fail("the SPAWNs a held XFER names are marked, no other");
    if (UrgentType(kMsgXferAck) != 1 || UrgentType(kMsgRelease) != 1 || UrgentType(kMsgReleaseAck) != 1 || UrgentType(kMsgMove) != 0) Fail("the hand-over types");
    std::vector<long long> a;
    T599Frame(&b, 0, 0, &a);
    if (a.size() != 3 || a[0] != 105 || a[1] != 108 || a[2] != 2000) Fail("the XFER's two SPAWNs are made first and the XFER applies in the same frame (" + I((int)a.size()) + " applied)");
    a.clear(); T599Frame(&b, 0, 0, &a);
    if (a.size() != 2 || a[0] != 100 || a[1] != 101) Fail("then the older SPAWNs, oldest first");
    if (!b.urgentNamed.empty()) Fail("the hand-over's names are forgotten once it applies");
    for (int f = 0; f < 10 && !b.rows.empty(); ++f) T599Frame(&b, 0, 0, &a);
    if (!T599Empty(b)) Fail("every count returns to nothing");
    /* at least one copy a frame: a frame whose budget an earlier drain spent still makes the oldest named SPAWN */
    HeldBook c;
    for (u = 200; u < 204; ++u) { std::vector<unsigned int> one(1, u); HeldPutRow(&c, kKindSpawn, kScopeNone, 1u, one, (long long)u, 0); }
    { std::vector<unsigned int> n; n.push_back(203u); n.push_back(202u); HeldPutRow(&c, kKindFollow, kScopeNone, 1u, n, 3000, UrgentType(kMsgRelease)); }
    a.clear(); T599Frame(&c, kFrameBudgetUs * 2, 0, &a);
    if (a.size() != 1 || a[0] != 202) Fail("over the budget: one named SPAWN, the oldest named first");
    a.clear(); T599Frame(&c, 0, 0, &a);
    if (a.size() != 3 || a[0] != 203 || a[1] != 200 || a[2] != 3000) Fail("next frame: the other named SPAWN, one older SPAWN, then the RELEASE");
    /* rows that must apply first: an older row naming the same character, a sender's sender-scoped row, a barrier for every SPAWN */
    HeldBook d;
    std::vector<unsigned int> none;
    { std::vector<unsigned int> n; n.push_back(401u); n.push_back(400u); HeldPutRow(&d, kKindFollow, kScopeNone, 1u, n, 1, 0); }
    { std::vector<unsigned int> one(1, 401u); HeldPutRow(&d, kKindSpawn, kScopeNone, 1u, one, 2, 0); }
    { std::vector<unsigned int> one(1, 402u); HeldPutRow(&d, kKindSpawn, kScopeNone, 2u, one, 3, 0); }
    HeldPutRow(&d, kKindBarrier, kScopeSender, 2u, none, 4, 0);
    { std::vector<unsigned int> one(1, 403u); HeldPutRow(&d, kKindSpawn, kScopeNone, 2u, one, 5, 0); }
    HeldPutRow(&d, kKindBarrier, kScopeAll, kNoSender, none, 6, 0);
    { std::vector<unsigned int> one(1, 404u); HeldPutRow(&d, kKindSpawn, kScopeNone, 3u, one, 7, 0); }
    { std::vector<unsigned int> n; n.push_back(401u); n.push_back(402u); n.push_back(403u); n.push_back(404u); HeldPutRow(&d, kKindFollow, kScopeNone, 1u, n, 8, UrgentType(kMsgXfer)); }
    HeldPassState st;
    static const int kWant[] = { kHeldKeep, kHeldKeep, kHeldApply, kHeldKeep, kHeldKeep, kHeldKeep, kHeldKeep, kHeldKeep };
    for (size_t k = 0; k < d.rows.size(); ++k)
        if (HeldUrgentStep(d, d.rows[k], &st, 0, 0, kFrameBudgetUs) != kWant[k]) Fail("hand-over sweep, row " + I((int)k) + ": only the SPAWN nothing before it orders is made ahead");
    if (SenderRows(d, 2u) != 1 || SenderRows(d, kNoSender) != 0) Fail("the sender-scoped row is counted per sender");
    while (!d.rows.empty()) HeldTake(&d, 0);
    if (!T599Empty(d)) Fail("taking every row returns every count to nothing");
}

/* T-599 order through held messages, not only held SPAWNs. (a) a message naming X held behind Y's SPAWN holds a later message
   naming only X; (b) in the pass a kept message naming X and Y keeps a later one naming X and Z; (c) a held sender-scoped row
   holds its sender's later messages, of one character or none. */
void t_t694_record_pos_order()
{
    using namespace recordpos;
    if (NoPos(0.0f, 0.0f) != 1) Fail("x and z both 0 is no position");
    if (NoPos(0.0f, 5.0f) != 0 || NoPos(5.0f, 0.0f) != 0 || NoPos(-1.0f, 3.0f) != 0) Fail("one of x or z not 0 is a position");
    Cand c[5];
    int i;
    for (i = 0; i < 5; ++i) { c[i].have = 0; c[i].x = 0.0f; c[i].y = 0.0f; c[i].z = 0.0f; }
    if (Pick(c) != kSrcNone) Fail("nothing read: none");
    if (Pick(0) != kSrcNone) Fail("no inputs: none");
    c[kSrcPlatoon].have = 1;   /* the platoon read 0,0 */
    if (Pick(c) != kSrcNone) Fail("only the platoon's own 0,0: none");
    c[kSrcTown].have = 1; c[kSrcTown].x = 300.0f; c[kSrcTown].z = -400.0f;
    if (Pick(c) != kSrcTown) Fail("the town wins when member, platoon, last record and home are none");
    c[kSrcHome].have = 1;   /* the home key parsed to 0,0 */
    if (Pick(c) != kSrcTown) Fail("a home at 0,0 is none: the town wins");
    c[kSrcHome].x = 200.0f; c[kSrcHome].y = 9.0f; c[kSrcHome].z = 0.0f;
    if (Pick(c) != kSrcHome) Fail("the home wins over the town when it holds a position (z 0 alone is a position)");
    c[kSrcLast].have = 1;   /* the last record was at 0,0 */
    if (Pick(c) != kSrcHome) Fail("a last record at 0,0 is none: the home wins");
    c[kSrcLast].x = 0.0f; c[kSrcLast].y = 50.0f; c[kSrcLast].z = 0.0f;
    if (Pick(c) != kSrcHome) Fail("y alone does not make a position");
    c[kSrcLast].z = 7.0f;
    if (Pick(c) != kSrcLast) Fail("the last record wins over home and town");
    c[kSrcPlatoon].x = -10.0f; c[kSrcPlatoon].z = 20.0f;
    if (Pick(c) != kSrcPlatoon) Fail("the platoon's own position wins when it is one");
    c[kSrcMember].have = 1;
    if (Pick(c) != kSrcMember) Fail("a living member's position wins whenever one was read, even at 0,0");
    c[kSrcMember].x = 1.0f;
    if (Pick(c) != kSrcMember) Fail("a living member's position wins");
    for (i = 0; i < 5; ++i) { c[i].have = 0; c[i].x = 11.0f; c[i].z = 12.0f; }
    if (Pick(c) != kSrcNone) Fail("a position not read does not count");
    c[kSrcLast].have = 1;
    if (Pick(c) != kSrcLast) Fail("only the last record read: it wins");
    if (std::string(SrcText(kSrcLast)) != "kept the last record's" || std::string(SrcText(kSrcHome)) != "used its home building's"
        || std::string(SrcText(kSrcTown)) != "used its town's" || std::string(SrcText(kSrcNone)) != "none") Fail("the log words");
}
void t_t599_pace_order_through_held()
{
    using namespace spawnpace;
    const unsigned int X = 4194308u, Y = 4194359u, Z = 4194400u;
    std::vector<unsigned int> none, onlyX(1, X), onlyY(1, Y), onlyZ(1, Z), xy, xz;
    xy.push_back(X); xy.push_back(Y); xz.push_back(X); xz.push_back(Z);
    HeldBook a;
    HeldPutRow(&a, kKindSpawn, kScopeNone, 1u, onlyY, 0, 0);
    if (RouteNames(a, 0, xy, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteFollow) Fail("(a) a message naming X and the held Y follows Y");
    HeldPutRow(&a, kKindFollow, kScopeNone, 1u, xy, 1, 0);
    if (RouteNames(a, 0, onlyX, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteFollow) Fail("(a) a later message naming only X waits behind it");
    if (RouteNames(a, 1, onlyX, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteHoldSpawn) Fail("(a) a SPAWN making X's copy behind it is held as a SPAWN");
    if (RouteNames(a, 0, onlyZ, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteApply) Fail("(a) a message naming nothing held applies");
    HeldTake(&a, 0); HeldTake(&a, 0);
    if (!T599Empty(a)) Fail("(a) the counts return to nothing");
    HeldBook b;
    HeldPutRow(&b, kKindSpawn, kScopeNone, 2u, onlyZ, 0, 0);
    HeldPutRow(&b, kKindSpawn, kScopeNone, 1u, onlyY, 1, 0);
    HeldPutRow(&b, kKindFollow, kScopeNone, 1u, xy, 2, 0);
    HeldPutRow(&b, kKindFollow, kScopeNone, 2u, xz, 3, 0);
    std::vector<long long> got;
    T599Frame(&b, kFrameBudgetUs - 1000, 0, &got);
    if (got.size() != 1 || got[0] != 0) Fail("(b) Z is made, Y kept for the budget, and the message naming X and Z waits behind the one naming X and Y");
    got.clear(); T599Frame(&b, 0, 0, &got);
    if (got.size() != 3 || got[0] != 1 || got[1] != 2 || got[2] != 3) Fail("(b) then Y and the two messages in arrival order");
    if (!T599Empty(b)) Fail("(b) the counts return to nothing");
    HeldBook c;
    std::vector<unsigned int> s500(1, 500u);
    HeldPutRow(&c, kKindSpawn, kScopeNone, 1u, s500, 0, 0);
    if (RouteNames(c, 0, none, kScopeSender, 1u, 0, 0, kFrameBudgetUs) != kRouteBarrier) Fail("(c) a sender-scoped message waits for its sender's SPAWN");
    HeldPutRow(&c, kKindBarrier, kScopeSender, 1u, none, 1, 0);
    if (RouteNames(c, 0, onlyX, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteFollow) Fail("(c) that sender's later message naming one character waits behind it");
    if (RouteNames(c, 0, none, kScopeNone, 1u, 0, 0, kFrameBudgetUs) != kRouteFollow) Fail("(c) ...and one naming none");
    if (RouteNames(c, 0, onlyX, kScopeNone, 2u, 0, 0, kFrameBudgetUs) != kRouteApply) Fail("(c) another sender's message applies");
    if (RouteNames(c, 0, onlyX, kScopeNone, kNoSender, 0, 0, kFrameBudgetUs) != kRouteApply) Fail("(c) a message on no game's road applies");
    HeldPutRow(&c, kKindFollow, kScopeNone, 1u, onlyX, 2, 0);
    HeldPutRow(&c, kKindFollow, kScopeNone, 1u, none, 3, 0);
    got.clear(); T599Frame(&c, kFrameBudgetUs, 1, &got);
    if (!got.empty()) Fail("(c) with the sender's SPAWN kept, its sender-scoped row and the messages behind it are kept");
    got.clear(); T599Frame(&c, 0, 0, &got);
    if (got.size() != 4 || got[0] != 0 || got[1] != 1 || got[2] != 2 || got[3] != 3) Fail("(c) then all four in arrival order");
    if (!T599Empty(c)) Fail("(c) the counts return to nothing");
    HeldBook e;
    HeldPutRow(&e, kKindBarrier, kScopeSender, 1u, none, 0, 0);
    if (RouteNames(e, 0, none, kScopeSender, 1u, 0, 0, kFrameBudgetUs) != kRouteBarrier) Fail("(c) a sender-scoped message waits behind its sender's held sender-scoped row");
    HeldTake(&e, 0);
    if (!T599Empty(e)) Fail("(c) HeldTake forgets the sender-scoped row");
}

/* the mod's version: the name is major.minor.patch of its numbers, and the Workshop upload number reads back as the version */
void t_t700_mod_version()
{
    char b[32]; _snprintf_s(b, sizeof(b), _TRUNCATE, "%d.%d.%d", modversion::kMajor, modversion::kMinor, modversion::kPatch);
    if (std::string(b) != modversion::kName) Fail("the version name is not major.minor.patch of its numbers");
    if (modversion::UploadNumber() != modversion::kMajor * 10000 + modversion::kMinor * 100 + modversion::kPatch) Fail("upload number formula");
    if (modversion::kMinor > 99 || modversion::kPatch > 99) Fail("minor and patch must stay below 100 for the upload number to read back");
}

/* the world-server link's carry (src/common/pumpcarry.h): a relayed MOVE for a (slot, uid) whose earlier MOVE still waits
   overwrites that one in its own place; a MOVE for another uid or slot, and any other message, is appended in arrival order */
struct T599CMsg { int tag; std::vector<char> payload; T599CMsg() : tag(0) {} };
T599CMsg T599CLive(int tag, int slot, unsigned int innerType, unsigned int uid, char mark)
{
    T599CMsg m; m.tag = tag;
    std::vector<char> inner(16, mark);   /* the uid, then a position-sized body */
    std::memcpy(&inner[0], &uid, 4);
    if (!cooplive::LiveDownEncode(&m.payload, slot, innerType, &inner[0], inner.size())) Fail("could not build a LIVE envelope");
    return m;
}
bool T599CAppend(pumpcarry::Carry<T599CMsg>* c, T599CMsg m)
{
    pumpcarry::Key k;
    const bool level = pumpcarry::LiveLevelKey(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), &k);
    return c->Append(&m, level, k);
}
void t_t599_carry_collapse_in_place()
{
    pumpcarry::Carry<T599CMsg> c;
    const unsigned int mv = cooplive::kInnerMove;
    if (T599CAppend(&c, T599CLive(1, 2, mv, 7u, 'a'))) Fail("the first MOVE collapsed into nothing");
    if (T599CAppend(&c, T599CLive(2, 2, cooplive::kInnerHit, 7u, 'h'))) Fail("an event message collapsed");
    if (!T599CAppend(&c, T599CLive(3, 2, mv, 7u, 'b'))) Fail("a newer MOVE for the same slot and uid did not take the waiting one's place");
    if (c.Size() != 2) Fail("a collapsed MOVE was appended too");
    if (c.At(0).tag != 1 || c.At(1).tag != 2) Fail("the MOVE did not keep its earlier slot ahead of the event message");
    if (c.At(0).payload != T599CLive(3, 2, mv, 7u, 'b').payload) Fail("the waiting MOVE does not carry the newer position");
    if (c.Collapsed() != 1) Fail("the collapse was not counted");
    if (T599CAppend(&c, T599CLive(4, 2, mv, 8u, 'c'))) Fail("a MOVE for another uid collapsed");
    if (T599CAppend(&c, T599CLive(5, 3, mv, 7u, 'd'))) Fail("a MOVE for the same uid from another slot collapsed");
    if (T599CAppend(&c, T599CLive(6, 2, mv, 0u, 'e')) || T599CAppend(&c, T599CLive(7, 2, mv, 0u, 'f'))) Fail("a MOVE with uid 0 collapsed");
    if (c.Size() != 6 || c.At(2).tag != 4 || c.At(3).tag != 5 || c.At(4).tag != 6 || c.At(5).tag != 7) Fail("appended messages are not in arrival order");
    T599CMsg odd; odd.tag = 9; odd.payload.assign(3, 'x');
    if (T599CAppend(&c, odd) || c.At(c.Size() - 1).tag != 9) Fail("a message that is not a LIVE envelope did not keep its own place");
    if (c.MaxSize() != 7) Fail("carryMax does not follow the largest size");
}
/* after a pop or a drop the index no longer points at the message that left, so a later MOVE is appended, not lost */
void t_t599_carry_pops_forgotten()
{
    pumpcarry::Carry<T599CMsg> c;
    const unsigned int mv = cooplive::kInnerMove;
    const pumpcarry::Key k7(2, mv, 7u);
    T599CAppend(&c, T599CLive(1, 2, mv, 7u, 'a'));
    T599CAppend(&c, T599CLive(2, 2, cooplive::kInnerHit, 7u, 'h'));
    T599CMsg out;
    if (!c.PopFront(&out) || out.tag != 1) Fail("PopFront did not hand back the front message");
    if (out.payload != T599CLive(1, 2, mv, 7u, 'a').payload) Fail("the popped message lost its payload");
    if (c.IndexOf(k7) != -1) Fail("the index still points at a popped MOVE");
    if (T599CAppend(&c, T599CLive(3, 2, mv, 7u, 'b'))) Fail("a MOVE collapsed into one already handled");
    if (c.Size() != 2 || c.At(0).tag != 2 || c.At(1).tag != 3) Fail("the new MOVE was not appended behind the event message");
    if (c.IndexOf(k7) != 1) Fail("the index does not point at the new MOVE");
    c.Clear();
    if (c.Size() != 0 || c.IndexOf(k7) != -1) Fail("Clear left a waiting entry behind");
    if (T599CAppend(&c, T599CLive(4, 2, mv, 7u, 'c'))) Fail("a MOVE collapsed into a dropped one");
    if (c.IndexOf(k7) != 0) Fail("after a drop the index does not point at the new front");
    if (!c.PopFront(&out) || out.tag != 4 || c.PopFront(&out)) Fail("PopFront on the last message / on an empty carry");
}
/* after a pop, a relayed MOVE whose earlier MOVE waits away from the front collapses into that one's place: the position
   kept for it is absolute, so the base the pop moved is subtracted to find it */
void t_t599_carry_collapse_after_pop()
{
    pumpcarry::Carry<T599CMsg> c;
    const unsigned int mv = cooplive::kInnerMove;
    const int s = 4;
    T599CAppend(&c, T599CLive(1, s, cooplive::kInnerHit, 5u, 'A'));
    T599CAppend(&c, T599CLive(2, s, cooplive::kInnerHit, 6u, 'B'));
    T599CMsg out;
    if (!c.PopFront(&out) || out.tag != 1) Fail("PopFront did not hand back A");
    if (T599CAppend(&c, T599CLive(3, s, mv, 8u, 'm'))) Fail("the first MOVE collapsed into nothing");
    if (c.Size() != 2 || c.At(1).tag != 3) Fail("the MOVE was not appended behind B");
    if (!T599CAppend(&c, T599CLive(4, s, mv, 8u, 'n'))) Fail("a newer MOVE for the same slot and uid did not collapse after a pop");
    if (c.Size() != 2) Fail("the collapsed MOVE changed the carry's size");
    if (c.At(0).tag != 2 || c.At(1).tag != 3) Fail("the collapse did not land on the waiting MOVE's own place");
    if (c.At(1).payload != T599CLive(4, s, mv, 8u, 'n').payload) Fail("the waiting MOVE does not carry the newer position");
    if (c.At(0).payload != T599CLive(2, s, cooplive::kInnerHit, 6u, 'B').payload) Fail("the collapse overwrote B");
}
/* where one PumpLink call stops: a test cap ends it at the cap whatever the time; otherwise every call handles 64, then
   stops once the time budget is spent or the arrival queue is near full while engine writes are blocked */
void t_t599_pump_stop_rule()
{
    if (pumpcarry::Stop(0, 1, 0, 2000, false)) Fail("cap 1 stopped before its first message");
    if (!pumpcarry::Stop(1, 1, 0, 2000, false)) Fail("cap 1 did not stop after one message");
    if (!pumpcarry::Stop(1, 1, 0, 2000, true) || pumpcarry::Stop(0, 1, 99999, 2000, true)) Fail("cap 1 is not the cap whatever the time or saturation");
    if (pumpcarry::Stop(63, 0, 99999, 2000, true)) Fail("cap 0 stopped below 64 messages");
    if (pumpcarry::Stop(0, 0, 99999, 2000, true)) Fail("cap 0 stopped before its first message");
    if (pumpcarry::Stop(64, 0, 1999, 2000, false)) Fail("at 64 with time left and no saturation the call stopped");
    if (!pumpcarry::Stop(64, 0, 2000, 2000, false)) Fail("at 64 with the budget spent the call did not stop");
    if (!pumpcarry::Stop(64, 0, 0, 2000, true)) Fail("at 64 saturated and blocked the call did not stop");
    if (!pumpcarry::Stop(200, 0, 0, 2000, true) || pumpcarry::Stop(200, 0, 10, 2000, false)) Fail("past 64 the rule is not time or saturation");
    if (pumpcarry::Stop(64, -1, 0, 2000, false)) Fail("a negative cap is not the normal rule");
}
/* PumpLink appends what it polls at the back of the carry and takes from the front until pumpcarry::Stop says the call
   ends; it does not copy the carry in front of the new messages and back */
void t_t599_pumplink_no_front_insert()
{
    const std::string src = T313ReadFile("../coop-plugin/store.cpp");
    if (src.empty()) { Fail("could not read store.cpp"); return; }
    const size_t a = src.find("\nvoid PumpLink()\r\n{") != std::string::npos ? src.find("\nvoid PumpLink()\r\n{") : src.find("\nvoid PumpLink()\n{");   /* the definition, not the declaration */
    const size_t b = (a == std::string::npos) ? a : src.find("\n}", a + 1);
    if (a == std::string::npos || b == std::string::npos) { Fail("PumpLink not found in store.cpp"); return; }
    const std::string body = src.substr(a, b - a);
    if (body.find("in.insert(") != std::string::npos) Fail("PumpLink still inserts into the polled messages");
    if (body.find("g_linkCarry.assign(") != std::string::npos) Fail("PumpLink still copies the tail back into the carry");
    if (body.find("g_linkCarry.Append(") == std::string::npos) Fail("PumpLink does not append polled messages to the carry");
    if (body.find("g_linkCarry.PopFront(") == std::string::npos) Fail("PumpLink does not take from the front of the carry");
    if (body.find("pumpcarry::Stop(handled, g_pumpCapTest, QpcUsSince(pumpT0), kPumpBudgetUs, satBlocked)") == std::string::npos) Fail("PumpLink does not stop on the shared rule with its time budget");
    {
        const std::string brake = "(InQueueSaturated(kOriginNotebook) != 0 || InQueueSaturated(kOriginRelay) != 0) && EngineWritesBlocked()";
        /* the brake before polling is PumpLinkHeld(), shared with the save-time link keeper; the drain applies the same test inline */
        const size_t h = src.find("\nbool PumpLinkHeld()");
        const size_t hEnd = (h == std::string::npos) ? h : src.find("\n}", h + 1);
        const std::string held = (h == std::string::npos || hEnd == std::string::npos) ? std::string() : src.substr(h, hEnd - h);
        if (held.find(brake) == std::string::npos) Fail("PumpLinkHeld does not test the saturated-and-blocked brake");
        if (body.find("if (PumpLinkHeld()) return;") == std::string::npos) Fail("PumpLink does not apply the brake before polling");
        if (body.find(brake) == std::string::npos) Fail("PumpLink's drain does not apply the saturated-and-blocked brake it applies before polling");
    }
    if (body.find("if (g_link == 0) break;") == std::string::npos) Fail("PumpLink does not stop when the link went away");
}
/* THE WORLD'S PASSWORD (src/common/authproof.h): SHA-256, HMAC-SHA256 (RFC 4231 cases 1 and 2) and PBKDF2-HMAC-SHA256 against
   published vectors, then the key, the proof and the three payloads. */
void t_t290s3a_sha_hmac_pbkdf2()
{
    unsigned char out[32];
    authproof::Sha256 s; authproof::Sha256Init(&s); authproof::Sha256Update(&s, "abc", 3); authproof::Sha256Final(&s, out);
    if (authproof::Hex(out, 32) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") Fail("SHA-256 of abc");
    unsigned char k1[20]; std::memset(k1, 0x0b, sizeof(k1));
    authproof::HmacSha256(k1, sizeof(k1), "Hi There", 8, out);
    if (authproof::Hex(out, 32) != "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") Fail("HMAC-SHA256 RFC 4231 case 1");
    authproof::HmacSha256("Jefe", 4, "what do ya want for nothing?", 28, out);
    if (authproof::Hex(out, 32) != "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") Fail("HMAC-SHA256 RFC 4231 case 2");
    unsigned char k6[131]; std::memset(k6, 0xaa, sizeof(k6));
    authproof::HmacSha256(k6, sizeof(k6), "Test Using Larger Than Block-Size Key - Hash Key First", 54, out);
    if (authproof::Hex(out, 32) != "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") Fail("HMAC-SHA256 RFC 4231 case 6 (a 131-byte key)");
    authproof::Pbkdf2Sha256("password", 8, "salt", 4, 1, out, 32);
    if (authproof::Hex(out, 32) != "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b") Fail("PBKDF2-HMAC-SHA256 password/salt, 1 iteration");
    authproof::Pbkdf2Sha256("password", 8, "salt", 4, 4096, out, 32);
    if (authproof::Hex(out, 32) != "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a") Fail("PBKDF2-HMAC-SHA256 password/salt, 4096 iterations");
}

/* access.txt: every mode x Steam / address joiner x listed / blocked / owner */
void t_t290s3a_access_decide()
{
    const unsigned long long A = 76561197960287930ULL, B = 76561197960287931ULL, C = 76561197960287932ULL;
    accessrule::Rules open = accessrule::Parse("mode open\nallow " + accessrule::IdText(A) + " friend\nblock " + accessrule::IdText(B) + " griefer\n");
    accessrule::Rules allow = accessrule::Parse("mode allowlist\nallow " + accessrule::IdText(A) + " friend\nblock " + accessrule::IdText(B) + " griefer\n");
    struct Row { const accessrule::Rules* r; bool steam; unsigned long long id; bool owner; int want; int entry; const char* what; };
    const Row rows[] = {
        { &open,  true,  A, false, accessrule::kAdmit,      0, "open: a listed Steam joiner" },
        { &open,  true,  C, false, accessrule::kAdmit,      0, "open: an unlisted Steam joiner" },
        { &open,  true,  B, false, accessrule::kBlocked,    2, "open: a blocked Steam joiner" },
        { &open,  true,  B, true,  accessrule::kAdmit,      0, "open: the owner is never blocked (Decide asks it first; on the wire it has no Steam id)" },
        { &open,  false, 0, false, accessrule::kAdmit,      0, "open: an address joiner" },
        { &open,  false, 0, true,  accessrule::kAdmit,      0, "open: the owner" },
        { &allow, true,  A, false, accessrule::kAdmit,      1, "allowlist: a listed Steam joiner" },
        { &allow, true,  C, false, accessrule::kNotAllowed, 0, "allowlist: an unlisted Steam joiner" },
        { &allow, true,  B, false, accessrule::kBlocked,    2, "allowlist: a blocked Steam joiner" },
        { &allow, true,  B, true,  accessrule::kAdmit,      0, "allowlist: the owner is never blocked (Decide asks it first; on the wire it has no Steam id)" },
        { &allow, false, 0, false, accessrule::kNotAllowed, 0, "allowlist: an address joiner" },
        { &allow, false, 0, true,  accessrule::kAdmit,      0, "allowlist: the owner" },
        { &allow, true,  C, true,  accessrule::kAdmit,      0, "allowlist: the owner whatever id is passed" } };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        int entry = -1;
        const int d = accessrule::Decide(*rows[i].r, rows[i].steam, rows[i].id, rows[i].owner, &entry);
        if (d != rows[i].want || entry != rows[i].entry) Fail(std::string("access decision: ") + rows[i].what);
    }
    accessrule::Rules none;   /* no access.txt */
    if (none.mode != accessrule::kModeOpen || accessrule::Decide(none, false, 0, false, 0) != accessrule::kAdmit) Fail("no access.txt is not open");
}

void t_t290s3a_access_parse()
{
    const std::string text = "# who may play\nmode allowlist\nallow 76561197960287930 Sam from work\n  block 76561197960287931   # no note\n"
                             "allow 0 zero\nallow 12x4 letters\nmode closed\nfrobnicate 1\nblock\n\n";
    const accessrule::Rules r = accessrule::Parse(text);
    if (r.mode != accessrule::kModeAllowlist || r.entries.size() != 2 || r.badLines != 5) Fail("access.txt parse: mode, entries, bad lines");
    else
    {
        if (r.entries[0].kind != accessrule::kKindAllow || r.entries[0].id != 76561197960287930ULL || r.entries[0].note != "Sam from work") Fail("access.txt: the allow entry and its note");
        if (r.entries[1].kind != accessrule::kKindBlock || r.entries[1].id != 76561197960287931ULL || !r.entries[1].note.empty()) Fail("access.txt: the block entry");
    }
    const accessrule::Rules back = accessrule::Parse(accessrule::Text(r));
    if (back.mode != r.mode || back.entries.size() != r.entries.size() || back.badLines != 0 || accessrule::Text(back) != accessrule::Text(r)) Fail("access.txt round trip");
    int entry = -1;
    accessrule::Rules t = accessrule::TestApply(r, accessrule::kTestModeOpen, &entry);
    if (t.mode != accessrule::kModeOpen || t.entries.size() != 2 || entry != 0) Fail("accesstest mode open");
    t = accessrule::TestApply(t, accessrule::kTestModeAllowlist, &entry);
    if (t.mode != accessrule::kModeAllowlist || t.entries.size() != 2) Fail("accesstest mode allowlist");
    t = accessrule::TestApply(t, accessrule::kTestClear, &entry);
    if (t.mode != accessrule::kModeOpen || !t.entries.empty()) Fail("accesstest clear");
    unsigned long long id = 0;
    if (!accessrule::ParseId("18446744073709551615", &id) || accessrule::ParseId("18446744073709551616", &id) || accessrule::ParseId("", &id)) Fail("the id's range");
}

/* the wrong-password wait: 4 wrong are only counted, the 5th locks for 60 s from itself, a right password clears the count */
void t_t290s3a_lockout()
{
    authlock::Table t;
    for (int i = 1; i <= 4; ++i)
    {
        if (authlock::NoteWrong(&t, "steam:1", 100.0 + i)) Fail("a wrong password before the 5th locks");
        if (authlock::IsLocked(&t, "steam:1", 100.0 + i)) Fail("locked before the 5th wrong password");
    }
    if (!authlock::NoteWrong(&t, "steam:1", 110.0)) Fail("the 5th wrong password does not lock");
    if (!authlock::IsLocked(&t, "steam:1", 110.0) || !authlock::IsLocked(&t, "steam:1", 169.9)) Fail("not locked inside the minute");
    if (authlock::IsLocked(&t, "addr:10.0.0.2", 120.0)) Fail("another player is locked too");
    if (authlock::IsLocked(&t, "steam:1", 170.0)) Fail("still locked 60 s after the 5th");
    if (authlock::NoteWrong(&t, "steam:1", 171.0)) Fail("the count did not start again after the lock");
    for (int i = 0; i < 4; ++i) authlock::NoteWrong(&t, "addr:10.0.0.2", 200.0);
    authlock::NoteRight(&t, "addr:10.0.0.2");
    if (authlock::NoteWrong(&t, "addr:10.0.0.2", 201.0) || authlock::IsLocked(&t, "addr:10.0.0.2", 201.0)) Fail("a right password did not clear the count");
    authlock::NoteWrong(&t, "x", 0.0); authlock::NoteWrong(&t, "x", 0.0); authlock::NoteWrong(&t, "x", 0.0); authlock::NoteWrong(&t, "x", 0.0); authlock::NoteWrong(&t, "x", 0.0);
    authlock::Expire(&t, 61.0);
    if (t.count("x") != 0) Fail("an expired lock is kept");
}

/* STEAM_ID is believed only with this hosting's secret, as the first message, alone, once */
void t_t290s3a_steam_id_trust()
{
    /* STEAM_ID: believed with the secret as the first message alone; later (or in a bundle, or again) closes; without it ignored.
       HOST_TOKEN: the owner only with the secret, off the bridge, before the HELLO. The address is an input to neither. */
    for (int m = 0; m < 16; ++m)
    {
        const bool token = (m & 1) != 0, first = (m & 2) != 0, inBundle = (m & 4) != 0, already = (m & 8) != 0;
        const int want = (!first || inBundle || already) ? (int)accessrule::kSteamIdLate : token ? (int)accessrule::kSteamIdBelieve : (int)accessrule::kSteamIdWrongToken;
        if (accessrule::SteamIdJudge(token, first, inBundle, already) != want) Fail("the STEAM_ID trust rule");
    }
    for (int m = 0; m < 8; ++m)
    {
        const bool token = (m & 1) != 0, bridged = (m & 2) != 0, afterHello = (m & 4) != 0;
        if (accessrule::HostTokenProves(token, bridged, afterHello) != (token && !bridged && !afterHello)) Fail("the HOST_TOKEN rule");
    }
}

/* refusal reasons 8-14: their words, they stop the join's re-dial, and a reason this build does not know is the protocol one */
void t_t290s3a_refusal_words()
{
    if (coopstore::StoreRefuseReasonText(coopstore::kRefusePasswordNeeded, 92u, 92u) != "the world needs a password") Fail("reason 8's words");
    if (coopstore::StoreRefuseReasonText(coopstore::kRefuseWrongPassword, 92u, 92u) != "the password was wrong") Fail("reason 9's words");
    if (coopstore::StoreRefuseReasonText(coopstore::kRefuseNotAllowed, 92u, 92u) != "this world does not let you in (the host's access list)") Fail("reason 10's words");
    if (coopstore::StoreRefuseReasonText(coopstore::kRefuseTooManyWrong, 92u, 92u) != "too many wrong passwords - wait a minute") Fail("reason 11's words");
    if (coopstore::StoreRefuseReasonText(coopstore::kRefuseAuthFileBad, 92u, 92u) != "the world's password file is damaged - the host must host again") Fail("reason 12's words");
    if ((int)coopstore::kRefuseAuthFileBad != 12) Fail("reason 12's number");
    if ((int)coopstore::kRefuseNotOnList != 13
        || coopstore::StoreRefuseReasonText(coopstore::kRefuseNotOnList, 92u, 92u) != "this world only lets in players the host has added (the host's access list)")
        Fail("reason 13's number and words");
    if ((int)coopstore::kRefuseAccessUnreadable != 14
        || coopstore::StoreRefuseReasonText(coopstore::kRefuseAccessUnreadable, 92u, 92u) != "the host's player list could not be read - the host must host again")
        Fail("reason 14's number and words");
    if ((int)coopstore::kRefusePasswordNeeded != 8 || (int)coopstore::kRefuseWrongPassword != 9 || (int)coopstore::kRefuseNotAllowed != 10 || (int)coopstore::kRefuseTooManyWrong != 11) Fail("the reason numbers");
    for (unsigned int r = 0; r < 16; ++r)
        if (coopstore::StoreRefuseStopsJoin(r) != (r >= 8 && r <= 14)) Fail("which refusals stop the join's re-dial");
    if (coopstore::StoreRefuseReasonText(15u, 92u, 91u) != coopstore::StoreRefuseReasonText(coopstore::kRefuseProtocol, 92u, 91u)) Fail("an unknown reason is not read as the protocol refusal");
    if (coopstore::StoreHandshakeDecide(95u, 94u) != coopstore::kHandshakeRefuse || coopstore::StoreHandshakeDecide(95u, 95u) != coopstore::kHandshakeAccept) Fail("a game of protocol 94 is not refused by a world server of 95");
}

/* T-290 - the password on screen: the CAN'T JOIN / CAN'T HOST sentences for 8-13 (none is the version sentence), which refusals show
   PASSWORD REQUIRED in place of the box, that screen's rows and title, HOST GAME's PASSWORD row and the driver's names. */
void t_t290s3b_password_screen()
{
    const std::string version = coopui::PanelRefusalText(coopstore::kRefuseProtocol, "");
    static const struct { int r; const char* text; } kWords[] = {
        { 8, "This game needs a password." }, { 9, "Wrong password." },
        { 10, "The host has blocked you from this world." },
        { 11, "Too many wrong passwords. Joining is blocked for a minute." },
        { 12, "Couldn't check the password." },
        { 13, "This world only lets in players the host has added." },
        { 14, "Couldn't check the host's player list." } };
    for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i)
        for (int host = 0; host <= 1; ++host)
        {
            const std::string s = coopui::PanelRefusalText(kWords[i].r, "", host);
            if (s != kWords[i].text || s == version)
                Fail("T-290: refusal " + I(kWords[i].r) + (host != 0 ? " under CAN'T HOST" : " under CAN'T JOIN") + " -> '" + s + "'");
        }
    if (coopui::PanelRefusalText(15, "") != version) Fail("T-290: a reason this build does not know is not the version sentence");
    if ((int)coopstore::kRefusePasswordNeeded != 8 || (int)coopstore::kRefuseWrongPassword != 9 || (int)coopstore::kRefuseNotAllowed != 10
        || (int)coopstore::kRefuseTooManyWrong != 11 || (int)coopstore::kRefuseAuthFileBad != 12 || (int)coopstore::kRefuseNotOnList != 13
        || (int)coopstore::kRefuseAccessUnreadable != 14)
        Fail("T-290: the reason numbers PanelRefusalText and PressPasswordAsk use");
    /* PASSWORD REQUIRED in place of the box: a JOIN refused with 8 (its opening line) or 9 (the wrong-password line), never a HOST press */
    for (int r = -1; r <= 14; ++r)
    {
        const int want = (r == 8) ? 1 : (r == 9) ? 2 : 0;
        if (coopui::PressPasswordAsk(1, r) != want || coopui::PressPasswordAsk(0, r) != 0) Fail("T-290: PressPasswordAsk for refusal " + I(r));
    }
    if (std::string(coopui::PressPasswordLine(1)) != "This game needs a password." || std::string(coopui::PressPasswordLine(2)) != "Wrong password.")
        Fail("T-290: PASSWORD REQUIRED's lines");
    {
        /* the press's verdict for a JOIN refused with 8 is still a failure with its sentence (ui.cpp shows the screen in its place) */
        coopui::PressFacts j;
        j.mode = 1; j.armed = 1; j.refusal = 8;
        std::string box;
        if (coopui::PressVerdictOf(j, "", &box) != coopui::kPressFailed || box != "This game needs a password.") Fail("T-290: a JOIN refused with 8 -> '" + box + "'");
    }
    if (coopui::PanelLayoutOf(8, 0) != coopui::kLayPassword || coopui::PanelScreenTitle(8, 0, 0, "", "") != "PASSWORD REQUIRED")
        Fail("T-290: PASSWORD REQUIRED's layout / title");
    {
        int na = 0, nb = 0;
        const coopui::PanelSlot* a = coopui::PanelRowsOf(coopui::kLayPassword, &na);
        const coopui::PanelSlot* b = coopui::PanelRowsOf(coopui::kLayNewWorld, &nb);
        if (a != b || na != nb) Fail("T-290: PASSWORD REQUIRED is not built like NEW WORLD");
    }
    /* HOST GAME's PASSWORD row on both its layouts: straight under the worlds list's buttons, its line straight under it and PORT
       straight under that; PORT's height; the label level with the box; the line one line at its width (1280x720 and 800x600) */
    static const int kAt[2][2] = { { 1280, 720 }, { 800, 600 } };
    for (int k = 0; k < 2; ++k)
    {
        const coopui::PanelGeom g = coopui::PanelGeomIn(kAt[k][0], kAt[k][1]);
        const int gap = coopui::PanelGapOf(g.rowH);
        const int la[] = { coopui::kLayHost, coopui::kLayHostHint };
        for (int m = 0; m < 2; ++m)
        {
            const coopui::PanelBand kb = coopui::PanelSlotBand(la[m], coopui::kSlotListBtns, g.rowH);
            const coopui::PanelBand pb = coopui::PanelSlotBand(la[m], coopui::kSlotPwRow, g.rowH);
            const coopui::PanelBand ob = coopui::PanelSlotBand(la[m], coopui::kSlotPortRow, g.rowH);
            const std::string where = "layout " + I(la[m]) + " at " + I(kAt[k][0]) + "x" + I(kAt[k][1]);
            if (pb.h <= 0 || pb.top != kb.top + kb.h || ob.top != pb.top + pb.h)
                Fail("T-290: the PASSWORD row is not between the list's buttons and PORT on " + where);
            if (pb.h != ob.h) Fail("T-290: the PASSWORD row is not PORT's height on " + where);
            const coopui::PanelPairRect p = coopui::PanelLabelBoxIn(g.innerW, gap, pb, 0, coopui::PanelSideBtnW(g.innerW) + gap);
            if (2 * p.top + p.h != 2 * pb.top + pb.h || p.boxW < coopui::kPanelMinFieldW) Fail("T-290: PASSWORD's label / box on " + where);
        }
    }
    if (coopui::PanelLayoutHalves(coopui::kLayHost) != coopui::kPanelMaxHalves - 2 || coopui::PanelLayoutHalves(coopui::kLayHostHint) != coopui::kPanelMaxHalves - 2)
        Fail("T-290: HOST GAME is not twelve rows at its ceilings (no line under PASSWORD)");
    {
        /* the driver's names: SHOW / HIDE, PASSWORD REQUIRED's JOIN and CANCEL, and the two boxes, whose typed text is never logged */
        const coopui::UiDriveName* s = coopui::UiDriveFind("hostpwshow", coopui::kDriveButton);
        const coopui::UiDriveName* j = coopui::UiDriveFind("pwjoin", coopui::kDriveButton);
        const coopui::UiDriveName* c = coopui::UiDriveFind("pwcancel", coopui::kDriveButton);
        const coopui::UiDriveName* e = coopui::UiDriveFind("password", coopui::kDriveEdit);
        const coopui::UiDriveName* h = coopui::UiDriveFind("hostpassword", coopui::kDriveEdit);
        if (s == 0 || j == 0 || c == 0 || e == 0 || h == 0 || std::string(s->w1) != "SWHostPwShowBtn" || std::string(j->w1) != "SWPwJoinBtn"
            || std::string(c->w1) != "SWPwCancelBtn" || std::string(e->w1) != "SWPwEdit" || std::string(h->w1) != "SWHostPwEdit")
            Fail("T-290: the driver's PASSWORD names");
        if (coopui::UiDriveTypedHidden("password") != 1 || coopui::UiDriveTypedHidden("hostpassword") != 1 || coopui::UiDriveTypedHidden("name") != 0)
            Fail("T-290: which typed boxes the log hides");
    }
    if (coopui::UiDriveArgsForLog("uitype", "password se#cret") != "password <hidden>" || coopui::UiDriveArgsForLog("uitype", "hostpassword x") != "hostpassword <hidden>"
        || coopui::UiDriveArgsForLog("uitype", "name Scott") != "name Scott" || coopui::UiDriveArgsForLog("uiclick", "pwjoin") != "pwjoin")
        Fail("T-290: the driver's arguments in a log line");
    {
        /* a password survives its box: written with '#' doubled, read without MyGUI's tags */
        const char* const pws[] = { "", "plain", "a#b", "##", "#FF0000", "x#ffffffy", "#" };
        for (size_t i = 0; i < sizeof(pws) / sizeof(pws[0]); ++i)
            if (coopbug::CaptionToPlain(coopui::PanelPwCaption(pws[i])) != pws[i]) Fail(std::string("T-290: a password does not survive its box: ") + pws[i]);
    }
}

/* wrong-password counts are forgotten 10 minutes after the last wrong one; a lock still ends 60 s after the 5th */
void t_t290s3a_lock_forget()
{
    authlock::Table t;
    for (int i = 0; i < 4; ++i) authlock::NoteWrong(&t, "steam:1", 1000.0 + i);   /* the last at 1003 */
    if (authlock::NoteWrong(&t, "steam:1", 1003.0 + authlock::kForgetSec)) Fail("four wrong passwords 10 minutes old still count toward the wait");
    for (int i = 1; i <= 3; ++i) if (authlock::NoteWrong(&t, "steam:1", 1610.0 + i)) Fail("a lock before the 5th wrong password after the count was forgotten");
    if (!authlock::NoteWrong(&t, "steam:1", 1620.0) || !authlock::IsLocked(&t, "steam:1", 1620.0)) Fail("5 wrong in a row after the forgotten ones do not lock");
    authlock::NoteWrong(&t, "host", 0.0); authlock::NoteWrong(&t, "host", 0.0);
    authlock::Expire(&t, authlock::kForgetSec - 1.0);
    if (t.count("host") != 1) Fail("a count younger than 10 minutes is dropped");
    authlock::Expire(&t, authlock::kForgetSec);
    if (t.count("host") != 0) Fail("a count 10 minutes old is kept");
    if (authlock::kForgetSec != 600.0) Fail("the forget time is not 10 minutes");
}

/* a world's export leaves its password key behind; its access list travels */
void t_t290s3a_export_auth()
{
    if (worldkeep::ExportTakes("auth.txt", 0) || worldkeep::ExportTakes("AUTH.TXT", 0)) Fail("auth.txt travels with an export");
    if (!worldkeep::ExportTakes("access.txt", 0)) Fail("access.txt is left out of an export");
    if (!worldkeep::ExportTakes("repair/7/auth.txt", 0)) Fail("a file named auth.txt below the top is left out");
}

/* T-424 / T-472 (src/common/floorplace.h): a copy on another floor than its stopped owner is placed at the owner's height. */
void t_t424_floor_place_gap()
{
    using namespace floorplace;
    if (FloorPlace(true, true, true, false, 0.5f, 19.9f, 0) != kFloorNone) Fail("floor: a 19.9 height gap must not place");
    if (FloorPlace(true, true, true, false, 0.5f, 20.1f, 0) != kFloorPlace) Fail("floor: a 20.1 height gap must place");
    if (FloorPlace(true, true, true, false, 0.5f, -20.1f, 0) != kFloorPlace) Fail("floor: a copy 20.1 above its owner must be placed too");
    if (FloorPlace(false, true, true, true, 0.5f, 63.0f, 0) != kFloorNone) Fail("floor: an owner moving must never place");
    if (FloorPlace(false, false, true, false, 0.5f, 63.0f, 0) != kFloorNone) Fail("floor: an owner moving with no stop held must never place");
    if (FloorPlace(true, true, true, false, 0.0f, 63.0f, 0) != kFloorPlace) Fail("floor: a copy arrived flat right under its owner (63 up) must be placed");
    if (FloorPlace(true, true, false, true, 39.9f, 43.0f, 0) != kFloorPlace) Fail("floor: a copy stalled within 40 flat must be placed");
    if (FloorPlace(true, true, false, true, 40.1f, 43.0f, 0) != kFloorNone) Fail("floor: a copy stalled beyond 40 flat must not be placed");
    if (FloorPlace(true, false, false, true, 10.0f, 43.0f, 0) != kFloorNone) Fail("floor: a stall with no stop point stated must not place");
    if (FloorPlace(true, true, false, false, 10.0f, 43.0f, 0) != kFloorNone) Fail("floor: a copy still closing on the stop point must not be placed");
    /* T849: the owner hopped 43 up at 17 sideways; its copy stopped closing on the stop point */
    if (FloorPlace(true, true, false, true, 17.0f, 43.0f, 0) != kFloorPlace) Fail("floor: 17 sideways and 43 up (T849) must place");
}
void t_t424_floor_strikes()
{
    using namespace floorplace;
    FloorWatch w; FloorWatchReset(&w);
    if (FloorWatchStop(&w, 100.0f, 200.0f) != 1 || w.strikes != 0) Fail("floor: the first stop point starts a count of 0");
    if (FloorPlace(true, true, true, false, 0.5f, 43.0f, w.strikes) != kFloorPlace) Fail("floor: no strikes yet places");
    if (!FloorStillApart(20.1f) || !FloorStillApart(-43.0f) || FloorStillApart(5.0f) || FloorStillApart(-19.9f)) Fail("floor: the result reads a gap over 20 as still apart");
    ++w.strikes;
    if (FloorWatchStop(&w, 100.5f, 200.0f) != 0 || w.strikes != 1) Fail("floor: a stop point within 1 flat is the same stop - its strikes kept");
    if (FloorPlace(true, true, true, false, 0.5f, 43.0f, w.strikes) != kFloorPlace) Fail("floor: one strike still places");
    ++w.strikes;
    if (FloorPlace(true, true, true, false, 0.5f, 43.0f, w.strikes) != kFloorGaveUp) Fail("floor: two strikes at one stop point give up");
    if (FloorPlace(true, true, true, false, 0.5f, 5.0f, w.strikes) != kFloorNone) Fail("floor: a closed gap after giving up is nothing to do");
    if (FloorWatchStop(&w, 130.0f, 200.0f) != 1 || w.strikes != 0 || w.gaveUpSaid != 0 || w.gatedSaid != 0) Fail("floor: a new stop point resets the strikes and the once-only notes");
    if (FloorPlace(true, true, true, false, 0.5f, 43.0f, w.strikes) != kFloorPlace) Fail("floor: a new stop point places again");
}
void t_t424_floor_nan()
{
    using namespace floorplace;
    volatile float zero = 0.0f;
    const float nan = zero / zero;
    const float inf = 1.0f / zero;
    if (FloorPlace(true, true, true, false, 0.5f, nan, 0) != kFloorNone) Fail("floor: a height gap that is not a number never places");
    if (FloorPlace(true, true, true, false, nan, 43.0f, 0) != kFloorNone) Fail("floor: a flat distance that is not a number never places");
    if (FloorPlace(true, true, true, false, 0.5f, inf, 0) != kFloorNone) Fail("floor: an infinite height gap never places");
    if (!FloorStillApart(nan)) Fail("floor: an unreadable result counts as a strike");
    FloorWatch w; FloorWatchReset(&w);
    FloorWatchStop(&w, 1.0f, 1.0f); ++w.strikes;
    if (FloorWatchStop(&w, nan, 1.0f) != 1 || w.strikes != 0) Fail("floor: a stop point that is not a number is not the same stop");
}

/* T-730: the font guard's pure rules - MyGUI's getFontName body, the jumps from its export, the guard's test, the empty name */
void t_t730_font_guard()
{
    unsigned char b[16];
    std::memcpy(b, swfont::kGetFontNameBody, 12);
    b[12] = 0xCC; b[13] = 0xCC; b[14] = 0xCC; b[15] = 0xCC;
    if (swfont::FontGuardBodyMatches(b, 12) != 1) Fail("the exact body bytes did not match");
    if (swfont::FontGuardBodyMatches(b, 16) != 1) Fail("the exact body bytes in a longer buffer did not match");
    for (int i = 0; i < 12; ++i)
    {
        unsigned char c[12];
        std::memcpy(c, swfont::kGetFontNameBody, 12);
        c[i] = (unsigned char)(c[i] ^ 0x01);
        if (swfont::FontGuardBodyMatches(c, 12) != 0) Fail("a body with one byte changed matched");
    }
    if (swfont::FontGuardBodyMatches(b, 11) != 0) Fail("a short buffer matched");
    if (swfont::FontGuardBodyMatches(0, 12) != 0) Fail("no buffer matched");
    const unsigned char e9[12] = { 0xE9, 0x10, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
    const unsigned char ff25[12] = { 0xFF, 0x25, 0x00, 0x10, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
    if (swfont::FontGuardBodyMatches(e9, 12) != 0) Fail("an E9 jump stub matched the body");
    if (swfont::FontGuardBodyMatches(ff25, 12) != 0) Fail("an FF 25 jump stub matched the body");
    unsigned long long next = 0;
    if (swfont::FontGuardJump(e9, 12, 0x180001000ull, &next) != swfont::kJumpRel32 || next != 0x180001015ull)
        Fail("an E9 forward jump was not followed to its target");
    const unsigned char back[5] = { 0xE9, 0xF0, 0xFF, 0xFF, 0xFF };
    if (swfont::FontGuardJump(back, 5, 0x180001000ull, &next) != swfont::kJumpRel32 || next != 0x180000FF5ull)
        Fail("an E9 backward jump was not followed to its target");
    if (swfont::FontGuardJump(ff25, 12, 0x180001000ull, &next) != swfont::kJumpIndirect || next != 0x180002006ull)
        Fail("an FF 25 jump did not give its slot's address");
    next = 7;
    if (swfont::FontGuardJump(swfont::kGetFontNameBody, 12, 0x180001000ull, &next) != swfont::kNoJump || next != 7)
        Fail("the body was taken for a jump");
    if (swfont::FontGuardJump(e9, 4, 0x180001000ull, &next) != swfont::kNoJump) Fail("a 4-byte E9 was taken for a jump");
    if (swfont::FontGuardJump(ff25, 5, 0x180001000ull, &next) != swfont::kNoJump) Fail("a 5-byte FF 25 was taken for a jump");
    int x = 0;
    if (swfont::FontGuardNeeded(0) != 1) Fail("a null font did not need the guard");
    if (swfont::FontGuardNeeded(&x) != 0) Fail("a font needed the guard");
    if (sizeof(swfont::RuntimeString) != 40) Fail("the empty name is not 40 bytes (the VS2010 runtime string)");
    if (offsetof(swfont::RuntimeString, size) != 0x10) Fail("the empty name's size is not at +0x10");
    if (offsetof(swfont::RuntimeString, capacity) != 0x18) Fail("the empty name's capacity is not at +0x18");
    if (swfont::kInlineCapacity != 15) Fail("the inline capacity is not 15");
}
/* ---- T-36: a joining game carried into the reset world - the reset history (world.txt line 4, the WELCOME of store protocol 93), the
   reset box's words and keep rule, the records the carry leaves behind, the status line ---- */
void t_t36_joiner_carry()
{
    {   using namespace coopworld;
        const std::string a = "1700000000-0000abcd", b = "1800000000-12345678", c = "1900000000-deadbeef";
        const std::string t0 = WorldTxtFormatWithId("My World", 1600000000, a, true);
        std::vector<WorldResetEntry> h;
        if (WorldTxtParseHistory(t0, &h)) Fail("history: a world never reset has a history");
        std::vector<std::string> two; two.push_back(a); two.push_back(b);
        const std::string old1 = WorldTxtFormatWithId("My World", 1600000000, c, false) + WorldTxtEarlierLine(two, 1850000000, true);
        if (!WorldTxtParseHistory(old1, &h) || h.size() != 2 || h[0].id != a || h[0].kept || h[0].at != 0 || h[1].id != b || !h[1].kept || h[1].at != 1850000000)
            Fail("history: a world.txt without line 4 is not read as 'not kept' but for line 3's own reset");
        std::string t1, t2, t3, was;
        if (WorldTxtAfterReset(t0, b, 1800000000, true, &t1, &was) != kResetTxtOk || WorldTxtAfterReset(t1, c, 1900000000, false, &t2, &was) != kResetTxtOk)
            Fail("history: the resets fail");
        if (!WorldTxtParseHistory(t2, &h) || h.size() != 2 || h[0].id != a || !h[0].kept || h[0].at != 1800000000 || h[1].id != b || h[1].kept || h[1].at != 1900000000)
            Fail("history: two resets do not each keep their own time and choice");
        std::vector<WorldResetEntry> r4;
        if (!WorldTxtParseResets(t2, 2, &r4) || WorldTxtParseResets(t2, 1, &r4)) Fail("history: line 4 is not one entry per earlier id");
        std::vector<std::string> e; long long at = 0; bool kept = true;
        if (!WorldTxtParseEarlier(t2, &e, &at, &kept) || e.size() != 2 || at != 1900000000 || kept) Fail("history: line 3 changed with line 4 beside it");
        if (WorldDataDirDecide(t2, c) != kDataDirOwn) Fail("history: a four-line world.txt is not read by the folder rule as before");
        const std::string bad = old1 + "resets\tnot-a-time:kept\n";
        if (!WorldTxtParseHistory(bad, &h) || h.size() != 2 || h[0].kept || !h[1].kept) Fail("history: a malformed line 4 is not set aside");
        const std::string shortL4 = old1 + "resets\t5:kept\n";
        if (WorldTxtParseResets(shortL4, 2, 0) || !WorldTxtParseHistory(shortL4, &h) || h[0].kept || h[0].at != 0) Fail("history: a line 4 with too few entries is read");
        if (WorldTxtAfterReset(old1, "2000000000-00000001", 2000000000, true, &t3, &was) != kResetTxtOk || !WorldTxtParseHistory(t3, &h) || h.size() != 3
            || h[0].kept || !h[1].kept || h[1].at != 1850000000 || h[2].id != c || !h[2].kept || h[2].at != 2000000000 || !WorldTxtParseResets(t3, 3, 0))
            Fail("history: a reset of a world.txt without line 4 does not write one entry per id");
    }
    {   using namespace coopstore;
        StoreWelcome a; a.protocol = kStoreWelcomeHistorySince; a.authority = 1; a.recordCount = 4; a.slot = 2; a.hasWorld = true; a.world = "Ridge";
        a.hasWorldId = true; a.worldId = "1900000000-deadbeef"; a.hasPerson = true; a.person = "p1"; a.hasMods = true; a.modsVerdict = kModsMatch;
        StoreWelcomeReset r1; r1.id = "1700000000-0000abcd"; r1.flags = kStoreWelcomeResetKept; r1.at = 1800000000ULL;
        StoreWelcomeReset r2; r2.id = "1800000000-12345678"; r2.flags = 0; r2.at = 1900000000ULL;
        a.hasHistory = true; a.history.push_back(r1); a.history.push_back(r2);
        std::vector<char> b; StoreWelcomeEncode(a, &b);
        StoreWelcome r;
        if (!StoreWelcomeDecode(b, &r) || !r.hasHistory || r.historyMalformed || r.history.size() != 2 || r.history[0].id != r1.id || r.history[0].flags != kStoreWelcomeResetKept
            || r.history[0].at != 1800000000ULL || r.history[1].id != r2.id || r.history[1].flags != 0 || r.history[1].at != 1900000000ULL || r.person != "p1" || !r.hasMods)
            Fail("welcome history: the round trip loses a field");
        StoreWelcome o = a; o.protocol = kStoreWelcomeHistorySince - 1; StoreWelcomeEncode(o, &b);
        if (!StoreWelcomeDecode(b, &r) || r.hasHistory || r.historyMalformed || r.person != "p1" || !r.hasMods) Fail("welcome history: a protocol 92 WELCOME is not read as before");
        StoreWelcome none = a; none.history.clear(); StoreWelcomeEncode(none, &b);
        if (!StoreWelcomeDecode(b, &r) || !r.hasHistory || !r.history.empty() || r.historyMalformed || !r.hasMods) Fail("welcome history: an empty history is not read as none");
        std::vector<char> big(b.begin(), b.begin() + (b.size() - 16));   /* up to the count: the count, then the mods' three words (no list, no tail) */
        StoreWelcomePutU32(&big, 1u); StoreWelcomePutU32(&big, kStoreWelcomeWorldIdMax + 1);
        big.insert(big.end(), (size_t)kStoreWelcomeWorldIdMax + 1, 'x'); StoreWelcomePutU32(&big, 0u); big.insert(big.end(), (size_t)8, (char)0);
        big.insert(big.end(), b.end() - 12, b.end());
        if (!StoreWelcomeDecode(big, &r) || !r.historyMalformed || r.hasHistory || r.hasMods) Fail("welcome history: an id longer than the cap is read");
        std::vector<char> many(b.begin(), b.end());
        { const unsigned int n = kStoreWelcomeHistoryMax + 1; std::memcpy(&many[many.size() - 16], &n, 4); }
        if (!StoreWelcomeDecode(many, &r) || !r.historyMalformed || r.hasMods) Fail("welcome history: a count over the cap is read");
        StoreWelcomeEncode(a, &b); b.resize(b.size() - 22);
        if (!StoreWelcomeDecode(b, &r) || !r.historyMalformed || r.hasHistory || r.hasMods) Fail("welcome history: a history cut short is read");
        StoreWelcome lots = none;
        for (unsigned int k = 0; k < kStoreWelcomeHistoryMax + 3; ++k) { StoreWelcomeReset x; x.id = "1700000000-0000abcd"; x.at = k; lots.history.push_back(x); }
        StoreWelcomeEncode(lots, &b);
        if (!StoreWelcomeDecode(b, &r) || r.history.size() != kStoreWelcomeHistoryMax || r.history[0].at != 3ULL) Fail("welcome history: over the cap, the newest are not the ones kept");
    }
    {   using namespace worldreset;
        std::vector<std::string> ids; std::vector<int> kept;
        ids.push_back("A"); kept.push_back(1);
        if (!ResetBoxKept(ids, kept, "A")) Fail("box: one reset that kept is not 'kept'");
        kept[0] = 0;
        if (ResetBoxKept(ids, kept, "A")) Fail("box: one reset that did not keep is 'kept'");
        ids.push_back("B"); kept[0] = 1; kept.push_back(0);
        if (ResetBoxKept(ids, kept, "A") || ResetBoxKept(ids, kept, "B")) Fail("box: two resets, the later not kept, read as kept");
        kept[0] = 0; kept[1] = 1;
        if (ResetBoxKept(ids, kept, "A") || !ResetBoxKept(ids, kept, "B")) Fail("box: two resets mixed - only the resets from the carried id on count");
        kept[0] = 1;
        if (!ResetBoxKept(ids, kept, "A") || ResetBoxKept(ids, kept, "C")) Fail("box: both kept is not kept, or an unknown id is");
        if (ResetBoxText(true) != "The host reset this world. Your characters, money, research, equipment, map and faction standings came with you. Your buildings and stored items came too. Everything else has started over.")
            Fail("box: the kept words are not the approved ones");
        if (ResetBoxText(false) != "The host reset this world. Your characters, money, research, equipment, map and faction standings came with you. Your buildings and stored items were not kept. Everything else has started over.")
            Fail("box: the not-kept words are not the approved ones");
        const std::string m1 = ResetBoxShownAdd(std::string(), "1900000000-deadbeef");
        if (!ResetBoxShownIn(m1, "1900000000-deadbeef") || ResetBoxShownIn(m1, "1900000000-deadbee") || ResetBoxShownIn(std::string(), "x")) Fail("box: the memo does not name the reset world exactly");
        const std::string m2 = ResetBoxShownAdd(m1, "2000000000-00000001");
        if (!ResetBoxShownIn(m2, "1900000000-deadbeef") || !ResetBoxShownIn(m2, "2000000000-00000001") || ResetBoxShownAdd(m2, "2000000000-00000001") != m2)
            Fail("box: a second reset world is not added once");
        if (!ResetBoxShownIn("1900000000-deadbeef\r\n", "1900000000-deadbeef")) Fail("box: a CRLF memo is not read");
    }
    {   using namespace coopown;
        if (!CarryKeyDropped("pp.build") || !CarryKeyDropped("pp.help") || CarryKeyDropped("pp.money") || CarryKeyDropped("pp.squad.x")) Fail("carry: the wrong keys are left behind");
        if (!CarryFileDropped("pp.build.rec") || !CarryFileDropped("PP.Build.REC.prev") || !CarryFileDropped("pp.build.rec.1234.tmp") || !CarryFileDropped("pp.help.rec")
            || CarryFileDropped("pp.buildx.rec") || CarryFileDropped("pp.build.recx") || CarryFileDropped("pp.squad.a.rec") || CarryFileDropped("pp.money.rec")
            || CarryFileDropped("own.index") || CarryFileDropped("pp.build"))
            Fail("carry: the wrong files are left behind");
        IndexLine l; std::string head, s1, s2, s3, s4;
        EncodeIndexHeader("w", "p", &head);
        l.key = "pp.build"; l.seq = 1; l.len = 3; l.crc = 1; EncodeIndexLine(l, &s1);
        l.key = "pp.money"; l.seq = 2; EncodeIndexLine(l, &s2);
        l.key = "pp.help"; l.seq = 3; EncodeIndexLine(l, &s3);
        l.key = "pp.build"; l.seq = 4; EncodeIndexLine(l, &s4);
        std::vector<std::string> dropped;
        const std::string out = CarryIndexText(head + s1 + s2 + s3 + s4 + "junk line\n", &dropped);
        if (out != head + s2 + "junk line\n" || dropped.size() != 2 || dropped[0] != "pp.build" || dropped[1] != "pp.help")
            Fail("carry: own.index keeps a line of a record left behind, or loses another line");
        if (CarryIndexText(head + s2, 0) != head + s2) Fail("carry: an index with nothing to leave behind changes");
    }
    if (std::string(coopui::kResetMovingText) != "Moving your characters into the reset world...") Fail("the status line is not the approved words");
}
/* RESET WORLD keeping the players' buildings (src/common/zonekeep.h, poskey.h): the position key as both sides build it, the area
   record read from two real world-server files (src/coop-test/data: zone.52.52 holds a player's General Storage Chest with six
   items and a Bed, written by slot 1; zone.35.42 holds a player's Small Shack, written by slot 0), a cut or damaged file refused
   cleanly, and the kept list's text round trip. Runs from src\coop-test. */
const zonekeep::KeptPiece* T36b1Find(const std::vector<zonekeep::KeptPiece>& ps, const std::string& key)
{
    for (size_t i = 0; i < ps.size(); ++i) if (ps[i].key == key) return &ps[i];
    return 0;
}
/* the area file and its writer slot from its .meta (-1 = none read); false = the file is not there */
bool T36b1Load(const char* stem, std::string* bytes, int* writer)
{
    *bytes = T313ReadFile((std::string("data\\") + stem + ".platoon").c_str());
    *writer = -1;
    const std::string meta = T313ReadFile((std::string("data\\") + stem + ".meta").c_str());
    const std::string::size_type nl = meta.find('\n');
    coopstore::MetaLine m;
    if (coopstore::MetaParse(nl == std::string::npos ? meta : meta.substr(0, nl), &m) && m.owner <= coopstore::kMaxOwnerSlot) *writer = (int)m.owner;
    return !bytes->empty();
}
void t_t36b1_position_key()
{
    char k[poskey::kKeyCap];
    if (poskey::KeyOfPos("1459-gamedata.base", 94950.0f, 3077.035400390625f, 95300.0f, k, poskey::kKeyCap) != poskey::kKeyOk
        || std::string(k) != "1459-gamedata.base@52,52@949500,953000,30770")
        Fail(std::string("key: the T955 chest's key is '") + k + "', the game logged 1459-gamedata.base@52,52@949500,953000,30770");
    int ok = 0;
    if (poskey::TenthsOfAUnit(-0.05f, &ok) != -1 || ok != 1 || poskey::TenthsOfAUnit(0.04f, &ok) != 0) Fail("key: tenths do not round half away from zero");
    int sx = 0, sy = 0;
    poskey::SectorOf(-1.0e6f, 1.0e6f, &sx, &sy);
    if (sx != 0 || sy != 63) Fail("key: a position off the map is not clamped into the 64 x 64 grid");
    const std::string longSid = "1234567890123456789012345678901234-long.mod";   /* fits the sid room, not the key */
    const int kr = poskey::KeyOfPos(longSid.c_str(), 94950.0f, 3077.0f, 95300.0f, k, poskey::kKeyCap);
    if (kr != poskey::kKeyHashed || k[0] != '#' || std::strlen(k) >= (size_t)poskey::kKeyCap) Fail(std::string("key: a long sid is not hashed into the key: '") + k + "'");
    char hashed[16]; int hat = poskey::AppendStr(hashed, 16, 0, "#"); hat = poskey::AppendHex8(hashed, 16, hat, poskey::SidHash32(longSid.c_str()));
    if (hat != 9 || std::string(k).compare(0, 10, std::string(hashed) + "@") != 0) Fail("key: the hashed form is not '#' + the sid's FNV-1a digits");
    if (poskey::KeyOfPos(std::string(96, 'a').c_str(), 0.0f, 0.0f, 0.0f, k, poskey::kKeyCap) != poskey::kKeySidTooLong || k[0] != 0)
        Fail("key: a sid longer than the sid room gives a key");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (poskey::KeyOfPos("1459-gamedata.base", nan, 0.0f, 0.0f, k, poskey::kKeyCap) != poskey::kKeyBadPos || k[0] != 0) Fail("key: a NaN position gives a key");
    /* T-779 position key forms: a record name whose key is kKeyCap..63 characters long is kept verbatim by a 64-byte buffer and
       hashed by the box road's kKeyCap one; the two texts name one piece. Synthetic record names: spaces, dots, bytes beyond
       ASCII, long names. */
    {
        const char* const sids[4] = { "1234567-Some Long Name.mod", "98765-a.b.c.d.e.f.g.mod", "4321-caf\xc3\xa9 b\xc3\xa4r stuff.mod",
                                      "123-a name with spaces that runs past the key room.mod" };
        for (int i = 0; i < 4; ++i)
        {
            const char* sid = sids[i];
            char big[64], box[poskey::kKeyCap], wf[poskey::kKeyCap];
            const int rb = poskey::ComposeOrHash(big, 64, sid, 18, 33, -604241, 87679, 12928, 0, 0);
            const int rx = poskey::ComposeOrHash(box, poskey::kKeyCap, sid, 18, 33, -604241, 87679, 12928, 0, 0);
            if (rx != 2 || box[0] != '#') Fail(std::string("key forms: the box road does not hash the record of '") + big + "'");
            if (poskey::WireForm(big, wf, poskey::kKeyCap) == 0 || std::string(wf) != box)
                Fail(std::string("key forms: the wire form of '") + big + "' is '" + wf + "', the box road's key is '" + box + "'");
            if (poskey::WireForm(box, wf, poskey::kKeyCap) != 1 || std::string(wf) != box) Fail("key forms: a hashed key is not its own wire form");
            char hf[16]; int hat = poskey::AppendStr(hf, 16, 0, "#"); hat = poskey::AppendHex8(hf, 16, hat, poskey::SidHash32(sid));
            if (poskey::SidFieldsSame(sid, hf) != 1 || poskey::SidFieldsSame(hf, sid) != 1 || poskey::SidFieldsSame(sid, sid) != 1)
                Fail(std::string("key forms: '") + sid + "' and its hashed field are not one record");
            if (rb == 1 && poskey::SameSpotSameRecord(sid, 18, 33, -604241, 87679, hf, 18, 33, -604241, 87679, 1) != 1)
                Fail("key forms: a standing piece does not answer to the other form of its own key at distance 0");
            if (poskey::SameSpotSameRecord(hf, 18, 33, -604240, 87680, sid, 18, 33, -604241, 87679, 1) != 1)
                Fail("key forms: a piece one tenth away (the match drift) does not answer");
            if (poskey::SameSpotSameRecord(hf, 18, 33, -604239, 87679, sid, 18, 33, -604241, 87679, 1) != 0
                || poskey::SameSpotSameRecord(hf, 18, 34, -604241, 87679, sid, 18, 33, -604241, 87679, 1) != 0)
                Fail("key forms: a piece two tenths away or in another sector answers");
        }
        /* the first name gives a 52-character key: verbatim at 64 bytes, hashed at kKeyCap */
        char big0[64];
        if (poskey::ComposeOrHash(big0, 64, sids[0], 18, 33, -604241, 87679, 12928, 0, 0) != 1)
            Fail("key forms: a 64-byte buffer hashes a 52-character key (the two forms the restore missed by)");
        /* another record, the same spot: never the same piece */
        char h2[16]; int h2at = poskey::AppendStr(h2, 16, 0, "#"); h2at = poskey::AppendHex8(h2, 16, h2at, poskey::SidHash32("1234568-Some Long Name.mod"));
        if (poskey::SidFieldsSame(sids[0], h2) != 0
            || poskey::SameSpotSameRecord(h2, 18, 33, -604241, 87679, sids[0], 18, 33, -604241, 87679, 1) != 0)
            Fail("key forms: another record's hashed field answers");
        if (poskey::SidFieldsSame("#0000000g", "x") != 0 || poskey::IsHashedSidField("#ABCDEF01") != 0 || poskey::IsHashedSidField("#abcdef0") != 0
            || poskey::IsHashedSidField("#abcdef012") != 0 || poskey::IsHashedSidField("#abcdef01") != 1)
            Fail("key forms: a hashed field is '#' + exactly 8 lowercase hex digits");
        /* a key shorter than kKeyCap is its own wire form; text that is no position key has none */
        char wf2[poskey::kKeyCap];
        if (poskey::WireForm("1459-gamedata.base@52,52@949500,953000,30770", wf2, poskey::kKeyCap) != 1
            || std::string(wf2) != "1459-gamedata.base@52,52@949500,953000,30770")
            Fail("key forms: a short key is not its own wire form");
        if (poskey::WireForm("no separators in this text at all, and long enough to pass the room", wf2, poskey::kKeyCap) != 0 || wf2[0] != 0)
            Fail("key forms: text with no '@' has a wire form");
        if (poskey::WireForm("", wf2, poskey::kKeyCap) != 0 || poskey::WireForm(0, wf2, poskey::kKeyCap) != 0) Fail("key forms: no key has a wire form");
    }
}
/* T-779 fold: the key texts at the 47 / 48 boundary, the wire-form comparison and a key's spot */
void t_t779_fold_keys()
{
    const char* const sid21 = "1234567-Some Name.mod";    /* 21 characters: a 47-character key */
    const char* const sid22 = "1234567-Some Names.mod";   /* 22 characters: a 48-character key */
    char k47[64], k47b[poskey::kKeyCap], k48[64], k48b[poskey::kKeyCap], wf[poskey::kKeyCap];
    if (poskey::ComposeOrHash(k47, 64, sid21, 18, 33, -604241, 87679, 12928, 0, 0) != 1 || std::strlen(k47) != 47) Fail(std::string("key fold: the 47-character key is '") + k47 + "'");
    if (poskey::ComposeOrHash(k47b, poskey::kKeyCap, sid21, 18, 33, -604241, 87679, 12928, 0, 0) != 1 || std::string(k47b) != k47) Fail("key fold: the box road hashes a 47-character key");
    if (poskey::WireForm(k47, wf, poskey::kKeyCap) != 1 || std::string(wf) != k47) Fail("key fold: a 47-character key is not its own wire form");
    if (poskey::ComposeOrHash(k48, 64, sid22, 18, 33, -604241, 87679, 12928, 0, 0) != 1 || std::strlen(k48) != 48) Fail(std::string("key fold: the 48-character key is '") + k48 + "'");
    if (poskey::ComposeOrHash(k48b, poskey::kKeyCap, sid22, 18, 33, -604241, 87679, 12928, 0, 0) != 2 || k48b[0] != '#') Fail("key fold: the box road does not hash a 48-character key");
    if (poskey::WireForm(k48, wf, poskey::kKeyCap) != 2 || std::string(wf) != k48b) Fail(std::string("key fold: the wire form of the 48-character key is '") + wf + "', the box road's '" + k48b + "'");
    const std::string hf48 = std::string(k48b).substr(0, 9);
    if (poskey::SidFieldsSame(sid22, hf48.c_str()) != 1 || poskey::SidFieldsSame(hf48.c_str(), sid22) != 1 || poskey::SidFieldsSame(sid21, hf48.c_str()) != 0)
        Fail("key fold: the 48-character key's hashed field is not its record's (or answers another)");
    if (poskey::SidFieldsSame(sid21, sid21) != 1) Fail("key fold: a 47-character key's record is not itself");
    if (poskey::KeysSamePiece(k48, k48b) != 1 || poskey::KeysSamePiece(k48b, k48) != 1 || poskey::KeysSamePiece(k47, k47b) != 1)
        Fail("key fold: one piece's two key texts are not the same piece");
    if (poskey::KeysSamePiece(k48, k47) != 0 || poskey::KeysSamePiece(k48, "") != 0 || poskey::KeysSamePiece(0, k48) != 0) Fail("key fold: two pieces' keys are the same piece");
    if (poskey::KeyHasTwoForms(k47) != 0 || poskey::KeyHasTwoForms(k48) != 1 || poskey::KeyHasTwoForms(k48b) != 0 || poskey::KeyHasTwoForms(0) != 0)
        Fail("key fold: the two-text test is wrong at the 47 / 48 boundary");
    char sid[poskey::kSidRoom];
    int sx = 0, sy = 0;
    long x10 = 0, z10 = 0;
    if (poskey::ParseKeySpot(k48, sid, poskey::kSidRoom, &sx, &sy, &x10, &z10) != 1 || std::string(sid) != sid22 || sx != 18 || sy != 33 || x10 != -604241 || z10 != 87679)
        Fail("key fold: a key's spot does not read back");
    if (poskey::ParseKeySpot(k48b, sid, poskey::kSidRoom, &sx, &sy, &x10, &z10) != 1 || hf48 != sid) Fail("key fold: a hashed key's spot does not read back");
    if (poskey::ParseKeySpot("no position key", sid, poskey::kSidRoom, &sx, &sy, &x10, &z10) != 0 || poskey::ParseKeySpot("a@1,2@3,4", sid, poskey::kSidRoom, &sx, &sy, &x10, &z10) != 0
        || poskey::ParseKeySpot("a@1,2@3,4,5x", sid, poskey::kSidRoom, &sx, &sy, &x10, &z10) != 0)
        Fail("key fold: text that is no position key reads as one");
}
void t_t36b1_area_extract()
{
    std::string b; int writer = -2;
    if (!T36b1Load("zone.52.52", &b, &writer)) { Fail("data\\zone.52.52.platoon is missing (the suite runs from src\\coop-test)"); return; }
    if (writer != 1) Fail("zone.52.52.meta's writer slot is " + I(writer) + ", expected 1");
    zonekeep::Container c; std::string why;
    if (!zonekeep::ParseContainer((const unsigned char*)b.data(), b.size(), &c, &why)) { Fail("zone.52.52 does not parse: " + why); return; }
    if (c.version != 15 || c.recs.size() != 172) Fail("zone.52.52: version " + I(c.version) + ", " + I((int)c.recs.size()) + " records (expected 15, 172)");
    std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st;
    zonekeep::ExtractKeptPieces(c, writer, &ps, &st, &why);
    if (st.kept != 2 || st.items != 1 || st.nobody != 116 || st.nodes != 0) Fail("zone.52.52: kept " + I(st.kept) + " piece(s) and " + I(st.items) + " item(s), "
        + I(st.nobody) + " nobody's, " + I(st.nodes) + " nodes (expected 2, 1, 116, 0)");
    const zonekeep::KeptPiece* chest = T36b1Find(ps, "1459-gamedata.base@52,52@949500,953000,30770");
    if (chest == 0) { Fail("zone.52.52: the General Storage Chest is not kept under its key"); return; }
    if (chest->ownerSlot != 2 || chest->tmpl != "1459-gamedata.base" || chest->complete != 1 || !chest->hostKey.empty())
        Fail("zone.52.52: the chest is kept as slot " + I(chest->ownerSlot) + " template " + chest->tmpl);
    if (chest->items.size() != 1 || chest->items[0].tmpl != "2292-gamedata.base" || chest->items[0].quantity != 6 || chest->items[0].quality != 1.0f
        || chest->items[0].material != "5263-lanterns_otto.mod" || chest->items[0].section != "backpack_content")
        Fail("zone.52.52: the chest's item is not 2292-gamedata.base x6, quality 1.0");
    /* the same area with no writer known: every piece names a player's stand-in id, so nothing changes */
    std::vector<zonekeep::KeptPiece> ps2; zonekeep::KeepStats st2;
    zonekeep::ExtractKeptPieces(c, -1, &ps2, &st2, &why);
    if (st2.kept != 2) Fail("zone.52.52: pieces named coop-p2 depend on the writer's slot");

    std::string b2; int w2 = -2;
    if (!T36b1Load("zone.35.42", &b2, &w2)) { Fail("data\\zone.35.42.platoon is missing"); return; }
    if (w2 != 0) Fail("zone.35.42.meta's writer slot is " + I(w2) + ", expected 0");
    zonekeep::Container c2;
    if (!zonekeep::ParseContainer((const unsigned char*)b2.data(), b2.size(), &c2, &why)) { Fail("zone.35.42 does not parse: " + why); return; }
    std::vector<zonekeep::KeptPiece> sh; zonekeep::KeepStats s3;
    zonekeep::ExtractKeptPieces(c2, w2, &sh, &s3, &why);
    const zonekeep::KeptPiece* shack = T36b1Find(sh, "4092-TwoStorey.mod@35,42@182634,479665,4113");
    if (s3.kept != 1 || shack == 0 || shack->ownerSlot != 0) Fail("zone.35.42: the Small Shack is not the one kept piece, as slot 0, under 4092-TwoStorey.mod@35,42@182634,479665,4113");
    std::vector<zonekeep::KeptPiece> sh2; zonekeep::KeepStats s4;
    zonekeep::ExtractKeptPieces(c2, -1, &sh2, &s4, &why);
    if (s4.kept != 0 || s4.writerUnknown != 1) Fail("zone.35.42: the player faction's own piece is kept although the area's writer is not known");

    /* cut short anywhere in the records, or a length that runs past the end: refused with a reason, never read past */
    const size_t cuts[] = { 0, 3, 11, 12, 40, 1000, b.size() / 2, c.end - 1 };
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i)
    {
        zonekeep::Container t; std::string w;
        if (zonekeep::ParseContainer((const unsigned char*)b.data(), cuts[i], &t, &w) || w.empty()) Fail("a file cut to " + I((int)cuts[i]) + " bytes is not refused");
    }
    std::string bad(b);
    bad[24] = (char)0xFF; bad[25] = (char)0xFF; bad[26] = (char)0xFF; bad[27] = (char)0x7F;   /* the first record's name length: 2^31-1 */
    {
        zonekeep::Container t; std::string w;
        if (zonekeep::ParseContainer((const unsigned char*)bad.data(), bad.size(), &t, &w) || w.empty()) Fail("a string length past the end is not refused");
    }
    std::string v14(b); v14[0] = 14;
    {
        zonekeep::Container t; std::string w;
        if (zonekeep::ParseContainer((const unsigned char*)v14.data(), v14.size(), &t, &w)) Fail("a container of version 14 is read");
    }
}
void t_t36b1_kept_list_round_trip()
{
    std::vector<zonekeep::KeptPiece> ps;
    zonekeep::KeptPiece p;
    p.ownerSlot = 3; p.key = "4092-TwoStorey.mod@35,42@182634,479665,4113"; p.tmpl = "4092-TwoStorey.mod";
    p.pos[0] = 18263.4f; p.pos[1] = -411.3f; p.pos[2] = 47966.5f; p.rot[0] = 0.5f; p.rot[3] = -0.25f;
    p.floor = 2; p.hostKey = "a\tb\\c\nd\re"; p.progress = 0.375f; p.complete = 0; p.destroyed = 1; p.broken = 1;
    zonekeep::KeptMat m; m.index = 3; m.count = 12.5f; p.mats.push_back(m); m.index = 0; m.count = 0.0f; p.mats.push_back(m);
    zonekeep::KeptItem it; it.tmpl = "2292-gamedata.base"; it.material = "x\\t"; it.company = ""; it.color = "c"; it.section = "main";
    it.quality = 0.1f; it.charges = -1.0f; it.quantity = 6; it.level = -7; it.function = 2; it.unique = 1; it.slotX = 4; it.slotY = 9;
    p.items.push_back(it); p.items.push_back(it);
    ps.push_back(p);
    zonekeep::KeptPiece q; q.ownerSlot = 0; q.key = "k"; q.tmpl = "t"; ps.push_back(q);
    const std::string text = zonekeep::EncodeKept(ps);
    if (text.find('\r') != std::string::npos || text.compare(0, 14, "kept-pieces\t3\n") != 0) Fail("list: the header is not 'kept-pieces<TAB>3' or a CR is written");
    std::vector<zonekeep::KeptPiece> back; std::string why;
    if (!zonekeep::DecodeKept(text, &back, &why)) { Fail("list: its own text is refused: " + why); return; }
    if (zonekeep::EncodeKept(back) != text) Fail("list: encode(decode(text)) is not the text");
    if (back.size() != 2 || back[0].hostKey != p.hostKey || back[0].mats.size() != 2 || back[0].mats[0].count != 12.5f || back[0].items.size() != 2
        || back[0].items[1].material != "x\\t" || back[0].items[1].level != -7 || back[0].items[1].quality != 0.1f || back[0].pos[1] != -411.3f
        || back[0].rot[3] != -0.25f || back[0].destroyed != 1 || back[1].items.size() != 0 || back[1].key != "k")
        Fail("list: a field changed on the round trip");
    std::string crlf;
    for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '\n') crlf += '\r'; crlf += text[i]; }
    if (!zonekeep::DecodeKept(crlf, &back, &why) || back.size() != 2) Fail("list: CR LF line ends are refused");
    if (zonekeep::DecodeKept("kept-pieces\t4\n", &back, &why) || !back.empty()) Fail("list: a format this build does not know is read");
    if (zonekeep::DecodeKept("kept-pieces\t1\nI\tx\n", &back, &why)) Fail("list: an item before any piece is read");
    if (zonekeep::DecodeKept(text.substr(0, text.size() - 3), &back, &why)) Fail("list: a cut last line is read");
    if (!zonekeep::DecodeKept("kept-pieces\t1\n", &back, &why) || !back.empty()) Fail("list: an empty list is refused");
    /* the two real areas' pieces, through the text and back */
    std::string b; int w = -1; zonekeep::Container c;
    if (T36b1Load("zone.52.52", &b, &w) && zonekeep::ParseContainer((const unsigned char*)b.data(), b.size(), &c, &why))
    {
        std::vector<zonekeep::KeptPiece> real; zonekeep::KeepStats st;
        zonekeep::ExtractKeptPieces(c, w, &real, &st, &why);
        const std::string t2 = zonekeep::EncodeKept(real);
        if (!zonekeep::DecodeKept(t2, &back, &why) || zonekeep::EncodeKept(back) != t2 || back.size() != real.size()) Fail("list: zone.52.52's pieces do not round-trip");
    }
    else Fail("list: zone.52.52 could not be read");
}
/* THE KEPT PIECES LAID BACK ONCE EACH (src/common/keptwire.h, zonekeep.h): the KEPT wire, every op both ways, its bounds and
   malformed messages; the claim / release / lay / settle / leave decisions; the has rule; kept-state.txt; whether a new area record
   holds a kept piece and its box contents (the real zone.52.52: the General Storage Chest and its 2292-gamedata.base x6); a second
   reset's merge. Runs from src\coop-test. */
keptwire::Row T36b2Row(unsigned row)
{
    keptwire::Row w; w.row = row; w.p.ownerSlot = 2; w.p.key = "1459-gamedata.base@52,52@949500,953000,30770"; w.p.tmpl = "1459-gamedata.base";
    w.p.pos[0] = 94950.0f; w.p.pos[1] = 3077.0f; w.p.pos[2] = 95300.0f; w.p.rot[0] = 0.5f; w.p.rot[2] = -0.5f; w.p.floor = 1; w.p.progress = 0.75f; w.p.complete = 1;
    zonekeep::KeptMat m; m.index = 1; m.count = 2.5f; w.p.mats.push_back(m);
    zonekeep::KeptItem it; it.tmpl = "2292-gamedata.base"; it.material = "5263-lanterns_otto.mod"; it.section = "backpack_content"; it.quality = 1.0f;
    it.quantity = 6; it.slotX = 3; it.slotY = -1; it.unique = 1; w.p.items.push_back(it);
    return w;
}
std::string T36b2Pieces(const std::vector<keptwire::Row>& rows)
{
    std::vector<zonekeep::KeptPiece> ps;
    for (size_t i = 0; i < rows.size(); ++i) ps.push_back(rows[i].p);
    return zonekeep::EncodeKept(ps);
}
void t_t36b2_wire()
{
    std::vector<char> b; keptwire::Msg m;
    const unsigned ops[3] = { keptwire::kOpClaim, keptwire::kOpRelease, keptwire::kOpSettled };
    for (int k = 0; k < 3; ++k)
        if (!keptwire::EncodePart(&b, ops[k], 7, keptwire::kPartItems) || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.op != ops[k] || m.row != 7 || m.part != keptwire::kPartItems)
            Fail("KEPT: CLAIM / RELEASE / SETTLED op " + I((int)ops[k]) + " does not round-trip");
    if (keptwire::EncodePart(&b, keptwire::kOpClaim, 7, keptwire::kPartOwner)) Fail("KEPT: the owner part is claimed");
    if (keptwire::EncodePart(&b, keptwire::kOpLaid, 7, keptwire::kPartPiece)) Fail("KEPT: EncodePart carries LAID");
    const std::string live = "4092-TwoStorey.mod@35,42@182634,479665,4113";
    if (!keptwire::EncodeLaid(&b, 3, keptwire::kPartPiece, keptwire::kHowHandedBack, live) || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.op != keptwire::kOpLaid
        || m.row != 3 || m.part != keptwire::kPartPiece || m.how != keptwire::kHowHandedBack || m.liveKey != live) Fail("KEPT: LAID does not round-trip");
    if (!keptwire::EncodeLaid(&b, 3, keptwire::kPartOwner, keptwire::kHowOwnerRegistered, "") || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.part != keptwire::kPartOwner || !m.liveKey.empty())
        Fail("KEPT: LAID owner with no live key does not round-trip");
    if (keptwire::EncodeLaid(&b, 3, keptwire::kPartPiece, keptwire::kHowItemsLaid, "")) Fail("KEPT: a how that does not fit the part is sent");
    if (keptwire::EncodeLaid(&b, 3, keptwire::kPartItems, keptwire::kHowItemsHas, std::string(48, 'k'))) Fail("KEPT: a live key past 47 bytes is sent");
    if (keptwire::EncodeLaid(&b, 3, keptwire::kPartItems, keptwire::kHowItemsHas, "a\tb")) Fail("KEPT: a live key with a control character is sent");
    if (!keptwire::EncodeState(&b, 9, keptwire::kFlagOwnerTook | 2u, 3, 0) || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.op != keptwire::kOpState || m.row != 9
        || m.flags != (keptwire::kFlagOwnerTook | 2u) || m.claimPiece != 3 || m.claimItems != 0) Fail("KEPT: STATE does not round-trip");
    if (keptwire::EncodeState(&b, 9, 0x100u, 0, 0)) Fail("KEPT: a STATE with unknown flag bits is sent");

    std::vector<keptwire::Row> rows; rows.push_back(T36b2Row(0));
    keptwire::Row r2 = T36b2Row(5); r2.p.items.clear(); r2.p.mats.clear(); r2.p.hostKey = "4092-TwoStorey.mod@52,52@949000,953000,30770"; r2.claimPiece = 2;
    r2.flags = keptwire::SetPartState(0, keptwire::kPartPiece, keptwire::kStClaimed); rows.push_back(r2);
    if (!keptwire::EncodeRows(&b, 52, 52, rows) || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.op != keptwire::kOpRows || m.sx != 52 || m.sy != 52 || m.rows.size() != 2)
    { Fail("KEPT: ROWS does not round-trip"); return; }
    if (T36b2Pieces(m.rows) != T36b2Pieces(rows) || m.rows[1].row != 5 || m.rows[1].claimPiece != 2 || m.rows[1].flags != r2.flags || m.rows[0].p.ownerSlot != 2)
        Fail("KEPT: a ROWS row comes back with another field");
    const std::vector<char> good = b;
    std::vector<keptwire::Row> one(1, T36b2Row(0));
    if (!keptwire::EncodeRows(&b, 52, 52, one) || b.size() != 16 + keptwire::RowWireSize(one[0])) Fail("KEPT: RowWireSize is not the bytes a row takes");
    if (keptwire::EncodeRows(&b, 52, 53, rows)) Fail("KEPT: a row of another sector is sent in a sector's ROWS");
    if (keptwire::EncodeRows(&b, 52, 52, std::vector<keptwire::Row>())) Fail("KEPT: an empty ROWS is sent");
    if (keptwire::EncodeRows(&b, 52, 52, std::vector<keptwire::Row>(keptwire::kMaxRows + 1, T36b2Row(0)))) Fail("KEPT: a ROWS of 65 rows is sent");
    if (!keptwire::EncodeRows(&b, 52, 52, std::vector<keptwire::Row>(keptwire::kMaxRows, T36b2Row(0))) || keptwire::Decode(&b[0], b.size(), &m) != 1) Fail("KEPT: a ROWS of 64 rows is refused");
    keptwire::Row bad = T36b2Row(0); bad.p.key.clear();
    if (keptwire::RowOk(bad)) Fail("KEPT: a row with no key is valid");
    bad = T36b2Row(0); bad.p.pos[0] = std::numeric_limits<float>::quiet_NaN();
    if (keptwire::RowOk(bad)) Fail("KEPT: a row at a NaN position is valid");
    bad = T36b2Row(0); bad.p.items.resize(keptwire::kMaxItems + 1, bad.p.items[0]);
    if (keptwire::RowOk(bad)) Fail("KEPT: a row of 513 items is valid");
    bad = T36b2Row(0); bad.p.ownerSlot = -1;
    if (keptwire::RowOk(bad)) Fail("KEPT: a row no player owns is valid");
    bad = T36b2Row(0); bad.p.complete = 2;
    if (keptwire::RowOk(bad)) Fail("KEPT: a complete flag of 2 is valid");
    for (size_t cut = 0; cut < good.size(); ++cut) if (keptwire::Decode(&good[0], cut, &m) != 0 || !m.rows.empty()) { Fail("KEPT: a ROWS cut to " + I((int)cut) + " bytes is read"); break; }
    std::vector<char> longer = good; longer.push_back(0);
    if (keptwire::Decode(&longer[0], longer.size(), &m) != 0) Fail("KEPT: a trailing byte is read");
    std::vector<char> huge;
    keptwire::PutU32(&huge, keptwire::kOpRows); keptwire::PutI32(&huge, 52); keptwire::PutI32(&huge, 52); keptwire::PutU32(&huge, 1);
    keptwire::Row h = T36b2Row(0); h.p.items.clear();
    keptwire::PutRow(&huge, h); huge.resize(huge.size() - 4);   /* the item count of 0 off: a count of 100000 with no bytes behind it instead */
    keptwire::PutU32(&huge, 100000u);
    if (keptwire::Decode(&huge[0], huge.size(), &m) != 0) Fail("KEPT: an item count past the bytes left is read");
    std::vector<char> laidBad;
    keptwire::PutU32(&laidBad, keptwire::kOpLaid); keptwire::PutU32(&laidBad, 1); keptwire::PutU32(&laidBad, keptwire::kPartItems); keptwire::PutU32(&laidBad, keptwire::kHowMade); keptwire::PutU32(&laidBad, 0);
    if (keptwire::Decode(&laidBad[0], laidBad.size(), &m) != 0) Fail("KEPT: a LAID whose how does not fit its part is read");
    const char junk[4] = { 9, 0, 0, 0 };
    if (keptwire::Decode(junk, 4, &m) != 0 || keptwire::Decode(0, 0, &m) != 0) Fail("KEPT: an unknown op or an empty message is read");
}
void t_t36b2_decisions()
{
    using namespace keptwire;
    ServerRow r; int res = 0, re = 0;
    /* the area's holder is slot 1 (who 2); the piece's owner is slot 2 (who 3) */
    if (Claim(&r, kPartPiece, 1, 1, 2, &res) || res != kClaimNotHolder) Fail("KEPT: the piece is given to a game that does not hold the area");
    if (Claim(&r, kPartPiece, 3, 1, 2, &res) || res != kClaimNotHolder) Fail("KEPT: the piece is given to its owner's game that does not hold the area");
    if (!Claim(&r, kPartPiece, 2, 1, 2, &res) || res != kClaimGrant || PartState(r.flags, kPartPiece) != kStClaimed || r.piece.claim != 2) Fail("KEPT: the holder is not given the piece");
    if (Claim(&r, kPartPiece, 2, 1, 2, &res) || res != kClaimGrant) Fail("KEPT: the holder asking again is not still granted");
    if (Claim(&r, kPartPiece, 3, 2, 2, &res) || res != kClaimHeld) Fail("KEPT: a claimed piece is given to a second game");
    if (Claim(&r, kPartOwner, 3, 2, 2, &res) || res != kClaimBadPart) Fail("KEPT: the owner part is claimed");
    if (!Claim(&r, kPartItems, 3, 1, 2, &res) || res != kClaimGrant) Fail("KEPT: the owner's game is not given the items");
    if (Release(&r, kPartItems, 2)) Fail("KEPT: a game that does not hold the claim releases it");
    if (!Release(&r, kPartItems, 3) || PartState(r.flags, kPartItems) != kStNew || r.items.claim != 0) Fail("KEPT: a release does not make the items new");
    if (Laid(&r, kPartPiece, kHowMade, 3, 0, 2) != kLaidNotHolder) Fail("KEPT: a lay from a game without the claim is taken");
    if (Laid(&r, kPartPiece, kHowItemsLaid, 2, 1, 2) != kLaidBad) Fail("KEPT: a lay whose how does not fit is taken");
    if (Laid(&r, kPartPiece, kHowRefused, 2, 1, 2) != kLaidRefusedBack || PartState(r.flags, kPartPiece) != kStNew || r.piece.claim != 0) Fail("KEPT: a refused lay does not hand the piece back");
    Claim(&r, kPartPiece, 2, 1, 2, &res);
    if (Laid(&r, kPartPiece, kHowMade, 2, 1, 2) != kLaidTaken || PartState(r.flags, kPartPiece) != kStLaid || (r.flags & kFlagPieceLaidNonWriter) != 0 || r.piece.laidBy != 2)
        Fail("KEPT: the holder's lay is not taken");
    if (Laid(&r, kPartPiece, kHowMade, 2, 1, 2) != kLaidRepeat) Fail("KEPT: a repeated lay is not a repeat");
    if (!PeerGone(&r, 2, &re) || re != 1 || PartState(r.flags, kPartPiece) != kStNew) Fail("KEPT: a holder that leaves before its save does not give its lay back");
    Claim(&r, kPartItems, 3, 1, 2, &res);
    if (Laid(&r, kPartItems, kHowItemsLaid, 3, 0, 2) != kLaidTaken || (r.flags & kFlagItemsLaidNonWriter) == 0) Fail("KEPT: a lay by a game that does not hold the area is not marked");
    re = 0;
    if (!PeerGone(&r, 3, &re) || re != 0 || PartState(r.flags, kPartItems) != kStLaid || r.items.laidBy != 0) Fail("KEPT: a non-holder's lay is offered again when it leaves");
    if (Claim(&r, kPartItems, 2, 1, 2, &res) || res != kClaimNotNew) Fail("KEPT: a part a non-holder laid is claimed again");
    if (Settled(&r, kPartItems, 3) != kSettledDone || PartState(r.flags, kPartItems) != kStDone || (r.flags & kFlagItemsLaidNonWriter) != 0)
        Fail("KEPT: a settle after a relink (nobody known to have laid) does not close the items");
    if (Settled(&r, kPartItems, 3) != kSettledRepeat) Fail("KEPT: a second settle is not a repeat");
    Claim(&r, kPartPiece, 2, 1, 2, &res); Laid(&r, kPartPiece, kHowPresent, 2, 1, 2);
    if (Settled(&r, kPartPiece, 3) != kSettledRefused || PartState(r.flags, kPartPiece) != kStLaid) Fail("KEPT: a game closes a part another connected game laid");
    if (Settled(&r, kPartOwner, 3) != kSettledRefused) Fail("KEPT: the owner part is settled");
    if (RowDone(r.flags) != 0) Fail("KEPT: a row with a laid piece is done");
    if (!MarkDone(&r, kPartPiece) || MarkDone(&r, kPartPiece) || r.piece.laidBy != 0) Fail("KEPT: the record does not close the piece once");
    if (Laid(&r, kPartOwner, kHowOwnerRegistered, 2, 1, 2) != kLaidNotHolder) Fail("KEPT: a game that does not own the piece takes the owner part");
    if (Laid(&r, kPartOwner, kHowOwnerRegistered, 3, 0, 2) != kLaidOwnerTook || RowDone(r.flags) != 1) Fail("KEPT: the owner's registration does not finish the row");
    if (Laid(&r, kPartOwner, kHowOwnerRegistered, 3, 0, 2) != kLaidRepeat) Fail("KEPT: a second registration is not a repeat");
    ServerRow q;
    Claim(&q, kPartPiece, 5, 4, 0, &res); re = 0;
    if (!PeerGone(&q, 5, &re) || re != 0 || PartState(q.flags, kPartPiece) != kStNew || q.piece.claim != 0) Fail("KEPT: a claim is not handed back when its game leaves");
    if (PeerGone(&q, 0, &re) || PeerGone(&q, 5, &re)) Fail("KEPT: a game with no claim and no lay changes a row when it leaves");
    zonekeep::KeptState s; s.pieceLaidNonWriter = 1; s.ownerTook = 1;
    const unsigned f = FlagsOfState(s, 1);
    if (PartState(f, kPartPiece) != kStLaid || (f & kFlagPieceLaidNonWriter) == 0 || PartState(f, kPartItems) != kStNew || (f & kFlagOwnerTook) == 0) Fail("KEPT: a state line does not give its flags");
    if (PartState(FlagsOfState(zonekeep::KeptState(), 0), kPartItems) != kStDone) Fail("KEPT: a piece that kept no items does not have its items done");
    const zonekeep::KeptState back = StateOfFlags(f);
    if (back.pieceLaidNonWriter != 1 || back.ownerTook != 1 || back.pieceDone != 0 || back.itemsDone != 0) Fail("KEPT: flags do not give their state line");
    const zonekeep::KeptState cl = StateOfFlags(SetPartState(0, kPartPiece, kStClaimed));
    if (cl.pieceDone != 0 || cl.pieceLaidNonWriter != 0) Fail("KEPT: a claim is written to the state file");
    if (AreaIdOfSector(52, 52) != "zone.52.52" || AreaIdOfSector(3, 41) != "zone.3.41") Fail("KEPT: a sector's area record id");
}
void t_t36gaps_fold4()
{
    using namespace keptwire;
    std::string why;
    /* the claim field a part carries: the claim's holder; while laid, its layer */
    ServerRow r;
    int res = 0;
    if (ClaimFieldOf(r, kPartItems) != 0) Fail("fold4: a new part carries a claim");
    Claim(&r, kPartItems, 2, 1, 5, &res);
    if (res != kClaimGrant || ClaimFieldOf(r, kPartItems) != 2) Fail("fold4: a claim's holder is not carried");
    if (Laid(&r, kPartItems, kHowItemsLaid, 2, 1, 5) != kLaidTaken || ClaimFieldOf(r, kPartItems) != 2 || PartState(r.flags, kPartItems) != kStLaid) Fail("fold4: a laid part does not carry its layer");
    /* a claim again of its own lay: only its layer, under the holder / owner rule */
    if (ClaimDecide(kPartItems, r.flags, 0, 3, 2, 5, r.items.laidSeen) != kClaimNotNew) Fail("fold4: another game claims a laid part");
    if (ClaimDecide(kPartItems, r.flags, 0, 2, 7, 6, r.items.laidSeen) != kClaimNotHolder) Fail("fold4: a claim again past the holder / owner rule");
    if (ClaimDecide(kPartPiece, SetPartState(0, kPartPiece, kStLaid), 0, 2, 7, 1, 2) != kClaimNotHolder) Fail("fold4: a piece is claimed again by a game that does not hold the area");
    if (!Claim(&r, kPartItems, 2, 1, 5, &res) || res != kClaimAgain || PartState(r.flags, kPartItems) != kStClaimed || r.items.claim != 2 || r.items.again != 1)
        Fail("fold4: its layer does not claim its lay again");
    if (Settled(&r, kPartItems, 2) != kSettledRefused) Fail("fold4: a claim again is settled");
    if (!Release(&r, kPartItems, 2) || PartState(r.flags, kPartItems) != kStLaid || r.items.again != 0 || ClaimFieldOf(r, kPartItems) != 2) Fail("fold4: a claim again handed back is not laid again");
    Claim(&r, kPartItems, 2, 1, 5, &res);
    if (Laid(&r, kPartItems, kHowRefused, 2, 1, 5) != kLaidRefusedBack || PartState(r.flags, kPartItems) != kStLaid) Fail("fold4: a claim again refused is not laid again");
    Claim(&r, kPartItems, 2, 1, 5, &res);
    int re = 0;
    if (!PeerGone(&r, 2, &re) || re != 0 || PartState(r.flags, kPartItems) != kStLaid || r.items.laidBy != 0 || r.items.laidSeen != 2 || r.items.claim != 0)
        Fail("fold4: a claim again whose game left is not laid by nobody");
    Claim(&r, kPartItems, 2, 1, 5, &res);
    if (res != kClaimAgain || Laid(&r, kPartItems, kHowItemsLaid, 2, 1, 5) != kLaidTaken || PartState(r.flags, kPartItems) != kStLaid || r.items.laidBy != 2 || r.items.again != 0)
        Fail("fold4: a lay once more is not taken");
    if (!MarkDone(&r, kPartItems) || ClaimFieldOf(r, kPartItems) != 0 || Claim(&r, kPartItems, 2, 1, 5, &res) || res != kClaimNotNew) Fail("fold4: a done part is claimed again");
    /* the layer of each part kept laid is kept in kept-state.txt (format 3); formats 1 and 2 are read too */
    ServerRow q;
    Claim(&q, kPartItems, 4, 3, 0, &res); Laid(&q, kPartItems, kHowItemsLaid, 4, 1, 0);
    Claim(&q, kPartPiece, 4, 3, 0, &res); Laid(&q, kPartPiece, kHowMade, 4, 0, 0);   /* the area's map named another holder at the lay: kept laid */
    std::vector<zonekeep::KeptState> sv(2), back;
    sv[1] = StateOfRow(q);
    if (sv[1].itemsLaidBy != 4 || sv[1].pieceLaidBy != 4) Fail("fold4: a part kept laid does not keep its layer");
    const std::string txt = zonekeep::EncodeKeptState(sv);
    if (txt.find("kept-state\t3\n") != 0 || !zonekeep::DecodeKeptState(txt, 2, &back, &why) || back[1].itemsLaidBy != 4 || back[1].pieceLaidBy != 4 || zonekeep::EncodeKeptState(back) != txt)
        Fail("fold4: the format 3 state does not round-trip: " + why);
    ServerRow q2;
    RowOfState(back[1], 1, &q2);
    if (PartState(q2.flags, kPartItems) != kStLaid || q2.items.laidSeen != 4 || q2.items.laidBy != 0 || ClaimFieldOf(q2, kPartItems) != 4 || q2.piece.laidSeen != 4 || q2.pieceFirstHow != kHowMade)
        Fail("fold4: a restart does not keep each part's layer");
    if (ClaimDecide(kPartItems, q2.flags, 0, 4, 3, 0, q2.items.laidSeen) != kClaimAgain) Fail("fold4: its layer cannot claim its lay again after a restart");
    if (!zonekeep::DecodeKeptState("kept-state\t2\nv2\t0\t0\t0\t0\t0\t1\t0\n", 1, &back, &why) || back[0].itemsLaidNonWriter != 1 || back[0].itemsLaidBy != 0)
        Fail("fold4: a format 2 state is not read: " + why);
    if (zonekeep::DecodeKeptState("kept-state\t3\nv3\t0\t0\t0\t0\t0\t1\t0\t0\t70000\n", 1, &back, &why)
        || zonekeep::DecodeKeptState("kept-state\t3\nv2\t0\t0\t0\t0\t0\t1\t0\n", 1, &back, &why) || zonekeep::DecodeKeptState("kept-state\t4\n", 1, &back, &why))
        Fail("fold4: an unusable state is read");
    ServerRow h;
    Claim(&h, kPartPiece, 2, 1, 0, &res); Laid(&h, kPartPiece, kHowMade, 2, 1, 0);
    if (StateOfRow(h).pieceLaidBy != 0) Fail("fold4: a holder's lay (new again at a restart) keeps its layer");
    /* the game's step for a laid part */
    const unsigned lf = SetPartState(0, kPartItems, kStLaid);
    if (PartStep(lf, kPartItems, 3, 2, 1, 0, 1) != kStepClaim || PartStep(lf, kPartItems, 3, 2, 1, 0, 0) != kStepNone || PartStep(lf, kPartItems, 3, 2, 1, 1, 1) != kStepNone
        || PartStep(lf, kPartItems, 4, 2, 1, 0, 1) != kStepNone || PartStep(lf, kPartItems, 3, 2, 0, 0, 1) != kStepNone || PartStep(lf, kPartItems, 3, 2, 1, 0) != kStepNone)
        Fail("fold4: PartStep for a laid part");
    /* the laid-here record: format 2 with the waiting items, format 1 read too */
    std::map<unsigned, HereRow> hm, hb;
    HereRow a, b;
    a.itemsHow = kHowItemsLaid; a.waits.push_back("3"); a.waits.push_back("3.1.0"); a.waits.push_back("x3.0");
    b.pieceHow = kHowMade;
    hm[2] = a; hm[5] = b;
    const std::string wid("w1");
    const std::string ht = EncodeHere(wid, hm);
    if (ht.find("kept-laid-here\t2\t") != 0 || ht.find("\t3,3.1.0,x3.0\n") == std::string::npos || ht.find("\t-\n") == std::string::npos) Fail("fold4: the record's text: " + ht);
    if (!DecodeHere(ht, wid, &hb, &why) || hb.size() != 2 || HereRowSame(hb[2], a) == 0 || HereRowSame(hb[5], b) == 0 || EncodeHere(wid, hb) != ht) Fail("fold4: the record does not round-trip: " + why);
    int other = 0;
    if (DecodeHere(ht, "w2", &hb, &why, &other) || other != 1 || !hb.empty()) Fail("fold4: another world's record is not told apart");
    if (DecodeHere("junk\n", wid, &hb, &why, &other) || other != 0) Fail("fold4: an unreadable record is taken for another world's");
    if (!DecodeHere(std::string("kept-laid-here\t1\t") + HexOf(wid) + "\nh\t1\t1\t0\t0\t0\t0\n", wid, &hb, &why) || hb[1].pieceHow != kHowMade || !hb[1].waits.empty())
        Fail("fold4: a format 1 record is not read: " + why);
    const std::string h2 = std::string("kept-laid-here\t2\t") + HexOf(wid) + "\n";
    if (DecodeHere(h2 + "h\t1\t0\t4\t0\t0\t0\t3,y\n", wid, &hb, &why) || DecodeHere(h2 + "h\t1\t0\t4\t0\t0\t0\n", wid, &hb, &why)
        || DecodeHere(h2 + "h\t1\t0\t4\t0\t0\t0\t\n", wid, &hb, &why) || DecodeHere(h2 + "h\t1\t0\t4\t0\t0\t0\t1,,2\n", wid, &hb, &why))
        Fail("fold4: a bad waiting list is read");
    if (HereRowEmpty(HereRow()) != 1) Fail("fold4: an empty row");
    HereRow wo; wo.waits.push_back("0");
    if (HereRowEmpty(wo) != 0 || HereRowSame(wo, HereRow()) != 0) Fail("fold4: a row with only waiting items is empty");
    /* item paths */
    if (!ItemPathOk("0") || !ItemPathOk("511.3.0.2.1") || ItemPathOk("") || ItemPathOk("1..2") || ItemPathOk("512") || ItemPathOk("1.2.3.4.5.6") || ItemPathOk("a")
        || ItemPathOk("1.") || ItemPathOk(".1") || ItemPathOk("0001")) Fail("fold4: ItemPathOk");
    if (!HereTokenOk("x2.1") || HereTokenOk("x") || HereTokenOk("xx1") || HereTokenOk("")) Fail("fold4: HereTokenOk");
    if (ItemPathChild("", 7) != "7" || ItemPathChild("3.1", 0) != "3.1.0") Fail("fold4: ItemPathChild");
    std::vector<zonekeep::KeptItem> items(2);
    items[0].tmpl = "a"; items[1].tmpl = "pack";
    zonekeep::KeptItem in;
    in.tmpl = "b"; items[1].inner.push_back(in);
    in.tmpl = "c"; items[1].inner.push_back(in);
    if (ItemAtPath(items, "1.1") == 0 || ItemAtPath(items, "1.1")->tmpl != "c" || ItemAtPath(items, "2") != 0 || ItemAtPath(items, "0.0") != 0 || ItemAtPath(items, "1")->tmpl != "pack")
        Fail("fold4: ItemAtPath");
    std::vector<std::string> tk;
    tk.push_back("1"); tk.push_back("x1.0");
    if (!HereNamed(tk, "1.0") || HereNamed(tk, "1.1") || !HereNamed(tk, "1") || HereNamed(tk, "x1")) Fail("fold4: HereNamed");
    std::vector<std::string> wt, el;
    wt.push_back("1"); wt.push_back("4.2");
    el.push_back("1.0"); el.push_back("2.1"); el.push_back("4.2.3"); el.push_back("11.0"); el.push_back("4.20");
    const std::vector<std::string> got = HereWaitTokens(wt, el);
    if (got.size() != 4 || got[0] != "1" || got[1] != "4.2" || got[2] != "x1.0" || got[3] != "x4.2.3") Fail("fold4: HereWaitTokens");
    /* when a noted change goes into the record */
    if (HereFinishAction(3, 0, -1, 4) != kHereCommit || HereFinishAction(3, 0, -1, 3) != kHereKeep || HereFinishAction(3, 1, 5, 5) != kHereCommit
        || HereFinishAction(3, 1, 5, 6) != kHereDrop || HereFinishAction(4, 1, 5, 4) != kHereKeep || HereFinishAction(9, 0, 5, 6) != kHereKeep || HereFinishAction(9, 0, 5, 10) != kHereCommit)
        Fail("fold4: HereFinishAction");
    if (HereTeardownKeeps(4, 5) != 1 || HereTeardownKeeps(5, 5) != 0 || HereEventHeld(2, 3) != 1 || HereEventHeld(3, 3) != 0) Fail("fold4: HereTeardownKeeps / HereEventHeld");
}
void t_t36gaps_fold3()
{
    /* the piece that already stood: the first lay's how is kept, and only a first 'present' marks the row */
    keptwire::ServerRow r;
    int res = 0, re = 0;
    keptwire::Claim(&r, keptwire::kPartPiece, 2, 1, 1, &res);
    if (keptwire::Laid(&r, keptwire::kPartPiece, keptwire::kHowPresent, 2, 1, 1) != keptwire::kLaidTaken || (r.flags & keptwire::kFlagPiecePresent) == 0
        || r.pieceFirstHow != keptwire::kHowPresent) Fail("fold3: a first lay that found the piece standing does not mark the row present");
    if (keptwire::ExtrasLayHere(r.flags, 0) != 0) Fail("fold3: a present row's own state is laid");
    std::vector<char> b;
    if (!keptwire::EncodeState(&b, 0, r.flags, 0, 0)) Fail("fold3: a STATE with the present flag does not encode");
    keptwire::ServerRow m;
    keptwire::Claim(&m, keptwire::kPartPiece, 2, 1, 1, &res);
    keptwire::Laid(&m, keptwire::kPartPiece, keptwire::kHowMade, 2, 1, 1);
    keptwire::PeerGone(&m, 2, &re);
    keptwire::Claim(&m, keptwire::kPartPiece, 3, 2, 1, &res);
    if (keptwire::Laid(&m, keptwire::kPartPiece, keptwire::kHowPresent, 3, 1, 1) != keptwire::kLaidTaken || (m.flags & keptwire::kFlagPiecePresent) != 0
        || m.pieceFirstHow != keptwire::kHowMade) Fail("fold3: a piece made, offered again and found standing is marked present");
    if (keptwire::ExtrasLayHere(m.flags, 0) != 1 || keptwire::ExtrasLayHere(m.flags, keptwire::kHowPresent) != 0) Fail("fold3: ExtrasLayHere");
    /* the state file keeps the first how (format 3); format 1 still reads */
    const zonekeep::KeptState s = keptwire::StateOfRow(r);
    if (s.pieceFirstHow != (int)keptwire::kHowPresent || (keptwire::FlagsOfState(s, 0) & keptwire::kFlagPiecePresent) == 0) Fail("fold3: the first how does not reach the state line or back");
    std::vector<zonekeep::KeptState> v(2);
    v[1] = s;
    const std::string t = zonekeep::EncodeKeptState(v);
    std::vector<zonekeep::KeptState> back;
    std::string why;
    if (t.compare(0, 13, "kept-state\t3\n") != 0 || !zonekeep::DecodeKeptState(t, 2, &back, &why) || back[1].pieceFirstHow != (int)keptwire::kHowPresent || back[0].pieceFirstHow != 0)
        Fail("fold3: the state does not round-trip: " + why);
    if (!zonekeep::DecodeKeptState("kept-state\t1\nv1\t1\t0\t0\t0\t1\t0\n", 2, &back, &why) || back[1].pieceLaidNonWriter != 1 || back[1].pieceFirstHow != 0)
        Fail("fold3: a format 1 state is not read");
    if (zonekeep::DecodeKeptState("kept-state\t2\nv2\t1\t0\t0\t0\t0\t0\t4\n", 2, &back, &why)) Fail("fold3: a first how past 3 is read");
    if (zonekeep::DecodeKeptState("kept-state\t2\nv1\t1\t0\t0\t0\t0\t0\n", 2, &back, &why)) Fail("fold3: a format 1 line under a format 2 header is read");
    /* own state parts */
    if (keptwire::ExtrasFinished(keptwire::kXName | keptwire::kXDoors, keptwire::kXName, 0) != 0 || keptwire::ExtrasFinished(keptwire::kXName | keptwire::kXDoors, keptwire::kXName, keptwire::kXDoors) != 1
        || keptwire::ExtrasFinished(0, 0, 0) != 1) Fail("fold3: ExtrasFinished");
    if (keptwire::ExtrasNames(keptwire::kXPower | keptwire::kXFarm) != "power, farm" || keptwire::ExtrasNames(0) != "none") Fail("fold3: ExtrasNames");
    /* the verified class sets: each production class is in all three, each other useable class in ClassUseable and ClassBuilding,
       each other building class in ClassBuilding only, anything else in none */
    {
        const char* const cProd[] = { ".?AVProductionBuilding@@", ".?AVFarmBuilding@@", ".?AVGeneratorBuilding@@", ".?AVWindGeneratorBuilding@@",
                                      ".?AVCraftingBuilding@@", ".?AVFurnaceBuilding@@", ".?AVRainCollectorBuilding@@", ".?AVTortureBuilding@@" };
        const char* const cUse[] = { ".?AVUseableStuff@@", ".?AVStorageBuilding@@", ".?AVLightBuilding@@", ".?AVResearchBuilding@@", ".?AVTurretBuilding@@" };
        const char* const cBld[] = { ".?AVBuilding@@", ".?AVGatewayBuilding@@", ".?AVDoorStuff@@", ".?AVWallBuilding@@" };
        const char* const cNone[] = { "", ".?AVCharacter@@", "FarmBuilding", ".?AVFarmBuilding@@ ", ".?AVProductionBuildingX@@", ".?AVFarm@@", ".?AVBuildingX@@" };
        for (size_t i = 0; i < sizeof(cProd) / sizeof(cProd[0]); ++i)
            if (keptwire::ClassProduction(cProd[i]) != 1 || keptwire::ClassUseable(cProd[i]) != 1 || keptwire::ClassBuilding(cProd[i]) != 1) Fail(std::string("t36classes: production ") + cProd[i]);
        for (size_t i = 0; i < sizeof(cUse) / sizeof(cUse[0]); ++i)
            if (keptwire::ClassProduction(cUse[i]) != 0 || keptwire::ClassUseable(cUse[i]) != 1 || keptwire::ClassBuilding(cUse[i]) != 1) Fail(std::string("t36classes: useable ") + cUse[i]);
        for (size_t i = 0; i < sizeof(cBld) / sizeof(cBld[0]); ++i)
            if (keptwire::ClassProduction(cBld[i]) != 0 || keptwire::ClassUseable(cBld[i]) != 0 || keptwire::ClassBuilding(cBld[i]) != 1) Fail(std::string("t36classes: building ") + cBld[i]);
        for (size_t i = 0; i < sizeof(cNone) / sizeof(cNone[0]); ++i)
            if (keptwire::ClassProduction(cNone[i]) != 0 || keptwire::ClassUseable(cNone[i]) != 0 || keptwire::ClassBuilding(cNone[i]) != 0) Fail(std::string("t36classes: other '") + cNone[i] + "'");
        if (keptwire::ClassProduction(0) != 0 || keptwire::ClassUseable(0) != 0 || keptwire::ClassBuilding(0) != 0) Fail("t36classes: null");
    }
    /* input stocks: laid only when every kept index has its record */
    std::vector<zonekeep::KeptStock> in(2);
    in[0].index = 0; in[0].amount = 1.0f; in[1].index = 3; in[1].amount = 2.0f;
    if (keptwire::InputsAllFit(in, 3) != 0 || keptwire::InputsAllFit(in, 4) != 1 || keptwire::InputsAllFit(in, -1) != 0 || keptwire::InputsAllFit(std::vector<zonekeep::KeptStock>(), 0) != 1)
        Fail("fold3: InputsAllFit");
    /* door states: opening is laid open, closing closed */
    if (keptwire::KeptDoorOpen(0) != 0 || keptwire::KeptDoorOpen(1) != 1 || keptwire::KeptDoorOpen(2) != 1 || keptwire::KeptDoorOpen(3) != 0) Fail("fold3: KeptDoorOpen");
    /* this game's record of what it laid */
    std::map<unsigned, keptwire::HereRow> hm, hb;
    keptwire::HereRow h, h2;
    h.pieceHow = keptwire::kHowMade; h.extrasDone = keptwire::kXName | keptwire::kXDoors; h.extrasEnded = keptwire::kXFarm; h.doorsDone = 0x80000001u;
    h2.itemsHow = keptwire::kHowItemsLaid;
    hm[4] = h; hm[9] = h2;
    const std::string wid("\x01\xfe" "ab", 4);
    const std::string ht = keptwire::EncodeHere(wid, hm);
    if (!keptwire::DecodeHere(ht, wid, &hb, &why) || hb.size() != 2 || hb[4].pieceHow != keptwire::kHowMade || hb[4].doorsDone != 0x80000001u || hb[4].extrasEnded != keptwire::kXFarm
        || hb[9].itemsHow != keptwire::kHowItemsLaid || keptwire::EncodeHere(wid, hb) != ht) Fail("fold3: the record does not round-trip: " + why);
    if (keptwire::DecodeHere(ht, "other", &hb, &why) || !hb.empty()) Fail("fold3: another world's record is read");
    const std::string head = std::string("kept-laid-here\t1\t") + keptwire::HexOf(wid) + "\n";
    if (keptwire::DecodeHere(head + "h\t1\t4\t0\t0\t0\t0\n", wid, &hb, &why) || keptwire::DecodeHere(head + "h\t1\t0\t0\t512\t0\t0\n", wid, &hb, &why)
        || keptwire::DecodeHere(head + "h\t1\t1\t0\t0\t0\t0\nh\t1\t1\t0\t0\t0\t0\n", wid, &hb, &why) || keptwire::DecodeHere("", wid, &hb, &why))
        Fail("fold3: an unusable record is read");
    if (!keptwire::DecodeHere(head, wid, &hb, &why) || !hb.empty()) Fail("fold3: an empty record is refused");
    /* items that wait for a place */
    if (keptwire::KeptWaitEnds(keptwire::kKeptItemWaitPasses - 1) != 0 || keptwire::KeptWaitEnds(keptwire::kKeptItemWaitPasses) != 1) Fail("fold3: KeptWaitEnds");
    /* one bad field never drops a row */
    zonekeep::KeptPiece p;
    p.ownerSlot = 0; p.key = "k@1@1,1,1"; p.tmpl = "t.mod";
    zonekeep::KeptDoor d;
    d.state = 7;
    p.doors.push_back(d);
    zonekeep::KeptItem pack, one;
    pack.tmpl = "pack.mod"; one.tmpl = "x.mod";
    pack.inner.assign(300, one);
    p.items.push_back(pack);
    zonekeep::KeepStats fs;
    zonekeep::FitPiece(&p, &fs);
    if (p.doors[0].state != 0 || fs.fieldsFixed != 1 || p.items.size() != 45 || p.items[0].inner.size() != 256 || fs.packSplit != 44 || fs.itemsOverCap != 0)
        Fail("fold3: FitPiece did not clamp the door state and move the pack's excess to the box (" + I(fs.fieldsFixed) + " fixed, " + I(fs.packSplit) + " split, "
             + I((int)p.items.size()) + " box items)");
    keptwire::Row w;
    w.p = p;
    if (!keptwire::RowOk(w)) Fail("fold3: a fitted row is not valid");
    zonekeep::KeptPiece big;
    big.ownerSlot = 0; big.key = "k@1@1,1,1"; big.tmpl = "t.mod";
    big.items.assign(600, one);
    zonekeep::KeepStats fb;
    zonekeep::FitPiece(&big, &fb);
    if (big.items.size() != 512 || fb.itemsOverCap != 88) Fail("fold3: box items past the row's cap are not counted");
}
void t_t36b2_state_file()
{
    std::vector<zonekeep::KeptState> s(3); s[0].pieceDone = 1; s[0].itemsDone = 1; s[0].ownerTook = 1; s[2].itemsLaidNonWriter = 1;
    const std::string t = zonekeep::EncodeKeptState(s);
    if (t.find('\r') != std::string::npos || t.compare(0, 13, "kept-state\t3\n") != 0) Fail("state: the header is not 'kept-state<TAB>3' or a CR is written");
    std::vector<zonekeep::KeptState> back; std::string why;
    if (!zonekeep::DecodeKeptState(t, 3, &back, &why) || back.size() != 3 || zonekeep::EncodeKeptState(back) != t) Fail("state: its own text does not round-trip: " + why);
    std::string crlf;
    for (size_t i = 0; i < t.size(); ++i) { if (t[i] == '\n') crlf += '\r'; crlf += t[i]; }
    if (!zonekeep::DecodeKeptState(crlf, 3, &back, &why) || zonekeep::EncodeKeptState(back) != t) Fail("state: CR LF line ends are refused");
    if (!zonekeep::DecodeKeptState("kept-state\t1\nv1\t1\t1\t0\t0\t0\t0\n", 3, &back, &why) || back.size() != 3 || back[1].pieceDone != 1 || back[0].pieceDone != 0 || back[2].pieceDone != 0)
        Fail("state: a row with no line is not new");
    const char* refused[] = { "kept-state\t1\nv1\t3\t0\t0\t0\t0\t0\n", "kept-state\t1\nv1\t1\t0\t0\t0\t0\t0\nv1\t1\t0\t0\t0\t0\t0\n",
                              "kept-state\t1\nv1\t1\t2\t0\t0\t0\t0\n", "kept-state\t1\nv1\t1\t-1\t0\t0\t0\t0\n", "kept-state\t1\nv1\t1\t0\t0\t0\t0\n",
                              "kept-state\t1\nv2\t1\t0\t0\t0\t0\t0\n", "kept-state\t4\n", "kept-pieces\t1\n", "" };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
        if (zonekeep::DecodeKeptState(refused[i], 3, &back, &why) || why.empty() || back.size() != 3 || back[0].pieceDone != 0 || back[1].pieceDone != 0)
            Fail("state: unusable text " + I((int)i) + " is read, or leaves other than three new rows");
    if (!zonekeep::DecodeKeptState("kept-state\t1\n", 0, &back, &why) || !back.empty()) Fail("state: an empty state for an empty list is refused");
}
void t_t36b2_record_holds()
{
    std::string b; int w = -1;
    if (!T36b1Load("zone.52.52", &b, &w)) { Fail("data\\zone.52.52.platoon is missing (the suite runs from src\\coop-test)"); return; }
    std::vector<zonekeep::KeptPiece> inRecord; std::string why;
    if (!zonekeep::PiecesOfRecord((const unsigned char*)b.data(), b.size(), w, &inRecord, &why)) { Fail("holds: zone.52.52 is not read: " + why); return; }
    const zonekeep::KeptPiece* chest = T36b1Find(inRecord, "1459-gamedata.base@52,52@949500,953000,30770");
    if (chest == 0) { Fail("holds: the chest is not among zone.52.52's pieces"); return; }
    zonekeep::KeptPiece want = *chest;
    if (!zonekeep::RecordHoldsPiece(inRecord, want)) Fail("holds: the record does not hold its own chest");
    want.pos[0] += 0.1f; want.pos[2] -= 0.1f; want.pos[1] += 5.0f;
    if (!zonekeep::RecordHoldsPiece(inRecord, want)) Fail("holds: a chest one tenth away (and at another height) is not held");
    want = *chest; want.pos[0] += 0.3f;
    if (zonekeep::RecordHoldsPiece(inRecord, want)) Fail("holds: a chest three tenths away is held");
    want = *chest; want.ownerSlot = 1;
    if (zonekeep::RecordHoldsPiece(inRecord, want)) Fail("holds: another player's chest is held");
    want = *chest; want.tmpl = "1460-gamedata.base";
    if (zonekeep::RecordHoldsPiece(inRecord, want)) Fail("holds: another template is held");
    want = *chest;
    if (!zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldAll) || !zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldHas)) Fail("holds: the chest's own 2292 x6 are not held");
    want.items[0].quantity = 7;
    if (zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldAll)) Fail("holds: 2292 x7 kept is held by a box of 2292 x6");
    if (!zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldHas)) Fail("holds: a box the has rule left alone is not held");
    want = *chest; want.items[0].tmpl = "99-other.mod";
    if (zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldAll) || zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldHas)) Fail("holds: items the box does not hold are held");
    want = *chest; want.items.clear();
    if (!zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldAll)) Fail("holds: a piece that kept no items is not held");
    want = *chest; want.pos[0] += 50.0f;
    if (zonekeep::RecordHoldsItems(inRecord, want, zonekeep::kHoldHas)) Fail("holds: the items of a piece the record does not hold are held");
    std::vector<zonekeep::KeptPiece> none;
    if (zonekeep::PiecesOfRecord((const unsigned char*)b.data(), 100, w, &none, &why) || why.empty()) Fail("holds: a record cut to 100 bytes is read");
}
/* T-36 B2b: the game side's pure steps (keptwire.h) */
bool KwNear(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }
void t_t36b2b_local_frame()
{
    const float s = 0.70710678f;
    const float hostPos[3] = { 10.0f, 2.0f, 10.0f };
    const float hostQ[4] = { s, 0.0f, s, 0.0f };   /* a quarter turn about the up axis */
    /* the host's own x axis turned a quarter about y points to world -z: a point one unit along it stands at (10, 2, 9) */
    const float worldPos[3] = { 10.0f, 2.5f, 9.0f };
    float lp[3], lq[4];
    keptwire::LocalOfWorld(hostPos, hostQ, worldPos, hostQ, lp, lq);
    if (!KwNear(lp[0], 1.0f) || !KwNear(lp[1], 0.5f) || !KwNear(lp[2], 0.0f)) Fail("local frame: a point one unit along the turned host's x axis is not (1, 0.5, 0) in its frame");
    if (!KwNear(lq[0], 1.0f) || !KwNear(lq[1], 0.0f) || !KwNear(lq[2], 0.0f) || !KwNear(lq[3], 0.0f)) Fail("local frame: the host's own rotation is not 'no turn' in its frame");
    const float ident[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    keptwire::LocalOfWorld(hostPos, ident, worldPos, hostQ, lp, lq);
    if (!KwNear(lp[0], 0.0f) || !KwNear(lp[1], 0.5f) || !KwNear(lp[2], -1.0f)) Fail("local frame: an unturned host does not just take its position away");
    if (!KwNear(lq[0], s) || !KwNear(lq[2], s)) Fail("local frame: an unturned host changes the piece's rotation");
    float c[4];
    keptwire::QuatConjMul(hostQ, hostQ, c);
    if (!KwNear(c[0], 1.0f) || !KwNear(c[1], 0.0f) || !KwNear(c[2], 0.0f) || !KwNear(c[3], 0.0f)) Fail("local frame: conj(q) * q is not 'no turn'");
    /* a half turn piece inside a quarter turn host: a quarter turn in the host's frame */
    const float half[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    keptwire::QuatConjMul(hostQ, half, c);
    if (!KwNear(c[0], s) || !KwNear(c[2], s)) Fail("local frame: a half turn inside a quarter turn host is not a quarter turn");
    const float v[3] = { 0.0f, 0.0f, -1.0f };
    float o[3];
    keptwire::RotateInv(hostQ, v, o);
    if (!KwNear(o[0], 1.0f) || !KwNear(o[2], 0.0f)) Fail("local frame: RotateInv does not undo the host's quarter turn");
}
void t_t36b2b_game_steps()
{
    using namespace keptwire;
    if (LayOrder(0, 1) != kOrderHost || LayOrder(0, 0) != kOrderFree || LayOrder(1, 0) != kOrderFurniture || LayOrder(1, 1) != kOrderFurniture)
        Fail("steps: the lay order is not hosts, then free-standing, then furniture");
    const unsigned newF = 0, claimedP = SetPartState(0, kPartPiece, kStClaimed), laidP = SetPartState(0, kPartPiece, kStLaid);
    if (PartStep(newF, kPartPiece, 0, 2, 1, 0) != kStepClaim) Fail("steps: a new piece in a held area is not claimed");
    if (PartStep(newF, kPartPiece, 0, 2, 0, 0) != kStepNone) Fail("steps: a new piece in an area this game does not hold is claimed");
    if (PartStep(newF, kPartPiece, 0, -1, 1, 0) != kStepNone) Fail("steps: a game with no slot claims");
    if (PartStep(claimedP, kPartPiece, 3, 2, 1, 0) != kStepWork) Fail("steps: this game's own claim is not worked");
    if (PartStep(claimedP, kPartPiece, 3, 2, 1, 1) != kStepReport) Fail("steps: a part laid here before is not reported again");
    if (PartStep(claimedP, kPartPiece, 3, 2, 0, 0) != kStepRelease) Fail("steps: a claim held after the area left is not given back");
    if (PartStep(claimedP, kPartPiece, 4, 2, 1, 0) != kStepNone) Fail("steps: another game's claim is worked here");
    if (PartStep(laidP, kPartPiece, 0, 2, 1, 0) != kStepNone) Fail("steps: a laid part is claimed again");
    const unsigned claimedI = SetPartState(0, kPartItems, kStClaimed);
    if (PartStep(claimedI, kPartItems, 1, 0, 1, 0) != kStepWork) Fail("steps: slot 0's own items claim (field 1) is not worked");
    if (PartStep(claimedI, kPartOwner, 1, 0, 1, 0) != kStepNone) Fail("steps: the owner part has claim steps");
    if (HowOfRestore(1) != kHowPresent || HowOfRestore(2) != kHowMade || HowOfRestore(3) != kHowPresent || HowOfRestore(4) != kHowRefused || HowOfRestore(0) != kHowRefused)
        Fail("steps: the restore road's outcomes are not present / made / present / refused");
    if (NearXZ(0.0f, 0.0f, 2.0f, 2.0f, 3.0f) != 1 || NearXZ(0.0f, 0.0f, 3.0f, 0.1f, 3.0f) != 0) Fail("steps: NearXZ");
    std::vector<zonekeep::KeptMat> mats(3);
    mats[0].index = 2; mats[0].count = 5.0f; mats[1].index = 0; mats[1].count = 1.5f; mats[2].index = 40; mats[2].count = 9.0f;
    float arr[16];
    const int n = MatsArray(mats, arr, 16);
    if (n != 3 || !KwNear(arr[0], 1.5f) || !KwNear(arr[1], 0.0f) || !KwNear(arr[2], 5.0f)) Fail("steps: MatsArray does not put each count at its index (an index past the room left out)");
    if (MatsArray(std::vector<zonekeep::KeptMat>(), arr, 16) != 0 || !KwNear(arr[3], 0.0f)) Fail("steps: MatsArray of nothing is not empty and zeroed");
}
void t_t36b2_merge()
{
    keptwire::Row a = T36b2Row(0);                         /* the chest, slot 2, with its items */
    zonekeep::KeptPiece wall; wall.ownerSlot = 0; wall.key = "77-gamedata.base@32,32@10000,20000,0"; wall.tmpl = "77-gamedata.base";
    wall.pos[0] = 1000.0f; wall.pos[2] = 2000.0f; wall.complete = 1;
    std::vector<zonekeep::KeptPiece> fresh; fresh.push_back(a.p); fresh.push_back(wall);
    std::vector<zonekeep::KeptPiece> old;
    old.push_back(a.p);                                     /* 0: done - dropped */
    zonekeep::KeptPiece near = a.p; near.pos[0] += 0.1f; old.push_back(near);   /* 1: the chest again, items not laid; the new chest has items - dropped, nothing moves */
    zonekeep::KeptPiece wallItems = wall; wallItems.items = a.p.items; old.push_back(wallItems);   /* 2: the wall with items not laid; the new wall has none - they move */
    zonekeep::KeptPiece far = wall; far.pos[0] = 3000.0f; far.key = "77-gamedata.base@32,32@30000,20000,0"; old.push_back(far);   /* 3: not in the new list - appended */
    zonekeep::KeptPiece other = a.p; other.ownerSlot = 5; old.push_back(other);   /* 4: the chest's spot, another player - appended */
    std::vector<zonekeep::KeptState> st(3);
    st[0].pieceDone = 1; st[0].itemsDone = 1; st[0].ownerTook = 1;
    st[1].pieceDone = 1; st[1].ownerTook = 1;
    st[2].pieceDone = 1;
    zonekeep::MergeStats ms; std::vector<zonekeep::KeptState> fst;
    zonekeep::MergeUndone(old, st, &fresh, &fst, &ms);
    if (ms.doneDropped != 1 || ms.matchedDropped != 2 || ms.itemsMoved != 1 || ms.appended != 2 || fresh.size() != 4)
        Fail("merge: done " + I(ms.doneDropped) + ", matched " + I(ms.matchedDropped) + ", moved " + I(ms.itemsMoved) + ", appended " + I(ms.appended) + ", list " + I((int)fresh.size())
             + " (expected 1, 2, 1, 2, 4)");
    else if (fresh[0].items.size() != 1 || fresh[1].items.size() != 1 || fresh[1].items[0].tmpl != "2292-gamedata.base" || fresh[2].key != far.key || fresh[3].ownerSlot != 5)
        Fail("merge: the merged list is not the new pieces, the moved items and the unfinished rows in order");
    if (fst.size() != 4 || fst[0].pieceDone != 0 || fst[2].pieceDone != 0 || fst[3].itemsDone != 0) Fail("merge: the merged rows' states are not one new state per row");
    std::vector<zonekeep::KeptPiece> fresh2; zonekeep::MergeStats ms2; std::vector<zonekeep::KeptState> fst2;
    zonekeep::MergeUndone(old, std::vector<zonekeep::KeptState>(), &fresh2, &fst2, &ms2);
    if (ms2.appended != 5 || fresh2.size() != 5) Fail("merge: with no state and no new pieces every old row is not carried");
    if (ms.matchedItemsKept != 1 || ms.matchedDropLaid != 0) Fail("merge: a matched row whose box contents are not moved (the piece read holds its own) is not counted");
}
void t_t36b2_settle_and_items()
{
    using namespace keptwire;
    int res = 0, re = 0;
    /* the area's holder is slot 1 (who 2); the piece's owner is slot 2 (who 3) */
    ServerRow n;
    if (Settled(&n, kPartPiece, 2) != kSettledRefused || PartState(n.flags, kPartPiece) != kStNew) Fail("KEPT settle: a SETTLED with no lay taken this run closes a new piece");
    Claim(&n, kPartPiece, 2, 1, 2, &res);
    if (Settled(&n, kPartPiece, 2) != kSettledRefused || PartState(n.flags, kPartPiece) != kStClaimed) Fail("KEPT settle: a SETTLED with no lay taken this run closes a claimed piece");
    if (Laid(&n, kPartPiece, kHowMade, 2, 1, 2) != kLaidTaken || n.piece.laidSeen != 2) Fail("KEPT settle: the lay is not remembered for this run");
    re = 0;
    if (!PeerGone(&n, 2, &re) || re != 1 || PartState(n.flags, kPartPiece) != kStNew || n.piece.laidSeen != 2) Fail("KEPT settle: the holder's piece is not offered again when it leaves, or its lay is forgotten");
    if (Settled(&n, kPartPiece, 3) != kSettledRefused || PartState(n.flags, kPartPiece) != kStNew) Fail("KEPT settle: a game that never laid the piece closes it");
    if (Settled(&n, kPartPiece, 2) != kSettledDone || PartState(n.flags, kPartPiece) != kStDone) Fail("KEPT settle: the layer, back on a new link, does not close the piece it laid this run");
    /* box contents laid by the area's holder: never offered again when it leaves, and kept laid across a restart */
    ServerRow t;
    if (!Claim(&t, kPartItems, 2, 1, 2, &res) || res != kClaimGrant) Fail("KEPT items: the holder is not given the items");
    if (Laid(&t, kPartItems, kHowItemsLaid, 2, 1, 2) != kLaidTaken || (t.flags & kFlagItemsLaidNonWriter) == 0) Fail("KEPT items: box contents the holder laid are not marked never offered again");
    re = 0;
    if (!PeerGone(&t, 2, &re) || re != 0 || PartState(t.flags, kPartItems) != kStLaid || t.items.laidBy != 0) Fail("KEPT items: box contents are offered again when their layer leaves");
    if (Claim(&t, kPartItems, 3, 1, 2, &res) || res != kClaimNotNew) Fail("KEPT items: laid box contents are claimed by another game");
    const zonekeep::KeptState ks = StateOfFlags(t.flags);
    if (ks.itemsLaidNonWriter != 1 || PartState(FlagsOfState(ks, 1), kPartItems) != kStLaid) Fail("KEPT items: laid box contents are not kept laid across a restart");
    if (Settled(&t, kPartItems, 2) != kSettledDone || PartState(t.flags, kPartItems) != kStDone) Fail("KEPT items: the layer's settle does not close its laid box contents");
}
void t_t36b2_merge_parts()
{
    keptwire::Row a = T36b2Row(0);                         /* the chest, slot 2, with its items */
    std::vector<zonekeep::KeptPiece> old(5, a.p);
    for (size_t i = 0; i < old.size(); ++i) old[i].pos[0] = 500.0f + 50.0f * (float)i;
    std::vector<zonekeep::KeptState> st(5);
    st[0].pieceDone = 1;                                    /* 0: piece done, items not laid - carried, the piece not laid again */
    st[1].pieceDone = 1; st[1].itemsLaidNonWriter = 1;      /* 1: piece done, items laid - carried owner-only */
    st[2].pieceLaidNonWriter = 1; st[2].itemsDone = 1; st[2].ownerTook = 1;   /* 2: piece laid not done, items done - the piece only, its owner registers it again */
    st[3].pieceDone = 1; st[3].itemsLaidNonWriter = 1; st[3].ownerTook = 1;   /* 3: nothing left to do - dropped */
    std::vector<zonekeep::KeptPiece> fresh; std::vector<zonekeep::KeptState> fs; zonekeep::MergeStats ms;
    zonekeep::MergeUndone(old, st, &fresh, &fs, &ms);
    if (ms.doneDropped != 1 || ms.appended != 4 || ms.ownerOnly != 1 || ms.pieceKeptDone != 2 || ms.itemsDoneLeft != 1 || ms.itemsLaidUnconfirmed != 2
        || ms.ownerRetake != 1 || fresh.size() != 4 || fs.size() != 4)
        Fail("merge parts: done " + I(ms.doneDropped) + ", appended " + I(ms.appended) + ", owner-only " + I(ms.ownerOnly) + ", piece done " + I(ms.pieceKeptDone)
             + ", contents done " + I(ms.itemsDoneLeft) + ", contents laid unconfirmed " + I(ms.itemsLaidUnconfirmed) + ", owner again " + I(ms.ownerRetake)
             + ", list " + I((int)fresh.size()) + ", states " + I((int)fs.size()) + " (expected 1, 4, 1, 2, 1, 2, 1, 4, 4)");
    else
    {
        if (fs[0].pieceDone != 1 || fs[0].itemsDone != 0 || fresh[0].items.size() != 1) Fail("merge parts: a done piece with box contents not laid is not carried for its items alone");
        if (fs[1].pieceDone != 1 || fs[1].itemsDone != 1 || fs[1].ownerTook != 0 || !fresh[1].items.empty()) Fail("merge parts: a row with only its owner part undone is not carried owner-only");
        if (fs[2].pieceDone != 0 || fs[2].ownerTook != 0 || !fresh[2].items.empty()) Fail("merge parts: a piece laid but not done is not carried alone with its owner part open (its done box contents must not go; its owner registers the remade piece)");
        if (fs[3].pieceDone != 0 || fs[3].itemsDone != 0 || fresh[3].items.size() != 1) Fail("merge parts: a row with nothing done is not carried whole");
        const unsigned f1 = keptwire::FlagsOfState(fs[1], fresh[1].items.empty() ? 0 : 1);
        if (keptwire::PartState(f1, keptwire::kPartPiece) != keptwire::kStDone || keptwire::PartState(f1, keptwire::kPartItems) != keptwire::kStDone || keptwire::RowDone(f1) != 0)
            Fail("merge parts: the owner-only row does not start with only its owner part open");
    }
}
void t_t36b2_host_missing()
{
    /* furniture whose host building is not in the area record is not kept (laid with no host it would stand free in the open) */
    zonekeep::Container c;
    zonekeep::Rec list; list.sid = zonekeep::kBuildingList;
    zonekeep::Rec host; host.sid = "st-host"; host.type = zonekeep::kTypeState; host.strs["owner faction ID"] = coopslot::StandInId(1);
    host.ints["handleS"] = 7; host.ints["handleI"] = 8; host.ints["handleC"] = 9; host.floats["world Y pos"] = 30.0f;
    zonekeep::Rec inside = host; inside.sid = "st-in"; inside.ints.clear(); inside.bools["is inside building"] = 1;
    inside.ints["isInsideBuildingS"] = 7; inside.ints["isInsideBuildingI"] = 8; inside.ints["isInsideBuildingC"] = 9;
    zonekeep::Rec lost = inside; lost.sid = "st-lost"; lost.ints["isInsideBuildingS"] = 70;
    const char* const states[3] = { "st-host", "st-in", "st-lost" };
    const char* const targets[3] = { "4092-TwoStorey.mod", "1459-gamedata.base", "1459-gamedata.base" };
    for (int i = 0; i < 3; ++i)
    {
        zonekeep::Inst in; in.target = targets[i];
        in.pos[0] = 94950.0f + 2.0f * (float)i; in.pos[1] = 0.0f; in.pos[2] = 95300.0f + 2.0f * (float)i;
        in.rot[0] = 1.0f; in.rot[1] = in.rot[2] = in.rot[3] = 0.0f;
        in.strs.push_back(states[i]);
        list.insts.push_back(in);
    }
    c.recs.push_back(list); c.recs.push_back(host); c.recs.push_back(inside); c.recs.push_back(lost);
    std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st; std::string why;
    zonekeep::ExtractKeptPieces(c, 0, &ps, &st, &why);
    if (st.kept != 2 || st.hostMissing != 1 || ps.size() != 2) Fail("host missing: kept " + I(st.kept) + ", host missing " + I(st.hostMissing) + " (expected 2, 1)");
    else if (!ps[0].hostKey.empty() || ps[1].hostKey != ps[0].key || ps[1].ownerSlot != 1) Fail("host missing: the furniture with its host found is not kept in that host");
    if (st.hostMissingList.size() != 1 || st.hostMissingList[0].first.empty() || st.hostMissingList[0].second != 1) Fail("host missing: the furniture not kept is not named with its key and owner slot");
}
void t_t36_echo_match()
{
    /* the lay's write is stamped 100; this game had sent that area stamped 90 before it */
    if (keptwire::EchoAnswersWrite(1, 100, 100, 90) != 1) Fail("echo match: the echo of the lay's own write does not settle it");
    if (keptwire::EchoAnswersWrite(1, 130, 100, 90) != 1) Fail("echo match: the echo of a later write of that area does not settle it (the lay's own write refused)");
    if (keptwire::EchoAnswersWrite(1, 90, 100, 90) != 0) Fail("echo match: an earlier write's echo (a replay or a re-push) settles a later lay");
    if (keptwire::EchoAnswersWrite(1, 95, 100, 90) != 0) Fail("echo match: an echo older than the lay's write settles it");
    if (keptwire::EchoAnswersWrite(1, 100, 100, 100) != 0) Fail("echo match: an earlier write in the same second settles the lay (the lay's write is refused as not newer then)");
    if (keptwire::EchoAnswersWrite(1, 101, 100, 100) != 1) Fail("echo match: the next newer write after a same-second one does not settle the lay");
    if (keptwire::EchoAnswersWrite(0, 130, 100, 90) != 0) Fail("echo match: an echo with no stamp settles a lay");
}
/* T-36 gaps: a backpack's own items and a building's doors kept with it (zonekeep.h format 2: J and D lines; format 1 still read),
   the KEPT row carrying them (world-server protocol 94), the has rule and RecordHoldsItems counting a pack's items, where a kept item
   goes (keptwire.h KeptPutFirst / KeptPutNext - never destroyed), the Small Shack's door read from the real zone.35.42, and broken in
   the build STATE. Runs from src\coop-test. */
zonekeep::Rec T36gRec(int type, const std::string& sid)
{
    zonekeep::Rec r; r.type = type; r.sid = sid;
    return r;
}
zonekeep::Inst T36gInst(const std::string& target)
{
    zonekeep::Inst in; in.id = "1"; in.target = target;
    for (int j = 0; j < 3; ++j) in.pos[j] = 0.0f;
    in.rot[0] = 1.0f; in.rot[1] = 0.0f; in.rot[2] = 0.0f; in.rot[3] = 0.0f;
    return in;
}
zonekeep::Rec T36gItemRec(const std::string& sid, const std::string& tmpl, int qty)
{
    zonekeep::Rec r = T36gRec(zonekeep::kTypeItem, sid);
    r.strs["base data sid"] = tmpl; r.strs["section"] = "backpack_content"; r.ints["quantity"] = qty; r.ints["inventory x"] = qty;
    return r;
}
/* a chest of the area writer's own player (slot 0) holding one item and a backpack of two items, one of which holds an item of its
   own; the chest's state record is broken and names two doors (door 1 locked and the player's) */
zonekeep::Container T36gArea()
{
    zonekeep::Container c; c.version = 15;
    zonekeep::Rec list = T36gRec(0, zonekeep::kBuildingList);
    zonekeep::Inst e = T36gInst("1459-gamedata.base"); e.pos[0] = 94950.0f; e.pos[1] = 3077.0f; e.pos[2] = 95300.0f;
    e.strs.push_back("st1"); e.strs.push_back("inv1");
    list.insts.push_back(e);
    c.recs.push_back(list);
    zonekeep::Rec st = T36gRec(zonekeep::kTypeState, "st1");
    st.strs["owner faction ID"] = zonekeep::kPlayerFactionSid; st.bools["is complete"] = 1; st.bools["broken"] = 1;
    st.ints["state0"] = 1; st.ints["lock lvl0"] = 29; st.ints["gatecode0"] = -1;
    st.ints["state1"] = 0; st.bools["locked1"] = 1; st.bools["wants lock1"] = 0; st.ints["lock lvl1"] = 40; st.ints["gatecode1"] = 7;
    st.strs["owner faction ID1"] = zonekeep::kPlayerFactionSid;
    c.recs.push_back(st);
    zonekeep::Rec inv = T36gRec(zonekeep::kTypeInventory, "inv1");
    inv.insts.push_back(T36gInst("it1")); inv.insts.push_back(T36gInst("pk1"));
    c.recs.push_back(inv);
    c.recs.push_back(T36gItemRec("it1", "2292-gamedata.base", 3));
    zonekeep::Rec pk = T36gItemRec("pk1", "4002-gamedata.base", 1); pk.ints["item function"] = 4; pk.insts.push_back(T36gInst("pinv"));
    c.recs.push_back(pk);
    zonekeep::Rec pinv = T36gRec(zonekeep::kTypeInventory, "pinv");
    pinv.insts.push_back(T36gInst("in1")); pinv.insts.push_back(T36gInst("in2"));
    c.recs.push_back(pinv);
    c.recs.push_back(T36gItemRec("in1", "2292-gamedata.base", 5));
    zonekeep::Rec in2 = T36gItemRec("in2", "4003-gamedata.base", 1); in2.insts.push_back(T36gInst("ninv"));
    c.recs.push_back(in2);
    zonekeep::Rec ninv = T36gRec(zonekeep::kTypeInventory, "ninv"); ninv.insts.push_back(T36gInst("n1"));
    c.recs.push_back(ninv);
    c.recs.push_back(T36gItemRec("n1", "77-deep.mod", 1));
    return c;
}
void t_t36g_packs_doors_extract()
{
    const zonekeep::Container c = T36gArea();
    std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st; std::string why;
    zonekeep::ExtractKeptPieces(c, 0, &ps, &st, &why);
    if (ps.size() != 1 || st.kept != 1) { Fail("packs: the chest is not the one kept piece (" + I((int)ps.size()) + ")"); return; }
    const zonekeep::KeptPiece& p = ps[0];
    if (p.items.size() != 2 || st.items != 2 || p.items[0].tmpl != "2292-gamedata.base" || !p.items[0].inner.empty()) { Fail("packs: the box's two items are not kept as they were"); return; }
    if (p.items[1].tmpl != "4002-gamedata.base" || p.items[1].inner.size() != 2 || p.items[1].inner[0].tmpl != "2292-gamedata.base"
        || p.items[1].inner[0].quantity != 5 || p.items[1].inner[0].section != "backpack_content" || p.items[1].inner[0].slotX != 5
        || p.items[1].inner[1].tmpl != "4003-gamedata.base" || p.items[1].inner[1].inner.size() != 1 || p.items[1].inner[1].inner[0].tmpl != "77-deep.mod")
        Fail("packs: the backpack's own two items are not kept with it, and the pack among them with its own item");
    if (st.bagItems != 2 || st.innerItems != 3 || st.nestedBags != 0)
        Fail("packs: counted " + I(st.bagItems) + " pack(s), " + I(st.innerItems) + " inner, " + I(st.nestedBags) + " past the depth bound (expected 2, 3, 0)");
    if (p.broken != 1) Fail("packs: the chest's broken flag is not kept");
    if (p.doors.size() != 2 || st.doors != 2) { Fail("doors: " + I((int)p.doors.size()) + " door(s) kept, expected 2"); return; }
    const zonekeep::KeptDoor& d0 = p.doors[0];
    const zonekeep::KeptDoor& d1 = p.doors[1];
    if (d0.index != 0 || d0.state != 1 || d0.locked != 0 || d0.lockLevel != 29 || d0.gateCode != -1 || d0.ownerSlot != -1) Fail("doors: door 0 is not kept as it was");
    if (d1.index != 1 || d1.state != 0 || d1.locked != 1 || d1.wantsLock != 0 || d1.lockLevel != 40 || d1.gateCode != 7 || d1.ownerSlot != 0) Fail("doors: door 1 is not kept as it was");
    /* the real Small Shack of zone.35.42 (written by slot 0): one door, open, unlocked, lock level 29, the player's */
    std::string b2; int w2 = -2;
    if (!T36b1Load("zone.35.42", &b2, &w2)) { Fail("data\\zone.35.42.platoon is missing"); return; }
    zonekeep::Container c2;
    if (!zonekeep::ParseContainer((const unsigned char*)b2.data(), b2.size(), &c2, &why)) { Fail("zone.35.42 does not parse: " + why); return; }
    std::vector<zonekeep::KeptPiece> sh; zonekeep::KeepStats s3;
    zonekeep::ExtractKeptPieces(c2, w2, &sh, &s3, &why);
    const zonekeep::KeptPiece* shack = T36b1Find(sh, "4092-TwoStorey.mod@35,42@182634,479665,4113");
    if (shack == 0) { Fail("doors: the Small Shack is not kept"); return; }
    if (shack->doors.size() != 1) { Fail("doors: the Small Shack keeps " + I((int)shack->doors.size()) + " door(s), expected 1"); return; }
    const zonekeep::KeptDoor& sd = shack->doors[0];
    if (sd.index != 0 || sd.state != 1 || sd.locked != 0 || sd.wantsLock != 0 || sd.lockLevel != 29 || sd.gateCode != -1 || sd.ownerSlot != 0)
        Fail("doors: the Small Shack's door is not state 1, unlocked, lock level 29, gate code -1, slot 0's (state " + I(sd.state) + " locked " + I(sd.locked)
             + " level " + I(sd.lockLevel) + " gate " + I(sd.gateCode) + " owner " + I(sd.ownerSlot) + ")");
}
void t_t36g_list_format2()
{
    std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st; std::string why;
    zonekeep::ExtractKeptPieces(T36gArea(), 0, &ps, &st, &why);
    if (ps.size() != 1) { Fail("list2: the area gave no piece"); return; }
    const std::string text = zonekeep::EncodeKeptAs(ps, 2);   /* a list as format 2 wrote it */
    if (text.compare(0, 14, "kept-pieces\t2\n") != 0 || text.find("\nJ\t4003-gamedata.base\t") == std::string::npos || text.find("\nD\t1\t0\t1\t0\t40\t7\t0\n") == std::string::npos)
        Fail("list2: the text has no format-2 header, J line or D line");
    std::vector<zonekeep::KeptPiece> back;
    if (!zonekeep::DecodeKept(text, &back, &why) || zonekeep::EncodeKeptAs(back, 2) != text) { Fail("list2: the text does not round-trip: " + why); return; }
    if (back[0].items[1].inner.size() != 2 || back[0].doors.size() != 2 || back[0].doors[1].gateCode != 7) Fail("list2: a pack item or a door changed on the round trip");
    /* a list of format 1 (written before packs and doors were kept) is still read */
    zonekeep::KeptPiece old = ps[0]; old.doors.clear();
    for (size_t j = 0; j < old.items.size(); ++j) old.items[j].inner.clear();
    std::string t1 = zonekeep::EncodeKeptAs(std::vector<zonekeep::KeptPiece>(1, old), 1);
    if (!zonekeep::DecodeKept(t1, &back, &why) || back.size() != 1 || back[0].items.size() != 2) Fail("list2: a format 1 list is not read: " + why);
    /* refused: a J or D line in format 1, a J line not right after an item line, a D line before any piece, a D line one field short */
    const size_t jAt = text.find("\nJ\t");
    const std::string jLine = text.substr(jAt + 1, text.find('\n', jAt + 1) - jAt);
    const std::string pLine = text.substr(14, text.find('\n', 14) - 14 + 1);
    const std::string refused[5] = { t1 + jLine, t1 + "D\t0\t1\t0\t0\t29\t-1\t0\n", "kept-pieces\t2\n" + pLine + jLine, "kept-pieces\t2\nD\t0\t1\t0\t0\t29\t-1\t0\n",
                                     "kept-pieces\t2\n" + pLine + "D\t0\t1\t0\t0\t29\t-1\n" };
    for (int i = 0; i < 5; ++i)
        if (zonekeep::DecodeKept(refused[i], &back, &why) || !back.empty()) Fail("list2: refused text " + I(i) + " is read");
}
/* outposts (townkeep.h): the TOWNS wire and both files fail closed, a said outpost matched to its kept pieces by handle then by
   distance, a SAY replacing its slot's rows, the lay step */
void t_t36o_outposts()
{
    using namespace townkeep;
    if (kMsgTowns == keptwire::kMsgKept) Fail("outposts: TOWNS shares KEPT's message number");
    TownRow a; a.owner = 2; a.hand[0] = 10; a.hand[1] = 3; a.hand[2] = 7; a.hand[3] = 41; a.hand[4] = 9001; a.pos[0] = 1000.0f; a.pos[1] = 50.0f; a.pos[2] = -2000.0f;
    a.name = "Hill \\ Fort"; a.keys.push_back("k1"); a.keys.push_back("k2");
    TownRow b; b.owner = 0; b.hand[3] = 5; b.hand[4] = 6; b.pos[0] = -5.5f; b.name = "";
    std::vector<TownRow> two; two.push_back(a); two.push_back(b);
    std::vector<char> m;
    if (!EncodeTowns(&m, kOpList, 1, two)) Fail("outposts: a LIST of two rows does not encode");
    unsigned op = 0; int first = 0; std::vector<TownRow> back;
    if (DecodeTowns(&m[0], m.size(), &op, &first, &back) != 1 || op != kOpList || first != 1 || back.size() != 2 || back[0].owner != 2 || !HandSame(back[0].hand, a.hand)
        || back[0].pos[2] != -2000.0f || back[0].name != a.name || back[0].keys != a.keys || back[1].name != "" || back[1].pos[0] != -5.5f) Fail("outposts: a LIST does not round-trip");
    std::vector<char> cut(m.begin(), m.end() - 1);
    if (DecodeTowns(&cut[0], cut.size(), &op, &first, &back) != 0 || !back.empty()) Fail("outposts: a LIST cut short decodes");
    std::vector<char> more(m); more.push_back(0);
    if (DecodeTowns(&more[0], more.size(), &op, &first, &back) != 0) Fail("outposts: a LIST with a trailing byte decodes");
    std::vector<char> badOp(m); badOp[0] = 4;   /* 3 is SHOW KEYS since protocol 101 */
    if (DecodeTowns(&badOp[0], badOp.size(), &op, &first, &back) != 0) Fail("outposts: an unknown op decodes");
    std::vector<char> big(m); big[8] = (char)(kMaxTownsMsg + 1);
    if (DecodeTowns(&big[0], big.size(), &op, &first, &back) != 0) Fail("outposts: a count past the cap decodes");
    if (DecodeTowns(0, 0, &op, &first, &back) != 0) Fail("outposts: an empty message decodes");
    if (EncodeTowns(&m, kOpSay, 1, two)) Fail("outposts: a SAY with owners and keys encodes");
    TownRow s(a); s.owner = -1; s.keys.clear();
    std::vector<TownRow> one(1, s);
    if (!EncodeTowns(&m, kOpSay, 1, one) || DecodeTowns(&m[0], m.size(), &op, &first, &back) != 1 || op != kOpSay || back.size() != 1 || back[0].owner != -1) Fail("outposts: a SAY does not round-trip");
    std::vector<TownRow> none;
    if (!EncodeTowns(&m, kOpSay, 1, none) || DecodeTowns(&m[0], m.size(), &op, &first, &back) != 1 || !back.empty()) Fail("outposts: an empty SAY (no outposts) does not round-trip");
    TownRow c(a); c.name = std::string("bad") + (char)1; if (RowOk(c, kOpList)) Fail("outposts: a name with a control byte is valid");
    if (NameFit(c.name) != "bad?" || NameFit(std::string(200, 'x')).size() != keptwire::kStrMax) Fail("outposts: NameFit does not fit a name");
    TownRow d(a); d.keys.push_back(""); if (RowOk(d, kOpList)) Fail("outposts: an empty key is valid");
    TownRow e(a); e.keys.push_back("k1"); if (RowOk(e, kOpList)) Fail("outposts: a key twice is valid");
    TownRow g(a); g.pos[1] = 1.0e9f; if (RowOk(g, kOpList)) Fail("outposts: a far float is valid");
    TownRow h(a); h.owner = -1; if (RowOk(h, kOpList)) Fail("outposts: a LIST row with no owner is valid");
    /* the files */
    const std::string kt = EncodeTownsFile(kKeptTownsHeader, two, true);
    std::vector<TownRow> fr; std::string why;
    if (!DecodeTownsFile(kt, kKeptTownsHeader, true, &fr, &why) || fr.size() != 2 || fr[0].name != a.name || fr[0].keys != a.keys || !HandSame(fr[1].hand, b.hand)) Fail("outposts: kept-towns.txt does not round-trip: " + why);
    std::string crlf;
    for (size_t i = 0; i < kt.size(); ++i) { if (kt[i] == '\n') crlf += '\r'; crlf += kt[i]; }
    if (!DecodeTownsFile(crlf, kKeptTownsHeader, true, &fr, &why) || fr.size() != 2) Fail("outposts: kept-towns.txt with CR LF line ends does not read");
    if (DecodeTownsFile(kt, kPlayerTownsHeader, false, &fr, &why) || !fr.empty()) Fail("outposts: kept-towns.txt reads as player-towns.txt");
    std::vector<TownRow> noKeys(two); noKeys[0].keys.clear();
    const std::string pt = EncodeTownsFile(kPlayerTownsHeader, noKeys, false);
    if (!DecodeTownsFile(pt, kPlayerTownsHeader, false, &fr, &why) || fr.size() != 2 || !fr[0].keys.empty()) Fail("outposts: player-towns.txt does not round-trip");
    if (DecodeTownsFile(pt + "K\tk9\n", kPlayerTownsHeader, false, &fr, &why)) Fail("outposts: a K line reads in player-towns.txt");
    if (DecodeTownsFile("kept-towns\t1\nK\tk1\n", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: a K line before any T line reads");
    if (DecodeTownsFile("kept-towns\t1\nT\t1\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tx\nK\tk1\nK\tk1\n", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: a key twice in one outpost reads");
    if (!DecodeTownsFile("kept-towns\t1\nT\t1\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tx\nK\tk1\n", kKeptTownsHeader, true, &fr, &why) || fr.size() != 1 || fr[0].keys.size() != 1) Fail("outposts: a written-by-hand kept-towns.txt does not read: " + why);
    if (DecodeTownsFile("kept-towns\t7\n", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: a list of an unknown format reads");   /* format 6 is the anchor + public format */
    if (DecodeTownsFile("kept-towns\t1\nT\t1\t0\t0\t0\t0\t0\tzz\t0\t0\tx\n", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: a bad float reads");
    if (DecodeTownsFile("", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: an empty file reads");
    if (DecodeTownsFile("kept-towns\t1\nX\t1\n", kKeptTownsHeader, true, &fr, &why)) Fail("outposts: an unknown line reads");
    /* a SAY replaces its slot's rows only */
    std::vector<TownRow> all;
    TownRow r1; r1.owner = 1; r1.name = "one"; all.push_back(r1);
    TownRow r2; r2.owner = 2; r2.name = "old"; all.push_back(r2);
    std::vector<TownRow> said2; TownRow n2; n2.owner = -1; n2.name = "new"; said2.push_back(n2); said2.push_back(n2);
    ReplaceSlotTowns(&all, 2, said2);
    if (all.size() != 3 || all[0].name != "one" || all[1].name != "new" || all[1].owner != 2 || all[2].owner != 2) Fail("outposts: a SAY does not replace only its slot's rows");
    ReplaceSlotTowns(&all, 2, std::vector<TownRow>());
    if (all.size() != 1 || all[0].owner != 1) Fail("outposts: an empty SAY does not clear its slot's rows");
    /* the reset's match: by the said keys only */
    std::vector<TownRow> said;
    TownRow A; A.owner = 2; A.hand[1] = 1; A.hand[3] = 11; A.hand[4] = 111; A.name = "A"; A.keys.push_back("p1"); A.keys.push_back("p2"); said.push_back(A);
    TownRow B; B.owner = 2; B.hand[1] = 2; B.pos[0] = 10000.0f; B.name = "B"; B.keys.push_back("p9"); said.push_back(B);
    TownRow C(A); C.owner = 3; C.name = "C"; C.keys.clear(); C.keys.push_back("p5"); said.push_back(C);
    TownRow D; D.owner = 2; D.hand[3] = 44; D.pos[0] = -30000.0f; D.name = "D"; said.push_back(D);
    std::vector<zonekeep::KeptPiece> ps;
    zonekeep::KeptPiece p;
    if (p.hasTown != 0 || p.townHand[0] != 0 || p.townHand[4] != 0) Fail("outposts: a kept piece starts with a town");
    p.ownerSlot = 2; p.hasTown = 1; for (int i = 0; i < 5; ++i) p.townHand[i] = A.hand[i];
    p.key = "p2"; ps.push_back(p); p.key = "p1"; ps.push_back(p); p.key = "p1"; ps.push_back(p);
    zonekeep::KeptPiece q(p); q.key = "p3"; q.pos[0] = 10300.0f; ps.push_back(q);   /* A's handle, near B, on no list */
    zonekeep::KeptPiece o3(p); o3.ownerSlot = 3; o3.key = "p5"; ps.push_back(o3);   /* on C's list, C's player */
    zonekeep::KeptPiece o2(p); o2.key = "p5"; ps.push_back(o2);   /* the same key, another player: not C's */
    zonekeep::KeptPiece nt(p); nt.hasTown = 0; nt.key = "p9"; ps.push_back(nt);   /* no town in its area record, on B's list */
    std::vector<TownRow> kept;
    KeptTownStats ts;
    KeptTownsOf(said, ps, &kept, &ts);
    if (kept.size() != 3 || kept[0].name != "A" || kept[0].keys.size() != 2 || kept[0].keys[0] != "p1" || kept[0].keys[1] != "p2"
        || kept[1].name != "B" || kept[1].keys.size() != 1 || kept[1].keys[0] != "p9" || kept[2].name != "C" || kept[2].keys.size() != 1 || kept[2].keys[0] != "p5")
        Fail("outposts: the reset does not give each kept piece to the outpost whose said keys name it");
    if (ts.towns != 3 || ts.keys != 4 || ts.byKey != 5 || ts.piecesLeft != 2 || ts.piecesLeftTown != 2 || ts.saidUnused != 1)
        Fail("outposts: the reset's match counts are wrong");
    const std::map<std::string, size_t> ki = KeyIndex(kept);
    if (ki.size() != 4 || ki.find("p5")->second != 2 || ki.count("p3") != 0) Fail("outposts: the key index is wrong");
    std::vector<TownRow> none2;
    KeptTownsOf(none2, ps, &kept, &ts);
    if (!kept.empty() || ts.piecesLeft != 7) Fail("outposts: pieces with no said outpost carry one");
    /* the lay step */
    if (TownLayStep(1, 1, 1) != kStepCached || TownLayStep(0, 1, 1) != kStepStanding || TownLayStep(0, 0, 1) != kStepFound || TownLayStep(0, 0, 0) != kStepNone) Fail("outposts: the lay step is wrong");
    /* owner 938: the team gate - own and teammates' outposts get a town; no table yet waits; a non-teammate's gets none */
    if (KeptTownTeamGate(2, 2, 0, 0, 0) != kTeamGateTown || KeptTownTeamGate(2, 2, 0, 1, 0) != kTeamGateTown || KeptTownTeamGate(-1, -1, 1, 0, 0) != kTeamGateTown)
        Fail("outposts: this player's own kept outpost is gated on the team");
    if (KeptTownTeamGate(3, 2, 0, 1, 1) != kTeamGateTown) Fail("outposts: a teammate's kept outpost gets no town");
    if (KeptTownTeamGate(3, 2, 0, 1, 0) != kTeamGateNotMate) Fail("outposts: a non-teammate's kept outpost gets a town");
    if (KeptTownTeamGate(3, 2, 0, 0, 1) != kTeamGateWait || KeptTownTeamGate(3, 2, 0, 0, 0) != kTeamGateWait) Fail("outposts: a kept outpost is decided before the team table is known");
    if (KeptTownTeamGate(-1, 2, 0, 1, 1) != kTeamGateWait || KeptTownTeamGate(3, -1, 0, 1, 1) != kTeamGateWait) Fail("outposts: a kept outpost with no slot known is decided");
    const float at1[3] = { 1030.0f, 0.0f, -2030.0f }, at2[3] = { 1100.0f, 0.0f, -2000.0f };
    if (TownStands(at1, a) != 1 || TownStands(at2, a) != 0) Fail("outposts: a standing town is not judged by its distance from the centre");
}
/* the outposts review fold: the empty SAY, the earlier reset's outposts carried on, the handle match's reach, the relink rule */
/* a stolen mark whose owner is not on this game yet: parked only for a character or squad uid, matched again only to the same
   clean item at the same place, given up in time (src/common/ownpark.h) */
void t_ownpark_decisions()
{
    coopmark::OwnerId o;
    o.kind = coopmark::kOwnCharacter; o.uid = 1;
    if (!ownpark::ShouldPark(o)) Fail("ownpark: a character owner named by uid is not parked");
    o.kind = coopmark::kOwnSquad;
    if (!ownpark::ShouldPark(o)) Fail("ownpark: a squad owner named by uid is not parked");
    o.uid = 0;
    if (ownpark::ShouldPark(o)) Fail("ownpark: an owner with no uid (owner unknown at the sender) is parked");
    o.uid = 5; o.kind = coopmark::kOwnOther;
    if (ownpark::ShouldPark(o)) Fail("ownpark: a faction-only owner is parked");
    o.kind = coopmark::kOwnNone;
    if (ownpark::ShouldPark(o)) Fail("ownpark: no owner is parked");
    ownpark::Place a; a.inInv = 1; a.section = "main"; a.x = 2; a.y = 3;
    ownpark::Place b = a;
    if (ownpark::Judge(true, "s", a, "s", true, b) != ownpark::kFoundApply) Fail("ownpark: the same clean item at the same place is not marked");
    if (ownpark::Judge(false, "s", a, "s", true, b) != ownpark::kFoundGone) Fail("ownpark: a hand that names nothing is not dropped as gone");
    if (ownpark::Judge(true, "s", a, "t", true, b) != ownpark::kFoundChanged) Fail("ownpark: another record is marked");
    if (ownpark::Judge(true, "s", a, "s", false, b) != ownpark::kFoundChanged) Fail("ownpark: an item that carries a mark already is marked again");
    b.x = 4;
    if (ownpark::Judge(true, "s", a, "s", true, b) != ownpark::kFoundMoved) Fail("ownpark: an item in another cell is marked");
    b = a; b.section = "backpack_content";
    if (ownpark::Judge(true, "s", a, "s", true, b) != ownpark::kFoundMoved) Fail("ownpark: an item in another section is marked");
    b = a; b.inInv = 0;
    if (ownpark::Judge(true, "s", a, "s", true, b) != ownpark::kFoundMoved) Fail("ownpark: an item taken out of the inventory is marked");
    if (ownpark::Expired(1000u, 1000u) || ownpark::Expired(1000u + ownpark::kParkLimitMs - 1u, 1000u)) Fail("ownpark: a mark expires early");
    if (!ownpark::Expired(1000u + ownpark::kParkLimitMs, 1000u)) Fail("ownpark: a mark does not expire at the limit");
    const unsigned int late = 0xFFFFFF00u;
    if (!ownpark::Expired(late + ownpark::kParkLimitMs, late) || ownpark::Expired(late + 10u, late)) Fail("ownpark: the limit is wrong across the tick counter's wrap");
    if (ownpark::RetryDue(5000u, 5000u + 0u) || !ownpark::RetryDue(5000u + ownpark::kParkRetryMs, 5000u)) Fail("ownpark: the retry interval is wrong");
}
static coopbag::BagRow BagNestTRow(const char* sid, unsigned int parent, unsigned int x)
{
    coopbag::BagRow r;
    r.section = "backpack_content"; r.x = x; r.y = 0; r.qty = 1; r.baseSid = sid; r.parent = parent;
    return r;
}
static bool BagNestTHasMarker(const std::vector<char>& b)
{
    for (size_t i = 0; i + 4 <= b.size(); ++i)
    {
        unsigned int v = 0;
        std::memcpy(&v, &b[i], 4);
        if (v == coopbag::kBagNestMarker) return true;
    }
    return false;
}
/* `levels` rows, each inside the one before (row 0 in the outer pack), then one row beside the first */
static std::vector<coopbag::BagRow> BagNestTChain(int levels)
{
    std::vector<coopbag::BagRow> rows;
    for (int l = 0; l < levels; ++l) rows.push_back(BagNestTRow(l + 1 < levels ? "pack" : "leaf", (unsigned int)l, (unsigned int)l));
    rows.push_back(BagNestTRow("beside", 0, 9));
    return rows;
}
/* packs inside packs on the wire: the BAGN tail after the pack rows (src/common/bagwire.h) */
void t_bagnest_moves()
{
    /* the box's writer: the move stands and the box is pushed, whatever else is true */
    if (coopbag::NestMoveRoad(1, 1, 1, 0, 0, 1) != coopbag::kNestRoadPush || coopbag::NestMoveRoad(1, 1, 0, 1, 1, 0) != coopbag::kNestRoadPush
        || coopbag::NestMoveRoad(1, 1, 2, 0, 0, 0) != coopbag::kNestRoadPush)
        Fail("nested move: the box's writer is not pushed");
    /* a character's writer is put back like any other game */
    if (coopbag::NestMoveRoad(0, 1, 1, 1, 0, 0) != coopbag::kNestRoadUndo) Fail("nested move: a character's writer let a move two levels down stand");
    if (coopbag::NestMoveRoad(0, 1, 1, 0, 0, 1) != coopbag::kNestRoadPark) Fail("nested move: a character's writer did not keep a lifted item");
    /* not the writer: the other half here is put back with it, before the cursor is asked */
    if (coopbag::NestMoveRoad(1, 0, 0, 1, 0, 0) != coopbag::kNestRoadUndo || coopbag::NestMoveRoad(1, 0, 1, 1, 0, 1) != coopbag::kNestRoadUndo
        || coopbag::NestMoveRoad(1, 0, 0, 1, 1, 0) != coopbag::kNestRoadUndo)
        Fail("nested move: a move with both halves here is not put back");
    /* an add whose removal is the held item: back where it was lifted from; a removal never takes that road */
    if (coopbag::NestMoveRoad(1, 0, 0, 0, 1, 0) != coopbag::kNestRoadUndoHeld) Fail("nested move: a drop from the cursor is not put back");
    if (coopbag::NestMoveRoad(1, 0, 1, 0, 1, 0) != coopbag::kNestRoadStands) Fail("nested move: a removal took the held road");
    /* a removal onto the cursor is kept; an add is never kept */
    if (coopbag::NestMoveRoad(0, 0, 1, 0, 0, 1) != coopbag::kNestRoadPark) Fail("nested move: a lift onto the cursor is not kept");
    if (coopbag::NestMoveRoad(0, 0, 0, 0, 0, 1) != coopbag::kNestRoadStands) Fail("nested move: an add was kept for the cursor");
    /* nothing to put back against: a count change, a split, a lone half */
    if (coopbag::NestMoveRoad(1, 0, 2, 1, 1, 1) != coopbag::kNestRoadStands || coopbag::NestMoveRoad(0, 0, 3, 1, 0, 0) != coopbag::kNestRoadStands
        || coopbag::NestMoveRoad(1, 0, 1, 0, 0, 0) != coopbag::kNestRoadStands || coopbag::NestMoveRoad(0, 0, 0, 0, 0, 0) != coopbag::kNestRoadStands)
        Fail("nested move: a move with nothing to put back against did not stand");
    /* the pack digest sees a row moved into another inner pack */
    {
        std::vector<coopbag::BagRow> a;
        a.push_back(BagNestTRow("pack", 0, 0)); a.push_back(BagNestTRow("pack", 1, 1)); a.push_back(BagNestTRow("leaf", 2, 2));
        std::vector<coopbag::BagRow> b = a;
        b[2].parent = 1;
        if (!coopbag::BagNestValid(a) || !coopbag::BagNestValid(b)) Fail("nested move: the digest rows are not valid parents");
        if (coopbag::BagRowsDigest(a) == coopbag::BagRowsDigest(b)) Fail("nested move: the pack digest does not see a row's parent");
        if (coopbag::BagRowsDigest(a) != coopbag::BagRowsDigest(a)) Fail("nested move: the pack digest is not stable");
    }
    /* a bad BAGN tail on an add is its own reason (counted as bagNest badRecv); a bad rows block stays a bad backpack block */
    {
        const std::vector<coopbag::BagRow> deep = BagNestTChain(5);
        std::vector<coopbag::BagRow> flat = deep;
        for (size_t i = 0; i < flat.size(); ++i) flat[i].parent = 0;
        std::vector<char> t;
        coopbag::EncodeBagRows(&t, flat);
        coopbag::BagPutU32(&t, coopbag::kBagNestMarker);
        coopbag::BagPutU32(&t, (unsigned int)deep.size());
        for (size_t i = 0; i < deep.size(); ++i) coopbag::BagPutU16(&t, (unsigned short)deep[i].parent);
        std::vector<coopbag::BagRow> gr; coopmark::OwnerId go; size_t e2 = 0;
        if (coopistat::DecodeAddTail(&t[0], t.size(), 0, &gr, &go, &e2) != coopistat::kTailBadNest) Fail("nested move: an add's bad BAGN tail is not its own reason");
        if (std::string(coopistat::TailWhy(coopistat::kTailBadNest)) == "unknown") Fail("nested move: the bad BAGN reason has no words");
    }
    /* a box answer's pack rows: a bad BAGN tail and a bad gear tail have their own reasons, anything else is a bad bag block */
    if (cooppar::ParityBagRowWhy(coopbag::kBagBadNest) != cooppar::kParityDecodeBadNest
        || cooppar::ParityBagRowWhy(coopbag::kBagBadGear) != cooppar::kParityDecodeBadGear
        || cooppar::ParityBagRowWhy(coopbag::kBagOverCap) != cooppar::kParityDecodeBadBag)
        Fail("nested move: a box answer's pack-row reason is mapped wrong");
}
void t_bagnest_wire()
{
    for (int levels = 2; levels <= 4; ++levels)
    {
        const std::vector<coopbag::BagRow> rows = BagNestTChain(levels);
        std::vector<char> b;
        if (!coopbag::EncodeBagRows(&b, rows) || !BagNestTHasMarker(b)) { Fail("bag nest: packs inside a pack did not encode with their BAGN tail"); continue; }
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (coopbag::DecodeBagRows(&b[0], b.size(), 0, &back, &end) != coopbag::kBagOk || end != b.size() || back.size() != rows.size())
        { Fail("bag nest: packs inside a pack did not decode whole"); continue; }
        for (size_t i = 0; i < rows.size(); ++i)
            if (back[i].parent != rows[i].parent || back[i].baseSid != rows[i].baseSid || back[i].x != rows[i].x) Fail("bag nest: a row's parent or fields changed on the way");
        if (coopbag::BagNestDepth(back) != levels) Fail("bag nest: the decoded depth is not the sent depth");
        /* the encoder's tail is exactly the documented bytes: the rows sent flat, then marker | n | n x u16 parent */
        std::vector<coopbag::BagRow> flat = rows;
        for (size_t i = 0; i < flat.size(); ++i) flat[i].parent = 0;
        std::vector<char> hand;
        coopbag::EncodeBagRows(&hand, flat);
        coopbag::BagPutU32(&hand, coopbag::kBagNestMarker);
        coopbag::BagPutU32(&hand, (unsigned int)rows.size());
        for (size_t i = 0; i < rows.size(); ++i) coopbag::BagPutU16(&hand, (unsigned short)rows[i].parent);
        if (hand != b) Fail("bag nest: the BAGN tail is not the documented layout");
    }
    {   /* no pack inside: no tail, every parent 0, and what follows the block is left to its own reader */
        std::vector<coopbag::BagRow> flat;
        flat.push_back(BagNestTRow("a", 0, 0)); flat.push_back(BagNestTRow("b", 0, 1));
        std::vector<char> b;
        if (!coopbag::EncodeBagRows(&b, flat) || BagNestTHasMarker(b)) Fail("bag nest: a pack with no pack inside wrote a BAGN tail");
        const size_t blockEnd = b.size();
        coopbag::BagPutU32(&b, coopmark::kOwnMarker);
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (coopbag::DecodeBagRows(&b[0], b.size(), 0, &back, &end) != coopbag::kBagOk || back.size() != 2 || back[0].parent != 0 || back[1].parent != 0
            || end != blockEnd)
            Fail("bag nest: a block without the tail does not decode as before");
    }
    {   /* 5 levels: the encoder writes nothing; a hand-made tail naming 5 levels is refused */
        const std::vector<coopbag::BagRow> deep = BagNestTChain(5);
        std::vector<char> b;
        if (coopbag::EncodeBagRows(&b, deep) || !b.empty()) Fail("bag nest: a pack 5 levels deep was encoded");
        if (coopbag::BagNestValid(deep)) Fail("bag nest: 5 levels pass the parents' check");
        std::vector<coopbag::BagRow> flat = deep;
        for (size_t i = 0; i < flat.size(); ++i) flat[i].parent = 0;
        std::vector<char> f;
        coopbag::EncodeBagRows(&f, flat);
        std::vector<char> t = f;
        coopbag::BagPutU32(&t, coopbag::kBagNestMarker);
        coopbag::BagPutU32(&t, (unsigned int)deep.size());
        for (size_t i = 0; i < deep.size(); ++i) coopbag::BagPutU16(&t, (unsigned short)deep[i].parent);
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (coopbag::DecodeBagRows(&t[0], t.size(), 0, &back, &end) != coopbag::kBagBadNest) Fail("bag nest: a tail naming 5 levels is taken");
        /* malformed tails: a wrong count, a cut tail, a parent that is a later row */
        std::vector<char> c1 = f;
        coopbag::BagPutU32(&c1, coopbag::kBagNestMarker); coopbag::BagPutU32(&c1, (unsigned int)deep.size() - 1u);
        for (size_t i = 0; i + 1 < deep.size(); ++i) coopbag::BagPutU16(&c1, 0);
        if (coopbag::DecodeBagRows(&c1[0], c1.size(), 0, &back, &end) != coopbag::kBagBadNest) Fail("bag nest: a tail with the wrong count is taken");
        std::vector<char> c2 = t; c2.pop_back();
        if (coopbag::DecodeBagRows(&c2[0], c2.size(), 0, &back, &end) != coopbag::kBagBadNest) Fail("bag nest: a cut tail is taken");
        std::vector<char> c3 = f;
        coopbag::BagPutU32(&c3, coopbag::kBagNestMarker); coopbag::BagPutU32(&c3, (unsigned int)deep.size());
        coopbag::BagPutU16(&c3, 2);
        for (size_t i = 1; i < deep.size(); ++i) coopbag::BagPutU16(&c3, 0);
        if (coopbag::DecodeBagRows(&c3[0], c3.size(), 0, &back, &end) != coopbag::kBagBadNest) Fail("bag nest: a parent that is a later row is taken");
    }
    {   /* the 256-row cap counts every level */
        std::vector<coopbag::BagRow> big;
        big.push_back(BagNestTRow("pack", 0, 0));
        for (unsigned int i = 1; i < coopbag::kBagMaxRows; ++i) big.push_back(BagNestTRow("in", 1, i % 64u));
        std::vector<char> b;
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (!coopbag::EncodeBagRows(&b, big) || coopbag::DecodeBagRows(&b[0], b.size(), 0, &back, &end) != coopbag::kBagOk || back.size() != coopbag::kBagMaxRows)
            Fail("bag nest: 256 rows over two levels do not travel");
        big.push_back(BagNestTRow("over", 1, 70));
        std::vector<char> b2;
        if (coopbag::EncodeBagRows(&b2, big) || !b2.empty()) Fail("bag nest: 257 rows over two levels were encoded");
    }
    {   /* marks on inner rows travel on the same row indexes as the parents */
        std::vector<coopbag::BagRow> m = BagNestTChain(3);
        m[2].owner.kind = coopmark::kOwnCharacter; m[2].owner.uid = 7;
        m[1].gear.crafter = "Smith";
        std::vector<char> b;
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (!coopbag::EncodeBagRows(&b, m) || coopbag::DecodeBagRows(&b[0], b.size(), 0, &back, &end) != coopbag::kBagOk || back.size() != m.size())
            Fail("bag nest: marked inner rows did not travel");
        else if (!coopmark::OwnerEqual(back[2].owner, m[2].owner) || back[2].parent != 2 || back[1].gear.crafter != "Smith" || back[1].parent != 1
                 || back[0].owner.kind != coopmark::kOwnNone || back[3].owner.kind != coopmark::kOwnNone || !back[3].gear.crafter.empty())
            Fail("bag nest: an inner row's stolen mark or crafter landed on another row");
        /* the kit form carries the same rows, parents included */
        std::vector<coopbag::KitBag> kb(1);
        kb[0].kitIndex = 1; kb[0].rows = m;
        std::vector<char> k;
        std::vector<coopbag::KitBag> kback; size_t ke = 0; int rowWhy = 0;
        if (!coopbag::EncodeKitBags(&k, kb, 3) || coopbag::DecodeKitBags(&k[0], k.size(), 0, 3, &kback, &ke, &rowWhy) != coopbag::kKitBagOk
            || kback.size() != 1 || kback[0].rows.size() != m.size() || kback[0].rows[3].parent != 0 || kback[0].rows[2].parent != 2)
            Fail("bag nest: a kit's pack with packs inside lost its parents");
        /* the pack digest counts the inner rows */
        std::vector<coopbag::BagRow> m2 = m;
        m2[2].qty = 2;
        if (coopbag::BagRowsDigest(m) == coopbag::BagRowsDigest(m2)) Fail("bag nest: the pack digest does not see an inner row");
    }
}
void t_t36o_outposts_fold()
{
    using townkeep::TownRow;
    std::vector<TownRow> all;
    TownRow r2; r2.owner = 2; r2.name = "two"; all.push_back(r2);
    if (townkeep::EmptySayIgnored(all, 2, 0) != 1) Fail("outposts fold: an empty SAY clears a slot that has rows on file");
    if (townkeep::EmptySayIgnored(all, 2, 1) != 0) Fail("outposts fold: a SAY with outposts is not taken");
    if (townkeep::EmptySayIgnored(all, 5, 0) != 0) Fail("outposts fold: an empty SAY from a slot with no rows is ignored");
    /* the said outposts a reset matches against: an owner with none said takes the earlier reset's, with their keys */
    std::vector<TownRow> said, oldKept;
    TownRow s1; s1.owner = 1; s1.name = "said1"; said.push_back(s1);
    TownRow o1; o1.owner = 1; o1.name = "old1"; o1.keys.push_back("x1"); oldKept.push_back(o1);
    TownRow o4; o4.owner = 4; o4.name = "old4"; o4.hand[1] = 7; o4.hand[3] = 70; o4.pos[0] = 1000.0f; o4.pos[2] = 2000.0f;
    o4.keys.push_back("gone"); o4.keys.push_back("y1"); o4.keys.push_back("y2"); o4.keys.push_back("y3"); oldKept.push_back(o4);
    int added = -1;
    const std::vector<TownRow> sw = townkeep::SaidWithCarried(said, oldKept, &added);
    if (added != 1 || sw.size() != 2 || sw[0].name != "said1" || sw[1].name != "old4" || sw[1].owner != 4 || sw[1].keys.size() != 4)
        Fail("outposts fold: the earlier reset's outposts are not taken for an owner with none said (and only for that one)");
    /* the keys carried on: still kept, of the outpost's own owner, named by no outpost now */
    std::vector<zonekeep::KeptPiece> ps;
    zonekeep::KeptPiece p; p.ownerSlot = 4;
    p.key = "y1"; ps.push_back(p); p.key = "y2"; ps.push_back(p);
    p.key = "y3"; p.ownerSlot = 5; ps.push_back(p);   /* another player's piece now: not carried */
    p.key = "x1"; p.ownerSlot = 1; ps.push_back(p);
    std::vector<TownRow> out;
    townkeep::CarryStats cs;
    townkeep::CarryKeptKeys(oldKept, ps, &out, &cs);
    if (out.size() != 2 || out[0].name != "old1" || out[0].keys.size() != 1 || out[0].keys[0] != "x1" || out[1].name != "old4" || out[1].keys.size() != 2
        || out[1].keys[0] != "y1" || out[1].keys[1] != "y2" || cs.keys != 3 || cs.rows != 2 || cs.joined != 0)
        Fail("outposts fold: the earlier reset's outposts do not carry on the keys still kept");
    /* an outpost matched now at the old one's centre takes the keys it does not name yet; none is named twice */
    std::vector<TownRow> out2;
    TownRow z; z.owner = 4; z.name = "now4"; z.hand[3] = 99; z.pos[0] = 1020.0f; z.pos[2] = 2010.0f; z.keys.push_back("y1"); out2.push_back(z);
    townkeep::CarryKeptKeys(oldKept, ps, &out2, &cs);
    if (out2.size() != 2 || out2[0].name != "now4" || out2[0].keys.size() != 2 || out2[0].keys[0] != "y1" || out2[0].keys[1] != "y2" || cs.joined != 1 || cs.keys != 2)
        Fail("outposts fold: a key is not joined to the outpost standing at the old one's centre, or is named twice");
    for (size_t i = 0; i < out2.size(); ++i) if (!townkeep::RowOk(out2[i], townkeep::kOpList)) Fail("outposts fold: a carried row is not a valid LIST row");
    /* a town handle never matches, nor distance: only a said key */
    std::vector<TownRow> sd;
    TownRow a; a.owner = 2; a.hand[1] = 1; a.hand[3] = 11; a.hand[4] = 111; a.name = "A"; sd.push_back(a);
    std::vector<zonekeep::KeptPiece> far;
    zonekeep::KeptPiece q; q.ownerSlot = 2; q.hasTown = 1; for (int i = 0; i < 5; ++i) q.townHand[i] = a.hand[i];
    q.key = "far1"; q.pos[0] = 100.0f; far.push_back(q);
    std::vector<TownRow> kt;
    townkeep::KeptTownStats ts;
    townkeep::KeptTownsOf(sd, far, &kt, &ts);
    if (!kt.empty() || ts.byKey != 0 || ts.piecesLeft != 1 || ts.piecesLeftTown != 1) Fail("outposts fold: a kept piece is matched by its town handle or its distance");
    sd[0].keys.push_back("far1");
    townkeep::KeptTownsOf(sd, far, &kt, &ts);
    if (kt.size() != 1 || kt[0].keys.size() != 1 || kt[0].keys[0] != "far1" || ts.byKey != 1) Fail("outposts fold: a said key does not match its kept piece");
}
/* the outposts' keys: a SAY with keys on the wire and in player-towns.txt (formats 1 and 2), a split SAY put together, a cut list
   keeping the keys on file, the reset's key-only match (the two texts of one key, the hashed record field, a tenth of a unit), a
   bought house never matched, an empty SAY read whole clearing a slot, the earlier reset's keys carried with a drift */
void t_t36o_outposts_keys()
{
    using namespace townkeep;
    const std::string kShort = "5-wall.mod@31,32@123450,234560,1000";
    const std::string kDrift1 = "5-wall.mod@31,32@123451,234559,1000";   /* a tenth of a unit off on x and z: the same piece */
    const std::string kDrift2 = "5-wall.mod@31,32@123452,234560,1000";   /* two tenths: another piece */
    const std::string kHouse = "9-house.mod@31,32@123450,234560,1000";   /* another record at the same spot */
    const std::string kLong = "77-a-long-record-name-for-keys.mod@31,32@123460,234570,1000";   /* a key with two texts */
    char wb[poskey::kKeyCap];
    if (poskey::WireForm(kLong.c_str(), wb, (int)sizeof(wb)) != 2) Fail("outposts keys: the long key has no hashed text");
    const std::string kWire(wb);
    char hb[64];
    int hw = poskey::AppendStr(hb, (int)sizeof(hb), 0, "#");
    hw = poskey::AppendHex8(hb, (int)sizeof(hb), hw, poskey::SidHash32("5-wall.mod"));
    hw = poskey::AppendStr(hb, (int)sizeof(hb), hw, "@31,32@123450,234560,1000");
    const std::string kHashed(hb);   /* kShort with its record field hashed */
    if (TownKeysSame(kLong, kWire) != 1 || TownKeysSame(kShort, kHashed) != 1 || TownKeysSame(kShort, kDrift1) != 1 || TownKeysSame(kShort, kDrift2) != 0
        || TownKeysSame(kShort, kHouse) != 0 || TownKeysSame("p1", "p1") != 1 || TownKeysSame("p1", "p2") != 0)
        Fail("outposts keys: the same-piece rule is wrong");
    /* a SAY with keys and a cut list round-trips; its flags and the cut bit are words a decoder without them refuses */
    TownRow s; s.owner = -1; s.name = "Camp"; s.pos[0] = 100.0f; s.keys.push_back(kShort); s.keys.push_back(kWire); s.cut = 1;
    std::vector<TownRow> one(1, s);
    std::vector<char> m; unsigned op = 0; int fl = 0; std::vector<TownRow> back;
    if (!EncodeTowns(&m, kOpSay, (int)(kSayFirst | kSayWhole), one) || DecodeTowns(&m[0], m.size(), &op, &fl, &back) != 1 || op != kOpSay
        || fl != (int)(kSayFirst | kSayWhole) || back.size() != 1 || back[0].keys != s.keys || back[0].cut != 1 || back[0].owner != -1)
        Fail("outposts keys: a SAY with keys and a cut list does not round-trip");
    unsigned flagsWord = 0;
    std::memcpy(&flagsWord, &m[4], 4);
    if (flagsWord <= 1) Fail("outposts keys: a SAY's flags fit the old first word");
    if (EncodeTowns(&m, kOpList, (int)kSayWhole, std::vector<TownRow>())) Fail("outposts keys: a LIST takes a SAY flag");
    if (EncodeTowns(&m, kOpSay, 8, one)) Fail("outposts keys: an unknown SAY flag encodes");
    TownRow badCut(s); badCut.cut = 2; if (RowOk(badCut, kOpSay)) Fail("outposts keys: a cut of 2 is valid");
    TownRow tooMany(s); tooMany.keys.clear(); tooMany.cut = 0;
    for (size_t i = 0; i <= kMaxTownKeys; ++i) tooMany.keys.push_back("k" + zonekeep::ZkNum((long long)i));
    if (RowOk(tooMany, kOpSay)) Fail("outposts keys: a SAY row past the key cap is valid");
    /* player-towns.txt: format 4 with keys, the cut field, the size level (T-811) and the outpost id (T-1047 step 5); formats 3, 2
       and 1 still read */
    TownRow fr1(s); fr1.owner = 3; fr1.level = 2; fr1.id = 7u;
    const std::vector<TownRow> frs(1, fr1);
    const std::string pt = EncodeTownsFile(kPlayerTownsHeader, frs, true);
    std::vector<TownRow> fb; std::string why;
    if (pt.compare(0, 15, "player-towns\t6\n") != 0 || !DecodeTownsFile(pt, kPlayerTownsHeader, true, &fb, &why) || fb.size() != 1 || fb[0].keys != fr1.keys || fb[0].cut != 1
        || fb[0].level != 2 || fb[0].id != 7u || fb[0].name != fr1.name)
        Fail("outposts keys: player-towns.txt with keys does not round-trip: " + why);
    if (!DecodeTownsFile("player-towns\t1\nT\t3\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tOld\n", kPlayerTownsHeader, true, &fb, &why) || fb.size() != 1
        || fb[0].name != "Old" || !fb[0].keys.empty() || fb[0].cut != 0)
        Fail("outposts keys: a format 1 player-towns.txt does not read: " + why);
    if (DecodeTownsFile("player-towns\t1\nT\t3\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tOld\t1\n", kPlayerTownsHeader, true, &fb, &why))
        Fail("outposts keys: a format 1 line with a cut field reads");
    if (DecodeTownsFile("player-towns\t2\nT\t3\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tOld\n", kPlayerTownsHeader, true, &fb, &why))
        Fail("outposts keys: a format 2 line with no cut field reads");
    if (DecodeTownsFile("player-towns\t2\nT\t3\t0\t0\t0\t0\t0\t00000000\t00000000\t00000000\tOld\t2\n", kPlayerTownsHeader, true, &fb, &why))
        Fail("outposts keys: a cut field of 2 reads");
    /* a SAY past one message is split between its rows and put together again */
    std::vector<TownRow> bigSay;
    for (int r = 0; r < 5; ++r)
    {
        TownRow t; t.owner = -1; t.name = "Big" + zonekeep::ZkNum((long long)r); t.pos[0] = 1000.0f * (float)r;
        for (int i = 0; i < 400; ++i) t.keys.push_back("r" + zonekeep::ZkNum((long long)r) + "-" + std::string(30, 'x') + "-" + zonekeep::ZkNum((long long)i));
        bigSay.push_back(t);
    }
    std::vector<std::vector<char> > msgs;
    if (!EncodeTownsSay(&msgs, bigSay, 1) || msgs.size() != 2) Fail("outposts keys: a SAY past one message is not split in two");
    SayAssembly as;
    std::vector<TownRow> done; int whole = -1; int took = -1;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        if (msgs[i].size() > kTownsMsgBytes) Fail("outposts keys: a split SAY message is past the message limit");
        if (DecodeTowns(&msgs[i][0], msgs[i].size(), &op, &fl, &back) != 1 || op != kOpSay) { Fail("outposts keys: a split SAY message does not decode"); break; }
        const unsigned want = (i == 0 ? kSayFirst : 0u) | (i + 1 < msgs.size() ? kSayMore : 0u) | kSayWhole;
        if ((unsigned)fl != want) Fail("outposts keys: a split SAY message's flags are wrong");
        took = SayTake(&as, (unsigned)fl, back, &done, &whole);
        if (i + 1 < msgs.size() && took != kSayPart) Fail("outposts keys: a split SAY ends early");
    }
    if (took != kSayDone || whole != 1 || done.size() != 5 || done[4].keys != bigSay[4].keys || done[0].name != "Big0") Fail("outposts keys: a split SAY is not put together whole");
    SayAssembly fresh;
    if (SayTake(&fresh, kSayWhole, back, &done, &whole) != kSayOrphan) Fail("outposts keys: a SAY's later message with none started is taken");
    std::vector<TownRow> many;
    for (int r = 0; r < 40; ++r) { TownRow t; t.owner = -1; t.name = "M"; t.pos[0] = 100.0f * (float)r; many.push_back(t); }
    if (!EncodeTownsSay(&msgs, many, 1) || msgs.size() != 2) Fail("outposts keys: a SAY past the rows of one message is not split");
    SayAssembly as2;
    for (size_t i = 0; i < msgs.size(); ++i)
        if (DecodeTowns(&msgs[i][0], msgs[i].size(), &op, &fl, &back) == 1) took = SayTake(&as2, (unsigned)fl, back, &done, &whole);
    if (took != kSayDone || done.size() != kMaxTownsPerSlot || whole != 0) Fail("outposts keys: a SAY past the outposts a player keeps is taken as read whole");
    if (!EncodeTownsSay(&msgs, std::vector<TownRow>(), 1) || msgs.size() != 1 || DecodeTowns(&msgs[0][0], msgs[0].size(), &op, &fl, &back) != 1
        || (unsigned)fl != (kSayFirst | kSayWhole) || !back.empty())
        Fail("outposts keys: an empty SAY read whole is not one message");
    /* a cut list keeps the keys on file; a whole one replaces them; a SAY not read whole keeps the outposts it does not name */
    std::vector<TownRow> all;
    TownRow f1; f1.owner = 2; f1.name = "Fort"; f1.keys.push_back("a1"); f1.keys.push_back("b1"); f1.keys.push_back(kShort); all.push_back(f1);
    TownRow other; other.owner = 1; other.name = "Other"; other.keys.push_back("a1"); all.push_back(other);
    TownRow n1; n1.owner = -1; n1.name = "Fort"; n1.pos[0] = 10.0f; n1.keys.push_back("a1"); n1.keys.push_back("d1"); n1.cut = 1;
    std::vector<TownRow> said1(1, n1);
    std::vector<TownRow> t1(all);
    ReplaceSlotTowns(&t1, 2, said1, 1);
    if (t1.size() != 2 || t1[0].owner != 1 || t1[0].keys.size() != 1 || t1[1].owner != 2 || t1[1].keys.size() != 4 || t1[1].keys[0] != "a1" || t1[1].keys[1] != "d1"
        || t1[1].keys[2] != "b1" || t1[1].keys[3] != kShort || t1[1].cut != 1)
        Fail("outposts keys: a cut list drops the keys on file");
    said1[0].cut = 0; t1 = all;
    ReplaceSlotTowns(&t1, 2, said1, 1);
    if (t1.size() != 2 || t1[1].keys.size() != 2) Fail("outposts keys: a whole list does not replace the keys on file");
    TownRow n2; n2.owner = -1; n2.name = "Fort"; n2.pos[0] = 5000.0f; n2.keys.push_back(kDrift1); n2.cut = 1;
    t1 = all; ReplaceSlotTowns(&t1, 2, std::vector<TownRow>(1, n2), 1);
    if (t1.size() != 2 || t1[1].keys.size() != 3 || t1[1].keys[0] != kDrift1) Fail("outposts keys: a cut list is not matched to its outpost on file by a drifted key");
    TownRow n3; n3.owner = -1; n3.name = "New"; n3.pos[0] = -9000.0f;
    t1 = all; ReplaceSlotTowns(&t1, 2, std::vector<TownRow>(1, n3), 0);
    if (t1.size() != 3 || t1[1].name != "New" || t1[2].name != "Fort" || t1[2].keys.size() != 3) Fail("outposts keys: a SAY not read whole drops an outpost it does not name");
    t1 = all; ReplaceSlotTowns(&t1, 2, std::vector<TownRow>(1, n3), 1);
    if (t1.size() != 2 || t1[1].name != "New") Fail("outposts keys: a SAY read whole keeps an outpost it does not name");
    /* the empty report: the first and second read whole in a row keep the rows, the third clears them; a report with outposts, or one
       not read whole, in between starts the run again */
    int run = 0;
    run = EmptyWholeRun(run, 0, 1);
    if (run != 1 || EmptySayIgnored(all, 2, 0, run) != 1) Fail("outposts keys: the first empty SAY read whole clears the rows");
    run = EmptyWholeRun(run, 0, 1);
    if (run != 2 || EmptySayIgnored(all, 2, 0, run) != 1) Fail("outposts keys: the second empty SAY read whole clears the rows");
    run = EmptyWholeRun(run, 0, 1);
    if (run != 3 || EmptySayIgnored(all, 2, 0, run) != 0) Fail("outposts keys: the third empty SAY read whole does not clear the rows");
    t1 = all; ReplaceSlotTowns(&t1, 2, std::vector<TownRow>(), 1);
    if (t1.size() != 1 || t1[0].owner != 1) Fail("outposts keys: an empty SAY read whole, once taken, does not clear its slot's rows");
    if (EmptyWholeRun(run, 0, 1) != kEmptyWholeToClear) Fail("outposts keys: the run of empty SAYs grows past its need");
    run = EmptyWholeRun(0, 0, 1); run = EmptyWholeRun(run, 0, 1);
    run = EmptyWholeRun(run, 1, 1);
    if (run != 0) Fail("outposts keys: a SAY with outposts does not start the run again");
    run = EmptyWholeRun(run, 0, 1); run = EmptyWholeRun(run, 0, 1);
    if (run != 2 || EmptySayIgnored(all, 2, 0, run) != 1) Fail("outposts keys: an empty SAY after a SAY with outposts counts the earlier run");
    if (EmptyWholeRun(2, 0, 0) != 0 || EmptySayIgnored(all, 2, 0, EmptyWholeRun(2, 0, 0)) != 1) Fail("outposts keys: an empty SAY not read whole does not start the run again");
    if (EmptySayIgnored(all, 5, 0, 0) != 0 || EmptySayIgnored(all, 2, 1, 0) != 0) Fail("outposts keys: an empty SAY of a slot with no rows, or a SAY with outposts, is ignored");
    /* the reset's match: a hashed record field against a plain one and a tenth of a unit off match; two tenths, another record
       at the same spot (a bought house) and another player's piece do not */
    TownRow r1; r1.owner = 2; r1.name = "Keys"; r1.keys.push_back(kWire); r1.keys.push_back(kHashed);
    std::vector<TownRow> saidK(1, r1);
    std::vector<zonekeep::KeptPiece> pk;
    zonekeep::KeptPiece kp; kp.ownerSlot = 2; kp.hasTown = 1; kp.townHand[3] = 77;
    char wd[poskey::kKeyCap];
    if (poskey::WireForm("77-a-long-record-name-for-keys.mod@31,32@123461,234570,1000", wd, (int)sizeof(wd)) != 2) Fail("outposts keys: the drifted long key has no hashed text");
    const std::string kWireDrift(wd);   /* the long key's wire text a tenth of a unit off */
    kp.key = kWireDrift; pk.push_back(kp);
    kp.key = kDrift1; pk.push_back(kp);
    kp.key = kDrift2; pk.push_back(kp);
    kp.key = kHouse; kp.townHand[3] = 5; pk.push_back(kp);
    kp.key = kShort; kp.ownerSlot = 3; pk.push_back(kp);
    std::vector<TownRow> kt; KeptTownStats ts;
    KeptTownsOf(saidK, pk, &kt, &ts);
    if (kt.size() != 1 || kt[0].keys.size() != 2 || kt[0].keys[0] != kWireDrift || kt[0].keys[1] != kDrift1 || ts.byKey != 2 || ts.piecesLeft != 3 || ts.piecesLeftTown != 3)
        Fail("outposts keys: the reset's key match is wrong");
    std::vector<TownRow> saidH(saidK); saidH[0].keys.push_back(kDrift2); saidH[0].cut = 1;
    KeptTownsOf(saidH, pk, &kt, &ts);
    if (kt.size() != 1 || kt[0].keys.size() != 3 || ts.saidCut != 1 || kt[0].cut != 0) Fail("outposts keys: a cut said list is not counted, or comes out cut");
    for (size_t i = 0; i < kt.size(); ++i) for (size_t k = 0; k < kt[i].keys.size(); ++k) if (kt[i].keys[k] == kHouse) Fail("outposts keys: a bought house joined an outpost");
    /* the earlier reset's outposts: their keys carried with a drift, by SaidWithCarried and by CarryKeptKeys */
    std::vector<TownRow> oldKept;
    TownRow o4; o4.owner = 4; o4.name = "Old4"; o4.pos[0] = 1000.0f; o4.pos[2] = 2000.0f; o4.keys.push_back(kShort); o4.keys.push_back("gone"); oldKept.push_back(o4);
    std::vector<zonekeep::KeptPiece> pc;
    zonekeep::KeptPiece c4; c4.ownerSlot = 4; c4.key = kDrift1; pc.push_back(c4);
    int added = 0;
    const std::vector<TownRow> sw = SaidWithCarried(std::vector<TownRow>(), oldKept, &added);
    KeptTownsOf(sw, pc, &kt, &ts);
    if (added != 1 || kt.size() != 1 || kt[0].name != "Old4" || kt[0].keys.size() != 1 || kt[0].keys[0] != kDrift1) Fail("outposts keys: an absent player's outpost does not keep its drifted piece");
    std::vector<TownRow> out;
    CarryStats cs;
    CarryKeptKeys(oldKept, pc, &out, &cs);
    if (out.size() != 1 || out[0].keys.size() != 1 || out[0].keys[0] != kDrift1 || cs.keys != 1 || cs.rows != 1) Fail("outposts keys: the earlier reset's key is not carried on with a drift");
    CarryKeptKeys(oldKept, pc, &kt, &cs);
    if (kt.size() != 1 || kt[0].keys.size() != 1 || cs.keys != 0) Fail("outposts keys: a carried key is named twice");
    const std::string kt2 = EncodeTownsFile(kKeptTownsHeader, out, true);
    if (!DecodeTownsFile(kt2, kKeptTownsHeader, true, &fb, &why) || fb.size() != 1 || fb[0].keys != out[0].keys) Fail("outposts keys: kept-towns.txt at format 2 does not round-trip: " + why);
}
void t_t36r_outposts_near()
{
    using namespace townkeep;
    const std::set<std::string> noWorld, noKeys;
    std::vector<zonekeep::KeptPiece> ps;
    zonekeep::KeptPiece p;
    p.ownerSlot = 2; p.key = "a"; p.pos[0] = 500.0f; ps.push_back(p);       /* within the radius of the outpost below */
    p.key = "b"; p.pos[0] = 1500.0f; ps.push_back(p);                           /* outside it */
    p.ownerSlot = 3; p.key = "c"; p.pos[0] = 100.0f; ps.push_back(p);          /* another player's piece near its centre */
    /* a said outpost with no keys takes its owner's piece within the radius, not one outside it nor another player's */
    std::vector<TownRow> said(1);
    said[0].owner = 2; said[0].name = "Bare"; said[0].hand[3] = 41;
    std::vector<TownRow> out;
    KeptTownStats ts; NearStats ns; MadeStats ms;
    KeptTownsOf(said, ps, &out, &ts);
    if (!out.empty() || ts.saidUnused != 1) Fail("outposts near: the key match gave a keyless outpost a piece");
    NearOutpostsTake(said, ps, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].name != "Bare" || out[0].owner != 2 || out[0].hand[3] != 41 || out[0].keys.size() != 1 || out[0].keys[0] != "a" || ns.took != 1 || ns.rows != 1)
        Fail("outposts near: a said outpost with no keys does not take its owner's piece within the radius alone");
    /* the pieces still left: "b" (far from its owner's outpost) and "c" (its owner said none) each get an outpost made */
    OutpostsFromPieces(said, ps, noWorld, noKeys, &out, &ms);
    if (out.size() != 3 || ms.made.size() != 2 || ms.pieces != 2 || ms.nearOther != 0 || out[1].owner != 2 || out[1].keys.size() != 1 || out[1].keys[0] != "b"
        || !out[1].name.empty() || HandKnown(out[1].hand) || out[1].pos[0] != 1500.0f || out[2].owner != 3 || out[2].keys.size() != 1 || out[2].keys[0] != "c")
        Fail("outposts near: the pieces left far away, or of a player with no outpost, get no outpost of their own");
    for (size_t i = 0; i < out.size(); ++i) if (!RowOk(out[i], kOpList)) Fail("outposts near: an outpost row is not a valid LIST row");
    /* a cut key list likewise: its keyed piece by key, the piece within the radius by distance, the far one not */
    std::vector<zonekeep::KeptPiece> pc(ps);
    p.ownerSlot = 2; p.key = "k1"; p.pos[0] = 0.0f; pc.push_back(p);
    std::vector<TownRow> saidC(said);
    saidC[0].keys.push_back("k1"); saidC[0].cut = 1;
    KeptTownsOf(saidC, pc, &out, &ts);
    NearOutpostsTake(saidC, pc, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].keys.size() != 2 || out[0].keys[0] != "a" || out[0].keys[1] != "k1" || ns.took != 1 || ns.rows != 0)
        Fail("outposts near: a said outpost with a cut key list does not take its owner's piece within the radius alone");
    /* a whole key list takes its owner's unnamed piece within the radius too (one built after the last say); the far ones get outposts made */
    std::vector<TownRow> saidW(saidC);
    saidW[0].cut = 0;
    KeptTownsOf(saidW, pc, &out, &ts);
    NearOutpostsTake(saidW, pc, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].keys.size() != 2 || out[0].keys[0] != "a" || out[0].keys[1] != "k1" || ns.took != 1 || ns.rows != 0)
        Fail("outposts near: a said outpost with a whole key list does not take its owner's unnamed piece within the radius");
    OutpostsFromPieces(saidW, pc, noWorld, noKeys, &out, &ms);
    if (ms.nearOther != 0 || ms.made.size() != 2 || out.size() != 3) Fail("outposts near: the pieces a whole-listed outpost does not reach get no outpost made");
    /* no outpost said: one made per group - by the town the area records name, then by distance; another player's pieces in their own */
    std::vector<zonekeep::KeptPiece> pg;
    zonekeep::KeptPiece g;
    g.ownerSlot = 2; g.hasTown = 1; g.townHand[3] = 9; g.pos[1] = 10.0f; g.pos[2] = 5000.0f;
    g.key = "p1"; g.pos[0] = 5000.0f; pg.push_back(g);
    g.key = "p2"; g.pos[0] = 5200.0f; pg.push_back(g);
    g.key = "t2"; g.townHand[3] = 8; g.pos[0] = 5600.0f; pg.push_back(g);                    /* another town near them: its own outpost */
    g.key = "p3"; g.hasTown = 0; g.townHand[3] = 0; g.pos[0] = 5100.0f; pg.push_back(g);     /* no town: joins the first group near it */
    g.key = "q1"; g.pos[0] = 20000.0f; g.pos[2] = 0.0f; pg.push_back(g);                     /* far away: its own */
    g.key = "r1"; g.ownerSlot = 3; g.pos[0] = 5000.0f; g.pos[2] = 5000.0f; pg.push_back(g);  /* another player's: its own */
    out.clear();
    OutpostsFromPieces(std::vector<TownRow>(), pg, noWorld, noKeys, &out, &ms);
    if (out.size() != 4 || ms.made.size() != 4 || ms.pieces != 6 || out[0].owner != 2 || out[0].keys.size() != 3 || out[0].keys[0] != "p1" || out[0].keys[1] != "p2"
        || out[0].keys[2] != "p3" || out[0].pos[0] != 5100.0f || out[0].pos[2] != 5000.0f || out[1].keys.size() != 1 || out[1].keys[0] != "t2"
        || out[2].keys.size() != 1 || out[2].keys[0] != "q1" || out[3].owner != 3 || out[3].keys.size() != 1 || out[3].keys[0] != "r1")
        Fail("outposts near: the outposts made from kept pieces are not one per town and place, each of one owner");
    /* the made outposts are a valid kept-towns.txt, and on the next reset two of them (no handle) are not joined to each other */
    std::vector<TownRow> fb;
    std::string why;
    if (!DecodeTownsFile(EncodeTownsFile(kKeptTownsHeader, out, true), kKeptTownsHeader, true, &fb, &why) || fb.size() != 4 || fb[0].keys != out[0].keys || !fb[0].name.empty())
        Fail("outposts near: kept-towns.txt with a made outpost does not round-trip: " + why);
    std::vector<TownRow> carried;
    CarryStats cs;
    CarryKeptKeys(out, pg, &carried, &cs);
    if (carried.size() != 4 || cs.joined != 0 || cs.rows != 4) Fail("outposts near: outposts made with no handle were joined to each other on the next reset");
}
void t_t36r_outposts_fold()
{
    using namespace townkeep;
    const std::set<std::string> noWorld, noKeys;
    std::vector<TownRow> out;
    NearStats ns; MadeStats ms;
    /* two keyless outposts of one player both reach a piece: the nearer takes it */
    std::vector<TownRow> said2(2);
    said2[0].owner = 2; said2[0].name = "West"; said2[0].hand[3] = 51;
    said2[1].owner = 2; said2[1].name = "East"; said2[1].hand[3] = 52; said2[1].pos[0] = 1200.0f;
    std::vector<zonekeep::KeptPiece> pn;
    zonekeep::KeptPiece p;
    p.ownerSlot = 2; p.key = "mid"; p.pos[0] = 700.0f; pn.push_back(p);
    NearOutpostsTake(said2, pn, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].name != "East" || out[0].keys.size() != 1 || out[0].keys[0] != "mid" || ns.took != 1)
        Fail("outposts fold: the nearer of two outposts in reach did not take the piece");
    /* a bought house and its furniture in a world town: no near outpost takes them and no outpost is made; with no world town known, taken */
    std::vector<zonekeep::KeptPiece> pw;
    zonekeep::KeptPiece h; h.ownerSlot = 2; h.key = "house"; h.pos[0] = 300.0f; h.hasTown = 1; h.townHand[0] = 3; h.townHand[3] = 77; pw.push_back(h);
    zonekeep::KeptPiece f; f.ownerSlot = 2; f.key = "bed"; f.pos[0] = 302.0f; f.hostKey = "house"; pw.push_back(f);
    std::set<std::string> world; world.insert(zonekeep::TownHandText(h.townHand));
    std::vector<TownRow> said1(1); said1[0].owner = 2; said1[0].name = "Base"; said1[0].hand[3] = 41;
    out.clear();
    NearOutpostsTake(said1, pw, world, &out, &ns);
    OutpostsFromPieces(said1, pw, world, ns.overKeys, &out, &ms);
    if (!out.empty() || ns.took != 0 || ms.worldTown != 2 || !ms.made.empty()) Fail("outposts fold: a bought house or its furniture in a world town joined or made an outpost");
    OutpostsFromPieces(std::vector<TownRow>(), pw, world, noKeys, &out, &ms);
    if (!out.empty() || ms.worldTown != 2) Fail("outposts fold: a bought house in a world town got an outpost made with no outpost said");
    NearOutpostsTake(said1, pw, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].keys.size() != 2 || ns.took != 2) Fail("outposts fold: with no world town known the pieces near the outpost were not taken");
    /* furniture takes its host's town only: its own words naming a world town (copied from its host as it was made) do not count */
    {
        std::vector<zonekeep::KeptPiece> pf;
        zonekeep::KeptPiece hb; hb.ownerSlot = 2; hb.key = "hall"; hb.pos[0] = 300.0f; pf.push_back(hb);
        zonekeep::KeptPiece fb; fb.ownerSlot = 2; fb.key = "chest"; fb.pos[0] = 301.0f; fb.hostKey = "hall"; fb.hasTown = 1;
        for (int i = 0; i < 5; ++i) fb.townHand[i] = h.townHand[i];
        pf.push_back(fb);
        const std::map<std::string, size_t> bk = PieceIndex(pf);
        if (InWorldTown(pf[1], pf, bk, world) != 0) Fail("outposts fold: furniture read its own town words, not its host's");
        pf[0].hasTown = 1;
        for (int i = 0; i < 5; ++i) pf[0].townHand[i] = h.townHand[i];
        if (InWorldTown(pf[1], pf, bk, world) != 1) Fail("outposts fold: furniture in a host standing in a world town is not in that town");
    }
    /* a town a player's game said it holds is never a world town */
    std::vector<TownRow> saidT(1); saidT[0].owner = 2; for (int i = 0; i < 5; ++i) saidT[0].hand[i] = h.townHand[i];
    if (!WorldTownsOf(world, saidT).empty() || WorldTownsOf(world, said1).size() != 1) Fail("outposts fold: a said town counts as a world town, or another town was dropped");
    /* the area record: a building no player owns names its town as a world town; the player's own chest names none */
    {
        zonekeep::Container c = T36gArea();
        zonekeep::Inst e2 = T36gInst("1459-gamedata.base"); e2.pos[0] = 95950.0f; e2.pos[1] = 3077.0f; e2.pos[2] = 95300.0f;
        e2.strs.push_back("st2");
        c.recs[0].insts.push_back(e2);
        zonekeep::Rec s2 = T36gRec(zonekeep::kTypeState, "st2");
        s2.strs["owner faction ID"] = "12-gamedata.base";   /* a non-player faction */
        s2.ints["townTYPE"] = 3; s2.ints["townC"] = 0; s2.ints["townCS"] = 0; s2.ints["townI"] = 77; s2.ints["townS"] = 0;
        c.recs.push_back(s2);
        std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st; std::string why;
        zonekeep::ExtractKeptPieces(c, 0, &ps, &st, &why);
        if (ps.size() != 1 || st.nobody != 1 || st.worldTowns.size() != 1 || st.worldTowns.count(zonekeep::TownHandText(h.townHand)) != 1)
            Fail("outposts fold: the town a building no player owns names is not read as a world town (" + I((int)st.worldTowns.size()) + ")");
    }
    /* pieces at 0, 900 and 1800 are one group whatever their order (the middle one joins the other two) */
    std::vector<zonekeep::KeptPiece> pl;
    p.key = "a"; p.pos[0] = 0.0f; pl.push_back(p);
    p.key = "b"; p.pos[0] = 1800.0f; pl.push_back(p);
    p.key = "c"; p.pos[0] = 900.0f; pl.push_back(p);
    for (int order = 0; order < 3; ++order)
    {
        std::vector<zonekeep::KeptPiece> po(pl);
        if (order == 1) std::reverse(po.begin(), po.end());
        if (order == 2) std::swap(po[0], po[2]);
        out.clear();
        OutpostsFromPieces(std::vector<TownRow>(), po, noWorld, noKeys, &out, &ms);
        if (out.size() != 1 || out[0].keys.size() != 3 || out[0].keys[0] != "a" || out[0].pos[0] != 900.0f)
            Fail("outposts fold: a base in a line is not one outpost (order " + I(order) + ")");
    }
    /* a group past the key cap goes on in a further outpost */
    std::vector<zonekeep::KeptPiece> pb;
    for (size_t i = 0; i <= kMaxTownKeys; ++i) { p.key = "k" + zonekeep::ZkNum((long long)(1000 + i)); p.pos[0] = (float)i; pb.push_back(p); }
    out.clear();
    OutpostsFromPieces(std::vector<TownRow>(), pb, noWorld, noKeys, &out, &ms);
    if (out.size() != 2 || ms.split != 1 || ms.pieces != (int)kMaxTownKeys + 1 || out[0].keys.size() != kMaxTownKeys || out[1].keys.size() != 1
        || out[1].keys[0] != "k" + zonekeep::ZkNum((long long)(1000 + kMaxTownKeys)))
        Fail("outposts fold: a group past the key cap did not go on in a second outpost");
    /* a said outpost already at the cap: the piece in reach past it goes to an outpost made, not held back as near */
    std::vector<TownRow> saidF(1); saidF[0].owner = 2; saidF[0].name = "Full"; saidF[0].hand[3] = 61;
    for (size_t i = 0; i < kMaxTownKeys; ++i) saidF[0].keys.push_back(pb[i].key);
    out = saidF;
    NearOutpostsTake(saidF, pb, noWorld, &out, &ns);
    if (out.size() != 1 || out[0].keys.size() != kMaxTownKeys || ns.over != 1 || ns.overKeys.size() != 1) Fail("outposts fold: the piece past a full outpost is not counted and named");
    OutpostsFromPieces(saidF, pb, noWorld, ns.overKeys, &out, &ms);
    if (out.size() != 2 || ms.made.size() != 1 || out[1].keys.size() != 1 || ms.nearOther != 0) Fail("outposts fold: the piece past a full outpost got no outpost made");
    /* rows a LIST cannot carry (an owner past the slots, a key the wire refuses): counted, not taken, no outpost */
    std::vector<zonekeep::KeptPiece> pr;
    p.ownerSlot = kOwnerMax + 1; p.key = "ok1"; p.pos[0] = 10.0f; pr.push_back(p);
    p.ownerSlot = 2; p.key = std::string((size_t)keptwire::kKeyMax + 1, 'x'); pr.push_back(p);
    out.clear();
    NearOutpostsTake(said1, pr, noWorld, &out, &ns);
    OutpostsFromPieces(said1, pr, noWorld, noKeys, &out, &ms);
    if (!out.empty() || ns.took != 0 || ms.refused != 2 || !ms.made.empty()) Fail("outposts fold: a row a LIST cannot carry was taken or made an outpost");
    /* an outpost made at one reset (no name, no handle) joins the said outpost standing at its centre at the next */
    std::vector<TownRow> oldMade(1); oldMade[0].owner = 2; oldMade[0].pos[0] = 4000.0f; oldMade[0].keys.push_back("m1");
    std::vector<TownRow> saidM(1); saidM[0].owner = 2; saidM[0].name = "Founded"; saidM[0].hand[3] = 71; saidM[0].pos[0] = 4010.0f; saidM[0].keys.push_back("f1");
    std::vector<zonekeep::KeptPiece> pm;
    p.ownerSlot = 2; p.key = "m1"; p.pos[0] = 4000.0f; pm.push_back(p);
    p.key = "f1"; p.pos[0] = 4020.0f; pm.push_back(p);
    KeptTownStats ts; CarryStats cs;
    out.clear();
    KeptTownsOf(saidM, pm, &out, &ts);
    CarryKeptKeys(oldMade, pm, &out, &cs);
    if (out.size() != 1 || out[0].name != "Founded" || cs.joined != 1 || out[0].keys.size() != 2 || out[0].keys[0] != "f1" || out[0].keys[1] != "m1")
        Fail("outposts fold: an outpost made at the last reset did not join the said outpost at its centre");
}
void t_t36g_state_broken()
{
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildState; m.key = "1459-gamedata.base@52,52@949500,953000,30770"; m.progress = 1.0f; m.needed = 1.0f;
    m.complete = 1; m.broken = 1;
    std::vector<char> b;
    coopbuild::BuildMsg back;
    if (!coopbuild::EncodeBuild(&b, m) || coopbuild::DecodeBuild(&b[0], b.size(), &back) != coopbuild::kBuildDecodeOk || back.broken != 1 || back.complete != 1 || back.destroyed != 0)
    { Fail("STATE: broken does not round-trip"); return; }
    const size_t cbAt = 1 + 1 + m.key.size() + 4 + 4;
    if ((unsigned char)b[cbAt] != (unsigned char)(1u | coopbuild::kBuildStateBrokenBit)) Fail("STATE: broken is not bit 2 of the complete byte");
    m.broken = 0; m.destroyed = 1; b.clear();
    if (!coopbuild::EncodeBuild(&b, m) || coopbuild::DecodeBuild(&b[0], b.size(), &back) != coopbuild::kBuildDecodeOk || back.broken != 0 || back.destroyed != 1)
        Fail("STATE: a ruin that is not broken reads broken");
    std::vector<char> odd = b; odd[cbAt] = (char)(1u | 8u);
    if (coopbuild::DecodeBuild(&odd[0], odd.size(), &back) == coopbuild::kBuildDecodeOk) Fail("STATE: a complete byte with bit 3 is read");
    m.broken = 2; b.clear();
    if (coopbuild::EncodeBuild(&b, m)) Fail("STATE: a broken value of 2 is sent");
}
/* T-36 gaps 2: A KEPT PIECE'S OWN STATE AND ITS ITEMS' MARKS (kept list format 3, the KEPT row of world-server protocol 95): read from
   a state record (name, layout names, pause, power, battery, production bar, input stocks, farm, own lock, doors' hardness and broken),
   items' uniform and crafter, packs at any depth to the bound; the list text both ways (format 3, and lists of format 1 and 2 still
   read); the wire both ways and its bounds; the farm ages; the parts a row wants laid; the lock level clamp */
zonekeep::Container T36g3Area()
{
    zonekeep::Container c = T36gArea();
    zonekeep::Rec& st = c.recs[1];
    st.strs["name"] = "My Loom"; st.strs["interior layout name"] = "lay in"; st.strs["exterior layout name"] = "lay out";
    st.bools["pause"] = 1; st.bools["gets battery"] = 1; st.bools["power on"] = 0; st.floats["batterycharge"] = 0.625f;
    st.floats["production amount"] = 0.375f; st.floats["consumption amount-0"] = 2.5f; st.floats["consumption amount-1"] = 0.25f;
    st.floats["consumption amount-17"] = 9.0f;
    st.floats["plant growth"] = 0.4f; st.floats["plant death"] = 0.125f; st.floats["plant start"] = 0.0625f; st.ints["plants harvested"] = 3;
    st.floats["save time"] = 1234.5f;
    st.ints["lock lvl"] = 55; st.floats["hardness"] = 1.5f; st.bools["locked"] = 1;
    st.floats["hardness1"] = 2.5f; st.bools["broken1"] = 1;
    for (size_t i = 0; i < c.recs.size(); ++i)
        if (c.recs[i].sid == "n1") { c.recs[i].strs["uniform"] = "1088-gamedata.base"; c.recs[i].strs["crafter"] = "Beep"; c.recs[i].insts.push_back(T36gInst("d3inv"));
                                    c.recs[i].ints["ownedbyTYPE"] = 1; c.recs[i].ints["ownedbyC"] = 7; c.recs[i].ints["ownedbyCS"] = 3; c.recs[i].ints["ownedbyI"] = 42; c.recs[i].ints["ownedbyS"] = -9; }
    /* n1 (depth 2) holds d3, d3 holds d4 (depth 4, the bound), d4 holds d5 (kept beside d4, in d3's list) */
    const char* chainSid[3] = { "d3", "d4", "d5" };
    for (int k = 0; k < 3; ++k)
    {
        zonekeep::Rec inv = T36gRec(zonekeep::kTypeInventory, std::string(chainSid[k]) + "inv"); inv.insts.push_back(T36gInst(chainSid[k]));
        c.recs.push_back(inv);
        zonekeep::Rec it = T36gItemRec(chainSid[k], std::string(chainSid[k]) + "-deep.mod", 1);
        if (k < 2) it.insts.push_back(T36gInst(std::string(chainSid[k + 1]) + "inv"));
        c.recs.push_back(it);
    }
    return c;
}
void t_t36g3_own_state()
{
    std::vector<zonekeep::KeptPiece> ps; zonekeep::KeepStats st; std::string why;
    zonekeep::ExtractKeptPieces(T36g3Area(), 0, &ps, &st, &why);
    if (ps.size() != 1) { Fail("own: the area gave no piece"); return; }
    const zonekeep::KeptPiece& p = ps[0];
    if (p.has != zonekeep::kHasKnown) Fail("own: has is " + I(p.has) + ", expected every part (" + I(zonekeep::kHasKnown) + ")");
    if (p.name != "My Loom" || p.interiorName != "lay in" || p.exteriorName != "lay out") Fail("own: the names are not kept");
    if (p.pause != 1 || p.getsBattery != 1 || p.powerOn != 0 || p.battery != 0.625f || p.production != 0.375f) Fail("own: pause / power / battery / bar not kept");
    if (p.inputs.size() != 2 || p.inputs[0].index != 0 || p.inputs[0].amount != 2.5f || p.inputs[1].index != 1 || p.inputs[1].amount != 0.25f || st.inputsLeftOut != 1)
        Fail("own: the input stocks are not 0:2.5 and 1:0.25 with stock 17 left out");
    if (p.growth != 0.4f || p.death != 0.125f || p.start != 0.0625f || p.harvested != 3) Fail("own: the farm is not kept");
    if (p.lockLevel != 55 || p.lockHardness != 1.5f || p.lockLocked != 1) Fail("own: the piece's own lock is not kept");
    if (p.doors.size() != 2 || p.doors[0].hardness != -1.0f || p.doors[0].broken != 0 || p.doors[1].hardness != 2.5f || p.doors[1].broken != 1) Fail("own: the doors' hardness / broken are not kept");
    const zonekeep::KeptItem& n1 = p.items[1].inner[1].inner[0];
    if (n1.uniform != "1088-gamedata.base" || n1.crafter != "Beep" || !p.items[0].uniform.empty() || !p.items[0].crafter.empty()) Fail("own: an item's uniform / crafter is not kept");
    if (n1.hasOwner != 1 || n1.owner[0] != 1 || n1.owner[1] != 7 || n1.owner[2] != 3 || n1.owner[3] != 42 || n1.owner[4] != -9 || p.items[0].hasOwner != 0)
        Fail("own: the owner ticket is not kept raw (TYPE, C, CS, I, S) or one is kept where the record holds none");
    if (n1.inner.size() != 1 || n1.inner[0].tmpl != "d3-deep.mod" || n1.inner[0].inner.size() != 2 || n1.inner[0].inner[0].tmpl != "d4-deep.mod" || !n1.inner[0].inner[0].inner.empty()
        || n1.inner[0].inner[1].tmpl != "d5-deep.mod" || !n1.inner[0].inner[1].inner.empty())
        Fail("own: the pack chain is not kept down to the depth bound, with the item below it kept beside the bound's item");
    if (st.bagItems != 4 || st.innerItems != 5 || st.nestedBags != 1 || st.flattened != 1 || st.leftOut != 0)
        Fail("own: counted " + I(st.bagItems) + " pack(s), " + I(st.innerItems) + " inner, " + I(st.nestedBags) + " past the bound, " + I(st.leftOut) + " left out (expected 4, 5, 1, 0)");
    /* a name the row cannot carry is left out, the rest kept */
    zonekeep::Container c2 = T36g3Area(); c2.recs[1].strs["name"] = std::string("bad\x01name");
    std::vector<zonekeep::KeptPiece> p2; zonekeep::KeepStats s2;
    zonekeep::ExtractKeptPieces(c2, 0, &p2, &s2, &why);
    if (p2.size() != 1 || (p2[0].has & zonekeep::kHasName) != 0 || !p2[0].name.empty() || s2.leftOut != 1 || p2[0].pause != 1) Fail("own: a name with a control character is not left out alone");
    /* format 3 text both ways */
    const std::string text = zonekeep::EncodeKept(ps);
    if (text.compare(0, 14, "kept-pieces\t3\n") != 0 || text.find("\nS\t127\tMy Loom\tlay in\tlay out\t1\t1\t0\t") == std::string::npos || text.find("\nF\t") == std::string::npos
        || text.find("\nC\t1\t") == std::string::npos || text.find("\nJ\t4\td4-deep.mod\t") == std::string::npos || text.find("\t1088-gamedata.base\tBeep\t1\t1\t7\t3\t42\t-9\n") == std::string::npos)
        Fail("own: the format 3 text lacks its header, S, F, C, depth-4 J line or the uniform, crafter and owner ticket");
    std::vector<zonekeep::KeptPiece> back;
    if (!zonekeep::DecodeKept(text, &back, &why) || zonekeep::EncodeKept(back) != text) { Fail("own: the format 3 text does not round-trip: " + why); return; }
    if (back[0].inputs.size() != 2 || back[0].harvested != 3 || back[0].doors[1].hardness != 2.5f || back[0].items[1].inner[1].inner[0].crafter != "Beep"
        || back[0].items[1].inner[1].inner[0].owner[4] != -9) Fail("own: a field changed on the round trip");
    /* formats 2 and 1 are still read: nothing of the own state, packs one level (format 2) or none (format 1) */
    const std::string t2 = zonekeep::EncodeKeptAs(ps, 2);
    if (!zonekeep::DecodeKept(t2, &back, &why) || back.size() != 1 || back[0].has != 0 || back[0].items[1].inner.size() != 2 || !back[0].items[1].inner[1].inner.empty()
        || back[0].doors.size() != 2 || back[0].doors[1].hardness != -1.0f || back[0].items[1].inner[1].hasOwner != 0 || zonekeep::EncodeKeptAs(back, 2) != t2)
        Fail("own: a format 2 list is not read as format 2 wrote it: " + why);
    const std::string t1 = zonekeep::EncodeKeptAs(ps, 1);
    if (!zonekeep::DecodeKept(t1, &back, &why) || back.size() != 1 || !back[0].items[1].inner.empty() || !back[0].doors.empty()) Fail("own: a format 1 list is not read: " + why);
    /* refused: a second S line, an F line the S line does not name, a stock index of 16, a J line past the bound or under no item line
       of the depth above it, a D line of 8 fields in format 3 */
    const size_t sAt = text.find("\nS\t") + 1;
    const std::string sLine = text.substr(sAt, text.find('\n', sAt) - sAt + 1);
    const std::string pLine = text.substr(14, text.find('\n', 14) - 14 + 1);
    std::string noFarm = sLine; noFarm.replace(2, 3, "95");
    const size_t iAt = text.find("\nI\t") + 1;
    const std::string iLine = text.substr(iAt, text.find('\n', iAt) - iAt + 1);
    const std::string jDeep = "J\t5\tx-deep.mod\t\t\t\t\t00000000\t00000000\t1\t0\t0\t0\t0\t0\t\t\t0\t11\t0\t0\t0\t0\n";
    const std::string jSkip = "J\t2\tx-deep.mod\t\t\t\t\t00000000\t00000000\t1\t0\t0\t0\t0\t0\t\t\t0\t11\t0\t0\t0\t0\n";
    const std::string refused[6] = { "kept-pieces\t3\n" + pLine + sLine + sLine, "kept-pieces\t3\n" + pLine + noFarm + "F\t00000000\t00000000\t00000000\t0\n",
                                     "kept-pieces\t3\n" + pLine + sLine + "C\t16\t00000000\n", "kept-pieces\t3\n" + pLine + sLine + iLine + jDeep,
                                     "kept-pieces\t3\n" + pLine + sLine + iLine + jSkip, "kept-pieces\t3\n" + pLine + sLine + "D\t0\t1\t0\t0\t29\t-1\t0\n" };
    for (int i = 0; i < 6; ++i)
        if (zonekeep::DecodeKept(refused[i], &back, &why) || !back.empty()) Fail("own: refused text " + I(i) + " is read");
    const std::string jOk = "J\t1\tx-deep.mod\t\t\t\t\t00000000\t00000000\t1\t0\t0\t0\t0\t0\t\t\t0\t11\t0\t0\t0\t0\n";
    if (!zonekeep::DecodeKept("kept-pieces\t3\n" + pLine + sLine + iLine + jOk, &back, &why)) Fail("own: a depth-1 J line after an I line is refused: " + why);
    /* the wire */
    keptwire::Row w = T36b2Row(5); w.p = ps[0];
    std::vector<keptwire::Row> rows(1, w);
    std::vector<char> b; keptwire::Msg m;
    if (!keptwire::EncodeRows(&b, 52, 52, rows) || keptwire::Decode(&b[0], b.size(), &m) != 1 || m.rows.size() != 1) { Fail("own: a row with its own state does not round-trip"); return; }
    if (T36b2Pieces(m.rows) != T36b2Pieces(rows)) Fail("own: the row's own state, pack chain or marks come back changed");
    if (b.size() != 16 + keptwire::RowWireSize(w)) Fail("own: RowWireSize is not the bytes a row with its own state takes");
    for (size_t cut = 0; cut < b.size(); cut += 5) if (keptwire::Decode(&b[0], cut, &m) != 0) { Fail("own: a ROWS cut to " + I((int)cut) + " bytes is read"); break; }
    keptwire::Row bad = w; bad.p.has = 128;
    if (keptwire::RowOk(bad)) Fail("own: an unknown has bit is valid");
    bad = w; bad.p.pause = 2;
    if (keptwire::RowOk(bad)) Fail("own: a pause of 2 is valid");
    bad = w; bad.p.inputs[1].index = zonekeep::kMaxInputs;
    if (keptwire::RowOk(bad)) Fail("own: an input index of 16 is valid");
    bad = w; bad.p.inputs.resize(keptwire::kMaxInputs + 1, w.p.inputs[0]);
    if (keptwire::RowOk(bad)) Fail("own: 17 input stocks are valid");
    bad = w; bad.p.items[1].inner[1].inner[0].crafter = std::string("a\tb");
    if (keptwire::RowOk(bad)) Fail("own: a crafter with a TAB is valid");
    bad = w; bad.p.items[1].inner[1].inner[0].hasOwner = 2;
    if (keptwire::RowOk(bad)) Fail("own: an owner ticket kept flag of 2 is valid");
    std::string shortJ = "kept-pieces\t3\n" + pLine + sLine + iLine + jOk;
    shortJ.erase(shortJ.size() - 3, 2);   /* the J line one owner word short */
    if (zonekeep::DecodeKept(shortJ, &back, &why) || !back.empty()) Fail("own: a J line one owner word short is read");
    bad = w; bad.p.doors[1].broken = 2;
    if (keptwire::RowOk(bad)) Fail("own: a door lock broken of 2 is valid");
    bad = w; bad.p.items[1].inner[1].inner[0].inner[0].inner[0].inner.push_back(w.p.items[0]);
    if (keptwire::RowOk(bad)) Fail("own: an item past the depth bound is valid");
    /* the farm ages: the harvested hidden at -10, the lowest of the others the growth, none above it by more than the spread */
    float ages[8];
    keptwire::KeptFarmAges(0.4f, 2, 6, ages);
    if (ages[0] != keptwire::kFarmHarvestedAge || ages[1] != keptwire::kFarmHarvestedAge || ages[2] != 0.4f || !(ages[5] > 0.4f && ages[5] <= 0.4f + keptwire::kFarmAgeSpread + 1.0e-6f)
        || !(ages[3] >= ages[2] && ages[4] >= ages[3] && ages[5] >= ages[4]))
        Fail("ages: not -10 for the harvested and growth upward for the rest");
    keptwire::KeptFarmAges(0.75f, 9, 3, ages);
    if (ages[0] != -10.0f || ages[2] != -10.0f) Fail("ages: more harvested than plants is not every plant hidden");
    keptwire::KeptFarmAges(0.75f, -1, 1, ages);
    if (ages[0] != 0.75f) Fail("ages: one plant is not at the growth");
    /* the parts a row wants laid */
    if (keptwire::ExtrasWanted(p) != 511u) Fail("wanted: a piece with every part does not want all nine (" + I((int)keptwire::ExtrasWanted(p)) + ")");
    zonekeep::KeptPiece ruin = p; ruin.destroyed = 1;
    if ((keptwire::ExtrasWanted(ruin) & keptwire::kXDoors) != 0) Fail("wanted: a ruin wants its doors laid");
    if (keptwire::ExtrasWanted(zonekeep::KeptPiece()) != 0u) Fail("wanted: an empty piece wants a part");
    if (keptwire::LockLevelClamp(-5) != 0 || keptwire::LockLevelClamp(150) != 100 || keptwire::LockLevelClamp(40) != 40) Fail("clamp: the lock level is not held to 0 .. 100");
}
void t_t36_merge_logs()
{
    keptwire::Row a = T36b2Row(0);                         /* the chest, slot 2, with its items */
    std::vector<zonekeep::KeptPiece> fresh(1, a.p);         /* the chest read from the records, with its own items */
    std::vector<zonekeep::KeptPiece> old(4, a.p);
    old[3].ownerSlot = 5;                                   /* 3: another player's chest at that spot - not among the pieces read */
    std::vector<zonekeep::KeptState> st(4);
    st[0].itemsLaidNonWriter = 1;                           /* 0: matched, contents laid never confirmed - not carried, may be lost */
    st[1].itemsDone = 1;                                    /* 1: matched, contents done - not carried */
    st[3].itemsDone = 1; st[3].ownerTook = 1;               /* 3: carried, contents done, the piece laid again - the owner part opens */
    std::vector<zonekeep::KeptState> fs; zonekeep::MergeStats ms;
    zonekeep::MergeUndone(old, st, &fresh, &fs, &ms);
    if (ms.matchedDropped != 3 || ms.matchedDropLaid != 2 || ms.itemsLaidUnconfirmed != 1 || ms.matchedItemsKept != 1 || ms.appended != 1
        || ms.ownerRetake != 1 || ms.itemsDoneLeft != 1 || ms.itemsMoved != 0 || fresh.size() != 2 || fs.size() != 2)
        Fail("merge logs: matched " + I(ms.matchedDropped) + ", matched dropping laid contents " + I(ms.matchedDropLaid) + ", laid unconfirmed " + I(ms.itemsLaidUnconfirmed)
             + ", matched not moved " + I(ms.matchedItemsKept) + ", appended " + I(ms.appended) + ", owner again " + I(ms.ownerRetake) + ", contents done " + I(ms.itemsDoneLeft)
             + ", moved " + I(ms.itemsMoved) + ", list " + I((int)fresh.size()) + " (expected 3, 2, 1, 1, 1, 1, 1, 0, 2)");
    else if (fs[1].ownerTook != 0 || fs[1].itemsDone != 1 || !fresh[1].items.empty() || fresh[1].ownerSlot != 5) Fail("merge logs: the carried row is not its piece alone with its owner part open");
}
void t_t36g_prod_bar_none()
{
    if (keptwire::ProdBarNothingToLay(0, 0.0f) != 1) Fail("bar: no bar here and a kept 0 is not done");
    if (keptwire::ProdBarNothingToLay(0, 0.5f) != 0) Fail("bar: no bar here and a kept 0.5 does not wait");
    if (keptwire::ProdBarNothingToLay(1, 0.0f) != 0) Fail("bar: a bar here is not laid the normal way");
    if (keptwire::ExtrasFinished(0x1d, 0x1d, 0) != 1) Fail("bar: every wanted part done does not finish the row");
}
/* a nested move whose tagged other half sits in a box another game writes (or on a character) is put back whole, never pushed alone */
void t_bagnest_push_partner()
{
    if (coopbag::NestMoveRoad(1, 1, 1, 1, 0, 0, 0) != coopbag::kNestRoadUndo || coopbag::NestMoveRoad(1, 1, 0, 1, 0, 0, 0) != coopbag::kNestRoadUndo
        || coopbag::NestMoveRoad(1, 1, 0, 1, 1, 0, 0) != coopbag::kNestRoadUndo || coopbag::NestMoveRoad(1, 1, 1, 1, 0, 1, 0) != coopbag::kNestRoadUndo)
        Fail("nested move: the box's writer pushed one half while the other half's box is written elsewhere");
    if (coopbag::NestMoveRoad(1, 1, 1, 1, 0, 0, 1) != coopbag::kNestRoadPush || coopbag::NestMoveRoad(1, 1, 0, 1, 1, 0, 1) != coopbag::kNestRoadPush)
        Fail("nested move: both halves in boxes this game writes were not pushed");
    if (coopbag::NestMoveRoad(1, 1, 1, 0, 0, 1, 0) != coopbag::kNestRoadPush || coopbag::NestMoveRoad(1, 1, 2, 1, 0, 0, 0) != coopbag::kNestRoadPush
        || coopbag::NestMoveRoad(1, 1, 3, 1, 0, 0, 0) != coopbag::kNestRoadPush)
        Fail("nested move: a lone half, a count change or a split in a box this game writes was not pushed");
    if (coopbag::NestMoveRoad(1, 1, 1, 1, 0, 0) != coopbag::kNestRoadPush) Fail("nested move: the six-input form no longer pushes");
    if (coopbag::NestMoveRoad(0, 1, 1, 1, 0, 0, 0) != coopbag::kNestRoadUndo || coopbag::NestMoveRoad(1, 0, 1, 1, 0, 0, 1) != coopbag::kNestRoadUndo)
        Fail("nested move: a game that does not write the box did not put both halves back");
}

}   /* namespace suite */
