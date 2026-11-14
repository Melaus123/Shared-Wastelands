#pragma once
/* T-1260: a body part a player takes from a knocked-out animal another game runs kills it on the game that runs it, as in single
 * player. Shared by the plugin (items.cpp, net/session.cpp) and the offline tests so the two can never disagree. Pure: no engine, no
 * Windows header. C++03.
 *
 * THE ENGINE (1.0.65; Read, the deciding byte Confirmed in T2109). The screen roads - the right-click take (RClickAutoTrade 0x713150,
 * its call at 0x7136EB) and the mouse pickup (GuiPickup 0x712DD0, its call at 0x712FF5) - run Inventory::deathCheck (0x75DCA0, row
 * Inventory_onOwnerDeath) on the SOURCE item before any split. It kills the knocked-out owner when the item's byte +0x138 is set and
 * always clears that byte. The engine's own takeItemOut checks the split-off copy on a partial take (whose byte is 0) and the source
 * on a whole take. On the game that only shows the animal the byte is 0 (never replicated), so its check kills nothing, and the item
 * request the take pairs into reached the owner's takeItemOut with no check of the source: a partial take never killed.
 *
 * THE FIX. The asking game notes (inventory, item) when deathCheck is called from one of those two routines on the main thread; a
 * take whose source (a split's original stack) was noted since the last drain carries `screenTake` on its item request (a TAKE or the
 * pickup's HOLD); the game that runs the animal calls deathCheck on its own item just before its takeItemOut - its own byte and its
 * own knocked-out state decide. Game-to-game protocol 217: the flag is the 'STK1' block every ITEM_REQUEST carries just before its
 * 'ASK1' trailer (boxrec.h AskTail). */
#include <vector>
#include <cstddef>

namespace coopstake {

/* 217 (T-1260; main was 213 - effort/t1122a holds 215 and effort/t1262a 216, both unmerged: whichever merges later re-checks this
   number). net/session.cpp kProtocolVersion asserts it is not above the session's. */
const unsigned int kGameProtocolScreenTake = 217u;

/* ---- the wire: u32 flags (bit 0 = screen take, nothing else may be set) | u32 'STK1' - 8 bytes, just before ASK1 ---- */
const unsigned int kTailMarker = 0x314B5453u;   /* 'STK1' */
const size_t kTailLen = 8u;
inline void PutU32Le(std::vector<char>* b, unsigned int v)
{
    b->push_back((char)(unsigned char)(v & 0xFFu)); b->push_back((char)(unsigned char)((v >> 8) & 0xFFu));
    b->push_back((char)(unsigned char)((v >> 16) & 0xFFu)); b->push_back((char)(unsigned char)((v >> 24) & 0xFFu));
}
inline unsigned int GetU32Le(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16)
         | ((unsigned int)(unsigned char)p[3] << 24);
}
inline void PutTail(std::vector<char>* b, int screenTake)
{
    PutU32Le(b, screenTake != 0 ? 1u : 0u);
    PutU32Le(b, kTailMarker);
}
/* the block at the end of `size` bytes: 1 = read (*screenTake 0 / 1; the caller cuts kTailLen bytes off the end); 0 = none there, or
   a flag word this build does not know (refused whole, never guessed at) */
inline int TakeTail(const char* p, size_t size, int* screenTake)
{
    if (p == 0 || size < kTailLen) return 0;
    if (GetU32Le(p + size - 4) != kTailMarker) return 0;
    const unsigned int f = GetU32Le(p + size - 8);
    if ((f & ~1u) != 0) return 0;
    if (screenTake != 0) *screenTake = (int)(f & 1u);
    return 1;
}

/* ---- the asking game's note (MAIN THREAD only: written by the deathCheck hook, read at the request's pairing) ---- */
const int kRoadNone = 0, kRoadRClick = 1, kRoadPickup = 2;
/* which screen road a deathCheck call came from, by its return address: inside [start, end) of the right-click routine or of the
   mouse pickup routine (a return address is never the routine's first byte). 0 = neither, or a row unbound. */
inline int RetRoad(unsigned long long ret, unsigned long long base, unsigned long long rclickRva, unsigned long long rclickEndRva,
                   unsigned long long pickupRva, unsigned long long pickupEndRva)
{
    if (base == 0 || ret <= base) return kRoadNone;
    const unsigned long long rva = ret - base;
    if (rclickRva != 0 && rclickEndRva > rclickRva && rva > rclickRva && rva < rclickEndRva) return kRoadRClick;
    if (pickupRva != 0 && pickupEndRva > pickupRva && rva > pickupRva && rva < pickupEndRva) return kRoadPickup;
    return kRoadNone;
}
/* The mouse pickup's body ends where the right-click routine begins in both Steam builds (the exception table, 1.0.65 0x712DD0 ..
   0x713150, 1.0.68 0x7139A0 .. 0x713D20). A second row at that address would be refused by the signature resolver (two rows at one
   address), so the right-click row is the pickup's end - used only while the order and sizes still hold. */
inline int RoadsOk(unsigned long long pickupRva, unsigned long long rclickRva, unsigned long long rclickEndRva)
{
    return (pickupRva != 0 && rclickRva > pickupRva && rclickRva - pickupRva <= 0x1000ull
            && rclickEndRva > rclickRva && rclickEndRva - rclickRva <= 0x2000ull) ? 1 : 0;
}
const int kNoteCap = 8;
struct Note { const void* inv; const void* item; unsigned long long gen; int road; int used; };
struct Notes { Note n[kNoteCap]; int next; };
inline void NotesClear(Notes* s)
{
    for (int i = 0; i < kNoteCap; ++i) { s->n[i].inv = 0; s->n[i].item = 0; s->n[i].gen = 0; s->n[i].road = 0; s->n[i].used = 0; }
    s->next = 0;
}
inline void NoteAdd(Notes* s, const void* inv, const void* item, unsigned long long gen, int road)
{
    if (item == 0) return;
    const int i = (s->next >= 0 && s->next < kNoteCap) ? s->next : 0;
    s->n[i].inv = inv; s->n[i].item = item; s->n[i].gen = gen; s->n[i].road = road; s->n[i].used = 1;
    s->next = (i + 1) % kNoteCap;
}
/* fresh = noted in this drain or the one before it (`gen` is bumped once per drain): a screen take is noted between two drains and
   paired in the next one; an older note never flags a request. A note that flags a request it was not made for (another take of the
   same source item within those two drains) cannot kill where single player would not: the note exists only because a screen road
   ran the engine's check on that very item, as single player does, and the owner's first call clears the item's mark (+0x138), so a
   second flagged call on it finds the mark clear and kills nothing */
inline int NoteFresh(unsigned long long noteGen, unsigned long long curGen) { return (curGen >= noteGen && curGen - noteGen <= 1ull) ? 1 : 0; }
/* THE DECISION: 1 = the request's source item (a split's original stack) was noted fresh, so the request carries screenTake. The
   item is compared, never read. srcInv 0 = the inventory is not compared. *roadOut: the note's road (0 = none). */
inline int Decide(const Notes* s, const void* srcItem, const void* srcInv, unsigned long long curGen, int* roadOut)
{
    if (roadOut != 0) *roadOut = kRoadNone;
    if (s == 0 || srcItem == 0) return 0;
    for (int i = 0; i < kNoteCap; ++i)
    {
        const Note& e = s->n[i];
        if (e.used == 0 || e.item != srcItem || NoteFresh(e.gen, curGen) == 0) continue;
        if (srcInv != 0 && e.inv != 0 && e.inv != srcInv) continue;
        if (roadOut != 0) *roadOut = e.road;
        return 1;
    }
    return 0;
}

/* ---- the owner's answer to deathCheck (for its line and counters) ---- */
const int kOwnerNotAsked = 0, kOwnerNoKill = 1, kOwnerKilled = 2, kOwnerNoCall = 3, kOwnerFault = 4;

/* ---- T-1260 leg 2: the two silent exits of the cross-game move's undo (items.cpp ItCrossTransfer) ----
   Each exit is counted on its own (the report's revertFailedBy) and said in a rate-limited ErrorLog line. */
const int kRfHalfMissing = 0;    /* a half of the move could not be found here (dst / src section, or the source's inventory) */
const int kRfPutBack = 1;        /* the put-back failed (ItRevert / ItRevertSplit answered 0) */
const int kRfGiveNoRecord = 2;   /* a give whose item record could not be read - not sent */
const int kRfTakeNoRecord = 3;   /* a request that names no item - not sent */
const int kRfCount = 4;
/* which half could not be found: the destination's section first, then the source's section, then the source's inventory */
const int kHalfNone = 0, kHalfDst = 1, kHalfSrc = 2, kHalfSrcInv = 3;
inline int HalfMissing(int dstSecFound, int srcSecFound, int srcInvFound)
{
    if (dstSecFound == 0) return kHalfDst;
    if (srcSecFound == 0) return kHalfSrc;
    if (srcInvFound == 0) return kHalfSrcInv;
    return kHalfNone;
}
inline const char* HalfName(int h)
{
    return h == kHalfDst ? "dst" : (h == kHalfSrc ? "src" : (h == kHalfSrcInv ? "srcInv" : "none"));
}
/* ItRevert's reason when it answers 0, through its out-parameter (it cannot log inside its __try) */
const int kRevNone = 0;
const int kRevNotFound = 1;       /* the object is not in the destination section and nothing could be made again in its place */
const int kRevCellTaken = 2;      /* the source square is taken and relocation is not allowed: nothing was touched */
const int kRevTakeOutFailed = 3;  /* the engine's takeItemOut handed nothing back: nothing was moved */
const int kRevPlaceFailed = 4;    /* taken out of the destination, the source would not take it: the object was destroyed here */
const int kRevFault = 5;          /* an engine call faulted inside the undo */
const int kRevRecreateFailed = 6; /* the object was gone from the destination and could not be made again from its record */
const int kRevSplit = 7;          /* the split's arithmetic undo (ItRevertSplit) failed */
const int kRevRemakeCellTaken = 8; /* T-1266: the object was gone from the destination and the copy made from its record could not be
                                      placed in the source square (taken): that copy was discarded - nothing was taken out (it was
                                      said as placeFailed before) */
const int kRevHoldsMore = 9;      /* T-1266: the object holds more units than were moved (merged onto a stack this game already had):
                                      nothing was touched - moving it back whole would carry this game's own units (undoarm.h) */
const int kRevMergedInto = 10;    /* T-1266 fold 1: a take's object is gone and the stack on its square has its record - its units merged
                                      into that stack: nothing was touched (the ending takes the moved units off it) */
const int kRevMergedUnknown = 11; /* T-1266 fold 1: a take's object is gone and a stack of its record is elsewhere in the section: not made
                                      again (a remake would double the units), nothing touched - where its units went is not known */
inline const char* RevertWhyName(int why)
{
    switch (why)
    {
    case kRevNotFound: return "notFound";
    case kRevCellTaken: return "cellTaken";
    case kRevTakeOutFailed: return "takeOutFailed";
    case kRevPlaceFailed: return "placeFailed";
    case kRevFault: return "fault";
    case kRevRecreateFailed: return "recreateFailed";
    case kRevSplit: return "splitUndo";
    case kRevRemakeCellTaken: return "remakeCellTaken";
    case kRevHoldsMore: return "holdsMore";
    case kRevMergedInto: return "mergedInto";
    case kRevMergedUnknown: return "mergedUnknown";
    default: return "unknown";
    }
}
/* WHAT A FAILED UNDO LEAVES (review fold 1: only what is known is said), by direction - dir 0 TAKE: the item came from the other
   game's side into this game's; 1 GIVE: this game's own item went to the other game's side - and by ItRevert's reason. Nothing is
   asked of the other game at either exit. */
const int kLeftDuplicate = 1;   /* a take whose move stands untouched: this game holds the item and the other game still does */
const int kLeftShortHere = 2;   /* a take whose object was destroyed / not made again here: this game's copy of the other side is short */
const int kLeftGiveGone = 3;    /* a give: this game's own item is out of its own side and the other game was never asked - no duplicate */
const int kLeftUnknown = 4;     /* notFound / fault / splitUndo / no reason: where the item is, is not known */
inline int RevertLeft(int dir, int why)
{
    if (why != kRevCellTaken && why != kRevTakeOutFailed && why != kRevPlaceFailed && why != kRevRecreateFailed
        && why != kRevRemakeCellTaken && why != kRevHoldsMore && why != kRevMergedInto) return kLeftUnknown;
    if (dir != 0) return kLeftGiveGone;
    return (why == kRevCellTaken || why == kRevTakeOutFailed || why == kRevHoldsMore || why == kRevMergedInto) ? kLeftDuplicate : kLeftShortHere;
}
/* the put-back exit's line: what the failure leaves, after "failed (<reason>) - " */
inline const char* RevertLeftText(int dir, int why)
{
    switch (why)
    {
    case kRevCellTaken:
    case kRevTakeOutFailed:
        return (dir == 0)
            ? "nothing was touched: the move stands on this screen, nothing was asked of the game that writes the other side, and the item"
              " now exists on both games (a duplicate)"
            : "nothing was touched: the move stands on this screen - this game's own item is out of its own side and in this screen's copy"
              " of the other side, and that game was never asked, so it does not hold the item (no duplicate); the item may be lost";
    case kRevPlaceFailed:
        return (dir == 0)
            ? "the object was destroyed here and nothing was asked of the game that writes the other side: this game's copy of that side"
              " is short of it until that game's next record (no duplicate)"
            : "this game's own item was destroyed here and the other game was never asked, so neither game holds it (no duplicate - the"
              " item is lost)";
    case kRevHoldsMore:
        return (dir == 0)
            ? "nothing was touched: the object on this game's side holds more units than were moved (the engine merged it onto a stack"
              " this game already had), so moving it back whole would carry this game's own units; nothing was asked of the game that"
              " writes the other side, and the moved units now exist on both games (a duplicate)"
            : "nothing was touched: this game's own item is merged into this screen's copy of the other side and that game was never"
              " asked, so it does not hold the item (no duplicate); the item may be lost";
    case kRevMergedInto:
        return (dir == 0)
            ? "nothing was touched: the moved object is gone from this game's side and the stack on its square has its record (the engine"
              " merged the units into it); nothing was asked of the game that writes the other side, and the moved units now exist on"
              " both games (a duplicate)"
            : "nothing was touched: this game's own item merged into a stack in this screen's copy of the other side and that game was"
              " never asked, so it does not hold the item (no duplicate); the item may be lost";
    case kRevMergedUnknown:
        return "nothing was made again and nothing was asked of the game that writes the other side: the moved object is gone and a stack"
               " of its record is elsewhere on this side, so where its units went is unknown - they may be on both games";
    case kRevRemakeCellTaken:
        return (dir == 0)
            ? "the object had left this game's side (merged or destroyed by the engine) and the copy made again from its record found its"
              " square on the other side taken, so it was discarded; nothing was asked of the game that writes that side: this game's"
              " copy of it is short of the item until that game's next record - unless the engine merged it into another stack on this"
              " game's side, whose units are then on both games"
            : "this game's own item had left this screen's copy of the other side (merged or destroyed by the engine) and the copy made"
              " again from its record found its square on this game's side taken, so it was discarded, and that game was never asked:"
              " this game no longer holds it and that game does not (no duplicate); the item may be lost";
    case kRevRecreateFailed:
        return (dir == 0)
            ? "the object had left this game's side (merged or destroyed by the engine) and could not be made again on the other side here,"
              " and nothing was asked of the game that writes it: this game's copy of that side is short of it until that game's next"
              " record - unless the engine merged it into another stack on this game's side, whose units are then on both games"
            : "this game's own item had left this screen's copy of the other side (merged or destroyed by the engine) and could not be"
              " made again on this game's side, and that game was never asked: this game no longer holds it and that game does not (no"
              " duplicate); the item may be lost";
    case kRevNotFound:
        return "the object was not on the destination side here and nothing was made again in its place, and nothing was asked of the"
               " game that writes the other side: where the item is, is unknown - it may be on both games, or on neither";
    case kRevFault:
        return "an engine call faulted inside the undo and nothing was asked of the game that writes the other side: the state is"
               " unknown - the item may be on both games, on neither, or half-filed here";
    case kRevSplit:
        return "the split's undo was not done whole and nothing was asked of the game that writes the other side: the state is unknown -"
               " the units may be on both games, or short on one side (splitUnitLost counts that)";
    default:
        return "the reason is unknown and nothing was asked of the game that writes the other side: the item may be on both games, or on"
               " neither";
    }
}
/* the half-missing exit's line: what it leaves, after "could not be found here - " (nothing was undone) */
inline const char* HalfMissingLeftText(int dir)
{
    return (dir == 0)
        ? "the move stands on this screen, nothing was asked of the game that writes the other side, and the item now exists on both"
          " games (a duplicate)"
        : "the move stands on this screen and nothing was asked of the game that writes the other side, so that game does not hold the"
          " item (no duplicate is known); whether this game's own item is lost is unknown";
}
/* the ErrorLog rate: the first 5 of each exit, then every 100th */
inline int SayNth(long long n) { return (n >= 1 && (n <= 5 || n % 100 == 0)) ? 1 : 0; }

}   /* namespace coopstake */
