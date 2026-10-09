// command_channel - out-of-band control surface for the kenshi-coop mod.
//
// WHY THIS EXISTS: Kenshi reads input through OIS/DirectInput and ignores all
// synthetic mouse/keyboard events (finding F010), so no test harness can drive the
// game's UI. Instead the harness writes a command file and the mod executes it from
// inside the process, on the game's main thread.
//
// Files (next to the plugin DLL, i.e. <gamedir>\mods\Shared Wastelands\):
//   shared_wastelands_cmd.txt     - harness writes a single command line here
//   shared_wastelands_status.txt  - mod writes the outcome of the last command here
//
// Commands:
//   load <saveName>   - load a save by name via SaveManager::load
//
// DESIGN RULES OBSERVED (project design principles):
//  * Events over timers: we never "wait N frames and assume ready". We poll the
//    AUTHORITATIVE state (SaveManager::getSingleton(), anySavesExist()) on a recurring
//    tick and only act once it actually reports ready.
//  * Live reads at the moment of commitment: the singleton and anySavesExist() are
//    re-read at execution time, never cached from an earlier tick.
//  * Retries are recurrence-covered: if preconditions are not met we DO NOT consume
//    the command; the next tick retries, and each deferral is logged so silence is
//    diagnosable.
//  * Cheap on the hot path: the pump runs ~900x/second (F041), so the command file
//    is only stat-checked every kPollInterval calls, and only re-read when its
//    last-write time actually changes.

#pragma once

#include <string>

namespace coop {
// T-201 PP6': SaveManager::load(name) and the request code read back - 1 posted, 0 not posted, -1 not tried (MAIN THREAD).
int PostLoadChecked(const std::string& saveName);
int SaveRequestCodeNow();   /* SaveManager+0xA0 now: 0 none, 1 save, 2 load, 3 import, 4 new game; -1 no SaveManager or unreadable (MAIN THREAD) */
int LoadPostReady();   /* T-201 PP6' fold: 1 a load can be posted now, 0 a request is pending (wait), -1 never on this title */

// Called from the menu pump (TitleScreen::update) on the main thread.
// Cheap: does real work only every kPollInterval calls.
void CommandChannelTick();

// Absolute path of a file sitting next to our DLL, in UTF-8 - open it with the u8file.h calls, never an A-call.
// `filename` alone when Windows cannot name the DLL's file.
std::string PathNextToDll(const char* filename);

// P008 (defined in coop.cpp): flip the engine's world-wide AI freeze flag (F064).
bool SetWorldAIFreeze(bool on);

} // namespace coop
