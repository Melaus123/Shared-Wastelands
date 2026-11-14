#pragma once
// T-1122 THE ROLLER FOR AN OFFLINE OWNER'S BASE (owner decision 1577 c, 2026-11-09) - the pure half, ONE header compiled into the
// plugin (towngen.cpp, the roller block at its end) and the offline suite.
//
// THE RULE (owner decision 1577 c as narrowed by 1579 a, 2026-11-09): while a base's owner is OFFLINE, exactly one teammate's game
// rolls raids for that base, and only while a member of the owner's team (any of the team's player characters) is within
// kPresenceRadius of it. NO CALL-OFF (1579 a): a raid that reaches the base with nobody of the team there waits at the base as in
// single player and starts when a team member - the teammate, or the owner logging in - is near; while it waits or runs the base is
// busy (no new roll for it). With the owner online the roller does not roll (the owner's game rolls as in single player).
//
// WHO ROLLS (design R2): the game IN CHARGE of the base's area (owner rule 1556 - one game per area, the world server's own fact,
// AreaHolderSlotTS) when it is a teammate's game and a team member stands within kPresenceRadius of the base. The owner offline is the
// world server's in-world stage (the PLAYERS roster, StoreRosterSlotInWorld); the team is g_team's copy (TeamSameAnyThread); presence
// is the live character spots on the rolling game (PlayerCharSpots: its own characters and the copies of the others'). No message.
//
// HOW IT ROLLS (design D3 of the read): a separate roll per (raiders' war manager, offline owner), after the engine's own update of
// that war manager: its own 6.0 h timer (the engine's interval, 1.0.65 _DAT_141743a78), busy while a campaign of that war manager
// targets the owner's base, the kinds' own target finder (vtable +0x00) asked for the owner's stand-in faction - its 0x927E90 town
// list given the stand-in and cut to the towns this rule covers - the kinds' own score (vtable +0x08) with the kind's repeat stamp
// (kind+0x108, stamped by the start 0x9C8960 at decomp_9c8960:126-128) swapped for this owner's own, the engine's two draws, and
// the engine's own start 0x9C8960.
namespace raidroll {

const float kPresenceRadius = 2500.0f;   // the announcement's reach (0x355710's camera test; owner 1577's "about 2,500 units")
const double kRollHours = 6.0;           // the engine's war-manager interval (1.0.65 _DAT_141743a78 = 6.0, Confirmed .rdata)
const int kSpotsMax = 256;               // character spots read per roll / arrival

/* THE COVER DECISION for one base (a town of the owner's stand-in) on this game. ownerSlot / mySlot: player slots (-1 unknown);
   sameTeam: this game's player and the owner share a team; ownerInWorld: the roster's word (1 in the world, 0 not, -1 no roster);
   holderSlot: the area holder of the base's area (-1 unknown); teamPresent: team characters within kPresenceRadius. */
enum { kCoverYes = 0, kCoverNoSlot = 1, kCoverNotTeam = 2, kCoverOwnerOnline = 3, kCoverOwnerUnknown = 4, kCoverNotHolder = 5, kCoverNobody = 6 };
inline int Covers(int ownerSlot, int mySlot, int sameTeam, int ownerInWorld, int holderSlot, int teamPresent)
{
    if (ownerSlot < 0 || mySlot < 0 || ownerSlot == mySlot) return kCoverNoSlot;
    if (sameTeam == 0) return kCoverNotTeam;
    if (ownerInWorld == 1) return kCoverOwnerOnline;    /* the owner's own game rolls, as in single player */
    if (ownerInWorld != 0) return kCoverOwnerUnknown;   /* no roster: nobody decides from a guess */
    if (holderSlot != mySlot) return kCoverNotHolder;   /* another game is in charge of the area (or nobody known): it is not this one */
    if (teamPresent <= 0) return kCoverNobody;
    return kCoverYes;
}
inline const char* CoverWhy(int c)
{
    switch (c)
    {
    case kCoverYes: return "covered";
    case kCoverNoSlot: return "no-slot";
    case kCoverNotTeam: return "not-team";
    case kCoverOwnerOnline: return "owner-online";
    case kCoverOwnerUnknown: return "owner-unknown";
    case kCoverNotHolder: return "not-holder";
    case kCoverNobody: return "no-team-member-near";
    }
    return "?";
}

/* a spot counts for the owner's team: a living character whose player is the owner or shares the owner's team (team number not 0) */
inline int SpotIsTeam(int spotSlot, unsigned spotTeam, int dead, int ownerSlot, unsigned ownerTeam)
{
    if (dead != 0 || spotSlot < 0 || ownerSlot < 0) return 0;
    if (spotSlot == ownerSlot) return 1;
    return (ownerTeam != 0 && spotTeam == ownerTeam) ? 1 : 0;
}
/* how many of n spots (xs / zs ground positions, slots, teams, deads - deads may be 0) are the owner's team within r of (x, z) */
inline int TeamPresent(float x, float z, const float* xs, const float* zs, const int* slots, const unsigned* teams, const int* deads, int n,
                       int ownerSlot, unsigned ownerTeam, float r)
{
    if (xs == 0 || zs == 0 || slots == 0 || teams == 0 || n <= 0) return 0;
    int c = 0;
    const double r2 = (double)r * (double)r;
    for (int i = 0; i < n; ++i)
    {
        if (SpotIsTeam(slots[i], teams[i], deads != 0 ? deads[i] : 0, ownerSlot, ownerTeam) == 0) continue;
        const double dx = (double)xs[i] - (double)x, dz = (double)zs[i] - (double)z;
        if (dx * dx + dz * dz <= r2) ++c;
    }
    return c;
}

/* the roll's own timer per (war manager, owner): due when never set (as the engine's unset 0.0) or the world time is past it */
inline int RollDue(int haveTimer, double next, double now)
{
    return (haveTimer == 0 || now > next) ? 1 : 0;
}

/* THE ENGINE'S PICK (decomp_9ca440:224-300): each enabled kind with a target whose chance beats its own draw (draw[i] in 0..1, the
   engine's 0x9B0D40) and is above 0 joins with its chance as weight; one joined = it; several = the first whose running total is
   above r (0x9B0DB0(0, total)), as the engine's map lookup finds it. hasTarget may be 0 (every kind has one). -1 = none joined. */
inline int PickWeighted(const int* hasTarget, const float* chance, const float* draw, int n, float r01, float* totalOut = 0)
{
    double total = 0.0;
    int joined = 0, only = -1;
    for (int i = 0; i < n; ++i)
    {
        if (hasTarget != 0 && hasTarget[i] == 0) continue;
        const float c = chance[i];
        if (!(c == c) || !(draw[i] < c) || !(0.0f < c)) continue;
        total += (double)c; ++joined; only = i;
    }
    if (totalOut != 0) *totalOut = (float)total;
    if (joined == 0) return -1;
    if (joined == 1) return only;
    const double r = (double)r01 * total;
    double run = 0.0;
    int last = -1;
    for (int i = 0; i < n; ++i)
    {
        if (hasTarget != 0 && hasTarget[i] == 0) continue;
        const float c = chance[i];
        if (!(c == c) || !(draw[i] < c) || !(0.0f < c)) continue;
        run += (double)c; last = i;
        if (run > r) return i;
    }
    return last;
}

/* the owner's own repeat stamp after a start: the start wrote `seen` over the `given` stamp - a changed value is the new stamp
   (the campaign got its strength, decomp_9c8960:126-128); unchanged keeps `ours` */
inline double StampAfterStart(double ours, double given, double seen)
{
    return seen != given ? seen : ours;
}

/* A ROLLER CAMPAIGN: one this game's war manager runs against another player's base - its target faction is a stand-in (targetSlot
   >= 0, not this game's own player) and it is not a campaign made here for a raid squad lent by another game (fix A's road) */
inline int IsRollerCampaign(int targetSlot, int mySlot, int borrowed)
{
    return (targetSlot >= 0 && targetSlot != mySlot && borrowed == 0) ? 1 : 0;
}

/* THE ENGINE'S OWN ROLL KEEPS ITS RATE: the engine rolls a war manager only when nothing runs or waits (decomp_9ca440:186). Roller
   campaigns are taken out of its running list for its update only when that alone would stop a due roll: the timer due (wm+0xB0
   unset 0.0 or past - 0x791080's rule), nothing waiting, no other campaign running, at least one roller campaign, and room in the
   list to put them back after a start (cap - others >= ours + 1). */
inline int HideForUpdate(int timerDue, int waiting, int others, int ours, int cap)
{
    if (timerDue == 0 || waiting != 0 || others != 0 || ours <= 0) return 0;
    return (cap - others >= ours + 1) ? 1 : 0;
}
inline int WarTimerDue(double stamp, double now)
{
    return (stamp == 0.0 || now > stamp) ? 1 : 0;
}

/* `raidnow roller <faction name> [kind ..]` (TEST-ONLY): 1 = the rest starts with the word `roller` and a space or tab; *after = the
   text after it (the faction name and any kind choice, raidnow::ParseArgs' form) */
inline int LeverIsRoller(const char* rest, const char** after)
{
    if (rest == 0) return 0;
    const char* p = rest;
    while (*p == ' ' || *p == '\t') ++p;
    if (p[0] != 'r' || p[1] != 'o' || p[2] != 'l' || p[3] != 'l' || p[4] != 'e' || p[5] != 'r') return 0;
    if (p[6] != ' ' && p[6] != '\t') return 0;
    if (after != 0) *after = p + 7;
    return 1;
}

/* REVIEW FOLD 1 (2026-11-09) */
/* H1: a target the finder returned is started only when it is the owner's town AND this rule covers it - the finder's first half
   lists towns from the GIVEN faction's war manager (decomp_9c4fb0:108-168) and asks the lister (where the cut sits) only when that
   list is empty (:186), so the cut alone does not guard every road */
inline int TargetOk(int ownersTown, int cover)
{
    return (ownersTown != 0 && cover == kCoverYes) ? 1 : 0;
}
/* M1: the score's town-level test the engine skips for a target faction without a PlayerInterface (decomp_9c4870:124-145): a town
   (type 0xd) whose level (town+0x388) is below the kind's minimum (kind+0x14) scores 0 */
inline int BelowTownLevel(int townType, int townLevel, int kindMin)
{
    return (townType == 0xd && townLevel < kindMin) ? 1 : 0;
}
/* M2 (i): busy for an owner = any campaign of the war manager whose target is that owner's base - the roller's own AND one made here
   for a raid squad another game lent (the raid of the game that rolled before this one took charge) */
inline int BusyFor(int targetSlot, int ownerSlot)
{
    return (targetSlot >= 0 && targetSlot == ownerSlot) ? 1 : 0;
}

}   /* namespace raidroll */
