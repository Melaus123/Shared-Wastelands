// teammap.cpp - T-930 (owner decision 1436, approved mock-up build/mockups-t930.md): TEAMMATES' CHARACTERS ON THE WORLD MAP.
//
// WHAT IT DOES. While this player's world map is open, each character of each online teammate is a dot on it: the game's own
// "Character" map dot (resource Kenshi_MapMarkers, group and name "Character" - the dot the game draws for the player's own
// characters, the same size), in the name tags' team blue #5a9be6 (nametag.h TeamColour; owner rule 2026-10-19: a teammate
// shows team blue, green is a friendly non-teammate; the player's own dots stay the game's light blue). Hovering a dot shows
// the character's name and "<player name>'s squad" - the player's name from the sender's slot on this game (owner decision
// 1438: the squad's own name is not read or sent). A dead character has no dot; a teammate who goes offline loses every dot.
//
// HOW THE MAP IS REACHED (Read, scratch decompiles of 1.0.65; every function below matched unchanged in 1.0.68 by
// tools/gen_table_1068.py's matcher):
//   0x6E98D0 (the game's GUI frame, called by the main loop 0x787E70 every frame unless the loop's +0x608 is set) calls
//   0x49AC50 - the world map window's frame - with the window as `this`, every frame, whether the window shows or not:
//     window+0x8   the window's own widget - the window shows when it is visible
//     window+0xA8  the MapScreen; MapScreen+0x10 is its page widget. 0x49AC50 runs the map's own frame (0x49A200) exactly
//                  when both widgets are visible - that is "the map is open" here, read every frame from the game's own test.
//   MapScreen+0x78 the widget the game builds its character dots under (0x492380), +0x20 the map image whose size is the
//   canvas, and +0x194 / +0x198 (world origin x / z) and +0x1A4 / +0x1A8 (world span x / z): the game's own sums place a dot
//   at (world - origin) / span x canvas size (0x493040), less half the dot's size (0x48F6C0). The dot's size is the image
//   set's size for "Character" times two game globals (rows MapMarkerScaleX / MapMarkerScaleY, 1.0.65 0x213278C / 0x2132790).
// This file hooks 0x49AC50 (row MapWindow_frame), lets the game run it, then reads that same state: an open / close is the
// moment the game's own test changes (events, not a timer), and the dots are re-placed every frame the map is open (the
// canvas size moves with the zoom). The game's own dots take no mouse; ours do not either, so the map pans and zooms under
// them exactly as before - the hover is a pointer-in-rectangle test, as the mod's other hover notes (ui.cpp UiPointerOver).
//
// THE DATA. A teammate far away has no copy on this game, so the dots do not come from copies: while this player's map is
// open this game sends OPEN to each online teammate's game by the world server's LIVE relay (route SLOT); each of those games
// sends back the positions of its own player's living characters - all once, then each one again only when it moved more than
// a few metres (checked about twice a second) - and stops at CLOSE. The road and the bytes: src/common/mapdots.h.
// The sender's list is the engine's own list of the player's characters (PlayerInterface+0x2B0, the list the map draws the
// player's own dots from, 0x4983B0; count +0x2B8, array +0x2C0 - 0x7F2070), not a sweep of the world.
#include "coop_log.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_ResourceImageSet.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_LayerManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "teammap.h"
#include "hooks.h"                         /* AddHook */
#include "addresses.h"                     /* AddrReg, AddrAbs */
#include "store.h"                         /* StoreSendLive, StoreLiveReady, StoreLiveGen, StoreMySlot, StoreRosterSlotInWorld */
#include "team.h"                          /* TeamMatesOfMine, TeamSameAnyThread */
#include "spawn.h"                         /* LiveCharacter, FindSpawnedUid, SafeReadPosition, NameOf */
#include "net/session.h"                   /* IsUidMine */
#include "game/Character.h"
#include "../common/mapdots.h"
#include "../common/nametag.h"             /* TeamColour - the dot's colour */
#include "../common/liverelay.h"           /* kInnerMapDots */
#include "../common/liveenvelope.h"        /* kRouteSlot */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace coop {
namespace {

/* ---- the rows ---- */
unsigned long long kTmFrameRva = 0;  AddrReg kTmFrameRva_reg("MapWindow_frame", &kTmFrameRva);       /* Steam_1.0.65 0x49AC50, hooked */
unsigned long long kTmScaleXRva = 0; AddrReg kTmScaleXRva_reg("MapMarkerScaleX", &kTmScaleXRva);     /* Steam_1.0.65 0x213278C (var, float) */
unsigned long long kTmScaleYRva = 0; AddrReg kTmScaleYRva_reg("MapMarkerScaleY", &kTmScaleYRva);     /* Steam_1.0.65 0x2132790 (var, float) */
unsigned long long kTmPiRva = 0;     AddrReg kTmPiRva_reg("PlayerInterfaceGlobal", &kTmPiRva);       /* Steam_1.0.65 0x2133630 (var): PlayerInterface* */

/* ---- the engine's layout (Read; header comment) ---- */
const size_t kWinWidget = 0x8, kWinMapScreen = 0xA8;
const size_t kMsPage = 0x10, kMsImage = 0x20, kMsDotParent = 0x78, kMsOriginX = 0x194, kMsOriginZ = 0x198, kMsSpanX = 0x1A4, kMsSpanZ = 0x1A8;
const size_t kPiCharCount = 0x2B8, kPiCharArray = 0x2C0;   /* PlayerInterface+0x2B0: the player's own characters (lektor) */
const unsigned kPiCharMax = 1024;
const int kHoverDx = 18, kHoverDy = 22, kHoverPadX = 10, kHoverPadY = 6;   /* as the mod's other hover notes (ui.cpp) */

typedef void (*MapFrameFn)(void* self);
MapFrameFn g_frameOrig = 0;
int g_hook = 0;                 /* 1 hooked, -1 failed, -2 no row */
int g_off = 0;                  /* 1 = a fault in this file's frame work: off for this process (logged once) */
bool g_offLogged = false;
long long g_hookSeq = 0;        /* +1 each time the latched (or, unlatched, any) map window's frame ran (MAIN THREAD: the GUI frame runs inside the main loop) */
long long g_hookSeqSeen = 0;    /* its value at the last main-loop tick */

std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return std::string(b); }

/* ---- the counters (one [TEAMMAP] line at each open / close) ---- */
long long g_opensSent = 0, g_closesSent = 0, g_dotsIn = 0, g_dotsRefused = 0, g_malformed = 0, g_dotsSent = 0, g_sendFailed = 0, g_widgetFail = 0;

/* ================= VIEWER: this player's map ================= */
struct MapView
{
    MyGUI::Widget* windowW; void* screen; MyGUI::Widget* page; MyGUI::Widget* image; MyGUI::Widget* parent;
    float ox, oz, sx, sz;
};
struct Dot
{
    int slot; unsigned uid; float x, z; std::string name; MyGUI::Widget* w; std::string wname;
    Dot() : slot(-1), uid(0), x(0.0f), z(0.0f), w(0) {}
};
/* a dot's widget waiting to be destroyed: the network path (TeamMapLiveIn) never touches a widget - the store pump runs before
   the game's frame, before the holder is checked - so it queues the widget here and the frame destroys it after the check */
struct Doomed { MyGUI::Widget* w; std::string wname; };
bool g_open = false;
std::set<int> g_asked;                         /* the slots sent OPEN since the map opened (and not CLOSE) */
long g_askedLinkGen = 0;                       /* the world-server link those OPENs went on */
std::map<unsigned long long, Dot> g_dots;      /* key: slot << 32 | uid */
std::vector<Doomed> g_doomed;                  /* widgets of dots the network path removed, destroyed by the next frame */
MyGUI::Widget* g_parent = 0;                   /* the widget the dots were made under */
std::set<MyGUI::Widget*> g_kids;               /* that holder's direct children, read this frame (review fold 1) */
MyGUI::Widget* g_tip = 0;                      /* the hover note (a root widget) */
unsigned long long g_tipKey = 0;
bool g_tipStale = false;                       /* the network path changed or removed the hovered dot: the frame drops the note */
DWORD g_viewerCheckAt = 0;
long long g_dotSeq = 0;
void* g_window = 0;                            /* the map window object, latched on a frame its map reads open; 0 after a close (review fold 11, re-check fold 2) */
long long g_otherWindow = 0;                   /* frames of another window object, ignored */
std::map<int, DWORD> g_closeReplyAt;           /* when a refused DOTS from a player was last answered with CLOSE (review fold 6) */
long long g_closeReplies = 0, g_widgetGone = 0, g_dotsCapped = 0;
const char* const kTipName = "SwTeamMapTip";

unsigned long long DotKey(int slot, unsigned uid) { return ((unsigned long long)(unsigned)slot << 32) | uid; }

/* the window's and the map's state, read the way 0x49AC50 reads it. 1 read, 0 a null on the way, -1 the read faulted */
int MapReadPod(void* window, MapView* v)
{
    __try
    {
        v->windowW = *(MyGUI::Widget**)((char*)window + kWinWidget);
        v->screen = *(void**)((char*)window + kWinMapScreen);
        if (v->windowW == 0 || v->screen == 0) return 0;
        const char* s = (const char*)v->screen;
        v->page = *(MyGUI::Widget* const*)(s + kMsPage);
        v->image = *(MyGUI::Widget* const*)(s + kMsImage);
        v->parent = *(MyGUI::Widget* const*)(s + kMsDotParent);
        v->ox = *(const float*)(s + kMsOriginX); v->oz = *(const float*)(s + kMsOriginZ);
        v->sx = *(const float*)(s + kMsSpanX);   v->sz = *(const float*)(s + kMsSpanZ);
        return (v->page != 0 && v->image != 0 && v->parent != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* the game's own "the map is open" test (0x49AC50): the window and the map page both visible. 1 / 0, -1 faulted */
int MapOpenPod(MyGUI::Widget* windowW, MyGUI::Widget* page)
{
    __try { return (windowW->getVisible() && page->getVisible()) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
float ScalePod(unsigned long long rva)
{
    if (rva == 0) return 1.0f;
    __try { return *(const float*)AddrAbs(rva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1.0f; }
}

/* true = handed to the world-server link */
bool SendSignal(int slot, int kind)
{
    std::vector<char> b;
    if (!mapdots::EncodeSignal(kind, &b)) return false;
    if (StoreSendLive(cooplive::kRouteSlot, (unsigned)slot, cooplive::kInnerMapDots, b, true))
    { if (kind == mapdots::kOpen) ++g_opensSent; else ++g_closesSent; return true; }
    ++g_sendFailed;
    return false;
}

/* the dot holder's direct children, read now - the only widgets of ours under it that still exist (review fold 1: the game
   may clear the holder when it redraws its own dots) */
void ReadKids(MyGUI::Widget* parent)
{
    g_kids.clear();
    if (parent == 0) return;
    const size_t n = parent->getChildCount();
    for (size_t i = 0; i < n; ++i) g_kids.insert(parent->getChildAt(i));
}
/* one of our widgets destroyed. kDropChecked: only when it is one of the holder's children read this frame AND carries its own
   unique name (a recycled address - the game's own dot made where ours was - is never ours; re-check fold 1); else it is already
   gone, and only forgotten. kDropByName: only when MyGUI still finds it by its own name. */
void DestroyWidget(MyGUI::Widget* w, const std::string& wname, int mode)
{
    if (w == 0) return;
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    if (mode == mapdots::kDropChecked)
    {
        if (g_kids.count(w) != 0 && w->getName() == wname) { gui->destroyWidget(w); g_kids.erase(w); }   /* getName: w is a live child, read now */
        else ++g_widgetGone;
        return;
    }
    MyGUI::Widget* f = gui->findWidgetT(wname, false);
    if (f == w) gui->destroyWidget(f); else ++g_widgetGone;
}
/* the hover note, a root widget of ours: destroyed by pointer only when MyGUI still finds it by its name */
void DropTip()
{
    g_tipStale = false;
    if (g_tip == 0) return;
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui != 0) { MyGUI::Widget* f = gui->findWidgetT(std::string(kTipName), false); if (f == g_tip) gui->destroyWidget(f); }
    g_tip = 0; g_tipKey = 0;
}
/* the queued widgets of removed dots, destroyed in `mode` */
void DrainDoomed(int mode)
{
    for (size_t i = 0; i < g_doomed.size(); ++i) DestroyWidget(g_doomed[i].w, g_doomed[i].wname, mode);
    g_doomed.clear();
}
/* the NETWORK-PATH removal of one dot: the entry goes now, its widget waits for the frame */
void ForgetDot(std::map<unsigned long long, Dot>::iterator it)
{
    if (g_tipKey == it->first) g_tipStale = true;
    if (it->second.w != 0) { Doomed d; d.w = it->second.w; d.wname = it->second.wname; g_doomed.push_back(d); }
    g_dots.erase(it);
}
void ForgetSlotDots(int slot)
{
    for (std::map<unsigned long long, Dot>::iterator it = g_dots.begin(); it != g_dots.end();)
    {
        if (it->second.slot != slot) { ++it; continue; }
        ForgetDot(it++);
    }
}
/* every dot's widget and the note destroyed in `mode`, the entries cleared */
void DropAllDots(int mode)
{
    DropTip();
    DrainDoomed(mode);
    for (std::map<unsigned long long, Dot>::iterator it = g_dots.begin(); it != g_dots.end(); ++it) DestroyWidget(it->second.w, it->second.wname, mode);
    g_dots.clear();
}

/* OPEN to every online teammate not asked yet on this link (all of them again on a new link); a slot counts as asked only when
   its OPEN went out (review fold 5) - else it is tried again at the next check */
void AskMates()
{
    const long gen = StoreLiveGen();
    if (gen != g_askedLinkGen) { g_asked.clear(); g_askedLinkGen = gen; }
    if (!StoreLiveReady()) return;
    const int me = StoreMySlot();
    const std::vector<int> mates = TeamMatesOfMine();
    for (size_t i = 0; i < mates.size(); ++i)
    {
        const int s = mates[i];
        if (g_asked.count(s) != 0 || !mapdots::TeammateOnline(s, me, TeamSameAnyThread(me, s), StoreRosterSlotInWorld(s))) continue;
        if (SendSignal(s, mapdots::kOpen)) g_asked.insert(s);
    }
}
/* a teammate who went offline or left the team: its dots go, it is no longer asked (called from the frame) */
void KeepAsked()
{
    const int me = StoreMySlot();
    std::vector<int> drop;
    for (std::set<int>::const_iterator it = g_asked.begin(); it != g_asked.end(); ++it)
        if (!mapdots::TeammateOnline(*it, me, TeamSameAnyThread(me, *it), StoreRosterSlotInWorld(*it))) drop.push_back(*it);
    for (size_t i = 0; i < drop.size(); ++i)
    {
        if (StoreRosterSlotInWorld(drop[i]) == 1) SendSignal(drop[i], mapdots::kClose);   /* still here, no longer a teammate */
        g_asked.erase(drop[i]);
        ForgetSlotDots(drop[i]);
    }
    std::set<int> haveDots;
    for (std::map<unsigned long long, Dot>::const_iterator it = g_dots.begin(); it != g_dots.end(); ++it) haveDots.insert(it->second.slot);
    for (std::set<int>::const_iterator it = haveDots.begin(); it != haveDots.end(); ++it) if (g_asked.count(*it) == 0) ForgetSlotDots(*it);
}

void MapOpened()
{
    g_open = true;
    g_asked.clear();
    g_askedLinkGen = StoreLiveGen();
    AskMates();
    g_viewerCheckAt = ::GetTickCount();
    DebugLog("[TEAMMAP] the world map opened - OPEN sent to " + N((long long)g_asked.size()) + " online teammate(s) (opensSent "
             + N(g_opensSent) + ", sendFailed " + N(g_sendFailed) + ")");
}
/* CLOSE to every asked teammate, then the dots go in `mode` (mapdots::CloseDropMode: checked against the holder's children read
   now when the holder is proven alive, else each looked up by its name first) */
void MapClosed(const char* why, int mode)
{
    for (std::set<int>::const_iterator it = g_asked.begin(); it != g_asked.end(); ++it) SendSignal(*it, mapdots::kClose);
    const size_t dots = g_dots.size();
    g_asked.clear();
    DropAllDots(mode);
    g_open = false;
    g_parent = 0;
    g_kids.clear();
    g_window = 0;   /* re-check fold 2: the next open latches the window that shows it */
    DebugLog(std::string("[TEAMMAP] the world map closed (") + why + ") - CLOSE sent, " + N((long long)dots) + " dot(s) removed (dotsIn "
             + N(g_dotsIn) + ", refused " + N(g_dotsRefused) + ", malformed " + N(g_malformed) + ", widgetFail " + N(g_widgetFail)
             + ", widgetGone " + N(g_widgetGone) + ", capped " + N(g_dotsCapped) + ", closeReplies " + N(g_closeReplies)
             + ", otherWindow " + N(g_otherWindow) + ")");
}

/* one dot's widget, made as the game makes its own (0x492380): an ImageBox with the game's "ImageBox" skin (its one
   drawing part; with no skin the widget has none and is placed but never drawn - T1951) under the map's dot parent, the image set
   Kenshi_MapMarkers / "Character", the set's size times the map's marker scale, taking no mouse; coloured team blue */
MyGUI::Widget* MakeDotWidget(MyGUI::Widget* parent, const std::string& wname)
{
    MyGUI::Widget* w = parent->createWidgetT(std::string("ImageBox"), std::string("ImageBox"), MyGUI::IntCoord(0, 0, 32, 32), MyGUI::Align::Default, wname);
    MyGUI::ImageBox* ib = w != 0 ? w->castType<MyGUI::ImageBox>(false) : 0;
    if (ib == 0) { if (w != 0) MyGUI::Gui::getInstancePtr()->destroyWidget(w); return 0; }
    const std::string ch("Character");
    ib->setItemResource(std::string("Kenshi_MapMarkers"));
    ib->setItemGroup(ch);
    ib->setItemName(ch);
    int iw = 0, ih = 0;
    MyGUI::ResourceImageSet* rs = ib->getItemResource();
    if (rs != 0) { MyGUI::ImageIndexInfo info = rs->getIndexInfo(ch, ch); iw = info.size.width; ih = info.size.height; }
    ib->setSize(MyGUI::IntSize(mapdots::DotSide(iw, ScalePod(kTmScaleXRva)), mapdots::DotSide(ih, ScalePod(kTmScaleYRva))));
    ib->setNeedMouseFocus(false);
    const nametag::Rgb c = nametag::TeamColour();
    ib->setColour(MyGUI::Colour(c.r, c.g, c.b, 1.0f));
    return w;
}

/* every dot made if missing and put where the game's sums put it. A dot whose widget is no longer one of the holder's children
   (read this frame), or is a child at that address with another name (the game's own widget made where ours was - re-check
   fold 1), was destroyed with the holder's other children: forgotten and made again (review fold 1) */
void PlaceDots(const MapView& v)
{
    const int cw = v.image->getWidth(), ch = v.image->getHeight();
    for (std::map<unsigned long long, Dot>::iterator it = g_dots.begin(); it != g_dots.end(); ++it)
    {
        Dot& d = it->second;
        if (d.w != 0 && (g_kids.count(d.w) == 0 || d.w->getName() != d.wname)) { d.w = 0; ++g_widgetGone; if (g_tipKey == it->first) g_tipStale = true; }
        if (d.w == 0)
        {
            d.wname = "SwTeamMapDot" + N(++g_dotSeq);
            d.w = MakeDotWidget(v.parent, d.wname);
            if (d.w == 0) { ++g_widgetFail; continue; }
            g_kids.insert(d.w);
        }
        int px = 0, py = 0;
        if (!mapdots::CanvasPixel(d.x, d.z, v.ox, v.oz, v.sx, v.sz, cw, ch, &px, &py)) { d.w->setVisible(false); continue; }
        d.w->setPosition(MyGUI::IntPoint(px - d.w->getWidth() / 2, py - d.w->getHeight() / 2));
        d.w->setVisible(true);
    }
}

bool InBranch(MyGUI::Widget* w, MyGUI::Widget* root)
{
    for (MyGUI::Widget* p = w; p != 0; p = p->getParent()) if (p == root) return true;
    return false;
}
/* the hover note: a thin panel with the two lines, below-right of the pointer, made when the hover starts and gone when it
   ends (the mod's hover-note look, ui.cpp UiHoverTipFill: Kenshi_FloatingPanelThinSkin, one Kenshi_GenericTextBoxFlat
   box in the note's text colour, centred, sized to the measured text) */
MyGUI::Widget* MakeTip(const std::string& text)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    static const char* const kLayers[2] = { "Info", "Popup" };
    MyGUI::Widget* tip = 0;
    for (int k = 0; k < 2 && tip == 0; ++k)
    {
        tip = gui->createWidgetT(std::string("Widget"), std::string("Kenshi_FloatingPanelThinSkin"), MyGUI::IntCoord(0, 0, 400, 80),
                                 MyGUI::Align::Default, std::string(kLayers[k]), std::string(kTipName));
        if (tip != 0 && tip->getLayer() == 0) { gui->destroyWidget(tip); tip = 0; }
    }
    if (tip == 0) return 0;
    tip->setNeedMouseFocus(false);
    MyGUI::Widget* tw = tip->createWidgetT(std::string("TextBox"), std::string("Kenshi_GenericTextBoxFlat"),
                                           MyGUI::IntCoord(kHoverPadX, kHoverPadY, 360, 60), MyGUI::Align::Default, std::string("SwTeamMapTipText"));
    MyGUI::TextBox* tb = tw != 0 ? tw->castType<MyGUI::TextBox>(false) : 0;
    if (tb == 0) { gui->destroyWidget(tip); return 0; }
    tw->setNeedMouseFocus(false);
    tb->setTextAlign(MyGUI::Align::Center);
    tb->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    tb->setCaption(MyGUI::UString(text));
    const MyGUI::IntSize ts = tb->getTextSize();
    if (ts.width <= 0 || ts.height <= 0) { gui->destroyWidget(tip); return 0; }
    const MyGUI::IntCoord tr = tb->getTextRegion();
    const int mw = (tr.width > 0 && tr.width <= tw->getWidth()) ? tw->getWidth() - tr.width : 0;
    const int mh = (tr.height > 0 && tr.height <= tw->getHeight()) ? tw->getHeight() - tr.height : 0;
    MyGUI::Widget* client = tip->getClientWidget();
    const int fw = client != 0 ? tip->getWidth() - client->getWidth() : 0;
    const int fh = client != 0 ? tip->getHeight() - client->getHeight() : 0;
    tip->setSize(MyGUI::IntSize(ts.width + mw + 2 * kHoverPadX + fw, ts.height + mh + 2 * kHoverPadY + fh));
    tw->setCoord(MyGUI::IntCoord(kHoverPadX, kHoverPadY, ts.width + mw, ts.height + mh));
    return tip;
}
void HoverTick(const MapView& v)
{
    if (g_tipStale) DropTip();
    if (g_tip != 0)
    {   /* the note still exists (review fold 1, the note likewise): else forgotten, never touched */
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (gui == 0 || gui->findWidgetT(std::string(kTipName), false) != g_tip) { g_tip = 0; g_tipKey = 0; ++g_widgetGone; }
    }
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
    unsigned long long over = 0;
    MyGUI::IntPoint m;
    if (im != 0 && lm != 0 && !im->isModalAny())
    {
        m = im->getMousePosition();
        MyGUI::Widget* top = lm->getWidgetFromPoint(m.left, m.top);
        if (top != 0 && InBranch(top, v.windowW))   /* nothing else on top of the map there */
            for (std::map<unsigned long long, Dot>::const_iterator it = g_dots.begin(); it != g_dots.end(); ++it)
            {
                const Dot& d = it->second;
                if (d.w == 0 || !d.w->getVisible()) continue;
                const MyGUI::IntCoord c = d.w->getAbsoluteCoord();
                if (m.left >= c.left && m.left < c.left + c.width && m.top >= c.top && m.top < c.top + c.height) over = it->first;   /* the last drawn wins */
            }
    }
    if (over == 0) { DropTip(); return; }
    if (over != g_tipKey)
    {
        DropTip();
        const Dot& d = g_dots[over];
        /* line 1 the character's name, line 2 "<player>'s squad" - the player's name as the faction screens say it (team.h); both
           made caption-safe in HoverText (review fold 9) */
        g_tip = MakeTip(mapdots::HoverText(d.name, TeamPlayerName(d.slot)));
        g_tipKey = g_tip != 0 ? over : 0;
    }
    if (g_tip == 0) return;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    const MyGUI::IntSize vs = rm != 0 ? rm->getViewSize() : MyGUI::IntSize(100000, 100000);
    int x = m.left + kHoverDx, y = m.top + kHoverDy;
    if (x + g_tip->getWidth() > vs.width) x = vs.width - g_tip->getWidth();   /* kept on the screen */
    if (y + g_tip->getHeight() > vs.height) y = m.top - g_tip->getHeight() - 4;
    g_tip->setPosition(MyGUI::IntPoint(x, y));
}

/* the map window's frame has just run (the game's own work done) */
void FrameWork(void* window)
{
    /* review fold 11 / re-check fold 2: one window object - latched on a frame where its map reads open, let go at the close;
       while latched any other is ignored, and only the latched one's frames count for the "frame stopped" check */
    if (g_window != 0 && window != g_window) { ++g_otherWindow; return; }
    ++g_hookSeq;
    MapView v;
    std::memset(&v, 0, sizeof(v));
    const int r = MapReadPod(window, &v);
    const bool open = r == 1 && MapOpenPod(v.windowW, v.page) == 1;
    const bool sameHolder = r == 1 && v.parent == g_parent;
    if (g_open && (!open || !sameHolder))
    {   /* review fold 2: the dots are destroyed through their pointers only under the holder proven alive this frame */
        const int mode = mapdots::CloseDropMode(r, sameHolder);
        if (mode == mapdots::kDropChecked) ReadKids(v.parent);
        MapClosed(open ? "the map screen was rebuilt" : "closed", mode);
    }
    if (!open) return;
    g_window = window;
    if (!g_open) MapOpened();
    g_parent = v.parent;
    const DWORD now = ::GetTickCount();
    if (now - g_viewerCheckAt >= (DWORD)mapdots::kCheckMs) { g_viewerCheckAt = now; KeepAsked(); AskMates(); }
    ReadKids(v.parent);
    DrainDoomed(mapdots::kDropChecked);   /* review fold 3: what the network path removed, destroyed now, after the holder check */
    PlaceDots(v);
    HoverTick(v);
}

void FrameDetour(void* window)
{
    g_frameOrig(window);
    if (g_off != 0) return;
    __try { FrameWork(window); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_off = 1; }
}

/* review fold 4: after a fault, the dots and the note are taken off the map by their names, guarded - a second fault leaves
   them (they are forgotten either way) */
void DropAllByName() { DropAllDots(mapdots::kDropByName); }
int DropAllByNamePod()
{
    __try { DropAllByName(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* ================= SENDER: this player's characters to the teammates whose map is open ================= */
struct Viewer { bool whole; std::map<unsigned, mapdots::SentRow> sent; Viewer() : whole(true) {} };
std::map<int, Viewer> g_viewers;
DWORD g_sendCheckAt = 0;
/* T-930 (T1925 fix 2): OPENs refused because the player was not a teammate here yet - slot -> the link (StoreLiveGen) it came on;
   stepped every main-loop tick (PendingTick, mapdots::PendingOpenStep), dropped at that player's CLOSE */
std::map<int, long> g_pendingOpen;
std::map<int, DWORD> g_pendingAt;
long long g_pendingKept = 0, g_pendingAccepted = 0, g_pendingDropped = 0;

/* the engine's list of the player's characters: count and array (PlayerInterface+0x2B8 / +0x2C0). 1 read, 0 none, -1 faulted */
int PlayerListPod(unsigned* count, ::Character*** arr)
{
    if (kTmPiRva == 0) return 0;
    __try
    {
        const char* pi = *(const char* const*)AddrAbs(kTmPiRva);
        if (pi == 0) return 0;
        *count = *(const unsigned*)(pi + kPiCharCount);
        *arr = *(::Character** const*)(pi + kPiCharArray);
        return (*arr != 0 && *count <= kPiCharMax) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
::Character* PlayerListAtPod(::Character** arr, unsigned i)
{
    __try { return arr[i]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 dead, 0 alive, -1 the read faulted */
int DeadPod(::Character* c)
{
    __try { return c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void ReadOwn(std::vector<mapdots::OwnRow>* out)
{
    out->clear();
    unsigned count = 0; ::Character** arr = 0;
    if (PlayerListPod(&count, &arr) != 1) return;
    for (unsigned i = 0; i < count; ++i)
    {
        ::Character* c = LiveCharacter(PlayerListAtPod(arr, i));
        if (c == 0 || DeadPod(c) != 0) continue;   /* dead (or unreadable): no dot; knocked out keeps its dot */
        const unsigned uid = FindSpawnedUid(c);
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        Ogre::Vector3 p;
        if (!SafeReadPosition(c, &p)) continue;
        mapdots::OwnRow o; o.uid = uid; o.x = p.x; o.z = p.z;
        NameOf(c, &o.name);
        out->push_back(o);
    }
}

/* one DOTS to one viewer: what mapdots::SendPlan picks; recorded (SentApply) only when the send went out, so a failed send is
   planned again unchanged at the next check */
void SendTo(int slot, Viewer* vw, const std::vector<mapdots::OwnRow>& own)
{
    std::vector<mapdots::Row> rows;
    std::vector<unsigned> gone;
    if (!mapdots::SendPlan(vw->whole, own, vw->sent, &rows, &gone)) return;
    std::vector<char> b;
    if (!mapdots::EncodeDots(vw->whole, rows, gone, &b)) return;
    if (!StoreSendLive(cooplive::kRouteSlot, (unsigned)slot, cooplive::kInnerMapDots, b, true)) { ++g_sendFailed; return; }
    ++g_dotsSent;
    mapdots::SentApply(vw->whole, rows, gone, &vw->sent);
    vw->whole = false;
}

void SenderTick()
{
    if (g_viewers.empty()) return;
    const DWORD now = ::GetTickCount();
    if (now - g_sendCheckAt < (DWORD)mapdots::kCheckMs) return;
    g_sendCheckAt = now;
    const int me = StoreMySlot();
    for (std::map<int, Viewer>::iterator it = g_viewers.begin(); it != g_viewers.end();)
    {
        if (mapdots::TeammateOnline(it->first, me, TeamSameAnyThread(me, it->first), StoreRosterSlotInWorld(it->first))) ++it;
        else
        {
            DebugLog("[TEAMMAP] player " + N((long long)it->first) + " is no longer an online teammate - its map gets no more positions");
            g_viewers.erase(it++);
        }
    }
    if (g_viewers.empty() || !StoreLiveReady()) return;
    std::vector<mapdots::OwnRow> own;
    ReadOwn(&own);
    for (std::map<int, Viewer>::iterator it = g_viewers.begin(); it != g_viewers.end(); ++it) SendTo(it->first, &it->second, own);
}

/* an OPEN answered: all of this player's characters to that viewer at the next check */
void AcceptOpen(int origin)
{
    g_viewers[origin] = Viewer();
    g_sendCheckAt = ::GetTickCount() - (DWORD)mapdots::kCheckMs;
}
/* T-930 (T1925 fix 2): each kept OPEN stepped from the live facts - answered once this game's team table names the player an
   online teammate, dropped when this game's link changed or the roster says the player left the world. MAIN THREAD */
void PendingTick()
{
    if (g_pendingOpen.empty()) return;
    const int me = StoreMySlot();
    const long gen = StoreLiveGen();
    for (std::map<int, long>::iterator it = g_pendingOpen.begin(); it != g_pendingOpen.end();)
    {
        const int o = it->first;
        const int inWorld = StoreRosterSlotInWorld(o);
        const int step = mapdots::PendingOpenStep(o, me, TeamSameAnyThread(me, o), inWorld, it->second, gen);
        if (step == mapdots::kPendKeep) { ++it; continue; }
        const DWORD keptMs = ::GetTickCount() - g_pendingAt[o];
        const bool linkMoved = it->second != gen;
        g_pendingAt.erase(o);
        g_pendingOpen.erase(it++);
        if (step == mapdots::kPendAccept)
        {
            ++g_pendingAccepted;
            AcceptOpen(o);
            DebugLog("[TEAMMAP] player " + N((long long)o) + " opened the world map (its OPEN kept " + N((long long)keptMs)
                     + " ms until this game's team table named it an online teammate) - its game gets this player's characters' positions (pendingAccepted "
                     + N(g_pendingAccepted) + ")");
        }
        else
        {
            ++g_pendingDropped;
            DebugLog("[TEAMMAP] the kept OPEN from player " + N((long long)o) + " dropped after " + N((long long)keptMs) + " ms ("
                     + std::string(linkMoved ? "this game's link changed - it asks again on the new one" : "not in the world") + "; pendingDropped " + N(g_pendingDropped) + ")");
        }
    }
}

/* review fold 6: a DOTS this game did not ask for (its map closed, never asked, the work off) is answered with CLOSE - at most
   once per mapdots::kCloseReplyMs per player - so a sender whose CLOSE was lost stops */
void ReplyClose(int origin)
{
    if (origin < 0 || origin == StoreMySlot() || StoreRosterSlotInWorld(origin) != 1) return;
    const DWORD now = ::GetTickCount();
    std::map<int, DWORD>::const_iterator it = g_closeReplyAt.find(origin);
    if (!mapdots::CloseReplyDue(it != g_closeReplyAt.end(), it != g_closeReplyAt.end() ? (unsigned long)it->second : 0ul, (unsigned long)now)) return;
    g_closeReplyAt[origin] = now;
    if (SendSignal(origin, mapdots::kClose)) ++g_closeReplies;
}

}   /* namespace */

void InstallTeamMap()
{
    if (kTmFrameRva == 0) { g_hook = -2; ErrorLog("[TEAMMAP] MapWindow_frame has no address in the table - teammates are not shown on the world map"); return; }
    void* target = (void*)AddrAbs(kTmFrameRva);
    g_hook = (target != 0 && AddHook(target, (void*)&FrameDetour, (void**)&g_frameOrig) == SUCCESS) ? 1 : -1;
    DebugLog(g_hook == 1 ? "[TEAMMAP] the world map window's frame (MapWindow_frame) hooked - teammates' characters show on the map"
                         : "[TEAMMAP] MapWindow_frame AddHook FAILED - teammates are not shown on the world map");
}

void TeamMapTick()
{
    if (g_off != 0 && !g_offLogged)
    {   /* review fold 4: CLOSE to every asked teammate (they stop sending), the dots and the note taken off by name, guarded */
        g_offLogged = true;
        for (std::set<int>::const_iterator it = g_asked.begin(); it != g_asked.end(); ++it) SendSignal(*it, mapdots::kClose);
        const int dropped = DropAllByNamePod();
        ErrorLog(std::string("[TEAMMAP] the map work faulted once - teammates' dots are off for the rest of this run (the map itself is untouched); CLOSE sent to ")
                 + N((long long)g_asked.size()) + " teammate(s), the dots " + (dropped == 1 ? "removed" : "left (the removal faulted too)"));
        g_open = false; g_asked.clear(); g_dots.clear(); g_doomed.clear(); g_kids.clear(); g_tip = 0; g_tipKey = 0; g_parent = 0;   /* forgotten, never touched again */
    }
    /* the map's frame did not run since the last main-loop tick while the map was open: the map is not drawn any more (a load,
       the world going) - closed, each dot looked up by name before it is destroyed */
    const long long seq = g_hookSeq;
    if (g_off == 0 && g_open && seq == g_hookSeqSeen) MapClosed("the map's frame stopped", mapdots::kDropByName);
    g_hookSeqSeen = seq;
    PendingTick();   /* T1925 fix 2: a kept OPEN answered the tick this game's team table names its player a teammate */
    SenderTick();
}

/* MAIN THREAD, the store pump - before the game's frame: entries only; no widget is touched here (review fold 3) */
void TeamMapLiveIn(int originSlot, const void* p, unsigned n)
{
    mapdots::Msg m;
    if (!mapdots::Decode(p, (size_t)n, &m))
    {
        ++g_malformed;
        if (g_malformed <= 5) ErrorLog("[TEAMMAP] a MAP_DOTS of " + N((long long)n) + " bytes from player " + N((long long)originSlot) + " did not decode - dropped");
        return;
    }
    const int me = StoreMySlot();
    const bool same = TeamSameAnyThread(me, originSlot);
    const int inWorld = StoreRosterSlotInWorld(originSlot);
    if (m.kind == mapdots::kOpen)
    {
        if (!mapdots::TeammateOnline(originSlot, me, same, inWorld))
        {   /* T1925 fix 2: kept - answered when this game's team table names that player an online teammate (PendingTick) */
            ++g_dotsRefused;
            const long gen = StoreLiveGen();
            const int keep = mapdots::PendingOpenKeep(originSlot, me, gen, g_pendingOpen.count(originSlot) != 0 ? 0 : g_pendingOpen.size());
            if (keep != 0) { g_pendingOpen[originSlot] = gen; g_pendingAt[originSlot] = ::GetTickCount(); ++g_pendingKept; }
            DebugLog("[TEAMMAP] OPEN from player " + N((long long)originSlot) + " refused - not an online teammate (same team " + N(same ? 1 : 0)
                     + ", in world " + N((long long)inWorld) + ")" + (keep != 0 ? " - kept: answered once this game's team table names it an online teammate (pendingKept "
                     + N(g_pendingKept) + ")" : std::string(" - not kept")));
            return;
        }
        g_pendingOpen.erase(originSlot); g_pendingAt.erase(originSlot);
        AcceptOpen(originSlot);   /* all of this player's characters at the next check */
        DebugLog("[TEAMMAP] player " + N((long long)originSlot) + " opened the world map - its game gets this player's characters' positions");
        return;
    }
    if (m.kind == mapdots::kClose)
    {
        if (g_pendingOpen.erase(originSlot) != 0)
        {
            g_pendingAt.erase(originSlot); ++g_pendingDropped;
            DebugLog("[TEAMMAP] player " + N((long long)originSlot) + " closed the world map - its kept OPEN dropped (pendingDropped " + N(g_pendingDropped) + ")");
        }
        if (g_viewers.erase(originSlot) != 0) DebugLog("[TEAMMAP] player " + N((long long)originSlot) + " closed the world map - no more positions to it");
        return;
    }
    if (g_off != 0 || !mapdots::ViewerTakes(originSlot, me, same, inWorld, g_asked.count(originSlot) != 0, g_open))
    {
        ++g_dotsRefused;
        ReplyClose(originSlot);
        return;
    }
    ++g_dotsIn;
    std::vector<unsigned> have, drop;
    for (std::map<unsigned long long, Dot>::const_iterator it = g_dots.begin(); it != g_dots.end(); ++it) if (it->second.slot == originSlot) have.push_back(it->second.uid);
    mapdots::WholeDrops(have, m, &drop);
    for (size_t i = 0; i < drop.size(); ++i)
    {
        std::map<unsigned long long, Dot>::iterator it = g_dots.find(DotKey(originSlot, drop[i]));
        if (it != g_dots.end()) ForgetDot(it);
    }
    size_t count = have.size() - drop.size();
    for (size_t i = 0; i < m.rows.size(); ++i)
    {
        const mapdots::Row& r = m.rows[i];
        const unsigned long long key = DotKey(originSlot, r.uid);
        std::map<unsigned long long, Dot>::iterator it = g_dots.find(key);
        if (it == g_dots.end())
        {
            if (!mapdots::DotRoom(count)) { ++g_dotsCapped; continue; }   /* review fold 8: at most kMaxDotsPerSlot per teammate */
            it = g_dots.insert(std::make_pair(key, Dot())).first;
            ++count;
        }
        Dot& d = it->second;
        d.slot = originSlot; d.uid = r.uid; d.x = r.x; d.z = r.z;
        if (r.hasNames)
        {
            if (g_tipKey == key && d.name != r.name) g_tipStale = true;   /* the frame rebuilds the note with the new words */
            d.name = r.name;
        }
    }
    for (size_t i = 0; i < m.gone.size(); ++i)
    {
        std::map<unsigned long long, Dot>::iterator it = g_dots.find(DotKey(originSlot, m.gone[i]));
        if (it == g_dots.end()) continue;
        ForgetDot(it);
        if (count > 0) --count;
    }
}

void TeamMapWorldTeardown()
{
    if (g_off == 0)
    {
        if (g_open) MapClosed("world teardown", mapdots::kDropByName);
        DropTip();
    }
    g_window = 0;   /* the next world's map window is latched afresh */
    /* review fold 7: a teammate whose map is open keeps getting positions - its next DOTS is this player's whole list again */
    for (std::map<int, Viewer>::iterator it = g_viewers.begin(); it != g_viewers.end(); ++it) { it->second.whole = true; it->second.sent.clear(); }
}

}   /* namespace coop */
