/* src/common/standinpurge.h - inv7a: WHAT HAPPENS TO ONE STAND-IN CHARACTER THE LOADED WORLD CARRIES.
 *
 * ONE RULE, the same one clockmath.h, loadlatch.h and retirewithdraw.h keep: nothing in here reads engine
 * memory, reads a global, calls the operating system or includes any header.  It is a pure function of its
 * arguments, so the offline suite drives every branch instead of believing a comment.
 *
 * WHY IT EXISTS (e47-inv7-replan.md section 3.1, gapA path 2).  A save made while linked carries the other
 * player's characters as this game's copies, in a stand-in faction (coop-p<n>, or an old save's coop-peer).
 * Loading it shows those copies AND the fresh ones the other game announces - the crew twice, the old set
 * AI-run and lootable.  At the end of a world load, before the first peer SPAWN is applied, every stand-in
 * character no mirror row knows is removed - caged and imprisoned ones included (owner decision 2026-09-26:
 * every copy of another player's character goes; the real ones are restored by their owner's game).
 * Buildings owned by stand-ins are NOT touched.
 *
 * THE ORDER OF THE STEPS IS THE ENGINE'S: a caged character leaves its cage first (setPrisonMode(false), at
 * the K2 safe point, because the cage's occupant set belongs to the AI worker), a carried one is put down by
 * its carrier (dropCarriedObject), one in a bed gets out of it (setBedMode(false)), and only then is it
 * destroyed.  A case that cannot be handled safely is SKIPPED, counted and logged - never forced.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_STANDINPURGE_H
#define COOP_COMMON_STANDINPURGE_H

namespace coopsp {

/* recordSlot is playerfaction.cpp's StandInRecordSlot: n >= 0 = the record id is coop-p<n>, -2 = the legacy
   coop-peer (protocol 67), -1 any other faction, -3 unreadable.  The stand-in TABLE is empty right after a
   load (ResetPlayerFactionState), so IsPeerFaction answers false there; the record id is what is asked. */
inline int IsStandInRecord(int recordSlot) { return (recordSlot >= 0 || recordSlot == -2) ? 1 : 0; }

/* The ACTION for one character on this pass. */
enum {
    kActNone        = 0,   /* not a stand-in, or a mirror row knows it (a live copy of this session) */
    kActDestroy     = 1,   /* destroy it now (the DropPeerOwnedCopy route) */
    kActCageOut     = 2,   /* queue setPrisonMode(false) for the K2 safe point; destroyed on a later pass */
    kActWaitCage    = 3,   /* its cage-out is queued and has not taken yet */
    kActDropCarrier = 4,   /* its carrier puts it down first (dropCarriedObject) */
    kActBedOut      = 5,   /* it gets out of its bed first (setBedMode(false)) */
    kActDropOwn     = 6,   /* it puts down what it carries first */
    kActSkip        = 7    /* cannot be handled safely: left standing, counted, logged (*why) */
};

/* WHY a character was skipped. */
enum {
    kWhyNone           = 0,
    kWhyGuarded        = 1,   /* the watched player / a player-faction character (H030) - never destroyed */
    kWhyPoseUnreadable = 2,   /* Character +0x2F8 could not be read */
    kWhyCageNoAddr     = 3,   /* setPrisonMode has no address in this build's table */
    kWhyCageTimeout    = 4,   /* the cage-out did not take before the wait limit (the K2 point did not run) */
    kWhyCarrierUnknown = 5,   /* carried, and no character in the update list carries it */
    kWhyCarryStuck     = 6,   /* the carrier was told to put it down and it is still carried */
    kWhyBedStuck       = 7,   /* told to leave its bed (or no setBedMode address) and still in it */
    kWhyOwnCarryStuck  = 8,   /* told to put down what it carries and still carrying */
    kWhyInUnknown      = 9    /* +0x2F8 names a state other than 0 / 1 bed / 2 cage */
};

/* Everything the decision needs about one character, read by the caller. */
struct Seen
{
    int recordSlot;     /* StandInRecordSlot of its faction */
    int known;          /* 1 = FindSpawnedUid answers a uid for it */
    int guarded;        /* 1 = the watched player or a player-faction character */
    int inSomething;    /* Character +0x2F8: 0 nothing, 1 bed, 2 cage, -1 unreadable */
    int cageAddr;       /* 1 = setPrisonMode has an address */
    int cageQueued;     /* 1 = its cage-out was already queued this load */
    int cageTimedOut;   /* 1 = the wait limit has passed */
    int beingCarried;   /* Character +0x3D4 */
    int carrierFound;   /* 1 = a character in the update list carries it */
    int carrierTried;   /* 1 = that carrier was already told to put it down */
    int bedTried;       /* 1 = setBedMode(false) was already called (or has no address) */
    int carrying;       /* Character +0x348: it carries something itself */
    int ownDropTried;   /* 1 = it was already told to put that down */
};

inline Seen SeenNone()
{
    Seen s;
    s.recordSlot = -1; s.known = 0; s.guarded = 0; s.inSomething = 0; s.cageAddr = 1; s.cageQueued = 0;
    s.cageTimedOut = 0; s.beingCarried = 0; s.carrierFound = 0; s.carrierTried = 0; s.bedTried = 0;
    s.carrying = 0; s.ownDropTried = 0;
    return s;
}

/* ONE STEP.  The caller acts on the answer, re-reads the character, and asks again (a drop and a bed-out are
   synchronous; a cage-out waits for the K2 point).  Total over every input - no unhandled combination. */
inline int Decide(const Seen& s, int* why)
{
    *why = kWhyNone;
    if (!IsStandInRecord(s.recordSlot)) return kActNone;
    if (s.known != 0) return kActNone;
    if (s.guarded != 0) { *why = kWhyGuarded; return kActSkip; }
    if (s.inSomething < 0) { *why = kWhyPoseUnreadable; return kActSkip; }
    if (s.inSomething == 2)
    {
        if (s.cageAddr == 0) { *why = kWhyCageNoAddr; return kActSkip; }
        if (s.cageTimedOut != 0) { *why = kWhyCageTimeout; return kActSkip; }
        return s.cageQueued != 0 ? kActWaitCage : kActCageOut;
    }
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

/* Is a purge due?  Once per world generation, and only while engine writes are allowed. */
inline int Due(long worldGen, long doneGen, int writesBlocked) { return (writesBlocked == 0 && worldGen != doneGen) ? 1 : 0; }

/* Does the arrival queue stay held this frame?  Only while a cage-out is still waiting and the limit has not
   passed; at the limit the waiting ones are skipped (kWhyCageTimeout) and the purge finishes. */
inline int HoldDrain(int waitingCages, unsigned long elapsedMs, unsigned long limitMs)
{
    return (waitingCages > 0 && elapsedMs < limitMs) ? 1 : 0;
}

inline const char* WhyText(int why)
{
    switch (why)
    {
    case kWhyGuarded:        return "the watched player / a player-faction character (H030)";
    case kWhyPoseUnreadable: return "its bed/cage state (+0x2F8) is unreadable";
    case kWhyCageNoAddr:     return "caged, and setPrisonMode has no address in this build";
    case kWhyCageTimeout:    return "caged, and the cage-out did not take within the wait limit (the K2 safe point did not run)";
    case kWhyCarrierUnknown: return "carried, and no character in the update list carries it";
    case kWhyCarryStuck:     return "carried, and still carried after its carrier was told to put it down";
    case kWhyBedStuck:       return "in a bed, and still in it after setBedMode(false) (or no setBedMode address)";
    case kWhyOwnCarryStuck:  return "carrying something, and still carrying after dropCarriedObject";
    case kWhyInUnknown:      return "its bed/cage state names something other than bed or cage";
    default:                 return "none";
    }
}

/* inv7a2 (run T412): the purge is re-armed by EVENTS. A pass starts when the load pass is due OR a trigger asked for
   one (a stand-in platoon woke, the game link came up); the triggers that started it are named in its log line. */
const int kTrigLoad = 1, kTrigWake = 2, kTrigLinkUp = 4;
const int kTrigSweep = 8;   /* area2 fold: the P033 sweep met an unregistered stand-in copy (SweepRequestDecide) */
inline int PassDue(int loadDue, int reqMask) { return (loadDue != 0 || reqMask != 0) ? 1 : 0; }
inline const char* TriggerText(int m)
{
    static const char* const t[16] = { "request", "load", "wake", "load+wake", "link-up", "load+link-up", "wake+link-up", "load+wake+link-up",
        "sweep", "load+sweep", "wake+sweep", "load+wake+sweep", "link-up+sweep", "load+link-up+sweep", "wake+link-up+sweep", "load+wake+link-up+sweep" };
    return t[m & 15];
}

/* area2 fold (review-area2 HIGH): MAY THE P033 SWEEP ASK FOR A PURGE PASS for one unregistered stand-in copy it met?
   Never after a uid mirror / hash index FULL error (a live copy could look unregistered - FindSpawnedUid answers 0 for a
   refused registration); only ONCE per copy per world generation (a copy the purge leaves standing must not ask forever);
   and not while a pass runs or within gapMs of the END of the last one (wait: not marked, asked again on a later tick). */
const int kSweepReqAsk = 0, kSweepReqSeen = 1, kSweepReqFull = 2, kSweepReqWait = 3;
const unsigned long kSweepReqGapMs = 5000;
inline int SweepRequestDecide(int tableFull, int alreadyAsked, int passRunning, int passEverEnded, unsigned long sinceEndMs, unsigned long gapMs)
{
    if (tableFull != 0) return kSweepReqFull;
    if (alreadyAsked != 0) return kSweepReqSeen;
    if (passRunning != 0) return kSweepReqWait;
    if (passEverEnded != 0 && sinceEndMs < gapMs) return kSweepReqWait;
    return kSweepReqAsk;
}
/* A pass ONLY the sweep asked for never holds the in-queue for a cage wait: it leaves caged copies to the wake/load passes. */
inline int SweepOnlyPass(int why) { return (why == kTrigSweep) ? 1 : 0; }

/* inv7e (run T412): may a context squad take the other game's platoon stringID?  Not when there is none, not when
   another squad of the same faction already holds that id (the engine files a squad under its id - two would
   collide), and not when the squad has no state record to carry it across a save. */
const int kCarryYes = 0, kCarrySkipEmpty = 1, kCarrySkipClash = 2, kCarrySkipNoState = 3;
inline int CarryIdDecide(int idEmpty, int stateOk, int clash)
{
    if (idEmpty != 0) return kCarrySkipEmpty;
    if (clash != 0) return kCarrySkipClash;
    if (stateOk == 0) return kCarrySkipNoState;
    return kCarryYes;
}

/* inv7a3 (review-inv7a2 HIGH): may the same-id supersede retire / destroy this game's squad for another game's
   announcement?  Never when THIS game minted the id (decision 31(a): the number's block is my relay slot) - that squad
   is the original and the announcement is the other game's stale copy from a save (the death watch's rule). A block of
   -1 (no numeric tail) or a slot of -1 (no relay) gives no answer: the old rule stands. */
inline int SupersedeRefuseOwn(int blockOwner, int mySlot) { return (mySlot >= 0 && blockOwner >= 0 && blockOwner == mySlot) ? 1 : 0; }

/* inv7a3 (review-inv7a2 MEDIUM): Platoon::activate builds no characters (the squad's first update does), so a woken
   stand-in platoon is re-checked on each main-thread tick: gone (no longer awake under its id) -> dropped; members
   exist -> a purge pass; still empty at the cap -> gave up, a pass anyway; else wait. Unreadable members wait. */
const int kWokenWait = 0, kWokenPurge = 1, kWokenGone = 2, kWokenGaveUp = 3;
const unsigned long kWokenCapMs = 5000;
inline int WokenRecheck(int stillAwake, int activeChars, unsigned long elapsedMs, unsigned long capMs)
{
    if (stillAwake == 0) return kWokenGone;
    if (activeChars > 0) return kWokenPurge;
    if (elapsedMs >= capMs) return kWokenGaveUp;
    return kWokenWait;
}
/* inv7a3 (review-inv7a2 LOW): the pass line's prefix. A pass the load started is "load" whatever else asked (the grep key
   "standin purge at load:"); the full trigger list is printed after it (triggers=). */
inline const char* PassPrefix(int m) { return (m & kTrigLoad) != 0 ? "load" : TriggerText(m); }

/* inv7e2 (T418): after a reload + rejoin the reloaded game's sweep adopts the town people its save brought back and
   announces them; the other game's same-id squads arrive 0.5 s later. The continuously running copy wins (decisions
   15/33/37): a local awake squad retires when EVERY session member is a character this game adopted by its own sweep
   during the CURRENT link generation. A member of any other kind - a SPAWN from the peer, an XFER, a uid handed off,
   an adoption in an EARLIER link generation - keeps the squad. Unreadable members keep it.
   FOLD (review-inv7e2 HIGH): the generation is this game's WORLD-LOAD generation, not the link's - a game that did not
   reload also sweeps after a re-link. The squad retires only when every member was picked up from the CURRENT world
   load, that load happened AFTER this game's last game-link down (it reloaded during the gap), and this game's slot is
   known (fail closed). A game that did not reload keeps its squads exactly as before (the counted duplicate). */
const int kMemberAdoptedThisGen = 0, kMemberEarlierGen = 1, kMemberOther = 2;
inline int ReloadMemberClass(int sweepAdopted, long adoptGen, long curGen, int mine)
{
    if (sweepAdopted == 0 || mine == 0) return kMemberOther;
    return adoptGen == curGen ? kMemberAdoptedThisGen : kMemberEarlierGen;
}
const int kReloadNoSession = 0, kReloadRetire = 1, kReloadKeepEarlierGen = 2, kReloadKeepSession = 3, kReloadUnreadable = 4,
          kReloadKeepNotReloaded = 5, kReloadKeepNoSlot = 6;
/* 1 = this game's world was loaded after its last game-link down (-1 = no link down yet: never). */
inline int ReloadedSinceLinkDown(long worldGenNow, long worldGenAtLastDown) { return (worldGenAtLastDown >= 0 && worldGenNow > worldGenAtLastDown) ? 1 : 0; }
inline int ReloadRetireDecide(int readable, int slotKnown, int reloadedSinceDown, int thisGen, int earlierGen, int other)
{
    if (readable == 0) return kReloadUnreadable;
    if (earlierGen > 0) return kReloadKeepEarlierGen;
    if (other > 0) return kReloadKeepSession;
    if (thisGen <= 0) return kReloadNoSession;
    if (slotKnown == 0) return kReloadKeepNoSlot;
    if (reloadedSinceDown == 0) return kReloadKeepNotReloaded;
    return kReloadRetire;
}

}   /* namespace coopsp */

#endif
