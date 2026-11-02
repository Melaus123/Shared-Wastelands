/* worldroles.h - WHO HOLDS WHICH PART OF A WORLD'S AUTHORITY on the world server. Pure: no I/O, no globals.

   OWNER   - the player the world server was started for (--owner, owner.txt, or the first game to talk after its WELCOME).
   ADMIN   - may change the world's rules (OPTIONS) and set the speed under `fixed`. The owner is always an admin; `named` holds
             the other admins' person ids.
   STEWARD - runs the technical duties (weather, faction relations, save numbering, sleeping squads, the AUTH flag) and sets no
             rules. While the duties are not handed over (handOver false) the steward is the owner, connected or not - the one
             operator every world had before the split. With hand-over (a world kept running, --keep-running): the owner while in
             the world, else the steward in post while still connected, else the first game in the world, else the owner while
             connected (still loading: the host is the first steward), else nobody (an empty string).
   Every id here is a PERSON id (profiles.h PersonOfKey), never a profile key. */
#ifndef COOP_WORLDROLES_H
#define COOP_WORLDROLES_H

#include <string>
#include <vector>

namespace worldroles {

const int kSlotUnknown = -1;   /* a role is held, but the holder's slot is not known yet */
const int kSlotNone = -2;      /* nobody holds the role */

inline bool IsAdmin(const std::string& person, const std::string& owner, const std::vector<std::string>& named)
{
    if (person.empty()) return false;
    if (person == owner) return true;
    for (size_t i = 0; i < named.size(); ++i) if (named[i] == person) return true;
    return false;
}

/* every admin once: the owner first, then the named admins in their order (an empty id and a repeat are skipped) */
inline std::vector<std::string> AdminList(const std::string& owner, const std::vector<std::string>& named)
{
    std::vector<std::string> out;
    if (!owner.empty()) out.push_back(owner);
    for (size_t i = 0; i < named.size(); ++i)
    {
        if (named[i].empty()) continue;
        bool seen = false;
        for (size_t k = 0; k < out.size(); ++k) if (out[k] == named[i]) seen = true;
        if (!seen) out.push_back(named[i]);
    }
    return out;
}

struct StewardState
{
    std::string owner;          /* the owner; empty = no owner yet */
    bool ownerInWorld;          /* a connection of the owner is in the world */
    bool ownerConnected;        /* a connection of the owner is admitted (in the world or not) */
    std::string current;        /* the steward in post; empty = vacant */
    bool currentConnected;      /* a connection of the steward in post is still admitted */
    std::string firstInWorld;   /* the first game in the world in connection order, a leaving one excluded; empty = none */
    bool handOver;              /* the duties may pass to another player while the owner is away */
    StewardState() : ownerInWorld(false), ownerConnected(false), currentConnected(false), handOver(false) {}
};

/* who holds the duties now (the pull request's StewardMoveDecide, with the owner first, and last while nobody is in the world) */
inline std::string StewardFor(const StewardState& s)
{
    if (!s.handOver) return s.owner;
    if (!s.owner.empty() && s.ownerInWorld) return s.owner;
    if (!s.current.empty() && s.currentConnected) return s.current;
    if (!s.firstInWorld.empty()) return s.firstInWorld;
    if (!s.owner.empty() && s.ownerConnected) return s.owner;   /* nobody in the world yet: the owner's game, still loading, holds them */
    return std::string();
}

/* A KEPT WORLD NOBODY WILL OPEN AGAIN UNLESS A JOINER MAY LOAD IN: with hand-over, the post vacant (StewardFor gave nobody - no game
   in the world, the owner not connected) after the owner's game was here once. The roster says so (joinstage.h opVacant) and a
   joiner's load does not wait for an operator; the first game in the world then takes the duties. Without hand-over (hosted,
   world door) the steward is always the owner, so this is never true and a joiner waits for the owner as before; a kept world
   whose owner's game has not arrived yet waits for it too. */
inline bool JoinWaitsForNobody(bool handOver, const std::string& steward, bool ownerEverHere)
{
    return handOver && steward.empty() && ownerEverHere;
}

inline std::string SlotWord(int slot)
{
    if (slot == kSlotNone) return "none";
    if (slot < 0) return "unknown";
    char buf[16]; int n = 0; unsigned v = (unsigned)slot;
    do { buf[n++] = (char)('0' + v % 10u); v /= 10u; } while (v != 0u && n < 15);
    std::string s;
    while (n > 0) s += buf[--n];
    return s;
}

/* the log line's words, without its reason: "roles: owner=3 steward=3 admins=[3]" */
inline std::string RolesLine(int ownerSlot, int stewardSlot, const std::vector<int>& adminSlots)
{
    std::string s = "roles: owner=" + SlotWord(ownerSlot) + " steward=" + SlotWord(stewardSlot) + " admins=[";
    for (size_t i = 0; i < adminSlots.size(); ++i) { if (i != 0) s += ","; s += SlotWord(adminSlots[i]); }
    return s + "]";
}

}   /* namespace worldroles */

#endif
