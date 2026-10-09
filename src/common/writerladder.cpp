/* src/common/writerladder.cpp - B9. See writerladder.h for the rule and for why each rung is where it is.
 * PURE C++03 (VS2010 v100): no auto, no nullptr, no range-for, no engine, no OS header. */
#include "writerladder.h"

namespace coopwriter {

int WriterLadder(int loadedHereLive, int peerSectorLoaded, int mySlotLower, int slotDecided,
                 long long notebookDownMs, long long refuseAfterMs, int forBox,
                 int* rungOut, int* spansOut)
{
    int rung  = kRungNone;
    int spans = 0;
    /* A negative "down" is not an outage. The caller answers 0 while the notebook is linked; -1 would mean
       "not known", and reading not-known as a 60-second outage would refuse every container in the world
       on a build that forgot to wire the clock. */
    const long long downMs = (notebookDownMs > 0) ? notebookDownMs : 0;

    /* R4 FIRST. Decision 44: past the grace this game stops writing and says so. */
    if (refuseAfterMs >= 0 && downMs > refuseAfterMs)
    {
        rung = kRungRefusedNoNotebook;
        if (rungOut  != 0) *rungOut  = rung;
        if (spansOut != 0) *spansOut = spans;
        return kWrRefuse;
    }
    if (downMs > 0) spans |= kSpanGracePass;   /* the notebook is down and the grace has not run out */

    /* R1. THE LIVE POINT READ. Anything that is not a clean 1 is "not loaded here", which is the safe
       direction: refusing to publish costs a move the player can make again. */
    if (loadedHereLive != 1)
    {
        rung = kRungNotLoadedHere;
        if (rungOut  != 0) *rungOut  = rung;
        if (spansOut != 0) *spansOut = spans;
        return kWrHeld;
    }

    /* R2. The other game reported its set and this sector was not in it, so this game is the only one that
       has the box. NOTE THAT -1 IS NOT 0: "no report" must never be read as "the other game does not have
       it" (design 2.3 - the mistake an `otherLoaded`-based rung would have made silently). */
    if (peerSectorLoaded == 0)
    {
        rung = kRungSoleLoaded;
        if (rungOut  != 0) *rungOut  = rung;
        if (spansOut != 0) *spansOut = spans;
        return kWrMine;
    }
    if (peerSectorLoaded != 1) spans |= kSpanPeerUnknown;

    /* R3. The tie-break. */
    /* B9-b (review-b9 M-3): the span asks WHO DECIDED, not what this game happens to own. `slotDecided`
       is 0 for every path on which the session role produced `mySlotLower`, which is one more path than
       "this game has no slot" - see writerladder.h. */
    if (slotDecided == 0)
    {
        /* M2-b (review-m2 H1, decision 52): INSIDE THE GRACE, NO SLOT COMPARISON IS A REFUSAL, SO THE WRITE IS
           JOURNALED AND NOT LOST. A link-down clears this game's slot, so with the notebook down R3 can never
           compare; answering NO-ANSWER there made every caller DROP the write (a box move neither journaled
           nor undone, a door read as "nobody holds it", a zone save skipped). REFUSE is the answer every
           outage-journal path already takes, and the replay at relink asks the writer question again. Same
           rung as R4, so the rung identity is unchanged and r4Refused counts it. */
        if (downMs > 0)
        {
            rung = kRungRefusedNoNotebook;
            if (rungOut  != 0) *rungOut  = rung;
            if (spansOut != 0) *spansOut = spans;
            return kWrRefuse;
        }
        /* M2 (decisions 32/44/54; audit B14): NO SLOT COMPARISON WITH THE NOTEBOOK LINKED, NO ANSWER. The
           session role used to decide here; nobody is named and the caller answers with its no-fresh-map
           value. */
        spans |= kSpanByRole;
        rung = kRungNoAnswer;
        if (rungOut  != 0) *rungOut  = rung;
        if (spansOut != 0) *spansOut = spans;
        return kWrNoAnswer;
    }
    {
        /* the forBox narrowing: with no peer answer a restock never presumes (writerladder.h). B9-b: a
           DOOR (forBox == 2) is on the non-zero side with a box, deliberately - a presumed door state is
           one flag on one door, not a wiped shop. */
        const int declineForRestock = (forBox == 0 && peerSectorLoaded != 1) ? 1 : 0;
        const int lower = (declineForRestock != 0) ? 0 : ((mySlotLower != 0) ? 1 : 0);
        if (lower != 0)
        {
            rung = kRungTieLower;
            if (rungOut  != 0) *rungOut  = rung;
            if (spansOut != 0) *spansOut = spans;
            return kWrMine;
        }
        rung = kRungTieHigher;
        if (rungOut  != 0) *rungOut  = rung;
        if (spansOut != 0) *spansOut = spans;
        return kWrHeld;
    }
}

/* B10 (audit C11, design-e46-store 3.4). ONE CALL OF THE LADDER, AND NO RULE OF ITS OWN BELOW THE
   two short-circuits: whatever a box does with these inputs, a zone file does. */
int ZoneWriterDecide(int roleIsSingle, int held, int mine, int loadedHereSector,
                     int peerSectorLoaded, int mySlotLower, int slotDecided,
                     long long notebookDownMs, long long refuseAfterMs,
                     int* pathOut, int* rungOut, int* spansOut)
{
    if (rungOut  != 0) *rungOut  = kRungNone;
    if (spansOut != 0) *spansOut = 0;
    if (roleIsSingle != 0) { if (pathOut != 0) *pathOut = kZonePathSingle;      return kWrMine; }
    if (mine == 1)         { if (pathOut != 0) *pathOut = kZonePathHolderMine;  return kWrMine; }
    if (held == 1)         { if (pathOut != 0) *pathOut = kZonePathHolderOther; return kWrHeld; }
    if (held == -2)        { if (pathOut != 0) *pathOut = kZonePathHolderOther; return kWrHeld; }   /* a frozen area (its holder is not in the world): its holder's file stands */
    if (pathOut != 0) *pathOut = kZonePathLadder;
    return WriterLadder(loadedHereSector, peerSectorLoaded, mySlotLower, slotDecided,
                        notebookDownMs, refuseAfterMs, 1, rungOut, spansOut);
}

/* inv7d: the owner's arrival (writerladder.h) */
int OwnerArriveEdge(double since, double sweptSince, int activePolls)
{
    if (since <= 0.0) return kArriveNone;
    if (since == sweptSince) return kArriveNone;
    if (activePolls < kArriveSettlePolls) return kArriveWait;
    return kArriveSweep;
}
int OwnerArriveWait(int state, unsigned long elapsedMs, unsigned long waitMs, int* timedOut)
{
    if (timedOut != 0) *timedOut = 0;
    if (state == 1) return 1;
    if (elapsedMs > waitMs) { if (timedOut != 0) *timedOut = 1; return 1; }
    return 0;
}
int OwnerArriveApplies(int state) { return (state == 0 || state == 2) ? 1 : 0; }
int OwnerArriveSwapOrReask(int state, int askHave, unsigned int askDigest, int liveHave, unsigned int liveDigest)
{
    if (state != 2 || askHave == 0 || liveHave == 0) return kArriveSwapIt;
    return (liveDigest == askDigest) ? kArriveSwapIt : kArriveReask;
}

}   /* namespace coopwriter */
