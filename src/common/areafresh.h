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
 * The game-to-game protocol does not move: this message rides only the world-server link. */
#ifndef COOP_COMMON_AREAFRESH_H
#define COOP_COMMON_AREAFRESH_H

#include <cstddef>
#include <vector>

namespace areafresh {

const unsigned int kMsgAreaFresh = 89u;   /* 28-87 are taken on the world-server link (store_main.cpp's enum and constants); 88 is CHUNK on effort/t1118f / t1118g (part 3) */
const unsigned int kProtocol = 133u;      /* the world-server protocol that carries AREA_FRESH (127 since part 3 moved it to 89) */

enum { kAsk = 1, kWrite = 2, kCant = 3, kFresh = 4 };
const size_t kMsgSize = 20;

/* why (a CANT's reason; kWhyNone = no reason - a WRITE / FRESH, or the world server's "write it") */
enum { kWhyNone = 0, kWhyNoHolder = 1, kWhyAskerHolds = 2, kWhyHolderNotLive = 3, kWhyNotHolder = 4, kWhyNotLoaded = 5,
       kWhyNoHook = 6, kWhyFault = 7, kWhyNotPublished = 8, kWhyBusy = 9, kWhyServerWrite = 10, kWhyNoWorld = 11,
       kWhySendFailed = 12, kWhyHolderGone = 13, kWhyDropped = 14, kWhyCount = 15 };   /* part 3: dropped - a queued WRITE the holder's stopped world dropped */
inline const char* WhyName(unsigned int w)
{
    static const char* const kNames[kWhyCount] = { "none", "noHolder", "askerHolds", "holderNotLive", "notHolder", "notLoaded", "noHook",
                                                   "fault", "notPublished", "busy", "serverWrite", "noWorld", "sendFailed", "holderGone", "dropped" };
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
    if (kind < (unsigned int)kAsk || kind > (unsigned int)kFresh || why >= (unsigned int)kWhyCount || !OnGrid(x, y) || stamp < 0) return false;
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
    if (d.kind < (unsigned int)kAsk || d.kind > (unsigned int)kFresh || d.why >= (unsigned int)kWhyCount || !OnGrid(d.x, d.y) || d.stamp < 0) return false;
    *m = d;
    return true;
}

/* WHO WAITS - the entering game, at "load decided" for an area (or that area's first file read). holder: the slot this game's area
   map names (>= 0), -1 unknown or nobody, or frozenCode (the map's "holder is a player not in the world"). kWaitYes = hold the read
   and ask; anything else = no wait (today's behaviour), the reason. */
enum { kWaitYes = 0, kNoWaitOff = 1, kNoWaitSingle = 2, kNoWaitNoWorld = 3, kNoWaitNoHook = 4, kNoWaitLinkDown = 5,
       kNoWaitSlotUnknown = 6, kNoWaitNoHolder = 7, kNoWaitHolderSelf = 8, kNoWaitHolderAway = 9, kWaitMapUnknown = 10, kNoWaitAlone = 11 };
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
    if (othersInWorld == 0) return kNoWaitAlone;
    if (mapFresh == 0) return kWaitMapUnknown;
    if (holder == frozenCode) return kNoWaitHolderAway;
    if (holder < 0) return kNoWaitNoHolder;
    if (mySlot < 0) return kNoWaitSlotUnknown;
    if (holder == mySlot) return kNoWaitHolderSelf;
    return kWaitYes;
}

/* WHEN THE WAIT ENDS. freshStamp: the FRESH's stamp (-1 = none yet); haveSwap / swapStamp: this game's swap entry for the area now;
   cant: a CANT came; holderGoneSaid: PLAYER_GONE named the asked holder; holderNow: the map's holder now (as WaitDecide). A map
   with no answer (-1: no fresh map - the link is down, or the area is not listed) keeps the wait: the link-up asks again. */
enum { kKeepWaiting = 0, kEndFresh = 1, kEndCant = 2, kEndHolderGone = 3 };
inline int WaitEndDecide(long long freshStamp, int haveSwap, long long swapStamp, int cant, int holderGoneSaid, int askedHolder, int holderNow, int frozenCode)
{
    if (freshStamp >= 0 && haveSwap != 0 && swapStamp >= freshStamp) return kEndFresh;   /* the fresh copy is in: the swap loads it */
    if (cant != 0) return kEndCant;
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

/* T-1129 PART 2 - RE-ASK AND TIME-OUT (manager decision, 2026-10-31). While a wait has no FRESH and no CANT the ASK goes again every
   kReaskMs on a link that is up (beside the ask at each link-up, AskDue); at kWaitLimitMs from the wait's start with no answer the
   wait ends and the newest copy here loads, as on a CANT (the game's counter timeout - 0 in a healthy run). Several ends due in one
   frame: an answer wins over the time-out, in WaitEndDecide's own order (FRESH, CANT, the holder gone), then the time-out. */
const long long kReaskMs = 10000;
const long long kWaitLimitMs = 60000;
enum { kEndTimeout = 4 };
inline int WaitEndOrTimeout(long long freshStamp, int haveSwap, long long swapStamp, int cant, int holderGoneSaid, int askedHolder, int holderNow, int frozenCode, long long waitedMs)
{
    const int e = WaitEndDecide(freshStamp, haveSwap, swapStamp, cant, holderGoneSaid, askedHolder, holderNow, frozenCode);
    if (e != kKeepWaiting) return e;
    return waitedMs >= kWaitLimitMs ? (int)kEndTimeout : (int)kKeepWaiting;
}
/* which ASK goes this frame (the link generation's ask wins when both are due): kAskLink = the first on this link (AskDue);
   kAskAgain = kReaskMs since the last ask was tried, the link up; none once the time-out is due (the next retry ends the wait). None
   once an answer is noted (haveAnswer: a FRESH - its record is on its way - or a CANT - the next read loads the copy here); the wait
   stays bounded by the 60 s end */
enum { kAskNone = 0, kAskLink = 1, kAskAgain = 2 };
inline int AskPlan(int linkUp, long askedGen, long linkGen, long long sinceAskMs, long long waitedMs, int haveAnswer)
{
    if (haveAnswer != 0) return kAskNone;
    if (waitedMs >= kWaitLimitMs) return kAskNone;
    if (AskDue(linkUp, askedGen, linkGen) != 0) return kAskLink;
    if (linkUp != 0 && sinceAskMs >= kReaskMs) return kAskAgain;
    return kAskNone;
}

/* T-1129 PART 2 - THE AREA-FILE SWAP'S REFUSALS, ASKED BEFORE THE READ. The swap (the game's detour_sfsReadFile) loads the world's
   copy of an area in place of this game's own file, and on any refusal quietly loads this game's own older file - the doubling road
   T-1129 is about. The game asks the same checks of the same entry on the main thread BEFORE the read, in the swap's own order:
   gateOn = the mod and the area swap on, not a lone game; haveRec / fileNamed = a swap entry naming a file (else nothing to swap);
   fileThere = that file on disk; translate = another game's (or an unknown writer's) record, whose owners are translated at the load;
   standInReady = the writer's stand-in (ZoneStandInReady); ownersReady = every owner the file names has one (ZoneOwnersReady);
   legacyReady = a protocol-67 owner can be kept (ZoneLegacyPeerReady). */
enum { kSwapNothing = 0, kSwapGo = 1, kSwapNoFile = 2, kSwapNoStandIn = 3, kSwapOwnersNotReady = 4, kSwapLegacyNotReady = 5, kSwapCount = 6 };
inline const char* SwapCheckName(int c)
{
    static const char* const kNames[kSwapCount] = { "nothingToSwap", "go", "noFile", "noStandIn", "ownersNotReady", "legacyNotReady" };
    return (c >= 0 && c < (int)kSwapCount) ? kNames[c] : "unknown";
}
inline int IsSwapRefusal(int c) { return (c >= (int)kSwapNoFile && c < (int)kSwapCount) ? 1 : 0; }
inline int SwapPrecheck(int gateOn, int haveRec, int fileNamed, int fileThere, int translate, int standInReady, int ownersReady, int legacyReady)
{
    if (gateOn == 0 || haveRec == 0 || fileNamed == 0) return kSwapNothing;
    if (fileThere == 0) return kSwapNoFile;
    if (translate == 0) return kSwapGo;
    if (standInReady == 0) return kSwapNoStandIn;
    if (ownersReady == 0) return kSwapOwnersNotReady;
    if (legacyReady == 0) return kSwapLegacyNotReady;
    return kSwapGo;
}
/* THE HOLD for a refusal: kFbRun = the read runs now (nothing refused, or the read cannot be held - a world load); kFbHold = held one
   more frame; kFbGiveUp = held kWaitLimitMs (the same 60 s): the read runs and the swap's own refusal loads this game's copy, as
   before (the game's counter fallbackTimeout - 0 in a healthy run) */
enum { kFbRun = 0, kFbHold = 1, kFbGiveUp = 2 };
inline int FallbackHoldDecide(int check, int canHold, long long heldMs)
{
    if (IsSwapRefusal(check) == 0 || canHold == 0) return kFbRun;
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
inline int UnreadRowExpired(int everHeld, long long sinceStartMs) { return (everHeld == 0 && sinceStartMs >= kWaitLimitMs) ? 1 : 0; }
/* THE WORLD SERVER'S ROUND for an area (one WRITE out to its holder), at an ASK. haveRound: a WRITE went; sameHolder: to the holder
   (slot and connection) the map names now; answeredFresh: the round was answered FRESH; ageMs: since its WRITE.
   kRoundWrite = a new WRITE (no round, another holder, or a FRESH-answered round past kRoundLimitMs); kRoundJoin = the asker joins
   (young and not answered - no FRESH yet, or the holder's FRESH waiting for its record); kRoundFreshNow = young and answered FRESH:
   the asker is told that FRESH now, no new WRITE; kRoundExpired = no answer within kRoundLimitMs: a new WRITE, counted.
   15 s: a holder answers a WRITE within its 10 s busy limit (FRESH, or CANT busy), so a round unanswered at 15 s was lost (a dropped
   WRITE or a lost answer) - a waiting game's 10 s re-ask reaches it past the limit at its second re-ask (20 s), well inside its 60 s */
const long long kRoundLimitMs = 15000;
enum { kRoundWrite = 0, kRoundJoin = 1, kRoundFreshNow = 2, kRoundExpired = 3 };
inline int ServerRoundDecide(int haveRound, int sameHolder, int answeredFresh, long long ageMs)
{
    if (haveRound == 0 || sameHolder == 0) return kRoundWrite;
    if (ageMs < kRoundLimitMs) return answeredFresh != 0 ? (int)kRoundFreshNow : (int)kRoundJoin;
    return answeredFresh != 0 ? (int)kRoundWrite : (int)kRoundExpired;
}

}   /* namespace areafresh */

#endif
