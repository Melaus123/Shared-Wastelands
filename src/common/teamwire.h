#pragma once
// T-546 step 3 (t546a): PLAYER FACTIONS - WHO IS IN WHOSE FACTION. Pure: the world server (src/coop-store/store_main.cpp),
// the plugin (src/coop-plugin/team.cpp) and the offline suite (src/coop-test) compile this same header.
//
// WHAT A "FACTION" IS HERE (owner, design B): characters never move between engine factions. A team is a MEMBERSHIP the world
// server keeps: one founder, the members who accepted the founder's invitation, and the joined faction's name (the founder's
// faction name at the moment the team was made). The world server is the only writer; every game receives the whole table at
// its WELCOME and on every change, as it receives the player list.
//
// KEYS: a player is the stable PROFILE id (coopprof::ProfileIdOfKey of the id the HELLO carries), never the per-session seat.
// On the wire players are named by their SLOT, which the world server keeps per player for the life of the world (slots.txt).
//
// THE PRE-JOIN STANDING: the accepting game sends, with its ACCEPT, its own faction's standing with every NPC faction as it is
// at that moment - the pp.faction record's encoding (src/common/ownrec.h EncodeFaction, read by relations.cpp
// RelationsOwnRecord). The world server keeps it with the membership, unread, and hands it back to that player in a RESTORE
// row when the player leaves, is removed or the team is disbanded. A founder who made the team has none; a member who becomes
// the founder (a deleted founder's role passes to them) keeps theirs, and gets it back in a RESTORE row when, as the founder
// alone, they leave (the team ends) or when they disband.
//
// RESTORE ROWS ("owed"): every departure makes one row for the departing player {number, reason, team name, snapshot}. It is
// sent at once when that player's game is connected, and at its next WELCOME when it is not (removal while away); the world
// server keeps the row until that game answers RESTORE_DONE with the row's number, and sends it once per connection of that
// game. At most kMaxOwedEach rows wait for one player (kMaxOwed in the world): an invitation, an accept, a leave, a removal or a
// disband that would make one more is refused (restore-backlog). A game holds a received row until its world is loaded, answers
// it then, and treats a row number it has answered as done when the row comes again (Inbox, below).
//
// RULES (owner 2026-10-03 and the industry standard decided in build/read-t545-t546.md): an invitation goes only to a
// connected player who is in no team and has no other invitation waiting; it lasts kInviteSeconds; only the founder (or a
// player in no team, who becomes the founder when the first invitation is accepted) invites and removes; a founder cannot
// leave while the team has members - they disband; one team at a time. A team stays when its last member goes (the founder
// alone); the founder alone may leave, which ends it. No ranks.
//
// WIRE: TEAM (60), both ways, little-endian as every world-server message: {u8 kind, then the kind's fields}.
//   up   1 INVITE {u32 slot, str factionName}       2 ANSWER {u8 accept, u32 snapshotRows, blob snapshot}
//        3 LEAVE {}   4 REMOVE {u32 slot}   5 DISBAND {}   6 RESTORE_DONE {u32 number}
//        7 ASK {}   (the whole table again: this game's copy was cleared - a world torn down - while the link stayed)
//   down 20 TABLE {u32 n, n x [u32 team, u32 founderSlot, str name, u32 m, m x u32 memberSlot]}
//        21 INVITED {u32 fromSlot, str teamName, u32 secondsLeft}   (secondsLeft 0 = the invitation is gone)
//        22 NOTICE {u8 event, u8 result, u32 actorSlot, u32 subjectSlot, str teamName}
//        23 RESTORE {u32 number, u8 reason, u8 whileAway, str teamName, u32 snapshotRows, blob snapshot}
//   str = {u32 len, bytes}; blob = {u32 len, bytes}.
// FILE (teams.txt in the world's folder, rewritten whole through a temp file and a write-through rename, only when its text
// changes; a change that cannot be written is refused - not-saved): hex for every string and blob -
//   N <nextTeam> <nextOwed>
//   T <team> <hex founder> <hex name>
//   F <team> <snapshotRows> <hex snapshot>            (only for a founder who holds a pre-join standing; after its T line)
//   M <team> <hex member> <joinedUnix> <snapshotRows> <hex snapshot>
//   O <number> <hex player> <reason> <whileAway> <hex teamName> <snapshotRows> <hex snapshot> <madeUnix> [<hex sides>]
//   R <team> <hex record>                             (the team's standing record, after its T line)
//   S <team> <hex owner> <ownerSlot> <hex other> <otherSlot> <relationBits> <flags>   (a member's side before the pin)
//
// THE TEAM'S STANDING (T-546 step 5; owner 476 / 477): ONE record per team, kept here - the team's standing with every NPC
// faction (both directions: relation, trust, trustNeg and the ally / atWar flags) and the founder's stance towards every other
// player's faction (by slot). The founder's game SEEDS it once (its own standing, when the team has none); a member's own engine
// change towards an NPC faction comes as a DELTA, added here (the relation kept on the engine's scale); a side towards another
// player moved on a member's game (the founder's choice, or any member's own engine - owner 512) comes as a STANCE. Every change is saved before the new record goes to every connected member (RECORD),
// and a member gets it at its WELCOME: each member's game writes its own faction to it. A member's game reports, when it first
// holds a new teammate at ally, its side towards that teammate as it stood before (SIDE, first report kept): on a departure the
// sides go back on BOTH sides - the leaver's own towards each remaining member, and each remaining member's towards the leaver
// (RESTORE rows carrying those sides beside the pre-join snapshot). A team that stays after a departure (three players or more)
// takes the founder's side towards the leaver as it stood before the pin as its stance towards the leaver
// (StanceBackOnDeparture), and its record goes to every connected member after the departure's RESTORE rows. That stance is
// every remaining member's side towards the leaver (476), written from the record, so the remaining members get no side rows
// when the record carries it (MemberOut).
//   up   8 SEED {blob record}   9 DELTA {u32 n, n x [str sid, u8 dir, f32 dRel, f32 dTrust, f32 dTrustNeg, f32 rel, f32 trust,
//        f32 trustNeg, u32 flags]}   10 STANCE {u32 slot, f32 rel, u32 flags}   11 SIDE {u32 otherSlot, f32 rel, u32 flags}
//   down 24 RECORD {u32 team, blob record}; RESTORE gains {blob sides} after its snapshot
//   record = {u32 gen, u8 seeded, u32 n, n x [str sid, f32 rel, trust, trustNeg, relBack, trustBack, trustNegBack, u32 flags,
//            u32 flagsBack, u8 have], u32 m, m x [u32 slot, f32 rel, u32 flags]};  sides = {u32 n, n x [u32 slot, f32 rel, u32 flags]}
//
// THE TEAM'S RESEARCH (T-546 step 6; owner rule 2026-09-26: research is shared on joining, each keeps what they have on leaving):
// every tech any member has finished, kept here per team (sorted, unique; teams.txt Q lines). A member's game sends its WHOLE
// finished list once per world load and link while it is in a team (and when it enters one), and each tech its own player
// finishes afterwards (OWN). The world server adds what the team lacks, saves, and answers the sender: a WHOLE list with the
// team's whole research (bySlot = the sender; its game loads every tech it lacks once, quietly, through the engine's
// Research::load), an OWN list with the techs it carried (bySlot = the sender); and sends every other connected member, when the
// union grew, the whole research again after a WHOLE list (bySlot 0xFFFFFFFF), or the new techs with the finisher's slot after an
// OWN one (each applied by the road the member's queue allows). A list it refuses (not in a team, too many techs, teams.txt not
// saved) is answered with a NOTICE research-whole / research-own carrying the reason, actor and subject the sender; the game
// keeps the list and sends it again after a growing wait until it is accepted. Nothing is ever taken away: a member who leaves
// keeps what their game holds, and the team keeps the techs it was given. src/common/teamresearch.h holds the game's decisions.
//   up   12 RESEARCH {u8 whole, u32 n, n x str sid}
//   down 25 RESEARCH {u32 team, u8 whole, u32 bySlot, u32 n, n x str sid}   (bySlot: the game a reply answers, or the finisher of
//                                                                           an OWN tech; 0xFFFFFFFF = a whole research for all)
//   file Q <team> <hex sid>                                                 (after its team's T line)
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <algorithm>

namespace swteam {

const unsigned kMsgTeam = 60;             // the world-server message number
const unsigned kInviteSeconds = 60;       // an invitation's life
const unsigned kDeclineWaitSeconds = 60;  // after a DECLINE, the same inviter's next invitation to that player is refused this long
const unsigned kMaxMembers = 15;          // members besides the founder (profilecap is at most 16 players)
const unsigned kMaxTeams = 64;
const unsigned kMaxName = 256;            // bytes of a faction name
const unsigned kMaxSnapshot = 64u * 1024u;   // bytes of a pre-join standing (about 100 factions measure ~6 KB)
const unsigned kMaxOwed = 256;            // RESTORE rows waiting in the whole world
const unsigned kMaxOwedEach = 8;          // RESTORE rows waiting for one player
const unsigned kInboxDoneKept = 64;       // row numbers a game remembers as answered
const unsigned kMaxRecRows = 4096;        // NPC factions in a team record
const unsigned kMaxStances = 256;         // other players in a team record
const unsigned kMaxDeltas = 512;          // changes in one DELTA
const unsigned kMaxSides = 256;           // recorded sides in one team / one RESTORE row
const float kRecLow = -100.0f, kRecHigh = 100.0f;   // the engine's standing scale

enum { kUpInvite = 1, kUpAnswer = 2, kUpLeave = 3, kUpRemove = 4, kUpDisband = 5, kUpRestoreDone = 6, kUpAsk = 7,
       kUpSeed = 8, kUpDelta = 9, kUpStance = 10, kUpSide = 11, kUpResearch = 12 };
enum { kDnTable = 20, kDnInvited = 21, kDnNotice = 22, kDnRestore = 23, kDnRecord = 24, kDnResearch = 25 };
const unsigned kMaxTechs = 4096;          // techs in a team's research and in one RESEARCH message
/* results: 0 = done; every other number is the reason a request was refused */
enum { kOk = 0, kRefNotOnline = 1, kRefInTeam = 2, kRefNotFounder = 3, kRefSelf = 4, kRefInvitePending = 5, kRefNoInvite = 6,
       kRefFounderLeaves = 7, kRefNotInTeam = 8, kRefNotMember = 9, kRefUnknownPlayer = 10, kRefTeamFull = 11, kRefNoSnapshot = 12,
       kRefTooMany = 13, kRefOwedFull = 14, kRefNotSaved = 15, kRefSeeded = 16, kRefNoRecord = 17, kRefDeclined = 18, kRefLast = 18 };
/* NOTICE events: the request a refusal answers (kEvInvite..kEvDisband with a result), or what happened (with kOk) */
enum { kEvInvite = 1, kEvAnswer = 2, kEvLeave = 3, kEvRemove = 4, kEvDisband = 5, kEvJoined = 6, kEvDeclined = 7, kEvExpired = 8,
       kEvLeft = 9, kEvRemoved = 10, kEvDisbanded = 11, kEvInviteGone = 12, kEvResearchWhole = 13, kEvResearchOwn = 14, kEvLast = 14 };
/* why a RESTORE row was made */
enum { kWhyLeft = 1, kWhyRemoved = 2, kWhyDisbanded = 3 };
/* what BookParseLine made of a line */
enum { kLineOk = 0, kLineBad = 1, kLineDuplicate = 2 };

inline const char* ResultName(int r)
{
    static const char* const n[] = { "ok", "not-online", "in-a-team", "not-founder", "self", "invite-pending", "no-invite",
        "founder-has-members", "not-in-a-team", "not-a-member", "unknown-player", "team-full", "no-snapshot", "too-many",
        "restore-backlog", "not-saved", "already-seeded", "no-record", "declined-recently" };
    return (r >= 0 && r <= kRefLast) ? n[r] : "?";
}
inline const char* EventName(int e)
{
    static const char* const n[] = { "?", "invite", "answer", "leave", "remove", "disband", "joined", "declined", "expired",
        "left", "removed", "disbanded", "invite-gone", "research-whole", "research-own" };
    return (e >= 1 && e <= kEvLast) ? n[e] : "?";
}
inline const char* WhyName(int w) { return w == kWhyLeft ? "left" : w == kWhyRemoved ? "removed" : w == kWhyDisbanded ? "disbanded" : "?"; }

/* ------------------------------------------------ the team's standing record ------------------------------------------------ */
enum { kHaveFwd = 1, kHaveBack = 2 };   /* RecRow.have: which directions the record holds (team -> it, it -> team) */
struct RecRow
{
    std::string sid;                 // the NPC faction's stringID
    float rel, trust, trustNeg;      // the team -> that faction
    float relBack, trustBack, trustNegBack;   // that faction -> the team
    unsigned flags, flagsBack;       // ally (1) / atWar (2) bits of each direction
    unsigned have;                   // kHaveFwd | kHaveBack
    RecRow() : rel(0), trust(0), trustNeg(0), relBack(0), trustBack(0), trustNegBack(0), flags(0), flagsBack(0), have(0) {}
};
struct RecStance { unsigned slot; float rel; unsigned flags; RecStance() : slot(0), rel(0.0f), flags(0) {} };   // a side towards a player
struct TeamRec
{
    unsigned gen;                    // raised at every change
    int seeded;                      // 0 = the founder's game has not given the team its standing yet
    std::vector<RecRow> rows;
    std::vector<RecStance> stances;  // the founder's stance towards other players' factions
    TeamRec() : gen(0), seeded(0) {}
};
/* a member's side towards a teammate as it stood before the pin (owner -> other), first report kept */
struct SideSnap { std::string owner, other; unsigned ownerSlot, otherSlot; float rel; unsigned flags; SideSnap() : ownerSlot(0), otherSlot(0), rel(0.0f), flags(0) {} };
/* one change a member's own engine made: dir bit 0 = the direction (0 mine -> that faction, 1 that faction -> mine), bit 1
   (kDirAbsolute) = the member had no value of the record's for that pair, so its value now is sent to be taken as it is; d* =
   the change, rel.. = the member's value now (taken when the record has no value in that direction yet, or kDirAbsolute) */
const unsigned kDirAbsolute = 2;
struct Delta
{
    std::string sid; unsigned dir; float dRel, dTrust, dTrustNeg, rel, trust, trustNeg; unsigned flags;
    Delta() : dir(0), dRel(0), dTrust(0), dTrustNeg(0), rel(0), trust(0), trustNeg(0), flags(0) {}
};

/* ------------------------------------------------ the table (world server) ------------------------------------------------ */
struct Member
{
    std::string id;                  // profile id
    long long joinedAt;              // unix seconds
    unsigned snapRows;               // rows in the pre-join standing (for the log; the blob is kept unread)
    std::vector<char> snap;          // the pre-join standing, as the accepting game sent it
    Member() : joinedAt(0), snapRows(0) {}
};
struct Team
{
    unsigned no;
    std::string founder;             // profile id
    std::string name;                // the joined faction's name
    std::vector<Member> members;     // never the founder
    unsigned founderSnapRows;        // the founder's own pre-join standing: kept when a member became the founder (a deleted
    std::vector<char> founderSnap;   // founder's role passed to them); empty = a founder who made the team, who has none
    TeamRec rec;                     // the team's standing (step 5)
    std::vector<SideSnap> sides;     // members' sides towards teammates as they stood before the pin
    std::vector<std::string> research;   // every tech any member has finished (stringIDs, sorted, unique)
    Team() : no(0), founderSnapRows(0) {}
};
struct Owed
{
    unsigned no;
    std::string id;                  // the player the row is for
    int why;                         // kWhyLeft / kWhyRemoved / kWhyDisbanded
    int away;                        // 1 = that player was not connected when the row was made
    std::string teamName;
    unsigned snapRows;
    std::vector<char> snap;
    long long madeAt;
    unsigned long long sentConn;     // the world server's serial of the connection this row was last sent on (0 = none); not in the file
    std::vector<RecStance> sides;    // this player's sides to put back towards those players (they override the snapshot's)
    Owed() : no(0), why(0), away(0), snapRows(0), madeAt(0), sentConn(0) {}
};
struct Invite
{
    std::string from, to;            // profile ids
    std::string name;                // the team name the invitee is asked to join
    double expiresAt;                // the world server's own seconds clock
    Invite() : expiresAt(0.0) {}
};
struct Book
{
    std::vector<Team> teams;
    std::vector<Owed> owed;
    std::vector<Invite> invites;     // never written to the file: an invitation is for connected players only
    std::vector<Invite> declined;    // memory only: a DECLINE's inviter and invitee, expiresAt = the decline + kDeclineWaitSeconds
    std::vector<unsigned> loadRefused;   // while teams.txt is read: team numbers whose T line was refused (their M lines are too)
    unsigned nextTeam, nextOwed;
    Book() : nextTeam(1), nextOwed(1) {}
};

/* the team a player is in (as founder or member): its index, or -1 */
inline int TeamOf(const Book& b, const std::string& id)
{
    for (size_t t = 0; t < b.teams.size(); ++t)
    {
        if (b.teams[t].founder == id) return (int)t;
        for (size_t m = 0; m < b.teams[t].members.size(); ++m) if (b.teams[t].members[m].id == id) return (int)t;
    }
    return -1;
}
inline bool IsFounder(const Book& b, const std::string& id) { const int t = TeamOf(b, id); return t >= 0 && b.teams[t].founder == id; }
inline int InviteTo(const Book& b, const std::string& to)
{
    for (size_t i = 0; i < b.invites.size(); ++i) if (b.invites[i].to == to) return (int)i;
    return -1;
}
/* the RESTORE rows waiting for a player */
inline unsigned OwedCount(const Book& b, const std::string& id)
{
    unsigned n = 0;
    for (size_t i = 0; i < b.owed.size(); ++i) if (b.owed[i].id == id) ++n;
    return n;
}
/* can one more RESTORE row be kept for this player */
inline bool OwedRoom(const Book& b, const std::string& id) { return OwedCount(b, id) < kMaxOwedEach && b.owed.size() < kMaxOwed; }
/* a refused ANSWER: true = the invitation is gone on the world server (the answering game forgets it too); a missing standing
   or a change that could not be saved leaves it waiting, so the game may answer again */
inline bool AnswerRefusalEndsInvite(int r) { return r != kOk && r != kRefNoSnapshot && r != kRefNotSaved; }

/* INVITE: `from` asks `to` (connected = toOnline) to join. The team name is the founder's team's when `from` founds one, else
   `factionName` (the inviter's own faction name, sent with the INVITE). Within kDeclineWaitSeconds of `to` declining `from`'s
   invitation, `from`'s next invitation to `to` is refused (declined-recently). */
inline int InviteDecide(Book* b, const std::string& from, const std::string& to, bool toKnown, bool toOnline,
                        const std::string& factionName, double now)
{
    if (!toKnown) return kRefUnknownPlayer;
    if (from == to) return kRefSelf;
    if (!toOnline) return kRefNotOnline;
    if (TeamOf(*b, to) >= 0) return kRefInTeam;
    if (OwedCount(*b, to) >= kMaxOwedEach) return kRefOwedFull;   /* the row its departure makes could not be kept */
    const int t = TeamOf(*b, from);
    if (t >= 0 && b->teams[t].founder != from) return kRefNotFounder;
    if (t >= 0 && b->teams[t].members.size() >= kMaxMembers) return kRefTeamFull;
    if (t < 0 && b->teams.size() >= kMaxTeams) return kRefTooMany;
    if (InviteTo(*b, to) >= 0) return kRefInvitePending;
    for (size_t i = 0; i < b->declined.size(); ++i)
        if (b->declined[i].from == from && b->declined[i].to == to && b->declined[i].expiresAt > now) return kRefDeclined;
    Invite v; v.from = from; v.to = to; v.expiresAt = now + (double)kInviteSeconds;
    v.name = t >= 0 ? b->teams[t].name : factionName.substr(0, kMaxName);
    b->invites.push_back(v);
    return kOk;
}

/* ANSWER: `to` accepts or declines its waiting invitation (a DECLINE starts the inviter's wait, kDeclineWaitSeconds). *used =
   that invitation (its inviter is who hears the answer);
   *teamNo and *teamName = the team joined (its own name, which the invitation's may not be). An ACCEPT whose inviter has meanwhile
   become another founder's member is refused (not-founder). AnswerRefusalEndsInvite says which refusals end the invitation. */
inline int AnswerDecide(Book* b, const std::string& to, bool accept, unsigned snapRows, const std::vector<char>& snap,
                        long long nowUnix, double now, Invite* used, unsigned* teamNo, std::string* teamName = 0)
{
    const int i = InviteTo(*b, to);
    if (i < 0 || b->invites[i].expiresAt <= now) return kRefNoInvite;
    const Invite v = b->invites[i];
    if (used != 0) *used = v;
    if (!accept)
    {
        b->invites.erase(b->invites.begin() + i);
        for (size_t k = 0; k < b->declined.size(); ) { if (b->declined[k].from == v.from && b->declined[k].to == to) b->declined.erase(b->declined.begin() + k); else ++k; }
        Invite d = v; d.expiresAt = now + (double)kDeclineWaitSeconds;
        b->declined.push_back(d);
        return kOk;
    }
    if (snap.empty() || snap.size() > kMaxSnapshot) return kRefNoSnapshot;   /* the invitation stays: the game may answer again */
    b->invites.erase(b->invites.begin() + i);
    if (TeamOf(*b, to) >= 0) return kRefInTeam;
    if (OwedCount(*b, to) >= kMaxOwedEach) return kRefOwedFull;
    int t = TeamOf(*b, v.from);
    if (t >= 0 && b->teams[t].founder != v.from) return kRefNotFounder;
    if (t >= 0 && b->teams[t].members.size() >= kMaxMembers) return kRefTeamFull;
    if (t < 0)
    {
        if (b->teams.size() >= kMaxTeams) return kRefTooMany;
        Team n; n.no = b->nextTeam++; n.founder = v.from; n.name = v.name;
        b->teams.push_back(n); t = (int)b->teams.size() - 1;
    }
    Member m; m.id = to; m.joinedAt = nowUnix; m.snapRows = snapRows; m.snap = snap;
    b->teams[t].members.push_back(m);
    if (teamNo != 0) *teamNo = b->teams[t].no;
    if (teamName != 0) *teamName = b->teams[t].name;
    return kOk;
}

/* the recorded sides `owner` holds towards `towards` ("" = towards anyone), as the sides a RESTORE row puts back */
inline std::vector<RecStance> SidesOwnedBy(const Team& t, const std::string& owner, const std::string& towards)
{
    std::vector<RecStance> out;
    for (size_t i = 0; i < t.sides.size(); ++i)
        if (t.sides[i].owner == owner && (towards.empty() || t.sides[i].other == towards))
        { RecStance r; r.slot = t.sides[i].otherSlot; r.rel = t.sides[i].rel; r.flags = t.sides[i].flags; out.push_back(r); }
    return out;
}
/* every recorded side with `id` on either end, dropped */
inline void DropSidesOf(Team* t, const std::string& id)
{
    for (size_t i = 0; i < t->sides.size(); ) { if (t->sides[i].owner == id || t->sides[i].other == id) t->sides.erase(t->sides.begin() + i); else ++i; }
}
/* the players left in team t besides `gone` that hold a recorded side towards it (one RESTORE row each when it departs) */
inline std::vector<std::string> SideOwnersTowards(const Team& t, const std::string& gone)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < t.sides.size(); ++i)
    {
        if (t.sides[i].other != gone || t.sides[i].owner == gone) continue;
        bool seen = false;
        for (size_t k = 0; k < out.size(); ++k) if (out[k] == t.sides[i].owner) seen = true;
        if (!seen) out.push_back(t.sides[i].owner);
    }
    return out;
}
inline bool StanceBackOnDeparture(Team* t, const std::string& gone);
inline bool RecordCarriesDeparture(const Team& t, const std::string& gone);
/* one member out of team t (477, both sides): its RESTORE row (its pre-join snapshot and its recorded sides towards the others)
   appended to b->owed and copied to *made, and one row for each remaining player that holds a recorded side towards it (that
   side only; appended to *sideRows; away = that player not among onlineIds) - except a player whose own rows are full: its side
   row is not made (named in *skipped), so one player's backlog never stops another's departure. The team stays, and its stance
   towards the leaver is the founder's side from before the pin (StanceBackOnDeparture). When another member stays with the
   founder and the record carries that stance (RecordCarriesDeparture), no side rows are made: the record the world server sends
   after the departure writes the team's stance as every remaining member's side towards the leaver (476). false = no room
   for the leaver's own row, or the world's rows full (OwedRoom, kMaxOwed) - nothing changed and no number used; the callers
   refuse before that (restore-backlog). */
inline bool MemberOut(Book* b, int t, size_t m, int why, bool away, long long nowUnix, Owed* made, std::vector<Owed>* sideRows = 0,
                      const std::vector<std::string>* onlineIds = 0, std::vector<std::string>* skipped = 0)
{
    Team& team = b->teams[t];
    const std::string id = team.members[m].id;
    bool byRecord = false;
    if (team.members.size() > 1) { Team probe = team; StanceBackOnDeparture(&probe, id); byRecord = RecordCarriesDeparture(probe, id); }
    std::vector<std::string> owners;
    if (!byRecord)
    {
        const std::vector<std::string> all = SideOwnersTowards(team, id);
        for (size_t k = 0; k < all.size(); ++k) { if (OwedCount(*b, all[k]) >= kMaxOwedEach) { if (skipped != 0) skipped->push_back(all[k]); } else owners.push_back(all[k]); }
    }
    if (!OwedRoom(*b, id) || b->owed.size() + 1 + owners.size() > kMaxOwed) return false;
    Owed o; o.no = b->nextOwed++; o.id = id; o.why = why; o.away = away ? 1 : 0;
    o.teamName = team.name; o.snapRows = team.members[m].snapRows; o.snap = team.members[m].snap; o.madeAt = nowUnix;
    o.sides = SidesOwnedBy(team, id, std::string());
    b->owed.push_back(o);
    if (made != 0) *made = o;
    for (size_t k = 0; k < owners.size(); ++k)
    {
        bool on = onlineIds == 0;
        for (size_t q = 0; onlineIds != 0 && q < onlineIds->size(); ++q) if ((*onlineIds)[q] == owners[k]) on = true;
        Owed r; r.no = b->nextOwed++; r.id = owners[k]; r.why = why; r.away = on ? 0 : 1; r.teamName = team.name; r.madeAt = nowUnix;
        r.sides = SidesOwnedBy(team, owners[k], id);
        b->owed.push_back(r);
        if (sideRows != 0) sideRows->push_back(r);
    }
    StanceBackOnDeparture(&team, id);
    DropSidesOf(&team, id);
    team.members.erase(team.members.begin() + m);
    return true;
}
/* the founder of team t out (the team is ending): the RESTORE row of the founder's own pre-join standing and recorded sides made
   (appended to b->owed and copied to *made; away = the founder is not connected), when the founder holds either. false = no room
   for the row - nothing changed. */
inline bool FounderOut(Book* b, int t, int why, long long nowUnix, Owed* made, bool away = false)
{
    const Team& team = b->teams[t];
    const std::vector<RecStance> sides = SidesOwnedBy(team, team.founder, std::string());
    if (team.founderSnap.empty() && sides.empty()) return true;
    if (!OwedRoom(*b, team.founder)) return false;
    Owed o; o.no = b->nextOwed++; o.id = team.founder; o.why = why; o.away = away ? 1 : 0;
    o.teamName = team.name; o.snapRows = team.founderSnapRows; o.snap = team.founderSnap; o.madeAt = nowUnix; o.sides = sides;
    b->owed.push_back(o);
    if (made != 0) *made = o;
    return true;
}
/* the invitations `from` sent, taken out (appended to *gone) */
inline void TakeInvitesFrom(Book* b, const std::string& from, std::vector<Invite>* gone)
{
    for (size_t i = 0; i < b->invites.size(); )
    {
        if (b->invites[i].from == from) { if (gone != 0) gone->push_back(b->invites[i]); b->invites.erase(b->invites.begin() + i); }
        else ++i;
    }
}

/* A TEAM ENDS WITH ITS LAST MEMBER (party-style: a founder is never left alone in a team). When a member's departure (leave,
   removal, a deleted profile) leaves the founder alone, the team ends as a disband does: the founder's own RESTORE row (why
   disbanded; its pre-join standing when it holds one - a member who became the founder) and every invitation the founder had
   open ends. */
struct TeamEnd
{
    bool ended;                      // the team ended with this departure
    std::string founder;             // the player left alone, now in no team
    unsigned teamNo;
    std::string teamName;
    Owed row;                        // the founder's own RESTORE row (number 0 = the founder holds no pre-join standing or sides)
    bool rowSkipped;                 // the row was due and had no room (a deleted profile's departure is never refused)
    std::vector<Invite> gone;        // the founder's invitations, ended with the team
    TeamEnd() : ended(false), teamNo(0), rowSkipped(false) {}
};
/* `id` is among onlineIds (0 = every player counts as connected) */
inline bool OnlineIn(const std::vector<std::string>* onlineIds, const std::string& id)
{
    if (onlineIds == 0) return true;
    for (size_t k = 0; k < onlineIds->size(); ++k) if ((*onlineIds)[k] == id) return true;
    return false;
}
/* team t, its founder alone, ends (*end). force = end it even when the founder's row has no room (rowSkipped); otherwise false =
   no room - nothing changed. */
inline bool EndLoneTeam(Book* b, int t, long long nowUnix, const std::vector<std::string>* onlineIds, bool force, TeamEnd* end)
{
    TeamEnd e; e.ended = true; e.founder = b->teams[t].founder; e.teamNo = b->teams[t].no; e.teamName = b->teams[t].name;
    if (!FounderOut(b, t, kWhyDisbanded, nowUnix, &e.row, !OnlineIn(onlineIds, e.founder)))
    {
        if (!force) return false;
        e.rowSkipped = true;
    }
    b->teams.erase(b->teams.begin() + t);
    TakeInvitesFrom(b, e.founder, &e.gone);
    if (end != 0) *end = e;
    return true;
}
/* member m of team t departs (MemberOut); when it was the last member the team ends with it (EndLoneTeam) - both or neither:
   false = no room for a row, nothing changed. The outputs are written only on success. */
inline bool MemberDeparts(Book* b, int t, size_t m, int why, bool away, long long nowUnix, Owed* made, std::vector<Owed>* sideRows,
                          const std::vector<std::string>* onlineIds, std::vector<std::string>* skipped, TeamEnd* end)
{
    Book c = *b;
    Owed o; std::vector<Owed> rows; std::vector<std::string> sk; TeamEnd e;
    if (!MemberOut(&c, t, m, why, away, nowUnix, &o, &rows, onlineIds, &sk)) return false;
    if (c.teams[t].members.empty() && !EndLoneTeam(&c, t, nowUnix, onlineIds, false, &e)) return false;
    b->teams.swap(c.teams); b->owed.swap(c.owed); b->invites.swap(c.invites); b->nextTeam = c.nextTeam; b->nextOwed = c.nextOwed;
    if (made != 0) *made = o;
    if (sideRows != 0) sideRows->insert(sideRows->end(), rows.begin(), rows.end());
    if (skipped != 0) skipped->insert(skipped->end(), sk.begin(), sk.end());
    if (end != 0) *end = e;
    return true;
}

/* LEAVE: a member leaves (*made = its RESTORE row); the last member's leave ends the team (*end). A founder with members is
   refused (they disband); a founder alone (a table kept from before a team ended with its last member) ends the team and every
   invitation they had open ends (*gone). A founder alone gets a row only when they hold a pre-join standing (a member who became
   the founder); a founder who made the team has none - *made keeps number 0. */
inline int LeaveDecide(Book* b, const std::string& id, long long nowUnix, Owed* made, std::vector<Invite>* gone = 0, std::vector<Owed>* sideRows = 0,
                       const std::vector<std::string>* onlineIds = 0, std::vector<std::string>* skipped = 0, TeamEnd* end = 0)
{
    const int t = TeamOf(*b, id);
    if (t < 0) return kRefNotInTeam;
    if (b->teams[t].founder == id)
    {
        if (!b->teams[t].members.empty()) return kRefFounderLeaves;
        if (!FounderOut(b, t, kWhyLeft, nowUnix, made)) return kRefOwedFull;
        TakeInvitesFrom(b, id, gone);
        b->teams.erase(b->teams.begin() + t);
        return kOk;
    }
    if (!OwedRoom(*b, id)) return kRefOwedFull;
    for (size_t m = 0; m < b->teams[t].members.size(); ++m)
        if (b->teams[t].members[m].id == id) return MemberDeparts(b, t, m, kWhyLeft, false, nowUnix, made, sideRows, onlineIds, skipped, end) ? kOk : kRefOwedFull;
    return kRefNotInTeam;
}

/* REMOVE: the founder removes a member, connected or not (targetOnline false = the row is owed until that player's next join);
   removing the last member ends the team (*end). */
inline int RemoveDecide(Book* b, const std::string& founder, const std::string& target, bool targetKnown, bool targetOnline,
                        long long nowUnix, Owed* made, std::vector<Owed>* sideRows = 0, const std::vector<std::string>* onlineIds = 0,
                        std::vector<std::string>* skipped = 0, TeamEnd* end = 0)
{
    const int t = TeamOf(*b, founder);
    if (t < 0) return kRefNotInTeam;
    if (b->teams[t].founder != founder) return kRefNotFounder;
    if (!targetKnown) return kRefUnknownPlayer;
    if (target == founder) return kRefSelf;
    for (size_t m = 0; m < b->teams[t].members.size(); ++m)
        if (b->teams[t].members[m].id == target)
        {
            if (!OwedRoom(*b, target)) return kRefOwedFull;
            return MemberDeparts(b, t, m, kWhyRemoved, !targetOnline, nowUnix, made, sideRows, onlineIds, skipped, end) ? kOk : kRefOwedFull;
        }
    return kRefNotMember;
}

/* DISBAND: the founder ends the team; every member gets a RESTORE row (*made, in member order) with its pre-join snapshot and
   every side it recorded (towards anyone in the team), then the founder when they hold a pre-join standing or recorded sides
   (never while away - the founder asked). onlineIds = the profile ids connected now (a member not among them gets a while-away
   row). The founder's own invitations end with it (*gone). Refused whole (restore-backlog) when any of those rows could not be
   kept. */
inline int DisbandDecide(Book* b, const std::string& founder, const std::vector<std::string>& onlineIds, long long nowUnix,
                         std::vector<Owed>* made, std::string* teamName, std::vector<Invite>* gone = 0)
{
    const int t = TeamOf(*b, founder);
    if (t < 0) return kRefNotInTeam;
    if (b->teams[t].founder != founder) return kRefNotFounder;
    const size_t founderRow = (b->teams[t].founderSnap.empty() && SidesOwnedBy(b->teams[t], founder, std::string()).empty()) ? 0 : 1;
    if (b->owed.size() + b->teams[t].members.size() + founderRow > kMaxOwed) return kRefOwedFull;
    for (size_t m = 0; m < b->teams[t].members.size(); ++m) if (OwedCount(*b, b->teams[t].members[m].id) >= kMaxOwedEach) return kRefOwedFull;
    if (founderRow != 0 && OwedCount(*b, founder) >= kMaxOwedEach) return kRefOwedFull;
    if (teamName != 0) *teamName = b->teams[t].name;
    for (size_t m = 0; m < b->teams[t].members.size(); ++m)
    {
        const Member& x = b->teams[t].members[m];
        bool on = false;
        for (size_t k = 0; k < onlineIds.size(); ++k) if (onlineIds[k] == x.id) on = true;
        Owed o; o.no = b->nextOwed++; o.id = x.id; o.why = kWhyDisbanded; o.away = on ? 0 : 1; o.teamName = b->teams[t].name;
        o.snapRows = x.snapRows; o.snap = x.snap; o.madeAt = nowUnix; o.sides = SidesOwnedBy(b->teams[t], x.id, std::string());
        b->owed.push_back(o);
        if (made != 0) made->push_back(o);
    }
    b->teams[t].members.clear();
    Owed f;
    if (!FounderOut(b, t, kWhyDisbanded, nowUnix, &f)) return kRefOwedFull;   /* not reached: room was checked above */
    if (f.no != 0 && made != 0) made->push_back(f);
    b->teams.erase(b->teams.begin() + t);
    TakeInvitesFrom(b, founder, gone);
    return kOk;
}

/* the invitations past their time, taken out (appended to *gone); the decline waits past their time dropped */
inline void ExpireInvites(Book* b, double now, std::vector<Invite>* gone)
{
    for (size_t i = 0; i < b->declined.size(); ) { if (b->declined[i].expiresAt <= now) b->declined.erase(b->declined.begin() + i); else ++i; }
    for (size_t i = 0; i < b->invites.size(); )
    {
        if (b->invites[i].expiresAt <= now) { if (gone != 0) gone->push_back(b->invites[i]); b->invites.erase(b->invites.begin() + i); }
        else ++i;
    }
}
/* a player's connection closed: every invitation from or to them ends (appended to *gone) */
inline void DropInvitesOf(Book* b, const std::string& id, std::vector<Invite>* gone)
{
    for (size_t i = 0; i < b->invites.size(); )
    {
        if (b->invites[i].from == id || b->invites[i].to == id) { if (gone != 0) gone->push_back(b->invites[i]); b->invites.erase(b->invites.begin() + i); }
        else ++i;
    }
}
/* RESTORE_DONE: the row `no` for player `id` is applied there - erased. false = no such row for that player. */
inline bool RestoreDone(Book* b, const std::string& id, unsigned no)
{
    for (size_t i = 0; i < b->owed.size(); ++i)
        if (b->owed[i].no == no && b->owed[i].id == id) { b->owed.erase(b->owed.begin() + i); return true; }
    return false;
}

/* a profile was deleted (owner 482 a). A member is taken out with no RESTORE row (there is no one to restore). A founder's role
   passes to the member who joined first (members are kept in joining order): the team keeps its number and its stored name, and
   that member's pre-join standing becomes the founder's own (owner rule 477: on leaving, standing goes back to before joining).
   A founder with no members ends the team (the deleted profile's rows go with it - there is no one to restore). When the
   deletion leaves one player alone in the team (a member deleted from a team of two, or a founder deleted with one member, who
   takes the role), the team ends with it (kDelLastEnded, *end - EndLoneTeam; the row is skipped when it has no room: a deletion
   is never refused; away = not among onlineIds, 0 = nobody connected). A member's departure gives a team that stays the founder's
   side from before the pin as its stance towards it (StanceBackOnDeparture), as a leave does. Every invitation from or to the
   profile ends (*gone) and every RESTORE row kept for it is dropped (*rowsDropped). */
enum { kDelNone = 0, kDelMember = 1, kDelFounderPassed = 2, kDelTeamEnded = 3, kDelLastEnded = 4 };
inline int ProfileDeletedDecide(Book* b, const std::string& id, std::vector<Invite>* gone, std::string* teamName, std::string* newFounder,
                                unsigned* rowsDropped, long long nowUnix = 0, const std::vector<std::string>* onlineIds = 0, TeamEnd* end = 0)
{
    const std::vector<std::string> noneOnline;
    if (onlineIds == 0) onlineIds = &noneOnline;
    DropInvitesOf(b, id, gone);
    unsigned dropped = 0;
    for (size_t i = 0; i < b->owed.size(); ) { if (b->owed[i].id == id) { b->owed.erase(b->owed.begin() + i); ++dropped; } else ++i; }
    if (rowsDropped != 0) *rowsDropped = dropped;
    const int t = TeamOf(*b, id);
    if (t < 0) return kDelNone;
    Team& team = b->teams[t];
    if (teamName != 0) *teamName = team.name;
    if (team.founder != id) StanceBackOnDeparture(&team, id);
    DropSidesOf(&team, id);
    if (team.founder == id)
    {
        if (team.members.empty()) { b->teams.erase(b->teams.begin() + t); return kDelTeamEnded; }
        team.founder = team.members[0].id;
        team.founderSnapRows = team.members[0].snapRows;
        team.founderSnap = team.members[0].snap;
        if (newFounder != 0) *newFounder = team.founder;
        team.members.erase(team.members.begin());
        if (!team.members.empty()) return kDelFounderPassed;
        EndLoneTeam(b, t, nowUnix, onlineIds, true, end);
        return kDelLastEnded;
    }
    for (size_t m = 0; m < team.members.size(); ++m)
        if (team.members[m].id == id)
        {
            team.members.erase(team.members.begin() + m);
            if (!team.members.empty()) return kDelMember;
            EndLoneTeam(b, t, nowUnix, onlineIds, true, end);
            return kDelLastEnded;
        }
    return kDelNone;
}

/* ------------------------------------------------- the record's decisions ------------------------------------------------- */
inline float ClampRel(float v) { return v < kRecLow ? kRecLow : v > kRecHigh ? kRecHigh : v; }
inline void EncodeRec(const TeamRec& r, std::vector<char>* out);
/* the record's encoded size: a record over kMaxSnapshot could not travel (RECORD, SEED) nor be read back from teams.txt, so every
   change that would make it larger is refused (too-many) */
inline size_t RecBytes(const TeamRec& r) { std::vector<char> b; EncodeRec(r, &b); return b.size(); }
/* every player of a team, the founder first: who a RECORD goes to */
inline std::vector<std::string> TeamIds(const Team& t)
{
    std::vector<std::string> out(1, t.founder);
    for (size_t m = 0; m < t.members.size(); ++m) out.push_back(t.members[m].id);
    return out;
}
/* SEED: the founder's game gives its team the founder's own standing, once (a team with a record refuses: already-seeded) */
inline int SeedDecide(Book* b, const std::string& id, const TeamRec& r)
{
    const int t = TeamOf(*b, id);
    if (t < 0) return kRefNotInTeam;
    if (b->teams[t].founder != id) return kRefNotFounder;
    if (b->teams[t].rec.seeded) return kRefSeeded;
    if (r.rows.size() > kMaxRecRows || r.stances.size() > kMaxStances) return kRefTooMany;
    if (RecBytes(r) > kMaxSnapshot) return kRefTooMany;
    TeamRec n = r; n.seeded = 1; n.gen = b->teams[t].rec.gen + 1;
    for (size_t i = 0; i < n.rows.size(); ++i) { n.rows[i].rel = ClampRel(n.rows[i].rel); n.rows[i].relBack = ClampRel(n.rows[i].relBack); }
    for (size_t i = 0; i < n.stances.size(); ++i) n.stances[i].rel = ClampRel(n.stances[i].rel);
    b->teams[t].rec = n;
    return kOk;
}
/* one change added to the record: a direction the record holds moves by the change (the relation kept on the scale); a
   direction it does not hold yet, or a change marked kDirAbsolute, takes the member's value; the flags are the member's.
   true = the record changed. */
inline bool ApplyDelta(TeamRec* r, const Delta& d)
{
    size_t i = 0;
    while (i < r->rows.size() && r->rows[i].sid != d.sid) ++i;
    if (i == r->rows.size()) { if (r->rows.size() >= kMaxRecRows || d.sid.empty()) return false; RecRow n; n.sid = d.sid; r->rows.push_back(n); }
    RecRow& row = r->rows[i];
    const unsigned dir = d.dir & 1u;
    const unsigned bit = dir == 0 ? (unsigned)kHaveFwd : (unsigned)kHaveBack;
    float* rel = dir == 0 ? &row.rel : &row.relBack;
    float* tr = dir == 0 ? &row.trust : &row.trustBack;
    float* tn = dir == 0 ? &row.trustNeg : &row.trustNegBack;
    unsigned* fl = dir == 0 ? &row.flags : &row.flagsBack;
    const float r0 = *rel, t0 = *tr, n0 = *tn; const unsigned f0 = *fl; const unsigned h0 = row.have;
    if ((row.have & bit) == 0 || (d.dir & kDirAbsolute) != 0) { *rel = ClampRel(d.rel); *tr = d.trust; *tn = d.trustNeg; row.have |= bit; }
    else { *rel = ClampRel(*rel + d.dRel); *tr += d.dTrust; *tn += d.dTrustNeg; }
    *fl = d.flags & 3u;
    return *rel != r0 || *tr != t0 || *tn != n0 || *fl != f0 || row.have != h0;
}
/* DELTA: a member's own engine changes, added to its team's record (a record not seeded yet refuses: no-record; a record that
   would grow past kMaxSnapshot refuses: too-many). *changed = the record moved (an empty or zero change moves nothing: no new
   generation, nothing to save or send). */
inline int DeltaDecide(Book* b, const std::string& id, const std::vector<Delta>& ds, bool* changed = 0)
{
    if (changed != 0) *changed = false;
    const int t = TeamOf(*b, id);
    if (t < 0) return kRefNotInTeam;
    if (!b->teams[t].rec.seeded) return kRefNoRecord;
    bool moved = false;
    for (size_t i = 0; i < ds.size(); ++i) if (ApplyDelta(&b->teams[t].rec, ds[i])) moved = true;
    if (!moved) return kOk;
    if (RecBytes(b->teams[t].rec) > kMaxSnapshot) return kRefTooMany;
    ++b->teams[t].rec.gen;
    if (changed != 0) *changed = true;
    return kOk;
}
/* RESEARCH: the techs `id`'s game reports finished, added to its team's research. *added = the ones the team lacked (sorted,
   unique; empty = nothing new, nothing to save or send). A player in no team is refused (not-in-team); a list or a union over
   kMaxTechs, or an empty or over-long stringID, is refused whole (too-many). */
inline int ResearchDecide(Book* b, const std::string& id, const std::vector<std::string>& techs, std::vector<std::string>* added)
{
    added->clear();
    const int t = TeamOf(*b, id);
    if (t < 0) return kRefNotInTeam;
    if (techs.size() > kMaxTechs) return kRefTooMany;
    std::vector<std::string>& have = b->teams[t].research;
    std::vector<std::string> in;
    for (size_t i = 0; i < techs.size(); ++i)
    {
        if (techs[i].empty() || techs[i].size() > kMaxName) return kRefTooMany;
        in.push_back(techs[i]);
    }
    std::sort(in.begin(), in.end()); in.erase(std::unique(in.begin(), in.end()), in.end());
    for (size_t k = 0; k < in.size(); ++k) if (!std::binary_search(have.begin(), have.end(), in[k])) added->push_back(in[k]);
    if (have.size() + added->size() > kMaxTechs) { added->clear(); return kRefTooMany; }
    if (added->empty()) return kOk;
    std::vector<std::string> merged; merged.reserve(have.size() + added->size());
    size_t i = 0, j = 0;   /* copied by hand: this toolset's std::merge moves out of its second range */
    while (i < have.size() || j < added->size())
        merged.push_back(j >= added->size() || (i < have.size() && have[i] < (*added)[j]) ? have[i++] : (*added)[j++]);
    have.swap(merged);
    return kOk;
}
/* a stance towards a player out of the record (that player joined the team: towards a member the pin holds) - true = removed */
inline bool DropStance(TeamRec* r, unsigned slot)
{
    for (size_t i = 0; i < r->stances.size(); ++i) if (r->stances[i].slot == slot) { r->stances.erase(r->stances.begin() + i); ++r->gen; return true; }
    return false;
}
/* STANCE: the team's stance towards another player's faction - set by the founder's choice (476: only the founder's PLAYERS-tab
   buttons are open) or by any member's own engine moving its side (owner 512: the team is one faction, so a member's fight is
   the faction's); the latest wins; never towards a member (slotInTeam) */
inline int StanceDecide(Book* b, const std::string& id, unsigned slot, bool slotInTeam, float rel, unsigned flags, bool* changed = 0)
{
    if (changed != 0) *changed = false;
    const int t = TeamOf(*b, id);
    if (t < 0) return kRefNotInTeam;
    if (slotInTeam) return kRefInTeam;
    if (!b->teams[t].rec.seeded) return kRefNoRecord;
    TeamRec& r = b->teams[t].rec;
    size_t i = 0;
    while (i < r.stances.size() && r.stances[i].slot != slot) ++i;
    if (i < r.stances.size() && r.stances[i].rel == ClampRel(rel) && r.stances[i].flags == (flags & 3u)) return kOk;
    if (i == r.stances.size()) { if (r.stances.size() >= kMaxStances) return kRefTooMany; RecStance n; n.slot = slot; r.stances.push_back(n); }
    r.stances[i].rel = ClampRel(rel); r.stances[i].flags = flags & 3u;
    if (RecBytes(r) > kMaxSnapshot) return kRefTooMany;
    ++r.gen;
    if (changed != 0) *changed = true;
    return kOk;
}
/* A DEPARTURE FROM A TEAM THAT STAYS (owner 476 / 512: the team is one faction and its stance towards another player is the
   founder's): the team's stance towards the departing member `gone` becomes the founder's side towards that player as it stood
   before the pin (the founder's recorded side, SidesOwnedBy) - the stance the joining took out of the record (DropStance) - so
   every remaining member's game writes the same side towards them, and no game sends the side the pin still holds as the team's.
   Nothing changes when the record is not seeded, the founder holds no recorded side towards `gone`, the stance is already that,
   or it would not fit (kMaxStances, kMaxSnapshot). true = the record changed (a new generation). */
inline bool StanceBackOnDeparture(Team* t, const std::string& gone)
{
    if (t == 0 || !t->rec.seeded || gone.empty() || gone == t->founder) return false;
    const std::vector<RecStance> fs = SidesOwnedBy(*t, t->founder, gone);
    if (fs.empty()) return false;
    TeamRec r = t->rec;
    const float rel = ClampRel(fs[0].rel);
    const unsigned flags = fs[0].flags & 3u;
    size_t i = 0;
    while (i < r.stances.size() && r.stances[i].slot != fs[0].slot) ++i;
    if (i < r.stances.size() && r.stances[i].rel == rel && r.stances[i].flags == flags) return false;
    if (i == r.stances.size()) { if (r.stances.size() >= kMaxStances) return false; RecStance n; n.slot = fs[0].slot; r.stances.push_back(n); }
    r.stances[i].rel = rel; r.stances[i].flags = flags; ++r.gen;
    if (RecBytes(r) > kMaxSnapshot) return false;
    t->rec = r;
    return true;
}
/* the record of team t carries the founder's side from before the pin towards `gone` as the team's stance (seeded, the founder
   holds a recorded side towards that player, and the stance towards its slot is that side) */
inline bool RecordCarriesDeparture(const Team& t, const std::string& gone)
{
    if (!t.rec.seeded || gone.empty() || gone == t.founder) return false;
    const std::vector<RecStance> fs = SidesOwnedBy(t, t.founder, gone);
    if (fs.empty()) return false;
    for (size_t i = 0; i < t.rec.stances.size(); ++i)
        if (t.rec.stances[i].slot == fs[0].slot) return t.rec.stances[i].rel == ClampRel(fs[0].rel) && t.rec.stances[i].flags == (fs[0].flags & 3u);
    return false;
}
/* SIDE: a member's side towards a teammate as it stood before the pin; the first report of a pair is kept (*kept = false for a
   later one: by then the pin has moved it). Both must share a team. */
inline int SideDecide(Book* b, const std::string& owner, unsigned ownerSlot, const std::string& other, unsigned otherSlot, float rel, unsigned flags, bool* kept)
{
    if (kept != 0) *kept = false;
    const int t = TeamOf(*b, owner);
    if (t < 0 || owner == other || other.empty() || TeamOf(*b, other) != t) return kRefNotMember;
    Team& team = b->teams[t];
    for (size_t i = 0; i < team.sides.size(); ++i) if (team.sides[i].owner == owner && team.sides[i].other == other) return kOk;
    if (team.sides.size() >= kMaxSides) return kRefTooMany;
    SideSnap x; x.owner = owner; x.other = other; x.ownerSlot = ownerSlot; x.otherSlot = otherSlot; x.rel = rel; x.flags = flags;
    team.sides.push_back(x);
    if (kept != 0) *kept = true;
    return kOk;
}

/* ------------------------------------------------------- bytes ------------------------------------------------------- */
inline void PutU8(std::vector<char>* b, unsigned v) { b->push_back((char)(unsigned char)v); }
inline void PutU32(std::vector<char>* b, unsigned v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void PutBytes(std::vector<char>* b, const char* p, size_t n) { PutU32(b, (unsigned)n); b->insert(b->end(), p, p + n); }
inline void PutStr(std::vector<char>* b, const std::string& s) { PutBytes(b, s.data(), s.size()); }
struct Cur { const char* p; size_t n, at; bool bad; Cur(const char* p_, size_t n_) : p(p_), n(n_), at(0), bad(p_ == 0 && n_ != 0) {} };
inline unsigned GetU8(Cur* c) { if (c->bad || c->at + 1 > c->n) { c->bad = true; return 0; } return (unsigned char)c->p[c->at++]; }
inline unsigned GetU32(Cur* c) { if (c->bad || c->at + 4 > c->n) { c->bad = true; return 0; } unsigned v = 0; std::memcpy(&v, c->p + c->at, 4); c->at += 4; return v; }
inline void GetBytes(Cur* c, std::vector<char>* out, unsigned cap)
{
    const unsigned len = GetU32(c);
    if (c->bad || len > cap || c->at + len > c->n) { c->bad = true; return; }
    out->assign(c->p + c->at, c->p + c->at + len); c->at += len;
}
inline void GetStr(Cur* c, std::string* out, unsigned cap) { std::vector<char> v; GetBytes(c, &v, cap); if (!c->bad) out->assign(v.begin(), v.end()); }
inline void PutF32(std::vector<char>* b, float f) { unsigned u = 0; std::memcpy(&u, &f, 4); PutU32(b, u); }
inline float GetF32(Cur* c) { const unsigned u = GetU32(c); float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
inline void EncodeRec(const TeamRec& r, std::vector<char>* out)
{
    out->clear(); PutU32(out, r.gen); PutU8(out, r.seeded ? 1u : 0u); PutU32(out, (unsigned)r.rows.size());
    for (size_t i = 0; i < r.rows.size(); ++i)
    {
        const RecRow& x = r.rows[i];
        PutStr(out, x.sid); PutF32(out, x.rel); PutF32(out, x.trust); PutF32(out, x.trustNeg); PutF32(out, x.relBack); PutF32(out, x.trustBack);
        PutF32(out, x.trustNegBack); PutU32(out, x.flags); PutU32(out, x.flagsBack); PutU8(out, x.have & 3u);
    }
    PutU32(out, (unsigned)r.stances.size());
    for (size_t i = 0; i < r.stances.size(); ++i) { PutU32(out, r.stances[i].slot); PutF32(out, r.stances[i].rel); PutU32(out, r.stances[i].flags); }
}
inline bool DecodeRec(const std::vector<char>& b, TeamRec* r)
{
    TeamRec o; Cur c(b.empty() ? 0 : &b[0], b.size());
    o.gen = GetU32(&c); o.seeded = GetU8(&c) ? 1 : 0;
    const unsigned n = GetU32(&c);
    if (c.bad || n > kMaxRecRows) return false;
    for (unsigned i = 0; i < n && !c.bad; ++i)
    {
        RecRow x; GetStr(&c, &x.sid, kMaxName);
        x.rel = GetF32(&c); x.trust = GetF32(&c); x.trustNeg = GetF32(&c); x.relBack = GetF32(&c); x.trustBack = GetF32(&c); x.trustNegBack = GetF32(&c);
        x.flags = GetU32(&c); x.flagsBack = GetU32(&c); x.have = GetU8(&c);
        if (x.have > 3) c.bad = true;
        o.rows.push_back(x);
    }
    const unsigned m = GetU32(&c);
    if (c.bad || m > kMaxStances) return false;
    for (unsigned i = 0; i < m && !c.bad; ++i) { RecStance x; x.slot = GetU32(&c); x.rel = GetF32(&c); x.flags = GetU32(&c); o.stances.push_back(x); }
    if (c.bad || c.at != b.size()) return false;
    *r = o; return true;
}
inline void PutSides(std::vector<char>* b, const std::vector<RecStance>& s)
{
    PutU32(b, (unsigned)s.size());
    for (size_t i = 0; i < s.size(); ++i) { PutU32(b, s[i].slot); PutF32(b, s[i].rel); PutU32(b, s[i].flags); }
}
inline void GetSides(Cur* c, std::vector<RecStance>* s)
{
    const unsigned n = GetU32(c);
    if (c->bad || n > kMaxSides) { c->bad = true; return; }
    for (unsigned i = 0; i < n && !c->bad; ++i) { RecStance x; x.slot = GetU32(c); x.rel = GetF32(c); x.flags = GetU32(c); s->push_back(x); }
}

/* ------------------------------------------------------ up (game -> world server) ------------------------------------------------------ */
struct Up
{
    int kind;
    unsigned slot, accept, snapRows, no;
    std::string name;
    std::vector<char> snap;
    TeamRec rec;                     // SEED
    std::vector<Delta> deltas;       // DELTA
    float rel; unsigned flags;       // STANCE / SIDE (with slot)
    unsigned whole;                  // RESEARCH: 1 = this game's whole finished list, 0 = techs its own player finished now
    std::vector<std::string> techs;  // RESEARCH
    Up() : kind(0), slot(0), accept(0), snapRows(0), no(0), rel(0.0f), flags(0), whole(0) {}
};
inline void EncodeInvite(std::vector<char>* b, unsigned slot, const std::string& factionName) { b->clear(); PutU8(b, kUpInvite); PutU32(b, slot); PutStr(b, factionName.substr(0, kMaxName)); }
inline bool EncodeAnswer(std::vector<char>* b, bool accept, unsigned snapRows, const std::vector<char>& snap)
{
    b->clear(); if (snap.size() > kMaxSnapshot) return false;
    PutU8(b, kUpAnswer); PutU8(b, accept ? 1u : 0u); PutU32(b, snapRows); PutBytes(b, snap.empty() ? "" : &snap[0], snap.size());
    return true;
}
inline void EncodeLeave(std::vector<char>* b) { b->clear(); PutU8(b, kUpLeave); }
inline void EncodeRemove(std::vector<char>* b, unsigned slot) { b->clear(); PutU8(b, kUpRemove); PutU32(b, slot); }
inline void EncodeDisband(std::vector<char>* b) { b->clear(); PutU8(b, kUpDisband); }
inline void EncodeRestoreDone(std::vector<char>* b, unsigned no) { b->clear(); PutU8(b, kUpRestoreDone); PutU32(b, no); }
inline void EncodeAsk(std::vector<char>* b) { b->clear(); PutU8(b, kUpAsk); }
inline bool EncodeSeed(std::vector<char>* b, const TeamRec& r)
{
    std::vector<char> blob; EncodeRec(r, &blob);
    b->clear(); if (blob.size() > kMaxSnapshot || r.rows.size() > kMaxRecRows || r.stances.size() > kMaxStances) return false;
    PutU8(b, kUpSeed); PutBytes(b, &blob[0], blob.size());
    return true;
}
inline bool EncodeDeltas(std::vector<char>* b, const std::vector<Delta>& d)
{
    b->clear(); if (d.size() > kMaxDeltas) return false;
    PutU8(b, kUpDelta); PutU32(b, (unsigned)d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        PutStr(b, d[i].sid.substr(0, kMaxName)); PutU8(b, d[i].dir & 3u); PutF32(b, d[i].dRel); PutF32(b, d[i].dTrust); PutF32(b, d[i].dTrustNeg);
        PutF32(b, d[i].rel); PutF32(b, d[i].trust); PutF32(b, d[i].trustNeg); PutU32(b, d[i].flags);
    }
    return true;
}
inline void EncodeStance(std::vector<char>* b, unsigned slot, float rel, unsigned flags) { b->clear(); PutU8(b, kUpStance); PutU32(b, slot); PutF32(b, rel); PutU32(b, flags); }
inline void EncodeSide(std::vector<char>* b, unsigned otherSlot, float rel, unsigned flags) { b->clear(); PutU8(b, kUpSide); PutU32(b, otherSlot); PutF32(b, rel); PutU32(b, flags); }
inline void PutTechs(std::vector<char>* b, const std::vector<std::string>& t) { PutU32(b, (unsigned)t.size()); for (size_t i = 0; i < t.size(); ++i) PutStr(b, t[i]); }
inline void GetTechs(Cur* c, std::vector<std::string>* t)
{
    const unsigned n = GetU32(c);
    if (c->bad || n > kMaxTechs) { c->bad = true; return; }
    for (unsigned i = 0; i < n && !c->bad; ++i) { std::string x; GetStr(c, &x, kMaxName); if (x.empty()) c->bad = true; t->push_back(x); }
}
/* false = more than kMaxTechs, or an empty or over-long stringID (nothing is encoded) */
inline bool EncodeResearchUp(std::vector<char>* b, bool whole, const std::vector<std::string>& techs)
{
    b->clear(); if (techs.size() > kMaxTechs) return false;
    for (size_t i = 0; i < techs.size(); ++i) if (techs[i].empty() || techs[i].size() > kMaxName) return false;
    PutU8(b, kUpResearch); PutU8(b, whole ? 1u : 0u); PutTechs(b, techs);
    return true;
}
/* false = not an up message this header knows, or malformed (trailing bytes included) */
inline bool DecodeUp(const char* p, size_t n, Up* u)
{
    if (u == 0) return false;
    *u = Up();
    Cur c(p, n);
    u->kind = (int)GetU8(&c);
    if (u->kind == kUpInvite) { u->slot = GetU32(&c); GetStr(&c, &u->name, kMaxName); }
    else if (u->kind == kUpAnswer) { u->accept = GetU8(&c); u->snapRows = GetU32(&c); GetBytes(&c, &u->snap, kMaxSnapshot); if (u->accept > 1) c.bad = true; }
    else if (u->kind == kUpRemove) u->slot = GetU32(&c);
    else if (u->kind == kUpRestoreDone) u->no = GetU32(&c);
    else if (u->kind == kUpSeed) { std::vector<char> blob; GetBytes(&c, &blob, kMaxSnapshot); if (!c.bad && !DecodeRec(blob, &u->rec)) c.bad = true; }
    else if (u->kind == kUpDelta)
    {
        const unsigned n = GetU32(&c);
        if (c.bad || n > kMaxDeltas) return false;
        for (unsigned i = 0; i < n && !c.bad; ++i)
        {
            Delta d; GetStr(&c, &d.sid, kMaxName); d.dir = GetU8(&c);
            d.dRel = GetF32(&c); d.dTrust = GetF32(&c); d.dTrustNeg = GetF32(&c); d.rel = GetF32(&c); d.trust = GetF32(&c); d.trustNeg = GetF32(&c); d.flags = GetU32(&c);
            if (d.dir > 3) c.bad = true;
            u->deltas.push_back(d);
        }
    }
    else if (u->kind == kUpStance || u->kind == kUpSide) { u->slot = GetU32(&c); u->rel = GetF32(&c); u->flags = GetU32(&c); }
    else if (u->kind == kUpResearch) { u->whole = GetU8(&c); if (u->whole > 1) c.bad = true; GetTechs(&c, &u->techs); }
    else if (u->kind != kUpLeave && u->kind != kUpDisband && u->kind != kUpAsk) return false;
    return !c.bad && c.at == n;
}

/* ------------------------------------------------------ down (world server -> game) ------------------------------------------------------ */
struct WireTeam
{
    unsigned no, founderSlot;
    std::string name;
    std::vector<unsigned> memberSlots;
    WireTeam() : no(0), founderSlot(0) {}
};
struct Down
{
    int kind;
    std::vector<WireTeam> table;                         // TABLE
    unsigned fromSlot, secondsLeft;                      // INVITED
    unsigned event, result, actorSlot, subjectSlot;      // NOTICE
    unsigned no, why, away, snapRows;                    // RESTORE
    std::string teamName;                                // INVITED / NOTICE / RESTORE
    std::vector<char> snap;                              // RESTORE
    std::vector<RecStance> sides;                        // RESTORE
    unsigned recTeam; TeamRec rec;                       // RECORD
    unsigned resTeam, resWhole, resBy;                   // RESEARCH (resBy: the game a reply answers, or the finisher of an OWN tech; 0xFFFFFFFF = for all)
    std::vector<std::string> techs;                      // RESEARCH
    Down() : recTeam(0), resTeam(0), resWhole(0), resBy(0xFFFFFFFFu), kind(0), fromSlot(0), secondsLeft(0), event(0), result(0), actorSlot(0), subjectSlot(0), no(0), why(0), away(0), snapRows(0) {}
};
/* the table as the games see it: profile ids turned into slots (slotOf: profile id -> slot; a player with no slot is skipped) */
inline std::vector<WireTeam> ToWire(const Book& b, const std::map<std::string, unsigned>& slotOf)
{
    std::vector<WireTeam> out;
    for (size_t t = 0; t < b.teams.size(); ++t)
    {
        std::map<std::string, unsigned>::const_iterator f = slotOf.find(b.teams[t].founder);
        if (f == slotOf.end()) continue;
        WireTeam w; w.no = b.teams[t].no; w.founderSlot = f->second; w.name = b.teams[t].name;
        for (size_t m = 0; m < b.teams[t].members.size(); ++m)
        { std::map<std::string, unsigned>::const_iterator s = slotOf.find(b.teams[t].members[m].id); if (s != slotOf.end()) w.memberSlots.push_back(s->second); }
        out.push_back(w);
    }
    return out;
}
inline void EncodeTable(std::vector<char>* b, const std::vector<WireTeam>& t)
{
    b->clear(); PutU8(b, kDnTable); PutU32(b, (unsigned)t.size());
    for (size_t i = 0; i < t.size(); ++i)
    {
        PutU32(b, t[i].no); PutU32(b, t[i].founderSlot); PutStr(b, t[i].name); PutU32(b, (unsigned)t[i].memberSlots.size());
        for (size_t m = 0; m < t[i].memberSlots.size(); ++m) PutU32(b, t[i].memberSlots[m]);
    }
}
inline void EncodeInvited(std::vector<char>* b, unsigned fromSlot, const std::string& teamName, unsigned secondsLeft)
{ b->clear(); PutU8(b, kDnInvited); PutU32(b, fromSlot); PutStr(b, teamName); PutU32(b, secondsLeft); }
inline void EncodeNotice(std::vector<char>* b, unsigned event, unsigned result, unsigned actorSlot, unsigned subjectSlot, const std::string& teamName)
{ b->clear(); PutU8(b, kDnNotice); PutU8(b, event); PutU8(b, result); PutU32(b, actorSlot); PutU32(b, subjectSlot); PutStr(b, teamName); }
inline void EncodeRestore(std::vector<char>* b, const Owed& o)
{
    b->clear(); PutU8(b, kDnRestore); PutU32(b, o.no); PutU8(b, (unsigned)o.why); PutU8(b, (unsigned)o.away); PutStr(b, o.teamName);
    PutU32(b, o.snapRows); PutBytes(b, o.snap.empty() ? "" : &o.snap[0], o.snap.size());
    std::vector<char> sides; PutSides(&sides, o.sides); PutBytes(b, &sides[0], sides.size());
}
inline void EncodeRecord(std::vector<char>* b, unsigned team, const TeamRec& r)
{
    std::vector<char> blob; EncodeRec(r, &blob);
    b->clear(); PutU8(b, kDnRecord); PutU32(b, team); PutBytes(b, &blob[0], blob.size());
}
/* false = more than kMaxTechs (nothing is encoded); bySlot as the wire comment above says */
inline bool EncodeResearchDown(std::vector<char>* b, unsigned team, bool whole, unsigned bySlot, const std::vector<std::string>& techs)
{
    b->clear(); if (techs.size() > kMaxTechs) return false;
    PutU8(b, kDnResearch); PutU32(b, team); PutU8(b, whole ? 1u : 0u); PutU32(b, bySlot); PutTechs(b, techs);
    return true;
}
inline bool DecodeDown(const char* p, size_t n, Down* d)
{
    if (d == 0) return false;
    *d = Down();
    Cur c(p, n);
    d->kind = (int)GetU8(&c);
    if (d->kind == kDnTable)
    {
        const unsigned k = GetU32(&c);
        if (k > kMaxTeams) return false;
        for (unsigned i = 0; i < k && !c.bad; ++i)
        {
            WireTeam w; w.no = GetU32(&c); w.founderSlot = GetU32(&c); GetStr(&c, &w.name, kMaxName);
            const unsigned m = GetU32(&c);
            if (m > kMaxMembers) return false;
            for (unsigned j = 0; j < m && !c.bad; ++j) w.memberSlots.push_back(GetU32(&c));
            d->table.push_back(w);
        }
    }
    else if (d->kind == kDnInvited) { d->fromSlot = GetU32(&c); GetStr(&c, &d->teamName, kMaxName); d->secondsLeft = GetU32(&c); }
    else if (d->kind == kDnNotice) { d->event = GetU8(&c); d->result = GetU8(&c); d->actorSlot = GetU32(&c); d->subjectSlot = GetU32(&c); GetStr(&c, &d->teamName, kMaxName); }
    else if (d->kind == kDnRestore)
    {
        d->no = GetU32(&c); d->why = GetU8(&c); d->away = GetU8(&c); GetStr(&c, &d->teamName, kMaxName); d->snapRows = GetU32(&c); GetBytes(&c, &d->snap, kMaxSnapshot);
        std::vector<char> sides; GetBytes(&c, &sides, kMaxSnapshot);
        if (!c.bad) { Cur sc(sides.empty() ? 0 : &sides[0], sides.size()); GetSides(&sc, &d->sides); if (sc.bad || sc.at != sides.size()) c.bad = true; }
    }
    else if (d->kind == kDnRecord)
    {
        d->recTeam = GetU32(&c);
        std::vector<char> blob; GetBytes(&c, &blob, kMaxSnapshot);
        if (!c.bad && !DecodeRec(blob, &d->rec)) c.bad = true;
    }
    else if (d->kind == kDnResearch) { d->resTeam = GetU32(&c); d->resWhole = GetU8(&c); d->resBy = GetU32(&c); if (d->resWhole > 1) c.bad = true; GetTechs(&c, &d->techs); }
    else return false;
    return !c.bad && c.at == n;
}

/* ------------------------------------------------------ a game's RESTORE rows ------------------------------------------------------ */
/* A game holds each received RESTORE row until its world is loaded (the row's standing is put back into a loaded world), then
   answers RESTORE_DONE and remembers the number. Row numbers are per world: rows of another world start the inbox afresh. */
struct Inbox
{
    std::string world;               // the world the rows below belong to
    std::vector<Down> held;          // received, waiting for this game's world to be loaded
    std::vector<unsigned> done;      // answered, newest last (at most kInboxDoneKept)
};
enum { kInHeld = 1, kInAgainHeld = 2, kInAgainDone = 3 };
/* one RESTORE row arrives: kInHeld = new, held; kInAgainHeld = already held (nothing to do); kInAgainDone = already answered
   (answer again, apply nothing) */
inline int InboxArrive(Inbox* in, const std::string& world, const Down& d)
{
    if (world != in->world) { in->world = world; in->held.clear(); in->done.clear(); }
    for (size_t i = 0; i < in->done.size(); ++i) if (in->done[i] == d.no) return kInAgainDone;
    for (size_t i = 0; i < in->held.size(); ++i) if (in->held[i].no == d.no) return kInAgainHeld;
    in->held.push_back(d);
    return kInHeld;
}
inline void InboxMarkDone(Inbox* in, unsigned no)
{
    in->done.push_back(no);
    if (in->done.size() > kInboxDoneKept) in->done.erase(in->done.begin());
}
/* the world is loaded: every held row out (appended to *ready, in arrival order) and remembered as answered */
inline void InboxTake(Inbox* in, std::vector<Down>* ready)
{
    for (size_t i = 0; i < in->held.size(); ++i) { ready->push_back(in->held[i]); InboxMarkDone(in, in->held[i].no); }
    in->held.clear();
}

/* ------------------------------------------------------ text ------------------------------------------------------ */
inline std::string U(unsigned long long v) { std::ostringstream o; o << v; return o.str(); }
inline std::string Clean(const std::string& s)   /* a name in a log line: printable, quotes and line breaks replaced */
{
    std::string o;
    for (size_t i = 0; i < s.size() && i < kMaxName; ++i) { const unsigned char ch = (unsigned char)s[i]; o += (ch < 32 || ch == '\'' || ch == 127) ? '_' : (char)ch; }
    return o;
}
/* "team 1 'Name' founder=s0 members=s1,s2; team 2 ..." or "no teams" - the same words on the world server and every game */
inline std::string TableText(const std::vector<WireTeam>& t)
{
    if (t.empty()) return "no teams";
    std::string s;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (i) s += "; ";
        s += "team " + U(t[i].no) + " '" + Clean(t[i].name) + "' founder=s" + U(t[i].founderSlot) + " members=";
        if (t[i].memberSlots.empty()) s += "-";
        for (size_t m = 0; m < t[i].memberSlots.size(); ++m) s += (m ? ",s" : "s") + U(t[i].memberSlots[m]);
    }
    return s;
}
/* the team a slot is in on a received table: its index, or -1; *founder = 1 when that slot founded it */
inline int WireTeamOf(const std::vector<WireTeam>& t, unsigned slot, int* founder)
{
    if (founder != 0) *founder = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i].founderSlot == slot) { if (founder != 0) *founder = 1; return (int)i; }
        for (size_t m = 0; m < t[i].memberSlots.size(); ++m) if (t[i].memberSlots[m] == slot) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------ file ------------------------------------------------------ */
inline std::string Hex(const char* p, size_t n)
{
    static const char* const d = "0123456789abcdef";
    std::string s; s.reserve(n * 2 + 1);
    for (size_t i = 0; i < n; ++i) { const unsigned char ch = (unsigned char)p[i]; s += d[ch >> 4]; s += d[ch & 15]; }
    return s.empty() ? std::string("-") : s;
}
inline std::string Hex(const std::string& s) { return Hex(s.data(), s.size()); }
inline std::string HexV(const std::vector<char>& v) { return Hex(v.empty() ? "" : &v[0], v.size()); }
inline bool Unhex(const std::string& h, std::vector<char>* out)
{
    out->clear();
    if (h == "-") return true;
    if (h.size() % 2) return false;
    for (size_t i = 0; i < h.size(); i += 2)
    {
        int v = 0;
        for (int k = 0; k < 2; ++k)
        {
            const char ch = h[i + k]; v <<= 4;
            if (ch >= '0' && ch <= '9') v |= ch - '0'; else if (ch >= 'a' && ch <= 'f') v |= ch - 'a' + 10; else return false;
        }
        out->push_back((char)v);
    }
    return true;
}
inline bool UnhexS(const std::string& h, std::string* out) { std::vector<char> v; if (!Unhex(h, &v)) return false; out->assign(v.begin(), v.end()); return true; }
inline std::string BookFile(const Book& b)
{
    std::string s = "N " + U(b.nextTeam) + " " + U(b.nextOwed) + "\n";
    for (size_t t = 0; t < b.teams.size(); ++t)
    {
        s += "T " + U(b.teams[t].no) + " " + Hex(b.teams[t].founder) + " " + Hex(b.teams[t].name) + "\n";
        if (!b.teams[t].founderSnap.empty()) s += "F " + U(b.teams[t].no) + " " + U(b.teams[t].founderSnapRows) + " " + HexV(b.teams[t].founderSnap) + "\n";
        if (b.teams[t].rec.seeded || !b.teams[t].rec.rows.empty() || !b.teams[t].rec.stances.empty())
        { std::vector<char> blob; EncodeRec(b.teams[t].rec, &blob); s += "R " + U(b.teams[t].no) + " " + HexV(blob) + "\n"; }
        for (size_t k = 0; k < b.teams[t].research.size(); ++k) s += "Q " + U(b.teams[t].no) + " " + Hex(b.teams[t].research[k]) + "\n";
        for (size_t m = 0; m < b.teams[t].members.size(); ++m)
        {
            const Member& x = b.teams[t].members[m];
            s += "M " + U(b.teams[t].no) + " " + Hex(x.id) + " " + U((unsigned long long)x.joinedAt) + " " + U(x.snapRows) + " " + HexV(x.snap) + "\n";
        }
        for (size_t k = 0; k < b.teams[t].sides.size(); ++k)
        {
            const SideSnap& x = b.teams[t].sides[k];
            unsigned bits = 0; std::memcpy(&bits, &x.rel, 4);
            s += "S " + U(b.teams[t].no) + " " + Hex(x.owner) + " " + U(x.ownerSlot) + " " + Hex(x.other) + " " + U(x.otherSlot) + " " + U(bits) + " " + U(x.flags) + "\n";
        }
    }
    for (size_t i = 0; i < b.owed.size(); ++i)
    {
        const Owed& o = b.owed[i];
        s += "O " + U(o.no) + " " + Hex(o.id) + " " + U((unsigned)o.why) + " " + U((unsigned)o.away) + " " + Hex(o.teamName) + " " + U(o.snapRows)
             + " " + HexV(o.snap) + " " + U((unsigned long long)o.madeAt);
        if (!o.sides.empty()) { std::vector<char> sb; PutSides(&sb, o.sides); s += " " + HexV(sb); }
        s += "\n";
    }
    return s;
}
/* one line into the book; false = not taken (*why: kLineBad = unusable, kLineDuplicate = a team or row number already read, or a
   second founder standing for one team). A member or founder-standing line must follow its team's line, and is refused when
   that team's line was. The next numbers stay at least one
   above every number read, whatever the order of the lines. Limits are the wire's: a name over kMaxName, a standing over
   kMaxSnapshot, a player's row over kMaxOwedEach, a row over kMaxOwed are refused. */
inline bool BookParseLine(const std::string& raw, Book* b, int* why = 0)
{
    if (why != 0) *why = kLineBad;
    std::string line = raw;
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    std::istringstream is(line);
    std::string tag; is >> tag;
    if (tag == "N")
    {
        unsigned a = 0, c = 0; if (!(is >> a >> c) || a == 0 || c == 0) return false;
        if (a > b->nextTeam) b->nextTeam = a;
        if (c > b->nextOwed) b->nextOwed = c;
        if (why != 0) *why = kLineOk;
        return true;
    }
    if (tag == "T")
    {
        Team t; std::string f, n; if (!(is >> t.no >> f >> n) || t.no == 0 || t.no == 0xFFFFFFFFu) return false;
        for (size_t k = 0; k < b->teams.size(); ++k)
            if (b->teams[k].no == t.no) { b->loadRefused.push_back(t.no); if (why != 0) *why = kLineDuplicate; return false; }
        if (!UnhexS(f, &t.founder) || !UnhexS(n, &t.name) || t.founder.empty() || t.name.size() > kMaxName || TeamOf(*b, t.founder) >= 0
            || b->teams.size() >= kMaxTeams) { b->loadRefused.push_back(t.no); return false; }
        b->teams.push_back(t); if (t.no >= b->nextTeam) b->nextTeam = t.no + 1;
        if (why != 0) *why = kLineOk;
        return true;
    }
    if (tag == "F")
    {
        unsigned no = 0, rows = 0; std::string snap; std::vector<char> v;
        if (!(is >> no >> rows >> snap)) return false;
        for (size_t k = 0; k < b->loadRefused.size(); ++k) if (b->loadRefused[k] == no) return false;
        if (snap.size() > 2 * (size_t)kMaxSnapshot || !Unhex(snap, &v) || v.empty()) return false;
        for (size_t t = 0; t < b->teams.size(); ++t)
            if (b->teams[t].no == no)
            {
                if (!b->teams[t].founderSnap.empty()) { if (why != 0) *why = kLineDuplicate; return false; }
                b->teams[t].founderSnapRows = rows; b->teams[t].founderSnap = v;
                if (why != 0) *why = kLineOk;
                return true;
            }
        return false;
    }
    if (tag == "M")
    {
        unsigned no = 0; std::string id, snap; Member m; unsigned long long at = 0;
        if (!(is >> no >> id >> at >> m.snapRows >> snap)) return false;
        for (size_t k = 0; k < b->loadRefused.size(); ++k) if (b->loadRefused[k] == no) return false;
        if (snap.size() > 2 * (size_t)kMaxSnapshot || !UnhexS(id, &m.id) || !Unhex(snap, &m.snap) || m.id.empty() || TeamOf(*b, m.id) >= 0) return false;
        m.joinedAt = (long long)at;
        for (size_t t = 0; t < b->teams.size(); ++t)
            if (b->teams[t].no == no)
            {
                if (b->teams[t].members.size() >= kMaxMembers) return false;
                b->teams[t].members.push_back(m);
                if (why != 0) *why = kLineOk;
                return true;
            }
        return false;
    }
    if (tag == "R")
    {
        unsigned no = 0; std::string hex; std::vector<char> v; TeamRec r;
        if (!(is >> no >> hex)) return false;
        for (size_t k = 0; k < b->loadRefused.size(); ++k) if (b->loadRefused[k] == no) return false;
        if (hex.size() > 2 * (size_t)kMaxSnapshot || !Unhex(hex, &v) || !DecodeRec(v, &r)) return false;
        for (size_t t = 0; t < b->teams.size(); ++t)
            if (b->teams[t].no == no)
            {
                const TeamRec& h = b->teams[t].rec;
                if (h.seeded || !h.rows.empty() || !h.stances.empty()) { if (why != 0) *why = kLineDuplicate; return false; }
                b->teams[t].rec = r;
                if (why != 0) *why = kLineOk;
                return true;
            }
        return false;
    }
    if (tag == "Q")
    {
        unsigned no = 0; std::string hex, sid;
        if (!(is >> no >> hex)) return false;
        for (size_t k = 0; k < b->loadRefused.size(); ++k) if (b->loadRefused[k] == no) return false;
        if (hex.size() > 2 * (size_t)kMaxName || !UnhexS(hex, &sid) || sid.empty()) return false;
        for (size_t t = 0; t < b->teams.size(); ++t)
            if (b->teams[t].no == no)
            {
                std::vector<std::string>& r = b->teams[t].research;
                std::vector<std::string>::iterator at = std::lower_bound(r.begin(), r.end(), sid);
                if (at != r.end() && *at == sid) { if (why != 0) *why = kLineDuplicate; return false; }
                if (r.size() >= kMaxTechs) return false;
                r.insert(at, sid);
                if (why != 0) *why = kLineOk;
                return true;
            }
        return false;
    }
    if (tag == "S")
    {
        unsigned no = 0, os = 0, xs = 0, bits = 0, flags = 0; std::string oh, xh; SideSnap x;
        if (!(is >> no >> oh >> os >> xh >> xs >> bits >> flags)) return false;
        for (size_t k = 0; k < b->loadRefused.size(); ++k) if (b->loadRefused[k] == no) return false;
        if (!UnhexS(oh, &x.owner) || !UnhexS(xh, &x.other) || x.owner.empty() || x.other.empty() || x.owner == x.other) return false;
        x.ownerSlot = os; x.otherSlot = xs; std::memcpy(&x.rel, &bits, 4); x.flags = flags;
        for (size_t t = 0; t < b->teams.size(); ++t)
            if (b->teams[t].no == no)
            {
                if (TeamOf(*b, x.owner) != (int)t || TeamOf(*b, x.other) != (int)t || b->teams[t].sides.size() >= kMaxSides) return false;
                for (size_t k = 0; k < b->teams[t].sides.size(); ++k)
                    if (b->teams[t].sides[k].owner == x.owner && b->teams[t].sides[k].other == x.other) { if (why != 0) *why = kLineDuplicate; return false; }
                b->teams[t].sides.push_back(x);
                if (why != 0) *why = kLineOk;
                return true;
            }
        return false;
    }
    if (tag == "O")
    {
        Owed o; std::string id, name, snap; unsigned why2 = 0, away = 0; unsigned long long at = 0;
        if (!(is >> o.no >> id >> why2 >> away >> name >> o.snapRows >> snap >> at) || o.no == 0 || o.no == 0xFFFFFFFFu) return false;
        for (size_t k = 0; k < b->owed.size(); ++k) if (b->owed[k].no == o.no) { if (why != 0) *why = kLineDuplicate; return false; }
        if (snap.size() > 2 * (size_t)kMaxSnapshot || !UnhexS(id, &o.id) || !UnhexS(name, &o.teamName) || !Unhex(snap, &o.snap) || o.id.empty()
            || o.teamName.size() > kMaxName || why2 < kWhyLeft || why2 > kWhyDisbanded || away > 1) return false;
        if (!OwedRoom(*b, o.id)) return false;
        std::string sidesHex;
        if (is >> sidesHex)
        {
            std::vector<char> sb;
            if (sidesHex.size() > 2 * (size_t)kMaxSnapshot || !Unhex(sidesHex, &sb)) return false;
            Cur sc(sb.empty() ? 0 : &sb[0], sb.size()); GetSides(&sc, &o.sides);
            if (sc.bad || sc.at != sb.size()) return false;
        }
        o.why = (int)why2; o.away = (int)away; o.madeAt = (long long)at;
        b->owed.push_back(o); if (o.no >= b->nextOwed) b->nextOwed = o.no + 1;
        if (why != 0) *why = kLineOk;
        return true;
    }
    return false;
}

}   // namespace swteam
