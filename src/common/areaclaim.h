/* src/common/areaclaim.h - WHAT THE NOTEBOOK REMEMBERS ACROSS A RESTART, AS PURE DECISIONS (B13).
 *
 * Same charter as storelink.h and clockmath.h: nothing in here reads a global, opens a file, or includes a
 * Windows, ENet or Ogre header. Every function is a pure function of its arguments, and the same
 * header is compiled into the notebook (SharedWastelandsServer.exe) and into the offline test exe - so the rule that
 * decides who holds an area after a restart, and the format of the three files that carry it, exist once.
 *
 * WHY IT EXISTS (T239, Confirmed). The notebook's area map, its slot numbers and its authority were all in
 * memory and all re-derived from DIAL ORDER. A relay killed and restarted therefore handed the areas, the
 * slot numbers (which RECORD.owner carries) and the authority flag to whichever game happened to reconnect
 * first: the map flipped, and the other game's replayed move was refused as a collision because the relay
 * had just told that game it was the area's writer. B13 gives every player a stable id, writes the id ->
 * slot table, the area map and the operator beside the records, and restores all three at start.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_COMMON_AREACLAIM_H
#define COOP_COMMON_AREACLAIM_H

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace coopstore {

/* ================= THE PLAYER ID =================
   32 lowercase hex characters, generated once by the plugin into shared_wastelands.cfg and carried in every HELLO.
   The test is a SHAPE test and nothing more - this header cannot know which ids exist. It is here rather than
   in cfgtext.h because the relay refuses a malformed id too, and one rule may not have two implementations. */
const size_t kPlayerIdLen = 32;

inline int PlayerIdOk(const std::string& id)
{
    if (id.size() != kPlayerIdLen) return 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        const int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) return 0;
    }
    return 1;
}

/* prof1 (docs/design-profiles1.md s1; src/common/profiles.h): A NOTEBOOK SLOT KEY - the name slots.txt and areas.txt key
   a player on. A person's profile 1 keeps the bare 32-hex person id (so a world from before profiles hands each person's slot
   and areas to their profile 1 unchanged); profile n >= 2 is "<person>.<n>", n written without a leading zero, 2..9999. */
inline int SlotKeyOk(const std::string& key)
{
    const size_t d = key.find('.');
    if (d == std::string::npos) return PlayerIdOk(key);
    if (!PlayerIdOk(key.substr(0, d))) return 0;
    const std::string n = key.substr(d + 1);
    if (n.empty() || n.size() > 4 || n[0] == '0') return 0;
    for (size_t i = 0; i < n.size(); ++i) if (n[i] < '0' || n[i] > '9') return 0;
    return std::atoi(n.c_str()) >= 2;
}

/* ================= ONE ID, ONE GAME - BUT A PLAYER OUTLIVES ITS OWN CONNECTION (B13-c) =================
   A HELLO carrying an id the notebook already has on ANOTHER connection is one of two things, and the
   notebook must tell them apart: two installs sharing a copied shared_wastelands.cfg (refuse the newcomer, in
   words), or the SAME game back on a new socket after a re-dial, a crash or a dropped network, while the
   notebook still holds the socket it left (drop the old one and take the new). Nothing in the HELLO says
   which. What does say is the old connection's SILENCE: ENet answers every ping within a round trip and
   the game reports its clock once a second, so a live connection is never quiet for long, and one the game
   abandoned is quiet from the moment it left. The notebook measures how long since the old connection last
   acknowledged anything and asks this function.

   The bound is a time, which the design principles forbid as a proxy for state; it is allowed here because
   the liveness of a remote socket HAS no event - the absence of one is the whole question - and ENet itself
   declares a peer dead by the same silence, only after 5 to 30 s. Three seconds is six missed ping rounds
   and three missed clock reports. At or inside the bound the old connection is live and the newcomer is
   REFUSED (the copied-file case, or a second machine that is merely lagging - it re-dials and asks again);
   past it the old connection is evicted. review-b13-b Q2 (2026-09-18, Read): without this, B12-e's re-dial
   after a non-graceful drop was refused as a duplicate of ITSELF for up to the ENet timeout, and the refusal
   told a single-install player to delete the playerid line from "one of the two" settings files.

   PRE-PATCH EQUIVALENT: refuse always. The offline row that fails on the old behaviour: 3.001 s of silence
   -> kDupIdEvictOld. */
const double kDupIdLiveSec = 3.0;

enum DuplicateIdAction
{
    kDupIdRefuse   = 0,   /* the connection already holding this id is live: the newcomer is refused in words */
    kDupIdEvictOld = 1    /* the old connection has been silent past the bound: it is dropped, the newcomer takes the id */
};

inline int DuplicateIdDecide(double oldPeerSilentSec, double liveSec)
{
    if (oldPeerSilentSec <= liveSec) return kDupIdRefuse;   /* the boundary itself is LIVE - a connection heard from within the window stands */
    return kDupIdEvictOld;
}

/* ================= WHO HOLDS AN AREA AFTER A RESTART =================
   Three answers, and the caller acts on exactly one of them.

   THE ARGUMENTS.
     restoredOwnerKnownNow - 1 when the id named as this row's owner has ALREADY SPOKEN to this process
                             (it dialled in and was welcomed), 0 when it has not been heard from since the
                             restart. The whole point of the restore grace is that 0 is not the same as gone.
     ownerSilentSec        - how long the owner has been silent. When the owner is known now, that is the time
                             since its last AREAS report; when it is not, it is the age of the RESTORE (how
                             long this process has been running), because a name that has never spoken has
                             been silent for exactly that long.
     reporterIsOwner       - 1 the game reporting this area right now IS the owner; 0 somebody else is
                             reporting it; -1 NOBODY is reporting it right now (the once-a-second sweep asks
                             with -1, because it has no reporter in its hand).
     restoreGraceSec       - how long a restored claim stands for an owner that has not dialled back in yet.
                             60 s: a game re-dialling on B12-e's cadence needs about 17 s an attempt, so 60
                             covers three of them.
     leaseSec              - the ordinary silence lease once the owner IS live again (kGraceSec, 10 s).

   PRE-PATCH EQUIVALENT: there was no restored row at all, so every area was "first there" (kTransfer to
   whoever reported first) the moment the relay came up. The offline row that fails on the old behaviour is
   a restored row inside its grace with another game reporting: kKeep here, kTransfer before. */
enum AreaClaimAction
{
    kAreaKeep     = 0,   /* the owner keeps it: it is back, or it is inside the restore grace */
    kAreaTransfer = 1,   /* the owner is past its lease/grace AND another game is reporting - it passes over */
    kAreaRelease  = 2    /* the owner is past its lease/grace and NOBODY is reporting - the row falls to nobody */
};

inline int AreaClaimDecide(int restoredOwnerKnownNow, double ownerSilentSec, int reporterIsOwner,
                           double restoreGraceSec, double leaseSec)
{
    /* The owner itself is reporting: nothing else matters, and this is the only arm that can end a restore. */
    if (reporterIsOwner == 1) return kAreaKeep;
    const double budget = restoredOwnerKnownNow ? leaseSec : restoreGraceSec;
    if (ownerSilentSec <= budget) return kAreaKeep;      /* the boundary itself KEEPS - a claim is lost past it, not at it */
    return (reporterIsOwner == 0) ? kAreaTransfer : kAreaRelease;
}

/* ================= THE SLOT NUMBER, KEYED ON THE ID =================
   A known id gets its old number back - even when a new id dialled first and would have taken it under
   AssignSlot's smallest-free rule. A new id gets the smallest number NO KNOWN ID holds, which is not the
   same as the smallest number no CONNECTED game holds: a number belonging to a player who is offline is
   still that player's, or RECORD.owner would rename past writers at the next restart. */
/* M1 (decisions 54 / 56(b), design-many D2): the numbers run 0 .. kSlotLifetimeMax - 1. It is a LIFETIME limit -
   a number is never handed back, because a player keeps it for the world's whole life - and a new id past it is
   REFUSED in words (storelink.h kRefuseLifetimeFull), never "given none". 65,520 is D2(b)'s figure: 16 blocks of
   65,536 group numbers plus 65,504 blocks of 32,768 is exactly 2^31, so every group number stays a positive int. */
const int kSlotLifetimeMax = 65520;

/* The search, over the SET of numbers already held - which is what makes it cheap. A full world answers -1 at
   once; otherwise the walk starts at lower_bound(floor), O(log n), and steps only over the run of consecutive
   held numbers that begins at the floor (M1: the old version scanned every known id for every candidate
   number, quadratic, and stopped at 1024). floor is --first-slot, a TEST option that lets a run reach high
   numbers without 20,000 games; it is 0 on every shipping path. */
inline int SlotFirstFreeDecide(const std::set<int>& used, int floor)
{
    if (floor < 0) floor = 0;
    if ((int)used.size() >= kSlotLifetimeMax) return -1;
    int c = floor;
    for (std::set<int>::const_iterator it = used.lower_bound(floor); it != used.end() && *it == c; ++it) ++c;
    return c < kSlotLifetimeMax ? c : -1;
}

/* The rule, for a caller that keeps the set beside the map (the notebook: g_slotsUsed). -1 = this world has
   admitted its lifetime limit of players and this id is new. */
inline int SlotAssignDecide(const std::map<std::string, int>& known, const std::set<int>& used,
                            const std::string& id, int floor)
{
    std::map<std::string, int>::const_iterator it = known.find(id);
    if (it != known.end()) return it->second;
    return SlotFirstFreeDecide(used, floor);
}

/* The same rule for a caller holding only the map (the offline suite): it builds the set, O(n log n). */
inline int SlotAssignDecide(const std::map<std::string, int>& known, const std::string& id, int floor = 0)
{
    std::map<std::string, int>::const_iterator it = known.find(id);
    if (it != known.end()) return it->second;
    std::set<int> used;
    for (std::map<std::string, int>::const_iterator k = known.begin(); k != known.end(); ++k) used.insert(k->second);
    return SlotFirstFreeDecide(used, floor);
}

/* ================= THE CONNECTED LIMIT (M1, decision 56(b)) =================
   How many games may be linked to the notebook at the same moment - a separate limit from the lifetime one
   above. connectedNow counts the admitted games OTHER than the one asking. At the limit the one asking is
   refused in words (storelink.h kRefuseFull) and may try again later. Named kConnect* because storelink.h's
   refusal REASON is already called kRefuseFull in this namespace. */
enum ConnectAdmitAction { kConnectAdmit = 0, kConnectRefuseFull = 1 };

inline int ConnectAdmitDecide(int connectedNow, int maxConnected)
{
    return (connectedNow < maxConnected) ? kConnectAdmit : kConnectRefuseFull;
}

/* ================= THE THREE FILES, ONE LINE AT A TIME =================
   All three are written in clock.txt's shape by the caller (a temp file plus MoveFileExA); what lives here is
   the LINE: the format and the parse, together, so that what the notebook writes is what it reads back. A
   line that does not parse is DROPPED and never guessed at - a corrupt slots.txt row that was guessed would
   hand one player's number, and therefore that player's past records, to somebody else. */

inline std::string AcNum(long long v) { char b[32]; std::sprintf(b, "%lld", v); return b; }

/* Split on tabs, exactly as the store's own loaders do, and drop a trailing CR so a file that has been
   through a text editor still reads. */
inline void AcFields(const std::string& lineIn, std::vector<std::string>* out)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    out->clear();
    size_t at = 0;
    while (at <= line.size())
    {
        const size_t t = line.find('\t', at);
        if (t == std::string::npos) { out->push_back(line.substr(at)); break; }
        out->push_back(line.substr(at, t - at));
        at = t + 1;
    }
}

/* ---- slots.txt: <playerId> <slot> <lastSeenUnix> ---- */
inline std::string SlotsLineFormat(const std::string& playerId, int slot, long long lastSeenUnix)
{
    return "v1\t" + playerId + "\t" + AcNum((long long)slot) + "\t" + AcNum(lastSeenUnix);
}

inline int SlotsLineParse(const std::string& line, std::string* playerId, int* slot, long long* lastSeenUnix)
{
    std::vector<std::string> f;
    AcFields(line, &f);
    if (f.size() < 4 || f[0] != "v1") return 0;
    if (!SlotKeyOk(f[1])) return 0;   /* prof1: a profile's key */
    const int s = std::atoi(f[2].c_str());
    if (s < 0 || s >= kSlotLifetimeMax) return 0;
    *playerId = f[1];
    *slot = s;
    *lastSeenUnix = (long long)std::atof(f[3].c_str());
    return 1;
}

/* ---- areas.txt: <x>,<y> <ownerPlayerId> <assignedUnix> ---- */
inline std::string AreasLineFormat(int x, int y, const std::string& ownerId, long long assignedUnix)
{
    return "v1\t" + AcNum((long long)x) + "," + AcNum((long long)y) + "\t" + ownerId + "\t" + AcNum(assignedUnix);
}

inline int AreasLineParse(const std::string& line, int* x, int* y, std::string* ownerId, long long* assignedUnix)
{
    std::vector<std::string> f;
    AcFields(line, &f);
    if (f.size() < 4 || f[0] != "v1") return 0;
    const size_t comma = f[1].find(',');
    if (comma == std::string::npos) return 0;
    const int px = std::atoi(f[1].substr(0, comma).c_str());
    const int py = std::atoi(f[1].substr(comma + 1).c_str());
    if (px < 0 || px >= 64 || py < 0 || py >= 64) return 0;
    if (!SlotKeyOk(f[2])) return 0;   /* prof1: a profile's key */
    *x = px; *y = py; *ownerId = f[2];
    *assignedUnix = (long long)std::atof(f[3].c_str());
    return 1;
}

/* ---- owner.txt: the operator's id and where it was learned (decision 49) ---- */
enum OwnerSource
{
    kOwnerSourceNone     = 0,   /* nobody is the operator yet */
    kOwnerSourceArg      = 1,   /* --owner on the command line: the panel or the harness started this notebook FOR that player */
    kOwnerSourceFirst    = 2,   /* no --owner was ever given, so the first game to talk to this notebook after its WELCOME became the operator (W3-f; before it, the first HELLO) */
    kOwnerSourceRestored = 3    /* owner.txt said so - which is the whole reason the file exists */
};

/* W3-f (review-w3 item 2): WHEN A WORLD WITH NO OPERATOR APPOINTS ONE - at a game's first message AFTER its WELCOME,
   never at its HELLO: a game that then refuses this notebook's world (or protocol) sends nothing more, because every send
   of the plugin is behind LinkUp(). senderAdmitted = the sender's HELLO was let in (it has an id, a slot and a WELCOME). */
inline bool OwnerAppointOnMessage(bool haveOwner, bool senderAdmitted, bool msgIsHello)
{
    return !haveOwner && senderAdmitted && !msgIsHello;
}

inline const char* OwnerSourceName(int src)
{
    if (src == kOwnerSourceArg)      return "arg";
    if (src == kOwnerSourceFirst)    return "first";
    if (src == kOwnerSourceRestored) return "restored";
    return "none";
}

inline std::string OwnerLineFormat(const std::string& ownerId, int source)
{
    return "v1\t" + ownerId + "\t" + std::string(OwnerSourceName(source));
}

/* The source comes back as RESTORED whatever the file says it was, because reading it out of the file is how
   this process learned it - recording "arg" for a run that was given no argument would be a number that reads
   as one thing and reports another (the clock's seed state made exactly that mistake in P7u). */
inline int OwnerLineParse(const std::string& line, std::string* ownerId, int* source)
{
    std::vector<std::string> f;
    AcFields(line, &f);
    if (f.size() < 2 || f[0] != "v1") return 0;
    if (!PlayerIdOk(f[1])) return 0;
    *ownerId = f[1];
    *source = kOwnerSourceRestored;
    return 1;
}

/* P5t (T418): WHO THE NOTEBOOK GIVES AN UNCLAIMED AREA TO. Each game's own ring-1 tie-break (zones.cpp
   MayInventFromView / MySlotLower) lets the LOWER slot invent when both players stand within one area of it; the
   notebook used to name whichever game's loaded report arrived first (T418: B stepped aside, A invented, and B's
   report named B). Now it names the tie-break's winner: the LOWEST slot among players whose position is fresh
   (now - at <= freshSec, at > 0) and within ring 1 (Chebyshev 1) of the area; nobody -> the reporter, as before.
   *nRing1 = how many such players; *why = kAssignRing1 / kAssignReporter. Pure: the offline suite sweeps it. */
const int kAssignReporter = 0, kAssignRing1 = 1;
inline int AreaAssignDecide(int reporterSlot, int ax, int ay, int count, const int* slots, const int* xs, const int* ys,
                            const double* ats, double now, double freshSec, int* nRing1, int* why)
{
    int low = -1, nr = 0;
    for (int i = 0; i < count; ++i)
    {
        if (slots[i] < 0 || slots[i] > 15 || xs[i] < 0 || ys[i] < 0 || ats[i] <= 0.0 || now - ats[i] > freshSec) continue;   /* area2 fold: slots 0-15, the plugin's table */
        const int dx = xs[i] - ax, dy = ys[i] - ay;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;
        ++nr;
        if (low < 0 || slots[i] < low) low = slots[i];
    }
    if (nRing1 != 0) *nRing1 = nr;
    if (why != 0) *why = (low >= 0) ? kAssignRing1 : kAssignReporter;
    return (low >= 0) ? low : reporterSlot;
}

/* area2 fold (review-area2 MED): AN UNCONFIRMED ASSIGNMENT. An area given to a ring-1 slot that has not yet listed it as
   loaded is held for that slot kUnconfirmedLeaseSec (H047: ~8.6-11.6 s from a teleport to the area reading loaded) and not
   on the ordinary 10 s lease stamped at the assignment; past it, it is handed to the reporter. The assignee's own report
   confirms it (and stamps its lease). Not unconfirmed -> the ordinary rules. */
const double kUnconfirmedLeaseSec = 12.0;
const int kLeaseNormal = 0, kLeaseConfirm = 1, kLeaseHold = 2, kLeaseHandOver = 3;
inline int UnconfirmedLeaseDecide(int unconfirmed, int reporterIsAssignee, double sinceAssignSec, double leaseSec)
{
    if (unconfirmed == 0) return kLeaseNormal;
    if (reporterIsAssignee != 0) return kLeaseConfirm;
    if (sinceAssignSec > leaseSec) return kLeaseHandOver;
    return kLeaseHold;
}

}   /* namespace coopstore */

#endif
