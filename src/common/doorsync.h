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

/* ============================ THE PLAYED SWING AND THE LOCK ============================
   The holder's word is followed the way the holder's own player saw it happen: a door that goes from
   one settled state to the other is moved by DoorStuff::openDoor 0x297000 / closeDoor 0x298C10 - the
   calls a character's own open uses - so the copy swings at the engine's own speed and plays the
   engine's own Door_Open / Door_Close / Door_Shut sounds.  setDoorState 0x298FC0 (a snap) is kept for
   the cases where nobody watched the door move: a gate the engine's physics setup forced open, a door
   this game is hearing about for the first time, a swing the engine refused, and a swing that never
   landed. */

/* Why the applier is writing this door - the three apply reasons doors.cpp books. */
const int kDoorWhyArrival    = 1;   /* the first word this game has heard about the door since it last agreed */
const int kDoorWhyForcedOpen = 2;   /* setupPhysicalUT forced this game's gate open under a holder that says shut */
const int kDoorWhyHolderWord = 3;   /* the holder published a change (or adopted one) since the last agreement */

const int kDoorApplySnap = 0;   /* DoorStuff::setDoorState - state, node, physics and navmesh in one write */
const int kDoorApplyPlay = 1;   /* DoorStuff::openDoor / closeDoor - OPENING / CLOSING, and the ramp lands it */

/* A swing is played only for a holder's word about a door that is settled in the OTHER terminal state,
   and only when the engine's open / close call passed its prologue check at install. */
inline int DoorApplyHow(int why, int liveState, int holderState, int playCallOk)
{
    if (playCallOk == 0 || why != kDoorWhyHolderWord) return kDoorApplySnap;
    if (liveState == kDoorClosed && holderState == kDoorOpen) return kDoorApplyPlay;
    if (liveState == kDoorOpen && holderState == kDoorClosed) return kDoorApplyPlay;
    return kDoorApplySnap;
}
/* What the engine call must have left behind for the swing to count as started. */
inline int DoorPlayStartedState(int target) { return (target == kDoorOpen) ? kDoorOpening : kDoorClosing; }

/* A SWING THIS GAME PLAYED IS ITS OWN WRITE UNTIL IT LANDS.  openDoor / closeDoor only set OPENING /
   CLOSING; the per-frame update then moves the door and writes the terminal state through the funnel
   frames later, outside the applier's own call.  Each change the funnel sees on a door with a played
   swing outstanding is classified here, so the swing's own frames are never reported back to the
   holder as something this game's world did. */
const int kDoorPlayNone   = 0;   /* no played swing on this door: an ordinary change */
const int kDoorPlayOwned  = 1;   /* the swing's own in-between state */
const int kDoorPlayLanded = 2;   /* the swing reached its target: still ours, and the swing is over */
const int kDoorPlayBroken = 3;   /* something else moved the door: the swing is over and this change is not ours */
inline int DoorPlayNote(int target, int newState)
{
    if (target != kDoorOpen && target != kDoorClosed) return kDoorPlayNone;
    if (newState == target) return kDoorPlayLanded;
    if (newState == DoorPlayStartedState(target)) return kDoorPlayOwned;
    return kDoorPlayBroken;
}
/* A played swing that has not landed inside this bound is finished with a snap.  The engine's ramp is
   0.5 of the door's travel per second (DoorStuff+0x388 from the constructor), so a swing takes about
   two seconds of game time; the bound is generous so a slow frame rate or a low game speed never
   reaches it, and it is what stops a swing the update never drives from leaving the door mid-way.  The
   times are on doors.cpp's UNPAUSED clock: the engine's door update does not move a door while the pause
   byte (0x2133969) is set, so time spent paused never uses the bound up. */
const unsigned long kDoorPlayLandMs = 15000;
inline int DoorPlayOverdue(int target, unsigned long startMs, unsigned long nowMs)
{
    if (target != kDoorOpen && target != kDoorClosed) return 0;
    return ((unsigned long)(nowMs - startMs) >= kDoorPlayLandMs) ? 1 : 0;
}

/* THE LOCK WORD - what travels in the DOOR_STATE lock byte.  bit 0 is DoorLock::locked (+0x20), the
   lock as it is now; bit 1 is DoorStuff::wantsToLock (+0x384), the lock the door's owner asked for,
   which the engine re-applies itself every time the door finishes closing (DoorStuff::update 0x298FF0
   calls lockDoor when it is set).  Both are needed: a door told to lock while it stands open has
   wantsToLock 1 and locked 0 until it shuts. */
inline int DoorLockWord(int locked, int wants) { return ((locked != 0) ? 1 : 0) | ((wants != 0) ? 2 : 0); }
inline int DoorLockWordValid(int w) { return (w >= 0 && w <= 3) ? 1 : 0; }
inline int DoorLockWordLocked(int w) { return w & 1; }
inline int DoorLockWordWants(int w) { return (w >> 1) & 1; }

/* With the two games agreeing about OPEN / CLOSED, does the lock need anything?  The same last-actor
   rule as the state: a lock this game's own world changed is reported (or, on the holder, adopted),
   never put back by the applier; otherwise the holder's lock word is applied. */
const int kDoorActLock = 6;   /* apply the holder's lock word through the engine's own lock calls */
inline int DoorLockDecide(int liveWord, int holderWord, int localWrite, int weHold)
{
    if (DoorLockWordValid(holderWord) == 0 || DoorLockWordValid(liveWord) == 0) return kDoorActNothing;
    if (liveWord == holderWord) return kDoorActNothing;
    if (localWrite != 0) return (weHold == 1) ? kDoorActAdoptLocal : kDoorActReport;
    return kDoorActLock;
}
/* A LOCK WORD NO DETOUR SAW.  The NPC close-then-lock action 0x337630 calls closeDoor and then writes
   DoorLock::locked = 1 itself (wantsToLock untouched); on a door that is already shut closeDoor does nothing,
   so no detour sees the lock.  doors.cpp keeps on every row the lock word this game last SAW (through a
   detour) or WROTE (an apply of its own records what it left), so a live word that differs from it is this
   game's own world acting: a LOCAL write, reported (not holder) or adopted (holder) like a hooked one, never
   undone.  A word outside 0..3 on either side says nothing. */
inline int DoorLockUnseenWrite(int liveWord, int rowWord)
{
    if (DoorLockWordValid(liveWord) == 0 || DoorLockWordValid(rowWord) == 0) return 0;
    return (liveWord != rowWord) ? 1 : 0;
}
/* A PLAYED SWING THAT LANDS WITH A CHANGE OF THIS GAME'S OWN INSIDE IT - a lock pressed (a hooked local
   write, localWrite set) or written by the NPC lock action (a word no detour saw) while the door was mid-way.
   Only a terminal state is queued, so the landing is where that change goes out. */
inline int DoorPlayLandReport(int localWrite, int rowWord, int liveWord)
{
    if (localWrite != 0) return 1;
    return DoorLockUnseenWrite(liveWord, rowWord);
}
/* THE SAME TEST ON THIS GAME'S LIVE ROWS, for a door no word from the other game has named.  A close that did
   nothing marks the door (the NPC lock action's first call), and the next tick compares its live lock word with
   the row's: a door at rest - terminal, and in the state the row last recorded - whose word moved with no detour
   seeing it is this game's own change, queued as a hooked one is (the holder publishes it, a non-holder reports
   it).  A door mid-swing is left to its landing, where the funnel sees the word with the state. */
inline int DoorLockSweepQueues(int liveState, int rowState, int liveWord, int rowWord)
{
    if (DoorStateIsTerminal(liveState) == 0 || liveState != rowState) return 0;
    return DoorLockUnseenWrite(liveWord, rowWord);
}
/* ONE WRITE AT A TIME toward the holder's lock word; doors.cpp re-reads the door after each and asks again.
   Each write is one the engine's own code makes (Confirmed from the 1.0.65 bytes and build/decomp_*.txt):
     lockDoor   0x2969B0  wantsToLock = 1, and locked = 1 if the door is CLOSED and settled;
     unlockDoor 0x569940  locked = 0, then the gate code is recomputed; wantsToLock untouched;
     wantsToLock = 0      written directly, as lockButton 0x5465D0 writes it - no engine call clears it alone;
     locked = 1           written directly, as the NPC lock action 0x337630 writes it - no engine call makes a
                          lock without the wish (word 1) or a lock on a door that is not shut.
   lockButton is never called to mirror: it posts Building_Lock_Unlock on the fixed sound object 0x6E, which
   is heard wherever the listener stands, so a mirrored lock is silent.  Every word 0..3 is reachable on a
   door that has a DoorLock. */
const int kDoorLockStepNone       = 0;
const int kDoorLockStepClearWants = 1;   /* wantsToLock = 0 */
const int kDoorLockStepLock       = 2;   /* lockDoor */
const int kDoorLockStepUnlock     = 3;   /* unlockDoor */
const int kDoorLockStepSetLocked  = 4;   /* DoorLock::locked = 1 */
const int kDoorLockStepWait       = 5;   /* mid-swing: the lock is looked at again once the door settles */
inline int DoorLockStep(int liveState, int liveWord, int holderWord)
{
    if (DoorLockWordValid(holderWord) == 0 || DoorLockWordValid(liveWord) == 0) return kDoorLockStepNone;
    if (liveWord == holderWord) return kDoorLockStepNone;
    if (DoorStateIsTerminal(liveState) == 0) return kDoorLockStepWait;
    {
        const int hw = DoorLockWordWants(holderWord), lw = DoorLockWordWants(liveWord);
        if (hw != 0 && lw == 0) return kDoorLockStepLock;
        if (hw == 0 && lw != 0) return kDoorLockStepClearWants;
        if (DoorLockWordLocked(holderWord) == 0) return kDoorLockStepUnlock;
        if (hw != 0 && liveState == kDoorClosed) return kDoorLockStepLock;
        return kDoorLockStepSetLocked;
    }
}
/* lockDoor 0x2969B0 makes the wish on any door but the lock only on a door that is CLOSED and settled (its open
   amount at or below the engine's threshold).  A lockDoor step that left the word as it was on a CLOSED door was
   therefore refused for the open amount, and the step after it is the direct locked = 1 write - the lock the
   engine itself makes there once the door settles. */
inline int DoorLockStepAfterRefusal(int step, int liveState, int lockRefused)
{
    if (step == kDoorLockStepLock && lockRefused != 0 && liveState == kDoorClosed) return kDoorLockStepSetLocked;
    return step;
}
/* The bound on one visit: the wish, then the lock - two writes reach any word from any other. */
const int kDoorLockStepsPerVisit = 3;
/* Consecutive visits whose lock write did not move the word before this door stops being lock-corrected
   until the two games agree about its lock again (lesson 14). */
const int kDoorLockGiveUpN = 3;
/* Why the applier could not bring the lock to the holder's word.  The open / closed state agrees, so the row
   takes the agreement clears as for full agreement, and doors.cpp counts the event once (a row latch). */
const int kDoorLockStuckNone   = 0;   /* applied, or still on its way (mid-swing, a step not yet effective) */
const int kDoorLockStuckNoLock = 1;   /* the door has no DoorLock */
const int kDoorLockStuckNoCall = 2;   /* lockDoor / unlockDoor did not pass its prologue check */
const int kDoorLockStuckGaveUp = 3;   /* kDoorLockGiveUpN ineffective visits */

/* ============ A GAME THAT LOADS AN AREA TAKES THE HOLDER'S DOORS; IT DOES NOT PUSH ITS OWN ============
   A door this game sees for the first time (its row is new: wasState -1) is in whatever state this game's own load built it in -
   nothing acted on it.  Sent as an actor report, the area holder adopts it under the last-actor rule, so a game that (re)loads a
   town would close on the holder's screen every door its load built closed.  So a change is an actor report only when an actor
   made it:
     - a write by one of the engine's load-time door writers (loadWriter, told by the caller's return address) is the load, not an
       actor, whatever the row knew before: a re-offer;
     - otherwise a call into openDoor / closeDoor / lockButton (actionEntry) is somebody acting on the door: an actor report, even
       on a door whose row is new;
     - otherwise a first sighting (wasState -1) is a re-offer and a change from a known state is an actor report.
   A re-offer is published if this game holds the door and refused if it does not.  The value is the reportIfNotHolder argument of
   the queue. */
inline int DoorChangeReportsIfNotHolder(int wasState, int loadWriter, int actionEntry)
{
    if (loadWriter != 0) return 0;
    if (actionEntry != 0) return 1;
    return (wasState < 0) ? 0 : 1;
}

/* THE ENTRY POINTS THAT ARE AN ACT ON THE DOOR, by the `where` name the detour that saw the change hands NoteDoor.  openDoor and
   closeDoor are the engine's door verbs (a player or an NPC using the door); lockButton is the player's lock press.  The funnel
   (setDoorOpenAmount) and lockDoor (the re-lock when a closing door lands) are the engine following a door, not an act.
   1 = an action. */
inline int DoorNameIs(const char* a, const char* b)
{
    if (a == 0 || b == 0) return 0;
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return (*a == *b) ? 1 : 0;
}
inline int DoorEntryIsAction(const char* where)
{
    return (DoorNameIs(where, "openDoor") || DoorNameIs(where, "closeDoor") || DoorNameIs(where, "lockButton")) ? 1 : 0;
}

/* A LOAD-TIME WRITER, BY RETURN ADDRESS.  `writers` are the address table's return addresses of the engine's load-time door writers
   (0 = a row the table did not fill); `ret` is the detour's caller, 0 when it could not be read.  1 = `ret` is one of them. */
inline int DoorRetIsLoadWriter(unsigned long long ret, const unsigned long long* writers, int n)
{
    int i;
    if (ret == 0 || writers == 0) return 0;
    for (i = 0; i < n; ++i)
        if (writers[i] != 0 && writers[i] == ret) return 1;
    return 0;
}

/* THE SECTOR A DOOR KEY NAMES.  A door key is "<record>@<sx>,<sy>@<position>" (ObjectPositionKey), so the sector is read off the
   key without touching the door.  1 = parsed; anything else leaves *sx / *sy alone. */
inline int DoorKeySector(const char* key, int* sx, int* sy)
{
    const char* p = key;
    int x = 0, y = 0, nx = 0, ny = 0;
    if (key == 0 || sx == 0 || sy == 0) return 0;
    while (*p != 0 && *p != '@') ++p;
    if (*p != '@') return 0;
    ++p;
    while (*p >= '0' && *p <= '9') { if (++nx > 4) return 0; x = x * 10 + (*p - '0'); ++p; }
    if (nx == 0 || *p != ',') return 0;
    ++p;
    while (*p >= '0' && *p <= '9') { if (++ny > 4) return 0; y = y * 10 + (*p - '0'); ++p; }
    if (ny == 0 || *p != '@') return 0;
    *sx = x; *sy = y;
    return 1;
}

/* ANOTHER GAME HAS JUST LOADED A SECTOR AND ASKED THIS GAME, ITS HOLDER, TO CATCH IT UP (the world server's catch-up ask).  Every door this
   game has registered in that sector, in a resting state, is re-offered as the holder's word; the asker applies it when its own copy
   of the door registers, so after any (re)load the holder's doors win.  1 = re-offer this row. */
inline int DoorServesSectorAsk(int keyParsed, int rowSx, int rowSy, int askSx, int askSy, int state)
{
    if (keyParsed == 0) return 0;
    if (rowSx != askSx || rowSy != askSy) return 0;
    return (state == kDoorClosed || state == kDoorOpen) ? 1 : 0;
}
/* ONE CATCH-UP ASK NAMES SEVERAL SECTORS (askX[i], askY[i] are one sector): a resting door of any of them is re-offered. */
inline int DoorServesCatchup(int keyParsed, int rowSx, int rowSy, const int* askX, const int* askY, int n, int state)
{
    int i;
    if (askX == 0 || askY == 0) return 0;
    for (i = 0; i < n; ++i)
        if (DoorServesSectorAsk(keyParsed, rowSx, rowSy, askX[i], askY[i], state) != 0) return 1;
    return 0;
}

/* A PLAYER WHO ENTERS THE WORLD MAY BE A RESTARTED GAME, whose door counter starts again from 1.  The newest-wins record this game
   keeps per door would refuse every door message from that player until its new counter passed the old one, so the record is
   forgotten when the player enters.  arrivedSlot -1 = the arrival names no single player: every publisher's record is forgotten
   (the cost is at most one late message from a game that did not restart taken out of order).  1 = forget this held row's record. */
inline int DoorGenForgetOnArrival(long heldGen, int heldSlot, int arrivedSlot)
{
    if (heldGen < 0) return 0;
    if (arrivedSlot < 0) return 1;
    return (heldSlot == arrivedSlot) ? 1 : 0;
}
/* IS A PUBLISHER ON RECORD FOR A HELD ROW.  The record is a player key (doors.cpp: net::PlayerKeyNow) - RelayPeerId(slot) =
   0x80000000 | slot, which is NEGATIVE stored as a long, or a session peer's raw id - and -1 is the empty marker (no record, or
   forgotten).  Keys top out at 0x8000FFFF, so -1 is never a key.  1 = a publisher is on record (its slot can be asked). */
inline int DoorHeldPublisherKnown(long fromPeer)
{
    return (fromPeer != -1) ? 1 : 0;
}

/* A ROW THAT GAVE UP AFTER THREE CORRECTIONS THAT DID NOT TAKE IS TRIED AGAIN WHEN A NEW WORD FOR ITS DOOR IS ACCEPTED - the
   holder's answer, or another game's report reaching the holder.  The give-up stops a correction repeating every tick; a new
   message is a new event and buys at most three more tries.  A refused word re-arms nothing.  1 = clear the give-up. */
inline int DoorWordReArmsGiveUp(int accepted, int gaveUp)
{
    return (accepted != 0 && gaveUp != 0) ? 1 : 0;
}

/* THE HELD TABLE IS FULL.  An entry may be taken for a new key only when no live door here carries its key and no report waits on
   it: its stored word is then for a door this game has not loaded, and the holder sends it again when this game loads that sector
   (DoorServesSectorAsk).  1 = evictable. */
inline int DoorHeldEvictable(int used, int hasLiveRow, int pendingActor)
{
    return (used != 0 && hasLiveRow == 0 && pendingActor == 0) ? 1 : 0;
}
/* Of the evictable entries the one used longest ago is taken.  1 = `stamp` is older than the best so far (or there is none yet);
   the stamps are a wrapping 32-bit use counter, compared by signed difference. */
inline int DoorHeldOlder(unsigned long stamp, unsigned long bestStamp, int haveBest)
{
    if (haveBest == 0) return 1;
    return ((long)(stamp - bestStamp) < 0) ? 1 : 0;
}
/* WHICH EVICTABLE ENTRY GIVES WAY: evicting an entry forgets its message counter and any word stored in it, so an entry holding no
   holder word goes before one that holds one, and among equals the one used longest ago.  1 = the candidate is a better victim than
   the best so far (or there is none yet). */
inline int DoorHeldBetterVictim(int candHasWord, unsigned long candStamp, int bestHasWord, unsigned long bestStamp, int haveBest)
{
    if (haveBest == 0) return 1;
    if ((candHasWord != 0) != (bestHasWord != 0)) return (candHasWord == 0) ? 1 : 0;
    return DoorHeldOlder(candStamp, bestStamp, 1);
}
/* A ROW DROPPED BY THE ACTIVE-ZONE CHECK while the holder's word for its door is stored here: the word is marked to look its door up
   again through the parent building when it is next due (doors.cpp DropRowKeepWord).  1 = mark. */
inline int DoorDropMarksReFind(long heldState)
{
    return (heldState == kDoorClosed || heldState == kDoorOpen) ? 1 : 0;
}
/* A held entry with no row here is looked up through its building when a report waits on it or its word is marked (above). */
inline int DoorHeldResolveDue(int pendingActor, int reFind)
{
    return (pendingActor != 0 || reFind != 0) ? 1 : 0;
}
/* A CATCH-UP ASK NAMES SECTORS (askX[i], askY[i] are one sector).  1 = the sector a door key names (DoorKeySector) is one of them. */
inline int DoorKeyInAsk(int keyParsed, int sx, int sy, const int* askX, const int* askY, int n)
{
    int i;
    if (keyParsed == 0 || askX == 0 || askY == 0) return 0;
    for (i = 0; i < n; ++i)
        if (sx == askX[i] && sy == askY[i]) return 1;
    return 0;
}
/* A DOOR THIS GAME KNOWS ONLY BY KEY - a held entry with no row here (the row dropped by the active-zone check, the purge or the full
   registry, and the engine has not moved the door since) - in a sector a catch-up ask names: the entry is marked, and the tick looks
   its door up through its building (doors.cpp DoorResolveByKey, one look-up per tick) and re-offers it if found.  1 = mark. */
inline int DoorServeMarksHeld(int inAsk, int hasRow)
{
    return (inAsk != 0 && hasRow == 0) ? 1 : 0;
}
/* A held entry with no row here is looked up through its building when a report waits on it, its word is marked (DoorDropMarksReFind)
   or a catch-up ask marked it (DoorServeMarksHeld).  1 = look it up. */
inline int DoorHeldLookupDue(int pendingActor, int reFind, int serveFind)
{
    return (DoorHeldResolveDue(pendingActor, reFind) != 0 || serveFind != 0) ? 1 : 0;
}
/* THE APPLY LOOP VISITS a held entry that holds the holder's word, carries an actor report or carries a catch-up mark.  1 = visit. */
inline int DoorHeldVisited(long heldState, int pendingActor, int serveFind)
{
    return (heldState >= 0 || pendingActor != 0 || serveFind != 0) ? 1 : 0;
}
/* DOES A DOOR KEY NAME A GATE, for the holder-word line budget: the word "gate" anywhere in the key, in any case.  1 = it does. */
inline int DoorKeyNamesGate(const char* key)
{
    const char* p;
    if (key == 0) return 0;
    for (p = key; *p != 0; ++p)
    {
        int i;
        for (i = 0; i < 4; ++i)
        {
            char c = p[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != "gate"[i]) break;
        }
        if (i == 4) return 1;
    }
    return 0;
}
/* THE HOLDER-WORD LINE BUDGET (doors.cpp DoorWordLine).  A key with a held entry has its own budget of perKeyCap lines, so one
   catch-up's doors cannot spend the line a gate needs; a gate key's lines print within that budget whatever the session total says;
   every other line also needs room in the session total (claimed by the caller, totalCap).  hasSlot 0 = the key has no held entry,
   and only the session total applies.  1 = print. */
inline int DoorWordLinePrints(int hasSlot, long keyLines, long totalLines, int isGate, long perKeyCap, long totalCap)
{
    if (hasSlot != 0 && keyLines > perKeyCap) return 0;
    if (hasSlot != 0 && isGate != 0) return 1;
    return (totalLines <= totalCap) ? 1 : 0;
}

}   /* namespace coopdoor */
