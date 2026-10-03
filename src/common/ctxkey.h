#pragma once
/* THE CONTEXT MAP'S SQUAD-ID KEY. The plugin keeps one local squad per other player's announced squad (spawn.cpp g_ctxPlatoons).
   When the squad's id is known its row is filed under

       id|<squad id>|<faction tag>

   where the faction tag is the faction the squad was built under, printed by the caller (its address in hex). The faction is part
   of the key because every game numbers its own squads, so two other players' squads can carry the same id ('Nameless_0'); with
   the id alone they shared one row, and one player's announcement reused or destroyed the other player's squad.
   A faction tag never contains '|', so the squad id is everything between "id|" and the LAST '|' (an id may itself hold a bar).
   PURE: the plugin and the offline suite (src/coop-test) compile this same header. */

#include <string>

namespace coopctx {

inline std::string CtxKeyMake(const std::string& id, const std::string& factionTag)
{
    return "id|" + id + "|" + factionTag;
}

/* The squad id of a key CtxKeyMake composed; "" for any other key (the older senders' template|building|town key, or an
   "id|" key with no faction part). */
inline std::string CtxKeyIdOf(const std::string& key)
{
    if (key.size() < 3 || key.compare(0, 3, "id|") != 0) return std::string();
    const std::string::size_type bar = key.rfind('|');
    if (bar == std::string::npos || bar <= 2) return std::string();
    return key.substr(3, bar - 3);
}

}   // namespace coopctx
