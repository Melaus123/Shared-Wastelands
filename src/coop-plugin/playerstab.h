// playerstab.h - THE PLAYERS TAB (T-545) and other players' factions on the FACTION tab.
// A PLAYERS tab in Kenshi's management window, after AI: a table of every other player of this world (online or not)
// and this player's stance towards each (ALLY / NEUTRAL / HOSTILE through relations.cpp RelateSet; HOSTILE asks first). Every
// other player's faction is also made known to this game (the engine's own known-set insert), so the FACTION tab lists it
// with its number. The pure decisions are src/common/playerstab.h; see playerstab.cpp for how the pieces run.
#pragma once

#include <string>

namespace coop {

// Every in-world frame (coop.cpp detour_mainLoop): other players' factions made known; the PLAYERS tab added to the management
// window once that window exists in a world with other players (no hook: the tab control is found by its layout name), and
// the table and the line under it kept current while the tab shows. MAIN THREAD.
void PlayersTabTick();

// Every title-screen frame (coop.cpp detour_titleUpdate): a HOSTILE box left up from the world is taken down. MAIN THREAD.
void PlayersTabTitleTick();

// The world is being torn down (store.cpp, beside RelationsForgetQueue): every faction pointer this module holds dies with it.
// MAIN THREAD.
void PlayersTabForgetWorld();

// TEST-ONLY lever `playerstab [faction | open | select <slot> | stance ally|neutral|hostile | action invite|remove|disband|leave
// | confirm | cancel | shown]`: each step fires the control's own handler, as a click would (faction: the game's FACTION tab;
// action: the bottom line's button showing that action; confirm / cancel: the right / left button of whichever box is up);
// shown: one [PLAYERS] SHOWN line - every word and button the tab, the box and the FACTION NAME box show now; bare = the report
// line. MAIN THREAD (the command channel). Returns the status.
std::string PlayersTabCommand(const std::string& args);

// MAIN THREAD (team.cpp, when a removal made while this player was away has been written back with the world loaded): the
// FACTION box "You were removed from <team> while you were away. ..." is owed - shown once, when no other box is up.
void PlayersTabRemovedWhileAway(const std::string& teamName);

// The `report` verb's [PLAYERS] line and the ui[] readout's counters.
void ReportPlayersTab();
// The PLAYERS tab's item in the management window's tab control (a MyGUI::TabItem*), 0 while it is not in.
void* PlayersTabItemPtr();
// The management window's tab strip (a MyGUI::TabControl*), called before and after the mod adds or removes a tab of it: the
// game's own button width is noted while the control holds its seven, and the seven and the mod's tabs (PLAYERS, FALLEN) share
// the strip the seven filled - each button sized to its caption measured on the game's own tab buttons (src/common/fallentab.h
// StripWidths) while the window is open, equal widths filling the strip exactly (EqualWidths) until then. MAIN THREAD.
void MgmtTabsFit(void* tabControl);
// Each frame while a mod tab is in (PLAYERS and FALLEN both call it with their control): while the window is open and no fit
// with measured captions has been applied at the control's tab count and width, MgmtTabsFit again - at once when the window
// opens or the width changes, else at most every kFitEveryMs. A measured fit is the exit (noted on the control). MAIN THREAD.
void MgmtTabsFitTick(void* tabControl);
long long PlayersTabOpened();
long long PlayersTabStanceSet();
long long PlayersTabHostileConfirmed();

}
