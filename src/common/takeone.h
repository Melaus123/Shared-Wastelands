#ifndef COOP_TAKEONE_H
#define COOP_TAKEONE_H
/* par1 (parity P1): an AI take-one (Inventory::takeOneItemOnly 0x749FF0 - eating, hauling, medic stocking, a builder fetching
   materials) from a storage box. The pure halves of the drain's decision (items.cpp PASS 2f and ItTakeOneConsume) and of the
   requester's answer (ApplyItemConfirm). Header-only, no engine types, so the offline suite runs every branch.

   THE DUPLICATE THIS ENDS. The engine lowers the box stack by one and hands the unit to the task. On a box THIS game writes the
   lowered count is published as before. On a box the OTHER game writes the op 2 was dropped (boxUnpaired): the taker kept its
   unit, the writer's box kept its unit, and the next box check put the unit back on the taker's copy = one unit made twice.

   NOW, on a box the other game writes:
     TRANSFER  the unit landed in our own character's pack in the same drain (hauling, medic stocking): the take is UNDONE here
               (the unit leaves the pack, the stack goes back up by one) and the ordinary cross-game TAKE request asks the
               writer for one unit. The taker keeps a unit only on the writer's confirm; a refusal leaves the unit in the box.
     CONSUME   the unit was used where it was taken (eaten, fed into a build) or went somewhere we cannot name: it cannot be
               taken back; since T-338 our copy of the box stays one short (held, shown at once) until the answer - a refusal or a
               lapse raises it by one again (ItShowSettle; the writer's own take publishes the loss to us) - and the
               same TAKE request of one unit asks the writer to remove it. The confirm places nothing and answers ok, so the
               writer destroys the unit it holds. A refusal leaves the unit in the box and the used one is the booked residual.

   review-par1 fold:
     F1  a TRANSFER whose undo fails (the unit or the stack could not be found) has left the unit in our pack; it goes the
         CONSUME way instead (AfterTransfer), so the writer still removes one unit and the unit is never on both games.
     F2  a unit whose ADD landed on a character this game does not own (a puppet running the task) is not our take: dropped
         as before (boxUnpaired, notOurTaker), nothing sent.
     F3  a CONSUME names the base sid of the stack at the cell; a confirm for any other item is answered 'not placed', so the
         writer puts back what it took, and the used unit is the booked residual, as a refusal.
     F4a an AI TRANSFER is never priced (trade 0), so it never refunds.
     F6  the CONSUME undo is RELATIVE: the stack goes up by one from whatever it holds now (UndoQty). */
namespace cooptake1
{
const int kT1Drop = 0;       /* not this road: a box with no holder, or an unload - the caller's existing counters decide */
const int kT1Publish = 1;    /* a box THIS game writes: the op 2 is published, unchanged */
const int kT1Transfer = 2;   /* the other game's box, the unit is in our own pack: undo + TAKE request, the unit only on confirm */
const int kT1Consume = 3;    /* the other game's box, the unit is used or unnamed: undo the box's loss + TAKE request, place nothing */

/* boxMine: this game writes the box. boxHeldByOther: the other game does. landedInOwnPack: an ADD of the very unit onto one of
   our own characters follows in the same drain. leftAgain: the unit moved on again in that drain (so it is not in the pack). */
inline int Route(int boxMine, int boxHeldByOther, int landedInOwnPack, int leftAgain)
{
    if (boxMine != 0) return kT1Publish;
    if (boxHeldByOther == 0) return kT1Drop;
    if (landedInOwnPack != 0 && leftAgain == 0) return kT1Transfer;
    return kT1Consume;
}

const int kT1CfNothing = 0;    /* refused: nothing changes here (a transfer's unit stays in the box) */
const int kT1CfPlace = 1;      /* a confirmed TRANSFER: the ordinary TAKE placement - the taker gets its unit now */
const int kT1CfAnswerOk = 2;   /* a confirmed CONSUME: nothing is placed; the writer is answered ok and destroys what it holds */
inline int OnConfirm(int ok, int consume)
{
    if (ok == 0) return kT1CfNothing;
    return (consume != 0) ? kT1CfAnswerOk : kT1CfPlace;
}
/* A CONSUME's confirm that arrives after our own 10 s timeout: an ok is still answered ok (the writer holds the unit in escrow
   for our answer); nothing is ever placed. 1 = answer ok. */
inline int LateConsume(int ok) { return (ok != 0) ? 1 : 0; }

/* review-par1 fold F1: what a TRANSFER is once ItCrossTransfer has run. `undone` = it took the unit back out of our pack and
   raised the stack; otherwise the unit is still in our pack and the box one short here, so it is a CONSUME (the writer is asked
   to remove one unit; since T-338 our copy's loss is held until the answer). */
inline int AfterTransfer(int undone) { return (undone != 0) ? kT1Transfer : kT1Consume; }

/* The undo of the box's local loss, RELATIVE (review-par1 fold F6): `live` is the count of the stack the hook lowered, read
   now, and it goes up by one - so two takes from one stack in one drain each give their unit back. A stack of none is not
   this write (-1); the caller books it. */
inline int UndoQty(int live)
{
    if (live < 1) return -1;
    return live + 1;
}

/* t338-consumehold (T-338, owner 108): A CONSUME SHOWS AT ONCE. The engine's take already lowered our copy's stack by one; instead of
   raising it again at the send (the bounce), the unit is HELD off the stack (coopshow kShowTakeUnits, one unit) until the answer.
   showWanted: coopshow::ShowWanted said yes; stackHere: the stack the hook lowered is still the object in its cell (read at the send).
   1 = hold (no undo now); 0 = today's undo at the send (UndoQty, F6). */
inline int ConsumeHolds(int showWanted, int stackHere) { return (showWanted != 0 && stackHere != 0) ? 1 : 0; }
/* The answer to a held CONSUME, as the `ok` the show settle takes: a yes naming the item asked for (F3) is a yes - the held unit is
   gone for good (nothing placed, nothing raised: the writer's own take publishes the same count). A refusal, a lapse or a yes for
   another item (the writer puts back what it took) is a no - the unit goes back on the stack, relative, unless the writer's own move
   named that cell meanwhile (its count is the truth). */
inline int ConsumeSettleOk(int ok, int sameItem) { return (ok != 0 && sameItem != 0) ? 1 : 0; }
}
#endif
