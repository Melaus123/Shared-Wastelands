/* inv7c (e47-inv7-replan.md section 5; decision D6): AN INTERRUPTED TAKE IS HELD IN ESCROW AND SETTLED BY PLACED OR ITS
   RE-SEND AT LINK-UP, NEVER ROLLED BACK BLINDLY. The pure decisions, shared by items.cpp and the offline suite.

   OWNER  a TAKE row whose ok CONFIRM went out is not rolled back at 10 s: it moves to ESCROW (the object stays alive in no
          inventory). A PLACED settles it (the same link: resolvedPlaced; after a link-up: resolvedResent). Nothing heard for
          kEscrowQuietMs (restarted at every link edge), or this game leaving the session, returns it to the owner - a
          duplicate rather than a loss (D6). A put-back the engine refuses keeps the row in escrow instead of destroying it.
   TAKER  every PLACED it answers is listed; an entry that sat kTakerSettleMs on a link that stayed up is delivered (reliable
          channel) and dropped; one that could not be sent, or whose link went down, is re-sent at the next link-up. */
#ifndef COOP_ITEMESCROW_H
#define COOP_ITEMESCROW_H

#include <string>
#include <vector>
#include "liveenvelope.h"   /* T-355: cooplive::IsRelayPeer - a sender key that names a player slot */

namespace coopesc
{
const unsigned int kEscrowQuietMs = 300000;      /* 5 minutes without a word, restarted at every link edge */
const unsigned int kTakerSettleMs = 30000;       /* a PLACED sent on a link that stayed up this long was delivered */
const unsigned int kTakerResendDelayMs = 5000;   /* after a link-up, before the re-send (the handshake goes first) */
const int kTakerCap = 32;
const int kDoneCap = 64;

/* The owner's pending row at a tick. dir 0 TAKE / 1 GIVE; cfSent: our ok CONFIRM went out (or is held by the TEST-only
   confirmdelay lever and will); escrow: the row is already in escrow. */
const int kExpWait = 0, kExpRollback = 1, kExpStand = 2, kExpEscrow = 3, kExpReturn = 4;
inline int ExpireAction(int dir, int cfSent, int escrow, unsigned int sinceSentMs, unsigned int sinceQuietMs,
                        unsigned int timeoutMs, unsigned int quietMs)
{
    if (escrow != 0) return (sinceQuietMs >= quietMs) ? kExpReturn : kExpWait;
    if (sinceSentMs < timeoutMs) return kExpWait;
    if (dir != 0) return kExpStand;               /* a GIVE nobody revoked stands */
    return (cfSent != 0) ? kExpEscrow : kExpRollback;
}

/* inv7c fold (review-inv7c 3): the quiet clock runs ONLY WHILE THE LINK IS UP - a down link is not an unanswering peer. A frame
   gap longer than kQuietDtCapMs (a save stall, a load) counts as that cap, so a hitch never ages an escrow. */
const unsigned int kQuietDtCapMs = 2000;
inline unsigned int QuietStep(unsigned int quietMs, unsigned int dtMs, int linkUp, unsigned int dtCapMs)
{
    if (linkUp == 0) return quietMs;
    if (dtMs > dtCapMs) dtMs = dtCapMs;
    const unsigned int n = quietMs + dtMs;
    return (n < quietMs) ? 0xFFFFFFFFu : n;
}
/* inv7c fold (review 1): at a link-up EVERY TAKE row whose ok CONFIRM went out - live or in escrow - may be answered from the
   peer's NEW id (the host numbers a reconnecting joiner afresh), so each is marked relinked.
   p105g-f2-1 (row T-435): a GROUND TAKE row is no longer left out - it is held in escrow like a character's / box's (its picker
   showed the item at pickup, so a 10 s put-back would be a second copy), and its PLACED re-send comes from the renumbered peer too. */
inline int RelinkOnUp(int dir, int cfSent, int isGround)
{
    (void)isGround;
    return (dir == 0 && cfSent != 0) ? 1 : 0;
}
/* inv7c fold (review 3/4): a PLACED that found no row by (id, peer). The order is fixed - an id already settled first, then an
   id handed back to its owner, then a relinked row (ids restart per process, so a stale match must lose). T-355: each of the
   three is asked by (sender player, id) - SameSender below - never by id alone. */
const int kMissDup = 0, kMissAfterReturn = 1, kMissRelinked = 2, kMissUnknown = 3;
inline int PlacedMissAction(int doneHas, int returnedHas, int relinkedFound)
{
    if (doneHas != 0) return kMissDup;
    if (returnedHas != 0) return kMissAfterReturn;
    if (relinkedFound != 0) return kMissRelinked;
    return kMissUnknown;
}
/* inv7c fold (review 5/6): a refused put-back of an escrow row (the engine would not re-add it, or control moved) keeps the row;
   the kGroundAfter-th refusal puts the item on the ground at its owner's feet instead. */
const int kGroundAfter = 3;
const int kRfKeep = 0, kRfGround = 1;
inline int RefusalAction(int refusals) { return (refusals >= kGroundAfter) ? kRfGround : kRfKeep; }

/* inv7c fold 2 (recheck 4): the session-ready flag carries the link generation it was stamped in. Accepted on an up link when
   it is this generation, or the one just before it (the host's HELLO can be dispatched before the up edge moves the generation);
   anything older belongs to a link that is gone. */
const int kRdAccept = 1, kRdStale = 2;
inline int ReadyFlagAction(long stampGen, long gen, int linkUp)
{
    return (linkUp != 0 && (stampGen == gen || stampGen + 1 == gen)) ? kRdAccept : kRdStale;
}

/* Which number a PLACED that found its row books. */
const int kPlLive = 0, kPlEscrowPlaced = 1, kPlEscrowResent = 2;
inline int PlacedKind(int escrow, int relinked)
{
    if (escrow == 0) return kPlLive;
    return (relinked != 0) ? kPlEscrowResent : kPlEscrowPlaced;
}

/* ItRollbackTake's put-back: `placed` is ItPlacePod's answer (1 / 2 placed, -1 the engine faulted, 0 refused). A refused
   re-add of a live object is KEPT in escrow; only an object that is not live any more is the old lost arm. */
const int kPbPut = 0, kPbFaulted = 1, kPbKeep = 2, kPbLost = 3;
inline int PutBackArm(int placed, int itemLive)
{
    if (placed == 1 || placed == 2) return kPbPut;
    if (placed == -1) return kPbFaulted;
    return (itemLive != 0) ? kPbKeep : kPbLost;
}

/* The taker: an ok CONFIRM for a TAKE (dir 0) whose own 10 s already lapsed here. The owner now waits for our answer, so a
   plain take - or a purchase whose timeout refund did not move money - is completed (placed); a purchase we already refunded
   is answered PLACED ok=0 so the owner puts the item and the shopkeeper's cats back. Anything else: nothing. */
const int kLtNone = 0, kLtPlace = 1, kLtAnswerNo = 2;
inline int LateTakeAction(int ok, int trade, int dir, int refunded)
{
    if (dir != 0 || ok == 0) return kLtNone;
    if (trade == 0 || refunded == 0) return kLtPlace;
    return kLtAnswerNo;
}

/* The taker's small list of PLACED answers not yet known delivered. */
struct TakerEntry
{
    int used;
    unsigned int id;
    int ok;
    int sent;           /* 1 = handed to the link */
    long gen;           /* its counterpart's generation when it was handed over (store.h StorePeerGen: the session link's for the session peer) */
    unsigned int at;
    int ground;         /* p105g-f2-2: a ground pickup's PLACED (it has no hand-over ledger line to cross-check) */
    unsigned int peer;  /* the player the answer goes to - the game the request was asked of (a relayed sender id; 0 = the session peer) */
    long ownLink;       /* THIS game's world-server link generation when it was handed over (store.h StoreLinkGen). Read only for a
                           roster-kind entry (gen < 0): its player's roster epoch does not move when our OWN world link drops and
                           comes back, so a PLACED handed to the world road just before such a drop is judged by this instead. */
};
/* A roster-kind entry (gen < 0) was handed over on a world-server link that has since been replaced by a new one. A link-kind
   entry (gen >= 0) never is - the session link's own generation already moves on its every edge. */
inline int TakerOwnLinkMoved(long gen, long recorded, long ownLinkNow) { return (gen < 0 && recorded != ownLinkNow) ? 1 : 0; }
struct TakerList
{
    TakerEntry e[kTakerCap];
    int next;
    TakerList() : next(0) { Clear(); }
    void Clear() { for (int i = 0; i < kTakerCap; ++i) { e[i].used = 0; e[i].id = 0; e[i].ground = 0; e[i].peer = 0; e[i].ownLink = 0; } next = 0; }   /* p105g-f2-2 */
    int Count() const { int n = 0; for (int i = 0; i < kTakerCap; ++i) if (e[i].used != 0) ++n; return n; }
    int Find(unsigned int id) const { for (int i = 0; i < kTakerCap; ++i) if (e[i].used != 0 && e[i].id == id) return i; return -1; }
    /* 1 = the list was full and an entry was evicted - inv7c fold (review 8): the OLDEST one handed to the current link (the
       likeliest to be delivered), else the oldest handed to any link, else the oldest of all. gen: the new entry's counterpart's
       generation now; peer: that counterpart; genNowOf (may be 0 = every entry's counterpart is at gen): each listed entry's own
       counterpart's generation now - "handed to the current link" is asked of each entry's own player. ownLink: this game's
       world-server link generation now (StoreLinkGen), recorded with the new entry; a roster-kind entry handed to an older one
       is not "handed to the current link". */
    int Note(unsigned int id, int ok, int sent, long gen, unsigned int now, int ground = 0, unsigned int peer = 0, const long* genNowOf = 0,
             long ownLink = 0)
    {
        int slot = Find(id);
        int over = 0;
        if (slot < 0) for (int i = 0; i < kTakerCap; ++i) if (e[i].used == 0) { slot = i; break; }
        if (slot < 0)
        {
            int pass;
            for (pass = 0; pass < 3 && slot < 0; ++pass)
            {
                unsigned int oldest = 0;
                for (int i = 0; i < kTakerCap; ++i)
                {
                    if (pass == 0 && (e[i].sent == 0 || e[i].gen != (genNowOf != 0 ? genNowOf[i] : gen)
                                      || TakerOwnLinkMoved(e[i].gen, e[i].ownLink, ownLink) != 0)) continue;
                    if (pass == 1 && e[i].sent == 0) continue;
                    const unsigned int age = now - e[i].at;
                    if (slot < 0 || age > oldest) { slot = i; oldest = age; }
                }
            }
            over = 1;
        }
        e[slot].used = 1; e[slot].id = id; e[slot].ok = ok; e[slot].sent = sent; e[slot].gen = gen; e[slot].at = now; e[slot].ground = ground;
        e[slot].peer = peer; e[slot].ownLink = ownLink;
        return over;
    }
    /* re-send it now? only on an up link: never handed over, or handed to a link that has since gone down - for a roster-kind
       entry that includes this game's own world-server link (ownLink: StoreLinkGen now) */
    int Due(int i, long gen, int linkUp, long ownLink = 0) const
    {
        if (i < 0 || i >= kTakerCap || e[i].used == 0 || linkUp == 0) return 0;
        return (e[i].sent == 0 || e[i].gen != gen || TakerOwnLinkMoved(e[i].gen, e[i].ownLink, ownLink) != 0) ? 1 : 0;
    }
    void MarkSent(int i, long gen, unsigned int now, long ownLink = 0) { e[i].sent = 1; e[i].gen = gen; e[i].at = now; e[i].ownLink = ownLink; }
    /* entry i, handed to its counterpart at the generation that counterpart still has (gen: its generation now; linkUp: it is
       here) settleMs ago: delivered - dropped. A roster-kind entry also needs this game's own world-server link to be the one it
       was handed to (ownLink: StoreLinkGen now) - else it is due again, not delivered. 1 = dropped. */
    int PruneAt(int i, long gen, int linkUp, unsigned int now, unsigned int settleMs, long ownLink = 0)
    {
        if (i < 0 || i >= kTakerCap || linkUp == 0) return 0;
        if (e[i].used != 0 && e[i].sent != 0 && e[i].gen == gen && TakerOwnLinkMoved(e[i].gen, e[i].ownLink, ownLink) == 0
            && (unsigned int)(now - e[i].at) >= settleMs) { e[i].used = 0; return 1; }
        return 0;
    }
    /* every entry judged by ONE counterpart (the one-link reading): the number dropped */
    int Prune(long gen, int linkUp, unsigned int now, unsigned int settleMs)
    {
        int n = 0;
        for (int i = 0; i < kTakerCap; ++i) n += PruneAt(i, gen, linkUp, now, settleMs);
        return n;
    }
};

/* MORE THAN TWO PLAYERS: AN OWNER ROW'S AND A TAKER ENTRY'S EDGES ARE ITS OWN COUNTERPART PLAYER'S. Each remembers the player it
   waits on (the asker; the game the request was asked of) and that player's generation (store.h StorePeerGen: the session link's
   generation - never negative - for the session peer, judged by the old link exactly as before; a negative roster epoch for any
   other player). Its edges come when THAT player leaves or comes back, or is judged the other way (the two kinds of number never
   meet), never because some other player did. Two games: the one counterpart is the session peer, every row reads the link
   generation, and every edge falls where the one link edge fell. */
const long kEscGenNone = -2147483647L;   /* no generation seen yet (a roster epoch is -1 - arrivals: never this low) */
/* The generation an owner row's key starts from at its first edge check (rows are made zeroed, between two checks). Judged by the
   link (genNow >= 0): the link generation the PREVIOUS check saw (linkSeen), so a link edge between the row's making and this check
   is that row's edge too - as when one check walked every row at each link edge. No check yet, or judged by the roster: now. */
inline long EscKeyFirstGen(long genNow, long linkSeen) { return (genNow >= 0 && linkSeen != kEscGenNone) ? linkSeen : genNow; }
/* One owner row at an edge check. kEscEdQuiet: its counterpart's generation moved since the last check (an escrow's quiet clock
   restarts). kEscEdRelink as well: that player is here now and the row is RelinkOnUp's (a PLACED may now come from its NEW id).
   *rowGen moves to genNow. */
const int kEscEdQuiet = 1, kEscEdRelink = 2;
inline int EscRowEdge(long* rowGen, long genNow, int here, int dir, int cfSent, int isGround)
{
    if (*rowGen == genNow) return 0;
    *rowGen = genNow;
    return kEscEdQuiet | ((here != 0 && RelinkOnUp(dir, cfSent, isGround) != 0) ? kEscEdRelink : 0);
}
/* May a taker entry be re-sent to its counterpart now? Judged by the link (genNow >= 0): only on the session-READY link (readyGen:
   the link generation whose HELLO accepted / WELCOME was taken - the handshake goes first), as before. Judged by the roster: while
   that player is IN_WORLD - it came through the world server, there is no session handshake to wait for. */
inline int TakerResendOpen(long genNow, int here, long readyGen) { return (here != 0 && (genNow < 0 || readyGen == genNow)) ? 1 : 0; }
/* A CAPTURE_DONE's sender against the capture's counterpart (the victim's owner the request went to), both keys folded by
   cooplive::PlayerKeyOf. Two slot keys: the same player only - another player's answer to a request id of ours is not this
   capture's. A raw key on either side is the session peer before its slot is known (a known link slot folds it): it cannot be
   shown to be another player, so it matches (the one-other-game reading, unchanged). */
inline int SameCounterpart(unsigned int pendKey, unsigned int fromKey)
{
    if (!cooplive::IsRelayPeer(pendKey) || !cooplive::IsRelayPeer(fromKey)) return 1;
    return (pendKey == fromKey) ? 1 : 0;
}

/* p105g-f2-2: a re-sent ok PLACED is cross-checked against mmo6's hand-over ledger only when it is not a ground pickup's - a ground
   pickup writes no hand-over line (its record is the refund ledger), so the check would only ever say "disagree". */
inline int TakerLedgerCheck(int ok, int ground) { return (ok != 0 && ground == 0) ? 1 : 0; }

/* T-355 (more than two players): request ids are each REQUESTING game's own, so two other players can use the same one - a
   PLACED is matched by WHO sent it as well. a and b are sender keys already folded by cooplive::PlayerKeyOf (the permanent
   player slot whenever one is known). Two slot keys match only when they are the same player. Two raw keys (no slot known for
   either - only ever the one session peer, perhaps renumbered by a reconnect) always match.
   T-355 fold 1 (review F1): a raw key matches a SLOT key only while THIS game has no notebook slot itself (mySlotKnown 0) - the
   two-game, no-notebook world, where raw is the only form a sender takes: the two-player reading, id alone, unchanged. Once
   this game has a slot (mySlotKnown 1) the other players are numbered, so a raw key is a session peer that never said its slot
   (it has none of its own) and matches raw keys only - never another player's slot key, in either direction: its re-send of
   id 7 cannot settle a third player's row 7, and a third player's settled id 7 does not make its real re-send a duplicate.
   T-355 fold 2 (re-check D1): b is the ARRIVING sender. A raw b also matches a slot key when bRawIsSessionPeerUnslotted - b is the
   CURRENT session link's peer and this game does not know that link's slot (RawIsSessionPeerUnslotted below): the session peer
   whose notebook link is down never sends PEER_SLOT, yet it may hold a slot and its row may be keyed by it (the TAKE came through
   the notebook) - it can only be the session peer, whatever its slot (the id-alone reading for it). A raw b that is NOT the
   current session peer still never matches a slot key while this game has a slot. a (a stored key) is never widened. */
inline int SameSender(unsigned int a, unsigned int b, int mySlotKnown, int bRawIsSessionPeerUnslotted)
{
    const bool rawA = !cooplive::IsRelayPeer(a), rawB = !cooplive::IsRelayPeer(b);
    if (rawA && rawB) return 1;
    if (rawB) return (mySlotKnown == 0 || bRawIsSessionPeerUnslotted != 0) ? 1 : 0;
    if (rawA) return (mySlotKnown == 0) ? 1 : 0;
    return (a == b) ? 1 : 0;
}
/* T-355 fold 2: SameSender's bRawIsSessionPeerUnslotted, from the arriving sender key, whether a session peer is up (haveSessionPeer)
   with transport id sessionPeerRaw, and LinkPeerSlot(). 1 only for a raw key equal to the current session peer's id while the
   link's slot is unknown (a known slot folds the raw id to that slot before this is asked). */
inline int RawIsSessionPeerUnslotted(unsigned int arriving, int haveSessionPeer, unsigned int sessionPeerRaw, int linkPeerSlot)
{
    return (!cooplive::IsRelayPeer(arriving) && haveSessionPeer != 0 && arriving == sessionPeerRaw && linkPeerSlot < 0) ? 1 : 0;
}

/* The owner's memory of TAKE ids already settled by a PLACED, so a re-send of one is a duplicate, not an unknown id.
   T-355: an entry is (sender player key, id) - another player's PLACED with the same id is not this one's duplicate. */
struct DoneRing
{
    unsigned int id[kDoneCap];
    unsigned int who[kDoneCap];   /* T-355: the sender's player key (cooplive::PlayerKeyOf) */
    int next;
    DoneRing() : next(0) { Clear(); }
    void Clear() { for (int i = 0; i < kDoneCap; ++i) { id[i] = 0; who[i] = 0; } next = 0; }
    void Note(unsigned int key, unsigned int v) { if (v == 0) return; id[next] = v; who[next] = key; next = (next + 1) % kDoneCap; }
    int Has(unsigned int key, unsigned int v, int mySlotKnown, int keyRawIsSessionPeerUnslotted) const   /* T-355 folds 1/2: as SameSender's (key arriving) */
    {
        if (v == 0) return 0;
        for (int i = 0; i < kDoneCap; ++i) if (id[i] == v && SameSender(who[i], key, mySlotKnown, keyRawIsSessionPeerUnslotted) != 0) return 1;
        return 0;
    }
};

/* The ledger cross-check (mmo6's pp.ledger, only while the records writer is on). Lines are paired by peer + message id +
   role, in file order: an IN line makes it 1, a LATER rev line makes it -1, an unpaired rev is a no-op. 0 = no IN line. */
template <class E>
int LedgerPlacedState(const std::vector<E>& v, const std::string& peer, unsigned int id, int dirIn, int dirRev, int role)
{
    int st = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].msgId != id || v[i].role != role || v[i].peer != peer) continue;
        if (v[i].dirIn == dirIn) st = 1;
        else if (v[i].dirIn == dirRev && st == 1) st = -1;
    }
    return st;
}
}   /* namespace coopesc */

#endif
