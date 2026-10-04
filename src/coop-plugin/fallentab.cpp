// fallentab.cpp - see fallentab.h. Built as playerstab.cpp builds PLAYERS: the management window's "<prefix>_TabsMain" is found by
// its layout suffix at most once a second; our widgets are kept from when they were made and MyGUI's destroy notice drops each
// pointer before its widget is freed; the control's own tab-change event refreshes the page. The engine's tab-change handler
// 0x49B500 titles the window from the chosen tab's name and switches on the game's numbers 0..6 only (playerstab.cpp), so FALLEN
// at position 7 (no PLAYERS) or 8 (after PLAYERS) titles the window FALLEN and runs nothing else (Read). PLAYERS always takes
// position 7: added after FALLEN, it is inserted before it. The tab buttons share the strip the game's seven filled
// (playerstab.cpp MgmtTabsFit).
// THE PAGE, top to bottom (mock-up section 2): the list (NAME RACE DIED PLACE CAUSE, Kenshi_MultiListBox) with
// "None of your characters has fallen." over it while it is empty; the three price lines; the caption line (the select prompt, a
// refusal, or the progress line); "BRING <NAME> BACK BESIDE"; the squadmate drop-down (Kenshi_ComboBox, "name  (squad)", free
// squadmates only; it starts on the character selected in the game, else the first) on the left and BRING BACK with the price
// on the right. The lines under the list start where the list's own text does. Which of them show, and their words:
// src/common/fallentab.h ViewFor / PriceLinesFor / RefusalWords. The drop-down's pick is read back from the box each frame
// (ComboBox::getIndexSelected) and adopted when it differs from what this file last set: no ComboBox event member is used.
// A column head's click sorts the list as drawn (DIED by the day's number, swtab::CellLess); the list's index stays the order
// the rows were added in (MyGUI's MultiListBox converts it to the drawn order inside), so a list index is a row index.
// THE ROAD: BRING BACK opens the confirm box (the price and the next price as read at the press, CANCEL left). Its BRING BACK
// closes the box, shows the progress line with the button greyed, and on the next frame calls resurrect.cpp
// ResurrectBringBackFor - the TEST lever's own road - with the row's uid, the squadmate's uid and the price the box showed. Done:
// "<name> is back, beside <mate>. Paid c.<n>." on the game's message line (store.cpp StoreShowPlayerLine); refused: the caption
// line says why until the selection or the squadmate changes. While the box is up, a word it shows that changes (the price, its
// sum, the next price) or a squadmate who can no longer be picked closes it, and the caption line says so; money spent while
// it is up is said only after its BRING BACK (the road refuses). The box is centred on the management window, inside it.
#include "fallentab.h"
#include "resurrect.h"
#include "playerstab.h"   /* PlayersTabItemPtr, MgmtTabsFit - the strip PLAYERS and FALLEN share */
#include "coop_log.h"
#include "ui.h"           /* UiFindLayoutSuffix */
#include "store.h"        /* HandSaveBlocked, EngineWritesBlocked, StoreShowPlayerLine */
#include "soak.h"         /* GameplayRunning */
#include "../common/fallentab.h"
#include "../common/playerstab.h"    /* kEngineTabCount, kFactionTab, EscapeHash */
#include "../common/panelstatus.h"   /* NoticeBoxH / NoticeLayoutIn - the mod's message-box sizing */
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ISubWidgetText.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_TabControl.h>
#include <mygui/MyGUI_TabItem.h>
#include <mygui/MyGUI_MultiListBox.h>
#include <mygui/MyGUI_ComboBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_WidgetManager.h>
#include <mygui/MyGUI_IUnlinkWidget.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* const kTabsSuffix = "TabsMain";   /* the management window's tab control, after the layout's prefix */
const unsigned long kLookEveryMs = 1000;      /* how often the widget tree is searched for it while our tab is not in */
const unsigned long kReadEveryMs = 1000;      /* the page read again at least this often while it shows (or the box is up) */
const int kGiveUpAfter = 5;                   /* refused adds in one world, after which the tab control is not looked for again */
const int kEngine = playerstab::kEngineTabCount;

/* our widgets: "SW" names, no '_' (ui.cpp's rule) */
const char* const kListName    = "SWFallenList";
const char* const kEmptyName   = "SWFallenEmpty";
const char* const kPriceName[3] = { "SWFallenPriceA", "SWFallenPriceB", "SWFallenPriceC" };
const char* const kCaptionName = "SWFallenCaption";
const char* const kLabelName   = "SWFallenBesideLabel";
const char* const kPickName    = "SWFallenMatePick";
const char* const kBtnName     = "SWFallenBringBtn";
const char* const kBoxName     = "SWFallenBox";
const char* const kBoxText     = "SWFallenBoxText";
const char* const kBoxCancel   = "SWFallenCancelBtn";
const char* const kBoxOk       = "SWFallenBringOkBtn";

bool g_uiDead = false;               /* a memory fault in this module's widget work: no more tab added, no more refreshes */
unsigned long g_lookMs = 0;
MyGUI::TabControl* g_tabs = 0;       /* the tab control our tab was added to, and our tab */
MyGUI::TabItem* g_item = 0;
MyGUI::MultiListBox* g_list = 0;
MyGUI::TextBox* g_empty = 0;
MyGUI::TextBox* g_price[3] = { 0, 0, 0 };
MyGUI::TextBox* g_caption = 0;
MyGUI::TextBox* g_label = 0;
MyGUI::ComboBox* g_pick = 0;
MyGUI::Button* g_btn = 0;
MyGUI::Widget* g_box = 0;
bool g_unlinkerOn = false;
int g_refusedThisWorld = 0;

coop::FallenTabRead g_read;               /* the last read of the list, A, the price and the purse */
unsigned long g_readMs = 0;
std::vector<swtab::Row> g_rows;           /* the list's rows, newest first (the order they were added in = the list's index) */
std::vector<swtab::Row> g_rowsEsc;        /* the same with their cells as the list holds them ('#' doubled) */
std::vector<coop::FallenMate> g_mates;    /* the drop-down as shown */
unsigned int g_selUid = 0;                /* the selected row (the dead character's uid), 0 none */
unsigned int g_mateUid = 0;               /* the picked squadmate, 0 none */
size_t g_pickSet = MyGUI::ITEM_NONE;      /* the drop-down's position as this file last set or adopted it */
int g_note = swtab::kRefNone;             /* the refusal the last press left, with its words' names and numbers */
std::string g_noteName, g_noteMate;
long long g_notePrice = 0, g_noteMoney = 0;
unsigned int g_boxDead = 0, g_boxMate = 0;   /* what the confirm box asks about */
long long g_boxPrice = -1;
std::string g_boxName, g_boxMateName, g_boxText;
int g_boxSeto = 0;
int g_busy = 0;                           /* a confirmed bring-back waits for the next frame */
unsigned int g_busyDead = 0, g_busyMate = 0;
long long g_busyPrice = -1;
std::string g_busyName, g_busyMateName;
std::string g_shownPrice[3], g_shownCaption, g_shownLabel, g_shownBtn;
int g_shownBtnOn = -1;
long long g_inserted = 0, g_removed = 0, g_insertRefused = 0, g_tabOpened = 0, g_looks = 0, g_tabEvents = 0, g_rebuilds = 0, g_mateRebuilds = 0,
          g_selects = 0, g_matePicks = 0, g_presses = 0, g_pressGreyed = 0, g_boxOpened = 0, g_boxCancelled = 0, g_boxClosedByChange = 0,
          g_confirmed = 0, g_broughtBack = 0, g_lineShown = 0, g_lineNotShown = 0, g_faults = 0, g_throws = 0;
long long g_refusals[swtab::kRefCodes] = { 0 };

template <class T> std::string S(const T& v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
MyGUI::UString U(const std::string& s) { return MyGUI::UString(playerstab::EscapeHash(s).c_str()); }   /* never a colour code */
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }    /* Kenshi's light caption colour, as the mod's boxes */
MyGUI::Colour Greyed() { return MyGUI::Colour(0.50f, 0.49f, 0.47f, 1.0f); }

void ForgetTab();
bool Inside(MyGUI::Widget* mine, MyGUI::Widget* gone)
{
    for (MyGUI::Widget* p = mine; p != 0; p = p->getParent()) if (p == gone) return true;
    return false;
}
/* MyGUI's destroy notice (see playerstab.cpp): a pointer we keep to a widget being destroyed, or to anything inside it, is dropped */
class Unlinker : public MyGUI::IUnlinkWidget
{
public:
    void _unlinkWidget(MyGUI::Widget* w)
    {
        if (w == 0) return;
        if (g_box != 0 && Inside(g_box, w)) g_box = 0;
        if (g_item != 0 && Inside(g_item, w)) { ForgetTab(); return; }
        if (g_list != 0 && Inside(g_list, w)) g_list = 0;
        if (g_empty != 0 && Inside(g_empty, w)) g_empty = 0;
        for (int k = 0; k < 3; ++k) if (g_price[k] != 0 && Inside(g_price[k], w)) g_price[k] = 0;
        if (g_caption != 0 && Inside(g_caption, w)) g_caption = 0;
        if (g_label != 0 && Inside(g_label, w)) g_label = 0;
        if (g_pick != 0 && Inside(g_pick, w)) g_pick = 0;
        if (g_btn != 0 && Inside(g_btn, w)) g_btn = 0;
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
bool PageWhole()
{
    return g_list != 0 && g_empty != 0 && g_price[0] != 0 && g_price[1] != 0 && g_price[2] != 0 && g_caption != 0 && g_label != 0
        && g_pick != 0 && g_btn != 0;
}
MyGUI::TabControl* FindTabs()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* hit = 0;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (hit == 0 && roots.next()) hit = coop::UiFindLayoutSuffix(roots.current(), kTabsSuffix);
    return hit != 0 ? hit->castType<MyGUI::TabControl>(false) : 0;
}
bool Shown(MyGUI::Widget* w)
{
    if (w == 0) return false;
    for (MyGUI::Widget* p = w; p != 0; p = p->getParent()) if (!p->getVisible()) return false;
    return true;
}
bool Reachable(MyGUI::Widget* w) { return w != 0 && w->getEnabled() && Shown(w); }
MyGUI::Widget* Find(const char* name)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    return gui != 0 ? gui->findWidgetT(std::string(name), false) : 0;
}
MyGUI::Widget* Mk(MyGUI::Widget* parent, const char* type, const char* skin, int l, int t, int w, int h, MyGUI::Align align, const char* name)
{
    if (parent == 0) return 0;
    return parent->createWidgetT(std::string(type), std::string(skin), MyGUI::IntCoord(l, t, w > 1 ? w : 1, h > 1 ? h : 1), align, std::string(name));
}
/* our tab's position in the control, -1 when it is not in */
int OurIndex()
{
    if (g_tabs == 0 || g_item == 0) return -1;
    const size_t n = g_tabs->getItemCount();
    for (size_t i = 0; i < n; ++i) if (g_tabs->getItemAt(i) == g_item) return (int)i;
    return -1;
}
void ForgetTab()
{
    g_tabs = 0; g_item = 0; g_list = 0; g_empty = 0; g_price[0] = g_price[1] = g_price[2] = 0; g_caption = 0; g_label = 0; g_pick = 0; g_btn = 0;
    g_rows.clear(); g_rowsEsc.clear(); g_mates.clear(); g_pickSet = MyGUI::ITEM_NONE;
    for (int k = 0; k < 3; ++k) g_shownPrice[k].clear();
    g_shownCaption.clear(); g_shownLabel.clear(); g_shownBtn.clear(); g_shownBtnOn = -1;
}
void SetText(MyGUI::TextBox* t, const std::string& s, std::string* shown)
{
    if (t == 0 || s == *shown) return;
    *shown = s;
    t->setCaption(U(s));
}
void SetNote(int code, const std::string& name, const std::string& mate, long long price, long long money)
{
    g_note = code; g_noteName = name; g_noteMate = mate; g_notePrice = price; g_noteMoney = money;
}
void ClearNote() { g_note = swtab::kRefNone; g_noteName.clear(); g_noteMate.clear(); g_notePrice = 0; g_noteMoney = 0; }

void OnRowPicked(MyGUI::MultiListBox* list, size_t index);
void OnListLess(MyGUI::MultiListBox* list, size_t column, const MyGUI::UString& a, const MyGUI::UString& b, bool& less);
void OnTabChanged(MyGUI::TabControl* sender, size_t index);
void OnPress(MyGUI::Widget* w);
void OnBoxCancel(MyGUI::Widget* w);
void OnBoxConfirm(MyGUI::Widget* w);

/* THE PAGE: sizes follow the tab's own size, as PLAYERS (playerstab.cpp BuildPage) */
bool BuildPage(MyGUI::TabItem* item)
{
    const int W = item->getWidth(), H = item->getHeight();
    const int pad = Clamp(H * 12 / 1000, 4, 12);
    const int lineH = Clamp(H * 30 / 1000, 18, 30);
    const int btnH = Clamp(H * 40 / 1000, 22, 40);
    const int btnW = Clamp(W * 26 / 100, 150, 300), pickW = Clamp(W * 40 / 100, 160, 420);
    const int btnY = H - pad - btnH;
    const int labelY = btnY - pad / 2 - lineH;
    const int capY = labelY - lineH;
    const int priceY = capY - pad - 3 * lineH;
    const int listY = pad, listH = priceY - pad - listY;
    const int inset = Clamp(W * 12 / 1000, 4, 12);   /* where the list's own row text starts (T985 / T957 shots: 5 px at 418, 10 at ~830) */
    if (listH < 60 || pickW + btnW + pad > W) return false;
    const MyGUI::Align low = MyGUI::Align::HStretch | MyGUI::Align::Bottom;
    MyGUI::Widget* lw = Mk(item, "MultiListBox", "Kenshi_MultiListBox", 0, listY, W, listH, MyGUI::Align::Stretch, kListName);
    MyGUI::Widget* ew = Mk(item, "TextBox", "Kenshi_TextboxStandardText", 0, listY + listH / 2 - lineH / 2, W, lineH, MyGUI::Align::HStretch | MyGUI::Align::VCenter, kEmptyName);
    MyGUI::Widget* pw[3];
    for (int k = 0; k < 3; ++k) pw[k] = Mk(item, "TextBox", "Kenshi_TextboxStandardText", inset, priceY + k * lineH, W - inset, lineH, low, kPriceName[k]);
    MyGUI::Widget* cw = Mk(item, "TextBox", "Kenshi_TextboxStandardText", inset, capY, W - inset, lineH, low, kCaptionName);
    MyGUI::Widget* lb = Mk(item, "TextBox", "Kenshi_TextboxPaintedText", inset, labelY, W - inset, lineH, low, kLabelName);
    MyGUI::Widget* kw = Mk(item, "ComboBox", "Kenshi_ComboBox", 0, btnY, pickW, btnH, MyGUI::Align::Left | MyGUI::Align::Bottom, kPickName);
    MyGUI::Widget* bw = Mk(item, "Button", "Kenshi_Button2", W - btnW, btnY, btnW, btnH, MyGUI::Align::Right | MyGUI::Align::Bottom, kBtnName);
    MyGUI::MultiListBox* list = lw != 0 ? lw->castType<MyGUI::MultiListBox>(false) : 0;
    MyGUI::TextBox* empty = ew != 0 ? ew->castType<MyGUI::TextBox>(false) : 0;
    MyGUI::TextBox* price[3];
    for (int k = 0; k < 3; ++k) price[k] = pw[k] != 0 ? pw[k]->castType<MyGUI::TextBox>(false) : 0;
    MyGUI::TextBox* cap = cw != 0 ? cw->castType<MyGUI::TextBox>(false) : 0;
    MyGUI::TextBox* label = lb != 0 ? lb->castType<MyGUI::TextBox>(false) : 0;
    MyGUI::ComboBox* pick = kw != 0 ? kw->castType<MyGUI::ComboBox>(false) : 0;
    MyGUI::Button* btn = bw != 0 ? bw->castType<MyGUI::Button>(false) : 0;
    if (list == 0 || empty == 0 || price[0] == 0 || price[1] == 0 || price[2] == 0 || cap == 0 || label == 0 || pick == 0 || btn == 0) return false;
    g_list = list; g_empty = empty; g_caption = cap; g_label = label; g_pick = pick; g_btn = btn;
    for (int k = 0; k < 3; ++k) g_price[k] = price[k];
    int widths[swtab::kColumns];
    swtab::ColumnWidths(W - 32 > 0 ? W - 32 : 0, widths);   /* the inner width: less the skin's frame and its scroll bar (ui.cpp's lists) */
    for (int c = 0; c < swtab::kColumns; ++c) list->addColumn(MyGUI::UString(swtab::ColumnHead(c)), widths[c]);
    list->eventListChangePosition += MyGUI::newDelegate(OnRowPicked);
    list->requestOperatorLess = MyGUI::newDelegate(OnListLess);   /* the heads' sort: DIED by the day's number */
    empty->setTextAlign(MyGUI::Align::Center);
    empty->setTextColour(Lit());
    empty->setCaption(U(swtab::kEmptyLine));
    empty->setVisible(false);
    for (int k = 0; k < 3; ++k) { price[k]->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter); price[k]->setTextColour(Lit()); price[k]->setCaption(MyGUI::UString("")); }
    cap->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    cap->setTextColour(Lit());
    cap->setCaption(MyGUI::UString(""));
    label->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    label->setCaption(MyGUI::UString(""));
    label->setVisible(false);
    pick->setComboModeDrop(true);   /* a drop-down list: picked from, never typed in; the pick is read back each frame (AdoptPick) */
    pick->setVisible(false);
    btn->setCaption(MyGUI::UString(swtab::ButtonCaption(0, 0).c_str()));
    btn->setTextColour(Lit());
    btn->eventMouseButtonClick += MyGUI::newDelegate(OnPress);
    btn->setVisible(false);
    const MyGUI::IntCoord c = item->getAbsoluteCoord();
    DebugLog("[UI] rect fallentab x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height)
             + " list h=" + S(listH) + " text x=" + S(inset) + " price y=" + S(priceY) + " caption y=" + S(capY) + " label y=" + S(labelY)
             + " picker w=" + S(pickW) + " button y=" + S(btnY) + " w=" + S(btnW) + " h=" + S(btnH));
    return true;
}

/* ---- in and out of the management window ---- */
void Refused(const std::string& why)
{
    ++g_insertRefused;
    if (++g_refusedThisWorld <= 3) DebugLog("[FALLEN] tab NOT added: " + why);
    if (g_refusedThisWorld == kGiveUpAfter) DebugLog("[FALLEN] tab NOT added " + S(kGiveUpAfter) + " times in this world - not tried again until the next world");
}
void Insert(MyGUI::TabControl* tabs)
{
    const size_t n = tabs->getItemCount();
    MyGUI::TabItem* players = (MyGUI::TabItem*)coop::PlayersTabItemPtr();
    const bool afterPlayers = players != 0 && n == (size_t)kEngine + 1 && tabs->getItemAt((size_t)kEngine) == players;
    if (n != (size_t)kEngine && !afterPlayers)
    {
        Refused("the management window holds " + S((long long)n) + " tabs, not the game's " + S(kEngine) + (players != 0 ? " and PLAYERS" : ""));
        return;
    }
    const size_t at = afterPlayers ? (size_t)kEngine + 1 : (size_t)kEngine;
    const int bw = tabs->getButtonDefaultWidth();
    coop::MgmtTabsFit(tabs);   /* the game's own width noted while the control holds its seven */
    MyGUI::TabItem* fac = tabs->getItemAt((size_t)playerstab::kFactionTab);
    MyGUI::TabItem* item = tabs->insertItemAt(at, MyGUI::UString(swtab::kTabCaption));
    if (item == 0) { Refused("insertItemAt returned no tab"); return; }
    if (fac != 0) { item->setCoord(fac->getCoord()); item->setAlign(fac->getAlign()); }
    coop::MgmtTabsFit(tabs);
    ForgetTab();
    g_tabs = tabs; g_item = item;
    const int iw = item->getWidth(), ih = item->getHeight();
    bool built = false;
    try { built = BuildPage(item); } catch (...) { ++g_throws; }
    if (!built)
    {
        tabs->removeItemAt(at);   /* our tab is in the control only with its whole page */
        coop::MgmtTabsFit(tabs);
        ForgetTab();
        Refused("its page could not be built (tab " + S(iw) + " x " + S(ih) + ") - the window keeps the tabs it had");
        return;
    }
    tabs->eventTabChangeSelect -= MyGUI::newDelegate(OnTabChanged);   /* a removal first: never the handler twice */
    tabs->eventTabChangeSelect += MyGUI::newDelegate(OnTabChanged);
    ++g_inserted;
    DebugLog("[FALLEN] tab added after " + std::string(afterPlayers ? "PLAYERS" : "AI") + " (position " + S((long long)at) + " of "
             + S((long long)tabs->getItemCount()) + "; tab buttons " + S(bw) + " -> " + S(tabs->getButtonDefaultWidth()) + " px wide)");
}
void Remove(const char* why)
{
    MyGUI::TabControl* tabs = g_tabs;
    const int at = OurIndex();
    if (tabs != 0 && at >= 0) tabs->removeItemAt((size_t)at);
    if (tabs != 0) coop::MgmtTabsFit(tabs);
    ForgetTab();
    ++g_removed;
    DebugLog(std::string("[FALLEN] tab removed (") + why + ")");
}
/* every multiplayer world, alone or not, with resurrection on or off (owner 507) */
bool Wanted() { return coop::HandSaveBlocked(); }

/* ---- the page's contents ---- */
int MateIndex(unsigned int uid)
{
    if (uid == 0) return -1;
    for (size_t i = 0; i < g_mates.size(); ++i) if (g_mates[i].uid == uid) return (int)i;
    return -1;
}
bool SameMates(const std::vector<coop::FallenMate>& a, const std::vector<coop::FallenMate>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].uid != b[i].uid || a[i].name != b[i].name || a[i].squad != b[i].squad) return false;
    return true;
}
std::string MateName(unsigned int uid) { const int i = MateIndex(uid); return i >= 0 ? g_mates[(size_t)i].name : std::string(); }
void DropBox()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* w = g_box;
    g_box = 0; g_boxDead = 0; g_boxMate = 0; g_boxPrice = -1;
    if (gui == 0 || w == 0) return;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->removeWidgetModal(w); im->resetKeyFocusWidget(); }
    gui->destroyWidget(w);
}
/* what the page shows, from the last read and the selection (fallentab.h ViewFor) */
void ApplyView()
{
    if (!PageWhole()) return;
    const int sel = swtab::RowOfUid(g_rows, g_selUid);
    const std::string selName = sel >= 0 ? g_rows[(size_t)sel].name : std::string();
    swtab::ViewIn in;
    in.on = g_read.on; in.rows = (int)g_rows.size(); in.selected = sel >= 0 ? 1 : 0; in.mates = (int)g_mates.size();
    in.busy = g_busy; in.note = g_note; in.price = g_read.price; in.money = g_read.money; in.moneyRead = g_read.moneyRead; in.boxUp = g_box != 0 ? 1 : 0;
    const swtab::View v = swtab::ViewFor(in);
    const swtab::PriceLines pl = swtab::PriceLinesFor(g_read.on, g_read.amount, g_read.growth, g_read.alive);
    const std::string before = g_shownPrice[0] + " | " + g_shownPrice[1] + " | " + g_shownPrice[2];
    for (int k = 0; k < 3; ++k)
    {
        SetText(g_price[k], pl.shown ? pl.line[k] : std::string(), &g_shownPrice[k]);
        if (g_price[k]->getVisible() != (pl.shown != 0)) g_price[k]->setVisible(pl.shown != 0);
    }
    const std::string after = g_shownPrice[0] + " | " + g_shownPrice[1] + " | " + g_shownPrice[2];
    if (after != before) DebugLog("[FALLEN] tab price lines: '" + after + "'");
    if (g_empty->getVisible() != (v.emptyShown != 0)) g_empty->setVisible(v.emptyShown != 0);
    std::string cap;
    if (v.caption == swtab::kCapSelect) cap = swtab::SelectLine();
    else if (v.caption == swtab::kCapBusy) cap = swtab::ProgressLine(g_busyName);
    else if (v.caption == swtab::kCapRefusal)
    {
        /* the names the refusal was made with; the price and the purse as read now */
        if (v.refusal == g_note && g_note != swtab::kRefNone) cap = swtab::RefusalWords(g_note, g_noteName, g_noteMate, g_read.price, g_read.money);
        else cap = swtab::RefusalWords(v.refusal, selName, MateName(g_mateUid), g_read.price, g_read.money);
    }
    if (cap != g_shownCaption) DebugLog("[FALLEN] tab caption line: '" + cap + "'" + (v.caption == swtab::kCapRefusal ? std::string(" (") + swtab::RefusalTag(v.refusal) + ")" : std::string()));
    SetText(g_caption, cap, &g_shownCaption);
    SetText(g_label, v.pickShown ? swtab::BesideLabel(selName) : std::string(), &g_shownLabel);
    if (g_label->getVisible() != (v.pickShown != 0)) g_label->setVisible(v.pickShown != 0);
    if (g_pick->getVisible() != (v.pickShown != 0)) g_pick->setVisible(v.pickShown != 0);
    if (g_pick->getEnabled() != (v.pickEnabled != 0)) g_pick->setEnabled(v.pickEnabled != 0);
    const std::string bc = swtab::ButtonCaption(g_read.on, g_read.price);
    if (bc != g_shownBtn) { g_shownBtn = bc; g_btn->setCaption(MyGUI::UString(bc.c_str())); }
    if (g_btn->getVisible() != (v.pickShown != 0)) g_btn->setVisible(v.pickShown != 0);
    if (g_shownBtnOn != v.btnEnabled)
    {
        g_shownBtnOn = v.btnEnabled;
        g_btn->setEnabled(v.btnEnabled != 0);
        g_btn->setTextColour(v.btnEnabled ? Lit() : Greyed());
    }
}
/* the table as the log shows it: on every rebuild and on every opening of the tab */
void LogTable()
{
    DebugLog("[FALLEN] tab table: " + S((long long)g_rows.size()) + " row(s)" + (g_rows.empty() ? std::string() : std::string(":")));
    for (size_t i = 0; i < g_rows.size() && i < 8; ++i)
        DebugLog("[FALLEN]   uid " + S(g_rows[i].uid) + " | " + g_rows[i].cell[0] + " | " + g_rows[i].cell[1] + " | " + g_rows[i].cell[2]
                 + " | " + g_rows[i].cell[3] + " | " + g_rows[i].cell[4] + (g_rows[i].setoOrBigBo ? " (Seto / Big Bo)" : ""));
}
std::string Utf8(const MyGUI::UString& u) { const char* t = u.asUTF8_c_str(); return std::string(t != 0 ? t : ""); }
/* the drop-down's position set from here, and remembered so the frame's read-back does not take it for a pick */
void SetPick(int mi)
{
    const size_t want = mi >= 0 ? (size_t)mi : MyGUI::ITEM_NONE;
    if (g_pick->getIndexSelected() != want)
    {
        if (want == MyGUI::ITEM_NONE) g_pick->clearIndexSelected(); else g_pick->setIndexSelected(want);
    }
    g_pickSet = want;
}
void MateBody(size_t index);
/* the pick a mouse made in the drop-down: its position read back from the box, adopted when it differs from what was set */
void AdoptPick()
{
    if (g_pick == 0 || !g_pick->getVisible()) return;
    const size_t i = g_pick->getIndexSelected();
    if (i == g_pickSet) return;
    g_pickSet = i;
    MateBody(i);
}
/* everything read again: the rows, the squadmates, A, the price and the purse; the box closed if a word it shows changed */
void Refresh()
{
    if (!PageWhole()) return;
    AdoptPick();   /* a pick made since the last frame, before the list it was made in is rebuilt */
    g_readMs = ::GetTickCount();
    coop::ResurrectTabRead(&g_read);
    std::vector<coop::FallenMate> mates;
    coop::ResurrectFreeMates(&mates);
    if (!swtab::SameRows(g_read.rows, g_rows))
    {
        g_list->removeAllItems();
        g_rowsEsc = g_read.rows;
        for (size_t i = 0; i < g_rowsEsc.size(); ++i)
            for (int c = 0; c < swtab::kColumns; ++c) g_rowsEsc[i].cell[c] = playerstab::EscapeHash(g_read.rows[i].cell[c]);
        for (size_t i = 0; i < g_rowsEsc.size(); ++i)
        {
            g_list->addItem(MyGUI::UString(g_rowsEsc[i].cell[0].c_str()));
            for (int c = 1; c < swtab::kColumns; ++c) g_list->setSubItemNameAt((size_t)c, i, MyGUI::UString(g_rowsEsc[i].cell[c].c_str()));
        }
        g_rows = g_read.rows;
        ++g_rebuilds;
        LogTable();
    }
    if (swtab::RowOfUid(g_rows, g_selUid) < 0 && g_selUid != 0 && !g_busy) { g_selUid = 0; ClearNote(); }
    const int sel = swtab::RowOfUid(g_rows, g_selUid);
    const int at = sel;   /* the list's index is the row's (the order of adding), however a column head sorted it */
    if (at >= 0) { if (g_list->getIndexSelected() != (size_t)at) g_list->setIndexSelected((size_t)at); }
    else if (g_list->getIndexSelected() != MyGUI::ITEM_NONE) g_list->clearIndexSelected();
    if (!SameMates(mates, g_mates))
    {
        g_pick->removeAllItems();
        for (size_t i = 0; i < mates.size(); ++i) g_pick->addItem(U(swtab::MateItem(mates[i].name, mates[i].squad)));
        g_pickSet = MyGUI::ITEM_NONE;
        ++g_mateRebuilds;
        std::string t;
        for (size_t i = 0; i < mates.size() && i < 8; ++i)
            t += std::string(i ? ", " : "") + "'" + swtab::MateItem(mates[i].name, mates[i].squad) + "' uid " + S(mates[i].uid) + (mates[i].selected ? " (selected in the game)" : "");
        DebugLog("[FALLEN] tab squadmates: " + S((long long)mates.size()) + (mates.empty() ? std::string() : ": " + t));
    }
    g_mates = mates;   /* the same entries; who is selected in the game is read fresh */
    if (g_box != 0)
    {
        const std::string now = g_read.on ? swtab::BoxText(g_boxName, g_boxMateName, g_read.amount, g_read.growth, (int)g_read.alive.size(), g_boxSeto) : std::string();
        if (!g_read.on || now != g_boxText)
        {
            ++g_boxClosedByChange;
            const std::string name = g_boxName, mate = g_boxMateName;
            const long long was = g_boxPrice;
            DropBox();
            if (g_read.on) SetNote(swtab::kRefPriceChanged, name, mate, g_read.price, g_read.money);
            DebugLog("[FALLEN] tab confirm box closed: " + std::string(g_read.on ? "a word it shows changed (price " + S(was) + " -> " + S(g_read.price) + "): '" + now + "'" : "the host turned resurrection off"));
        }
        else if (MateIndex(g_boxMate) < 0)
        {
            ++g_boxClosedByChange;
            const std::string name = g_boxName, mate = g_boxMateName;
            DropBox();
            SetNote(swtab::kRefMateGone, name, mate, g_read.price, g_read.money);
            DebugLog("[FALLEN] tab confirm box closed: '" + mate + "' can no longer be picked");
        }
    }
    int mi = MateIndex(g_mateUid);
    if (mi < 0 && !g_mates.empty())
    {
        std::vector<int> selected;
        for (size_t i = 0; i < g_mates.size(); ++i) selected.push_back(g_mates[i].selected);
        mi = swtab::StartMate(selected);
        g_mateUid = g_mates[(size_t)mi].uid;
        DebugLog("[FALLEN] tab squadmate start: uid " + S(g_mateUid) + " '" + swtab::MateItem(g_mates[(size_t)mi].name, g_mates[(size_t)mi].squad) + "' ("
                 + (g_mates[(size_t)mi].selected ? "selected in the game" : "the first in the list - none of them is selected in the game") + ")");
    }
    if (mi < 0) g_mateUid = 0;
    SetPick(mi);
    ApplyView();
}

/* ---- the confirm box (the approved page's box: Kenshi_WindowC on the Info layer over the management window, the text
   left-aligned from the top, the box as tall as its measured text, one blank line, then CANCEL left and BRING BACK right) ---- */
bool OpenBox(const std::string& text)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return false;
    int vw = 1280, vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { vw = s.width; vh = s.height; } }
    /* the area: the management window (the tab control's top widget), else the screen */
    MyGUI::IntCoord area(0, 0, vw, vh);
    MyGUI::Widget* top = g_tabs;
    while (top != 0 && top->getParent() != 0) top = top->getParent();
    if (top != 0) { const MyGUI::IntCoord m = top->getAbsoluteCoord(); if (m.width > 240 && m.height > 200) area = m; }
    int w = vw * 34 / 100;
    if (w < 360) w = 360;
    if (w > area.width - 16) w = area.width - 16;
    int h = coopui::NoticeBoxH(text, w);
    if (h < 170) h = 170;
    if (h > area.height - 16) h = area.height - 16;
    MyGUI::Widget* raw = 0;
    static const char* const kLayers[2] = { "Info", "Popup" };
    for (int k = 0; k < 2 && raw == 0; ++k)
    {
        raw = gui->createWidgetT(std::string("Window"), std::string("Kenshi_WindowC"), MyGUI::IntCoord(area.left + (area.width - w) / 2, area.top + (area.height - h) / 2, w, h),
                                 MyGUI::Align::Default, std::string(kLayers[k]), std::string(kBoxName));
        if (raw != 0 && raw->getLayer() == 0) { gui->destroyWidget(raw); raw = 0; }
    }
    MyGUI::Window* win = raw != 0 ? raw->castType<MyGUI::Window>(false) : 0;
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) { if (raw != 0) gui->destroyWidget(raw); return false; }
    win->setMovable(false);
    win->setCaption(MyGUI::UString(swtab::kBoxTitle));
    const int W = cl->getWidth(), H = cl->getHeight();
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);
    const int btnW = Clamp(W / 4, 120, W / 2 - 12);
    MyGUI::Widget* ew = Mk(cl, "EditBox", "Kenshi_WordWrapEmpty", 8, lay.textY, W - 16, lay.textH, MyGUI::Align::Default, kBoxText);
    MyGUI::Widget* b0w = Mk(cl, "Button", "Kenshi_Button2", 8, lay.btnY, btnW, lay.btnH, MyGUI::Align::Default, kBoxCancel);
    MyGUI::Widget* b1w = Mk(cl, "Button", "Kenshi_Button2", W - btnW - 8, lay.btnY, btnW, lay.btnH, MyGUI::Align::Default, kBoxOk);
    MyGUI::EditBox* e = ew != 0 ? ew->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* b0 = b0w != 0 ? b0w->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* b1 = b1w != 0 ? b1w->castType<MyGUI::Button>(false) : 0;
    if (e == 0 || b0 == 0 || b1 == 0) { gui->destroyWidget(raw); return false; }
    e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true);
    e->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    e->setTextColour(Lit());
    e->setCaption(U(text));
    b0->setCaption(MyGUI::UString(swtab::kBoxCancel)); b0->setTextColour(Lit());
    b1->setCaption(MyGUI::UString(swtab::kBoxConfirm)); b1->setTextColour(Lit());
    /* the box fitted to the text MyGUI laid out (EditBox::getTextSize at the box's width, the line height from the text's
       sub-widget, as ui.cpp's boxes): the text from the top, one blank line, the buttons; the estimate stays when it reads 0 */
    MyGUI::Widget* ec = e->getClientWidget();
    MyGUI::ISubWidgetText* st = (ec != 0 && ec != e) ? ec->getSubWidgetText() : 0;
    if (st == 0) st = e->getSubWidgetText();
    const int textH = e->getTextSize().height, lineH = st != 0 ? st->getFontHeight() : 0;
    const int insetH = (ec != 0 && ec != e && e->getHeight() > ec->getHeight()) ? e->getHeight() - ec->getHeight() : 0;
    if (textH > 0 && lineH > 0)
    {
        const MyGUI::IntCoord bc = raw->getCoord();
        int nh = (bc.height - H) + coopui::NoticeClientHFor(textH + insetH, lineH);
        if (nh > area.height - 16) nh = area.height - 16;
        if (nh != bc.height && nh > bc.height - H) raw->setCoord(MyGUI::IntCoord(bc.left, area.top + (area.height - nh) / 2, bc.width, nh));
        const coopui::NoticeLayout fit = coopui::NoticeLayoutFor(cl->getHeight(), lineH);
        e->setCoord(MyGUI::IntCoord(8, fit.textY, W - 16, fit.textH > 1 ? fit.textH : 1));
        b0->setCoord(MyGUI::IntCoord(8, fit.btnY, btnW, fit.btnH));
        b1->setCoord(MyGUI::IntCoord(W - btnW - 8, fit.btnY, btnW, fit.btnH));
    }
    b0->eventMouseButtonClick += MyGUI::newDelegate(OnBoxCancel);
    b1->eventMouseButtonClick += MyGUI::newDelegate(OnBoxConfirm);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->addWidgetModal(raw); im->setKeyFocusWidget(raw); }
    g_box = raw;
    const MyGUI::IntCoord c = raw->getAbsoluteCoord();
    const MyGUI::IntCoord tc = e->getCoord(), bb = b0->getCoord();
    DebugLog("[UI] rect fallenbox x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height) + " text y=" + S(tc.top) + " h=" + S(tc.height)
             + " (measured " + S(textH) + ", line " + S(lineH) + (textH > 0 && lineH > 0 ? std::string() : std::string(" - NOT measured, the estimate kept")) + ", left-aligned)"
             + " buttons y=" + S(bb.top) + " in x=" + S(area.left) + " y=" + S(area.top)
             + " w=" + S(area.width) + " h=" + S(area.height) + (area.width == vw && area.left == 0 ? " (the screen)" : " (the management window)"));
    return true;
}

/* ---- the controls' handlers (MyGUI calls them from the game's input pass, main thread) ---- */
void RowBody(size_t index)
{
    const int r = index != MyGUI::ITEM_NONE && index < g_rows.size() ? (int)index : -1;   /* the list's index is the row's */
    g_selUid = r >= 0 ? g_rows[(size_t)r].uid : 0;
    ClearNote();
    ++g_selects;
    ApplyView();
    if (g_selUid != 0) DebugLog("[FALLEN] tab row selected: list index " + S((long long)index) + " uid " + S(g_selUid) + " '" + g_rows[(size_t)r].name + "' -> '" + swtab::BesideLabel(g_rows[(size_t)r].name) + "'");
}
void MateBody(size_t index)
{
    g_mateUid = (index != MyGUI::ITEM_NONE && index < g_mates.size()) ? g_mates[index].uid : 0;
    ClearNote();
    ++g_matePicks;
    ApplyView();
    if (g_mateUid != 0) DebugLog("[FALLEN] tab squadmate picked: uid " + S(g_mateUid) + " '" + swtab::MateItem(g_mates[index].name, g_mates[index].squad) + "'");
}
void PressBody()
{
    if (g_busy || g_box != 0 || coop::EngineWritesBlocked()) return;
    ++g_presses;
    Refresh();   /* read live at the press */
    const int sel = swtab::RowOfUid(g_rows, g_selUid);
    const int mi = MateIndex(g_mateUid);
    if (sel < 0 || mi < 0 || g_shownBtnOn != 1)
    {
        ++g_pressGreyed;
        DebugLog("[FALLEN] tab BRING BACK pressed, but " + std::string(sel < 0 ? "no row is selected" : mi < 0 ? "no squadmate is picked" : "the button reads greyed now") + " - no box");
        return;
    }
    const swtab::Row& r = g_rows[(size_t)sel];
    const coop::FallenMate& m = g_mates[(size_t)mi];
    const std::string text = swtab::BoxText(r.name, m.name, g_read.amount, g_read.growth, (int)g_read.alive.size(), r.setoOrBigBo);
    if (!OpenBox(text)) { DebugLog("[FALLEN] tab confirm box could NOT be made - nothing done"); return; }
    g_boxDead = r.uid; g_boxMate = m.uid; g_boxPrice = g_read.price; g_boxName = r.name; g_boxMateName = m.name; g_boxText = text; g_boxSeto = r.setoOrBigBo;
    ++g_boxOpened;
    DebugLog("[FALLEN] tab confirm box up for uid " + S(r.uid) + " beside uid " + S(m.uid) + " price " + S(g_read.price) + ": '" + text + "'");
}
void BoxCancelBody()
{
    ++g_boxCancelled;
    DebugLog("[FALLEN] tab confirm box: CANCEL (uid " + S(g_boxDead) + ") - nothing done");
    DropBox();
}
void BoxConfirmBody()
{
    const unsigned int dead = g_boxDead, mate = g_boxMate;
    const long long price = g_boxPrice;
    const std::string name = g_boxName, mateName = g_boxMateName;
    DropBox();
    if (dead == 0 || mate == 0) return;
    ++g_confirmed;
    g_busy = 1; g_busyDead = dead; g_busyMate = mate; g_busyPrice = price; g_busyName = name; g_busyMateName = mateName;
    ClearNote();
    ApplyView();   /* the progress line, the button greyed - the road runs on the next frame */
    DebugLog("[FALLEN] tab confirm box: BRING BACK confirmed for uid " + S(dead) + " '" + name + "' beside uid " + S(mate) + " '" + mateName + "' at price " + S(price));
}
/* the confirmed bring-back, on the frame after the press: resurrect.cpp's road (the TEST lever's own) */
void RunBringBack()
{
    const unsigned int dead = g_busyDead, mate = g_busyMate;
    const long long price = g_busyPrice;
    const std::string name = g_busyName, mateName = g_busyMateName;
    g_busy = 0;
    const coop::BringBackOut out = coop::ResurrectBringBackFor(dead, mate, price);
    const std::string nm = out.name.empty() ? name : out.name, mt = out.mate.empty() ? mateName : out.mate;
    if (out.ok)
    {
        ++g_broughtBack;
        const std::string line = swtab::DoneLine(nm, mt, out.paid);
        const int shown = coop::StoreShowPlayerLine(playerstab::EscapeHash(line));   /* '#' doubled, as every caption: never a colour code */
        if (shown == 1) ++g_lineShown; else ++g_lineNotShown;
        if (g_selUid == dead) g_selUid = 0;
        ClearNote();
        DebugLog("[FALLEN] tab brought back uid " + S(dead) + ": message line '" + line + "' " + (shown == 1 ? "shown" : "NOT shown (" + S(shown) + ")"));
    }
    else
    {
        const int code = (out.refusal > 0 && out.refusal < swtab::kRefCodes) ? out.refusal : swtab::kRefFailed;
        ++g_refusals[code];
        SetNote(code, nm, mt, out.price, out.money);
        DebugLog("[FALLEN] tab refused (" + std::string(swtab::RefusalTag(code)) + "): '" + swtab::RefusalWords(code, nm, mt, out.price, out.money) + "'");
    }
    if (PageWhole()) Refresh();
}

void RowCatching(size_t index) { try { RowBody(index); } catch (...) { ++g_throws; } }
void OnRowPicked(MyGUI::MultiListBox*, size_t index) { __try { RowCatching(index); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void ListLessCatching(size_t column, const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
{ try { less = swtab::CellLess((int)column, Utf8(a), Utf8(b)); } catch (...) { ++g_throws; } }
void OnListLess(MyGUI::MultiListBox*, size_t column, const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
{ __try { ListLessCatching(column, a, b, less); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void PressCatching() { try { PressBody(); } catch (...) { ++g_throws; } }
void OnPress(MyGUI::Widget*) { __try { PressCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void BoxCancelCatching() { try { BoxCancelBody(); } catch (...) { ++g_throws; } }
void OnBoxCancel(MyGUI::Widget*) { __try { BoxCancelCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void BoxConfirmCatching() { try { BoxConfirmBody(); } catch (...) { ++g_throws; } }
void OnBoxConfirm(MyGUI::Widget*) { __try { BoxConfirmCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* the FALLEN tab chosen (after the game's handler titled the window FALLEN): the page read at once */
void TabChangedBody(MyGUI::TabControl* sender, size_t index)
{
    if (sender == 0 || sender != g_tabs || g_item == 0) return;
    ++g_tabEvents;
    if ((int)index != OurIndex()) return;
    ++g_tabOpened;
    g_mateUid = 0;   /* each opening starts the drop-down on the character selected in the game (the page) */
    const long long rebuilt = g_rebuilds;
    if (!coop::EngineWritesBlocked()) Refresh();
    if (g_rebuilds == rebuilt) LogTable();
    DebugLog("[FALLEN] tab opened (" + S((long long)g_rows.size()) + " row(s), " + S((long long)g_mates.size()) + " squadmate(s); fallenTab " + S(g_tabOpened) + ")");
}
void TabChangedCatching(MyGUI::TabControl* sender, size_t index) { try { TabChangedBody(sender, index); } catch (...) { ++g_throws; } }
void OnTabChanged(MyGUI::TabControl* sender, size_t index) { __try { TabChangedCatching(sender, index); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* Each frame: a confirmed bring-back run; with our tab in, the page read again while it shows (or the box is up) at most once
   a second, and the tab taken out - the window closed - once this is no multiplayer world or a widget of its page was destroyed;
   without it, the window's tab control looked for at most once a second (until kGiveUpAfter refused adds in this world). */
void TickBody()
{
    if (!coop::GameplayRunning() || coop::EngineWritesBlocked()) return;
    if (g_busy) RunBringBack();
    if (g_uiDead) return;
    if (g_item != 0)
    {
        if (!PageWhole()) { Remove("a widget of its page was destroyed"); return; }
        const bool shown = Shown(g_tabs);
        if (!shown && !Wanted()) { Remove("this is no multiplayer world"); return; }
        const bool ours = shown && (int)g_tabs->getIndexSelected() == OurIndex();
        coop::MgmtTabsFitTick(g_tabs);   /* the strip's captions measured once the window is open */
        if (ours) AdoptPick();
        if ((ours || g_box != 0) && ::GetTickCount() - g_readMs >= kReadEveryMs) Refresh();
        return;
    }
    if (!Wanted() || g_refusedThisWorld >= kGiveUpAfter) return;
    const unsigned long now = ::GetTickCount();
    if (g_looks != 0 && now - g_lookMs < kLookEveryMs) return;
    g_lookMs = now; ++g_looks;
    if (!UnlinkerOn()) return;
    MyGUI::TabControl* tabs = FindTabs();
    if (tabs != 0) Insert(tabs);
}
void TickCatching() { try { TickBody(); } catch (...) { ++g_throws; } }
void TickSeh()
{
    __try { TickCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; ErrorLog("[FALLEN] a memory fault in the FALLEN tab's frame work - the tab is no longer refreshed or added"); }
}
void DropBoxCatching() { try { DropBox(); } catch (...) { ++g_throws; } }
void DropBoxSeh() { __try { DropBoxCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* ---- the TEST-ONLY lever: each step fires the control's own handler, as a mouse would, and only where a mouse could ---- */
std::string LeverBody(const std::string& args)
{
    const swtab::Lever l = swtab::ParseLever(args);
    if (l.kind == swtab::kLeverBad) return "error resurrect tab usage: resurrect tab [open | select <row> | mate <i> | press | confirm | cancel | report]";
    if (l.kind == swtab::kLeverReport) { coop::ReportFallenTab(); return "ok resurrect tab"; }
    if (l.kind == swtab::kLeverConfirm || l.kind == swtab::kLeverCancel)
    {
        MyGUI::Widget* b = Find(l.kind == swtab::kLeverConfirm ? kBoxOk : kBoxCancel);
        if (!Reachable(b)) { DebugLog("[FALLEN] tab lever" + args + " REFUSED: no BRING BACK box on screen"); return "error resurrect tab no-box"; }
        b->eventMouseButtonClick(b);
        return l.kind == swtab::kLeverConfirm ? "ok resurrect tab confirm" : "ok resurrect tab cancel";
    }
    if (g_item == 0 || !PageWhole() || !Shown(g_tabs))
    { DebugLog("[FALLEN] tab lever" + args + " REFUSED: the management window is not open with the FALLEN tab in it"); return "error resurrect tab window-closed"; }
    if (l.kind == swtab::kLeverOpen)
    {
        const int at = OurIndex();
        if (at < 0) return "error resurrect tab not-in";
        g_tabs->setIndexSelected((size_t)at);
        g_tabs->eventTabChangeSelect(g_tabs, (size_t)at);
        DebugLog("[FALLEN] tab lever: the FALLEN tab chosen (control position " + S(at) + ")");
        return "ok resurrect tab open";
    }
    if (l.kind == swtab::kLeverSelect)
    {
        if (!Reachable(g_list) || l.n >= (int)g_rows.size()) { DebugLog("[FALLEN] tab lever" + args + " REFUSED: the list is not on screen or has no such row (" + S((long long)g_rows.size()) + " rows)"); return "error resurrect tab no-row"; }
        g_list->setIndexSelected((size_t)l.n);
        g_list->eventListChangePosition(g_list, (size_t)l.n);
        return "ok resurrect tab select";
    }
    if (l.kind == swtab::kLeverMate)
    {
        if (!Reachable(g_pick) || l.n >= (int)g_mates.size()) { DebugLog("[FALLEN] tab lever" + args + " REFUSED: the squadmate list is not on screen or has no such entry (" + S((long long)g_mates.size()) + ")"); return "error resurrect tab no-mate"; }
        g_pick->setIndexSelected((size_t)l.n);   /* where a mouse pick leaves the box; read back as each frame does */
        AdoptPick();
        return "ok resurrect tab mate";
    }
    if (!Reachable(g_btn)) { DebugLog("[FALLEN] tab lever" + args + " REFUSED: BRING BACK is not on screen or is greyed"); return "error resurrect tab no-button"; }
    g_btn->eventMouseButtonClick(g_btn);
    return "ok resurrect tab press";
}
std::string LeverCatching(const std::string& args) { try { return LeverBody(args); } catch (...) { ++g_throws; return "error resurrect tab threw"; } }

}   /* namespace */

namespace coop {

void FallenTabTick() { TickSeh(); }

void FallenTabTitleTick()
{
    if (g_box == 0) return;
    DropBoxSeh();
}

void FallenTabForgetWorld()
{
    g_refusedThisWorld = 0;
    g_rows.clear(); g_rowsEsc.clear(); g_mates.clear(); g_selUid = 0; g_mateUid = 0; g_pickSet = MyGUI::ITEM_NONE; ClearNote(); g_busy = 0;
    g_read = FallenTabRead(); g_readMs = 0;
    for (int k = 0; k < 3; ++k) g_shownPrice[k].clear();
    g_shownCaption.clear(); g_shownLabel.clear(); g_shownBtn.clear(); g_shownBtnOn = -1;
    if (g_box != 0) DropBoxSeh();   /* a box asks about a character of the world being left */
}

std::string FallenTabCommand(const std::string& args) { return LeverCatching(args); }

void ReportFallenTab()
{
    std::string ref;
    for (int i = 1; i < swtab::kRefCodes; ++i) ref += std::string(i > 1 ? "," : "") + swtab::RefusalTag(i);
    ref += "]=";
    for (int i = 1; i < swtab::kRefCodes; ++i) ref += std::string(i > 1 ? "," : "") + S(g_refusals[i]);
    DebugLog("[FALLEN] TAB REPORT uiDead=" + S(g_uiDead ? 1 : 0) + " tabIn=" + S(g_item != 0 ? 1 : 0) + " position=" + S(OurIndex())
             + " added=" + S(g_inserted) + " removed=" + S(g_removed) + " addRefused=" + S(g_insertRefused)
             + " fallenTab=" + S(g_tabOpened) + " rows=" + S((long long)g_rows.size()) + " squadmates=" + S((long long)g_mates.size())
             + " selected=" + S(g_selUid) + " picked=" + S(g_mateUid) + " selects=" + S(g_selects) + " picks=" + S(g_matePicks)
             + " presses=" + S(g_presses) + " pressGreyed=" + S(g_pressGreyed) + " boxOpened=" + S(g_boxOpened) + " boxCancelled=" + S(g_boxCancelled)
             + " boxClosedByChange=" + S(g_boxClosedByChange) + " confirmed=" + S(g_confirmed) + " broughtBack=" + S(g_broughtBack)
             + " refused[" + ref + " messageLine[shown,not]=" + S(g_lineShown) + "," + S(g_lineNotShown)
             + " rebuilds=" + S(g_rebuilds) + "," + S(g_mateRebuilds) + " tabEvents=" + S(g_tabEvents) + " looks=" + S(g_looks)
             + " faults=" + S(g_faults) + " throws=" + S(g_throws));
}

void* FallenTabItemPtr() { return g_item; }

}
