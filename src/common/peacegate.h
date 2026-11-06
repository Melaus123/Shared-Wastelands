// peacegate.h - owner decision 1205 a (2026-10-31): the peace lever's gate (peace.cpp PeaceGate), in its plain and fight forms.
// Plain peace covers any pair with a player-side character (a player faction or a stand-in): the engine's is-enemy answer is
// forced "no" and the two fight pickers' is-ally answer "yes". The FIGHT form (TEST-ONLY, `peace fight <faction>[,<faction>]`)
// covers the same pairs EXCEPT two player-side characters (two players' characters fight as the engine decides) and a pair whose
// non-player side's faction is in the small exempt list the test set (the run's own opponent, spawned with `spawnfaction`).
// The detours run on the AI worker thread: this decision is pointer compares only - the exempt cells are never followed.
// PURE: no engine, no plugin types.
#pragma once
namespace swpeacegate {
const int kExemptCells = 4;   // the exempt list: at most this many factions
// why a fight-form answer was "not covered": none, both sides player-side, or the non-player side's faction exempt
const int kWhyNone = 0, kWhyPlayers = 1, kWhyExempt = 2;
// A faction is exempt when one of the first n cells holds it. No faction is never exempt.
inline bool Exempt(const void* f, void* const volatile* cells, int n)
{
    if (f == 0 || cells == 0) return false;
    for (int k = 0; k < n; ++k) if (cells[k] == f) return true;
    return false;
}
// a, b: each side's read - 1 player-side, 0 not, -1 a read gave doubt. fa, fb: each side's faction (0 = none or not read).
// fight 0 (plain): 1 when either side is player-side, -1 when neither is and a read gave doubt, else 0 - exactly as before.
// fight 1: -1 when a read gave doubt (the engine answers); 0 when both are player-side (why kWhyPlayers) or one is and the other's
// faction is exempt (why kWhyExempt); 1 when one is and the other's faction is not exempt; 0 when neither is.
// Returns 1 covered, 0 not covered, -1 doubt (fail open to the engine's own answer).
inline int Decide(int a, int b, const void* fa, const void* fb, int fight, void* const volatile* exempt, int n, int* why)
{
    if (why != 0) *why = kWhyNone;
    if (fight == 0)
    {
        if (a == 1 || b == 1) return 1;
        if (a < 0 || b < 0) return -1;
        return 0;
    }
    if (a < 0 || b < 0) return -1;
    if (a == 1 && b == 1) { if (why != 0) *why = kWhyPlayers; return 0; }
    if (a == 1 || b == 1)
    {
        const void* other = (a == 1) ? fb : fa;
        if (Exempt(other, exempt, n)) { if (why != 0) *why = kWhyExempt; return 0; }
        return 1;
    }
    return 0;
}
// T-1194: the THIRD hook's answer - the planner's attack test, AI::treatsAsEnemy(RootObjectBase*) (1.0.65 0x5993A0, "relation
// below -1"), which an attack must pass before it is carried out and which does not go through the is-enemy question the first
// hook answers. engine: the engine's own answer (1 enemy, 0 not). gate: Decide's answer for the AI's own character and the target
// (1 covered, 0 not covered, -1 doubt). The engine's answer stands unless the lever is on, the hooks are not bypassed on this
// thread, the engine said "enemy" and the pair is covered; then "not an enemy". Returns 1 enemy, 0 not.
inline int PlanEnemyAnswer(int peaceOn, int bypassed, int engine, int gate)
{
    if (engine == 0) return 0;
    if (peaceOn == 0 || bypassed != 0) return 1;
    return (gate == 1) ? 0 : 1;
}
// T-1194 (T2029): A TALK LINE'S ATTACK RESULTS. A line's actions run in Dialogue::_doActions (1.0.65 0x67FAD0 / 1.0.68 0x680560,
// row DialogueDoActions) on the game that runs the speaker. Three action types give the speaker a direct attack order on the
// conversation's target, which reaches combat without the planner test (T2029 Confirmed: plan asked stayed 0 while the tower's
// Dust Bandits attacked after "Intruders!"): 6 (attack - re-think 0x5C78A0, remember the target as an enemy 0x673A10, AddOrder
// 0x5D1640 task 0x65), 55 (the same with task 0xF8) and 57 (attack unless the two factions are allied - it falls into 6).
// Read build/decomp_67fad0.txt:1583-1627.
const int kTalkActAttack = 6, kTalkActAttackB = 55, kTalkActAttackUnlessAlly = 57;
// bit 0 = the line carries 6, bit 1 = 55, bit 2 = 57; 0 = none of them
inline unsigned TalkAttackMask(const int* types, int n)
{
    unsigned m = 0;
    for (int i = 0; types != 0 && i < n; ++i)
    {
        if (types[i] == kTalkActAttack) m |= 1u;
        else if (types[i] == kTalkActAttackB) m |= 2u;
        else if (types[i] == kTalkActAttackUnlessAlly) m |= 4u;
    }
    return m;
}
// peaceOn: the lever's flag. mask: TalkAttackMask of the line. gate: Decide's answer for the speaker and the conversation's target
// (1 covered, 0 not covered, -1 doubt). Returns 1 = drop the line's results (the line does not run), 0 = the engine runs it.
// Only a covered pair is dropped: lever off, no attack action, a pair peace does not cover, or doubt - the line runs unchanged.
inline int TalkAttackDrop(int peaceOn, unsigned mask, int gate)
{
    if (peaceOn == 0 || mask == 0) return 0;
    return (gate == 1) ? 1 : 0;
}
// Writes list[0..n) into cells[0..cap) (zero entries skipped, repeats kept once) and clears the rest. Returns how many cells
// hold a faction. MAIN THREAD writer; a reader on another thread sees each cell either old or new (one aligned pointer store).
inline int CellsWrite(void* volatile* cells, int cap, void* const* list, int n)
{
    if (cells == 0 || cap <= 0) return 0;
    int k = 0;
    for (int i = 0; list != 0 && i < n && k < cap; ++i)
    {
        void* f = list[i];
        if (f == 0) continue;
        bool seen = false;
        for (int j = 0; j < k; ++j) if (cells[j] == f) seen = true;
        if (seen) continue;
        cells[k++] = f;
    }
    for (int r = k; r < cap; ++r) cells[r] = 0;
    return k;
}
}
