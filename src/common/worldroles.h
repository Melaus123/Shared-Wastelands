/* worldroles.h - WHO HOLDS WHICH PART OF A WORLD'S AUTHORITY on the world server. Pure: no I/O, no globals.

   OWNER   - a connection logged in with the owner password from the world's rules.txt (adminauth.h, owner 1369 b; at its door or by
             /login): admin without the admin password, the admin list, the admin password, never kicked or banned. A player id
             alone never makes anyone the owner - it is self-reported - and an empty owner password line means no owner at all.
   ADMIN   - may change the world's rules (OPTIONS) and set the speed under `fixed`: a game logged in with the owner password, or
             one whose person is in `named` (admins.txt) logged in with the admin password. `named` holds the admins' person ids.
   STEWARD - runs the technical duties (weather, faction relations, save numbering, sleeping squads, the AUTH flag) and sets no
             rules. Whoever is in the world holds them, the same way in a world started from the game's HOST and one started by
             hand (owner 1381: nobody waits for the owner): the owner while in the world, else the steward in post while in the
             world, else the first game in the world, else - nobody in the world yet - the first admitted game (it is loading, or
             at the title), else nobody (an empty string).
   Every id here is a PERSON id (profiles.h PersonOfKey), never a profile key. */
#ifndef COOP_WORLDROLES_H
#define COOP_WORLDROLES_H

#include <string>
#include <vector>

namespace worldroles {

const int kSlotUnknown = -1;   /* a role is held, but the holder's slot is not known yet */
const int kSlotNone = -2;      /* nobody holds the role */

/* LISTED: a person in admins.txt - who may log in with the admin password. The owner's person id is never listed by itself. */
inline bool IsListed(const std::string& person, const std::vector<std::string>& named)
{
    if (person.empty()) return false;
    for (size_t i = 0; i < named.size(); ++i) if (named[i] == person) return true;
    return false;
}

/* every listed admin once, in their order (an empty id and a repeat are skipped) */
inline std::vector<std::string> AdminList(const std::vector<std::string>& named)
{
    std::vector<std::string> out;
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
    std::string owner;            /* the person of a connection logged in with the owner password and in the world; empty = none */
    std::string current;          /* the steward in post; empty = vacant */
    bool currentInWorld;          /* a connection of the steward in post is in the world */
    std::string firstInWorld;     /* the first game in the world in connection order, a leaving one excluded; empty = none */
    std::string firstConnected;   /* the first admitted game in connection order (any stage), a leaving one excluded; empty = none */
    StewardState() : currentInWorld(false) {}
};

/* who holds the duties now: the owner in the world first, then whoever is in the world (the one in post keeps them), and while
   nobody is in the world the first admitted game - so a world's first game seeds it as it loads, whoever it is */
inline std::string StewardFor(const StewardState& s)
{
    if (!s.owner.empty()) return s.owner;
    if (!s.current.empty() && s.currentInWorld) return s.current;
    if (!s.firstInWorld.empty()) return s.firstInWorld;
    return s.firstConnected;
}

/* A JOINER NEVER WAITS FOR THE OWNER (owner 1381): the roster says the post is open (joinstage.h opVacant) whenever the steward is
   not in the world, so a joiner's load goes ahead; the first game in the world then holds the duties (StewardFor). */
inline bool JoinWaitsForNobody(bool stewardInWorld)
{
    return !stewardInWorld;
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
