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
::Faction* RelationsFactionFromWire(const std::string& sid);
// P079 (rel2, read-only probe): logs one faction's standing with this game's player faction and with the peer faction, both
// directions. MAIN THREAD. Returns the command status.
std::string RelationProbe(const std::string& sid);
// ally1 (T343): `relation <a> <b>` - both directions of one pair (@me, @peer or a stringID). MAIN THREAD.
std::string RelationPairProbe(const std::string& a, const std::string& b);
// ally1 (T343): `ally on|off` - MY "mine -> stand-in" relation to 100 / back, through the engine's setRelation. MAIN THREAD.
std::string AllySet(bool on);
// relate1: `relate <target> ally|neutral|hostile` - MY "mine -> stand-in" relation to +100 / 0 / -100 through the engine's
// setRelation; refused for any target that is not another player's stand-in. `ally on|off` stays as the alias. MAIN THREAD.
std::string RelateSet(const std::string& target, const std::string& level);
/* mmo3: MAIN THREAD. The player faction row (name, platoonIDs, standings both ways with every NPC faction) read for the
   pp.faction record, and laid back after a load (setRelation 0x6B4A30 + direct trust stores; platoonIDs never steps back). */
bool RelationsOwnRecord(coopown::FactionRec* out, std::string* why);
bool RelationsOwnRestore(const coopown::FactionRec& rec, std::string* why, std::string* detail);
/* par24 (parity P24; src/common/worldrelwire.h): WORLD-VS-WORLD standings (no player faction and no stand-in on either side)
   are the NOTEBOOK's table, not the host's: a WELCOME empties this game's copy (WorldRelTableReset), WORLD_REL rows fill it
   (WorldRelNoteRows, store.cpp's drain), the K2 safe point writes them into the engine and, once per world, offers the pairs
   this save holds that the table lacks (WorldRelSafePointDrain); this game's own engine changes go up on the tick. MAIN THREAD. */
void WorldRelTableReset();
void WorldRelNoteRows(const std::vector<char>& payload);
void WorldRelSafePointDrain();
std::string WorldRelCommand(const std::string& args);   /* TEST-ONLY verb `worldrel [set <a> <b> <v> | test | get <a> <b>]` */
}
