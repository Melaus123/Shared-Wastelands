// ping.cpp - T-1393 (owner decisions 1725 / 1726 a / 1728, approved mock-up build/pages/ping-mockup.html): PINGS.
//
// WHAT THE PLAYER SEES. The Ping key (G until the player rebinds it: T-1437, the 'Ping' row at the end of Settings > Controls)
// pings the spot under the mouse: on the world map when the map is open, otherwise the ground in the
// world; ignored while typing in chat or any text box (the guard the name tags' Insert key obeys). The player's teammates
// (owner 1726 a) - never a hostile player (owner 1725) - and the player who pinged see a yellow diamond on a short stem with the
// pinging player's name above it, standing on the spot as the camera moves; off screen a yellow arrow at the screen edge points
// towards it, with the name. On the world map the same marker, slightly smaller, sits on the spot as the map pans and zooms; a
// world ping shows there too. It pulses gently for 6 s, then fades; a new ping from the same player replaces the old one. A
// chat line says "<name> pinged a spot." / "<name> pinged a spot on the map." with the chat message sound for a teammate's ping
// (chat.cpp ChatPingLine). At most 3 pings in 5 s per player: extra presses do nothing, with no message.
//
// HOW (design A, 05-findings F1121). The world marker and the edge arrow are root MyGUI widgets on the name tags' layer, placed
// every frame after the engine's loop through the name tags' own projection (tags.cpp TagsScreenSpot). The map marker is a child
// of the map's "MapOverlaysMin" overlay (MapScreen+0x60, the one full-canvas holder the zoom never hides - F1214: the character
// dots' holder +0x78 is hidden fully zoomed out, teammap.cpp kMsOverlayMin), placed on the map window's own frame (teammap.cpp
// calls PingMapFrame) with the map's own sums (mapdots::CanvasPixel), as the teammates' dots are; its size is fixed in screen
// pixels (kMapScale), the same at every zoom. No 3D scene object. A marker is drawn from flat stripes of the game's
// WhiteSkin (tinted), a stem and the name (pingwire::DiamondSpans / ArrowSpans).
// THE SPOT. In the world: the mouse's ray from Ogre's own Camera::getCameraToViewportRay (exported by OgreMain_x64.dll, as
// tags.cpp finds getRelativeOrigin) on the camera the engine draws with (RenderHolderPtr's object +0x58 - the camera 0x787E70
// reads its view matrix from), plus the scene's relative origin, then the nearer of the physics scene's first hit (combat.cpp
// GroundTraceAt, groundline's UtilityT::trace road) and the terrain's surface along the ray (pingwire::MarchGround on
// UtilityT::getTerrainHeightFast, combat.cpp GroundTerrainHeightAt) - the game's right-click asks the same two. No new address
// row. On the map: the mouse over the map's own widgets, its pixel on the canvas turned back into world x, z
// (pingwire::MapWorldFromPixel, the inverse of the map's own sum).
// THE ROAD. One SPOT_PING (cooplive::kInnerSpotPing 207, 17 bytes, pingwire.h) per online teammate, route SLOT, as faction chat
// goes; the receiver re-checks the team and hostility and the rate, and drops what fails (counted, one [PING] line).
//
// THREADS / SEH. Everything runs on the main thread. The network path (PingLiveIn) touches no widget: a replaced ping's widgets
// wait in g_doomed for the tick, which destroys each only when MyGUI still finds it by its own name. The tick and the map frame
// run their work behind their own SEH frame; a fault turns the pings off for the process (one line).
#include "coop_log.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_LayerManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ping.h"
#include "addresses.h"                     /* AddrAbs */
#include "store.h"                         /* StoreSendLive, StoreLiveReady, StoreMySlot, StoreRosterSlotInWorld, EngineWritesBlocked */
#include "team.h"                          /* TeamMatesOfMine, TeamSameAnyThread */
#include "tags.h"                          /* TagsScreenSpot, TagsSceneOrigin, TagsInterfaceShown */
#include "chat.h"                          /* ChatPingLine, ChatPlayerName */
#include "relations.h"                     /* RelationsMineTowards - the hostility re-check */
#include "soak.h"                          /* GameplayRunning */
#include "ui.h"                            /* UiPlayerTyping - the typing guard */
#include "../common/pingwire.h"
#include "../common/mapdots.h"             /* CanvasPixel - the map's own sum */
#include "../common/playerstab.h"          /* EscapeHash - a name made caption-safe */
#include "../common/liverelay.h"           /* kInnerSpotPing */
#include "../common/liveenvelope.h"        /* kRouteSlot */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>                        /* _ReturnAddress - the Controls list's spacer call is told by its return address */
#include "hooks.h"                         /* coop::AddHook - T-1437's two hooks */
#pragma intrinsic(_ReturnAddress)

namespace coop {

extern unsigned long long kRenderHolderPtrRva;   /* tags.cpp: row RenderHolderPtr (1.0.65 0x21322B8) - the object holding the camera (+0x58) */
int GroundTerrainHeightAt(float x, float z, float* out);              /* combat.cpp: 1 a height, 0 no terrain, -1 no row / a fault */
int GroundTraceAt(const float* origin, const float* dir, float* pt);  /* combat.cpp: 1 a hit, 0 none / refused now, -1 no row / a fault */

namespace {

/* ---- the look (mock-up colours: marker #f2c14e, its dark edge #1b1813, the name #f0d9a2) ---- */
const float kMark[3] = { 0xf2 / 255.0f, 0xc1 / 255.0f, 0x4e / 255.0f };
const float kEdge[3] = { 0x1b / 255.0f, 0x18 / 255.0f, 0x13 / 255.0f };
const float kName[3] = { 0xf0 / 255.0f, 0xd9 / 255.0f, 0xa2 / 255.0f };
const char* const kLayer = "Dialog";                         /* the name tags' layer (tags.cpp kLayer) */
const char* const kTextSkin = "Kenshi_GenericTextBoxFlat";   /* the mod's hover-note text (teammap.cpp MakeTip) */
const int kPool = 24;            /* stripes per shape, made once (edge stripes first so they draw behind) */
const int kRowH = 2;             /* each stripe is 2 px tall */
const float kHalf = 8.5f;        /* the diamond's yellow fill, half its diagonal, px: the page's 16 px square (its 2 px border inside it) turned 45 degrees is 11.3 px to the outer point = kHalf + kEdgePx (re-check fold) */
const float kEdgePx = 2.83f;     /* its 2 px dark border, measured along the diagonal */
const float kStemLen = 14.0f;    /* the short stem */
const float kMapScale = 0.75f;   /* "slightly smaller" on the map */
const float kArrowLen = 13.0f, kArrowHalf = 9.0f;   /* the page's arrow: 13 px long, 18 px across (review fold 10) */
const int kEdgeMargin = 14;      /* the arrow's tip this far inside the screen's edge */
const float kMarchMax = 30000.0f, kMarchStep = 25.0f;   /* the terrain walk: up to 3 km along the ray, a sample every 2.5 m */
const float kTraceOnRay = 30.0f; /* a physics hit counts only within 3 m of the ray's line (its space checked against the ray) */
const int kLogCap = 300;         /* [PING] lines per process; then one line says the rest are not logged */

/* Ogre::Camera::getCameraToViewportRay(Real x, Real y, Ray* out) const - `this` RCX, x XMM1, y XMM2, out R9 */
typedef void (*CamRayFn)(const void* camera, float x, float y, float* outRay);
const char kOgreDll[] = "OgreMain_x64.dll";
const char kCamRaySym[] = "?getCameraToViewportRay@Camera@Ogre@@QEBAXMMPEAVRay@2@@Z";
const size_t kHolderCameraOff = 0x58;   /* RenderHolderPtr's object: the camera (tags.cpp; 0x787E70 / 0x7FF6B0, Read) */

struct Marker
{
    MyGUI::Widget* root; std::string rname;
    MyGUI::Widget* edge[kPool]; MyGUI::Widget* fill[kPool];
    MyGUI::Widget* stemEdge; MyGUI::Widget* stem; MyGUI::TextBox* text;
    int tw, th;
    Marker() : root(0), stemEdge(0), stem(0), text(0), tw(0), th(0) { for (int i = 0; i < kPool; ++i) { edge[i] = 0; fill[i] = 0; } }
};
struct Entry
{
    pingwire::Msg m;
    DWORD at;
    std::string caption;   /* the name, caption-safe */
    bool own;
    Marker world, arrow, map;
    int whereLogged;       /* 0 not yet, 1 on screen, 2 edge, 3 hidden (the map open or the interface hidden) */
    bool mapLogged;
    Entry() : at(0), own(false), whereLogged(0), mapLogged(false) {}
};
struct Doomed { MyGUI::Widget* w; std::string name; };
struct MapNow
{
    MyGUI::Widget* windowW; MyGUI::Widget* page; MyGUI::Widget* image; MyGUI::Widget* parent;
    float ox, oz, sx, sz;
};

std::map<int, pingwire::Live> g_rule;  /* pingwire::Place's record of the live pings - the replace rule */
std::map<int, Entry> g_live;          /* one per player (slot; this game's own pings under its slot, -1 before it is known) */
std::vector<Doomed> g_doomed;
pingwire::Rate g_sendRate;
std::map<int, pingwire::Rate> g_recvRate;
unsigned g_count = 0;                 /* this player's pings so far (the message's count) */
long long g_seq = 0;                  /* widget names */
volatile unsigned char g_pingDown = 0;  /* T-1437: the Ping action's down byte - the game's key-down dispatch (0x360AD0) sets it at each press of the bound key, its release 0x360D40 clears it */
volatile unsigned char g_pingPressed = 0;  /* T-1437 fold 1: a press seen by our key-down hook (survives a release in the same frame); the tick consumes it */
bool g_keySaid = false;               /* the per-load [PING] line naming the bound key */
MapNow g_mapNow;
long long g_mapSeq = 0, g_mapSeqSeen = 0;
bool g_mapOpen = false;               /* the map's frame ran this frame (read by the tick) */
int g_off = 0;
bool g_offSaid = false;
int g_logs = 0;
long long g_sent = 0, g_sendCopies = 0, g_sendFailed = 0, g_sendCapped = 0, g_noSpot = 0, g_typing = 0, g_in = 0, g_inMalformed = 0,
          g_inCapped = 0, g_inDropped[pingwire::kVerdicts] = { 0, 0, 0, 0, 0, 0 }, g_noMate = 0, g_overWindow = 0, g_faded = 0, g_makeFail = 0, g_gone = 0, g_throws = 0, g_traceUsed = 0,
          g_marchUsed = 0;

std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return std::string(b); }
std::string F(float v) { char b[48]; _snprintf(b, 47, "%.1f", v); b[47] = 0; return std::string(b); }
void Log(const std::string& s)
{
    if (g_logs < kLogCap) { ++g_logs; DebugLog(s); return; }
    if (g_logs == kLogCap) { ++g_logs; DebugLog("[PING] capped - further [PING] lines are not logged this run (" + N(kLogCap) + ")"); }
}
std::string Who(int slot) { return (slot < 0 || slot == StoreMySlot()) ? std::string("this player") : "player " + N(slot); }   /* T2480: this game's own slot is "this player" in [PING] lines */

/* ================= widgets ================= */
/* review fold 8 - no widget-tree sweep per frame: a world marker or arrow is a root widget of ours that nothing but this file
   destroys (as the name tags keep theirs), so its pointer is kept until we destroy it; a map marker lives under the map's dot
   holder, which the game may clear, so it is checked against the holder's children read ONCE per map frame (g_kids) */
std::set<MyGUI::Widget*> g_kids;
bool MapAlive(const Marker& mk) { return mk.root != 0 && g_kids.count(mk.root) != 0 && mk.root->getName() == mk.rname; }
void ReadKids(MyGUI::Widget* parent)
{
    g_kids.clear();
    if (parent == 0) return;
    const size_t n = parent->getChildCount();
    for (size_t i = 0; i < n; ++i) g_kids.insert(parent->getChildAt(i));
}
/* the marker's widgets queued for the tick's destroy and forgotten (no widget touched here) */
void Doom(Marker& mk)
{
    if (mk.root != 0) { Doomed d; d.w = mk.root; d.name = mk.rname; g_doomed.push_back(d); }
    mk = Marker();
}
void DrainDoomed()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    for (size_t i = 0; i < g_doomed.size(); ++i)
    {
        MyGUI::Widget* f = gui != 0 ? gui->findWidgetT(g_doomed[i].name, false) : 0;
        if (f != 0 && f == g_doomed[i].w) gui->destroyWidget(f); else ++g_gone;
    }
    g_doomed.clear();
}
void DoomEntry(Entry& e) { Doom(e.world); Doom(e.arrow); Doom(e.map); }

MyGUI::Widget* Stripe(MyGUI::Widget* root, const float* c)
{
    MyGUI::Widget* w = root->createWidgetT(std::string("Widget"), std::string("WhiteSkin"), MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, std::string());
    if (w == 0) return 0;
    w->setNeedMouseFocus(false);
    w->setColour(MyGUI::Colour(c[0], c[1], c[2], 1.0f));
    w->setVisible(false);
    return w;
}
/* a marker's widgets: a root that draws nothing and takes no mouse (on the tags' layer, or under `parent`), the dark edge's
   stripes, the stem's edge, the yellow stripes, the stem, the name. false = MyGUI refused the root (counted). */
bool Make(Marker& mk, MyGUI::Widget* parent, const std::string& caption, bool withStem)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return false;
    mk = Marker();
    mk.rname = "SwPing" + N(++g_seq);
    mk.root = parent != 0
        ? parent->createWidgetT(std::string("Widget"), std::string("PanelEmpty"), MyGUI::IntCoord(0, 0, 10, 10), MyGUI::Align::Default, mk.rname)
        : gui->createWidgetT(std::string("Widget"), std::string("PanelEmpty"), MyGUI::IntCoord(0, 0, 10, 10), MyGUI::Align::Default, std::string(kLayer), mk.rname);
    if (mk.root == 0) { ++g_makeFail; mk = Marker(); return false; }
    mk.root->setNeedMouseFocus(false);
    mk.root->setVisible(false);
    for (int i = 0; i < kPool; ++i) mk.edge[i] = Stripe(mk.root, kEdge);
    if (withStem) mk.stemEdge = Stripe(mk.root, kEdge);
    for (int i = 0; i < kPool; ++i) mk.fill[i] = Stripe(mk.root, kMark);
    if (withStem) mk.stem = Stripe(mk.root, kMark);
    MyGUI::Widget* tw = mk.root->createWidgetT(std::string("TextBox"), std::string(kTextSkin), MyGUI::IntCoord(0, 0, 400, 40), MyGUI::Align::Default, mk.rname + "Name");
    mk.text = tw != 0 ? tw->castType<MyGUI::TextBox>(false) : 0;
    if (mk.text != 0)
    {
        tw->setNeedMouseFocus(false);
        mk.text->setTextAlign(MyGUI::Align::Center);
        mk.text->setTextColour(MyGUI::Colour(kName[0], kName[1], kName[2], 1.0f));
        mk.text->setTextShadow(true);
        mk.text->setTextShadowColour(MyGUI::Colour(0.0f, 0.0f, 0.0f, 1.0f));
        mk.text->setCaption(MyGUI::UString(caption));
        const MyGUI::IntSize ts = mk.text->getTextSize();
        mk.tw = ts.width + 4; mk.th = ts.height + 2;
        if (mk.tw < 4) mk.tw = 4;
        if (mk.th < 4) mk.th = 4;
        tw->setSize(MyGUI::IntSize(mk.tw, mk.th));
    }
    return true;
}
void SetStripes(MyGUI::Widget* const* pool, const std::vector<pingwire::Span>& sp)
{
    for (int i = 0; i < kPool; ++i)
    {
        if (pool[i] == 0) continue;
        if (i < (int)sp.size())
        {
            pool[i]->setCoord(MyGUI::IntCoord(sp[i].x0, sp[i].y, sp[i].x1 - sp[i].x0, kRowH));
            pool[i]->setVisible(true);
        }
        else pool[i]->setVisible(false);
    }
}
/* the diamond on its stem with the name above, sized by `scale` and the pulse; the root's size set; *ax, *ay = the spot's place
   in the root (its bottom centre) */
void LayoutMarker(Marker& mk, float scale, float gemAlpha, int* ax, int* ay)
{
    const float half = kHalf * scale, edgeHalf = half + kEdgePx * scale;
    const float stemLen = kStemLen * scale;
    int w = mk.tw + 4, dw = (int)(2.0f * edgeHalf) + 8;
    if (dw > w) w = dw;
    const int h = mk.th + 4 + (int)(2.0f * edgeHalf + 0.5f) + (int)(stemLen + 0.5f) + 2;
    mk.root->setSize(MyGUI::IntSize(w, h));
    const float cx = (float)(w / 2), groundY = (float)h;
    const float cy = groundY - stemLen - edgeHalf;
    std::vector<pingwire::Span> sp;
    pingwire::DiamondSpans(cx, cy, edgeHalf, kRowH, &sp); SetStripes(mk.edge, sp);
    pingwire::DiamondSpans(cx, cy, half, kRowH, &sp); SetStripes(mk.fill, sp);
    for (int i = 0; i < kPool; ++i)   /* the pulse: the diamond's opacity only (the page's .gem), the stem and the name stay */
    {
        if (mk.edge[i] != 0) mk.edge[i]->setAlpha(gemAlpha);
        if (mk.fill[i] != 0) mk.fill[i]->setAlpha(gemAlpha);
    }
    const int sl = (int)(stemLen + 0.5f);
    if (mk.stemEdge != 0) { mk.stemEdge->setCoord(MyGUI::IntCoord((int)cx - 2, h - sl - 1, 4, sl + 1)); mk.stemEdge->setVisible(true); }
    if (mk.stem != 0) { mk.stem->setCoord(MyGUI::IntCoord((int)cx - 1, h - sl, 2, sl)); mk.stem->setVisible(true); }
    if (mk.text != 0) mk.text->setPosition(MyGUI::IntPoint((int)cx - mk.tw / 2, (int)(cy - edgeHalf) - 2 - mk.th));
    *ax = (int)cx; *ay = h;
}
/* the arrow pointing along (dx, dy) with the name on its inner side; *ax, *ay = the tip's place in the root */
void LayoutArrow(Marker& mk, float dx, float dy, int* ax, int* ay)
{
    int w = 2 * ((int)kArrowLen + 10 + mk.tw), h = 2 * ((int)kArrowLen + 10 + mk.th);
    if (w < 60) w = 60;
    if (h < 60) h = 60;
    mk.root->setSize(MyGUI::IntSize(w, h));
    const float cx = (float)(w / 2), cy = (float)(h / 2);
    std::vector<pingwire::Span> sp;
    pingwire::ArrowSpans(cx + dx * 2.0f, cy + dy * 2.0f, dx, dy, kArrowLen + 4.0f, kArrowHalf + kEdgePx, kRowH, &sp); SetStripes(mk.edge, sp);
    pingwire::ArrowSpans(cx, cy, dx, dy, kArrowLen, kArrowHalf, kRowH, &sp); SetStripes(mk.fill, sp);
    const float adx = dx < 0.0f ? -dx : dx, ady = dy < 0.0f ? -dy : dy;
    const float k = kArrowLen + 6.0f + 0.5f * (adx * (float)mk.tw + ady * (float)mk.th);
    if (mk.text != 0) mk.text->setPosition(MyGUI::IntPoint((int)(cx - dx * k) - mk.tw / 2, (int)(cy - dy * k) - mk.th / 2));
    *ax = (int)cx; *ay = (int)cy;
}
void Hide(Marker& mk) { if (mk.root != 0) mk.root->setVisible(false); }
/* a map marker hidden - or, when it is no longer one of the holder's children, queued for its destroy by name */
void HideMap(Marker& mk) { if (MapAlive(mk)) mk.root->setVisible(false); else if (mk.root != 0) { ++g_gone; Doom(mk); } }
/* the marker made if missing; a map marker that is no longer one of the holder's children (cleared or rebuilt) is queued for its
   destroy by name and made again */
bool Ensure(Marker& mk, MyGUI::Widget* parent, const std::string& caption, bool withStem)
{
    if (mk.root != 0 && parent != 0 && !MapAlive(mk)) { ++g_gone; Doom(mk); }
    if (mk.root != 0) return true;
    return Make(mk, parent, caption, withStem);
}

/* ================= the spot ================= */
CamRayFn CamRay()
{
    static bool s_tried = false;
    static CamRayFn s_fn = 0;
    if (s_tried) return s_fn;
    s_tried = true;
    HMODULE h = ::GetModuleHandleA(kOgreDll);
    if (h != 0) s_fn = (CamRayFn)::GetProcAddress(h, kCamRaySym);
    if (s_fn == 0) Log(std::string("[PING] Ogre's Camera::getCameraToViewportRay did not resolve (") + (h == 0 ? "OgreMain_x64.dll is not loaded" : "no such export")
                       + ") - G cannot ping the world (the map still works)");
    return s_fn;
}
/* the mouse's ray in Ogre's (shifted) space: 1 read, 0 no camera / row / export, -1 faulted */
int RayPod(CamRayFn fn, float nx, float ny, float* o, float* d)
{
    if (fn == 0 || kRenderHolderPtrRva == 0) return 0;
    __try
    {
        const char* holder = *(const char* const*)AddrAbs(kRenderHolderPtrRva);
        if (holder == 0) return 0;
        const void* cam = *(const void* const*)(holder + kHolderCameraOff);
        if (cam == 0) return 0;
        float r[16];
        for (int i = 0; i < 16; ++i) r[i] = 0.0f;
        fn(cam, nx, ny, r);
        o[0] = r[0]; o[1] = r[1]; o[2] = r[2]; d[0] = r[3]; d[1] = r[4]; d[2] = r[5];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int HeightCb(void*, float x, float z, float* h) { return GroundTerrainHeightAt(x, z, h); }

/* the point a press picks at: the mouse (G), or the lever's given place on the view (fractions 0..1) */
struct Point { int x, y, vw, vh; };
bool PointOf(bool given, float fx, float fy, Point* p)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (im == 0 || rm == 0) return false;
    const MyGUI::IntSize vs = rm->getViewSize();
    if (vs.width <= 0 || vs.height <= 0) return false;
    p->vw = vs.width; p->vh = vs.height;
    if (given) { p->x = (int)(fx * (float)vs.width); p->y = (int)(fy * (float)vs.height); return true; }
    const MyGUI::IntPoint m = im->getMousePosition();
    p->x = m.left; p->y = m.top;
    return true;
}
/* the ground under the point in the world. false + *why when there is none */
bool PickWorld(const Point& pt, pingwire::Msg* m, std::string* why)
{
    const int mx = pt.x, my = pt.y, vw = pt.vw, vh = pt.vh;
    float o[3] = { 0, 0, 0 }, d[3] = { 0, 0, 0 };
    const int r = RayPod(CamRay(), (float)mx / (float)vw, (float)my / (float)vh, o, d);
    if (r != 1) { *why = r == 0 ? "no camera ray (row, camera or Ogre export missing)" : "the camera ray faulted"; return false; }
    float sx = 0, sy = 0, sz = 0;
    if (!TagsSceneOrigin(&sx, &sy, &sz)) { *why = "the scene's origin could not be read"; return false; }
    o[0] += sx; o[1] += sy; o[2] += sz;
    float best[3] = { 0, 0, 0 };
    float bestT = -1.0f;
    float hit[3] = { 0, 0, 0 };
    if (GroundTraceAt(o, d, hit) == 1 && pingwire::Finite(hit[0]) && pingwire::RayLineDistance(o, d, hit) <= kTraceOnRay)
    {
        const float t = pingwire::RayAlong(o, d, hit);
        if (t > 0.0f) { bestT = t; best[0] = hit[0]; best[1] = hit[1]; best[2] = hit[2]; }
    }
    float g[3] = { 0, 0, 0 };
    const float limit = bestT > 0.0f ? bestT : kMarchMax;
    bool fromTrace = bestT > 0.0f;
    if (pingwire::MarchGround(o, d, limit, kMarchStep, &HeightCb, 0, g))
    {
        const float t = pingwire::RayAlong(o, d, g);
        if (bestT < 0.0f || (t > 0.0f && t < bestT)) { bestT = t; best[0] = g[0]; best[1] = g[1]; best[2] = g[2]; fromTrace = false; }
    }
    if (bestT < 0.0f) { *why = "no ground under the mouse"; return false; }
    if (fromTrace) ++g_traceUsed; else ++g_marchUsed;
    m->kind = pingwire::kWorld; m->x = best[0]; m->y = best[1]; m->z = best[2];
    if (!pingwire::MsgOk(*m)) { *why = "the spot is outside the world"; return false; }
    return true;
}
bool InBranch(MyGUI::Widget* w, MyGUI::Widget* root)
{
    for (MyGUI::Widget* p = w; p != 0; p = p->getParent()) if (p == root) return true;
    return false;
}
/* the world map's spot under the mouse (the map's frame ran this frame). false + *why when the mouse is not on the map */
bool PickMap(const Point& pt, pingwire::Msg* m, std::string* why)
{
    const MapNow& v = g_mapNow;
    if (v.windowW == 0 || v.page == 0 || v.image == 0 || v.parent == 0) { *why = "the map's widgets were not read"; return false; }
    const int mx = pt.x, my = pt.y;
    MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
    MyGUI::Widget* top = lm != 0 ? lm->getWidgetFromPoint(mx, my) : 0;
    if (top == 0 || !InBranch(top, v.windowW)) { *why = "the mouse is not over the map window"; return false; }
    const MyGUI::IntCoord pg = v.page->getAbsoluteCoord();
    if (mx < pg.left || my < pg.top || mx >= pg.left + pg.width || my >= pg.top + pg.height) { *why = "the mouse is outside the map's view"; return false; }
    const MyGUI::IntCoord pc = v.parent->getAbsoluteCoord();
    float wx = 0, wz = 0;
    if (!pingwire::MapWorldFromPixel(mx - pc.left, my - pc.top, v.ox, v.oz, v.sx, v.sz, v.image->getWidth(), v.image->getHeight(), &wx, &wz))
    { *why = "the mouse is not on the map's picture"; return false; }
    m->kind = pingwire::kMap; m->x = wx; m->y = 0.0f; m->z = wz;
    return true;
}

/* ================= the road ================= */
/* a ping kept to show: the player's old one replaced (its widgets wait for the tick), the chat line written. No widget here. */
void Keep(int slot, const pingwire::Msg& m, bool own)
{
    const DWORD now = ::GetTickCount();
    if (pingwire::Place(&g_rule, slot, m, (unsigned long)now) == 2)   /* the replace rule: one live ping per player */
    {
        std::map<int, Entry>::iterator it = g_live.find(slot);
        if (it != g_live.end()) DoomEntry(it->second);
    }
    Entry& e = g_live[slot];
    e = Entry();
    e.m = m; e.at = now; e.own = own;
    e.caption = playerstab::EscapeHash(ChatPlayerName(slot));
    ChatPingLine(slot, m.kind, !own);
}
/* one ping from this player: the online teammates it may reach first (none: nothing happens - review fold 7), the sender's rate
   limit (`gate`), one copy to each, then - only when a copy went out, "so they know it went out" (the page) - this player's own
   marker and line. 1 sent, 0 nothing happened */
int Fire(pingwire::Msg m, bool gate, const char* src)
{
    const int me = StoreMySlot();
    const std::vector<int> mates = TeamMatesOfMine();
    std::vector<int> to;
    int skipped = 0;
    std::string skipWhy;
    for (size_t i = 0; i < mates.size(); ++i)
    {
        const int s = mates[i];
        float rel = 0.0f;
        const int relRead = RelationsMineTowards(s, &rel);
        const int v = pingwire::ReceiveVerdict(s, me, TeamSameAnyThread(me, s), StoreRosterSlotInWorld(s), relRead, rel);
        if (v == pingwire::kTake) to.push_back(s); else { ++skipped; skipWhy += " " + N(s) + ":" + pingwire::VerdictWord(v); }
    }
    if (to.empty())
    {
        ++g_noMate;
        Log(std::string("[PING] not sent - no online teammate to send it to; nothing happens (") + src
            + (skipped != 0 ? ", skipped" + skipWhy : std::string()) + ", noMate " + N(g_noMate) + ")");
        return 0;
    }
    const DWORD now = ::GetTickCount();
    if (gate && !pingwire::RateTake(&g_sendRate, (unsigned long)now))
    {
        ++g_sendCapped;
        Log(std::string("[PING] dropped (this player: more than 3 pings in 5 s - the press does nothing; ") + src + ", sendCapped " + N(g_sendCapped) + ")");
        return 0;
    }
    m.count = ++g_count;
    std::vector<char> bytes;
    const bool enc = pingwire::Encode(m, &bytes);
    int copies = 0, failed = 0;
    for (size_t i = 0; i < to.size(); ++i)
    {
        if (enc && StoreLiveReady() && StoreSendLive(cooplive::kRouteSlot, (unsigned)to[i], cooplive::kInnerSpotPing, bytes, true)) ++copies;
        else ++failed;
    }
    ++g_sent; g_sendCopies += copies; g_sendFailed += failed;
    Log(std::string("[PING] sent ") + pingwire::KindWord(m.kind) + " #" + N(m.count) + " at " + F(m.x) + "," + F(m.y) + "," + F(m.z)
        + " to " + N(copies) + " teammate(s)" + (failed != 0 ? " (" + N(failed) + " send(s) failed)" : std::string())
        + (skipped != 0 ? " (skipped" + skipWhy + ")" : std::string()) + " (" + src + (gate ? "" : ", no sender gate") + ")"
        + (copies == 0 ? " - none went out: no own marker or line" : std::string()));
    if (copies == 0) return 0;
    Keep(me, m, true);
    return 1;
}
/* G's road after the key is read (G, or the `press` lever at a given place): not while typing in chat or any text box (the guard
   the name tags' Insert key obeys: UiPlayerTyping - a writable text box holds the key focus, not merely any widget such as the map);
   the map's spot when the map is open this frame; else - unless a game window (inventory, trade, any interface window) is under the point, where G does nothing (review fold 3) - the ground in the world */
void Press(const char* src, bool given, float fx, float fy)
{
    if (UiPlayerTyping()) { ++g_typing; Log(std::string("[PING] ignored - typing in a text box (") + src + ")"); return; }
    Point pt;
    if (!PointOf(given, fx, fy, &pt)) { ++g_noSpot; Log(std::string("[PING] ping not made: the mouse could not be read (") + src + ")"); return; }
    if (!g_mapOpen)
    {
        MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
        MyGUI::Widget* top = lm != 0 ? lm->getWidgetFromPoint(pt.x, pt.y) : 0;
        if (top != 0)
        {
            ++g_overWindow;
            Log("[PING] ignored - a game window is under the " + std::string(given ? "point " : "mouse ") + N(pt.x) + "," + N(pt.y)
                + " (widget " + top->getName() + "; " + src + ", overWindow " + N(g_overWindow) + ")");
            return;
        }
    }
    pingwire::Msg m;
    std::string why;
    const bool ok = g_mapOpen ? PickMap(pt, &m, &why) : PickWorld(pt, &m, &why);
    if (!ok)
    {
        ++g_noSpot;
        Log(std::string("[PING] ") + (g_mapOpen ? "map" : "world") + " ping not made: " + why + " (" + src + ", noSpot " + N(g_noSpot) + ")");
        return;
    }
    Fire(m, true, src);
}

/* T-1437: the press is the game's own: its key-down dispatch 0x360AD0 sets the Ping action's down byte (entry +0x10 points at
   g_pingDown), and our hook on that dispatch (detour_ihKeyDown) turns the byte's rise into g_pingPressed - read and cleared here,
   one press one ping. The down byte alone could lose a quick tap (review F1323 SHOULD 1): for a held action the release 0x360D40
   writes 0 through +0x10 with no other test, and the game delivers a frame's presses and releases before this tick runs, so a
   press and release inside one frame left 0 behind (Read: 0x360D40 disassembly 0x360DFC-0x360E0A; the per-frame reset 0x360A20
   clears only NON-held actions, decomp_360a20.txt:13-15). A press action instead would queue our command id 0 into the game's
   command list (decomp_360ad0.txt:158-160), so Ping stays held and the edge is caught in the dispatch.
   The world map needs nothing extra (review SHOULD 2, Read): the dispatch's 'keys on' flag handler+0xD0 is written only by the
   handler's set-up 0x365260 and by the Faction tab's name box (off on key focus, on when it loses focus or the tab closes:
   delegates 0x4A6A70 / 0x491D40 made at 0x493A32 / 0x493A6F, destructor 0x4920D7) - a scan of all 1.0.65 code for writes to
   0x21323F0 and of the handler's users for +0xD0; and the key listener 0x82A460 calls the dispatch unless the rebind popup, an
   EditBox / ComboBox key focus or the console takes the key (decomp_82a460.txt:44-110). None of that is the map.
   The dispatch itself tests neither the foreground window nor a text box (Read), so both guards stay: the foreground one here,
   the typing one in Press. */
void PollKey()
{
    const bool pressed = g_pingPressed != 0;
    g_pingPressed = 0;
    if (!pressed) return;
    HWND fg = ::GetForegroundWindow();   /* our window only */
    DWORD pid = 0;
    if (fg == 0 || (::GetWindowThreadProcessId(fg, &pid), pid) != ::GetCurrentProcessId()) return;
    Press("key", false, 0.0f, 0.0f);
}

/* ================= T-1437: the Ping action in Kenshi's own key list and its Controls row =================
   Every struct fact below is read from our own 1.0.65 decompiles / disassembly (build/decomp_<rva>.txt, build/fndis_3f0120_t1437.txt)
   and, for 1.0.68, from gen_table_1068 (the same code where it matched the function unchanged) and the T-1437 builder's disassembly
   of the 1.0.68 OptionsWindow::create, build/fndis_3fad00_t1437_1068.txt (the row size at 0x3FAD4D and the spacer call 0x3FAE46
   after the last key row; gen_table_1068's hand_ret_moved_ok checks that call is the 9th spacer call in both versions):
   - the handler is ONE global object (InputHandlerGlobal; 0x3EFEC0 and 0x3E7410 pass &DAT_142132320 as `this`);
   - addCommand 0x3633E0 (this, const std::string& name, int command, int key1, int key2, int modMask, int mode) makes the entry in
     the name map; find 0x361EF0 (this, name) returns the entry or 0 (it does not insert); bind 0x362200 (this, name, key) puts the
     key in the mode-0 key map (+0x28, its head at +0x30) or the mode-1 map (+0x50, head +0x58) and strips Shift / Ctrl / Alt
     (key & 0xF0FF) when the entry is held; keyBound 0x360F50 (this, key) asks both key maps; parseKey 0x361B30 (this, word) -> key,
     0 for a word it does not know (it logs it); keyName 0x361280 (this, std::string* out, key, int) constructs the word in `out`;
   - an entry: +0x04 the held byte, +0x10 a pointer to the down byte (initialise 0x363660 sets both for every camera_* action:
     entry+0x10 = &byte, entry+4 = 1, byte = 0); the dispatch 0x360AD0 writes 1 through +0x10 at a press and queues a command only
     when the held byte is 0;
   - a key-map node (VS2010 std::map): +0x00 left, +0x08 parent, +0x10 right, +0x18 the key, +0x20 the entry, +0x29 the nil byte
     (0x360AD0, 0x360F50, saveConfig 0x361450 - which writes `name=key` for every entry in either key map, ours included);
   - a Controls row: Ogre's AllocatedObject new(0x158) (create 0x3F5647 MOV ECX,0x158; 1.0.68 0x3FAD4D the same), then
     DataPanelLine_KeyConfig's constructor 0x3E7B20 (mem, const std::string& action, const std::string& label, 0x19) - it keeps
     the action name at +0x130 and the type 0xC at +0x118 - then DatapanelGUI's add 0x6FEC00 (panel, line); the section is 0x19;
   - the Controls list ends: last key row, a spacer 0x6FD830 (panel, 0x19, float) whose call returns to OptionsControlsEndRet,
     the "Defaults" button, a spacer. Our row goes in just before that first spacer: the end of the key rows.
   - rebinding and Defaults need nothing of ours: the key popup 0x3EFEC0 rebinds the row's action by the name at row+0x130,
     and Defaults 0x3E7410 re-runs initialise (our hook adds the action again with G) and refreshes every type-0xC row. */
unsigned long long kIhInitRva = 0;      static AddrReg kIhInitRva_reg("InputHandlerInit", &kIhInitRva);                               /* Steam_1.0.65 0x363660 InputHandler::initialise(this) - hooked */
unsigned long long kIhAddCmdRva = 0;    static AddrReg kIhAddCmdRva_reg("InputHandlerAddCommand", &kIhAddCmdRva, kAddrUseCallOnly);    /* 0x3633E0 */
unsigned long long kIhFindRva = 0;      static AddrReg kIhFindRva_reg("InputHandlerFind", &kIhFindRva, kAddrUseCallOnly);              /* 0x361EF0 */
unsigned long long kIhBindRva = 0;      static AddrReg kIhBindRva_reg("InputHandlerBind", &kIhBindRva, kAddrUseCallOnly);              /* 0x362200 */
unsigned long long kIhParseKeyRva = 0;  /* T2577: NOT called - its name table exists only inside loadConfig (see GameKeyForWord) */  static AddrReg kIhParseKeyRva_reg("InputHandlerParseKey", &kIhParseKeyRva, kAddrUseCallOnly);  /* 0x361B30 */
unsigned long long kIhKeyBoundRva = 0;  static AddrReg kIhKeyBoundRva_reg("InputHandlerKeyBound", &kIhKeyBoundRva, kAddrUseCallOnly);  /* 0x360F50 */
unsigned long long kIhKeyNameRva = 0;   static AddrReg kIhKeyNameRva_reg("InputHandlerKeyName", &kIhKeyNameRva, kAddrUseCallOnly);    /* 0x361280 */
unsigned long long kIhGlobalRva = 0;    static AddrReg kIhGlobalRva_reg("InputHandlerGlobal", &kIhGlobalRva);                         /* 0x2132320 the handler object itself */
unsigned long long kKeyLineCtorRva = 0; static AddrReg kKeyLineCtorRva_reg("KeyConfigLineCtor", &kKeyLineCtorRva, kAddrUseCallOnly);  /* 0x3E7B20 */
unsigned long long kDpAddLineRva = 0;   static AddrReg kDpAddLineRva_reg("DatapanelAddLine", &kDpAddLineRva, kAddrUseCallOnly);       /* 0x6FEC00 */
unsigned long long kDpSpacerRva = 0;    static AddrReg kDpSpacerRva_reg("DatapanelAddSpacer", &kDpSpacerRva);                         /* 0x6FD830 - hooked */
unsigned long long kOptCtlEndRet = 0;   static AddrReg kOptCtlEndRet_reg("OptionsControlsEndRet", &kOptCtlEndRet);                    /* 0x3FAB2E (1.0.68 0x3FAE4B) */
unsigned long long kIhKeyDownRva = 0;   static AddrReg kIhKeyDownRva_reg("InputHandlerKeyDown", &kIhKeyDownRva);                      /* 0x360AD0 the key-down dispatch (this, key) - hooked */
unsigned long long kGameFreeRva = 0;    static AddrReg kGameFreeRva_reg("GameFreeStub", &kGameFreeRva, kAddrUseCallOnly);              /* 0xED5628 (1.0.68 0xED64F8) the game's free stub */

const int kEntHeld = 0x04, kEntDownPtr = 0x10;                     /* entry fields (initialise 0x363660) */
const int kIhKeyHead0 = 0x30, kIhKeyHead1 = 0x58;                  /* the two key maps' head nodes (0x360AD0) */
const int kNodeLeft = 0x00, kNodeParent = 0x08, kNodeRight = 0x10, kNodeKey = 0x18, kNodeVal = 0x20, kNodeNil = 0x29;
const unsigned kModStrip = 0xF0FFu;                                /* bind's mask for a held action (0x362200) */
const size_t kRowBytes = 0x158;
const int kControlsSection = 0x19;
const char kOgreNewSym[] = "??2?$AllocatedObject@V?$CategorisedAllocPolicy@$0A@@Ogre@@@Ogre@@SAPEAX_K@Z";   /* the create's own allocator import */

typedef void (*IhInitFn)(void* ih);
typedef void (*IhAddCmdFn)(void* ih, const std::string* name, int command, int key1, int key2, int modMask, int mode);
typedef void* (*IhFindFn)(void* ih, const std::string* name);
typedef void (*IhBindFn)(void* ih, const std::string* name, unsigned key);
typedef unsigned (*IhParseKeyFn)(void* ih, const std::string* word);
typedef unsigned char (*IhKeyBoundFn)(void* ih, unsigned key);
typedef void* (*IhKeyNameFn)(void* ih, void* outString, unsigned key, int flag);
typedef void* (*KeyLineCtorFn)(void* mem, const std::string* action, const std::string* label, int section);
typedef void* (*DpAddLineFn)(void* panel, void* line);
typedef void* (*DpSpacerFn)(void* panel, int section, float height);
typedef void* (*OgreNewFn)(size_t bytes);
typedef void (*IhKeyDownFn)(void* ih, unsigned key);
typedef void (*GameDeleteFn)(void* p);

IhInitFn orig_ihInit = 0;
DpSpacerFn orig_dpSpacer = 0;
IhKeyDownFn orig_ihKeyDown = 0;
int g_keyHooks = 0;                   /* 1 all three hooks in (initialise, spacer, key-down), -1 not */
bool g_actionSetUp = false;           /* the title tick's one set-up */
long long g_rowsAdded = 0, g_rowFail = 0, g_actionFail = 0;
long long g_lateAdds = 0;             /* T-1437 fold 3: the title tick had to add the action (the early add did not take) - a run asserts 0 */

bool KeyRowsOk()
{
    return kIhInitRva != 0 && kIhAddCmdRva != 0 && kIhFindRva != 0 && kIhBindRva != 0 && kIhParseKeyRva != 0 && kIhKeyBoundRva != 0
        && kIhKeyNameRva != 0 && kIhGlobalRva != 0 && kKeyLineCtorRva != 0 && kDpAddLineRva != 0 && kDpSpacerRva != 0 && kOptCtlEndRet != 0 && kIhKeyDownRva != 0;
}
void* Ih() { return (void*)AddrAbs(kIhGlobalRva); }

/* the entry (0 = none): 1 asked, -1 faulted */
int FindPod(void* ih, const std::string* name, void** entry)
{
    __try { *entry = ((IhFindFn)AddrAbs(kIhFindRva))(ih, name); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *entry = 0; return -1; }
}
/* the action made as the game makes its held camera keys, its down byte ours: 1 made, 0 not found after, -1 faulted */
int AddPod(void* ih, const std::string* name, void** entry)
{
    __try
    {
        ((IhAddCmdFn)AddrAbs(kIhAddCmdRva))(ih, name, 0, 0, 0, 0, 0);
        void* e = ((IhFindFn)AddrAbs(kIhFindRva))(ih, name);
        if (e == 0) return 0;
        *(volatile unsigned char**)((char*)e + kEntDownPtr) = &g_pingDown;
        *((unsigned char*)e + kEntHeld) = 1;
        g_pingDown = 0;
        *entry = e;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* 1 bound now, 0 the key is another action's (left to it), -1 faulted */
int BindPod(void* ih, const std::string* name, unsigned key)
{
    __try
    {
        if (((IhKeyBoundFn)AddrAbs(kIhKeyBoundRva))(ih, key & kModStrip) != 0) return 0;
        ((IhBindFn)AddrAbs(kIhBindRva))(ih, name, key);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-1437 fold 3: the game's plain bind 0x362200, exactly as addCommand 0x3633E0 binds a default key (decomp_3633e0.txt:65-70 -
   no is-bound check). 1 bound, -1 faulted */
int PlainBindPod(void* ih, const std::string* name, unsigned key)
{
    __try { ((IhBindFn)AddrAbs(kIhBindRva))(ih, name, key); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* the keys bound to `entry` in one key map (an in-order walk of its tree, depth-bounded). Called only inside KeysPod's __try. */
void WalkKeys(const char* node, const char* head, const void* entry, unsigned* keys, int* n, int depth)
{
    if (node == 0 || node == head || depth > 64 || *n >= pingwire::kMaxKeys || *(node + kNodeNil) != 0) return;
    WalkKeys(*(const char* const*)(node + kNodeLeft), head, entry, keys, n, depth + 1);
    if (*n < pingwire::kMaxKeys && *(const void* const*)(node + kNodeVal) == entry) keys[(*n)++] = *(const unsigned*)(node + kNodeKey);
    WalkKeys(*(const char* const*)(node + kNodeRight), head, entry, keys, n, depth + 1);
}
int KeysPod(void* ih, const void* entry, unsigned* keys, int* n)
{
    __try
    {
        const int heads[2] = { kIhKeyHead0, kIhKeyHead1 };
        for (int i = 0; i < 2; ++i)
        {
            const char* head = *(const char* const*)((const char*)ih + heads[i]);
            if (head != 0) WalkKeys(*(const char* const*)(head + kNodeParent), head, entry, keys, n, 0);
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* the game's word for a key into `raw` (uninitialised std::string storage the function constructs): 1 made, -1 faulted */
int KeyNamePod(void* ih, void* raw, unsigned key)
{
    __try { ((IhKeyNameFn)AddrAbs(kIhKeyNameRva))(ih, raw, key, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* review F1323 NIT / F1355 SHOULD: the game-built std::string's buffer is freed the way the game frees one - a call to the game's
   own free stub, row GameFreeStub (1.0.65 0xED5628, 1.0.68 0xED64F8; the stub every `if (capacity > 15) operator_delete(text)`
   in the game calls, e.g. decomp_363660.txt:69-70). Evidence, read from both executables' own import tables:
   C:/code/kenshi-coop/build/imports_free_t1437.txt - in both builds that stub is the only `jmp [IAT]` through the slot importing
   mfc100u.dll ordinal 1278, and that export is `jmp [MSVCR100.dll!free]`. Only when the text is on the heap (capacity at +0x18
   > 15). No row -> the few bytes are left, never handed to another allocator; a fault is caught (the bytes are left).
   1 freed, 0 nothing to free or no row, -1 faulted. No C++ objects here (__try). */
int GameStringFree(void* raw)
{
    if (kGameFreeRva == 0) return 0;
    __try
    {
        if (*(const unsigned long long*)((const char*)raw + 0x18) <= 15) return 0;
        ((GameDeleteFn)AddrAbs(kGameFreeRva))(*(void**)raw);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T2577 (Confirmed: log-A-life1.txt:573 'G is not a key the game knows', the game's own kenshi_info log
   '[InputHandler::parseKey] Invalid key string 'G''): parseKey 0x361B30 looks the word up in the handler's name table at +0xA8
   (decomp_361b30.txt:142-145, :176-192), and the game fills that table only inside loadConfig 0x3623D0 - from its static key-name
   list PTR_DAT_141cf7e70 (decomp_3623d0.txt:517-614) - and empties it again at loadConfig's end (:864-868). Outside loadConfig every
   word is 'invalid'. So the word is matched against the SAME list through the game's own key -> word function KeyName 0x361280
   (flag 0 reads PTR_DAT_141cf7e70[key & 0xff], decomp_361280.txt:39-51), with parseKey's own prefix / mouse handling
   (pingwire::KeyWordSplit). 0 = a word the game does not know (or KeyName faulted). Main thread. */
unsigned GameKeyForWord(const std::string& word)
{
    unsigned mods = 0, mouse = 0;
    const std::string name = pingwire::KeyWordSplit(word, &mods, &mouse);
    if (mouse != 0) return mouse | mods;
    if (name.empty()) return 0;
    for (unsigned k = 1; k < 256; ++k)
    {
        union { void* align; char raw[sizeof(std::string)]; } u;
        if (KeyNamePod(Ih(), u.raw, k) != 1) return 0;
        const bool same = *(const std::string*)u.raw == name;
        GameStringFree(u.raw);
        if (same) return k | mods;
    }
    return 0;
}
std::string KeyWord(unsigned key)
{
    union { void* align; char raw[sizeof(std::string)]; } u;
    if (KeyNamePod(Ih(), u.raw, key) != 1) return "key " + N((long long)key);
    const std::string w = *(const std::string*)u.raw;
    GameStringFree(u.raw);
    return w;
}
/* "G", "G and H", "none - ..." - the per-load line's words */
std::string KeyWords()
{
    if (g_keyHooks != 1) return "none - the Ping action is not set up (see the [PING] start line)";
    const std::string name(pingwire::kActionName);
    void* e = 0;
    if (FindPod(Ih(), &name, &e) != 1 || e == 0) return "none - the Ping action is not in the game's key list";
    unsigned keys[2] = { 0, 0 };
    int n = 0;
    if (KeysPod(Ih(), e, keys, &n) != 1) return "unknown - reading the key maps faulted";
    if (n == 0) return "none - Ping has no key (bind one in Settings > Controls)";
    std::string s = KeyWord(keys[0]);
    for (int i = 1; i < n; ++i) s += " and " + KeyWord(keys[i]);
    return s;
}
/* the action added and its keys bound: from controls.cfg's sw_ping lines when `fromCfg` (none -> G), else G (the game's defaults).
   A key another action holds is left to it. MAIN THREAD: the title tick, or the game's own initialise (Controls > Defaults). */
void ActionSetUp(bool fromCfg, const char* why)
{
    void* ih = Ih();
    const std::string name(pingwire::kActionName);
    void* e = 0;
    if (FindPod(ih, &name, &e) != 1) { ++g_actionFail; ErrorLog(std::string("[PING] the game's key list could not be read (") + why + ") - the Ping key is not set up"); return; }
    if (e != 0 && fromCfg) { Log(std::string("[PING] the Ping action is already in the game's key list (") + why + ") - left as it is"); return; }
    if (fromCfg)
    {   /* fold 3: the LATE road - the early add at the mod's start did not take, so the game's key load skipped a saved sw_ping line */
        ++g_lateAdds;
        ErrorLog(std::string("[PING] LATE ROAD: the Ping action was not in the game's key list at the title tick (") + why
            + ") - the early add at the mod's start did not take; it is added now, AFTER the game's key load, and a saved key another action holds is lost (late adds "
            + N(g_lateAdds) + ")");
    }
    if (AddPod(ih, &name, &e) != 1) { ++g_actionFail; ErrorLog(std::string("[PING] adding the Ping action faulted or did not take (") + why + ") - the Ping key does nothing"); return; }
    std::vector<std::string> words;
    std::string from = "the default";
    if (fromCfg)
    {
        std::string text;
        FILE* f = ::fopen("controls.cfg", "rb");   /* the game's own file, opened by the same relative name as its loadConfig 0x3623D0 */
        if (f != 0)
        {
            char buf[4096];
            size_t got;
            while ((got = ::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 1000000) text.append(buf, got);
            ::fclose(f);
        }
        pingwire::CfgPingKeys(text, &words);
        if (!words.empty()) from = "controls.cfg";
    }
    if (words.empty()) words.push_back(pingwire::kDefaultKey);
    std::string said;
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (!said.empty()) said += "; ";
        const unsigned key = GameKeyForWord(words[i]);
        if (key == 0) { said += words[i] + " is not a key the game knows"; continue; }
        const int br = BindPod(ih, &name, key);
        said += words[i] + (br == 1 ? " bound" : br == 0 ? " left to the action that holds it" : " faulted");
    }
    Log(std::string("[PING] the Ping action is in the game's key list (") + why + "); its key from " + from + ": " + said);
}
/* T-1437 fold 3 (F1396 / F1397): the action and its default key G made at the mod's start (InstallPing, inside Root::initialise).
   The game's static initialiser has already built the handler's defaults (initialise 0x363660; no 'key defaults were set' line in
   any evidence/hand-1806 log, yet focus_char held F at the title tick), and its own controls.cfg load (loadConfig 0x3623D0 /
   1.0.68 0x362430, called from 0x8198AB) runs after this - so that load binds a saved sw_ping line by the game's own rules, as for
   a built-in action. G is bound with the plain bind, as the game binds its defaults. MAIN THREAD (the game's start). */
void ActionEarlyAdd()
{
    void* ih = Ih();
    const std::string name(pingwire::kActionName);
    void* e = 0;
    if (FindPod(ih, &name, &e) != 1) { ++g_actionFail; ErrorLog("[PING] the game's key list could not be read at the mod's start - the Ping action is not added early"); return; }
    if (e != 0) { Log("[PING] the Ping action is already in the game's key list at the mod's start, before the game's key load - left as it is"); return; }
    if (AddPod(ih, &name, &e) != 1) { ++g_actionFail; ErrorLog("[PING] adding the Ping action at the mod's start faulted or did not take"); return; }
    const unsigned key = GameKeyForWord(pingwire::kDefaultKey);
    const int br = key == 0 ? 0 : PlainBindPod(ih, &name, key);
    Log(std::string("[PING] the Ping action was added to the game's key list at the mod's start, BEFORE the game's key load (controls.cfg); its default key ")
        + pingwire::kDefaultKey + (br == 1 ? " bound" : br == 0 ? " is not a key the game knows - not bound" : " faulted - not bound")
        + " - a saved sw_ping line is read by the game's own load");
}
void detour_ihInit(void* ih)
{
    orig_ihInit(ih);
    if (g_off != 0 || g_keyHooks != 1 || ih != Ih()) return;   /* review F1355 NIT: not unless all three hooks went in */
    try { ActionSetUp(false, "the game's key defaults were set"); }
    catch (...) { ++g_throws; }
}
/* the Controls row: made as the create makes every key row; 1 added, 0 Ogre's allocator did not resolve, -1 faulted */
int RowPod(void* panel, const std::string* action, const std::string* label)
{
    __try
    {
        static OgreNewFn s_new = 0;
        if (s_new == 0)
        {
            HMODULE h = ::GetModuleHandleA(kOgreDll);
            if (h != 0) s_new = (OgreNewFn)::GetProcAddress(h, kOgreNewSym);
            if (s_new == 0) return 0;
        }
        void* mem = s_new(kRowBytes);
        if (mem == 0) return 0;
        void* line = ((KeyLineCtorFn)AddrAbs(kKeyLineCtorRva))(mem, action, label, kControlsSection);
        ((DpAddLineFn)AddrAbs(kDpAddLineRva))(panel, line);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void AddRow(void* panel)
{
    const std::string action(pingwire::kActionName), label(pingwire::kRowLabel);
    void* e = 0;
    if (FindPod(Ih(), &action, &e) != 1 || e == 0) { ++g_rowFail; Log("[PING] the Controls list has no Ping row: the Ping action is not in the game's key list"); return; }
    const int r = RowPod(panel, &action, &label);
    if (r == 1) { ++g_rowsAdded; Log("[PING] the Ping row was added at the end of the Controls list's key rows (rows added " + N(g_rowsAdded) + ")"); }
    else { ++g_rowFail; ErrorLog(std::string("[PING] the Controls list's Ping row was not added (") + (r == 0 ? "Ogre's allocator did not resolve" : "it faulted") + ")"); }
}
/* the game's key-down dispatch (this, key): a rise of our down byte inside it is a press of the Ping key, kept until the tick
   reads it. The byte is zeroed first so a release the game never delivered (focus lost while held) cannot hide the next press. */
void detour_ihKeyDown(void* ih, unsigned key)
{
    g_pingDown = 0;
    orig_ihKeyDown(ih, key);
    if (g_pingDown != 0) g_pingPressed = 1;
}
/* DatapanelGUI's spacer: when the call is OptionsWindow::create's own after its last key row, the Ping row goes in first */
void* detour_dpSpacer(void* panel, int section, float height)
{
    if (g_off == 0 && g_keyHooks == 1 && section == kControlsSection && _ReturnAddress() == (void*)AddrAbs(kOptCtlEndRet))
    {
        try { AddRow(panel); }
        catch (...) { ++g_throws; }
    }
    return orig_dpSpacer(panel, section, height);
}

/* ================= drawing ================= */
void FadeLog(int slot, const Entry& e, DWORD now)
{
    ++g_faded;
    Log("[PING] faded " + Who(slot) + " " + pingwire::KindWord(e.m.kind) + " #" + N(e.m.count) + " after " + N((long long)(now - e.at)) + " ms");
}
void DrawWorld(int slot, Entry& e, unsigned long age, int vw, int vh, bool canShow)
{
    if (e.m.kind != pingwire::kWorld || !canShow)
    {
        Hide(e.world); Hide(e.arrow);
        if (e.m.kind == pingwire::kWorld && e.whereLogged != 3 && e.whereLogged != 0) e.whereLogged = 3;
        return;
    }
    float sx = 0, sy = 0, vx = 0, vy = 0, vz = 0;
    const int r = TagsScreenSpot(e.m.x, e.m.y, e.m.z, &sx, &sy, &vx, &vy, &vz);
    if (r < 0) { Hide(e.world); Hide(e.arrow); return; }
    const float alpha = pingwire::AlphaAt(age);
    int where = 0;
    if (r == 1 && pingwire::OnScreen(sx, sy, vw, vh, 0))
    {
        Hide(e.arrow);
        if (!Ensure(e.world, 0, e.caption, true)) return;
        int ax = 0, ay = 0;
        LayoutMarker(e.world, 1.0f, pingwire::PulseAlphaAt(age), &ax, &ay);
        e.world.root->setPosition(MyGUI::IntPoint((int)sx - ax, (int)sy - ay));
        e.world.root->setAlpha(alpha);
        e.world.root->setVisible(true);
        where = 1;
    }
    else
    {
        Hide(e.world);
        float dx = 0, dy = 1;
        if (r == 1) pingwire::ScreenDirFromPoint(sx, sy, vw, vh, &dx, &dy);   /* review fold 4: projected - towards its own screen place */
        else pingwire::ScreenDir(vx, vy, vz, &dx, &dy);                        /* behind the camera: the view-space direction */
        int ex = 0, ey = 0;
        if (!pingwire::EdgePlace(vw, vh, kEdgeMargin, dx, dy, &ex, &ey)) { Hide(e.arrow); return; }
        if (!Ensure(e.arrow, 0, e.caption, false)) return;
        int ax = 0, ay = 0;
        LayoutArrow(e.arrow, dx, dy, &ax, &ay);
        e.arrow.root->setPosition(MyGUI::IntPoint(ex - ax, ey - ay));
        e.arrow.root->setAlpha(alpha);
        e.arrow.root->setVisible(true);
        where = 2;
    }
    if (where != e.whereLogged)
    {
        e.whereLogged = where;
        Log("[PING] shown world " + std::string(where == 1 ? "on screen" : "edge") + " " + Who(slot) + " #" + N(e.m.count) + " at " + F(e.m.x) + "," + F(e.m.z)
            + (where == 1 ? " screen " + N((long long)sx) + "," + N((long long)sy) : std::string()) + " (age " + N((long long)age) + " ms)");
    }
}
void TickBody()
{
    DrainDoomed();
    g_mapOpen = g_mapSeq != g_mapSeqSeen;
    g_mapSeqSeen = g_mapSeq;
    if (!GameplayRunning() || EngineWritesBlocked())
    {
        g_pingDown = 0;      /* a press during a load or in a menu is not a ping */
        g_pingPressed = 0;
        g_keySaid = false;   /* the next load names the key again */
        for (std::map<int, Entry>::iterator it = g_live.begin(); it != g_live.end(); ++it) { Hide(it->second.world); Hide(it->second.arrow); }
        return;
    }
    if (!g_keySaid) { g_keySaid = true; Log("[PING] the ping key: " + KeyWords() + " (late adds " + N(g_lateAdds) + ")"); }
    PollKey();
    if (g_live.empty()) return;
    const DWORD now = ::GetTickCount();
    int vw = 0, vh = 0;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize vs = rm->getViewSize(); vw = vs.width; vh = vs.height; }
    const bool canShow = !g_mapOpen && TagsInterfaceShown() != 0 && vw > 0 && vh > 0;
    for (std::map<int, Entry>::iterator it = g_live.begin(); it != g_live.end();)
    {
        Entry& e = it->second;
        const unsigned long age = (unsigned long)(now - e.at);
        if (pingwire::PhaseAt(age) == pingwire::kGone)
        {
            FadeLog(it->first, e, now);
            DoomEntry(e);
            g_rule.erase(it->first);
            g_live.erase(it++);
            continue;
        }
        DrawWorld(it->first, e, age, vw, vh, canShow);
        ++it;
    }
    DrainDoomed();
}
void MapFrameBody()
{
    const MapNow& v = g_mapNow;
    if (g_live.empty() || v.image == 0 || v.parent == 0) return;
    const DWORD now = ::GetTickCount();
    const int cw = v.image->getWidth(), ch = v.image->getHeight();
    ReadKids(v.parent);   /* review fold 8: the holder's children, once this frame */
    for (std::map<int, Entry>::iterator it = g_live.begin(); it != g_live.end(); ++it)
    {
        Entry& e = it->second;
        const unsigned long age = (unsigned long)(now - e.at);
        if (pingwire::PhaseAt(age) == pingwire::kGone) { HideMap(e.map); continue; }   /* the tick takes it down */
        int px = 0, py = 0;
        if (!mapdots::CanvasPixel(e.m.x, e.m.z, v.ox, v.oz, v.sx, v.sz, cw, ch, &px, &py)) { HideMap(e.map); continue; }
        if (!Ensure(e.map, v.parent, e.caption, true)) continue;
        int ax = 0, ay = 0;
        LayoutMarker(e.map, kMapScale, pingwire::PulseAlphaAt(age), &ax, &ay);
        e.map.root->setPosition(MyGUI::IntPoint(px - ax, py - ay));
        e.map.root->setAlpha(pingwire::AlphaAt(age));
        e.map.root->setVisible(true);
        if (!e.mapLogged)
        {
            e.mapLogged = true;
            Log("[PING] shown map " + Who(it->first) + " " + pingwire::KindWord(e.m.kind) + " #" + N(e.m.count) + " at " + F(e.m.x) + "," + F(e.m.z)
                + " canvas " + N(px) + "," + N(py) + " of " + N(cw) + "x" + N(ch) + " (age " + N((long long)age) + " ms)");
        }
    }
}
void TickCatching() { try { TickBody(); } catch (...) { ++g_throws; } }
void MapFrameCatching() { try { MapFrameBody(); } catch (...) { ++g_throws; } }
void DropAllBody()
{
    for (std::map<int, Entry>::iterator it = g_live.begin(); it != g_live.end(); ++it) DoomEntry(it->second);
    g_live.clear(); g_rule.clear();
    DrainDoomed();
}
int DropAllPod()
{
    __try { DropAllBody(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int TickPod()
{
    __try { TickCatching(); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}
int MapFramePod()
{
    __try { MapFrameCatching(); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

std::string ShowLine()
{
    return "sent=" + N(g_sent) + " copies=" + N(g_sendCopies) + " sendFailed=" + N(g_sendFailed) + " sendCapped=" + N(g_sendCapped)
         + " noSpot=" + N(g_noSpot) + " typingIgnored=" + N(g_typing) + " in=" + N(g_in) + " inMalformed=" + N(g_inMalformed) + " inCapped=" + N(g_inCapped)
         + " inDropped[self,notMate,hostile,offline,unreadable]=" + N(g_inDropped[1]) + "," + N(g_inDropped[2]) + "," + N(g_inDropped[3]) + "," + N(g_inDropped[4]) + "," + N(g_inDropped[5])
         + " noMate=" + N(g_noMate) + " overWindow=" + N(g_overWindow)
         + " live=" + N((long long)g_live.size()) + " faded=" + N(g_faded) + " spot[trace,terrain]=" + N(g_traceUsed) + "," + N(g_marchUsed)
         + " makeFail=" + N(g_makeFail) + " gone=" + N(g_gone) + " throws=" + N(g_throws) + " mapOpen=" + N(g_mapOpen ? 1 : 0) + " off=" + N(g_off) + " lateAdds=" + N(g_lateAdds);
}

}   /* namespace */

void PingTick()
{
    if (g_off != 0)
    {
        if (!g_offSaid)
        {
            g_offSaid = true;
            const int d = DropAllPod();
            g_live.clear(); g_rule.clear(); g_doomed.clear();
            ErrorLog(std::string("[PING] the ping work faulted once - pings are off for the rest of this run; the markers ") + (d == 1 ? "were removed" : "were left (the removal faulted too)"));
        }
        return;
    }
    if (TickPod() != 0) g_off = 1;
}

void PingMapFrame(MyGUI::Widget* windowW, MyGUI::Widget* page, MyGUI::Widget* image, MyGUI::Widget* parent,
                  float originX, float originZ, float spanX, float spanZ)
{
    ++g_mapSeq;
    g_mapNow.windowW = windowW; g_mapNow.page = page; g_mapNow.image = image; g_mapNow.parent = parent;
    g_mapNow.ox = originX; g_mapNow.oz = originZ; g_mapNow.sx = spanX; g_mapNow.sz = spanZ;
    if (g_off != 0) return;
    if (MapFramePod() != 0) g_off = 1;   /* our own frame: a fault here never reaches the teammates' dots */
}

/* the receiver's work (PingLiveIn wraps it as ChatLiveIn wraps its own) */
static void PingLiveInBody(int originSlot, const void* p, unsigned n)
{
    pingwire::Msg m;
    if (!pingwire::Decode(p, (size_t)n, &m))
    {
        ++g_inMalformed;
        Log("[PING] dropped (malformed: " + N((long long)n) + " bytes) from " + Who(originSlot) + " (inMalformed " + N(g_inMalformed) + ")");
        return;
    }
    const int me = StoreMySlot();
    float rel = 0.0f;
    const int relRead = RelationsMineTowards(originSlot, &rel);
    const int v = pingwire::ReceiveVerdict(originSlot, me, TeamSameAnyThread(me, originSlot), StoreRosterSlotInWorld(originSlot), relRead, rel);
    if (v != pingwire::kTake)
    {
        if (v > 0 && v < pingwire::kVerdicts) ++g_inDropped[v];
        if (v == pingwire::kDropUnread && g_inDropped[v] > 5) return;   /* review fold 5: logged at most 5 times, counted always */
        Log(std::string("[PING] dropped (") + pingwire::VerdictWord(v) + ") " + pingwire::KindWord(m.kind) + " #" + N(m.count) + " from " + Who(originSlot)
            + (relRead == 1 ? " (relation " + F(rel) + ")" : std::string()));
        return;
    }
    if (!pingwire::RateTake(&g_recvRate[originSlot], (unsigned long)::GetTickCount(), pingwire::kRecvRateMax))
    {
        ++g_inCapped;
        Log("[PING] dropped (more than 4 pings in 5 s from one player) " + std::string(pingwire::KindWord(m.kind)) + " #" + N(m.count) + " from " + Who(originSlot) + " (inCapped " + N(g_inCapped) + ")");
        return;
    }
    ++g_in;
    Log("[PING] received " + std::string(pingwire::KindWord(m.kind)) + " #" + N(m.count) + " from " + Who(originSlot) + " at " + F(m.x) + "," + F(m.y) + "," + F(m.z)
        + (g_live.count(originSlot) != 0 ? " - replaces that player's earlier ping" : std::string()));
    Keep(originSlot, m, false);
}
void PingLiveIn(int originSlot, const void* p, unsigned n)
{
    if (g_off != 0) return;
    try { PingLiveInBody(originSlot, p, n); }
    catch (...) { ++g_throws; DebugLog("[PING] a C++ exception reading a ping; it is dropped"); }
}

void PingWorldTeardown()
{
    if (g_off == 0 && DropAllPod() != 1) g_off = 1;
    g_live.clear(); g_rule.clear(); g_doomed.clear(); g_recvRate.clear();
    g_mapNow = MapNow(); g_mapOpen = false;
    g_pingPressed = 0;
}

void InstallPing()
{
    if (!KeyRowsOk()) { g_keyHooks = -1; ErrorLog("[PING] an InputHandler / Controls row is not in the address table - the Ping key and its Controls row are NOT set up"); return; }
    const bool a = coop::AddHook((void*)AddrAbs(kIhInitRva), (void*)&detour_ihInit, (void**)&orig_ihInit) == coop::SUCCESS;
    const bool b = a && coop::AddHook((void*)AddrAbs(kDpSpacerRva), (void*)&detour_dpSpacer, (void**)&orig_dpSpacer) == coop::SUCCESS;
    const bool c = b && coop::AddHook((void*)AddrAbs(kIhKeyDownRva), (void*)&detour_ihKeyDown, (void**)&orig_ihKeyDown) == coop::SUCCESS;
    g_keyHooks = (a && b && c) ? 1 : -1;
    if (g_keyHooks == 1)
    {
        DebugLog("[PING] InputHandler::initialise post-hook, the key-down hook and the Controls list's spacer hook installed - the Ping key is Kenshi's own rebindable action");
        if (g_off == 0)
        {
            try { ActionEarlyAdd(); }   /* fold 3: before the game's own controls.cfg load */
            catch (...) { ++g_throws; }
        }
    }
    else ErrorLog("[PING] the Ping key's hooks were NOT installed (initialise " + std::string(a ? "in" : "failed") + ", spacer " + (b ? "in" : "failed") + ", key-down " + (c ? "in" : "failed") + ") - the Ping action and its Controls row are not added; the Ping key does nothing");
}

void PingTitleTick()
{
    g_pingPressed = 0;   /* fold 1: a press on the title screen (no world tick runs there) is not kept for the first frame of play */
    if (g_actionSetUp) return;
    g_actionSetUp = true;
    if (g_keyHooks != 1 || g_off != 0) return;
    try { ActionSetUp(true, "the game's start"); }
    catch (...) { ++g_throws; }
}

std::string PingTestCommand(const std::string& args)
{
#ifdef SW_DEV_BUILD
    std::istringstream is(args);
    std::string verb;
    is >> verb;
    if (g_off != 0) return "error pingtest: the pings are off after a fault";
    if (verb == "show") { const std::string s = ShowLine(); Log("[PING] pingtest show: " + s); return "ok pingtest show " + s; }
    if (verb == "press")
    {   /* G's own road after the key read (Press: the typing guard, the window-under-the-point check, PickMap / PickWorld, Fire),
           at the mouse - or at <fx> <fy>, a place on the view in fractions 0..1, standing in for the mouse */
        double fx = 0, fy = 0;
        std::string rest; std::getline(is, rest);
        std::istringstream ps(rest); int n = 0; std::string tok;
        while (ps >> tok) ++n;   /* re-check fold: one number alone is an error, not the mouse */
        if (n != 0 && n != 2) return "error pingtest: usage press [<fx 0-1> <fy 0-1>]";
        std::istringstream vs(rest); vs >> fx >> fy;
        const bool given = n == 2 && !vs.fail();
        if (n == 2 && vs.fail()) return "error pingtest: usage press [<fx 0-1> <fy 0-1>]";
        if (given && (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0)) return "error pingtest: usage press [<fx 0-1> <fy 0-1>]";
        Press("pingtest press", given, (float)fx, (float)fy);
        return "ok pingtest press " + std::string(g_mapOpen ? "map" : "world") + (given ? " at the given place" : " at the mouse") + " (see the [PING] line; " + ShowLine() + ")";
    }
    int count = 1;
    bool gate = true;
    std::string kind;
    double x = 0, z = 0;
    if (verb == "burst")
    {
        is >> count >> kind >> x >> z;
        if (is.fail() || count < 1 || count > 8) return "error pingtest: usage burst <n 1-8> world|map <x> <z> [nogate]";
        std::string extra;
        if (is >> extra) { if (extra != "nogate") return "error pingtest: usage burst <n 1-8> world|map <x> <z> [nogate]"; gate = false; }
    }
    else if (verb == "world" || verb == "map")
    {
        kind = verb;
        is >> x >> z;
        if (is.fail()) return "error pingtest: usage " + verb + " <x> <z>";
    }
    else return "error pingtest: usage world <x> <z> | map <x> <z> | burst <n> world|map <x> <z> [nogate] | press | show";
    if (kind != "world" && kind != "map") return "error pingtest: the kind is world or map";
    pingwire::Msg m;
    m.kind = kind == "map" ? pingwire::kMap : pingwire::kWorld;
    m.x = (float)x; m.z = (float)z; m.y = 0.0f;
    if (m.kind == pingwire::kWorld)
    {
        float h = 0.0f;
        if (GroundTerrainHeightAt(m.x, m.z, &h) != 1) return "error pingtest: no terrain height at x,z";
        m.y = h;
    }
    if (!pingwire::MsgOk(m)) return "error pingtest: x,z is outside the world";
    int fired = 0;
    for (int i = 0; i < count; ++i) fired += Fire(m, gate, "pingtest");
    return "ok pingtest " + verb + " fired " + N(fired) + " of " + N(count) + " (" + ShowLine() + ")";
#else
    (void)args;
    return "error pingtest: a TEST-ONLY lever of the DEV build";
#endif
}

}   /* namespace coop */
