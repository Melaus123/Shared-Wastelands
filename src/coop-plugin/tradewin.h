// tradewin.h - THE PLAYER TRADE WINDOW (T-1167, owner 1633 / 1634 / 1635; the approved mock-up's words are src/common/tradewin.h).
// Piece 2 of 4: the window opens on BOTH games - the clicking game asks the other player's game through the world server (TRADE 94,
// src/common/tradewire.h), and that game opens its own window for the same pair or refuses; each side picks items from its own
// character's items into its offer and types its money; the other side's offer shows as it arrives ("Bo changed the offer."); ACCEPT
// names the offers' serials and the world server says when both accepted the same ones. Nothing moves: piece 3 adds the lock / commit /
// undo at READY, piece 4 the cancels (range, hostility, knockout, death, a player leaving). The right-click entry, its label and the
// click that opens this window are policy.cpp's T-1167 block.
#pragma once
#include <string>
#include <vector>
#include "../common/tradewire.h"   /* tradew::Item: the room check's rows */
#include "../common/handrec.h"    /* T-1167 piece 3 fold 1: hovrec::TradeSquare - where an object was lifted from / built to */

namespace coop {

/* one stack of a character's own items, as the window lists it (items.cpp ItemsTradeRows): the thing (record key, maker / material /
   colour records, the quality float's bits - tradewire.h Item's identity), the engine's own name for it, and the stack's count */
struct TradeInvRow { std::string key, company, material, colour; unsigned q, charges, unique; std::string name; int qty; int type, level; std::string model;   /* v3: the grade facts (chatlink.h) */
                     TradeInvRow() : q(0), charges(0), unique(0), qty(0), type(-1), level(0) {} };
// MAIN THREAD (items.cpp): the character's top-level items, one row per stack. 1 read, 0 no inventory.
int ItemsTradeRows(void* character, std::vector<TradeInvRow>* rows, std::vector<void*>* items = 0);   /* items: each row's object (piece 3's lock) */
// T-1167 piece 3 build 2 (items.cpp), MAIN THREAD - THE LOCK'S ENGINE WORK:
// `units` of the stack `item` (its whole count: the object itself; fewer: a split, the engine's partial arm - the shop's lift) out of
// the character's inventory into no inventory, NOT behind the applying guard (the loss publishes, as a give's lift); *obj the object
// now held, *body its bytes (the ITEM_CONFIRM body a hand-over line carries: fields, pack rows, owner mark, gear). 1 lifted; 0 nothing
// lifted (nothing changed); -1 lifted but not as asked or its bytes unreadable - *obj is set and the caller puts it back.
// Fold 1 (review p3a M2): *from (when given) the square it was lifted from, that square's stack before and after (has 0: not told).
int ItemsTradeLift(void* character, void* item, int units, int whole, void** obj, std::string* body, hovrec::TradeSquare* from);
// a lifted object back into the character (anywhere in its inventory, published), else what is left at its feet. 1 all in the
// inventory, 2 (part) at its feet, 0 not placed (logged; the object is left alone).
int ItemsTradePutBack(void* character, void* obj);
// the trade's `held` lines (handrec.h HandTradeLine), one per body in order, in ONE write of pp.handover: on disk when it returns 1.
// 0 not written (records writer off - fold 1, review p3a L1: then the lock refuses -, the file unreadable or unwritable, the cap, no key
// left) - *why says which. squares[i]: where object i was lifted from (written into its line).
int ItemsTradeLinesWrite(unsigned long long tradeId, int otherSlot, void* character, const std::vector<std::string>& bodies, std::string* why,
                         const std::vector<hovrec::TradeSquare>& squares);
// the trade's open lines closed in ONE write (the give-back). 1 written or none open (*closed = how many), 0 not written.
int ItemsTradeLinesClose(unsigned long long tradeId, int* closed);
// how many of the trade's lines are still open (held) on file; -1 the file could not be read (0 with the records writer off).
int ItemsTradeLinesOpen(unsigned long long tradeId);
// ---- T-1167 piece 3 build 3 (items.cpp), MAIN THREAD - COMMIT APPLIED, THE ROOM CHECK, THE ROAD AFTER A RELOAD ----
// one object of the other side built from its bytes (every field, pack rows, owner mark) into the character through the engine's own
// whole-inventory add (onto matching stacks first, then the first free square; published). 1 all in, 2 the rest at its feet (the rare
// fallback - an inventory that filled since the room check; logged; also made but placed nowhere: never made again), 0 not made (logged;
// tried again). Fold 1 (review p3a M2): *where (when given) the square the add ended on and its stack before / after (has 0: not told).
int ItemsTradeBuild(void* character, const std::string& body, hovrec::TradeSquare* where);
// 1 the bytes decode as an object (0: it can never be built here)
int ItemsTradeBodyDecodes(const std::string& body);
// FOLD 1 (review p3a M1 / M2): object `obj` of the other side built (E_i reserved just before, its placed mark - E_i and where it landed -
// written in the same call): tradel::kBdBuilt / kBdFeet / kBdTodo (not made: tried again) / kBdGiveUp (the bytes will not decode).
int ItemsTradeBuildObj(unsigned long long tradeId, unsigned int obj, void* character, const std::string& body, int* result);
// AFTER A RELOAD, received object `obj` (tradelock.h RecvObjAct on its placed mark, then only its square): kBdHere / kBdTodo / kBdWait.
int ItemsTradeRecvObj(unsigned long long tradeId, unsigned int obj, std::string* why);
// the give-back marks of this side's lifted objects `objs` (E_b `e`, reserved just before they were put back). 1 on disk.
int ItemsTradeBackMark(unsigned long long tradeId, const std::vector<unsigned>& objs, unsigned long long e);
// FOLD 1 (review p3a H1): does THIS GAME'S OWN SAVED RECORD hold the trade's result - the trading character's squad record committed at
// or past `e` (T-889's InRecordDurable; tradelock.h RecordHolds)? 1 yes (or nothing kept to wait for), 0 not yet (or a mark still to be
// written), -1 pp.handover unreadable.
int ItemsTradeDurable(unsigned long long tradeId, unsigned long long e);
// a lifted object destroyed - the other side has it now (COMMIT). 1 the engine destroyed it.
int ItemsTradeDestroy(void* obj);
// THE ROOM CHECK (owner 1641): do all of `rows` (the other side's offer) fit the character now, together (shopwire.h FitList over the
// engine's own cell test, stack limits and matching stacks, each row's size from a display copy made and destroyed here)? 1 they fit;
// 0 not - *noFit the rows (indexes) any unit of which did not fit, *why the words for the log; -1 could not tell (*why).
int ItemsTradeRoom(void* character, const std::vector<tradew::Item>& rows, std::vector<unsigned>* noFit, std::string* why);
// AFTER A RELOAD: the trade's open held lines settled by the world server's decision (handrec.h TradeHolderAction, hovrec::kTdCommitted /
// kTdAborted): a reloaded copy removed, nothing, or rebuilt (its give-back mark written in the same call; *eMax the E reserved before
// it). Fold 1: the lines are NOT closed here (the APPLIED gate closes them once this game's record holds the result); the state comes
// from a give-back mark, the records, then only the lift's square. 1 every line settled; 0 some wait (*waiting); -1 the file unreadable.
int ItemsTradeSettleHeld(unsigned long long tradeId, int decision, int* waiting, unsigned long long* eMax);
// the trade's receiving line (handrec.h HandTradeRecvLine) as this load sees it
struct TradeRecvInfo
{
    int open, frozenHolds, live, squadKnown; unsigned long long e, applied; void* character; unsigned int uid;
    TradeRecvInfo() : open(0), frozenHolds(0), live(0), squadKnown(0), e(0), applied(0), character(0), uid(0) {}
};
// 1 found (*out; character 0 = not loaded here or not this game's now), 0 none (or the records writer off), -1 the file unreadable.
int ItemsTradeRecvInfo(unsigned long long tradeId, TradeRecvInfo* out);
// COMMIT being applied, in ONE write: the receiving line closed with E (fold 1: the held lines stay open until the APPLIED gate). 1 written
// (or a repeat: *changed 0), 2 the records writer is off (no lines kept), -1 no open receiving line (missing or given back), 0 not written.
int ItemsTradeCommitWrite(unsigned long long tradeId, unsigned long long e, int* changed);
// the trade's engine counters, for the REPORT line
std::string ItemsTradeToken();
// MAIN THREAD (money.cpp): money.cpp's chat line for a delivery is left out when it is a trade's money (the trade says its own line)
// Fold 1 (review p3a L2): only while this trade's lock is open, or by the inbox number its COMMIT named (tradelock.h QuietMoney).
bool TradeWinQuietMoney(unsigned fromSlot, int amount, unsigned long long inbox);
// the trading character's uid while a TRADE window is open (0 none) - the cursortest pick lever's "me" (fold 1, review p3b L3)
unsigned int TradeWinMineUid();

// MAIN THREAD: the TRADE window for this game's character `mine` and another player's character `theirs` (player `theirSlot`), opened
// by this player's click: the window opens here and the other player's game is asked (ASK). 1 opened, 2 already open for that same
// pair (nothing done), 0 not opened (no GUI, or a widget was refused - logged).
int TradeWinOpen(void* mine, void* theirs, int theirSlot);
// MAIN THREAD: the window is up.
bool TradeWinIsOpen();
// MAIN THREAD, store.cpp's link pump: one TRADE message from the world server - kept here, acted on by TradeWinTick.
void TradeWinArrive(const std::vector<char>& payload);
// MAIN THREAD, every in-world frame (coop.cpp): the messages that arrived acted on (the other player's ask, answers, offers, accepts,
// cancels), this character's items and the money box read again, the lists redrawn.
void TradeWinTick();
// Every title-screen frame (coop.cpp): a window left up from the world is taken down (its trade cancelled); an ask that arrives at
// the title is answered "gone". MAIN THREAD.
void TradeWinTitleTick();
// The world teardown (store.cpp: leaving to the title, or another load): the window is taken down and its trade cancelled.
void TradeWinWorldTeardown();
// TEST-ONLY lever `tradetest status` / `press accept|cancel` / `offer <itemKey|name> <n>` / `cats <n>` (policy.cpp PolicyTradeTest
// hands them here): the window's state read back (both offers, their serials, the accept states), a button's own handler fired as a
// click, this side's offer of one item set to n (0 takes it out) as the clicks would, the money box set to the text as if typed.
std::string TradeWinStatus();
std::string TradeWinPress(const std::string& which);
std::string TradeWinOfferLever(const std::string& item, const std::string& n);
std::string TradeWinCatsLever(const std::string& text);
// The window's counters, a token for policy.cpp's REPORT line.
std::string TradeWinReportToken();

// T-1167 piece 3 build 4 (owner 1638 / 1639, mock-up 1642): THE DRAG (src/common/tradewin.h DropDecide; items.cpp's engine work).
// The cursor as the engine holds it at a let-go: the item's row (its thing and the count on the cursor - a plain drag one, the shop's
// modifier-drag the whole stack), whether it came out of `character`'s own inventory window, and the engine's own mouse position (the
// floats 0x712AD0 reads). 1 read (rowRead / fromMine / mouseRead say what), 0 no cursor or no item on it. MAIN THREAD.
struct TradeCursor { int rowRead, fromMine, mouseRead; float mx, my; TradeInvRow row; void* obj; TradeCursor() : rowRead(0), fromMine(0), mouseRead(0), mx(0.0f), my(0.0f), obj(0) {} };   /* v3: obj - the item on the cursor */
int ItemsTradeCursor(void* mouse, void* character, TradeCursor* out);
// the cursor item back on its square: the engine's own MouseInventory::returnItem (row MouseReturnItem). 1 called.
int ItemsTradeCursorReturn(void* mouse);
// FOLD 1 (review p3b): the cursor now - 1 an item on it, 0 empty, -1 unread; the cursor read now (ItemsTradeCursor on the engine's own
// mouse); and the cursor item that came out of `character`'s own window put back on its square (before that window is closed): 1 put
// back, 0 nothing of that window on the cursor, -1 it could not be put back.
int ItemsTradeCursorBusy();
int ItemsTradeCursorNow(void* character, TradeCursor* out);
int ItemsTradeCursorHome(void* character);
// the trading character's own inventory window opened through ForgottenGUI::showInventory's own entry in the engine's own place (as
// the shop opens the player's window: fold 1, every inventory window closed first - only with an empty cursor, else 0); *wasOpen it was
// up already; *widget its MyGUI window (0 unread). 1 called.
int ItemsTradeShowInventory(void* character, int* wasOpen, void** widget);
// that window closed through ForgottenGUI::closeInventory's own entry, when it is up. 1 closed.
int ItemsTradeCloseInventory(void* character);
// TEST-ONLY (tradetest drop): the engine's mouse set to (x, y) around the HOOKED 0x712AD0 entry on the cursor - the mod's drop decision
// runs as for a person's let-go - then put back. 1 called, 0 not (*why).
int ItemsTradeDropLever(int x, int y, std::string* why);
std::string ItemsTradeDragToken();
// T-1167 (owner's hands-on 2026-11-17): where object `obj` of `character` is now - 1 in its own inventory, 2 inside a pack it carries
// (*name, when given, the pack's own name), 0 neither. MAIN THREAD.
int ItemsTradeHolderName(void* character, const void* obj, std::string* name);
// MAIN THREAD, items.cpp's hook on 0x712AD0: 1 = the let-go was the TRADE window's (offered and put back, or put back) - the engine's
// own release does not run; 0 = not the TRADE window's.
int TradeWinDrop(void* mouse);
// T-1167 v3 (owner 1678), MAIN THREAD, items.cpp's hook on the engine's right release over an inventory window (1.0.65 0x713E50 /
// 1.0.68 0x714A20): 1 = the right-click was the TRADE window's (an item of the trading character's own window offered, or taken with
// nothing offered while the lock is open) - the engine's own right-click does not run; 0 = not ours. That engine function both moves
// items to another open window and, with no other window open, uses some items (a map read, two other kinds - decomp_713e50 ~:244-300):
// while trading, both are replaced by the offer for the trading character's own items (owner 1678, fold 1 review LOW-5).
int TradeWinRightClick(void* gui);
// the item under the engine's own mouse position in InventoryGUI `gui` when `gui` is `character`'s own window: the same floats, section
// test (the window's section table walked in its key order, the first hit, the section named by the table's KEY - fold 2) and cell sum
// as the engine's 0x70DC90 / 0x70FF10 inside 0x713E50 - so on the road where 0x7145F0 calls it with positions it
// stored itself (it writes them into those floats first) the item named is still the engine's (fold 1, review LOW-5). 1 found (*obj, *row
// read when *rowRead), 2 that window but no item there (or the position did not read), 0 not that window.
int ItemsTradeRightItem(void* gui, void* character, void** obj, TradeInvRow* row, int* rowRead);
// fold 1 (review LOW-4): Shift / Ctrl as the game saw them at the click, never the live key state - Shift from the engine's own flag (the
// byte its drag and its right-click loop read, row RClickAllFlag), Ctrl from MyGUI's InputManager (the game injects its keys there first,
// decomp_82a460:47-49). The engine has its own Ctrl byte too (1.0.65 DAT_1421323f8, set by the key handler 0x82A460, decomp_82a460:118-119)
// but no address row for it yet. Without the Shift row, MyGUI's Shift. 1 = Shift from the engine's flag, 0 = from MyGUI.
int ItemsTradeKeys(int* shift, int* ctrl);
// T-1167 v3 (the fade, measured in the owner's hands-on 2026-11-18): where offered object `obj` of `character` shows now - the inventory
// window showing the inventory that holds it (the character's own, or the own window of a pack it carries: the engine keeps one window
// per owner's hand, ForgottenGUI +0xA0), the section by that window's section-table key, the item's cells turned into screen pixels with
// the engine's own slot size (swtrade::ShadeRect). 1 placed: *l *t *w *h, *win that window's MyGUI widget, *inPack 1 for a pack's own
// window; 0 not shown now (no window open for it, a worn slot, on the cursor, lifted, or a read failed). MAIN THREAD.
int ItemsTradeShadeBox(void* character, const void* obj, int* l, int* t, int* w, int* h, void** win, int* inPack);
// TEST-ONLY `tradetest drop [x y]`: ItemsTradeDropLever at (x, y), else the middle of this player's offer pane; the window read back.
std::string TradeWinDropLever(const std::string& args);

// policy.cpp (the T-1167 block, MAIN THREAD): may this game open the window an ASK names - the asker's character `askerUid` (player
// `fromSlot`) and this player's own character `myUid`? tradew::Answer: kAnsOpen with *mine / *theirs set, else the refusal.
int PolicyTradeAsked(unsigned askerUid, unsigned myUid, int fromSlot, void** mine, void** theirs);

}
