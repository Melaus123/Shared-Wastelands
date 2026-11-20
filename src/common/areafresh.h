/* src/common/areafresh.h - T-1129 (owner decision 1215 a): A FRESH AREA FILE FROM THE GAME RUNNING AN AREA, BEFORE ANOTHER GAME
 * LOADS THAT AREA (world-server message 89 AREA_FRESH; 88 until part 3). Pure: the world server (src/coop-store/store_main.cpp), the game
 * (src/coop-plugin/store.cpp) and the offline suite (src/coop-test) compile this same header.
 *
 * WHY. A game entering an area another game runs (that area's holder) loaded a stale copy of it - its own save, or an older world
 * copy - and the catch-up then added what was already there: items on the ground doubled in place. Now the entering game holds that
 * area's file read (ZoneMapContent::_activate 0x9FEC00 - the hold Confirmed survivable for 10 s and 30 s in T1698) until the holder
 * has written the area afresh and the world server has the new record; the existing area-file swap then loads that copy.
 *
 * WIRE: u8 kind, u8 why, two zero bytes, i32 x, i32 y, i64 stamp - kMsgSize bytes, nothing after.
 *   up   1 ASK    entering game -> world server: x, y; stamp = writtenAt of the world copy it would load now (0 = none). Sent at
 *                 "load decided" (or the area's first file read) and again after every link-up while it waits.
 *   down 2 WRITE  world server -> the area's holder: write x, y now (why 0, stamp 0).
 *   up   3 CANT   holder -> world server: it cannot write the area (why); passed on to every game waiting on that area.
 *                 Part 3: also for every queued WRITE the holder drops when its world stops (why dropped).
 *   down 3 CANT   world server -> a waiting game: no fresh copy is coming (why) - it loads the newest copy it has, as before.
 *   up   4 FRESH  holder -> world server: written; stamp = the LOWEST stamp the new record can carry (zonerec::ZoneStampNext taken just
 *                 before the write: every record of the area this holder sent before carries less).
 *   down 4 FRESH  world server -> a waiting game: the area's record with writtenAt >= the holder's stamp has been stored and forwarded;
 *                 stamp = its writtenAt. The wait ends when the game's swap entry for the area carries writtenAt >= stamp.
 * The game-to-game protocol does not move: this message rides only the world-server link.
 * T-1279 (design B; owner decision 1580 a "records win", 1581 a: an area is entered only once its authoritative record is in) -
 * WORLD-SERVER PROTOCOL 149. An entering game now waits for EVERY area it loads in a running world (WaitDecide kWaitRecord), not
 * only one another live game holds; the world server picks the road from its own area table: a live holder that is not the asker
 * -> the WRITE round above; otherwise (nobody holds it, the holder is not in the world, or the asker holds it) it ANSWERS FROM ITS
 * RECORDS (ServerRecordAnswer): the area's record, when newer than the ASK's stamp, goes to the asker (a RECORD on the same
 * reliable channel) followed by FRESH(its writtenAt); else CANT why=current ("yours is current"). A world server of 148 would
 * answer such an ASK CANT noHolder and the game would load an older copy, so the pair SHIPS TOGETHER. The wait has no time bound
 * (the 60 s give-up is gone): it ends on the record, on a CANT, or when no world server can ever answer this game.
 * T-1429 s2 (owner decision 1788 "every game takes everything from the server"; design D3 s2, finding F1310) - WORLD-SERVER PROTOCOL
 * 165. THE CREATION LEASE: an area the world server holds no copy of is created by ONE game, and every other game loads that copy. The
 * established practice of an authoritative store: the first asker gets a creation lease, creates and publishes; every other asker
 * waits for the published copy; the lease ends when its holder's copy lands, or when its holder leaves, says it cannot, or the lease
 * leaves (no age since T-1429 fold 8c) - then the next waiter is granted it. Before 165 an ASK for an area with no record was answered CANT current
 * and every asker loaded its own save's copy, each rolled differently (lock levels), the first upload coming at its next save.
 *   down 5 LEASE  world server -> asker: "you create it" - no copy here and no lease out (ServerRecordOrLease kRecLease /
 *                 ServerClaimDecide kClaimGrant); the game loads its own save's copy and writes the area AT ONCE after the read
 *                 (the WRITE road's own write, gated by LeaseWriteGate), answering FRESH (the copy is coming) or CANT (it cannot: the
 *                 lease ends).
 *   down 6 WAIT   world server -> asker: another game holds the lease (ServerLeaseOnAsk kLsWait); the asker's read stays held as a record
 *                 wait and is answered FRESH (behind the copy) when the lease holder's copy lands; its 10 s re-ask is answered WAIT again.
 *   up   1 ASK why=loadedOwn: a CLAIM - this game's area read ran during a world load (a read is never held at a load screen: the hold
 *                 is not proven safe there - part 3 (5b) - and AreaFreshTick drops every wait once the world has stopped 2 s), so it
 *                 loaded its own copy (stamp = that copy's writtenAt, 0 = its own save's file). ServerClaimDecide: no copy and no lease
 *                 -> LEASE (its loaded copy is uploaded at once and becomes the first); it holds the lease already -> LEASE again; the
 *                 world's copy is the one it loaded, or older -> CANT current; another game holds the lease or the world's copy is
 *                 newer -> CANT notWorlds: the game says so in an error line and the world server refuses this connection's area
 *                 records of that area (LeaseRecordJudge kLrRefuseDiverged) until it asks for the area again (a new read).
 * The world server keeps the lease holder's copy as the first: while a lease is out, an area record from any other game is refused
 * (LeaseRecordJudge kLrRefuseLease), and the lease holder's is taken whoever the area table names as holder (kLrTakeLease). */
#ifndef COOP_COMMON_AREAFRESH_H
#define COOP_COMMON_AREAFRESH_H

#include <cstddef>
#include <vector>

namespace areafresh {

const unsigned int kMsgAreaFresh = 89u;   /* 28-87 are taken on the world-server link (store_main.cpp's enum and constants); 88 is CHUNK on effort/t1118f / t1118g (part 3) */
const unsigned int kProtocol = 170u;      /* the world-server protocol that carries AREA_FRESH (127 since part 3 moved it to 89; 149: T-1279 - an ASK no live holder can answer is answered from the world server's records; 165-168 (T-1429 on its branch, never reused): 165: T-1429 s2 - LEASE / WAIT and the world-load claim; 166: s2 fold 1 - the ASK why=settle before the world load; 167: s2 fold 2 - the settle ask carries the whole ordered area list, and why worldFirst; 168: s2 fold 3 - an area RECORD carries its base (the stored copy's seq it was made from) and a stale one is answered CANT staleBase; 169: the merge of those with main's 165, T-1427 - a piece record may carry the builder's town; 170: the merge of 169 with effort/t1408a's wall group facts in a piece record's STATE, T-1408) */



enum { kAsk = 1, kWrite = 2, kCant = 3, kFresh = 4, kLease = 5, kWait = 6 };   /* T-1429 s2: 5 / 6 down only */
const size_t kMsgSize = 20;

/* why (a CANT's reason; kWhyNone = no reason - a WRITE / FRESH, or the world server's "write it") */
enum { kWhyNone = 0, kWhyNoHolder = 1, kWhyAskerHolds = 2, kWhyHolderNotLive = 3, kWhyNotHolder = 4, kWhyNotLoaded = 5,
       kWhyNoHook = 6, kWhyFault = 7, kWhyNotPublished = 8, kWhyBusy = 9, kWhyServerWrite = 10, kWhyNoWorld = 11,
       kWhySendFailed = 12, kWhyHolderGone = 13, kWhyDropped = 14, kWhyCurrent = 15, kWhyLease = 16, kWhyNotWorlds = 17, kWhyLoadedOwn = 18,
       kWhySettle = 19, kWhyWorldFirst = 20, kWhyStaleBase = 21, kWhyCount = 22 };   /* s2 fold 3 (review F1372): staleBase - down only, to the sender of an area record made from an older stored copy than the one held (ServerBaseJudge) */   /* s2 fold 2 (review F1364 C): worldFirst - the brand-new world's one reserved lease (x 0, y 0 carry no area): ASK / LEASE / WAIT / FRESH down, FRESH (its start areas are up) / CANT (given back) up */   /* s2 fold 1: settle - an ASK made before the world load (the pre-load), answered with the decision only (FRESH / CANT current / LEASE / WAIT) */   /* T-1429 s2: lease - a wait ended by this game's LEASE (game-side only, never sent); notWorlds - a claimer's loaded copy is not the world's; loadedOwn - an ASK that is a world-load claim */   /* part 3: dropped - a queued WRITE the holder's stopped world dropped; T-1279: current - the world server holds no newer record than the asker's */
inline const char* WhyName(unsigned int w)
{
    static const char* const kNames[kWhyCount] = { "none", "noHolder", "askerHolds", "holderNotLive", "notHolder", "notLoaded", "noHook",
                                                   "fault", "notPublished", "busy", "serverWrite", "noWorld", "sendFailed", "holderGone", "dropped", "current", "lease", "notWorlds", "loadedOwn", "settle", "worldFirst", "staleBase" };
    return w < (unsigned int)kWhyCount ? kNames[w] : "unknown";
}

struct Msg { unsigned int kind, why; int x, y; long long stamp; Msg() : kind(0u), why(0u), x(-1), y(-1), stamp(0) {} };

inline bool OnGrid(int x, int y) { return x >= 0 && x < 64 && y >= 0 && y < 64; }
/* one area's row in a 4096-row table: x * 64 + y; -1 off the grid */
inline int Key(int x, int y) { return OnGrid(x, y) ? x * 64 + y : -1; }
inline int KeyX(int k) { return k / 64; }
inline int KeyY(int k) { return k % 64; }

inline void Put32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline unsigned int Get32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16) | ((unsigned int)(unsigned char)p[3] << 24);
}
/* false (nothing appended) for an unknown kind, a why out of range, a square off the grid or a negative stamp */
inline bool Encode(std::vector<char>* b, unsigned int kind, unsigned int why, int x, int y, long long stamp)
{
    if (kind < (unsigned int)kAsk || kind > (unsigned int)kWait || why >= (unsigned int)kWhyCount || !OnGrid(x, y) || stamp < 0) return false;
    b->push_back((char)kind); b->push_back((char)why); b->push_back(0); b->push_back(0);
    Put32(b, (unsigned int)x); Put32(b, (unsigned int)y);
    const unsigned long long s = (unsigned long long)stamp;
    Put32(b, (unsigned int)(s & 0xFFFFFFFFull)); Put32(b, (unsigned int)(s >> 32));
    return true;
}
inline bool Decode(const char* p, size_t n, Msg* m)
{
    if (p == 0 || n != kMsgSize || p[2] != 0 || p[3] != 0) return false;
    Msg d;
    d.kind = (unsigned int)(unsigned char)p[0]; d.why = (unsigned int)(unsigned char)p[1];
    d.x = (int)Get32(p + 4); d.y = (int)Get32(p + 8);
    const unsigned long long s = (unsigned long long)Get32(p + 12) | ((unsigned long long)Get32(p + 16) << 32);
    d.stamp = (long long)s;
    if (d.kind < (unsigned int)kAsk || d.kind > (unsigned int)kWait || d.why >= (unsigned int)kWhyCount || !OnGrid(d.x, d.y) || d.stamp < 0) return false;
    *m = d;
    return true;
}

/* WHO WAITS - the entering game, at "load decided" for an area (or that area's first file read). holder: the slot this game's area
   map names (>= 0), -1 unknown or nobody, or frozenCode (the map's "holder is a player not in the world"). kWaitYes = hold the read
   and ask; anything else = no wait (today's behaviour), the reason. */
enum { kWaitYes = 0, kNoWaitOff = 1, kNoWaitSingle = 2, kNoWaitNoWorld = 3, kNoWaitNoHook = 4, kNoWaitLinkDown = 5,
       kNoWaitSlotUnknown = 6, kNoWaitNoHolder = 7, kNoWaitHolderSelf = 8, kNoWaitHolderAway = 9, kWaitMapUnknown = 10, kNoWaitAlone = 11,
       kWaitRecord = 12 };   /* T-1279: 6, 7, 9, 10 and 11 are no longer answered (kept: their numbers) - each of those cases is kWaitRecord */
/* part 3: mapFresh = this game's area map is no older than 5 s (zones.cpp AreaHolderSlotAgeTS). A stale or missing map is NOT "nobody
   holds": kWaitMapUnknown holds the read (no ASK) and decides again at each retry, within the read's shared 60 s (counted).
   othersInWorld: the world server's player list shows another slot in the world (1), none (0), or no list on this link (-1). Only a game
   in the world can hold an area, so with nobody else in it there is nothing to wait for - and an empty area map (nobody reports any
   area) never counts as fresh, so a lone game would otherwise hold every read for the whole 60 s. -1 keeps the map wait. */
inline int WaitDecide(int modOn, int single, int worldRunning, int hooked, int linkUp, int mySlot, int holder, int frozenCode, int mapFresh, int othersInWorld)
{
    if (modOn == 0) return kNoWaitOff;
    if (single != 0) return kNoWaitSingle;
    if (worldRunning == 0) return kNoWaitNoWorld;
    if (hooked == 0) return kNoWaitNoHook;
    if (linkUp == 0) return kNoWaitLinkDown;   /* before the map: the map only comes over the link, so with it down every map goes stale */
    /* T-1279 (design B): the read waits for the world's record of EVERY area - the world server picks the road from its own area
       table (a live holder writes it; otherwise its records answer). Only a fresh map naming THIS game as the holder skips the wait
       (the area is in its charge); kWaitYes = a fresh map names another game in the world (that holder is waited on as before, and
       its leaving ends the wait); kWaitRecord = nobody, a holder not in the world, nobody else in the world, or the map is stale /
       missing / this game's slot unknown - no holder is waited on, only the world server's answer */
    if (mapFresh != 0 && mySlot >= 0 && holder == mySlot) return kNoWaitHolderSelf;
    if (othersInWorld != 0 && mapFresh != 0 && mySlot >= 0 && holder >= 0 && holder != frozenCode) return kWaitYes;
    return kWaitRecord;
}

/* T-1429 s2 fold 5 (review F1378 SHOULD): AN AREA MARKED staleBase ALWAYS ASKS THE WORLD SERVER AT ITS NEXT READ - even when this game's
   fresh map names it the holder (kNoWaitHolderSelf would load this game's own copy, the one the world server refused). Every other
   verdict stands (single, no world, no hook, link down: nothing can be asked). */
inline int StaleBaseWait(int verdict, int staleBase) { return (staleBase != 0 && verdict == kNoWaitHolderSelf) ? kWaitRecord : verdict; }
/* ... and its read is decided again (the no-wait a load decision left on the row does not stand for it) */
inline int StaleBaseDecided(int decided, int staleBase) { return staleBase != 0 ? 0 : decided; }
/* THE AREA SWAP WHILE staleBase: this game's mirror copy is served only once it holds a copy the world server sent after the mark (its
   mirror version known and not 0 for the stamp held); until then nothing is served from the mirror, so the held read takes the world's
   copy when it lands. 1 = serve. */
inline int StaleSwapServes(int staleBase, int mirrorKnown, long long mirrorStamp, unsigned long long mirrorSeq, long long heldStamp)
{
    if (staleBase == 0) return 1;
    return (mirrorKnown != 0 && mirrorStamp == heldStamp && heldStamp > 0 && mirrorSeq != 0ULL) ? 1 : 0;
}

/* T-1365: THE "NO WAIT" MARK A LOAD DECISION LEAVES on its area's row (store.cpp AfRow.decided) until the area's read: none (the
   read decides), no wait, or no wait ONLY because the world was still loading (kNoWaitNoWorld - engine writes blocked at "load
   decided"). */
enum { kDecidedNone = 0, kDecidedNoWait = 1, kDecidedNoWorld = 2 };
inline int DecidedMark(int verdict) { return verdict == kNoWaitNoWorld ? kDecidedNoWorld : kDecidedNoWait; }
/* AT THE AREA'S READ (main thread, the row not held for the area map or a wait): kReadDecideFirst = nothing decided yet - decide now,
   as before; kReadDecideAgain = the no-wait was decided at "load decided" while the world was still loading and engine writes are
   allowed now: the read runs in a live world, so it is decided again there (WaitDecide's own rule: a read with no current record
   waits for the world server's answer) - the pre-load's ring does not cover every such read (a NEW GAME, an area outside a load's
   list); kReadDecideNone = keep the decision. */
enum { kReadDecideNone = 0, kReadDecideFirst = 1, kReadDecideAgain = 2 };
/* T-1365 fold (review M1): kReadDecideAgain is acted on only where the pre-load did not cover the read - this area has no record here
   (haveRecord 0), or this load's fetch did not cover it (coveredByFetch 0: no exchange let this load go as done, or the area lies outside
   its list plus kPreRing), or the world is a NEW GAME's (newGameWorld 1: its fetch is every area record or its start town's ring, never a
   returning player's list). A load whose records the pre-load just fetched keeps its no-wait - no round trip per area. */
inline int RedecideAtLiveRead(int haveRecord, int coveredByFetch, int newGameWorld) { return (haveRecord == 0 || coveredByFetch == 0 || newGameWorld != 0) ? 1 : 0; }
inline int DecideAtRead(int state, int decided, int writesAllowedNow)
{
    if (state != 0) return kReadDecideNone;
    if (decided == kDecidedNone) return kReadDecideFirst;
    return (decided == kDecidedNoWorld && writesAllowedNow != 0) ? kReadDecideAgain : kReadDecideNone;
}

/* WHEN THE WAIT ENDS. freshStamp: the FRESH's stamp (-1 = none yet); haveSwap / swapStamp: this game's swap entry for the area now;
   cant: a CANT came; holderGoneSaid: PLAYER_GONE named the asked holder; holderNow: the map's holder now (as WaitDecide). A map
   with no answer (-1: no fresh map - the link is down, or the area is not listed) keeps the wait: the link-up asks again.
   T-1279: recordWait = a kWaitRecord wait (no holder was asked for): the holder facts do not end it - only the record or a CANT. */
enum { kKeepWaiting = 0, kEndFresh = 1, kEndCant = 2, kEndHolderGone = 3 };
inline int WaitEndDecide(long long freshStamp, int haveSwap, long long swapStamp, int cant, int holderGoneSaid, int askedHolder, int holderNow, int frozenCode, int recordWait)
{
    if (freshStamp >= 0 && haveSwap != 0 && swapStamp >= freshStamp) return kEndFresh;   /* the fresh copy is in: the swap loads it */
    if (cant != 0) return kEndCant;
    if (recordWait != 0) return kKeepWaiting;
    if (holderGoneSaid != 0 || holderNow == frozenCode || (holderNow >= 0 && holderNow != askedHolder)) return kEndHolderGone;
    return kKeepWaiting;
}
/* the ASK goes (again) when the link is up and it has not gone on this link (askedGen = the link generation it went on, -1 none) */
inline int AskDue(int linkUp, long askedGen, long linkGen) { return (linkUp != 0 && askedGen != linkGen) ? 1 : 0; }

/* THE WORLD SERVER'S ANSWER TO AN ASK: kWhyNone = send WRITE to the holder; otherwise the CANT's why */
inline unsigned int ServerAskDecide(int holder, int asker, int holderLive)
{
    if (holder < 0) return (unsigned int)kWhyNoHolder;
    if (holder == asker) return (unsigned int)kWhyAskerHolds;
    if (holderLive == 0) return (unsigned int)kWhyHolderNotLive;
    return (unsigned int)kWhyNone;
}
/* T-1279 (design B): THE WORLD SERVER'S ANSWER FROM ITS RECORDS - for an ASK ServerAskDecide does not send to a holder (any why but
   kWhyNone: nobody holds the area, its holder is not in the world, or the asker holds it). haveRecord = it holds the area's record
   with its file; recStamp = that record's writtenAt; askerStamp = the ASK's stamp (the writtenAt of the copy the asker would load, 0 =
   none). kRecSend = the record goes to the asker, then FRESH(recStamp); kRecCurrent = CANT current ("yours is current") */
enum { kRecSend = 0, kRecCurrent = 1 };
inline int ServerRecordAnswer(int haveRecord, long long recStamp, long long askerStamp) { return (haveRecord != 0 && recStamp > askerStamp) ? (int)kRecSend : (int)kRecCurrent; }
/* the world server: a stored record answers the write when the holder has said FRESH (holderStamp > 0) and the record is that new */
inline int ServerLanded(long long holderStamp, long long landedStamp) { return (holderStamp > 0 && landedStamp >= holderStamp) ? 1 : 0; }

/* THE HOLDER'S SAFE-POINT GATE for a WRITE. kRetryLater = not now (an engine save, a load or a world-copy read in flight) - asked
   again next frame, until the caller's limit; kWhyNone = write now; otherwise the CANT's why. */
const unsigned int kRetryLater = 100u;
inline unsigned int HolderGate(int worldRunning, int busy, int mySlot, int holderNow, int loaded, int hooked)
{
    if (worldRunning == 0) return (unsigned int)kWhyNoWorld;
    if (busy != 0) return kRetryLater;
    if (mySlot < 0 || holderNow != mySlot) return (unsigned int)kWhyNotHolder;
    if (loaded == 0) return (unsigned int)kWhyNotLoaded;
    if (hooked == 0) return (unsigned int)kWhyNoHook;
    return (unsigned int)kWhyNone;
}
/* after the engine's one-area save: called = 1 returned, else faulted; the publish road's counters moved by writtenDelta (sent now)
   or parkedDelta (sent when a load of that mirror file ends) - either way a record comes */
inline unsigned int HolderWriteWhy(int called, long long writtenDelta, long long parkedDelta)
{
    if (called != 1) return (unsigned int)kWhyFault;
    if (writtenDelta > 0 || parkedDelta > 0) return (unsigned int)kWhyNone;
    return (unsigned int)kWhyNotPublished;
}

/* T-1129 PART 2 - RE-ASK (manager decision, 2026-10-31). While a wait has no CANT the ASK goes again every kReaskMs on a link that
   is up (beside the ask at each link-up, AskDue). T-1279 (owner decision 1580 a): THE 60 s TIME-OUT IS GONE - a wait no longer ends
   with this game's own copy because time passed (kEndTimeout is no longer answered; kept: the report's column). It ends on the
   record (FRESH), a CANT, the asked holder gone (a kWaitYes wait), or kEndNoServer: no world server can ever answer this game (none
   set, this game's protocol refused, another world named, the join refused - the same facts as the pre-load's) - then the newest
   copy here loads, as the disconnect handling has it. While the link is down and a world server may still answer, the wait stays
   (the connection-trouble steps cover the player) and asks again at the next link-up. Several ends due in one frame: an answer wins,
   in WaitEndDecide's own order, then no server. kWaitLimitMs stays for the swap's hold and the area-map hold (FallbackHoldDecide,
   MapHoldDecide - the map hold is no longer entered: WaitDecide no longer answers kWaitMapUnknown) and a row whose read never came. */
const long long kReaskMs = 10000;
const long long kWaitLimitMs = 60000;
enum { kEndTimeout = 4, kEndNoServer = 5 };
inline int WaitEndOrNoServer(long long freshStamp, int haveSwap, long long swapStamp, int cant, int holderGoneSaid, int askedHolder, int holderNow, int frozenCode, int recordWait, int canServe)
{
    const int e = WaitEndDecide(freshStamp, haveSwap, swapStamp, cant, holderGoneSaid, askedHolder, holderNow, frozenCode, recordWait);
    if (e != kKeepWaiting) return e;
    return canServe == 0 ? (int)kEndNoServer : (int)kKeepWaiting;
}
/* which ASK goes this frame (the link generation's ask wins when both are due): kAskLink = the first on this link (AskDue);
   kAskAgain = kReaskMs since the last ask was tried, the link up. None once a CANT is noted (haveCant: the next read loads the copy
   here). T-1279 (design principle 4 - re-armed until done): a FRESH noted no longer stops the asks - with no time bound, a FRESH
   whose record never reaches the swap would hold the read for ever; the 10 s re-ask asks again (the world server's records road sends
   the record again; a young answered WRITE round tells its FRESH again - FreshKeep keeps the lowest stamp) */
enum { kAskNone = 0, kAskLink = 1, kAskAgain = 2 };
/* KEPT (T-1429 fold 8d): the live read's 10 s re-ask covers a FRESH whose record never reaches the swap (the read would hold for ever) -
   no message says that record was dropped on its way in */
inline int AskPlan(int linkUp, long askedGen, long linkGen, long long sinceAskMs, int haveCant)
{
    if (haveCant != 0) return kAskNone;
    if (AskDue(linkUp, askedGen, linkGen) != 0) return kAskLink;
    if (linkUp != 0 && sinceAskMs >= kReaskMs) return kAskAgain;
    return kAskNone;
}

/* T-1279 fold 2 (re-check B; owner decision 1580 a): A CANT, OR THE ASKED HOLDER GONE, WHILE A WORLD SERVER CAN ANSWER. CANT current
   ("nothing newer than yours") and CANT fault (the holder's save faulted, or this read cannot be held) end the wait: the copy here
   loads. Any other why (the holder teleported away - notLoaded; not the holder now; busy; dropped; sendFailed; ...) and the asked
   holder gone (kWhyHolderGone) do not while canServe: the row becomes a record wait (kWaitRecord's road - no holder asked for; the
   world server picks the road from its own area table) and asks again - at once the first time (kCantRecordAskNow), by the 10 s
   re-ask after that (kCantRecordReask: a world server whose area table still names a holder that answers CANT is not asked in a tight
   loop). turnedBefore = the times this row was turned already. With no world server that can answer, every CANT ends the wait. */
enum { kCantEnd = 0, kCantRecordAskNow = 1, kCantRecordReask = 2 };
inline int CantDecide(unsigned int why, int canServe, int turnedBefore)
{
    if (canServe == 0 || why == (unsigned int)kWhyCurrent || why == (unsigned int)kWhyFault) return kCantEnd;
    return turnedBefore == 0 ? (int)kCantRecordAskNow : (int)kCantRecordReask;
}
/* T-1279 fold 2 (re-check F): NO ENDLESS SILENT HOLD. (1) FreshPublishDue: a FRESH whose record this game already holds (the record
   here as new as the FRESH's stamp, with its file) while its swap entry is missing or older - a kept key held back unpublished, whose
   world-server copy was then dropped as not newer - publishes that record at the FRESH. (2) FreshUnmetNote: a FRESH told while the
   one kept before it (keptBefore >= 0) is still not met by the swap is an unmet FRESH; at the kFreshUnmetSay-th the game says an
   error line once - the hold goes on (no fallback). (3) ServerRecordFreshDue: the world server sends no FRESH after a record whose
   file it could not read (it went with its position only), so a FRESH always names a record the game can load. */
inline int FreshPublishDue(int haveRec, long long recStamp, int haveSwap, long long swapStamp, long long freshStamp)
{
    return (haveRec != 0 && recStamp >= freshStamp && (haveSwap == 0 || swapStamp < freshStamp)) ? 1 : 0;
}
const int kFreshUnmetSay = 3;
inline int FreshUnmetNote(long long keptBefore, int haveSwap, long long swapStamp) { return (keptBefore >= 0 && (haveSwap == 0 || swapStamp < keptBefore)) ? 1 : 0; }
inline int ServerRecordFreshDue(int taken, int fileRead) { return (taken != 0 && fileRead != 0) ? 1 : 0; }

/* T-1129 PART 2 - THE AREA-FILE SWAP'S REFUSALS, ASKED BEFORE THE READ. The swap (the game's detour_sfsReadFile) loads the world's
   copy of an area in place of this game's own file, and on any refusal quietly loads this game's own older file - the doubling road
   T-1129 is about. The game asks the same checks of the same entry on the main thread BEFORE the read, in the swap's own order:
   gateOn = the mod and the area swap on, not a lone game; haveRec / fileNamed = a swap entry naming a file (else nothing to swap);
   fileThere = that file on disk; translate = another game's (or an unknown writer's) record, whose owners are translated at the load;
   standInReady = the writer's stand-in (ZoneStandInReady); ownersReady = every owner the file names has one (ZoneOwnersReady);
   legacyReady = a protocol-67 owner can be kept (ZoneLegacyPeerReady). */
enum { kSwapNothing = 0, kSwapGo = 1, kSwapNoFile = 2, kSwapNoStandIn = 3, kSwapOwnersNotReady = 4, kSwapLegacyNotReady = 5, kSwapWriterUnknown = 6, kSwapStandInCapped = 7, kSwapPiecesNotReady = 8, kSwapCount = 9 };
inline const char* SwapCheckName(int c)
{
    static const char* const kNames[kSwapCount] = { "nothingToSwap", "go", "noFile", "noStandIn", "ownersNotReady", "legacyNotReady", "writerUnknown", "standInCapped", "piecesNotReady" };
    return (c >= 0 && c < (int)kSwapCount) ? kNames[c] : "unknown";
}
inline int IsSwapRefusal(int c) { return (c >= (int)kSwapNoFile && c < (int)kSwapCount) ? 1 : 0; }
/* T-1443 (F1379 design a): A HOLD ONLY WHEN IT CAN END. writerKnown = the record's writer is a player number (its owner field, or the
   one other game on a direct session link that is up - coopslot::ZoneWriterOf); namesPlayerSid = the record's file names the shared
   player-faction id (coopslot::ZoneFileNamesSid - the writer's own buildings, whose owner only the writer's number can translate; 1
   too when the file could not be read). An unknown writer is its own outcome, kSwapWriterUnknown, which is never held (no event
   could ever make it known); a file that names no player-faction building of its writer needs no writer, so the writer's stand-in is
   not asked and the swap goes on to the named owners and the protocol-67 check. */
inline int SwapPrecheckW(int gateOn, int haveRec, int fileNamed, int fileThere, int translate, int writerKnown, int namesPlayerSid, int standInReady, int ownersReady, int legacyReady)
{
    if (gateOn == 0 || haveRec == 0 || fileNamed == 0) return kSwapNothing;
    if (fileThere == 0) return kSwapNoFile;
    if (translate == 0) return kSwapGo;
    if (writerKnown == 0) { if (namesPlayerSid != 0) return kSwapWriterUnknown; }
    else if (standInReady == 0) return kSwapNoStandIn;
    if (ownersReady == 0) return kSwapOwnersNotReady;
    if (legacyReady == 0) return kSwapLegacyNotReady;
    return kSwapGo;
}
inline int SwapPrecheck(int gateOn, int haveRec, int fileNamed, int fileThere, int translate, int standInReady, int ownersReady, int legacyReady)
{
    return SwapPrecheckW(gateOn, haveRec, fileNamed, fileThere, translate, 1, 1, standInReady, ownersReady, legacyReady);
}
/* T-1443: whether a held check can ever pass - noStandIn only for a known writer (its stand-in is made by an event: the drain's
   stand-in pass, the player's SPAWN); writerUnknown never. The hold line prints it (canBecomeTrue=). */
inline int HoldCanEnd(int check, int writerKnown, int capFull)
{
    if (check == kSwapWriterUnknown || check == kSwapStandInCapped) return 0;
    if (check == kSwapNoStandIn) return (writerKnown != 0 && capFull == 0) ? 1 : 0;
    if (check == kSwapOwnersNotReady) return capFull == 0 ? 1 : 0;
    return 1;
}
/* T-1443 fold 1 (F1382 SHOULD 3): a missing stand-in (missing: the writer's, or a named player's) while the stand-in pass may make no
   more placeholders in this world (capFull: kStandInTableCap / 2 made) is never made by the pass - only that player's own arrival
   could make it - so that read is not held: kSwapStandInCapped, handled as the decision-1804 case (the swap refuses; the copy here loads) */
/* T-1443 fold 2 (owner 1804 a): an unknown writer's file naming player-faction buildings takes each building's owner from its own
   piece record (pieceowner.h); while the area is not DONE on this link (its piece records not all here) the read is held as
   kSwapPiecesNotReady - the area's AREA_DONE ends it (piecesWait: the resolution answered "wait") */
inline int PiecesRoute(int check, int piecesWait) { return (check == kSwapWriterUnknown && piecesWait != 0) ? (int)kSwapPiecesNotReady : check; }
inline int CapRoute(int check, int missing, int capFull)
{
    return ((check == kSwapNoStandIn || check == kSwapOwnersNotReady) && missing != 0 && capFull != 0) ? (int)kSwapStandInCapped : check;
}
/* THE HOLD for a refusal: kFbRun = the read runs now (nothing refused, or the read cannot be held - a world load); kFbHold = held one
   more frame; kFbGiveUp = held kWaitLimitMs (the same 60 s): the read runs and the swap's own refusal loads this game's copy, as
   before (the game's counter fallbackTimeout - 0 in a healthy run). T-1279 fold 2 (re-check C; owner decision 1580 a): while a world
   server can answer this game (canServe - the same fact as the pre-load's and the wait's) there is no time bound: noStandIn /
   ownersNotReady / legacyNotReady hold until the stand-ins are made; noFile (the swap names a file that is not there) is
   kFbAskRecord - the record here and its swap entry are dropped and the world server's record is waited for, asked with haveStamp 0.
   kFbGiveUp is answered only when no world server can answer. */
enum { kFbRun = 0, kFbHold = 1, kFbGiveUp = 2, kFbAskRecord = 3 };
inline int FallbackHoldDecide(int check, int canHold, long long heldMs, int canServe)
{
    if (IsSwapRefusal(check) == 0 || canHold == 0 || check == kSwapWriterUnknown || check == kSwapStandInCapped) return kFbRun;   /* T-1443: an unknown writer, or a stand-in past the cap, is never held */
    if (canServe != 0) return check == kSwapNoFile ? (int)kFbAskRecord : (int)kFbHold;
    return heldMs >= kWaitLimitMs ? kFbGiveUp : kFbHold;
}
/* the [ZONE] RESTORED line's source: 1 = "after a fresh copy" - this read's wait ended on FRESH AND the swap loaded the world's copy */
inline int RestoredFromFresh(int waitEndedFresh, int swapped) { return (waitEndedFresh != 0 && swapped != 0) ? 1 : 0; }

/* T-1129 PART 3 (independent review 2026-10-31, the manager's fold). */
/* the FRESH stamps a waiting game is told: it keeps the LOWEST (cur -1 = none yet) - a later FRESH must not raise the bar past the
   record the first one named */
inline long long FreshKeep(long long cur, long long told) { return (cur < 0 || told < cur) ? told : cur; }
/* THE SHARED BUDGET: a read's holds (the area map unknown, the wait, the swap's check) share one kWaitLimitMs measured from the read's
   FIRST hold (firstHoldAt in seconds, 0 = not held yet) - no longer 60 s for the wait and another 60 s for the swap's hold */
inline long long BudgetMs(double firstHoldAt, double now) { return firstHoldAt > 0.0 ? (long long)((now - firstHoldAt) * 1000.0 + 0.5) : 0; }
/* a read held for the area map: kFbHold one more frame; kFbGiveUp = the budget is spent (the read then runs with no wait) */
inline int MapHoldDecide(long long budgetMs) { return budgetMs >= kWaitLimitMs ? (int)kFbGiveUp : (int)kFbHold; }
/* a row whose read never came (0xA0D6A0 marks the load decided before it knows the content job will be queued - decomp_a0d6a0.txt:33
   vs 47-59): it ends kWaitLimitMs after it was made, so a later load of that area decides and asks afresh */
/* KEPT (T-1429 fold 8d): no engine event says a decided load will never queue its content job - this bound is that case's only end */
inline int UnreadRowExpired(int everHeld, long long sinceStartMs) { return (everHeld == 0 && sinceStartMs >= kWaitLimitMs) ? 1 : 0; }
/* THE WORLD SERVER'S ROUND for an area (one WRITE out to its holder), at an ASK. haveRound: a WRITE went; sameHolder: to the holder
   (slot and connection) the map names now; answeredFresh: the round was answered FRESH; ageMs: since its WRITE.
   kRoundWrite = a new WRITE (no round, another holder, or a FRESH-answered round past kRoundLimitMs); kRoundJoin = the asker joins
   (young and not answered - no FRESH yet, or the holder's FRESH waiting for its record); kRoundFreshNow = young and answered FRESH:
   the asker is told that FRESH now, no new WRITE; kRoundExpired = no answer within kRoundLimitMs: a new WRITE, counted.
   15 s: a holder answers a WRITE within its 10 s busy limit (FRESH, or CANT busy), so a round unanswered at 15 s was lost (a dropped
   WRITE or a lost answer) - a waiting game's 10 s re-ask reaches it past the limit at its second re-ask (20 s), well inside its 60 s.
   T-1320 C: answerQueued = a RECORD of the area, or a FRESH of it, still waits on the ASKER's own link (the world server's send queue
   toward that game - store_main.cpp SbZoneAnswerQueued). A FRESH-answered round whose answer is still queued to the asker, at any
   age, is kRoundJoinQueued: no new WRITE (the answer is on its way; a slow link is not a lost answer) - the caller tells the asker
   that round's FRESH behind the waiting record only when no FRESH of the area already waits for it. The 15 s limit now bounds only
   a FRESH-answered round whose answer has left the queue (kRoundWrite) and an unanswered round (kRoundExpired - a lost WRITE or
   answer keeps its retry). */
const long long kRoundLimitMs = 15000;
enum { kRoundWrite = 0, kRoundJoin = 1, kRoundFreshNow = 2, kRoundExpired = 3, kRoundJoinQueued = 4 };
inline int ServerRoundDecide(int haveRound, int sameHolder, int answeredFresh, long long ageMs, int answerQueued)
{
    if (haveRound == 0 || sameHolder == 0) return kRoundWrite;
    if (answeredFresh != 0 && answerQueued != 0) return kRoundJoinQueued;
    if (ageMs < kRoundLimitMs) return answeredFresh != 0 ? (int)kRoundFreshNow : (int)kRoundJoin;
    return answeredFresh != 0 ? (int)kRoundWrite : (int)kRoundExpired;
}

/* ---- T-1429 s2: THE CREATION LEASE (see the head of this file) ---- */
/* THE BOUND. A lease holder answers within kLeaseWriteLimitMs of its LEASE: its read runs at once (its wait ends on the LEASE), the
   engine marks the area loaded (ZoneMap+0xB1, phase 4 of its load) within seconds, and the one-area write costs 2-40 ms (runs/T1710);
   LeaseWriteGate gives the area 30 s to finish loading or a busy frame (a load, an engine save) to pass, then the game answers CANT.
   The copy then travels as one area record of about 250-280 KB (T2568: 257,772 and 274,324 bytes) on the reliable channel - seconds.
   T-1429 fold 8c (owner 1580, review F1402 d): THE WORLD SERVER HOLDS NO AGE ON A LEASE any more (the 60 s live and 300 s load bounds
   are gone). A lease ends on its holder's events: its upload stored (DONE), its CANT (the after-load step's notLoaded for an unread
   settled area, LeaseWriteGate's answer, an EMPTY lease write's notPublished, a dropped wait), or its connection
   closing (PeerGone). Fold 8d (review F1405 A): a game hung on its main thread stops servicing its link and the link's timeout closes
   it - EXCEPT while it hangs inside a whole save or a one-area save, when a helper thread keeps the link serviced (T-1118): then the
   lease ends only when the player closes that frozen game (its link drops: PeerGone). No timer covers it; the world server logs it. */
const long long kLeaseWriteLimitMs = 30000;
/* THE WORLD SERVER, an ASK no live holder can write for (ServerAskDecide not kWhyNone): kRecSend / kRecCurrent as ServerRecordAnswer;
   kRecLease = it holds no record of the area (with its file) - the asker is granted the creation lease */
enum { kRecLease = 2 };
inline int ServerRecordOrLease(int haveRecord, long long recStamp, long long askerStamp) { return haveRecord == 0 ? (int)kRecLease : ServerRecordAnswer(haveRecord, recStamp, askerStamp); }
/* THE WORLD SERVER, any ASK (not a claim) of an area while a lease is out (leaseOut = a lease granted and its copy not landed):
   kLsNone = no lease out (the roads above); kLsAgain = the asker holds the lease (its re-ask): LEASE again; kLsEnd = the holder's
   connection is gone (fold 8c: no age): the lease ends and the next waiter (this asker among them) is granted it;
   kLsWait = another game holds it: the asker joins the waiters and is told WAIT */
enum { kLsNone = 0, kLsAgain = 1, kLsWait = 2, kLsEnd = 3 };
inline int ServerLeaseOnAsk(int leaseOut, int askerIsHolder, int holderConnected)
{
    if (leaseOut == 0) return kLsNone;
    if (holderConnected == 0) return kLsEnd;
    return askerIsHolder != 0 ? (int)kLsAgain : (int)kLsWait;
}
/* THE WORLD SERVER, a CLAIM (ASK why=loadedOwn: the claimer's read ran during a world load with its own copy; claimStamp = that copy's
   writtenAt, 0 = its save's own file). kClaimGrant = no copy here, no lease: the claimer is granted the lease (its loaded copy is
   uploaded at once); kClaimAgain = it holds the lease already; kClaimCurrent = the world's copy is the one it loaded (or older - its
   own newer copy goes up at its next save, as before); kClaimNotWorlds = another game holds the lease, or the world's copy is newer
   than the one it loaded: its copy is not the world's */
enum { kClaimGrant = 0, kClaimAgain = 1, kClaimCurrent = 2, kClaimNotWorlds = 3 };
inline int ServerClaimDecide(int haveRecord, long long recStamp, long long claimStamp, int leaseOut, int claimerIsHolder, int liveRoundOpen = 0)
{
    if (leaseOut != 0) return claimerIsHolder != 0 ? (int)kClaimAgain : (int)kClaimNotWorlds;
    if (haveRecord == 0) return liveRoundOpen != 0 ? (int)kClaimNotWorlds : (int)kClaimGrant;   /* s2 fold 1 (review F1337 item 8): a live holder's open WRITE round - its copy is coming; the claimer joins it */
    return recStamp > claimStamp ? (int)kClaimNotWorlds : (int)kClaimCurrent;
}
/* THE WORLD SERVER, an area record (zone.x.y with its file) arriving: kLrTakeLease = from the lease holder (that slot and connection)
   while its lease is out - taken whoever the area table names as holder: the area's first copy; kLrRefuseDiverged = from a connection
   told notWorlds for this area (its loaded copy is not the world's) - refused until it asks for the area again; kLrRefuseLease = a
   lease is out to another game - refused; kLrNone = no lease, no mark: the holder rule (zonerec::ZoneJudgeWrite) decides as before */
enum { kLrNone = 0, kLrTakeLease = 1, kLrRefuseLease = 2, kLrRefuseDiverged = 3 };
inline int LeaseRecordJudge(int leaseOut, int senderIsLeaseHolder, int senderDiverged)
{
    if (leaseOut != 0 && senderIsLeaseHolder != 0) return kLrTakeLease;
    if (senderDiverged != 0) return kLrRefuseDiverged;
    return leaseOut != 0 ? (int)kLrRefuseLease : (int)kLrNone;
}
inline int LeaseRecordTakes(int v) { return (v == kLrRefuseLease || v == kLrRefuseDiverged) ? 0 : 1; }
/* THE WORLD SERVER, a lease that ends without its copy (its holder's CANT, its leaving, its age): the next waiter - the first of the
   waiting slots (ascending) whose game is still connected - is granted it; -1 = none: the row goes and the next ASK is granted it */
inline int LeaseNextPick(int n, const int* slots, const int* connected, int endedHolder)
{
    for (int i = 0; i < n; ++i) if (slots[i] >= 0 && slots[i] != endedHolder && connected[i] != 0) return slots[i];
    return -1;
}
/* THE GAME, a LEASE arriving: kLaEndWait = this area's read is held in a wait (state 1): the wait ends, the read loads this game's own
   copy and the area is written once loaded; kLaWriteLoaded = no wait, but this game claimed the area (its world-load read ran with its
   own copy): the area is written once loaded; kLaRefuse = neither (a stale answer): CANT notLoaded, so the lease passes on at once */
enum { kLaEndWait = 0, kLaWriteLoaded = 1, kLaRefuse = 2 };
inline int LeaseArriveDecide(int waiting, int claimed) { return waiting != 0 ? (int)kLaEndWait : (claimed != 0 ? (int)kLaWriteLoaded : (int)kLaRefuse); }
/* THE GAME, the lease holder's write of the area (MAIN THREAD, each tick from its LEASE): kWhyNone = write now (the WRITE road's own
   write, AfHolderWrite); kRetryLater = the area is not loaded yet, or a load / engine save / world-copy read is in flight - asked again
   next frame until kLeaseWriteLimitMs; otherwise the CANT's why (noWorld, noHook; busy / notLoaded once the limit is spent). Unlike a
   WRITE (HolderGate) the area map's holder is not asked: the lease, not the area table, makes this game the area's creator. */
inline unsigned int LeaseWriteGate(int worldRunning, int busy, int loaded, int hooked, long long ageMs)
{
    if (worldRunning == 0) return (unsigned int)kWhyNoWorld;
    if (hooked == 0) return (unsigned int)kWhyNoHook;
    if (busy != 0) return kRetryLater;   /* T-1429 fold 8d (owner 1580): no bound - the engine's own load / save / world-copy read ending is the event */
    if (loaded == 0)
    {   /* KEPT: kLeaseWriteLimitMs for an area that never finishes loading here - no engine event says a dropped area load (as kAfAbandonSec) */
        if (ageMs < kLeaseWriteLimitMs) return kRetryLater;
        return (unsigned int)kWhyNotLoaded;
    }
    return (unsigned int)kWhyNone;
}
/* THE GAME, an area read: 1 = it runs during a world load (engine writes blocked - never held there) with the gate on (the mod and
   the area swap on, not a lone game): its copy is this game's own, and the world server is told (a CLAIM, sent once the world runs) */
inline int ClaimDue(int gateOn, int worldLoadRead) { return (gateOn != 0 && worldLoadRead != 0) ? 1 : 0; }

/* ---- T-1429 s2 fold 1 (review F1337 item 1, the BLOCKER; owner decision 1788): AREAS SETTLED BEFORE THE WORLD LOAD READS THEM. The
   established practice of an authoritative server: the world load waits for the server's decision; it never reads an area the server
   has not settled. In the pre-load hold (the 'records before the load' exchange, store.cpp PreloadHoldsTitleLoad), once the records are
   in, every area the load will read - each area of the exchange's list and its ring (cooppre::kPreRing) - is asked with why=settle,
   ONE AT A TIME in ascending key order, and the load stays held until each is settled:
     FRESH(stamp) / CANT current - the world server holds a copy (the exchange fetches it: the exchange runs once more when a FRESH
                    names a copy newer than the one fetched);
     LEASE        - no copy anywhere: this game loads its own save's copy and uploads it as soon as its world runs (a read area), or
                    answers CANT notLoaded (an area its load did not read) so the lease passes on;
     WAIT         - another game holds the lease: the ask stays open (asked again every 10 s) until FRESH (that copy landed) or LEASE
                    (the lease passed here).
   One at a time, in one order: a game holding leases waits only for an area above every one it holds, so two games settling the same
   areas can never wait for each other (the lowest waited-for area's holder waits for nothing it does not get). An area the load reads
   that the list did not cover still takes the world-load claim road (ClaimDue). ---- */
/* THE WORLD SERVER, a SETTLE ask: kStFresh = a copy is here - FRESH(its writtenAt) (the decision only: the asker's exchange fetches it);
   kStGrant = no copy, no lease, no live WRITE round: LEASE (a load lease); kStAgain = it holds the lease: LEASE again;
   kStWait = another game holds the lease, or a live holder's WRITE round of an area with no copy is open: the asker joins that row's
   waiters and is told WAIT (the row's FRESH / LEASE answers it) */
enum { kStFresh = 0, kStGrant = 1, kStAgain = 2, kStWait = 3 };
inline int ServerSettleDecide(int haveRecord, int leaseOut, int askerIsHolder, int liveRoundOpen)
{
    if (leaseOut != 0) return askerIsHolder != 0 ? (int)kStAgain : (int)kStWait;
    if (haveRecord != 0) return kStFresh;
    return liveRoundOpen != 0 ? (int)kStWait : (int)kStGrant;
}
/* THE GAME, a settle row's state: kSetNone (not asked), kSetAsked (asked, no answer), kSetWait (WAIT told), kSetFresh (a copy is the
   world's - settled), kSetLease (this game holds the area's lease - settled), kSetLeaseRead (that area was read by the world load: its copy
   goes up once the world runs), kSetLost (the lease was lost after the settle and before the read: the read is a claim) */
enum { kSetNone = 0, kSetAsked = 1, kSetWait = 2, kSetFresh = 3, kSetLease = 4, kSetLeaseRead = 5, kSetLost = 6 };
inline int SettleSettled(int st) { return (st == kSetFresh || st == kSetLease || st == kSetLeaseRead) ? 1 : 0; }
/* an answer to a settle ask (state kSetAsked / kSetWait): the new state - FRESH and CANT current settle it as the world's copy, LEASE as
   this game's lease, WAIT keeps it open; any other CANT is the event that asks it again - T-1429 fold 8e (review F1406 BLOCKER): kSetNone,
   so the next SettleStep asks it at once (a settle WAIT joined to a WRITE round that its holder answers CANT, or that a new lease grant
   answers CANT, is asked again then - not left 'asked' with no ask, now that there is no 10 s re-ask) */
inline int SettleOnAnswer(unsigned int kind, unsigned int why)
{
    if (kind == (unsigned int)kFresh) return kSetFresh;
    if (kind == (unsigned int)kLease) return kSetLease;
    if (kind == (unsigned int)kWait) return kSetWait;
    if (kind == (unsigned int)kCant && why == (unsigned int)kWhyCurrent) return kSetFresh;
    return kSetNone;
}
/* a lease of a settled area lost (CANT notWorlds: it aged out at the world server) - while the pre-load still settles: asked again
   (kSetNone: asked again at the next step - s2 fold 2, review F1364 A: kSetAsked with no ask time was 'just asked' and never asked again);
   after it (the load runs): kSetLost - its read loads this game's copy and claims it (the world-load road) */
inline int SettleOnLeaseLost(int st, int stillSettling)
{
    if (st != kSetLease) return st;
    return stillSettling != 0 ? (int)kSetNone : (int)kSetLost;
}
/* the pre-load's settle step: the first unsettled area (in key order) - kPsAsk = ask it now (never asked, or set back after a WAIT);
   kPsHold = it is asked: hold the load; kPsGo = every area settled; kPsRefetch = every area settled and a FRESH named a copy newer than
   the one fetched - the exchange runs once more (once). T-1429 fold 8b (owner 1580, no timed fallback for area loads): the 360 s give-up
   (its give-up answer and its bound) is gone - a held settle ends on the world server's answers: the lease's DONE (FRESH), its holder's CANT
   (its after-load step, its EMPTY give-back) or its holder gone (the next waiter is granted it).
   kPsAsk asks the whole unsettled rest of the list from `at` in one message (s2 fold 2 B). */
enum { kPsGo = 0, kPsAsk = 1, kPsHold = 2, kPsRefetch = 3 };
/* T-1429 fold 8d (owner 1580): no 10 s re-ask - a WAIT ends on the world server's push (FRESH at the lease's DONE, LEASE when it passes to
   this game), and a link drop asks everything again (AfSettleStepHere: the link generation) */
inline int SettleStep(int n, const int* states, int newerSeen, int refetched, int* at)
{
    for (int i = 0; i < n; ++i)
    {
        if (SettleSettled(states[i]) != 0) continue;
        *at = i;
        if (states[i] == kSetNone) return kPsAsk;
        return kPsHold;
    }
    *at = -1;
    return (newerSeen != 0 && refetched == 0) ? (int)kPsRefetch : (int)kPsGo;
}
/* once the world runs after the load: each leased area - read: its copy is written and uploaded (kSpWrite); not read: CANT notLoaded, the
   lease passes on (kSpCant); anything else: nothing (kSpNone) */
enum { kSpNone = 0, kSpWrite = 1, kSpCant = 2 };
inline int SettlePostLoad(int st) { return st == kSetLeaseRead ? (int)kSpWrite : (st == kSetLease ? (int)kSpCant : (int)kSpNone); }
/* THE WORLD SERVER, a lease ending without its holder's FRESH (review F1337 item 4): a copy of the area already stored (it landed with no
   FRESH, or another road stored one) answers the row FRESH (kLeStored) - never a second grant; else the next waiter (kLePass) */
enum { kLePass = 0, kLeStored = 1 };
inline int LeaseEndDecide(int copyStored) { return copyStored != 0 ? (int)kLeStored : (int)kLePass; }
/* (review F1337 item 2) a lease that ended while its holder is still connected and did not end it itself (it aged out) - that game is
   told CANT notWorlds and its records of the area are refused until it asks for the area again: a lost lease is never silent */
inline int LeaseLostTell(int holderStillConnected, int endedByHolder) { return (holderStillConnected != 0 && endedByHolder == 0) ? 1 : 0; }
/* (review F1337 item 3) a hand-over's WRITE never goes onto a lease row: while a lease of the area is out the hand-over waits */
inline int HandOverWaitsForLease(int leaseOut) { return leaseOut != 0 ? 1 : 0; }
/* (review F1337 item 5) THE GAME, a wait / lease row dropped (the world stopped, the read abandoned, never read): a LEASE this game holds
   and has not written is answered CANT notLoaded at once, so the lease passes on (not at the world server's bound) */
inline int LeaseDropTells(int leaseHeld) { return leaseHeld != 0 ? 1 : 0; }

/* ---- T-1429 s2 fold 2 (review F1364, manager decisions 2026-11-19 A-D) ----
   B. ONE ASK PER LOAD. The settle ask carries the whole unsettled rest of the load's area list, ascending (the key order every game
      uses): kAsk why=settle, x / y = the first area, stamp 0, then u16 n and n * u16 keys (x * 64 + y). The world server answers each area
      in that order with the 20-byte answers (FRESH / LEASE / WAIT) and STOPS at the first WAIT - the leases it grants are all below the
      area this game then waits for, so the order that rules out two games waiting for each other is kept. The areas after the WAIT are
      not answered: the game asks them again (kSetNone) once the waited area settles. */
const size_t kSettleListMax = 256;
inline bool EncodeSettleList(std::vector<char>* b, const int* keys, int n)
{
    if (keys == 0 || n < 1 || n > (int)kSettleListMax) return false;
    for (int i = 0; i < n; ++i) if (keys[i] < 0 || keys[i] >= 4096 || (i > 0 && keys[i] <= keys[i - 1])) return false;
    std::vector<char> m;
    if (!Encode(&m, kAsk, kWhySettle, KeyX(keys[0]), KeyY(keys[0]), 0)) return false;
    m.push_back((char)(n & 0xFF)); m.push_back((char)((n >> 8) & 0xFF));
    for (int i = 0; i < n; ++i) { m.push_back((char)(keys[i] & 0xFF)); m.push_back((char)((keys[i] >> 8) & 0xFF)); }
    b->insert(b->end(), m.begin(), m.end());
    return true;
}
/* false for anything but a well-formed list ask: its head a settle ASK naming the first key, 1..kSettleListMax keys strictly ascending */
inline bool DecodeSettleList(const char* p, size_t n, Msg* m, std::vector<int>* keys)
{
    if (p == 0 || n < kMsgSize + 2) return false;
    Msg d;
    if (!Decode(p, kMsgSize, &d) || d.kind != (unsigned int)kAsk || d.why != (unsigned int)kWhySettle) return false;
    const int cnt = (int)(unsigned char)p[kMsgSize] | ((int)(unsigned char)p[kMsgSize + 1] << 8);
    if (cnt < 1 || cnt > (int)kSettleListMax || n != kMsgSize + 2 + 2 * (size_t)cnt) return false;
    std::vector<int> ks;
    for (int i = 0; i < cnt; ++i)
    {
        const int k = (int)(unsigned char)p[kMsgSize + 2 + 2 * i] | ((int)(unsigned char)p[kMsgSize + 3 + 2 * i] << 8);
        if (k < 0 || k >= 4096 || (i > 0 && k <= ks.back())) return false;
        ks.push_back(k);
    }
    if (ks[0] != Key(d.x, d.y)) return false;
    *m = d; keys->swap(ks);
    return true;
}
/* THE WORLD SERVER, a list ask: 1 = answer the next area too; 0 = this answer was WAIT - stop here */
inline int SettleListGoesOn(int d) { return d == kStWait ? 0 : 1; }
/* THE GAME, a WAIT for one area of its list: every LATER area still kSetAsked was not answered (the server stopped) - kSetNone */
inline int SettleAfterWait(int st) { return st == kSetAsked ? (int)kSetNone : st; }
/* A. a lease lost while the settle runs: the area is asked again; every lease this game holds ABOVE it is given back (CANT notLoaded)
   and asked again too, so it never waits for a lower area while holding a higher one (the no-waiting-circle order) */
inline int SettleLostGivesBack(int lostKey, int key, int st) { return (key > lostKey && st == kSetLease) ? 1 : 0; }
/* C. THE BRAND-NEW WORLD. A load with no area list (a NEW GAME before anyone's start areas are up) settles ONE reserved lease first, the
   world-first lease (why worldFirst, x 0 y 0), through the same answers: the world server answers FRESH when it holds any area copy and
   no world-first lease is out, LEASE to the first asker (no age: fold 8, WorldFirstEndDecide), WAIT to everyone else (ServerSettleDecide with
   haveRecord = any area copy). The holder's load goes (its reads take the claim road: no copy anywhere, so each read area is leased to it
   and uploaded) and, once its world runs and its claims and lease uploads are done, it says FRESH worldFirst (its start areas are up);
   every waiter is then told FRESH and runs the exchange again - now with a list - and settles that list as any load. WorldFirstOnGo:
   the world-first row settled - kWfLoad (LEASE: this game creates the start areas; the load goes) or kWfRerun (FRESH: the exchange once more) */
enum { kWfLoad = 0, kWfRerun = 1 };
inline int WorldFirstOnGo(int st) { return st == kSetLease ? (int)kWfLoad : (int)kWfRerun; }
/* the holder says FRESH worldFirst once nothing of its first load is open (no claim unanswered, no lease upload queued). T-1429 fold 8
   (owner 1580, events over timers): no time bound any more - the 60 s kWorldFirstUpLimitMs is gone; a claim or lease upload that never
   ends is ended by the world server's own events (WorldFirstEndDecide: the holder in the world with none of its area leases out, or gone) */
inline int WorldFirstUpDue(int claimsOpen, int leaseWritesOpen) { return (claimsOpen == 0 && leaseWritesOpen == 0) ? 1 : 0; }
/* T-1429 fold 8 (F1399 / F1400): THE AFTER-LOAD STEP'S TRIGGER. The settle lets the load go at phase 2 and notes the world number then
   (store.cpp g_worldEpoch, raised once at each world-started edge); the step runs once, at the first world-started edge of a NEWER world
   - the real event, not a tick that saw the load in progress (the title pump lets the load go and the engine loads with no tick between,
   T2585). goWorld = the world number at the let-go, worldNow = the number after this edge. 1 = run the after-load step now. */
inline int SettleAfterLoadDue(int phase, long long goWorld, long long worldNow) { return (phase == 2 && worldNow > goWorld) ? 1 : 0; }
/* a settle dropped before its world started: kSdHeld = the held load went away (phase 1, nothing let go); kSdLoadFailed = the settled
   load never reached its world (phase 2: refused at the pump, or the next load asked first) - its after-load step: every lease it held
   given back (CANT), the world-first lease too */
enum { kSdNone = 0, kSdHeld = 1, kSdLoadFailed = 2 };
inline int SettleDropKind(int phase) { return phase <= 0 ? (int)kSdNone : (phase == 1 ? (int)kSdHeld : (int)kSdLoadFailed); }
/* fold 8b: a dropped settle gives the world-first lease back (CANT worldFirst) when this load held it (wfHeld) or was answered LEASE for it (wfState) */
inline int SettleDropGivesWorldFirst(int wfHeld, int wfState) { return (wfHeld != 0 || wfState == kSetLease) ? 1 : 0; }
/* T-1429 fold 8: THE WORLD SERVER ENDS A WORLD-FIRST LEASE ON ITS HOLDER'S OWN EVENTS (no age: the 300 s bound is gone from this lease).
   Asked at the holder's entering (its first area report without the LOADING mark), at each end of one of its area leases (DONE, CANT,
   its game gone), at its own FRESH worldFirst and at its connection's close.
   kWeGone = its connection is gone; kWeUp = it entered the world since the grant, none of its area leases is still out and an area copy
   is stored (its start areas are up: every waiter is told FRESH); kWeEmpty = entered, none of its area leases out, at least one of them
   granted and ended since the grant (holderLeasesEnded: stored + given back) and NO area copy (every start area came back empty: the lease
   passes to the next waiter, who creates them); kWeKeep = otherwise - with no area lease ever granted to it, entering alone ends nothing
   (its engine may still be about to ask for a start area loaded at the world-started edge). T-1429 fold 9 (F1417, design
   D1): the holder's FRESH worldFirst is only one more of these events - the world server's own lease table decides (only its CANT
   worldFirst gives the lease back outright). */
enum { kWeKeep = 0, kWeGone = 1, kWeUp = 2, kWeEmpty = 3 };
inline int WorldFirstEndDecide(int out, int holderConnected, int holderEntered, int holderLeasesOut, int holderLeasesEnded, int anyCopy)
{
    if (out == 0) return kWeKeep;
    if (holderConnected == 0) return kWeGone;
    if (holderEntered == 0 || holderLeasesOut != 0) return kWeKeep;
    if (anyCopy != 0) return kWeUp;
    return holderLeasesEnded > 0 ? (int)kWeEmpty : (int)kWeKeep;
}
/* T-1429 fold 9: THE HOLDER ENTERED - its first AREAS report without the LOADING mark that names areas (its engine has loaded its start
   areas, so every start-area ask went before it); not its JOIN_STAGE IN_WORLD, which the game sends at the world-started edge, before the
   engine loads (and asks for) any start area. unmarked = the report has no LOADING mark; count = its area count. 1 = entered now. */
inline int WorldFirstEntersAt(int unmarked, unsigned count) { return (unmarked != 0 && count != 0u) ? 1 : 0; }
/* D. AN EMPTY FIRST COPY (F1357). THE GAME: a lease write whose area save wrote nothing (Kenshi saved nothing, ZoneMap+0x24 = 0) is NOT
   published as the area's first copy. T-1429 fold 8c (owner 1580, review F1402 d): that EMPTY lease write IS the event - the lease is
   given back at once (CANT notPublished), so the next waiting game creates the area (no 10 s retries, no 30 s bound). A later real save
   of the area by this game (an unload, a full save) goes up by the ordinary record road. While a lease write of the area is open no
   EMPTY of it is published (LeaseEmptyMayPublish). */
inline int LeaseEmptyMayPublish(int isEmpty, int leaseWriteOpen) { return (isEmpty != 0 && leaseWriteOpen != 0) ? 0 : 1; }
/* ---- T-1429 s2 fold 3 (review F1372 BLOCKER, manager decision): COMPARE-AND-SET ON AN AREA RECORD. Established practice for a shared
   store with several writers (optimistic concurrency: an HTTP If-Match / ETag, a compare-and-set): every upload names the version it was
   made from, and the store takes it only when that is still the current version. The version is the world server's own record number
   (Record.seq, stamped by the one process that orders the folder's writes, B12) - the RECORD message's seq field, which a game sent as 0
   until now, carries for an area record the seq of the stored copy this game's copy was made from (ContentBaseOnLoad / ContentBaseOnEcho). The world server takes
   an area record - EMPTY or not, bigger or smaller - when no copy is stored, when its base is the stored copy's seq, or when the stored
   copy is this same connection's own last accepted upload (its echo not back yet: a chain of its own saves); otherwise it is STALE: not
   stored, the sender told CANT staleBase, and an open area request answered as for any refused record. A real emptying from the current
   copy is taken; an EMPTY or smaller copy made from an older one is not (F1357's road). Replaces fold 2's blanket EMPTY refusal. ---- */
enum { kBaseTake = 0, kBaseStale = 1 };
inline int ServerBaseJudge(int storedHasFile, unsigned long long storedSeq, unsigned long long base, int storedBySender)
{
    if (storedHasFile == 0) return kBaseTake;
    if (base != 0 && base == storedSeq) return kBaseTake;
    return storedBySender != 0 ? (int)kBaseTake : (int)kBaseStale;
}
/* THE GAME (fold 4, review F1376 BLOCKER): an area record goes up with the CONTENT BASE - the version of the content this game actually
   holds loaded, never a number something else moved (a version note of a record that was not newer moves the held record's seq, not the
   content). It changes in exactly two cases: (a) a load of the area from the world server's copy in this game's mirror - the seq that copy
   arrived with (ContentBaseOnLoad: the loaded stamp is the mirror copy's, which came from the world server with seq mirrorSeq; a load of
   this game's own save, or of its own not-yet-confirmed upload, is 0); (b) the world server's echo that it stored this game's upload -
   the seq it returns, when that upload is the content loaded here (ContentBaseOnEcho). 0 = no known base (a first upload, taken only when
   the world server holds no copy). */
inline unsigned long long ContentBaseOnLoad(long long loadedStamp, int mirrorKnown, long long mirrorStamp, unsigned long long mirrorSeq)
{
    return (mirrorKnown != 0 && loadedStamp == mirrorStamp) ? mirrorSeq : 0ULL;
}
inline unsigned long long ContentBaseOnEcho(unsigned long long cur, long long loadedStamp, long long echoStamp, unsigned long long echoSeq)
{
    return (loadedStamp == echoStamp && echoSeq > cur) ? echoSeq : cur;
}
/* after CANT staleBase: nothing of the area is published until a load of it takes the world's copy - lifted only by a load whose content
   base is known (not 0) */
inline int StaleBaseLifts(unsigned long long baseAtLoad) { return baseAtLoad != 0 ? 1 : 0; }
/* ... and any area record that replaces a bigger stored one is logged (old -> new size) */
inline int ServerSmallerReplace(int storedHasFile, long long storedLen, long long newLen) { return (storedHasFile != 0 && newLen < storedLen) ? 1 : 0; }

}   /* namespace areafresh */

#endif
