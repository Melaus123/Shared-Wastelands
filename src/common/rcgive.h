#pragma once
#include <cmath>   /* T-1191: BesideSpot */
#include <string>  /* T-1191: BFacText */
/* A MOVE MADE WITHOUT THE CURSOR ACROSS THE AUTHORITY LINE (T-1079): the player's right-click on an item with two inventory windows
   open - InventoryGUI::RClickAutoTrade 0x713150; shift-click repeats it while it answers OK (0x713D60). Each call moves ONE unit
   (decomp_713150):
     - a stack of two or more: the engine copies one unit (copyItem 0x57B5A0) and lowers the source stack with a raw write - NOTHING
       rings for the source side;
     - a single item (the last unit of a stack): it leaves its section through InventorySection::removeItem - an op 1 there.
   The unit lands as a new object (op 0) or is added onto a matching stack (addQuantity, op 2; the added object is destroyed). So the
   two halves never carry one pointer, and the pointer pairing cannot see the move.
   A GIVE: one of this game's own characters -> a box or a character another game writes. A TAKE: the reverse.
   items.cpp records each call in the call's own detour (the source object, its cell, the units it lost, both containers, the ring
   window the call rang in); the drain counts what the destination gained inside that window, per object, of the source item's
   record only, and decides here: equal units -> the drag's request road (the other game is asked); anything else -> back (the units
   leave the destination and return to the source, on this game, at once). Header-only, no engine types; shared by items.cpp and
   the offline suite (coop-test). C++03. */

namespace rcgive {

/* The units one call took off the source object. qtyBefore: its count before the call; stillThere: it is still the object in its
   cell after the call; qtyAfter: its count after (when still there; -1 unread). A gone object gave all it had (the single-item arm). */
inline int UnitsMoved(int qtyBefore, int stillThere, int qtyAfter)
{
    if (qtyBefore <= 0) return 0;
    if (stillThere == 0) return qtyBefore;
    if (qtyAfter < 0 || qtyAfter >= qtyBefore) return 0;
    return qtyBefore - qtyAfter;
}

/* Is the click this road's at all? ourMine: the character on our side is this game's; theirMine / theirNamed: the other container is
   written by this game / can be named (a box with a key, a character with a uid); shop: the other container is a shop's stock - shop
   trades keep their own road (the trade intercept), exactly as before. */
inline int Applies(int ourMine, int theirMine, int theirNamed, int shop)
{
    return (ourMine != 0 && theirMine == 0 && theirNamed != 0 && shop == 0) ? 1 : 0;
}

/* Whether a destination change in the window belongs to the click: a change of ANOTHER item (record known and different) is not the
   click's and is left to the road it always took. kindKnown: the change's record was read; kindSame: it is the source item's. */
inline int ChangeIsTheClicks(int kindKnown, int kindSame)
{
    return (kindKnown != 0 && kindSame == 0) ? 0 : 1;
}

/* The units one destination change added. op 0 (an object put in): its count; op 2 (a count change): the rise, only with the count
   before it read (qtyBefore > 0). Anything else - a removal, a split, a fall, an unread count - is -1: not a gain this pairing counts. */
inline int EntryGain(int op, int quantity, int qtyBefore)
{
    if (op == 0) return (quantity > 0) ? quantity : -1;
    if (op == 2) return (qtyBefore > 0 && quantity > qtyBefore) ? quantity - qtyBefore : -1;
    return -1;
}

/* One object in the destination that gained units in the window, and how many. */
struct Piece { unsigned long long obj; int gain; };

/* Folds one destination change into the pieces. 1 = counted; 0 = not counted (not a gain, inside a pack, or more objects than `cap`). */
inline int AddEntry(Piece* pieces, int* count, int cap, unsigned long long obj, int op, int quantity, int qtyBefore, int inPack)
{
    const int g = (inPack != 0) ? -1 : EntryGain(op, quantity, qtyBefore);
    if (g <= 0 || obj == 0) return 0;
    for (int i = 0; i < *count; ++i)
        if (pieces[i].obj == obj) { pieces[i].gain += g; return 1; }
    if (*count >= cap) return 0;
    pieces[*count].obj = obj;
    pieces[*count].gain = g;
    ++*count;
    return 1;
}

/* kNone: no change of the click's item in the destination - not this road, everything goes on as before;
   kPair: every unit the source lost is in the destination, on objects of the source item's record - each object's share goes to the
   other game as a drag would; kBack: anything else - the units go back to the source on this game. units: what the source lost;
   entries: the destination's changes of the click; uncounted: those AddEntry did not count, plus pieces whose live object could not
   be read; kindSame[i]: piece i's object carries the source item's record. */
const int kNone = 0, kPair = 1, kBack = 2;
inline int Decide(int units, int entries, int uncounted, const Piece* pieces, int count, const int* kindSame)
{
    if (entries <= 0) return kNone;
    if (units <= 0 || uncounted != 0 || count <= 0) return kBack;
    int sum = 0;
    for (int i = 0; i < count; ++i)
    {
        if (pieces[i].gain <= 0 || kindSame[i] == 0) return kBack;
        sum += pieces[i].gain;
    }
    return (sum == units) ? kPair : kBack;
}

/* How one paired piece is undone before the other game is asked (ItCrossTransfer). give: 1 a give, 0 a take; gone: the source object
   left its container on the last call; now: the piece's count; gain: what it got from the click.
   kPrSplit: the source stack is still there - the units go back onto it (ItRevertSplit: an object holding exactly the units is
             removed, one holding more is lowered);
   kPrWhole: the piece holds exactly the units - the object goes back whole into the source cell;
   kPrMergedGive: a give onto their stack - only our units come back and their stack is rebuilt (ownpick::MergedGiveSplit);
   kPrMergedTake: a take onto our own stack that held units before, with their object gone - only the units it gained come off our
                  stack and are made again in their cell (MergedTakeSplit), and the other game is asked for exactly them. */
const int kPrSplit = 0, kPrWhole = 1, kPrMergedGive = 2, kPrMergedTake = 3;
inline int PieceRoad(int give, int gone, int now, int gain)
{
    if (gone == 0) return kPrSplit;
    if (now <= gain) return kPrWhole;
    return (give != 0) ? kPrMergedGive : kPrMergedTake;
}

/* A take merged onto our own stack, undone before the ask: our stack of `now` keeps *keep, and *back units are made again in their cell.
   1 = the stack holds more than the units it gained (both sides get a positive count, *keep + *back == now); 0 = not this road. */
inline int MergedTakeSplit(int now, int gain, int* keep, int* back)
{
    *keep = 0; *back = 0;
    if (gain <= 0 || now <= gain) return 0;
    *keep = now - gain;
    *back = gain;
    return 1;
}

/* A DRAG's whole stack taken out of their container and dropped on a matching stack we already held: the engine's drop
   (decomp_714990 :563-611) folds our stack into the carried object, destroys our object and files the carried one in our square, so
   the carried object then holds our units too. give: 1 a give; split: 1 the pickup split units off a stack (op 3); sameObj: 1 the
   object in the drop square is the carried object; now: its live count; carried: the units carried at the pickup.
   1 = only the carried units come off it and are made again in their square (*keep stays ours, *back is asked for);
   0 = not this road - the object holds only what was carried, or this is not a whole-stack drag take. */
inline int DragMergedTake(int give, int split, int sameObj, int now, int carried, int* keep, int* back)
{
    *keep = 0; *back = 0;
    if (give != 0 || split != 0 || sameObj == 0) return 0;
    return MergedTakeSplit(now, carried, keep, back);
}

/* Where a merged drag take's carried units go back in their container. ownFits: their own square takes them; otherFree: another free
   square of that container does. kDtOwn: their own square - the request is sent (it names that square, as the writer holds it);
   kDtOther: another square - the units leave our stack but nothing is asked (the request's square would not be where they are), the
   writer is re-asked for the container; kDtNone: nowhere - the move stands, the writer is re-asked. */
const int kDtOwn = 0, kDtOther = 1, kDtNone = 2;
inline int DragTakeBackPlace(int ownFits, int otherFree)
{
    if (ownFits != 0) return kDtOwn;
    return (otherFree != 0) ? kDtOther : kDtNone;
}
inline int DragTakeAsks(int place)
{
    return (place == kDtOwn) ? 1 : 0;
}

/* Back to the source: 1 = raise the source stack by the units (it is still the object in its cell); 0 = make the units again there
   from the record read before the call. */
inline int BackRaises(int gone, int stillThere)
{
    return (gone == 0 && stillThere != 0) ? 1 : 0;
}

/* What leaves a destination object on the way back: 1 = it is taken out whole (it holds no more than the click's units); 0 = its count
   is lowered by them. */
inline int BackTakesWhole(int now, int gain)
{
    return (now <= gain) ? 1 : 0;
}

/* A click with no record row left is refused before the engine moves anything (1 = refuse): an unrecorded click could not be paired. */
inline int RefuseUnrecorded(int armed, int row)
{
    return (armed != 0 && row < 0) ? 1 : 0;
}

/* A row takes at most `cap` calls. One call moves one unit onto at most one destination object, so a row never names more objects
   than it has calls, and the drain can always list every one. 1 = the row is full: the next call of that shift-click is refused (the
   player clicks again for the rest, after the drain has settled this row). */
inline int RowFull(int calls, int cap)
{
    return (calls >= cap) ? 1 : 0;
}

/* A click off the main thread cannot be recorded (the record and the drain are main-thread only): 1 = refuse it before anything moves,
   when it would have been this road's (one side this game's own character, the other not). */
inline int RefuseOffMain(int thisRoad, int onMain)
{
    return (thisRoad != 0 && onMain == 0) ? 1 : 0;
}

/* THE BACK ROAD'S ONE RULE: a destination object gives back only what is taken out of it, and the source gets exactly that.
   takenOut: the object left the destination copy (whole) or its count was lowered; now: its count; want: the units asked of it
   (its share of the click, capped by what the source still lacks). Taken out whole when it holds no more than `want` - then its
   whole count comes back - else lowered by `want`. 0 when nothing could be taken out: those units stay where they are. */
inline int BackUnitsTaken(int takenOut, int now, int want)
{
    if (takenOut == 0 || now <= 0 || want <= 0) return 0;
    return (BackTakesWhole(now, want) != 0) ? now : want;
}

/* What one piece is asked for on the whole-row back road: its gain, never more than the source still lacks. */
inline int BackWant(int gain, int stillLacking)
{
    if (gain <= 0 || stillLacking <= 0) return 0;
    return (gain < stillLacking) ? gain : stillLacking;
}

/* A dropped record (not this road at the drain - the other side became this game's, or cannot be named) whose source stack was lowered
   by the engine's direct count write: 1 = its count is published from a live read, when this game writes the source and the stack is
   still the object in its cell (a gone object's removal rang and goes out as usual). */
inline int FlushSourceOnDrop(int sourceWrittenHere, int stillThere)
{
    return (sourceWrittenHere != 0 && stillThere != 0) ? 1 : 0;
}

/* A whole-stack give (our object left our character) whose units, or some of them, take the back road: the original's removal was
   consumed with the click, so it is published once now - the other games must not keep the original as well as what comes back. */
inline int PublishOriginalRemoval(int give, int gone, int backTaken)
{
    return (give != 0 && gone != 0 && backTaken != 0) ? 1 : 0;
}

/* ---- THE NET: a move no other road paired, paired by item record and unit count ----
   One item record's changes in one drain pass that no pointer, click, drag or take-one pairing claimed. side: kSideOurs (a character
   this game runs, its NPCs included), kSideTheirs (a box another game writes, or another game's character), kSideOursOther (a box this
   game writes); container: the same number for every change in one container; rel: the object the engine's own call tied to the
   change - on an op 3 the object the split handed back (takeItemOut 0x749E10's return), on an op 2 rise the object merged into the
   stack (Item::addQuantity's `added`), else 0. */
const int kSideOurs = 0, kSideTheirs = 1, kSideOursOther = 2;
struct NetChange { int side; int container; unsigned long long obj, rel; int op; int quantity; int qtyBefore; int inPack; };

/* The side of one change: a box by its area verdict (held by another game / this game's), a character by its uid (this game's or
   another's); -1 not judged (no verdict, no uid, a pack). */
inline int NetSideOf(int isBox, int isChar, int boxHeld, int boxMine, unsigned int uid, int uidMine)
{
    if (isBox != 0) return (boxHeld != 0) ? kSideTheirs : ((boxMine != 0) ? kSideOursOther : -1);
    if (isChar != 0 && uid != 0) return (uidMine != 0) ? kSideOurs : kSideTheirs;
    return -1;
}

/* 1 = another road has this change, or it cannot be named: handled already, one half of a pointer pair, a one-tick move inside one
   container, a take-one's route, a drag's held half, a shop's stand-in, a change inside a pack, no name. The net never touches it. */
inline int NetSkip(int handled, int paired, int oneTick, int takeOneRouted, int dragHalf, int shopFacade, int inPack, int unnamed)
{
    return (handled != 0 || paired != 0 || oneTick != 0 || takeOneRouted != 0 || dragHalf != 0 || shopFacade != 0 || inPack != 0
            || unnamed != 0) ? 1 : 0;
}

/* One change as the drain reads it: live 1 = judged, not skipped, its record read; kind / container: the index of the first live change
   of the same record / the same container (the drain numbers them). */
struct NetIn { int live; int kind; int side; int container; unsigned long long obj, rel; int op; int quantity; int qtyBefore; int inPack; };

/* The record of change `start`: every live change of it not yet grouped, from `start` on, in drain order, marked grouped. grp: their
   indexes; out: them as NetChange. At most cap; *over 1 when there were more (all are marked grouped either way). */
inline int NetGroup(const NetIn* in, int n, int start, int* done, int* grp, NetChange* out, int cap, int* over)
{
    int gn = 0;
    *over = 0;
    for (int k = start; k < n; ++k)
    {
        if (in[k].live == 0 || done[k] != 0 || in[k].kind != in[start].kind) continue;
        done[k] = 1;
        if (gn >= cap) { *over = 1; continue; }
        grp[gn] = k;
        NetChange& c = out[gn];
        c.side = in[k].side; c.container = in[k].container; c.obj = in[k].obj; c.rel = in[k].rel;
        c.op = in[k].op; c.quantity = in[k].quantity; c.qtyBefore = in[k].qtyBefore; c.inPack = in[k].inPack;
        ++gn;
    }
    return gn;
}

/* The units one change took out of its container: op 1 (an object out) and op 3 (units split off a stack) their count; op 2 the fall,
   only with the count before it read. Anything else is -1: not a loss. */
inline int NetLoss(int op, int quantity, int qtyBefore)
{
    if (op == 1 || op == 3) return (quantity > 0) ? quantity : -1;
    if (op == 2) return (qtyBefore > 0 && quantity >= 0 && quantity < qtyBefore) ? qtyBefore - quantity : -1;
    return -1;
}

/* give: 1 = our side lost the units; src: the index of the source object's first change; srcObj: the one object the units left;
   units: what it lost; gone: it left its container itself (an op 1). */
struct NetPlan { int give; int src; unsigned long long srcObj; int units; int gone; };

/* kNetNone: no change on one of the two sides - nothing for the net, today's road stands (uncounted);
   kNetPair: one container of ours (a character) and one of theirs; one side only LOST units, all off ONE object; the other only
             GAINED, on at most pieceCap objects, each gain TIED TO THAT SOURCE BY THE ENGINE'S OWN OBJECT IDENTITY (an op 0 of the
             source object or of an object a source split handed back; an op 2 rise that merged one of those in); equal counts;
   kNetAmbiguous: more than one container on a side could be the partner;
   kNetNotCharacter: this game's side is a box (an NPC carrying between two boxes);
   kNetTooMany: more destination objects than a job lists;
   kNetUnlinked: a gain the engine's calls do not tie to the source - an unrelated change (eating, a drop to the ground) beside an
                 unrelated gain in the same pass;
   kNetUnbalanced: a change that is neither a loss nor a gain, inside a pack, losses on two objects or on both sides, gains on both
                   sides, or counts that differ. Only kNetPair is paired; everything else keeps today's road. */
const int kNetNone = 0, kNetPair = 1, kNetAmbiguous = 2, kNetUnbalanced = 3, kNetNotCharacter = 4, kNetTooMany = 5, kNetUnlinked = 6;
inline int NetDecide(const NetChange* c, int n, int pieceCap, NetPlan* out)
{
    out->give = 0; out->src = -1; out->srcObj = 0; out->units = 0; out->gone = 0;
    int firstC[3] = { -1, -1, -1 }, many[3] = { 0, 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const int s = c[i].side;
        if (s < 0 || s > 2) return kNetUnbalanced;
        if (firstC[s] < 0) firstC[s] = c[i].container;
        else if (firstC[s] != c[i].container) many[s] = 1;
    }
    const int ours = (firstC[kSideOurs] >= 0 ? 1 : 0) + many[kSideOurs] + (firstC[kSideOursOther] >= 0 ? 1 : 0) + many[kSideOursOther];
    const int theirs = (firstC[kSideTheirs] >= 0 ? 1 : 0) + many[kSideTheirs];
    if (ours == 0 || theirs == 0) return kNetNone;
    if (ours > 1 || theirs > 1) return kNetAmbiguous;
    if (firstC[kSideOursOther] >= 0) return kNetNotCharacter;
    int lossSide = -1, gainSide = -1, gains = 0, objs = 0;
    for (int i = 0; i < n; ++i)
    {
        if (c[i].inPack != 0 || c[i].obj == 0) return kNetUnbalanced;
        const int l = NetLoss(c[i].op, c[i].quantity, c[i].qtyBefore);
        const int g = EntryGain(c[i].op, c[i].quantity, c[i].qtyBefore);
        if (l > 0)
        {
            if ((lossSide >= 0 && lossSide != c[i].side) || (out->srcObj != 0 && out->srcObj != c[i].obj)) return kNetUnbalanced;
            lossSide = c[i].side;
            if (out->src < 0) out->src = i;
            out->srcObj = c[i].obj;
            out->units += l;
            if (c[i].op == 1) out->gone = 1;
        }
        else if (g > 0)
        {
            if (gainSide >= 0 && gainSide != c[i].side) return kNetUnbalanced;
            gainSide = c[i].side;
            gains += g;
            int seen = 0;
            for (int i2 = 0; i2 < i; ++i2) if (c[i2].obj == c[i].obj) seen = 1;
            if (seen == 0) ++objs;
        }
        else return kNetUnbalanced;
    }
    if (lossSide < 0 || gainSide < 0 || lossSide == gainSide) return kNetUnbalanced;
    if (objs > pieceCap) return kNetTooMany;
    for (int i = 0; i < n; ++i)
    {
        if (c[i].side != gainSide) continue;
        const unsigned long long tie = (c[i].op == 0) ? c[i].obj : c[i].rel;
        int linked = (tie != 0 && tie == out->srcObj) ? 1 : 0;
        for (int i2 = 0; i2 < n && linked == 0; ++i2)
            if (c[i2].side == lossSide && c[i2].op == 3 && c[i2].rel != 0 && c[i2].rel == tie) linked = 1;
        if (linked == 0) return kNetUnlinked;
    }
    if (out->units != gains) return kNetUnbalanced;
    out->give = (lossSide == kSideOurs) ? 1 : 0;
    return kNetPair;
}

/* T-932: SEVERAL OF OUR OBJECTS GIVEN IN ONE PASS. A hauler (the hauling job) empties its inventory into their box: each of our objects
   of the record leaves (an op 1, or units split off it, an op 3) and lands there as an object (op 0) or merged onto a stack (an op 2
   rise tied to it). NetDecide refuses losses on two objects (kNetUnbalanced); this splits such a group - called only then, so it has
   one container on each side - into one part per object we lost, each gain going to the one object the engine's own call tied it to
   (the object itself for an op 0, the object merged in for an op 2 rise, or the object a split of it handed back). part[i]: change i's
   part (0..). Returns the parts (2 or more), or 0 when the group is not that shape: a change on another side, a change that is not a
   loss of ours or a gain of theirs, a change inside a pack, a gain tied to none or to two of our objects, an object of ours with no
   gain, or more than cap parts. Each part is then judged by NetDecide on its own, and becomes its own give. */
/* T-932 review fold 2: THE SPLIT'S BOUND. kNetSplitMax: our objects one split gives. Every object a hauler's dump moves left one of its
   character's own grids (a pack's changes are not the net's), and those hold 111 squares (main 8x10, belt 2x2, hip 7x1, the backpack
   slot 5x4: the sections' dimensions read in a run, findings), one object a square at least; 128 covers them. kNetGroupMax: the
   changes one group holds - four an object (its loss, a split's hand-back landing, its rise, the drop's own merge). */
const int kNetSplitMax = 128;
const int kNetGroupMax = 4 * kNetSplitMax;
inline int NetSplitGive(const NetChange* c, int n, int cap, int* part)
{
    unsigned long long src[kNetSplitMax];
    int ns = 0;
    if (cap > kNetSplitMax) cap = kNetSplitMax;
    for (int i = 0; i < n; ++i)
    {
        part[i] = -1;
        if (c[i].inPack != 0 || c[i].obj == 0) return 0;
        if (c[i].side == kSideOurs)
        {
            if (NetLoss(c[i].op, c[i].quantity, c[i].qtyBefore) <= 0) return 0;
            int j = 0;
            while (j < ns && src[j] != c[i].obj) ++j;
            if (j == ns)
            {
                if (ns >= cap) return 0;
                src[ns++] = c[i].obj;
            }
            part[i] = j;
        }
        else if (c[i].side != kSideTheirs || EntryGain(c[i].op, c[i].quantity, c[i].qtyBefore) <= 0) return 0;
    }
    if (ns < 2) return 0;
    for (int i = 0; i < n; ++i)
    {
        if (c[i].side != kSideTheirs) continue;
        const unsigned long long tie = (c[i].op == 0) ? c[i].obj : c[i].rel;
        int owner = -1;
        for (int k = 0; k < n && tie != 0; ++k)
        {
            if (c[k].side != kSideOurs) continue;
            if (c[k].obj != tie && !(c[k].op == 3 && c[k].rel != 0 && c[k].rel == tie)) continue;
            if (owner >= 0 && owner != part[k]) return 0;
            owner = part[k];
        }
        if (owner < 0) return 0;
        part[i] = owner;
    }
    for (int q = 0; q < ns; ++q)
    {
        int gained = 0;
        for (int i = 0; i < n && gained == 0; ++i) if (c[i].side == kSideTheirs && part[i] == q) gained = 1;
        if (gained == 0) return 0;
    }
    return ns;
}

/* T-932: A POINTER-PAIRED GIVE THAT TOOK IN OTHER OBJECTS OF OURS. A hauler's dump into an empty square of their box files our first
   object there (its op 1 and op 0 pair by pointer) and merges the rest of our objects onto it (op 2 rises on it, each tied to one of
   them). Left paired, the pair's put-back would carry those units with it and the drop's merge (HdMergedAt) would swallow their rises:
   asked for one object's units, the rest lost. 1 = the pair is let go, so the net takes the whole pass and each of our objects is asked
   for on its own. give: our character's object into a container another game writes, removed then added; othersMerged: unclaimed op 2
   rises on the landed object tied to another object that left that same character in this pass. The drop's own merge (the stack
   already in their square folded into ours) never counts: that object was not ours. */
inline int GiveLetGoForOthers(int give, int removeThenAdd, int othersMerged)
{
    return (give != 0 && removeThenAdd != 0 && othersMerged > 0) ? 1 : 0;
}

/* T-932 review fold: ONE RISE THE LET-GO COUNTS (GiveLetGoForOthers' othersMerged). rise: an op 2 on the pair's landed object
   (riseOnLanded), tied to riseRel - not the pair's own object (giveObj): that is the drop's own merge, never ours. other: a change
   that left our character in the same pass - an op 1 of that very object, or an op 3 whose hand-back is it. 1 = counted. */
inline int LetGoOtherRise(int riseOp, int riseOnLanded, unsigned long long riseRel, unsigned long long giveObj, int otherOp,
                          unsigned long long otherObj, unsigned long long otherRel)
{
    if (riseOp != 2 || riseOnLanded == 0 || riseRel == 0 || riseRel == giveObj) return 0;
    return ((otherOp == 1 && otherObj == riseRel) || (otherOp == 3 && otherRel != 0 && otherRel == riseRel)) ? 1 : 0;
}

/* T-932 review fold 1: A LET-GO PAIR THE NET DID NOT TAKE (its split refused - a change that is not a loss of ours beside it, a gain
   tied to none - or no job left for its first part): neither its remove nor its add claimed by a net row. 1 = it is paired again, so its
   first object keeps the asked road it had before the let-go, not the lone change's that loses it. */
inline int LetGoRestore(int letGo, int removeTaken, int addTaken)
{
    return (letGo != 0 && removeTaken == 0 && addTaken == 0) ? 1 : 0;
}

/* T-932 review fold 2: how many of a split's parts take a net job now - one job a part, in drain order, while jobs are left (jobsUsed
   of jobCap taken). A part left over keeps today's road and is counted full; the parts taken are given, not all of them refused. */
inline int NetSplitBatch(int parts, int jobsUsed, int jobCap)
{
    const int left = jobCap - jobsUsed;
    if (parts <= 0 || left <= 0) return 0;
    return (parts < left) ? parts : left;
}

/* T-932 re-check LOW (a): THE ORDER A SPLIT'S PARTS TAKE JOBS. lead: the part holding the let-go pair (-1: none) - its landed object
   is the stack the other parts merged onto. The lead goes first, then the others in drain order. The part at position pos (-1: none). */
inline int NetSplitOrder(int parts, int lead, int pos)
{
    if (pos < 0 || pos >= parts) return -1;
    if (lead < 0 || lead >= parts) return pos;
    if (pos == 0) return lead;
    return (pos <= lead) ? pos - 1 : pos;
}
/* 1 = the whole split is dropped: the lead part (pos 0, the let-go pair's) did not become a job. The pair is then paired again
   (LetGoRestore) and its put-back carries the whole landed stack, so no later part may take a job against that stack. */
inline int NetSplitDrop(int pos, int lead, int taken)
{
    return (pos == 0 && lead >= 0 && taken == 0) ? 1 : 0;
}
/* T-932 re-check LOW (b): what a put-back's result (ItRevert: 1 put back, 2 kept, 0 failed) means to the move. Kept: a merged give whose
   stack could not be rebuilt went back into their square whole, so our units are only in our copy of their box. kUndoBack: today's ask;
   kUndoKept (a give only): the give stands - our pack's loss is published and the box's holder is asked to add our units; kUndoFailed:
   revertFailed, nothing sent. */
const int kUndoFailed = 0, kUndoBack = 1, kUndoKept = 2;
inline int PutBackRoad(int undone, int give)
{
    if (undone == 2) return (give != 0) ? kUndoKept : kUndoFailed;
    return (undone == 1) ? kUndoBack : kUndoFailed;
}

/* T-932: A SPLIT PART'S PIECE NOT FOUND AS ITS OBJECT (ItRcPieceEntry). An earlier part of the same pass put its units back through the
   merged put-back, which rebuilt the stack as a new object in the same square; a part (part) reads that square's object (cellOk: the
   square is in the section and read; plausible: an object is there; sameRecord: of the row's record). 1 = read it there. */
inline int PieceSquareFallback(int part, int cellOk, int plausible, int sameRecord)
{
    return (part != 0 && cellOk != 0 && plausible != 0 && sameRecord != 0) ? 1 : 0;
}

/* T-932: WHOM A BOX'S RE-ASK GOES TO (ItParityAskOne), as the open ask: the writer's slot when the box resolves here, its writer verdict
   is held by another game and the owner rule decided it (a player's box another game writes); else -1, the holder of the box's area. */
inline int ReaskSlot(int resolved, int held, int byOwner, int writerSlot)
{
    return (resolved != 0 && held != 0 && byOwner != 0) ? writerSlot : -1;
}

/* What one change in a row's window is to the row's job. handled: another road has it; inWindow: rung between the row's first and
   last change; isSrcObj / onSrc / onDst: the row's source object / in the source's container / in the destination's; listed: its
   object is already one of the job's pieces; kindRead / kindSame: its record read, and the row's. A click's source rings only the
   clicked object's op 1; a net row's source is every change of its source object. A click lists a destination change whose record
   could not be read (it is then read live with its piece); a net row only one whose record reads as the row's. */
const int kJobNone = 0, kJobSource = 1, kJobDest = 2;
inline int JobRole(int net, int handled, int inWindow, int op, int isSrcObj, int onSrc, int onDst, int listed, int kindRead, int kindSame)
{
    if (handled != 0 || inWindow == 0) return kJobNone;
    if ((op == 1 || net != 0) && isSrcObj != 0 && onSrc != 0) return kJobSource;
    if (onDst == 0) return kJobNone;
    if (listed != 0) return kJobDest;
    if (net != 0) return (kindRead != 0 && kindSame != 0) ? kJobDest : kJobNone;
    return (ChangeIsTheClicks(kindRead, kindSame) != 0) ? kJobDest : kJobNone;
}

/* THE BACK ROAD over a whole row: each readable piece (ok) is asked for its gain, never more than the source still lacks (BackWant), and
   `move` returns what it actually took out and gave back (ItRcMoveBack: BackUnitsTaken). Returns the units still lacking. */
typedef int (*BackMoveFn)(void* ctx, int piece, int want);
inline int BackRun(int units, const int* gain, const int* ok, int pieces, BackMoveFn move, void* ctx)
{
    int lacking = units;
    for (int p = 0; p < pieces; ++p)
    {
        if (ok[p] == 0) continue;   /* not listed as handled at the pass: its changes keep their old road */
        lacking -= move(ctx, p, BackWant(gain[p], lacking));
    }
    return lacking;
}

/* T-1191 (the `itemtest lclick` lever): the spot `d` units from a building's centre (bx, bz) on the line towards where the character
   stands now (cx, cz) - ground plane, x and z only, as the trade check 0x70C6C0 measures. A character on the centre itself is put
   on the +x side. 0 when d is not above 0. */
inline int BesideSpot(float bx, float bz, float cx, float cz, float d, float* tx, float* tz)
{
    if (!(d > 0.0f)) return 0;
    float dx = cx - bx, dz = cz - bz;
    const float len2 = dx * dx + dz * dz;
    if (!(len2 > 1e-6f)) { dx = 1.0f; dz = 0.0f; }
    else { const float len = std::sqrt(len2); dx /= len; dz /= len; }
    *tx = bx + dx * d; *tz = bz + dz * d;
    return 1;
}

/* T-1191 (the `itemtest bfac` / `lclick` levers): what a building's faction reads on this game. own: the building's own faction
   field (RootObject +0x10) is set (1), empty (0; the faction getter then hands back the shared "nofac", decomp_593bb0:18-24), unread
   (-1); resolved: the getter (vtable +0x58) gave a faction; local: it is this game's own player faction (pointer compare); controller:
   its player-controller field +0x250 is set; built: the building's finished byte (vtable +0x228); standIn: the player slot whose
   stand-in it is (-1 none). The gate's two inputs are built and controller (decomp_54fa10:34-39). -1 = unread throughout. */
struct BFac
{
    int own, resolved, local, controller, built, standIn;
    std::string id, name;
    BFac() : own(-1), resolved(-1), local(-1), controller(-1), built(-1), standIn(-1) {}
};
/* a name or id for the line: at most 48 characters, quotes and control characters as '?' */
inline std::string BFacClean(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < 48; ++i)
    {
        const unsigned char ch = (unsigned char)s[i];
        o += (ch < 0x20 || ch == '\'' || ch == '"' || ch == 0x7F) ? '?' : (char)ch;
    }
    return o;
}
/* nofac: the own field empty and the getter gave a faction (1), the own field set (0), unknown (-1) */
inline int BFacNofac(const BFac& f) { return (f.own == 0 && f.resolved == 1) ? 1 : ((f.own == 1) ? 0 : -1); }
/* the gate's answer from its inputs: 1 both, 0 either one 0, -1 an input unread */
inline int BFacGateIn(const BFac& f)
{
    if (f.built == 0 || f.controller == 0 || f.resolved == 0) return 0;
    return (f.built == 1 && f.controller == 1) ? 1 : -1;
}
inline std::string BFacText(const BFac& f)
{
    return "faction '" + BFacClean(f.name) + "' id '" + BFacClean(f.id) + "' own=" + std::to_string((long long)f.own) + " nofac=" + std::to_string((long long)BFacNofac(f))
        + " resolved=" + std::to_string((long long)f.resolved) + " localPlayer=" + std::to_string((long long)f.local) + " controller=" + std::to_string((long long)f.controller) + " standIn=" + std::to_string((long long)f.standIn)
        + " built=" + std::to_string((long long)f.built) + " gateIn=" + std::to_string((long long)BFacGateIn(f));
}

}   /* namespace rcgive */
