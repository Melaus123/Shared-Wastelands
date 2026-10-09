/* src/common/removalreason.h - M7a3 (manager decision 2026-10-01 on T772): AN ENGINE UNLOAD IS NEVER SENT AS A DEATH.
 * The pure decision only: no engine memory, no Windows, no globals. The plugin (spawn.cpp NotifyDespawn, worldgen.cpp's destroy
 * detour) and the offline suite (coop-test) compile the same functions. C++03 (VS2010 v100).
 *
 * WHY. GameWorld::destroy 0x798F50 takes (object, justUnloaded, reason literal). The engine passes justUnloaded = FALSE on paths
 * that only put a character away - the squad walk 0x79BA60 ('wandered out of zone' / 'leader wandered out of zone',
 * build/decomp_79ba60.txt:146,157), ~ActivePlatoon 0x4FE4B0 ('platoon destructor', decomp_4fe4b0.txt:13-14), the corpse update
 * 0x5CC300 ('corpse unloaded', decomp_5cc300.txt:~99-101) - so T772's host sent 89 of them as deaths and the other game, which
 * still had the area loaded, deleted live characters. The REASON is read, not the flag alone:
 *   UNLOAD    - the reason's own test is "the zone is not loaded" (the character stays in the engine's world data);
 *   DEATH     - the character ends: eaten (decomp_64f8e0.txt:192), the corpse decayed (decomp_5cc300.txt:81), a squad member
 *               created and discarded before it ever stood in the world (decomp_582f80.txt:1430, decomp_79b620.txt:126);
 *               the plugin's own removal of a second living body of a named character (kDuplicateNamedRemoved, worldstate.cpp)
 *               is a DEATH too: that body ends for good and the other games remove their copies of it.
 *   AMBIGUOUS - every other reason, and an unknown one: a DEATH only when the engine's own Character::hasDied says dead.
 */
#ifndef COOP_COMMON_REMOVALREASON_H
#define COOP_COMMON_REMOVALREASON_H

namespace coopremoval {

enum { kReasonDeath = 0, kReasonUnload = 1, kReasonAmbiguous = 2 };

/* the reason the plugin passes to GameWorld::destroy when it removes a second living body of a named character this game owns */
static const char* const kDuplicateNamedRemoved = "duplicate named character removed";

inline int ReasonIs(const char* r, const char* lit)
{
    if (r == 0 || lit == 0) return 0;
    for (int i = 0; ; ++i) { if (r[i] != lit[i]) return 0; if (lit[i] == 0) return 1; }
}

/* The engine's reason literal -> its class. A null reason is AMBIGUOUS. */
inline int EngineRemovalReasonClass(const char* r)
{
    if (r == 0) return kReasonAmbiguous;
    if (ReasonIs(r, "corpse unloaded") || ReasonIs(r, "ZoneMapContent::deactivate") || ReasonIs(r, "delayedSpawningChecks")) return kReasonUnload;
    if (ReasonIs(r, "eaten") || ReasonIs(r, "corpse decayed") || ReasonIs(r, "createRandomSquad") || ReasonIs(r, "_putTheSpecialCharactersInNewSquads") || ReasonIs(r, kDuplicateNamedRemoved)) return kReasonDeath;
    return kReasonAmbiguous;
}

/* 1 = the peer is told UNLOAD (not a death), 0 = DESPAWN. justUnloaded (the engine's own flag) always means UNLOAD; then the
   reason's class; an AMBIGUOUS reason is a DEATH only when deadState is 1 (Character::hasDied read under SEH: 1 dead, 0 alive,
   -1 not read or faulted - not a confirmation). */
inline int RemovalIsUnload(int justUnloaded, int reasonClass, int deadState)
{
    if (justUnloaded != 0) return 1;
    if (reasonClass == kReasonUnload) return 1;
    if (reasonClass == kReasonDeath) return 0;
    return deadState == 1 ? 0 : 1;
}

}   /* namespace coopremoval */

#endif
