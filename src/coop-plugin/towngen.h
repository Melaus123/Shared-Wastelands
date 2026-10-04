// P4 / P8a (DECISION 48): a town's squad generation.  THE GAME THAT HOLDS THE AREA GENERATES ITS TOWN'S PEOPLE
// and publishes them; the fixed host-generates / client-refuses roles of decision 25 are RETIRED and the client
// declaration that carried them is gone.  The gate is MayInventFromView over the notebook's area map - the same
// holder test both games already use to decide what they may publish - with the two refusals that are not
// answers about the area (no link yet, no fresh map) refusing identically on both games.
#pragma once
#include <string>
#include <vector>   /* refill1: RefillNoteRows */
namespace coop {
/* recruit3: the host option `recruitmult` (auto|1..4), fed from the notebook's OPTIONS map on the MAIN thread */
void RecruitMultMapBegin();
void RecruitMultOption(const std::string& v);
void RecruitMultMapEnd(bool complete);   /* a complete map with no recruitmult row = auto */
std::string RecruitMultStatus();         /* "option=<> slotsSeen=<> effective=x<>" */
void InstallTownGen();
void SetTownGenOn(bool on);        // lever "towngen on|off" (default on): off = never refuse (control runs)
int  TownGenMadeThisPlatoon(const char* platoonId);  /* P8a: 1 = THIS game's town-generation hook allowed the group with this id (the roster's spawnCause=towngen); bounded set, cleared at every world teardown */
/* PROBE-START: P085 - this game's allowed town creations in sector x,y since the world loaded (type 2 = residents); any thread */
void TownGenP085Sector(int x, int y, long long* residents, long long* squads);
/* PROBE-END: P085 */
/* refill1 - defined in spawn.cpp: `roster full`'s context read of one character (its Platoon, squad template +0x108,
   squad type +0xA8, home town +0x148+0x30); 1 = read. The refill's free-recruit count (towngen.cpp) walks with it. */
int TownCharContextPod(void* c, void** platoon, void** squadGd, int* squadType, void** town);
/* refill1 (docs/design-refill1.md s4): the TEST verb `refill [now <town stringID or name>]`, MAIN THREAD */
std::string RefillCommand(const std::string& args);
/* towns2: the TEST verb `townres` (alias `towns2`) [now <town stringID or name> [nosight] | kill <town> <n>], MAIN THREAD */
std::string Towns2Command(const std::string& args);
/* refill1: the notebook's town_bars rows (store.cpp's drain, MAIN THREAD) - a WELCOME empties the table, TOWN_BAR fills it */
void RefillTableReset();
void RefillNoteRows(const std::vector<char>& payload);
/* T-581: the world server's owed town populations (src/common/owedpop.h) - store.cpp's drain, MAIN THREAD: a WELCOME empties
   this game's copy of the table, OWED fills and changes it. The TEST-ONLY verb `owedtest aside on|off | show`. */
void TownGenOwedReset();
void TownGenOwedArrive(const std::vector<char>& payload);
std::string TownGenOwedLever(const std::string& arg);
void ReportTownGen();              // the [TG] report line
void TownGenTick();                // MAIN THREAD: captures the main thread id on the first tick (review-p4a HIGH-2)
void TownGenWorldTeardown();
/* T-392 (owner 335 a): a building's residents. worldgen.cpp's populateBuilding detour asks TownGenResidentsGate before the
   original: 1 = run it now; 0 = SET ASIDE (t438-towngen-h: at load the original STILL runs - faction, furniture - with its
   squads refused as set aside, townreoffer::PopulateMode) (no notebook link, no fresh map, or nobody holds the area), kept by the building's
   position key and re-offered from the town's own check-up through WorldGenRerunPopulate (worldgen.cpp: 1 = the original ran,
   0 = set aside again, -1 = no original / a fault inside the engine call). MAIN THREAD. TownOfBuilding is spawn.cpp's guarded
   Building::asTown 0xF6BE0, ANY THREAD. */
int  TownGenResidentsGate(void* factory, void* building);
void TownGenResidentsDone(int ran);   /* T-392 fold: called when the original populateBuilding returns (the gate's building-sector context ends). T-438: and the
                                         set-aside flag; ran = 1 the engine call ran (a set-aside building's furniture line is logged), 0 it did not */
int  WorldGenRerunPopulate(void* factory, void* building);
void* TownOfBuilding(void* building);
int  TownGenTownSid(void* town, char* out, int cap);   /* decision 34: the town's string id (guarded), 1 ok 0 fault; any thread */       // forgets the refusal keys (town x cause) and their counts
}
