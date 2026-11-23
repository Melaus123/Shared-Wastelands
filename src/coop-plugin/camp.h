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
/* T-1429 A1 (campgame.h): an area save's camp pieces - Begin before the engine's one-area save, End after it (store.cpp
   detour_saveLevelData); End logs '[CAMP] camp keep: area save x.y: n camp pieces saved ...' when the area had any. MAIN THREAD (else no-ops). */
void CampAreaSaveBegin();
void CampAreaSaveEnd(void* content);
// T-1429 camp note design (src/common/camplaid.h): the camp note inside each area file, the camp layer the world server names, and each
// area's camp pieces record (camplaid.<sx>.<sy>)
void CampNoteRead(const std::string& zoneId, int fileOk, int found, int unreadable, const std::vector<std::string>& keys, int onMain, const std::string& path);   // ANY THREAD (store.cpp CampNoteAfterLoad): a read of the area's file replaces its note
void CampNoteKeysForSave(const std::string& zoneId, std::vector<std::string>* keys, int* over);   // MAIN THREAD (store.cpp CampNoteBeforeSave): the keys the area's save writes
void CampNoteSaved(const std::string& zoneId, int keys, int over, const char* fail);   // MAIN THREAD: the note was written with `keys` (-1 = not written: fail)
void CampLayerAnswer(int x, int y, int layer, int kind);   // MAIN THREAD (store.cpp AreaFreshArrive): the world server's camp layer for the area's coming load
void CampLayerAsked(int x, int y);   // MAIN THREAD (store.cpp AfDecide / AfSendSettleList): a new load's ask of the area - an answer kept from an earlier load is dropped (T-1429 F1612 item 4)
void CampAreaLoadStart(int x, int y);   // MAIN THREAD (store.cpp detour_afZoneMapActivate, the engine's 'load decided' for the area, after its ask): this load is in force from now, file read or not, and the map number at its start is kept (T-1429 F1634)
void CampAreaHeld(int x, int y);    // MAIN THREAD (store.cpp AreaFreshArrive): the world server's HELD - this game holds the area and no other game is its camp layer: its kept owed steps run, its kept camp pieces write goes (T-1429 F1617 items 1 / 4)
int  CampAreaUnloadSector(void* content, int* sx, int* sy);   // ANY THREAD (items.cpp detour_zoneDeactivate, before the engine's unload): the unloading area's square; 1 = read (T-1429 F1627 item 3)
void CampAreaUnloaded(int named, int sx, int sy);              // ANY THREAD (items.cpp detour_zoneDeactivate, after the unload and its area save, on every road): the area's unload is counted; the main thread ends its load (kept steps dropped, unsaved pieces forgotten, the read and a waiting answer end) (T-1429 F1627 item 3)
void CampLaidArrive(const std::string& id, const std::vector<char>& bytes);   // store.cpp ApplyRemoteRecord: a camplaid record from the world server
void CampLaidEcho(const std::string& id, int hasStamp, long long stamp);   // the world server stored this game's camplaid write sent with that stamp
void CampLaidRefused(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen);   // items.cpp: BOX_REFUSE of a camplaid write
int CampViewPoint(float* vp, float* dist);           // T-1235: this game's view point (0x7F1DC0) and the engine's spot distance (1382.4) - camp.cpp's own read (CpViewPod); 0 = they do not read. Any thread the engine's spot test runs on; towngen.cpp reads it on the main thread

}
