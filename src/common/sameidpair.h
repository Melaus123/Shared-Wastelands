/* src/common/sameidpair.h - ONE LIVE PLATOON PER WORLD SQUAD ID ON A GAME.
 *
 * ONE RULE, the one squadorigin.h keeps: nothing in here reads engine memory, reads a global or calls the operating system. A pure
 * function of its arguments, so the offline suite drives every branch.
 *
 * THE YIELD (spawn.cpp PrepareContextForSpawn, AdoptExistingTwin). Another game announces a squad it runs, and this game holds an
 * AWAKE platoon of its own under the same id. Asleep, the world server's record is the squad's truth; awake, the game running it is.
 * So this game's copy that was BUILT FROM THE WORLD RECORD (store.cpp CreateUnknownSquad) yields to the announcement, and so does one
 * that came from THIS GAME'S SAVE (its number is below its faction's load floor) - put away through the engine's unload with no record
 * written, its registered people withdrawn, and the announcer's copy built in its place. What does not yield:
 *   - a player's or another player's faction (never this road's);
 *   - a squad this game handed over that still holds a person this game runs (squadwriter.h HandedOverRetireRefuse);
 *   - a squad this game made live (neither from the record nor from the save): the id refusal stands when it is decided as this
 *     game's, and the earlier roads decide it otherwise;
 *   - a save copy decided as this game's BY ITS OWN MEMBERS: the two games' world-load ages already weigh that pair
 *     (squadorigin.h OwnIdLiftedByLoadAge), and at most one of two games lifts there - a yield on top of it would put both away;
 *   - a save copy holding a person another game runs (an adopted twin, a copy): the copy is that game's in place;
 *   - a copy whose people cannot be read (fail closed);
 *   - a copy the yield's order does not name as the one to give way (YieldOrder): the announcement carries where the announcer's copy
 *     came from. A copy here built from the record or from the save gives way at once to an announced copy its game RUNS (made live
 *     there); against an announced copy that is itself from a record or a save, or whose origin is unknown, both sides take the
 *     load-age order (YieldTurn) and at most one of them puts its copy away. A copy made live here never gives way;
 *   - an announcement that no longer stands: its game is not in the world on this link's roster, or the announced person's recorded
 *     owner is no longer that game (YieldGate, one verdict each).
 * A verdict other than kYield leaves the earlier roads exactly as they were.
 *
 * THE PAIR GUARD (store.cpp). While two platoons here answer to one world id, neither writes the record - not at its sleep and not at
 * its heartbeat - and neither re-points the id's by-id entry. pairOther: another platoon here is bound to the id, awake or asleep
 * (PartnerStands); pairMarked: a platoon judged earlier was refused for holding this id beside this one, and still stands. Either
 * refuses. THE PAIR'S END: one side destroyed or retired (its forget) or unbound - asleep is not an end, so the second of two copies
 * put to sleep does not write over the first's refusal. While the pair stands this game writes no record of the squad: the record
 * keeps its last version. The announcing game may hold a pair of its own and then writes none either: the record goes stale, never
 * overwritten by the smaller copy; samePair[..,idsNow,..] counts the pairs standing.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no lambdas.
 */
#ifndef SAMEIDPAIR_H_INCLUDED
#define SAMEIDPAIR_H_INCLUDED

#include <map>
#include <set>
#include <string>
#include "squadorigin.h"   /* the load-age order a yield's turn shares with the load-age lift (pure, like this file) */

namespace sameid {

/* where this game's copy came from */
const int kCopyLive = 0, kCopyFromRecord = 1, kCopyFromSave = 2;

/* madeFromRecord 1: CreateUnknownSquad built it from its world record (store.cpp g_madeFromRecord). floorKnown / number / floorValue:
   the id's number and its faction's load floor (store.cpp SquadLoadFloor) - a number below the floor came from the save. A floor not
   known reads as made live, so nothing is put away on a missing floor. */
inline int CopyOrigin(int madeFromRecord, int floorKnown, unsigned number, unsigned floorValue)
{
    if (madeFromRecord != 0) return kCopyFromRecord;
    if (floorKnown != 0 && number < floorValue) return kCopyFromSave;
    return kCopyLive;
}

/* where the ANNOUNCER's copy came from, carried on its CONTEXT (net/session.cpp SendContext): the sender's own CopyOrigin of the
   platoon it runs the announced person in, read from its own state. kCopyUnknown: a sender without the field, or a value this build
   does not know. On the wire a u32: 0 made live (running), 1 from the record, 2 from the save, 0xFFFFFFFF unknown. */
const int kCopyUnknown = -1;
const unsigned int kWireOriginUnknown = 0xFFFFFFFFu;
inline unsigned int WireOfOrigin(int origin)
{
    return (origin == kCopyLive || origin == kCopyFromRecord || origin == kCopyFromSave) ? (unsigned int)origin : kWireOriginUnknown;
}
inline int OriginOfWire(unsigned int w) { return (w == 0u || w == 1u || w == 2u) ? (int)w : kCopyUnknown; }
inline const char* OriginWord(int origin)
{
    return origin == kCopyLive ? "running" : origin == kCopyFromRecord ? "fromRecord" : origin == kCopyFromSave ? "fromSave" : "unknown";
}

const int kVerdictToday = 0;            /* not this rule's: the earlier roads decide, as before */
const int kVerdictYield = 1;            /* put away; the announcer's copy is built and bound */
const int kVerdictKeepLive = 2;         /* made live here and decided as this game's: the id refusal stands */
const int kVerdictKeepPlayerSide = 3;   /* a player's or another player's faction */
const int kVerdictKeepHandedOverMine = 4;   /* handed over, and a person this game runs (or an unread one) is still in it */
const int kVerdictKeepLoadAge = 5;      /* a save copy decided as this game's by its own members: the load-age weighing decides */
const int kVerdictKeepOthersAboard = 6; /* a save copy holding a person another game runs */
const int kVerdictKeepUnread = 7;       /* its people could not be read */
const int kVerdictKeepTurn = 8;         /* the load-age order does not name this game's copy as the one to give way (YieldTurn) */
const int kVerdictKeepLate = 9;         /* the announcing game is not IN_WORLD on this link's roster: the announcement no longer stands */
const int kVerdictKeepOwnerMoved = 10;  /* the announced person's recorded owner is no longer the announcing game */
const int kVerdictCount = 11;

inline const char* VerdictWord(int v)
{
    switch (v)
    {
    case kVerdictToday: return "today";
    case kVerdictYield: return "yield";
    case kVerdictKeepLive: return "keepLive";
    case kVerdictKeepPlayerSide: return "keepPlayerSide";
    case kVerdictKeepHandedOverMine: return "keepHandedOverMine";
    case kVerdictKeepLoadAge: return "keepLoadAge";
    case kVerdictKeepOthersAboard: return "keepOthersAboard";
    case kVerdictKeepUnread: return "keepUnread";
    case kVerdictKeepTurn: return "keepTurn";
    case kVerdictKeepLate: return "keepLate";
    case kVerdictKeepOwnerMoved: return "keepOwnerMoved";
    default: return "?";
    }
}

/* origin: CopyOrigin. ownId 1: the earlier test decided the id as this game's (spawn.cpp ownId, after the hand-over and load-age
   tests); ownByMembers 1: that decision came from the copy's own members (squadorigin.h kByMembers). playerSide 1: a player's or
   another player's faction. handedOverKeep 1: HandedOverRetireRefuse refused. sessionMembers: the copy's registered people (-1 =
   unread); mineMembers: how many of them this game runs (-1 = unread). */
inline int CopyYield(int origin, int ownId, int ownByMembers, int playerSide, int handedOverKeep, int sessionMembers, int mineMembers)
{
    if (playerSide != 0) return kVerdictKeepPlayerSide;
    if (handedOverKeep != 0) return kVerdictKeepHandedOverMine;
    if (origin != kCopyFromRecord && origin != kCopyFromSave) return ownId != 0 ? kVerdictKeepLive : kVerdictToday;
    if (sessionMembers < 0 || mineMembers < 0) return kVerdictKeepUnread;
    if (origin == kCopyFromRecord) return kVerdictYield;
    if (ownId != 0 && ownByMembers != 0) return kVerdictKeepLoadAge;
    if (mineMembers < sessionMembers) return kVerdictKeepOthersAboard;
    return kVerdictYield;
}

/* the twin step (spawn.cpp AdoptExistingTwin runs before the platoon is prepared): a body is not adopted in place when its squad's
   platoon here is about to be put away by the yield. 1 = do not adopt. */
inline int TwinAdoptRefused(int verdict) { return verdict == kVerdictYield ? 1 : 0; }

/* THE YIELD'S TURN: of two games' copies of one squad, at most one is put away. Each game judges the other's announcement alone and
   is told where the other's copy came from only when the other could tell (CONTEXT's origin; unknown otherwise), so CopyYield's answer
   alone would put both away: two copies built from the record
   (or one from the record, one a save copy) each yield to the other, and a record-built copy yields while the announcer's own save
   copy is retired by the load-age lift (squadorigin.h OwnIdLiftedByLoadAge). So a yield takes the lift's own order: 1 = this game's
   copy is the one to give way - its world load is younger than the announcer's by more than sqorigin::kLoadAgeTieMs, or the two are
   within that and this game's seat is the higher one. The order names one side or neither (an unknown age or seat, or equal seats in
   a tie: neither - both copies kept, the pair guard holds); the announcer's age reaches each side short by its transit, which only
   takes turns away. Every put-away on this road (a lift or a yield) needs this side named, so the two sides never both put theirs away. */
inline int YieldTurn(unsigned int ownAgeMs, unsigned int peerAgeMs, int mySeat, int peerSeat)
{
    return sqorigin::OwnIdLiftedByLoadAge(sqorigin::kByMembers, 1, ownAgeMs, peerAgeMs, mySeat, peerSeat) == sqorigin::kOwnIdLifted ? 1 : 0;
}

/* THE YIELD'S ORDER, the first decision: 1 = this game's copy is the one to give way. ownOrigin: this game's copy (CopyOrigin);
   announcedOrigin: the announcer's copy as its CONTEXT carried it (kCopyUnknown when absent).
   - a copy made live here never gives way (0);
   - a copy here from the record or the save gives way at once to an announced RUNNING copy - the game running the squad holds it,
     and this game's copy is the stale one;
   - otherwise (the announced copy is itself from a record or a save, or its origin is unknown) the load-age order decides (YieldTurn).
   Never both: the announced origin is the announcer's own reading of its copy. A copy that reads as running there is neither built from
   the record nor below a known load floor, so it never yields there (CopyYield keeps a live copy) and is never lifted there (the lift
   needs the copy's own members to decide, which needs a number below a known floor - a save copy). Every other put-away on this road,
   on either side, needs the one shared load-age order to name that side, which names at most one. */
inline int YieldOrder(int ownOrigin, int announcedOrigin, unsigned int ownAgeMs, unsigned int peerAgeMs, int mySeat, int peerSeat)
{
    if (ownOrigin != kCopyFromRecord && ownOrigin != kCopyFromSave) return 0;
    if (announcedOrigin == kCopyLive) return 1;
    return YieldTurn(ownAgeMs, peerAgeMs, mySeat, peerSeat);
}

/* the yield's gates, on CopyYield's verdict - an announcement that no longer stands is not acted on. announcerInWorld 1: the announcing
   game is IN_WORLD on this link's roster (0 = not, or no roster: kept). ownerIsAnnouncer 1: the announced person's recorded owner is
   still that game. turn: YieldTurn. Only a kVerdictYield is changed. */
inline int YieldGate(int verdict, int announcerInWorld, int ownerIsAnnouncer, int turn)
{
    if (verdict != kVerdictYield) return verdict;
    if (announcerInWorld == 0) return kVerdictKeepLate;
    if (ownerIsAnnouncer == 0) return kVerdictKeepOwnerMoved;
    return turn != 0 ? kVerdictYield : kVerdictKeepTurn;
}

/* the pair guard: 1 = refuse this platoon's record write and its by-id re-point */
inline int PairRefuse(int pairOther, int pairMarked) { return (pairOther != 0 || pairMarked != 0) ? 1 : 0; }

/* the pair guard's standing test (store.cpp PairPartnerStands): 1 = a partner still answers to the id here. boundTo: 1 bound to this id,
   0 not bound, -1 bound to another id. engineIdIs (asked only when not bound): 1 its engine id is this id, 0 another, -1 unread (fail
   closed: stands). Awake or asleep is not asked: a partner asleep and not forgotten still holds the id, and its sleep record and this
   copy's writes would land on the one record in turn. */
inline int PartnerStands(int boundTo, int engineIdIs)
{
    if (boundTo > 0) return 1;
    if (boundTo < 0) return 0;
    return engineIdIs == 0 ? 0 : 1;
}

/* the pair guard's notes: world id -> the platoons found answering to it beside another. A side that is not bound (it answers by its
   engine id) is found only when it is judged; both sides are noted then, so the bound side - which sees no other binding - is refused
   from that moment too. A platoon leaves at its forget (destroyed / retired) and its unbind (store.cpp), not at its sleep; a noted
   partner that no longer stands (PartnerStands: bound to another id, or unbound with another engine id) is dropped at the next Judge
   and refuses nothing; a set of one is no pair. */
struct PairBook
{
    std::map<std::string, std::set<const void*> > seen;
    /* stands(partner, id): 1 = a noted partner still answers to id here, 0 = it is gone (dropped from the notes at this Judge);
       no function given = every note stands */
    typedef int (*StandsFn)(const void* partner, const std::string& id);
    /* otherBound: another platoon bound to id here and standing (0 = none). 1 = refuse; *partnerOut = the other side named. */
    int Judge(const void* p, const std::string& id, const void* otherBound, const void** partnerOut, StandsFn stands = 0)
    {
        *partnerOut = otherBound;
        const void* marked = 0;
        std::map<std::string, std::set<const void*> >::iterator ps = seen.find(id);
        if (ps != seen.end())
        {
            for (std::set<const void*>::iterator m = ps->second.begin(); m != ps->second.end(); )
            {
                if (*m != p && stands != 0 && stands(*m, id) == 0) { ps->second.erase(m++); continue; }   /* gone: its note refuses nothing */
                if (*m != p && marked == 0) marked = *m;
                ++m;
            }
            if (ps->second.size() < 2) seen.erase(ps);   /* a set of one is no pair */
        }
        if (PairRefuse(otherBound != 0 ? 1 : 0, marked != 0 ? 1 : 0) == 0) return 0;
        if (*partnerOut == 0) *partnerOut = marked;
        std::set<const void*>& s = seen[id];
        s.insert(p);
        if (otherBound != 0) s.insert(otherBound);
        return 1;
    }
    void Forget(const void* p)
    {
        for (std::map<std::string, std::set<const void*> >::iterator ps = seen.begin(); ps != seen.end(); )
        {
            ps->second.erase(p);
            if (ps->second.size() < 2) seen.erase(ps++); else ++ps;
        }
    }
    void Clear() { seen.clear(); }
    size_t Pairs() const { return seen.size(); }
};

}   /* namespace sameid */

#endif
