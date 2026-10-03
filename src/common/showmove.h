#pragma once
/* T-164 non-shop road (2026-09-29; owner decisions 207 A, 208, 209 A, 212): a move between this game's side and a character / box /
   pack the OTHER game holds SHOWS AT ONCE - the shop rule. The item leaves where it was at the move and appears where it was put when
   the other game agrees; on a refusal or a lapse it goes back, with no text. Header-only, no engine types, shared by the plugin
   (items.cpp: ItShowStart / ItShowSettle) and the offline suite (coop-test).

   GIVE  (our item to the other game's side): our item is taken out of our pack into escrow (no inventory) at the move - the pack's
         loss publishes as usual. Yes: the escrow is destroyed. Refusal / lapse / our half failing: it goes back into our pack.
   TAKE  (the other game's item to our side): the other window's item is lifted into escrow, or - for units dragged off a stack - the
         stack is lowered and the units held. Yes: the item is built on our side (ItTakerAdd) and the escrow destroyed. Refusal /
         lapse / our half failing: it goes back into the other window (209 A: the old cell, else any free cell of that container;
         a full box - the copy goes and the holder is re-asked; a full character - the copy is KEPT, counted: no character
         inventory re-sync road exists). A box is re-asked on EVERY refusal / lapse.
   T-164 fold (review of d8396d09): a FAULTED add (-1) keeps the object where it is - never destroyed, never added again (ItRevert's
   __except rule). A hold the HOLDER changed (its op 1 / op 2 named the held cell during the hold) is not put back - the holder's
   messages are the truth. A give nothing takes back is dropped at the character's feet (the overflow standard). */

namespace coopshow {

const int kShowNone = 0, kShowGive = 1, kShowTakeItem = 2, kShowTakeUnits = 3;

/* Does a SENT request show at once at all? Kept as today (the put-back stands until the answer): the loot route's give (it publishes
   the book in our pack itself), a put-back that had to RE-CREATE the item (the engine merged or destroyed the object the player moved -
   the object the rule would hold is not the one that moved), and a priced move.
   t338-showwanted (T-338, owner 108, 2026-10-01): an AI take-one (par1 - eating, hauling, building, out of a box the other game
   writes) SHOWS AT ONCE too: the unit leaves the box at the take; a TRANSFER's unit arrives in the taker's pack on the yes, a CONSUME's
   stays used; a refusal, a lapse or a yes for another item puts it back on the stack. aiTakeOne stays in the signature (every caller
   names it) and no longer decides. When the AI took the LAST unit, the clone is filed back and held whole (TakeShowKind(1,0,1)), as a
   player's split is - the reviewed road (T-338 review LOW). */
inline int ShowWanted(int aiTakeOne, int keepToday, int recreated, int trade)
{
    (void)aiTakeOne;
    return (keepToday != 0 || recreated != 0 || trade != 0) ? 0 : 1;
}

/* A TAKE, after the put-back, by what now sits in the other window's cell. isSplit: units were dragged off the other game's stack
   (a merged drop onto our own stack included - owner 212); atIsStack: the cell holds that stack; atIsMoved: it holds the object
   the engine moved (a whole item, or the split's clone filed there because the stack was gone). Anything else: today's road. */
inline int TakeShowKind(int isSplit, int atIsStack, int atIsMoved)
{
    if (isSplit != 0 && atIsStack != 0) return kShowTakeUnits;
    if (atIsMoved != 0) return kShowTakeItem;
    return kShowNone;
}

/* What the answer does with what is held. ok: the other game said yes; ourHalfDone: our own half of the yes succeeded (a GIVE: 1;
   a TAKE: the item was built on our side). */
/* holderMoved: the holder's own op 1 / op 2 named the held cell during the hold (a TAKE only) - on a refusal / lapse nothing is put
   back (kStHolderTruth: a lifted copy is destroyed - the removal our empty cell could not take; held units stay as the holder's
   count left them). */
const int kStNothing = 0, kStDestroy = 1, kStBack = 2, kStHolderTruth = 3;
/* t338-f2-settle (T-338 attempt 2): absorbed 1 = the holder's own split of the held units arrived and was ABSORBED (not applied - see
   SplitAbsorbAction): the holder took them, so a refusal, a lapse or our half failing after it never raises the stack (the holder's
   truth, as holderMoved - a box is re-asked); a yes leaves them gone. Ignored for anything but held units. */
inline int SettleAction(int showKind, int ok, int ourHalfDone, int holderMoved, int absorbed = 0)
{
    if (showKind == kShowNone) return kStNothing;
    if (absorbed != 0 && showKind == kShowTakeUnits) return (ok != 0 && ourHalfDone != 0) ? kStNothing : kStHolderTruth;   /* t338-f2-settle */
    if (ok == 0 || ourHalfDone == 0) return (holderMoved != 0 && showKind != kShowGive) ? kStHolderTruth : kStBack;
    return (showKind == kShowTakeUnits) ? kStNothing : kStDestroy;   /* held units: the holder's own move lowers its stack for good */
}

/* H2: a holder's own move while a take is held. sameCell: it names the held uid / box key, section, pack path and cell;
   cellEmptyHere: our copy of that cell is empty (the lifted item's cell, nothing added since); sidSame: the move names the held
   record. op 1 there: mark. op 2 on a lowered stack: mark (its absolute count is the holder's truth). op 2 at a lifted item's empty
   cell naming its record: the count is written onto the held copy (the holder still has it). Anything else: nothing. */
const int kHmNone = 0, kHmMark = 1, kHmQtyOnCopy = 2;
inline int HolderMoveAction(int showKind, int op, int sameCell, int cellEmptyHere, int sidSame)
{
    if (sameCell == 0 || (showKind != kShowTakeItem && showKind != kShowTakeUnits)) return kHmNone;
    if (op == 1) return kHmMark;
    if (op != 2) return kHmNone;
    if (showKind == kShowTakeUnits) return kHmMark;
    if (cellEmptyHere == 0) return kHmNone;   /* it applies to what now sits there; the holder's earlier removal marked already */
    return (sidSame != 0) ? kHmQtyOnCopy : kHmMark;
}
/* H2 (b): a BOX's holder is re-asked (parity) on every refusal / lapse / our half failing of a take - not only when it is full. */
inline int ReaskBoxAfter(int settleAction, int isBox)
{
    return (isBox != 0 && (settleAction == kStBack || settleAction == kStHolderTruth)) ? 1 : 0;
}
/* H1: an engine add's answer - 1 placed, 0 refused (try the next step), -1 FAULTED: the object may be half-filed into that section,
   so it is kept where it is - never destroyed, never added again (ItRevert's __except rule) - and counted. */
const int kPutPlaced = 1, kPutNext = 0, kPutKeep = 2;
inline int PutOutcome(int r)
{
    if (r == 1) return kPutPlaced;
    return (r < 0) ? kPutKeep : kPutNext;
}
/* 209 A: where a refused / lapsed TAKE's lifted item goes in the other game's window. A full box: the copy is destroyed and the
   holder re-asked. A full character: the copy is KEPT alive in no inventory, counted (M3 - no character re-sync road exists). */
const int kTbOldCell = 1, kTbAnyCell = 2, kTbReaskBox = 3, kTbKeptNoRoom = 4;
inline int TakeBackAction(int oldCellFree, int anyCellFree, int isBox)
{
    if (oldCellFree != 0) return kTbOldCell;
    if (anyCellFree != 0) return kTbAnyCell;
    return (isBox != 0) ? kTbReaskBox : kTbKeptNoRoom;
}
/* The held units of a split: back on their stack (relative) when it is still the object in its cell; otherwise re-ask / counted. */
inline int UnitsBackAction(int raised, int isBox)
{
    if (raised != 0) return kTbOldCell;
    return (isBox != 0) ? kTbReaskBox : kTbKeptNoRoom;
}

/* A refused / lapsed GIVE's escrow back into OUR pack: the cell it left, else the first free cell of that section, else the engine's
   own add (kGbPack: into the pack's own inventory when it left a pack, else the character); if that too refuses, DROPPED AT THE
   CHARACTER'S FEET by the engine's own drop (kGbGround, the overflow standard); only if the drop fails too is it kept alive in no
   inventory and counted (kGbKept). */
const int kGbCell = 1, kGbFreeCell = 2, kGbPack = 3, kGbGround = 4, kGbKept = 5;
inline int GiveBackAction(int cellFree, int freeCellFound)
{
    if (cellFree != 0) return kGbCell;
    if (freeCellFound != 0) return kGbFreeCell;
    return kGbPack;
}
inline int GiveOverflowAction(int engineAddTook, int dropped)
{
    if (engineAddTook != 0) return kGbPack;
    return (dropped != 0) ? kGbGround : kGbKept;
}
/* M1: a late yes to a lapsed give. The row names the escrow's object and cell (an exact put): our copy is removed there. Otherwise
   (the engine's own add, the ground, kept, a fault) the owner is asked to revoke the copy it built. */
const int kLgRemoveExact = 1, kLgRevoke = 2;
inline int LateGiveRoad(int rowNamesIt) { return (rowNamesIt != 0) ? kLgRemoveExact : kLgRevoke; }

/* t338-f2-absorb (T-338 attempt 2, T791 diagnosis H-t338-1, 2026-10-02): THE HOLDER'S OWN SPLIT FOR A HELD TAKE IS ABSORBED.
   The holder grants a partial take through the engine's split, which it publishes as a RELATIVE "remove N from the cell" (op 3,
   items.cpp ItOwnerTake -> takeItemOut -> the split hook). Our copy of that cell already holds the N units off
   since the take showed at once, so applying that split too lowered the stack twice (one short). The holder's split of what we hold
   is therefore NOT applied: it is the holder's own take - the authoritative sign that it happened.
   sameCell: the split names the held uid / box key, section, pack path and cell; sidSame: our copy's cell still holds the held stack
   (and the split's base sid, when it carries one, is the held record); qty: the split's units; heldUnits: the units held;
   absorbedAlready: this hold absorbed its split already; holderMoved: the holder's absolute count (op 2) or removal (op 1) landed on
   that cell during the hold - our lowered count was overwritten by the holder's truth, so its split is applied as any.
   kAbApplyReask: a split that is not ours to absorb (other units, another stack) - applied (the holder's truth; a split is relative,
   so the hold stays right) and the hold settles by re-asking the box. */
const int kAbNone = 0, kAbAbsorb = 1, kAbApply = 2, kAbApplyReask = 3;
inline int SplitAbsorbAction(int sameCell, int sidSame, int qty, int heldUnits, int absorbedAlready, int holderMoved)
{
    if (sameCell == 0) return kAbNone;
    if (absorbedAlready != 0 || holderMoved != 0) return kAbApply;
    if (sidSame != 0 && qty > 0 && qty == heldUnits) return kAbAbsorb;
    return kAbApplyReask;
}
/* t338-f3-move (T-338 fold 3, 2026-10-02): WHICH HOLD AN ABSORBED SPLIT BELONGS TO. ItShowAbsorbSplit gives the holder's split
   to the FIRST matching hold on the cell. Two holds of the same units on one stack (T1, T2) - the holder grants T1, refuses T2, and
   T1's split may land on T2's row. So when a hold that absorbed a split ends WITHOUT a yes (refused, lapsed, our half failed) the
   split was another hold's if one is still open on the same cell (AbsorbMoveCandidate): the absorb MOVES there - a wait row first
   (its yes came first, so the split most likely was its; it settles absorbed at once), else an open pending row (marked absorbed;
   its own answer settles it) - and THIS hold settles as if it had never absorbed (its units go back by the normal road). With no
   other open hold: the holder's truth, nothing put back (kAmNoCandidate). A yes, a hold that absorbed nothing, or anything but held
   units: unchanged (kAmNone).
   absorbed: this hold absorbed a split; yes: the answer was a yes AND our half was done (SettleAction's absorbed yes); waitFound /
   pendFound: a candidate was found among the wait rows / the open pending rows. */
const int kAmNone = 0, kAmToWait = 1, kAmToPend = 2, kAmNoCandidate = 3;
inline int AbsorbMoveAction(int showKind, int absorbed, int yes, int waitFound, int pendFound)
{
    if (showKind != kShowTakeUnits || absorbed == 0 || yes != 0) return kAmNone;
    if (waitFound != 0) return kAmToWait;
    if (pendFound != 0) return kAmToPend;
    return kAmNoCandidate;
}
/* t338-f3-move: another open hold may take the moved absorb when it names the same cell (uid / box key, section, pack path, cell),
   the same held stack and the same units, has not absorbed a split itself and was not overwritten by the holder's op 1 / op 2. */
inline int AbsorbMoveCandidate(int sameCell, int sameStack, int sameUnits, int absorbed, int holderMoved)
{
    return (sameCell != 0 && sameStack != 0 && sameUnits != 0 && absorbed == 0 && holderMoved == 0) ? 1 : 0;
}
/* t338-f2-await: a yes for held units whose split has not come yet keeps a small wait row open for it. The yes usually comes FIRST:
   the holder sends its confirm at once (ItSendConfirm) and its split in its next drain (ItQueue -> ItemsTick). Not after an absorbed
   split, and not when the holder's absolute count / removal already landed (holderMoved: its split, when it comes, is applied). */
inline int AwaitSplitAfter(int showKind, int settleAction, int absorbed, int holderMoved)
{
    return (showKind == kShowTakeUnits && settleAction == kStNothing && absorbed == 0 && holderMoved == 0) ? 1 : 0;
}
/* The wait is bounded: 10 s - the request's own lapse bound (kPendTimeoutMs) and the holder's own wait for our answer. The split
   rides the same link right behind the yes and normally lands within a tick or two; one not here after 10 s is not guessed at: a box's
   holder is re-asked (parity); a character has no re-sync road - counted. Checked once per tick (ItPendExpire). */
const unsigned int kAwaitSplitMs = 10000;
inline int AwaitLapsed(unsigned int elapsedMs) { return (elapsedMs >= kAwaitSplitMs) ? 1 : 0; }
/* A wait sees the holder's op 1 / op 2 on that cell before its split: the holder's truth landed first (an op 2 rung before its take -
   its split must then be applied; an op 1 - the stack is gone). The wait ends; nothing absorbed. */
inline int AwaitEndsOnHolderMove(int op, int sameCell) { return (sameCell != 0 && (op == 1 || op == 2)) ? 1 : 0; }
/* t338-f2-repeat: an AI take-one CONSUME is sent only when the stack REALLY dropped: the live count is below the count before the
   take (the take hook rings the count after it, countAfterTake = before - 1). A unit returned to the same stack in the same pass
   leaves the count where it was - nothing is sent (counted). An unreadable count sends nothing (our copy stays as it is; the safety
   net settles it toward the writer, as after a failed send). */
inline int ConsumeCountDropped(int liveQty, int countAfterTake) { return (liveQty >= 0 && liveQty <= countAfterTake) ? 1 : 0; }
/* t338-f2-line: the outcome a held take's one settle line names. */
const int kSoYes = 1, kSoAbsorbed = 2, kSoRefused = 3, kSoLapsed = 4, kSoHolderMoved = 5, kSoOurHalfFailed = 6;
inline int SettleOutcome(int ok, int ourHalfDone, int lapsed, int holderMoved, int absorbed)
{
    if (lapsed != 0) return kSoLapsed;
    if (ok == 0) return kSoRefused;
    if (ourHalfDone == 0) return kSoOurHalfFailed;
    if (absorbed != 0) return kSoAbsorbed;
    if (holderMoved != 0) return kSoHolderMoved;
    return kSoYes;
}
inline const char* SettleOutcomeName(int o)
{
    switch (o)
    {
    case kSoYes: return "yes";
    case kSoAbsorbed: return "absorbed";
    case kSoRefused: return "refused";
    case kSoLapsed: return "lapsed";
    case kSoHolderMoved: return "holderMoved";
    case kSoOurHalfFailed: return "ourHalfFailed";
    default: return "unknown";
    }
}

}   /* namespace coopshow */
