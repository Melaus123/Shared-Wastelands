/* src/common/tagcfg.h - T-1153 (owner 1245): tags.cfg, THE THREE NAME-TAG PARTS THIS PLAYER CHOSE.
 *
 * The pause menu's MULTIPLAYER window (src/coop-plugin/ui.cpp) has three tick boxes - Player name, Character name, Faction or
 * team name - for the name tags over other players' characters. Each player's choice is kept on that player's own computer, in
 * tags.cfg in the data folder (beside player.cfg), and is never stored in the world or sent to another game. The file is three
 * lines, tagPlayer=1, tagCharacter=1, tagFaction=1 (0 = that part off). A part whose key is missing is on, so a missing file
 * shows every part; a line whose value is not 0 or 1, or whose key is none of the three, is ignored; a key named twice takes
 * its last line. Pure text in and out, no files; swept by src/coop-test (t_tagcfg_read_write).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstddef>
#include <string>

namespace tagcfg {

const char* const kFileName     = "tags.cfg";
const char* const kKeyPlayer    = "tagPlayer";
const char* const kKeyCharacter = "tagCharacter";
const char* const kKeyFaction   = "tagFaction";
const int kMaxLines = 64;   /* lines looked at, at most */

struct Parts
{
    bool player, character, faction;
};

inline Parts Defaults()
{
    Parts p;
    p.player = true; p.character = true; p.faction = true;
    return p;
}

inline bool Same(const Parts& a, const Parts& b)
{
    return a.player == b.player && a.character == b.character && a.faction == b.faction;
}

/* Spaces, tabs and a carriage return taken off both ends. */
inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

/* The parts the text names: each part on unless a line `<key>=0` turns it off (and no later `<key>=1` turns it on again). */
inline Parts Parse(const std::string& text)
{
    Parts p = Defaults();
    size_t at = 0;
    int lines = 0;
    while (at <= text.size() && lines < kMaxLines)
    {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = Trim(text.substr(at, nl - at));
        at = nl + 1;
        ++lines;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));
        if (val != "0" && val != "1") continue;
        const bool on = (val == "1");
        if (key == kKeyPlayer) p.player = on;
        else if (key == kKeyCharacter) p.character = on;
        else if (key == kKeyFaction) p.faction = on;
    }
    return p;
}

/* The whole file: the three lines, each ended by CR LF. */
inline std::string Format(const Parts& p)
{
    return std::string(kKeyPlayer) + "=" + (p.player ? "1" : "0") + "\r\n"
         + kKeyCharacter + "=" + (p.character ? "1" : "0") + "\r\n"
         + kKeyFaction + "=" + (p.faction ? "1" : "0") + "\r\n";
}

/* For the log: "player=on character=off faction=on". */
inline std::string Words(const Parts& p)
{
    return std::string("player=") + (p.player ? "on" : "off") + " character=" + (p.character ? "on" : "off")
         + " faction=" + (p.faction ? "on" : "off");
}

}   /* namespace tagcfg */
