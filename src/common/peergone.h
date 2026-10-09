/* M8 (T-197 piece 8) - PER-PLAYER LEAVE. Pure: the world server (src/coop-store/store_main.cpp), the plugin
 * (src/coop-plugin/net/session.cpp) and the offline suite (src/coop-test) compile this same header.
 *
 * THE WIRE: PLAYER_GONE (57), world server -> every remaining admitted game, {u16 slot}: the PERMANENT slot of the player whose
 * connection the world server closed (store_main.cpp PeerGone - a clean unlink, an ENet timeout, an eviction) and did not see admitted
 * again within the hold (PlayerGoneHoldDecide below: kGraceSec, 10 s - a world-server link blip is not a leave, T240). World-server
 * protocol 62 (61 is effort/m11a's). A 60 world server never sends it and a 60 game would log it as unknown: the pair SHIPS TOGETHER.
 *
 * THE RULE (owner decisions 53, 54): a player is an id and its SLOT is what leaves. A game removes exactly the rows of its owner
 * table (net/session.cpp g_owner: uid -> owner key) whose key is that player's key, cooplive::RelayPeerId(slot), and that this game
 * did not author - never "everything not mine", which with three games removed the third game's characters too.
 *
 * TWO ROADS, ONE REMOVAL. The session link's own peer-gone (the link to the one other game) CAPTURES, at its DOWN edge, the rows of
 * that link's peer: keyed by its slot (once PEER_SLOT said it) or still by a raw transport id (only the session link writes raw
 * keys; a relayed row is always keyed by a slot). Either road removes a row only while it still carries the key it was selected or
 * captured with (PeerGoneStillTheirs) and erases it when it does, so the road that runs second finds nothing of the same uid:
 * nothing is removed twice and nothing selected is left behind.
 */
#ifndef COOP_COMMON_PEERGONE_H
#define COOP_COMMON_PEERGONE_H

#include <map>
#include <set>
#include <vector>
#include <cstring>
#include "liveenvelope.h"
#include "areadrop.h"   /* M9f1 (M9 review LOW): kAreaSeats / kAreaMaskWords, not 256 / 8 */

namespace cooppg {

const unsigned int kMsgPlayerGone = 57;   /* world-server message number (protocol 62) */
const size_t kPlayerGoneBytes = 2;       /* {u16 slot} */

typedef std::map<unsigned int, unsigned int> OwnerMap;   /* uid -> owner key (net/session.cpp g_owner) */
typedef std::set<unsigned int> UidSet;                   /* the uids this game authored (g_localOwned) */

/* false (nothing appended) = a slot a u16 cannot hold */
inline bool PlayerGoneEncode(std::vector<char>* b, unsigned int slot)
{
    if (b == 0 || slot > cooplive::kLiveSlotMax) return false;
    const unsigned short s = (unsigned short)slot;
    const size_t at = b->size();
    b->resize(at + kPlayerGoneBytes);
    std::memcpy(&(*b)[at], &s, kPlayerGoneBytes);
    return true;
}
inline bool PlayerGoneDecode(const char* p, size_t n, unsigned int* slot)
{
    if (p == 0 || slot == 0 || n != kPlayerGoneBytes) return false;
    unsigned short s = 0;
    std::memcpy(&s, p, kPlayerGoneBytes);
    *slot = s;
    return true;
}

/* PLAYER_GONE {slot}: the rows that player owns here - its key, not authored here. Appended in uid order. */
inline void PeerGoneSelect(const OwnerMap& owners, const UidSet& local, unsigned int slot, std::vector<unsigned int>* out)
{
    if (out == 0 || slot > cooplive::kLiveSlotMax) return;
    const unsigned int key = cooplive::RelayPeerId(slot);
    for (OwnerMap::const_iterator it = owners.begin(); it != owners.end(); ++it)
        if (it->second == key && local.find(it->first) == local.end()) out->push_back(it->first);
}

/* THE RULE BEFORE M8 (net/session.cpp OnPeerGone until M8): every row this game did not author. Kept only so the offline suite can
   show what it did with three games - it is not called by the plugin. */
inline void PeerGoneSelectNotMine(const OwnerMap& owners, const UidSet& local, std::vector<unsigned int>* out)
{
    if (out == 0) return;
    for (OwnerMap::const_iterator it = owners.begin(); it != owners.end(); ++it)
        if (local.find(it->first) == local.end()) out->push_back(it->first);
}

/* THE SESSION LINK'S DOWN EDGE: add to `pending` (uid -> key as it is NOW) every row not authored here that is keyed to the link
   peer - its slot's key when the slot is known (linkSlot >= 0), or a raw transport id. Returns how many rows it added. */
inline int PeerGoneSessionCapture(const OwnerMap& owners, const UidSet& local, int linkSlot, OwnerMap* pending)
{
    if (pending == 0) return 0;
    const bool slotKnown = linkSlot >= 0 && (unsigned int)linkSlot <= cooplive::kLiveSlotMax;
    const unsigned int key = slotKnown ? cooplive::RelayPeerId((unsigned int)linkSlot) : 0u;
    int n = 0;
    for (OwnerMap::const_iterator it = owners.begin(); it != owners.end(); ++it)
    {
        if (local.find(it->first) != local.end()) continue;
        if (!cooplive::IsRelayPeer(it->second) || (slotKnown && it->second == key)) { (*pending)[it->first] = it->second; ++n; }
    }
    return n;
}

/* Remove this row now? Only while it is still there, not authored here, and keyed as it was selected or captured. A row the
   other road already removed is gone; a row claimed since (a SPAWN, a hand-over) carries another key. */
inline bool PeerGoneStillTheirs(const OwnerMap& owners, const UidSet& local, unsigned int uid, unsigned int key)
{
    if (local.find(uid) != local.end()) return false;
    OwnerMap::const_iterator it = owners.find(uid);
    return it != owners.end() && it->second == key;
}

/* THE SESSION ROAD'S TAKE (net/session.cpp OnPeerGone and PeerGoneSweepPending): every captured row still keyed as it was at the DOWN
   edge goes to `uids` (uid order); every other mark is spent and counted (PLAYER_GONE removed it first, or it was re-claimed). `pending`
   is emptied either way. Returns the spent marks (peerGoneM8 alreadyGone). */
inline long long PeerGoneTake(const OwnerMap& owners, const UidSet& local, OwnerMap* pending, std::vector<unsigned int>* uids)
{
    if (pending == 0) return 0;
    long long alreadyGone = 0;
    for (OwnerMap::const_iterator it = pending->begin(); it != pending->end(); ++it)
        if (PeerGoneStillTheirs(owners, local, it->first, it->second)) { if (uids != 0) uids->push_back(it->first); } else ++alreadyGone;
    pending->clear();
    return alreadyGone;
}

/* THE LOADED-BIT RULE (zones.cpp ZonesForgetEffectiveMask) - M9 (T-197; world-server protocol 65): the effective area mask is
   coopdrop::kAreaSeats wide (areadrop.h, kAreaMaskWords u32 words; M9f1) and indexed by SEAT, not slot, so the caller names both: the departed player's slot and
   the seat its column holds in the game's seat book (-1 = no column yet). A known slot with a seat: that seat's bit only (owner
   decision 54 - every other player's carried bits stand). A known slot with no seat yet: none here (the caller arms it by slot at
   the first map that lists it). An unknown slot (-1: a session peer that never announced its number): every bit, the pre-M8 rule.
   `bits` = kAreaMaskWords words. Returns the bits armed (0, 1 or kAreaSeats). M9f1 */
inline int PeerGoneMaskBits(int slot, int seat, unsigned int* bits)
{
    int w, armed = 0;
    if (bits != 0) for (w = 0; w < coopdrop::kAreaMaskWords; ++w) bits[w] = 0u;   /* M9f1 */
    if (slot < 0) { if (bits != 0) for (w = 0; w < coopdrop::kAreaMaskWords; ++w) bits[w] = 0xFFFFFFFFu; armed = coopdrop::kAreaSeats; }
    else if (seat >= 0 && seat < coopdrop::kAreaSeats) { if (bits != 0) bits[seat >> 5] = 1u << (seat & 31); armed = 1; }
    return armed;
}

/* PER-PLAYER CLEAN-UP of a table row kept for another player (a held name or slave state, a hire request or promise, a context platoon,
   a squad handed over): rowSlot is the player the row was recorded for (-1 unknown), goneSlot the player who left. A known goneSlot takes
   only its own rows - a row recorded with no slot stays; goneSlot < 0 (the old link's peer that never announced its slot) takes every
   row, the whole-link rule that link always had. */
inline bool PlayerGoneTakesRow(int rowSlot, int goneSlot)
{
    return goneSlot < 0 || rowSlot == goneSlot;
}

/* THE SESSION PEER'S PLAYER_GONE (net/session.cpp OnPlayerGone): skipped while the session link is up and PLAYER_GONE names that
   link's peer. The world server lost that player's world-server link, but the player is still playing on the session link, whose own
   DOWN edge removes its rows (T240: a player has not left because the world server lost them). The M11 flip (the session link folded
   into the world-server road) removes this skip. */
/* M8 re-check 2a: after a session drop the RE-LINKED peer announces its slot (PEER_SLOT) only once its own world-server WELCOME has
   come, so while the link is up and the slot is still unknown (-1) the slot captured at the last DOWN edge names the session peer. */
inline bool PlayerGoneSkipsSessionPeer(unsigned int slot, int linkPeerSlot, bool sessionLinkUp, int lastCapturedSlot)
{
    if (!sessionLinkUp) return false;
    if (linkPeerSlot >= 0) return (unsigned int)linkPeerSlot == slot;
    return lastCapturedSlot >= 0 && (unsigned int)lastCapturedSlot == slot;
}

/* THE WORLD SERVER'S HOLD (store_main.cpp PlayerGoneHoldTick / PlayerGoneHoldReturned): a closed player's PLAYER_GONE waits `graceSec`
   after the close and is CANCELLED if that player (its permanent slot) is admitted again before it goes - a re-dial, or an eviction
   whose new connection is that very player, is not a leave. */
const int kPgHoldWait = 0;     /* keep holding */
const int kPgHoldSend = 1;     /* the grace ran out with the player still away: send PLAYER_GONE now */
const int kPgHoldCancel = 2;   /* the player is back: the hold is dropped and nothing is sent */
inline int PlayerGoneHoldDecide(double heldAt, double now, double graceSec, bool admittedAgain)
{
    if (admittedAgain) return kPgHoldCancel;
    return (now - heldAt >= graceSec) ? kPgHoldSend : kPgHoldWait;
}
/* THE HOLD AS A GAME KNOWS IT: the world server's kGraceSec (store_main.cpp), the same number. A game cannot read the world server's
   constant; it uses this one where it must judge "away longer than the hold" itself (lostcopy.h ReturnGiveUpOwnerGone). */
const double kPlayerGoneHoldSec = 10.0;
/* The margin a game adds to that hold where it judges an absence by its own clock (the return check: its own link-down, another
   player's time out of the world roster) - its reads come a little apart from the world server's own close and hold. */
const double kReturnAwayMarginSec = 10.0;

/* A FINAL LEAVE'S NPCs - PLAYER_GONE arrives only after the world server's hold (PlayerGoneHoldDecide), so a link
   blip never reaches this. For each row of the leaver's that this game holds a copy of: the leaver's own player characters and the
   people of a player faction go as before (kGtDrop); an NPC is taken over by the FIRST game of the area's receiver order - the order a
   live hand-over offers in (liveowner.h AreaReceiverOrder, asked as the leaver: every other in-world game with the area loaded whose
   engine would keep it at its spot, the area's holder first, then the lower slot). That game takes it (kGtTake); every other game keeps
   its copy and the leaver's row (kGtWait) until the taker's OWNER_MOVED re-keys it. A map that cannot say now (stale, or this game has
   no slot yet) is asked again at the next look (kGtHold); that hold ends after kGoneTakeLooksMax looks (one a second) and the copy
   goes as before. A first receiver that has not taken it within kGoneTakeLooksMax looks is passed over (kGtNext): the caller adds that
   slot to its tried list (GoneTakeTriedAdd) and asks the order again without it, as the live RELEASE walk does, so the next game in
   line takes it; the copy goes only when no untried game is left (receivers 0). No copy here, or no game with the area loaded: as
   before.
   playerChar: 1 a player character or a person of a player faction, 0 an NPC, -1 unreadable (as before: it goes); receivers:
   AreaReceiverOrder's answer asked as the leaver (-1 hold, 0 none, k >= 1); firstSlot: its first slot; mySlot: this game's slot (-1
   not known yet); looks: the looks already made at this first receiver (or in this hold). */
enum { kGtDrop = 0, kGtTake = 1, kGtWait = 2, kGtHold = 3, kGtNext = 4 };
const int kGoneTakeLooksMax = 15;
inline int GoneTakeOverDecide(int playerChar, int haveCopy, int receivers, int firstSlot, int mySlot, int looks)
{
    if (playerChar != 0 || haveCopy == 0) return kGtDrop;
    const bool more = looks < kGoneTakeLooksMax;
    if (mySlot < 0 || receivers < 0) return more ? kGtHold : kGtDrop;
    if (receivers == 0) return kGtDrop;
    if (firstSlot == mySlot) return kGtTake;
    return more ? kGtWait : kGtNext;
}
/* A held or waiting row is settled once it no longer carries the leaver's key: the taker's OWNER_MOVED re-keyed it, this game took
   it, or it went. */
inline bool GoneTakeSettled(const OwnerMap& owners, const UidSet& local, unsigned int uid, unsigned int goneKey)
{
    return !PeerGoneStillTheirs(owners, local, uid, goneKey);
}
/* A passed-over receiver joins the tried list once (the order never names a tried slot again, so the walk ends). Answers 1 when it
   was added. */
inline int GoneTakeTriedAdd(std::vector<int>* tried, int slot)
{
    if (tried == 0 || slot < 0) return 0;
    for (size_t i = 0; i < tried->size(); ++i) if ((*tried)[i] == slot) return 0;
    tried->push_back(slot);
    return 1;
}

/* THE SESSION'S END (net/session.cpp SessionLeave, SessionHost, SessionJoin): which owner rows go. With the world-server link up, a
   row keyed to a world-road player (a slot key) that this game did not author stays - that player is still reached through the world
   server and only the session ended; the session peer's own rows were taken at the link's DOWN edge before this. Every other row goes
   (this game's own rows from the per-session view only: the authorship set keeps them), and with the world link down every row goes. */
inline bool SessionEndKeepsRow(bool worldLinkUp, bool authoredHere, unsigned int key)
{
    return worldLinkUp && !authoredHere && cooplive::IsRelayPeer(key);
}

}   /* namespace cooppg */

#endif
