/* inv4 (design inv4-owner-storage W2-W4; user decisions 2026-09-26; fold review-inv4). WHICH GAME WRITES A PLAYER
   BUILDING'S STORAGE. Pure decisions only; the plugin (items.cpp ItBoxOwnerOf / ItOwnerRungAt) reads the facts, the offline
   suite (coop-test) tests these. Header-only. No C++11.
   - What is INSIDE a player's house belongs to the HOUSE owner, whoever placed it (decision 2).
   - ONE WRITER (review-inv4): both games decide from the SAME input - the notebook's area map, which both receive from one
     clock - never from a local read. The owner's game writes only while the link is up AND the map shows the OWNER's slot
     loaded in the box's sector; the owner's own game uses exactly that test for itself.
   - FAIL SAFE: a player-owned box whose owner cannot be resolved here (slot unknown, stand-in unknown, faction record
     unreadable, map stale / notebook down) has NO writer on either game (kRungDefer): moves are refused. The area rule
     applies only when a FRESH map positively shows the owner not loaded (decision 1), or when the session link is down
     (nothing can cross - the lone game writes as it always has).
   - A player faction that is not this game's own reads as SHARED (two players one faction - out of scope): the area rule. */
#ifndef COOP_BOXOWNER_H
#define COOP_BOXOWNER_H

namespace coopown {

const int kOwnUnreadable = -1;   /* the building itself could not be read (ownership unknown - the area rule, as before) */
const int kOwnNone = 0;          /* no player owns it (a town's box, a nest's chest) */
const int kOwnMe = 1;            /* this game's player */
const int kOwnPeer = 2;          /* another player: a coop-p<slot> stand-in (by the table or by its record id) or a legacy coop-peer */
const int kOwnShared = 3;        /* a player faction that is not this game's own: a shared faction - out of scope, area rule */
const int kOwnUnknown = 4;       /* a faction whose record id cannot be read: it may be a player's - nobody writes */

/* one faction's class. isLocal = it IS this game's player faction; isStandIn = in this game's stand-in table; recordSlot =
   the slot its record id "coop-p<n>" names (-1 not a stand-in record, -2 the legacy "coop-peer", -3 record unreadable);
   isPlayerFlag = Faction+0x250 set; localKnown = this game's player faction could be read at all; mySlot = this game's own
   notebook slot (-1 unknown) - inv4b 6: a record naming THIS game's slot is this game's, not another player's */
inline int OwnerClassOf(int isLocal, int isStandIn, int recordSlot, int isPlayerFlag, int localKnown, int mySlot = -1)
{
    if (isLocal != 0) return kOwnMe;
    if (isStandIn == 0 && recordSlot >= 0 && recordSlot == mySlot) return kOwnMe;
    if (isStandIn != 0 || recordSlot >= 0 || recordSlot == -2) return kOwnPeer;
    if (isPlayerFlag != 0) return (localKnown != 0) ? kOwnShared : kOwnMe;
    if (recordSlot == -3) return kOwnUnknown;
    return kOwnNone;
}

/* decision 2: the host house decides when there is one and it is (or may be) a player's */
inline int UseHostOwner(int hostFound, int hostCls)
{
    return (hostFound != 0 && (hostCls == kOwnMe || hostCls == kOwnPeer || hostCls == kOwnShared || hostCls == kOwnUnknown)) ? 1 : 0;
}

/* the box's owner: the host's (via 1) when UseHostOwner, else its own (via 0) */
inline int PickOwner(int hostFound, int hostCls, int hostSlot, int selfCls, int selfSlot, int* slot, int* via)
{
    if (UseHostOwner(hostFound, hostCls) != 0) { *slot = hostSlot; *via = 1; return hostCls; }
    *slot = selfSlot; *via = 0;
    return selfCls;
}

/* build1h H1 (investigations/build1h-house-owner-design.md 2(a); owner decisions 2026-09-26): WHOSE IS A NEW PIECE placed inside
   a building? The placer's game decides at the commit from the host's owner class (hostCls / hostSlot as ItFactionOwnerClass
   reads them). Returns the slot the piece is HANDED to (the house owner), or -1 = the placer keeps it. *why: kHoNoHost
   (free-standing), kHoTown (nobody's house - decision 5, unchanged), kHoMine (the placer's own house), kHoHanded, kHoUnresolved
   (the house is or may be a player's but whose cannot be read, or its slot does not fit the PLACE byte: the placer keeps it,
   counted - fail visible). */
const int kHoNoHost = 0, kHoTown = 1, kHoMine = 2, kHoHanded = 3, kHoUnresolved = 4;
const int kHandSlotMax = 254;   /* the PLACE owner-slot byte carries 0..254; 0xFF = the sender */
inline int HouseOwnerOfNewPiece(int hostFound, int hostCls, int hostSlot, int mySlot, int* why)
{
    if (hostFound == 0) { *why = kHoNoHost; return -1; }
    if (hostCls == kOwnNone) { *why = kHoTown; return -1; }
    if (hostCls == kOwnMe) { *why = kHoMine; return -1; }
    if (hostCls == kOwnPeer && hostSlot >= 0 && hostSlot <= kHandSlotMax)
    {
        if (mySlot >= 0 && hostSlot == mySlot) { *why = kHoMine; return -1; }
        *why = kHoHanded;
        return hostSlot;
    }
    *why = kHoUnresolved;
    return -1;
}

/* build1h H1 (design 2(c)): the box's owner with the SHARED BUILD RECORD first - the registry row both games hold for a synced
   piece, filled from the one PLACE (recFound; recSlot = the owner's slot it names; mySlot = this game's slot). The record (via 2)
   beats the host (via 1), which beats the piece's own faction (via 0); a record without a usable slot falls through. */
inline int PickOwnerRec(int recFound, int recSlot, int mySlot, int hostFound, int hostCls, int hostSlot, int selfCls, int selfSlot,
                        int* slot, int* via)
{
    if (recFound != 0 && recSlot >= 0)
    {
        *slot = recSlot; *via = 2;
        return (mySlot >= 0 && recSlot == mySlot) ? kOwnMe : kOwnPeer;
    }
    return PickOwner(hostFound, hostCls, hostSlot, selfCls, selfSlot, slot, via);
}

const int kRungArea = -1;   /* the owner rung steps aside: the area rule decides */
const int kRungMine = 1;    /* == items.cpp kBoxMine: this game writes */
const int kRungHeld = 2;    /* == items.cpp kBoxHeld: the owner's (other) game writes */
const int kRungDefer = 4;   /* == items.cpp kBoxNoAnswer: nobody writes - refused on both games */

const int kWhyMine = 0, kWhyHeld = 1, kWhyAbsent = 2, kWhyNone = 3, kWhyUnreadable = 4, kWhyShared = 5,
          kWhyUnresolved = 6, kWhyStale = 7, kWhyLinkDown = 8;

/* THE OWNER RUNG - the same on both games. linked = the session link is up; ownerSlot = the owner's notebook slot as THIS
   game knows it (-1 unknown); mapOwnerLoaded = the notebook map's loaded bit for ownerSlot in the box's sector (1 set, 0 clear,
   -1 no fresh map / notebook down). */
inline int OwnerRung(int cls, int ownerSlot, int linked, int mapOwnerLoaded, int* why)
{
    if (cls == kOwnNone) { *why = kWhyNone; return kRungArea; }
    if (cls == kOwnUnreadable) { *why = kWhyUnreadable; return kRungArea; }
    if (cls == kOwnShared) { *why = kWhyShared; return kRungArea; }
    if (linked == 0) { *why = kWhyLinkDown; return kRungArea; }                 /* nothing crosses: the lone game's own rule */
    if (cls == kOwnUnknown || ownerSlot < 0) { *why = kWhyUnresolved; return kRungDefer; }
    if (mapOwnerLoaded < 0) { *why = kWhyStale; return kRungDefer; }            /* the plugin keeps the notebook-down refusal */
    if (mapOwnerLoaded == 1)
    {
        if (cls == kOwnMe) { *why = kWhyMine; return kRungMine; }
        *why = kWhyHeld; return kRungHeld;
    }
    *why = kWhyAbsent;   /* decision 1: a fresh map says the owner is not here -> the area rule (basepolicy still gates access) */
    return kRungArea;
}

/* the whole writer answer: the rung when it decided, else the area verdict */
inline int WriterIs(int rung, int areaVerdict) { return (rung >= 0) ? rung : areaVerdict; }

/* inv4b 3: a box whose owner cannot be read HERE (off the main thread - getOwnerFaction is an engine call - or its building not
   resolvable by key). knownCls = the class last read for that key on the main thread (kOwnUnreadable = never read). The area
   rule only where the box is KNOWN not player-owned (none / shared, as OwnerRung itself) or the link is down; else nobody writes. */
inline int UnreadRung(int knownCls, int linked)
{
    if (linked == 0) return kRungArea;
    if (knownCls == kOwnNone || knownCls == kOwnShared) return kRungArea;
    return kRungDefer;
}

/* inv4b 1: THE HAND-BACK STARTS ONLY ON A REAL ARRIVAL. since = when this game's own bit appeared in the box sector's notebook
   map (0 = not set now); mySlot = this game's slot; prevState = the key's hand-back entry from an EARLIER stamp (-1 none,
   0 asked, 2 contents on the way, 1 done); sameBuilding = that entry was decided on this same live building. A stamp of 0 or an
   unknown slot starts nothing (a notebook re-register clears the bit); a new stamp after a DONE hand-back on the same live
   building is a flap or a re-register - this game was the writer immediately before and holds the true contents - skipped. */
const int kHbNone = 0, kHbSkip = 1, kHbAsk = 2;
inline int HandbackStart(double since, int mySlot, int prevState, int sameBuilding)
{
    if (since <= 0.0 || mySlot < 0) return kHbNone;
    if (prevState == 1 && sameBuilding != 0) return kHbSkip;
    return kHbAsk;
}

/* inv4b 2: may this game answer an owner's hand-back ask with its contents? Only for a box the OWNER now writes (held by the
   owner rule), when this game really was its writer: it last wrote it by the area rule and still holds that area now. */
inline int HandbackServes(int heldByOwner, int wroteByArea, int areaMineNow)
{
    return (heldByOwner != 0 && wroteByArea != 0 && areaMineNow != 0) ? 1 : 0;
}

/* inv4b 7: a parity answer for a hand-back in `state`: the new state; *counted = 1 only for the FIRST answer. Contents that
   wait to be swapped in (2) are never released by a later answer, and a done hand-back (1) stays done. */
inline int HandbackAnswer(int state, int contents, int* counted)
{
    *counted = 0;
    if (state != 0) return state;
    *counted = 1;
    return (contents != 0) ? 2 : 1;
}

/* inv4b 5: the hand-back table is full - only a done entry may go; a pending one is never dropped */
inline int HandbackEvictable(int state) { return (state == 1) ? 1 : 0; }

}   /* namespace coopown */

#endif
