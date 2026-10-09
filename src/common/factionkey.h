/* src/common/factionkey.h - HOW A FACTION IS NAMED BETWEEN GAMES.
 *
 * A faction's displayed name depends on the game's language: a game running in French does not call its factions by the
 * names an English game shows ("Shinobi Thieves", "Drifters"), so a displayed name sent by one game finds no faction on
 * the other. Every faction the game data defines also carries a stringID ("<number>-<mod file>", GameData+0x58), the same
 * in every language.
 *
 * An NPC faction is therefore named by its stringID: in a SPAWN's faction field and in a world-store RECORD's faction
 * field. A player's faction - this game's own, or another player's stand-in - is named by the name its player chose, which
 * no language changes: a SPAWN sends this game's own as "@slot:<n>:<name>" (slotwire.h) and a stand-in by its name; a
 * RECORD sends both by name. An NPC faction whose stringID cannot be read is named by its displayed name.
 *
 * A receiver tries a key as a stringID first and takes the answer only when it is an NPC faction; otherwise it looks the
 * key up as a faction's current name (a player's chosen name, or a world-store row that holds a displayed name), and then
 * as the name of a FACTION game-data record (Kenshi's record index, which may hold a name this game does not display),
 * taking that record's faction only when it is an NPC faction.
 *
 * A world-store row that arrives naming an NPC faction by a name is re-keyed in this game's copy to that faction's
 * stringID, so this game's own later sends of the row carry the id; the world server's row takes the id with the next
 * full write of that group from any game (a group put to sleep, the heartbeat, a new record - each writes the live
 * faction's key), or with a position update: the game that moves sleeping groups sends a group whose row names its NPC
 * faction by a name once with the faction's stringID, marked kRecFacCode in the RECORD's flags byte (the sender knows the
 * field is an NPC faction's code; a code's shape does not tell - "defaultEmpireFactionSID" is one), and a marked position
 * update replaces a stored row's different faction field that is not known to be a code (PositionTakesKey) - on the world
 * server (which knows a row whose last re-keying update was marked) and in each game's copy.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once
#include <string>

namespace factionkey {

const unsigned int kMaxSid = 95u;   /* a stringID longer than this is not sent as one (ownerwire.h kOwnMaxSid: the same bound) */

/* a stringID this layer sends and looks up: 1..kMaxSid bytes, no control byte, and not starting with '@' (the slot names) */
inline bool SidUsable(const std::string& s)
{
    if (s.empty() || s.size() > kMaxSid || s[0] == '@') return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

/* the key a faction travels by: an NPC faction with a usable stringID by that id; a player's faction, or an NPC faction
   whose stringID is unusable (empty when it could not be read), by its name */
inline std::string KeyOf(bool npc, const std::string& sid, const std::string& name)
{
    return (npc && SidUsable(sid)) ? sid : name;
}

/* receiver: is the key tried as a stringID at all */
inline bool TryAsSid(const std::string& key) { return SidUsable(key); }

/* receiver: does the stringID lookup's answer stand? Only a found NPC faction: a key that is a player's chosen name never
   lands on a player's faction by its record id; the name lookup decides then */
inline bool TakeIdMatch(bool found, bool foundIsPlayers) { return found && !foundIsPlayers; }

/* a RECORD's flags byte (after the sequence number; absent = 0): bit 0 - the faction field is an NPC faction's game-data
   code, as the sender's FactionKey made it */
const unsigned int kRecFacCode = 1u;

/* a position update for a stored row: its faction key replaces the stored one only when the sender marked it an NPC
   faction's code (incomingIsCode) and it is usable as one, and the stored field is a different, non-empty key that is not a
   slot name ('@...') and not known to be a code (storedIsCode: the row was re-keyed by a marked update). An unmarked key
   never replaces anything, so a code never becomes a name, a known code is never swapped for another, and a player's
   faction (never sent marked) is never re-keyed. */
inline bool PositionTakesKey(const std::string& stored, bool storedIsCode, const std::string& incoming, bool incomingIsCode)
{
    return incomingIsCode && SidUsable(incoming) && !storedIsCode && !stored.empty() && stored[0] != '@' && stored != incoming;
}

/* re-key a world-store row: the key found an NPC faction (by any lookup) whose stringID was read (foundIsCode: FactionKey
   gave its code, not its name) and differs from the key */
inline bool Rekey(bool foundIsCode, const std::string& key, const std::string& sid)
{
    return foundIsCode && SidUsable(sid) && sid != key;
}

}   /* namespace factionkey */
