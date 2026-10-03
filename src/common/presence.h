/* presence.h - M11 C1 (T-197, to-do M11; .modding/investigations/t197-parallel-plan-2026-10-02.md section C1, site table
   m11-s2-sites-2026-09-30.md, the seven 'D' sites): "AM I THE ONLY GAME IN THIS WORLD?" ASKED OF THE WORLD SERVER'S PLAYER
   LIST AS WELL AS THE OLD GAME-TO-GAME LINK. The pure half, included by the plugin and the offline suite.
   WHY. Seven places decide "I am the only game, so I write everything" from the old session link being down: the box writer's
   no-session fail-open (items.cpp ItAreaVerdictAt, F598's own hole), the owner rung and the unread-box rung (items.cpp), the
   farm gate (farm.cpp FarmTick and the readers of g_fmLinked), town invention (worldgen.cpp WorldGenTick and LeafGatedAt's
   reader), and the area tie-break (zones.cpp MySlotLowerEx), which named "the host" - with three games or a headless server
   nobody is the host of the others, so nobody wrote. A game on the world road (joinvia=world today, every joiner after the
   M11 flip) never has the old link, so every one of those answered "alone": two writers everywhere.
   THE RULE. Another player is present when the old link is up (unchanged) OR this link's PLAYERS roster shows ANOTHER slot
   IN_WORLD. No roster (the world server down, not welcomed, or a roster from an older link) and no old link = alone, exactly
   as before: the design's no-world fail-open. The area tie-break with no fresh area table: while the old link is up, the
   session role as before; else the LOWEST IN_WORLD slot on the roster writes - every game reads the same roster, so exactly
   one game answers "mine" however many there are and whether or not any of them is a host. */
#ifndef KM_PRESENCE_H
#define KM_PRESENCE_H

#include <vector>
#include "joinstage.h"   /* coopjoin::RosterRow, kStageInWorld */

namespace mppresence {

/* The sites, for the plugin's counters (presence[...] on the store REPORT line). */
enum Site { kSiteBoxArea = 0, kSiteOwnerRung = 1, kSiteUnreadRung = 2, kSiteFarm = 3, kSiteWorldGen = 4, kSiteCount = 5 };

/* oldLinkUp: the old game-to-game link is up. rosterOtherInWorld: 1 = this link's PLAYERS roster shows another slot IN_WORLD,
   0 = it shows none, -1 = no roster on this link. 1 = another player is present, 0 = this game is alone. */
inline int PresenceDecide(int oldLinkUp, int rosterOtherInWorld)
{
    if (oldLinkUp != 0) return 1;                  /* unchanged: the old link up is another player */
    return (rosterOtherInWorld == 1) ? 1 : 0;      /* no roster and no old link = alone (the no-world fail-open) */
}
/* M11 C5 (from the C1 review) [m11c5] - A GAME THAT JOINED THROUGH THE WORLD ROAD IS NOT ALONE WHEN THE WORLD SERVER IS OUT OF SIGHT.
   joinedWorldRoad: this running world was entered through the world road (joinstage.h JoinedByWorldRoad). With no roster on this
   link (the world-server link down, not yet welcomed) such a game cannot see the others but they are still there: it answers
   "another player present" and does not write as the only game - otherwise every world-road game would take the alone path during
   an outage (two writers after the flip). A roster on this link, or the old link, decides as PresenceDecide. */
inline int PresenceDecideRoad(int oldLinkUp, int rosterOtherInWorld, int joinedWorldRoad)
{
    if (oldLinkUp == 0 && rosterOtherInWorld == -1 && joinedWorldRoad != 0) return 1;
    return PresenceDecide(oldLinkUp, rosterOtherInWorld);
}

/* The roster's two answers. mySlot < 0: this game cannot tell its own row from another's - both answers -1 (no roster).
   *other: 1 another slot IN_WORLD, 0 none. *lowest: the lowest IN_WORLD slot, this game's own included; -1 none. */
inline void RosterPresence(const std::vector<coopjoin::RosterRow>& rows, int mySlot, int* other, int* lowest)
{
    *other = -1; *lowest = -1;
    if (mySlot < 0) return;
    *other = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].stage != (unsigned)coopjoin::kStageInWorld) continue;
        const int s = (int)rows[i].slot;
        if (s != mySlot) *other = 1;
        if (*lowest < 0 || s < *lowest) *lowest = s;
    }
}

/* The area tie-break when no fresh area table names the other holders (MySlotLowerEx's no-table arms): 1 = this game writes,
   0 = it does not, -1 = the roster does not decide (the old link is up, this game has no slot, or no IN_WORLD row) and the
   caller keeps the session role. */
inline int TieBreakByRoster(int oldLinkUp, int mySlot, int lowestInWorld)
{
    if (oldLinkUp != 0 || mySlot < 0 || lowestInWorld < 0) return -1;
    return (mySlot == lowestInWorld) ? 1 : 0;
}
inline int TieBreakNoTable(int oldLinkUp, int isHost, int mySlot, int lowestInWorld)
{
    const int r = TieBreakByRoster(oldLinkUp, mySlot, lowestInWorld);
    return (r >= 0) ? r : (isHost != 0 ? 1 : 0);
}

}  // namespace mppresence

#endif
