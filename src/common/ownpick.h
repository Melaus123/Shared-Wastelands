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

}   /* namespace ownpick */
