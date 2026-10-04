/* src/common/relside.h - WHICH ENTRY A FACTION-RELATIONS CHANGER WROTE.
 *
 * Every faction keeps a relations object: a table of its own standing towards each other faction. The engine's changers
 * (affectRelations and its by-event form, declareWar, setNoLongerEnemies, setEnemy, affectTrust) find the entry to change
 * through the object's own lookup (getRelationData, vtable +0x50); setRelation writes the object's table directly.
 *
 * The PLAYER faction's relations object is a PlayerFactionRelations (made when a faction becomes the player's: Faction+0x250
 * set, its relations replaced - 0x801970). Its lookup answers "the player towards X" with X's OWN entry towards the player
 * (0x6B2EF0: f->relations->getRelationData(owner)). So a changer called on the player's relations writes X -> player, not
 * player -> X; only setRelation still writes the player's own table, which the engine's lookups never read.
 *
 * WrittenPair names the pair a changer call wrote, so the mod forwards, puts back or records the entry that actually moved.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

namespace relside {

enum Changer
{
    kNotAChanger = -1,   /* a read of the pair as named (the snapshot) */
    kAffect = 0,         /* affectRelations(f, amount, mult) 0x6B2B50 */
    kAffectEvent,        /* affectRelations(f, event, mult) 0x6B29D0 */
    kDeclareWar,         /* 0x6B2CB0 */
    kNoLongerEnemies,    /* setNoLongerEnemies 0x6B2C40 */
    kSetEnemy,           /* 0x6B2E40 */
    kAffectTrust,        /* 0x6B2210 */
    kSetRelation         /* 0x6B4A30 - writes the object's own table, no lookup */
};

/* Whether a call of `changer` on a relations object whose owner is the player faction wrote the OTHER faction's entry towards
   the player. Not for the player's own self entry (other == owner: the lookup answers a fixed self entry). */
inline bool WritesOtherSide(int changer, bool ownerIsPlayer, bool otherIsOwner)
{
    if (!ownerIsPlayer || otherIsOwner) return false;
    return changer >= kAffect && changer < kSetRelation;
}

/* The pair (whose table, towards whom) the call wrote: (owner, other) as called, or (other, owner) when WritesOtherSide. */
template <class F>
inline void WrittenPair(int changer, bool ownerIsPlayer, F owner, F other, F* wroteOwner, F* wroteOther)
{
    if (WritesOtherSide(changer, ownerIsPlayer, owner == other)) { *wroteOwner = other; *wroteOther = owner; }
    else { *wroteOwner = owner; *wroteOther = other; }
}

/* ---- Where a forwarded change goes (relations.cpp Forward / QueueOffThread / Owned / WorldPair / Snapshot) ---- */

/* One faction as the forward sees it: this game's player faction; another player's faction (a stand-in met this session, or the
   coop-p<n> faction the save carries for another slot); or a world faction (neither, and its record id names no player). */
struct Side { bool mine; bool otherPlayers; bool world; };

/* Who owns a standing (owner's table, towards other): this game when the owner side is this game's player faction; another player
   when the owner side is that player's faction; otherwise this game when the other side is this game's player faction (X -> me);
   else nobody here (world-vs-world is the world server's table). */
inline bool OwnsPair(const Side& owner, const Side& other)
{
    if (owner.mine) return true;
    if (owner.otherPlayers) return false;
    return other.mine;
}
inline bool WorldPair(const Side& owner, const Side& other) { return owner.world && other.world; }

enum Road
{
    kRoadWorldTable = 0,   /* world-vs-world: noted for the world server's table */
    kRoadPutBack,          /* another player's standing this game's engine moved: the owner's value is put back */
    kRoadSend              /* this game's own standing: sent to the other games */
};
/* the road of the WRITTEN pair (after WrittenPair) */
inline int PairRoad(const Side& owner, const Side& other)
{
    if (WorldPair(owner, other)) return kRoadWorldTable;
    return OwnsPair(owner, other) ? kRoadSend : kRoadPutBack;
}

enum Moment
{
    kMomentEcho = 0,   /* the mod's own write of a value from the wire: no forward (no echo) */
    kMomentQueue,      /* an engine change on a worker thread: queued, the main thread's tick drains it */
    kMomentNow         /* on the main thread: decided now */
};
inline int ForwardMoment(bool applying, bool mainThreadKnown, bool onMainThread)
{
    if (applying) return kMomentEcho;
    if (mainThreadKnown && !onMainThread) return kMomentQueue;
    return kMomentNow;
}

}   /* namespace relside */
