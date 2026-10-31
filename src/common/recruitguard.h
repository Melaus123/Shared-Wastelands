/* src/common/recruitguard.h - a character that belongs to a player is never recruited by another player.
 *
 * A recruit stays with the player who recruited it. Every road to PlayerInterface::recruit 0x693010 (a hire reply held by the
 * 0x67FAD0 detour, the R0 owner/hirer handshake, the other game's NPC joining through its own talk, the engine's own call) asks
 * these questions first:
 *   - the game that runs the person (the OWNER) answers NO {kNoFaction} when the person is in ANY player's faction - its own
 *     player's, or a stand-in coop-p<slot> for another player (a copy it should not run, refused all the same);
 *   - the asking game (the HIRER, or any game about to call recruit) refuses when the person is in a player's faction that is not
 *     its own player's - another player's stand-in, or a recorded stand-in a save carries.
 * An NPC in an NPC faction is untouched: the handshake runs exactly as before. An unreadable faction is not refused here (the
 * owner's live check already answers kNoFaction for it, and the engine level keeps its old behaviour).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include "hirewire.h"

namespace cooprecruit {

const int kRecruitAllow       = 0;
const int kRecruitRefuseOwned = 1;   /* another player's character */

/* inAnyPlayers: 1 the person's faction is a player's (this game's own, a stand-in, or a recorded stand-in), 0 an NPC faction,
   -1 unreadable. inMine: 1 that faction is this game's own player faction. */
inline int LocalRecruitDecide(int inAnyPlayers, int inMine)
{
    if (inAnyPlayers == 1 && inMine != 1) return kRecruitRefuseOwned;
    return kRecruitAllow;
}

/* The owner's answer for a person in a player's faction: kNoFaction (the reason hirewire.h already keeps for "already in a player /
   peer faction"), else kNoNone. The owner's own player counts too - that is the stolen recruit. */
inline int OwnerHireReason(int inAnyPlayers)
{
    return inAnyPlayers == 1 ? coophire::kNoFaction : coophire::kNoNone;
}

/* The line the refused player is shown. The player's name when it is known, else the player's faction name, else a plain word. */
inline std::string OwnedLine(const std::string& playerName, const std::string& factionName)
{
    const std::string who = !playerName.empty() ? playerName : (!factionName.empty() ? factionName : std::string("another player"));
    return "That character already belongs to " + who + ".";
}

} // namespace cooprecruit
