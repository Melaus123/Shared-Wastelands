// ping.h - T-1393 (owner decisions 1725 / 1726 a / 1728, approved mock-up build/pages/ping-mockup.html): PINGS. G pings the spot
// under the mouse (the world map's spot when the map is open, else the ground); the player's teammates - never a hostile player -
// see a marker with the name, an edge arrow when it is off screen, the map marker and a chat line. The pure half (the bytes, the
// rate limit, the life, the filter, the words, the sums, the shapes) is src/common/pingwire.h.
#pragma once
#include <string>
namespace MyGUI { class Widget; }
namespace coop {
// PLUGIN START (coop.cpp): T-1437 - the post-hook on InputHandler::initialise (the game's key defaults, re-run by Controls >
// Defaults: the Ping action added again with G) and the hook on the Controls list's spacer (the 'Ping' row at the end of the key rows).
void InstallPing();
// MAIN THREAD, the title screen's tick (coop.cpp), once: the Ping action added to Kenshi's key list, its key from controls.cfg's
// `sw_ping=` lines (the game's own loadConfig ran before the plugin and skipped the name), none -> G.
void PingTitleTick();
// MAIN THREAD, every in-world frame after the engine's main loop (coop.cpp): the Ping key (its down byte), then every live ping's world marker or
// edge arrow placed, faded and, when its life is over, taken down.
void PingTick();
// MAIN THREAD, the world map window's frame while the map is open (teammap.cpp FrameWork, after the game's own work): the map's
// widgets and sums for this frame - the G key's map spot is read from them - and every live ping's map marker placed.
void PingMapFrame(MyGUI::Widget* windowW, MyGUI::Widget* page, MyGUI::Widget* image, MyGUI::Widget* parent,
                  float originX, float originZ, float spanX, float spanZ);
// MAIN THREAD (the store layer's road-local LIVE): one SPOT_PING (cooplive::kInnerSpotPing) from the player in `originSlot`.
// Checked (bytes, teammate, hostility, the rate) and kept; no widget is touched here.
void PingLiveIn(int originSlot, const void* p, unsigned n);
// MAIN THREAD: the world is being torn down - every ping taken down and forgotten.
void PingWorldTeardown();
// TEST-ONLY lever (DEV build; command_channel `pingtest ...`): world <x> <z> | map <x> <z> | burst <n> world|map <x> <z> [nogate]
// | press | show. Returns "ok pingtest ..." or "error pingtest ...". MAIN THREAD.
std::string PingTestCommand(const std::string& args);
}
