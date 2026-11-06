#pragma once
/* AN ITEM THIS GAME'S OWN PLAYER LIFTS OUT OF A CONTAINER THIS GAME WRITES (one of its own characters, or a box whose area it holds) -
   P105, .modding/investigations/p105-boxmove-cursor-design-2026-10-02.md sections 4.6 and 15.7.
   Its removal is published the moment it is lifted, not when it is put down, so the other players see it leave at once and cannot
   take it while it is carried. Every road that later puts it back into a container this game writes then publishes that add: the
   drop into one of our own containers and the engine's own put-back already do (they ring an ADD); a drop into a container another
   game writes runs ItCrossTransfer, whose put-back is silent, so whatever it leaves standing in our container is published back.
   Units lifted off a stack this game writes are the stack's new absolute count, published from a live read; dropped into a
   container another game writes, they take the give road a whole item takes (SplitDropRoad).
   Header-only, no engine types; shared by items.cpp and the offline suite (coop-test). C++03. */

namespace ownpick {

/* Is a removal that stays on this game's cursor published at the pickup? mine: this game writes the container it left; askedHold: a
   HOLD went to the game that writes it (that game publishes the removal instead). */
inline int PublishAtPickup(int mine, int askedHold)
{
    return (mine != 0 && askedHold == 0) ? 1 : 0;
}

/* What the drop (or the cursor letting go of the item with no drop) does with the removal it carried. pubAtPick: it went out at the
   pickup; mineNow: this game writes the container it left, read again now. */
const int kDrNone = 0, kDrPublish = 1, kDrSkip = 2;
inline int DeferredRemove(int pubAtPick, int mineNow)
{
    if (pubAtPick != 0) return kDrSkip;
    return (mineNow != 0) ? kDrPublish : kDrNone;
}

/* After a drop into a container another game writes (ItCrossTransfer). give: the item went from our side to theirs; pubAtPick: its
   removal went out at the pickup; stillHere: the object the put-back filed in our container is still the object in that square.
   1 = publish its add - the other games saw it leave, and it is back. */
inline int BackAddAfterGive(int give, int pubAtPick, int stillHere)
{
    return (give != 0 && pubAtPick != 0 && stillHere != 0) ? 1 : 0;
}

/* Units lifted off a stack this game writes. stackAtSquare: the stack is still the object in its own square (a stack the drag merged
   away or moved again is published by its own changes); count: its count read now. 1 = publish that count. */
inline int SplitCountPublish(int mine, int stackAtSquare, int count)
{
    return (mine != 0 && stackAtSquare != 0 && count > 0) ? 1 : 0;
}

/* Units lifted off a stack this game writes, at the drop (the ADD of the engine's clone that ends the drag). srcMine: this game still
   writes the stack's container, read again at the drop; dstMine: this game writes the container the units landed in; dstNamed: that
   container can be named (a replicated uid, a box key). kSdGive: into a container another game writes - the give road a whole item
   takes (the units go back onto the stack, the writer is asked, they show gone at once, a refusal returns them to the stack);
   kSdOwn: into one of our own containers - an ordinary move of ours, published as one; kSdKept: the stack is no longer ours, or the
   container cannot be named - nobody can be asked, the drop stands as the engine left it. */
const int kSdOwn = 0, kSdGive = 1, kSdKept = 2;
inline int SplitDropRoad(int srcMine, int dstMine, int dstNamed)
{
    if (dstMine != 0) return kSdOwn;
    return (srcMine != 0 && dstNamed != 0) ? kSdGive : kSdKept;
}

/* A whole item of ours dropped onto a matching stack in a container another game writes: the engine folds that stack into our item
   and destroys it (design 15.3), so our object in their square holds ours + theirs. merged: the drop showed that shape (the stack's
   removal at the same square and the merge onto our item, HdMergedAt); lifted: the units we carried; now: the object's count, read
   at the put-back. 1 = the put-back takes back only `lifted` and rebuilds their stack with *restore units in their square; 0 = the
   whole object goes back (no merge, or the count does not show one). */
inline int MergedGiveSplit(int merged, int lifted, int now, int* restore)
{
    *restore = 0;
    if (merged == 0 || lifted <= 0 || now <= lifted) return 0;
    *restore = now - lifted;
    return 1;
}

/* T-1184: this game's own lifted item, still in the air. heldUsed: the held row stands (items.cpp g_heldPend); mine: this game wrote
   the container it left; askedHold: a HOLD went to the game that writes it (that game's item - the hold roads put it back, HdEdge);
   cursorNamesIt: the cursor, read NOW, holds that same object - never the row alone, which outlives a destroyed item until the next
   release test. 1 = ours, and on the cursor. */
inline int OwnLiftInAir(int heldUsed, int mine, int askedHold, int cursorNamesIt)
{
    return (heldUsed != 0 && mine != 0 && askedHold == 0 && cursorNamesIt != 0) ? 1 : 0;
}

/* T-1184 (A): the squad record of the character it was lifted off waits while it is in the air. fromThis: the record's member is that
   character (or wears the pack it was lifted out of). Its removal went out at the pickup, so a record written now holds the character
   without it, and a record written after a save started beats that save at the next load: the item would be in neither. The wait
   ends the moment the cursor stops naming the object (put back, dropped, placed, destroyed, the world gone); the squad stays dirty
   and its next write goes out as usual. */
inline int LiftHoldsRecord(int inAir, int fromThis)
{
    return (inAir != 0 && fromThis != 0) ? 1 : 0;
}

/* LiftHoldsRecord's fromThis - is the squad member being asked the character the item came from? isChar: lifted off a character
   (sameChar: this member is that character); isBag: lifted out of a pack (walked: the member's packs could be walked - off the main
   thread they cannot, and every member then waits, as an undrained pack move makes them; wears: this member carries that pack, at
   any depth). A box's item is no squad's. */
inline int LiftFromThis(int isChar, int sameChar, int isBag, int walked, int wears)
{
    if (isChar != 0) return (sameChar != 0) ? 1 : 0;
    if (isBag != 0) return (walked == 0 || wears != 0) ? 1 : 0;
    return 0;
}

/* The longest hold so far, in whole seconds: longest = the answer before; since / now = GetTickCount of the hold's first held write and
   of this one (unsigned: one wrap of the tick counter is read right). */
inline unsigned int HoldLongestS(unsigned int longest, unsigned int since, unsigned int now)
{
    const unsigned int s = (unsigned int)(now - since) / 1000u;
    return (s > longest) ? s : longest;
}

/* T-1184 (owner 1467 a): where an edge's own lifted item goes when the engine's return did not take. ready: the cursor can be
   emptied (its clear's row bound, the cursor there) - nothing is placed otherwise; put: the put into its container (or the main
   section) 1 placed, 0 no room, -1 faulted; dropped: the drop at the character's feet 1 on the ground, 0 not, -1 faulted. Only kEpInv
   and kEpGround empty the cursor; kEpLeft leaves the item on it (a fault included: it may be placed already, so never twice). */
const int kEpLeft = 0, kEpInv = 1, kEpGround = 2;
inline int EdgePlaceRoad(int ready, int put, int dropped)
{
    if (ready == 0 || put < 0) return kEpLeft;
    if (put == 1) return kEpInv;
    return (dropped == 1) ? kEpGround : kEpLeft;
}

/* Said once per hold: holds = LiftHoldsRecord's answer now; said = this held row already said it. */
inline int LiftHoldSay(int holds, int said)
{
    return (holds != 0 && said == 0) ? 1 : 0;
}

}   /* namespace ownpick */
