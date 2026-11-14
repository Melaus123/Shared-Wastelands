// P4 / P8a (DECISION 48): a town's squad generation.  THE GAME THAT HOLDS THE AREA GENERATES ITS TOWN'S PEOPLE
// and publishes them; the fixed host-generates / client-refuses roles of decision 25 are RETIRED and the client
// declaration that carried them is gone.  The gate is MayMakeFromView over the notebook's area map (T-1261, owner
// decision 1531: the named holder only, no loaded-here or ring-1 presumption) - with the two refusals that are not
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
/* T-4 TEST-ONLY verb `towntime period <hours 1-120> | period show`: the town clock period for every NEW due on this game, MAIN THREAD */
std::string TownTimePeriodCommand(const std::string& args);
/* refill1: the notebook's town_bars rows (store.cpp's drain, MAIN THREAD) - a WELCOME empties the table, TOWN_BAR fills it */
void RefillTableReset();
void RefillNoteRows(const std::vector<char>& payload);
/* T-581: the world server's owed town populations (src/common/owedpop.h) - store.cpp's drain, MAIN THREAD: a WELCOME empties
   this game's copy of the table, OWED fills and changes it. The TEST-ONLY verb `owedtest aside on|off | show`. */
void TownGenOwedReset();
void TownGenOwedArrive(const std::vector<char>& payload);
/* The world server's town loss rows (src/common/townrefill.h) - store.cpp's drain, MAIN THREAD: a WELCOME empties this game's copy
   (the refills this game has not had taken yet stay in it), TOWN_LOSS fills it. */
void TownLossReset();
/* TEST-ONLY lever `townfill force | show` (towngen.cpp TownFillLeverImpl): force starts the refill of the next held town near this
   game's player without the empty-town verdict and the buildings' home answers; show lists the refills running here. MAIN THREAD */
std::string TownFillLever(const std::string& arg);
void TownLossNoteRows(const std::vector<char>& payload);
/* MAIN THREAD (store.cpp, the frame pump or the exit routine) - this game's own profile save, asked for as save request reqSeq
   (StoreSaveRequestSeq's count), has finished with its own quick.save on disk: the owed rows whose people were made before that
   request are settled (DONE made, owed until the world server's GONE). TownGenOwedNotThisSave counts a finished save whose quick.save
   was an earlier save's; TownGenOwedSettling (ANY THREAD) = rows made here that no finished save holds yet; TownGenOwedCanSend =
   1 while the world-server link is admitted with its opening push in (an owed DONE can go now). */
void TownGenOwedSaveFinished(const std::string& saveName, long reqSeq);
void TownGenOwedNotThisSave();
int TownGenOwedSettling();
int TownGenOwedCanSend();
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
void TownGenNobodyYetCharacter(int sx, int sy, void* caller);   /* T-1261 (owner decision 1531): worldgen's character gate refused a member because nobody holds the area yet (a player-faction pass is not a refusal and is not counted) - counted on the [TG] line (nobodyYetDeferred[..,character]), the first 5 per world logged with `caller`, the engine's return address. ANY THREAD */
void TownGenResidentsDone(int ran);   /* T-392 fold: called when the original populateBuilding returns (the gate's building-sector context ends). T-438: and the
                                         set-aside flag; ran = 1 the engine call ran (a set-aside building's furniture line is logged), 0 it did not */
int  WorldGenRerunPopulate(void* factory, void* building);
void* TownOfBuilding(void* building);
/* The group's home building, carried on its world record. TownGenPlatoonHomeKey: the position key (items.cpp ObjectPositionKey, the
   residents table's own key form) of the building the group's Ownerships+0x38 names - 1 written to out, 0 the group has no home
   building, -1 unreadable or a key a record line cannot hold. TownGenGiveHomeTo: Ownerships::setHomeBuilding for the group (spawn.cpp
   PlatoonSetHomeBuildingPod) - 1 given and the building's residents hand names the group, 2 given and it does not, 0 the call faulted,
   4 (coopstore::kGiveHomePlayerHouse) refused: the building is a player's house (TownGenPlayerHouse), never given.
   TownGenGiveHomeByKey: the same after resolving the key (items.cpp ObjectByPositionKey) - 3 the building is not loaded here.
   MAIN THREAD. */
int  TownGenRepairMarks(const std::string& buildingKey, int* repaired, int* owedSettling);   /* T-984 (TEST lever's read), any thread: *repaired 1 = the empty-building repair set this building's residents aside in this world; *owedSettling 1 = an owed residents row for it was made here and no finished save holds it yet (OwedSettling). 0 = empty key */
int  TownGenPlatoonHomeKey(const void* platoon, char* out, int cap);
int  TownGenGiveHomeTo(void* platoon, void* building);
int  TownGenGiveHomeByKey(void* platoon, const char* key);
void* TownGenResidentGroupOf(void* building);   /* T-1334 TEST-ONLY read, MAIN THREAD: the group the building's residents hand (+0xD0) names - 0 none or unread */
/* A player's house (a building a player owns: the owner's player marker, another player's stand-in, or the build records naming a
   player - townreoffer::PlayerHouseSkips). TownGenPlayerHouse: townreoffer::kHouse* (0 = not one, or the owner unreadable); the
   build-record test is asked on the main thread only. TownGenPlayerHouseSkip: worldgen.cpp's populateBuilding detour (rerun 0) and
   the re-offer's call (rerun 1), before the residents gate - 1 = a player's house: counted, logged once per building, its waiting
   entry erased, and the engine call is not made. ANY THREAD.
   TownGenBuyHeld (owner decisions 809 b / 813 b, MAIN THREAD): a house being bought whose residents are on hold - its waiting entry is
   offered now through the check-up's own steps so the engine's purchase rehomes the groups made; when nothing is made the entry is
   dropped. -1 off the main thread, 0 no waiting entry, else townreoffer::kOne* (what the re-offer did). TownGenBuyHeldGroups: the groups
   that run made (up to cap; valid until the next TownGenBuyHeld). */
int  TownGenPlayerHouse(void* building);
int  TownGenPlayerHouseSkip(void* building, int rerun);
int  TownGenBuyHeld(void* building);
int  TownGenBuyHeldGroups(void** out, int cap);
int  TownGenTownSid(void* town, char* out, int cap);   /* decision 34: the town's string id (guarded), 1 ok 0 fault; any thread */       // forgets the refusal keys (town x cause) and their counts
void TownGenUnparkAfterPublish();   /* T-1099, MAIN THREAD (store.cpp PublishPendingTowns, at its end): un-parks every parked residents building whose note no longer holds it */
void TownTimeArrive(const std::string& id, const std::vector<char>& bytes);   /* T-4, MAIN THREAD (store.cpp ApplyRemoteRecord): a town clock record (src/common/towntime.h) from the world server */
void TownTimeLinkNew();   /* T-4 fold 3, MAIN THREAD (store.cpp at a WELCOME): an unanswered town clock write goes back to the accepted record */
void TownTimeEcho(const std::string& id, int hasStamp, long long stamp);   /* T-4 fold 2, MAIN THREAD (store.cpp): the world server stored this game's town clock write sent with that stamp */
void TownTimeRefuseArrive(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen);   /* T-4, MAIN THREAD (items.cpp BoxRefuseArrive): the world server refused this game's town clock write */
int  TownGenTownPos(void* town, float* pos);   /* the town's position (guarded; 0,0 reads as none), 1 read 0 none or fault; main thread */
/* TEST-ONLY `raidnow <faction name>` (T-1122, MAIN THREAD, command_channel.cpp): the faction's war manager timer set so its next
   update rolls a campaign; one [RAID] raidnow line, or a [RAID] raidnow error line. Answers the status text. */
std::string RaidNowLever(const std::string& rest);
}
