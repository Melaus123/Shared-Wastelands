// ui.cpp - E43: the MULTIPLAYER button on Kenshi's title screen, and (P8i / U1, U2) the panel it opens.
//
// P7z put ONE button here to settle two questions a read cannot: does our DLL load with a MyGUI import
// present, and does a widget we create survive and receive clicks on this build.  P8i gave that button
// something to open - design-ui-panel phase U1, the SHELL.  U2 is phase two, "it works now".
//
// WHAT U2 CHANGED, IN ONE PARAGRAPH.  U1's one button wrote shared_wastelands.cfg and said, truthfully, that
// the settings took effect the next time Kenshi started.  U2's button writes the same file and then
// calls coop::ConfigRearmFromFile, which re-reads it and clears ConfigTitleTick's latch - so the NEXT
// title tick opens the links through the arming path that already existed and that F574 measured.  There
// is no second arming path in this file: ui.cpp opens no socket, and the only Win32 work it does itself
// is CreateProcess for the world's notebook when the player ticked "run it for me".  The status area
// stops being one fixed paragraph and becomes a live reading, rebuilt at most four times a second from
// coopui::PanelStatusText - a pure decision in src/common/panelstatus.h that the offline suite sweeps.
//
// T-201 N1 (2026-09-29) - CONNECT ON THE PRESS, LEAVE ON CANCEL OR A FAILURE.  Nothing goes online until HOST or JOIN is
// pressed.  The press writes session.cfg, starts (or reuses) this computer's world server for a host, and re-arms; the
// HOST GAME / JOIN GAME window stays up with its fields greyed and BACK reading CANCEL until the world answers or a
// failure is known (a CAN'T HOST / CAN'T JOIN box, whose OK returns to the screen).  CANCEL, every failure and a new press
// call coop::ConfigLeave: the session is left (BYE), the store link closed, the role put back to single and the arming
// latch and dial count reset - so HOST / JOIN work again with no restart, for the same world (another world still needs a
// restart: W2b).  The "Already connected / Already connecting ... Restart Kenshi" refusals are gone.
// The notebook is never force-killed (design 2.2: a write caught half-finished is served back as good,
// which is the damage class the S1 ladder is repairing).  `--owner <this install's player id>` IS passed to
// SharedWastelandsServer.exe (B13 - the relay reads the pair and names the player who pressed the button the world's operator;
// see the start code below). F725 R5's old reason for leaving it out - an unknown flag consumed the next token -
// was closed by that relay work.
//
// THE ONE RULE: NEVER KEEP A WIDGET POINTER ACROSS FRAMES.
// Kenshi builds its title screen ONCE per process (0x82A973) and deletes it at the first world load (0x911F10); it is NOT
// rebuilt on every appearance, as this header used to say (refuted by build/read-return-to-title.md, F691).  Our button
// and our panel are children of that screen's own widget tree, so they are destroyed with it at the load - which is what
// we want, no leak and no cleanup - and the panel is also destroyed and rebuilt by this file whenever it closes and opens
// again.  Any pointer we had cached would be dangling at either moment.  RE_Kenshi hit
// exactly this defect and chose to LEAK rather than risk freeing a widget the game had already freed
// (dllmain.cpp:1602-1610).  We avoid the choice: every frame looks every widget up by name.
// A consequence worth reading twice: uiCreated and panelBuilt count RE-creations - the panel closed and opened again,
// or a build retried - not a fault.
//
// AND THEREFORE THE PANEL'S STATE IS NOT IN THE PANEL.  "The panel is open" is a LONG (g_panelWanted),
// never a pointer, and the six things the player typed live in module-scope strings.  A rebuilt panel
// is repopulated from them.
//
// EXISTS MUST IMPLY WIRED (review-p8a M-5 / F670).  A MyGUI throw between createWidget and the delegate
// attach used to leave a drawn, clickable, DEAD button while created= read 0.  Under P8i that button is
// the only way to open the panel, so the rule is enforced for both: a flag is raised as the LAST step of
// a successful build, and a widget found with its flag down is DESTROYED and rebuilt rather than
// returned early from.
//
// THE CLICK HANDLERS DO NO WORK (review-p7z M-5).  A click arrives on whichever thread MyGUI injects
// input from and is OUTSIDE the fault guard.  Every handler here does two interlocked writes - an
// action code into one slot, and actionsQueued - and returns.  The tick takes the slot on the title
// pump, inside the guard, books actionsRun, and does the work.  actionsQueued == actionsRun at rest is
// the measurement that says no click was swallowed; a gap says one was.
//
// WHY THE EXIT BUTTON IS NOT FOUND BY ITS NAME.
// TitleScreen derives from wraps::BaseLayout, and BaseLayout::initialise loads the layout under a
// per-instance prefix - mPrefix = MyGUI::utility::toString(this, "_") - so every widget named in
// data/gui/layout/Kenshi_MainMenu.layout carries "<pointer-hex>_" in front of its runtime name, and
// the prefix changes every time the screen is rebuilt. Gui::findWidget("ExitButton") is therefore
// ALWAYS null, and build/read-ui.md's recipe was wrong on this point. RE_Kenshi matches on the text
// after the first '_' (dllmain.cpp:71-89) and so do we. Our OWN widgets are created by us rather than
// by a layout, so they keep the exact names we give them and the plain lookup finds them - and because
// every one of our names contains no '_' at all, RE_Kenshi's suffix test can never match one either,
// which is the widget-name separation read-ui.md asks for between two mods drawing on one screen.
//
// SEH: every MyGUI call is behind UiTitleTickGuarded, whose frame holds no C++ object (C2712 - a
// function containing __try may not require object unwinding). A MEMORY fault turns the tick off for
// the rest of the process; a C++ throw is retried and latches after kUiSoftStrikeCap of them.

#include "coop_log.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_ISubWidgetText.h>   /* ui7: the text's measured line height (getFontHeight, virtual) */
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_MultiListBox.h>    /* ui2: the worlds and profiles lists - Kenshi's own Load Game list widget */
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_LayerManager.h>    /* pp1b: getWidgetFromPoint - uiclick refuses a covered widget */
#include <mygui/MyGUI_RenderManager.h>   /* mmo5: the view size, for the host-left window (tags.cpp includes it too) */
#include <mygui/MyGUI_KeyCode.h>          /* P8i-b: KeyCode::Escape for the panel's own key handler */
#include "game/TitleScreen.h"       /* P8i-b: TitleScreen::closeTheOtherBits - the ESC gate we detour */
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
#include "addresses.h"
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3CloseTheOtherBits = 0; static coop::AddrReg kMig3CloseTheOtherBits_reg("TitleScreen_closePanels", &kMig3CloseTheOtherBits);   /* Steam_1.0.65 0x913BC0 */
#include "hooks.h"                       /* P8i-b: coop::AddHook (own MinHook) */
#include "ui.h"
#include "addresses.h"   /* P8h: the caption says the mod is off when the address gate refused */
#include "config.h"
#include "store.h"                        /* U2: StoreWelcomedThisLink / StoreLinkIsUp / StoreMySlot / TimeModeName / StoreNotebookDir */
#include "net/session.h"                  /* U2: SessionLinked / SessionPeerCount - read live, never cached */
#include "net/homeaddr.h"                 /* mp4: HomeNetworkAddress - the Hosting screen's home-network address */
#include "upnp.h"                         /* T-53: UpnpHostStart - the router is asked to forward the world server's port */
#include "../common/upnpplan.h"          /* T-53: MapOnHostStart */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "settings.h"                     /* mp5: SettingsHostChoices / SettingsLiveValue - the Game options screen */
#include "command_channel.h"              /* U2: PathNextToDll - where SharedWastelandsServer.exe and the mod folder are */
#include "../common/panelfit.h"           /* P8i-b: the size refusal as a pure, swept decision */
#include "../common/panelstatus.h"        /* U2: every sentence the status area can say, as a pure decision */
#include "../common/worlddir.h"           /* W2-f: WorldOrDefault - the notebook is started for the world the plugin uses */
#include "../common/profiles.h"          /* T-201 PP6': AutoLoadDecide, HostProfileChoose (pure, swept offline) */
#include "../common/joinstage.h"         /* coopjoin::kHoldPress - the JOIN press's load held for the world's operator */
#include "../common/uidrive.h"           /* pp1: the TEST-ONLY UI driver verbs' name table (uiclick / uipick / uitype / uistate) */
#include "../common/pausemenu.h"         /* T-514: where the pause menu's rows go, hidden three or not */
#include "../common/ownrec.h"            /* uishot: the host-left texts the uipreview verb shows (coopown::HostLeftTitle ...) */
#include "../common/datadir.h"           /* PP3d: IdentityRefusesHostJoin / IdentityMayReplace, the identity box's buttons */
#include "soak.h"                         /* uishot: GameplayRunning - a title preview is refused in a world and back */
#include "bugreport.h"                    /* T-461: REPORT A BUG - its ESC at the title and its pause-menu row */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>                     /* mp3: SHFileOperationA - a deleted world goes to the Recycle Bin, never a hard delete */
#pragma comment(lib, "shell32.lib")
#ifndef FOF_WANTNUKEWARNING
#define FOF_WANTNUKEWARNING 0x4000        /* shellapi.h (IE 5.0): warn rather than silently destroy what the Recycle Bin cannot take */
#endif
#include <vector>
#include <map>
#include <ctime>

#include <sstream>
#include <locale>
#include <string>
#include <cstdio>   // P8a: _snprintf for the fault line (no C++ object in a frame that must stay simple)

namespace coop {

static const char* const kOurName    = "CoopMultiplayerButton";
static const char* const kExitSuffix = "ExitButton";                    // matched AFTER the layout prefix
/* U3 (user decision 2026-09-22): the title column's order is CONTINUE, NEW GAME, MULTIPLAYER, LOAD GAME,
   IMPORT GAME, OPTIONS, CREDITS, EXIT.  NEW GAME is the anchor ours goes under; the five below it each move
   down one row.  Same suffix match as EXIT (the layout prefix is per-instance - see the header comment). */
static const char* const kNewGameSuffix = "NewGameButton";
/* U3-b: CONTINUE is read, never moved - its gap to NEW GAME is the live row pitch (UiLivePitch). */
static const char* const kContinueSuffix = "ContinueButton";
static const char* const kBelowSuffixes[5] = { "LoadGameButton", "ImportGameButton", "OptionsButton",
                                               "CreditsButton", "ExitButton" };
static const char* const kSkin       = "Kenshi_Button1";                // the skin all seven menu buttons use
static const char* const kFont       = "Kenshi_PaintedTextFont_Large";  // ditto, from the layout XML
static const char* const kCaption    = "MULTIPLAYER";

// ------------------------------------------------------------------------------------------------
// THE NAMES, held as std::string so the per-tick lookups do not build a temporary each time.
// review-p7z M-4: Gui::findWidgetT takes const std::string&, "CoopMultiplayerButton" is 21 characters,
// MSVC10's small-string buffer holds 15 - so a const char* here was one malloc and one free per tick at
// the measured 1,065 ticks/s.  P8i adds a second per-tick lookup and would have doubled that.
//
// EVERY WIDGET NAME BEGINS WITH "Coop" AND CONTAINS NO '_' (see the header comment).  The SKIN names are
// the game's own, read from <Steam>\Kenshi\data\gui\templates\kenshi_templates.xml and skins\kenshi_skins.xml.
// The type names are MyGUI's, passed as strings to createWidgetT so no widget class's getClassTypeName
// has to be imported for the sake of a template argument.
// ------------------------------------------------------------------------------------------------
struct UiNames
{
    std::string ourBtn, panel, modeHost, modeJoin, hostGroup, joinGroup;
    std::string portLabel, portEdit, nbMineTick, nbMineLabel, nbFarTick, nbFarLabel, nbAddrEdit;
    std::string joinLabel, joinEdit, pasteBtn;
    std::string worldLabel, worldEdit, slotLabel, slotEdit;
    std::string status, goBtn, closeBtn, notice, backdrop;
    std::string landGroup, nameLabel, nameEdit, nameHint, landBackBtn, screenTitle, tryBtn;   /* mp1 */
    std::string worldsLabel, worldList, worldEmpty, newWorldBtn, optionsBtn, deleteBtn;   /* mp3; ui2: one scrolling list */
    std::string dlgGroup, dlgText, dlgNameLabel, dlgNameEdit, dlgOk, dlgCancel;                         /* mp3 */
    std::string hostingGroup, homeAddr, copyBtn, routerHelp, playersBox, chooseBtn, homeAddrLabel;     /* mp4; ui5: homeAddrLabel */
    std::string netAddrLabel, netAddr, netShowBtn, netCopyBtn;                                         /* T-510: INTERNET ADDRESS */
    std::string optGroup, optTab[coopui::kOptTabCount], optNote, optCoopNote, optDefaultsBtn, optDoneBtn; /* mp5 */
    std::string optLabel[coopui::kOptRowsShown], optDec[coopui::kOptRowsShown], optVal[coopui::kOptRowsShown];
    std::string optInc[coopui::kOptRowsShown], optTick[coopui::kOptRowsShown];
    std::string profGroup, profList, profPlay, profNew, profDelete;          /* prof3; ui2: one scrolling list */
    std::string hostProfLabel, hostProfValue, hostProfChangeBtn;             /* T-201 PP6': HOST GAME's PROFILE row */
    std::string skinWindow, skinBtn1, skinBtn2, skinEdit, skinFlat, skinTick, skinWrap, skinPanel;
    std::string skinSolid, skinMulti, skinCaption;   /* ui3: skinCaption */
    std::string typeWindow, typeWidget, typeButton, typeEdit, typeText, typeMulti;
    std::string fontLarge;
    UiNames()
        : ourBtn(kOurName), panel("CoopPanel"),
          modeHost("CoopModeHost"), modeJoin("CoopModeJoin"),
          hostGroup("CoopHostGroup"), joinGroup("CoopJoinGroup"),
          portLabel("CoopHostPortLabel"), portEdit("CoopHostPortEdit"),
          nbMineTick("CoopNotebookMineTick"), nbMineLabel("CoopNotebookMineLabel"),
          nbFarTick("CoopNotebookFarTick"), nbFarLabel("CoopNotebookFarLabel"),
          nbAddrEdit("CoopNotebookAddrEdit"),
          joinLabel("CoopJoinAddrLabel"), joinEdit("CoopJoinAddrEdit"), pasteBtn("CoopPasteBtn"),
          worldLabel("CoopWorldLabel"), worldEdit("CoopWorldEdit"),
          slotLabel("CoopSlotLabel"), slotEdit("CoopSlotEdit"),
          status("CoopStatusText"),
          /* U2 RENAME: the button is no longer a SAVE.  `CoopSaveBtn` would have been a name that says
             what the control used to do, which is the class of defect this project fixes on sight. */
          goBtn("CoopGoBtn"), closeBtn("CoopCloseBtn"), notice("CoopNoticeText"),
          /* U2-b: the opaque surface that goes BEHIND the panel.  "Coop" prefix, no '_', like every
             other name here, so RE_Kenshi's after-the-first-underscore test can never match it. */
          backdrop("CoopBackdrop"),
          /* mp1 (design-mpmenu1 sections 3 and 7): the Multiplayer screen's group and rows, the Host/Join screens' title
             line and the Join screen's Try again.  CoopModeHost / CoopModeJoin keep their names: they are now the
             Multiplayer screen's "Host a game" / "Join a game", which is what they always did (pick the mode). */
          landGroup("CoopLandGroup"), nameLabel("CoopNameLabel"), nameEdit("CoopNameEdit"), nameHint("CoopNameHint"),
          landBackBtn("CoopLandBackBtn"), screenTitle("CoopScreenTitle"), tryBtn("CoopTryAgainBtn"),
          skinWindow("Kenshi_WindowCX"), skinBtn1("Kenshi_Button1"), skinBtn2("Kenshi_Button2"),
          skinEdit("Kenshi_EditBox"), skinFlat("Kenshi_GenericTextBoxFlat"),
          skinTick("Kenshi_TickBoxSkin"), skinWrap("Kenshi_WordWrapEmpty"), skinPanel("PanelEmpty"),
          /* U2-b - THE OPAQUE ONE, and it is the game's own:
             <Kenshi>\data\gui\skins\kenshi_skins.xml:42 declares Kenshi_InventoryPanelSkin as a
             single BasisSkin SubSkin at align="Stretch" over texture="Kenshi_UI.png", so it covers
             whatever rectangle it is given at any size with one stretched bitmap.  That is the whole
             requirement for a backdrop.  PanelEmpty, which the panel's own Client uses, draws nothing -
             which is why the menu text behind the panel was visible through it (hands-on report,
             2026-09-05).  U2-c (review-u2 Q9-1): PanelEmpty is declared in
             <Kenshi>\data\gui\skins\common_skins.xml:3, NOT in kenshi_skins.xml - U2-b named the
             wrong file for it.  The reading of both skins, and the conclusion, are unchanged. */
          skinSolid("Kenshi_InventoryPanelSkin"),
          typeWindow("Window"), typeWidget("Widget"), typeButton("Button"),
          typeEdit("EditBox"), typeText("TextBox"),
          fontLarge(kFont)
    {
        /* mp3 (design-mpmenu1 section 4): the Host a game screen's worlds list and its two small dialogs (New world,
           Delete world?).  Assigned here because a C++03 initialiser list cannot fill an array. */
        worldsLabel = "CoopWorldsLabel"; worldList = "CoopWorldList"; worldEmpty = "CoopWorldEmptyText";   /* ui2: in place of three row buttons + Newer / Older */
        /* ui2: the widget type and skin of Kenshi's own Load Game list (data\gui\layout\Kenshi_LoadGamePanel.layout, GamesList). */
        skinMulti = "Kenshi_MultiListBox"; typeMulti = "MultiListBox";
        /* ui3 (ui3-title-panel-look item 3): Kenshi's own panel captions ("LOAD GAME", "CHOOSE YOUR BEGINNING") are
           painted text with no box - kenshi_templates.xml Kenshi_TextboxPaintedText - not the beige value box. */
        skinCaption = "Kenshi_TextboxPaintedText";
        newWorldBtn = "CoopNewWorldBtn"; optionsBtn = "CoopGameOptionsBtn"; deleteBtn = "CoopDeleteWorldBtn";
        dlgGroup = "CoopDlgGroup"; dlgText = "CoopDlgText"; dlgNameLabel = "CoopDlgNameLabel"; dlgNameEdit = "CoopDlgNameEdit";
        dlgOk = "CoopDlgOkBtn"; dlgCancel = "CoopDlgCancelBtn";
        /* mp4 (design-mpmenu1 section 6): the Hosting screen. */
        hostingGroup = "CoopHostingGroup"; homeAddr = "CoopHomeAddrText"; copyBtn = "CoopCopyAddrBtn";
        routerHelp = "CoopRouterHelpText"; playersBox = "CoopPlayersText"; chooseBtn = "CoopChooseProfileBtn";
        homeAddrLabel = "CoopHomeAddrLabel";   /* ui5: LOCAL ADDRESS, a label level with its value */
        /* T-510: INTERNET ADDRESS under it - the label, the value, SHOW / HIDE and its own COPY.  The one rule a name of ours
           follows is no '_' (RE_Kenshi's suffix test, the note at the top of this file); new names start "SW". */
        netAddrLabel = "SWNetAddrLabel"; netAddr = "SWNetAddrText"; netShowBtn = "SWNetAddrShowBtn"; netCopyBtn = "SWNetAddrCopyBtn";
        /* prof3 (design-mpmenu1 section 8): the Your profiles screen - the list (ui2: one scrolling list with NAME / FACTION /
           LAST PLAYED columns, in place of a hand-spaced heads line, Up / Down and four row buttons), Play / New profile / Delete. */
        profGroup = "CoopProfGroup"; profList = "CoopProfList";
        profPlay = "CoopProfPlayBtn"; profNew = "CoopProfNewBtn"; profDelete = "CoopProfDeleteBtn";
        hostProfLabel = "CoopHostProfLabel"; hostProfValue = "CoopHostProfValue"; hostProfChangeBtn = "CoopHostProfChangeBtn";   /* T-201 PP6' */
        /* mp5 (design-mpmenu1 section 5): the Game options screen - three tab buttons, nine rows of label / < / value / >
           or a tick box, two notes, Defaults and Done. */
        optGroup = "CoopOptGroup"; optNote = "CoopOptNote"; optCoopNote = "CoopOptCoopNote";
        optDefaultsBtn = "CoopOptDefaultsBtn"; optDoneBtn = "CoopOptDoneBtn";
        for (int t = 0; t < coopui::kOptTabCount; ++t) optTab[t] = std::string("CoopOptTab") + (char)('0' + t);
        for (int r = 0; r < coopui::kOptRowsShown; ++r)
        {
            const char d = (char)('0' + r);
            optLabel[r] = std::string("CoopOptLabel") + d; optDec[r] = std::string("CoopOptDec") + d;
            optVal[r] = std::string("CoopOptVal") + d;     optInc[r] = std::string("CoopOptInc") + d;
            optTick[r] = std::string("CoopOptTick") + d;
        }
    }
};
// TITLE PUMP ONLY.  VS2010 does not make a function-local static's initialisation thread-safe, and it
// does not have to be: nothing off the title pump calls this.  The click handlers touch interlocked
// longs and nothing else.
static const UiNames& NM()
{
    static const UiNames n;
    return n;
}

// UNITS. created, clicks, panelOpened, panelSaved, panelInvalid and the rest count EVENTS. noGuiTicks,
// noExitButtonTicks, noParentTicks and noRoomTicks count TICKS: this runs on the title pump, which is
// roughly 1,065 Hz (review-p7z Q1(b), F041), so a screen without our anchor books thousands of them per
// second. That is the honest reading and the identifiers say so - a refusal counter read as an event
// count has cost this project runs before. faulted can only reach 1: the first memory fault disables
// the tick.
static volatile LONG64 g_uiCreated    = 0;
static volatile LONG64 g_uiClicks     = 0;
static volatile LONG64 g_uiNoGui      = 0;
/* P8i WIDENS THIS ONE'S SPAN, and it is said here rather than left for a reader to work out: it used to
   count ticks on which the button was missing and the anchor was not found.  It now counts ticks on
   which SOMETHING needed building - the button, or the panel - and the anchor was not found.  The name
   is kept because run readouts grep for it; the meaning is "a tick that wanted the anchor and did not
   get it", which is what it always was. */
static volatile LONG64 g_uiNoExit     = 0;
/* P8a (review-p7z M-1): TWO DIFFERENT REFUSALS, TWO COUNTERS.  Both sites used to book noExitButtonTicks, and
   "the exit button is not on screen" and "the exit button was found and has no parent" are different facts -
   the second can only ever be 0 or a very large number, and which of the two it is matters. */
static volatile LONG64 g_uiNoParent   = 0;
/* P8a (review-p7z M-2): the button had nowhere to go that overlapped nothing.  It can only be non-zero on a
   parent shorter than the layout implies, and it is a NUMBER rather than a widget landing somewhere arbitrary. */
static volatile LONG64 g_uiNoRoom     = 0;
/* U3 - THE MENU ORDER.  buttonsMoved counts game buttons moved down a row by an ORDERING - five when OUR
   button is created ordered, five again each time the order is re-applied after a fallback.  U3-b (review-u3):
   buttonsReshifted counts ONLY a game button found away from where WE last put it at an UNCHANGED row pitch -
   the game moved it; 0 at rest, and 0 through a window resize (every menu button is align="Default", so a
   background resize leaves their pixels alone).  buttonsRepitched counts game buttons re-placed because the
   live pitch itself changed.  buttonOrderFallback counts decisions to use the old spot below EXIT (a column
   button missing, or no room for the longer column); buttonOrderRestored counts returns from that fallback to
   the order.  ONE log line per MODE CHANGE (ordered -> fallback, fallback -> ordered), never per reason or per
   pixel size.  On a parent too small for BOTH placements the create is attempted every tick, so there
   buttonOrderFallback climbs with noRoomTicks, in ticks. */
static volatile LONG64 g_uiBtnsMoved     = 0;
static volatile LONG64 g_uiBtnsReshifted = 0;
static volatile LONG64 g_uiBtnsRepitched = 0;
static volatile LONG64 g_uiOrderFallback = 0;
static volatile LONG64 g_uiOrderRestored = 0;
/* TITLE PUMP ONLY: 0 = no decision for the button on screen, 1 = the column is arranged, 2 = fell back to
   below EXIT.  U3-b: BOTH 1 and 2 are re-checked (no latched fallback) - on a parent-size change only once that
   size has held still for coopui::kPanelResizeSettleMs (the panel's settle rule), and every
   kUiOrderCheckEveryMs otherwise. */
static int         g_uiOrderMode          = 0;
static DWORD       g_uiOrderCheckMs       = 0;
static int         g_uiOrderParentW       = 0, g_uiOrderParentH = 0;   // the size last CHECKED
static DWORD       g_uiOrderSettleSinceMs = 0;                         // 0 = no size change pending
static int         g_uiOrderSettleW       = 0, g_uiOrderSettleH = 0;
static int         g_uiOrderLoggedMode    = 1;   // the mode the log last announced; ordered is the expected start
static int         g_uiPutPitch           = 0;   // the pitch the column was last laid out at
static const DWORD kUiOrderCheckEveryMs = 500;
static const int   kUiBookMoved = 0, kUiBookReshift = 1, kUiBookRepitch = 2;   // which counter UiApplyOrder books
/* U3-b - THE ORIGINALS.  At the first shift in a title instance the five engine buttons' own coords are
   remembered, keyed by widget pointer, and the put-back restores exactly those - never a recomputed row.  The
   pointers are only ever COMPARED, never followed: a restore touches a widget only when the live suffix walk
   finds it under the same parent at the same address.  Cleared when a put-back has used them, when our button
   is missing at the top of a tick, and when the title is gone (no EXIT found). */
static MyGUI::Widget* g_uiOrigW[5]   = { 0, 0, 0, 0, 0 };
static int            g_uiOrigL[5]   = { 0, 0, 0, 0, 0 };
static int            g_uiOrigT[5]   = { 0, 0, 0, 0, 0 };
static MyGUI::Widget* g_uiOrigParent = 0;
static int            g_uiOrigHeld   = 0;
/* ui5c (user decision 40, 2026-09-27) - THE SUB-MENU REPLACES THE MAIN MENU.  Which title-column buttons WE
   hid while the MULTIPLAYER panel is up, by slot: 0 CONTINUE, 1 NEW GAME, 2 ours, 3..7 kBelowSuffixes (LOAD GAME
   .. EXIT).  0 in a slot = we did not hide that one (it was already hidden by the game, or not found).  Same rule
   as the U3-b originals: the pointers are only ever COMPARED, never followed.  g_uiMenuHeld = "the column is
   hidden by us"; g_uiMenuParent = the column's parent it was hidden under (compared only).  TITLE PUMP ONLY.
   Counters: menuHidden / menuRestored / menuForgot are EVENTS (one per transition); menuRehidden is BUTTONS -
   each one the game showed again while the panel was up and the throttled pass hid again. */
static const int      kUiMenuSlots = 8;
static MyGUI::Widget* g_uiMenuHidW[kUiMenuSlots] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static MyGUI::Widget* g_uiMenuParent  = 0;
static int            g_uiMenuHeld    = 0;
static DWORD          g_uiMenuCheckMs = 0;
static volatile LONG64 g_uiMenuHidden   = 0;
static volatile LONG64 g_uiMenuRestored = 0;
static volatile LONG64 g_uiMenuRehidden = 0;
static volatile LONG64 g_uiMenuForgot   = 0;
/* P8a (review-p7z H-1): a C++ throw is not a memory fault.  MyGUI delivers ordinary recoverable conditions as
   C++ exceptions (MSVC code 0xE06D7363) - an unregistered skin on one frame, the same delegate added twice -
   and the old handler treated those exactly like a wild-pointer read: latch, one line, feature gone for the
   process.  softStrikes counts every NON-memory fault and is what the retry cap is keyed on. */
static volatile LONG64 g_uiCppThrow    = 0;
static volatile LONG64 g_uiSoftStrikes = 0;
static volatile LONG   g_uiLastCode    = 0;
static const unsigned long kUiCppThrowCode = 0xE06D7363ul;   // MSVC's SEH code for a C++ throw
static const long long     kUiSoftStrikeCap = 3;             // 6a lesson 14: retry, then give up - not one-strike-and-out
static volatile LONG64 g_uiCreateNull = 0;   // createWidget returned null - never seen, counted anyway
/* P8h: how many times the button was put up carrying the DISABLED caption rather than MULTIPLAYER. It counts
   creations, like uiCreated, so on a refused game the two climb together and on a healthy one this stays 0. */
static volatile LONG64 g_uiRefused    = 0;
static volatile LONG64 g_uiFaulted    = 0;
static volatile LONG   g_uiDisabled   = 0;
static bool g_uiFirstCreateLogged = false;
/* P8h-b (review-p8h H-1): the churn guard's one-shot latch - see the refusal branch of UiCreateButton. */
static bool g_uiRefuseChurnLogged = false;

/* P8i - THE BUTTON'S OWN FLAG (review-p8a M-5 / F670).  Raised as the LAST step of a create, lowered
   whenever the button is not on screen.  A button found with this down was built by a create that did not
   run to the end - a delegate attach that threw - so it is drawn, pickable and dead: destroy it, count it,
   and let the next tick build it again.
   P8h-b (review-p8h H-1) RESTATES WHAT IT MEANS, because the old name's promise was wrong for one of the
   two buttons this code makes.  It means CREATION FINISHED, not "a click handler is attached": the refusal
   caption is unwired ON PURPOSE (it is disabled and takes no clicks) and is nonetheless a finished button
   that must be left alone.  Both branches of UiCreateButton raise it as their last step. */
static volatile LONG   g_uiBtnWired   = 0;
static volatile LONG64 g_uiBtnUnwired = 0;

/* ------------------------------------------------------------------------------------------------
   P8i - THE PANEL.
   ------------------------------------------------------------------------------------------------ */
static volatile LONG   g_panelWanted  = 0;   /* THIS is what "the panel is open" means. Never a pointer. */
static volatile LONG   g_panelBuiltOk = 0;   /* raised as the LAST step of a build; lowered on destroy, by the title tick when it finds no panel widget (ui5c fold F2), and by the fault latch (F3) */
static volatile LONG   g_uiAction     = 0;   /* the click handlers' one slot */
static volatile LONG64 g_uiActionsQueued = 0, g_uiActionsRun = 0;
static volatile LONG64 g_panelOpened    = 0; /* the MULTIPLAYER button opened it (events) */
static volatile LONG64 g_panelSaved     = 0; /* shared_wastelands.cfg written (events) */
static volatile LONG64 g_panelInvalid   = 0; /* the GO button refused before writing anything (events).
                                                U2-c (review-u2 L-2): this said "SAVE", which is the name
                                                the button lost in U2 - it is START HOSTING or JOIN now. */
static volatile LONG64 g_panelCastNull  = 0; /* castType<T>(false) found the widget and said "not that type" */
static volatile LONG64 g_panelMissing   = 0; /* a widget we built was not there when we looked for it - a DIFFERENT fact */
static volatile LONG64 g_panelBuilt     = 0, g_panelDestroyed = 0;
static volatile LONG64 g_panelNoRoom    = 0; /* TICKS: the client area was too small for the rows */
static volatile LONG64 g_uiTextMeasureFallback = 0; /* ui7: MyGUI reported no text size / line height - the old estimate was used */
static volatile LONG64 g_panelNoClient  = 0; /* the Window skin gave us no Client widget - the skin is not what we think */
static volatile LONG64 g_panelBuildFail = 0; /* a build that got part way and gave up (events) */
static volatile LONG64 g_panelPasted    = 0, g_panelPasteFailed = 0;
/* P8i-b (review-p8i H-1/H-2/H-3).  FOUR DIFFERENT FACTS, FOUR COUNTERS - the same rule review-p7z M-1
   applied to the two "no anchor" refusals.  engineClosed is every time the title screen's own
   close-the-other-bits path closed our panel; escClosed is the subset of those where the Escape key was
   physically down, which is the quit that did not happen.  xClosed is the title-bar X.  tooSmall counts
   REFUSALS (events, capped by the fail streak), not ticks - which is why it is not folded into
   panelNoRoomTicks, whose two sites really do book one per tick. */
static volatile LONG64 g_panelEngineClosed = 0;
static volatile LONG64 g_panelEscClosed    = 0;
static volatile LONG64 g_panelXClosed      = 0;
static volatile LONG64 g_panelTooSmall     = 0;
/* U2-b, all EVENTS (6a lesson 1: the units are said once and the names carry no Ticks suffix because
   none of these is booked per tick).  panelBackdrop / panelBackdropFailed: the opaque surface behind the
   panel was created, or could not be.  panelGrown: the 40% x 70% box was below the comfort target for
   this parent and was grown, centred.  panelResized: the parent rectangle CHANGED under an open panel
   and the panel was taken down to be laid out again at the new one - this is the counter that reports
   the defect of 2026-09-05, where the first opening was laid out from a half-screen rectangle and only
   a close-and-reopen picked up the maximised one. */
static volatile LONG64 g_panelBackdrop       = 0;
/* U2-c, EVENTS except panelResizeSettling, which is TICKS and says so in its name's absence of a better
   word - it is the number of title ticks spent watching a window whose size has moved and not settled.
   panelBackdropReused: a build found a backdrop already on screen under our name and kept it instead of
   creating a second one (review-u2 H-2).  panelArmBusy: a press arrived while this game was still
   trying to connect from an earlier press, and was told so rather than starting a second attempt. */
static volatile LONG64 g_panelBackdropReused = 0;
static volatile LONG64 g_panelResizeSettling = 0;
static volatile LONG64 g_panelArmBusy        = 0;
static volatile LONG64 g_panelBackdropFailed = 0;
static volatile LONG64 g_panelGrown          = 0;
static volatile LONG64 g_panelResized        = 0;
/* TITLE PUMP ONLY (review-u2 H-1): the mismatching size first seen and WHEN, so a size that keeps
   moving can be told from one that has settled.  0 in g_panelMismatchSinceMs means no mismatch is
   pending; GetTickCount can legitimately return 0, so the stored value is nudged to 1 in that case. */
static DWORD g_panelMismatchSinceMs = 0;
static int   g_panelMismatchW = 0;
static int   g_panelMismatchH = 0;
/* TITLE PUMP ONLY: the parent rectangle the open panel was laid out from, so a change can be seen. */
static volatile LONG   g_panelAtParentW = 0;
static volatile LONG   g_panelAtParentH = 0;
/* TITLE PUMP ONLY: how many of the two above have had their line printed.  The hook must not log - see
   UiCloseFromEngine. */
static long long g_panelEngineLogged = 0;
static long long g_panelEscLogged    = 0;
/* ui6 (decision 42) - A TITLE BOX THAT NEEDS AN ANSWER BLOCKS THE MENU UNDER IT (UiBoxBlockSync).  g_boxLive: the box that is
   live on screen - 0 none, 1 the error box, 2 an identity box - raised and lowered ONLY by the title tick and the fault latch,
   read by UiCloseFromEngine (which may not touch MyGUI).  g_boxEscPending: an ESC that gate answered for that box, pressed
   on the next title tick.  EVENTS: boxBlockBuilt (strips cut around a live box, re-cuts included), boxBlockDropped (the
   strips taken away), boxBlockFailed (a strip createWidgetT returned null for), boxBlockedClicks (clicks the strips
   swallowed - clicks meant for Kenshi's menu), boxEscAnswered (ESCs that answered a box instead of reaching Kenshi's quit). */
static volatile LONG   g_boxLive           = 0;
static volatile LONG   g_boxEscPending     = 0;
static volatile LONG64 g_boxBlockBuilt     = 0;
static volatile LONG64 g_boxBlockDropped   = 0;
static volatile LONG64 g_boxBlockFailed    = 0;
static volatile LONG64 g_boxBlockedClicks  = 0;
static volatile LONG64 g_boxEscAnswered    = 0;

/* ------------------------------------------------------------------------------------------------
   U2 - THE COUNTERS FOR ACTING NOW.  Every one is an EVENT count; none of them is a tick count, and the
   names say which (6a lesson 1).  armRequested is what the player asked for, armStarted is what
   ConfigTitleTick actually did (coop::ConfigArmGen()), and the two being unequal at rest is the
   measurement that says a request was dropped - the same shape actionsQueued/actionsRun has.
   ------------------------------------------------------------------------------------------------ */
static volatile LONG64 g_panelArmRequested = 0;   /* START HOSTING / JOIN was pressed and the file was written */
static volatile LONG64 g_panelArmRefused   = 0;   /* ConfigRearmFromFile said no, in words the player was shown */
static volatile LONG64 g_panelArmLate      = 0;   /* the game had already armed this session: settings written for next launch */
static volatile LONG64 g_panelNbSpawned    = 0;   /* CreateProcess for SharedWastelandsServer.exe returned TRUE */
static volatile LONG64 g_panelNbSpawnFail  = 0;   /* CreateProcess refused */
static volatile LONG64 g_panelNbCmdTooLong = 0;   /* B13-b: the command line did not FIT its buffer, so the notebook was NOT started */
static volatile LONG64 g_panelNbNoExe      = 0;   /* SharedWastelandsServer.exe is not beside the DLL */
static volatile LONG64 g_panelNbAlready    = 0;   /* we had already started one and it is still running */
static volatile LONG64 g_panelNbExited     = 0;   /* the one we started has stopped (exit code kept below) */
static volatile LONG64 g_panelGaveUp       = 0;   /* the build-fail cap fired (events, not ticks) */
static volatile LONG64 g_noticeBuilt = 0, g_noticeDestroyed = 0;
static volatile LONG64 g_noticeGaveUp = 0;   /* EVENTS: the notice hit the same cap the panel has, not ticks */
static volatile LONG   g_panelStatusState  = 0;   /* coopui::PanelLinkState, for the verdict line */

/* THE NOTEBOOK PROCESS.  A PROCESS handle, not a widget pointer - the one rule at the top of this file
   is about widget pointers, and this handle is ours until we close it.  TITLE PUMP ONLY.
   design-ui-panel 2.2, verbatim as a constraint: it is used for GetExitCodeProcess and NOTHING else.
   There is no TerminateProcess in this file and there must not be one until the relay has a clean
   shutdown message of its own. */
static HANDLE g_nbProc      = 0;
static int    g_nbState     = coopui::kNbNotOurs;
static long   g_nbExitCode  = 0;
static int    g_nbPort      = 0;
static DWORD  g_nbLastPollMs = 0;

/* THE GIVE-UP NOTICE (review-p8i M-2).  The cap's own sentence used to be written into the panel's
   status area on the very frame the panel was closed and could not be rebuilt, so the one message that
   explains why nothing opened had nowhere to appear.  It goes on the TITLE SCREEN instead, in a widget
   of ours beside the MULTIPLAYER button, built and destroyed by the same tick under the same guard. */
static volatile LONG g_noticeWanted = 0;
static std::string   g_noticeText;
static long long     g_noticeSeq = 0, g_noticeShown = -1;
/* AND IT IS CAPPED BY THE SAME RULE THE PANEL IS.  A notice that cannot be created - a parent too small,
   a createWidgetT that returns null - would otherwise be retried at the title pump's ~1,065 Hz for ever,
   which is F709's own shape reintroduced by the fix for F709's sibling.  6a lesson 14, and 6a's warning
   that five of seventeen defects were introduced by the repair for the previous one. */
static long g_noticeFailStreak = 0;

enum UiActionCode
{
    kActNone = 0, kActToggle = 1, kActClose = 2, kActGo = 3, kActPaste = 4,
    kActModeHost = 5, kActModeJoin = 6, kActNotebookMine = 7, kActNotebookFar = 8, kActBack = 9,
    /* mp3: the Host a game screen.  7 and 8 (the two helper ticks) are no longer on screen and are never queued. */
    kActWorldPick = 10 /* ui2: a row of the worlds list was picked (11..14 were the row buttons and Newer / Older) */,
    kActNewWorld = 15, kActDeleteWorld = 16, kActDlgOk = 17, kActDlgCancel = 18,
    kActCopy = 19,  /* mp4: the Hosting screen's Copy */
    /* mp5: the Game options screen.  The rows' codes are ranges, one per row slot. */
    kActGameOptions = 20, kActOptTab0 = 21 /* 21..23 */, kActOptDec0 = 24 /* 24..32 */, kActOptInc0 = 33 /* 33..41 */,
    kActOptTick0 = 42 /* 42..50 */, kActOptDefaults = 51, kActOptDone = 52,
    /* prof3: the Your profiles screen, and the Hosting screen's Choose my profile. */
    kActProfPick = 53 /* ui2: a row of the profiles list was picked (54..58 were the row buttons and Up / Down) */, kActProfPlay = 59, kActProfNew = 60, kActProfDelete = 61,
    kActChooseProfile = 62,
    kActHostProfChange = 63,  /* T-201 PP6': HOST GAME's PROFILE row CHANGE */
    kActNetShow = 64, kActNetCopy = 65   /* T-510: HOSTING's INTERNET ADDRESS row SHOW / HIDE and COPY */
};

/* TITLE PUMP ONLY, and deliberately NOT behind a lock.  design-ui-panel 1.4.3 asks for a CRITICAL_SECTION.
   In U1 the only code that runs off the title pump is the click handlers, and they touch nothing but the
   interlocked longs above; every string below is written and read on the title pump alone.  A lock here
   would be a load-time initialiser and a false claim of thread safety.  U2 adds work that may read this
   state from elsewhere and must revisit this deliberately. */
static long        g_panelMode          = 0;   /* 0 HOST, 1 JOIN */
static long        g_panelNotebookMine  = 1;   /* 1 = run the notebook for me (U2's job), 0 = one already running */
static long        g_panelScreen        = 0;   /* mp1: 0 = Multiplayer (your name), 1 = Host a game, 2 = Join a game; mp3: 3 = New world, 4 = Delete world?; mp4: 5 = Hosting; mp5: 6 = Game options */
/* prof3 (design-mpmenu1 section 8): screen 7 = [F] Your profiles in this world, with two small dialogs over it (g_profDlg 1 =
   New profile's name, 2 = Delete profile?).  The rows are the store's last PROFILES answer, copied by UiProfilesPoll. */
static std::vector<StoreProfRow> g_profList;
static unsigned        g_profCapUi = 0, g_profSel = 0;
static long            g_profDlg = 0, g_profBusy = 0, g_profEverShown = 0;
/* ui2: the profiles list is refilled only when g_profList changed (UiProfilesPoll bumps g_profListGen), never on a push that
   only moves the pick - a refill would lose the player's scroll position.  -1 = the list on screen was just created. */
static long long       g_profListGen = 0, g_profListShown = -1;
static long long       g_profSeqSeen = 0;
static std::string     g_profSay;
static int             g_profPushWanted = 0;
static const int       kProfReqNew = 1, kProfReqDelete = 2;   /* coopprof::kReqNew / kReqDelete (src/common/profiles.h), as command_channel.cpp passes them */
static volatile LONG64 g_profilesShown = 0, g_profPicked = 0, g_profNewAsked = 0, g_profDeleteConfirmed = 0, g_profRefusedAtCap = 0, g_profRefusedShown = 0;
static std::string g_fName;                    /* mp1: the Your name box (cfg playername=) */
static long        g_panelNameOkShown   = -1;  /* mp1: what Host a game / Join a game were last enabled as (-1 = not yet) */
static std::string g_panelNameHintShown;       /* mp1: the line under Your name as last shown */
static long        g_panelTryShown      = -1;  /* mp1: whether Try again was last shown (-1 = not yet) */
static int         g_panelJoinGaveUp    = 0;   /* mp1: coopui::PanelJoinGaveUp at the last status build */
static std::string g_fPort, g_fNotebookAddr, g_fJoinAddr, g_fWorld, g_fSlot;
/* mp3 (design-mpmenu1 section 4) - the Host a game screen's worlds, read from disk when the screen opens and after a
   New world or a Delete, never per tick.  g_fWorld / g_fSlot are now SET from the picked world at Host (the boxes are
   gone), and g_panelNotebookMine stays 1: this computer's own helper, always. */
static std::vector<coopworld::WorldRow> g_worlds;
static std::vector<int> g_worldFresh;          /* parallel to g_worlds: 1 = the folder holds nothing but world.txt */
static std::string      g_worldSel;            /* the picked world's FOLDER name ("" = none) */
/* ui2: the worlds list is refilled only when g_worlds was re-read (PanelScanWorlds bumps g_worldListGen: the screen opening,
   New world, Delete), never on a push that only moves the pick.  -1 = the list on screen was just created. */
static long long        g_worldListGen = 0, g_worldListShown = -1;
static std::string      g_fNewWorld;           /* the New world box */
static std::string      g_dlgText;             /* what the open dialog says */
static std::string      g_nbWorld;             /* the folder of the world this computer's helper was started for ("" = none) */
static volatile LONG64  g_worldScans = 0, g_worldsListed = 0, g_worldsSkipped = 0, g_worldsCreated = 0, g_worldCreateFailed = 0;
static volatile LONG64  g_worldsDeleted = 0, g_worldDeleteRefused = 0, g_worldDeleteFailed = 0;
/* mp4 (design-mpmenu1 section 6) - the Hosting screen.  The address is read from Windows when the screen opens, never
   per tick; the players list is rebuilt with the status area (4 Hz) and pushed only when its words change. */
static std::string     g_hostHomeAddr;        /* this computer's home-network IPv4 ("" = none found) */
static std::string     g_hostCopyNote;        /* what the last Copy did, in words ("" = no Copy yet) */
static std::string     g_hostingPlayersShown; /* the players list as last pushed */
static volatile LONG64 g_hostingShown = 0, g_homeAddrFound = 0, g_homeAddrMissing = 0, g_addrCopied = 0, g_addrCopyFailed = 0;
/* The INTERNET ADDRESS row (owner 439 / 452): the address the home router reports, asked for on the router thread (upnp.cpp
   UpnpAddrAsk) when HOSTING opens; when the router gives none, the lookup websites (upnp.cpp UpnpLookupAsk), asked on SHOW or
   COPY.  Both answers are polled by the status build; the decisions are coopui's (panelstatus.h PanelNetAddrPress /
   PanelNetRouterAnswer / PanelNetLookupAnswer).  Hidden until SHOW, hidden again every time the window opens.  Never logged. */
static int             g_netAddrState = coopui::kNetAddrAsking;   /* coopui::kNetAddr* */
static std::string     g_netAddrIp;           /* the internet address ("" = none) */
static int             g_netAddrShown = 0;    /* 1 = SHOW pressed: the address is on screen */
static int             g_netAddrPending = coopui::kNetPressNone;   /* a SHOW / COPY press waiting for an address */
static long            g_netAddrAskId = 0;    /* the router thread's ask number (0 = none) */
static long            g_netAddrLookId = 0;   /* the lookup worker's ask number (0 = none) */
static int             g_netAddrPushed = 0;   /* 0 = the row's widgets need the state above */
static volatile LONG64 g_netAddrFound = 0, g_netAddrMissing = 0, g_netAddrShowPressed = 0;
static volatile LONG64 g_netAddrLookAsked = 0, g_netAddrLookFound = 0, g_netAddrLookFailed = 0;
/* mp5 (design-mpmenu1 section 5) - the Game options screen.  TITLE PUMP ONLY.  A row's value is the notebook's own text
   for it; `base` is what the screen opened with (the world's options.txt, else this game's own live value, else the
   design's default), and Done hands on only the rows that differ from it. */
static long        g_optTab = 0;
static std::string g_optWorld;                                    /* the world FOLDER the screen is showing */
static std::string g_optVal[coopui::kOptCount];
static std::string g_optBase[coopui::kOptCount];
static std::string g_optChosenWorld;                              /* the world the last Done was for ("" = none) */
static std::vector<std::pair<std::string, std::string> > g_optChosen;   /* ... and the rows it changed */
static volatile LONG64 g_optOpened = 0, g_optDoneCount = 0, g_optHandedOver = 0, g_optFileRead = 0;
static std::string g_panelStatus;
static long long   g_panelStatusSeq   = 0;
static long long   g_panelStatusShown = -1;
static bool        g_panelDefaultsDone = false;
static DWORD       g_panelLastMirrorMs = 0;
static long        g_panelFailStreak   = 0;
static long long   g_panelBuildLogs    = 0;
static const long  kPanelFailStreakCap = 5;    /* 6a lesson 14: a corrective that cannot work must give up */
static const DWORD kPanelMirrorMs      = 250;
/* U2: the status area is rebuilt at most four times a second.  The title pump runs at ~1,065 Hz (F655);
   building the string allocates and setCaption constructs a UString, so at that rate this would be
   thousands of allocations a second for a line nobody can read changing that fast (design 3.1). */
static const DWORD kPanelStatusMs      = 250;
static DWORD       g_panelLastStatusMs = 0;

/* U2: the three role numbers coopui uses must be the three coop::RoleId uses - a pure header may include
   neither, so the agreement is asserted here, at the one place both are visible.  A negative array size
   is a compile error, so they cannot drift apart unnoticed (config.cpp carries the same shape for
   coopcfg). */
typedef char CoopUiRoleValuesAgree[
    (coopui::kPanelRoleSingle == kRoleSingle && coopui::kPanelRoleHost == kRoleHost
     && coopui::kPanelRoleClient == kRoleClient) ? 1 : -1];

// ------------------------------------------------------------------------------------------------
// THE CLICK HANDLERS.  Two interlocked writes each, and nothing else, ever.  They run on MyGUI's input
// thread, OUTSIDE the fault guard (review-p7z M-5), so anything they touched would be unprotected and
// unordered against the tick.
// ------------------------------------------------------------------------------------------------
static void UiQueue(int code)
{
    ::InterlockedExchange(&g_uiAction, (LONG)code);
    ::InterlockedIncrement64(&g_uiActionsQueued);
}
static void OnMultiplayerClicked(MyGUI::Widget* /*sender*/)
{
    ::InterlockedIncrement64(&g_uiClicks);
    UiQueue(kActToggle);
}
static void OnCoopBackClicked(MyGUI::Widget*)         { UiQueue(kActBack); }    /* mp1: Back - a screen back, and off the Multiplayer screen it closes */
static void OnCoopGoClicked(MyGUI::Widget*)           { UiQueue(kActGo); }
static void OnCoopPasteClicked(MyGUI::Widget*)        { UiQueue(kActPaste); }
static void OnCoopCopyClicked(MyGUI::Widget*)         { UiQueue(kActCopy); }   /* mp4 */
static void OnNetAddrShowClicked(MyGUI::Widget*)      { UiQueue(kActNetShow); }   /* T-510 */
static void OnNetAddrCopyClicked(MyGUI::Widget*)      { UiQueue(kActNetCopy); }   /* T-510 */
static void OnCoopModeHostClicked(MyGUI::Widget*)     { UiQueue(kActModeHost); }
static void OnCoopModeJoinClicked(MyGUI::Widget*)     { UiQueue(kActModeJoin); }
/* ui2: the worlds list's pick (mouse or keys).  No index is carried: the tick reads the list's selected row back on the
   title pump, inside the guard, so this stays the same two interlocked writes as every other handler. */
static void OnCoopWorldListPicked(MyGUI::MultiListBox*, size_t) { UiQueue(kActWorldPick); }   /* mp3: the Host a game screen */
static void OnCoopNewWorldClicked(MyGUI::Widget*)     { UiQueue(kActNewWorld); }
static void OnCoopDeleteWorldClicked(MyGUI::Widget*)  { UiQueue(kActDeleteWorld); }
static void OnCoopDlgOkClicked(MyGUI::Widget*)        { UiQueue(kActDlgOk); }
static void OnCoopDlgCancelClicked(MyGUI::Widget*)    { UiQueue(kActDlgCancel); }
/* mp5: the Game options screen.  One handler per button, as everywhere here - the handler IS the row, so nothing has
   to be read back from the sender. */
#define COOP_OPT_CLICK(fn, code) static void fn(MyGUI::Widget*) { UiQueue(code); }
COOP_OPT_CLICK(OnCoopGameOptionsClicked, kActGameOptions)
COOP_OPT_CLICK(OnCoopOptTab0, kActOptTab0)     COOP_OPT_CLICK(OnCoopOptTab1, kActOptTab0 + 1)  COOP_OPT_CLICK(OnCoopOptTab2, kActOptTab0 + 2)
COOP_OPT_CLICK(OnCoopOptDec0, kActOptDec0)     COOP_OPT_CLICK(OnCoopOptDec1, kActOptDec0 + 1)  COOP_OPT_CLICK(OnCoopOptDec2, kActOptDec0 + 2)
COOP_OPT_CLICK(OnCoopOptDec3, kActOptDec0 + 3) COOP_OPT_CLICK(OnCoopOptDec4, kActOptDec0 + 4)  COOP_OPT_CLICK(OnCoopOptDec5, kActOptDec0 + 5)
COOP_OPT_CLICK(OnCoopOptDec6, kActOptDec0 + 6) COOP_OPT_CLICK(OnCoopOptDec7, kActOptDec0 + 7)  COOP_OPT_CLICK(OnCoopOptDec8, kActOptDec0 + 8)
COOP_OPT_CLICK(OnCoopOptInc0, kActOptInc0)     COOP_OPT_CLICK(OnCoopOptInc1, kActOptInc0 + 1)  COOP_OPT_CLICK(OnCoopOptInc2, kActOptInc0 + 2)
COOP_OPT_CLICK(OnCoopOptInc3, kActOptInc0 + 3) COOP_OPT_CLICK(OnCoopOptInc4, kActOptInc0 + 4)  COOP_OPT_CLICK(OnCoopOptInc5, kActOptInc0 + 5)
COOP_OPT_CLICK(OnCoopOptInc6, kActOptInc0 + 6) COOP_OPT_CLICK(OnCoopOptInc7, kActOptInc0 + 7)  COOP_OPT_CLICK(OnCoopOptInc8, kActOptInc0 + 8)
COOP_OPT_CLICK(OnCoopOptTick0, kActOptTick0)     COOP_OPT_CLICK(OnCoopOptTick1, kActOptTick0 + 1) COOP_OPT_CLICK(OnCoopOptTick2, kActOptTick0 + 2)
COOP_OPT_CLICK(OnCoopOptTick3, kActOptTick0 + 3) COOP_OPT_CLICK(OnCoopOptTick4, kActOptTick0 + 4) COOP_OPT_CLICK(OnCoopOptTick5, kActOptTick0 + 5)
COOP_OPT_CLICK(OnCoopOptTick6, kActOptTick0 + 6) COOP_OPT_CLICK(OnCoopOptTick7, kActOptTick0 + 7) COOP_OPT_CLICK(OnCoopOptTick8, kActOptTick0 + 8)
COOP_OPT_CLICK(OnCoopOptDefaults, kActOptDefaults)
COOP_OPT_CLICK(OnCoopOptDone, kActOptDone)
#undef COOP_OPT_CLICK
typedef void (*CoopClickFn)(MyGUI::Widget*);
static const CoopClickFn kOptTabFns[coopui::kOptTabCount] = { OnCoopOptTab0, OnCoopOptTab1, OnCoopOptTab2 };
static const CoopClickFn kOptDecFns[coopui::kOptRowsShown] = { OnCoopOptDec0, OnCoopOptDec1, OnCoopOptDec2, OnCoopOptDec3, OnCoopOptDec4, OnCoopOptDec5, OnCoopOptDec6, OnCoopOptDec7, OnCoopOptDec8 };
static const CoopClickFn kOptIncFns[coopui::kOptRowsShown] = { OnCoopOptInc0, OnCoopOptInc1, OnCoopOptInc2, OnCoopOptInc3, OnCoopOptInc4, OnCoopOptInc5, OnCoopOptInc6, OnCoopOptInc7, OnCoopOptInc8 };
static const CoopClickFn kOptTickFns[coopui::kOptRowsShown] = { OnCoopOptTick0, OnCoopOptTick1, OnCoopOptTick2, OnCoopOptTick3, OnCoopOptTick4, OnCoopOptTick5, OnCoopOptTick6, OnCoopOptTick7, OnCoopOptTick8 };
/* prof3: the Your profiles screen's buttons, one handler each, and the Hosting screen's Choose my profile. */
static void OnCoopProfListPicked(MyGUI::MultiListBox*, size_t) { UiQueue(kActProfPick); }   /* ui2: as the worlds list's */
/* ui2b - THE COLUMN HEADS' SORT.  A click on a head sorts the list by that column with MultiListBox's own compare, a plain
   text "<" unless requestOperatorLess is set (MyGUI_MultiListBox.h) - so LAST PLAYED sorted "2 days ago" before "yesterday"
   and capitals before lower case.  These compares replace it: LAST PLAYED by the time behind the words, newest first (the
   head's second click reverses it, MultiListBox's own swap), every other column by text with case ignored.  The words to a
   time map is rebuilt with the rows (PanelPushWorlds / PanelPushProfiles, the title pump - the same thread MyGUI sorts on);
   rows with the same words are equal, which is right because one wording covers one unbroken stretch of time (panelstatus.h,
   swept offline).  Never played / not played yet / unknown go below every played row. */
static std::map<std::string, long long> g_worldPlayedKey;   /* the worlds list's LAST PLAYED words -> the newest time behind them */
static std::map<std::string, long long> g_profPlayedKey;    /* the same for the profiles list */
static const long long kPlayedKeyFresh = -1, kPlayedKeyNone = -2, kPlayedKeyStray = -3;
static void UiPlayedKeyNote(std::map<std::string, long long>& m, const std::string& words, long long key)
{
    std::map<std::string, long long>::iterator it = m.find(words);
    if (it == m.end()) m[words] = key;
    else if (key > it->second) it->second = key;
}
static long long UiPlayedKey(const std::map<std::string, long long>& m, const MyGUI::UString& s)
{
    const char* p = s.asUTF8_c_str();
    std::map<std::string, long long>::const_iterator it = m.find(std::string(p != 0 ? p : ""));
    return it != m.end() ? it->second : kPlayedKeyStray;
}
static void UiListLess(const std::map<std::string, long long>& m, size_t playedCol, size_t col,
                       const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
{
    if (col == playedCol) { less = UiPlayedKey(m, a) > UiPlayedKey(m, b); return; }
    const char* pa = a.asUTF8_c_str();
    const char* pb = b.asUTF8_c_str();
    less = _stricmp(pa != 0 ? pa : "", pb != 0 ? pb : "") < 0;
}
static void OnCoopWorldListLess(MyGUI::MultiListBox*, size_t col, const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
{ UiListLess(g_worldPlayedKey, 1, col, a, b, less); }   /* WORLD / LAST PLAYED */
static void OnCoopProfListLess(MyGUI::MultiListBox*, size_t col, const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
{ UiListLess(g_profPlayedKey, 2, col, a, b, less); }    /* NAME / FACTION / LAST PLAYED */
static void OnCoopProfPlayClicked(MyGUI::Widget*)   { UiQueue(kActProfPlay); }
static void OnCoopProfNewClicked(MyGUI::Widget*)    { UiQueue(kActProfNew); }
static void OnCoopProfDeleteClicked(MyGUI::Widget*) { UiQueue(kActProfDelete); }
static void OnCoopChooseProfileClicked(MyGUI::Widget*) { UiQueue(kActChooseProfile); }
static void OnCoopHostProfChangeClicked(MyGUI::Widget*) { UiQueue(kActHostProfChange); }   /* T-201 PP6' */

/* ------------------------------------------------------------------------------------------------
   P8i-b / review-p8i H-2 - THE TITLE-BAR X.
   Kenshi_WindowCX's header carries <Button skin="Kenshi_CloseButtonSkin"> with UserString
   Event="close" (kenshi_templates.xml:355-357).  MyGUI::Window binds every skin child carrying that
   UserString to notifyPressedButtonEvent, which raises eventWindowButtonPressed with the string.  The
   X was drawn and pickable and did nothing - the affordance-the-code-ignores class this project
   discloses on sight, and design-ui-panel V1a requires it to close the panel.
   ------------------------------------------------------------------------------------------------ */
static void OnCoopWindowButton(MyGUI::Window* /*sender*/, const std::string& button)
{
    if (button != "close") return;      /* Kenshi_WindowCX has no other header button */
    ::InterlockedIncrement64(&g_panelXClosed);
    UiQueue(kActClose);
}

/* Belt and braces for ESC (the engine-side gate below is the one that must hold): if a widget of ours
   happens to own MyGUI's key focus, this fires first.  It cannot be the whole fix - the player may have
   clicked anywhere else, and then nothing of ours has key focus while the engine's ESC arm still runs.
   ui2b: it is subscribed on the panel window only; a picked list row hands the key focus back to it (UiFocusPanel). */
static void OnCoopPanelKey(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char /*ch*/)
{
    if (key == MyGUI::KeyCode::Escape) UiQueue(kActClose);
}

/* ------------------------------------------------------------------------------------------------
   P8i-b / review-p8i H-1 - ESC AT THE TITLE SCREEN MUST CLOSE THE PANEL, NOT QUIT KENSHI.

   THE ENGINE PATH, from build/decomp_82a460.txt (the front-end key-down handler at 0x82A460) and F537:

       MyGUI::InputManager::injectKeyPress(...)                  // MyGUI is fed the key FIRST
       if (keyCode == 1 && ...) {                                // scancode 1 is ESC
           if (DAT_1421322D0 != 0) {                             // the front-end screen is up
               if (thunk_FUN_140913BC0() == 0) DAT_142132408 = 1; // <-- the quit byte
           }
       }
       if (MyGUI::InputManager::isFocusKey()) { ...EditBox -> return... }   // the guard is DOWN HERE
       thunk_FUN_140360AD0(&DAT_142132320, keyCode);             // the general keybind dispatch

   Two things follow.  The EditBox focus guard that keeps every OTHER Kenshi hotkey from firing while
   the player types in our panel sits BELOW the ESC arm, so it does not cover ESC.  And the only thing
   between the ESC arm and the quit byte is 0x913BC0 = TitleScreen::closeTheOtherBits, which closes one
   of TitleScreen's OWN BaseLayout sub-panels and returns non-zero if it closed one.  Our panel is a
   widget we created under the title-art ImageBox; the engine cannot see it, so the call returned 0 and
   the byte went up.

   WHY WE HOOK 0x913BC0 AND NOT 0x82A460.  It is the smallest function on the arm's path; it is
   `bool TitleScreen::closeTheOtherBits()` (its address is our table row
   TitleScreen_closePanels, checked byte for byte at start - F020); and its contract is exactly the question we need to answer - "did
   anything close?".  Its other callers are TitleScreen::exitGame (0x9142B0, which raises the quit byte
   UNCONDITIONALLY afterwards, so the EXIT button is untouched by this) and the show-a-sub-panel
   routines 0x915FC0 / 0x916080 / 0x916530 / 0x916660, which ignore the return value - and having our
   panel close when the player opens NEW GAME or OPTIONS is the behaviour we want anyway.  There is no
   recursion: closeTheOtherBits calls 0x915FC0 only with show=0, and 0x915FC0 calls back only on show=1.

   THE ORIGINAL RUNS FIRST, ALWAYS, so the engine's own behaviour is unchanged; we only widen the
   answer.  Nothing here touches MyGUI or allocates: this runs on whichever thread the engine dispatches
   keys from, OUTSIDE UiTitleTickGuarded, exactly like the click handlers (review-p7z M-5).
   ------------------------------------------------------------------------------------------------ */
/* ui6 (decision 42) - ESC ON A LIVE BOX IS THAT BOX'S ANSWER, never Kenshi's quit.  Only reached when no panel is up (the
   panel, when open, hides the box and takes the ESC first).  Interlocked only, like the rest of the gate: the button is
   pressed by the title tick - CANCEL on MULTIPLAYER DATA DAMAGED, OK on CAN'T START MULTIPLAYER and on the error box.
   Not gated on the key being physically down: a tap already released when the engine dispatches it would otherwise quit
   the game over a box, and while a box is live the menu clicks that also call closeTheOtherBits are blocked. */
static int UiBoxEscFromEngine()
{
    const LONG live = ::InterlockedCompareExchange(&g_boxLive, 0, 0);
    if (live == 0) return 0;
    ::InterlockedExchange(&g_boxEscPending, live);
    ::InterlockedIncrement64(&g_boxEscAnswered);
    return 1;
}

static int UiCloseFromEngine(int engineClosed)
{
    /* P8i-c / F751 L-4 - WE MAY ONLY ANSWER "SOMETHING CLOSED" IF SOMETHING WAS ACTUALLY ON SCREEN.
       This return value is the whole gate on the engine's quit byte (F739/F740): answering 1 for a panel
       that was WANTED but never DRAWN swallows an ESC the player meant for the game - which is exactly
       what happened on a screen too small for the panel, where g_panelWanted stayed 1 for ever.
       g_panelBuiltOk is raised as the LAST step of a successful build and lowered on destroy - and (ui5c
       fold F2) by the title tick when it finds no panel widget with the flag up (the title screen was
       destroyed with the panel on it, e.g. a game load), and (F3) when the UI shuts itself off after a
       fault - so it is the interlocked answer to "is there a panel Window up?" - and it is the only one
       available on this thread, which may not touch MyGUI at all. */
    if (::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) == 0) return engineClosed ? 0 : UiBoxEscFromEngine();   /* ui6: no panel - a live box, or the game's; review-ui6 #1: only when Kenshi closed nothing of its own, so one ESC never also answers a box the player has not seen */
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0) return engineClosed ? 0 : UiBoxEscFromEngine();
    /* The same interlocked write kActClose performs.  Done here rather than through the action slot so
       a queued click cannot be overwritten, and so the answer we return is already true. */
    ::InterlockedExchange(&g_panelWanted, 0);
    /* P8i-c / F751's second LOW - THE ORDER OF THESE TWO INCREMENTS IS LOAD-BEARING.  The title pump's
       drain reads panelEngineClosed first and panelEscClosed second; with engineClosed raised first, a
       drain landing between the two saw an engine close with no ESC beside it and logged the wrong
       sentence for a key that had in fact been pressed.  Raised the other way round, a drain in between
       sees escClosed ahead of engineClosed, its outer loop does not run at all, and the next tick logs
       both correctly.  Inferred, and it only chooses which sentence gets logged: inside the ESC arm the
       key is still physically down, and no mouse-driven caller of closeTheOtherBits has it down. */
    if ((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) ::InterlockedIncrement64(&g_panelEscClosed);
    ::InterlockedIncrement64(&g_panelEngineClosed);
    return 1;
}

/* ::TitleScreen, fully qualified on purpose: an unqualified name here would declare an incomplete
   coop::TitleScreen if the include above ever moved, and that is the C2668 class this project keeps
   hitting. */
static bool (*orig_titleCloseOtherBits)(::TitleScreen*) = 0;

static bool detour_titleCloseOtherBits(::TitleScreen* thisptr)
{
    /* T-461: the REPORT A BUG window (or one of its boxes) is on top of everything, so ESC is its CANCEL / BACK / OK, never Kenshi's
       quit and never the panel's close; nothing under it is closed by the same press */
    if (BugReportTakesEscape() != 0) return true;
    bool engine = false;
    if (orig_titleCloseOtherBits != 0) engine = orig_titleCloseOtherBits(thisptr);
    const int ours = UiCloseFromEngine(engine ? 1 : 0);
    return engine || ours != 0;
}

void InstallUiTitleClose()
{
    const intptr_t target = (intptr_t)coop::AddrAbs(kMig3CloseTheOtherBits);
    if (target == 0)
    {
        ErrorLog("[UI] TitleScreen::closeTheOtherBits resolved to 0 - the ESC gate is NOT installed, so"
                 " Escape at the title screen still quits Kenshi with the panel open.");
        return;
    }
    const coop::HookStatus st = coop::AddHook((void*)target,
                                                        (void*)&detour_titleCloseOtherBits,
                                                        (void**)&orig_titleCloseOtherBits);
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[UI] ESC gate: TitleScreen::closeTheOtherBits at 0x" << std::hex << std::uppercase
       << (unsigned long long)target << std::dec
       << " (expect game RVA 0x913BC0 + module base) AddHook "
       << (st == coop::SUCCESS ? "SUCCESS - ESC closes the panel instead of quitting"
                                    : "FAILED - ESC still quits with the panel open");
    if (st == coop::SUCCESS) DebugLog(ss.str()); else ErrorLog(ss.str());
}

// ------------------------------------------------------------------------------------------------
// LOOKUPS.  Every one is by name, on the frame that uses it, with the NON-THROWING cast and a null test.
// A widget that is not there and a widget that is there but is not the type we asked for are different
// facts and get different numbers (6a lesson 1).
// ------------------------------------------------------------------------------------------------
static MyGUI::Widget* UiFind(MyGUI::Gui* gui, const std::string& name)
{
    MyGUI::Widget* w = gui->findWidgetT(name, false);
    if (w == 0) ::InterlockedIncrement64(&g_panelMissing);
    return w;
}
static MyGUI::EditBox* UiEdit(MyGUI::Gui* gui, const std::string& name)
{
    MyGUI::Widget* w = UiFind(gui, name);
    if (w == 0) return 0;
    MyGUI::EditBox* e = w->castType<MyGUI::EditBox>(false);
    if (e == 0) ::InterlockedIncrement64(&g_panelCastNull);
    return e;
}
static MyGUI::Button* UiBtn(MyGUI::Gui* gui, const std::string& name)
{
    MyGUI::Widget* w = UiFind(gui, name);
    if (w == 0) return 0;
    MyGUI::Button* b = w->castType<MyGUI::Button>(false);
    if (b == 0) ::InterlockedIncrement64(&g_panelCastNull);
    return b;
}
/* ui2: the two lists (MultiListBox, the widget type of Kenshi_LoadGamePanel.layout's GamesList), and the row picked in one.
   The index is the row's INSERTION index - MultiListBox keeps those when the player sorts by a column head (MyGUI_MultiListBox.h:
   "All indexes used here is indexes of unsorted Multilist") - so it indexes g_worlds / g_profList as the list was filled. */
static MyGUI::MultiListBox* UiMulti(MyGUI::Gui* gui, const std::string& name)
{
    MyGUI::Widget* w = UiFind(gui, name);
    if (w == 0) return 0;
    MyGUI::MultiListBox* m = w->castType<MyGUI::MultiListBox>(false);
    if (m == 0) ::InterlockedIncrement64(&g_panelCastNull);
    return m;
}
static size_t UiListPicked(MyGUI::Gui* gui, const std::string& name)
{
    MyGUI::MultiListBox* m = UiMulti(gui, name);
    return m != 0 ? m->getIndexSelected() : MyGUI::ITEM_NONE;
}
/* ui2b: a clicked list takes MyGUI's key focus, and Escape is listened for on the panel window only (OnCoopPanelKey), so
   after a pick Escape no longer closed the panel.  The tick hands the key focus back to the window once a pick is handled. */
static void UiFocusPanel(MyGUI::Gui* gui)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* w = UiFind(gui, NM().panel);
    if (im != 0 && w != 0) im->setKeyFocusWidget(w);
}

static std::string UiTextOf(MyGUI::EditBox* e, size_t cap)
{
    if (e == 0) return std::string();
    /* getCaption() returns a const UString& and asUTF8_c_str() a const char* - both references, nothing
       crosses the module boundary by value.  design-ui-panel names getOnlyText(), which returns a UString
       BY VALUE, and review-p7z Q1(a)'s finding is that nothing in this plugin does that. */
    const char* p = e->getCaption().asUTF8_c_str();
    if (p == 0) return std::string();
    return coopcfg::CfgSanitiseTyped(std::string(p), cap);
}

static void PanelSetStatus(const std::string& s)
{
    g_panelStatus = s;
    ++g_panelStatusSeq;
}

/* T-201 N1 - THE PRESS IN PROGRESS (HOST / JOIN pressed, the world not yet answered). Module state, never a widget. */
static int         g_pressBusy       = 0;   /* 0 idle, 1 HOST, 2 JOIN */
/* T-201 PP6' - THE ONE PRESS CARRIES ON TO THE LOAD (player-path-plan.md PP6 + the one-press revision). After the world answered
   (HOST) or PLAY (JOIN): 1 = waiting for the world to admit the picked profile (its WELCOME, the save folder decided) - CANCEL
   leaves; 2 = decided, Loading "<profile>"... / Opening NEW GAME for "<profile>"... on screen and CANCEL greyed; 3 = the panel was
   told to close - the load is posted, or the NEW GAME window opened, at the tail of a title frame once the panel is gone. */
static int         g_loadStage       = 0;
static int         g_loadMode        = 0;   /* 1 HOST, 2 JOIN */
static DWORD       g_loadAtMs        = 0;
static int         g_loadVerdict     = -1;  /* coopprof::AutoLoadVerdict once decided */
static std::string g_loadFolder, g_loadProfile;
static int         g_loadTries       = 0;   /* tail attempts that could not post / find NEW GAME yet */
static int         g_loadHeld        = 0;   /* 1 = the join gate HELD this press's load for the world's operator (StoreJoinHoldPoll says when it goes on) */
static int         g_loadNewAsked    = 0;   /* HOST: the new profile named after the player was asked for */
static int         g_profMode        = 0;   /* PROFILES' main button: 0 PLAY, 1 SELECT (opened by HOST GAME's CHANGE - nothing starts) */
static unsigned    g_hostProfNum     = 0;   /* HOST GAME: the profile SELECT chose in g_hostProfWorld, 0 = none (the automatic pick) */
static std::string g_hostProfNew, g_hostProfWorld;   /* ... or the name NEW PROFILE gave it (made at the press); the world folder they belong to */
static int         g_profPrefillDone = 0;   /* JOIN: the NEW PROFILE box opened pre-filled once for this connection */
static void LoadBegin(int mode);
static const coopworld::WorldRow* PanelPickedRow();
static int PanelHostProfChoice(coopprof::HostProfileChoice* c);
static void PanelProfSelectLoad();
/* T-201 PP6' fold (review HIGH 1): SELECT mode ends on EVERY panel close and open - X, Escape, MULTIPLAYER, the engine's close, a box -
   not only on SELECT / OK / BACK; a JOIN after it sees PLAY and the world's list again. */
static void PanelProfModeEnd(const char* why)
{
    if (g_profMode == 0) return;
    g_profMode = 0;
    DebugLog(std::string("[UI] T-201 PP6': PROFILES leaves SELECT mode (") + why + ") - its main button is PLAY again");
}
static DWORD       g_pressAtMs       = 0;
static DWORD       g_pressLinkedAtMs = 0;   /* JOIN: when the world-server link first came up after the press, 0 = not yet */
static long long   g_pressArmGen     = 0;   /* ConfigArmGen() at the press */
static int         g_pressBoxReturn  = 0;   /* 1 / 2: the screen a failure box's OK returns to */
static int         g_nameTakenBox    = 0;   /* T-201 N1b (owner 166): the box up is the name-taken CAN'T JOIN - its OK returns to JOIN GAME */
static int         g_nameFocusWanted = 0;   /* T-201 N1b: JOIN GAME's PLAYER NAME box takes the key focus once the panel is up */
static volatile LONG64 g_pressStarted = 0, g_pressDone = 0, g_pressFailed = 0, g_pressCancelled = 0, g_pressIgnored = 0;
static std::string g_noticeTitle;           /* T-201 N1: the error box's title when its raiser names one ("" = coopui::NoticeTitle) */

/* U2 / review-p8i M-2 - PUT A SENTENCE WHERE THE PLAYER CAN SEE IT WHEN THERE IS NO PANEL.
   Raised on the title pump; the widget is built and destroyed by the tick, exactly like the panel. */
static void UiRaiseNotice(const std::string& s)
{
    g_noticeTitle.clear();
    g_noticeText = s;
    g_nameTakenBox = coopui::NoticeIsNameTaken(s) ? 1 : 0;   /* T-201 N1b */
    ++g_noticeSeq;
    g_noticeFailStreak = 0;
    ::InterlockedExchange(&g_noticeWanted, 1);
}
static void UiRaiseNoticeTitled(const std::string& s, const char* title)   /* T-201 N1: CAN'T HOST / CAN'T JOIN by name */
{
    UiRaiseNotice(s);
    g_noticeTitle = title ? title : "";
}
static void UiClearNotice()
{
    ::InterlockedExchange(&g_noticeWanted, 0);
    g_noticeFailStreak = 0;
}

/* PP3d (owner-approved 2026-09-27) - THE IDENTITY BOX on the title screen: CAN'T START MULTIPLAYER (kind 1, OK) or MULTIPLAYER DATA
   DAMAGED (kind 2, CANCEL + CONTINUE AS NEW PLAYER), shown instead of HOST / JOIN while the identity is unusable. It takes
   precedence: raising it closes the panel first and takes the plain error box down; opening the panel takes it down (CANCEL).
   Built and destroyed by the title tick like the error box; a pressed button is only RECORDED by its handler and acted on by the
   tick (a widget is never destroyed inside its own click). */
static volatile LONG g_idBoxWanted = 0;   /* 0 none, else the kind the box shows */
static volatile LONG g_idBoxButton = 0;   /* coopdata::kIdBox*: pressed, not yet handled */
static int  g_idBoxShownKind = 0;          /* the kind of the box on screen (0 none) */
static int  g_idBoxPreview = 0;            /* TEST-ONLY uipreview identity1/2: its buttons only close it */
static long g_idBoxFailStreak = 0;
static void IdBoxRaise(int kind, int preview)
{
    UiClearNotice();
    ::InterlockedExchange(&g_panelWanted, 0);
    g_idBoxPreview = preview;
    g_idBoxFailStreak = 0;
    ::InterlockedExchange(&g_idBoxButton, 0);
    ::InterlockedExchange(&g_idBoxWanted, (LONG)kind);
}
static void IdBoxClear()
{
    ::InterlockedExchange(&g_idBoxWanted, 0);
    ::InterlockedExchange(&g_idBoxButton, 0);
    g_idBoxPreview = 0;
}
/* PP3d: HOST / JOIN pressed - true = refused (the panel closes and the box shows). */
static bool IdBoxRefuseHostJoin(const char* what)
{
    const int kind = IdentitySessionOnlyKind();
    if (!coopdata::IdentityRefusesHostJoin(kind)) return false;
    ErrorLog(std::string("[UI] PP3d: ") + what + " refused - this game's identity is " + (kind == 1 ? "UNREADABLE" : "DAMAGED")
             + " (session-only). The panel is closed and the " + coopui::IdBoxTitle(kind) + " box is shown; nothing was started.");
    IdBoxRaise(kind, 0);
    return true;
}

/* mp1 - THE FIRST LINES OF THE HOST AND JOIN SCREENS' STATUS AREA: what to do on this screen, in plain words. */
static void PanelIntro()
{
    PanelSetStatus(coopui::PressIntroText(g_panelScreen == 2 ? 1 : 0));   /* ui5: panel-mockups.md sections 3 and 7; T-201 N1: pure, swept */
}

/* mp3 - F874: A GAME THAT HAS USED ONE WORLD CANNOT MOVE TO ANOTHER WITHOUT A RESTART (ConfigRearmFromFile refuses the
   switch once a record index is read, and this computer's helper, once started, keeps its world).  "Used" is the world
   whose index was read, else the one the helper was started for.  The Host a game screen says so in one line. */
static std::string PanelUsedWorld()
{
    const std::string idx = StoreIndexWorld();
    if (!idx.empty()) return coopworld::WorldFolderName(idx);
    return g_nbWorld;
}
static std::string PanelSwitchNote()
{
    const std::string used = PanelUsedWorld();
    if (used.empty() || g_worldSel.empty() || used == g_worldSel) return std::string();
    return "Restart Kenshi to host a different world.";
}

/* T-201 PP6' (owner 178a): CHANGE's DELETE edits the picked world's profiles.txt from this game - only while no helper of this game holds
   that world (a notebook running it would write its own list back over the edit). T-220: a helper for that world in ANOTHER process is
   not visible here - StoreProfileDeleteOffline refuses while that helper holds the world lock (coopprof::kWorldLockFile). */
static bool PanelProfOfflineEditable()
{
    const std::string used = PanelUsedWorld();
    return !g_hostProfWorld.empty() && (used.empty() || coopworld::WorldFolderName(used) != g_hostProfWorld);
}

static void PanelDefaults()
{
    PanelProfModeEnd("the panel opens");
    if (g_panelDefaultsDone) return;
    g_panelDefaultsDone = true;
    g_fPort         = "7777";   /* ui5d: coopui::kPanelDefaultGamePort - change together */
    g_fNotebookAddr = "127.0.0.1:27016";
    g_fJoinAddr     = "";
    g_fWorld        = ConfigFileWorld(); if (g_fWorld.empty()) g_fWorld = coopworld::kDefaultWorld;   /* owner decision 249 a: "New World" */   /* W3-f (review-w3 item 4, M5): the FILE's world, not ConfigWorldKey() - a world adopted from a WELCOME is never written into shared_wastelands.cfg by a panel save */
    g_fSlot.clear();   /* T-201 PP5 fold (L2): the panel has no save-name box - HOST and JOIN write no slot= and every multiplayer save goes to
                          the profile's own folder (store.cpp), so a profile folder's long name is never copied here to fail the 16-character check */
    g_fName         = ConfigFilePlayerName();   /* mp1: remembered across a restart - the file is the memory */
    /* PP3b (manager decision 2026-09-27): session.cfg arms nothing at start; it only PRE-FILLS these boxes. A game armed at
       start (the harness's test file) still shows what it armed with. */
    int pRole = ConfigRole(); std::string pAddr = ConfigHostAddr(); unsigned short pPort = ConfigHostPort();
    {
        coopcfg::CfgFields pf;
        if (pRole == kRoleSingle && ConfigPanelPrefill(&pf)) { pRole = pf.role; pAddr = pf.hostAddr; pPort = pf.hostPort; }
        if (pRole != kRoleClient) { const std::string last = ConfigLastJoinAddr(); if (!last.empty()) g_fJoinAddr = last; }
    }
    if (pPort != 0)
    {
        /* If this game already has a settings file, show what it says rather than the defaults - the
           player is editing, not starting from nothing.  A live read, at the moment it is needed. */
        if (pRole == kRoleClient)
        {
            g_panelMode = 1;
            g_fJoinAddr = pAddr;
            if (!g_fJoinAddr.empty())
            {
                char b[24];
                _snprintf(b, 23, ":%u", (unsigned)pPort);
                b[23] = 0;
                g_fJoinAddr += b;
            }
        }
        else if (pRole == kRoleHost)
        {
            char b[24];
            _snprintf(b, 23, "%u", (unsigned)pPort);
            b[23] = 0;
            g_fPort = b;
        }
    }
    PanelIntro();
}

/* U2 - THE LIVE BLOCK.  Every reader is called HERE, at the moment the line is built, and none is
   cached (design 3.1).  All of them are MAIN-THREAD-ONLY calls and the title pump is the main thread.
   The DECISION - which of the five states this is, and what sentence it gets - is not taken here: it is
   coopui::PanelStatusText, pure, and the offline suite sweeps every branch of it. */
static std::string PanelLiveBlock()
{
    coopui::PanelLinkFacts f;
    f.role            = ConfigRole();
    f.worldLinked     = StoreLinkIsUp() ? 1 : 0;   /* the world server is every game's one door */
    { std::vector<std::string> others; const int n = StoreRosterNames(&others); f.peers = n > 0 ? n : 0; }   /* the other players on the world server's roster */
    /* StoreWelcomedThisLink() compares generations and can answer 1 for a link that has since dropped
       (store.h), so "the notebook has answered" is the AND of the welcome and the live link. */
    f.notebookUp      = (StoreWelcomedThisLink() != 0 && StoreLinkIsUp()) ? 1 : 0;
    f.dials           = ConfigDialCount();
    f.dialCap         = ConfigDialCap();
    f.mySlot          = StoreMySlot();
    f.worldKeyMismatch= ConfigWorldKeyMismatches();

    ::InterlockedExchange(&g_panelStatusState, (LONG)coopui::PanelLinkStateOf(f));

    const std::string addr = (f.role == kRoleHost) ? std::string("0.0.0.0") : ConfigHostAddr();
    std::string out = coopui::PanelStatusText(f, addr, (int)ConfigHostPort(), TimeModeName());
    g_panelJoinGaveUp = coopui::PanelJoinGaveUp(f);
    /* mp1 (design-mpmenu1 section 7): WHY THE SHARED WORLD SAID NO, said here on every build of the line rather than
       once into the intro, so it cannot be overwritten by moving between screens.  The mods sentence is store.cpp's. */
    {
        long long modsSeq = 0;
        const std::string r = coopui::PanelRefusalText(StoreRefusedReason(), StoreModsRefusal(&modsSeq), f.role == kRoleHost ? 1 : 0);   /* T-201 N1b */
        if (!r.empty()) out += "\n\n" + r;
    }

    /* The notebook process is a fact about a PROCESS, not about the link, so it gets its own line
       rather than being folded into a sentence that would then have to be true of both. */
    if (g_nbState != coopui::kNbNotOurs)
    {
        int shown = g_nbState;
        if (shown == coopui::kNbStarting && f.notebookUp != 0) shown = coopui::kNbRunning;
        const std::string nb = coopui::PanelNotebookText(shown, g_nbPort, g_nbExitCode);
        if (!nb.empty()) out += "\n" + nb;
    }
    return out;
}

/* T-510 - THE INTERNET ADDRESS ROW'S WIDGETS from g_netAddr*: the value (the mask, the address after SHOW, or a sentence),
   SHOW / HIDE, and both buttons greyed once no lookup website answered (coopui::PanelNetAddrButtonsOn). */
static int PanelHostingGamePort();
static const coopui::PanelGeom& PanelGeomNow();
/* The value as shown: wrapped to the value's width at the panel's fallback character width (one line or two -
   PanelLayout gives the row one row or two by it). */
static std::string PanelNetAddrShownText()
{
    const coopui::PanelGeom& g = PanelGeomNow();
    const std::string line = coopui::PanelNetAddrLine(g_netAddrState, g_netAddrIp, PanelHostingGamePort(), g_netAddrShown, g_netAddrPending);
    if (g.rowH <= 0) return line;
    return coopui::PanelWrapText(line, coopui::PanelNetAddrValueW(g.innerW, coopui::PanelGapOf(g.rowH)) / coopui::kPanelTextCharW);
}
static void PanelNetAddrPush(MyGUI::Gui* gui)
{
    const UiNames& n = NM();
    MyGUI::Widget* w = UiFind(gui, n.netAddr);
    MyGUI::TextBox* t = w ? w->castType<MyGUI::TextBox>(false) : 0;
    if (t) t->setCaption(MyGUI::UString(PanelNetAddrShownText().c_str()));
    else if (w) ::InterlockedIncrement64(&g_panelCastNull);
    const bool on = coopui::PanelNetAddrButtonsOn(g_netAddrState) != 0;
    MyGUI::Button* b = UiBtn(gui, n.netShowBtn);
    if (b) { b->setCaption(MyGUI::UString(coopui::PanelNetAddrShowCaption(g_netAddrState, g_netAddrIp, g_netAddrShown).c_str())); b->setEnabled(on); }
    b = UiBtn(gui, n.netCopyBtn);
    if (b) b->setEnabled(on);
    g_netAddrPushed = 1;
}
static void PanelCopy(int netRow);
/* One step of the row's decisions (coopui::PanelNetStep) carried out: the websites asked, the address shown, SHOW / HIDE
   flipped, the address copied.  A website ask whose worker cannot be started is answered "none" at once. */
static void PanelNetAddrDo(const coopui::PanelNetStep& s)
{
    g_netAddrState = s.state;
    g_netAddrPending = s.pending;
    g_netAddrPushed = 0;
    if (s.startLookup != 0)
    {
        g_netAddrLookId = coop::UpnpLookupAsk();
        ::InterlockedIncrement64(&g_netAddrLookAsked);
        DebugLog("[UI] internet address: the router gave none - asking the lookup websites (" + std::string(g_netAddrPending == coopui::kNetPressCopy ? "COPY" : "SHOW") + " pressed)");
        if (g_netAddrLookId <= 0)
        {
            ::InterlockedIncrement64(&g_netAddrLookFailed);
            DebugLog("[UI] internet address: not found by the lookup websites (the lookup worker could not be started)");
            PanelNetAddrDo(coopui::PanelNetLookupAnswer(g_netAddrState, g_netAddrPending, 0));
            return;
        }
    }
    if (s.showNow != 0) g_netAddrShown = 1;
    if (s.toggleShown != 0) g_netAddrShown = g_netAddrShown != 0 ? 0 : 1;
    if (s.copyNow != 0) PanelCopy(1);
}
/* THE ROUTER'S AND THE WEBSITES' ANSWERS, picked up by the HOSTING status build (4 Hz): one [UI] line when an ask finishes -
   found (and from which source) or not found and why, NEVER the address (logs go into bug reports). */
static void PanelNetAddrPoll(MyGUI::Gui* gui)
{
    if (g_netAddrState == coopui::kNetAddrAsking)
    {
        std::string ip, why;
        const int got = (g_netAddrAskId > 0) ? coop::UpnpAddrResult(g_netAddrAskId, &ip, &why) : 2;
        if (got != 0 && g_netAddrAskId <= 0) why = "the router thread could not be started";
        if (got == 1)
        {
            g_netAddrIp = ip;
            ::InterlockedIncrement64(&g_netAddrFound);
            DebugLog("[UI] internet address: found (the router)");
        }
        else if (got != 0)
        {
            g_netAddrIp.clear();
            ::InterlockedIncrement64(&g_netAddrMissing);
            DebugLog("[UI] internet address: not found by the router (" + why + ")");
        }
        if (got != 0) PanelNetAddrDo(coopui::PanelNetRouterAnswer(g_netAddrState, g_netAddrPending, got == 1 ? 1 : 0));
    }
    else if (g_netAddrState == coopui::kNetAddrLooking)
    {
        std::string ip, source, why;
        const int got = coop::UpnpLookupResult(g_netAddrLookId, &ip, &source, &why);
        if (got == 1)
        {
            g_netAddrIp = ip;
            ::InterlockedIncrement64(&g_netAddrLookFound);
            DebugLog("[UI] internet address: found (" + source + ")");
        }
        else if (got != 0)
        {
            g_netAddrIp.clear();
            ::InterlockedIncrement64(&g_netAddrLookFailed);
            DebugLog("[UI] internet address: not found by the lookup websites (" + why + ") - SHOW / COPY greyed until the window opens again");
        }
        if (got != 0) PanelNetAddrDo(coopui::PanelNetLookupAnswer(g_netAddrState, g_netAddrPending, got == 1 ? 1 : 0));
    }
    if (g_netAddrPushed == 0) PanelNetAddrPush(gui);
}

/* mp4 - THE HOSTING SCREEN'S STATUS AREA AND PLAYERS LIST.  The words are coopui's (pure, swept offline).  Only one
   friend can be connected until the many-players work, so the one connected game carries the one HELLO name. */
static std::string PanelHostingStatus(MyGUI::Gui* gui)
{
    PanelNetAddrPoll(gui);   /* first: an answer that completes a waiting COPY sets the copy note read below */
    std::string s = coopui::PanelHostingText(ConfigRole() == kRoleHost ? 1 : 0, 0 /* a busy port stops the world server: g_nbState says it */,
                                             g_nbState, (int)ConfigHostPort());
    if (!g_hostCopyNote.empty()) s += "\n\n" + g_hostCopyNote;
    std::vector<std::string> friends;
    if (StoreRosterNames(&friends) < 0)   /* M11a S1: the world server's PLAYERS roster names every player; with none on this link, the session peer as before */
    {
        const unsigned int peers = net::SessionLinked() ? net::SessionPeerCount() : 0u;
        for (unsigned int i = 0; i < peers; ++i) friends.push_back(i == 0 ? net::SessionPeerName() : std::string());
    }
    const std::string pl = coopui::PanelPlayersText(coopcfg::CfgTrim(g_fName), friends);
    /* ui5d: PanelPushStatus runs PanelLayout just after this, which sizes the PLAYERS area to the list (ui7: its measured height). */
    if (pl != g_hostingPlayersShown)
    {
        MyGUI::EditBox* e = UiEdit(gui, NM().playersBox);
        if (e) { e->setCaption(MyGUI::UString(pl.c_str())); g_hostingPlayersShown = pl; }
    }
    return s;
}

/* prof3: the Your profiles screen's status area - the world's last answer, then the cap line. */
static std::string PanelProfStatus()
{
    std::string s = coopui::PanelProfileCapLine((unsigned)g_profList.size(), g_profCapUi);
    if (!g_profSay.empty()) s = s.empty() ? g_profSay : g_profSay + "\n\n" + s;
    if (g_profList.empty() && s.empty()) s = "You have no profile in this world yet. Press NEW PROFILE to make one.";   /* ui2b: caps as the button */
    return s;
}

/* ui5c: the status area's text as last set - PanelLayout sizes the area to it (PanelLiveTextHalves). */
static std::string g_panelStatusCaption;
/* ui7 fold (review-ui7 #3): 1 from PanelPush's own layout (the status widget still holds the previous screen's text) until
   PanelPushStatus sets the caption - the status slot is not logged in between, so its once-per-build-and-screen line
   records the screen's real text. */
static int g_uiStatusCaptionStale = 0;
/* ui5d: HOSTING's router help as last set (PanelPush) - PanelLayout sizes its area to it, and the PLAYERS area to
   g_hostingPlayersShown ("\x01" = not pushed since the last PanelPush: the list's value). */
static std::string g_hostingHelpShown;
static void PanelLayout(MyGUI::Gui* gui);   /* ui5b: below - the hint's row follows the hint; ui5c: the status area its text */

/* `force` is set on any frame that ran an action or rebuilt the panel; otherwise this is throttled, and
   the throttle is on COST, not a wait for an event - the condition is re-asked every tick. */
static void PanelPushStatus(MyGUI::Gui* gui, int force)
{
    const DWORD now = ::GetTickCount();
    if (force == 0 && g_panelStatusShown == g_panelStatusSeq
        && (DWORD)(now - g_panelLastStatusMs) < kPanelStatusMs) return;
    MyGUI::EditBox* s = UiEdit(gui, NM().status);
    if (s == 0) return;
    g_panelLastStatusMs = now;
    const std::string live = PanelLiveBlock();   /* T-201 N1: still read on every build - the verdict state and the report use it */
    std::string text = (g_panelScreen == 1 || g_panelScreen == 2) ? g_panelStatus   /* T-201 N1: HOST GAME / JOIN GAME say only their own line; failures are boxes */
                     : (g_panelStatus.empty() ? live : g_panelStatus + "\n\n" + live);   /* ui5: no blank first line */
    if (g_panelScreen == 1) { const std::string sw = PanelSwitchNote(); if (!sw.empty()) text = sw + "\n\n" + text; }   /* mp3: F874, first */
    if (g_panelScreen == 5) text = PanelHostingStatus(gui);   /* mp4: the live block above still ran (the verdict state) */
    if (g_panelScreen == 5 && StoreProfilesWaiting()) text = "Press CHOOSE PROFILE to select a profile.\n\n" + text;   /* prof3 */
    if (g_panelScreen == 7) text = PanelProfStatus();   /* prof3: the cap line and the world's answers */
    s->setCaption(MyGUI::UString(text.c_str()));
    g_panelStatusCaption = text;
    g_uiStatusCaptionStale = 0;   /* ui7 fold (review-ui7 #3): the layout below measures - and logs - this screen's text */
    PanelLayout(gui);   /* ui5c: the status area as tall as this text (ui5d: and PLAYERS as tall as its list) - acts only when a measured height changed (ui7) */
    g_panelStatusShown = g_panelStatusSeq;
    /* T-201 N1: TRY AGAIN is gone - a join that gets no answer is a CAN'T JOIN box, and JOIN works again after its OK. */
}

/* mp1 (design-mpmenu1 section 3): Host a game and Join a game wait until Your name passes coopworld::DisplayNameOk -
   the one rule the settings file's reader applies to playername= - and the line under the box says why they wait.
   Widgets are touched only when the answer or the sentence changed. */
/* ui5b: PanelLayout (below) is declared above PanelPushStatus (ui5c), which runs it too. */
/* T-201 N1b: HOST / JOIN's own conditions (no press in progress, not linked on JOIN GAME, a picked world on HOST GAME) -
   PanelNameCheck adds the name's. */
static bool PanelGoAllowed()
{
    const bool joinedLive = (g_panelMode == 1 && g_panelScreen == 2 && ConfigRole() == kRoleClient && StoreWelcomedThisLink() != 0 && StoreLinkIsUp());
    return g_pressBusy == 0 && !joinedLive && (g_panelScreen != 1 || !g_worldSel.empty());
}
static void PanelNameCheck(MyGUI::Gui* gui)
{
    const std::string name = coopcfg::CfgTrim(g_fName);
    std::string why;
    const long ok = coopworld::DisplayNameOk(name, &why) ? 1 : 0;
    std::string hint;
    if (ok == 0) hint = name.empty() ? std::string("Enter a player name to host or join.") : why;
    if (ok == g_panelNameOkShown && hint == g_panelNameHintShown) return;
    const UiNames& n = NM();
    MyGUI::Button* b;
    b = UiBtn(gui, n.goBtn); if (b) b->setEnabled(ok != 0 && PanelGoAllowed());   /* T-201 N1b (owner 166): HOST / JOIN wait for the name */
    MyGUI::EditBox* e = UiEdit(gui, n.nameHint);
    if (e) e->setCaption(MyGUI::UString(hint.c_str()));
    g_panelNameOkShown = ok;
    g_panelNameHintShown = hint;
    PanelLayout(gui);   /* ui5b: the hint's row appears / goes with its sentence */
}

/* mp1: the name is kept when Host a game / Join a game is pressed - trimmed, checked by the same rule, and written
   to shared_wastelands.cfg only when it changed.  0 = the name is not acceptable (the press is ignored and the hint says
   why).  A write that fails does not stop the player: *err says why, and the name is typed again next time. */
static int PanelNameCommit(std::string* err)
{
    const std::string name = coopcfg::CfgTrim(g_fName);
    if (!coopworld::DisplayNameOk(name, 0)) { g_panelNameOkShown = -1; return 0; }
    g_fName = name;
    if (!ConfigWritePlayerName(name, err))
        ErrorLog("[UI] mp1: the name could not be written to shared_wastelands.cfg - " + (err ? *err : std::string("?")));
    return 1;
}
/* T-201 N1b (owner 166): a name that could not be written is said on HOST GAME / JOIN GAME, after the press's own line. */
static void PanelNameSaveNote(const std::string& err)
{
    if (err.empty()) return;
    ErrorLog("[UI] words1: the player name was not saved: " + err);   /* the file error is the log's, not the player's */
    const std::string line("Your name couldn't be saved. You'll need to enter it again next time.");
    PanelSetStatus(g_panelStatus.empty() ? line : g_panelStatus + "\n\n" + line);
}

/* mp3 - THE WORLDS LIST.  ui2: ONE scrolling list (Kenshi's Load Game look) with two columns, WORLD (the name as typed) and
   LAST PLAYED, newest first as coopworld::BuildWorldTable orders them (ui2b: a click on a head re-sorts, LAST PLAYED by time -
   OnCoopWorldListLess); the picked world is the list's selected row.  The rows
   are rebuilt only when g_worlds was re-read; any other push only moves the selection.  With no worlds at all the line over
   the list's first rows says what to do. */
static void PanelPushWorlds(MyGUI::Gui* gui)
{
    const UiNames& n = NM();
    const size_t count = g_worlds.size();
    MyGUI::MultiListBox* ml = UiMulti(gui, n.worldList);
    if (ml != 0)
    {
        if (g_worldListShown != g_worldListGen)
        {
            const long long nowUnix = (long long)std::time(0);
            g_worldPlayedKey.clear();   /* ui2b: the LAST PLAYED head's sort, filled before the rows */
            for (size_t k = 0; k < count; ++k)
                UiPlayedKeyNote(g_worldPlayedKey, coopui::PanelPlayedText(g_worlds[k].lastPlayedUnix, nowUnix, g_worldFresh[k]),
                                g_worldFresh[k] != 0 ? kPlayedKeyFresh : (g_worlds[k].lastPlayedUnix > 0 ? g_worlds[k].lastPlayedUnix : kPlayedKeyNone));
            ml->removeAllItems();
            for (size_t k = 0; k < count; ++k)
            {
                const coopworld::WorldRow& r = g_worlds[k];
                ml->addItem(MyGUI::UString(r.name.c_str()));
                ml->setSubItemNameAt(1, k, MyGUI::UString(coopui::PanelPlayedText(r.lastPlayedUnix, nowUnix, g_worldFresh[k]).c_str()));
            }
            g_worldListShown = g_worldListGen;
        }
        size_t want = MyGUI::ITEM_NONE;
        for (size_t k = 0; k < count; ++k) if (g_worlds[k].folder == g_worldSel) want = k;
        if (ml->getIndexSelected() != want) ml->setIndexSelected(want);
    }
    MyGUI::EditBox* e = UiEdit(gui, n.worldEmpty);
    if (e) e->setVisible(count == 0);
    MyGUI::Button* b;
    b = UiBtn(gui, n.deleteBtn);  if (b) b->setEnabled(!g_worldSel.empty());
    b = UiBtn(gui, n.optionsBtn); if (b) b->setEnabled(!g_worldSel.empty());   /* mp5: Game options is for the picked world */
}

/* mp5 - THE GAME OPTIONS ROWS.  Slot r shows the r-th option of the open tab: label, <, value, > - or a tick box. */
static void PanelSetTextBox(MyGUI::Gui* gui, const std::string& name, const std::string& text, bool visible)
{
    MyGUI::Widget* w = UiFind(gui, name);
    if (w == 0) return;
    w->setVisible(visible);
    MyGUI::TextBox* t = w->castType<MyGUI::TextBox>(false);
    if (t) t->setCaption(MyGUI::UString(text.c_str()));
    else ::InterlockedIncrement64(&g_panelCastNull);
}
static std::string PanelOptWorldName()
{
    for (size_t k = 0; k < g_worlds.size(); ++k) if (g_worlds[k].folder == g_optWorld) return g_worlds[k].name;
    return g_optWorld;
}
static void PanelPushOptions(MyGUI::Gui* gui)
{
    const UiNames& n = NM();
    MyGUI::Button* b;
    for (int t = 0; t < coopui::kOptTabCount; ++t) { b = UiBtn(gui, n.optTab[t]); if (b) b->setStateSelected(t == (int)g_optTab); }
    for (int r = 0; r < coopui::kOptRowsShown; ++r)
    {
        const int i = coopui::OptIndexOf((int)g_optTab, r);
        const bool on = i >= 0;
        const bool tick = on && coopui::OptDefs()[i].kind == coopui::kOptKindTick;
        const bool step = on && !tick;
        PanelSetTextBox(gui, n.optLabel[r], on ? std::string(coopui::OptDefs()[i].label) : std::string(), on);
        PanelSetTextBox(gui, n.optVal[r], step ? coopui::OptShown(i, g_optVal[i]) : std::string(), step);
        b = UiBtn(gui, n.optDec[r]);  if (b) { b->setVisible(step); b->setEnabled(step && coopui::OptCanStep(i, g_optVal[i], -1)); }
        b = UiBtn(gui, n.optInc[r]);  if (b) { b->setVisible(step); b->setEnabled(step && coopui::OptCanStep(i, g_optVal[i], 1)); }
        b = UiBtn(gui, n.optTick[r]); if (b) { b->setVisible(tick); b->setStateSelected(tick && g_optVal[i] == "1"); }
    }
    MyGUI::EditBox* e;
    e = UiEdit(gui, n.optNote);
    if (e) { e->setCaption(MyGUI::UString(g_optTab == coopui::kOptTabDifficulty ? "Animal nests: only affects areas not yet visited." : "")); e->setVisible(g_optTab == coopui::kOptTabDifficulty); }
    e = UiEdit(gui, n.optCoopNote);
    if (e) { e->setCaption(MyGUI::UString("All players must use the same mods as the host.")); e->setVisible(g_optTab == coopui::kOptTabCoop); }
}

/* prof3 (design-mpmenu1 section 8) - [F] PROFILES (words1; was YOUR PROFILES IN THIS WORLD).  Shown after a connection this panel started, when
   the world has sent this player's profiles; nothing is picked until PLAY. */
static const StoreProfRow* PanelProfPicked()
{
    for (size_t i = 0; i < g_profList.size(); ++i) if (g_profList[i].num == g_profSel) return &g_profList[i];
    return 0;
}
/* T-220 (b): the world a PROFILES title names - in SELECT mode (HOST GAME's CHANGE) the picked row CHANGE edits (g_hostProfWorld),
   by the name the worlds list shows; otherwise the hosted world (g_fWorld), as before. */
static std::string PanelProfTitleWorld()
{
    if (g_profMode != 1) return g_fWorld;
    const coopworld::WorldRow* r = PanelPickedRow();
    return (r != 0 && r->folder == g_hostProfWorld) ? r->name : g_hostProfWorld;
}
/* ui5: the PROFILES titles are coopui::PanelScreenTitle's (panelstatus.h), with every other screen's. */
static void PanelPushProfiles(MyGUI::Gui* gui)
{
    const UiNames& n = NM();
    const long count = (long)g_profList.size();
    const long long nowUnix = (long long)std::time(0);
    const int waiting = StoreProfilesWaiting();
    const bool ready = (waiting != 0 || g_profMode == 1) && g_profBusy == 0;   /* T-201 PP6': SELECT mode needs no world */
    /* ui2: ONE scrolling list with three real columns NAME / FACTION / LAST PLAYED (it was a heads line spaced by hand over
       four row buttons with Up / Down).  Rows rebuilt only when g_profList changed; any other push only moves the pick. */
    MyGUI::MultiListBox* ml = UiMulti(gui, n.profList);
    if (ml != 0)
    {
        if (g_profListShown != g_profListGen)
        {
            g_profPlayedKey.clear();   /* ui2b: the LAST PLAYED head's sort, filled before the rows */
            for (size_t k = 0; k < g_profList.size(); ++k)
                UiPlayedKeyNote(g_profPlayedKey, coopui::PanelProfilePlayedText(g_profList[k].lastPlayed, nowUnix),
                                g_profList[k].lastPlayed > 0 ? g_profList[k].lastPlayed : kPlayedKeyNone);
            ml->removeAllItems();
            for (size_t k = 0; k < g_profList.size(); ++k)
            {
                const StoreProfRow& r = g_profList[k];
                ml->addItem(MyGUI::UString(r.name.c_str()));
                ml->setSubItemNameAt(1, k, MyGUI::UString(r.faction.c_str()));
                ml->setSubItemNameAt(2, k, MyGUI::UString(coopui::PanelProfilePlayedText(r.lastPlayed, nowUnix).c_str()));
            }
            g_profListShown = g_profListGen;
        }
        size_t want = MyGUI::ITEM_NONE;
        for (size_t k = 0; k < g_profList.size(); ++k) if (g_profList[k].num == g_profSel) want = k;
        if (ml->getIndexSelected() != want) ml->setIndexSelected(want);
    }
    MyGUI::Button* b;
    b = UiBtn(gui, n.profPlay);   if (b) { b->setCaption(MyGUI::UString(coopui::ProfMainButtonText(g_profMode))); b->setEnabled(ready && PanelProfPicked() != 0); }   /* T-201 PP6' (owner 143) */
    b = UiBtn(gui, n.profNew);    if (b) b->setEnabled(ready && coopui::PanelProfileNewAllowed((unsigned)count, g_profCapUi) != 0);   /* greyed at the cap */
    b = UiBtn(gui, n.profDelete); if (b) b->setEnabled(ready && PanelProfPicked() != 0 && (g_profMode == 0 || PanelProfOfflineEditable()));   /* T-201 PP6' (owner 178a): from CHANGE too - this game edits the world's file while it is not running */
    b = UiBtn(gui, n.chooseBtn);  if (b) b->setEnabled(waiting != 0);   /* mp4's greyed Choose my profile, lit while a pick waits */
}
static void PanelProfOpen()
{
    if (g_panelScreen != 7) ::InterlockedIncrement64(&g_profilesShown);
    g_panelScreen = 7;
    g_profDlg = 0;
    g_profEverShown = 1;
}
static void PanelProfAction(int act, size_t picked)   /* ui2: picked = the list's selected row (kActProfPick only) */
{
    if (act == kActChooseProfile)
    {
        DebugLog("[UI] prof3: Choose my profile pressed" + std::string(StoreProfilesWaiting() ? "" : " - nothing waits for a pick"));
        if (StoreProfilesWaiting()) PanelProfOpen();
        return;
    }
    if (g_panelScreen != 7 || g_profDlg != 0) return;
    if (act == kActProfPick)
    {
        if (picked >= g_profList.size() || g_profListShown != g_profListGen) return;   /* ui2: a list older than g_profList is ignored */
        g_profSel = g_profList[picked].num;
        DebugLog("[UI] prof3: row picked - profile " + coopui::PanelNum((long long)g_profSel) + " '" + g_profList[picked].name + "'");
        return;
    }
    const StoreProfRow* r = PanelProfPicked();
    const std::string which = r ? " - profile " + coopui::PanelNum((long long)r->num) + " '" + r->name + "'" : std::string(" with no row picked");
    if (act == kActProfPlay && g_profMode == 1)   /* T-201 PP6' (owner 143): SELECT - back to HOST GAME with that profile; nothing starts */
    {
        DebugLog("[UI] T-201 PP6': SELECT pressed" + which);
        if (r == 0) return;
        g_hostProfNum = r->num;
        g_hostProfNew.clear();
        g_profMode = 0;
        g_panelScreen = 1;
        return;
    }
    if (act == kActProfPlay)
    {
        DebugLog("[UI] prof3: PLAY pressed" + which);
        if (r == 0 || g_profBusy != 0) return;
        if (!StoreProfilePickFromPanel(r->num))
        {
            g_profSay = "Profile selection has closed.";
            return;
        }
        ::InterlockedIncrement64(&g_profPicked);
        /* T-201 PP6' (owner 159): PLAY loads you in - the world admits the pick (its WELCOME), then LoadTick decides and acts. */
        g_profBusy = 1;
        LoadBegin(g_panelMode == 0 ? 1 : 2);
        return;
    }
    if (act == kActProfNew)
    {
        DebugLog("[UI] prof3: NEW PROFILE pressed (" + coopui::PanelNum((long long)g_profList.size()) + " of cap " + coopui::PanelNum((long long)g_profCapUi) + ")");
        if (g_profBusy != 0) return;
        if (!coopui::PanelProfileNewAllowed((unsigned)g_profList.size(), g_profCapUi)) { ::InterlockedIncrement64(&g_profRefusedAtCap); return; }
        g_fNewWorld.clear();   /* the dialog's name box is the New world box, reused */
        g_dlgText = "Enter a profile name (up to 24 characters).";   /* ui5: panel-mockups.md section 9 - "Start its characters with NEW GAME." REMOVED */
        g_profDlg = 1;
        return;
    }
    if (act == kActProfDelete)
    {
        DebugLog("[UI] prof3: DELETE pressed" + which);
        if (r == 0 || g_profBusy != 0) return;
        if (g_profMode == 1 && !PanelProfOfflineEditable()) return;   /* T-201 PP6' (owner 178a): this game's helper holds that world */
        g_dlgText = "Delete '" + r->name + "'? You won't be able to play it in this world again. Its save on this computer is moved to the Recycle Bin.";   /* review-prof3: no claim about its people or buildings until prof4 */
        g_profDlg = 2;
        return;
    }
}
static void PanelProfDlgOk()
{
    if (g_profDlg == 2 && g_profMode == 1)   /* T-201 PP6' (owner 178a): DELETE from HOST GAME's CHANGE - the world is not running; this game edits its file */
    {
        const StoreProfRow* r = PanelProfPicked();
        g_profDlg = 0;
        if (r == 0 || !PanelProfOfflineEditable()) return;
        const unsigned num = r->num;
        const coopworld::WorldRow* w = PanelPickedRow();
        std::string say;
        int box = 0;
        const int v = StoreProfileDeleteOffline(g_hostProfWorld, w != 0 ? w->name : g_hostProfWorld, num, &say, &box);
        DebugLog("[UI] T-201 PP6': Delete confirmed on PROFILES (CHANGE) for profile " + coopui::PanelNum((long long)num) + " - " + coopprof::VerdictName(v)
                 + (box != 0 ? std::string(" - the CAN'T DELETE box; its OK returns to HOST GAME") : std::string()));
        if (box != 0)   /* T-220 (owner 180): the world's file could not be edited (its helper runs in another process, or the write failed) */
        {
            ::InterlockedExchange(&g_panelWanted, 0);   /* the box shows once the panel is down (ui3b) */
            UiRaiseNoticeTitled(coopui::kProfDeleteFailText, coopui::kProfDeleteFailTitle);
            g_pressBoxReturn = 1;
            return;
        }
        if (v == coopprof::kOk && g_hostProfNum == num) g_hostProfNum = 0;
        if (v == coopprof::kOk && g_profSel == num) g_profSel = 0;
        PanelProfSelectLoad();
        g_profSay = say;
        return;
    }
    if (g_profDlg == 1 && g_profMode == 1)   /* T-201 PP6': NEW PROFILE from HOST GAME's CHANGE - the world is not running, it is made at the press */
    {
        const std::string name = coopcfg::CfgTrim(g_fNewWorld);
        DebugLog("[UI] T-201 PP6': Create pressed on PROFILES (CHANGE) for '" + name + "' - HOST GAME, made at the HOST press");
        std::string why;
        if (!coopworld::DisplayNameOk(name, &why)) { g_dlgText = why; return; }
        for (size_t k = 0; k < g_profList.size(); ++k)
            if (g_profList[k].name == name)   /* T-201 PP6' fold: exactly as the world's NewDecide compares (case counts) */
            { g_dlgText = "Couldn't do that: " + coopprof::VerdictText(coopprof::kRefusedNameTaken, 0) + "."; return; }   /* the world's own refusal words */
        g_profDlg = 0;
        g_hostProfNum = 0;
        g_hostProfNew = name;
        g_profMode = 0;
        g_panelScreen = 1;
        return;
    }
    if (g_profDlg == 1)
    {
        const std::string name = coopcfg::CfgTrim(g_fNewWorld);
        DebugLog("[UI] prof3: Create pressed for a new profile named '" + name + "'");
        std::string why;
        if (!coopworld::DisplayNameOk(name, &why)) { g_dlgText = why; return; }   /* the dialog stays open and says why */
        g_profDlg = 0;
        if (!StoreProfileRequest(kProfReqNew, 0, name)) { g_profSay = "You're no longer connected to this world. Press BACK, then HOST or JOIN again."; return; }   /* words1b: host-neutral; T-201 N1b (owner 165) */
        ::InterlockedIncrement64(&g_profNewAsked);
        g_profBusy = 1;
        g_profSay = "Creating '" + name + "'...";
        return;
    }
    if (g_profDlg == 2)
    {
        const StoreProfRow* r = PanelProfPicked();
        g_profDlg = 0;
        DebugLog("[UI] prof3: DELETE confirmed" + (r ? " - profile " + coopui::PanelNum((long long)r->num) + " '" + r->name + "'" : std::string(" with no row picked")));
        if (r == 0) return;
        if (!StoreProfileRequest(kProfReqDelete, r->num, std::string())) { g_profSay = "You're no longer connected to this world. Press BACK, then HOST or JOIN again."; return; }   /* words1b: host-neutral; T-201 N1b (owner 165) */
        ::InterlockedIncrement64(&g_profDeleteConfirmed);
        g_profBusy = 1;
        g_profSay = "Deleting '" + r->name + "'...";
    }
}

/* MODULE STATE -> WIDGETS.  Runs on a build and on an action frame, never per tick. */
/* ui5 (panel-mockups.md) - THE SCREEN'S OWN SIZE.  The window and its backdrop are as tall as the screen showing
   (coopui::PanelScreenRect: its rows, the ui3 margin, the measured frame), centred again; the widgets the screens share -
   the status area, BACK, the action button, the dialog's rows - move to that screen's rows.  Widget::setCoord(const
   IntCoord&) is VIRTUAL in MyGUI_Widget.h, so it is a call through the vtable and adds no import (the build's gate checks).
   Runs on every push, acts only when the layout, HOSTING's address row or (ui5c) the text area's height changed (ui7: its
   MEASURED height in half-rows, re-read on every push - a size MyGUI only settled a pass later is corrected on the next push):
   the status area / a dialog's text is as tall as its text (no dead space above the bottom row), so the window, the
   bottom row - BACK, the action, CHOOSE PROFILE, PLAY - and a name dialog's name row follow it.  ui5d: HOSTING's router
   help and PLAYERS areas are as tall as their texts too (coopui::PanelLive), and it acts when either's height changes
   (a player joining or leaving changes the list).  TITLE PUMP ONLY. */
static coopui::PanelGeom g_layGeom;   /* rowH 0 = no panel built yet */
static int g_layParentH = 0, g_layChromeH = coopui::kWindowCXChromeH, g_layShown = -1, g_layAddrShown = -1, g_layTextShown = -1;
static int g_layHelpShown = -1, g_layPlayersShown = -1;   /* ui5d: HOSTING's router help / PLAYERS half-rows last laid out */
static int g_layNetShown = -1;                            /* T-510: HOSTING's INTERNET ADDRESS row half-rows last laid out */
static void PanelPlace(MyGUI::Gui* gui, const std::string& name, int l, int t, int w, int h)
{
    MyGUI::Widget* x = UiFind(gui, name);
    if (x != 0) x->setCoord(MyGUI::IntCoord(l, t, w > 1 ? w : 1, h > 1 ? h : 1));
}
/* ui7 (T496 at 1280x720 / T497 at 1920x1080: the 18 px-a-line estimate left two blank lines at one size and none at the
   other - Kenshi's font scales with the window) - THE TEXT'S MEASURED SIZE.  MyGUI lays the words out itself:
   EditBox::getTextSize() (MyGUI_EditBox.h:233, virtual - a vtable call, no import) is the wrapped text's size at the edit
   box's real width, and the text sub-widget's getFontHeight() (MyGUI_ISubWidgetText.h:142, virtual) its line height
   (MyGUI's TextView advances one font height a line - Inferred: the MyGUI 3.2.3 source is not in this repo).  The one
   import is SkinItem::getSubWidgetText (MyGUI_SkinItem.h:26), checked by name by the build's gate.  WHEN IT IS VALID:
   EditText recomputes its layout inside getTextSize when the caption or width changed (Inferred, same source), so it is
   read straight after setCaption / setCoord; every caller re-reads it on its next pass and re-lays out only when it
   changed (the panel on every push, a box on the two title ticks after its build), so a size MyGUI settled only a pass later
   is corrected then - never in a loop (the width never depends on the height).  `insetH` = the edit box's height minus
   its text client's (the skin's own margins).  false = MyGUI reported 0: the caller uses the old estimate, counted in
   g_uiTextMeasureFallback. */
struct UiTextMeasure { int lineH, textH, textW, insetH; };
static bool UiMeasureText(MyGUI::EditBox* e, UiTextMeasure* m)
{
    m->lineH = m->textH = m->textW = m->insetH = 0;
    if (e == 0) { ::InterlockedIncrement64(&g_uiTextMeasureFallback); return false; }
    MyGUI::Widget* cw = e->getClientWidget();
    MyGUI::ISubWidgetText* st = (cw != 0 && cw != e) ? cw->getSubWidgetText() : 0;
    if (st == 0) st = e->getSubWidgetText();
    const MyGUI::IntSize sz = e->getTextSize();
    m->textH = sz.height;
    m->textW = sz.width;
    m->lineH = (st != 0) ? st->getFontHeight() : 0;
    if (cw != 0 && cw != e) { m->insetH = e->getHeight() - cw->getHeight(); if (m->insetH < 0) m->insetH = 0; }
    if (m->textH <= 0 || m->lineH <= 0) { ::InterlockedIncrement64(&g_uiTextMeasureFallback); return false; }
    return true;
}
/* ui7: one `[UI] measured <area> ...` line per `key` (the panel: per build of each screen; a box: per build and per changed
   size) - so every run records Kenshi's real line height and text height at its window size.  Slots: 0 status, 1 dialog,
   2 router help, 3 PLAYERS, 4 error box, 5 identity box. */
static long long g_uiMeasureLogKey[6] = { -1, -1, -1, -1, -1, -1 };
static void UiMeasureLog(int slot, const char* area, long long key, const UiTextMeasure& m)
{
    if (slot < 0 || slot > 5 || g_uiMeasureLogKey[slot] == key) return;
    g_uiMeasureLogKey[slot] = key;
    DebugLog(std::string("[UI] measured ") + area + " lineH=" + coopui::PanelNum((long long)m.lineH)
             + " textH=" + coopui::PanelNum((long long)m.textH) + " textW=" + coopui::PanelNum((long long)m.textW)
             + " inset=" + coopui::PanelNum((long long)m.insetH)
             + " fallbacks=" + coopui::PanelNum((long long)g_uiTextMeasureFallback));
}
/* ui5d: one text area's live height in half-rows - the rule below, shared by the status / dialog text and HOSTING's
   router help and PLAYERS areas.  No text, no rows.  ui7: the MEASURED height of the text in `widget` (set just before
   PanelLayout runs) plus its skin inset, through coopui::PanelTextHalvesPx (text + kPanelTextSparePx, whole half-rows);
   the ui5b line estimate (coopui::NoticeLinesFor at kPanelTextCharW, kPanelTextLinePx a line) only when MyGUI reports 0. */
static int PanelTextAreaHalves(MyGUI::Gui* gui, const std::string& widget, int slot, const char* area, int lay,
                               const std::string& text, int innerW, int rowH)
{
    if (text.empty()) return 0;
    UiTextMeasure m;
    if (UiMeasureText(UiEdit(gui, widget), &m))
    {
        if (slot != 0 || g_uiStatusCaptionStale == 0)   /* ui7 fold (review-ui7 #3) */
            UiMeasureLog(slot, area, ((long long)g_panelBuilt << 8) | (long long)(lay & 0xFF), m);
        return coopui::PanelTextHalvesPx(m.textH + m.insetH, rowH);
    }
    return coopui::PanelTextHalves(coopui::NoticeLinesFor(text, innerW / coopui::kPanelTextCharW), rowH);   /* the fallback */
}
/* ui5c (user 2026-09-27: every screen sized to its content, no dead space) - THE TEXT AREA'S LIVE HEIGHT in half-rows: ui7's
   measured text height plus kPanelTextSparePx, rounded up to whole half-rows at this rowH (PanelTextAreaHalves), and
   none for no text.  -1 = this layout has no text area.  The status text is the one PanelPushStatus last set; a
   dialog's is g_dlgText, which PanelPush sets just before it runs PanelLayout. */
static int PanelLiveTextHalves(MyGUI::Gui* gui, int lay, int innerW, int rowH)
{
    if (coopui::PanelSlotBand(lay, coopui::kSlotStatus, 2).h > 0)
        return PanelTextAreaHalves(gui, NM().status, 0, "status", lay, g_panelStatusCaption, innerW, rowH);
    if (coopui::PanelSlotBand(lay, coopui::kSlotDlgText, 2).h > 0)
        return PanelTextAreaHalves(gui, NM().dlgText, 1, "dialog", lay, g_dlgText, innerW, rowH);
    return -1;
}
static void PanelLayout(MyGUI::Gui* gui)
{
    const coopui::PanelGeom& g = g_layGeom;
    if (g.rowH <= 0) return;
    const int lay  = coopui::PanelLayoutOf((int)g_panelScreen, (int)g_profDlg, g_panelNameHintShown.empty() ? 0 : 1);   /* ui5b */
    const int addr = g_hostHomeAddr.empty() ? 0 : 1;
    coopui::PanelLive live(PanelLiveTextHalves(gui, lay, g.innerW, g.rowH));   /* ui5c: -1 = no text area on this screen */
    if (lay == coopui::kLayHosting)
    {
        /* ui5d: the router help and PLAYERS areas as tall as their texts (ui7: measured); -1 (the list's value) until each is pushed */
        live.help    = g_hostingHelpShown.empty() ? -1
                     : PanelTextAreaHalves(gui, NM().routerHelp, 2, "routerhelp", lay, g_hostingHelpShown, g.innerW, g.rowH);
        live.players = (g_hostingPlayersShown.empty() || g_hostingPlayersShown == "\x01") ? -1
                     : PanelTextAreaHalves(gui, NM().playersBox, 3, "players", lay, g_hostingPlayersShown, g.innerW, g.rowH);
        live.net = coopui::PanelNetAddrHalves(PanelNetAddrShownText());   /* T-510 */
    }
    const int txt = live.text;
    if (lay == g_layShown && addr == g_layAddrShown && txt == g_layTextShown && live.help == g_layHelpShown
        && live.players == g_layPlayersShown && live.net == g_layNetShown) return;
    const UiNames& n = NM();
    const coopui::PanelRect r = coopui::PanelScreenRect(g, g_layParentH, lay, g_layChromeH, live);
    PanelPlace(gui, n.backdrop, r.left, r.top, r.w, r.h);
    PanelPlace(gui, n.panel,    r.left, r.top, r.w, r.h);
    const int W = g.innerW, rowH = g.rowH, gap = coopui::PanelGapOf(rowH);
    PanelPlace(gui, std::string("CoopPanelInner"), g.padX, g.padY, W, coopui::PanelLayoutInnerH(lay, rowH, live));
    const int btnW = coopui::PanelBottomBtnW(W);
    const coopui::PanelBand bb = coopui::PanelSlotBand(lay, coopui::kSlotButtons, rowH, live);
    const coopui::PanelBand sb = coopui::PanelSlotBand(lay, coopui::kSlotStatus, rowH, live);
    const coopui::PanelBand tb = coopui::PanelSlotBand(lay, coopui::kSlotDlgText, rowH, live);
    const coopui::PanelPairRect nr = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(lay, coopui::kSlotDlgName, rowH, live), 0, 0);
    PanelPlace(gui, n.status,       0,         sb.top, W,         sb.h);
    PanelPlace(gui, n.closeBtn,     0,         bb.top, btnW,      bb.h);   /* BACK bottom-left */
    PanelPlace(gui, n.goBtn,        W - btnW,  bb.top, btnW,      bb.h);   /* HOST / JOIN bottom-right */
    PanelPlace(gui, n.dlgText,      0,         tb.top, W,         tb.h);
    PanelPlace(gui, n.dlgNameLabel, nr.labelX, nr.top, nr.labelW, nr.h);
    PanelPlace(gui, n.dlgNameEdit,  nr.boxX,   nr.top, nr.boxW,   nr.h);
    PanelPlace(gui, n.dlgCancel,    0,         bb.top, btnW,      bb.h);   /* CANCEL bottom-left */
    PanelPlace(gui, n.dlgOk,        W - btnW,  bb.top, btnW,      bb.h);   /* CREATE / DELETE ... bottom-right */
    /* ui5c: the two bottom-right buttons PanelBuild placed at the list's fixed row follow the text too. */
    if (lay == coopui::kLayHosting)  PanelPlace(gui, n.chooseBtn, W - btnW, bb.top, btnW, bb.h);   /* CHOOSE PROFILE */
    if (lay == coopui::kLayProfiles) PanelPlace(gui, n.profPlay,  W - btnW, bb.top, btnW, bb.h);   /* PLAY */
    /* ui5d: HOSTING's router help and PLAYERS boxes at their live bands (PanelBuild made them at the list's values). */
    if (lay == coopui::kLayHosting)
    {
        const coopui::PanelBand hb = coopui::PanelSlotBand(lay, coopui::kSlotRouterHelp, rowH, live);
        const coopui::PanelBand pb = coopui::PanelSlotBand(lay, coopui::kSlotPlayers, rowH, live);
        PanelPlace(gui, n.routerHelp, 0, hb.top, W, hb.h);
        PanelPlace(gui, n.playersBox, 0, pb.top, W, pb.h);
    }
    /* HOSTING (panel-mockups.md section 8): with no address found the sentence takes the whole row and the label hides.
       T-510: INTERNET ADDRESS under it - its label level with its value in the same label column, SHOW then COPY at the
       right, each button the width of LOCAL ADDRESS's COPY (PanelAddrBtnW). */
    const int addrBtnW = coopui::PanelAddrBtnW(W);
    const coopui::PanelPairRect ar = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, rowH),
                                                             0, addrBtnW + gap);
    PanelPlace(gui, n.homeAddr, addr != 0 ? ar.boxX : 0, ar.top, addr != 0 ? ar.boxW : ar.boxX + ar.boxW, ar.h);
    PanelPlace(gui, n.copyBtn, W - addrBtnW, ar.top, addrBtnW, ar.h);
    {
        const coopui::PanelPairRect nr2 = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, rowH, live),
                                                                  0, 2 * (addrBtnW + gap));
        PanelPlace(gui, n.netAddrLabel, nr2.labelX,                  nr2.top, nr2.labelW, nr2.h);
        PanelPlace(gui, n.netAddr,      nr2.boxX,                    nr2.top, nr2.boxW,   nr2.h);
        PanelPlace(gui, n.netShowBtn,   W - 2 * addrBtnW - gap,      nr2.top, addrBtnW,   nr2.h);
        PanelPlace(gui, n.netCopyBtn,   W - addrBtnW,                nr2.top, addrBtnW,   nr2.h);
    }
    MyGUI::Widget* al = UiFind(gui, n.homeAddrLabel);
    if (al != 0) al->setVisible(addr != 0);
    /* T-201 N1b (owner 166): MULTIPLAYER - HOST GAME, JOIN GAME and BACK, a full row each. */
    if (lay == coopui::kLayLanding)
    {
        const coopui::PanelBand lhb = coopui::PanelSlotBand(lay, coopui::kSlotHostBtn, rowH);
        const coopui::PanelBand ljb = coopui::PanelSlotBand(lay, coopui::kSlotJoinBtn, rowH);
        const coopui::PanelBand lbb = coopui::PanelSlotBand(lay, coopui::kSlotButtons, rowH);
        PanelPlace(gui, n.modeHost,    W / 5, lhb.top, W - 2 * (W / 5), lhb.h);
        PanelPlace(gui, n.modeJoin,    W / 5, ljb.top, W - 2 * (W / 5), ljb.h);
        PanelPlace(gui, n.landBackBtn, 0,     lbb.top, btnW,            lbb.h);
    }
    /* T-201 N1b (owner 166): HOST GAME / JOIN GAME - PLAYER NAME the top row, its label level with its box (PanelLabelBoxIn, the
       one rule every label follows); the hint's row under the box only while it has a sentence (PanelNameCheck re-runs this when
       it changes), and the rows below it follow. */
    const int hostLay = (lay == coopui::kLayHost || lay == coopui::kLayHostHint) ? 1 : 0;
    const int joinLay = (lay == coopui::kLayJoin || lay == coopui::kLayJoinHint) ? 1 : 0;
    if (hostLay != 0 || joinLay != 0)
    {
        const coopui::PanelPairRect lm = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(lay, coopui::kSlotNameRow, rowH), 0, 0);
        const coopui::PanelBand lh = coopui::PanelSlotBand(lay, coopui::kSlotNameHint, rowH);
        PanelPlace(gui, n.nameLabel, lm.labelX, lm.top, lm.labelW, lm.h);
        PanelPlace(gui, n.nameEdit,  lm.boxX,   lm.top, lm.boxW,   lm.h);
        if (lh.h > 0) PanelPlace(gui, n.nameHint, lm.boxX, lh.top, lm.boxW, lh.h);
    }
    {
        MyGUI::Widget* hw = UiFind(gui, n.nameHint);
        if (hw != 0) hw->setVisible(coopui::PanelSlotBand(lay, coopui::kSlotNameHint, rowH).h > 0);
    }
    if (hostLay != 0)   /* T-201 N1b: PanelBuild made these at kLayHost's rows */
    {
        const int thirdW = (W - 2 * gap) / 3;
        const coopui::PanelBand lb = coopui::PanelSlotBand(lay, coopui::kSlotList, rowH);
        const coopui::PanelBand kb = coopui::PanelSlotBand(lay, coopui::kSlotListBtns, rowH);
        const coopui::PanelPairRect pp = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(lay, coopui::kSlotPortRow, rowH),
                                                                 coopui::PanelPortBoxW(W), 0);
        PanelPlace(gui, n.worldList,   0,                  lb.top,      W,         lb.h);
        PanelPlace(gui, n.worldEmpty,  12,                 lb.top + 40, W - 44,    rowH);
        PanelPlace(gui, n.newWorldBtn, 0,                  kb.top,      thirdW,    kb.h);
        PanelPlace(gui, n.optionsBtn,  thirdW + gap,       kb.top,      thirdW,    kb.h);
        PanelPlace(gui, n.deleteBtn,   2 * (thirdW + gap), kb.top,      thirdW,    kb.h);
        PanelPlace(gui, n.portLabel,   pp.labelX,          pp.top,      pp.labelW, pp.h);
        PanelPlace(gui, n.portEdit,    pp.boxX,            pp.top,      pp.boxW,   pp.h);
        const int profSideW = coopui::PanelSideBtnW(W);   /* T-201 PP6' (owner 141): PROFILE   <name>   [ CHANGE ] */
        const coopui::PanelPairRect pr = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(lay, coopui::kSlotProfRow, rowH), 0, profSideW + gap);
        PanelPlace(gui, n.hostProfLabel,     pr.labelX,     pr.top, pr.labelW,  pr.h);
        PanelPlace(gui, n.hostProfValue,     pr.boxX,       pr.top, pr.boxW,    pr.h);
        PanelPlace(gui, n.hostProfChangeBtn, W - profSideW, pr.top, profSideW,  pr.h);
    }
    if (joinLay != 0)   /* T-201 N1b: PanelBuild made these at kLayJoin's rows */
    {
        const int sideW = coopui::PanelSideBtnW(W);
        const coopui::PanelPairRect jr = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(lay, coopui::kSlotAddrRow, rowH), 0, sideW + gap);
        PanelPlace(gui, n.joinLabel, jr.labelX, jr.top, jr.labelW, jr.h);
        PanelPlace(gui, n.joinEdit,  jr.boxX,   jr.top, jr.boxW,   jr.h);
        PanelPlace(gui, n.pasteBtn,  W - sideW, jr.top, sideW,     jr.h);
    }
    g_layShown = lay;
    g_layAddrShown = addr;
    g_layTextShown = txt;
    g_layHelpShown = live.help;
    g_layPlayersShown = live.players;
    g_layNetShown = live.net;
}
static const coopui::PanelGeom& PanelGeomNow() { return g_layGeom; }

/* ui5d (T485: "forward UDP ports 0 and 27016") - THE GAME PORT THE HOSTING SCREEN NAMES (router help, LOCAL ADDRESS,
   COPY): the port this game hosts on while it is set up to host (the one the HOST press wrote), else the HOST GAME PORT
   box's value (g_fPort - what the player chose), else coopui::kPanelDefaultGamePort.  Never 0: ConfigHostPort() is 0 in a
   game set up for neither, and a joiner's is the friend's port (coopui::PanelHostingPort). */
static int PanelHostingGamePort()
{
    unsigned short field = 0;
    std::string why;
    if (!coopcfg::CfgPortFieldOk(g_fPort, &field, &why)) field = 0;
    return coopui::PanelHostingPort(ConfigRole() == kRoleHost ? (int)ConfigHostPort() : 0, (int)field);
}

static void PanelPush(MyGUI::Gui* gui)
{
    const UiNames& n = NM();
    MyGUI::EditBox* e;
    e = UiEdit(gui, n.portEdit);   if (e) e->setCaption(MyGUI::UString(g_fPort.c_str()));
    {   /* T-201 PP6' (owner 141): HOST GAME's PROFILE row - the name, "<name> (new)", or empty with CHANGE greyed when no world is picked */
        coopprof::HostProfileChoice hc;
        const int picked = (g_panelScreen == 1) ? PanelHostProfChoice(&hc) : 0;
        MyGUI::Widget* pw = UiFind(gui, n.hostProfValue);
        MyGUI::TextBox* pt = pw ? pw->castType<MyGUI::TextBox>(false) : 0;
        if (pt) pt->setCaption(MyGUI::UString(coopui::HostProfileValue(hc.name, hc.isNew, picked).c_str()));
        MyGUI::Button* pb = UiBtn(gui, n.hostProfChangeBtn);
        if (pb) pb->setEnabled(picked != 0 && g_pressBusy == 0 && g_loadStage == 0);
    }
    e = UiEdit(gui, n.joinEdit);   if (e) e->setCaption(MyGUI::UString(g_fJoinAddr.c_str()));
    e = UiEdit(gui, n.nameEdit);   if (e) e->setCaption(MyGUI::UString(g_fName.c_str()));
    /* mp3: the world and save boxes are gone from the screen (the world is picked from the list, the save folder is
       named automatically), and so are the helper's two ticks and address box (always this computer's own). */
    e = UiEdit(gui, n.dlgNameEdit); if (e) e->setCaption(MyGUI::UString(g_fNewWorld.c_str()));
    e = UiEdit(gui, n.dlgText);     if (e) e->setCaption(MyGUI::UString(g_dlgText.c_str()));

    MyGUI::Button* b;
    b = UiBtn(gui, n.dlgOk);       if (b) b->setCaption(MyGUI::UString(g_panelScreen == 4 ? "DELETE WORLD" : (g_panelScreen == 7 && g_profDlg == 2) ? "DELETE PROFILE" : "CREATE"));   /* ui1 */
    PanelPushWorlds(gui);
    PanelPushProfiles(gui);   /* prof3 */
    {
        /* mp4: the Hosting screen's address line, Copy (greyed with no address) and the router help.  ui5d: the game port
           is PanelHostingGamePort's - never 0 (T485 showed ConfigHostPort()'s 0 in a game not set up to host). */
        const int gamePort = PanelHostingGamePort();
        MyGUI::Widget* aw = UiFind(gui, n.homeAddr);
        if (aw)
        {
            MyGUI::TextBox* at = aw->castType<MyGUI::TextBox>(false);
            if (at) at->setCaption(MyGUI::UString(coopui::PanelHomeAddrLine(g_hostHomeAddr, gamePort).c_str()));
            else ::InterlockedIncrement64(&g_panelCastNull);
        }
        b = UiBtn(gui, n.copyBtn); if (b) b->setEnabled(!g_hostHomeAddr.empty());
        PanelNetAddrPush(gui);   /* T-510 */
        /* ui5d: PanelLayout sizes the area to it.  T-510: the address note above the router help. */
        g_hostingHelpShown = coopui::PanelAddrNoteText() + "\n" + coopui::PanelRouterHelpText(gamePort);
        e = UiEdit(gui, n.routerHelp); if (e) e->setCaption(MyGUI::UString(g_hostingHelpShown.c_str()));
        g_hostingPlayersShown = "\x01";   /* never a real list: the next status build pushes it */
    }

    MyGUI::Widget* g;
    /* mp1: one screen at a time - 0 Multiplayer, 1 Host a game, 2 Join a game.  The world and save rows are the host's
       (a joiner follows the host's world, W3); their values still ride every save, exactly as before. */
    g = UiFind(gui, n.landGroup); if (g) g->setVisible(g_panelScreen == 0);
    g = UiFind(gui, n.hostGroup); if (g) g->setVisible(g_panelScreen == 1);
    g = UiFind(gui, n.joinGroup); if (g) g->setVisible(g_panelScreen == 2);
    g = UiFind(gui, n.nameLabel); if (g) g->setVisible(g_panelScreen == 1 || g_panelScreen == 2);   /* T-201 N1b (owner 166) */
    g = UiFind(gui, n.nameEdit);  if (g) g->setVisible(g_panelScreen == 1 || g_panelScreen == 2);   /* T-201 N1b: the hint's is PanelLayout's */
    g = UiFind(gui, n.dlgGroup);  if (g) g->setVisible(g_panelScreen == 3 || g_panelScreen == 4 || (g_panelScreen == 7 && g_profDlg != 0));   /* prof3: its two dialogs */
    g = UiFind(gui, n.profGroup); if (g) g->setVisible(g_panelScreen == 7 && g_profDlg == 0);   /* prof3 */
    g = UiFind(gui, n.hostingGroup); if (g) g->setVisible(g_panelScreen == 5);   /* mp4 */
    g = UiFind(gui, n.optGroup);     if (g) g->setVisible(g_panelScreen == 6);   /* mp5 */
    PanelPushOptions(gui);
    g = UiFind(gui, n.chooseBtn);    if (g) g->setVisible(g_panelScreen == 5 && StoreProfilesWaiting());   /* mp4: only while a profile pick waits (the approved HOSTING window holds BACK alone) */
    {
        /* mp3: the status area, Host / Join and Back belong to the Host and Join screens; a dialog has its own two buttons. */
        const std::string* hostOrJoin[3] = { &n.status, &n.goBtn, &n.closeBtn };
        int i;
        /* mp4: the Hosting screen keeps the status area and Back; its left button is Choose my profile, not Host. */
        for (i = 0; i < 3; ++i) { g = UiFind(gui, *hostOrJoin[i]); if (g) g->setVisible(g_panelScreen == 1 || g_panelScreen == 2 || (g_panelScreen == 5 && i != 1) || (g_panelScreen == 7 && g_profDlg == 0 && i != 1)); }   /* prof3: status and Leave */
        /* ui5: no title row - the window's caption names the screen (below). */
        g = UiFind(gui, n.dlgNameLabel); if (g) g->setVisible(g_panelScreen == 3 || (g_panelScreen == 7 && g_profDlg == 1));
        g = UiFind(gui, n.dlgNameEdit);  if (g) g->setVisible(g_panelScreen == 3 || (g_panelScreen == 7 && g_profDlg == 1));
        g = UiFind(gui, n.dlgNameLabel);   /* prof3: the name row is the profile's on screen 7 */
        MyGUI::TextBox* nl = g ? g->castType<MyGUI::TextBox>(false) : 0;
        if (nl) nl->setCaption(MyGUI::UString(g_panelScreen == 7 ? "PROFILE NAME" : "WORLD NAME"));
    }
    /* ui5 (panel-mockups.md): ONE title - the window's own caption names the screen; there is no repeated first row. */
    g = UiFind(gui, n.panel);
    {
        MyGUI::Window* pwin = (g != 0) ? g->castType<MyGUI::Window>(false) : 0;
        const std::string title = coopui::PanelTitleFit(coopui::PanelScreenTitle((int)g_panelScreen, (int)g_profDlg, g_panelMode == 0 ? 1 : 0, PanelProfTitleWorld(), PanelOptWorldName()),
                                                        g_layGeom.maxRect.w);   /* ui5b: a long world name is cut with "..." */
        if (pwin) pwin->setCaption(MyGUI::UString(title.c_str()));
        else ::InterlockedIncrement64(&g_panelCastNull);
    }
    g_uiStatusCaptionStale = 1;   /* ui7 fold (review-ui7 #3): the status text is the previous screen's until PanelPushStatus below */
    PanelLayout(gui);   /* ui5: the window as tall as this screen, the shared widgets on its rows */

    /* U2: the one action button says what it will do in the mode that is showing.  A caption that
       promises what the build does not do is F725 R6's defect, and so is one that promises less. */
    b = UiBtn(gui, n.goBtn);
    /* T-201 N1: while a press is in progress HOST / JOIN and everything above the status are greyed and BACK reads CANCEL. */
    const bool pressBusy = (g_pressBusy != 0 || g_loadStage != 0);   /* T-201 PP6': the press carries on through the load */
    const bool joinedLive = (g_panelMode == 1 && g_panelScreen == 2 && ConfigRole() == kRoleClient && StoreWelcomedThisLink() != 0 && StoreLinkIsUp());   /* T-201 N1 fold: JOIN would drop the session */
    if (b) { b->setCaption(MyGUI::UString(g_panelMode == 0 ? "HOST" : "JOIN")); b->setEnabled(PanelGoAllowed()); }   /* mp3: Host waits for a picked world; T-201 N1b: PanelNameCheck (below) adds the name */
    b = UiBtn(gui, n.closeBtn);   if (b) { b->setCaption(MyGUI::UString(pressBusy ? "CANCEL" : "BACK")); b->setEnabled(coopui::LoadCancelAllowed(g_loadStage) != 0); }   /* prof3; T-201 PP6': greyed once Loading / Opening NEW GAME shows */
    g = UiFind(gui, n.hostGroup); if (g) g->setEnabled(!pressBusy);
    g = UiFind(gui, n.joinGroup); if (g) g->setEnabled(!pressBusy && !joinedLive);
    g = UiFind(gui, n.nameEdit);  if (g) g->setEnabled(!pressBusy && !joinedLive);   /* T-201 N1b: greyed with its screen's boxes */

    g_panelNameOkShown = -1;
    g_panelTryShown = -1;
    PanelNameCheck(gui);
    PanelPushStatus(gui, 1);
}

/* WIDGETS -> MODULE STATE.  `force` is set on any frame that runs an action, which is the LIVE READ AT
   THE MOMENT OF COMMITMENT that SAVE needs; otherwise this is throttled to 4 Hz, which bounds its cost
   at the title pump's ~1 kHz.  The 250 ms is a throttle on cost, not a wait for an event: the condition
   is re-asked on every tick and nothing is timed against anything. */
static void PanelMirror(MyGUI::Gui* gui, int force)
{
    const DWORD now = ::GetTickCount();
    if (force == 0 && (DWORD)(now - g_panelLastMirrorMs) < kPanelMirrorMs) return;
    g_panelLastMirrorMs = now;
    const UiNames& n = NM();
    MyGUI::EditBox* e;
    e = UiEdit(gui, n.portEdit);   if (e) g_fPort         = UiTextOf(e, coopcfg::kCfgPortFieldMax);
    e = UiEdit(gui, n.joinEdit);   if (e) g_fJoinAddr     = UiTextOf(e, coopcfg::kCfgAddrFieldMax);
    e = UiEdit(gui, n.dlgNameEdit); if (e) g_fNewWorld    = UiTextOf(e, coopcfg::kCfgWorldFieldMax);   /* mp3: the New world box */
    e = UiEdit(gui, n.nameEdit);   if (e) g_fName         = UiTextOf(e, coopcfg::kCfgPlayerNameFieldMax);
    PanelNameCheck(gui);
}

/* PASTE.  design-ui-panel 5.3: Ctrl+V into a MyGUI EditBox in Kenshi is Read twice over and observed
   never, so the panel does not gamble on it - this reads the Windows clipboard directly and is thirty
   lines with no MyGUI dependency.  Ctrl+V is tested separately in the session pack and its result is a
   datum either way. */
static void PanelPaste()
{
    std::string got;
    int ok = 0;
    if (::OpenClipboard(0))
    {
        HANDLE h = ::GetClipboardData(CF_TEXT);
        if (h != 0)
        {
            const char* p = (const char*)::GlobalLock(h);
            if (p != 0)
            {
                const SIZE_T sz = ::GlobalSize(h);
                size_t len = 0;
                while (len < (size_t)sz && len < 512 && p[len] != 0) ++len;
                got.assign(p, len);
                ::GlobalUnlock(h);
                ok = 1;
            }
        }
        ::CloseClipboard();
    }
    if (ok == 0)
    {
        ::InterlockedIncrement64(&g_panelPasteFailed);
        PanelSetStatus("Nothing to paste. Copy the host's address first.");
        return;
    }
    g_fJoinAddr = coopcfg::CfgSanitiseTyped(got, coopcfg::kCfgAddrFieldMax);
    ::InterlockedIncrement64(&g_panelPasted);
    PanelSetStatus("Press JOIN to connect.");   /* words1: the address shows in its box; this replaces any older paste line */
}

/* mp4 - COPY: the Hosting screen's address onto the Windows clipboard, as text - the mirror of PanelPaste.  The
   clipboard is opened for this thread's active window (the game's): EmptyClipboard under a null owner makes
   SetClipboardData fail (MSDN), so null is only the fallback. */
/* T-510 - ONE CLIPBOARD MARK beside the text (the clipboard is open and owned): a registered format holding a DWORD.  Windows'
   clipboard history and cloud clipboard leave out data carrying "ExcludeClipboardContentFromMonitorProcessing", or
   "CanIncludeInClipboardHistory" / "CanUploadToCloudClipboard" = 0 (MSDN, Clipboard Formats).  1 = set. */
static int PanelClipMark(const char* format, DWORD value)
{
    const UINT f = ::RegisterClipboardFormatA(format);
    if (f == 0) return 0;
    HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (h == 0) return 0;
    DWORD* p = (DWORD*)::GlobalLock(h);
    if (p == 0) { ::GlobalFree(h); return 0; }
    *p = value;
    ::GlobalUnlock(h);
    if (::SetClipboardData(f, h) == 0) { ::GlobalFree(h); return 0; }
    return 1;   /* the clipboard owns h now */
}

static void PanelCopy(int netRow)
{
    /* ui5d: the port LOCAL ADDRESS shows, never 0.  T-510: netRow 1 = INTERNET ADDRESS's COPY - the address whether hidden or shown. */
    const std::string text = coopui::PanelHostCopyText(netRow != 0 ? (coopui::PanelNetAddrHave(g_netAddrState, g_netAddrIp) != 0 ? g_netAddrIp : std::string())
                                                                   : g_hostHomeAddr, PanelHostingGamePort());
    int ok = 0, marks = 0;
    if (!text.empty() && ::OpenClipboard(::GetActiveWindow()))
    {
        if (::EmptyClipboard())
        {
            HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (h != 0)
            {
                char* p = (char*)::GlobalLock(h);
                if (p != 0)
                {
                    text.copy(p, text.size());
                    p[text.size()] = 0;
                    ::GlobalUnlock(h);
                    if (::SetClipboardData(CF_TEXT, h) != 0) ok = 1;   /* the clipboard owns h now */
                }
                if (ok == 0) ::GlobalFree(h);
            }
        }
        /* T-510: the internet address stays out of clipboard history and cloud sync (a mark that fails leaves the copy as it is) */
        if (ok != 0 && netRow != 0)
            marks = PanelClipMark("ExcludeClipboardContentFromMonitorProcessing", 0) + PanelClipMark("CanIncludeInClipboardHistory", 0)
                  + PanelClipMark("CanUploadToCloudClipboard", 0);
        ::CloseClipboard();
    }
    ::InterlockedIncrement64(ok != 0 ? &g_addrCopied : &g_addrCopyFailed);
    g_hostCopyNote = coopui::PanelCopyNote(netRow, ok, text);
    DebugLog(std::string("[UI] COPY ") + (netRow != 0 ? "internet" : "local") + " address: " + (ok != 0 ? "copied" : "not copied")
             + ((ok != 0 && netRow != 0) ? " (kept out of clipboard history: " + coopui::PanelNum((long long)marks) + " of 3 marks set)" : std::string()));
}

/* mp4 - OPEN THE HOSTING SCREEN: read the home-network address once, from Windows (no outside service). */
static void PanelHostingOpen()
{
    std::string how;
    g_hostHomeAddr = net::HomeNetworkAddress(&how);
    g_hostCopyNote.clear();
    /* The INTERNET ADDRESS row opens hidden and asks the router again (upnp.cpp, on its own thread); the lookup websites wait
       for a SHOW or COPY press. */
    g_netAddrShown = 0;
    g_netAddrIp.clear();
    g_netAddrPushed = 0;
    g_netAddrPending = coopui::kNetPressNone;
    g_netAddrLookId = 0;
    if (ConfigRouterPort() != 0)
    {
        g_netAddrState = coopui::kNetAddrAsking;
        g_netAddrAskId = coop::UpnpAddrAsk((unsigned short)PanelHostingGamePort());
    }
    else   /* routerport=0 (a TEST-ONLY settings key): the router is not asked, so the row starts as "the router gave none" -
              SHOW / COPY still ask the lookup websites (owner 452), on their own worker (upnp.cpp UpnpLookupAsk) */
    {
        g_netAddrState = coopui::kNetAddrMissing;
        g_netAddrAskId = 0;
        ::InterlockedIncrement64(&g_netAddrMissing);
        DebugLog("[UI] internet address: not found (router use is off)");
    }
    ::InterlockedIncrement64(&g_hostingShown);
    ::InterlockedIncrement64(g_hostHomeAddr.empty() ? &g_homeAddrMissing : &g_homeAddrFound);
    DebugLog("[UI] mp4: Hosting screen for '" + g_fWorld + "' - home-network address "
             + (g_hostHomeAddr.empty() ? std::string("NOT FOUND") : g_hostHomeAddr) + " (" + how + ")");
    g_panelScreen = 5;
}

/* The port this computer's world server, started by this game, is listening on while it still runs; 0 = none running. TITLE PUMP. */
static int PanelNotebookRunningPort()
{
    if (g_nbProc == 0) return 0;
    DWORD code = 0;
    if (::GetExitCodeProcess(g_nbProc, &code) == 0 || code != STILL_ACTIVE) return 0;
    return g_nbPort;
}

/* ------------------------------------------------------------------------------------------------
   U2 - STARTING THE WORLD'S NOTEBOOK, with design-ui-panel 2.2's four rules as written.
     * CREATE_NO_WINDOW | DETACHED_PROCESS  - no black console box over the title screen;
     * lpCurrentDirectory = the mod folder  - Kenshi's own working directory is the game root, and a
       relative path would resolve against it;
     * bInheritHandles = FALSE              - the game holds handles on saves and logs; a child must not;
     * --world names the WORLD, never a folder (W2b, decision 58): the notebook builds
       <save root>\coop-store\<world> with coopworld::WorldDir, the same construction StoreNotebookDir() uses,
       so the plugin and the notebook name the same folder by construction rather than by both defaulting
       correctly. An empty world key is passed as coopworld::kDefaultWorld ("New World"), the default world. The name is
       quoted, so a world name with spaces is ONE argument (owner decision 249 a).
   `--owner <this install's player id>` is passed (B13): the relay reads the pair, so the player who pressed the
   button is the world's operator. F725 R5's old reason for leaving it out (an unknown flag consumed the next
   token) was closed by that relay work.
   Returns 1 when a process was created, 0 when it was not, 2 when ours is already running.
   ------------------------------------------------------------------------------------------------ */
static int PanelStartNotebook(unsigned short port)
{
    if (coopdata::IdentityRefusesHostJoin(IdentitySessionOnlyKind()))   /* PP3d: never a world server under a session-only owner id */
    {
        ErrorLog("[UI] PP3d: the world's notebook was NOT started - this game's identity is unusable (session-only).");
        return 0;
    }
    if (g_nbProc != 0)
    {
        DWORD code = 0;
        if (::GetExitCodeProcess(g_nbProc, &code) != 0 && code == STILL_ACTIVE)
        {
            ::InterlockedIncrement64(&g_panelNbAlready);
            return 2;   /* g_nbPort stays the port it really listens on (PanelGo rings that one) */
        }
        ::CloseHandle(g_nbProc);
        g_nbProc = 0;
    }
    g_nbPort = (int)port;

    const std::string exe = PathNextToDll(swnames::kServerExe);
    if (::GetFileAttributesA(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ::InterlockedIncrement64(&g_panelNbNoExe);
        g_nbState = coopui::kNbNoExe;
        ErrorLog("[UI] the MULTIPLAYER panel cannot start the world's notebook: there is no SharedWastelandsServer.exe"
                 " at " + exe + ". tools/deploy.ps1 copies it beside the plugin; a build that predates that"
                 " change has to have the notebook started by hand.");
        return 0;
    }

    /* PathNextToDll("") ends in a separator; CreateProcessA accepts that for lpCurrentDirectory, and it
       is trimmed anyway so the log line names the folder the way a person writes it. */
    std::string dir = PathNextToDll("");
    if (!dir.empty() && (dir[dir.size() - 1] == '\\' || dir[dir.size() - 1] == '/'))
        dir.erase(dir.size() - 1);
    const std::string sdir = StoreNotebookDir();   /* named in the log line only - the notebook builds it itself from --world */
    /* W2b / W2-f (review-w2 D): the world the PLUGIN uses - the same fallback (an empty or invalid name is the default
       world), so a name the notebook would refuse cannot start a notebook that exits 2 while this game uses coop. */
    std::string worldArg = coopworld::WorldOrDefault(ConfigWorldKey(), 0);
    /* PP3: the notebook's TOP folder is this game's own (store.cpp BaseDir: storedir=, else <data folder>\worlds), passed
       with --root so the two can never differ - the data folder may be a TEST datadir= the notebook cannot know. */
    const std::string rootArg = StoreTopDir();

    /* B13 (decision 49) - THE NOTEBOOK IS STARTED FOR A PLAYER, AND IT IS TOLD WHICH ONE. The relay's
       authority was g_order[0] - whoever CONNECTED first - so a relay killed and restarted handed the
       authority to whichever game dialled first, and OPTIONS and WEATHER with it. The player who pressed
       this button is the operator: --owner names them, the relay writes the name into owner.txt, and a
       restart reads it back. An install with no id yet (no settings file read) passes no --owner at all,
       and the relay then takes the first HELLO as the operator and persists that instead. */
    const std::string ownerId = ConfigPlayerId();
    std::string ownerArg;
    if (!ownerId.empty()) ownerArg = " --owner " + ownerId;

    /* CreateProcessA MUTATES lpCommandLine, so it is never a string literal and never a c_str().
       B13-b (review-b13, LOW): the line grew by --owner's 42 characters, and a TRUNCATED command line is
       worse than none - it would start the notebook on the wrong world, or without its operator, and the
       run would then look like a different fault entirely. _snprintf returns negative when it did not fit,
       and this refuses in words instead of launching something it cannot describe. */
    char cmd[1024];
    /* owner decision 183: --parent <this Kenshi's pid> - the helper closes itself (its normal quit: every queued write, then the world
       lock goes with the process) when this Kenshi is gone, crashed or not, so a crash never leaves the lock and the port held. */
    const int cmdLen = _snprintf(cmd, sizeof(cmd) - 1, "\"%s\" --port %u --world \"%s\" --root \"%s\"%s --parent %lu", exe.c_str(), (unsigned)port, worldArg.c_str(), rootArg.c_str(), ownerArg.c_str(),
                                 (unsigned long)::GetCurrentProcessId());
    cmd[sizeof(cmd) - 1] = 0;
    if (cmdLen < 0)
    {
        ::InterlockedIncrement64(&g_panelNbCmdTooLong);
        g_nbState = coopui::kNbSpawnFailed;
        ErrorLog("[UI] the world's notebook was NOT started: its command line does not fit 1024 characters."
                 " That is the path to SharedWastelandsServer.exe, the notebook folder and the player id together - a very"
                 " long folder name is the usual cause. NOTHING was launched, because a truncated command line"
                 " would start the notebook on the wrong folder or without its operator. Counted"
                 " nbCmdTooLong; the panel reports the same state it does for a CreateProcess failure.");
        return 0;
    }

    STARTUPINFOA si;
    ::ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    ::ZeroMemory(&pi, sizeof pi);

    const BOOL ok = ::CreateProcessA(exe.c_str(), cmd, 0, 0, FALSE,
                                     CREATE_NO_WINDOW | DETACHED_PROCESS,
                                     0, dir.c_str(), &si, &pi);
    if (ok == FALSE)
    {
        const DWORD e = ::GetLastError();
        ::InterlockedIncrement64(&g_panelNbSpawnFail);
        g_nbState = coopui::kNbSpawnFailed;
        char b[320];
        _snprintf(b, 319, "[UI] CreateProcess for the world's notebook FAILED, Windows error %lu."
                          " No notebook was started and nothing else was changed.", (unsigned long)e);
        b[319] = 0;
        ErrorLog(b);
        return 0;
    }

    ::CloseHandle(pi.hThread);      /* design 2.2: the thread handle at once, the process handle kept */
    g_nbProc     = pi.hProcess;
    g_nbState    = coopui::kNbStarting;
    g_nbWorld    = coopworld::WorldFolderName(worldArg);   /* mp3: the world this computer's helper now runs (F874 note, Delete refusal) */
    g_nbExitCode = 0;
    ::InterlockedIncrement64(&g_panelNbSpawned);
    DebugLog("[UI] the MULTIPLAYER panel started the world's notebook: " + exe + " --world " + worldArg + " (its folder " + sdir + ")"
             + " (no console window, handles not inherited, working directory " + dir + "). Started is NOT"
             " running: the proof it is up is the notebook's own WELCOME, which the status area waits for."
             " NOTHING in this plugin will ever kill it - see design-ui-panel 2.2.");
    return 1;
}

/* TITLE PUMP, and the in-world tick (UiHostingServerWatch) - both MAIN THREAD: has the notebook we started stopped?  The
   NEGATIVE proof of design 2.2.
   THROTTLED TO 4 Hz, and the throttle is on COST, not a wait for an event: the title pump runs at
   ~1,065 Hz (F655) and this would otherwise be a syscall per tick for the whole of a hosting session.
   The condition is re-asked on every eligible tick and nothing here is timed against anything. */
static void PanelPollNotebook()
{
    if (g_nbProc == 0) return;
    if (g_nbState == coopui::kNbStoppedEarly) return;
    const DWORD nowMs = ::GetTickCount();
    if ((DWORD)(nowMs - g_nbLastPollMs) < kPanelStatusMs) return;
    g_nbLastPollMs = nowMs;
    DWORD code = 0;
    if (::GetExitCodeProcess(g_nbProc, &code) == 0) return;
    if (code == STILL_ACTIVE) return;
    g_nbExitCode = (long)code;
    g_nbState    = coopui::kNbStoppedEarly;
    ::InterlockedIncrement64(&g_panelNbExited);
    ::CloseHandle(g_nbProc);
    g_nbProc = 0;
    ++g_panelStatusSeq;   /* the status area must say this on the next push, not in 250 ms */
}

/* ------------------------------------------------------------------------------------------------
   mp3 (design-mpmenu1 section 4) - YOUR WORLDS.  One folder per world inside the helper's top folder (coop-store\<world>,
   W2), each with the world.txt the helper writes at its first start; coopworld::BuildWorldTable (pure, offline-tested)
   turns the listing into rows, newest first.  "Last played" is the newest file time in the folder.  PLAYERS ARE NOT
   SHOWN: no world records its players' names yet (design section 4: "player names per world NOT recorded").
   Title pump only, when the screen opens and after New world / Delete - never per tick.
   ------------------------------------------------------------------------------------------------ */
static bool PanelReadSmall(const std::string& path, std::string* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == 0) return false;
    char buf[512];
    const size_t got = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    out->assign(buf, got);
    return true;
}

static long long PanelUnixOf(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const unsigned long long kEpoch = 116444736000000000ULL;   /* 1601-01-01 to 1970-01-01 in 100 ns steps */
    if (u.QuadPart < kEpoch) return 0;
    return (long long)((u.QuadPart - kEpoch) / 10000000ULL);
}

static void PanelScanWorlds()
{
    ::InterlockedIncrement64(&g_worldScans);
    const std::string top = StoreTopDir();
    std::vector<coopworld::WorldFolderInput> in;
    std::map<std::string, int> freshByFolder;
    WIN32_FIND_DATAA fd;
    HANDLE h = ::FindFirstFileA((top + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            const std::string name(fd.cFileName);
            if (name == "." || name == "..") continue;
            coopworld::WorldFolderInput w;
            w.folder = name;
            const std::string dir = top + "\\" + name;
            if (!PanelReadSmall(dir + "\\world.txt", &w.worldTxt)) w.worldTxt.clear();
            long long newest = 0;
            int others = 0;
            WIN32_FIND_DATAA fe;
            HANDLE g = ::FindFirstFileA((dir + "\\*").c_str(), &fe);
            if (g != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fe.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
                    if (coopcfg::CfgLower(std::string(fe.cFileName)) != "world.txt") ++others;
                    const long long t = PanelUnixOf(fe.ftLastWriteTime);
                    if (t > newest) newest = t;
                } while (::FindNextFileA(g, &fe));
                ::FindClose(g);
            }
            if (newest > 0) w.lastPlayed = coopui::PanelNum(newest);
            freshByFolder[coopworld::WorldFolderName(name)] = (others == 0) ? 1 : 0;
            in.push_back(w);
        } while (::FindNextFileA(h, &fd));
        ::FindClose(h);
    }
    std::vector<std::string> skipped;
    g_worlds = coopworld::BuildWorldTable(in, &skipped);
    ++g_worldListGen;   /* ui2: the list on screen is refilled on the next push */
    g_worldFresh.assign(g_worlds.size(), 0);
    bool selFound = false;
    for (size_t i = 0; i < g_worlds.size(); ++i)
    {
        g_worldFresh[i] = freshByFolder[g_worlds[i].folder];
        if (g_worlds[i].folder == g_worldSel) selFound = true;
    }
    if (!selFound)
    {
        /* the first visit, or the picked world went away: the world the settings file names, else the newest */
        g_worldSel.clear();
        const std::string cfgFolder = coopworld::WorldFolderName(g_fWorld);
        for (size_t i = 0; i < g_worlds.size(); ++i) if (g_worlds[i].folder == cfgFolder) g_worldSel = cfgFolder;
        if (g_worldSel.empty() && !g_worlds.empty()) g_worldSel = g_worlds[0].folder;
    }
    ::InterlockedExchange64(&g_worldsListed, (LONG64)g_worlds.size());
    ::InterlockedExchange64(&g_worldsSkipped, (LONG64)skipped.size());
    std::string sk;
    for (size_t i = 0; i < skipped.size() && i < 4; ++i) sk += std::string(i ? "; " : "") + skipped[i];
    DebugLog("[UI] mp3: worlds listed in " + top + ": " + coopui::PanelNum((long long)g_worlds.size())
             + ", folders skipped " + coopui::PanelNum((long long)skipped.size()) + (sk.empty() ? std::string() : " (" + sk + ")")
             + ", picked '" + g_worldSel + "'");
}

static const coopworld::WorldRow* PanelPickedRow()
{
    for (size_t i = 0; i < g_worlds.size(); ++i) if (g_worlds[i].folder == g_worldSel) return &g_worlds[i];
    return 0;
}

/* NEW WORLD: the name passes coopworld::WorldNameOk (the World box's rule - CfgNameFieldOk, 48 characters - plus what
   Windows refuses in a folder name), then its folder is made with the same world.txt the helper would write
   (coopworld::WorldTxtFormat), so the list shows it at once and the helper reads it back as "matched". */
static void PanelCreateWorld()
{
    const std::string name = coopcfg::CfgTrim(g_fNewWorld);
    std::string why;
    if (!coopworld::WorldNameOk(name, &why)) { g_dlgText = why; return; }   /* the dialog stays open and says why */
    const std::string folder = coopworld::WorldFolderName(name);
    for (size_t i = 0; i < g_worlds.size(); ++i)
    {
        if (g_worlds[i].folder != folder) continue;
        g_worldSel = folder;
        g_panelScreen = 1;
        PanelSetStatus("A world named \"" + g_worlds[i].name + "\" already exists. It is now selected.");
        return;
    }
    const std::string top = StoreTopDir();
    ::CreateDirectoryA(top.c_str(), 0);   /* the first world on this computer: the top folder may not be there yet */
    const std::string dir = top + "\\" + folder;
    if (::CreateDirectoryA(dir.c_str(), 0) == 0)
    {
        const DWORD e = ::GetLastError();
        std::string had;
        /* A folder with no world.txt is one this game made for a world whose helper never started: it becomes this
           world.  A folder WITH one is something BuildWorldTable skipped (another world's), and is left alone. */
        if (e != ERROR_ALREADY_EXISTS || PanelReadSmall(dir + "\\world.txt", &had))
        {
            ::InterlockedIncrement64(&g_worldCreateFailed);
            ErrorLog("[UI] mp3: New world '" + name + "' NOT made: " + dir
                     + (e == ERROR_ALREADY_EXISTS ? std::string(" already holds a world.txt that is not this world's")
                                                  : " could not be created, Windows error " + coopui::PanelNum((long long)e)));
            g_dlgText = (e == ERROR_ALREADY_EXISTS)
                ? std::string("That name is already in use. Choose another name.")
                : "Couldn't create the world (error " + coopui::PanelNum((long long)e) + ").";
            return;
        }
    }
    const long long made = (long long)std::time(0);
    LARGE_INTEGER pc; pc.QuadPart = 0; ::QueryPerformanceCounter(&pc);
    const std::string worldId = coopworld::WorldIdMake(made, coopworld::WorldIdMix((unsigned long long)pc.QuadPart, (unsigned long long)::GetCurrentProcessId(),
                                                                                   (unsigned long long)::GetTickCount()));
    const std::string txt = coopworld::WorldTxtFormatWithId(name, made, worldId, false);   /* T-490: line 2, the world's id, born now */
    bool ok = false;
    {   /* a temp file, flushed to the disk, then renamed - the road every identity file takes */
        const std::string wt = dir + "\\world.txt", tmp = wt + ".tmp";
        HANDLE h = ::CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD put = 0;
            ok = ::WriteFile(h, txt.data(), (DWORD)txt.size(), &put, 0) != 0 && put == (DWORD)txt.size() && ::FlushFileBuffers(h) != 0;
            ::CloseHandle(h);
            ok = ok && ::MoveFileExA(tmp.c_str(), wt.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!ok) ::DeleteFileA(tmp.c_str());
        }
    }
    if (!ok)
    {
        ::InterlockedIncrement64(&g_worldCreateFailed);
        ErrorLog("[UI] mp3: New world '" + name + "' NOT made: " + dir + "\\world.txt could not be written");
        g_dlgText = "Couldn't create the world.";
        return;
    }
    ::InterlockedIncrement64(&g_worldsCreated);
    DebugLog("[UI] mp3: New world '" + name + "' made: " + dir + "\\world.txt written, id " + worldId + " (the helper reads it back at its first start)");
    PanelScanWorlds();
    g_worldSel = folder;
    g_panelScreen = 1;
    PanelSetStatus("\"" + name + "\" created. Press HOST to start.");
}

/* DELETE refuses a world this game is using: its record index is read, this computer's helper was started for it, or
   the settings this game armed with name it.  A helper left running from an earlier Kenshi session is not known here;
   its open files make the move fail, and the failure is said in words. */
static int PanelWorldInUse(const std::string& folder)
{
    if (folder.empty()) return 0;
    if (PanelUsedWorld() == folder) return 1;
    if (ConfigArmGen() > 0 && ConfigRole() != kRoleSingle
        && coopworld::WorldFolderName(coopworld::WorldOrDefault(ConfigWorldKey(), 0)) == folder) return 1;
    return 0;
}

static void PanelDeleteRefused(const std::string& name)
{
    ::InterlockedIncrement64(&g_worldDeleteRefused);
    PanelSetStatus("\"" + name + "\" is in use. Restart Kenshi, then delete it.");
}

static void PanelDeletePress()
{
    const coopworld::WorldRow* r = PanelPickedRow();
    if (g_panelScreen != 1 || r == 0) return;
    if (PanelWorldInUse(r->folder)) { PanelDeleteRefused(r->name); return; }
    g_dlgText = "Delete \"" + r->name + "\"?\n\nThis deletes the world for all players, including bases, items and game"
                " time. Other players' saves for it will no longer work.\n\nThe world and this computer's saves of it are moved to the Recycle Bin.";
    g_panelScreen = 4;
}

/* The confirmed delete: coop-store\<world> goes to the Recycle Bin through SHFileOperationA with FOF_ALLOWUNDO, never a
   hard delete.  FOF_WANTNUKEWARNING makes Windows ask rather than silently destroy anything the Bin cannot take.  Then this
   computer's saves of the world follow it (StoreWorldRecycleLeftovers): they are found by the world's name, so a new world of the
   same name would otherwise load them and be refused. */
static void PanelDeleteConfirmed()
{
    const coopworld::WorldRow* r = PanelPickedRow();
    g_panelScreen = 1;
    if (r == 0) return;
    const std::string name = r->name, folder = r->folder;   /* copies: the rescan below rebuilds g_worlds */
    if (PanelWorldInUse(folder)) { PanelDeleteRefused(name); return; }
    std::string path = StoreTopDir() + "\\" + folder;
    std::string worldId;   /* T-490: read BEFORE the folder moves - the saves and records stores that follow it are this world's by id */
    { std::string t; bool up = false; if (PanelReadSmall(path + "\\world.txt", &t)) coopworld::WorldTxtParseId(t, &worldId, &up); }
    {
        char full[MAX_PATH * 2];
        const DWORD got = ::GetFullPathNameA(path.c_str(), (DWORD)sizeof full, full, 0);
        if (got > 0 && got < (DWORD)sizeof full) path = full;   /* the Recycle Bin needs a full path */
    }
    std::vector<char> from(path.begin(), path.end());
    from.push_back(0);
    from.push_back(0);   /* pFrom is a list ended by an empty string */
    SHFILEOPSTRUCTA op;
    ::ZeroMemory(&op, sizeof op);
    op.hwnd   = 0;
    op.wFunc  = FO_DELETE;
    op.pFrom  = &from[0];
    op.fFlags = (FILEOP_FLAGS)(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_WANTNUKEWARNING);
    const int rc = ::SHFileOperationA(&op);
    const bool gone = (::GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES);
    if (rc != 0 || op.fAnyOperationsAborted || !gone)
    {
        ::InterlockedIncrement64(&g_worldDeleteFailed);
        ErrorLog("[UI] mp3: world '" + name + "' NOT moved to the Recycle Bin: SHFileOperation returned "
                 + coopui::PanelNum((long long)rc) + (op.fAnyOperationsAborted ? ", aborted" : "")
                 + (gone ? ", the folder is gone" : ", the folder is still there") + " (" + path + ")");
        PanelScanWorlds();
        PanelSetStatus("Couldn't delete \"" + name + "\" - its files are in use. Restart your computer and try again.");
        return;
    }
    ::InterlockedIncrement64(&g_worldsDeleted);
    DebugLog("[UI] mp3: world '" + name + "' moved to the Recycle Bin (" + path + ")");
    std::string left;
    const int failed = StoreWorldRecycleLeftovers(name, worldId, &left);   /* saves this game uses stay and are not failures; only a move that did not happen is said */
    DebugLog("[WORLD] delete '" + name + "': " + left);
    g_worldSel.clear();
    PanelScanWorlds();
    PanelSetStatus("\"" + name + "\" moved to the Recycle Bin."
                   + (failed > 0 ? std::string(" Some of its saves couldn't be moved and are still on this computer.") : std::string()));
}

/* T-201 N1 fold - THIS GAME HOSTS A WORLD NOW: a host role that armed, no start failure, no press in progress, a world in use,
   and this computer's world server not stopped on its own (N1 re-check: without that test a dead world server kept the
   menu on HOSTING and a HOST press on the same world only showed HOSTING, so nothing could restart it). */
static bool PanelHostingNow()
{
    return g_pressBusy == 0 && ConfigRole() == kRoleHost && ConfigArmGen() > 0 && !PanelUsedWorld().empty()
        && g_nbState != coopui::kNbStoppedEarly;
}
/* T-201 N1 - THE PRESS: begin, cancel, and the per-tick verdict (coopui::PressVerdictOf - pure, swept offline). */
static void PressBegin(int mode, const std::string& world, const std::string& addr)
{
    g_pressBusy = mode;
    g_pressAtMs = ::GetTickCount();
    g_pressLinkedAtMs = 0;
    g_pressArmGen = ConfigArmGen();
    g_pressBoxReturn = 0;
    g_profPrefillDone = 0;   /* T-201 PP6' */
    ::InterlockedIncrement64(&g_pressStarted);
    PanelSetStatus(coopui::PressStatusText(mode == 1 ? 0 : 1, world, addr));
    if (mode == 1)
        DebugLog("[UI] T-201 N1: HOST pressed - starting \"" + world + "\" on port " + coopcfg::CfgTrim(g_fPort) + "; the links open on the next title tick");
    else
        DebugLog("[UI] T-201 N1: JOIN pressed - connecting to " + addr + "; the links open on the next title tick");
}
static void PressCancel(const char* how)
{
    const int mode = g_pressBusy;
    g_pressBusy = 0;
    ::InterlockedIncrement64(&g_pressCancelled);
    DebugLog(std::string("[UI] T-201 N1: CANCEL (") + how + ") - the " + (mode == 1 ? "HOST" : "JOIN") + " in progress is stopped after "
             + coopui::PanelNum((long long)(DWORD)(::GetTickCount() - g_pressAtMs)) + " ms; back to the screen's opening line");
    ConfigLeave(mode == 1 ? "CANCEL on HOST GAME" : "CANCEL on JOIN GAME");
    PanelIntro();
}
/* Title pump, every tick while a press is in progress. 1 = the screen changed and wants a push. */
static int PressTick()
{
    if (g_pressBusy == 0) return 0;
    /* T-201 N1 fold (finding 2): whatever took the panel down - X, Escape, MULTIPLAYER or the engine's own close
       (UiCloseFromEngine) - a press with the panel no longer wanted is cancelled here, the one place every route passes. */
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0) { PressCancel("the panel was closed"); return 0; }
    const DWORD now = ::GetTickCount();
    coopui::PressFacts p;
    p.mode            = (g_pressBusy == 1) ? 0 : 1;
    p.armed           = (ConfigArmGen() != g_pressArmGen) ? 1 : 0;
    p.nbState         = g_nbState;
    p.nbExit          = g_nbExitCode;
    p.nbPort          = g_nbPort;
    p.port            = (int)ConfigHostPort();
    p.worldLinked     = StoreLinkConnected() ? 1 : 0;   /* JOIN goes through the world server alone; linked before its WELCOME, which the profile lobby holds back */
    p.notebookUp      = (StoreWelcomedThisLink() != 0 && StoreLinkIsUp()) ? 1 : 0;
    p.dials           = ConfigDialCount();
    p.dialCap         = ConfigDialCap();
    p.refusal         = StoreRefusedReason();
    p.wrongWorld      = StoreWrongWorldRefusal(&p.openedWorld, &p.theirWorld);   /* T-201 N1 fold */
    p.lobbyGen        = StoreLobbyAnswerGen();   /* T552: the world's profile lobby answer counts as its answer (read before the generation) */
    p.linkGen         = StoreLinkGen();
    long long modsSeq = 0;
    const std::string mods = StoreModsRefusal(&modsSeq);
    if (p.mode == 1 && p.worldLinked != 0 && g_pressLinkedAtMs == 0)
    {
        g_pressLinkedAtMs = (now != 0) ? now : 1;
        DebugLog("[UI] T-201 N1: JOIN - the world server's link came up after " + coopui::PanelNum((long long)(DWORD)(now - g_pressAtMs))
                 + " ms; waiting for its WELCOME or profile lobby");
    }
    p.elapsedMs = (unsigned)(DWORD)(now - g_pressAtMs);
    p.linkedMs  = (g_pressLinkedAtMs != 0) ? (unsigned)(DWORD)(now - g_pressLinkedAtMs) : 0u;
    std::string box;
    const int v = coopui::PressVerdictOf(p, mods, &box);
    if (v == coopui::kPressWait) return 0;
    const int mode = g_pressBusy;
    g_pressBusy = 0;
    const std::string what = (mode == 1) ? "HOST" : "JOIN";
    const std::string ms = coopui::PanelNum((long long)p.elapsedMs);
    if (v == coopui::kPressDone)
    {
        ::InterlockedIncrement64(&g_pressDone);
        DebugLog("[UI] T-201 N1: " + what + " done after " + ms + " ms - "
                 + (mode == 1 ? "hosting \"" + g_fWorld + "\" on port " + coopui::PanelNum((long long)p.port) + "; the world answered"
                              : std::string("in the host's world: the world server answered"))
                 + (p.notebookUp != 0 ? std::string(" (WELCOME)")
                                      : " with its profile lobby on link generation " + coopui::PanelNum((long long)p.linkGen) + " - the WELCOME follows a profile pick (T552)"));
        if (mode == 1) LoadBegin(1);   /* T-201 PP6': no HOSTING screen - Starting "<world>"... stays up while the profile is picked and admitted */
        if (mode == 2) PanelSetStatus(std::string());   /* T-201 N1 fold (finding 5): Connecting comes down, CANCEL is BACK again, JOIN greys while linked (PanelPush); PROFILES follows when its list arrives */
        return 1;
    }
    ::InterlockedIncrement64(&g_pressFailed);
    ErrorLog("[UI] T-201 N1: " + what + " FAILED after " + ms + " ms - the " + coopui::PressBoxTitle(p.mode) + " box: " + box);
    ConfigLeave(mode == 1 ? "a failed HOST" : "a failed JOIN");
    ::InterlockedExchange(&g_panelWanted, 0);   /* the box shows once the panel is down; its OK brings the screen back */
    UiRaiseNoticeTitled(box, coopui::PressBoxTitle(p.mode));
    g_pressBoxReturn = mode;
    PanelIntro();
    return 1;
}
/* Title pump: a failure box that has been closed (OK or Escape) returns to HOST GAME / JOIN GAME with its opening line. */
static void PressBoxReturnTick()
{
    if (g_pressBoxReturn == 0 && g_nameTakenBox == 0) return;
    if (::InterlockedCompareExchange(&g_noticeWanted, 0, 0) != 0) return;
    const int mode = g_nameTakenBox != 0 ? 2 : g_pressBoxReturn;   /* T-201 N1b (owner 166): the name-taken box returns to JOIN GAME */
    if (g_nameTakenBox != 0) g_nameFocusWanted = 1;               /* ...with the PLAYER NAME box selected (PanelNameFocusTick) */
    g_nameTakenBox = 0;
    g_pressBoxReturn = 0;
    g_panelMode   = (mode == 1) ? 0 : 1;
    g_panelScreen = mode;
    PanelIntro();
    if (mode == 1) PanelScanWorlds();
    ::InterlockedExchange(&g_panelWanted, 1);
    DebugLog(std::string("[UI] T-201 N1: the ") + coopui::PressBoxTitle(mode == 1 ? 0 : 1) + " box was closed - back to "
             + (mode == 1 ? "HOST GAME" : "JOIN GAME") + " with its opening line; the press works again (no restart)");
}

/* T-201 PP6' - THE ONE PRESS, FROM THE WORLD'S ANSWER TO THE LOAD. */
static void LoadBegin(int mode)
{
    g_loadStage = 1; g_loadMode = mode; g_loadAtMs = ::GetTickCount(); g_loadVerdict = -1; g_loadTries = 0; g_loadNewAsked = 0; g_loadHeld = 0;
    g_loadFolder.clear(); g_loadProfile.clear();
    DebugLog(std::string("[AUTOLOAD] T-201 PP6': ") + (mode == 1 ? "HOST" : "JOIN") + " - waiting for the world to admit the profile");
}
/* A failure: the box (none when box is empty - the screen's opening line says the rest), the world left, and the box's OK returns
   to HOST GAME / JOIN GAME where the press works again (PressBoxReturnTick). */
static void LoadFail(const std::string& box, const char* title, const std::string& why)
{
    const int mode = g_loadMode;
    g_loadStage = 0; g_loadMode = 0; g_profBusy = 0;
    ErrorLog("[AUTOLOAD] T-201 PP6': " + std::string(mode == 1 ? "HOST" : "JOIN") + " FAILED - " + why
             + (box.empty() ? std::string(" (no box)") : " - the " + std::string(title) + " box: " + box));
    ConfigLeave(mode == 1 ? "a HOST that could not load" : "a JOIN that could not load");
    ::InterlockedExchange(&g_panelWanted, 0);
    if (!box.empty()) UiRaiseNoticeTitled(box, title);
    g_pressBoxReturn = mode;
    PanelIntro();
}
static void LoadCancel(const char* how)
{
    const int mode = g_loadMode;
    const int stage = g_loadStage;
    g_loadStage = 0; g_loadMode = 0; g_profBusy = 0;
    ::InterlockedIncrement64(&g_pressCancelled);
    DebugLog(std::string("[AUTOLOAD] T-201 PP6': CANCEL (") + how + ") " + (stage == 2 ? "while the line waited for the engine (T-220: nothing was posted)" : "while the world admitted the profile")
             + " - left; back to "
             + (mode == 1 ? "HOST GAME" : "JOIN GAME") + "'s opening line");
    ConfigLeave(mode == 1 ? "CANCEL on HOST GAME" : "CANCEL on PROFILES");
    g_panelMode = (mode == 1) ? 0 : 1;
    g_panelScreen = mode;
    PanelIntro();
}
/* The PROFILES rows as the world's rows (num / name / last played) for coopprof's pure picks. */
static std::vector<coopprof::Row> PanelProfRowsAsProf()
{
    std::vector<coopprof::Row> v;
    for (size_t k = 0; k < g_profList.size(); ++k)
    { coopprof::Row r; r.num = g_profList[k].num; r.name = g_profList[k].name; r.faction = g_profList[k].faction; r.lastPlayed = g_profList[k].lastPlayed; v.push_back(r); }
    return v;
}
/* HOST (owner 134): the world's lobby list is here - play the profile HOST GAME's PROFILE row named (coopprof::HostProfileChoose over
   the WORLD's list, the authority), asking the world to make it first when it is new. */
static void LoadHostPickStep(unsigned select, int verdict, const std::string& say)
{
    if (verdict != 0) { LoadFail(say, "CAN'T HOST", "the world refused the profile request"); return; }
    if (StoreProfilesWaiting() == 0) return;   /* picked already - the WELCOME follows */
    unsigned num = 0;
    std::string name;
    if (select != 0 && g_loadNewAsked != 0) num = select;   /* the answer to our NEW: the profile it made */
    else
    {
        const coopworld::WorldRow* r = PanelPickedRow();
        const bool same = r != 0 && r->folder == g_hostProfWorld;
        const coopprof::HostProfileChoice c = coopprof::HostProfileChoose(PanelProfRowsAsProf(), same ? g_hostProfNum : 0u,
                                                                          same ? g_hostProfNew : std::string(), coopcfg::CfgTrim(g_fName));
        if (c.isNew != 0)
        {
            if (g_loadNewAsked != 0) return;
            if (!StoreProfileRequest(kProfReqNew, 0, c.name)) { LoadFail(coopui::kPressHostLateText, "CAN'T HOST", "the world's link went down before the new profile was asked for"); return; }
            g_loadNewAsked = 1;
            ::InterlockedIncrement64(&g_profNewAsked);
            DebugLog("[AUTOLOAD] T-201 PP6': HOST - this player has no profile '" + c.name + "' in the world yet - asking the world to make it");
            return;
        }
        num = c.num;
    }
    for (size_t k = 0; k < g_profList.size(); ++k) if (g_profList[k].num == num) name = g_profList[k].name;
    if (!StoreProfilePickFromPanel(num)) { LoadFail(coopui::kHostPickFailText, "CAN'T HOST", "the pick could not be sent"); return; }   /* owner 176 C */
    ::InterlockedIncrement64(&g_profPicked);
    DebugLog("[AUTOLOAD] T-201 PP6': HOST plays profile " + coopui::PanelNum((long long)num) + " '" + name + "' (" + (g_loadNewAsked ? "made now" : "chosen") + ")");
}
/* Title pump, every tick (after PressTick). 1 = the screen changed and wants a push. */
static int LoadTick()
{
    if (g_loadStage == 0 || g_loadStage == 3) return 0;
    if (g_loadStage == 2)   /* T-201 PP6' fold (LOW 7): the line STAYS on screen until the action can go - then the panel goes, and the action
                               follows at the tail once its widgets are gone (stage 3). A load waits for the engine to take a request (item 6). */
    {
        if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0) { LoadCancel("the panel was closed"); return 0; }   /* T-220: X / Escape at stage 2 - nothing posted */
        std::string f2, p2;
        int fo2 = 0, q2 = 0, n2 = 0;
        if (StoreAutoLoadFacts(&f2, &fo2, &q2, &n2, &p2) == 0 || f2 != g_loadFolder)
        { LoadFail(coopui::kLoadLinkLostText, "CAN'T LOAD", "the world's link or the pick changed while the line was up"); return 1; }   /* owner 176 D */
        const DWORD shown = (DWORD)(::GetTickCount() - g_loadAtMs);
        const int ready = g_loadVerdict == coopprof::kAutoLoadLoad ? LoadPostReady() : 1;
        const int post = coopui::LoadPostStep(ready, (unsigned)shown);
        if (post == coopui::kPostWait)
        {
            if (g_loadTries++ == 0) DebugLog("[AUTOLOAD] T-201 PP6': a save request is pending (SaveManager+0xA0) - the line stays up until it is done");
            return 0;
        }
        if (post == coopui::kPostFail)
        {
            LoadFail(coopui::kLoadFailText, "CAN'T LOAD", std::string(ready < 0 ? "the engine cannot take a load on this title (no SaveManager, anySavesExist() false, or +0xA0 unreadable)"
                                                                               : "a save request stayed pending") + " after " + coopui::PanelNum((long long)shown) + " ms");   /* owner 176 A */
            return 1;
        }
        ::InterlockedExchange(&g_panelWanted, 0);
        g_loadStage = 3; g_loadTries = 0; g_loadAtMs = ::GetTickCount();
        DebugLog("[AUTOLOAD] T-201 PP6': the line was up " + coopui::PanelNum((long long)shown) + " ms; the panel closes; the "
                 + std::string(g_loadVerdict == coopprof::kAutoLoadLoad ? "load" : "NEW GAME window") + " follows at the title's tail once it is gone");
        return 0;
    }
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0) { LoadCancel("the panel was closed"); return 0; }
    if (g_loadHeld != 0)   /* the join gate held this load for the world's operator: the roster that shows it in the world releases it */
    {
        const int h = StoreJoinHoldPoll(coopjoin::kHoldPress);
        if (h == 1) return 0;   /* still held - the panel keeps the line it shows */
        g_loadHeld = 0;
        if (h == 0) g_loadAtMs = ::GetTickCount();   /* the hold was dropped (link down, another refusal): the admit wait and its CAN'T JOIN run from now */
    }
    std::string folder, prof;
    int found = 0, quick = 0, never = 0;
    const DWORD waited = (DWORD)(::GetTickCount() - g_loadAtMs);
    const int ready = StoreAutoLoadFacts(&folder, &found, &quick, &never, &prof);
    const int v = ready != 0 ? coopprof::AutoLoadDecide(found != 0, quick != 0, never != 0) : -1;
    const int step = coopui::LoadAdmitStep(ready, v, (unsigned)waited, g_loadMode);
    if (step == coopui::kLoadStepWait) return 0;
    if (step == coopui::kLoadStepLate)
    {
        LoadFail(g_loadMode == 1 ? coopui::kPressHostLateText : coopui::kPressJoinLateText, coopui::PressBoxTitle(g_loadMode == 1 ? 0 : 1),
                 "the world did not admit the profile within " + coopui::PanelNum((long long)waited) + " ms");
        return 1;
    }
    DebugLog("[AUTOLOAD] decide profile='" + prof + "' verdict=" + coopprof::AutoLoadVerdictName(v) + " folder='" + folder + "' found=" + coopui::PanelNum((long long)found)
             + " quicksave=" + coopui::PanelNum((long long)quick) + " neverPlayed=" + coopui::PanelNum((long long)never) + " role=" + (g_loadMode == 1 ? "host" : "joiner"));
    if (step == coopui::kLoadStepMissing) { LoadFail(coopui::kSaveNotHereText, "CAN'T LOAD", "this profile was played before and its save is not on this computer"); return 1; }
    const int gate = StoreJoinGateAsk("the JOIN press's automatic load", coopjoin::kHoldPress, std::string());   /* the world road only; read live */
    if (gate == 2) { g_loadHeld = 1; return 0; }   /* refused for the operator alone: HELD, not cancelled - nothing new on screen */
    if (gate != 0) { LoadCancel("M11a S1: the join gate refused the load - the [STORE] JOIN gate line says why"); return 1; }
    g_loadVerdict = v; g_loadFolder = folder; g_loadProfile = prof; g_loadStage = 2; g_loadAtMs = ::GetTickCount(); g_loadTries = 0;
    const std::string line = coopui::LoadLineOf(v, prof);
    PanelSetStatus(line);
    g_profSay = line;
    return 1;
}

/* T-201 N1b (owner 166): after the name-taken box, JOIN GAME's PLAYER NAME box takes the key focus - looked up by name on the
   tick the panel is up, never kept across frames. */
static void PanelNameFocusTick(MyGUI::Gui* gui)
{
    if (g_nameFocusWanted == 0) return;
    if (g_panelScreen != 2) { g_nameFocusWanted = 0; return; }
    if (::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) == 0) return;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    MyGUI::Widget* w = UiFind(gui, NM().nameEdit);
    if (im == 0 || w == 0) return;
    im->setKeyFocusWidget(w);
    g_nameFocusWanted = 0;
    DebugLog("[UI] T-201 N1b: the name-taken box was closed - JOIN GAME with the PLAYER NAME box selected");
}

/* THE ACTION BUTTON.  Validate first, in the SAME words the file parser would refuse in (one
   implementation, in src/common/cfgtext.cpp), and on a refusal write NO FILE and change NOTHING. */
static void PanelGo()
{
    coopcfg::CfgFields f;
    std::string why;

    if (!coopcfg::CfgNameFieldOk(g_fWorld, "world name", coopcfg::kCfgWorldFieldMax, &why)
     || (!g_fSlot.empty() && !coopcfg::CfgNameFieldOk(g_fSlot,  "save name",  coopcfg::kCfgSlotFieldMax,  &why)))   /* T-201 PP5: HOST writes no slot= */
    {
        ::InterlockedIncrement64(&g_panelInvalid);
        PanelSetStatus(why);
        return;
    }
    f.slot  = coopcfg::CfgTrim(g_fSlot);
    f.world = coopcfg::CfgTrim(g_fWorld);
    /* B13 - CARRY THE PLAYER ID THROUGH. CfgFields starts empty and CfgFormat writes only what it is
       given, so a settings save that did not copy this line would DELETE this install's stable name: the
       next start would generate another one, and the notebook would hand this game a new slot number, no
       restored areas and none of its old records' ownership. Nobody types this field and the panel has no
       box for it. */
    f.playerId = ConfigPlayerId();
    f.playerName = coopcfg::CfgTrim(g_fName);   /* mp1: carried, like the id (ConfigWrite also falls back to the file's) */

    if (g_panelMode == 0)
    {
        unsigned short port = 0;
        if (!coopcfg::CfgPortFieldOk(g_fPort, &port, &why))
        {
            ::InterlockedIncrement64(&g_panelInvalid);
            PanelSetStatus(why);
            return;
        }
        /* THE WORLD SERVER THIS GAME STARTED STILL RUNS ON ANOTHER PORT (HOST, CANCEL, the PORT box changed, HOST again): it keeps
           that port, because nothing in this plugin may stop it (design-ui-panel 2.2 - no TerminateProcess, and the world server
           has no stop message the game can send) and a second one for the same world cannot open it. So this press rings the
           running one's port, and the PORT box, the settings and the hosting screen are set to that port, so the number shown to
           the host - and passed on to the joiners - is the one that answers. A restart of Kenshi frees the port box again. */
        if (g_panelNotebookMine != 0)
        {
            const int running = PanelNotebookRunningPort();
            if (running > 0 && running != (int)port)
            {
                DebugLog("[UI] HOST: the PORT box asks for port " + coopui::PanelNum((long long)port) + " but this computer's world server,"
                         " started by this game, still runs on port " + coopui::PanelNum((long long)running) + " and cannot be stopped from here"
                         " - this press hosts on port " + coopui::PanelNum((long long)running) + " and the PORT box is set back to it");
                port = (unsigned short)running;
                g_fPort = coopui::PanelNum((long long)running);
            }
        }
        f.role = coopcfg::kCfgHost;
        /* config.cpp rings the world server (store=) for a host and opens nothing on host=; host= keeps the PORT box's number for
           the hosting screen's checks - and it has to be there, because FallBackToSingle fires when the port is 0. */
        f.hostAddr = "0.0.0.0";
        f.hostPort = port;
        if (g_panelNotebookMine != 0)
        {
            f.storeAddr = "127.0.0.1";
            f.storePort = port;   /* the world server this computer starts listens on the PORT box's number: every player's one door */
        }
        else if (!coopcfg::CfgAddrFieldOk(g_fNotebookAddr, "world address", &f.storeAddr, &f.storePort, &why))
        {
            ::InterlockedIncrement64(&g_panelInvalid);
            PanelSetStatus(why);
            return;
        }
    }
    else
    {
        if (!coopcfg::CfgAddrFieldOk(g_fJoinAddr, "address", &f.hostAddr, &f.hostPort, &why))
        {
            ::InterlockedIncrement64(&g_panelInvalid);
            PanelSetStatus(why);
            return;
        }
        f.role = coopcfg::kCfgClient;
        /* NO store= line for a client: the address typed IS the world server's, and config.cpp rings host= when the settings
           name no store= (coopjoin::WorldDoorPick). */
    }

    /* T-201 N1: the one refusal left (W2b, another world) is asked BEFORE the file is written, so a refused press writes nothing. */
    {
        std::string sw;
        if (ConfigWorldSwitchRefused(f.world, "the MULTIPLAYER panel", &sw)) { ::InterlockedIncrement64(&g_panelArmRefused); PanelSetStatus(sw); return; }
    }
    std::string err;
    if (!ConfigWrite(f, &err))
    {
        ::InterlockedIncrement64(&g_panelInvalid);
        PanelSetStatus("Couldn't save your settings. " + err);
        return;
    }
    ::InterlockedIncrement64(&g_panelSaved);

    /* mp1: the file's path, the world and the save folder are the log's business, not the player's. */
    DebugLog("[UI] mp1: settings written to " + ConfigFilePath() + " - world '" + f.world + "', slot '" + f.slot + "'");
    /* words1 (owner wording audit 2026-09-27, section 7): no "settings saved" line - the player cannot act on it. */

    /* T-201 N1 - THE "Already connected" / "Already connecting ... Restart Kenshi" REFUSALS ARE GONE: a press always starts
       clean. Whatever an earlier press (or a test settings file) opened is closed first - ConfigLeave - and this press arms
       from nothing through ConfigRearmFromFile and the next title tick, as a first press does. */
    if (ConfigRole() != kRoleSingle || net::SessionLinked()) ConfigLeave("a new HOST / JOIN press");

    std::string armErr;
    if (!ConfigRearmFromFile("the MULTIPLAYER panel", &armErr))
    {
        ::InterlockedIncrement64(&g_panelArmRefused);
        static const char kSwitchRefused[] = "This game already opened world \"";   /* config.cpp ConfigRearmFromFile's whole sentence */
        if (armErr.compare(0, sizeof(kSwitchRefused) - 1, kSwitchRefused) == 0) { PanelSetStatus(armErr); return; }   /* words1b (owner 2026-09-27): said as it is */
        ErrorLog("[UI] T-201 N1 fold: the " + std::string(f.role == coopcfg::kCfgHost ? "HOST" : "JOIN") + " press could not arm: " + armErr);   /* the internal reason is the log's */
        PanelSetStatus(f.role == coopcfg::kCfgHost ? coopui::PanelNotebookText(coopui::kNbSpawnFailed, 0, 0) : std::string(coopui::kPressJoinLateText));   /* the approved box words for the mode */
        return;
    }
    ::InterlockedIncrement64(&g_panelArmRequested);
    StoreProfilesByPanel();   /* prof3: THE FLAG - this connection is the panel's, so its profile list waits for the player's pick */

    /* U2-c (review-u2 M-2) - THE NOTEBOOK IS STARTED ONLY FOR AN ACCEPTED ARM.  It used to be spawned
       above, BEFORE ConfigRearmFromFile could refuse, so a refused press left SharedWastelandsServer.exe running for
       a session that never started and nothing in this build ever stops it (there is no
       TerminateProcess anywhere - design 2.2).  It still runs before the arming itself happens, because
       ConfigRearmFromFile only clears the latch and the next title tick does the work - so the socket
       keeps the head start the old comment claimed.  Host only, and only when the player asked. */
    if (f.role == coopcfg::kCfgHost && g_panelNotebookMine != 0)
    {
        /* T-53: a world server newly started here asks the home router, in the background, to forward its one UDP port
           (upnp.cpp); the entry goes when that process ends or this game quits. Nothing is shown to the player (owner 382).
           routerport=0 in the settings file skips the router entirely, so test runs leave the home router alone. */
        const int started = PanelStartNotebook(f.storePort);
        if (coopupnp::MapOnHostStart(started))
        {
            if (ConfigRouterPort() != 0) UpnpHostStart(f.storePort, g_nbProc);
            else                         UpnpHostOff(f.storePort);
        }
    }

    /* T-201 N1: the screen stays up, greyed, with CANCEL, saying Starting "<world>"... / Connecting to <address>... until PressTick decides. */
    PressBegin(f.role == coopcfg::kCfgHost ? 1 : 2, g_fWorld, coopcfg::CfgTrim(g_fJoinAddr));
}

/* T-201 N1 fold (finding 1b): the picked world is the one this game hosts, on the port in the field. */
static bool PanelHostingSame()
{
    const coopworld::WorldRow* r = PanelPickedRow();
    if (r == 0 || !PanelHostingNow()) return false;
    return coopui::PressSameHosted(PanelUsedWorld(), r->folder, coopcfg::CfgTrim(g_fPort), (int)ConfigHostPort());
}

/* T-201 PP6' (owner 134 + 141 + 143) - HOST GAME's PROFILE row: the profile the HOST press plays, read from the picked world's own
   profiles.txt (this computer hosts it; the world is not running yet). 0 = no world picked. A CHANGE made for another world is
   forgotten when the pick moves. */
static bool PanelReadWorldFile(const std::string& folder, const char* file, std::string* out)
{
    out->clear();
    FILE* f = std::fopen((StoreTopDir() + "\\" + folder + "\\" + file).c_str(), "rb");
    if (f == 0) return false;
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0 && out->size() < 262144) out->append(buf, got);
    std::fclose(f);
    return true;
}
static bool PanelReadProfilesFile(const std::string& folder, std::string* out) { return PanelReadWorldFile(folder, "profiles.txt", out); }
static int PanelHostProfChoice(coopprof::HostProfileChoice* c)
{
    const coopworld::WorldRow* r = PanelPickedRow();
    if (r == 0) return 0;
    if (g_hostProfWorld != r->folder) { g_hostProfWorld = r->folder; g_hostProfNum = 0; g_hostProfNew.clear(); }
    std::string text;
    PanelReadProfilesFile(r->folder, &text);
    *c = coopprof::HostProfileChoose(coopprof::PersonActiveRows(text, ConfigPlayerId()), g_hostProfNum, g_hostProfNew, coopcfg::CfgTrim(g_fName));
    return 1;
}
/* CHANGE's list: this player's profiles in the picked world (its profiles.txt) and the world's profile limit (its options.txt
   `profilecap`, the default when absent - T-201 PP6' owner 178a: NEW PROFILE respects it before the press). */
static void PanelProfSelectLoad()
{
    std::string text;
    PanelReadProfilesFile(g_hostProfWorld, &text);
    const std::vector<coopprof::Row> mine = coopprof::PersonActiveRows(text, ConfigPlayerId());
    g_profList.clear();
    for (size_t k = 0; k < mine.size(); ++k)
    { StoreProfRow p; p.num = mine[k].num; p.name = mine[k].name; p.faction = mine[k].faction; p.lastPlayed = mine[k].lastPlayed; g_profList.push_back(p); }
    std::string opt;
    std::map<std::string, std::string> om;
    if (PanelReadWorldFile(g_hostProfWorld, "options.txt", &opt)) coopui::OptParseFile(opt, &om);
    g_profCapUi = coopprof::CapFromOption(om.count("profilecap") ? om["profilecap"] : std::string());
    g_profBusy = 0;
    ++g_profListGen;
}
/* CHANGE: PROFILES with SELECT as its main button, this player's profiles in the picked world (from its file); DELETE edits that file
   (owner 178a - the world is not running); NEW PROFILE names one that the HOST press makes. */
static void PanelProfSelectOpen()
{
    coopprof::HostProfileChoice c;
    if (PanelHostProfChoice(&c) == 0) return;
    PanelProfSelectLoad();
    g_profSay.clear();
    g_profSel = c.isNew != 0 ? 0u : c.num;
    g_profMode = 1;
    PanelProfOpen();
    DebugLog("[UI] T-201 PP6': CHANGE - PROFILES (SELECT) for world '" + g_hostProfWorld + "': " + coopui::PanelNum((long long)g_profList.size())
             + " profile(s) of this player; now '" + c.name + "'" + (c.isNew ? " (new)" : ""));
}

/* mp3 - HOST on the Host a game screen: the picked world and always this computer's own helper.  T-201 PP5: no slot= is
   written - every multiplayer save goes to the profile's own folder, decided when the profile is picked (store.cpp). */
static void PanelHostPress()
{
    const coopworld::WorldRow* r = PanelPickedRow();
    if (r == 0)
    {
        ::InterlockedIncrement64(&g_panelInvalid);
        PanelSetStatus("Select a world first, or press NEW WORLD.");
        return;
    }
    {   /* owner 429: a world folder a newer build wrote is not hosted - nothing is started and nothing in it is written (M4) */
        const std::string fp = StoreTopDir() + "\\" + r->folder + "\\" + swnames::kFormatFile;
        const bool exists = ::GetFileAttributesA(fp.c_str()) != INVALID_FILE_ATTRIBUTES;
        std::string t, why; unsigned int found = 0;
        if (exists) PanelReadSmall(fp, &t);
        const int v = swformat::FormatDecide(swformat::FormatParse(exists, t, swformat::kKindWorld, &found, &why), found, swformat::kWorldFolderFormat);
        if (swformat::FormatRefuses(v))
        {
            if (v == swformat::kFormatRefuseUnreadable) ErrorLog("[UI] owner 429: format.txt unreadable: " + why + " (" + fp + ")");
            ::InterlockedIncrement64(&g_panelArmRefused);
            ErrorLog("[UI] HOST '" + r->name + "' refused: " + fp + " says format " + coopui::PanelNum((long long)found) + " (" + swformat::FormatVerdictName(v)
                     + "); this build writes " + coopui::PanelNum((long long)swformat::kWorldFolderFormat) + " - nothing started");
            UiRaiseNoticeTitled(swformat::FormatHostRefusedText(), coopui::PressBoxTitle(0));
            return;
        }
    }
    g_fWorld = r->name;
    g_fSlot.clear();
    g_panelNotebookMine = 1;
    g_panelMode = 0;
    PanelGo();
}

/* mp5 (design-mpmenu1 section 5) - [C] GAME OPTIONS for the picked world.  The screen opens on what the world already
   holds (its options.txt beside world.txt), then this game's own live value, then the design's default; the host's
   earlier choices for the same world are shown on top.  Done keeps the rows the host changed; they go to the world
   with the Host press that arms (PanelOptHandOver), or at once (PanelOptDone) when this game already hosts that world - T-201 N1
   fold: a HOST press on the world already hosted, same port, arms nothing and only shows HOSTING, so Done is the way there. */
static bool PanelReadOptionsFile(const std::string& path, std::string* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == 0) return false;
    char buf[8192];
    const size_t got = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    out->assign(buf, got);
    return true;
}
static void PanelOptOpen()
{
    const coopworld::WorldRow* r = PanelPickedRow();
    if (r == 0) { PanelSetStatus("Select a world first, or press NEW WORLD."); return; }
    g_optWorld = r->folder;
    std::map<std::string, std::string> rec;
    std::string text;
    const bool have = PanelReadOptionsFile(StoreTopDir() + "\\" + r->folder + "\\options.txt", &text);
    if (have) { coopui::OptParseFile(text, &rec); ::InterlockedIncrement64(&g_optFileRead); }
    int fromWorld = 0;
    for (int i = 0; i < coopui::kOptCount; ++i)
    {
        const coopui::OptDef& d = coopui::OptDefs()[i];
        std::string v;
        std::map<std::string, std::string>::const_iterator it = rec.find(d.key);
        if (it != rec.end()) { v = coopui::OptCanon(i, it->second); if (!v.empty()) ++fromWorld; }
        if (v.empty() && d.tab != coopui::kOptTabCoop) v = coopui::OptCanon(i, SettingsLiveValue(d.key));
        if (v.empty()) v = coopui::OptCanon(i, d.dflt);
        g_optBase[i] = v;
        g_optVal[i] = v;
    }
    int shownChosen = 0;
    if (g_optChosenWorld == g_optWorld)
        for (size_t k = 0; k < g_optChosen.size(); ++k)
            for (int i = 0; i < coopui::kOptCount; ++i)
                if (g_optChosen[k].first == coopui::OptDefs()[i].key) { g_optVal[i] = g_optChosen[k].second; ++shownChosen; }
    g_optTab = coopui::kOptTabDifficulty;
    g_panelScreen = 6;
    ::InterlockedIncrement64(&g_optOpened);
    DebugLog("[UI] mp5: Game options for world '" + g_optWorld + "' - options.txt " + (have ? "read, " + coopui::PanelNum(fromWorld) + " of the screen's rows recorded" : std::string("absent"))
             + (shownChosen ? ", " + coopui::PanelNum(shownChosen) + " earlier choice(s) shown" : std::string()));
}
static void PanelOptDone()
{
    /* mp5b review: showing the choices in Kenshi's New Game Advanced window (settings.cpp NgLock) writes them into the
       live options, which this screen then reads as its starting point - a row chosen earlier for this world now
       equals its base and OptChanged alone would drop it. Such a row stays chosen, at the value on screen. */
    std::vector<std::pair<std::string, std::string> > prev;
    if (g_optChosenWorld == g_optWorld) prev = g_optChosen;
    g_optChosen = coopui::OptChanged(g_optVal, g_optBase);
    for (size_t k = 0; k < prev.size(); ++k)
    {
        bool have = false;
        for (size_t m = 0; m < g_optChosen.size() && !have; ++m) have = g_optChosen[m].first == prev[k].first;
        if (have) continue;
        for (int i = 0; i < coopui::kOptCount; ++i)
            if (coopui::OptDefs()[i].key == prev[k].first) { g_optChosen.push_back(std::make_pair(prev[k].first, g_optVal[i])); break; }
    }
    g_optChosenWorld = g_optWorld;
    ::InterlockedIncrement64(&g_optDoneCount);
    std::string what;
    for (size_t k = 0; k < g_optChosen.size(); ++k) what += " " + g_optChosen[k].first + "=" + g_optChosen[k].second;
    g_panelScreen = 1;
    const bool hostingThis = ConfigRole() == kRoleHost && ConfigArmGen() > 0 && PanelUsedWorld() == g_optWorld;
    int sent = 0;
    if (hostingThis) { sent = SettingsHostChoices(g_optChosen); ::InterlockedIncrement64(&g_optHandedOver); }
    DebugLog("[UI] mp5: Game options Done for world '" + g_optWorld + "' - " + coopui::PanelNum((long long)g_optChosen.size()) + " row(s) changed:"
             + (what.empty() ? std::string(" none") : what) + (hostingThis ? (sent ? " - this game hosts it: sent to the world record now" : " - this game hosts it: sent when the world's record arrives") : " - kept for the Host press"));
    if (hostingThis) PanelSetStatus(sent ? "Game options applied." : "Game options will apply when the world starts.");
    else PanelSetStatus(g_optChosen.empty() ? "No game options were changed." : "Game options will apply when you press HOST.");
}
static void PanelOptHandOver()
{
    const bool mine = !g_optChosenWorld.empty() && g_optChosenWorld == g_worldSel;
    const int sent = SettingsHostChoices(mine ? g_optChosen : std::vector<std::pair<std::string, std::string> >());
    if (mine && !g_optChosen.empty())
    {
        ::InterlockedIncrement64(&g_optHandedOver);
        DebugLog("[UI] mp5: Host for '" + g_worldSel + "' carries " + coopui::PanelNum((long long)g_optChosen.size()) + " Game options choice(s) - "
                 + (sent ? "sent to the world record now" : "sent when the world's record arrives, and written over this game's values at its load"));
    }
}
static void PanelOptAction(int act)
{
    if (act == kActGameOptions) { if (g_panelScreen == 1) PanelOptOpen(); return; }
    if (g_panelScreen != 6) return;
    if (act >= kActOptTab0 && act < kActOptTab0 + coopui::kOptTabCount) { g_optTab = act - kActOptTab0; return; }
    if (act == kActOptDone) { PanelOptDone(); return; }
    if (act == kActOptDefaults)
    {
        /* The open tab only.  Attacks on your base and limb loss have no fixed default here: they go back to this
           player's own Options values. */
        for (int i = 0; i < coopui::kOptCount; ++i)
        {
            const coopui::OptDef& d = coopui::OptDefs()[i];
            if (d.tab != (int)g_optTab) continue;
            const std::string v = coopui::OptCanon(i, d.dflt[0] != 0 ? std::string(d.dflt) : SettingsLiveValue(d.key));
            if (!v.empty()) g_optVal[i] = v;
        }
        return;
    }
    int row = -1, dir = 1;
    if (act >= kActOptDec0 && act < kActOptDec0 + coopui::kOptRowsShown) { row = act - kActOptDec0; dir = -1; }
    else if (act >= kActOptInc0 && act < kActOptInc0 + coopui::kOptRowsShown) row = act - kActOptInc0;
    else if (act >= kActOptTick0 && act < kActOptTick0 + coopui::kOptRowsShown) row = act - kActOptTick0;
    const int i = row < 0 ? -1 : coopui::OptIndexOf((int)g_optTab, row);
    if (i >= 0) g_optVal[i] = coopui::OptStep(i, g_optVal[i], dir);
}

/* `havePanel` is the LIVE answer from this tick's own lookup, not a remembered one.  Without it, an
   action that arrives on the frame the panel was destroyed would push module state into widgets that
   are gone and book a dozen panelMissing for something that is not a defect. */
static void PanelApply(MyGUI::Gui* gui, int act, int havePanel)
{
    if (g_pressBusy != 0)   /* T-201 N1: a press in progress - CANCEL (BACK's place) and Escape stop it; everything else is greyed */
    {
        if (act == kActBack) { PressCancel("CANCEL"); if (havePanel) PanelPush(gui); return; }
        if (act != kActToggle && act != kActClose) { ::InterlockedIncrement64(&g_pressIgnored); return; }
        PressCancel(act == kActClose ? "X / Escape" : "the MULTIPLAYER button");   /* T-201 N1 fold: then the panel closes - X, Escape and MULTIPLAYER alike */
    }
    if (g_loadStage != 0)   /* T-201 PP6': CANCEL / X / Escape leave while the world admits the profile; T-220: and while the line waits for the
                               engine (stage 2 - nothing posted yet); nothing once the post can go (coopui::LoadCancelAllowed) */
    {
        const int canCancel = coopui::LoadCancelAllowed(g_loadStage);
        if (canCancel != 0 && act == kActBack) { LoadCancel("CANCEL"); if (havePanel) PanelPush(gui); return; }
        if (canCancel == 0 || (act != kActToggle && act != kActClose)) { ::InterlockedIncrement64(&g_pressIgnored); return; }
        LoadCancel(act == kActClose ? "X / Escape" : "the MULTIPLAYER button");
    }
    if (act >= kActGameOptions && act <= kActOptDone) { PanelOptAction(act); if (havePanel) PanelPush(gui); return; }   /* mp5 */
    if (act >= kActProfPick && act <= kActChooseProfile)   /* prof3; ui2: the list's pick is read back here, on the title pump */
    { PanelProfAction(act, (act == kActProfPick && havePanel) ? UiListPicked(gui, NM().profList) : MyGUI::ITEM_NONE); if (havePanel) PanelPush(gui);
      if (havePanel && act == kActProfPick) UiFocusPanel(gui);   /* ui2b: Escape reaches the window again after a pick */
      return; }
    switch (act)
    {
        case kActToggle:
            if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) != 0)
            {
                ::InterlockedExchange(&g_panelWanted, 0);
            }
            else
            {
                PanelDefaults();
                g_panelFailStreak = 0;
                /* Opening the panel is the player asking again, so the give-up notice comes down with
                   the same click that starts the fresh set of tries. */
                UiClearNotice();
                IdBoxClear();   /* PP3d: opening the panel is CANCEL for the identity box (HOST / JOIN stay refused) */
                g_panelScreen = 0;   /* mp1: MULTIPLAYER opens on the Multiplayer screen (design-mpmenu1 section 2) ... */
                if (PanelHostingNow())   /* T-201 N1 fold (finding 1a): ... or, while this game hosts, on HOSTING - nothing is left or restarted */
                {
                    DebugLog("[UI] T-201 N1 fold: MULTIPLAYER while hosting - the HOSTING screen, hosting untouched");
                    PanelHostingOpen();
                }
                ::InterlockedExchange(&g_panelWanted, 1);
                ::InterlockedIncrement64(&g_panelOpened);
            }
            return;
        case kActClose:
            ::InterlockedExchange(&g_panelWanted, 0);
            return;
        case kActModeHost:
        case kActModeJoin:
        {
            PanelProfModeEnd("HOST GAME / JOIN GAME opened");
            /* T-201 N1b (owner 166): HOST GAME / JOIN GAME open their screen - PLAYER NAME is on it, kept by HOST / JOIN. */
            if (StoreProfilesWaiting())   /* prof3: still connected and nothing picked - Host a game / Join a game lead back to the profiles */
            {
                DebugLog("[UI] prof3: this game still waits for a profile pick - the Your profiles screen again");
                PanelProfOpen();
                if (havePanel) PanelPush(gui);
                return;
            }
            g_panelMode   = (act == kActModeHost) ? 0 : 1;
            g_panelScreen = (act == kActModeHost) ? 1 : 2;
            PanelIntro();
            if (act == kActModeHost) PanelScanWorlds();   /* mp3: the list is read from disk when the screen opens */
            if (havePanel) PanelPush(gui);
            return;
        }
        case kActBack:
            /* mp1: Back on the Host or Join screen goes to the Multiplayer screen; on the Multiplayer screen it closes. */
            if (g_panelScreen == 0) { ::InterlockedExchange(&g_panelWanted, 0); return; }
            if (g_panelScreen == 7)   /* prof3: LEAVE - the Join screen's own way back (there is no disconnect yet): to the Multiplayer screen */
            {
                if (g_profDlg != 0) g_profDlg = 0;
                else if (g_profMode == 1) { DebugLog("[UI] T-201 PP6': BACK on PROFILES (CHANGE) - HOST GAME, nothing chosen"); g_profMode = 0; g_panelScreen = 1; }
                else if (g_panelMode == 1)   /* T-201 PP6' (owner 159): BACK disconnects and returns to JOIN GAME */
                {
                    DebugLog("[UI] T-201 PP6': BACK on PROFILES - this game leaves the world; JOIN GAME");
                    ConfigLeave("BACK on PROFILES");
                    g_panelScreen = 2;
                    PanelIntro();
                }
                else { DebugLog("[UI] prof3: LEAVE pressed - back to the Multiplayer screen"); g_panelScreen = 0; }
                if (havePanel) PanelPush(gui);
                return;
            }
            /* mp3: a dialog's Back is its Cancel.  T-201 N1 fold (finding 6, manager decision): the Hosting screen's Back keeps
               hosting and goes to the Multiplayer screen, like Escape / MULTIPLAYER / X; leaving is the in-game EXIT GAME. A port
               that could not be opened never reaches HOSTING now (it is a CAN'T HOST box), so its branch is gone. */
            if (g_panelScreen == 5) DebugLog("[UI] T-201 N1 fold: BACK on HOSTING - still hosting; the Multiplayer screen");
            g_panelScreen = (g_panelScreen == 3 || g_panelScreen == 4 || g_panelScreen == 6) ? 1 : 0;   /* mp5: Game options' Back is Host a game, nothing kept */
            if (havePanel) PanelPush(gui);
            return;
        case kActHostProfChange:   /* T-201 PP6' (owner 143) */
            if (g_panelScreen == 1) PanelProfSelectOpen();
            if (havePanel) PanelPush(gui);
            return;
        case kActPaste:        PanelPaste();            if (havePanel) PanelPush(gui); return;
        case kActCopy:         if (g_panelScreen == 5) PanelCopy(0); if (havePanel) PanelPush(gui); return;   /* mp4 */
        case kActNetCopy:      /* INTERNET ADDRESS's COPY: copied now with an address; otherwise it waits for the router / the websites */
        case kActNetShow:      /* SHOW / HIDE: the same */
            if (g_panelScreen == 5 && coopui::PanelNetAddrButtonsOn(g_netAddrState) != 0)
            {
                const int wasShown = g_netAddrShown;
                if (act == kActNetShow) ::InterlockedIncrement64(&g_netAddrShowPressed);
                PanelNetAddrDo(coopui::PanelNetAddrPress(g_netAddrState, g_netAddrPending, act == kActNetShow ? coopui::kNetPressShow : coopui::kNetPressCopy));
                if (g_netAddrShown != wasShown) DebugLog(std::string("[UI] internet address ") + (g_netAddrShown != 0 ? "shown (SHOW)" : "hidden (HIDE)"));
                else if (g_netAddrPending != coopui::kNetPressNone) DebugLog(std::string("[UI] internet address: ") + (act == kActNetShow ? "SHOW" : "COPY") + " waits for an address");
            }
            if (havePanel) PanelPush(gui);
            return;
        case kActGo:
        {
            /* T-201 N1b (owner 166): HOST / JOIN wait for PLAYER NAME and keep it BEFORE the press (JOIN sends it). */
            std::string nameErr;
            if ((g_panelScreen == 1 || g_panelScreen == 2) && PanelNameCommit(&nameErr) == 0) { if (havePanel) PanelNameCheck(gui); return; }
            if (g_panelScreen == 1 && PanelHostingSame())   /* T-201 N1 fold (finding 1b): HOST on the world already hosted, same port */
            {
                DebugLog("[UI] T-201 N1 fold: HOST on '" + g_worldSel + "', which this game already hosts on port "
                         + coopui::PanelNum((long long)ConfigHostPort()) + " - HOSTING shown; nothing left or restarted");
                PanelSetStatus(std::string());
                PanelHostingOpen();
                if (havePanel) PanelPush(gui);
                return;
            }
            if (IdBoxRefuseHostJoin(g_panelScreen == 1 ? "HOST" : g_panelMode == 1 ? "JOIN" : "GO")) return;   /* PP3d */
            if (g_panelScreen == 1)   /* mp3: Host takes the picked world */
            {
                const LONG64 armedBefore = g_panelArmRequested;
                PanelHostPress();
                /* mp4 / T-201 N1: a Host that armed carries the Game options; HOSTING opens when the world answers (PressTick). The
                   world already hosted, same port, never gets here (above). */
                if (g_panelArmRequested != armedBefore) PanelOptHandOver();   /* mp5: the picked world's Game options go with the Host that armed; T-201 N1: HOSTING opens when the world answers (PressTick) */
                PanelNameSaveNote(nameErr);   /* T-201 N1b */
                if (havePanel) PanelPush(gui);
                return;
            }
            if (g_panelMode == 1) SettingsHostChoices(std::vector<std::pair<std::string, std::string> >());   /* mp5: a joiner's game hands no host choices on */
            PanelGo();
            PanelNameSaveNote(nameErr);   /* T-201 N1b */
            if (havePanel) PanelPush(gui);   /* T-201 N1: the greying and CANCEL */
            return;
        }
        case kActWorldPick:
        {
            /* ui2: the worlds list's selected row, read back here (the handler carries no index); a list older than g_worlds is ignored */
            const size_t k = havePanel ? UiListPicked(gui, NM().worldList) : MyGUI::ITEM_NONE;
            if (g_panelScreen == 1 && k < g_worlds.size() && g_worldListShown == g_worldListGen) g_worldSel = g_worlds[k].folder;
            if (havePanel) PanelPush(gui);
            if (havePanel) UiFocusPanel(gui);   /* ui2b: the clicked list took the key focus; Escape is heard on the window */
            return;
        }
        case kActNewWorld:
            if (g_panelScreen != 1) return;
            g_fNewWorld = "World 1";   /* words1 (owner 2026-09-27: "World 1"); owner decision 249/250 (2026-09-30): world names may hold single spaces */
            g_dlgText = "Enter a world name (up to 48 letters, numbers, spaces, - or _).";   /* owner decision 250, approved word for word */
            g_panelScreen = 3;
            if (havePanel) PanelPush(gui);
            return;
        case kActDeleteWorld:  PanelDeletePress();      if (havePanel) PanelPush(gui); return;
        case kActDlgOk:
            if (g_panelScreen == 3)      PanelCreateWorld();
            else if (g_panelScreen == 4) PanelDeleteConfirmed();
            else if (g_panelScreen == 7) PanelProfDlgOk();   /* prof3 */
            if (havePanel) PanelPush(gui);
            return;
        case kActDlgCancel:
            if (g_panelScreen == 3 || g_panelScreen == 4) g_panelScreen = 1;
            if (g_panelScreen == 7 && g_profDlg != 0) { DebugLog("[UI] prof3: Cancel pressed"); g_profDlg = 0; }   /* prof3 */
            if (havePanel) PanelPush(gui);
            return;
        default: return;
    }
}

/* U2-c (review-u2 H-2) - THE BACKDROP'S ONE REMOVAL PATH, and both callers use it.
   U2-b created CoopBackdrop BEFORE the panel and took it down only inside PanelDestroy, which needs a
   panel to have been built.  Every PanelBuildFailed after the backdrop existed therefore left an OPAQUE,
   MOUSE-ABSORBING widget over the menu column, and the next tick created another one under the same
   name - up to the fail-streak cap of them, stacked, with nothing that could ever remove them.  The
   take-down is now one function: a by-name lookup on this tick, never cached, destroyed BY POINTER
   (F725 R8).  Finding nothing is not an error - a build whose backdrop could not be created leaves
   nothing to take down. */
static void PanelDropBackdrop(MyGUI::Gui* gui)
{
    MyGUI::Widget* bd = gui->findWidgetT(NM().backdrop, false);
    if (bd != 0) gui->destroyWidget(bd);
}

static void PanelDestroy(MyGUI::Gui* gui, MyGUI::Widget* panel)
{
    /* A key focus left pointing at a widget we are about to free is the same dangling-pointer class the
       one rule exists to prevent, only inside MyGUI rather than inside us (design-ui-panel 5.2). */
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) im->resetKeyFocusWidget();
    ::InterlockedExchange(&g_panelBuiltOk, 0);
    g_panelStatusShown = -1;
    /* ONLY the panel, by pointer, and its children go with it.  Nothing enumerates and destroys by name
       prefix: CoopMultiplayerButton also begins with "Coop", and RE_Kenshi caches widget pointers of its
       own that a prefix sweep must never reach. */
    gui->destroyWidget(panel);
    /* U2-b: and the backdrop, which is a SIBLING and so is not carried away by the panel's own destroy. */
    PanelDropBackdrop(gui);
    /* U2-c (H-1): the panel that is going away takes its settle clock with it, so the one built next
       starts from its own rectangle and not from a mismatch measured against the old one. */
    g_panelMismatchSinceMs = 0;
    ::InterlockedIncrement64(&g_panelDestroyed);
}

/* One widget, created by TYPE STRING and converted with the non-throwing cast by the caller. */
static MyGUI::Widget* Mk(MyGUI::Widget* parent, const std::string& type, const std::string& skin,
                         int l, int t, int w, int h, const std::string& name)
{
    if (parent == 0) return 0;
    MyGUI::Widget* x = parent->createWidgetT(type, skin, MyGUI::IntCoord(l, t, w, h),
                                             MyGUI::Align::Default, name);
    if (x == 0) ::InterlockedIncrement64(&g_uiCreateNull);
    return x;
}

/* ui3 (ui3-title-panel-look item 1, Fix A) - AN OVERLAPPED CHILD: same parent, same coordinates, destroyed with the title
   art, but with its OWN layer node under the title art's.  MyGUI draws a layer node's bitmaps, then ALL its text, then its
   child nodes - so a plain child's bitmaps covered the title buttons while the buttons' CAPTIONS (text) were drawn over
   the panel.  A child node is drawn after the parent node's text, so nothing of the title column shows through.  Used
   for the top widgets only (backdrop, panel, error box, identity box, and ui6's click-blocking strips); their rows are plain Mk
   children and share the node. */
static MyGUI::Widget* MkOver(MyGUI::Widget* parent, const std::string& type, const std::string& skin,
                             int l, int t, int w, int h, const std::string& name)
{
    if (parent == 0) return 0;
    MyGUI::Widget* x = parent->createWidgetT(MyGUI::WidgetStyle(MyGUI::WidgetStyle::Overlapped), type, skin,
                                             MyGUI::IntCoord(l, t, w, h), MyGUI::Align::Default, std::string(), name);
    if (x == 0) ::InterlockedIncrement64(&g_uiCreateNull);
    return x;
}

static void PanelBuildFailed(MyGUI::Gui* gui, MyGUI::Widget* panel, const char* why)
{
    ::InterlockedIncrement64(&g_panelBuildFail);
    if (panel != 0) { ::InterlockedExchange(&g_panelBuiltOk, 0); gui->destroyWidget(panel); }
    /* U2-c (review-u2 H-2) - AND THE BACKDROP, on this path too.  It is created before the panel, so a
       build that fails ANYWHERE after that point has one on screen; leaving it there left an opaque
       widget over the menu column that no code path could remove.  Called unconditionally: it is a
       by-name lookup and finding nothing is the ordinary case for a failure that happened earlier. */
    PanelDropBackdrop(gui);
    /* P8i-b: the streak advance is the pure decision the offline suite sweeps, so the shipped rule and
       the tested rule cannot drift apart by hand (6a lesson 11). */
    int giveUp = 0;
    {
        const coopui::PanelRefusal r = coopui::PanelNoteRefusal(g_panelFailStreak, kPanelFailStreakCap);
        g_panelFailStreak = r.streak;
        giveUp = r.giveUp;
    }
    /* P8i-c - THE GIVE-UP IS THE FLAG PanelNoteRefusal ALREADY RETURNED, not the same decision taken a
       second time from the streak.  The old code computed `r.giveUp` and discarded it, then re-derived
       `streak >= cap` on the next line: two expressions of one rule, only one of which the offline suite
       sweeps, which is the shape 6a lesson 11 names. */
    if (giveUp != 0)
    {
        /* 6a lesson 14: a corrective that cannot work must stop, not retry at 1 kHz. */
        ::InterlockedExchange(&g_panelWanted, 0);
        ::InterlockedIncrement64(&g_panelGaveUp);
        /* review-p8i M-2 - AND IT HAS TO BE SOMEWHERE THE PLAYER CAN SEE.  Writing this into the panel's
           own status area was writing it into a widget that is about to be destroyed and will not be
           rebuilt, so the one message explaining why nothing opened had nowhere to appear.  It goes on
           the TITLE SCREEN, in CoopNoticeText, which the tick builds beside the MULTIPLAYER button. */
        const std::string msg = coopui::PanelGiveUpMessage(kPanelFailStreakCap);
        PanelSetStatus(msg);
        UiRaiseNotice(msg);
        /* The cap is PRINTED, not spelled out in the sentence: the message used to say "5" beside a
           constant an edit could move (review-p8i-b LOW). */
        ErrorLog("[UI] the MULTIPLAYER panel gave up after "
                 + coopui::PanelNum((long long)kPanelFailStreakCap)
                 + " build attempts in a row - " + (why ? why : "?")
                 + ". The panel is closed and a line saying so is on the title screen; clicking"
                   " MULTIPLAYER again tries again.");
    }
}

static void PanelBuild(MyGUI::Gui* gui, MyGUI::Widget* parent)
{
    const UiNames& n = NM();
    const int parentW = parent->getWidth();
    const int parentH = parent->getHeight();
    if (parentW <= 0 || parentH <= 0) { ::InterlockedIncrement64(&g_panelNoRoom); return; }

    /* design-ui-panel 1.3's rectangle, computed in INTEGERS from the LIVE parent, never from a layout
       file and never through createWidgetReal.  A different resolution, a language pack or another UI mod
       moving the column changes nothing.
       U2-b: the arithmetic moved into coopui::PanelRectIn so the offline suite sweeps it.  It is still
       the middle 40% x 70% whenever that is big enough to read, and it is now CENTRED by construction
       rather than by a left of 30% that only happens to be the middle while the width is exactly 40%. */
    const coopui::PanelRect pr = coopui::PanelRectIn(parentW, parentH);
    const int pxLeft = pr.left;
    const int pyTop  = pr.top;
    const int pW     = pr.w;
    const int pH     = pr.h;
    if (coopui::PanelRectWasGrown(parentW, parentH)) ::InterlockedIncrement64(&g_panelGrown);
    /* P8i-c / F751 - ONE GUARD, TAKEN BEFORE ANYTHING IS CREATED, AND IT IS THE ONLY ONE ON THIS INPUT.
       There used to be two here.  The first was `if (pW < 260 || pH < 240) { ++panelNoRoomTicks; return; }`
       - a bare early return with no streak, no cap and no message, leaving g_panelWanted at 1 - and it
       sat ABOVE the capped guard P8i-b had added, so on a screen too small the panel was refused for
       ever in silence, the capped guard was unreachable, and UiCloseFromEngine swallowed an ESC on a
       panel that had never been drawn.  Both minimums now live in coopui::PanelWindowAllowed, which is
       one predicate the offline suite sweeps, so there is no longer an ordering to get wrong. */
    if (!coopui::PanelWindowAllowed(pW, pH))
    {
        ::InterlockedIncrement64(&g_panelTooSmall);
        /* U2-b made this sentence stale and it is corrected here rather than left to mislead: the
           rectangle is no longer always the middle 40% x 70% - PanelRectIn grows it towards a readable
           row height when the parent is small - so a refusal that named that box would be describing
           arithmetic the code had stopped doing. It now says what was actually refused. */
        PanelBuildFailed(gui, 0, "the largest panel this screen allows is still smaller than the panel's"
                                 " minimum, or too small for thirteen rows of text");
        return;
    }

    /* U2-b - THE OPAQUE BACKDROP, AND IT IS CREATED FIRST BECAUSE THAT IS WHAT PUTS IT BEHIND.
       MyGUI draws siblings in the order of the parent's child list, which is the same ordering fact P7z
       used to put our button on top of CreditsPanel (F657).  Created here, it lands after every widget
       the title layout made - so it covers the menu column - and before CoopPanel, so the panel and all
       its rows draw on top of it.  It takes no input and has no handler: an affordance that does nothing
       when pressed would be a defect, and this is not an affordance.
       A backdrop that cannot be created is NOT a build failure: the panel is still usable, only
       see-through, and refusing to open it would be a worse answer than opening it. */
    /* U2-c (review-u2 H-2): and it is NEVER CREATED TWICE.  createWidgetT does not object to a second
       widget with the same name, so a leftover from an earlier failed build would have been joined by a
       new one rather than replaced.  A backdrop already on screen under our name is the right size only
       if the rectangle has not changed - and if it has, the take-down above has already removed it - so
       reusing it is correct and is counted. */
    MyGUI::Widget* bd = gui->findWidgetT(n.backdrop, false);
    if (bd != 0)
    {
        ::InterlockedIncrement64(&g_panelBackdropReused);
    }
    else
    {
        bd = MkOver(parent, n.typeWidget, n.skinSolid, pxLeft, pyTop, pW, pH, n.backdrop);   /* ui3: own layer node */
        if (bd != 0) ::InterlockedIncrement64(&g_panelBackdrop);
        else         ::InterlockedIncrement64(&g_panelBackdropFailed);
    }
    /* ui3b (review HIGH, MyGUI 3.2.3 Confirmed in the DLL): Overlapped siblings are asked for clicks in creation order but drawn in
       reverse, so the backdrop - made first, same rectangle - took every click meant for the panel. It never takes the mouse. */
    if (bd != 0) bd->setNeedMouseFocus(false);

    MyGUI::Widget* raw = MkOver(parent, n.typeWindow, n.skinWindow, pxLeft, pyTop, pW, pH, n.panel);   /* ui3: own layer node */
    if (raw == 0) { PanelBuildFailed(gui, 0, "createWidgetT returned null for the panel window"); return; }
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    if (win == 0)
    {
        ::InterlockedIncrement64(&g_panelCastNull);
        PanelBuildFailed(gui, raw, "Kenshi_WindowCX did not produce a MyGUI::Window");
        return;
    }
    win->setCaption(MyGUI::UString(kCaption));
    win->setMovable(false);
    /* P8i-b / H-2 and H-1.  Both are EventPair / CMultiDelegate operator+=, which are header-inline, so
       neither adds an import to check.  Subscribed here, on the freshly created Window, so the same
       delegate can never be added twice to one widget. */
    win->eventWindowButtonPressed += MyGUI::newDelegate(OnCoopWindowButton);
    win->eventKeyButtonPressed    += MyGUI::newDelegate(OnCoopPanelKey);

    MyGUI::Widget* cl = win->getClientWidget();
    if (cl == 0)
    {
        ::InterlockedIncrement64(&g_panelNoClient);
        PanelBuildFailed(gui, raw, "the Kenshi_WindowCX skin gave no Client widget");
        return;
    }
    /* ui3 (ui3-title-panel-look item 2) - ONE INNER MARGIN FOR EVERY ROW.  The client is only 3 px inside the window's frame
       art and Kenshi_WordWrapEmpty has no text padding, so text built at x=0 put its first letter on the frame.  Every row
       is now built inside CoopPanelInner, about 3% of the client in from each side (Kenshi's message box insets 2.9%);
       coopui::PanelInnerPadOf never lets the margin take the client below the thirteen-row minimum, so the fit test
       below - now run on the INNER size - agrees with the pre-create prediction. */
    const coopui::PanelInnerPad ip = coopui::PanelInnerPadOf(cl->getWidth(), cl->getHeight());
    MyGUI::Widget* c = Mk(cl, n.typeWidget, n.skinPanel, ip.x, ip.y, cl->getWidth() - 2 * ip.x, cl->getHeight() - 2 * ip.y,
                          std::string("CoopPanelInner"));
    if (c == 0) { PanelBuildFailed(gui, raw, "the panel's inner area could not be created"); return; }

    const int W = c->getWidth();
    const int H = c->getHeight();
    const int rows = coopui::kPanelRows;
    const int rowH = H / rows;
    /* P8i-b: the same pure test again, now on the REAL client rectangle.  The pre-create test above
       predicts this one from the template; if a skin ever makes the prediction wrong, this is what
       catches it - and unlike the code it replaces it goes through PanelBuildFailed, so it books the
       failure, advances the streak and gives up after the cap instead of rebuilding for ever. */
    if (!coopui::PanelClientFits(W, H))
    {
        ::InterlockedIncrement64(&g_panelTooSmall);
        PanelBuildFailed(gui, raw, "the created window's client area is smaller than its template implies");
        return;
    }
    const int gap  = coopui::PanelGapOf(rowH);
    /* ui5 (panel-mockups.md, owner-approved 2026-09-27) - EVERY SCREEN FROM ITS OWN ROW LIST, NO TITLE ROW.
       The screen's name is the window's caption (PanelPush); each screen's rows are coopui::PanelRowsOf's, in half-rows of
       this row height, and the window is only as tall as the screen showing (PanelLayout).  Every group covers the largest
       inner area and is shown on its screen only - the inner widget clips it to that screen's height.  A label is exactly
       as tall as its box and its text is centred in it (VCenter below), so the two are level whatever the font.  BACK /
       CANCEL sit bottom-left, the screen's action bottom-right.  The widgets several screens share (status, BACK, the
       action button, the dialog's rows) are placed by PanelLayout. */
    g_layGeom.maxRect = pr;
    g_layGeom.padX    = ip.x;
    g_layGeom.padY    = ip.y;
    g_layGeom.innerW  = W;
    g_layGeom.rowH    = rowH;
    g_layParentH      = parentH;
    g_layChromeH      = pH - cl->getHeight();   /* the REAL frame, measured on the window just made at the largest size */
    g_layShown        = -1;
    const int btnW   = coopui::PanelBottomBtnW(W);
    const int sideW  = coopui::PanelSideBtnW(W);
    const int thirdW = (W - 2 * gap) / 3;

    MyGUI::Widget* hg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.hostGroup);
    MyGUI::Widget* jg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.joinGroup);
    if (hg == 0 || jg == 0) { PanelBuildFailed(gui, raw, "a mode group could not be created"); return; }

    /* [2] MULTIPLAYER: HOST GAME, JOIN GAME, BACK (T-201 N1b, owner 166: PLAYER NAME moved to HOST GAME and JOIN GAME). */
    MyGUI::Widget* lg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.landGroup);
    if (lg == 0) { PanelBuildFailed(gui, raw, "the Multiplayer screen's group could not be created"); return; }
    {
        const coopui::PanelBand hb   = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotHostBtn, rowH);
        const coopui::PanelBand jb   = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotJoinBtn, rowH);
        const coopui::PanelBand bb   = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotButtons, rowH);
        Mk(lg, n.typeButton, n.skinBtn1,    W / 5,     hb.top,         W - 2 * (W / 5), hb.h,             n.modeHost);
        Mk(lg, n.typeButton, n.skinBtn1,    W / 5,     jb.top,         W - 2 * (W / 5), jb.h,             n.modeJoin);
        Mk(lg, n.typeButton, n.skinBtn1,    0,         bb.top,         btnW,             bb.h,             n.landBackBtn);
    }

    /* [3] HOST GAME: the worlds list (its WORLD column head names it - the WORLDS label is gone), NEW WORLD / GAME OPTIONS /
       DELETE WORLD, PORT level with its box.  NO PASSWORD ROW: no password exists anywhere yet (MP8 builds it). */
    {
        const coopui::PanelBand lb = coopui::PanelSlotBand(coopui::kLayHost, coopui::kSlotList, rowH);
        const coopui::PanelBand kb = coopui::PanelSlotBand(coopui::kLayHost, coopui::kSlotListBtns, rowH);
        const coopui::PanelPairRect pp = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHost, coopui::kSlotPortRow, rowH),
                                                                 coopui::PanelPortBoxW(W), 0);
        /* ui2: Kenshi's Load Game list - Kenshi_LoadGamePanel.layout's GamesList (Kenshi_MultiListBox, heads
           Kenshi_MultiListButton, rows Kenshi_ListBoxItem 21 px, Kenshi_ScrollBarV, mouse wheel). */
        Mk(hg, n.typeMulti,  n.skinMulti,   0,                  lb.top,      W,         lb.h, n.worldList);
        /* ...and the empty-list line over the list's first rows (created after it), shown only with no worlds. */
        Mk(hg, n.typeEdit,   n.skinWrap,    12,                 lb.top + 40, W - 44,    rowH, n.worldEmpty);
        Mk(hg, n.typeButton, n.skinBtn1,    0,                  kb.top,      thirdW,    kb.h, n.newWorldBtn);
        Mk(hg, n.typeButton, n.skinBtn1,    thirdW + gap,       kb.top,      thirdW,    kb.h, n.optionsBtn);
        Mk(hg, n.typeButton, n.skinBtn1,    2 * (thirdW + gap), kb.top,      thirdW,    kb.h, n.deleteBtn);
        Mk(hg, n.typeText,   n.skinCaption, pp.labelX,          pp.top,      pp.labelW, pp.h, n.portLabel);
        Mk(hg, n.typeEdit,   n.skinEdit,    pp.boxX,            pp.top,      pp.boxW,   pp.h, n.portEdit);
        /* T-201 PP6' (owner 141/143): PROFILE   <name>   [ CHANGE ] under PORT.  T568: PanelLayout placed and PanelPush filled
           these three but nothing ever made them, so the row never showed. */
        const coopui::PanelPairRect pr = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHost, coopui::kSlotProfRow, rowH),
                                                                 0, sideW + gap);
        Mk(hg, n.typeText,   n.skinCaption, pr.labelX,          pr.top,      pr.labelW, pr.h, n.hostProfLabel);
        Mk(hg, n.typeText,   std::string("Kenshi_TextboxStandardText"), pr.boxX, pr.top, pr.boxW, pr.h, n.hostProfValue);
        Mk(hg, n.typeButton, n.skinBtn1,    W - sideW,          pr.top,      sideW,     pr.h, n.hostProfChangeBtn);
    }

    /* [8] HOSTING: LOCAL ADDRESS (a label level with its value, no colon) and COPY, the router help, the PLAYERS list;
       CHOOSE PROFILE bottom-right (its main action).  The status area and BACK are the shared ones. */
    {
        MyGUI::Widget* sg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.hostingGroup);
        if (sg == 0) { PanelBuildFailed(gui, raw, "the Hosting screen's group could not be created"); return; }
        const coopui::PanelPairRect ar = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, rowH),
                                                                 0, coopui::PanelAddrBtnW(W) + gap);
        const coopui::PanelBand hb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotRouterHelp, rowH);
        const coopui::PanelBand pb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotPlayers, rowH);
        const coopui::PanelBand bb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotButtons, rowH);
        const int addrBtnW = coopui::PanelAddrBtnW(W);   /* T-510: one width for the address rows' buttons */
        const coopui::PanelPairRect nr2 = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, rowH),
                                                                  0, 2 * (addrBtnW + gap));
        Mk(sg, n.typeText,   std::string("Kenshi_TextboxStandardText"), ar.labelX, ar.top, ar.labelW, ar.h, n.homeAddrLabel);   /* ui5b: fits at 1280x720 */
        Mk(sg, n.typeText,   std::string("Kenshi_TextboxStandardText"), ar.boxX, ar.top, ar.boxW, ar.h, n.homeAddr);   /* ui3b: the address must fit */
        Mk(sg, n.typeButton, n.skinBtn1,    W - addrBtnW, ar.top, addrBtnW, ar.h, n.copyBtn);
        Mk(sg, n.typeText,   std::string("Kenshi_TextboxStandardText"), nr2.labelX, nr2.top, nr2.labelW, nr2.h, n.netAddrLabel);   /* T-510 */
        Mk(sg, n.typeText,   std::string("Kenshi_TextboxStandardText"), nr2.boxX, nr2.top, nr2.boxW, nr2.h, n.netAddr);
        Mk(sg, n.typeButton, n.skinBtn1,    W - 2 * addrBtnW - gap, nr2.top, addrBtnW, nr2.h, n.netShowBtn);
        Mk(sg, n.typeButton, n.skinBtn1,    W - addrBtnW, nr2.top, addrBtnW, nr2.h, n.netCopyBtn);
        Mk(sg, n.typeEdit,   n.skinWrap,    0,         hb.top, W,         hb.h, n.routerHelp);
        Mk(sg, n.typeEdit,   n.skinWrap,    0,         pb.top, W,         pb.h, n.playersBox);   /* ui5d: both re-placed at their texts' heights by PanelLayout */
        Mk(c,  n.typeButton, n.skinBtn1,    W - btnW,  bb.top, btnW,      bb.h, n.chooseBtn);
    }

    /* [6] GAME OPTIONS: the three tabs (plain buttons, never MyGUI's tab control), nine option slots (label, <, value, > - or
       a tick box in the stepper's column), the nests note, DEFAULTS left and DONE right.  The window is as tall as the
       longest tab, so it does not jump when tabs change.  The MULTIPLAYER tab's mods line sits after its four rows and a
       blank one.  STEP BUTTONS, NOT A SLIDER: no new MyGUI import. */
    {
        MyGUI::Widget* og = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.optGroup);
        if (og == 0) { PanelBuildFailed(gui, raw, "the Game options screen's group could not be created"); return; }
        const coopui::PanelBand tb = coopui::PanelSlotBand(coopui::kLayOptions, coopui::kSlotTabs, rowH);
        const coopui::PanelBand rb = coopui::PanelSlotBand(coopui::kLayOptions, coopui::kSlotOptRows, rowH);
        const coopui::PanelBand nb = coopui::PanelSlotBand(coopui::kLayOptions, coopui::kSlotNote, rowH);
        const coopui::PanelBand bb = coopui::PanelSlotBand(coopui::kLayOptions, coopui::kSlotButtons, rowH);
        for (int t = 0; t < coopui::kOptTabCount; ++t)
            Mk(og, n.typeButton, n.skinBtn1, t * (thirdW + gap), tb.top, thirdW, tb.h, n.optTab[t]);
        const int stepW = (W * 8) / 100;
        const int decX  = (W * 51) / 100;
        const int incX  = W - stepW;
        const int valX  = decX + stepW + gap;
        const int valW  = incX - gap - valX;
        for (int r = 0; r < coopui::kOptRowsShown; ++r)
        {
            const int y  = rb.top + (rb.h * r) / coopui::kOptRowsShown;
            const int rh = rb.top + (rb.h * (r + 1)) / coopui::kOptRowsShown - y;
            const int tickW = rh < stepW ? rh : stepW;
            Mk(og, n.typeText,   std::string("Kenshi_TextboxStandardText"), 0, y, (W * 50) / 100, rh, n.optLabel[r]);   /* ui3b: the painted font overflowed 50% at 1280x720 */
            Mk(og, n.typeButton, n.skinBtn1, decX, y,                    stepW, rh,    n.optDec[r]);
            Mk(og, n.typeText,   n.skinFlat, valX, y,                    valW,  rh,    n.optVal[r]);
            Mk(og, n.typeButton, n.skinBtn1, incX, y,                    stepW, rh,    n.optInc[r]);
            Mk(og, n.typeButton, n.skinTick, decX, y + (rh - tickW) / 2, tickW, tickW, n.optTick[r]);   /* ui5: in the stepper's column */
        }
        const int coopY = rb.top + (rb.h * 5) / coopui::kOptRowsShown;
        Mk(og, n.typeEdit,   n.skinWrap, 0,        nb.top, W,    nb.h,                                   n.optNote);
        Mk(og, n.typeEdit,   n.skinWrap, 0,        coopY,  W,    (rb.h * 2) / coopui::kOptRowsShown,     n.optCoopNote);
        Mk(og, n.typeButton, n.skinBtn1, 0,        bb.top, btnW, bb.h,                                   n.optDefaultsBtn);
        Mk(og, n.typeButton, n.skinBtn1, W - btnW, bb.top, btnW, bb.h,                                   n.optDoneBtn);
    }

    /* [9] PROFILES: the list (NAME / FACTION / LAST PLAYED - its own highlight marks the pick), NEW PROFILE and DELETE
       PROFILE under it, PLAY bottom-right.  The status area and BACK are the shared ones; NEW PROFILE's name and DELETE
       PROFILE? are the dialog group's. */
    {
        MyGUI::Widget* fg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.profGroup);
        if (fg == 0) { PanelBuildFailed(gui, raw, "the Your profiles screen's group could not be created"); return; }
        const coopui::PanelBand lb = coopui::PanelSlotBand(coopui::kLayProfiles, coopui::kSlotList, rowH);
        const coopui::PanelBand kb = coopui::PanelSlotBand(coopui::kLayProfiles, coopui::kSlotListBtns, rowH);
        const coopui::PanelBand bb = coopui::PanelSlotBand(coopui::kLayProfiles, coopui::kSlotButtons, rowH);
        Mk(fg, n.typeMulti,  n.skinMulti, 0,            lb.top, W,      lb.h, n.profList);
        Mk(fg, n.typeButton, n.skinBtn1,  0,            kb.top, thirdW, kb.h, n.profNew);
        Mk(fg, n.typeButton, n.skinBtn1,  thirdW + gap, kb.top, thirdW, kb.h, n.profDelete);
        Mk(fg, n.typeButton, n.skinBtn1,  W - btnW,     bb.top, btnW,   bb.h, n.profPlay);
    }

    /* [7] JOIN GAME: IP ADDRESS level with its box, PASTE on the right. */
    {
        const coopui::PanelPairRect jr = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayJoin, coopui::kSlotAddrRow, rowH), 0, sideW + gap);
        Mk(jg, n.typeText,   n.skinCaption, jr.labelX, jr.top, jr.labelW, jr.h, n.joinLabel);
        Mk(jg, n.typeEdit,   n.skinEdit,    jr.boxX,   jr.top, jr.boxW,   jr.h, n.joinEdit);
        Mk(jg, n.typeButton, n.skinBtn1,    W - sideW, jr.top, sideW,     jr.h, n.pasteBtn);
    }

    /* T-201 N1b (owner 166): PLAYER NAME - ONE box, its label and its hint - the top row of HOST GAME and JOIN GAME.  Made on the
       inner area after both groups (so over them), shown on those two screens only (PanelPush) and placed by PanelLayout. */
    {
        const coopui::PanelPairRect nm = coopui::PanelLabelBoxIn(W, gap, coopui::PanelSlotBand(coopui::kLayHost, coopui::kSlotNameRow, rowH), 0, 0);
        const coopui::PanelBand hint = coopui::PanelSlotBand(coopui::kLayHostHint, coopui::kSlotNameHint, rowH);
        Mk(c, n.typeText, n.skinCaption, nm.labelX, nm.top,   nm.labelW, nm.h,   n.nameLabel);
        Mk(c, n.typeEdit, n.skinEdit,    nm.boxX,   nm.top,   nm.boxW,   nm.h,   n.nameEdit);
        Mk(c, n.typeEdit, n.skinWrap,    nm.boxX,   hint.top, nm.boxW,   hint.h, n.nameHint);
    }

    /* [4][5] and PROFILES' two - THE DIALOGS (NEW WORLD, DELETE WORLD?, NEW PROFILE, DELETE PROFILE?): one group, shown on
       those screens only; the text, the name row (the NEW ones), CANCEL left and the action right - placed by PanelLayout. */
    MyGUI::Widget* dg = Mk(c, n.typeWidget, n.skinPanel, 0, 0, W, H, n.dlgGroup);
    if (dg == 0) { PanelBuildFailed(gui, raw, "the dialog group could not be created"); return; }
    Mk(dg, n.typeEdit,   n.skinWrap,    0, 0, W,    rowH, n.dlgText);
    Mk(dg, n.typeText,   n.skinCaption, 0, 0, W,    rowH, n.dlgNameLabel);
    Mk(dg, n.typeEdit,   n.skinEdit,    0, 0, W,    rowH, n.dlgNameEdit);
    Mk(dg, n.typeButton, n.skinBtn1,    0, 0, btnW, rowH, n.dlgCancel);
    Mk(dg, n.typeButton, n.skinBtn1,    0, 0, btnW, rowH, n.dlgOk);

    /* The status area, in ordinary sentences - an EditBox on the game's own Kenshi_WordWrapEmpty skin, which is exactly what
       Kenshi_MessageBox.layout does for its own text - and the shared bottom row: BACK and the action button (its caption
       set by PanelPush from the mode).  Placed by PanelLayout. */
    Mk(c, n.typeEdit,   n.skinWrap, 0, 0, W,    rowH, n.status);
    Mk(c, n.typeButton, n.skinBtn1, 0, 0, btnW, rowH, n.goBtn);
    Mk(c, n.typeButton, n.skinBtn1, 0, 0, btnW, rowH, n.closeBtn);

    /* Now the properties and the delegates.  Every one of these lookups is by name and non-throwing. */
    MyGUI::EditBox* e;
    e = UiEdit(gui, n.portEdit);   if (e) { e->setEditMultiLine(false); e->setMaxTextLength(coopcfg::kCfgPortFieldMax); }
    e = UiEdit(gui, n.joinEdit);   if (e) { e->setEditMultiLine(false); e->setMaxTextLength(coopcfg::kCfgAddrFieldMax); }
    e = UiEdit(gui, n.dlgNameEdit); if (e) { e->setEditMultiLine(false); e->setMaxTextLength(coopcfg::kCfgWorldFieldMax); }   /* mp3: 48, the World box's limit */
    e = UiEdit(gui, n.dlgText);    if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }
    e = UiEdit(gui, n.nameEdit);   if (e) { e->setEditMultiLine(false); e->setMaxTextLength(coopcfg::kCfgPlayerNameFieldMax); }
    e = UiEdit(gui, n.nameHint);   if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }
    e = UiEdit(gui, n.routerHelp); if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }   /* mp4 */
    e = UiEdit(gui, n.playersBox); if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }   /* mp4 */
    e = UiEdit(gui, n.optNote);     if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }   /* mp5 */
    e = UiEdit(gui, n.optCoopNote); if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true); }   /* mp5 */
    e = UiEdit(gui, n.status);
    if (e == 0) { PanelBuildFailed(gui, raw, "the status area could not be found after it was created"); return; }
    e->setEditStatic(true);
    e->setEditReadOnly(true);
    e->setEditMultiLine(true);
    e->setEditWordWrap(true);
    /* ui3 (ui3-title-panel-look item 7 / fix 4): body text in Kenshi's message-box colour (Kenshi_MessageBox.layout TextColour
       0.871 0.845 0.810), not the skin's near-black, which was unreadable on the dark panel. */
    {
        const std::string* const bodies[] = { &n.status, &n.nameHint, &n.routerHelp, &n.playersBox, &n.optNote, &n.optCoopNote,
                                              &n.dlgText, &n.worldEmpty };
        for (size_t bi = 0; bi < sizeof(bodies) / sizeof(bodies[0]); ++bi)
        { MyGUI::EditBox* be = UiEdit(gui, *bodies[bi]); if (be) be->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f)); }
    }

    MyGUI::TextBox* t;
    MyGUI::Widget* w;
    w = UiFind(gui, n.portLabel);   if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("PORT")); else ::InterlockedIncrement64(&g_panelCastNull); }
    w = UiFind(gui, n.hostProfLabel); if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("PROFILE")); else ::InterlockedIncrement64(&g_panelCastNull); }   /* T-201 PP6' */
    w = UiFind(gui, n.homeAddrLabel); if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("LOCAL ADDRESS")); else ::InterlockedIncrement64(&g_panelCastNull); }   /* ui5 */
    w = UiFind(gui, n.netAddrLabel); if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("INTERNET ADDRESS")); else ::InterlockedIncrement64(&g_panelCastNull); }   /* T-510 */
    w = UiFind(gui, n.dlgNameLabel); if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("WORLD NAME")); else ::InterlockedIncrement64(&g_panelCastNull); }
    w = UiFind(gui, n.joinLabel);   if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("IP ADDRESS")); else ::InterlockedIncrement64(&g_panelCastNull); }
    w = UiFind(gui, n.nameLabel);   if (w) { t = w->castType<MyGUI::TextBox>(false); if (t) t->setCaption(MyGUI::UString("PLAYER NAME")); else ::InterlockedIncrement64(&g_panelCastNull); }
    /* ui5 - EVERY LABEL LEVEL WITH ITS BOX: the label is as tall as the box (coopui::PanelLabelBoxIn) and its text is centred
       in it here - Kenshi_TextboxPaintedText / StandardText carry no vertical align of their own.  TextBox::setTextAlign is
       the call ui3's title row already made (no new import). */
    {
        const std::string* const labs[] = { &n.nameLabel, &n.portLabel, &n.joinLabel, &n.dlgNameLabel, &n.homeAddrLabel, &n.homeAddr,
                                            &n.hostProfLabel, &n.hostProfValue, &n.netAddrLabel, &n.netAddr };   /* T-201 PP6'; T-510 */
        for (size_t li = 0; li < sizeof(labs) / sizeof(labs[0]); ++li)
        { w = UiFind(gui, *labs[li]); t = w ? w->castType<MyGUI::TextBox>(false) : 0; if (t) t->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter); }
        for (int li = 0; li < coopui::kOptRowsShown; ++li)
        { w = UiFind(gui, n.optLabel[li]); t = w ? w->castType<MyGUI::TextBox>(false) : 0; if (t) t->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter); }
    }

    MyGUI::Button* b;
    b = UiBtn(gui, n.modeHost);   if (b == 0) { PanelBuildFailed(gui, raw, "the HOST button vanished after it was created"); return; }
    b->setCaption(MyGUI::UString("HOST GAME"));  b->setFontName(n.fontLarge);
    b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopModeHostClicked);
    b = UiBtn(gui, n.modeJoin);   if (b == 0) { PanelBuildFailed(gui, raw, "the JOIN button vanished after it was created"); return; }
    b->setCaption(MyGUI::UString("JOIN GAME"));  b->setFontName(n.fontLarge);
    b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopModeJoinClicked);
    /* mp3: the Host a game screen's buttons and the dialogs'.  mp5: Game options opens its screen for the picked world. */
    {
        /* ui2: the worlds list's two columns and its pick.  Widths split the list's inner width (less the skin's frame and the
           scroll bar, kenshi_templates.xml Kenshi_MultiListBox / Kenshi_MultiSubListBox) 60 / 40. */
        const int inner = W - 32;
        MyGUI::MultiListBox* ml = UiMulti(gui, n.worldList);
        if (ml) { ml->addColumn(MyGUI::UString("WORLD"), (inner * 60) / 100); ml->addColumn(MyGUI::UString("LAST PLAYED"), inner - (inner * 60) / 100);
                  ml->eventListChangePosition += MyGUI::newDelegate(OnCoopWorldListPicked);
                  ml->requestOperatorLess = MyGUI::newDelegate(OnCoopWorldListLess); }   /* ui2b: the heads sort by time / case ignored */
        e = UiEdit(gui, n.worldEmpty);
        if (e) { e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true);
                 e->setCaption(MyGUI::UString("No worlds yet. Press NEW WORLD to make one.")); e->setVisible(false); }
    }
    b = UiBtn(gui, n.newWorldBtn); if (b) { b->setCaption(MyGUI::UString("NEW WORLD")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopNewWorldClicked); }
    b = UiBtn(gui, n.optionsBtn);  if (b) { b->setCaption(MyGUI::UString("GAME OPTIONS")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopGameOptionsClicked); }
    {
        /* mp5: the Game options screen's buttons. */
        static const char* const kTabWords[coopui::kOptTabCount] = { "DIFFICULTY", "WORLD", "MULTIPLAYER" };   /* ui1 */
        for (int t = 0; t < coopui::kOptTabCount; ++t)
        { b = UiBtn(gui, n.optTab[t]); if (b) { b->setCaption(MyGUI::UString(kTabWords[t])); b->eventMouseButtonClick += MyGUI::newDelegate(kOptTabFns[t]); } }
        for (int r = 0; r < coopui::kOptRowsShown; ++r)
        {
            b = UiBtn(gui, n.optDec[r]);  if (b) { b->setCaption(MyGUI::UString("<")); b->eventMouseButtonClick += MyGUI::newDelegate(kOptDecFns[r]); }
            b = UiBtn(gui, n.optInc[r]);  if (b) { b->setCaption(MyGUI::UString(">")); b->eventMouseButtonClick += MyGUI::newDelegate(kOptIncFns[r]); }
            b = UiBtn(gui, n.optTick[r]); if (b) b->eventMouseButtonClick += MyGUI::newDelegate(kOptTickFns[r]);
        }
        b = UiBtn(gui, n.optDefaultsBtn); if (b) { b->setCaption(MyGUI::UString("DEFAULTS")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopOptDefaults); }
        b = UiBtn(gui, n.optDoneBtn);     if (b) { b->setCaption(MyGUI::UString("DONE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopOptDone); }
    }
    b = UiBtn(gui, n.deleteBtn);   if (b) { b->setCaption(MyGUI::UString("DELETE WORLD")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopDeleteWorldClicked); }
    b = UiBtn(gui, n.dlgCancel);   if (b) { b->setCaption(MyGUI::UString("CANCEL")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopDlgCancelClicked); }
    b = UiBtn(gui, n.dlgOk);       if (b) { b->setCaption(MyGUI::UString("CREATE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopDlgOkClicked); }
    b = UiBtn(gui, n.pasteBtn);   if (b) { b->setCaption(MyGUI::UString("PASTE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopPasteClicked); }
    /* mp4: Copy, and Choose my profile drawn greyed with no handler (the profiles step builds it). */
    b = UiBtn(gui, n.copyBtn);    if (b) { b->setCaption(MyGUI::UString("COPY")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopCopyClicked); }
    b = UiBtn(gui, n.netShowBtn); if (b) { b->setCaption(MyGUI::UString("SHOW")); b->eventMouseButtonClick += MyGUI::newDelegate(OnNetAddrShowClicked); }   /* T-510 */
    b = UiBtn(gui, n.netCopyBtn); if (b) { b->setCaption(MyGUI::UString("COPY")); b->eventMouseButtonClick += MyGUI::newDelegate(OnNetAddrCopyClicked); }
    b = UiBtn(gui, n.chooseBtn);  if (b) { b->setCaption(MyGUI::UString("CHOOSE PROFILE")); b->setEnabled(false); b->setVisible(false); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopChooseProfileClicked); }   /* prof3: wired */
    /* prof3: the Your profiles screen's buttons and column heads. */
    b = UiBtn(gui, n.profPlay);   if (b) { b->setCaption(MyGUI::UString("PLAY")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopProfPlayClicked); }
    b = UiBtn(gui, n.hostProfChangeBtn); if (b) { b->setCaption(MyGUI::UString("CHANGE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopHostProfChangeClicked); }   /* T-201 PP6' */
    b = UiBtn(gui, n.profNew);    if (b) { b->setCaption(MyGUI::UString("NEW PROFILE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopProfNewClicked); }
    b = UiBtn(gui, n.profDelete); if (b) { b->setCaption(MyGUI::UString("DELETE PROFILE")); b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopProfDeleteClicked); }
    {
        /* ui2: the profiles list's three columns (34 / 36 / 30 of the inner width) and its pick. */
        const int inner = W - 32;
        const int c0 = (inner * 34) / 100, c1 = (inner * 36) / 100;
        MyGUI::MultiListBox* ml = UiMulti(gui, n.profList);
        if (ml) { ml->addColumn(MyGUI::UString("NAME"), c0); ml->addColumn(MyGUI::UString("FACTION"), c1);
                  ml->addColumn(MyGUI::UString("LAST PLAYED"), inner - c0 - c1);
                  ml->eventListChangePosition += MyGUI::newDelegate(OnCoopProfListPicked);
                  ml->requestOperatorLess = MyGUI::newDelegate(OnCoopProfListLess); }    /* ui2b: as the worlds list's */
    }
    b = UiBtn(gui, n.goBtn);      if (b == 0) { PanelBuildFailed(gui, raw, "the action button vanished after it was created"); return; }
    b->setCaption(MyGUI::UString(g_panelMode == 0 ? "HOST" : "JOIN")); b->setFontName(n.fontLarge);
    b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopGoClicked);
    b = UiBtn(gui, n.closeBtn);   if (b == 0) { PanelBuildFailed(gui, raw, "the Back button vanished after it was created"); return; }
    b->setCaption(MyGUI::UString("BACK"));
    b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopBackClicked);
    b = UiBtn(gui, n.landBackBtn); if (b == 0) { PanelBuildFailed(gui, raw, "the Multiplayer screen's Back button vanished after it was created"); return; }
    b->setCaption(MyGUI::UString("BACK"));
    b->eventMouseButtonClick += MyGUI::newDelegate(OnCoopBackClicked);

    g_panelStatusShown = -1;
    g_panelStatusCaption.clear();   /* ui7 fold (review-ui7 #4): the new status widget is empty - no old text to measure (a false fallback) */
    g_worldListShown = -1;   /* ui2: both lists were just created empty - the push below fills them */
    g_profListShown = -1;
    PanelPush(gui);

    /* THE LAST TWO STEPS, and their order is the whole of "exists implies wired": anything that threw
       above leaves CoopPanel on screen with g_panelBuiltOk still 0, and the next tick destroys it and
       builds it again rather than finding it and returning. */
    /* U2-b: the rectangle this panel was laid out from, so the steady state below can see the parent
       change size under it.  Written BEFORE the built-ok flag goes up, so no tick can ever find a panel
       that is built and has no rectangle recorded. */
    ::InterlockedExchange(&g_panelAtParentW, (LONG)parentW);
    ::InterlockedExchange(&g_panelAtParentH, (LONG)parentH);
    ::InterlockedIncrement64(&g_panelBuilt);
    ::InterlockedExchange(&g_panelBuiltOk, 1);
    g_panelFailStreak = 0;

    if (g_panelBuildLogs < 4)
    {
        ++g_panelBuildLogs;
        /* The evidence line prints its INPUTS (6a lesson 12): a status area that is off the bottom of
           the window, or a row height of 3, is a number here before it is a screenshot. */
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << "[UI] built CoopPanel #" << (long long)g_panelBuilt
           << " skin=" << n.skinWindow
           << " parent=(" << parentW << "," << parentH << ")"
           << " panel=(" << pxLeft << "," << pyTop << "," << pW << "," << pH << ")"
           << " client=(" << W << "," << H << ")"
           << " rowH=" << rowH << " rows=" << rows
           << " mode=" << (g_panelMode == 0 ? "HOST" : "JOIN")
           << " screen=" << (long long)g_panelScreen
           << " threadId=" << (long long)::GetCurrentThreadId()
           << " - U2: the button WRITES shared_wastelands.cfg and then asks config.cpp to re-read it and arm on"
              " the next title tick. This file opens no socket itself.";
        DebugLog(ss.str());
    }
}

/* ------------------------------------------------------------------------------------------------
   U2 / review-p8i M-2 - THE TITLE-SCREEN NOTICE.
   One read-only, word-wrapped EditBox on the game's own Kenshi_WordWrapEmpty skin - the same thing
   Kenshi_MessageBox.layout uses for its own text - laid across the top of the title art, so a sentence
   the panel cannot show (because there is no panel) still reaches the player.  It is a child of the same
   parent the MULTIPLAYER button uses, so it is destroyed with the title screen like everything else of
   ours, its name begins with "Coop" and carries no '_' (RE_Kenshi's suffix test can never match it), and
   it is destroyed BY POINTER, never by a name sweep (F725 R8).
   ------------------------------------------------------------------------------------------------ */
static void NoticeFailed(const char* why)
{
    const coopui::PanelRefusal r = coopui::PanelNoteRefusal(g_noticeFailStreak, kPanelFailStreakCap);
    g_noticeFailStreak = r.streak;
    if (r.giveUp == 0) return;
    ::InterlockedExchange(&g_noticeWanted, 0);
    ::InterlockedIncrement64(&g_noticeGaveUp);
    ErrorLog(std::string("[UI] the title-screen notice could not be created after ")
             + coopui::PanelNum((long long)kPanelFailStreakCap) + " tries - " + (why ? why : "?")
             + ". It has stopped trying. The sentence it was carrying is in the [UI] line above this one,"
               " and it is the only place that sentence now exists.");
}

/* ui1 - HARNESS SCREENSHOTS (`shot <label> dialog`): each dialog's absolute on-screen rectangle in MyGUI's view pixels (the
   game window's client area - Inferred: MyGUI's view is the render window), logged when it shows and again when it changes.
   ui2: the title-screen panel too, as coop-panel. */
static int g_rectHl[4] = { -1, -1, -1, -1 }, g_rectNotice[4] = { -1, -1, -1, -1 }, g_rectQuiet[4] = { -1, -1, -1, -1 };
/* uishot (owner 2026-09-27) - TEST-ONLY `uipreview`: what the preview put up. MAIN THREAD (the command channel and the title
   tick share the title pump). kind 0 none, 1 a title panel screen, 2 the title error box, 3 the host-left dialog, 4 the
   connection-problem notice. g_uiPrevPending: the title tick still owes the SHOWN line (logged once the widget exists). */
static int         g_uiPrevKind = 0, g_uiPrevPending = 0, g_uiPrevSampleWorlds = 0, g_uiPrevSampleProfs = 0;
static std::string g_uiPrevWhat;
static int g_rectPanel[4] = { -1, -1, -1, -1 };
static void UiRectForget(int* seen) { seen[0] = seen[1] = seen[2] = seen[3] = -1; }
static void UiLogRect(const char* name, MyGUI::Widget* w, int* seen)
{
    if (w == 0) return;
    const MyGUI::IntCoord c = w->getAbsoluteCoord();
    if (seen[0] == c.left && seen[1] == c.top && seen[2] == c.width && seen[3] == c.height) return;
    seen[0] = c.left; seen[1] = c.top; seen[2] = c.width; seen[3] = c.height;
    DebugLog(std::string("[UI] rect ") + name + " x=" + coopui::PanelNum((long long)c.left) + " y=" + coopui::PanelNum((long long)c.top)
             + " w=" + coopui::PanelNum((long long)c.width) + " h=" + coopui::PanelNum((long long)c.height));
}
/* ui1 (ui-polish-audit 3.3): the OK button takes the error box down (the tick destroys it on its next pass). */
static void OnCoopNoticeOkClicked(MyGUI::Widget* /*sender*/) { UiClearNotice(); }

/* ui7 - A BOX RE-FITTED TO ITS MEASURED TEXT.  The builders make the error / identity box at the estimate (coopui::NoticeBoxH),
   set its sentence, and call this: the body's text is measured (UiMeasureText - MyGUI's wrapped size at the body's real
   width, which a height change does not move), the box is made frame + coopui::NoticeClientHFor(text + inset, lineH) tall -
   its text, ONE BLANK LINE of the measured line height, the buttons - under the ceiling the builders keep (the title art's
   height - 20), and the body and buttons go to coopui::NoticeLayoutFor's rows.  The frame is measured too (the box's height
   minus its client's).  A failed measurement leaves the estimate's box (counted).  The title tick calls it again on the
   build's own tick and the two after it (the refit counters), so a size MyGUI settles only after a layout pass still lands - bounded,
   never a loop; setCoord only when a rectangle differs.  `key` = the build's number, so each build logs once. */
static long long g_boxFitBuilds = 0;
static int g_noticeRefit = 0, g_idBoxRefit = 0;   /* ticks of re-fit left after a box's build */
static void BoxPlaceY(MyGUI::Widget* w, int y, int h)
{
    if (w == 0) return;
    const MyGUI::IntCoord c = w->getCoord();
    if (c.top != y || c.height != h) w->setCoord(MyGUI::IntCoord(c.left, y, c.width, h));
}
static void BoxFitToText(MyGUI::Gui* gui, MyGUI::Widget* box, int ph, const char* bodyName, const char* btn0, const char* btn1,
                         int slot, const char* area)
{
    if (box == 0) return;
    MyGUI::Window* win = box->castType<MyGUI::Window>(false);
    MyGUI::Widget* c = (win != 0) ? win->getClientWidget() : 0;
    MyGUI::EditBox* e = UiEdit(gui, std::string(bodyName));
    MyGUI::Widget* b0 = UiFind(gui, std::string(btn0));
    MyGUI::Widget* b1 = (btn1 != 0) ? UiFind(gui, std::string(btn1)) : 0;
    if (c == 0 || e == 0 || b0 == 0) return;
    UiTextMeasure m;
    if (!UiMeasureText(e, &m)) return;   /* the estimate's box stays (counted) */
    UiMeasureLog(slot, area, (g_boxFitBuilds << 32) | ((long long)(m.lineH & 0xFFFF) << 16) | (long long)(m.textH & 0xFFFF), m);
    const MyGUI::IntCoord bc = box->getCoord();
    const int frameH = bc.height - c->getHeight();
    int nh = frameH + coopui::NoticeClientHFor(m.textH + m.insetH, m.lineH);
    if (nh > ph - 20) nh = ph - 20;   /* the builders' ceiling */
    if (nh != bc.height && nh > frameH) box->setCoord(MyGUI::IntCoord(bc.left, bc.top, bc.width, nh));
    const coopui::NoticeLayout lay = coopui::NoticeLayoutFor(c->getHeight(), m.lineH);
    BoxPlaceY(e, lay.textY, lay.textH);
    BoxPlaceY(b0, lay.btnY, lay.btnH);
    BoxPlaceY(b1, lay.btnY, lay.btnH);
}

/* ui1 (ui-polish-audit 3.3) - AN ERROR BOX, not a bare strip: the host-left dialog's recipe on the title screen - a
   Kenshi_WindowC window (title bar, no close X) titled by coopui::NoticeTitle (CAN'T LOAD / CAN'T JOIN / MOD MISMATCH /
   MULTIPLAYER), the sentence centred in Kenshi_WordWrapEmpty in the message box's colour, one Kenshi_Button2 OK. Still a
   CHILD of the title art (destroyed with the title screen), still named CoopNoticeText and destroyed by pointer; centred
   across the top, taller for a long sentence (the mods refusal lists mods). ui6 (decision 42): while it is live, Kenshi's
   menu under it is blocked (UiBoxBlockSync) and ESC presses its OK; the panel, when open, hides it and stays usable. */
static void NoticeBuild(MyGUI::Gui* gui, MyGUI::Widget* parent)
{
    const int pw = parent->getWidth();
    const int ph = parent->getHeight();
    if (pw < 120 || ph < 60) { NoticeFailed("the title screen's own parent is smaller than 120 x 60"); return; }
    const size_t len = g_noticeText.size();
    int nw = (pw * (len > 160 ? 40 : 30)) / 100; if (nw < 380) nw = 380; if (nw > pw - 20) nw = pw - 20;
    /* ui5 (panel-mockups.md section 10): as tall as its text, not a share of the screen - built at coopui::NoticeBoxH (the
       estimate) and re-fitted to the measured text below (ui7, BoxFitToText).
       ui6: its text, one blank line, OK - ui5's 150 px floor is gone (it stretched a short box's text area). */
    int nh = coopui::NoticeBoxH(g_noticeText, nw); if (nh > ph - 20) nh = ph - 20;
    if (nw < 200 || nh < 120) { NoticeFailed("the title screen is too small for the error box"); return; }
    const int nx = (pw - nw) / 2;
    const int ny = ph / 40;

    MyGUI::Widget* raw = MkOver(parent, NM().typeWindow, std::string("Kenshi_WindowC"), nx, ny, nw, nh, NM().notice);   /* ui3: own layer node */
    if (raw == 0) { NoticeFailed("createWidgetT returned null for the notice"); return; }
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    MyGUI::Widget* c = (win != 0) ? win->getClientWidget() : 0;
    if (win == 0 || c == 0) { gui->destroyWidget(raw); NoticeFailed("the notice is not a Window with a client area"); return; }
    win->setMovable(false);
    win->setCaption(MyGUI::UString((g_noticeTitle.empty() ? coopui::NoticeTitle(g_noticeText) : g_noticeTitle).c_str()));   /* T-201 N1 */
    const int W = c->getWidth(), H = c->getHeight();
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);   /* ui6: the text, one blank line, the button row */
    int btnW = W / 4; if (btnW < 120) btnW = 120; if (btnW > W - 16) btnW = W - 16;
    MyGUI::Widget* bodyW = Mk(c, NM().typeEdit, NM().skinWrap, 8, lay.textY, W - 16, lay.textH, std::string("CoopNoticeBody"));
    MyGUI::Widget* okW = Mk(c, NM().typeButton, NM().skinBtn2, W - btnW - 8, lay.btnY, btnW, lay.btnH, std::string("CoopNoticeOk"));   /* ui5: OK bottom-right */
    MyGUI::EditBox* e = (bodyW != 0) ? bodyW->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* ok = (okW != 0) ? okW->castType<MyGUI::Button>(false) : 0;
    if (e == 0 || ok == 0) { gui->destroyWidget(raw); NoticeFailed("the error box's text or OK button could not be made"); return; }
    g_noticeFailStreak = 0;
    e->setEditStatic(true);
    e->setEditReadOnly(true);
    e->setEditMultiLine(true);
    e->setEditWordWrap(true);
    e->setTextAlign(MyGUI::Align::Center);   /* ui5b: centred, as Kenshi's own message box (manager decision 2026-09-27) */
    e->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    e->setCaption(MyGUI::UString(g_noticeText.c_str()));
    ok->setCaption(MyGUI::UString("OK"));
    ok->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));   /* ui3: lit by hand, as UiHostLeftBody lights EXIT GAME */
    ok->eventMouseButtonClick += MyGUI::newDelegate(OnCoopNoticeOkClicked);
    ++g_boxFitBuilds;
    BoxFitToText(gui, raw, ph, "CoopNoticeBody", "CoopNoticeOk", 0, 4, "errorbox");   /* ui7: as tall as its MEASURED text */
    g_noticeRefit = 3;
    g_noticeShown = g_noticeSeq;
    ::InterlockedIncrement64(&g_noticeBuilt);
}

static void NoticeDestroy(MyGUI::Gui* gui, MyGUI::Widget* w)
{
    gui->destroyWidget(w);
    g_noticeShown = -1;
    ::InterlockedIncrement64(&g_noticeDestroyed);
    UiRectForget(g_rectNotice);   /* ui1 */
}

/* PP3d - THE IDENTITY BOX: the error box's recipe (Kenshi_WindowC titled in CAPS, the sentence centred in Kenshi_WordWrapEmpty
   in the message box's colour 0.871 0.845 0.810, Kenshi_Button2 buttons lit by hand) with 1 or 2 buttons, in the top third.
   An Overlapped child of the title art like the error box. Destroyed by pointer. ui6 (decision 42): while it is live, Kenshi's
   menu under it is blocked (UiBoxBlockSync) and ESC presses CANCEL (MULTIPLAYER DATA DAMAGED) or OK (CAN'T START). */
static const char* const kIdBoxName = "CoopIdentityBox";   /* no '_' in any of our names (RE_Kenshi's suffix match) */
static int g_rectIdBox[4] = { -1, -1, -1, -1 };
static void OnCoopIdBoxDismiss(MyGUI::Widget* /*sender*/)  { ::InterlockedExchange(&g_idBoxButton, (LONG)coopdata::kIdBoxDismiss); }
static void OnCoopIdBoxContinue(MyGUI::Widget* /*sender*/) { ::InterlockedExchange(&g_idBoxButton, (LONG)coopdata::kIdBoxContinue); }
static void IdBoxFailed(const char* why)
{
    const coopui::PanelRefusal r = coopui::PanelNoteRefusal(g_idBoxFailStreak, kPanelFailStreakCap);
    g_idBoxFailStreak = r.streak;
    if (r.giveUp == 0) return;
    ::InterlockedExchange(&g_idBoxWanted, 0);
    ErrorLog(std::string("[UI] PP3d: the identity box could not be created after ") + coopui::PanelNum((long long)kPanelFailStreakCap)
             + " tries - " + (why ? why : "?") + ". It has stopped trying; HOST and JOIN stay refused.");
}
static void IdBoxBuild(MyGUI::Gui* gui, MyGUI::Widget* parent, int kind)
{
    const int pw = parent->getWidth();
    const int ph = parent->getHeight();
    if (pw < 120 || ph < 60) { IdBoxFailed("the title screen's own parent is smaller than 120 x 60"); return; }
    int nw = (pw * 36) / 100; if (nw < 540) nw = 540; if (nw > pw - 20) nw = pw - 20;
    int nh = coopui::NoticeBoxH(coopui::IdBoxText(kind), nw); if (nh > ph - 20) nh = ph - 20;   /* ui5: as tall as its text; ui6: + one blank line + the buttons, no 150 px floor; ui7: the estimate - re-fitted to the measured text below */
    if (nw < 200 || nh < 120) { IdBoxFailed("the title screen is too small for the identity box"); return; }
    const int nx = (pw - nw) / 2;
    const int ny = ph / 12;   /* the top third of the screen */
    MyGUI::Widget* raw = MkOver(parent, NM().typeWindow, std::string("Kenshi_WindowC"), nx, ny, nw, nh, std::string(kIdBoxName));
    if (raw == 0) { IdBoxFailed("createWidgetT returned null for the identity box"); return; }
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    MyGUI::Widget* c = (win != 0) ? win->getClientWidget() : 0;
    if (win == 0 || c == 0) { gui->destroyWidget(raw); IdBoxFailed("the identity box is not a Window with a client area"); return; }
    win->setMovable(false);
    win->setCaption(MyGUI::UString(coopui::IdBoxTitle(kind)));
    const int W = c->getWidth(), H = c->getHeight();
    const int nb = coopui::IdBoxButtons(kind);
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);   /* ui6: the text, one blank line, the button row */
    const int btnH = lay.btnH;
    int btnW = nb == 2 ? (W - 24) / 2 : W / 4; if (btnW < 120) btnW = 120; if (btnW > 280) btnW = 280;
    const int gap = 8;
    /* ui5 (panel-mockups.md section 11): CANCEL bottom-left, the action (OK / CONTINUE AS NEW PLAYER) bottom-right. */
    const int bx = nb == 2 ? gap : W - btnW - gap;
    MyGUI::Widget* bodyW = Mk(c, NM().typeEdit, NM().skinWrap, 8, lay.textY, W - 16, lay.textH, std::string("CoopIdentityBody"));
    MyGUI::Widget* b0W = Mk(c, NM().typeButton, NM().skinBtn2, bx, lay.btnY, btnW, btnH, std::string("CoopIdentityDismiss"));
    MyGUI::Widget* b1W = nb == 2 ? Mk(c, NM().typeButton, NM().skinBtn2, W - btnW - gap, lay.btnY, btnW, btnH, std::string("CoopIdentityContinue")) : 0;
    MyGUI::EditBox* e = (bodyW != 0) ? bodyW->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* b0 = (b0W != 0) ? b0W->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* b1 = (b1W != 0) ? b1W->castType<MyGUI::Button>(false) : 0;
    if (e == 0 || b0 == 0 || (nb == 2 && b1 == 0)) { gui->destroyWidget(raw); IdBoxFailed("the identity box's text or buttons could not be made"); return; }
    g_idBoxFailStreak = 0;
    e->setEditStatic(true);
    e->setEditReadOnly(true);
    e->setEditMultiLine(true);
    e->setEditWordWrap(true);
    e->setTextAlign(MyGUI::Align::Center);   /* ui5b: centred, as the error box */
    e->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    e->setCaption(MyGUI::UString(coopui::IdBoxText(kind)));
    b0->setCaption(MyGUI::UString(coopui::IdBoxDismissCaption(kind)));
    b0->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    b0->eventMouseButtonClick += MyGUI::newDelegate(OnCoopIdBoxDismiss);
    if (b1 != 0)
    {
        b1->setCaption(MyGUI::UString(coopui::kIdBox2Continue));
        b1->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
        b1->eventMouseButtonClick += MyGUI::newDelegate(OnCoopIdBoxContinue);
    }
    ++g_boxFitBuilds;
    BoxFitToText(gui, raw, ph, "CoopIdentityBody", "CoopIdentityDismiss", nb == 2 ? "CoopIdentityContinue" : 0, 5, "identitybox");   /* ui7 */
    g_idBoxRefit = 3;
    g_idBoxShownKind = kind;
    DebugLog(std::string("[UI] PP3d: the ") + coopui::IdBoxTitle(kind) + " box is on the title screen");
}
static void IdBoxDestroy(MyGUI::Gui* gui, MyGUI::Widget* w)
{
    gui->destroyWidget(w);
    g_idBoxShownKind = 0;
    UiRectForget(g_rectIdBox);
}

/* ui6 (decision 42, owner-approved 2026-09-28) - A BOX THAT NEEDS AN ANSWER BLOCKS KENSHI'S TITLE MENU UNTIL IT IS ANSWERED.
   Every title box has a button to press (the error box: OK, whatever its title; CAN'T START MULTIPLAYER: OK; MULTIPLAYER DATA
   DAMAGED: CANCEL + CONTINUE AS NEW PLAYER) - none is a passive notice - so every one blocks while it is live.  Kenshi's menu
   stays VISIBLE: the blocking layer is four PanelEmpty strips, which draw nothing (Kenshi's own message box does not dim
   either, runs/T457); a click on them does nothing but count boxBlockedClicks.
   THE CLICK ORDER IS NOT RELIED ON.  The panel's U2-b backdrop showed the trap: MyGUI 3.2.3 asks Overlapped siblings for
   clicks in CREATION order but draws them last-on-top (ui3b review HIGH), so a full-screen layer made before the box takes
   the box's clicks, and one made after it is only safe until a click raises a node (Inferred).  The strips therefore tile
   the title art AROUND the box's rectangle (coopui::BoxBlockStrips) and never overlap it: inside the box nothing of ours
   but the box is under the mouse, whatever the order.  Outside it a strip - an Overlapped child with its own layer node -
   is asked before the title buttons, which are plain children in the title art's own node (ui3 Fix A: child nodes first).
   One live box at a time (the identity box takes precedence and hides the error box), so there is one hole.
   Cut by the title tick when a box becomes live, and cut again when the box, its rectangle or the title art changes;
   dropped when no box is live (a button, ESC, the panel opened over it, the title screen gone) and by the fault latch. */
static const char* const kBoxBlockName[4] = { "CoopBoxBlockTop", "CoopBoxBlockBottom", "CoopBoxBlockLeft", "CoopBoxBlockRight" };
static int         g_boxBlockCut[6] = { -1, -1, -1, -1, -1, -1 };   /* TITLE PUMP ONLY: the hole (l t w h) and parent (w h) cut for */
static const void* g_boxBlockParent = 0;                             /* compared, never dereferenced */
static void OnCoopBoxBlockClick(MyGUI::Widget* /*sender*/) { ::InterlockedIncrement64(&g_boxBlockedClicks); }
static void UiBoxBlockForget()
{
    ::InterlockedExchange(&g_boxLive, 0);   /* ESC is the game's again */
    for (int i = 0; i < 6; ++i) g_boxBlockCut[i] = -1;
    g_boxBlockParent = 0;
}
/* By name on this tick, destroyed by pointer (the backdrop's rule, F725 R8). */
static int UiBoxBlockDestroyStrips(MyGUI::Gui* gui)
{
    int n = 0;
    for (int i = 0; i < 4; ++i)
    {
        MyGUI::Widget* w = gui->findWidgetT(std::string(kBoxBlockName[i]), false);
        if (w != 0) { gui->destroyWidget(w); ++n; }
    }
    return n;
}
/* Takes the strips away if anything says they may be up; costs nothing (no lookup) when nothing is. */
static void UiBoxBlockGone(MyGUI::Gui* gui, const char* why)
{
    if (g_boxBlockCut[0] < 0 && ::InterlockedCompareExchange(&g_boxLive, 0, 0) == 0) return;
    const int n = UiBoxBlockDestroyStrips(gui);
    UiBoxBlockForget();
    ::InterlockedIncrement64(&g_boxBlockDropped);
    DebugLog(std::string("[UI] ui6: Kenshi's title menu takes clicks again (") + coopui::PanelNum((long long)n)
             + " strips removed) - " + (why ? why : "?"));
}
/* The box `box` (live 1 the error box, 2 an identity box) is live under `parent`: make sure the strips are cut around it. */
static void UiBoxBlockSync(MyGUI::Gui* gui, MyGUI::Widget* parent, MyGUI::Widget* box, int live, int boxNew)
{
    ::InterlockedExchange(&g_boxLive, (LONG)live);   /* from now ESC answers this box (UiBoxEscFromEngine) */
    const MyGUI::IntCoord c = box->getCoord();     /* relative to the title art: the box is its child */
    const int pw = parent->getWidth(), ph = parent->getHeight();
    if (boxNew == 0 && g_boxBlockParent == (const void*)parent
     && g_boxBlockCut[0] == c.left && g_boxBlockCut[1] == c.top && g_boxBlockCut[2] == c.width && g_boxBlockCut[3] == c.height
     && g_boxBlockCut[4] == pw && g_boxBlockCut[5] == ph) return;   /* already cut for this box: no lookup, no work */
    UiBoxBlockDestroyStrips(gui);   /* a re-cut: the old strips go first, under the same names */
    int r[16];
    coopui::BoxBlockStrips(pw, ph, c.left, c.top, c.width, c.height, r);
    int made = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (r[4 * i + 2] <= 0 || r[4 * i + 3] <= 0) continue;
        MyGUI::Widget* w = MkOver(parent, NM().typeWidget, NM().skinPanel, r[4 * i], r[4 * i + 1], r[4 * i + 2], r[4 * i + 3],
                                  std::string(kBoxBlockName[i]));
        if (w == 0) { ::InterlockedIncrement64(&g_boxBlockFailed); continue; }
        w->setNeedMouseFocus(true);   /* the whole point: it takes the click, and does nothing with it */
        w->eventMouseButtonClick += MyGUI::newDelegate(OnCoopBoxBlockClick);
        ++made;
    }
    /* Stored even when a strip failed: the next tick does not retry at 1 kHz; a changed rectangle or a new box cuts again. */
    g_boxBlockCut[0] = c.left; g_boxBlockCut[1] = c.top; g_boxBlockCut[2] = c.width; g_boxBlockCut[3] = c.height;
    g_boxBlockCut[4] = pw; g_boxBlockCut[5] = ph;
    g_boxBlockParent = (const void*)parent;
    ::InterlockedIncrement64(&g_boxBlockBuilt);
    DebugLog(std::string("[UI] ui6: Kenshi's title menu is blocked around the ") + (live == 2 ? "identity box" : "error box")
             + " (" + coopui::PanelNum((long long)made) + " transparent strips) - its buttons or ESC answer it");
}
/* ui7 fold (review-ui7 #1): a box's measured size re-read on the ticks after its build - the build's own tick included
   (the builders set 3, so two later ticks, then it stops).  Called on BOTH of the title tick's exits just before
   UiBoxBlockTick; before, it sat after the steady-state return, so it never ran while the button and box were up.  A
   count left over for a box that is gone is dropped, so it cannot fire on a later box's build. */
static void UiBoxRefitTick(MyGUI::Gui* gui, MyGUI::Widget* parent, MyGUI::Widget* notice, MyGUI::Widget* idbox)
{
    if (notice == 0) g_noticeRefit = 0;
    if (idbox == 0) g_idBoxRefit = 0;
    if (parent == 0) return;
    if (notice != 0 && g_noticeRefit > 0)
    {
        --g_noticeRefit;
        BoxFitToText(gui, notice, parent->getHeight(), "CoopNoticeBody", "CoopNoticeOk", 0, 4, "errorbox");
    }
    if (idbox != 0 && g_idBoxRefit > 0)
    {
        --g_idBoxRefit;
        BoxFitToText(gui, idbox, parent->getHeight(), "CoopIdentityBody", "CoopIdentityDismiss",
                     coopui::IdBoxButtons(g_idBoxShownKind) == 2 ? "CoopIdentityContinue" : 0, 5, "identitybox");
    }
}
/* The title tick's one call, on both of its return paths, after every box has been built or destroyed for this tick. */
static void UiBoxBlockTick(MyGUI::Gui* gui, MyGUI::Widget* parent, MyGUI::Widget* panel, MyGUI::Widget* notice,
                           MyGUI::Widget* idbox, int boxNew)
{
    /* ui3b (review MED): a box is hidden while the panel is open (the panel's status area carries the message) and shows
       again once it closes.  ui6: the error box is hidden while an identity box is up too - the identity box takes
       precedence (PP3d) - so one box is live at a time and the error box shows once the identity box is answered. */
    if (notice != 0) notice->setVisible(panel == 0 && idbox == 0);
    if (idbox != 0) idbox->setVisible(panel == 0);
    if (panel != 0 || parent == 0) { UiBoxBlockGone(gui, panel != 0 ? "the panel is open over the box" : "no title art"); return; }
    if (idbox != 0)       UiBoxBlockSync(gui, parent, idbox, 2, boxNew);
    else if (notice != 0) UiBoxBlockSync(gui, parent, notice, 1, boxNew);
    else                  UiBoxBlockGone(gui, "the box was answered");
}
/* The fault latch's drop (UiLatchPutBack): no tick runs again to do it. Own SEH frame, no C++ temporaries in it. */
static __declspec(noinline) void UiBoxBlockLatchDropInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) { UiBoxBlockForget(); return; }
    UiBoxBlockGone(gui, "the UI shut itself off after a fault");
}
static int UiBoxBlockLatchDropGuarded()
{
    __try { UiBoxBlockLatchDropInner(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return 0;
}
/* PP3d: a button of the box, handled on the title pump. Only CONTINUE AS NEW PLAYER on a DAMAGED box, pressed for real (not a
   preview), replaces the identity (IdentityReplaceByChoice checks coopdata::IdentityMayReplace again); every button closes it. */
static void IdBoxPressed(int button)
{
    const int shown = (int)::InterlockedCompareExchange(&g_idBoxWanted, 0, 0);
    if (shown == 0) return;
    if (g_idBoxPreview != 0)
        DebugLog("[UI] uipreview: an identity box button was pressed - the preview closes, nothing replaced");
    else if (shown == coopdata::kIdKindDamaged && coopdata::IdentityMayReplace(IdentitySessionOnlyKind(), button))
    {
        if (IdentityReplaceByChoice() != 0) DebugLog("[UI] PP3d: CONTINUE AS NEW PLAYER - a new identity is in place; press HOST or JOIN again");
        else ErrorLog("[UI] PP3d: CONTINUE AS NEW PLAYER did not replace the identity (the [CFG] line says why); HOST and JOIN stay refused");
    }
    else
        DebugLog(std::string("[UI] PP3d: the identity box closed (") + coopui::IdBoxDismissCaption(shown) + ") - nothing changed; HOST and JOIN stay refused");
    IdBoxClear();
}

// "<something>_ExitButton" -> true. The prefix is a pointer printed in hex, which contains no '_',
// so the first '_' is always the layout separator.
static bool NameCarriesLayoutSuffix(const std::string& name, const char* suffix)
{
    const std::string::size_type p = name.find('_');
    if (p == std::string::npos) return false;
    return name.compare(p + 1, std::string::npos, suffix) == 0;
}

// Depth-limited so a cycle in someone else's widget tree cannot hang the menu thread. The title
// screen is four levels deep (Root / ImageBox / Button), so 12 is slack, not a constraint.
static MyGUI::Widget* FindByLayoutSuffix(MyGUI::Widget* w, const char* suffix, int depth)
{
    if (w == 0 || depth > 12) return 0;
    if (NameCarriesLayoutSuffix(w->getName(), suffix)) return w;
    const size_t n = w->getChildCount();
    for (size_t i = 0; i < n; ++i)
    {
        MyGUI::Widget* hit = FindByLayoutSuffix(w->getChildAt(i), suffix, depth + 1);
        if (hit != 0) return hit;
    }
    return 0;
}
MyGUI::Widget* UiFindLayoutSuffix(MyGUI::Widget* root, const char* suffix) { return FindByLayoutSuffix(root, suffix, 0); }

/* U3 - THE COLUMN, FOUND LIVE.  col[0] = NEW GAME, col[1..5] = LOAD GAME, IMPORT GAME, OPTIONS, CREDITS, EXIT.
   Each must be a DIRECT child of `parent` (the title art ImageBox that holds the column), or it is treated as
   missing.  Returns 1 with every slot filled, else 0 with `why` naming the first one that was not there. */
static int UiFindColumn(MyGUI::Widget* parent, MyGUI::Widget* col[6], std::string& why)
{
    for (int k = 0; k < 6; ++k)
    {
        const char* suf = (k == 0) ? kNewGameSuffix : kBelowSuffixes[k - 1];
        MyGUI::Widget* w = FindByLayoutSuffix(parent, suf, 0);
        if (w == 0 || w->getParent() != parent)
        {
            why = std::string(suf) + (w == 0 ? " is not on the title screen" : " is not in the menu column's parent");
            return 0;
        }
        col[k] = w;
    }
    return 1;
}

/* U3-b (review-u3): the row step is the LIVE gap from CONTINUE's top to NEW GAME's top.  The game never moves
   those two and neither do we, and every menu button is align="Default" (pixel coords), so this gap is the pitch
   in BOTH states of the column and through a background resize - which U3's 0.111111 x parent height was not:
   after a resize it re-spaced LOAD GAME..EXIT at a new pitch while CONTINUE and NEW GAME kept their pixels.
   Returns 0 with `why` when either is missing or the gap is not a positive row. */
static int UiLivePitch(MyGUI::Widget* parent, std::string& why)
{
    MyGUI::Widget* c = FindByLayoutSuffix(parent, kContinueSuffix, 0);
    if (c == 0 || c->getParent() != parent)
    {
        why = std::string(kContinueSuffix) + " is not in the menu column's parent";
        return 0;
    }
    MyGUI::Widget* g = FindByLayoutSuffix(parent, kNewGameSuffix, 0);
    if (g == 0 || g->getParent() != parent)
    {
        why = std::string(kNewGameSuffix) + " is not in the menu column's parent";
        return 0;
    }
    const int gap = g->getTop() - c->getTop();
    if (gap > 0) return gap;
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "the gap from CONTINUE to NEW GAME is not a row (" << gap << " px)";
    why = ss.str();
    return 0;
}

/* U3: does the longer column fit?  EXIT ends up six rows under NEW GAME (ours + five moved), and its bottom
   must stay inside the parent.  At the layout's numbers that is 0.25 + 6 x 0.111111 + 0.0638889 = 0.980556,
   the very row P8a's button used below EXIT - so the longer column needs no room the old one did not use. */
static int UiColumnFits(MyGUI::Widget* parent, MyGUI::Widget* col[6], int pitch, std::string& why)
{
    const int bottom = col[0]->getTop() + 6 * pitch + col[5]->getHeight();
    if (pitch > 0 && bottom <= parent->getHeight()) return 1;
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "the longer column would run off the parent (EXIT would end at y=" << bottom
       << ", parent height " << parent->getHeight() << ", rowPitch " << pitch << ")";
    why = ss.str();
    return 0;
}

/* U3 - THE MOVE, AND WHY IT CANNOT SHIFT TWICE.  Every button is put ONE ROW BELOW THE BUTTON ABOVE IT, read
   live: ours under NEW GAME, LOAD GAME under ours, IMPORT GAME under LOAD GAME, and so on down to EXIT.  A
   button already where that rule puts it is not touched, so running this on every check is a no-op at rest
   and puts back only what the engine moved.  Each keeps its own left; only its top changes.
   Widget::setPosition(const IntPoint&) is VIRTUAL in MyGUI_Widget.h (line 112), so this is a call through the
   vtable and adds no MyGUI import (the (int,int) overload beside it is NOT virtual and is deliberately not
   used).  Returns how many widgets it moved; `book` (kUiBook*) says which counter books the game buttons. */
static int UiApplyOrder(MyGUI::Widget* ours, MyGUI::Widget* col[6], int pitch, int book)
{
    int moved = 0;
    MyGUI::Widget* above = col[0];
    for (int k = 0; k < 6; ++k)
    {
        MyGUI::Widget* w = (k == 0) ? ours : col[k];
        const int want = above->getTop() + pitch;
        if (w->getTop() != want)
        {
            w->setPosition(MyGUI::IntPoint(w->getLeft(), want));
            ++moved;
            if (w != ours)
                ::InterlockedIncrement64(book == kUiBookReshift ? &g_uiBtnsReshifted : (book == kUiBookRepitch ? &g_uiBtnsRepitched : &g_uiBtnsMoved));
        }
        above = w;
    }
    return moved;
}

/* U3-b (review-u3): one line per MODE CHANGE - ordered -> fallback here, fallback -> ordered in
   UiOrderNowOrdered - so a fallback re-decided every tick, or at every pixel size of a drag, cannot become a
   log stream.  The counts carry the rest. */
static void UiOrderFallback(const std::string& why)
{
    ::InterlockedIncrement64(&g_uiOrderFallback);
    if (g_uiOrderLoggedMode == 2) return;
    g_uiOrderLoggedMode = 2;
    DebugLog(std::string("[UI] MENU ORDER FALLBACK: the MULTIPLAYER button goes below EXIT, not after NEW GAME,"
                         " because ") + why + ". Read buttonOrderFallback= in the ui[] readout.");
}

/* U3-b: the column is ordered (again).  Counted and logged only as a return FROM a fallback. */
static void UiOrderNowOrdered()
{
    if (g_uiOrderLoggedMode != 2) return;
    g_uiOrderLoggedMode = 1;
    ::InterlockedIncrement64(&g_uiOrderRestored);
    DebugLog(std::string("[UI] MENU ORDER RESTORED: the MULTIPLAYER button is back right after NEW GAME - the"
                         " column fits again. Read buttonOrderRestored= in the ui[] readout."));
}

/* U3-b: remember the five engine buttons' own coords before the first shift.  Already held for THESE widgets
   under THIS parent -> kept as they are (the column may be moved now, and must not be re-read as original). */
static void UiRememberOriginals(MyGUI::Widget* parent, MyGUI::Widget* col[6])
{
    if (g_uiOrigHeld != 0 && g_uiOrigParent == parent)
    {
        int same = 1;
        for (int k = 0; k < 5; ++k)
            if (g_uiOrigW[k] != col[k + 1]) same = 0;
        if (same != 0) return;
    }
    for (int k = 0; k < 5; ++k)
    {
        g_uiOrigW[k] = col[k + 1];
        g_uiOrigL[k] = col[k + 1]->getLeft();
        g_uiOrigT[k] = col[k + 1]->getTop();
    }
    g_uiOrigParent = parent;
    g_uiOrigHeld = 1;
}

/* U3-b (review-u3): THE PUT-BACK RESTORES, IT DOES NOT RECOMPUTE.  Each engine button found live under this
   parent at its remembered address goes back to exactly its remembered coords; one that is gone is skipped
   (its pointer is compared, never followed).  The originals are spent either way. */
static void UiPutBack(MyGUI::Widget* parent)
{
    if (g_uiOrigHeld != 0 && g_uiOrigParent == parent)
    {
        for (int k = 0; k < 5; ++k)
        {
            MyGUI::Widget* w = FindByLayoutSuffix(parent, kBelowSuffixes[k], 0);
            if (w == 0 || w != g_uiOrigW[k] || w->getParent() != parent) continue;
            if (w->getLeft() != g_uiOrigL[k] || w->getTop() != g_uiOrigT[k])
                w->setPosition(MyGUI::IntPoint(g_uiOrigL[k], g_uiOrigT[k]));
        }
    }
    g_uiOrigHeld = 0;
}

/* ui5c (user decision 40, 2026-09-27) - THE SUB-MENU REPLACES THE MAIN MENU.  While the MULTIPLAYER panel is up
   (our panel widget found live this tick - ui5c fold F2) the title column - CONTINUE, NEW GAME, ours, LOAD GAME, IMPORT GAME, OPTIONS, CREDITS, EXIT -
   is hidden, and it comes back when the panel closes.  Only a button that was VISIBLE is hidden and remembered;
   one the game had already hidden is never remembered, so we never show it.  The put-back shows a button only
   when the live lookup under the same parent finds it at the remembered address (compared, never followed).
   Hiding does not move anything - MyGUI keeps an invisible widget's coords - and the U3 order code reads only
   getTop / getLeft / getHeight / getParent, never visibility, so it works unchanged on a hidden column. */
static MyGUI::Widget* UiMenuSlot(MyGUI::Gui* gui, MyGUI::Widget* parent, int k)
{
    MyGUI::Widget* w = 0;
    if (k == 2) w = gui->findWidgetT(NM().ourBtn, false);
    else w = FindByLayoutSuffix(parent, k == 0 ? kContinueSuffix : (k == 1 ? kNewGameSuffix : kBelowSuffixes[k - 3]), 0);
    return (w != 0 && w->getParent() == parent) ? w : 0;   /* a direct child of the column's parent, or not ours to touch */
}

static void UiMenuClear()
{
    for (int k = 0; k < kUiMenuSlots; ++k) g_uiMenuHidW[k] = 0;
    g_uiMenuParent = 0;
    g_uiMenuHeld = 0;
    g_uiMenuCheckMs = 0;
}

static void UiMenuLog(const char* what, int n, const char* why)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[UI] TITLE MENU " << what << " n=" << n << " (" << why << ")";
    DebugLog(ss.str());
}

/* The title the column was hidden on is gone (or rebuilt): clear the state WITHOUT touching a widget - a new
   title's buttons were never hidden by us.  A no-op, and no log, when nothing is held. */
static void UiMenuForget(const char* why)
{
    if (g_uiMenuHeld == 0) return;
    int held = 0;
    for (int k = 0; k < kUiMenuSlots; ++k) if (g_uiMenuHidW[k] != 0) ++held;
    UiMenuClear();
    ::InterlockedIncrement64(&g_uiMenuForgot);
    UiMenuLog("forgotten", held, why);
}

/* The put-back: a remembered button is shown only when the live lookup under `parent` finds it at the remembered
   address (compared, never followed).  Clears the state, counts one menuRestored, one log line.  ui5c fold (F3): shared
   by the pass below and the fault latch's one best-effort put-back (UiLatchPutBack). */
static void UiMenuRestore(MyGUI::Gui* gui, MyGUI::Widget* parent, const char* why)
{
    int shown = 0;
    for (int k = 0; k < kUiMenuSlots; ++k)
    {
        if (g_uiMenuHidW[k] == 0) continue;
        MyGUI::Widget* w = UiMenuSlot(gui, parent, k);
        if (w == 0 || w != g_uiMenuHidW[k]) continue;   /* gone or replaced: the stored pointer is not followed */
        w->setVisible(true);
        ++shown;
    }
    UiMenuClear();
    ::InterlockedIncrement64(&g_uiMenuRestored);
    UiMenuLog("restored", shown, why);
}

/* Called once per tick on a LIVE title, with the column's parent: from the steady-state return (our button's
   parent) and after the build step (EXIT's parent) - the same widget, since ours is created with
   parent->createWidget.  ui5c fold (F2): `panelUp` is the caller's "our panel widget was found live this tick",
   never g_panelBuiltOk, which a title destroyed under the panel used to leave up.  Panel closed and nothing held:
   returns at once, no walk.  Panel up: the column is walked on the transition and then every kUiOrderCheckEveryMs,
   never per frame - or at once when `force` is set (F6: our button was just made, and must not show for a pass). */
static void UiMenuColumnSync(MyGUI::Gui* gui, MyGUI::Widget* parent, int panelUp, int force)
{
    if (parent == 0) return;   /* both callers pass a live parent */
    if (g_uiMenuHeld != 0 && g_uiMenuParent != parent) UiMenuForget("the title screen was rebuilt");
    if (panelUp == 0)
    {
        if (g_uiMenuHeld == 0) return;
        UiMenuRestore(gui, parent, "the MULTIPLAYER panel closed");
        return;
    }
    const DWORD nowMs = ::GetTickCount();
    if (g_uiMenuHeld != 0 && force == 0 && (DWORD)(nowMs - g_uiMenuCheckMs) < kUiOrderCheckEveryMs) return;
    g_uiMenuCheckMs = nowMs;
    /* ui5c fold (F7): Held / Parent go up, and each pointer is stored, BEFORE its setVisible(false) - a fault part-way
       through the walk can then leave a remembered button still showing (harmless: the put-back shows it), never a
       hidden one nobody remembers. */
    const int first = (g_uiMenuHeld == 0) ? 1 : 0;
    if (first != 0)
    {
        g_uiMenuHeld = 1;
        g_uiMenuParent = parent;
    }
    int hid = 0;
    for (int k = 0; k < kUiMenuSlots; ++k)
    {
        MyGUI::Widget* w = UiMenuSlot(gui, parent, k);
        if (w == 0 || !w->getVisible()) continue;   /* already hidden (by the game, or by us) - not re-remembered */
        g_uiMenuHidW[k] = w;
        w->setVisible(false);
        ++hid;
    }
    if (first != 0)
    {
        ::InterlockedIncrement64(&g_uiMenuHidden);
        UiMenuLog("hidden", hid, "the MULTIPLAYER panel is open");
    }
    else if (hid > 0)
    {
        ::InterlockedExchangeAdd64(&g_uiMenuRehidden, (LONG64)hid);
    }
}

/* ui5c fold (F3) - THE FAULT LATCH PUTS THE COLUMN BACK.  Once g_uiDisabled is up, UiTitleTick returns at its top and
   no menu pass runs again, so a column hidden for an open panel stayed hidden for the rest of the process, and the
   first ESC was swallowed by panel flags nothing would lower.  On the transition into that state (UiTitleTick, once):
   both panel flags come down, and ONE best-effort put-back runs - the same compare-then-show, under the live parent
   found through our button or else EXIT, inside its own SEH frame; if that faults too the state is dropped and
   nothing is retried.  The panel widget itself is not touched. */
static __declspec(noinline) void UiMenuLatchPutBackInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) { UiMenuForget("the UI shut itself off and there is no GUI"); return; }
    MyGUI::Widget* parent = 0;
    MyGUI::Widget* ours = gui->findWidgetT(NM().ourBtn, false);
    if (ours != 0) parent = ours->getParent();
    if (parent == 0)
    {
        MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
        while (roots.next())
        {
            MyGUI::Widget* exitBtn = FindByLayoutSuffix(roots.current(), kExitSuffix, 0);
            if (exitBtn != 0) { parent = exitBtn->getParent(); break; }
        }
    }
    if (parent == 0 || parent != g_uiMenuParent) { UiMenuForget("the UI shut itself off and the column it hid is not live"); return; }
    UiMenuRestore(gui, parent, "the UI shut itself off after a fault");
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiMenuLatchPutBackGuarded()
{
    __try { UiMenuLatchPutBackInner(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return 0;
}
static void UiLatchPutBack()
{
    ::InterlockedExchange(&g_panelWanted, 0);     /* ESC is the game's again: UiCloseFromEngine answers 0 */
    ::InterlockedExchange(&g_panelBuiltOk, 0);
    /* ui6: and for a live box - its strips go (no tick will), so Kenshi's menu takes clicks again. The box itself stays, as
       before ui6: its buttons record a press nothing reads any more. */
    ::InterlockedExchange(&g_boxEscPending, 0);
    if (UiBoxBlockLatchDropGuarded() != 0)
    {
        UiBoxBlockForget();
        ErrorLog("[UI] ui6: removing the box's click-blocking strips after the UI shut itself off faulted as well: given up,"
                 " not retried - Kenshi's title menu may stay unclickable around the box until this title screen is rebuilt.");
    }
    if (g_uiMenuHeld == 0) return;
    if (UiMenuLatchPutBackGuarded() != 0)
    {
        UiMenuClear();
        ErrorLog("[UI] TITLE MENU put-back after the UI shut itself off faulted as well: given up, not retried - the title"
                 " column may stay hidden until this title screen is rebuilt (a game load and back) or the game restarts.");
    }
}

/* ui98 (owner decision 98 = A, 2026-09-28) - THE REFUSED BUTTON'S HOVER NOTE. On a refused game build the title button reads
   MULTIPLAYER, greyed and unclickable, and while the mouse is over it a small note says why, in the owner's exact words.
   HOW THE HOVER IS SEEN - not through MyGUI's ToolTipManager / eventToolTip. MyGUI does not pick a DISABLED widget (Widget::
   getLayerItemByPoint answers nothing for it - Inferred from MyGUI 3.2's source; the pick falls through to the title art), so no
   mouse-focus or tooltip event ever reaches the button; and Kenshi draws its own tooltips in game code (its layouts carry only
   empty "...Tooltip" catcher widgets, e.g. Kenshi_MainPanel.layout LifeBar1Tooltip - Read), which nothing of ours can call.
   So the refusal pump (it runs this tick every frame - coop.cpp) reads MyGUI's own pointer position (InputManager::
   getMousePosition) and tests it against the button's on-screen rectangle. The note is up exactly while that holds and
   nothing else is on top there: no modal window, and LayerManager::getWidgetFromPoint - the pick MyGUI's own mouse move
   makes - finds nothing, the button, or the title art under it.
   HOW IT LOOKS - Kenshi's own tooltip look is not reachable (above), so the mod's own notice look: the connection-problem
   strip's recipe (UiQuietBody: a Kenshi_FloatingPanelThinSkin panel, one Kenshi_GenericTextBoxFlat line in the message
   box's text colour), sized to its MEASURED text (TextBox::getTextSize, virtual - no import) plus a small inset, just
   below-right of the pointer and kept inside the title art. A child of the title art on its own layer node (MkOver), so it
   goes with the title screen and draws over the menu's captions; it takes no mouse, so it never covers what it describes.
   Built when the hover starts, destroyed when it ends. */
static const char* const kRefuseTipName     = "CoopRefusedTip";       /* 14 characters: no heap temporary per lookup */
static const char* const kRefuseTipTextName = "CoopRefusedTipText";
static const char* const kRefuseTipText     = "Multiplayer doesn't support this version of Kenshi.";   /* owner 98: EXACTLY these words */
static const int kRefuseTipPadX = 10, kRefuseTipPadY = 6;   /* the inset around the text */
static const int kRefuseTipDx = 18, kRefuseTipDy = 22;      /* below-right of the pointer, clear of its arrow */
static int  g_refuseTipUp    = 0;       /* 1 while the note is (believed) on screen - it is looked up only then */
static int  g_refuseTipFails = 0;       /* builds failed in a row; at 3 it is given up for the run (no per-frame rebuild churn) */
static volatile LONG64 g_uiRefuseTipShows = 0;   /* times the note was put up - the ui[] readout's refusedNotes= */
static bool g_refuseTipLogged = false;
static int  g_refuseTipForced = 0;       /* TEST LEVER (KENSHI_COOP_TEST_SHOW_REFUSAL_NOTE=1): 1 = the note stays up, placed for a pointer at the button centre */
static bool g_refuseTipForcedRead = false;   /* the variable is read once, in UiCreateButton's refused branch, and nowhere else */
static int  g_rectRefuseTip[4] = { -1, -1, -1, -1 };

static bool UiRefusedHovered(MyGUI::Widget* btn, MyGUI::IntPoint* at)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im == 0 || btn->getParent() == 0 || !btn->getInheritedVisible() || im->isModalAny()) return false;
    const MyGUI::IntPoint& m = im->getMousePosition();
    const MyGUI::IntCoord c = btn->getAbsoluteCoord();
    if (m.left < c.left || m.top < c.top || m.left >= c.left + c.width || m.top >= c.top + c.height) return false;
    MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
    if (lm != 0)
    {
        MyGUI::Widget* top = lm->getWidgetFromPoint(m.left, m.top);
        if (top != 0 && top != btn && top != btn->getParent()) return false;   /* something else is on top there */
    }
    *at = m;
    return true;
}
static void UiRefusedTipDrop(MyGUI::Gui* gui)
{
    if (g_refuseTipUp == 0) return;
    g_refuseTipUp = 0;
    MyGUI::Widget* tip = gui->findWidgetT(std::string(kRefuseTipName), false);
    if (tip != 0) gui->destroyWidget(tip);
    UiRectForget(g_rectRefuseTip);
}
static MyGUI::Widget* UiRefusedTipBuild(MyGUI::Gui* gui, MyGUI::Widget* parent)
{
    const UiNames& n = NM();
    const int W = parent->getWidth() > 200 ? parent->getWidth() : 200;   /* wide enough that the line is never clipped while measured */
    MyGUI::Widget* tip = MkOver(parent, n.typeWidget, std::string("Kenshi_FloatingPanelThinSkin"), 0, 0, W, 60, std::string(kRefuseTipName));
    if (tip == 0) return 0;
    tip->setNeedMouseFocus(false);
    MyGUI::Widget* tw = Mk(tip, n.typeText, n.skinFlat, kRefuseTipPadX, kRefuseTipPadY, W - 2 * kRefuseTipPadX - 16, 30, std::string(kRefuseTipTextName));
    MyGUI::TextBox* tb = (tw != 0) ? tw->castType<MyGUI::TextBox>(false) : 0;
    if (tb == 0) { gui->destroyWidget(tip); return 0; }
    tw->setNeedMouseFocus(false);
    tb->setTextAlign(MyGUI::Align::Center);
    tb->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    tb->setCaption(MyGUI::UString(kRefuseTipText));
    const MyGUI::IntSize ts = tb->getTextSize();
    if (ts.width <= 0 || ts.height <= 0) { gui->destroyWidget(tip); return 0; }
    /* the skin's frame, if it has a client area: the panel is the text + inset + that frame, nothing more */
    MyGUI::Widget* client = tip->getClientWidget();
    const int fw = (client != 0) ? tip->getWidth() - client->getWidth() : 0;
    const int fh = (client != 0) ? tip->getHeight() - client->getHeight() : 0;
    tip->setSize(MyGUI::IntSize(ts.width + 2 * kRefuseTipPadX + fw, ts.height + 2 * kRefuseTipPadY + fh));
    tw->setCoord(MyGUI::IntCoord(kRefuseTipPadX, kRefuseTipPadY, ts.width, ts.height));
    return tip;
}
/* ui98 TEST LEVER (T516, F010: the game does not see the harness's SetCursorPos, so the note could never be hovered
   up for a screenshot). With KENSHI_COOP_TEST_SHOW_REFUSAL_NOTE=1 the note is placed as if the pointer sat at the
   button's centre and it stays up while the button is on screen. Without it this is exactly UiRefusedHovered. */
static bool UiRefusedPointer(MyGUI::Widget* btn, MyGUI::IntPoint* at)
{
    if (g_refuseTipForced == 0) return UiRefusedHovered(btn, at);
    if (btn->getParent() == 0 || !btn->getInheritedVisible()) return false;
    const MyGUI::IntCoord c = btn->getAbsoluteCoord();
    *at = MyGUI::IntPoint(c.left + c.width / 2, c.top + c.height / 2);
    return true;
}
/* Every refusal-pump tick (UiTitleTickInner, AddrOk() == 0 only): up while hovered, down otherwise. */
static void UiRefusedTipTick(MyGUI::Gui* gui, MyGUI::Widget* btn)
{
    MyGUI::IntPoint m;
    if (btn == 0 || g_refuseTipFails >= 3 || !UiRefusedPointer(btn, &m)) { UiRefusedTipDrop(gui); return; }
    MyGUI::Widget* parent = btn->getParent();
    MyGUI::Widget* tip = (g_refuseTipUp != 0) ? gui->findWidgetT(std::string(kRefuseTipName), false) : 0;
    int built = 0;
    if (tip == 0)
    {
        g_refuseTipUp = 0;
        tip = UiRefusedTipBuild(gui, parent);
        if (tip == 0)
        {
            if (++g_refuseTipFails == 3)
                ErrorLog("[UI] ui98: the refusal hover note could not be built 3 times in a row - given up for this run;"
                         " the greyed MULTIPLAYER button stays.");
            return;
        }
        g_refuseTipFails = 0;
        g_refuseTipUp = 1;
        built = 1;
        ::InterlockedIncrement64(&g_uiRefuseTipShows);
    }
    const MyGUI::IntCoord pc = parent->getAbsoluteCoord();
    int x = m.left - pc.left + kRefuseTipDx;
    int y = m.top - pc.top + kRefuseTipDy;
    if (x + tip->getWidth() > parent->getWidth()) x = parent->getWidth() - tip->getWidth();
    if (y + tip->getHeight() > parent->getHeight()) y = m.top - pc.top - tip->getHeight() - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (tip->getLeft() != x || tip->getTop() != y) tip->setPosition(MyGUI::IntPoint(x, y));
    if (built != 0)
    {
        UiRectForget(g_rectRefuseTip);
        UiLogRect("refusednote", tip, g_rectRefuseTip);   /* harness screenshots: where it went up, once per showing */
        if (!g_refuseTipLogged)
        {
            g_refuseTipLogged = true;
            DebugLog(std::string("[UI] the refusal hover note is showing beside the greyed MULTIPLAYER button: '") + kRefuseTipText + "'");
        }
    }
}

static void UiCreateButton(MyGUI::Widget* parent, MyGUI::Widget* exitBtn)
{
    // U3 (user decision 2026-09-22) - RIGHT AFTER NEW GAME, AND THE FIVE BELOW IT MOVE DOWN ONE ROW.
    // The column reads CONTINUE, NEW GAME, MULTIPLAYER, LOAD GAME, IMPORT GAME, OPTIONS, CREDITS, EXIT.  Ours
    // takes the row LOAD GAME used to occupy (NEW GAME's top + one row pitch), and LOAD GAME, IMPORT GAME,
    // OPTIONS, CREDITS and EXIT each move one row down (UiApplyOrder).  EXIT lands on y 0.916667..0.980556
    // real - exactly the row P8a gave our button below EXIT - so against EVERY widget in
    // data/gui/layout/Kenshi_MainMenu.layout the longer column overlaps nothing P8a's did not already clear:
    //   the column         x 0.2604..0.4167, y 0.138889..0.980556
    //   CreditsPanel       x 0.5203..0.8844                   - no x overlap
    //   VersionText        x 0.6047..0.9984                   - no x overlap
    //   the ImageBox       0 0 1 1                            - our PARENT, not a sibling
    //
    // THE FALLBACK IS P8a's PLACEMENT, unchanged: if any of the six buttons is not found (or not a child of
    // this parent), or the longer column would run off the parent's bottom, ours goes ONE ROW BELOW EXIT and
    // nothing of the game's is moved; buttonOrderFallback counts it and one line says which.  Below that,
    // P8a's own last resort - left of the column, else not created and noRoomTicks.
    //
    // P8a (review-p7z M-2) - WHY NOT TO THE RIGHT: P7z put us to the RIGHT of EXIT, which sat ON TOP of the
    // hidden CreditsPanel (x 0.520312..0.884374) once CREDITS was pressed, drawn over its text and keeping its
    // own mouse pick there.  A UI affordance that behaves differently from what the screen shows is a defect
    // in this project.  The row position is taken from the game's live buttons at run time (see below).
    //
    // DERIVED FROM THE LIVE WIDGETS, never from the layout file: left/width/height from the EXIT button, the
    // row from NEW GAME's live top, and the row pitch from the live gap CONTINUE -> NEW GAME (UiLivePitch, U3-b).
    // U3-b (review-u3 LOW 1): a fallback first PUTS BACK a column that is already moved (a rebuild of an unwired
    // button over an ordered column), and only then reads EXIT - "below EXIT" means below EXIT's own row.
    const int parentH = parent->getHeight();
    MyGUI::Widget* col[6] = { 0, 0, 0, 0, 0, 0 };
    std::string why;
    std::string whyPitch;
    const int rowPitch = UiLivePitch(parent, whyPitch);
    int ordered = 0;
    if (UiFindColumn(parent, col, why) != 0)
    {
        if (rowPitch <= 0) why = whyPitch;
        else if (UiColumnFits(parent, col, rowPitch, why) != 0) ordered = 1;
    }
    if (ordered == 0) UiPutBack(parent);
    const MyGUI::IntCoord e = exitBtn->getCoord();
    MyGUI::IntCoord mine(e.left, e.top + rowPitch, e.width, e.height);
    if (ordered != 0)
    {
        mine = MyGUI::IntCoord(e.left, col[0]->getTop() + rowPitch, e.width, e.height);
    }
    else
    {
        UiOrderFallback(why);
        if (rowPitch <= 0 || mine.top + mine.height > parentH)
        {
            // No room below - a parent shorter than the layout implies.  LEFT of the column, which no widget in the
            // layout occupies either; and if THAT does not fit, do not create it at all and say so with a number.
            const int gap = (e.width / 8) + 1;
            mine = MyGUI::IntCoord(e.left - e.width - gap, e.top, e.width, e.height);
            if (mine.left < 0) { ::InterlockedIncrement64(&g_uiNoRoom); return; }
        }
    }

    MyGUI::Button* b = parent->createWidget<MyGUI::Button>(kSkin, mine, MyGUI::Align::Default, kOurName);
    if (b == 0) { ::InterlockedIncrement64(&g_uiCreateNull); return; }

    /* U3: the game's five move now, before either caption branch - the refusal caption sits in the same row.
       A throw in here leaves the wired flag down, the next tick rebuilds ours, and the move (idempotent by
       construction) touches only what is still out of place. */
    g_uiOrderMode = 2;
    if (ordered != 0)
    {
        UiRememberOriginals(parent, col);
        UiApplyOrder(b, col, rowPitch, kUiBookMoved);
        g_uiPutPitch = rowPitch;
        g_uiOrderMode = 1;
        UiOrderNowOrdered();
    }
    g_uiOrderParentW = parent->getWidth();
    g_uiOrderParentH = parentH;
    g_uiOrderCheckMs = ::GetTickCount();
    g_uiOrderSettleSinceMs = 0;

    /* P8h - THE REFUSAL IS VISIBLE ON SCREEN, not only in a log file nobody opens. When the address gate said
       this executable is not the one every RVA was read from, the button that would start a session says so
       instead, and it does not accept clicks: an affordance that does nothing when pressed is a defect in this
       project, so the handler is not attached at all rather than attached and silently ignoring the press. */
    if (coop::AddrOk() == 0)
    {
        /* ui98 (owner decision 98 = A): it reads MULTIPLAYER, as the enabled button and the game's own title buttons do, and
           is greyed by the skin's own "disabled" state (Kenshi_Button1Skin, data/gui/skins/kenshi_skins.xml: the same plate,
           the caption drawn dark - Read) - no colour set by hand. Why it is greyed is the hover note (UiRefusedTipTick). */
        b->setCaption(kCaption);
        b->setFontName(kFont);
        b->setEnabled(false);
        const LONG64 made = ::InterlockedIncrement64(&g_uiCreated);
        ::InterlockedIncrement64(&g_uiRefused);
        /* ui98 TEST LEVER - the only place the process environment is read. Exactly "1" turns it on; unset or any
           other value leaves today's hover-only note. Read once per run, never stored anywhere. */
        if (!g_refuseTipForcedRead)
        {
            g_refuseTipForcedRead = true;
            char lv[4] = { 0, 0, 0, 0 };
            const DWORD ln = ::GetEnvironmentVariableA("KENSHI_COOP_TEST_SHOW_REFUSAL_NOTE", lv, (DWORD)sizeof(lv));
            if (ln == 1 && lv[0] == '1')
            {
                g_refuseTipForced = 1;
                DebugLog(std::string("[UI] TEST LEVER: the refusal note is forced visible (KENSHI_COOP_TEST_SHOW_REFUSAL_NOTE)"));
            }
        }
        /* P8h-b (review-p8h H-1) - THE REFUSAL BUTTON IS FINISHED, SO THE FLAG GOES UP ON THIS PATH TOO.
           It used to be raised only on the healthy path below, so UiTitleTickInner found a button with the
           flag down and did what that means everywhere else: destroyed it and built it again - on EVERY
           title tick, ~1 kHz, each one carrying the full widget-tree walk that finds EXIT.  Every rebuild is
           another chance for a MyGUI throw, and three of those latch g_uiDisabled and take the refusal
           caption off the screen entirely, which is the one thing a refused build must not lose.  The flag
           means "creation finished"; this button is unwired deliberately and is still finished. */
        ::InterlockedExchange(&g_uiBtnWired, 1);
        if (!g_uiFirstCreateLogged)
        {
            g_uiFirstCreateLogged = true;
            DebugLog(std::string("[UI] the MULTIPLAYER button is showing '") + kCaption
                     + "' and is DISABLED (greyed, not clickable; hovering it shows the note '" + kRefuseTipText
                     + "'): the address gate refused this executable (" + coop::AddrRefusalCaption()
                     + "), so no part of this mod is installed. See the [ADDR] line at plugin start for the fingerprint and the reason.");
            /* ui98: the button's rectangle, in the same form as the healthy path's '[UI] created ... ours=' line, so the
               harness-side 'hover multiplayer' verb can find it on a refused build too. */
            std::stringstream rs;
            rs.imbue(std::locale::classic());
            rs << "[UI] created " << kOurName << " (refused build) parentSize=(" << parent->getWidth() << "," << parentH << ")"
               << " ours=(" << mine.left << "," << mine.top << "," << mine.width << "," << mine.height << ")";
            DebugLog(rs.str());
        }
        /* THE CHURN GUARD, and it is here because the fix above cannot be tested offline: the decision reads
           a live MyGUI widget tree, so nothing about it is a pure function and no test in coop_test.exe can
           reach it.  A RUN can.  A refused game creates this button ONCE; a second, third or fourth creation
           is the rebuild loop back again.  Logged once, with the count, and never again. */
        if (made > 3 && !g_uiRefuseChurnLogged)
        {
            g_uiRefuseChurnLogged = true;
            std::stringstream cs;
            cs.imbue(std::locale::classic());
            cs << "[UI] REBUILD CHURN: the refusal caption has now been created " << (long long)made
               << " times and a refused game should create it once - the title tick is destroying and"
                  " rebuilding it (review-p8h H-1). Read created= and refusedCaption= in the ui[] readout.";
            DebugLog(cs.str());
        }
        return;
    }

    b->setCaption(kCaption);
    b->setFontName(kFont);
    b->eventMouseButtonClick += MyGUI::newDelegate(OnMultiplayerClicked);
    ::InterlockedIncrement64(&g_uiCreated);
    /* P8i: EXISTS IMPLIES WIRED - this is the last step, after the attach, so a throw at the attach leaves
       the flag down and the next tick destroys the dead button instead of returning early on it. */
    ::InterlockedExchange(&g_uiBtnWired, 1);

    if (!g_uiFirstCreateLogged)
    {
        g_uiFirstCreateLogged = true;
        // The evidence line, and it prints its INPUTS: the anchor's full runtime name is what proves
        // the layout prefix is real, and both coords are what a screenshot is checked against.
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        // P8a (review-p7z M-3): THE PARENT SLOT WAS EMPTY BY CONSTRUCTION.  The title art ImageBox is declared in
        // the layout with NO name attribute at all, and MyGUI's loader prefixes only non-empty names, so its
        // runtime name is the empty string: the line read "under parent  exit=(...)" and a reader could not tell
        // "this widget has no name" from "getName() came back wrong", which is half of what this build tests.
        // Its TYPE NAME, child count and pointer are all facts the layout cannot make empty.
        ss << "[UI] created " << kOurName << " beside " << exitBtn->getName()
           << " under parent type=" << parent->getTypeName()
           << " children=" << (long long)parent->getChildCount()
           << " ptr=" << (const void*)parent
           << " parentSize=(" << parent->getWidth() << "," << parentH << ")"
           << " rowPitch=" << rowPitch
           << " exit=(" << e.left << "," << e.top << "," << e.width << "," << e.height << ")"
           << " ours=(" << mine.left << "," << mine.top << "," << mine.width << "," << mine.height << ")"
           << " placement=" << (ordered != 0 ? "afterNewGame" : "belowExit")
           << " buttonsMoved=" << (long long)g_uiBtnsMoved
           << " threadId=" << (long long)::GetCurrentThreadId();
        DebugLog(ss.str());
    }
}

// noinline: this body owns C++ temporaries, and it must never be folded into the __try frame below.
static __declspec(noinline) void UiTitleTickInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) { ::InterlockedIncrement64(&g_uiNoGui); UiMenuForget("the title screen is gone"); UiBoxBlockForget(); return; }   /* ui5c: no GUI, no title - touch nothing; ui6: no box is live */
    const UiNames& n = NM();

    /* P8i-b: the ESC gate's evidence line is printed HERE, on the title pump and inside the fault
       guard, and not in the hook - the engine may dispatch keys from a thread we have not identified,
       and a log call is not the way to find that out.  The drain is bounded so a counter that ran away
       could never turn this into the 1 kHz log the fix exists to prevent. */
    {
        int drain = 0;
        while (g_panelEngineLogged < (long long)g_panelEngineClosed && drain < 8)
        {
            ++g_panelEngineLogged;
            ++drain;
            if (g_panelEscLogged < (long long)g_panelEscClosed)
            {
                ++g_panelEscLogged;
                DebugLog("ui: esc closes panel");
            }
            else
            {
                DebugLog("ui: the title screen closed the panel with its own sub-panels");
            }
        }
    }

    /* ONE action per tick, taken off the click handlers' single slot.  Two clicks between two ticks lose
       the first, and actionsQueued != actionsRun is exactly that measurement - at ~1 kHz it should never
       happen, and if it does the number says so rather than the panel behaving oddly. */
    const LONG act = ::InterlockedExchange(&g_uiAction, (LONG)kActNone);

    MyGUI::Widget* btn = gui->findWidgetT(n.ourBtn, false);
    if (btn == 0)
    {
        if (::InterlockedCompareExchange(&g_uiBtnWired, 0, 0) != 0) ::InterlockedExchange(&g_uiBtnWired, 0);
        g_uiOrigHeld = 0;   /* U3-b: our button is gone - a new title instance's buttons are not these originals */
        g_uiOrderLoggedMode = 1;   /* U3-c (re-check-u3b LOW): a new title starts in the expected mode, so its first ordered layout is not logged as a RESTORE and its first fallback is logged */
    }
    else if (::InterlockedCompareExchange(&g_uiBtnWired, 0, 0) == 0)
    {
        /* review-p8a M-5 / F670: drawn, pickable, and with no click handler.  Under P8i it is also the
           only way to open the panel.  Destroy it and build it again below. */
        ::InterlockedIncrement64(&g_uiBtnUnwired);
        gui->destroyWidget(btn);
        btn = 0;
    }

    if (coop::AddrOk() == 0) UiRefusedTipTick(gui, btn);   /* ui98: the refused button's hover note - up while hovered, down otherwise */

    /* U2: the notebook's exit code is the NEGATIVE proof of design 2.2, and it is one Win32 call over a
       handle we own.  It runs before the action so a button pressed on this frame reads a fresh answer. */
    PanelPollNotebook();

    /* The panel is only looked for when there could be one - "closed and never built" is the common case
       and costs exactly the one lookup P7z already did. */
    MyGUI::Widget* panel = 0;
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) != 0
     || ::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0)
    {
        panel = gui->findWidgetT(n.panel, false);
        /* ui5c fold (F2) - THE FLAG FOLLOWS THE WIDGET.  A title screen destroyed with the panel on it (a game load
           while it is open) takes CoopPanel with it and PanelDestroy never runs, so g_panelBuiltOk stayed 1: the next
           title hid its column for a panel that was not there, and UiCloseFromEngine answered "closed" for it and
           swallowed ESC.  No panel widget with the flag up means no panel Window is up - the ESC gate's contract - so
           the flag comes down here, before anything below reads it or builds. */
        if (panel == 0 && ::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0)
        {
            ::InterlockedExchange(&g_panelBuiltOk, 0);
            g_panelMismatchSinceMs = 0;   /* as PanelDestroy: the next panel starts its own settle clock */
            DebugLog("[UI] the MULTIPLAYER panel's widget is gone (its title screen was destroyed under it) - marked not built");
        }
    }

    /* Same rule for the notice: looked for only when there could be one, and it can outlive the panel by
       design - it exists precisely for the frames on which there is no panel.  Only the LOOKUP happens
       here; whether it stays is decided below, AFTER the action has run, because the action is what
       takes it down (opening the panel is the player asking again) and deciding from a value read before
       the action would leave a withdrawn notice on screen for one more frame. */
    MyGUI::Widget* notice = 0;
    if (::InterlockedCompareExchange(&g_noticeWanted, 0, 0) != 0 || g_noticeShown >= 0)
        notice = gui->findWidgetT(n.notice, false);
    MyGUI::Widget* idbox = 0;   /* PP3d: looked for only when there could be one */
    if (::InterlockedCompareExchange(&g_idBoxWanted, 0, 0) != 0 || g_idBoxShownKind != 0)
        idbox = gui->findWidgetT(kIdBoxName, false);

    if (panel != 0) PanelMirror(gui, act != (LONG)kActNone ? 1 : 0);
    if (act != (LONG)kActNone)
    {
        ::InterlockedIncrement64(&g_uiActionsRun);
        PanelApply(gui, (int)act, panel != 0 ? 1 : 0);
    }
    if (panel != 0 && g_profPushWanted != 0) { g_profPushWanted = 0; PanelPush(gui); }   /* prof3: a PROFILES answer came in (UiProfilesPoll) */

    const LONG wanted = ::InterlockedCompareExchange(&g_panelWanted, 0, 0);
    if (panel != 0 && (wanted == 0 || ::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) == 0))
    {
        PanelDestroy(gui, panel);
        panel = 0;
    }
    if (panel != 0) UiLogRect("coop-panel", panel, g_rectPanel); else UiRectForget(g_rectPanel);   /* ui2: harness screenshots (the panel is found the tick after it is built) */

    /* ui6 (decision 42): an ESC the engine's key handler answered for the live box (UiBoxEscFromEngine) is that box's button,
       pressed here on the title pump before the destroys below read their flags: the identity box's first button (CANCEL on
       MULTIPLAYER DATA DAMAGED, OK on CAN'T START MULTIPLAYER), the error box's OK. */
    {
        const LONG esc = ::InterlockedExchange(&g_boxEscPending, 0);
        if (esc == 2 && ::InterlockedCompareExchange(&g_idBoxWanted, 0, 0) != 0)
        {
            ::InterlockedExchange(&g_idBoxButton, (LONG)coopdata::kIdBoxDismiss);
            DebugLog("[UI] ui6: ESC answered the identity box (its CANCEL / OK) - it did not reach Kenshi's quit");
        }
        else if (esc == 1 && ::InterlockedCompareExchange(&g_noticeWanted, 0, 0) != 0)
        {
            UiClearNotice();
            DebugLog("[UI] ui6: ESC answered the error box (its OK) - it did not reach Kenshi's quit");
        }
    }
    /* U2: the notice's own destroy, taken AFTER the action, from a fresh read - the same place and the
       same reason the panel's destroy is taken here. */
    const LONG noticeWanted = ::InterlockedCompareExchange(&g_noticeWanted, 0, 0);
    if (notice != 0 && (noticeWanted == 0 || g_noticeShown != g_noticeSeq))
    {
        NoticeDestroy(gui, notice);
        notice = 0;
    }
    if (notice != 0) UiLogRect("errorbox", notice, g_rectNotice);   /* ui1: harness screenshots */
    /* T-201 N1: the press in progress - done, failed (a box), or still waiting; a closed failure box returns to its screen. */
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0) PanelProfModeEnd("the panel closed");   /* T-201 PP6' fold: every close route passes here */
    if (PressTick() != 0 && panel != 0) PanelPush(gui);
    if (LoadTick() != 0 && panel != 0) PanelPush(gui);   /* T-201 PP6' */
    PressBoxReturnTick();
    /* ui3b (review MED): an Overlapped error box made after the panel is drawn above it but asked for clicks after it, so its OK
       could not be clicked over the panel. While the panel is open the box is hidden (the panel's status area carries the
       message); it shows again once the panel closes.  ui6: set in UiBoxBlockTick, at the end of the tick, with the strips. */
    /* PP3d: the identity box - a pressed button first, then its destroy from a fresh read, as the error box's. */
    {
        const LONG pressed = ::InterlockedExchange(&g_idBoxButton, 0);
        if (pressed != 0) IdBoxPressed((int)pressed);
    }
    const LONG idWanted = ::InterlockedCompareExchange(&g_idBoxWanted, 0, 0);
    if (idbox != 0 && (idWanted == 0 || (int)idWanted != g_idBoxShownKind)) { IdBoxDestroy(gui, idbox); idbox = 0; }
    if (idbox == 0 && idWanted == 0) g_idBoxShownKind = 0;   /* gone with the title screen: stop looking for it */
    if (idbox != 0) UiLogRect("identitybox", idbox, g_rectIdBox);   /* ui6: its visibility is set in UiBoxBlockTick */
    if (g_uiPrevPending != 0 && ((g_uiPrevKind == 1 && panel != 0) || (g_uiPrevKind == 2 && notice != 0) || (g_uiPrevKind == 5 && idbox != 0)))   /* uishot: TEST-ONLY uipreview; PP3d kind 5 */
    {
        if (g_uiPrevKind == 1) { UiRectForget(g_rectPanel); UiLogRect("coop-panel", panel, g_rectPanel); }   /* ui2's own rect line, said again */
        else if (g_uiPrevKind == 5) { UiRectForget(g_rectIdBox); UiLogRect("identitybox", idbox, g_rectIdBox); }
        else { UiRectForget(g_rectNotice); UiLogRect("errorbox", notice, g_rectNotice); }
        g_uiPrevPending = 0;
        DebugLog("[UI] uipreview " + g_uiPrevWhat + " SHOWN");
    }

    /* THE STEADY STATE, and the whole cost of this feature once everything is up.  U2 adds one term: a
       notice that is wanted and not yet on screen also has something to build, and the build needs the
       parent that only the walk below finds.  ui5c: the steady state also runs the title-menu pass
       (UiMenuColumnSync), which returns at once while the panel is closed and nothing is hidden, and walks
       the column only when the panel opens or closes and every kUiOrderCheckEveryMs while it is open. */
    /* U2-b - THE DEFECT OF 2026-09-05, ANSWERED HERE.  The player opened the panel, saw it laid out
       for a rectangle that was not the one on screen, closed it and opened it again, and the second one
       "visually looks perfect".  The geometry was always read live (design-ui-panel 1.3) - what was
       missing is that it was read live ONCE, at the instant of opening, and the parent's rectangle is
       not final at that instant.  So the rectangle is re-read on every tick and a panel that no longer
       matches its parent is taken down; the build below then lays it out again at the new one, within
       one tick, with no click from the player.
       getParent() is the imported accessor the button placement already uses, and getWidth/getHeight are
       inline in the header - so this costs no new import and no widget-tree walk. */
    if (panel != 0)
    {
        MyGUI::Widget* pp = panel->getParent();
        if (pp != 0)
        {
            const int laidW = (int)::InterlockedCompareExchange(&g_panelAtParentW, 0, 0);
            const int laidH = (int)::InterlockedCompareExchange(&g_panelAtParentH, 0, 0);
            const int nowW = pp->getWidth();
            const int nowH = pp->getHeight();
            if (nowW == laidW && nowH == laidH)
            {
                /* Back at the size it was laid out for - whatever was happening is over and nothing is
                   rebuilt.  A drag that ends where it began costs one comparison per tick and no
                   widget at all. */
                g_panelMismatchSinceMs = 0;
            }
            else
            {
                /* U2-c (review-u2 H-1) - DIFFERENT IS NOT ENOUGH; IT HAS TO HAVE STOPPED MOVING.
                   The size is read on EVERY tick, which is what makes this a recurring check and not a
                   one-shot delay, and the take-down is the confirmed exit.  GetTickCount is the same
                   clock the status throttle uses; the subtraction is unsigned, so its 49-day wrap costs
                   at worst one extra settle window and can never produce a negative age. */
                const DWORD now = ::GetTickCount();
                if (g_panelMismatchSinceMs == 0 || nowW != g_panelMismatchW || nowH != g_panelMismatchH)
                {
                    g_panelMismatchSinceMs = (now == 0) ? 1 : now;
                    g_panelMismatchW = nowW;
                    g_panelMismatchH = nowH;
                    ::InterlockedIncrement64(&g_panelResizeSettling);
                }
                else if ((DWORD)(now - g_panelMismatchSinceMs) >= (DWORD)coopui::kPanelResizeSettleMs)
                {
                    g_panelMismatchSinceMs = 0;
                    ::InterlockedIncrement64(&g_panelResized);
                    PanelDestroy(gui, panel);
                    panel = 0;
                }
                else
                {
                    ::InterlockedIncrement64(&g_panelResizeSettling);
                }
            }
        }
    }

    /* U3 - THE ORDER IS RE-CHECKED, CHEAPLY; U3-b (review-u3) - AND NEVER AGAINST A SIZE STILL MOVING.  Checked
       every kUiOrderCheckEveryMs, and on a parent-size change only once that size has held still for
       coopui::kPanelResizeSettleMs - the panel's settle rule (U2-c), read on every tick - never per tick, because
       it is seven suffix walks.  The pitch is the live CONTINUE -> NEW GAME gap, so a resize that moves no button
       changes nothing.  ORDERED and it still fits: UiApplyOrder moves only a button not one row below the one
       above it (buttonsReshifted at an unchanged pitch - the game moved it; buttonsRepitched when the pitch
       itself changed).  ORDERED and it no longer fits: the five go back to their REMEMBERED originals, ours is
       destroyed, and the build below re-decides - the fallback below EXIT.  FALLEN BACK and it fits again: ours
       moves in place to under NEW GAME and the order is re-applied (buttonOrderRestored) - nothing latches. */
    if (btn != 0 && (g_uiOrderMode == 1 || g_uiOrderMode == 2))
    {
        MyGUI::Widget* op = btn->getParent();
        const DWORD nowMs = ::GetTickCount();
        int due = 0;
        if (op != 0)
        {
            const int ow = op->getWidth();
            const int oh = op->getHeight();
            if (ow != g_uiOrderParentW || oh != g_uiOrderParentH)
            {
                if (g_uiOrderSettleSinceMs == 0 || ow != g_uiOrderSettleW || oh != g_uiOrderSettleH)
                {
                    g_uiOrderSettleSinceMs = (nowMs == 0) ? 1 : nowMs;
                    g_uiOrderSettleW = ow;
                    g_uiOrderSettleH = oh;
                }
                else if ((DWORD)(nowMs - g_uiOrderSettleSinceMs) >= (DWORD)coopui::kPanelResizeSettleMs)
                {
                    due = 1;
                }
            }
            else
            {
                g_uiOrderSettleSinceMs = 0;   // back at the size last checked: nothing is pending
                if ((DWORD)(nowMs - g_uiOrderCheckMs) >= kUiOrderCheckEveryMs) due = 1;
            }
        }
        if (due != 0)
        {
            g_uiOrderSettleSinceMs = 0;
            g_uiOrderCheckMs = nowMs;
            g_uiOrderParentW = op->getWidth();
            g_uiOrderParentH = op->getHeight();
            MyGUI::Widget* col[6] = { 0, 0, 0, 0, 0, 0 };
            std::string why;
            int pitch = 0;
            int fits = 0;
            if (UiFindColumn(op, col, why) != 0)
            {
                pitch = UiLivePitch(op, why);
                if (pitch > 0 && UiColumnFits(op, col, pitch, why) != 0) fits = 1;
            }
            if (g_uiOrderMode == 1 && fits != 0)
            {
                UiApplyOrder(btn, col, pitch, pitch == g_uiPutPitch ? kUiBookReshift : kUiBookRepitch);
                g_uiPutPitch = pitch;
            }
            else if (g_uiOrderMode == 1)
            {
                UiPutBack(op);
                g_uiOrderMode = 0;
                ::InterlockedExchange(&g_uiBtnWired, 0);
                gui->destroyWidget(btn);
                btn = 0;
            }
            else if (fits != 0)
            {
                UiRememberOriginals(op, col);
                btn->setPosition(MyGUI::IntPoint(col[5]->getLeft(), col[0]->getTop() + pitch));
                UiApplyOrder(btn, col, pitch, kUiBookMoved);
                g_uiPutPitch = pitch;
                g_uiOrderMode = 1;
                UiOrderNowOrdered();
            }
        }
    }
    if (btn == 0) g_uiOrderMode = 0;

    if (btn != 0 && (wanted == 0 || panel != 0) && (noticeWanted == 0 || notice != 0) && (idWanted == 0 || idbox != 0))   /* PP3d */
    {
        if (panel != 0) PanelPushStatus(gui, 0);
        UiMenuColumnSync(gui, btn->getParent(), panel != 0 ? 1 : 0, 0);   /* ui5c: the title is live on this return - hide / re-hide / restore here (fold F2: up = our panel widget found live this tick) */
        UiBoxRefitTick(gui, btn->getParent(), notice, idbox);   /* ui7 fold (review-ui7 #1): the ticks after a box's build take this return */
        UiBoxBlockTick(gui, btn->getParent(), panel, notice, idbox, 0);   /* ui6: a live box blocks the menu under it */
        return;
    }

    /* Below here something has to be built, and only here does the walk for EXIT run.  (ui5c's title-menu
       pass walks under the known parent on both paths, throttled - see UiMenuColumnSync.) */
    MyGUI::Widget* exitBtn = 0;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (roots.next())
    {
        exitBtn = FindByLayoutSuffix(roots.current(), kExitSuffix, 0);
        if (exitBtn != 0) break;
    }
    if (exitBtn == 0) { g_uiOrigHeld = 0; UiMenuForget("the title screen is gone"); UiBoxBlockGone(gui, "the title screen is gone"); ::InterlockedIncrement64(&g_uiNoExit); return; }   /* U3-b: title gone; ui5c: its buttons are not touched */

    // The parent is the title art ImageBox that holds all seven menu buttons - the sibling
    // relationship is what gives our button the same coordinate space and the same clipping.
    MyGUI::Widget* parent = exitBtn->getParent();
    if (parent == 0) { UiMenuForget("EXIT has no parent - the menu column cannot be found"); UiBoxBlockGone(gui, "the title art cannot be found"); ::InterlockedIncrement64(&g_uiNoParent); return; }   /* P8a (M-1): its OWN counter */

    /* ui5c fold (F6): a button made while the panel is up (the U3 fallback's destroy above, an unwired rebuild) is hidden
       by the menu pass on this same tick, not up to kUiOrderCheckEveryMs later. */
    int menuNow = 0;
    if (btn == 0) { UiCreateButton(parent, exitBtn); menuNow = 1; }
    if (wanted != 0 && panel == 0)
    {
        PanelBuild(gui, parent);
        /* ui5c fold (F2): the flag was down on entry (no panel widget lowers it above), so it is up now only if this
           build finished - and the pass is told "up" only when the widget is then found live. */
        if (::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0) panel = gui->findWidgetT(n.panel, false);
    }
    UiMenuColumnSync(gui, parent, panel != 0 ? 1 : 0, menuNow);   /* ui5c: after the build, so the tick that builds the panel also hides the column */
    if (panel != 0) PanelNameFocusTick(gui);   /* T-201 N1b */
    int boxNew = 0;   /* ui6: a box built on this tick has the strips cut around it on this tick */
    if (::InterlockedCompareExchange(&g_noticeWanted, 0, 0) != 0 && notice == 0)
    {
        NoticeBuild(gui, parent);
        notice = gui->findWidgetT(n.notice, false);
        if (notice != 0) boxNew = 1;
    }
    {
        const LONG idNow = ::InterlockedCompareExchange(&g_idBoxWanted, 0, 0);   /* PP3d */
        if (idNow != 0 && idbox == 0)
        {
            IdBoxBuild(gui, parent, (int)idNow);
            idbox = gui->findWidgetT(std::string(kIdBoxName), false);
            if (idbox != 0) boxNew = 1;
        }
    }
    /* ui7: a box's measured size re-read on the ticks after its build, before the strips are cut around it (UiBoxRefitTick). */
    UiBoxRefitTick(gui, parent, notice, idbox);
    UiBoxBlockTick(gui, parent, panel, notice, idbox, boxNew);   /* ui6: a live box blocks the menu under it */
}

/* P8a (review-p7z H-1) - THE FILTER IS A DECISION, NOT A CONSTANT.  EXCEPTION_EXECUTE_HANDLER is a literal, so
   the old handler ran for a wild-pointer read AND for a C++ throw, and wrote the same sentence for both.  MyGUI
   throws for ordinary recoverable conditions - `CMultiDelegate::operator+=` throws "Trying to add same delegate
   twice", `createWidgetT` throws when the skin or widget type is not registered at that instant - and the
   plugin's own import table proves the throw machinery is linked in.  One of those latched the feature off for
   the rest of the process, on the build whose entire purpose is to produce this datum. */
static int UiFilter(unsigned long code)
{
    ::InterlockedExchange(&g_uiLastCode, (LONG)code);
    return EXCEPTION_EXECUTE_HANDLER;
}
/* The memory-fault class, and only it, latches.  0xC0000005 access violation, 0xC0000006 in-page error,
   0xC00000FD stack overflow, 0x80000001 guard page, 0x80000002 datatype misalignment. */
static int UiIsMemoryFault(unsigned long code)
{
    return (code == 0xC0000005ul || code == 0xC0000006ul || code == 0xC00000FDul
         || code == 0x80000001ul || code == 0x80000002ul) ? 1 : 0;
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiTitleTickGuarded()
{
    __try { UiTitleTickInner(); }
    __except (UiFilter(GetExceptionCode())) { return 1; }
    return 0;
}

/* ==================================================================================================================
   mmo5 (e47-mmo-design.md 6, 7 item 5) - THE HOST-LEFT WINDOW, drawn INSIDE A RUNNING WORLD (not at the title). The
   panel's recipe: createWidgetT by type string, the non-throwing castType<T>(false), every result null-checked, and every
   MyGUI call behind an SEH frame that holds no C++ object (C2712).
   ui1 (ui-polish-audit 3.1; owner rulings 2026-09-27) - REBUILT IN KENSHI'S MESSAGE-BOX STYLE, by hand after
   Kenshi_MessageBox.layout (Read): ONE ROOT window on the "Info" layer (core_layer.xml lists it above Main / Window / Popup,
   where the pause menu and inventory windows draw - Read; that a later layer draws on top is Inferred from MyGUI's rule).
   NO BACKDROP (owner 2026-09-27): Kenshi's own message box does not dim the screen, and a separate backdrop root on an
   overlapped layer could be raised over the window by one click (the ui1 review's stuck pause). The game stays paused and
   Escape's pause menu stays usable as a second way out. The window: Kenshi_WindowC (a title bar, NO close X), not movable,
   centred across, its top at 14% of the screen height (ui1c, owner 2026-09-27 from the T457 screenshot: in the top third,
   under the game's PAUSED banner, clear of the pause menu, which opens in the middle), ~26% x 20% of the screen; a
   Kenshi_WordWrapEmpty body, centred, in the message box's text colour 0.871 0.845 0.810; one Kenshi_Button2 at the bottom
   centre, ALWAYS present (greyed while the leave save runs, so the box never changes shape; ui1c: its caption is set in the
   body's colour when enabled and a dim 0.50 0.48 0.46 when not - the skin's own caption colour drew it dim grey even when
   enabled, T457). A widget made on a layer MyGUI does not know comes back attached to NO layer (MyGUI logs and draws nothing): then
   it is made again on "Popup" (Kenshi_MessagePanel.layout's layer); UiHostLeftLayer says which.
   NOT established: that it is drawn above the pause menu in play - the hands-on list.
   ================================================================================================================== */
static const char* const kHlWinName    = "CoopHostLeftWindow";   /* no '_' in any of our names (RE_Kenshi's suffix match) */
static const char* const kHlDimName    = "CoopHostLeftDim";   /* ui1: no longer built (owner: no backdrop); the close still sweeps the name */
static const char* const kHlLineName   = "CoopHostLeftLine";
static const char* const kHlBtnName    = "CoopHostLeftExit";
static const char* const kHlLayer      = "Info";
static const char* const kHlLayerAlt   = "Popup";
static const char* g_hlLayerUsed = "none";
static volatile LONG g_hlExitClickedFlag = 0;
static void OnCoopHostLeftExitClicked(MyGUI::Widget* /*sender*/) { ::InterlockedExchange(&g_hlExitClickedFlag, 1); }

static void UiHostLeftDrop(MyGUI::Gui* gui, MyGUI::Widget* dim, MyGUI::Widget* win)
{
    if (win != 0) gui->destroyWidget(win);
    if (dim != 0) gui->destroyWidget(dim);
}
/* One ROOT window on one layer. 1 built, 0 refused, 2 the layer is unknown; whatever was made is handed back to be dropped. */
static int UiHostLeftMake(MyGUI::Gui* gui, const char* layer, int vw, int vh, MyGUI::Widget** winOut)
{
    const UiNames& n = NM();
    int w = (vw * 26) / 100; if (w < 420) w = 420; if (w > vw - 20) w = vw - 20;
    int h = (vh * 20) / 100; if (h < 190) h = 190; if (h > vh - 20) h = vh - 20;
    if (w < 200 || h < 120) return 0;
    int y = (vh * 14) / 100; if (y > vh - h) y = vh - h; if (y < 0) y = 0;   /* ui1c (owner 2026-09-27): the top third, under PAUSED */
    MyGUI::Widget* raw = gui->createWidgetT(n.typeWindow, std::string("Kenshi_WindowC"), MyGUI::IntCoord((vw - w) / 2, y, w, h),
                                            MyGUI::Align::Default, std::string(layer), std::string(kHlWinName));
    *winOut = raw;
    if (raw == 0) { ::InterlockedIncrement64(&g_uiCreateNull); return 0; }
    if (raw->getLayer() == 0) return 2;
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    if (win == 0) return 0;
    win->setMovable(false);
    MyGUI::Widget* c = win->getClientWidget();
    if (c == 0) return 0;
    const int W = c->getWidth(), H = c->getHeight();
    if (W < 120 || H < 60) return 0;
    int btnH = H / 4; if (btnH < 26) btnH = 26;
    int btnW = W / 3; if (btnW < 150) btnW = 150; if (btnW > W - 16) btnW = W - 16;
    MyGUI::Widget* l = Mk(c, n.typeEdit, n.skinWrap, 8, 6, W - 16, H - btnH - 18, kHlLineName);
    MyGUI::Widget* b = Mk(c, n.typeButton, n.skinBtn2, (W - btnW) / 2, H - btnH - 6, btnW, btnH, kHlBtnName);
    MyGUI::EditBox* le = (l != 0) ? l->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* bb = (b != 0) ? b->castType<MyGUI::Button>(false) : 0;
    if (le == 0 || bb == 0) return 0;
    le->setEditStatic(true); le->setEditReadOnly(true); le->setEditMultiLine(true); le->setEditWordWrap(true);
    le->setTextAlign(MyGUI::Align::Center);
    le->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    bb->setCaption(MyGUI::UString("EXIT GAME"));
    bb->eventMouseButtonClick += MyGUI::newDelegate(OnCoopHostLeftExitClicked);
    return 1;
}
static int UiHostLeftBody(const char* title, const char* line, int buttonEnabled)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* raw = gui->findWidgetT(std::string(kHlWinName), false);
    if (raw == 0)
    {
        int vw = 1280, vh = 720;
        MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
        if (rm != 0) { const MyGUI::IntSize& vs = rm->getViewSize(); if (vs.width > 0 && vs.height > 0) { vw = vs.width; vh = vs.height; } }
        const char* layers[2] = { kHlLayer, kHlLayerAlt };
        int rc = 0;
        for (int k = 0; k < 2; ++k)
        {
            raw = 0;
            rc = UiHostLeftMake(gui, layers[k], vw, vh, &raw);
            if (rc == 1) { g_hlLayerUsed = layers[k]; break; }
            UiHostLeftDrop(gui, 0, raw); raw = 0;
            if (rc != 2) return 0;
        }
        if (rc != 1) return 0;
    }
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    MyGUI::Widget* lw = gui->findWidgetT(std::string(kHlLineName), false);
    MyGUI::Widget* bw = gui->findWidgetT(std::string(kHlBtnName), false);
    MyGUI::EditBox* le = (lw != 0) ? lw->castType<MyGUI::EditBox>(false) : 0;
    if (win == 0 || le == 0 || bw == 0) return 0;
    win->setCaption(MyGUI::UString(title));
    le->setCaption(MyGUI::UString(line));
    bw->setEnabled(buttonEnabled != 0);
    {   /* ui1c (owner 2026-09-27, T457): Kenshi_Button2's own caption colour is dim grey even when enabled - set it by hand.
           A caption colour set by hand is kept over the skin's per-state colours (MyGUI EditText mManualColour, Read). */
        MyGUI::Button* bb = bw->castType<MyGUI::Button>(false);
        if (bb != 0) bb->setTextColour(buttonEnabled != 0 ? MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f) : MyGUI::Colour(0.50f, 0.48f, 0.46f, 1.0f));
    }
    raw->setVisible(true);
    UiLogRect("hostleft", raw, g_rectHl);   /* ui1: harness screenshots */
    return 1;
}
static int UiHostLeftCatching(const char* title, const char* line, int buttonEnabled)
{
    try { return UiHostLeftBody(title, line, buttonEnabled); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiHostLeftGuarded(const char* title, const char* line, int buttonEnabled)
{
    __try { return UiHostLeftCatching(title, line, buttonEnabled); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiHostLeftShow(const char* title, const char* line, int buttonEnabled)
{
    return UiHostLeftGuarded(title != 0 ? title : "", line != 0 ? line : "", buttonEnabled);
}
int UiHostLeftExitClicked() { return ::InterlockedExchange(&g_hlExitClickedFlag, 0) != 0 ? 1 : 0; }
/* mmo5 fold: the host came back - the window goes, and a click still pending goes with it. */
static int UiHostLeftCloseBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* raw = gui->findWidgetT(std::string(kHlWinName), false);
    MyGUI::Widget* dim = gui->findWidgetT(std::string(kHlDimName), false);   /* ui1: the backdrop goes too */
    ::InterlockedExchange(&g_hlExitClickedFlag, 0);
    UiRectForget(g_rectHl);   /* ui1; ui1c: also when nothing was found (the game removed it) - the next build logs its rect */
    if (raw == 0 && dim == 0) return 0;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) im->resetKeyFocusWidget();
    UiHostLeftDrop(gui, dim, raw);
    return 1;
}
static int UiHostLeftCloseCatching()
{
    try { return UiHostLeftCloseBody(); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiHostLeftCloseGuarded()
{
    __try { return UiHostLeftCloseCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiHostLeftClose() { return UiHostLeftCloseGuarded(); }
const char* UiHostLeftLayer() { return g_hlLayerUsed; }   /* ui1 */

/* T-246 (owner 199 b / 200) - THE MODS DON'T MATCH NOTICE, drawn INSIDE A RUNNING WORLD once after a joiner whose mods differ
   from the world's was let in. The host-left window's recipe above (one ROOT Kenshi_WindowC on "Info", made again on "Popup"
   when Info is unknown; no close X; not movable; the body centred in the message box's colour) laid out like the title's error
   box (NoticeBoxH / NoticeLayoutIn, OK bottom-right, lit by hand). Titled by coopui::NoticeTitle (MODS DON'T MATCH). It does not
   pause and blocks nothing: play continues behind it. OK only records the click; store.cpp's ModsWarnTick closes it. */
static const char* const kMwWinName  = "CoopModsWarnWindow";   /* no '_' in any of our names (RE_Kenshi's suffix match) */
static const char* const kMwLineName = "CoopModsWarnLine";
static const char* const kMwBtnName  = "CoopModsWarnOk";
static volatile LONG g_mwOkClickedFlag = 0;
static void OnCoopModsWarnOkClicked(MyGUI::Widget* /*sender*/) { ::InterlockedExchange(&g_mwOkClickedFlag, 1); }
/* 1 built, 0 refused, 2 the layer is unknown; whatever was made is handed back to be dropped. */
static int UiModsWarnMake(MyGUI::Gui* gui, const char* layer, int vw, int vh, const std::string& text, MyGUI::Widget** winOut)
{
    const UiNames& n = NM();
    int w = (vw * 34) / 100; if (w < 520) w = 520; if (w > vw - 20) w = vw - 20;
    int h = coopui::NoticeBoxH(text, w); if (h < 190) h = 190; if (h > vh - 20) h = vh - 20;
    if (w < 200 || h < 120) return 0;
    int y = (vh * 14) / 100; if (y > vh - h) y = vh - h; if (y < 0) y = 0;   /* the host-left window's place: the top third */
    MyGUI::Widget* raw = gui->createWidgetT(n.typeWindow, std::string("Kenshi_WindowC"), MyGUI::IntCoord((vw - w) / 2, y, w, h),
                                            MyGUI::Align::Default, std::string(layer), std::string(kMwWinName));
    *winOut = raw;
    if (raw == 0) { ::InterlockedIncrement64(&g_uiCreateNull); return 0; }
    if (raw->getLayer() == 0) return 2;
    MyGUI::Window* win = raw->castType<MyGUI::Window>(false);
    if (win == 0) return 0;
    win->setMovable(false);
    win->setCaption(MyGUI::UString(coopui::NoticeTitle(text).c_str()));
    MyGUI::Widget* c = win->getClientWidget();
    if (c == 0) return 0;
    const int W = c->getWidth(), H = c->getHeight();
    if (W < 120 || H < 60) return 0;
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);   /* the title error box's layout: the text, one blank line, the button row */
    int btnW = W / 4; if (btnW < 120) btnW = 120; if (btnW > W - 16) btnW = W - 16;
    MyGUI::Widget* l = Mk(c, n.typeEdit, n.skinWrap, 8, lay.textY, W - 16, lay.textH, kMwLineName);
    MyGUI::Widget* b = Mk(c, n.typeButton, n.skinBtn2, W - btnW - 8, lay.btnY, btnW, lay.btnH, kMwBtnName);
    MyGUI::EditBox* le = (l != 0) ? l->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* bb = (b != 0) ? b->castType<MyGUI::Button>(false) : 0;
    if (le == 0 || bb == 0) return 0;
    le->setEditStatic(true); le->setEditReadOnly(true); le->setEditMultiLine(true); le->setEditWordWrap(true);
    le->setTextAlign(MyGUI::Align::Center);
    le->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    le->setCaption(MyGUI::UString(text.c_str()));
    bb->setCaption(MyGUI::UString("OK"));
    bb->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));   /* ui1c: Kenshi_Button2's own caption colour is dim - lit by hand */
    bb->eventMouseButtonClick += MyGUI::newDelegate(OnCoopModsWarnOkClicked);
    return 1;
}
static int UiModsWarnBody(const char* text)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    {   /* T-246 fold 2 (item 4): never reuse a window already there - a show happens only when the notice is not up, so one found
           here is stale (a close that failed, or a root left half-built by a throw): dropped, and built again with this text */
        MyGUI::Widget* old = gui->findWidgetT(std::string(kMwWinName), false);
        if (old != 0) UiHostLeftDrop(gui, 0, old);
    }
    int vw = 1280, vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& vs = rm->getViewSize(); if (vs.width > 0 && vs.height > 0) { vw = vs.width; vh = vs.height; } }
    const std::string t(text);
    const char* layers[2] = { kHlLayer, kHlLayerAlt };
    for (int k = 0; k < 2; ++k)
    {
        MyGUI::Widget* raw = 0;
        const int rc = UiModsWarnMake(gui, layers[k], vw, vh, t, &raw);
        if (rc == 1) { ::InterlockedExchange(&g_mwOkClickedFlag, 0); raw->setVisible(true); return 1; }
        UiHostLeftDrop(gui, 0, raw);
        if (rc != 2) return 0;
    }
    return 0;
}
static int UiModsWarnCatching(const char* text)
{
    try { return UiModsWarnBody(text); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiModsWarnGuarded(const char* text)
{
    __try { return UiModsWarnCatching(text); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiModsWarnShow(const char* text) { return UiModsWarnGuarded(text != 0 ? text : ""); }
int UiModsWarnOkClicked() { return ::InterlockedExchange(&g_mwOkClickedFlag, 0) != 0 ? 1 : 0; }
static int UiModsWarnCloseBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    ::InterlockedExchange(&g_mwOkClickedFlag, 0);
    MyGUI::Widget* raw = gui->findWidgetT(std::string(kMwWinName), false);
    if (raw == 0) return 0;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) im->resetKeyFocusWidget();
    UiHostLeftDrop(gui, 0, raw);
    return 1;
}
static int UiModsWarnCloseCatching()
{
    try { return UiModsWarnCloseBody(); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiModsWarnCloseGuarded()
{
    __try { return UiModsWarnCloseCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiModsWarnClose() { return UiModsWarnCloseGuarded(); }
/* T-246 fold 2 (item 4): after a Close that failed, the window is at least hidden so it is never on screen with the host-left
   window; the tick closes it again and the next Show builds it anew. */
static int UiModsWarnHideBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* raw = gui->findWidgetT(std::string(kMwWinName), false);
    if (raw == 0) return 0;
    raw->setVisible(false);
    return 1;
}
static int UiModsWarnHideCatching()
{
    try { return UiModsWarnHideBody(); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiModsWarnHideGuarded()
{
    __try { return UiModsWarnHideCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiModsWarnHide() { return UiModsWarnHideGuarded(); }

/* ui1 (owner 2026-09-27, connection trouble step 2) - THE SMALL NOTICE: "Connection problem - waiting for the host...", a
   thin Kenshi_FloatingPanelThinSkin strip (Kenshi_ProgressBarPanel.layout's panel, Read) centred near the top, on the
   dialog's layer, taking NO mouse (it never blocks the game and does not pause it); a centred TextBox in the message box's
   colour. Taken down as soon as the host is heard again. The panel's recipe, behind the same SEH frame. */
static const char* const kQnName     = "CoopQuietNotice";
static const char* const kQnTextName = "CoopQuietNoticeText";
static int UiQuietBody(const char* text, int on)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* raw = gui->findWidgetT(std::string(kQnName), false);
    if (on == 0) { if (raw != 0) gui->destroyWidget(raw); UiRectForget(g_rectQuiet); return 1; }
    if (raw == 0)
    {
        const UiNames& n = NM();
        int vw = 1280, vh = 720;
        MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
        if (rm != 0) { const MyGUI::IntSize& vs = rm->getViewSize(); if (vs.width > 0 && vs.height > 0) { vw = vs.width; vh = vs.height; } }
        int w = (vw * 30) / 100; if (w < 360) w = 360; if (w > vw - 20) w = vw - 20;
        const int h = 44;
        const char* layers[2] = { kHlLayer, kHlLayerAlt };
        for (int k = 0; k < 2 && raw == 0; ++k)
        {
            raw = gui->createWidgetT(n.typeWidget, std::string("Kenshi_FloatingPanelThinSkin"), MyGUI::IntCoord((vw - w) / 2, vh / 12, w, h),
                                     MyGUI::Align::Default, std::string(layers[k]), std::string(kQnName));
            if (raw == 0) { ::InterlockedIncrement64(&g_uiCreateNull); return 0; }
            if (raw->getLayer() == 0) { gui->destroyWidget(raw); raw = 0; }
        }
        if (raw == 0) return 0;
        raw->setNeedMouseFocus(false);
        MyGUI::Widget* tw = Mk(raw, n.typeText, n.skinFlat, 6, 4, w - 12, h - 8, kQnTextName);
        MyGUI::TextBox* tb = (tw != 0) ? tw->castType<MyGUI::TextBox>(false) : 0;
        if (tb == 0) { gui->destroyWidget(raw); return 0; }
        tw->setNeedMouseFocus(false);
        tb->setTextAlign(MyGUI::Align::Center);
        tb->setTextColour(MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f));
    }
    MyGUI::Widget* tw = gui->findWidgetT(std::string(kQnTextName), false);
    MyGUI::TextBox* tb = (tw != 0) ? tw->castType<MyGUI::TextBox>(false) : 0;
    if (tb == 0) return 0;
    tb->setCaption(MyGUI::UString(text));
    raw->setVisible(true);
    UiLogRect("quietnotice", raw, g_rectQuiet);   /* ui1: harness screenshots */
    return 1;
}
static int UiQuietCatching(const char* text, int on)
{
    try { return UiQuietBody(text, on); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiQuietGuarded(const char* text, int on)
{
    __try { return UiQuietCatching(text, on); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiQuietNotice(const char* text, int on) { return UiQuietGuarded(text != 0 ? text : "", on); }
/* ui1: the in-world dialog's and notice's rectangles, re-read (the view can be resized while they are up). ui1c: one found
   gone (by name - the game can remove it itself, e.g. a world unload) is forgotten, so a rebuild logs its [UI] rect again. */
static int UiDialogRectBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return 0;
    MyGUI::Widget* w = gui->findWidgetT(std::string(kHlWinName), false);
    if (w != 0) UiLogRect("hostleft", w, g_rectHl); else UiRectForget(g_rectHl);
    w = gui->findWidgetT(std::string(kQnName), false);
    if (w != 0) UiLogRect("quietnotice", w, g_rectQuiet); else UiRectForget(g_rectQuiet);
    return 1;
}
static int UiDialogRectCatching()
{
    try { return UiDialogRectBody(); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiDialogRectGuarded()
{
    __try { return UiDialogRectCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int UiDialogRectPoll() { return UiDialogRectGuarded(); }

/* ==================================================================================================================
   uishot (owner 2026-09-27, option a) - TEST-ONLY `uipreview <what>`: every co-op screen on demand, for the harness's
   `shot <label> dialog`. Each screen is shown through the code that shows it for real, with sample text:
     title (no world running): `panel multiplayer|host|join|newworld|deleteworld|hosting|options|profiles` - the co-op panel,
       opened by setting the panel's own screen state and letting the title tick build it (PanelBuild / PanelPush), through
       each screen's own open code where it has one (PanelIntro, PanelScanWorlds, the New world action, PanelDeletePress,
       PanelHostingOpen, PanelOptOpen, PanelProfOpen); `errorbox` - UiRaiseNotice with a sample CAN'T JOIN sentence.
       An empty worlds / profiles list gets two SAMPLE rows (said only in the log), removed again at `off`.
     in a world: `closed` / `lost` - UiHostLeftShow with the DISCONNECTED / CONNECTION LOST texts (coopown::HostLeftTitle,
       HostLeftSentence); `notice` - UiQuietNotice with coopown::HostQuietLine. store.cpp's host-left state (g_hl) is never
       read or written: nothing pauses, nothing saves. `off` closes the widget and drains the Exit click flag.
   Refused while a real dialog / notice / panel is up, so a preview never takes one over; a new preview while one of ours is up
   takes ours down first (an implied `off` - a request fits five screens in its ten lines). Nothing is pressed: no GO / HOST /
   CREATE / DELETE / Done runs, so nothing is written, started or deleted. Every MyGUI call is behind an SEH frame: the title
   tick's own (UiTitleTickGuarded) or the Ui*Guarded frames below.
   ================================================================================================================== */
static int UiPrevFindBody(const char* name)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return -3;
    return gui->findWidgetT(std::string(name), false) != 0 ? 1 : 0;
}
static int UiPrevFindCatching(const char* name)
{
    try { return UiPrevFindBody(name); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiPrevFindGuarded(const char* name)
{
    __try { return UiPrevFindCatching(name); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
static bool UiPreviewRefuse(const std::string& what, const std::string& why)
{
    DebugLog("[UI] uipreview " + what + " REFUSED - " + why);
    return false;
}
static void UiPreviewSampleWorlds()
{
    if (!g_worlds.empty()) return;
    const long long now = (long long)std::time(0);
    const char* names[2] = { "Sample world", "Sample world 2" };
    for (int i = 0; i < 2; ++i)
    {
        coopworld::WorldRow r;
        r.name = names[i];
        r.folder = coopworld::WorldFolderName(r.name);
        r.lastPlayedUnix = now - 3600LL * (i == 0 ? 2 : 50);
        r.players.push_back("Sample player");
        g_worlds.push_back(r);
        g_worldFresh.push_back(0);
    }
    g_worldSel = g_worlds[0].folder;
    ++g_worldListGen;
    g_uiPrevSampleWorlds = 1;
    DebugLog("[UI] uipreview: no worlds on disk - 2 SAMPLE rows shown ('Sample world', 'Sample world 2'); nothing written");
}
static void UiPreviewSampleProfiles()
{
    if (!g_profList.empty()) return;
    const long long now = (long long)std::time(0);
    StoreProfRow a; a.num = 1; a.name = "Sample profile";   a.faction = "Sample Clan";    a.lastPlayed = now - 7200;
    StoreProfRow b; b.num = 2; b.name = "Sample profile 2"; b.faction = "Sample Traders"; b.lastPlayed = 0;
    g_profList.push_back(a);
    g_profList.push_back(b);
    g_profSel = 1;
    ++g_profListGen;
    g_uiPrevSampleProfs = 1;
    DebugLog("[UI] uipreview: no profiles from a notebook - 2 SAMPLE rows shown ('Sample profile', 'Sample profile 2'); nothing sent");
}
static bool UiPreviewTitle(const std::string& what, const std::string& kind, const std::string& screen)
{
    int scr = -1;
    if (kind == "panel")
    {
        static const char* const names[8] = { "multiplayer", "host", "join", "newworld", "deleteworld", "hosting", "options", "profiles" };
        for (int i = 0; i < 8; ++i) if (screen == names[i]) scr = i;
        if (scr < 0) return UiPreviewRefuse(what, "not a panel screen (multiplayer|host|join|newworld|deleteworld|hosting|options|profiles)");
    }
    else if (!screen.empty()) return UiPreviewRefuse(what, kind + " takes no argument");
    if (kind == "errorbox")
    {
        if (::InterlockedCompareExchange(&g_noticeWanted, 0, 0) != 0) return UiPreviewRefuse(what, "a real error box is up");
        UiRaiseNotice("You could not join this world: the host isn't responding. Check the address and try again.");   /* words1b: the prefix picks CAN'T JOIN */
        g_uiPrevKind = 2;
    }
    else if (kind == "identity1" || kind == "identity2")   /* PP3d: the identity box, buttons that only close it */
    {
        if (::InterlockedCompareExchange(&g_idBoxWanted, 0, 0) != 0) return UiPreviewRefuse(what, "a real identity box is up");
        IdBoxRaise(kind == "identity1" ? 1 : 2, 1);
        g_uiPrevKind = 5;
    }
    else
    {
        if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) != 0) return UiPreviewRefuse(what, "the co-op panel is already open - close it first");
        PanelDefaults();
        g_panelFailStreak = 0;
        g_profDlg = 0;
        g_panelScreen = 0;
        if (scr == 2) { g_panelMode = 1; g_panelScreen = 2; PanelIntro(); }
        else if (scr == 1 || scr == 3 || scr == 4 || scr == 6)
        {
            g_panelMode = 0;
            g_panelScreen = 1;
            PanelIntro();
            PanelScanWorlds();
            UiPreviewSampleWorlds();
            if (scr == 3) UiQueue(kActNewWorld);   /* the New world button's own action, run by the title tick (PanelApply) */
            if (scr == 4) PanelDeletePress();      /* the Delete button's own code: the confirm dialog, nothing deleted */
            if (scr == 6) PanelOptOpen();          /* the Game options button's own code: reads options.txt, writes nothing */
        }
        else if (scr == 5) { g_panelMode = 0; PanelHostingOpen(); }
        else if (scr == 7) { UiPreviewSampleProfiles(); PanelProfOpen(); }
        g_profPushWanted = 1;
        ::InterlockedExchange(&g_panelWanted, 1);
        g_uiPrevKind = 1;
    }
    g_uiPrevWhat = what;
    g_uiPrevPending = 1;
    DebugLog("[UI] uipreview " + what + " - queued for the title tick (nothing pressed, nothing written)");
    return true;
}
static bool UiPreviewWorld(const std::string& what, const std::string& kind)
{
    int r = 0;
    if (kind == "notice")
    {
        const int f = UiPrevFindGuarded(kQnName);
        if (f != 0) return UiPreviewRefuse(what, f == 1 ? "a real connection-problem notice is up" : "the widget lookup failed rc=" + coopui::PanelNum((long long)f));
        UiRectForget(g_rectQuiet);
        r = UiQuietNotice(coopown::HostQuietLine(), 1);
        if (r != 1) { (void)UiQuietNotice("", 0); return UiPreviewRefuse(what, "UiQuietNotice rc=" + coopui::PanelNum((long long)r)); }
        g_uiPrevKind = 4;
    }
    else
    {
        const int f = UiPrevFindGuarded(kHlWinName);
        if (f != 0) return UiPreviewRefuse(what, f == 1 ? "a real host-left dialog is up" : "the widget lookup failed rc=" + coopui::PanelNum((long long)f));
        const int hk = (kind == "lost") ? coopown::kHlLost : coopown::kHlAnnounced;
        const int en = (kind == "lost") ? (coopown::HostLeftButtonEnabled(coopown::kHlSaveNone, false) ? 1 : 0) : 1;
        UiRectForget(g_rectHl);
        r = UiHostLeftShow(coopown::HostLeftTitle(hk), coopown::HostLeftSentence(hk), en);
        if (r != 1) { (void)UiHostLeftClose(); return UiPreviewRefuse(what, "UiHostLeftShow rc=" + coopui::PanelNum((long long)r)); }
        g_uiPrevKind = 3;
    }
    g_uiPrevWhat = what;
    g_uiPrevPending = 0;
    DebugLog("[UI] uipreview " + what + " SHOWN");
    return true;
}
static bool UiPreviewOff()
{
    if (g_uiPrevKind == 0) { DebugLog("[UI] uipreview off - nothing to close"); return true; }
    int r = 1;
    if (g_uiPrevKind == 1) { ::InterlockedExchange(&g_panelWanted, 0); g_panelScreen = 0; g_profDlg = 0; }
    if (g_uiPrevKind == 2) UiClearNotice();
    if (g_uiPrevKind == 3) { r = UiHostLeftClose(); (void)UiHostLeftExitClicked(); }
    if (g_uiPrevKind == 4) r = UiQuietNotice("", 0);
    if (g_uiPrevKind == 5) IdBoxClear();   /* PP3d */
    if (g_uiPrevSampleWorlds != 0) { g_worlds.clear(); g_worldFresh.clear(); g_worldSel.clear(); ++g_worldListGen; g_uiPrevSampleWorlds = 0; }
    if (g_uiPrevSampleProfs != 0) { g_profList.clear(); g_profSel = 0; ++g_profListGen; g_uiPrevSampleProfs = 0; }
    DebugLog("[UI] uipreview off - " + g_uiPrevWhat + " taken down (rc=" + coopui::PanelNum((long long)r) + ")");
    g_uiPrevKind = 0;
    g_uiPrevPending = 0;
    g_uiPrevWhat.clear();
    return true;
}
static bool UiPreviewCommandBody(const std::string& args)
{
    std::istringstream is(args);
    std::string kind, screen, extra;
    is >> kind >> screen >> extra;
    const std::string what = screen.empty() ? kind : kind + " " + screen;
    if (kind.empty() || !extra.empty())
        return UiPreviewRefuse(args, "usage: uipreview panel <screen> | errorbox | identity1 | identity2 | closed | lost | notice | off");
    if (kind == "off") return screen.empty() ? UiPreviewOff() : UiPreviewRefuse(what, "off takes no argument");
    const bool world = GameplayRunning();
    if (kind == "panel" || kind == "errorbox" || kind == "identity1" || kind == "identity2")   /* PP3d: identity1 / identity2 */
    {
        if (world) return UiPreviewRefuse(what, "a title-screen preview while a world is running");
        if (g_uiPrevKind != 0) { DebugLog("[UI] uipreview " + what + " replaces " + g_uiPrevWhat + " (an implied off)"); UiPreviewOff(); }
        return UiPreviewTitle(what, kind, screen);
    }
    if (kind == "closed" || kind == "lost" || kind == "notice")
    {
        if (!screen.empty()) return UiPreviewRefuse(what, kind + " takes no argument");
        if (!world) return UiPreviewRefuse(what, "an in-world preview with no world running");
        if (g_uiPrevKind != 0) { DebugLog("[UI] uipreview " + what + " replaces " + g_uiPrevWhat + " (an implied off)"); UiPreviewOff(); }
        return UiPreviewWorld(what, kind);
    }
    return UiPreviewRefuse(what, "unknown preview (panel <screen> | errorbox | identity1 | identity2 | closed | lost | notice | off)");
}
bool UiPreviewCommand(const std::string& args)
{
    try { return UiPreviewCommandBody(args); }
    catch (...) { DebugLog("[UI] uipreview " + args + " - a C++ exception; nothing more done"); return false; }
}

/* pp1 (player-path-plan section 1a, owner 2026-09-27) - THE TEST-ONLY UI DRIVER: `uiclick`, `uipick`, `uitype`, `uistate`.
   A run that proves a player's path presses the REAL controls the player presses, through their own handlers, and nothing
   else: a click fires the widget's own eventMouseButtonClick delegate (the call MyGUI makes for a mouse click), a pick sets
   the list's selected row and fires its own eventListChangePosition (what MultiListBox does for a clicked row), a type sets
   the box's caption and fires its own eventEditTextChange and eventKeyButtonPressed (T-228: Kenshi's boxes re-check on the key press) - the panel then reads the box through UiTextOf / CfgSanitiseTyped
   on its next mirror, the path every keypress takes. A widget that is hidden or disabled, ITSELF OR ANY PARENT, is REFUSED:
   MyGUI's own pick stops at the first invisible or disabled widget on the way down, so a mouse could not reach it and neither
   may the test. MAIN THREAD (the command channel's tick, the same thread as the title tick and MyGUI's input). No new
   state: the panel's handlers are the two interlocked writes they always were. The name table is pure (common/uidrive.h,
   swept offline). Log lines: `[UI] <verb> <what> FIRED (screen <n>, ...)` / `[UI] <verb> <what> REFUSED - <why>`. */
static bool UiDriveReach(MyGUI::Widget* w, std::string* why, int* visOut, int* enOut)
{
    int vis = 1, en = 1, depth = 0;
    std::string r;
    for (MyGUI::Widget* p = w; p != 0 && depth < 80; p = p->getParent(), ++depth)   /* pp1b: 80 - the engine search goes 64 deep */
    {
        if (vis != 0 && !p->getVisible()) { vis = 0; if (r.empty()) r = (p == w) ? std::string("hidden") : "hidden (its parent '" + p->getName() + "' is hidden)"; }
        if (en != 0 && !p->getEnabled())  { en = 0;  if (r.empty()) r = (p == w) ? std::string("disabled") : "disabled (its parent '" + p->getName() + "' is disabled)"; }
    }
    if (why != 0) *why = r;
    if (visOut != 0) *visOut = vis;
    if (enOut != 0) *enOut = en;
    return vis != 0 && en != 0;
}
/* pp1b (review 2026-09-27, item 4) - one of Kenshi's own widgets by layout suffix (the U3 column's match). Depth-first over
   every root, BOUNDED (64 levels, 20000 widgets - FindByLayoutSuffix's 12 was the title column's slack, not a dialog's), and
   the FIRST REACHABLE match anywhere wins - not the first match of each root; else the first match (with its refusal). */
static void UiDriveEngineWalk(MyGUI::Widget* w, const char* suffix, int depth, int* budget, MyGUI::Widget** hit, MyGUI::Widget** first)
{
    if (w == 0 || *hit != 0 || depth > 64 || *budget <= 0) return;
    --*budget;
    if (NameCarriesLayoutSuffix(w->getName(), suffix))
    {
        if (UiDriveReach(w, 0, 0, 0)) { *hit = w; return; }
        if (*first == 0) *first = w;
    }
    const size_t n = w->getChildCount();
    for (size_t i = 0; i < n && *hit == 0; ++i) UiDriveEngineWalk(w->getChildAt(i), suffix, depth + 1, budget, hit, first);
}
static MyGUI::Widget* UiDriveEngine(MyGUI::Gui* gui, const std::string& suffix, int reachableOnly, std::string* why)
{
    MyGUI::Widget* hit = 0;
    MyGUI::Widget* first = 0;
    int budget = 20000;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (hit == 0 && roots.next()) UiDriveEngineWalk(roots.current(), suffix.c_str(), 0, &budget, &hit, &first);
    if (hit != 0) return hit;
    if (why != 0)
    {
        if (first != 0) UiDriveReach(first, why, 0, 0);
        else *why = std::string("not found (no widget ends in ") + suffix + (budget <= 0 ? " - the 20000-widget search budget was spent)" : ")");
    }
    return reachableOnly != 0 ? 0 : first;
}
/* pp1b: a row with a `caption` presses its widget only while the widget READS it (uidrive.h: `create` is never DELETE WORLD). */
static bool UiDriveCaptionOk(MyGUI::Widget* w, const char* want, std::string* why)
{
    if (want == 0) return true;
    MyGUI::TextBox* tb = w->castType<MyGUI::TextBox>(false);
    const char* cap = (tb != 0) ? tb->getCaption().asUTF8_c_str() : 0;
    if (cap != 0 && std::strcmp(cap, want) == 0) return true;
    if (why != 0) *why = std::string("it reads '") + (cap != 0 ? cap : "") + "', not '" + want + "' (use dlgok for the dialog's confirm, whatever it reads)";
    return false;
}
/* One of OUR widgets by the table's row: w1, or w2 when w1 is not the reachable one (BACK). */
static MyGUI::Widget* UiDriveNamed(MyGUI::Gui* gui, const coopui::UiDriveName* row, int reachableOnly, std::string* why)
{
    if (row == 0) { if (why != 0) *why = "not in the name table"; return 0; }
    const char* names[2] = { row->w1, row->w2 };
    MyGUI::Widget* first = 0;
    std::string firstWhy;
    for (int i = 0; i < 2; ++i)
    {
        if (names[i] == 0) continue;
        MyGUI::Widget* w = gui->findWidgetT(std::string(names[i]), false);
        if (w == 0) continue;
        std::string r;
        if (UiDriveReach(w, &r, 0, 0) && UiDriveCaptionOk(w, row->caption, &r)) return w;
        if (first == 0) { first = w; firstWhy = r; }
    }
    if (why != 0) *why = (first != 0) ? firstWhy : std::string("not found (") + row->w1 + " is not on screen)";
    return reachableOnly != 0 ? 0 : first;
}
static std::string UiDriveScreen()
{
    return coopui::PanelNum((long long)g_panelScreen);
}
static bool UiDriveRefuse(const std::string& verb, const std::string& what, const std::string& why)
{
    DebugLog("[UI] " + verb + " " + what + " REFUSED - " + why);
    return false;
}
/* pp1b (review 2026-09-27, item 2) - WHAT THE MOUSE WOULD HIT, past UiDriveReach (hidden / disabled). A click is REFUSED when
   (a) a MODAL window is up and the target is not inside the TOP modal root: MyGUI's InputManager lets the mouse reach only
   that root's widgets (T-241: the root is read from the private mVectorModalRootWidget, UiTopModalRoot above);
   (b) the target is not the topmost widget at its own centre: LayerManager::getWidgetFromPoint - the pick MyGUI's own
   injectMouseMove makes - must return the target or one of its children. */
/* T-241 (2026-09-29): the TOP modal root, read from InputManager's private mVectorModalRootWidget through the standard
   explicit-instantiation access (legal C++03: an explicit instantiation may name a private member). The headers are the ones
   the game's MyGUIEngine_x64.dll was built from (the mod imports MyGUI from that DLL), so the member is the one the game uses.
   MyGUI routes the mouse only to the LAST root in that vector and its children. */
struct UiModalRootsTag { typedef MyGUI::VectorWidgetPtr MyGUI::InputManager::*type; friend type UiModalRootsGet(UiModalRootsTag); };
template <typename Tag, typename Tag::type M> struct UiRobMember { friend typename Tag::type UiModalRootsGet(Tag) { return M; } };
template struct UiRobMember<UiModalRootsTag, &MyGUI::InputManager::mVectorModalRootWidget>;
static MyGUI::Widget* UiTopModalRoot(MyGUI::InputManager* im)
{
    if (im == 0) return 0;
    const MyGUI::VectorWidgetPtr& v = im->*UiModalRootsGet(UiModalRootsTag());
    return v.empty() ? 0 : v.back();
}
static bool UiDriveMouseReach(MyGUI::Widget* w, std::string* why)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0 && im->isModalAny())
    {
        /* T-241: a click reaches a widget inside the TOP modal root (Kenshi's quit box, for example); any other is refused */
        MyGUI::Widget* root = UiTopModalRoot(im);
        bool inside = false;
        int d = 0;
        for (MyGUI::Widget* p = w; p != 0 && d < 80 && !inside; p = p->getParent(), ++d) inside = (p == root);
        if (root == 0 || !inside)
        {
            if (why != 0) *why = "a modal window is up ('" + (root != 0 ? root->getName() : std::string("unnamed")) + "') and the target is not inside it";
            return false;
        }
    }
    MyGUI::LayerManager* lm = MyGUI::LayerManager::getInstancePtr();
    if (lm == 0) { if (why != 0) *why = "no LayerManager"; return false; }
    const MyGUI::IntCoord c = w->getAbsoluteCoord();
    const int x = c.left + c.width / 2, y = c.top + c.height / 2;
    MyGUI::Widget* top = lm->getWidgetFromPoint(x, y);
    int depth = 0;
    for (MyGUI::Widget* p = top; p != 0 && depth < 80; p = p->getParent(), ++depth)
        if (p == w) return true;
    if (why != 0) *why = "covered at its centre (" + coopui::PanelNum((long long)x) + "," + coopui::PanelNum((long long)y) + ") by '"
                         + (top != 0 ? top->getName() : std::string("nothing that takes the mouse")) + "'";
    return false;
}
/* T-220 - TEST-ONLY `uiclick close` and `uiclick escape`: the panel's two close routes that are not a button of ours (PP6 fold item 1:
   SELECT mode must end on X and Escape too). `close` fires the panel window's OWN eventWindowButtonPressed("close") - the event MyGUI's
   Window raises for a click on Kenshi_WindowCX's X skin button (OnCoopWindowButton). `escape` makes the call the engine's front-end
   ESC arm makes (decomp_82a460, the ESC gate above): TitleScreen::closeTheOtherBits through its hooked entry with the live TitleScreen
   (UiTitleScreenNote) - the answer it gets is logged, and the quit byte that a 0 answer would raise is NEVER raised here. No injected
   input (F010). Each engine call is in its own frame with no C++ object (C2712). */
static void* g_titleSeen = 0;
static DWORD g_titleSeenAtMs = 0;
static int UiEscArmPod(uintptr_t fn, void* title)
{
    __try { return ((bool (*)(::TitleScreen*))fn)((::TitleScreen*)title) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static bool UiDriveWindowClose(MyGUI::Gui* gui, const std::string& name)
{
    std::string why;
    MyGUI::Widget* w = UiDriveNamed(gui, coopui::UiDriveFind(name, coopui::kDriveWindowClose), 1, &why);
    if (w == 0) return UiDriveRefuse("uiclick", name, why);
    MyGUI::Window* win = w->castType<MyGUI::Window>(false);
    if (win == 0) return UiDriveRefuse("uiclick", name, "the panel is not a window");
    if (!UiDriveMouseReach(w, &why)) return UiDriveRefuse("uiclick", name, why);
    const std::string screen = UiDriveScreen();
    win->eventWindowButtonPressed(win, "close");
    DebugLog("[UI] uiclick " + name + " FIRED (screen " + screen + ", the title-bar X: the window's own eventWindowButtonPressed 'close')");
    return true;
}
static bool UiDriveEscape(const std::string& name)
{
    const uintptr_t fn = (uintptr_t)AddrAbs(kMig3CloseTheOtherBits);
    if (fn == 0 || orig_titleCloseOtherBits == 0) return UiDriveRefuse("uiclick", name, "the ESC gate is not installed (TitleScreen::closeTheOtherBits)");
    if (g_titleSeen == 0 || ::GetTickCount() - g_titleSeenAtMs > 2000)
        return UiDriveRefuse("uiclick", name, "no title frame in the last 2 s - Escape in a world is Kenshi's own menu (uimenu ingame)");
    const std::string screen = UiDriveScreen();
    const LONG wanted = ::InterlockedCompareExchange(&g_panelWanted, 0, 0);
    const int rc = UiEscArmPod(fn, g_titleSeen);
    if (rc < 0) { ErrorLog("[UI] uiclick " + name + " - the engine's closeTheOtherBits faulted; nothing more done"); return false; }
    DebugLog("[UI] uiclick " + name + " FIRED (screen " + screen + ", the engine's ESC arm: closeTheOtherBits answered " + coopui::PanelNum((long long)rc)
             + (rc != 0 ? std::string(" - something closed, Kenshi does not quit") : std::string(" - nothing closed: a real Escape here would QUIT Kenshi (the quit byte is not raised by this lever)"))
             + "; the panel was " + (wanted != 0 ? "open" : "closed") + ")");
    return true;
}
/* pp1b (item 3): an accepted engine: click waits here for the tail of the pump (UiDriveEngineClickFlush). */
static std::string g_driveEngineQueued;
static bool UiDriveClick(MyGUI::Gui* gui, const std::string& name)
{
    if (coopui::UiDriveFind(name, coopui::kDriveWindowClose) != 0) return UiDriveWindowClose(gui, name);   /* T-220 */
    if (coopui::UiDriveFind(name, coopui::kDriveEscape) != 0) return UiDriveEscape(name);                  /* T-220 */
    std::string why, suffix;
    const int engine = coopui::UiDriveEngineName(name, &suffix);
    MyGUI::Widget* w = (engine != 0)
        ? UiDriveEngine(gui, suffix, 1, &why)
        : UiDriveNamed(gui, coopui::UiDriveFind(name, coopui::kDriveButton), 1, &why);
    if (w == 0) return UiDriveRefuse("uiclick", name, why);
    if (!UiDriveMouseReach(w, &why)) return UiDriveRefuse("uiclick", name, why);
    const std::string widget = w->getName();   /* read BEFORE the click: an engine handler may destroy the widget */
    const std::string screen = UiDriveScreen();
    if (engine != 0)
    {
        /* pp1b (review item 3): an ENGINE handler does not run here, mid-frame inside our hook; it is queued and fired at the tail
           of this pump (after the engine's own update), and the status says `ok uiclick` first - the quitmenu shape. */
        if (!g_driveEngineQueued.empty()) return UiDriveRefuse("uiclick", name, "an engine click is already queued (" + g_driveEngineQueued + ")");
        g_driveEngineQueued = name;
        DebugLog("[UI] uiclick " + name + " QUEUED (screen " + screen + ", widget " + widget + ") - it fires at the end of this frame");
        return true;
    }
    w->eventMouseButtonClick(w);
    DebugLog("[UI] uiclick " + name + " FIRED (screen " + screen + ", widget " + widget + ")");
    return true;
}
/* The queued engine click, found AGAIN (the engine's update may have rebuilt the screen) and checked again before it fires. */
static bool UiDriveEngineClickFire(const std::string& name)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return UiDriveRefuse("uiclick", name, "no GUI at the end of the frame");
    std::string why, suffix;
    if (coopui::UiDriveEngineName(name, &suffix) == 0) return UiDriveRefuse("uiclick", name, "not an engine name");
    MyGUI::Widget* w = UiDriveEngine(gui, suffix, 1, &why);
    if (w == 0) return UiDriveRefuse("uiclick", name, why + " (at the end of the frame)");
    if (!UiDriveMouseReach(w, &why)) return UiDriveRefuse("uiclick", name, why + " (at the end of the frame)");
    const std::string widget = w->getName();
    const std::string screen = UiDriveScreen();
    w->eventMouseButtonClick(w);
    DebugLog("[UI] uiclick " + name + " FIRED at the end of the frame (screen " + screen + ", widget " + widget + ")");
    return true;
}
static bool UiDrivePick(MyGUI::Gui* gui, const std::string& list, const std::string& text)
{
    const std::string what = list + " '" + text + "'";
    std::string why;
    MyGUI::Widget* w = UiDriveNamed(gui, coopui::UiDriveFind(list, coopui::kDriveList), 1, &why);
    if (w == 0) return UiDriveRefuse("uipick", what, why);
    MyGUI::MultiListBox* m = w->castType<MyGUI::MultiListBox>(false);
    if (m == 0) return UiDriveRefuse("uipick", what, "the widget is not a list");
    const size_t count = m->getItemCount();
    size_t row = MyGUI::ITEM_NONE;
    for (size_t i = 0; i < count && row == MyGUI::ITEM_NONE; ++i)
    {
        const char* p = m->getItemNameAt(i).asUTF8_c_str();   /* the row's first column, as the player reads it */
        if (p != 0 && text == p) row = i;
    }
    if (row == MyGUI::ITEM_NONE) return UiDriveRefuse("uipick", what, "no such row (" + coopui::PanelNum((long long)count) + " rows)");
    const std::string screen = UiDriveScreen();
    m->setIndexSelected(row);
    m->eventListChangePosition(m, row);
    DebugLog("[UI] uipick " + what + " FIRED (screen " + screen + ", row " + coopui::PanelNum((long long)row) + " of " + coopui::PanelNum((long long)count) + ")");
    return true;
}
static bool UiDriveType(MyGUI::Gui* gui, const std::string& field, const std::string& text)
{
    std::string why, suffix;
    MyGUI::Widget* w = coopui::UiDriveEngineName(field, &suffix)
        ? UiDriveEngine(gui, suffix, 1, &why)
        : UiDriveNamed(gui, coopui::UiDriveFind(field, coopui::kDriveEdit), 1, &why);
    if (w == 0) return UiDriveRefuse("uitype", field, why);
    MyGUI::EditBox* e = w->castType<MyGUI::EditBox>(false);
    if (e == 0) return UiDriveRefuse("uitype", field, "the widget is not a text box");
    const std::string screen = UiDriveScreen();
    e->setCaption(MyGUI::UString(text.c_str()));
    e->eventEditTextChange(e);
    /* T-228 (2026-09-29 read, decomp_481660/47a740): Kenshi's own boxes re-check on the KEY-PRESS event, not the text change - the
       Save window's name box enables SAVE only in its key handler 0x47a740 (SaveButton->setEnabled(!caption.empty())). A player's
       keystroke fires both events (MyGUI puts the character in, then fires the key press), so the lever fires both. Character 0 is
       not one of the handler's forbidden characters, so nothing is deleted; the mod's own panel key handler sits on its window, not
       on the box, and is not reached. */
    e->eventKeyButtonPressed(e, MyGUI::KeyCode::None, 0);
    DebugLog("[UI] uitype " + field + " FIRED (screen " + screen + ", '" + text + "' - the text change and a key press, as a player's keystroke fires them; the panel reads it through UiTextOf / CfgSanitiseTyped)");
    return true;
}
static std::string UiDriveFlags(MyGUI::Widget* w)
{
    if (w == 0) return "-";
    int vis = 0, en = 0;
    UiDriveReach(w, 0, &vis, &en);
    return std::string(vis != 0 ? "1" : "0") + (en != 0 ? "1" : "0");
}
static std::string UiStateOneLine(const std::string& s)   /* T-201 N1: the status text on one log line, its line breaks as " | " */
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\n') o += " | "; else if (s[i] != '\r') o += s[i]; }
    return o;
}
static bool UiDriveState(MyGUI::Gui* gui)
{
    const LONG noticeOn = ::InterlockedCompareExchange(&g_noticeWanted, 0, 0);
    std::string line = "[UI] state screen=" + UiDriveScreen()
        + " panel=" + coopui::PanelNum((long long)::InterlockedCompareExchange(&g_panelWanted, 0, 0))
        + " notice='" + (noticeOn != 0 ? g_noticeText : std::string()) + "'"
        + " press=" + (g_pressBusy == 1 ? "host" : g_pressBusy == 2 ? "join" : "idle")   /* T-201 N1 */
        + " load=" + coopui::PanelNum((long long)g_loadStage)   /* T-201 PP6': 0 none, 1 admitting, 2 Loading shown, 3 acting */
        + " status='" + UiStateOneLine(g_panelStatusCaption) + "' buttons=";
    size_t n = 0;
    const coopui::UiDriveName* t = coopui::UiDriveTable(&n);
    int first = 1;
    for (size_t i = 0; i < n; ++i)
    {
        if (t[i].kind != coopui::kDriveButton) continue;
        int dup = 0;
        for (size_t j = 0; j < i; ++j) if (t[j].kind == coopui::kDriveButton && std::strcmp(t[j].w1, t[i].w1) == 0) dup = 1;
        if (dup != 0) continue;
        line += (first != 0 ? "" : ",") + std::string(t[i].name) + ":" + UiDriveFlags(UiDriveNamed(gui, &t[i], 0, 0));
        first = 0;
    }
    static const char* const kTitle[4] = { "ContinueButton", "NewGameButton", "LoadGameButton", "ExitButton" };
    for (int k = 0; k < 4; ++k)
        line += ",engine:" + std::string(kTitle[k]) + ":" + UiDriveFlags(UiDriveEngine(gui, std::string(kTitle[k]), 0, 0));
    DebugLog(line);
    return true;
}
static bool UiDriveCommandBody(const std::string& verb, const std::string& args)
{
    const std::string bad = coopui::UiDriveUsage(verb, args);
    if (!bad.empty()) return UiDriveRefuse(verb, args, bad);
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return UiDriveRefuse(verb, args, "no GUI yet");
    std::string a, rest;
    coopui::UiDriveSplit(args, &a, &rest);
    if (verb == "uiclick") return UiDriveClick(gui, a);
    if (verb == "uipick")  return UiDrivePick(gui, a, rest);
    if (verb == "uitype")  return UiDriveType(gui, a, rest);
    return UiDriveState(gui);
}
void UiTitleScreenNote(void* title)   /* T-220: title pump only (detour_titleUpdate) */
{
    g_titleSeen = title;
    g_titleSeenAtMs = ::GetTickCount();
}
bool UiDriveCommand(const std::string& verb, const std::string& args)
{
    try { return UiDriveCommandBody(verb, args); }
    catch (...) { DebugLog("[UI] " + verb + " " + args + " - a C++ exception; nothing more done"); return false; }
}
/* T-228 (1) - TEST-ONLY `uimenu ingame`. The in-game ESC arm of the key handler (build/decomp_82a460.txt:65-73, F537(a)) is
   `panel = 0x47A0B0(); ... 0x9164F0(panel)`, and 0x9164F0 is `getVisible(*(panel+8)) ? hide 0x916190 : show 0x913250(panel)`.
   0x47A0B0 is the singleton getter that builds the panel once through 0x917210 (which loads Kenshi_MainMenuPopupPanel.layout and
   binds SAVE GAME / LOAD GAME / ... / RESUME); 0x913250 is the show half of that toggle (it pauses the game as ESC does, then shows
   the widget). This calls the getter and then the SHOW half only - never the toggle - so a menu already open is left open. Rows
   MainMenuPopup_get / MainMenuPopup_show (1.0.68: 0x47A910 / 0x914120 - the same bytes with their rel32s masked, and the getter's
   constructor call lands on 0x9180E0 = 0x917210's twin). Each engine call is in its own frame with no C++ object (C2712). */
static unsigned long long kT228MenuGet = 0; static coop::AddrReg kT228MenuGet_reg("MainMenuPopup_get", &kT228MenuGet);     /* Steam_1.0.65 0x47A0B0 */
static unsigned long long kT228MenuShow = 0; static coop::AddrReg kT228MenuShow_reg("MainMenuPopup_show", &kT228MenuShow);  /* Steam_1.0.65 0x913250 */
static void* UiMenuGetPod(uintptr_t fn, int* fault)
{
    __try { return ((void* (*)())fn)(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = 1; return 0; }
}
static int UiMenuShowPod(uintptr_t fn, void* panel)
{
    __try { ((void (*)(void*))fn)(panel); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-514 (owner 448) - IN A MULTIPLAYER WORLD THE PAUSE MENU'S NEW GAME, SAVE GAME AND LOAD GAME ARE HIDDEN AND THE MENU CLOSES UP.
   A post-hook on the menu's show (MainMenuPopup_show above): the engine's own show re-enables NEW GAME and SAVE GAME on every open
   (decomp_913250.txt), so AFTER it runs, while HandSaveBlocked() (store.cpp: the title down and a multiplayer world), the three
   buttons are hidden AND disabled (no click or key reaches them) and OPTIONS, EXIT GAME, REPORT A BUG and RESUME move up to where
   NEW GAME stands, at Kenshi's row pitch and gap, the panel shrunk to fit around its own centre (pausemenu.h Place). Outside a
   multiplayer world the three are shown, LOAD GAME (which the show leaves alone) is enabled again if this disabled it, and every
   Kenshi row is where Kenshi put it. Kenshi's positions are recorded once per panel, the first time this sees it, before anything
   moves. REPORT A BUG (bugreport.cpp PauseRowInner) calls UiPauseMenuArrange after adding its button; every call places all rows
   from Kenshi's positions, so neither the order of the two calls nor a reopen moves anything twice. The buttons are found by
   layout suffix under the panel's own root (panel+8) only. MAIN THREAD (ESC, or `uimenu ingame`). Opens in a multiplayer world
   are counted; one line is logged each time the arrangement changes. The save / load / new-game refusals in store.cpp stand
   behind this. */
/* T-524 (owner 451 / 454) - HOSTING, THE TOP ROW OF THE HOST'S PAUSE MENU.  Made by the arranging below, once per menu, the
   size, skin and caption colour of OPTIONS; shown and enabled only while this game hosts the multiplayer world
   (HandSaveBlocked() and PanelHostingNow() - the title's own "this game hosts a world now" - read on every open, and again
   when the world server stops while the menu is open) and the rows were placed; hidden and disabled otherwise, so a
   joiner's and a single-player menu are unchanged.  Its click is one interlocked write; the in-world
   tick (UiHostingWorldTick) opens the HOSTING window. */
static volatile LONG g_hostingAsk = 0;
static void OnHostingRowClicked(MyGUI::Widget* /*sender*/) { ::InterlockedExchange(&g_hostingAsk, 1); }
static MyGUI::Widget* UiPauseHostingMake(MyGUI::Widget* pp, MyGUI::Widget* options)
{
    const MyGUI::IntCoord o = options->getCoord();
    MyGUI::Widget* x = Mk(pp, "Button", "Kenshi_Button1", o.left, o.top, o.width, o.height, pausemenu::kHostingButtonName);
    MyGUI::Button* b = x != 0 ? x->castType<MyGUI::Button>(false) : 0;
    if (b == 0) { if (x != 0) MyGUI::Gui::getInstancePtr()->destroyWidget(x); return 0; }
    b->setVisible(false);   /* shown only once the arranging has placed it */
    b->setEnabled(false);
    b->setCaption(MyGUI::UString("HOSTING"));
    MyGUI::TextBox* ot = options->castType<MyGUI::TextBox>(false);
    if (ot != 0) b->setTextColour(ot->getTextColour());   /* OPTIONS' own lettering, read from it (else the skin's own) */
    b->eventMouseButtonClick += MyGUI::newDelegate(OnHostingRowClicked);
    return b;
}
static long long g_hiddenOpens = 0;
static int g_arrangeLastLogged = -100, g_disabledLoad = 0;
static MyGUI::Widget* g_pmPanel = 0;          /* the MainMenuPopupPanel whose Kenshi positions g_pmKenshi holds */
static pausemenu::KenshiRows g_pmKenshi;
static pausemenu::Placement g_pmLast;         /* the last placement applied, for the log line */
static void* (*orig_pauseMenuShow)(void*) = 0;
/* Result bits: 1 a multiplayer world (the three hidden), 2 REPORT A BUG on the panel, 4 every row placed, 8 * (found of the three,
   0..3), 32 HOSTING on the panel. < 0: -3 the panel holds no widget, -1 a MyGUI throw, -2 a fault. */
static int UiPauseArrangeBody(void* panel)
{
    MyGUI::Widget* root = *(MyGUI::Widget**)((char*)panel + 8);
    if (root == 0) return -3;
    const bool block = HandSaveBlocked();
    static const char* const kSuffix[6] = { "NewGameButton", "SaveGameButton", "LoadGameButton", "OptionsButton", "ExitGameButton", "ResumeButton" };
    MyGUI::Widget* w[6];
    int found = 0;
    for (int i = 0; i < 6; ++i) w[i] = FindByLayoutSuffix(root, kSuffix[i], 0);
    for (int i = 0; i < 3; ++i)
    {
        if (w[i] == 0) continue;
        ++found;
        if (block) { w[i]->setEnabled(false); w[i]->setVisible(false); }
        else
        {
            w[i]->setVisible(true);
            if (i == 2 && g_disabledLoad != 0) { w[i]->setEnabled(true); g_disabledLoad = 0; }
        }
    }
    if (block) g_disabledLoad = 1;
    int r = (block ? 1 : 0) + 8 * found;
    MyGUI::Widget* pp = FindByLayoutSuffix(root, "MainMenuPopupPanel", 0);
    bool all = pp != 0;
    for (int i = 0; i < 6 && all; ++i) all = w[i] != 0 && w[i]->getParent() == pp;
    if (!all) return r;
    if (g_pmPanel != pp)
    {
        pausemenu::KenshiRows k;
        k.newGame = w[0]->getTop(); k.saveGame = w[1]->getTop(); k.loadGame = w[2]->getTop();
        k.options = w[3]->getTop(); k.exitGame = w[4]->getTop(); k.resume = w[5]->getTop();
        k.panelHeight = pp->getHeight();
        g_pmKenshi = k;
        g_pmPanel = pp;
    }
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* bug = gui != 0 ? gui->findWidgetT(std::string(pausemenu::kBugButtonName), false) : 0;
    if (bug != 0 && bug->getParent() != pp) bug = 0;
    if (bug != 0) r += 2;
    MyGUI::Widget* hosting = gui != 0 ? gui->findWidgetT(std::string(pausemenu::kHostingButtonName), false) : 0;
    if (hosting != 0 && hosting->getParent() != pp) { gui->destroyWidget(hosting); hosting = 0; }
    const bool hostRow = block && PanelHostingNow();
    if (hostRow && hosting == 0) hosting = UiPauseHostingMake(pp, w[3]);
    const bool hasHosting = hostRow && hosting != 0;
    if (hosting != 0 && !hasHosting) { hosting->setVisible(false); hosting->setEnabled(false); }
    const MyGUI::IntCoord pc = pp->getCoord();
    pausemenu::Placement p;
    if (!pausemenu::Place(g_pmKenshi, block, bug != 0, hasHosting, pc.top, pc.height, &p))
    {
        if (hosting != 0) { hosting->setVisible(false); hosting->setEnabled(false); }   /* refused rows: HOSTING is not shown over OPTIONS */
        return r;
    }
    const int tops[6] = { p.newGame, p.saveGame, p.loadGame, p.options, p.exitGame, p.resume };
    for (int i = 0; i < 6; ++i)
        if (w[i]->getTop() != tops[i]) w[i]->setPosition(w[i]->getLeft(), tops[i]);
    if (bug != 0 && bug->getTop() != p.bug) bug->setPosition(bug->getLeft(), p.bug);
    if (hasHosting)
    {
        if (hosting->getTop() != p.hosting || hosting->getLeft() != w[3]->getLeft()) hosting->setPosition(w[3]->getLeft(), p.hosting);
        if (!hosting->getVisible()) hosting->setVisible(true);
        if (!hosting->getEnabled()) hosting->setEnabled(true);
        r += 32;
    }
    if (pc.top != p.panelTop || pc.height != p.panelHeight) pp->setCoord(MyGUI::IntCoord(pc.left, p.panelTop, pc.width, p.panelHeight));
    g_pmLast = p;
    return r + 4;
}
static int UiPauseArrangeCatching(void* panel)
{
    try { return UiPauseArrangeBody(panel); }
    catch (...) { return -1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiPauseArrangeGuarded(void* panel)
{
    __try { return UiPauseArrangeCatching(panel); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
static void UiPauseArrangeLog(int r)
{
    if (r == g_arrangeLastLogged) return;
    g_arrangeLastLogged = r;
    if (r < 0)
    {
        ErrorLog(std::string("[UI] pause menu arranging FAILED (") + (r == -3 ? "the panel holds no widget" : (r == -1 ? "a MyGUI throw" : "a fault"))
                 + ") - NEW GAME / SAVE GAME / LOAD GAME may be shown and clickable on this open; the save / load / new-game refusals still stand");
        return;
    }
    const bool block = (r & 1) != 0, bug = (r & 2) != 0, placed = (r & 4) != 0, hosting = (r & 32) != 0;
    const int found = (r / 8) & 3;
    const std::string rows = std::string(hosting ? "HOSTING, " : "") + (block ? "OPTIONS, EXIT GAME, " : "NEW GAME, SAVE GAME, LOAD GAME, OPTIONS, EXIT GAME, ")
                             + (bug ? "REPORT A BUG, " : "") + "RESUME";
    const std::string where = placed
        ? rows + " at pitch " + coopui::PanelNum((long long)g_pmLast.pitch) + ", gap " + coopui::PanelNum((long long)g_pmLast.gap) + " before "
              + (bug ? "REPORT A BUG and " : "") + "RESUME; the menu " + coopui::PanelNum((long long)g_pmLast.panelHeight) + " tall (Kenshi's "
              + coopui::PanelNum((long long)g_pmKenshi.panelHeight) + "), top " + coopui::PanelNum((long long)g_pmLast.panelTop)
        : std::string("rows NOT moved (a row not found under MainMenuPopupPanel, or its rows out of Kenshi's order)");
    if (block)
    {
        const std::string s = "[UI] pause menu in a multiplayer world: NEW GAME, SAVE GAME and LOAD GAME hidden and disabled (" + coopui::PanelNum((long long)found)
                              + " of 3 found); " + where + " (hiddenOpens=" + coopui::PanelNum(g_hiddenOpens) + ")";
        if (found == 3 && placed) DebugLog(s);
        else ErrorLog(s + " - a button not found stays clickable; the save / load / new-game refusals still stand");
    }
    else
    {
        const std::string s = "[UI] pause menu outside a multiplayer world: every button shown, Kenshi's rows where Kenshi put them; " + where;
        if (placed) DebugLog(s); else ErrorLog(s);
    }
}
int UiPauseMenuArrange(void* panel)
{
    if (panel == 0) return -3;
    const int r = UiPauseArrangeGuarded(panel);
    UiPauseArrangeLog(r);
    return r;
}
static void* detour_pauseMenuShow(void* panel)
{
    void* ret = orig_pauseMenuShow(panel);
    if (panel == 0) return ret;
    if (HandSaveBlocked()) ++g_hiddenOpens;
    BugReportPauseMenuShown(panel);   /* T-461: REPORT A BUG on its own row (its own SEH frame); it arranges the menu when it adds the row */
    UiPauseMenuArrange(panel);
    return ret;
}
long long UiHandSaveHiddenOpens() { return g_hiddenOpens; }
void InstallUiPauseMenuArrange()
{
    const uintptr_t target = (uintptr_t)AddrAbs(kT228MenuShow);
    if (target == 0)
    {
        ErrorLog("[UI] T-514 pause menu arranging=not-installed: MainMenuPopup_show has no address in this build's table - SAVE GAME /"
                 " LOAD GAME / NEW GAME stay shown and clickable in a multiplayer world (the save / load / new-game refusals still stand)");
        return;
    }
    const coop::HookStatus st = coop::AddHook((void*)target, (void*)&detour_pauseMenuShow, (void**)&orig_pauseMenuShow);
    if (st == coop::SUCCESS && orig_pauseMenuShow != 0)
        DebugLog("[UI] T-514 hook installed: MainMenuPopup_show 0x913250 - the pause menu's NEW GAME / SAVE GAME / LOAD GAME are hidden in a multiplayer world");
    else
        ErrorLog("[UI] T-514 pause menu arranging=not-installed: AddHook on MainMenuPopup_show 0x913250 FAILED - SAVE GAME / LOAD GAME /"
                 " NEW GAME stay shown and clickable in a multiplayer world (the save / load / new-game refusals still stand)");
}
/* 1 visible, 0 hidden, -2 the panel holds no widget, -1 a fault reading it. */
static int UiMenuVisiblePod(void* panel)
{
    __try
    {
        MyGUI::Widget* w = *(MyGUI::Widget**)((char*)panel + 8);
        if (w == 0) return -2;
        return w->getVisible() ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-461: the in-game pause menu, read and opened again through the engine's own getter and show (the in-game ESC route, the same
   calls as `uimenu ingame` below), for the REPORT A BUG window that sits over it: the engine's ESC closes the menu, and the
   window's ESC (its CANCEL) must leave the menu open. Visible: 1 open, 0 closed, -1 / -2 as UiMenuVisiblePod, -3 no rows / no
   panel. Reshow: 1 open now, 0 the engine's show declined (its guard at the top), -1 a fault, -3 no rows / no panel. MAIN THREAD. */
int UiPauseMenuVisible()
{
    const uintptr_t get = (uintptr_t)AddrAbs(kT228MenuGet);
    if (get == 0) return -3;
    int fault = 0;
    void* panel = UiMenuGetPod(get, &fault);
    if (panel == 0) return fault ? -1 : -3;
    return UiMenuVisiblePod(panel);
}
int UiPauseMenuReshow()
{
    const uintptr_t get = (uintptr_t)AddrAbs(kT228MenuGet), show = (uintptr_t)AddrAbs(kT228MenuShow);
    if (get == 0 || show == 0) return -3;
    int fault = 0;
    void* panel = UiMenuGetPod(get, &fault);
    if (panel == 0) return fault ? -1 : -3;
    const int before = UiMenuVisiblePod(panel);
    if (before == 1) return 1;
    if (before < 0) return -1;
    if (UiMenuShowPod(show, panel) != 1) return -1;
    return UiMenuVisiblePod(panel) == 1 ? 1 : 0;
}
/* T-524 (owner 451 / 454) - THE HOSTING WINDOW IN A WORLD: the title screen's own HOSTING screen (PanelBuild / PanelPush /
   PanelApply, the same widgets and words), built over the pause menu.  Its parent is a full-screen root of ours on the "Info"
   layer (Kenshi's message box's, above the pause menu; "Popup" when Info is unknown), which draws nothing and takes the
   mouse, so the menu under it cannot be clicked while the window is up; the panel centres itself on it as it does on the
   title art.  The pause menu stays open under it, so the game stays paused.  BACK, the window's X and ESC close it and leave
   the player on the pause menu: the engine's own ESC closes the menu (MyGUI hears the key first, then the engine's toggle -
   the REPORT A BUG window's finding), so a menu found closed while the window is up is that ESC, and the menu is opened
   again through the engine's own show.  Every open is HOSTING opened afresh (PanelHostingOpen: the internet address hidden,
   the router asked).  MAIN THREAD: the in-world pump's tail (UiDriveEngineClickFlush(0)). */
static const char* const kHostingRootName = "SWHostingLayer";
static int g_panelWorld = 0;                       /* 1 = the HOSTING window is open in a world */
static int g_rectHosting[4] = { -1, -1, -1, -1 };
static DWORD g_hwSettleMs = 0;
static int g_hwSettleW = 0, g_hwSettleH = 0;
static int g_hwFaults = 0, g_hwOff = 0, g_hwRootFailLogged = 0;
static volatile LONG64 g_hostingWorldOpened = 0, g_hostingWorldClosed = 0;
/* THE WORLD SERVER WATCHED FROM THE WORLD: the title's own poll (PanelPollNotebook, 4 Hz) run from the in-world tick too, so
   PanelHostingNow() turns false when this computer's world server stops while the host plays.  On that change: an open pause
   menu is arranged again (HOSTING hidden and disabled, the rows close up), and an open HOSTING window is closed onto the
   pause menu - no approved line says that the world server stopped mid-world (PanelHostingText's "The world couldn't
   start. Try again." is the title's start failure), so the window is closed rather than reworded, and the log says why.
   1 = the world server stopped on this call. */
static int g_hwServerGone = 0;   /* 1 = the stop was seen and the window / menu still have to follow it (UiHostingWorldBody) */
static int UiHostingServerWatch()
{
    const int before = g_nbState;
    PanelPollNotebook();
    if (before == coopui::kNbStoppedEarly || g_nbState != coopui::kNbStoppedEarly) return 0;
    DebugLog("[UI] T-524: this computer's world server stopped while this game is in its world (exit code " + coopui::PanelNum((long long)g_nbExitCode)
             + ") - HOSTING leaves the pause menu" + std::string(g_panelWorld != 0 ? " and the HOSTING window closes onto it (no approved words for this in a world)" : ""));
    return 1;
}
/* An open pause menu arranged again (its own guarded frame), so HOSTING follows PanelHostingNow() while the menu is up.  A menu
   never arranged in this process is left alone: Kenshi's menu getter would create it, and its first open arranges it anyway. */
static void UiPauseMenuRearrange()
{
    if (g_pmPanel == 0) return;
    const uintptr_t get = (uintptr_t)AddrAbs(kT228MenuGet);
    if (get == 0) return;
    int fault = 0;
    void* panel = UiMenuGetPod(get, &fault);
    if (panel != 0 && UiMenuVisiblePod(panel) == 1) UiPauseMenuArrange(panel);
}
static MyGUI::Widget* UiHostingRootMake(MyGUI::Gui* gui, int vw, int vh)
{
    const char* layers[2] = { kHlLayer, kHlLayerAlt };
    for (int k = 0; k < 2; ++k)
    {
        MyGUI::Widget* w = gui->createWidgetT(std::string("Widget"), std::string("PanelEmpty"), MyGUI::IntCoord(0, 0, vw, vh), MyGUI::Align::Default,
                                              std::string(layers[k]), std::string(kHostingRootName));
        if (w == 0) return 0;
        if (w->getLayer() != 0) return w;
        gui->destroyWidget(w);
    }
    return 0;
}
static void UiHostingWorldClose(MyGUI::Gui* gui, MyGUI::Widget* panel, MyGUI::Widget* root, const char* how)
{
    if (panel != 0) PanelDestroy(gui, panel);
    if (root != 0) gui->destroyWidget(root);
    ::InterlockedExchange(&g_panelWanted, 0);
    g_panelWorld = 0;
    g_panelScreen = 0;
    g_hwSettleMs = 0;
    UiRectForget(g_rectHosting);
    ::InterlockedIncrement64(&g_hostingWorldClosed);
    DebugLog(std::string("[UI] T-524: the HOSTING window closed (") + how + ") - the pause menu is under it");
}
static void UiHostingWorldOpen()
{
    if (!HandSaveBlocked() || !PanelHostingNow() || UiPauseMenuVisible() != 1)
    {
        DebugLog("[UI] T-524: HOSTING pressed, but this game does not host a multiplayer world now or the pause menu is closed - nothing opened");
        return;
    }
    PanelDefaults();
    g_panelFailStreak = 0;
    g_profDlg = 0;
    ::InterlockedExchange(&g_uiAction, (LONG)kActNone);
    PanelHostingOpen();
    ::InterlockedExchange(&g_panelWanted, 1);
    g_panelWorld = 1;
    UiRectForget(g_rectHosting);
    ::InterlockedIncrement64(&g_hostingWorldOpened);
    DebugLog("[UI] T-524: HOSTING pressed on the pause menu - the HOSTING window opens over it (the game stays paused)");
}
static void UiHostingWorldBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    if (::InterlockedExchange(&g_hostingAsk, 0) != 0 && g_panelWorld == 0) UiHostingWorldOpen();
    if (g_panelWorld == 0) return;
    const UiNames& n = NM();
    const LONG act = ::InterlockedExchange(&g_uiAction, (LONG)kActNone);
    MyGUI::Widget* panel = gui->findWidgetT(n.panel, false);
    MyGUI::Widget* root = gui->findWidgetT(std::string(kHostingRootName), false);
    if (g_hwServerGone != 0)   /* the world server stopped (UiHostingServerWatch): the window closes, the menu loses HOSTING */
    {
        g_hwServerGone = 0;
        UiHostingWorldClose(gui, panel, root, "the world server stopped");
        UiPauseMenuRearrange();
        return;
    }
    const bool built = panel != 0 && ::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0;
    if (built && UiPauseMenuVisible() == 0)
    {
        UiHostingWorldClose(gui, panel, root, "ESC");
        const int rr = UiPauseMenuReshow();
        if (rr != 1) ErrorLog("[UI] T-524: the pause menu could not be opened again after ESC over the HOSTING window (" + coopui::PanelNum((long long)rr) + ")");
        return;
    }
    if (act == kActBack || act == kActClose) { UiHostingWorldClose(gui, panel, root, act == kActBack ? "BACK" : "the window's X or ESC"); return; }
    if (panel != 0) PanelMirror(gui, act != kActNone ? 1 : 0);
    if (act != kActNone) { ::InterlockedIncrement64(&g_uiActionsRun); PanelApply(gui, (int)act, panel != 0 ? 1 : 0); }
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0 || g_panelScreen != 5) { UiHostingWorldClose(gui, panel, root, "it left the HOSTING screen"); return; }
    if (panel != 0 && !built) { PanelDestroy(gui, panel); panel = 0; }
    int vw = 1280, vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& vs = rm->getViewSize(); if (vs.width > 0 && vs.height > 0) { vw = vs.width; vh = vs.height; } }
    /* The window resized: once the new size has held still for kPanelResizeSettleMs (the title panel's settle rule), the root
       and the panel are made again at it. */
    if (root != 0 && (root->getWidth() != vw || root->getHeight() != vh))
    {
        const DWORD now = ::GetTickCount();
        if (g_hwSettleMs == 0 || vw != g_hwSettleW || vh != g_hwSettleH) { g_hwSettleMs = (now == 0) ? 1 : now; g_hwSettleW = vw; g_hwSettleH = vh; }
        else if ((DWORD)(now - g_hwSettleMs) >= (DWORD)coopui::kPanelResizeSettleMs)
        {
            g_hwSettleMs = 0;
            if (panel != 0) PanelDestroy(gui, panel);
            gui->destroyWidget(root);
            panel = 0;
            root = 0;
            UiRectForget(g_rectHosting);
        }
    }
    else g_hwSettleMs = 0;
    if (root == 0)
    {
        root = UiHostingRootMake(gui, vw, vh);
        if (root == 0)
        {
            if (g_hwRootFailLogged++ == 0) ErrorLog("[UI] T-524: the HOSTING window's layer could not be made (neither Info nor Popup) - the window is not shown");
            if (panel != 0) PanelDestroy(gui, panel);
            UiHostingWorldClose(gui, 0, 0, "its layer could not be made");
            return;
        }
    }
    if (panel == 0)
    {
        PanelBuild(gui, root);
        if (::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0) panel = gui->findWidgetT(n.panel, false);
        if (panel != 0) UiFocusPanel(gui);   /* the window's own key handler hears ESC first */
    }
    if (panel != 0)
    {
        PanelPushStatus(gui, act != kActNone ? 1 : 0);
        UiLogRect("hosting-window", panel, g_rectHosting);
    }
}
static int UiHostingWorldCatching()
{
    try { UiHostingWorldBody(); return 0; }
    catch (...) { return 1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiHostingWorldGuarded()
{
    __try { return UiHostingWorldCatching(); }
    __except (UiFilter(GetExceptionCode())) { return 2; }
}
/* After a fault the window is closed; a memory fault, or a third fault of any kind, turns the window off for the process (the
   HOSTING row still opens nothing, and says so in the log). */
static void UiHostingWorldDropBody()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    MyGUI::Widget* root = gui->findWidgetT(std::string(kHostingRootName), false);
    if (root != 0) gui->destroyWidget(root);
}
static int UiHostingWorldDropCatching()
{
    try { UiHostingWorldDropBody(); return 0; }
    catch (...) { return 1; }
}
// The SEH frame. No local object with a destructor may appear here, and none does.
static int UiHostingWorldDropGuarded()
{
    __try { return UiHostingWorldDropCatching(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}
static void UiHostingWorldTick()
{
    if (UiHostingServerWatch() != 0)
    {
        if (g_panelWorld != 0 && g_hwOff == 0) g_hwServerGone = 1;   /* the window closes inside its guarded frame first */
        else UiPauseMenuRearrange();
    }
    if (g_hwOff != 0)
    {
        if (::InterlockedExchange(&g_hostingAsk, 0) != 0) DebugLog("[UI] T-524: HOSTING pressed, but the HOSTING window is off after a fault this game - nothing opened");
        return;
    }
    if (g_panelWorld == 0 && g_hwServerGone == 0 && ::InterlockedCompareExchange(&g_hostingAsk, 0, 0) == 0) return;
    const int r = UiHostingWorldGuarded();
    if (r == 0) return;
    const unsigned long code = (unsigned long)(LONG)g_uiLastCode;
    ++g_hwFaults;
    if (r == 2 && UiIsMemoryFault(code) != 0) g_hwFaults = 3;
    ::InterlockedExchange(&g_panelBuiltOk, 0);
    ::InterlockedExchange(&g_panelWanted, 0);
    g_panelWorld = 0;
    g_panelScreen = 0;
    (void)UiHostingWorldDropGuarded();
    if (g_hwFaults >= 3) g_hwOff = 1;
    char b[240];
    _snprintf(b, 239, "[UI] T-524: the HOSTING window faulted (%s, code 0x%08lX) - it is closed%s", r == 1 ? "a C++ throw" : "an SEH fault", code,
              g_hwOff != 0 ? " and off for the rest of this game" : "; HOSTING opens it again");
    b[239] = 0;
    ErrorLog(b);
}

static bool UiMenuCommandBody(const std::string& args)
{
    const uintptr_t get = (uintptr_t)AddrAbs(kT228MenuGet), show = (uintptr_t)AddrAbs(kT228MenuShow);
    const std::string why = coopui::UiMenuRefusal(args, (get != 0 && show != 0) ? 1 : 0, GameplayRunning() ? 1 : 0);
    if (!why.empty()) { DebugLog("[UI] uimenu " + args + " TEST-ONLY REFUSED - " + why); return false; }
    int fault = 0;
    void* panel = UiMenuGetPod(get, &fault);
    if (panel == 0)
    {
        ErrorLog(std::string("[UI] uimenu ingame TEST-ONLY REFUSED - the engine's menu getter ") + (fault ? "faulted" : "returned no panel") + "; the show was not called");
        return false;
    }
    const int before = UiMenuVisiblePod(panel);
    if (before == 1) { DebugLog("[UI] uimenu ingame TEST-ONLY: the in-game menu opened (it was already open; the engine's show was not called again)"); return true; }
    if (before < 0) { ErrorLog("[UI] uimenu ingame TEST-ONLY REFUSED - the menu panel's widget could not be read (" + std::string(before == -2 ? "null" : "a fault") + "); the show was not called"); return false; }
    const int rc = UiMenuShowPod(show, panel);
    const int after = UiMenuVisiblePod(panel);
    if (rc == 1 && after == 1)
    {
        DebugLog("[UI] uimenu ingame TEST-ONLY: the in-game menu opened (the engine's own getter + show, the in-game ESC route; no injected input)");
        return true;
    }
    char b[260];
    _snprintf(b, 259, "[UI] uimenu ingame TEST-ONLY REFUSED - the engine's show %s and the menu is %s (visible=%d); the engine's own guard at the top of the show may have declined it",
              rc == 1 ? "returned" : "faulted", after == 1 ? "open" : "not open", after);
    b[259] = 0;
    ErrorLog(b);
    return false;
}
/* T-510 - TEST-ONLY `hostaddr fake <ip>` | `hostaddr show` | `hostaddr hide`.  fake: the INTERNET ADDRESS row takes <ip> as if
   the router had reported it (a bench's router may not answer), so a screenshot run can show the SHOW state; it replaces an
   ask still running.  show / hide: SHOW / HIDE without a click (`uiclick hostaddrshow` is the player's road).  The address
   is never logged. */
bool UiHostAddrCommand(const std::string& args)
{
    std::string a, rest;
    coopui::UiDriveSplit(args, &a, &rest);
    if (a == "fake")
    {
        std::string kind, clean;
        coopupnp::RouterAddressPublic(rest, &kind, &clean);
        if (kind == "unreadable") { DebugLog("[UI] hostaddr fake REFUSED - not a dotted IPv4 address"); return false; }
        g_netAddrAskId = 0;
        g_netAddrLookId = 0;
        g_netAddrIp = clean;
        g_netAddrState = coopui::kNetAddrFound;
        g_netAddrPending = coopui::kNetPressNone;
        g_netAddrPushed = 0;
        DebugLog("[UI] internet address: found (TEST-ONLY hostaddr fake, a " + kind + " address)");
        return true;
    }
    if ((a == "show" || a == "hide") && rest.empty())
    {
        if (coopui::PanelNetAddrHave(g_netAddrState, g_netAddrIp) == 0) { DebugLog("[UI] hostaddr " + a + " REFUSED - no internet address"); return false; }
        g_netAddrShown = (a == "show") ? 1 : 0;
        g_netAddrPushed = 0;
        DebugLog("[UI] internet address " + std::string(g_netAddrShown != 0 ? "shown" : "hidden") + " (TEST-ONLY hostaddr " + a + ")");
        return true;
    }
    DebugLog("[UI] hostaddr REFUSED - usage: hostaddr fake <ip> | hostaddr show | hostaddr hide");
    return false;
}

bool UiMenuCommand(const std::string& args)
{
    try { return UiMenuCommandBody(args); }
    catch (...) { DebugLog("[UI] uimenu " + args + " - a C++ exception; nothing more done"); return false; }
}
/* T-201 PP6' - THE LOAD ITSELF, at the tail of a title frame (after the engine's own title update, as a queued engine click), once
   our panel's widgets are gone. The load is posted only with the profile folder's quick.save present NOW - the engine's own
   saveExists test - because a load that fails after the pump deleted the title is the T243 crash; posted = +0xA0 reads 2. */
static bool UiEngineNewGameClick(const char* why)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    std::string miss;
    MyGUI::Widget* w = gui != 0 ? UiDriveEngine(gui, "NewGameButton", 0, &miss) : 0;
    if (w == 0) { ErrorLog(std::string("[UI] NEW GAME window NOT shown (") + why + "): the NEW GAME button was not found - " + miss); return false; }
    w->eventMouseButtonClick(w);   /* the button's own delegate - Confirmed by run T568: this click opened Kenshi's NEW GAME window (the joiner's one-press NEW GAME); that the delegate is NewGameWindow's show 0x916080 is still not read in a decompile */
    DebugLog(std::string("[UI] NewGameWindow shown by ") + why);
    return true;
}
/* atTitle: 1 = after the title's own update (coop.cpp's title hook), 0 = the in-world tail - which never posts: the title that decided is gone. */
static void LoadTailAct(int atTitle)
{
    if (atTitle != 0 && StoreNewGameReshowTake() != 0) UiEngineNewGameClick("the refused NEW GAME (PP5 gate, F1)");
    if (g_loadStage != 3) return;
    if (atTitle == 0)   /* T-220: reachable - between the panel closing (stage 3) and this tail the title menu is live, so the engine's own
                           LOAD / CONTINUE can take the game in-world first. The press ends as any other failed press: box D, the world left. */
    {
        /* PP6 fold-2 F2: unless it took the decided save - the open save (SaveFileSystem+0x120, the PP5 redirect's read) is g_loadFolder:
           the press is done (the right save is loading), no box, the world stays. */
        char ob[260]; ob[0] = 0;
        const int openRead = StoreOpenSaveName(ob, 260);
        ob[259] = 0;
        if (coopprof::OpenSaveIsDecidedFolder(openRead == 1, std::string(ob), g_loadFolder))
        {
            g_loadStage = 0; g_loadMode = 0; g_profBusy = 0;
            DebugLog("[AUTOLOAD] the game's own CONTINUE / LOAD took the decided save '" + std::string(ob) + "' (folder '" + g_loadFolder
                     + "') before the " + std::string(g_loadVerdict == coopprof::kAutoLoadLoad ? "load" : "NEW GAME window") + " was posted - the press is done, the world stays");
            return;
        }
        LoadFail(coopui::kLoadLinkLostText, "CAN'T LOAD", "the title screen is gone before the " + std::string(g_loadVerdict == coopprof::kAutoLoadLoad ? "load" : "NEW GAME window")
                 + " - nothing posted");   /* owner 176 D */
        return;
    }
    /* T-201 PP6' fold (LOW 7): a re-link that reopened the panel since the decision (UiProfilesPoll) - never post under it. */
    if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) != 0)
    { LoadFail(coopui::kLoadLinkLostText, "CAN'T LOAD", "the panel was opened again (a re-link) after the load was decided - nothing posted"); return; }   /* owner 176 D */
    const DWORD waited = (DWORD)(::GetTickCount() - g_loadAtMs);
    if (::InterlockedCompareExchange(&g_panelBuiltOk, 0, 0) != 0)
    {
        if (waited < 5000) return;
        LoadFail(coopui::kLoadFailText, "CAN'T LOAD", "the panel did not close within 5 s");   /* owner 176 A */
        return;
    }
    std::string folder, prof;
    int found = 0, quick = 0, never = 0;
    if (StoreAutoLoadFacts(&folder, &found, &quick, &never, &prof) == 0 || folder != g_loadFolder)
    { LoadFail(coopui::kLoadLinkLostText, "CAN'T LOAD", "the world's link or the pick changed before the load"); return; }   /* owner 176 D */
    if (g_loadVerdict == coopprof::kAutoLoadLoad)
    {
        if (quick == 0) { LoadFail(coopui::kSaveNotHereText, "CAN'T LOAD", "quick.save of '" + folder + "' is gone at the moment of the load (T243 pre-check)"); return; }
        const int posted = PostLoadChecked(folder);
        if (posted == 1)
        {
            g_loadStage = 0; g_loadMode = 0; g_profBusy = 0;
            DebugLog("[AUTOLOAD] load posted (code 2) folder='" + folder + "' profile='" + prof + "'");
            return;
        }
        /* item 6: a pending request (0) is waited on - SaveManager+0xA0 read every title frame; -1 / -2 will not change: box A at once. */
        if (coopui::LoadPostStep(posted == 0 ? 0 : -1, (unsigned)waited) == coopui::kPostWait)
        {
            if (g_loadTries++ == 0) DebugLog("[AUTOLOAD] load of '" + folder + "' waits: a save request is pending (SaveManager+0xA0) - posted when it returns to 0");
            return;
        }
        LoadFail(coopui::kLoadFailText, "CAN'T LOAD", "the load of '" + folder + "' could not be posted (" + coopui::PanelNum((long long)posted)
                 + (posted == -1 ? ": no SaveManager, anySavesExist() false or +0xA0 unreadable" : posted == -2 ? ": load() posted nothing" : ": a save request stayed pending") + ")");   /* owner 176 A */
        return;
    }
    if (!UiEngineNewGameClick("autoload"))   /* the world's settings are locked in the window by settings.cpp (NgLock / detour_smNewGame) */
    { LoadFail(coopui::kLoadFailText, "CAN'T LOAD", "the NEW GAME button was not found"); return; }   /* owner 176 A: the world is left */
    g_loadStage = 0; g_loadMode = 0; g_profBusy = 0;
}
void UiDriveEngineClickFlush(int atTitle)
{
    if (atTitle == 0) UiHostingWorldTick();   /* T-524: the HOSTING window over the pause menu - this is the in-world pump's tail */
    try { LoadTailAct(atTitle); }
    catch (...) { DebugLog("[AUTOLOAD] a C++ exception at the end of the frame; nothing more done"); }
    if (g_driveEngineQueued.empty()) return;
    const std::string name = g_driveEngineQueued;
    g_driveEngineQueued.clear();   /* cleared FIRST: a handler that throws or re-enters never fires it twice */
    bool ok = false;
    try { ok = UiDriveEngineClickFire(name); }
    catch (...) { DebugLog("[UI] uiclick " + name + " - a C++ exception at the end of the frame; nothing more done"); ok = false; }
    if (!ok) CommandStatusLate("error uiclick");
}

/* settings5 S5 - A MODS REFUSAL IS SAID ON THE TITLE SCREEN. store.cpp keeps the sentence (StoreModsRefusal); this raises it
   in the title-screen notice (CoopNoticeText) and the panel's status area when it changes, and takes the notice down when
   a later WELCOME lets the game in. Outside the SEH frame (it builds strings - C2712). */
static long long g_modsRefusalShown = -1;
static int g_modsNoticeRaised = 0;
static std::string g_modsNoticeText;   /* settings5 fold: what WE put in the notice, so clearing never takes down another feature's */
static void UiModsRefusalPoll()
{
    long long seq = 0;
    const std::string text = StoreModsRefusal(&seq);
    if (seq == g_modsRefusalShown) return;
    g_modsRefusalShown = seq;
    if (!text.empty() && g_pressBusy != 0) { DebugLog("[MODS] T-201 N1 fold: a refusal during a press - PressTick's box says it, no second notice"); return; }
    if (!text.empty()) { /* mp1: the panel's status says it through PanelLiveBlock's refusal line */ UiRaiseNotice(text); g_modsNoticeText = text; g_modsNoticeRaised = 1; DebugLog("[MODS] the refusal is on the title screen"); }
    else if (g_modsNoticeRaised != 0) { if (g_noticeText == g_modsNoticeText) UiClearNotice(); g_modsNoticeRaised = 0; }
}

/* prof3 - THE WORLD'S PROFILES ANSWER, HANDED TO THE PANEL (item 7 of the brief).  StoreOnProfiles runs in PumpLink on the MAIN
   thread and so does this title tick, so the hand-over is a copy and a change number - no lock - and MyGUI is touched only by
   the tick's own PanelPush (g_profPushWanted).  Outside the SEH frame: it builds strings (C2712). */
static void UiProfilesPoll()
{
    if (StoreProfilesPanelDriven() == 0) return;
    if (g_profMode == 1 && g_panelScreen == 7) return;   /* T-201 PP6': PROFILES from CHANGE shows the world's file - an answer waits */
    if (g_pressBusy != 0) return;   /* T-201 N1 fold (finding 4): a press is waiting - the list is held (its change unread) until PressTick decides; then host -> HOSTING first, joiner -> PROFILES. T552: the list's arrival is itself what ends the press (StoreLobbyAnswerGen), so the hold lasts one tick */
    std::vector<StoreProfRow> rows;
    unsigned cap = 0, select = 0;
    std::string say;
    int verdict = 0;
    const long long seq = StoreProfilesPanel(&rows, &cap, &select, &say, &verdict);
    if (seq == 0 || seq == g_profSeqSeen) return;
    g_profSeqSeen = seq;
    g_profList = rows;
    g_profCapUi = cap;
    g_profBusy = 0;
    g_profSay = say;
    if (select != 0) g_profSel = select;
    if (PanelProfPicked() == 0)   /* T-201 PP6' (owner 159): the profile played last is picked (was: the first row) */
    {
        unsigned last = 0;
        g_profSel = coopprof::AutoPickDecide(PanelProfRowsAsProf(), 0, &last) == coopprof::kAutoPick ? last : 0u;
    }
    ++g_profListGen;   /* ui2: the list on screen is refilled, and its pick set, on the push this answer asks for */
    if (g_loadStage == 1 && g_loadMode == 1)   /* T-201 PP6' (owner 134): HOST picks by itself - PROFILES is not shown */
    {
        LoadHostPickStep(select, verdict, say);
        g_profPushWanted = 1;
        return;
    }
    if (coopui::LoadPickRefused(g_loadStage, g_loadMode, verdict) != 0)   /* T-201 PP6' fold (MED 4): JOIN's PLAY refused - the wait ends now, in the world's words */
    {
        ::InterlockedIncrement64(&g_profRefusedShown);
        LoadFail(say.empty() ? std::string(coopui::kLoadFailText) : say, "CAN'T JOIN", "the world refused the pick (verdict " + coopui::PanelNum((long long)verdict) + ")");
        g_profPushWanted = 1;
        return;
    }
    if (verdict != 0)
    {
        ::InterlockedIncrement64(&g_profRefusedShown);
        if (verdict == 1) ::InterlockedIncrement64(&g_profRefusedAtCap);   /* coopprof::kRefusedCap */
    }
    const int waiting = StoreProfilesWaiting();
    DebugLog("[UI] prof3: profiles in - " + coopui::PanelNum((long long)g_profList.size()) + " of cap " + coopui::PanelNum((long long)cap)
             + " (change " + coopui::PanelNum(seq) + ")" + (waiting ? std::string() : std::string(" - nothing waits for a pick")));
    if (waiting != 0)
    {
        if (::InterlockedCompareExchange(&g_panelWanted, 0, 0) == 0)
        {
            PanelDefaults();
            g_panelFailStreak = 0;
            PanelProfOpen();
            ::InterlockedExchange(&g_panelWanted, 1);   /* the panel comes back up for the pick */
        }
        /* The host's first list leaves the Hosting screen up (its address is what the friends need), with Choose my profile
           lit; every other case, and every later answer, shows the profiles. */
        else if (!(g_panelScreen == 5 && g_profEverShown == 0) && g_panelScreen != 3 && g_panelScreen != 4 && g_panelScreen != 6)
            PanelProfOpen();
        /* T-201 PP6' (owner 159): a joiner with no profile in this world gets the NEW PROFILE box, filled with PLAYER NAME - once. */
        if (g_panelMode == 1 && g_panelScreen == 7 && g_profList.empty() && g_profDlg == 0 && g_profPrefillDone == 0
            && coopui::PanelProfileNewAllowed(0u, cap) != 0)
        {
            g_profPrefillDone = 1;
            g_fNewWorld = coopcfg::CfgTrim(g_fName);
            g_dlgText = "Enter a profile name (up to 24 characters).";
            g_profDlg = 1;
            DebugLog("[UI] T-201 PP6': no profile of this player in the world - the NEW PROFILE box, filled with '" + g_fNewWorld + "'");
        }
    }
    g_profPushWanted = 1;
}

void UiTitleTick()
{
    if (g_uiDisabled != 0) return;
    UiModsRefusalPoll();
    UiProfilesPoll();   /* prof3 */
    if (UiTitleTickGuarded() == 0) return;
    ::InterlockedIncrement64(&g_uiFaulted);
    {
        const unsigned long code = (unsigned long)(LONG)g_uiLastCode;
        const int mem = UiIsMemoryFault(code);
        long long strikes = 0;
        if (code == kUiCppThrowCode) ::InterlockedIncrement64(&g_uiCppThrow);
        if (mem == 0) strikes = (long long)::InterlockedIncrement64(&g_uiSoftStrikes);
        {
            /* A memory fault disables at once.  Anything else is retried and latches only after
               kUiSoftStrikeCap of them - 6a lesson 14's shape (a corrective must verify its own effect and give
               up) rather than one-strike-and-out.  The cap also bounds this line: at 1 kHz, an unlatched fault
               that logged every frame would be its own defect. */
            const int latch = (mem != 0 || strikes >= kUiSoftStrikeCap) ? 1 : 0;
            char b[420];
            _snprintf(b, 419,
                      "[UI] a MyGUI call faulted: faultCode=0x%08lX class=%s softStrikes=%lld -> %s."
                      " 0xC0000005 and kin are a MEMORY fault and disable the title-screen button for the rest of"
                      " this process; 0xE06D7363 is a C++ throw, which is how MyGUI reports an ordinary recoverable"
                      " condition (an unregistered skin on one frame, the same delegate added twice), so it is"
                      " retried and latches only after %lld of them. uiCppThrow counts that class on its own.",
                      code,
                      mem ? "memory-fault" : (code == kUiCppThrowCode ? "cpp-throw" : "other"),
                      strikes, latch ? "DISABLED" : "retrying on the next title frame",
                      (long long)kUiSoftStrikeCap);
            b[419] = 0;
            ErrorLog(b);
            if (latch) { ::InterlockedExchange(&g_uiDisabled, 1); StoreProfilesPanelGone(); UiLatchPutBack(); }   /* review-prof3 D1: a pick waiting on the screen must not wait forever; ui5c fold F3: the title column back, ESC the game's again */
        }
    }
}

std::string UiReportToken()
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "ui[created=" << (long long)g_uiCreated
       << ",clicks=" << (long long)g_uiClicks
       << ",noGuiTicks=" << (long long)g_uiNoGui
       << ",noExitButtonTicks=" << (long long)g_uiNoExit
       << ",noParentTicks=" << (long long)g_uiNoParent
       << ",noRoomTicks=" << (long long)g_uiNoRoom
       << ",buttonsMoved=" << (long long)g_uiBtnsMoved
       << ",buttonsReshifted=" << (long long)g_uiBtnsReshifted
       << ",buttonsRepitched=" << (long long)g_uiBtnsRepitched
       << ",buttonOrderFallback=" << (long long)g_uiOrderFallback
       << ",buttonOrderRestored=" << (long long)g_uiOrderRestored
       << ",faulted=" << (long long)g_uiFaulted
       << ",uiCppThrow=" << (long long)g_uiCppThrow
       << ",softStrikes=" << (long long)g_uiSoftStrikes
       << ",lastFaultCode=0x" << std::hex << (unsigned long)(LONG)g_uiLastCode << std::dec
       << ",disabled=" << (long)g_uiDisabled
       << ",createNull=" << (long long)g_uiCreateNull
       << ",refusedCaption=" << (long long)g_uiRefused
       << ",refusedNotes=" << (long long)g_uiRefuseTipShows   /* ui98 */
       << ",btnUnwired=" << (long long)g_uiBtnUnwired
       << ",actionsQueued=" << (long long)g_uiActionsQueued
       << ",actionsRun=" << (long long)g_uiActionsRun
       << ",panelOpened=" << (long long)g_panelOpened
       << ",panelBuilt=" << (long long)g_panelBuilt
       << ",panelDestroyed=" << (long long)g_panelDestroyed
       << ",panelSaved=" << (long long)g_panelSaved
       << ",panelInvalid=" << (long long)g_panelInvalid
       << ",panelCastNull=" << (long long)g_panelCastNull
       << ",panelMissing=" << (long long)g_panelMissing
       << ",panelNoRoomTicks=" << (long long)g_panelNoRoom
       << ",textMeasureFallback=" << (long long)g_uiTextMeasureFallback
       << ",panelNoClient=" << (long long)g_panelNoClient
       << ",panelBuildFail=" << (long long)g_panelBuildFail
       << ",panelPasted=" << (long long)g_panelPasted
       << ",panelPasteFailed=" << (long long)g_panelPasteFailed
       << ",hostingShown=" << (long long)g_hostingShown   /* mp4 */
       << ",homeAddrFound=" << (long long)g_homeAddrFound
       << ",homeAddrMissing=" << (long long)g_homeAddrMissing
       << ",addrCopied=" << (long long)g_addrCopied
       << ",addrCopyFailed=" << (long long)g_addrCopyFailed
       << ",netAddrFound=" << (long long)g_netAddrFound   /* T-510 */
       << ",netAddrMissing=" << (long long)g_netAddrMissing
       << ",netAddrShowPressed=" << (long long)g_netAddrShowPressed
       << ",netAddrLookAsked=" << (long long)g_netAddrLookAsked
       << ",netAddrLookFound=" << (long long)g_netAddrLookFound
       << ",netAddrLookFailed=" << (long long)g_netAddrLookFailed
       << ",hostingWorldOpened=" << (long long)g_hostingWorldOpened
       << ",hostingWorldClosed=" << (long long)g_hostingWorldClosed
       << ",gameOptionsOpened=" << (long long)g_optOpened   /* mp5 */
       << ",gameOptionsDone=" << (long long)g_optDoneCount
       << ",gameOptionsHandedOver=" << (long long)g_optHandedOver
       << ",gameOptionsFileRead=" << (long long)g_optFileRead
       << ",panelEscClosed=" << (long long)g_panelEscClosed
       << ",panelEngineClosed=" << (long long)g_panelEngineClosed
       << ",panelXClosed=" << (long long)g_panelXClosed
       << ",panelTooSmall=" << (long long)g_panelTooSmall
       << ",panelBackdrop=" << (long long)g_panelBackdrop
       << ",panelBackdropFailed=" << (long long)g_panelBackdropFailed
       << ",panelBackdropReused=" << (long long)g_panelBackdropReused
       << ",panelResizeSettling=" << (long long)g_panelResizeSettling
       << ",panelArmBusy=" << (long long)g_panelArmBusy
       << ",panelGrown=" << (long long)g_panelGrown
       << ",panelResized=" << (long long)g_panelResized
       << ",menuHidden=" << (long long)g_uiMenuHidden   /* ui5c: EVENTS, except menuRehidden (BUTTONS) */
       << ",menuRestored=" << (long long)g_uiMenuRestored
       << ",menuRehidden=" << (long long)g_uiMenuRehidden
       << ",menuForgot=" << (long long)g_uiMenuForgot
       << ",panelGaveUp=" << (long long)g_panelGaveUp
       << ",noticeBuilt=" << (long long)g_noticeBuilt
       << ",noticeDestroyed=" << (long long)g_noticeDestroyed
       << ",noticeGaveUp=" << (long long)g_noticeGaveUp
       << ",boxBlockBuilt=" << (long long)g_boxBlockBuilt   /* ui6: EVENTS */
       << ",boxBlockDropped=" << (long long)g_boxBlockDropped
       << ",boxBlockFailed=" << (long long)g_boxBlockFailed
       << ",boxBlockedClicks=" << (long long)g_boxBlockedClicks
       << ",boxEscAnswered=" << (long long)g_boxEscAnswered
       /* U2.  armRequested is what the player asked for; armStarted is what ConfigTitleTick actually
          did.  They are equal at rest or a request was dropped - the actionsQueued/actionsRun shape. */
       << ",armRequested=" << (long long)g_panelArmRequested
       << ",armStarted=" << (long long)ConfigArmGen()
       << ",armRefused=" << (long long)g_panelArmRefused
       << ",armLate=" << (long long)g_panelArmLate
       << ",press[started,done,failed,cancelled,ignored]=" << (long long)g_pressStarted << "," << (long long)g_pressDone << ","
       << (long long)g_pressFailed << "," << (long long)g_pressCancelled << "," << (long long)g_pressIgnored   /* T-201 N1 */
       << ",dials=" << (long long)ConfigDialCount()
       << ",nbSpawned=" << (long long)g_panelNbSpawned
       << ",nbSpawnFailed=" << (long long)g_panelNbSpawnFail
       << ",nbCmdTooLong=" << (long long)g_panelNbCmdTooLong
       << ",nbNoExe=" << (long long)g_panelNbNoExe
       << ",nbAlready=" << (long long)g_panelNbAlready
       << ",nbExited=" << (long long)g_panelNbExited
       << ",nbExitCode=" << (long long)g_nbExitCode
       << ",linkState=" << coopui::PanelLinkStateName((int)::InterlockedCompareExchange(&g_panelStatusState, 0, 0))
       /* mp3: the Host a game screen's worlds - listed/skipped at the last scan, the rest are events */
       << ",worldScans=" << (long long)g_worldScans
       << ",worldsListed=" << (long long)g_worldsListed
       << ",worldsSkipped=" << (long long)g_worldsSkipped
       << ",worldsCreated=" << (long long)g_worldsCreated
       << ",worldCreateFailed=" << (long long)g_worldCreateFailed
       << ",worldsDeleted=" << (long long)g_worldsDeleted
       << ",worldDeleteRefused=" << (long long)g_worldDeleteRefused
       << ",worldDeleteFailed=" << (long long)g_worldDeleteFailed
       /* prof3: the Your profiles screen (events) */
       << ",profilesShown=" << (long long)g_profilesShown
       << ",picked=" << (long long)g_profPicked
       << ",newAsked=" << (long long)g_profNewAsked
       << ",deleteConfirmed=" << (long long)g_profDeleteConfirmed
       << ",refusedAtCap=" << (long long)g_profRefusedAtCap
       << ",refusedShown=" << (long long)g_profRefusedShown
       << ",panelOpen=" << (long)::InterlockedCompareExchange(&g_panelWanted, 0, 0)
       << "]";
    return ss.str();
}

}
