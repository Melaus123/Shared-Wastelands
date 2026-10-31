// townver.h - a town's version (its normal record, destroyed, taken over) decided by the game running the town's area with the
// engine's own chooser, kept on the world server (src/common/townverwire.h, TOWN_VER 87) and put on every game's town each time
// its area is switched on, before the town's people are made. Owner decision 260: "player ally / enemy" conditions are ignored
// (only the named-character events decide).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace coop {

typedef bool (*TvChooserFn)(void* zoneManager, void* town);
void TownVerInstall();   // preload, main thread (from InstallWorldGen): the world-state hooks; the version setter is called, not hooked
// The checkForRepopulateTown detour's body (worldgen.cpp): MAIN THREAD ONLY does anything; any other thread runs `orig` untouched.
bool TownVerChoose(void* zoneManager, void* town, TvChooserFn orig);
void TownVerArrive(const std::vector<char>& payload);   // MAIN THREAD (store pump): a STORED / REFUSED record from the world server
void TownVerWorldLoadBegan();                           // MAIN THREAD (StoreWorldLoadBegan): queued sweeps and seeds cleared, the note book marked ended
void TownVerTeardownDone();                             // MAIN THREAD (after the engine's teardown / load call returns): an ended note book is emptied
void TownVerWelcome();                                  // MAIN THREAD (store pump, at a WELCOME): one from another world drops the last world's records, pending writes and stops
void TownVerFeedBegin();                                // MAIN THREAD (store pump): the record feed's first page begins - the list follows it
void TownVerFeedPageEnd();                              // MAIN THREAD (store pump): a feed page ended - after a BEGIN, the whole list is here
void TownVerTick();                                     // MAIN THREAD, every frame: records onto towns whose area is not loaded here; old-world seeds
std::string TownVerReportToken();                       // townOv[...] for the [P032] REPORT line
// THE "BUILT WITH" NOTE inside each area file (store.cpp TownNote*, townverwire.h kTvNoteId)
int TownVerNoteTowns(int sx, int sy, std::vector<std::pair<std::string, std::string> >* out);   // MAIN THREAD: the towns the area's next write notes, each with the version its buildings in that area are built with; -1 = no area
void TownVerNoteWritten(const std::string& zoneId, int ok, int reused, const std::vector<std::pair<std::string, std::string> >& towns);   // MAIN THREAD: a note was added to an area write
void TownVerNoteFailed(const std::string& zoneId, const char* why);                     // MAIN THREAD: an area write on the main thread got no note
void TownVerNoteOffThread();                                                            // ANY THREAD: an area write off the main thread (no note)
void TownVerNoteOffThreadRead(int sx, int sy);                                         // ANY THREAD: an area file read off the main thread - its notes are forgotten
void TownVerNoteRead(int sx, int sy, const std::string& zoneId, int loaded, int source, int unreadable, const std::vector<std::pair<std::string, std::string> >& notes, const std::string& path);   // MAIN THREAD: an area file was read - its notes replace that area's

}
