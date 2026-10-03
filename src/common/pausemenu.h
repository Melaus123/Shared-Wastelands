/* src/common/pausemenu.h - T-514 (owner 448): WHERE THE PAUSE MENU'S ROWS GO.
 *
 * Kenshi's pause menu (data/gui/layout/Kenshi_MainMenuPopupPanel.layout) is a panel holding NEW GAME, SAVE GAME, LOAD GAME,
 * OPTIONS and EXIT GAME at one row pitch, then a gap, then RESUME. The mod may add REPORT A BUG (owner 378 d): its own row set
 * apart by that same gap below EXIT GAME, RESUME one more pitch-plus-gap further down. In a multiplayer world NEW GAME, SAVE
 * GAME and LOAD GAME are hidden and the rows below them close up to where NEW GAME stands; outside one every row is where
 * Kenshi put it. The game that hosts a multiplayer world also gets HOSTING (owner 454): the top row, every row below it one
 * pitch further down, the panel one pitch taller.
 *
 * Every answer is computed from Kenshi's own positions (recorded once, before anything was moved) and the panel's CURRENT
 * centre, never from where a row stands now - so the answer is the same whichever caller asks first, and asking again on the
 * same menu moves nothing. Pure integer arithmetic, no MyGUI; swept by src/coop-test/test_main.cpp.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

namespace pausemenu {

/* REPORT A BUG's button on the panel (bugreport.cpp makes it; ui.cpp places it). */
const char* const kBugButtonName = "BugReportPauseButton";
/* HOSTING's button on the panel (ui.cpp makes and places it). */
const char* const kHostingButtonName = "SWHostingPauseButton";

/* Kenshi's layout as the engine built it: each button's top inside the panel, and the panel's height. */
struct KenshiRows
{
    int newGame, saveGame, loadGame, options, exitGame, resume;
    int panelHeight;
};

/* Where everything goes: each button's top inside the panel (bug only meaningful when there is a REPORT A BUG row, hosting
   only when there is a HOSTING row), and the panel's own top and height in its parent. */
struct Placement
{
    int hosting, newGame, saveGame, loadGame, options, exitGame, bug, resume;
    int panelTop, panelHeight;
    int pitch, gap;
};

/* The pitch is SAVE GAME's top less NEW GAME's; the gap is what RESUME stands below EXIT GAME beyond one pitch. A layout whose
   rows do not run downward in Kenshi's order (no pitch, OPTIONS not below NEW GAME, EXIT GAME not below OPTIONS, RESUME not a
   full pitch below EXIT GAME) is refused. */
inline bool RowsUsable(const KenshiRows& k)
{
    const int pitch = k.saveGame - k.newGame;
    return pitch > 0 && k.options > k.newGame && k.exitGame > k.options && k.resume - k.exitGame >= pitch && k.panelHeight > 0;
}

/* The top that keeps a panel's centre where it is when its height goes from curHeight to newHeight. Going there and back
   returns the same top: (curHeight/2 - newHeight/2) + (newHeight/2 - curHeight/2) is 0 in integers too. */
inline int CentredTop(int curTop, int curHeight, int newHeight)
{
    return curTop + curHeight / 2 - newHeight / 2;
}

/* The placement for this open. `closeUp` = a multiplayer world (the three rows hidden); `hasBug` = REPORT A BUG is on the
   panel; `hasHosting` = HOSTING is on the panel (the top row, where NEW GAME stands; everything below it one pitch lower);
   curPanelTop / curPanelHeight = the panel as it stands now. false (and *out untouched) when the rows are refused. */
inline bool Place(const KenshiRows& k, bool closeUp, bool hasBug, bool hasHosting, int curPanelTop, int curPanelHeight, Placement* out)
{
    if (!RowsUsable(k)) return false;
    Placement p;
    p.pitch = k.saveGame - k.newGame;
    p.gap = k.resume - k.exitGame - p.pitch;
    const int step = p.pitch + p.gap;
    const int lift = closeUp ? k.options - k.newGame : 0;   /* the three rows' height, taken out */
    const int grow = hasBug ? step : 0;                     /* REPORT A BUG's row and its gap, put in */
    const int down = hasHosting ? p.pitch : 0;              /* HOSTING's row, put in at the top */
    p.hosting = k.newGame;
    p.newGame = k.newGame + down;
    p.saveGame = k.saveGame + down;
    p.loadGame = k.loadGame + down;
    p.options = k.options - lift + down;
    p.exitGame = k.exitGame - lift + down;
    p.bug = p.exitGame + step;
    p.resume = k.resume - lift + grow + down;
    p.panelHeight = k.panelHeight - lift + grow + down;
    p.panelTop = CentredTop(curPanelTop, curPanelHeight, p.panelHeight);
    *out = p;
    return true;
}

}   /* namespace pausemenu */
