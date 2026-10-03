/* src/common/liveenvelope.h - THE LIVE ENVELOPE (M5a, T-197 piece 4; store protocol 59).
 *
 * A live game-to-game message can travel THROUGH THE NOTEBOOK (SharedWastelandsServer.exe) to any number of games, not only down the
 * two-game session link. The game sends LIVE (53) up with a ROUTE; the notebook STAMPS THE SENDER - the PERMANENT slot it
 * gave that player at HELLO (B13, slots.txt; manager decision 2026-09-30: M5b keys ownership by player number, so the stamp
 * is the player's number and not the connection's uid seat), never a field the sender wrote and never read out of a uid
 * (owner 203/205 A: "the id is only a NAME") - and sends LIVE down to every destination.
 *
 * UP   (game -> notebook), a 12-byte header, then the inner message's own payload:
 *        u8 route | u8 inner type | u8 0 | u8 0 | u32 target | u32 inner length | inner bytes
 *      route kRouteWorld (1): every OTHER admitted game; target 0.
 *            kRouteSlot  (2): the one admitted game whose slot is `target` (never the sender itself).
 *            kRouteArea  (3): M6 (store protocol 60) - target = a SECTOR (src/common/liverelay.h AreaTarget); every
 *                             other admitted game with that sector in its delivery area (loaded + 1 ring), owner
 *                             decision 57. LiveRouteDecide below does not decide it (badRoute); the notebook asks
 *                             cooplive::LiveAreaRouteDecide.
 *            kRouteWorldExcept (4, fold 1): every other admitted game EXCEPT the one holding slot `target` - the
 *                             sender's session peer, served on the session link until it is known reachable
 *                             here (one delivery per destination). WORLD's target must be 0 (else badLen).
 * DOWN (notebook -> game), an 8-byte header:
 *        u16 origin slot | u8 inner type | u8 0 | u32 inner length | inner bytes
 *
 * The inner length word must equal what the frame carries and is at most kLiveInnerMax; the two pad bytes are 0; anything
 * else is badLen. The slot fits a u16: coopstore::kSlotLifetimeMax is 65520 (areaclaim.h).
 *
 * On the receiving game a relayed message is dispatched with the sender id RelayPeerId(slot) = 0x80000000 | slot. No
 * transport peer id has that bit (they count up from 0), so it is never mistaken for a transport id.
 * M5b (T-197 piece 5): every ownership check keys the owner by the PLAYER (PlayerKeyOf below - the slot, never a uid), so a
 * relayed message passes exactly when the owner record names its stamped slot, whichever road the record came by. Only
 * the inner types in LiveInnerAccepted are dispatched at all (see there).
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_LIVEENVELOPE_H
#define COOP_COMMON_LIVEENVELOPE_H

#include <cstddef>
#include <cstring>
#include <vector>

namespace cooplive {

enum { kRouteWorld = 1, kRouteSlot = 2, kRouteArea = 3, kRouteWorldExcept = 4 };
/* The notebook's verdict for one LIVE up - exactly one per message, each a counter on its REPORT line. */
enum { kLiveFwd = 0, kLiveNoSlot = 1, kLiveBadRoute = 2, kLiveBadLen = 3, kLiveNoTarget = 4 };

const size_t kLiveUpHeader = 12;
const size_t kLiveDownHeader = 8;
const unsigned int kLiveInnerMax = 65536;          /* the largest inner payload either way; a RELATION is ~100 bytes */
const unsigned int kLiveSlotMax = 65535;           /* a u16 - coopstore::kSlotLifetimeMax (65520) fits */
const unsigned int kRelayPeerBase = 0x80000000u;   /* the relayed sender marker - never a transport peer id */
const unsigned int kInnerRelation = 32, kInnerRelSync = 33;   /* net::MSG_RELATION / MSG_RELSYNC (transport.h:72-74) */
/* M5b: the owner-checked inner types a game also takes through the notebook (net/transport.h numbers; store.cpp asserts each) */
const unsigned int kInnerTask = 11, kInnerMove = 12, kInnerHit = 13, kInnerState = 14, kInnerDespawn = 18, kInnerUnload = 27,
                   kInnerSay = 43, kInnerStats = 44, kInnerCrime = 45, kInnerCarryBreak = 47, kInnerShot = 63;
/* T-327 (protocol 111): MSG_EFFECT - a request about the receiver's own character (its handler checks the owner itself) or the
   answer to one; the answer goes back on the road the request came by (effectwire.h EffectAnswerRoad) */
const unsigned int kInnerEffect = 65;
/* T-354 (protocol 130; 66 / 115 on its branch, renumbered at the merge of main 22ca5c91): MSG_NOT_SHOWN - the copy's game tells the owner its character is not shown there; answers nobody, and its
   handler counts it as ours only for a uid this game runs */
const unsigned int kInnerNotShown = 73;
/* M7a: the rest of a character's stream (net/transport.h numbers; store.cpp asserts each) */
const unsigned int kInnerSpawn = 10, kInnerAppearance = 15, kInnerClothing = 16, kInnerCombatMode = 17, kInnerSwing = 19,
                   kInnerIntent = 21, kInnerContext = 22;
/* P25 fold 2 (protocol 115): MSG_INSIDE - the owner's word on which building its own character is in (insidewire.h; net/transport.h
   number; store.cpp asserts it). Rides the character stream's road (AREA) and passes its relayed-owner gate. */
const unsigned int kInnerInside = 66;
/* M7a A1 build 2 (protocol 125) [a1b2-le0]: RELEASE (68) / RELEASE_ACK (69) - src/common/liveowner.h, addressed by LIVE SLOT
   (WorldFirstRoute). 67 (the retired MSG_HANDBACK) is never reused and is not accepted. net/transport.h numbers; store.cpp asserts them. */
const unsigned int kInnerRelease = 68, kInnerReleaseAck = 69;
/* M7a A1 build 1 (game-to-game protocol 118) [a1b1-le0]: ROSTER (70) and RECEIPT (71) - src/common/liveowner.h; and the live hand-over's
   XFER (25) / XFER_ACK (26), addressed by LIVE SLOT (liveowner.h WorldFirstRoute), and SQUAD_LEAD (57), route AREA with the squad's
   sector, now ride the world server too (manager decision, design 1.2). net/transport.h numbers; store.cpp asserts each. */
const unsigned int kInnerRoster = 70, kInnerReceipt = 71, kInnerXfer = 25, kInnerXferAck = 26, kInnerSquadLead = 57;
/* M7b slice 1: the copy-effect requests and their answers, addressed to one player by LIVE SLOT (ownerroute.h; net/transport.h
   numbers; store.cpp asserts each) */
const unsigned int kInnerPrison = 48, kInnerTreat = 49, kInnerCapture = 60, kInnerCaptureDone = 61, kInnerCapturePlaced = 62;
/* M7b slice 2: the item road (ownerroute.h ItemAddrDecide / ItemAddrRoute) */
const unsigned int kInnerItemMove = 37, kInnerItemRequest = 38, kInnerItemConfirm = 39, kInnerItemPlaced = 40, kInnerItemRevoke = 41,
                   kInnerParityReq = 53, kInnerParityBox = 54;
/* M7b slice 4 (C3) + DOOR_STATE (C4) (protocol 121): the side messages on the character stream's road (ownerroute.h SideRouteOf;
   net/transport.h numbers; store.cpp asserts each) */
const unsigned int kInnerDoorState = 42, kInnerBounty = 46, kInnerBuild = 50, kInnerTalk = 59;
/* NAME, SLAVE and HIRE on the character stream's road (ownerroute.h SideRouteOf; net/transport.h numbers; store.cpp asserts each) */
const unsigned int kInnerName = 51, kInnerSlave = 52, kInnerHire = 55;
/* MOVESTOP: the owner's stated stop for one of its player-faction characters, on the character stream's road (AREA) like MOVE;
   its handler takes it only from the uid's owner (RemoteMayWrite, MOVE's test). net/transport.h number; store.cpp asserts it. */
const unsigned int kInnerMoveStop = 72;

struct LiveUp
{
    unsigned int route, innerType, target;
    size_t innerAt, innerLen;   /* the inner payload is p[innerAt .. innerAt + innerLen) of the decoded frame */
    LiveUp() : route(0), innerType(0), target(0), innerAt(0), innerLen(0) {}
};
struct LiveDown
{
    unsigned int originSlot, innerType;
    size_t innerAt, innerLen;
    LiveDown() : originSlot(0), innerType(0), innerAt(0), innerLen(0) {}
};

/* Appends one UP envelope. false (nothing appended) = a route this wire does not know, an inner type over a byte, or an
   inner payload over kLiveInnerMax. */
inline bool LiveUpEncode(std::vector<char>* b, unsigned int route, unsigned int target, unsigned int innerType, const char* inner, size_t n)
{
    if (b == 0 || route < (unsigned int)kRouteWorld || route > (unsigned int)kRouteWorldExcept || innerType > 255u
        || n > (size_t)kLiveInnerMax || (n != 0 && inner == 0)) return false;
    if ((route == (unsigned int)kRouteWorld && target != 0) || (route == (unsigned int)kRouteWorldExcept && target > kLiveSlotMax)) return false;   /* fold 1 */
    const size_t at = b->size();
    b->resize(at + kLiveUpHeader + n);
    char* p = &(*b)[at];
    const unsigned int len = (unsigned int)n;
    p[0] = (char)(unsigned char)route; p[1] = (char)(unsigned char)innerType; p[2] = 0; p[3] = 0;
    std::memcpy(p + 4, &target, 4);
    std::memcpy(p + 8, &len, 4);
    if (n != 0) std::memcpy(p + kLiveUpHeader, inner, n);
    return true;
}

/* kLiveFwd = decoded; kLiveBadLen = shorter than the header, a length word that disagrees with the frame or is over the bound,
   or a pad byte that is not 0; kLiveBadRoute = a route this wire does not know (out->route says which). */
inline int LiveUpDecode(const char* p, size_t n, LiveUp* out)
{
    if (p == 0 || out == 0 || n < kLiveUpHeader) return kLiveBadLen;
    unsigned int len = 0;
    std::memcpy(&len, p + 8, 4);
    if (len > kLiveInnerMax || (size_t)len != n - kLiveUpHeader || p[2] != 0 || p[3] != 0) return kLiveBadLen;
    out->route = (unsigned char)p[0];
    out->innerType = (unsigned char)p[1];
    std::memcpy(&out->target, p + 4, 4);
    out->innerAt = kLiveUpHeader;
    out->innerLen = len;
    if (out->route < (unsigned int)kRouteWorld || out->route > (unsigned int)kRouteWorldExcept) return kLiveBadRoute;
    /* fold 1: WORLD names nobody (target 0); WORLD_EXCEPT names a slot a u16 can hold */
    if ((out->route == (unsigned int)kRouteWorld && out->target != 0) || (out->route == (unsigned int)kRouteWorldExcept && out->target > kLiveSlotMax)) return kLiveBadLen;
    return kLiveFwd;
}

/* Appends one DOWN envelope. false = an origin slot outside 0..kLiveSlotMax, an inner type over a byte, or an inner payload
   over the bound. */
inline bool LiveDownEncode(std::vector<char>* b, int originSlot, unsigned int innerType, const char* inner, size_t n)
{
    if (b == 0 || originSlot < 0 || (unsigned int)originSlot > kLiveSlotMax || innerType > 255u
        || n > (size_t)kLiveInnerMax || (n != 0 && inner == 0)) return false;
    const size_t at = b->size();
    b->resize(at + kLiveDownHeader + n);
    char* p = &(*b)[at];
    const unsigned short slot = (unsigned short)originSlot;
    const unsigned int len = (unsigned int)n;
    std::memcpy(p, &slot, 2);
    p[2] = (char)(unsigned char)innerType; p[3] = 0;
    std::memcpy(p + 4, &len, 4);
    if (n != 0) std::memcpy(p + kLiveDownHeader, inner, n);
    return true;
}

inline bool LiveDownDecode(const char* p, size_t n, LiveDown* out)
{
    if (p == 0 || out == 0 || n < kLiveDownHeader) return false;
    unsigned int len = 0;
    std::memcpy(&len, p + 4, 4);
    if (len > kLiveInnerMax || (size_t)len != n - kLiveDownHeader || p[3] != 0) return false;
    unsigned short slot = 0;
    std::memcpy(&slot, p, 2);
    out->originSlot = slot;
    out->innerType = (unsigned char)p[2];
    out->innerAt = kLiveDownHeader;
    out->innerLen = len;
    return true;
}

/* THE ROUTE RULE. connected[i] = the slot of the i-th connection the notebook could send to (-1 = holds no slot - a lobby
   connection - and is never a destination); originSlot = the sender's stamped slot (-1 = none); dests receives INDICES into
   `connected`. WORLD: every entry with a slot other than the origin's (none is fine - a game alone in its world). SLOT: the
   one entry holding `target`, never the origin itself. AREA and anything else: badRoute. One pass, no table. */
inline int LiveRouteDecide(unsigned int route, unsigned int target, int originSlot, const std::vector<int>& connected, std::vector<size_t>* dests)
{
    if (dests != 0) dests->clear();
    if (originSlot < 0) return kLiveNoSlot;
    if (route == (unsigned int)kRouteWorld)
    {
        for (size_t i = 0; i < connected.size(); ++i)
            if (connected[i] >= 0 && connected[i] != originSlot && dests != 0) dests->push_back(i);
        return kLiveFwd;
    }
    if (route == (unsigned int)kRouteWorldExcept)   /* fold 1 */
    {
        for (size_t i = 0; i < connected.size(); ++i)
            if (connected[i] >= 0 && connected[i] != originSlot && connected[i] != (int)target && dests != 0) dests->push_back(i);
        return kLiveFwd;
    }
    if (route == (unsigned int)kRouteSlot)
    {
        if (target > kLiveSlotMax || (int)target == originSlot) return kLiveNoTarget;
        for (size_t i = 0; i < connected.size(); ++i)
            if (connected[i] == (int)target) { if (dests != 0) dests->push_back(i); return kLiveFwd; }
        return kLiveNoTarget;
    }
    return kLiveBadRoute;
}

/* FOLD 1 - THE ROAD(S) ONE STANDING TAKES (net/session.cpp RelSendOneRoad), so that every other player gets it EXACTLY
   ONCE. The notebook road (liveReady) reaches the games the notebook admitted; the session road reaches the session peer.
     answer == kAnswerSession: the session road only (the ask came by it).
     answer == kAnswerSlot:    SLOT to the asker only, on the notebook road (the ask came by it) - never WORLD, so a join
                               costs N answers, not N x N sends. Nothing when the notebook road is down.
     notebook road down:       the session road only (the pre-M5a way).
     no session link, or the session peer KNOWN reachable through the notebook: WORLD only (an ASK too).
     an ASK otherwise (fold 2): the session road to the peer AND WORLD marked relayProof - the session peer takes that
                               copy only as proof that it is reachable and answers the session copy; every other
                               game answers the WORLD copy.
     otherwise:                the session road to the peer AND WORLD_EXCEPT its slot; with its slot unknown, WORLD
                               (peerSlotUnknown - an admitted peer then gets two copies; the snapshot owed when the road
                               changes re-bases it). */
enum { kAnswerNone = 0, kAnswerSession = 1, kAnswerSlot = 2 };
struct RoadPlan
{
    bool session, live, peerSlotUnknown, relayProof;
    unsigned int route, target;
    RoadPlan() : session(false), live(false), peerSlotUnknown(false), relayProof(false), route(0), target(0) {}
};
inline RoadPlan LiveRoadDecide(int answer, unsigned int answerSlot, bool liveReady, bool sessionUp, bool isAsk, bool peerRelayOk, int peerSlot)
{
    RoadPlan r;
    if (answer == kAnswerSession) { r.session = sessionUp; return r; }
    if (answer == kAnswerSlot)
    {
        if (liveReady && answerSlot <= kLiveSlotMax) { r.live = true; r.route = kRouteSlot; r.target = answerSlot; }
        return r;
    }
    if (!liveReady) { r.session = sessionUp; return r; }
    r.live = true; r.route = kRouteWorld; r.target = 0;
    if (!sessionUp || peerRelayOk) return r;
    r.session = true;
    if (isAsk) { r.relayProof = true; return r; }   /* fold 2: the ask's WORLD copy - proof only, for the session peer */
    if (peerSlot >= 0 && (unsigned int)peerSlot <= kLiveSlotMax) { r.route = kRouteWorldExcept; r.target = (unsigned int)peerSlot; }
    else r.peerSlotUnknown = true;
    return r;
}

/* FOLD 2 - RELSYNC'S PAYLOAD: {u8 flags, u32 ask number}. The ask number counts this game's asks (one number for both
   copies of one ask); a 1-byte payload (fold 1 and older) reads as flags 0, number 0. */
enum { kAskRelayUp = 1,      /* the asker's notebook road is UP */
       kAskProofOnly = 2 };  /* the notebook copy of an ask that ALSO went on the session road: the asker's session peer does not
                                answer it - it takes it as proof it is reachable through the notebook - every other game does */
inline void RelSyncEncode(std::vector<char>* b, unsigned int flags, unsigned int seq)
{
    const size_t at = b->size();
    b->resize(at + 5);
    (*b)[at] = (char)(unsigned char)flags;
    std::memcpy(&(*b)[at + 1], &seq, 4);
}
inline void RelSyncDecode(const char* p, size_t n, unsigned int* flags, unsigned int* seq)
{
    *flags = 0; *seq = 0;
    if (p == 0 || n < 5) return;
    *flags = (unsigned char)p[0];
    std::memcpy(seq, p + 1, 4);
}

/* FOLD 2 - IS THE SESSION PEER REACHABLE THROUGH THE NOTEBOOK. Kept per (this game's welcomed notebook link, session link,
   the peer's slot); the caller starts a fresh book when any of the three changes. ok is set by anything that arrived
   THROUGH THE NOTEBOOK stamped with the peer's slot. A session-road ask whose sender says its notebook road is DOWN clears
   it and BARS it at that ask's number: while barred, a relayed standing sets nothing (it may have been sent before the
   drop and delivered late), and only a relayed ask NEWER than the barring one re-sets it. Each returns 1 when ok changed. */
struct PeerRelayBook
{
    bool ok, barred;
    unsigned int barSeq;
    PeerRelayBook() : ok(false), barred(false), barSeq(0) {}
};
inline int PeerBookRelayed(PeerRelayBook* b, bool isAsk, unsigned int seq)
{
    if (b->barred)
    {
        if (!isAsk || seq <= b->barSeq) return 0;
        b->barred = false;
    }
    if (b->ok) return 0;
    b->ok = true;
    return 1;
}
inline int PeerBookSessionAskRelayDown(PeerRelayBook* b, unsigned int seq)
{
    const int was = b->ok ? 1 : 0;
    b->ok = false;
    b->barred = true;
    if (seq > b->barSeq) b->barSeq = seq;
    return was;
}

/* M7a FOLD (review 2026-09-30 F1; T708, Confirmed) - THE PROOF RE-ASKED WHEN THE SESSION PEER ENTERS THE WORLD. The world server
   delivers a live message only to a game that is IN_WORLD (M11a S1, joinstage.h LiveDestAllowed), so the proof copy of an ask
   sent while the session peer was still at the title was dropped, nothing re-sent it, and that peer's relPeer ok stayed 0 for
   the rest of the link. Each game watches the session peer's row on this link's PLAYERS roster and owes a RELSYNC (its notebook
   copy is the proof, or with the peer already proven a plain ask) on every edge of that row INTO the world; the first sight of
   it in the world on a new key counts as an edge. Kept per (session link key, welcomed notebook link key, the peer's slot); a
   key with either link down or the slot unknown owes nothing. Returns 1 when an ask is owed. */
struct PeerWorldWatch
{
    long sess, live;
    int slot;
    bool wasIn;
    PeerWorldWatch() : sess(-1), live(-1), slot(-2), wasIn(false) {}
};
inline int PeerInWorldAskDue(PeerWorldWatch* w, long sessKey, long liveKey, int peerSlot, bool peerInWorld)
{
    if (sessKey != w->sess || liveKey != w->live || peerSlot != w->slot) { w->sess = sessKey; w->live = liveKey; w->slot = peerSlot; w->wasIn = false; }
    if (sessKey == 0 || liveKey == 0 || peerSlot < 0) { w->wasIn = false; return 0; }
    const bool edge = peerInWorld && !w->wasIn;
    w->wasIn = peerInWorld;
    return edge ? 1 : 0;
}

/* THE RELAYED SENDER. */
inline unsigned int RelayPeerId(unsigned int slot) { return kRelayPeerBase | (slot & 0xFFFFu); }
inline bool IsRelayPeer(unsigned int peer) { return (peer & kRelayPeerBase) != 0; }
inline unsigned int RelayPeerSlot(unsigned int peer) { return peer & 0xFFFFu; }

/* M5b (T-197 piece 5) - OWNERSHIP BY PLAYER NUMBER. Who owns a uid (net/session.cpp g_owner) and every sender id a game-to-game
   handler sees are PLAYER KEYS: RelayPeerId(slot) - the permanent slot the notebook gave that player - whenever the slot is
   known, so the owner's message matches whether it came on the session link (the session peer said its slot with PEER_SLOT)
   or through the notebook (stamped with the sender's slot). The raw transport id stays the key only while no slot is known
   (a session with no notebook, or before PEER_SLOT). Never read out of a uid (owner 203/205 A: the id is only a NAME). A
   link slot a u16 cannot hold is never folded into another slot's key (S2-67). */
inline unsigned int PlayerKeyOf(unsigned int sender, int linkPeerSlot)
{
    if (IsRelayPeer(sender)) return sender;
    if (linkPeerSlot >= 0 && (unsigned int)linkPeerSlot <= kLiveSlotMax) return RelayPeerId((unsigned int)linkPeerSlot);
    return sender;
}
inline bool SamePlayer(unsigned int recorded, unsigned int sender, int linkPeerSlot)
{
    return PlayerKeyOf(recorded, linkPeerSlot) == PlayerKeyOf(sender, linkPeerSlot);
}
/* P25 fold 1 M1 [p25f1-env]: is a RECORDED owner key (g_owner) the SESSION PEER's - the game a session-link send (peer 0: SendTalk)
   reaches? Once PEER_SLOT said the link's slot, the session peer is that slot (a raw record from this link folds to it); before it,
   a record from this link is the raw (non-relay) id and a relay id is always a third game's (it could only come through the notebook). */
inline bool IsSessionPeerKey(unsigned int recorded, int linkPeerSlot)
{
    if (linkPeerSlot >= 0 && (unsigned int)linkPeerSlot <= kLiveSlotMax) return SamePlayer(recorded, 0u, linkPeerSlot);
    return !IsRelayPeer(recorded);
}

/* THE INNER TYPES A GAME DISPATCHES FROM THE NOTEBOOK. M5a: RELATION and RELSYNC (their handlers read no owner). M5b: plus exactly
   the types whose only sender test is the slot-keyed owner check (RemoteMayWrite / UidOwnedByPeer) and which answer nobody:
   TASK, MOVE, HIT, STATE, DESPAWN, UNLOAD, SAY, STATS, CRIME, CARRY_BREAK, SHOT. M7a: plus the rest of a character's stream -
   SPAWN, APPEARANCE, CLOTHING, COMBATMODE, SWING, INTENT, CONTEXT (liverelay.h CharStreamIndex); the six of them whose handler
   has no owner test of its own pass net/session.cpp's relayed-owner gate first (liverelay.h RelayedStreamGate). Not yet: every
   type that answers its sender or reads the session peer - ITEM_*, TALK, BUILD, HIRE, XFER*, BOUNTY, DOOR_STATE, NAME, SLAVE,
   SQUAD_LEAD (M7b). M7b slice 1: plus PRISON, TREAT, CAPTURE, CAPTURE_DONE, CAPTURE_PLACED - sent to the target's owner (or back
   to the asker) by LIVE SLOT (ownerroute.h). M7b fold 1 - what their handlers test of the SENDER: PRISON REFUSED, that the
   sender owns the uid (spawn.cpp ApplyRemotePrison, UidOwnedByPeer); CAPTURE_PLACED, that it is the held request's asker;
   CAPTURE_DONE, that the request id is one this game sent and its sender the player it went to (items.cpp CapApplyDone). TREAT and PRISON IN / RELEASE / DEATH
   carry no actor (no medic, guard or killer uid), so their handlers (spawn.cpp ApplyRemoteTreat, ApplyRemotePrison; medical.cpp
   ApplyOwnerDeathRequest) test only that the TARGET is this game's - any game in the world can send one by slot. A sender test
   for them needs the actor on the wire (a protocol change). M7b slice 2 (protocol 115): plus the eight item messages - ITEM_MOVE,
   ITEM_REQUEST, ITEM_CONFIRM, ITEM_PLACED, ITEM_REVOKE, PARITY_REQ, PARITY_BOX - on one road (the character stream's): a request
   to its target's owner or its key's writer, an answer back to the asker (an ITEM_CONFIRM is taken only from the game asked -
   items.cpp ApplyItemConfirm), a move or a box push by AREA (an ITEM_MOVE only from the character's owner or the box's writer -
   items.cpp ApplyItemMove). M7b slice 4 (protocol 121): plus BOUNTY, TALK, BUILD and DOOR_STATE (C3 + C4) - what their handlers
   test of the SENDER: a BOUNTY list, that the sender owns the uid (an addition or a clear, only that the uid is this game's - crime.cpp
   ApplyRemoteBounty); TALK, that the sender owns the NPC / target it names and is the conversation's peer (speech.cpp, every TALK sender compared as one player - cooplive::SamePlayer, fold 1 F1 - player keys on
   both roads); BUILD and the farm's kinds, build.cpp's / farm.cpp's owner and writer rules - only HELP_WORK and HELP_GONE read the stamped
   sender's slot (P106); PLACE, REMOVE and the reconcile still read the one linked game (P106 S3);
   DOOR_STATE, newest-per-publisher and the holder rule (doors.cpp ApplyDoorState). NAME and SLAVE: applied only to a copy (spawn.cpp NameNoteRecv / SlaveNoteRecv refuse this game's own uid); HIRE: hire.cpp's owner
   check, and its OK / NO / DONE go back to the asker's key. */
inline bool LiveInnerAccepted(unsigned int t)
{
    return t == kInnerRelation || t == kInnerRelSync || t == kInnerTask || t == kInnerMove || t == kInnerHit || t == kInnerState
        || t == kInnerDespawn || t == kInnerUnload || t == kInnerSay || t == kInnerStats || t == kInnerCrime || t == kInnerCarryBreak
        || t == kInnerShot
        || t == kInnerEffect   /* T-327 */
        || t == kInnerNotShown   /* T-354 */
        || t == kInnerSpawn || t == kInnerAppearance || t == kInnerClothing || t == kInnerCombatMode || t == kInnerSwing
        || t == kInnerIntent || t == kInnerContext   /* M7a */
        || t == kInnerInside   /* P25 fold 2 */
        || t == kInnerRelease || t == kInnerReleaseAck   /* M7a A1 build 2 [a1b2-le1] */
        || t == kInnerRoster || t == kInnerReceipt || t == kInnerXfer || t == kInnerXferAck || t == kInnerSquadLead   /* M7a A1 build 1 [a1b1-le1] */
        || t == kInnerPrison || t == kInnerTreat || t == kInnerCapture || t == kInnerCaptureDone || t == kInnerCapturePlaced   /* M7b slice 1 */
        || t == kInnerItemMove || t == kInnerItemRequest || t == kInnerItemConfirm || t == kInnerItemPlaced || t == kInnerItemRevoke
        || t == kInnerParityReq || t == kInnerParityBox   /* M7b slice 2 */
        || t == kInnerBounty || t == kInnerTalk || t == kInnerBuild || t == kInnerDoorState   /* M7b slice 4 + DOOR_STATE */
        || t == kInnerName || t == kInnerSlave || t == kInnerHire
        || t == kInnerMoveStop;   /* MOVE's owner test */
}

/* M7b slice 2 fold 1: the relayed types that WAIT in the arrival queue while this game has no running world (store.cpp), as they
   do when they come by the session link - the item messages, addressed to this game (a lost answer is a duplicate or a loss).
   M7b slice 4: and TALK, BUILD and DOOR_STATE - a conversation step, a building message and a door's level each wait in the queue
   on the session link too (speech.cpp / build.cpp / doors.cpp apply them at their safe points). BOUNTY is dropped there, as here.
   Every other relayed type is still dropped there (a standing owes a RELSYNC; a character comes again by the catch-up). */
inline bool LiveInnerWaitsForWorld(unsigned int t)
{
    return t == kInnerItemMove || t == kInnerItemRequest || t == kInnerItemConfirm || t == kInnerItemPlaced || t == kInnerItemRevoke
        || t == kInnerParityReq || t == kInnerParityBox
        || t == kInnerTalk || t == kInnerBuild || t == kInnerDoorState   /* M7b slice 4 */
        || t == kInnerName || t == kInnerSlave || t == kInnerHire;   /* a name and a slave state are kept pending, a HIRE queued, as on the session link */
}

/* A repeating refusal line: the first 5, then every 100th. `count` is the refusal's own counter, already incremented. */
inline int LiveLogThis(long long count) { return (count >= 1 && (count <= 5 || (count % 100) == 0)) ? 1 : 0; }

}   /* namespace cooplive */

#endif
