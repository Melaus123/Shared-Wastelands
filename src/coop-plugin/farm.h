// farm.h - par16 (parity backlog P16; parity-audit-buildings-world.md H5 / M5): a farm's growth and harvest belong to ONE game.
// The writer is the production machines' writer (items.cpp FarmWriterHere: the owner's game while it is online and holds the
// area, else the area holder). The writer's game grows its farm and publishes the whole growth state (MSG_BUILD kind 6 FARM,
// src/common/farmwire.h) on a change, at most every 2 s per farm, and re-sends it every 30 s - a farm no player owns only: a player's
// farm's crop (growth, death, plant start, plants harvested, plant ages) rides its piece record alone (build.cpp PieceRecFarmNote /
// PcfDrain, at each 1% and each harvest, replant and death start), and its FARM goes only when the rest moves (water, harvest bar,
// work state, cleared value, work spot), never written onto a player's farm's crop (farmwire.h FarmPublishDueOf / FarmApplyCrop).
// Every other game HOLDS its copy's
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
int FarmPutInHold(int ownOp, unsigned int uid);   /* T-1211, MAIN THREAD: hold an own caging's refusal while its put-in is owed (spawn.cpp) */
void FarmDrain();                                     // K2 safe point (from BuildCopyDrain): apply FARM / FARM_OP, the writer's publish, the lever
void FarmForgetWorld();                               // world teardown (from BuildForgetWorld)
void FarmNoteRecv(const coopfarm::FarmMsg& m, unsigned int fromPeer);   // MAIN THREAD (session dispatch): a decoded FARM / FARM_OP, kept
std::string FarmTestArm(const std::string& arg);      // MAIN THREAD: `buildtest farm list | grow <key-substring> <age> | harvest <key-substring> [amount] | water <key-substring> <amount 0-1>` (test-only)
void ReportFarm();                                    // one [FARM] REPORT line (from ReportBuild)
// ANY THREAD (items.cpp detour_prodOperate): a worker's step on a production building this game does not write (or whose
// writer is not named yet) - its work is kept for FarmTick to send to the writer as MINE_OP. 1 kept (the caller does not run
// the step); 0 not kept (no key, the table full, an unusable amount: the caller runs it).
int  MineStepRelay(void* pb, void* who, float amount);
// ANY THREAD: 1 while another player is in this world (FarmTick's link test)
int  MineLinked();
// T-1125, ANY THREAD, crash-guarded: 1 = the character is in the building's worker list (a torture building's occupant), 0 = not, -1 unreadable
int  FarmWorkerListedPod(const void* b, const void* c);
// ANY THREAD (items.cpp detour_prodOperate): a worker's step ran here on a production building this game writes - its bar and
// state go to the other games (MINE_STATE, FarmTick) while the work goes on, and once when it stops.
void MineWorkedNote(void* pb);
// ANY THREAD, crash-guarded, read-only: a production building's bar *(+0x448) and state +0x468. 1 both read, 2 the state
// only (no output record), 0 unreadable.
int  MineBarReadPod(void* b, float* progress, int* state);
// T-1047 step 2b, ANY THREAD: 1 = a worker's step ran here on the production building at this P7n key within the last
// coopfarm::kMineStateWorkedMs (MINE_STATE's worked window, the table MineWorkedNote marks) - its piece record's bar bands are fine then
int  MineWorkedRecently(const char* key);
// T-1047 step 2b, MAIN THREAD (defined in build.cpp's power section): a player's power piece's bar and work state ride its piece record.
// PwMineStateSkip: 1 = no MINE_STATE is sent for b (its record carries them from this game). PwMineStateDrop: 1 = a received MINE_STATE
// for b is dropped (the POWER_STATE receiver's rule, PwStateDrop: its record's values are still to land here).
int  PwMineStateSkip(void* b, const char* key);
int  PwMineStateDrop(void* b, const std::string& key);
int  FarmKeptLay(void* b, float growth, float death, float start, int harvested, int* plants);   // KEPT, K2: a kept farm onto the live one
int  FarmKeptRead(void* b, float* growth, float* death, float* start, int* harvested, int* plants);   // KEPT / bldstate: read-only
// K2 (build.cpp PcfDrain / PieceRecFarmNote): a player farm's crop from its piece record laid onto the live farm (FarmKeptLay);
// every lay also re-times a load catch-up still held for it to catchUpHours (catchUp 1: the hours since the laid crop's minute) or
// drops it (catchUp 0: none owed from that minute). 1 laid, 0 not (*plants 0: its plants are not made yet)
int  FarmRecordLay(void* b, float growth, float death, float start, int harvested, int catchUp, float catchUpHours, int* plants);
// ANY THREAD, read-only: the world clock's minute count as the farm's load reads it (0x66C070). 1 read, 0 not
int  FarmClockMinute(unsigned int* m);
// MAIN THREAD (defined in build.cpp beside PieceRecFarmNote; FarmDrain's catch-up replay): 1 = the farm at key waits - its area's
// records are not all here and applied yet, or its stored crop is not laid this load (piecerec::PieceFarmLayOwed); its held load
// catch-up waits (each lay re-times it: FarmRecordLay); 0 = nothing to wait for
int  PieceRecFarmLayOwed(const std::string& key);
// MAIN THREAD (defined in build.cpp's piece-record section; FarmDrain calls it on the farm's writer): 1 = a player's piece (its piece
// record is stored) - its crop is noted for its piece record (a write marked at a clear event or a band step); 0 = no stored record,
// or no write row free (the FARM road carries the crop)
int  PieceRecFarmNote(const std::string& key, void* piece, float growth, float death, float start, int harvested);
// MAIN THREAD (defined beside it; FarmDrain's FARM apply): 1 = the stored piece record of the farm at the key carries its crop (a fact
// every game shares) - its crop comes from that record alone; 0 = not (no record, or no crop in it yet, or a single-player world)
int  PieceRecFarmIsPlayer(const std::string& key);
}
