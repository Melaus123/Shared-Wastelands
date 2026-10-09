// farm.h - par16 (parity backlog P16; parity-audit-buildings-world.md H5 / M5): a farm's growth and harvest belong to ONE game.
// The writer is the production machines' writer (items.cpp FarmWriterHere: the owner's game while it is online and holds the
// area, else the area holder). The writer's game grows its farm and publishes the whole growth state (MSG_BUILD kind 6 FARM,
// src/common/farmwire.h) on a change, at most every 2 s per farm, and re-sends it every 30 s. Every other game HOLDS its copy's
// growth tick (FarmBuilding::update 0xE5540 runs only its tail, vt+0x598, the engine's own early-out path) and writes the
// writer's state onto its copy at the K2 safe point. A worker harvesting on a non-writer's game does not harvest there: the
// operate WORK (FarmBuilding::operate 0xE59A0's amount x the frame's g_dt) and the worker (uid + farming skill) are sent to
// the writer (kind 7 FARM_OP), whose game runs the engine's own operate with work / its own g_dt and its copy of the worker
// (or the carried skill) - the crop is made once, into the farm's own output box, and reaches the other game as an ordinary
// box move. par16 fold (review-par16 #1-#9): the worker and real work, harvested plants hidden on the copy, the engine's
// guards on the visual refresh, a farm-own lookup hint, the catch-up growth (timeSkip 0xDF6A0) held, the water carried,
// FARM accepted only while another game writes, queued work kept across a writer change, a 1024-row table (full = held).
#pragma once
#include <string>
namespace coopfarm { struct FarmMsg; }   /* src/common/farmwire.h */
namespace coop {
void InstallFarm();                                   // preload (from InstallBuild): the farm hooks (update, operate, timeSkip, yield)
void FarmTick();                                      // MAIN THREAD, every frame (from BuildTick): verdicts, the held games' FARM_OP sends
void FarmDrain();                                     // K2 safe point (from BuildCopyDrain): apply FARM / FARM_OP, the writer's publish, the lever
void FarmForgetWorld();                               // world teardown (from BuildForgetWorld)
void FarmNoteRecv(const coopfarm::FarmMsg& m, unsigned int fromPeer);   // MAIN THREAD (session dispatch): a decoded FARM / FARM_OP, kept
std::string FarmTestArm(const std::string& arg);      // MAIN THREAD: `buildtest farm list | grow <key-substring> <age> | harvest <key-substring> [amount]` (test-only)
void ReportFarm();                                    // one [FARM] REPORT line (from ReportBuild)
// ANY THREAD (items.cpp detour_prodOperate): a worker's step on a production building this game does not write (or whose
// writer is not named yet) - its work is kept for FarmTick to send to the writer as MINE_OP. 1 kept (the caller does not run
// the step); 0 not kept (no key, the table full, an unusable amount: the caller runs it).
int  MineStepRelay(void* pb, void* who, float amount);
// ANY THREAD: 1 while another player is in this world (FarmTick's link test)
int  MineLinked();
}
