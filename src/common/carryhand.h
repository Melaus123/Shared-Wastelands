/* src/common/carryhand.h - T-1467 (owner 1863 a / 1864 a / 1865 a, 2026-11-22; cause F1933 / F1935, design read F1936): A BODY CARRIED
   ACROSS INTO AN AREA ITS OWN GAME HAS NOT LOADED IS HANDED, ALONE, TO THE CARRIER'S GAME.
   Before it: game B's character carries a body game A runs; B's once-a-second announce pass judged B's character by its own spot only,
   so the step into an area A has not loaded withdrew the carrier from A (UNLOAD), A removed its copy of the carrier, A's carry watcher
   saw no carrier and sent a carry break, and B dropped the body.
   Now:
     - THE HOLD (B, the carrier's game): while a character B runs carries a body another game runs, B's announce pass does not withdraw
       it - the withdrawal waits for the body's hand-over. It ends only on events: the body handed to B (it is then B's own), B refusing
       the hand-over, the carry ending, or the body's game gone (its copy is removed here, so nothing names it). Never a timer.
     - THE TRIGGER (A, the body's game): while a copy carrier (another game's character) carries it, A's engine STARTS its countdown to
       put the copy carrier's squad away (attempt 2, owner 1870; F1964: ActivePlatoon+0xB0 falls below 4.0 once the squad's stored
       point leaves A's loaded area pulled in 160) - or the body's own live spot leaves the areas A has loaded (attempt 1, kept) ->
       A offers that one body to the carrier's game (an XFER of one member, reason coopsquad::kXferReasonCarry).
       Its squad stays with A (owner 1865 a). Where both games share the area nothing changes: the body's own game decides the carry
       (owner 333 a).
     - THE COUNTDOWN HOLD (A, attempt 2): while that offer awaits its answer, A holds the copy carrier's squad countdown where it stood
       (its value is written back each watcher pass when the engine counted below it), so A's engine does not put the copy carrier
       away - and end the carry - before the hand-over is decided. It ends only on events: the answer (taken / refused / the flight
       abandoned by its own send count), the carrier's game gone, the carry ending here. Never a timer of its own. Only a squad whose
       member carries a body A runs (not a player's) is ever held; every other squad's countdown is untouched.
     - THE TAKE (B): taken only when B has a copy of it and a character B runs carries it, and B's engine keeps its spot (live reads).
       While that character still carries it, B's squad pass leaves it out (no follow-leader / position hand-over straight back).
     - THE WATCHER (A): a carried body handed to the carrier's game is forgotten without a carry break.
     - OWNER AWAY (A): A's copy of a carrier holding a body A runs is not removed by the 'owner in an area not loaded here' rule while
       the body's hand-over is undecided.
   Pure decisions, no engine reads. C++03. */
#ifndef COOP_COMMON_CARRYHAND_H
#define COOP_COMMON_CARRYHAND_H

namespace carryhand {

/* One carry's hand-over on the BODY'S game (A), per body uid: none yet / offered (the XFER in flight) / refused (the carrier's game kept
   none, or the offer was abandoned unanswered) / given (the carrier's game took it). */
enum { kHandNone = 0, kHandOpen = 1, kHandRefused = 2, kHandGiven = 3 };
inline const char* HandStateName(int s)
{
    return s == kHandOpen ? "open" : (s == kHandRefused ? "refused" : (s == kHandGiven ? "given" : "none"));
}

/* THE TRIGGER (A, every watcher pass, read live). bodyMine: A runs the body; carriedByCopy: A's copy of another game's character
   carries it here; bodySpotLoaded: the body's own live spot is in an area A's engine has loaded (1), not (0), unread (-1);
   countdown: the engine's countdown to put the copy carrier's squad away runs (CountdownRuns below, attempt 2);
   handState: this carry's state above; inFlight: another hand-over of the body is in flight; carrierSlot: the slot of the game that
   runs the carrier (-1 unknown); bodyIsPlayers: the body is a player's character (this game's player faction or another player's stand-in;
   unread counts as a player's). 1 = offer the body to that game now. An unread spot alone never triggers. */
inline int HandTrigger(int bodyMine, int carriedByCopy, int bodySpotLoaded, int countdown, int handState, int inFlight, int carrierSlot, int bodyIsPlayers)
{
    if (bodyIsPlayers != 0) return 0;   /* a player's character is never handed over (decision 23): its carry is as before this change */
    if (bodyMine == 0 || carriedByCopy == 0 || carrierSlot < 0) return 0;
    if (countdown == 0 && bodySpotLoaded != 0) return 0;   /* neither the engine's countdown nor the body's spot outside: nothing */
    if (handState != kHandNone || inFlight != 0) return 0;
    return 1;
}

/* THE ENGINE'S COUNTDOWN (A, attempt 2; F1964, decomp_4fe140:43-60): the engine's squad keep test sets ActivePlatoon+0xB0 to 4.0 while
   the squad's stored point (+0xB4 / +0xBC) stands inside its loaded area pulled in 160, to 5.0 when that point has no area at all (no
   countdown), and otherwise subtracts the frame's game time; below 0 the squad is put away (Platoon::deactivate, caller 0x6BABEA).
   A value under 4.0 alone is not proof: the keep test returns early without touching +0xB0 when the squad's +0xE8 is set
   (decomp_4fe140:22), so a leftover value can stand still (fold LOW-1). The countdown runs only when the engine is seen counting: this
   pass's read is LOWER than the previous pass's read of the same squad (which covers a previous 4.0 / 5.0 too).
   prevRead / prev: the previous pass's read of this squad (0 = none - a first look, or the carrier's squad changed); nowRead / now: this
   pass's. 1 = the countdown runs. By the bits for finiteness (the plugin builds with /fp:fast). */
const float kGraceFull = 4.0f;
inline int GraceFinite(float v)
{
    union { float f; unsigned int u; } b; b.f = v;
    return (b.u & 0x7F800000u) != 0x7F800000u ? 1 : 0;
}
inline int CountdownRuns(int prevRead, float prev, int nowRead, float now)
{
    if (prevRead == 0 || nowRead == 0) return 0;
    if (GraceFinite(prev) == 0 || GraceFinite(now) == 0) return 0;   /* not finite: unread */
    if (!(now < kGraceFull)) return 0;
    return now < prev ? 1 : 0;   /* the engine subtracted the frame's game time since the last pass */
}

/* THE COUNTDOWN HOLD (A, every watcher pass while the carry is kept here, read live). bodyMine / carriedByCopy: as the trigger;
   handState: this carry's state; offerInFlight: this game's offer of the body still awaits its answer (the flight is open);
   carrierSlot: the carrier's game (-1 gone); bodyIsPlayers: as the trigger (a player's character: never held - no hand-over exists);
   graceRead: the squad's +0xB0 was read; carrierSpotLoaded: the copy carrier's own live spot is in an area this game has loaded (1), not (0),
   unread (-1). 1 = hold the copy carrier's squad countdown. Ends on events only: the answer settles the state (given / refused, or the
   flight abandoned by its own send count -> refused), the carrier's game goes, the carry ends here, or (fold LOW-3: resends that fail while
   the other game still counts as linked, or a hand-over pass that stops early, would otherwise hold it for as long as the carry lasts) the
   copy carrier's own spot has left every area this game has loaded. An unread spot is no event: it holds on. */
inline int GraceHold(int bodyMine, int carriedByCopy, int handState, int offerInFlight, int carrierSlot, int bodyIsPlayers, int graceRead, int carrierSpotLoaded)
{
    if (bodyIsPlayers != 0) return 0;
    if (bodyMine == 0 || carriedByCopy == 0 || carrierSlot < 0 || graceRead == 0) return 0;
    if (carrierSpotLoaded == 0) return 0;
    return (handState == kHandOpen && offerInFlight != 0) ? 1 : 0;
}
/* the value a hold keeps: held >= 0 the value this hold froze; else (its first pass) the value read now, raised to kGraceHoldFloor when
   lower (a hold that starts late in a countdown is not put away by the engine's next subtraction - one frame's game time at speed 3 is
   ~0.05) and never above the full 4.0. */
const float kGraceHoldFloor = 0.5f;
inline float GraceFreeze(float held, float grace)
{
    if (held >= 0.0f) return held;
    if (grace < kGraceHoldFloor) return kGraceHoldFloor;
    return grace > kGraceFull ? kGraceFull : grace;
}
/* 1 = write the frozen value back: the engine counted below it. A value at or above it (the engine reset it to 4.0 - the squad's
   point came back inside - or the 5.0 of no area) is left as the engine wrote it. */
inline int GraceWriteDue(float grace, float frozen) { return grace < frozen ? 1 : 0; }

/* THE ANSWER SETTLES IT (A): the carrier's game named the body in its ACK (taken) -> given; it named none, or the offer was abandoned
   unanswered -> refused. */
inline int HandSettle(int taken) { return taken != 0 ? (int)kHandGiven : (int)kHandRefused; }

/* THE TAKE (B, the carrier's game, read live at the take). count: members in the XFER (a carry hand-over names exactly one, the body);
   alreadyMine: B already runs it (a resend after B's ACK was lost - named again); copyHere: B has a copy of it; carrierMine: a character
   B runs carries that copy here; keepHere: B's engine keeps the body's own spot; bodyIsPlayers: the copy is a player's character (refused,
   decision 23). 1 = take. */
inline int HandTake(int count, int alreadyMine, int copyHere, int carrierMine, int keepHere, int bodyIsPlayers)
{
    if (count != 1 || bodyIsPlayers != 0) return 0;
    if (alreadyMine != 0) return 1;
    return (copyHere != 0 && carrierMine != 0 && keepHere != 0) ? 1 : 0;
}

/* AFTER THE TAKE (B): a body taken by a carry hand-over stays out of this game's squad pass - its follow-leader and position hand-overs, which
   would hand it straight back to its squad's game (the squad is split, owner 1865 a) - while the carrier that held it at the take, a
   character B runs, still carries it (read live). bodyMine: B runs the body; sameCarrierCarries: that carrier's hand names it now;
   carrierMine: B runs that carrier. 1 = keep it out; 0 = the carry ended (or the body left B): its squad's ordinary roads judge it again. */
inline int TakenKeep(int bodyMine, int sameCarrierCarries, int carrierMine)
{
    return (bodyMine != 0 && sameCarrierCarries != 0 && carrierMine != 0) ? 1 : 0;
}

/* THE HOLD (B's announce pass, read live at the decision). wouldUnload: the pass's rule says withdraw (cooplive::kAnnUnload);
   carriedUid: what the character carries (0 nothing); bodyMine: B runs that body; bodyOwnerSlot: the slot of the game that runs it
   (-1 none - its game is gone); refusedHere: B refused this body's carry hand-over during this carry; bodyIsPlayers: the body is a
   player's character (no hand-over exists for it, so no hold). 1 = hold (nothing sent, the character stays announced). */
inline int CarrierHold(int wouldUnload, unsigned int carriedUid, int bodyMine, int bodyOwnerSlot, int refusedHere, int bodyIsPlayers)
{
    if (wouldUnload == 0 || carriedUid == 0u || bodyIsPlayers != 0) return 0;   /* a player's character is never handed: no hold, as before */
    if (bodyMine != 0 || bodyOwnerSlot < 0) return 0;
    return refusedHere != 0 ? 0 : 1;
}

/* THE WATCHER (A, OwnCarryWatch). bodyHere / carrierHere: A still has the body / its copy of the carrier; mine: A runs the body;
   carries: the copy carrier holds the body; handedByCarry: this carry's hand-over gave the body to the carrier's game.
   keep = still carried here (the engine decides, owner 333 a); break = A's engine ended the carry of A's own body (MSG_CARRY_BREAK);
   handed = given to the carrier's game by this carry's hand-over (forgotten, no break); forget = no longer A's another way (no break). */
enum { kWatchKeep = 0, kWatchBreak = 1, kWatchForget = 2, kWatchHanded = 3 };
inline int WatchAct(int bodyHere, int carrierHere, int mine, int carries, int handedByCarry)
{
    if (bodyHere != 0 && carrierHere != 0 && mine != 0 && carries != 0) return kWatchKeep;
    if (mine == 0 && handedByCarry != 0) return kWatchHanded;
    if (bodyHere != 0 && mine != 0) return kWatchBreak;
    return kWatchForget;
}

/* OWNER AWAY (A): 1 = A's copy of a carrier is NOT removed by the 'owner in an area not loaded here' rule - it carries a body A runs
   (carriesBodyRunHere, read live) whose carry hand-over is not refused. Once the body is given it is no longer A's, and the rule
   applies again; once refused, the rule applies as before this change. */
inline int FarRetireKeep(int carriesBodyRunHere, int handState, int bodyIsPlayers)
{
    return (carriesBodyRunHere != 0 && handState != kHandRefused && bodyIsPlayers == 0) ? 1 : 0;
}

}  // namespace carryhand

#endif
