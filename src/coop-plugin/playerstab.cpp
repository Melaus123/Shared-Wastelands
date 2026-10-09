// playerstab.cpp - see playerstab.h. Engine facts (Read from decompiles of the 1.0.65 numbering):
//   ManagementScreen (Kenshi_OverviewWindow.layout, ctor 0x49EB20, built the first time the window opens and kept in a
//   global): its MyGUI::TabControl "TabsMain" (MAP FACTION RESEARCH CRAFTING SQUADS DIALOGUE AI) carries the layout's
//   per-instance prefix in its name ("<hex>_TabsMain"; no other layout of the game names a TabsMain). The ctor sets the tab
//   buttons' default width to the control's width / its item count, and makes its tab-change handler 0x49B500 the
//   control's eventTabChangeSelect (the new-style half of the EventPair, so a += of the same kind runs after it).
//   PLAYERS is inserted after AI, at position 7 (before FALLEN when that tab is in - fallentab.cpp): the game's own seven keep
//   their numbers. The handler 0x49B500 with 7: the
//   window caption from that tab's name (TabControl's IItemContainer::_getItemNameAt -> Window::setCaption, so the title
//   reads PLAYERS), then a switch on 0..6 with no case for 7 - nothing else runs, no sound. The game's other reads of the
//   selected position compare it with fixed numbers (0x49AAC0 ==0, 0x49AC50 ==3/==5, 0x499A30 ==2, 0x4996A0 ==5, 0x495370
//   ==2, 0x494060 ==2; through the getter 0x48B100 the main bar's toggle 0x728220 ==the asked tab, the tutorial pointers
//   0x971B80 ==3 and 0x98F390 !=2 / !=3), none matching 7; its one item read is getItemAt(3) (0x48B120, the tutorial's
//   CRAFTING button), still CRAFTING. The show function 0x49B780 is only called with the game's numbers 0..6.
//   PlayerInterface::makeKnown 0x7F72F0(PlayerInterface*, Faction*): inserts the faction into the player faction's known set
//   (PlayerInterface+0x2A0 = the player Faction*, its FactionRelations+0x68); the engine calls it on discovery (0x92E7E0
//   "Discovered {1}"), on meeting (0x858500), at a new game's start (0x871F30) and on loading relations (0x6B3D80). The FACTION
//   tab's refresh 0x498ED0 lists a faction of the player's relation map that is in that set, whose byte Faction+0x1D0 is 0 and
//   whose stringID is not GENERATION_TEMPLATE_FACTION. Faction+0x1D0 is the faction record's bool "not real" (Faction setup
//   0x7FD6D0 copies it from GameData bools); Faction+0x250 is the PlayerInterface of a player faction.
#include "playerstab.h"
#include "coop_log.h"
#include "addresses.h"
#include "ui.h"   /* UiFindLayoutSuffix: the tab control under the layout's prefix */
#include "fallentab.h"   /* FallenTabItemPtr - FALLEN shares the strip and comes after PLAYERS */
#include "playerfaction.h"
#include "relations.h"
#include "store.h"
#include "team.h"   /* T-546 step 4: TeamSameAnyThread - towards a teammate only ALLY */
#include "chat.h"   /* MESSAGE opens the chat with TO set to the selected player */
#include "../common/chatwire.h"   /* MESSAGE's word */
#include "../common/teamscreens.h"   /* the faction screens: the bottom line's actions, the boxes' words */
#include "soak.h"
#include "net/session.h"
#include "game/GameWorld.h"
#include "game/Faction.h"
#include "../common/playerstab.h"
#include "../common/fallentab.h"   /* StripButtonWidth */
#include "../common/slotwire.h"
#include "../common/panelstatus.h"   /* NoticeBoxH / NoticeLayoutIn - the mod's message-box sizing */
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_TabControl.h>
#include <mygui/MyGUI_TabItem.h>
#include <mygui/MyGUI_MultiListBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_WidgetManager.h>
#include <mygui/MyGUI_IUnlinkWidget.h>
#include <mygui/MyGUI_ISubWidgetText.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>

namespace {

unsigned long long kMakeKnownRva = 0; static coop::AddrReg kMakeKnownRva_reg("PlayerInterfaceMakeKnown", &kMakeKnownRva);   /* Steam_1.0.65 0x7F72F0 */
typedef void (*MakeKnownFn)(void* playerInterface, ::Faction* f);

const char* const kTabsSuffix = "TabsMain";   /* the management window's tab control, after the layout's prefix */
const unsigned long kLookEveryMs = 1000;      /* how often the widget tree is searched for it while our tab is not in */
const unsigned long kRowsEveryMs = 1000;      /* the rows rebuilt at least this often while they show (roster names, online) */
const int kTextMargin = 4;                     /* a list cell's text margin inside its column (Kenshi_MultiListBox's item skin) */
const int kGiveUpAfter = 5;                   /* refused adds in one world, after which the tab control is not looked for again */
const int kFacNotRealOff = 0x1D0, kFacPlayerIfOff = 0x250, kPiFactionOff = 0x2A0;

/* our widgets: "SW" names, no '_' (ui.cpp's rule - never matched by another mod's suffix test) */
const char* const kListName   = "SWPlayersList";
const char* const kLineName   = "SWPlayersStanceLine";
const char* const kSelectName = "SWPlayersSelectLine";
const char* const kBtnName[3] = { "SWPlayersAllyBtn", "SWPlayersNeutralBtn", "SWPlayersHostileBtn" };
const char* const kActName[2] = { "SWPlayersLeftActBtn", "SWPlayersRightActBtn" };   /* the bottom line's action buttons */
const char* const kMsgName = "SWPlayersMessageBtn";   /* MESSAGE: the chat with TO set to the selected player */
/* the one box this module shows at a time: SET HOSTILE, FACTION INVITATION, LEAVE / REMOVE FROM / DISBAND FACTION, FACTION */
const char* const kBoxName    = "SWFactionBox";
const char* const kBoxText    = "SWFactionBoxText";
const char* const kBoxCancel  = "SWFactionBoxLeftBtn";
const char* const kBoxOk      = "SWFactionBoxRightBtn";
const char* const kNameSuffix = "FactionNameText";   /* the FACTION tab's FACTION NAME box (Kenshi_OverviewWindow.layout) */

bool g_uiDead = false;               /* a memory fault in this module's widget work: no more tab added, no more refreshes */
unsigned long g_lookMs = 0;          /* the last search for the tab control */
MyGUI::TabControl* g_tabs = 0;       /* the tab control our tab was added to, and our tab */
MyGUI::TabItem* g_item = 0;
/* our page's widgets and the HOSTILE box, kept from when they were made; MyGUI's destroy notice (Unlinker) drops each pointer
   before its widget is freed, so no frame searches the widget tree for them */
MyGUI::MultiListBox* g_list = 0;
MyGUI::TextBox* g_line = 0;
MyGUI::TextBox* g_select = 0;
MyGUI::Button* g_btn[3] = { 0, 0, 0 };
MyGUI::Widget* g_box = 0;
std::string g_fitShown;              /* the tab widths MgmtTabsFit last logged */
bool g_unlinkerOn = false;
int g_refusedThisWorld = 0;
long long g_rowsEpoch = -1;          /* RelationsPlayerPairEpoch when the rows were last built */
unsigned long g_rowsMs = 0;
std::vector<playerstab::RowView> g_rows;   /* the table as shown, row = the list's insertion index */
int g_selSlot = -1;                  /* the selected player's slot */
int g_boxSlot = -1;                  /* the slot the box asks about (SET HOSTILE, REMOVE FROM FACTION, the inviter) */
int g_boxKind = 0;                   /* teamscreen::kBox* of the box up (kBoxNone = none) */
unsigned g_boxSerial = 0;            /* the invitation the FACTION INVITATION box answers */
std::string g_boxTeam, g_boxWho, g_boxText;   /* the faction and the player the box names, and its words */
/* the bottom line's two action buttons (left, right), the action each shows (teamscreen::kAct*), and the view last applied */
MyGUI::Button* g_act[2] = { 0, 0 };
int g_actShown[2] = { 0, 0 };
MyGUI::Button* g_msg = 0;            /* MESSAGE, at the right end of the text line */
int g_msgShown = -1;
std::string g_shownView;
teamscreen::View g_view;
bool g_viewSelected = false;
std::vector<std::string> g_awayOwed; /* FACTION boxes owed (a removal while away), by the faction's name */
unsigned long g_inviteNameSaid = 0;  /* the invitation whose box waits for the inviter's name - said once */
unsigned long g_inviteOtherSaid = 0; /* the invitation whose box waits for another box that blocks the game - said once */
unsigned long g_boxTryMs = 0;        /* the last box that came by itself and could not be opened: tried again a second later */
unsigned g_boxRetrySaid = 0;         /* a box that could not be opened is logged once, not on every retry */
/* a box's button press, RECORDED by its click handler (1 the left button, 2 the right) with the box it was pressed on, and
   acted on by the next tick (BoxPressTick) - a widget is never destroyed inside its own click (ui.cpp's rule) */
volatile LONG g_boxPress = 0;
MyGUI::Widget* g_boxPressedOn = 0;
/* the FACTION NAME box, found under the management window, and whether this module disabled it (a member's is locked, 478) */
MyGUI::EditBox* g_nameBox = 0;
bool g_nameLocked = false;
unsigned long g_nameLookMs = 0;
/* this player's place in a faction of players, read with the rows (team.cpp's copy of the table) */
struct TeamCtx { int role; std::string team; int founder; std::string founderName; TeamCtx() : role(0), founder(-1) {} };
TeamCtx g_ctx;
long long g_actPressed = 0, g_actNothing = 0, g_invitesSent = 0, g_requestsSent = 0, g_requestsRefused = 0, g_boxesShown = 0, g_inviteBoxes = 0,
          g_awayBoxes = 0, g_nameLocks = 0;
std::string g_shownLine, g_shownSelect;
int g_shownTicked = -2, g_shownSelected = -1, g_shownLocked = -1;
std::map<int, std::string> g_names;  /* the roster's player name per slot, kept for the session */
/* the other players' factions this world carries from its save (record id coop-p<n>) - one walk of the faction list per world */
bool g_carriedWalked = false;
std::vector<std::pair<int, ::Faction*> > g_carried;
std::set< ::Faction*> g_known;       /* made known in this world */
long long g_tabOpened = 0, g_stanceSet = 0, g_hostileConfirmed = 0, g_boxOpened = 0, g_boxCancelled = 0, g_stanceRefused = 0,
          g_inserted = 0, g_removed = 0, g_insertRefused = 0, g_rebuilds = 0, g_looks = 0, g_tabEvents = 0, g_knownMade = 0, g_knownFaults = 0, g_knownNoInterface = 0, g_faults = 0, g_throws = 0;

template <class T> std::string S(const T& v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

/* ---- guarded reads of engine memory (no C++ objects in these frames) ---- */
void* ReadPtrPod(const void* p, int off)
{
    __try { return *(void* const*)((const char*)p + off); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadBytePod(const void* p, int off)
{
    __try { return (int)*((const unsigned char*)p + off); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int MakeKnownPod(void* pi, ::Faction* f)
{
    __try { ((MakeKnownFn)(Base() + kMakeKnownRva))(pi, f); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int ReadFactionArrayPod(void*** arr, unsigned* n)
{
    __try { *arr = *(void***)((char*)coop::GameWorldPtr()->factionDirectory + 0x10); *n = *(unsigned*)((char*)coop::GameWorldPtr()->factionDirectory + 8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void ForgetTab();
/* MyGUI's destroy notice: WidgetManager calls each registered unlinker for every widget it destroys, parents before their
   children, while the widget still exists. A pointer we keep to that widget, or to anything inside it, is dropped then. */
bool Inside(MyGUI::Widget* mine, MyGUI::Widget* gone)
{
    for (MyGUI::Widget* p = mine; p != 0; p = p->getParent()) if (p == gone) return true;
    return false;
}
class Unlinker : public MyGUI::IUnlinkWidget
{
public:
    void _unlinkWidget(MyGUI::Widget* w)
    {
        if (w == 0) return;
        if (g_box != 0 && Inside(g_box, w)) { g_box = 0; g_boxSlot = -1; g_boxKind = 0; }
        if (g_nameBox != 0 && Inside(g_nameBox, w)) { g_nameBox = 0; g_nameLocked = false; }
        if (g_item != 0 && Inside(g_item, w)) { ForgetTab(); return; }   /* the window, its tab control or our tab: all of it goes */
        if (g_list != 0 && Inside(g_list, w)) g_list = 0;
        if (g_line != 0 && Inside(g_line, w)) g_line = 0;
        if (g_select != 0 && Inside(g_select, w)) g_select = 0;
        for (int k = 0; k < 3; ++k) if (g_btn[k] != 0 && Inside(g_btn[k], w)) g_btn[k] = 0;
        for (int k = 0; k < 2; ++k) if (g_act[k] != 0 && Inside(g_act[k], w)) g_act[k] = 0;
        if (g_msg != 0 && Inside(g_msg, w)) g_msg = 0;
    }
};
/* registered once, before our first widget is made; without it no tab is added. The listener is made once on the heap and
   never freed, so it outlives every call WidgetManager makes to it, at exit too. */
bool UnlinkerOn()
{
    if (g_unlinkerOn) return true;
    MyGUI::WidgetManager* wm = MyGUI::WidgetManager::getInstancePtr();
    if (wm == 0) return false;
    wm->registerUnlinker(new Unlinker());
    g_unlinkerOn = true;
    return true;
}
bool PageWhole() { return g_list != 0 && g_line != 0 && g_select != 0 && g_btn[0] != 0 && g_btn[1] != 0 && g_btn[2] != 0 && g_act[0] != 0 && g_act[1] != 0; }

/* the management window's tab control, or 0 while the window has not been built (it is built the first time it opens) */
MyGUI::TabControl* FindTabs()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* hit = 0;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (hit == 0 && roots.next()) hit = coop::UiFindLayoutSuffix(roots.current(), kTabsSuffix);
    return hit != 0 ? hit->castType<MyGUI::TabControl>(false) : 0;
}
/* the widget and every parent shown: the management window is open */
bool Shown(MyGUI::Widget* w)
{
    if (w == 0) return false;
    for (MyGUI::Widget* p = w; p != 0; p = p->getParent()) if (!p->getVisible()) return false;
    return true;
}
/* our tab is at its place in this control: the game's seven, then ours (FALLEN may follow) */
bool OursIn(MyGUI::TabControl* tabs)
{
    if (tabs == 0 || tabs != g_tabs || g_item == 0) return false;
    if (tabs->getItemCount() <= (size_t)playerstab::kTabIndex) return false;
    return tabs->getItemAt((size_t)playerstab::kTabIndex) == g_item;
}

/* ---- the other players of this world: the stand-in table (players seen this session) and the stand-ins the save carries ---- */
void WalkCarried()
{
    g_carriedWalked = true;
    g_carried.clear();
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return;
    void** arr = 0; unsigned n = 0;
    if (!ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) return;
    const int me = coop::MySlotForWire();
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!Plaus(f)) continue;
        const int s = coop::StandInRecordSlot(f);
        if (s >= 0 && s != me) g_carried.push_back(std::make_pair(s, f));
    }
}
struct Other { int slot; ::Faction* f; };
std::vector<Other> OtherPlayers()
{
    std::vector<Other> out;
    if (!g_carriedWalked) WalkCarried();
    int slots[coop::kStandInTableCap]; ::Faction* fs[coop::kStandInTableCap];
    const int n = coop::StandInList(slots, fs, coop::kStandInTableCap);
    const int me = coop::MySlotForWire();
    std::set<int> seen;
    for (int i = 0; i < n; ++i) if (slots[i] != me && seen.insert(slots[i]).second) { Other o; o.slot = slots[i]; o.f = fs[i]; out.push_back(o); }
    for (size_t i = 0; i < g_carried.size(); ++i)
        if (g_carried[i].first != me && seen.insert(g_carried[i].first).second) { Other o; o.slot = g_carried[i].first; o.f = g_carried[i].second; out.push_back(o); }
    return out;
}
std::string FactionWords(::Faction* f)
{
    const std::string shown = coop::StandInDisplayName(f);
    return shown.empty() ? f->getName() : shown;
}
bool Online(int slot)
{
    if (coop::StoreRosterSlotInWorld(slot) == 1) return true;
    return slot == coop::LinkPeerSlot() && coop::net::SessionLinked();
}
std::vector<playerstab::RowView> BuildRows()
{
    std::vector<playerstab::RowView> rows;
    g_ctx.role = coop::TeamMyRole(&g_ctx.team, &g_ctx.founder);
    g_ctx.founderName = g_ctx.role != 0 ? coop::TeamPlayerName(g_ctx.founder) : std::string();
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine)) return rows;
    const std::vector<Other> others = OtherPlayers();
    for (size_t i = 0; i < others.size(); ++i)
    {
        playerstab::RowIn in;
        in.slot = others[i].slot;
        std::string nm;
        if (coop::StoreRosterNameOf(in.slot, &nm) == 1) { in.rosterName = nm; g_names[in.slot] = nm; }
        std::map<int, std::string>::const_iterator it = g_names.find(in.slot);
        if (it != g_names.end()) in.rememberedName = it->second;
        in.placeholder = coopslot::PlaceholderName(in.slot);
        in.faction = FactionWords(others[i].f);
        coop::RelationsStanceLevels(mine, others[i].f, &in.you, &in.them);
        in.online = Online(in.slot);
        in.teammate = coop::TeamSameAnyThread(coop::StoreMySlot(), in.slot);
        in.founderSets = coop::TeamStanceIsFounders(in.slot);
        in.mateFounder = in.teammate && coop::TeamIsFounder(in.slot);
        in.inTeam = coop::TeamNoOfSlotAnyThread(in.slot) != 0;
        { std::string line2; bool mate = false; coop::TeamTagFor(in.slot, in.faction, &line2, &mate); in.faction = line2; }   /* in a faction of players: its name (478) */
        rows.push_back(playerstab::RowFor(in));
    }
    playerstab::SortBySlot(&rows);
    return rows;
}
::Faction* FactionOfSlot(int slot)
{
    const std::vector<Other> others = OtherPlayers();
    for (size_t i = 0; i < others.size(); ++i) if (others[i].slot == slot) return others[i].f;
    return 0;
}

/* ---- [FACTSCR] other players' factions made known: the FACTION tab lists them with their numbers ---- */
void MakeOthersKnown()
{
    if (kMakeKnownRva == 0) return;
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine)) return;
    void* pi = ReadPtrPod(mine, kFacPlayerIfOff);
    if (!Plaus(pi) || ReadPtrPod(pi, kPiFactionOff) != (void*)mine) { if (++g_knownNoInterface == 1) DebugLog("[FACTSCR] this game's player faction has no PlayerInterface that names it back - nothing made known"); return; }
    const std::vector<Other> others = OtherPlayers();
    std::string made;
    for (size_t i = 0; i < others.size(); ++i)
    {
        ::Faction* f = others[i].f;
        if (!Plaus(f) || g_known.count(f) != 0) continue;
        int reads = 0;
        coop::RelationsTagLevel(mine, f, &reads);   /* the player's relation map holds the faction (the FACTION tab walks that map) */
        if (MakeKnownPod(pi, f) != 1)
        {
            g_known.insert(f);   /* tried: not entered again in this world */
            if (++g_knownFaults == 1) ErrorLog("[FACTSCR] makeKnown 0x7F72F0 faulted for slot " + S(others[i].slot) + " - that faction is not listed on the FACTION tab in this world (not tried again)");
            continue;
        }
        g_known.insert(f); ++g_knownMade;
        made += (made.empty() ? "" : ",") + S(others[i].slot) + ":" + S(ReadBytePod(f, kFacNotRealOff));
    }
    if (!made.empty())
        DebugLog("[FACTSCR] listed=" + S((long long)g_known.size()) + " hiddenFlag=" + made
                 + " (slot:Faction+0x1D0 of each other player's faction made known now - the record's \"not real\" bool, which"
                   " must read 0 for the FACTION tab to list it; makeKnown 0x7F72F0)");
}

/* ---- widgets ---- */
int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
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
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }   /* Kenshi's light caption colour, as the mod's boxes */
/* the control can be reached by a mouse: it and every parent shown, and it enabled */
bool Reachable(MyGUI::Widget* w) { return w != 0 && w->getEnabled() && Shown(w); }
void ForgetTab()
{
    g_act[0] = g_act[1] = 0; g_actShown[0] = g_actShown[1] = 0; g_shownView.clear(); g_view = teamscreen::View(); g_viewSelected = false;
    g_tabs = 0; g_item = 0; g_list = 0; g_line = 0; g_select = 0; g_btn[0] = g_btn[1] = g_btn[2] = 0;
    g_msg = 0; g_msgShown = -1;
    g_rows.clear(); g_selSlot = -1; g_shownLine.clear(); g_shownSelect.clear(); g_shownTicked = -2; g_shownSelected = -1; g_shownLocked = -1; g_rowsEpoch = -1;
}

void OnRowPicked(MyGUI::MultiListBox* list, size_t index);
void OnTabChanged(MyGUI::TabControl* sender, size_t index);
void OnStanceClicked(MyGUI::Widget* w);
void OnBoxCancel(MyGUI::Widget* w);
void OnBoxConfirm(MyGUI::Widget* w);
void OnActClicked(MyGUI::Widget* w);
void OnMessageClicked(MyGUI::Widget* w);

/* THE PAGE (mock-up A4.2): the table (Kenshi's own Kenshi_MultiListBox, as the Load Game list) over the tab's height, the
   stance line (painted text, the window's label face) and the three tick buttons (Kenshi_TickButton1) along the bottom. Sizes
   follow the tab's own size, as the FACTION tab's layout numbers do. */
bool BuildPage(MyGUI::TabItem* item)
{
    const int W = item->getWidth(), H = item->getHeight();
    const int pad = Clamp(H * 12 / 1000, 4, 12);
    const int btnH = Clamp(H * 40 / 1000, 22, 40), gap = Clamp(W / 100, 6, 14);
    /* the bottom row: the three stance buttons from the left and INVITE TO FACTION at the right show together, so the stance
       buttons take what the right-hand button leaves, a gap in from each side frame and between them */
    const int actW = Clamp(W * 26 / 100, 150, 300);
    const int btnW = Clamp((W - actW - 5 * gap) / 3, 60, Clamp(W * 16 / 100, 96, 200));
    const int lineH = Clamp(H * 32 / 1000, 18, 32);
    const int btnY = H - pad - btnH;
    const int lineY = btnY - pad / 2 - lineH;
    const int listY = pad, listH = lineY - pad - listY;
    if (listH < 60) return false;
    MyGUI::Widget* lw = Mk(item, "MultiListBox", "Kenshi_MultiListBox", 0, listY, W, listH, MyGUI::Align::Stretch, kListName);
    MyGUI::Widget* line = Mk(item, "TextBox", "Kenshi_TextboxPaintedText", 0, lineY, W, lineH, MyGUI::Align::HStretch | MyGUI::Align::Bottom, kLineName);
    MyGUI::Widget* sel = Mk(item, "TextBox", "Kenshi_TextboxStandardText", 0, lineY, W, lineH, MyGUI::Align::HStretch | MyGUI::Align::Bottom, kSelectName);
    MyGUI::MultiListBox* list = lw != 0 ? lw->castType<MyGUI::MultiListBox>(false) : 0;
    MyGUI::TextBox* lt = line != 0 ? line->castType<MyGUI::TextBox>(false) : 0;
    MyGUI::TextBox* st = sel != 0 ? sel->castType<MyGUI::TextBox>(false) : 0;
    if (list == 0 || lt == 0 || st == 0) return false;
    g_list = list; g_line = lt; g_select = st;
    /* the line's text starts where the list's text does (the list's client area inside its frame, plus the column's own text
       margin), not on the window's frame */
    {
        MyGUI::Widget* lc = list->getClientWidget();
        const int inset = Clamp((lc != 0 && lc != list ? lc->getAbsoluteCoord().left - list->getAbsoluteCoord().left : 0) + kTextMargin, kTextMargin, W / 4);
        lt->setCoord(MyGUI::IntCoord(inset, lineY, W - inset, lineH));
        st->setCoord(MyGUI::IntCoord(inset, lineY, W - inset, lineH));
        DebugLog("[UI] rect playerstab line inset x=" + S(inset) + " (the list's client area " + S(lc != 0 && lc != list ? lc->getAbsoluteCoord().left - list->getAbsoluteCoord().left : -1) + " + " + S(kTextMargin) + ")");
    }
    int cw[playerstab::kColumns];
    playerstab::ColumnWidths(W - 32 > 0 ? W - 32 : 0, cw);   /* the inner width: less the skin's frame and its scroll bar (ui.cpp's lists) */
    for (int c = 0; c < playerstab::kColumns; ++c) list->addColumn(MyGUI::UString(playerstab::ColumnHead(c)), cw[c]);
    list->eventListChangePosition += MyGUI::newDelegate(OnRowPicked);
    lt->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    lt->setCaption(MyGUI::UString(""));
    lt->setVisible(false);
    st->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    st->setTextColour(Lit());
    st->setCaption(MyGUI::UString(playerstab::kSelectLine));
    for (int k = 0; k < 3; ++k)
    {
        MyGUI::Widget* bw = Mk(item, "Button", "Kenshi_TickButton1", gap + k * (btnW + gap), btnY, btnW, btnH, MyGUI::Align::Left | MyGUI::Align::Bottom, kBtnName[k]);
        MyGUI::Button* b = bw != 0 ? bw->castType<MyGUI::Button>(false) : 0;
        if (b == 0) return false;
        g_btn[k] = b;
        b->setCaption(MyGUI::UString(playerstab::ButtonCaption(k)));
        b->eventMouseButtonClick += MyGUI::newDelegate(OnStanceClicked);
        b->setVisible(false);
    }
    /* the faction screens' two action buttons on the same row: one at the left edge (where the stance buttons are, which never
       show with it), one at the right edge; Kenshi_Button2 lit by hand, as the mod's boxes */
    for (int k = 0; k < 2; ++k)
    {
        MyGUI::Widget* aw = Mk(item, "Button", "Kenshi_Button2", k == 0 ? gap : W - actW - gap, btnY, actW, btnH,
                               (k == 0 ? MyGUI::Align::Left : MyGUI::Align::Right) | MyGUI::Align::Bottom, kActName[k]);
        MyGUI::Button* a = aw != 0 ? aw->castType<MyGUI::Button>(false) : 0;
        if (a == 0) return false;
        g_act[k] = a;
        a->setTextColour(Lit());
        a->setCaption(MyGUI::UString(""));
        a->eventMouseButtonClick += MyGUI::newDelegate(OnActClicked);
        a->setVisible(false);
    }
    /* MESSAGE: at the right end of the text line above the buttons, the same place for every selected player (the bottom row is
       full when the stance buttons show); the line's text ends a gap before it. The same style as the action buttons. */
    {
        const int msgW = Clamp(W * 14 / 100, 100, 200);
        const int msgX = W - gap - msgW;
        const int msgY = lineY + (lineH - btnH) / 2;
        MyGUI::Widget* mw = Mk(item, "Button", "Kenshi_Button2", msgX, msgY, msgW, btnH, MyGUI::Align::Right | MyGUI::Align::Bottom, kMsgName);
        MyGUI::Button* m = mw != 0 ? mw->castType<MyGUI::Button>(false) : 0;
        if (m == 0) return false;
        g_msg = m; g_msgShown = -1;
        m->setTextColour(Lit());
        m->setCaption(MyGUI::UString(chatwire::MessageWord()));
        m->eventMouseButtonClick += MyGUI::newDelegate(OnMessageClicked);
        m->setVisible(false);
        const MyGUI::IntCoord lc = lt->getCoord();
        const int lineW = msgX - gap - lc.left;
        lt->setCoord(MyGUI::IntCoord(lc.left, lc.top, lineW > 1 ? lineW : 1, lc.height));
        DebugLog("[UI] rect playerstab message x=" + S(msgX) + " y=" + S(msgY) + " w=" + S(msgW) + " h=" + S(btnH) + " on the text line (its text x="
                 + S(lc.left) + " w=" + S(lineW) + "; clear of the bottom buttons: " + (msgY + btnH <= btnY ? "yes" : "NO") + ")");
    }
    DebugLog("[UI] rect playerstab actions w=" + S(actW) + " h=" + S(btnH) + " left x=" + S(gap) + " right x=" + S(W - actW - gap) + " y=" + S(btnY)
             + " stance buttons x=" + S(gap) + ".." + S(gap + 3 * btnW + 2 * gap) + " (clear of the right-hand button: " + (gap + 3 * btnW + 2 * gap <= W - actW - gap ? "yes" : "NO") + ")");
    const MyGUI::IntCoord c = item->getAbsoluteCoord();
    DebugLog("[UI] rect playerstab x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height)
             + " list h=" + S(listH) + " line y=" + S(lineY) + " buttons y=" + S(btnY) + " w=" + S(btnW) + " h=" + S(btnH));
    return true;
}

/* Our tab after the game's seven (before FALLEN when it is in), its page built, the tab buttons narrowed so the tabs fill the
   strip the seven did (MgmtTabsFit), and our handler on the control's tab-change event (after the game's own, which titles the
   window from the tab's name). */
/* a refused add: counted, the first lines logged, and after kGiveUpAfter in one world the control is no longer looked for */
void Refused(const std::string& why)
{
    ++g_insertRefused;
    if (++g_refusedThisWorld <= 3) DebugLog("[PLAYERS] tab NOT added: " + why);
    if (g_refusedThisWorld == kGiveUpAfter) DebugLog("[PLAYERS] tab NOT added " + S(kGiveUpAfter) + " times in this world - not tried again until the next world");
}
void Insert(MyGUI::TabControl* tabs)
{
    const size_t n = tabs->getItemCount();
    MyGUI::TabItem* fallen = (MyGUI::TabItem*)coop::FallenTabItemPtr();
    const bool beforeFallen = fallen != 0 && n == (size_t)playerstab::kEngineTabCount + 1 && tabs->getItemAt((size_t)playerstab::kEngineTabCount) == fallen;
    if (n != (size_t)playerstab::kEngineTabCount && !beforeFallen)
    {
        Refused("the management window holds " + S((long long)n) + " tabs, not the game's " + S(playerstab::kEngineTabCount) + (fallen != 0 ? " and FALLEN" : ""));
        return;
    }
    const int bw = tabs->getButtonDefaultWidth();
    coop::MgmtTabsFit(tabs);   /* the game's own width noted while the control holds its seven */
    MyGUI::TabItem* fac = tabs->getItemAt((size_t)playerstab::kFactionTab);
    MyGUI::TabItem* item = tabs->insertItemAt((size_t)playerstab::kTabIndex, MyGUI::UString(playerstab::kTabCaption));
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
        /* our tab is in the control only with its whole page */
        tabs->removeItemAt((size_t)playerstab::kTabIndex);
        coop::MgmtTabsFit(tabs);
        ForgetTab();
        Refused("its page could not be built (tab " + S(iw) + " x " + S(ih) + ") - the window keeps the game's seven tabs");
        return;
    }
    /* a removal first, so a second insert into the same control never adds the handler twice */
    tabs->eventTabChangeSelect -= MyGUI::newDelegate(OnTabChanged);
    tabs->eventTabChangeSelect += MyGUI::newDelegate(OnTabChanged);
    ++g_inserted;
    DebugLog("[PLAYERS] tab added after AI (position " + S(playerstab::kTabIndex) + " of " + S((long long)tabs->getItemCount()) + (beforeFallen ? ", before FALLEN" : "")
             + "; tab buttons " + S(bw) + " -> " + S(tabs->getButtonDefaultWidth()) + " px wide)");
}
/* our tab out of the control (wherever it sits now), the tab buttons refitted to the tabs left */
void Remove(MyGUI::TabControl* tabs, const char* why)
{
    const size_t n = tabs->getItemCount();
    for (size_t i = 0; i < n; ++i) if (tabs->getItemAt(i) == g_item) { tabs->removeItemAt(i); break; }
    coop::MgmtTabsFit(tabs);
    ForgetTab();
    ++g_removed;
    DebugLog(std::string("[PLAYERS] tab removed (") + why + "; tab buttons " + S(tabs->getButtonDefaultWidth()) + " px wide)");
}
/* a world with other players: a multiplayer world, or one where another player's faction is already here */
bool Wanted() { return coop::HandSaveBlocked() || coop::StandInAnyAnyThread() != 0; }

/* ---- the table and the line under it ---- */
/* the faction screens' view of the bottom line for the rows and the selection (src/common/teamscreens.h) */
teamscreen::View ViewNow(const std::vector<playerstab::RowView>& rows, int selSlot)
{
    teamscreen::Sel s; s.myRole = g_ctx.role; s.founder = g_ctx.founderName;
    const int i = selSlot < 0 ? -1 : playerstab::RowOfSlot(rows, selSlot);
    if (i >= 0)
    {
        const playerstab::RowView& r = rows[(size_t)i];
        s.mate = r.teammate; s.inTeam = r.inTeam; s.online = r.online; s.you = r.you; s.name = r.name; s.faction = r.faction;
    }
    return teamscreen::ViewFor(i >= 0, s);
}
std::string Num(int v) { return S(v); }
/* `s` on the line, cut at a whole character and ended with "..." when it is wider than the line (a long faction name runs up to
   MESSAGE); a cut never leaves half of an escaped '#' ("##") */
void FitCaption(MyGUI::TextBox* t, const std::string& s)
{
    t->setCaption(MyGUI::UString(s.c_str()));
    const int w = t->getWidth();
    if (w <= 1 || t->getTextSize().width <= w) return;
    std::string cut = s;
    while (!cut.empty())
    {
        size_t n = cut.size() - 1;
        while (n > 0 && ((unsigned char)cut[n] & 0xC0) == 0x80) --n;   /* back to the start of the last character */
        cut.erase(n);
        size_t hashes = 0;
        for (size_t k = cut.size(); k > 0 && cut[k - 1] == '#'; --k) ++hashes;
        if (hashes % 2 == 1) cut.erase(cut.size() - 1);
        t->setCaption(MyGUI::UString((cut + "...").c_str()));
        if (t->getTextSize().width <= w) return;
    }
}

void ApplyBottom(const std::vector<playerstab::RowView>& rows)
{
    const playerstab::Bottom b = playerstab::BottomFor(rows, g_selSlot);
    if (!b.selected) g_selSlot = -1;
    MyGUI::TextBox* lt = g_line;
    MyGUI::TextBox* st = g_select;
    if (lt == 0 || st == 0) return;
    const teamscreen::View v = ViewNow(rows, g_selSlot);
    const std::string line = v.lineKind == teamscreen::kLineStance ? b.line : v.line;
    const std::string sig = std::string(b.selected ? "1|" : "0|") + line + "|" + (v.stance ? "1" : "0") + "|" + Num(v.left) + "|" + Num(v.right);
    g_view = v; g_view.line = line; g_viewSelected = b.selected;
    if (sig != g_shownView)
    {
        g_shownView = sig;
        lt->setVisible(b.selected && !line.empty());
        st->setVisible(!b.selected);
        if (b.selected) FitCaption(lt, line);
        for (int k = 0; k < 3; ++k) if (g_btn[k] != 0) g_btn[k]->setVisible(b.selected && v.stance);
        const int acts[2] = { v.left, v.right };
        for (int k = 0; k < 2; ++k)
        {
            if (g_act[k] == 0) continue;
            g_actShown[k] = acts[k];
            g_act[k]->setCaption(MyGUI::UString(teamscreen::ActionCaption(acts[k])));
            g_act[k]->setVisible(acts[k] != teamscreen::kActNone);
        }
        DebugLog("[PLAYERS] bottom line: '" + (b.selected ? line : std::string(playerstab::kSelectLine)) + "' stance buttons " + (b.selected && v.stance ? "shown" : "hidden")
                 + ", left '" + teamscreen::ActionCaption(v.left) + "', right '" + teamscreen::ActionCaption(v.right) + "' (selected slot " + S(g_selSlot) + ", role " + S(g_ctx.role) + ")");
    }
    if (g_msg != 0 && (b.selected ? 1 : 0) != g_msgShown) { g_msgShown = b.selected ? 1 : 0; g_msg->setVisible(b.selected); }   /* MESSAGE: while a player is selected */
    if (b.ticked != g_shownTicked)
    {
        g_shownTicked = b.ticked;
        for (int k = 0; k < 3; ++k) if (g_btn[k] != 0) g_btn[k]->setStateSelected(k == b.ticked);
    }
    /* T-546 step 4: towards a teammate NEUTRAL and HOSTILE are disabled (the skin's own disabled look) */
    if (playerstab::LockLook(b) != g_shownLocked)
    {
        g_shownLocked = playerstab::LockLook(b);
        for (int k = 0; k < 3; ++k) if (g_btn[k] != 0) g_btn[k]->setEnabled(playerstab::ButtonEnabled(b, k));
    }
}
/* the rows' inputs moved since they were built: a standing with this game's player faction (RelationsPlayerPairEpoch), or
   kRowsEveryMs passed (the roster's names and who is online, a player's faction arriving) */
bool RowsStale()
{
    if (coop::TeamMatesSettling()) return false;   /* the teammates just changed: the cells wait for the values the restore settles on */
    return coop::RelationsPlayerPairEpoch() != g_rowsEpoch || ::GetTickCount() - g_rowsMs >= kRowsEveryMs;
}
void Refresh()
{
    MyGUI::MultiListBox* list = g_list;
    if (list == 0) return;
    g_rowsEpoch = coop::RelationsPlayerPairEpoch(); g_rowsMs = ::GetTickCount();
    std::vector<playerstab::RowView> rows = BuildRows();
    if (!playerstab::SameSlots(rows, g_rows))
    {
        list->removeAllItems();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            list->addItem(MyGUI::UString(rows[i].cell[0].c_str()));
            for (int c = 1; c < playerstab::kColumns; ++c) list->setSubItemNameAt((size_t)c, i, MyGUI::UString(rows[i].cell[c].c_str()));
        }
        const int sel = g_selSlot < 0 ? -1 : playerstab::RowOfSlot(rows, g_selSlot);
        if (sel >= 0) list->setIndexSelected((size_t)sel); else list->clearIndexSelected();
        ++g_rebuilds;
        DebugLog("[PLAYERS] table: " + S((long long)rows.size()) + " other player(s)" + (rows.empty() ? std::string() : std::string(":")));
        for (size_t i = 0; i < rows.size() && i < 8; ++i)
            DebugLog("[PLAYERS]   slot " + S(rows[i].slot) + " | " + rows[i].cell[0] + " | " + rows[i].cell[1] + " | " + rows[i].cell[2]
                     + " | " + rows[i].cell[3] + " | " + rows[i].cell[4]);
    }
    else
    {
        for (size_t i = 0; i < rows.size(); ++i)
            for (int c = 0; c < playerstab::kColumns; ++c)
                if (rows[i].cell[c] != g_rows[i].cell[c])
                {
                    if (c == 0) list->setItemNameAt(i, MyGUI::UString(rows[i].cell[0].c_str()));
                    else list->setSubItemNameAt((size_t)c, i, MyGUI::UString(rows[i].cell[c].c_str()));
                    DebugLog("[PLAYERS] slot " + S(rows[i].slot) + " " + playerstab::ColumnHead(c) + " now '" + rows[i].cell[c] + "' (was '" + g_rows[i].cell[c] + "')");
                }
    }
    g_rows = rows;
    ApplyBottom(g_rows);
}
/* Each frame: other players' factions made known; with our tab in (its widgets kept; the destroy notice drops them with the
   window), the table rebuilt while it shows when its inputs moved, and the tab taken out - the window closed - once the world
   has no other players or a widget of its page was destroyed; without it, the window's tab control looked for at most once a
   second in a world with other players (until kGiveUpAfter refused adds in this world), and our tab added to it. */
bool OpenBox(int kind, int slot, const std::string& team, const std::string& who, unsigned serial);
void DropBox();
/* the boxes that come by themselves: the invitation's box while an invitation waits unanswered (taken down when it is answered,
   ends or a newer one replaces it), and a FACTION box owed for a removal while away - one box at a time */
void BoxesTick()
{
    int from = -1; std::string team; unsigned serial = 0;
    const bool waiting = coop::TeamInvitationWaiting(&from, &team, &serial);
    if (g_box != 0 && g_boxKind == teamscreen::kBoxInvite && teamscreen::InviteBoxEnds(waiting, serial, g_boxSerial))
    {
        DebugLog(std::string("[PLAYERS] FACTION INVITATION box taken down: ") + (waiting ? "a newer invitation replaced it" : "the invitation was answered or ended"));
        DropBox();
    }
    if (g_box != 0) return;
    const unsigned long now = ::GetTickCount();
    if (g_boxTryMs != 0 && now - g_boxTryMs < kLookEveryMs) return;   /* a box that could not be opened: tried again a second later */
    g_boxTryMs = 0;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    const bool otherUp = im != 0 && im->isModalAny();   /* the mod's other boxes, the game's own quit box: a box that comes by itself waits */
    if (!g_awayOwed.empty())
    {
        const std::string t = g_awayOwed.front();
        if (teamscreen::BoxText(teamscreen::kBoxAway, t, std::string()).empty()) { g_awayOwed.erase(g_awayOwed.begin()); DebugLog("[PLAYERS] FACTION box owed with no faction name - dropped"); return; }
        if (otherUp) return;
        if (OpenBox(teamscreen::kBoxAway, -1, t, std::string(), 0)) { ++g_awayBoxes; g_awayOwed.erase(g_awayOwed.begin()); }
        else { g_boxTryMs = now | 1u; if (++g_boxRetrySaid <= 1) DebugLog("[PLAYERS] FACTION box for '" + t + "' could not be shown - tried again every second"); }
        return;
    }
    if (!waiting) return;
    const std::string who = coop::TeamPlayerName(from);
    if (!teamscreen::InviteBoxDue(waiting, g_box != 0, otherUp, !who.empty()))
    {
        if (who.empty() && g_inviteNameSaid != serial) { g_inviteNameSaid = serial; DebugLog("[PLAYERS] FACTION INVITATION box waits: this game does not know the name of slot " + S(from) + " yet"); }
        else if (!who.empty() && otherUp && g_inviteOtherSaid != serial) { g_inviteOtherSaid = serial; DebugLog("[PLAYERS] FACTION INVITATION box waits: another box that blocks the game is up - it opens after"); }
        return;
    }
    if (OpenBox(teamscreen::kBoxInvite, from, team, who, serial)) ++g_inviteBoxes;
    else { g_boxTryMs = now | 1u; if (++g_boxRetrySaid <= 1) DebugLog("[PLAYERS] FACTION INVITATION box could not be shown - tried again every second"); }
}
void BoxCancelBody();
void BoxConfirmBody();
/* the press a box's button recorded: acted on here, on the box it was pressed on (a box gone since then takes no action) */
void BoxPressTick()
{
    const LONG p = ::InterlockedExchange(&g_boxPress, 0);
    if (p == 0) return;
    MyGUI::Widget* on = g_boxPressedOn;
    g_boxPressedOn = 0;
    if (g_box == 0 || on != g_box) { DebugLog("[PLAYERS] a box's " + std::string(p == 1 ? "left" : "right") + " button press dropped: that box is no longer up"); return; }
    if (p == 1) BoxCancelBody(); else BoxConfirmBody();
}
/* 478: a member's FACTION NAME box (the FACTION tab) cannot be edited - disabled while this player is a member, enabled again
   when it no longer is (only when this module disabled it); while the link is down it stays as it is. The box is looked for under
   the management window at most once a second while a member and kept until MyGUI destroys it. */
void NameBoxTick()
{
    if (!coop::TeamTableKnown()) return;   /* the link down (or no table from it yet): the box stays as it is */
    std::string team; int founder = -1;
    const bool lock = coop::TeamMyRole(&team, &founder) == teamscreen::kRoleMember;
    if (g_nameBox == 0)
    {
        if (!lock || g_tabs == 0) return;
        const unsigned long now = ::GetTickCount();
        if (g_nameLookMs != 0 && now - g_nameLookMs < kLookEveryMs) return;
        g_nameLookMs = now;
        MyGUI::Widget* top = g_tabs;
        while (top->getParent() != 0) top = top->getParent();
        MyGUI::Widget* hit = coop::UiFindLayoutSuffix(top, kNameSuffix);
        g_nameBox = hit != 0 ? hit->castType<MyGUI::EditBox>(false) : 0;
        if (g_nameBox == 0) return;
    }
    if (lock && g_nameBox->getEnabled())
    {
        g_nameBox->setEnabled(false); g_nameLocked = true; ++g_nameLocks;
        DebugLog("[PLAYERS] FACTION NAME box locked: this player is a member of '" + team + "' (locks " + S(g_nameLocks) + ")");
    }
    else if (!lock && g_nameLocked)
    {
        g_nameBox->setEnabled(true); g_nameLocked = false;
        DebugLog("[PLAYERS] FACTION NAME box unlocked: this player is no longer a member");
    }
}
void TickBody()
{
    if (!coop::GameplayRunning() || coop::EngineWritesBlocked()) return;
    MakeOthersKnown();
    if (g_uiDead) { if (g_box != 0) DropBox(); return; }   /* off after a fault: a box of ours left up would block the game with dead buttons */
    BoxPressTick();
    BoxesTick();
    NameBoxTick();
    if (g_item != 0)
    {
        if (!PageWhole()) { Remove(g_tabs, "a widget of its page was destroyed"); return; }
        const bool shown = Shown(g_tabs);
        if (!shown && !Wanted()) { Remove(g_tabs, "this world has no other players"); return; }
        coop::MgmtTabsFitTick(g_tabs);   /* the strip's captions measured once the window is open */
        if (shown && g_tabs->getIndexSelected() == (size_t)playerstab::kTabIndex && RowsStale()) Refresh();
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
    __try { TickCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; ErrorLog("[PLAYERS] a memory fault in the PLAYERS tab's frame work - the tab is no longer refreshed or added"); }
}

/* ---- the PLAYERS tab chosen (the control's tab-change event, after the game's handler titled the window PLAYERS): the table
   at once ---- */
void TabChangedBody(MyGUI::TabControl* sender, size_t index)
{
    if (sender == 0 || !OursIn(sender)) return;
    ++g_tabEvents;
    if (index != (size_t)playerstab::kTabIndex) return;
    ++g_tabOpened;
    if (!coop::EngineWritesBlocked()) Refresh();
    DebugLog("[PLAYERS] tab opened (" + S((long long)g_rows.size()) + " other player(s); playersTab " + S(g_tabOpened) + ")");
}
void TabChangedCatching(MyGUI::TabControl* sender, size_t index) { try { TabChangedBody(sender, index); } catch (...) { ++g_throws; } }
void OnTabChanged(MyGUI::TabControl* sender, size_t index) { __try { TabChangedCatching(sender, index); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* ---- the controls' handlers (MyGUI calls them from the game's input pass, main thread) ---- */
void SetStance(int button, int slot, const std::string& faction)
{
    const std::string r = coop::RelateSet(coopslot::kSlotWirePrefix + coopslot::SlotNum(slot), playerstab::LevelVerb(button));
    if (r.compare(0, 3, "ok ") != 0)
    {
        ++g_stanceRefused;
        DebugLog("[PLAYERS] stance " + std::string(playerstab::LevelVerb(button)) + " towards slot " + S(slot) + " '" + faction + "' NOT set: " + r);
        return;
    }
    ++g_stanceSet;
    const std::string line = playerstab::MyLine(button, faction);
    const int shown = coop::StoreShowPlayerLine(line);
    DebugLog("[PLAYERS] stance " + std::string(playerstab::LevelVerb(button)) + " towards slot " + S(slot) + " '" + faction + "': " + r
             + "; message line '" + line + "' " + (shown == 1 ? "shown" : "NOT shown (" + S(shown) + ")") + " (stanceSet " + S(g_stanceSet) + ")");
    Refresh();
}
void DropBox()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* w = g_box;
    g_boxSlot = -1; g_box = 0; g_boxKind = 0; g_boxSerial = 0; g_boxText.clear();
    ::InterlockedExchange(&g_boxPress, 0); g_boxPressedOn = 0;
    if (gui == 0 || w == 0) return;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0)
    {
        im->removeWidgetModal(w);   /* nothing when the box was not modal (the invitation's) */
        MyGUI::Widget* f = im->getKeyFocusWidget();
        if (f != 0 && Inside(f, w)) im->resetKeyFocusWidget();   /* the keyboard focus another window holds stays */
    }
    gui->destroyWidget(w);
}
/* THE BOXES (mock-ups A4.3 and B7.3-B7.7), as bugreport.cpp BuildBox builds the mod's message boxes: Kenshi_WindowC on the Info
   layer, titled in capitals, the words left-aligned from the top and the box fitted to the text MyGUI laid out (as the FALLEN
   tab's box), the back / cancel button left and the action right (Kenshi_Button2, lit by hand; the FACTION box has OK alone,
   right). Centred on the management window and inside it with a margin while that window shows, else on the screen. Modal,
   except the FACTION INVITATION box: the player keeps playing while it waits for an answer. One box at a time: a box the player
   asks for takes an invitation box down (it comes back while the invitation waits); any other box asked for while one is up is
   not opened. `who` = the player the words name (SET HOSTILE: the faction); `serial` = the invitation a FACTION INVITATION box
   answers. */
bool OpenBox(int kind, int slot, const std::string& team, const std::string& who, unsigned serial)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return false;
    const std::string title = teamscreen::BoxTitle(kind);
    if (g_box != 0 && g_boxKind == teamscreen::kBoxInvite && kind != teamscreen::kBoxInvite && kind != teamscreen::kBoxAway)
    {
        DebugLog("[PLAYERS] FACTION INVITATION box taken down for the " + title + " box the player asked for - it comes back while the invitation waits");
        DropBox();
    }
    if (g_box != 0) { DebugLog("[PLAYERS] " + title + " box NOT opened: the " + teamscreen::BoxTitle(g_boxKind) + " box is up"); return false; }
    const std::string text = teamscreen::BoxText(kind, team, who);
    if (text.empty()) { DebugLog("[PLAYERS] " + title + " box NOT opened: its words need a name this game does not know"); return false; }
    int vw = 1280, vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { vw = s.width; vh = s.height; } }
    /* the area: the management window (the tab control's top widget) while it shows, else the screen */
    MyGUI::IntCoord area(0, 0, vw, vh);
    MyGUI::Widget* top = g_tabs;
    while (top != 0 && top->getParent() != 0) top = top->getParent();
    if (top != 0 && Shown(top)) { const MyGUI::IntCoord m = top->getAbsoluteCoord(); if (m.width > 240 && m.height > 200) area = m; }
    int w = vw * 30 / 100;
    if (w < 360) w = 360;
    if (w > area.width - 16) w = area.width - 16;
    const int h = Clamp(coopui::NoticeBoxH(text, w), 150, area.height - 16);
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
    win->setCaption(MyGUI::UString(title.c_str()));
    const int W = cl->getWidth(), H = cl->getHeight();
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);
    const int btnW = Clamp(W / 4, 120, W / 2 - 12);
    const std::string leftWord = teamscreen::BoxLeft(kind), rightWord = teamscreen::BoxRight(kind);
    MyGUI::Widget* ew = Mk(cl, "EditBox", "Kenshi_WordWrapEmpty", 8, lay.textY, W - 16, lay.textH, MyGUI::Align::Default, kBoxText);
    MyGUI::Widget* b0w = leftWord.empty() ? 0 : Mk(cl, "Button", "Kenshi_Button2", 8, lay.btnY, btnW, lay.btnH, MyGUI::Align::Default, kBoxCancel);
    MyGUI::Widget* b1w = Mk(cl, "Button", "Kenshi_Button2", W - btnW - 8, lay.btnY, btnW, lay.btnH, MyGUI::Align::Default, kBoxOk);
    MyGUI::EditBox* e = ew != 0 ? ew->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* b0 = b0w != 0 ? b0w->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* b1 = b1w != 0 ? b1w->castType<MyGUI::Button>(false) : 0;
    if (e == 0 || (b0 == 0 && !leftWord.empty()) || b1 == 0) { gui->destroyWidget(raw); return false; }
    e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true);
    e->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    e->setTextColour(Lit());
    e->setCaption(MyGUI::UString(text.c_str()));
    if (b0 != 0) { b0->setCaption(MyGUI::UString(leftWord.c_str())); b0->setTextColour(Lit()); }
    b1->setCaption(MyGUI::UString(rightWord.c_str())); b1->setTextColour(Lit());
    /* the box fitted to the text MyGUI laid out (EditBox::getTextSize at the box's width, the line height from the text's
       sub-widget, as ui.cpp's boxes): the text from the top, one blank line, the buttons; the estimate stays when it reads 0 */
    MyGUI::Widget* ec = e->getClientWidget();
    MyGUI::ISubWidgetText* stw = (ec != 0 && ec != e) ? ec->getSubWidgetText() : 0;
    if (stw == 0) stw = e->getSubWidgetText();
    const int textH = e->getTextSize().height, lineH = stw != 0 ? stw->getFontHeight() : 0;
    const int insetH = (ec != 0 && ec != e && e->getHeight() > ec->getHeight()) ? e->getHeight() - ec->getHeight() : 0;
    if (textH > 0 && lineH > 0)
    {
        const MyGUI::IntCoord bc = raw->getCoord();
        int nh = (bc.height - H) + coopui::NoticeClientHFor(textH + insetH, lineH);
        if (nh > area.height - 16) nh = area.height - 16;
        if (nh != bc.height && nh > bc.height - H) raw->setCoord(MyGUI::IntCoord(bc.left, area.top + (area.height - nh) / 2, bc.width, nh));
        const coopui::NoticeLayout fit = coopui::NoticeLayoutFor(cl->getHeight(), lineH);
        e->setCoord(MyGUI::IntCoord(8, fit.textY, W - 16, fit.textH > 1 ? fit.textH : 1));
        if (b0 != 0) b0->setCoord(MyGUI::IntCoord(8, fit.btnY, btnW, fit.btnH));
        b1->setCoord(MyGUI::IntCoord(W - btnW - 8, fit.btnY, btnW, fit.btnH));
    }
    if (b0 != 0) b0->eventMouseButtonClick += MyGUI::newDelegate(OnBoxCancel);
    b1->eventMouseButtonClick += MyGUI::newDelegate(OnBoxConfirm);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    const bool modal = kind != teamscreen::kBoxInvite;   /* the invitation does not stop the game: no modal layer, no keyboard focus taken */
    if (im != 0 && modal) { im->addWidgetModal(raw); im->setKeyFocusWidget(raw); }
    g_box = raw;
    g_boxSlot = slot; g_boxKind = kind; g_boxSerial = serial; g_boxTeam = team; g_boxWho = who; g_boxText = text;
    ++g_boxOpened; ++g_boxesShown;
    const MyGUI::IntCoord c = raw->getAbsoluteCoord();
    const MyGUI::IntCoord tc = e->getCoord(), bb = b1->getCoord();
    DebugLog("[UI] rect factionbox '" + title + "' x=" + S(c.left) + " y=" + S(c.top) + " w=" + S(c.width) + " h=" + S(c.height) + " text y=" + S(tc.top) + " h=" + S(tc.height)
             + " (measured " + S(textH) + ", line " + S(lineH) + (textH > 0 && lineH > 0 ? std::string() : std::string(" - NOT measured, the estimate kept")) + ", left-aligned)"
             + " buttons y=" + S(bb.top) + " w=" + S(bb.width) + " in x=" + S(area.left) + " y=" + S(area.top) + " w=" + S(area.width) + " h=" + S(area.height)
             + (area.width == vw && area.left == 0 ? " (the screen)" : " (the management window)") + " screen " + S(vw) + "x" + S(vh) + (modal ? " modal" : " not modal"));
    DebugLog("[PLAYERS] " + title + " box up" + (slot >= 0 ? " for slot " + S(slot) : std::string()) + ": '" + text + "' " + (leftWord.empty() ? std::string() : "[" + leftWord + "] ") + "[" + rightWord + "]"
             + (leftWord.empty() ? " (one button, on the right)" : "") + " (boxes " + S(g_boxesShown) + ")");
    return true;
}
int WhichButton(MyGUI::Widget* w)
{
    if (w == 0) return -1;
    const std::string n = w->getName();
    for (int k = 0; k < 3; ++k) if (n == kBtnName[k]) return k;
    return -1;
}
void StanceBody(MyGUI::Widget* w)
{
    const int k = WhichButton(w);
    if (k < 0 || g_selSlot < 0 || coop::EngineWritesBlocked()) return;
    const std::vector<playerstab::RowView> rows = BuildRows();   /* read live at the press */
    const int i = playerstab::RowOfSlot(rows, g_selSlot);
    if (i < 0) { DebugLog("[PLAYERS] " + std::string(playerstab::ButtonCaption(k)) + " pressed, but the selected player (slot " + S(g_selSlot) + ") is no longer listed"); return; }
    const playerstab::RowView& r = rows[(size_t)i];
    const int press = playerstab::PressForRow(k, r.you, r.teammate, r.founderSets);
    DebugLog("[PLAYERS] " + std::string(playerstab::ButtonCaption(k)) + " pressed for slot " + S(r.slot) + " '" + r.faction + "' (now "
             + playerstab::LevelWord(r.you) + ") -> " + (press == playerstab::kPressSet ? "set" : press == playerstab::kPressConfirm ? "ask first" : playerstab::NothingWhy(k, r.teammate, r.founderSets)));
    if (press == playerstab::kPressSet) SetStance(k, r.slot, r.faction);
    else if (press == playerstab::kPressConfirm) OpenBox(teamscreen::kBoxHostile, r.slot, std::string(), r.faction, 0);
    else
    {
        /* nothing changes: the ticks are put back as the row reads (a teammate's row: ALLY), never the pressed button's */
        const playerstab::Bottom b = playerstab::BottomFor(rows, r.slot);
        for (int j = 0; j < 3; ++j) if (g_btn[j] != 0) g_btn[j]->setStateSelected(j == b.ticked);
        g_shownTicked = b.ticked;
    }
}
/* a request the box or the bottom line sends, through team.cpp's own request (the TEST lever's `team ...` sends the same) */
void Request(const std::string& sub, int slot, const std::string& what)
{
    const std::string r = coop::TeamRequestSend(sub, slot);
    const bool ok = r.compare(0, 3, "ok ") == 0;
    if (ok) ++g_requestsSent; else ++g_requestsRefused;
    if (ok && sub == "invite") ++g_invitesSent;
    DebugLog("[PLAYERS] " + what + ": " + r + " (requests sent " + S(g_requestsSent) + ", not sent " + S(g_requestsRefused) + ")");
}
/* the left button: CANCEL closes the box (nothing changes); the invitation's DECLINE answers it */
void BoxCancelBody()
{
    const int kind = g_boxKind, slot = g_boxSlot;
    ++g_boxCancelled;
    DropBox();
    if (kind == teamscreen::kBoxInvite) { Request("decline", -1, "FACTION INVITATION box: DECLINE (from slot " + S(slot) + ")"); return; }
    DebugLog("[PLAYERS] " + std::string(teamscreen::BoxTitle(kind)) + " box: " + teamscreen::BoxLeft(kind) + (slot >= 0 ? " (slot " + S(slot) + ")" : std::string()) + " - nothing changed");
}
void HostileConfirm(int slot);
/* the right button: the box's action, the state read live at the press */
void BoxConfirmBody()
{
    const int kind = g_boxKind, slot = g_boxSlot;
    DropBox();
    if (kind == teamscreen::kBoxHostile) { HostileConfirm(slot); return; }
    if (kind == teamscreen::kBoxAway) { DebugLog("[PLAYERS] FACTION box: OK - closed"); return; }
    if (coop::EngineWritesBlocked()) { DebugLog("[PLAYERS] " + std::string(teamscreen::BoxTitle(kind)) + " box: " + teamscreen::BoxRight(kind) + " pressed with no world - nothing sent"); return; }
    std::string team; int founder = -1;
    const int role = coop::TeamMyRole(&team, &founder);
    if (kind == teamscreen::kBoxInvite) { Request("accept", -1, "FACTION INVITATION box: ACCEPT (from slot " + S(slot) + ")"); return; }
    if (kind == teamscreen::kBoxLeave)
    {
        if (role != teamscreen::kRoleMember) { DebugLog("[PLAYERS] LEAVE FACTION box: LEAVE pressed, but this player is no longer a member - nothing sent"); return; }
        Request("leave", -1, "LEAVE FACTION box: LEAVE");
        return;
    }
    if (kind == teamscreen::kBoxRemove)
    {
        if (role != teamscreen::kRoleFounder || !coop::TeamSameAnyThread(coop::StoreMySlot(), slot)) { DebugLog("[PLAYERS] REMOVE FROM FACTION box: REMOVE pressed, but slot " + S(slot) + " is no longer a member of this founder's faction - nothing sent"); return; }
        Request("remove", slot, "REMOVE FROM FACTION box: REMOVE (slot " + S(slot) + ")");
        return;
    }
    if (kind == teamscreen::kBoxDisband)
    {
        if (role != teamscreen::kRoleFounder) { DebugLog("[PLAYERS] DISBAND FACTION box: DISBAND pressed, but this player no longer founds a faction - nothing sent"); return; }
        Request("disband", -1, "DISBAND FACTION box: DISBAND");
    }
}
/* the bottom line's action buttons: INVITE TO FACTION sends the invitation; REMOVE / DISBAND / LEAVE ask first (their box). The
   view is read live at the press; a button whose action the selection no longer offers does nothing. */
void ActBody(MyGUI::Widget* w)
{
    int k = -1;
    for (int j = 0; j < 2; ++j) if (w != 0 && g_act[j] != 0 && w == g_act[j]) k = j;
    if (k < 0 || coop::EngineWritesBlocked()) return;
    ++g_actPressed;
    const std::vector<playerstab::RowView> rows = BuildRows();   /* read live at the press */
    const teamscreen::View v = ViewNow(rows, g_selSlot);
    const int a = k == 0 ? v.left : v.right;
    const int shown = g_actShown[k];
    const int i = g_selSlot < 0 ? -1 : playerstab::RowOfSlot(rows, g_selSlot);
    if (a == teamscreen::kActNone || a != shown)
    {
        ++g_actNothing;
        DebugLog("[PLAYERS] " + std::string(teamscreen::ActionCaption(shown)) + " pressed, but the selection now offers '" + teamscreen::ActionCaption(a) + "' there - nothing done");
        return;
    }
    DebugLog("[PLAYERS] " + std::string(teamscreen::ActionCaption(a)) + " pressed" + (i >= 0 ? " with slot " + S(rows[(size_t)i].slot) + " selected" : std::string()));
    if (a == teamscreen::kActInvite) { if (i >= 0) Request("invite", rows[(size_t)i].slot, "INVITE TO FACTION (slot " + S(rows[(size_t)i].slot) + ")"); return; }
    if (a == teamscreen::kActRemove && i < 0) return;
    OpenBox(teamscreen::BoxOfAction(a), a == teamscreen::kActRemove ? rows[(size_t)i].slot : -1, g_ctx.team, a == teamscreen::kActRemove ? rows[(size_t)i].name : std::string(), 0);
}
/* SET HOSTILE's HOSTILE: the stance set, read live at the press */
void HostileConfirm(int slot)
{
    if (slot < 0 || coop::EngineWritesBlocked()) return;
    const std::vector<playerstab::RowView> rows = BuildRows();   /* read live at the press */
    const int i = playerstab::RowOfSlot(rows, slot);
    if (i < 0) { DebugLog("[PLAYERS] SET HOSTILE box: HOSTILE pressed, but slot " + S(slot) + " is no longer listed - nothing changed"); return; }
    if (rows[(size_t)i].you == nametag::kHostile) { DebugLog("[PLAYERS] SET HOSTILE box: already hostile towards slot " + S(slot) + " - nothing changed"); return; }
    if (rows[(size_t)i].teammate) { DebugLog("[PLAYERS] SET HOSTILE box: slot " + S(slot) + " shares this player's faction now - nothing changed"); return; }
    if (rows[(size_t)i].founderSets) { DebugLog("[PLAYERS] SET HOSTILE box: the founder sets this faction's stance towards slot " + S(slot) + " now - nothing changed"); return; }
    ++g_hostileConfirmed;
    DebugLog("[PLAYERS] SET HOSTILE box: HOSTILE confirmed for slot " + S(slot) + " '" + rows[(size_t)i].faction + "' (hostileConfirmed " + S(g_hostileConfirmed) + ")");
    SetStance(2, slot, rows[(size_t)i].faction);
}
void RowBody(MyGUI::MultiListBox* list, size_t index)
{
    (void)list;
    g_selSlot = (index != MyGUI::ITEM_NONE && index < g_rows.size()) ? g_rows[index].slot : -1;
    ApplyBottom(g_rows);
    if (g_selSlot >= 0) DebugLog("[PLAYERS] row selected: slot " + S(g_selSlot) + " '" + g_rows[index].faction + "' -> the line '" + g_view.line + "'");
}
/* each handler: its body under try (MyGUI throws C++ exceptions) under SEH (a memory fault turns the module's widget work off) */
void StanceCatching(MyGUI::Widget* w) { try { StanceBody(w); } catch (...) { ++g_throws; } }
void OnStanceClicked(MyGUI::Widget* w) { __try { StanceCatching(w); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
/* a box's buttons only RECORD the press (and the box it was on); the tick acts on it (BoxPressTick) */
void OnBoxCancel(MyGUI::Widget*) { g_boxPressedOn = g_box; ::InterlockedExchange(&g_boxPress, 1); }
void ActCatching(MyGUI::Widget* w) { try { ActBody(w); } catch (...) { ++g_throws; } }
void OnActClicked(MyGUI::Widget* w) { __try { ActCatching(w); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void MessageBody()
{
    if (g_selSlot < 0) return;
    DebugLog("[PLAYERS] MESSAGE pressed for slot " + S(g_selSlot));
    coop::ChatOpenTo(g_selSlot);
}
void MessageCatching() { try { MessageBody(); } catch (...) { ++g_throws; } }
void OnMessageClicked(MyGUI::Widget*) { __try { MessageCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }
void OnBoxConfirm(MyGUI::Widget*) { g_boxPressedOn = g_box; ::InterlockedExchange(&g_boxPress, 2); }
void RowCatching(MyGUI::MultiListBox* list, size_t index) { try { RowBody(list, index); } catch (...) { ++g_throws; } }
void OnRowPicked(MyGUI::MultiListBox* list, size_t index) { __try { RowCatching(list, index); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

void TitleBody() { if (g_box != 0) DropBox(); }
void TitleCatching() { try { TitleBody(); } catch (...) { ++g_throws; } }
void DropBoxSeh() { __try { TitleCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* ---- what the screens show now, word for word (the `playerstab shown` lever and the report) ---- */
std::string Vis(MyGUI::Widget* w) { return Shown(w) ? "shown" : "hidden"; }
void ShownReport()
{
    std::string rows;
    for (size_t i = 0; i < g_rows.size(); ++i)
    {
        rows += (i ? "; s" : "s") + S(g_rows[i].slot) + " ";
        for (int c = 0; c < playerstab::kColumns; ++c) rows += (c ? " | " : "") + g_rows[i].cell[c];
    }
    std::string stance;
    for (int k = 0; k < 3; ++k)
        stance += std::string(k ? "," : "") + playerstab::ButtonCaption(k) + (g_btn[k] != 0 && g_btn[k]->getStateSelected() ? "*" : "")
                  + (g_btn[k] != 0 && !g_btn[k]->getEnabled() ? "(grey)" : "");
    const bool tabShown = g_item != 0 && Shown(g_tabs) && g_tabs->getIndexSelected() == (size_t)playerstab::kTabIndex;
    const bool lineShown = g_line != 0 && Shown(g_line), selShown = g_select != 0 && Shown(g_select);
    DebugLog("[PLAYERS] SHOWN tab=" + std::string(tabShown ? "open" : "not open") + " role=" + S(g_ctx.role) + " team='" + g_ctx.team + "'"
             + " | line='" + (lineShown ? g_view.line : selShown ? std::string(playerstab::kSelectLine) : std::string()) + "'"
             + " | stance buttons " + (g_btn[0] != 0 ? Vis(g_btn[0]) : std::string("none")) + " [" + stance + "]"
             + " | left='" + (g_act[0] != 0 && Shown(g_act[0]) ? std::string(teamscreen::ActionCaption(g_actShown[0])) : std::string()) + "'"
             + " right='" + (g_act[1] != 0 && Shown(g_act[1]) ? std::string(teamscreen::ActionCaption(g_actShown[1])) : std::string()) + "'"
             + " | rows: " + (rows.empty() ? std::string("none") : rows)
             + " | box: " + (g_box != 0 ? "'" + std::string(teamscreen::BoxTitle(g_boxKind)) + "' text='" + g_boxText + "' left='" + teamscreen::BoxLeft(g_boxKind) + "' right='" + teamscreen::BoxRight(g_boxKind) + "'" : std::string("none"))
             + " | FACTION NAME box " + (g_nameBox == 0 ? std::string("not found") : g_nameBox->getEnabled() ? std::string("editable") : std::string("locked")));
}

/* the SHOWN line read under the module's guards (the `report` / `netstat` verbs and the lever read live widgets) */
void ShownCatching() { try { ShownReport(); } catch (...) { ++g_throws; } }
void ShownSeh() { __try { ShownCatching(); } __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; g_uiDead = true; } }

/* ---- the TEST-ONLY lever: each step fires the control's own handler, as a mouse would, and only where a mouse could ---- */
std::string LeverBody(const std::string& args)
{
    /* the command channel hands over the rest of its line with the space after the verb: the word is compared without it */
    const size_t first = args.find_first_not_of(" \t\r\n");
    const std::string word = first == std::string::npos ? std::string() : args.substr(first, args.find_last_not_of(" \t\r\n") - first + 1);
    if (word == "message")
    {
        if (!Reachable(g_msg)) { DebugLog("[PLAYERS] lever message REFUSED: no MESSAGE button on screen (no player selected?)"); return "error playerstab no-message"; }
        g_msg->eventMouseButtonClick(g_msg);
        return "ok playerstab message";
    }
    const playerstab::Lever l = playerstab::ParseLever(args);
    if (l.kind == playerstab::kLeverBad) return "error playerstab usage";
    if (l.kind == playerstab::kLeverReport) { coop::ReportPlayersTab(); return "ok playerstab"; }
    if (l.kind == playerstab::kLeverShown) { ShownSeh(); return "ok playerstab shown"; }
    if (l.kind == playerstab::kLeverConfirm || l.kind == playerstab::kLeverCancel)
    {
        MyGUI::Widget* b = Find(l.kind == playerstab::kLeverConfirm ? kBoxOk : kBoxCancel);
        if (!Reachable(b)) { DebugLog("[PLAYERS] lever " + args + " REFUSED: no box with that button on screen"); return "error playerstab no-box"; }
        b->eventMouseButtonClick(b);
        return l.kind == playerstab::kLeverConfirm ? "ok playerstab confirm" : "ok playerstab cancel";
    }
    if (g_item == 0 || !PageWhole() || !Shown(g_tabs))
    { DebugLog("[PLAYERS] lever " + args + " REFUSED: the management window is not open with the PLAYERS tab in it"); return "error playerstab window-closed"; }
    if (l.kind == playerstab::kLeverOpen || l.kind == playerstab::kLeverFaction)
    {
        /* the control's own selection and event, as its tab button fires them: PLAYERS, or the game's FACTION tab (position 1) */
        const size_t tab = l.kind == playerstab::kLeverOpen ? (size_t)playerstab::kTabIndex : (size_t)playerstab::kFactionTab;
        g_tabs->setIndexSelected(tab);
        g_tabs->eventTabChangeSelect(g_tabs, tab);
        DebugLog("[PLAYERS] lever: the " + std::string(l.kind == playerstab::kLeverOpen ? "PLAYERS" : "FACTION") + " tab chosen (control position " + S((long long)tab) + ")");
        return l.kind == playerstab::kLeverOpen ? "ok playerstab open" : "ok playerstab faction";
    }
    if (l.kind == playerstab::kLeverSelect)
    {
        MyGUI::MultiListBox* list = g_list;
        const int row = playerstab::RowOfSlot(g_rows, l.slot);
        if (!Reachable(list) || row < 0) { DebugLog("[PLAYERS] lever " + args + " REFUSED: the table is not on screen or has no row for that slot (" + S((long long)g_rows.size()) + " rows)"); return "error playerstab no-row"; }
        list->setIndexSelected((size_t)row);
        list->eventListChangePosition(list, (size_t)row);
        return "ok playerstab select";
    }
    if (l.kind == playerstab::kLeverAction)
    {
        for (int k = 0; k < 2; ++k)
            if (g_actShown[k] == l.button && Reachable(g_act[k])) { g_act[k]->eventMouseButtonClick(g_act[k]); return std::string("ok playerstab action ") + teamscreen::ActionWord(l.button); }
        DebugLog("[PLAYERS] lever " + args + " REFUSED: no " + teamscreen::ActionCaption(l.button) + " button on screen");
        return "error playerstab no-action";
    }
    MyGUI::Widget* b = (l.button >= 0 && l.button < 3) ? g_btn[l.button] : 0;
    if (!Reachable(b)) { DebugLog("[PLAYERS] lever " + args + " REFUSED: the " + playerstab::ButtonCaption(l.button) + " button is not on screen (no player selected?)"); return "error playerstab no-button"; }
    b->eventMouseButtonClick(b);
    return "ok playerstab stance";
}
std::string LeverCatching(const std::string& args) { try { return LeverBody(args); } catch (...) { ++g_throws; return "error playerstab threw"; } }

}   /* namespace */

namespace coop {

void PlayersTabTick() { TickSeh(); }

void PlayersTabTitleTick()
{
    if (g_box == 0) return;
    DropBoxSeh();
}

void PlayersTabForgetWorld()
{
    g_carriedWalked = false; g_carried.clear(); g_known.clear(); g_refusedThisWorld = 0;
    g_rows.clear(); g_selSlot = -1; g_shownLine.clear(); g_shownSelect.clear(); g_shownTicked = -2; g_shownSelected = -1; g_shownLocked = -1; g_rowsEpoch = -1;
    g_shownView.clear(); g_nameLookMs = 0;
    if (g_box != 0) DropBoxSeh();   /* a box asks about a player or a faction of the world being left */
    g_boxSlot = -1;
    if (!g_awayOwed.empty()) DebugLog("[PLAYERS] " + S((long long)g_awayOwed.size()) + " owed FACTION box(es) cleared: the world was left");
    g_awayOwed.clear(); g_boxTryMs = 0;
}

std::string PlayersTabCommand(const std::string& args) { return LeverCatching(args); }

bool PlayersTabPlayerWords(int slot, std::string* name, std::string* faction)
{
    name->clear(); faction->clear();
    if (slot < 0) return false;
    try
    {
        std::string nm;
        if (StoreRosterNameOf(slot, &nm) == 1 && !nm.empty()) { *name = nm; g_names[slot] = nm; }
        else
        {
            std::map<int, std::string>::const_iterator it = g_names.find(slot);
            *name = it != g_names.end() ? it->second : TeamPlayerName(slot);
        }
        if (name->empty()) *name = coopslot::PlaceholderName(slot);
        if (!g_uiDead)
        {
            const std::vector<Other> others = OtherPlayers();
            for (size_t i = 0; i < others.size(); ++i)
                if (others[i].slot == slot && Plaus(others[i].f))
                {
                    std::string line2;
                    bool mate = false;
                    TeamTagFor(slot, FactionWords(others[i].f), &line2, &mate);   /* in a faction of players: its name */
                    *faction = line2;
                    break;
                }
        }
    }
    catch (...) { ++g_throws; return false; }
    return true;
}

void PlayersTabRemovedWhileAway(const std::string& teamName)
{
    g_awayOwed.push_back(teamName);
    DebugLog("[PLAYERS] FACTION box owed: removed from '" + teamName + "' while away (shown when no other box is up)");
}

void ReportPlayersTab()
{
    DebugLog("[PLAYERS] REPORT uiDead=" + S(g_uiDead ? 1 : 0) + " tabIn=" + S(g_item != 0 ? 1 : 0)
             + " added=" + S(g_inserted) + " removed=" + S(g_removed) + " addRefused=" + S(g_insertRefused)
             + " playersTab=" + S(g_tabOpened) + " stanceSet=" + S(g_stanceSet) + " hostileConfirmed=" + S(g_hostileConfirmed)
             + " boxOpened=" + S(g_boxOpened) + " boxCancelled=" + S(g_boxCancelled) + " stanceRefused=" + S(g_stanceRefused)
             + " rows=" + S((long long)g_rows.size()) + " selected=" + S(g_selSlot) + " rebuilds=" + S(g_rebuilds)
             + " tabEvents=" + S(g_tabEvents) + " looks=" + S(g_looks) + " destroyNotice=" + S(g_unlinkerOn ? 1 : 0)
             + " factscr[known,made,faults,noInterface]=" + S((long long)g_known.size()) + "," + S(g_knownMade) + "," + S(g_knownFaults) + "," + S(g_knownNoInterface)
             + " carried=" + S((long long)g_carried.size()) + " faults=" + S(g_faults) + " throws=" + S(g_throws)
             + " actPressed=" + S(g_actPressed) + " actNothing=" + S(g_actNothing) + " invitesSent=" + S(g_invitesSent) + " requestsSent=" + S(g_requestsSent)
             + " requestsNotSent=" + S(g_requestsRefused) + " boxesShown=" + S(g_boxesShown) + " inviteBoxes=" + S(g_inviteBoxes) + " awayBoxes=" + S(g_awayBoxes)
             + " nameLocks=" + S(g_nameLocks));
    ShownSeh();
}
void* PlayersTabItemPtr() { return g_item; }

}
namespace {
const char* const kEngineWidthKey = "SWEngineTabWidth";   /* the game's own button width, kept on the tab control itself */
const char* const kMeasuredKey = "SWTabsMeasured";       /* "<tabs>x<width>" of the last fit with measured captions (else "") */
const unsigned long kFitEveryMs = 500;                    /* the retry while the window is open and the captions unmeasured */
/* why a fit's captions were not measured (one bit each; each is logged once until a measured fit) */
const int kWhyClosed = 1, kWhyNoCaption = 2, kWhyZero = 4, kWhyRoom = 8, kWhyThrow = 16;
int g_fitWhyLogged = 0;
unsigned long g_fitMs = 0;
bool g_fitWasShown = false;
std::string g_fitTriedKey;
std::string WhyWords(int why, const std::string& caption)
{
    if (why == kWhyClosed) return "the window is closed - its tab buttons are laid out when it opens";
    if (why == kWhyNoCaption) return "a tab has no caption";
    if (why == kWhyZero) return "the caption '" + caption + "' measured 0 px wide (the tab button's text has no font)";
    if (why == kWhyRoom) return "the tab button's room around its text read outside 0..40 px";
    return "the measure threw";
}
std::string FitKey(MyGUI::TabControl* tabs) { return S((long long)tabs->getItemCount()) + "x" + S(tabs->getWidth()); }
/* TabControl::_getTextWidth - MyGUI's own measure for its auto-width tabs, exported by the game's MyGUIEngine_x64.dll under
   its protected name: it puts the caption on the control's first tab button, reads that button's text size (the text
   sub-widget's getTextSize) and text region, puts the button's caption back, and returns the text's width plus the button's
   width less its text region's. The tab buttons live under the skin's HeaderPlace, a skin widget getChildAt does not list,
   so the game's own button is reached this way: a member pointer named through a class derived from TabControl (never made). */
struct TabMeasure : MyGUI::TabControl
{
    static int Width(MyGUI::TabControl* tabs, const MyGUI::UString& caption)
    {
        int (MyGUI::TabControl::*f)(const MyGUI::UString&) = &TabMeasure::_getTextWidth;
        return (tabs->*f)(caption);
    }
};
/* Every tab's caption width in the strip's font and the room each button keeps around its text plus a pixel either side,
   measured on the game's own first tab button while the window is open (its buttons laid out). 0 = measured, else why not
   (kWhy*); `bad` names the caption that measured 0. */
int MeasureTabCaptions(MyGUI::TabControl* tabs, std::vector<int>* text, int* pad, std::string* bad)
{
    text->clear(); *pad = 0;
    if (!Shown(tabs)) return kWhyClosed;
    try
    {
        const int room = TabMeasure::Width(tabs, MyGUI::UString(""));
        if (room < 0 || room > 40) return kWhyRoom;
        const size_t n = tabs->getItemCount();
        for (size_t i = 0; i < n; ++i)
        {
            const MyGUI::UString name = tabs->getItemNameAt(i);
            const char* t = name.asUTF8_c_str();
            *bad = t != 0 ? t : "";
            if (bad->empty()) return kWhyNoCaption;
            const int w = TabMeasure::Width(tabs, name) - room;
            if (w <= 0) return kWhyZero;
            text->push_back(w);
        }
        bad->clear();
        *pad = room + 2;
        return 0;
    }
    catch (...) { text->clear(); return kWhyThrow; }
}
}   /* namespace */
namespace coop {

/* The game's own button width is noted on the control while it holds its seven (a user string: a rebuilt control starts
   without one and is read again). With more tabs they share the strip the seven filled: with the captions measured (the
   window open), equal widths while every caption fits whole, else each button sized to its caption (swtab::StripWidths);
   unmeasured, equal widths filling the strip exactly (swtab::EqualWidths) and MgmtTabsFitTick fits again once the window is
   open. Back to seven, the game's own width again. */
void MgmtTabsFit(void* tabControl)
{
    MyGUI::TabControl* tabs = (MyGUI::TabControl*)tabControl;
    if (tabs == 0) return;
    const int n = (int)tabs->getItemCount(), k = playerstab::kEngineTabCount;
    int base = 0;
    if (tabs->isUserString(kEngineWidthKey)) base = std::atoi(tabs->getUserString(kEngineWidthKey).c_str());
    else if (n == k)
    {
        base = tabs->getButtonDefaultWidth();
        if (base > 0) tabs->setUserString(kEngineWidthKey, S(base));
        return;
    }
    if (base <= 0) return;
    if (n <= k)
    {
        if (tabs->getButtonDefaultWidth() != base) tabs->setButtonDefaultWidth(base);
        for (int i = 0; i < n; ++i) tabs->setButtonWidthAt((size_t)i, MyGUI::DEFAULT);
        tabs->setUserString(kMeasuredKey, std::string());
        g_fitShown.clear();
        return;
    }
    const int equal = swtab::StripButtonWidth(base, k, n);
    std::vector<int> text;
    int pad = 0;
    std::string bad;
    const int why = MeasureTabCaptions(tabs, &text, &pad, &bad);
    const bool measured = why == 0;
    const std::vector<int> w = measured ? swtab::StripWidths(base * k, pad, text) : swtab::EqualWidths(base * k, n);
    if (tabs->getButtonDefaultWidth() != equal) tabs->setButtonDefaultWidth(equal);
    std::string shown;
    for (int i = 0; i < n && i < (int)w.size(); ++i)
    {
        if (tabs->getButtonWidthAt((size_t)i) != w[(size_t)i]) tabs->setButtonWidthAt((size_t)i, w[(size_t)i]);
        const char* t = tabs->getItemNameAt((size_t)i).asUTF8_c_str();
        shown += std::string(i ? ", " : "") + (t != 0 ? t : "") + " " + S(w[(size_t)i]) + (measured ? " (text " + S(text[(size_t)i]) + ")" : std::string());
    }
    tabs->setUserString(kMeasuredKey, measured ? FitKey(tabs) : std::string());
    const bool newWhy = !measured && (g_fitWhyLogged & why) == 0;
    if (measured) g_fitWhyLogged = 0;
    else g_fitWhyLogged |= why;
    if (shown != g_fitShown || newWhy)
    {
        g_fitShown = shown;
        DebugLog("[UI] tab strip: " + S(n) + " tabs across " + S(base * k) + " px (the game's " + S(k) + " x " + S(base) + ")"
                 + (measured ? "; captions measured, pad " + S(pad) : "; captions NOT measured (" + WhyWords(why, bad) + ") - equal widths") + ": " + shown);
    }
}

void MgmtTabsFitTick(void* tabControl)
{
    MyGUI::TabControl* tabs = (MyGUI::TabControl*)tabControl;
    if (tabs == 0 || (int)tabs->getItemCount() <= playerstab::kEngineTabCount) return;
    if (!Shown(tabs)) { g_fitWasShown = false; return; }
    const std::string key = FitKey(tabs);
    const bool opened = !g_fitWasShown;
    g_fitWasShown = true;
    if (tabs->isUserString(kMeasuredKey) && tabs->getUserString(kMeasuredKey) == key) return;   /* measured at this count and width */
    const unsigned long now = ::GetTickCount();
    if (!opened && key == g_fitTriedKey && now - g_fitMs < kFitEveryMs) return;
    g_fitTriedKey = key; g_fitMs = now;
    MgmtTabsFit(tabs);
}

long long PlayersTabOpened() { return g_tabOpened; }
long long PlayersTabStanceSet() { return g_stanceSet; }
long long PlayersTabHostileConfirmed() { return g_hostileConfirmed; }

}
