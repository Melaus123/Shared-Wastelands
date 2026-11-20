// tradewin.cpp - see tradewin.h. Built as the mod's other windows are built (fallentab.cpp OpenBox, playerstab.cpp OpenBox): the
// game's own skins, made in code, never from a layout file - Kenshi_WindowC on the Info layer (Popup when Info is not there), titled
// TRADE; two panes side by side, each with its head (src/common/tradewin.h OwnHead / TheirHead), its offer list (T-1167 v3: a plain Kenshi_ListBox, no column header; was Kenshi_MultiListBox,
// one column with no head: the mock-up shows none - "2 x Bolts"), its money line and its status line; ACCEPT and CANCEL at the foot
// (Kenshi_Button2, lit by hand). This player's money line is "Money: c." with the digits-only box after it (the GIVE MONEY box's:
// Kenshi_EditBox, moneyw::DigitsOnly on every change, at most moneyw::kAmountDigitsMax digits) and its hint, "(digits only)" - or
// "You don't have that much." / "Type an amount." as the amount is checked against the purse while it is typed.
// THE DRAG (piece 3 build 4, owner 1638 / 1639, mock-up 1642): the trading character's own inventory window opens beside the window
// (the shop's way: the engine's own showInventory, in the engine's own place; the TRADE window moves beside it) and closes with the
// trade when the trade opened it. An item dragged out of it and let go over this player's offer pane is offered - a plain drag one
// unit, the shop's modifier-drag the whole stack - and goes straight back to its square (TradeWinDrop, from items.cpp's hook on the
// engine's release-over-no-window 0x712AD0): the item never leaves the inventory until the lock lifts it (design A). A click on a row
// of this player's offer takes that row back out (nothing moves) - a click on a list row, as the FALLEN and PLAYERS tabs pick theirs
// (eventListChangePosition); acted on in the next tick, never inside the list's own event, the selection cleared.
// NOT MODAL (build 4): a modal MyGUI window takes every mouse press, so no drag could start in the inventory window; the key focus is
// put on the money box at the open (a click elsewhere moves it, as anywhere in MyGUI). One window at a time. MyGUI's destroy notice
// drops each pointer before its widget is freed (as fallentab.cpp's Unlinker). Every handler runs behind SEH and a C++ catch.
// THE TRADE (src/common/tradewire.h): this game holds one trade - its id, its side (0 the asker, 1 the asked), both offers with their
// serials and both accept states. Every message goes through the world server (TRADE 94): ASK when this player clicks, ANSWER when
// the other player's game asks, OFFER on every change of this side's offer (serial + 1: both accepts cleared), ACCEPT naming both
// serials, CANCEL when the window closes. The world server's ACCEPTS is the accept state both windows show.
// PIECE 3 BUILD 3 (owner 1635 / 1637 / 1638 / 1641 / 1642): COMMIT is applied (this side's lifted objects are the other side's: destroyed;
// theirs built into the character; the lines and the money each in one write; APPLIED once on disk; the chat line "You traded ... to
// <name> for ...."); a lock that has no room for the other side's items is refused NO ROOM and both windows stay open with "No room for
// <rows>." under the pane of the side without room; offered rows read "2 x Bolts" in one column; no "(digits only)" hint. A decision
// with no live lock here (a reload or a restart since the lift) is settled from the lines and the held money on file (RecoverTick).
// PIECE 2: nothing moves - both accepted shows ACCEPTED on both panes and the trade waits; PIECE 3 hooks the lock /
// commit / undo where READY arrives (ActOn kArrAccepts); PIECE 4 adds the cancels (range, hostility, knockout, death, a player
// leaving) and their lines, where Close is called with the reason.
#include "tradewin.h"
#include "coop_log.h"
#include "spawn.h"         /* NameOf: a character's current name; FindSpawned / FindSpawnedUid */
#include "team.h"          /* TeamPlayerName: a player's name as the faction screens say it */
#include "store.h"         /* StoreSendTrade, StoreMoneyRead, StoreShowPlayerLine */
#include "playerfaction.h" /* MySlotForWire */
#include "../common/tradewin.h"
#include "../common/tradewire.h"
#include "../common/moneywire.h"     /* DigitsOnly, kAmountDigitsMax, DigitsHint - the GIVE MONEY box's amount rules */
#include "../common/playerstab.h"    /* EscapeHash: a '#' in a name is shown as itself, never a colour code */
#include "../common/tradelock.h"     /* T-1167 piece 3 build 2: the lock decision table, LOCK_NOW / ABORT steps */
#include "ownstore.h"                /* OwnLoadGen: a load ends the live lifted objects */
#include "money.h"                   /* T-1167 piece 3 build 3: MoneyTradeSettle */
#include "chat.h"                    /* T-1167 piece 3 build 3: the chat line */
#include "../common/handrec.h"       /* T-1167 piece 3 build 3: kTdCommitted / kTdAborted */
#include <map>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_ListBox.h>     /* T-1167 v3: a plain list, no column header */
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_WidgetManager.h>
#include <mygui/MyGUI_IUnlinkWidget.h>
#include <mygui/MyGUI_LayerManager.h>   /* T-1167 v3 (the fade): the shades kept above the inventory window */
#include <mygui/MyGUI_ILayer.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ctime>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

namespace {

/* our widgets: "SW" names, no '_' (ui.cpp's rule) */
const char* const kWinName       = "SWTradeWin";
const char* const kHeadName[2]   = { "SWTradeOwnHead", "SWTradeTheirHead" };
const char* const kListName[2]   = { "SWTradeOwnList", "SWTradeTheirList" };
const char* const kMoneyName[2]  = { "SWTradeOwnMoney", "SWTradeTheirMoney" };
const char* const kStatusName[2] = { "SWTradeOwnStatus", "SWTradeTheirStatus" };
const char* const kAmountName    = "SWTradeOwnAmount";
const char* const kHintName      = "SWTradeOwnHint";
const char* const kAcceptName    = "SWTradeAcceptBtn";
const char* const kCancelName    = "SWTradeCancelBtn";
const char* const kTintName[2]   = { "SWTradeOwnTint", "SWTradeTheirTint" };   /* T-1167 v3: the green panel of a side that accepted */
const char* const kGripName      = "SWTradeGrip";                              /* T-1167 v3: the resize grip */
const size_t kInboxCap = 256;                    /* TRADE messages waiting for the tick; more are counted and dropped */
const int kSayCap = 200;                         /* [TRADE] message lines a game prints; the rest are counted */
const unsigned long kInvEveryMs = 1000;          /* this character's items and the purse read again */

bool g_unlinkerOn = false;
MyGUI::Widget* g_win = 0;
MyGUI::TextBox* g_head[2] = { 0, 0 };
MyGUI::ListBox* g_list[2] = { 0, 0 };
MyGUI::Widget* g_tint[2] = { 0, 0 };   /* T-1167 v3 */
MyGUI::Widget* g_grip = 0;
/* T-1167 v3 (the fade, ShadeTick): one see-through root on the inventory window's own layer, a shade under it for each offered object
   shown now; the root's layer name; the counters on the REPORT line */
const char* const kShadeRootName = "SWTradeShades";
MyGUI::Widget* g_shadeRoot = 0;
std::vector<MyGUI::Widget*> g_shade;
std::string g_shadeLayer;
long long g_shadeShown = 0, g_shadeInPack = 0, g_shadeUnplaced = 0, g_shadePeak = 0, g_shadePackPeak = 0, g_shadeMade = 0, g_shadeRaised = 0, g_shadeLayerFail = 0;
bool g_shadeLayerSaid = false;
MyGUI::TextBox* g_caption = 0;          /* the window's TRADE title: a press-and-drag on it moves the window */
MyGUI::TextBox* g_money[2] = { 0, 0 };
MyGUI::TextBox* g_status[2] = { 0, 0 };
MyGUI::TextBox* g_hint = 0;
MyGUI::EditBox* g_amount = 0;
MyGUI::Button* g_accept = 0;
MyGUI::Button* g_cancel = 0;
void* g_mine = 0;                /* the pair the window is for (compared only, never read through: the uids are looked up again) */
void* g_theirs = 0;
unsigned int g_mineUid = 0, g_theirUid = 0;
int g_theirSlot = -1;
std::string g_headText[2];
std::string g_theirWho;          /* "Bo changed the offer.": the other player's name, else the character's */
/* the trade (tradewire.h): g_side -1 = none */
tradew::Trade g_tr;
int g_side = -1;
bool g_changed = false;          /* their offer changed since the last accept state */
unsigned g_idRun = 0;
/* this character's items (read, never listed: build 4 drags from the character's own window), the money box, the picks */
std::vector<coop::TradeInvRow> g_rows;
std::vector<void*> g_items;              /* T-1167 v3: each of g_rows' own object (the cursor item last, when it was added) */
std::vector<tradel::ExRow> g_ex;         /* T-1167 v3: THE EXACT ROWS - this side's offer rows with their objects (tradelock.h) */
unsigned g_exNextId = 1;
long long g_exSame = 0, g_exTrims = 0, g_rightOffered = 0, g_rightTaken = 0, g_moves = 0, g_resizes = 0;
/* the move / resize (chat.cpp's road): 1 moving by the title, 2 resizing by the grip; the press point and the window's rect then */
int g_dragKind = 0; int g_pressX = 0, g_pressY = 0; MyGUI::IntCoord g_dragFrom;
std::vector<std::string> g_offShown[2];
unsigned long g_invAt = 0;
std::string g_amountText;
int g_amountCheck = tradew::kAmtOk;
long long g_purse = -1;
int g_pickOffer = -1;
/* build 4: the character's inventory window - opened by this trade (closed with it); the drag's counters. Fold 1 (review p3b M2): the
   open waits while anything is on the cursor (g_invWant); g_rowsRead: the last read of the character's items worked (review p3b L1) */
bool g_invOpened = false, g_invWant = false, g_rowsRead = false;
long long g_dropEngine = 0, g_dropOffered = 0, g_dropBack = 0, g_dropNoReturn = 0, g_dropFaults = 0, g_invOpenFail = 0;
std::deque<std::vector<char> > g_inbox;
long long g_opened = 0, g_repeat = 0, g_refusedOpen = 0, g_accepts = 0, g_cancels = 0, g_closed = 0, g_faults = 0, g_throws = 0;
long long g_sent[8] = { 0 }, g_recv[8] = { 0 }, g_sendFail = 0, g_malformed = 0, g_otherId = 0, g_inboxDropped = 0, g_ready = 0, g_acceptRefused = 0;
/* T-1167 piece 2 fold 1: the world-server link's edge when the trade began (StoreMoneyEdge: every WELCOME, link going down and
   profile pick raises it); the half-open ends (window gone, link edge) and the trims, counted */
long g_linkEdge = 0;
long long g_windowGone = 0, g_linkDropped = 0, g_trimmed = 0;
int g_said = 0, g_unsaid = 0;
/* T-1167 piece 3 build 2: THE LOCK (src/common/tradelock.h) - held apart from the window and the trade: Forget never clears it */
struct LockState
{
    unsigned long long id; int phase; int otherSlot; unsigned int uid; long gen;
    unsigned serial, cats; unsigned long long transfer, seq;
    std::vector<void*> objs; std::vector<std::string> bodies; std::vector<int> back;   /* back: 0 still out, 1 in the character, 2 at its feet */
    unsigned long backAt; bool waitSaid;
    /* build 3: both offers and the other player's name as the lock took them (the chat line), the decision, COMMIT's steps done */
    tradew::Offer mine, theirs; std::string who; tradew::Msg dec; bool linesDone, built; unsigned long long e;
    /* fold 1 (review p3a H1 / M1): each received object's build state (tradelock.h Build), the lifted objects destroyed once, the E the
       APPLIED gate waits for (this game's record at or past it), the give-back's E_b, the gate's one wait line */
    std::vector<int> bst; bool destroyed; unsigned long long eMax, eBack; bool recSaid, buildSaid;
    LockState() : id(0), phase(tradel::kLkNone), otherSlot(-1), uid(0), gen(0), serial(0), cats(0), transfer(0), seq(0), backAt(0), waitSaid(false), linesDone(false), built(false), e(0),
                  destroyed(false), eMax(0), eBack(0), recSaid(false), buildSaid(false) {}
};
LockState g_lock;
const unsigned long kLockWaitMs = 8000;   /* a LOCK_NOW waits this long for money to be allowed to move (the world server's clock: 10 s) */
bool g_nowPend = false; tradew::Msg g_nowMsg; unsigned long g_nowAt = 0; std::string g_nowWaitSaid;
unsigned long long g_readyId = 0;          /* the trade both sides accepted (READY): no change from then on */
/* build 3: A DECISION WITH NO LIVE LOCK HERE (a reload or restart since the lift, a refusal before any lift, a decision that came at the
   title): settled from what is on file - stage 0 the held lines (TradeHolderAction), 1 the objects received (COMMIT), 2 the money, 3
   APPLIED once on disk (RecoverTick) */
struct Pend
{
    tradew::Msg m; int stage; unsigned long long seq; unsigned long at; bool said;
    std::vector<int> st; unsigned long long eMax; bool recSaid;   /* fold 1: each received object's state; the E the APPLIED gate waits for */
    Pend() : stage(0), seq(0), at(0), said(false), eMax(0), recSaid(false) {}
};
std::map<unsigned long long, Pend> g_pend;
/* build 3 (owner 1641): the no-room line - under this player's pane (0) or the other player's (1); cleared by any change of either offer */
std::string g_noRoomLine; int g_noRoomPane = 0;
bool g_refundOwed = false;
long long g_lkLocked = 0, g_lkRefused = 0, g_lkAborted = 0, g_lkApplied = 0, g_lkRepeats = 0, g_lkFails = 0, g_lkRecv[14] = { 0 };
long long g_lkNoRoom = 0, g_lkNoRoomShown = 0, g_lkCommitted = 0, g_lkRecovered = 0;
/* the window takes no change: READY, a LOCK_NOW waiting, or this trade's lock open */
bool Locking()
{
    if (g_side < 0) return false;
    return (g_nowPend && g_nowMsg.id == g_tr.id) || (tradel::PhaseOpen(g_lock.phase) && g_lock.id == g_tr.id) || (g_readyId != 0 && g_readyId == g_tr.id)
        || g_pend.count(g_tr.id) != 0;   /* build 3: a decision of this trade still being settled here */
}

template <class T> std::string S(const T& v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
MyGUI::UString U(const std::string& s) { return MyGUI::UString(playerstab::EscapeHash(s).c_str()); }   /* never a colour code */
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }    /* Kenshi's light caption colour, as the mod's boxes */
std::string IdText(unsigned long long id) { return tradew::N(id); }
void Say(const std::string& line)
{
    if (g_said >= kSayCap) { ++g_unsaid; return; }
    ++g_said;
    DebugLog(line + (g_said == kSayCap ? std::string(" (the last such line this game prints; the rest are counted on the REPORT line)") : std::string()));
}

bool Inside(MyGUI::Widget* mine, MyGUI::Widget* gone)
{
    for (MyGUI::Widget* p = mine; p != 0; p = p->getParent()) if (p == gone) return true;
    return false;
}
void ForgetWidgets()
{
    g_win = 0; g_amount = 0; g_accept = 0; g_cancel = 0; g_hint = 0; g_grip = 0; g_caption = 0; g_dragKind = 0;
    for (int k = 0; k < 2; ++k) { g_head[k] = 0; g_list[k] = 0; g_money[k] = 0; g_status[k] = 0; g_tint[k] = 0; }
}
void Forget()
{
    ForgetWidgets();
    for (int k = 0; k < 2; ++k) { g_headText[k].clear(); g_offShown[k].clear(); }
    g_mine = 0; g_theirs = 0; g_mineUid = 0; g_theirUid = 0; g_theirSlot = -1; g_theirWho.clear();
    g_tr = tradew::Trade(); g_side = -1; g_changed = false; g_readyId = 0;
    g_rows.clear(); g_items.clear(); g_ex.clear(); g_invAt = 0; g_amountText.clear(); g_amountCheck = tradew::kAmtOk; g_purse = -1;
    g_pickOffer = -1; g_invOpened = false; g_invWant = false; g_rowsRead = false;
    g_noRoomLine.clear(); g_noRoomPane = 0;
}
/* MyGUI's destroy notice (see fallentab.cpp): the window or anything inside it being destroyed drops our pointers */
class Unlinker : public MyGUI::IUnlinkWidget
{
public:
    void _unlinkWidget(MyGUI::Widget* w)
    {
        if (w == 0) return;
        /* T-1167 v3 (the fade): the shades live outside the TRADE window - forgotten whether or not the window is still up */
        if (g_shadeRoot != 0 && Inside(g_shadeRoot, w)) { g_shadeRoot = 0; g_shade.clear(); g_shadeLayer.clear(); }
        for (size_t i = 0; i < g_shade.size(); ++i) if (g_shade[i] != 0 && Inside(g_shade[i], w)) g_shade[i] = 0;
        if (g_win == 0) return;
        if (Inside(g_win, w)) { ForgetWidgets(); return; }
        for (int k = 0; k < 2; ++k)
        {
            if (g_head[k] != 0 && Inside(g_head[k], w)) g_head[k] = 0;
            if (g_list[k] != 0 && Inside(g_list[k], w)) g_list[k] = 0;
            if (g_money[k] != 0 && Inside(g_money[k], w)) g_money[k] = 0;
            if (g_status[k] != 0 && Inside(g_status[k], w)) g_status[k] = 0;
            if (g_tint[k] != 0 && Inside(g_tint[k], w)) g_tint[k] = 0;
        }
        if (g_hint != 0 && Inside(g_hint, w)) g_hint = 0;
        if (g_amount != 0 && Inside(g_amount, w)) g_amount = 0;
        if (g_accept != 0 && Inside(g_accept, w)) g_accept = 0;
        if (g_cancel != 0 && Inside(g_cancel, w)) g_cancel = 0;
        if (g_grip != 0 && Inside(g_grip, w)) g_grip = 0;
        if (g_caption != 0 && Inside(g_caption, w)) g_caption = 0;
    }
};
bool UnlinkerOn()
{
    if (g_unlinkerOn) return true;
    MyGUI::WidgetManager* wm = MyGUI::WidgetManager::getInstancePtr();
    if (wm == 0) return false;
    wm->registerUnlinker(new Unlinker());   /* made once, never freed: it outlives every call WidgetManager makes to it */
    g_unlinkerOn = true;
    return true;
}
MyGUI::Widget* Mk(MyGUI::Widget* parent, const char* type, const char* skin, int l, int t, int w, int h, MyGUI::Align align, const char* name)
{
    if (parent == 0) return 0;
    return parent->createWidgetT(std::string(type), std::string(skin), MyGUI::IntCoord(l, t, w > 1 ? w : 1, h > 1 ? h : 1), align, std::string(name));
}
MyGUI::TextBox* Text(MyGUI::Widget* parent, int l, int t, int w, int h, const char* name)
{
    MyGUI::Widget* r = Mk(parent, "TextBox", "Kenshi_TextboxStandardText", l, t, w, h, MyGUI::Align::Default, name);
    MyGUI::TextBox* tb = r != 0 ? r->castType<MyGUI::TextBox>(false) : 0;
    if (tb != 0) { tb->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter); tb->setTextColour(Lit()); tb->setCaption(MyGUI::UString("")); }
    return tb;
}

/* ---- the wire ---- */
bool Send(const tradew::Msg& m)
{
    std::vector<char> b;
    if (!tradew::Encode(m, &b)) { ++g_sendFail; Say("[TRADE] " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + " NOT sent: it does not encode"); return false; }
    if (!coop::StoreSendTrade(b)) { ++g_sendFail; Say("[TRADE] " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + " NOT sent: no world-server link"); return false; }
    if (m.kind < 8) ++g_sent[m.kind];
    std::string what;
    if (m.kind == tradew::kOffer) what = " " + tradew::OfferText(m.offer);
    else if (m.kind == tradew::kAccept) what = " serials mine=" + S(m.serialMine) + " theirs=" + S(m.serialTheirs);
    else if (m.kind == tradew::kAnswer) what = std::string(" ") + tradew::AnswerName(m.code);
    else if (m.kind == tradew::kCancel) what = std::string(" ") + tradew::WhyName(m.code);
    else if (m.kind == tradew::kAsk) what = " to slot " + S(m.slot) + " mine uid " + S(m.uidFrom) + " theirs uid " + S(m.uidTo);
    Say("[TRADE] sent " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + what);
    return true;
}
void SendOwnOffer()
{
    if (g_side < 0 || g_tr.state != tradew::kStOpen) return;
    tradew::Msg m; m.kind = tradew::kOffer; m.id = g_tr.id; m.offer = g_tr.offer[g_side];
    Send(m);
}

/* ---- the panes ---- */
/* T-1167 piece 3 build 3 (owner 1638): an offer list is ONE column - each row "2 x Bolts" (swtrade::RowText). T-1167 v3: a plain ListBox
   (no column header); each row of this side carries its exact row's id as its own data - a click picks the row by it, never by its
   place on screen */
void FillRows(MyGUI::ListBox* l, const std::vector<std::string>& rows, const std::vector<unsigned>& ids, std::vector<std::string>* shown)
{
    if (l == 0) return;
    std::vector<std::string> key(rows);
    for (size_t i = 0; i < key.size() && i < ids.size(); ++i) key[i] = S(ids[i]) + "|" + rows[i];
    if (key == *shown) return;
    l->removeAllItems();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i < ids.size()) l->addItem(U(rows[i]), ids[i]);
        else l->addItem(U(rows[i]));
    }
    *shown = key;
}
void ShowAll()
{
    if (g_win == 0) return;
    std::vector<std::string> c[2];
    for (int k = 0; k < 2; ++k)
    {
        if (g_side < 0) break;
        const tradew::Offer& o = g_tr.offer[k == 0 ? g_side : 1 - g_side];
        for (size_t i = 0; i < o.items.size(); ++i) c[k].push_back(swtrade::RowText(o.items[i].name, o.items[i].count));
        std::vector<unsigned> ids;   /* v3: this side's rows carry their exact row's id (the offer and g_ex are in the same order) */
        if (k == 0 && g_ex.size() == o.items.size()) for (size_t i = 0; i < g_ex.size(); ++i) ids.push_back(g_ex[i].id);
        FillRows(g_list[k], c[k], ids, &g_offShown[k]);
    }
    const bool own = g_side >= 0 && g_tr.accepted[g_side] != 0, theirs = g_side >= 0 && g_tr.accepted[1 - g_side] != 0;
    /* T-1167 v3 (owner 2026-11-15): the green panel alone shows an accept - no ACCEPTED / waiting... words; kept: "Bo changed the offer."
       and the no-room line */
    std::string ownLine = swtrade::OwnNote(), theirLine = swtrade::TheirNote(own, theirs, g_changed, g_theirWho);
    if (Locking()) theirLine.clear();
    for (int k = 0; k < 2; ++k)
        if (g_tint[k] != 0) { const bool on = k == 0 ? own : theirs; if (g_tint[k]->getVisible() != on) g_tint[k]->setVisible(on); }
    if (!g_noRoomLine.empty()) { if (g_noRoomPane == 0) ownLine = g_noRoomLine; else theirLine = g_noRoomLine; }   /* build 3 (owner 1641): under the side without room */
    if (g_amount != 0 && g_amount->getEnabled() == Locking()) g_amount->setEnabled(!Locking());   /* no change while the lock is open */
    if (g_status[0] != 0) g_status[0]->setCaption(U(ownLine));
    if (g_status[1] != 0) g_status[1]->setCaption(U(theirLine));
    if (g_money[1] != 0) g_money[1]->setCaption(MyGUI::UString(swtrade::MoneyLine(g_side >= 0 ? (long long)g_tr.offer[1 - g_side].cats : 0).c_str()));
    if (g_hint != 0) g_hint->setCaption(MyGUI::UString(swtrade::AmountHint(g_amountCheck)));
}
/* a row of this character's items as an offer row's thing (tradewire.h Item: the identity and the engine's name; count 0) */
tradew::Item ThingOf(const coop::TradeInvRow& r)
{
    tradew::Item t(r.key, r.name, 0);
    t.company = r.company; t.material = r.material; t.colour = r.colour; t.q = r.q; t.charges = r.charges; t.unique = r.unique;
    return t;
}
/* what this character holds of exactly that thing (every stack of it) */
unsigned HeldOf(const tradew::Item& thing)
{
    unsigned long long n = 0;
    for (size_t i = 0; i < g_rows.size(); ++i) if (g_rows[i].qty > 0 && tradew::SameThing(ThingOf(g_rows[i]), thing)) n += (unsigned long long)g_rows[i].qty;
    return n > tradew::kMaxCount ? tradew::kMaxCount : (unsigned)n;
}
struct HeldNow { unsigned operator()(const tradew::Item& t) const { return HeldOf(t); } };
/* T-1167 v3: the character's objects now, each with its thing and count (g_rows / g_items) */
std::vector<tradel::ExHeld> HeldList()
{
    std::vector<tradel::ExHeld> v;
    for (size_t i = 0; i < g_rows.size() && i < g_items.size(); ++i)
    {
        tradel::ExHeld h; h.obj = g_items[i]; h.thing = ThingOf(g_rows[i]); h.qty = g_rows[i].qty > 0 ? (unsigned)g_rows[i].qty : 0u;
        v.push_back(h);
    }
    return v;
}
/* an exact row's prototype from one row of the character's items (its thing and the grade facts) */
tradel::ExRow ProtoOf(const coop::TradeInvRow& r)
{
    tradel::ExRow e; e.thing = ThingOf(r); e.type = r.type; e.level = r.level; e.model = r.model;
    return e;
}
int OwnOffer(const tradew::Offer& next, const char* why);
/* this character's items and the purse, read again (the uid looked up afresh: no pointer kept from the open is read through).
   T-1167 piece 2 fold 1 (review LOW): an offered item or money that left the character since is trimmed out of this side's offer
   and the offer re-sent (serial + 1: both accepts cleared, by the rules) - only from a read that worked (a failed read trims
   nothing); counted, the line capped (Say). */
void ReadMine()
{
    g_invAt = ::GetTickCount();
    void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
    std::vector<coop::TradeInvRow> rows; std::vector<void*> items;
    const bool itemsRead = me != 0 && coop::ItemsTradeRows(me, &rows, &items) == 1 && items.size() == rows.size();
    if (itemsRead) { g_rows.swap(rows); g_items.swap(items); }
    else { g_rows.clear(); g_items.clear(); }
    g_rowsRead = itemsRead;
    bool held = false;
    if (itemsRead)
    {   /* fold 1 (review p3b M1): a stack picked up out of the character's own window left the inventory at mouse-down - it is still held */
        coop::TradeCursor cc;
        if (coop::ItemsTradeCursorNow(me, &cc) == 1 && cc.fromMine != 0 && cc.rowRead != 0 && cc.row.qty > 0) { g_rows.push_back(cc.row); g_items.push_back(cc.obj); held = true; }
    }
    int money = 0;
    g_purse = coop::StoreMoneyRead(&money) == 1 ? (long long)money : -1;
    if (g_side < 0 || Locking()) return;   /* T-1167 piece 3: the lock lifted the offer - nothing is trimmed while it is open */
    /* T-1167 v3: every object of the exact rows re-checked (the pointer AND its thing, ItemsTradeRows' own list) - an object gone leaves
       its row, a row with none left goes; not while an item of the character's window is on the cursor (it is out of the inventory
       until it is let go - review p3b M1) */
    std::vector<tradel::ExRow> ex = g_ex;
    const bool exTrim = itemsRead && !held && tradel::ExactTrim(&ex, HeldList()) == 1;
    tradew::Offer next = g_tr.offer[g_side];
    if (exTrim) next.items = swtrade::ExactItems(ex);
    const bool money0 = tradew::TrimToHeld(&next, HeldNow(), false, g_purse) == 1;   /* the money only: the items are the exact rows' */
    if (!exTrim && !money0) return;
    ++g_trimmed;
    if (exTrim) ++g_exTrims;
    Say("[TRADE] this side's offer held more than the character now has: " + tradew::OfferText(g_tr.offer[g_side]) + " - trimmed" + (exTrim ? " (an offered object is gone or holds fewer units)" : " (the money)"));
    if (OwnOffer(next, "trimmed to what the character holds") >= 0 && exTrim) g_ex = ex;
}
/* this side's offer replaced by `next`: the serial rises (both accepts cleared) and the OFFER goes out once the trade is open */
int OwnOffer(const tradew::Offer& next, const char* why)
{
    if (g_side < 0) return -1;
    const int r = tradew::OwnChange(&g_tr, g_side, next);
    if (r == 0) return 0;
    if (r != tradew::kStepChanged) { Say("[TRADE] this side's offer NOT changed (" + std::string(why) + "): step " + S(r)); return -1; }
    g_changed = false;
    g_noRoomLine.clear();   /* build 3: any change of either offer clears the no-room line */
    Say("[TRADE] this side's offer changed (" + std::string(why) + "): " + tradew::OfferText(g_tr.offer[g_side]) + (g_tr.state == tradew::kStOpen ? std::string() : std::string(" - sent once the other game opens its window")));
    SendOwnOffer();
    ShowAll();
    return 1;
}
/* T-1167 v3: the exact rows `ex` become this side's offer (their things, graded names and counts - swtrade::ExactItems); kept only when
   the offer took them */
int ExCommit(const std::vector<tradel::ExRow>& ex, const char* why)
{
    if (g_side < 0) return -1;
    tradew::Offer next = g_tr.offer[g_side];
    next.items = swtrade::ExactItems(ex);
    const int r = OwnOffer(next, why);
    if (r >= 0) g_ex = ex;
    return r;
}
/* the money box read: checked against the purse; a good amount that differs is this side's new money offer */
void AmountRead(bool force)
{
    if (g_amount == 0 || g_side < 0 || Locking()) return;
    const std::string t = g_amount->getOnlyText().asUTF8();
    if (!force && t == g_amountText) return;
    g_amountText = t;
    unsigned cats = 0;
    g_amountCheck = tradew::AmountCheck(t, g_purse, &cats);
    if (g_amountCheck == tradew::kAmtOk && cats != g_tr.offer[g_side].cats)
    {
        tradew::Offer next = g_tr.offer[g_side]; next.cats = cats;
        OwnOffer(next, "money");
    }
    ShowAll();
}

/* T-1167 v3 (the fade): every shade taken down (the trade closed, or the TRADE window gone) - the root destroyed with its shades; the
   destroy notice forgets the pointers, and they are forgotten here too (no MyGUI left: nothing to destroy) */
void ShadeDrop()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* r = g_shadeRoot;
    if (r != 0 && gui != 0) gui->destroyWidget(r);
    g_shadeRoot = 0; g_shade.clear(); g_shadeLayer.clear(); g_shadeShown = 0; g_shadeInPack = 0; g_shadeUnplaced = 0;
}
/* invToo (build 4): the character's inventory window closes too when this trade opened it - not at the title or a world teardown,
   where the engine takes its windows down itself */
void Close(const char* why, unsigned cancelWhy, bool invToo = true)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* w = g_win;
    const unsigned int mu = g_mineUid, tu = g_theirUid;
    if (invToo && g_invOpened && mu != 0)
    {   /* fold 1 (review p3b HIGH): the engine's close destroys a cursor item of the window it closes - every close road comes here (their
           cancel, an ABORT, the trade made, the link's drop, CANCEL pressed), so an item on the cursor goes back to its square FIRST; one
           that cannot leaves the window open */
        void* me = (void*)coop::FindSpawned(mu);
        const int home = me != 0 ? coop::ItemsTradeCursorHome(me) : 0;
        if (home < 0) ErrorLog("[TRADE] the item on the cursor could NOT be put back - the character's inventory window the trade opened is left open (its close would destroy it)");
        else if (me != 0 && coop::ItemsTradeCloseInventory(me) == 1) DebugLog("[TRADE] the character's inventory window the trade opened is closed with it (uid " + S(mu) + ")"
                                                                            + (home > 0 ? std::string(" - the item on the cursor put back on its square first") : std::string()));
    }
    if (g_side >= 0 && cancelWhy != 0)
    {
        tradew::Msg m; m.kind = tradew::kCancel; m.id = g_tr.id; m.code = cancelWhy;
        Send(m);
    }
    if (w != 0)
    {
        if (gui != 0) gui->destroyWidget(w);   /* the destroy notice forgets our pointers */
        ++g_closed;
        DebugLog("[TRADE] window closed (" + std::string(why) + ") mine uid " + S(mu) + " theirs uid " + S(tu) + " trade id " + IdText(g_tr.id));
    }
    ShadeDrop();   /* T-1167 v3 (the fade): no shade outlives the trade */
    Forget();
}
void CloseGuarded(const char* why, unsigned cancelWhy, bool invToo = true) { try { Close(why, cancelWhy, invToo); } catch (...) { ++g_throws; try { ShadeDrop(); } catch (...) { ++g_throws; } Forget(); } }

/* ---- the controls' handlers (MyGUI calls them from the game's input pass, main thread) ---- */
void AcceptBody()
{
    if (g_win == 0 || g_side < 0) return;
    ++g_accepts;
    if (Locking()) { ++g_acceptRefused; Say("[TRADE] ACCEPT pressed while the lock is open - nothing changes"); return; }
    if (g_tr.state != tradew::kStOpen) { ++g_acceptRefused; Say("[TRADE] ACCEPT pressed before the other game opened its window - nothing accepted"); return; }
    if (g_amountCheck != tradew::kAmtOk) { ++g_acceptRefused; Say(std::string("[TRADE] ACCEPT pressed with the money box refused ('") + swtrade::AmountHint(g_amountCheck) + "') - nothing accepted"); return; }
    const unsigned mine = g_tr.offer[g_side].serial, theirs = g_tr.offer[1 - g_side].serial;
    tradew::Accept(&g_tr, g_side, mine, theirs);   /* shown at once; the world server's ACCEPTS is the state both windows keep */
    tradew::Msg m; m.kind = tradew::kAccept; m.id = g_tr.id; m.serialMine = mine; m.serialTheirs = theirs;
    Send(m);
    ShowAll();
}
void AcceptCatching() { try { AcceptBody(); } catch (...) { ++g_throws; } }
void OnAccept(MyGUI::Widget*) { __try { AcceptCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }
void CancelBody()
{
    if (g_win == 0) return;
    ++g_cancels;
    if (Locking())
    {   /* T-1167 piece 3: the world server decides - CANCEL aborts the lock only while it is undecided; its ABORT closes the window */
        tradew::Msg m; m.kind = tradew::kCancel; m.id = g_tr.id; m.code = tradew::kWhyPressed;
        Send(m);
        Say("[TRADE] CANCEL pressed while the lock is open - sent; the world server's decision closes the window");
        return;
    }
    Close("CANCEL pressed", tradew::kWhyPressed);
}
void CancelCatching() { try { CancelBody(); } catch (...) { ++g_throws; } }
void OnCancel(MyGUI::Widget*) { __try { CancelCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }
/* the amount box keeps digits only: anything else typed or pasted is taken out as it arrives (playerstab.cpp AmountBody); the
   check against the purse runs in the tick (AmountRead) */
void AmountBody(MyGUI::EditBox* e)
{
    if (e == 0) return;
    const std::string s = e->getOnlyText().asUTF8();
    const std::string d = moneyw::DigitsOnly(s);
    if (d != s) e->setOnlyText(MyGUI::UString(d.c_str()));
}
void AmountCatching(MyGUI::EditBox* e) { try { AmountBody(e); } catch (...) { ++g_throws; } }
void OnAmountChanged(MyGUI::EditBox* e) { __try { AmountCatching(e); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }
/* a click on a row: kept for the tick (never acted on inside the list's own event) */
/* T-1167 v3: the row's own data (its exact row's id), never its place on screen */
void PickedBody(MyGUI::ListBox* l, size_t index)
{
    if (l == 0 || index == MyGUI::ITEM_NONE || index >= l->getItemCount()) return;
    unsigned* id = l->getItemDataAt<unsigned>(index, false);
    if (id != 0 && *id != 0 && *id < 0x7FFFFFFFu) g_pickOffer = (int)*id;
}
void PickedCatching(MyGUI::ListBox* l, size_t index) { try { PickedBody(l, index); } catch (...) { ++g_throws; } }
void OnOfferPicked(MyGUI::ListBox* l, size_t index) { __try { PickedCatching(l, index); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }
void OnTheirPicked(MyGUI::ListBox* l, size_t index) { (void)l; (void)index; }
/* T-1167 v3: THE MOVE AND THE RESIZE (chat.cpp's road): a press-and-drag on the TRADE title moves the window, on the bottom-right grip
   resizes it (never under swtrade::kWinMinW x kWinMinH, never past the screen); every widget is placed again for the new size (Layout) */
void Layout();
void ViewSize(int* vw, int* vh)
{
    *vw = 1280; *vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { *vw = s.width; *vh = s.height; } }
}
void PressBody(int kind, int left, int top)
{
    if (g_win == 0) return;
    g_dragKind = kind; g_pressX = left; g_pressY = top; g_dragFrom = g_win->getCoord();
}
void DragBody(int left, int top)
{
    if (g_win == 0 || g_dragKind == 0) return;
    int vw = 0, vh = 0;
    ViewSize(&vw, &vh);
    const int dx = left - g_pressX, dy = top - g_pressY;
    if (g_dragKind == 1)
    {
        const int x = Clamp(g_dragFrom.left + dx, 0, vw - g_dragFrom.width > 0 ? vw - g_dragFrom.width : 0);
        const int y = Clamp(g_dragFrom.top + dy, 0, vh - g_dragFrom.height > 0 ? vh - g_dragFrom.height : 0);
        if (x != g_win->getLeft() || y != g_win->getTop()) g_win->setPosition(x, y);
        return;
    }
    int w = g_dragFrom.width + dx, h = g_dragFrom.height + dy;
    swtrade::ClampSize(vw - g_dragFrom.left, vh - g_dragFrom.top, &w, &h);
    if (w != g_win->getWidth() || h != g_win->getHeight()) { g_win->setSize(w, h); Layout(); }
}
void ReleaseBody()
{
    const int kind = g_dragKind;
    g_dragKind = 0;
    if (g_win == 0 || kind == 0) return;
    const MyGUI::IntCoord c = g_win->getCoord();
    if (kind == 1) ++g_moves; else ++g_resizes;
    Say("[TRADE] the window was " + std::string(kind == 1 ? "moved" : "resized") + " to x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height));
}
void MoveCatching(int what, int kind, int left, int top) { try { if (what == 0) PressBody(kind, left, top); else if (what == 1) DragBody(left, top); else ReleaseBody(); } catch (...) { ++g_throws; } }
void MoveGuarded(int what, int kind, int left, int top) { __try { MoveCatching(what, kind, left, top); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }
void OnTitlePress(MyGUI::Widget*, int left, int top, MyGUI::MouseButton b) { if (b == MyGUI::MouseButton::Left) MoveGuarded(0, 1, left, top); }
void OnGripPress(MyGUI::Widget*, int left, int top, MyGUI::MouseButton b) { if (b == MyGUI::MouseButton::Left) MoveGuarded(0, 2, left, top); }
void OnMoveDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton) { MoveGuarded(1, 0, left, top); }
void OnMoveRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton) { MoveGuarded(2, 0, 0, 0); }

/* T-1167 v3: EVERY WIDGET PLACED for the window's client size now (swtrade::PlaceAll) - at the build and after every resize */
void Layout()
{
    if (g_win == 0) return;
    MyGUI::Window* win = g_win->castType<MyGUI::Window>(false);
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) return;
    swtrade::Places p;
    const int fits = swtrade::PlaceAll(cl->getWidth(), cl->getHeight(), &p);
    for (int k = 0; k < 2; ++k)
    {
        if (g_head[k] != 0) g_head[k]->setCoord(MyGUI::IntCoord(p.head[k].l, p.head[k].t, p.head[k].w > 1 ? p.head[k].w : 1, p.head[k].h));
        if (g_list[k] != 0) g_list[k]->setCoord(MyGUI::IntCoord(p.list[k].l, p.list[k].t, p.list[k].w > 1 ? p.list[k].w : 1, p.list[k].h > 1 ? p.list[k].h : 1));
        if (g_tint[k] != 0) g_tint[k]->setCoord(MyGUI::IntCoord(p.list[k].l, p.list[k].t, p.list[k].w > 1 ? p.list[k].w : 1, p.list[k].h > 1 ? p.list[k].h : 1));
        if (g_status[k] != 0) g_status[k]->setCoord(MyGUI::IntCoord(p.status[k].l, p.status[k].t, p.status[k].w > 1 ? p.status[k].w : 1, p.status[k].h));
    }
    if (g_money[1] != 0) g_money[1]->setCoord(MyGUI::IntCoord(p.money[1].l, p.money[1].t, p.money[1].w > 1 ? p.money[1].w : 1, p.money[1].h));
    /* this player's money line: "Money: c." measured, then the digits-only box, then its hint */
    int wordW = 70;
    if (g_money[0] != 0) { wordW = g_money[0]->getTextSize().width > 0 ? g_money[0]->getTextSize().width : 70; g_money[0]->setCoord(MyGUI::IntCoord(p.money[0].l, p.money[0].t, wordW + 2, p.money[0].h)); }
    const int editX = p.money[0].l + wordW + 6, editW = Clamp(p.paneW / 3, 60, 120);
    if (g_amount != 0) g_amount->setCoord(MyGUI::IntCoord(editX, p.money[0].t - 2, editW, p.lineH + 4));
    const int hintX = editX + editW + 6;
    if (g_hint != 0) g_hint->setCoord(MyGUI::IntCoord(hintX, p.money[0].t, p.money[0].l + p.paneW - hintX > 1 ? p.money[0].l + p.paneW - hintX : 1, p.money[0].h));
    if (g_accept != 0) g_accept->setCoord(MyGUI::IntCoord(p.accept.l, p.accept.t, p.accept.w, p.accept.h));
    if (g_cancel != 0) g_cancel->setCoord(MyGUI::IntCoord(p.cancel.l, p.cancel.t, p.cancel.w, p.cancel.h));
    if (g_grip != 0) g_grip->setCoord(MyGUI::IntCoord(p.grip.l, p.grip.t, p.grip.w, p.grip.h));
    if (fits == 0) Say("[TRADE] the window's client " + S(cl->getWidth()) + "x" + S(cl->getHeight()) + " is under the layout's minimum - placed anyway");
}
/* THE WINDOW (the approved mock-up, decision 1635; T-1167 v3 mock-up 1677: about 440 x 320 at 1920 x 1080, moved by its title,
   resized by its corner grip, plain lists, the green panel of a side that accepted, no help line); 0 = not made (a widget refused) */
int Build(const std::string& ownHead, const std::string& theirHead)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    UnlinkerOn();
    int vw = 1280, vh = 720;
    ViewSize(&vw, &vh);
    int w = 0, h = 0;
    swtrade::DefaultSize(vw, vh, &w, &h);
    MyGUI::Widget* raw = 0;
    static const char* const kLayers[2] = { "Info", "Popup" };
    for (int k = 0; k < 2 && raw == 0; ++k)
    {
        raw = gui->createWidgetT(std::string("Window"), std::string("Kenshi_WindowC"), MyGUI::IntCoord((vw - w) / 2, (vh - h) / 2, w, h),
                                 MyGUI::Align::Default, std::string(kLayers[k]), std::string(kWinName));
        if (raw != 0 && raw->getLayer() == 0) { gui->destroyWidget(raw); raw = 0; }
    }
    MyGUI::Window* win = raw != 0 ? raw->castType<MyGUI::Window>(false) : 0;
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) { if (raw != 0) gui->destroyWidget(raw); return 0; }
    win->setMovable(false);   /* the skin's caption has no move tag: the mod moves it (OnTitlePress) */
    win->setCaption(MyGUI::UString(swtrade::Title()));
    MyGUI::TextBox* head[2]; MyGUI::ListBox* list[2]; MyGUI::TextBox* money[2]; MyGUI::TextBox* status[2]; MyGUI::Widget* tint[2];
    for (int k = 0; k < 2; ++k)
    {
        head[k] = Text(cl, 0, 0, 10, 10, kHeadName[k]);
        MyGUI::Widget* lw = Mk(cl, "ListBox", "Kenshi_ListBox", 0, 0, 100, 100, MyGUI::Align::Default, kListName[k]);
        list[k] = lw != 0 ? lw->castType<MyGUI::ListBox>(false) : 0;
        /* the green panel over the list (made after it, so drawn above it), see-through, never taking the mouse: hidden until that side accepts */
        tint[k] = Mk(cl, "Widget", "WhiteSkin", 0, 0, 100, 100, MyGUI::Align::Default, kTintName[k]);
        money[k] = Text(cl, 0, 0, 10, 10, kMoneyName[k]);
        status[k] = Text(cl, 0, 0, 10, 10, kStatusName[k]);
    }
    MyGUI::Widget* bAw = Mk(cl, "Button", "Kenshi_Button2", 0, 0, 100, 30, MyGUI::Align::Default, kAcceptName);
    MyGUI::Widget* bCw = Mk(cl, "Button", "Kenshi_Button2", 0, 0, 100, 30, MyGUI::Align::Default, kCancelName);
    MyGUI::Button* bA = bAw != 0 ? bAw->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* bC = bCw != 0 ? bCw->castType<MyGUI::Button>(false) : 0;
    MyGUI::Widget* grip = Mk(cl, "Widget", "WhiteSkin", 0, 0, swtrade::kGripPx, swtrade::kGripPx, MyGUI::Align::Default, kGripName);
    for (int k = 0; k < 2; ++k)
        if (head[k] == 0 || list[k] == 0 || money[k] == 0 || status[k] == 0 || tint[k] == 0) { gui->destroyWidget(raw); return 0; }
    if (bA == 0 || bC == 0 || grip == 0) { gui->destroyWidget(raw); return 0; }
    for (int k = 0; k < 2; ++k)
    {
        tint[k]->setNeedMouseFocus(false);
        tint[k]->setColour(MyGUI::Colour(0.36f, 0.62f, 0.25f, 1.0f));   /* a muted green (mock-up v3's okEdge, a little brighter for the skin) */
        tint[k]->setAlpha(0.30f);
        tint[k]->setVisible(false);
    }
    grip->setColour(MyGUI::Colour(0.49f, 0.42f, 0.32f, 1.0f));   /* the frame's colour (mock-up v3's edgeHi) */
    grip->setAlpha(0.55f);
    grip->eventMouseButtonPressed += MyGUI::newDelegate(OnGripPress);
    grip->eventMouseDrag += MyGUI::newDelegate(OnMoveDrag);
    grip->eventMouseButtonReleased += MyGUI::newDelegate(OnMoveRelease);
    MyGUI::TextBox* cap = win->getCaptionWidget();
    if (cap != 0)
    {
        cap->eventMouseButtonPressed += MyGUI::newDelegate(OnTitlePress);
        cap->eventMouseDrag += MyGUI::newDelegate(OnMoveDrag);
        cap->eventMouseButtonReleased += MyGUI::newDelegate(OnMoveRelease);
    }
    list[0]->eventListChangePosition += MyGUI::newDelegate(OnOfferPicked);
    list[1]->eventListChangePosition += MyGUI::newDelegate(OnTheirPicked);
    head[0]->setCaption(U(ownHead));
    head[1]->setCaption(U(theirHead));
    money[0]->setCaption(MyGUI::UString(swtrade::MoneyWord()));
    MyGUI::Widget* aw = Mk(cl, "EditBox", "Kenshi_EditBox", 0, 0, 80, 24, MyGUI::Align::Default, kAmountName);
    MyGUI::EditBox* a = aw != 0 ? aw->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::TextBox* hint = Text(cl, 0, 0, 10, 10, kHintName);
    if (a == 0 || hint == 0) { gui->destroyWidget(raw); return 0; }
    a->setMaxTextLength(moneyw::kAmountDigitsMax);
    a->setTextColour(Lit());
    a->setOnlyText(MyGUI::UString(""));
    a->eventEditTextChange += MyGUI::newDelegate(OnAmountChanged);
    hint->setCaption(MyGUI::UString(swtrade::AmountHint(tradew::kAmtOk)));   /* build 3 (owner 1637): no '(digits only)' hint */
    money[1]->setCaption(MyGUI::UString(swtrade::MoneyLine(0).c_str()));   /* the other side offers nothing until its OFFER arrives */
    bA->setCaption(MyGUI::UString(swtrade::AcceptWord())); bA->setTextColour(Lit());
    bC->setCaption(MyGUI::UString(swtrade::CancelWord())); bC->setTextColour(Lit());
    bA->eventMouseButtonClick += MyGUI::newDelegate(OnAccept);
    bC->eventMouseButtonClick += MyGUI::newDelegate(OnCancel);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) im->setKeyFocusWidget(a);   /* build 4: NOT modal - a modal window takes every mouse press and no drag could start in the inventory window */
    g_win = raw; g_amount = a; g_accept = bA; g_cancel = bC; g_hint = hint; g_grip = grip; g_caption = cap;
    for (int k = 0; k < 2; ++k) { g_head[k] = head[k]; g_list[k] = list[k]; g_money[k] = money[k]; g_status[k] = status[k]; g_tint[k] = tint[k]; }
    g_headText[0] = ownHead; g_headText[1] = theirHead;
    Layout();
    const MyGUI::IntCoord c = raw->getAbsoluteCoord(), lc = list[0]->getCoord();
    DebugLog("[UI] rect tradewin x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height) + " client " + S(cl->getWidth()) + "x" + S(cl->getHeight())
             + " list x=" + S(lc.left) + " y=" + S(lc.top) + " w=" + S(lc.width) + " h=" + S(lc.height) + " title-move=" + S(cap != 0 ? 1 : 0) + " grip=1 modal=0");
    if (cap == 0) ErrorLog("[TRADE] the window's title widget did not read - the window cannot be moved by its title (the grip still resizes it)");
    return 1;
}
int BuildCatching(const std::string& ownHead, const std::string& theirHead)
{
    try { return Build(ownHead, theirHead); } catch (...) { ++g_throws; return 0; }
}
int BuildGuarded(const std::string* ownHead, const std::string* theirHead)
{
    __try { return BuildCatching(*ownHead, *theirHead); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; return 0; }
}

/* T-1167 piece 3 build 4: the inventory window's screen rectangle, read through MyGUI from the engine's InventoryGUI widget. 1 read. */
int InvRectPod(void* widget, int* l, int* t, int* w, int* h)
{
    __try
    {
        const MyGUI::IntCoord c = ((MyGUI::Widget*)widget)->getAbsoluteCoord();
        *l = c.left; *t = c.top; *w = c.width; *h = c.height;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the trading character's own inventory window opened (the engine's own place, as the shop opens the player's window) and the TRADE
   window moved beside it (swtrade::BesideX: to its right, else its left, else it stays centred); g_invOpened when it was not up */
void InvBeside(void* mine)
{
    int was = 0; void* wd = 0;
    if (coop::ItemsTradeShowInventory(mine, &was, &wd) != 1) { ++g_invOpenFail; ErrorLog("[TRADE] the character's inventory window could NOT be opened - nothing can be dragged onto the offer until the player opens it"); return; }
    g_invOpened = (was == 0);
    int l = 0, t = 0, w = 0, h = 0, x = 0, vw = 0;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) vw = rm->getViewSize().width;
    const bool rect = wd != 0 && InvRectPod(wd, &l, &t, &w, &h) == 1;
    const bool placed = rect && g_win != 0 && swtrade::BesideX(l, w, g_win->getWidth(), vw, 8, &x) == 1;
    if (placed) g_win->setPosition(x, g_win->getTop());
    DebugLog("[TRADE] the character's inventory window " + std::string(was != 0 ? "was already open" : "opened") + (rect ? " at x=" + S(l) + " y=" + S(t) + " w=" + S(w) + " h=" + S(h) : std::string(" (its rectangle unread)"))
             + (placed ? " - the TRADE window moved beside it to x=" + S(x) : " - the TRADE window stays centred"));
}
/* the window for the pair, with its heads; 1 made */
int MakeFor(void* mine, void* theirs, int theirSlot)
{
    std::string myName, theirName;
    if (!coop::NameOf(mine, &myName)) myName.clear();
    if (!coop::NameOf(theirs, &theirName)) theirName.clear();
    const std::string player = theirSlot >= 0 ? coop::TeamPlayerName(theirSlot) : std::string();
    const std::string ownHead = swtrade::OwnHead(myName), theirHead = swtrade::TheirHead(theirName, player);
    if (BuildGuarded(&ownHead, &theirHead) != 1)
    {
        ++g_refusedOpen;
        Forget();
        ErrorLog("[TRADE] window NOT opened: a widget was refused (heads '" + ownHead + "' / '" + theirHead + "')");
        return 0;
    }
    g_mine = mine; g_theirs = theirs; g_theirSlot = theirSlot;
    g_ex.clear();   /* T-1167 v3: a new window starts with no exact rows */
    /* fold 1 (review p3b M2): opened only with an empty cursor (it closes every inventory window first, as the shop does) - else at the
       first tick the cursor is empty */
    if (coop::ItemsTradeCursorBusy() == 0) InvBeside(mine);   /* build 4: the character's own inventory window beside the TRADE window */
    else { g_invWant = true; DebugLog("[TRADE] the character's inventory window waits: something is on the cursor (an open closes every inventory window)"); }
    g_linkEdge = coop::StoreMoneyEdge();   /* fold 1: a later edge ends this trade (TickBody) */
    g_mineUid = coop::FindSpawnedUid(mine); g_theirUid = coop::FindSpawnedUid(theirs);
    g_theirWho = !player.empty() ? player : theirName;
    ++g_opened;
    ReadMine();
    return 1;
}

/* ---- T-1167 piece 3 build 2: THE LOCK (src/common/tradelock.h says the whole road) ----
   LOCK_NOW is kept (g_nowPend) and acted on in the tick, MAIN THREAD, only while money may move (StoreMoneyReady: no save between
   its request and its landing, no restore waiting - the lock's escrow waits for a save exactly as a money SEND does); after
   kLockWaitMs it is refused (the world server's own clock ends a lock at 10 s). The escrow (g_lock) is NOT the window: a window
   that closes (the link's edge, the world left) leaves the escrow held until the world server's decision; a load or the world's
   teardown ends the live objects (their lines and held money stay on disk for the after-reload rule, handrec.h TradeHolderAction). */
void LockRefuse(unsigned long long id, unsigned row, const std::string& why, unsigned code = tradew::kRefMissing, const std::vector<unsigned>& rows = std::vector<unsigned>())
{
    tradew::Msg r; r.kind = tradew::kLockRefused; r.id = id; r.row = row; r.code = code; r.rows = rows;
    Send(r);
    ++g_lkRefused;
    std::string rs;
    for (size_t i = 0; i < rows.size(); ++i) rs += (i ? "," : "") + S(rows[i]);
    Say("[TRADE] LOCK_REFUSED id " + IdText(id) + (code == tradew::kRefNoRoom ? " NO-ROOM rows [" + rs + "]" : " row " + (row == tradew::kNoRow ? std::string("none (money / not lockable)") : S(row)))
        + ": " + why + " (refused " + S(g_lkRefused) + ")");
}
void LockedSend()
{
    tradew::Msg r; r.kind = tradew::kLocked; r.id = g_lock.id; r.serialMine = g_lock.serial; r.cats = g_lock.cats; r.transfer = g_lock.transfer; r.bodies = g_lock.bodies;
    if (!Send(r)) return;
    ++g_lkLocked;
    Say("[TRADE] LOCKED id " + IdText(g_lock.id) + ": " + S(g_lock.bodies.size()) + " object(s) lifted and written down, " + S(g_lock.cats) + " c. held"
        + (g_lock.cats > 0 ? " under transfer " + tradew::N(g_lock.transfer) : std::string()) + " - pp.money on disk (locked " + S(g_lkLocked) + ")");
}
void LockPutBackAll(void* me, const std::vector<void*>& objs)
{
    for (size_t i = objs.size(); i-- > 0; ) coop::ItemsTradePutBack(me, objs[i]);   /* pack fold: reverse lift order - a pack back before the objects lifted out of it */
}
/* a lock that failed part-way: what was lifted goes back, its lines are closed, the lock is refused */
void LockUndo(unsigned long long id, void* me, const std::vector<void*>& objs, bool linesWritten, const std::string& why)
{
    LockPutBackAll(me, objs);
    int closed = 0;
    if (linesWritten && coop::ItemsTradeLinesClose(id, &closed) != 1) ErrorLog("[TRADE] trade " + IdText(id) + ": the lifted objects are back but their lines could not be closed - they stay held on file");
    ++g_lkFails;
    LockRefuse(id, tradew::kNoRow, why + " - " + S(objs.size()) + " lifted object(s) put back");
}
void LockTry()
{
    if (!g_nowPend) return;
    const tradew::Msg m = g_nowMsg;
    const int k = tradel::LockNowCheck(g_tr, g_side, m, tradel::PhaseOpen(g_lock.phase) ? g_lock.id : 0);
    if (k == tradel::kNowRepeat) { g_nowPend = false; if (g_lock.phase == tradel::kLkLocked) LockedSend(); return; }
    if (k != tradel::kNowOk) { g_nowPend = false; LockRefuse(m.id, tradew::kNoRow, std::string("LOCK_NOW ") + tradel::NowName(k)); return; }
    std::string why;
    if (coop::StoreMoneyReady(&why) != 1)
    {   /* saves wait as money's do: nothing is lifted between a save's request and its landing */
        if (::GetTickCount() - g_nowAt < kLockWaitMs)
        {
            if (why != g_nowWaitSaid) { g_nowWaitSaid = why; Say("[TRADE] LOCK_NOW id " + IdText(m.id) + " waits: money may not move now (" + why + ")"); }
            return;
        }
        g_nowPend = false;
        LockRefuse(m.id, tradew::kNoRow, "money could not move for " + S(kLockWaitMs / 1000) + " s (" + why + ")");
        return;
    }
    g_nowPend = false;
    void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
    std::vector<coop::TradeInvRow> rows; std::vector<void*> items;
    if (me == 0 || coop::ItemsTradeRows(me, &rows, &items) != 1 || items.size() != rows.size()) { LockRefuse(m.id, tradew::kNoRow, "this character's items could not be read"); return; }
    int money = 0;
    const long long purse = coop::StoreMoneyRead(&money) == 1 ? (long long)money : -1;
    std::vector<tradel::Stack> inv(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) { inv[i].thing = ThingOf(rows[i]); inv[i].qty = rows[i].qty > 0 ? (unsigned)rows[i].qty : 0u; inv[i].obj = items[i]; }
    const tradew::Offer o = g_tr.offer[g_side];
    std::vector<tradel::Take> takes; unsigned row = tradew::kNoRow;
    /* T-1167 v3: exactly the objects and units the rows recorded (never another stack of the same thing, never a worn one not dragged) */
    const int plan = tradel::LockPlanExact(o, g_ex, inv, purse, &takes, &row);
    if (plan != tradel::kPlanOk)
    {
        LockRefuse(m.id, row, std::string("the offer is not all there (") + tradel::PlanName(plan) + ")"
                   + (plan == tradel::kPlanMissing && row < o.items.size() ? " - '" + o.items[row].name + "' x" + S(o.items[row].count) : std::string())
                   + (plan == tradel::kPlanMoney ? " - " + S(o.cats) + " c. offered, " + S(purse) + " in the purse" : std::string()));
        return;
    }
    /* T-1167 (owner's hands-on 2026-11-17): the objects inside a carried pack are lifted FIRST - that pack may be offered too, and
       lifted before them it would carry them in its own bytes as well */
    {
        std::vector<tradel::Take> inner, rest;
        for (size_t i = 0; i < takes.size(); ++i) (coop::ItemsTradeHolderName(me, items[takes[i].stack], 0) == 2 ? inner : rest).push_back(takes[i]);
        inner.insert(inner.end(), rest.begin(), rest.end());
        takes.swap(inner);
    }
    /* 1. lift exactly those units */
    std::vector<void*> objs; std::vector<std::string> bodies; std::vector<hovrec::TradeSquare> squares;
    for (size_t i = 0; i < takes.size(); ++i)
    {
        void* ob = 0; std::string b; hovrec::TradeSquare sq;
        const int r = coop::ItemsTradeLift(me, items[takes[i].stack], (int)takes[i].units, takes[i].whole, &ob, &b, &sq);
        if (ob != 0) objs.push_back(ob);
        if (r != 1) { LockUndo(m.id, me, objs, false, "a lift of '" + o.items[takes[i].row].name + "' did not go as asked (" + S(r) + ")"); return; }
        bodies.push_back(b); squares.push_back(sq);   /* fold 1 (review p3a M2): the square it left, written into its line */
    }
    /* 1b. THE ROOM CHECK (owner 1641, mock-up 1642) - build 3: with this side's outgoing items lifted (their squares free), do ALL the
       other side's items fit this character together (items.cpp ItemsTradeRoom, shopwire.h FitList)? If not, what was lifted goes back
       and the lock is refused NO ROOM naming the rows that did not fit: the world server ABORTs both sides for no room and puts the trade
       back on its relay with both accepts cleared - both windows stay open with "No room for <rows>." under this side's pane. Nothing is
       exchanged; never a feet drop for lack of room here. */
    {
        std::vector<unsigned> noFit; std::string rw;
        const int room = coop::ItemsTradeRoom(me, g_tr.offer[1 - g_side].items, &noFit, &rw);
        if (room != 1)
        {
            LockPutBackAll(me, objs);
            if (room == 0) { ++g_lkNoRoom; LockRefuse(m.id, tradew::kNoRow, "NO ROOM for the other side's " + rw + " - " + S(objs.size()) + " lifted object(s) put back", tradew::kRefNoRoom, noFit); }
            else { ++g_lkFails; LockRefuse(m.id, tradew::kNoRow, "the room for the other side's items could not be read (" + rw + ") - " + S(objs.size()) + " lifted object(s) put back"); }
            return;
        }
    }
    /* 2. one held line per lifted object and the receiving line (build 3: also when no object leaves), one write, on disk */
    const int lw = coop::ItemsTradeLinesWrite(m.id, (int)m.slot, me, bodies, &why, squares);
    if (lw == 0) { LockUndo(m.id, me, objs, false, "the lines could not be written (" + why + ")"); return; }
    const bool linesWritten = lw == 1;
    /* 3. the money into a held row, in one pp.money write with the balance */
    unsigned long long seq = 0, transfer = 0;
    if (o.cats > 0)
    {
        moneyw::Ledger* l = coop::StoreMoneyLedger();
        if (l == 0) { LockUndo(m.id, me, objs, linesWritten, "no money ledger"); return; }
        const moneyw::Ledger before = *l;
        moneyw::HoldRow h;
        const int hr = moneyw::LedgerAddHold(l, (unsigned long long)::time(0), m.id, (int)o.cats, &h);
        if (hr != 1) { LockUndo(m.id, me, objs, linesWritten, hr == 2 ? "this trade already holds money here" : "no held money row could be made (rows full)"); return; }
        const int t = coop::StoreMoneyTake((int)o.cats);   /* the game's own take: refused when short */
        if (t != 1) { *l = before; LockUndo(m.id, me, objs, linesWritten, std::string("the game's take of the money was ") + (t == 0 ? "refused (short)" : "not made")); return; }
        if (!coop::StoreMoneyWriteNow(&seq))
        {
            if (coop::StoreMoneyAdjust((int)o.cats) == 1) { *l = before; LockUndo(m.id, me, objs, linesWritten, "pp.money could not be written - the money is back in the purse"); return; }
            /* the money could not go back: it stays out WITH its held row (the next pp.money write carries both); the world server's
               ABORT for this refusal gives it back */
            LockUndo(m.id, me, objs, linesWritten, "pp.money could not be written nor the money put back - it stays held until the ABORT");
            return;
        }
        transfer = h.transfer;
    }
    g_lock = LockState();
    g_lock.id = m.id; g_lock.phase = tradel::kLkWaitDisk; g_lock.otherSlot = (int)m.slot; g_lock.uid = g_mineUid; g_lock.gen = coop::OwnLoadGen();
    g_lock.serial = o.serial; g_lock.cats = o.cats; g_lock.transfer = transfer; g_lock.seq = seq;
    g_lock.objs = objs; g_lock.bodies = bodies; g_lock.back.assign(objs.size(), 0);
    g_lock.mine = o; g_lock.theirs = g_tr.offer[1 - g_side]; g_lock.who = g_theirWho;   /* build 3: the chat line's words */
    Say("[TRADE] trade " + IdText(m.id) + " locked here: " + tradew::OfferText(o) + " - " + S(objs.size()) + " object(s) lifted, " + S(o.cats) + " c. held"
        + (seq != 0 ? " (pp.money seq " + tradew::N(seq) + ")" : std::string()) + "; LOCKED goes once that write is on disk");
}
void PendAdd(const tradew::Msg& m)
{
    std::map<unsigned long long, Pend>::iterator it = g_pend.find(m.id);
    if (it != g_pend.end()) { ++g_lkRepeats; return; }   /* being settled: the repeat changes nothing */
    Pend p; p.m = m;
    g_pend[m.id] = p;
    Say("[TRADE] " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + ": no live lock of it here - settled from the lines and the held money on file (RecoverTick)");
}
/* the world teardown or a load: the lifted objects end with the world; their lines and held money stay on disk. Build 3: a decision this
   lock was applying stands - it is settled from the lines once a world is up (RecoverTick); every pending settle starts again from its
   lines (what the last world built or removed is that world's) */
void LockForgetWorld(const char* why)
{
    g_nowPend = false;
    for (std::map<unsigned long long, Pend>::iterator it = g_pend.begin(); it != g_pend.end(); ++it)
    { it->second.stage = 0; it->second.seq = 0; it->second.at = 0; it->second.said = false; it->second.st.clear(); it->second.eMax = 0; it->second.recSaid = false; }
    if (!tradel::PhaseOpen(g_lock.phase)) return;
    size_t out = 0;
    for (size_t i = 0; i < g_lock.back.size(); ++i) if (g_lock.back[i] == 0) ++out;
    const bool decided = g_lock.phase != tradel::kLkWaitDisk && g_lock.phase != tradel::kLkLocked;
    if (decided && g_lock.dec.id == g_lock.id && g_pend.count(g_lock.id) == 0) { Pend p; p.m = g_lock.dec; g_pend[g_lock.id] = p; }
    ErrorLog("[TRADE] trade " + IdText(g_lock.id) + " (" + tradel::PhaseName(g_lock.phase) + "): " + why + " - " + S(out) + " lifted object(s) end with this world; their held lines"
             " and held money stay on disk" + (decided ? std::string(" - its decision is settled from them once a world is up") : std::string(" until the world server's decision (the after-reload rule)")));
    g_lock = LockState();
}
/* the held money of trade `id` back in the purse with the row's removal, in one write: 1 done (*seq the write, 0 none needed - no row),
   0 not now (tried again) */
int LockRefund(unsigned long long id, unsigned long long* seq)
{
    *seq = 0;
    moneyw::Ledger* l = coop::StoreMoneyLedger();
    if (l == 0) return 0;
    const moneyw::Ledger before = *l;
    moneyw::HoldRow h;
    if (!moneyw::LedgerDropHold(l, id, &h)) return 1;
    if (coop::StoreMoneyAdjust(h.amount) != 1) { *l = before; return 0; }
    if (!coop::StoreMoneyWriteNow(seq))
    {
        if (coop::StoreMoneyAdjust(-h.amount) == 1) { *l = before; return 0; }
        ErrorLog("[TRADE] trade " + IdText(id) + ": the held " + S(h.amount) + " c. are back in the purse but pp.money could not be written nor the money taken out again - the next pp.money write carries it");
        g_refundOwed = true;
        return 1;
    }
    Say("[TRADE] trade " + IdText(id) + ": the held " + S(h.amount) + " c. given back, written with the row's removal (pp.money seq " + tradew::N(*seq) + ")");
    return 1;
}
void SendApplied(unsigned long long id)
{
    tradew::Msg a; a.kind = tradew::kApplied; a.id = id;
    if (Send(a)) ++g_lkApplied;
}
/* T-1167 piece 3 fold 1 (review p3a H1): THE APPLIED GATE (tradelock.h AppliedGate) - pp.money on disk, then THIS GAME'S OWN RECORD of the
   trading character's squad committed at or past `e` (items.cpp ItemsTradeDurable: T-889's rule), then the trade's lines closed (one
   write); 1 = APPLIED may go now. Until then nothing is closed and nothing is sent: a crash leaves the lines for the after-reload rule and
   the world server sends its decision again at the next WELCOME. */
int AppliedReady(unsigned long long id, unsigned long long moneySeq, unsigned long long e, bool* said)
{
    const int money = (!g_refundOwed && coop::StoreMoneyOnDisk(moneySeq)) ? 1 : 0;
    const int rec = money != 0 && coop::ItemsTradeDurable(id, e) == 1 ? 1 : 0;
    const int open = rec != 0 ? coop::ItemsTradeLinesOpen(id) : 0;
    int gate = tradel::AppliedGate(money, rec, open != 0 ? 1 : 0);
    if (gate == tradel::kGateWaitRecord && !*said)
    {
        *said = true;
        Say("[TRADE] trade " + IdText(id) + ": APPLIED waits for this game's own record of the trading character's squad at or past E " + tradew::N(e)
            + " (T-889's rule) - the trade's lines stay open until then");
    }
    if (gate == tradel::kGateCloseLines) { int closed = 0; if (coop::ItemsTradeLinesClose(id, &closed) == 1) gate = tradel::kGateSend; }
    return gate == tradel::kGateSend ? 1 : 0;
}
/* fold 1 (review p3a M1): one round over the other side's objects - each one still kBdTodo built through ItemsTradeBuildObj (E_i just
   before it, its placed mark written in the same call); one not made stays kBdTodo and is tried again a second later; bytes that will
   not decode can never be built: given up (kBdGiveUp) with an error line - the world server's decision stands, the trade is not held
   open for ever. *changed: something was built this round. */
void BuildRound(unsigned long long id, void* ch, const std::vector<std::string>& bodies, std::vector<int>* st, bool* changed, int* built, int* feet, int* todo, int* gaveUp)
{
    *changed = false; *built = 0; *feet = 0; *todo = 0; *gaveUp = 0;
    if (st->size() != bodies.size()) st->assign(bodies.size(), tradel::kBdTodo);
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if ((*st)[i] != tradel::kBdTodo) continue;
        int res = 0;
        (*st)[i] = coop::ItemsTradeBuildObj(id, (unsigned)i, ch, bodies[i], &res);
        if ((*st)[i] == tradel::kBdBuilt || (*st)[i] == tradel::kBdFeet) { *changed = true; ++*built; if ((*st)[i] == tradel::kBdFeet) ++*feet; }
        else if ((*st)[i] == tradel::kBdGiveUp)
        {
            ++*gaveUp;
            ErrorLog("[TRADE] trade " + IdText(id) + ": object " + S(i) + " of the other side (" + S(bodies[i].size()) + " bytes) will NEVER be built here - its bytes do not decode;"
                     " GIVEN UP (the world server's decision stands: the other side's object is gone there, nothing is given back for it)");
        }
        else ++*todo;
    }
}
/* build 3: COMMIT's objects received, after a reload or with no live lock (fold 1: per object - tradelock.h RecvObjAct on its placed mark,
   then only its square; built through the same round as the live COMMIT): 1 every object settled, 0 wait */
int RecvAfter(unsigned long long id, Pend& p)
{
    coop::TradeRecvInfo ri;
    const int f = coop::ItemsTradeRecvInfo(id, &ri);
    if (f < 0) return 0;
    if (f == 0)
    {
        if (!p.m.bodies.empty())
            ErrorLog("[TRADE] trade " + IdText(id) + " COMMITTED: no receiving line on file (the line is gone) - the other side's "
                     + S(p.m.bodies.size()) + " object(s) CANNOT be built here: the character is not known");
        return 1;
    }
    const std::string head = "[TRADE] trade " + IdText(id) + " COMMITTED, received here (line " + (ri.open ? std::string("open") : "closed E " + tradew::N(ri.e)) + "): ";
    if (!ri.open && ri.e == 0) { ErrorLog(head + "the receiving line says GIVEN BACK - nothing built (one decision per lock)"); return 1; }
    if (!ri.open && ri.frozenHolds != 0) { Say(head + "the receiving line says DONE (the APPLIED gate closed this trade here already) - nothing built"); return 1; }   /* fold 2: re-check p3f1 HIGH */
    if (ri.character == 0) { if (!p.said) { p.said = true; Say(head + "its character is not loaded here (or not ours now) - waits"); } return 0; }
    if (ri.open)
    {   /* E reserved BEFORE any build, the receiving line closed with it: COMMIT is being applied */
        const unsigned long long e = coop::OwnSeqReserve();
        int changed = 0;
        if (coop::ItemsTradeCommitWrite(id, e, &changed) == 0) return 0;
    }
    if (p.st.size() != p.m.bodies.size()) p.st.assign(p.m.bodies.size(), -1);
    std::string why, waits;
    for (size_t i = 0; i < p.st.size(); ++i)
    {
        if (p.st[i] != -1 && p.st[i] != tradel::kBdWait) continue;
        p.st[i] = coop::ItemsTradeRecvObj(id, (unsigned)i, &why);
        if (p.st[i] == tradel::kBdWait) waits += " [object " + S(i) + ": " + why + "]";
    }
    bool changed = false; int built = 0, feet = 0, todo = 0, gaveUp = 0;
    BuildRound(id, ri.character, p.m.bodies, &p.st, &changed, &built, &feet, &todo, &gaveUp);
    if (changed) { coop::OwnMarkCharDirtyNow(ri.character); p.eMax = coop::OwnSeqReserve(); }   /* the gate: a record built after these builds */
    if (changed || gaveUp > 0) Say(head + S(built) + " object(s) built" + (feet > 0 ? " (" + S(feet) + " partly at the feet)" : std::string()) + (gaveUp > 0 ? ", " + S(gaveUp) + " given up" : std::string()));
    if (!waits.empty() && !p.said) { p.said = true; Say(head + "the record cannot settle whether this state has" + waits + " - WAITS, never a guess"); }
    return tradel::BuildsDone(p.st);
}
/* build 3: every decision with no live lock here, settled from the file (stage 0 the held lines, 1 the objects received, 2 the money,
   3 APPLIED - fold 1: through the APPLIED gate); each tried at most once a second, only while money may move */
void RecoverTick()
{
    std::string why;
    for (std::map<unsigned long long, Pend>::iterator it = g_pend.begin(); it != g_pend.end(); )
    {
        Pend& p = it->second;
        const unsigned long long id = it->first;
        const bool commit = p.m.kind == tradew::kCommit;
        if (p.stage < 3)
        {
            if (coop::StoreMoneyReady(&why) != 1 || (p.at != 0 && ::GetTickCount() - p.at < 1000)) { ++it; continue; }
            p.at = ::GetTickCount();
        }
        if (p.stage == 0)
        {   /* the held lines by the decision (handrec.h TradeHolderAction): COMMITTED - a reloaded copy removed; ABORTED - given back: nothing
               when this state has it, else rebuilt into its character (its give-back mark in the same call). Fold 1: not closed here */
            int waiting = 0;
            const int s = coop::ItemsTradeSettleHeld(id, commit ? hovrec::kTdCommitted : hovrec::kTdAborted, &waiting, &p.eMax);
            if (s != 1) { if (!p.said) { p.said = true; Say("[TRADE] trade " + IdText(id) + " " + tradew::KindName(p.m.kind) + ": " + S(waiting) + " held line(s) wait (character not loaded here, or its state cannot be told)" + (s < 0 ? " - pp.handover unreadable" : "")); } ++it; continue; }
            p.stage = commit ? 1 : 2;
        }
        if (p.stage == 1)
        {
            if (RecvAfter(id, p) != 1) { ++it; continue; }
            p.stage = 2;
        }
        if (p.stage == 2)
        {
            unsigned long long seq = 0;
            const int r = commit ? coop::MoneyTradeSettle(id, p.m.inbox, (int)p.m.cats, p.m.slot, &seq) : LockRefund(id, &seq);
            if (r != 1) { ++it; continue; }
            p.seq = seq; p.stage = 3;
        }
        if (AppliedReady(id, p.seq, p.eMax, &p.recSaid) != 1) { ++it; continue; }
        SendApplied(id);
        ++g_lkRecovered;
        Say("[TRADE] trade " + IdText(id) + " " + tradew::KindName(p.m.kind) + " settled here with no live lock (lines, " + (commit ? "objects received, " : "") + "money, this game's record) - APPLIED (recovered " + S(g_lkRecovered) + ")");
        g_pend.erase(it++);
    }
}
/* build 3 (owner 1641): a lock ABORTED for no room - the window stays open, both accepts cleared, the line under the side without room */
void NoRoomShow(const tradew::Msg& m)
{
    g_readyId = 0; g_tr.accepted[0] = 0; g_tr.accepted[1] = 0; g_changed = false;
    const bool mine = g_side >= 0 && (int)m.slot == g_tr.slot[g_side];
    g_noRoomPane = mine ? 0 : 1;
    g_noRoomLine = swtrade::NoRoomLine(mine ? g_tr.offer[1 - g_side] : g_tr.offer[g_side], m.rows);
    ++g_lkNoRoomShown;
    Say("[TRADE] trade " + IdText(m.id) + " ABORTED for no room on " + (mine ? std::string("THIS side") : std::string("the other side")) + ": '" + g_noRoomLine + "' under "
        + (mine ? "this player's" : "the other player's") + " pane; the window stays open, both accepts cleared, nothing exchanged");
    ShowAll();
}
/* THE GIVE-BACK (ABORT): each lifted object into the character (else at its feet) - fold 1: E_b reserved just before and the give-back
   marks written in the same call -, then the held money refunded (one write); the lines close and APPLIED goes through the gate */
void GiveBackStep()
{
    g_lock.backAt = ::GetTickCount();
    void* me = g_lock.uid != 0 ? (void*)coop::FindSpawned(g_lock.uid) : 0;
    if (me == 0) { if (!g_lock.waitSaid) { g_lock.waitSaid = true; Say("[TRADE] trade " + IdText(g_lock.id) + ": the give-back waits for the character to be loaded here"); } return; }
    bool all = true; unsigned long long e = 0; std::vector<unsigned> now;
    for (size_t i = g_lock.objs.size(); i-- > 0; )   /* pack fold: reverse lift order - a pack back before the objects lifted out of it */
    {
        if (g_lock.back[i] != 0) continue;
        if (e == 0) e = coop::OwnSeqReserve();   /* E_b, before the first put-back of this round */
        const int r = coop::ItemsTradePutBack(me, g_lock.objs[i]);
        if (r != 0) { g_lock.back[i] = r; now.push_back((unsigned)i); } else all = false;
    }
    if (!now.empty()) { coop::ItemsTradeBackMark(g_lock.id, now, e); coop::OwnMarkCharDirtyNow(me); if (g_lock.eBack < e) g_lock.eBack = e; }
    if (!all) return;   /* tried again in a second; the lines stay held meanwhile */
    unsigned long long seq = 0;
    if (LockRefund(g_lock.id, &seq) != 1) return;
    g_lock.seq = seq; g_lock.phase = tradel::kLkBackWaitDisk;
    Say("[TRADE] trade " + IdText(g_lock.id) + " given back: " + S(g_lock.objs.size()) + " object(s) back in the character (give-back marks, E " + tradew::N(g_lock.eBack)
        + "); the lines close and APPLIED goes once pp.money and this game's record at or past that E are on disk");
}
void LockTick()
{
    if (g_refundOwed) { unsigned long long s = 0; if (coop::StoreMoneyWriteNow(&s)) { g_refundOwed = false; if (g_lock.phase == tradel::kLkBackWaitDisk) g_lock.seq = s; } }
    if (tradel::PhaseOpen(g_lock.phase) && g_lock.gen != coop::OwnLoadGen()) LockForgetWorld("a load since the lift");
    LockTry();
    if (g_lock.phase == tradel::kLkWaitDisk && coop::StoreMoneyOnDisk(g_lock.seq)) { g_lock.phase = tradel::kLkLocked; LockedSend(); }
    std::string why;
    if (g_lock.phase == tradel::kLkBackPending && coop::StoreMoneyReady(&why) == 1 && ::GetTickCount() - g_lock.backAt >= 1000) GiveBackStep();
    if (g_lock.phase == tradel::kLkBackWaitDisk && AppliedReady(g_lock.id, g_lock.seq, g_lock.eBack, &g_lock.recSaid) == 1)
    {
        g_lock.phase = tradel::kLkGivenBack;
        g_lock.objs.clear(); g_lock.back.clear(); g_lock.bodies.clear();
        SendApplied(g_lock.id);
    }
    /* build 3: COMMIT APPLIED (design section 4) - the receiving line closed with E reserved BEFORE the build; then this side's lifted
       objects destroyed (the other side's now) and theirs built into the character - fold 1: each object on its own (E_i, its placed mark
       in the same call), one not made tried again a second later, APPLIED only once every object is settled; then the money (their cats
       in, the held row out) in ONE pp.money write; then THE APPLIED GATE (pp.money on disk, this game's record at or past the last E, the
       lines closed); then the chat line. Each step once (linesDone / destroyed / built). */
    if (g_lock.phase == tradel::kLkCommitPending && coop::StoreMoneyReady(&why) == 1 && ::GetTickCount() - g_lock.backAt >= 1000)
    {
        g_lock.backAt = ::GetTickCount();
        void* me = g_lock.uid != 0 ? (void*)coop::FindSpawned(g_lock.uid) : 0;
        if (me == 0) { if (!g_lock.waitSaid) { g_lock.waitSaid = true; Say("[TRADE] trade " + IdText(g_lock.id) + ": COMMIT waits for the character to be loaded here"); } }
        else
        {
            if (!g_lock.linesDone)
            {
                const unsigned long long e = coop::OwnSeqReserve();
                int changed = 0;
                const int w = coop::ItemsTradeCommitWrite(g_lock.id, e, &changed);
                if (w != 0)
                {
                    g_lock.linesDone = true; g_lock.e = e; if (g_lock.eMax < e) g_lock.eMax = e;
                    if (w < 0) ErrorLog("[TRADE] trade " + IdText(g_lock.id) + ": COMMIT found no open receiving line on file - applied without its durable mark");
                }
            }
            if (g_lock.linesDone && !g_lock.destroyed)
            {   /* the lifted objects were out of the inventory since the lock: every record at or past E lacks them */
                int destroyed = 0;
                for (size_t i = 0; i < g_lock.objs.size(); ++i) destroyed += coop::ItemsTradeDestroy(g_lock.objs[i]);
                g_lock.objs.clear(); g_lock.back.clear();
                g_lock.destroyed = true;
                coop::OwnMarkCharDirtyNow(me);   /* write-now: the gate waits for a record of this squad at or past E */
                Say("[TRADE] trade " + IdText(g_lock.id) + " COMMIT: receiving line closed (E " + tradew::N(g_lock.e) + "), " + S(destroyed) + " lifted object(s) destroyed (the other side's now)");
            }
            if (g_lock.linesDone && !g_lock.built)
            {
                bool changed = false; int built = 0, feet = 0, todo = 0, gaveUp = 0;
                BuildRound(g_lock.id, me, g_lock.dec.bodies, &g_lock.bst, &changed, &built, &feet, &todo, &gaveUp);
                if (changed) { coop::OwnMarkCharDirtyNow(me); g_lock.eMax = coop::OwnSeqReserve(); }   /* the gate: a record built after these builds */
                if (changed || gaveUp > 0 || (todo > 0 && !g_lock.buildSaid))
                {
                    if (todo > 0) g_lock.buildSaid = true;
                    Say("[TRADE] trade " + IdText(g_lock.id) + " COMMIT: " + S(built) + " object(s) of theirs built this round" + (feet > 0 ? " (" + S(feet) + " partly at the feet)" : std::string())
                        + (gaveUp > 0 ? ", " + S(gaveUp) + " given up" : std::string()) + (todo > 0 ? ", " + S(todo) + " not made - tried again in a second" : std::string()));
                }
                if (tradel::BuildsDone(g_lock.bst)) g_lock.built = true;
            }
            unsigned long long seq = 0;
            if (g_lock.built && coop::MoneyTradeSettle(g_lock.id, g_lock.dec.inbox, (int)g_lock.dec.cats, g_lock.dec.slot, &seq) == 1)
            {
                g_lock.seq = seq; g_lock.phase = tradel::kLkCommitWaitDisk;
            }
        }
    }
    if (g_lock.phase == tradel::kLkCommitWaitDisk && AppliedReady(g_lock.id, g_lock.seq, g_lock.eMax, &g_lock.recSaid) == 1)
    {
        g_lock.phase = tradel::kLkCommitted;
        SendApplied(g_lock.id);
        ++g_lkCommitted;
        const std::string who = g_lock.who.empty() ? std::string("the other player") : g_lock.who;
        const std::string line = swtrade::TradedLine(g_lock.mine, who, g_lock.theirs);
        coop::ChatMoneyLine(g_lock.otherSlot, who, line);   /* owner 1635: the chat line, each side's own view */
        Say("[TRADE] trade " + IdText(g_lock.id) + " COMMITTED and applied here - this game's record holds it, the lines closed, APPLIED sent; chat: '" + line + "' (committed " + S(g_lkCommitted) + ")");
        g_lock.bodies.clear(); g_lock.dec.bodies.clear();
        if (g_side >= 0 && g_tr.id == g_lock.id) CloseGuarded("the trade was made", 0);
    }
    RecoverTick();   /* build 3: decisions with no live lock here */
}
void LockArrive(const tradew::Msg& m)
{
    ++g_lkRecv[m.kind < 14 ? m.kind : 0];
    if (m.kind == tradew::kLockNow)
    {
        const int k = tradel::LockNowCheck(g_tr, g_side, m, tradel::PhaseOpen(g_lock.phase) ? g_lock.id : 0);
        Say("[TRADE] LOCK_NOW id " + IdText(m.id) + " serials yours=" + S(m.serialMine) + " theirs=" + S(m.serialTheirs) + " cats " + S(m.cats) + ": " + tradel::NowName(k));
        if (k == tradel::kNowRepeat) { ++g_lkRepeats; if (g_lock.phase == tradel::kLkLocked) LockedSend(); return; }
        if (k != tradel::kNowOk) { LockRefuse(m.id, tradew::kNoRow, std::string("LOCK_NOW ") + tradel::NowName(k)); return; }
        if (g_nowPend && g_nowMsg.id == m.id) { ++g_lkRepeats; return; }
        g_nowPend = true; g_nowMsg = m; g_nowAt = ::GetTickCount(); g_nowWaitSaid.clear();
        LockTry();
        ShowAll();
        return;
    }
    if (m.kind == tradew::kAbort)
    {
        if (g_nowPend && g_nowMsg.id == m.id) g_nowPend = false;
        const int a = tradel::AbortStep(g_lock.id, g_lock.phase, m.id);
        Say("[TRADE] ABORT id " + IdText(m.id) + " (" + tradew::AbortName(m.code) + ", slot " + S(m.slot) + ", row " + S(m.row) + "): "
            + (a == tradel::kAbGive ? "the escrow goes back" : a == tradel::kAbAgain ? "already given back - nothing changes" : "no live lock here"));
        if (a == tradel::kAbGive) { ++g_lkAborted; g_lock.phase = tradel::kLkBackPending; g_lock.backAt = 0; g_lock.waitSaid = false; g_lock.dec = m; }
        else if (a == tradel::kAbAgain) { ++g_lkRepeats; if (g_lock.phase == tradel::kLkGivenBack) SendApplied(m.id); }
        else PendAdd(m);   /* build 3: settled from the file (held lines, the receiving line, the held money) */
        if (g_side >= 0 && g_tr.id == m.id)
        {
            if (m.code == tradew::kAbNoRoom) NoRoomShow(m);   /* build 3 (owner 1641): the window stays open */
            else CloseGuarded("the world server aborted the trade", 0);   /* PIECE 4: the player's line */
        }
        return;
    }
    if (m.kind == tradew::kCommit)
    {   /* build 3: COMMIT applied (LockTick) - or, with no live lock here, settled from the file (RecoverTick) */
        if (g_nowPend && g_nowMsg.id == m.id) g_nowPend = false;
        const int c = tradel::CommitStep(g_lock.id, g_lock.phase, m.id);
        Say("[TRADE] COMMIT id " + IdText(m.id) + ": " + S(m.bodies.size()) + " object(s) and " + S(m.cats) + " c. (inbox " + tradew::N(m.inbox) + ") from the other side - "
            + (c == tradel::kCmApply ? "applied now" : c == tradel::kCmAgain ? "already applied - nothing changes" : "no live lock here"));
        if (c == tradel::kCmApply) { g_lock.phase = tradel::kLkCommitPending; g_lock.dec = m; g_lock.backAt = 0; g_lock.waitSaid = false; }
        else if (c == tradel::kCmAgain) { ++g_lkRepeats; if (g_lock.phase == tradel::kLkCommitted) SendApplied(m.id); }
        else PendAdd(m);
        return;
    }
    Say(std::string("[TRADE] ") + tradew::KindName(m.kind) + " id " + IdText(m.id) + " goes up only - ignored");
}

/* ---- what arrived (the tick) ---- */
void AskedArrive(const tradew::Msg& m)
{
    tradew::Msg a; a.kind = tradew::kAnswer; a.id = m.id;
    if (g_win != 0 || g_side >= 0) { a.code = tradew::kAnsBusy; Send(a); Say("[TRADE] ASKED id " + IdText(m.id) + " from slot " + S(m.slot) + ": this game's window is already up - answered busy"); return; }
    void* mine = 0; void* theirs = 0;
    const int ans = coop::PolicyTradeAsked(m.uidFrom, m.uidTo, (int)m.slot, &mine, &theirs);
    if (ans != tradew::kAnsOpen) { a.code = (unsigned)ans; Send(a); Say("[TRADE] ASKED id " + IdText(m.id) + " from slot " + S(m.slot) + " (their uid " + S(m.uidFrom) + ", mine " + S(m.uidTo) + "): answered " + tradew::AnswerName(a.code)); return; }
    if (MakeFor(mine, theirs, (int)m.slot) != 1) { a.code = tradew::kAnsGone; Send(a); return; }
    g_tr = tradew::Trade(); g_tr.id = m.id; g_tr.state = tradew::kStOpen; g_side = 1;
    g_tr.slot[0] = (int)m.slot; g_tr.slot[1] = coop::MySlotForWire(); g_tr.uid[0] = m.uidFrom; g_tr.uid[1] = m.uidTo;
    a.code = tradew::kAnsOpen;
    Send(a);
    DebugLog("[TRADE] window opened (asked by slot " + S(m.slot) + "): '" + g_headText[0] + "' / '" + g_headText[1] + "' mine uid " + S(g_mineUid) + " theirs uid " + S(g_theirUid) + " trade id " + IdText(m.id));
    ShowAll();
}
void ActOn(const tradew::Msg& m)
{
    if (m.kind < 8) ++g_recv[m.kind];
    if (m.kind == tradew::kAsked) { AskedArrive(m); return; }
    if (m.kind >= tradew::kLockNow) { LockArrive(m); return; }   /* T-1167 piece 3: the lock is matched by its own id (the window may be gone) */
    if (g_side < 0 || m.id != g_tr.id) { ++g_otherId; Say("[TRADE] " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + " is not this game's trade (" + (g_side < 0 ? std::string("none open") : "id " + IdText(g_tr.id)) + ") - ignored"); return; }
    const int r = tradew::GameApply(&g_tr, g_side, m);
    switch (r)
    {
    case tradew::kArrOpened:
        Say("[TRADE] ANSWER open id " + IdText(m.id) + ": the other game opened its window");
        if (g_tr.offer[g_side].serial > 0) SendOwnOffer();   /* what this side picked while it was asked */
        break;
    case tradew::kArrRefused:
        Say("[TRADE] ANSWER " + std::string(tradew::AnswerName(m.code)) + " id " + IdText(m.id) + ": the other game did not open its window - this one closes");
        if (m.code == tradew::kAnsRange) coop::StoreShowPlayerLine(swtrade::OutOfRange());
        CloseGuarded("the other game refused the trade", 0);
        return;
    case tradew::kArrTheirOffer:
        g_changed = true;   /* "Bo changed the offer." - both accepts were cleared */
        g_noRoomLine.clear();   /* build 3: any change of either offer clears the no-room line */
        Say("[TRADE] OFFER id " + IdText(m.id) + " from the other side: " + tradew::OfferText(g_tr.offer[1 - g_side]) + " - both accepts cleared");
        break;
    case tradew::kArrAccepts:
        if (g_tr.accepted[0] != 0 || g_tr.accepted[1] != 0) g_changed = false;
        Say("[TRADE] ACCEPTS id " + IdText(m.id) + ": serials yours=" + S(m.serialMine) + " theirs=" + S(m.serialTheirs) + " you=" + S(m.you) + " they=" + S(m.they) + " ready=" + S(m.ready));
        if (m.ready != 0)
        {   /* PIECE 3 hooks the lock here: both accepted the same serials (the world server said so) */
            ++g_ready;
            g_readyId = g_tr.id;   /* piece 3: no change from READY on - LOCK_NOW follows */
            g_noRoomLine.clear();
            DebugLog("[TRADE] both sides accepted the same offers (serials " + S(g_tr.offer[0].serial) + "/" + S(g_tr.offer[1].serial) + ") - READY for the lock; piece 2 moves nothing");
        }
        break;
    case tradew::kArrAcceptsOld:
        Say("[TRADE] ACCEPTS id " + IdText(m.id) + " names older serials (" + S(m.serialMine) + "/" + S(m.serialTheirs) + ") - a newer state follows");
        break;
    case tradew::kArrCancelled:
        Say("[TRADE] CANCEL id " + IdText(m.id) + " from the other side (" + tradew::WhyName(m.code) + ")");
        CloseGuarded("the other side cancelled", 0);   /* PIECE 4: the cancel's line for the player */
        return;
    default:
        Say("[TRADE] " + std::string(tradew::KindName(m.kind)) + " id " + IdText(m.id) + " changed nothing");
        break;
    }
    ShowAll();
}
void Drain(bool inWorld)
{
    while (!g_inbox.empty())
    {
        std::vector<char> b; b.swap(g_inbox.front()); g_inbox.pop_front();
        tradew::Msg m;
        if (!tradew::Decode(b.empty() ? 0 : &b[0], b.size(), &m)) { ++g_malformed; Say("[TRADE] a malformed TRADE (" + S(b.size()) + " bytes) - ignored"); continue; }
        if (!inWorld && m.kind == tradew::kAsked)
        {
            ++g_recv[m.kind];
            tradew::Msg a; a.kind = tradew::kAnswer; a.id = m.id; a.code = tradew::kAnsGone; Send(a);
            continue;
        }
        if (!inWorld && (m.kind == tradew::kCommit || m.kind == tradew::kAbort))
        {   /* build 3: a decision that came at the title stands - kept, settled from the lines once a world is up (RecoverTick) */
            ++g_lkRecv[m.kind];
            PendAdd(m);
            continue;
        }
        if (!inWorld) { if (m.kind < 8) ++g_recv[m.kind]; continue; }
        ActOn(m);
    }
}
/* T-1167 piece 3 build 4: THE ONE OFFER-ADD (the person's let-go and the `tradetest drop` lever through items.cpp's hook on 0x712AD0;
   T-1167 v3: also the right-click through the hook on 0x713E50). T-1167 v3 (owner 1677): THE EXACT OBJECT - object `obj` holding `qty`
   units now; `units` asked, whole: the whole object (tradelock.h ExactAdd: an object already wholly offered adds nothing - logged; a
   one-unit add never past the object's count). What the character holds is read again first. 1 changed, 0 the same, -1 refused. */
int OfferAdd(const void* obj, const coop::TradeInvRow& row, unsigned qty, unsigned units, bool whole, const char* why)
{
    ReadMine();
    if (g_side < 0 || Locking()) return -1;
    if (!g_rowsRead)
    {   /* fold 1 (review p3b L1): a read that failed is not "held 0" - the add is refused, the offer left as it is */
        Say("[TRADE] an item NOT offered (" + std::string(why) + "): " + row.name + " - this character's items could not be read now");
        return -1;
    }
    unsigned heldQty = 0;
    if (tradel::ExHeldQty(HeldList(), obj, ThingOf(row), &heldQty) != 1)
    {   /* T-1167 v3 fold 1 (review LOW-3): only an object the character's own list names, with the count that list reads */
        Say("[TRADE] an item NOT offered (" + std::string(why) + "): " + row.name + " - that object is not among this character's items now");
        return -1;
    }
    qty = heldQty;
    std::vector<tradel::ExRow> ex = g_ex;
    const int r = tradel::ExactAdd(&ex, ProtoOf(row), obj, qty, units, whole, &g_exNextId);
    if (r != 1)
    {
        if (r == 0) ++g_exSame;
        Say("[TRADE] an item NOT offered (" + std::string(why) + "): " + row.name + (whole ? " (the whole object, " + S(qty) + ")" : " x" + S(units))
            + (r == 0 ? " - that object is offered already (all of it): nothing added" : " - refused: too many rows, or a key or name too long"));
        return r == 0 ? 0 : -1;
    }
    const int c = ExCommit(ex, why);
    if (c == 1)
    {   /* T-1167 (owner's hands-on 2026-11-17): an object offered from a pack the character carries - the pack named */
        std::string pk;
        void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
        if (me != 0 && coop::ItemsTradeHolderName(me, obj, &pk) == 2) Say("[TRADE] " + row.name + " is offered from the pack '" + pk + "' the character carries - the exchange takes it from there");
    }
    return c;
}
/* the let-go over no inventory window (items.cpp detour_mouseReleaseNoWindow): swtrade::DropDecide on the window, the release point
   against this player's offer pane (the engine's own mouse floats, truncated as 0x712AD0 truncates them), the cursor item's source
   window and the lock. 1 taken (the engine's release must not run), 0 the engine's. */
int DropBody(void* mouse)
{
    if (g_win == 0 || g_side < 0 || g_list[0] == 0) return 0;
    void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
    coop::TradeCursor c;
    if (me == 0 || coop::ItemsTradeCursor(mouse, me, &c) != 1 || c.mouseRead == 0) { ++g_dropEngine; return 0; }
    const MyGUI::IntCoord r = g_list[0]->getAbsoluteCoord(), wr = g_win->getAbsoluteCoord();
    const long long mx = (long long)c.mx, my = (long long)c.my;
    const bool inPane = swtrade::InRect(mx, my, r.left, r.top, r.width, r.height);
    const bool overWin = swtrade::InRect(mx, my, wr.left, wr.top, wr.width, wr.height);   /* fold 1: anything let go over the TRADE window goes back */
    const bool locking = Locking();
    const int d = swtrade::DropDecide(true, overWin, inPane, c.fromMine != 0, c.rowRead != 0, locking);
    if (d == swtrade::kDropEngine) { ++g_dropEngine; return 0; }
    const unsigned units = swtrade::DropUnits(c.row.qty);
    std::vector<tradel::ExHeld> before;   /* T-1167 v3: the character's objects before the put-back (the cursor item is out of them) */
    bool beforeRead = false;              /* fold 1 (review LOW-2): that read worked - else nothing is offered */
    if (d == swtrade::kDropOffer) { ReadMine(); beforeRead = g_rowsRead; before = HeldList(); if (!before.empty() && c.obj != 0 && before.back().obj == c.obj) before.pop_back(); }
    if (coop::ItemsTradeCursorReturn(mouse) != 1)
    {   /* nothing could put it back: the engine's own release runs (it puts it back or drops it), never a lost item */
        ++g_dropNoReturn;
        Say("[TRADE] an item let go over this side's offer could not be put back by the mod - the engine's own release runs");
        return 0;
    }
    if (d == swtrade::kDropBack)
    {
        ++g_dropBack;
        Say("[TRADE] an item let go over the TRADE window at x=" + S(mx) + " y=" + S(my) + " was put back where it came from, nothing offered - "
            + (!inPane ? std::string("not over this player's offer") : c.fromMine == 0 ? std::string("not from the trading character's own window or a pack it carries")
               : locking ? std::string("the lock is open") : std::string("its identity did not read")));
        return 1;
    }
    ++g_dropOffered;
    Say("[TRADE] dragged onto this side's offer at x=" + S(mx) + " y=" + S(my) + ": " + c.row.name + " x" + S(units) + " - put back on its square");
    /* T-1167 v3: which object it was - the object itself back (a whole drag), else the stack the one-unit copy merged back onto */
    ReadMine();
    unsigned qtyNow = 0; int whole = 0;
    const void* obj = tradel::ExReturned(before, beforeRead, HeldList(), c.obj, ThingOf(c.row), units, &qtyNow, &whole);
    if (!beforeRead) Say("[TRADE] the dragged " + c.row.name + " - this character's items could not be read before the put-back - nothing offered");
    else if (obj == 0) Say("[TRADE] the dragged " + c.row.name + " was not found back in the character after the put-back - nothing offered");
    else OfferAdd(obj, c.row, qtyNow, units, whole != 0, "a drag from the character's inventory");
    ShowAll();
    return 1;
}
int DropCatching(void* mouse) { try { return DropBody(mouse); } catch (...) { ++g_throws; return 0; } }
/* T-1167 v3 (owner 1678): THE RIGHT-CLICK - a right release over the trading character's own inventory window while the TRADE window is
   open: the item under the mouse offered through the same exact add as a drag (plain: one unit; Shift or Ctrl: the whole stack); the
   engine's own right-click (0x713E50: a move to another open window, or with none open the use of some items - a map read and two other
   kinds) does not run then. Anything else: the engine's, unchanged. 1 taken. */
int RightBody(void* gui)
{
    if (g_win == 0 || g_side < 0) return 0;
    void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
    if (me == 0) return 0;
    void* obj = 0; coop::TradeInvRow row; int rowRead = 0;
    const int f = coop::ItemsTradeRightItem(gui, me, &obj, &row, &rowRead);
    int ks = 0, kc = 0;
    coop::ItemsTradeKeys(&ks, &kc);   /* fold 1 (review LOW-4): the keys as the game saw them at the click, not the live key state */
    const bool shift = ks != 0, ctrl = kc != 0;
    bool whole = false;
    const int d = swtrade::RightDecide(true, f != 0, f == 1, rowRead != 0, Locking(), shift, ctrl, &whole);
    if (d == swtrade::kRightEngine) return 0;
    if (d == swtrade::kRightTaken)
    {
        ++g_rightTaken;
        Say("[TRADE] a right-click on an item of the trading character's window - nothing offered (" + std::string(Locking() ? "the lock is open" : "the item did not read") + "); the engine's right-click does not run");
        return 1;
    }
    ++g_rightOffered;
    const unsigned qty = row.qty > 0 ? (unsigned)row.qty : 1u;
    Say("[TRADE] right-click on " + row.name + " (" + S(qty) + " in the stack)" + (whole ? std::string(" with ") + (shift ? "Shift" : "Ctrl") + ": the whole stack" : std::string(": one unit")));
    OfferAdd(obj, row, qty, whole ? qty : 1u, whole, "a right-click in the character's inventory");
    ShowAll();
    return 1;
}
int RightCatching(void* gui) { try { return RightBody(gui); } catch (...) { ++g_throws; return 0; } }
/* T-1167 v3 (the fade): the MyGUI layer of the engine's window widget `w` (its name) and whether the window shows. 1 read. */
int LayerOfPod(void* w, char* name, int cap, int* visible)
{
    name[0] = 0; *visible = 0;
    __try
    {
        const MyGUI::Widget* mw = (const MyGUI::Widget*)w;
        *visible = mw->getInheritedVisible() ? 1 : 0;
        const MyGUI::ILayer* lay = mw->getLayer();
        if (lay == 0) return 0;
        const std::string& nm = lay->getName();
        if (nm.empty() || nm.size() + 1 > (size_t)cap) return 0;
        std::memcpy(name, nm.c_str(), nm.size() + 1);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { name[0] = 0; *visible = 0; return 0; }
}
void ShadeHide()
{
    if (g_shadeRoot != 0 && g_shadeRoot->getVisible()) g_shadeRoot->setVisible(false);
    g_shadeShown = 0; g_shadeInPack = 0;
}
/* T-1167 v3 (mock-up v3 "after a test", measured in the 2026-11-18 hands-on; owner's hands-on 2026-11-18, F1214): THE FADE - a see-through dark shade over
   each object of this side's offer where an inventory window shows it: the trading character's own window, or the own window of a
   pack it carries (items.cpp ItemsTradeShadeBox: the engine's own section table, grid corner and slot size). Placed again every tick,
   so it follows the window's move, the item's move and every change of the offer; an object not shown now (no window, a worn slot, on
   the cursor, lifted by the lock) has none; a row taken back or the trade closed (ShadeDrop) takes it away. The shades are children of
   one see-through root made on the inventory window's own MyGUI layer and lifted to the top of that layer whenever anything else is on
   top there (a click on the window lifts the window: the next tick puts the shades back over it). Nothing of it takes the mouse: a
   press goes through to the item under it. Look: swtrade::kShadeAlpha (mock-up v3's faded item). */
void ShadeTick()
{
    std::vector<const void*> objs;   /* each offered object once, in the offer's order */
    for (size_t i = 0; i < g_ex.size(); ++i)
        for (size_t k = 0; k < g_ex[i].objs.size(); ++k)
        {
            const void* o = g_ex[i].objs[k].obj;
            if (o == 0 || g_ex[i].objs[k].units == 0) continue;
            bool seen = false;
            for (size_t j = 0; j < objs.size() && !seen; ++j) seen = (objs[j] == o);
            if (!seen) objs.push_back(o);
        }
    for (size_t i = 0; i < g_shade.size(); )   /* a shade MyGUI destroyed (the destroy notice set it to 0) is dropped */
        if (g_shade[i] == 0) g_shade.erase(g_shade.begin() + i); else ++i;
    void* me = (!objs.empty() && g_mineUid != 0) ? (void*)coop::FindSpawned(g_mineUid) : 0;
    std::vector<MyGUI::IntCoord> boxes;
    void* win = 0; long long inPack = 0, unplaced = 0;
    for (size_t i = 0; i < objs.size(); ++i)
    {
        int l = 0, t = 0, w = 0, h = 0, p = 0; void* ww = 0;
        if (me == 0 || boxes.size() >= 64 || coop::ItemsTradeShadeBox(me, objs[i], &l, &t, &w, &h, &ww, &p) != 1) { ++unplaced; continue; }
        boxes.push_back(MyGUI::IntCoord(l, t, w, h));
        inPack += p;
        if (win == 0) win = ww;
    }
    g_shadeUnplaced = unplaced;
    if (boxes.empty()) { ShadeHide(); return; }
    char layer[64]; int vis = 0;
    if (LayerOfPod(win, layer, (int)sizeof layer, &vis) != 1 || vis == 0) { ShadeHide(); return; }
    if (g_shadeRoot != 0 && g_shadeLayer != layer) ShadeDrop();   /* the window is on another layer now: made again there */
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    int vw = 0, vh = 0;
    ViewSize(&vw, &vh);
    if (g_shadeRoot == 0)
    {
        MyGUI::Widget* r = gui->createWidgetT(std::string("Widget"), std::string("PanelEmpty"), MyGUI::IntCoord(0, 0, vw, vh), MyGUI::Align::Default, std::string(layer), std::string(kShadeRootName));
        if (r != 0 && r->getLayer() == 0) { gui->destroyWidget(r); r = 0; }
        if (r == 0) { if (++g_shadeLayerFail == 1) ErrorLog("[TRADE] the offered items' shade could NOT be made on the inventory window's layer '" + std::string(layer) + "' - no fade"); return; }
        r->setNeedMouseFocus(false); r->setInheritsPick(false); r->setNeedKeyFocus(false);
        g_shadeRoot = r; g_shadeLayer = layer;
        if (!g_shadeLayerSaid)
        {
            g_shadeLayerSaid = true;
            const MyGUI::ILayer* lay = r->getLayer();
            DebugLog("[TRADE] the offered items' shade is drawn on the inventory window's layer '" + g_shadeLayer + "' (" + S(lay != 0 ? (long long)lay->getLayerNodeCount() : -1LL)
                     + " node(s) on it; logged once a launch)");
        }
    }
    if (g_shadeRoot->getWidth() != vw || g_shadeRoot->getHeight() != vh) g_shadeRoot->setCoord(MyGUI::IntCoord(0, 0, vw, vh));
    while (g_shade.size() < boxes.size())
    {
        const std::string nm = "SWTradeShade" + S(g_shade.size());
        MyGUI::Widget* s = Mk(g_shadeRoot, "Widget", "WhiteSkin", 0, 0, 1, 1, MyGUI::Align::Default, nm.c_str());
        if (s == 0) break;
        s->setNeedMouseFocus(false);
        s->setColour(MyGUI::Colour(swtrade::kShadeRed, swtrade::kShadeGreen, swtrade::kShadeBlue, 1.0f));
        s->setAlpha(swtrade::kShadeAlpha);
        g_shade.push_back(s);
        ++g_shadeMade;
    }
    for (size_t i = 0; i < g_shade.size(); ++i)
    {
        MyGUI::Widget* s = g_shade[i];
        if (i < boxes.size()) { if (s->getCoord() != boxes[i]) s->setCoord(boxes[i]); if (!s->getVisible()) s->setVisible(true); }
        else if (s->getVisible()) s->setVisible(false);
    }
    if (!g_shadeRoot->getVisible()) g_shadeRoot->setVisible(true);
    MyGUI::ILayer* lay = g_shadeRoot->getLayer();
    const size_t nodes = lay != 0 ? lay->getLayerNodeCount() : 0;
    MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
    if (lm != 0 && nodes > 1 && lay->getLayerNodeAt(nodes - 1) != g_shadeRoot->getLayerNode()) { lm->upLayerItem(g_shadeRoot); ++g_shadeRaised; }
    g_shadeShown = (long long)(boxes.size() < g_shade.size() ? boxes.size() : g_shade.size());
    g_shadeInPack = inPack;
    if (g_shadeShown > g_shadePeak) g_shadePeak = g_shadeShown;
    if (inPack > g_shadePackPeak) g_shadePackPeak = inPack;
}
void PickBody()
{
    if (g_win == 0 || g_side < 0 || Locking()) { g_pickOffer = -1; return; }
    if (g_pickOffer >= 0)
    {
        const unsigned id = (unsigned)g_pickOffer; g_pickOffer = -1;
        if (g_list[0] != 0) g_list[0]->clearIndexSelected();
        std::vector<tradel::ExRow> ex = g_ex;
        const int i = tradel::ExFindId(ex, id);   /* T-1167 v3: the row by its own id, never by its place on screen */
        if (i >= 0) { ex.erase(ex.begin() + i); ExCommit(ex, "a click on this side's offer"); }
    }
    if (g_list[1] != 0 && g_list[1]->getIndexSelected() != MyGUI::ITEM_NONE) g_list[1]->clearIndexSelected();
}
void TickBody()
{
    /* T-1167 piece 2 fold 1 (review MEDIUM 2): NO HALF-OPEN TRADE. The world-server link's edge moved (it went down, or a new
       WELCOME, or a profile pick): the world server has already ended this game's trades (TradePeerGone tells the other side), so
       the window closes and the trade is cleared here. Fold 2 (re-check LOW): CANCEL left is sent too, for a WELCOME on the same
       live link; the world server ignores an id it no longer holds, and with no link the send is refused and logged. */
    if (g_side >= 0 && coop::StoreMoneyEdge() != g_linkEdge)
    {
        ++g_linkDropped;
        Say("[TRADE] the world-server link dropped or was made anew - trade id " + IdText(g_tr.id) + " is over on the world server; the window closes");
        CloseGuarded("the world-server link dropped", tradew::kWhyLeft);
    }
    Drain(true);
    LockTick();   /* T-1167 piece 3: the lock, the give-back and APPLIED - also with the window gone */
    /* the window gone (destroyed by anything but Close) with the trade still set: the trade is closed and the world server told */
    if (g_win == 0 && g_side >= 0)
    {
        ++g_windowGone;
        Say("[TRADE] the window is gone with trade id " + IdText(g_tr.id) + " still open - the trade is closed (CANCEL window-failed)");
        CloseGuarded("the window was gone", tradew::kWhyWindow);
    }
    if (g_win == 0) { if (g_shadeRoot != 0 || !g_shade.empty()) ShadeDrop(); return; }
    if (g_invWant && coop::ItemsTradeCursorBusy() == 0)
    {   /* fold 1 (review p3b M2): the character's inventory window, once nothing is on the cursor */
        g_invWant = false;
        void* me = g_mineUid != 0 ? (void*)coop::FindSpawned(g_mineUid) : 0;
        if (me != 0) InvBeside(me);
    }
    if (::GetTickCount() - g_invAt >= kInvEveryMs) { ReadMine(); AmountRead(true); }
    else AmountRead(false);
    PickBody();
    ShowAll();
    ShadeTick();   /* T-1167 v3 (the fade): after the offer's changes of this tick */
}
void TickCatching() { try { TickBody(); } catch (...) { ++g_throws; } }
void TickGuarded() { __try { TickCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; } }

}   // namespace

namespace coop {

int TradeWinOpen(void* mine, void* theirs, int theirSlot)
{
    if (mine == 0 || theirs == 0) return 0;
    if (g_win != 0 && g_mine == mine && g_theirs == theirs) { ++g_repeat; return 2; }
    if (g_win != 0 || g_side >= 0) CloseGuarded("another trade asked for", tradew::kWhyLeft);   /* fold 1: a half-open one too */
    if (MakeFor(mine, theirs, theirSlot) != 1) return 0;
    /* the trade: this game asks (side 0); the other player's game opens its own window for the same pair, or refuses */
    g_tr = tradew::Trade();
    g_tr.id = tradew::MakeId(MySlotForWire(), (unsigned long long)::time(0), ++g_idRun);
    g_tr.state = tradew::kStAsked; g_side = 0;
    g_tr.slot[0] = MySlotForWire(); g_tr.slot[1] = theirSlot; g_tr.uid[0] = g_mineUid; g_tr.uid[1] = g_theirUid;
    tradew::Msg a; a.kind = tradew::kAsk; a.id = g_tr.id; a.slot = theirSlot >= 0 ? (unsigned)theirSlot : 0xFFFFFFFFu; a.uidFrom = g_mineUid; a.uidTo = g_theirUid;
    const bool sent = Send(a);
    DebugLog("[TRADE] window opened: '" + g_headText[0] + "' / '" + g_headText[1] + "' mine uid " + S(g_mineUid) + " theirs uid " + S(g_theirUid)
             + " slot " + S(theirSlot) + (g_theirWho.empty() ? std::string(" (that player's name is not known here: no brackets)") : std::string())
             + " trade id " + IdText(g_tr.id) + (sent ? " - the other game asked through the world server" : " - the ASK could NOT be sent (no world-server link): the other game does not open its window"));
    ShowAll();
    return 1;
}

bool TradeWinIsOpen() { return g_win != 0; }

bool TradeWinQuietMoney(unsigned fromSlot, int amount, unsigned long long inbox)
{
    if (amount <= 0) return false;
    /* fold 1 (review p3a L2): by player and amount only while this trade's lock is open here (never after it); by the inbox number COMMIT named */
    const int open = (g_lock.phase == tradel::kLkLocked || g_lock.phase == tradel::kLkCommitPending || g_lock.phase == tradel::kLkCommitWaitDisk) ? 1 : 0;
    int inboxSame = (inbox != 0 && g_lock.dec.kind == tradew::kCommit && g_lock.dec.id == g_lock.id && g_lock.dec.inbox == inbox) ? 1 : 0;
    for (std::map<unsigned long long, Pend>::const_iterator it = g_pend.begin(); it != g_pend.end() && inboxSame == 0; ++it)
        if (inbox != 0 && it->second.m.kind == tradew::kCommit && it->second.m.inbox == inbox) inboxSame = 1;
    return tradel::QuietMoney(open, g_lock.otherSlot == (int)fromSlot ? 1 : 0, (int)g_lock.theirs.cats == amount ? 1 : 0, inboxSame) == 1;
}

unsigned int TradeWinMineUid() { return (g_win != 0 && g_side >= 0) ? g_mineUid : 0; }

void TradeWinArrive(const std::vector<char>& payload)
{
    if (g_inbox.size() >= kInboxCap) { ++g_inboxDropped; return; }
    g_inbox.push_back(payload);
}

void TradeWinTick() { TickGuarded(); }

void TradeWinTitleTick()
{
    if (g_win != 0 || g_side >= 0) CloseGuarded("the world was left", tradew::kWhyWorld, false);
    LockForgetWorld("the world was left");
    try { Drain(false); } catch (...) { ++g_throws; }
}

void TradeWinWorldTeardown()
{
    if (g_win != 0 || g_side >= 0) CloseGuarded("the world was torn down (the title, or another load)", tradew::kWhyWorld, false);
    LockForgetWorld("the world was torn down");
}

std::string TradeWinStatus()
{
    const std::string trade = g_side < 0 ? std::string(" trade=none")
        : " trade id=" + IdText(g_tr.id) + " side=" + S(g_side) + " state=" + (g_tr.state == tradew::kStOpen ? "open" : "asked")
          + " own{" + tradew::OfferText(g_tr.offer[g_side]) + " accepted=" + S(g_tr.accepted[g_side]) + "}"
          + " theirs{" + tradew::OfferText(g_tr.offer[1 - g_side]) + " accepted=" + S(g_tr.accepted[1 - g_side]) + "}"
          + " ready=" + (tradew::Ready(g_tr) ? "1" : "0") + " lock=" + std::string(tradel::PhaseName(g_lock.phase)) + (g_lock.id != 0 ? ":" + IdText(g_lock.id) : std::string()) + (Locking() ? " waiting=1" : "");
    if (g_win == 0) return "ok tradetest status open=0 opened=" + S(g_opened) + " closed=" + S(g_closed) + trade;
    const std::string amount = g_amount != 0 ? g_amount->getOnlyText().asUTF8() : std::string("?");
    return "ok tradetest status open=1 title=" + std::string(swtrade::Title()) + " own='" + g_headText[0] + "' theirs='" + g_headText[1]
         + "' ownRows=" + S(g_list[0] != 0 ? (long long)g_list[0]->getItemCount() : -1LL) + " theirRows=" + S(g_list[1] != 0 ? (long long)g_list[1]->getItemCount() : -1LL)
         + " invOpened=" + S(g_invOpened ? 1 : 0)
         + " ownMoney='" + std::string(swtrade::MoneyWord()) + "' amount='" + amount + "' hint='" + (g_hint != 0 ? g_hint->getCaption().asUTF8() : std::string("?"))
         + "' purse=" + S(g_purse) + " theirMoney='" + (g_money[1] != 0 ? g_money[1]->getCaption().asUTF8() : std::string("?"))
         + "' ownStatus='" + (g_status[0] != 0 ? g_status[0]->getCaption().asUTF8() : std::string("?")) + "' theirStatus='" + (g_status[1] != 0 ? g_status[1]->getCaption().asUTF8() : std::string("?"))
         + "' accept='" + (g_accept != 0 ? g_accept->getCaption().asUTF8() : std::string("?"))
         + "' cancel='" + (g_cancel != 0 ? g_cancel->getCaption().asUTF8() : std::string("?")) + "' mineUid=" + S(g_mineUid) + " theirUid=" + S(g_theirUid)
         + " slot=" + S(g_theirSlot) + " noRoom='" + g_noRoomLine + "'" + trade;
}

std::string TradeWinPress(const std::string& which)
{
    if (g_win == 0) return "error tradetest press: no trade window is open";
    if (which == "accept") { OnAccept(g_accept); return "ok tradetest press accept -> " + TradeWinStatus(); }
    if (which == "cancel") { OnCancel(g_cancel); return std::string("ok tradetest press cancel -> open=") + (g_win != 0 ? "1" : "0"); }
    return "error tradetest press: accept or cancel";
}

std::string TradeWinOfferLever(const std::string& item, const std::string& n)
{
    if (g_win == 0 || g_side < 0) return "error tradetest offer: no trade window is open";
    unsigned long long v = 0;
    if (n.empty() || n.size() > 7) return "error tradetest offer: usage offer <itemKey|name> <n 0.." + S(tradew::kMaxCount) + ">";
    for (size_t i = 0; i < n.size(); ++i) { if (n[i] < '0' || n[i] > '9') return "error tradetest offer: <n> is not a whole number"; v = v * 10 + (unsigned)(n[i] - '0'); }
    ReadMine();
    std::string low = item;
    for (size_t i = 0; i < low.size(); ++i) if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] - 'A' + 'a');
    int at = -1;
    for (size_t i = 0; i < g_rows.size() && at < 0; ++i) if (g_rows[i].key == item) at = (int)i;
    for (size_t i = 0; i < g_rows.size() && at < 0; ++i)
    {
        std::string nm = g_rows[i].name;
        for (size_t k = 0; k < nm.size(); ++k) if (nm[k] >= 'A' && nm[k] <= 'Z') nm[k] = (char)(nm[k] - 'A' + 'a');
        if (nm == low) at = (int)i;
    }
    /* the first stack with that key or name names the thing (fold 1: its maker, material, colour and quality too); a row of the
       offer no longer held can still be set to 0 by its key */
    tradew::Item thing(item, item, 0);
    if (at >= 0) thing = ThingOf(g_rows[(size_t)at]);
    else
    {
        const tradew::Offer& o = g_tr.offer[g_side];
        int off = -1;
        for (size_t i = 0; i < o.items.size() && off < 0; ++i) if (o.items[i].key == item) off = (int)i;
        if (v > 0 || off < 0) return "error tradetest offer: this character holds no item with the key or name '" + item + "' (" + S(g_rows.size()) + " stacks read)";
        thing = o.items[(size_t)off];
    }
    if (v > HeldOf(thing)) return "error tradetest offer: '" + thing.name + "' - this character holds " + S(HeldOf(thing)) + ", not " + S(v);
    /* T-1167 v3: the units from that thing's objects in the inventory's order (the exact rows) */
    tradel::ExRow proto = at >= 0 ? ProtoOf(g_rows[(size_t)at]) : tradel::ExRow();
    if (at < 0) proto.thing = thing;
    std::vector<tradel::ExRow> ex = g_ex;
    if (tradel::ExactSetFromHeld(&ex, proto, (unsigned)v, HeldList(), &g_exNextId) < 0) return "error tradetest offer: refused (too many rows, or a key or name too long)";
    const int r = ExCommit(ex, "the TEST-ONLY lever");
    return std::string(r >= 0 ? "ok" : "error") + " tradetest offer " + thing.key + " " + S(v) + (r == 0 ? " (the same as before: nothing sent)" : "") + " -> " + TradeWinStatus();
}

std::string TradeWinCatsLever(const std::string& text)
{
    if (g_win == 0 || g_side < 0 || g_amount == 0) return "error tradetest cats: no trade window is open";
    g_amount->setOnlyText(MyGUI::UString(text.c_str()));   /* as if typed; a text that is not digits stays in the box ("Type an amount.") */
    g_invAt = 0;
    TickGuarded();
    return "ok tradetest cats '" + text + "' -> " + TradeWinStatus();
}

std::string TradeWinReportToken()
{
    std::string sent, recv;
    for (int k = 1; k < 8; ++k) { sent += (k > 1 ? "," : "") + S(g_sent[k]); recv += (k > 1 ? "," : "") + S(g_recv[k]); }
    return " tradeWin[open,opened,repeat,refusedOpen,accepts,cancels,closed,faults,throws]=" + S(g_win != 0 ? 1 : 0) + "," + S(g_opened) + "," + S(g_repeat)
         + "," + S(g_refusedOpen) + "," + S(g_accepts) + "," + S(g_cancels) + "," + S(g_closed) + "," + S(g_faults) + "," + S(g_throws)
         + " tradeMsg[sent ask..cancel]=" + sent + " tradeMsg[recv ask..cancel]=" + recv
         + " tradeMsg[sendFail,malformed,otherId,inboxDropped,ready,acceptRefused,unsaid]=" + S(g_sendFail) + "," + S(g_malformed) + "," + S(g_otherId)
         + "," + S(g_inboxDropped) + "," + S(g_ready) + "," + S(g_acceptRefused) + "," + S(g_unsaid)
         + " tradeEnd[windowGone,linkDropped,trimmed]=" + S(g_windowGone) + "," + S(g_linkDropped) + "," + S(g_trimmed)
         + " tradeLock[locked,refused,aborted,applied,repeats,fails,phase]=" + S(g_lkLocked) + "," + S(g_lkRefused) + "," + S(g_lkAborted) + "," + S(g_lkApplied)
         + "," + S(g_lkRepeats) + "," + S(g_lkFails) + "," + S(g_lock.phase)
         + " tradeLock3[noRoom,noRoomShown,committed,recovered,pending]=" + S(g_lkNoRoom) + "," + S(g_lkNoRoomShown) + "," + S(g_lkCommitted) + "," + S(g_lkRecovered) + "," + S(g_pend.size())
         + " tradeDrag[engine,offered,back,noReturn,faults,invOpenFail]=" + S(g_dropEngine) + "," + S(g_dropOffered) + "," + S(g_dropBack) + "," + S(g_dropNoReturn)
         + "," + S(g_dropFaults) + "," + S(g_invOpenFail)
         + " tradeV3[exSame,exTrims,rightOffered,rightTaken,moves,resizes]=" + S(g_exSame) + "," + S(g_exTrims) + "," + S(g_rightOffered) + "," + S(g_rightTaken)
         + "," + S(g_moves) + "," + S(g_resizes)
         + " tradeShade[shown,inPack,unplaced,peak,packPeak,made,raised,layerFail]=" + S(g_shadeShown) + "," + S(g_shadeInPack) + "," + S(g_shadeUnplaced) + "," + S(g_shadePeak)
         + "," + S(g_shadePackPeak) + "," + S(g_shadeMade) + "," + S(g_shadeRaised) + "," + S(g_shadeLayerFail)
         + coop::ItemsTradeToken() + coop::ItemsTradeDragToken();
}

int TradeWinDrop(void* mouse)
{
    if (g_win == 0) return 0;   /* no TRADE window: the engine's own release, at once */
    __try { return DropCatching(mouse); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_dropFaults; return 0; }
}

int TradeWinRightClick(void* gui)
{
    if (g_win == 0) return 0;   /* no TRADE window: the engine's own right-click, at once */
    __try { return RightCatching(gui); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_dropFaults; return 0; }
}

std::string TradeWinDropLever(const std::string& args)
{
    if (g_win == 0 || g_side < 0 || g_list[0] == 0) return "error tradetest drop: no trade window is open";
    std::istringstream is(args);
    int x = 0, y = 0;
    const MyGUI::IntCoord r = g_list[0]->getAbsoluteCoord();
    if (!(is >> x >> y)) { x = r.left + r.width / 2; y = r.top + r.height / 2; }   /* the middle of this player's offer pane */
    const long long before[3] = { g_dropOffered, g_dropBack, g_dropEngine };
    std::string why;
    if (coop::ItemsTradeDropLever(x, y, &why) != 1) return "error tradetest drop: " + why;
    const char* road = g_dropOffered != before[0] ? "offered" : (g_dropBack != before[1] ? "back" : (g_dropEngine != before[2] ? "engine" : "none"));
    return "ok tradetest drop x=" + S(x) + " y=" + S(y) + " pane=" + S(r.left) + "," + S(r.top) + "," + S(r.width) + "," + S(r.height) + " road=" + road + " -> " + TradeWinStatus();
}

}   // namespace coop
