// ui.h - E43 / P7z: the plugin's first player-facing control.
//
// One button on the title screen. See ui.cpp for the lifetime rule that governs everything here.
#pragma once

#include <string>

namespace MyGUI { class Widget; }

namespace coop {

// Called from detour_titleUpdate (coop.cpp) on EVERY title frame, and cheap in the steady state:
// one lookup by exact name and nothing else once the button exists. Never call it from any other
// thread - MyGUI is not thread-safe and the title pump is the menu-time main thread (P005).
void UiTitleTick();
// T-220: the live TitleScreen, noted by detour_titleUpdate each title frame BEFORE the command channel runs - the TEST-ONLY
// `uiclick escape` makes the engine's ESC-arm call (TitleScreen::closeTheOtherBits) with it. Title pump only.
void UiTitleScreenNote(void* title);

// P8i-b (review-p8i H-1): detour TitleScreen::closeTheOtherBits so ESC at the title screen closes the
// Multiplayer panel instead of raising the engine's quit byte. Called ONCE from startPlugin, before any
// title screen exists. Safe when the panel has never been opened: the detour calls the original first
// and only widens its answer. See ui.cpp for the decompile this rests on.
void InstallUiTitleClose();

// One token for the [P005] periodic title-pump line. Locale-safe: RE_Kenshi imbues a process-wide
// locale that inserts digit-group separators into numbers (F030).
std::string UiReportToken();
// mmo5 (e47-mmo-design.md 6): the host-left window, drawn in a RUNNING WORLD. MAIN THREAD only (store.cpp OwnHostLeftTick).
// Built on the first call, updated after. 1 shown/updated, 0 refused (no Gui, a null create or cast, no client area),
// -1 a C++ throw inside MyGUI, -2 a fault. The caller falls back to the game's message line on anything but 1.
int UiHostLeftShow(const char* title, const char* line, int buttonEnabled);   /* ui1: the DISCONNECTED dialog (Kenshi's message-box style) */
// 1 once per click of the window's one button, Exit game (MyGUI calls the delegate on the main thread).
int UiHostLeftExitClicked();
// mmo5 fold: take the host-left window down (the host came back). 1 closed, 0 none, -1 a MyGUI throw, -2 a fault.
int UiHostLeftClose();
// ui1: the layer the dialog was built on - "Info", or "Popup" when Info is unknown; "none" before the first build.
const char* UiHostLeftLayer();
// T-246 (owner 199 b / 200): the MODS DON'T MATCH notice, drawn in a RUNNING WORLD (the host-left window's recipe, one OK that
// only closes it, no pause). MAIN THREAD only (store.cpp ModsWarnTick). Show: 1 built (T-246 fold 2: always anew - a window
// already there is stale and dropped first), 0 refused, -1 a MyGUI throw, -2 a fault. OkClicked: 1 once per click. Close: 1
// closed, 0 none, -1 / -2 as Show. Hide (T-246 fold 2 item 4, after a failed Close): 1 hidden, 0 none, -1 / -2 as Show.
int UiModsWarnShow(const char* text);
int UiModsWarnOkClicked();
int UiModsWarnClose();
int UiModsWarnHide();
// ui1 (connection trouble step 2): the small non-blocking notice - on 1 shows `text`, on 0 takes it down. 1 done, 0 refused,
// -1 a MyGUI throw, -2 a fault. MAIN THREAD only.
int UiQuietNotice(const char* text, int on);
// ui1: log the host-left dialog's and the notice's on-screen rectangles when they changed ([UI] rect ...). MAIN THREAD.
int UiDialogRectPoll();
// T-514 (owner 448): the pause menu's NEW GAME / SAVE GAME / LOAD GAME are hidden and disabled on every open while HandSaveBlocked()
// and the rows below close up (a post-hook on the menu's show 0x913250). Install once, after the address table. Opens in a
// multiplayer world -> the [SAVE] hb line's handSave[].
void InstallUiPauseMenuArrange();
long long UiHandSaveHiddenOpens();
// T-514: shows / hides the three and places every row of the pause menu `panel` (the engine's MainMenuPopup) from Kenshi's own
// positions - the same answer whoever calls and however often. >= 0 done (ui.cpp's result bits), < 0 not done. MAIN THREAD.
int UiPauseMenuArrange(void* panel);
// uishot (owner 2026-09-27, option a) - TEST-ONLY `uipreview <what>` from the command channel (MAIN THREAD: the title or the
// in-game pump). Shows one co-op screen through the code that shows it for real, with sample text, so the harness can
// screenshot it; `uipreview off` takes down what the preview put up. true = done or queued, false = refused (logged).
bool UiPreviewCommand(const std::string& args);
// pp1 (player-path-plan 1a) - TEST-ONLY `uiclick <name>` / `uipick <list> <row text>` / `uitype <field> <text>` / `uistate` from
// the command channel (MAIN THREAD). They drive the REAL widgets through their own handlers (the click delegate, the list's
// change event, the box's caption + change event) and REFUSE a hidden or disabled one - a mouse could not reach it either.
// true = fired / said, false = refused (logged `[UI] <verb> ... REFUSED - <why>`).
bool UiDriveCommand(const std::string& verb, const std::string& args);
// T-510 - TEST-ONLY `hostaddr fake <ip>` | `hostaddr show` | `hostaddr hide` from the command channel (MAIN THREAD): the HOSTING
// screen's INTERNET ADDRESS row as if the router had reported <ip>, and SHOW / HIDE without a click. true = done, false = refused.
bool UiHostAddrCommand(const std::string& args);
// T-461: the first widget at or under `root` whose name ends in "_<suffix>" after Kenshi's per-instance layout prefix (the match
// the title column uses), or 0. MAIN THREAD, inside the caller's SEH frame.
MyGUI::Widget* UiFindLayoutSuffix(MyGUI::Widget* root, const char* suffix);
// T-461: Kenshi's in-game pause menu through the engine's own getter and show (the ESC route): Visible 1 open / 0 closed / < 0 not
// read; Reshow opens it again (1 open now, 0 the engine declined, < 0 not done). MAIN THREAD, in a world.
int UiPauseMenuVisible();
int UiPauseMenuReshow();
// T-228 (1) - TEST-ONLY `uimenu ingame` from the command channel (MAIN THREAD: the in-game pump). Opens Kenshi's in-game menu
// (Kenshi_MainMenuPopupPanel) through the engine's own getter and show function - the route the in-game ESC key takes (F537(a):
// 0x47A0B0 -> 0x9164F0 -> show 0x913250) - no injected input (F010). Refused (logged) at the title screen. true = the menu is open.
bool UiMenuCommand(const std::string& args);
bool UiFactionTabCaption(const std::string& name);   // T-368, MAIN THREAD: the FACTION tab's name box shows the name the world gave this game's player faction
bool UiFactionTabRename(const std::string& name);   // T-368, TEST-ONLY `renamemyfaction tab <name>`: the FACTION tab's name box gets the name and its Enter (the engine's own handler runs)
// pp1b (review 2026-09-27) - an accepted `uiclick engine:<Suffix>` is QUEUED and fired here, at the TAIL of the pump that ran the
// command (coop.cpp: after orig_titleUpdate / after orig_mainLoop and TagsTick). The status was written `ok uiclick` first; a
// click that refuses here (the widget is gone or covered by then) writes `error uiclick`, as quitmenu does.
void UiDriveEngineClickFlush(int atTitle);   /* T-201 PP6' fold: 1 = after the title's update (the one-press load may post), 0 = the in-world tail */
// command_channel.cpp - the harness status file, written late (UiDriveEngineClickFlush's refusal after `ok uiclick`).
void CommandStatusLate(const std::string& text);

}
