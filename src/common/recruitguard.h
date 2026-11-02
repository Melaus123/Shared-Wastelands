/* src/common/recruitguard.h - a character that belongs to a player is never recruited by another player.
 *
 * A recruit stays with the player who recruited it. Every road to PlayerInterface::recruit 0x693010 (a hire reply held by the
 * 0x67FAD0 detour, the owner / hirer handshake, the other game's NPC joining through its own talk, the engine's own call) asks
 * these questions first:
 *   - the game that runs the person (the OWNER) answers NO {kNoFaction} when the person is in ANY player's faction - its own
 *     player's, or the stand-in faction of another player (a copy it should not run, refused all the same);
 *   - the asking game (the HIRER, or any game about to call recruit) refuses when the person is in a player's faction that is not
 *     its own player's - another player's stand-in, or a stand-in record a save carries.
 * An NPC in an NPC faction is untouched: the handshake runs exactly as before. inAnyPlayers -1 (unreadable) is not refused here;
 * the plugin (hire.cpp OwnedVerdict) passes only 0 or 1, and its AnyPlayersFaction already counts a faction whose kind or record
 * id cannot be read as a player's.
 *
 * Pure: no engine memory, no Windows; the offline suite tests the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include "hirewire.h"

namespace recruitguard {

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
   peer faction"), else kNoNone. The owner's own player counts too - that is the recruit another player would take. */
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

/* The line the refused player is shown when the owner said NO {kNoFaction} and this game's own copy does not show the person in
   another player's faction: the owner sends the same reason when it cannot read the person's faction, so no player is named. */
inline std::string NotHireableLine()
{
    return "That character can't be hired right now.";
}

/* The engine's own recruit (PlayerInterface::recruit 0x693010 returned "recruited") of a person this game runs - a dialogue hire the
   0x67FAD0 detour lets run unchanged, a freed prisoner, any engine road: the other games learned the person's faction when it was
   announced and still show the old one. The answer: send the hire handshake's DONE {taken 0, joined 1} to every game (the road
   this game's own hire already uses), or the reason it is not sent. Checked in this order:
     ownRoadDepth > 0  - one of hire.cpp's own roads (RecruitPod), which tells the other games itself: not counted as the engine's;
     recruited 0       - the engine refused (squad full / limit): nothing changed;
     uid 0             - not a character the other games know (the caller then does not read the person's faction);
     nowInMine != 1    - the person is not in this game's player faction after the call;
     wasInMine == 1    - it was already in this game's player faction before: no faction changed;
     uidMine != 1      - another game runs the person: its copies are not this game's to move. */
const int kEngineRecruitSend       = 0;
const int kEngineRecruitOwnRoad    = 1;
const int kEngineRecruitRefused    = 2;
const int kEngineRecruitNoUid      = 3;
const int kEngineRecruitNotJoined  = 4;
const int kEngineRecruitWasPlayers = 5;
const int kEngineRecruitNotMine    = 6;
const int kEngineRecruitMax        = 6;

inline int EngineRecruitDecide(int recruited, int ownRoadDepth, unsigned int uid, int uidMine, int wasInMine, int nowInMine)
{
    if (ownRoadDepth > 0) return kEngineRecruitOwnRoad;
    if (recruited == 0) return kEngineRecruitRefused;
    if (uid == 0) return kEngineRecruitNoUid;
    if (nowInMine != 1) return kEngineRecruitNotJoined;
    if (wasInMine == 1) return kEngineRecruitWasPlayers;
    if (uidMine != 1) return kEngineRecruitNotMine;
    return kEngineRecruitSend;
}

/* The log's words for a verdict that is not kEngineRecruitSend. */
inline const char* EngineRecruitWhy(int verdict)
{
    switch (verdict)
    {
    case kEngineRecruitOwnRoad:    return "this file's own hire road, which tells the other games itself";
    case kEngineRecruitRefused:    return "the engine did not recruit the person";
    case kEngineRecruitNoUid:      return "the person has no uid (not a character the other games know)";
    case kEngineRecruitNotJoined:  return "the person is not in this game's player faction after the recruit";
    case kEngineRecruitWasPlayers: return "the person was in this game's player faction already (no faction changed)";
    case kEngineRecruitNotMine:    return "another game runs the person (its copies here are not this game's to move)";
    default:                       return "unknown verdict";
    }
}

} // namespace recruitguard
