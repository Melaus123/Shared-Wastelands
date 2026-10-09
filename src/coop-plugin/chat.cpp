// chat.cpp - IN-GAME TEXT CHAT, as the approved mock-up build/pages/text-chat-mockup.html shows it.
//
// WHAT THE PLAYER SEES. Enter (in a world, with no text box focused, no pause menu and no modal window up) opens CHAT: Kenshi's
// window header with CHAT at its left, then OPACITY and a slider that fades only the window's background, then the close X;
// the lines, each row on a dark band; then TO [ EVERYONE v ], the button as wide as its words; then [ type a message... ] and
// [ SEND ] at the bottom. Enter (or SEND) sends and closes; Esc closes without sending and the pause menu does
// not open (ui.cpp detour_pauseMenuShow asks ChatTakesEscape first). Tab steps EVERYONE -> MY FACTION -> the last player
// messaged -> EVERYONE. The TO button opens a list with a search line; clicking a player's name on a chat line sets TO to that
// player; the PLAYERS tab's MESSAGE button opens the chat with TO set to its selected player. Closed, the window's frame and
// controls go and only the lines (on their bands) stay where the window was, each fading after 10 seconds; the feed takes no clicks. The
// window's place, size and opacity are kept in chat.cfg in the data folder. A message that cannot go leaves a grey line and
// the typed text in the box.
//
// THE ROAD. A message is one kInnerChat (src/common/chatwire.h) through the world server's relay - everyone by route WORLD, a
// player by route SLOT, a faction as one SLOT copy to each other member in the world. The server stamps the sender's slot; the
// name shown is this game's own name for that slot. "X joined / left the world." come from this game's own copy of who is in
// the world, diffed every half second; the first look after entering the world, and the first after this game's own link to
// the world server comes back, only take note. A message goes only with the link up and the world's player list known (a
// faction of one needs neither). A received line is kept only when it is this game's: a faction line from a member of this
// player's faction, a private line naming this game's slot, and at most 5 lines a second from any one player.
//
// LOGS: counts, kinds and slots only - never a typed or received word.
//
// THE RULES OF ui.cpp HOLD HERE: no widget pointer is kept across frames (every widget is found by name, every name starts
// "SWChat" and holds no '_'); click and key handlers only write interlocked values that the tick reads; every MyGUI call runs
// behind an SEH frame with no C++ object in it (C2712); a memory fault, or the third C++ throw, takes the chat down for the rest
// of the process after removing whatever of it is on screen.

#include "coop_log.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_ScrollBar.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_KeyCode.h>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstdio>
#include "chat.h"
#include "ui.h"                            /* UiPauseMenuVisible, UiMenuCommand */
#include "store.h"                         /* StoreSendLive, StoreLiveReady, StoreMySlot, StoreRosterSlotInWorld, StoreWorldPlayerCount */
#include "team.h"                          /* TeamMatesOfMine, TeamPlayerName */
#include "playerstab.h"                    /* PlayersTabPlayerWords */
#include "config.h"                        /* ConfigDataDir, ConfigFilePlayerName */
#include "soak.h"                          /* GameplayRunning */
#include "../common/chatwire.h"
#include "../common/liverelay.h"           /* kInnerChat */
#include "../common/liveenvelope.h"        /* kRouteWorld, kRouteSlot */
#include "../common/arrivals.h"            /* mparrive::InWorldDiff */
#include "../common/bugreport.h"           /* coopbug::CaptionToPlain */
#include "../common/playerstab.h"          /* playerstab::EscapeHash */
#include "../common/slotwire.h"            /* coopslot::PlaceholderName */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "u8file.h"

namespace coop {

namespace {

/* the widgets: one name each, all "SWChat..." */
const char* const kBg       = "SWChatBg";         /* the background, a sibling of the root so its alpha fades nothing else */
const char* const kRoot     = "SWChatRoot";
const char* const kHead     = "SWChatHead";
const char* const kTitle    = "SWChatTitle";
const char* const kClose    = "SWChatClose";
const char* const kView     = "SWChatLines";
const char* const kCanvas   = "SWChatCanvas";
const char* const kToLbl    = "SWChatToLabel";
const char* const kToBtn    = "SWChatToBtn";
const char* const kEdit     = "SWChatEdit";
const char* const kHint     = "SWChatHint";
const char* const kSend     = "SWChatSend";
const char* const kOpLbl    = "SWChatOpacityLabel";
const char* const kOpBar    = "SWChatOpacity";
const char* const kGrip     = "SWChatGrip";
const char* const kMeasure  = "SWChatMeasure";
const char* const kPop      = "SWChatToList";
const char* const kPopEdit  = "SWChatToSearch";
const char* const kPopHint  = "SWChatToSearchHint";
const char* const kPopRows  = "SWChatToRows";

const int kHeadH = 38;    /* Kenshi_WindowCX's header: 38 tall, its close X 31 x 32 at 5 from the top */
const int kPad = 8;
const int kCtlH = 30;

/* ---- the clock: GetTickCount carried past its 49-day wrap ---- */
long long NowMs()
{
    static DWORD last = 0;
    static long long high = 0;
    const DWORD t = ::GetTickCount();
    if (t < last) high += 0x100000000LL;
    last = t;
    return high + (long long)t;
}
std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return std::string(b); }
int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- what the player pressed: written by the handlers, taken by the tick ---- */
enum { kActNone = 0, kActSend = 1, kActClose = 2, kActToList = 3, kActSearchPick = 4 };
volatile LONG g_act = kActNone;
volatile LONG g_esc = 0;          /* an ESC meant for the open window */
volatile LONG g_tabN = 0;         /* Tab presses while typing */
volatile LONG g_uiUp = 0;         /* the window is open: ESC is the chat's */
volatile LONG g_off = 0;          /* the chat took itself down after a fault */
volatile LONG g_escTook = 0;      /* pause-menu shows refused while the window was open */
volatile LONG g_pickHas = 0, g_pickTo = 0, g_pickWhy = 0;   /* a TO picked: 1 from the list, 2 a clicked name */
volatile LONG g_openReq = 0, g_openSlot = -1;              /* the PLAYERS tab's MESSAGE */
volatile LONG g_drag = 0, g_dragSerial = 0, g_pressX = 0, g_pressY = 0, g_dragX = 0, g_dragY = 0, g_released = 0;   /* 1 move, 2 size */
volatile LONG g_opNew = -1;       /* the slider's new position, -1 none */
volatile LONG g_searchDirty = 0;
volatile LONG g_escHold = 0;      /* the window closed on an ESC: that ESC's pause-menu show is still refused (EscHoldOn) */
volatile LONG g_escClosedTick = 0;
const DWORD kEscHoldMs = 500;

void OnSend(MyGUI::Widget*)          { ::InterlockedExchange(&g_act, kActSend); }
void OnAccept(MyGUI::EditBox*)       { ::InterlockedExchange(&g_act, kActSend); }
void OnClose(MyGUI::Widget*)         { ::InterlockedExchange(&g_act, kActClose); }
void OnToBtn(MyGUI::Widget*)         { ::InterlockedExchange(&g_act, kActToList); }
void OnSearchAccept(MyGUI::EditBox*) { ::InterlockedExchange(&g_act, kActSearchPick); }
void OnSearchChange(MyGUI::EditBox*) { ::InterlockedExchange(&g_searchDirty, 1); }
void OnKey(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
{
    if (key == MyGUI::KeyCode::Escape) ::InterlockedExchange(&g_esc, 1);
    else if (key == MyGUI::KeyCode::Tab) ::InterlockedIncrement(&g_tabN);
}
void PickFrom(MyGUI::Widget* w, const char* key, LONG why)
{
    if (w == 0) return;
    const std::string& s = w->getUserString(key);
    if (s.empty()) return;
    ::InterlockedExchange(&g_pickTo, (LONG)atoi(s.c_str()));
    ::InterlockedExchange(&g_pickWhy, why);
    ::InterlockedExchange(&g_pickHas, 1);
}
void OnPickRow(MyGUI::Widget* w)   { PickFrom(w, "to", 1); }
void OnNameClick(MyGUI::Widget* w) { PickFrom(w, "slot", 2); }
void DragStart(int left, int top, LONG kind)
{
    ::InterlockedExchange(&g_pressX, left); ::InterlockedExchange(&g_pressY, top);
    ::InterlockedExchange(&g_dragX, left);  ::InterlockedExchange(&g_dragY, top);
    ::InterlockedIncrement(&g_dragSerial);
    ::InterlockedExchange(&g_drag, kind);
}
void OnHeadPress(MyGUI::Widget*, int left, int top, MyGUI::MouseButton b) { if (b == MyGUI::MouseButton::Left) DragStart(left, top, 1); }
void OnGripPress(MyGUI::Widget*, int left, int top, MyGUI::MouseButton b) { if (b == MyGUI::MouseButton::Left) DragStart(left, top, 2); }
void OnDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton) { ::InterlockedExchange(&g_dragX, left); ::InterlockedExchange(&g_dragY, top); }
void OnRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton)
{
    if (::InterlockedExchange(&g_drag, 0) != 0) ::InterlockedExchange(&g_released, 1);
}
void OnOpacity(MyGUI::ScrollBar*, size_t pos) { ::InterlockedExchange(&g_opNew, (LONG)(pos > 100 ? 100 : pos)); }

/* ---- the chat's state: MAIN THREAD ---- */
enum { kStNone = 0, kStFeed = 1, kStOpen = 2 };
int g_st = kStNone;
chatwire::Ring g_ring;
int g_to = chatwire::kToEveryone;
int g_lastSlot = -1;              /* the last player messaged (this session only) */
chatwire::Cfg g_cfg;
int g_cfgRead = 0;
long long g_cfgDirtyMs = 0;
int g_vw = 0, g_vh = 0;
bool g_linesDirty = false, g_layoutDirty = false;
int g_builtW = -1;
int g_rowH = 22;
bool g_pop = false, g_popDirty = false;
std::string g_search;
std::vector<int> g_in;            /* the other players in the world at the last look */
bool g_needSeed = true;
long long g_inMs = 0;
bool g_enterWas = false;
long long g_fadeMs = 0;
LONG g_escSeen = 0, g_dragSeen = 0;
chatwire::Cfg g_dragFrom;
std::string g_shownTo, g_rectShown, g_partsShown;
struct Words { std::string name, faction; long long ms; bool have; Words() : ms(0), have(false) {} };
std::map<int, Words> g_words;
/* one label of a line, laid out at the canvas width it was measured for */
struct Piece { int x, row, w, colour, slot; bool click; std::string text; };
std::map<long long, std::vector<Piece> > g_pieces;   /* by line seq */
int g_piecesW = -1;
long long g_softStrikes = 0;
volatile LONG g_lastCode = 0;
long long g_throwLogMs = 0;
bool g_busyPrev = true;           /* another text field had the keys, or a modal window was up, at the end of the last tick */
std::map<int, chatwire::InRate> g_inRate;   /* received lines a second, by sender */
long long g_dropBad = 0, g_dropNotMate = 0, g_dropNotMine = 0, g_dropFast = 0, g_dropLogMs = 0;

/* ---- widgets ---- */
MyGUI::Widget* Find(MyGUI::Gui* gui, const char* name) { return gui->findWidgetT(std::string(name), false); }
template <class T> T* As(MyGUI::Widget* w) { return w != 0 ? w->castType<T>(false) : 0; }
MyGUI::Widget* Mk(MyGUI::Widget* parent, const char* type, const char* skin, int l, int t, int w, int h, const char* name)
{
    if (parent == 0) return 0;
    return parent->createWidgetT(std::string(type), std::string(skin), MyGUI::IntCoord(l, t, w > 1 ? w : 1, h > 1 ? h : 1),
                                 MyGUI::Align::Default, std::string(name));
}
/* a root widget on the "Info" layer (above the menus, as REPORT A BUG's window), or "Popup" when Info is unknown */
MyGUI::Widget* MkRoot(MyGUI::Gui* gui, const char* skin, const MyGUI::IntCoord& c, const char* name)
{
    static const char* const kLayers[2] = { "Info", "Popup" };
    for (int k = 0; k < 2; ++k)
    {
        MyGUI::Widget* w = gui->createWidgetT(std::string("Widget"), std::string(skin), c, MyGUI::Align::Default, std::string(kLayers[k]), std::string(name));
        if (w == 0) return 0;
        if (w->getLayer() != 0) return w;
        gui->destroyWidget(w);
    }
    return 0;
}
void ViewSize(int* vw, int* vh)
{
    *vw = 1280; *vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { *vw = s.width; *vh = s.height; } }
}
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }
MyGUI::Colour Dim() { return MyGUI::Colour(0.55f, 0.55f, 0.55f, 1.0f); }
MyGUI::Colour Col(int c) { const chatwire::Rgb r = chatwire::ColourRgb(c); return MyGUI::Colour(r.r, r.g, r.b, 1.0f); }
/* every caption goes through EscapeHash: a typed '#' is never read as a colour code */
void SetText(MyGUI::Widget* w, const std::string& s)
{
    MyGUI::TextBox* t = As<MyGUI::TextBox>(w);
    if (t != 0) t->setCaption(MyGUI::UString(playerstab::EscapeHash(s)));
}
void Style(MyGUI::Widget* w, const std::string& s, MyGUI::Align a, const MyGUI::Colour& c)
{
    MyGUI::TextBox* t = As<MyGUI::TextBox>(w);
    if (t == 0) return;
    SetText(w, s);
    t->setTextAlign(a);
    t->setTextColour(c);
}
bool Inside(MyGUI::Widget* w, MyGUI::Widget* top)
{
    for (int d = 0; w != 0 && d < 40; ++d, w = w->getParent()) if (w == top) return true;
    return false;
}

/* ---- names ---- */
const Words& WordsOf(int slot, long long now)
{
    Words& w = g_words[slot];
    if (!w.have || now - w.ms > 1000)
    {
        std::string n, f;
        PlayersTabPlayerWords(slot, &n, &f);
        if (n.empty()) n = coopslot::PlaceholderName(slot);
        w.name = n; w.faction = f; w.ms = now; w.have = true;
    }
    return w;
}
std::string MyName()
{
    const int me = StoreMySlot();
    std::string n = me >= 0 ? TeamPlayerName(me) : std::string();
    if (n.empty()) n = ConfigFilePlayerName();
    if (n.empty() && me >= 0) n = coopslot::PlaceholderName(me);
    return n;
}
std::string ToWords(int to, long long now)
{
    if (to == chatwire::kToEveryone) return chatwire::EveryoneWord();
    if (to == chatwire::kToFaction) return chatwire::FactionWord();
    return WordsOf(to, now).name;
}
std::string ToLog(int to)
{
    if (to == chatwire::kToEveryone) return "everyone";
    if (to == chatwire::kToFaction) return "faction";
    return "slot " + N(to);
}
const char* KindLog(int kind) { return kind == chatwire::kFaction ? "faction" : (kind == chatwire::kPrivate ? "private" : "everyone"); }
void Push(chatwire::Entry e, long long now)
{
    e.ms = now;
    g_ring.Push(e);
    g_linesDirty = true;
}
void PushGrey(int lineKind, int slot, const std::string& name, const std::string& sentence, long long now)
{
    chatwire::Entry e;
    e.lineKind = lineKind; e.slot = slot; e.name = name; e.text = sentence;
    Push(e, now);
}
/* a received line dropped: counted, and one log line at most every 5 seconds - counts and slots only */
void InDropped(long long* counter, const char* why, int slot, long long now)
{
    ++*counter;
    if (g_dropLogMs != 0 && now - g_dropLogMs < 5000) return;
    g_dropLogMs = now;
    DebugLog(std::string("[CHAT] in dropped (") + why + ") from slot " + N(slot) + " - dropped so far: malformed " + N(g_dropBad)
             + ", not a mate " + N(g_dropNotMate) + ", not for this player " + N(g_dropNotMine) + ", too fast " + N(g_dropFast));
}
/* ESC: when the window closes on one, the engine's own ESC handler (which runs after the tick) may still ask for the pause
   menu - refused while that ESC is held and for kEscHoldMs after the close */
void EscHoldStart()
{
    ::InterlockedExchange(&g_escClosedTick, (LONG)::GetTickCount());
    ::InterlockedExchange(&g_escHold, 1);
}
bool EscHoldOn()
{
    if (::InterlockedCompareExchange(&g_escHold, 0, 0) == 0) return false;
    const DWORD since = ::GetTickCount() - (DWORD)::InterlockedCompareExchange(&g_escClosedTick, 0, 0);
    if ((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0 || since < kEscHoldMs) return true;
    ::InterlockedExchange(&g_escHold, 0);
    return false;
}

/* ---- chat.cfg: beside player.cfg in the data folder, written whole through a temp file and a rename ---- */
std::string CfgPath()
{
    std::string d = ConfigDataDir();
    if (d.empty()) return std::string();
    const char c = d[d.size() - 1];
    if (c != '\\' && c != '/') d += '\\';
    return d + "chat.cfg";
}
bool ReadSmall(const std::string& path, std::string* out)
{
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[4096];
    DWORD got = 0;
    const BOOL ok = ::ReadFile(h, buf, (DWORD)sizeof(buf), &got, 0);
    ::CloseHandle(h);
    if (!ok) return false;
    out->assign(buf, (size_t)got);
    return true;
}
bool WriteSmall(const std::string& path, const std::string& body)
{
    const std::string tmp = path + ".new";
    HANDLE h = U8CreateFile(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    const BOOL ok = ::WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
    if (!ok || wrote != (DWORD)body.size()) { U8DeleteFile(tmp.c_str()); return false; }
    if (!U8MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { U8DeleteFile(tmp.c_str()); return false; }
    return true;
}
std::string PlaceWords(const chatwire::Cfg& c)
{
    return N(c.x) + " " + N(c.y) + " " + N(c.w) + " " + N(c.h);
}
void CfgLoad(int vw, int vh)
{
    if (g_cfgRead) return;
    g_cfgRead = 1;
    std::string text;
    chatwire::Cfg c;
    const std::string path = CfgPath();
    if (!path.empty() && ReadSmall(path, &text) && chatwire::CfgParse(text, &c))
    {
        g_cfg = chatwire::FitRect(c, vw, vh);
        DebugLog("[CHAT] place " + PlaceWords(g_cfg) + " read opacity=" + N(g_cfg.opacity));
    }
    else
    {
        g_cfg = chatwire::DefaultRect(vw, vh);
        DebugLog("[CHAT] place " + PlaceWords(g_cfg) + " default opacity=" + N(g_cfg.opacity) + " (no chat.cfg)");
    }
}
void CfgSave()
{
    g_cfgDirtyMs = 0;
    const std::string path = CfgPath();
    if (path.empty() || !WriteSmall(path, chatwire::CfgFormat(g_cfg)))
    { ErrorLog("[CHAT] place " + PlaceWords(g_cfg) + " NOT saved (chat.cfg could not be written)"); return; }
    DebugLog("[CHAT] place " + PlaceWords(g_cfg) + " saved opacity=" + N(g_cfg.opacity));
}

/* ---- building ---- */
void DropPop(MyGUI::Gui* gui)
{
    MyGUI::Widget* p = Find(gui, kPop);
    if (p != 0) gui->destroyWidget(p);
    g_pop = false;
}
void DropAll(MyGUI::Gui* gui)
{
    DropPop(gui);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* r = Find(gui, kRoot);
    if (r != 0)
    {
        if (im != 0 && Inside(im->getKeyFocusWidget(), r)) im->resetKeyFocusWidget();
        gui->destroyWidget(r);
    }
    MyGUI::Widget* b = Find(gui, kBg);
    if (b != 0) gui->destroyWidget(b);
    g_pieces.clear(); g_piecesW = -1; g_builtW = -1; g_shownTo.clear(); g_rectShown.clear(); g_partsShown.clear();
}
bool BuildAll(MyGUI::Gui* gui)
{
    DropAll(gui);
    const MyGUI::IntCoord c(g_cfg.x, g_cfg.y, g_cfg.w, g_cfg.h);
    MyGUI::Widget* bg = MkRoot(gui, "Kenshi_GenericWindowSkin", c, kBg);
    if (bg == 0) return false;
    MyGUI::Widget* root = MkRoot(gui, "PanelEmpty", c, kRoot);
    if (root == 0) { gui->destroyWidget(bg); return false; }
    MyGUI::Widget* head  = Mk(root, "Widget", "Kenshi_GenericWindowHeaderSkin", 0, 0, c.width, kHeadH, kHead);
    MyGUI::Widget* title = Mk(head, "TextBox", "Kenshi_TextboxPaintedText", 3, 3, c.width - 46, kHeadH - 4, kTitle);
    MyGUI::Widget* close = Mk(head, "Button", "Kenshi_CloseButtonSkin", c.width - 40, 5, 31, 32, kClose);
    MyGUI::Widget* view  = Mk(root, "ScrollView", "Kenshi_ScrollViewEmpty", kPad, kHeadH + 4, c.width - 2 * kPad, 60, kView);
    MyGUI::Widget* toL   = Mk(root, "TextBox", "Kenshi_TextboxPaintedText", 0, 0, 30, kCtlH, kToLbl);
    MyGUI::Widget* toB   = Mk(root, "Button", "Kenshi_Button1", 0, 0, 100, kCtlH, kToBtn);
    MyGUI::Widget* edit  = Mk(root, "EditBox", "Kenshi_EditBox", 0, 0, 100, kCtlH, kEdit);
    MyGUI::Widget* hint  = Mk(root, "TextBox", "Kenshi_TextboxStandardText", 0, 0, 100, kCtlH, kHint);
    MyGUI::Widget* send  = Mk(root, "Button", "Kenshi_Button1", 0, 0, 80, kCtlH, kSend);
    /* OPACITY and its slider sit in the header: its children, so they draw above it and go with it; a press on the slider is
       the slider's, a press on the header's empty part or the title is the drag */
    MyGUI::Widget* opL   = Mk(head, "TextBox", "Kenshi_TextboxPaintedText", 0, 0, 90, kCtlH, kOpLbl);
    MyGUI::Widget* opB   = Mk(head, "ScrollBar", "Kenshi_ScrollBar", 0, 0, 100, kCtlH, kOpBar);
    MyGUI::Widget* grip  = Mk(root, "Widget", "PanelEmpty", 0, 0, 18, 18, kGrip);
    MyGUI::Widget* meas  = Mk(root, "TextBox", "Kenshi_TextboxStandardText", 0, 0, 10, 10, kMeasure);
    MyGUI::EditBox* e = As<MyGUI::EditBox>(edit);
    MyGUI::ScrollView* sv = As<MyGUI::ScrollView>(view);
    MyGUI::ScrollBar* sb = As<MyGUI::ScrollBar>(opB);
    if (head == 0 || As<MyGUI::TextBox>(title) == 0 || As<MyGUI::Button>(close) == 0 || sv == 0 || As<MyGUI::TextBox>(toL) == 0
        || As<MyGUI::Button>(toB) == 0 || e == 0 || As<MyGUI::TextBox>(hint) == 0 || As<MyGUI::Button>(send) == 0
        || As<MyGUI::TextBox>(opL) == 0 || sb == 0 || grip == 0 || As<MyGUI::TextBox>(meas) == 0)
    { DropAll(gui); return false; }
    bg->setNeedMouseFocus(false);
    Style(title, chatwire::WindowTitle(), MyGUI::Align::Left | MyGUI::Align::VCenter, Lit());
    title->setNeedMouseFocus(false);   /* a press on the title reaches the header under it: the drag */
    Style(toL, chatwire::ToLabel(), MyGUI::Align::Left | MyGUI::Align::VCenter, Lit());
    Style(opL, chatwire::OpacityWord(), MyGUI::Align::Left | MyGUI::Align::VCenter, Lit());
    Style(hint, chatwire::TypeHint(), MyGUI::Align::Left | MyGUI::Align::VCenter, Dim());
    hint->setNeedMouseFocus(false);
    toL->setNeedMouseFocus(false); opL->setNeedMouseFocus(false);
    meas->setNeedMouseFocus(false); meas->setAlpha(0.0f);
    As<MyGUI::Button>(send)->setCaption(MyGUI::UString(chatwire::SendWord()));
    As<MyGUI::Button>(send)->setTextColour(Lit());
    As<MyGUI::Button>(toB)->setTextColour(Lit());
    sv->setVisibleHScroll(false);
    e->setEditMultiLine(false);
    e->setMaxTextLength(chatwire::kMaxChars);
    e->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    e->eventEditSelectAccept += MyGUI::newDelegate(OnAccept);
    e->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    send->eventMouseButtonClick += MyGUI::newDelegate(OnSend);
    send->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    close->eventMouseButtonClick += MyGUI::newDelegate(OnClose);
    toB->eventMouseButtonClick += MyGUI::newDelegate(OnToBtn);
    toB->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    head->eventMouseButtonPressed += MyGUI::newDelegate(OnHeadPress);
    head->eventMouseDrag += MyGUI::newDelegate(OnDrag);
    head->eventMouseButtonReleased += MyGUI::newDelegate(OnRelease);
    grip->eventMouseButtonPressed += MyGUI::newDelegate(OnGripPress);
    grip->eventMouseDrag += MyGUI::newDelegate(OnDrag);
    grip->eventMouseButtonReleased += MyGUI::newDelegate(OnRelease);
    sb->setScrollRange(101);
    sb->setScrollPage(5);
    sb->setScrollPosition((size_t)Clamp(g_cfg.opacity, 0, 100));
    sb->eventScrollChangePosition += MyGUI::newDelegate(OnOpacity);
    sb->eventMouseButtonReleased += MyGUI::newDelegate(OnRelease);
    g_linesDirty = true; g_layoutDirty = true;
    return true;
}

/* the width `s` takes on screen, read off the hidden measuring box */
int Measure(MyGUI::TextBox* m, const std::string& s)
{
    m->setCaption(MyGUI::UString(playerstab::EscapeHash(s)));
    return m->getTextSize().width;
}

/* THE PLACE OF EVERYTHING, from g_cfg, the mode and the measured words (chatwire::WindowLayout). Closed (the feed) keeps the
   lines exactly where the open window shows them. */
void Layout(MyGUI::Gui* gui)
{
    g_layoutDirty = false;
    MyGUI::Widget* root = Find(gui, kRoot);
    MyGUI::Widget* bg = Find(gui, kBg);
    if (root == 0 || bg == 0) return;
    const bool open = g_st == kStOpen;
    const int W = g_cfg.w, H = g_cfg.h;
    root->setCoord(MyGUI::IntCoord(g_cfg.x, g_cfg.y, W, H));
    bg->setCoord(MyGUI::IntCoord(g_cfg.x, g_cfg.y + kHeadH, W, H - kHeadH));
    bg->setAlpha((float)Clamp(g_cfg.opacity, 0, 100) / 100.0f);
    bg->setVisible(open);
    root->setNeedMouseFocus(open);    /* closed: nothing of the feed is picked, clicks go through to the game */
    /* the TO button as wide as its caption (the narrowest until UpdateOpenBits first sets it), SEND as its word, the slider
       what the header has left */
    MyGUI::TextBox* m = As<MyGUI::TextBox>(Find(gui, kMeasure));
    const int titleW = m != 0 ? Measure(m, chatwire::WindowTitle()) : 40;
    const int opW = m != 0 ? Measure(m, chatwire::OpacityWord()) : 70;
    const int toW = m != 0 && !g_shownTo.empty() ? Measure(m, g_shownTo) : 0;
    const int sendW = m != 0 ? Measure(m, chatwire::SendWord()) : 40;
    chatwire::WinLayout L;
    chatwire::WindowLayout(W, H, titleW, opW, toW, sendW, &L);
    struct Place { const char* name; chatwire::WinBox b; };
    const Place ps[] = {
        { kHead, L.head }, { kTitle, L.title }, { kOpLbl, L.opWord }, { kOpBar, L.opBar }, { kClose, L.close },
        { kView, L.view }, { kToLbl, L.toWord }, { kToBtn, L.toBtn }, { kEdit, L.edit }, { kHint, L.hint }, { kSend, L.send },
        { kGrip, L.grip }
    };
    for (size_t i = 0; i < sizeof(ps) / sizeof(ps[0]); ++i)
    {
        MyGUI::Widget* w = Find(gui, ps[i].name);
        if (w == 0) continue;
        const chatwire::WinBox& b = ps[i].b;
        w->setCoord(MyGUI::IntCoord(b.x, b.y, b.w > 1 ? b.w : 1, b.h > 1 ? b.h : 1));
        if (ps[i].name != kView) w->setVisible(open);
    }
    MyGUI::ScrollView* sv = As<MyGUI::ScrollView>(Find(gui, kView));
    if (sv != 0) sv->setVisibleVScroll(open);
    root->setVisible(true);
    g_fadeMs = 0;
    if (open)
    {
        const MyGUI::IntCoord c = root->getAbsoluteCoord();
        const std::string r = "x=" + N(c.left) + " y=" + N(c.top) + " w=" + N(c.width) + " h=" + N(c.height);
        if (r != g_rectShown) { g_rectShown = r; DebugLog("[UI] rect chat " + r); }
        const std::string parts = "slider x=" + N(L.opBar.x) + " w=" + N(L.opBar.w) + " edit x=" + N(L.edit.x) + " w=" + N(L.edit.w)
                                + " to w=" + N(L.toBtn.w) + " send w=" + N(L.send.w);
        if (parts != g_partsShown) { g_partsShown = parts; DebugLog("[UI] rect chat parts " + parts); }
    }
}

/* ---- the lines ---- */
/* `text` cut into rows: the first `firstW` wide, the rest `fullW`; at spaces where it can, inside a word only when one word is
   wider than a row (never inside a character) */
std::vector<std::string> Wrap(MyGUI::TextBox* m, const std::string& text, int firstW, int fullW)
{
    std::vector<std::string> out;
    std::string cur;
    int avail = firstW;
    size_t i = 0;
    while (i < text.size())
    {
        size_t j = i;
        while (j < text.size() && text[j] == ' ') ++j;
        while (j < text.size() && text[j] != ' ') ++j;
        const std::string cand = cur + text.substr(i, j - i);
        if (Measure(m, cand) <= avail) { cur = cand; i = j; continue; }
        if (!cur.empty())
        {
            out.push_back(cur); cur.clear(); avail = fullW;
            while (i < j && text[i] == ' ') ++i;
            continue;
        }
        std::string piece;
        size_t k = i;
        while (k < j)
        {
            size_t n = k + 1;
            while (n < j && chatwire::IsCont((unsigned char)text[n])) ++n;
            const std::string more = piece + text.substr(k, n - k);
            if (!piece.empty() && Measure(m, more) > avail) break;
            piece = more; k = n;
        }
        out.push_back(piece); avail = fullW; i = k;
    }
    if (!cur.empty() || out.empty()) out.push_back(cur);
    return out;
}
void PartsOf(const chatwire::Entry& e, std::string* pre, std::string* nm, std::string* post)
{
    pre->clear(); nm->clear(); post->clear();
    if (e.lineKind == chatwire::kLineTalk) { chatwire::LineParts(e.kind, e.dir, e.name, e.text, pre, nm, post); return; }
    if (e.slot >= 0 && !e.name.empty() && e.text.compare(0, e.name.size(), e.name) == 0) { *nm = e.name; *post = e.text.substr(e.name.size()); return; }
    *post = e.text;
}
const std::vector<Piece>& PiecesOf(MyGUI::TextBox* m, const chatwire::Entry& e, int cw, int me)
{
    std::map<long long, std::vector<Piece> >::iterator it = g_pieces.find(e.seq);
    if (it != g_pieces.end()) return it->second;
    std::vector<Piece>& ps = g_pieces[e.seq];
    std::string pre, nm, post;
    PartsOf(e, &pre, &nm, &post);
    const int colour = chatwire::LineColour(e.lineKind, e.kind);
    int x = 0;
    if (!pre.empty()) { Piece p; p.x = x; p.row = 0; p.w = Measure(m, pre); p.colour = colour; p.slot = -1; p.click = false; p.text = pre; ps.push_back(p); x += p.w; }
    if (!nm.empty())
    {
        Piece p; p.x = x; p.row = 0; p.w = Measure(m, nm); p.colour = colour; p.slot = e.slot; p.text = nm;
        p.click = e.slot >= 0 && e.slot != me;   /* another player's name: clicking it sets TO to that player */
        ps.push_back(p); x += p.w;
    }
    const std::vector<std::string> rows = Wrap(m, post, cw - x, cw);
    for (size_t r = 0; r < rows.size(); ++r)
    {
        Piece p; p.x = r == 0 ? x : 0; p.row = (int)r; p.text = rows[r]; p.w = Measure(m, rows[r]) + 4; p.colour = colour; p.slot = -1; p.click = false;
        ps.push_back(p);
    }
    return ps;
}
void RebuildLines(MyGUI::Gui* gui, long long now)
{
    MyGUI::ScrollView* sv = As<MyGUI::ScrollView>(Find(gui, kView));
    MyGUI::TextBox* m = As<MyGUI::TextBox>(Find(gui, kMeasure));
    if (sv == 0 || m == 0) return;
    MyGUI::Widget* old = Find(gui, kCanvas);
    if (old != 0) gui->destroyWidget(old);
    const MyGUI::IntCoord vc = sv->getViewCoord();
    const int cw = vc.width > 40 ? vc.width : 40;
    if (cw != g_piecesW) { g_pieces.clear(); g_piecesW = cw; }
    m->setCaption(MyGUI::UString("Ag"));
    g_rowH = Clamp(m->getTextSize().height + 2, 14, 48);
    const int me = StoreMySlot();
    /* names first seen before they were known (a line that came in before its sender's name) */
    for (size_t i = 0; i < g_ring.lines.size(); ++i)
    {
        chatwire::Entry& e = g_ring.lines[i];
        if (e.name.empty() && e.slot >= 0) e.name = WordsOf(e.slot, now).name;
    }
    int rows = 0;
    for (size_t i = 0; i < g_ring.lines.size(); ++i)
    {
        const std::vector<Piece>& ps = PiecesOf(m, g_ring.lines[i], cw - 6, me);
        int most = 0;
        for (size_t k = 0; k < ps.size(); ++k) if (ps[k].row > most) most = ps[k].row;
        rows += most + 1;
    }
    const int total = rows * g_rowH;
    const int canvasH = total > vc.height ? total : vc.height;
    const int top = canvasH - total;   /* few lines sit at the bottom, by the controls (or where they were) */
    MyGUI::Widget* canvas = Mk(sv, "Widget", "PanelEmpty", 0, 0, cw, canvasH, kCanvas);
    if (canvas == 0) return;
    canvas->setInheritsPick(true);   /* the canvas itself is never picked; a name label on it is */
    int y = top;
    for (size_t i = 0; i < g_ring.lines.size(); ++i)
    {
        const chatwire::Entry& e = g_ring.lines[i];
        const std::vector<Piece>& ps = PiecesOf(m, e, cw - 6, me);
        int most = 0;
        const std::string ms = N(e.ms), seq = N(e.seq);
        for (size_t k = 0; k < ps.size(); ++k)
        {
            const Piece& p = ps[k];
            if (p.row > most) most = p.row;
            if (k == 0 || ps[k - 1].row != p.row)
            {
                /* the row's dark band, made before its words so it draws under them: from 3 before the first word to 3 past the last
                   word's end (words sit 3 in from the canvas's left, so the band shows on both sides), the row's full height. WhiteSkin is the game's own flat fill (common_skins.xml), tinted black. It
                   takes no clicks, so a name on it still does; FadeTick fades it with its line ("band"). */
                int left = 0, right = 0;
                bool any = false;
                for (size_t j = k; j < ps.size() && ps[j].row == p.row; ++j)
                {
                    if (ps[j].text.empty()) continue;
                    if (!any || ps[j].x < left) left = ps[j].x;
                    if (!any || ps[j].x + ps[j].w > right) right = ps[j].x + ps[j].w;
                    any = true;
                }
                MyGUI::Widget* band = any ? Mk(canvas, "Widget", "WhiteSkin", left, y + p.row * g_rowH, right - left + 6, g_rowH, "") : 0;
                if (band != 0)
                {
                    band->setColour(MyGUI::Colour(0.0f, 0.0f, 0.0f, 1.0f));
                    band->setAlpha(chatwire::kLineBandAlpha);
                    band->setNeedMouseFocus(false);
                    band->setUserString("ms", ms);
                    band->setUserString("band", "1");
                }
            }
            if (p.text.empty()) continue;
            MyGUI::Widget* l = Mk(canvas, "TextBox", "Kenshi_TextboxStandardText", p.x + 3, y + p.row * g_rowH, p.w, g_rowH, "");
            if (l == 0) continue;
            Style(l, p.text, MyGUI::Align::Left | MyGUI::Align::VCenter, Col(p.colour));
            l->setUserString("ms", ms);
            l->setUserString("seq", seq);
            l->setNeedMouseFocus(p.click);
            if (p.click) { l->setUserString("slot", N(p.slot)); l->eventMouseButtonClick += MyGUI::newDelegate(OnNameClick); }
        }
        y += (most + 1) * g_rowH;
    }
    /* drop the measured rows of lines no longer kept */
    if (g_pieces.size() > g_ring.lines.size() && !g_ring.lines.empty())
    {
        const long long oldest = g_ring.lines[0].seq;
        while (!g_pieces.empty() && g_pieces.begin()->first < oldest) g_pieces.erase(g_pieces.begin());
    }
    sv->setCanvasSize(cw, canvasH);
    sv->setViewOffset(MyGUI::IntPoint(0, -1000000));   /* the newest line in view (MyGUI clamps to the bottom) */
    g_builtW = cw;
    g_linesDirty = false;
    g_fadeMs = 0;
}
int ViewW(MyGUI::Gui* gui)
{
    MyGUI::ScrollView* sv = As<MyGUI::ScrollView>(Find(gui, kView));
    return sv != 0 ? sv->getViewCoord().width : -1;
}
/* closed: each line fully shown for 10 s, then faded out; the feed hidden when nothing shows. Open: every line at full. A
   row's band fades with its line, at kLineBandAlpha of the line's alpha, and is not counted: `shown` counts words only. */
int FadeTick(MyGUI::Gui* gui, long long now, bool force)
{
    if (!force && g_fadeMs != 0 && now - g_fadeMs < 100) return -1;
    g_fadeMs = now;
    MyGUI::Widget* canvas = Find(gui, kCanvas);
    MyGUI::Widget* root = Find(gui, kRoot);
    if (canvas == 0 || root == 0) return 0;
    int shown = 0;
    const size_t n = canvas->getChildCount();
    for (size_t i = 0; i < n; ++i)
    {
        MyGUI::Widget* l = canvas->getChildAt(i);
        if (l == 0) continue;
        const float a = g_st == kStOpen ? 1.0f : chatwire::FadeAlpha(now - _atoi64(l->getUserString("ms").c_str()));
        const bool band = !l->getUserString("band").empty();
        if (a > 0.0f && !band) ++shown;
        const float want = band ? a * chatwire::kLineBandAlpha : a;
        const float was = l->getAlpha();
        if (was - want > 0.01f || want - was > 0.01f) l->setAlpha(want);
    }
    root->setVisible(g_st == kStOpen || shown > 0);
    return shown;
}

/* ---- the TO list ---- */
struct PopRow { int to; std::string text; bool sep; };
void FillPopRows(MyGUI::Gui* gui, long long now)
{
    g_popDirty = false;
    MyGUI::Widget* pop = Find(gui, kPop);
    MyGUI::Widget* toB = Find(gui, kToBtn);
    if (pop == 0 || toB == 0) return;
    MyGUI::EditBox* se = As<MyGUI::EditBox>(Find(gui, kPopEdit));
    if (se != 0) g_search = coopbug::CaptionToPlain(se->getCaption().asUTF8());
    MyGUI::Widget* hint = Find(gui, kPopHint);
    if (hint != 0) hint->setVisible(g_search.empty());
    MyGUI::Widget* old = Find(gui, kPopRows);
    if (old != 0) gui->destroyWidget(old);
    std::vector<PopRow> rows;
    { PopRow r; r.to = chatwire::kToEveryone; r.text = chatwire::EveryoneWord(); r.sep = false; rows.push_back(r); }
    { PopRow r; r.to = chatwire::kToFaction; r.text = chatwire::FactionWord(); r.sep = false; rows.push_back(r); }
    { PopRow r; r.to = 0; r.text = chatwire::SeparatorWord(); r.sep = true; rows.push_back(r); }
    int players = 0;
    for (size_t i = 0; i < g_in.size(); ++i)
    {
        const Words& w = WordsOf(g_in[i], now);
        if (!chatwire::SearchMatch(w.name, g_search)) continue;
        PopRow r; r.to = g_in[i]; r.text = w.name + (w.faction.empty() ? std::string() : "   " + w.faction); r.sep = false;
        rows.push_back(r); ++players;
    }
    const int rowH = g_rowH + 6;
    const int popW = pop->getWidth();
    const int listY = kPad + kCtlH + 4;
    const int H = listY + (int)rows.size() * rowH + kPad;
    MyGUI::Widget* box = Mk(pop, "Widget", "PanelEmpty", kPad, listY, popW - 2 * kPad, (int)rows.size() * rowH, kPopRows);
    if (box == 0) return;
    box->setInheritsPick(true);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        MyGUI::Widget* l = Mk(box, "TextBox", "Kenshi_TextboxStandardText", 4, (int)i * rowH, popW - 2 * kPad - 8, rowH, "");
        if (l == 0) continue;
        Style(l, rows[i].text, MyGUI::Align::Left | MyGUI::Align::VCenter, rows[i].sep ? Dim() : Lit());
        if (rows[i].sep) { l->setNeedMouseFocus(false); continue; }
        l->setUserString("to", N(rows[i].to));
        l->eventMouseButtonClick += MyGUI::newDelegate(OnPickRow);
    }
    /* above the TO button (the window sits low on the screen), below it when there is no room above */
    const MyGUI::IntCoord tc = toB->getAbsoluteCoord();
    int y = tc.top - H;
    if (y < 0) y = tc.top + tc.height;
    pop->setCoord(MyGUI::IntCoord(tc.left, y, popW, H));
    DebugLog("[CHAT] tolist rows=" + N((long long)rows.size() - 1) + " players=" + N(players) + " search chars=" + N((long long)chatwire::Utf8Chars(g_search)));
}
void OpenPop(MyGUI::Gui* gui, long long now)
{
    DropPop(gui);
    const int popW = Clamp(g_cfg.w * 70 / 100, 240, 420);
    MyGUI::Widget* pop = MkRoot(gui, "Kenshi_GenericWindowSkin", MyGUI::IntCoord(g_cfg.x, g_cfg.y, popW, 200), kPop);
    if (pop == 0) return;
    MyGUI::EditBox* se = As<MyGUI::EditBox>(Mk(pop, "EditBox", "Kenshi_EditBox", kPad, kPad, popW - 2 * kPad, kCtlH, kPopEdit));
    MyGUI::Widget* hint = Mk(pop, "TextBox", "Kenshi_TextboxStandardText", kPad + 8, kPad, popW - 2 * kPad - 16, kCtlH, kPopHint);
    if (se == 0 || hint == 0) { DropPop(gui); return; }
    Style(hint, chatwire::SearchHint(), MyGUI::Align::Left | MyGUI::Align::VCenter, Dim());
    hint->setNeedMouseFocus(false);
    se->setEditMultiLine(false);
    se->setMaxTextLength(40);
    se->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    se->eventEditTextChange += MyGUI::newDelegate(OnSearchChange);
    se->eventEditSelectAccept += MyGUI::newDelegate(OnSearchAccept);
    se->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    g_search.clear();
    g_pop = true;
    FillPopRows(gui, now);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) im->setKeyFocusWidget(se);
}

/* ---- opening, closing, sending ---- */
void FocusEdit(MyGUI::Gui* gui)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* e = Find(gui, kEdit);
    if (im != 0 && e != 0) im->setKeyFocusWidget(e);
}
bool EnsureBuilt(MyGUI::Gui* gui)
{
    if (Find(gui, kRoot) != 0 && Find(gui, kBg) != 0) return true;
    if (!BuildAll(gui)) { ErrorLog("[CHAT] the window could not be built (a Kenshi skin was not made)"); return false; }
    if (g_st == kStNone) g_st = kStFeed;
    return true;
}
void Open(MyGUI::Gui* gui, const char* how, int to, long long now)
{
    if (!EnsureBuilt(gui)) return;
    g_to = to;
    g_st = kStOpen;
    Layout(gui);
    FocusEdit(gui);
    ::InterlockedExchange(&g_uiUp, 1);
    DebugLog(std::string("[CHAT] box opened (") + how + ") to=" + ToLog(g_to));
    (void)now;
}
void Close(MyGUI::Gui* gui, const char* why, bool clearText)
{
    DropPop(gui);
    if (clearText) { MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kEdit)); if (e != 0) e->setCaption(MyGUI::UString("")); }
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* r = Find(gui, kRoot);
    if (im != 0 && r != 0 && Inside(im->getKeyFocusWidget(), r)) im->resetKeyFocusWidget();   /* the game's keys work again */
    g_st = kStFeed;
    Layout(gui);
    ::InterlockedExchange(&g_uiUp, 0);
    DebugLog(std::string("[CHAT] box closed (") + why + ")");
}
/* the other members of this player's faction on this game's table (*all, in the world or not), and those of them in the
   world now (read only when the link is up - SendPlan decides on *all alone without it) */
std::vector<int> MatesInWorld(int* all)
{
    std::vector<int> out;
    const std::vector<int> m = TeamMatesOfMine();
    const int me = StoreMySlot();
    *all = 0;
    for (size_t i = 0; i < m.size(); ++i)
    {
        if (m[i] == me) continue;
        ++*all;
        if (StoreRosterSlotInWorld(m[i]) == 1) out.push_back(m[i]);
    }
    return out;
}
/* one message to whoever TO names: returns the chatwire::Plan that was followed */
int SendText(const std::string& text, long long now)
{
    const int kind = g_to == chatwire::kToEveryone ? chatwire::kEveryone : (g_to == chatwire::kToFaction ? chatwire::kFaction : chatwire::kPrivate);
    int mateCount = 0;
    const std::vector<int> mates = kind == chatwire::kFaction ? MatesInWorld(&mateCount) : std::vector<int>();
    const bool targetIn = kind == chatwire::kPrivate && StoreRosterSlotInWorld(g_to) == 1;
    const bool listKnown = StoreWorldPlayerCount() >= 0 && (kind != chatwire::kFaction || TeamTableKnown());   /* no player list or faction table on this link yet (just after it came back): not connected */
    std::vector<chatwire::Route> routes;
    int plan = chatwire::SendPlan(kind, g_to, mateCount, mates, targetIn, listKnown, StoreLiveReady(), &routes);
    const long long chars = (long long)chatwire::Utf8Chars(text);
    if (plan == chatwire::kPlanSend)
    {
        std::vector<char> bytes;
        int sent = 0;
        if (chatwire::Encode(kind, kind == chatwire::kPrivate ? g_to : -1, text, &bytes))
            for (size_t i = 0; i < routes.size(); ++i)
            {
                const unsigned route = routes[i].route == chatwire::kRouteSlotNo ? (unsigned)cooplive::kRouteSlot : (unsigned)cooplive::kRouteWorld;
                if (StoreSendLive(route, (unsigned)routes[i].target, cooplive::kInnerChat, bytes, true)) ++sent;
            }
        if (sent == 0) plan = chatwire::kPlanNotConnected;
        else
        {
            DebugLog(std::string("[CHAT] sent ") + KindLog(kind) + " to=" + ToLog(g_to) + " chars=" + N(chars)
                     + (kind == chatwire::kFaction ? " copies=" + N(sent) : std::string()));
            chatwire::Entry e;
            e.kind = kind; e.dir = chatwire::kOut; e.text = text;
            if (kind == chatwire::kPrivate) { e.slot = g_to; e.name = WordsOf(g_to, now).name; g_lastSlot = g_to; }
            else { e.slot = StoreMySlot(); e.name = MyName(); }
            Push(e, now);
            return plan;
        }
    }
    if (plan == chatwire::kPlanSelfOnly)
    {
        DebugLog("[CHAT] to=self only chars=" + N(chars));
        chatwire::Entry e;
        e.kind = chatwire::kFaction; e.dir = chatwire::kOut; e.text = text; e.slot = StoreMySlot(); e.name = MyName();
        Push(e, now);
        return plan;
    }
    if (plan == chatwire::kPlanGone)
    {
        const std::string name = WordsOf(g_to, now).name;
        DebugLog("[CHAT] refused gone (" + ToLog(g_to) + ") - TO stays on that player, so a second Enter sends nothing");
        PushGrey(chatwire::kLineGone, g_to, name, chatwire::GoneLine(name), now);
        return plan;
    }
    DebugLog("[CHAT] refused not-connected (" + ToLog(g_to) + ")");
    PushGrey(chatwire::kLineNotConnected, -1, std::string(), chatwire::NotConnectedLine(), now);
    return chatwire::kPlanNotConnected;
}
void SendFromBox(MyGUI::Gui* gui, long long now)
{
    MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kEdit));
    if (e == 0) return;
    const std::string text = chatwire::CleanTyped(coopbug::CaptionToPlain(e->getCaption().asUTF8()));
    if (text.empty()) { Close(gui, "nothing typed", true); return; }
    const int plan = SendText(text, now);
    if (plan == chatwire::kPlanSend || plan == chatwire::kPlanSelfOnly) Close(gui, "sent", true);
    /* gone / not connected: the window stays open with the text in the line, to send again */
}
void Pick(MyGUI::Gui* gui, int to, const char* how)
{
    g_to = to;
    DropPop(gui);
    FocusEdit(gui);
    DebugLog(std::string("[CHAT] to=") + ToLog(g_to) + " (" + how + ")");
}
void PickFirst(MyGUI::Gui* gui, long long now)
{
    for (size_t i = 0; i < g_in.size(); ++i)
        if (chatwire::SearchMatch(WordsOf(g_in[i], now).name, g_search)) { Pick(gui, g_in[i], "search"); return; }
}

/* ---- Enter, from the key's state (as tags.cpp's Insert): only on the press, only for this game's own window ---- */
bool EnterPressed()
{
    const bool down = (::GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool edge = down && !g_enterWas;
    g_enterWas = down;
    return edge;
}
/* another text field has the keys, or a modal window is up */
bool OtherBusy()
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    return im != 0 && (im->isFocusKey() || im->isModalAny());
}
bool EnterMayOpen()
{
    HWND fg = ::GetForegroundWindow();
    DWORD pid = 0;
    if (fg == 0 || (::GetWindowThreadProcessId(fg, &pid), pid) != ::GetCurrentProcessId()) return false;
    /* typing elsewhere or a modal window up - this tick or at the end of the last one, so the Enter that confirms another
       text field or closes a message box does not open the chat on the same press */
    if (OtherBusy() || g_busyPrev) return false;
    if (UiPauseMenuVisible() == 1) return false;
    return true;
}

/* ---- who is in the world: join / leave lines ---- */
void JoinTick(long long now)
{
    if (now - g_inMs < 500) return;
    g_inMs = now;
    const int me = StoreMySlot();
    /* this game's own link down, or this game not in the world yet: nothing is said, and the next look only takes note */
    if (!StoreLiveReady() || me < 0 || StoreRosterSlotInWorld(me) != 1) { g_needSeed = true; return; }
    const int count = StoreWorldPlayerCount();
    if (count < 0) { g_needSeed = true; return; }
    /* slot numbers are the world's player numbers; looked at a little past the count in case they are not 0..count-1 */
    const int bound = count + 16 < 1024 ? count + 16 : 1024;
    std::vector<int> now_in;
    for (int s = 0; s < bound; ++s) if (s != me && StoreRosterSlotInWorld(s) == 1) now_in.push_back(s);
    if (g_needSeed)
    {
        g_in = now_in; g_needSeed = false;
        DebugLog("[CHAT] in-world set taken (" + N((long long)g_in.size()) + " other players) - no join lines for them");
        return;
    }
    std::vector<int> arrived, left;
    mparrive::InWorldDiff(g_in, now_in, &arrived, &left);
    for (size_t i = 0; i < arrived.size(); ++i)
    {
        const std::string name = WordsOf(arrived[i], now).name;
        PushGrey(chatwire::kLineJoin, arrived[i], name, chatwire::JoinLine(name), now);
        DebugLog("[CHAT] join slot " + N(arrived[i]));
    }
    for (size_t i = 0; i < left.size(); ++i)
    {
        const std::string name = WordsOf(left[i], now).name;
        PushGrey(chatwire::kLineLeave, left[i], name, chatwire::LeaveLine(name), now);
        DebugLog("[CHAT] leave slot " + N(left[i]));
    }
    if (!arrived.empty() || !left.empty()) g_popDirty = g_pop;
    g_in = now_in;
}

void TakeDown(MyGUI::Gui* gui, const char* why)
{
    const bool had = g_st != kStNone || !g_ring.lines.empty();
    if (gui != 0) DropAll(gui);
    g_ring.Clear(); g_in.clear(); g_needSeed = true; g_words.clear(); g_inRate.clear();
    g_to = chatwire::kToEveryone; g_lastSlot = -1;   /* the last player messaged is not kept past the session */
    g_st = kStNone; g_pop = false;
    ::InterlockedExchange(&g_uiUp, 0);
    if (had) DebugLog(std::string("[CHAT] taken down (") + why + ")");
}

void UpdateOpenBits(MyGUI::Gui* gui, long long now)
{
    MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kEdit));
    MyGUI::Widget* hint = Find(gui, kHint);
    const std::string to = ToWords(g_to, now) + "  v";
    if (to != g_shownTo)
    {
        g_shownTo = to;
        MyGUI::Button* b = As<MyGUI::Button>(Find(gui, kToBtn));
        if (b != 0) b->setCaption(MyGUI::UString(playerstab::EscapeHash(to)));
        Layout(gui);   /* the TO button takes its new caption's width; Layout shows every control, so the hint is set after it */
    }
    if (e != 0 && hint != 0) hint->setVisible(e->getTextLength() == 0);
    /* typing goes to the chat while it is open: the box, or the TO list's search line */
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* root = Find(gui, kRoot);
    MyGUI::Widget* pop = Find(gui, kPop);
    if (im == 0 || root == 0) return;
    if (im->isModalAny()) return;   /* a modal window up keeps the keys */
    MyGUI::Widget* f = im->getKeyFocusWidget();
    if (Inside(f, root) || (pop != 0 && Inside(f, pop))) return;
    MyGUI::Widget* want = pop != 0 ? Find(gui, kPopEdit) : Find(gui, kEdit);
    if (want != 0) im->setKeyFocusWidget(want);
}
void DragTick(MyGUI::Gui* gui)
{
    const LONG serial = ::InterlockedCompareExchange(&g_dragSerial, 0, 0);
    if (serial != g_dragSeen) { g_dragSeen = serial; g_dragFrom = g_cfg; }
    const LONG kind = ::InterlockedCompareExchange(&g_drag, 0, 0);
    if (kind != 0 && g_st == kStOpen)
    {
        const int dx = (int)g_dragX - (int)g_pressX, dy = (int)g_dragY - (int)g_pressY;
        chatwire::Cfg c = g_dragFrom;
        if (kind == 1) { c.x += dx; c.y += dy; } else { c.w += dx; c.h += dy; }
        c.opacity = g_cfg.opacity;
        c = chatwire::FitRect(c, g_vw, g_vh);
        if (c.x != g_cfg.x || c.y != g_cfg.y || c.w != g_cfg.w || c.h != g_cfg.h) { g_cfg = c; Layout(gui); }
    }
    if (::InterlockedExchange(&g_released, 0) != 0) { CfgSave(); g_linesDirty = true; }
}
std::string StateLine(MyGUI::Gui* gui, long long now)
{
    const int shown = gui != 0 ? FadeTick(gui, now, true) : 0;
    return "[CHAT] state to=" + ToLog(g_to) + " open=" + N(g_st == kStOpen ? 1 : 0) + " lines=" + N((long long)g_ring.lines.size())
         + " opacity=" + N(g_cfg.opacity) + " shown=" + N(shown) + " place=" + PlaceWords(g_cfg) + " last=" + N(g_lastSlot)
         + " in-dropped=" + N(g_dropBad + g_dropNotMate + g_dropNotMine + g_dropFast);
}

void TickBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    if (!GameplayRunning()) { if (g_st != kStNone || !g_ring.lines.empty()) TakeDown(gui, "the world is not running"); return; }
    const long long now = NowMs();
    int vw = 0, vh = 0;
    ViewSize(&vw, &vh);
    CfgLoad(vw, vh);
    if (vw != g_vw || vh != g_vh) { g_vw = vw; g_vh = vh; g_cfg = chatwire::FitRect(g_cfg, vw, vh); g_layoutDirty = true; g_linesDirty = true; }
    JoinTick(now);
    const LONG took = ::InterlockedCompareExchange(&g_escTook, 0, 0);
    if (took != g_escSeen) { g_escSeen = took; DebugLog("[CHAT] esc took the pause menu's show"); }
    const int act = (int)::InterlockedExchange(&g_act, kActNone);
    const int esc = (int)::InterlockedExchange(&g_esc, 0);
    const int tabs = (int)::InterlockedExchange(&g_tabN, 0);
    const int pickHas = (int)::InterlockedExchange(&g_pickHas, 0);
    const int openReq = (int)::InterlockedExchange(&g_openReq, 0);
    const bool enter = EnterPressed();   /* every frame, so a held key is one press */
    if (g_st != kStOpen)
    {
        if (openReq) Open(gui, "players-tab", (int)g_openSlot, now);
        else if (enter && EnterMayOpen()) Open(gui, "enter", g_to, now);
    }
    else
    {
        if (openReq) { g_to = (int)g_openSlot; DropPop(gui); DebugLog("[CHAT] to=" + ToLog(g_to) + " (players-tab)"); }
        if (pickHas) Pick(gui, (int)g_pickTo, g_pickWhy == 2 ? "name" : "list");
        for (int i = 0; i < tabs && i < 8; ++i) g_to = chatwire::TabNext(g_to, g_lastSlot);
        if (tabs > 0) DebugLog("[CHAT] to=" + ToLog(g_to) + " (tab)");
        if (act == kActToList) { if (g_pop) DropPop(gui); else OpenPop(gui, now); }
        else if (act == kActSearchPick) PickFirst(gui, now);
        else if (act == kActSend) SendFromBox(gui, now);
        else if (act == kActClose) Close(gui, "close", true);
        if (esc && g_st == kStOpen) { if (g_pop) { DropPop(gui); FocusEdit(gui); } else { Close(gui, "esc", true); EscHoldStart(); } }
    }
    if (g_st == kStNone && !g_ring.lines.empty() && !EnsureBuilt(gui)) return;
    if (g_st == kStNone) return;
    if (Find(gui, kRoot) == 0 || Find(gui, kBg) == 0) { if (!BuildAll(gui)) { g_st = kStNone; ::InterlockedExchange(&g_uiUp, 0); return; } }
    DragTick(gui);
    const LONG op = ::InterlockedExchange(&g_opNew, -1);
    if (op >= 0 && op != g_cfg.opacity)
    {
        g_cfg.opacity = (int)op;
        MyGUI::Widget* bg = Find(gui, kBg);
        if (bg != 0) bg->setAlpha((float)op / 100.0f);
        g_cfgDirtyMs = now;
    }
    if (g_layoutDirty) Layout(gui);
    if ((g_linesDirty || g_builtW != ViewW(gui)) && ::InterlockedCompareExchange(&g_drag, 0, 0) != 2) RebuildLines(gui, now);
    if (g_pop && (g_popDirty || ::InterlockedExchange(&g_searchDirty, 0) != 0)) FillPopRows(gui, now);
    FadeTick(gui, now, false);
    if (g_st == kStOpen) UpdateOpenBits(gui, now);
    if (g_cfgDirtyMs != 0 && now - g_cfgDirtyMs > 1000) CfgSave();
    ::InterlockedExchange(&g_uiUp, g_st == kStOpen ? 1 : 0);
}
__declspec(noinline) void TickInner()
{
    EscHoldOn();                 /* an ESC hold that is over ends here, so a later ESC opens the pause menu again */
    TickBody();
    g_busyPrev = OtherBusy();    /* read by the next tick's Enter */
}

/* ---- the lever (TEST-ONLY): only fixed words and numbers, never free text ---- */
std::vector<std::string> g_cmdArgs;
std::string g_cmdResult;
bool ParseTo(const std::string& s, int* to)
{
    if (s == "everyone") { *to = chatwire::kToEveryone; return true; }
    if (s == "faction") { *to = chatwire::kToFaction; return true; }
    if (s.empty() || s.size() > 5) return false;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    *to = atoi(s.c_str());
    return true;
}
std::string Arg(size_t i) { return i < g_cmdArgs.size() ? g_cmdArgs[i] : std::string(); }
bool FillSample(MyGUI::Gui* gui, int k)
{
    MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kEdit));
    if (e == 0 || k < 1 || k > chatwire::kSampleCount) return false;
    e->setCaption(MyGUI::UString(playerstab::EscapeHash(chatwire::Sample(k))));
    return true;
}
/* the most words each command has: a command with more is refused, so no free text rides in on one */
size_t MaxWords(const std::string& v)
{
    if (v == "open" || v == "click" || v == "opacity") return 2;
    if (v == "send") return 4;
    if (v == "fill" || v == "tolist") return 3;
    if (v == "move") return 5;
    return 1;   /* state, tab, esc, close, and a verb the lever does not have */
}
__declspec(noinline) void CmdInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    const long long now = NowMs();
    const std::string verb = Arg(0);
    if (g_cmdArgs.size() > MaxWords(verb)) { g_cmdResult = "error chat usage: more words than the command takes"; return; }
    if (verb == "state") { g_cmdResult = StateLine(gui, now); DebugLog(g_cmdResult); g_cmdResult = "ok chat " + g_cmdResult.substr(7); return; }
    if (gui == 0 || !GameplayRunning()) { g_cmdResult = "error chat no-world"; return; }
    int to = 0;
    if (verb == "open")
    {
        if (!ParseTo(Arg(1), &to)) { g_cmdResult = "error chat usage: open everyone|faction|<slot>"; return; }
        if (g_st == kStOpen) { g_to = to; DebugLog("[CHAT] to=" + ToLog(g_to) + " (lever)"); }
        else Open(gui, "lever", to, now);
        g_cmdResult = g_st == kStOpen ? "ok chat open" : "error chat not-built";
        return;
    }
    if (verb == "send")
    {
        const int k = atoi(Arg(3).c_str());
        if (!ParseTo(Arg(1), &to) || Arg(2) != "sample" || k < 1 || k > chatwire::kSampleCount) { g_cmdResult = "error chat usage: send everyone|faction|<slot> sample <1-4>"; return; }
        if (g_st != kStOpen) Open(gui, "lever", to, now); else g_to = to;
        MyGUI::Widget* s = Find(gui, kSend);
        if (g_st != kStOpen || s == 0 || !FillSample(gui, k)) { g_cmdResult = "error chat not-open"; return; }
        s->eventMouseButtonClick(s);   /* SEND's own handler: the next tick sends, as a click would */
        g_cmdResult = "ok chat send";
        return;
    }
    if (g_st != kStOpen && verb != "move" && verb != "opacity") { g_cmdResult = "error chat not-open"; return; }
    if (verb == "fill")
    {
        const int k = atoi(Arg(2).c_str());
        g_cmdResult = (Arg(1) == "sample" && FillSample(gui, k)) ? "ok chat fill" : "error chat usage: fill sample <1-4>";
        return;
    }
    if (verb == "tolist")
    {
        if (!g_pop) OpenPop(gui, now);
        if (!g_pop) { g_cmdResult = "error chat tolist not-built"; return; }
        if (Arg(1) == "search")
        {
            const std::string letters = Arg(2);
            for (size_t i = 0; i < letters.size(); ++i)
                if (!((letters[i] >= 'a' && letters[i] <= 'z') || (letters[i] >= 'A' && letters[i] <= 'Z') || (letters[i] >= '0' && letters[i] <= '9')))
                { g_cmdResult = "error chat usage: tolist search <letters>"; return; }
            MyGUI::EditBox* se = As<MyGUI::EditBox>(Find(gui, kPopEdit));
            if (se != 0) se->setCaption(MyGUI::UString(letters));
            FillPopRows(gui, now);
        }
        MyGUI::Widget* rows = Find(gui, kPopRows);
        g_cmdResult = "ok chat tolist rows=" + N(rows != 0 ? (long long)rows->getChildCount() - 1 : -1);
        return;
    }
    if (verb == "tab") { ::InterlockedIncrement(&g_tabN); g_cmdResult = "ok chat tab"; return; }
    if (verb == "click")
    {
        const int n = atoi(Arg(1).c_str());
        if (n < 1 || (size_t)n > g_ring.lines.size()) { g_cmdResult = "error chat usage: click <line from the newest, 1 = newest>"; return; }
        if (g_linesDirty) RebuildLines(gui, now);
        const std::string seq = N(g_ring.lines[g_ring.lines.size() - (size_t)n].seq);
        MyGUI::Widget* canvas = Find(gui, kCanvas);
        const size_t c = canvas != 0 ? canvas->getChildCount() : 0;
        for (size_t i = 0; i < c; ++i)
        {
            MyGUI::Widget* l = canvas->getChildAt(i);
            if (l != 0 && l->getUserString("seq") == seq && !l->getUserString("slot").empty())
            { l->eventMouseButtonClick(l); g_cmdResult = "ok chat click slot " + l->getUserString("slot"); return; }
        }
        g_cmdResult = "error chat click: that line has no player's name to click";
        return;
    }
    if (verb == "opacity")
    {
        const int v = atoi(Arg(1).c_str());
        if (Arg(1).empty() || v < 0 || v > 100) { g_cmdResult = "error chat usage: opacity <0-100>"; return; }
        MyGUI::ScrollBar* sb = As<MyGUI::ScrollBar>(Find(gui, kOpBar));
        if (sb != 0) sb->setScrollPosition((size_t)v);
        ::InterlockedExchange(&g_opNew, v);
        g_cmdResult = "ok chat opacity";
        return;
    }
    if (verb == "move")
    {
        chatwire::Cfg c;
        c.x = atoi(Arg(1).c_str()); c.y = atoi(Arg(2).c_str()); c.w = atoi(Arg(3).c_str()); c.h = atoi(Arg(4).c_str()); c.opacity = g_cfg.opacity;
        if (Arg(4).empty()) { g_cmdResult = "error chat usage: move <x> <y> <w> <h>"; return; }
        g_cfg = chatwire::FitRect(c, g_vw > 0 ? g_vw : 1280, g_vh > 0 ? g_vh : 720);
        g_layoutDirty = true; g_linesDirty = true;
        if (Find(gui, kRoot) != 0) Layout(gui);
        CfgSave();
        g_cmdResult = "ok chat move " + PlaceWords(g_cfg);
        return;
    }
    if (verb == "esc")
    {
        /* the engine's own pause-menu show while the window is open: detour_pauseMenuShow must refuse it */
        const bool r = UiMenuCommand("ingame");
        g_cmdResult = std::string("ok chat esc show-said=") + (r ? "1" : "0") + " menu-visible=" + N(UiPauseMenuVisible());
        return;
    }
    if (verb == "close") { Close(gui, "lever", true); g_cmdResult = "ok chat close"; return; }
    g_cmdResult = "error chat usage: open|send|fill|tolist|tab|click|opacity|move|esc|close|state";
}
__declspec(noinline) void CmdCatching() { try { CmdInner(); } catch (...) { g_cmdResult = "error chat threw"; } }
/* a C++ throw out of the frame work: counted (ChatTick turns the chat off at the third), one log line at most every 5 s */
__declspec(noinline) void TickCatching()
{
    try { TickInner(); }
    catch (...)
    {
        ++g_softStrikes;
        const long long now = NowMs();
        if (g_throwLogMs == 0 || now - g_throwLogMs > 5000)
        {
            g_throwLogMs = now;
            DebugLog("[CHAT] a C++ exception in the chat's frame work (" + N(g_softStrikes) + " so far; the third turns the chat off)");
        }
    }
}

int Filter(unsigned long code) { ::InterlockedExchange(&g_lastCode, (LONG)code); return EXCEPTION_EXECUTE_HANDLER; }
int IsMemoryFault(unsigned long c) { return (c == 0xC0000005ul || c == 0xC0000006ul || c == 0xC00000FDul || c == 0x80000001ul || c == 0x80000002ul) ? 1 : 0; }
/* the SEH frames: no local object with a destructor in any of them */
int TickGuarded()
{
    __try { TickCatching(); }
    __except (Filter(GetExceptionCode())) { return 1; }
    return 0;
}
int CmdGuarded()
{
    __try { CmdCatching(); }
    __except (Filter(GetExceptionCode())) { return 1; }
    return 0;
}
const char* g_downWhy = "";
__declspec(noinline) void TakeDownInner() { TakeDown(MyGUI::Gui::getInstancePtr(), g_downWhy); }
__declspec(noinline) void TakeDownCatching() { try { TakeDownInner(); } catch (...) {} }
int TakeDownGuarded()
{
    __try { TakeDownCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return 0;
}
void Latch(const char* where)
{
    const unsigned long code = (unsigned long)(LONG)g_lastCode;
    const int mem = IsMemoryFault(code);
    if (!mem) ++g_softStrikes;
    const int off = (mem || g_softStrikes >= 3) ? 1 : 0;
    char b[300];
    _snprintf(b, 299, "[CHAT] a MyGUI call faulted in %s (code 0x%08lX, %s) - %s", where, code,
              mem ? "a memory fault" : "a C++ throw or other", off ? "the chat is OFF for the rest of this process" : "retried on the next frame");
    b[299] = 0;
    ErrorLog(b);
    if (!off) return;
    ::InterlockedExchange(&g_off, 1);
    ::InterlockedExchange(&g_uiUp, 0);
    g_downWhy = "the chat was turned off after a fault";
    TakeDownGuarded();
}
/* the third C++ throw out of the frame work: the chat goes for the rest of the process, as after a memory fault */
void LatchThrows()
{
    ErrorLog("[CHAT] a C++ exception in the chat's frame work for the third time - the chat is OFF for the rest of this process");
    ::InterlockedExchange(&g_off, 1);
    ::InterlockedExchange(&g_uiUp, 0);
    g_downWhy = "the chat was turned off after a fault";
    TakeDownGuarded();
}

}   // namespace

void ChatTick()
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    if (TickGuarded() != 0) { Latch("a world"); return; }
    if (g_softStrikes >= 3) LatchThrows();
}

void ChatTitleTick()
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    if (g_st == kStNone && g_ring.lines.empty()) return;
    g_downWhy = "the world was left";
    if (TakeDownGuarded() != 0) Latch("the title screen");
}

int ChatTakesEscape()
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return 0;
    if (::InterlockedCompareExchange(&g_uiUp, 0, 0) == 0)
    {
        if (!EscHoldOn()) return 0;   /* the ESC that just closed the window: the menu stays shut for it too */
        ::InterlockedIncrement(&g_escTook);
        return 1;
    }
    ::InterlockedExchange(&g_esc, 1);
    ::InterlockedIncrement(&g_escTook);
    return 1;
}

void ChatLiveIn(int originSlot, const void* p, unsigned n)
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    try
    {
        const long long now = NowMs();
        int kind = 0, target = -1;
        std::string text;
        if (!chatwire::Decode(p, (size_t)n, &kind, &target, &text)) { InDropped(&g_dropBad, "malformed", originSlot, now); return; }
        const int me = StoreMySlot();
        if (originSlot == me) return;
        const int verdict = chatwire::AcceptIn(kind, target, me, kind == chatwire::kFaction && TeamSameAnyThread(me, originSlot));
        if (verdict == chatwire::kInNotMate) { InDropped(&g_dropNotMate, "a faction line from outside this faction", originSlot, now); return; }
        if (verdict != chatwire::kInTake) { InDropped(&g_dropNotMine, "a private line for another player", originSlot, now); return; }
        if (!chatwire::RateTake(&g_inRate[originSlot], now)) { InDropped(&g_dropFast, "more lines a second than are kept", originSlot, now); return; }
        chatwire::Entry e;
        e.kind = kind; e.dir = chatwire::kIn; e.slot = originSlot; e.text = text;   /* the name is found by the tick */
        Push(e, now);
        DebugLog(std::string("[CHAT] in ") + KindLog(kind) + " from slot " + N(originSlot) + " chars=" + N((long long)chatwire::Utf8Chars(text)));
    }
    catch (...) { DebugLog("[CHAT] a C++ exception reading a chat message; it is dropped"); }
}

void ChatOpenTo(int slot)
{
    ::InterlockedExchange(&g_openSlot, (LONG)slot);
    ::InterlockedExchange(&g_openReq, 1);
}

std::string ChatCommand(const std::string& args)
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return "error chat off";
    g_cmdArgs.clear();
    size_t i = 0;
    while (i < args.size())
    {
        while (i < args.size() && args[i] == ' ') ++i;
        size_t j = i;
        while (j < args.size() && args[j] != ' ') ++j;
        if (j > i) g_cmdArgs.push_back(args.substr(i, j - i));
        i = j;
    }
    g_cmdResult.clear();
    if (CmdGuarded() != 0) { Latch("the chat lever"); return "error chat faulted"; }
    return g_cmdResult;
}

}   // namespace coop
