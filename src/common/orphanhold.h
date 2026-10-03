/* src/common/orphanhold.h - T-306 (owner decision 226, 2026-09-30): WHEN AN ORPHANED ZONE TICK SENDS ITS PLAYER
 * SECTOR AS UNKNOWN.
 *
 * Pure: no engine memory, no globals, no OS call, no include - the offline suite sweeps it.
 *
 * Owner decision 226: "a game keeps control of the areas it holds while its player is connected, dead or alive - a
 * single death must not reshuffle who runs the whole area". When the engine destroys the watched character (T674: the
 * corpse was eaten) the zone tick carries on ORPHANED around the last position it read and reports exactly what a live
 * tick would (T-306 fold 1, manager ruling: a dead or spectating player's game keeps streaming what its camera sees).
 * The one thing it must not keep saying is where its player stands - there is no player - so the player sector goes out
 * as unknown. Not on the FIRST orphaned tick, though: one tick whose position will not read while the character is
 * alive would otherwise erase this game's player row at the notebook (store_main.cpp OnAreas, "I do not know" clears
 * the row) and hand its ring-1 tie-break away for a second.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_ORPHANHOLD_H
#define COOP_COMMON_ORPHANHOLD_H

namespace cooporphan {

/* The consecutive orphaned ticks after which the player sector is sent as unknown. */
const int kOrphanUnknownAfterTicks = 2;

/* orphanRun: consecutive orphaned ticks in the current spell, THIS tick included (0 = the tick is live).
   Returns 1 = send the player sector as unknown, 0 = send it as a live tick would. */
inline int OrphanSendUnknownSector(int orphanRun)
{
    return (orphanRun >= kOrphanUnknownAfterTicks) ? 1 : 0;
}

}   /* namespace cooporphan */

#endif
