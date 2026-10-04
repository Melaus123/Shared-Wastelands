#pragma once
// T-546 step 4 (t546b): WHAT MEMBERSHIP DOES ON EACH GAME. Pure: the plugin (team.cpp, tags.cpp, relations.cpp, policy.cpp)
// and the offline suite compile this same header. The table itself is src/common/teamwire.h (the world server's, as each
// game last received it).
//
// Owner decisions (2026-10-03, .modding/02-project-rules.md 473-481, page build/pages/player-factions.html):
//   - members are allies to each other: while two players share a team, each game holds ITS OWN side towards every teammate
//     at the engine's top standing (the PIN). Each game writes only its own side, as every standing between players is
//     written (relations.cpp Owned), so the other games learn it through the ordinary forwarding.
//   - 478: a player in a team shows the FOUNDER's faction name; leaving gives the player's own name back.
//   - 479: members see each other's name tags in blue #5a9be6 (nametag.h TeamColour). A player outside the team sees the
//     members' tags in the colour of its own standing with them, with the team's name on line 2.
//   - 481: every member's base is open to every member, whatever the world's base access policy (policy.cpp).
// C++03 (VS2010 v100).
#include "teamwire.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>

namespace swteam {

const float kPinRelation = 100.0f;   /* a pinned side's value: the engine's top standing (ally is 50 or more) */
const float kPinSlack = 0.5f;        /* a pinned side read this far below kPinRelation is written back */
const unsigned kSlotIndexCap = 4096; /* slots a game indexes by team (the wire's slot range, team.cpp's any-thread index) */

/* the slots that share `me`'s team on a received table, without `me`, ascending and once each; empty when `me` is in none */
inline std::vector<unsigned> TeammatesOf(const std::vector<WireTeam>& t, unsigned me)
{
    std::vector<unsigned> out;
    const int i = WireTeamOf(t, me, 0);
    if (i < 0) return out;
    if (t[i].founderSlot != me) out.push_back(t[i].founderSlot);
    for (size_t m = 0; m < t[i].memberSlots.size(); ++m) if (t[i].memberSlots[m] != me) out.push_back(t[i].memberSlots[m]);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
/* two different slots in one team */
inline bool SameTeam(const std::vector<WireTeam>& t, unsigned a, unsigned b)
{
    if (a == b) return false;
    const int ia = WireTeamOf(t, a, 0);
    return ia >= 0 && ia == WireTeamOf(t, b, 0);
}
/* the team NUMBER each slot is in (0 = none), for the slots below kSlotIndexCap; slots at or above it are left out */
inline std::vector<unsigned> TeamIndex(const std::vector<WireTeam>& t)
{
    std::vector<unsigned> idx(kSlotIndexCap, 0u);
    for (size_t i = 0; i < t.size(); ++i)
    {
        const unsigned no = t[i].no == 0 ? 0xFFFFFFFFu : t[i].no;   /* a team numbered 0 still counts as a team */
        if (t[i].founderSlot < kSlotIndexCap) idx[t[i].founderSlot] = no;
        for (size_t m = 0; m < t[i].memberSlots.size(); ++m) if (t[i].memberSlots[m] < kSlotIndexCap) idx[t[i].memberSlots[m]] = no;
    }
    return idx;
}

/* ---------------------------------------------- THE PIN (members are allies) ---------------------------------------------- */
/* pinned: the slots this game holds at ally now; teammates: the slots it must hold (both ascending, once each - TeammatesOf).
   start = teammates not pinned yet; end = pinned slots no longer teammates (left, removed, disbanded, or this game left);
   keep = both. */
struct PinPlan { std::vector<unsigned> start, end, keep; };
inline PinPlan PlanPins(const std::vector<unsigned>& pinned, const std::vector<unsigned>& teammates)
{
    PinPlan p;
    std::set_difference(teammates.begin(), teammates.end(), pinned.begin(), pinned.end(), std::back_inserter(p.start));
    std::set_difference(pinned.begin(), pinned.end(), teammates.begin(), teammates.end(), std::back_inserter(p.end));
    std::set_intersection(pinned.begin(), pinned.end(), teammates.begin(), teammates.end(), std::back_inserter(p.keep));
    return p;
}
/* a pinned side is written back when it was read and stands below the pin (a fight, or a change from another road) */
inline bool PinNeedsWrite(bool read, float relation) { return read && relation < kPinRelation - kPinSlack; }
/* a stance this player may choose towards another player (0 ally, 1 neutral, 2 hostile): towards a teammate only ally - the
   pin holds that side at ally while both share the team */
inline bool StanceAllowedTowards(bool teammate, int level) { return !teammate || level == 0; }

/* ------------------------------------------------------ THE NAME TAG ------------------------------------------------------ */
enum { kTagByStanding = 0, kTagTeam = 1 };
/* the colour class of another player's tag: team blue when that player shares this player's team, else the standing's colour */
inline int TagColourClass(bool sameTeamAsMe) { return sameTeamAsMe ? kTagTeam : kTagByStanding; }
/* line 2 (the faction line): a player in a team shows the team's name - the founder's faction name as this game shows it now
   (founderNow), else the name the team was made with (teamName); a player in no team shows its own faction name */
inline std::string TagFactionLine(const std::string& own, bool inTeam, const std::string& founderNow, const std::string& teamName)
{
    if (!inTeam) return own;
    for (size_t i = 0; i < founderNow.size(); ++i) if (founderNow[i] != ' ' && founderNow[i] != '\t') return founderNow;
    return teamName.empty() ? own : teamName;
}

/* --------------------------------------------------------- BASES ---------------------------------------------------------- */
/* a building whose owner is player ownerSlot opens to player askerSlot as a member's: two different players (slots >= 0) in
   the same team (team numbers from TeamIndex, 0 = none). The owner asking is not a member's case - the policy's owner rule. */
inline bool MemberOpens(int ownerSlot, int askerSlot, unsigned ownerTeam, unsigned askerTeam)
{
    return ownerSlot >= 0 && askerSlot >= 0 && ownerSlot != askerSlot && ownerTeam != 0 && ownerTeam == askerTeam;
}

/* ------------------------------------------------- A PLAYER NAMED IN A NOTICE ------------------------------------------------- */
/* the roster's name (a player connected now), else the name the roster gave that slot earlier this session, else that
   player's faction name as this game shows it, else "s<slot>" */
inline std::string PlayerWords(const std::string& roster, const std::string& remembered, const std::string& faction, unsigned slot)
{
    if (!roster.empty()) return roster;
    if (!remembered.empty()) return remembered;
    if (!faction.empty()) return faction;
    return "s" + U(slot);
}

}   // namespace swteam
