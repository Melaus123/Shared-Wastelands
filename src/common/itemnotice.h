#pragma once
// itemnotice.h - T-942 / owner decision 1191: a line in the chat window when ANOTHER player takes items from, or gives items to, one
// of this player's characters (never for boxes or other containers). Shown only on the game of that character's owner, decided
// there from the item requests that arrive from the other player's game. Moves by the same player on the same character in the same
// direction within 5 seconds of the first make one line. Approved wording:
//     "<other player's name> took Crossbow, 2 x Bolts from <your character's name>."
//     "<other player's name> gave 3 x Dried Meat to <your character's name>."
// ('N x ' only when N > 1; the same item named twice is counted once with its units added.)
// Pure: the plugin (items.cpp) and the offline suite compile this same header. C++03 (VS2010 v100).
#include <string>
#include <vector>
#include <cstdio>

namespace itemnotice {

const int kTook = 0;
const int kGave = 1;
const long long kGroupMs = 5000;   /* a line gathers the moves of its first 5 seconds */
const size_t kMaxGroups = 32;      /* open lines at most; a further move is counted as dropped */
const size_t kMaxItems = 24;       /* item names on one line at most; more are added as "and more" */

struct Item { std::string name; long long units; };
struct Group
{
    int slot;                 /* the other player's slot */
    unsigned int charUid;     /* this player's character */
    int dir;                  /* kTook / kGave */
    std::string who;          /* the other player's name */
    std::string charName;     /* the character's name */
    long long firstMs;
    std::vector<Item> items;
    bool more;                /* items past kMaxItems */
};

inline std::string Units(long long n) { char b[32]; std::sprintf(b, "%lld", n); return std::string(b); }

/* "Crossbow, 2 x Bolts" */
inline std::string ItemList(const std::vector<Item>& items, bool more)
{
    std::string s;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0) s += ", ";
        if (items[i].units > 1) s += Units(items[i].units) + " x ";
        s += items[i].name;
    }
    if (more) s += items.empty() ? std::string("more") : std::string(" and more");
    return s;
}

/* the sentence of one line */
inline std::string Sentence(const Group& g)
{
    const std::string who = g.who.empty() ? std::string("A player") : g.who;
    const std::string ch = g.charName.empty() ? std::string("your character") : g.charName;
    return who + (g.dir == kGave ? " gave " : " took ") + ItemList(g.items, g.more) + (g.dir == kGave ? " to " : " from ") + ch + ".";
}

/* one move added: to the open line of (slot, character, direction) if it is younger than kGroupMs, else a new line. Returns 1
   added, 0 refused (nothing named, or kMaxGroups lines open). */
inline int Add(std::vector<Group>* open, int slot, const std::string& who, unsigned int charUid, const std::string& charName, int dir,
               const std::string& itemName, long long units, long long nowMs)
{
    if (open == 0 || itemName.empty() || charUid == 0 || (dir != kTook && dir != kGave)) return 0;
    if (units < 1) units = 1;
    Group* g = 0;
    for (size_t i = 0; i < open->size(); ++i)
    {
        Group& x = (*open)[i];
        if (x.slot == slot && x.charUid == charUid && x.dir == dir && nowMs - x.firstMs < kGroupMs) { g = &x; break; }
    }
    if (g == 0)
    {
        if (open->size() >= kMaxGroups) return 0;
        Group n;
        n.slot = slot; n.charUid = charUid; n.dir = dir; n.who = who; n.charName = charName; n.firstMs = nowMs; n.more = false;
        open->push_back(n);
        g = &open->back();
    }
    for (size_t i = 0; i < g->items.size(); ++i)
        if (g->items[i].name == itemName) { g->items[i].units += units; return 1; }
    if (g->items.size() >= kMaxItems) { g->more = true; return 1; }
    Item it; it.name = itemName; it.units = units;
    g->items.push_back(it);
    return 1;
}

/* the lines whose 5 seconds are over (all of them when `all`), in the order they were opened, taken out of `open` */
inline std::vector<Group> Due(std::vector<Group>* open, long long nowMs, bool all)
{
    std::vector<Group> out;
    if (open == 0) return out;
    std::vector<Group> keep;
    for (size_t i = 0; i < open->size(); ++i)
    {
        const Group& g = (*open)[i];
        if (all || nowMs - g.firstMs >= kGroupMs) out.push_back(g);
        else keep.push_back(g);
    }
    open->swap(keep);
    return out;
}

/* the line is this player's to see: a non-trade request on a character (not a box: ownerUid != 0) of this game's own player,
   served (ok) for a player of another game whose own character (that player's faction here, not an NPC that game runs) took or
   gave */
inline bool Wanted(int ok, unsigned int ownerUid, int trade, bool ownersCharacter, bool fromOtherPlayer, bool takerIsThatPlayers)
{
    return ok != 0 && ownerUid != 0 && trade == 0 && ownersCharacter && fromOtherPlayer && takerIsThatPlayers;
}

}
