/* src/common/nametag.h - THE NAME TAG OVER ANOTHER PLAYER'S CHARACTERS: what it says and what colour it is.
 *
 * The tag has two lines. Line 1 is the PLAYER's name as they typed it when joining (the world server's PLAYERS roster), never
 * the character's name. Line 2 is that player's faction name, smaller and dimmer. When the roster has no name for that player
 * (no world server, or no roster received yet) line 1 shows the faction name instead and line 2 is left out, so the tag never
 * shows a blank line or repeats the faction name.
 *
 * The colour says how the two players' factions stand, using the game's own levels: ally at 50 or more (or the ally flag),
 * hostile at -30 or less, neutral between. Each direction (mine towards theirs, theirs towards mine) is judged on its own and
 * the WORSE of the two colours the tag, so a player who treats you as an enemy shows red even when you do not.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

#include <string>

namespace nametag {

enum Level { kUnknown = -1, kFriendly = 0, kNeutral = 1, kHostile = 2 };   /* ordered: a higher number is worse */

const float kAllyAt    = 50.0f;    /* the game's own ally test: relation >= 50 */
const float kHostileAt = -30.0f;   /* the game's own enemy test: relation <= -30 */

/* One direction's level from its stored relation and ally flag. The enemy test comes first: the game's enemy test does not
   look at the ally flag, so a flagged ally at -30 or less is still an enemy. */
inline int LevelOf(float relation, bool allyFlag)
{
    if (relation <= kHostileAt) return kHostile;
    if (allyFlag || relation >= kAllyAt) return kFriendly;
    return kNeutral;
}

/* The worse of the two directions. A direction that could not be read does not count; when neither could, neutral. */
inline int WorseLevel(int a, int b)
{
    const bool ha = a >= kFriendly && a <= kHostile, hb = b >= kFriendly && b <= kHostile;
    if (!ha && !hb) return kNeutral;
    if (!ha) return b;
    if (!hb) return a;
    return a > b ? a : b;
}

/* The tag's colour for a level, 0..1 per channel: friendly #5cb473, neutral #f0cd57, hostile #e65139. */
struct Rgb { float r, g, b; };
inline Rgb LevelColour(int level)
{
    Rgb c;
    if (level == kFriendly)     { c.r = 0x5c / 255.0f; c.g = 0xb4 / 255.0f; c.b = 0x73 / 255.0f; }
    else if (level == kHostile) { c.r = 0xe6 / 255.0f; c.g = 0x51 / 255.0f; c.b = 0x39 / 255.0f; }
    else                        { c.r = 0xf0 / 255.0f; c.g = 0xcd / 255.0f; c.b = 0x57 / 255.0f; }
    return c;
}

/* Spaces and tabs at either end do not count as a name. */
inline std::string Trimmed(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

/* The two lines. playerName: the roster's name for that player's slot ("" when there is none); factionName: the name this
   game shows for that player's faction. */
struct Caption { std::string line1, line2; };
inline Caption CaptionFor(const std::string& playerName, const std::string& factionName)
{
    Caption c;
    const std::string p = Trimmed(playerName), f = Trimmed(factionName);
    if (!p.empty()) { c.line1 = p; c.line2 = f; }
    else c.line1 = f;   /* no name for that player yet: the faction name, as the tag said before, and no second line */
    return c;
}

}   /* namespace nametag */
