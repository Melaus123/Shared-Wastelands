#pragma once
/* T-1266: the cross-game move's undo (items.cpp ItRevert) picks what it does from LIVE facts, and a take that could neither be sent
 * nor undone gets a fixed ending (items.cpp ItCrossTransfer's two failure exits) - so the item exists once. Shared by the plugin and
 * the offline tests. Pure: no engine, no Windows header. C++03.
 *
 * WHY (run T2109, Confirmed; root-fix read 2026-11-07, decision 1540, design B). ItRevert chose its arm by what it had itself done
 * (`moved`), not by where the object is: after a take-out that failed, or a refused source square, with the object on ANOTHER square of
 * this game's section, its record-remake arm still ran (its recorded square was empty) - a second copy made, or made and destroyed and
 * said as 'placeFailed'. And both failure exits of ItCrossTransfer returned with the move standing and nothing asked of the game that
 * writes the other side: a take left the item on both games.
 *
 * THE ARM (RevertArm), from what the undo reads just before it acts:
 *   where         - the moved object: on its recorded square / on another square of the same section / gone from that section
 *   squareEmpty   - the recorded square holds nothing (read only when the object is gone)
 *   haveRecord    - the moved item's record is in hand (a remake needs it)
 *   liveCount     - the object's live count (0 = not read)
 *   units         - the units a TAKE recorded (0 = not given: a give, or a caller that does not compare - the old rule)
 *   sameAtSquare  - (gone, a take) the recorded square holds another object of the moved item's record: its units merged into it
 *   sameInSection - (gone, a take) an object of that record is somewhere in the section: where the units went cannot be told
 * THE ENDING (TakeEnding / LeftoverAct): a TAKE whose move still stands here is ended by removing this game's leftover - the units it
 * took - but only when a live fact shows the owner still has them, never while a charge for it stands, and never on a road whose
 * caller ends it itself. A give is left as it was: nothing of the other game's is on this game, and removing would destroy the
 * player's own item. */
#include "screentake.h"

namespace undoarm {

const int kWhereGone = 0, kWhereRecorded = 1, kWhereOther = 2;

const int kArmNothing = 0;    /* the object is gone and its square holds something else (or there is no record): nothing is done here */
const int kArmPutBack = 1;    /* the object is here: it is moved back (ask first - the source square is tested before it is taken out) */
const int kArmRemake = 2;     /* the object is gone from the section and its square is empty: it is made again from its record */
const int kArmLeftover = 3;   /* the object is here and holds MORE than was moved (the engine merged it onto a stack this game already
                                 had): moving it back would carry this game's own units, so nothing is moved back and the caller's
                                 ending takes only the moved units off it (kRevHoldsMore) */
const int kArmMergedAt = 4;   /* fold 1 (M4): a take's object is gone and the stack on its square has its record - the engine merged the
                                 units into it: nothing is touched, the ending takes the moved units off that stack (kRevMergedInto) */
const int kArmMergedSomewhere = 5;   /* fold 1 (M4): a take's object is gone and a stack of its record is elsewhere in the section: never
                                        remade (a remake plus a request would double the units on a yes); nothing is touched (kRevMergedUnknown) */
inline int RevertArm(int where, int squareEmpty, int haveRecord, int liveCount, int units, int sameAtSquare, int sameInSection)
{
    if (where == kWhereGone)
    {
        if (units > 0 && sameAtSquare != 0) return kArmMergedAt;
        if (units > 0 && sameInSection != 0) return kArmMergedSomewhere;
        return (squareEmpty != 0 && haveRecord != 0) ? kArmRemake : kArmNothing;
    }
    if (units > 0 && liveCount > units) return kArmLeftover;
    return kArmPutBack;   /* the object holds what was moved, or less, or a count is not known (the old rule) */
}
inline const char* ArmName(int a)
{
    switch (a)
    {
    case kArmPutBack: return "putBack";
    case kArmRemake: return "remake";
    case kArmLeftover: return "leftover";
    case kArmMergedAt: return "mergedAt";
    case kArmMergedSomewhere: return "mergedSomewhere";
    default: return "nothing";
    }
}

/* ---- fold 1 (M3): does a live fact show the game that writes the other side still has the item? ----
   ghostIsBox 1: the other side is a box - its area must be held by another game (the box verdict kBoxHeld), else no game writes it and
   this game's leftover may be the only copy. 0: a character (or a pack on one) - another game must run it (an owner row here, and not
   this game's). */
inline int OwnerStillHas(int ghostIsBox, int boxHeldElsewhere, int uidRunElsewhere)
{
    return (ghostIsBox != 0) ? (boxHeldElsewhere != 0 ? 1 : 0) : (uidRunElsewhere != 0 ? 1 : 0);
}

/* ---- the fixed ending at ItCrossTransfer's two failure exits ---- */
const int kEndNone = 0;         /* a take whose object is not known to stand here (destroyed, gone, faulted, a split's units): nothing to remove */
const int kEndLeftover = 1;     /* a take whose move stands here, the owner still has it, no charge stands: remove this game's leftover */
const int kEndGive = 2;         /* a give: unchanged (nothing of the other game's is here; removing would destroy this game's own item) */
const int kEndCaller = 3;       /* fold 1 (M5, L3): the caller ends it itself - the AI take-one (it asks the writer to consume), the
                                   right-click pieces (they move the units back), a take whose units the caller already made again */
const int kEndKeepUnheld = 4;   /* fold 1 (M3): no live fact shows the owner still has it - the leftover is kept (it may be the only copy) */
const int kEndRefundAfter = 5;  /* fold 1b (N2): a paid take (a shop or caravan move the trade call did not intercept): the leftover is
                                   removed FIRST, and its charge is given back only when that removal succeeded (RefundAfterRemoval) -
                                   a stuck or not-found removal keeps both the item and the charge, as before the fix */
const int kEndKeepPaid = 6;     /* fold 1b (N4): whether a charge stands cannot be told (a trade window open, the far box's building not
                                   resolved): the buyer keeps the item - never removed while a charge may stand */
/* halfMissing 1 = the exit where a half of the move could not be found (no undo ran, the move stands as the engine left it);
   0 = the put-back exit, with ItRevert's reason. callerEnds: the road's caller has its own ending. ownerHas: OwnerStillHas.
   priced (PriceState): 0 no charge, 1 a shop / caravan move whose charge the engine took, 2 not known. */
inline int TakeEnding(int dir, int halfMissing, int why, int callerEnds, int ownerHas, int priced)
{
    if (dir != 0) return kEndGive;
    if (callerEnds != 0) return kEndCaller;
    if (halfMissing == 0 && why != coopstake::kRevCellTaken && why != coopstake::kRevTakeOutFailed && why != coopstake::kRevHoldsMore
        && why != coopstake::kRevMergedInto) return kEndNone;
    if (ownerHas == 0) return kEndKeepUnheld;
    if (priced == 2) return kEndKeepPaid;
    return (priced != 0) ? kEndRefundAfter : kEndLeftover;
}
/* fold 1b (N4): is a charge standing for this move. shopKnown: the one shop rule (a shop piece's or a caravan's move) answered yes;
   tradeOpen: a money trade window is open; a far half that is a box whose building did not resolve cannot be asked the shop rule */
inline int PriceState(int shopKnown, int tradeOpen, int ghostIsBox, int bldResolved)
{
    if (shopKnown != 0) return 1;
    return (tradeOpen == 1 && ghostIsBox != 0 && bldResolved == 0) ? 2 : 0;
}
/* fold 1b (N2): RefundAfterRemoval (below the counters) - the refund of a paid take follows a removal that succeeded, and nothing else */

/* what the leftover removal does with the object it finds (live facts again, read just before it acts). intoOther 1: the moved object
   is gone and its units merged into the stack on its square (kRevMergedInto) - that stack is only ever lowered, never removed whole */
const int kLoNothing = 0;   /* not here, or its count / the moved units not known: nothing is removed (never guess at what is ours) */
const int kLoWhole = 1;     /* it holds the moved units or fewer: the object is taken out and destroyed */
const int kLoLower = 2;     /* it holds more (merged onto this game's own stack): its count is lowered by the moved units */
inline int LeftoverAct(int found, int liveCount, int units, int intoOther)
{
    if (found == 0 || liveCount <= 0 || units <= 0) return kLoNothing;
    if (liveCount > units) return kLoLower;
    return (intoOther != 0) ? kLoNothing : kLoWhole;
}

/* fold 1 (M1): what goes on the wire after a lowered stack - the other games' copy of this game's side must get the corrected stack.
   The removal of that stack may already have gone out (the drag's 'merge gone at send' fallback, a merged split's drain), so an ADD of
   it as it is now; and its absolute count, for a copy that still holds it (where the ADD is refused as occupied). No record = the
   count only. Not lowered = nothing (a whole removal took out an object the other games never heard of). */
const int kPubNone = 0, kPubCount = 1, kPubAddAndCount = 2;
inline int LowerPublish(int lowered, int haveRecord)
{
    if (lowered == 0) return kPubNone;
    return (haveRecord != 0) ? kPubAddAndCount : kPubCount;
}

/* the ending's counters (the report's revertEndedBy) - one per ending taken */
const int kEbRemoved = 0;    /* the leftover was removed (taken out whole, or the moved units taken off a stack) */
const int kEbNotHere = 1;    /* the ending was to remove the leftover, and no object of the move was found here to remove */
const int kEbStuck = 2;      /* the object was here and could not be removed (the engine handed nothing back, a write failed, a fault) */
const int kEbNone = 3;       /* a take with nothing known to remove (kEndNone) */
const int kEbGive = 4;       /* a give - unchanged */
const int kEbCaller = 5;     /* the caller ends it (kEndCaller) */
const int kEbKeptUnheld = 6; /* kept: no live fact shows the owner still has it (kEndKeepUnheld) */
const int kEbKeptPaid = 7;   /* kept: whether a charge stands could not be told (kEndKeepPaid) */
const int kEbCount = 8;
inline int RefundAfterRemoval(int end, int endedBy) { return (end == kEndRefundAfter && endedBy == kEbRemoved) ? 1 : 0; }
/* fold 1b (N1): an ending that KEPT a take's object on this game's side while it holds more than was moved (a merged stack) re-sends that
   stack as it stands - an ADD, then its count - because its removal may already have gone out (the drag's 'merge gone at send'
   fallback sends it before the undo): the other games' copy must never lose the player's own units. */
inline int KeptStackPublish(int endedBy, int found, int liveCount, int units, int haveRecord)
{
    if (endedBy != kEbKeptUnheld && endedBy != kEbKeptPaid && endedBy != kEbStuck) return kPubNone;
    if (found == 0 || units <= 0 || liveCount <= units) return kPubNone;
    return (haveRecord != 0) ? kPubAddAndCount : kPubCount;
}
/* fold 1b (N3): a right-click piece whose caller's move-back moved nothing BEFORE touching the destination (its source not found, or no
   room in their copy) falls back to the exit's own ending (the same owner and charge rules) - when the exit left one waiting */
inline int CallerFallback(int movedBackUnits, int untouched, int deferred)
{
    return (movedBackUnits == 0 && untouched != 0 && deferred != 0) ? 1 : 0;
}
/* TEST-ONLY lever `itemtest undofail` (run T2166): does the armed lever fire on THIS move. Only a plain take from another game's
   CHARACTER whose put-back is ItRevert's (no split, no caller's own ending, no take-one, no shop move): there the lever makes the put-back
   exit's real condition - a placeholder on the source square, so the put-back finds it taken (kRevCellTaken) - and the exit's own
   ending runs. Any other move leaves it armed. */
inline int UndoFailLeverFires(int armed, int dir, int srcIsBox, int isSplit, int takeSplitDone, int aiTakeOne, int callerEnds,
                              int shopMove)
{
    return (armed != 0 && dir == 0 && srcIsBox == 0 && isSplit == 0 && takeSplitDone == 0 && aiTakeOne == 0 && callerEnds == 0
            && shopMove == 0) ? 1 : 0;
}
/* the counter of an ending that removes nothing (kEndLeftover's and kEndRefundAfter's are the removal's own answer) */
inline int EndedByOf(int end)
{
    switch (end)
    {
    case kEndGive: return kEbGive;
    case kEndCaller: return kEbCaller;
    case kEndKeepUnheld: return kEbKeptUnheld;
    case kEndKeepPaid: return kEbKeptPaid;
    default: return kEbNone;
    }
}
inline const char* EndedByName(int eb)
{
    switch (eb)
    {
    case kEbRemoved: return "removed";
    case kEbNotHere: return "notHere";
    case kEbStuck: return "stuck";
    case kEbNone: return "none";
    case kEbGive: return "give";
    case kEbCaller: return "caller";
    case kEbKeptUnheld: return "keptUnheld";
    case kEbKeptPaid: return "keptPaid";
    default: return "unknown";
    }
}
inline const char* EndedByText(int eb)
{
    switch (eb)
    {
    case kEbRemoved:    return "T-1266 ending: this game's leftover of the take was removed - the item is now only on the game that writes"
                               " the other side (its copy here is short of it until that game's next record)";
    case kEbNotHere:    return "T-1266 ending: remove this game's leftover - but no object of the move was found on this game's side, so"
                               " nothing was removed";
    case kEbStuck:      return "T-1266 ending: remove this game's leftover - the object is here but could not be removed, so the item is"
                               " still on both games";
    case kEbNone:       return "T-1266 ending: none - nothing of this take is known to stand on this game's side, so nothing was removed";
    case kEbGive:       return "T-1266 ending: none for a give - nothing of the other game's is on this game, and removing would destroy"
                               " this game's own item";
    case kEbCaller:     return "T-1266 ending: left to the caller - its road ends the move itself (a take-one's consume, a right-click"
                               " piece's move back, units the caller already made again)";
    case kEbKeptUnheld: return "T-1266 ending: kept - no live fact shows the game that writes the other side still has the item (a box"
                               " no game holds, or a character no other game runs), so this game's copy may be the only one";
    case kEbKeptPaid:   return "T-1266 ending: kept - a trade window is open and the far box could not be resolved, so whether a"
                               " charge stands is not known and the buyer keeps the item";
    default:            return "T-1266 ending: unknown";
    }
}

/* T-1266 fix attempt 1 (H-T1266-putback, run T2166 LEG 2): THE UNDO'S CELL RULE (items.cpp ItUndoCellRule). An animal's main section
   carries an allowed-items list no item matches (decomp_6296a0:215-216, decomp_74b0f0), so the engine's fitsAt 0x74B300 refused every
   put-back into a knocked-out or dead animal and a take from it was never asked of its owner. fitsAt asks four things (decomp_74b300):
   the section's on/off byte (+0xD0), the allowed-items test 0x74B0F0 (the list +0x98/+0xA0, the type filter +0xB8/+0xC0, the +0x38
   flag), and the footprint against the section's own width / height (+0x30 / +0x34).
   `rule`:
     kCellFits      - a cell the item did not just leave: the engine's fitsAt, whole (the rule before the fix)
     kCellJustLeft  - the cell the item (or the stack it stands for) has JUST LEFT - in the same pass or a round trip later (the
                      show-at-once settle): the item was legitimately there, so neither the on/off byte nor the allowed-items list is
                      asked - only the grid and the shape. Fold 1b (recheck-T1266-putback MED): an animal's main section is ALWAYS
                      switched off (en=0, 05-findings, Confirmed), so asking the on/off byte (fold 1's round-trip-later rule) refused every
                      settle into an animal; the undo rule never asks it.
   Every other input is 1 = yes, 0 = no:
     inGrid       - ItSlotInRange: the origin and the footprint lie on the content grid (_addItem throws off it, decomp_748be0:99-103)
     inShape      - the footprint within the section's own width / height (fitsAt's bound, kept for a just-left cell)
     engineFits   - fitsAt's whole answer (asked for kCellFits only; it includes the shape bound)
     visibleOther - a grid cell of the footprint holds another item (asked only when the other tests pass - getItemAt throws off grid)
   1 = the undo may place the item there. */
const int kCellFits = 0, kCellJustLeft = 1;
inline int UndoCellFree(int rule, int inGrid, int inShape, int engineFits, int visibleOther)
{
    if (inGrid == 0 || visibleOther != 0) return 0;
    if (rule == kCellJustLeft) return (inShape != 0) ? 1 : 0;
    return (engineFits != 0) ? 1 : 0;
}

}   /* namespace undoarm */
