/* src/common/ownerroute.h - M7b SLICE 1 (T-197; game-to-game protocol 113): A COPY-EFFECT REQUEST GOES TO THE TARGET'S OWNER.
 *
 * Before M7b every request about another player's character went g_transport->Send(0, ...) - "the one linked game", whoever
 * owned the target. With three or more games that is the wrong game whenever the target's owner is not the session peer.
 * Now each request names its TARGET (a uid) and goes to the player that OWNS it (net/session.cpp g_owner, the M5b owner table,
 * keyed by player number); each answer goes back to the player that ASKED (the handler's sender, the same kind of key).
 *
 * THE ROAD (AddrRoute), one per send, never two:
 *   the owner is the session peer and the session link is up -> the session link (exactly the pre-M7b send);
 *   otherwise, the notebook road up (StoreLiveReady)          -> LIVE route SLOT, target = the owner's slot;
 *   otherwise                                                  -> none (kAddrWhyNoSlot: no road reaches that player);
 *   no owner on record (or the owner is this game)             -> none (kAddrWhyNoOwner).
 * A RECORDED KEY (AddrOwnerSlotOf) is a player key (liveenvelope.h PlayerKeyOf): RelayPeerId(slot) names that slot; a raw id
 * (the session peer before its PEER_SLOT arrived) names the session peer - kAddrOwnerSessionPeer.
 *
 * THE TYPE GROUPS of the [net] REPORT's addr.<group>[toOwner,viaSession,viaLive,noOwner,noSlot] (AddrGroupOf): hit (13), shot
 * (63), treat (49), prison (48), capture (60 CAPTURE, 61 CAPTURE_DONE, 62 CAPTURE_PLACED), carry (47 CARRY_BREAK), item (M7b slice 2:
 * 37 ITEM_MOVE, 38 ITEM_REQUEST, 39 ITEM_CONFIRM, 40 ITEM_PLACED, 41 ITEM_REVOKE, 53 PARITY_REQ, 54 PARITY_BOX); M7b slice 4 (C3 + C4): say
 * (43), stats (44), crime (45), bounty (46), talk (59), build (50, with the farm's kinds), door (42 DOOR_STATE) - SideRouteOf below.
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_OWNERROUTE_H
#define COOP_COMMON_OWNERROUTE_H

#include "liveenvelope.h"
#include <set>

namespace cooplive {

enum { kAddrNone = 0, kAddrSession = 1, kAddrLive = 2 };
enum { kAddrWhyOk = 0, kAddrWhyNoOwner = 1, kAddrWhyNoSlot = 2,
       kAddrWhyNotInWorld = 3 };   /* M7b slice 2 fold 1: the target slot is not in the PLAYERS roster - counted noSlot and live.toSlot.noSuchSlot */
const int kAddrOwnerNone = -1;          /* no owner on record */
const int kAddrOwnerSessionPeer = -2;   /* the session peer, its slot not yet known (a raw record) */
const int kAddrOwnerUnknown = -3;       /* M7b slice 2 fold 1: no target known (no holder, no ask on record) - only the session link's fallback */

struct AddrPlan
{
    int road;            /* kAddrNone / kAddrSession / kAddrLive */
    unsigned int slot;   /* kAddrLive: the LIVE SLOT target */
    int why;             /* kAddrNone: kAddrWhyNoOwner or kAddrWhyNoSlot */
    AddrPlan() : road(kAddrNone), slot(0), why(kAddrWhyOk) {}
};

/* The owner's slot from a recorded player key (g_owner) or a handler's sender key; linkPeerSlot = the session peer's slot
   (-1 unknown). */
inline int AddrOwnerSlotOf(unsigned int key, int linkPeerSlot)
{
    const unsigned int k = PlayerKeyOf(key, linkPeerSlot);
    if (IsRelayPeer(k)) return (int)RelayPeerSlot(k);
    return kAddrOwnerSessionPeer;
}

inline AddrPlan AddrRoute(int ownerSlot, int sessionPeerSlot, bool sessionUp, bool liveReady)
{
    AddrPlan r;
    if (ownerSlot == kAddrOwnerSessionPeer)
    {
        if (sessionUp) r.road = kAddrSession; else r.why = kAddrWhyNoSlot;
        return r;
    }
    if (ownerSlot < 0 || (unsigned int)ownerSlot > kLiveSlotMax) { r.why = kAddrWhyNoOwner; return r; }
    if (sessionUp && sessionPeerSlot >= 0 && ownerSlot == sessionPeerSlot) { r.road = kAddrSession; return r; }
    if (liveReady) { r.road = kAddrLive; r.slot = (unsigned int)ownerSlot; return r; }
    r.why = kAddrWhyNoSlot;
    return r;
}

enum { kAddrGroupHit = 0, kAddrGroupShot = 1, kAddrGroupTreat = 2, kAddrGroupPrison = 3, kAddrGroupCapture = 4, kAddrGroupCarry = 5,
       kAddrGroupItem = 6,
       kAddrGroupSay = 7, kAddrGroupStats = 8, kAddrGroupCrime = 9, kAddrGroupBounty = 10, kAddrGroupTalk = 11, kAddrGroupBuild = 12,
       kAddrGroupDoor = 13,   /* M7b slice 4 + DOOR_STATE */
       kAddrGroupName = 14, kAddrGroupSlave = 15, kAddrGroupHire = 16,
       kAddrGroupCount = 17 };
inline int AddrGroupOf(unsigned int innerType)
{
    switch (innerType)
    {
    case kInnerHit: return kAddrGroupHit;
    case kInnerShot: return kAddrGroupShot;
    case kInnerTreat: return kAddrGroupTreat;
    case kInnerPrison: return kAddrGroupPrison;
    case kInnerCapture: case kInnerCaptureDone: case kInnerCapturePlaced: return kAddrGroupCapture;
    case kInnerCarryBreak: return kAddrGroupCarry;
    case kInnerItemMove: case kInnerItemRequest: case kInnerItemConfirm: case kInnerItemPlaced: case kInnerItemRevoke:
    case kInnerParityReq: case kInnerParityBox: return kAddrGroupItem;   /* M7b slice 2 */
    case kInnerSay: return kAddrGroupSay;   /* M7b slice 4 + DOOR_STATE: the side messages */
    case kInnerStats: return kAddrGroupStats;
    case kInnerCrime: return kAddrGroupCrime;
    case kInnerBounty: return kAddrGroupBounty;
    case kInnerTalk: return kAddrGroupTalk;
    case kInnerBuild: return kAddrGroupBuild;
    case kInnerDoorState: return kAddrGroupDoor;
    case kInnerName: return kAddrGroupName;
    case kInnerSlave: return kAddrGroupSlave;
    case kInnerHire: return kAddrGroupHire;
    default: return -1;
    }
}
inline const char* AddrGroupName(int g)
{
    static const char* const k[kAddrGroupCount] = { "hit", "shot", "treat", "prison", "capture", "carry", "item",
                                                    "say", "stats", "crime", "bounty", "talk", "build", "door",
                                                    "name", "slave", "hire" };   /* M7b slice 4 */
    return (g >= 0 && g < kAddrGroupCount) ? k[g] : "?";
}

/* M7b FOLD 1 (review 2026-09-30) - THREE PURE RULES.
 * AddrLiveSendGoes: a LIVE SLOT send goes unless the PLAYERS roster says the slot is NOT in the world. inWorld is
 *   coop::StoreRosterSlotInWorld: 1 in the world -> sent; -1 the roster is stale or unknown -> sent (the world server judges);
 *   0 not in the world -> NOT sent, counted noSlot and live.toSlot.noSuchSlot, false to the caller: not delivered, so it retries
 *   or waits exactly as for a session-link failure (before M7b a capture waited, prison and treatment retried).
 * AddrNoOwnerCountOnce: a request whose target has no recorded owner counts noOwner ONCE per (group, uid) until a send for that
 *   uid finds an owner (the entry is then forgotten, so an owner lost again counts again). The sender keeps retrying; only the
 *   count is once. uid 0 counts every time. Bounded: kAddrNoOwnerSeenMax entries, then the set starts again.
 * AddrPrisonRefusesEarlier: a PRISON IN for a uid that already holds a waiting request from a DIFFERENT player (SamePlayer: the
 *   session peer's raw key and its slot key are one player) refuses that earlier asker before the new one is stored, so the first
 *   guard's game takes its copy out of the cage instead of waiting on a caging its owner will never answer.
 */
inline bool AddrLiveSendGoes(int inWorld) { return inWorld != 0; }
const size_t kAddrNoOwnerSeenMax = 4096;
inline unsigned long long AddrNoOwnerKey(int group, unsigned int uid)
{
    return ((unsigned long long)(unsigned int)(group + 1) << 32) | (unsigned long long)uid;
}
inline bool AddrNoOwnerCountOnce(std::set<unsigned long long>* seen, int group, unsigned int uid, bool noOwner)
{
    if (seen == 0 || uid == 0) return noOwner;
    const unsigned long long k = AddrNoOwnerKey(group, uid);
    if (!noOwner) { seen->erase(k); return false; }
    if (seen->find(k) != seen->end()) return false;
    if (seen->size() >= kAddrNoOwnerSeenMax) seen->clear();
    seen->insert(k);
    return true;
}
inline bool AddrPrisonRefusesEarlier(bool had, unsigned int earlierPeer, unsigned int newPeer, int linkPeerSlot)
{
    return had && !SamePlayer(earlierPeer, newPeer, linkPeerSlot);
}

/* M7b SLICE 2 (T-197; game-to-game protocol 115) - THE EIGHT ITEM MESSAGES ON ONE ADDRESSED ROAD.
 * THE ROAD: a move and a box push ride the character stream's road (liverelay.h CharStreamRoad, net/session.cpp CharRoadNow);
 * an ADDRESSED send picks its road per TARGET (ItemTargetRoad, fold 1) - the session peer keeps the character road while the
 * session link is its road (so a box answer still reaches its asker before any later move of that box), any other game goes
 * through the world server by slot. Answers to one target keep one road while that target's reachability is unchanged.
 * WHO (ItemAddrDecide) - the answer is an owner slot in AddrRoute's form (a slot, kAddrOwnerSessionPeer or kAddrOwnerNone):
 *   kItemToChar  a request about a character: its OWNER (the owner table's key; none on record -> nobody);
 *   kItemToArea  a request about a box, a shop or the ground, or a parity ask: the WRITER of its key - a player-owned box's
 *                owner, else the HOLDER of the area it is decided in (decision 48: the area holder or the world server, never
 *                the host); none known -> kAddrOwnerUnknown (ItemTargetRoad: the session peer while the session link is
 *                its road - no notebook: the host holds every box, the pre-M7b send - else nobody);
 *   kItemToAsker an answer (CONFIRM, PLACED, REVOKE, the shop tail, a parity answer): the game that asked / was asked; none on
 *                record -> kAddrOwnerUnknown, as above.
 * HOW (ItemTargetRoad, fold 1): (a) the session peer (or an unknown target) with the session link up, and either the character
 * road is the session link or the peer is not in the world roster -> the session link; (b) otherwise this game's world-server
 * link up and the target slot in the roster (or the roster stale - the world server judges) -> LIVE SLOT; (c) otherwise not
 * sent (noSlot; noOwner for an unknown target) - the caller's retry / timeout applies. */
enum { kItemRoadNone = 0, kItemRoadLive = 1, kItemRoadSession = 2 };   /* == liverelay.h kCharRoad* (net/session.cpp asserts it) */
enum { kItemToChar = 0, kItemToArea = 1, kItemToAsker = 2 };
inline int ItemAddrDecide(int kind, bool haveOwner, unsigned int ownerKey, int holderSlot, bool haveAsked, unsigned int askedKey,
                          int linkPeerSlot)
{
    if (kind == kItemToChar) return haveOwner ? AddrOwnerSlotOf(ownerKey, linkPeerSlot) : kAddrOwnerNone;
    if (kind == kItemToArea)
    {
        if (holderSlot >= 0 && (unsigned int)holderSlot <= kLiveSlotMax) return holderSlot;
        return kAddrOwnerUnknown;
    }
    if (haveAsked) return AddrOwnerSlotOf(askedKey, linkPeerSlot);
    return kAddrOwnerUnknown;
}
/* peerInWorld / targetInWorld: coop::StoreRosterSlotInWorld of the session peer's / the target's slot (1 in, 0 out, -1 stale or
   unknown). FAILS BEFORE (fold 1): the road was CharRoadNow alone - on the session road an answer to a request relayed from a third
   game was never sent (the asker lapsed and kept its copy while the owner had built the GIVE: a duplicate), and a session peer
   dropped from the world while the session link stayed up was sent nothing. */
inline AddrPlan ItemTargetRoad(int toSlot, int sessionPeerSlot, bool sessionUp, int itemRoad, int peerInWorld, bool liveReady, int targetInWorld)
{
    AddrPlan r;
    const bool unknown = (toSlot == kAddrOwnerUnknown);
    const bool rawPeer = (toSlot == kAddrOwnerSessionPeer);
    if (!unknown && !rawPeer && (toSlot < 0 || (unsigned int)toSlot > kLiveSlotMax)) { r.why = kAddrWhyNoOwner; return r; }
    const bool isPeer = unknown || rawPeer || (sessionPeerSlot >= 0 && toSlot == sessionPeerSlot);
    if (isPeer && sessionUp && (rawPeer || itemRoad == kItemRoadSession || peerInWorld == 0)) { r.road = kAddrSession; return r; }   /* (a) */
    if (unknown) { r.why = kAddrWhyNoOwner; return r; }
    if (rawPeer || !liveReady) { r.why = kAddrWhyNoSlot; return r; }
    if (!AddrLiveSendGoes(targetInWorld)) { r.why = kAddrWhyNotInWorld; return r; }
    r.road = kAddrLive; r.slot = (unsigned int)toSlot;   /* (b) */
    return r;
}
/* THE ROAD OF AN ITEM MESSAGE FOR EVERY GAME BUT ONE - the area holder's ground word (GROUND GONE / ADD) to the other games after it
   served one game's TAKE or PUT; that game already did the same on its own ground and never gets it. exceptKey: that game's sender
   key (a relayed sender's RelayPeerId, else the raw session peer). itemRoad kItemRoadLive (with the world-server link ready):
   WORLD_EXCEPT that game's slot - a raw key names the session peer, whose slot is linkPeerSlot; a slot not known is not sent
   (kExceptWhyNoSlot: WORLD would reach that game too). kItemRoadSession: the session peer, unless it is that game
   (kExceptWhyOnlyThem - the session link reaches nobody else); a relayed requester while the session peer's slot is not known
   is not sent (kExceptWhyNoSlot: the session peer may be that game). Otherwise not sent (kExceptWhyNoRoad).
   kExceptWhyRoadChanged: ExceptReplayWhy's - a stored plan's road is not up now. */
enum { kExceptWhyOk = 0, kExceptWhyOnlyThem = 1, kExceptWhyNoSlot = 2, kExceptWhyNoRoad = 3, kExceptWhyRoadChanged = 4 };
struct ExceptPlan
{
    int road;                      /* kAddrNone / kAddrSession / kAddrLive */
    unsigned int route, target;    /* kAddrLive: kRouteWorldExcept and the excepted slot */
    int why;
    ExceptPlan() : road(kAddrNone), route(0), target(0), why(kExceptWhyNoRoad) {}
};
inline ExceptPlan ItemExceptRoad(unsigned int exceptKey, int linkPeerSlot, int itemRoad, bool liveReady)
{
    ExceptPlan r;
    const bool relayed = IsRelayPeer(exceptKey);
    const int slot = relayed ? (int)RelayPeerSlot(exceptKey) : linkPeerSlot;
    if (itemRoad == kItemRoadSession)
    {
        if (!relayed) { r.why = kExceptWhyOnlyThem; return r; }
        if (linkPeerSlot < 0) { r.why = kExceptWhyNoSlot; return r; }
        if (slot == linkPeerSlot) { r.why = kExceptWhyOnlyThem; return r; }
        r.road = kAddrSession; r.why = kExceptWhyOk;
        return r;
    }
    if (itemRoad != kItemRoadLive || !liveReady) { r.why = kExceptWhyNoRoad; return r; }
    if (slot < 0 || (unsigned int)slot > kLiveSlotMax) { r.why = kExceptWhyNoSlot; return r; }
    r.road = kAddrLive; r.route = (unsigned int)kRouteWorldExcept; r.target = (unsigned int)slot; r.why = kExceptWhyOk;
    return r;
}
/* 1 = the other games were really told: the message went out on a road (kExceptWhyOnlyThem sends nothing - nobody was told). */
inline int ExceptToldBy(const ExceptPlan& e, bool sent) { return (sent && e.road != kAddrNone) ? 1 : 0; }
/* A message that must follow an earlier one on the SAME road (an undone TAKE's ADD after its GONE): kExceptWhyOk while the stored
   plan's road is up now, kExceptWhyRoadChanged when it is not, kExceptWhyNoRoad for a plan that never had one. */
inline int ExceptReplayWhy(int storedRoad, bool liveReadyNow, bool sessionUpNow)
{
    if (storedRoad == kAddrLive) return liveReadyNow ? kExceptWhyOk : kExceptWhyRoadChanged;
    if (storedRoad == kAddrSession) return sessionUpNow ? kExceptWhyOk : kExceptWhyRoadChanged;
    return kExceptWhyNoRoad;
}
/* M7b SLICE 4 FOLD 2 (re-check 2026-10-02 D1): WHOM a BUILD meant for one player goes to (net/session.cpp SendBuildToSlot - a
   HELP_WORK to the piece's owner, a HAND_ACK to the PLACE's sender). recorded: the slot on record (a slot; kAddrOwnerSessionPeer =
   the session peer before its slot was known; any other value < 0 = not known); linkPeerSlot: the session peer's slot now (-1
   unknown); targetInWorld: coop::StoreRosterSlotInWorld of `recorded` (1 in, 0 out, -1 stale or unknown).
   By SLOT (fold 1) while the notebook road is up and the slot is known - unless the session link is up, the session peer's slot is
   not known here and that slot is out of the world roster (it may be the session peer, re-linked); otherwise the RAW SESSION PEER
   while the session link is up (the send before fold 1: with two games, or no notebook, the session peer is the one other game -
   decision 47); otherwise the known slot (ItemTargetRoad: noSlot) or nobody (kAddrOwnerNone: the caller holds it).
   FAILS BEFORE (fold 1 F6): a slot < 0 was never sent and a known slot with the notebook down was noSlot - with two games and no
   notebook every HELP_WORK was held forever and no HAND_ACK was ever sent (the placer re-sent its PLACE every roster round). */
inline int BuildAddrTarget(int recorded, int linkPeerSlot, bool liveReady, bool sessionUp, int targetInWorld)
{
    const bool known = recorded >= 0 && (unsigned int)recorded <= kLiveSlotMax;
    if (known && liveReady && (!sessionUp || linkPeerSlot >= 0 || targetInWorld != 0)) return recorded;
    if (sessionUp) return kAddrOwnerSessionPeer;
    return known ? recorded : kAddrOwnerNone;
}
/* M7b slice 4 fold 2 (D2): does an AREA / WORLD side message read as SENT (net/session.cpp SideSendSpread)? With the session link in
   the spread its send decides - otherwise the session peer missed it and the caller retries (a third game on the notebook may then
   hear it twice); without it, the notebook's send. FAILS BEFORE (fold 1): either road taking it read as sent - with two games and the
   peer not proven on the notebook, a failed session send plus a WORLD_EXCEPT that reached nobody was never retried. */
inline bool SideSpreadSent(bool session, bool sessionOk, bool live, bool liveOk)
{
    if (session) return sessionOk;
    return live && liveOk;
}
/* M7b slice 4 fold 2 (D4): may a BOUNTY message that was not sent be DROPPED (counted) instead of waiting at the head of the queue
   (crime.cpp)? Only with a road open (net::SideRoadOpen) and a lasting reason: a LIST (it goes again within kBountyResendMs), or an
   addition / clear (an event, never re-sent) whose owner's game is out of the world roster, or whose owner's slot is unknown with no
   session link (why noSlot, toSlot kAddrOwnerSessionPeer). No road at all, no road to that owner yet (a known slot, noSlot) or a send
   that failed: kept, in order. FAILS BEFORE (fold 1 F7): any failure with a road open dropped it - an addition lost to a send that
   failed or to the notebook being down while its owner is a third game. */
inline bool BountyUnsentDrops(bool isList, bool roadOpen, int why, int toSlot)
{
    if (!roadOpen) return false;
    if (isList) return true;
    if (why == kAddrWhyNotInWorld) return true;
    return why == kAddrWhyNoSlot && toSlot == kAddrOwnerSessionPeer;
}
/* ItemAskedKeyMatches: is `sender` the game a request was asked of under `asked` (0 / a raw key = the session peer on the session
   link, resolved to its slot NOW, at the answer - fold 1; its slot still unknown -> it cannot be told apart and is taken). */
inline bool ItemAskedKeyMatches(unsigned int asked, unsigned int sender, int linkPeerSlot)
{
    if (!IsRelayPeer(asked))
    {
        if (!IsRelayPeer(sender)) return true;
        if (linkPeerSlot < 0 || (unsigned int)linkPeerSlot > kLiveSlotMax) return true;
        return sender == RelayPeerId((unsigned int)linkPeerSlot);
    }
    return SamePlayer(asked, sender, linkPeerSlot);
}
/* ItemAnswerFromAsked: an ITEM_CONFIRM is taken only from a game its request was asked of. No ask on record (had false) -> taken:
   the late / foreign handling judges it. */
inline bool ItemAnswerFromAsked(bool had, unsigned int askedKey, unsigned int sender, int linkPeerSlot)
{
    return !had || ItemAskedKeyMatches(askedKey, sender, linkPeerSlot);
}
/* FOLD 1 - EVERY GAME ONE REQUEST ID WAS ASKED OF (a ground PUT keeps its first id across re-sends). An answer is taken from any of
   them; a RE-SEND goes to the `sticky` one (the game asked) until it REFUSES explicitly (ItemAskedRelease) - only then to the current
   writer - so a PUT one holder served is never served by a second; follow-ups (PLACED / REVOKE / the shop tail) go to the game whose
   answer was taken. Bounded: kItemAskedKeys, the oldest dropped. */
const int kItemAskedKeys = 4;
struct ItemAskedSet
{
    unsigned int key[kItemAskedKeys];
    int n, sticky, answered;   /* sticky / answered: an index, -1 none */
    ItemAskedSet() : n(0), sticky(-1), answered(-1) { for (int i = 0; i < kItemAskedKeys; ++i) key[i] = 0; }
};
inline int ItemAskedIndexOf(const ItemAskedSet& a, unsigned int sender, int linkPeerSlot)
{
    for (int i = 0; i < a.n; ++i) if (ItemAskedKeyMatches(a.key[i], sender, linkPeerSlot)) return i;
    return -1;
}
inline void ItemAskedAdd(ItemAskedSet* a, unsigned int key, int linkPeerSlot)
{
    const int i = ItemAskedIndexOf(*a, key, linkPeerSlot);
    if (i >= 0) { a->sticky = i; return; }
    if (a->n >= kItemAskedKeys)
    {
        for (int k = 1; k < a->n; ++k) a->key[k - 1] = a->key[k];
        --a->n;
        a->sticky = (a->sticky > 0) ? a->sticky - 1 : -1;
        a->answered = (a->answered > 0) ? a->answered - 1 : -1;
    }
    a->key[a->n] = key;
    a->sticky = a->n;
    ++a->n;
}
inline bool ItemAskedAccept(ItemAskedSet* a, unsigned int sender, int linkPeerSlot)
{
    const int i = ItemAskedIndexOf(*a, sender, linkPeerSlot);
    if (i < 0) return false;
    a->answered = i;
    return true;
}
inline bool ItemAskedRelease(ItemAskedSet* a, unsigned int sender, int linkPeerSlot)
{
    if (a->sticky < 0 || !ItemAskedKeyMatches(a->key[a->sticky], sender, linkPeerSlot)) return false;
    a->sticky = -1;
    return true;
}
inline bool ItemAskedSticky(const ItemAskedSet& a, unsigned int* key)
{
    if (a.sticky < 0) return false;
    *key = a.key[a.sticky];
    return true;
}
inline bool ItemAskedFollowUp(const ItemAskedSet& a, unsigned int* key)
{
    const int i = (a.answered >= 0) ? a.answered : ((a.sticky >= 0) ? a.sticky : a.n - 1);
    if (i < 0) return false;
    *key = a.key[i];
    return true;
}
/* ItemMoveSenderOk: an ITEM_MOVE is applied only from a game that may write what it names - a character's OWNER; a box's or the
   ground's WRITER: the area holder (holderSlot, never this game) or a player-owned box's owner (boxOwnerSlot). Nobody to judge by
   (no fresh holder and no player owner, or the session peer before its slot is known: senderSlot < 0) -> taken, as before. */
inline bool ItemMoveSenderOk(bool isChar, bool senderOwnsChar, int senderSlot, int holderSlot, int boxOwnerSlot)
{
    if (isChar) return senderOwnsChar;
    if (senderSlot < 0) return true;
    if (holderSlot < 0 && boxOwnerSlot < 0) return true;
    return senderSlot == holderSlot || senderSlot == boxOwnerSlot;
}

/* M7b SLICE 4 (C3) + DOOR_STATE (C4) - T-197; game-to-game protocol 121. THE SIDE MESSAGES ON THE CHARACTER STREAM'S ROAD.
 * Before: SAY, STATS, CRIME, BOUNTY, TALK, BUILD (and the farm's kinds on it) and DOOR_STATE went only g_transport->Send(0, ...) - the
 * one linked game. Now each send takes ONE road: the character stream's (CharStreamRoad - the notebook while this game's notebook link
 * is welcomed and the session peer is known reachable there, the session link only while it is not; an addressed send picks per
 * TARGET, ItemTargetRoad). On the notebook the ROUTE is SideRouteOf:
 *   SAY, STATS                         AREA  - about the sender's own character: every game covering its sector holds the copy (a
 *                                              bubble is momentary; STATS is re-sent every ~30 s, so a game arriving later is repaired);
 *   CRIME, a BOUNTY list               WORLD - manager 2026-10-02: crime and bounty STATE set or cleared while a game is elsewhere
 *                                              is never re-sent, so every game hears it (rare events - the cost is small);
 *   a BOUNTY addition / clear          SLOT  - to the character's OWNER (only the owner applies one - crime.cpp ApplyRemoteBounty);
 *   TALK                               SLOT  - to the other side of the conversation (TalkToSlot);
 *   BUILD (and the farm's kinds)       WORLD - every game, as the session link reached its one peer; the receivers keep their owner /
 *                                              writer rules (build.cpp, farm.cpp);
 *   DOOR_STATE, the holder's answer    AREA  - the door's sector; an ACTOR REPORT: SLOT to the door's area holder, AREA while none is
 *                                              known (only a game that holds the door adopts a report - doors.cpp DoorsTick).
 *   NAME, SLAVE                        WORLD - a copy's state, sent on a change only, so every game holding a copy hears it; after a
 *                                              SPAWN it goes on that SPAWN's own road and route, right behind it;
 *   HIRE                               SLOT  - a REQ to the person's OWNER, an OK / NO / DONE back to the asker; sub 1 (a DONE with
 *                                              no asker: this game's own hire) WORLD, so every game re-files its copy.
 * sub: BOUNTY - the kind (0 = coopbounty::kBountyKindList); DOOR_STATE - the origin (1 = coopdoor::kDoorOriginActor). haveSlot: the
 * door's area holder is known. Counted in addr.<group> (AddrGroupOf) by the road taken. */
enum { kSideRouteNone = -1, kSideRouteArea = 0, kSideRouteSlot = 1, kSideRouteWorld = 2 };
inline int SideRouteOf(unsigned int innerType, int sub, bool haveSlot)
{
    switch (innerType)
    {
    case kInnerSay: case kInnerStats: return kSideRouteArea;
    case kInnerCrime: return kSideRouteWorld;   /* manager 2026-10-02: state, never re-sent - every game */
    case kInnerBounty: return (sub == 0) ? kSideRouteWorld : kSideRouteSlot;
    case kInnerTalk: return kSideRouteSlot;
    case kInnerBuild: return kSideRouteWorld;
    case kInnerDoorState: return (sub == 1 && haveSlot) ? kSideRouteSlot : kSideRouteArea;
    case kInnerName: case kInnerSlave: return kSideRouteWorld;   /* a copy's state, sent on a change only */
    case kInnerHire: return (sub == 1) ? kSideRouteWorld : kSideRouteSlot;
    default: return kSideRouteNone;
    }
}
/* TalkToSlot: the player a TALK goes to - the NPC's owner when the NPC is not this game's, else the target character's owner (each
   from the owner table: have* = a record exists, *Key = its player key); neither -> kAddrOwnerUnknown (only the session link's
   fallback - ItemTargetRoad). */
inline int TalkToSlot(bool npcMine, bool haveNpcOwner, unsigned int npcKey, bool targetMine, bool haveTargetOwner, unsigned int targetKey,
                      int linkPeerSlot)
{
    if (!npcMine && haveNpcOwner) return AddrOwnerSlotOf(npcKey, linkPeerSlot);
    if (!targetMine && haveTargetOwner) return AddrOwnerSlotOf(targetKey, linkPeerSlot);
    return kAddrOwnerUnknown;
}

} // namespace cooplive

#endif
