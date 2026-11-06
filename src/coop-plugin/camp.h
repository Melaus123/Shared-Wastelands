// camp.h - T-929: the world's bandit camps and animal dens are ONE list on the world server (src/common/campwire.h, CAMP 93). The
// engine's own camp pass draws a camp; the game proposes it; the camp is built only once the list holds it, on every game, by the
// engine's own build with the camp's kind and spot forced (src/common/campgame.h).
#pragma once

#include <string>
#include <vector>

namespace coop {

void CampInstall();                                  // preload, main thread (from InstallWorldGen): the four camp hooks
void CampArrive(const std::vector<char>& payload);   // MAIN THREAD (store pump): a CAMP message from the world server
void CampWorldLoadBegan();                           // MAIN THREAD (StoreWorldLoadBegan): waiting builds, the proposal and the list end with the world
void CampTick();                                     // MAIN THREAD, every frame: a drawn camp goes out (or is answered here in single player); build results
std::string CampReportToken();                       // camps[...] for the [P032] REPORT line
// T-929 part 6 (src/common/campgame.h CampHomeWay), MAIN THREAD: a group's home camp by the world's camp list key.
void CampWorldTeardown();                            // MAIN THREAD (the store's world teardown): this game's camps by key, and the removals, end with the world
std::string CampListKeyOfTown(void* town);           // the list key of this game's camp `town` ("?" = a camp on the engine's camp list this game names no key for while it holds the whole list; "" = not a camp, or no list)
void* CampTownOfListKey(const std::string& key);     // this game's camp of that list key (0 = not built here, or no longer that camp)
int CampViewPoint(float* vp, float* dist);           // T-1235: this game's view point (0x7F1DC0) and the engine's spot distance (1382.4) - camp.cpp's own read (CpViewPod); 0 = they do not read. Any thread the engine's spot test runs on; towngen.cpp reads it on the main thread

}
