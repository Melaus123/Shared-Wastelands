/* src/common/uidlayout.h - WHAT A UID IS MADE OF (M4, owner decision 203, 2026-09-29).
 *
 * A uid is the mod's u32 name for a synced character/object. It is (the MAKING game's 10-bit SEAT << 22) | a 22-bit counter.
 * OWNER DECISION 205 A (2026-09-29): the seat is the one the making game holds among the games connected to the notebook NOW
 * (0-1023: the lowest free seat with counters left when it links, freed when it unlinks - store_main.cpp SeatAssign / PeerGone),
 * NOT the permanent profile slot. The notebook keeps, per seat, the highest counter it ever handed out for the world's life and
 * grants a game blocks of counters of its seat (src/common/uidblock.h, uid_seats.txt), so a new player on a freed seat, and a
 * restarted game, continue above that seat's high-water: no uid ever repeats, and two connected games never hold one seat.
 * Before M4 the top 8 bits were folded from the process id, and two games could fold to the same prefix (pids 0x010203 and
 * 0x030201 both fold to 1) and then mint identical uids from counter 1 up; M4's first form keyed a per-process counter to the
 * profile slot, and a restarted game repeated its own earlier uids (review 2026-09-29 H1).
 *
 * THE UID IS ONLY A NAME. Which game may write a character is the hand-over record (net/session.cpp g_owner,
 * g_localOwned, RemoteMayWrite), never UidSlot(uid): a character handed to another game keeps the uid its maker
 * minted, and a seat changes hands. Nothing may read "UidSlot(uid) is my seat" as "mine to write" (owner 203).
 *
 * 0 means "unassigned" everywhere. Counter 0 is never granted, so UidMint never returns uid 0 - not even for seat 0.
 * 1,024 seats x 4,194,303 uids each for the world's life (measured mint rate 722-878 per hour: about 4,700 hours per seat,
 * less the unused rest of the blocks a crash or a link drop discards). At most 1,024 games hold a seat at once.
 *
 * The same rule as uidtable.h: no header, no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_UIDLAYOUT_H
#define COOP_COMMON_UIDLAYOUT_H

namespace coopuid {

const unsigned int UidSlotBits    = 10;
const unsigned int UidCounterBits = 22;
const unsigned int UidSlotMax     = 0x3FFu;      /* 1023: the highest slot a uid can carry */
const unsigned int UidCounterMax  = 0x3FFFFFu;   /* 4,194,303: the highest counter; counter 0 is never minted */

inline unsigned int UidMake(unsigned int slot, unsigned int n) { return ((slot & 0x3FFu) << 22) | (n & 0x3FFFFFu); }
inline unsigned int UidSlot(unsigned int uid)    { return uid >> 22; }
inline unsigned int UidCounter(unsigned int uid) { return uid & 0x3FFFFFu; }

/* Why the most recent UidMint (or counted probe) returned what it did. kUidNoBlock (M4 fold): the notebook has not given this game
   a seat and a block of counters on this link yet (store.cpp UidBlockTick asks; the caller retries). 2 is retired by owner 205 A:
   the notebook hands out seats 0-1023 only, so a seat a uid cannot carry does not exist. */
/* kUidStaleBlock (M4 fold 2, re-check H-A): the seat and blocks held were granted on an EARLIER notebook link - dropped at that
   mint, never used; the next ask on this link gets fresh ones. */
enum UidMintOutcome { kUidMinted = 0, kUidNoSlot = 1, kUidCounterExhausted = 3, kUidNoBlock = 4, kUidStaleBlock = 5 };

const unsigned int UidNoSeat = 0xFFFFFFFFu;
const unsigned int UidBlockAskNum = 3, UidBlockAskDen = 4;   /* ask for the next block once 3/4 of the current one is used */

/* One process's minting state. seat = the seat the blocks belong to - the one the notebook's last GRANT on this link named
   (UidNoSeat = none). count = the last counter minted (0 = none yet); minted = uids this process minted. The current block is
   [curLo, curHi] with curNext the next counter to mint (spent when curNext == 0 or curNext > curHi); one spare [nextLo, nextHi]
   (0 = none). grantedHi = the highest counter accepted for `seat` (a grant at or below it is stale and refused). exhausted = the
   notebook said EXHAUSTED. asked / askedAt = an ASK is out (GetTickCount ms); it is repeated after a retry interval. The counts
   are cumulative. last = the outcome of the most recent UidMint or counted probe.
   M4 fold 2: blockGen = the notebook link generation (store.cpp g_notebookLinkGen) the held seat and blocks were granted on;
   staleBlockRefused = mints refused because it differed (kUidStaleBlock), staleBlocksDropped = seats/blocks dropped for it.
   stretchOutcome = the refusal the adoption sweep has already counted in its current stretch (kUidMinted = none);
   sweepTicksRefused = every sweep tick that could not mint (UidSweepProbe). */
struct UidMinter
{
    unsigned int count, seat, curLo, curNext, curHi, nextLo, nextHi, grantedHi, askedAt;
    int          exhausted, asked;
    long long    minted, noSlotDeferred, counterExhausted, noBlockDeferred;
    long long    blockAsks, blocksTaken, blocksRefused;
    int          last;
    long         blockGen;
    long long    staleBlockRefused, staleBlocksDropped, sweepTicksRefused;
    int          stretchOutcome;
    UidMinter() : count(0), seat(UidNoSeat), curLo(0), curNext(0), curHi(0), nextLo(0), nextHi(0), grantedHi(0), askedAt(0),
                  exhausted(0), asked(0), minted(0), noSlotDeferred(0), counterExhausted(0), noBlockDeferred(0),
                  blockAsks(0), blocksTaken(0), blocksRefused(0), last(kUidMinted), blockGen(-1),
                  staleBlockRefused(0), staleBlocksDropped(0), sweepTicksRefused(0), stretchOutcome(kUidMinted) {}
};

inline bool UidCurLive(const UidMinter* m) { return m->curNext != 0 && m->curNext <= m->curHi; }

/* the current block is spent and a spare is held: the spare becomes current */
inline void UidPromote(UidMinter* m)
{
    if (UidCurLive(m) || m->nextLo == 0) return;
    m->curLo = m->nextLo; m->curNext = m->nextLo; m->curHi = m->nextHi; m->nextLo = 0; m->nextHi = 0;
}

/* The link went down: the seat and every block this game held are dropped. The notebook never grants those counters again, so
   they are wasted, never repeated. count, minted and the counts stay. */
inline void UidBlocksForget(UidMinter* m)
{
    m->seat = UidNoSeat; m->curLo = 0; m->curNext = 0; m->curHi = 0; m->nextLo = 0; m->nextHi = 0; m->grantedHi = 0;
    m->exhausted = 0; m->asked = 0; m->askedAt = 0;
}

/* A GRANT from the notebook: counters lo..hi of `seat`. 1 = taken (as the current block, or as the spare); 0 = refused and
   counted (malformed or a seat above 1023; stale - at or below a counter already granted; or both places full). A grant for
   another seat than the blocks held (the notebook moved this game off a spent seat) drops them first. */
inline int UidBlockGrant(UidMinter* m, unsigned int seat, unsigned int lo, unsigned int hi)
{
    if (seat > UidSlotMax || lo == 0 || lo > hi || hi > UidCounterMax) { ++m->blocksRefused; return 0; }
    if (m->seat != seat) { UidBlocksForget(m); m->seat = seat; }
    if (lo <= m->grantedHi) { ++m->blocksRefused; return 0; }   /* M4 fold 2: a refusal leaves the ask out - re-asked after the retry interval, never next frame */
    UidPromote(m);
    if (!UidCurLive(m))      { m->curLo = lo; m->curNext = lo; m->curHi = hi; }
    else if (m->nextLo == 0) { m->nextLo = lo; m->nextHi = hi; }
    else                     { ++m->blocksRefused; return 0; }
    m->grantedHi = hi; m->exhausted = 0; ++m->blocksTaken; m->asked = 0; m->stretchOutcome = kUidMinted;
    return 1;
}

/* EXHAUSTED from the notebook: this game's seat is spent and no free seat has counters left. */
inline void UidBlockNoMore(UidMinter* m, unsigned int seat)
{
    if (seat > UidSlotMax) return;
    if (m->seat != seat) { UidBlocksForget(m); m->seat = seat; }
    m->exhausted = 1; m->asked = 0;
}

/* Could a uid be minted right now? linked = this link's WELCOME stands (L2). The outcome UidMint would have; nothing counted. */
inline int UidMintProbe(UidMinter* m, bool linked)
{
    if (!linked) return kUidNoSlot;
    if (m->seat == UidNoSeat) return kUidNoBlock;
    UidPromote(m);
    if (UidCurLive(m)) return kUidMinted;
    if (m->exhausted) return kUidCounterExhausted;
    return kUidNoBlock;
}

inline void UidCountRefusal(UidMinter* m, int outcome)
{
    if (outcome == kUidNoSlot) ++m->noSlotDeferred;
    else if (outcome == kUidCounterExhausted) ++m->counterExhausted;
    else if (outcome == kUidNoBlock) ++m->noBlockDeferred;
    else if (outcome == kUidStaleBlock) ++m->staleBlockRefused;
    m->last = outcome;
}

/* THE ONE MINT, under the seat the notebook's GRANT named. Returns the new uid, or 0 = NOT MINTED, counted, and the caller
   treats it as "not now" and never uses it:
     not linked / not WELCOMEd on this link -> noSlotDeferred   (retried once the notebook's WELCOME has numbered this game)
     no seat or no block granted yet        -> noBlockDeferred  (retried once the notebook's GRANT arrives)
     seat spent and no free seat has room   -> counterExhausted (the notebook said EXHAUSTED: never wrapped, never repeated) */
inline unsigned int UidMintAfter(UidMinter* m, int o)
{
    if (o != kUidMinted) { UidCountRefusal(m, o); return 0; }
    const unsigned int n = m->curNext++;
    m->count = n; ++m->minted; m->last = kUidMinted;
    return UidMake(m->seat, n);
}
inline unsigned int UidMint(UidMinter* m, bool linked) { return UidMintAfter(m, UidMintProbe(m, linked)); }

/* M4 fold 2 (re-check H-A, defence): A SEAT AND ITS BLOCKS ARE BOUND TO THE NOTEBOOK LINK THEY ARRIVED ON. gen = the link
   generation now (store.cpp g_notebookLinkGen, bumped on both edges and by SetStoreServer's teardown). Held blocks of another
   generation are dropped (counted staleBlocksDropped); the link-down step drops them first, so this is the second wall. */
inline int UidLinkCheck(UidMinter* m, long gen)
{
    if (m->seat == UidNoSeat || m->blockGen == gen) return 0;
    ++m->staleBlocksDropped; UidBlocksForget(m);
    return 1;
}
/* a GRANT that arrived on link generation gen: blocks of an earlier link are dropped first, then UidBlockGrant decides */
inline int UidGrantOnLink(UidMinter* m, unsigned int seat, unsigned int lo, unsigned int hi, long gen)
{
    UidLinkCheck(m, gen);
    const int took = UidBlockGrant(m, seat, lo, hi);
    if (m->seat != UidNoSeat) m->blockGen = gen;
    return took;
}
inline void UidNoMoreOnLink(UidMinter* m, unsigned int seat, long gen)
{
    UidLinkCheck(m, gen);
    UidBlockNoMore(m, seat);
    if (m->seat != UidNoSeat) m->blockGen = gen;
}
/* the probe and the mint on link generation gen: a seat of another generation refuses this mint (kUidStaleBlock, counted) */
inline int UidMintProbeOnLink(UidMinter* m, bool linked, long gen)
{
    if (linked && UidLinkCheck(m, gen)) return kUidStaleBlock;
    return UidMintProbe(m, linked);
}
inline unsigned int UidMintOnLink(UidMinter* m, bool linked, long gen) { return UidMintAfter(m, UidMintProbeOnLink(m, linked, gen)); }

/* M4 fold 2 (re-check L-B): THE ADOPTION SWEEP'S ONCE-A-TICK QUESTION. A refusal is counted ONCE per stretch of the same
   reason - a stretch ends when a uid can be minted or a block is taken - and every refused tick is counted in
   sweepTicksRefused. *counted = 1 when this call counted (the caller logs only then). Returns the probe's outcome. */
inline int UidSweepProbe(UidMinter* m, bool linked, long gen, int* counted)
{
    *counted = 0;
    const int o = UidMintProbeOnLink(m, linked, gen);
    if (o == kUidMinted) { m->stretchOutcome = kUidMinted; return o; }
    ++m->sweepTicksRefused;
    if (o == kUidStaleBlock || o != m->stretchOutcome) { UidCountRefusal(m, o); *counted = 1; }
    m->stretchOutcome = o; m->last = o;
    return o;
}

/* Should this game ASK the notebook for a block now? Linked, not told EXHAUSTED, and: no seat/block yet, or 3/4 of the current
   block used and no spare - with no ask out, or the last one unanswered for retryMs. */
inline bool UidBlockWanted(UidMinter* m, bool linked, unsigned int nowMs, unsigned int retryMs)
{
    if (!linked || m->exhausted) return false;
    if (m->seat != UidNoSeat)
    {
        UidPromote(m);
        if (m->nextLo != 0) return false;
        if (UidCurLive(m))
        {
            const unsigned long long size = (unsigned long long)(m->curHi - m->curLo) + 1ull;
            const unsigned long long used = (unsigned long long)(m->curNext - m->curLo);
            if (used * UidBlockAskDen < size * UidBlockAskNum) return false;
        }
    }
    return !m->asked || (unsigned int)(nowMs - m->askedAt) >= retryMs;
}

inline void UidBlockAsked(UidMinter* m, unsigned int nowMs) { m->asked = 1; m->askedAt = nowMs; ++m->blockAsks; }
/* M4 fold 2: the same on link generation gen - a seat of an earlier link is dropped first, so the ask names no stale seat */
inline bool UidBlockWantedOnLink(UidMinter* m, bool linked, long gen, unsigned int nowMs, unsigned int retryMs)
{
    if (linked) UidLinkCheck(m, gen);
    return UidBlockWanted(m, linked, nowMs, retryMs);
}

/* M4 fold (defence in depth): a SPAWN from a peer whose uid names a character THIS game runs (net/session.cpp g_localOwned) is
   refused and counted (spawnUidOwnRefused). It can only be a repeated uid; taking it would write the peer over our character. */
enum { kSpawnUidAccept = 0, kSpawnUidOwnRefused = 1 };
inline int SpawnUidDecide(bool ownedHere) { return ownedHere ? kSpawnUidOwnRefused : kSpawnUidAccept; }

inline const char* UidMintWords(int outcome)
{
    switch (outcome)
    {
    case kUidNoSlot:           return "this game is not linked to a notebook (single player included), or the notebook has not WELCOMEd it on this link";
    case kUidNoBlock:          return "the notebook has not given this game a uid seat and a block of counters on this link yet (asked for; retried - a 1,025th connected game gets no seat)";
    case kUidCounterExhausted: return "this game's uid seat has handed out all 4,194,303 counters in this world's life and no free seat has any left (a counter never repeats)";
    case kUidStaleBlock:       return "the uid seat and blocks this game held were granted on an earlier notebook link - dropped, never used (asked for again on this link)";
    default:                   return "minted";
    }
}

}   /* namespace coopuid */

#endif
