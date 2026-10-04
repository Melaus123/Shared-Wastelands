// relations.h - P3 piece 2 (decisions 23/24): faction standings are one world.
// Each player's standing with every world faction is OWNED by that player's own game (the engine there computes it from
// what that player does; it is what that player's save carries). World-vs-world standings are the NOTEBOOK's table (par24, decision 48 - see WorldRel* below; they were the host's). The owner
// forwards every change the engine makes (post-call detours on the six changers + trust) and re-sends its owned entries
// as a snapshot on link-up, on a faction rename, on a world rebuild and on RELSYNC - there is NO periodic re-send (the
// "every ~30 s" this comment said until 2026-09-24 was never in the code; investigations/faction-relations.md); the other
// side writes them straight into its own relation entries (no engine notification, no echo) and refuses a pair it owns
// itself (rel1). Factions travel by stringID; a player faction travels as "@player:<name>", the peer faction as "@peer".
#pragma once
#include <string>
#include <vector>   /* par24 */
class Faction;
namespace coopown { struct FactionRec; }   /* mmo3 */
namespace swteam { struct PlayerSide; struct TeamRec; struct Delta; }   /* T-546 step 5 */
namespace coop {
void InstallRelations();
void RelationsTick();          // MAIN THREAD, every frame (cheap): link-up snapshot + the periodic re-send
void ReportRelations();
void SetRelationsOn(bool on);
void RelationsSendSnapshot();
void RelationsNoteRelayedDropped();   // M5a fold 1, MAIN THREAD: a relayed standing was dropped with no running world - a RELSYNC is owed
void RelationsForgetQueue();    // world teardown   // send my owned entries now (an event, e.g. my faction was renamed)
void ApplyRemoteRelation(const std::string& ownerSid, const std::string& otherSid, float relation, float trust, float trustNeg,
                         unsigned int flags, unsigned int reason);
// crime3: the wire sid of a faction as relations sends it (MAIN THREAD), and the local faction an arriving sid names.
std::string RelationsWireSid(::Faction* f);
// The name tag's colour level between this game's player faction and another player's faction: nametag::kFriendly /
// kNeutral / kHostile, the worse of the two directions (src/common/nametag.h). MAIN THREAD.
int RelationsTagLevel(::Faction* mine, ::Faction* theirs, int* reads);   // *reads = directions actually read (0..2)
// T-545: both directions' levels (mine -> theirs, theirs -> mine), nametag levels, kUnknown when unread. MAIN THREAD.
void RelationsStanceLevels(::Faction* mine, ::Faction* theirs, int* you, int* them);
// ANY THREAD: a count bumped each time a standing with this game's player faction on either side may have moved (a hooked
// changer, a write from the wire, a put-back) - the PLAYERS tab rebuilds its rows when it moves.
long long RelationsPlayerPairEpoch();
::Faction* RelationsFactionFromWire(const std::string& sid);
// P079 (rel2, read-only probe): logs one faction's standing with this game's player faction and with the peer faction, both
// directions. MAIN THREAD. Returns the command status.
std::string RelationProbe(const std::string& sid);
// ally1 (T343): `relation <a> <b>` - both directions of one pair (@me, @peer or a stringID). MAIN THREAD.
std::string RelationPairProbe(const std::string& a, const std::string& b);
// TEST-ONLY `relation change crime|war|peace <faction stringID or name>`: the engine's own changer on an NPC faction and this
// game's player faction as the engine's roads call it (crime: the faction's relations towards this player; war / peace: this
// player's relations towards the faction); `relation of <faction>`: read-only, that faction's standing with every player here.
// MAIN THREAD.
std::string RelationChangeLever(const std::string& kind, const std::string& faction);
std::string RelationOfProbe(const std::string& faction);
// ally1 (T343): `ally on|off` - MY "mine -> stand-in" relation to 100 / back, through the engine's setRelation. MAIN THREAD.
std::string AllySet(bool on);
// relate1: `relate <target> ally|neutral|hostile` - MY "mine -> stand-in" relation to +100 / 0 / -100 through the engine's
// setRelation; refused for any target that is not another player's faction (met this session, or carried by the save). `ally on|off`
// stays as the alias. MAIN THREAD.
std::string RelateSet(const std::string& target, const std::string& level);
// T-546 step 4: this game's side towards player `slot` (mine -> that player's faction, met this session or carried by the save):
// 1 = read into *v, 0 = not readable here now (no world, no faction of that player, or the entry would not read). MAIN THREAD.
int RelationsMineTowards(int slot, float* v);
// T-546 step 4, THE PIN: this game's side towards teammate `slot` set to ally (+100) through RelateSet's road (setRelation,
// forwarded) when it reads below the pin. 1 written, 0 already at the pin, -1 not readable now, -2 the relate road refused (*why
// says which). *before = the value read.
// MAIN THREAD.
int RelationsPinAlly(int slot, float* before, std::string* why);
/* mmo3: MAIN THREAD. The player faction row (name, platoonIDs, standings both ways with every NPC faction) read for the
   pp.faction record, and laid back after a load (setRelation 0x6B4A30 + direct trust stores; platoonIDs never steps back). */
bool RelationsOwnRecord(coopown::FactionRec* out, std::string* why);
bool RelationsOwnRestore(const coopown::FactionRec& rec, std::string* why, std::string* detail);
/* T-546 step 5, SHARED STANDING (src/common/teamstanding.h; the record: teamwire.h TeamRec). MAIN THREAD.
   RelationsNpcRecordOf: player `slot`'s standing with every NPC faction, both directions - this game's own (slot = this game's,
   or -1) or that player's faction here. RelationsTeamSet: those rows written onto this game's own faction outright as the
   TEAM's write (a departure's NPC standing put back). RelationsPlayerSides: this game's side towards every other player's
   faction here (value and flags), by slot. RelationsMineTowardsFull: one of them.
   RelationsSetMineTowards: this game's side towards player `slot` set to `v` and `flags` (< 0: the ally flag cleared below the
   ally line) - asTeam = the team's write (team reason), else an ordinary row (that player's game shows its sentence); never
   this game's own change. 1 written, -1 no faction of that player here (this world's faction list read; nothing to write),
   -2 the setter raised, -3 not writable now (no world, an engine address missing, or the entry would not read).
   RelationsTeamApplyRecord: the team's record written onto this game's faction (every NPC direction it holds; the stance
   towards every player not in `teammates`), the base for this game's own changes set from it.
   RelationsTeamCollect: the worker threads' changes drained, then this game's own changes since the base - NPC pairs as
   differences, the players whose side this game's own engine or player moved; RelationsTeamUncollect: a gather not sent put back.
   RelationsTeamForgetBase: the world is going, or this game left its team - no base until the record is written again.
   RelationsTeamDropSideMarks: the marks of this game's own side towards these players not gathered yet dropped (players who
   have just left this game's team: the pin's own writes are never their team's stance); the number dropped.
   RelationsSideOwnNo: the number of this game's latest own change of its side towards player `slot` (0 = none since start);
   RelationsSideNoNow: the latest number given (each own side change takes the next). */
bool RelationsNpcRecordOf(int slot, coopown::FactionRec* out, std::string* why);
bool RelationsTeamSet(const coopown::FactionRec& rec, std::string* why, std::string* detail);
void RelationsPlayerSides(std::vector<swteam::PlayerSide>* out);
int RelationsMineTowardsFull(int slot, float* v, unsigned* flags);
int RelationsSetMineTowards(int slot, float v, int flags, bool asTeam, float* before, std::string* why);
bool RelationsTeamApplyRecord(const swteam::TeamRec& rec, const std::vector<int>& teammates, std::string* detail);
void RelationsTeamCollect(std::vector<swteam::Delta>* npc, std::vector<int>* sideSlots);
void RelationsTeamUncollect(const std::vector<swteam::Delta>& npc);
void RelationsTeamForgetBase();
int RelationsTeamDropSideMarks(const std::vector<unsigned>& slots);
unsigned long long RelationsSideOwnNo(int slot);
unsigned long long RelationsSideNoNow();
void RelationsTeamBaseFromRec(const swteam::TeamRec& rec);   /* the founder's SEED as the base until its first record is written */
bool RelationsTeamBaseValid();
std::string RelationsTeamText();   /* the shared-standing counters for `team show` */
long long RelationsTeamEventSkipped();   /* T-546 (owner 512), ANY THREAD: standing changes by event between two team factions not passed to the engine */
/* par24 (parity P24; src/common/worldrelwire.h): WORLD-VS-WORLD standings (no player faction and no stand-in on either side)
   are the NOTEBOOK's table, not the host's: a WELCOME empties this game's copy (WorldRelTableReset), WORLD_REL rows fill it
   (WorldRelNoteRows, store.cpp's drain), the K2 safe point writes them into the engine and, once per world, offers the pairs
   this save holds that the table lacks (WorldRelSafePointDrain); this game's own engine changes go up on the tick. MAIN THREAD. */
void WorldRelTableReset();
void WorldRelNoteRows(const std::vector<char>& payload);
void WorldRelSafePointDrain();
std::string WorldRelCommand(const std::string& args);   /* TEST-ONLY verb `worldrel [set <a> <b> <v> | test | get <a> <b>]` */
}
