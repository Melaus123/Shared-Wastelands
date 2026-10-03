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
namespace coop {
void InstallPeace();               // startPlugin: prologue-checked MinHook install of the two virtuals
void SetPeaceOn(bool on);          // the peace on|off verb. MAIN THREAD: the ON edge does the evidence walk
void ReportPeace();                // folded into ReportEverything() (F125/F177), not behind a verb of its own
}
