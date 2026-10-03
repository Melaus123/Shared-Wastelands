/* src/common/orphanpurge.h - orphan1: PEOPLE CREATED IN ANOTHER GAME'S AREA WHILE THE GAME LINK WAS DOWN.
 *
 * ONE RULE, the same one clockmath.h, loadlatch.h and standinpurge.h keep: nothing in here reads engine
 * memory, reads a global, calls the operating system or includes any header.  It is a pure function of its
 * arguments, so the offline suite drives every branch instead of believing a comment.
 *
 * WHY IT EXISTS (T420/T426).  The leaf creation gate let everything through while the game-to-game link was
 * down, even while the notebook's area map was live and named ANOTHER game the holder of the area.  T420: A
 * created 65 people in B-held 43,11 while B reloaded; T426: B created 16 in A-held sectors.  After link-up the
 * adoption pass (decision 33) skips a character standing in an area another game holds, so nobody announced
 * them and nothing removed them - people that existed on one machine only, for good.
 *
 * TWO HALVES.
 *   (1) PREVENTION (LeafUnlinked): with the game link down, a LIVE notebook map that names another holder refuses
 *       the creation (decision 37: a game invents people only where the notebook names it holder).  No live map
 *       (true solo / offline play, or the notebook down) and an area that is unclaimed or this game's keep the old
 *       unlinked answer: allowed.
 *   (2) CLEANUP (Decide): after link-up, an unregistered non-player character standing in an area another game
 *       holds is removed through the stand-in purge's route (the holder's world wins; there is no handover).
 *       Never a player-faction character, never one carried by a player character, never a caged one (a caged
 *       person may be a player's prisoner and letting them out is not ours to do), and not while a fight with a
 *       player character is going on nearby (removal mid-fight would be visible and unfair; a later pass does it).
 *       review-orphan1: nor within 100 units of a player character (talking, trading, looting need adjacency), nor
 *       before it has been noted CONTINUOUSLY for 5 s and the game link has been up 10 s - a twin awaiting the holder's
 *       SPAWN (H027/H030: withdrawn, never destroyed), this game's own new people stepping across a sector line and a
 *       refused adoption all look like orphans for a moment.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_ORPHANPURGE_H
#define COOP_COMMON_ORPHANPURGE_H

namespace coopor {

/* (1) THE LEAF GATE'S UNLINKED ARM.  heldByOther is HeldByOtherTS's answer: 1 another game holds the area,
   0 it does not (unclaimed or this game's), -1 no live notebook map. */
const int kLeafAllowUnlinked = 0, kLeafGatedHeldUnlinked = 1;
inline int LeafUnlinked(int heldByOther) { return heldByOther == 1 ? kLeafGatedHeldUnlinked : kLeafAllowUnlinked; }

/* (1b) p97-op - P97 (owner 334 a / 337 a): THE LEAF GATE'S LINKED ARM.  `may` is MayInventFromView's answer:
   > 0 this game invents here, 0 it does not, < 0 no fresh area map (the caller takes the teardown answer apart
   first).  With no fresh map EVERY game refuses.  The retired bounded wait let the game holding slot 0 generate
   once 20 s had passed; neither the slot nor the length of the outage is an input here, so neither can move the
   answer.  During a notebook outage world population freezes and resumes once the map is back. */
const int kLeafLinkedAllow = 0, kLeafLinkedNotHere = 1, kLeafLinkedNoMap = 2;
inline int LeafLinked(int may) { return may > 0 ? kLeafLinkedAllow : (may == 0 ? kLeafLinkedNotHere : kLeafLinkedNoMap); }
/* The character factory's last word (worldgen.cpp detour_createRandomCharacter): a gate refusal (gated != 0)
   refuses the creation unless the character is the player's own faction (T-230: the new-game squad). */
inline int LeafCreationRefused(int gated, int isPlayerFaction) { return (gated != 0 && isPlayerFaction == 0) ? 1 : 0; }

/* (1c) P97: THE ANNOUNCE SWEEP'S AREA ANSWER (worldsync.cpp AnnounceAreaDecide).  heldByOther is HeldByOtherTS's
   answer as in (1).  Adoptable only where the map answered and no other game holds the area; with no fresh map the
   sweep skips on every game, whatever its slot.  Nothing is lost: the sweep re-walks every un-adopted character
   each pass, so the holder adopts and announces once the map is fresh again. */
const int kAnnounceAdoptable = 0, kAnnounceOtherHeld = 1, kAnnounceNoMap = 2;
inline int AnnounceArea(int heldByOther) { return heldByOther == 1 ? kAnnounceOtherHeld : (heldByOther < 0 ? kAnnounceNoMap : kAnnounceAdoptable); }

/* (2) THE CLEANUP'S ACTION for one queued character on this pass. */
enum {
    kActNone        = 0,   /* not an orphan (any more): leave it alone and drop it from the queue */
    kActDestroy     = 1,   /* destroy it now (the stand-in purge's route) */
    kActDropCarrier = 2,   /* its (non-player) carrier puts it down first */
    kActBedOut      = 3,   /* it gets out of its bed first */
    kActDropOwn     = 4,   /* it puts down what it carries first */
    kActSkip        = 5,   /* left standing, counted (*why); re-judged on a later pass */
    kActLater       = 6    /* not now (*why = kWhyInCombatWithPlayer); a later pass removes it */
};

/* WHY it was skipped. */
enum {
    kWhyNone              = 0,
    kWhyPlayerTied        = 1,   /* the watched player / a player-faction character (recruit, slave, ...) */
    kWhyCarriedByPlayer   = 2,   /* a player-faction character carries it */
    kWhyCaged             = 3,   /* in a cage: may be a player's prisoner - never let out by us */
    kWhyInCombatWithPlayer= 4,   /* a fight with a player character is on within the proximity radius */
    kWhyPoseUnreadable    = 5,   /* Character +0x2F8 could not be read */
    kWhyCarrierUnknown    = 6,   /* carried, and no character in the update list carries it */
    kWhyCarryStuck        = 7,   /* its carrier was told to put it down and it is still carried */
    kWhyBedStuck          = 8,   /* told to leave its bed (or no setBedMode address) and still in it */
    kWhyOwnCarryStuck     = 9,   /* told to put down what it carries and still carrying */
    kWhyInUnknown         = 10,  /* +0x2F8 names a state other than 0 / 1 bed / 2 cage */
    kWhyNearPlayer        = 11,  /* review-orphan1 HIGH: a player-faction character within kNearPlayerR - a later pass */
    kWhyNotedTooShort     = 12,  /* review-orphan1 MED: noted continuously for less than kNotedMinMs - a later pass */
    kWhyLinkTooFresh      = 13,  /* review-orphan1 MED: the game link has been up less than kLinkedMinMs - a later pass */
    kWhyCount             = 14
};

/* Everything the decision needs about one queued character, read by the caller. */
struct Seen
{
    int linked;           /* 1 = the game-to-game link is up (the cleanup runs only after link-up) */
    int areaOther;        /* HeldByOtherTS for the sector it stands in: 1 another game holds it, 0 not, -1 no live map */
    int known;            /* 1 = FindSpawnedUid answers a uid for it (registered: adopted or a peer copy) */
    int standIn;          /* 1 = its faction record is a stand-in (coop-p<n> / coop-peer): the stand-in purge's business */
    int guarded;          /* 1 = the watched player or a player-faction character */
    int fightNear;        /* 1 = it or a player character within the radius is in combat mode, with a player character near */
    int inSomething;      /* Character +0x2F8: 0 nothing, 1 bed, 2 cage, -1 unreadable */
    int beingCarried;     /* Character +0x3D4 */
    int carrierFound;     /* 1 = a character in the update list carries it */
    int carrierIsPlayer;  /* 1 = that carrier is a player-faction character (or the watched player) */
    int carrierTried;     /* 1 = that carrier was already told to put it down (or there is no drop address) */
    int bedTried;         /* 1 = setBedMode(false) was already called (or has no address) */
    int carrying;         /* Character +0x348: it carries something itself */
    int ownDropTried;     /* 1 = it was already told to put that down (or there is no drop address) */
    int nearPlayer;       /* 1 = a player-faction character is within the adjacency radius */
    int dupOfLiveCopy;    /* T-304 (1): 1 = a registered copy of ANOTHER game's character with the same template and faction stands within kDupCopyR - a duplicate, so the near-player wait does not apply */
    unsigned long notedMs;   /* how long the adoption pass has noted it CONTINUOUSLY */
    unsigned long linkedMs;  /* how long the game link has been up (0 when it is down) */
};

/* review-orphan1 MED: the waiting period. */
const unsigned long kNotedMinMs = 5000, kLinkedMinMs = 10000;

inline Seen SeenNone()
{
    Seen s;
    s.linked = 1; s.areaOther = 1; s.known = 0; s.standIn = 0; s.guarded = 0; s.fightNear = 0; s.inSomething = 0;
    s.beingCarried = 0; s.carrierFound = 0; s.carrierIsPlayer = 0; s.carrierTried = 0; s.bedTried = 0;
    s.carrying = 0; s.ownDropTried = 0;
    s.nearPlayer = 0; s.dupOfLiveCopy = 0; s.notedMs = kNotedMinMs; s.linkedMs = kLinkedMinMs;
    return s;
}

/* ONE STEP.  The caller acts on the answer, re-reads the character, and asks again (a drop and a bed-out are
   synchronous).  Total over every input - no unhandled combination.  The player-tie tests come BEFORE every
   action, so nothing is ever done to a player's character or to what a player carries. */
inline int Decide(const Seen& s, int* why)
{
    *why = kWhyNone;
    if (s.linked == 0) return kActNone;          /* only after link-up: a solo / unlinked game keeps its world */
    if (s.areaOther != 1) return kActNone;       /* unclaimed, mine, or no live map: not an orphan */
    if (s.known != 0) return kActNone;           /* registered: someone announces it */
    if (s.standIn != 0) return kActNone;         /* the stand-in purge's (inv7a), not ours */
    if (s.guarded != 0) { *why = kWhyPlayerTied; return kActSkip; }
    if (s.beingCarried != 0 && s.carrierIsPlayer != 0) { *why = kWhyCarriedByPlayer; return kActSkip; }
    if (s.inSomething == 2) { *why = kWhyCaged; return kActSkip; }
    if (s.fightNear != 0) { *why = kWhyInCombatWithPlayer; return kActLater; }
    if (s.nearPlayer != 0 && s.dupOfLiveCopy == 0) { *why = kWhyNearPlayer; return kActLater; }   /* T-304 (1): a duplicate of a live copy is not someone the player is dealing with - the other game already shows that character */
    if (s.linkedMs < kLinkedMinMs) { *why = kWhyLinkTooFresh; return kActLater; }   /* before ANY action, a put-down included */
    if (s.notedMs < kNotedMinMs) { *why = kWhyNotedTooShort; return kActLater; }
    if (s.inSomething < 0) { *why = kWhyPoseUnreadable; return kActSkip; }
    if (s.beingCarried != 0)
    {
        if (s.carrierTried != 0) { *why = kWhyCarryStuck; return kActSkip; }
        if (s.carrierFound == 0) { *why = kWhyCarrierUnknown; return kActSkip; }
        return kActDropCarrier;
    }
    if (s.inSomething == 1)
    {
        if (s.bedTried != 0) { *why = kWhyBedStuck; return kActSkip; }
        return kActBedOut;
    }
    if (s.inSomething != 0) { *why = kWhyInUnknown; return kActSkip; }
    if (s.carrying != 0)
    {
        if (s.ownDropTried != 0) { *why = kWhyOwnCarryStuck; return kActSkip; }
        return kActDropOwn;
    }
    return kActDestroy;
}

/* The fight test: squared planar distance within the radius, and a combat flag on either side. */
inline int FightNear(float dx, float dz, float radius, int orphanInCombat, int playerInCombat)
{
    if (dx * dx + dz * dz > radius * radius) return 0;
    return (orphanInCombat != 0 || playerInCombat != 0) ? 1 : 0;
}

/* review-orphan1 HIGH: a player-faction character within this planar radius (adjacency) - not now. */
inline int NearPlayer(float dx, float dz, float radius) { return (dx * dx + dz * dz <= radius * radius) ? 1 : 0; }

/* T-304 (1), fold 1: the duplicate radius (planar) = the adjacency radius (spawn.cpp kOrphanNearR). Run T674 measured the
   stray 26 units from the copy it duplicated (copy 109 built at 53818,-94575; the stray adopted at 53836,-94593); 100 keeps a
   4x margin over that one measurement and matches the near-player scale this rule overrides. */
const float kDupCopyR = 100.0f;
inline int DupNear(float dx, float dz) { return (dx * dx + dz * dz <= kDupCopyR * kDupCopyR) ? 1 : 0; }

/* T-304 (1), fold 1: a noted character is held (its decisions suppressed) ONLY while it waits for the link or the noted clock -
   the waits that end in its removal (a duplicate near a player lands here, Decide skips the near-player wait for it). Never
   mid-fight with a player (kWhyInCombatWithPlayer), never a plain near-player wait, never a kActSkip one. */
inline int HoldWhilePending(int act, int why) { return (act == kActLater && (why == kWhyLinkTooFresh || why == kWhyNotedTooShort)) ? 1 : 0; }

/* At most this many removals per judging pass (removal = a destroy; the put-down steps are free). */
const int kPerPass = 4;
inline int PassAllows(int removedThisPass) { return removedThisPass < kPerPass ? 1 : 0; }

inline const char* WhyToken(int why)
{
    switch (why)
    {
    case kWhyPlayerTied:         return "playerTied";
    case kWhyCarriedByPlayer:    return "carriedByPlayer";
    case kWhyCaged:              return "caged";
    case kWhyInCombatWithPlayer: return "inCombatWithPlayer";
    case kWhyPoseUnreadable:     return "poseUnreadable";
    case kWhyCarrierUnknown:     return "carrierUnknown";
    case kWhyCarryStuck:         return "carryStuck";
    case kWhyBedStuck:           return "bedStuck";
    case kWhyOwnCarryStuck:      return "ownCarryStuck";
    case kWhyInUnknown:          return "inUnknown";
    case kWhyNearPlayer:         return "nearPlayer";
    case kWhyNotedTooShort:      return "notedTooShort";
    case kWhyLinkTooFresh:       return "linkTooFresh";
    default:                     return "none";
    }
}

}   /* namespace coopor */

#endif
