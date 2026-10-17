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

// T-810 (money.cpp): a grey line of this game's own - "You sent Bob c.500" / "Bob sent you c.500" / money that came back -
// kept for the window as a join line is; `slot` is the other player's (-1 none). MAIN THREAD.
void ChatMoneyLine(int slot, const std::string& name, const std::string& sentence);

// T-546 (owner 521 / T-634): one of the game's own interface sounds by its event name (e.g. "Prospect_Click"), on T-815's road -
// the SoundPostEvent row, the interface object and the fault latch the message sounds use (a fault turns both off for the process).
// Not a chat message: no chat.cfg switch governs it. 1 = played, 0 = the sound engine refused it, -1 = no world running,
// -2 = the sound call is off after a fault, -3 = the address table has no usable SoundPostEvent row. MAIN THREAD.
int ChatPlayUiEvent(const char* ev);

// T-546: one of the game's notification sounds by its state in the sound switch group "Notifications" (e.g. "Thing_Complete", the
// research-complete sound), played as the game plays it: SetSwitch("Notifications", state, 0x6E) - table row SoundSetSwitch - then
// PostEvent("Notifications"). Same gates and fault latch as ChatPlayUiEvent. 1 = played, 0 = the sound engine refused the post,
// -1 = no world running, -2 = the sound call is off after a fault, -3 = the address table has no usable SoundSetSwitch or
// SoundPostEvent row, -4 = SetSwitch did not return success (its AKRESULT in *switchResult when given; 0 when it was not called).
// MAIN THREAD.
int ChatPlayNotification(const char* state, unsigned long* switchResult = 0);

// TEST-ONLY lever (command_channel `chat ...`): open, send, fill, tolist, tab, click, opacity, move, esc, close, state.
// Never takes free text - only the fixed sample sentences. Returns "ok chat ..." or "error chat ...". MAIN THREAD.
std::string ChatCommand(const std::string& args);

}
