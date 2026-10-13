/* src/common/panelfit.h - P8i-b: WILL THE MULTIPLAYER PANEL FIT, DECIDED BEFORE ANY WIDGET EXISTS.
 *
 * review-p8i H-3: the size refusal used to run AFTER the panel Window had been created - it destroyed
 * the window, returned, and never touched the build-fail streak, so at the measured ~1,065 title
 * ticks/s the whole widget tree was built and torn down a thousand times a second, for ever, with no
 * message anywhere.  A refusal that cannot be decided before the thing is built cannot be capped.
 *
 * So the decision lives here: pure integer arithmetic over the rectangle, no MyGUI, no engine memory,
 * header-only in the manner of loadlatch.h, and swept by src/coop-test/test_main.cpp with the same
 * compiler that builds the plugin.
 *
 * THE CHROME NUMBERS ARE READ OFF THE GAME'S OWN TEMPLATE, not guessed.
 * <Kenshi>/data/gui/templates/kenshi_templates.xml, resource "Kenshi_WindowCX", declared at 360x320:
 *     Root            0   0 360 320
 *       body          0  38 360 281   align Stretch
 *         Client      3   3 354 275   align Stretch   <- what MyGUI::Window::getClientWidget returns
 * MyGUI's Stretch keeps each margin as the parent resizes, so the margins are constants:
 *     body  loses 38 above and 320-38-281 = 1 below      -> body   = (W,      H - 39)
 *     Client loses 3 on each side of the body            -> client = (W - 6,  H - 45)
 * At the declared size that reproduces 354 x 275 exactly, which is the check that the reading is right.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

namespace coopui {

/* The panel's own layout: thirteen rows, and a row shorter than 14 pixels cannot hold Kenshi's font. */
const int kPanelRows       = 13;
const int kPanelMinRowH    = 14;
const int kPanelMinClientW = 220;

/* Kenshi_WindowCX chrome, from the template above. */
const int kWindowCXChromeW = 6;
const int kWindowCXChromeH = 45;

/* Does a client area of this size hold the thirteen rows?
   The shipped code computes rowH = clientH / kPanelRows with INTEGER division and refuses rowH < 14,
   which is exactly clientH < 14 * 13 - said as a multiplication here so the test does not have to
   reproduce a division to know the answer. */
inline bool PanelClientFits(int clientW, int clientH)
{
    if (clientW < kPanelMinClientW) return false;
    if (clientH < kPanelMinRowH * kPanelRows) return false;
    return true;
}

/* Does a Kenshi_WindowCX of this OUTER size give such a client area?  This is the question the old
   code could only answer after creating the window. */
inline bool PanelWindowFits(int windowW, int windowH)
{
    return PanelClientFits(windowW - kWindowCXChromeW, windowH - kWindowCXChromeH);
}

/* P8i-c / F751 - THE DESIGN MINIMUM, MOVED OUT OF ui.cpp AND JOINED TO THE FIT TEST.
   design-ui-panel 1.3 sets a floor of 260 x 240 on the panel's outer rectangle.  It lived in ui.cpp as
   two bare literals in an early return that sat ABOVE the capped guard, so on a small screen the panel
   was refused for ever with no streak, no cap and no message, and the capped guard below it was
   unreachable (F709 -> F751; the bug class is "a repair placed BELOW an older refusal on the same
   input", .modding/07-bug-classes.md).  The repair is not a third guard - it is ONE predicate that
   answers the whole question, so there is no longer an ordering to get wrong. */
const int kPanelMinWindowW = 260;
const int kPanelMinWindowH = 240;

inline bool PanelWindowAllowed(int windowW, int windowH)
{
    if (windowW < kPanelMinWindowW || windowH < kPanelMinWindowH) return false;
    return PanelWindowFits(windowW, windowH);
}

/* The refusal bookkeeping, also pure (6a lesson 14: a corrective that cannot work must give up).
   streak is the count of consecutive refusals BEFORE this one; giveUp is 1 when this refusal is the
   one that reaches the cap and the panel must stop retrying. */
struct PanelRefusal
{
    long streak;
    int  giveUp;
};

inline PanelRefusal PanelNoteRefusal(long streakBefore, long cap)
{
    PanelRefusal r;
    r.streak = streakBefore + 1;
    r.giveUp = (cap > 0 && r.streak >= cap) ? 1 : 0;
    return r;
}

/* =================================================================================================
   U2-b - THE RECTANGLE AND THE ROW SHARES, PURE, FROM THE HANDS-ON REPORT OF 2026-09-05.
   =================================================================================================
   Three defects were reported on the deployed U1 panel, and each one is answered here rather than in
   ui.cpp, so the offline suite can sweep the answer:

   (1) "the text on the buttons behind it bleed through into the new menu".  CONFIRMED CAUSE, read from
       the game's own skin files.  U2-c (review-u2 Q9-1) CORRECTS THE FILE NAMED HERE - the reading was
       right and the file was not: PanelEmpty is declared in
       <Kenshi>\data\gui\skins\common_skins.xml:3, not in kenshi_skins.xml, as
       `<Resource type="ResourceSkin" name="PanelEmpty" size="16 16"/>` - self-closing, with NO BasisSkin
       and no texture, so it draws nothing at all.  (Kenshi_InventoryPanelSkin, the opaque one ui.cpp
       uses, IS in kenshi_skins.xml, at line 42.)  Kenshi_WindowCX's body/Client is a PanelEmpty, so the panel
       has never had a surface; the menu column behind it was always visible through it.  ui.cpp now
       puts an opaque widget behind the panel; the skin it uses is named there, not here.

   (2) "the menu cant be drag moved ... we should center it regardless".  setMovable(false) stays - no
       dragging - so the rectangle must be centred by construction.  The old rectangle was left 30% /
       width 40% of the parent, which is centred only for as long as the width stays at exactly 40%.
       PanelRectIn centres whatever width it ends up choosing, which is what makes (3) safe.

   (3) "the entire bottom half of that text is cut off ... 'world name (both players ty... is cut off".
       Two separate faults with one cause - a panel that is too small in proportion to its own text:
         - VERTICAL: rowH is clientH/13, and at a half-screen window that is a handful of pixels.
           PanelRectIn grows the rectangle towards kPanelWantRowH per row when the parent is small,
           bounded by 90% of the parent so it can never spill.  See the honesty note on that constant.
         - HORIZONTAL: the label of a label+field pair was given a FIXED 55% of the client width while
           the field - a port of at most five characters - took the other 45%.  PanelPairRowOf gives
           the field what its contents need and the LABEL EVERYTHING ELSE, which is at least 64% of the
           client at every rectangle PanelWindowAllowed permits (swept, and the assertion that pins it
           fails on the old fixed share).

   WHAT THIS ARITHMETIC CANNOT SAY, said plainly (design-offline-tests 7.4).  Kenshi's painted font has
   never been measured in pixels by this project, so NO function here can prove a particular string
   fits a particular rectangle.  What is proved is relative and total: every paired label gets a
   strictly larger share than it used to, every row is inside the client, and the rectangle is centred
   and inside the parent.  kPanelWantRowH below is a COMFORT TARGET, not a measurement - it is not read
   off any file and it is bounded so that being wrong about it cannot break the layout.  The size
   refusal (PanelWindowAllowed, P8i-c) is untouched and still refuses in words. */

/* A comfortable row, and the panel width that goes with it.  UNMEASURED - see the note above. */
const int kPanelWantRowH = 24;
const int kPanelWantW    = 480;

/* The outer window the comfort target implies, through the Kenshi_WindowCX chrome read at the top. */
const int kPanelWantH = kPanelRows * kPanelWantRowH + kWindowCXChromeH;

/* Never take more than this share of the parent when growing: the panel must stay a panel, and it must
   stay inside the rectangle it was measured against. */
const int kPanelMaxParentPct = 90;

struct PanelRect
{
    int left;
    int top;
    int w;
    int h;
};

/* THE PANEL'S OWN RECTANGLE, from the LIVE parent rectangle every time - design-ui-panel 1.3.  The
   40% x 70% box is kept whenever it is already at least the comfort target, because that is the shape
   the player reported as correct at full screen; below it the box grows towards the target and stops at
   kPanelMaxParentPct of the parent. */
inline PanelRect PanelRectIn(int parentW, int parentH)
{
    PanelRect r;
    int capW, capH;
    r.w = (parentW * 40) / 100;
    r.h = (parentH * 70) / 100;
    capW = (parentW * kPanelMaxParentPct) / 100;
    capH = (parentH * kPanelMaxParentPct) / 100;
    if (r.w < kPanelWantW) r.w = (kPanelWantW < capW) ? kPanelWantW : capW;
    if (r.h < kPanelWantH) r.h = (kPanelWantH < capH) ? kPanelWantH : capH;
    if (r.w > parentW) r.w = parentW;
    if (r.h > parentH) r.h = parentH;
    /* Centred by construction, on both axes, whatever width and height were chosen.  The odd pixel
       goes to the right and bottom, which is what integer division does and is why the tests allow
       one pixel of asymmetry and not more. */
    r.left = (parentW - r.w) / 2;
    r.top  = (parentH - r.h) / 2;
    if (r.left < 0) r.left = 0;
    if (r.top  < 0) r.top  = 0;
    return r;
}

/* Did PanelRectIn have to grow the box?  ui.cpp books a counter on exactly this answer, so the answer
   is a function and not a comparison written twice. */
inline bool PanelRectWasGrown(int parentW, int parentH)
{
    const PanelRect r = PanelRectIn(parentW, parentH);
    return (r.w != (parentW * 40) / 100) || (r.h != (parentH * 70) / 100);
}

/* U2-c (review-u2 H-1) - HOW LONG A CHANGED WINDOW MUST HOLD STILL BEFORE THE PANEL IS LAID OUT AGAIN.
   U2-b took the panel down on the FIRST tick whose parent rectangle differed from the one it was laid
   out at.  During a window drag that rectangle differs on every tick, so a ~25-widget tree was destroyed
   and rebuilt at the title pump's ~1 kHz for as long as the drag lasted - F709's own shape, and the
   build-fail streak could not catch it because every one of those rebuilds SUCCEEDS and resets the
   streak to zero.  The repair is not a delay before acting: the size is re-read every tick and the
   take-down happens when the NEW size has been both different from the laid-out one AND unchanged for
   this long, so it is a recurring check with a confirmed exit.  A size that moves again restarts the
   clock; a size that comes back to the laid-out one clears it and nothing is rebuilt at all. */
const int kPanelResizeSettleMs = 300;

/* A label and its field on one row.  The field gets what its contents need, the label gets the rest. */
const int kPanelMinFieldW  = 60;    /* a five-character port, and "s1" */
const int kPanelMinLabelW  = 120;   /* the label never falls below this, whatever the field wants */
const int kPanelMaxPairGap = 8;     /* the gap between them, capped so a tall panel cannot eat the row */
const int kPanelLabelPctMin = 64;   /* the swept floor on the label's share.  The old layout gave 55. */

struct PanelPair
{
    int labelW;
    int fieldX;
    int fieldW;
};

inline PanelPair PanelPairRowOf(int clientW, int gapIn)
{
    PanelPair p;
    int gap = (gapIn < 1) ? 1 : gapIn;
    int fw, maxF;
    if (gap > kPanelMaxPairGap) gap = kPanelMaxPairGap;
    fw = (clientW * 30) / 100;
    if (fw < kPanelMinFieldW) fw = kPanelMinFieldW;
    maxF = clientW - gap - kPanelMinLabelW;
    if (maxF < 1) maxF = 1;
    if (fw > maxF) fw = maxF;
    p.fieldW = fw;
    p.labelW = clientW - gap - fw;
    p.fieldX = p.labelW + gap;
    return p;
}

/* ui3 (ui3-title-panel-look item 2) - THE PANEL'S INNER MARGIN.  Every row is built inside a widget inset from the window's
   client by this much, so no text starts on the frame art.  About 3% of the client width each side (Kenshi's message box
   insets its text 2.9%), half that above and below - but NEVER so much that the inner area falls below PanelClientFits's
   minimum: a client the pre-create test (PanelWindowAllowed) accepted must still hold the thirteen rows inside the margin,
   or the prediction and the built panel would disagree.  Swept in test_main.cpp. */
struct PanelInnerPad
{
    int x;
    int y;
};

inline PanelInnerPad PanelInnerPadOf(int clientW, int clientH)
{
    PanelInnerPad p;
    const int roomX = (clientW - kPanelMinClientW) / 2;
    const int roomY = (clientH - kPanelMinRowH * kPanelRows) / 2;
    p.x = (clientW * 3) / 100 + 2;
    p.y = p.x / 2;
    if (p.x > roomX) p.x = roomX;
    if (p.y > roomY) p.y = roomY;
    if (p.x < 0) p.x = 0;
    if (p.y < 0) p.y = 0;
    return p;
}

/* =================================================================================================
   ui5 (panel-mockups.md, owner-approved 2026-09-27) - EACH SCREEN IS AS TALL AS ITS OWN ROWS.
   =================================================================================================
   The thirteen-row grid gave every screen the same height, so a four-line dialog sat in a window built for the Game
   options list, and every screen repeated its name in a first row.  Each screen - and each dialog, which the panel shows
   as a screen - now has its own ROW LIST: slots in HALF-ROWS, top to bottom, the bottom button row always last.  The
   screen's name is the window's caption, so no list has a title row.
   THE ROW HEIGHT IS STILL THE GRID'S: the largest rectangle PanelRectIn allows, less the ui3 margin, over kPanelRows - the
   ui3 look the owner approved - and no list may be taller than kPanelRows rows, so every screen fits inside the rectangle
   the pre-create test (PanelWindowAllowed) already accepted.  Only the window's HEIGHT follows the screen (centred again);
   its width, margin and row height do not, so nothing jumps sideways between screens.  Swept in test_main.cpp. */
enum PanelLayoutId
{
    kLayLanding = 0, kLayHost = 1, kLayJoin = 2, kLayNewWorld = 3, kLayDeleteWorld = 4, kLayHosting = 5,
    kLayOptions = 6, kLayProfiles = 7, kLayNewProfile = 8, kLayDeleteProfile = 9, kLayHostHint = 10, kLayJoinHint = 11,
    kLayHostingVpn = 12,   /* T-631: HOSTING with the RADMIN / HAMACHI ADDRESS row (a game VPN was found) */
    kLayPassword = 13,     /* T-290: PASSWORD REQUIRED - a JOIN refused for its password; NEW WORLD's rows (its line, PASSWORD, CANCEL / JOIN) */
    kLayInvite = 14,       /* INVITE FRIENDS, over HOSTING: the friends online, the status line, BACK / INVITE */
    kLayResetWorld = 15,   /* RESET WORLD?: HOST GAME's reset confirm - its text, the keep-buildings tick box, the last paragraph, CANCEL / RESET WORLD */
    kLayCount = 16
};
/* Screen 4 (the world dialog) is RESET WORLD? while its dialog number (PanelLayoutOf's profDlg on that screen) is this, else DELETE WORLD?. */
const int kWorldDlgReset = 3;
/* Whether the point (px, py) is inside the rectangle left l, top t, width w, height h - its left and top edges in, its right
   and bottom edges out: the hover notes' test of the mouse pointer against a widget's on-screen rectangle. */
inline bool PanelPointIn(int px, int py, int l, int t, int w, int h)
{
    return px >= l && py >= t && px < l + w && py < t + h;
}
/* What a slot holds.  kSlotGap is blank space between groups (the mock-ups' empty lines). */
enum PanelSlotId
{
    kSlotGap = 0, kSlotNameRow, kSlotNameHint, kSlotHostBtn, kSlotJoinBtn, kSlotList, kSlotListBtns, kSlotPortRow,
    kSlotStatus, kSlotAddrRow, kSlotDlgText, kSlotDlgName, kSlotHomeAddr, kSlotPlayers, kSlotTabs,
    kSlotOptRows, kSlotButtons, kSlotProfRow,  /* T-201 PP6': HOST GAME's PROFILE row, under PORT */
    kSlotNetAddr,                                         /* T-510: HOSTING's INTERNET ADDRESS row, under LOCAL ADDRESS */
    kSlotVpnAddr,                                         /* T-631: HOSTING's RADMIN / HAMACHI ADDRESS row, between the two */
    kSlotPwRow,                                           /* T-290: HOST GAME's PASSWORD row (label, box, SHOW / HIDE), above PORT */
    kSlotJoinByAddr,                                      /* JOIN BY ADDRESS: JOIN GAME's line between the friends list and IP ADDRESS */
    kSlotSteamRow,                                        /* HOSTING's STEAM row: its label, INVITE FRIENDS at the right and, without Steam, its line */
    kSlotDlgTick,                                         /* RESET WORLD?'s keep-buildings tick box and its label, between its text and its last paragraph */
    kSlotDlgTail                                          /* RESET WORLD?'s last paragraph, under the tick box (PanelLive::tail) */
};
struct PanelSlot { int slot; int halves; };
const int kPanelMaxHalves = kPanelRows * 2;

/* The layout a screen shows: the panel's screen number (ui.cpp g_panelScreen: 0 MULTIPLAYER, 1 HOST GAME, 2 JOIN GAME,
   3 NEW WORLD, 4 DELETE WORLD?, 5 HOSTING, 6 GAME OPTIONS, 7 PROFILES, 8 PASSWORD REQUIRED) and, on PROFILES, which of its
   dialogs is up. */
inline int PanelLayoutOf(int screen, int profDlg, int nameHint = 0, int vpnRow = 0)
{
    if (screen == 7) return profDlg == 1 ? kLayNewProfile : profDlg == 2 ? kLayDeleteProfile : kLayProfiles;
    /* T-201 N1b (owner 166): PLAYER NAME is the top row of HOST GAME and JOIN GAME; its hint's row exists only while it has a
       sentence (ui5b's rule, moved with the box).  MULTIPLAYER has no name row now. */
    if (screen == 1) return nameHint != 0 ? kLayHostHint : kLayHost;
    if (screen == 2) return nameHint != 0 ? kLayJoinHint : kLayJoin;
    if (screen == 5 && vpnRow != 0) return kLayHostingVpn;   /* T-631: HOSTING with its VPN row */
    if (screen == 8) return kLayPassword;   /* T-290: PASSWORD REQUIRED */
    if (screen == 9) return kLayInvite;     /* INVITE FRIENDS, over HOSTING */
    if (screen == 4 && profDlg == kWorldDlgReset) return kLayResetWorld;   /* RESET WORLD? */
    if (screen >= 3 && screen <= 6) return screen;
    return kLayLanding;
}
/* T-631: either HOSTING layout (with or without the VPN row). */
inline int PanelLayIsHosting(int layout) { return (layout == kLayHosting || layout == kLayHostingVpn) ? 1 : 0; }

/* ui5b - THE PANEL'S TEXT ESTIMATE (ui7: now only the FALLBACK when MyGUI reports no text size).  A status / text area holds one line per HALF-ROW (17 px at 1280x720; the T479 shots
   show Kenshi's body lines about 14 px apart - Inferred from the cropped shots) and wraps at kPanelTextCharW px a character
   (the same shots show about 5 px - Inferred); the areas below are the most lines each screen shows at 1280x720 by this
   estimate, plus one half-row - swept against the real sentences in test_main.cpp. */
const int kPanelTextCharW = 7;

/* THE ROW LISTS, read off panel-mockups.md sections 2-9.  A status area is sized for the most lines it carries at once
   (the intro, a blank line, the live line and a refusal paragraph); a list for its head and a few rows; a text line for
   one wrap at 1280x720 (ui7: the live areas are MEASURED on screen - PanelTextHalvesPx).  ui5c: a status / text area's value here is its
   CEILING - on screen it is as tall as its live text (PanelSlotHalves); ui5d: so is HOSTING's PLAYERS list; its STEAM row follows Steam (PanelLive::steam). */
inline const PanelSlot* PanelRowsOf(int layout, int* count)
{
    /* T-201 N1b (owner 166): MULTIPLAYER - HOST GAME, JOIN GAME and BACK only, a full row each (the bottom buttons' height). */
    static const PanelSlot landing[]  = { { kSlotHostBtn, 2 }, { kSlotGap, 1 }, { kSlotJoinBtn, 2 }, { kSlotGap, 1 },
                                          { kSlotButtons, 2 } };
    /* T-201 N1b (owner 166): PLAYER NAME the top row of HOST GAME and JOIN GAME; while the hint has a sentence its row (one
       line at the box's width, 1280x720 - swept in test_main.cpp) takes the gap under the box, as ui5b's MULTIPLAYER did.
       HOST GAME's status says only its own lines (T-201 N1 - failures are boxes): the switch note and the opening line, or
       during a press its line and the name-not-saved line. */
    /* HOST GAME's rows: the worlds list and its buttons, then one group with no gap inside - PASSWORD (label, box, SHOW / HIDE)
       (T-290), PORT, PROFILE (T-201 PP6') - then the status area.  Twelve rows hold them with the
       list at 4 half-rows and no gap between PLAYER NAME and the list's head; the status ceiling is 5: a press's line with the name-not-saved line is
       four lines at 1280x720 (swept).  With the name's hint up (its row between the name and the list) the ceiling is 3:
       HOST is greyed while the hint shows, so no press line is up. */
    static const PanelSlot host[]     = { { kSlotNameRow, 2 }, { kSlotList, 4 }, { kSlotGap, 1 },
                                          { kSlotListBtns, 2 }, { kSlotPwRow, 2 }, { kSlotPortRow, 2 },
                                          { kSlotProfRow, 2 }, { kSlotGap, 1 }, { kSlotStatus, 5 }, { kSlotGap, 1 }, { kSlotButtons, 2 } };
    static const PanelSlot hostHint[] = { { kSlotNameRow, 2 }, { kSlotNameHint, 2 }, { kSlotList, 4 }, { kSlotGap, 1 },
                                          { kSlotListBtns, 2 }, { kSlotPwRow, 2 }, { kSlotPortRow, 2 },
                                          { kSlotProfRow, 2 }, { kSlotGap, 1 }, { kSlotStatus, 3 }, { kSlotGap, 1 }, { kSlotButtons, 2 } };
    /* JOIN GAME's rows: PLAYER NAME, the friends list (its heads and rows, or the line in its place - kSlotList) with REFRESH under it at
       the right (kSlotListBtns), JOIN BY ADDRESS, IP ADDRESS, then the status area.  Thirteen rows hold them with the list at 5
       half-rows; with the name's hint up the status ceiling is 7 - the opening line alone, JOIN is greyed while the hint shows.
       No gap row between the status area and BACK / JOIN: the status area's own spare under its last line is that gap, so the
       help line sits one gap above the bottom row and the window is no taller than its rows. */
    static const PanelSlot join[]     = { { kSlotNameRow, 2 }, { kSlotList, 5 }, { kSlotListBtns, 2 }, { kSlotJoinByAddr, 2 },
                                          { kSlotAddrRow, 2 }, { kSlotGap, 1 }, { kSlotStatus, 9 }, { kSlotButtons, 2 } };
    static const PanelSlot joinHint[] = { { kSlotNameRow, 2 }, { kSlotNameHint, 2 }, { kSlotList, 5 }, { kSlotListBtns, 2 },
                                          { kSlotJoinByAddr, 2 }, { kSlotAddrRow, 2 }, { kSlotGap, 1 }, { kSlotStatus, 7 },
                                          { kSlotButtons, 2 } };
    static const PanelSlot nameDlg[]  = { { kSlotDlgText, 3 }, { kSlotGap, 1 }, { kSlotDlgName, 2 }, { kSlotGap, 1 },
                                          { kSlotButtons, 2 } };
    static const PanelSlot delWorld[] = { { kSlotDlgText, 10 }, { kSlotGap, 1 }, { kSlotButtons, 2 } };   /* ui5b: one row more at 1280x720 */
    /* HOSTING's rows: the STEAM row - its label, INVITE FRIENDS at the right level with it and, only while Steam is not running,
       its line wrapped between them (two lines at 1280x720 and 1920x1080: the row is two rows tall then, one row while Steam
       runs - PanelLive::steam) - then the PLAYERS list, LOCAL ADDRESS, INTERNET ADDRESS (one row for an address, two while its sentence wraps -
       PanelLive::net), the status area and BACK.  No gap rows: each text area's spare under its last line is the gap.  22
       half-rows at the ceilings; the status ceiling is 6 (the profile line, the hosting line and the copy note at 1280x720). */
    static const PanelSlot hosting[]  = { { kSlotSteamRow, 4 }, { kSlotPlayers, 4 }, { kSlotHomeAddr, 2 }, { kSlotNetAddr, 4 },
                                          { kSlotStatus, 6 }, { kSlotButtons, 2 } };
    /* With a game VPN found, its RADMIN / HAMACHI ADDRESS row (one row, LOCAL ADDRESS's height) between LOCAL ADDRESS and INTERNET
       ADDRESS; the status ceiling is 4 there, so the window at its ceilings is as tall as HOSTING's. */
    static const PanelSlot hostingVpn[] = { { kSlotSteamRow, 4 }, { kSlotPlayers, 4 }, { kSlotHomeAddr, 2 }, { kSlotVpnAddr, 2 },
                                            { kSlotNetAddr, 4 }, { kSlotStatus, 4 }, { kSlotButtons, 2 } };
    /* INVITE FRIENDS: the friends list (FRIEND / STATUS, or the line in its place), the status line, BACK / INVITE. */
    static const PanelSlot invite[]   = { { kSlotList, 8 }, { kSlotGap, 1 }, { kSlotStatus, 3 }, { kSlotButtons, 2 } };
    static const PanelSlot options[]  = { { kSlotTabs, 2 }, { kSlotGap, 1 }, { kSlotOptRows, 18 }, { kSlotGap, 1 },
                                          { kSlotButtons, 2 } };
    static const PanelSlot profiles[] = { { kSlotList, 8 }, { kSlotGap, 1 }, { kSlotListBtns, 2 }, { kSlotGap, 1 },
                                          { kSlotStatus, 8 }, { kSlotGap, 1 }, { kSlotButtons, 2 } };
    static const PanelSlot delProf[]  = { { kSlotDlgText, 4 }, { kSlotGap, 1 }, { kSlotButtons, 2 } };
    /* RESET WORLD?: its text down to what every player keeps, the keep-buildings tick box (one row, as GAME OPTIONS' ticks), the last
       paragraph (the old world's name), then CANCEL / RESET WORLD - no gap row: the last paragraph's own spare under its last line is
       that gap (as JOIN GAME's status area).  At the panel's width with a 48-character world name the text above the tick box
       is 17 lines and the last paragraph 3 at 1280x720 (rowH 34): Kenshi's own line of about 14 px needs 15 and 3 half-rows; the
       fallback estimate's 18 px lines fill the 18 and 4 here exactly, without its 9 px spare.  The list is the 26 half-rows the
       window has.  Swept in test_main.cpp at 1280x720 and 1920x1080. */
    static const PanelSlot resetWorld[] = { { kSlotDlgText, 18 }, { kSlotDlgTick, 2 }, { kSlotDlgTail, 4 }, { kSlotButtons, 2 } };
    const PanelSlot* s = landing;
    int n = (int)(sizeof(landing) / sizeof(landing[0]));
    switch (layout)
    {
    case kLayHost:          s = host;     n = (int)(sizeof(host) / sizeof(host[0]));         break;
    case kLayJoin:          s = join;     n = (int)(sizeof(join) / sizeof(join[0]));         break;
    case kLayNewWorld:
    case kLayPassword:      /* T-290: PASSWORD REQUIRED is built like NEW WORLD */
    case kLayNewProfile:    s = nameDlg;  n = (int)(sizeof(nameDlg) / sizeof(nameDlg[0]));   break;
    case kLayDeleteWorld:   s = delWorld; n = (int)(sizeof(delWorld) / sizeof(delWorld[0])); break;
    case kLayHosting:       s = hosting;  n = (int)(sizeof(hosting) / sizeof(hosting[0]));   break;
    case kLayOptions:       s = options;  n = (int)(sizeof(options) / sizeof(options[0]));   break;
    case kLayProfiles:      s = profiles; n = (int)(sizeof(profiles) / sizeof(profiles[0])); break;
    case kLayDeleteProfile: s = delProf;  n = (int)(sizeof(delProf) / sizeof(delProf[0]));   break;
    case kLayHostHint:      s = hostHint; n = (int)(sizeof(hostHint) / sizeof(hostHint[0])); break;   /* T-201 N1b */
    case kLayJoinHint:      s = joinHint; n = (int)(sizeof(joinHint) / sizeof(joinHint[0])); break;   /* T-201 N1b */
    case kLayHostingVpn:    s = hostingVpn; n = (int)(sizeof(hostingVpn) / sizeof(hostingVpn[0])); break;   /* T-631 */
    case kLayInvite:        s = invite;   n = (int)(sizeof(invite) / sizeof(invite[0]));     break;
    case kLayResetWorld:    s = resetWorld; n = (int)(sizeof(resetWorld) / sizeof(resetWorld[0])); break;
    default: break;
    }
    if (count != 0) *count = n;
    return s;
}

/* ui5c (user 2026-09-27: "every screen is sized to its content, no dead space") - THE TEXT AREA FOLLOWS ITS TEXT.  The
   status area (kSlotStatus) and a dialog's text (kSlotDlgText) - never both in one list - were reserved at the most lines
   the screen can ever show, so a screen saying one line kept the rest of it empty above the bottom row (T480 at 1280x720:
   PROFILES about 180 px, HOST GAME about 120 px).  `liveHalves` (ui5d: PanelLive::text) >= 0 is the text's own height in half-rows (ui.cpp
   PanelLiveTextHalves -> PanelTextHalvesPx: none for no text, else ui7's measured text height plus kPanelTextSparePx,
   in whole half-rows) and replaces the list's value, which stays the CEILING, so no screen grows
   past what PanelWindowAllowed accepted.  -1 = the list's value. */
/* ui5d (manager decision 2026-09-28; T486 at 1920x1080 left about two rows empty under HOSTING's status) - THE TEXT'S
   HEIGHT IN PIXELS, in whole half-rows: never clipped and less than one half-row + kPanelTextSparePx blank under its last
   line.  ui7 (T496 at 1280x720 / T497 at 1920x1080): ui5d assumed the skin font does not grow with the window and took
   kPanelTextLinePx = 18 px a line; the screenshots show the text proportionally as large at both sizes (the font DOES
   scale - Confirmed from the shots), so one estimate erred opposite ways at the two sizes.  The height is now MEASURED:
   ui.cpp reads MyGUI's own wrapped text size (EditBox::getTextSize at the area's real width, plus the edit box's skin
   inset) and passes it to PanelTextHalvesPx.  PanelTextHalves (lines x kPanelTextLinePx, the ui5b wrap estimate) is only
   the fallback when MyGUI reports 0, and ui.cpp counts each fallback.  Swept in test_main.cpp at rowH 14..60. */
const int kPanelTextLinePx  = 18;   /* the FALLBACK line pitch only (ui7) - Kenshi's real one is logged as [UI] measured ... lineH= */
const int kPanelTextSparePx = 9;    /* the small guard under a text area's last line (ui7: kept for the measured height too) */
/* ui7: `textPx` = the text's measured height in px (0 or less = no text: no rows).  rowH <= 0 (no panel on screen) = -1,
   PanelLive's "the list's value" - the ceiling. */
inline int PanelTextHalvesPx(int textPx, int rowH)
{
    if (textPx <= 0) return 0;   /* no text: no rows (the gap slot under it still follows) */
    if (rowH <= 0) return -1;
    return (2 * (textPx + kPanelTextSparePx) + rowH - 1) / rowH;   /* a half-row is rowH / 2 px */
}
/* The fallback: `lines` from the wrap estimate at kPanelTextLinePx each. */
inline int PanelTextHalves(int lines, int rowH)
{
    if (lines <= 0) return 0;   /* no text: no rows (the gap slot under it still follows) */
    if (rowH <= 0) return lines + 1;   /* no row height yet (never on screen): a half-row a line and one spare */
    return PanelTextHalvesPx(lines * kPanelTextLinePx, rowH);
}
/* ui5d (T485 at 1280x720: HOSTING's router help and PLAYERS areas kept their fixed rows) - EVERY TEXT AREA'S LIVE HEIGHT.
   PanelLive carries one live height per text area a screen can have, each -1 (= the list's value) until set: `text` the
   status area / a dialog's text (ui5c's liveHalves - an int converts to it, so every ui5c call reads as before), `players`
   HOSTING's PLAYERS list (kSlotPlayers), `steam` its STEAM row (kSlotSteamRow, PanelSteamRowHalves).  ui.cpp fills each from the text on
   screen by the same rule (PanelTextAreaHalves: ui7's measured height, PanelTextHalvesPx), and the list's value stays the CEILING.  GAME OPTIONS'
   rows are not live: that screen is as tall as its longest tab so its tabs never move under the mouse. */
struct PanelLive
{
    int text;
    int players;
    int net;   /* T-510: HOSTING's INTERNET ADDRESS row (kSlotNetAddr) - 2 for one line, 4 for a value wrapped in two */
    int steam; /* HOSTING's STEAM row (kSlotSteamRow): PanelSteamRowHalves */
    int tail;  /* RESET WORLD?'s last paragraph (kSlotDlgTail), under its tick box */
    PanelLive(int textHalves = -1) : text(textHalves), players(-1), net(-1), steam(-1), tail(-1) {}
};
inline int PanelSlotHalves(const PanelSlot& s, const PanelLive& live)
{
    int v = -1;
    if (s.slot == kSlotStatus || s.slot == kSlotDlgText) v = live.text;
    else if (s.slot == kSlotSteamRow) v = live.steam;
    else if (s.slot == kSlotPlayers) v = live.players;
    else if (s.slot == kSlotNetAddr) v = live.net;
    else if (s.slot == kSlotDlgTail) v = live.tail;
    if (v >= 0) return v < s.halves ? v : s.halves;
    return s.halves;
}
inline int PanelLayoutHalves(int layout, const PanelLive& live = PanelLive())
{
    int n = 0, sum = 0;
    const PanelSlot* s = PanelRowsOf(layout, &n);
    for (int i = 0; i < n; ++i) sum += PanelSlotHalves(s[i], live);
    return sum;
}
inline int PanelLayoutInnerH(int layout, int rowH, const PanelLive& live = PanelLive()) { return (PanelLayoutHalves(layout, live) * rowH) / 2; }

/* A slot's band inside the inner area: its top and height in pixels (h 0 when the layout has no such slot).  The bands
   tile the inner height exactly: the last one ends at PanelLayoutInnerH. */
struct PanelBand { int top; int h; };
inline PanelBand PanelSlotBand(int layout, int slot, int rowH, const PanelLive& live = PanelLive())   /* ui5c/ui5d: live - PanelSlotHalves */
{
    PanelBand b;
    b.top = 0;
    b.h = 0;
    int n = 0, before = 0;
    const PanelSlot* s = PanelRowsOf(layout, &n);
    for (int i = 0; i < n; ++i)
    {
        const int halves = PanelSlotHalves(s[i], live);
        if (slot != kSlotGap && s[i].slot == slot)
        {
            b.top = (before * rowH) / 2;
            b.h = ((before + halves) * rowH) / 2 - b.top;
            return b;
        }
        before += halves;
    }
    return b;
}

/* A LABEL AND ITS BOX ON ONE ROW: the label a fixed share of the width (so every screen's boxes start in one column), the
   box after it - `boxWant` wide when that fits (0 = to the edge), `rightReserve` kept free on the right for a side button
   (PASTE, COPY).  Both take the band's top and height, so their vertical centres are the same by construction; ui.cpp
   also centres the label's text vertically (Kenshi_TextboxPaintedText / StandardText carry no vertical align). */
const int kPanelLabelPct = 33;
struct PanelPairRect { int labelX; int labelW; int boxX; int boxW; int top; int h; };
inline PanelPairRect PanelLabelBoxIn(int innerW, int gap, PanelBand band, int boxWant, int rightReserve)
{
    PanelPairRect p;
    p.labelX = 0;
    p.labelW = (innerW * kPanelLabelPct) / 100;
    p.boxX = p.labelW + gap;
    const int room = innerW - rightReserve - p.boxX;
    p.boxW = (boxWant > 0 && boxWant < room) ? boxWant : room;
    if (p.boxW < 1) p.boxW = 1;
    p.top = band.top;
    p.h = band.h;
    return p;
}
/* BACK / CANCEL (left) and the action (right) share one width; PASTE / COPY one; the port box a five-digit width. */
inline int PanelBottomBtnW(int innerW) { return (innerW * 38) / 100; }
inline int PanelSideBtnW(int innerW)   { return (innerW * 20) / 100; }
/* HOST GAME's four world buttons (NEW WORLD, GAME OPTIONS, RESET WORLD, DELETE WORLD): one width, a gap between each two. */
inline int PanelQuarterBtnW(int innerW, int gap) { return (innerW - 3 * gap) / 4; }
/* T-510: HOSTING's address-row buttons - LOCAL ADDRESS's COPY and INTERNET ADDRESS's SHOW and COPY, one width - narrower than
   PanelSideBtnW so the internet row's value (between the label column and its two buttons) has room; never narrower than a
   four-letter caption (SHOW / HIDE / COPY) at kPanelTextCharW plus kPanelAddrBtnPadPx each side. */
const int kPanelAddrBtnPadPx = 8;
inline int PanelAddrBtnW(int innerW)
{
    const int w = (innerW * 11) / 100, least = 4 * kPanelTextCharW + 2 * kPanelAddrBtnPadPx;
    return w < least ? least : w;
}
/* HOSTING's INVITE FRIENDS, at the right of the STEAM row: about a quarter of the width, so the Steam line keeps two lines'
   room between the label column and it, never narrower than its caption at kPanelTextCharW plus kPanelAddrBtnPadPx each side. */
inline int PanelInviteBtnW(int innerW)
{
    const int w = (innerW * 24) / 100, least = 14 * kPanelTextCharW + 2 * kPanelAddrBtnPadPx;
    return w < least ? least : w;
}
/* How far the Steam line's text area starts below the STEAM row's top, so its first line is level with the label and INVITE
   FRIENDS (one row tall, text centred): Kenshi's body line is about 0.4 of a row at 1280x720 and a third at 1920x1080
   ([UI] measured lineH 14 at rowH 34, 17 at rowH 52). */
inline int PanelSteamLineDy(int rowH) { return (rowH * 3) / 10; }
/* The STEAM row's height in half-rows: one row while Steam runs (its label and INVITE FRIENDS only), two while the line
   saying Steam is not running wraps beside them (the list's value). */
inline int PanelSteamRowHalves(int steamRunning) { return steamRunning != 0 ? 2 : 4; }
inline int PanelPortBoxW(int innerW)   { const int w = (innerW * 20) / 100; return w < kPanelMinFieldW ? kPanelMinFieldW : w; }
inline int PanelGapOf(int rowH)        { return rowH / 6 + 1; }
/* T-510: the INTERNET ADDRESS row's value width - from the label column to its two buttons (SHOW, COPY). */
inline int PanelNetAddrValueW(int innerW, int gap)
{
    PanelBand b;
    b.top = 0;
    b.h = 1;
    return PanelLabelBoxIn(innerW, gap, b, 0, 2 * (PanelAddrBtnW(innerW) + gap)).boxW;
}

/* The geometry every screen shares, from the parent as the template predicts it (ui.cpp measures the real client and
   fills the same fields). */
struct PanelGeom { PanelRect maxRect; int padX; int padY; int innerW; int rowH; };
inline PanelGeom PanelGeomIn(int parentW, int parentH)
{
    PanelGeom g;
    g.maxRect = PanelRectIn(parentW, parentH);
    const int cw = g.maxRect.w - kWindowCXChromeW;
    const int ch = g.maxRect.h - kWindowCXChromeH;
    const PanelInnerPad p = PanelInnerPadOf(cw, ch);
    g.padX = p.x;
    g.padY = p.y;
    g.innerW = cw - 2 * p.x;
    g.rowH = (ch - 2 * p.y) / kPanelRows;
    return g;
}
/* The window for one layout: the widest rectangle's width and left, as tall as the layout's rows (ui5c/ui5d: its text areas
   at `live`, PanelSlotHalves) plus the margin and the frame (chromeH: the template's 45, or the real window's measured
   one), centred on the parent, never taller than the largest rectangle. */
inline PanelRect PanelScreenRect(const PanelGeom& g, int parentH, int layout, int chromeH, const PanelLive& live = PanelLive())
{
    PanelRect r;
    r.left = g.maxRect.left;
    r.w = g.maxRect.w;
    r.h = PanelLayoutInnerH(layout, g.rowH, live) + 2 * g.padY + chromeH;
    if (r.h > g.maxRect.h) r.h = g.maxRect.h;
    r.top = (parentH - r.h) / 2;
    if (r.top < 0) r.top = 0;
    return r;
}

}   /* namespace coopui */
