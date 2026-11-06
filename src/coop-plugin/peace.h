// peace.h - E42 / the dev-only peace lever (user approval 2026-09-05, .modding/02-project-rules.md).
//
// "peace on" makes every faction treat the players as friends FOR THIS PROCESS, by answering the two
// questions the engine itself asks - Character::isEnemyOf (0x79BDB0, vtable +0x3E8) and
// Character::isAllyOf (0x791830, vtable +0x3F0) - instead of writing any relation value. Nothing goes
// on the wire, nothing reaches the save, and relations.cpp is untouched: a lever that WROTE relations
// would be forwarded for the pairs this game owns and reverted within a tick for the pairs it does not
// (relations.cpp RevertPair), i.e. it would fight the mod. Both games run the lever locally; the flag is
// not replicated and needs no message.
//
// ONE THING THIS LEVER DOES WRITE, and it is not the answers (P7y, folding F617 / review-p7x H-1): the ON
// edge runs an evidence walk that reads every faction pair through the engine's own
// FactionRelations::getRelationData 0x6B4910, and that call INSERTS a default-valued entry on a miss
// rather than merely reading one. So turning the lever on can create up to 2*(N-1) relation rows, each
// holding the value the engine would have used anyway (the faction's own defaultRelation +0x60). Nothing is
// forwarded - getRelationData is not one of relations.cpp's hooked setters - and nothing goes on the
// wire or into the save. The walk's own printed line states this and prints relationPairsRead.
//
// THE ALLY ANSWER IS FORCED AT TWO CALL SITES ONLY (P7y, folding F619): the return addresses inside
// CombatClass::getNearestEnemyInAttackZone and AI::findMeleeOpponent, the two consumers that pick a fight
// target by NOT-ALLY. About fifty other consumers - medics, body carrying, looting, trespass, turrets,
// stealth, squad bookkeeping, map marker colours - get the engine's own answer, because forcing theirs
// walks characters away from where a run parked them. See kAllyRets in peace.cpp.
//
// Default OFF. The hooks are installed unconditionally at startPlugin and the verb only flips a flag:
// patching a function body the AI worker thread is executing right now is a live hazard, a flag test is not.
// The detours run on the AI WORKER thread - interlocked counters only, no allocation, no strings, no logging.
#pragma once
#include <string>
namespace coop {
void InstallPeace();               // startPlugin: prologue-checked MinHook install of the two virtuals
void SetPeaceOn(bool on);          // the peace on|off verb (plain peace; ends the fight form). MAIN THREAD: the ON edge does the evidence walk
// Owner 1205 a, TEST-ONLY: `peace fight <faction>[,<faction>]` - the lever on in its FIGHT form: covered as plain peace except two
// player-side characters and a pair whose non-player side's faction is one of these (at most 4, looked up by name as `spawnfaction`
// does; a name that does not resolve, or a player-side faction, refuses the whole command). No names = only player pairs are
// left to the engine. Returns the status line: `ok peace fight exempt=<n>` or `error peace fight: ...`. MAIN THREAD.
std::string PeaceFightLever(const std::string& list);
void ReportPeace();                // folded into ReportEverything() (F125/F177), not behind a verb of its own
// T-546 (owner 512): teammates' characters are one side, "as if one person owned both sets". For a pair of DIFFERENT factions both
// in this player's team - this game's player faction and its teammates' factions - both detours answer before the engine is asked,
// as the engine answers two characters of one faction: isAllyOf 1, isEnemyOf 0 (src/common/teamally.h). Every caller gets it.
// MAIN THREAD: the team set written - own = this game's player faction, mates = the teammates' factions (none = no team).
void PeaceTeamFactions(void* own, void* const* mates, int n);
// ANY THREAD: two different factions both in the team set (pointer compares only; one load when in no team).
bool PeaceTeamPairAnyThread(const void* a, const void* b);
// ANY THREAD: two characters of a team pair - 1 yes, 0 no, -1 a faction would not read (their own vtable +0x58, guarded).
int PeaceTeamPairOfCharacters(void* a, void* b);
// The " team[held,ally,notEnemy,doubt,writes]=" tokens.
std::string PeaceTeamTokens();
// T-1194 (T2029): A TALK LINE'S ATTACK RESULTS (hire.cpp detour_doActions, both thread branches, before the line runs).
// ANY THREAD: the lever's flag - one aligned 32-bit load, nothing else. Off = the talk check costs only this.
bool PeaceOnAnyThread();
// ANY THREAD: the lever's own gate (PeaceGate, the rule the other three hooks use) for a speaker and the conversation's target:
// 1 covered, 0 not covered, -1 doubt. Interlocked counters only, no allocation, no log.
int PeaceGateAnyThread(void* speaker, void* target);
// ANY THREAD: one line carrying an attack action (6 / 55 / 57) seen while the lever was on. dropped 1 = its results were dropped;
// gate as above; unread 1 = the line's actions would not read. The first kept notes are logged by PeaceTalkDrain. No allocation.
void PeaceTalkNote(int dropped, int gate, int unread, unsigned speakerUid, unsigned targetUid, const int* types, const int* values,
                   int n, int total, int offMain);
// MAIN THREAD (hire.cpp HireSafePointDrain, every frame): one '[PEACE] talk attack dropped ...' line per kept note, capped.
void PeaceTalkDrain();
}
