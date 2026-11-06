/* src/common/tradewinguard.h - THE TRADE WINDOW NEVER CALLS THROUGH A FREED TRADER (T-1230).

   WHAT IT IS FOR. A trade window with a trader shows the trader's stand-in shop (a ShopTrader); the window's per-frame update
   reads the trader character the ShopTrader keeps (ShopTrader +0xC0) and calls through it (run T1965: InventoryTraderGUI +0x50
   -> 0x70CDC0 -> ShopTrader +0x140 on 1.0.68, the dump showing +0xC0 = the freed copy). Nothing closes the window when that
   character goes: in a session the trade window does not pause the game, and the game's own area unload destroys a copy of
   another game's trader while the window stays open (T1965: A teleported away, the engine's unload destroyed the copy) - the
   next frames call through freed memory and the game crashes once the memory is reused.

   THE RULE. The window is closed through the engine's own closeAllInventories (address row CloseInventories):
   - before the free: when GameWorld::destroy is about to destroy the window's trader (the engine's area unload, any other engine
     removal, or this mod's own removal), on the main thread, while the character is whole;
   - the backstop, at the main thread's safe point (the AI worker paused): while a trade window is open, the ShopTrader's trader is
     checked against the engine's character lists and this mod's character registry by ADDRESS only - the trader is never read to
     decide - and a trader in neither is gone: the window closes.
   The window counts as open from the engine's showTraderInventory until its closeInventory names the ShopTrader or a
   closeAllInventories runs (both hooked; the record is cleared by the same two events that close the engine's window). The closed
   hand's words are read before the engine's close runs (CloseEndsRecord below).
   Before the free, a cursor item is put back first through the mod's own put-back (the engine's 0x70D000, under the shop guard for
   the whole call when it is the tracked shop copy); on the backstop road (the trader already gone) the mod's put-back is skipped and
   closeAllInventories' own put-back (0x70D000, its first call, decomp_6e5380 :31) runs inside the close's __try/__except.

   Pure decisions; no engine types. */
#pragma once

namespace tradewinguard {

/* What is done with the open trade window: kKeep nothing; kClose close it now; kWait not now (inside the engine's own window
   open / close, or off the main thread) - the safe point's backstop decides later. */
enum { kKeep = 0, kClose = 1, kWait = 2 };

/* The road a close took, for the line and the counters. */
enum { kRoadUnload = 1, kRoadEngineRemoval = 2, kRoadModRemoval = 3, kRoadBackstop = 4 };

/* The cursor item before the close: kCurNone nothing held; kCurShopCopy the tracked shop copy (put back under the shop guard);
   kCurOwn any other item (put back plainly - the engine's own put-back to the window it came from). */
enum { kCurNone = 0, kCurShopCopy = 1, kCurOwn = 2 };

/* A hand's five id words (type, container, containerStamp, index, serial) name the same object. */
inline int SameHand(const unsigned int* a, const unsigned int* b)
{
    if (a == 0 || b == 0) return 0;
    for (int i = 0; i < 5; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

/* The type word of an empty hand: the engine's closeInventory 0x6E5010 writes it (and zeroes the other four words) into its own copy
   of the trader window's hand (gui +0x120) BEFORE it returns, when the close goes through that copy (decomp_6e5010 :92-99, :112-114). */
const unsigned int kHandTypeNone = 0xB;

/* Does the engine's close end the open trade window's record? all: 1 = closeAllInventories (every window). Otherwise named: 1 = the
   closed hand's five words were read, closeWords those words. THE WORDS MUST BE READ BEFORE THE ENGINE'S CLOSE RUNS: read after it,
   a close through the engine's own copy reads as an empty hand (kHandTypeNone, zeros), never matches, and the record stays open after
   the window closed - the trader's later unload would then close the player's other windows. recWords: the record's hand words. */
inline int CloseEndsRecord(int open, int all, int named, const unsigned int* closeWords, const unsigned int* recWords)
{
    if (open != 1) return 0;
    if (all != 0) return 1;
    return (named == 1 && SameHand(closeWords, recWords) == 1) ? 1 : 0;
}

/* The safe point's backstop. open: 1 = a trade window is open (recorded at its open, no close since). facade: 1 = the recorded
   ShopTrader still reads as a shop stand-in (its inventory's owner points back at it), 0 not, -1 unreadable. traderLive: 1 = the
   ShopTrader's trader is an address the engine's character lists or this mod's registry hold, 0 = in neither (or no trader).
   busy: 1 = inside the engine's own window open / close. A ShopTrader that no longer reads as one cannot name its trader: the
   window the engine still shows would call through whatever is there, so it closes. */
inline int Verdict(int open, int facade, int traderLive, int busy)
{
    if (open != 1) return kKeep;
    if (facade == 1 && traderLive == 1) return kKeep;
    return (busy != 0) ? kWait : kClose;
}

/* Before GameWorld::destroy destroys doomed: kClose when an open trade window's trader is doomed (address compare only) and the
   main thread is outside the engine's own window open / close; kWait when it is doomed but now is not the moment (the backstop
   closes it at the next safe point); else kKeep. mainThread: 1 = on the main thread. */
inline int BeforeDestroy(int open, int facade, const void* doomed, const void* trader, int mainThread, int busy)
{
    if (open != 1 || facade != 1 || doomed == 0 || doomed != trader) return kKeep;
    return (mainThread == 1 && busy == 0) ? kClose : kWait;
}

/* Which put-back the cursor item takes before the close. */
inline int CursorRoad(const void* held, const void* shopCopy)
{
    if (held == 0) return kCurNone;
    return (shopCopy != 0 && held == shopCopy) ? kCurShopCopy : kCurOwn;
}

/* The destroy's road: the engine's unload flag or an unload reason -> kRoadUnload; this mod's own removal -> kRoadModRemoval;
   anything else -> kRoadEngineRemoval. */
inline int DestroyRoad(int unload, int modRemoval)
{
    if (modRemoval != 0) return kRoadModRemoval;
    return (unload != 0) ? kRoadUnload : kRoadEngineRemoval;
}

}   /* namespace tradewinguard */
