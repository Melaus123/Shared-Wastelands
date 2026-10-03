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

}   /* namespace cooppg */

#endif
