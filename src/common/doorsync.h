/* src/common/doorsync.h - P8n: WHO DECIDES A DOOR, AS PURE ARITHMETIC.
 *
 * build/read-door-churn.md measured a 12.7-16.6 Hz livelock on ONE door: a locally owned NPC on the
 * non-holder called openDoor, the applier snapped the door back to the holder's CLOSED within one
 * frame, and because DoorStuff::openDoor only acts on a door that is ALREADY CLOSED the snap handed
 * the NPC a fresh opportunity every single time.  2,652 of that game's 2,672 applies were that one
 * pair.  Nothing stopped it: the existing guard measures whether its own write LANDED, and every one
 * of those writes landed - it is whether the write STAYED landed that was never asked.
 *
 * So the rule changes, and the new rule lives here rather than inside a detour, because every part
 * of it is a decision over small integers and can therefore be swept offline with the same compiler
 * that builds the plugin (src/coop-test/test_main.cpp).  doors.cpp CALLS these functions; it does
 * not carry a second copy of them (lesson 11).
 *
 *   LAST ACTOR WINS, THE AREA HOLDER TIE-BREAKS.  An ENGINE-originated door change on either game is
 *   NOT REVERTED BY THE APPLIER; a later holder word or an adopted report may supersede it - convergent,
 *   not frozen (P8n-b, review-p8n M-3).  On a game that does not hold the door's patch of map it is SENT
 *   to the holder;
 *   the holder ADOPTS it, writes it through the engine's own setter and republishes, so both games
 *   converge on the last actor's state.  Two actors inside one round trip resolve to the holder's
 *   most recent adopted state: the holder is the serialisation point and nothing else is.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no C++11 anything.
 */
#pragma once

namespace coopdoor {

/* DoorStuff::state, the int at +0x380 - .modding/03-systems/containers.md "Doors and gates",
   Confirmed from the decompiles.  ONLY 0 and 1 are durable; 2 and 3 are the animation ramp that
   DoorStuff::update 0x298FF0 drives, and they never travel. */
const int kDoorClosed  = 0;
const int kDoorOpen    = 1;
const int kDoorOpening = 2;
const int kDoorClosing = 3;

inline int DoorStateIsTerminal(int s)
{
    return (s == kDoorClosed || s == kDoorOpen) ? 1 : 0;
}

/* ============================ THE MESSAGE'S ORIGIN ============================
   MSG_DOOR_STATE's OPTIONAL TRAILING BYTE.  The decoder's own length test is a MINIMUM
   (payload.size() < at + 6 is the refusal), so a trailing byte is read when it is there and its
   absence means kDoorOriginHolder - which is exactly what every message before this build was.
   That is why no protocol bump is needed and why 43 stays free for B10. */
const int kDoorOriginHolder = 0;   /* the area holder's answer - authority */
const int kDoorOriginActor  = 1;   /* a non-holder telling the holder what its own world just did */

/* ---- P8n-b (review-p8n L-8): THE TRAILING BYTE, PARSED BY ONE PURE FUNCTION THAT A TEST CAN HIT.
   session.cpp read `payload[at+6]` and passed it through as an int, so a byte of 2..255 - a corrupted
   packet, or a future build's third origin - was NOT an actor report and therefore took the HOLDER
   arm: an unknown origin was treated as authority.  It is now four named answers, and the caller
   ignores the message on kDoorOriginParseUnknown and counts it (doorOriginUnknown).
   `size` and `at` are the decoder's own numbers; `trailingByte` is payload[at+6] when the message is
   long enough to carry it and is NOT READ when it is not (the caller passes 0). */
const int kDoorOriginParseShort   = 0;   /* shorter than key + state + locked + gen: malformed, no origin */
const int kDoorOriginParseAbsent  = 1;   /* no trailing byte - every DOOR_STATE before P8n: the holder's answer */
const int kDoorOriginParseHolder  = 2;
const int kDoorOriginParseActor   = 3;
const int kDoorOriginParseUnknown = 4;   /* a byte that is neither 0 nor 1 - the message is IGNORED */

inline int DoorParseOrigin(unsigned long payloadSize, unsigned long at, int trailingByte)
{
    if (payloadSize < at + 6) return kDoorOriginParseShort;
    if (payloadSize < at + 7) return kDoorOriginParseAbsent;
    if (trailingByte == kDoorOriginHolder) return kDoorOriginParseHolder;
    if (trailingByte == kDoorOriginActor)  return kDoorOriginParseActor;
    return kDoorOriginParseUnknown;
}

/* What a receiving game does with an arriving door state. */
const int kDoorRecvStore  = 0;   /* the holder has spoken: this is the state to hold this door to */
const int kDoorRecvAdopt  = 1;   /* we hold this door and an actor elsewhere moved it: adopt, apply, republish */
const int kDoorRecvIgnore = 2;   /* an actor report for a door we do not hold - only the holder serialises */

inline int DoorRecvDecide(int origin, int weHold)
{
    if (origin != kDoorOriginActor) return kDoorRecvStore;
    return (weHold == 1) ? kDoorRecvAdopt : kDoorRecvIgnore;
}

/* ============================ THE PUBLISH DRAIN ============================
   weHold: 1 = this game holds the door's patch of map, 0 = it does not, -1 = the question could not
   be answered.  reportIfNotHolder: 1 for an ordinary local change (the actor report the new rule
   rests on), 0 for the link-up re-offer, which is the weather pattern's "tell the late joiner what
   I hold" and must NOT push a joiner's own stale local states at the holder. */
const int kDoorSendHolder = 0;
const int kDoorSendActor  = 1;
const int kDoorSendRefuse = 2;
/* B12-a's FOURTH ANSWER IS RETIRED (B12-c, review-b12 H-2), and the drain has three again.
   B12-a gave this decision a kDoorActQueue arm for "past decision 44's grace with no notebook". Two things
   were wrong with it and the second is the one that matters. It never fired, because the caller collapsed
   the verdict to 0 a line before asking (doors.cpp); and had it fired it would have suppressed an ACTOR
   REPORT - a SESSION-LINK message from a non-holder to the holder, which is not a durable write, has
   nothing to do with the notebook, and must keep flowing whether or not there is one (decision 47).
   What decision 52 owes the door family is the HOLDER'S OWN WRITE, and that is journaled at the r4 refusal
   site in doors.cpp instead, where the key, the state and the lock are in hand. The drain is untouched.
   kDoorActQueue is GONE rather than left returning nothing: a value no arm produces is read as an arm that
   never fires. */
const int kDoorAreaNoNotebook = 5;   /* B12-a's area answer, KEPT: DoorHolderWordDecide consumes it (it falls into kDoorWordStore, the same action as before this build), and doors.cpp tests it to decide what to journal */

/* M7b slice 4 fold 1 (review 2026-10-02 F2): NEWEST PER PUBLISHER, the publisher keyed as a PLAYER (doors.cpp: net::PlayerKeyNow,
   cooplive::PlayerKeyOf) - so a road switch (the session link's raw id, then the notebook's slot id) keeps the age check. A message
   from the publisher on record whose generation is not newer is stale (1); another publisher, or nothing held, resets it (0). */
inline int DoorGenStale(long heldKey, long heldGen, long key, long gen)
{
    return (key == heldKey && heldGen >= 0 && gen <= heldGen) ? 1 : 0;
}
inline int DoorDrainDecide(int weHold, int reportIfNotHolder)
{
    if (weHold == 1) return kDoorSendHolder;
    return (reportIfNotHolder != 0) ? kDoorSendActor : kDoorSendRefuse;
}
/* WHAT BECOMES OF A PUBLISH THE DRAIN COULD NOT SEND (the road open, the send refused). An actor report is queued again and tried
   until it is kDoorReportRetryMs old. A holder's publish queued with reportIfNotHolder 0 - its RE-OFFER when another player entered the world,
   and its republish after adopting another game's report (doors.cpp, the adopt) - is queued again until it is sent: dropping the
   re-offer would leave the newcomer told nothing about the door, dropping the republish would leave the reporter unanswered.
   A holder's own change publish is dropped, as before. */
const int kDoorUnsentDrop = 0;
const int kDoorUnsentRetryTimed = 1;
const int kDoorUnsentRetryUntilSent = 2;
inline int DoorUnsentDecide(int origin, int reportIfNotHolder)
{
    if (origin == kDoorOriginActor) return kDoorUnsentRetryTimed;
    return (reportIfNotHolder == 0) ? kDoorUnsentRetryUntilSent : kDoorUnsentDrop;
}
/* a timed retry gives up once its first failure is limitMs old; an until-sent retry never does */
inline int DoorRetryGivesUp(int how, unsigned long sinceFirstMs, unsigned long limitMs)
{
    return (how == kDoorUnsentRetryTimed && sinceFirstMs >= limitMs) ? 1 : 0;
}

/* ============================ THE APPLIER'S VERDICT FOR ONE ROW ON ONE TICK ============================ */
const int kDoorActNothing      = 0;   /* the two games agree - clear the terms and cost nothing */
const int kDoorActWaitMidSwing = 1;   /* mid-swing: leave it alone and re-check next tick */
const int kDoorActApply        = 2;   /* write the holder's state through DoorStuff::setDoorState */
const int kDoorActReport       = 3;   /* an actor moved it here and we do NOT hold it: never revert */
const int kDoorActAdoptLocal   = 4;   /* an actor moved it here and we DO hold it: our state is now the answer */
const int kDoorActGaveUp       = 5;   /* this row has stopped being corrected */

struct DoorDecideIn
{
    int liveState;     /* 0..3, re-read from the door at this instant - never a cached value */
    int holderState;   /* 0 or 1; anything else means nothing has been heard about this door */
    int localWrite;    /* an engine writer moved this door since the last agreement */
    int forcedOpen;    /* setupPhysicalUT forced this GATE open since the last agreement */
    int weHold;        /* 1 yes, 0 no, -1 unanswerable */
    int gaveUp;        /* three corrections in a row did not take (the pre-existing guard) */
    int gaveUpChurn;   /* three corrections in a row were UNDONE (the new one) */
};

/* THE ORDER OF THESE TESTS IS THE DESIGN, so it is written down rather than left to the reader:
     1. a row that has given up is not touched at all;
     2. a door nobody has spoken about is not touched;
     3. A DOOR MID-SWING IS LEFT ALONE - and this comes BEFORE the agreement test because a
        transient 2 or 3 can never equal a terminal 0 or 1, which is precisely why the old code
        treated every animating door as a disagreement and snapped it every tick;
     4. agreement is the common case and it is free;
     5. a forced open from physics setup is world CONSTRUCTION, not an actor, so the holder's answer
        still has to survive it (F633) - the last-actor rule must not swallow this one;
     6. otherwise, an engine actor moved this door and the last actor wins: the holder adopts,
        anyone else reports and waits;
     7. and with no local writer behind it, the disagreement is the holder's word to follow. */
inline int DoorDecide(const DoorDecideIn& in)
{
    if (in.gaveUp != 0 || in.gaveUpChurn != 0) return kDoorActGaveUp;
    if (in.holderState != kDoorClosed && in.holderState != kDoorOpen) return kDoorActNothing;
    if (DoorStateIsTerminal(in.liveState) == 0) return kDoorActWaitMidSwing;
    if (in.liveState == in.holderState) return kDoorActNothing;
    if (in.forcedOpen != 0) return kDoorActApply;
    if (in.localWrite != 0) return (in.weHold == 1) ? kDoorActAdoptLocal : kDoorActReport;
    return kDoorActApply;
}

/* ============================ THE GIVE-UP THAT MEASURES PERSISTENCE ============================
   The shipped guard counts corrections that did not TAKE, read back inside the same function.  In
   T236d every one of the 2,652 reverts read back correctly, so that counter was 0 and its give-up
   was unreachable while the loop ran at frame rate.  This one counts corrections that a NON-SELF
   writer UNDID before the next agreement.
   N = 3 IN T = 5 s, AND THE NUMBERS COME OFF THE MEASUREMENT.  The livelock ran at 12.7-16.6 Hz, so
   three undone corrections take about 0.2 s and the guard fires almost at once.  Ordinary traffic is
   four orders of magnitude below that: over the same nine minutes the holder's whole town produced
   24 openDoor calls across 27 doors, about one per door per ten minutes, so three separate genuine
   NPC uses of ONE door inside five seconds does not happen. */
const long          kDoorChurnGiveUpN  = 3;
const unsigned long kDoorChurnWindowMs = 5000;
/* P8n-b (review-p8n M-4, MANAGER RULING): A ROW IS NOT DEAD FOR THE RUN.  The shipped give-up was
   permanent - kDoorActGaveUp is returned before every other test, so a row that gave up on churn was
   never corrected again however long the two games agreed afterwards.  Thirty seconds of CONTINUOUS
   agreement on that row re-arms it; any disagreement resets the clock.  Thirty seconds is six
   thousand times the 5 ms period of the measured livelock, so a re-arm cannot land inside one. */
const unsigned long kDoorChurnReArmMs = 30000;

struct DoorChurn
{
    long          undone;    /* undone corrections inside the current window */
    unsigned long firstMs;   /* when that window opened */
    int           gaveUp;
    int           agreeing;    /* P8n-b: the agreement clock is running */
    unsigned long agreeSinceMs;/* ... and this is when it started */
};

inline void DoorChurnReset(DoorChurn* c)
{
    c->undone = 0; c->firstMs = 0; c->gaveUp = 0; c->agreeing = 0; c->agreeSinceMs = 0;
}

/* THE ROW AND THE HOLDER AGREE ON THIS TICK.  1 = THIS call re-armed the row, so the caller counts
   exactly once.  Called only for a row that has given up; on any other row it just stops the clock. */
inline int DoorChurnNoteAgreement(DoorChurn* c, unsigned long nowMs)
{
    if (c->gaveUp == 0) { c->agreeing = 0; return 0; }
    if (c->agreeing == 0) { c->agreeing = 1; c->agreeSinceMs = nowMs; return 0; }
    if ((unsigned long)(nowMs - c->agreeSinceMs) < kDoorChurnReArmMs) return 0;
    c->undone = 0; c->firstMs = 0; c->gaveUp = 0; c->agreeing = 0; c->agreeSinceMs = 0;
    return 1;
}

/* ANY disagreement resets the clock: thirty seconds of agreement means thirty CONTINUOUS seconds. */
inline void DoorChurnNoteDisagreement(DoorChurn* c)
{
    c->agreeing = 0; c->agreeSinceMs = 0;
}

/* ============================ A REPORT THE HOLDER NEVER ANSWERED ============================
   P8n-b (review-p8n L-6).  An actor report is queued ONCE, at the funnel, and the applier's report
   verdict did not even mark the tick dirty - so a report lost on the wire, or one whose holder never
   adopted, left the two games disagreeing with nothing ever asking again.  A pending report is now
   re-sent every two seconds until the holder's word lands, and abandoned after fifteen so a door in a
   sector nobody holds cannot report for ever.  Both bounds are unsigned differences and survive the
   GetTickCount wrap. */
const unsigned long kDoorReportRetryMs  = 2000;
const unsigned long kDoorReportGiveUpMs = 15000;
const int kDoorReportWait    = 0;
const int kDoorReportRetry   = 1;
const int kDoorReportAbandon = 2;

inline int DoorReportRetryDecide(unsigned long nowMs, unsigned long firstMs, unsigned long lastMs)
{
    if ((unsigned long)(nowMs - firstMs) > kDoorReportGiveUpMs) return kDoorReportAbandon;
    if ((unsigned long)(nowMs - lastMs) >= kDoorReportRetryMs)  return kDoorReportRetry;
    return kDoorReportWait;
}

/* ---- P8n-c (review-p8n-b M-1): AND THE ABANDON IS STICKY, OR IT IS NOT A BOUND AT ALL.
   P8n-b cleared reportPending on the abandon and booked nothing durable on the row, so the very next
   dirty tick found pending == 0, opened a FRESH fifteen-second episode and started retrying again -
   an abandon every fifteen seconds for as long as the two games disagreed, which is exactly the
   "cannot report for ever" the docs claim.  The row now carries a LATCH: while it is set no new
   episode opens and every report verdict books suppressedAbandoned instead.  Only AGREEMENT clears
   it (doors.cpp's terminal-state compare, the same place reportPending is cleared), so a later
   disagreement on a door that has since agreed does get a fresh episode with the full bound. */
const int kDoorReportOpen       = 3;   /* no episode is open on this row - open one and send nothing */
const int kDoorReportSuppressed = 4;   /* the row abandoned this door and it has not agreed since */

inline int DoorReportDecide(int abandoned, int pending, unsigned long nowMs,
                            unsigned long firstMs, unsigned long lastMs)
{
    if (abandoned != 0) return kDoorReportSuppressed;
    if (pending == 0)   return kDoorReportOpen;
    return DoorReportRetryDecide(nowMs, firstMs, lastMs);
}

/* ============ P8n-c (review-p8n-b M-2): WHEN A HOLDER'S WORD IS REFUSED, AND WHAT A REFUSAL TAKES ============
   P8n-b refused a holder publish for any door AreaVerdictForDoor answered "mine" about.  That verdict
   is ItAreaVerdictAt's, and its FIRST arm returns kBoxMine for kPicLoading - the deliberate load
   window that lets a game act on its own world while it is still building it, entered whenever engine
   writes are blocked OR no area map has arrived for this world yet.  So before the first AREAMAP
   landed this game refused EVERY holder publish it received.  A refusal has to be EARNED by a SETTLED
   answer; a loading picture is a presumption, and the word is stored exactly as it was before P8n-b.
   The four answers the door road asks for, and the four things it does with them. */
const int kDoorAreaLoading      = 0;   /* the area picture has not settled - "mine" is presumed, not known */
const int kDoorAreaMine         = 1;   /* settled: this game holds the door's patch of map */
const int kDoorAreaOther        = 2;   /* settled: the other game holds it */
const int kDoorAreaNobody       = 3;   /* settled: nobody holds it */
const int kDoorAreaUnanswerable = 4;   /* no row, a row the engine's active-zone walk dropped, or an unreadable position */
/* kDoorAreaNoNotebook = 5 is the sixth member of this list and is declared with the publish drain above.
   DoorHolderWordDecide below folds it into kDoorWordStore, which is the action it had before B12-a existed:
   a game with no notebook stores the holder's word exactly as one with an unsettled picture does. */

const int kDoorWordStore             = 0;
const int kDoorWordRefuse            = 1;
const int kDoorWordStoreLoading      = 2;   /* stored, and counted apart: the picture was loading */
const int kDoorWordStoreUnanswerable = 3;   /* stored, and counted apart (F172): the question could not be asked */

inline int DoorHolderWordDecide(int area)
{
    if (area == kDoorAreaUnanswerable) return kDoorWordStoreUnanswerable;
    if (area == kDoorAreaLoading)      return kDoorWordStoreLoading;
    return (area == kDoorAreaMine) ? kDoorWordRefuse : kDoorWordStore;
}

/* 1 = this message may advance the ordering filter's state (generation, publisher, lock bit).
   A REFUSED message must not.  P8n-b wrote gen/fromPeer BEFORE the refusal returned, so the discarded
   word CONSUMED the holder's generation: the holder's next publish for that door was no longer newer
   than what this game held, the ordering filter dropped it, and the holder had no reason to resend.
   Refusing a word and eating its sequence number is how a refusal becomes permanent. */
inline int DoorWordAdvancesGen(int what) { return (what == kDoorWordRefuse) ? 0 : 1; }

/* P8n-c (review-p8n-b M-3): THE RE-ARM CLOCK IS JUDGED ON ITS OWN CADENCE, NOT BY HOLDING THE TICK BUSY.
   P8n-b kept g_dirty raised for the whole thirty seconds so DoorsTick would keep running and the clock
   would keep being read - which ran the WHOLE tick (a 256-slot publish scan, a 256-slot apply scan, a
   linear active-zone walk test per row and a key re-derivation with two virtual calls) every frame for
   thirty seconds because one row had given up.  The clock is now read by a sweep of its own, at most
   once a second, and only while some row is given up on churn. */
const unsigned long kDoorReArmSweepMs = 1000;

/* 1 = THIS event is the one that gives up, so the caller logs exactly once. */
inline int DoorChurnNoteUndone(DoorChurn* c, unsigned long nowMs)
{
    if (c->gaveUp != 0) return 0;
    if (c->undone <= 0 || (unsigned long)(nowMs - c->firstMs) > kDoorChurnWindowMs)
    {
        c->undone = 1;
        c->firstMs = nowMs;
    }
    else ++c->undone;
    if (c->undone >= kDoorChurnGiveUpN) { c->gaveUp = 1; return 1; }
    return 0;
}

/* ============================ THE RE-REGISTRATION MIS-BOOKING ============================
   The publish drain frees a row whose door the engine's active-zone walk no longer lists, and the
   door's next funnel call re-registers it with was = -1.  The shipped code booked that -1 -> N as a
   LOCAL WRITER, so the next apply for that key was attributed to "a local writer moved a door this
   game does not hold" when nothing had moved at all - which is part of why the arrival span read 20
   for 27 doors.  A first sighting is not a writer. */
inline int DoorSightingIsLocalWrite(int wasState)
{
    return (wasState < 0) ? 0 : 1;
}

/* ============ P8o (T236g, 2026-09-18): A KEY THIS GAME HAS NEVER SEEN MOVE, RESOLVED TO A DOOR ============
   A DoorRow used to exist only because THIS game's own engine had moved that door - before P8o, NoteDoor
   was RowFor's only caller - so a holder whose zone had reloaded held no row for the key its peer kept
   reporting: 24 reports in T236g were booked lastActor.unnameable and dropped, and the two games
   showed that one door differently for about 100 s.  The holder now resolves the key the long way -
   the engine's own memoised loaded-buildings walk, each building's door array (Building+0x1B8: count
   at +0x1C0, Building** at +0x1C8, stride 8; Confirmed in the decompile), and the key rebuilt FROM
   THE DOOR by the one builder the detours use.  This is the decision asked per candidate door; the
   walk itself lives in doors.cpp, because only that side may read the engine.
   P8o-b (review-p8o H-1) TOOK THE VIRTUAL CALLS OUT OF THE WALK ALTOGETHER: the position comes from the
   +0x48 field through the one key builder's POD mode, and whether a key names a gate is read off the KEY
   and never asked of the object, so no slot is called on any pointer the resolver found for itself.
   P8o-b (review-p8o M-1) ALSO RETIRED THE TEARDOWN ARM: the apply loop this is reached from has already
   returned on EngineWritesBlocked(), so the input could not fire and a term that cannot fire is a lie
   about what was checked (F172).  FOUR CHECKS REMAIN, IN ORDER, AND EACH ONE IS FOR:
     plausible      - DoorPlausible on the ARRAY ELEMENT, before anything is read out of it;
     stateOk        - DoorReadPod's state is 0..3, the cheapest test that this is a DoorStuff;
     parentMatches  - the door's +0x368 parent IS the building we walked, and destroyDoors nulls that
                      field, so a door the engine has let go cannot pass it;
     keyEqual       - the key rebuilt from the door equals the incoming key EXACTLY.  There is no
                      nearest-position fallback and there must not be one: setDoorOpenAmount moves the
                      Ogre node and never +0x48, so a swinging door has the same key as a still one.
   A door that passes every check but the last is simply A DIFFERENT DOOR - kDoorResolveNoMatch, keep
   walking - which is why no-match and refused-check are two answers and not one bucket. */
const int kDoorResolveAccept          = 0;
const int kDoorResolveRefusedCheck    = 2;   /* implausible, not a DoorStuff, or not this building's door */
const int kDoorResolveNoMatch         = 3;   /* a live door of this building, but not the one the key names */
/* 1 is the RETIRED teardown code and is deliberately not reused (F172: a retired span stays absent). */

inline int DoorResolveAccept(int plausible, int stateOk, int parentMatches, int keyEqual)
{
    if (plausible == 0)     return kDoorResolveRefusedCheck;
    if (stateOk == 0)       return kDoorResolveRefusedCheck;
    if (parentMatches == 0) return kDoorResolveRefusedCheck;
    return (keyEqual != 0) ? kDoorResolveAccept : kDoorResolveNoMatch;
}

}   /* namespace coopdoor */
