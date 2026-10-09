/* arrivals.h - M11 C2 (T-197, row M11; .modding/investigations/t197-parallel-plan-2026-10-02.md section C2, site table
   m11-s2-sites-2026-09-30.md): "WHICH OTHER PLAYER HAS JUST ENTERED THE WORLD?" ASKED OF THE WORLD SERVER'S PLAYER LIST AS WELL
   AS THE OLD GAME-TO-GAME LINK. The pure half, included by the plugin and the offline suite.
   WHY. About thirty places re-send their whole state ("a new player came - tell it everything") when the old session link comes
   up. A third player entering through the world server moves neither that link nor this game's world-server link, so the
   newcomer was told nothing.
   THE RULE.
   (1) THE DIFF (RosterArrivals): between two rosters, the other slots that newly reached IN_WORLD and the ones that left it. A
       roster in which THIS game's own row is not IN_WORLD lists nobody: an arrival means something only to a game in the world
       (so a game at the title never "sees" a player that is already there - it sees it when it enters itself).
   (2) THE MERGE (Book): the old link coming up is the session peer's arrival. It and the roster's IN_WORLD for the same slot
       are ONE arrival, whichever comes first: a roster arrival waits (waitLink) to absorb the next link-up bound to its slot,
       and a link that is up and bound to a slot absorbs every roster arrival of that slot while it stays up. A link-up whose
       peer slot is not known yet (PEER_SLOT not in) counts at once when no roster arrival is waiting - today's timing - and a
       roster arrival that comes while such a link is up is held until the slot is known; a link-up while a roster arrival is
       waiting is itself held until the slot is known. Either hold is decided after kBindWaitMs (counted). A game with no slot
       (no world server) has no roster arrivals, so every link-up counts at once: today's link-only behaviour.
   (3) THE CURSORS: every counted arrival gets the next number (seq) and a ring entry {slot, via}; a site keeps its own cursor and
       asks "what arrived since" (Since) - one serve per call, the slots for a site that can address one. The EPOCH (seq + the
       old link's downs and session leaves) is the drop-in replacement for the session link generation at the "generation changed"
       sites: in a two-game run it moves exactly as often as that generation (once per link-up, once per link-down or leave).
   (4) ONE PARTICULAR PLAYER (PeerByLink): a hold or a row kept for one counterpart is judged by the old link when that
       counterpart is the session peer (the raw link id, the link's slot, or - with the link down - the slot it last had, or any
       slot when the link went down before its slot was known), else by the roster and that slot's own arrival count.
   No engine, no ENet, no C++11 (VS2010). MAIN THREAD for the Book (the plugin feeds and asks it on the main thread only). */
#ifndef KM_ARRIVALS_H
#define KM_ARRIVALS_H

#include <cstddef>
#include <map>
#include <vector>
#include "joinstage.h"   /* coopjoin::RosterRow, kStageInWorld */

namespace mparrive {

const unsigned int kBindWaitMs = 10000;   /* a hold waiting for the session peer's slot is decided (counted) after this */
const int kRingCap = 64;                  /* arrivals remembered by slot; a cursor older than this reads "someone" (-1) */
enum Via { kViaRoster = 0, kViaLink = 1 };

/* this game's own row is IN_WORLD on the roster */
inline bool MeInWorld(const std::vector<coopjoin::RosterRow>& rows, int mySlot)
{
    for (size_t i = 0; i < rows.size(); ++i) if (mySlot >= 0 && (int)rows[i].slot == mySlot && rows[i].stage == (unsigned)coopjoin::kStageInWorld) return true;
    return false;
}
/* The other slots IN_WORLD on a roster, sorted - none when this game's own row is not IN_WORLD (or it has no slot). */
inline void InWorldOthers(const std::vector<coopjoin::RosterRow>& rows, int mySlot, std::vector<int>* out)
{
    out->clear();
    if (!MeInWorld(rows, mySlot)) return;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].stage != (unsigned)coopjoin::kStageInWorld || (int)rows[i].slot == mySlot) continue;
        const int s = (int)rows[i].slot;
        size_t at = 0;
        while (at < out->size() && (*out)[at] < s) ++at;
        if (at < out->size() && (*out)[at] == s) continue;
        out->insert(out->begin() + (std::ptrdiff_t)at, s);
    }
}
inline bool SlotListHas(const std::vector<int>& v, int s) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return true; return false; }
inline bool SlotListDrop(std::vector<int>* v, int s)
{
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i] == s) { v->erase(v->begin() + (std::ptrdiff_t)i); return true; }
    return false;
}
/* (1) THE DIFF over two in-world lists */
inline void InWorldDiff(const std::vector<int>& prevIn, const std::vector<int>& nowIn, std::vector<int>* arrived, std::vector<int>* left)
{
    arrived->clear(); left->clear();
    for (size_t i = 0; i < nowIn.size(); ++i) if (!SlotListHas(prevIn, nowIn[i])) arrived->push_back(nowIn[i]);
    for (size_t i = 0; i < prevIn.size(); ++i) if (!SlotListHas(nowIn, prevIn[i])) left->push_back(prevIn[i]);
}
/* (1) THE DIFF over two rosters (slot -> stage): the other slots that newly reached IN_WORLD, and the ones that left it */
inline void RosterArrivals(const std::vector<coopjoin::RosterRow>& prev, const std::vector<coopjoin::RosterRow>& now, int mySlot,
                           std::vector<int>* arrived, std::vector<int>* left)
{
    std::vector<int> a, b;
    InWorldOthers(prev, mySlot, &a);
    InWorldOthers(now, mySlot, &b);
    InWorldDiff(a, b, arrived, left);
}

struct Arrival { long seq; int slot; int via; Arrival() : seq(0), slot(-1), via(0) {} };

/* (2)+(3) THE ARRIVAL RECORD. linkState: 0 no link, 1 its arrival counted or merged, 2 held for its slot. */
struct Book
{
    std::vector<int> in;         /* other slots IN_WORLD on the last roster (empty while this game is not IN_WORLD itself) */
    std::vector<int> waitLink;   /* roster arrivals counted, not yet paired with a link-up */
    std::vector<int> held;       /* roster arrivals held: the link's own arrival counted with its slot not known yet */
    unsigned int heldSince, linkPendSince;
    int linkUp, linkSlot, lastLinkSlot, linkState;
    int linkEverUp;              /* 1 once BookLink has seen the old link come up in this process; a session left while the link was
                                    down moves the generation (downs) but never sets it */
    long linkGen, seq, downs;
    std::map<int, long> perSlot; /* arrivals counted per slot (the one-particular-player epoch) */
    Arrival ring[kRingCap];
    std::vector<Arrival> news;   /* arrivals counted since the plugin last drained them (its log lines) */
    long long seen, left, linkMerged, viaRoster, viaLink, bindTimeouts, linkDropped;
    long long lateBindSame;      /* [m11c2f1-3] a PEER_SLOT that came after a hold was decided named a slot whose roster arrival was
                                    counted on its own: two arrivals for one player (both already served - declared, not undone) */
    Book() : heldSince(0), linkPendSince(0), linkUp(0), linkSlot(-1), lastLinkSlot(-1), linkState(0), linkEverUp(0), linkGen(-1), seq(0), downs(0),
             seen(0), left(0), linkMerged(0), viaRoster(0), viaLink(0), bindTimeouts(0), linkDropped(0), lateBindSame(0) {}
};

inline void BookCount(Book* b, int slot, int via)
{
    ++b->seq; ++b->seen;
    if (via == kViaLink) ++b->viaLink; else ++b->viaRoster;
    Arrival a; a.seq = b->seq; a.slot = slot; a.via = via;
    b->ring[b->seq % kRingCap] = a;
    if (slot >= 0) ++b->perSlot[slot];
    b->news.push_back(a);
}
/* a roster arrival with no link to pair with yet: counted, and waiting to absorb the next link-up bound to its slot */
inline void BookCountRoster(Book* b, int s) { BookCount(b, s, kViaRoster); if (!SlotListHas(b->waitLink, s)) b->waitLink.push_back(s); }

/* every roster that decoded for this game's current world-server link. Returns the arrivals counted. */
inline int BookRoster(Book* b, const std::vector<coopjoin::RosterRow>& rows, int mySlot, unsigned int nowMs)
{
    if (mySlot < 0) return 0;   /* cannot tell its own row: no roster arrivals (the link-only behaviour) */
    const long before = b->seq;
    std::vector<int> nowIn, arrived, gone;
    InWorldOthers(rows, mySlot, &nowIn);
    InWorldDiff(b->in, nowIn, &arrived, &gone);
    const bool meOut = !MeInWorld(rows, mySlot);   /* this game left the world itself: the others did not leave it */
    b->in = nowIn;
    for (size_t i = 0; i < gone.size(); ++i)
    {
        SlotListDrop(&b->waitLink, gone[i]); SlotListDrop(&b->held, gone[i]);
        if (!(b->linkUp != 0 && b->linkSlot == gone[i]) && !meOut) ++b->left;   /* a slot the link still holds has not left */
    }
    for (size_t i = 0; i < arrived.size(); ++i)
    {
        const int s = arrived[i];
        if (b->linkUp != 0 && b->linkSlot == s) { ++b->linkMerged; continue; }   /* the link holds it: the same arrival */
        if (b->linkUp != 0 && b->linkSlot < 0 && b->linkState == 1)
        {   /* the link's arrival is counted but its slot is unknown: this may be the same player - held */
            if (b->held.empty()) b->heldSince = nowMs;
            if (!SlotListHas(b->held, s)) b->held.push_back(s);
            continue;
        }
        BookCountRoster(b, s);
    }
    return (int)(b->seq - before);
}
inline void BookLinkDownEdge(Book* b)
{
    ++b->downs;
    if (b->linkState == 2) ++b->linkDropped;   /* held for its slot and gone before it was known: a waiting roster arrival stood for it */
    /* [m11c2f1-3] the held roster arrivals are NOT counted at the down edge: each may be the player this link stood for (counted
       already). They stay held and resolve as any hold does: by the next link's slot (BookResolveHeld), after kBindWaitMs from
       heldSince (BookHeldTimeout - link up or down), or by leaving the roster (BookRoster). */
    if (b->linkSlot >= 0) { b->lastLinkSlot = b->linkSlot; if (!SlotListHas(b->in, b->linkSlot)) ++b->left; }
    b->linkUp = 0; b->linkSlot = -1; b->linkState = 0;
}
/* [m11c2f1-3] the held roster arrivals resolved by a link's known slot: that slot's is the link's own arrival (merged), the others
   are other players (counted, waiting for a link-up of their own) */
inline void BookResolveHeld(Book* b, int peerSlot)
{
    for (size_t i = 0; i < b->held.size(); ++i)
    {
        if (b->held[i] == peerSlot) ++b->linkMerged;
        else BookCountRoster(b, b->held[i]);
    }
    b->held.clear();
}
/* [m11c2f1-3] a hold whose slot never came: counted after kBindWaitMs, with the link up or down */
inline void BookHeldTimeout(Book* b, unsigned int nowMs)
{
    if (b->held.empty() || (unsigned int)(nowMs - b->heldSince) < kBindWaitMs) return;
    ++b->bindTimeouts;
    for (size_t i = 0; i < b->held.size(); ++i) BookCountRoster(b, b->held[i]);
    b->held.clear();
}
/* the old link, every main-thread tick: up = net::SessionLinked(), gen = net::SessionLinkGen(), peerSlot = LinkPeerSlot().
   Returns the arrivals counted. */
inline int BookLink(Book* b, int up, long gen, int peerSlot, unsigned int nowMs)
{
    const long before = b->seq;
    if (up == 0)
    {
        if (b->linkUp != 0) BookLinkDownEdge(b);
        else if (b->linkGen >= 0 && gen != b->linkGen) ++b->downs;   /* a session left with the link already down moves the generation too */
        b->linkGen = gen;
        BookHeldTimeout(b, nowMs);   /* [m11c2f1-3] holds kept through the down edge */
        return (int)(b->seq - before);
    }
    if (b->linkUp == 0 || gen != b->linkGen)
    {
        if (b->linkUp != 0) BookLinkDownEdge(b);   /* down and up again between two ticks */
        b->linkUp = 1; b->linkEverUp = 1; b->linkGen = gen; b->linkSlot = peerSlot >= 0 ? peerSlot : -1;
        if (b->linkSlot >= 0)
        {
            b->lastLinkSlot = b->linkSlot;
            if (SlotListDrop(&b->waitLink, b->linkSlot)) ++b->linkMerged; else BookCount(b, b->linkSlot, kViaLink);
            b->linkState = 1;
            BookResolveHeld(b, b->linkSlot);   /* [m11c2f1-3] holds left by the last link: this link's slot decides them */
        }
        else if (b->waitLink.empty()) { BookCount(b, -1, kViaLink); b->linkState = 1; }   /* today's timing: counted at the edge */
        else { b->linkState = 2; b->linkPendSince = nowMs; }   /* a roster arrival is waiting: this may be the same player */
    }
    else if (peerSlot >= 0 && b->linkSlot < 0)   /* the slot becomes known */
    {
        b->linkSlot = peerSlot; b->lastLinkSlot = peerSlot;
        const bool waited = SlotListDrop(&b->waitLink, peerSlot);
        if (b->linkState == 2) { if (waited) ++b->linkMerged; else BookCount(b, peerSlot, kViaLink); }
        else if (waited) ++b->lateBindSame;   /* [m11c2f1-3] the link was counted without its slot (a decided hold) and this slot's roster
                                                 arrival on its own: two arrivals for one player, both served by now - declared; the wait is
                                                 dropped so the next real link-up bound to this slot is not merged away */
        b->linkState = 1;
        BookResolveHeld(b, peerSlot);
    }
    else if (peerSlot >= 0 && peerSlot != b->linkSlot)
    {   /* PEER_SLOT changed: no arrival. [m11c2f1-3] a roster arrival of the NEW slot that waits is this link's from now on (merged, as
           if bound to it from the start) - left waiting it would merge away the next real link-up bound to that slot. The OLD slot has
           nothing waiting: a bound link absorbs its slot's roster arrivals (BookRoster) and its own up edge dropped that slot's wait. */
        b->linkSlot = peerSlot; b->lastLinkSlot = peerSlot;
        if (SlotListDrop(&b->waitLink, peerSlot)) ++b->linkMerged;
    }
    if (b->linkState == 2 && (unsigned int)(nowMs - b->linkPendSince) >= kBindWaitMs) { ++b->bindTimeouts; BookCount(b, -1, kViaLink); b->linkState = 1; }
    BookHeldTimeout(b, nowMs);
    return (int)(b->seq - before);
}
/* the "generation changed" sites' key: moves once per arrival and once per old-link down (or session left while down) */
inline long BookEpoch(const Book& b) { return b.seq + b.downs; }
/* (3) the arrivals since *cursor (then moved to now). slots (may be 0): each arrival's slot, -1 = the session peer with its slot not
   known, or "someone" when the cursor is older than the ring. Returns how many. */
inline int BookSince(const Book& b, long* cursor, std::vector<int>* slots)
{
    if (slots != 0) slots->clear();
    const long n = b.seq - *cursor;
    if (n <= 0) { *cursor = b.seq; return 0; }
    if (slots != 0)
    {
        long from = *cursor + 1;
        if (n > kRingCap) { slots->push_back(-1); from = b.seq - kRingCap + 1; }
        for (long k = from; k <= b.seq; ++k) slots->push_back(b.ring[k % kRingCap].slot);
    }
    *cursor = b.seq;
    return (int)n;
}
/* the one slot to address a serve to: >= 0 only when every arrival since the cursor was the same ROSTER arrival (a player that
   came through the world server, never the session peer) - else -1 = everyone, as today */
inline int ServeTarget(const Book& b, long cursorBefore)
{
    const long n = b.seq - cursorBefore;
    if (n != 1) return -1;
    const Arrival& a = b.ring[b.seq % kRingCap];
    return (a.via == kViaRoster && a.slot >= 0) ? a.slot : -1;
}
inline long BookSlotArrivals(const Book& b, int slot)
{
    std::map<int, long>::const_iterator it = b.perSlot.find(slot);
    return it == b.perSlot.end() ? 0 : it->second;
}

/* (4) ONE PARTICULAR PLAYER: 1 = the counterpart is the session peer and is judged by the old link exactly as before; 0 = it is
   another player, judged by the roster. isRelay/peerSlot: the counterpart's id is a relayed sender id carrying a slot. linkEverUp:
   Book::linkEverUp - the old link has really come up in this process (sessions left while it was down do not count). */
inline int PeerByLink(int isRelay, int peerSlot, int linkUp, int linkSlot, int lastLinkSlot, int linkEverUp)
{
    if (isRelay == 0 || peerSlot < 0) return 1;                   /* the raw link id: the session peer */
    if (linkUp != 0) return (linkSlot < 0 || peerSlot == linkSlot) ? 1 : 0;   /* slot unknown yet: the session peer, as before */
    if (peerSlot == lastLinkSlot) return 1;                       /* the link is down: its last peer, judged by the link as before */
    return (lastLinkSlot < 0 && linkEverUp != 0) ? 1 : 0;         /* it went down before its slot was known: as before; never a link: the roster */
}
/* that counterpart's epoch when judged by the roster: -1 - its arrival count (never equal to a session link generation, >= 0) */
inline long RosterPeerEpoch(const Book& b, int slot) { return -1 - BookSlotArrivals(b, slot); }
/* ONE KEY PER HOLD: a hold kept for one counterpart takes the way that player is judged (PeerByLink: byLink) once, when it is made,
   and compares that way's number from then on - the link's generation (linkGen) when judged by the link, its slot's roster epoch
   otherwise. A player judged the other way later (the link's slot became known as another player's, or a link came up bound to a
   roster player - one arrival, merged) is then not read as "left or came back" only because the two ways count differently.
   Two games: always the link's generation, as before. */
inline long PeerGenIn(const Book& b, int byLink, int slot, long linkGen) { return byLink != 0 ? linkGen : RosterPeerEpoch(b, slot); }
/* [m11c2f1-2] 1 = some other player IN_WORLD (others: InWorldOthers) is surely NOT the session peer. The session peer's slot is known
   when the old link is up and bound (linkSlot) or went down after binding (lastLinkSlot): every other in-world slot is beyond the
   link. While it is NOT known (the link never came up, is up before PEER_SLOT, or went down before it), any ONE in-world other may be
   the session peer: only a second one proves a player beyond the link. Two games: never 1 (the one other is the session peer). */
inline int OthersBeyondLink(const std::vector<int>& others, int linkUp, int linkSlot, int lastLinkSlot)
{
    const int peer = linkUp != 0 ? linkSlot : lastLinkSlot;
    if (peer < 0) return others.size() >= 2 ? 1 : 0;
    for (size_t i = 0; i < others.size(); ++i) if (others[i] != peer) return 1;
    return 0;
}
/* 1 = some other player IN_WORLD is not the old link's peer. linkEverUp: the old game-to-game link has come up at least once in this
   process. With no such link ever, nobody can be its peer, so every other player in the world counts - one is enough; once the link has
   been up, OthersBeyondLink decides. */
inline int OthersInWorldBeyondLink(const std::vector<int>& others, int linkUp, int linkSlot, int lastLinkSlot, int linkEverUp)
{
    if (linkEverUp == 0) return others.empty() ? 0 : 1;
    return OthersBeyondLink(others, linkUp, linkSlot, lastLinkSlot);
}

/* (6) REACHABLE: may a send meant for player `to` go now (-1 = any other game)? sessionUp: the old link; liveOk: this game's
   world-server link up and welcomed (its roster current) and this game in the world; others: the other IN_WORLD slots on that
   roster; linkSlot / lastLinkSlot / linkEverUp: the arrival record's view of the session peer (PeerByLink).
   Any other game: with the session link up always 1 - the old "linked" gate exactly; with it down, another player in the world
   that is surely not the session peer (OthersBeyondLink; with no session link ever, every other in the world counts - nobody can
   be the session peer). One player: the session peer (PeerByLink) by the old link alone, as before; any other by the world-server
   road. Two games: the one other game is the session peer, so this is the old link in every case. */
inline int Reachable(int to, int sessionUp, int liveOk, const std::vector<int>& others, int linkSlot, int lastLinkSlot, int linkEverUp)
{
    if (to < 0)
    {
        if (sessionUp != 0) return 1;
        if (liveOk == 0) return 0;
        if (linkEverUp == 0) return others.empty() ? 0 : 1;
        return OthersBeyondLink(others, 0, linkSlot, lastLinkSlot);
    }
    if (PeerByLink(1, to, sessionUp, linkSlot, lastLinkSlot, linkEverUp) != 0) return sessionUp != 0 ? 1 : 0;
    return (liveOk != 0 && SlotListHas(others, to)) ? 1 : 0;
}

/* (7) WHOM A ROUND OWED FOR ONE ARRIVAL GOES TO: `to` (ServeTarget's slot; -1 = every game) only when no round is owed or running
   already, the newcomer is surely not the session peer (PeerByLink - its slot not known yet reads as the session peer) and it is
   not the only other player in the world. In each of those cases a round to every game reaches it too, and a piece owed outside
   the round is then covered by the round and sent once (an addressed round leaves owed pieces to go again to every game). */
inline int RoundTarget(int to, int owedOrActive, int sessionUp, const std::vector<int>& others, int linkSlot, int lastLinkSlot, int linkEverUp)
{
    if (to < 0 || owedOrActive != 0) return -1;
    if (PeerByLink(1, to, sessionUp, linkSlot, lastLinkSlot, linkEverUp) != 0) return -1;
    if (others.size() == 1 && others[0] == to) return -1;
    return to;
}

}  // namespace mparrive

#endif
