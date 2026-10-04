#pragma once
// T-546 step 5: SHARED STANDING - what a team does to its members' standing, on each game. Pure: the plugin (team.cpp,
// relations.cpp, playerstab.cpp) and the offline suite compile this same header. The team's standing itself is ONE record the
// world server keeps (src/common/teamwire.h TeamRec: the team's standing with every NPC faction and the founder's stance towards
// every other player's faction); this header is what a game does with it.
//
// Owner decisions (2026-10-03, .modding/02-project-rules.md 476 / 477; design build/read-t545-t546.md B3):
//   - every member's game writes its own faction to the team's record - on joining, after every world load, and at every new
//     record - as the TEAM's write: sent to the other games with the team reason and never reported as the member's own change;
//   - "for better or for worse": a member's own engine change towards an NPC faction (a crime, a fight) goes to the world server
//     as a DIFFERENCE from the record's value this game last wrote; the world server adds it and every member's game follows;
//   - 476: only the founder CHOOSES the team's stance towards another player's faction (a member's PLAYERS-tab buttons towards a
//     non-member are disabled, showing the team's value); owner 512: the team is one faction, so a member's own engine moving
//     its side towards an outsider (a fight) moves the team's stance too; a stance another player sets towards any member is set
//     by that player's game towards every member, so a hostile stance from either side makes the whole team hostile in effect;
//   - 477: on leaving, removal or a disband BOTH sides go back to before joining: the leaver's NPC standing and its sides towards
//     the others (its own from the pre-join snapshot; a side the snapshot lacks from the side recorded before the pin), and each
//     remaining member's side towards the leaver - in a team that stays (three players or more) that side is the team's stance
//     (476): the founder's side from before the pin, carried by the record the world server sends after the departure.
// C++03 (VS2010 v100).
#include "teamwire.h"
#include "relside.h"
#include "ownrec.h"
#include "nametag.h"
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace swteam {

/* The RELATION message's reason for a write the TEAM made on this game (the record written, a restore). A receiving game writes
   it as any other row of the sender's (quietly, as a snapshot row). */
const unsigned kRelReasonTeam = 3;
const unsigned long kDeltaEveryMs = 1000;     /* a member's own changes are gathered and sent at most once a second */
const unsigned long kRestoreRetryFirstMs = 1000, kRestoreRetryCapMs = 60000;   /* a RESTORE row not written yet: tried again */

/* ------------------------------------------- WHICH ENTRY OF AN NPC STANDING ------------------------------------------- */
/* This game's player faction's standing with an NPC faction X is two entries (relside.h):
   X -> mine - X's own table; the entry THIS game's engine reads for both directions (the player faction's lookup answers "mine
               towards X" with it). The record's back direction (kHaveBack, dir 1).
   mine -> X - the player faction's own table; never read on this game, but the forward sends it and the other games keep it as
               this player's stand-in's own table, which THEIR engines read. The record's forward direction (kHaveFwd, dir 0).
   Every team write (the record written on joining and at each new record - a teammate's change arrives that way - and a
   departure's restore) sets both entries the row holds, X -> mine first; the pre-join snapshot reads both. */
const int kDirNone = -1;
/* the direction of this game's NPC standing that a write moved, from the WRITTEN pair (relside::WrittenPair, which the forward
   resolves before it marks): 0 mine -> X, 1 X -> mine; kDirNone when the pair is not this game's faction with an NPC faction */
inline int NpcDirOfWritten(bool ownerIsMine, bool otherIsMine, bool counterpartIsNpc)
{
    if (!counterpartIsNpc || ownerIsMine == otherIsMine) return kDirNone;
    return ownerIsMine ? 0 : 1;
}
/* the direction a hooked changer call moved, as the call was made (the relations' owner, towards `other`); ownerIsPlayer = the
   owner's relations object is a player faction's (on this game, only this game's own faction) */
template <class F>
inline int NpcDirOfCall(int changer, bool ownerIsPlayer, F owner, F other, F mine, bool counterpartIsNpc)
{
    F wo = owner, wt = other;
    relside::WrittenPair(changer, ownerIsPlayer, owner, other, &wo, &wt);
    return NpcDirOfWritten(wo == mine, wt == mine, counterpartIsNpc);
}
enum { kWriteItMine = 1, kWriteMineIt = 2 };
/* the entries a team write of one record row sets on this game */
inline unsigned NpcEntriesOf(unsigned have)
{
    return ((have & kHaveBack) ? (unsigned)kWriteItMine : 0u) | ((have & kHaveFwd) ? (unsigned)kWriteMineIt : 0u);
}

/* ------------------------------------------------- THE PRE-JOIN SNAPSHOT ------------------------------------------------- */
/* this player's own side towards one other player's faction at the moment of joining; flags = the entry's ally / atWar bits
   (-1 = not recorded: a version 1 snapshot) */
struct PlayerSide { unsigned slot; float rel; int flags; PlayerSide() : slot(0), rel(0.0f), flags(-1) {} };
/* what the accepting game sends with its ACCEPT (the world server keeps it unread and hands it back in the RESTORE row) */
struct PreJoin
{
    coopown::FactionRec npc;            /* the standing with every NPC faction, both directions (the pp.faction record's rows) */
    std::vector<PlayerSide> players;    /* this player's side towards every other player's faction it held then, by slot */
};
const unsigned kPreJoinMagic = 0x4A505753u;   /* "SWPJ" little-endian; a plain pp.faction record starts with its tag's length */
const unsigned kPreJoinVersion = 2;           /* 1: sides without flags; 2: with the entry's flags */
const unsigned kMaxPlayerSides = 4096;
/* {u32 magic, u32 version, blob pp.faction record, u32 n, n x {u32 slot, f32 relation, u32 flags}} */
inline void EncodePreJoin(const PreJoin& p, std::vector<char>* out)
{
    std::vector<char> fac; coopown::EncodeFaction(p.npc, &fac);
    out->clear(); PutU32(out, kPreJoinMagic); PutU32(out, kPreJoinVersion); PutBytes(out, fac.empty() ? "" : &fac[0], fac.size());
    PutU32(out, (unsigned)p.players.size());
    for (size_t i = 0; i < p.players.size(); ++i) { PutU32(out, p.players[i].slot); PutF32(out, p.players[i].rel); PutU32(out, (unsigned)p.players[i].flags); }
}
/* kPjOk; kPjVersion = a snapshot of a version this build does not know (kept, never confirmed as written); kPjMalformed = the
   bytes are broken (they can never be written). A plain pp.faction record (a snapshot kept before the player sides) is kPjOk. */
enum { kPjOk = 0, kPjVersion = 1, kPjMalformed = 2 };
inline int DecodePreJoin(const std::vector<char>& b, PreJoin* p)
{
    PreJoin o;
    Cur c(b.empty() ? 0 : &b[0], b.size());
    if (b.size() >= 4 && GetU32(&c) == kPreJoinMagic)
    {
        const unsigned v = GetU32(&c);
        if (c.bad) return kPjMalformed;
        if (v != 1 && v != 2) return kPjVersion;
        std::vector<char> fac; GetBytes(&c, &fac, kMaxSnapshot);
        if (c.bad || !coopown::DecodeFaction(fac, &o.npc)) return kPjMalformed;
        const unsigned n = GetU32(&c);
        if (c.bad || n > kMaxPlayerSides) return kPjMalformed;
        for (unsigned i = 0; i < n && !c.bad; ++i)
        {
            PlayerSide s; s.slot = GetU32(&c); s.rel = GetF32(&c);
            if (v >= 2) s.flags = (int)(GetU32(&c) & 3u);
            o.players.push_back(s);
        }
        if (c.bad || c.at != b.size()) return kPjMalformed;
        *p = o; return kPjOk;
    }
    if (!coopown::DecodeFaction(b, &o.npc)) return kPjMalformed;
    *p = o; return kPjOk;
}

/* --------------------------------------------------------- ROLES --------------------------------------------------------- */
/* role on this game's copy of the table: -1 in no team, 0 a member, 1 the founder */
inline int RoleOf(const std::vector<WireTeam>& t, unsigned me)
{
    int founder = 0;
    return WireTeamOf(t, me, &founder) < 0 ? -1 : (founder ? 1 : 0);
}
/* 476: this player's own stance towards player `slot` is the founder's to set - a member (not the founder) towards a player
   outside its team */
inline bool StanceIsFounders(int role, bool teammateOrMe) { return role == 0 && !teammateOrMe; }
/* 476, either side: a stance this player (in no team with them) sets towards player `target` is set towards every member of
   target's team too - the slots besides target and this player; empty when target is in no team or shares this player's */
inline std::vector<unsigned> FanOutTargets(const std::vector<WireTeam>& t, unsigned me, unsigned target)
{
    std::vector<unsigned> out;
    const int i = WireTeamOf(t, target, 0);
    if (i < 0 || WireTeamOf(t, me, 0) == i) return out;
    if (t[i].founderSlot != target && t[i].founderSlot != me) out.push_back(t[i].founderSlot);
    for (size_t m = 0; m < t[i].memberSlots.size(); ++m) if (t[i].memberSlots[m] != target && t[i].memberSlots[m] != me) out.push_back(t[i].memberSlots[m]);
    return out;
}

/* ------------------------------------------------------- THE RECORD ------------------------------------------------------- */
/* the founder's game seeds the team's record when the team has none it holds (not seeded) and it has not sent one on this link */
inline bool SeedDue(int role, bool recordHeldSeeded, bool sentThisLink) { return role == 1 && !recordHeldSeeded && !sentThisLink; }
/* the record is written onto this game's faction: owed (a new record, or a world loaded since), the world ready, this game in
   the record's team, and the record seeded */
inline bool RecordWriteDue(bool owed, bool worldReady, bool inThatTeam, bool seeded) { return owed && worldReady && inThatTeam && seeded; }
struct Standing { float rel, trust, trustNeg; Standing() : rel(0.0f), trust(0.0f), trustNeg(0.0f) {} Standing(float r, float t, float n) : rel(r), trust(t), trustNeg(n) {} };
/* one own change of this game's faction (cur, flagsNow) from the value it last wrote from the record or last reported (base,
   flagsBase) - false = nothing moved */
inline bool DeltaOf(const std::string& sid, unsigned dir, const Standing& cur, unsigned flagsNow, const Standing& base, unsigned flagsBase, Delta* out)
{
    if (cur.rel == base.rel && cur.trust == base.trust && cur.trustNeg == base.trustNeg && flagsNow == flagsBase) return false;
    out->sid = sid; out->dir = dir; out->dRel = cur.rel - base.rel; out->dTrust = cur.trust - base.trust; out->dTrustNeg = cur.trustNeg - base.trustNeg;
    out->rel = cur.rel; out->trust = cur.trust; out->trustNeg = cur.trustNeg; out->flags = flagsNow;
    return true;
}
/* an old record is not written over this game's faction while this game's own changes since it last wrote one could not be
   sent first (no link): with a base those changes would be lost; without one (a world just loaded) the record is written */
inline bool RecordWriteSafe(bool linked, bool baseValid) { return linked || !baseValid; }
/* this game's own changes are gathered with a base to measure them from (the record written here, or - on the founder's game
   until its first record - the SEED it sent) and a link to send them on */
inline bool GatherDue(bool baseValid, bool linked, bool recordHeldOrSeeded) { return baseValid && linked && recordHeldOrSeeded; }
/* the players outside the team the founder holds a side towards that the record has no stance for yet (a faction that appeared
   after the SEED): sent as STANCEs. A player in `waiting` is not: one who has just left the team (DepartedMates) - the record the
   world server sends after the departure carries the team's stance towards them (teamwire.h StanceBackOnDeparture) - and one a
   held RESTORE row still puts this player's side back towards (HeldSideSlots) */
inline std::vector<unsigned> StancesMissing(const std::vector<PlayerSide>& mine, const std::vector<RecStance>& rec, const std::vector<unsigned>& mates, unsigned me,
                                            const std::vector<unsigned>& waiting)
{
    std::vector<unsigned> out;
    for (size_t i = 0; i < mine.size(); ++i)
    {
        const unsigned s = mine[i].slot;
        bool skip = s == me;
        for (size_t k = 0; !skip && k < mates.size(); ++k) if (mates[k] == s) skip = true;
        for (size_t k = 0; !skip && k < waiting.size(); ++k) if (waiting[k] == s) skip = true;
        for (size_t k = 0; !skip && k < rec.size(); ++k) if (rec[k].slot == s) skip = true;
        if (!skip) out.push_back(s);
    }
    return out;
}
/* the players who were this player's teammates on the last table and are not on this one (left, removed, or the team ended):
   the founder's game waits with their stance until the next record (StancesMissing's `waiting`); ascending, once each */
inline std::vector<unsigned> DepartedMates(const std::vector<unsigned>& before, const std::vector<unsigned>& now)
{
    std::vector<unsigned> out;
    for (size_t i = 0; i < before.size(); ++i)
    {
        bool still = false;
        for (size_t k = 0; !still && k < now.size(); ++k) if (now[k] == before[i]) still = true;
        if (!still) out.push_back(before[i]);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
/* owner 512: the team is one faction - a side towards a player outside the team moved on ANY member's game (the founder's choice,
   or a member's own engine: a fight) becomes the team's stance and every member's game follows it */
inline bool SideMoveIsTeamStance(int role) { return role == 0 || role == 1; }
/* 477: the side a member reports towards a new teammate is the one its FIRST pin write moved (never a side already at the pin,
   which after a reload is the pin's own value) */
inline bool SideReportDue(int pinWrite, bool reportedThisPin) { return pinWrite == 1 && !reportedThisPin; }
/* a side written below the ally line has the entry's ally flag cleared first (the flag answers ally whatever the value) */
inline bool FlagClearedFor(float rel, bool flagSet) { return flagSet && rel < nametag::kAllyAt; }
/* another player's side towards THIS team - the worst of its sides towards each member read here (this player's included); a
   level not read does not count (nametag::WorseLevel) */
inline int TheirSideTowardsTeam(const std::vector<int>& levels)
{
    int w = nametag::kUnknown;
    for (size_t i = 0; i < levels.size(); ++i)
    {
        if (levels[i] < nametag::kFriendly || levels[i] > nametag::kHostile) continue;
        w = (w == nametag::kUnknown) ? levels[i] : nametag::WorseLevel(w, levels[i]);
    }
    return w;
}

/* --------------------------------------------------------- RESTORE --------------------------------------------------------- */
/* a held RESTORE row is written when this game's world is ready (loaded, its load's own restore done) and it holds a table from
   this link (so the teammates it skips are today's: the departure's table reached it first) */
inline bool RestoreDue(bool worldReady, bool tableFromThisLink) { return worldReady && tableFromThisLink; }
/* the sides a row puts back: the snapshot's (this player's own sides at the moment of joining), then the row's recorded sides
   (each as it stood before the pin, kept by the world server) towards players the snapshot does not name. The snapshot wins for
   every player it holds: a side towards a player who joined the team later was written from the team's record before its pin
   moved it, so the side recorded there is the founder's stance, not this player's own */
inline std::vector<PlayerSide> MergeSides(const std::vector<PlayerSide>& snap, const std::vector<RecStance>& row)
{
    std::vector<PlayerSide> out = snap;
    for (size_t i = 0; i < row.size(); ++i)
    {
        size_t k = 0;
        while (k < snap.size() && snap[k].slot != row[i].slot) ++k;
        if (k < snap.size()) continue;
        PlayerSide s; s.slot = row[i].slot; s.rel = row[i].rel; s.flags = (int)(row[i].flags & 3u);
        size_t o = 0;
        while (o < out.size() && out[o].slot != s.slot) ++o;
        if (o < out.size()) out[o] = s; else out.push_back(s);
    }
    return out;
}
/* the sides to put back: every one except this player and the players it shares a team with NOW (held at ally while it does);
   *skipped = those left out because they are teammates now */
inline std::vector<PlayerSide> RestoreSides(const std::vector<PlayerSide>& sides, unsigned me, const std::vector<unsigned>& teammatesNow, std::vector<unsigned>* skipped)
{
    std::vector<PlayerSide> out;
    for (size_t i = 0; i < sides.size(); ++i)
    {
        const unsigned s = sides[i].slot;
        if (s == me) continue;
        bool mate = false;
        for (size_t k = 0; k < teammatesNow.size(); ++k) if (teammatesNow[k] == s) mate = true;
        if (mate) { if (skipped != 0) skipped->push_back(s); continue; }
        out.push_back(sides[i]);
    }
    return out;
}
/* the players the held RESTORE rows put this player's side back towards (each row's recorded sides, and its snapshot's when this
   build reads it), ascending, once each: the founder's stance fill waits for these players only (StancesMissing's `waiting`) -
   a row this build cannot read holds back no other player */
inline std::vector<unsigned> HeldSideSlots(const std::vector<Down>& held)
{
    std::vector<unsigned> out;
    for (size_t i = 0; i < held.size(); ++i)
    {
        for (size_t k = 0; k < held[i].sides.size(); ++k) out.push_back(held[i].sides[k].slot);
        PreJoin pj;
        if (!held[i].snap.empty() && DecodePreJoin(held[i].snap, &pj) == kPjOk)
            for (size_t k = 0; k < pj.players.size(); ++k) out.push_back(pj.players[k].slot);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
/* a row's side towards a player whose side this player changed itself after this game learned of the departure - an own change
   numbered after the pin towards that player ended here, or after the row arrived, whichever came first - is not written: the
   newer own change stands. Numbers count this game's own side changes (0 = none). */
inline bool OwnSideStands(unsigned long long ownNo, bool pinEndKnown, unsigned long long pinEndNo, unsigned long long arrivedNo)
{
    const unsigned long long since = (pinEndKnown && pinEndNo < arrivedNo) ? pinEndNo : arrivedNo;
    return ownNo != 0 && ownNo > since;
}
/* a departure row's progress on this game: its NPC standing is written ONCE (a retry never writes it again - this player's own
   changes after it stand) and each side once */
struct RestoreProgress { bool npcDone; std::vector<unsigned> sidesDone; RestoreProgress() : npcDone(false) {} };
inline bool SideDone(const RestoreProgress& p, unsigned slot)
{
    for (size_t i = 0; i < p.sidesDone.size(); ++i) if (p.sidesDone[i] == slot) return true;
    return false;
}
/* what one side's write means for its row: written; nothing to write (-1: this game's faction list holds no faction of that
   player - there is no side here to put back); or tried again (any other failure: no world now, an engine address missing, the
   entry would not read, or the engine's setter raised) */
enum { kSideWritten = 0, kSideNothing = 1, kSideRetry = 2 };
inline int SideOutcome(int writeResult) { return writeResult == 1 ? kSideWritten : writeResult == -1 ? kSideNothing : kSideRetry; }
/* the wait before a row not written yet is tried again: doubling from the first wait, never past the cap */
inline unsigned long RestoreRetryMs(long long fails)
{
    unsigned long ms = kRestoreRetryFirstMs;
    for (long long i = 1; i < fails && ms < kRestoreRetryCapMs; ++i) ms *= 2;
    return ms > kRestoreRetryCapMs ? kRestoreRetryCapMs : ms;
}
/* a held row whose standing was written is answered (RESTORE_DONE) and remembered; one that was not stays held */
inline bool InboxAnswerOne(Inbox* in, unsigned no)
{
    for (size_t i = 0; i < in->held.size(); ++i)
        if (in->held[i].no == no) { in->held.erase(in->held.begin() + i); InboxMarkDone(in, no); return true; }
    return false;
}

}   // namespace swteam
