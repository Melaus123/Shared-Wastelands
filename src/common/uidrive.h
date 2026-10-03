/* uidrive.h - pp1 (player-path-plan section 1a): THE NAME TABLE of the TEST-ONLY UI driver verbs.

   `uiclick <name>`, `uipick <list> <row text>`, `uitype <field> <text>` and `uistate` drive the REAL panel and the Kenshi title
   buttons through their own handlers (ui.cpp UiDriveCommand): a click fires the widget's own eventMouseButtonClick delegate, a
   pick sets the list's selected row and fires its own eventListChangePosition, a type sets the box's caption and fires its own
   eventEditTextChange - and the panel then reads the box through the same UiTextOf / CfgSanitiseTyped path a keypress feeds.
   A hidden or disabled widget (itself or any parent) is REFUSED, because a player's mouse could not reach it either.

   This header is PURE (no MyGUI, no Windows): the friendly name -> widget name table and the argument split, so the offline
   suite (src/coop-test/test_main.cpp) can pin every row. The widget names are ui.cpp's UiNames strings; the suite asserts each
   row literally, and a row whose widget is not on screen logs `REFUSED - not found` in the game rather than guessing.
   `engine:<LayoutSuffix>` names one of KENSHI'S OWN widgets by the suffix after its layout prefix (the same match the U3 column
   code uses, ui.cpp NameCarriesLayoutSuffix), e.g. `engine:NewGameButton`. C++03, header-only. */
#ifndef COOP_UIDRIVE_H
#define COOP_UIDRIVE_H

#include <string>
#include <cstring>
#include <cstddef>

namespace coopui {

enum UiDriveKind { kDriveButton = 1, kDriveList = 2, kDriveEdit = 3,
                   kDriveWindowClose = 4,   /* T-220: the panel window's title-bar X (its own eventWindowButtonPressed "close") */
                   kDriveEscape = 5 };      /* T-220: the engine's ESC arm at the title (TitleScreen::closeTheOtherBits) */

/* One friendly name. `w2` is a second widget for a name the player sees as ONE control that the panel builds twice (BACK:
   the Host / Join / Hosting screens' Back and the Multiplayer screen's own Back - only one is ever visible); the driver takes
   the first of the two that is visible. */
struct UiDriveName { const char* name; int kind; const char* w1; const char* w2; const char* caption; };
/* pp1b (review 2026-09-27): `caption` = the text the button must READ for this name to press it (0 = any) - the one dialog
   confirm button reads CREATE, DELETE WORLD or DELETE PROFILE, and `create` must never press a delete. */

inline const UiDriveName* UiDriveTable(size_t* count)
{
    static const UiDriveName t[] = {
        /* the title screen's MULTIPLAYER button (ours) */
        { "multiplayer",   kDriveButton, "CoopMultiplayerButton", 0 },
        /* the Multiplayer screen */
        { "host",          kDriveButton, "CoopModeHost",          0 },
        { "join",          kDriveButton, "CoopModeJoin",          0 },
        /* the action button: HOST on Host a game, JOIN on Join a game */
        { "go",            kDriveButton, "CoopGoBtn",             0 },
        { "hostgo",        kDriveButton, "CoopGoBtn",             0 },
        { "joingo",        kDriveButton, "CoopGoBtn",             0 },
        { "back",          kDriveButton, "CoopCloseBtn",          "CoopLandBackBtn" },
        { "paste",         kDriveButton, "CoopPasteBtn",          0 },
        { "copy",          kDriveButton, "CoopCopyAddrBtn",       0 },
        /* T-510: HOSTING's INTERNET ADDRESS row - SHOW / HIDE (one button) and its own COPY */
        { "hostaddrshow",  kDriveButton, "SWNetAddrShowBtn",      0 },
        { "hosting",       kDriveButton, "SWHostingPauseButton",  0 },   /* T-524: HOSTING on the host's pause menu */
        { "hostaddrcopy",  kDriveButton, "SWNetAddrCopyBtn",      0 },
        /* Host a game: the worlds list's buttons and the New world / Delete world? dialog */
        { "newworld",      kDriveButton, "CoopNewWorldBtn",       0 },
        { "deleteworld",   kDriveButton, "CoopDeleteWorldBtn",    0 },
        { "options",       kDriveButton, "CoopGameOptionsBtn",    0 },
        { "optdefaults",   kDriveButton, "CoopOptDefaultsBtn",    0 },
        { "optdone",       kDriveButton, "CoopOptDoneBtn",        0 },
        { "dlgok",         kDriveButton, "CoopDlgOkBtn",          0 },            /* the dialog's confirm, whatever it reads */
        { "create",        kDriveButton, "CoopDlgOkBtn",          0, "CREATE" },  /* only while it reads CREATE (new world / profile) */
        { "cancel",        kDriveButton, "CoopDlgCancelBtn",      "CoopCloseBtn", "CANCEL" },   /* T-201 N1: also BACK while it reads CANCEL (a HOST / JOIN in progress) */
        { "boxok",         kDriveButton, "CoopNoticeOk",          0 },            /* T-201 N1: the error box's OK (CAN'T HOST / CAN'T JOIN) */
        /* Hosting and Your profiles */
        { "chooseprofile", kDriveButton, "CoopChooseProfileBtn",  0 },
        { "play",          kDriveButton, "CoopProfPlayBtn",       0 },
        { "select",        kDriveButton, "CoopProfPlayBtn",       0, "SELECT" },  /* T-201 PP6': PROFILES' main button while CHANGE opened it */
        { "changeprofile", kDriveButton, "CoopHostProfChangeBtn", 0 },            /* T-201 PP6': HOST GAME's PROFILE row CHANGE */
        { "newprofile",    kDriveButton, "CoopProfNewBtn",        0 },
        { "deleteprofile", kDriveButton, "CoopProfDeleteBtn",     0 },
        /* in a world: the host-left window's EXIT GAME */
        { "exitgame",      kDriveButton, "CoopHostLeftExit",      0 },
        /* T-220: GAME OPTIONS - its three tabs (a DIFFICULTY, b WORLD, c CO-OP) and each shown row's < / > (rows a..i = slots 0..8
           of the tab on screen, top to bottom) */
        { "opttaba",       kDriveButton, "CoopOptTab0",           0 },
        { "opttabb",       kDriveButton, "CoopOptTab1",           0 },
        { "opttabc",       kDriveButton, "CoopOptTab2",           0 },
        { "optdeca",       kDriveButton, "CoopOptDec0",           0 },
        { "optdecb",       kDriveButton, "CoopOptDec1",           0 },
        { "optdecc",       kDriveButton, "CoopOptDec2",           0 },
        { "optdecd",       kDriveButton, "CoopOptDec3",           0 },
        { "optdece",       kDriveButton, "CoopOptDec4",           0 },
        { "optdecf",       kDriveButton, "CoopOptDec5",           0 },
        { "optdecg",       kDriveButton, "CoopOptDec6",           0 },
        { "optdech",       kDriveButton, "CoopOptDec7",           0 },
        { "optdeci",       kDriveButton, "CoopOptDec8",           0 },
        { "optinca",       kDriveButton, "CoopOptInc0",           0 },
        { "optincb",       kDriveButton, "CoopOptInc1",           0 },
        { "optincc",       kDriveButton, "CoopOptInc2",           0 },
        { "optincd",       kDriveButton, "CoopOptInc3",           0 },
        { "optince",       kDriveButton, "CoopOptInc4",           0 },
        { "optincf",       kDriveButton, "CoopOptInc5",           0 },
        { "optincg",       kDriveButton, "CoopOptInc6",           0 },
        { "optinch",       kDriveButton, "CoopOptInc7",           0 },
        { "optinci",       kDriveButton, "CoopOptInc8",           0 },
        /* T-220: the panel's two close routes that are not a button of ours - the title-bar X and the Escape key */
        { "close",         kDriveWindowClose, "CoopPanel",         0 },
        { "escape",        kDriveEscape,      "CoopPanel",         0 },
        /* the two lists */
        { "worlds",        kDriveList,   "CoopWorldList",         0 },
        { "profiles",      kDriveList,   "CoopProfList",          0 },
        /* the typed boxes (New world and New profile share the one dialog box) */
        { "name",          kDriveEdit,   "CoopNameEdit",          0 },   /* T-201 N1b: PLAYER NAME - ONE box, on HOST GAME and JOIN GAME */
        { "address",       kDriveEdit,   "CoopJoinAddrEdit",      0 },
        { "port",          kDriveEdit,   "CoopHostPortEdit",      0 },
        { "worldname",     kDriveEdit,   "CoopDlgNameEdit",       0 },
        { "profilename",   kDriveEdit,   "CoopDlgNameEdit",       0 },
        /* the bug report window (bugreport.cpp): REPORT A BUG on the title screen, or in the Esc menu */
        { "bugreport",     kDriveButton, "BugReportTitleButton",  "BugReportPauseButton" },
        { "bugsend",       kDriveButton, "BugReportSend",         0 },
        { "bugcancel",     kDriveButton, "BugReportCancel",       0 },
        { "bugok",         kDriveButton, "BugReportSentOk",       0 },
        { "bugback",       kDriveButton, "BugReportFailBack",     0 },
        { "bugretry",      kDriveButton, "BugReportFailRetry",    0 },
        { "bugtext",       kDriveEdit,   "BugReportText",         0 },
    };
    if (count != 0) *count = sizeof(t) / sizeof(t[0]);
    return t;
}

/* The row for `name` of `kind`, or 0. Exact, case-sensitive: the harness writes lower case. */
inline const UiDriveName* UiDriveFind(const std::string& name, int kind)
{
    size_t n = 0;
    const UiDriveName* t = UiDriveTable(&n);
    for (size_t i = 0; i < n; ++i)
        if (t[i].kind == kind && name == t[i].name) return &t[i];
    return 0;
}

/* The engine layout suffix of `engine:<Suffix>`: 1..48 of [A-Za-z0-9]; 0 when `name` is not that shape. */
inline int UiDriveEngineName(const std::string& name, std::string* suffix)
{
    static const char kPre[] = "engine:";
    const size_t pre = sizeof(kPre) - 1;
    if (name.size() <= pre || name.size() > pre + 48 || name.compare(0, pre, kPre) != 0) return 0;
    for (size_t i = pre; i < name.size(); ++i)
    {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return 0;
    }
    if (suffix != 0) *suffix = name.substr(pre);
    return 1;
}

/* The four driver verbs. They are NOT counted as harness verbs (player-path-plan section 0 item 6: g_cmdVerbsRun). */
inline int UiDriveIsVerb(const std::string& verb)
{
    return (verb == "uiclick" || verb == "uipick" || verb == "uitype" || verb == "uistate") ? 1 : 0;
}

/* pp1b (review 2026-09-27): the read-only OBSERVATION verbs. Each body in command_channel.cpp only reads and prints (its own
   comment says read-only, or it calls a report / list reader), so - like the driver verbs - they do not mark this game as
   harness-driven (g_cmdVerbsRun): a player-path run that observes still runs the player's own host-left path. NOT in it, on
   purpose: ping (sends a packet), buildings / towns (not audited), every on|off switch and every lever. */
inline int CmdIsObservationVerb(const std::string& verb)
{
    static const char* const k[] = { "report", "netstat", "roster", "relation", "snapshot", "boxlist", "boxdigest", "researchlist",
        "settingsdump", "captlist", "buildlist", "nearlist", "shoplist", "townlist", "recruitlist", "tasks", "health", "look",
        "handle", "factions", "templates" };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) if (verb == k[i]) return 1;
    return 0;
}
/* The verbs that count toward g_cmdVerbsRun: everything but the driver verbs, the observation verbs and `uimenu` (T-228). */
inline int CmdMarksHarness(const std::string& verb)
{
    return (UiDriveIsVerb(verb) == 0 && CmdIsObservationVerb(verb) == 0 && verb != "uimenu") ? 1 : 0;   /* T-228: uimenu opens Kenshi's own menu, as ESC would - a driver, not a lever */
}

/* Split "first rest of line": first = the first space-separated token, rest = everything after it with the outer blanks
   trimmed (a typed text or a row text may hold inner spaces). */
inline void UiDriveSplit(const std::string& args, std::string* first, std::string* rest)
{
    size_t b = 0;
    while (b < args.size() && (args[b] == ' ' || args[b] == '\t')) ++b;
    size_t e = b;
    while (e < args.size() && args[e] != ' ' && args[e] != '\t') ++e;
    if (first != 0) *first = args.substr(b, e - b);
    size_t r = e;
    while (r < args.size() && (args[r] == ' ' || args[r] == '\t')) ++r;
    size_t z = args.size();
    while (z > r && (args[z - 1] == ' ' || args[z - 1] == '\t' || args[z - 1] == '\r' || args[z - 1] == '\n')) --z;
    if (rest != 0) *rest = args.substr(r, z - r);
}

/* The usage error for one verb's arguments, or "" when the shape is right (the widget itself is checked live). */
inline std::string UiDriveUsage(const std::string& verb, const std::string& args)
{
    std::string a, rest;
    UiDriveSplit(args, &a, &rest);
    if (verb == "uistate") return a.empty() ? std::string() : std::string("usage: uistate (no argument)");
    if (verb == "uiclick")
    {
        if (a.empty() || !rest.empty()) return "usage: uiclick <name> | uiclick engine:<LayoutSuffix>";
        if (UiDriveFind(a, kDriveButton) == 0 && UiDriveFind(a, kDriveWindowClose) == 0 && UiDriveFind(a, kDriveEscape) == 0
            && UiDriveEngineName(a, 0) == 0) return "no button called '" + a + "'";   /* T-220: close / escape */
        return std::string();
    }
    if (verb == "uipick")
    {
        if (a.empty() || rest.empty()) return "usage: uipick worlds|profiles <row text>";
        if (UiDriveFind(a, kDriveList) == 0) return "no list called '" + a + "'";
        return std::string();
    }
    if (verb == "uitype")
    {
        if (a.empty() || rest.empty()) return "usage: uitype <field> <text>";
        if (UiDriveFind(a, kDriveEdit) == 0 && UiDriveEngineName(a, 0) == 0) return "no field called '" + a + "'";
        return std::string();
    }
    return "not a UI driver verb";
}

/* T-228 (1) - TEST-ONLY `uimenu ingame`: why this call is refused, or "" when it may open Kenshi's in-game menu (the
   Kenshi_MainMenuPopupPanel window - SAVE GAME, LOAD GAME, OPTIONS, EXIT GAME, RESUME). The one argument is `ingame`; the two
   engine rows (the menu's singleton getter and its show function) must be bound on this executable; and a world must be
   running - at the title screen that menu belongs to no world and the engine never shows it (F537(a)). Checked in this order. */
inline std::string UiMenuRefusal(const std::string& args, int rowsBound, int worldRunning)
{
    std::string a, rest;
    UiDriveSplit(args, &a, &rest);
    if (a != "ingame" || !rest.empty()) return "usage: uimenu ingame";
    if (rowsBound == 0) return "the address table does not bind MainMenuPopup_get / MainMenuPopup_show here; nothing was called";
    if (worldRunning == 0) return "no world is running (this is the title screen) - the in-game menu belongs to a loaded world; nothing was called";
    return std::string();
}

}  // namespace coopui

#endif
