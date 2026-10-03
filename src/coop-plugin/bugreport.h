// bugreport.h - REPORT A BUG (T-461): the button on the title screen and in Kenshi's pause menu, the report window, and the
// report's gathering and sending. The pure rules (words, scrub, compression, budget, zip, the nearby-log messages) are in
// src/common/bugreport.h; see bugreport.cpp for how the pieces run.
#pragma once

#include <cstddef>

namespace coop {

// Every frame, on the pump that is running: atTitle 1 from the title screen's update (coop.cpp detour_titleUpdate), 0 from the
// in-world main loop (coop.cpp detour_mainLoop). Builds the title-screen button, runs the report window and its boxes, hands
// nearby players' answers to the report being made, and sends this game's own answer to another player's ask. MAIN THREAD.
void BugReportTick(int atTitle);

// The pause menu's own show has just run (ui.cpp detour_pauseMenuShow, after its greying): REPORT A BUG is put on its own row
// between EXIT GAME and RESUME if this menu does not hold it yet. `panel` = the engine's MainMenuPopup object. MAIN THREAD.
void BugReportPauseMenuShown(void* panel);

// ESC at the title screen (ui.cpp detour_titleCloseOtherBits): 1 = the report window or one of its boxes is up and takes this
// ESC (the tick answers it: CANCEL, BACK or OK), so Kenshi must not quit; 0 = nothing of this feature is up. Interlocked only -
// it may run on whichever thread the engine dispatches keys from, and touches no widget.
int BugReportTakesEscape();

// A LOG_ASK or LOG_PART from the world server (store.cpp StoreLiveRoadLocal): `originSlot` is the sender's slot the server
// stamped. An ask starts this game's answer (its current log, scrubbed and compressed, sent back by slot - the player is not
// told); a part joins the answer to this game's own ask. MAIN THREAD; touches no widget and no engine state.
void BugReportLiveIn(unsigned int innerType, unsigned int originSlot, const char* data, size_t len);

}
