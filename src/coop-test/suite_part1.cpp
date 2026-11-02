/* src/coop-test/suite_part1.cpp - THE OFFLINE SUITE, part 1 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"

namespace suite {

const char* WlRungName(int r)
{
    switch (r)
    {
        case kRungNone:              return "kRungNone";
        case kRungNotLoadedHere:     return "kRungNotLoadedHere";
        case kRungSoleLoaded:        return "kRungSoleLoaded";
        case kRungTieLower:          return "kRungTieLower";
        case kRungTieHigher:         return "kRungTieHigher";
        case kRungRefusedNoNotebook: return "kRungRefusedNoNotebook";
        case kRungNoAnswer:          return "kRungNoAnswer";
        default:                     return "??";
    }
}
const char* WlVerdictName(int v)
{
    switch (v)
    {
        case kWrMine:   return "kWrMine";
        case kWrHeld:   return "kWrHeld";
        case kWrRefuse: return "kWrRefuse";
        case kWrNoAnswer: return "kWrNoAnswer";
        default:        return "??";
    }
}
/* every failure prints the six inputs it read, not only the answer it computed (6a lesson 12) */
std::string WlIn(int lh, int peer, int low, int known, long long down, long long grace, int forBox)
{
    return std::string("loadedHereLive=") + I(lh) + " peerSectorLoaded=" + I(peer) + " mySlotLower=" + I(low)
         + " slotDecided=" + I(known) + " notebookDownMs=" + D((double)down) + " refuseAfterMs=" + D((double)grace)
         + " forBox=" + I(forBox);
}
void WlCheck(int lh, int peer, int low, int known, long long down, long long grace, int forBox,
             int wantV, int wantRung)
{
    int rung = -1, spans = -1;
    const int v = WriterLadder(lh, peer, low, known, down, grace, forBox, &rung, &spans);
    if (v != wantV)
        Fail(std::string("verdict ") + WlVerdictName(v) + " wanted " + WlVerdictName(wantV) + " for "
             + WlIn(lh, peer, low, known, down, grace, forBox));
    if (wantRung >= 0 && rung != wantRung)
        Fail(std::string("rung ") + WlRungName(rung) + " wanted " + WlRungName(wantRung) + " for "
             + WlIn(lh, peer, low, known, down, grace, forBox));
    if (rung == kRungNone)
        Fail(std::string("no rung was reported for ") + WlIn(lh, peer, low, known, down, grace, forBox));
}

/* 1. R1 IS UNCONDITIONAL. Not loaded here -> HELD, for every combination of the other inputs. */
void t_wl_r1_refuses_when_not_loaded_here()
{
    int peer, low, known, forBox;
    for (peer = -1; peer <= 1; ++peer)
        for (low = 0; low <= 1; ++low)
            for (known = 0; known <= 1; ++known)
                for (forBox = 0; forBox <= 2; ++forBox)
                {
                    WlCheck(0, peer, low, known, 0,     60000, forBox, kWrHeld, kRungNotLoadedHere);
                    WlCheck(0, peer, low, known, 59999, 60000, forBox, kWrHeld, kRungNotLoadedHere);
                }
}

/* 2. R2. The other game reported its set and this sector was NOT in it -> this game is the only one that
      has the box, so it writes. Independent of the slot ordering, which is the point. */
void t_wl_r2_sole_loaded_writes()
{
    int low, known, forBox;
    for (low = 0; low <= 1; ++low)
        for (known = 0; known <= 1; ++known)
            for (forBox = 0; forBox <= 2; ++forBox)
                WlCheck(1, 0, low, known, 0, 60000, forBox, kWrMine, kRungSoleLoaded);
}

/* 3. R3. Both games have it -> the lower notebook slot writes and the higher declines. */
void t_wl_r3_tie_break_by_slot()
{
    WlCheck(1, 1, 1, 1, 0, 60000, 1, kWrMine, kRungTieLower);
    WlCheck(1, 1, 0, 1, 0, 60000, 1, kWrHeld, kRungTieHigher);
    WlCheck(1, 1, 1, 1, 0, 60000, 0, kWrMine, kRungTieLower);   /* a restock with a real peer answer still presumes */
    WlCheck(1, 1, 0, 1, 0, 60000, 0, kWrHeld, kRungTieHigher);
}

/* 4. THE TEST THAT WOULD HAVE CAUGHT THE `otherLoaded` MISTAKE (design 2.3). -1 is "no report", and it must
      route to the tie-break and NEVER to kRungSoleLoaded. A rung built on AreaViewTS's `otherLoaded` reads
      -1 in the exact state this ladder serves, so an implementation that folded -1 into 0 would answer
      "I am the only one that has it" for every container in the world. */
void t_wl_peer_unknown_is_never_sole_loaded()
{
    int low, known;
    for (low = 0; low <= 1; ++low)
        for (known = 0; known <= 1; ++known)
        {
            int rung = -1, spans = 0;
            const int v = WriterLadder(1, -1, low, known, 0, 60000, 1, &rung, &spans);
            if (rung == kRungSoleLoaded)
                Fail(std::string("an unknown peer answer was read as SOLE LOADED for ")
                     + WlIn(1, -1, low, known, 0, 60000, 1));
            /* M2 (decisions 32/44/54): with no notebook slot comparison the tie-break has no answer */
            if (rung != (known ? (low ? kRungTieLower : kRungTieHigher) : kRungNoAnswer))
                Fail(std::string("an unknown peer answer did not route to the tie-break: rung ")
                     + WlRungName(rung) + " for " + WlIn(1, -1, low, known, 0, 60000, 1));
            if ((spans & kSpanPeerUnknown) == 0)
                Fail(std::string("the r2PeerUnknown span was not raised for ")
                     + WlIn(1, -1, low, known, 0, 60000, 1));
            if (v != (known ? (low ? kWrMine : kWrHeld) : kWrNoAnswer))
                Fail(std::string("verdict ") + WlVerdictName(v) + " for " + WlIn(1, -1, low, known, 0, 60000, 1));
        }
    /* and a RESTOCK declines there, because a presumed restock wipes a shop (P7a's narrowing, generalised) */
    WlCheck(1, -1, 1, 1, 0, 60000, 0, kWrHeld, kRungTieHigher);
    WlCheck(1, -1, 0, 1, 0, 60000, 0, kWrHeld, kRungTieHigher);
}

/* 5. THE SINGLE-WRITER GUARANTEE, AS A CHECKED PROPERTY RATHER THAN AN ARGUMENT. This is what C7 existed
      for. For every pair of consistent two-game views - A's loadedHere is B's peerSectorLoaded and vice
      versa, and exactly one of them has the lower slot - EXACTLY ONE of the two answers is kWrMine, or
      NEITHER is (both may legitimately decline when neither has the box loaded). Two MINEs is the defect
      F598 measured and it is impossible here. */
void t_wl_exactly_one_writer_over_consistent_views()
{
    /* B9-b (review-b9 L-9). THE SWEEP IS WIDENED IN THREE DIRECTIONS, because the narrow version could not
       see the states the ladder is actually entered in:
         - EACH GAME'S REPORT OF THE OTHER MAY BE UNKNOWN (-1), independently. That is the ordinary case
           during a notebook outage, and it is where the tie-break does all the work;
         - ALL THREE FAMILIES: restock (0), box (1) and DOOR (2). A door takes the box's road and this is
           what says so; a restock's narrowing must not break single-writer either;
         - THE ROLE-DECIDED TIE (slotDecided = 0) with the two games holding OPPOSITE roles, which is what
           exception A1 actually looks like on two machines.
       `bPeer`/`aPeer` are what each game reports ABOUT THE OTHER, so A's input is B's loaded state (or -1
       when A never heard); the two games are never given contradictory facts. */
    int aLoaded, bLoaded, aLower, aPeerKnown, bPeerKnown, decided, fam;
    const int fams[3] = { 0, 1, 2 };   /* restock, box, door */
    for (fam = 0; fam < 3; ++fam)
    for (aLoaded = 0; aLoaded <= 1; ++aLoaded)
        for (bLoaded = 0; bLoaded <= 1; ++bLoaded)
            for (aLower = 0; aLower <= 1; ++aLower)
                for (aPeerKnown = 0; aPeerKnown <= 1; ++aPeerKnown)
                for (bPeerKnown = 0; bPeerKnown <= 1; ++bPeerKnown)
                for (decided = 0; decided <= 1; ++decided)
            {
                /* what A was told about B, and what B was told about A. -1 = never heard. */
                const int aSeesB = aPeerKnown ? bLoaded : -1;
                const int bSeesA = bPeerKnown ? aLoaded : -1;
                int ra = 0, rb = 0;
                const int va = WriterLadder(aLoaded, aSeesB, aLower,      decided, 0, 60000, fams[fam], &ra, 0);
                const int vb = WriterLadder(bLoaded, bSeesA, 1 - aLower,  decided, 0, 60000, fams[fam], &rb, 0);
                const int mines = (va == kWrMine ? 1 : 0) + (vb == kWrMine ? 1 : 0);
                const std::string where = std::string(" forBox=") + I(fams[fam]) + " aLoaded=" + I(aLoaded)
                         + " bLoaded=" + I(bLoaded) + " aLower=" + I(aLower) + " aSeesB=" + I(aSeesB)
                         + " bSeesA=" + I(bSeesA) + " slotDecided=" + I(decided)
                         + ": A=" + WlVerdictName(va) + "/" + WlRungName(ra)
                         + " B=" + WlVerdictName(vb) + "/" + WlRungName(rb);
                if (mines > 1) Fail(std::string("TWO WRITERS for") + where);
                /* THE MUST-WRITE HALF IS ASSERTED WHERE THE INPUTS SUPPORT IT: both games heard the
                   other's report. With a report MISSING, declining is legitimate and is the safe
                   direction - the game holding the higher slot with no peer answer declines even when it
                   is in fact the only one with the box loaded, and the move is refused and can be made
                   again. Asserting must-write there would assert the unsafe behaviour. */
                {
                    const int bothHeard = (aSeesB >= 0 && bSeesA >= 0) ? 1 : 0;
                    /* M2: and only where a notebook slot comparison decided - without one nobody is named */
                    if ((aLoaded == 1 || bLoaded == 1) && bothHeard != 0 && decided != 0 && mines != 1)
                        Fail(std::string("NO WRITER for") + where);
                }
            }
    /* M2 (decisions 32/44/54): THE ROLE-DECIDED TIE IS GONE. Two games with no notebook slot comparison and no
       report of each other - A host, B client, as exception A1 used to look on two machines - now BOTH answer
       NO-ANSWER: nobody writes, and certainly not two. */
    {
        int f;
        for (f = 0; f < 3; ++f)
        {
            int ra = 0, rb = 0, sa = 0, sb = 0;
            const int va = WriterLadder(1, -1, 1, 0, 0, 60000, fams[f], &ra, &sa);
            const int vb = WriterLadder(1, -1, 0, 0, 0, 60000, fams[f], &rb, &sb);
            if (va != kWrNoAnswer || vb != kWrNoAnswer)
                Fail(std::string("a tie with no notebook slot comparison was decided for forBox=") + I(fams[f])
                     + ": A=" + WlVerdictName(va) + " B=" + WlVerdictName(vb));
            if ((sa & kSpanPeerUnknown) == 0 || (sb & kSpanPeerUnknown) == 0)
                Fail(std::string("the r2PeerUnknown span was not raised on BOTH games for forBox=") + I(fams[f]));
        }
    }
}

/* 6. R4 IS FIRST AND IT REFUSES FROM EVERY INPUT COMBINATION, for a box and for a restock alike (a presumed
      restock wipes a shop, so it refuses too). And the grace boundary is exclusive: exactly at the grace it
      does NOT refuse, one millisecond past it does. */
void t_wl_r4_refuses_past_the_grace_from_every_input()
{
    int lh, peer, low, known, forBox;
    for (lh = 0; lh <= 1; ++lh)
        for (peer = -1; peer <= 1; ++peer)
            for (low = 0; low <= 1; ++low)
                for (known = 0; known <= 1; ++known)
                    for (forBox = 0; forBox <= 2; ++forBox)
                        WlCheck(lh, peer, low, known, 60001, 60000, forBox, kWrRefuse, kRungRefusedNoNotebook);
    /* the boundary, both sides of it */
    WlCheck(1, 0, 1, 1, 60000, 60000, 1, kWrMine,   kRungSoleLoaded);
    WlCheck(1, 0, 1, 1, 60001, 60000, 1, kWrRefuse, kRungRefusedNoNotebook);
    /* a negative "down" is NOT an outage - it is "not known", and reading it as one would refuse the world */
    WlCheck(1, 0, 1, 1, -1, 60000, 1, kWrMine, kRungSoleLoaded);
    /* inside the grace, the span says the notebook was down and the answer still comes from the ladder */
    {
        int rung = 0, spans = 0;
        const int v = WriterLadder(1, 0, 1, 1, 30000, 60000, 1, &rung, &spans);
        if (v != kWrMine || rung != kRungSoleLoaded) Fail("inside the grace the ladder must still answer");
        if ((spans & kSpanGracePass) == 0) Fail("the r4GracePasses span was not raised inside the grace");
        /* and it must NOT be raised with the notebook up - a span that is always on says nothing */
        rung = 0; spans = 0;
        WriterLadder(1, 0, 1, 1, 0, 60000, 1, &rung, &spans);
        if ((spans & kSpanGracePass) != 0) Fail("the r4GracePasses span was raised with the notebook up");
    }
}

/* 7. M2 (decisions 32/44/54; audit B14) - WITH NO NOTEBOOK, THE LADDER NAMES NOBODY. slotDecided = 0 is "no
      notebook slot comparison" (no slot of my own, or no fresh notebook table). R3 used to let the SESSION ROLE
      decide there (exception A1) - FAILS BEFORE M2: the pre-M2 ladder returned kWrMine for mySlotLower = 1 (the
      host's role) and kWrHeld for 0, from rung kRungTieLower / kRungTieHigher. It must answer NO-ANSWER from
      every input that reaches R3, for every family, and the r3ByRole span still marks it. */
void t_wl_no_notebook_slot_answers_no_answer()
{
    int low, peer, forBox;
    for (peer = -1; peer <= 1; peer += 2)          /* -1 and 1: both reach R3 */
        for (low = 0; low <= 1; ++low)
            for (forBox = 0; forBox <= 2; ++forBox)
            {
                int rung = 0, spans = 0;
                WlCheck(1, peer, low, 0, 0, 60000, forBox, kWrNoAnswer, kRungNoAnswer);
                WriterLadder(1, peer, low, 0, 0, 60000, forBox, &rung, &spans);
                if ((spans & kSpanByRole) == 0)
                    Fail(std::string("the r3ByRole span was not raised for ") + WlIn(1, peer, low, 0, 0, 60000, forBox));
            }
    /* M2-b (review-m2 H1, decision 52): INSIDE THE GRACE, a tie with no slot comparison is REFUSED, so every
       caller takes its outage-journal path. FAILS BEFORE M2-b: the ladder answered NO-ANSWER here and the
       write was dropped. The role still never decides - every input, every family, both edges of the grace. */
    for (peer = -1; peer <= 1; peer += 2)
        for (low = 0; low <= 1; ++low)
            for (forBox = 0; forBox <= 2; ++forBox)
            {
                WlCheck(1, peer, low, 0, 1,     60000, forBox, kWrRefuse, kRungRefusedNoNotebook);
                WlCheck(1, peer, low, 0, 30000, 60000, forBox, kWrRefuse, kRungRefusedNoNotebook);
                WlCheck(1, peer, low, 0, 60000, 60000, forBox, kWrRefuse, kRungRefusedNoNotebook);
            }
    /* and with the notebook LINKED (down = 0) and no slot it is still NO-ANSWER (the sweep above; once more by name) */
    WlCheck(1, -1, 1, 0, 0, 60000, 1, kWrNoAnswer, kRungNoAnswer);
    /* and R1 / R2 / R4 still answer above it - NO-ANSWER is R3's alone */
    WlCheck(0, -1, 1, 0, 0, 60000, 1, kWrHeld, kRungNotLoadedHere);
    WlCheck(1, 0, 1, 0, 0, 60000, 1, kWrMine, kRungSoleLoaded);
    WlCheck(1, -1, 1, 0, 60001, 60000, 1, kWrRefuse, kRungRefusedNoNotebook);
    /* with a slot, the span must NOT be raised - a span that is always on says nothing */
    {
        int rung = 0, spans = 0;
        WriterLadder(1, 1, 1, 1, 0, 60000, 1, &rung, &spans);
        if ((spans & kSpanByRole) != 0) Fail("the r3ByRole span was raised with a known slot");
    }
}

/* 8. COVERAGE: every rung must be reachable, and the sweep must see all five. A rung that no input can
      produce is a silent zero waiting to happen (F172). */
void t_wl_every_rung_is_reachable()
{
    int seen[kRungCount];
    int i, lh, peer, low, known, forBox, down;
    const long long downs[3] = { 0, 30000, 60001 };
    for (i = 0; i < kRungCount; ++i) seen[i] = 0;
    for (lh = 0; lh <= 1; ++lh)
        for (peer = -1; peer <= 1; ++peer)
            for (low = 0; low <= 1; ++low)
                for (known = 0; known <= 1; ++known)
                    for (forBox = 0; forBox <= 2; ++forBox)
                        for (down = 0; down < 3; ++down)
                        {
                            int rung = 0;
                            WriterLadder(lh, peer, low, known, downs[down], 60000, forBox, &rung, 0);
                            if (rung <= 0 || rung >= kRungCount) { Fail(std::string("rung out of range: ") + I(rung)); return; }
                            seen[rung] = 1;
                        }
    for (i = 1; i < kRungCount; ++i)
        if (!seen[i]) Fail(std::string("rung never reached by the sweep: ") + WlRungName(i));
}

const char* KindName(int k)
{
    switch (k)
    {
        case kClockNothing:      return "kClockNothing";
        case kClockJump:         return "kClockJump";
        case kClockThrottle:     return "kClockThrottle";
        case kClockForwardSet:   return "kClockForwardSet";
        case kClockAheadNoWrite: return "kClockAheadNoWrite";
        case kClockOverBound:    return "kClockOverBound";
        case kClockJumpOverBound: return "kClockJumpOverBound";
        default:                 return "??";
    }
}
const char* PullName(int k)
{
    switch (k)
    {
        case kPullIgnore:        return "kPullIgnore";
        case kPullSeed:          return "kPullSeed";
        case kPullWindowAdvance: return "kPullWindowAdvance";
        case kPullPlayForward:   return "kPullPlayForward";
        case kPullOverBound:     return "kPullOverBound";
        case kPullRateLimited:   return "kPullRateLimited";
        default:                 return "??";
    }
}

/* ------------------------------------------------------------------------------------------------
   1. clock_decide_never_writes_below_current - THE POST-CONDITION, SWEPT.
   220 cases. For every one of them, with the join jump NOT owed: the decision is never a jump, and whenever it
   is a throttle or a forward set the target is at or above the value the clock holds right now. This is
   review-p7j H-1's property, proved rather than commented.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_never_writes_below_current()
{
    const ClockTuning t = ClockDefaultTuning();
    const double locals[4]  = { 0.0, 24.0, 2400.0, 24000.0 };
    const double deltas[11] = { -1e5, -24.0, -1.0, -0.02, -0.005, 0.0, 0.005, 0.02, 1.0, 24.0, 1e5 };
    int i, j, k, cases = 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 11; ++j)
            for (k = 0; k < 5; ++k)
            {
                const double localAbs = locals[i];
                const double sharedAbs = localAbs + deltas[j];
                double prev = -1.0;
                if (k == 1) prev = localAbs;
                else if (k == 2) prev = localAbs - 0.0001;
                else if (k == 3) prev = localAbs - 0.01;
                else if (k == 4) prev = localAbs + 0.01;
                {
                    const ClockAct a = ClockDecide(localAbs, sharedAbs, prev, 0, t);
                    ++cases;
                    if (a.kind == kClockJump)
                        Fail("a pass with joinJumpOwed=0 decided a JUMP - inputs localAbs=" + D(localAbs)
                             + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(prev)
                             + " expected any kind but kClockJump got kClockJump");
                    if ((a.kind == kClockThrottle || a.kind == kClockForwardSet) && !(a.writeAbs >= localAbs))
                        Fail(std::string("a ") + KindName(a.kind) + " produced a target BELOW the current clock - inputs localAbs="
                             + D(localAbs) + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(prev)
                             + " expected writeAbs >= " + D(localAbs) + " got " + D(a.writeAbs));
                }
            }
    if (cases != 220)
        Fail("the sweep did not cover the grid it claims - inputs 4 locals x 11 deltas x 5 prevs expected 220 got " + I(cases));
}

/* ------------------------------------------------------------------------------------------------
   2. clock_decide_engine_held_writes_nothing - review-p7j H-1's FAILING SCENARIO.
   The engine added nothing since the last pass (a dialogue window is open, or a load screen). The measured
   advance is 0, so the cap is 0, so nothing is written at all - where P7j's `elapsed x speed x +/-0.5` formula
   computed a delta from wall-clock time the engine never put on the clock, and in the AHEAD band that delta was
   NEGATIVE. Both expressions are stated here so the regression is legible.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_engine_held_writes_nothing()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 1000.0;

    /* Leg 1: BEHIND by 10 in-game minutes, engine held. */
    {
        const double sharedAbs = localAbs + 10.0 / 60.0;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, localAbs /* prev == local: no advance */, 0, t);
        if (a.kind != kClockNothing)
            Fail(std::string("the engine added nothing yet the pass decided to write - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(localAbs)
                 + " expected kClockNothing got " + KindName(a.kind));
        if (a.engineAdvanceHours != 0.0)
            Fail("the measured engine advance is not zero on a held engine - inputs localAbs=" + D(localAbs)
                 + " prevLocalAbs=" + D(localAbs) + " expected 0 got " + D(a.engineAdvanceHours));
    }

    /* Leg 2: AHEAD by 30 in-game minutes, engine held. P7j's formula, reproduced: with elapsed = 1.0 real
       second at speed 1 and kRealSecondsPerGameHour = 109.0909, ownAdvanceHours = 1/109.0909 and the ahead
       band took deltaHours = -0.5 * ownAdvanceHours - a NEGATIVE number handed to a writer that added it. */
    {
        const double sharedAbs = localAbs - 30.0 / 60.0;
        const double p7jOwnAdvanceHours = 1.0 * 1.0 / 109.0909;
        const double p7jDeltaHours = -0.5 * p7jOwnAdvanceHours;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, localAbs, 0, t);
        if (!(p7jDeltaHours < 0.0))
            Fail("the P7j formula reproduced here is not negative in the ahead band, so this test is not the regression it claims - inputs elapsed=1s speed=1 expected < 0 got " + D(p7jDeltaHours));
        if (a.kind != kClockAheadNoWrite)
            Fail(std::string("a game AHEAD of the shared clock did not refuse to write - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(localAbs)
                 + " expected kClockAheadNoWrite got " + KindName(a.kind));
        if (a.writeAbs != 0.0)
            Fail("kClockAheadNoWrite consumed a writeAbs - inputs localAbs=" + D(localAbs) + " sharedAbs=" + D(sharedAbs)
                 + " expected 0 (unset) got " + D(a.writeAbs));
    }
}

/* ------------------------------------------------------------------------------------------------
   3. clock_decide_join_jump_is_bounded_and_two_way - DECISION 46, AMENDMENT 46b's WIDTH (P8a/F637), AND
   AMENDMENT 46c (clock1, user 2026-09-26): the width binds only a BACKWARD jump; forward is taken at any size.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_join_jump_is_bounded_and_two_way()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 10000.0;
    const double fourWeeks = 28.0 * 24.0;
    /* [clock1 / amendment 46c, 2026-09-26: the refusal below is now BACKWARD-only; a forward jump of any size is taken - see the cases after this block.]
       P8a (F637 / review-p7y HIGH-1).  THE JUMP IS TWO-WAY WITHIN THE BOUND AND REFUSED OUTSIDE IT.
       Inside: six in-game days, both directions, still one jump straight to the shared clock.  Outside: four
       weeks, both directions, kClockJumpOverBound with writeAbs == localAbs so nothing can be written even by a
       caller that ignores the kind.  Before P8a the four-week case was a JUMP - which is how a game the notebook
       had just refused at seven days was dragged 299 in-game days backward one second later. */
    const double sixDays = 6.0 * 24.0;
    int d;
    for (d = 0; d < 2; ++d)
    {
        const double sharedAbs = (d == 0) ? (localAbs - sixDays) : (localAbs + sixDays);
        const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 1 /* joinJumpOwed */, t);
        if (a.kind != kClockJump)
            Fail(std::string("a join jump INSIDE the seven-day bound was not taken - inputs localAbs=") + D(localAbs) + " sharedAbs=" + D(sharedAbs)
                 + " joinJumpOwed=1 expected kClockJump got " + KindName(a.kind));
        if (a.writeAbs != sharedAbs)
            Fail("the join jump did not target the shared clock exactly - inputs localAbs=" + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " expected writeAbs=" + D(sharedAbs) + " got " + D(a.writeAbs));
    }
    /* clock1 (user 2026-09-26, AMENDMENT 46c).  FORWARD PAST THE BOUND IS A JUMP: this game is BEHIND the world
       - a new character, or any late joiner - and takes the world's clock at any distance.  Four weeks and 300
       in-game days, both straight to the shared clock. */
    {
        const double fwd[2] = { fourWeeks, 300.0 * 24.0 };
        for (d = 0; d < 2; ++d)
        {
            const double sharedAbs = localAbs + fwd[d];
            const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 1 /* joinJumpOwed */, t);
            if (a.kind != kClockJump)
                Fail(std::string("a FORWARD join jump past the seven-day bound was refused (amendment 46c) - inputs localAbs=") + D(localAbs)
                     + " sharedAbs=" + D(sharedAbs) + " joinJumpOwed=1 expected kClockJump got " + KindName(a.kind));
            if (a.writeAbs != sharedAbs)
                Fail("a FORWARD join jump past the bound did not target the shared clock exactly - inputs localAbs=" + D(localAbs)
                     + " sharedAbs=" + D(sharedAbs) + " expected writeAbs=" + D(sharedAbs) + " got " + D(a.writeAbs));
        }
    }
    /* BACKWARD PAST THE BOUND IS STILL REFUSED: this game is AHEAD of the world by more than seven in-game days,
       the same gap the notebook refuses as an offer.  Four weeks and 300 days back, writeAbs == localAbs. */
    {
        const double back[2] = { fourWeeks, 300.0 * 24.0 };
        for (d = 0; d < 2; ++d)
        {
            const double sharedAbs = localAbs - back[d];
            const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 1 /* joinJumpOwed */, t);
            if (a.kind != kClockJumpOverBound)
                Fail(std::string("a BACKWARD join jump OVER the seven-day bound was taken - inputs localAbs=") + D(localAbs) + " sharedAbs=" + D(sharedAbs)
                     + " joinJumpOwed=1 expected kClockJumpOverBound got " + KindName(a.kind));
            if (a.writeAbs != localAbs)
                Fail("a refused join jump did not leave this game's own clock alone - inputs localAbs=" + D(localAbs)
                     + " sharedAbs=" + D(sharedAbs) + " expected writeAbs=" + D(localAbs) + " got " + D(a.writeAbs));
        }
    }
    /* THE TWO BOUNDS ARE THE SAME WIDTH AND FACE THE SAME WAY: the notebook refuses an offer AHEAD of the world
       past seven in-game days and the joining game refuses the BACKWARD jump (itself ahead) past the same seven,
       so the pair can never disagree about one gap.  Asserted rather than described. */
    if (t.joinBoundMinutes != 7.0 * 24.0 * 60.0)
        Fail("the join bound is not seven in-game days - expected 10080 got " + D(t.joinBoundMinutes));
    {   /* BACKWARD: exactly ON the bound is still a jump; one minute past it is not */
        const double onBound = localAbs - t.joinBoundMinutes / 60.0;
        const ClockAct a = ClockDecide(localAbs, onBound, -1.0, 1, t);
        if (a.kind != kClockJump)
            Fail(std::string("a BACKWARD gap exactly ON the seven-day bound was refused - expected kClockJump got ") + KindName(a.kind));
        if (a.writeAbs != onBound)
            Fail("a BACKWARD jump ON the bound did not target the shared clock - expected " + D(onBound) + " got " + D(a.writeAbs));
    }
    {
        const double pastBound = localAbs - (t.joinBoundMinutes + 1.0) / 60.0;
        const ClockAct a = ClockDecide(localAbs, pastBound, -1.0, 1, t);
        if (a.kind != kClockJumpOverBound)
            Fail(std::string("a BACKWARD gap one in-game minute past the bound was taken - expected kClockJumpOverBound got ") + KindName(a.kind));
        if (a.writeAbs != localAbs)
            Fail("a BACKWARD jump one minute past the bound moved this clock - expected " + D(localAbs) + " got " + D(a.writeAbs));
    }
    {   /* FORWARD one minute past the bound is a jump - the asymmetry amendment 46c asks for */
        const double pastBound = localAbs + (t.joinBoundMinutes + 1.0) / 60.0;
        const ClockAct a = ClockDecide(localAbs, pastBound, -1.0, 1, t);
        if (a.kind != kClockJump)
            Fail(std::string("a FORWARD gap one in-game minute past the bound was refused (amendment 46c) - expected kClockJump got ") + KindName(a.kind));
    }
    /* And the same magnitudes WITHOUT the jump owed: the forward one is refused by the 24-hour bound, which is
       what makes "the jump is exempt" a statement about the jump and not about the magnitude. */
    {
        const ClockAct a = ClockDecide(localAbs, localAbs + fourWeeks, -1.0, 0, t);
        if (a.kind != kClockOverBound)
            Fail(std::string("four weeks behind with no jump owed was not refused - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(localAbs + fourWeeks) + " joinJumpOwed=0 expected kClockOverBound got " + KindName(a.kind));
    }
}

/* ------------------------------------------------------------------------------------------------
   4. clock_decide_forward_bound_refuses - review-p7j H-3, both ends.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_forward_bound_refuses()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 500.0;
    {
        const double sharedAbs = localAbs + 23.9;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 0, t);
        if (a.kind != kClockForwardSet)
            Fail(std::string("23.9 in-game hours behind was not a forward set - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " expected kClockForwardSet got " + KindName(a.kind));
        if (a.writeAbs != sharedAbs)
            Fail("the forward set did not target the shared clock exactly - inputs sharedAbs=" + D(sharedAbs)
                 + " expected " + D(sharedAbs) + " got " + D(a.writeAbs));
    }
    {
        const double sharedAbs = localAbs + 24.1;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 0, t);
        if (a.kind != kClockOverBound)
            Fail(std::string("24.1 in-game hours behind was not refused - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " expected kClockOverBound got " + KindName(a.kind));
        if (a.writeAbs != 0.0)
            Fail("kClockOverBound consumed a writeAbs - inputs sharedAbs=" + D(sharedAbs) + " expected 0 (unset) got " + D(a.writeAbs));
    }
    /* THE NOTEBOOK'S BOUNDED PLAY-TIME PULL, mirrored. */
    {
        double ns = -777.0;
        const int k = ClockPullDecide(100.0, 1, 0, 100.05, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
        if (k != kPullPlayForward)
            Fail(std::string("a game 3 in-game minutes ahead did not pull this world forward - inputs shared=100 seeded=1 windowOpen=0 fromGame=100.05 bound=5min expected kPullPlayForward got ") + PullName(k));
        if (ns != 100.05)
            Fail("the pull did not target the game's clock exactly - inputs fromGame=100.05 expected 100.05 got " + D(ns));
    }
    {
        double ns = -777.0;
        /* P7w (F583): 12 in-game minutes ahead is RATE-LIMITED, not refused. The refusal now lives only at
           the 24-hour mismatch width, and the leg below it checks that it is still there. */
        const int k = ClockPullDecide(100.0, 1, 0, 100.2, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
        if (k != kPullRateLimited)
            Fail(std::string("a game 12 in-game minutes ahead was not rate-limited - inputs shared=100 seeded=1 windowOpen=0 fromGame=100.2 bound=5min mismatch=1440min expected kPullRateLimited got ") + PullName(k));
        if (ns != 100.0 + 5.0 / 60.0)
            Fail("the rate limit did not advance by EXACTLY the bound - inputs shared=100 bound=5min expected " + D(100.0 + 5.0 / 60.0) + " got " + D(ns));
        ns = -777.0;
        {
            const int k2 = ClockPullDecide(100.0, 1, 0, 130.0, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
            if (k2 != kPullOverBound)
                Fail(std::string("a game 30 in-game HOURS ahead was not refused - inputs shared=100 fromGame=130 mismatch=1440min expected kPullOverBound got ") + PullName(k2));
            if (ns != -777.0)
                Fail("kPullOverBound wrote a new shared clock - inputs fromGame=130 expected the sentinel -777 untouched got " + D(ns));
        }
    }
    {
        double ns = -777.0;
        const int k = ClockPullDecide(100.0, 1, 0, 99.0, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
        if (k != kPullIgnore)
            Fail(std::string("a game BEHIND this world moved it - inputs shared=100 fromGame=99 expected kPullIgnore got ") + PullName(k));
        if (ns != -777.0)
            Fail("kPullIgnore wrote a new shared clock - inputs fromGame=99 expected the sentinel -777 untouched got " + D(ns));
    }
    {   /* the window arm, and the unseeded arm - both move forward, and the unseeded one takes anything */
        double ns = -777.0;
        const int k = ClockPullDecide(100.0, 1, 1, 100.9, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
        if (k != kPullWindowAdvance || ns != 100.9)
            Fail(std::string("inside the seed window a later clock did not advance this world - inputs shared=100 windowOpen=1 fromGame=100.9 expected kPullWindowAdvance/100.9 got ") + PullName(k) + "/" + D(ns));
        ns = -777.0;
        {
            const int k2 = ClockPullDecide(-1.0, 0, 0, 42.0, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
            if (k2 != kPullSeed || ns != 42.0)
                Fail(std::string("an unseeded world was not seeded - inputs seeded=0 fromGame=42 expected kPullSeed/42 got ") + PullName(k2) + "/" + D(ns));
        }
    }
}

/* ------------------------------------------------------------------------------------------------
   5. clock_decide_cap_is_a_fraction_of_the_measured_advance.
   TWO CORRECTIONS TO design-offline-tests 5, both found by running it:
     * its first leg proposed a 60-in-game-minute gap with the throttle expected to add half a minute. 60 is
       ABOVE the 30-minute forward-set band of design-clock46 4, so that gap is SET in one pass, not throttled.
       Run here at 20 minutes - inside the throttle band, where the cap is the point.
     * its second leg proposed a 0.2-in-game-minute gap to show the gap clamping the cap, but 0.2 minutes is
       INSIDE the 1-minute dead band, so the decision is kClockNothing and nothing is clamped. Run here with a
       gap above the dead band and below the cap, which is the case it was describing.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_cap_is_a_fraction_of_the_measured_advance()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 100.0;
    {   /* behind 20 in-game minutes, measured advance 1 in-game minute -> add 0.5 minutes, not 10 */
        const double prev = localAbs - 1.0 / 60.0;
        const double sharedAbs = localAbs + 20.0 / 60.0;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, prev, 0, t);
        if (a.kind != kClockThrottle)
            Fail(std::string("a 20-minute gap inside the throttle band was not throttled - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(prev) + " expected kClockThrottle got " + KindName(a.kind));
        {
            const double added = a.writeAbs - localAbs;
            const double want = 0.5 / 60.0;
            const double err = (added > want) ? (added - want) : (want - added);
            if (!(err < 1e-12))
                Fail("the throttle added more than half the measured advance - inputs measuredAdvance=1 in-game minute gap=20 in-game minutes expected added="
                     + D(want) + " got " + D(added));
            if (!(a.capHours > 0.0))
                Fail("capHours did not report the bound that applied - inputs measuredAdvance=1 in-game minute expected > 0 got " + D(a.capHours));
        }
    }
    {   /* the GAP is the binding clamp: measured advance 10 in-game minutes (cap 5), gap 2 minutes -> no overshoot */
        const double prev = localAbs - 10.0 / 60.0;
        const double sharedAbs = localAbs + 2.0 / 60.0;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, prev, 0, t);
        if (a.kind != kClockThrottle)
            Fail(std::string("a 2-minute gap above the dead band was not throttled - inputs localAbs=") + D(localAbs)
                 + " sharedAbs=" + D(sharedAbs) + " prevLocalAbs=" + D(prev) + " expected kClockThrottle got " + KindName(a.kind));
        if (a.writeAbs != sharedAbs)
            Fail("the throttle OVERSHOT the shared clock - inputs measuredAdvance=10 in-game minutes gap=2 in-game minutes expected writeAbs="
                 + D(sharedAbs) + " got " + D(a.writeAbs));
    }
}

/* ------------------------------------------------------------------------------------------------
   6. relay_resting_speed_is_the_last_real_one_not_zero - review-p7e H-3's crafted input.
   Before P7o this returned 0.0f, that value went into clock.txt and into the CLOCK a joining game is handed,
   so THE FIRST CLOCK A JOINING GAME EVER SAW SAID THE WORLD WAS PAUSED. One assertion, no game, no relay.
   ------------------------------------------------------------------------------------------------ */
void t_relay_resting_speed_is_the_last_real_one_not_zero()
{
    const std::vector<void*> none;
    const std::map<void*, float> noVotes;
    if (EffectiveSpeedFrom(none, noVotes, 0, 2.0f) != 2.0f)
        Fail("a resting notebook did not publish the last real speed - inputs order={} votes={} consensus=0 lastRealSpeed=2 expected 2 got "
             + D(EffectiveSpeedFrom(none, noVotes, 0, 2.0f)));
    if (EffectiveSpeedFrom(none, noVotes, 1, 5.0f) != 5.0f)
        Fail("a resting notebook did not publish the last real speed - inputs order={} votes={} consensus=1 lastRealSpeed=5 expected 5 got "
             + D(EffectiveSpeedFrom(none, noVotes, 1, 5.0f)));
    if (EffectiveSpeedFrom(none, noVotes, 0, 1.0f) != 1.0f)
        Fail("a resting notebook did not publish the last real speed - inputs order={} votes={} consensus=0 lastRealSpeed=1 expected 1 got "
             + D(EffectiveSpeedFrom(none, noVotes, 0, 1.0f)));
    {   /* and the whole way round the wire: what a joining game is actually told */
        char buf[14];
        double h = 0.0; float sp = 0.0f; unsigned char md = 9, ss = 9;
        ClockEncodeDown(buf, 1234.5, EffectiveSpeedFrom(none, noVotes, 0, 2.0f), (unsigned char)0, (unsigned char)1);
        if (!ClockParseDown(buf, 14, &h, &sp, &md, &ss))
            Fail("a 14-byte CLOCK did not parse - inputs n=14 expected 1 got 0");
        if (sp != 2.0f)
            Fail("the speed a joining game reads back is not the resting pace - inputs hours=1234.5 lastRealSpeed=2 expected 2 got " + D(sp));
        if (h != 1234.5)
            Fail("the hours a joining game reads back are wrong - inputs hours=1234.5 expected 1234.5 got " + D(h));
    }
}

/* ------------------------------------------------------------------------------------------------
   7. relay_consensus_is_the_minimum_over_games_that_voted - decision 45's rule.
   A game that has NEVER voted is skipped, not counted as 1x: a joiner must not drag a 5x world to 1x merely
   by connecting.
   ------------------------------------------------------------------------------------------------ */
void t_relay_consensus_is_the_minimum_over_games_that_voted()
{
    static int a = 1, b = 2, c = 3;
    void* A = (void*)&a; void* B = (void*)&b; void* C = (void*)&c;
    std::vector<void*> order; order.push_back(A); order.push_back(B); order.push_back(C);
    {
        std::map<void*, float> votes; votes[A] = 5.0f; votes[B] = 2.0f;   /* C silent */
        const float v = EffectiveSpeedFrom(order, votes, 1, 1.0f);
        if (v != 2.0f) Fail("consensus was not the minimum over the games that voted - inputs votes={A:5,B:2} C silent expected 2 got " + D(v));
    }
    {
        const std::map<void*, float> votes;                                /* all silent */
        const float v = EffectiveSpeedFrom(order, votes, 1, 9.0f);
        if (v != 1.0f) Fail("three silent games did not read as 1x - inputs votes={} consensus=1 expected 1 got " + D(v));
    }
    {
        std::map<void*, float> votes; votes[A] = 0.0f;                     /* pause is a vote and the slowest wins */
        const float v = EffectiveSpeedFrom(order, votes, 1, 1.0f);
        if (v != 0.0f) Fail("a pause vote did not win the consensus - inputs votes={A:0} B,C silent expected 0 got " + D(v));
    }
    {
        std::vector<void*> two; two.push_back(A); two.push_back(B);
        std::map<void*, float> votes; votes[B] = 0.0f;                     /* only the FRONT of the order decides */
        const float v = EffectiveSpeedFrom(two, votes, 0, 1.0f);
        if (v != 1.0f) Fail("under timemode fixed a non-authority vote decided the pace - inputs order={A,B} votes={B:0} consensus=0 expected 1 got " + D(v));
    }
}

/* ------------------------------------------------------------------------------------------------
   8. clock_wire_round_trip_and_truncation - THE SHAPE DOES NOT CHANGE (design-clock46 6).
   ------------------------------------------------------------------------------------------------ */
void t_clock_wire_round_trip_and_truncation()
{
    const float speeds[4] = { 0.0f, 1.0f, 2.0f, 5.0f };
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 5; ++k)
            {
                char buf[14];
                const double hours = 4242.4242424242;
                double h = -1.0; float sp = -1.0f; unsigned char md = 99, ss = 99;
                ClockEncodeDown(buf, hours, speeds[i], (unsigned char)j, (unsigned char)k);
                if (!ClockParseDown(buf, 14, &h, &sp, &md, &ss))
                { Fail("a 14-byte CLOCK did not parse - inputs speed=" + D(speeds[i]) + " mode=" + I(j) + " seedState=" + I(k) + " expected 1 got 0"); continue; }
                if (std::memcmp(&h, &hours, sizeof(double)) != 0)
                    Fail("hours did not survive bit-exact - inputs hours=" + D(hours) + " expected the same 8 bytes got " + D(h));
                if (sp != speeds[i]) Fail("speed did not survive - inputs speed=" + D(speeds[i]) + " expected " + D(speeds[i]) + " got " + D(sp));
                if (md != (unsigned char)j) Fail("mode did not survive - inputs mode=" + I(j) + " expected " + I(j) + " got " + I((int)md));
                if (ss != (unsigned char)k) Fail("seedState did not survive - inputs seedState=" + I(k) + " expected " + I(k) + " got " + I((int)ss));
            }
    {   /* 13 bytes: an older notebook. Parses, seedState defaults to 0. */
        char buf[14]; double h = -1.0; float sp = -1.0f; unsigned char md = 99, ss = 99;
        ClockEncodeDown(buf, 7.5, 2.0f, (unsigned char)1, (unsigned char)3);
        if (!ClockParseDown(buf, 13, &h, &sp, &md, &ss))
            Fail("a 13-byte CLOCK was refused - inputs n=13 expected 1 got 0");
        if (ss != 0) Fail("a 13-byte CLOCK did not default seedState to 0 - inputs n=13 expected 0 got " + I((int)ss));
        if (h != 7.5 || sp != 2.0f || md != 1)
            Fail("a 13-byte CLOCK lost a field it carries - inputs n=13 hours=7.5 speed=2 mode=1 expected 7.5/2/1 got " + D(h) + "/" + D(sp) + "/" + I((int)md));
    }
    {   /* 12 bytes: refused, and NONE of the out-parameters is written. */
        char buf[14]; double h = -1.0; float sp = -1.0f; unsigned char md = 99, ss = 99;
        ClockEncodeDown(buf, 7.5, 2.0f, (unsigned char)1, (unsigned char)3);
        if (ClockParseDown(buf, 12, &h, &sp, &md, &ss) != 0)
            Fail("a 12-byte CLOCK was accepted - inputs n=12 expected 0 got 1");
        if (h != -1.0 || sp != -1.0f || md != 99 || ss != 99)
            Fail("a refused CLOCK wrote its out-parameters - inputs n=12 expected the sentinels -1/-1/99/99 untouched got "
                 + D(h) + "/" + D(sp) + "/" + I((int)md) + "/" + I((int)ss));
    }
    {   /* the speed predicate: pause, the buttons' speeds and a speed mod's, inside 0..kClockSpeedMax */
        const float good[9] = { 0.0f, 1.0f, 2.0f, 5.0f, 3.0f, 0.5f, 10.0f, 1.25f, kClockSpeedMax };
        const float bad[4]  = { -1.0f, -0.001f, kClockSpeedMax + 0.5f, 1e30f };
        float nan; const unsigned int nanBits = 0x7FC00000u; std::memcpy(&nan, &nanBits, 4);
        float inf; const unsigned int infBits = 0x7F800000u; std::memcpy(&inf, &infBits, 4);
        for (i = 0; i < 9; ++i) if (!ClockSpeedIsValid(good[i])) Fail("a speed inside 0..kClockSpeedMax was rejected - inputs speed=" + D(good[i]) + " expected 1 got 0");
        for (i = 0; i < 4; ++i) if (ClockSpeedIsValid(bad[i]))   Fail("a negative or over-range speed was accepted - inputs speed=" + D(bad[i]) + " expected 0 got 1");
        if (ClockSpeedIsValid(nan)) Fail("a NaN speed was accepted - inputs speed=NaN (0x7FC00000) expected 0 got 1");
        if (ClockSpeedIsValid(inf)) Fail("an infinite speed was accepted - inputs speed=+inf expected 0 got 1");
        for (i = 0; i < 4; ++i) if (!ClockSpeedIsBuiltIn(good[i])) Fail("a button speed was not named built-in - inputs speed=" + D(good[i]));
        for (i = 4; i < 9; ++i) if (ClockSpeedIsBuiltIn(good[i]))  Fail("a speed mod's speed was named built-in - inputs speed=" + D(good[i]));
    }
}

/* ------------------------------------------------------------------------------------------------
   T-604. A speed change no game button made (a speed mod): adopted as this player's vote, held while the
   notebook answers, and otherwise the world's speed is set again exactly as before.
   ------------------------------------------------------------------------------------------------ */
/* adoptAt / holdAt < 0 = not set */
static LiveSpeedMemo SpdMemo(float lastSeen, float appliedFor, long long adoptAt, long long holdAt, int holding)
{
    LiveSpeedMemo m; LiveSpeedMemoReset(&m);
    m.lastSeen = lastSeen; m.appliedFor = appliedFor; m.holding = holding;
    if (adoptAt >= 0) { m.adoptAt = (unsigned int)adoptAt; m.adopted = 1; }
    if (holdAt >= 0) { m.holdAt = (unsigned int)holdAt; m.held = 1; }
    return m;
}
/* One frame of the tick's caller (ClockApplySpeed), the world never answering: returns the speed the engine runs at. */
static float SpdFrame(LiveSpeedMemo* m, float live, float srv, float* vote, unsigned int now, int* votes)
{
    const int act = ClockLiveSpeedDecide(*m, live, srv, *vote, now, 0);
    if (act == kLiveSpeedNothing || act == kLiveSpeedHold) return live;
    if (act == kLiveSpeedAdopt)
    {
        const float value = LiveSpeedAdoptValue(*m, live);
        ++*votes; *vote = value; LiveSpeedNoteAdopted(m, live, now, 1);
        if (m->holding && value == live) return live;
    }
    else if (act == kLiveSpeedWait) { LiveSpeedNoteWaited(m, live); if (m->holding) return live; }
    LiveSpeedNoteApplied(m, srv, srv);
    return srv;
}
void t_clock_speed_mod_change_is_a_vote()
{
    LiveSpeedMemo m; LiveSpeedMemoReset(&m);
    if (m.lastSeen != -1.0f || m.appliedFor != -1.0f || m.pending != -1.0f || m.adopted != 0 || m.held != 0 || m.holding != 0) Fail("a reset memo is not empty");
    /* the T1073 story: world 1x, this game's own apply recorded; A's speed mod writes 3 -> adopted and held */
    LiveSpeedNoteApplied(&m, 1.0f, 1.0f);
    if (ClockLiveSpeedDecide(m, 3.0f, 1.0f, 1.0f, 1000u, 0) != kLiveSpeedAdopt) Fail("a speed mod's 3x on a 1x world was not adopted as a vote");
    if (LiveSpeedAdoptValue(m, 3.0f) != 3.0f) Fail("the adoption does not send the global's 3x");
    LiveSpeedNoteAdopted(&m, 3.0f, 1000u, 1);
    if (m.holding != 1 || m.lastSeen != 3.0f || m.appliedFor != -1.0f) Fail("an adoption whose vote counts was not held");
    if (ClockLiveSpeedDecide(m, 3.0f, 1.0f, 3.0f, 1500u, 0) != kLiveSpeedHold) Fail("an adopted 3x was not held before the world answered");
    if (ClockLiveSpeedDecide(m, 3.0f, 1.0f, 3.0f, 1100u, 1) != kLiveSpeedApply) Fail("a hold outlived the world's answer of 1x");
    if (ClockLiveSpeedDecide(m, 3.0f, 3.0f, 3.0f, 1100u, 1) != kLiveSpeedNothing) Fail("the world's answer of 3x was changed");
    if (ClockLiveSpeedDecide(m, 3.0f, 1.0f, 3.0f, 1000u + kClockSpeedVoteHoldMs, 0) != kLiveSpeedApply) Fail("a hold with no answer outlived its ceiling");
    if (ClockLiveSpeedDecide(m, 3.0f, 3.0f, 3.0f, 61000u, 1) != kLiveSpeedNothing) Fail("3x held by the world for 60 s was changed");
    /* no hold when the vote does not count (fixed time mode, not the authority) */
    { LiveSpeedMemo f = SpdMemo(1.0f, 1.0f, -1, -1, 0); LiveSpeedNoteAdopted(&f, 3.0f, 1000u, 0);
      if (f.holding != 0) Fail("an adoption under fixed time mode on a non-authority game was held");
      if (ClockLiveSpeedDecide(f, 3.0f, 1.0f, 3.0f, 1100u, 0) != kLiveSpeedApply) Fail("an uncounted vote's speed was kept"); }
    /* at most one hold per kClockSpeedRevoteMs */
    { LiveSpeedMemo h = SpdMemo(3.0f, -1.0f, 1000, 1000, 0); LiveSpeedNoteAdopted(&h, 4.0f, 1300u, 1);
      if (h.holding != 0) Fail("a second hold began inside the revote period");
      LiveSpeedNoteAdopted(&h, 6.0f, 1000u + kClockSpeedRevoteMs, 1);
      if (h.holding != 1) Fail("no hold began after the revote period"); }
    /* the adoption gap: a change inside it waits, its value is kept and adopted after the gap even if the world's speed was set meanwhile */
    { LiveSpeedMemo g = SpdMemo(3.0f, -1.0f, 1000, 1000, 0);
      if (ClockLiveSpeedDecide(g, 4.0f, 1.0f, 3.0f, 1100u, 0) != kLiveSpeedWait) Fail("a change 100 ms after an adoption was not made to wait");
      LiveSpeedNoteWaited(&g, 4.0f); LiveSpeedNoteApplied(&g, 1.0f, 1.0f);
      if (ClockLiveSpeedDecide(g, 1.0f, 1.0f, 3.0f, 1200u, 0) != kLiveSpeedNothing) Fail("a kept value was adopted inside the gap");
      if (ClockLiveSpeedDecide(g, 1.0f, 1.0f, 3.0f, 1000u + kClockSpeedAdoptGapMs, 0) != kLiveSpeedAdopt) Fail("a kept value was not adopted after the gap");
      if (LiveSpeedAdoptValue(g, 1.0f) != 4.0f) Fail("the kept 4x was not the adopted value");
      LiveSpeedNoteAdopted(&g, 1.0f, 1250u, 1);
      if (g.pending != -1.0f) Fail("an adoption left the kept value standing"); }
    /* MED-A: a mod changing the speed every frame for 5 s keeps this game off the world's speed for at most one hold */
    { LiveSpeedMemo sl = SpdMemo(1.0f, 1.0f, -1, -1, 0); float vote = 1.0f; int votes = 0; unsigned int offMs = 0, t;
      float v = 3.0f;
      for (t = 0u; t < 5000u; t += 16u)
      { v = (v >= 5.0f) ? 3.0f : v + 0.01f; if (SpdFrame(&sl, v, 1.0f, &vote, t, &votes) != 1.0f) offMs += 16u; }
      if (offMs > kClockSpeedVoteHoldMs) Fail("MED-A: a mod changing every frame kept the game off the world's speed for " + I((int)offMs) + " ms of 5000");
      if (votes < 5 || votes > 21) Fail("a mod changing every frame for 5 s sent " + I(votes) + " votes, expected 5..21 (one per 250 ms at most)"); }
    /* LOW-B: across GetTickCount's wrap the gap and the hold's re-arm still count */
    { LiveSpeedMemo w = SpdMemo(3.0f, -1.0f, 0xFFFFFF00LL, 0xFFFFFF00LL, 0);
      if (ClockLiveSpeedDecide(w, 4.0f, 1.0f, 3.0f, 0x00000010u, 0) != kLiveSpeedAdopt) Fail("LOW-B: 272 ms across the wrap was read as inside the gap");
      if (ClockLiveSpeedDecide(w, 4.0f, 1.0f, 3.0f, 0xFFFFFF80u, 0) != kLiveSpeedWait) Fail("LOW-B: 128 ms before the wrap was not inside the gap");
      LiveSpeedNoteAdopted(&w, 4.0f, 0xFFFFFF00u + kClockSpeedRevoteMs, 1);
      if (w.holding != 1) Fail("LOW-B: the hold did not re-arm 10 s after a stamp taken before the wrap"); }
    /* HIGH-1 (a): a button press while unlinked moves the global to 5; the stale apply of 1x must not answer Nothing */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 1.0f, 1.0f); LiveSpeedNoteSeen(&k, 5.0f);
      if (k.appliedFor != -1.0f) Fail("a recorded change did not clear the apply");
      if (ClockLiveSpeedDecide(k, 5.0f, 1.0f, 5.0f, 5000u, 1) != kLiveSpeedApply) Fail("HIGH-1: after a link bounce the game was left at 5x on a 1x world"); }
    /* HIGH-1 (b): the world is paused (this game paused it), the engine's own window unpauses to 1 */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 0.0f, 0.0f); LiveSpeedNoteSeen(&k, 1.0f);
      if (ClockLiveSpeedDecide(k, 1.0f, 0.0f, 0.0f, 5000u, 1) != kLiveSpeedApply) Fail("HIGH-1: an engine unpause on a paused world was not paused again"); }
    /* HIGH-1 (c): a reload - the memo is reset at teardown; the new world's first frame resyncs and applies */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 1.0f, 1.0f); LiveSpeedMemoReset(&k); LiveSpeedNoteSeen(&k, 2.0f);
      if (ClockLiveSpeedDecide(k, 2.0f, 1.0f, 1.0f, 5000u, 1) != kLiveSpeedApply) Fail("HIGH-1: a reloaded world's speed was left at 2x on a 1x world"); }
    /* the engine kept a different value for this game's own apply, and nothing changed since: not set again */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 5.0f, 8.0f);
      if (ClockLiveSpeedDecide(k, 5.0f, 8.0f, 1.0f, 5000u, 1) != kLiveSpeedNothing) Fail("a speed the engine settled was set again every frame"); }
    /* B follows the world to 3x */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 1.0f, 1.0f);
      if (ClockLiveSpeedDecide(k, 1.0f, 3.0f, 1.0f, 5000u, 1) != kLiveSpeedApply) Fail("B did not follow the world to 3x"); }
    /* a mod that rewrites the same refused speed is re-sent only after kClockSpeedRevoteMs */
    { LiveSpeedMemo k = SpdMemo(1.0f, 1.0f, 1000, 1000, 0);
      if (ClockLiveSpeedDecide(k, 3.0f, 1.0f, 3.0f, 4000u, 1) != kLiveSpeedApply) Fail("a restated 3x inside the revote period was not set back");
      if (ClockLiveSpeedDecide(k, 3.0f, 1.0f, 3.0f, 1000u + kClockSpeedRevoteMs, 1) != kLiveSpeedAdopt) Fail("a restated 3x after the revote period was not sent again"); }
    /* not adopted: a pause nobody voted for, an over-range or NaN value, a first frame with nothing recorded */
    { LiveSpeedMemo k = SpdMemo(1.0f, 1.0f, -1, -1, 0);
      float nan; const unsigned int nanBits = 0x7FC00000u; std::memcpy(&nan, &nanBits, 4);
      if (ClockLiveSpeedDecide(k, 0.0f, 1.0f, 1.0f, 0u, 1) != kLiveSpeedApply) Fail("an unaccounted 0 was adopted instead of set back");
      if (ClockLiveSpeedDecide(k, 60.0f, 1.0f, 1.0f, 0u, 1) != kLiveSpeedApply) Fail("a 60x (over kClockSpeedMax) was not set back");
      if (ClockLiveSpeedDecide(k, nan, 1.0f, 1.0f, 0u, 1) != kLiveSpeedApply) Fail("a NaN speed global was not set back");
      LiveSpeedMemo e; LiveSpeedMemoReset(&e);
      if (ClockLiveSpeedDecide(e, 3.0f, 1.0f, 1.0f, 0u, 1) != kLiveSpeedApply) Fail("a change with nothing recorded was adopted"); }
    /* a paused world: a speed mod's 3x is a vote to run at 3x */
    { LiveSpeedMemo k = SpdMemo(-1.0f, -1.0f, -1, -1, 0); LiveSpeedNoteApplied(&k, 0.0f, 0.0f);
      if (ClockLiveSpeedDecide(k, 3.0f, 0.0f, 0.0f, 0u, 1) != kLiveSpeedAdopt) Fail("a speed mod's 3x on a paused world was not adopted"); }

    /* which passed-through setter calls are accounted */
    if (ClockSetterCallAccounted(1, 0, 1.0f, 0) != 1) Fail("an engine setter call from inside the game image was not accounted");
    if (ClockSetterCallAccounted(0, 0, 3.0f, 0) != 0) Fail("a speed mod's setGameSpeed(3) from outside the image was accounted");
    if (ClockSetterCallAccounted(0, 0, 0.0f, 0) != 1) Fail("another mod's pause from outside the image was taken as a pace");
    if (ClockSetterCallAccounted(0, 0, 1.0f, 1) != 1) Fail("a call that ends an engine-held pause was taken as a pace");
    if (ClockSetterCallAccounted(0, 1, 1.0f, 0) != 1) Fail("this mod's own setGameSpeed(1) was left to be adopted as a vote");
    /* T1073 log-B: the host-left resume (this mod's own write, 0 -> 1, while the world is at 3x) is noted as seen before the
       tick decides, so it is set to the world's speed and never adopted */
    { LiveSpeedMemo k = SpdMemo(0.0f, -1.0f, -1, -1, 0);
      if (ClockLiveSpeedDecide(k, 1.0f, 3.0f, 3.0f, 5000u, 1) != kLiveSpeedAdopt) Fail("the unrecorded resume is not the case this guards");
      LiveSpeedNoteSeen(&k, 1.0f);
      if (ClockLiveSpeedDecide(k, 1.0f, 3.0f, 3.0f, 5000u, 1) != kLiveSpeedApply) Fail("this mod's own recorded resume was not set to the world's 3x"); }
}

/* ------------------------------------------------------------------------------------------------
   9. clock_shared_hours_interpolates_and_refuses_a_stale_reading.
   ------------------------------------------------------------------------------------------------ */
void t_clock_shared_hours_interpolates_and_refuses_a_stale_reading()
{
    if (ClockSharedHoursAt(-1.0, 0.0, 1000.0, 1.0f, 109.0909f) >= 0.0)
        Fail("an unseeded world reported a clock - inputs srvHours=-1 expected < 0 got " + D(ClockSharedHoursAt(-1.0, 0.0, 1000.0, 1.0f, 109.0909f)));
    {   /* 10.90909 real seconds is a tenth of an in-game hour at 1x. It is deliberately under the 30-second
           extrapolation limit below: at 109.09 s - a whole in-game hour - the reading is refused as stale, so
           a test written at one in-game hour would be asserting the wrong arm. */
        const double v = ClockSharedHoursAt(100.0, 0.0, 10909.09, 1.0f, 109.0909f);
        const double err = (v > 100.1) ? (v - 100.1) : (100.1 - v);
        if (!(err < 1e-4))
            Fail("a tenth of an in-game hour of real time did not advance the shared clock by 0.1 h - inputs srvHours=100 dt=10.90909 s speed=1 expected 100.1 got " + D(v));
    }
    {   /* a stale or wrapped reading is NOT extrapolated */
        const double v = ClockSharedHoursAt(100.0, 0.0, 60000.0, 1.0f, 109.0909f);
        if (v != 100.0) Fail("a 60-second-old CLOCK was extrapolated - inputs dt=60 s (over the 30 s limit) expected 100 got " + D(v));
        {
            const double w = ClockSharedHoursAt(100.0, 5000.0, 0.0, 1.0f, 109.0909f);
            if (w != 100.0) Fail("a wrapped tick count was extrapolated backward - inputs nowMs < srvAtMs expected 100 got " + D(w));
        }
    }
    {   /* a paused world does not drift */
        const double v = ClockSharedHoursAt(100.0, 0.0, 10000.0, 0.0f, 109.0909f);
        if (v != 100.0) Fail("a paused world's clock advanced - inputs speed=0 dt=10 s expected 100 got " + D(v));
    }
}

/* ------------------------------------------------------------------------------------------------
   10. clock_decide_dead_band_and_join_jump_precedence.
   The jump is decided by a LEVEL, not by a magnitude - review-p7j L-1: at HEAD a hard set INSIDE the +/-1
   minute dead band could still step the clock back. Here the level is asked first and the dead band cannot
   swallow it.
   ------------------------------------------------------------------------------------------------ */
void t_clock_decide_dead_band_and_join_jump_precedence()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 300.0;
    {   /* half a minute BEHIND, jump owed -> still a jump, targeting the shared clock exactly */
        const double sharedAbs = localAbs + 0.5 / 60.0;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 1, t);
        if (a.kind != kClockJump || a.writeAbs != sharedAbs)
            Fail(std::string("a jump owed inside the dead band was swallowed - inputs gap=+0.5 in-game minutes joinJumpOwed=1 expected kClockJump/")
                 + D(sharedAbs) + " got " + KindName(a.kind) + "/" + D(a.writeAbs));
    }
    {   /* half a minute AHEAD, jump owed -> a jump BACKWARD, which is the only backward write in the design */
        const double sharedAbs = localAbs - 0.5 / 60.0;
        const ClockAct a = ClockDecide(localAbs, sharedAbs, -1.0, 1, t);
        if (a.kind != kClockJump || !(a.writeAbs < localAbs))
            Fail(std::string("the one backward write the design allows was refused - inputs gap=-0.5 in-game minutes joinJumpOwed=1 expected kClockJump below ")
                 + D(localAbs) + " got " + KindName(a.kind) + "/" + D(a.writeAbs));
    }
    {   /* the same half minute with NO jump owed -> nothing at all, in either direction */
        const ClockAct behind = ClockDecide(localAbs, localAbs + 0.5 / 60.0, localAbs - 0.01, 0, t);
        const ClockAct ahead  = ClockDecide(localAbs, localAbs - 0.5 / 60.0, localAbs - 0.01, 0, t);
        if (behind.kind != kClockNothing)
            Fail(std::string("a half-minute behind was acted on - inputs gap=+0.5 in-game minutes joinJumpOwed=0 expected kClockNothing got ") + KindName(behind.kind));
        if (ahead.kind != kClockNothing)
            Fail(std::string("a half-minute ahead was acted on - inputs gap=-0.5 in-game minutes joinJumpOwed=0 expected kClockNothing got ") + KindName(ahead.kind));
    }
}

/* ------------------------------------------------------------------------------------------------
   11. clock_forward_set_never_below_current (P7w fold 2, F582 / review-p7u H-1).
   The DECISION half of "the mid-play forward set never writes the clock backward". The WRITE half lives in
   store.cpp's ClockAdvanceToPod and this suite cannot see it (F580's own boundary) - what it CAN assert is
   that a forward set is only ever produced when the shared clock is genuinely ahead of this game, over a
   sweep that reaches the arm from both sides of every band edge, including the two edges review-p7u L-5
   found missing: exactly 1.0 in-game minute (the dead band) and exactly 30.0 (the forward-set threshold).
   ------------------------------------------------------------------------------------------------ */
void t_clock_forward_set_never_below_current()
{
    const ClockTuning t = ClockDefaultTuning();
    /* in-game MINUTES of gap ASKED FOR; the identifier carries the unit (6a lesson 1). The two edges
       review-p7u L-5 found missing - exactly 1.0 and exactly 30.0 - are in the grid so the arm boundary is
       REACHED. Every assertion below is stated against `a.gapMinutes`, THE VALUE THE FUNCTION ITSELF
       COMPUTED, and never against the number this loop asked for: localAbs + m/60 then (shared-local)*60 is
       not exact in binary, so a test that asserted which side of an edge the asked-for value landed on would
       be asserting the rounding rather than the rule. */
    const double gapMinutes[12] = { -30.0, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 29.9, 30.0, 30.1, 120.0, 600.0 };
    /* locals[0] is 1.0 and not 0.0 ON PURPOSE: prevLocalAbs below is localAbs - 0.01, and a NEGATIVE prev
       is this function's "no previous pass in this world epoch" sentinel, which would silently turn every
       throttle case at that local into kClockNothing - a leg whose premise fails reports a vacuous pass. */
    const double locals[3] = { 1.0, 23.9, 24000.0 };
    int i, j, forwardSetsSeen = 0, forwardSetsExpected = 0, cases = 0;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 12; ++j)
        {
            const double localAbs = locals[i];
            const double sharedAbs = localAbs + gapMinutes[j] / 60.0;
            const ClockAct a = ClockDecide(localAbs, sharedAbs, localAbs - 0.01, 0, t);
            const double g = a.gapMinutes;
            const double absG = (g < 0.0) ? -g : g;
            ++cases;
            if (g > t.forwardSetMinutes && g <= t.forwardBoundHours * 60.0) ++forwardSetsExpected;
            if (a.kind == kClockForwardSet)
            {
                ++forwardSetsSeen;
                /* THE PROPERTY, over the arm F582 re-routed: a forward set never targets a lower time. */
                if (!(a.writeAbs >= localAbs))
                    Fail("a forward set produced a target BELOW the current clock - inputs localAbs=" + D(localAbs)
                         + " gapMinutesAsked=" + D(gapMinutes[j]) + " gapMinutesComputed=" + D(g)
                         + " expected writeAbs >= " + D(localAbs) + " got " + D(a.writeAbs));
                if (!(g > t.forwardSetMinutes))
                    Fail("a forward set was decided at or below the 30-minute band - inputs localAbs=" + D(localAbs)
                         + " gapMinutesComputed=" + D(g) + " expected some other kind got kClockForwardSet");
            }
            /* THE BAND RULE, asserted at every point including the two edges. */
            if (absG <= t.deadBandMinutes && a.kind != kClockNothing)
                Fail(std::string("a gap inside the dead band was acted on - inputs localAbs=") + D(localAbs)
                     + " gapMinutesAsked=" + D(gapMinutes[j]) + " gapMinutesComputed=" + D(g)
                     + " expected kClockNothing got " + KindName(a.kind));
            if (absG > t.deadBandMinutes && g < 0.0 && a.kind != kClockAheadNoWrite)
                Fail(std::string("a game ahead of the world outside the dead band did not refuse - inputs localAbs=") + D(localAbs)
                     + " gapMinutesComputed=" + D(g) + " expected kClockAheadNoWrite got " + KindName(a.kind));
            if (absG > t.deadBandMinutes && g > 0.0 && g <= t.forwardSetMinutes && a.kind != kClockThrottle)
                Fail(std::string("a gap inside the throttle band was not throttled - inputs localAbs=") + D(localAbs)
                     + " gapMinutesAsked=" + D(gapMinutes[j]) + " gapMinutesComputed=" + D(g)
                     + " engineAdvanceHours=" + D(a.engineAdvanceHours) + " expected kClockThrottle got " + KindName(a.kind));
        }
    if (cases != 36)
        Fail("the sweep did not cover the grid it claims - inputs 3 locals x 12 gaps expected 36 got " + I(cases));
    if (forwardSetsSeen != forwardSetsExpected || forwardSetsSeen < 9)
        Fail("the forward-set arm was not reached exactly where the band says it should be - expected "
             + I(forwardSetsExpected) + " (and at least 9) got " + I(forwardSetsSeen));
}

/* ------------------------------------------------------------------------------------------------
   T-288. clock_dialogue_pause_suppressed_in_a_session_only.
   A conversation window's pause (site 1) is skipped in a session and its paired unpause (site 2) with it;
   outside a session both pass through (single player unchanged); a conversation opened before connecting
   keeps its real pause and its unpause passes through; a stale flag never leaves single player paused.
   ------------------------------------------------------------------------------------------------ */
void t_clock_dialogue_pause_suppressed_in_a_session_only()
{
    int f = 0, r = 0;
    /* single player: both calls pass through, the flag stays down */
    r = DialoguePauseLetThrough(1, 0, &f); if (r != 1 || f != 0) Fail("single player: show's pause was not let through - inputs site=1 linked=0 expected 1/0 got " + I(r) + "/" + I(f));
    r = DialoguePauseLetThrough(2, 0, &f); if (r != 1 || f != 0) Fail("single player: hide's unpause was not let through - inputs site=2 linked=0 expected 1/0 got " + I(r) + "/" + I(f));
    /* session (a host alone included): both skipped, the flag goes up then down; a second hide passes */
    r = DialoguePauseLetThrough(1, 1, &f); if (r != 0 || f != 1) Fail("session: show's pause was not skipped - inputs site=1 linked=1 expected 0/1 got " + I(r) + "/" + I(f));
    r = DialoguePauseLetThrough(2, 1, &f); if (r != 0 || f != 0) Fail("session: hide's unpause was not skipped - inputs site=2 linked=1 flag=1 expected 0/0 got " + I(r) + "/" + I(f));
    r = DialoguePauseLetThrough(2, 1, &f); if (r != 1 || f != 0) Fail("session: an unpaired hide was skipped - inputs site=2 linked=1 flag=0 expected 1/0 got " + I(r) + "/" + I(f));
    /* opened before connecting, closed in a session: the real pause is lifted */
    f = 0;
    r = DialoguePauseLetThrough(1, 0, &f); if (r != 1 || f != 0) Fail("pre-link open: show was not let through - expected 1/0 got " + I(r) + "/" + I(f));
    r = DialoguePauseLetThrough(2, 1, &f); if (r != 1 || f != 0) Fail("pre-link open: hide's unpause was skipped, leaving the game paused - expected 1/0 got " + I(r) + "/" + I(f));
    /* opened in a session, link dropped before it closed: nothing to lift, the unpause stays skipped */
    r = DialoguePauseLetThrough(1, 1, &f); r = DialoguePauseLetThrough(2, 0, &f);
    if (r != 0 || f != 0) Fail("link dropped mid-conversation: hide was not paired with its skipped show - expected 0/0 got " + I(r) + "/" + I(f));
    /* a stale flag (session show, no hide, then single player): the next real pause is still lifted */
    r = DialoguePauseLetThrough(1, 1, &f);
    r = DialoguePauseLetThrough(1, 0, &f); if (r != 1 || f != 0) Fail("stale flag: an offline show did not lower it - expected 1/0 got " + I(r) + "/" + I(f));
    r = DialoguePauseLetThrough(2, 0, &f); if (r != 1) Fail("stale flag: an offline hide was skipped, leaving single player paused - expected 1 got " + I(r));
    /* not a conversation call: always through, flag untouched */
    f = 1; r = DialoguePauseLetThrough(0, 1, &f); if (r != 1 || f != 1) Fail("another caller was affected - inputs site=0 linked=1 flag=1 expected 1/1 got " + I(r) + "/" + I(f));
    if (DialoguePauseLetThrough(1, 1, 0) != 1) Fail("a null flag pointer did not fall back to let-through");
}

/* ------------------------------------------------------------------------------------------------
   12. clock_relink_no_jump (P7w fold 4, F584 / review-p7u H-3).
   The ARMING rule lives in store.cpp's ClockNoteEpoch, which this pure file cannot see. What it can assert
   is the property that makes the rule matter: with the jump NOT owed - which is what a relink mid-play now
   leaves - a game AHEAD of the world writes nothing at all, in any band, at any distance; and with the jump
   owed, the very same inputs produce an unbounded backward write. Those two lines together are why the arm
   site had to move.
   ------------------------------------------------------------------------------------------------ */
void t_clock_relink_no_jump()
{
    const ClockTuning t = ClockDefaultTuning();
    const double localAbs = 1000.0;
    const double aheadBy[4] = { 2.0, 27.0, 600.0, 4320.0 };   /* in-game MINUTES this game is AHEAD */
    int i;
    for (i = 0; i < 4; ++i)
    {
        const double sharedAbs = localAbs - aheadBy[i] / 60.0;
        {   /* the relink case: not owed -> nothing is written, ever */
            const ClockAct a = ClockDecide(localAbs, sharedAbs, localAbs - 0.01, 0, t);
            if (a.kind != kClockAheadNoWrite)
                Fail(std::string("a game ahead of the world with no jump owed did not refuse the write - inputs localAbs=1000 aheadByMinutes=")
                     + D(aheadBy[i]) + " joinJumpOwed=0 expected kClockAheadNoWrite got " + KindName(a.kind));
            if (a.writeAbs != 0.0)
                Fail("kClockAheadNoWrite consumed a writeAbs - inputs aheadByMinutes=" + D(aheadBy[i]) + " expected 0 (unset) got " + D(a.writeAbs));
        }
        {   /* the same inputs WITH the level owed: an unbounded backward write. This is what a link
               generation used to arm, and it is why it no longer does. */
            const ClockAct a = ClockDecide(localAbs, sharedAbs, localAbs - 0.01, 1, t);
            if (a.kind != kClockJump)
                Fail(std::string("the join jump was not answered before the bands - inputs localAbs=1000 aheadByMinutes=")
                     + D(aheadBy[i]) + " joinJumpOwed=1 expected kClockJump got " + KindName(a.kind));
            if (!(a.writeAbs < localAbs))
                Fail("the jump did not step BACKWARD for a game ahead of the world - inputs aheadByMinutes=" + D(aheadBy[i])
                     + " expected writeAbs < 1000 got " + D(a.writeAbs));
        }
    }
}

/* ------------------------------------------------------------------------------------------------
   13. clock_pull_converges (P7w fold 3, F583 / review-p7u H-2).
   Bounded per pass, unbounded in total. A game 27 in-game minutes ahead - the state the notebook's own
   dt > 60 clamp produces on a suspended machine - used to be refused every pass forever. Driven here pass
   by pass until it closes, with the pass count asserted against arithmetic (27 / 5 = 6 passes) rather than
   against whatever the loop happens to do.
   ------------------------------------------------------------------------------------------------ */
void t_clock_pull_converges()
{
    const double gameAbs = 100.45;          /* 27 in-game minutes ahead of 100.0 */
    double shared = 100.0;
    int passes = 0, rateLimited = 0, met = 0;
    while (passes < 50)
    {
        double ns = -777.0;
        const int k = ClockPullDecide(shared, 1, 0, gameAbs, 5.0, 1440.0, 7.0 * 24.0 * 60.0, &ns);
        ++passes;
        if (k == kPullRateLimited)
        {
            ++rateLimited;
            if (!(ns > shared))
                Fail("a rate-limited pull did not move this world forward - inputs shared=" + D(shared) + " fromGame=" + D(gameAbs) + " got " + D(ns));
            if (ns - shared > 5.0 / 60.0 + 1e-12)
                Fail("a rate-limited pull moved MORE than the per-pass bound - inputs shared=" + D(shared)
                     + " bound=5min expected at most " + D(5.0 / 60.0) + " got " + D(ns - shared));
            shared = ns;
            continue;
        }
        if (k == kPullPlayForward) { shared = ns; met = 1; break; }
        Fail(std::string("the pull took an unexpected arm while converging - inputs shared=") + D(shared)
             + " fromGame=" + D(gameAbs) + " expected kPullRateLimited or kPullPlayForward got " + PullName(k));
        break;
    }
    if (!met)
        Fail("the pull never met the game - inputs shared0=100 fromGame=100.45 bound=5min expected convergence within 50 passes got shared=" + D(shared) + " after " + I(passes) + " passes");
    if (shared != gameAbs)
        Fail("the pull converged to the wrong value - inputs fromGame=100.45 expected 100.45 got " + D(shared));
    if (rateLimited != 5 || passes != 6)
        Fail("the convergence did not take the number of passes the bound implies - inputs gap=27 in-game minutes bound=5 expected 5 rate-limited passes then 1 final pass (6 total) got " + I(rateLimited) + " and " + I(passes));
}

/* P7y, folding F610 / review-p7w H-1. THE WORLD GENERATION RISES ONCE PER LOAD AND ONCE PER TEARDOWN.
   The three inputs are the three real sequences, written out as literals rather than computed:
     a quit to the title    - one call, no latch
     a save load            - latch up, then TWO calls (detour_resetGame, then the detour_worldTeardown that
                              orig_resetGame nests inside itself)
     two loads in a process - the latch comes down between them, so the second load bumps again
   A green result says the ARITHMETIC is right. It says nothing about whether detour_resetGame really raises
   the latch - that is store.cpp and this exe cannot link it (the same boundary F580 draws). */
void t_worldgen_bump_once_per_load()
{
    using namespace cooplatch;
    if (WorldGenBumpDecide(0, 0) != 1)
        Fail("a standalone teardown-to-title did not bump - inputs latch=0 alreadyBumped=0 expected 1 got " + I(WorldGenBumpDecide(0, 0)));
    if (WorldGenBumpDecide(1, 0) != 1)
        Fail("the FIRST call inside a load did not bump - inputs latch=1 alreadyBumped=0 expected 1 got " + I(WorldGenBumpDecide(1, 0)));
    if (WorldGenBumpDecide(1, 1) != 0)
        Fail("the nested teardown inside a load bumped a SECOND time - inputs latch=1 alreadyBumped=1 expected 0 got " + I(WorldGenBumpDecide(1, 1)));
    {
        /* The whole save load, replayed: the latch goes up, two calls arrive, exactly one bumps - and then
           the latch comes down (detour_resetGame's __finally) and a SECOND load repeats it. `already` is
           reset per load because WorldGenLoadLatchRaise() clears it. */
        int bumps = 0, nested = 0, load;
        for (load = 0; load < 2; ++load)
        {
            int already = 0, call;
            for (call = 0; call < 2; ++call)
            {
                if (WorldGenBumpDecide(1, already) != 0) { ++bumps; already = 1; }
                else ++nested;
            }
        }
        if (bumps != 2 || nested != 2)
            Fail("two save loads did not produce exactly two bumps and two nested calls - inputs 2 loads x 2 calls expected bumps=2 nested=2 got bumps=" + I(bumps) + " nested=" + I(nested));
    }
}

/* P7y, folding amendment 46b and F612 / review-p7w M-2. THE SEVEN-DAY BOUND REFUSES INSIDE THE WINDOW TOO.
   Every width below is a literal, and they are the same numbers src/coop-store/store_main.cpp holds as
   kClockSeedBoundMinutes (7 in-game days) and kClockMismatchWarnMinutes (1 in-game hour). */
void t_clock_pull_seed_bound_refuses_in_and_out_of_the_window()
{
    const double seedBound = 7.0 * 24.0 * 60.0;   /* in-game MINUTES - kClockSeedBoundMinutes */
    const double warnWidth = 60.0;                /* in-game MINUTES - kClockMismatchWarnMinutes */
    const double shared = 100.0;
    double ns;
    int k;

    /* Day 1 against day 300, INSIDE the window. clock1 (amendment 46c, 2026-09-26): the furthest first player wins at any
       distance - a day-1 new character that seeded first must not hold the real world on day 1 (review-clock1 D1).
       P7y had refused this (F612); the refusal now applies outside the window only. */
    ns = -777.0;
    k = ClockPullDecide(shared, 1, 1, shared + 299.0 * 24.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullWindowAdvance)
        Fail("a 299-in-game-day offer inside the seed window was not taken - inputs shared=100 fromGame=" + D(shared + 299.0 * 24.0) + " windowOpen=1 seedBound=" + D(seedBound) + " expected kPullWindowAdvance got " + PullName(k));
    if (ns != shared + 299.0 * 24.0)
        Fail("a 299-in-game-day offer inside the window did not advance the world to it - expected " + D(shared + 299.0 * 24.0) + " got " + D(ns));

    /* Outside the window the same offer is refused - the seven-day width still guards a running world. */
    ns = -777.0;
    k = ClockPullDecide(shared, 1, 0, shared + 299.0 * 24.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullSeedOverBound)
        Fail(std::string("a 299-in-game-day offer outside the seed window took the wrong arm - inputs windowOpen=0 expected kPullSeedOverBound got ") + PullName(k));

    /* Exactly ON the bound is ACCEPTED: the refusal is strictly greater-than, so the width is inclusive. */
    ns = -777.0;
    k = ClockPullDecide(shared, 1, 1, shared + seedBound / 60.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullWindowAdvance)
        Fail("an offer exactly ON the seven-day bound was refused - inputs delta=" + D(seedBound) + " in-game minutes windowOpen=1 expected kPullWindowAdvance got " + PullName(k));
    if (ns != shared + seedBound / 60.0)
        Fail("an offer exactly ON the bound did not advance this world to it - expected " + D(shared + seedBound / 60.0) + " got " + D(ns));

    /* Just over the WARN width - six in-game hours - is APPLIED, not refused. The warning is a log line on
       the relay and this test exists to state that it never becomes a refusal by accident. */
    ns = -777.0;
    k = ClockPullDecide(shared, 1, 1, shared + 6.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullWindowAdvance)
        Fail("a six-in-game-hour offer inside the window was not applied - inputs delta=360 in-game minutes warnWidth=" + D(warnWidth) + " expected kPullWindowAdvance got " + PullName(k));
    if (ns != shared + 6.0)
        Fail("a six-in-game-hour offer did not advance this world - expected " + D(shared + 6.0) + " got " + D(ns));

    /* An UNSEEDED world takes the first offer whatever its size: there is no shared clock to move, so no
       width exists to refuse. This is the arm T235 needed and it must not be caught by the new bound. */
    ns = -777.0;
    k = ClockPullDecide(-1.0, 0, 1, 7200.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullSeed || ns != 7200.0)
        Fail(std::string("an unseeded world did not take the first offer - inputs seeded=0 fromGame=7200 expected kPullSeed and 7200 got ") + PullName(k) + " and " + D(ns));

    /* And a BEHIND game is still ignored, forward-only, whatever the widths say. */
    ns = -777.0;
    k = ClockPullDecide(shared, 1, 1, shared - 1000.0, 5.0, 1440.0, seedBound, &ns);
    if (k != kPullIgnore)
        Fail(std::string("a game far BEHIND this world was not ignored - inputs delta=-1000 in-game hours expected kPullIgnore got ") + PullName(k));
}

/* ---- P8b: THE NOTEBOOK'S INDEX LINE, ITS CHECKSUM AND ITS RECOVERY RULE (design-save.md S1) ------------
 *
 * Everything here is arithmetic and format over src\common\storemeta.cpp, which reads no engine memory and
 * touches no disk. A passing suite says NOTHING about whether the relay renames the right files - it says that
 * the DECISION those renames carry out is the one design-save 1.2(d) states, over inputs written out as
 * literals. Every fixture states its own precondition where a wrong fixture could pass vacuously.
 */
std::string H8(unsigned int v) { char b[32]; std::sprintf(b, "0x%08x", v); return b; }
std::string L64(long long v)   { char b[32]; std::sprintf(b, "%I64d", v); return b; }

const char* ChoiceName(int k)
{
    switch (k)
    {
        case coopstore::kChooseNone:            return "kChooseNone";
        case coopstore::kChooseCurrent:         return "kChooseCurrent";
        case coopstore::kChoosePrevMetaCurrent: return "kChoosePrevMetaCurrent";
        case coopstore::kChoosePrevPair:        return "kChoosePrevPair";
        case coopstore::kChooseCurrentRefused:  return "kChooseCurrentRefused";
        case coopstore::kChoosePrevRefused:     return "kChoosePrevRefused";
        default:                                return "??";
    }
}
const char* VerdictName(int v)
{
    switch (v)
    {
        case coopstore::kPayloadOk:           return "kPayloadOk";
        case coopstore::kPayloadNoneExpected: return "kPayloadNoneExpected";
        case coopstore::kPayloadMissing:      return "kPayloadMissing";
        case coopstore::kPayloadTorn:         return "kPayloadTorn";
        case coopstore::kPayloadCrcBad:       return "kPayloadCrcBad";
        case coopstore::kPayloadUnreadable:   return "kPayloadUnreadable";
        default:                              return "??";
    }
}

struct CrcRow { const char* text; unsigned int want; };

void t_crc32_known_vectors()
{
    static const CrcRow rows[] =
    {
        { "",                                           0x00000000u },
        { "a",                                          0xE8B7BE43u },
        { "abc",                                        0x352441C2u },
        { "123456789",                                  0xCBF43926u },
        { "The quick brown fox jumps over the lazy dog", 0x414FA339u }
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    int i;
    for (i = 0; i < n; ++i)
    {
        const unsigned int got = coopstore::Crc32(rows[i].text, std::strlen(rows[i].text));
        if (got != rows[i].want)
            Fail(std::string("CRC-32 of \"") + rows[i].text + "\" = " + H8(got) + ", want " + H8(rows[i].want)
                 + " (IEEE, reflected, poly 0xEDB88320 - the value zip and zlib produce)");
    }
    /* A checksum that ignored its input would pass every row above only if all five agreed; they do not. */
    if (coopstore::Crc32("a", 1) == coopstore::Crc32("b", 1))
        Fail("CRC-32 of \"a\" and of \"b\" are equal - this function is not reading its input");
}

void t_storemeta_v5_round_trip()
{
    coopstore::MetaLine m;
    m.writtenAt = 1234567890123LL;
    m.posAt = 42LL;
    m.owner = 3u;
    m.x = -50546.5f; m.y = 1074.0f; m.z = -1223.25f;
    m.squadSid = "44422-rebirth.mod";
    m.factionName = "Holy Nation Outlaws";      /* SPACES: the line is tab-separated for exactly this reason */
    m.worldId = "Holy Nation Outlaws_5";
    m.town = "The Hub";
    m.len = 33395LL;                            /* the one full record measured on this machine */
    m.crc = 0xCBF43926u;

    std::string line;
    if (!coopstore::MetaEncodeV5(m, &line)) { Fail("encode REFUSED a clean line"); return; }
    if (line.empty() || line[line.size() - 1] != '\n') Fail("the encoded line does not end in a newline");

    coopstore::MetaLine b;
    if (!coopstore::MetaParse(line, &b)) { Fail("parse refused the line just encoded: " + line); return; }
    if (b.version != 5)             Fail("version " + I(b.version) + ", want 5; line: " + line);
    if (b.writtenAt != m.writtenAt) Fail("writtenAt " + L64(b.writtenAt) + ", want " + L64(m.writtenAt));
    if (b.posAt != m.posAt)         Fail("posAt " + L64(b.posAt) + ", want " + L64(m.posAt));
    if (b.owner != m.owner)         Fail("owner " + I((int)b.owner) + ", want " + I((int)m.owner));
    if (b.x != m.x || b.y != m.y || b.z != m.z)
        Fail("position " + D(b.x) + "," + D(b.y) + "," + D(b.z) + ", want " + D(m.x) + "," + D(m.y) + "," + D(m.z));
    if (b.squadSid != m.squadSid)       Fail("squadSid '" + b.squadSid + "', want '" + m.squadSid + "'");
    if (b.factionName != m.factionName) Fail("factionName '" + b.factionName + "', want '" + m.factionName + "'");
    if (b.worldId != m.worldId)         Fail("worldId '" + b.worldId + "', want '" + m.worldId + "'");
    if (b.town != m.town)               Fail("town '" + b.town + "', want '" + m.town + "'");
    if (b.len != m.len)                 Fail("len " + L64(b.len) + ", want " + L64(m.len));
    if (b.crc != m.crc)                 Fail("crc " + H8(b.crc) + ", want " + H8(m.crc));

    /* The encoder ends the line with a newline; a line read back with getline carries none. BOTH must parse
       to the same record - the v4 test below is where this first went wrong, and it is asserted here too so a
       reader of this test does not have to find it there. */
    {
        std::string trimmed = line;
        if (!trimmed.empty() && trimmed[trimmed.size() - 1] == '\n') trimmed.erase(trimmed.size() - 1);
        if (trimmed.size() + 1 != line.size()) Fail("the encoded line did not end in exactly one newline");
        coopstore::MetaLine t;
        if (!coopstore::MetaParse(trimmed, &t)) { Fail("parse refused the line without its newline"); return; }
        if (t.town != m.town || t.crc != m.crc || t.len != m.len || t.worldId != m.worldId)
            Fail("a line parses differently with and without its trailing newline");
    }

    /* A tab or a newline inside a field would silently become a field or a line boundary. It is REFUSED. */
    std::string out2;
    { coopstore::MetaLine bad = m; bad.factionName = "Holy\tNation";
      if (coopstore::MetaEncodeV5(bad, &out2)) Fail("encode ACCEPTED a faction name carrying a TAB"); }
    { coopstore::MetaLine bad = m; bad.town = "The Hub\n";
      if (coopstore::MetaEncodeV5(bad, &out2)) Fail("encode ACCEPTED a town carrying a NEWLINE"); }
    { coopstore::MetaLine bad = m; bad.worldId = "";
      if (coopstore::MetaEncodeV5(bad, &out2)) Fail("encode ACCEPTED an empty world id"); }

    /* A v5 line that is missing its two new fields is refused rather than read as "length 0", which would
       turn a real record into a position-only note. */
    if (coopstore::MetaParse("v5\t1\t0\t0\t0\t0\tsid\tfac\tFac_1\t0\ttown\n", &b))
        Fail("a v5 line with 11 fields was ACCEPTED - it carries no length and no checksum");
    /* A tag this build does not know is refused, not guessed at. */
    if (coopstore::MetaParse("v9\t1\t0\t0\t0\t0\tsid\tfac\tFac_1\t0\ttown\t5\t00000000\n", &b))
        Fail("a v9 line was ACCEPTED");
}

void t_storemeta_v4_upgrades_to_v5()
{
    const std::string v4 =
        "v4\t900\t2\t10\t20\t30\tsquad sid\tTrade Ninjas\tTrade Ninjas_0\t950\tThe Hub\n";
    coopstore::MetaLine m;
    if (!coopstore::MetaParse(v4, &m)) { Fail("a v4 line was REFUSED: " + v4); return; }
    if (m.version != 4) Fail("version " + I(m.version) + ", want 4");
    if (m.len != 0 || m.crc != 0u)
        Fail("a v4 line carries no length and no checksum, got len " + L64(m.len) + " crc " + H8(m.crc));
    if (m.worldId != "Trade Ninjas_0") Fail("worldId '" + m.worldId + "', want 'Trade Ninjas_0'");
    if (m.town != "The Hub")           Fail("town '" + m.town + "', want 'The Hub'");

    /* THE UPGRADE, as the relay does it at start: length and checksum come from the payload FOUND ON DISK,
       and the line is rewritten as v5. The old file is trusted for that one upgrade because there is no way
       to tell a whole old file from a torn one (design-save 1.3). */
    static const char payload[] = "the squad bytes";
    const long long len = (long long)(sizeof(payload) - 1);
    m.len = len;
    m.crc = coopstore::Crc32(payload, (std::size_t)len);
    if (m.crc != 0x384ABA7Au)
        Fail("the fixture's own checksum is " + H8(m.crc) + ", want 0x384aba7a - the upgrade is being checked "
             "against a value this suite computed rather than one it knows");

    std::string v5;
    if (!coopstore::MetaEncodeV5(m, &v5)) { Fail("encode refused the upgraded line"); return; }
    if (v5.compare(0, 3, "v5\t") != 0) Fail("the upgraded line is not tagged v5: " + v5);

    coopstore::MetaLine b;
    if (!coopstore::MetaParse(v5, &b)) { Fail("the upgraded line does not parse: " + v5); return; }
    if (b.version != 5)                 Fail("version " + I(b.version) + ", want 5");
    if (b.writtenAt != 900LL)           Fail("writtenAt " + L64(b.writtenAt) + ", want 900");
    if (b.posAt != 950LL)               Fail("posAt " + L64(b.posAt) + ", want 950");
    if (b.owner != 2u)                  Fail("owner " + I((int)b.owner) + ", want 2");
    if (b.squadSid != "squad sid")      Fail("squadSid '" + b.squadSid + "', want 'squad sid'");
    if (b.factionName != "Trade Ninjas") Fail("factionName '" + b.factionName + "'");
    if (b.worldId != "Trade Ninjas_0")  Fail("worldId '" + b.worldId + "'");
    if (b.town != "The Hub")            Fail("town '" + b.town + "'");
    if (b.len != len)                   Fail("len " + L64(b.len) + ", want " + L64(len));
    if (b.crc != m.crc)                 Fail("crc " + H8(b.crc) + ", want " + H8(m.crc));
    /* and what it names now validates against the file it was computed from */
    if (coopstore::PayloadCheck(b.len, b.crc, coopstore::kFileReadable, len, m.crc) != coopstore::kPayloadOk)
        Fail("the upgraded line does not validate against its own payload");

    /* A v2-v4 line whose payload is MISSING keeps today's behaviour: it is still indexed, with no payload.
       That is len 0 in v5, and it must read back as "no payload expected" rather than as a missing file. */
    coopstore::MetaLine nofile = b; nofile.len = 0; nofile.crc = 0;
    if (coopstore::PayloadCheck(nofile.len, nofile.crc, coopstore::kFileAbsent, 0, 0) != coopstore::kPayloadNoneExpected)
        Fail("an upgraded line with no payload reads as "
             + std::string(VerdictName(coopstore::PayloadCheck(nofile.len, nofile.crc, coopstore::kFileAbsent, 0, 0)))
             + ", want kPayloadNoneExpected");
}

void t_store_refuses_a_truncated_payload()
{
    static const char whole[] = "0123456789";
    coopstore::FolderFacts f;
    f.haveMeta = 1;
    f.meta.version = 5; f.meta.worldId = "Trade Ninjas_0";
    f.meta.len = 10; f.meta.crc = coopstore::Crc32(whole, 10);
    f.payloadState = coopstore::kFileReadable; f.payloadLen = 6;               /* killed mid-write: six of ten bytes are on disk */
    f.payloadCrc = coopstore::Crc32(whole, 6);
    if (f.meta.len == f.payloadLen) { Fail("the fixture is not truncated - it proves nothing"); return; }

    coopstore::Choice c = coopstore::ChooseVersion(f);
    if (c.kind != coopstore::kChooseCurrentRefused)
        Fail(std::string("kind ") + ChoiceName(c.kind) + ", want kChooseCurrentRefused - a torn payload is never served");
    if (c.refusal != coopstore::kPayloadTorn)
        Fail(std::string("refusal ") + VerdictName(c.refusal) + ", want kPayloadTorn (6 bytes on disk, 10 in the line)");
    if (c.hasFile != 0) Fail("hasFile " + I(c.hasFile) + ", want 0 - the position is kept, the payload is not");

    /* A line that claims a payload with no file at all is a DIFFERENT refusal and is counted apart. */
    coopstore::FolderFacts g = f;
    g.payloadState = coopstore::kFileAbsent; g.payloadLen = 0; g.payloadCrc = 0;
    coopstore::Choice d = coopstore::ChooseVersion(g);
    if (d.refusal != coopstore::kPayloadMissing)
        Fail(std::string("refusal ") + VerdictName(d.refusal) + ", want kPayloadMissing");
    if (d.kind != coopstore::kChooseCurrentRefused)
        Fail(std::string("kind ") + ChoiceName(d.kind) + ", want kChooseCurrentRefused");
}

void t_store_refuses_a_crc_mismatch()
{
    static const char whole[] = "0123456789";
    static const char other[] = "9876543210";   /* SAME LENGTH, different bytes - what a length check cannot see */
    coopstore::FolderFacts f;
    f.haveMeta = 1;
    f.meta.version = 5; f.meta.worldId = "Drifters_12";
    f.meta.len = 10; f.meta.crc = coopstore::Crc32(whole, 10);
    f.payloadState = coopstore::kFileReadable; f.payloadLen = 10; f.payloadCrc = coopstore::Crc32(other, 10);
    if (f.meta.crc == f.payloadCrc)
    { Fail("the two fixtures share a CRC (" + H8(f.meta.crc) + ") - this test would pass vacuously"); return; }
    if (f.meta.len != f.payloadLen)
    { Fail("the fixtures differ in LENGTH - then the length check decides and the CRC is not exercised"); return; }

    coopstore::Choice c = coopstore::ChooseVersion(f);
    if (c.kind != coopstore::kChooseCurrentRefused)
        Fail(std::string("kind ") + ChoiceName(c.kind) + ", want kChooseCurrentRefused");
    if (c.refusal != coopstore::kPayloadCrcBad)
        Fail(std::string("refusal ") + VerdictName(c.refusal) + ", want kPayloadCrcBad");
}

void t_store_promotes_the_previous_version()
{
    static const char oldBytes[] = "the previous write";
    static const char newBytes[] = "the newest write, which is longer";
    const long long oldLen = (long long)(sizeof(oldBytes) - 1);
    const long long newLen = (long long)(sizeof(newBytes) - 1);

    coopstore::MetaLine oldMeta;
    oldMeta.version = 5; oldMeta.worldId = "Trade Ninjas_0"; oldMeta.writtenAt = 100;
    oldMeta.len = oldLen; oldMeta.crc = coopstore::Crc32(oldBytes, (std::size_t)oldLen);
    coopstore::MetaLine newMeta = oldMeta;
    newMeta.writtenAt = 200;
    newMeta.len = newLen; newMeta.crc = coopstore::Crc32(newBytes, (std::size_t)newLen);
    if (oldMeta.crc == newMeta.crc || oldLen == newLen)
    { Fail("the two versions are indistinguishable in the fixture"); return; }

    /* Window 1 - the swap never started; the current pair is whole and is what is served. */
    {
        coopstore::FolderFacts f;
        f.haveMeta = 1; f.meta = newMeta;
        f.payloadState = coopstore::kFileReadable; f.payloadLen = newLen; f.payloadCrc = newMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChooseCurrent || c.hasFile != 1)
            Fail(std::string("window 1: kind ") + ChoiceName(c.kind) + " hasFile " + I(c.hasFile) + ", want kChooseCurrent / 1");
    }
    /* Window 2 - crashed after the meta rename: no <id>.meta, and <id>.meta.prev describes the payload that
       is still sitting under the current name. */
    {
        coopstore::FolderFacts f;
        f.havePrevMeta = 1; f.prevMeta = oldMeta;
        f.payloadState = coopstore::kFileReadable; f.payloadLen = oldLen; f.payloadCrc = oldMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChoosePrevMetaCurrent || c.hasFile != 1)
            Fail(std::string("window 2: kind ") + ChoiceName(c.kind) + " hasFile " + I(c.hasFile) + ", want kChoosePrevMetaCurrent / 1");
    }
    /* Window 3 - crashed after both renames: neither current name exists, both .prev do. */
    {
        coopstore::FolderFacts f;
        f.havePrevMeta = 1; f.prevMeta = oldMeta;
        f.prevPayloadState = coopstore::kFileReadable; f.prevPayloadLen = oldLen; f.prevPayloadCrc = oldMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChoosePrevPair || c.hasFile != 1)
            Fail(std::string("window 3: kind ") + ChoiceName(c.kind) + " hasFile " + I(c.hasFile) + ", want kChoosePrevPair / 1");
    }
    /* Window 4 - the NEW payload landed and the meta never did. The previous LINE must not be paired with the
       new FILE (it names the wrong length), so the .prev pair is promoted and only the newest write is lost. */
    {
        coopstore::FolderFacts f;
        f.havePrevMeta = 1; f.prevMeta = oldMeta;
        f.payloadState = coopstore::kFileReadable; f.payloadLen = newLen; f.payloadCrc = newMeta.crc;
        f.prevPayloadState = coopstore::kFileReadable; f.prevPayloadLen = oldLen; f.prevPayloadCrc = oldMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChoosePrevPair)
            Fail(std::string("window 4: kind ") + ChoiceName(c.kind) + ", want kChoosePrevPair - the previous line "
                 "must never be served against the newer file");
    }
    /* A torn CURRENT pair with a whole previous one: promoted, and the refusal is still reported so the
       counter moves. A recovery that hid the damage would read as "nothing happened". */
    {
        coopstore::FolderFacts f;
        f.haveMeta = 1; f.meta = newMeta;
        f.payloadState = coopstore::kFileReadable; f.payloadLen = newLen - 1; f.payloadCrc = 0x1234u;
        f.havePrevMeta = 1; f.prevMeta = oldMeta;
        f.prevPayloadState = coopstore::kFileReadable; f.prevPayloadLen = oldLen; f.prevPayloadCrc = oldMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChoosePrevPair)
            Fail(std::string("torn current: kind ") + ChoiceName(c.kind) + ", want kChoosePrevPair");
        if (c.refusal != coopstore::kPayloadTorn)
            Fail(std::string("torn current: refusal ") + VerdictName(c.refusal) + ", want kPayloadTorn");
        if (c.hasFile != 1) Fail("torn current: hasFile " + I(c.hasFile) + ", want 1");
    }
    /* A .meta.prev that is a v4 line carries neither length nor checksum. It can validate nothing, so it is
       not promoted over a refusal it is unable to judge. */
    {
        coopstore::FolderFacts f;
        f.haveMeta = 1; f.meta = newMeta;
        f.payloadState = coopstore::kFileReadable; f.payloadLen = newLen - 1; f.payloadCrc = 0x1234u;
        coopstore::MetaLine v4 = oldMeta; v4.version = 4; v4.len = 0; v4.crc = 0;
        f.havePrevMeta = 1; f.prevMeta = v4;
        f.prevPayloadState = coopstore::kFileReadable; f.prevPayloadLen = oldLen; f.prevPayloadCrc = oldMeta.crc;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChooseCurrentRefused)
            Fail(std::string("v4 previous line: kind ") + ChoiceName(c.kind) + ", want kChooseCurrentRefused");
    }
    /* Nothing at all on disk for this id: nothing is indexed and nothing is invented. */
    {
        coopstore::FolderFacts f;
        coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind != coopstore::kChooseNone)
            Fail(std::string("empty folder: kind ") + ChoiceName(c.kind) + ", want kChooseNone");
    }
}

/* ---- P8c: THE UPGRADE OF A v2-v4 LINE IS A DECISION AND IT IS TESTED LIKE ONE (review-p8b C-1/H-1/H-2) --
 *
 * WHAT THESE CAN AND CANNOT SAY. UpgradeDecide and ChooseVersion touch no disk, so "the file is there and
 * could not be read" arrives here as a literal argument rather than as a locked file. That the RELAY hands
 * that argument the right value - an attribute query rather than a failed open - is not testable here and is
 * measured against a real folder with a real FILE_SHARE_NONE lock instead. Stated so nobody reads a green
 * suite as covering it.
 */
const char* UpgradeName(int k)
{
    switch (k)
    {
        case coopstore::kUpgradeDefer:        return "kUpgradeDefer";
        case coopstore::kUpgradePromotePrev:  return "kUpgradePromotePrev";
        case coopstore::kUpgradeFromPayload:  return "kUpgradeFromPayload";
        case coopstore::kUpgradePositionOnly: return "kUpgradePositionOnly";
        default:                              return "??";
    }
}

/* The v2-v4 line, a validated previous pair beside it, and a payload under the current name that is neither.
   Built once and handed to the three tests below, each of which changes exactly one thing. */
static const char kOldBytes[] = "the previous write";
static const char kTornBytes[] = "the p";

void MakeV4Facts(coopstore::FolderFacts* f)
{
    const long long oldLen = (long long)(sizeof(kOldBytes) - 1);
    coopstore::MetaLine v4;
    v4.version = 4; v4.worldId = "Trade Ninjas_0"; v4.writtenAt = 2000; v4.posAt = 2000;
    v4.len = 0; v4.crc = 0;                       /* a v4 line carries NEITHER - that is the whole problem */
    coopstore::MetaLine prev;
    prev.version = 5; prev.worldId = "Trade Ninjas_0"; prev.writtenAt = 1000; prev.posAt = 1000;
    prev.len = oldLen; prev.crc = coopstore::Crc32(kOldBytes, (std::size_t)oldLen);
    f->haveMeta = 1; f->meta = v4;
    f->havePrevMeta = 1; f->prevMeta = prev;
    f->payloadState = coopstore::kFileReadable;                            /* the TORN five bytes, under the current name */
    f->payloadLen = (long long)(sizeof(kTornBytes) - 1);
    f->payloadCrc = coopstore::Crc32(kTornBytes, sizeof(kTornBytes) - 1);
    f->prevPayloadState = coopstore::kFileReadable; f->prevPayloadLen = oldLen; f->prevPayloadCrc = prev.crc;
}

void t_store_upgrade_defers_an_unreadable_payload()
{
    coopstore::FolderFacts f;
    MakeV4Facts(&f);
    /* THE CASE THAT COST A CRITICAL. The payload is on disk and could not be read for a moment. Whatever
       else is true of the folder, the answer is: write NOTHING and look again at the next start. */
    f.payloadState = coopstore::kFileUnreadable; f.payloadLen = 0; f.payloadCrc = 0;   /* nothing was read... */
    coopstore::UpgradeChoice u = coopstore::UpgradeDecide(f);
    if (u.kind != coopstore::kUpgradeDefer)
        Fail(std::string("an UNREADABLE payload gave ") + UpgradeName(u.kind) + ", want kUpgradeDefer - a v4"
             " line rewritten as len 0 is a squad lost, and a v4 line left alone is upgraded at any later start");

    /* ...and the SAME facts with the payload genuinely ABSENT must NOT defer, or this test would pass for a
       function that ignored its first argument entirely. */
    coopstore::FolderFacts fa = f; fa.payloadState = coopstore::kFileAbsent;
    coopstore::UpgradeChoice a = coopstore::UpgradeDecide(fa);
    if (a.kind == coopstore::kUpgradeDefer)
        Fail("an ABSENT payload also deferred - UpgradeDecide is not reading its payloadState argument");

    /* With no previous version at all, absent means a position-only line and nothing more. */
    coopstore::FolderFacts g;
    g.haveMeta = 1; g.meta.version = 4; g.meta.worldId = "Trade Ninjas_0";
    coopstore::UpgradeChoice p = coopstore::UpgradeDecide(g);   /* g.payloadState defaults to kFileAbsent */
    if (p.kind != coopstore::kUpgradePositionOnly)
        Fail(std::string("an absent payload with no previous version gave ") + UpgradeName(p.kind)
             + ", want kUpgradePositionOnly");
    coopstore::FolderFacts gu = g; gu.payloadState = coopstore::kFileUnreadable;
    coopstore::UpgradeChoice q = coopstore::UpgradeDecide(gu);
    if (q.kind != coopstore::kUpgradeDefer)
        Fail(std::string("an unreadable payload with no previous version gave ") + UpgradeName(q.kind)
             + ", want kUpgradeDefer - there is nothing to fall back on, so there is even less reason to write");
}

void t_store_upgrade_promotes_a_prev_pair_over_a_torn_payload()
{
    coopstore::FolderFacts f;
    MakeV4Facts(&f);
    if (f.payloadLen == f.prevPayloadLen)
    { Fail("the fixture's torn payload is the same length as the whole one - it proves nothing"); return; }

    /* H-2: the bytes under the current name are five of eighteen, and a WHOLE, CHECKSUMMED previous pair is
       sitting beside them. P8b checksummed the five bytes and wrote them into the index as truth. */
    coopstore::UpgradeChoice u = coopstore::UpgradeDecide(f);   /* f.payloadState is kFileReadable, five bytes */
    if (u.kind != coopstore::kUpgradePromotePrev)
        Fail(std::string("a v4 line over a torn payload with a valid previous pair gave ") + UpgradeName(u.kind)
             + ", want kUpgradePromotePrev");
    if (u.promote.kind != coopstore::kChoosePrevPair)
        Fail(std::string("the promotion is ") + ChoiceName(u.promote.kind) + ", want kChoosePrevPair");
    if (u.promote.hasFile != 1)
        Fail("the promoted pair has no payload - the previous version's squad is the thing being recovered");

    /* THE NARROWING, and it is the difference between a repair and a rollback. When the previous LINE
       validates against the file under the CURRENT name, that file is the one the line describes: nothing is
       torn, and promoting would throw away the newer v4 line's position for no gain. Upgrade from the file. */
    coopstore::FolderFacts g;
    MakeV4Facts(&g);
    g.payloadLen = g.prevPayloadLen; g.payloadCrc = g.prevPayloadCrc;   /* the current file IS the prev one */
    coopstore::UpgradeChoice v = coopstore::UpgradeDecide(g);
    if (v.kind != coopstore::kUpgradeFromPayload)
        Fail(std::string("a v4 line over the very file its previous line describes gave ") + UpgradeName(v.kind)
             + ", want kUpgradeFromPayload - promoting there would roll the position back one version for nothing");

    /* And a v4 line with a whole payload and no previous version at all is the ordinary migration. */
    coopstore::FolderFacts h;
    h.haveMeta = 1; h.meta.version = 4; h.meta.worldId = "Trade Ninjas_0";
    h.payloadState = coopstore::kFileReadable; h.payloadLen = 10; h.payloadCrc = 0x1234u;
    coopstore::UpgradeChoice w = coopstore::UpgradeDecide(h);
    if (w.kind != coopstore::kUpgradeFromPayload)
        Fail(std::string("the ordinary migration gave ") + UpgradeName(w.kind) + ", want kUpgradeFromPayload");
}

void t_store_pass2_keeps_the_position_instead_of_dropping_the_record()
{
    /* review-p8b M-1 + H-1. No <id>.meta at all; the previous line is v5 and names a payload that is not
       there. Pass 1 keeps such a record on its position (kChooseCurrentRefused); pass 2 dropped it, and
       moved no counter while doing so, because the refusal was only ever recorded in the current-line arm. */
    coopstore::FolderFacts f;
    f.havePrevMeta = 1;
    f.prevMeta.version = 5; f.prevMeta.worldId = "Trade Ninjas_0";
    f.prevMeta.len = 18; f.prevMeta.crc = 0xABCDEF01u;
    if (f.haveMeta != 0) { Fail("the fixture has a current meta line - this is not the pass-2 shape"); return; }

    coopstore::Choice c = coopstore::ChooseVersion(f);
    if (c.kind != coopstore::kChoosePrevRefused)
        Fail(std::string("kind ") + ChoiceName(c.kind) + ", want kChoosePrevRefused - dropping it loses the group");
    if (c.hasFile != 0) Fail("hasFile " + I(c.hasFile) + ", want 0 - the position is kept, the squad is not");
    if (c.refusal != coopstore::kPayloadMissing)
        Fail(std::string("refusal ") + VerdictName(c.refusal) + ", want kPayloadMissing - a drop that moves no"
             " counter reads as nothing having happened");

    /* A torn previous payload is the same shape with a different refusal. */
    coopstore::FolderFacts g = f;
    g.prevPayloadState = coopstore::kFileReadable; g.prevPayloadLen = 5; g.prevPayloadCrc = 0x1111u;
    coopstore::Choice d = coopstore::ChooseVersion(g);
    if (d.kind != coopstore::kChoosePrevRefused)
        Fail(std::string("torn previous payload: kind ") + ChoiceName(d.kind) + ", want kChoosePrevRefused");
    if (d.refusal != coopstore::kPayloadTorn)
        Fail(std::string("torn previous payload: refusal ") + VerdictName(d.refusal) + ", want kPayloadTorn");

    /* Nothing at all is still nothing: a record with no line of any version is not invented. */
    coopstore::FolderFacts e;
    if (coopstore::ChooseVersion(e).kind != coopstore::kChooseNone)
        Fail("an empty folder no longer returns kChooseNone");
    /* A v4 previous line carries no length and no checksum, so it cannot be the basis of an indexed
       position either - it goes down the upgrade path in the caller instead. */
    coopstore::FolderFacts v4only;
    v4only.havePrevMeta = 1; v4only.prevMeta.version = 4; v4only.prevMeta.worldId = "Trade Ninjas_0";
    if (coopstore::ChooseVersion(v4only).kind != coopstore::kChooseNone)
        Fail("a v4-only previous line was given a version to serve");
}

void t_store_an_empty_or_damaged_meta_line_is_refused_not_guessed()
{
    /* The two shapes review-p8b measured as SILENT drops. What is asserted here is that the parser refuses
       them rather than inventing a record; that the relay COUNTS the refusal (metaEmpty / metaUnparsable) is
       a property of LoadIndex and is measured against a real folder, not here. */
    coopstore::MetaLine m;
    if (coopstore::MetaParse("", &m))                Fail("an EMPTY line was parsed as a record");
    if (coopstore::MetaParse(std::string(64, '\0'), &m)) Fail("a zero-filled line was parsed as a record");
    if (coopstore::MetaParse("v5", &m))              Fail("a one-field line was parsed as a record");
    if (coopstore::MetaParse("v4\t900\t2\t10\t20\t30\tsid\tfac\t\t950\ttown", &m))
        Fail("a line with an EMPTY world id was parsed as a record - it would index under no name at all");
    /* ...and the same shape WITH an id parses, so the four refusals above are not vacuous. */
    if (!coopstore::MetaParse("v4\t900\t2\t10\t20\t30\tsid\tfac\tFac_1\t950\ttown", &m))
        Fail("a well-formed v4 line was refused - the refusals above prove nothing");
}

/* ---- P8d: EVERY SHAPE A RECORD'S FOLDER CAN BE IN, GENERATED (review-p8c C-1 / C-2 / H-1) -----------
 *
 * The tests above are hand-written cases. This one is a SWEEP. A record has four files - <id>.meta,
 * <id>.platoon, <id>.meta.prev, <id>.platoon.prev. A PAYLOAD file is put into one of five shapes -
 * intact, torn, absent, zero-length, unreadable - and a META file into one of SEVEN: those five plus a
 * POSITION-ONLY v5 line (len 0) and a v4 line. That is 7 x 5 x 7 x 5 = 1225 folders, and ALL 1225 ARE
 * SWEPT - none is excluded, because the four files exist independently of one another (a crash, a lock,
 * a scanner and a power cut are four different agents and can be in the folder at the same time), and
 * every one of the shapes has been found in this folder for real.
 *
 * P8l (review-p8d M-1) ADDED THE LAST TWO META SHAPES, and they are not decoration. Every meta line in
 * the P8d sweep CLAIMED a payload, so both "the line says there is no payload" arms of ChooseVersion
 * were unreachable in all 625 shapes; and every line parsed as v5, so the final `return` - reached only
 * when the only line on disk is a v4 one - was unreachable too. Three of the twelve return sites could
 * not be exercised at all, which is why the coverage leg below counts SITES.
 *
 * WHAT IS ASSERTED IS NOT A TABLE OF EXPECTED ANSWERS - that would be a second copy of ChooseVersion,
 * kept in step with the first by hand, which is the defect class this whole effort is about. Three
 * PROPERTIES are computed from the fixture instead:
 *
 *   (a) IF A COMMITTED VERSION IS ON DISK, IT IS RECOVERED. A committed version is a (line, payload)
 *       pair whose length and checksum agree, among the three design-save 1.2(d) allows: <id>.meta
 *       against <id>.platoon, <id>.meta.prev against <id>.platoon, and <id>.meta.prev against
 *       <id>.platoon.prev. When one exists and nothing is unreadable, the decision must serve a squad.
 *       The agreement test is written out in longhand and does NOT call PayloadCheck, so the property
 *       is independent of the function under test.
 *   (b) AN UNREADABLE FILE NEVER FEEDS A WRITE. Two kinds make the caller write: kChoosePrevMetaCurrent
 *       rewrites <id>.meta from the previous line, and kChoosePrevPair also RENAMES <id>.platoon.prev
 *       onto <id>.platoon. Either may be chosen only when every file it reads or touches was readable.
 *       review-p8c C-2 and H-1 are both failures of exactly this property, measured on a real folder.
 *   (c) THE DECISION CONVERGES IN ONE MORE PASS. What the caller would do on disk is applied to the
 *       fixture and the decision is taken again: after a promotion it must be kChooseCurrent, and after
 *       a refusal - which writes nothing - it must be the same refusal. A recovery that has to run
 *       twice is a recovery that did not finish.
 *
 * And the sweep states its own coverage: every one of ChooseVersion's TWELVE RETURN SITES must be
 * reached, and all six ChoiceKinds produced, or the shapes are not exercising the decision they claim
 * to and (a) and (b) pass vacuously.
 *
 * P8l (review-p8d M-1): THE COVERAGE LEG USED TO COUNT ChoiceKinds, AND THAT IS WHY IT COULD NOT SEE
 * THE REPAIR IT EXISTED FOR. Two returns produce kChoosePrevRefused and two produce kChooseNone, so
 * making one of them unreachable left the other one's hits standing and the leg passed. Worse, ARM TWO
 * - "a CURRENT payload that is unreadable can never be promoted over", the headline repair of P8d -
 * could be DELETED ENTIRELY and not one of the 625 answers changed, because the pass-2 guard catches
 * the same folder from the other side with the same Choice. Counting the return site makes deleting any
 * arm a test failure, which is the only thing that makes the sweep a guard rather than a description.
 * A failure prints the four shapes by name, so the folder that broke it can be staged by hand.
 */
enum { kShapeIntact = 0, kShapeTorn = 1, kShapeAbsent = 2, kShapeZero = 3, kShapeUnreadable = 4,
       kShapePositionOnly = 5, kShapeV4 = 6 };
enum { kPayloadShapes = 5, kMetaShapes = 7 };   /* the last two are META-only: they are properties of a LINE */
const char* ShapeName(int sh)
{
    switch (sh)
    {
        case kShapeIntact:       return "intact";
        case kShapeTorn:         return "torn";
        case kShapeAbsent:       return "absent";
        case kShapeZero:         return "zero-length";
        case kShapeUnreadable:   return "unreadable";
        case kShapePositionOnly: return "position-only(v5 len 0)";
        case kShapeV4:           return "v4 line";
        default:                 return "??";
    }
}
/* P8l: the twelve return sites of ChooseVersion, by name, so a coverage failure says WHICH arm nothing
   reached rather than printing an index. */
const char* SiteName(int t)
{
    switch (t)
    {
        case coopstore::kSiteMetaUnreadablePrevIndexed: return "kSiteMetaUnreadablePrevIndexed";
        case coopstore::kSiteMetaUnreadableNoPrev:      return "kSiteMetaUnreadableNoPrev";
        case coopstore::kSiteCurrentOk:                 return "kSiteCurrentOk";
        case coopstore::kSiteCurrentNoPayloadExpected:  return "kSiteCurrentNoPayloadExpected";
        case coopstore::kSiteCurrentPayloadUnreadable:  return "kSiteCurrentPayloadUnreadable (ARM TWO)";
        case coopstore::kSiteNoLineAtAll:               return "kSiteNoLineAtAll";
        case coopstore::kSitePrevMetaCurrentOk:         return "kSitePrevMetaCurrentOk";
        case coopstore::kSitePrevMetaCurrentNoPayload:  return "kSitePrevMetaCurrentNoPayload";
        case coopstore::kSitePrevPairOk:                return "kSitePrevPairOk";
        case coopstore::kSiteCurrentRefused:            return "kSiteCurrentRefused";
        case coopstore::kSitePrevRefused:               return "kSitePrevRefused";
        case coopstore::kSiteNothingValidates:          return "kSiteNothingValidates";
        default:                                        return "??";
    }
}
static const char kSweepWhole[] = "eighteen bytes ok!";   /* 18 */
static const char kSweepTorn[]  = "eight";                /*  5 */
/* A meta file: `intact` carries a v5 line describing the WHOLE payload, `torn` one describing the FIVE-byte
   one. `zero-length` is on disk and has no first line, so no line parses out of it - which is a different
   fact from `absent` and from `unreadable`, and telling those three apart is the whole point. */
void ApplyMetaShape(int sh, int* state, int* have, coopstore::MetaLine* m,
                    long long describeLen, unsigned int describeCrc)
{
    *have = 0;
    if (sh == kShapeIntact || sh == kShapeTorn)
    {
        *state = coopstore::kFileReadable; *have = 1;
        m->version = 5; m->worldId = "Trade Ninjas_0"; m->writtenAt = 100; m->posAt = 100;
        m->len = describeLen; m->crc = describeCrc;
        return;
    }
    /* P8l: a v5 line that says THIS RECORD HAS NO SQUAD. It is a committed statement, not damage, and it
       is the only way to reach either of the "no payload expected" arms. */
    if (sh == kShapePositionOnly)
    {
        *state = coopstore::kFileReadable; *have = 1;
        m->version = 5; m->worldId = "Trade Ninjas_0"; m->writtenAt = 100; m->posAt = 100;
        m->len = 0; m->crc = 0;
        return;
    }
    /* P8l: a v4 line - what UpgradeDecide feeds ChooseVersion for every folder written before P8b. It can
       validate nothing (v4 carries no len and no crc), and as a <id>.meta.prev it is the only route to the
       last return in the function. */
    if (sh == kShapeV4)
    {
        *state = coopstore::kFileReadable; *have = 1;
        m->version = 4; m->worldId = "Trade Ninjas_0"; m->writtenAt = 100; m->posAt = 100;
        m->len = 0; m->crc = 0;
        return;
    }
    if (sh == kShapeZero)   { *state = coopstore::kFileReadable;   return; }
    if (sh == kShapeAbsent) { *state = coopstore::kFileAbsent;     return; }
    *state = coopstore::kFileUnreadable;
}
void ApplyPayloadShape(int sh, int* state, long long* len, unsigned int* crc,
                       long long wholeLen, unsigned int wholeCrc, long long tornLen, unsigned int tornCrc)
{
    *len = 0; *crc = 0;
    if (sh == kShapeIntact) { *state = coopstore::kFileReadable; *len = wholeLen; *crc = wholeCrc; return; }
    if (sh == kShapeTorn)   { *state = coopstore::kFileReadable; *len = tornLen;  *crc = tornCrc;  return; }
    if (sh == kShapeZero)   { *state = coopstore::kFileReadable; return; }   /* ON DISK WITH NO BYTES IN IT */
    if (sh == kShapeAbsent) { *state = coopstore::kFileAbsent; return; }
    *state = coopstore::kFileUnreadable;
}
/* Longhand, and deliberately NOT a call to PayloadCheck: a property computed with the function under test
   proves only that the function agrees with itself. Every meta line in this sweep claims a payload
   (len > 0), so "no payload expected" never arises here; a zero-length or missing payload agrees with
   nothing, and an unreadable one is not a claim at all. */
int PairAgrees(int metaShape, int payloadShape)
{
    if (metaShape != kShapeIntact && metaShape != kShapeTorn) return 0;
    if (payloadShape != kShapeIntact && payloadShape != kShapeTorn) return 0;
    return metaShape == payloadShape ? 1 : 0;
}
void t_store_every_folder_shape_keeps_every_committed_version()
{
    const long long wholeLen = (long long)(sizeof(kSweepWhole) - 1);
    const long long tornLen  = (long long)(sizeof(kSweepTorn) - 1);
    const unsigned int wholeCrc = coopstore::Crc32(kSweepWhole, (std::size_t)wholeLen);
    const unsigned int tornCrc  = coopstore::Crc32(kSweepTorn,  (std::size_t)tornLen);
    if (wholeLen == tornLen || wholeCrc == tornCrc)
    { Fail("the two payload fixtures are not distinguishable - every shape would pass vacuously"); return; }

    int hit[6]; int i;
    for (i = 0; i < 6; ++i) hit[i] = 0;
    int hitSite[coopstore::kChoiceSiteCount];
    for (i = 0; i < coopstore::kChoiceSiteCount; ++i) hitSite[i] = 0;
    int shapes = 0;
    int a, b, cc, d;
    for (a = 0; a < kMetaShapes; ++a)
    for (b = 0; b < kPayloadShapes; ++b)
    for (cc = 0; cc < kMetaShapes; ++cc)
    for (d = 0; d < kPayloadShapes; ++d)
    {
        coopstore::FolderFacts f;
        ApplyMetaShape(a, &f.metaState, &f.haveMeta, &f.meta,
                       a == kShapeTorn ? tornLen : wholeLen, a == kShapeTorn ? tornCrc : wholeCrc);
        ApplyPayloadShape(b, &f.payloadState, &f.payloadLen, &f.payloadCrc,
                          wholeLen, wholeCrc, tornLen, tornCrc);
        ApplyMetaShape(cc, &f.prevMetaState, &f.havePrevMeta, &f.prevMeta,
                       cc == kShapeTorn ? tornLen : wholeLen, cc == kShapeTorn ? tornCrc : wholeCrc);
        ApplyPayloadShape(d, &f.prevPayloadState, &f.prevPayloadLen, &f.prevPayloadCrc,
                          wholeLen, wholeCrc, tornLen, tornCrc);
        ++shapes;

        const std::string where = std::string("meta=") + ShapeName(a) + " payload=" + ShapeName(b)
                                + " meta.prev=" + ShapeName(cc) + " payload.prev=" + ShapeName(d);
        const coopstore::Choice c = coopstore::ChooseVersion(f);
        if (c.kind < 0 || c.kind > 5) { Fail(where + ": kind " + I(c.kind) + " is not a ChoiceKind"); return; }
        ++hit[c.kind];
        /* P8l: -1 is what Choice() leaves behind, so an arm added without tagging itself fails HERE rather
           than quietly counting as one of the existing ones. */
        if (c.site < 0 || c.site >= coopstore::kChoiceSiteCount)
        { Fail(where + ": the Choice carries site " + I(c.site) + " - some `return` in ChooseVersion did not tag itself"); return; }
        ++hitSite[c.site];

        const int anyUnreadable = (f.metaState        == coopstore::kFileUnreadable ||
                                   f.payloadState     == coopstore::kFileUnreadable ||
                                   f.prevMetaState    == coopstore::kFileUnreadable ||
                                   f.prevPayloadState == coopstore::kFileUnreadable) ? 1 : 0;

        /* (a) - P8l scopes it away from a POSITION-ONLY current line. Such a line is a committed statement
           that this group has no squad, made later than anything a .prev pair describes, so serving no
           squad for it is the CORRECT answer and not a loss. Every other meta shape is claimed as before. */
        if (!anyUnreadable && a != kShapePositionOnly &&
            (PairAgrees(a, b) || PairAgrees(cc, b) || PairAgrees(cc, d)))
        {
            if (c.hasFile != 1 ||
                (c.kind != coopstore::kChooseCurrent &&
                 c.kind != coopstore::kChoosePrevMetaCurrent &&
                 c.kind != coopstore::kChoosePrevPair))
                Fail("(a) " + where + ": a committed version is on disk and the answer was "
                     + ChoiceName(c.kind) + " hasFile " + I(c.hasFile)
                     + " - a squad that can be recovered must be recovered");
        }

        /* (b) */
        /* P8l: the payload must be READABLE when the promoted line claims one, and merely not-unreadable
           when it does not - a position-only previous line validates against an absent payload, and
           nothing is renamed on this arm either way. */
        if (c.kind == coopstore::kChoosePrevMetaCurrent &&
            !(f.metaState != coopstore::kFileUnreadable &&
              f.prevMetaState == coopstore::kFileReadable &&
              f.payloadState != coopstore::kFileUnreadable &&
              (c.hasFile == 0 || f.payloadState == coopstore::kFileReadable)))
            Fail("(b) " + where + ": kChoosePrevMetaCurrent REWRITES <id>.meta from a line validated against"
                 " <id>.platoon, and one of those three files was not readable");
        if (c.kind == coopstore::kChoosePrevPair &&
            !(f.metaState != coopstore::kFileUnreadable &&
              f.payloadState != coopstore::kFileUnreadable &&
              f.prevMetaState == coopstore::kFileReadable &&
              f.prevPayloadState == coopstore::kFileReadable))
            Fail("(b) " + where + ": kChoosePrevPair RENAMES <id>.platoon.prev ONTO <id>.platoon and rewrites"
                 " <id>.meta, and one of those four files was on disk and unreadable");

        /* (c) - what the caller would have left on disk, decided again. */
        coopstore::FolderFacts g = f;
        if (c.kind == coopstore::kChoosePrevMetaCurrent)
        { g.metaState = coopstore::kFileReadable; g.haveMeta = 1; g.meta = f.prevMeta; }
        else if (c.kind == coopstore::kChoosePrevPair)
        {
            g.metaState = coopstore::kFileReadable; g.haveMeta = 1; g.meta = f.prevMeta;
            g.payloadState = f.prevPayloadState; g.payloadLen = f.prevPayloadLen; g.payloadCrc = f.prevPayloadCrc;
            g.prevPayloadState = coopstore::kFileAbsent; g.prevPayloadLen = 0; g.prevPayloadCrc = 0;
        }
        const coopstore::Choice c2 = coopstore::ChooseVersion(g);
        if (c.kind == coopstore::kChoosePrevMetaCurrent || c.kind == coopstore::kChoosePrevPair)
        {
            /* P8l: the second pass must AGREE WITH THE FIRST about hasFile rather than always saying 1 - a
               promoted position-only line legitimately carries no squad, and hard-coding 1 would have made
               that shape unreachable rather than tested. */
            if (c2.kind != coopstore::kChooseCurrent || c2.hasFile != c.hasFile)
                Fail("(c) " + where + ": after the caller carried out " + ChoiceName(c.kind)
                     + " the second pass says " + ChoiceName(c2.kind) + " hasFile " + I(c2.hasFile)
                     + ", want kChooseCurrent / " + I(c.hasFile)
                     + " - a recovery that has to run twice did not finish");
        }
        else if (c2.kind != c.kind || c2.hasFile != c.hasFile)
            Fail("(c) " + where + ": nothing was written, and the second pass changed its mind from "
                 + ChoiceName(c.kind) + " to " + ChoiceName(c2.kind));
    }

    if (shapes != 1225) Fail("the sweep covered " + I(shapes) + " shapes, want 1225 (7 meta shapes x 5 payload shapes, twice)");
    for (i = 0; i < 6; ++i)
        if (hit[i] == 0)
            Fail(std::string("the sweep never produced ") + ChoiceName(i) + " - it is not"
                 " exercising the decision it claims to, and (a) and (b) above pass vacuously");
    /* P8l (review-p8d M-1) - THE LEG THAT MAKES DELETING AN ARM A FAILURE. */
    for (i = 0; i < coopstore::kChoiceSiteCount; ++i)
        if (hitSite[i] == 0)
            Fail(std::string("no folder shape reached ") + SiteName(i) + " - that arm of ChooseVersion is"
                 " either unreachable or gone, and nothing else in this suite would notice either");
}
/* P8l (review-p8d M-1's second half): ARM TWO, NAMED. The sweep now fails if the arm is deleted, and this
   says in one place WHAT it does, so a reader of the suite finds the C-2 repair by looking for it. A v5 line
   claiming a payload, that payload ON DISK AND UNREADABLE, and a .prev pair that validates perfectly beside
   it: the previous version must NOT be promoted, because a payload we cannot read is a payload we cannot
   rule out - and P8c promoted it, renamed the older copy over the intact one at the next start, and reported
   both starts as successful recoveries (review-p8c C-2, measured on a real folder). */
void t_store_an_unreadable_current_payload_is_never_promoted_over()
{
    const char whole[] = "eighteen bytes ok!";
    const long long wholeLen = (long long)(sizeof(whole) - 1);
    const unsigned int wholeCrc = coopstore::Crc32(whole, (std::size_t)wholeLen);

    coopstore::FolderFacts f;
    f.metaState = coopstore::kFileReadable; f.haveMeta = 1;
    f.meta.version = 5; f.meta.worldId = "Trade Ninjas_0"; f.meta.writtenAt = 200; f.meta.posAt = 200;
    f.meta.len = wholeLen; f.meta.crc = wholeCrc;
    f.payloadState = coopstore::kFileUnreadable;                       /* locked, NOT missing */
    f.prevMetaState = coopstore::kFileReadable; f.havePrevMeta = 1;
    f.prevMeta = f.meta; f.prevMeta.writtenAt = 100; f.prevMeta.posAt = 100;
    f.prevPayloadState = coopstore::kFileReadable; f.prevPayloadLen = wholeLen; f.prevPayloadCrc = wholeCrc;

    const coopstore::Choice c = coopstore::ChooseVersion(f);
    if (c.kind != coopstore::kChooseCurrentRefused || c.hasFile != 0)
        Fail(std::string("kind ") + ChoiceName(c.kind) + " hasFile " + I(c.hasFile)
             + ", want kChooseCurrentRefused / 0 - a .prev pair was promoted over a payload that is merely LOCKED");
    if (c.refusal != coopstore::kPayloadUnreadable)
        Fail("the refusal is " + I(c.refusal) + ", want kPayloadUnreadable - an unreadable payload counted as something else");
    if (c.site != coopstore::kSiteCurrentPayloadUnreadable)
        Fail(std::string("the answer came from ") + SiteName(c.site) + ", want kSiteCurrentPayloadUnreadable"
             " - arm two is gone and another arm is producing the same Choice, which is exactly what made"
             " deleting it invisible (review-p8d M-1)");
}
/* P8l (review-p8d C-1): THE MERGE THAT RUNS WHEN uniques.txt IS FINALLY READ. The DEAD-IS-TERMINAL guard
   lives on the stored map, and while the load was deferred the stored map was EMPTY - so the guard could
   not fire and a character recorded permanently dead came back alive on disk, from one ordinary message.
   Here it is applied against the map that was actually on the file, which is the only map it ever meant. */
void t_uniques_merge_after_a_deferred_load_refuses_a_revival()
{
    /* the file records this character DEAD; a state 2 arrived while the file could not be read */
    if (coopmerge::UniqueMergeDecide(1, 0, 2) != coopmerge::kMergeKeepLoaded)
        Fail("a DEAD character was revived by a state accepted while uniques.txt was unreadable - review-p8d C-1");
    if (coopmerge::UniqueMergeDecide(1, 0, 1) != coopmerge::kMergeKeepLoaded)
        Fail("state 1 over a stored DEAD row was taken - DEAD IS TERMINAL whatever the new state is");
    /* DEAD arriving over a live stored row is the one direction that IS allowed */
    if (coopmerge::UniqueMergeDecide(1, 2, 0) != coopmerge::kMergeTakeDeferred)
        Fail("a character reported DEAD while the file was unreadable was not recorded dead");
    if (coopmerge::UniqueMergeDecide(1, 1, 2) != coopmerge::kMergeTakeDeferred)
        Fail("an ordinary newer state lost to the row on disk - the deferred change is the newer one");
    /* a character the file has never heard of */
    if (coopmerge::UniqueMergeDecide(0, 0, 1) != coopmerge::kMergeInsertDeferred)
        Fail("a character with no row on disk was not inserted");
    if (coopmerge::UniqueMergeDecide(0, 0, 0) != coopmerge::kMergeInsertDeferred)
        Fail("a DEAD report for a character with no row on disk was not inserted - the loadedState argument"
             " is being read when there is no loaded row");
}
/* P8l (review-p8d H-2): the world's clock when clock.txt is read at last and a game has seeded a time in
   the meantime. Decision 46: this world's clock only ever moves FORWARD. */
void t_clock_merge_after_a_deferred_load_never_moves_backward()
{
    if (coopmerge::ClockMergeHours(5000.0, 4000.0) != 5000.0)
        Fail("the file's later recorded time lost to a joining game's earlier one - review-p8d H-2's loss");
    if (coopmerge::ClockMergeHours(4000.0, 5000.0) != 5000.0)
        Fail("the running clock was rolled BACK to the file's older time");
    if (coopmerge::ClockMergeHours(-1.0, 5000.0) != 5000.0)
        Fail("a file with no usable time erased the clock this session is running");
    if (coopmerge::ClockMergeHours(5000.0, -1.0) != 5000.0)
        Fail("this world's recorded time was not restored when memory had none");
    if (coopmerge::ClockMergeHours(-1.0, -1.0) >= 0.0)
        Fail("a world with no time anywhere was given one");
}

/* ------------------------------------------------------------------------------------------------
   p97_leaf_nomap_refuses_every_slot - P97 (p97-test, owner 334 a / 337 a).
   The leaf gate's linked arm with no fresh area map (MayInventFromView's -1) refuses on EVERY game.
   The retired fallback let slot 0 generate once 20 s had passed.  The slots and waits it used are
   swept below; LeafLinked takes neither, so the sweep can only fail if one is wired back in through
   a changed signature - which this call would then have to change too.  The player's own faction
   still passes a refusal (T-230).
   ------------------------------------------------------------------------------------------------ */
void t_p97_leaf_nomap_refuses_every_slot()
{
    const int slots[3] = { 0, 1, -1 };
    const double waited[4] = { 0.0, 19.9, 20.0, 600.0 };
    for (int si = 0; si < 3; ++si)
        for (int wi = 0; wi < 4; ++wi)
        {
            const std::string at = " at slot " + I(slots[si]) + " after " + D(waited[wi]) + " s with no map";
            const int a = coopor::LeafLinked(-1);
            if (a != coopor::kLeafLinkedNoMap) Fail("p97 leaf:" + at + " the gate answered " + I(a) + ", want refuse (no-map)");
            const int gated = (a != coopor::kLeafLinkedAllow) ? 1 : 0;
            if (coopor::LeafCreationRefused(gated, 0) != 1) Fail("p97 leaf:" + at + " a non-player creation was let through");
            if (coopor::LeafCreationRefused(gated, 1) != 0) Fail("p97 leaf:" + at + " the player's own faction was refused");
        }
    if (coopor::LeafLinked(1) != coopor::kLeafLinkedAllow) Fail("p97 leaf: the holder (may 1) was refused");
    if (coopor::LeafLinked(0) != coopor::kLeafLinkedNotHere) Fail("p97 leaf: may 0 was not answered not-here");
    if (coopor::LeafCreationRefused(0, 0) != 0) Fail("p97 leaf: an allowed creation was refused");
}

/* ------------------------------------------------------------------------------------------------
   p97_announce_nomap_refuses_every_slot - P97.  The announce sweep's area answer: no fresh map is
   "skip" on every game (the retired arm let slot 0 adopt after 20 s); an area the map answered and
   no other game holds is adoptable; another game's area is other-held.
   ------------------------------------------------------------------------------------------------ */
void t_p97_announce_nomap_refuses_every_slot()
{
    const int slots[3] = { 0, 1, -1 };
    for (int si = 0; si < 3; ++si)
    {
        const int a = coopor::AnnounceArea(-1);
        if (a != coopor::kAnnounceNoMap) Fail("p97 announce: no map at slot " + I(slots[si]) + " answered " + I(a) + ", want skip (no-map)");
    }
    if (coopor::AnnounceArea(0) != coopor::kAnnounceAdoptable) Fail("p97 announce: held 0 was not adoptable");
    if (coopor::AnnounceArea(1) != coopor::kAnnounceOtherHeld) Fail("p97 announce: held 1 was not other-held");
}


/* ------------------------------------------------------------------------------------------------
   P8i / U1 - shared_wastelands.cfg's TEXT LAYER (src/common/cfgtext.cpp), which is the same translation unit
   the plugin and the MULTIPLAYER panel compile.  Nothing here touches a file: CfgFormat produces text,
   CfgParseText consumes text, and the round trip is the property that says the panel cannot write
   something the game then reads back as something else.
   ------------------------------------------------------------------------------------------------ */

std::string CfgRoleName(int r)
{
    return r == coopcfg::kCfgHost ? "host" : (r == coopcfg::kCfgClient ? "client" : "single");
}

std::string CfgShow(const coopcfg::CfgFields& f)
{
    return "role=" + CfgRoleName(f.role)
         + " host='" + f.hostAddr + "':" + I((int)f.hostPort)
         + " store='" + f.storeAddr + "':" + I((int)f.storePort)
         + " slot='" + f.slot + "' world='" + f.world + "'";
}

int CfgSame(const coopcfg::CfgFields& a, const coopcfg::CfgFields& b)
{
    return a.role == b.role && a.hostAddr == b.hostAddr && a.hostPort == b.hostPort
        && a.storeAddr == b.storeAddr && a.storePort == b.storePort
        && a.slot == b.slot && a.world == b.world;
}

/* 1. THE WRITER ROUND-TRIPS THROUGH THE PARSER, over every combination of role, notebook and names.
      This is the U1 claim in one sentence: what the panel writes is what the game reads back. */
void t_cfg_write_round_trips_through_the_parser()
{
    const int roles[3] = { coopcfg::kCfgSingle, coopcfg::kCfgHost, coopcfg::kCfgClient };
    const char* addrs[4] = { "127.0.0.1", "192.168.1.20", "myfriend.example.com", "0.0.0.0" };
    const unsigned short ports[3] = { 1, 7777, 65535 };
    const char* worlds[3] = { "coop", "northlands", "a-very-long-world-name-that-is-still-legal" };
    const char* slots[2] = { "s1", "quicksave2" };
    int r, a, p, wI, sI, cases = 0;

    for (r = 0; r < 3; ++r)
    for (a = 0; a < 4; ++a)
    for (p = 0; p < 3; ++p)
    for (wI = 0; wI < 3; ++wI)
    for (sI = 0; sI < 2; ++sI)
    {
        coopcfg::CfgFields f;
        f.role = roles[r];
        f.hostAddr = addrs[a];
        f.hostPort = ports[p];
        f.storeAddr = "127.0.0.1";
        f.storePort = (unsigned short)(27016 + p);
        f.world = worlds[wI];
        f.slot = slots[sI];

        const std::string text = coopcfg::CfgFormat(f);
        coopcfg::CfgFields back;
        long long unknown = 0, bad = 0;
        int lines = 0;
        coopcfg::CfgParseText(text, &back, 0, &lines, &unknown, &bad);
        ++cases;

        if (!CfgSame(f, back))
            Fail("the writer and the parser disagree.\n    wrote: " + CfgShow(f)
                 + "\n    read : " + CfgShow(back) + "\n    text :\n" + text);
        if (bad != 0)
            Fail("the parser called " + I((int)bad) + " of its own writer's lines bad. text:\n" + text);
        if (unknown != 0)
            Fail("the parser called " + I((int)unknown) + " of its own writer's keys unknown. text:\n" + text);
    }
    if (cases != 216) Fail("the sweep ran " + I(cases) + " cases, want 216");
}

/* 2. THE ADDRESS RULE - the LAST colon, digits only, 1..65535, refused rather than clamped.  Every
      input below is a literal; nothing here is computed by the function under test. */
void t_cfg_addr_rule()
{
    struct Row { const char* in; int ok; const char* addr; int port; };
    const Row rows[] =
    {
        { "127.0.0.1:7777",          1, "127.0.0.1",      7777 },
        { "192.168.1.20:1",          1, "192.168.1.20",      1 },
        { "host.example.com:65535",  1, "host.example.com", 65535 },
        { "::1:7777",                1, "::1",            7777 },   /* the LAST colon, so the literal keeps its tail */
        { "[fe80::1]:7777",          1, "[fe80::1]",      7777 },
        { "127.0.0.1:0",             0, "",                  0 },   /* 0 is refused, not accepted */
        { "127.0.0.1:65536",         0, "",                  0 },   /* out of range REFUSED, never clamped */
        { "127.0.0.1:70000",         0, "",                  0 },
        { "127.0.0.1:123456",        0, "",                  0 },   /* more than five characters */
        { "127.0.0.1:77a7",          0, "",                  0 },
        { "127.0.0.1:",              0, "",                  0 },
        { ":7777",                   0, "",                  0 },
        { "hello",                   0, "",                  0 },
        { "",                        0, "",                  0 }
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    int i;
    for (i = 0; i < n; ++i)
    {
        std::string a("!untouched!");
        unsigned short p = 4242;
        const bool got = coopcfg::CfgSplitAddr(std::string(rows[i].in), &a, &p);
        if (got != (rows[i].ok != 0))
        {
            Fail(std::string("CfgSplitAddr('") + rows[i].in + "') answered " + I(got ? 1 : 0)
                 + ", want " + I(rows[i].ok));
            continue;
        }
        if (rows[i].ok)
        {
            if (a != rows[i].addr || (int)p != rows[i].port)
                Fail(std::string("CfgSplitAddr('") + rows[i].in + "') gave '" + a + "':" + I((int)p)
                     + ", want '" + rows[i].addr + "':" + I(rows[i].port));
        }
        else
        {
            /* A REFUSAL WRITES NOTHING - the caller's variables are what they were. */
            if (a != "!untouched!" || p != 4242)
                Fail(std::string("CfgSplitAddr('") + rows[i].in + "') refused but still wrote '" + a
                     + "':" + I((int)p));
        }
    }
}

/* 3. THE PORT FIELD and the ADDRESS FIELD as the panel sees them - the answer AND the sentence. */
void t_cfg_panel_field_validators()
{
    unsigned short p = 0;
    std::string why;

    if (!coopcfg::CfgPortFieldOk("7777", &p, &why) || p != 7777)
        Fail("CfgPortFieldOk('7777') refused or gave " + I((int)p));
    p = 0; why.clear();
    if (coopcfg::CfgPortFieldOk("70000", &p, &why)) Fail("CfgPortFieldOk('70000') accepted an out-of-range port");
    if (why.empty()) Fail("CfgPortFieldOk('70000') refused with no sentence for the player");
    why.clear();
    if (coopcfg::CfgPortFieldOk("", &p, &why)) Fail("CfgPortFieldOk('') accepted an empty port");
    if (why.empty()) Fail("CfgPortFieldOk('') refused with no sentence for the player");

    std::string a;
    why.clear();
    if (!coopcfg::CfgAddrFieldOk("192.168.1.20:7777", "address for the host", &a, &p, &why)
        || a != "192.168.1.20" || p != 7777)
        Fail("CfgAddrFieldOk on a good address gave '" + a + "':" + I((int)p) + " (" + why + ")");

    const char* bad[5] = { "hello", "1.2.3.4:70000", "", "0.0.0.0:7777", "no spaces here:7777" };
    int i;
    for (i = 0; i < 5; ++i)
    {
        a = "!untouched!"; p = 4242; why.clear();
        if (coopcfg::CfgAddrFieldOk(bad[i], "address for the host", &a, &p, &why))
            Fail(std::string("CfgAddrFieldOk('") + bad[i] + "') accepted it");
        else if (why.empty())
            Fail(std::string("CfgAddrFieldOk('") + bad[i] + "') refused with no sentence for the player");
        else if (a != "!untouched!" || p != 4242)
            Fail(std::string("CfgAddrFieldOk('") + bad[i] + "') refused but still wrote '" + a + "'");
    }

    /* The world name and the save slot become a folder name and a line in the settings file. */
    why.clear();
    if (!coopcfg::CfgNameFieldOk("northlands", "world name", coopcfg::kCfgWorldFieldMax, &why))
        Fail("CfgNameFieldOk('northlands') refused: " + why);
    /* owner decision 249 a: single spaces inside a world name are fine; two in a row are not */
    if (!coopcfg::CfgNameFieldOk("New World", "world name", coopcfg::kCfgWorldFieldMax, &why)
        || !coopcfg::CfgNameFieldOk("north lands 2", "world name", coopcfg::kCfgWorldFieldMax, &why))
        Fail("249a: CfgNameFieldOk refused a world name with single spaces: " + why);
    why.clear();
    if (coopcfg::CfgNameFieldOk("New  World", "world name", coopcfg::kCfgWorldFieldMax, &why)
        || why != "That name has a character that can't be used.")
        Fail("249a: CfgNameFieldOk must refuse two spaces in a row with the hint sentence (read '" + why + "')");
    why.clear();
    if (coopcfg::CfgNameFieldOk("north/lands", "world name", coopcfg::kCfgWorldFieldMax, &why)
        || why != "The world name can't contain '/'.")
        Fail("249a: the bad-character sentence ends with the new hint (read '" + why + "')");
    const char* badNames[6] = { "", "  ", "north  lands", "north/lands", "north=lands", "north;lands" };
    for (i = 0; i < 6; ++i)
    {
        why.clear();
        if (coopcfg::CfgNameFieldOk(badNames[i], "world name", coopcfg::kCfgWorldFieldMax, &why))
            Fail(std::string("CfgNameFieldOk('") + badNames[i] + "') accepted it");
        else if (why.empty())
            Fail(std::string("CfgNameFieldOk('") + badNames[i] + "') refused with no sentence for the player");
    }
    why.clear();
    if (coopcfg::CfgNameFieldOk(std::string(coopcfg::kCfgWorldFieldMax + 1, 'a'), "world name",
                                coopcfg::kCfgWorldFieldMax, &why))
        Fail("CfgNameFieldOk accepted a name one character over the cap the EditBox enforces");
}

/* JOIN BY ADDRESS's ADDRESS and PORT boxes (contributor task 1): the split ADDRESS hands to PORT (typed or pasted "host:port",
   "[v6]:port"; never steam: or a bare IPv6 literal), the PORT range and its one sentence, and the JOIN press's address and port. */
void t_cfg_join_addr_port()
{
    const std::string why1 = "Port must be a number from 1 to 65535.";
    if (std::string(coopcfg::kCfgJoinPortWhy) != why1) Fail("the PORT sentence is not the approved one");
    struct TakeRow { const char* in; bool ok; const char* host; const char* port; };
    const TakeRow take[] = {
        { "192.168.1.20:7778", true,  "192.168.1.20", "7778" },
        { "host.example:7778", true,  "host.example", "7778" },
        { " 10.0.0.2:27015 ",  true,  "10.0.0.2",     "27015" },
        { "[::1]:7777",        true,  "[::1]",        "7777" },
        { "1.2.3.4:99999",     true,  "1.2.3.4",      "99999" },   /* moved; PORT's check refuses it */
        { "192.168.1.20",      false, "", "" },
        { "[::1]",             false, "", "" },
        { "::1",               false, "", "" },                    /* bare IPv6: its last colon is not a port */
        { "fe80::1:7777",      false, "", "" },
        { "host:abc",          false, "", "" },
        { "host:",             false, "", "" },
        { ":7777",             false, "", "" },
        { "[::1:7777",         false, "", "" },
        { "steam:76561198000000000", false, "", "" },
        { "",                  false, "", "" },
    };
    for (size_t i = 0; i < sizeof(take) / sizeof(take[0]); ++i)
    {
        std::string h = "!", p = "!";
        const bool got = coopcfg::CfgJoinTakePort(take[i].in, &h, &p);
        if (got != take[i].ok) { Fail(std::string("CfgJoinTakePort('") + take[i].in + "') answered " + I(got ? 1 : 0)); continue; }
        if (got && (h != take[i].host || p != take[i].port)) Fail(std::string("CfgJoinTakePort('") + take[i].in + "') gave '" + h + "' / '" + p + "'");
        if (!got && (h != "!" || p != "!")) Fail(std::string("CfgJoinTakePort('") + take[i].in + "') refused but wrote");
    }
    if (!coopcfg::CfgJoinIsSteam("steam:76561198000000000") || coopcfg::CfgJoinIsSteam("192.168.1.20") || coopcfg::CfgJoinIsSteam(""))
        Fail("CfgJoinIsSteam");

    /* PORT: the range edges, digits only, and empty - each refusal the one sentence */
    struct PortRow { const char* in; bool ok; int port; };
    const PortRow ports[] = { { "1", true, 1 }, { "7777", true, 7777 }, { "65535", true, 65535 }, { " 7778 ", true, 7778 },
                              { "0", false, 0 }, { "65536", false, 0 }, { "abc", false, 0 }, { "", false, 0 }, { "-1", false, 0 },
                              { "77a7", false, 0 }, { "123456", false, 0 } };
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
    {
        unsigned short p = 0; std::string why;
        const bool got = coopcfg::CfgJoinPortOk(ports[i].in, &p, &why);
        if (got != ports[i].ok) Fail(std::string("CfgJoinPortOk('") + ports[i].in + "') answered " + I(got ? 1 : 0));
        else if (got && p != (unsigned short)ports[i].port) Fail(std::string("CfgJoinPortOk('") + ports[i].in + "') gave " + I((int)p));
        else if (!got && why != why1) Fail(std::string("CfgJoinPortOk('") + ports[i].in + "') said '" + why + "'");
    }

    /* the JOIN press */
    struct JoinRow { const char* addr; const char* port; bool ok; const char* host; int p; const char* why; };
    const JoinRow joins[] = {
        { "192.168.1.20",      "7777",  true,  "192.168.1.20", 7777, "" },        /* no port typed: PORT's number */
        { "192.168.1.20",      "7778",  true,  "192.168.1.20", 7778, "" },
        { "192.168.1.20:7778", "7777",  true,  "192.168.1.20", 7778, "" },        /* still in ADDRESS: that port wins */
        { "host.example",      "27015", true,  "host.example", 27015, "" },
        { "[::1]",             "7777",  true,  "[::1]",        7777, "" },
        { "[::1]:7778",        "7777",  true,  "[::1]",        7778, "" },
        { "::1",               "7777",  true,  "[::1]",        7777, "" },        /* a bare IPv6 literal is bracketed */
        { "192.168.1.20",      "0",     false, "", 0, "Port must be a number from 1 to 65535." },
        { "192.168.1.20",      "65536", false, "", 0, "Port must be a number from 1 to 65535." },
        { "192.168.1.20",      "abc",   false, "", 0, "Port must be a number from 1 to 65535." },
        { "192.168.1.20",      "",      false, "", 0, "Port must be a number from 1 to 65535." },
        { "1.2.3.4:99999",     "7777",  false, "", 0, "Port must be a number from 1 to 65535." },
        { "",                  "7777",  false, "", 0, "No host address entered." },
        { "host:abc",          "7777",  false, "", 0, "\"host:abc\" isn't a valid address." },
        { "0.0.0.0",           "7777",  false, "", 0, "0.0.0.0 can't be joined." },
        { "no spaces",         "7777",  false, "", 0, "\"no spaces\" isn't a valid IP address or host name." },
    };
    for (size_t i = 0; i < sizeof(joins) / sizeof(joins[0]); ++i)
    {
        std::string a = "!untouched!", why; unsigned short p = 4242;
        const bool got = coopcfg::CfgJoinAddrOk(joins[i].addr, joins[i].port, &a, &p, &why);
        const std::string row = std::string("CfgJoinAddrOk('") + joins[i].addr + "', '" + joins[i].port + "')";
        if (got != joins[i].ok) { Fail(row + " answered " + I(got ? 1 : 0) + " (" + why + ")"); continue; }
        if (got && (a != joins[i].host || p != (unsigned short)joins[i].p)) Fail(row + " gave '" + a + "':" + I((int)p));
        if (!got && why != joins[i].why) Fail(row + " said '" + why + "'");
        if (!got && (a != "!untouched!" || p != 4242)) Fail(row + " refused but wrote");
    }
    /* steam: joins as today - PORT is ignored, whatever it holds */
    {
        std::string a, why; unsigned short p = 0, p2 = 0; std::string a2, why2;
        const bool now = coopcfg::CfgAddrFieldOk("steam:76561198000000000", "address", &a2, &p2, &why2);
        const bool got = coopcfg::CfgJoinAddrOk("steam:76561198000000000", "abc", &a, &p, &why);
        if (!now || !got || a != a2 || p != p2) Fail("a steam: address does not join as it did, or PORT was read for it");
    }
    /* the settings file's own address:port rule is untouched: CfgSplitAddr still refuses an address with no port */
    {
        std::string a; unsigned short p = 0;
        if (coopcfg::CfgSplitAddr("192.168.1.20", &a, &p) || !coopcfg::CfgSplitAddr("192.168.1.20:7777", &a, &p) || p != 7777)
            Fail("CfgSplitAddr changed");
    }
}

/* 4. WHAT A PASTE OR A KEYSTROKE IS REDUCED TO.  A newline in a value would be two lines on the next
      read, which is how a paste becomes a parse error nobody can see. */
void t_cfg_sanitise_typed()
{
    std::string in;
    in += "192.168.1.20";
    in += '\r';
    in += '\n';
    in += ":7777";
    in += '\t';
    const std::string out = coopcfg::CfgSanitiseTyped(in, coopcfg::kCfgAddrFieldMax);
    if (out != "192.168.1.20:7777")
        Fail("CfgSanitiseTyped kept a control character: '" + out + "'");
    if (coopcfg::CfgSanitiseTyped(std::string(200, 'x'), coopcfg::kCfgAddrFieldMax).size()
        != coopcfg::kCfgAddrFieldMax)
        Fail("CfgSanitiseTyped did not truncate to the field cap");
    if (!coopcfg::CfgSanitiseTyped("", 8).empty())
        Fail("CfgSanitiseTyped invented characters out of an empty string");
}

/* 5. THE COMPLETENESS RULE, which is the one FallBackToSingle acts on AND the one the panel refuses on.
      A role that cannot reach anybody is not that role. */
void t_cfg_incomplete_reason()
{
    coopcfg::CfgFields f;
    if (coopcfg::CfgIncompleteReason(f) != 0) Fail("a default (single) config was called incomplete");

    f.role = coopcfg::kCfgHost; f.hostPort = 0;
    if (coopcfg::CfgIncompleteReason(f) != 1) Fail("role=host with no port was not reason 1");
    f.hostPort = 7777;
    if (coopcfg::CfgIncompleteReason(f) != 0) Fail("role=host with a port was called incomplete");

    f.role = coopcfg::kCfgClient; f.hostPort = 0; f.hostAddr = "127.0.0.1";
    if (coopcfg::CfgIncompleteReason(f) != 1) Fail("role=client with no port was not reason 1");
    f.hostPort = 7777; f.hostAddr = "";
    if (coopcfg::CfgIncompleteReason(f) != 2) Fail("role=client with no address was not reason 2");
    f.hostAddr = "127.0.0.1";
    if (coopcfg::CfgIncompleteReason(f) != 0) Fail("a complete client config was called incomplete");

    f.role = coopcfg::kCfgSingle; f.hostPort = 0; f.hostAddr = "";
    if (coopcfg::CfgIncompleteReason(f) != 0) Fail("role=single was called incomplete - it needs nothing");
}

/* 6. THE PARSER'S OWN BOUNDS AND ITS DIAGNOSTICS.  A file that is not a config file cannot make this
      loop long, and every line it ignores is NAMED. */
/* PP3 (player-path-plan PP3) - THE PLAYER'S DATA FOLDER. Every input is a literal. */
void t_pp3_datadir_choose()
{
    std::string d;
    /* the TEST override wins over everything, trailing separator dropped */
    if (coopdata::DataDirChoose("C:\\code\\kenshi-coop\\instances\\A\\data\\", "C:\\Users\\p\\AppData\\Local", "C:\\Users\\p", &d) != coopdata::kDataOverride
        || d != "C:\\code\\kenshi-coop\\instances\\A\\data") Fail("datadir= did not win: '" + d + "'");
    /* the default: %LOCALAPPDATA%\kenshi\Shared Wastelands (beside Kenshi's own save folder) */
    if (coopdata::DataDirChoose("", "C:\\Users\\p\\AppData\\Local", "C:\\Users\\p", &d) != coopdata::kDataLocalAppData
        || d != "C:\\Users\\p\\AppData\\Local\\kenshi\\Shared Wastelands") Fail("the default data folder is wrong: '" + d + "'");
    if (coopdata::DataDirChoose("", "C:\\Users\\p\\AppData\\Local\\", 0, &d) != coopdata::kDataLocalAppData
        || d != "C:\\Users\\p\\AppData\\Local\\kenshi\\Shared Wastelands") Fail("a trailing separator on LOCALAPPDATA doubled: '" + d + "'");
    /* an unreadable / unusable environment falls to USERPROFILE, then to NONE - never to the mod folder */
    if (coopdata::DataDirChoose("", 0, "C:\\Users\\p", &d) != coopdata::kDataUserProfile
        || d != "C:\\Users\\p\\AppData\\Local\\kenshi\\Shared Wastelands") Fail("no LOCALAPPDATA did not fall to USERPROFILE: '" + d + "'");
    if (coopdata::DataDirChoose("", "relative\\x", "C:\\Users\\p", &d) != coopdata::kDataUserProfile) Fail("a relative LOCALAPPDATA was used");
    if (coopdata::DataDirChoose("", "", "", &d) != coopdata::kDataNone || !d.empty()) Fail("no usable environment still named a folder: '" + d + "'");
    if (coopdata::DataDirChoose("", "C:", "x", &d) != coopdata::kDataNone) Fail("a bare drive was used as the data folder root");
    if (coopdata::DataDirChoose("", "\\\\srv\\share", 0, &d) != coopdata::kDataLocalAppData) Fail("a UNC LOCALAPPDATA was refused");
    /* the world server's top folder is <data folder>\worlds; no data folder = none */
    if (coopdata::WorldsDir("C:\\D") != "C:\\D\\worlds") Fail("WorldsDir is not <data>\\worlds");
    if (!coopdata::WorldsDir("").empty()) Fail("WorldsDir of no data folder is not empty");
    if (coopworld::WorldDirIn(coopdata::WorldsDir("C:\\D"), "Coop2") != "C:\\D\\worlds\\coop2") Fail("a world folder under worlds\\ is wrong: " + coopworld::WorldDirIn(coopdata::WorldsDir("C:\\D"), "Coop2"));
}

void t_pp3_identity_migration_table()
{
    const std::string a = "0123456789abcdef0123456789abcdef", b = "fedcba9876543210fedcba9876543210";
    if (coopdata::IdentityDecide("", "") != coopdata::kIdGenerate) Fail("none/none is not GENERATE");
    if (coopdata::IdentityDecide("", a) != coopdata::kIdMigrate) Fail("old only is not MIGRATE");
    if (coopdata::IdentityDecide(a, "") != coopdata::kIdRead) Fail("new only is not READ");
    if (coopdata::IdentityDecide(a, b) != coopdata::kIdRead) Fail("both: the new file did not win");
    if (coopdata::IdentityDecide(a, a) != coopdata::kIdRead) Fail("both equal is not READ");
}

/* PP3b (review 2026-09-27 HIGH) - THE IDENTITY IS NEVER REPLACED WHILE player.cfg IS PRESENT. */
void t_pp3b_identity_state_table()
{
    const std::string a = "0123456789abcdef0123456789abcdef", k = "00112233445566778899aabbccddeeff", o = "fedcba9876543210fedcba9876543210";
    const std::string none;
    if (coopdata::IdentityDecideState(coopdata::kPcValid, a, k, o) != coopdata::kIdRead) Fail("a valid player.cfg did not win");
    if (coopdata::IdentityDecideState(coopdata::kPcAbsent, none, none, none) != coopdata::kIdGenerate) Fail("absent / no bak / no old id is not GENERATE");
    if (coopdata::IdentityDecideState(coopdata::kPcAbsent, none, none, o) != coopdata::kIdMigrate) Fail("absent + old id is not MIGRATE");
    if (coopdata::IdentityDecideState(coopdata::kPcAbsent, none, k, o) != coopdata::kIdFromBak) Fail("absent + a good .bak did not use the .bak");
    const int bad[2] = { coopdata::kPcUnreadable, coopdata::kPcInvalid };
    for (int i = 0; i < 2; ++i)
    {
        if (coopdata::IdentityDecideState(bad[i], none, none, none) != coopdata::kIdSessionOnly) Fail("present-but-bad with nothing else is not SESSION-ONLY");
        if (coopdata::IdentityDecideState(bad[i], none, none, o) != (bad[i] == coopdata::kPcInvalid ? coopdata::kIdMigrate : coopdata::kIdSessionOnly))
            Fail("present-but-bad + an old id: a DAMAGED file is not restored from it or a LOCKED one is (PP3e)");
        if (coopdata::IdentityDecideState(bad[i], none, k, o) != coopdata::kIdFromBak) Fail("present-but-bad + a good .bak did not recover from the .bak");
    }
    for (int s = 1; s <= 3; ++s)
        for (int b = 0; b < 2; ++b)
            for (int ol = 0; ol < 2; ++ol)
                if (coopdata::IdentityDecideState(s, s == coopdata::kPcValid ? a : none, b ? k : none, ol ? o : none) == coopdata::kIdGenerate)
                    Fail("a PRESENT player.cfg led to a NEW id");
    if (coopdata::IdentityMayWrite(coopdata::kPcUnreadable, coopdata::kIdFromBak)) Fail("a LOCKED player.cfg would be overwritten from the .bak");
    if (!coopdata::IdentityMayWrite(coopdata::kPcInvalid, coopdata::kIdFromBak)) Fail("a readable-but-invalid player.cfg is not rewritten from the .bak");
    if (!coopdata::IdentityMayWrite(coopdata::kPcAbsent, coopdata::kIdFromBak)) Fail("an absent player.cfg is not rewritten from the .bak");
    if (coopdata::IdentityMayWrite(coopdata::kPcUnreadable, coopdata::kIdSessionOnly) || coopdata::IdentityMayWrite(coopdata::kPcInvalid, coopdata::kIdSessionOnly))
        Fail("a session-only identity may write player.cfg");
    if (!coopdata::IdentityMayWrite(coopdata::kPcAbsent, coopdata::kIdGenerate) || !coopdata::IdentityMayWrite(coopdata::kPcValid, coopdata::kIdRead)
        || !coopdata::IdentityMayWrite(coopdata::kPcAbsent, coopdata::kIdMigrate))
        Fail("a normal identity may not write player.cfg");
}

/* PP3c (re-check 2026-09-27 HIGH) - TWO STARTS IN A ROW over a simulated data folder, through the SAME pure plan IdentityLoad
   follows (coopdata::IdentityPlanStart). A bad player.cfg with no usable .bak: start 1 is session-only and the file is left
   exactly as it is; start 2 sees the same file and is session-only again - no move aside, no write, never a new id. */
struct Pp3cDisk { bool cfg; std::string cfgId; bool bak; std::string bakId; int asides; };
static coopdata::IdentityStartPlan Pp3cStart(Pp3cDisk* k, const std::string& oldId, const std::string& fresh)
{
    const int st = !k->cfg ? coopdata::kPcAbsent : (coopstore::PlayerIdOk(k->cfgId) ? coopdata::kPcValid : coopdata::kPcInvalid);
    const std::string bakId = (k->bak && coopstore::PlayerIdOk(k->bakId)) ? k->bakId : std::string();
    const coopdata::IdentityStartPlan p = coopdata::IdentityPlanStart(st, st == coopdata::kPcValid ? k->cfgId : std::string(), bakId, oldId);
    if (p.moveAside) { k->cfg = false; ++k->asides; }
    if (p.write)
    {
        k->cfg = true;
        k->cfgId = p.decision == coopdata::kIdFromBak ? bakId : (p.decision == coopdata::kIdMigrate ? oldId : fresh);
    }
    return p;
}

void t_pp3c_bad_player_cfg_two_starts()
{
    const std::string k = "00112233445566778899aabbccddeeff", o = "fedcba9876543210fedcba9876543210";
    const std::string fresh = "abababababababababababababababab", junk = "not a player id";
    for (int withOld = 0; withOld < 1; ++withOld)   /* PP3e: a valid legacy id RESTORES a damaged player.cfg - t_pp3e */
        for (int junkBak = 0; junkBak < 2; ++junkBak)
        {
            Pp3cDisk d; d.cfg = true; d.cfgId = junk; d.bak = junkBak != 0; d.bakId = junk; d.asides = 0;
            for (int start = 1; start <= 2; ++start)
            {
                const coopdata::IdentityStartPlan p = Pp3cStart(&d, withOld ? o : std::string(), fresh);
                if (p.decision != coopdata::kIdSessionOnly || !p.sessionOnly) Fail("a bad player.cfg with no usable .bak is not SESSION-ONLY at every start");
                if (p.moveAside) Fail("a bad player.cfg was moved aside with no .bak restore in the same step");
                if (p.write) Fail("a session-only start writes player.cfg");
                if (!d.cfg || d.cfgId != junk || d.asides != 0) Fail("a session-only start did not leave player.cfg exactly where and as it was");
            }
        }
    /* A bad player.cfg WITH a good .bak: moved aside and restored in one step; the next start simply reads it. */
    Pp3cDisk d; d.cfg = true; d.cfgId = junk; d.bak = true; d.bakId = k; d.asides = 0;
    coopdata::IdentityStartPlan p = Pp3cStart(&d, o, fresh);
    if (p.decision != coopdata::kIdFromBak || !p.moveAside || !p.write || p.sessionOnly) Fail("a bad player.cfg + a good .bak was not moved aside and restored");
    if (!d.cfg || d.cfgId != k || d.asides != 1) Fail("the .bak restore did not leave the .bak's id in player.cfg");
    p = Pp3cStart(&d, o, fresh);
    if (p.decision != coopdata::kIdRead || p.moveAside || p.write || p.sessionOnly || d.cfgId != k || d.asides != 1) Fail("the start after a .bak restore did not simply read it");
    /* A LOCKED player.cfg is never moved or written, even with a good .bak. */
    p = coopdata::IdentityPlanStart(coopdata::kPcUnreadable, std::string(), k, o);
    if (p.moveAside || p.write || p.sessionOnly || p.decision != coopdata::kIdFromBak) Fail("a LOCKED player.cfg would be moved or written");
    /* The whole table: a move aside ALWAYS comes with a .bak restore write; a session-only start never moves or writes. */
    for (int s = 0; s <= 3; ++s)
        for (int b = 0; b < 2; ++b)
            for (int ol = 0; ol < 2; ++ol)
            {
                const coopdata::IdentityStartPlan q = coopdata::IdentityPlanStart(s, s == coopdata::kPcValid ? k : std::string(), b ? k : std::string(), ol ? o : std::string());
                if (q.moveAside && !(q.write && (q.decision == coopdata::kIdFromBak || q.decision == coopdata::kIdMigrate) && s == coopdata::kPcInvalid))
                    Fail("a move aside without a .bak or legacy restore in the same step");
                if (q.sessionOnly && (q.write || q.moveAside)) Fail("a session-only plan moves or writes player.cfg");
                if (s != coopdata::kPcAbsent && q.decision == coopdata::kIdGenerate) Fail("a PRESENT player.cfg led to a NEW id");
            }
}

/* PP3d (owner-approved 2026-09-27) - THE IDENTITY KIND, THE HOST / JOIN REFUSAL AND THE ONE PATH TO A NEW ID. Kind 1 only for an
   UNREADABLE player.cfg with no usable .bak, kind 2 only for a DAMAGED one with none; every other start is normal. HOST / JOIN
   are refused exactly while the kind is not normal. Only CONTINUE AS NEW PLAYER on a DAMAGED identity may replace it: a
   dismissed box leaves the disk as it was (the next start is DAMAGED again), the chosen one gives a normal start after. */
void t_pp3d_identity_kind_and_box()
{
    const std::string none, k = "0123456789abcdef0123456789abcdef", o = "fedcba9876543210fedcba9876543210";
    const int states[4] = { coopdata::kPcAbsent, coopdata::kPcValid, coopdata::kPcUnreadable, coopdata::kPcInvalid };
    for (int s = 0; s < 4; ++s)
        for (int b = 0; b < 2; ++b)
            for (int ol = 0; ol < 2; ++ol)
            {
                const int st = states[s];
                const coopdata::IdentityStartPlan p = coopdata::IdentityPlanStart(st, st == coopdata::kPcValid ? k : none, b ? k : none, ol ? o : none);
                const int kind = coopdata::IdentitySessionOnlyKindOf(st, p);
                const int want = b ? 0 : (st == coopdata::kPcUnreadable ? 1 : (st == coopdata::kPcInvalid && !ol) ? 2 : 0);   /* PP3e: a legacy id restores */
                if (kind != want)
                {
                    char m[96]; sprintf(m, "identity kind %d for state %d bak %d old %d (want %d)", kind, st, b, ol, want);
                    Fail(m);
                }
                if (coopdata::IdentityRefusesHostJoin(kind) != (kind != 0)) Fail("HOST / JOIN refusal does not follow the kind");
                if (kind != 0 && (p.write || p.moveAside)) Fail("an unusable identity's start writes or moves player.cfg");
            }
    if (coopdata::IdentityRefusesHostJoin(coopdata::kIdKindNormal)) Fail("HOST / JOIN refused with a normal identity");
    if (!coopdata::IdentityRefusesHostJoin(coopdata::kIdKindUnreadable) || !coopdata::IdentityRefusesHostJoin(coopdata::kIdKindDamaged))
        Fail("HOST / JOIN allowed with an unusable identity");
    for (int kind = 0; kind < 3; ++kind)
        for (int btn = 0; btn < 3; ++btn)
            if (coopdata::IdentityMayReplace(kind, btn) != (kind == coopdata::kIdKindDamaged && btn == coopdata::kIdBoxContinue))
                Fail("an identity may be replaced by something other than CONTINUE AS NEW PLAYER on a DAMAGED box");
    /* the flow over a simulated disk: DAMAGED, dismissed, restarted, then CONTINUE AS NEW PLAYER, then restarted */
    Pp3cDisk d; d.cfg = true; d.cfgId = "damaged"; d.bak = false; d.asides = 0;
    coopdata::IdentityStartPlan p = Pp3cStart(&d, none, o);
    int kind = coopdata::IdentitySessionOnlyKindOf(coopdata::kPcInvalid, p);
    if (kind != coopdata::kIdKindDamaged) Fail("a damaged player.cfg with no .bak is not DAMAGED");
    if (coopdata::IdentityMayReplace(kind, coopdata::kIdBoxDismiss)) Fail("CANCEL replaced the identity");
    p = Pp3cStart(&d, none, o);
    if (coopdata::IdentitySessionOnlyKindOf(coopdata::kPcInvalid, p) != coopdata::kIdKindDamaged || d.cfgId != "damaged" || d.asides != 0)
        Fail("the start after CANCEL is not DAMAGED again with the file untouched");
    if (!coopdata::IdentityMayReplace(kind, coopdata::kIdBoxContinue)) Fail("CONTINUE AS NEW PLAYER may not replace a DAMAGED identity");
    ++d.asides; d.cfgId = k; d.bak = true; d.bakId = k;   /* what IdentityReplaceByChoice does: renamed aside, new id + .bak written */
    p = Pp3cStart(&d, none, o);
    if (p.decision != coopdata::kIdRead || coopdata::IdentitySessionOnlyKindOf(coopdata::kPcValid, p) != 0 || d.cfgId != k)
        Fail("the start after CONTINUE AS NEW PLAYER does not simply read the new id");
    /* the owner's texts: CAPS titles and buttons, one button for UNREADABLE, two for DAMAGED */
    if (coopui::IdBoxButtons(1) != 1 || coopui::IdBoxButtons(2) != 2) Fail("identity box button counts");
    if (std::string(coopui::IdBoxTitle(1)) != "CAN'T START MULTIPLAYER" || std::string(coopui::IdBoxTitle(2)) != "MULTIPLAYER DATA DAMAGED")
        Fail("identity box titles");
    if (std::string(coopui::IdBoxDismissCaption(1)) != "OK" || std::string(coopui::IdBoxDismissCaption(2)) != "CANCEL"
     || std::string(coopui::kIdBox2Continue) != "CONTINUE AS NEW PLAYER") Fail("identity box buttons");
    if (std::string(coopui::IdBoxText(1)) != "Kenshi couldn't read your multiplayer data. Another program (such as antivirus or a backup tool) may be using it."
     || std::string(coopui::IdBoxText(2)) != "Your multiplayer data is damaged and couldn't be recovered. You can continue as a new player, but worlds you've played before won't recognise your characters.")
        Fail("identity box texts are not the owner-approved wording");
}

/* PP3e (review 2026-09-27 MED) - DAMAGED (case 2) ONLY WHEN NOTHING IS RECOVERABLE: a LOCKED .bak is case 1, a valid legacy id
   restores player.cfg, and the CONTINUE AS NEW PLAYER click re-reads both files and recovers instead of replacing. */
void t_pp3e_case2_only_when_nothing_recoverable()
{
    const std::string none, k = "0123456789abcdef0123456789abcdef", o = "fedcba9876543210fedcba9876543210", fresh = "abababababababababababababababab";
    const int bad[2] = { coopdata::kPcInvalid, coopdata::kPcUnreadable };
    for (int ol = 0; ol < 2; ++ol)
        for (int i = 0; i < 2; ++i)
        {
            const coopdata::IdentityStartPlan q = coopdata::IdentityPlanStart(bad[i], none, coopdata::kPcUnreadable, none, ol ? o : none);
            if (!q.sessionOnly || q.write || q.moveAside) Fail("a LOCKED .bak beside a bad player.cfg did not leave both files alone");
            if (coopdata::IdentitySessionOnlyKindOf(bad[i], coopdata::kPcUnreadable, q) != coopdata::kIdKindUnreadable) Fail("a LOCKED .bak is not kind 1 (UNREADABLE)");
        }
    const int nob[2] = { coopdata::kPcAbsent, coopdata::kPcInvalid };
    for (int i = 0; i < 2; ++i)
    {
        const coopdata::IdentityStartPlan q = coopdata::IdentityPlanStart(coopdata::kPcInvalid, none, nob[i], none, none);
        if (coopdata::IdentitySessionOnlyKindOf(coopdata::kPcInvalid, nob[i], q) != coopdata::kIdKindDamaged) Fail("nothing recoverable is not DAMAGED");
    }
    /* a valid legacy id RESTORES a damaged player.cfg: moved aside and rewritten in one step, kind 0; the next start reads it */
    Pp3cDisk d; d.cfg = true; d.cfgId = "damaged"; d.bak = false; d.asides = 0;
    coopdata::IdentityStartPlan p = Pp3cStart(&d, o, fresh);
    if (p.decision != coopdata::kIdMigrate || !p.moveAside || !p.write || p.sessionOnly) Fail("a damaged player.cfg + a legacy id was not moved aside and restored");
    if (coopdata::IdentitySessionOnlyKindOf(coopdata::kPcInvalid, coopdata::kPcAbsent, p) != coopdata::kIdKindNormal) Fail("a legacy-id restore still shows case 2");
    if (!d.cfg || d.cfgId != o || d.asides != 1) Fail("the legacy restore did not leave the legacy id in player.cfg");
    p = Pp3cStart(&d, o, fresh);
    if (p.decision != coopdata::kIdRead || p.write || p.moveAside || d.asides != 1) Fail("the start after a legacy restore did not simply read it");
    p = coopdata::IdentityPlanStart(coopdata::kPcUnreadable, none, coopdata::kPcAbsent, none, o);
    if (!p.sessionOnly || p.write || p.moveAside) Fail("a LOCKED player.cfg was restored from the legacy id");
    /* THE CLICK RE-CHECK, the whole table: replace ONLY when neither file holds a valid id and neither is unreadable */
    const int states[4] = { coopdata::kPcAbsent, coopdata::kPcValid, coopdata::kPcUnreadable, coopdata::kPcInvalid };
    for (int c = 0; c < 4; ++c)
        for (int b = 0; b < 4; ++b)
        {
            const int a = coopdata::IdentityClickDecide(states[c], states[b]);
            const bool anyValid = states[c] == coopdata::kPcValid || states[b] == coopdata::kPcValid;
            const bool anyLocked = states[c] == coopdata::kPcUnreadable || states[b] == coopdata::kPcUnreadable;
            if ((a == coopdata::kClickReplace) != (!anyValid && !anyLocked)) Fail("the click replaces beside a valid or unreadable file, or refuses with neither");
            if (states[c] == coopdata::kPcValid && a != coopdata::kClickRecoverCfg) Fail("a valid player.cfg at the click is not recovered first");
            if (states[c] != coopdata::kPcValid && states[b] == coopdata::kPcValid && a != coopdata::kClickRecoverBak) Fail("a valid .bak at the click is not recovered");
            if (!anyValid && anyLocked && a != coopdata::kClickRefuseUnreadable) Fail("an unreadable file at the click is not refused");
        }
    /* player.cfg FIXED mid-session: the start is DAMAGED, the file is repaired, then CONTINUE - recovered, nothing renamed */
    Pp3cDisk e; e.cfg = true; e.cfgId = "damaged"; e.bak = false; e.asides = 0;
    p = Pp3cStart(&e, none, fresh);
    if (coopdata::IdentitySessionOnlyKindOf(coopdata::kPcInvalid, coopdata::kPcAbsent, p) != coopdata::kIdKindDamaged) Fail("the mid-session case did not start DAMAGED");
    e.cfgId = k;
    const int act = coopdata::IdentityClickDecide(coopstore::PlayerIdOk(e.cfgId) ? coopdata::kPcValid : coopdata::kPcInvalid, coopdata::kPcAbsent);
    if (act == coopdata::kClickReplace) { ++e.asides; e.cfgId = fresh; }   /* what the replacement would do */
    if (act != coopdata::kClickRecoverCfg || e.asides != 0 || e.cfgId != k) Fail("a player.cfg fixed mid-session was not recovered at the click (renamed or replaced)");
    if (coopdata::IdentityClickDecide(coopdata::kPcInvalid, coopdata::kPcValid) != coopdata::kClickRecoverBak) Fail("a .bak readable at the click is not recovered");
}

/* PP3b - session.cfg CARRIES THE PANEL'S OWN FIELDS ONLY (identity and TEST keys stripped); a host's 0.0.0.0 is no last address. */
void t_pp3b_session_cfg_strips_identity()
{
    coopcfg::CfgFields f;
    f.role = coopcfg::kCfgClient; f.hostAddr = "192.168.1.20"; f.hostPort = 7777; f.slot = "s1"; f.world = "w2";
    f.playerId = "0123456789abcdef0123456789abcdef"; f.playerName = "Beep";
    f.saveRoot = "C:\\S"; f.storeDir = "C:\\T"; f.dataDir = "C:\\D"; f.modSalt = 1; f.profileTest = 2;
    const std::string body = coopdata::SessionCfgText(f);
    coopcfg::CfgFields b;
    coopcfg::CfgParseText(body, &b, 0, 0, 0, 0);
    if (!b.playerId.empty() || !b.playerName.empty()) Fail("session.cfg carries the identity: '" + b.playerId + "' '" + b.playerName + "'");
    if (!b.saveRoot.empty() || !b.storeDir.empty() || !b.dataDir.empty()) Fail("session.cfg carries a folder TEST key");
    if (b.modSalt != 0 || b.profileTest != 0) Fail("session.cfg carries a TEST-only key");
    /* T-251: built on by default - the records writer is ON unless a cfg says ownsave=off (a test switches it off) */
    if (coopcfg::CfgFields().ownSave != 1 || b.ownSave != 1) Fail("T-251: ownsave defaults to on");
    { coopcfg::CfgFields o; coopcfg::CfgParseText("ownsave=off\n", &o, 0, 0, 0, 0); if (o.ownSave != 0) Fail("T-251: ownsave=off switches the records writer off"); }
    if (b.role != coopcfg::kCfgClient || b.hostAddr != "192.168.1.20" || b.hostPort != 7777 || b.world != "w2")
        Fail("session.cfg lost the panel's own fields");
    if (body.compare(0, 13, "# session.cfg") != 0) Fail("session.cfg's header is not its own: " + body.substr(0, 40));
    if (coopdata::TestCfgKindOf(body) == coopdata::kTestPanelOld) Fail("session.cfg's own text reads as an old panel file");
    if (coopdata::LastAddrRecordable(coopcfg::kCfgHost, "0.0.0.0")) Fail("a host's 0.0.0.0 was recorded as the last address");
    if (coopdata::LastAddrRecordable(coopcfg::kCfgHost, "192.168.1.20")) Fail("a HOST save recorded a last address");
    if (coopdata::LastAddrRecordable(coopcfg::kCfgClient, "0.0.0.0")) Fail("0.0.0.0 was recorded as a joined address");
    if (!coopdata::LastAddrRecordable(coopcfg::kCfgClient, "192.168.1.20")) Fail("a joined address was not recorded");
}

/* PP3b - THE OLD PANEL-WRITTEN TEST FILE IS IGNORED (but for identity), AND session.cfg NEVER ARMS AT START. */
void t_pp3b_old_panel_cfg_and_prefill()
{
    const std::string panelOld = "# shared_wastelands.cfg - written by the in-game MULTIPLAYER panel (E43 / U1).\r\nrole=client\r\nhost=10.0.0.1:7777\r\nworld=old\r\nplayerid=0123456789abcdef0123456789abcdef\r\n";
    const std::string harness = "# shared_wastelands.cfg - written by tools/write-cfg.ps1 (E38 / decisions 42 and 43).\r\nrole=host\r\nhost=0.0.0.0:7777\r\nworld=coop\r\n";
    const std::string sess = "# session.cfg - written by the in-game MULTIPLAYER panel (E43 / U1).\r\nrole=client\r\nhost=192.168.1.20:7777\r\nworld=w2\r\n";
    if (coopdata::TestCfgKindOf(panelOld) != coopdata::kTestPanelOld) Fail("the old panel header was not recognised");
    if (coopdata::TestCfgKindOf("\xEF\xBB\xBF" + panelOld) != coopdata::kTestPanelOld) Fail("the old panel header behind a BOM was not recognised");
    if (coopdata::TestCfgKindOf(harness) != coopdata::kTestHarness) Fail("the harness's header was taken for the panel's");
    if (coopdata::TestCfgKindOf("role=host\r\nhost=0.0.0.0:7777\r\n") != coopdata::kTestHarness) Fail("a headerless test file lost its precedence");
    if (coopdata::TestCfgKindOf("") != coopdata::kTestNone) Fail("an empty test file is not NONE");
    {
        coopcfg::CfgFields w;
        w.role = coopcfg::kCfgClient; w.hostAddr = "192.168.1.20"; w.hostPort = 7777;
        if (coopdata::TestCfgKindOf(coopcfg::CfgFormat(w)) != coopdata::kTestPanelOld) Fail("the panel writer's own header is not the one the ignore rule reads");
    }
    coopcfg::CfgFields f;
    coopcfg::CfgParseText(coopdata::ArmSettingsText(true, sess, ""), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgSingle) Fail("session.cfg ARMED a connection at start");
    f = coopcfg::CfgFields();
    coopcfg::CfgParseText(coopdata::ArmSettingsText(true, sess, panelOld), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgSingle) Fail("an old panel-written test file ARMED a connection at start");
    f = coopcfg::CfgFields();
    coopcfg::CfgParseText(coopdata::ArmSettingsText(true, sess, harness), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgHost || f.hostPort != 7777) Fail("the harness's test file no longer arms at start");
    f = coopcfg::CfgFields();
    coopcfg::CfgParseText(coopdata::ArmSettingsText(false, sess, ""), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgClient || f.hostAddr != "192.168.1.20" || f.world != "w2") Fail("a panel press did not arm from session.cfg");
    f = coopcfg::CfgFields();
    coopcfg::CfgParseText(coopdata::ArmSettingsText(false, sess, panelOld), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgClient || f.hostAddr != "192.168.1.20" || f.world != "w2") Fail("an old panel-written test file overrode a panel press");
    f = coopcfg::CfgFields();
    coopcfg::CfgParseText(coopdata::ArmSettingsText(false, sess, harness), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgHost || f.world != "coop") Fail("the harness's test keys lost their precedence at a panel press");
}

/* PP3b - THE WORLD SERVER'S DEFAULT --root (store_main.cpp) is <data folder>\worlds, never inside Kenshi's save folder. */
void t_pp3b_store_root_default()
{
    std::string d;
    coopdata::DataDirChoose("", "C:\\Users\\p\\AppData\\Local", "C:\\Users\\p", &d);
    const std::string r = coopdata::WorldsDir(d);
    if (r != "C:\\Users\\p\\AppData\\Local\\kenshi\\Shared Wastelands\\worlds") Fail("the default --root is not <LOCALAPPDATA>\\kenshi\\Shared Wastelands\\worlds: '" + r + "'");
    if (r.find("\\kenshi\\save") != std::string::npos) Fail("the default world folder is inside Kenshi's save folder");
}

void t_pp3_player_cfg_round_trip()
{
    const std::string id = "0123456789abcdef0123456789abcdef";
    coopcfg::CfgFields f;
    long long unknown = 0, bad = 0;
    coopcfg::CfgParseText(coopdata::PlayerCfgText(id, "Beep", "192.168.1.20", 7777), &f, 0, 0, &unknown, &bad);
    if (f.playerId != id) Fail("player.cfg's id did not read back: '" + f.playerId + "'");
    if (f.playerName != "Beep") Fail("player.cfg's name did not read back: '" + f.playerName + "'");
    if (f.hostAddr != "192.168.1.20" || f.hostPort != 7777) Fail("player.cfg's last address did not read back");
    if (unknown != 0 || bad != 0) Fail("player.cfg's own text has unknown or bad lines");
    if (f.role != coopcfg::kCfgSingle) Fail("player.cfg names a role - it must never arm anything");
    const std::string bare = coopdata::PlayerCfgText(id, "", "", 0);
    if (bare.find("host=") != std::string::npos || bare.find("playername=") != std::string::npos) Fail("empty name / port 0 still wrote lines");
    if (bare.find("\r\n") == std::string::npos) Fail("player.cfg is not CRLF");
    if (coopdata::PlayerCfgText(id, "", "", 65535).find("host=0.0.0.0:65535\r\n") == std::string::npos) Fail("the port digits are wrong");
}

void t_pp3_cfg_datadir_key()
{
    coopcfg::CfgFields f;
    long long unknown = 0, bad = 0;
    coopcfg::CfgParseText("datadir = C:\\code\\kenshi-coop\\instances\\C\\data\\\n", &f, 0, 0, &unknown, &bad);
    if (f.dataDir != "C:\\code\\kenshi-coop\\instances\\C\\data") Fail("datadir= was not read: '" + f.dataDir + "'");
    if (unknown != 0 || bad != 0) Fail("datadir= counted as unknown or bad");
    coopcfg::CfgFields g;
    coopcfg::CfgParseText("datadir=relative\\data\n", &g, 0, 0, &unknown, &bad);
    if (!g.dataDir.empty() || bad != 1) Fail("a relative datadir= was not refused as a bad line");
    /* the panel's writer never carries the TEST key */
    coopcfg::CfgFields w; w.role = coopcfg::kCfgHost; w.hostPort = 7777; w.dataDir = "C:\\x\\data";
    if (coopcfg::CfgFormat(w).find("datadir") != std::string::npos) Fail("CfgFormat wrote datadir=");
}

/* hosttoken= (TEST ONLY): exactly 64 lowercase hex digits fill CfgFields::hostToken; anything else is a bad line whose note never
   holds the value, and the field stays empty */
void t_cfg_hosttoken_key()
{
    const std::string good = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    {
        coopcfg::CfgFields f; std::vector<coopcfg::CfgNote> notes; long long unknown = 0, bad = 0;
        coopcfg::CfgParseText("role=host\nhosttoken=" + good + "\n", &f, &notes, 0, &unknown, &bad);
        if (f.hostToken != good) Fail("a 64-hex hosttoken= was not read");
        if (unknown != 0 || bad != 0 || !notes.empty()) Fail("a good hosttoken= counted as unknown or bad, or left a note");
    }
    std::string upper = good; upper[63] = 'F';
    std::string nonHex = good; nonHex[10] = 'g';
    const std::string badVals[] = { good.substr(0, 63), upper, nonHex, good + "0", std::string() };
    for (size_t i = 0; i < sizeof(badVals) / sizeof(badVals[0]); ++i)
    {
        coopcfg::CfgFields f; std::vector<coopcfg::CfgNote> notes; long long unknown = 0, bad = 0;
        coopcfg::CfgParseText("hosttoken=" + badVals[i] + "\n", &f, &notes, 0, &unknown, &bad);
        if (!f.hostToken.empty()) Fail("a bad hosttoken= (case " + I((int)i) + ") filled the field");
        if (bad != 1 || unknown != 0) Fail("a bad hosttoken= (case " + I((int)i) + ") is not one bad line");
        if (notes.size() != 1 || notes[0].bad != 1 || notes[0].text != "hosttoken= is not 64 hexadecimal digits - ignored")
            Fail("a bad hosttoken= (case " + I((int)i) + ") has not its note");
        if (!badVals[i].empty() && !notes.empty() && notes[0].text.find(badVals[i].substr(0, 16)) != std::string::npos)
            Fail("a bad hosttoken='s note holds the value");
    }
    /* the panel's writer never carries the TEST key */
    coopcfg::CfgFields w; w.role = coopcfg::kCfgHost; w.hostPort = 7777; w.hostToken = good;
    if (coopcfg::CfgFormat(w).find("hosttoken") != std::string::npos) Fail("CfgFormat wrote hosttoken=");
}

void t_pp3_test_keys_layered()
{
    const std::string session = "role=client\r\nhost=10.0.0.5:7777\r\nworld=far west\r\nslot=s2\r\n";
    coopcfg::CfgFields f;
    /* the TEST file's keys win; a key it leaves out keeps the session's */
    coopcfg::CfgParseText(coopdata::LayeredSettingsText(session, "role=host\nhost=0.0.0.0:7778\nsaveroot=C:\\i3\\save\n"), &f, 0, 0, 0, 0);
    if (f.role != coopcfg::kCfgHost || f.hostPort != 7778) Fail("the TEST file's role/host did not win over session.cfg");
    if (f.world != "far west" || f.slot != "s2") Fail("a key the TEST file leaves out lost the session's value");
    if (f.saveRoot != "C:\\i3\\save") Fail("the TEST file's saveroot= was not honoured");
    /* no TEST file: the session alone */
    coopcfg::CfgFields s;
    coopcfg::CfgParseText(coopdata::LayeredSettingsText(session, ""), &s, 0, 0, 0, 0);
    if (s.role != coopcfg::kCfgClient || s.hostAddr != "10.0.0.5" || s.hostPort != 7777) Fail("session.cfg alone was not read");
    /* no session: the TEST file alone (the harness's old shape) */
    coopcfg::CfgFields t;
    coopcfg::CfgParseText(coopdata::LayeredSettingsText("", "role=host\nhost=0.0.0.0:7777\nprofile=2\n"), &t, 0, 0, 0, 0);
    if (t.role != coopcfg::kCfgHost || t.profileTest != 2) Fail("the TEST file alone was not read");
    /* a session.cfg with no trailing newline still ends before the TEST file's first key */
    coopcfg::CfgFields u;
    coopcfg::CfgParseText(coopdata::LayeredSettingsText("world=a", "world=b"), &u, 0, 0, 0, 0);
    if (u.world != "b") Fail("the layers ran together: '" + u.world + "'");
}

/* P8J-B2 (bench 2, F738) - saveroot= / storedir=. Absent must be byte-for-byte what it was: no field, no written
   line, and the world folder built from the notebook's top folder equal to the one built from the save root. */
void t_cfg_roots_absent_is_unchanged()
{
    coopcfg::CfgFields f;
    long long unknown = 0, bad = 0;
    coopcfg::CfgParseText("role=host\nhost=0.0.0.0:7777\nslot=s1\nworld=coop\n", &f, 0, 0, &unknown, &bad);
    if (!f.saveRoot.empty() || !f.storeDir.empty()) Fail("a file without the keys produced saveRoot='" + f.saveRoot + "' storeDir='" + f.storeDir + "'");
    if (unknown != 0 || bad != 0) Fail("a file without the keys counted unknown/bad lines");
    const std::string text = coopcfg::CfgFormat(f);
    if (text.find("saveroot") != std::string::npos || text.find("storedir") != std::string::npos)
        Fail("CfgFormat wrote a folder line for fields that carry none:\n" + text);
    const char* roots[] = { "C:\\Users\\player\\AppData\\Local\\kenshi\\save", "C:\\x\\", "D:/a/b" };
    const char* names[] = { "coop", "", "Coop2", "bad name!", "coop2" };
    for (int r = 0; r < 3; ++r)
        for (int n = 0; n < 5; ++n)
        {
            bool fb1 = false, fb2 = false;
            std::string root(roots[r]);
            std::string trimmed(root);
            if (!trimmed.empty() && (trimmed[trimmed.size() - 1] == '\\' || trimmed[trimmed.size() - 1] == '/')) trimmed.erase(trimmed.size() - 1);
            const std::string a = coopworld::WorldDir(root, names[n], &fb1);
            const std::string b = coopworld::WorldDirIn(trimmed + "\\Shared Wastelands\\worlds", names[n], &fb2);
            if (a != b || fb1 != fb2) Fail("WorldDirIn(<root>\\Shared Wastelands\\worlds) differs from WorldDir(<root>): '" + a + "' vs '" + b + "'");
        }
}

void t_cfg_roots_parse_refuse_and_round_trip()
{
    std::vector<coopcfg::CfgNote> notes;
    long long unknown = 0, bad = 0;
    coopcfg::CfgFields f;
    coopcfg::CfgParseText("role=host\n"
                          "SaveRoot = C:\\code\\kenshi-coop\\instances\\i3\\save\\\n"
                          "storedir=\\\\nas\\share\\coop-store\n", &f, &notes, 0, &unknown, &bad);
    if (f.saveRoot != "C:\\code\\kenshi-coop\\instances\\i3\\save") Fail("saveroot was not read (key case, spaces, trailing separator): '" + f.saveRoot + "'");
    if (f.storeDir != "\\\\nas\\share\\coop-store") Fail("a UNC storedir was not read: '" + f.storeDir + "'");
    if (unknown != 0 || bad != 0 || !notes.empty()) Fail("two good folder lines produced notes or bad/unknown counts");

    /* Refusals: each is ONE bad line with a note, and the field stays empty (= the default folder). */
    const char* badVals[] = { "save", "\\save", "C:", "C:\\", "C:save", "C:\\a:b", "C:\\a|b", "C:\\a*b", "", "  " };
    for (int i = 0; i < 10; ++i)
    {
        for (int k = 0; k < 2; ++k)
        {
            const std::string key = k == 0 ? "saveroot" : "storedir";
            std::vector<coopcfg::CfgNote> n2;
            long long u2 = 0, b2 = 0;
            coopcfg::CfgFields g;
            coopcfg::CfgParseText(key + "=" + badVals[i] + "\n", &g, &n2, 0, &u2, &b2);
            if (!g.saveRoot.empty() || !g.storeDir.empty()) Fail(key + "='" + badVals[i] + "' was accepted");
            if (b2 != 1 || n2.size() != 1 || !n2[0].bad) Fail(key + "='" + badVals[i] + "' was not ONE bad line with a note");
        }
    }
    /* A value longer than the cap is refused; one at the cap is taken. */
    std::string atCap = "C:\\";
    atCap += std::string(coopcfg::kCfgRootFieldMax - 3, 'a');
    if (atCap.size() != coopcfg::kCfgRootFieldMax) Fail("the at-cap fixture is not at the cap");
    std::string out = "unchanged";
    if (!coopcfg::CfgRootPathOk(atCap, &out, 0) || out != atCap) Fail("a root exactly at the cap was refused");
    out = "unchanged";
    if (coopcfg::CfgRootPathOk(atCap + "a", &out, 0)) Fail("a root one over the cap was accepted");
    if (out != "unchanged") Fail("a refused root wrote *norm");
    std::string why;
    if (coopcfg::CfgRootPathOk("relative\\x", 0, &why) || why.empty()) Fail("a relative root gave no reason");

    /* The last line of a key wins, like every other key. */
    coopcfg::CfgFields d;
    coopcfg::CfgParseText("saveroot=C:\\one\nsaveroot=C:\\two\n", &d, 0, 0, 0, 0);
    if (d.saveRoot != "C:\\two") Fail("a repeated saveroot did not take the last value: '" + d.saveRoot + "'");

    /* Round trip: what the writer puts down is what the reader takes back. */
    coopcfg::CfgFields w;
    w.role = coopcfg::kCfgHost; w.hostPort = 7778; w.slot = "s1c"; w.world = "coop2";
    w.saveRoot = "C:\\code\\kenshi-coop\\instances\\i3\\save";
    w.storeDir = "C:\\code\\kenshi-coop\\instances\\i3\\save\\coop-store";
    coopcfg::CfgFields back;
    long long b3 = 0;
    coopcfg::CfgParseText(coopcfg::CfgFormat(w), &back, 0, 0, 0, &b3);
    if (back.saveRoot != w.saveRoot || back.storeDir != w.storeDir || b3 != 0)
        Fail("the folder keys did not round-trip: '" + back.saveRoot + "' '" + back.storeDir + "'");
    /* A value the reader would refuse is never written. */
    coopcfg::CfgFields z;
    z.saveRoot = "relative";
    if (coopcfg::CfgFormat(z).find("saveroot") != std::string::npos) Fail("CfgFormat wrote a saveroot the reader refuses");
}

void t_cfg_parser_bounds_and_notes()
{
    std::string text;
    int i;
    for (i = 0; i < 400; ++i) text += "world=w\n";
    coopcfg::CfgFields f;
    int lines = 0;
    coopcfg::CfgParseText(text, &f, 0, &lines, 0, 0);
    if (lines != coopcfg::kCfgMaxLines)
        Fail("the parser read " + I(lines) + " lines out of 400, want the cap " + I(coopcfg::kCfgMaxLines));

    std::string over = "world=";
    over += std::string(600, 'z');
    over += "\n";
    coopcfg::CfgFields g;
    coopcfg::CfgParseText(over, &g, 0, 0, 0, 0);
    if (g.world.size() != coopcfg::kCfgMaxLineChars - 6)
        Fail("a 606-character line was not truncated to " + I((int)coopcfg::kCfgMaxLineChars)
             + " characters; world is " + I((int)g.world.size()) + " long");

    std::vector<coopcfg::CfgNote> notes;
    long long unknown = 0, bad = 0;
    coopcfg::CfgFields h;
    coopcfg::CfgParseText("# a comment\n"
                          "role = HOST\n"
                          "this line has no equals\n"
                          "wibble=3\n"
                          "host=nonsense\n"
                          "slot=s2 ; trailing comment\n"
                          "world=coop\n", &h, &notes, 0, &unknown, &bad);
    if (h.role != coopcfg::kCfgHost) Fail("'role = HOST' was not read as host (spaces and case)");
    if (h.slot != "s2")   Fail("a trailing ';' comment was not stripped: slot='" + h.slot + "'");
    if (h.world != "coop") Fail("world was not read: '" + h.world + "'");
    if (h.hostPort != 0)   Fail("host='nonsense' was accepted");
    if (unknown != 1) Fail("the unknown key 'wibble' was counted " + I((int)unknown) + " times, want 1");
    if (bad != 2)     Fail("the two bad lines were counted " + I((int)bad) + " times, want 2");
    if ((int)notes.size() != 3)
        Fail("the parser produced " + I((int)notes.size()) + " diagnostics, want 3 (one per ignored line)");
    for (i = 0; i < (int)notes.size(); ++i)
        if (notes[i].text.empty() || notes[i].line <= 0)
            Fail("a parser diagnostic has no text or no line number - it cannot be logged usefully");

    /* An empty file and a file that is only comments are BOTH role=single with slot s1 - the defaults
       ConfigLoad has always used. */
    coopcfg::CfgFields e1, e2;
    coopcfg::CfgParseText("", &e1, 0, 0, 0, 0);
    coopcfg::CfgParseText("# nothing\n; nothing\n", &e2, 0, 0, 0, 0);
    if (e1.role != coopcfg::kCfgSingle || e1.slot != "s1") Fail("an empty file did not parse as single/s1");
    if (!CfgSame(e1, e2)) Fail("a comments-only file did not parse the same as an empty one");
}

/* ---------------------------------------------------------------------------------------------
   P8i-b / review-p8i H-3.  The refusal used to be taken AFTER the panel Window was created, from a
   client rectangle only MyGUI could supply, and it left the build-fail streak alone - so the panel was
   built and destroyed at the title pump's ~1,065 Hz for ever, with no message.  These two tests pin the
   two halves of the repair: the answer is now reachable from the OUTER rectangle alone (so it can be
   taken before anything exists), and a refusal advances a streak that gives up at the cap.
   --------------------------------------------------------------------------------------------- */
void t_panel_fit_is_decided_from_the_window_rectangle_alone()
{
    /* The chrome reading, checked against the template's own declared size: Kenshi_WindowCX is
       declared 360 x 320 with Client 354 x 275. */
    if (360 - coopui::kWindowCXChromeW != 354)
        Fail("chrome width " + I(coopui::kWindowCXChromeW) + " does not reproduce the template's 354");
    if (320 - coopui::kWindowCXChromeH != 275)
        Fail("chrome height " + I(coopui::kWindowCXChromeH) + " does not reproduce the template's 275");

    /* Too short: client height 226-45 = 181, one pixel under 13 rows of 14. */
    if (coopui::PanelWindowFits(300, 226))
        Fail("a 300x226 window was accepted; its client is 294x181 and 181/13 = 13 < 14");
    /* One pixel taller is the boundary and must be accepted. */
    if (!coopui::PanelWindowFits(300, 227))
        Fail("a 300x227 window was refused; its client is 294x182 and 182/13 = 14");
    /* Too narrow: client width 225-6 = 219, one under 220. */
    if (coopui::PanelWindowFits(225, 300))
        Fail("a 225x300 window was accepted; its client width is 219 < 220");
    if (!coopui::PanelWindowFits(226, 300))
        Fail("a 226x300 window was refused; its client width is 220");

    /* And the arithmetic the shipped comment claims: the design minimum fits, which is why the
       pre-create branch is unreachable today rather than merely improbable. */
    if (!coopui::PanelWindowFits(260, 240))
        Fail("the design minimum 260x240 was refused; its client is 254x195 and 195/13 = 15");

    /* The client-side test is the one the shipped code ran, said as a multiplication. */
    if (coopui::PanelClientFits(220, 13 * 14 - 1)) Fail("a client of 220x181 was accepted");
    if (!coopui::PanelClientFits(220, 13 * 14))    Fail("a client of 220x182 was refused");
}

void t_panel_refusal_advances_the_streak_and_gives_up_at_the_cap()
{
    const long cap = 5;
    coopui::PanelRefusal r = coopui::PanelNoteRefusal(0, cap);
    /* THE ASSERTION THAT ENCODES THE DEFECT: the pre-fix refusal path incremented nothing at all, so
       the streak stayed 0 and the cap could never be reached. */
    if (r.streak != 1) Fail("the first refusal left the streak at " + I((int)r.streak) + ", expected 1");
    if (r.giveUp != 0) Fail("the first refusal of five gave up already");

    long streak = 0;
    int i;
    int gaveUpAt = 0;
    for (i = 1; i <= (int)cap; ++i)
    {
        r = coopui::PanelNoteRefusal(streak, cap);
        streak = r.streak;
        if (streak != (long)i) Fail("refusal " + I(i) + " produced streak " + I((int)streak));
        if (r.giveUp != 0 && gaveUpAt == 0) gaveUpAt = i;
    }
    if (gaveUpAt != (int)cap)
        Fail("gave up at refusal " + I(gaveUpAt) + ", expected " + I((int)cap));

    /* A cap of 0 or less must not make every first refusal a give-up. */
    r = coopui::PanelNoteRefusal(0, 0);
    if (r.giveUp != 0) Fail("a cap of 0 gave up on the first refusal");
}


/* ------------------------------------------------------------------ O1 (recheck-c2b): the owned-uid mirror.
   IsUidMineAnyThread answers from this table; it must give the std::set's answer through insert, erase,
   re-insert, tombstones in a shared probe chain, and a full table. */
coopown::OwnedMirror g_o1m;
unsigned int g_o1set[40000];   /* O1-b: the owned set a rebuild reads, as a plain list */
void O1Rebuild(coopown::OwnedMirror* m, const unsigned int* set, int n)
{
    coopown::OwnedTable* t = coopown::OwnedMirrorRebuildBegin(m);
    int i;
    for (i = 0; i < n; ++i) coopown::OwnedMirrorRebuildAdd(m, t, set[i]);
    coopown::OwnedMirrorRebuildPublish(m, t);
}
/* kit3 (kit2 review + T522, review D8): exact while nothing was refused and a slot is free; not exact after a refused insert
   until a rebuild; not exact with every slot live. */
void t_kit3_owned_mirror_exact_only_while_nothing_is_missing()
{
    coopown::OwnedMirror* m = &g_o1m;
    coopown::OwnedMirrorReset(m);
    if (!coopown::OwnedMirrorExact(m)) Fail("an empty table is not exact");
    coopown::OwnedMirrorInsert(m, 7);
    if (!coopown::OwnedMirrorExact(m)) Fail("a one-uid table is not exact");
    const long cap = (long)coopown::kOwnedMirrorCap;
    long k;
    for (k = 1; k < cap; ++k) coopown::OwnedMirrorInsert(m, 0x05000000u + (unsigned int)k);
    if (coopown::OwnedMirrorLive(m) != cap) Fail("the table did not fill: live=" + I((int)coopown::OwnedMirrorLive(m)));
    if (coopown::OwnedMirrorExact(m)) Fail("a table with every slot live answered exact");
    if (coopown::OwnedMirrorInsert(m, 0x06000001u) != -1) Fail("an insert into a full table was not refused");
    if (coopown::OwnedMirrorHas(m, 0x06000001u)) Fail("a refused uid answered mine");
    if (coopown::OwnedMirrorExact(m)) Fail("a refused insert answered exact");
    const unsigned int one[2] = { 7u, 0x06000001u };
    O1Rebuild(m, one, 2);
    if (!coopown::OwnedMirrorExact(m)) Fail("the rebuilt table is not exact");
    if (!coopown::OwnedMirrorHas(m, 0x06000001u)) Fail("the rebuild lost the refused uid");
    coopown::OwnedMirrorReset(m);
}
void t_o1_owned_mirror_matches_the_set_through_tombstones_and_full()
{
    coopown::OwnedMirror* m = &g_o1m;
    coopown::OwnedMirrorReset(m);
    if (coopown::OwnedMirrorHas(m, 7)) Fail("an empty table answered mine");
    if (coopown::OwnedMirrorInsert(m, 7) != 1) Fail("first insert not 1");
    if (coopown::OwnedMirrorInsert(m, 7) != 0) Fail("duplicate insert not 0 (a set holds a uid once)");
    if (!coopown::OwnedMirrorHas(m, 7)) Fail("inserted uid not found");
    if (coopown::OwnedMirrorLive(m) != 1) Fail("live=" + I((int)coopown::OwnedMirrorLive(m)) + ", want 1");
    if (coopown::OwnedMirrorErase(m, 7) != 1) Fail("erase not 1");
    if (coopown::OwnedMirrorErase(m, 7) != 0) Fail("second erase not 0");
    if (coopown::OwnedMirrorHas(m, 7)) Fail("erased uid still found");
    if (coopown::OwnedMirrorTombs(m) != 0) Fail("a lone erase left a tombstone (next slot empty: must reclaim)");
    if (coopown::OwnedMirrorInsert(m, 7) != 1 || !coopown::OwnedMirrorHas(m, 7)) Fail("re-insert after erase failed");
    if (coopown::OwnedMirrorInsert(m, 0) != 1 || !coopown::OwnedMirrorHas(m, 0)) Fail("uid 0 is a key like any other");
    if (coopown::OwnedMirrorNeedsRebuild(m)) Fail("a near-empty table asked for a rebuild");

    /* THREE UIDS IN ONE PROBE CHAIN: erase the middle one and the last must still be found past its tombstone. */
    coopown::OwnedMirrorReset(m);
    unsigned int c[3]; int k = 0; unsigned int u;
    const unsigned int home = coopown::OwnedMirrorHome(0x01000001u);
    for (u = 0x01000001u; k < 3 && u < 0x01000001u + 50000000u; ++u)
        if (coopown::OwnedMirrorHome(u) == home) c[k++] = u;
    if (k != 3) { Fail("could not find three colliding uids"); return; }
    for (k = 0; k < 3; ++k) coopown::OwnedMirrorInsert(m, c[k]);
    coopown::OwnedMirrorErase(m, c[1]);
    if (coopown::OwnedMirrorTombs(m) != 1) Fail("middle erase: tombs=" + I((int)coopown::OwnedMirrorTombs(m)) + ", want 1");
    if (!coopown::OwnedMirrorHas(m, c[2])) Fail("a live uid past a tombstone was lost");
    if (coopown::OwnedMirrorHas(m, c[1])) Fail("tombstoned uid still answered mine");
    if (coopown::OwnedMirrorInsert(m, c[1]) != 1 || coopown::OwnedMirrorTombs(m) != 0) Fail("re-insert did not reuse the tombstone");
    if (!coopown::OwnedMirrorHas(m, c[1]) || !coopown::OwnedMirrorHas(m, c[2])) Fail("chain wrong after tombstone reuse");
    if (coopown::OwnedMirrorInsert(m, c[2]) != 0) Fail("a uid live past a tombstone was inserted twice");
    coopown::OwnedMirrorErase(m, c[1]);
    coopown::OwnedMirrorErase(m, c[2]);   /* the chain's tail: reclaims c[2] AND the tombstone before it */
    if (coopown::OwnedMirrorTombs(m) != 0) Fail("tail erase left tombs=" + I((int)coopown::OwnedMirrorTombs(m)) + ", want 0");
    if (!coopown::OwnedMirrorHas(m, c[0]) || coopown::OwnedMirrorLive(m) != 1) Fail("the chain head was lost by the reclaim");

    /* FULL: every slot live. The next insert is refused and counted; everything inserted before is still found. */
    const int cap = (int)coopown::kOwnedMirrorCap;
    coopown::OwnedMirrorReset(m);
    int bad = 0;
    for (k = 0; k < cap; ++k) if (coopown::OwnedMirrorInsert(m, 0x02000000u + (unsigned int)k) != 1) ++bad;
    if (bad != 0) Fail(I(bad) + " inserts refused below capacity");
    if (coopown::OwnedMirrorNeedsRebuild(m)) Fail("a full table with no tombstone and no refusal asked for a rebuild");
    const unsigned int over = 0x03000000u;
    if (coopown::OwnedMirrorInsert(m, over) != -1) Fail("insert into a full table not refused");
    if (m->full != 1 || m->refusedSinceRebuild != 1) Fail("full=" + I((int)m->full) + " refusedSinceRebuild=" + I((int)m->refusedSinceRebuild) + ", want 1 1");
    if (coopown::OwnedMirrorHas(m, over)) Fail("a refused uid answered mine");
    for (k = 0, bad = 0; k < cap; ++k) if (!coopown::OwnedMirrorHas(m, 0x02000000u + (unsigned int)k)) ++bad;
    if (bad != 0) Fail(I(bad) + " uids inserted before the table filled were lost");
    if (!coopown::OwnedMirrorNeedsRebuild(m)) Fail("a refusal did not ask for a rebuild");

    /* O1-b REFUSED RE-INSERT: the set drops uid +5 (its mirror erase leaves a tombstone in a full table) and still
       holds `over`. The rebuild puts `over` in, swaps the published pointer, and leaves the retired table as it was. */
    coopown::OwnedMirrorErase(m, 0x02000005u);
    int n = 0;
    for (k = 0; k < cap; ++k) if (k != 5) g_o1set[n++] = 0x02000000u + (unsigned int)k;
    g_o1set[n++] = over;
    coopown::OwnedTable* before = coopown::OwnedMirrorCur(m);
    if (before != &m->buf[0]) Fail("a reset mirror did not publish buf[0]");
    O1Rebuild(m, g_o1set, n);
    if (coopown::OwnedMirrorCur(m) != &m->buf[1]) Fail("the rebuild did not swap in the spare buffer");
    if (m->rebuilds != 1 || m->refusedSinceRebuild != 0) Fail("rebuilds=" + I((int)m->rebuilds) + " refusedSinceRebuild=" + I((int)m->refusedSinceRebuild) + ", want 1 0");
    if (!coopown::OwnedMirrorHas(m, over)) Fail("the refused uid was not re-inserted by the rebuild");
    if (coopown::OwnedMirrorHas(m, 0x02000005u)) Fail("a uid the set dropped came back in the rebuild");
    for (k = 0, bad = 0; k < n; ++k) if (!coopown::OwnedMirrorHas(m, g_o1set[k])) ++bad;
    if (bad != 0) Fail(I(bad) + " owned uids missing after the rebuild");
    if (coopown::OwnedMirrorLive(m) != cap || coopown::OwnedMirrorTombs(m) != 0) Fail("rebuilt table: live=" + I((int)coopown::OwnedMirrorLive(m)) + " tombs=" + I((int)coopown::OwnedMirrorTombs(m)));
    if (m->full != 1) Fail("a rebuild that fit counted a refusal: full=" + I((int)m->full));
    if (coopown::OwnedMirrorNeedsRebuild(m)) Fail("a freshly rebuilt table with no tombstone asked again");
    /* A reader that loaded the old pointer before the swap still gets the old truth, not garbage. */
    if (!coopown::OwnedTableHas(before, 0x02000006u) || coopown::OwnedTableHas(before, over)) Fail("the retired table changed under a late reader");

    /* O1-b CROWDED: 30000 live, then 5000 erased - tombstones and under 1/4 empty ask for a rebuild that clears them. */
    coopown::OwnedMirrorReset(m);
    for (k = 0; k < 30000; ++k) coopown::OwnedMirrorInsert(m, 0x04000000u + (unsigned int)k);
    if (coopown::OwnedMirrorNeedsRebuild(m)) Fail("a crowded table with no tombstone asked for a rebuild");
    for (k = 0, n = 0; k < 30000; ++k)
    {
        if (k % 6 == 0) coopown::OwnedMirrorErase(m, 0x04000000u + (unsigned int)k);
        else g_o1set[n++] = 0x04000000u + (unsigned int)k;
    }
    if (coopown::OwnedMirrorTombs(m) <= 0) Fail("5000 erases at 91% load left no tombstone");
    if (coopown::OwnedMirrorEmpty(m) * 4 >= cap) Fail("empty=" + I((int)coopown::OwnedMirrorEmpty(m)) + " is not under 1/4");
    if (!coopown::OwnedMirrorNeedsRebuild(m)) Fail("a crowded table with tombstones did not ask for a rebuild");
    O1Rebuild(m, g_o1set, n);
    if (coopown::OwnedMirrorTombs(m) != 0 || coopown::OwnedMirrorLive(m) != n) Fail("crowded rebuild: tombs=" + I((int)coopown::OwnedMirrorTombs(m)) + " live=" + I((int)coopown::OwnedMirrorLive(m)));
    if (coopown::OwnedMirrorEmpty(m) != cap - n) Fail("crowded rebuild: empty=" + I((int)coopown::OwnedMirrorEmpty(m)));
    for (k = 0, bad = 0; k < 30000; ++k)
        if (coopown::OwnedMirrorHas(m, 0x04000000u + (unsigned int)k) != (k % 6 != 0)) ++bad;
    if (bad != 0) Fail(I(bad) + " wrong answers after the crowded rebuild");
    if (coopown::OwnedMirrorNeedsRebuild(m)) Fail("the rebuilt crowded table asked again");
    /* The NEXT rebuild reuses the retired buffer: the two alternate. */
    O1Rebuild(m, g_o1set, n);
    if (coopown::OwnedMirrorCur(m) != &m->buf[0] || m->rebuilds != 2) Fail("the second rebuild did not swap back to buf[0]");
    for (k = 0, bad = 0; k < n; ++k) if (!coopown::OwnedMirrorHas(m, g_o1set[k])) ++bad;
    if (bad != 0) Fail(I(bad) + " owned uids missing after the second rebuild");
}

/* ------------------------------------------------------------------ P8m (H-ghost): the pending-withdraw set.
   THE PRE-PATCH LOGIC IS IN THE SUITE, not in a comment: RetireWithdrawLegacyWouldSend(1) is the old
   withdraw leg's answer for a retired uid, and it is 0 - which is the whole defect.  Every assertion
   below that expects a send is one the pre-patch logic fails. */
void t_retire_withdraw_a_retired_uid_is_owed_an_unload()
{
    int c = -1;
    /* [1] THE ASSERTION THAT FAILS WITHOUT THE FIX.  Pre-patch the withdraw leg reached FindSpawned first
       and a retired object resolves to nothing, so the answer for this uid was "do not send". */
    if (cooprw::RetireWithdrawLegacyWouldSend(1) != 0)
        Fail("[1] the pre-patch predicate for a RETIRED mirror answered "
             + I(cooprw::RetireWithdrawLegacyWouldSend(1)) + ", want 0 - the hole is mis-stated");
    const int afterRetire = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);
    if (afterRetire != cooprw::kPending)
        Fail("[1] retire from absent gave state " + I(afterRetire) + ", want kPending " + I(cooprw::kPending));
    if (c != cooprw::kCountNone)
        Fail("[1] retire moved counter " + I(c) + ", want kCountNone - a retire sends nothing by itself");
    if (cooprw::RetireWithdrawShouldSend(afterRetire) != 1)
        Fail("[1] a retired uid is not owed an UNLOAD (shouldSend "
             + I(cooprw::RetireWithdrawShouldSend(afterRetire)) + "), want 1; the pre-patch answer was "
             + I(cooprw::RetireWithdrawLegacyWouldSend(1)));

    /* [2] the send resolves it exactly once and counts it */
    c = -1;
    const int afterSend = cooprw::RetireWithdrawStep(afterRetire, cooprw::kEvSent, &c);
    if (afterSend != cooprw::kWithdrawn || c != cooprw::kCountSent)
        Fail("[2] send from pending gave state " + I(afterSend) + " counter " + I(c) + ", want kWithdrawn "
             + I(cooprw::kWithdrawn) + " / kCountSent " + I(cooprw::kCountSent));
    if (cooprw::RetireWithdrawShouldSend(afterSend) != 0)
        Fail("[2] a withdrawn uid is still owed an UNLOAD - it would be sent twice");

    /* [3] a SECOND send event over a withdrawn entry must move nothing */
    c = -1;
    const int again = cooprw::RetireWithdrawStep(afterSend, cooprw::kEvSent, &c);
    if (again != cooprw::kWithdrawn || c != cooprw::kCountNone)
        Fail("[3] a repeat send gave state " + I(again) + " counter " + I(c) + ", want kWithdrawn / kCountNone");
}

void t_retire_withdraw_link_down_defers_and_never_consumes()
{
    int c = -1;
    int st = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);

    /* THE RECURRENCE COVER: three refused sends in a row leave the uid owed all three times. */
    int i;
    for (i = 1; i <= 3; ++i)
    {
        c = -1;
        st = cooprw::RetireWithdrawStep(st, cooprw::kEvSendFailed, &c);
        if (st != cooprw::kPending)
            Fail("[1] after refusal " + I(i) + " the state is " + I(st) + ", want kPending "
                 + I(cooprw::kPending) + " - a link that is down consumed the withdrawal");
        if (c != cooprw::kCountDeferredLinkDown)
            Fail("[1] refusal " + I(i) + " moved counter " + I(c) + ", want kCountDeferredLinkDown "
                 + I(cooprw::kCountDeferredLinkDown));
        if (cooprw::RetireWithdrawShouldSend(st) != 1)
            Fail("[1] after refusal " + I(i) + " the uid is no longer owed an UNLOAD");
    }

    /* and the send that finally goes through still counts exactly one sent */
    c = -1;
    st = cooprw::RetireWithdrawStep(st, cooprw::kEvSent, &c);
    if (st != cooprw::kWithdrawn || c != cooprw::kCountSent)
        Fail("[2] the send after three refusals gave state " + I(st) + " counter " + I(c)
             + ", want kWithdrawn / kCountSent");
}

void t_retire_withdraw_restore_cancels_and_a_same_tick_pair_sends_nothing()
{
    int c = -1;
    int st = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);

    /* [1] RESTORE BEFORE THE SEND - cancelled, counted as a cancellation, and the entry is dropped */
    c = -1;
    st = cooprw::RetireWithdrawStep(st, cooprw::kEvRestore, &c);
    if (st != cooprw::kAbsent || c != cooprw::kCountCancelledByRestore)
        Fail("[1] restore over a pending entry gave state " + I(st) + " counter " + I(c)
             + ", want kAbsent " + I(cooprw::kAbsent) + " / kCountCancelledByRestore "
             + I(cooprw::kCountCancelledByRestore));
    if (cooprw::RetireWithdrawShouldSend(st) != 0)
        Fail("[1] a cancelled withdrawal is still owed an UNLOAD - the character came back");
    if (cooprw::RetireWithdrawKeep(st) != 0)
        Fail("[1] a cancelled entry is kept in the set, so it would be re-examined forever");

    /* [2] RETIRE AND RESTORE INSIDE ONE TICK: the drain sees the FINAL state, and it sends nothing */
    int tick = cooprw::kAbsent;
    c = -1; tick = cooprw::RetireWithdrawStep(tick, cooprw::kEvRetire, &c);
    c = -1; tick = cooprw::RetireWithdrawStep(tick, cooprw::kEvRestore, &c);
    if (cooprw::RetireWithdrawShouldSend(tick) != 0)
        Fail("[2] retire+restore in one tick left the uid owed an UNLOAD (state " + I(tick)
             + ") - a live character would be dropped on the peer");

    /* [3] RESTORE AFTER THE SEND is NOT a cancellation - the UNLOAD is already out, and the existing
       announce pass re-announces with a SPAWN.  Counting it as cancelled would break the identity. */
    int post = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);
    c = -1; post = cooprw::RetireWithdrawStep(post, cooprw::kEvSent, &c);
    c = -1; post = cooprw::RetireWithdrawStep(post, cooprw::kEvRestore, &c);
    if (post != cooprw::kAbsent || c != cooprw::kCountNone)
        Fail("[3] restore after a completed send gave state " + I(post) + " counter " + I(c)
             + ", want kAbsent / kCountNone");

    /* [4] and a uid that goes away AGAIN after coming back is a fresh withdrawal */
    c = -1;
    const int reretired = cooprw::RetireWithdrawStep(cooprw::kWithdrawn, cooprw::kEvRetire, &c);
    if (reretired != cooprw::kPending || c != cooprw::kCountNone)
        Fail("[4] re-retiring a withdrawn uid gave state " + I(reretired) + " counter " + I(c)
             + ", want kPending / kCountNone");
}

void t_retire_withdraw_an_unannounced_uid_is_resolved_without_a_send()
{
    int c = -1;
    int st = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);
    c = -1;
    st = cooprw::RetireWithdrawStep(st, cooprw::kEvNotAnnounced, &c);
    if (st != cooprw::kWithdrawn || c != cooprw::kCountNotAnnounced)
        Fail("[1] a uid the peer was never told about gave state " + I(st) + " counter " + I(c)
             + ", want kWithdrawn " + I(cooprw::kWithdrawn) + " / kCountNotAnnounced "
             + I(cooprw::kCountNotAnnounced));
    if (cooprw::RetireWithdrawShouldSend(st) != 0)
        Fail("[1] it is still owed an UNLOAD for a character the peer never received");

    /* THE IDENTITY, driven rather than asserted in a comment: four retirements, four resolutions.
       retired == sent + notAnnounced + cancelledByRestore + pending. */
    int sent = 0, notAnn = 0, cancelled = 0, pending = 0;
    int retired = 0;
    int s1, s2, s3, s4;
    c = -1; s1 = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c); ++retired;
    c = -1; s1 = cooprw::RetireWithdrawStep(s1, cooprw::kEvSent, &c);
    if (c == cooprw::kCountSent) ++sent;
    c = -1; s2 = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c); ++retired;
    c = -1; s2 = cooprw::RetireWithdrawStep(s2, cooprw::kEvNotAnnounced, &c);
    if (c == cooprw::kCountNotAnnounced) ++notAnn;
    c = -1; s3 = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c); ++retired;
    c = -1; s3 = cooprw::RetireWithdrawStep(s3, cooprw::kEvRestore, &c);
    if (c == cooprw::kCountCancelledByRestore) ++cancelled;
    c = -1; s4 = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c); ++retired;
    c = -1; s4 = cooprw::RetireWithdrawStep(s4, cooprw::kEvSendFailed, &c);   /* an ATTEMPT, not a resolution */
    pending = cooprw::RetireWithdrawShouldSend(s1) + cooprw::RetireWithdrawShouldSend(s2)
            + cooprw::RetireWithdrawShouldSend(s3) + cooprw::RetireWithdrawShouldSend(s4);
    if (retired != sent + notAnn + cancelled + pending)
        Fail("[2] the identity does not close: retired " + I(retired) + " != sent " + I(sent)
             + " + notAnnounced " + I(notAnn) + " + cancelledByRestore " + I(cancelled)
             + " + pending " + I(pending));
    if (pending != 1)
        Fail("[2] pending is " + I(pending) + ", want 1 - the refused send must still be owed");
}

/* ================= B10 (design-e46-store 3.8) - THE OWNER FIELD, FORMAT 6, AND THE TWO ROTATION
   DECISIONS review-p8l LEFT OWED. Every input is a literal; nothing here needs a game, a relay, a socket
   or a locked file, which is the whole point - the H-1 case takes a file held open on a real disk to
   reproduce and is one line of arithmetic here. ============================================== */

/* A v5 line as a pre-B10 folder holds it: the owner field carries a SESSION ROLE. */
std::string V5LineWithOwner(unsigned int owner)
{
    coopstore::MetaLine m;
    m.version = 5; m.writtenAt = 1700000000LL; m.posAt = 1700000001LL; m.owner = owner;
    m.x = 1.5f; m.y = 2.5f; m.z = 3.5f;
    m.squadSid = "Holy Nation Outlaws_5"; m.factionName = "Holy Nation"; m.worldId = "Fac_1"; m.town = "Squin";
    m.len = 9; m.crc = coopstore::Crc32("123456789", 9);
    std::string out;
    if (!coopstore::MetaEncodeV5(m, &out)) Fail("could not build a v5 fixture line");
    return out;
}

void t_b10_v5_line_upgrades_to_v6_with_an_unknown_owner()
{
    /* design 3.8 (1) and (2). owner = 1 (the host wrote it) and owner = 2 (the client did) both become
       kOwnerUnknown, because a ROLE CANNOT BE MAPPED TO A SLOT. The upgraded line re-parses identically,
       and A SECOND LOAD DOES NOT UPGRADE AGAIN - the migration that keeps migrating is review-p8b C-1's
       shape and indexUpgradedV5toV6 = 0 on the second start is the verdict for it. */
    unsigned int roles[2]; roles[0] = 1u; roles[1] = 2u;
    for (int i = 0; i < 2; ++i)
    {
        coopstore::MetaLine parsed;
        if (!coopstore::MetaParse(V5LineWithOwner(roles[i]), &parsed)) { Fail("a v5 line was REFUSED"); return; }
        if (parsed.version != 5) Fail("a v5 line did not read as version 5");
        if (parsed.owner != roles[i]) Fail("a v5 line's owner was not read as found");

        coopstore::MetaLine upgraded = parsed;
        upgraded.owner = coopstore::OwnerAfterUpgrade(parsed.version, parsed.owner);
        if (upgraded.owner != coopstore::kOwnerUnknown)
            Fail("a v5 line's session role " + I((int)roles[i]) + " was carried into v6 as a SLOT instead of"
                 " becoming kOwnerUnknown - owner=" + I((int)upgraded.owner));

        std::string v6;
        if (!coopstore::MetaEncodeV6(upgraded, &v6)) { Fail("encode refused the upgraded line"); return; }
        if (v6.compare(0, 3, "v6\t") != 0) Fail("the upgraded line is not tagged v6: " + v6);

        coopstore::MetaLine back;
        if (!coopstore::MetaParse(v6, &back)) { Fail("the upgraded v6 line does not parse: " + v6); return; }
        if (back.version != 6) Fail("the upgraded line did not read as version 6");
        if (back.owner != coopstore::kOwnerUnknown) Fail("the v6 line's owner did not round-trip as unknown");
        if (back.len != parsed.len || back.crc != parsed.crc)
            Fail("the S1 length/checksum fields did not survive the 5 -> 6 upgrade");
        if (back.squadSid != parsed.squadSid || back.factionName != parsed.factionName ||
            back.worldId != parsed.worldId || back.town != parsed.town || back.posAt != parsed.posAt)
            Fail("a field other than owner changed across the 5 -> 6 upgrade");

        /* the second load: the line is v6, so nothing is owed and nothing is written again */
        if (coopstore::OwnerAfterUpgrade(back.version, back.owner) != back.owner)
            Fail("a v6 line was migrated a SECOND time - the upgrade is not idempotent");
    }
}

void t_b10_v2_v3_v4_chain_to_v6_keeps_the_payload_fields()
{
    /* design 3.8 (2), the older half: a v2/v3/v4 line carries no length and no checksum, so the upgrade
       fills them from the payload as found - and it is a ROLE in `owner` just as much as a v5 line is. */
    const char* tags[3]; tags[0] = "v2"; tags[1] = "v3"; tags[2] = "v4";
    for (int i = 0; i < 3; ++i)
    {
        std::string line = std::string(tags[i]) + "\t1700000000\t1\t1\t2\t3\tsid\tfac\tFac_7\t1700000001\tSquin\n";
        coopstore::MetaLine m;
        if (!coopstore::MetaParse(line, &m)) { Fail(std::string("a ") + tags[i] + " line was REFUSED"); return; }
        if (m.len != 0 || m.crc != 0) Fail("a pre-v5 line came back carrying a length or a checksum");
        m.len = 9; m.crc = coopstore::Crc32("123456789", 9);          /* the payload as found */
        m.owner = coopstore::OwnerAfterUpgrade(m.version, m.owner);   /* the role goes */
        std::string v6;
        if (!coopstore::MetaEncodeV6(m, &v6)) { Fail("encode refused the chained line"); return; }
        coopstore::MetaLine back;
        if (!coopstore::MetaParse(v6, &back)) { Fail("the chained v6 line does not parse"); return; }
        if (back.version != 6) Fail("the chained line is not v6");
        if (back.owner != coopstore::kOwnerUnknown) Fail(std::string("a ") + tags[i] + " role survived into v6");
        if (back.len != 9 || back.crc != coopstore::Crc32("123456789", 9))
            Fail("the length/checksum filled in by the upgrade did not survive the encode");
        if (back.town != "Squin") Fail("the v4 home town did not survive the chain");
    }
}

void t_b10_v6_round_trips_every_owner_value()
{
    /* design 3.8 (3): 0 is a REAL SLOT and must round-trip as one; 1023 is the top of the numbering; the
       sentinel must come back as the sentinel and not as 1023, 0 or a refusal. */
    unsigned int vals[3]; vals[0] = 0u; vals[1] = 1023u; vals[2] = coopstore::kOwnerUnknown;
    for (int i = 0; i < 3; ++i)
    {
        coopstore::MetaLine m;
        m.version = 6; m.writtenAt = 5; m.posAt = 6; m.owner = vals[i];
        m.squadSid = "s"; m.factionName = "f"; m.worldId = "Fac_2"; m.town = "t"; m.len = 0; m.crc = 0;
        std::string line;
        if (!coopstore::MetaEncodeV6(m, &line)) { Fail("encode refused a clean v6 line"); return; }
        coopstore::MetaLine back;
        if (!coopstore::MetaParse(line, &back)) { Fail("parse refused the v6 line just encoded: " + line); return; }
        if (back.owner != vals[i]) Fail("owner " + I((int)vals[i]) + " did not round-trip through v6: " + line);
    }
}

void t_b10_a_bad_owner_field_is_refused_not_zeroed()
{
    /* design 3.8 (4). std::atoi read "1024x" as 1024, "" as 0 and "-1" as -1 -> 0xFFFFFFFF, and 0 IS A REAL
       SLOT: a silent zero makes some game the recorded author of a record it never wrote. */
    const char* bad[6]; bad[0] = ""; bad[1] = "abc"; bad[2] = "65520"; bad[3] = "-1"; bad[4] = "4294967294"; bad[5] = "12x";
    for (int i = 0; i < 6; ++i)
    {
        unsigned int got = 12345u;
        if (coopstore::MetaOwnerFieldValid(bad[i], &got))
            Fail(std::string("an owner field of '") + bad[i] + "' was ACCEPTED (as " + I((int)got) + ")");
        std::string line = std::string("v6\t1\t") + bad[i] + "\t0\t0\t0\tsid\tfac\tFac_3\t2\ttown\t0\t00000000\n";
        coopstore::MetaLine m;
        if (coopstore::MetaParse(line, &m))
            Fail(std::string("a v6 line whose owner field is '") + bad[i] + "' PARSED, with owner=" + I((int)m.owner));
    }
    /* M1-b (review-m1 M4): the ceiling is the lifetime limit's last number, 65519; 40000 is a slot the old 1023
       ceiling refused, and 65520 (one past the numbering) is in bad[] above. */
    const char* good[4]; good[0] = "0"; good[1] = "65519"; good[2] = "4294967295"; good[3] = "40000";
    unsigned int want[4]; want[0] = 0u; want[1] = 65519u; want[2] = coopstore::kOwnerUnknown; want[3] = 40000u;
    for (int i = 0; i < 4; ++i)
    {
        unsigned int got = 7u;
        if (!coopstore::MetaOwnerFieldValid(good[i], &got))
            Fail(std::string("a legal owner field '") + good[i] + "' was refused");
        else if (got != want[i])
            Fail(std::string("owner field '") + good[i] + "' read as " + I((int)got));
    }
}

void t_b10_translate_predicate_and_every_one_of_its_four_exits()
{
    /* design 3.8 (5), and the coverage leg with it: every branch of the decision must be REACHED by the
       sweep, so an arm cannot be deleted without the suite noticing (the P8l lesson from ChooseVersion's
       sites). ShouldTranslate is OwnerTranslateDecide with haveRecord = 1 and "anything but skipOwn". */
    if (coopstore::OwnerTranslateDecide(1, 1u, 1u) != coopstore::kOwnerSkipOwn)
        Fail("(1,1) - my own record - was translated");
    if (coopstore::OwnerTranslateDecide(1, 1u, 2u) != coopstore::kOwnerTranslateOther)
        Fail("(1,2) - another game's record - was NOT translated");
    if (coopstore::OwnerTranslateDecide(1, coopstore::kOwnerUnknown, 1u) != coopstore::kOwnerTranslateUnknown)
        Fail("(UNKNOWN,1) - an unknown writer - was NOT translated");
    if (coopstore::OwnerTranslateDecide(1, 1u, coopstore::kOwnerUnknown) != coopstore::kOwnerTranslateUnknown)
        Fail("(1,UNKNOWN) - I have no slot - was NOT translated");
    if (coopstore::OwnerTranslateDecide(1, coopstore::kOwnerUnknown, coopstore::kOwnerUnknown) != coopstore::kOwnerTranslateUnknown)
        Fail("(UNKNOWN,UNKNOWN) was NOT translated");
    if (coopstore::OwnerTranslateDecide(0, 1u, 1u) != coopstore::kOwnerNoRecord)
        Fail("a zone with NO record here did not answer noRecord");

    /* THE COVERAGE LEG. 0 and 1023 are the ends of the numbering and 0 is the value a silent-zero bug
       produces, so it is swept as a real slot on both sides. */
    int seen[4]; int i;
    for (i = 0; i < 4; ++i) seen[i] = 0;
    unsigned int vals[4]; vals[0] = 0u; vals[1] = 1u; vals[2] = 1023u; vals[3] = coopstore::kOwnerUnknown;
    for (int h = 0; h < 2; ++h)
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
            {
                const int v = coopstore::OwnerTranslateDecide(h, vals[a], vals[b]);
                if (v < 0 || v > 3) { Fail("OwnerTranslateDecide answered outside its enum"); return; }
                seen[v] = 1;
                if (h == 1 && vals[a] != coopstore::kOwnerUnknown && vals[b] != coopstore::kOwnerUnknown)
                {
                    const int want = (vals[a] == vals[b]) ? coopstore::kOwnerSkipOwn : coopstore::kOwnerTranslateOther;
                    if (v != want) Fail("two known slots did not decide by equality");
                }
            }
    for (i = 0; i < 4; ++i) if (!seen[i]) Fail("OwnerTranslateDecide exit " + I(i) + " was never reached by the sweep");
}

void t_b10_owner_field_from_slot()
{
    /* design 3.8 (6). A slot below 0 is "no WELCOME yet, or the link is down"; a slot outside the numbering
       is a number the notebook never gave us. Neither may be published as authorship. */
    if (coopstore::OwnerFieldFromSlot(-1) != coopstore::kOwnerUnknown) Fail("slot -1 did not answer unknown");
    if (coopstore::OwnerFieldFromSlot(0) != 0u)                        Fail("slot 0 did not answer 0 - it is a REAL slot");
    if (coopstore::OwnerFieldFromSlot(5) != 5u)                        Fail("slot 5 did not answer 5");
    /* M1-b (review-m1 M4): the numbering is the notebook's lifetime limit, not 1024 - a slot of 40000 is a real
       writer and round-trips as itself; 65520 is one past the last number and reads as unknown. */
    if (coopstore::kMaxOwnerSlot != (unsigned int)(coopstore::kSlotLifetimeMax - 1)) Fail("kMaxOwnerSlot is not the lifetime limit's last number");
    if (coopstore::OwnerFieldFromSlot(1024) != 1024u)                  Fail("slot 1024 did not answer 1024");
    if (coopstore::OwnerFieldFromSlot(40000) != 40000u)                Fail("slot 40000 did not answer 40000");
    { unsigned int back = 0u; if (!coopstore::MetaOwnerFieldValid("40000", &back) || back != coopstore::OwnerFieldFromSlot(40000)) Fail("slot 40000 did not round-trip through the file's owner field"); }
    if (coopstore::OwnerFieldFromSlot(65519) != 65519u)                Fail("slot 65519 did not answer 65519");
    if (coopstore::OwnerFieldFromSlot(65520) != coopstore::kOwnerUnknown) Fail("a slot above the numbering was published as authorship");
    /* and every value it can produce must survive the wire's u32 -> int -> u32 trip (session.cpp hands out
       `int owner`, so kOwnerUnknown reads as -1 there; that is deliberate and must not be "fixed") */
    const int asInt = (int)coopstore::OwnerFieldFromSlot(-1);
    if ((unsigned int)asInt != coopstore::kOwnerUnknown) Fail("kOwnerUnknown did not survive the u32 -> int -> u32 trip");
}

void t_b10_rotation_tells_a_locked_source_from_a_locked_destination()
{
    /* review-p8l H-1. P8l refused EVERY failed meta rename, so a DESTINATION-locked <id>.meta.prev turned
       away a position update and a new squad that P8d would have committed, and served the old record from
       memory. The source-locked refusal is right and stays. */
    if (coopstore::RotateMetaFailCause(coopstore::kFileUnreadable, coopstore::kFileReadable) != coopstore::kRotCauseSourceLocked)
        Fail("an unreadable <id>.meta was not read as SOURCE-locked");
    if (coopstore::RotateMetaFailCause(coopstore::kFileReadable, coopstore::kFileUnreadable) != coopstore::kRotCauseDestLocked)
        Fail("a readable <id>.meta with an unreadable <id>.meta.prev was not read as DESTINATION-locked");
    if (coopstore::RotateMetaFailCause(coopstore::kFileReadable, coopstore::kFileAbsent) != coopstore::kRotCauseUnknown)
        Fail("a rename that failed with neither file shut was not read as unknown");
    if (coopstore::RotateMetaFailCause(coopstore::kFileUnreadable, coopstore::kFileUnreadable) != coopstore::kRotCauseSourceLocked)
        Fail("both files shut must be SOURCE-locked - the source is the one that fails the write at the end");

    if (coopstore::RotateMetaFailAction(coopstore::kRotCauseDestLocked) != coopstore::kRotActionProceedNoRotate)
        Fail("a DESTINATION-locked <id>.meta.prev still REFUSES the write - that is the P8l defect H-1 names");
    if (coopstore::RotateMetaFailAction(coopstore::kRotCauseSourceLocked) != coopstore::kRotActionRefuse)
        Fail("a SOURCE-locked <id>.meta no longer refuses - review-p8d C-2 would be back");
    if (coopstore::RotateMetaFailAction(coopstore::kRotCauseUnknown) != coopstore::kRotActionRefuse)
        Fail("a rename that failed for an unknown reason proceeded - a write may not rest on a fact we do not have");

    /* COVERAGE: every cause and every action reached over all nine state pairs. */
    int cause[3], act[2]; int i;
    for (i = 0; i < 3; ++i) cause[i] = 0;
    for (i = 0; i < 2; ++i) act[i] = 0;
    int states[3]; states[0] = coopstore::kFileAbsent; states[1] = coopstore::kFileUnreadable; states[2] = coopstore::kFileReadable;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
        {
            const int c = coopstore::RotateMetaFailCause(states[a], states[b]);
            if (c < 0 || c > 2) { Fail("RotateMetaFailCause answered outside its enum"); return; }
            cause[c] = 1;
            const int v = coopstore::RotateMetaFailAction(c);
            if (v < 0 || v > 1) { Fail("RotateMetaFailAction answered outside its enum"); return; }
            act[v] = 1;
        }
    for (i = 0; i < 3; ++i) if (!cause[i]) Fail("RotateMetaFailCause arm " + I(i) + " was never reached");
    for (i = 0; i < 2; ++i) if (!act[i]) Fail("RotateMetaFailAction arm " + I(i) + " was never reached");
}

void t_b10_a_failed_rollback_never_also_reads_as_a_clean_one()
{
    /* review-p8l H-2. ++g_rotateRolledBack sat OUTSIDE the rollback-failure block, so a failed rollback
       printed rolledBack=1,rollbackFailed=1 - the one state that must not read clean reading as an ordinary
       rollback - and it fired when metaMoved was 0, i.e. when NOTHING had been rotated. */
    if (coopstore::RotateFailBooking(0, 1) != coopstore::kRotBookNothing)
        Fail("a payload rename that failed with NOTHING rotated was booked as a rollback");
    if (coopstore::RotateFailBooking(0, 0) != coopstore::kRotBookNothing)
        Fail("nothing rotated must book nothing whatever the rollback flag says - there was no rollback to make");
    if (coopstore::RotateFailBooking(1, 1) != coopstore::kRotBookRolledBack)
        Fail("a rollback that succeeded was not booked as rolledBack");
    if (coopstore::RotateFailBooking(1, 0) != coopstore::kRotBookRollbackFailed)
        Fail("a rollback that FAILED was not booked as rollbackFailed");
    /* the exclusivity is the finding: exactly one counter moves, so rolledBack and rollbackFailed can never
       both be 1 for one rotation */
    int seen[3]; int i;
    for (i = 0; i < 3; ++i) seen[i] = 0;
    for (int mm = 0; mm < 2; ++mm)
        for (int ok = 0; ok < 2; ++ok)
        {
            const int b = coopstore::RotateFailBooking(mm, ok);
            if (b < 0 || b > 2) { Fail("RotateFailBooking answered outside its enum"); return; }
            seen[b] = 1;
        }
    for (i = 0; i < 3; ++i) if (!seen[i]) Fail("RotateFailBooking arm " + I(i) + " was never reached");
}

void t_b10_the_uniques_retry_republishes_the_whole_merged_map()
{
    /* review-p8l M-2. P8l put back on the wire only the rows the merge REFUSED. The rows the FILE held were
       never sent, and a game that connected while uniques.txt was shut had been handed an EMPTY unique map
       in its WELCOME and was never corrected. This models one recovery with literals: three rows on disk
       (one of them DEAD), two changes held in memory (one of them a revival of that dead row). */
    const int diskStates[3] = { 0, 1, 2 };            /* row A is DEAD, rows B and C are alive */
    int mergedRows = 3, refusedRows = 0, insertedRows = 0;

    /* the held change for row A revives a DEAD character: refused, and row A stays as the file has it */
    if (coopmerge::UniqueMergeDecide(1, diskStates[0], 2) != coopmerge::kMergeKeepLoaded) Fail("the dead row was revived");
    else ++refusedRows;
    /* the held change for a row the file never had: inserted */
    if (coopmerge::UniqueMergeDecide(0, 1, 2) != coopmerge::kMergeInsertDeferred) Fail("a new row was not inserted");
    else { ++insertedRows; ++mergedRows; }

    const int republished = mergedRows;    /* the rule under test */
    int counted = 0;
    for (int i = 0; i < mergedRows; ++i)
    {
        const int wasRefused = (i == 0) ? 1 : 0;
        if (coopmerge::UniqueRepublishRow(1, wasRefused)) ++counted;
    }
    if (counted != republished)
        Fail("the republish rule dropped a row of the merged map: " + I(counted) + " of " + I(republished));
    if (counted == refusedRows)
        Fail("the retry republished only the " + I(refusedRows) + " REFUSED row(s) of a " + I(mergedRows)
             + "-row merged map - the rows the file held are never sent, and a game that joined during the"
               " deferral keeps the EMPTY unique map its WELCOME gave it (review-p8l M-2)");
    if (!coopmerge::UniqueRepublishRow(1, 0))
        Fail("a row the merge did NOT refuse is not republished - that is exactly the P8l defect");
    if (coopmerge::UniqueRepublishRow(0, 1))
        Fail("a row that is not in the merged map was published");
    if (insertedRows != 1) Fail("the fixture did not exercise an insert");
}


/* ================= B10 (audit C11, design-e46-store 3.4) - THE ZONE FILE'S WRITER ==================== */

void t_b10_c11_the_zone_file_writer_follows_the_ladder()
{
    const long long kGrace = 60000;   /* coop::kNoNotebookRefuseMs */

    /* THE PRE-PATCH ASSERTION - the one-sided role test. What stood in ZoneWriteIsMine was
           if ((g_sessionLinkedCached == 0) || net::SessionIsHost()) return true;
       so the session HOST wrote every zone file in the world, including the sector the notebook had just
       named the OTHER game the holder of. There is no session role in these inputs at all now: the holder
       decides, and this case answers HELD on both games. */
    int path = -1, rung = -1, spans = -1;
    if (coopwriter::ZoneWriterDecide(0, 1, 0, 1, 1, 1, 1, 0, kGrace, &path, &rung, &spans) != coopwriter::kWrHeld)
        Fail("the notebook named the OTHER game the holder and this game still wrote the zone file - that is"
             " the pre-B10 rule `no session or I am the host, so I write every zone file` (audit C11)");
    if (path != coopwriter::kZonePathHolderOther) Fail("a held sector did not take the holderOther path");
    if (rung != coopwriter::kRungNone)            Fail("a held sector entered the ladder - the short-circuit is above it");

    /* the notebook named THIS game the holder: written, and again no ladder */
    if (coopwriter::ZoneWriterDecide(0, 0, 1, 0, 0, 0, 1, 0, kGrace, &path, &rung, &spans) != coopwriter::kWrMine)
        Fail("the notebook named this game the holder and the zone file was not written");
    if (path != coopwriter::kZonePathHolderMine) Fail("a held-by-me sector did not take the holderMine path");
    if (rung != coopwriter::kRungNone)           Fail("a held-by-me sector entered the ladder");

    /* a never-co-op save is vanilla and writes its own world, with no ladder and no notebook */
    if (coopwriter::ZoneWriterDecide(1, 1, 0, 0, 1, 0, 1, 999999LL, kGrace, &path, &rung, &spans) != coopwriter::kWrMine)
        Fail("a never-co-op save was refused its own zone file");
    if (path != coopwriter::kZonePathSingle) Fail("a never-co-op save did not take the single path");

    /* WITH NOBODY NAMED, IT IS THE LADDER AND NOTHING ELSE. Every answer below must equal WriterLadder's
       own answer for the same inputs with forBox = 1 - that is what "no second copy" means, and it is
       swept rather than asserted once. */
    int states[3]; states[0] = -1; states[1] = 0; states[2] = 1;
    int peers[3];  peers[0]  = -1; peers[1]  = 0; peers[2]  = 1;
    long long downs[3]; downs[0] = 0; downs[1] = kGrace - 1; downs[2] = kGrace + 1;
    int seenPath[coopwriter::kZonePathCount]; int i;
    for (i = 0; i < coopwriter::kZonePathCount; ++i) seenPath[i] = 0;
    int seenRung[coopwriter::kRungCount];
    for (i = 0; i < coopwriter::kRungCount; ++i) seenRung[i] = 0;
    for (int h = 0; h < 3; ++h)
      for (int m = 0; m < 3; ++m)
        for (int lh = 0; lh < 2; ++lh)
          for (int pk = 0; pk < 3; ++pk)
            for (int lo = 0; lo < 2; ++lo)
              for (int sd = 0; sd < 2; ++sd)
                for (int dm = 0; dm < 3; ++dm)
                {
                    int p = -1, r = -1, sp = 0;
                    const int v = coopwriter::ZoneWriterDecide(0, states[h], states[m], lh, peers[pk], lo, sd,
                                                               downs[dm], kGrace, &p, &r, &sp);
                    if (p < 0 || p >= coopwriter::kZonePathCount) { Fail("ZoneWriterDecide answered outside its path enum"); return; }
                    if (r < 0 || r >= coopwriter::kRungCount)     { Fail("ZoneWriterDecide answered outside the rung enum"); return; }
                    seenPath[p] = 1; seenRung[r] = 1;
                    if (p == coopwriter::kZonePathSingle) Fail("a co-op save took the never-co-op path");
                    if (p == coopwriter::kZonePathLadder)
                    {
                        int lr = -1, ls = 0;
                        const int lv = coopwriter::WriterLadder(lh, peers[pk], lo, sd, downs[dm], kGrace, 1, &lr, &ls);
                        if (lv != v || lr != r || ls != sp)
                            Fail("the zone file did NOT get the same answer as the ladder for the same inputs -"
                                 " that is a second copy of the rule (audit C11 / design-e46-store 3.4)");
                    }
                    else if (r != coopwriter::kRungNone) Fail("a short-circuited zone save reported a rung");
                    /* decision 44 reaches a zone file exactly as it reaches a chest */
                    if (p == coopwriter::kZonePathLadder && downs[dm] > kGrace && v != coopwriter::kWrRefuse)
                        Fail("the notebook was down past the grace and the zone file was still decided (decision 44)");
                }
    /* COVERAGE: three of the four paths are reachable with roleIsSingle = 0, and the single path was taken
       by the case above; every rung the ladder can answer with must be reached by this sweep. */
    for (i = 1; i < coopwriter::kZonePathCount; ++i)
        if (!seenPath[i]) Fail("ZoneWriterDecide path " + I(i) + " was never reached by the sweep");
    for (i = 1; i < coopwriter::kRungCount; ++i)
        if (!seenRung[i]) Fail("ZoneWriterDecide rung " + I(i) + " was never reached by the sweep");
    if (!seenRung[coopwriter::kRungRefusedNoNotebook])
        Fail("no zone save was refused for a missing notebook - decision 44 does not reach the zone file");
}

/* ================= B10-b (review-b10 M-1, M-3, M-7) =================
 * WHAT EACH OF THESE WOULD HAVE CAUGHT ON THE PRE-B10-b CODE. Two of the three functions did not exist
 * before this build, so "fails without the patch" means the pre-patch RULE expressed as its equivalent -
 * the same convention the B9 ladder tests above state for themselves.
 */

/* M-1. THE UNKNOWN WRITER SURVIVES AN INDEX LINE. The plugin's own loader read this field with std::atoi,
   which cannot hold 4294967295 and saturates to 2147483647 - so an unknown writer came back as a real
   (enormous) slot and the translation decided "another game wrote this" instead of "unknown". The plugin
   now calls MetaOwnerFieldValid, the function this test sweeps, so there is one owner parse and not two.
   PRE-PATCH EQUIVALENT: with atoi in place, `parsed.owner == kOwnerUnknown` below is 2147483647 !=
   4294967295 and the assertion "a v6 line's unknown owner did not round-trip" fires. */
void t_b10b_an_unknown_owner_round_trips_through_the_index_line()
{
    coopstore::MetaLine m;
    m.version = 6; m.writtenAt = 1700000000LL; m.posAt = 1700000001LL;
    m.owner = coopstore::kOwnerUnknown;
    m.x = 1.5f; m.y = 2.5f; m.z = 3.5f;
    m.squadSid = "Holy Nation Outlaws_5"; m.factionName = "Holy Nation"; m.worldId = "Fac_1"; m.town = "Squin";
    m.len = 9; m.crc = coopstore::Crc32("123456789", 9);
    std::string line;
    if (!coopstore::MetaEncodeV6(m, &line)) { Fail("could not build a v6 fixture line"); return; }
    if (line.find("\t4294967295\t") == std::string::npos)
        Fail("the v6 fixture did not carry 4294967295 as its owner field - line was: " + line);

    /* the field on its own, through the one parse both programs now make */
    unsigned int got = 0;
    if (!coopstore::MetaOwnerFieldValid("4294967295", &got))
        Fail("MetaOwnerFieldValid REFUSED the unknown-owner field");
    if (got != coopstore::kOwnerUnknown)
        Fail("the unknown-owner field did not parse as unknown - got " + I((int)got));

    /* and the whole line */
    coopstore::MetaLine parsed;
    if (!coopstore::MetaParse(line, &parsed)) { Fail("a v6 line with an unknown owner was REFUSED"); return; }
    if (parsed.owner != coopstore::kOwnerUnknown)
        Fail("a v6 line's unknown owner did not round-trip - got " + I((int)parsed.owner));
    if ((int)parsed.owner != -1)
        Fail("kOwnerUnknown did not read back as -1 through the int the record decoders hand out");
    /* and the translation therefore answers UNKNOWN, not "a different game wrote this" */
    if (coopstore::OwnerTranslateDecide(1, parsed.owner, 0u) != coopstore::kOwnerTranslateUnknown)
        Fail("an unknown writer was translated as a KNOWN other slot");
    /* the neighbouring values still refuse, so this is not a hole */
    if (coopstore::MetaOwnerFieldValid("4294967294", &got))
        Fail("4294967294 - one below unknown and far above the numbering - was accepted as a slot");
    if (!coopstore::MetaOwnerFieldValid("65519", &got) || got != 65519u)   /* M1-b: the highest real slot is the lifetime limit's last number */
        Fail("the highest real slot stopped parsing");
}

/* M-3. THE PUSH UP STAYS OWED WHILE THE LINK IS DOWN, AND IS DISARMED ONLY BY RUNNING.
   PRE-PATCH EQUIVALENT: the arm was `if (!pending) return; pending = 0; if (!linkUp) return;` - it wrote
   pendingAfter = 0 on every path, so the assertion "the push was DISARMED while the link was down" below
   fires on the link-down row. */
void t_b10b_the_repush_stays_owed_while_the_link_is_down()
{
    int after = -1;
    if (coopstore::RepushDecide(0, 0, &after) != coopstore::kRepushIdle) Fail("nothing armed did not read as idle (link down)");
    if (after != 0) Fail("an unarmed push came back armed (link down)");
    after = -1;
    if (coopstore::RepushDecide(0, 1, &after) != coopstore::kRepushIdle) Fail("nothing armed did not read as idle (link up)");
    if (after != 0) Fail("an unarmed push came back armed (link up)");
    after = -1;
    if (coopstore::RepushDecide(1, 0, &after) != coopstore::kRepushDeferLinkDown) Fail("an armed push with the link down did not defer");
    if (after != 1) Fail("the push was DISARMED while the link was down - it would never be retried");
    after = -1;
    if (coopstore::RepushDecide(1, 1, &after) != coopstore::kRepushRun) Fail("an armed push with the link up did not run");
    if (after != 0) Fail("the push stayed armed after it ran - it would run again every tick");
    /* the recurrence, stated as the loop it actually is: three link-down ticks then one with a link, and
       the push runs exactly once and is owed until it does. */
    int pending = 1, ran = 0, deferred = 0, tick;
    for (tick = 0; tick < 4; ++tick)
    {
        const int act = coopstore::RepushDecide(pending, (tick == 3) ? 1 : 0, &pending);
        if (act == coopstore::kRepushRun) ++ran;
        if (act == coopstore::kRepushDeferLinkDown) ++deferred;
    }
    if (deferred != 3) Fail("the link-down ticks were not all counted - deferred=" + I(deferred));
    if (ran != 1) Fail("the push did not run exactly once when the link came back - ran=" + I(ran));
    if (pending != 0) Fail("the push stayed owed after it ran");
}

/* M-7. A VERSION PAIR DECIDES THE HANDSHAKE, AND THE REFUSAL SAYS WHICH TWO NUMBERS DISAGREED.
   PRE-PATCH EQUIVALENT: the relay's rule was "accept always" (it logged MISMATCH and then WELCOMEd and
   pushed), so the assertion "a mismatched pair was ACCEPTED" fires on every unequal row. */
void t_b10b_a_store_protocol_mismatch_is_refused_in_words()
{
    if (coopstore::StoreHandshakeDecide(40u, 40u) != coopstore::kHandshakeAccept)
        Fail("two games speaking the same protocol were refused");
    unsigned int others[4]; others[0] = 0u; others[1] = 39u; others[2] = 41u; others[3] = 43u;
    for (int i = 0; i < 4; ++i)
    {
        if (coopstore::StoreHandshakeDecide(40u, others[i]) != coopstore::kHandshakeRefuse)
            Fail("a mismatched pair was ACCEPTED - store 40 against game " + I((int)others[i]));
        if (coopstore::StoreHandshakeDecide(others[i], 40u) != coopstore::kHandshakeRefuse)
            Fail("a mismatched pair was ACCEPTED the other way round - store " + I((int)others[i]) + " against game 40");
    }
    const std::string t = coopstore::StoreHandshakeRefusalText(40u, 43u);
    if (t != "store protocol 40, game speaks 43")
        Fail("the refusal text is not the words both sides print - got: " + t);
    /* the two numbers are NOT interchangeable: a text that reads the same both ways cannot tell an
       operator which side to update. */
    if (coopstore::StoreHandshakeRefusalText(43u, 40u) == t)
        Fail("the refusal text does not distinguish the store's protocol from the game's");
}

/* ---------------------------------------------------------------------------------------------
   U2 (and the P8i-c fold).  Four pure decisions the panel now takes, swept here so the shipped rule and
   the tested rule cannot drift apart by hand (6a lesson 11).
   --------------------------------------------------------------------------------------------- */
void t_panel_layout_is_centred_and_no_label_keeps_the_old_55_per_cent_share()
{
    int w, h;

    /* ---------------------------------------------------------------------------------------------
       THE ASSERTION THAT FAILS ON THE OLD LAYOUT, NAMED.  U1 gave a paired label a flat
       (clientW * 55) / 100 and its field the remainder.  Substituting that expression for
       PanelPairRowOf().labelW makes the sweep below fire, because 55 is less than the floor
       kPanelLabelPctMin the new share is held to.  The premise is PINNED BY MEASUREMENT rather than
       asserted in a comment: the old share really is under the floor at a representative client. */
    {
        const int cw = 300;
        const int oldLabW = (cw * 55) / 100;
        if (oldLabW * 100 >= cw * coopui::kPanelLabelPctMin)
            Fail("the premise is gone: the old 55% label share is no longer below the "
                 + I(coopui::kPanelLabelPctMin) + "% floor, so this test no longer pins the defect the"
                 " hands-on report of 2026-09-05 named");
        if (coopui::kPanelLabelPctMin <= 55)
            Fail("kPanelLabelPctMin is " + I(coopui::kPanelLabelPctMin) + ", which is no improvement on"
                 " the old fixed 55% share");
    }

    /* ---------------------------------------------------------------------------------------------
       THE ROW SHARES, over every rectangle PanelWindowAllowed permits in the prep's own swept window
       (w 250..400 x h 230..400 = 25,500 rectangles), and at the gap the shipped code passes: one
       derived from the row height, so the sweep walks the gap's whole range too. */
    for (w = 250; w <= 400; w += 1)
        for (h = 230; h <= 400; h += 1)
        {
            int clientW, clientH, rowH, gap;
            if (!coopui::PanelWindowAllowed(w, h)) continue;
            clientW = w - coopui::kWindowCXChromeW;
            clientH = h - coopui::kWindowCXChromeH;
            rowH = clientH / coopui::kPanelRows;
            gap = rowH / 6 + 1;
            {
                const coopui::PanelPair p = coopui::PanelPairRowOf(clientW, gap);
                /* The label's share never falls below the floor - this is the assertion the old
                   layout's flat 55% fails. */
                if (p.labelW * 100 < clientW * coopui::kPanelLabelPctMin)
                    Fail("at client " + I(clientW) + "x" + I(clientH) + " the label got " + I(p.labelW)
                         + " of " + I(clientW) + ", under the " + I(coopui::kPanelLabelPctMin)
                         + "% floor");
                /* Nothing is lost and nothing overflows: label + gap + field is the client exactly. */
                if (p.fieldX + p.fieldW != clientW)
                    Fail("at client width " + I(clientW) + " the field ends at " + I(p.fieldX + p.fieldW)
                         + ", not at the client edge");
                if (p.fieldX <= p.labelW)
                    Fail("at client width " + I(clientW) + " the field starts at " + I(p.fieldX)
                         + ", which is not past a label of " + I(p.labelW));
                /* Both parts stay usable at the smallest allowed rectangle. */
                if (p.fieldW < coopui::kPanelMinFieldW && p.fieldW != clientW - gap - p.labelW)
                    Fail("at client width " + I(clientW) + " the field is " + I(p.fieldW)
                         + ", under the minimum " + I(coopui::kPanelMinFieldW));
                if (p.labelW < coopui::kPanelMinLabelW)
                    Fail("at client width " + I(clientW) + " the label is " + I(p.labelW)
                         + ", under the minimum " + I(coopui::kPanelMinLabelW));
            }
        }

    /* A degenerate gap must not be able to produce a negative rectangle. */
    {
        const coopui::PanelPair p0 = coopui::PanelPairRowOf(coopui::kPanelMinClientW, 0);
        if (p0.labelW < 1 || p0.fieldW < 1)
            Fail("a zero gap produced an empty part of the row");
    }

    /* ui3 - THE INNER MARGIN (coopui::PanelInnerPadOf): every window PanelWindowAllowed permits, up to a 3840 x 2160 screen's
       panel, still holds its thirteen rows INSIDE the margin, so the pre-create prediction stays true; and an ordinary
       client gets a real margin, never 0. */
    for (w = 260; w <= 3840; w += 7)
        for (h = 240; h <= 2160; h += 11)
        {
            if (!coopui::PanelWindowAllowed(w, h)) continue;
            const int icw = w - coopui::kWindowCXChromeW;
            const int ich = h - coopui::kWindowCXChromeH;
            const coopui::PanelInnerPad ipd = coopui::PanelInnerPadOf(icw, ich);
            if (ipd.x < 0 || ipd.y < 0 || !coopui::PanelClientFits(icw - 2 * ipd.x, ich - 2 * ipd.y))
                Fail("ui3: the inner margin at client " + I(icw) + "x" + I(ich) + " leaves an area the thirteen rows do not fit");
        }
    {
        const coopui::PanelInnerPad ipd = coopui::PanelInnerPadOf(474, 650);
        if (ipd.x < 10 || ipd.y < 5) Fail("ui3: a 474 x 650 client got no real inner margin");
    }

    /* U2-c (review-u2 L-3) - THE SWEEP ABOVE STOPS AT A 400-PIXEL WINDOW, AND THE SHIPPED CODE BUILDS
       CLIENTS FAR WIDER THAN THAT.  PanelRectIn can take up to kPanelMaxParentPct of the parent, so on a
       3840-wide screen the client reaches about 90% of 3840.  A share rule that held only on small
       rectangles would be a rule tested where it does not matter, so the whole built range is swept -
       every client width up to that bound, at the widest gap the cap allows and at the narrowest. */
    {
        const int widest = (3840 * coopui::kPanelMaxParentPct) / 100;
        int cw, gi;
        for (cw = coopui::kPanelMinClientW; cw <= widest; cw += 7)
            for (gi = 0; gi <= 2; ++gi)
            {
                const int gap = (gi == 0) ? 1 : ((gi == 1) ? coopui::kPanelMaxPairGap
                                                           : coopui::kPanelMaxPairGap * 4);
                const coopui::PanelPair p = coopui::PanelPairRowOf(cw, gap);
                if (p.labelW * 100 < cw * coopui::kPanelLabelPctMin)
                    Fail("at client width " + I(cw) + " gap " + I(gap) + " the label got " + I(p.labelW)
                         + ", under the " + I(coopui::kPanelLabelPctMin) + "% floor");
                if (p.fieldX + p.fieldW != cw)
                    Fail("at client width " + I(cw) + " gap " + I(gap) + " the row does not close on the"
                         " client edge");
                if (p.labelW < coopui::kPanelMinLabelW || p.fieldW < 1)
                    Fail("at client width " + I(cw) + " gap " + I(gap) + " a part of the row is too"
                         " small: label " + I(p.labelW) + ", field " + I(p.fieldW));
            }
    }

    /* And the largest parent this project has ever been asked about, as a named sample rather than a
       point inside a sweep, so a regression at 4K is reported as 4K and not as an index. */
    {
        const coopui::PanelRect r4k = coopui::PanelRectIn(3840, 2160);
        const coopui::PanelPair p4k = coopui::PanelPairRowOf(r4k.w - coopui::kWindowCXChromeW,
                                                            coopui::kPanelMaxPairGap);
        if (!coopui::PanelWindowAllowed(r4k.w, r4k.h))
            Fail("a 3840x2160 parent produced a refused panel: " + I(r4k.w) + "x" + I(r4k.h));
        if (p4k.labelW * 100 < (r4k.w - coopui::kWindowCXChromeW) * coopui::kPanelLabelPctMin)
            Fail("at 3840x2160 the label share fell under the floor");
        if (coopui::PanelRectWasGrown(3840, 2160))
            Fail("a 3840x2160 parent had to be grown, so the comfort target is not the reason the"
                 " 40x70 box is kept at large sizes");
    }

    /* ---------------------------------------------------------------------------------------------
       THE RECTANGLE: centred on both axes, inside the parent, and never smaller than it used to be.
       Swept over parents from a small window to 4K, both axes. */
    for (w = 320; w <= 3840; w += 17)
        for (h = 240; h <= 2160; h += 13)
        {
            const coopui::PanelRect r = coopui::PanelRectIn(w, h);
            const int rightGap = w - r.left - r.w;
            const int bottomGap = h - r.top - r.h;
            if (r.left < 0 || r.top < 0 || rightGap < 0 || bottomGap < 0)
                Fail("the panel rectangle left the parent at parent " + I(w) + "x" + I(h));
            if (rightGap - r.left > 1 || r.left - rightGap > 1)
                Fail("the panel is not centred across at parent " + I(w) + "x" + I(h) + ": left "
                     + I(r.left) + ", right " + I(rightGap));
            if (bottomGap - r.top > 1 || r.top - bottomGap > 1)
                Fail("the panel is not centred down at parent " + I(w) + "x" + I(h) + ": top "
                     + I(r.top) + ", bottom " + I(bottomGap));
            /* Growing is the only change of size, so the box is never smaller than the old 40 x 70. */
            if (r.w < (w * 40) / 100 || r.h < (h * 70) / 100)
                Fail("the panel shrank below the old 40x70 box at parent " + I(w) + "x" + I(h));
            if (r.w > (w * coopui::kPanelMaxParentPct) / 100 && r.w != w)
                Fail("the panel took more than " + I(coopui::kPanelMaxParentPct) + "% of the parent"
                     " width at parent " + I(w) + "x" + I(h));
            /* AND THE REFUSAL IS NOT WEAKENED: every parent whose OLD box was allowed still gets an
               allowed box.  A layout change that made the panel refuse where it used to open would be
               a regression dressed as a repair. */
            if (coopui::PanelWindowAllowed((w * 40) / 100, (h * 70) / 100)
             && !coopui::PanelWindowAllowed(r.w, r.h))
                Fail("the grown box is refused where the old 40x70 box was allowed, at parent "
                     + I(w) + "x" + I(h));
            /* The grown flag agrees with the rectangle it reports on - ui.cpp books a counter on it. */
            if (coopui::PanelRectWasGrown(w, h)
                != (r.w != (w * 40) / 100 || r.h != (h * 70) / 100))
                Fail("PanelRectWasGrown disagrees with PanelRectIn at parent " + I(w) + "x" + I(h));
        }

    /* The comfort target is reached whenever the parent is big enough to give it - the whole point of
       growing.  A 1024x768 parent's old box was 409x537, whose client held rows of 41 pixels; a
       640x480 parent's old box was 256x336 and was REFUSED outright by the 260 minimum.  That second
       case is the one the growth exists for. */
    {
        const coopui::PanelRect small = coopui::PanelRectIn(640, 480);
        if (!coopui::PanelWindowAllowed(small.w, small.h))
            Fail("a 640x480 parent still cannot hold the panel after growing: got "
                 + I(small.w) + "x" + I(small.h));
        if (coopui::PanelWindowAllowed((640 * 40) / 100, (480 * 70) / 100))
            Fail("the premise is gone: the OLD box at a 640x480 parent is no longer refused, so growth"
                 " is no longer what makes that parent work");
    }
}

void t_panel_one_guard_owns_both_size_minimums()
{
    /* THE ASSERTION THAT ENCODES F751.  Before the fold, the pure layer's only predicate was
       PanelWindowFits, and PanelWindowFits(259, 240) is TRUE - its client is 253 x 195, which is
       253 >= 220 and 195 >= 13*14.  The 260 x 240 design minimum lived in ui.cpp as two bare literals in
       an early return ABOVE the capped guard, so the pure layer ACCEPTED a rectangle the shipped code
       refused, and the refusal it took instead was uncapped, unmessaged and permanent.  Run against the
       pre-patch header (PanelWindowAllowed does not exist; substitute PanelWindowFits) this assertion
       fails, which is exactly the disagreement the fold removes. */
    if (!coopui::PanelWindowFits(259, 240))
        Fail("the premise is gone: PanelWindowFits(259,240) is no longer true, so this test no longer"
             " pins the disagreement F751 named");
    if (coopui::PanelWindowAllowed(259, 240))
        Fail("PanelWindowAllowed accepted 259x240 - one pixel under the 260 design minimum");
    if (coopui::PanelWindowAllowed(260, 239))
        Fail("PanelWindowAllowed accepted 260x239 - one pixel under the 240 design minimum");
    if (!coopui::PanelWindowAllowed(260, 240))
        Fail("PanelWindowAllowed refused the design minimum 260x240");

    /* And it still refuses everything the fit test refuses, at and above the design minimum: the two
       rules are ANDed, never ordered. */
    if (coopui::kPanelMinWindowW != 260 || coopui::kPanelMinWindowH != 240)
        Fail("the design minimum in panelfit.h is " + I(coopui::kPanelMinWindowW) + "x"
             + I(coopui::kPanelMinWindowH) + ", not 260x240");
    int w, h;
    for (w = 250; w <= 400; w += 1)
        for (h = 230; h <= 400; h += 1)
        {
            const bool allowed = coopui::PanelWindowAllowed(w, h);
            const bool wanted = (w >= 260 && h >= 240 && coopui::PanelWindowFits(w, h));
            if (allowed != wanted)
                Fail("PanelWindowAllowed(" + I(w) + "," + I(h) + ") = " + I(allowed ? 1 : 0)
                     + ", but minimum-AND-fit says " + I(wanted ? 1 : 0));
        }
}

void t_panel_numbers_carry_no_locale_separators()
{
    /* F030: RE_Kenshi imbues a process-wide locale that puts digit-group separators into numbers, and
       the status area prints a port and an attempt count.  PanelNum is hand-rolled for that reason, so
       the test is that it is DIGITS AND NOTHING ELSE. */
    if (coopui::PanelNum(0) != "0")         Fail("PanelNum(0) = '" + coopui::PanelNum(0) + "'");
    if (coopui::PanelNum(7) != "7")         Fail("PanelNum(7) = '" + coopui::PanelNum(7) + "'");
    if (coopui::PanelNum(27016) != "27016") Fail("PanelNum(27016) = '" + coopui::PanelNum(27016) + "'");
    if (coopui::PanelNum(-1) != "-1")       Fail("PanelNum(-1) = '" + coopui::PanelNum(-1) + "'");
    if (coopui::PanelNum(1234567) != "1234567")
        Fail("PanelNum(1234567) = '" + coopui::PanelNum(1234567) + "' - a separator got in");
    /* The negation of the most negative value is the one arithmetic trap in the conversion. */
    if (coopui::PanelNum(-9223372036854775807LL - 1) != "-9223372036854775808")
        Fail("PanelNum(LLONG_MIN) = '" + coopui::PanelNum(-9223372036854775807LL - 1) + "'");
    if (coopui::PanelAddrText(std::string(), 7777) != "none")
        Fail("an empty address did not read 'none'");
    if (coopui::PanelAddrText("1.2.3.4", 0) != "none")
        Fail("port 0 did not read 'none'");
    if (coopui::PanelAddrText("1.2.3.4", 7777) != "1.2.3.4:7777")
        Fail("PanelAddrText = '" + coopui::PanelAddrText("1.2.3.4", 7777) + "'");
}

void t_panel_status_picks_one_of_five_states_and_a_host_alone_is_not_connected()
{
    coopui::PanelLinkFacts f;

    /* 1 - role=single. */
    f.role = coopui::kPanelRoleSingle;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelNotConnected) Fail("single was not notConnected");

    /* 2 - THE ASSERTION THAT ENCODES THE CORRECTION OF RECORD against design-ui-panel 3.3.  Its four
       states test "!linked AND role is client" and then fall through to "connected to the other game",
       so a HOST that is listening with nobody joined reads as connected.  net::SessionLinked() is only
       true after an ENET CONNECT event, so that is the ordinary case for a host waiting for a friend. */
    f.role = coopui::kPanelRoleHost;
    f.worldLinked = 0;
    f.notebookUp = 1;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelHostWaiting)
        Fail("a host with nobody connected read as " + std::string(coopui::PanelLinkStateName(coopui::PanelLinkStateOf(f))));
    {
        const std::string s = coopui::PanelStatusText(f, "0.0.0.0", 7777, "consensus");
        if (s.find("Hosting on port 7777") == std::string::npos)
            Fail("the host-waiting sentence does not name the port: '" + s + "'");
        if (s.find("Connected") != std::string::npos)
            Fail("a host with nobody joined was told it is connected: '" + s + "'");
    }

    /* 3 - a client that has not been answered, and its attempt count. */
    f.role = coopui::kPanelRoleClient;
    f.worldLinked = 0;
    f.dials = 4;
    f.dialCap = 60;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelWaitingHost) Fail("an unlinked client was not waitingHost");
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, 0);
        if (s != "Connecting to 192.168.1.50:7777...")   /* T-201 N1: the approved line, no attempt count */
            Fail("T-201 N1: the connecting sentence is not the approved one: '" + s + "'");
        if (s.find("192.168.1.50:7777") == std::string::npos)
            Fail("the waiting-host sentence lost the address: '" + s + "'");
    }
    if (coopui::PanelJoinGaveUp(f) != 0) Fail("mp1: a join on try 4 of 60 read as given up (Try again would show)");
    /* 3b - at the cap it stops saying "looking" and says the friend's game is not answering (mp1: + Try again). */
    f.dials = 60;
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, 0);
        if (s != coopui::kPressJoinNoAnswerText)   /* T-201 N1: the CAN'T JOIN box's words; TRY AGAIN is gone */
            Fail("at the dial cap the panel still claimed to be looking: '" + s + "'");
    }
    if (coopui::PanelJoinGaveUp(f) != 1) Fail("mp1: a join at 60 of 60 did not read as given up (no Try again)");
    f.dialCap = 0;
    if (coopui::PanelJoinGaveUp(f) != 0) Fail("mp1: a cap of 0 read as given up");
    f.dialCap = 60;
    f.dials = 0;

    /* 4 - linked to the other game, notebook silent. */
    f.worldLinked = 1;
    f.notebookUp = 0;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelWaitingNotebook) Fail("a silent notebook was not waitingNotebook");

    /* 5 - everything up, with and without a player number. */
    f.notebookUp = 1;
    f.mySlot = -1;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelConnected) Fail("both links up was not connected");
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, "fixed");
        if (s.find("Connected. Joining world...") == std::string::npos)
            Fail("an unnumbered game claimed a player number: '" + s + "'");
    }
    f.mySlot = 1;
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, "fixed");
        /* slot 1 is the SECOND player: the numbering the player reads is one-based. */
        if (s.find("You are player 2.") == std::string::npos)
            Fail("slot 1 was not shown as player 2: '" + s + "'");
        /* mp1: the time mode is jargon (design-mpmenu1 section 1) and is no longer on the panel. */
        if (s.find("Time is run") != std::string::npos)
            Fail("mp1: the time mode is still on the panel: '" + s + "'");
    }
    /* A null time mode must not produce "Time is run ." */
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, 0);
        if (s.find("Time is run") != std::string::npos)
            Fail("a null time mode still printed a time sentence: '" + s + "'");
    }

    /* The world-key mismatch rides every connected state rather than replacing it. */
    f.worldKeyMismatch = 1;
    {
        const std::string s = coopui::PanelStatusText(f, "192.168.1.50", 7777, "fixed");
        if (s.find("You are player 2.") == std::string::npos || s.find("different worlds") == std::string::npos)
            Fail("the world-key warning replaced the state instead of joining it: '" + s + "'");
    }
    f.worldKeyMismatch = 0;

    /* Every state name is distinct and none is the fallback - a run greps these. */
    int i, j;
    for (i = 0; i <= 4; ++i)
        for (j = i + 1; j <= 4; ++j)
            if (std::strcmp(coopui::PanelLinkStateName(i), coopui::PanelLinkStateName(j)) == 0)
                Fail("two states share the name '" + std::string(coopui::PanelLinkStateName(i)) + "'");
    for (i = 0; i <= 4; ++i)
        if (std::strcmp(coopui::PanelLinkStateName(i), "??") == 0)
            Fail("state " + I(i) + " has no name");
}

void t_panel_notebook_and_give_up_sentences_name_their_numbers()
{
    /* The notebook's three states of design 2.2, and each names the number the player needs. */
    if (coopui::PanelNotebookText(coopui::kNbNotOurs, 27016, 0) != "")
        Fail("a notebook we did not start produced a sentence");
    if (coopui::PanelNotebookText(coopui::kNbStarting, 27016, 0).find("27016") == std::string::npos)
        Fail("the starting sentence does not name the port");
    if (coopui::PanelNotebookText(coopui::kNbRunning, 27016, 0) != "")
        Fail("words1: the running line is dropped (nothing the player can act on)");
    {
        const std::string s = coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, 1);
        if (s.find("error 1") == std::string::npos || s.find("27016") == std::string::npos)
            Fail("the stopped-early sentence lost the exit code or the port: '" + s + "'");
    }
    {   /* the world server's two start refusals say why, under CAN'T HOST, and never blame a busy port */
        const std::string s33 = coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, swformat::kServerExitSlotsShrunk);
        const std::string s34 = coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, swformat::kServerExitWorldAside);
        if (s33 != "Couldn't host: this world's list of players is shorter than its last backup."
                   " Starting it could give some players' characters to the wrong player. Nothing was changed.")
            Fail("exit 33 (slots.txt shorter than its backup) lost its approved words: '" + s33 + "'");
        if (s34 != "Couldn't host: moving this world to this computer didn't finish."
                   " Starting it now would make a new, empty world in its place. Nothing was changed.")
            Fail("exit 34 (the world folder an unfinished import left missing) lost its approved words: '" + s34 + "'");
        if (s33.find("port") != std::string::npos || s34.find("port") != std::string::npos
            || s33.find("Port") != std::string::npos || s34.find("Port") != std::string::npos)
            Fail("a start refusal (exit 33 / 34) still blames a port");
        if (coopui::NoticeTitle(s33) != std::string("CAN'T HOST") || coopui::NoticeTitle(s34) != std::string("CAN'T HOST"))
            Fail("a start refusal (exit 33 / 34) is not titled CAN'T HOST");
        const std::string s4 = coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, swformat::kServerExitWorldLocked);
        if (s4 != "Couldn't host: this world is already running in another program on this computer (another Kenshi, or one left"
                  " over after a crash).")
            Fail("exit 4 (the world's lock is held) lost its approved words: '" + s4 + "'");
        if (coopui::NoticeTitle(s4) != std::string("CAN'T HOST")) Fail("exit 4 is not titled CAN'T HOST");
    }
    if (coopui::PanelNotebookText(coopui::kNbNoExe, 27016, 0).find("missing") == std::string::npos)
        Fail("the missing-exe sentence does not say the program is missing");
    /* mp1 (owner, 2026-09-26: no 'notebook' wording anywhere): no sentence the panel can build says it. */
    {
        int st;
        for (st = -1; st <= 8; ++st)
        {
            const std::string s = coopui::PanelNotebookText(st, 27016, 1) + coopui::PanelRefusalText(st, std::string());
            if (s.find("notebook") != std::string::npos || s.find("Notebook") != std::string::npos
             || s.find("coop-store") != std::string::npos)
                Fail("mp1: a panel sentence still names the notebook: '" + s + "'");
        }
        coopui::PanelLinkFacts lf;
        int role, linked, up, dials, slot, mism;
        for (role = 0; role <= 2; ++role) for (linked = 0; linked <= 1; ++linked) for (up = 0; up <= 1; ++up)
        for (dials = 0; dials <= 60; dials += 60) for (slot = -1; slot <= 1; slot += 2) for (mism = 0; mism <= 1; ++mism)
        {
            lf.role = role; lf.worldLinked = linked; lf.notebookUp = up; lf.dials = dials; lf.dialCap = 60;
            lf.mySlot = slot; lf.worldKeyMismatch = mism;
            const std::string s = coopui::PanelStatusText(lf, "192.168.1.50", 7777, "unknown (no notebook clock yet)");
            if (s.find("notebook") != std::string::npos || s.find("START HOSTING") != std::string::npos
             || s.find("slot") != std::string::npos)
                Fail("mp1: a status sentence still carries jargon: '" + s + "'");
        }
    }
    /* mp1: the refusal numbers are storelink.h's - the pure header cannot include it, so they are tied here. */
    if (coopui::PanelRefusalText(-1, "x") != "") Fail("mp1: not refused produced a refusal sentence");
    if (coopui::PanelRefusalText(coopstore::kRefuseFull, "").find("full") == std::string::npos
     || coopui::PanelRefusalText(coopstore::kRefuseLifetimeFull, "").find("player limit") == std::string::npos)
        Fail("mp1: a full world was not called full");
    if (coopui::PanelRefusalText(coopstore::kRefuseDuplicateId, "").find("another Kenshi window") == std::string::npos)
        Fail("mp1: the already-in refusal lost its words");
    if (coopui::PanelRefusalText(coopstore::kRefuseProtocol, "").find("different version") == std::string::npos)
        Fail("mp1: the version refusal lost its words");
    if (coopui::PanelRefusalText(coopstore::kRefuseMods, "MODS SENTENCE") != "MODS SENTENCE"
     || coopui::PanelRefusalText(coopstore::kRefuseMods, "").find("Mod mismatch") != 0)
        Fail("mp1: the mods refusal does not carry store.cpp's sentence, or has no fallback");
    /* words1 (owner wording audit 2026-09-27): the game is MULTIPLAYER - no refusal says co-op or "your friend". */
    {
        int rr;
        for (rr = -1; rr <= 100; ++rr)
        {
            const std::string low = coopcfg::CfgLower(coopui::PanelRefusalText(rr, std::string()));
            if (low.find("co-op") != std::string::npos || low.find("coop") != std::string::npos || low.find("friend") != std::string::npos)
                Fail("words1: a refusal says co-op or friend: '" + low + "'");
        }
    }

    /* words1 (owner wording audit 2026-09-27, section 2): the give-up message no longer shows the cap (review-p8i-b's LOW
       about a hardcoded "5" is moot - no number is on screen; the log line prints the cap), so it is one sentence. */
    if (coopui::PanelGiveUpMessage(5) != "The multiplayer menu couldn't open."
     || coopui::PanelGiveUpMessage(3) != coopui::PanelGiveUpMessage(5))
        Fail("words1: the give-up message: '" + coopui::PanelGiveUpMessage(5) + "'");
    /* ui1 (ui-polish-audit 3.3): the title-screen error box's title; 3.4: no "quit Kenshi" on the Hosting screen */
    if (coopui::PanelRefusalText(coopstore::kRefuseProtocol, "").find("host") != std::string::npos) Fail("words1b: the version sentence names no host");
    if (coopui::NoticeTitle("You could not load that save. It is older than the world.") != "CAN'T LOAD"
        || coopui::NoticeTitle("You could not join this world: the host is full.") != "CAN'T JOIN"
        || coopui::NoticeTitle("Mod mismatch: extra coop-test-salt. Use the same mods as the host, in the same order, then restart Kenshi and join again.") != "MOD MISMATCH"
        || coopui::NoticeTitle("Mod mismatch: extra Beta. Use the same mod list as the host, then restart Kenshi.") != "MOD MISMATCH"   /* words1b: the start alone */
        || coopui::NoticeTitle(coopui::PanelRefusalText(coopstore::kRefuseMods, "")) != "MOD MISMATCH"
        || coopui::NoticeTitle(coopui::PanelRefusalText(1, "")) != "CAN'T JOIN" || coopui::NoticeTitle(coopui::PanelRefusalText(100, "")) != "CAN'T JOIN"
        || coopui::NoticeTitle("The Multiplayer screen could not be opened.") != "MULTIPLAYER" || coopui::NoticeTitle("") != "MULTIPLAYER")
        Fail("ui1: the title-screen error box's title");
}

/* D3 (read-pathend, T266 stop-start): the lead covers the landing delay (each new destination at least 0.5 s of the copy's
   speed beyond the near-end point), the near end is in time on the COPY's speed and stays below 0.9 x the lead, a too-close
   destination extends forward past the engine's 2 u drop (and is refused when it cannot), and a 1 counts as landed only
   after the request read pending. */
bool NearF(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }
void t_d3_path_lead_near_end_extend_and_landed()
{
    using namespace cooppath;
    if (!NearF(LeadDist(0.0f, 0.0f), 8.0f) || !NearF(LeadDist(-1.0f, -1.0f), 8.0f)) Fail("a still pair must lead by the 8 u minimum");
    if (!NearF(LeadDist(10.0f, 0.0f), 18.0f) || !NearF(LeadDist(5.0f, 20.0f), 34.0f)) Fail("the lead must be max(owner, copy) x 1.6 + 2");
    if (!NearF(LeadDist(200.0f, 0.0f), 90.0f)) Fail("the lead must clamp at 90");
    if (!NearF(NearEndDist(0.0f, 8.0f), 3.0f) || !NearF(NearEndDist(-1.0f, 8.0f), 3.0f)) Fail("the near end must floor at 3 u");
    if (!NearF(NearEndDist(20.0f, 34.0f), 22.0f)) Fail("the near end must be the copy's speed x 1.1 s");
    if (!NearF(NearEndDist(85.0f, 90.0f), 81.0f)) Fail("the near end must stay below 0.9 x the lead");
    {
        float v;
        for (v = 0.0f; v <= 100.0f; v += 0.5f)
        {
            const float la = LeadDist(v, v), ne = NearEndDist(v, la);
            if (!(ne < la)) { Fail("near end >= lead at speed " + I((int)v)); return; }
            if (la < 90.0f && !(la - ne >= v * 0.5f)) { Fail("a new destination is < 0.5 s of copy speed beyond the near end at " + I((int)v)); return; }
        }
    }
    if (!NearF(ExtendDist(0.0f, 8.0f), 2.5f) || !NearF(ExtendDist(20.0f, 34.0f), 10.0f)) Fail("the extension must be max(2.5, copy speed x 0.5)");
    if (!NearF(ExtendDist(1000.0f, 90.0f), 60.0f) || !NearF(ExtendDist(100.0f, 20.0f), 20.0f)) Fail("the extension must clamp the speed at 120 and stop at the lead");
    if (!NearF(LeadDist(0.0f, 5000.0f), 90.0f) || !NearF(NearEndDist(5000.0f, 90.0f), 81.0f) || !NearF(CopySpeed(5000.0f), 120.0f))
        Fail("a jump-sized copy speed must clamp at 120 u/s");
    float x = 0.0f, z = 0.0f;
    if (ExtendTooClose(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -10.0f, 0.0f, 0.0f, 8.0f, 2.0f, 1.0f, &x, &z) != 1 || !NearF(x, 3.5f) || !NearF(z, 0.0f))
        Fail("a destination 1 u past the last must extend forward by 2.5");
    if (ExtendTooClose(-1.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -10.0f, 0.0f, 0.0f, 8.0f, 2.0f, 1.0f, &x, &z) != 1 || !NearF(x, 2.5f))
        Fail("a destination still too close after the push must extend from the last one");
    if (ExtendTooClose(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -10.0f, 0.0f, 0.0f, 8.0f, 2.0f, 1.0f, &x, &z) != 0) Fail("no heading must refuse");
    if (ExtendTooClose(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.8f, 0.0f, 0.0f, 8.0f, 2.0f, 1.0f, &x, &z) != 0) Fail("a push landing < 1 u from the copy must refuse");
    bool seen = false;
    if (LandedStep(1, &seen) != 0 || seen) Fail("a 1 never preceded by a pending read must not count as landed");
    if (LandedStep(4, &seen) != 0 || !seen) Fail("a 4 must mark the request pending");
    if (LandedStep(5, &seen) != 0 || LandedStep(1, &seen) != 1) Fail("a 1 after a pending read must count as landed");
    if (LandedStep(-1, &seen) != -1) Fail("a faulted read must say so");
    if (!IssueGateBlocks(0.1, true, 0.5) || !IssueGateBlocks(0.19, true, 0.5)) Fail("a landed path must still wait the 0.2 s gap");
    if (IssueGateBlocks(0.2, true, 0.5) || IssueGateBlocks(0.3, true, 0.5)) Fail("a landed path must lift the floor down to 0.2 s");
    if (!IssueGateBlocks(0.3, false, 0.5) || IssueGateBlocks(0.5, false, 0.5)) Fail("an unlanded path must keep the 0.5 s floor");
    if (!OldLeadStillWalked(true, true, false, false, false)) Fail("a stop issue over a live lead path must keep ahead-stop");
    if (!OldLeadStillWalked(true, false, true, false, false)) Fail("a second exact-aim issue before the first landed must inherit it");
    if (OldLeadStillWalked(true, false, true, true, false)) Fail("once the exact-aim path landed the lead is no longer walked");
    if (OldLeadStillWalked(true, true, true, false, true) || OldLeadStillWalked(false, true, true, false, false))
        Fail("a lead issue, or no live destination, must clear it");
}

/* The follow drive (src/common/followplay.h): the copy replays its owner's stamped reports at (owner time - D), guesses past
   the newest report for at most 0.05 s and then holds, never aims past the newest reported point, and takes its keep-walking
   distance from the OWNER's reported speed only. */
static followplay::Sample FpSample(float x, float z, float vx, float vz, double t, bool still)
{
    followplay::Sample s; s.x = x; s.y = 0.0f; s.z = z; s.vx = vx; s.vz = vz; s.t = t; s.still = still; return s;
}
static followplay::Track FpRunner(int n, double t0)   /* an owner running +x at 70 u/s, a report every 0.1 s from t0 */
{
    followplay::Track k; followplay::TrackClear(&k);
    for (int i = 0; i < n; ++i) { const double t = t0 + 0.1 * i; followplay::Push(&k, FpSample((float)(70.0 * (t - t0)), 0.0f, 70.0f, 0.0f, t, false)); }
    return k;
}
void t_follow_playback_target_at_delay()
{
    using namespace followplay;
    Track k; TrackClear(&k);
    Sample out;
    if (TargetAt(k, 1.0, kExtrapMaxSec, &out) != kModeEmpty) Fail("follow: an empty recording must give no target");
    k = FpRunner(11, 0.0);   /* x = 0 .. 70 at t = 0 .. 1.0 */
    if (k.n != 11) Fail("follow: eleven reports must be kept");
    if (TargetAt(k, 0.55, kExtrapMaxSec, &out) != kModeInterp || !NearF(out.x, 38.5f)) Fail("follow: the target between two reports lies on the line between them");
    if (TargetAt(k, -1.0, kExtrapMaxSec, &out) != kModeInterp || !NearF(out.x, 0.0f)) Fail("follow: before the recording the target is its oldest report");
    /* order by owner time: an older report is dropped, the same moment replaces the newest */
    if (Push(&k, FpSample(30.0f, 0.0f, 70.0f, 0.0f, 0.45, false)) != 0 || k.n != 11) Fail("follow: a report older than the newest must be dropped");
    if (Push(&k, FpSample(70.5f, 0.0f, 70.0f, 0.0f, 1.0, false)) != 2 || k.n != 11 || !NearF(TrackNewest(k).x, 70.5f)) Fail("follow: a report at the newest's time replaces it");
    volatile float zero = 0.0f;
    const float nan = zero / zero;
    if (Push(&k, FpSample(nan, 0.0f, 70.0f, 0.0f, 1.1, false)) != 0) Fail("follow: a report that is not a number must be dropped");
    /* the ring keeps the newest 16 */
    Track r = FpRunner(20, 0.0);
    if (r.n != kTrackLen || !(TrackGet(r, 0).t > 0.39 && TrackGet(r, 0).t < 0.41) || !(TrackNewest(r).t > 1.89)) Fail("follow: a full recording keeps the newest 16 reports");
    /* the delay: two report intervals, at least 0.15 s, at most 0.30 s, 0.2 s while none is measured */
    if (!(PlaybackDelay(0.1) > 0.199 && PlaybackDelay(0.1) < 0.201)) Fail("follow: 0.1 s reports give a 0.2 s delay");
    if (!(PlaybackDelay(0.05) > 0.149 && PlaybackDelay(0.05) < 0.151)) Fail("follow: the delay is at least 0.15 s");
    if (!(PlaybackDelay(0.2) > 0.299 && PlaybackDelay(0.2) < 0.301)) Fail("follow: the delay is at most 0.30 s");
    if (!(PlaybackDelay(0.0) > 0.199 && PlaybackDelay(0.0) < 0.201) || !(PlaybackDelay(-1.0) > 0.199 && PlaybackDelay(-1.0) < 0.201)) Fail("follow: no measured interval gives 0.2 s");
    /* the clock: the offset is the fastest delivery, so a late report reads late by exactly its jitter */
    ClockOff c; ClockReset(&c);
    double off = ClockOffsetUpdate(&c, 105.03, 100.0);
    off = ClockOffsetUpdate(&c, 105.10, 100.1);
    off = ClockOffsetUpdate(&c, 105.25, 100.2);
    if (!(off > 4.999 && off < 5.001)) Fail("follow: the clock offset is the smallest arrival - owner time");
    off = ClockOffsetUpdate(&c, 107.70, 102.6);   /* a new half-window: the old smallest still counts */
    if (!(off > 4.999 && off < 5.001)) Fail("follow: the smallest gap counts until two half-windows have passed");
    off = ClockOffsetUpdate(&c, 110.30, 105.2);
    off = ClockOffsetUpdate(&c, 112.90, 107.8);
    if (!(off > 5.099 && off < 5.101)) Fail("follow: a smallest gap older than the window is let go");
    /* the stamp's 32-bit wrap is unwrapped against the last one */
    ClockOff w; ClockReset(&w);
    const double a = StampUnwrapMs(&w, 4294967200u);
    const double b = StampUnwrapMs(&w, 50u);
    if (!(b - a > 145.0 && b - a < 147.0)) Fail("follow: a stamp past the 32-bit wrap must read as 146 ms later");
    if (!(StampUnwrapMs(&w, 4294967250u) < b)) Fail("follow: a late stamp from before the wrap must read as earlier");
    /* the replay: newest report at owner time 1.0, arrived 0.04 s late, delay 0.2 - the target is 0.16 s behind it */
    Track rr = FpRunner(11, 0.0);
    if (TargetAt(rr, 1.0 + 0.04 - 0.2, kExtrapMaxSec, &out) != kModeInterp || !NearF(out.x, 58.8f)) Fail("follow: the replayed target trails the newest report by (D - age) x speed");
}
void t_follow_bounded_guess_and_limit()
{
    using namespace followplay;
    Track k = FpRunner(11, 0.0);   /* newest: x = 70 at t = 1.0, 70 u/s */
    Sample out;
    if (TargetAt(k, 1.03, kExtrapMaxSec, &out) != kModeExtrap || !NearF(out.x, 72.1f)) Fail("follow: just past the newest report the target moves on along its velocity");
    if (TargetAt(k, 1.2, kExtrapMaxSec, &out) != kModeHeld || !NearF(out.x, 73.5f)) Fail("follow: past 0.05 s the target holds 0.05 s of speed beyond the newest report");
    Push(&k, FpSample(70.0f, 0.0f, 0.0f, 0.0f, 1.1, true));
    if (TargetAt(k, 1.4, kExtrapMaxSec, &out) != kModeHeld || !NearF(out.x, 70.0f)) Fail("follow: a standing newest report is not guessed past");
    float ax = 0.0f, az = 0.0f;
    ClampToLimit(73.5f, 2.0f, 70.0f, 0.0f, 1.0f, 0.0f, &ax, &az);
    if (!NearF(ax, 70.0f) || !NearF(az, 2.0f)) Fail("follow: an aim past N is moved back onto N's crossing line, keeping its side offset");
    ClampToLimit(60.0f, 1.0f, 70.0f, 0.0f, 1.0f, 0.0f, &ax, &az);
    if (!NearF(ax, 60.0f) || !NearF(az, 1.0f)) Fail("follow: an aim short of N is left alone");
    if (!NearF(AlongError(60.0f, 3.0f, 50.0f, 0.0f, 1.0f, 0.0f), 10.0f) || !NearF(AlongError(45.0f, 0.0f, 50.0f, 0.0f, 1.0f, 0.0f), -5.0f))
        Fail("follow: e is the target minus the copy along the owner's route");
}
void t_follow_keep_walk_from_owner_speed()
{
    using namespace followplay;
    if (!NearF(KeepWalkDist(0.0f), 8.0f) || !NearF(KeepWalkDist(70.0f), 54.5f) || !NearF(KeepWalkDist(100.0f), 60.0f) || !NearF(KeepWalkDist(-3.0f), 8.0f))
        Fail("follow: the keep-walking distance is clamp(owner speed x 0.75 + 2, 8, 60)");
    volatile float zero = 0.0f;
    if (!NearF(KeepWalkDist(zero / zero), 8.0f)) Fail("follow: a speed that is not a number gives the 8 u floor");
    /* h and the owner's speed come from the newest MOVING report, not the copy */
    Track k = FpRunner(5, 0.0);
    Push(&k, FpSample(28.5f, 0.0f, 0.0f, 0.0f, 0.5, true));
    float hx = 0.0f, hz = 0.0f, v = 0.0f;
    if (!RouteHeading(k, &hx, &hz, &v) || !NearF(hx, 1.0f) || !NearF(hz, 0.0f) || !(v > 69.9f && v < 70.1f)) Fail("follow: h and the speed are the newest moving report's");
    if (OwnerMovingNow(k, 0.0, 0.35)) Fail("follow: a standing newest report means the owner stands");
    double t = 0.0;
    if (!TrackTimeNear(FpRunner(11, 0.0), 35.0f, 1.0f, &t) || !(t > 0.499 && t < 0.501)) Fail("follow: the copy at x = 35 is where the owner was at 0.5 s");
    /* the destination */
    DestIn in;
    in.stopHeld = false; in.sx = in.sz = 0.0f; in.hidden = false; in.ownerMoving = true;
    in.haveN = true; in.nx = 14.0f; in.nz = 0.0f; in.haveH = true; in.hx = 1.0f; in.hz = 0.0f; in.v = 70.0f;
    in.haveG = false; in.gx = in.gz = 0.0f; in.cx = 0.0f; in.cz = 0.0f; in.playerCopy = true;
    float dx = 0.0f, dz = 0.0f;
    if (FollowDest(in, &dx, &dz) != kDestKeep || !NearF(dx, 68.5f)) Fail("follow: a visible copy of a moving owner walks to N + KeepWalkDist(owner speed)");
    in.haveG = true; in.gx = 40.0f; in.gz = 3.0f;
    if (FollowDest(in, &dx, &dz) != kDestGoal || !NearF(dx, 40.0f) || !NearF(dz, 3.0f)) Fail("follow: a goal nearer than the keep-walking point is the destination (no cone)");
    in.gx = 100.0f; in.gz = 0.0f;
    if (FollowDest(in, &dx, &dz) != kDestKeep) Fail("follow: a goal beyond the keep-walking point leaves the keep-walking point");
    in.gx = -5.0f;
    if (FollowDest(in, &dx, &dz) != kDestKeep) Fail("follow: a goal behind the copy is not this trip's");
    in.hidden = true; in.gx = 100.0f;
    if (FollowDest(in, &dx, &dz) != kDestNewest || !NearF(dx, 14.0f)) Fail("follow: a hidden copy walks only to N");
    in.gx = 10.0f;
    if (FollowDest(in, &dx, &dz) != kDestGoal || !NearF(dx, 10.0f)) Fail("follow: a hidden copy stops at a goal nearer than N");
    in.hidden = false; in.ownerMoving = false;
    if (FollowDest(in, &dx, &dz) != kDestNewest || !NearF(dx, 14.0f)) Fail("follow: a standing owner's copy walks to N");
    in.stopHeld = true; in.sx = 20.0f; in.sz = 1.0f;
    if (FollowDest(in, &dx, &dz) != kDestStop || !NearF(dx, 20.0f) || !NearF(dz, 1.0f)) Fail("follow: a stated stop is the destination, exactly");
    in.stopHeld = false; in.haveN = false;
    if (FollowDest(in, &dx, &dz) != kDestNone) Fail("follow: no recording gives no destination");
}
void t_follow_never_past_and_pace()
{
    using namespace followplay;
    /* the limit N = (100, 0), the route along +x */
    if (NeverPastHalt(99.0f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: a copy 1 u short of N walks on");
    if (!NeverPastHalt(99.6f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: a copy less than 0.5 u short of N halts");
    if (!NeverPastHalt(101.0f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: a copy past N halts");
    if (!NeverPastHalt(100.0f, 5.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: level with N off to the side halts");
    if (NeverPastHalt(60.0f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: a copy far behind N walks on");
    if (NeverPastHalt(140.0f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f)) Fail("follow: the check applies only within 30 u of N");
    if (!NeverPastRestart(100.0f, 0.0f, 106.5f, 0.0f, 1.0f, 0.0f) || NeverPastRestart(100.0f, 0.0f, 105.0f, 0.0f, 1.0f, 0.0f))
        Fail("follow: a halted copy starts again once N is more than 6 u ahead");
    /* the slow-down for a copy ahead of its target */
    if (!NearF(PaceFactor(0.0f), 1.0f) || !NearF(PaceFactor(-1.5f), 1.0f) || !NearF(PaceFactor(3.0f), 1.0f)) Fail("follow: no slow-down within 1.5 u ahead, or behind");
    if (!NearF(PaceFactor(-5.0f), 0.85f) || !NearF(PaceFactor(-10.0f), 0.7f) || !NearF(PaceFactor(-40.0f), 0.7f)) Fail("follow: the cut grows to 30% at 10 u ahead");
    volatile float zero = 0.0f;
    if (!NearF(PaceFactor(zero / zero), 1.0f)) Fail("follow: an error that is not a number slows nothing");
    if (!NearF(PaceSlowSpeed(90.0f, -10.0f), 63.0f)) Fail("follow: the slowed speed is the base x the factor");
    /* the run raise against the target: 4 u in, 1 u out */
    if (BoostDecide(true, false, true, true, 4.5f, 0, 1, 2, 3) != 1 || BoostDecide(true, false, true, true, 3.9f, 0, 1, 2, 3) != 0)
        Fail("follow: the raise starts more than 4 u behind the target");
    if (BoostDecide(true, true, true, true, 1.5f, 0, 1, 2, 3) != 2 || BoostDecide(true, true, true, true, 0.9f, 0, 1, 2, 3) != 3)
        Fail("follow: the raise holds down to 1 u behind");
    if (BoostDecide(true, true, false, true, 20.0f, 0, 1, 2, 3) != 3 || BoostDecide(false, false, true, true, 20.0f, 0, 1, 2, 3) != 0)
        Fail("follow: no raise for a walker or with the lever off");
}
void t_follow_stop_detect_and_order()
{
    using namespace followplay;
    /* arming: any moving report of any character this game streams (its player faction or an NPC), whatever its task */
    if (!OwnerStopArm(false, kSampleMoving)) Fail("follow: a moving character arms its stop, whatever its faction");
    if (!OwnerStopArm(true, kSampleStill) || !OwnerStopArm(true, kSampleNone)) Fail("follow: a still or untrusted report keeps the arming");
    if (OwnerStopArm(false, kSampleStill) || OwnerStopArm(false, kSampleNone)) Fail("follow: a still or untrusted report alone arms nothing");
    /* due: the engine's flag 0 and the body still two frames running */
    if (StillFramesStep(0, 0.01f) != 1 || StillFramesStep(1, 0.04f) != 2 || StillFramesStep(2, 0.2f) != 0) Fail("follow: still frames count a body that moved under 0.05 u");
    volatile float zero = 0.0f;
    if (StillFramesStep(3, zero / zero) != 0) Fail("follow: an unreadable move is not still");
    if (!OwnerStopDue(true, 0, 2, 0)) Fail("follow: flag 0 and two still frames - the stop is due");
    if (OwnerStopDue(true, 0, 1, 0)) Fail("follow: one still frame is not yet a stop");
    if (OwnerStopDue(true, 1, 5, 5)) Fail("follow: the engine still moving the body is not a stop");
    if (!OwnerStopDue(true, -1, 0, 2) || OwnerStopDue(true, -1, 9, 1)) Fail("follow: an unreadable flag needs two still send passes");
    if (OwnerStopDue(false, 0, 9, 9)) Fail("follow: an unarmed character sends no stop");
    if (StillPassesStep(1, kSampleStill) != 2 || StillPassesStep(2, kSampleMoving) != 0 || StillPassesStep(2, kSampleNone) != 2) Fail("follow: still passes count still reports, reset on a moving one");
    /* the copy orders MOVE against the stop by the owner's clock */
    if (MoveVsStop(false, 0.0, 5.0, true, 0.0f, 0.0f, 0.0f, 0.0f) != kMoveApply) Fail("follow: with no stop a report applies");
    if (MoveVsStop(true, 2.0, 1.95, true, 5.0f, 0.0f, 0.0f, 0.0f) != kMoveStale || MoveVsStop(true, 2.0, 1.95, false, 5.0f, 0.0f, 0.0f, 0.0f) != kMoveStale)
        Fail("follow: a report stamped before the stop is dropped");
    if (MoveVsStop(true, 2.0, 2.1, true, 5.0f, 0.0f, 0.0f, 0.0f) != kMoveClearsStop) Fail("follow: a moving report stamped after the stop ends it");
    if (MoveVsStop(true, 2.0, 2.0, true, 5.0f, 0.0f, 0.0f, 0.0f) != kMoveStale) Fail("follow: a moving report at the stop's own moment does not end it");
    if (MoveVsStop(true, 2.0, 2.1, false, 0.0f, 0.0f, 0.0f, 0.0f) != kMoveApply) Fail("follow: a standing report after the stop keeps it");
    if (!StopAlreadyOver(true, 2.1, false, 5.0f, 0.0f, 2.0, 0.0f, 0.0f) || StopAlreadyOver(true, 2.1, true, 5.0f, 0.0f, 2.0, 0.0f, 0.0f)
        || StopAlreadyOver(true, 1.9, false, 5.0f, 0.0f, 2.0, 0.0f, 0.0f) || StopAlreadyOver(false, 0.0, false, 5.0f, 0.0f, 2.0, 0.0f, 0.0f))
        Fail("follow: a stop older than a moving report already received is over");
    /* the copy is still behind the stop point when it lands: D (0.2 s) exceeds the stop's two frames plus delivery */
    Track k = FpRunner(11, 0.0);   /* the owner stops at x = 70 at t = 1.0 */
    Push(&k, FpSample(70.0f, 0.0f, 0.0f, 0.0f, 1.03, true));
    Sample out;
    TargetAt(k, 1.03 + 0.05 - PlaybackDelay(0.1), kExtrapMaxSec, &out);
    if (!(70.0f - out.x >= 2.0f)) Fail("follow: when the stop lands the replayed target is still short of the stop point");
}
/* NPC copies in a crowd: the owner moves only while its newest moving report is fresh; an NPC copy walks only to N, takes the
   path only far from its target or after a stall, re-plans less often while hidden, and comes after every player's copy in
   the path budget; the stop's radius is wide for an NPC copy and for a copy that stopped closing on the stop point. */
void t_follow_npc_copies()
{
    using namespace followplay;
    volatile float zf = 0.0f; volatile double zd = 0.0;
    /* the owner moving now: the newest report moves and is within the horizon */
    Track k = FpRunner(5, 0.0);
    if (!OwnerMovingNow(k, 0.1, 0.35)) Fail("follow: a fresh moving newest report is an owner that moves");
    if (OwnerMovingNow(k, 0.36, 0.35)) Fail("follow: an expired moving report is an owner that went quiet: it stands");
    if (OwnerMovingNow(k, zd / zd, 0.35) || OwnerMovingNow(k, 0.1, zd / zd)) Fail("follow: an age that is not a number is not moving");
    Track e; TrackClear(&e);
    if (OwnerMovingNow(e, 0.0, 0.35)) Fail("follow: no recording is no moving owner");
    /* the destination of an NPC copy: N, never the keep-walking point; a goal nearer than N still ends it */
    DestIn in;
    in.stopHeld = false; in.sx = in.sz = 0.0f; in.hidden = false; in.ownerMoving = true;
    in.haveN = true; in.nx = 14.0f; in.nz = 0.0f; in.haveH = true; in.hx = 1.0f; in.hz = 0.0f; in.v = 70.0f;
    in.haveG = false; in.gx = in.gz = 0.0f; in.cx = 0.0f; in.cz = 0.0f; in.playerCopy = false;
    float dx = 0.0f, dz = 0.0f;
    if (FollowDest(in, &dx, &dz) != kDestNewest || !NearF(dx, 14.0f)) Fail("follow: a visible NPC copy of a moving owner walks only to N");
    in.haveG = true; in.gx = 40.0f; in.gz = 0.0f;
    if (FollowDest(in, &dx, &dz) != kDestNewest || !NearF(dx, 14.0f)) Fail("follow: a goal past N never takes an NPC copy past N");
    in.gx = 10.0f;
    if (FollowDest(in, &dx, &dz) != kDestGoal || !NearF(dx, 10.0f)) Fail("follow: an NPC copy stops at a goal nearer than N");
    in.stopHeld = true; in.sx = 20.0f; in.sz = 1.0f;
    if (FollowDest(in, &dx, &dz) != kDestStop || !NearF(dx, 20.0f)) Fail("follow: an NPC copy's stated stop is its destination");
    /* the path entry: a player's copy from the owner's first moving report; an NPC copy only beyond 6 u or after a stall */
    if (!FollowPathEnter(true, true, true, 1.0f, 6.0f, 3.0f, 0)) Fail("follow: a player's copy takes the path from its owner's first moving report");
    if (FollowPathEnter(true, true, false, 1.0f, 6.0f, 3.0f, 0)) Fail("follow: no heading, no early path");
    if (FollowPathEnter(false, true, true, 5.9f, 6.0f, 3.0f, 0)) Fail("follow: an NPC copy near its target keeps the push while its owner walks");
    if (!FollowPathEnter(false, true, true, 6.1f, 6.0f, 3.0f, 0)) Fail("follow: an NPC copy beyond 6 u of its target takes the path");
    if (!FollowPathEnter(false, false, false, 3.0f, 6.0f, 3.0f, 1) || FollowPathEnter(false, false, false, 2.9f, 6.0f, 3.0f, 1))
        Fail("follow: a stalled NPC copy 3 u or more from its target takes the path");
    if (FollowPathEnter(true, true, true, zf / zf, 6.0f, 3.0f, 1)) Fail("follow: a distance that is not a number enters nothing");
    /* the re-plan to N */
    if (!NearF(NewestReplanDist(false, true, 4.0f), 12.0f)) Fail("follow: a hidden NPC copy re-plans after N moved 12 u");
    if (!NearF(NewestReplanDist(false, false, 4.0f), 4.0f) || !NearF(NewestReplanDist(true, true, 4.0f), 4.0f))
        Fail("follow: a visible NPC copy and a player's copy keep the 4 u re-plan");
    /* the path budget: every player's copy before every NPC copy, stops first within each, oldest first within a rank */
    if (!(PathServeRank(true, true) < PathServeRank(true, false) && PathServeRank(true, false) < PathServeRank(false, true)
          && PathServeRank(false, true) < PathServeRank(false, false)))
        Fail("follow: the budget serves player stops, player re-plans, NPC stops, NPC re-plans, in that order");
    {
        /* 12 waiting NPC stop issues (all older) and 3 player re-plans, 8 grants: the 3 players are all served */
        std::vector<std::pair<std::pair<int, double>, unsigned int> > w;
        for (unsigned int u = 1; u <= 12; ++u) w.push_back(std::make_pair(std::make_pair(PathServeRank(false, true), 0.1 * u), u));
        for (unsigned int u = 100; u < 103; ++u) w.push_back(std::make_pair(std::make_pair(PathServeRank(true, false), 50.0), u));
        std::sort(w.begin(), w.end());
        int players = 0;
        for (size_t i = 0; i < 8 && i < w.size(); ++i) if (w[i].second >= 100) ++players;
        if (players != 3 || w[3].second != 1u) Fail("follow: a crowd of waiting NPC copies never holds back a player's copy; NPC copies then go oldest first");
    }
    if (!PathTakesLeftover(true, true) || PathTakesLeftover(true, false) || !PathTakesLeftover(false, false))
        Fail("follow: an NPC follow copy's issue always queues; a player's copy and other rows may take the leftover budget");
    /* the stop's arrival radius */
    if (!NearF(StopArriveRadius(true, false), 0.5f)) Fail("follow: a player's copy closing on its stop arrives within 0.5 u");
    if (!NearF(StopArriveRadius(false, false), 1.5f) || !NearF(StopArriveRadius(true, true), 1.5f))
        Fail("follow: an NPC copy, or a copy that stopped closing on the stop point, arrives within 1.5 u");
    StopProgress sp; StopProgressReset(&sp);
    if (StopProgressStep(&sp, 1.2f, 10.0)) Fail("follow: the first frame on a stop is not stalled");
    if (StopProgressStep(&sp, 1.1f, 10.6) || StopProgressStep(&sp, 1.15f, 10.9)) Fail("follow: under 1 s without closing is not stalled");
    if (!StopProgressStep(&sp, 1.05f, 11.0)) Fail("follow: 1 s without closing 0.25 u is stalled (a body jostled in a crowd)");
    StopProgressReset(&sp);
    StopProgressStep(&sp, 5.0f, 20.0);
    if (StopProgressStep(&sp, 4.7f, 20.8) || StopProgressStep(&sp, 4.6f, 21.7)) Fail("follow: closing 0.25 u restarts the watch");
    if (!StopProgressStep(&sp, 4.6f, 21.81)) Fail("follow: no closing since the last restart for 1 s is stalled");
    StopProgressReset(&sp);
    StopProgressStep(&sp, 2.0f, 30.0);
    if (StopProgressStep(&sp, zf / zf, 31.5) || StopProgressStep(&sp, 2.0f, zd / zd)) Fail("follow: a value that is not a number is not stalled");
    if (StopProgressStep(&sp, 2.0f, 29.0)) Fail("follow: a clock that went backwards restarts the watch");
}
/* An idle copy skips the replay: the owner stands, the copy stands within its arrival radius on no path, and the last
   moving report arrived more than the delay plus 1 s ago. Anything else - or a value that is not a number - runs it. */
void t_follow_replay_idle()
{
    using namespace followplay;
    const double D = PlaybackDelay(0.1);
    if (!ReplayIdle(false, false, 0.3f, 1.5f, D + kReplaySettleSec + 0.01, D)) Fail("follow: a standing copy on its owner's standing point is idle");
    if (!ReplayIdle(false, false, 0.3f, 1.5f, 1.0e9, D)) Fail("follow: a copy whose owner never moved is idle on its point");
    if (ReplayIdle(true, false, 0.3f, 1.5f, 1.0e9, D)) Fail("follow: an owner that moves keeps the replay running");
    if (ReplayIdle(false, true, 0.3f, 1.5f, 1.0e9, D)) Fail("follow: a copy on a path keeps the replay running");
    if (ReplayIdle(false, false, 1.6f, 1.5f, 1.0e9, D)) Fail("follow: a copy off its owner's point keeps the replay running");
    if (!ReplayIdle(false, false, 0.4f, kStopArriveDist, 1.0e9, D) || ReplayIdle(false, false, 0.6f, kStopArriveDist, 1.0e9, D))
        Fail("follow: on a stated stop the stop's arrival radius decides");
    if (ReplayIdle(false, false, 0.3f, 1.5f, D + kReplaySettleSec - 0.01, D)) Fail("follow: the replay runs until it has passed the last moving report");
    volatile float zf = 0.0f; volatile double zd = 0.0;
    if (ReplayIdle(false, false, zf / zf, 1.5f, 1.0e9, D) || ReplayIdle(false, false, 0.3f, 1.5f, zd / zd, D) || ReplayIdle(false, false, 0.3f, 1.5f, 1.0e9, zd / zd))
        Fail("follow: a value that is not a number is not idle");
}
/* One stop per trip: the owner's first report after its body stopped still reads moving (distance since the previous report)
   and sits on the stop point. It neither arms a second stop on the owner nor ends the stop on the copy, in either arrival
   order; a report off the point by more than 0.5 u does both. */
void t_follow_stop_trailing_report()
{
    using namespace followplay;
    if (MovedOffSentStop(70.0f, 0.0f, 70.0f, 0.0f) || MovedOffSentStop(70.4f, 0.2f, 70.0f, 0.0f))
        Fail("follow: a moving report on the stop point just sent re-arms the stop (a second MOVESTOP)");
    if (!MovedOffSentStop(70.6f, 0.0f, 70.0f, 0.0f)) Fail("follow: a moving report off the stop point arms the next stop");
    volatile float zero = 0.0f;
    if (!MovedOffSentStop(zero / zero, 0.0f, 70.0f, 0.0f)) Fail("follow: an unreadable position is not known to sit on the stop point");
    if (MoveVsStop(true, 2.0, 2.1, true, 70.0f, 0.0f, 70.0f, 0.0f) != kMoveStale || MoveVsStop(true, 2.0, 2.1, true, 70.3f, 0.3f, 70.0f, 0.0f) != kMoveStale)
        Fail("follow: the stop's trailing moving report ends the stop (the copy twitches)");
    if (MoveVsStop(true, 2.0, 2.2, true, 70.6f, 0.0f, 70.0f, 0.0f) != kMoveClearsStop) Fail("follow: a moving report 0.6 u off the stop point ends it");
    if (StopAlreadyOver(true, 2.1, false, 70.2f, 0.0f, 2.0, 70.0f, 0.0f)) Fail("follow: a stop arriving after its own trailing report is thrown away");
    if (!StopAlreadyOver(true, 2.1, false, 71.0f, 0.0f, 2.0, 70.0f, 0.0f)) Fail("follow: a stop arriving after the owner walked off is over");
}
/* Behind schedule: a target further from the owner's newest reported point than speed x (D + 0.1 s) + 4 u. A healthy
   recording never is; a recording that stopped taking reports while the owner ran on is; the state leaves only at speed x
   (D + 0.05 s) + 2 u or nearer. A snap places a follow copy on the stated stop point, else on the owner's newest point. */
void t_follow_behind_schedule()
{
    using namespace followplay;
    const double D = PlaybackDelay(0.1);
    Sample t;
    /* a healthy recording: the newest report (x 70 at owner time 1.0) arrived 0.04 s late - the target trails it by 11.2 u */
    Track k = FpRunner(11, 0.0);
    TargetAt(k, 1.04 - D, kExtrapMaxSec, &t);
    if (BehindScheduleStep(false, t.x, t.z, 70.0f, 0.0f, 70.0f, D)) Fail("follow: a healthy recording's target is on schedule");
    TargetAt(k, 1.0 - D, kExtrapMaxSec, &t);
    if (BehindScheduleStep(false, t.x, t.z, 70.0f, 0.0f, 70.0f, D)) Fail("follow: a target a full delay behind the newest report is on schedule");
    /* the recording stopped at x 35 (owner time 0.5) while the owner's reports ran on to x 70: the target holds at 38.5 */
    Track f = FpRunner(6, 0.0);
    if (TargetAt(f, 1.04 - D, kExtrapMaxSec, &t) != kModeHeld || !NearF(t.x, 38.5f)) Fail("follow: a stopped recording holds its target");
    if (!BehindScheduleStep(false, t.x, t.z, 70.0f, 0.0f, 70.0f, D)) Fail("follow: a target 31.5 u behind a 70 u/s owner's newest point is behind schedule");
    /* the thresholds: enter past 70 x 0.3 + 4 = 25 u, leave at 70 x 0.25 + 2 = 19.5 u */
    if (BehindScheduleStep(false, 0.0f, 0.0f, 24.9f, 0.0f, 70.0f, D) || !BehindScheduleStep(false, 0.0f, 0.0f, 25.1f, 0.0f, 70.0f, D))
        Fail("follow: a runner's copy falls behind schedule past 25 u");
    if (!BehindScheduleStep(true, 0.0f, 0.0f, 19.6f, 0.0f, 70.0f, D) || BehindScheduleStep(true, 0.0f, 0.0f, 19.4f, 0.0f, 70.0f, D))
        Fail("follow: a runner's copy is back on schedule at 19.5 u, not before");
    if (BehindScheduleStep(false, 0.0f, 0.0f, 0.0f, 12.0f, 27.0f, D) || !BehindScheduleStep(false, 0.0f, 0.0f, 0.0f, 12.2f, 27.0f, D))
        Fail("follow: a walker's copy falls behind schedule past 12.1 u");
    /* a standing owner: past 4 u, back at 2 u */
    if (BehindScheduleStep(false, 0.0f, 0.0f, 3.9f, 0.0f, 0.0f, D) || !BehindScheduleStep(false, 0.0f, 0.0f, 4.1f, 0.0f, 0.0f, D))
        Fail("follow: a standing owner's copy falls behind schedule past 4 u");
    if (!BehindScheduleStep(true, 0.0f, 0.0f, 2.1f, 0.0f, 0.0f, D) || BehindScheduleStep(true, 0.0f, 0.0f, 1.9f, 0.0f, 0.0f, D))
        Fail("follow: a standing owner's copy is back on schedule at 2 u");
    /* values that are not usable numbers */
    volatile float zf = 0.0f; volatile double zd = 0.0;
    if (BehindScheduleStep(true, zf / zf, 0.0f, 70.0f, 0.0f, 70.0f, D) || BehindScheduleStep(true, 0.0f, 0.0f, 70.0f, zf / zf, 70.0f, D))
        Fail("follow: a position that is not a number is not behind schedule");
    if (BehindScheduleStep(false, 0.0f, 0.0f, 3.9f, 0.0f, zf / zf, D) || !BehindScheduleStep(false, 0.0f, 0.0f, 4.1f, 0.0f, -5.0f, D))
        Fail("follow: a speed that is not a number, or below 0, counts as a standing owner");
    if (BehindScheduleStep(false, 0.0f, 0.0f, 24.9f, 0.0f, 70.0f, zd / zd) || !BehindScheduleStep(false, 0.0f, 0.0f, 25.1f, 0.0f, 70.0f, -1.0))
        Fail("follow: a delay that is not usable counts as the 0.2 s default");
    /* the owner's point a snap places a follow copy on */
    float px = 0.0f, pz = 0.0f;
    OwnerPoint(true, 20.0f, 1.0f, 70.0f, 0.0f, &px, &pz);
    if (!NearF(px, 20.0f) || !NearF(pz, 1.0f)) Fail("follow: with a stop stated a snap places the copy on the stop point");
    OwnerPoint(false, 20.0f, 1.0f, 70.0f, 0.0f, &px, &pz);
    if (!NearF(px, 70.0f) || !NearF(pz, 0.0f)) Fail("follow: with no stop stated a snap places the copy on the owner's newest point");
}
/* A held stop ends when the owner leaves it without a moving report (prone, crawling, ragdolled, carried: velocity 0): any
   report stamped after it more than 2 u from the stop point, or an owner teleport. */
void t_follow_stop_left_without_moving_report()
{
    using namespace followplay;
    if (StopLeftBy(false, 2.0, 3.0, 90.0f, 0.0f, 70.0f, 0.0f, true)) Fail("follow: no stop held, nothing to end");
    if (!StopLeftBy(true, 2.0, 3.0, 72.5f, 0.0f, 70.0f, 0.0f, false)) Fail("follow: a report 2.5 u from the stop point leaves the copy pinned there");
    if (StopLeftBy(true, 2.0, 3.0, 71.5f, 0.0f, 70.0f, 0.0f, false)) Fail("follow: a report 1.5 u from the stop point ends the stop");
    if (StopLeftBy(true, 2.0, 1.9, 75.0f, 0.0f, 70.0f, 0.0f, false) || StopLeftBy(true, 2.0, 2.0, 75.0f, 0.0f, 70.0f, 0.0f, false))
        Fail("follow: a report not stamped after the stop ends it");
    if (!StopLeftBy(true, 2.0, 2.0, 70.0f, 0.0f, 70.0f, 0.0f, true)) Fail("follow: an owner teleport ends the stop");
    volatile float zero = 0.0f;
    if (StopLeftBy(true, 2.0, 3.0, zero / zero, 0.0f, 70.0f, 0.0f, false)) Fail("follow: an unreadable position ends the stop");
}
/* The owner's Move-order goal on the wire: re-sent when it moved more than 2 u, no sooner than 0.5 s after the last INTENT
   unless it jumped more than 10 u (a new click). */
void t_follow_goal_resend()
{
    using namespace cooppath;
    if (!GoalResendDue(true, 3.0f, 0.0f, 0.0f, 0.0f, 0.5)) Fail("goal: a goal moved 3 u after 0.5 s must be re-sent");
    if (GoalResendDue(true, 3.0f, 0.0f, 0.0f, 0.0f, 0.4)) Fail("goal: a goal re-send waits 0.5 s after the last INTENT");
    if (!GoalResendDue(true, 15.0f, 0.0f, 0.0f, 0.0f, 0.1)) Fail("goal: a goal that jumped more than 10 u goes at once");
    if (GoalResendDue(true, 2.0f, 0.0f, 0.0f, 0.0f, 1.0) || GoalResendDue(true, 1.0f, 1.0f, 0.0f, 0.0f, 9.0)) Fail("goal: a goal moved 2 u or less is not re-sent");
    if (GoalResendDue(false, 30.0f, 0.0f, 0.0f, 0.0f, 9.0)) Fail("goal: only a Move order's goal is re-sent");
    volatile float zero = 0.0f;
    if (GoalResendDue(true, zero / zero, 0.0f, 0.0f, 0.0f, 9.0)) Fail("goal: a goal that is not a number is not re-sent");
}
/* MOVESTOP (72) rides the world server: taken from it (its handler checks the owner, as MOVE's does), AREA like the rest of
   the character's stream, not behind the relayed-owner gate, a one-off (a missed one is caught up), not a stream column. */
void t_follow_movestop_on_live_road()
{
    if (cooplive::kInnerMoveStop != 72u) Fail("follow: MOVESTOP's inner number is not the transport's");
    if (!cooplive::LiveInnerAccepted(72)) Fail("follow: MOVESTOP must be taken from the world server");
    if (cooplive::CharStreamRoute(72) != (unsigned int)cooplive::kRouteArea) Fail("follow: MOVESTOP goes AREA on the world road");
    if (cooplive::CharStreamNeedsOwnerGate(72)) Fail("follow: MOVESTOP's handler has its own owner test");
    if (!cooplive::AreaMissedIsOneOff(72)) Fail("follow: a missed MOVESTOP is a one-off");
    if (cooplive::CharStreamIndex(72) != -1) Fail("follow: MOVESTOP is not a counted stream column (the twelve are unchanged)");
    if (cooplive::LiveInnerWaitsForWorld(72)) Fail("follow: a relayed MOVESTOP is not queued while the world loads (MOVE is not)");
}
/* join1 (2026-09-24): a WELCOME's loopback notebook address means the HOST's machine - the joiner dials the host address
   it used instead; a joiner that itself dialled a loopback host (one computer) keeps the named address. */
void t_join1_notebook_addr_for_joiner()
{
    using namespace coopstore;
    int sw = -1;
    if (NotebookAddrForJoiner("127.0.0.1", "203.0.113.7", &sw) != "203.0.113.7" || sw != 1) Fail("a loopback notebook must become the dialled host");
    if (NotebookAddrForJoiner("localhost", "friend.example.net", &sw) != "friend.example.net" || sw != 1) Fail("localhost must become the dialled host");
    if (NotebookAddrForJoiner("0.0.0.0", "192.168.1.20", &sw) != "192.168.1.20" || sw != 1) Fail("an unspecified notebook address must become the dialled host");
    if (NotebookAddrForJoiner("127.0.0.1", "127.0.0.1", &sw) != "127.0.0.1" || sw != 0) Fail("one computer: a loopback host keeps the named loopback");
    if (NotebookAddrForJoiner("127.0.0.1", "", &sw) != "127.0.0.1" || sw != 0) Fail("no dialled host: keep the named address");
    if (NotebookAddrForJoiner("198.51.100.4", "203.0.113.7", &sw) != "198.51.100.4" || sw != 0) Fail("a real notebook address is never replaced");
    if (NotebookAddrForJoiner("127.0.0.1", "127.8.9.1", &sw) != "127.0.0.1" || sw != 0) Fail("any 127.x host counts as loopback");
}

/* addr1 (2026-09-26): which host address replaces a WELCOME's loopback notebook address - the session peer first. */
void t_addr1_notebook_dial_from_welcome()
{
    using namespace coopstore;
    int sw = -1; std::string why;
    if (NotebookDialFromWelcome("127.0.0.1", "192.168.1.20", "192.168.1.20", "", &sw, &why) != "192.168.1.20" || sw != 1 || why.empty())
        Fail("two PCs: a loopback notebook becomes the host's peer address");
    if (NotebookDialFromWelcome("127.0.0.1", "192.168.1.20", "myhost.lan", "127.0.0.1", &sw, &why) != "192.168.1.20" || sw != 1)
        Fail("the peer address wins over the dialled name and the config file");
    if (NotebookDialFromWelcome("127.0.0.1", "", "203.0.113.7", "127.0.0.1", &sw, &why) != "203.0.113.7" || sw != 1)
        Fail("no peer address: the dialled address (a join verb) wins over a stale config host");
    if (NotebookDialFromWelcome("0.0.0.0", "0.0.0.0", "", "198.51.100.9", &sw, &why) != "198.51.100.9" || sw != 1)
        Fail("an unspecified peer address falls through to the config host");
    if (NotebookDialFromWelcome("127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", &sw, &why) != "127.0.0.1" || sw != 0 || why.find("both games") == std::string::npos)
        Fail("one computer: loopback reached at loopback stays loopback");
    if (NotebookDialFromWelcome("127.0.0.1", "", "", "", &sw, &why) != "127.0.0.1" || sw != 0 || why.find("no host address") == std::string::npos)
        Fail("no host address known: keep the named address");
    if (NotebookDialFromWelcome("198.51.100.4", "192.168.1.20", "192.168.1.20", "", &sw, &why) != "198.51.100.4" || sw != 0 || why.find("real address") == std::string::npos)
        Fail("a real notebook address is never replaced");
    if (NotebookDialFromWelcome("127.0.0.1", "192.168.1.20", "", "", &sw, 0) != "192.168.1.20" || sw != 1)
        Fail("a null reason pointer is allowed");
}

/* T-296 fix 2 (H055): every combination of the wake observations, the reload gate and the create gate. M1: whenever the
   reload may run (and so the container is emptied), +0x40 is outside the container - moved, ours, or never inside. */
void t_t632_create_pos_agrees()
{
    using namespace coopstate;
    if (CreatePosAgrees(-62830.7f, 16433.2f, -62830.7f, 16433.2f) != 1) Fail("the record's own position must agree");
    if (CreatePosAgrees(-62830.2f, 16432.4f, -62830.7f, 16433.2f) != 1) Fail("within one unit must agree");
    if (CreatePosAgrees(0.0f, 0.0f, -62830.7f, 16433.2f) != 0) Fail("a squad asleep at 0,0 is not at its record's position");
    if (CreatePosAgrees(-62830.7f, 16435.0f, -62830.7f, 16433.2f) != 0) Fail("two units off in z must not agree");
    if (CreatePosAgrees(-62828.0f, 16433.2f, -62830.7f, 16433.2f) != 0) Fail("2.7 units off in x must not agree");
    volatile float zero = 0.0f; const float nan = zero / zero;
    if (CreatePosAgrees(nan, 16433.2f, -62830.7f, 16433.2f) != 0) Fail("a NaN sleeping x never agrees");
    if (CreatePosAgrees(-62830.7f, nan, -62830.7f, 16433.2f) != 0) Fail("a NaN sleeping z never agrees");
}

void t_t296_state_wake_decide()
{
    using namespace coopstate;
    int tr, ou, wk, mem, mv;
    for (tr = 0; tr <= 1; ++tr) for (ou = 0; ou <= 1; ++ou) for (wk = 0; wk <= 1; ++wk) for (mem = 0; mem <= 1; ++mem) for (mv = 0; mv <= 1; ++mv)
    {
        if (ou && !tr) continue;   /* "ours" exists only for a tracked squad */
        const int a = StateWakeDecide(tr, ou, wk, mem);
        const int ok = StateReloadAllowed(a, mv);
        const std::string at = " (tracked=" + I(tr) + " ours=" + I(ou) + " walkOk=" + I(wk) + " member=" + I(mem) + " moveOk=" + I(mv) + ")";
        if (wk && mem && a != kStMove) Fail("a record inside the container must be moved" + at);
        if (!(wk && mem) && tr && ou && a != kStKeep) Fail("our global record must be kept" + at);
        if (!wk && !(tr && ou) && a != kStRefuse) Fail("an unwalkable container must refuse the reload" + at);
        if (wk && !mem && !(tr && ou) && a != kStLeave) Fail("a record outside the container must be left" + at);
        if ((a == kStKeep || a == kStLeave) && wk && mem) Fail("nothing inside the container may be kept or left" + at);
        if (a == kStLeave && !(wk && !mem)) Fail("leave only when the walk showed it outside" + at);
        const int outsideAfter = (a == kStMove && mv) || a == kStKeep || a == kStLeave;
        if (ok && !outsideAfter) Fail("M1: the reload may only run with +0x40 outside the container" + at);
        if (!ok && a != kStRefuse && !(a == kStMove && !mv)) Fail("the reload is refused only for an unknown or an unmoved record" + at);
        if (std::string(StateActionWord(a)) == "?") Fail("every action must have a word" + at);
    }
    int h, m, c;
    for (h = 0; h <= 1; ++h) for (m = 0; m <= 1; ++m) for (c = 0; c <= 1; ++c)
        if (StateCreateDecide(h, m, c) != ((h && m && c) ? 1 : 0)) Fail("create only on a complete 0x22 record (have1E=" + I(h) + " made22=" + I(m) + " copyOk=" + I(c) + ")");
    if (std::string(StateActionWord(99)) != "?") Fail("an unknown action must read ?");
}


/* ------------------------------------------------------------------------------------------------
   P8h - THE ADDRESS TABLE. Three tests, and between them they cover the three things that decide
   whether a public build of this mod installs on somebody else's Kenshi: the fingerprint arithmetic,
   the table parser, and the byte comparison that refuses. Every input below is a literal; no test's
   precondition is computed by the function it is checking (design-offline-tests 7.5).
   ------------------------------------------------------------------------------------------------ */

/* A minimal 64-bit PE header, built here so the test depends on no file existing. Layout: "MZ" at 0,
   e_lfanew at 0x3C, "PE\0\0" at e_lfanew, TimeDateStamp at +8, optional-header magic at +24,
   SizeOfImage at +24+56. */
void MakePeHead(unsigned char* b, size_t n, unsigned int e_lfanew,
                unsigned short magic, unsigned int tds, unsigned int soi)
{
    std::memset(b, 0, n);
    b[0] = 'M'; b[1] = 'Z';
    std::memcpy(b + 0x3C, &e_lfanew, 4);
    b[e_lfanew + 0] = 'P'; b[e_lfanew + 1] = 'E'; b[e_lfanew + 2] = 0; b[e_lfanew + 3] = 0;
    std::memcpy(b + e_lfanew + 8, &tds, 4);
    std::memcpy(b + e_lfanew + 24, &magic, 2);
    std::memcpy(b + e_lfanew + 24 + 56, &soi, 4);
}

/* T-63 stage 7: THE PATTERN ROAD, on a synthetic PE32+ built here (.text / .rdata / .data), one row of every kind.
   The real-executable proof is tools/prove_signatures.py; this pins the C++ resolver's own arithmetic and refusals. */
namespace sigt {
const unsigned int kText = 0x1000, kRdata = 0x2000, kData = 0x3000;
void Put32(std::vector<unsigned char>& b, size_t o, unsigned int v)
{ b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); b[o + 2] = (unsigned char)(v >> 16); b[o + 3] = (unsigned char)(v >> 24); }
void Put16(std::vector<unsigned char>& b, size_t o, unsigned int v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); }
size_t T(unsigned int rva) { return 0x400 + (rva - kText); }
size_t R(unsigned int rva) { return 0x1400 + (rva - kRdata); }
void PutBytes(std::vector<unsigned char>& b, size_t o, const unsigned char* p, size_t n) { size_t i; for (i = 0; i < n; ++i) b[o + i] = p[i]; }
void Section(std::vector<unsigned char>& b, int i, const char* name, unsigned int vsize, unsigned int va,
             unsigned int rsize, unsigned int rptr, unsigned int chars)
{
    const size_t o = 0x188 + 40 * (size_t)i;
    size_t k;
    for (k = 0; k < 8; ++k) b[o + k] = (unsigned char)(k < std::strlen(name) ? name[k] : 0);
    Put32(b, o + 8, vsize); Put32(b, o + 12, va); Put32(b, o + 16, rsize); Put32(b, o + 20, rptr); Put32(b, o + 36, chars);
}
std::vector<unsigned char> Image()
{
    std::vector<unsigned char> b(0x1800, 0);
    b[0] = 'M'; b[1] = 'Z'; Put32(b, 0x3C, 0x80);
    b[0x80] = 'P'; b[0x81] = 'E';
    Put16(b, 0x84, 0x8664); Put16(b, 0x86, 3); Put16(b, 0x94, 0xF0);
    Put16(b, 0x98, 0x20B); Put32(b, 0x98 + 24, 0x40000000u); Put32(b, 0x98 + 28, 0x1u);   /* ImageBase 0x140000000 */
    Section(b, 0, ".text", 0x1000, kText, 0x1000, 0x400, 0x60000020u);
    Section(b, 1, ".rdata", 0x200, kRdata, 0x200, 0x1400, 0x40000040u);
    Section(b, 2, ".data", 0x400, kData, 0x200, 0x1600, 0xC0000040u);
    /* Fn at 0x1100: mov [rsp+8],rbx / push rdi / sub rsp,20h / mov rax,[rip -> 0x3010] / nop / ret */
    const unsigned char fn[] = { 0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0x05,0,0,0,0, 0x90, 0xC3 };
    PutBytes(b, T(0x1100), fn, sizeof(fn));
    Put32(b, T(0x110D), 0x3010 - 0x1111);
    /* Ret: a call at 0x1200 to Fn (0x1100), its return address 0x1205 */
    const unsigned char rt[] = { 0xE8,0xFB,0xFE,0xFF,0xFF, 0x85,0xC0, 0x74,0x05, 0xB8,0x01,0x00,0x00,0x00, 0xC3 };
    PutBytes(b, T(0x1200), rt, sizeof(rt));
    /* gog1 fold: a SECOND reference to the global, in another function: mov ecx,[rip -> 0x3010] / ret at 0x1600 */
    const unsigned char g2[] = { 0x8B,0x0D,0,0,0,0, 0xC3, 0xCC,0xCC,0xCC };
    PutBytes(b, T(0x1600), g2, sizeof(g2));
    Put32(b, T(0x1602), 0x3010 - 0x1606);
    /* Konst: movss xmm0,[rip -> 0x2040] at 0x1300, ret, int3 x3; the constant in .rdata */
    const unsigned char ro[] = { 0xF3,0x0F,0x10,0x05,0,0,0,0, 0xC3, 0xCC,0xCC,0xCC };
    PutBytes(b, T(0x1300), ro, sizeof(ro));
    Put32(b, T(0x1304), 0x2040 - 0x1308);
    const unsigned char k8[] = { 0x00,0x00,0x80,0x3F, 0x00,0x00,0x00,0x40 };
    PutBytes(b, R(0x2040), k8, sizeof(k8));
    /* Vt: lea rax,[rip -> 0x2080] / mov [rcx],rax / ret at 0x1350; slot +8 -> stub 0x1400 -> jmp 0x1500 */
    const unsigned char vt[] = { 0x48,0x8D,0x05,0,0,0,0, 0x48,0x89,0x01, 0xC3 };
    PutBytes(b, T(0x1350), vt, sizeof(vt));
    Put32(b, T(0x1353), 0x2080 - 0x1357);
    Put32(b, R(0x2088), 0x40001400u); Put32(b, R(0x208C), 0x1u);
    b[T(0x1400)] = 0xE9; Put32(b, T(0x1401), 0x1500 - 0x1405);
    const unsigned char tg[] = { 0x40,0x53,0x48,0x83,0xEC,0x20, 0xC3 };
    PutBytes(b, T(0x1500), tg, sizeof(tg));
    return b;
}
const char* kGood =
    "# synthetic\n!sigversion 2\n!source X Y\n!count 5\n!nosigcount 0\n"
    "Fn code 0 48895C2408574883EC20488B05????????90C3 0 0 16 0 - .text 0 -\n"
    "Glob var 0 48895C2408574883EC20488B05????????90C3 13 4 0 0 - .data 0 g:6:8B0D????????C3CCCCCC:2:4:0\n"
    "Ret ret 5 E8????????85C07405B801000000 5 5 0 0 E8???????? .text 1 k:0:Fn\n"
    "Konst ro 0 F30F1005????????C3CCCCCC 4 4 0 0 0000803F00000040 .rdata 0 -\n"
    "Vt ptr 7 488D05????????488901C3 3 4 0 8 40534883EC20 .rdata 1 -\n";
int Resolve(const std::vector<unsigned char>& img, const std::string& text, coopaddr::AddrEntry* e, coopsigtab::SigOutcome* oc)
{
    coopsigtab::SigFile f;
    coopsigtab::SigParseFile(text.c_str(), text.size(), &f);
    coopsigtab::PeView pe;
    if (!coopsigtab::SigParsePe(&img[0], img.size(), &pe)) return -1;
    return coopsigtab::SigResolveAll(pe, f, e, 16, oc);
}
bool Mentions(const coopsigtab::SigOutcome& oc, const std::string& what)
{
    size_t i;
    for (i = 0; i < oc.fails.size(); ++i) if (oc.fails[i].find(what) != std::string::npos) return true;
    return false;
}
std::string Swap(const std::string& s, const std::string& from, const std::string& to)
{
    std::string r = s;
    const size_t p = r.find(from);
    if (p != std::string::npos) r.replace(p, from.size(), to);
    return r;
}
}   /* namespace sigt */

/* gog1 fold 2: EVERY SLOT CLEARED after a pattern set fails its running-game check (addrtable.cpp AddrSettleSlots, the
   bind AddrInit makes in addresses.cpp). Literal inputs: two entries, slots preset to a marker. */
void t_addr_pattern_refusal_clears_every_slot()
{
    coopaddr::AddrEntry e[2];
    std::memcpy(e[0].name, "Alpha", 6); e[0].rva = 0x1100u;
    std::memcpy(e[1].name, "Beta", 5); e[1].rva = 0x2200u;
    unsigned long long a = 0, b = 0, g = 0;
    const coopaddr::AddrSlot two[2] = { { "Alpha", &a }, { "Beta", &b } };
    const coopaddr::AddrSlot three[3] = { { "Alpha", &a }, { "Beta", &b }, { "Gamma", &g } };
    int missing = -1, first = -2;
    if (coopaddr::AddrSettleSlots(e, 2, two, 2, 0, 1, &missing, &first) != 1 || a != 0x1100u || b != 0x2200u || missing != 0 || first != -1)
        Fail("a clean pattern set was not bound: a=" + I((int)a) + " b=" + I((int)b) + " missing=" + I(missing));
    a = b = 0xDEADull;   /* pattern road, a row failed its check against the running game */
    if (coopaddr::AddrSettleSlots(e, 2, two, 2, 1, 1, &missing, &first) != 0 || a != 0 || b != 0)
        Fail("a pattern set that failed its running-game check left a slot filled: a=" + I((int)a) + " b=" + I((int)b));
    a = b = g = 0xDEADull;   /* pattern road, a registered name the set lacks */
    if (coopaddr::AddrSettleSlots(e, 2, three, 3, 0, 1, &missing, &first) != 0 || a != 0 || b != 0 || g != 0 || missing != 1 || first != 2)
        Fail("a pattern set missing a registered name was not refused with every slot cleared: missing=" + I(missing) + " first=" + I(first));
    a = b = 0;   /* the table road's refusal leaves the slots filled (addresses.h AddrAbs: a refusal does not mean 0) */
    if (coopaddr::AddrSettleSlots(e, 2, two, 2, 1, 0, &missing, &first) != 0 || a != 0x1100u || b != 0x2200u)
        Fail("a table-road refusal changed: a=" + I((int)a) + " b=" + I((int)b));
    a = b = g = 7;
    coopaddr::AddrClearSlots(three, 3);
    if (a != 0 || b != 0 || g != 0) Fail("AddrClearSlots left a slot filled");
}

/* rk1: A ROW ANOTHER MOD HAS HOOKED (src/common/addrhooked.h AddrHookedVerdict, the gate's call in addresses.cpp VerifyEntry).
   Literal inputs: the 1.0.65 SoundPostEvent row and the bytes run T1339 read under RE_Kenshi 0.3.5 (log-A line 4). */
static void Rk1Hex(const char* h, unsigned char* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        unsigned int v = 0;
        for (int k = 0; k < 2; ++k)
        {
            const char c = h[2 * i + k];
            v = v * 16u + (unsigned int)(c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - '0');
        }
        out[i] = (unsigned char)v;
    }
}
void t_rk1_addr_hooked_call_only_verdict()
{
    using namespace coopaddr;
    unsigned char want[16], got[16];
    Rk1Hex("48895C240848896C241048897C241841", want, 16);
    /* exact match: matched whatever the use and wherever the bytes are read */
    Rk1Hex("48895C240848896C241048897C241841", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 0) != kHookExact || AddrHookedVerdict(want, got, 16, 0, 1) != kHookExact)
        Fail("rk1: an exact match is not kHookExact");
    /* T1339: E9 + 4 bytes, the rest the same, call-only -> accepted */
    Rk1Hex("E94BAB56FF48896C241048897C241841", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookAccepted) Fail("rk1: the T1339 SoundPostEvent bytes on a call-only row were not accepted");
    /* the same bytes on a strict row (hooked or patched by this mod, or undeclared) -> refused */
    if (AddrHookedVerdict(want, got, 16, 1, 0) != kHookRefuse) Fail("rk1: a hooked strict row was accepted");
    /* the same bytes where the row's bytes are not read at the function's own start -> refused */
    if (AddrHookedVerdict(want, got, 16, 0, 1) != kHookRefuse) Fail("rk1: a hooked row read away from its entry was accepted");
    /* E9 at the start but a later byte differs -> not the same function: refused */
    Rk1Hex("E94BAB56FF48896C241048897C241941", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookRefuse) Fail("rk1: E9 with a different tail was accepted");
    Rk1Hex("E94BAB56FF49896C241048897C241841", got, 16);   /* the first byte after the jump */
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookRefuse) Fail("rk1: E9 with byte 5 different was accepted");
    /* a difference that is not an E9 jump -> refused: a different first byte, a short EB jump, an FF 25 absolute jump */
    Rk1Hex("48895C240848896C241048897C241842", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookRefuse) Fail("rk1: a plain difference was accepted");
    Rk1Hex("EB4BAB56FF48896C241048897C241841", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookRefuse) Fail("rk1: an EB jump was accepted");
    Rk1Hex("FF25000000001122334455667788AABB", got, 16);
    if (AddrHookedVerdict(want, got, 16, 1, 1) != kHookRefuse) Fail("rk1: an FF 25 jump was accepted");
    /* 5 bytes or fewer: nothing after the jump to prove it -> refused */
    Rk1Hex("E94BAB56FF", got, 5);
    if (AddrHookedVerdict(want, got, 5, 1, 1) != kHookRefuse) Fail("rk1: a 5-byte row with a jump was accepted");
    if (AddrHookedVerdict(want, got, 3, 1, 1) != kHookRefuse) Fail("rk1: a 3-byte row with a jump was accepted");
    Rk1Hex("E94BAB56FF48", got, 6);   /* 6 bytes: one byte after the jump, matching - too little to prove it (review N2) */
    if (AddrHookedVerdict(want, got, 6, 1, 1) != kHookRefuse) Fail("rk1: a 6-byte row, jump plus one matching byte, was accepted");
    Rk1Hex("E94BAB56FF48896C241048897C", got, 13);   /* 13 bytes: the jump plus 8 matching - accepted */
    if (AddrHookedVerdict(want, got, 13, 1, 1) != kHookAccepted) Fail("rk1: a 13-byte row, jump plus 8 matching, was not accepted");
    {   /* review N3: a function that itself starts with a jump, its distance changed - refused */
        unsigned char w2[16], g2[16];
        Rk1Hex("E9000000004889742410574883EC2049", w2, 16);
        Rk1Hex("E9111111114889742410574883EC2049", g2, 16);
        if (AddrHookedVerdict(w2, g2, 16, 1, 1) != kHookRefuse) Fail("rk1: a jump-started function with a changed jump was accepted");
    }
    /* no bytes at all */
    if (AddrHookedVerdict(want, got, 0, 1, 1) != kHookRefuse || AddrHookedVerdict(0, got, 16, 1, 1) != kHookRefuse)
        Fail("rk1: no bytes was not refused");
}

/* review S2/S3 (2026-10-17): src/common/addrhooked.h AddrUsesAllCallOnly - the rule behind RowCallOnly (every registration
   of a NAME) and AddrHookRefusedAt (every registration bound to a hook's ADDRESS): call-only only when all are call-only. */
void t_rk1_addr_uses_all_call_only()
{
    using namespace coopaddr;
    const unsigned char allCall[3] = { 1, 1, 1 };
    const unsigned char oneStrict[3] = { 1, 0, 1 };
    const unsigned char lastStrict[2] = { 1, 0 };
    const unsigned char odd[2] = { 1, 7 };   /* a value that is not call-only counts as strict */
    if (AddrUsesAllCallOnly(allCall, 3) != 1) Fail("S2: every registration call-only is not call-only");
    if (AddrUsesAllCallOnly(allCall, 1) != 1) Fail("S2: one call-only registration is not call-only");
    if (AddrUsesAllCallOnly(oneStrict, 3) != 0) Fail("S2: one strict registration among call-only ones did not make the row strict");
    if (AddrUsesAllCallOnly(lastStrict, 2) != 0) Fail("S2: a strict last registration did not make the row strict");
    if (AddrUsesAllCallOnly(odd, 2) != 0) Fail("S2: an unknown use value was taken as call-only");
    if (AddrUsesAllCallOnly(allCall, 0) != 0 || AddrUsesAllCallOnly(0, 3) != 0) Fail("S2: no registrations at all was taken as call-only");
}

void t_addr_signatures_resolve_every_kind_and_refuse_anything_less()
{
    const std::vector<unsigned char> img = sigt::Image();
    coopaddr::AddrEntry e[16];
    coopsigtab::SigOutcome oc;
    const int n = sigt::Resolve(img, sigt::kGood, e, &oc);
    if (n != 5) { Fail("the good file resolved " + I(n) + " rows, not 5" + (oc.fails.empty() ? std::string("") : ": " + oc.fails[0])); return; }
    const coopaddr::AddrEntry* fn = coopaddr::AddrFind(e, n, "Fn");
    const coopaddr::AddrEntry* gl = coopaddr::AddrFind(e, n, "Glob");
    const coopaddr::AddrEntry* rt = coopaddr::AddrFind(e, n, "Ret");
    const coopaddr::AddrEntry* ko = coopaddr::AddrFind(e, n, "Konst");
    const coopaddr::AddrEntry* vt = coopaddr::AddrFind(e, n, "Vt");
    if (!fn || !gl || !rt || !ko || !vt) { Fail("a resolved row is missing by name"); return; }
    if (fn->kind != coopaddr::kAddrCode || fn->rva != 0x1100 || fn->verifyAt != 0x1100 || fn->nbytes != 16
        || fn->bytes[0] != 0x48 || fn->bytes[5] != 0x57)
        Fail("code row: rva " + I((int)fn->rva) + " verifyAt " + I((int)fn->verifyAt) + " nbytes " + I(fn->nbytes));
    if (gl->kind != coopaddr::kAddrVar || gl->rva != 0x3010 || gl->nbytes != 0)
        Fail("var row: rva " + I((int)gl->rva) + ", expected 0x3010 (the RIP-relative target)");
    if (rt->kind != coopaddr::kAddrRet || rt->rva != 0x1205 || rt->verifyAt != 0x1200 || rt->nbytes != 5 || rt->bytes[0] != 0xE8 || rt->bytes[4] != 0xFF)
        Fail("ret row: rva " + I((int)rt->rva) + " verifyAt " + I((int)rt->verifyAt));
    if (ko->kind != coopaddr::kAddrRo || ko->rva != 0x2040 || ko->nbytes != 8 || ko->bytes[3] != 0x3F || ko->bytes[7] != 0x40)
        Fail("ro row: rva " + I((int)ko->rva) + " nbytes " + I(ko->nbytes));
    if (vt->kind != coopaddr::kAddrPtr || vt->rva != 0x2080 || vt->verifyAt != 0x2088 || vt->ptrRva != 0x1400)
        Fail("ptr row: rva " + I((int)vt->rva) + " slot " + I((int)vt->verifyAt) + " ptrRva " + I((int)vt->ptrRva));

    /* THE CODE BLOCK TWICE: two matches is a refusal, and it names the row */
    {
        std::vector<unsigned char> dup = img;
        size_t i;
        for (i = 0; i < 19; ++i) dup[sigt::T(0x1700) + i] = img[sigt::T(0x1100) + i];
        coopsigtab::SigOutcome o2;
        if (sigt::Resolve(dup, sigt::kGood, e, &o2) != 0 || !sigt::Mentions(o2, "Fn: the pattern occurs 0x2 times"))
            Fail("a pattern found twice was not refused by name");
    }
    /* A row the generator could not make a signature for refuses the whole set */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::kGood, "!nosigcount 0\n", "!nosigcount 1\n!nosig Lost\n");
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "Lost:")) Fail("a !nosig row did not refuse the set");
    }
    /* A pattern that occurs nowhere */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::kGood, "85C07405B801000000", "85C07406B801000000");
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "Ret: the pattern occurs nowhere")) Fail("a missing pattern was not refused");
    }
    /* A writable global that lands in read-only data */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::kGood, "13 4 0 0 - .data", "13 4 -4096 0 - .rdata");
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "Glob: a writable global")) Fail("a var in .rdata was not refused");
        coopsigtab::SigOutcome o3;
        const std::string t3 = sigt::Swap(sigt::kGood, "13 4 0 0 - .data", "13 4 -4096 0 - .data");
        if (sigt::Resolve(img, t3, e, &o3) != 0 || !sigt::Mentions(o3, "Glob: resolved into section '.rdata'"))
            Fail("a var that resolved outside the section its row names was not refused");
    }
    /* The bytes before a return address that are not the expected call */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::kGood, "5 5 0 0 E8????????", "5 5 0 0 E9????????");
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "Ret: the bytes before")) Fail("a wrong call shape was not refused");
    }
    /* A constant that differs, and a vtable slot that leads to other code */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::Swap(sigt::kGood, "0000803F00000040", "0000803F00000041"), "40534883EC20", "40554883EC20");
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "Konst:") || !sigt::Mentions(o2, "Vt:"))
            Fail("a changed constant or a wrong vtable target was not refused");
    }
    /* A damaged line, a wrong !count, and an anchor over wildcards are refusals, never guesses */
    {
        coopsigtab::SigOutcome o2;
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "Fn code 0 ", "Fn code 13 "), e, &o2) != 0) Fail("an anchor over wildcards was accepted");
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "!count 5", "!count 6"), e, &o2) != 0) Fail("a wrong !count was accepted");
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "Fn code 0 48", "Fn code 0 4G"), e, &o2) != 0) Fail("a damaged pattern was accepted");
    }
    /* gog1 fold - THE CROSS-CHECKS. (a) ORDER: a deliberately SWAPPED PAIR - Fn's and a second code row's patterns
       exchanged, so each finds the other's place, every row still found exactly once and passing its own check - must
       be refused; so must two ranks exchanged in the ro+ptr class. */
    {
        coopsigtab::SigOutcome o2;
        const std::string plus = sigt::Swap(sigt::kGood, "!count 5", "!count 6") + "Tg code 0 40534883EC20C3 0 0 7 0 - .text 2 -\n";
        if (sigt::Resolve(img, plus, e, &o2) != 6) Fail("the good file plus a third code row did not resolve: " + (o2.fails.empty() ? std::string("") : o2.fails[0]));
        std::string sw = sigt::Swap(plus, "Fn code 0 48895C2408574883EC20488B05????????90C3 0 0 16 0", "Fn code 0 40534883EC20C3 0 0 7 0");
        sw = sigt::Swap(sw, "Tg code 0 40534883EC20C3 0 0 7 0", "Tg code 0 48895C2408574883EC20488B05????????90C3 0 0 16 0");
        coopsigtab::SigOutcome o3;
        if (sigt::Resolve(img, sw, e, &o3) != 0 || !sigt::Mentions(o3, "ORDER (code+ret): Fn") || o3.resolved != 6)
            Fail("a swapped pair of code rows (each found once, each passing its own check) was not refused by ORDER");
        if (!sigt::Mentions(o3, "CALL TARGET: Ret")) Fail("the swapped pair did not also fail Ret's call-target check");
        coopsigtab::SigOutcome o4;
        const std::string rp = sigt::Swap(sigt::Swap(sigt::Swap(sigt::kGood, "0000803F00000040 .rdata 0", "0000803F00000040 .rdata X"),
                                                     "40534883EC20 .rdata 1", "40534883EC20 .rdata 0"), ".rdata X", ".rdata 1");
        if (sigt::Resolve(img, rp, e, &o4) != 0 || !sigt::Mentions(o4, "ORDER (ro+ptr): Vt")) Fail("two exchanged ro+ptr ranks were not refused");
    }
    /* (b) DISTINCT: a second row that finds the same function */
    {
        coopsigtab::SigOutcome o2;
        const std::string t = sigt::Swap(sigt::kGood, "!count 5", "!count 6") + "Fn2 code 0 574883EC20488B05 -5 0 16 0 - .text 5 -\n";
        if (sigt::Resolve(img, t, e, &o2) != 0 || !sigt::Mentions(o2, "DISTINCT: Fn and Fn2")) Fail("two rows at one address were not refused");
    }
    /* (c) GLOBALS TWICE: a second reference naming another address, one that is the first reference, and none at all */
    {
        coopsigtab::SigOutcome o2, o3, o4;
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "C3CCCCCC:2:4:0", "C3CCCCCC:2:4:8"), e, &o2) != 0
            || !sigt::Mentions(o2, "GLOBAL TWICE: Glob: its second reference names 0x3018"))
            Fail("a global whose two references disagree was not refused");
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "g:6:8B0D????????C3CCCCCC:2:4:0", "g:0:48895C2408574883EC20488B05????????90C3:13:4:0"), e, &o3) != 0
            || !sigt::Mentions(o3, "GLOBAL TWICE: Glob: its second reference is the first one"))
            Fail("a global 'resolved twice' through the same instruction was not refused");
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, " g:6:8B0D????????C3CCCCCC:2:4:0", " -"), e, &o4) != 0)
            Fail("a var row without its second pattern was accepted");
    }
    /* (d) CALL TARGETS: through a jmp stub to the row is fine; a call that lands elsewhere is refused */
    {
        std::vector<unsigned char> st = img;
        st[sigt::T(0x1700)] = 0xE9; sigt::Put32(st, sigt::T(0x1701), 0x1100 - 0x1705);
        sigt::Put32(st, sigt::T(0x1201), 0x1700 - 0x1205);
        coopsigtab::SigOutcome o2;
        if (sigt::Resolve(st, sigt::kGood, e, &o2) != 5) Fail("a call through a jmp stub to its row was refused: " + (o2.fails.empty() ? std::string("") : o2.fails[0]));
        sigt::Put32(st, sigt::T(0x1201), 0x1400 - 0x1205);   /* the stub at 0x1400 jumps to 0x1500, which is no row */
        coopsigtab::SigOutcome o3;
        if (sigt::Resolve(st, sigt::kGood, e, &o3) != 0 || !sigt::Mentions(o3, "CALL TARGET: Ret: the call lands on 0x1500"))
            Fail("a call that lands off its row was not refused");
    }
    /* gog1 fold 2 - A RANK TIE: a second row with a rank already used in its class is an unreadable line, so the set is
       refused (the good file already holds rank 0 in three classes, so a tie ACROSS classes is fine) */
    {
        coopsigtab::SigOutcome o2;
        const std::string tie = sigt::Swap(sigt::kGood, "40534883EC20 .rdata 1", "40534883EC20 .rdata 0");
        if (sigt::Resolve(img, tie, e, &o2) != 0 || !sigt::Mentions(o2, "unreadable line(s), the first: 'Vt ptr"))
            Fail("a rank repeated within its class (Konst and Vt both ro+ptr rank 0) was not refused");
    }
    /* gog1 fold 2 - AT MOST FOUR jmp STUBS: a chain of four from the call reaches the row; a chain of five is refused */
    {
        std::vector<unsigned char> st = img;
        unsigned int k;
        for (k = 0; k < 5; ++k)   /* 0x1700 -> 0x1710 -> 0x1720 -> 0x1730 -> 0x1740 -> Fn at 0x1100 */
        {
            st[sigt::T(0x1700 + 0x10 * k)] = 0xE9;
            sigt::Put32(st, sigt::T(0x1701 + 0x10 * k), (k < 4 ? 0x1710 + 0x10 * k : 0x1100u) - (0x1705 + 0x10 * k));
        }
        sigt::Put32(st, sigt::T(0x1201), 0x1710 - 0x1205);
        coopsigtab::SigOutcome o2;
        if (sigt::Resolve(st, sigt::kGood, e, &o2) != 5) Fail("a call through four jmp stubs to its row was refused: " + (o2.fails.empty() ? std::string("") : o2.fails[0]));
        sigt::Put32(st, sigt::T(0x1201), 0x1700 - 0x1205);
        coopsigtab::SigOutcome o3;
        if (sigt::Resolve(st, sigt::kGood, e, &o3) != 0 || !sigt::Mentions(o3, "CALL TARGET: Ret: the call lands on 0x1740"))
            Fail("a call through FIVE jmp stubs was not refused");
    }
    /* Only !sigversion 2 carries the cross-checks: a version-1 file is refused, whatever its rows say */
    {
        coopsigtab::SigOutcome o2;
        if (sigt::Resolve(img, sigt::Swap(sigt::kGood, "!sigversion 2", "!sigversion 1"), e, &o2) != 0 || !sigt::Mentions(o2, "!sigversion"))
            Fail("a !sigversion 1 file was not refused");
    }
    /* Not a PE32+ at all, and no bytes past a section's raw data */
    {
        std::vector<unsigned char> bad = img;
        bad[0] = 'X';
        coopsigtab::PeView pe;
        if (coopsigtab::SigParsePe(&bad[0], bad.size(), &pe) != 0) Fail("a buffer without MZ was read as a PE");
        coopsigtab::PeView ok;
        coopsigtab::SigParsePe(&img[0], img.size(), &ok);
        if (coopsigtab::PeAt(ok, 0x3300, 4) != 0) Fail("PeAt returned bytes past a section's raw data");
    }
}

void t_addr_fingerprint_is_the_four_numbers_and_a_bad_header_writes_nothing()
{
    unsigned char head[512];
    unsigned int tds = 0xDEAD, soi = 0xBEEF;

    /* 1. THE COMPOSITION, against the four numbers actually measured from the shipped executable on
          2026-09-05: TimeDateStamp 0x65D604D7, SizeOfImage 0x0232C000, CRC-32 of the first 64 KB
          0xB914BEE3, file length 36,713,984 = 0x02303600. This string becomes a FILE NAME. */
    {
        const std::string fp = coopaddr::AddrFingerprint(0x65D604D7u, 0x0232C000u, 0xB914BEE3u, 0x02303600u);
        if (fp != "65D604D7-0232C000-B914BEE3-02303600")
            Fail("fingerprint of the shipped exe's four numbers is '" + fp
                 + "', expected '65D604D7-0232C000-B914BEE3-02303600'");
    }
    {
        const std::string fp = coopaddr::AddrFingerprint(0, 0, 0, 0);
        if (fp != "00000000-00000000-00000000-00000000")
            Fail("fingerprint of four zeroes is '" + fp + "', expected four all-zero groups");
    }
    /* The two Kenshi executables on this machine - the RE_Kenshi-patched 1.0.65 that runs and the
       vanilla 1.0.68 kept beside it (01-environment.md) - must not share a fingerprint. */
    {
        const std::string a1 = coopaddr::AddrFingerprint(0x65D604D7u, 0x0232C000u, 0xB914BEE3u, 0x02303600u);
        const std::string a2 = coopaddr::AddrFingerprint(0x6602D59Du, 0x0232D000u, 0xBBA641C8u, 0x02304800u);
        if (a1 == a2) Fail("the 1.0.65 and 1.0.68 executables produced the SAME fingerprint " + a1);
    }

    /* 2. THE HEADER READ. A good PE64 answers; every rejection leaves the outputs untouched, so a
          caller can never fingerprint a buffer that was not understood. */
    {
        MakePeHead(head, sizeof(head), 0x100, 0x020B, 0x11223344u, 0x00A0B000u);
        tds = 0xDEAD; soi = 0xBEEF;
        if (coopaddr::AddrReadPeHeader(head, sizeof(head), &tds, &soi) != 1)
            Fail("a well-formed PE64 header was refused");
        else if (tds != 0x11223344u || soi != 0x00A0B000u)
            Fail("PE64 header read tds=" + I((int)tds) + " soi=" + I((int)soi)
                 + ", expected 0x11223344 / 0x00A0B000");
    }
    {
        MakePeHead(head, sizeof(head), 0x100, 0x010B, 0x11223344u, 0x00A0B000u);   /* PE32, not PE32+ */
        tds = 0xDEAD; soi = 0xBEEF;
        if (coopaddr::AddrReadPeHeader(head, sizeof(head), &tds, &soi) != 0) Fail("a 32-bit PE was accepted");
        if (tds != 0xDEAD || soi != 0xBEEF) Fail("a refused header still wrote through its output pointers");
    }
    {
        std::memset(head, 0x41, sizeof(head));                                     /* not an executable */
        tds = 0xDEAD; soi = 0xBEEF;
        if (coopaddr::AddrReadPeHeader(head, sizeof(head), &tds, &soi) != 0)
            Fail("a buffer of 'A's was accepted as a PE");
        if (tds != 0xDEAD || soi != 0xBEEF) Fail("a refused buffer still wrote through its output pointers");
    }
    {
        MakePeHead(head, sizeof(head), 0x100, 0x020B, 0x11223344u, 0x00A0B000u);   /* truncated read */
        tds = 0xDEAD; soi = 0xBEEF;
        if (coopaddr::AddrReadPeHeader(head, 0x80, &tds, &soi) != 0) Fail("a truncated header was accepted");
        if (tds != 0xDEAD || soi != 0xBEEF) Fail("a truncated header still wrote through its output pointers");
    }
}

/* The table text both parser tests read. One entry of each kind, with real numbers out of the shipped
   Steam 1.0.65 table, then four lines that must be REFUSED AND COUNTED rather than half-read. */
const char* const kAddrTestTable =
    "# a comment, and the blank line below it\n"
    "\n"
    "!fingerprint 65D604D7-0232C000-B914BEE3-02303600\n"
    "!version Steam_1_0_65\n"
    "!count 5\n"
    "IsEnemy          code 0079BDB0 0079BDB0 48897424184889 7C242041544883EC20\n"
    "AllyRet1         ret  00608ED3 00608ECD FF90F0030000\n"
    "HourRealSeconds  ro   016FC454 016FC454 8C2EDA42\n"
    "ActivePlatoonVt  ptr  016E1CD8 016E1CD8 0001DA20\n"
    "QuitFlag         var  02132408 02132408 -\n"
    "Bad_Kind         wat  00123456 00123456 9090\n"
    "Bad!Name         code 00123456 00123456 9090\n"
    "OddNybbles       code 00123456 00123456 909\n"
    "NotHex           code 0012345Z 00123456 9090\n";

void t_addr_table_parser_reads_every_kind_and_names_what_it_refuses()
{
    coopaddr::AddrEntry e[16];
    coopaddr::AddrParseResult r;
    const int n = coopaddr::AddrParseTable(kAddrTestTable, std::strlen(kAddrTestTable), e, 16, &r);

    /* IsEnemy's bytes field carries a SPACE on purpose. Fields are whitespace-separated, so the tail is
       a sixth field and the entry keeps only the seven bytes before it: a parser that glued them back
       together would store bytes this table never said. */
    if (n != 5) Fail("entries parsed = " + I(n) + ", expected 5 (the four malformed lines must be refused)");
    if (r.badLines != 4) Fail("badLines = " + I(r.badLines) + ", expected 4");
    if (r.declaredCount != 5) Fail("!count read as " + I(r.declaredCount) + ", expected 5");
    if (r.fingerprint != "65D604D7-0232C000-B914BEE3-02303600")
        Fail("!fingerprint read as '" + r.fingerprint + "'");
    if (r.version != "Steam_1_0_65") Fail("!version read as '" + r.version + "'");
    if (r.firstBad.find("Bad_Kind") == std::string::npos)
        Fail("the first refused line was not named; firstBad='" + r.firstBad + "'");
    if (r.overflow != 0) Fail("overflow = " + I(r.overflow) + " with room for 16 entries");

    {
        const coopaddr::AddrEntry* p = coopaddr::AddrFind(e, n, "IsEnemy");
        if (p == 0) Fail("IsEnemy is not in the parsed table");
        else
        {
            if (p->kind != coopaddr::kAddrCode) Fail("IsEnemy kind = " + I(p->kind));
            if (p->rva != 0x0079BDB0u) Fail("IsEnemy rva is wrong");
            if (p->nbytes != 7)
                Fail("IsEnemy nbytes = " + I(p->nbytes) + ", expected 7 (the field stops at the space)");
            if (p->nbytes == 7 && (p->bytes[0] != 0x48 || p->bytes[6] != 0x89))
                Fail("IsEnemy bytes are wrong");
        }
    }
    {
        const coopaddr::AddrEntry* p = coopaddr::AddrFind(e, n, "AllyRet1");
        if (p == 0) Fail("AllyRet1 is not in the parsed table");
        else if (p->kind != coopaddr::kAddrRet || p->verifyAt != 0x00608ECDu || p->nbytes != 6)
            Fail("AllyRet1 parsed as kind " + I(p->kind) + " with the wrong verifyAt or nbytes");
    }
    {
        const coopaddr::AddrEntry* p = coopaddr::AddrFind(e, n, "ActivePlatoonVt");
        if (p == 0) Fail("ActivePlatoonVt is not in the parsed table");
        else if (p->kind != coopaddr::kAddrPtr || p->ptrRva != 0x0001DA20u || p->nbytes != 0)
            Fail("ActivePlatoonVt: a ptr entry must carry ptrRva and NO bytes");
    }
    {
        const coopaddr::AddrEntry* p = coopaddr::AddrFind(e, n, "QuitFlag");
        if (p == 0) Fail("QuitFlag is not in the parsed table");
        else if (p->kind != coopaddr::kAddrVar || p->nbytes != 0)
            Fail("QuitFlag: a var entry must carry no bytes at all");
    }
    if (coopaddr::AddrFind(e, n, "OddNybbles") != 0)
        Fail("a line with an odd number of hex digits was stored anyway");
    if (coopaddr::AddrFind(e, n, "NotHex") != 0)
        Fail("a line with a non-hex rva was stored anyway");

    {
        coopaddr::AddrParseResult r2;
        if (coopaddr::AddrParseTable("", 0, e, 16, &r2) != 0) Fail("an empty buffer produced entries");
        if (coopaddr::AddrParseTable(kAddrTestTable, std::strlen(kAddrTestTable), e, 2, &r2) != 2)
            Fail("a two-entry ceiling did not stop at two");
        if (r2.overflow != 3) Fail("overflow = " + I(r2.overflow) + ", expected 3 entries dropped");
    }
}

void t_addr_a_corrupted_table_refuses_and_never_passes_by_default()
{
    coopaddr::AddrEntry e[16];
    coopaddr::AddrParseResult r;
    const int n = coopaddr::AddrParseTable(kAddrTestTable, std::strlen(kAddrTestTable), e, 16, &r);
    const coopaddr::AddrEntry* p = coopaddr::AddrFind(e, n, "IsEnemy");
    /* The seven bytes the table names for IsEnemy, as they stand in the shipped executable. */
    const unsigned char good[7] = { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89 };
    unsigned char bad[7];
    unsigned char hooked[7];

    if (p == 0) { Fail("IsEnemy is not in the parsed table - the rest of this test cannot run"); return; }

    if (coopaddr::AddrBytesMatch(*p, good) != 1)
        Fail("the bytes the table itself names were reported as a MISMATCH");

    /* ONE byte different is a refusal, and that is the whole gate: a build whose bytes are not these
       bytes never gets hooked. */
    std::memcpy(bad, good, 7); bad[6] = 0x88;
    if (coopaddr::AddrBytesMatch(*p, bad) != 0) Fail("a one-byte difference at the END was accepted");
    std::memcpy(bad, good, 7); bad[0] = 0x49;
    if (coopaddr::AddrBytesMatch(*p, bad) != 0) Fail("a one-byte difference at the START was accepted");

    /* Somebody else's hook - a jump written over the entry - is a refusal too. The plugin reports that
       shape separately (addrLooksHooked) because its repair is a different one. */
    std::memcpy(hooked, good, 7); hooked[0] = 0xE9;
    if (coopaddr::AddrBytesMatch(*p, hooked) != 0) Fail("an E9 jump over the first byte was accepted");

    /* A var entry has nothing to compare and must NEVER answer 'matched': a pass by default there would
       be the plugin claiming it verified a writable global that it cannot verify at all. */
    {
        const coopaddr::AddrEntry* q = coopaddr::AddrFind(e, n, "QuitFlag");
        if (q == 0) Fail("QuitFlag is not in the parsed table");
        else if (coopaddr::AddrBytesMatch(*q, good) != 0)
            Fail("a var entry, which carries no bytes, reported a MATCH");
    }
    if (coopaddr::AddrBytesMatch(*p, 0) != 0) Fail("a null buffer reported a MATCH");
}

/* ============================ P8n - THE DOOR ROAD'S DECISIONS ============================
 * Every one of these fails on the logic that shipped, and the comment on each says HOW, because a
 * test that would also pass before the fix says nothing about the fix (design-offline-tests 7.5).
 * The pre-patch applier was, in full: "if the live state differs from the holder's, write the
 * holder's" - no terminal-state test, no holder question, no notion of a correction being undone,
 * and a give-up that measured its own read-back rather than persistence.
 */
const char* DoorActName(int a)
{
    switch (a)
    {
        case coopdoor::kDoorActNothing:      return "Nothing";
        case coopdoor::kDoorActWaitMidSwing: return "WaitMidSwing";
        case coopdoor::kDoorActApply:        return "Apply";
        case coopdoor::kDoorActReport:       return "Report";
        case coopdoor::kDoorActAdoptLocal:   return "AdoptLocal";
        case coopdoor::kDoorActGaveUp:       return "GaveUp";
        default:                             return "??";
    }
}
coopdoor::DoorDecideIn DoorIn(int live, int holder, int localWrite, int forcedOpen, int weHold)
{
    coopdoor::DoorDecideIn in;
    in.liveState = live; in.holderState = holder; in.localWrite = localWrite;
    in.forcedOpen = forcedOpen; in.weHold = weHold; in.gaveUp = 0; in.gaveUpChurn = 0;
    return in;
}
void DoorExpect(const char* tag, const coopdoor::DoorDecideIn& in, int want)
{
    const int got = coopdoor::DoorDecide(in);
    if (got != want)
        Fail(std::string(tag) + ": live=" + I(in.liveState) + " holder=" + I(in.holderState)
             + " localWrite=" + I(in.localWrite) + " forcedOpen=" + I(in.forcedOpen)
             + " weHold=" + I(in.weHold) + " gaveUp=" + I(in.gaveUp) + "," + I(in.gaveUpChurn)
             + " -> " + DoorActName(got) + ", wanted " + DoorActName(want));
}

/* T-160: a holder's word about a settled door is PLAYED (openDoor / closeDoor); everything nobody watched
 * move is SNAPPED.  [1] is the assertion the snap-only applier fails: it had no play arm at all. */
void t_door_a_holder_word_plays_the_swing_and_the_rest_snap()
{
    using namespace coopdoor;
    if (DoorApplyHow(kDoorWhyHolderWord, kDoorClosed, kDoorOpen, 1) != kDoorApplyPlay)
        Fail("[1] a holder's OPEN for a settled CLOSED door is not played");
    if (DoorApplyHow(kDoorWhyHolderWord, kDoorOpen, kDoorClosed, 1) != kDoorApplyPlay)
        Fail("[2] a holder's CLOSED for a settled OPEN door is not played");
    if (DoorApplyHow(kDoorWhyForcedOpen, kDoorOpen, kDoorClosed, 1) != kDoorApplySnap)
        Fail("[3] a gate physics setup forced open is not snapped back");
    if (DoorApplyHow(kDoorWhyArrival, kDoorOpen, kDoorClosed, 1) != kDoorApplySnap)
        Fail("[4] a first word about a door is not snapped");
    if (DoorApplyHow(kDoorWhyHolderWord, kDoorClosed, kDoorOpen, 0) != kDoorApplySnap)
        Fail("[5] a swing is played although the engine call failed its prologue check");
    if (DoorApplyHow(kDoorWhyHolderWord, kDoorOpening, kDoorOpen, 1) != kDoorApplySnap)
        Fail("[6] a mid-swing door is given a swing");
    if (DoorPlayStartedState(kDoorOpen) != kDoorOpening || DoorPlayStartedState(kDoorClosed) != kDoorClosing)
        Fail("[7] the started state of a swing is wrong");
}
/* T-160: the frames of a swing this game played are its own, so they are never reported to the holder
 * (the echo); any other change ends the swing and is the world's.  [1] fails on the snap-only applier's
 * funnel, which booked the OPENING frame of any engine call as a local writer. */
void t_door_a_played_swing_is_ours_until_it_lands()
{
    using namespace coopdoor;
    if (DoorPlayNote(kDoorOpen, kDoorOpening) != kDoorPlayOwned)   Fail("[1] the OPENING frame of a played open is not ours");
    if (DoorPlayNote(kDoorOpen, kDoorOpen) != kDoorPlayLanded)     Fail("[2] the landing of a played open is not ours");
    if (DoorPlayNote(kDoorClosed, kDoorClosing) != kDoorPlayOwned) Fail("[3] the CLOSING frame of a played close is not ours");
    if (DoorPlayNote(kDoorClosed, kDoorClosed) != kDoorPlayLanded) Fail("[4] the landing of a played close is not ours");
    if (DoorPlayNote(kDoorOpen, kDoorClosing) != kDoorPlayBroken)  Fail("[5] a close during a played open is taken as ours");
    if (DoorPlayNote(kDoorClosed, kDoorOpen) != kDoorPlayBroken)   Fail("[6] a snap open during a played close is taken as ours");
    if (DoorPlayNote(-1, kDoorOpen) != kDoorPlayNone)              Fail("[7] a door with no swing is classified as one");
    if (DoorPlayOverdue(kDoorOpen, 1000ul, 1000ul + kDoorPlayLandMs - 1) != 0) Fail("[8] a swing is overdue early");
    if (DoorPlayOverdue(kDoorOpen, 1000ul, 1000ul + kDoorPlayLandMs) != 1)     Fail("[9] a swing is never overdue");
    if (DoorPlayOverdue(kDoorClosed, 0xFFFFF000ul, 0x00000100ul) != 0)          Fail("[10] the tick-count wrap makes a swing overdue");
    if (DoorPlayOverdue(-1, 0ul, 100000ul) != 0)                                 Fail("[11] no swing is overdue");
}
/* T-160: the lock word carries the lock AND the owner's wish to lock, and the receiving game moves toward
 * the holder's word one engine call at a time.  [1] fails on the old wire, which collapsed the byte to
 * locked-or-not and so could never say "lock it when it shuts". */
void t_door_the_lock_word_is_applied_by_the_engine_lock_calls()
{
    using namespace coopdoor;
    if (DoorLockWord(0, 1) != 2 || DoorLockWord(1, 1) != 3 || DoorLockWord(5, 0) != 1 || DoorLockWord(0, 0) != 0)
        Fail("[1] the lock word does not carry both bits");
    if (DoorLockWordValid(4) != 0 || DoorLockWordValid(-1) != 0 || DoorLockWordValid(3) != 1)
        Fail("[2] a lock byte outside 0..3 is accepted");
    /* the holder's player locked a shut door (lockButton: wants 1, locked 1); this game's is unlocked: lockDoor
       makes both, silently - lockButton's sound is heard everywhere */
    if (DoorLockStep(kDoorClosed, 0, 3) != kDoorLockStepLock) Fail("[3] a lock press on a shut door is not mirrored by lockDoor");
    if (DoorLockStep(kDoorClosed, 2, 3) != kDoorLockStepLock) Fail("[4] a wanted lock on a shut door is not locked");
    /* the holder's player unlocked (wants 0, locked 0): the wish is cleared as lockButton clears it, then unlockDoor */
    if (DoorLockStep(kDoorClosed, 3, 0) != kDoorLockStepClearWants) Fail("[5] an unlock press does not clear the wish");
    if (DoorLockStep(kDoorClosed, 1, 0) != kDoorLockStepUnlock) Fail("[6] a leftover lock is not unlocked");
    /* the holder's player opened a locked door: openButton clears the lock and keeps the wish */
    if (DoorLockStep(kDoorOpen, 3, 2) != kDoorLockStepUnlock) Fail("[7] an opened door keeps its lock");
    /* word 1 - locked without the wish, what the NPC lock action 0x337630 writes - and a lock on an open door:
       copied with the direct write that action makes */
    if (DoorLockStep(kDoorClosed, 0, 1) != kDoorLockStepSetLocked) Fail("[8] an NPC's lock (word 1) is not copied");
    if (DoorLockStep(kDoorOpen, 2, 3) != kDoorLockStepSetLocked)   Fail("[9] a lock on an open door is not copied");
    if (DoorLockStep(kDoorClosing, 0, 3) != kDoorLockStepWait)        Fail("[10] the lock is touched mid-swing");
    if (DoorLockStep(kDoorClosed, 3, 3) != kDoorLockStepNone)         Fail("[11] an agreeing lock is touched");
    if (DoorLockStep(kDoorClosed, 3, -1) != kDoorLockStepNone)        Fail("[12] a lock nobody has spoken about is touched");
    /* the last-actor rule, for the lock */
    if (DoorLockDecide(3, 0, 0, 0) != kDoorActLock)       Fail("[13] a holder's lock word is not applied");
    if (DoorLockDecide(3, 0, 1, 0) != kDoorActReport)     Fail("[14] a non-holder reverts its own player's lock");
    if (DoorLockDecide(3, 0, 1, 1) != kDoorActAdoptLocal) Fail("[15] the holder reverts its own player's lock");
    if (DoorLockDecide(3, 3, 1, 0) != kDoorActNothing)    Fail("[16] agreeing locks are not left alone");
    if (DoorLockDecide(2, -1, 0, 0) != kDoorActNothing)   Fail("[17] a lock nobody has spoken about is decided");
    /* the invalid-live-word exits: a word read outside 0..3 decides nothing and writes nothing */
    if (DoorLockDecide(4, 0, 0, 0) != kDoorActNothing || DoorLockDecide(-1, 3, 1, 1) != kDoorActNothing)
        Fail("[18] an invalid live lock word is decided");
    if (DoorLockStep(kDoorClosed, 4, 3) != kDoorLockStepNone || DoorLockStep(kDoorOpen, -1, 0) != kDoorLockStepNone)
        Fail("[19] an invalid live lock word is written toward");
    if (DoorLockStep(kDoorClosed, 0, 7) != kDoorLockStepNone) Fail("[20] an invalid holder lock word is written toward");
}
/* T-160: NO LOCK WORD IS UNREACHABLE.  Every live word moves to every holder word, on a shut door and on an open
 * one, inside one visit, when each write does what the engine's code does (lockDoor: wish 1, and the lock if
 * shut; unlockDoor: lock 0; the direct writes: wish 0, lock 1).  [1] fails on the lockButton applier, which
 * left word 1 (an NPC's lock) and a lock on an open door unreachable. */
void t_door_every_lock_word_is_reachable()
{
    using namespace coopdoor;
    const int states[2] = { kDoorClosed, kDoorOpen };
    for (int si = 0; si < 2; ++si)
        for (int from = 0; from <= 3; ++from)
            for (int to = 0; to <= 3; ++to)
            {
                int w = from, n = 0;
                for (; n < kDoorLockStepsPerVisit; ++n)
                {
                    const int step = DoorLockStep(states[si], w, to);
                    int l = DoorLockWordLocked(w), x = DoorLockWordWants(w);
                    if (step == kDoorLockStepNone) break;
                    if (step == kDoorLockStepLock) { x = 1; if (states[si] == kDoorClosed) l = 1; }
                    else if (step == kDoorLockStepUnlock) l = 0;
                    else if (step == kDoorLockStepClearWants) x = 0;
                    else if (step == kDoorLockStepSetLocked) l = 1;
                    else { Fail("[2] a terminal door is told to wait"); return; }
                    if (DoorLockWord(l, x) == w) { Fail("[3] a lock step does not move the word"); return; }
                    w = DoorLockWord(l, x);
                }
                if (w != to) { Fail("[1] a holder lock word is not reached inside one visit"); return; }
            }
}
/* T-160 fold: A LOCK NO DETOUR SAW IS THIS GAME'S OWN.  The NPC lock action 0x337630 closes (nothing, on a shut
 * door) and writes the lock byte itself, so no hook sees it.  [2] and [3] fail on the first T-160 applier,
 * which took the live word against the holder's with no local flag and UNLOCKED the NPC's door - on the
 * holder too. */
void t_door_a_lock_no_detour_saw_is_a_local_write()
{
    using namespace coopdoor;
    /* this game last saw word 0; the holder's word is 0; the NPC wrote locked (word 1) */
    if (DoorLockUnseenWrite(1, 0) != 1) Fail("[1] an NPC's lock on a shut door is not this game's own");
    if (DoorLockDecide(1, 0, DoorLockUnseenWrite(1, 0), 0) != kDoorActReport)     Fail("[2] the receiver undoes its own NPC's lock");
    if (DoorLockDecide(1, 0, DoorLockUnseenWrite(1, 0), 1) != kDoorActAdoptLocal) Fail("[3] the holder undoes its own NPC's lock");
    /* this game's own apply: the row records the word the write left, so the next look sees nothing new */
    if (DoorLockUnseenWrite(3, 3) != 0) Fail("[4] this game's own lock apply is taken as a local write");
    if (DoorLockDecide(0, 3, DoorLockUnseenWrite(0, 0), 0) != kDoorActLock) Fail("[5] a holder's word is not applied when nothing local happened");
    /* invalid words say nothing */
    if (DoorLockUnseenWrite(4, 0) != 0 || DoorLockUnseenWrite(1, -1) != 0 || DoorLockUnseenWrite(-1, 1) != 0)
        Fail("[6] an invalid lock word is taken as a local write");
    /* a played swing that lands with a lock of this game's own inside it is reported at the landing */
    if (DoorPlayLandReport(1, 0, 0) != 1) Fail("[7] a lock pressed mid-swing is not reported at the landing");
    if (DoorPlayLandReport(0, 0, 1) != 1) Fail("[8] an NPC's lock written mid-swing is not reported at the landing");
    if (DoorPlayLandReport(0, 3, 3) != 0) Fail("[9] a plain landing is reported");
}
/* T-160: THE LIVE ROWS ARE SWEPT TOO.  The holder's own NPC locking a shut door no word has named, and a lockDoor
 * the engine refuses on a closed door that has not settled.  [1] fails where the unseen-lock test runs only on
 * doors the other game has spoken about (the holder's own NPC lock never goes out); [5] fails where a refused
 * lockDoor is retried until the row gives up. */
void t_door_a_live_row_lock_no_detour_saw_is_queued()
{
    using namespace coopdoor;
    if (DoorLockSweepQueues(kDoorClosed, kDoorClosed, 1, 0) != 1) Fail("[1] an NPC's lock on a shut door is not queued");
    if (DoorLockSweepQueues(kDoorClosed, kDoorClosed, 0, 0) != 0) Fail("[2] a no-op close with no lock is queued");
    if (DoorLockSweepQueues(kDoorClosing, kDoorClosing, 1, 0) != 0 || DoorLockSweepQueues(kDoorClosed, kDoorOpen, 1, 0) != 0)
        Fail("[3] a door mid-swing, or not in the state the row recorded, is queued from the sweep");
    if (DoorLockSweepQueues(kDoorClosed, kDoorClosed, 1, -1) != 0) Fail("[4] a row with no recorded word is queued");
    if (DoorLockStepAfterRefusal(kDoorLockStepLock, kDoorClosed, 1) != kDoorLockStepSetLocked)
        Fail("[5] a lockDoor refused on a closed door is not followed by the direct lock write");
    if (DoorLockStepAfterRefusal(kDoorLockStepLock, kDoorClosed, 0) != kDoorLockStepLock
        || DoorLockStepAfterRefusal(kDoorLockStepLock, kDoorOpen, 1) != kDoorLockStepLock
        || DoorLockStepAfterRefusal(kDoorLockStepUnlock, kDoorClosed, 1) != kDoorLockStepUnlock)
        Fail("[6] a step is replaced where lockDoor was not refused on a closed door");
}

/* T236d, exactly: game B does not hold sector 43,11; B's own NPC opened the house door (live OPEN),
 * A's published answer is CLOSED.  The shipped code applied CLOSED - 2,652 times at 12.7-16.6 Hz.
 * [1] is the assertion that fails on the pre-patch logic: it demanded Apply where the rule now
 * demands Report (send it to the holder and leave the door alone). */
void t_door_last_actor_is_reported_not_reverted()
{
    DoorExpect("[1] a non-holder never reverts an engine actor",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 1, 0, 0), coopdoor::kDoorActReport);
    DoorExpect("[2] the holder adopts its own actor instead",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 1, 0, 1), coopdoor::kDoorActAdoptLocal);
    DoorExpect("[3] an unanswerable holder question reports rather than reverts",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 1, 0, -1), coopdoor::kDoorActReport);
    /* setupPhysicalUT's forced GATE open is world CONSTRUCTION, not an actor (F633), so the holder's
       answer must still survive it - the last-actor rule must not swallow this one. */
    DoorExpect("[4] a forced open is still re-applied",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 1, 1, 0), coopdoor::kDoorActApply);
    DoorExpect("[5] with no local writer the holder's word is followed",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 0, 0, 0), coopdoor::kDoorActApply);
    /* The routing half of the same rule. */
    if (coopdoor::DoorRecvDecide(coopdoor::kDoorOriginActor, 1) != coopdoor::kDoorRecvAdopt)
        Fail("[6] the holder adopts an actor report");
    if (coopdoor::DoorRecvDecide(coopdoor::kDoorOriginActor, 0) != coopdoor::kDoorRecvIgnore)
        Fail("[7] only the holder serialises: a non-holder ignores an actor report");
    if (coopdoor::DoorRecvDecide(coopdoor::kDoorOriginHolder, 0) != coopdoor::kDoorRecvStore)
        Fail("[8] a holder publish is stored whoever receives it");
    if (coopdoor::DoorRecvDecide(coopdoor::kDoorOriginHolder, 1) != coopdoor::kDoorRecvStore)
        Fail("[9] ... including on a game that thinks it holds the door");
}

/* Every one of B's 2,652 reverts read was=2 now=0 - it was interrupting an OPENING animation.  The
 * holder's state is only ever 0 or 1, so an animating door can never equal it and was snapped every
 * tick.  [1] fails on the pre-patch logic, which compared 2 against 0 and applied. */
void t_door_only_terminal_states_are_compared()
{
    DoorExpect("[1] a door mid-OPENING is left alone",
               DoorIn(coopdoor::kDoorOpening, coopdoor::kDoorClosed, 0, 0, 0), coopdoor::kDoorActWaitMidSwing);
    DoorExpect("[2] a door mid-CLOSING is left alone",
               DoorIn(coopdoor::kDoorClosing, coopdoor::kDoorOpen, 1, 0, 0), coopdoor::kDoorActWaitMidSwing);
    DoorExpect("[3] agreement costs nothing",
               DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorOpen, 1, 1, 0), coopdoor::kDoorActNothing);
    DoorExpect("[4] a door nobody has spoken about is not touched",
               DoorIn(coopdoor::kDoorOpen, -1, 1, 0, 0), coopdoor::kDoorActNothing);
    if (coopdoor::DoorStateIsTerminal(coopdoor::kDoorClosed) == 0 ||
        coopdoor::DoorStateIsTerminal(coopdoor::kDoorOpen) == 0)
        Fail("[5] 0 and 1 are the durable states");
    if (coopdoor::DoorStateIsTerminal(coopdoor::kDoorOpening) != 0 ||
        coopdoor::DoorStateIsTerminal(coopdoor::kDoorClosing) != 0)
        Fail("[6] 2 and 3 are the animation ramp and are never durable");
    {   /* a gave-up row is not touched at all, whatever else is true of it */
        coopdoor::DoorDecideIn in = DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 0, 1, 1);
        in.gaveUp = 1;
        DoorExpect("[7] the three-corrections guard still stops everything", in, coopdoor::kDoorActGaveUp);
        in.gaveUp = 0; in.gaveUpChurn = 1;
        DoorExpect("[8] and so does the churn guard", in, coopdoor::kDoorActGaveUp);
    }
}

/* The shipped guard counted corrections that did not TAKE; all 2,652 took, so it read 0 and its
 * give-up was unreachable.  [1] and [2] are the assertions that fail on the pre-patch logic: there
 * was no undone counter at all, so three successful-but-undone corrections produced no give-up. */
void t_door_give_up_counts_corrections_that_were_undone()
{
    {
        coopdoor::DoorChurn c;
        coopdoor::DoorChurnReset(&c);
        if (coopdoor::DoorChurnNoteUndone(&c, 1000u) != 0) Fail("[1a] one undone correction is not a give-up");
        if (coopdoor::DoorChurnNoteUndone(&c, 1060u) != 0) Fail("[1b] two are not either");
        if (coopdoor::DoorChurnNoteUndone(&c, 1120u) != 1)
            Fail("[1] three undone inside the window IS the give-up, undone=" + I((int)c.undone));
        if (c.gaveUp != 1) Fail("[2] and the row is marked, undone=" + I((int)c.undone));
        if (coopdoor::DoorChurnNoteUndone(&c, 1180u) != 0) Fail("[3] the give-up is logged exactly once");
    }
    {   /* three genuine, unrelated NPC uses spread over minutes must NOT give up */
        coopdoor::DoorChurn c;
        coopdoor::DoorChurnReset(&c);
        if (coopdoor::DoorChurnNoteUndone(&c, 0u) != 0) Fail("[4a]");
        if (coopdoor::DoorChurnNoteUndone(&c, 6000u) != 0) Fail("[4b] the window reopened");
        if (coopdoor::DoorChurnNoteUndone(&c, 12000u) != 0)
            Fail("[4] three undone spread over 12 s is ordinary traffic, undone=" + I((int)c.undone));
        if (c.undone != 1) Fail("[5] each one opened a fresh window, undone=" + I((int)c.undone));
        if (c.gaveUp != 0) Fail("[6] and nothing gave up");
    }
    {   /* the boundary: exactly at the window, and one millisecond past it */
        coopdoor::DoorChurn c;
        coopdoor::DoorChurnReset(&c);
        coopdoor::DoorChurnNoteUndone(&c, 100u);
        coopdoor::DoorChurnNoteUndone(&c, 100u + coopdoor::kDoorChurnWindowMs);
        if (c.undone != 2) Fail("[7] exactly at the window is still inside it, undone=" + I((int)c.undone));
        coopdoor::DoorChurnReset(&c);
        coopdoor::DoorChurnNoteUndone(&c, 100u);
        coopdoor::DoorChurnNoteUndone(&c, 101u + coopdoor::kDoorChurnWindowMs);
        if (c.undone != 1) Fail("[8] one millisecond past it is a new window, undone=" + I((int)c.undone));
    }
    {   /* the clock wraps every 49.7 days and the window must survive it */
        coopdoor::DoorChurn c;
        coopdoor::DoorChurnReset(&c);
        coopdoor::DoorChurnNoteUndone(&c, 0xFFFFFF00u);
        coopdoor::DoorChurnNoteUndone(&c, 0x00000064u);   /* 356 ms later, across the wrap */
        if (c.undone != 2) Fail("[9] the unsigned difference is correct across the wrap, undone=" + I((int)c.undone));
    }
    {   /* and a row that has given up is left alone by the applier */
        coopdoor::DoorDecideIn in = DoorIn(coopdoor::kDoorOpen, coopdoor::kDoorClosed, 0, 0, 0);
        in.gaveUpChurn = 1;
        DoorExpect("[10] a churn give-up stops the corrections", in, coopdoor::kDoorActGaveUp);
    }
}

/* Two mis-bookings the read named.  [1] fails on the pre-patch logic, which set localWrite on EVERY
 * non-self change including a row's very first sighting - so an apply that was first contact was
 * attributed to "a local writer moved a door this game does not hold" (arrival read 20 for 27 doors).
 * [3] fails on it too: a non-holder's change was refused as notHolder and thrown away, which is the
 * publish half of the livelock. */
void t_door_drain_drop_and_re_registration_booking()
{
    if (coopdoor::DoorSightingIsLocalWrite(-1) != 0)
        Fail("[1] a re-registration (was = -1) is a first sighting, not a writer");
    if (coopdoor::DoorSightingIsLocalWrite(coopdoor::kDoorClosed) != 1 ||
        coopdoor::DoorSightingIsLocalWrite(coopdoor::kDoorOpen) != 1 ||
        coopdoor::DoorSightingIsLocalWrite(coopdoor::kDoorOpening) != 1)
        Fail("[2] a change from a known state IS a writer");
    if (coopdoor::DoorDrainDecide(0, 1) != coopdoor::kDoorSendActor)
        Fail("[3] a non-holder's own change is SENT to the holder as an actor report");
    if (coopdoor::DoorDrainDecide(1, 1) != coopdoor::kDoorSendHolder)
        Fail("[4] the holder publishes");
    if (coopdoor::DoorDrainDecide(1, 0) != coopdoor::kDoorSendHolder)
        Fail("[5] ... including at the link-up re-offer");
    if (coopdoor::DoorDrainDecide(0, 0) != coopdoor::kDoorSendRefuse)
        Fail("[6] but a non-holder's link-up re-offer is still refused - a joiner must not push its"
             " stale local states at the holder");
    if (coopdoor::DoorDrainDecide(-1, 0) != coopdoor::kDoorSendRefuse)
        Fail("[7] an unanswerable holder question refuses a re-offer");
    if (coopdoor::DoorDrainDecide(-1, 1) != coopdoor::kDoorSendActor)
        Fail("[8] ... and reports an ordinary change, which the holder is free to ignore");
}

/* ============================ P8n-b - review-p8n's RULINGS, AS ARITHMETIC ============================ */

/* review-p8n M-4 (MANAGER RULING): a row is not dead for the run.  Every assertion here fails on the
 * P8n logic, which had no re-arm at all - DoorChurnNoteUndone latched gaveUp and nothing ever cleared
 * it, so [3] could not be produced by any sequence of calls. */
void t_door_churn_re_arms_after_thirty_seconds_of_agreement()
{
    coopdoor::DoorChurn c;
    coopdoor::DoorChurnReset(&c);
    if (coopdoor::DoorChurnNoteUndone(&c, 1000u) != 0) Fail("[0a]");
    if (coopdoor::DoorChurnNoteUndone(&c, 1100u) != 0) Fail("[0b]");
    if (coopdoor::DoorChurnNoteUndone(&c, 1200u) != 1) Fail("[0] three undone inside the window gives up");
    if (c.gaveUp != 1) Fail("[0d] and the row is marked");
    /* the clock starts on the FIRST agreeing tick, not on the give-up */
    if (coopdoor::DoorChurnNoteAgreement(&c, 2000u) != 0) Fail("[1] the first agreement only starts the clock");
    if (c.agreeing != 1) Fail("[1b] and the clock is running, agreeing=" + I(c.agreeing));
    if (coopdoor::DoorChurnNoteAgreement(&c, 2000u + 29000u) != 0)
        Fail("[2] twenty-nine seconds of agreement is not yet a re-arm");
    if (c.gaveUp != 1) Fail("[2b] so the row is still given up");
    if (coopdoor::DoorChurnNoteAgreement(&c, 2000u + 30000u) != 1)
        Fail("[3] thirty seconds of continuous agreement RE-ARMS the row");
    if (c.gaveUp != 0 || c.undone != 0 || c.agreeing != 0)
        Fail("[4] and the whole churn state is cleared, gaveUp=" + I(c.gaveUp) + " undone=" + I((int)c.undone));
    if (coopdoor::DoorChurnNoteAgreement(&c, 2000u + 60000u) != 0)
        Fail("[5] a re-armed row re-arms once, not on every later tick");
    {   /* a disagreement resets the clock: 29 s, a disagreement, then 29 s more is still not 30 */
        coopdoor::DoorChurn d;
        coopdoor::DoorChurnReset(&d);
        d.gaveUp = 1;
        coopdoor::DoorChurnNoteAgreement(&d, 0u);
        if (coopdoor::DoorChurnNoteAgreement(&d, 29000u) != 0) Fail("[6a]");
        coopdoor::DoorChurnNoteDisagreement(&d);
        if (d.agreeing != 0) Fail("[6b] a disagreement stops the clock");
        coopdoor::DoorChurnNoteAgreement(&d, 30000u);
        if (coopdoor::DoorChurnNoteAgreement(&d, 30000u + 29000u) != 0)
            Fail("[6] a disagreement RESETS the clock - the thirty seconds must be continuous");
        if (coopdoor::DoorChurnNoteAgreement(&d, 30000u + 30000u) != 1)
            Fail("[7] ... and thirty seconds from the RESET does re-arm");
    }
    {   /* the clock is an unsigned difference and survives the 49.7-day GetTickCount wrap */
        coopdoor::DoorChurn e;
        coopdoor::DoorChurnReset(&e);
        e.gaveUp = 1;
        coopdoor::DoorChurnNoteAgreement(&e, 0xFFFFF000u);
        if (coopdoor::DoorChurnNoteAgreement(&e, 0x00006800u) != 1)
            Fail("[8] thirty seconds across the wrap still re-arms");
    }
    {   /* a row that never gave up has nothing to re-arm */
        coopdoor::DoorChurn f;
        coopdoor::DoorChurnReset(&f);
        if (coopdoor::DoorChurnNoteAgreement(&f, 0u) != 0) Fail("[9a]");
        if (coopdoor::DoorChurnNoteAgreement(&f, 60000u) != 0)
            Fail("[9] agreement on a row that never gave up re-arms nothing");
    }
}

/* review-p8n L-8: session.cpp read payload[at+6] and passed it straight through, and doors.cpp tests
 * it only against kDoorOriginActor - so ANY byte other than 1 took the HOLDER arm and was applied as
 * authority.  [4] is the assertion that fails on that logic: a garbage byte must be its own answer. */
void t_door_origin_byte_is_parsed_and_an_unknown_one_is_ignored()
{
    /* the decoder's own numbers: `at` is where the key ended, and the fields are state, locked, gen. */
    const unsigned long at = 9;
    if (coopdoor::DoorParseOrigin(at + 5, at, 0) != coopdoor::kDoorOriginParseShort)
        Fail("[1] a payload too short for state+locked+gen is malformed, not an origin");
    if (coopdoor::DoorParseOrigin(at + 6, at, 0) != coopdoor::kDoorOriginParseAbsent)
        Fail("[2] no trailing byte at all - every DOOR_STATE before P8n");
    if (coopdoor::DoorParseOrigin(at + 7, at, 0) != coopdoor::kDoorOriginParseHolder)
        Fail("[3] a trailing 0 is the holder's answer");
    if (coopdoor::DoorParseOrigin(at + 7, at, 1) != coopdoor::kDoorOriginParseActor)
        Fail("[4] a trailing 1 is an actor report");
    if (coopdoor::DoorParseOrigin(at + 7, at, 2) != coopdoor::kDoorOriginParseUnknown)
        Fail("[5] a trailing 2 is UNKNOWN - it must not fall through to the holder arm");
    if (coopdoor::DoorParseOrigin(at + 7, at, 255) != coopdoor::kDoorOriginParseUnknown)
        Fail("[6] ... and neither does 255");
    if (coopdoor::DoorParseOrigin(at + 12, at, 1) != coopdoor::kDoorOriginParseActor)
        Fail("[7] a LONGER message still reads its byte at at+6 - the test is a minimum");
    if (coopdoor::DoorParseOrigin(0u, 0u, 1) != coopdoor::kDoorOriginParseShort)
        Fail("[8] an empty payload is short");
}

/* review-p8n L-6: the report verdict queued nothing and did not even mark the tick dirty, so a lost
 * report was never asked about again.  Every assertion here fails on that logic - there was no retry
 * decision at all. */
void t_door_a_lost_report_retries_and_is_abandoned_at_the_bound()
{
    if (coopdoor::DoorReportRetryDecide(1000u, 1000u, 1000u) != coopdoor::kDoorReportWait)
        Fail("[1] the tick the report is made does not immediately re-send it");
    if (coopdoor::DoorReportRetryDecide(1000u + 1999u, 1000u, 1000u) != coopdoor::kDoorReportWait)
        Fail("[2] one millisecond short of the retry period still waits");
    if (coopdoor::DoorReportRetryDecide(1000u + 2000u, 1000u, 1000u) != coopdoor::kDoorReportRetry)
        Fail("[3] two seconds with no holder word RE-SENDS the report");
    if (coopdoor::DoorReportRetryDecide(1000u + 4000u, 1000u, 1000u + 2000u) != coopdoor::kDoorReportRetry)
        Fail("[4] and again two seconds after the LAST send, not after the first");
    if (coopdoor::DoorReportRetryDecide(1000u + 15000u, 1000u, 1000u + 14000u) != coopdoor::kDoorReportWait)
        Fail("[5] exactly at the bound is still inside it");
    if (coopdoor::DoorReportRetryDecide(1000u + 15001u, 1000u, 1000u + 14000u) != coopdoor::kDoorReportAbandon)
        Fail("[6] past fifteen seconds the report is ABANDONED - a door nobody holds must not report for ever");
    if (coopdoor::DoorReportRetryDecide(1000u + 20000u, 1000u, 1000u) != coopdoor::kDoorReportAbandon)
        Fail("[7] abandonment outranks a due retry");
    {   /* the wrap again: both bounds are unsigned differences */
        const unsigned long first = 0xFFFFF000u;
        if (coopdoor::DoorReportRetryDecide(0x00000100u, first, first) != coopdoor::kDoorReportRetry)
            Fail("[8] a retry due across the GetTickCount wrap is still a retry");
    }
}

/* ============================ P8n-c - review-p8n-b's RULINGS, AS ARITHMETIC ============================ */

/* PROBE P113 (reuse), T-341: run T736's four alarms were appearance copies (type 66, GameDataCopyStandalone) at the
 * address of an earlier NAMED record - a new object, not the same one destroyed twice. */
void t_p113_reuse_decide()
{
    const unsigned long long gd = 0x1000, gcs = 0x2000, other = 0x3000;
    if (p113::P113ReuseDecide(10, gd, 66, gcs, gd) != p113::kP113ReuseType) Fail("[1] T736 shape: a type-66 copy where a type-10 record died is REUSE (type)");
    if (p113::P113ReuseDecide(66, gcs, 66, gcs, gd) != p113::kP113SameObject) Fail("[2] same type, the subclass destructor re-stored its vtable: possible double");
    if (p113::P113ReuseDecide(66, gcs, 66, gd, gd) != p113::kP113SameObject) Fail("[3] same type, entered through the left-over GameData vtable: possible double");
    if (p113::P113ReuseDecide(66, gcs, 66, other, gd) != p113::kP113ReuseClass) Fail("[4] same type, a third class's vtable: REUSE (class)");
    if (p113::P113ReuseDecide(66, gcs, 66, 0, gd) != p113::kP113SameObject) Fail("[5] rec+0 not in the exe (freed block words): possible double");
    if (p113::P113ReuseDecide(66, 0, 66, other, gd) != p113::kP113SameObject) Fail("[6] first vtable unknown: no class verdict");
    if (p113::P113ReuseDecide(66, gcs, 66, other, 0) != p113::kP113ReuseClass) Fail("[7] GameData vtable not learned yet: a different exe vtable is still REUSE");
    if (p113::P113ReuseDecide(0, gd, 0, gd, 0) != p113::kP113SameObject) Fail("[8] everything equal: possible double");
}
/* src/common/livechar.h: a row whose stored handle is not the object's - the same character under a new handle (renote), another
   character (refuse), or not known yet (undecided). */
void t_t920_notstored_judge()
{
    const int kW = livechar::kFpWitnesses;   /* the template, the AnimationClass, the AI object */
    /* the template (0x1000), the AnimationClass (0x2000), the AI object (0x3000) of the registered character */
    const void* stored[3] = { (const void*)0x1000, (const void*)0x2000, (const void*)0x3000 };
    const void* same3[3]  = { (const void*)0x1000, (const void*)0x2000, (const void*)0x3000 };
    const void* noStore[3] = { (const void*)0x1000, (const void*)0, (const void*)0x3000 };
    const void* nowZero[3] = { (const void*)0x1000, (const void*)0x2000, (const void*)0 };
    const void* otherTpl[3] = { (const void*)0x1100, (const void*)0x2000, (const void*)0x3000 };
    const void* mateAnim[3] = { (const void*)0x1000, (const void*)0x2200, (const void*)0x3000 };   /* a squad-mate of one template built here */
    const void* mateAi[3]   = { (const void*)0x1000, (const void*)0x2000, (const void*)0x3300 };
    const void* mateBoth[3] = { (const void*)0x1000, (const void*)0x2200, (const void*)0x3300 };
    if (livechar::FingerprintOf(noStore, true, same3, kW) != livechar::kFpUnknown) Fail("[t920 1] a row that stored a witness as 0: unknown");
    if (livechar::FingerprintOf(stored, false, same3, kW) != livechar::kFpUnknown) Fail("[t920 2] the read now failed: unknown");
    if (livechar::FingerprintOf(stored, true, nowZero, kW) != livechar::kFpUnknown) Fail("[t920 3] a witness read now as 0: unknown");
    if (livechar::FingerprintOf(stored, true, same3, kW) != livechar::kFpSame) Fail("[t920 4] template and every owned object equal: same");
    if (livechar::FingerprintOf(stored, true, otherTpl, kW) != livechar::kFpDiffers) Fail("[t920 5] another template: differs");
    if (livechar::FingerprintOf(stored, true, mateAnim, kW) != livechar::kFpDiffers) Fail("[t920 6] the same template, another AnimationClass (a squad-mate built at the address): differs");
    if (livechar::FingerprintOf(stored, true, mateAi, kW) != livechar::kFpDiffers) Fail("[t920 6b] the same template, another AI object: differs");
    if (livechar::FingerprintOf(stored, true, mateBoth, kW) != livechar::kFpDiffers) Fail("[t920 6c] the same template, both owned objects other: differs");
    if (livechar::NotStoredJudge(false, false, false, true, false, true, livechar::FingerprintOf(stored, true, mateAnim, kW)) != livechar::kRehandleRefuse)
        Fail("[t920 6d] a same-template character live at the address, resolving and listed: refused, never renoted");
    const int same = livechar::kFpSame, differs = livechar::kFpDiffers, unknown = livechar::kFpUnknown;
    /*                          rowDead destroyed doomed newSelf newOther inList fp */
    if (livechar::NotStoredJudge(false, false,    false, true,   false,   true,  same) != livechar::kRehandleRenote) Fail("[t920 7] every witness says the same character: renote");
    if (livechar::NotStoredJudge(false, false,    false, true,   false,   true,  differs) != livechar::kRehandleRefuse) Fail("[t920 8] the fingerprint differs: refuse, whatever the engine says");
    if (livechar::NotStoredJudge(false, true,     false, true,   false,   true,  same) != livechar::kRehandleRefuse) Fail("[t920 9] the row is destroyed: refuse");
    if (livechar::NotStoredJudge(false, false,    false, false,  true,    true,  same) != livechar::kRehandleRefuse) Fail("[t920 10] the new handle names another character: refuse");
    if (livechar::NotStoredJudge(false, false,    false, true,   false,   true,  unknown) != livechar::kRehandleUndecided) Fail("[t920 11] no fingerprint stored or read: undecided, never renote");
    if (livechar::NotStoredJudge(false, false,    false, true,   false,   false, same) != livechar::kRehandleUndecided) Fail("[t920 12] not in the active list: undecided");
    if (livechar::NotStoredJudge(false, false,    false, false,  false,   true,  same) != livechar::kRehandleUndecided) Fail("[t920 13] the new handle resolves to nothing: undecided");
    if (livechar::NotStoredJudge(false, false,    true,  true,   false,   true,  same) != livechar::kRehandleUndecided) Fail("[t920 14] a destroy is waiting: undecided");
    if (livechar::NotStoredJudge(true,  false,    false, true,   false,   true,  same) != livechar::kRehandleUndecided) Fail("[t920 15] a dead row: undecided");
    if (livechar::NotStoredJudge(true,  false,    false, true,   false,   true,  differs) != livechar::kRehandleRefuse) Fail("[t920 16] a proof of another character wins over a dead row: refuse");
    if (livechar::NotStoredJudge(false, false,    false, false,  false,   false, unknown) != livechar::kRehandleUndecided) Fail("[t920 17] nothing read: undecided");
    if (livechar::RehandleName(livechar::kRehandleRenote)[0] == '?' || livechar::RehandleName(livechar::kRehandleRefuse)[0] == '?'
        || livechar::RehandleName(livechar::kRehandleUndecided)[0] == '?' || livechar::RehandleName(3)[0] != '?')
        Fail("[t920 18] every judgement has a name, out of range prints '?'");
    if (livechar::FingerprintName(same)[0] == '?' || livechar::FingerprintName(differs)[0] == '?' || livechar::FingerprintName(unknown)[0] == '?'
        || livechar::FingerprintName(-1)[0] != '?')
        Fail("[t920 19] every fingerprint answer has a name, out of range prints '?'");
}

/* src/common/livechar.h: a stored Character pointer is called into only when the engine still knows it this frame. */
void t_livechar_judge()
{
    /*                           plaus  retir  world  handle stored doomed main   self   other  update */
    if (livechar::Judge(true,  false, true,  true,  true,  false, true,  true,  false, false) != livechar::kLive) Fail("[1] the handle resolves to this address: live");
    if (livechar::Judge(false, false, true,  true,  true,  false, true,  true,  false, false) != livechar::kNoObject) Fail("[2] no game vtable: refused before anything is asked");
    if (livechar::Judge(true,  true,  true,  true,  true,  false, true,  true,  false, false) != livechar::kRetired) Fail("[3] withdrawn by the registry: refused even if it resolves");
    if (livechar::Judge(true,  false, false, true,  true,  false, true,  true,  false, false) != livechar::kNoWorld) Fail("[4] no GameWorld: refused");
    if (livechar::Judge(true,  false, true,  false, true,  false, true,  false, false, true) != livechar::kNoHandle) Fail("[5] no CHARACTER handle in the bytes: refused, the update list is not enough");
    if (livechar::Judge(true,  false, true,  true,  true,  true,  true,  true,  false, true) != livechar::kDoomed) Fail("[6] destroy accepted, still resolving and still listed: refused");
    if (livechar::Judge(true,  false, true,  true,  true,  false, false, true,  false, false) != livechar::kUnasked) Fail("[7] off the main thread: the registry is not asked, the checks that ran decide (usable)");
    if (livechar::Judge(true,  false, true,  true,  true,  false, true,  false, false, false) != livechar::kGone) Fail("[8] resolves to nothing and not updated: gone");
    if (livechar::Judge(true,  false, true,  true,  true,  false, true,  false, true,  false) != livechar::kOtherObject) Fail("[9] resolves to another character: the bytes here are not that character");
    if (livechar::Judge(true,  false, true,  true,  true,  false, true,  false, false, true) != livechar::kLive) Fail("[10] handle fails but the engine updates this address (P034 falseGone): live");
    if (livechar::Judge(true,  false, true,  true,  true,  false, true,  false, true,  true) != livechar::kLive) Fail("[11] handle names another but the engine updates this address: live");
    if (livechar::Judge(true,  false, true,  true,  true,  true,  false, false, false, false) != livechar::kDoomed) Fail("[12] doomed is known on any thread: doomed before unasked");
    if (livechar::Judge(true,  true,  true,  true,  true,  false, false, false, false, false) != livechar::kRetired) Fail("[13] off the main thread a retired pointer is still refused");
    if (livechar::Judge(true,  false, true,  true,  false, false, true,  true,  false, true) != livechar::kNotStored) Fail("[14] a new character at a reused address resolves to itself, but its handle is not the stored one: refused");
    if (livechar::Judge(true,  false, true,  true,  false, false, false, false, false, false) != livechar::kNotStored) Fail("[15] the stored-handle compare runs off the main thread too");
    if (!livechar::Usable(livechar::kLive) || !livechar::Usable(livechar::kUnasked)) Fail("[16] live and unasked are usable");
    for (int v = livechar::kNoObject; v < livechar::kVerdicts; ++v)
        if (livechar::Usable(v)) Fail("[17] every other verdict is a refusal");
    for (int v = 0; v < livechar::kVerdicts; ++v)
        if (livechar::VerdictName(v)[0] == '?') Fail("[18] every verdict has a REPORT name");
    if (livechar::VerdictName(livechar::kVerdicts)[0] != '?' || livechar::VerdictName(-1)[0] != '?') Fail("[19] out-of-range verdicts print '?'");
    if (!livechar::SameStored(0, 0, 7, 9)) Fail("[20] a row that stored no handle does not decide");
    if (!livechar::SameStored(7, 9, 7, 9)) Fail("[21] the same index and serial: the same character");
    if (livechar::SameStored(7, 9, 7, 10)) Fail("[22] a new serial: another character at this address");
    if (!livechar::SameStored(7, 9, 8, 9)) Fail("[23] the same serial with another index: the same character moved (a squad change)");
    if (!livechar::Renote(7, 9, 8, 9)) Fail("[24] same serial, index moved: the stored handle is brought up to date");
    if (livechar::Renote(7, 9, 7, 9) || livechar::Renote(7, 9, 8, 10) || livechar::Renote(0, 0, 8, 9)) Fail("[25] nothing to re-note: unchanged, another character, or nothing stored");
    if (livechar::DoomEnds(true, false) || livechar::DoomEnds(false, true) || livechar::DoomEnds(true, true)) Fail("[26] a doomed entry stays while the handle or the update list still names the address (a pause before the flush)");
    if (!livechar::DoomEnds(false, false)) Fail("[27] neither names it: the flush freed it, the entry ends");
    {   /* a destroy at a row's address: the object's handle against the row's stored one */
        const int a = livechar::DestroyIdentityOf(false, 7, 9, true, 7, 9);
        if (a != livechar::kDestroyIsRow || !livechar::DestroyAnswersUid(a) || livechar::DestroyMarksRow(a)) Fail("[28] a live row, the same handle: the row's uid, nothing marked");
        const int b = livechar::DestroyIdentityOf(true, 7, 9, true, 7, 10);
        if (b != livechar::kDestroyNotRow || livechar::DestroyAnswersUid(b) || !livechar::DestroyMarksRow(b)) Fail("[29] a dead row, another serial: 0 and the row marked destroyed");
        const int c = livechar::DestroyIdentityOf(true, 0, 0, true, 7, 9);
        if (c != livechar::kDestroyNoProof || livechar::DestroyAnswersUid(c) || livechar::DestroyMarksRow(c)) Fail("[30] a dead row that stored no handle: 0 (nothing proves it is the character)");
        const int d = livechar::DestroyIdentityOf(true, 7, 9, false, 0, 0);
        if (d != livechar::kDestroyNotRow || livechar::DestroyAnswersUid(d) || !livechar::DestroyMarksRow(d)) Fail("[31] a dead row, the object has no character handle: 0 and the row marked destroyed");
        const int e = livechar::DestroyIdentityOf(false, 0, 0, true, 7, 9);
        if (e != livechar::kDestroyIsRow || !livechar::DestroyAnswersUid(e)) Fail("[32] a live row that stored no handle: the address answers as before");
        const int f = livechar::DestroyIdentityOf(false, 0, 0, false, 0, 0);
        if (f != livechar::kDestroyNotRow || livechar::DestroyAnswersUid(f) || !livechar::DestroyMarksRow(f)) Fail("[33] a live row that stored no handle, the object has no character handle: not the row's character, the row marked destroyed");
        if (livechar::DestroyIdentityOf(false, 7, 9, true, 7, 10) != livechar::kDestroyNotRow) Fail("[34] a live row, another serial: another character at a reused address");
        if (livechar::DestroyIdentityOf(false, 7, 9, false, 0, 0) != livechar::kDestroyNotRow) Fail("[35] a live row, the object has no character handle: not the row's character");
        if (livechar::DestroyIdentityOf(true, 7, 9, true, 8, 9) != livechar::kDestroyIsRow) Fail("[36] a dead row, the same serial with another index: the row's character (moved squads)");
    }
}
/* PROBE P124 (src/common/p124stop.h): the owner's reports read as a trip - moving at >= 2 u/s, still under 0.5 u/s, the first
 * still report after a moving one is the stop, a moving report while the stop is open is a resume. */
void t_p124_stop_report_reading()
{
    using namespace p124;
    int ph = kPhaseIdle;
    if (OnReport(&ph, 0.0f) != kRepNone || ph != kPhaseIdle) Fail("[1] a still report with no trip before it is no stop");
    if (OnReport(&ph, 1.0f) != kRepNone || ph != kPhaseIdle) Fail("[2] a report between still and moving starts nothing");
    if (OnReport(&ph, 2.0f) != kRepMoving || ph != kPhaseMoving) Fail("[3] 2 u/s is moving");
    if (OnReport(&ph, 5.0f) != kRepMoving || ph != kPhaseMoving) Fail("[4] each moving report is a moving report");
    if (OnReport(&ph, 1.0f) != kRepNone || ph != kPhaseMoving) Fail("[5] slowing (0.5..2 u/s) is neither moving nor the stop");
    if (OnReport(&ph, 0.5f) != kRepNone || ph != kPhaseMoving) Fail("[6] 0.5 u/s is not still");
    if (OnReport(&ph, 0.2f) != kRepStop || ph != kPhaseStopped) Fail("[7] the first still report after moving is the stop");
    if (OnReport(&ph, 0.0f) != kRepNone || ph != kPhaseStopped) Fail("[8] a second still report is not a second stop");
    if (OnReport(&ph, 3.0f) != kRepResumed || ph != kPhaseMoving) Fail("[9] moving while the stop is open is a resume");
    const float nan = std::sqrt(-1.0f);
    if (OnReport(&ph, nan) != kRepNone || ph != kPhaseMoving) Fail("[10] a non-number speed changes nothing");
    if (OnReport(&ph, 0.0f) != kRepStop) Fail("[11] the next still report is the stop again");
    ph = kPhaseIdle;   /* the caller ends the measure */
    if (OnReport(&ph, 4.0f) != kRepMoving) Fail("[12] after the measure ended a moving report is a plain moving one, not a resume");
}
/* PROBE P124: the measure along the owner's last heading - the farthest reach past the still point, the way back, the end
 * distance; a zero last velocity takes the heading from the last moving position to the still position. */
void t_p124_stop_measure()
{
    using namespace p124;
    {   /* runs 3 u past and comes 2 u back */
        Pt s[7] = { {8,0}, {9,0}, {10,0}, {12,0}, {13,0}, {12,0}, {11,0} };
        const Measure m = MeasureStop(s, 7, 9.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f);
        if (!m.headingOk || m.farIndex != 4 || !NearF(m.overshoot, 3.0f) || !NearF(m.back, 2.0f) || m.backtrack != 1 || !NearF(m.endDist, 1.0f))
            Fail("[1] 3 u past, 2 u back: overshoot 3, back 2, a backtrack, end 1 u");
        if (!CountsAsOvershoot(m.overshoot)) Fail("[2] 3 u is counted");
    }
    {   /* stops short */
        Pt s[4] = { {7,0}, {8,0}, {9,0}, {9.5f,0} };
        const Measure m = MeasureStop(s, 4, 9.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f);
        if (!NearF(m.overshoot, 0.0f) || m.backtrack != 0 || !NearF(m.endDist, 0.5f) || !NearF(m.farAlong, -0.5f))
            Fail("[3] short of the still point: overshoot 0, no backtrack, end 0.5 u");
    }
    {   /* zero last velocity: the heading is last moving -> still (+z) */
        Pt s[2] = { {10,9}, {10,11} };
        const Measure m = MeasureStop(s, 2, 10.0f, 5.0f, 0.0f, 0.0f, 10.0f, 10.0f);
        if (!m.headingOk || !NearF(m.overshoot, 1.0f) || m.backtrack != 0) Fail("[4] a zero velocity takes the heading from the positions");
    }
    {   /* no heading at all: no overshoot verdict, the end distance still read */
        Pt s[1] = { {13,14} };
        const Measure m = MeasureStop(s, 1, 10.0f, 10.0f, 0.0f, 0.0f, 10.0f, 10.0f);
        if (m.headingOk || !NearF(m.overshoot, 0.0f) || m.backtrack != 0 || !NearF(m.endDist, 5.0f)) Fail("[5] no heading: overshoot 0, endDist read");
    }
    {   /* no samples */
        const Measure m = MeasureStop(0, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
        if (m.farIndex != -1 || !NearF(m.endDist, -1.0f) || !NearF(m.overshoot, 0.0f)) Fail("[6] no samples: nothing measured");
    }
    {   /* a 0.4 u reach is not a backtrack however far it comes back */
        Pt s[3] = { {10.4f,0}, {9,0}, {8,0} };
        const Measure m = MeasureStop(s, 3, 9.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f);
        if (m.backtrack != 0 || !NearF(m.overshoot, 0.4f) || CountsAsOvershoot(m.overshoot)) Fail("[7] a reach of 0.4 u is no backtrack and not counted");
    }
    {   /* sideways of the still point is not past it */
        Pt s[1] = { {10,5} };
        const Measure m = MeasureStop(s, 1, 9.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f);
        if (!NearF(m.overshoot, 0.0f) || !NearF(m.endDist, 5.0f)) Fail("[8] 5 u to the side: no overshoot, end 5 u");
    }
    {   /* the heading is the velocity's direction, not its size; the threshold is strictly above 2 u */
        Pt s[1] = { {12,0} };
        const Measure m = MeasureStop(s, 1, 9.0f, 0.0f, 50.0f, 0.0f, 10.0f, 0.0f);
        if (!NearF(m.overshoot, 2.0f) || CountsAsOvershoot(m.overshoot)) Fail("[9] exactly 2 u past is not counted");
    }
}
/* review-p8n-b M-1: THE ABANDON WAS NOT STICKY.  P8n-b's abandon cleared reportPending and booked
 * nothing durable on the row, so the next dirty tick found no episode open, opened a fresh one and
 * started retrying again - the documented "a door in a sector nobody holds cannot report for ever"
 * held for exactly one fifteen-second episode and then repeated for ever.  [5], [6] and [7] cannot be
 * produced by the P8n-b logic at all: DoorReportRetryDecide has no term for a row that gave up. */
void t_door_an_abandoned_report_stays_abandoned_until_the_door_agrees()
{
    /* the ordinary life of one episode, unchanged */
    if (coopdoor::DoorReportDecide(0, 0, 1000u, 0u, 0u) != coopdoor::kDoorReportOpen)
        Fail("[1] a row with no episode open opens one and sends nothing - the funnel already queued it");
    if (coopdoor::DoorReportDecide(0, 1, 1000u, 1000u, 1000u) != coopdoor::kDoorReportWait)
        Fail("[2] the tick the report was made does not immediately re-send it");
    if (coopdoor::DoorReportDecide(0, 1, 3000u, 1000u, 1000u) != coopdoor::kDoorReportRetry)
        Fail("[3] two seconds with no holder word RE-SENDS it");
    if (coopdoor::DoorReportDecide(0, 1, 16001u, 1000u, 15000u) != coopdoor::kDoorReportAbandon)
        Fail("[4] past fifteen seconds from the FIRST send the report is abandoned");
    /* and the latch, which is the whole of M-1 */
    if (coopdoor::DoorReportDecide(1, 0, 16002u, 1000u, 15000u) != coopdoor::kDoorReportSuppressed)
        Fail("[5] the very next dirty tick does NOT open a fresh episode");
    if (coopdoor::DoorReportDecide(1, 0, 999999u, 1000u, 15000u) != coopdoor::kDoorReportSuppressed)
        Fail("[6] ... and never does, however long the disagreement lasts - THAT is the bound the docs state");
    if (coopdoor::DoorReportDecide(1, 1, 20000u, 1000u, 1000u) != coopdoor::kDoorReportSuppressed)
        Fail("[7] the latch outranks a due retry as well as a fresh open");
    /* agreement is the ONLY thing that clears it (doors.cpp's terminal-state compare), and a later
       disagreement is a new event entitled to the whole bound again */
    if (coopdoor::DoorReportDecide(0, 0, 40000u, 0u, 0u) != coopdoor::kDoorReportOpen)
        Fail("[8] once the door has AGREED, a later disagreement opens a fresh episode");
    if (coopdoor::DoorReportDecide(0, 1, 41000u, 40000u, 40000u) != coopdoor::kDoorReportWait)
        Fail("[9] and that episode gets the full fifteen seconds again");
    if (coopdoor::DoorReportDecide(0, 1, 55001u, 40000u, 54000u) != coopdoor::kDoorReportAbandon)
        Fail("[10] ... measured from ITS own first send, not from the run");
    {   /* the bounds are unsigned differences and survive the 49.7-day GetTickCount wrap */
        const unsigned long first = 0xFFFFF000u;
        if (coopdoor::DoorReportDecide(0, 1, 0x00000100u, first, first) != coopdoor::kDoorReportRetry)
            Fail("[11] a retry due across the wrap is still a retry");
        if (coopdoor::DoorReportDecide(1, 1, 0x00000100u, first, first) != coopdoor::kDoorReportSuppressed)
            Fail("[12] and an abandoned row across the wrap is still abandoned");
    }
}

/* review-p8n-b M-2: THE OWN-KEY REFUSAL FIRED WHILE THE AREA PICTURE WAS LOADING, AND IT ATE THE
 * SENDER'S GENERATION.  ItAreaVerdictAt's first arm answers kBoxMine for kPicLoading - deliberately,
 * so a game can act on its own world while it builds it - and that arm is entered whenever engine
 * writes are blocked OR no area map has arrived for this world yet.  P8n-b's refusal read that
 * verdict, so before the first AREAMAP landed this game refused EVERY holder publish; and it wrote
 * the ordering filter's generation BEFORE returning, so the discarded word consumed the holder's
 * sequence and the holder's next publish was filtered out instead of followed.  [1] and [6] both
 * fail on the P8n-b logic. */
void t_door_a_holder_word_is_refused_only_for_a_settled_own_door()
{
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaLoading) != coopdoor::kDoorWordStoreLoading)
        Fail("[1] while the area picture is LOADING the word is STORED, not refused - a refusal has to"
             " be earned by a settled answer, and 'mine' during a load is a presumption");
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaMine) != coopdoor::kDoorWordRefuse)
        Fail("[2] a SETTLED 'this game holds that patch of map' is the only refusal");
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaOther) != coopdoor::kDoorWordStore)
        Fail("[3] a settled 'the other game holds it' stores the word");
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaNobody) != coopdoor::kDoorWordStore)
        Fail("[4] and so does a settled 'nobody holds it'");
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaUnanswerable) != coopdoor::kDoorWordStoreUnanswerable)
        Fail("[5] an unanswerable question stores the word and is COUNTED rather than booking nothing (F172)");
    if (coopdoor::DoorWordAdvancesGen(coopdoor::kDoorWordRefuse) != 0)
        Fail("[6] a REFUSED message must not consume the sender's generation - the ordering filter would"
             " then drop the holder's next publish and the holder has no reason to resend");
    if (coopdoor::DoorWordAdvancesGen(coopdoor::kDoorWordStore) != 1 ||
        coopdoor::DoorWordAdvancesGen(coopdoor::kDoorWordStoreLoading) != 1 ||
        coopdoor::DoorWordAdvancesGen(coopdoor::kDoorWordStoreUnanswerable) != 1)
        Fail("[7] every message that IS stored advances it, or newest-wins stops working");
}

/* P8o (T236g, 2026-09-18): the holder had no DoorRow for the key its peer reported 24 times, so every
 * report was booked lastActor.unnameable and dropped, and the two games showed that door differently
 * for about 100 s.  WHAT "FAILS ON THE OLD BEHAVIOUR" MEANS HERE, EXACTLY (review-p8o L-4): none of
 * these lines would COMPILE against the old tree, because the decision did not exist - there was no
 * key -> door route at all.  The behavioural claim is the one [1] stands for: a key with no row had
 * exactly one outcome, dropped, and this is the verdict that now says otherwise.
 * P8o-b (review-p8o M-1): the teardown input is RETIRED - the apply loop this decision is reached
 * from has already returned on EngineWritesBlocked(), so that arm could never fire. */
void t_door_a_key_with_no_row_resolves_through_the_parent_building()
{
    /* [1] every check passes: the door is named, and the existing adopt path gets a row. */
    if (coopdoor::DoorResolveAccept(1, 1, 1, 1) != coopdoor::kDoorResolveAccept)
        Fail("[1] a key with no row is resolved through the parent building, not dropped");
    /* Each refusing arm, in the order the walk asks them. */
    if (coopdoor::DoorResolveAccept(0, 0, 0, 0) != coopdoor::kDoorResolveRefusedCheck)
        Fail("[2] an implausible array element is refused before anything is read out of it");
    if (coopdoor::DoorResolveAccept(1, 0, 0, 0) != coopdoor::kDoorResolveRefusedCheck)
        Fail("[3] a state outside 0..3 says this pointer is not a DoorStuff");
    if (coopdoor::DoorResolveAccept(1, 1, 0, 0) != coopdoor::kDoorResolveRefusedCheck)
        Fail("[4] a door whose parent is not the walked building is refused");
    if (coopdoor::DoorResolveAccept(1, 1, 1, 0) != coopdoor::kDoorResolveNoMatch)
        Fail("[5] a live door of this building that the key does not name is no match, not a refusal");
    /* A no-match must never read as an accept: that is what makes the EXACT key compare load-bearing,
     * and it is why the box road's nearest-position fallback was not copied into the door road. */
    if (coopdoor::DoorResolveAccept(1, 1, 1, 0) == coopdoor::kDoorResolveAccept)
        Fail("[6] a nearest-position match is neither available nor implied");
}

/* ================= B12 (decision 52): THE OUTAGE QUEUE =================
   Every branch of the conflict rule, the one assertion that FAILS under the rule this replaces, the
   journal line's round trip, a corrupt line, and the v6 -> v7 index line upgrade. */
void t_b12_the_conflict_rule_takes_every_branch()
{
    /* nobody wrote since: the seq the entry saw is the seq the notebook has now. */
    if (coopqueue::QueueReplayDecide(7ULL, 7ULL, 1, 0, 0) != coopqueue::kReplay) Fail("an unchanged record did not replay");
    if (coopqueue::QueueReplayDecide(0ULL, 0ULL, 1, 0, 0) != coopqueue::kReplay) Fail("an unstamped record did not replay");
    /* no record at all, and the entry was queued against no record: seq 0 on both sides. */
    if (coopqueue::QueueReplayDecide(0ULL, 0ULL, 0, 0, 0) != coopqueue::kReplay) Fail("an entry queued against no record did not replay");
    if (coopqueue::QueueReplayDecide(0ULL, 99ULL, 0, 0, 0) != coopqueue::kReplay) Fail("seqNow was READ although there is no record - hasRecordNow must decide that");
    /* somebody else wrote it while the notebook was away: the entry LOSES. */
    if (coopqueue::QueueReplayDecide(7ULL, 8ULL, 1, 0, 0) != coopqueue::kDropNewerWriter) Fail("a record another writer changed did not drop");
    if (coopqueue::QueueReplayDecide(0ULL, 1ULL, 1, 0, 0) != coopqueue::kDropNewerWriter) Fail("a first stamp by another writer did not drop");
    /* the notebook's number went backward - it lost history. Never a licence to replay. */
    if (coopqueue::QueueReplayDecide(8ULL, 7ULL, 1, 0, 0) != coopqueue::kDropUnknown) Fail("a backward seq did not read as unknown");
    if (coopqueue::QueueReplayDecide(8ULL, 0ULL, 0, 0, 0) != coopqueue::kDropUnknown) Fail("a record that has VANISHED did not read as unknown");
    /* a tie goes to the incumbent by construction: equal means unchanged, which IS the replay's case. */
    if (coopqueue::QueueReplayDecide(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 1, 0, 0) != coopqueue::kReplay) Fail("the top of the range did not compare equal");
}
/* THE ASSERTION THAT FAILS UNDER THE OLD RULE. Before B12 there was no per-record sequence number and the
   only ordering field was writtenAt - whole seconds of wall clock, stamped at WRITE time. A replay stamps
   itself at replay, so under that rule it is ALWAYS the newer write and always wins. This states the case
   the new rule must lose and shows the old one winning it. */
void t_b12_the_writtenat_rule_would_have_let_the_replay_win()
{
    const long long queuedAt = 100, theirWrite = 100, replayAt = 200;   /* the same second, then the replay */
    /* THE OLD RULE, written out: ApplyRemoteRecord's own test is "the newer writtenAt wins", so what would
       have been compared is the REPLAY'S OWN stamp - later than anything written during the outage. */
    const int oldRuleReplayWins = (replayAt > theirWrite) ? 1 : 0;
    if (oldRuleReplayWins != 1) Fail("the old writtenAt rule did not let the replay win - the premise of this test is wrong");
    if (theirWrite != queuedAt) Fail("the two writes were meant to land in the SAME second, which is where writtenAt ties");
    /* THE NEW RULE, on the same facts: their write took the next sequence number, so the entry loses. */
    if (coopqueue::QueueReplayDecide(4ULL, 5ULL, 1, 0, 0) != coopqueue::kDropNewerWriter)
        Fail("the new rule let a replay win over another writer - which is exactly what the sequence number exists to stop");
}
void t_b12_a_journal_line_round_trips()
{
    coopqueue::QueueEntry e;
    e.family = coopqueue::kQueueFamilyBox;
    e.key = "zone.-3.14";
    e.seqSeen = 123456789012345ULL;
    e.queuedAtUnix = 1789000000LL;
    e.payload.push_back((char)0x00); e.payload.push_back((char)0x7F);
    e.payload.push_back((char)0x80); e.payload.push_back((char)0xFF); e.payload.push_back('A');
    std::string line;
    if (coopqueue::QueueLineFormat(e, &line) == 0) Fail("a good entry would not format");
    if (line.empty() || line[line.size() - 1] != '\n') Fail("the formatted line does not end in a newline");
    if (line.find("q1\t0\tzone.-3.14\t123456789012345\t1789000000\t") != 0) Fail("the line's shape is not q1 family key seq when hex: " + line);
    if (line.find("007f80ff41") == std::string::npos) Fail("the payload is not lowercase hex, two characters per byte: " + line);
    coopqueue::QueueEntry b;
    if (coopqueue::QueueLineParse(line, &b) == 0) Fail("the line this build wrote would not parse back");
    if (b.family != e.family) Fail("the family did not survive the round trip");
    if (b.key != e.key) Fail("the key did not survive the round trip");
    if (b.seqSeen != e.seqSeen) Fail("the sequence number did not survive the round trip");
    if (b.queuedAtUnix != e.queuedAtUnix) Fail("the queued-at time did not survive the round trip");
    if (b.payload.size() != e.payload.size()) Fail("the payload changed length in the round trip");
    for (std::vector<char>::size_type i = 0; i < e.payload.size(); ++i)
        if (b.payload[i] != e.payload[i]) Fail("a payload byte changed in the round trip at " + I((int)i));
    /* an empty payload is a real entry and not a failure - a door entry's key is all it needs to be judged */
    coopqueue::QueueEntry d; d.family = coopqueue::kQueueFamilyDoor; d.key = "door.1"; d.queuedAtUnix = 1;
    std::string dl;
    if (coopqueue::QueueLineFormat(d, &dl) == 0) Fail("an entry with no payload would not format");
    coopqueue::QueueEntry d2;
    if (coopqueue::QueueLineParse(dl, &d2) == 0) Fail("an entry with no payload would not parse back");
    if (!d2.payload.empty()) Fail("an empty payload came back with bytes in it");
    /* a line that has been through a text-mode round trip still parses */
    coopqueue::QueueEntry crl;
    if (coopqueue::QueueLineParse("q1\t2\tzone.0.0\t1\t2\tff\r", &crl) == 0) Fail("a line with a trailing CR would not parse");
    if (crl.payload.size() != 1 || crl.payload[0] != (char)0xFF) Fail("the trailing CR was read as part of the payload");
}
void t_b12_a_corrupt_journal_line_is_dropped_not_guessed()
{
    coopqueue::QueueEntry e;
    if (coopqueue::QueueLineParse("", &e) != 0) Fail("an empty line parsed");
    if (coopqueue::QueueLineParse("q2\t0\tk\t1\t2\t", &e) != 0) Fail("an unknown tag parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2", &e) != 0) Fail("a line with five fields parsed");
    /* B12-c: SEVEN fields is legal now - the seventh is ownAhead and it is 0 or 1 and nothing else. */
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2\t\textra", &e) != 0) Fail("a seventh field that is not 0 or 1 parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2\t\t", &e) != 0) Fail("an empty seventh field parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2\t\t1\t1", &e) != 0) Fail("a line with eight fields parsed");
    if (coopqueue::QueueLineParse("q1\t3\tk\t1\t2\t", &e) != 0) Fail("a family this build does not know parsed");
    if (coopqueue::QueueLineParse("q1\t0\t\t1\t2\t", &e) != 0) Fail("an empty key parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t\t2\t", &e) != 0) Fail("an empty sequence number parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1x\t2\t", &e) != 0) Fail("a non-numeric sequence number parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t-1\t2\t", &e) != 0) Fail("a negative sequence number parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t99999999999999999999\t2\t", &e) != 0) Fail("a sequence number past a u64 parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\tx\t", &e) != 0) Fail("a non-numeric time parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2\tabc", &e) != 0) Fail("a hex payload of odd length parsed");
    if (coopqueue::QueueLineParse("q1\t0\tk\t1\t2\tgg", &e) != 0) Fail("a payload with a non-hex character parsed");
    /* and the formatter refuses to MAKE a line that could not be read back */
    coopqueue::QueueEntry bad; bad.family = coopqueue::kQueueFamilyZone; bad.key = "a\tb"; bad.queuedAtUnix = 1;
    std::string out;
    if (coopqueue::QueueLineFormat(bad, &out) != 0) Fail("a key with a tab in it was formatted - it would split into two fields");
    bad.key = "a\nb";
    if (coopqueue::QueueLineFormat(bad, &out) != 0) Fail("a key with a newline in it was formatted - it would split into two lines");
    bad.key = "ok"; bad.family = 7;
    if (coopqueue::QueueLineFormat(bad, &out) != 0) Fail("an unknown family was formatted");
    bad.family = coopqueue::kQueueFamilyZone; bad.queuedAtUnix = -1;
    if (coopqueue::QueueLineFormat(bad, &out) != 0) Fail("a negative queued-at time was formatted");
}
/* THE INDEX LINE'S OWN UPGRADE, v6 -> v7 WITH seq 0, and the monotonic counter the notebook derives from a
   folder full of lines. The notebook's OWN index code (PutInIndex, NextSeq) lives in SharedWastelandsServer.exe and is
   not compiled into this exe, so what is asserted here is the two things that are: the line format either
   side of the upgrade, and the arithmetic the counter is raised by. */
/* the group's home building key: optional field 15 of a v7 line, written only when the record has one, read back as "" from every
   line without it, refusing a key the line could not hold; and the key a record keeps when another write of it arrives */
void t591_home_key_rides_the_v7_line()
{
    coopstore::MetaLine m;
    m.version = 7; m.writtenAt = 1700000000LL; m.posAt = 1700000001LL;
    m.owner = 2; m.x = 1.0f; m.y = 2.0f; m.z = 3.0f;
    m.squadSid = "sid"; m.factionName = "fac"; m.worldId = "The Holy Nation_65545"; m.town = "town";
    m.len = 10; m.crc = 0xDEADBEEFu; m.seq = 44ULL;
    std::string none;
    if (!coopstore::MetaEncodeV7(m, &none)) { Fail("a v7 line with no home would not encode"); return; }
    {
        int tabs = 0;
        for (std::string::size_type i = 0; i < none.size(); ++i) if (none[i] == '\t') ++tabs;
        if (tabs != 13) Fail("a v7 line with no home is not the 14-field line it was before the home field existed");
    }
    coopstore::MetaLine back;
    if (!coopstore::MetaParse(none, &back) || !back.home.empty() || back.seq != 44ULL) Fail("a v7 line with no home did not read back as no home");
    m.home = "house_large@40@37@1234@567@-890";   /* an illustrative position key (no TAB, CR or LF) */
    std::string with;
    if (!coopstore::MetaEncodeV7(m, &with)) { Fail("a v7 line with a home would not encode"); return; }
    if (with.compare(0, none.size() - 1, none, 0, none.size() - 1) != 0) Fail("the home field is not appended after the 14 fields");
    if (!coopstore::MetaParse(with, &back)) { Fail("a v7 line with a home was REFUSED"); return; }
    if (back.home != m.home || back.seq != 44ULL || back.town != "town" || back.worldId != m.worldId || back.len != 10 || back.crc != 0xDEADBEEFu)
        Fail("a v7 line with a home did not round trip");
    {   /* a CR after the last field (a text-mode round trip) is not part of the key */
        const std::string cr = with.substr(0, with.size() - 1) + "\r\n";
        coopstore::MetaLine c;
        if (!coopstore::MetaParse(cr, &c) || c.home != m.home) Fail("a CR after the home field stayed in the key");
    }
    {   /* AN OLDER v7 READER reads 14 fields by index: field 14 is still the whole seq, and the home is a 15th it ignores */
        std::vector<std::string> f;
        const std::string body = with.substr(0, with.size() - 1);
        std::string::size_type at = 0;
        for (;;)
        {
            const std::string::size_type t = body.find('\t', at);
            if (t == std::string::npos) { f.push_back(body.substr(at)); break; }
            f.push_back(body.substr(at, t - at)); at = t + 1;
        }
        if (f.size() != 15 || f[0] != "v7" || f[13] != "44" || f[14] != m.home) Fail("the home is not field 15 after an unchanged field 14");
    }
    {   /* older lines carry no home and read back as none */
        std::string v6;
        if (!coopstore::MetaEncodeV6(m, &v6)) Fail("a v6 line would not encode");
        if (v6.find(m.home) != std::string::npos) Fail("a v6 line carried the home field");
        coopstore::MetaLine b6;
        if (!coopstore::MetaParse(v6, &b6) || !b6.home.empty()) Fail("a v6 line read back a home it does not carry");
        coopstore::MetaLine b4;
        if (!coopstore::MetaParse("v4\t900\t2\t10\t20\t30\tsid\tfac\tFac_1\t950\ttown", &b4) || !b4.home.empty()) Fail("a v4 line read back a home");
    }
    {   /* a key the line cannot hold refuses the line, as every other field does */
        coopstore::MetaLine bad = m; bad.home = "a\tb";
        std::string s;
        if (coopstore::MetaEncodeV7(bad, &s)) Fail("a home with a TAB was written - it would split the line");
    }
    /* the key a record keeps when another write of it arrives */
    if (coopstore::MetaHomeMerge("", "") != "") Fail("merge: no key and no key");
    if (coopstore::MetaHomeMerge("A", "") != "A") Fail("merge: a write without a home erased the key the group was made from");
    if (coopstore::MetaHomeMerge("", "B") != "B") Fail("merge: the first key known was not kept");
    /* review F6: a home key off the wire is kept only when a line can hold it and it fits the key cap */
    if (coopstore::MetaHomeClean("12.5_3.0_-7.25") != "12.5_3.0_-7.25") Fail("clean: a usable home key was dropped");
    if (coopstore::MetaHomeClean("a\tb") != "" || coopstore::MetaHomeClean("a\rb") != "" || coopstore::MetaHomeClean("a\nb") != "") Fail("clean: a home key a line cannot hold was kept");
    if (coopstore::MetaHomeClean(std::string(coopstore::kMetaHomeCap, 'k')).size() != coopstore::kMetaHomeCap) Fail("clean: a key at the cap was dropped");
    if (coopstore::MetaHomeClean(std::string(coopstore::kMetaHomeCap + 1, 'k')) != "") Fail("clean: a key over the cap was kept");
    if (coopstore::MetaHomeClean("") != "") Fail("clean: no key became a key");
    if (coopstore::MetaHomeMerge("A", "B") != "B") Fail("merge: the live group's home did not replace the older key");
    if (coopstore::MetaHomeMerge("A", "x\ty") != "A") Fail("merge: a key a line cannot hold replaced a good one");
}
/* a house a player bought: which test names it a player's house, the cleared home mark "-" on every road, a give refused as a
   player's house, and the purchase of a house whose residents are on hold (made first, or the entry dropped) [t805b-tests] */
void t805b_player_house_and_cleared_home()
{
    /* PlayerHouseSkips: unread first (as before), then the marker, a stand-in, the build record */
    for (int m = 0; m < 2; ++m) for (int si = 0; si < 2; ++si) for (int rc = 0; rc < 2; ++rc) for (int u = 0; u < 2; ++u)
    {
        const int want = (u != 0) ? townreoffer::kHouseNot : (m != 0 ? townreoffer::kHousePlayer : (si != 0 ? townreoffer::kHouseStandIn : (rc != 0 ? townreoffer::kHouseRecord : townreoffer::kHouseNot)));
        if (townreoffer::PlayerHouseSkips(m, si, rc, u) != want) Fail("player house: the truth table answered wrong");
    }
    if (townreoffer::PlayerHouseSkips(0, 0, 0, 0) != townreoffer::kHouseNot) Fail("player house: a town building was skipped");
    if (townreoffer::PlayerHouseSkips(1, 0, 0, 1) != townreoffer::kHouseNot) Fail("player house: an unreadable owner did not behave as before");
    if (townreoffer::PlayerHouseSkips(0, 1, 0, 0) != townreoffer::kHouseStandIn) Fail("player house: another player's stand-in owner was populated");
    if (townreoffer::PlayerHouseSkips(0, 0, 1, 0) != townreoffer::kHouseRecord) Fail("player house: the build record alone did not name it");
    if (std::string(townreoffer::PlayerHouseWord(townreoffer::kHousePlayer)) != "player" || std::string(townreoffer::PlayerHouseWord(townreoffer::kHouseStandIn)) != "standin"
        || std::string(townreoffer::PlayerHouseWord(townreoffer::kHouseRecord)) != "record") Fail("player house: the log word");
    /* the world server's merge keeps the mark like any key */
    if (coopstore::MetaHomeMerge("A", "-") != "-") Fail("cleared: the mark did not replace the house key");
    if (coopstore::MetaHomeMerge("-", "") != "-") Fail("cleared: a write without a home erased the mark");
    if (coopstore::MetaHomeMerge("-", "B") != "B") Fail("cleared: the server's plain merge takes a later key");
    if (coopstore::MetaHomeClean("-") != "-") Fail("cleared: the wire clean dropped the mark");
    /* the readers that give homes read the mark as no key */
    if (!coopstore::MetaHomeIsCleared("-") || coopstore::MetaHomeIsCleared("") || coopstore::MetaHomeIsCleared("A")) Fail("cleared: the mark test");
    if (coopstore::MetaHomeKey("-") != "" || coopstore::MetaHomeKey("A") != "A" || coopstore::MetaHomeKey("") != "") Fail("cleared: the reader mapping");
    if (coopstore::LoadOverrideHomeDecide(0, 0, coopstore::MetaHomeKey("-").empty() ? 0 : 1) != coopstore::kLoHomeNoneAnywhere) Fail("cleared: a load override with the mark was owed a home");
    /* the game's own note: the mark stays against the cleared key or one near it (sameAsCleared 1, from any road), and against every
       non-live key while the cleared key is unknown (-1); any other key (0) replaces it */
    if (coopstore::MetaHomeNote("-", "H", 0, 1) != "-") Fail("cleared: an older record naming the house undid the mark");
    if (coopstore::MetaHomeNote("-", "H~", 0, 1) != "-") Fail("cleared: an older record naming a key near the house undid the mark");
    if (coopstore::MetaHomeNote("-", "H", 1, 1) != "-") Fail("cleared: a live group still homed in the house undid the mark");
    if (coopstore::MetaHomeNote("-", "B", 0, 0) != "B") Fail("cleared: a record naming another building did not replace the mark");
    if (coopstore::MetaHomeNote("-", "B", 1, 0) != "B") Fail("cleared: the group's own new home did not replace the mark");
    if (coopstore::MetaHomeNote("-", "B", 1, -1) != "B") Fail("cleared: a live new home with the cleared key unknown was refused");
    if (coopstore::MetaHomeNote("-", "B", 0, -1) != "-") Fail("cleared: a record with the cleared key unknown undid the mark");
    if (coopstore::MetaHomeNote("-", "", 1, 0) != "-" || coopstore::MetaHomeNote("-", "", 0, -1) != "-") Fail("cleared: no key erased the mark");
    if (coopstore::MetaHomeNote("-", "-", 0, 0) != "-") Fail("cleared: the mark arriving again changed it");
    if (coopstore::MetaHomeNote("A", "-", 0, -1) != "-") Fail("cleared: another game's mark was not taken");
    if (coopstore::MetaHomeNote("A", "B", 0, -1) != "B" || coopstore::MetaHomeNote("A", "", 0, -1) != "A" || coopstore::MetaHomeNote("A", "B", 1, 1) != "B") Fail("cleared: the plain merge changed");
    /* a give refused as a player's house drops the owed entry */
    if (coopstore::kGiveHomePlayerHouse != 4) Fail("give: the refusal code");
    if (coopstore::OwedHomeGiveAct(4) != coopstore::kOwedGiveDrop) Fail("give: a player's house was kept owed");
    if (coopstore::OwedHomeGiveAct(0) != coopstore::kOwedGiveKeep || coopstore::OwedHomeGiveAct(3) != coopstore::kOwedGiveKeep) Fail("give: a fault / not loaded was not retried");
    if (coopstore::OwedHomeGiveAct(1) != coopstore::kOwedGiveDone || coopstore::OwedHomeGiveAct(2) != coopstore::kOwedGiveDone) Fail("give: a given home stayed owed");
    /* owner decisions 809 b / 813 b: at the purchase the held residents are made first; anything but a run or residents already there
       drops the entry and the purchase goes on */
    if (townreoffer::BuyHeldDrops(townreoffer::kOneMade) != 0) Fail("buy held: a run (made) was dropped");
    if (townreoffer::BuyHeldDrops(townreoffer::kOneHas) != 0) Fail("buy held: residents already there were dropped");
    {
        const int dropped[] = { townreoffer::kOneWait, townreoffer::kOneClaim, townreoffer::kOneGone, townreoffer::kOneOther, townreoffer::kOneFault,
                                townreoffer::kOneAside, townreoffer::kOneRelease, townreoffer::kOneKept, townreoffer::kOneGivenUp,
                                townreoffer::kOnePlayerHouse, townreoffer::kOneCannot };
        for (size_t i = 0; i < sizeof(dropped) / sizeof(dropped[0]); ++i)
        {
            if (townreoffer::BuyHeldDrops(dropped[i]) != 1) Fail("buy held: an outcome that made nothing kept the entry (owner 813 b)");
            if (std::string(townreoffer::OneWord(dropped[i])) == "?") Fail("buy held: an outcome has no log word");
        }
    }
    if (std::string(townreoffer::OneWord(townreoffer::kOneMade)) != "made" || std::string(townreoffer::GateWord(-1)) != "test-lever"
        || std::string(townreoffer::GateWord(townreoffer::kReGenerate)) != "generate") Fail("buy held: the log words");
    /* the buy callback asks only on its confirm answer, the engine's own yes and a purse that covers the price (unread counts as covered) */
    if (townreoffer::BuyHeldAsk(2, 1, 1) != 1) Fail("buy held: a purchase going through was not asked");
    if (townreoffer::BuyHeldAsk(1, 1, 1) != 0 || townreoffer::BuyHeldAsk(0, 1, 1) != 0) Fail("buy held: a cancelled dialogue was asked");
    if (townreoffer::BuyHeldAsk(2, 0, 1) != 0 || townreoffer::BuyHeldAsk(2, -1, 1) != 0) Fail("buy held: a house the engine does not sell was asked");
    if (townreoffer::BuyHeldAsk(2, 1, 0) != 0) Fail("buy held: a purchase the purse cannot cover was asked");
    if (townreoffer::PurseCovers(100, 99) != 0 || townreoffer::PurseCovers(100, 100) != 1 || townreoffer::PurseCovers(-1, 0) != 1 || townreoffer::PurseCovers(100, -1) != 1)
        Fail("buy held: the purse test");
}
/* a copy built while its home building was not loaded here, and a record swapped in with no home [t755-copyhome-tests] */
void t755_copy_home_owed_and_load_override_keep()
{
    /* (a) the building resolved, or none was sent: nothing is owed */
    if (coopstore::CopyHomeOwe(1, 1, 1) != coopstore::kCopyHomeNone) Fail("a copy whose building resolved was left owing its home");
    if (coopstore::CopyHomeOwe(0, 0, 1) != coopstore::kCopyHomeNone) Fail("a copy sent with no building was made to owe one");
    if (coopstore::CopyHomeOwe(1, 1, 0) != coopstore::kCopyHomeNone) Fail("a resolved building on an unbound copy was counted");
    /* (b) sent and not resolved: owed while the copy is bound under its world id, counted only when it is not */
    if (coopstore::CopyHomeOwe(1, 0, 1) != coopstore::kCopyHomeOwe) Fail("a copy whose building did not resolve was not owed its home");
    if (coopstore::CopyHomeOwe(1, 0, 0) != coopstore::kCopyHomeUntracked) Fail("an unbound copy was listed as owed");
    /* (c) the owed list as the store's once-a-second check walks it (store.cpp CopyHomeOwedTick): an entry leaves it only when the
       squad's home is read back set, when it has one already, when it is gone, or after kCopyHomeMaxTries gives that did not take
       [t755-copyhome-walktest] */
    {
        struct E { int bound, hasHome, loaded, takesAt, gives, tries, out; };   /* takesAt: the give that takes (0 = none does); out: 0 = still listed */
        E list[6] = {
            { 1, 0, 1, 1, 0, 0, 0 },     /* loaded, the first give takes */
            { 1, 0, 0, 1, 0, 0, 0 },     /* its building never loads */
            { 0, 0, 1, 1, 0, 0, 0 },     /* gone */
            { 1, 0, 1, 0, 0, 0, 0 },     /* loaded, no give ever takes */
            { 1, 1, 0, 0, 0, 0, 0 },     /* it has a home already */
            { 1, 0, 1, 3, 0, 0, 0 } };   /* the third give takes */
        for (int round = 0; round < 10; ++round)
            for (int i = 0; i < 6; ++i)
            {
                E& e = list[i];
                if (e.out != 0) continue;
                const int v = coopstore::CopyHomeBefore(e.bound, e.hasHome, e.loaded);
                if (v == coopstore::kCopyHomeWait) continue;
                if (v != coopstore::kCopyHomeGive) { e.out = v; continue; }
                ++e.gives;
                const int a = coopstore::CopyHomeAfter((e.takesAt != 0 && e.gives >= e.takesAt) ? 1 : 0, &e.tries, coopstore::kCopyHomeMaxTries);
                if (a != coopstore::kCopyHomeRetry) e.out = a;
                if (round == 1 && i == 3 && e.out != 0) Fail("a give that did not take was dropped before its tries ran out");
            }
        if (list[0].out != coopstore::kCopyHomeDone || list[0].gives != 1 || list[0].tries != 0) Fail("a home that took on the first give did not leave the list once");
        if (list[1].out != 0 || list[1].gives != 0) Fail("an entry whose building is not loaded was given or dropped");
        if (list[2].out != coopstore::kCopyHomeDrop || list[2].gives != 0) Fail("a gone squad was given a home or kept listed");
        if (list[3].out != coopstore::kCopyHomeFailed || list[3].gives != coopstore::kCopyHomeMaxTries || list[3].tries != coopstore::kCopyHomeMaxTries) Fail("a give that never took was not tried exactly the maximum times, then dropped");
        if (list[4].out != coopstore::kCopyHomeDone || list[4].gives != 0) Fail("a squad with a home already was given one again");
        if (list[5].out != coopstore::kCopyHomeDone || list[5].gives != 3 || list[5].tries != 2) Fail("a home that took on the third give was not kept listed until it was read back set");
    }
    /* (d) LOAD-OVERRIDE, judged after the engine's read: (the squad's home after the read, the save's own home before it, a home key known) */
    for (int sh = 0; sh < 2; ++sh)
        for (int k = 0; k < 2; ++k)
            if (coopstore::LoadOverrideHomeDecide(1, sh, k) != coopstore::kLoHomeKept) Fail("a squad with a home after the read was touched");
    for (int k = 0; k < 2; ++k)
        if (coopstore::LoadOverrideHomeDecide(0, 1, k) != coopstore::kLoHomeRestore) Fail("the save's own home was not given back");
    if (coopstore::LoadOverrideHomeDecide(0, 0, 1) != coopstore::kLoHomeOweByKey) Fail("a squad homeless after the read with a known key was not owed by it");
    if (coopstore::LoadOverrideHomeDecide(0, 0, 0) != coopstore::kLoHomeNoneAnywhere) Fail("a squad with no home known anywhere was not said so");
}
void t_b12_a_v6_index_line_upgrades_to_v7_with_seq_zero()
{
    coopstore::MetaLine m;
    m.version = 6; m.writtenAt = 1700000000LL; m.posAt = 1700000001LL;
    m.owner = 5; m.x = 1.0f; m.y = 2.0f; m.z = 3.0f;
    m.squadSid = "sid"; m.factionName = "fac"; m.worldId = "zone.1.2"; m.town = "town";
    m.len = 10; m.crc = 0xDEADBEEFu; m.seq = 44ULL;
    std::string v6;
    if (!coopstore::MetaEncodeV6(m, &v6)) Fail("a v6 line would not encode");
    coopstore::MetaLine back;
    if (!coopstore::MetaParse(v6, &back)) Fail("a v6 line would not parse");
    if (back.version != 6) Fail("a v6 line did not read back as v6");
    if (back.seq != 0ULL) Fail("a v6 line handed back a sequence number it does not carry - a seq from nowhere would decide a conflict");
    /* THE UPGRADE IS THE WRITE: the same fields, the v7 tag, and seq as it stands - which for a line that
       carried none is 0. A 1 here would be a guess that reads as fact. */
    coopstore::MetaLine up = back;
    std::string v7;
    if (!coopstore::MetaEncodeV7(up, &v7)) Fail("the upgraded line would not encode as v7");
    coopstore::MetaLine after;
    if (!coopstore::MetaParse(v7, &after)) Fail("the upgraded v7 line would not parse");
    if (after.version != 7) Fail("the upgraded line did not read back as v7");
    if (after.seq != 0ULL) Fail("the upgrade invented a sequence number");
    if (after.writtenAt != m.writtenAt || after.posAt != m.posAt || after.owner != m.owner
        || after.squadSid != m.squadSid || after.factionName != m.factionName
        || after.worldId != m.worldId || after.town != m.town || after.len != m.len || after.crc != m.crc)
        Fail("the upgrade changed a field that was not seq");
    /* a real v7 line round trips its seq, and a v7 line with a broken seq field is REFUSED */
    up.seq = 987654321987ULL;
    if (!coopstore::MetaEncodeV7(up, &v7)) Fail("a stamped v7 line would not encode");
    if (!coopstore::MetaParse(v7, &after)) Fail("a stamped v7 line would not parse");
    if (after.seq != 987654321987ULL) Fail("the sequence number did not survive the index line");
    {
        std::string broken = v7;
        const std::string::size_type t = broken.rfind('\t');
        if (t == std::string::npos) Fail("the v7 line has no last field");
        broken = broken.substr(0, t + 1) + "12x\n";
        coopstore::MetaLine junk;
        if (coopstore::MetaParse(broken, &junk)) Fail("a v7 line with a non-numeric seq parsed - a seq read short decides a conflict wrongly and silently");
    }
    /* THE COUNTER ACROSS A RESTART: the notebook raises it past every seq the index load finds, so the
       next number it stamps is never one a record already carries. */
    {
        unsigned long long seqNext = 1ULL;
        const unsigned long long onDisk[4] = { 0ULL, 3ULL, 17ULL, 2ULL };
        int i;
        for (i = 0; i < 4; ++i) if (onDisk[i] >= seqNext) seqNext = onDisk[i] + 1ULL;
        if (seqNext != 18ULL) Fail("the next sequence number after a restart was not one past the highest on disk");
        const unsigned long long first = seqNext++;
        const unsigned long long second = seqNext++;
        if (first != 18ULL || second != 19ULL || second <= first) Fail("the stamp is not monotonic after a restart");
        if (first <= 17ULL) Fail("the first stamp after a restart re-issued a number a record already carries");
    }
}
/* B12-b: THE SEQUENCE ECHO'S ONE DECISION. The notebook tells the writer the number it stamped, and what
   the receiving game may do with it is the whole of this: a key it knows takes the number, a key it does
   not know is counted and ignored. The echo carries no position, no owner and no file, so a record
   invented from one would be a record made of a single number - which is why there is no third answer.
   That the APPLY writes only the seq field is the handler's own (store.cpp StoreApplyRecordSeq): this exe
   holds no record index, so the property asserted here is the decision, not the assignment. */
void t_b12b_a_seq_echo_applies_to_a_known_key_and_is_counted_for_an_unknown_one()
{
    if (coopqueue::QueueSeqEchoDecide(1) != coopqueue::kSeqEchoApply) Fail("an echo for a key this game holds did not apply");
    if (coopqueue::QueueSeqEchoDecide(0) != coopqueue::kSeqEchoUnknownKey) Fail("an echo for an unknown key did not read as unknown");
    /* the answer depends on that one fact and on nothing else - any non-zero is "I hold a record" */
    if (coopqueue::QueueSeqEchoDecide(2) != coopqueue::kSeqEchoApply) Fail("a non-zero other than 1 did not read as holding a record");
    if (coopqueue::QueueSeqEchoDecide(-1) != coopqueue::kSeqEchoApply) Fail("a non-zero negative did not read as holding a record");
    if (coopqueue::kSeqEchoApply == coopqueue::kSeqEchoUnknownKey) Fail("the two answers are the same value, so the caller cannot tell them apart");
    /* AND THE HOLE IT CLOSES, stated as the conflict rule sees it. Before B12-b this game's cached seq for
       a key it had written itself was one behind the notebook's, so the queued change read as another
       writer's; with the echo applied the two are equal and the change replays. */
    {
        const unsigned long long notebookStamped = 5ULL;
        const unsigned long long cachedWithoutEcho = 4ULL;   /* what B12 left behind */
        if (coopqueue::QueueReplayDecide(cachedWithoutEcho, notebookStamped, 1, 0, 0) != coopqueue::kDropNewerWriter)
            Fail("the stale-cache case did not drop - the premise of this test is wrong");
        if (coopqueue::QueueReplayDecide(notebookStamped, notebookStamped, 1, 0, 0) != coopqueue::kReplay)
            Fail("with the echo applied, this game's own earlier write still lost its queued change");
    }
}
/* B12-c (review-b12 H-2): THE DOOR DRAIN HAS THREE ANSWERS AND B12-a's FOURTH IS RETIRED. B12-a made the
   no-notebook verdict suppress the drain's ACTOR REPORT, which is a session-link message from a non-holder
   to the holder: not a durable write, nothing to do with the notebook, and it must keep flowing with or
   without one (decision 47). What decision 52 owes the door family is the holder's own state write, and
   that is journaled at the r4 refusal site in doors.cpp - the drain is not the place. So the decision is
   asserted to be UNCHANGED by B12, including for the no-notebook verdict, which reaches it as a 0 (not
   1 = "I hold this door") and therefore takes the report arm exactly as "nobody holds it" does. */
void t_b12a_the_door_drain_is_unchanged_by_the_outage_queue()
{
    /* the verdict still EXISTS and is still distinct - doors.cpp tests it to decide what to journal, and
       DoorHolderWordDecide folds it into kDoorWordStore */
    if (coopdoor::kDoorAreaNoNotebook == 1) Fail("the no-notebook verdict collides with 'this game holds it'");
    if (coopdoor::kDoorAreaNoNotebook == coopdoor::kDoorAreaNobody
        || coopdoor::kDoorAreaNoNotebook == coopdoor::kDoorAreaLoading
        || coopdoor::kDoorAreaNoNotebook == coopdoor::kDoorAreaOther
        || coopdoor::kDoorAreaNoNotebook == coopdoor::kDoorAreaUnanswerable)
        Fail("the no-notebook verdict collides with another area answer");
    if (coopdoor::DoorHolderWordDecide(coopdoor::kDoorAreaNoNotebook) != coopdoor::kDoorWordStore)
        Fail("a game with no notebook no longer stores the holder's word - that is not what B12 changed");
    /* the drain's own answer for a door this game does not hold is a REPORT, notebook or no notebook */
    if (coopdoor::DoorDrainDecide(0, 1) != coopdoor::kDoorSendActor)
        Fail("the actor report is suppressed - a session-link report is not a durable write (decision 47)");
    /* and the four answers that were there are untouched */
    if (coopdoor::DoorDrainDecide(1, 1) != coopdoor::kDoorSendHolder) Fail("the holder arm changed");
    if (coopdoor::DoorDrainDecide(1, 0) != coopdoor::kDoorSendHolder) Fail("the holder's re-offer changed");
    if (coopdoor::DoorDrainDecide(0, 1) != coopdoor::kDoorSendActor) Fail("the actor report changed");
    if (coopdoor::DoorDrainDecide(0, 0) != coopdoor::kDoorSendRefuse) Fail("the re-offer refusal changed");
    if (coopdoor::DoorDrainDecide(-1, 1) != coopdoor::kDoorSendActor) Fail("an unanswerable holder question changed");
    if (coopdoor::DoorDrainDecide(-1, 0) != coopdoor::kDoorSendRefuse) Fail("an unanswerable re-offer changed");
}


/* ================= B12-c (review-b12) =================
   The four decisions the fold added or changed, each as the review stated it. */
void t_b12c_a_zero_sequence_carries_no_information()
{
    if (coopqueue::QueueSeqNoteDecide(0ULL) != coopqueue::kSeqNoteZeroIgnored)
        Fail("a seq of 0 was taken as a fact - the session peer always sends 0 and writing it in resets the cached number");
    if (coopqueue::QueueSeqNoteDecide(1ULL) != coopqueue::kSeqNoteApply) Fail("a real seq of 1 was ignored");
    if (coopqueue::QueueSeqNoteDecide(0xFFFFFFFFFFFFFFFFULL) != coopqueue::kSeqNoteApply) Fail("a real seq at the top of the range was ignored");
    if (coopqueue::kSeqNoteApply == coopqueue::kSeqNoteZeroIgnored) Fail("the two answers are the same value");
    /* AND THE HOLE IT CLOSES (H-1): with the zero written in, the cached number is 0 and this game's own
       next queued change to that key reads as a foreign writer's; ignored, the cached number stands. */
    if (coopqueue::QueueReplayDecide(0ULL, 9ULL, 1, 0, 0) != coopqueue::kDropNewerWriter)
        Fail("a cached seq reset to zero did not drop - the premise of this test is wrong");
    if (coopqueue::QueueReplayDecide(9ULL, 9ULL, 1, 0, 0) != coopqueue::kReplay)
        Fail("with the zero ignored, the entry still lost its replay");
}
/* M-6: an entry left behind one of ITS OWN KEY that replayed is not a foreign conflict. */
void t_b12c_an_entry_behind_this_games_own_replay_is_not_a_foreign_conflict()
{
    /* unmarked, the seq difference reads as somebody else's write - which is what it looks like */
    if (coopqueue::QueueReplayDecide(4ULL, 7ULL, 1, 1, 0) != coopqueue::kDropNewerWriter)
        Fail("an unmarked entry with a higher seq did not drop");
    /* marked AND the record's owner is this game's slot: the difference is this game's own */
    if (coopqueue::QueueReplayDecide(4ULL, 7ULL, 1, 1, 1) != coopqueue::kReplay)
        Fail("an entry left behind this game's own replay was dropped as another writer's");
    /* marked and the owner is somebody else: a real writer got in, and the mark is no licence */
    if (coopqueue::QueueReplayDecide(4ULL, 7ULL, 1, 0, 1) != coopqueue::kDropNewerWriter)
        Fail("the mark let a foreign writer's change be overwritten");
    /* the mark never turns a BACKWARD seq into a replay - that is a notebook that lost history */
    if (coopqueue::QueueReplayDecide(7ULL, 4ULL, 1, 1, 1) != coopqueue::kDropUnknown)
        Fail("the mark turned a backward sequence into a replay");
    /* nor does it change the ordinary equal case, or a key with no record */
    if (coopqueue::QueueReplayDecide(7ULL, 7ULL, 1, 1, 1) != coopqueue::kReplay) Fail("the equal case changed");
    if (coopqueue::QueueReplayDecide(0ULL, 5ULL, 0, 1, 1) != coopqueue::kReplay) Fail("a key with no record changed");
}
/* M-6's other half: the mark travels in the journal line, and a line without it still parses. */
void t_b12c_the_own_ahead_mark_travels_in_the_journal_line()
{
    coopqueue::QueueEntry e;
    e.family = coopqueue::kQueueFamilyBox; e.key = "zone.1.1"; e.seqSeen = 3ULL; e.queuedAtUnix = 7LL;
    std::string plain;
    if (coopqueue::QueueLineFormat(e, &plain) == 0) Fail("an unmarked entry would not format");
    if (plain.find("\t0\n") != std::string::npos) Fail("an unmarked entry wrote a seventh field - absent is what 0 means");
    coopqueue::QueueEntry back;
    if (coopqueue::QueueLineParse(plain, &back) == 0) Fail("a six-field line would not parse");
    if (back.ownAhead != 0) Fail("an absent seventh field did not read back as 0");
    e.ownAhead = 1;
    std::string marked;
    if (coopqueue::QueueLineFormat(e, &marked) == 0) Fail("a marked entry would not format");
    if (coopqueue::QueueLineParse(marked, &back) == 0) Fail("a seven-field line would not parse");
    if (back.ownAhead != 1) Fail("the mark did not survive the round trip");
    if (back.key != e.key || back.seqSeen != e.seqSeen) Fail("the seventh field disturbed the fields before it");
    coopqueue::QueueEntry z;
    if (coopqueue::QueueLineParse("q1\t1\tdoor.a\t2\t3\t\t0", &z) == 0) Fail("an explicit zero in the seventh field would not parse");
    if (z.ownAhead != 0) Fail("an explicit zero did not read back as 0");
}
/* M-1: which families supersede per key, and the cap's two bounds are real numbers. */
void t_b12c_only_state_families_supersede_and_the_cap_is_bounded()
{
    if (coopqueue::QueueFamilySupersedes(coopqueue::kQueueFamilyZone) != 1) Fail("a zone file did not supersede - only its newest state can be right");
    if (coopqueue::QueueFamilySupersedes(coopqueue::kQueueFamilyDoor) != 1) Fail("a door state did not supersede");
    if (coopqueue::QueueFamilySupersedes(coopqueue::kQueueFamilyBox) != 0)
        Fail("a box move superseded - a move is an EVENT (remove eight, then add three) and the second does not describe the first");
    if (coopqueue::kQueueMaxEntries <= 0) Fail("the entry cap is not a bound");
    if (coopqueue::kQueueMaxBytes == 0u) Fail("the byte cap is not a bound");
    if (coopqueue::kQueueMaxEntries > 4096) Fail("the entry cap is large enough that a whole-file rewrite stalls the main thread, which is what the cap is for");
}


/* THE LOAD WAITS STOP WHEN THE WORLD-SERVER LINK CANNOT DELIVER A WELCOME. Connecting, up mid-handshake and welcomed can
   still deliver one (or already did); down / absent and refused in words cannot, so a wait on them is a wait on nothing. */
void t_t962_store_sock_could_arm_only_while_a_welcome_can_come()
{
    if (coopstore::StoreSockCouldArm(coopstore::kSockDownOrFailed) != 0) Fail("a down or absent world-server link read as able to welcome");
    if (coopstore::StoreSockCouldArm(coopstore::kSockUpRefused) != 0) Fail("a refused world-server link read as able to welcome");
    if (coopstore::StoreSockCouldArm(coopstore::kSockConnecting) != 1) Fail("a connecting world-server link read as unable to welcome");
    if (coopstore::StoreSockCouldArm(coopstore::kSockUpNotWelcomed) != 1) Fail("an up, not-yet-welcomed world-server link read as unable to welcome");
    if (coopstore::StoreSockCouldArm(coopstore::kSockUpWelcomed) != 1) Fail("a welcomed world-server link read as unable to welcome");
    if (coopstore::StoreSockCouldArm(-1) != 0 || coopstore::StoreSockCouldArm(5) != 0) Fail("an unknown socket state read as able to welcome");
}

/* B12-e / B12-f (T238, review-b12e). THE LINK WENT DOWN IN THE WORLD AND NOTHING RE-DIALLED IT.
   PRE-PATCH EQUIVALENT: there was no re-dial anywhere outside the title screen, so the answer to every input -
   including the one row this defect is about, a DOWN link with a notebook address configured, a co-op role and a
   running world - was "do nothing", i.e. kRedialIdle. The first assertion below is that row, and it fails on the
   old behaviour. The rest sweeps every arm and every budget boundary.
   ARGUMENT ORDER: sockState, hasServer, roleIsSession, worldRunning, msSinceLast, intervalMs, sinceDialMs,
   connectBudgetMs, welcomeBudgetMs. */
void t_b12e_a_down_link_with_a_server_configured_is_re_dialled()
{
    const int D = coopstore::kSockDownOrFailed, C = coopstore::kSockConnecting;
    const int UN = coopstore::kSockUpNotWelcomed, UW = coopstore::kSockUpWelcomed, UR = coopstore::kSockUpRefused;

    /* THE DEFECT'S OWN ROW. Old answer: idle. */
    if (coopstore::StoreRedialDecide(D, 1, 1, 1, 5000u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialNow)
        Fail("a down link with a notebook configured, a co-op role and a running world did not re-dial");
    if (coopstore::StoreRedialDecide(coopstore::kSockDownOrFailed, 1, 1, 1, 60000u, 5000u, 60000u, 15000u, 15000u) != coopstore::kRedialNow)
        Fail("a link that has been down for a minute did not re-dial");

    /* The exit condition is the LINK STATE, and it wins over a long-overdue interval. */
    if (coopstore::StoreRedialDecide(UW, 1, 1, 1, 999999u, 5000u, 999999u, 15000u, 15000u) != coopstore::kRedialIdle)
        Fail("an UP and WELCOMED link did not read as idle");
    if (coopstore::StoreRedialDecide(UW, 0, 0, 0, 999999u, 5000u, 999999u, 15000u, 15000u) != coopstore::kRedialIdle)
        Fail("an UP and WELCOMED link did not read as idle with nothing else configured either");
    /* B12-f: A REFUSAL IS AN ANSWER. The notebook said no in words; re-dialling it would bounce a live socket. */
    if (coopstore::StoreRedialDecide(UR, 1, 1, 1, 999999u, 5000u, 999999u, 15000u, 15000u) != coopstore::kRedialIdle)
        Fail("a protocol refusal was not left alone");

    /* The three skips, each with its own cause, most permanent first. */
    if (coopstore::StoreRedialDecide(D, 0, 1, 1, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialSkipNoServer)
        Fail("a down link with NO notebook address configured was not skipped as no-server");
    if (coopstore::StoreRedialDecide(D, 1, 0, 1, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialSkipSingle)
        Fail("role=single was not skipped as single");
    if (coopstore::StoreRedialDecide(D, 1, 1, 0, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialSkipNoWorld)
        Fail("a down link with no running world was not skipped as no-world");
    if (coopstore::StoreRedialDecide(D, 0, 0, 0, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialSkipNoServer)
        Fail("the skip order did not put no-server first");
    if (coopstore::StoreRedialDecide(D, 1, 0, 0, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialSkipSingle)
        Fail("the skip order did not put single ahead of no-world");
    /* AND THE SKIPS ARE ASKED FIRST, so a socket mid-connect at the title screen is left standing. */
    if (coopstore::StoreRedialDecide(C, 1, 1, 0, 999999u, 5000u, 999999u, 15000u, 15000u) != coopstore::kRedialSkipNoWorld)
        Fail("a connecting socket with no world was given up on instead of skipped");

    /* B12-f H-3: A CONNECTING SOCKET IS NOT A DOWN ONE. 5 s is shorter than ENet's own connect timeout, so
       these three rows are the difference between re-dialling forever and ever completing a connect. */
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("a connect that had just started was not left alone");
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 999999u, 5000u, 4999u, 15000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("a connect past ONE interval was not left alone");
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 999999u, 5000u, 14999u, 15000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("a connect one millisecond inside its budget was not left alone");
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 999999u, 5000u, 15000u, 15000u, 15000u) != coopstore::kRedialGiveUpConnect)
        Fail("a connect exactly at its budget was not given up on");
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 0u, 5000u, 15001u, 15000u, 15000u) != coopstore::kRedialGiveUpConnect)
        Fail("a connect past its budget was not given up on, whatever the interval said");

    /* B12-f M-1/H-3: UP AND SILENT IS ITS OWN CASE - the far end accepted the socket and sent no WELCOME. */
    if (coopstore::StoreRedialDecide(UN, 1, 1, 1, 999999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("a handshake that had just started was not left alone");
    if (coopstore::StoreRedialDecide(UN, 1, 1, 1, 999999u, 5000u, 14999u, 15000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("a handshake one millisecond inside its budget was not left alone");
    if (coopstore::StoreRedialDecide(UN, 1, 1, 1, 999999u, 5000u, 15000u, 15000u, 15000u) != coopstore::kRedialStalledHandshake)
        Fail("a handshake exactly at its budget was not declared stalled");
    if (coopstore::StoreRedialDecide(UN, 1, 1, 1, 0u, 5000u, 20000u, 15000u, 15000u) != coopstore::kRedialStalledHandshake)
        Fail("a stalled handshake waited for the interval instead of being re-dialled");
    /* The two budgets are separate inputs: a stalled handshake is judged by the WELCOME budget alone. */
    if (coopstore::StoreRedialDecide(UN, 1, 1, 1, 999999u, 5000u, 9000u, 1000u, 15000u) != coopstore::kRedialWaitConnecting)
        Fail("the handshake arm was judged by the CONNECT budget");
    if (coopstore::StoreRedialDecide(C, 1, 1, 1, 999999u, 5000u, 9000u, 15000u, 1000u) != coopstore::kRedialWaitConnecting)
        Fail("the connect arm was judged by the WELCOME budget");

    /* The interval PACES the attempts on a DOWN socket; it never ends them. */
    if (coopstore::StoreRedialDecide(D, 1, 1, 1, 0u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialWait)
        Fail("an attempt in the same millisecond as the last one did not wait");
    if (coopstore::StoreRedialDecide(D, 1, 1, 1, 4999u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialWait)
        Fail("an attempt one millisecond early did not wait");
    if (coopstore::StoreRedialDecide(D, 1, 1, 1, 5001u, 5000u, 0u, 15000u, 15000u) != coopstore::kRedialNow)
        Fail("an attempt one millisecond late did not dial");

    /* NO ATTEMPT CAP. An hour of outage at 5 s is about 720 attempts and the 721st is still a dial - the only
       thing that ever stops it is the link coming up AND being welcomed. */
    {
        int attempts = 0; int tick = 0; int state = D;
        for (tick = 1; tick <= 900; ++tick)
        {
            const int a = coopstore::StoreRedialDecide(state, 1, 1, 1, 5000u, 5000u, 0u, 15000u, 15000u);
            if (a == coopstore::kRedialNow) ++attempts;
            if (state == UW && a != coopstore::kRedialIdle) Fail("the re-dial kept dialling after the link came up and was welcomed");
            if (tick == 800) state = UW;          /* the notebook comes back after 800 intervals */
        }
        if (attempts != 800) Fail("the re-dial did not attempt once per interval for the whole outage");
    }
}


/* ================= B13: WHAT THE NOTEBOOK REMEMBERS ACROSS A RESTART =================
 * T239 (Confirmed): the relay was killed and restarted mid-run. It re-derived every slot number, the whole
 * area map and the authority flag from DIAL ORDER, so the areas and the operator changed hands, and the
 * other game's replayed move was then refused as a collision by the game the relay had just named the
 * writer. Every decision below is the same source the relay compiles; every input here is a literal.
 */
void t_b13_areaclaim_takes_every_arm_including_the_grace_boundary()
{
    const double grace = 60.0, lease = 10.0;

    /* The owner is the one reporting: it is back, and nothing else is asked. */
    if (coopstore::AreaClaimDecide(1, 999.0, 1, grace, lease) != coopstore::kAreaKeep)
        Fail("a reporting owner must KEEP its area whatever it has been silent for (silent 999, reporterIsOwner 1)");
    if (coopstore::AreaClaimDecide(0, 999.0, 1, grace, lease) != coopstore::kAreaKeep)
        Fail("a restored owner that has just reported must KEEP its area (silent 999, reporterIsOwner 1)");

    /* Restored, the owner has NOT dialled back in: the restore grace stands, not the 10 s lease. This is the
       row that fails on the pre-B13 behaviour, where every area was 'first there' at the restart. */
    if (coopstore::AreaClaimDecide(0, 30.0, 0, grace, lease) != coopstore::kAreaKeep)
        Fail("a restored claim 30 s into a 60 s grace must KEEP even though another game is reporting it");
    if (coopstore::AreaClaimDecide(0, 60.0, 0, grace, lease) != coopstore::kAreaKeep)
        Fail("the grace BOUNDARY itself keeps: a claim is lost PAST the grace, not AT it (silent 60, grace 60)");
    if (coopstore::AreaClaimDecide(0, 60.001, 0, grace, lease) != coopstore::kAreaTransfer)
        Fail("past the restore grace with another game reporting, the area must TRANSFER (silent 60.001, grace 60)");
    if (coopstore::AreaClaimDecide(0, 60.001, -1, grace, lease) != coopstore::kAreaRelease)
        Fail("past the restore grace with NOBODY reporting, the area must be RELEASED (silent 60.001, grace 60)");

    /* The owner is live again: the ordinary 10 s silence lease applies and the 60 s grace is over with. */
    if (coopstore::AreaClaimDecide(1, 9.5, 0, grace, lease) != coopstore::kAreaKeep)
        Fail("a live owner silent 9.5 s of a 10 s lease must KEEP");
    if (coopstore::AreaClaimDecide(1, 10.0, 0, grace, lease) != coopstore::kAreaKeep)
        Fail("the lease BOUNDARY itself keeps (silent 10, lease 10)");
    if (coopstore::AreaClaimDecide(1, 10.5, 0, grace, lease) != coopstore::kAreaTransfer)
        Fail("a live owner silent past its 10 s lease loses the area to a current reporter (silent 10.5)");
    if (coopstore::AreaClaimDecide(1, 10.5, -1, grace, lease) != coopstore::kAreaRelease)
        Fail("a live owner silent past its lease with nobody reporting releases the area (silent 10.5)");
    if (coopstore::AreaClaimDecide(1, 45.0, 0, grace, lease) != coopstore::kAreaTransfer)
        Fail("a LIVE owner does not get the restore grace: 45 s of silence is past its 10 s lease");
}

/* An area the map names for a player who is not in the world is FROZEN on every other game: nothing is created, adopted, woken,
   loaded from a record, written or removed there. The shape of the live report: slot 1's restored claim on the town's area stood
   while slot 1 re-dialled, and the other game removed its people there in favour of slot 1's world. Before the change the five
   deciders below answered -2 as unclaimed (create, load, wake) - the first refusal checks fail on that. */
void t_t592_frozen_area()
{
    { int path = -1; if (coopwriter::ZoneWriterDecide(0, -2, -2, 1, 0, 1, 1, 0, 1000, &path, 0, 0) != coopwriter::kWrHeld || path != coopwriter::kZonePathHolderOther) Fail("a frozen area's zone file is not written here (its holder's file stands)"); }
    using coopdrop::AreaFrozenDecide;
    if (AreaFrozenDecide(1, 0, 1, 0) != 1) Fail("another slot named, the roster shows it not in the world: frozen");
    if (AreaFrozenDecide(1, 0, 1, 1) != 0) Fail("another slot named and in the world: not frozen (held by it)");
    if (AreaFrozenDecide(1, 0, 0, 0) != 1 || AreaFrozenDecide(1, 0, 0, 1) != 1) Fail("no roster on this link yet: frozen for every other-slot owner");
    if (AreaFrozenDecide(0, 0, 0, 0) != 0 || AreaFrozenDecide(0, 0, 1, 0) != 0) Fail("my own slot is never frozen");
    if (AreaFrozenDecide(-1, 0, 0, 0) != 0 || AreaFrozenDecide(-1, 0, 1, 1) != 0) Fail("a row naming nobody is not frozen");
    if (AreaFrozenDecide(0, -1, 1, 0) != 1 || AreaFrozenDecide(0, -1, 1, 1) != 0) Fail("no slot of my own: slot 0 is another player like any other");
    if (coopdrop::kAreaFrozen != -2) Fail("frozen is -2: below 0, so every '== 1', '!= 1' and '< 0' test reads it as no answer");
    /* the deciders refuse on frozen; their -1 answers are unchanged */
    if (coopor::AnnounceArea(-2) != coopor::kAnnounceNoMap) Fail("announce sweep, frozen area: no adoption");
    { coopor::Seen s = coopor::SeenNone(); s.areaOther = -2; int why = -1; if (coopor::Decide(s, &why) != coopor::kActNone) Fail("orphan clean-up, frozen area: nobody removed"); }
    if (coopsquad::NpcWakeSkipLoad(1, -2) != 1) Fail("wake in a frozen area: the record is not loaded");
    if (coopsquad::NpcWakeSkipLoad(1, -1) != 0) Fail("wake with no fresh map: the record applies as before");
    if (coopsquad::NpcWakeSkipLoad(0, -2) != 0) Fail("not a world NPC squad: the record applies");
    if (coopsquad::HandedOverWakeRefuse(1, -2, 1) != 1) Fail("a handed-over world squad in a frozen area: its wake is refused");
    if (coopsquad::HandedOverWakeRefuse(1, -2, 0) != 0) Fail("a player / peer / stand-in squad: never refused");
    if (coopsquad::HandedOverWakeRefuse(0, -2, 1) != 0) Fail("not handed over: woken");
    if (coopsquad::HandedOverWakeRefuse(1, -1, 1) != 0) Fail("no fresh map: woken as before");
    if (coopsquad::HandedOverSweepAction(1, 0, -2) != coopsquad::kHoSweepSkip) Fail("handed-over sweep, frozen area: not adopted");
    if (coopsquad::OwnBlockRecvHold(0, 1, 0, -2, 1) != 1) Fail("another game's record of my squad in a frozen area: left asleep");
    if (coopsquad::OwnBlockRecvHold(0, 1, 0, -2, 0) != 0) Fail("not a world faction's squad: the ordinary path");
    if (coopsquad::OwnBlockRecvHold(0, 0, 0, -2, 1) != 0) Fail("my own record: the ordinary path");
    if (coopsquad::OwnBlockRecvHold(0, 1, 0, -1, 1) != 0) Fail("no fresh map: the ordinary path as before");
    if (coopsquad::NpcSquadWriter(1, 1, -2, -2, 0, 0, 0) != coopsquad::kNpcWriteRefuseStray || coopsquad::NpcSquadWriter(1, 1, -2, -2, 0, 2, 0) != coopsquad::kNpcWriteRefuseStray)
        Fail("frozen area, none of the people is mine: never written here");
    if (coopsquad::NpcSquadWriter(1, 1, -2, -2, 3, 0, 0) != coopsquad::kNpcWriteToday) Fail("frozen area, I run some of the people: the block and member rules decide");
    if (coopsquad::NpcSquadWriter(1, 1, -1, -1, 0, 0, 0) != coopsquad::kNpcWriteToday) Fail("no fresh map, none mine: today's rules as before");
}

/* A restored holder that says LOADING releases its restored claims, and nothing else does. */
void t_restored_claim_released_when_its_holder_loads()
{
    if (coopstore::RestoredClaimOnStageDecide(1, 1, 1) != coopstore::kAreaRelease)
        Fail("a restored claim whose holder says LOADING must be RELEASED");
    if (coopstore::RestoredClaimOnStageDecide(1, 1, 0) != coopstore::kAreaKeep)
        Fail("a restored claim whose holder says IN_WORLD (a re-dial with its world running) must be KEPT");
    if (coopstore::RestoredClaimOnStageDecide(0, 1, 1) != coopstore::kAreaKeep)
        Fail("another player's LOADING must not release this row");
    if (coopstore::RestoredClaimOnStageDecide(1, 0, 1) != coopstore::kAreaKeep)
        Fail("a live (not restored) claim is left to the lease, not released at a LOADING");
}

void t_b13_the_three_file_lines_round_trip_and_a_corrupt_line_is_dropped()
{
    const std::string idA = "0123456789abcdef0123456789abcdef";
    const std::string idB = "fedcba9876543210fedcba9876543210";

    {
        std::string id; int slot = -9; long long seen = -9;
        if (!coopstore::SlotsLineParse(coopstore::SlotsLineFormat(idA, 3, 1758200000LL), &id, &slot, &seen))
            Fail("a slots.txt line this build WROTE did not parse: " + coopstore::SlotsLineFormat(idA, 3, 1758200000LL));
        else if (id != idA || slot != 3 || seen != 1758200000LL)
            Fail("slots.txt round trip changed its fields: id '" + id + "' slot " + I(slot) + " lastSeen " + D((double)seen));
        if (coopstore::SlotsLineParse("v1\tNOTHEX\t3\t1", &id, &slot, &seen))
            Fail("a slots.txt line whose id is not 32 hex characters must be DROPPED, not guessed at");
        if (coopstore::SlotsLineParse("v1\t" + idA + "\t65520\t1", &id, &slot, &seen))
            Fail("a slots.txt line whose slot is outside 0..65519 (M1: the lifetime limit) must be DROPPED");
        if (!coopstore::SlotsLineParse("v1\t" + idA + "\t65519\t1", &id, &slot, &seen) || slot != 65519)
            Fail("M1: a slots.txt line holding the last lifetime number, 65519, must be READ");
        if (coopstore::SlotsLineParse("v2\t" + idA + "\t3\t1", &id, &slot, &seen))
            Fail("a slots.txt line with an unknown version must be DROPPED");
        if (coopstore::SlotsLineParse("v1\t" + idA + "\t3", &id, &slot, &seen))
            Fail("a TRUNCATED slots.txt line (three fields) must be DROPPED");
    }
    {
        int x = -9, y = -9; std::string owner; long long at = -9;
        if (!coopstore::AreasLineParse(coopstore::AreasLineFormat(43, 11, idB, 1758200123LL), &x, &y, &owner, &at))
            Fail("an areas.txt line this build WROTE did not parse: " + coopstore::AreasLineFormat(43, 11, idB, 1758200123LL));
        else if (x != 43 || y != 11 || owner != idB || at != 1758200123LL)
            Fail("areas.txt round trip changed its fields: " + I(x) + "," + I(y) + " owner '" + owner + "'");
        if (coopstore::AreasLineParse("v1\t43,64\t" + idB + "\t1", &x, &y, &owner, &at))
            Fail("an areas.txt line outside the 64x64 grid must be DROPPED");
        if (coopstore::AreasLineParse("v1\t4311\t" + idB + "\t1", &x, &y, &owner, &at))
            Fail("an areas.txt sector with no comma must be DROPPED, not read as one number");
        if (coopstore::AreasLineParse("v1\t43,11\tshort\t1", &x, &y, &owner, &at))
            Fail("an areas.txt line whose owner is not a player id must be DROPPED");
    }
    {
        std::string owner; int src = -9;
        if (!coopstore::OwnerLineParse(coopstore::OwnerLineFormat(idA, coopstore::kOwnerSourceArg), &owner, &src))
            Fail("an owner.txt line this build WROTE did not parse");
        else if (owner != idA || src != coopstore::kOwnerSourceRestored)
            Fail("owner.txt must come back as RESTORED whatever source wrote it - this run learned it from the file");
        if (coopstore::OwnerLineParse("v1\t\targ", &owner, &src))
            Fail("an owner.txt line with an empty id must be DROPPED");
    }
    /* W3-f (review-w3 item 2): a world with no operator appoints one at a game's first message AFTER its WELCOME, never at a HELLO */
    if (!coopstore::OwnerAppointOnMessage(false, true, false)) Fail("the first message after the WELCOME appoints the operator");
    if (coopstore::OwnerAppointOnMessage(false, true, true))   Fail("a HELLO never appoints the operator");
    if (coopstore::OwnerAppointOnMessage(false, false, false)) Fail("a game that was not let in never appoints the operator");
    if (coopstore::OwnerAppointOnMessage(true, true, false))   Fail("a world with an operator never appoints another");
    if (coopstore::PlayerIdOk("0123456789ABCDEF0123456789ABCDEF"))
        Fail("an upper-case id must be refused: one id may not have two spellings, or one player would hold two slots");
}

void t_b13_a_known_id_gets_its_old_slot_even_when_a_new_id_dialled_first()
{
    const std::string a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const std::string c = "cccccccccccccccccccccccccccccccc";
    std::map<std::string, int> known;
    known[a] = 0;
    known[b] = 1;

    /* This is T239 exactly: after the restart B dialled FIRST. Under AssignSlot's smallest-free rule it took
       0 - A's number - and RECORD.owner therefore renamed every record A had ever written. */
    if (coopstore::SlotAssignDecide(known, b) != 1)
        Fail("the second player dialling FIRST must still get slot 1, the number it already held");
    if (coopstore::SlotAssignDecide(known, a) != 0)
        Fail("the first player dialling SECOND must still get slot 0");
    if (coopstore::SlotAssignDecide(known, c) != 2)
        Fail("a NEW id gets the smallest number no KNOWN id holds - 2, not 0, even though nobody is connected on 0");
    {
        std::map<std::string, int> sparse;
        sparse[a] = 1;
        if (coopstore::SlotAssignDecide(sparse, c) != 0)
            Fail("a gap left by no known id is the smallest free number: c must take 0 when only slot 1 is held");
    }
    {
        std::map<std::string, int> empty;
        if (coopstore::SlotAssignDecide(empty, a) != 0)
            Fail("the first id this world has ever seen takes slot 0");
    }
}

void t_b13_the_playerid_survives_a_cfg_round_trip_and_a_v41_game_is_refused()
{
    coopcfg::CfgFields f;
    f.role = coopcfg::kCfgHost;
    f.hostAddr = "0.0.0.0"; f.hostPort = 27015;
    f.storeAddr = "127.0.0.1"; f.storePort = 27016;
    f.slot = "s1"; f.world = "t102";
    f.playerId = "0123456789abcdef0123456789abcdef";

    coopcfg::CfgFields back;
    long long unknown = -1, bad = -1;
    coopcfg::CfgParseText(coopcfg::CfgFormat(f), &back, 0, 0, &unknown, &bad);
    if (back.playerId != f.playerId)
        Fail("the playerId did not survive CfgFormat -> CfgParseText: wrote '" + f.playerId + "', read '" + back.playerId + "'");
    if (unknown != 0)
        Fail("the file this build writes must have NO unknown keys in it - playerid counted as one (" + D((double)unknown) + ")");
    if (bad != 0)
        Fail("the file this build writes must have no bad lines in it");
    if (back.slot != f.slot || back.world != f.world || back.storePort != f.storePort)
        Fail("adding playerid to the settings file changed what the OTHER keys round trip to");

    /* A game one protocol behind carries no id at all, so the notebook cannot name it - and the protocol gate
       is what refuses it, in words, before it is given a slot or put in the connection order. */
    if (coopstore::StoreHandshakeDecide(42u, 41u) != coopstore::kHandshakeRefuse)
        Fail("a v41 game (no playerId in its HELLO) must be REFUSED by a v42 notebook");
    if (coopstore::StoreHandshakeDecide(42u, 42u) != coopstore::kHandshakeAccept)
        Fail("two v42 ends must accept each other");
    if (coopstore::StoreHandshakeRefusalText(42u, 41u) != "store protocol 42, game speaks 41")
        Fail("the refusal must name both numbers: '" + coopstore::StoreHandshakeRefusalText(42u, 41u) + "'");
}

/* B13-c (review-b13-b Q2, Read 2026-09-18): the same player back on a new socket must not be refused as a copy
   of itself. The old connection's silence decides; the boundary is live. */
void t_b13c_a_silent_old_connection_on_the_same_id_is_evicted_and_a_live_one_refuses()
{
    const double live = coopstore::kDupIdLiveSec;
    if (coopstore::DuplicateIdDecide(0.0, live) != coopstore::kDupIdRefuse)
        Fail("an old connection heard from just now is live: the newcomer is refused");
    if (coopstore::DuplicateIdDecide(live, live) != coopstore::kDupIdRefuse)
        Fail("the boundary itself is live - a connection heard from within the window stands");
    if (coopstore::DuplicateIdDecide(-1.0, live) != coopstore::kDupIdRefuse)
        Fail("a negative silence (clock skew) reads as live, never as abandoned");
    /* FAILS BEFORE B13-c: the notebook refused every duplicate, however long the old socket had been dead. */
    if (coopstore::DuplicateIdDecide(live + 0.001, live) != coopstore::kDupIdEvictOld)
        Fail("an old connection silent past the bound was abandoned: it is evicted and the newcomer takes the id");
    if (coopstore::DuplicateIdDecide(30.0, live) != coopstore::kDupIdEvictOld)
        Fail("thirty seconds of silence - ENet's own outer timeout - is abandoned");
    if (live < 2.0 || live > 5.0)
        Fail("the live bound must sit above one ping round and one clock report and under ENet's 5 s minimum timeout");
}

/* link1 (T416, Confirmed in the run: ~5.35 s with no ENet service on either game dropped B's game link on its first
   world frame). The timeout terms, the backoff schedule and who may re-dial. */
void t_link1_the_game_link_outlasts_a_load_and_a_joiner_redials_with_backoff()
{
    /* FAILS BEFORE link1: no enet_peer_timeout call, so ENet's 5000 ms minimum was the binding term. */
    if (coopgl::kGameLinkTimeoutMinimumMs < 5350u * 4u)
        Fail("the game link's timeout minimum must sit well above T416's 5.35 s load overlap");
    if (coopgl::kGameLinkTimeoutMaximumMs <= coopgl::kGameLinkTimeoutMinimumMs)
        Fail("ENet's maximum must be above its minimum, or the minimum never binds");
    if (coopgl::kGameLinkTimeoutLimit != 32u)
        Fail("the retry limit stays ENet's own 32");
    const unsigned int want[7] = { 2000u, 4000u, 8000u, 16000u, 30000u, 30000u, 30000u };
    for (unsigned int i = 0; i < 7; ++i)
        if (coopgl::GameRedialBackoffMs(i) != want[i])
            Fail("the re-dial backoff must be 2, 4, 8, 16 s then capped at 30 s");
    if (coopgl::GameRedialBackoffMs(4000000000u) != 30000u)
        Fail("a huge attempt count stays at the 30 s cap (no overflow)");
    using namespace coopgl;
    /* who never dials: no session (after `leave`, a refusal, quit), the host, a joiner with no address */
    if (GameRedialDecide(0, 0, 1, kGlSockDown, 1, 999999u, 0) != kGameRedialNotJoiner)
        Fail("a game that LEFT (no session) must never re-dial");
    if (GameRedialDecide(1, 1, 0, kGlSockDown, 1, 999999u, 0) != kGameRedialNotJoiner)
        Fail("the host never dials");
    if (GameRedialDecide(1, 0, 0, kGlSockDown, 1, 999999u, 0) != kGameRedialNotJoiner)
        Fail("no host address, nothing to dial");
    if (GameRedialDecide(1, 0, 1, kGlSockUp, 1, 999999u, 3) != kGameRedialIdle)
        Fail("an UP link is left alone");
    if (GameRedialDecide(1, 0, 1, kGlSockConnecting, 1, 999999u, 3) != kGameRedialWaitConnecting)
        Fail("a dial in flight is left alone");
    /* never at the title screen or during a load */
    if (GameRedialDecide(1, 0, 1, kGlSockDown, 0, 999999u, 0) != kGameRedialSkipNoWorld)
        Fail("no running world (title screen / a load): never dial");
    if (GameRedialDecide(1, 0, 1, kGlSockDown, 1, 1999u, 0) != kGameRedialWait)
        Fail("the first attempt waits 2 s after the drop");
    if (GameRedialDecide(1, 0, 1, kGlSockDown, 1, 2000u, 0) != kGameRedialNow)
        Fail("at 2 s the first attempt dials");
    if (GameRedialDecide(1, 0, 1, kGlSockDown, 1, 29999u, 4) != kGameRedialWait)
        Fail("the fifth attempt waits the full 30 s");
    if (GameRedialDecide(1, 0, 1, kGlSockDown, 1, 30000u, 9) != kGameRedialNow)
        Fail("past the cap every attempt is 30 s apart");
}

/* M1 (decisions 54 / 56(b), design-many M1): the notebook's two named limits. */
std::string M1Id(int i) { char b[40]; std::sprintf(b, "%032x", (unsigned int)i); return b; }

void t_m1_the_notebooks_named_limits_are_refused_in_words()
{
    /* FAILS BEFORE: kSlotMax was 1024, so the 1,025th player a world ever named was answered -1 ("given none"). */
    {
        std::map<std::string, int> known;
        for (int i = 0; i < 1024; ++i) known[M1Id(i)] = i;
        const int s = coopstore::SlotAssignDecide(known, M1Id(99999));
        if (s != 1024) Fail("M1: with 1,024 known ids a new id must get 1024, got " + coopstore::AcNum((long long)s));
    }
    /* A world that has admitted its lifetime limit answers -1 for a new id, quickly, and a KNOWN id still gets its
       own number - the limit refuses newcomers, never a returning player. */
    {
        std::map<std::string, int> full; std::set<int> used;
        for (int i = 0; i < coopstore::kSlotLifetimeMax; ++i) { full[M1Id(i)] = i; used.insert(i); }
        const std::clock_t t0 = std::clock();
        if (coopstore::SlotAssignDecide(full, used, M1Id(70000), 0) != -1)
            Fail("M1: a new id in a world holding all 65,520 numbers must be answered -1");
        if (coopstore::SlotAssignDecide(full, M1Id(70000)) != -1)
            Fail("M1: the map-only form must agree - -1 for a new id in a full world");
        if (coopstore::SlotAssignDecide(full, used, M1Id(5), 0) != 5)
            Fail("M1: a KNOWN id keeps its number even when the world is full");
        const double sec = (double)(std::clock() - t0) / (double)CLOCKS_PER_SEC;
        if (sec > 1.0) Fail("M1: the full-world answer took more than a second");
    }
    /* --first-slot: new ids start at the floor; a gap BELOW it is not used; a known id is not moved by it. */
    {
        const std::string a = M1Id(1), b = M1Id(2), c = M1Id(3);
        std::map<std::string, int> known;
        if (coopstore::SlotAssignDecide(known, a, 40000) != 40000) Fail("M1: the first new id must take the floor, 40000");
        known[a] = 40000;
        if (coopstore::SlotAssignDecide(known, b, 40000) != 40001) Fail("M1: the second new id must take 40001");
        known[c] = 3;
        if (coopstore::SlotAssignDecide(known, c, 40000) != 3) Fail("M1: a known id keeps its number below the floor");
        std::set<int> used; used.insert(40000); used.insert(3);
        if (coopstore::SlotAssignDecide(known, used, b, 40000) != 40001) Fail("M1: the set form must honour the floor too");
        if (coopstore::SlotAssignDecide(known, used, b, coopstore::kSlotLifetimeMax) != -1)
            Fail("M1: a floor at the lifetime limit leaves no number to give");
    }
    if (coopstore::ConnectAdmitDecide(256, 256) != coopstore::kConnectRefuseFull) Fail("M1: 256 connected of 256 must refuse the next game");
    if (coopstore::ConnectAdmitDecide(255, 256) != coopstore::kConnectAdmit)      Fail("M1: 255 connected of 256 must admit the next game");
    if (coopstore::ConnectAdmitDecide(0, 1) != coopstore::kConnectAdmit)          Fail("M1: --max-connected 1 admits the first game");
    if (coopstore::ConnectAdmitDecide(1, 1) != coopstore::kConnectRefuseFull)     Fail("M1: --max-connected 1 refuses the second");
    {
        const std::string full = coopstore::StoreRefuseReasonText(coopstore::kRefuseFull, 43u, 43u, 256u, 256u);
        const std::string life = coopstore::StoreRefuseReasonText(coopstore::kRefuseLifetimeFull, 43u, 43u, 65520u);
        const std::string proto = coopstore::StoreRefuseReasonText(coopstore::kRefuseProtocol, 43u, 42u);
        if (full.find("full") == std::string::npos || full.find("256 of 256") == std::string::npos)
            Fail("M1: the full refusal must say the world is full and name the numbers: '" + full + "'");
        if (coopstore::StoreRefuseReasonText(coopstore::kRefuseFull, 43u, 43u, 1u, 1u).find("1 of 1") == std::string::npos)
            Fail("M1: the full refusal must carry the numbers it was sent");
        if (life.find("65,520") == std::string::npos)
            Fail("M1: the lifetime refusal must name the limit, 65,520: '" + life + "'");
        if (full == proto || life == proto || full == life)
            Fail("M1: each limit refusal must have its OWN sentence, not the protocol one");
        if (full.find("deployed") != std::string::npos || life.find("deployed") != std::string::npos)
            Fail("M1: only the protocol refusal may mention deploying");
    }
}

void t_b13b_a_refusal_names_its_reason_and_only_the_protocol_one_mentions_deploying()
{
    const std::string proto = coopstore::StoreRefuseReasonText(coopstore::kRefuseProtocol, 42u, 41u);
    const std::string noId  = coopstore::StoreRefuseReasonText(coopstore::kRefuseNoPlayerId, 42u, 42u);
    const std::string dup   = coopstore::StoreRefuseReasonText(coopstore::kRefuseDuplicateId, 42u, 42u);

    if (proto != "store protocol 42, game speaks 41")
        Fail("the protocol refusal must still be the two numbers and nothing else: '" + proto + "'");
    if (noId == proto || dup == proto || noId == dup)
        Fail("each refusal reason must have its OWN sentence - B13 sent all three down the protocol one, which told"
             " a player with a copied settings file to rebuild the mod");
    if (noId.find("player id") == std::string::npos)
        Fail("the no-id refusal must say what is missing: '" + noId + "'");
    if (dup.find("already in the world") == std::string::npos)
        Fail("the duplicate-id refusal must say the id is already here: '" + dup + "'");

    /* An unknown reason from a newer notebook reads as the protocol refusal - the oldest meaning of this
       message, and the only thing an older end can have meant by it. */
    if (coopstore::StoreRefuseReasonText(99u, 42u, 41u) != proto)
        Fail("an unknown reason code must fall back to the protocol refusal");
}

/* ================= W1-b: THE EFFECTIVE AREA MAP (T240; review 2026-09-22 H2/H3) =================
 * The pre-patch rule, written as its equivalent so "fails before" can be checked by hand: eff = newMask - the
 * map's own mask, with no reference to the previous effective mask and none to whether that reporter appeared
 * anywhere in the map at all. The first test is exactly the input T240 hit (a reporter absent from the whole
 * map) and is the one that FAILS BEFORE. Every input here is a literal mask; no test's precondition is computed
 * by the function it is checking. */

void t_w1b_an_absent_reporter_keeps_its_areas()
{
    const unsigned kMine = 1u << 0;   /* my slot is 0 */
    const unsigned kPeer = 1u << 1;   /* the peer's slot is 1 */
    /* T240's map: last second the peer held this sector; this second the notebook has restarted and its map
       carries MY bits only, so the peer's bit is clear in every sector and clear in `present` as well. */
    const unsigned masks[3] = { kMine, kMine, kMine };
    const unsigned present = coopdrop::AreaDropReportersPresent(masks, 3);
    if (present != kMine)
        Fail("the map lists only my own bit, so only my own bit is present (read " + I((int)present) + ")");
    /* FAILS BEFORE: eff = newMask read the peer's bit clear, g_otherLoaded went to 0 and the withdraw leg sent the
       UNLOAD - 241 of them in T240, followed by 241 re-spawns one second later when the peer re-linked. */
    if (coopdrop::AreaEffectiveMask(kMine | kPeer, kMine, present) != (kMine | kPeer))
        Fail("a reporter absent from the whole map keeps its last known bit");
    /* A sector the map omits entirely (new mask 0) keeps the absent peer's bit too. */
    if (coopdrop::AreaEffectiveMask(kPeer, 0u, present) != kPeer)
        Fail("an absent reporter keeps a sector the map does not list at all");
    /* An empty map (nobody present) carries everything - it is no map, not a map of nothing. */
    if (coopdrop::AreaEffectiveMask(kPeer | kMine, 0u, coopdrop::AreaDropReportersPresent(0, 0)) != (kPeer | kMine))
        Fail("with nobody present, every bit is carried");
}

void t_w1b_a_present_reporter_is_taken_exactly()
{
    const unsigned kMine = 1u << 0;
    const unsigned kPeer = 1u << 1;
    const unsigned kThird = 1u << 5;
    /* The peer is present (it lists another sector), and it no longer lists this one: its bit is CLEARED - the
       self-healing the review found missing (H3): a character walking out of a peer-held sector is withdrawn. */
    const unsigned masks[2] = { kMine, kPeer };
    const unsigned present = coopdrop::AreaDropReportersPresent(masks, 2);
    if (present != (kMine | kPeer))
        Fail("both slots listed something in this map (read " + I((int)present) + ")");
    if (coopdrop::AreaEffectiveMask(kMine | kPeer, kMine, present) != kMine)
        Fail("a present reporter that stopped listing a sector no longer holds it");
    /* A new bit from a present reporter is set, whatever the previous effective mask said. */
    if (coopdrop::AreaEffectiveMask(0u, kPeer, present) != kPeer)
        Fail("a present reporter that newly lists a sector holds it");
    /* The first map after a forget (prev 0) is the map itself. */
    if (coopdrop::AreaEffectiveMask(0u, kMine | kPeer, present) != (kMine | kPeer))
        Fail("with nothing carried, the effective mask is the map's own mask");
    /* SEVERAL SLOTS AT ONCE, each judged on its own: the peer and the third slot both held it and both are gone
       from this sector's mask, but only the peer is present - so the peer's bit is cleared and the third slot's
       bit is carried; my own bit, present and still listed, stays. */
    if (coopdrop::AreaEffectiveMask(kMine | kPeer | kThird, kMine, present) != (kMine | kThird))
        Fail("the present peer is taken exactly and the absent third slot keeps its bit");
    /* And once the third slot speaks again without this sector, its bit goes too. */
    if (coopdrop::AreaEffectiveMask(kMine | kThird, kMine, present | kThird) != kMine)
        Fail("a returning reporter is taken exactly again");
}

/* P1 (read-parity3 GAP 1; user saw it T244). The SPAWN payload carries the owner's age (CharacterAnimal
   +0x700) as a trailing f32. It must round-trip bit-for-bit, and a pre-45 payload (everything up to the
   keepContainer byte, no age) must still decode, with age 0.0 and ageAbsent set so OnSpawn counts it. */
void t_p1_spawn_round_trips_the_age_and_an_old_payload_reads_0()
{
    std::vector<char> b;
    coopspawn::EncodeSpawn(&b, 3204448269u, 1.5f, -2.25f, 300.0f, "Bonedog", "Bounty Hunters", false, 0.625f);
    if (b.size() != (size_t)(20 + 7 + 4 + 14 + 1 + 4)) Fail("the encoded SPAWN is " + I((int)b.size()) + " bytes, not 50");
    coopspawn::SpawnMsg m;
    if (coopspawn::DecodeSpawn(&b[0], b.size(), &m) != coopspawn::kSpawnDecodeOk) { Fail("a whole SPAWN did not decode"); return; }
    if (m.uid != 3204448269u || m.x != 1.5f || m.y != -2.25f || m.z != 300.0f) Fail("uid / position did not round-trip");
    if (m.templateName != "Bonedog" || m.factionName != "Bounty Hunters" || m.keepContainer) Fail("template / faction / keepContainer did not round-trip");
    if (m.age != 0.625f || m.ageAbsent || m.ageClamped) Fail("the age did not round-trip as 0.625, present, unclamped");

    /* Every age a real animal can have survives exactly, including both ends. */
    const float ages[4] = { 0.0f, 1.0f, 0.41f, 1e-7f };
    int i;
    for (i = 0; i < 4; ++i)
    {
        std::vector<char> c;
        coopspawn::EncodeSpawn(&c, 7u, 0, 0, 0, "Goat", "", true, ages[i]);
        coopspawn::SpawnMsg n;
        if (coopspawn::DecodeSpawn(&c[0], c.size(), &n) != coopspawn::kSpawnDecodeOk || n.age != ages[i] || n.ageClamped || !n.keepContainer || !n.factionName.empty())
            Fail("age case " + I(i) + " did not round-trip");
    }

    /* A 44 sender's payload: drop the 4 trailing bytes. */
    std::vector<char> old(b.begin(), b.end() - 4);
    coopspawn::SpawnMsg o;
    if (coopspawn::DecodeSpawn(&old[0], old.size(), &o) != coopspawn::kSpawnDecodeOk) { Fail("a pre-45 SPAWN (no age) was refused"); return; }
    if (o.age != 0.0f || !o.ageAbsent || o.ageClamped) Fail("a pre-45 SPAWN must read age 0.0 with ageAbsent");
    if (o.templateName != "Bonedog" || o.factionName != "Bounty Hunters") Fail("a pre-45 SPAWN lost its template / faction");
    /* One to three stray bytes are not an age either. */
    std::vector<char> part(b.begin(), b.end() - 1);
    if (coopspawn::DecodeSpawn(&part[0], part.size(), &o) != coopspawn::kSpawnDecodeOk || !o.ageAbsent || o.age != 0.0f)
        Fail("a SPAWN with a 3-byte tail must read as no age");
}

void t_p1_a_bad_age_is_clamped_and_a_truncated_spawn_is_refused()
{
    unsigned int nanBits = 0x7FC00000u, infBits = 0x7F800000u, ninfBits = 0xFF800000u;
    float nanV, infV, ninfV;
    std::memcpy(&nanV, &nanBits, 4); std::memcpy(&infV, &infBits, 4); std::memcpy(&ninfV, &ninfBits, 4);
    const float bad[5]  = { nanV, infV, ninfV, -0.5f, 3.0f };
    const float want[5] = { 0.0f, 0.0f, 0.0f,  0.0f,  1.0f };
    int i;
    for (i = 0; i < 5; ++i)
    {
        std::vector<char> c;
        coopspawn::EncodeSpawn(&c, 9u, 0, 0, 0, "Beak Thing", "", true, bad[i]);
        coopspawn::SpawnMsg n;
        if (coopspawn::DecodeSpawn(&c[0], c.size(), &n) != coopspawn::kSpawnDecodeOk) { Fail("bad-age case " + I(i) + " was refused"); continue; }
        if (n.age != want[i] || !n.ageClamped || n.ageAbsent) Fail("bad-age case " + I(i) + " was not clamped to " + (want[i] == 0.0f ? "0" : "1"));
    }
    /* The pre-existing refusals are kept: too short, name, modifier block, faction. */
    std::vector<char> b;
    coopspawn::EncodeSpawn(&b, 1u, 0, 0, 0, "Bonedog", "Hivers", true, 0.5f);
    coopspawn::SpawnMsg m;
    if (coopspawn::DecodeSpawn(&b[0], 19, &m) != coopspawn::kSpawnDecodeTooShort) Fail("19 bytes must be too short");
    if (coopspawn::DecodeSpawn(&b[0], 26, &m) != coopspawn::kSpawnDecodeNameTrunc) Fail("a cut name must be refused");
    if (coopspawn::DecodeSpawn(&b[0], 29, &m) != coopspawn::kSpawnDecodeNoModifier) Fail("a missing faction length must be refused");
    if (coopspawn::DecodeSpawn(&b[0], 37, &m) != coopspawn::kSpawnDecodeFactionTrunc) Fail("a SPAWN without its keepContainer byte must be refused");
    /* A hostile name length cannot wrap the bound. */
    std::vector<char> h(b.begin(), b.begin() + 20);
    unsigned int huge = 0xFFFFFFF0u; std::memcpy(&h[16], &huge, 4);
    if (coopspawn::DecodeSpawn(&h[0], h.size(), &m) != coopspawn::kSpawnDecodeNameTrunc) Fail("a hostile name length must be refused");
    if (coopspawn::DecodeSpawn(0, 0, &m) != coopspawn::kSpawnDecodeTooShort) Fail("an empty payload must be too short");

    /* T-303 (protocol 102): the owner's flags byte after the stats tail - with no block, with a block, absent, unknown bits. */
    {
        std::vector<char> f;
        coopspawn::EncodeSpawn(&f, 5u, 1.0f, 2.0f, 3.0f, "Wanderer", "", false, 0.0f);
        coopspawn::AppendSpawnStats(&f, 0);
        coopspawn::AppendSpawnFlags(&f, coopspawn::kSpawnFlagDead);
        coopspawn::SpawnMsg d;
        if (coopspawn::DecodeSpawn(&f[0], f.size(), &d) != coopspawn::kSpawnDecodeOk || d.flagsAbsent
            || d.ownerFlags != coopspawn::kSpawnFlagDead || d.statsState != coopspawn::kSpawnStatsNone)
            Fail("T-303: a dead SPAWN without a stats block did not read dead");
        unsigned int raw[coopstats::kStatsCount];
        for (int i = 0; i < coopstats::kStatsCount; ++i) raw[i] = 0x3F800000u;   /* 1.0f */
        std::vector<char> g;
        coopspawn::EncodeSpawn(&g, 6u, 0, 0, 0, "Wanderer", "Nameless", true, 0.5f);
        coopspawn::AppendSpawnStats(&g, raw);
        coopspawn::AppendSpawnFlags(&g, coopspawn::kSpawnFlagDead | coopspawn::kSpawnFlagKo);
        coopspawn::SpawnMsg e;
        if (coopspawn::DecodeSpawn(&g[0], g.size(), &e) != coopspawn::kSpawnDecodeOk || e.flagsAbsent
            || e.ownerFlags != (coopspawn::kSpawnFlagDead | coopspawn::kSpawnFlagKo) || e.statsState != coopspawn::kSpawnStatsOk)
            Fail("T-303: a dead + knocked-out SPAWN after a stats block did not read both bits");
        std::vector<char> o(g.begin(), g.end() - 1);   /* the same payload without its flags byte */
        coopspawn::SpawnMsg a;
        if (coopspawn::DecodeSpawn(&o[0], o.size(), &a) != coopspawn::kSpawnDecodeOk || !a.flagsAbsent || a.ownerFlags != 0)
            Fail("T-303: a SPAWN without the flags byte must read absent and alive");
        /* T-303 fold 1: a stats block present but cut short leaves no byte to read - the flags read absent (and alive);
           a NaN block is skipped whole (its length is fixed), so the flags after it are still read. */
        std::vector<char> t(g.begin(), g.end() - 2);   /* the flags byte and the block's last byte gone */
        coopspawn::SpawnMsg tm;
        if (coopspawn::DecodeSpawn(&t[0], t.size(), &tm) != coopspawn::kSpawnDecodeOk || tm.statsState != coopspawn::kSpawnStatsBad
            || !tm.flagsAbsent || tm.ownerFlags != 0)
            Fail("T-303 fold 1: a truncated stats block must read bad with the flags absent");
        std::vector<char> nf(g);
        const unsigned int nanBits = 0x7FC00000u;
        std::memcpy(&nf[nf.size() - 1 - coopstats::kStatsBlockBytes + 4 * 20], &nanBits, 4);   /* stat 20 of the block */
        coopspawn::SpawnMsg nm;
        if (coopspawn::DecodeSpawn(&nf[0], nf.size(), &nm) != coopspawn::kSpawnDecodeOk || nm.statsState != coopspawn::kSpawnStatsBad
            || nm.statsBadIndex != 20 || nm.flagsAbsent || nm.ownerFlags != (coopspawn::kSpawnFlagDead | coopspawn::kSpawnFlagKo))
            Fail("T-303 fold 1: the flags after a NaN stats block must still be read");
        std::vector<char> u;
        coopspawn::EncodeSpawn(&u, 7u, 0, 0, 0, "Wanderer", "", false, 0.0f);
        coopspawn::AppendSpawnStats(&u, 0);
        u.push_back((char)0xFC);   /* unknown bits only */
        coopspawn::SpawnMsg k;
        if (coopspawn::DecodeSpawn(&u[0], u.size(), &k) != coopspawn::kSpawnDecodeOk || k.flagsAbsent || k.ownerFlags != 0)
            Fail("T-303: unknown flag bits must be ignored");
    }
}

/* K1 (read-carry 2026-09-22): the MSG_STATE payload with and without carryingUid (session protocol 47). */
void t_k1_state_round_trips_with_and_without_carrying()
{
    float parts[2 * 8];
    for (int i = 0; i < 16; ++i) parts[i] = (float)i * 0.5f - 3.0f;
    const unsigned int body = 3204448269u;
    std::vector<char> b;
    coopstate::EncodeState(&b, 77u, parts, 2u, 8u, 12.5f, 4, 0, 0x21u, 3.25f, 9.0f, body);
    if (b.size() != (size_t)(8 + 2 * 8 * 4 + 41 + 4)) { Fail("a 2-part STATE is not 117 bytes: " + I((int)b.size())); return; }
    const size_t k47 = b.size() - 17;   /* R3 + POSE + BED1: the protocol-47 length (no rest block, no ACTPOSE block) */
    float out[32 * 8];
    coopstate::StateMsg m;
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk) { Fail("a whole STATE did not decode"); return; }
    if (m.uid != 77u || m.partCount != 2u || m.blood != 12.5f || m.prone != 4 || m.dead != 0 || m.latchBits != 0x21u
        || m.nextKnockoutAt != 3.25f || m.koTimer != 9.0f)
        Fail("the fields before carryingUid did not survive");
    if (m.carryingUid != body || m.carryAbsent) Fail("carryingUid did not survive");
    for (int i = 0; i < 16; ++i) if (out[i] != parts[i]) { Fail("part float " + I(i) + " changed"); break; }

    std::vector<char> z;
    coopstate::EncodeState(&z, 5u, parts, 1u, 8u, 1.0f, 0, 1, 0u, 0.0f, 0.0f, 0u);
    if (coopstate::DecodeState(&z[0], z.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.carryingUid != 0u || m.carryAbsent || m.dead != 1)
        Fail("a STATE carrying nothing must read carryingUid 0, present");

    /* a protocol-46 payload (no carryingUid) reads as carrying nothing, flagged absent; koTimer intact */
    if (coopstate::DecodeState(&b[0], k47 - 4, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.carryingUid != 0u || !m.carryAbsent || m.koTimer != 9.0f)
        Fail("a STATE without carryingUid must read 0 and absent");
    /* pre-H029 (no koTimer) and pre-F147 (no latch) payloads still decode */
    if (coopstate::DecodeState(&b[0], k47 - 8, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.koTimer != -1.0f || m.latchBits != 0x21u)
        Fail("a STATE without koTimer must read -1");
    if (coopstate::DecodeState(&b[0], k47 - 16, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.latchBits != 0u || m.prone != 4)
        Fail("a STATE without the latch block must read no latch");

    /* hostile or short payloads */
    if (coopstate::DecodeState(&b[0], 7, 32u, 8u, out, &m) != coopstate::kStateDecodeTooShort) Fail("7 bytes must be too short");
    if (coopstate::DecodeState(&b[0], k47 - 29, 32u, 8u, out, &m) != coopstate::kStateDecodePartsTrunc) Fail("a STATE cut before dead must be refused");
    if (coopstate::DecodeState(&b[0], b.size(), 1u, 8u, out, &m) != coopstate::kStateDecodeBadCount) Fail("a part count above the maximum must be refused");
    std::vector<char> h(b);
    const unsigned int huge = 0xFFFFFFFFu, none = 0u;
    std::memcpy(&h[4], &huge, 4);
    if (coopstate::DecodeState(&h[0], h.size(), 0xFFFFFFFFu, 8u, 0, &m) != coopstate::kStateDecodePartsTrunc) Fail("a hostile part count must not wrap");
    std::memcpy(&h[4], &none, 4);
    if (coopstate::DecodeState(&h[0], h.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeBadCount) Fail("a zero part count must be refused");
}

/* R3 (read-ragdoll 2026-09-22): the MSG_STATE rest block (session protocol 48), with and without a rest position. */
void t_r3_state_round_trips_with_and_without_rest()
{
    float parts[8];
    for (int i = 0; i < 8; ++i) parts[i] = (float)i - 2.5f;
    const float rest[3] = { 1234.5f, -56.25f, 98765.0f };
    std::vector<char> b;
    coopstate::EncodeState(&b, 9u, parts, 1u, 8u, 2.0f, 4, 0, 0x2u, 1.0f, 5.0f, 42u, rest);
    if (b.size() != (size_t)(8 + 8 * 4 + 41 + 4)) { Fail("a 1-part STATE with a rest block and an empty ACTPOSE block is not 85 bytes: " + I((int)b.size())); return; }
    float out[32 * 8];
    coopstate::StateMsg m;
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk) { Fail("a STATE with a rest block did not decode"); return; }
    if (m.restHas != 1 || m.rest[0] != rest[0] || m.rest[1] != rest[1] || m.rest[2] != rest[2]) Fail("the rest position did not survive");
    if (m.carryingUid != 42u || m.carryAbsent || m.koTimer != 5.0f || m.prone != 4 || m.latchBits != 0x2u) Fail("the fields before the rest block did not survive");
    for (int i = 0; i < 8; ++i) if (out[i] != parts[i]) { Fail("part float " + I(i) + " changed"); break; }

    std::vector<char> z;
    coopstate::EncodeState(&z, 9u, parts, 1u, 8u, 2.0f, 0, 0, 0u, 0.0f, 0.0f, 0u, 0);
    if (z.size() != b.size()) Fail("a STATE with no rest must still carry the (empty) block");
    if (coopstate::DecodeState(&z[0], z.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.restHas != 0
        || m.rest[0] != 0.0f || m.rest[1] != 0.0f || m.rest[2] != 0.0f)
        Fail("a STATE with no rest must read restHas 0");
    std::vector<char> d;
    coopstate::EncodeState(&d, 9u, parts, 1u, 8u, 2.0f, 0, 0, 0u, 0.0f, 0.0f, 7u);   /* the default argument: no rest */
    if (d.size() != z.size() || coopstate::DecodeState(&d[0], d.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.restHas != 0 || m.carryingUid != 7u)
        Fail("EncodeState without a rest argument must write restHas 0");

    /* a protocol-47 payload (no rest block) reads as no rest, carryingUid intact; a cut block reads as no rest */
    if (coopstate::DecodeState(&b[0], b.size() - 17, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.restHas != 0 || m.carryingUid != 42u || m.carryAbsent)
        Fail("a STATE without the rest block must read no rest and keep carryingUid");
    if (coopstate::DecodeState(&b[0], b.size() - 5, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.restHas != 0)
        Fail("a STATE with a cut rest block must read no rest");
}

/* POSE (read-poses 2026-09-23): the MSG_STATE ACTPOSE block (session protocol 50), with and without a pose. */
void t_pose_state_round_trips_with_and_without_actpose()
{
    float parts[8];
    for (int i = 0; i < 8; ++i) parts[i] = (float)i + 0.25f;
    const float rest[3] = { 10.0f, 20.0f, 30.0f };
    coopstate::PoseWire pw;
    coopstate::PoseClear(&pw);
    pw.kind = coopstate::kPoseBed;
    std::strcpy(pw.action, "sleeponfloor");
    std::strcpy(pw.bedKey, "Bed Camp|12,-3|1234,56,-789");
    const size_t al = std::strlen(pw.action), kl = std::strlen(pw.bedKey);
    std::vector<char> b;
    coopstate::EncodeState(&b, 11u, parts, 1u, 8u, 3.0f, 0, 0, 0u, 0.0f, -1.0f, 5u, rest, &pw);
    if (b.size() != (size_t)(8 + 8 * 4 + 41 + 4) + al + kl) { Fail("a STATE with a bed pose has the wrong size: " + I((int)b.size())); return; }
    float out[32 * 8];
    coopstate::StateMsg m;
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk) { Fail("a STATE with a pose did not decode"); return; }
    if (m.poseAbsent || m.pose.kind != coopstate::kPoseBed || std::strcmp(m.pose.action, pw.action) != 0
        || std::strcmp(m.pose.bedKey, pw.bedKey) != 0 || !coopstate::PoseEqual(m.pose, pw))
        Fail("the bed pose did not survive");
    if (m.restHas != 1 || m.rest[2] != 30.0f || m.carryingUid != 5u || m.koTimer != -1.0f) Fail("the fields before the ACTPOSE block did not survive");

    /* arrest2 (56): kind 3, a cage, keyed like a bed */
    coopstate::PoseWire pc;
    coopstate::PoseClear(&pc);
    pc.kind = coopstate::kPoseCage;
    std::strcpy(pc.bedKey, "Prison cage|43,11|5290,23,-9493");
    std::strcpy(pc.outerKey, "Police station|43,11|5301,20,-9480");
    std::vector<char> cb;
    coopstate::EncodeState(&cb, 11u, parts, 1u, 8u, 3.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pc);
    if (coopstate::DecodeState(&cb[0], cb.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent
        || m.pose.kind != coopstate::kPoseCage || !coopstate::PoseEqual(m.pose, pc))
        Fail("a cage pose did not survive");
    /* a kind above kPoseCage still reads as absent */
    std::vector<char> bad(cb);
    bad[8 + 8 * 4 + 41] = (char)4;
    if (coopstate::DecodeState(&bad[0], bad.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent)
        Fail("a pose kind above kPoseCage must read as absent");

    /* kind 1, an action alone */
    coopstate::PoseWire pa;
    coopstate::PoseClear(&pa);
    pa.kind = coopstate::kPoseAction;
    std::strcpy(pa.action, "sit_bench");
    std::vector<char> a;
    coopstate::EncodeState(&a, 11u, parts, 1u, 8u, 3.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pa);
    if (coopstate::DecodeState(&a[0], a.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent
        || m.pose.kind != coopstate::kPoseAction || std::strcmp(m.pose.action, "sit_bench") != 0 || m.pose.bedKey[0] != 0 || m.restHas != 0)
        Fail("an action pose did not survive");

    /* no pose argument: an empty block, kind 0, present */
    std::vector<char> z;
    coopstate::EncodeState(&z, 11u, parts, 1u, 8u, 3.0f, 0, 0, 0u, 0.0f, -1.0f, 0u);
    if (z.size() != (size_t)(8 + 8 * 4 + 41 + 4)) Fail("a STATE with no pose must still carry the 4-byte empty block");
    if (coopstate::DecodeState(&z[0], z.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent
        || m.pose.kind != coopstate::kPoseNone || m.pose.action[0] != 0 || m.pose.bedKey[0] != 0)
        Fail("a STATE with no pose must read kind 0, present");

    /* a protocol-49 payload (no block), and a cut block, read as absent with the rest block intact */
    const size_t k49 = b.size() - (4 + al + kl);
    if (coopstate::DecodeState(&b[0], k49, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent || m.pose.kind != 0 || m.restHas != 1)
        Fail("a STATE without the ACTPOSE block must read absent and keep the rest block");
    if (coopstate::DecodeState(&b[0], b.size() - 2, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent)
        Fail("a STATE with a cut ACTPOSE block must read absent");

    /* hostile: a kind above 2, and a length above 48, read as absent */
    std::vector<char> h(z);
    h[8 + 8 * 4 + 41] = (char)7;
    if (coopstate::DecodeState(&h[0], h.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent) Fail("pose kind 7 must read absent");
    std::vector<char> g(b);
    g[8 + 8 * 4 + 41 + 1] = (char)200;
    if (coopstate::DecodeState(&g[0], g.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent) Fail("an action length of 200 must read absent");

    /* an over-long string is clipped to 48 on the wire */
    coopstate::PoseWire pl;
    coopstate::PoseClear(&pl);
    pl.kind = coopstate::kPoseAction;
    for (int i = 0; i < 48; ++i) pl.action[i] = 'a';
    pl.action[48] = 0;
    std::vector<char> l;
    coopstate::EncodeState(&l, 11u, parts, 1u, 8u, 3.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pl);
    if (coopstate::DecodeState(&l[0], l.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent || std::strlen(m.pose.action) != 48)
        Fail("a 48-byte action must survive");
}

/* BED1 (T263/T264, session 51): the ACTPOSE block's trailing outer key - round trip, a 50 payload, a hostile length. */
void t_bed1_state_outer_key_round_trips_and_50_payload_reads_empty()
{
    float parts[8];
    for (int i = 0; i < 8; ++i) parts[i] = (float)i;
    coopstate::PoseWire pw;
    coopstate::PoseClear(&pw);
    pw.kind = coopstate::kPoseBed;
    std::strcpy(pw.bedKey, "Bed@43,11@541200,-945100,11100");
    std::strcpy(pw.outerKey, "House large@43,11@541056,-945305,11021");
    const size_t kl = std::strlen(pw.bedKey), ol = std::strlen(pw.outerKey);
    std::vector<char> b;
    coopstate::EncodeState(&b, 3u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pw);
    if (b.size() != (size_t)(8 + 8 * 4 + 41 + 4) + kl + ol) { Fail("a STATE with an outer key has the wrong size: " + I((int)b.size())); return; }
    float out[32 * 8];
    coopstate::StateMsg m;
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent
        || std::strcmp(m.pose.outerKey, pw.outerKey) != 0 || std::strcmp(m.pose.bedKey, pw.bedKey) != 0 || !coopstate::PoseEqual(m.pose, pw))
        Fail("the outer key did not survive");
    /* PoseEqual sees the outer key: the same bed under another building is a different pose */
    coopstate::PoseWire other = pw;
    std::strcpy(other.outerKey, "House small@43,11@1,2,3");
    if (coopstate::PoseEqual(other, pw)) Fail("PoseEqual must compare the outer key");
    /* a protocol-50 payload - the block ends after bedKey - reads present, outer key empty */
    if (coopstate::DecodeState(&b[0], b.size() - (1 + ol), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.poseAbsent
        || m.pose.outerKey[0] != 0 || std::strcmp(m.pose.bedKey, pw.bedKey) != 0)
        Fail("a 50 payload must read the bed pose with an empty outer key");
    /* an outer length that does not fit reads the whole block absent */
    std::vector<char> h(b);
    h[b.size() - 1 - ol] = (char)(ol + 5);
    if (coopstate::DecodeState(&h[0], h.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent || m.pose.kind != 0)
        Fail("an outer length past the payload must read absent");
    h[b.size() - 1 - ol] = (char)200;
    if (coopstate::DecodeState(&h[0], h.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || !m.poseAbsent)
        Fail("an outer length above 48 must read absent");
    /* a null pose still writes an empty outer key byte */
    std::vector<char> z;
    coopstate::EncodeState(&z, 3u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0);
    if (z.size() != (size_t)(8 + 8 * 4 + 41 + 4) || z[z.size() - 1] != 0) Fail("a STATE with no pose must end in an empty outer key");
}

/* par5 (parity P5) */
/* par5 (parity P5, session 79): the owner's hunger block after the outer key - round trip, absent, cut, pre-79. */
void t_par5_state_hunger_round_trips_absent_and_cut_reads_not_carried()
{
    float parts[8];
    for (int i = 0; i < 8; ++i) parts[i] = (float)i;
    coopstate::PoseWire pw;
    coopstate::PoseClear(&pw);
    pw.kind = coopstate::kPoseBed;
    std::strcpy(pw.bedKey, "Bed@43,11@541200,-945100,11100");
    std::strcpy(pw.outerKey, "House large@43,11@541056,-945305,11021");
    const size_t kl = std::strlen(pw.bedKey), ol = std::strlen(pw.outerKey);
    const float hg[2] = { 123.5f, 7.25f };
    std::vector<char> b;
    coopstate::EncodeState(&b, 3u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pw, hg);
    if (b.size() != (size_t)(8 + 8 * 4 + 41 + 4) + kl + ol + 9) { Fail("a STATE with hunger has the wrong size: " + I((int)b.size())); return; }
    float out[32 * 8];
    coopstate::StateMsg m;
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 1
        || m.hunger != 123.5f || m.fed != 7.25f)
        Fail("the hunger block did not survive");
    if (m.poseAbsent || !coopstate::PoseEqual(m.pose, pw)) Fail("the pose before the hunger block did not survive");
    /* a null pose still carries hunger (after the empty outer key byte) */
    std::vector<char> z;
    coopstate::EncodeState(&z, 3u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0, hg);
    if (coopstate::DecodeState(&z[0], z.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 1
        || m.hunger != 123.5f || m.fed != 7.25f || m.pose.kind != coopstate::kPoseNone)
        Fail("hunger after a null pose did not survive");
    /* no hunger: nothing written, reads not carried */
    std::vector<char> n;
    coopstate::EncodeState(&n, 3u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, &pw);
    if (n.size() != b.size() - 9) Fail("a STATE without hunger must not grow");
    if (coopstate::DecodeState(&n[0], n.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 0 || m.poseAbsent)
        Fail("a STATE without hunger must read not carried with its pose");
    /* a cut block reads not carried, pose intact */
    if (coopstate::DecodeState(&b[0], b.size() - 1, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 0 || m.poseAbsent)
        Fail("a cut hunger block must read not carried");
    /* a has byte other than 1 reads not carried */
    std::vector<char> h0(b);
    h0[b.size() - 9] = (char)0;
    if (coopstate::DecodeState(&h0[0], h0.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 0)
        Fail("a hunger has byte of 0 must read not carried");
    /* a 50 payload (ends after bedKey) carries none */
    if (coopstate::DecodeState(&b[0], b.size() - 9 - (1 + ol), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 0)
        Fail("a 50 payload must read hunger not carried");
    /* the plausibility test a copy applies */
    volatile float zero = 0.0f;
    const float nan = zero / zero, inf = 1.0f / zero;
    if (!coopstate::HungerPlausible(0.0f, 0.0f) || !coopstate::HungerPlausible(-5.0f, 300.0f)) Fail("ordinary hunger must be plausible");
    if (coopstate::HungerPlausible(nan, 0.0f) || coopstate::HungerPlausible(0.0f, nan) || coopstate::HungerPlausible(1e9f, 0.0f)
        || coopstate::HungerPlausible(0.0f, inf))
        Fail("NaN / infinite / absurd hunger must be refused");
}

/* LIMBS (P38 / P39): the limb block after the hunger block - with hunger, without it (one 0 byte first), absent, cut,
   another version byte, a state above crushed, a sid over the cap. */
void t_limbs_state_block_round_trips_and_malformed_reads_absent()
{
    const float parts[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    const float hg[2] = { 50.0f, 3.0f };
    cooplimb::LimbsWire w;
    cooplimb::LimbsClear(&w);
    w.state[0] = cooplimb::kLimbStump;
    w.state[1] = cooplimb::kLimbReplaced; cooplimb::LimbSetSid(&w, 1, "123-gamedata.quack"); w.quality[1] = 0.75f;
    w.state[3] = cooplimb::kLimbReplaced; cooplimb::LimbSetSid(&w, 3, "456-gamedata.quack"); w.quality[3] = 1.5f;
    const size_t blockLen = 1 + 4 + 2 * (1 + 18 + 4 + 4);
    float out[32 * 8];
    coopstate::StateMsg m;
    std::vector<char> b;
    coopstate::EncodeState(&b, 9u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0, hg, &w);
    if (coopstate::DecodeState(&b[0], b.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 1
        || m.hunger != 50.0f || m.fed != 3.0f || m.limbsHas != 1 || !cooplimb::LimbsEqual(m.limbs, w))
        Fail("limbs after a carried hunger block did not survive");
    if (std::strcmp(m.limbs.sid[3], "456-gamedata.quack") != 0 || m.limbs.quality[1] != 0.75f || m.limbs.state[2] != cooplimb::kLimbOriginal)
        Fail("a limb's sid / quality / state came back wrong");
    /* no limbs: the payload is exactly the one before the block existed */
    std::vector<char> nl;
    coopstate::EncodeState(&nl, 9u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0, hg);
    if (nl.size() + blockLen != b.size()) { Fail("the limb block has the wrong size: " + I((int)(b.size() - nl.size()))); return; }
    if (coopstate::DecodeState(&nl[0], nl.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.limbsHas != 0 || m.hungerHas != 1)
        Fail("a STATE without limbs must read limbs not carried, hunger intact");
    /* without hunger: one 0 byte stands in its place, the limbs still read */
    std::vector<char> nh, none;
    coopstate::EncodeState(&nh, 9u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0, 0, &w);
    coopstate::EncodeState(&none, 9u, parts, 1u, 8u, 1.0f, 0, 0, 0u, 0.0f, -1.0f, 0u, 0, 0, 0);
    if (nh.size() != none.size() + 1 + blockLen) Fail("limbs without hunger must add one 0 byte and the block");
    if (coopstate::DecodeState(&nh[0], nh.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.hungerHas != 0
        || m.limbsHas != 1 || !cooplimb::LimbsEqual(m.limbs, w))
        Fail("limbs without hunger did not survive");
    /* a cut block reads absent; the hunger before it stays */
    if (coopstate::DecodeState(&b[0], b.size() - 1, 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.limbsHas != 0 || m.hungerHas != 1)
        Fail("a cut limb block must read not carried, hunger intact");
    /* another version byte, or a state above crushed: absent */
    std::vector<char> v(b);
    v[nl.size()] = (char)3;
    if (coopstate::DecodeState(&v[0], v.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.limbsHas != 0)
        Fail("another limb block version must read not carried");
    std::vector<char> s4(b);
    s4[nl.size() + 1] = (char)4;
    if (coopstate::DecodeState(&s4[0], s4.size(), 32u, 8u, out, &m) != coopstate::kStateDecodeOk || m.limbsHas != 0)
        Fail("a limb state above crushed must read not carried");
    /* all four whole: five bytes, round trip */
    cooplimb::LimbsWire z, zr;
    cooplimb::LimbsClear(&z);
    std::vector<char> zb;
    cooplimb::EncodeLimbs(&zb, z);
    size_t end = 0;
    if (zb.size() != 5 || !cooplimb::DecodeLimbs(&zb[0], zb.size(), 0, &zr, &end) || end != 5 || !cooplimb::LimbsEqual(z, zr))
        Fail("four whole limbs must be five bytes and round-trip");
    if (cooplimb::DecodeLimbs(&zb[0], zb.size(), zb.size() + 1, &zr, 0) || cooplimb::DecodeLimbs(&zb[0], zb.size(), 1, &zr, 0))
        Fail("a block that starts past the end, or mid-block, must be refused");
    /* a sid over the cap: refused by the decoder; LimbSetSid cuts it */
    std::vector<char> big;
    big.push_back((char)1); big.push_back((char)2); big.push_back((char)0); big.push_back((char)0); big.push_back((char)0);
    big.push_back((char)64);
    for (int k = 0; k < 64 + 4; ++k) big.push_back('a');
    if (cooplimb::DecodeLimbs(&big[0], big.size(), 0, &zr, 0)) Fail("a 64-byte sid must be refused");
    std::string longSid(100, 'b');
    cooplimb::LimbSetSid(&z, 0, longSid.c_str());
    if (cooplimb::LimbSidLen(z.sid[0]) != cooplimb::kLimbSidMax) Fail("LimbSetSid must cut a long sid to the cap");
    /* an empty sid on a fitted limb travels (the owner could not read it) and plans as unnamed */
    cooplimb::LimbsWire e;
    cooplimb::LimbsClear(&e);
    e.state[2] = cooplimb::kLimbReplaced;
    std::vector<char> eb;
    cooplimb::EncodeLimbs(&eb, e);
    if (eb.size() != 5 + 1 + 4 + 4 || !cooplimb::DecodeLimbs(&eb[0], eb.size(), 0, &zr, 0) || zr.state[2] != cooplimb::kLimbReplaced || zr.sid[2][0] != 0)
        Fail("a fitted limb with no name must round-trip with an empty sid");
}

/* LIMBS: the copy's decision per limb, from its own state and its owner's. */
void t_limbs_plan_limb_per_owner_and_copy_state()
{
    struct Row { unsigned char cs; const char* csid; unsigned char os; const char* osid; int want; };
    const Row rows[] = {
        { 0, "",  0, "",  cooplimb::kLimbStepNone },
        { 1, "",  1, "",  cooplimb::kLimbStepNone },
        { 3, "",  3, "",  cooplimb::kLimbStepNone },
        { 0, "",  1, "",  cooplimb::kLimbStepAmputate },
        { 3, "",  1, "",  cooplimb::kLimbStepAmputate },
        { 2, "x", 1, "",  cooplimb::kLimbStepUnfit },
        { 0, "",  2, "x", cooplimb::kLimbStepFit },
        { 1, "",  2, "x", cooplimb::kLimbStepFit },
        { 3, "",  2, "x", cooplimb::kLimbStepFit },
        { 2, "x", 2, "x", cooplimb::kLimbStepNone },
        { 2, "x", 2, "y", cooplimb::kLimbStepRefit },
        { 2, "",  2, "x", cooplimb::kLimbStepRefit },
        { 2, "x", 2, "",  cooplimb::kLimbStepNone },
        { 1, "",  2, "",  cooplimb::kLimbStepUnnamed },
        { 1, "",  0, "",  cooplimb::kLimbStepCannotRegrow },
        { 2, "x", 0, "",  cooplimb::kLimbStepCannotRegrow },
        { 0, "",  3, "",  cooplimb::kLimbStepNoCrushRoad },
        { 0, "",  4, "",  cooplimb::kLimbStepNone }
    };
    for (size_t k = 0; k < sizeof(rows) / sizeof(rows[0]); ++k)
    {
        const int got = cooplimb::PlanLimb(rows[k].cs, rows[k].csid, rows[k].os, rows[k].osid);
        if (got != rows[k].want)
            Fail("PlanLimb row " + I((int)k) + ": want " + cooplimb::LimbStepName(rows[k].want) + ", got " + cooplimb::LimbStepName(got));
    }
    if (cooplimb::PlanLimb(2, 0, 2, "x") != cooplimb::kLimbStepRefit || cooplimb::PlanLimb(0, 0, 2, 0) != cooplimb::kLimbStepUnnamed)
        Fail("PlanLimb must take null sids");
    if (std::strcmp(cooplimb::LimbName(3), "rightLeg") != 0 || std::strcmp(cooplimb::LimbName(4), "?") != 0) Fail("LimbName order");
}

/* LIMBS: a same-item quality refit, the robotic limb class test and the limbtest lever's arguments. */
void t_limbs_quality_refit_robot_class_and_limbtest_args()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "x", 0.9f) != cooplimb::kLimbStepRefit) Fail("same item, other quality must refit");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "x", 0.5f) != cooplimb::kLimbStepNone) Fail("same item, same quality is none");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "x", 0.5005f) != cooplimb::kLimbStepNone) Fail("a difference under 0.001 is none");
    if (cooplimb::PlanLimbQ(2, "x", -1.0f, 2, "x", 0.9f) != cooplimb::kLimbStepNone) Fail("an unreadable copy quality never refits");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "x", nan) != cooplimb::kLimbStepNone) Fail("a NaN owner quality never refits");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "x", 100000.0f) != cooplimb::kLimbStepNone) Fail("an implausible owner quality never refits");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "", 0.9f) != cooplimb::kLimbStepNone) Fail("an unnamed owner item never refits on quality");
    if (cooplimb::PlanLimbQ(2, "x", 0.5f, 2, "y", 0.5f) != cooplimb::kLimbStepRefit) Fail("another item still refits");
    if (cooplimb::PlanLimbQ(0, "", 0.0f, 1, "", 0.0f) != cooplimb::kLimbStepAmputate) Fail("PlanLimbQ must keep PlanLimb's other steps");
    if (!cooplimb::LimbIsRobotItemClass(".?AVRobotLimbItem@@")) Fail("RobotLimbItem is the robotic limb class");
    if (cooplimb::LimbIsRobotItemClass(".?AVSeveredLimbItem@@") || cooplimb::LimbIsRobotItemClass(".?AVItem@@")
        || cooplimb::LimbIsRobotItemClass("") || cooplimb::LimbIsRobotItemClass(0))
        Fail("only RobotLimbItem is a robotic limb");
    unsigned int uid = 0; int limb = -1; std::string sid;
    if (cooplimb::LimbTestParse("cut 77 2", &uid, &limb, &sid) != cooplimb::kLimbTestCut || uid != 77 || limb != 2)
        Fail("limbtest cut args");
    if (cooplimb::LimbTestParse("fit 77 0 95747-Newwworld.mod", &uid, &limb, &sid) != cooplimb::kLimbTestFit || limb != 0
        || sid != "95747-Newwworld.mod")
        Fail("limbtest fit args");
    if (cooplimb::LimbTestParse(" show 12 ", &uid, &limb, &sid) != cooplimb::kLimbTestShow || uid != 12) Fail("limbtest show args");
    const char* bad[] = { "", "cut", "cut 0 1", "cut 5 4", "cut 5 -1", "cut 5 1 extra", "fit 5 1", "show 5 6", "grow 5 1",
                          "fit 5 1 0123456789012345678901234567890123456789012345678901234567890123", "cut x 1" };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
        if (cooplimb::LimbTestParse(bad[k], &uid, &limb, &sid) != cooplimb::kLimbTestBad)
            Fail(std::string("limbtest must refuse '") + bad[k] + "'");
}

/* squadtest: swsquad::SquadTestParse - add <ownUid|first> <n 1..8> [templateSid]; `first` hands back uid 0. */
void t_squadtest_args()
{
    unsigned int uid = 7; int n = 7; std::string sid = "x";
    if (!swsquad::SquadTestParse("add 4194305 5", &uid, &n, &sid) || uid != 4194305u || n != 5 || sid != "2205-gamedata.base")
        Fail("squadtest add: the Drifter record when no template is named");
    if (!swsquad::SquadTestParse(" add 1 8 1234-gamedata.base ", &uid, &n, &sid) || uid != 1 || n != 8 || sid != "1234-gamedata.base")
        Fail("squadtest add with a template sid");
    if (!swsquad::SquadTestParse("add 4294967295 1", &uid, &n, &sid) || uid != 4294967295u || n != 1) Fail("squadtest: the largest uid");
    if (!swsquad::SquadTestParse("add first 1 57390-rebirth.mod", &uid, &n, &sid) || uid != 0 || n != 1 || sid != "57390-rebirth.mod")
        Fail("squadtest add first: the player's first character and a named template");
    uid = 9; n = 9; sid = "keep";
    const char* bad[] = { "", "add", "add 1", "add 0 1", "add 1 0", "add 1 9", "add x 1", "add First 1", "add 4294967296 1",
                          "remove 1 1", "add 1 1 a b", "add -1 1", "add 1 -1", "add first 0", "add first",
                          "add 1 1 0123456789012345678901234567890123456789012345678901234567890123" };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
        if (swsquad::SquadTestParse(bad[k], &uid, &n, &sid)) Fail(std::string("squadtest must refuse '") + bad[k] + "'");
    if (uid != 9 || n != 9 || sid != "keep") Fail("squadtest: a refused shape leaves the outputs untouched");
}

/* statstest: swsquad::StatsTestParse (squad <level 1..100> | show <uid>) and StatsLevelApply - block values 0..40 (skills, attributes,
   attack, defence) take the level; warrior spirit, xp and the free attribute points keep theirs. */
void t_statstest_args_and_levels()
{
    int level = 0; unsigned int uid = 0;
    if (swsquad::StatsTestParse("squad 60", &level, &uid) != swsquad::kStatsTestSquad || level != 60) Fail("statstest squad 60");
    if (swsquad::StatsTestParse(" squad 100 ", &level, &uid) != swsquad::kStatsTestSquad || level != 100) Fail("statstest squad 100");
    if (swsquad::StatsTestParse("squad 1", &level, &uid) != swsquad::kStatsTestSquad || level != 1) Fail("statstest squad 1");
    if (swsquad::StatsTestParse("show 4194305", &level, &uid) != swsquad::kStatsTestShow || uid != 4194305u) Fail("statstest show");
    if (swsquad::StatsTestParse("show 4294967295", &level, &uid) != swsquad::kStatsTestShow || uid != 4294967295u) Fail("statstest show: the largest uid");
    level = 7; uid = 9;
    const char* bad[] = { "", "squad", "squad 0", "squad 101", "squad 60 1", "squad x", "all 60", "squad -5", "squad 6.5",
                          "squad 0060", "show", "show 0", "show x", "show 5 6", "show 4294967296", "show 12345678901", "Show 5" };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
        if (swsquad::StatsTestParse(bad[k], &level, &uid) != swsquad::kStatsTestBad) Fail(std::string("statstest must refuse '") + bad[k] + "'");
    if (level != 7 || uid != 9) Fail("statstest: a refused shape leaves the outputs untouched");
    if (coopstats::StatsOffset(swsquad::kStatsIdxToughness) != 0x90u || coopstats::StatsOffset(swsquad::kStatsIdxKatana) != 0xF8u
        || coopstats::StatsOffset(swsquad::kStatsIdxDexterity) != 0x88u)
        Fail("statstest show: toughness +0x90, dexterity +0x88, katana +0xF8 (CharStats::updateStats 0x64AC10)");
    unsigned int raw[coopstats::kStatsCount];
    for (int i = 0; i < coopstats::kStatsCount; ++i) raw[i] = 0x3F800000u + (unsigned int)i;
    swsquad::StatsLevelApply(raw, 60);
    for (int i = 0; i < coopstats::kStatsCount; ++i)
    {
        const bool set = i < swsquad::kStatsLevelCount;
        if (set && coopstats::StatsBitsToFloat(raw[i]) != 60.0f) Fail("statstest: a skill, attribute, attack or defence value not set");
        if (!set && raw[i] != 0x3F800000u + (unsigned int)i) Fail("statstest: warrior spirit, xp or free points changed");
    }
    if (coopstats::StatsOffset(swsquad::kStatsLevelCount - 1) != 0x124u || coopstats::StatsOffset(swsquad::kStatsLevelCount) != 0x13Cu)
        Fail("statstest: the last value set is defence (+0x124), warrior spirit (+0x13C) is the first kept");
}

/* par6b: coopstate::MedicalUpdateDeathCall - medicalUpdate's two declareDead call sites, by return address and base. */
void t_par6b_medical_update_death_call_by_return_address()
{
    const unsigned long long base = 0x140000000ULL;
    const unsigned long long kR1 = 0x651A15ULL, kR2 = 0x651C6EULL;   /* the Steam 1.0.65 table's MedUpdateDeathRet1/2 rows */
    if (!coopstate::MedicalUpdateDeathCall(base + 0x651A15ULL, base, kR1, kR2)) Fail("the blood/hunger call site (ret 0x651A15) must be recognised");
    if (!coopstate::MedicalUpdateDeathCall(base + 0x651C6EULL, base, kR1, kR2)) Fail("the vital-part call site (ret 0x651C6E) must be recognised");
    /* the call instructions themselves, one byte either side, the event callers and the lever are not */
    const unsigned long long no[8] = { 0x651A10ULL, 0x651A14ULL, 0x651A16ULL, 0x651C69ULL, 0x651C6DULL, 0x651C6FULL, 0x75DD00ULL, 0x5DB900ULL };
    for (int i = 0; i < 8; ++i)
        if (coopstate::MedicalUpdateDeathCall(base + no[i], base, kR1, kR2)) Fail("a return address that is not a medicalUpdate call site must not be recognised");
    /* not armed: base 0 recognises nothing, even the bare RVAs */
    if (coopstate::MedicalUpdateDeathCall(0x651A15ULL, 0, kR1, kR2) || coopstate::MedicalUpdateDeathCall(base + 0x651A15ULL, 0, kR1, kR2)) Fail("an unarmed base must recognise nothing");
    /* a return address at or below the base (outside the image) is not recognised and does not wrap */
    if (coopstate::MedicalUpdateDeathCall(base, base, kR1, kR2) || coopstate::MedicalUpdateDeathCall(0x651A15ULL, base, kR1, kR2)) Fail("an address at or below the base must not be recognised");
    /* stage 7/9: an unbound row (0) arms nothing, even for a real call site */
    if (coopstate::MedicalUpdateDeathCall(base + 0x651A15ULL, base, 0, kR2) || coopstate::MedicalUpdateDeathCall(base + 0x651C6EULL, base, kR1, 0)) Fail("an unbound row must recognise nothing");
}

/* BED1: coopbed::PickPieceKey - which interior piece a bed key names. */
void t_bed1_pick_piece_key_exact_near_tiebreak_and_refusals()
{
    const char* want = "Bed@43,11@541200,-945100,11100";
    int nearest = 0;
    long nt = 0;
    {   /* exact wins over a near neighbour */
        const char* have[3] = { "Bed@43,11@541300,-945100,11100", "Bed@43,11@541200,-945100,11100", "Bed@43,11@541201,-945100,11100" };
        if (coopbed::PickPieceKey(want, have, 3, &nearest, &nt) != 1) Fail("the exact key must be picked");
        if (nearest != 1 || nt != 0) Fail("nearest must name the exact piece at distance 0");
    }
    {   /* two identical exact keys are refused */
        const char* have[2] = { want, want };
        if (coopbed::PickPieceKey(want, have, 2, 0, 0) != coopbed::kPickAmbiguous) Fail("two exact matches must be refused");
    }
    {   /* no exact: one within a tenth on x and z */
        const char* have[2] = { "Bed@43,11@541201,-945099,11100", "Bed@43,11@541210,-945100,11100" };
        if (coopbed::PickPieceKey(want, have, 2, 0, 0) != 0) Fail("a piece one tenth off must be picked");
    }
    {   /* two tenths off is a miss, and says how close it came */
        const char* have[1] = { "Bed@43,11@541202,-945100,11100" };
        if (coopbed::PickPieceKey(want, have, 1, &nearest, &nt) != coopbed::kPickNone) Fail("two tenths off must miss");
        if (nearest != 0 || nt != 2) Fail("the miss must name its nearest piece and 2 tenths");
    }
    {   /* another record or another sector never matches, and is not a 'nearest' */
        const char* have[2] = { "Camp Bed@43,11@541200,-945100,11100", "Bed@43,12@541200,-945100,11100" };
        if (coopbed::PickPieceKey(want, have, 2, &nearest, &nt) != coopbed::kPickNone) Fail("another sid / sector must miss");
        if (nearest != -1 || nt != -1) Fail("another sid / sector must not be nearest");
    }
    {   /* two near pieces on two floors: y breaks the tie; the same y is refused */
        const char* have[2] = { "Bed@43,11@541200,-945101,11100", "Bed@43,11@541201,-945100,13600" };
        if (coopbed::PickPieceKey(want, have, 2, 0, 0) != 0) Fail("y must break a two-floor tie");
        const char* same[2] = { "Bed@43,11@541200,-945101,11100", "Bed@43,11@541201,-945100,11099" };
        if (coopbed::PickPieceKey(want, same, 2, 0, 0) != coopbed::kPickAmbiguous) Fail("two near pieces at one height must be refused");
        if (coopbed::PickPieceKey("Bed@43,11@541200,-945100", same, 2, 0, 0) != coopbed::kPickAmbiguous) Fail("no y, no tiebreak");
    }
    {   /* a record name holding '@', a hashed sid, a null entry */
        const char* have[3] = { 0, "a@b@1,2@10,20,30", "#1a2b3c4d@1,2@10,20,30" };
        if (coopbed::PickPieceKey("a@b@1,2@10,21,30", have, 3, 0, 0) != 1) Fail("a sid holding '@' must still match");
        if (coopbed::PickPieceKey("#1a2b3c4d@1,2@10,20,30", have, 3, 0, 0) != 2) Fail("a hashed sid must match exactly");
    }
    {   /* malformed or empty input never picks */
        const char* have[1] = { "Bed@43,11@541200,-945100,11100" };
        if (coopbed::PickPieceKey("", have, 1, 0, 0) != coopbed::kPickNone) Fail("an empty key must miss");
        if (coopbed::PickPieceKey("Bed", have, 1, 0, 0) != coopbed::kPickNone) Fail("a malformed key must miss");
        if (coopbed::PickPieceKey(want, have, 0, 0, 0) != coopbed::kPickNone) Fail("no pieces must miss");
        const char* bad[1] = { "Bed@43,11@x,y" };
        if (coopbed::PickPieceKey(want, bad, 1, 0, 0) != coopbed::kPickNone) Fail("a malformed piece key must miss");
    }
}

/* BED1-b (review LOW-2): a lone near piece a floor away is refused; a small height difference is not. */
void t_bed1_pick_refuses_a_lone_piece_a_floor_away()
{
    const char* want = "Bed@43,11@541200,-945100,11100";
    const char* upstairs[1] = { "Bed@43,11@541201,-945100,13600" };
    if (coopbed::PickPieceKey(want, upstairs, 1, 0, 0) != coopbed::kPickNone) Fail("a lone near piece 250 units up must be refused");
    const char* edge[1] = { "Bed@43,11@541200,-945101,11201" };
    if (coopbed::PickPieceKey(want, edge, 1, 0, 0) != coopbed::kPickNone) Fail("101 tenths of height is a floor away");
    const char* close[1] = { "Bed@43,11@541200,-945101,11200" };
    if (coopbed::PickPieceKey(want, close, 1, 0, 0) != 0) Fail("100 tenths of height is still the same floor");
    if (coopbed::PickPieceKey("Bed@43,11@541200,-945100", upstairs, 1, 0, 0) != 0) Fail("a wanted key with no y cannot refuse on height");
    const char* exact[1] = { want };
    if (coopbed::PickPieceKey(want, exact, 1, 0, 0) != 0) Fail("the exact key is unaffected");
}

/* R3 (read-ragdoll 2026-09-22): the lock-free pending-move table - post, take once, done, slide (decision 61), withdraw, full.
   R3-b (review-r3): a second post for a ragdoll with a move in the table is refused busy (READY or mid-step), a withdraw
   while a step is being made loses and leaves the move, and a taker can abandon the move it holds. */
void t_r3_rest_table_post_take_withdraw()
{
    static cooprest::RestTable t;
    cooprest::RestReset(&t);
    int ka = 0, kb = 0;
    void* ra = &ka; void* rb = &kb; void* anim = &t;
    if (cooprest::RestStepsFor(1.5f) != 1 || cooprest::RestStepsFor(10.0f) != 2 || cooprest::RestStepsFor(119.0f) != 29
        || cooprest::RestStepsFor(500.0f) != 30 || cooprest::RestStepsFor(0.0f) != 1)
        Fail("steps must be clamp(|delta| / 4, 1, 30)");
    if (cooprest::RestPost(&t, ra, anim, 1.0f, 2.0f, 3.0f, 1) != 1 || t.pending != 1) { Fail("a post must be pending"); return; }
    if (cooprest::RestPost(&t, ra, anim, 4.0f, 5.0f, 6.0f, 1) != cooprest::kRestPostBusy || t.pending != 1 || t.busy != 1)
        Fail("a second post for a ragdoll with a READY move must be refused busy - not replace, not add");
    void* got = 0;
    float d[3] = { 0.0f, 0.0f, 0.0f };
    if (cooprest::RestTake(&t, rb, &got, d) >= 0) Fail("another ragdoll must not take it");
    if (!cooprest::RestIsPending(&t, ra) || cooprest::RestIsPending(&t, rb)) Fail("pending must name the posted ragdoll only");
    const int slot = cooprest::RestTake(&t, ra, &got, d);
    if (slot < 0 || d[0] != 1.0f || d[1] != 2.0f || d[2] != 3.0f || got != anim) { Fail("the take must return the posted delta and anim"); return; }
    if (cooprest::RestPost(&t, ra, anim, 9.0f, 9.0f, 9.0f, 1) != cooprest::kRestPostBusy || t.busy != 2 || t.pending != 1)
        Fail("a post while that ragdoll's step is being made must be refused busy");
    if (t.pending != 1) Fail("a move whose step is being made is still unfinished");
    if (!cooprest::RestIsPending(&t, ra)) Fail("a move being made stays pending until RestDone");
    if (cooprest::RestWithdraw(&t, ra)) Fail("a move being made cannot be withdrawn");
    if (!cooprest::RestIsPending(&t, ra) || t.pending != 1) Fail("a withdraw that lost to the taker leaves the move in the table");
    if (!cooprest::RestDone(&t, slot)) Fail("a one-step move is done after one step");
    if (cooprest::RestIsPending(&t, ra)) Fail("RestDone must free the slot");
    if (cooprest::RestTake(&t, ra, &got, d) >= 0) Fail("one post is taken once");

    /* decision 61: a 3-step slide - three takes of delta/3, READY between them, finished only after the last */
    if (cooprest::RestPost(&t, ra, anim, 30.0f, -6.0f, 0.0f, 3) != 1) Fail("a slide post must succeed");
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < 3; ++k)
    {
        const int s2 = cooprest::RestTake(&t, ra, &got, d);
        if (s2 < 0) { Fail("slide step " + I(k) + " was not taken"); return; }
        if (d[0] != 10.0f || d[1] != -2.0f || d[2] != 0.0f) Fail("a slide step must be delta / steps");
        sum[0] += d[0]; sum[1] += d[1]; sum[2] += d[2];
        const bool last = cooprest::RestDone(&t, s2);
        if (last != (k == 2)) Fail("only the third step is the last");
        if ((k < 2) != cooprest::RestIsPending(&t, ra)) Fail("a slide stays pending until its last step");
    }
    if (sum[0] != 30.0f || sum[1] != -6.0f || t.pending != 0) Fail("the steps must add up to the whole move");
    if (cooprest::RestPost(&t, ra, anim, 8.0f, 0.0f, 0.0f, 4) != 1) Fail("a second slide post must succeed");
    const int s3 = cooprest::RestTake(&t, ra, &got, d);
    cooprest::RestDone(&t, s3);
    if (cooprest::RestPost(&t, ra, anim, 1.0f, 0.0f, 0.0f, 1) != cooprest::kRestPostBusy) Fail("a post between slide steps must be refused busy");
    if (!cooprest::RestWithdraw(&t, ra) || cooprest::RestIsPending(&t, ra) || t.pending != 0) Fail("a slide can be withdrawn between steps");
    if (cooprest::RestPost(&t, ra, anim, 1.0f, 0.0f, 0.0f, 1) != cooprest::kRestPosted) Fail("once withdrawn, the ragdoll can be posted again");
    if (!cooprest::RestWithdraw(&t, ra)) Fail("the re-post must be withdrawable");

    /* R3-b item 2: a withdraw while a step is being made loses; the taker abandons (carried / inactive) - slot free */
    if (cooprest::RestPost(&t, ra, anim, 12.0f, 0.0f, 0.0f, 3) != cooprest::kRestPosted) Fail("an abandon-test post must succeed");
    const int s4 = cooprest::RestTake(&t, ra, &got, d);
    if (s4 < 0) { Fail("the abandon-test step was not taken"); return; }
    if (cooprest::RestWithdraw(&t, ra) || t.pending != 1) Fail("a withdraw while stepping must lose and leave the move");
    if (!cooprest::RestAbandon(&t, s4)) Fail("the taker must be able to abandon the move it holds");
    if (cooprest::RestIsPending(&t, ra) || t.pending != 0) Fail("an abandoned move must leave the table");
    if (cooprest::RestTake(&t, ra, &got, d) >= 0) Fail("an abandoned slide must never take another step");
    if (cooprest::RestAbandon(&t, s4) || cooprest::RestDone(&t, s4)) Fail("an abandoned slot is no longer held");
    if (cooprest::RestPost(&t, ra, anim, 1.0f, 0.0f, 0.0f, 1) != cooprest::kRestPosted || !cooprest::RestWithdraw(&t, ra) || t.pending != 0)
        Fail("after an abandon the ragdoll can be posted again");

    if (cooprest::RestPost(&t, rb, anim, 1.0f, 1.0f, 1.0f, 1) != 1) Fail("a post after a done must succeed");
    if (!cooprest::RestWithdraw(&t, rb) || cooprest::RestIsPending(&t, rb) || t.pending != 0) Fail("a withdraw must remove a READY move");
    if (cooprest::RestTake(&t, rb, &got, d) >= 0) Fail("a withdrawn move must never be taken");
    static char keys[cooprest::kRestSlots + 1];
    for (int i = 0; i < (int)cooprest::kRestSlots; ++i)
        if (cooprest::RestPost(&t, &keys[i], 0, 0.0f, 0.0f, 0.0f, 1) != 1) { Fail("slot " + I(i) + " refused before the table was full"); return; }
    if (cooprest::RestPost(&t, &keys[cooprest::kRestSlots], 0, 0.0f, 0.0f, 0.0f, 1) != 0 || t.full != 1) Fail("a full table must refuse and count it");
    if (t.pending != (long)cooprest::kRestSlots) Fail("a full table has every slot READY");
}

/* S1 (read-stats, T260). The 44 saved stat values as one 176-byte block: the offsets are the serialise set,
   a block and a MSG_STATS round-trip bit-for-bit, a truncated one is refused, and a NaN or infinite FLOAT refuses
   the whole block and names its index (the int at index 43 is not a float and is never refused). */
void t_s1_stats_block_round_trips_and_refuses_truncated_and_nonfinite()
{
    using namespace coopstats;
    if (StatsOffset(0) != 0x80 || StatsOffset(5) != 0x94 || StatsOffset(38) != 0x118) Fail("the 39 floats must sit at +0x80..+0x118 (athletics +0x94)");
    if (StatsOffset(39) != 0x120 || StatsOffset(40) != 0x124 || StatsOffset(41) != 0x13C || StatsOffset(42) != 0x194 || StatsOffset(43) != 0x198)
        Fail("attack / defence / warrior spirit / xp / free points offsets are wrong");
    if (StatsOffset(44) != 0 || StatsOffset(-1) != 0) Fail("an index out of range must read offset 0");
    int i;
    for (i = 1; i < kStatsCount; ++i) if (StatsOffset(i) <= StatsOffset(i - 1)) Fail("offset " + I(i) + " is not above the one before");
    if (kStatsBlockBytes != 176 || kStatsMsgBytes != 180) Fail("the block is 176 bytes and the message 180");

    unsigned int raw[kStatsCount];
    for (i = 0; i < kStatsFreePointsIndex; ++i) { const float f = 1.25f * (float)i - 3.0f; std::memcpy(&raw[i], &f, 4); }
    raw[kStatsFreePointsIndex] = 7u;
    std::vector<char> b;
    EncodeStatsBlock(&b, raw);
    if (b.size() != 176) { Fail("the encoded block is " + I((int)b.size()) + " bytes, not 176"); return; }
    unsigned int out[kStatsCount];
    int bad = 99;
    if (DecodeStatsBlock(&b[0], b.size(), out, &bad) != kStatsDecodeOk || bad != -1 || std::memcmp(out, raw, 176) != 0)
        Fail("a whole block did not round-trip bit-for-bit");
    std::vector<char> msg;
    EncodeStatsMsg(&msg, 3204448269u, raw);
    unsigned int uid = 0;
    if (msg.size() != 180) Fail("the encoded MSG_STATS is not 180 bytes");
    else if (DecodeStatsMsg(&msg[0], msg.size(), &uid, out, &bad) != kStatsDecodeOk || uid != 3204448269u || std::memcmp(out, raw, 176) != 0)
        Fail("a whole MSG_STATS did not round-trip");

    /* Truncated. */
    if (DecodeStatsBlock(&b[0], 175, out, &bad) != kStatsDecodeTooShort) Fail("175 bytes must be too short for a block");
    if (DecodeStatsMsg(&msg[0], 179, &uid, out, &bad) != kStatsDecodeTooShort) Fail("179 bytes must be too short for MSG_STATS");
    if (DecodeStatsBlock(0, 0, out, &bad) != kStatsDecodeTooShort) Fail("an empty block must be too short");
    /* A trailing byte is ignored. */
    std::vector<char> tail(msg); tail.push_back('X');
    if (DecodeStatsMsg(&tail[0], tail.size(), &uid, out, &bad) != kStatsDecodeOk) Fail("a trailing byte must be ignored");

    /* Non-finite, per float: NaN, +inf, -inf each refuse the block and name the index; out stays untouched. */
    const unsigned int badBits[3] = { 0x7FC00000u, 0x7F800000u, 0xFF800000u };
    const int badAt[3] = { 10, 42, 0 };
    for (i = 0; i < 3; ++i)
    {
        std::vector<char> c(b);
        std::memcpy(&c[4 * badAt[i]], &badBits[i], 4);
        unsigned int keep[kStatsCount];
        std::memset(keep, 0xAB, sizeof(keep));
        if (DecodeStatsBlock(&c[0], c.size(), keep, &bad) != kStatsDecodeBadFloat || bad != badAt[i])
            Fail("non-finite case " + I(i) + " was not refused at index " + I(badAt[i]));
        if (keep[0] != 0xABABABABu) Fail("a refused block must not write the output");
    }
    /* The int's bits may look like infinity - it is not a float. */
    std::vector<char> ib(b);
    const unsigned int infBits = 0x7F800000u;
    std::memcpy(&ib[4 * kStatsFreePointsIndex], &infBits, 4);
    if (DecodeStatsBlock(&ib[0], ib.size(), out, &bad) != kStatsDecodeOk) Fail("free attribute points is an int and must never be refused as non-finite");
}

/* S1. The owner's send rule, and the SPAWN tail: a block rides after the age; statsHas 0 reads as none; an older
   payload (nothing after the age) reads as absent; a truncated or NaN block is refused while the SPAWN itself
   still decodes. */
void t_s1_send_rule_and_spawn_stats_tail()
{
    using namespace coopstats;
    unsigned int a[kStatsCount], c[kStatsCount];
    int i;
    for (i = 0; i < kStatsFreePointsIndex; ++i) { const float f = 10.5f; std::memcpy(&a[i], &f, 4); }
    a[kStatsFreePointsIndex] = 3u;
    std::memcpy(c, a, sizeof(a));
    if (StatsWorthSending(a, c)) Fail("identical values must not be sent");
    float f = 10.54f; std::memcpy(&c[5], &f, 4);
    if (StatsWorthSending(a, c)) Fail("+0.04 inside the same whole number must not be sent");
    f = 10.625f; std::memcpy(&c[5], &f, 4);
    if (!StatsWorthSending(a, c)) Fail("+0.125 must be sent");
    f = 10.99f; std::memcpy(&a[7], &f, 4); f = 11.01f; std::memcpy(&c[7], &f, 4); std::memcpy(&c[5], &a[5], 4);
    if (!StatsWorthSending(a, c)) Fail("a whole-number change (10.99 -> 11.01) must be sent");
    std::memcpy(c, a, sizeof(a)); c[kStatsFreePointsIndex] = 4u;
    if (!StatsWorthSending(a, c)) Fail("a change of the free points must be sent");
    std::memcpy(c, a, sizeof(a)); f = -0.02f; std::memcpy(&a[2], &f, 4); f = 0.01f; std::memcpy(&c[2], &f, 4);
    if (!StatsWorthSending(a, c)) Fail("-0.02 -> 0.01 crosses a whole number and must be sent");

    std::vector<char> b;
    coopspawn::EncodeSpawn(&b, 42u, 1.0f, 2.0f, 3.0f, "Greenlander", "Holy Nation", true, 0.0f);
    const size_t base = b.size();
    coopspawn::AppendSpawnStats(&b, a);
    if (b.size() != base + 1 + 176) { Fail("the SPAWN tail is not statsHas + 176 bytes"); return; }
    coopspawn::SpawnMsg m;
    if (coopspawn::DecodeSpawn(&b[0], b.size(), &m) != coopspawn::kSpawnDecodeOk) { Fail("a SPAWN with stats did not decode"); return; }
    if (m.statsState != coopspawn::kSpawnStatsOk || std::memcmp(m.stats, a, 176) != 0) Fail("the SPAWN stats did not round-trip");
    if (m.templateName != "Greenlander" || m.factionName != "Holy Nation" || m.ageAbsent || m.age != 0.0f) Fail("the stats tail disturbed the fields before it");

    std::vector<char> none(b.begin(), b.begin() + base);
    coopspawn::AppendSpawnStats(&none, 0);
    if (none.size() != base + 1 || coopspawn::DecodeSpawn(&none[0], none.size(), &m) != coopspawn::kSpawnDecodeOk || m.statsState != coopspawn::kSpawnStatsNone)
        Fail("statsHas 0 must decode as no stats");
    std::vector<char> old(b.begin(), b.begin() + base);
    if (coopspawn::DecodeSpawn(&old[0], old.size(), &m) != coopspawn::kSpawnDecodeOk || m.statsState != coopspawn::kSpawnStatsAbsent || m.ageAbsent)
        Fail("a 48 SPAWN (nothing after the age) must decode with stats absent");
    std::vector<char> tr(b.begin(), b.end() - 1);
    if (coopspawn::DecodeSpawn(&tr[0], tr.size(), &m) != coopspawn::kSpawnDecodeOk || m.statsState != coopspawn::kSpawnStatsBad)
        Fail("a truncated stats block must be refused while the SPAWN still decodes");
    std::vector<char> nan(b);
    const unsigned int nanBits = 0x7FC00000u;
    std::memcpy(&nan[base + 1 + 4 * 20], &nanBits, 4);
    if (coopspawn::DecodeSpawn(&nan[0], nan.size(), &m) != coopspawn::kSpawnDecodeOk || m.statsState != coopspawn::kSpawnStatsBad || m.statsBadIndex != 20)
        Fail("a NaN in the SPAWN stats must be refused at index 20");
}

/* quality1: the CLOTHING details block - round trip, absent (older sender / other marker), refused when cut or out of range,
   and the encoder's clamps. */
void t_quality1_garment_details_block()
{
    std::vector<coopgarment::GarmentDetail> d(2);
    d[0].quality = 0.73f; d[0].charges = 12.5f; d[0].quantity = 1; d[0].colorSid = "";
    d[1].quality = 1.0f;  d[1].charges = 0.0f;  d[1].quantity = 37; d[1].colorSid = "1234-gamedata.base";
    d[1].functionKind = 5; d[1].unique = 1;
    std::vector<char> b(10, 'x');   /* stands for the item list before it */
    coopgarment::EncodeGarmentDetails(&b, d);
    std::vector<coopgarment::GarmentDetail> o;
    if (coopgarment::DecodeGarmentDetails(&b[0], b.size(), 10, 2, &o) != coopgarment::kDetailOk || o.size() != 2
        || o[0].quality != 0.73f || o[0].charges != 12.5f || o[1].quantity != 37 || o[1].colorSid != "1234-gamedata.base"
        || o[1].functionKind != 5 || o[1].unique != 1 || o[0].unique != 0)
        Fail("a details block did not round-trip");
    /* Absent: nothing after the list, or a different marker. */
    if (coopgarment::DecodeGarmentDetails(&b[0], 10, 10, 2, &o) != coopgarment::kDetailAbsent) Fail("no block must read as absent");
    std::vector<char> m = b; m[10] = 'Z';
    if (coopgarment::DecodeGarmentDetails(&m[0], m.size(), 10, 2, &o) != coopgarment::kDetailAbsent) Fail("another marker must read as absent");
    /* Refused: cut short, quantity 0, NaN quality, a colour length past the end. */
    if (coopgarment::DecodeGarmentDetails(&b[0], b.size() - 1, 10, 2, &o) != coopgarment::kDetailBad) Fail("a cut block must be refused");
    std::vector<char> z = b; { const unsigned int zero = 0; std::memcpy(&z[14 + 8], &zero, 4); }
    if (coopgarment::DecodeGarmentDetails(&z[0], z.size(), 10, 2, &o) != coopgarment::kDetailBad) Fail("quantity 0 must be refused");
    std::vector<char> n = b; { const unsigned int nan = 0x7FC00000u; std::memcpy(&n[14], &nan, 4); }
    if (coopgarment::DecodeGarmentDetails(&n[0], n.size(), 10, 2, &o) != coopgarment::kDetailBad) Fail("a NaN quality must be refused");
    std::vector<char> l = b; { const unsigned int big = 97; std::memcpy(&l[14 + 17], &big, 4); }
    if (coopgarment::DecodeGarmentDetails(&l[0], l.size(), 10, 2, &o) != coopgarment::kDetailBad) Fail("a 97-byte colour must be refused");
    /* The encoder clamps rather than writing something the decoder would refuse. */
    std::vector<coopgarment::GarmentDetail> c(1);
    c[0].quantity = 0; c[0].colorSid = std::string(200, 'c'); { const unsigned int inf = 0x7F800000u; std::memcpy(&c[0].quality, &inf, 4); }
    std::vector<char> cb;
    coopgarment::EncodeGarmentDetails(&cb, c);
    if (coopgarment::DecodeGarmentDetails(&cb[0], cb.size(), 0, 1, &o) != coopgarment::kDetailOk || o[0].quantity != 1 || o[0].quality != 0.0f
        || o[0].colorSid.size() != 96)
        Fail("the encoder must clamp quantity, a non-finite float and a long colour");
}

/* inv3a (inv3-backpack-design.md D5, phase 1): the BAG1 block a handed-over backpack's contents ride in - round trip, absent,
   truncated at every length, wrong marker, the 256-row cap (257 refused by the encoder and by the decoder, never truncated),
   rows out of range, and the order-free digest `bagtest list` prints. */
/* bag23 part 2 (inv3 phase 3): the pack path block (BAGP) - round trip at an offset, every cut, absent, out of range both ways; and an
   ITEM_MOVE op-0 tail leaves a trailing BAGP for its caller. */
void t_bag23_path_block()
{
    coopbag::BagPath p;
    p.has = 1; p.section = "backpack_attach"; p.x = 1; p.y = 2; p.sid = "123-gamedata.base"; p.packSid = "456-gamedata.base";
    std::vector<char> b;
    b.push_back('x');
    if (!coopbag::EncodeBagPath(&b, p)) { Fail("a path did not encode"); return; }
    coopbag::BagPath o; size_t end = 0;
    if (coopbag::DecodeBagPath(&b[0], b.size(), 1, &o, &end) != coopbag::kBagOk || end != b.size() || o.has != 1
        || o.section != p.section || o.x != 1 || o.y != 2 || o.sid != p.sid || o.packSid != p.packSid) Fail("the path did not round-trip");
    if (!coopbag::BagPathAt(&b[0], b.size(), 1) || coopbag::BagPathAt(&b[0], b.size(), 0)) Fail("the marker test is wrong");
    for (size_t cut = 1; cut < b.size(); ++cut)
    {
        coopbag::BagPath q; size_t e2 = 0;
        const int w = coopbag::DecodeBagPath(&b[0], cut, 1, &q, &e2);
        if (cut == 1 ? (w != coopbag::kBagAbsent) : (w != coopbag::kBagBad)) { Fail("a cut path was not refused (or absent read as bad)"); return; }
    }
    { coopbag::BagPath none; std::vector<char> c; if (!coopbag::EncodeBagPath(&c, none) || !c.empty()) Fail("no path must write nothing"); }
    { coopbag::BagPath q = p; q.x = coopbag::kBagMaxCell; std::vector<char> c; if (coopbag::EncodeBagPath(&c, q) || !c.empty()) Fail("a cell out of range must write nothing"); }
    { coopbag::BagPath q = p; q.section.clear(); std::vector<char> c; if (coopbag::EncodeBagPath(&c, q) || !c.empty()) Fail("an empty section must write nothing"); }
    {
        std::vector<char> c;
        coopbag::BagPutU32(&c, coopbag::kBagPathMarker); coopbag::BagPutStr(&c, "backpack_attach");
        coopbag::BagPutU32(&c, 5000); coopbag::BagPutU32(&c, 0); coopbag::BagPutStr(&c, "");
        coopbag::BagPath q; size_t e3 = 0;
        if (coopbag::DecodeBagPath(&c[0], c.size(), 0, &q, &e3) != coopbag::kBagBad) Fail("a decoded cell out of range must be refused");
    }
    {   /* the op-0 add tail: [BAG1][BAGP] and a lone [BAGP] - the tail ends before the path */
        std::vector<coopbag::BagRow> rows(1);
        rows[0].section = "backpack_content"; rows[0].x = 0; rows[0].y = 0; rows[0].qty = 2; rows[0].baseSid = "bread";
        std::vector<char> c;
        if (!coopbag::EncodeBagRows(&c, rows)) { Fail("rows did not encode"); return; }
        const size_t rowsEnd = c.size();
        coopbag::EncodeBagPath(&c, p);
        std::vector<coopbag::BagRow> gotRows; coopmark::OwnerId own; size_t te = 0;
        if (coopistat::DecodeAddTail(&c[0], c.size(), 0, &gotRows, &own, &te) != coopistat::kTailOk || te != rowsEnd || gotRows.size() != 1)
            Fail("an add tail followed by a path did not stop before it");
        std::vector<char> d; coopbag::EncodeBagPath(&d, p);
        if (coopistat::DecodeAddTail(&d[0], d.size(), 0, &gotRows, &own, &te) != coopistat::kTailOk || te != 0 || !gotRows.empty())
            Fail("a lone path after an add did not read as an empty tail");
        std::vector<char> g(c.begin(), c.begin() + rowsEnd); g.push_back(1); g.push_back(2); g.push_back(3); g.push_back(4);
        if (coopistat::DecodeAddTail(&g[0], g.size(), 0, &gotRows, &own, &te) != coopistat::kTailTrailing) Fail("garbage after the rows must still be trailing");
    }
}

/* P94 / P109: a holder answers from its own contents unless the box is a non-player box of a sector it had loaded as the
   non-holder and it neither received the holder's contents nor stocked the box itself since. */
void t_p94_parity_serve_trusted()
{
    if (cooppar::ParityServeTrusted(0, 0, 0, 0) != 1) Fail("a sector this game never had as the non-holder must be answered");
    if (cooppar::ParityServeTrusted(0, 1, 0, 0) != 0) Fail("a never-received box of a sector once held elsewhere must not be answered");
    if (cooppar::ParityServeTrusted(0, 1, 1, 0) != 1) Fail("a box received from the holder must be answered");
    if (cooppar::ParityServeTrusted(0, 1, 0, 1) != 1) Fail("a box this game stocked itself must be answered");
    if (cooppar::ParityServeTrusted(1, 1, 0, 0) != 1) Fail("a player box stays under the owner rules");
    int n = 0;
    for (int m = 0; m < 16; ++m) n += cooppar::ParityServeTrusted(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1);
    if (n != 15) Fail("exactly one of the sixteen cases is refused");
}

/* The ground listing on PARITY_REQ (the PRQG tail, after the PRQK kind tail): an ask with no listing keeps its bytes, a listing
   round-trips alone or beside each kind with each row's quantity, quality and road flag and the count left out at the cap, a
   listing of no key is still a ground ask, and a cut, misplaced, oversized, empty-keyed or wrongly flagged listing is refused. */
void t_t481_parity_ground_tail()
{
    cooppar::ParityReq r; r.sx = 5; r.sy = 6;
    cooppar::ParityReq o;
    std::vector<char> plain;
    if (!cooppar::EncodeParityReq(&plain, r) || plain.size() != 10) { Fail("an ask with no box and no listing is ten bytes"); return; }
    if (cooppar::DecodeParityReq(&plain[0], plain.size(), &o) != cooppar::kParityDecodeOk || o.ground != 0 || !o.groundKeys.empty() || o.groundLeft != 0)
        Fail("an ask with no listing is not a ground ask");
    r.ground = 1;
    std::vector<char> g0;
    if (!cooppar::EncodeParityReq(&g0, r) || g0.size() != 10 + 4 + 2 + 2) { Fail("a listing of no key is the PRQG mark, a zero count and a zero left-out count"); return; }
    if (cooppar::DecodeParityReq(&g0[0], g0.size(), &o) != cooppar::kParityDecodeOk || o.ground != 1 || !o.groundKeys.empty()
        || !o.boxes.empty() || o.sx != 5 || o.sy != 6 || o.groundLeft != 0)
        Fail("a listing of no key decodes as a ground ask");
    cooppar::ParityBoxRow row; row.key = "box@1,2@3,4,5"; row.digest = 7; row.items = 1;
    r.boxes.push_back(row);
    cooppar::ParityGroundRow g1; g1.key = "G|sword@5,6@10,20,30"; g1.qty = 1; g1.q100 = 75; g1.road = 1;
    cooppar::ParityGroundRow g2; g2.key = "G|ration@5,6@-4,8,2"; g2.qty = 12; g2.q100 = -1; g2.road = 0;
    r.groundKeys.push_back(g1); r.groundKeys.push_back(g2);
    r.groundLeft = 300;
    const size_t boxEnd = 10 + 1 + row.key.size() + 8;
    for (int k = 0; k <= 2; ++k)
    {
        r.kind = k;
        std::vector<char> b;
        if (!cooppar::EncodeParityReq(&b, r)) { Fail("a listing beside each kind encodes"); continue; }
        const size_t kindEnd = boxEnd + (k == 0 ? 0 : 5);
        if (b.size() != kindEnd + 8 + (1 + g1.key.size() + 9) + (1 + g2.key.size() + 9))
            Fail("the listing is the mark, a count, a left-out count and a key, quantity, quality and road flag per item, after the kind tail");
        if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeOk || o.kind != k || o.ground != 1 || o.boxes.size() != 1
            || o.boxes[0].kind != k || o.boxes[0].key != row.key || o.groundKeys.size() != 2 || o.groundKeys[0].key != g1.key
            || o.groundKeys[0].qty != 1 || o.groundKeys[0].q100 != 75 || o.groundKeys[0].road != 1
            || o.groundKeys[1].key != g2.key || o.groundKeys[1].qty != 12 || o.groundKeys[1].q100 != -1 || o.groundKeys[1].road != 0
            || o.groundLeft != 300)
            Fail("the listing round-trips beside the kind tail");
        for (size_t cut = b.size() - 1; cut > kindEnd; --cut)
            if (cooppar::DecodeParityReq(&b[0], cut, &o) == cooppar::kParityDecodeOk) { Fail("a cut listing was accepted"); break; }
    }
    r.kind = 0;
    {   /* any road flag that is not 0 is sent as 1; a left-out count past 65535 is sent as 65535 */
        cooppar::ParityReq f = r;
        f.groundKeys[1].road = 7; f.groundLeft = 70000;
        std::vector<char> b;
        if (!cooppar::EncodeParityReq(&b, f) || cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeOk
            || o.groundKeys[1].road != 1 || o.groundLeft != 65535)
            Fail("a road flag is sent as 0 or 1 and the left-out count is capped at 65535");
    }
    {   /* the kind tail AFTER the listing is not the layout */
        std::vector<char> b;
        if (!cooppar::EncodeParityReq(&b, r)) Fail("a plain listing encodes");
        else
        {
            const char t[5] = { 'P', 'R', 'Q', 'K', 1 };
            b.insert(b.end(), t, t + 5);
            if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeTrailing) Fail("a kind tail after the listing is refused");
        }
    }
    {
        cooppar::ParityReq big = r;
        big.groundKeys.assign(cooppar::kParityMaxGroundKeys + 1, g1);
        std::vector<char> b;
        if (cooppar::EncodeParityReq(&b, big) || !b.empty()) Fail("a listing over the cap is refused at encode");
        big.groundKeys.resize(cooppar::kParityMaxGroundKeys);
        b.clear();
        if (!cooppar::EncodeParityReq(&b, big)) Fail("a listing at the cap encodes");
    }
    {
        std::vector<char> b = plain;
        const char t[8] = { 'P', 'R', 'Q', 'G', (char)0x01, (char)0x01, 0, 0 };   /* a count of 257 */
        b.insert(b.end(), t, t + 8);
        if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeTooMany) Fail("a listing count over the cap on the wire is refused");
    }
    {
        cooppar::ParityReq e = r;
        e.groundKeys[1].key = "";
        std::vector<char> b;
        if (cooppar::EncodeParityReq(&b, e) || !b.empty()) Fail("an empty ground key is refused at encode");
    }
    {
        std::vector<char> b = plain;
        const char t[18] = { 'P', 'R', 'Q', 'G', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 };   /* one row: an empty key, quantity 1, road */
        b.insert(b.end(), t, t + 18);
        if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeBadKey) Fail("an empty ground key on the wire is refused");
    }
    {
        std::vector<char> b = plain;
        const char t[19] = { 'P', 'R', 'Q', 'G', 1, 0, 0, 0, 1, 'a', 1, 0, 0, 0, 0, 0, 0, 0, 2 };   /* one row: road flag 2 */
        b.insert(b.end(), t, t + 19);
        if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeBadFlag) Fail("a road flag that is neither 0 nor 1 on the wire is refused");
        b[b.size() - 1] = 1;
        if (cooppar::DecodeParityReq(&b[0], b.size(), &o) != cooppar::kParityDecodeOk || o.groundKeys.size() != 1 || o.groundKeys[0].road != 1)
            Fail("the same row with road flag 1 decodes");
    }
}

/* holder rows for ParityGroundPlan: one per key with its road flag (a flag missing past the end reads 0), quantity 1, quality 0 */
std::vector<cooppar::ParityGroundRow> T481HolderRows(const std::vector<std::string>& keys, const std::vector<unsigned char>& road)
{
    std::vector<cooppar::ParityGroundRow> out;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        cooppar::ParityGroundRow g; g.key = keys[i]; g.qty = 1; g.q100 = 0; g.road = (i < road.size() && road[i] != 0) ? 1 : 0;
        out.push_back(g);
    }
    return out;
}

/* A ground key in sector (5, 6) at flat position (x10, z10) tenths, height 0 - sector 5 spans x10 -1244160 .. -1198080, sector 6
   spans z10 -1198080 .. -1152000 (poskey.h's 4608-unit grid in tenths) */
std::string T697WKey(const char* sid, long x10, long z10)
{
    char b[128];
    sprintf_s(b, sizeof b, "G|%s@5,6@%ld,%ld,0", sid, x10, z10);
    return std::string(b);
}
/* The holder's world loot the asker's listing lacks: with the holder's ground the authority (holderWhole) and a whole listing it is
   in *addWorld (never in *add) - the asker's copy that lies elsewhere in the sector is a GONE in the same plan, so both games end
   with the holder's copy; never with holderWhole 0, a cut listing, a paired copy, or within 5 units of the sector's edge. */
/* The asker's listing counts as cut (groundLeft != 0, so the holder sends no ADD) when items were left out at the cap, when keyless
   items lie in the sector, or when the ground walk itself was cut; clamped to 16 bits. */
void t_t697_listing_left()
{
    if (cooppar::ParityListingLeft(0, 0, 0) != 0) Fail("T-697: a whole walk with every item listed is a whole listing");
    if (cooppar::ParityListingLeft(3, 0, 0) != 3) Fail("T-697: the items left out at the cap");
    if (cooppar::ParityListingLeft(0, 2, 0) != 2) Fail("T-697: keyless items in the sector count as left out");
    if (cooppar::ParityListingLeft(3, 2, 0) != 5) Fail("T-697: both add up");
    if (cooppar::ParityListingLeft(0, 0, 1) != 1) Fail("T-697: a cut walk makes the listing cut even with nothing left out");
    if (cooppar::ParityListingLeft(4, 0, 1) != 4) Fail("T-697: a cut walk keeps the real count");
    if (cooppar::ParityListingLeft(0xFFFFu, 5, 1) != 0xFFFFu) Fail("T-697: clamped to the wire's 16 bits");
    std::vector<cooppar::ParityGroundRow> holder(1), none;
    holder[0].key = "G|plate@5,6@-1220000,-1175000,0"; holder[0].qty = 1; holder[0].q100 = 0; holder[0].road = 0;
    std::vector<size_t> add, gone, gu, aw;
    cooppar::ParityGroundPlan(holder, none, cooppar::ParityListingLeft(0, 0, 1) != 0 ? 1 : 0, &add, &gone, 0, 1, &gu, &aw);
    if (!aw.empty()) Fail("T-697: a listing from a cut walk gets no world ADD");
    {   /* an incomplete listing: no UNMARKED GONE; a road (marked) GONE keeps its rule */
        std::vector<cooppar::ParityGroundRow> asked;
        cooppar::ParityGroundRow r; r.qty = 1; r.q100 = 0;
        r.road = 0; r.key = "G|ration@5,6@-1210000,-1170000,0"; asked.push_back(r);   /* 0: unmarked, the holder has none */
        r.road = 1; r.key = "G|club@5,6@-1211000,-1170000,0"; asked.push_back(r);     /* 1: road, the holder has none */
        cooppar::ParityGroundPlan(holder, asked, 1, &add, &gone, 0, 1, &gu, &aw);
        if (!gu.empty() || gone.size() != 1 || gone[0] != 1) Fail("T-697: an incomplete listing - no unmarked GONE, the road GONE stays");
        cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone, 0, 1, &gu, &aw);
        if (gu.size() != 1 || gu[0] != 0 || gone.size() != 2) Fail("T-697: a whole listing - the unmarked GONE and the road GONE");
        cooppar::ParityGroundPlan(holder, asked, 1, &add, &gone, 0, 0, &gu, &aw);
        if (!gu.empty() || gone.size() != 1 || gone[0] != 1) Fail("T-697: not the authority and incomplete - the road GONE alone, as before");
    }
}
void t_t697_catchup_world_add()
{
    if (cooppar::kParitySectorTenths != (long)(poskey::kSectorSize * 10.0f) || cooppar::kParitySectorOriginTenths != (long)(poskey::kSectorOrigin * 10.0f))
        Fail("T-697: the planner's sector grid in tenths is poskey.h's");
    const long left = 5 * 46080L - 1474560L, top = 6 * 46080L - 1474560L;
    const long cx = left + 23040, cz = top + 23040;   /* the sector's centre */
    coopground::GroundKeyParts kp;
    std::memset(&kp, 0, sizeof kp);
    if (coopground::GroundKeyParse(T697WKey("plate", cx, cz).c_str(), &kp) != 1 || kp.x10 != cx || kp.z10 != cz) Fail("T-697: the test key parses");
    if (cooppar::ParityGroundNearEdge(kp, 50) != 0) Fail("T-697: the centre is not near an edge");
    kp.x10 = left + 49; if (cooppar::ParityGroundNearEdge(kp, 50) != 1) Fail("T-697: 4.9 units from the west edge is near");
    kp.x10 = left + 50; if (cooppar::ParityGroundNearEdge(kp, 50) != 0) Fail("T-697: 5 units from the west edge is not near");
    kp.x10 = cx; kp.z10 = top + 46080 - 10; if (cooppar::ParityGroundNearEdge(kp, 50) != 1) Fail("T-697: 1 unit from the south edge is near");
    kp.z10 = top - 10; if (cooppar::ParityGroundNearEdge(kp, 50) != 1) Fail("T-697: a position outside its key's sector counts as near");

    std::vector<cooppar::ParityGroundRow> holder, asked;
    cooppar::ParityGroundRow h; h.qty = 1; h.q100 = 0;
    h.road = 0; h.key = T697WKey("plate", cx, cz); holder.push_back(h);                 /* 0: world loot, the asker's copy is 1 unit away */
    h.road = 0; h.key = T697WKey("ration", cx + 500, cz); holder.push_back(h);         /* 1: world loot, listed as is: paired */
    h.road = 0; h.key = T697WKey("axe", left + 20, cz); holder.push_back(h);           /* 2: world loot 2 units from the edge: never sent */
    h.road = 1; h.key = T697WKey("sword", cx + 900, cz); holder.push_back(h);          /* 3: a road item not listed: the plain ADD */
    cooppar::ParityGroundRow a; a.qty = 1; a.q100 = 0; a.road = 0;
    a.key = T697WKey("plate", cx + 10, cz); asked.push_back(a);                         /* 0: the asker's own copy, 1 unit off: GONE */
    a.key = T697WKey("ration", cx + 500, cz); asked.push_back(a);                       /* 1: pairs with holder 1 */
    std::vector<size_t> add, gone, gu, aw;
    cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone, 0, 1, &gu, &aw);
    if (add.size() != 1 || add[0] != 3) Fail("T-697: world loot never goes in the plain ADDs");
    if (gone.size() != 1 || gone[0] != 0 || gu.size() != 1 || gu[0] != 0) Fail("T-697: the asker's copy 1 unit off is an unmarked GONE");
    if (aw.size() != 1 || aw[0] != 0) Fail("T-697: the holder's copy is the world ADD - not the paired one, not the one by the edge");
    cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone, 0, 0, &gu, &aw);
    if (!aw.empty() || !gone.empty()) Fail("T-697: with the holder's ground not the authority, no world ADD and no unmarked GONE");
    cooppar::ParityGroundPlan(holder, asked, 1, &add, &gone, 0, 1, &gu, &aw);
    if (!aw.empty() || !add.empty()) Fail("T-697: a cut listing gets no world ADD (the asker may have the copy it left out)");
    aw.push_back(7);
    cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone, 0, 1, &gu);
    if (aw.size() != 1 || add.size() != 1) Fail("T-697: without the world output nothing else changes");
    std::vector<cooppar::ParityGroundRow> none;
    cooppar::ParityGroundPlan(holder, none, 0, &add, &gone, 0, 1, &gu, &aw);
    if (aw.size() != 2 || aw[0] != 0 || aw[1] != 1) Fail("T-697: an empty whole listing gets every inner world-loot item");
}
/* The holder's catch-up plan: a listed key pairs with the same key (any holder item), else the best same-sid holder road item
   within 0.5 units horizontally, else the best other holder item there; the holder's unpaired road items are ADDs (world loot
   never), the listing's unpaired ROAD rows are GONEs, a key that is not a ground key is neither. */
void t_t481_ground_catch_up_plan()
{
    std::vector<std::string> h;
    h.push_back("G|sword@5,6@10,20,30");       /* 0: listed as is */
    h.push_back("G|ration@5,6@100,100,0");     /* 1: the listed ration is 0.6 units away - not paired: ADD */
    h.push_back("G|axe@5,6@2003,2001,555");    /* 2: listed at another height, 0.3 units off: paired */
    h.push_back("G|axe@5,6@3000,3000,0");      /* 3: not listed: ADD */
    h.push_back("G|sword@5,6@10,20,30");       /* 4: a second item of the same key, listed once: ADD */
    std::vector<cooppar::ParityGroundRow> a;
    cooppar::ParityGroundRow g; g.road = 1;
    g.key = "G|sword@5,6@10,20,30"; g.qty = 1; a.push_back(g);     /* 0 */
    g.key = "G|axe@5,6@2000,2000,100"; a.push_back(g);              /* 1 */
    g.key = "G|club@5,6@50,50,0"; a.push_back(g);                   /* 2: the holder has none: GONE */
    g.key = "box@1,2@3,4,5"; a.push_back(g);                        /* 3: not a ground key: never GONE */
    g.key = "G|ration@5,6@106,100,0"; g.qty = 12; a.push_back(g);   /* 4: GONE */
    std::vector<size_t> add, gone;
    const std::vector<cooppar::ParityGroundRow> hRows = T481HolderRows(h, std::vector<unsigned char>(h.size(), 1));   /* every holder item a road item */
    if (cooppar::ParityGroundPlan(hRows, a, 0, &add, &gone) != 0) Fail("a whole listing holds no ADD back");
    if (add.size() != 3 || add[0] != 1 || add[1] != 3 || add[2] != 4) Fail("the holder's unpaired items are the ADDs");
    if (gone.size() != 2 || gone[0] != 2 || gone[1] != 4) Fail("the listing's unpaired ground keys are the GONEs");
    std::vector<cooppar::ParityGroundRow> none;
    cooppar::ParityGroundPlan(hRows, none, 0, &add, &gone);
    if (add.size() != h.size() || !gone.empty()) Fail("an empty listing gets every holder item as an ADD");
    std::vector<cooppar::ParityGroundRow> empty;
    cooppar::ParityGroundPlan(empty, a, 0, &add, &gone);
    if (!add.empty() || gone.size() != 4) Fail("a holder with nothing there sends a GONE for every listed road ground key");
    {   /* an UNMARKED listed row the holder lacks is a GONE only when the holder's ground is the authority (holderWhole) */
        std::vector<cooppar::ParityGroundRow> u = a;
        cooppar::ParityGroundRow w; w.road = 0; w.qty = 1;
        w.key = "G|plate@5,6@700,700,0"; u.push_back(w);   /* 5: unmarked, unpaired (picked before a restart) */
        w.key = "G|sword@5,6@10,20,30"; u.push_back(w);    /* 6: unmarked, pairs with the holder's second sword (4) */
        std::vector<size_t> gu;
        cooppar::ParityGroundPlan(hRows, u, 0, &add, &gone, 0, 1, &gu);
        if (gu.size() != 1 || gu[0] != 5) Fail("T-697 fold 5: an unmarked unpaired row with a whole walk is a GONE");
        if (gone.size() != 3 || gone[0] != 2 || gone[1] != 4 || gone[2] != 5) Fail("T-697 fold 5: the road GONEs stay, the unmarked one is added");
        cooppar::ParityGroundPlan(hRows, u, 0, &add, &gone, 0, 0, &gu);
        if (!gu.empty() || gone.size() != 2 || gone[0] != 2 || gone[1] != 4) Fail("T-697 fold 5: with a cut walk an unmarked row is kept (as before)");
        cooppar::ParityGroundPlan(hRows, u, 0, &add, &gone, 0, 1, 0);
        if (gone.size() != 3) Fail("T-697 fold 5: the unmarked output may be absent");
        std::vector<cooppar::ParityGroundRow> wl; w.key = "box@1,2@3,4,5"; wl.push_back(w);   /* not a ground key: never */
        cooppar::ParityGroundPlan(empty, wl, 0, &add, &gone, 0, 1, &gu);
        if (!gone.empty() || !gu.empty()) Fail("T-697 fold 5: a key that is not a ground key is never a GONE");
    }
    std::vector<cooppar::ParityGroundRow> mace;
    g.key = "G|mace@5,6@10,20,30"; mace.push_back(g);
    std::vector<std::string> one(1, std::string("G|sword@5,6@10,20,30"));
    cooppar::ParityGroundPlan(T481HolderRows(one, std::vector<unsigned char>(1, 1)), mace, 0, &add, &gone);
    if (add.size() != 1 || gone.size() != 1) Fail("items of another sid at the same spot never pair");
    /* world loot: never an ADD, but a listed item lying where the holder has a world item of its sid is never a GONE; a listed item
       pairs with the holder's near road item before a nearer world item, and with an item of its exact key before either */
    {
        std::vector<std::string> wh;
        std::vector<unsigned char> wr;
        wh.push_back("G|sword@5,6@10,20,30");   wr.push_back(0);   /* 0: world loot, nothing listed near it: not an ADD */
        wh.push_back("G|axe@5,6@500,500,0");    wr.push_back(0);   /* 1: world loot under the listed axe (0.2 flat, another height) */
        wh.push_back("G|club@5,6@900,900,0");   wr.push_back(1);   /* 2: road, 0.4 units from the listed club: paired */
        wh.push_back("G|club@5,6@897,900,0");   wr.push_back(0);   /* 3: world, 0.1 units from the listed club - nearer, not paired */
        wh.push_back("G|mace@5,6@50,50,0");     wr.push_back(1);   /* 4: road, not listed: ADD */
        const std::vector<cooppar::ParityGroundRow> whRows = T481HolderRows(wh, wr);
        std::vector<cooppar::ParityGroundRow> wa;
        cooppar::ParityGroundRow w; w.qty = 1; w.road = 1;
        w.key = "G|axe@5,6@502,500,140";  wa.push_back(w);   /* 0: pairs with the world axe: no GONE */
        w.key = "G|club@5,6@896,900,0";   wa.push_back(w);   /* 1: pairs with the road club */
        w.key = "G|ration@5,6@300,300,0"; wa.push_back(w);   /* 2: nothing of the holder's there: GONE */
        cooppar::ParityGroundPlan(whRows, wa, 0, &add, &gone);
        if (add.size() != 1 || add[0] != 4) Fail("only the holder's unpaired road items are ADDs - world loot never");
        if (gone.size() != 1 || gone[0] != 2) Fail("a listed item where the holder has an item of its sid, road or world, is never a GONE");
        wa.resize(1);
        w.key = "G|club@5,6@897,900,0"; wa.push_back(w);   /* the exact key of the world club, 0.3 units from the road club */
        cooppar::ParityGroundPlan(whRows, wa, 0, &add, &gone);
        if (add.size() != 2 || add[0] != 2 || add[1] != 4 || !gone.empty())
            Fail("a listed key pairs with the holder's item of its exact key, world loot too, before a near road item - the road item is then an ADD");
        std::vector<cooppar::ParityGroundRow> wnone;
        std::vector<unsigned char> twoRoad(2, 1);
        cooppar::ParityGroundPlan(T481HolderRows(wh, twoRoad), wnone, 0, &add, &gone);
        if (add.size() != 2 || add[0] != 0 || add[1] != 1) Fail("a holder row's road flag alone makes it an ADD candidate");
    }
    /* the settle step: settled once two consecutive polls count the same, and it stays settled */
    int last = 0; unsigned char st = 0;
    if (cooppar::GroundSettleStep(&last, &st, 0) != 0 || st != 1) Fail("the first poll only records the count");
    if (cooppar::GroundSettleStep(&last, &st, 3) != 0 || st != 1 || last != 3) Fail("a count that changed is recorded, not settled");
    if (cooppar::GroundSettleStep(&last, &st, 7) != 0 || last != 7) Fail("a count still rising is not settled");
    if (cooppar::GroundSettleStep(&last, &st, 7) != 1 || st != 2) Fail("the same count on two consecutive polls is settled");
    if (cooppar::GroundSettleStep(&last, &st, 9) != 1 || st != 2) Fail("a settled sector stays settled until its record is cleared");
    st = 0;
    if (cooppar::GroundSettleStep(&last, &st, 9) != 0) Fail("a cleared record counts again from its first poll");
    if (cooppar::GroundSettleStep(0, &st, 1) != 0) Fail("no record: never settled");
}

/* The plan when the asker lists items that are not road: a game started again lists the items it got by the road (and saved) as
   not road at its own heights - they pair with the holder's road items (no ADD) and are never sent a GONE; a listing cut at its
   cap gets no ADD at all (GONEs still go); the flat-near match ranks quantity and quality, then height difference, then flat
   distance; and the ranking helpers themselves. */
void t_t481_ground_catch_up_rejoin()
{
    std::vector<size_t> add, gone;
    std::vector<cooppar::ParityGroundRow> holder;
    cooppar::ParityGroundRow x; x.qty = 1; x.road = 1;
    x.key = "G|sword@5,6@100,100,500"; holder.push_back(x);   /* X1: a road item on the holder */
    x.key = "G|axe@5,6@300,300,700";   holder.push_back(x);   /* X2: a road item on the holder */
    x.key = "G|ration@5,6@50,50,10"; x.road = 0; holder.push_back(x);   /* the world's own ration, on both games */
    std::vector<cooppar::ParityGroundRow> asked;
    cooppar::ParityGroundRow y; y.qty = 1; y.road = 0;
    y.key = "G|sword@5,6@101,100,120"; asked.push_back(y);   /* X1 saved and loaded again: another height, not road */
    y.key = "G|axe@5,6@300,302,90";    asked.push_back(y);   /* X2 likewise */
    y.key = "G|ration@5,6@50,50,10";   asked.push_back(y);   /* the world's ration */
    y.key = "G|club@5,6@700,700,0";    asked.push_back(y);   /* the asker's own world loot the holder no longer has */
    if (cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone) != 0 || !add.empty() || !gone.empty())
        Fail("a game started again: its saved road items listed as not road pair with the holder's road items - no ADD, no GONE");
    {   /* the same items listed as road: the same pairing, and only the club, which the holder lacks, is a GONE */
        std::vector<cooppar::ParityGroundRow> roadAsked = asked;
        for (size_t j = 0; j < roadAsked.size(); ++j) roadAsked[j].road = 1;
        cooppar::ParityGroundPlan(holder, roadAsked, 0, &add, &gone);
        if (!add.empty() || gone.size() != 1 || gone[0] != 3) Fail("a listed road row the holder lacks is a GONE");
    }
    {   /* a cut listing: no ADD (X2 may be one left out), the count held back returned; a listed road row the holder lacks is still a GONE */
        std::vector<cooppar::ParityGroundRow> cutAsked(1, asked[0]);
        y.key = "G|mace@5,6@900,900,0"; y.road = 1; cutAsked.push_back(y);
        if (cooppar::ParityGroundPlan(holder, cutAsked, 1, &add, &gone) != 1 || !add.empty() || gone.size() != 1 || gone[0] != 1)
            Fail("a cut listing gets no ADD and its unpaired road rows still get a GONE");
        if (cooppar::ParityGroundPlan(holder, cutAsked, 0, &add, &gone) != 0 || add.size() != 1 || add[0] != 1)
            Fail("the same listing whole gets the unpaired road item as an ADD");
    }
    {   /* the near order: quantity and quality before distance, height difference before flat distance */
        std::vector<cooppar::ParityGroundRow> hr;
        cooppar::ParityGroundRow c; c.road = 1; c.q100 = 50;
        c.key = "G|axe@5,6@1000,1000,100"; c.qty = 1; hr.push_back(c);   /* 0: 0.1 flat, the wrong quantity */
        c.key = "G|axe@5,6@1004,1000,100"; c.qty = 5; hr.push_back(c);   /* 1: 0.3 flat, the right quantity and quality */
        c.key = "G|bow@5,6@2000,2000,100"; c.qty = 1; hr.push_back(c);   /* 2: 0.1 flat, 29 units lower */
        c.key = "G|bow@5,6@2004,2000,400"; c.qty = 1; hr.push_back(c);   /* 3: 0.3 flat, 1 unit higher */
        std::vector<cooppar::ParityGroundRow> la;
        cooppar::ParityGroundRow l; l.road = 1; l.q100 = 50;
        l.key = "G|axe@5,6@1001,1000,300"; l.qty = 5; la.push_back(l);
        l.key = "G|bow@5,6@2001,2000,390"; l.qty = 1; la.push_back(l);
        cooppar::ParityGroundPlan(hr, la, 0, &add, &gone);
        if (add.size() != 2 || add[0] != 0 || add[1] != 2 || !gone.empty())
            Fail("the near match takes the quantity and quality match, then the smaller height difference, before the flat distance");
    }
    {   /* the helpers */
        coopground::GroundKeyParts p, q;
        std::memset(&p, 0, sizeof p); std::memset(&q, 0, sizeof q);
        if (coopground::GroundKeyParse("G|axe@5,6@10,20,30", &p) != 1 || coopground::GroundKeyParse("G|axe@5,6@10,20,-45", &q) != 1
            || coopground::GroundKeyDy(p, q) != 75 || coopground::GroundKeyDy(q, p) != 75)
            Fail("the height difference is the y tenths apart, never negative");
        if (coopground::GroundBagRank(3, 50, 3, 50) != 0 || coopground::GroundBagRank(3, 40, 3, 50) != 1
            || coopground::GroundBagRank(2, 50, 3, 50) != 2 || coopground::GroundBagRank(2, 40, 3, 50) != 3)
            Fail("the match rank: both, quantity, quality, neither");
        if (coopground::GroundNearBetter(1, 900, 900, 2, 0, 0) != 1 || coopground::GroundNearBetter(2, 0, 0, 1, 900, 900) != 0
            || coopground::GroundNearBetter(1, 5, 900, 1, 6, 0) != 1 || coopground::GroundNearBetter(1, 5, 4, 1, 5, 9) != 1
            || coopground::GroundNearBetter(1, 5, 9, 1, 5, 9) != 0)
            Fail("the near order: rank, then height difference, then flat distance; a tie is not better");
    }
}

/* P109: the ask's kind on the PRQK tail (a plain ask is 113's bytes), the pull serve, the lift on open only (R5), the sector
   mark (R3) and the holder's pull trigger. */
void t_p109_parity_pull()
{
    cooppar::ParityReq r; r.sx = 3; r.sy = 4;
    cooppar::ParityBoxRow row; row.key = "box@1,2@3,4,5"; row.digest = 9; row.items = 2;
    r.boxes.push_back(row);
    std::vector<char> plain;
    if (!cooppar::EncodeParityReq(&plain, r)) Fail("a plain ask encodes");
    if (plain.size() != (size_t)(4 + 4 + 2 + 1 + row.key.size() + 4 + 4)) Fail("a plain ask carries no kind tail (the 113 bytes)");
    for (int k = 0; k <= 2; ++k)
    {
        r.kind = k;
        std::vector<char> b;
        if (!cooppar::EncodeParityReq(&b, r)) Fail("each kind encodes");
        if (b.size() != plain.size() + (k == 0 ? 0 : 5)) Fail("the kind tail is five bytes, and only when the ask is not plain");
        cooppar::ParityReq d;
        if (cooppar::DecodeParityReq(&b[0], b.size(), &d) != cooppar::kParityDecodeOk) Fail("each kind decodes");
        if (d.kind != k || d.boxes.size() != 1 || d.boxes[0].kind != k || d.boxes[0].key != row.key || d.boxes[0].digest != 9)
            Fail("the kind round-trips and is stamped on every row");
    }
    r.kind = 3;
    { std::vector<char> b; if (cooppar::EncodeParityReq(&b, r)) Fail("an unknown kind is refused at encode"); }
    { std::vector<char> b = plain; const char t[5] = { 'P', 'R', 'Q', 'K', 3 }; b.insert(b.end(), t, t + 5);
      cooppar::ParityReq d; if (cooppar::DecodeParityReq(&b[0], b.size(), &d) != cooppar::kParityDecodeBadKind) Fail("an unknown kind on the wire is refused"); }
    { std::vector<char> b = plain; const char t[5] = { 'X', 'R', 'Q', 'K', 1 }; b.insert(b.end(), t, t + 5);
      cooppar::ParityReq d; if (cooppar::DecodeParityReq(&b[0], b.size(), &d) != cooppar::kParityDecodeTrailing) Fail("other trailing bytes stay refused"); }
    { std::vector<char> b = plain; const char t[4] = { 'P', 'R', 'Q', 'K' }; b.insert(b.end(), t, t + 4);
      cooppar::ParityReq d; if (cooppar::DecodeParityReq(&b[0], b.size(), &d) != cooppar::kParityDecodeTrailing) Fail("a cut kind tail is refused"); }
    /* R5 */
    if (cooppar::ParityLiftOnServe(cooppar::kParityAskOpen) != 1) Fail("an open ask lifts");
    if (cooppar::ParityLiftOnServe(cooppar::kParityAskPlain) != 0 || cooppar::ParityLiftOnServe(cooppar::kParityAskPull) != 0)
        Fail("an arrival row, a re-ask or a pull never lifts");
    /* R1 / R4: the previous holder's answer */
    if (cooppar::ParityPullAnswer(cooppar::kParityAskPull, 1, 1) != 1) Fail("the previous holder answers the holder's pull from a trusted copy");
    if (cooppar::ParityPullAnswer(cooppar::kParityAskPull, 1, 0) != 0) Fail("an untrusted copy is never handed to the holder");
    if (cooppar::ParityPullAnswer(cooppar::kParityAskPull, 0, 1) != 0) Fail("only a game whose box the asker holds by the area answers a pull");
    if (cooppar::ParityPullAnswer(cooppar::kParityAskPlain, 1, 1) != 0 || cooppar::ParityPullAnswer(cooppar::kParityAskOpen, 1, 1) != 0)
        Fail("an ordinary ask to a non-writer stays not holder");
    if (cooppar::ParityPullOnRefusal(cooppar::kParityAskPlain) != 1 || cooppar::ParityPullOnRefusal(cooppar::kParityAskOpen) != 1)
        Fail("a refused ask pulls the contents from the asker");
    if (cooppar::ParityPullOnRefusal(cooppar::kParityAskPull) != 0) Fail("a refused pull never pulls back (no back and forth)");
    if (cooppar::ParityApplyOverOwn(1, 0) != 0) Fail("an answer over a box this game writes stands refused");
    if (cooppar::ParityApplyOverOwn(1, 1) != 1) Fail("this game's own pull's answer goes in over its own copy");
    if (cooppar::ParityApplyOverOwn(0, 0) != 1 || cooppar::ParityApplyOverOwn(0, 1) != 1) Fail("a non-writer swaps answers in as before");
    /* an answer with items waits until items can be made (a scratch container and the item maker); an empty one applies */
    if (cooppar::ParityApplyGate(0, 0) != cooppar::kParityGateApply) Fail("an empty answer applies when no item can be made");
    if (cooppar::ParityApplyGate(5, 0) != cooppar::kParityGateHold) Fail("an answer with items holds when no item can be made");
    if (cooppar::ParityApplyGate(5, 1) != cooppar::kParityGateApply || cooppar::ParityApplyGate(0, 1) != cooppar::kParityGateApply)
        Fail("when items can be made the answer applies");
    if (cooppar::ParityHeldReask(0) != 0) Fail("a held box waits while no item can be made (not re-asked)");
    if (cooppar::ParityHeldReask(1) != 1) Fail("once items can be made a held box is re-asked");
    /* where a finished fill goes */
    if (cooppar::ParityPushTarget(1, 0) != cooppar::kParityPushPeer) Fail("the map shows a game there and no asker is off it: by area");
    if (cooppar::ParityPushTarget(1, 1) != cooppar::kParityPushPeerAndAsker || cooppar::ParityPushTarget(1, 3) != cooppar::kParityPushPeerAndAsker)
        Fail("the map shows one game there and an asker is not on it yet: by area and addressed to the asker");
    if (cooppar::ParityPushTarget(0, 1) != cooppar::kParityPushAsker) Fail("nobody on the map but an asker: the push goes to the asker");
    if (cooppar::ParityPushTarget(0, 0) != cooppar::kParityPushDrop) Fail("nobody on the map and no asker: dropped");
    if (cooppar::ParityPushTarget(-1, 0) != cooppar::kParityPushWait || cooppar::ParityPushTarget(-1, 1) != cooppar::kParityPushWait)
        Fail("a stale map waits, asker or not");
    /* an asker record's end */
    if (cooppar::ParityAskerCaughtUp(2, 1, 1) != 1) Fail("the map shows the asker in the sector: its record ends");
    if (cooppar::ParityAskerCaughtUp(2, 0, 1) != 0) Fail("the map shows another game there, not the asker: its record stays");
    if (cooppar::ParityAskerCaughtUp(2, -1, -1) != 0 || cooppar::ParityAskerCaughtUp(-1, -1, -1) != 0) Fail("a stale map ends no record");
    if (cooppar::ParityAskerCaughtUp(-1, -1, 1) != 1) Fail("a record with no slot ends once the map shows any other game there");
    if (cooppar::ParityAskerCaughtUp(-1, -1, 0) != 0) Fail("a record with no slot stays while the map shows nobody there");
    if (cooppar::ParityAskerCaughtUp(2, 1, 0) != 0) Fail("the asker's bit set but the sector read as nobody's (two reads apart): its record stays");
    if (cooppar::ParityAskerForget(3, 3) != 1) Fail("the gone player's record ends");
    if (cooppar::ParityAskerForget(3, 4) != 0) Fail("another player's record stays when one player leaves");
    if (cooppar::ParityAskerForget(-1, 4) != 1) Fail("a departure whose slot is not known ends every record");
    if (cooppar::ParityAskerForget(3, -1) != 1) Fail("a record whose slot was not known ends with any departure");
    /* R3: the mark */
    if (cooppar::ParitySectorMark(0, 0, 1, 0) != 1) Fail("an arrival held by the other game marks");
    if (cooppar::ParitySectorMark(0, 0, 0, 0) != 0) Fail("an arrival this game holds (or nobody holds) does not mark");
    if (cooppar::ParitySectorMark(0, 0, 0, 1) != 1) Fail("a gave-up arrival marks (R3: the asker keeps its own copy)");
    if (cooppar::ParitySectorMark(0, 1, 1, 0) != 0 || cooppar::ParitySectorMark(0, 1, 0, 1) != 0)
        Fail("a sector this game had as its own keeps its trust when it returns as the non-holder");
    if (cooppar::ParitySectorMark(1, 0, 0, 0) != 1 || cooppar::ParitySectorMark(1, 1, 0, 0) != 1) Fail("a mark is kept for the world");
    if (cooppar::ParitySectorOwned(0, 0, 1) != 1) Fail("an arrival as the holder owns the sector");
    if (cooppar::ParitySectorOwned(0, 1, 1) != 0) Fail("holding a sector after it was marked never makes its copies real");
    if (cooppar::ParitySectorOwned(1, 1, 0) != 1 || cooppar::ParitySectorOwned(0, 0, 0) != 0) Fail("owned is kept; not owned without a holding arrival");
    /* R1: the holder's pull trigger */
    if (cooppar::ParityPullSector(1, 1, 0, 1) != 1) Fail("a marked loaded sector this game now holds pulls");
    if (cooppar::ParityPullSector(1, 1, 1, 1) != 0) Fail("once per holding");
    if (cooppar::ParityPullSector(1, 1, 0, 0) != 0 || cooppar::ParityPullSector(0, 1, 0, 1) != 0 || cooppar::ParityPullSector(1, 0, 0, 1) != 0)
        Fail("no pull while held elsewhere, for an unmarked sector, or for one not loaded");
    /* fold 1 (F2): a gave-up mark is cleared only while this game holds the sector alone (fresh map, nobody else loaded) */
    if (cooppar::ParityGaveUpClears(1, 1, 0) != 1) Fail("a gave-up mark is cleared while this game holds the sector alone");
    if (cooppar::ParityGaveUpClears(1, 1, 1) != 0 || cooppar::ParityGaveUpClears(1, 1, -1) != 0)
        Fail("another game loaded there, or no fresh map, keeps the mark");
    if (cooppar::ParityGaveUpClears(1, 0, 0) != 0 || cooppar::ParityGaveUpClears(0, 1, 0) != 0)
        Fail("not holding keeps it; a held-verdict mark is never cleared");
    {   /* fold 1b (manager ruling revised): a cleared gave-up mark leaves the sector unmarked and NOT owned (the plugin's clear
           touches only the mark), so a later arrival held elsewhere marks it again, and only a later holding arrival owns it */
        const int ownedAfterClear = 0, markedAfterClear = 0;
        if (cooppar::ParitySectorMark(markedAfterClear, ownedAfterClear, 1, 0) != 1) Fail("a cleared gave-up mark gives no trust: a held arrival marks again");
        if (cooppar::ParitySectorOwned(ownedAfterClear, markedAfterClear, 1) != 1) Fail("a later holding arrival owns the cleared sector by the normal rule");
    }
}

}   /* namespace suite */
