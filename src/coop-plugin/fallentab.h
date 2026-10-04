// fallentab.h - THE FALLEN TAB (T-556, owner 485-510; the approved screen is build/pages/resurrection.html).
// A FALLEN tab in Kenshi's management window, after PLAYERS (after AI while PLAYERS is not in), in every multiplayer world: this
// player's fallen characters (resurrect.cpp's copy of the world server's list), the price lines, the squadmate drop-down and
// BRING BACK, the confirm box, the caption line and the game's message line. BRING BACK takes the TEST lever's own road
// (resurrect.h ResurrectBringBackFor). The words and states are src/common/fallentab.h; see fallentab.cpp for how it runs.
#pragma once
#include <string>

namespace coop {

// Every in-world frame (coop.cpp detour_mainLoop, after PlayersTabTick): the tab added to the management window once that
// window exists in a multiplayer world (no hook: the tab control is found by its layout name, as PLAYERS), kept current while it
// shows, and a confirmed bring-back run on the frame after the press (so the progress line is drawn first). MAIN THREAD.
void FallenTabTick();
// Every title-screen frame: a confirm box left up from the world is taken down. MAIN THREAD.
void FallenTabTitleTick();
// The world is being torn down (store.cpp, beside PlayersTabForgetWorld): the selection, the squadmates and the box go. MAIN THREAD.
void FallenTabForgetWorld();
// TEST-ONLY lever `resurrect tab [open | select <row> | mate <i> | press | confirm | cancel | report]`: each step fires the control's
// own handler, as a click would, and only where a mouse could reach it. MAIN THREAD (the command channel). Returns the status.
std::string FallenTabCommand(const std::string& args);
// The `report` verb's "[FALLEN] TAB REPORT" line.
void ReportFallenTab();
// The FALLEN tab's item in the management window's tab control (a MyGUI::TabItem*), 0 while it is not in.
void* FallenTabItemPtr();

}
