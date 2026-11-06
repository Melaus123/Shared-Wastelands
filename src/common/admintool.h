/* src/common/admintool.h - THE ADMIN TOOLBOX'S ROAD: a name as an admin types it, which player or character it names, the job the
   world server hands to the game that owns the target, and how each job ends. Pure: no Windows.h, no disk, no clock of its own
   (every call takes the time), no engine; the world server (store_main.cpp), the game (admin.cpp) and the offline suite compile it.
   C++03 (VS2010 v100). Ported in part from public pull request 3 (author MassDivide) and changed for this project.

   THE ROAD. The admin's game sends ADMIN_REQ (admincmd.h, 97); the world server checks who may (admincmd::MayAsk), finds the target
   (Resolve) and opens one JOB per game that must act - always the game that OWNS the target, since a game only changes what it owns:
     98 ADMIN_DO   (server -> the owning game): {u32 job, u32 op, u32 num, u32 flags, str8 target, str8 extra, f32 x, f32 y, f32 z}
                   target = the character's name, or "" for the player's lead character (the one the player is controlling);
                   num = /additem's count; flags = kDo*; extra = /additem's item record id; x, y, z = where a move goes
     99 ADMIN_DONE (owning game -> server):    {u32 job, u32 code, u32 num, u32 num2, f32 x, f32 z, str8 place, str8 name, f32 y,
                                                u8 n + n str8 names}
                   code = kDone*; x, y, z = the map position; place = the record name of the town near it ("" = none near);
                   name = the character it was done to; num, num2 = the op's numbers (kOpFind: the characters of that name;
                   /additem: the items that went in; the "all" heal: the characters healed and the dead left; a squad move: the
                   characters moved and the dead who stayed); names = a squad move's characters, the moved ones (the first num)
                   then those knocked out who stayed; /revive: name = the character brought back, names[0] = the one beside it
   THE MOVES. A character is moved only by the game that owns it (kOpMove, kOpSquadMove, at the K2 safe point, through the engine's
   own teleport); the other games' copies follow through the existing move records. /bring and /goto are two jobs in a row
   (ChainDo): the position of one character (kOpPos, on the game that owns it), then the move on the other game - /bring asks the
   admin's own game where its lead character is and moves the target there; /goto asks the target's game and moves the admin's own
   lead character. /tp moves the target to the typed map position. /sethome asks the admin's own game for its lead character's
   position and the world server keeps it (homes.txt, Homes*); /home and /jail move a whole squad (kOpSquadMove: the lead
   character's squad, laid out in rows by SquadSpot; the knocked-out and the dead stay where they are). A knocked-out character is
   never moved while it is down (the engine's teleport leaves its body behind and it snaps back). /bring and /tp heal it first
   (kDoHealFirst, mockups-t1205.md section 6): the owning game heals it as /heal does, at the same safe point, and moves it once the
   engine's own medical tick has stood it up - tried again at each safe point for at most kStandWaitMs (MoveNextOf), well inside the
   job's kJobWaitSec. Still down by then: the admin is told it is still knocked out (section 13). /goto, /home and /jail heal nobody
   first: a knocked-out character stays and the admin is told so. A dead character is never moved.
   THE CHARACTER SEARCH (Searches). The world server keeps no character names, so a name that may be a character's is asked of every
   game in the world (for #character@player, of that player's game alone) as a kOpFind job: each game counts its own player
   characters of that name. When every one has answered, Resolve decides and the command's job goes to the one owning game. A game
   that does not answer (refused, lapsed or gone) ends the search with nothing done: the admin is told that game did not answer.
   The world server answers the admin with ADMIN_REPLY (admincmd.h, 100): a reply CODE and values, never words; the admin's game
   shows its own approved words (adminwords.h).
   /GOD (kOpGod, mockups-t1205.md section 12): the job goes to the game that owns the character, like /heal's; num = 1 on, 0 off.
   That game alone keeps the god mode, in memory (medical.cpp's god table, never saved): set or cleared by the job (GodNext), gone
   when that player leaves the world or the character's ownership moves; the character's STATE carries it to every other game
   (statewire.h kGodBit) so their AI ignores it too. ADMIN_DONE's num echoes on / off.
   /REVIVE (kOpRevive). The world server keeps every player's fallen list (fallenwire.h Book), so it finds which player's list holds
   the name itself (ReviveResolve) - no search - and hands the job to that player's game, which brings back the newest row of that
   name on the FALLEN tab's own road (resurrect.cpp), beside one of that player's free living characters. An admin's job is free
   (owner 1394) and is not refused by the world's resurrection option (owner 1398); the FALLEN tab's own bring-back keeps both
   (fallentab.h OffRefuses, PriceToPay). The player is told (owner 1396).

   EVERY JOB ENDS ONE WAY (Jobs):
     accepted - its owning game's ADMIN_DONE came, from that game's own connection: its code becomes the admin's reply
     refused  - the same, with a code other than kDoneOk: the reply says it did not work on that game (DoneReply)
     lapsed   - kJobWaitSec passed with no ADMIN_DONE, or the owning game left the world first: the reply says no answer came
                (LapseReply). An ADMIN_DONE for a job that ended is not acted on.
   A job whose admin left ends with nobody told (there is nobody to tell).
   REPEATS ARE HARMLESS. Job numbers are unique for the whole run of a world server (JobOpen never hands out 0 or a number still
   open). The owning game keeps the ADMIN_DONE of its last kMemoRows jobs on this link (Memo) and answers an ADMIN_DO it has finished
   with that same ADMIN_DONE, without acting again; one it is still working on is not queued twice. A new link starts an empty memo:
   a link never spans two runs of a world server, whose numbers start again. */
#ifndef SW_ADMINTOOL_H
#define SW_ADMINTOOL_H

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "admincmd.h"

namespace admintool {

const unsigned kMsgAdminDo = 98;
const unsigned kMsgAdminDone = 99;

enum Op { kOpNone = 0, kOpWhere = 1, kOpFind = 2, kOpMedic = 3, kOpHeal = 4, kOpAddItem = 5,
          kOpPos = 6,         /* where a character is (target, or "" = the lead character): name, x, y, z and the town near it */
          kOpMove = 7,        /* move one character (target, or "" = the lead character) to x, y, z (kDoNear: beside it) */
          kOpSquadMove = 8,   /* move the squad of the lead character (target "") to x, y, z, laid out by SquadSpot */
          kOpRevive = 9,      /* bring back the newest fallen character called target on this player's fallen list - an admin's job */
          kOpRestore = 10,    /* /restore: /heal's job, and every lost limb of the character grown back (a robotic limb is left on) */
          kOpGod = 11,        /* /god: the character's god mode on (num 1) or off (num 0) on the game that owns it */
          kOpCount = 12 };
enum DoneCode { kDoneOk = 0,          /* done: the values are the answer */
                kDoneNoLead = 1,      /* the player has no lead character on its game now */
                kDoneFailed = 2,      /* the owning game could not do it (a read or an engine call failed) */
                kDoneUnknownOp = 3,   /* the owning game does not know that op */
                kDoneDead = 4,        /* /medic, /heal, a move, kOpPos, /god on: the character is dead - nothing done */
                kDoneOut = 5,         /* /medic, /heal: healed, and the character is still knocked out */
                kDoneNotFound = 6,    /* /medic, /heal, a move, kOpPos: no own character of that name now (or more than one) - nothing done;
                                         /revive: no fallen character of that name on this game's copy of the list */
                kDoneDown = 7,        /* a move: the character (a squad move: every living one) is knocked out - not moved; name = it */
                kDoneNoMate = 8,      /* /revive: no free living character of this player to come back beside - nothing done */
                kDoneFull = 9,        /* /revive: this player has as many characters as the game lets it lead - nothing done */
                kDoneAlready = 10,    /* /god: the character's god mode was already as asked - nothing changed; num = on / off asked */
                kDoneCount = 11 };
const unsigned kDoAll = 1u;           /* ADMIN_DO flags: every character of the player's squad (target "") */
const unsigned kDoAskerHere = 2u;     /* ... the admin who asked plays on this same game (no line to the target player) */
const unsigned kDoHaveY = 4u;         /* ... y is a height to land at where this game reads no ground height at x, z */
const unsigned kDoNear = 8u;          /* ... kOpMove: land kNearUnits beside x, z, not on it */
const unsigned kDoHealFirst = 16u;    /* ... kOpMove (/bring, /tp): a knocked-out character is healed first and moved once it stands */
const unsigned kDoFlagsKnown = kDoAll | kDoAskerHere | kDoHaveY | kDoNear | kDoHealFirst;
const float kNearUnits = 50.0f;       /* /bring, /goto: this far beside the other character (about 5 m; Inferred scale) */
const float kSquadSpacing = 25.0f;    /* a squad move: its characters this far apart, in rows of four */
const size_t kSquadNamesMax = 32;     /* a squad move's answer: at most this many names moved, and this many that stayed */
const int kAddItemMax = 99;           /* /additem's count: 1 to this */
const size_t kItemListMax = 8;        /* "More than one item is called ...": at most this many names */
const double kJobWaitSec = 25.0;      /* a job with no ADMIN_DONE by then has lapsed */
const long kStandWaitMs = 10000;      /* a healed-first move waits at most this long for the character to stand (well inside kJobWaitSec) */
/* a job queued on the owning game kJobWaitSec ago (GetTickCount ms) or more: the world server has told its admin it lapsed, so the
   game drops it, never acts on it late */
inline bool DoStale(unsigned queuedMs, unsigned nowMs) { return (double)(unsigned)(nowMs - queuedMs) >= kJobWaitSec * 1000.0; }
const size_t kMemoRows = 64;          /* the finished jobs a game remembers on its link */
const size_t kNameChars = 32;         /* a typed name: at most this many characters */
const size_t kNameBytes = admincmd::kTargetMax;   /* ... and so at most this many bytes (4 a character) */
const float kCoordMax = 1.0e7f;       /* a position past this is not one */

/* ---- a name as an admin types it ----
   plain        Ana  or  "Old Tam"     a player (meaning that player's lead character) or a character
   @player      @Ana or  @"Old Tam"    a player only
   #character   #Ruk or  #"Old Tam"    a character only
   #character@player  #Ruk@Ana  or  #"Old Tam"@"Big Al"   that player's character */
enum Pick { kPickAny = 0, kPickPlayer = 1, kPickChar = 2 };
struct Name { int pick; std::string name, player; Name() : pick(kPickAny) {} };
enum { kNameOk = 0, kNameNone = 1, kNameNoClose = 2, kNameTooLong = 3 };

/* characters in a UTF-8 text (bytes that do not continue a character) */
inline size_t Chars(const std::string& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) if (((unsigned char)s[i] & 0xC0) != 0x80) ++n;
    return n;
}
/* one part of a name at s[*at]: "quoted" (to the next quote) or a bare word ending at a space, or also at '@' when stopAtAt.
   kNameOk with *part filled (control bytes dropped, trimmed), kNameNoClose, kNameTooLong */
inline int NamePart(const std::string& s, size_t* at, bool stopAtAt, std::string* part)
{
    part->clear();
    std::string raw;
    if (*at < s.size() && s[*at] == '"')
    {
        const size_t q = s.find('"', *at + 1);
        if (q == std::string::npos) return kNameNoClose;
        raw = s.substr(*at + 1, q - *at - 1);
        *at = q + 1;
    }
    else
    {
        size_t b = *at;
        while (b < s.size() && !admincmd::IsSpace(s[b]) && !(stopAtAt && s[b] == '@')) ++b;
        raw = s.substr(*at, b - *at);
        *at = b;
    }
    std::string clean;
    for (size_t i = 0; i < raw.size(); ++i) { const unsigned char c = (unsigned char)raw[i]; if (c >= 0x20 && c != 0x7F) clean += (char)c; }
    clean = admincmd::Trim(clean);
    if (Chars(clean) > kNameChars || clean.size() > kNameBytes) return kNameTooLong;
    *part = clean;
    return kNameOk;
}
/* the name at the start of `rest` (the words after the command): kNameOk with *out filled and *after = what follows it (trimmed);
   kNameNone = no name (nothing typed, or an empty name); kNameNoClose = a quote is not closed; kNameTooLong = past kNameChars */
inline int ParseName(const std::string& rest, Name* out, std::string* after)
{
    *out = Name(); after->clear();
    const std::string t = admincmd::Trim(rest);
    if (t.empty()) return kNameNone;
    size_t at = 0;
    if (t[0] == '@') { out->pick = kPickPlayer; at = 1; }
    else if (t[0] == '#') { out->pick = kPickChar; at = 1; }
    int r = NamePart(t, &at, out->pick == kPickChar, &out->name);
    if (r != kNameOk) return r;
    if (out->pick == kPickChar && at < t.size() && t[at] == '@')
    {
        ++at;
        r = NamePart(t, &at, false, &out->player);
        if (r != kNameOk) return r;
        if (out->player.empty()) return kNameNone;
    }
    if (out->name.empty()) return kNameNone;
    *after = admincmd::Trim(t.substr(at));
    return kNameOk;
}
/* two names are the same name: ASCII case does not count, nor spaces at either end */
inline bool SameName(const std::string& a, const std::string& b)
{
    const std::string x = admincmd::LowerAscii(admincmd::Trim(a)), y = admincmd::LowerAscii(admincmd::Trim(b));
    return !x.empty() && x == y;
}

/* ---- which player or character a name names (the world server's decision) ----
   players: every admitted game - its slot, its player's name, and whether it is in the world now (only a game in the world acts).
   chars: each game's count of its OWN characters carrying the name, when the characters were searched; 0 = they were not (a
   command that names players only), and then every name is a player's. A game's count that includes that game's own player
   (a player whose lead carries the player's own name) is the same target as the player, never a second one. */
struct Player { int slot; std::string name; bool inWorld; Player() : slot(-1), inWorld(false) {} };
struct CharCount { int slot; int count; CharCount() : slot(-1), count(0) {} };
enum { kResOne = 0,            /* one target: slot, character (false = the player's lead character) */
       kResNone = 1,           /* no player or character by that name */
       kResNotInWorld = 2,     /* a player by that name is admitted but not in the world now: shown = that name */
       kResManyPlayers = 3,    /* more than one player in the world has that name */
       kResPlayerAndChar = 4,  /* a player has that name and another player's character too: owner = that player */
       kResManyChars = 5,      /* characters of that name on more than one game: owners = their players, one per player, those with
                                  one such character first (a hint names owners[0], so it answers) */
       kResUnanswered = 6,     /* a search: a game did not answer - shown = its player; nothing done */
       kResManyInSquad = 7 };  /* more than one character of that name, all of one player's: owner = that player (no hint names
                                  them apart - mockups-t1205.md section 14) */
struct Resolved
{
    int res, slot; bool character; std::string shown, owner; std::vector<std::string> owners;
    Resolved() : res(kResNone), slot(-1), character(false) {}
};
inline const Player* PlayerOfSlot(const std::vector<Player>& players, int slot)
{
    for (size_t i = 0; i < players.size(); ++i) if (players[i].slot == slot) return &players[i];
    return 0;
}
/* the players called `name`: in the world (*inWorld, their slots) and admitted but not in the world (*away) */
inline void PlayersCalled(const std::vector<Player>& players, const std::string& name, std::vector<int>* inWorld, int* away)
{
    inWorld->clear(); *away = 0;
    for (size_t i = 0; i < players.size(); ++i)
    {
        if (!SameName(players[i].name, name)) continue;
        if (players[i].inWorld) inWorld->push_back(players[i].slot); else ++*away;
    }
}
inline Resolved Resolve(const Name& n, const std::vector<Player>& players, const std::vector<CharCount>* chars)
{
    Resolved r; r.shown = n.name;
    std::vector<int> called; int away = 0;
    if (n.pick == kPickChar && !n.player.empty())
    {   /* #character@player: that player's own game alone */
        PlayersCalled(players, n.player, &called, &away);
        if (called.size() > 1) { r.res = kResManyPlayers; r.shown = n.player; return r; }
        if (called.empty()) { r.res = away > 0 ? kResNotInWorld : kResNone; if (away > 0) r.shown = n.player; return r; }
        if (chars != 0) for (size_t i = 0; i < chars->size(); ++i)
        {
            const CharCount& c = (*chars)[i];
            if (c.slot != called[0] || c.count <= 0) continue;
            if (c.count == 1) { r.res = kResOne; r.slot = called[0]; r.character = true; return r; }
            r.res = kResManyInSquad; r.owner = PlayerOfSlot(players, called[0])->name;
            return r;
        }
        r.res = kResNone; return r;
    }
    if (n.pick != kPickChar) PlayersCalled(players, n.name, &called, &away);
    if (called.size() > 1) { r.res = kResManyPlayers; return r; }
    std::vector<int> games, single;   /* games with such a character (the named player's own game left out); those with just one */
    int total = 0;
    if (n.pick != kPickPlayer && chars != 0)
        for (size_t i = 0; i < chars->size(); ++i)
        {
            const CharCount& c = (*chars)[i];
            const Player* p = PlayerOfSlot(players, c.slot);
            if (c.count <= 0 || p == 0 || !p->inWorld) continue;
            if (!called.empty() && c.slot == called[0]) continue;
            if (std::find(games.begin(), games.end(), c.slot) != games.end()) continue;   /* a game answers once */
            games.push_back(c.slot); total += c.count;
            if (c.count == 1) single.push_back(c.slot);
        }
    if (called.size() == 1)
    {
        if (!games.empty()) { r.res = kResPlayerAndChar; r.owner = PlayerOfSlot(players, games[0])->name; return r; }
        r.res = kResOne; r.slot = called[0]; return r;
    }
    if (total == 1) { r.res = kResOne; r.slot = games[0]; r.character = true; return r; }
    if (games.size() == 1) { r.res = kResManyInSquad; r.owner = PlayerOfSlot(players, games[0])->name; return r; }
    if (games.size() > 1)
    {   /* one per player, those with one character of the name first: the hint's #name@player then names exactly one */
        r.res = kResManyChars;
        for (size_t i = 0; i < single.size() && r.owners.size() < admincmd::kOthersMax; ++i) r.owners.push_back(PlayerOfSlot(players, single[i])->name);
        for (size_t i = 0; i < games.size() && r.owners.size() < admincmd::kOthersMax; ++i)
            if (std::find(single.begin(), single.end(), games[i]) == single.end()) r.owners.push_back(PlayerOfSlot(players, games[i])->name);
        return r;
    }
    r.res = away > 0 ? kResNotInWorld : kResNone;
    return r;
}

/* ---- the wire ---- */
struct Do
{
    unsigned job, op, num, flags; std::string target, extra; float x, y, z;
    Do() : job(0), op(0), num(0), flags(0), x(0.0f), y(0.0f), z(0.0f) {}
};
struct Done
{
    unsigned job, code, num, num2; float x, z; std::string place, name; float y; std::vector<std::string> names;
    Done() : job(0), code(0), num(0), num2(0), x(0.0f), z(0.0f), y(0.0f) {}
};
/* a usable map position: a number, and not past kCoordMax */
inline bool CoordOk(float f) { return f == f && f <= kCoordMax && f >= -kCoordMax; }
inline bool EncodeDo(const Do& d, std::vector<char>* b)
{
    b->clear(); admincmd::PutU32(b, d.job); admincmd::PutU32(b, d.op); admincmd::PutU32(b, d.num); admincmd::PutU32(b, d.flags);
    if (!admincmd::PutStr8(b, d.target, kNameBytes) || !admincmd::PutStr8(b, d.extra, kNameBytes)) return false;
    admincmd::PutF32(b, d.x); admincmd::PutF32(b, d.y); admincmd::PutF32(b, d.z);
    return true;
}
/* the whole payload, nothing left over; a job number; known flags; no control byte in a text. An op this build does not know still
   decodes, so the game can answer kDoneUnknownOp. */
inline bool DecodeDo(const char* p, size_t n, Do* d)
{
    size_t at = 0;
    if (p == 0 && n != 0) return false;
    if (!admincmd::GetU32(p, n, &at, &d->job) || !admincmd::GetU32(p, n, &at, &d->op) || !admincmd::GetU32(p, n, &at, &d->num) || !admincmd::GetU32(p, n, &at, &d->flags)) return false;
    if (!admincmd::GetStr8(p, n, &at, kNameBytes, &d->target) || !admincmd::GetStr8(p, n, &at, kNameBytes, &d->extra)) return false;
    if (!admincmd::GetF32(p, n, &at, &d->x) || !admincmd::GetF32(p, n, &at, &d->y) || !admincmd::GetF32(p, n, &at, &d->z)) return false;
    if (!CoordOk(d->x) || !CoordOk(d->y) || !CoordOk(d->z)) return false;
    return at == n && d->job != 0 && (d->flags & ~kDoFlagsKnown) == 0;
}
inline bool EncodeDone(const Done& d, std::vector<char>* b)
{
    b->clear(); admincmd::PutU32(b, d.job); admincmd::PutU32(b, d.code); admincmd::PutU32(b, d.num); admincmd::PutU32(b, d.num2);
    admincmd::PutF32(b, d.x); admincmd::PutF32(b, d.z);
    if (!admincmd::PutStr8(b, d.place, kNameBytes) || !admincmd::PutStr8(b, d.name, kNameBytes)) return false;
    admincmd::PutF32(b, d.y);
    if (d.names.size() > 2 * kSquadNamesMax) return false;
    b->push_back((char)(unsigned char)d.names.size());
    for (size_t i = 0; i < d.names.size(); ++i) if (!admincmd::PutStr8(b, d.names[i], kNameBytes)) return false;
    return true;
}
inline bool DecodeDone(const char* p, size_t n, Done* d)
{
    size_t at = 0;
    if (p == 0 && n != 0) return false;
    if (!admincmd::GetU32(p, n, &at, &d->job) || !admincmd::GetU32(p, n, &at, &d->code) || !admincmd::GetU32(p, n, &at, &d->num) || !admincmd::GetU32(p, n, &at, &d->num2)) return false;
    if (!admincmd::GetF32(p, n, &at, &d->x) || !admincmd::GetF32(p, n, &at, &d->z)) return false;
    if (!admincmd::GetStr8(p, n, &at, kNameBytes, &d->place) || !admincmd::GetStr8(p, n, &at, kNameBytes, &d->name)) return false;
    if (!admincmd::GetF32(p, n, &at, &d->y) || at >= n) return false;
    const size_t k = (size_t)(unsigned char)p[at]; ++at;
    if (k > 2 * kSquadNamesMax) return false;
    d->names.assign(k, std::string());
    for (size_t i = 0; i < k; ++i) if (!admincmd::GetStr8(p, n, &at, kNameBytes, &d->names[i])) return false;
    if (at != n || d->job == 0 || d->code >= (unsigned)kDoneCount) return false;
    return d->code != (unsigned)kDoneOk || (CoordOk(d->x) && CoordOk(d->y) && CoordOk(d->z));
}

/* ---- the world server's job table ----
   target / asker: the connections of the owning game and of the admin, as the world server keys them (never followed here).
   search: the character search this kOpFind job answers (0 = none); slot: the owning game's slot; flags: its ADMIN_DO's kDo*;
   player: the owning game's player (the name a "did not work on that game" reply names). /bring and /goto's first job (kOpPos):
   nextSlot = the game whose character moves next (-1 = no next job), nextTarget = that character ("" = its lead character),
   nextShown = the name its reply names; other = the further name the last job's reply names (ChainDo). */
struct Job
{
    unsigned job, op, req, verb, search, flags; int slot; unsigned long long target, asker; std::string shown; double openedSec;
    std::string player, other, nextTarget, nextShown; int nextSlot;
    Job() : job(0), op(0), req(0), verb(0), search(0), flags(0), slot(-1), target(0), asker(0), openedSec(0.0), nextSlot(-1) {}
};
struct Jobs { unsigned last; std::vector<Job> open; Jobs() : last(0) {} };
inline int JobIndex(const Jobs& t, unsigned job)
{
    for (size_t i = 0; i < t.open.size(); ++i) if (t.open[i].job == job) return (int)i;
    return -1;
}
/* opens `j` at `now` under a new number (never 0, never one still open) and returns it */
inline unsigned JobOpen(Jobs* t, Job j, double now)
{
    do { ++t->last; } while (t->last == 0 || JobIndex(*t, t->last) >= 0);
    j.job = t->last; j.openedSec = now;
    t->open.push_back(j);
    return j.job;
}
enum { kTakeAccepted = 0, kTakeNotOpen = 1, kTakeWrongGame = 2 };
/* an ADMIN_DONE for `job` from the connection `from`: kTakeAccepted (the job ends; *out = it), kTakeNotOpen (never opened, or ended
   already - not acted on), kTakeWrongGame (not the owning game's connection - not acted on, the job stays open) */
inline int JobTake(Jobs* t, unsigned job, unsigned long long from, Job* out)
{
    const int k = JobIndex(*t, job);
    if (k < 0) return kTakeNotOpen;
    if (t->open[(size_t)k].target != from) return kTakeWrongGame;
    *out = t->open[(size_t)k];
    t->open.erase(t->open.begin() + k);
    return kTakeAccepted;
}
/* the jobs that lapsed by `now` (kJobWaitSec with no answer), taken out of the table */
inline std::vector<Job> JobsLapsed(Jobs* t, double now)
{
    std::vector<Job> out, keep;
    for (size_t i = 0; i < t->open.size(); ++i) (now - t->open[i].openedSec >= kJobWaitSec ? out : keep).push_back(t->open[i]);
    t->open.swap(keep);
    return out;
}
/* a connection went: the jobs its game owned lapse now (*lapsed - their admins are told); the jobs it asked end untold */
inline void JobsConnGone(Jobs* t, unsigned long long conn, std::vector<Job>* lapsed)
{
    lapsed->clear();
    std::vector<Job> keep;
    for (size_t i = 0; i < t->open.size(); ++i)
    {
        if (t->open[i].asker == conn) continue;
        if (t->open[i].target == conn) lapsed->push_back(t->open[i]); else keep.push_back(t->open[i]);
    }
    t->open.swap(keep);
}
/* the admin's reply code for a job that lapsed */
inline int LapseReply(unsigned op) { return op == (unsigned)kOpWhere ? (int)admincmd::kRepWhereNoAnswer : (int)admincmd::kRepLapsed; }
/* the admin's reply code for a job's ADMIN_DONE */
inline int DoneReply(unsigned op, unsigned code)
{
    if (code == (unsigned)kDoneNoLead && op == (unsigned)kOpWhere) return admincmd::kRepWhereNoChar;      /* the player has no character in the world */
    if (code == (unsigned)kDoneNoLead && op == (unsigned)kOpAddItem) return admincmd::kRepNoCharSelected; /* the admin's own game has none selected */
    if (code != (unsigned)kDoneOk) return admincmd::kRepFailedOnGame;
    return op == (unsigned)kOpWhere ? (int)admincmd::kRepWhereAt : (int)admincmd::kRepOk;
}
/* /medic, /heal and /restore: the three strengths of healing, one road (mockups-t1205.md section 4) */
inline bool HealOp(unsigned op) { return op == (unsigned)kOpMedic || op == (unsigned)kOpHeal || op == (unsigned)kOpRestore; }
inline bool HealVerb(unsigned verb) { return verb == (unsigned)admincmd::kVerbMedic || verb == (unsigned)admincmd::kVerbHeal || verb == (unsigned)admincmd::kVerbRestore; }
/* a healing verb's job on the owning game; kOpNone = not a healing verb */
inline unsigned HealOpOf(unsigned verb)
{
    if (verb == (unsigned)admincmd::kVerbMedic) return kOpMedic;
    if (verb == (unsigned)admincmd::kVerbHeal) return kOpHeal;
    if (verb == (unsigned)admincmd::kVerbRestore) return kOpRestore;
    return kOpNone;
}
/* the whole reply for a job's ADMIN_DONE: its code, and the name it names (*name) - the character the game did it to, or the player
   the job was opened for (`shown`) when the game named none or healed a whole squad */
inline bool MoveOp(unsigned op) { return op == (unsigned)kOpMove || op == (unsigned)kOpSquadMove; }
inline int MoveReply(const Job& j, const Done& d, std::string* name)
{
    *name = !d.name.empty() ? d.name : j.shown;
    if (d.code == (unsigned)kDoneDown) return admincmd::kRepStillDown;
    if (d.code == (unsigned)kDoneDead) return admincmd::kRepDead;
    if (d.code == (unsigned)kDoneNotFound) return admincmd::kRepNoSuchName;
    if (d.code == (unsigned)kDoneOk)
    {
        if (j.verb == (unsigned)admincmd::kVerbBring) return d.num == 1 ? admincmd::kRepBroughtHealed : admincmd::kRepBrought;
        if (j.verb == (unsigned)admincmd::kVerbTp) return admincmd::kRepMovedTo;
        if (j.verb == (unsigned)admincmd::kVerbGoto) return admincmd::kRepWent;
        if (j.verb == (unsigned)admincmd::kVerbHome) return admincmd::kRepWentHome;
        if (j.verb == (unsigned)admincmd::kVerbJail) { *name = j.shown; return admincmd::kRepJailed; }
    }
    *name = j.player.empty() ? j.shown : j.player;
    return admincmd::kRepFailedOnGame;
}
/* /revive's reply: the character (the game's name for it, else the one typed), or the player for "no living character" and "squad
   full"; any other ending is "not brought back" (nothing was done, nothing paid) */
inline int ReviveReply(const Job& j, const Done& d, std::string* name)
{
    *name = !d.name.empty() ? d.name : j.shown;
    if (d.code == (unsigned)kDoneOk) return admincmd::kRepRevived;
    if (d.code == (unsigned)kDoneNotFound) return admincmd::kRepNoFallen;
    if (d.code == (unsigned)kDoneNoMate) { *name = j.player; return admincmd::kRepReviveNoMate; }
    if (d.code == (unsigned)kDoneFull) { *name = j.player; return admincmd::kRepSquadFull; }
    return admincmd::kRepNotRevived;
}
/* /revive's reply's further name (others[0]): the character it came back beside (kRepRevived), or the fallen character
   (kRepReviveNoMate, whose name is the player); "" = none */
inline std::string ReviveOther(const Job& j, const Done& d, int code)
{
    if (code == admincmd::kRepRevived) return d.names.empty() ? std::string() : d.names[0];
    if (code == admincmd::kRepReviveNoMate) return !d.name.empty() ? d.name : j.shown;
    return std::string();
}
/* /god on the owning game: what the job does (mockups-t1205.md section 12). A dead character is never MADE a god (revive it first); a
   dead one whose god mode is on may still have it switched off, so a row never sticks. */
enum GodStep { kGodSet = 0, kGodAlready = 1, kGodDead = 2 };
inline int GodNext(bool wantOn, bool isOn, bool dead)
{
    if (wantOn == isOn) return kGodAlready;
    if (wantOn && dead) return kGodDead;
    return kGodSet;
}
/* /god's reply: the character (the game's name for it, else the one typed); on / off from the job's ADMIN_DONE num */
inline int GodReply(const Job& j, const Done& d, std::string* name)
{
    *name = !d.name.empty() ? d.name : j.shown;
    const bool on = d.num != 0;
    if (d.code == (unsigned)kDoneOk) return on ? admincmd::kRepGodOn : admincmd::kRepGodOff;
    if (d.code == (unsigned)kDoneAlready) return on ? admincmd::kRepGodAlready : admincmd::kRepGodNotOn;
    if (d.code == (unsigned)kDoneDead) return admincmd::kRepGodDead;
    if (d.code == (unsigned)kDoneNotFound) return admincmd::kRepNoSuchName;
    *name = j.player.empty() ? j.shown : j.player;
    return admincmd::kRepFailedOnGame;
}
inline int JobReply(const Job& j, const Done& d, std::string* name)
{
    if (j.op == (unsigned)kOpRevive) return ReviveReply(j, d, name);
    if (j.op == (unsigned)kOpGod) return GodReply(j, d, name);
    if (MoveOp(j.op)) return MoveReply(j, d, name);
    if (j.op == (unsigned)kOpPos)
    {   /* /sethome's job (the world server keeps the home first), or a first job whose next one could not be opened */
        if (d.code == (unsigned)kDoneOk && j.verb == (unsigned)admincmd::kVerbSetHome) { *name = j.shown; return admincmd::kRepHomeSet; }
        if (d.code == (unsigned)kDoneNotFound) { *name = j.shown; return admincmd::kRepNoSuchName; }
        if (d.code == (unsigned)kDoneDead) { *name = !d.name.empty() ? d.name : j.shown; return admincmd::kRepDead; }   /* /goto a dead one */
        *name = j.player.empty() ? j.shown : j.player;
        return admincmd::kRepFailedOnGame;
    }
    const bool heal = HealOp(j.op);
    const bool all = heal && (j.flags & kDoAll) != 0;
    *name = (!all && !d.name.empty()) ? d.name : j.shown;
    if (heal && !all && d.code == (unsigned)kDoneDead) return admincmd::kRepDead;
    if (heal && !all && d.code == (unsigned)kDoneOut) return admincmd::kRepHealedOut;
    if (heal && !all && d.code == (unsigned)kDoneNotFound) return admincmd::kRepNoSuchName;
    if (all && d.code == (unsigned)kDoneOk) return admincmd::kRepSquadHealed;
    return DoneReply(j.op, d.code);
}

/* ---- the world server's character searches ----
   One per command whose name may be a character's: the games still to answer (waiting, by slot) and what each said. */
struct Search
{
    unsigned no, req, verb, flags; unsigned long long asker; Name n; std::vector<CharCount> counts; std::vector<int> waiting;
    std::string unanswered; float x, z;   /* x, z: /tp's typed position, kept for the command's job */
    Search() : no(0), req(0), verb(0), flags(0), asker(0), x(0.0f), z(0.0f) {}
};
struct Searches { unsigned last; std::vector<Search> open; Searches() : last(0) {} };
inline int SearchIndex(const Searches& t, unsigned no)
{
    for (size_t i = 0; i < t.open.size(); ++i) if (t.open[i].no == no) return (int)i;
    return -1;
}
/* opens `s` (its `waiting` filled) under a new number (never 0, never one still open) and returns it */
inline unsigned SearchOpen(Searches* t, Search s)
{
    do { ++t->last; } while (t->last == 0 || SearchIndex(*t, t->last) >= 0);
    s.no = t->last;
    t->open.push_back(s);
    return s.no;
}
/* the game at `slot` ended its part of search `no`: answered with `count` characters of the name, or not (refused, lapsed or gone -
   `player` = its player). True = the search has every answer now: *out = it, taken out of the table. A search already ended, or a
   slot it no longer waits on, changes nothing. */
inline bool SearchNote(Searches* t, unsigned no, int slot, bool answered, int count, const std::string& player, Search* out)
{
    const int k = SearchIndex(*t, no);
    if (k < 0) return false;
    Search& s = t->open[(size_t)k];
    bool was = false;
    for (size_t i = 0; i < s.waiting.size(); ++i) if (s.waiting[i] == slot) { s.waiting.erase(s.waiting.begin() + i); was = true; break; }
    if (!was) return false;
    if (answered) { CharCount c; c.slot = slot; c.count = count; s.counts.push_back(c); }
    else if (s.unanswered.empty()) s.unanswered = player;
    if (!s.waiting.empty()) return false;
    *out = s;
    t->open.erase(t->open.begin() + k);
    return true;
}
/* the admin's connection went: the searches it asked end untold */
inline void SearchesAskerGone(Searches* t, unsigned long long asker)
{
    std::vector<Search> keep;
    for (size_t i = 0; i < t->open.size(); ++i) if (t->open[i].asker != asker) keep.push_back(t->open[i]);
    t->open.swap(keep);
}
/* a search with every answer: which player or character it names (kResUnanswered when a game did not answer) */
inline Resolved SearchResolve(const Search& s, const std::vector<Player>& players)
{
    if (!s.unanswered.empty()) { Resolved r; r.res = kResUnanswered; r.shown = s.unanswered; return r; }
    return Resolve(s.n, players, &s.counts);
}

/* ---- a character by name on its owning game, the living and the fallen (T-1205c, run T1899) ----
   A dead character leaves the engine's character-update list (findings: "dead characters leave the engine's character-update
   list"), so the game's own living list never holds it; its row on this game's fallen list does. live: the own characters of the
   update list with that name; fallen: this game's fallen-list rows of that name whose uid is not among them (FallenNotLive). */
enum NamedPick { kNamedNone = 0, kNamedOne = 1, kNamedMany = 2, kNamedDead = 3 };
/* one living character = it; several = not one; none living but a fallen row = dead (the dead refusal, never "no such name") */
inline int NamedPickOf(int live, int fallen)
{
    if (live == 1) return kNamedOne;
    if (live > 1) return kNamedMany;
    return fallen > 0 ? kNamedDead : kNamedNone;
}
/* what the character search counts on this game (kOpFind's num): the living of that name; with none, one for the fallen of it
   (however many rows - its job answers dead) */
inline unsigned NamedCount(int live, int fallen)
{
    if (live > 0) return (unsigned)live;
    return fallen > 0 ? 1u : 0u;
}
/* the fallen rows (names[i], uids[i]) called `typed` ("" = every row) whose uid is not one of liveUids */
inline int FallenNotLive(const std::vector<std::string>& names, const std::vector<unsigned>& uids, const std::vector<unsigned>& liveUids,
                         const std::string& typed)
{
    int n = 0;
    for (size_t i = 0; i < names.size() && i < uids.size(); ++i)
    {
        if (!typed.empty() && !SameName(names[i], typed)) continue;
        bool live = false;
        for (size_t k = 0; k < liveUids.size(); ++k) if (liveUids[k] == uids[i] && uids[i] != 0) { live = true; break; }
        if (!live) ++n;
    }
    return n;
}
/* the lead character (admin.cpp LeadCharacter): dead[i] is DeadPod of each candidate, in the order they are tried - the player's
   selection in its order, the game's target, then every own character of the update list - each already this game's own player
   character (1 dead, 0 alive, -1 unread). The first living one; a dead or unread one is skipped (run T1899: a revived player's
   old body stayed selected, and /where, /jail answered for it). down[i] != 0: that candidate is knocked out (handoff.cpp's
   IsDownedCharacter, the test the squad move's "knocked out stayed" uses; a missing entry = not knocked out). The first living one
   NOT knocked out; only when every living one is knocked out, the first living one (run T1902: /jail moved B's character and the
   knocked-out squadmate stayed, and /where answered for the squadmate). -1 = none living */
inline int LeadPick(const std::vector<int>& dead, const std::vector<int>& down = std::vector<int>())
{
    for (size_t i = 0; i < dead.size(); ++i) if (dead[i] == 0 && (i >= down.size() || down[i] == 0)) return (int)i;
    for (size_t i = 0; i < dead.size(); ++i) if (dead[i] == 0) return (int)i;
    return -1;
}

/* ---- /revive's fallen character, by name ----
   names: a fallen list's rows' names (fallenwire.h FallenName), newest first, as the world server keeps it and each game's copy has
   it. The same rule on both: the world server counts (ReviveResolve), the owning game picks. */
/* the rows called `typed` */
inline int FallenCount(const std::vector<std::string>& names, const std::string& typed)
{
    int n = 0;
    for (size_t i = 0; i < names.size(); ++i) if (SameName(names[i], typed)) ++n;
    return n;
}
/* the row a typed name brings back: the newest of that name; -1 none */
inline int FallenPick(const std::vector<std::string>& names, const std::string& typed)
{
    for (size_t i = 0; i < names.size(); ++i) if (SameName(names[i], typed)) return (int)i;
    return -1;
}
/* which player's fallen character a /revive names (the world server's decision). chars: each admitted player's count of fallen
   characters of that name on its list (in the world now or not); a player with none may be left out. One player's list holding the
   name - however many rows - is one target (its game brings back the newest). #name@player: that player's list alone.
   kResOne (slot; character is true), kResNone (no fallen character by that name), kResNotInWorld (shown = the one player whose list
   holds it, not in the world now), kResManyPlayers (#name@player: more than one player of that name; shown = it), kResManyChars
   (several players' lists hold it: owners = those players, in the world first) */
inline Resolved ReviveResolve(const Name& n, const std::vector<Player>& players, const std::vector<CharCount>& chars)
{
    Resolved r; r.shown = n.name; r.character = true;
    if (!n.player.empty())
    {
        std::vector<int> called; int away = 0;
        PlayersCalled(players, n.player, &called, &away);
        if (called.size() > 1) { r.res = kResManyPlayers; r.shown = n.player; return r; }
        if (called.empty()) { r.res = away > 0 ? kResNotInWorld : kResNone; if (away > 0) r.shown = n.player; return r; }
        int count = 0;
        for (size_t i = 0; i < chars.size(); ++i) if (chars[i].slot == called[0] && chars[i].count > 0) count += chars[i].count;
        if (count <= 0) { r.res = kResNone; return r; }
        r.res = kResOne; r.slot = called[0]; return r;
    }
    std::vector<int> in, away;
    for (size_t i = 0; i < chars.size(); ++i)
    {
        if (chars[i].count <= 0) continue;
        const Player* p = PlayerOfSlot(players, chars[i].slot);
        if (p == 0) continue;
        std::vector<int>& to = p->inWorld ? in : away;
        if (std::find(to.begin(), to.end(), chars[i].slot) == to.end()) to.push_back(chars[i].slot);
    }
    if (in.size() + away.size() > 1)
    {
        r.res = kResManyChars;
        for (size_t i = 0; i < in.size() && r.owners.size() < admincmd::kOthersMax; ++i) r.owners.push_back(PlayerOfSlot(players, in[i])->name);
        for (size_t i = 0; i < away.size() && r.owners.size() < admincmd::kOthersMax; ++i) r.owners.push_back(PlayerOfSlot(players, away[i])->name);
        return r;
    }
    if (in.size() == 1) { r.res = kResOne; r.slot = in[0]; return r; }
    if (away.size() == 1) { r.res = kResNotInWorld; r.shown = PlayerOfSlot(players, away[0])->name; return r; }
    r.res = kResNone;
    return r;
}

/* ---- /additem's item, by name (the admin's own game) ----
   rows: item records (name, record id). Those whose names start with what was typed (any ASCII case) match: the one whose whole
   name is the typed name, if exactly one is; else the one match, if there is exactly one. kItemOne (*pick = its row), kItemNone,
   kItemMany (*names = the matching names, sorted, at most kItemListMax: the whole-name matches when there are several, else every
   match). */
struct ItemRow { std::string name, sid; };
enum { kItemOne = 0, kItemNone = 1, kItemMany = 2 };
inline bool StartsName(const std::string& name, const std::string& typed)
{
    const std::string a = admincmd::LowerAscii(admincmd::Trim(name)), b = admincmd::LowerAscii(admincmd::Trim(typed));
    return !b.empty() && a.compare(0, b.size(), b) == 0;
}
inline int ItemPick(const std::string& typed, const std::vector<ItemRow>& rows, size_t* pick, std::vector<std::string>* names)
{
    *pick = 0; names->clear();
    std::vector<size_t> whole, start;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].name.empty() || rows[i].sid.empty() || !StartsName(rows[i].name, typed)) continue;
        start.push_back(i);
        if (SameName(rows[i].name, typed)) whole.push_back(i);
    }
    if (whole.size() == 1) { *pick = whole[0]; return kItemOne; }
    if (whole.empty() && start.size() == 1) { *pick = start[0]; return kItemOne; }
    if (start.empty()) return kItemNone;
    const std::vector<size_t>& many = whole.empty() ? start : whole;
    for (size_t i = 0; i < many.size(); ++i) names->push_back(rows[many[i]].name);
    std::sort(names->begin(), names->end());
    if (names->size() > kItemListMax) names->resize(kItemListMax);
    return kItemMany;
}
/* /additem's count as typed: true = a whole number from 1 to kAddItemMax (*count) */
inline bool CountOk(const std::string& w, int* count)
{
    if (w.empty() || w.size() > 3) return false;
    int v = 0;
    for (size_t i = 0; i < w.size(); ++i) { if (w[i] < '0' || w[i] > '9') return false; v = v * 10 + (w[i] - '0'); }
    if (v < 1 || v > kAddItemMax) return false;
    *count = v; return true;
}

/* ---- the moves (THE MOVES, above) ---- */
/* /bring and /goto: the first job (kOpPos) answered with `d`: true = the next job is due - *next is its ADMIN_DO (no job number; the
   world server adds kDoAskerHere when the moving game is the admin's own) and *nj its job's names. False = no next job (none was
   planned, or the first job did not answer with a position: its own reply is the answer). */
inline bool ChainDo(const Job& j, const Done& d, Do* next, Job* nj)
{
    if (j.op != (unsigned)kOpPos || j.nextSlot < 0 || d.code != (unsigned)kDoneOk) return false;
    *next = Do(); next->op = kOpMove; next->target = j.nextTarget; next->flags = kDoNear | kDoHaveY;
    if (j.verb == (unsigned)admincmd::kVerbBring) next->flags |= kDoHealFirst;   /* /bring heals a knocked-out target first; /goto does not */
    next->x = d.x; next->y = d.y; next->z = d.z;
    *nj = Job(); nj->req = j.req; nj->verb = j.verb; nj->asker = j.asker; nj->op = kOpMove; nj->shown = j.nextShown; nj->other = d.name;
    return true;
}
/* a single move (kOpMove) at the safe point, decided from the owning game's own reads: dead (1 dead, 0 alive, -1 unreadable), moved
   (the engine move's answer, tried only when alive: 1 moved, 2 the character is down - knocked out - and was not moved, 0 failed),
   healFirst (kDoHealFirst) and waitedMs (how long this job has waited since its character was healed; -1 = not healed). */
enum MoveNext { kMoveDone = 0,         /* moved: answer kDoneOk (num 1 when it was healed first) */
                kMoveDead = 1,         /* dead: answer kDoneDead, nothing done */
                kMoveFailed = 2,       /* a read or the engine move failed: answer kDoneFailed */
                kMoveDown = 3,         /* still down: answer kDoneDown (healed first and still down after kStandWaitMs, or no heal-first) */
                kMoveHealThenWait = 4, /* down, heal-first, not healed yet: heal it now (as /heal) and wait */
                kMoveWait = 5 };       /* down, healed, inside kStandWaitMs: try again at the next safe point */
inline int MoveNextOf(int dead, int moved, bool healFirst, long waitedMs)
{
    if (dead > 0) return kMoveDead;
    if (dead < 0) return kMoveFailed;
    if (moved == 1) return kMoveDone;
    if (moved != 2) return kMoveFailed;
    if (!healFirst) return kMoveDown;
    if (waitedMs < 0) return kMoveHealThenWait;
    return waitedMs >= kStandWaitMs ? kMoveDown : kMoveWait;
}
/* kDoNear: the spot beside (x, z) a character lands on */
inline void NearSpot(float x, float z, float* ox, float* oz) { *ox = x + kNearUnits; *oz = z; }
/* a squad move's k-th character (0 = the first, on the spot itself): rows of four, kSquadSpacing apart */
inline void SquadSpot(float x, float z, int k, float* ox, float* oz)
{
    *ox = x + kSquadSpacing * (float)(k % 4); *oz = z + kSquadSpacing * (float)(k / 4);
}
/* a squad move's answer: the moved names (at most kSquadNamesMax) then those that stayed knocked out (as many), num = the moved
   names listed */
inline void SquadNames(const std::vector<std::string>& moved, const std::vector<std::string>& down, Done* d)
{
    d->names.clear();
    for (size_t i = 0; i < moved.size() && i < kSquadNamesMax; ++i) d->names.push_back(moved[i]);
    d->num = (unsigned)d->names.size();
    for (size_t i = 0; i < down.size() && i < kSquadNamesMax; ++i) d->names.push_back(down[i]);
}

/* ---- /jail's spots (mockups-t1205.md section 10). The positions are the pull request's (public pull request 3, author
   MassDivide), read there from the game's own placements: each one town's or marker's x, y, z in the named region. ---- */
struct JailSpot { const char* key; const char* place; float x, y, z; const char* source; };
const JailSpot kJailSpots[] = {
    { "fog",       "the Fog Islands",     -79155.0f,  671.0f,  -38919.0f, "a fogmen nest marker, MARKER-Nest-Cower 40594-Newwworld.mod" },
    { "ash",       "the Ashlands",         91792.0f,  506.0f,  107090.0f, "Ashland Dome II, 1533506-rebirth.mod" },
    { "acid",      "the Black Desert",      8698.0f,  645.0f,  -13295.0f, "Black Desert City, 52210-Newwworld.mod" },
    { "cannibal",  "the Cannibal Plains", -65388.0f, 1291.0f,  -99378.0f, "Cannibal Village, 1224-gamedata.base" },
    { "leviathan", "the Leviathan Coast",  27281.0f,  234.0f,  123868.0f, "Dead Fishing Village, 64915-rebirth.mod" },
};
const int kJailSpotCount = (int)(sizeof(kJailSpots) / sizeof(kJailSpots[0]));
/* the spot a typed word names (its word, any ASCII case); -1 none */
inline int JailSpotOf(const std::string& word)
{
    const std::string k = admincmd::LowerAscii(admincmd::Trim(word));
    for (int i = 0; i < kJailSpotCount; ++i) if (k == kJailSpots[i].key) return i;
    return -1;
}
/* the spot for a /jail: the typed word's, or (none typed) the one `random` picks; -1 = a word that names no spot */
inline int JailSpotFor(const std::string& word, unsigned random)
{
    if (admincmd::Trim(word).empty()) return (int)(random % (unsigned)kJailSpotCount);
    return JailSpotOf(word);
}

/* ---- the homes (the world server's homes.txt, the world folder): "<player id> <x> <y> <z>" per line, the numbers in map units with
   at most one decimal; '#' starts a comment; blank lines are skipped; any other line is counted bad and kept as written when the
   server edits the file. One home per player id: a later line for an id replaces an earlier one. ---- */
struct Home { float x, y, z; Home() : x(0.0f), y(0.0f), z(0.0f) {} };
/* a number as homes.txt has it: an optional '-', digits, an optional '.' and digits; within kCoordMax. No locale. */
inline bool HomeNumber(const std::string& w, float* out)
{
    size_t i = 0; bool neg = false;
    if (i < w.size() && w[i] == '-') { neg = true; ++i; }
    double v = 0.0, scale = 0.0; size_t digits = 0;
    for (; i < w.size(); ++i)
    {
        if (w[i] == '.' && scale == 0.0) { scale = 1.0; continue; }
        if (w[i] < '0' || w[i] > '9') return false;
        ++digits;
        if (scale == 0.0) v = v * 10.0 + (w[i] - '0'); else { scale /= 10.0; v += (w[i] - '0') * scale; }
        if (v > (double)kCoordMax) return false;
    }
    if (digits == 0) return false;
    *out = (float)(neg ? -v : v);
    return true;
}
/* a map position as homes.txt writes it: tenths, no locale ("-12450.5", "3210") */
inline std::string HomeNumberText(float f)
{
    const long long t = (long long)std::floor((double)f * 10.0 + 0.5);
    const long long a = t < 0 ? -t : t;
    char b[40]; std::sprintf(b, "%s%lld", t < 0 ? "-" : "", a / 10);
    std::string s(b);
    if (a % 10 != 0) { std::sprintf(b, ".%lld", a % 10); s += b; }
    return s;
}
/* one line: true = a home (*id, *h); false = a comment, a blank line or a bad line (*bad set for a bad one) */
inline bool HomeLine(const std::string& line, std::string* id, Home* h, bool* bad)
{
    *bad = false;
    if (admincmd::CommentOrBlank(line)) return false;
    std::vector<std::string> w;
    const std::string t = admincmd::Trim(line);
    size_t a = 0;
    while (a < t.size())
    {
        while (a < t.size() && admincmd::IsSpace(t[a])) ++a;
        size_t b = a; while (b < t.size() && !admincmd::IsSpace(t[b])) ++b;
        if (b > a) w.push_back(t.substr(a, b - a));
        a = b;
    }
    if (w.size() != 4 || !admincmd::IdOk(w[0]) || !HomeNumber(w[1], &h->x) || !HomeNumber(w[2], &h->y) || !HomeNumber(w[3], &h->z)) { *bad = true; return false; }
    *id = w[0];
    return true;
}
/* the home kept for `id` (the last line for it wins); *bad = the bad lines counted */
inline bool HomeFind(const std::string& text, const std::string& id, Home* out, int* bad)
{
    *bad = 0;
    bool found = false;
    const std::vector<std::string> lines = admincmd::Lines(text);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string lid; Home h; bool b = false;
        if (HomeLine(lines[i], &lid, &h, &b)) { if (lid == id) { *out = h; found = true; } }
        else if (b) ++*bad;
    }
    return found;
}
/* homes.txt with `id`'s home set to `h`: its lines replaced by one (where the first was), or one added at the end; every other line
   kept as written. A new file starts with a comment line saying what it is. */
inline std::string HomesEdit(const std::string& text, const std::string& id, const Home& h)
{
    const std::string row = id + " " + HomeNumberText(h.x) + " " + HomeNumberText(h.y) + " " + HomeNumberText(h.z);
    std::vector<std::string> lines = admincmd::Lines(text), out;
    if (lines.empty()) out.push_back("# homes: player id, x, y, z - each admin's /sethome, kept by the world server");
    bool put = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string lid; Home old; bool bad = false;
        if (HomeLine(lines[i], &lid, &old, &bad) && lid == id) { if (!put) { out.push_back(row); put = true; } continue; }
        out.push_back(lines[i]);
    }
    if (!put) out.push_back(row);
    return admincmd::JoinLines(out);
}

/* ---- the owning game's memory of the jobs it finished on its link ---- */
struct Memo { int gen; std::vector<unsigned> jobs; std::vector<std::vector<char> > done; Memo() : gen(-1) {} };
/* the ADMIN_DONE sent for `job` on the link `gen`; false = not finished on this link */
inline bool MemoFind(const Memo& m, int gen, unsigned job, std::vector<char>* done)
{
    if (m.gen != gen) return false;
    for (size_t i = 0; i < m.jobs.size(); ++i) if (m.jobs[i] == job) { *done = m.done[i]; return true; }
    return false;
}
/* `job` finished on the link `gen` with the ADMIN_DONE `done`; another link's rows are forgotten, the oldest beyond kMemoRows too */
inline void MemoKeep(Memo* m, int gen, unsigned job, const std::vector<char>& done)
{
    if (m->gen != gen) { m->gen = gen; m->jobs.clear(); m->done.clear(); }
    m->jobs.push_back(job); m->done.push_back(done);
    if (m->jobs.size() > kMemoRows) { m->jobs.erase(m->jobs.begin()); m->done.erase(m->done.begin()); }
}

/* ---- numbers as the chat shows them: whole map units ---- */
inline long long Whole(float f) { return (long long)std::floor((double)f + 0.5); }

}   /* namespace admintool */

#endif
