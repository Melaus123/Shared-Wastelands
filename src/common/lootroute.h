/* loot2c part B (user decision 2026-09-26: a research copy is the player's real item from its first touch, never undone).
   THE PURE DECISIONS of the two routes that take a touched book somewhere this game cannot publish it on its own - its home
   box when ANOTHER game holds that box (build-plan step 8, "materialise"), and a friend's character (step 9, "give"). Both go
   through the mod's existing cross-authority GIVE (items.cpp ItCrossTransfer dir 1: MSG_ITEM_REQUEST / MSG_ITEM_CONFIRM).
   Header-only; the plugin (items.cpp) and the offline suite (coop-test) include it. No C++11. */
#ifndef COOP_LOOTROUTE_H
#define COOP_LOOTROUTE_H

namespace coopres {

const int kLootRouteFailed = 0;    /* nothing moved: the book is where it was (and still this game's only) */
const int kLootRouteAsked = 1;     /* the book is in the acting player's own pack and the far side's writer was asked to make it */
const int kLootRouteOwnPack = 2;   /* the book is in the acting player's own pack and stays there (an ordinary item of theirs) */
const int kLootRouteDone = 3;      /* the far side made it and the pack copy was removed */
const int kLootRouteNoRoom = 4;    /* fold (review-loot2c-restructure 3b): no own character, or no free cell in any own character's pack -
                                      nothing moved; the book waits where it is, watched, and is routed again every drain (never destroyed) */

/* after one ItCrossTransfer call: did its undo move the book onto our character (reverted), and did a request go out (sent) */
inline int LootRouteOutcome(int reverted, int sent)
{
    if (sent != 0) return kLootRouteAsked;
    return (reverted != 0) ? kLootRouteOwnPack : kLootRouteFailed;
}
/* at the MSG_ITEM_CONFIRM of such a request: refused = the far side made nothing; removed = our pack copy was taken out */
inline int LootConfirmOutcome(int refused, int removedFromPack)
{
    if (refused != 0) return kLootRouteOwnPack;
    return (removedFromPack != 0) ? kLootRouteDone : kLootRouteFailed;
}
/* the drain's unpaired ADD: a give (step 9) only for an op-0 ADD onto a CHARACTER this game does not own, of an item that a
   recorded first touch put on the watch list */
inline int LootLandingIsGive(int op, int kindIsChar, int charIsMine, int watched)
{
    return (op == 0 && kindIsChar != 0 && charIsMine == 0 && watched != 0) ? 1 : 0;
}

/* fold (review-loot2c-restructure 3a): THE WATCH LIST's decisions. A taken book (and every stack split off one) stays watched
   until it is SETTLED - the other games know it where it is. After every drain one watched book is looked at: */
const int kWatchAtNowhere = 0;     /* not found where the last entry put it: the ground (not synced for any item), or a route no detour saw */
const int kWatchAtOwnChar = 1;     /* in one of this game's own characters' inventories */
const int kWatchAtHeldBox = 2;     /* in a box this game holds */
const int kWatchAtOtherBox = 3;    /* in a box another game holds (or no game can be said to hold now) */
const int kWatchAtFriend = 4;      /* on a character another game owns */
const int kWatchLeave = 0, kWatchSettle = 1, kWatchPublish = 2, kWatchRouteHolder = 3, kWatchRouteGive = 4, kWatchWait = 5;
inline int LootWatchAction(int onCursor, int requestOut, int known, int at)
{
    if (requestOut != 0 || onCursor != 0) return kWatchLeave;   /* a request's answer decides; a book on the cursor is left alone until it lands */
    if (known != 0) return kWatchSettle;                         /* published where it is (own pack, held box, or a route's pack add) */
    if (at == kWatchAtOwnChar) return kWatchSettle;              /* an ordinary item of that character */
    if (at == kWatchAtHeldBox) return kWatchPublish;             /* this game is the box's writer: an ordinary op-0 box add */
    if (at == kWatchAtOtherBox) return kWatchRouteHolder;        /* own pack -> ITEM_REQUEST to the holder (the ordinary non-holder box write) */
    if (at == kWatchAtFriend) return kWatchRouteGive;            /* the ordinary give */
    return kWatchWait;                                           /* watched, looked at again next drain */
}
/* the drain: a REMOVE / quantity entry of a watched book the other games do not know where it is is absorbed (they never had it
   there); a landing ADD, a published book and a book whose request is out go on as ordinary moves */
inline int LootWatchAbsorb(int op, int known, int requestOut)
{
    return (op != 0 && known == 0 && requestOut == 0) ? 1 : 0;
}

/* a small ring of (tag, value): the recently taken copies' item pointers, and the request ids these routes sent. Tag 0 is
   never stored; a re-put tag keeps one entry; the oldest slot is overwritten when it wraps. Zero-filled = empty. */
const int kLootRingCap = 16;
struct LootRing
{
    unsigned long long tag[kLootRingCap];
    int val[kLootRingCap];
    int next;
};
inline void LootRingClear(LootRing* r)
{
    for (int i = 0; i < kLootRingCap; ++i) { r->tag[i] = 0; r->val[i] = 0; }
    r->next = 0;
}
inline void LootRingPut(LootRing* r, unsigned long long tag, int val)
{
    if (tag == 0) return;
    for (int i = 0; i < kLootRingCap; ++i) if (r->tag[i] == tag) { r->val[i] = val; return; }
    if (r->next < 0 || r->next >= kLootRingCap) r->next = 0;
    r->tag[r->next] = tag; r->val[r->next] = val;
    r->next = (r->next + 1) % kLootRingCap;
}
inline int LootRingHas(const LootRing* r, unsigned long long tag)
{
    if (tag == 0) return 0;
    for (int i = 0; i < kLootRingCap; ++i) if (r->tag[i] == tag) return 1;
    return 0;
}
/* the tag's value (0 = not there), and the entry is removed */
inline int LootRingTake(LootRing* r, unsigned long long tag)
{
    if (tag == 0) return 0;
    for (int i = 0; i < kLootRingCap; ++i)
        if (r->tag[i] == tag) { const int v = r->val[i]; r->tag[i] = 0; r->val[i] = 0; return v; }
    return 0;
}

/* loot2d (T379): WHY a research copy's retract did or did not take it out of its home box - kept on the copy (retractWhy) and
   printed by the save tripwire. The retract takes the copy out BY ITS OBJECT (Inventory::takeItemOut on the
   copy's own pointer), never by the cell it reports: T379 showed two copies at ONE cell (section backpack_content, slot 0,0),
   and once the first was taken the cell held nothing - the old by-cell remove found no item there on every tick and every
   save (retractFailed=30910, lootLiveAtWrite=93). The cell is only asked to name the outcome. */
const int kLootRetractNever = 0;         /* no retract has looked at this copy */
const int kLootRetractOk = 1;            /* taken out; its cell held it */
const int kLootRetractOkByObject = 2;    /* taken out; its cell held another item or nothing (T379's case) */
const int kLootRetractNotLive = 3;       /* touched, taken or gone - not the retract's */
const int kLootRetractNoBox = 4;         /* the home box did not resolve (unloaded) */
const int kLootRetractNotInBox = 5;      /* no section of the box lists it */
const int kLootRetractClaimLost = 6;     /* the engine destroyed it between the look and the claim */
const int kLootRetractRefused = 7;       /* the engine's take-out handed nothing back - still in the box */
const int kLootRetractFault = 8;         /* the take-out faulted - still counted as in the box */
/* after the take-out of a copy a section LISTS: takeOut 1 = the engine handed the copy back (destroyed), 0 = it handed nothing
   back, -1 = the call faulted. Whether its cell held it never decides whether it is taken out. */
inline int LootRetractWhyAfterTakeOut(int takeOut, int cellHoldsCopy)
{
    if (takeOut < 0) return kLootRetractFault;
    if (takeOut == 0) return kLootRetractRefused;
    return (cellHoldsCopy != 0) ? kLootRetractOk : kLootRetractOkByObject;
}
/* the retract's answer to its callers: 1 retracted, 0 nothing to take out here, -1 still in its box (close: retried next tick) */
inline int LootRetractResult(int why)
{
    if (why == kLootRetractOk || why == kLootRetractOkByObject) return 1;
    if (why == kLootRetractRefused || why == kLootRetractFault) return -1;
    return 0;
}
inline const char* LootRetractWhyName(int why)
{
    switch (why)
    {
    case kLootRetractNever: return "never tried";
    case kLootRetractOk: return "taken out";
    case kLootRetractOkByObject: return "taken out by the object (its cell held another item or nothing)";
    case kLootRetractNotLive: return "not live";
    case kLootRetractNoBox: return "box not resolved";
    case kLootRetractNotInBox: return "not listed in its box";
    case kLootRetractClaimLost: return "destroyed by the engine during the retract";
    case kLootRetractRefused: return "the engine's take-out handed nothing back";
    case kLootRetractFault: return "the take-out faulted";
    }
    return "?";
}

} // namespace coopres

#endif
