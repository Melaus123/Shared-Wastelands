/* inv4 (design inv4-owner-storage W2-W4; user decisions 2026-09-26; fold review-inv4). WHICH GAME WRITES A PLAYER
   BUILDING'S STORAGE. Pure decisions only; the plugin (items.cpp ItBoxOwnerOf / ItOwnerRungAt) reads the facts, the offline
   suite (coop-test) tests these. Header-only. No C++11.
   - What is INSIDE a player's house belongs to the HOUSE owner, whoever placed it (decision 2).
   - ONE WRITER: every game decides from the SAME input - the notebook's area map, which all receive from one clock - never
     from a local read. The owner's game writes only while the map shows the OWNER's slot loaded in the box's sector; the
     owner's own game uses exactly that test for itself. The number of players in the world is not an input: a game alone in
     the world decides as a game with company does.
   - FAIL SAFE: a player-owned box whose owner cannot be resolved here (slot unknown, stand-in unknown, faction record
     unreadable, map stale / notebook down) has NO writer on any game (kRungDefer): moves are refused. The area rule
     applies only when a FRESH map positively shows the owner not loaded (decision 1).
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
   (the house is or may be a player's but whose cannot be read, or its slot is not a notebook slot: the placer keeps it,
   counted - fail visible). */
const int kHoNoHost = 0, kHoTown = 1, kHoMine = 2, kHoHanded = 3, kHoUnresolved = 4;
const int kHandSlotMax = 1023;   /* the PLACE's two-byte owner number carries every notebook slot 0..1023 (buildwire.h kBuildOwnerSlotMax) */
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

/* A PRODUCTION BUILDING WHOSE ONLY PLAYER CLAIM IS ITS OWN FACTION MARK (via 0: no build record names an owner and no
   player's house holds it) - a wild resource node the engine marked for a worker - is NOBODY'S: the area rule writes it. That
   mark is read by each game on its own copy and the two copies can carry different marks, so it is not one input both games
   share; the build record (via 2) and a player's house (via 1) are, and keep the owner rule. isProduction = the building's
   operate slot is ProductionBuilding::operate. */
inline int MarkOnlyNodeClass(int isProduction, int via, int cls)
{
    if (isProduction != 0 && via == 0 && cls != kOwnUnreadable) return kOwnNone;
    return cls;
}

/* THE WORLD RESOURCE NODE (an ore rock - Iron Resource, Copper Resource, Stone Resource). The base game's data marks the
   world's rocks' records "is foliage" (templates 42248, 42249, 42250) and leaves that mark clear on the mines and wells players
   build (Ore Mine, Stone Mine, Well). Those are production buildings a worker operates that take no inputs too, so being
   operated with no inputs does not say the world placed a piece; the foliage mark does. A world node is a production building
   whose game-data record is marked foliage, that a worker operates (the needs-operating flag +0x3A9 set:
   ProductionBuilding::update 0x29AD50 runs the step itself only while it is clear), that takes no inputs (input count +0x480 is
   0) and that has no build record. It is nobody's piece: the build scan never registers it as this game's own, a saved row of it
   is dropped from the player's records, a copy-list row of it is retired and a PLACE naming it is refused (build.cpp), whatever
   faction mark it carries. Its construction materials are no test - a rock's construction list has one entry. isProd /
   needsOperating / hasRow / isFoliage are 1 or 0, inputCount the count; -1 = unreadable. Anything unreadable or out of range
   answers 0: the piece is not a world node (a player's mine or well keeps its row and is registered). */
inline int WorldNodeOf(int isProd, int needsOperating, int inputCount, int hasRow, int isFoliage)
{
    return (isProd == 1 && needsOperating == 1 && inputCount == 0 && hasRow == 0 && isFoliage == 1) ? 1 : 0;
}

const int kRungArea = -1;   /* the owner rung steps aside: the area rule decides */
const int kRungMine = 1;    /* == items.cpp kBoxMine: this game writes */
const int kRungHeld = 2;    /* == items.cpp kBoxHeld: the owner's (other) game writes */
const int kRungDefer = 4;   /* == items.cpp kBoxNoAnswer: nobody writes - refused on both games */

const int kWhyMine = 0, kWhyHeld = 1, kWhyAbsent = 2, kWhyNone = 3, kWhyUnreadable = 4, kWhyShared = 5,
          kWhyUnresolved = 6, kWhyStale = 7;

/* THE OWNER RUNG - the same on every game, whatever the number of players in the world. ownerSlot = the owner's notebook slot
   as THIS game knows it (-1 unknown); mapOwnerLoaded = the notebook map's loaded bit for ownerSlot in the box's sector (1 set,
   0 clear, -1 no fresh map / notebook down). */
inline int OwnerRung(int cls, int ownerSlot, int mapOwnerLoaded, int* why)
{
    if (cls == kOwnNone) { *why = kWhyNone; return kRungArea; }
    if (cls == kOwnUnreadable) { *why = kWhyUnreadable; return kRungArea; }
    if (cls == kOwnShared) { *why = kWhyShared; return kRungArea; }
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
   rule only where the box is KNOWN not player-owned (none / shared, as OwnerRung itself); else nobody writes - whatever the
   number of players in the world. */
inline int UnreadRung(int knownCls)
{
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
/* T-823 fold 3: a waiting hand-back (state 0) whose ask did not go out (askSent 0: the box could not be read, or there was no road
   to the area's holder) is asked again at each tick until it goes; its wait then ends on the answer as usual. 1 = ask now. */
inline int HandbackAskDue(int state, int askSent) { return (state == 0 && askSent == 0) ? 1 : 0; }
/* T-823 fold 3: 1 = no other game can answer the hand-back with contents - this game holds the box's area itself (holderSlot is
   mySlot), and only the area's holder serves a hand-back's contents (HandbackServes). The hand-back then does not wait at all. */
inline int HandbackNobodyAnswers(int holderSlot, int mySlot) { return (holderSlot >= 0 && holderSlot == mySlot) ? 1 : 0; }
/* T-823 (run T1424): DOES THE SECTOR ARRIVAL ASK LIST THIS BOX? The ask goes to the game that holds the area, and only a box's
   writer answers with its contents. cls = the box's owner class; writerHeld = this game's writer answer names another game
   (kRungHeld / kBoxHeld); writerSlot = that game's slot (-1 not known); areaSlot = the slot of the OTHER game holding the box's area
   (-1 none, not known, or this game). A box no player owns, or whose building cannot be read, is listed as before (the area rule);
   this game's own box never (its hand-back covers it); another player's, a shared or an unresolved owner's box only when its writer
   IS the area's holder. A box whose writer is a game other than the area's holder is not listed (that holder would answer "not
   holder"); one this game writes, or nobody writes now, is not listed either. */
inline int ParityAskListsBox(int cls, int writerHeld, int writerSlot, int areaSlot)
{
    if (cls == kOwnMe) return 0;
    if (cls != kOwnPeer && cls != kOwnShared && cls != kOwnUnknown) return 1;
    return (writerHeld != 0 && writerSlot >= 0 && writerSlot == areaSlot) ? 1 : 0;
}
/* T-823 fold 3: THE ARRIVAL ASK'S FIRST CUT by the owner class alone, before the key is read. kPaTake = listed as before (no
   player owns it, or its building cannot be read); kPaLeave = left out (this game's own box: its hand-back covers it); kPaByWriter =
   a player's box whose writer decides (ParityAskListsBox) once its key and sector are read. Values of their own, not the kPb* modes. */
const int kPaTake = 0, kPaLeave = 1, kPaByWriter = 2;
inline int ParityAskBoxClass(int cls)
{
    if (cls == kOwnMe || cls == kOwnPeer) return kPaLeave;   /* a player's box: its world-server record is applied at the load (items.cpp BoxLiveApplyOne) */
    if (cls == kOwnShared || cls == kOwnUnknown) return kPaByWriter;
    return kPaTake;
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

/* an arrival that would ask (kHbAsk) on a box with no hand-back on record, which this game placed itself (the commit or the placing
   lever) while the area's "loaded since" stamp was the one it has now (placedSince == nowSince): from that placement on this game
   has held the area without a break and written its own box itself, so no other game can have written it - nothing is asked and
   the hand-back is recorded done. A different stamp (the area was left and loaded again, so another game may have written the box
   meanwhile), no stamp at the placement (0), or a hand-back table too full to record the skip (tableFull): asked as usual. */
inline int HandbackMadeHereSkip(int start, int haveRecord, int madeHere, double placedSince, double nowSince, int tableFull)
{
    return (start == kHbAsk && haveRecord == 0 && madeHere != 0 && placedSince != 0.0 && placedSince == nowSince && tableFull == 0)
           ? 1 : 0;
}

/* an arrival that would ask (kHbAsk) on a box with no hand-back on record, which this game last wrote BY THE AREA RULE (wroteByArea:
   the area rule answered this game for the box and no later area answer for it said another game holds the area) while the area's
   "loaded since" stamp was the one it has now (wroteSince == nowSince), in an area this game holds now (areaMineNow): only the area's
   holder writes such a box, so no other game has written it since, and no other game could answer the ask with contents (it answers
   only for an area it holds now, HandbackServes) - nothing is asked and the hand-back is recorded done. A different stamp (the area
   was left and loaded again), no stamp at the write (0), the area held by another game now, or a table too full to record the
   skip (tableFull): asked as usual. */
inline int HandbackAreaWroteSkip(int start, int haveRecord, int wroteByArea, double wroteSince, double nowSince, int areaMineNow,
                                 int tableFull)
{
    return (start == kHbAsk && haveRecord == 0 && wroteByArea != 0 && wroteSince != 0.0 && wroteSince == nowSince
            && areaMineNow != 0 && tableFull == 0) ? 1 : 0;
}

/* a hand-back that has not ended: asked with no answer yet (0), or contents on the way to the swap (2); done (1) is not waiting */
inline int HandbackWaiting(int state) { return (state == 0 || state == 2) ? 1 : 0; }

/* a move rung into the drain's ring with ticket moveSeq before this game saved a box's baseline for a hand-back (holdSeq = the
   ring's next ticket at the save) is already in that baseline. Tickets wrap: compared as a 32-bit distance. */
inline int HandbackMoveBeforeHold(long moveSeq, long holdSeq)
{
    return ((unsigned int)moveSeq - (unsigned int)holdSeq >= 0x80000000u) ? 1 : 0;
}

/* a merged hand-back swap (this game's moves since the ask laid on the holder's rows) removes this game's items, then lays the set.
   precheckOk: every item was found at its own cell before anything left; removeFailed: removes that did not work (the swap stops at
   the first). kHbSwapNotRun = nothing left the box; kHbSwapPutBack = nothing of the answer is laid and the items that left are put
   back at their own cells; kHbSwapGo = every item left, the set is laid. */
const int kHbSwapGo = 0, kHbSwapNotRun = 1, kHbSwapPutBack = 2;
inline int HandbackSwapRemoveOutcome(int precheckOk, int removeFailed)
{
    if (precheckOk == 0) return kHbSwapNotRun;
    return (removeFailed > 0) ? kHbSwapPutBack : kHbSwapGo;
}

/* a swap whose re-digest differs from the holder's: a plain swap is asked once more (tries = asks already made); a merged swap is not
   asked again - the answer could be swapped in plain over this game's rows before the safety net has sent them */
inline int HandbackReaskAfterMismatch(int merged, int tries)
{
    return (merged == 0 && tries < 1) ? 1 : 0;
}

/* an answer for a box: a live pull lets it be swapped in over this game's own box only while the box owes no rows to the other games
   (owed = rows laid by a merged swap, or a box kept over the holder's answer, the safety net has not sent yet) */
inline int HandbackPullStands(int owed, int pullLive)
{
    return (owed == 0 && pullLive != 0) ? 1 : 0;
}

}   /* namespace coopown */

#endif
