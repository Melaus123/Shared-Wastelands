/* src/common/liverelay.h - PER-AREA DELIVERY AND THE CATCH-UP (M6, T-197 piece 6; store protocol 60).
 *
 * Owner decision 57 (2026-09-22): live traffic about a character goes only to the players whose game has that character's
 * area loaded - the ENGINE'S OWN loaded set, never a guessed distance - with (a) a MARGIN, so a character pacing on the edge
 * does not churn, and (b) a CATCH-UP: a player who comes to have an area loaded receives the full current state of every
 * character already standing there.
 *
 * THE SECTOR KEY is the notebook's own area key: y * 64 + x, x and y in 0..63 (store_main.cpp g_areas). -1 = no sector.
 *
 * THE DELIVERY SET of a game = the sectors it reported loaded in its latest AREAS (zones.cpp: the engine's loaded set, about
 * once a second) widened by kAreaMargin (1) ring, clipped at the map's edge (no wrap). A report older than the notebook's
 * grace (10 s) is no report: that game is a destination for nothing.
 *
 * LIVE route AREA (liveenvelope.h kRouteArea = 3): target = AreaTarget(sector, previous sector)
 *        bits 0..15  the sector the message is about (0..4095)
 *        bits 16..31 the sector its subject has just LEFT, + 1 (0 = none; 1..4096)
 *      goes to every OTHER admitted game whose delivery set holds the sector OR the previous sector - so a character is
 *      sent from one ring short of a game's loaded area to one ring past it, and the message that carries it across that
 *      edge reaches the game on BOTH sides (the "stop" is seen). A target naming no sector is badLen.
 *
 * CATCHUP (56, notebook -> game), a 12-byte header then the sectors:
 *        u8 kind | u8 0 | u16 slot | u32 ask number | u32 count | count x u16 sector key
 *      kind kCatchupAsk   (1): to an OWNER - slot = the game that newly has these sectors in its delivery area; the sectors
 *                              listed are only those this owner has LOADED. The owner answers that game directly with
 *                              LIVE SLOT: the state of what it owns there (M7a) and last CATCHUP_END.
 *      kind kCatchupAsked (2): to the REQUESTER - slot = how many owners were asked (0 = nothing to wait for); the sectors
 *                              are every sector newly in its delivery area.
 *
 * TWO INNER TYPES THAT ONLY THE NOTEBOOK ROAD CARRIES (they are not session messages and have no session number; the
 * receiving game's store layer reads them, they touch no engine state):
 *        kInnerLiveProbe  (200): TEST ONLY - {u32 probe number, u32 sector key}, sent AREA by the `liveprobe` command.
 *        kInnerCatchupEnd (201): {u32 ask number, u32 characters} - the owner's last word on one ask; M7a: the characters
 *                                 whose state it sent ahead of it (SPAWN, CONTEXT, APPEARANCE, CLOTHING, STATE, COMBATMODE).
 *                                 M7a2 (protocol 112) [m7a2-lr0]: + {u32 n, n x u16 the sectors THIS owner was asked about, u32 h,
 *                                 h x u32 its characters there held back by a hand-over in flight} (CatchupEndMsg).
 *        kInnerOwnerMoved (202): M7a2 - {u32 uid, u32 the new owner's slot}, route WORLD, from the OLD owner at a hand-over's
 *                                 release: a third game re-keys its owner record (OwnerMovedTaken).
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_LIVERELAY_H
#define COOP_COMMON_LIVERELAY_H

#include <cstddef>
#include <cstring>
#include <map>   /* fold 1: the rejoin window's stamps */
#include <set>
#include <vector>
#include "liveenvelope.h"

namespace cooplive {

const int kAreaGrid = 64;
const int kAreaKeyCount = kAreaGrid * kAreaGrid;   /* 4096 */
const int kAreaMargin = 1;                          /* owner decision 57 (a): one area-square beyond the loaded set */
const unsigned int kAreaTargetNone = 0xFFFFFFFFu;   /* what AreaTarget gives for a sector that does not exist - badLen */

inline int AreaKey(int x, int y) { return (x >= 0 && x < kAreaGrid && y >= 0 && y < kAreaGrid) ? y * kAreaGrid + x : -1; }
inline int AreaKeyX(int k) { return k % kAreaGrid; }
inline int AreaKeyY(int k) { return k / kAreaGrid; }

inline unsigned int AreaTarget(int key, int prevKey)
{
    if (key < 0 || key >= kAreaKeyCount || prevKey < -1 || prevKey >= kAreaKeyCount) return kAreaTargetNone;
    return (unsigned int)key | ((unsigned int)(prevKey + 1) << 16);
}
inline bool AreaTargetDecode(unsigned int t, int* key, int* prevKey)
{
    const unsigned int lo = t & 0xFFFFu, hi = t >> 16;
    if (lo >= (unsigned int)kAreaKeyCount || hi > (unsigned int)kAreaKeyCount) return false;
    if (key != 0) *key = (int)lo;
    if (prevKey != 0) *prevKey = (int)hi - 1;
    return true;
}

/* out = every sector within `margin` (Chebyshev) of a sector in `loaded`, inside the grid. */
inline void AreaDilate(const std::set<int>& loaded, int margin, std::set<int>* out)
{
    if (out == 0) return;
    out->clear();
    if (margin < 0) margin = 0;
    for (std::set<int>::const_iterator it = loaded.begin(); it != loaded.end(); ++it)
    {
        if (*it < 0 || *it >= kAreaKeyCount) continue;
        const int x = AreaKeyX(*it), y = AreaKeyY(*it);
        for (int dy = -margin; dy <= margin; ++dy)
            for (int dx = -margin; dx <= margin; ++dx)
            {
                const int k = AreaKey(x + dx, y + dy);
                if (k >= 0) out->insert(k);
            }
    }
}

/* out = the sectors in `after` that are not in `before`, ascending. */
inline void AreaNewlyIn(const std::set<int>& before, const std::set<int>& after, std::vector<int>* out)
{
    if (out == 0) return;
    out->clear();
    for (std::set<int>::const_iterator it = after.begin(); it != after.end(); ++it)
        if (before.count(*it) == 0) out->push_back(*it);
}

/* out = the keys that are also in `in`, in the keys' order. */
inline void AreaIntersect(const std::vector<int>& keys, const std::set<int>& in, std::vector<int>* out)
{
    if (out == 0) return;
    out->clear();
    for (size_t i = 0; i < keys.size(); ++i) if (in.count(keys[i]) != 0) out->push_back(keys[i]);
}

/* THE AREA ROUTE RULE. connected[i] = the slot of the i-th connection the notebook could send to (-1 = none, never a
   destination); deliveryOf[i] = that connection's delivery set, 0 = no fresh report (a game at the title, loading, or
   silent) - never a destination; originSlot = the sender's stamped slot. dests receives INDICES into `connected`: every
   entry other than the origin whose set holds the target's sector or its previous sector. An empty dests is not a refusal
   (nobody else has that area). No slot width: a slot is only compared, never used as a bit. */
inline int LiveAreaRouteDecide(unsigned int target, int originSlot, const std::vector<int>& connected,
                               const std::vector<const std::set<int>*>& deliveryOf, std::vector<size_t>* dests)
{
    if (dests != 0) dests->clear();
    if (originSlot < 0) return kLiveNoSlot;
    int key = -1, prev = -1;
    if (!AreaTargetDecode(target, &key, &prev)) return kLiveBadLen;
    for (size_t i = 0; i < connected.size(); ++i)
    {
        if (connected[i] < 0 || connected[i] == originSlot) continue;
        const std::set<int>* d = i < deliveryOf.size() ? deliveryOf[i] : 0;
        if (d == 0) continue;
        if (d->count(key) != 0 || (prev >= 0 && d->count(prev) != 0)) { if (dests != 0) dests->push_back(i); }
    }
    return kLiveFwd;
}

/* FOLD 1 (review of 49bac485, MED) - THE CATCH-UP'S REJOIN WINDOW. A game's loaded set can flicker at an edge from one
   report (about 1 s) to the next; comparing each report with the one before asked for a catch-up round every time a sector
   came back - once M7a sends full state, a burst a second. Each game keeps a STAMP per sector: the time of the last report
   that had it in the delivery set. A sector is NEWLY delivered only when it has no stamp or its stamp is older than
   kCatchupRejoinSec; a stamp that old answers the same as none, so it is pruned (the map never holds more than the sectors
   delivered in the last kCatchupRejoinSec). An EMPTY report (the game left its world - zones.cpp ZonesAreasLeave - or its
   loaded set is down after a teleport) clears every stamp: the game's copies went with that world, so whatever it has
   next is all new. A game silent past the world server's grace has only stale stamps, so it is caught up in full too. */
const double kCatchupRejoinSec = 5.0;
inline void AreaReportNewlyIn(std::map<int, double>* stamps, const std::set<int>& loaded, int margin, double now, double rejoinSec,
                              std::set<int>* delivery, std::vector<int>* fresh)
{
    if (stamps == 0 || delivery == 0 || fresh == 0) return;
    fresh->clear();
    if (loaded.empty()) stamps->clear();
    AreaDilate(loaded, margin, delivery);
    for (std::set<int>::const_iterator it = delivery->begin(); it != delivery->end(); ++it)
    {
        std::map<int, double>::const_iterator f = stamps->find(*it);
        if (f == stamps->end() || now - f->second > rejoinSec) fresh->push_back(*it);
    }
    for (std::set<int>::const_iterator it = delivery->begin(); it != delivery->end(); ++it) (*stamps)[*it] = now;
    for (std::map<int, double>::iterator m = stamps->begin(); m != stamps->end(); )
    {
        if (now - m->second > rejoinSec) stamps->erase(m++); else ++m;
    }
}

/* ---- CATCHUP (56) ---- */
enum { kCatchupAsk = 1, kCatchupAsked = 2 };
const size_t kCatchupHeader = 12;
const unsigned int kCatchupKeysMax = (unsigned int)kAreaKeyCount;
struct CatchupMsg
{
    unsigned int kind, slot, askNo;
    std::vector<int> keys;
    CatchupMsg() : kind(0), slot(0), askNo(0) {}
};
/* false (nothing appended) = an unknown kind, a slot past a u16, more than kCatchupKeysMax sectors, or a key off the grid. */
inline bool CatchupEncode(std::vector<char>* b, unsigned int kind, unsigned int slot, unsigned int askNo, const std::vector<int>& keys)
{
    if (b == 0 || (kind != (unsigned int)kCatchupAsk && kind != (unsigned int)kCatchupAsked) || slot > kLiveSlotMax || keys.size() > (size_t)kCatchupKeysMax) return false;
    for (size_t i = 0; i < keys.size(); ++i) if (keys[i] < 0 || keys[i] >= kAreaKeyCount) return false;
    const size_t at = b->size();
    b->resize(at + kCatchupHeader + keys.size() * 2);
    char* p = &(*b)[at];
    const unsigned short s = (unsigned short)slot;
    const unsigned int n = (unsigned int)keys.size();
    p[0] = (char)(unsigned char)kind; p[1] = 0;
    std::memcpy(p + 2, &s, 2);
    std::memcpy(p + 4, &askNo, 4);
    std::memcpy(p + 8, &n, 4);
    for (size_t i = 0; i < keys.size(); ++i) { const unsigned short k = (unsigned short)keys[i]; std::memcpy(p + kCatchupHeader + i * 2, &k, 2); }
    return true;
}
inline bool CatchupDecode(const char* p, size_t n, CatchupMsg* out)
{
    if (p == 0 || out == 0 || n < kCatchupHeader || p[1] != 0) return false;
    const unsigned int kind = (unsigned char)p[0];
    if (kind != (unsigned int)kCatchupAsk && kind != (unsigned int)kCatchupAsked) return false;
    unsigned int count = 0; std::memcpy(&count, p + 8, 4);
    if (count > kCatchupKeysMax || n != kCatchupHeader + (size_t)count * 2) return false;
    unsigned short s = 0; std::memcpy(&s, p + 2, 2);
    std::vector<int> keys; keys.reserve(count);
    for (unsigned int i = 0; i < count; ++i)
    {
        unsigned short k = 0; std::memcpy(&k, p + kCatchupHeader + (size_t)i * 2, 2);
        if (k >= (unsigned short)kAreaKeyCount) return false;
        keys.push_back((int)k);
    }
    out->kind = kind; out->slot = s; std::memcpy(&out->askNo, p + 4, 4); out->keys.swap(keys);
    return true;
}

/* ---- the notebook road's own inner types ---- */
const unsigned int kInnerLiveProbe = 200, kInnerCatchupEnd = 201;
/* M11a S3 (T-197; game-to-game protocol 110; manager decisions 4(a), 7(a), 2026-09-30) - THREE SESSION MESSAGES THE NOTEBOOK ROAD
   ALSO CARRIES, read by the receiving game's store layer like the two above (never queued, no engine state), under their session
   numbers (net/transport.h; store.cpp asserts each): PING (3) and PONG (4) {u32 seq} - a world-road game's keepalive and round trip
   to the operator, route SLOT both ways; SESSION_CLOSING (56, empty) - the operator's deliberate close, route WORLD, taken only
   from the roster's operator slot (joinstage.h ClosingLiveAccepted). */
const unsigned int kInnerPing = 3, kInnerPong = 4, kInnerHostClosing = 56;
const unsigned int kInnerOwnerMoved = 202;   /* M7a2 item 3 [m7a2-lr1]: {u32 uid, u32 new owner slot} - the OLD owner's word, route WORLD */
/* M7a2 fold 1 [m7a2f-lr0]: + u32 the PREVIOUS owner's slot (12 bytes, OwnerMovedEncode); the 8-byte form names its sender as previous.
   The NEW owner sends it too, the moment it takes a character over (item 2); an old owner whose world link is down owes it (item 1). */
/* T-461 (REPORT A BUG): a nearby player's current log for a bug report - LOG_ASK, route AREA (the asker's player sector), and
   LOG_PART, route SLOT back to the asker (src/common/bugreport.h). Read by the receiving game's store layer and handed to
   bugreport.cpp; they touch no engine state. */
const unsigned int kInnerLogAsk = 203, kInnerLogPart = 204;
inline bool LiveInnerRoadLocal(unsigned int innerType)
{
    return innerType == kInnerLiveProbe || innerType == kInnerCatchupEnd
        || innerType == kInnerPing || innerType == kInnerPong || innerType == kInnerHostClosing   /* M11a S3 */
        || innerType == kInnerOwnerMoved   /* M7a2 item 3 [m7a2-lr2] */
        || innerType == kInnerLogAsk || innerType == kInnerLogPart;   /* T-461 */
}

/* M7a (T-197 piece 7a; game-to-game protocol 108; owner decisions 54(a), 57) - THE CHARACTER STREAM ON THE NOTEBOOK ROAD.
   SPAWN, CONTEXT, APPEARANCE, CLOTHING, MOVE, TASK, INTENT, STATE, COMBATMODE, SWING, DESPAWN and UNLOAD take ONE road each:
   the notebook (LIVE) whenever this game's notebook link is up and welcomed, the session link ONLY while it is not. */
enum { kCharRoadNone = 0, kCharRoadLive = 1, kCharRoadSession = 2 };
/* M7a FOLD (review 2026-09-30 F1): the road is RELATION's rule (LiveRoadDecide) - the session link while the session peer is not
   PROVEN reachable through the notebook (peerRelayOk, net/session.cpp SessionPeerRelayOk), the notebook once it is or with no
   session link. The notebook alone had forgotten a peer whose own notebook link was down, and that peer froze every character
   of this game until it was admitted again and caught up. (Other games while the peer is unproven: leftover F5.) */
inline int CharStreamRoad(bool liveReady, bool sessionUp, bool peerRelayOk, int peerSlot)
{
    const RoadPlan p = LiveRoadDecide(kAnswerNone, 0, liveReady, sessionUp, false, peerRelayOk, peerSlot);
    if (p.session) return kCharRoadSession;
    return p.live ? (int)kCharRoadLive : (int)kCharRoadNone;
}
/* M7b slice 4 FOLD 1 (review 2026-10-02 F4) - THE ROADS ONE AREA / WORLD SIDE MESSAGE TAKES (SAY, STATS, CRIME, a BOUNTY list,
   BUILD, a door holder's answer - net/session.cpp SideSendSpread): RELATION's rule (LiveRoadDecide, a change), so every other game
   gets it EXACTLY ONCE. The session peer proven reachable through the notebook, or no session link: the notebook only, AREA
   (areaTarget) or WORLD (areaTarget kAreaTargetNone). Not proven: the session link to the session peer AND the notebook to every
   other game - WORLD_EXCEPT its slot; an AREA message is WIDENED to that (the world server has no "area except one slot"), so a
   third game still hears it. The peer's slot unknown: the session link only (slotUnknown) - a WORLD copy could reach the peer a
   second time. The notebook road down: the session link only. */
struct SidePlan
{
    bool session, live, slotUnknown, widened;
    unsigned int route, target;
    SidePlan() : session(false), live(false), slotUnknown(false), widened(false), route(0), target(0) {}
};
inline SidePlan SideRoadDecide(bool liveReady, bool sessionUp, bool peerRelayOk, int peerSlot, unsigned int areaTarget)
{
    SidePlan s;
    const RoadPlan r = LiveRoadDecide(kAnswerNone, 0, liveReady, sessionUp, false, peerRelayOk, peerSlot);
    if (r.peerSlotUnknown) { s.session = true; s.slotUnknown = true; return s; }
    s.session = r.session; s.live = r.live;
    if (!s.live) return s;
    s.route = r.route; s.target = r.target;
    if (r.route == (unsigned int)kRouteWorldExcept) { s.widened = (areaTarget != kAreaTargetNone); return s; }
    if (areaTarget != kAreaTargetNone) { s.route = kRouteArea; s.target = areaTarget; }
    return s;
}
const int kCharStreamTypes = 12;
/* The stream's own index of a message type (the REPORT's column order), -1 = not a character-stream type. */
inline int CharStreamIndex(unsigned int t)
{
    switch (t)
    {
    case kInnerSpawn: return 0;       case kInnerContext: return 1;     case kInnerAppearance: return 2;  case kInnerClothing: return 3;
    case kInnerMove: return 4;        case kInnerTask: return 5;        case kInnerIntent: return 6;      case kInnerState: return 7;
    case kInnerCombatMode: return 8;  case kInnerSwing: return 9;       case kInnerDespawn: return 10;    case kInnerUnload: return 11;
    default: return -1;
    }
}
inline const char* CharStreamName(int i)
{
    static const char* const k[kCharStreamTypes] = { "SPAWN", "CONTEXT", "APPEARANCE", "CLOTHING", "MOVE", "TASK", "INTENT", "STATE",
                                                     "COMBATMODE", "SWING", "DESPAWN", "UNLOAD" };
    return (i >= 0 && i < kCharStreamTypes) ? k[i] : "?";
}
/* The route on the notebook road. AREA (the character's sector, owner decision 57) for everything but the two WITHDRAWALS:
   DESPAWN and UNLOAD go WORLD, because a withdrawal is owed to every game that holds a copy, and a game that has walked the
   copy's area out of its delivery set still holds it (the notebook makes no UNLOAD of its own). */
inline unsigned int CharStreamRoute(unsigned int t)
{
    return (t == kInnerDespawn || t == kInnerUnload) ? (unsigned int)kRouteWorld : (unsigned int)kRouteArea;   /* M7a A1 build 2 [a1b2-lr0]: HANDBACK retired */
}
/* The AREA target of one send: the sector now and, when it differs, the sector this character was last sent in - so the message
   that carries it over a delivery edge reaches the games on both sides. No sector now = kAreaTargetNone (the caller goes WORLD). */
inline unsigned int CharAreaTarget(int keyNow, int keyLast)
{
    if (keyNow < 0 || keyNow >= kAreaKeyCount) return kAreaTargetNone;
    const int prev = (keyLast >= 0 && keyLast < kAreaKeyCount && keyLast != keyNow) ? keyLast : -1;
    return AreaTarget(keyNow, prev);
}
/* THE ANNOUNCE, PER CHARACTER - "announced to the notebook" (worldsync.cpp g_announced), not per receiver: which games hear it
   is the notebook's delivery set. anyOtherLoaded = some other game has the character's sector loaded (the notebook's AREAMAP);
   mineLoaded = this game still has it loaded (E9); areaGainedReporter = the notebook has just said a game NEWLY has that sector
   in its delivery area (CATCHUP ASK). An announced character is RE-SENT to that game (the B8 case: before M7a the one flag was
   1, another game still held the area, and nothing was ever sent to the newcomer). UNLOAD and SPAWN are the announce pass's
   rule unchanged. */
enum { kAnnKeep = 0, kAnnSpawn = 1, kAnnUnload = 2, kAnnResend = 3 };
inline int AnnounceDecide(bool anyOtherLoaded, bool mineLoaded, bool announcedToRelay, bool areaGainedReporter)
{
    if (announcedToRelay && (!anyOtherLoaded || !mineLoaded)) return kAnnUnload;
    if (announcedToRelay && areaGainedReporter) return kAnnResend;
    if (!announcedToRelay && anyOtherLoaded && mineLoaded) return kAnnSpawn;
    return kAnnKeep;
}
/* THE RELAYED-OWNER GATE for the six stream types whose session handler has no owner test of its own (it applies only to a
   copy this game holds - fine for the one session peer, not for any number of games): a relayed one is refused when this game
   runs the uid, or when the uid's recorded owner is another player. An unknown uid passes (CONTEXT arrives before SPAWN). */
inline bool CharStreamNeedsOwnerGate(unsigned int t)
{
    return t == kInnerAppearance || t == kInnerClothing || t == kInnerCombatMode || t == kInnerSwing || t == kInnerIntent || t == kInnerContext
        || t == kInnerInside;   /* P25 fold 2: its handler, like INTENT's, asks only for a puppet row */
}
enum { kStreamGatePass = 0, kStreamGateOwnedHere = 1, kStreamGateOtherOwner = 2 };
inline int RelayedStreamGate(bool ownedHere, bool ownerKnown, bool ownerIsSender)
{
    if (ownedHere) return kStreamGateOwnedHere;
    if (ownerKnown && !ownerIsSender) return kStreamGateOtherOwner;
    return kStreamGatePass;
}
/* M7a FOLD (review 2026-09-30 F4): SPAWN WRITES THE OWNER RECORD, so a relayed SPAWN is refused when the uid's recorded owner is
   another player - the record is the hand-over record (a hand-over re-writes it: ReleaseLocalOwner), so a SPAWN from the player a
   hand-over named passes. A first SPAWN (no record) passes; this game's own uid is OnSpawn's refusal (SpawnUidDecide). */
inline bool RelayedSpawnRefused(bool ownerKnown, bool ownerIsSender) { return ownerKnown && !ownerIsSender; }
/* M7a FOLD (review 2026-09-30 F2 b) - THE RECONNECT SWEEP. At the first catch-up this game asks after its notebook link came back
   under a running world, the copies it holds of other players' characters in the asked sectors are MARKED with their owner; any
   stream message from that owner for one of them (it re-sent it) takes the mark off; that owner's CATCHUP_END for this ask
   withdraws every copy of its still marked - a character that despawned or unloaded in the gap is not left a ghost. A newer sweep
   replaces an older one's marks; an END of another ask, or from another player, withdraws nothing. */
/* M7a2 (T-197 second half, part 1; protocol 112) [m7a2-lr3]: each mark keeps the SECTOR the copy stood in. An owner's END withdraws
   only its marked copies standing in a sector IT WAS ASKED ABOUT (item 1 - the notebook asks an owner only for the sectors it has
   loaded, so a copy elsewhere is not its to answer for) and not named HELD (item 2 - a hand-over of it is in flight); the rest of that
   owner's marks are dropped - kept, not withdrawn. OWNER_MOVED re-keys a mark to the new owner; a sweep older than
   kCatchupAskTimeoutSec drops its marks (item 7 - an owner that never answers withdraws nothing). */
struct CatchupSweepMark { unsigned int owner; int key; CatchupSweepMark() : owner(0), key(-1) {} };
struct CatchupSweep
{
    unsigned int askNo;
    double openedAt;
    std::map<unsigned int, CatchupSweepMark> marked;   /* uid -> its owner's player key and the sector it stood in */
    std::set<unsigned int> heard;   /* fold 2 [m7a2f2-lr0]: the players this game heard from during the sweep (a stream message, its END) */
    CatchupSweep() : askNo(0), openedAt(0.0) {}
};
inline void CatchupSweepOpen(CatchupSweep* s, unsigned int askNo, const std::vector<unsigned int>& uids, const std::vector<unsigned int>& owners,
                             const std::vector<int>& keys, double now)
{
    s->askNo = askNo; s->openedAt = now;
    s->heard.clear();   /* [m7a2f2-lr1] */
    s->marked.clear();
    for (size_t i = 0; i < uids.size() && i < owners.size() && i < keys.size(); ++i) { CatchupSweepMark m; m.owner = owners[i]; m.key = keys[i]; s->marked[uids[i]] = m; }
}
inline bool CatchupSweepSeen(CatchupSweep* s, unsigned int uid, unsigned int sender)
{
    if (s != 0) s->heard.insert(sender);   /* fold 2 [m7a2f2-lr2]: that player is heard */
    std::map<unsigned int, CatchupSweepMark>::iterator it = s->marked.find(uid);
    if (it == s->marked.end() || it->second.owner != sender) return false;
    s->marked.erase(it);
    return true;
}
inline bool CatchupSweepRekey(CatchupSweep* s, unsigned int uid, unsigned int from, unsigned int to)
{
    std::map<unsigned int, CatchupSweepMark>::iterator it = s->marked.find(uid);
    if (it == s->marked.end() || it->second.owner != from) return false;
    it->second.owner = to;
    return true;
}
/* M7a2 fold 1 [m7a2f-lr1]: item 5 - an owner whose END names ANY sector answers for all its marked copies: one standing in a sector it was
   not asked about is withdrawn too unless the END names it held (the 2-player clean-up restored; withdrawnNotAsked). Only an END naming no
   sector keeps them (keptNotAsked) - and lists them in notAsked, so a claim on one is taken (item 3). */
struct CatchupSweepEndResult { std::vector<unsigned int> withdraw, notAsked; long long keptNotAsked, keptHeld, withdrawnNotAsked; CatchupSweepEndResult() : keptNotAsked(0), keptHeld(0), withdrawnNotAsked(0) {} };
inline void CatchupSweepEnd(CatchupSweep* s, unsigned int askNo, unsigned int sender, const std::vector<int>& askedKeys,
                            const std::vector<unsigned int>& held, CatchupSweepEndResult* r)
{
    if (s == 0 || r == 0 || askNo != s->askNo) return;
    s->heard.insert(sender);   /* fold 2 [m7a2f2-lr3]: an owner that answered is heard */
    const std::set<int> asked(askedKeys.begin(), askedKeys.end());
    const std::set<unsigned int> h(held.begin(), held.end());
    for (std::map<unsigned int, CatchupSweepMark>::iterator it = s->marked.begin(); it != s->marked.end(); )
    {
        if (it->second.owner != sender) { ++it; continue; }
        if (h.count(it->first) != 0) ++r->keptHeld;
        else if (asked.empty()) { ++r->keptNotAsked; r->notAsked.push_back(it->first); }   /* fold 1 items 3, 5 [m7a2f-lr2] */
        else { if (asked.count(it->second.key) == 0) ++r->withdrawnNotAsked; r->withdraw.push_back(it->first); }
        s->marked.erase(it++);
    }
}
/* fold 1 items 3, 5 [m7a2f-lr3]: the marks still open at the timeout are handed back in *left - their claims are taken, the rest WITHDRAWN
   (manager decision: the owner that sent no END was not asked or lost its answer; its next SPAWN brings a live character back). */
inline size_t CatchupSweepExpire(CatchupSweep* s, double now, double timeoutSec, std::map<unsigned int, CatchupSweepMark>* left = 0)
{
    if (s == 0 || s->marked.empty() || now - s->openedAt <= timeoutSec) return 0;
    const size_t n = s->marked.size();
    if (left != 0) left->swap(s->marked);
    s->marked.clear();
    return n;
}
/* fold 2 [m7a2f2-lr4] (manager sign-off, narrows item 5): a mark handed back at the timeout is WITHDRAWN only when its owner was HEARD
   during the sweep - it spoke and did not re-send that character. A SILENT owner was most likely never asked (its area report stale, a
   world-road game not in the world, a failed ask send): it sends no SPAWN, so withdrawing its copy would leave it gone until the sector
   leaves and re-enters - the copy is KEPT, as before fold 1. */
inline bool SweepExpiryWithdraws(const CatchupSweep& s, unsigned int owner) { return s.heard.count(owner) != 0; }
/* item 3: a relayed SPAWN refused because a MARKED copy's recorded owner is another player is kept as a CLAIM (net/session.cpp) and
   taken if that owner's END then drops the copy - the owner no longer runs it: a hand-over this game missed while its link was down. */
inline bool SweepClaimKept(const CatchupSweep& s, unsigned int uid, unsigned int sender)
{
    std::map<unsigned int, CatchupSweepMark>::const_iterator it = s.marked.find(uid);
    return it != s.marked.end() && it->second.owner != sender;
}
/* A relayed MOVE is a LEVEL in the arrival queue, keyed by its uid (the session link's own rule, net/session.cpp): the next
   MOVE for the same character overwrites an undrained one in place. Every other relayed type stays an EDGE. */
inline bool RelayedIsLevel(unsigned int innerType, size_t innerLen) { return innerType == kInnerMove && innerLen >= 4; }
inline void LivePairEncode(std::vector<char>* b, unsigned int a, unsigned int c)
{
    const size_t at = b->size(); b->resize(at + 8); std::memcpy(&(*b)[at], &a, 4); std::memcpy(&(*b)[at + 4], &c, 4);
}
inline bool LivePairDecode(const char* p, size_t n, unsigned int* a, unsigned int* c)
{
    if (p == 0 || n != 8) return false;
    std::memcpy(a, p, 4); std::memcpy(c, p + 4, 4);
    return true;
}
/* PROBE {u32 probe number, u32 sector key}; CATCHUP_END {u32 ask number, u32 characters counted}. */
inline void LiveProbeEncode(std::vector<char>* b, unsigned int seq, int key) { LivePairEncode(b, seq, (unsigned int)key); }
inline bool LiveProbeDecode(const char* p, size_t n, unsigned int* seq, int* key)
{
    unsigned int k = 0;
    if (!LivePairDecode(p, n, seq, &k) || k >= (unsigned int)kAreaKeyCount) return false;
    *key = (int)k; return true;
}
/* M7a2 (protocol 112) [m7a2-lr4]: CATCHUP_END = {u32 ask number, u32 characters sent, u32 n, n x u16 sector, u32 h, h x u32 uid}. */
const unsigned int kCatchupEndHeldMax = 256;
struct CatchupEndMsg
{
    unsigned int askNo, count;
    std::vector<int> keys;            /* the sectors THIS owner was asked about (its ASK's list) */
    std::vector<unsigned int> held;   /* its characters there not re-sent because a hand-over of them is in flight */
    CatchupEndMsg() : askNo(0), count(0) {}
};
inline bool CatchupEndEncode(std::vector<char>* b, const CatchupEndMsg& m)
{
    if (b == 0 || m.keys.size() > (size_t)kCatchupKeysMax || m.held.size() > (size_t)kCatchupEndHeldMax) return false;
    for (size_t i = 0; i < m.keys.size(); ++i) if (m.keys[i] < 0 || m.keys[i] >= kAreaKeyCount) return false;
    const unsigned int nk = (unsigned int)m.keys.size(), nh = (unsigned int)m.held.size();
    const size_t at = b->size();
    b->resize(at + 16 + (size_t)nk * 2 + (size_t)nh * 4);
    char* p = &(*b)[at];
    std::memcpy(p, &m.askNo, 4); std::memcpy(p + 4, &m.count, 4); std::memcpy(p + 8, &nk, 4);
    size_t o = 12;
    for (size_t i = 0; i < m.keys.size(); ++i) { const unsigned short k = (unsigned short)m.keys[i]; std::memcpy(p + o, &k, 2); o += 2; }
    std::memcpy(p + o, &nh, 4); o += 4;
    for (size_t i = 0; i < m.held.size(); ++i) { std::memcpy(p + o, &m.held[i], 4); o += 4; }
    return true;
}
inline bool CatchupEndDecode(const char* p, size_t n, CatchupEndMsg* out)
{
    if (p == 0 || out == 0 || n < 16) return false;
    unsigned int a = 0, c = 0, nk = 0, nh = 0;
    std::memcpy(&a, p, 4); std::memcpy(&c, p + 4, 4); std::memcpy(&nk, p + 8, 4);
    if (nk > kCatchupKeysMax || n < 16 + (size_t)nk * 2) return false;
    std::memcpy(&nh, p + 12 + (size_t)nk * 2, 4);
    if (nh > kCatchupEndHeldMax || n != 16 + (size_t)nk * 2 + (size_t)nh * 4) return false;
    std::vector<int> keys; keys.reserve(nk);
    for (unsigned int i = 0; i < nk; ++i)
    {
        unsigned short k = 0; std::memcpy(&k, p + 12 + (size_t)i * 2, 2);
        if (k >= (unsigned short)kAreaKeyCount) return false;
        keys.push_back((int)k);
    }
    std::vector<unsigned int> held; held.reserve(nh);
    const size_t ho = 16 + (size_t)nk * 2;
    for (unsigned int i = 0; i < nh; ++i) { unsigned int u = 0; std::memcpy(&u, p + ho + (size_t)i * 4, 4); held.push_back(u); }
    out->askNo = a; out->count = c; out->keys.swap(keys); out->held.swap(held);
    return true;
}
/* items 2 and 5: ONE OF THIS GAME'S CHARACTERS IN A CATCH-UP (an ASK answered, or this game's own reverse catch-up) - HELD while a
   hand-over of it is in flight (named in the END, re-sent when the hand-over settles), else AnnounceDecide with the reporter GAINED. */
enum { kCatchupHeld = 4 };
inline int CatchupCharDecide(bool handoverPending, bool anyOtherLoaded, bool mineLoaded, bool announcedToRelay)
{
    if (handoverPending) return kCatchupHeld;
    return AnnounceDecide(anyOtherLoaded, mineLoaded, announcedToRelay, true);
}
/* item 7: THE ASK ENDS IN BOUNDED TIME. The requester books each ask the notebook made for it (CATCHUP ASKED: the owners asked); every
   owner's END counts it down; an ask not ended within kCatchupAskTimeoutSec is ended by the clock (counted, nothing withdrawn). */
const double kCatchupAskTimeoutSec = 20.0;
struct CatchupAskRow { unsigned int owners, ends; double at; CatchupAskRow() : owners(0), ends(0), at(0.0) {} };
struct CatchupAskBook { std::map<unsigned int, CatchupAskRow> rows; };
const size_t kCatchupAskBookMax = 64;
inline void CatchupAskOpen(CatchupAskBook* b, unsigned int askNo, unsigned int owners, double now)
{
    if (b == 0 || owners == 0) return;
    if (b->rows.size() >= kCatchupAskBookMax && b->rows.find(askNo) == b->rows.end()) b->rows.erase(b->rows.begin());
    CatchupAskRow r; r.owners = owners; r.at = now; b->rows[askNo] = r;
}
enum { kAskEndCounted = 0, kAskEndDone = 1, kAskEndUnknown = 2 };
inline int CatchupAskEnd(CatchupAskBook* b, unsigned int askNo)
{
    if (b == 0) return kAskEndUnknown;
    std::map<unsigned int, CatchupAskRow>::iterator it = b->rows.find(askNo);
    if (it == b->rows.end()) return kAskEndUnknown;   /* late (after the timeout) or a repeat */
    if (++it->second.ends >= it->second.owners) { b->rows.erase(it); return kAskEndDone; }
    return kAskEndCounted;
}
inline size_t CatchupAskExpire(CatchupAskBook* b, double now, double timeoutSec, std::vector<unsigned int>* expired)
{
    size_t n = 0;
    if (b == 0) return 0;
    for (std::map<unsigned int, CatchupAskRow>::iterator it = b->rows.begin(); it != b->rows.end(); )
    {
        if (now - it->second.at > timeoutSec) { if (expired != 0) expired->push_back(it->first); b->rows.erase(it++); ++n; }
        else ++it;
    }
    return n;
}
/* item 7: DEDUP - an owner answers one (requester slot, ask number) once; the last kCatchupDedupMax are remembered. */
const size_t kCatchupDedupMax = 64;
inline bool CatchupAnswerFirst(std::vector<unsigned long long>* recent, unsigned int slot, unsigned int askNo)
{
    if (recent == 0) return true;
    const unsigned long long k = ((unsigned long long)slot << 32) | (unsigned long long)askNo;
    for (size_t i = 0; i < recent->size(); ++i) if ((*recent)[i] == k) return false;
    if (recent->size() >= kCatchupDedupMax) recent->erase(recent->begin());
    recent->push_back(k);
    return true;
}
/* item 4: a WORLD-ROAD game answers an ASK only once it has sent JOIN_STAGE IN_WORLD for this link (the world server refuses its live
   messages until then, so an answer made earlier is lost); until then the ASK is HELD, and one held past the timeout is dropped. */
enum { kAskHoldAnswer = 0, kAskHoldHold = 1, kAskHoldDrop = 2 };
inline int CatchupAskHoldDecide(bool worldRoad, bool inWorldSent, double heldSec, double timeoutSec)
{
    if (heldSec > timeoutSec) return kAskHoldDrop;
    return (worldRoad && !inWorldSent) ? (int)kAskHoldHold : (int)kAskHoldAnswer;
}
/* item 3: OWNER_MOVED is taken only from the RECORDED owner, for a copy this game holds (not one it runs), naming another player. */
inline bool OwnerMovedTaken(bool ownedHere, bool ownerKnown, bool ownerIsSender, bool newIsSender)
{
    return !ownedHere && ownerKnown && ownerIsSender && !newIsSender;
}
/* ---- M7a2 FOLD 1 (review 2026-09-30) [m7a2f-lr4] ---------------------------------------------------------------------------------- */
/* item 2: OWNER_MOVED {u32 uid, u32 new owner slot, u32 previous owner slot}; the 8-byte form names its sender as the previous owner. */
inline void OwnerMovedEncode(std::vector<char>* b, unsigned int uid, unsigned int newSlot, unsigned int prevSlot)
{
    LivePairEncode(b, uid, newSlot);
    const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &prevSlot, 4);
}
inline bool OwnerMovedDecode(const char* p, size_t n, unsigned int senderSlot, unsigned int* uid, unsigned int* newSlot, unsigned int* prevSlot)
{
    if (p == 0 || uid == 0 || newSlot == 0 || prevSlot == 0 || (n != 8 && n != 12)) return false;
    std::memcpy(uid, p, 4); std::memcpy(newSlot, p + 4, 4);
    if (n == 12) std::memcpy(prevSlot, p + 8, 4); else *prevSlot = senderSlot;
    return true;
}
/* M7a2 FOLD 3 [m7a2f3-lr0]: THE NOTEBOOK ROAD'S GATE FOR OWNER_MOVED (store.cpp StoreLiveRoadLocal) - every form the session layer
   decodes passes: the 12-byte {uid, new, previous} every sender has used since fold 1, and the 8-byte legacy pair. T756: an
   8-byte-only gate (LivePairDecode) dropped every hand-over word as malformed. The bytes go on unchanged; the session layer decodes. */
inline bool OwnerMovedRoadAccepted(const char* p, size_t n, unsigned int senderSlot)
{
    unsigned int uid = 0, newSlot = 0, prevSlot = 0;
    return OwnerMovedDecode(p, n, senderSlot, &uid, &newSlot, &prevSlot);
}
/* item 2 (manager decision): WHO MAY MOVE AN OWNER RECORD. The RECORDED owner naming another player (the old owner's word), OR a sender
   naming the recorded owner as previous and ITSELF as new (the taker's announce - its world-server order puts it before its own SPAWN). One
   naming the owner already on record is a no-op: both words arrive, the second finds the first's work done. */
enum { kOwnerMovedTake = 0, kOwnerMovedNoop = 1, kOwnerMovedRefuse = 2, kOwnerMovedMine = 3, kOwnerMovedUnknown = 4 };
inline int OwnerMovedDecide(bool ownedHere, bool ownerKnown, bool ownerIsSender, bool newIsSender, bool prevIsOwner, bool newIsOwner)
{
    if (!ownerKnown) return kOwnerMovedUnknown;
    if (ownedHere) return kOwnerMovedMine;
    if (newIsOwner) return kOwnerMovedNoop;
    if (ownerIsSender && !newIsSender) return kOwnerMovedTake;
    if (newIsSender && prevIsOwner) return kOwnerMovedTake;
    return kOwnerMovedRefuse;
}
/* item 1: AN OWNER_MOVED OWED - the old owner released a character while its world link was down. Sent once the link is ready, unless
   this game runs the character again (it took it back) or holds no record of it any more (the session was left). */
enum { kOwedWait = 0, kOwedSend = 1, kOwedDrop = 2 };
inline int OwnerMovedOwedDecide(bool linkReady, bool runsItAgain, bool recordKnown)
{
    if (runsItAgain || !recordKnown) return kOwedDrop;
    return linkReady ? (int)kOwedSend : (int)kOwedWait;
}
/* items 3, 4: CLAIMS ON ONE MARKED COPY - one per claiming player (its latest SPAWN), at most kSweepClaimsPerUid. SweepClaimSlot: where a
   claim from `sender` goes (its own row, else a new one; -1 = full). SweepClaimPick: the claim TAKEN - the one from the player an
   OWNER_MOVED named (`named`, 0 = none named), else the FIRST (a later claim never silently beats an earlier one); *dropped = the others. */
const size_t kSweepClaimsPerUid = 4;
inline int SweepClaimSlot(const std::vector<unsigned int>& claimants, unsigned int sender, size_t maxClaims)
{
    for (size_t i = 0; i < claimants.size(); ++i) if (claimants[i] == sender) return (int)i;
    return claimants.size() < maxClaims ? (int)claimants.size() : -1;
}
inline int SweepClaimPick(const std::vector<unsigned int>& claimants, unsigned int named, size_t* dropped)
{
    int pick = -1;
    if (named == 0) { if (!claimants.empty()) pick = 0; }
    else for (size_t i = 0; i < claimants.size(); ++i) if (claimants[i] == named) { pick = (int)i; break; }
    if (dropped != 0) *dropped = claimants.size() - (pick >= 0 ? 1u : 0u);
    return pick;
}
/* item 6: the answered-asks list belongs to ONE world-server link - the world server numbers its asks from 0 again after a restart, so a
   new link generation clears it before the test. */
inline bool CatchupAnswerFirstOnLink(std::vector<unsigned long long>* recent, long* seenGen, long gen, unsigned int slot, unsigned int askNo)
{
    if (recent != 0 && seenGen != 0 && *seenGen != gen) { recent->clear(); *seenGen = gen; }
    return CatchupAnswerFirst(recent, slot, askNo);
}
/* item 6: A RELAYED MOVE FOR A CHARACTER THIS GAME HAS NO RECORD OF (its catch-up state not yet here) is HELD - the latest per uid - and
   applied right after that character's SPAWN from the same player; a hold older than kMoveHoldSec is dropped; at most kMoveHoldMax. */
enum { kMoveHoldApply = 0, kMoveHoldHold = 1 };
inline int RelayedMoveHoldDecide(bool relayed, bool ownedHere, bool ownerKnown)
{
    return (relayed && !ownedHere && !ownerKnown) ? (int)kMoveHoldHold : (int)kMoveHoldApply;
}
const size_t kMoveHoldMax = 512;
const double kMoveHoldSec = 10.0;
struct MoveHeld { unsigned int sender; double at; std::vector<char> payload; MoveHeld() : sender(0), at(0.0) {} };
struct MoveHoldBook { std::map<unsigned int, MoveHeld> rows; };
enum { kMoveHoldPutNew = 0, kMoveHoldPutReplaced = 1, kMoveHoldPutFull = 2 };
inline int MoveHoldPut(MoveHoldBook* b, unsigned int uid, unsigned int sender, const std::vector<char>& payload, double now)
{
    std::map<unsigned int, MoveHeld>::iterator it = b->rows.find(uid);
    if (it == b->rows.end() && b->rows.size() >= kMoveHoldMax) return kMoveHoldPutFull;
    const bool had = it != b->rows.end();
    MoveHeld& h = b->rows[uid];
    if (!had) h.at = now;   /* the first hold's time: a stream of MOVEs does not keep an unknown character held for ever */
    h.sender = sender; h.payload = payload;
    return had ? (int)kMoveHoldPutReplaced : (int)kMoveHoldPutNew;
}
inline bool MoveHoldTake(MoveHoldBook* b, unsigned int uid, std::vector<char>* payload, unsigned int* sender)
{
    std::map<unsigned int, MoveHeld>::iterator it = b->rows.find(uid);
    if (it == b->rows.end()) return false;
    if (payload != 0) payload->swap(it->second.payload);
    if (sender != 0) *sender = it->second.sender;
    b->rows.erase(it);
    return true;
}
inline size_t MoveHoldExpire(MoveHoldBook* b, double now, double sec)
{
    size_t n = 0;
    for (std::map<unsigned int, MoveHeld>::iterator it = b->rows.begin(); it != b->rows.end(); )
    {
        if (now - it->second.at > sec) { b->rows.erase(it++); ++n; } else ++it;
    }
    return n;
}
/* item 8 (the notebook, store_main.cpp OnLive): THE 'MISSED' STAMP. An AREA message about a sector that a game has NOT in its delivery
   set now, but had within kCatchupRejoinSec (it still holds a stamp), was dropped for that game: the stamp is erased, so when the sector
   comes back it is newly delivered and caught up - not taken for an edge flicker that missed nothing. */
inline bool AreaStampMissed(std::map<int, double>* stamps, const std::set<int>* delivery, int key)
{
    if (stamps == 0 || key < 0) return false;
    if (delivery != 0 && delivery->count(key) != 0) return false;
    return stamps->erase(key) != 0;
}
/* fold 1 item 7 [m7a2f-lr5]: only a ONE-OFF kind's miss erases the stamp. A repeating MOVE / STATE / INTENT is replaced by the next one, so
   a sector-edge flicker that dropped one of those missed nothing a full catch-up must restore. */
inline bool AreaMissedIsOneOff(unsigned int innerType) { return innerType != kInnerMove && innerType != kInnerState && innerType != kInnerIntent
                                                                && innerType != kInnerInside; }   /* P25 fold 2: INSIDE repeats every 5 s */

}   /* namespace cooplive */

#endif
