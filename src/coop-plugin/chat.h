// chat.h - IN-GAME TEXT CHAT (the approved mock-up
// build/pages/text-chat-mockup.html). The pure rules (the message's bytes, the words, the colours, Tab, search, the kept lines,
// the fade, the road, chat.cfg) are in src/common/chatwire.h; see chat.cpp for how the window runs.
#pragma once

#include <string>

namespace coop {

// Every frame of the in-world pump (coop.cpp detour_mainLoop, after BugReportTick): Enter opens the window, the window and the
// closed feed are drawn, join / leave lines are found, chat.cfg is read and saved. MAIN THREAD.
void ChatTick();

// Every frame of the title screen's pump (coop.cpp detour_titleUpdate): whatever of the chat is still on screen from the world
// just left is taken down and its lines are forgotten. MAIN THREAD.
void ChatTitleTick();

// The pause menu's show is about to run (ui.cpp detour_pauseMenuShow): 1 = the chat window is open and takes this ESC (the show
// must not run; the tick closes the window), or the window just closed on an ESC that is still held or under half a second
// old; 0 = neither. Interlocked values and the key's state only - touches no widget.
int ChatTakesEscape();

// A chat message from the world server (store.cpp StoreLiveRoadLocal): `originSlot` is the sender's slot the server stamped.
// The bytes are checked (chatwire::Decode); a faction line from outside this player's faction, a private line naming another
// slot, and one sender's lines past 5 a second are dropped and counted; the rest is kept for the window. Nothing is drawn
// here. MAIN THREAD.
void ChatLiveIn(int originSlot, const void* p, unsigned n);

// The PLAYERS tab's MESSAGE button: open the chat with TO set to that player, on the next tick. Interlocked only.
void ChatOpenTo(int slot);

// TEST-ONLY lever (command_channel `chat ...`): open, send, fill, tolist, tab, click, opacity, move, esc, close, state.
// Never takes free text - only the fixed sample sentences. Returns "ok chat ..." or "error chat ...". MAIN THREAD.
std::string ChatCommand(const std::string& args);

}
