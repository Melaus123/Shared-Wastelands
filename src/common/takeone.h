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

/* T-969 (owner decision 992 b, verbatim: "when a world's box access is not 'shared' and the players are not teammates, the taking
   game checks the access rule before a character takes from a box another game writes"). The take-one hook asks BEFORE the engine's
   take. `policy` is this world's base access option as policy.h numbers it (kPolicyUnknown 0 / kPolicyShared 1 / owner 2 / locked 3;
   items.cpp asserts the two mirrored here); unknown is judged as shared, as the writer's own judge does. boxHeldByOther: another
   game writes the box (the area / owner verdict kBoxHeld). 1 = ask the access rule (BoxAccessAllowed with this game's own player as
   the requester - the decision the writer applies to our TAKE request; teammates are allowed inside it). */
const int kT1PolicyUnknown = 0;
const int kT1PolicyShared = 1;
inline int AccessAsk(int policy, int boxHeldByOther)
{
    if (boxHeldByOther == 0) return 0;
    return (policy == kT1PolicyUnknown || policy == kT1PolicyShared) ? 0 : 1;
}
/* 1 = the engine's take runs; 0 = refused here (the detour returns 0: nothing taken, the character does not get the unit) */
inline int AccessTakeRuns(int asked, int allowed) { return (asked == 0 || allowed != 0) ? 1 : 0; }

/* T-969 fold 2 (review): THE GATE'S WHOLE DECISION, the same on every thread - PolicyJudge's answer for this game's own player,
   from what the take hook can read without the writer decision. ownerCls: boxowner.h's classes (mirrored below, items.cpp checks
   them); known 0 = no owner answer here (never read on the main thread, or not resolvable now). thisGameWrites: this game writes
   the box (the owner rung said mine, or the area rule answers this game) - since T-1199 (decision 1348 a) it no longer lets a take through: an owner away is
   protected as an owner present is. mate: the owner's
   slot is this player's current teammate (rule "member"). peerIsPlayer: PolicyPeerIsPlayer (1 = the stand-in is a player
   faction, so `locked` refuses instead of leaning on the engine's lock).
   DIFFERS FROM PolicyJudge ON PURPOSE: an owner this game cannot read is allowed (counted, kT1GateRunUnread) - PolicyJudge refuses
   'owner-unreadable', but the gate only refuses a KNOWN other player's box (review item 3). */
const int kT1OwnUnreadable = -1;
const int kT1OwnNone = 0;
const int kT1OwnMe = 1;
const int kT1OwnPeer = 2;
const int kT1OwnShared = 3;
const int kT1OwnUnknown = 4;
const int kT1PolicyOwner = 2;
const int kT1PolicyLocked = 3;
const int kT1GateRun = 0;          /* the engine's take runs (today's road) */
const int kT1GateRunUnread = 1;    /* runs: the owner is not known here - counted */
const int kT1GateRunMate = 2;      /* runs: a teammate's box - counted */
const int kT1GateRefuse = 3;       /* refused: the detour returns 0 */
inline int GateDecide(int policy, int known, int ownerCls, int thisGameWrites, int mate, int peerIsPlayer)
{
    if (AccessAsk(policy, 1) == 0) return kT1GateRun;                  /* shared / not known yet: judged as shared */
    if (known == 0 || ownerCls == kT1OwnUnreadable || ownerCls == kT1OwnUnknown) return kT1GateRunUnread;
    if (ownerCls != kT1OwnPeer) return kT1GateRun;                      /* nobody's (an ore mine's too), mine, a shared faction's: allowed */
    (void)thisGameWrites;   /* T-1199 (decision 1348 a): whichever game writes the box - the owner away included - the rule is the same */
    if (mate != 0) return kT1GateRunMate;
    if (policy == kT1PolicyOwner) return kT1GateRefuse;
    if (policy == kT1PolicyLocked) return (peerIsPlayer == 1) ? kT1GateRefuse : kT1GateRun;   /* else the engine's own lock */
    return kT1GateRun;
}
/* T-1199 fold 1 (HIGH, review): ANOTHER PLAYER'S OWNER FACTION. standIn: IsStandInFaction (the stand-in table, pointer compares);
   recordSlot: StandInRecordSlot when it is not a stand-in (the slot its record id coop-p<n> names; < 0 none) - a saved player's
   faction stays out of the stand-in table until that player is resolved (playerfaction.cpp), so after a load with its owner never
   joining only its record id names it; mySlot: this game's slot (MySlotForWire). 1 = another player's.
   Fold 2 (LOW, recheck): before this game knows its own slot (mySlot < 0) no saved player record counts as another player's - this
   game's own record included, which cannot be told apart yet - so nothing is refused by a record alone then. */
inline int OtherPlayerOwner(int standIn, int recordSlot, int mySlot)
{
    if (standIn != 0) return 1;
    if (mySlot < 0) return 0;
    return (recordSlot >= 0 && recordSlot != mySlot) ? 1 : 0;
}
/* T-1199 fold 1 (design B, manager 2026-11-14; decision 1348 a; owner 1480): THE OPEN OF A BOX OR A MACHINE WITH INVENTORY. Under
   Owner only ("who may open a player's chests", rulesfile.h) this game's player's character does not open another non-teammate
   player's box or machine at all - whether or not its owner is in the world (the loot window is not opened: policy.cpp
   detour_KitShowTrade). policy: the base access option; character: the thing opened is a character (the trade window's own rule,
   swaccess::WindowFor, decides those); worldNode: an ore rock (nobody's, owner 1480); otherPlayer: OtherPlayerOwner; mate: that
   player is this game's player's teammate (decision 481). 1 = refused. */
inline int OpenRefuse(int policy, int character, int worldNode, int otherPlayer, int mate)
{
    if (policy != kT1PolicyOwner || character != 0 || worldNode != 0) return 0;
    return (otherPlayer != 0 && mate == 0) ? 1 : 0;
}
/* T-1199 fold 1 (owner 1480: "another player can still use the owner's ... doors"): A DOOR UNDER Owner only. applying: this game's
   door applier (another game's state) or the house-purchase replay is opening it; otherPlayer: OtherPlayerOwner of the door's owner;
   mate: that player is this game's teammate; openerMine: the open is made by THIS game's player - one of its characters' AI open
   actions (the opener's faction is a player faction, the engine's own test in both actions) or its door panel's Open button. Town
   people, raiders, other games' copies and the engine's other door opens (a dialogue, a purchase, a melee bash) are never refused.
   A picked lock does not change it: the door stays refused to this player (the owner's lock is not the protection under Owner only). */
inline int DoorOpenRefuse(int policy, int applying, int otherPlayer, int mate, int openerMine)
{
    if (applying != 0 || openerMine == 0 || policy != kT1PolicyOwner) return 0;
    return (otherPlayer != 0 && mate == 0) ? 1 : 0;
}
/* the per-door repeat line: the 1st, 2nd, 4th, 8th ... refusal of one door is written (a refused open that repeats every tick shows
   as a climbing count without filling the log) */
inline int RepeatSays(long n) { return (n > 0 && (n & (n - 1)) == 0) ? 1 : 0; }
/* T-1199 fold 4 (manager, option A - parity with single player's locked door): A REFUSED DOOR ENDS THE OPENER'S ORDER. Neither engine
   open action reads openDoor's answer and the task has no timeout, so a refused open was tried again every frame (T2368: 25036
   refusals). The engine's own locked branch in the lock-test action sets the task's 'impossible' flag (FUN_140019538: +0x26D = 1) and
   the task system cancels the task on its next look (0x50CE90 -> 0x50C5F0). refused: DoorOpenRefuse said 1; fromPanel: the door panel's
   Open button (no task - nothing to end); actionMine: an AI open action of THIS game's player's character is running (its task is
   known). 1 = set that task's end flag, 0 = nothing. */
inline int DoorRefusalEndsTask(int refused, int fromPanel, int actionMine)
{
    return (refused != 0 && fromPanel == 0 && actionMine != 0) ? 1 : 0;
}
/* what the end-flag write found (doors.cpp DoorTaskEndPod); kT1DoorTaskPanel = no write asked (the panel's open) */
enum { kT1DoorTaskPanel = -1, kT1DoorTaskNoTask = 0, kT1DoorTaskEnded = 1, kT1DoorTaskAgain = 2, kT1DoorTaskDone = 3 };
/* the refusal line, once per order: a refusal that set the end flag is written (the order ends there, so this is the order's one
   line); a second refusal of the same order before the cancel (Again) or one whose task had already reported done (Done) is not; a
   refusal that ended no task (the panel's open, a task that could not be read) keeps the per-door 1st, 2nd, 4th, 8th ... rule. */
inline int DoorRefusalSays(int taskResult, long perDoorN)
{
    if (taskResult == kT1DoorTaskEnded) return 1;
    if (taskResult == kT1DoorTaskAgain || taskResult == kT1DoorTaskDone) return 0;
    return RepeatSays(perDoorN);
}
/* the repeat counter's minute: 1 = a refusal at `now` starts a new minute for the box (none yet, or the last began 60 s ago) */
inline int GateNewMinute(int haveWindow, unsigned int windowAt, unsigned int now)
{
    return (haveWindow == 0 || (unsigned int)(now - windowAt) >= 60000u) ? 1 : 0;
}
}
#endif
