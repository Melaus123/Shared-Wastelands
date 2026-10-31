#pragma once
/* kmode.h - DEDICATED: THE WORLD'S START MODE, plugin side ([KMODE] lines). The rules are src/common/startmode.h.

   The notebook sends startmode=freeplay|kenshi in its OPTIONS maps (the WELCOME's map first). In kenshi mode the SaveManager::newGame
   pre-hook (settings.cpp) replaces the start the player picked with one of the six Drifter starts, picked at random, when that record
   is loaded. A returning player loads a save and never reaches newGame. All MAIN THREAD. */
#include <string>
#include <vector>

namespace coop
{
    void StartModeOptionsBegin();                                          /* store.cpp's OPTIONS handler: a map starts */
    bool StartModeOption(const std::string& key, const std::string& value); /* ... one row; true = it was the startmode row */
    void StartModeOptionsEnd(bool complete, bool refusal);                 /* ... the map ended: the world's mode is decided */
    void StartModeDedInfo(unsigned startMode);                             /* DEDINFO (in the world): logged when it disagrees */
    int  StartModeWorld();                                                 /* swstart::Mode of the current link's world (freeplay = none) */

    /* SaveManager::newGame pre-hook: `picked` is the engine's const std::string* (the start record's string id the NEW GAME window
       chose). true = start with *use instead. */
    bool StartModeNewGame(const void* picked, std::string* use);

    /* store.cpp's OPTIONS handler: the map's timemode (fixed | consensus) - the footer's Time on a hosted world */
    void StartModeTimeOption(const std::string& value);
    /* store.cpp: a multiplayer save finished in `dir` (its own-records mark written) - the lean's key file written or removed there
       when owed; a multiplayer save is loaded from `dir` - the lean read from it (absent = unknown). */
    void StartModeSaveWritten(const std::string& dir);
    void StartModeSaveLoaded(const std::string& dir);
    /* the PLAYERS tab's footer: its label / value lines, top to bottom (swstart::FooterLines; lines with no value left out) */
    void StartModeFooter(std::vector<std::string>* labels, std::vector<std::string>* values);

    /* coop.cpp's start: ONLY when the last world this game joined was kenshi mode (swstart::DataModDecide over the state file beside
       mods.cfg), copy <this mod>\dedicatedserver\dedicatedserver.mod to <Kenshi>\mods\dedicatedserver\ when it differs, and switch it
       on in <Kenshi>\data\mods.cfg when it is not a line there; otherwise take out a mods.cfg line this plugin added earlier, if mods.cfg is still as it left it. Logs what
       it did; never refuses the mod's start. */
    void InstallStartModeData();

    /* MAIN THREAD, a world loaded: a record with this string id is in the game's data (any type). Fault-guarded; false when unreadable. */
    bool GameRecordLoaded(const std::string& sid);

    /* MAIN THREAD: every Drifter start (swstart::kLeans) is in the game's data as a START record (type 64) in this run. false names
       the first one that is not in *why. Fault-guarded; false when unreadable. */
    bool DrifterStartsLoaded(std::string* why);
}
