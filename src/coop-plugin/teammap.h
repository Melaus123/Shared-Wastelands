// teammap.h - T-930 (owner decision 1436): teammates' characters on the big world map, as dots in team blue with a hover note.
// The pure half (the message, the "moved enough" rule, the team filter, the map's position sums) is src/common/mapdots.h.
#pragma once
namespace coop {
// Startup: hooks the world map window's frame (row MapWindow_frame, 1.0.65 0x49AC50 / 1.0.68 0x49B6E0). Without the row the
// feature is off and says so once.
void InstallTeamMap();
// MAIN THREAD, every frame after the engine's main loop (coop.cpp): this game's side as a SENDER - about twice a second, the
// positions of this player's own living characters to each teammate whose map is open (all of them once after that teammate's
// OPEN, then only those that moved more than a few metres) - and, as a VIEWER, the close when the map's frame stopped running.
void TeamMapTick();
// MAIN THREAD (the store layer's road-local LIVE): one MAP_DOTS message (cooplive::kInnerMapDots) from the player in `originSlot`.
void TeamMapLiveIn(int originSlot, const void* p, unsigned n);
// MAIN THREAD: the world is being torn down - every dot and every viewer forgotten.
void TeamMapWorldTeardown();
}
