// tags.cpp - the name tags over OTHER players' characters.
//
// WHAT IT SAYS.  Two lines (src/common/nametag.h): the PLAYER's name as they typed it when joining (the world server's PLAYERS
// roster, by the slot of the stand-in faction the copy stands in), and below it that player's faction name, smaller and dimmer.
// With no roster name for that slot the faction name takes line 1 and line 2 is left out.  Both lines are coloured by how this
// game's player faction and that player's faction stand - the worse of the two directions: green friendly, yellow neutral,
// red hostile.  A player who shares this game's player's faction (a team on the world server's table, team.cpp) is team
// blue instead, and line 2 of every player in a team names the team - its founder's faction name (decisions 478 / 479).
// Each line has a black outline all round, so it reads on bright ground as well as dark.  This game's own
// characters keep the game's own name tags; nothing here touches them.
//
// WHAT IT DOES.  Each copy of another player's own character (a copy standing in a player's stand-in faction) gets two MyGUI
// TextBoxes, created once when the copy is created and reused until the copy is removed.  Each line also has four black
// copies of itself (its outline), moved 1 px up, down, left and right and drawn behind it; every caption, size, position
// and visibility change made to a line is made to its four copies in the same call.  Every in-game frame the
// labels in THIS registry - and nothing else in the world - are projected from the character's position with the
// engine's own projection (UtilityT::projectToScreen, the call the game's floating labels use) and moved there.
// A label is hidden when its character is behind the camera or off screen, beyond the engine's own name-tag
// distance, while the game's interface is hidden, while the world is loading, and while the player has turned the
// labels off (the Insert key, or the `tags on|off` verb).  Nothing is persisted: ON at every start.
//
// WHEN THE WORDS AND COLOUR CHANGE.  Every label rebuilds its lines and colour on the frame after TagsCaptionsDirty: a new
// PLAYERS roster (store.cpp), a standing between this game's player faction and a stand-in changing either way
// (relations.cpp), a stand-in created or renamed (playerfaction.cpp), a new team table or its copy cleared (team.cpp).  An
// event, not a timer.
//
// WHY THE ENGINE'S OWN NAME TAG IS NOT USED (nameplates.md s1/s5): it only says the character's name, cannot be
// recoloured, and the engine gives it only to player-faction characters - a stand-in faction is not one.  The face is the
// engine tag's own (Kenshi_CharacterNamePanel.layout): skin Kenshi_TextboxStandardText_Large.  The engine tag's soft grey text
// shadow is not used: the black outline takes its place.  The outline is four black copies because this MyGUI (3.2.3) has
// no outline setting for text - only a one-direction shadow (Read, the vendored headers).
//
// THREADS.  Every MyGUI call is on the main thread: TagsTick rides detour_mainLoop, TagsTitleTick the title pump,
// TagsForgetUid is called from RemoveLocalCopy (main thread - it edits spawn.cpp's main-thread tables).  The two
// any-thread entry points only raise interlocked flags.
//
// SEH.  Engine reads are in small POD functions with their own __try.  The tick body (C++ objects, MyGUI throws)
// runs inside TagsTickSeh's __try; a MEMORY fault turns the labels off for the process (faults=1 on the report).
//
// WIDGET POINTERS ARE KEPT ACROSS FRAMES, unlike ui.cpp's title widgets: these are created at the Gui ROOT on a
// layer, not inside a screen the game rebuilds, so nothing but this file destroys them (Inferred).  Every label is
// destroyed at world teardown and at the title screen, so none outlives the world it was made in, and a destroy
// first confirms the pointer is still the widget MyGUI knows under that name.

#include "tags.h"
#include "spawn.h"          // FindSpawned / IsRetiredObject / SafeReadPosition
#include "replicate.h"      // PeerFactionPod - "is this copy in the peer faction", the existing test (E25)
#include "ai_spike.h"       // GetTarget - this game's watched player, whose own engine name tag gives the lift
#include "store.h"          // EngineWritesBlocked - load / teardown; StoreRosterNameOf - the player's name
#include "playerfaction.h"  // StandInSlotOf / StandInDisplayName / LocalPlayerFaction
#include "relations.h"      // RelationsTagLevel - the colour
#include "team.h"           // TeamTagFor - a teammate's team blue, and the team's name on line 2 (T-546 step 4)
#include "addresses.h"
#include "../common/nametag.h"   // the two lines and the colour - pure, swept by the offline suite
#include "../common/teameffect.h"   // TagColourClass - team blue or the standing's colour (T-546 step 4)

#include "coop_log.h"
#include "game/Character.h"
#include "game/UtilityT.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <map>
#include <string>
#include <sstream>
#include <locale>
#include <cstring>

namespace coop {

/* The address table fills these (Steam_1.0.65).  Both are `var` entries - writable globals, not verifiable at start.
   InterfaceShown 0x2132A80: the byte every engine name tag and floating bar is gated on (0x5C8120 / 0x5CB880, Read);
   "the interface is shown" is Inferred (nameplates.md s1) - [TAGS] REPORT prints it as uiShown= so a run can see it.
   UtilityTObject 0x2134B10: THE UtilityT object itself, not a pointer - 0x787E70 memcpy's the camera's view matrix
   into its first 64 bytes every frame (Read, build/decomp_787e70.txt:36), and 0x36DCA0 calls its methods on &it.
   That matrix belongs to the scene's SHIFTED space: the engine draws everything moved by the scene manager's relative
   origin, which it keeps near the camera (0x82AAF0 moves it when the camera wanders far), so a world point has the
   origin taken off before the matrix applies.
   RenderHolderPtr 0x21322B8 (also a `var`): a pointer to the engine object that holds the camera (+0x58, the one 0x787E70
   reads the matrix from) and the scene manager (+0x60).  The engine's own worldToScreen (0x9B12C0) does exactly this:
   getRelativeOrigin on *(*0x21322B8 + 0x60), the world point minus it, then the matrix at 0x2134B10 (Read in the exe's
   bytes and import slots). */
unsigned long long kInterfaceShownRva = 0; static AddrReg kInterfaceShownRva_reg("InterfaceShown", &kInterfaceShownRva);   /* Steam_1.0.65 0x2132A80 */
unsigned long long kUtilityTObjectRva = 0; static AddrReg kUtilityTObjectRva_reg("UtilityTObject", &kUtilityTObjectRva);   /* Steam_1.0.65 0x2134B10 */
unsigned long long kRenderHolderPtrRva = 0; static AddrReg kRenderHolderPtrRva_reg("RenderHolderPtr", &kRenderHolderPtrRva);   /* Steam_1.0.65 0x21322B8 */

namespace {

/* The engine's own name-tag distance: CharacterNameTag::update 0x6E7960 hides a tag whose SQUARED distance from the
   camera exceeds 1.2e8 (Read, nameplates.md s1).  Measured here from the camera's eye (the view-space origin); the engine
   measures from its camera node (0x21322C0 +0x58) - whether that node sits at the eye is not traced. */
const float kMaxDist2 = 1.2e8f;
/* Height above the feet when the watched player's own engine tag cannot be read (Guess - `tags lift <n>` moves it). */
const float kLiftFallback = 20.0f;
/* The toggle key.  Insert is bound to nothing in Kenshi's controls.cfg (it binds F1-F9, Shift+F12, Ctrl+Shift+F11,
   Home/End, PgUp/PgDn, Delete...), is not a Windows system key (F10 is: it enters menu mode) and is not Steam's
   screenshot key (F12). */
const int kToggleVk = VK_INSERT;
const char* const kLayer = "Dialog";                         // the layer the engine's own screen labels use (Read)
const char* const kSkinName    = "Kenshi_TextboxStandardText_Large";   // line 1: the engine's own name tag skin (Exo2 SemiBold 24)
const char* const kSkinFaction = "Kenshi_TextboxStandardText_Large";   // line 2: the same skin and size as line 1
const char* const kFontFaction = 0;                                    // line 2: the skin's own font, as line 1
const float kFactionAlpha = 0.7f;                                      // line 2 is dimmer than line 1
const int kLogChanges = 20;      // label rebuilds that changed the words or colour, logged one line each up to this many
const int kLogCreations = 10;
const int kCreateTries   = 3;    // a refused create is not retried at frame rate for ever

enum Hide { kShown = 0, kHideOff, kHideUi, kHideBlocked, kHideNotPeer, kHideOffscreen, kHideRange, kHideProject, kHideNew, kHideWithdrawn };

struct Label
{
    MyGUI::TextBox* w;      // line 1: the player's name
    MyGUI::TextBox* w2;     // line 2: the player's faction name (hidden when the line is empty)
    MyGUI::TextBox* e[nametag::kOutlineCopies];    // line 1's outline: black copies of line 1, drawn behind it
    MyGUI::TextBox* e2[nametag::kOutlineCopies];   // line 2's outline
    std::string widgetName, widgetName2;
    std::string caption, caption2;
    long gen;          // g_captionGen when the lines were last built
    int hide;          // current Hide reason (kShown = on screen)
    int tw, th;        // line 1's size, from its text size
    int tw2, th2;      // line 2's size; 0 x 0 while line 2 is empty
    int level;         // nametag::Level the colour shows; kUnknown before the first build
    int teammate;      // 1 = the colour is team blue (that player shares this player's team), whatever `level` reads
    int createFails;   // createWidgetT refusals for this label; it stops asking after kCreateTries
};

std::map<unsigned int, Label> g_labels;   // MAIN THREAD ONLY - the registry; the per-frame loop walks this and nothing else
volatile LONG g_captionGen   = 1;
volatile LONG g_dropAll      = 0;
bool  g_on        = true;
bool  g_keyWasDown = false;
bool  g_dead      = false;    // a memory fault turned the labels off for the process
float g_liftSet   = 0.0f;     // `tags lift` override; 0 = automatic
float g_liftAuto  = 0.0f;     // learned from the watched player's own engine tag; 0 = not yet
int   g_liftTries = 0;

/* hidden* COUNT PER-LABEL TRANSITIONS, not frames: a label that changes to hidden-for-this-reason counts 1, however many
   frames it then stays hidden - five labels hidden together by the interface-hide key add 5 to hiddenUi. Turning the
   labels off has no hidden* counter of its own; toggledOff counts the toggle itself. */
long long g_posReadFail = 0, g_viewReadFail = 0, g_originReadFail = 0, g_hiddenWithdrawn = 0, g_hideAllPasses = 0;
long long g_created = 0, g_destroyed = 0, g_hiddenOffscreen = 0, g_hiddenRange = 0, g_hiddenUi = 0, g_hiddenBlocked = 0,
          g_hiddenNotPeer = 0, g_toggledOff = 0, g_toggledOn = 0, g_keyToggles = 0, g_projectFail = 0, g_createFail = 0,
          g_notRegistered = 0, g_captionSets = 0, g_faults = 0, g_throws = 0, g_destroyGone = 0, g_destroyStale = 0,
          g_edgesMade = 0, g_edgesDestroyed = 0;   // outline copies made / destroyed (eight per label)
long long g_byRoster = 0, g_byFaction = 0, g_recolours = 0, g_changesLogged = 0;   // builds that named the player from the roster / fell back to the faction name; colour changes
long long g_levelBuilds[3] = { 0, 0, 0 };                                            // builds by colour: friendly, neutral, hostile
long long g_teamBuilds = 0, g_teamLineBuilds = 0;                                    // builds in team blue; builds whose line 2 named a team
int g_visibleNow = 0;

std::string N(long long v) { std::ostringstream s; s.imbue(std::locale::classic()); s << v; return s.str(); }
std::string Fl(float v) { std::ostringstream s; s.imbue(std::locale::classic()); s.setf(std::ios::fixed); s.precision(2); s << v; return s.str(); }

uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

// ---- engine reads (POD, each under its own __try) --------------------------------------------------------------------
int ReadUiShownPod()
{
    if (kInterfaceShownRva == 0) return -1;
    __try { return *(volatile unsigned char*)(Base() + kInterfaceShownRva) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* Ogre::SceneManager::getRelativeOrigin returns a Vector3 BY VALUE: `this` in RCX, the result slot in RDX, no XMM
   register.  Found by name in OgreMain_x64.dll, as appearance.cpp finds its Ogre call. */
typedef float* (*GetRelativeOriginFn)(const void* sceneManager, float* out);
const char kOgreDll[] = "OgreMain_x64.dll";
const char kGetRelativeOriginSym[] = "?getRelativeOrigin@SceneManager@Ogre@@QEBA?AVVector3@2@XZ";
const size_t kHolderSceneManagerOff = 0x60;   // RenderHolderPtr's object: its Ogre::SceneManager* (Read, 0x9B12C0 and 0x82AAF0)

/* Resolved once; 0 when the export is missing, logged once - every label then stays hidden (originReadFail). */
GetRelativeOriginFn RelativeOriginFn()
{
    static bool s_tried = false;
    static GetRelativeOriginFn s_fn = 0;
    if (s_tried) return s_fn;
    s_tried = true;
    HMODULE h = ::GetModuleHandleA(kOgreDll);
    if (h != 0) s_fn = (GetRelativeOriginFn)::GetProcAddress(h, kGetRelativeOriginSym);
    if (s_fn == 0)
        DebugLog(std::string("[TAGS] Ogre::SceneManager::getRelativeOrigin did not resolve (")
                 + (h == 0 ? "OgreMain_x64.dll is not loaded" : "the export is not in OgreMain_x64.dll")
                 + ") - every label stays hidden (originReadFail)");
    return s_fn;
}

/* The scene's relative origin this frame, read the way the engine's own worldToScreen reads it (see RenderHolderPtr
   above).  false when the row, either pointer or the export is missing, or the read faults. */
bool RelativeOriginPod(GetRelativeOriginFn fn, float* ox, float* oy, float* oz)
{
    if (fn == 0 || kRenderHolderPtrRva == 0) return false;
    __try
    {
        const char* holder = *(const char* const*)(Base() + kRenderHolderPtrRva);
        if (holder == 0) return false;
        const void* sm = *(const void* const*)(holder + kHolderSceneManagerOff);
        if (sm == 0) return false;
        float o[3] = { 0.0f, 0.0f, 0.0f };
        fn(sm, o);
        *ox = o[0]; *oy = o[1]; *oz = o[2];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

/* The point in VIEW space, from a point already in the scene's shifted space (the world point minus the relative
   origin): Ogre::Matrix4 is row-major, v' = M * (x,y,z,1).  The camera looks down -Z. */
bool ViewSpacePod(float x, float y, float z, float* vx, float* vy, float* vz)
{
    if (kUtilityTObjectRva == 0) return false;
    __try
    {
        const float* m = (const float*)(Base() + kUtilityTObjectRva);
        *vx = m[0] * x + m[1] * y + m[2]  * z + m[3];
        *vy = m[4] * x + m[5] * y + m[6]  * z + m[7];
        *vz = m[8] * x + m[9] * y + m[10] * z + m[11];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

/* 1 on screen, 0 the engine says not, -1 faulted.  Takes the WORLD point: the engine's call takes the relative origin
   off by itself (0x9B12C0, Read). */
int ProjectPod(float x, float y, float z, float* sx, float* sy)
{
    if (kUtilityTObjectRva == 0) return -1;
    __try
    {
        UtilityT* u = (UtilityT*)(Base() + kUtilityTObjectRva);
        const Ogre::Vector3 p(x, y, z);
        return u->projectToScreen(p, *sx, *sy) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* The engine's own head height for the WATCHED PLAYER: its CharacterNameTag (Character +0x638, Read) holds the offset
   0x6E8BA0 computed at +0x48 as (0, h, 0) (Inferred from build/decomp_6e8ba0.txt:89).  0 when unreadable. */
float OwnTagLiftPod(::Character* c)
{
    if (c == 0) return 0.0f;
    __try
    {
        const char* tag = *(const char* const*)((const char*)c + 0x638);
        if (tag == 0) return 0.0f;
        const float h = *(const float*)(tag + 0x48 + 4);
        return (h > 0.1f && h < 1000.0f) ? h : 0.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
}

float Lift()
{
    if (g_liftSet > 0.0f) return g_liftSet;
    if (g_liftAuto <= 0.0f && g_liftTries < 600)   // the watched player may not have its tag yet in the first frames
    {
        ++g_liftTries;
        const float h = OwnTagLiftPod(GetTarget());
        if (h > 0.0f)
        {
            g_liftAuto = h;
            DebugLog("[TAGS] lift " + Fl(h) + " from the watched player's own engine name tag");
        }
        else if (g_liftTries == 600) DebugLog("[TAGS] the watched player's engine name tag gave no height - lift stays " + Fl(kLiftFallback) + " (Guess)");
    }
    return g_liftAuto > 0.0f ? g_liftAuto : kLiftFallback;
}

// ---- widgets ---------------------------------------------------------------------------------------------------------
/* The widget name of a line's outline copy k: the line's name, "Edge", k (no '_'). */
std::string EdgeName(const std::string& line, int k) { return line + "Edge" + N(k); }

void DestroyOne(MyGUI::Gui* gui, MyGUI::TextBox*& w, const std::string& name, long long* counter)
{
    if (w != 0 && gui != 0)
    {
        /* Only a widget MyGUI still knows under our name, at our address - never a freed one. */
        MyGUI::Widget* found = gui->findWidgetT(name, false);
        if (found == w) { gui->destroyWidget(w); ++*counter; }
        else ++g_destroyStale;
    }
    else if (w != 0) ++g_destroyStale;
    w = 0;
}
void DestroyLabel(MyGUI::Gui* gui, Label& L)
{
    for (int k = 0; k < nametag::kOutlineCopies; ++k)
    {
        DestroyOne(gui, L.e[k], EdgeName(L.widgetName, k), &g_edgesDestroyed);
        DestroyOne(gui, L.e2[k], EdgeName(L.widgetName2, k), &g_edgesDestroyed);
    }
    DestroyOne(gui, L.w, L.widgetName, &g_destroyed);
    DestroyOne(gui, L.w2, L.widgetName2, &g_destroyed);
}

/* A line and its outline, shown or hidden together. */
void ShowLine(MyGUI::TextBox* w, MyGUI::TextBox* const* e, bool show)
{
    for (int k = 0; k < nametag::kOutlineCopies; ++k) if (e[k] != 0) e[k]->setVisible(show);
    if (w != 0) w->setVisible(show);
}

/* A line at (x, y) and each outline copy at its 1 px offset from it. */
void PlaceLine(MyGUI::TextBox* w, MyGUI::TextBox* const* e, int x, int y)
{
    for (int k = 0; k < nametag::kOutlineCopies; ++k)
    {
        if (e[k] == 0) continue;
        int dx = 0, dy = 0;
        nametag::OutlineOffset(k, &dx, &dy);
        e[k]->setPosition(x + dx, y + dy);
    }
    w->setPosition(x, y);
}

void DestroyAll()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    for (std::map<unsigned int, Label>::iterator it = g_labels.begin(); it != g_labels.end(); ++it) DestroyLabel(gui, it->second);
    g_labels.clear();
    g_visibleNow = 0;
}

void SetHidden(Label& L, int why)
{
    if (L.hide == why) return;
    if (L.hide == kShown && g_visibleNow > 0) --g_visibleNow;
    L.hide = why;
    switch (why)
    {
    case kHideOffscreen: ++g_hiddenOffscreen; break;
    case kHideRange:     ++g_hiddenRange; break;
    case kHideUi:        ++g_hiddenUi; break;
    case kHideBlocked:   ++g_hiddenBlocked; break;
    case kHideNotPeer:   ++g_hiddenNotPeer; break;
    case kHideWithdrawn: ++g_hiddenWithdrawn; break;
    default: break;
    }
    ShowLine(L.w, L.e, false);
    ShowLine(L.w2, L.e2, false);
}

::Faction* TagFactionPod(::Character* c)   /* stand1: the copy's own faction, fault-guarded */
{
    __try { return c->getOwnerFactionDirect(); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
const char* LevelWord(int level) { return level == nametag::kFriendly ? "friendly" : level == nametag::kHostile ? "hostile" : "neutral"; }
const char* ColourWord(int level, int teammate) { return teammate ? "team" : LevelWord(level); }

/* A line's words and size, and its outline copies' with it. */
void SetLine(MyGUI::TextBox* w, MyGUI::TextBox* const* e, const std::string& text, int* tw, int* th)
{
    const MyGUI::UString u(text.c_str());
    w->setCaption(u);
    for (int k = 0; k < nametag::kOutlineCopies; ++k) if (e[k] != 0) e[k]->setCaption(u);
    if (text.empty()) { *tw = 0; *th = 0; return; }
    const MyGUI::IntSize ts = w->getTextSize();
    *tw = ts.width + 6; *th = ts.height + 2;
    w->setSize(*tw, *th);
    for (int k = 0; k < nametag::kOutlineCopies; ++k) if (e[k] != 0) e[k]->setSize(*tw, *th);
}

/* Both lines and the colour, from the copy's stand-in faction: its slot names the player on the roster, its displayed name is
   line 2, and the standing between this game's player faction and it is the colour.  `mine` is this game's player faction,
   found once per frame by the caller (MineOnce).  MAIN THREAD. */
void BuildCaption(unsigned int uid, ::Character* c, Label& L, ::Faction* mine, bool logCreated)
{
    const long gen = g_captionGen;   /* taken first, so a refresh signalled while this runs is not lost */
    ::Faction* f = TagFactionPod(c);
    const int slot = StandInSlotOf(f);
    std::string player;
    const int named = StoreRosterNameOf(slot, &player);
    const std::string fac = StandInDisplayName(f);
    std::string facLine; bool mate = false;
    TeamTagFor(slot, fac, &facLine, &mate);   /* a player in a team: the team's name; a teammate: team blue */
    const int teammate = swteam::TagColourClass(mate) == swteam::kTagTeam ? 1 : 0;
    const nametag::Caption cap = nametag::CaptionFor(named == 1 ? player : std::string(), facLine);
    int reads = 0;
    const int level = RelationsTagLevel(mine, f, &reads);
    /* the refresh counts as done only when the colour came from a real read; otherwise the next frame tries again */
    if (mine != 0 && reads > 0) L.gen = gen;
    if (named == 1 && !nametag::Trimmed(player).empty()) ++g_byRoster; else ++g_byFaction;
    if (teammate) ++g_teamBuilds;
    else if (level >= nametag::kFriendly && level <= nametag::kHostile) ++g_levelBuilds[level];
    if (facLine != fac) ++g_teamLineBuilds;
    const bool textSame = cap.line1 == L.caption && cap.line2 == L.caption2, levelSame = teammate == L.teammate && (teammate != 0 || level == L.level);
    if (textSame && levelSame) return;
    if (logCreated)
        DebugLog("[TAGS] created uid=" + N(uid) + " slot=" + N(slot) + " player='" + player + "' ("
                 + (named == 1 ? "roster" : named == 0 ? "not on the roster" : "no roster") + ") faction='" + fac
                 + "' lines='" + cap.line1 + "' / '" + cap.line2 + "' colour=" + ColourWord(level, teammate));
    else if (L.level != nametag::kUnknown && (g_changesLogged < kLogChanges || teammate != L.teammate))   /* into or out of team blue: always said */
    {
        ++g_changesLogged;
        DebugLog("[TAGS] uid=" + N(uid) + " slot=" + N(slot) + " now '" + cap.line1 + "' / '" + cap.line2 + "' colour=" + ColourWord(level, teammate)
                 + " (was '" + L.caption + "' / '" + L.caption2 + "' " + ColourWord(L.level, L.teammate) + ")");
    }
    if (!levelSame)
    {
        if (L.level != nametag::kUnknown) ++g_recolours;
        L.level = level; L.teammate = teammate;
        const nametag::Rgb rgb = nametag::TagColour(level, teammate != 0);
        const MyGUI::Colour col(rgb.r, rgb.g, rgb.b, 1.0f);
        if (L.w != 0) L.w->setTextColour(col);     // the outline copies stay black
        if (L.w2 != 0) L.w2->setTextColour(col);
    }
    if (!textSame)
    {
        L.caption = cap.line1; L.caption2 = cap.line2;
        if (L.w != 0) SetLine(L.w, L.e, L.caption, &L.tw, &L.th);
        if (L.w2 != 0)
        {
            SetLine(L.w2, L.e2, L.caption2, &L.tw2, &L.th2);
            if (L.caption2.empty()) ShowLine(L.w2, L.e2, false);
            else if (L.hide == kShown) ShowLine(L.w2, L.e2, true);
        }
        ++g_captionSets;
    }
}

/* One TextBox at the Gui root on kLayer, hidden, with no text shadow (the outline does that work).  font 0 keeps the
   skin's font; alpha 1 keeps it opaque. */
MyGUI::TextBox* MakeLine(MyGUI::Gui* gui, const char* skin, const char* font, float alpha, const std::string& name)
{
    MyGUI::Widget* raw = gui->createWidgetT("TextBox", skin, MyGUI::IntCoord(0, 0, 8, 8), MyGUI::Align::Default, kLayer, name);
    if (raw == 0) return 0;
    MyGUI::TextBox* t = raw->castType<MyGUI::TextBox>(false);
    if (t == 0) { gui->destroyWidget(raw); return 0; }
    t->setNeedMouseFocus(false);   // a label must never take a click meant for the world
    t->setVisible(false);
    t->setTextShadow(false);
    if (font != 0) t->setFontName(font);
    if (alpha < 1.0f) t->setAlpha(alpha);
    return t;
}

/* Destroys a line and its outline copies just made (none of them registered anywhere yet). */
void DropLine(MyGUI::Gui* gui, MyGUI::TextBox*& w, MyGUI::TextBox** e)
{
    for (int k = 0; k < nametag::kOutlineCopies; ++k) if (e[k] != 0) { gui->destroyWidget(e[k]); e[k] = 0; }
    if (w != 0) { gui->destroyWidget(w); w = 0; }
}

/* One line with its outline: the four black copies are created first and the coloured line last, so the coloured line is
   drawn over them - root widgets on one layer draw in the order they were created and a label never takes focus, so
   nothing raises one over another (Inferred from MyGUI 3.2's layer code).  On a refusal everything it made is destroyed
   and it returns 0 with e[] all 0. */
MyGUI::TextBox* MakeOutlined(MyGUI::Gui* gui, const char* skin, const char* font, float alpha, const std::string& name,
                             MyGUI::TextBox** e)
{
    MyGUI::TextBox* w = 0;
    for (int k = 0; k < nametag::kOutlineCopies; ++k) e[k] = 0;
    for (int k = 0; k < nametag::kOutlineCopies; ++k)
    {
        e[k] = MakeLine(gui, skin, font, alpha, EdgeName(name, k));
        if (e[k] == 0) { DropLine(gui, w, e); return 0; }
        e[k]->setTextColour(MyGUI::Colour(0.0f, 0.0f, 0.0f, 1.0f));
    }
    w = MakeLine(gui, skin, font, alpha, name);
    if (w == 0) { DropLine(gui, w, e); return 0; }
    g_edgesMade += nametag::kOutlineCopies;
    return w;
}

/* This game's player faction, looked up at most once per frame and only when a label needs it (a walk of the faction list). */
struct MineOnce
{
    bool known; ::Faction* f;
    MineOnce() : known(false), f(0) {}
    ::Faction* Get() { if (!known) { known = true; f = LocalPlayerFaction(); } return f; }
};

bool CreateWidget(unsigned int uid, ::Character* c, Label& L, MineOnce& mine)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) { ++g_createFail; return false; }
    L.widgetName = "PlayerTagName" + N(uid);        // no '_', like every widget name in ui.cpp
    L.widgetName2 = "PlayerTagFaction" + N(uid);
    MyGUI::TextBox* t = MakeOutlined(gui, kSkinName, 0, 1.0f, L.widgetName, L.e);
    if (t == 0) { ++g_createFail; return false; }
    /* line 2's outline takes line 2's alpha with it, so the whole line stays the dimmer one */
    MyGUI::TextBox* t2 = MakeOutlined(gui, kSkinFaction, kFontFaction, kFactionAlpha, L.widgetName2, L.e2);
    if (t2 == 0) { g_edgesMade -= nametag::kOutlineCopies; DropLine(gui, t, L.e); ++g_createFail; return false; }
    L.w = t; L.w2 = t2;
    L.caption.clear(); L.caption2.clear(); L.level = nametag::kUnknown; L.teammate = 0;
    L.tw = L.th = L.tw2 = L.th2 = 0;
    ++g_created;
    BuildCaption(uid, c, L, mine.Get(), g_created <= kLogCreations);
    return true;
}

void ProcessDropAll()
{
    if (::InterlockedExchange(&g_dropAll, 0) != 0) DestroyAll();
}

void PollKey()
{
    const bool down = (::GetAsyncKeyState(kToggleVk) & 0x8000) != 0;
    const bool edge = down && !g_keyWasDown;
    g_keyWasDown = down;
    if (!edge) return;
    /* Our window only - Insert pressed in another program is not for us. */
    HWND fg = ::GetForegroundWindow();
    DWORD pid = 0;
    if (fg == 0 || (::GetWindowThreadProcessId(fg, &pid), pid) != ::GetCurrentProcessId()) return;
    /* The same guard the engine's own hotkeys obey (F707): not while the player is typing into a text box. */
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0 && im->isFocusKey()) return;
    ++g_keyToggles;
    TagsSetOn(!g_on);
}

void TickBody()
{
    ProcessDropAll();
    PollKey();
    if (g_labels.empty()) return;

    int global = kShown;
    const int ui = ReadUiShownPod();
    if (!g_on) global = kHideOff;
    else if (EngineWritesBlocked()) global = kHideBlocked;
    else if (ui == 0) global = kHideUi;

    int viewW = 0, viewH = 0;
    if (global == kShown)
    {
        MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
        if (rm != 0) { const MyGUI::IntSize& vs = rm->getViewSize(); viewW = vs.width; viewH = vs.height; }
    }
    const float lift = (global == kShown) ? Lift() : 0.0f;
    /* The relative origin, read once per frame and only while labels may show; each label takes it off its point. */
    float ox = 0, oy = 0, oz = 0;
    const bool originOk = (global == kShown) && RelativeOriginPod(RelativeOriginFn(), &ox, &oy, &oz);
    MineOnce mine;

    for (std::map<unsigned int, Label>::iterator it = g_labels.begin(); it != g_labels.end(); )
    {
        const unsigned int uid = it->first;
        Label& L = it->second;
        ::Character* c = FindSpawned(uid);
        if (c == 0 && SpawnedRawObject(uid) != 0)
        {
            /* review-tags1 2c: the uid is still in the copy table but does not resolve - WITHDRAWN (streamed out, retired
               by P034). It may come back through MirrorRestore without ApplyRemoteSpawn, so the entry and its widget are
               KEPT and only hidden; the next frame it resolves again, it is shown again (names1's D2 fold, the same idea). */
            ++it;
            SetHidden(L, kHideWithdrawn);
            continue;
        }
        if (c == 0)
        {
            /* The uid has left the copy table altogether by a path other than RemoveLocalCopy (retired as stale). */
            DestroyLabel(MyGUI::Gui::getInstancePtr(), L);
            ++g_destroyGone;
            if (L.hide == kShown && g_visibleNow > 0) --g_visibleNow;
            g_labels.erase(it++);
            continue;
        }
        ++it;
        if (global != kShown) { SetHidden(L, global); continue; }
        if (PeerFactionPod(c) != 1) { SetHidden(L, kHideNotPeer); continue; }
        if (L.w == 0)
        {
            if (L.createFails >= kCreateTries) { SetHidden(L, kHideProject); continue; }
            if (!CreateWidget(uid, c, L, mine)) { ++L.createFails; SetHidden(L, kHideProject); continue; }
        }
        else if (L.gen != g_captionGen) BuildCaption(uid, c, L, mine.Get(), false);

        Ogre::Vector3 pos;
        if (!SafeReadPosition(c, &pos)) { ++g_posReadFail; SetHidden(L, kHideProject); continue; }
        const float hx = pos.x, hy = pos.y + lift, hz = pos.z;
        float vx = 0, vy = 0, vz = 0;
        if (!originOk) { ++g_originReadFail; SetHidden(L, kHideProject); continue; }
        if (!ViewSpacePod(hx - ox, hy - oy, hz - oz, &vx, &vy, &vz)) { ++g_viewReadFail; SetHidden(L, kHideProject); continue; }
        if (vz >= 0.0f) { SetHidden(L, kHideOffscreen); continue; }                  // behind the camera
        if (vx * vx + vy * vy + vz * vz > kMaxDist2) { SetHidden(L, kHideRange); continue; }
        float sx = 0, sy = 0;
        const int pr = ProjectPod(hx, hy, hz, &sx, &sy);
        if (pr < 0) { ++g_projectFail; SetHidden(L, kHideProject); continue; }
        if (pr == 0) { SetHidden(L, kHideOffscreen); continue; }
        /* The two lines stand as one block whose bottom is at the projected point: the name on top, the faction under it,
           each centred on the character. */
        const int bw = L.tw > L.tw2 ? L.tw : L.tw2, bh = L.th + L.th2;
        const int left = (int)sx - bw / 2, top = (int)sy - bh;
        if (viewW > 0 && (left + bw < 0 || top + bh < 0 || left > viewW || top > viewH)) { SetHidden(L, kHideOffscreen); continue; }
        PlaceLine(L.w, L.e, (int)sx - L.tw / 2, top);
        PlaceLine(L.w2, L.e2, (int)sx - L.tw2 / 2, top + L.th);
        if (L.hide != kShown)
        {
            L.hide = kShown;
            ++g_visibleNow;
            ShowLine(L.w, L.e, true);
            ShowLine(L.w2, L.e2, !L.caption2.empty());
        }
    }
}

/* review-tags1 3: a fault switches the labels off for the process - and then ONE guarded pass hides every widget still
   standing, so none is left frozen on screen (in game or at the title). If that pass faults too, it is not retried. */
void HideAllBody()
{
    for (std::map<unsigned int, Label>::iterator it = g_labels.begin(); it != g_labels.end(); ++it)
    {
        ShowLine(it->second.w, it->second.e, false);
        ShowLine(it->second.w2, it->second.e2, false);
        it->second.hide = kHideOff;
    }
    g_visibleNow = 0;
}
void HideAllCatching() { try { HideAllBody(); } catch (...) { ++g_throws; } }
void HideAllSeh()
{
    ++g_hideAllPasses;
    __try { HideAllCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; }
}
void Die()
{
    if (g_dead) return;
    g_dead = true;
    HideAllSeh();
}

void TickBodyCatching()
{
    try { TickBody(); }
    catch (...) { ++g_throws; }
}

void TickSeh()
{
    __try { TickBodyCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; Die(); }
}

void TitleBody()
{
    ::InterlockedExchange(&g_dropAll, 0);
    if (!g_labels.empty()) DestroyAll();
}
void TitleBodyCatching() { try { TitleBody(); } catch (...) { ++g_throws; } }
void TitleSeh()
{
    __try { TitleBodyCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; Die(); }
}

/* review-tags1 4: the drop-all reached from registration runs under the same guard as the tick. */
void DropCatching() { try { ProcessDropAll(); } catch (...) { ++g_throws; } }
void DropSeh()
{
    __try { DropCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; Die(); }
}

} // namespace

void TagsNoteCopy(unsigned int uid)
{
    if (g_dead || uid == 0) return;
    if (g_dropAll != 0) DropSeh();   // a teardown flag raised before this new world's first tick must not take this label with it
    if (g_dead) return;
    ::Character* c = FindSpawned(uid);
    if (c == 0) return;
    /* Called when a copy is created or adopted, and when another player recruits a character whose copy already exists
       (hire.cpp, after its faction is set). */
    if (PeerFactionPod(c) != 1) { ++g_notRegistered; return; }
    if (g_labels.find(uid) != g_labels.end()) return;
    Label L;
    L.w = 0; L.w2 = 0;
    for (int k = 0; k < nametag::kOutlineCopies; ++k) { L.e[k] = 0; L.e2[k] = 0; }
    L.gen = 0; L.hide = kHideNew; L.tw = 8; L.th = 8; L.tw2 = 0; L.th2 = 0; L.level = nametag::kUnknown; L.teammate = 0; L.createFails = 0;
    g_labels[uid] = L;   // the widgets are made on the first tick that would show them
}

namespace {
void ForgetBody(unsigned int uid)
{
    std::map<unsigned int, Label>::iterator it = g_labels.find(uid);
    if (it == g_labels.end()) return;
    if (it->second.hide == kShown && g_visibleNow > 0) --g_visibleNow;
    try { if (!g_dead) DestroyLabel(MyGUI::Gui::getInstancePtr(), it->second); } catch (...) { ++g_throws; }
    g_labels.erase(it);
}
void ForgetSeh(unsigned int uid)
{
    __try { ForgetBody(uid); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_faults; Die(); }
}
} // namespace

void TagsForgetUid(unsigned int uid)
{
    if (g_labels.empty()) return;
    ForgetSeh(uid);
}

void TagsCaptionsDirty() { ::InterlockedIncrement(&g_captionGen); }
void TagsWorldTeardown() { ::InterlockedExchange(&g_dropAll, 1); }

void TagsTick()
{
    if (g_dead) return;
    TickSeh();
}

void TagsTitleTick()
{
    if (g_dead) return;
    if (g_labels.empty() && g_dropAll == 0) return;
    TitleSeh();
}

void TagsSetOn(bool on)
{
    if (on == g_on) return;
    g_on = on;
    if (on) ++g_toggledOn; else ++g_toggledOff;
    DebugLog(std::string("[TAGS] labels turned ") + (on ? "ON" : "OFF"));
}

void TagsSetLift(float lift) { g_liftSet = (lift > 0.0f) ? lift : 0.0f; }

std::string TagsReportLine()
{
    return "[TAGS] REPORT on=" + N(g_on ? 1 : 0) + " key=Insert registered=" + N((long long)g_labels.size())
         + " created=" + N(g_created) + " destroyed=" + N(g_destroyed) + " visible=" + N(g_visibleNow)
         + " hiddenOffscreen=" + N(g_hiddenOffscreen) + " hiddenRange=" + N(g_hiddenRange) + " hiddenUi=" + N(g_hiddenUi)
         + " toggledOff=" + N(g_toggledOff) + " posReadFail=" + N(g_posReadFail) + " viewReadFail=" + N(g_viewReadFail) + " originReadFail=" + N(g_originReadFail)
         + " projectFail=" + N(g_projectFail) + " createFail=" + N(g_createFail) + " hiddenWithdrawn=" + N(g_hiddenWithdrawn)
         + " hideAllPasses=" + N(g_hideAllPasses)
         + " hiddenBlocked=" + N(g_hiddenBlocked) + " hiddenNotPeer=" + N(g_hiddenNotPeer) + " toggledOn=" + N(g_toggledOn)
         + " keyToggles=" + N(g_keyToggles) + " notRegistered=" + N(g_notRegistered) + " captionSets=" + N(g_captionSets)
         + " destroyGone=" + N(g_destroyGone) + " destroyStale=" + N(g_destroyStale) + " throws=" + N(g_throws)
         + " outlineMade=" + N(g_edgesMade) + " outlineDestroyed=" + N(g_edgesDestroyed)
         + " named[roster,faction]=" + N(g_byRoster) + "," + N(g_byFaction)
         + " colourBuilds[friendly,neutral,hostile]=" + N(g_levelBuilds[0]) + "," + N(g_levelBuilds[1]) + "," + N(g_levelBuilds[2])
         + " teamBuilds[blue,teamLine]=" + N(g_teamBuilds) + "," + N(g_teamLineBuilds)
         + " recolours=" + N(g_recolours)
         + " faults=" + N(g_faults) + " uiShown=" + N(ReadUiShownPod())
         + " lift=" + Fl(g_liftSet > 0.0f ? g_liftSet : (g_liftAuto > 0.0f ? g_liftAuto : kLiftFallback))
         + (g_liftSet > 0.0f ? "(set)" : (g_liftAuto > 0.0f ? "(own-tag)" : "(fallback)"));
}

} // namespace coop
