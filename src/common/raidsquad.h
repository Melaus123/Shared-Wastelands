#pragma once
// T-1122: A GAME'S OWN RAID SQUADS - the pure decisions and the book of rows, ONE header compiled into the plugin (towngen.cpp's
// creation gate; store.cpp's off-screen update, sweep, wake, forget and sleeping-position publish) and the offline suite.
//
// THE CAUSE (run T1918, and Read along the path): each game picks raid targets only among its own player's bases. A raid campaign
// asks createRandomUnloadedSquad for its squads (the raid caller, address-table row RaidSquadCreateRet) at the attacker towns, which
// usually lie in areas nobody holds; the creation gate refused every one of them ('not-holder (nobody holds the area)'), the campaign
// was short of squads and the engine cancelled it. In solo play those squads are made asleep and travel off-screen to the base.
//
// THE THREE RULES:
//  1. GATE (GateAllows): a squad asked for by the raid caller, in an area nobody holds and no other game has loaded or stands beside, is
//     made on THIS game - it is this game's own campaign, aimed at this game's own player's base. Every other answer is unchanged.
//  2. TRAVEL (TravelDecide): this game's own raid squad gets the engine's own off-screen update (its travel and its wake check) on this
//     game even when this game is not the off-screen authority - where this game holds the squad's sleeping area (MayInventHereTS 1),
//     or where no other game holds or has loaded it and the map is fresh. Otherwise the existing roads decide: in another game's area
//     the squad is held where it is, its clock kept fresh, until that game leaves (no hand-over - an owner question). The stops are
//     counted and timed (Book::heldStops / heldSquads / HeldMsNow) so a run shows how long raids wait.
//  3. ONE TRAVELLER (SleepPosPublishes): nothing of an own raid squad reaches the world server while it sleeps - the engine's create
//     call writes no record (store.cpp detour_deactivate: `container != 0` returns before any write; StoreRecordCreated has no
//     caller), and the sleeping-position publish skips it should this game be the authority. So no other game holds a record of it:
//     the authority builds no copy of it and sends no position note for it, and its only traveller is the game that made it. At its
//     wake its row goes (the squad is then an ordinary awake squad, written and released by the existing roads).
//
// WHICH SQUADS ARE "OWN" - TWO ROADS, ONE BOOK:
//  - the creation gate notes each squad it makes (rule 1);
//  - THE LASTING FACTS (LastingOwn, store.cpp OwnRaidSweepTick, every 5 s on the main thread): a sleeping squad whose kept byte
//    (Platoon+0x115) is 0 while the engine's own keep test (0x7EB9D0, the original - not the mod's hook) answers "keep" is in one of its
//    faction's running war campaigns on THIS game (decomp_7eb9d0: with the byte 0 the only "keep" is the faction war manager's
//    campaign lookup, Faction+0x88, campaign state +0xC0 != 2; Read) - and with no binding to a world record and no world record of
//    its id, nobody else has it - and its campaign's target belongs to THIS game's own player faction, so this game made the campaign
//    (a save both games load holds it on both; T-1122 review fold). Such a squad is noted too. A save reload or a rejoin empties the book (the teardown); the sweep finds
//    the campaigns' sleeping squads again. The raid caller makes its squads with that byte 0 (decomp_9c3280: argument 10 = 0, which
//    createRandomUnloadedSquad writes to +0x115, F524; Read).
//
// WHEN A SQUAD CANNOT BE NOTED (NoteDecide; counted noSleeper / awake / capFull on the store REPORT's ownRaid token):
//  - no sleeping object (Platoon+0x1E0 unreadable or null) or already awake: not noted. An awake squad is the existing roads' (its
//    heartbeat writes it). A squad with no sleeping object is not in a sleeping list either, so no off-screen update reaches it.
//  - the book full (kCap rows): not noted; the sweep tries again every 5 s.
//  An un-noted sleeping campaign squad is an ordinary sleeping squad: on the AUTHORITY the authority road travels it as it travels
//  every sleeping squad, and the sleeping-position publish sends its position (an ordinary position-only record); on a NON-AUTHORITY
//  game nothing travels it (as before T-1122) - it waits where it is until a later sweep notes it or it wakes near this game's player.
//
// THE ROWS: keyed by the platoon, with the engine's sleeping object read at the note. A row stands for its squad only while the platoon
// still points at that object (RowStillThis) and - on the main thread - the platoon is in its faction's sleeping list; otherwise it is
// dropped. A row goes at the wake, at the free, at the destroy drain's reused-address branch and at the world teardown.
#include <cmath>
#include <map>
#include <cstring>
#include <string>
#include <vector>
#include "liveowner.h"   /* T-1122 attempt 2: the engine's strip and the release keep margin (cooplo::kEngineEdgeStrip, kKeepMarginUnits) */

namespace raidsq {

const size_t kCap = 512;                   // rows kept at once
const unsigned long kSweepGapMs = 5000;    // the lasting-facts sweep's period

/* The creation gate's allowance. raidCaller: raidnow::IsRaidCaller of this call; may: MayInventFromView's answer (1 this game holds
   it, 0 not, -1 no fresh map, -2 teardown); held / mine / otherLoaded / peerRing1: the same locked AreaViewTS read (held 1 another
   game holds it, 0 nobody, -1 no fresh map, -2 frozen; mine 1 this game holds it; otherLoaded 1 another game has it loaded, 0 not,
   -1 no fresh map; peerRing1 1 another game's player stands beside it). 1 = make the squad here. */
inline int GateAllows(int raidCaller, int may, int held, int mine, int otherLoaded, int peerRing1)
{
    if (raidCaller == 0 || may != 0) return 0;   // not the raid caller, or the gate already answered otherwise (holder, no map, teardown)
    return (held == 0 && mine == 0 && otherLoaded == 0 && peerRing1 != 1) ? 1 : 0;
}

/* The off-screen update of one sleeping squad on a game whose authority / single-player roads did not run it. linked: the world-server
   link is up; ownRaid: the squad is one of this game's own raid squads (a live row); mineHolds: MayInventHereTS == 1 at its sleeping
   position (this game is that area's game); heldByOther / otherLoaded: HeldByOtherTS / OtherLoadedTS there (1 yes, 0 no, -1 no fresh
   map, -2 frozen). */
enum { kTravelNotOwn = 0, kTravelRuns = 1, kTravelOtherArea = 2 };
inline int TravelDecide(int linked, int ownRaid, int mineHolds, int heldByOther, int otherLoaded)
{
    if (linked == 0 || ownRaid == 0) return kTravelNotOwn;   // the existing roads decide
    if (mineHolds == 1) return kTravelRuns;                   // this game's own area: the engine's update runs here in any case
    return (heldByOther == 0 && otherLoaded == 0) ? kTravelRuns : kTravelOtherArea;
}

/* the sleeping-position publish: 0 = an own raid squad is not published (rule 3) */
inline int SleepPosPublishes(int ownRaid) { return ownRaid != 0 ? 0 : 1; }

/* a row still names its squad: the platoon was read, and it still points at the sleeping object noted at its creation */
inline int RowStillThis(int readOk, const void* nowUnloaded, const void* noted)
{
    return (readOk != 0 && noted != 0 && nowUnloaded == noted) ? 1 : 0;
}

/* a travel line is due: the squad's sleeping sector changed since the last one */
inline int SectorChanged(int lastX, int lastY, int x, int y) { return (x != lastX || y != lastY) ? 1 : 0; }

/* may this squad be noted: readOk - the platoon was read; sleeper - its sleeping object (+0x1E0) is a plausible pointer; awake - its
   awake object (+0x1D8) is */
enum { kNoteGo = 0, kNoteNoSleeper = 1, kNoteAwake = 2 };
inline int NoteDecide(int readOk, int sleeper, int awake)
{
    if (readOk == 0) return kNoteNoSleeper;
    if (awake != 0) return kNoteAwake;
    return sleeper != 0 ? kNoteGo : kNoteNoSleeper;
}

/* THE LASTING FACTS: a sleeping squad with its kept byte 0 that the engine's own keep test keeps (so: in a running war campaign of its
   faction on this game), bound to no world record and with no world record of its id, whose campaign THIS game made. 1 = this game's
   own campaign squad. madeHere (T-1122 review fold): the campaign's target belongs to this game's own player faction (1), to another
   or not read (0 / -1) - each game picks raid targets only among its own player's bases (THE CAUSE above), and a save both games load
   holds every campaign on both, so without it both sweeps would claim the same squads. */
inline int LastingOwn(int asleep, int keptByte, int engineKeeps, int bound, int hasRecord, int madeHere)
{
    return (asleep != 0 && keptByte == 0 && engineKeeps == 1 && bound == 0 && hasRecord == 0 && madeHere == 1) ? 1 : 0;
}

/* one row */
struct Row
{
    const void* unloaded;        // the sleeping object noted
    int sx, sy;                  // the last sleeping sector seen
    long long steps;             // sector changes seen
    int held;                    // 1 while stopped at another game's area
    unsigned long heldSinceMs;   // when that stop began (a GetTickCount value; differences wrap correctly)
    int everHeld;                // 1 once it has stopped at all
    char id[64];
};

/* THE BOOK. Plain code - the caller holds its lock. nowMs: a GetTickCount value. */
class Book
{
public:
    Book() : heldMs(0), heldSquads(0), heldStops(0) {}
    std::map<const void*, Row> rows;                 // the platoon -> its row
    std::map<const void*, const void*> byUnloaded;   // the noted sleeping object -> the platoon
    unsigned long long heldMs;                       // the closed stops' time
    long long heldSquads, heldStops;                 // squads that ever stopped; stops begun

    size_t Size() const { return rows.size(); }
    int Has(const void* p) const { return rows.find(p) != rows.end() ? 1 : 0; }
    int HasWith(const void* p, const void* u) const
    {
        std::map<const void*, Row>::const_iterator it = rows.find(p);
        return (it != rows.end() && it->second.unloaded == u) ? 1 : 0;
    }
    const void* PlatoonOf(const void* u) const
    {
        std::map<const void*, const void*>::const_iterator it = byUnloaded.find(u);
        return it != byUnloaded.end() ? it->second : 0;
    }
    /* 1 = noted; 0 = the book is full (or no platoon / sleeping object). A row already on this platoon (an address given to a new squad,
       or the same squad noted again after its wake) or on this sleeping object goes first: *replaced = 1. */
    int Note(const void* p, const void* u, int sx, int sy, const char* id, size_t cap, unsigned long nowMs, int* replaced)
    {
        *replaced = 0;
        if (p == 0 || u == 0) return 0;
        if (Forget(p, nowMs, 0) != 0) *replaced = 1;
        const void* q = PlatoonOf(u);
        if (q != 0) { Forget(q, nowMs, 0); *replaced = 1; }
        if (rows.size() >= cap) return 0;
        Row r; std::memset(&r, 0, sizeof(r));
        r.unloaded = u; r.sx = sx; r.sy = sy;
        if (id != 0) { std::strncpy(r.id, id, sizeof(r.id) - 1); r.id[sizeof(r.id) - 1] = 0; }
        rows[p] = r; byUnloaded[u] = p;
        return 1;
    }
    /* 1 = the row went (*out = it, its open stop closed first) */
    int Forget(const void* p, unsigned long nowMs, Row* out)
    {
        std::map<const void*, Row>::iterator it = rows.find(p);
        if (it == rows.end()) return 0;
        CloseStop(it->second, nowMs);
        if (out != 0) *out = it->second;
        std::map<const void*, const void*>::iterator u = byUnloaded.find(it->second.unloaded);
        if (u != byUnloaded.end() && u->second == p) byUnloaded.erase(u);
        rows.erase(it);
        return 1;
    }
    /* the stale drop: only the row that still names this sleeping object */
    int ForgetIf(const void* p, const void* u, unsigned long nowMs, Row* out) { return HasWith(p, u) != 0 ? Forget(p, nowMs, out) : 0; }
    /* an update that does not travel the squad: 1 = it begins a stop (*out = the row) */
    int HoldBegin(const void* p, unsigned long nowMs, Row* out)
    {
        std::map<const void*, Row>::iterator it = rows.find(p);
        if (it == rows.end() || it->second.held != 0) return 0;
        it->second.held = 1; it->second.heldSinceMs = nowMs; ++heldStops;
        if (it->second.everHeld == 0) { it->second.everHeld = 1; ++heldSquads; }
        if (out != 0) *out = it->second;
        return 1;
    }
    /* an update that travelled the squad, now at sector x,y: a stop ends (*stopMs = its length, else 0); 1 = the sector changed (*fx,*fy
       the sector before, *steps the row's count) */
    int TravelStep(const void* p, int x, int y, unsigned long nowMs, int* fx, int* fy, long long* steps, unsigned long* stopMs, Row* out)
    {
        *stopMs = 0;
        std::map<const void*, Row>::iterator it = rows.find(p);
        if (it == rows.end()) return 0;
        Row& r = it->second;
        if (r.held != 0) { *stopMs = nowMs - r.heldSinceMs; CloseStop(r, nowMs); }
        int moved = 0;
        if (SectorChanged(r.sx, r.sy, x, y) != 0) { *fx = r.sx; *fy = r.sy; r.sx = x; r.sy = y; ++r.steps; moved = 1; }
        *steps = r.steps;
        if (out != 0) *out = r;
        return moved;
    }
    /* the time stopped so far: the closed stops plus the open ones */
    unsigned long long HeldMsNow(unsigned long nowMs) const
    {
        unsigned long long t = heldMs;
        for (std::map<const void*, Row>::const_iterator it = rows.begin(); it != rows.end(); ++it)
            if (it->second.held != 0) t += (unsigned long)(nowMs - it->second.heldSinceMs);
        return t;
    }
    long long HeldNow() const
    {
        long long n = 0;
        for (std::map<const void*, Row>::const_iterator it = rows.begin(); it != rows.end(); ++it) if (it->second.held != 0) ++n;
        return n;
    }
    /* the world teardown: every row goes, open stops closed (the totals are cumulative evidence and stay) */
    void Clear(unsigned long nowMs)
    {
        for (std::map<const void*, Row>::iterator it = rows.begin(); it != rows.end(); ++it) CloseStop(it->second, nowMs);
        rows.clear(); byUnloaded.clear();
    }
private:
    void CloseStop(Row& r, unsigned long nowMs) { if (r.held != 0) { heldMs += (unsigned long)(nowMs - r.heldSinceMs); r.held = 0; } }
};

/* ==================== T-1122 build (owner 1443 b): THE RAID HAND-OVER (the LEND) ====================
   The lender (B, the game that made the raid squad and keeps the raid) holds its sleeping squad where another game (A) holds the area
   (TravelDecide kTravelOtherArea with heldByOther == 1 only); it writes the squad's state as its WORLD record and LENDs naming that
   record to A (T-1122 lend by reference, game protocol 219 - the section at the end). A builds an awake-able copy from that record,
   marked LENT (nothing of it reaches the world server until its put-away), walks its people toward the raid's target, and gives it back
   (RETURN naming the world record its put-away wrote, and its position) when its engine puts it away at A's area edge, or says DIED
   when it is wiped out. B keeps the raid; its squad stays
   frozen while lent. Keyed by the lender's slot + the squad's row id + a serial (LendSerialNext: unique across the lender's restarts) -
   never by an id's number block. A's keep question keeps the copy while the lend is open (LentKeep), so its engine puts it to sleep at the
   edge rather than deleting it.
   One game-to-game message, MSG_RAID_LEND (net/transport.h 86), with a kind byte. */
const unsigned int kMsgRaidLend = 86;           /* MSG_RAID_LEND (net/transport.h, liveenvelope.h kInnerRaidLend and store.cpp assert it) */
const unsigned int kLendMagic = 0x31444C52u;    /* 'RLD1' */
const unsigned long kLendAskGapMs = 10000;      /* LEND asked again while unanswered and the link is up */
const unsigned long kLendStatusGapMs = 30000;   /* while TAKEN: the lender asks for the lend's state again */
const unsigned long kLendBackoffMs = 30000;     /* after a refusal (or an unknown answer): no new lend of that squad before this */
const unsigned long kLendReturnSweepMs = 2000;  /* borrower: a returned copy still in its faction's sleeping list after this is removed */
const unsigned long kLendGoneMs = 15000;        /* the other side of a lend missing from the roster this long has left (a shorter gap is a blip) */

enum { kLendKindLend = 1, kLendKindTaken = 2, kLendKindRefused = 3, kLendKindReturn = 4, kLendKindDied = 5, kLendKindUnknown = 6,
       kLendKindAwake = 7, kLendKindKeepOwn = 8, kLendKindWithdrawn = 9 };   /* AWAKE: T-1122 awake hand (owner 1524, game protocol 215) - see that section at the end. KEEPOWN:
                                                     T-1122 lend by reference (game protocol 219) - the borrower's put-away could not write its copy's state as the world record (or
                                                     the world server refused that write after the RETURN named it): the lender resumes its own squad as it lent it (never DIED for
                                                     that - DIED removes a living squad). WITHDRAWN: T-1122 review folds (game protocol 219) - the lender's answer to a TAKEN or
                                                     RETURN for a serial it has not open: the borrower removes its copy (section at the end) */
enum { kRefNone = 0, kRefNoWorld = 1, kRefNotHolder = 2, kRefNoFaction = 3, kRefBuildFailed = 4, kRefCapFull = 5, kRefBadBytes = 6, kRefOutsideWake = 7,
       kRefNoRecord = 8 };   /* 8: T-1122 lend by reference - the LEND's named world record not here within kLendRecordWaitMs (6: no longer said - no bytes ride a LEND) */

/* the lender's row state */
enum { kLendIdle = 0, kLendAsked = 1, kLendTaken = 2 };

/* MAY A LEND START (the lender, at the hold point). linked: the world-server link is up; heldByOther: HeldByOtherTS (1 only - 0, -1 no
   fresh map and -2 frozen never lend); holderSlot: the area holder's slot (AreaHolderSlotTS), mySlot this game's; state: the row's lend
   state; quiet: 1 while a refusal's backoff or a short return's wait runs (LendShortQuietMs). raidOver (T-1122 attempt 2 fold 2, re-check
   HIGH): the borrower's RAID_END said the raid is over there (LendRaidOver) - such a squad is never lent (nor moved) again: its own engine
   runs it as in one-player play (with no campaign it is not kept - LentKeep). */
enum { kStartGo = 0, kStartNoLink = 1, kStartNotHeld = 2, kStartNoHolder = 3, kStartOpen = 4, kStartQuiet = 5, kStartRaidOver = 6 };
inline int LendStart(int linked, int heldByOther, int holderSlot, int mySlot, int state, int quiet, int raidOver = 0)
{
    if (linked == 0) return kStartNoLink;
    if (heldByOther != 1) return kStartNotHeld;
    if (holderSlot < 0 || holderSlot == mySlot) return kStartNoHolder;
    if (state != kLendIdle) return kStartOpen;
    if (raidOver != 0) return kStartRaidOver;
    if (quiet != 0) return kStartQuiet;
    return kStartGo;
}

/* ==== T-1122 fix (run T1967, F518 / F1050): A RAID SQUAD IS WOKEN ONLY WHERE THE GAME CAN WAKE IT - THE GAME'S WAKE BOX ====
   The game wakes a sleeping squad only while its spot stays inside its area's box for 5 s of game time: the area, pulled in kWakeEdge
   on each side whose neighbouring area that game has not loaded (F518: grown 10 u, pulled in 170 u). Outside that box, even in an area
   the game holds, the countdown is set back to 2.0 at every check and the squad never wakes (T1967, Confirmed: copy 1 built 0.4 u from
   an edge whose neighbour the borrower had not loaded - countdown 2.000 for 61 s, never woke; copy 2 built 159 u from a loaded
   neighbour's edge - woke at 5 s and marched). The lend fired the moment the squad crossed into the borrower's area.
   THE BORROWER DECIDES (it knows its own loaded areas exactly; the lender's view comes through the world server's area map and lags):
   a lend whose spot is outside the borrower's box is refused with kRefOutsideWake and the box; the lender's squad then travels on, as
   the game itself travels a sleeping squad there (unseen, not woken), and is lent again once its spot is inside that box (LendBoxDecide).
   T-1122 review fold (item 1): the borrower reads the game's OWN flags at the decision (main thread): its area's loaded byte and, for
   each neighbour, the game's own side-neighbour test (loaded or loading - ZoneKeepSectorTri), never the once-a-second loaded set (an
   unloading neighbour can still be listed there after the game stopped counting it). The box is pulled in kWakeMargin more than the
   game's, so it is never larger than the game's. */
const float kWakeEdge = 160.0f;      /* F518: the area grown 10 u, then pulled in 170 u on a side facing an unloaded neighbour */
const float kWakeMargin = 16.0f;     /* kept further in, so a spot on the box's line is never judged by a float's last bit */
const float kAreaSize = 4608.0f;     /* poskey.h: the world is a 64 x 64 grid of 4608-unit areas */
const float kAreaOrigin = 147456.0f;
const unsigned long kLendBoxAskGapMs = 2000;   /* the lender: after an 'outside my wake box' refusal, no new lend of that squad before this (a rate limit on asks, not a readiness wait) */
const unsigned long kLendAsleepMaxMs = 60000;  /* the borrower: a copy asleep INSIDE its wake box this long after its build is given back (the game wakes one there in ~5 s - T1967) */
struct WakeBox { int ok; float x0, z0, x1, z1; WakeBox() : ok(0), x0(0), z0(0), x1(0), z1(0) {} };
/* area (ax, ay)'s wake box on a game whose loaded set has the area (self) and its neighbours W = ax-1, E = ax+1, N = ay-1, S = ay+1
   (1 loaded, anything else not). ok 0 = no box (the area itself is not loaded there, or off the grid). A loaded side keeps the area's
   own edge (the game's box reaches 10 u past it). */
inline WakeBox WakeBoxOf(int ax, int ay, int self, int lW, int lE, int lN, int lS)
{
    WakeBox b;
    if (self != 1 || ax < 0 || ay < 0 || ax > 63 || ay > 63) return b;
    const float x0 = (float)ax * kAreaSize - kAreaOrigin, z0 = (float)ay * kAreaSize - kAreaOrigin, in = kWakeEdge + kWakeMargin;
    b.x0 = x0 + (lW == 1 ? 0.0f : in); b.x1 = x0 + kAreaSize - (lE == 1 ? 0.0f : in);
    b.z0 = z0 + (lN == 1 ? 0.0f : in); b.z1 = z0 + kAreaSize - (lS == 1 ? 0.0f : in);
    b.ok = 1;
    return b;
}
/* 1 = the spot (x, z) is inside the box (a NaN spot never is) */
inline int WakeBoxHas(const WakeBox& b, float x, float z) { return (b.ok == 1 && x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1) ? 1 : 0; }
/* T-1122 review fold (item 2): THE LENDER'S OWN GAME on a travel-on road (the borrower's 'outside my wake box' answer, or the grace
   after a RETURN) runs the game's full off-screen update, its own wake check included. If the lender has the area loaded too and has
   loaded a neighbour the borrower lacks, its own game would wake the squad inside the borrower's area. So the lender travels the squad
   only where its own game would NOT wake it: the spot outside the lender's own box, taken at its LARGEST - the game's box (a loaded
   side reaches kWakeGrow past the area's edge, an unloaded side is pulled in kWakeEdge) grown kWakeMargin all round, and a neighbour
   read as anything but 0 (loaded, loading or unread) counted loaded. self / lW..lS: the lender's own keep flags (the game's own
   side-neighbour test: 1 kept, 0 not, -1 unread). */
const float kWakeGrow = 10.0f;       /* F518: the game's box reaches 10 u past a side whose neighbour is loaded */
inline WakeBox WakeBoxOuterOf(int ax, int ay, int self, int lW, int lE, int lN, int lS)
{
    WakeBox b;
    if (self != 1 || ax < 0 || ay < 0 || ax > 63 || ay > 63) return b;
    const float x0 = (float)ax * kAreaSize - kAreaOrigin, z0 = (float)ay * kAreaSize - kAreaOrigin;
    const float in = kWakeEdge - kWakeMargin, out = kWakeGrow + kWakeMargin;
    b.x0 = x0 + (lW == 0 ? in : -out); b.x1 = x0 + kAreaSize - (lE == 0 ? in : -out);
    b.z0 = z0 + (lN == 0 ? in : -out); b.z1 = z0 + kAreaSize - (lS == 0 ? in : -out);
    b.ok = 1;
    return b;
}
/* kHostTravel = the lender's game would not wake the squad at (x, z): it may run the off-screen update; kHostStay = it would, or that
   cannot be told (off the main thread, no spot, a NaN spot, the area's own flag unread): it is held instead. self 0 = the lender's
   game has not loaded the squad's area: no wake there. */
enum { kHostTravel = 0, kHostStay = 1 };
inline int HostTravelDecide(int onMain, int haveSpot, int ax, int ay, int self, int lW, int lE, int lN, int lS, float x, float z)
{
    if (onMain != 1 || haveSpot != 1 || x != x || z != z || self < 0) return kHostStay;
    if (self == 0) return kHostTravel;
    return WakeBoxHas(WakeBoxOuterOf(ax, ay, 1, lW, lE, lN, lS), x, z) == 1 ? kHostStay : kHostTravel;
}
/* the lender's view of the four areas around the refused spot (W, E, N, S): each one's holder slot (AreaHolderSlotTS) and whether
   another game has it loaded (OtherLoadedTS), folded into one number. When it changes, the borrower may have loaded or dropped a
   neighbour, so its box may have changed: the lender asks it afresh (LendBoxDecide kBoxNone). */
inline unsigned int WakeSigOf(const int* holder4, const int* other4)
{
    unsigned int h = 2166136261u;
    for (int i = 0; i < 4; ++i) { h = (h ^ (unsigned int)(holder4[i] + 3)) * 16777619u; h = (h ^ (unsigned int)(other4[i] + 3)) * 16777619u; }
    return h;
}
/* THE LENDER after a kRefOutsideWake refusal (haveBox 1: the box for area (bx, by), sigAt the area map's signature then):
   kBoxTravel = the squad, still in that area and outside that box, travels on (the game itself would travel it there unseen and not
   wake it); kBoxLend = inside the box: held and lent (the borrower checks its live box again at the build); kBoxNone = no box, another
   area, or the map around it changed since (sigNow != sigAt): the ordinary rule - held and lent, so the borrower answers afresh. */
enum { kBoxNone = 0, kBoxTravel = 1, kBoxLend = 2 };
inline int LendBoxDecide(int haveBox, int bx, int by, unsigned int sigAt, int ax, int ay, unsigned int sigNow, const WakeBox& b, float x, float z)
{
    if (haveBox != 1 || b.ok != 1 || ax != bx || ay != by || sigNow != sigAt) return kBoxNone;
    return WakeBoxHas(b, x, z) == 1 ? kBoxLend : kBoxTravel;
}
/* ==== T-1122 gate 2 (2a+, owner 1612 a, 2026-11-13; run T2327, F-T2327-RAIDGATES): NO STRIP BETWEEN THE TWO BOXES ====
   On a side facing an unloaded neighbour the borrower's box is pulled in kWakeEdge + kWakeMargin (176) and the lender's outer box only
   kWakeEdge - kWakeMargin (144). A squad whose spot lay between got 'outside my wake box' from the borrower (kBoxTravel) and 'my own game
   would wake it' from the lender (kHostStay), and neither game acted (T2327: one Hive squad held for the rest of the run). SpotAlongLine
   moves the spot along the squad's line toward its raid's target (tx, tz):
     into 1 (the lend road): to the first point of the box b on that line - the near edge of the borrower's box. The lender writes that
       spot into its own squad and lends it (the borrower still checks its live box at the build);
     into 0 (the grace road, after a RETURN): to kSpotOutStep past the last point of b on that line - just outside the lender's own outer
       box, so its own game does not wake the squad, which travels on.
   T-1122 fold 1 (review MED 2, manager decision 2026-11-13): the move's DEPTH straight across the strip (perpendicular to the box edge
   it crosses) is at most maxMove (kSpotMoveMax: the strip's width, twice the margin) and its LENGTH along the line at most kSpotAlongMax
   (twice that: lines up to 60 degrees from straight across); never past the target. *ox / *oz the new spot, *moved the length along
   the line (on kSpotTooFar the length it would have needed). Any answer but kSpotMoved leaves the spot as it is and the squad is held
   as before; the lender asks again at its next off-screen update tick (LendSpotMove runs every tick for a held strip squad: a lock, the
   campaign map walk and a target read each time - cheap; the raidSpot counters are per tick). */
const float kSpotMoveMax = 32.0f;
const float kSpotAlongMax = 64.0f;
const float kSpotOutStep = 1.0f;
enum { kSpotMoved = 0, kSpotAlready = 1, kSpotNoBox = 2, kSpotNoLine = 3, kSpotMissed = 4, kSpotTooFar = 5 };
inline int SpotAlongLine(const WakeBox& b, int into, float x, float z, float tx, float tz, float maxMove, float* ox, float* oz, float* moved, float maxAlong = kSpotAlongMax)
{
    *ox = x; *oz = z; *moved = 0.0f;
    if (b.ok != 1 || x != x || z != z) return kSpotNoBox;
    const int inside = WakeBoxHas(b, x, z);
    if ((into != 0 && inside == 1) || (into == 0 && inside == 0)) return kSpotAlready;
    if (tx != tx || tz != tz) return kSpotNoLine;
    const double dx = (double)tx - (double)x, dz = (double)tz - (double)z;
    const double len = std::sqrt(dx * dx + dz * dz);
    if (!(len > 0.001)) return kSpotNoLine;
    const double ux = dx / len, uz = dz / len;
    /* the line's stretch inside the box: t in [lo, hi] (the slab method, each axis in turn) */
    double lo = -1.0e30, hi = 1.0e30; int loAx = -1, hiAx = -1;   /* the axis whose edge the line enters / leaves by */
    const double p[2] = { (double)x, (double)z }, u[2] = { ux, uz }, a0[2] = { (double)b.x0, (double)b.z0 }, a1[2] = { (double)b.x1, (double)b.z1 };
    for (int i = 0; i < 2; ++i)
    {
        if (u[i] > -1.0e-9 && u[i] < 1.0e-9) { if (p[i] < a0[i] || p[i] > a1[i]) return kSpotMissed; continue; }
        double t1 = (a0[i] - p[i]) / u[i], t2 = (a1[i] - p[i]) / u[i];
        if (t1 > t2) { const double k = t1; t1 = t2; t2 = k; }
        if (t1 > lo) { lo = t1; loAx = i; }
        if (t2 < hi) { hi = t2; hiAx = i; }
    }
    if (lo > hi) return kSpotMissed;
    double t;
    if (into != 0) { if (lo < 0.0) return kSpotMissed; t = lo; }       /* the box lies behind the squad */
    else { if (hi < 0.0) return kSpotMissed; t = hi + (double)kSpotOutStep; }
    if (t > len) return kSpotMissed;                                     /* the target comes first: the squad never gets there */
    const int ax = into != 0 ? loAx : hiAx;
    const double depth = ax >= 0 ? t * (u[ax] < 0.0 ? -u[ax] : u[ax]) : t;   /* straight across the edge it crosses */
    if (depth > (double)maxMove || t > (double)maxAlong) { *moved = (float)t; return kSpotTooFar; }
    float nx = (float)(p[0] + ux * t), nz = (float)(p[1] + uz * t);
    if (into != 0)
    {   /* on the edge: a float's last bit must not leave it outside */
        if (nx < b.x0) nx = b.x0; if (nx > b.x1) nx = b.x1;
        if (nz < b.z0) nz = b.z0; if (nz > b.z1) nz = b.z1;
    }
    if (WakeBoxHas(b, nx, nz) != (into != 0 ? 1 : 0)) return kSpotMissed;
    *ox = nx; *oz = nz; *moved = (float)t;
    return kSpotMoved;
}

/* ==== T-1122 attempt 2 (owner 1619 a, 2026-11-13; run T2334, F-T2334-LENDLOOP): A LENT SQUAD'S PEOPLE ARE PLACED WELL PAST THE STRIP ====
   T2334: the lend moved only the squad's marker; its people stayed where the borrower's engine had last put them away (5 u inside the
   area's edge, inside the engine's 160 u strip), so the borrower's engine put every copy away again seconds after its wake (it tests the
   squad's own stored position, ActivePlatoon +0xB4/+0xBC - cooplo::EngineKeepsAt's comment; T-1355) and the squad bounced between the two games. Owner 1619 a: move the PEOPLE with the
   marker, to a point well past the strip - the release rule's margin, kLendDeep = the strip + the keep margin (410 u from the area's
   edge on a side whose neighbour is not loaded); judge by where the people stand (at the borrower's put-away that is now the engine's own
   sleeping spot - owner 1684, T-1355 design E, LentKeepRule); the borrower keeps it until it is that margin outside.
   DeepOf: the borrower's wake box (WakeBoxOf) with each pulled-in side moved to kLendDeep from the area's edge (a loaded side keeps its
   edge - the engine does not narrow it). The deep box lies inside the wake box (410 > 176) and inside the area. */
const float kLendDeep = cooplo::kEngineEdgeStrip + cooplo::kKeepMarginUnits;   /* 410: the release rule's keep line (liveowner.h) */
const float kLendPlaceMax = 2.0f * kLendDeep;                                   /* 820: the longest move of a lend's people (the lender's spot move and the borrower's placement)
                                                                                   - from the area's edge to kLendDeep along a line up to 60 degrees from straight across; F463's fault was 5.6 km */
const int kLendPlaceFrames = 600;                                               /* frames a woken copy's people are waited for (steady) before its placement is given up (~10 s) */
const int kLendPlaceWatchFrames = 300;                                          /* frames after the placement in which people made later are placed too (~5 s) */
const float kLendSteadyEps = 0.01f;                                             /* a person 'the same two frames running': within this on each axis */
/* fold 1 (review MED 1): THE BORROWER'S GIVE-BACK LINE - its sleeping spot (T-1355 design E: the engine's own spot; before, its people's centre) this far OUTSIDE the areas it holds (the release rule's keep
   margin, owner 1619 a: "a margin past the edge"). With the lend line (kLendDeep inside) the two lines are 660 u apart. */
const float kLendGiveBack = cooplo::kKeepMarginUnits;                           /* 250 */
const unsigned long kLendShortBackoffMs = 60000;                                /* fold 2: the lender's first wait after a short return from one holder (LendShortQuietMs) */
inline WakeBox DeepOf(const WakeBox& b)
{
    WakeBox d;
    if (b.ok != 1) return d;
    const float cx = 0.5f * (b.x0 + b.x1), cz = 0.5f * (b.z0 + b.z1);
    const float ax0 = (float)std::floor((kAreaOrigin + cx) / kAreaSize) * kAreaSize - kAreaOrigin, az0 = (float)std::floor((kAreaOrigin + cz) / kAreaSize) * kAreaSize - kAreaOrigin;
    d = b;
    if (b.x0 > ax0 + 1.0f) d.x0 = ax0 + kLendDeep;
    if (b.x1 < ax0 + kAreaSize - 1.0f) d.x1 = ax0 + kAreaSize - kLendDeep;
    if (b.z0 > az0 + 1.0f) d.z0 = az0 + kLendDeep;
    if (b.z1 < az0 + kAreaSize - 1.0f) d.z1 = az0 + kAreaSize - kLendDeep;
    if (d.x0 > d.x1 || d.z0 > d.z1) return WakeBox();
    return d;
}
/* THE PEOPLE'S CENTRE - the mean of an awake squad's members' positions (Character+0x48); where a lend's people are placed from at its first
   wake. NOT the point the engine's keep test reads (that is the squad's stored position, ActivePlatoon +0xB4/+0xBC - T-1355).
   n positions (x, z); a NaN one is skipped. Returns the number used (0 = none: *cx / *cz untouched). */
inline int PeopleCentre(const float* xs, const float* zs, int n, float* cx, float* cz)
{
    double sx = 0.0, sz = 0.0; int k = 0;
    for (int i = 0; i < n && xs != 0 && zs != 0; ++i)
    {
        if (xs[i] != xs[i] || zs[i] != zs[i]) continue;
        sx += (double)xs[i]; sz += (double)zs[i]; ++k;
    }
    if (k == 0) return 0;
    *cx = (float)(sx / (double)k); *cz = (float)(sz / (double)k);
    return k;
}
/* THE LEND'S TARGET POINT, from the people's centre (cx, cz) toward the raid's target (tx, tz; haveTarget 0 = none), in the deep box d:
   kTpAlready = the centre is inside d already (no move); kTpLine = the first point of d on the line toward the target, 1 u inside;
   kTpNearest = no target, the line misses d, or the target comes before d (a target in the strip): the nearest point of d to the
   centre, 1 u inside (the squad then walks to its target itself); kTpTooFar = the move would be longer than kLendPlaceMax (*moved its
   length) - refused, nothing moved; kTpNoBox = no deep box or a NaN centre. d lies inside the holder's area, so the point never leaves it. */
enum { kTpAlready = 0, kTpLine = 1, kTpNearest = 2, kTpTooFar = 3, kTpNoBox = 4 };
inline int LendTargetPoint(const WakeBox& d, float cx, float cz, int haveTarget, float tx, float tz, float* ox, float* oz, float* moved)
{
    *ox = cx; *oz = cz; *moved = 0.0f;
    if (d.ok != 1 || cx != cx || cz != cz) return kTpNoBox;
    if (WakeBoxHas(d, cx, cz) == 1) return kTpAlready;
    float nx = cx, nz = cz, along = 0.0f; int how = kTpNearest;
    if (haveTarget != 0 && tx == tx && tz == tz)
    {
        float lx = cx, lz = cz, ld = 0.0f;
        if (SpotAlongLine(d, 1, cx, cz, tx, tz, 1.0e30f, &lx, &lz, &ld, 1.0e30f) == kSpotMoved)
        {   /* 1 u further along, so a float's last bit never leaves it on the box's line */
            const double dx = (double)tx - (double)cx, dz = (double)tz - (double)cz, len = std::sqrt(dx * dx + dz * dz);
            if (len > (double)ld + 1.0) { lx = (float)((double)cx + dx / len * ((double)ld + 1.0)); lz = (float)((double)cz + dz / len * ((double)ld + 1.0)); ld += 1.0f; }
            if (WakeBoxHas(d, lx, lz) == 1) { nx = lx; nz = lz; along = ld; how = kTpLine; }
        }
    }
    if (how == kTpNearest)
    {
        nx = cx < d.x0 + 1.0f ? d.x0 + 1.0f : (cx > d.x1 - 1.0f ? d.x1 - 1.0f : cx);
        nz = cz < d.z0 + 1.0f ? d.z0 + 1.0f : (cz > d.z1 - 1.0f ? d.z1 - 1.0f : cz);
        if (nx < d.x0 || nx > d.x1) nx = 0.5f * (d.x0 + d.x1);
        if (nz < d.z0 || nz > d.z1) nz = 0.5f * (d.z0 + d.z1);
        along = (float)std::sqrt(((double)nx - cx) * ((double)nx - cx) + ((double)nz - cz) * ((double)nz - cz));
    }
    if (along > kLendPlaceMax) { *moved = along; return kTpTooFar; }
    *ox = nx; *oz = nz; *moved = along;
    return how;
}
/* one member's new spot: the target point plus its own offset from the people's centre (the formation kept) */
inline void PlaceMember(float px, float pz, float cx, float cz, float nx, float nz, float* ox, float* oz) { *ox = nx + (px - cx); *oz = nz + (pz - cz); }
/* how far (x, z) lies outside box b (0 inside; the larger of the two axes' distances); -1 = no box or a NaN spot */
inline float OutsideBy(const WakeBox& b, float x, float z)
{
    if (b.ok != 1 || x != x || z != z) return -1.0f;
    const float ox = x < b.x0 ? b.x0 - x : (x > b.x1 ? x - b.x1 : 0.0f), oz = z < b.z0 ? b.z0 - z : (z > b.z1 ? z - b.z1 : 0.0f);
    return ox > oz ? ox : oz;
}
/* ==== T-1122 attempt 2 fold 1 (owner 1619 a; review T1122a2 MED 1): A TRUE TWO-LINE HYSTERESIS ON WHERE THE PEOPLE STAND ====
   THE LEND LINE (the lender, B): a lend's people stand kLendDeep inside the borrower's area (DeepOf its wake box) before the borrower's
   engine judges them - the single rule. The lender moves its sleeping squad's spot (its people, asleep) there before the lend's write when
   it knows the borrower's box (LendLineDecide kBoxShallow / kBoxTravel, LendTargetPoint, at most kLendPlaceMax); when it does not, the
   borrower's placement at the lend's FIRST wake does it (the people are built where the spot was).
   THE GIVE-BACK LINE (the borrower, A): the copy is kept - asleep or awake - while A holds an area within kLendGiveBack of its sleeping
   spot (owner 1684, T-1355 design E: the engine's own spot, the stored squad position its put-away tested - inside such an area, or less
   than kLendGiveBack outside its edge); it is given back only when that spot is kLendGiveBack or more outside every area A holds, or A holds none of them - fold 2: a raid's end is no reason (LentKeepRule at a put-away,
   LentAsleepRule each tick). Measured against the AREA edges A holds, never a wake box.
   NO LOOP: the placement runs once per lend (its first wake), never at a re-wake of a kept copy, so nothing but the squad's own walk
   moves it once it is lent; a give-back past the line leaves the squad kLendGiveBack outside the area, and the lender lends it again
   only when its own travel brings it back into the area - every lend after a give-back costs the squad's own walk of at least
   kLendDeep + kLendGiveBack (660 u) out and kLendGiveBack back. */
/* how far (x, z) lies outside area (ax, ay): the larger of the two axes' distances past its edges; 0 inside (the edge counts inside);
   -1 = a NaN spot or an area off the grid */
inline float AreaOutsideBy(int ax, int ay, float x, float z)
{
    if (x != x || z != z || ax < 0 || ay < 0 || ax > 63 || ay > 63) return -1.0f;
    const float x0 = (float)ax * kAreaSize - kAreaOrigin, z0 = (float)ay * kAreaSize - kAreaOrigin, x1 = x0 + kAreaSize, z1 = z0 + kAreaSize;
    const float ox = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0.0f), oz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0.0f);
    return ox > oz ? ox : oz;
}
inline int AreaIndexOf(float v) { return (int)std::floor((kAreaOrigin + v) / kAreaSize); }
/* held[9]: 1 = this game holds that area, for the 3 x 3 areas around (x, z)'s own area (index (dy + 1) * 3 + (dx + 1); kLendGiveBack is
   far less than an area, so no farther area can be nearer). The distance of (x, z) outside the nearest held one (0 inside one); -1 = none
   held, or a NaN spot */
inline float HeldOutsideBy(const int* held, float x, float z)
{
    if (held == 0 || x != x || z != z) return -1.0f;
    const int ax = AreaIndexOf(x), ay = AreaIndexOf(z);
    float best = -1.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (held[(dy + 1) * 3 + (dx + 1)] != 1) continue;
            const float o = AreaOutsideBy(ax + dx, ay + dy, x, z);
            if (o >= 0.0f && (best < 0.0f || o < best)) best = o;
        }
    return best;
}
/* THE BORROWER AT ITS ENGINE'S PUT-AWAY of a lent copy (owner 1684, T-1355 design E - it supersedes owner 1619 a's people's centre here:
   haveSpot: the engine's own sleeping spot was read just after the sleep (LentSleepSpotUsable); heldOutside: HeldOutsideBy at that spot;
   fold 2: whether its raid runs is no reason): kSlKeep = kept here asleep where the engine put it, nothing written (this game's own
   off-screen update travels it on, and its engine wakes it if it comes back into the wake box, moving its people by spot minus anchor);
   otherwise given back (RETURN at that spot) with the reason */
enum { kSlKeep = 0, kSlPastMargin = 1, kSlNotHeld = 2, kSlRaidEnded = 3, kSlNoSpot = 4, kSlCount = 5 };
inline int LentKeepRule(int haveSpot, float heldOutside)   /* kSlRaidEnded is no longer answered (fold 2) - kept for the REPORT's order */
{
    if (haveSpot != 1) return kSlNoSpot;
    if (!(heldOutside >= 0.0f)) return kSlNotHeld;
    if (heldOutside >= kLendGiveBack) return kSlPastMargin;
    return kSlKeep;
}
inline const char* LentSleepWhy(int k)
{
    switch (k)
    {
    case kSlKeep:       return "kept (its sleeping spot inside this game's areas or less than the give-back line outside them)";
    case kSlPastMargin: return "its sleeping spot is the give-back line (250 u) or more outside every area this game holds";
    case kSlNotHeld:    return "this game holds no area within the give-back line of its sleeping spot (or it lies in an area loaded here that another game holds)";
    case kSlRaidEnded:  return "its raid ended here (not a reason since fold 2)";
    case kSlNoSpot:     return "its engine's sleeping spot was not read after the sleep";
    default:            return "?";
    }
}
/* T-1355 design E (owner 1684): the engine's own sleeping spot of a lent copy just put away - the sleeping object's +0x64 (Platoon::deactivate
   0x7EE910 builds it at the squad's stored position ActivePlatoon +0xB4, the point its keep test just rejected; decomp_7ee910:30-31, 66-70).
   sleepRead: the sleeping object was read after the sleep; x, y, z: its spot. 1 = usable (all finite, not the unset 0,0,0); 0 = not read */
inline int LentSpotFinite(float f) { union { float f; unsigned int u; } b; b.f = f; return (b.u & 0x7F800000u) != 0x7F800000u ? 1 : 0; }
inline int LentSleepSpotUsable(int sleepRead, float x, float y, float z)
{
    if (sleepRead != 1) return 0;
    if (LentSpotFinite(x) == 0 || LentSpotFinite(y) == 0 || LentSpotFinite(z) == 0) return 0;
    return (x != 0.0f || y != 0.0f || z != 0.0f) ? 1 : 0;
}
/* T-1355: THE WAKE LINE of a kept lent copy (sleeping spot, anchor, people's centre, squad position, grace) - said for the first
   kLentWakeLineMax wakes of a lend, then only counted, so a copy that cycles cannot flood the log. shownSoFar: lines said for this lend */
const int kLentWakeLineMax = 6;
inline int LentWakeLineShown(int shownSoFar) { return (shownSoFar >= 0 && shownSoFar < kLentWakeLineMax) ? 1 : 0; }
/* T-1355 fold 1 (review 1): WHAT A WAKE OF A LENT COPY DOES TO ITS PLACEMENT ENTRY (the borrower's g_lentPlace). placed: placements tried
   this lend; line: this wake owes a wake line (a KEPT copy's, under the cap); haveEntry: an entry is there already - a first placement still
   watching for people made later (~5 s), or a wake line still waiting for steady people. kWeNew = a new entry (the lend's first placement,
   replacing one not finished - as before T-1355; or a wake line with no entry there); kWeAddLine = the entry is KEPT and only the wake-line
   duty added to it, so a first placement's watch for late people goes on (before T-1355 a re-wake left the entry alone); kWeNone =
   nothing (a re-wake that owes no line) */
enum { kWeNone = 0, kWeNew = 1, kWeAddLine = 2 };
inline int LentWokeEntry(int placed, int line, int haveEntry)
{
    if (placed == 0) return kWeNew;
    if (line == 0) return kWeNone;
    return haveEntry != 0 ? kWeAddLine : kWeNew;
}
/* T-1355 attempt 2 (F1032): MOVING A SLEEPING SQUAD. The engine's off-screen update (0x7EDBF0, decomp_7edbf0:214-217; the same tail bytes at 1.0.68 0x7EED96)
   runs the sleeping object's travel object (UnloadedPlatoon +0x58, AbstractMovementBase: position +0xC4, destination +0xDC, road follower
   +0xF8) and copies its position into the sleeping spot +0x64 - so a write of the spot alone is undone at the next update. The move writes
   Platoon +0x48, the spot +0x64 (and the anchor +0x80 when asked) and the travel position +0xC4, then the engine's own stop (vtable +0x98:
   destination := position, speed 0, road follower cleared) and set-destination (vtable +0x90: the old destination again - a far one plans
   its road from the new position). sleepingOk: the sleeping object read; travelOk: its travel object read; tableOk: that object's table and
   both slots inside the game's own image. kSmMoved = all of it; kSmSpotOnly = no usable travel object - the spot (and anchor) written as
   before, travel untouched; kSmNoSleeping = no sleeping object - +0x48 alone; kSmFault = a fault (crash-guarded) */
/* Review fold (2026-11-15): classOk - the object's table IS AbstractMovementBase's for the running version (the address table's
   AbstractMovementBaseVt row), else kSmOtherClass: the spot (and anchor) written as before, counted apart. kSmMovedStill - moved, but the
   squad stood still (its old destination within kSleepMoveStillU of its old travel position - a fresh or engine-stopped travel object,
   decomp_7ee4a0:53-54, :67): the destination is NOT set again (it would plan a road back to the old spot); stop leaves it at the new one */
enum { kSmFault = -1, kSmNoSleeping = 0, kSmMoved = 1, kSmSpotOnly = 2, kSmOtherClass = 3, kSmMovedStill = 4, kSmCodes = 6 };
inline int SleepMovePlan(int sleepingOk, int travelOk, int classOk, int tableOk)
{
    if (sleepingOk != 1) return kSmNoSleeping;
    if (travelOk != 1) return kSmSpotOnly;
    if (classOk != 1) return kSmOtherClass;
    if (tableOk != 1) return kSmSpotOnly;
    return kSmMoved;
}
/* the old destination is set again only when it lies more than kSleepMoveStillU from the old travel position (d: destination minus
   position); NaN = not set again. 1 = set again (kSmMoved), 0 = it stood still (kSmMovedStill) */
const float kSleepMoveStillU = 1.0f;
inline int SleepMoveDestAgain(float dx, float dy, float dz) { const float d2 = dx * dx + dy * dy + dz * dz; return (d2 == d2 && d2 > kSleepMoveStillU * kSleepMoveStillU) ? 1 : 0; }
/* 1 = the position was written (the caller's old 'moved' - SetSleepPosPod's 1): every code but a fault */
inline int SleepMoveWritten(int code) { return (code == kSmMoved || code == kSmMovedStill || code == kSmSpotOnly || code == kSmOtherClass || code == kSmNoSleeping) ? 1 : 0; }
inline const char* SleepMoveWhy(int code)
{
    switch (code)
    {
    case kSmMoved: return "moved (spot, travel position, stop, destination set again)";
    case kSmMovedStill: return "moved (spot, travel position, stop; it stood still - no destination set)";
    case kSmSpotOnly: return "spot only (no usable travel object - the next off-screen update may undo it)";
    case kSmOtherClass: return "spot only (its travel object is not AbstractMovementBase for this game version - the next off-screen update may undo it)";
    case kSmNoSleeping: return "platoon position only (no sleeping object)";
    case kSmFault: return "FAULTED (nothing more written)";
    default: return "?";
    }
}
/* the road follower (+0xF8) is active unless its state +0x18 is 0, 3 or 4 (0x45A240 / 1.0.68 0x45A5E0); while active the step takes the
   position from the route, never from +0xC4. haveFollower: +0xF8 not null */
inline int SleepRoadActive(int haveFollower, int state) { return (haveFollower != 0 && state != 0 && state != 3 && state != 4) ? 1 : 0; }
/* the follow-up: the spot after the next off-screen update is within kSleepMoveHeldU of the point moved to (one update's own travel is
   short; F1032's undo put it back on its old travel line, hundreds of units away). NaN = not held */
const float kSleepMoveHeldU = 100.0f;
inline int SleepMoveHeld(float dx, float dz) { const float d2 = dx * dx + dz * dz; return (d2 == d2 && d2 <= kSleepMoveHeldU * kSleepMoveHeldU) ? 1 : 0; }
/* T-1385 (F1142 / F1143; replaces T-1355 attempt 2 (b)'s put-away memory): THE ANCHOR OF A SQUAD WOKEN FROM A RECORD. The engine relocates a
   woken squad's people by spot minus anchor (F456); a sleep serialises the people where they stand and sets the anchor to the squad's
   stored position (0x7EE910:30-31, 66-70), and a sleeping squad travels on (its spot moves, its anchor stays) - so a record's people
   stand around the anchor of the copy that was saved, not around the record's spot. Every save of a sleeping squad's record carries
   that RAW anchor and the sleeping spot read with it as the squad record's vector fields (coopown::kRecAnchorKey / kRecAnchorSpotKey, read
   live at the save), so a game that wakes the record sets its own anchor from them (AnchorPick): kBaRecord. A record without them (an
   older record, or one whose anchor could not be read at its save):
   kBaLendPos - the anchor is the spot it is woken at (the lend's position, the RETURN's, the record's), wake move 0, today's rule.
   kBaUnread: the bytes did not parse (or not one squad record) - the same fallback. recAnchor: coopown::RecordAnchorOf's answer
   (kRecAnchorCarried 1, kRecAnchorNone 0, kRecAnchorUnread -1). */
enum { kBaLendPos = 0, kBaRecord = 1, kBaUnread = 2, kBaCount = 3 };
inline int LentBuildAnchor(int recAnchor)
{
    if (recAnchor == 1) return kBaRecord;
    return recAnchor == 0 ? kBaLendPos : kBaUnread;
}
/* T-1385 fold 2: does a record's wake shift the saved anchor by (its point - the saved spot)? Only while the record's position is still the
   one its full write gave it (posAt not after writtenAt: a member's position at the save, not the sleeping spot). Once a position has been
   published since the write (posAt > writtenAt - PublishSleepingPositions: the squad's CURRENT sleeping spot), the saved anchor is used as
   it is, so the wake moves the people by that spot minus the saved anchor - the engine's own wake of a squad that travelled on asleep. */
inline int AnchorShifts(long long posAt, long long writtenAt) { return posAt > writtenAt ? 0 : 1; }
/* the anchor a record's wake uses - out[3]. ref: the point today's rule uses as the anchor (the point the wake treats the record's people as
   standing around: the lend's position, the record's position, a kept sleeping spot). kBaRecord with shift 1: the record's raw anchor moved
   by (ref - the spot saved with it), so the wake moves the people by (its spot - ref) + (saved spot - saved anchor) - the engine's own
   pending move at the save, however far the record's position (a member's) is from the spot (T-1385 fold 1). kBaRecord with shift 0: the
   raw anchor (AnchorShifts: a published spot; the RETURN, which moves the people to the spot it sets). Otherwise (fallback): ref itself,
   wake move 0 at ref. */
inline int AnchorPick(int recAnchor, const float* saved, const float* savedSpot, const float* ref, int shift, float* out)
{
    const int k = LentBuildAnchor(recAnchor);
    if (k == kBaRecord && saved != 0 && savedSpot != 0)
    {
        for (int i = 0; i < 3; ++i) out[i] = saved[i] + (shift != 0 ? ref[i] - savedSpot[i] : 0.0f);
        return kBaRecord;
    }
    out[0] = ref[0]; out[1] = ref[1]; out[2] = ref[2];
    return k == kBaRecord ? kBaUnread : k;   /* a carried anchor with no point handed over is not used */
}
inline const char* LentBuildAnchorWhy(int k)
{
    switch (k)
    {
    case kBaLendPos: return "missing - the lend spot (the record carries no people anchor - wake move 0)";
    case kBaRecord: return "from the record (the anchor its people were saved around - the wake moves them to the spot)";
    case kBaUnread: return "unread - the lend spot (the record's bytes did not parse - wake move 0)";
    default: return "?";
    }
}
/* T-1385 (secondary guard): THE POINT A WOKEN LENT COPY'S PLACEMENT IS JUDGED FROM - the squad's stored position (ActivePlatoon +0xB4/+0xBC,
   the point the engine's keep test reads; squadRead 1 and no NaN) when the copy's build anchor came from its record (buildAnchor kBaRecord -
   its people were moved to the lend's spot by the wake): kPfSquad; otherwise the people's centre (cx, cz): kPfCentre (fold 2: a copy built
   without the record's anchor has its people where the bytes put them, and +0xB4 is the lend's spot - judging from it would call them in
   place wherever they stand). Every person is then moved by the same offset (target point minus this point: PlaceMember with this point as
   the centre) - formation kept. */
enum { kPfCentre = 0, kPfSquad = 1 };
inline int PlaceFrom(int squadRead, int buildAnchor, float px, float pz, float cx, float cz, float* fx, float* fz)
{
    if (squadRead == 1 && buildAnchor == kBaRecord && px == px && pz == pz) { *fx = px; *fz = pz; return kPfSquad; }
    *fx = cx; *fz = cz; return kPfCentre;
}
/* T-1355 fold 1 (review 3): the sleeping spot and anchor before a wake - kPreRead read on the main thread just before the engine's wake;
   kPreUnreadable woken on the main thread but its sleeping object could not be read; kPreOffThread woken off the main thread (the wake is
   queued and its handling run later on the main thread, after the engine woke it - nothing left to read). Words for the wake line */
enum { kPreUnreadable = 0, kPreRead = 1, kPreOffThread = 2 };
inline int LentPreState(int offThread, int readOk) { return offThread != 0 ? kPreOffThread : (readOk == 1 ? kPreRead : kPreUnreadable); }
inline const char* LentPreWhy(int s)
{
    switch (s)
    {
    case kPreRead:       return "read just before the wake";
    case kPreUnreadable: return "its sleeping object could not be read just before the wake";
    case kPreOffThread:  return "woken off the main thread - handled later on the main thread, nothing left to read";
    default:             return "?";
    }
}
/* MED 3: the people are steady - the same n people (the caller compares who they are) at the same positions as the last frame, each axis
   within kLendSteadyEps (the engine makes them and relocates them over frames) */
inline int LentSteady(const float* xs, const float* zs, int n, const float* lx, const float* lz, int ln)
{
    if (n <= 0 || n != ln || xs == 0 || zs == 0 || lx == 0 || lz == 0) return 0;
    for (int i = 0; i < n; ++i)
    {
        if (xs[i] != xs[i] || zs[i] != zs[i]) return 0;
        const float dx = xs[i] - lx[i], dz = zs[i] - lz[i];
        if (dx > kLendSteadyEps || dx < -kLendSteadyEps || dz > kLendSteadyEps || dz < -kLendSteadyEps) return 0;
    }
    return 1;
}
/* the placement at a lend's first wake (tp: LendTargetPoint's answer; heldAtPoint: this game holds the target point's area - LOW) */
enum { kPdMove = 0, kPdAlready = 1, kPdTooFar = 2, kPdNoBox = 3, kPdNotHeld = 4 };
inline int LentPlaceDecide(int tp, int heldAtPoint)
{
    if (tp == kTpAlready) return kPdAlready;
    if (tp == kTpTooFar) return kPdTooFar;
    if (tp != kTpLine && tp != kTpNearest) return kPdNoBox;
    return heldAtPoint == 1 ? kPdMove : kPdNotHeld;
}
/* HIGH 1: one person's height at its new spot - the ground there (groundRead 1: the engine's terrain height read at that spot), never the
   height it had at its old spot; 0 = not read: that person is not moved (counted) */
inline int LentMemberY(int groundRead, float groundY, float* y)
{
    if (groundRead != 1 || groundY != groundY) return 0;
    *y = groundY;
    return 1;
}
/* T-1122 attempt 2 fold 2 (re-check LOW): the group moves whole or not at all - 1 only when every one of the n people's ground heights was read
   (ok[i]: LentMemberY's answer for person i) */
inline int LentGroundAll(const int* ok, int n)
{
    if (ok == 0 || n <= 0) return 0;
    for (int i = 0; i < n; ++i) if (ok[i] != 1) return 0;
    return 1;
}
/* THE LENDER'S LEND LINE against the borrower's REFUSED box b (LendBoxDecide's arguments): kBoxNone / kBoxTravel as LendBoxDecide;
   kBoxShallow = inside the borrower's wake box but short of its lend line (DeepOf): moved to the line, then lent; kBoxLend = on or past it */
const int kBoxShallow = 3;
inline int LendLineDecide(int haveBox, int bx, int by, unsigned int sigAt, int ax, int ay, unsigned int sigNow, const WakeBox& b, float x, float z)
{
    const int d = LendBoxDecide(haveBox, bx, by, sigAt, ax, ay, sigNow, b, x, z);
    if (d != kBoxLend) return d;
    return WakeBoxHas(DeepOf(b), x, z) == 1 ? kBoxLend : kBoxShallow;
}
/* THE LENDER AT A RETURN (outsideLendArea: AreaOutsideBy(the lend's area, the RETURN's spot - the people's centre)): 1 = a SHORT return -
   the spot is less than kLendGiveBack outside the lend's area: the borrower gave it back for a reason other than the give-back line (it no
   longer holds the area, the raid ended there, no centre read, or it never woke there); 0 = past the line (or no spot: not counted).
   A short return is moved to the lend line and lent again while the borrower holds the area (MED 2: never held still), after its wait. */
inline int LendReturnShort(float outsideLendArea) { return (outsideLendArea >= 0.0f && outsideLendArea < kLendGiveBack) ? 1 : 0; }
/* T-1122 attempt 2 fold 2 (re-check MED: the safety stop froze the squad unseen): NO STOP. The lender's short returns are counted per area AND
   holder (outsideLendArea: by the areas that holder holds - a return into another area it holds is short too); before that holder is lent the
   squad again it waits LendShortQuietMs - kLendShortBackoffMs after the first, doubled after each further one, at most 16 times it (16 min):
   a time, so it always ends. The count and its wait clear when the area's holder changes (LendShortReset - a hand-over: the new holder is lent
   it at once), when a return is past the give-back line, or when its spot is the give-back line outside that holder's areas (LendShortClears,
   a walk out). */
inline unsigned long LendShortQuietMs(int shortReturns)
{
    if (shortReturns <= 0) return 0;
    const int k = shortReturns - 1 < 4 ? shortReturns - 1 : 4;
    return kLendShortBackoffMs << k;
}
/* 1 = the short-return count no longer applies: the squad is in another area (ax, ay) than the one counted, or the area's holder (holderSlot;
   < 0 = not another game's area now / unknown: kept) is another than the one counted (shortSlot) */
inline int LendShortReset(int shortAx, int shortAy, int shortSlot, int ax, int ay, int holderSlot)
{
    if (shortAx != ax || shortAy != ay) return 1;
    return (holderSlot >= 0 && holderSlot != shortSlot) ? 1 : 0;
}
inline int LendShortClears(float outsideShortArea) { return outsideShortArea >= kLendGiveBack ? 1 : 0; }
/* ==== T-1122 gate 1 (1a, owner 1612 a, 2026-11-13; run T2327, F-T2327-RAIDGATES): THE WAR-FORCE ROUTINE SKIPS DECISION 34 ====
   Decision 34's notebook gate (towngen.cpp CreateJudged) holds back every creation for a town while the notebook lists a group of that
   town this game has not placed - it exists to stop the engine inventing extra residents. A creation the engine's war-force routine asked
   for (raidnow::IsRaidCaller: raids and caravan visits) is a new campaign squad, not a resident: it passes that gate and goes on to the
   holder gate and GateAllows, as in one-player play. townHeld: the notebook's answer for the town (1 = it holds the town). */
enum { kNbGoOn = 0, kNbHold = 1, kNbRaidPasses = 2 };
inline int NotebookGate(int raidCaller, int townHeld)
{
    if (townHeld == 0) return kNbGoOn;
    return raidCaller != 0 ? kNbRaidPasses : kNbHold;
}
/* THE BORROWER'S SLEEPING COPY, each tick while its lend is open: kept, or given back to its lender from its sleeping state (T-1122 lend by
   reference: the world record its give-back writes, named by RETURN).
   held: this game holds the copy's area; inWake: its spot is inside this game's wake box NOW (the live loaded set - a neighbour this
   game dropped after the build shrinks it); awake: it woke here before; sinceBuiltMs: since its build. kAsleepNeverWoke is the last
   resort for a cause not known: inside the box the game wakes a copy in ~5 s. */
enum { kAsleepKeep = 0, kAsleepNotHeld = 1, kAsleepOutsideWake = 2, kAsleepNeverWoke = 3 };
inline int LentAsleepGiveBack(int held, int inWake, int awake, unsigned long sinceBuiltMs)
{
    if (held != 1) return kAsleepNotHeld;
    if (inWake != 1) return kAsleepOutsideWake;
    if (awake == 0 && sinceBuiltMs > kLendAsleepMaxMs) return kAsleepNeverWoke;
    return kAsleepKeep;
}
/* T-1122 attempt 2 fold 1 (review MED 1): THE ASLEEP COPY EACH TICK - the give-back line at its sleeping spot (heldOutside:
   HeldOutsideBy at its sleeping spot, the engine's own spot from the put-away (T-1355 design E: nothing is written there) moved since only
   by this game's own off-screen travel; -1 none held). No clock decides a kept copy: it is kept until it crosses the line (this game's off-screen update
   travels it there - also through the band outside its areas, LentBandUpdate), it stands in another game's area, or this game no longer
   holds near it (fold 2: a raid's end is no reason); it
   wakes only by this game's engine, inside the wake box. kAsleepNeverWoke stays the last resort for a cause not known, as before attempt
   2: a copy that never woke since its build (everWoke 0), inside the wake box (inWake 1), past kLendAsleepMaxMs. */
const int kAsleepOtherArea = 4;   /* fold 2 (re-check LOW): its spot is in an area this game does not hold that another game holds or has loaded (otherArea 1) -
                                     this game's own off-screen update does not travel it there (LentBandUpdate), so it is given back, never left still unseen */
inline int LentAsleepRule(float heldOutside, int otherArea, int everWoke, int inWake, unsigned long sinceBuiltMs)
{
    if (!(heldOutside >= 0.0f)) return kAsleepNotHeld;
    if (otherArea == 1) return kAsleepOtherArea;
    if (heldOutside >= kLendGiveBack) return kAsleepOutsideWake;
    if (everWoke == 0 && inWake == 1 && sinceBuiltMs > kLendAsleepMaxMs) return kAsleepNeverWoke;
    return kAsleepKeep;
}
/* fold 2 (re-check LOW): a KEPT copy that woke here before (everWoke), asleep inside this game's wake box (inWake) longer than the game takes to
   wake one there (asleepMs past kLendAsleepMaxMs): the unknown cause of T1967 - counted and said once per copy, never given back by a clock */
inline int LentKeptNoWake(int kept, int everWoke, int inWake, unsigned long asleepMs) { return (kept == 1 && everWoke != 0 && inWake == 1 && asleepMs > kLendAsleepMaxMs) ? 1 : 0; }

/* T-1122 fix (H-T1122-emptylend, proven by run T1957): WHAT A LEND SAVES. A raid squad the campaign makes asleep has an EMPTY own
   sleeping container - no records at all; its state lives only in the shared type-0x22 record it points to (+0x40), outside the
   container - so the container alone saved to 12 bytes and the borrower's build stopped at step 6 (no type-0x1E state record).
   walked: the container walk (1 = walked, else broken or faulted); n1E: its type-0x1E records; people: its person records (type 0x24,
   < 0 unread); recType: the +0x40 record's type (< 0 none or unread); recOutside: 1 = the walk did not find +0x40 in the container.
   kLendSrcRecord = the +0x40 record's fields as ONE 0x1E record in a scratch container, saved: a container with no people has nothing
   the record lacks, and the live record beats a 0x1E an earlier RETURN left there (the borrower builds the template's people at the
   wake, as for any squad with no person records); kLendSrcContainer = the container as it is (its people, with its own 0x1E);
   kLendSrcNone = no state record to lend (an unreadable container, or people with no 0x1E): not lent - the borrower would refuse it. */
enum { kLendSrcContainer = 0, kLendSrcRecord = 1, kLendSrcNone = 2 };
inline int LendStateSource(int walked, int n1E, int people, int recType, int recOutside)
{
    if (walked != 1 || people < 0) return kLendSrcNone;
    if (people == 0 && recType == 0x22 && recOutside != 0) return kLendSrcRecord;
    if (n1E > 0) return kLendSrcContainer;
    return kLendSrcNone;
}

/* A LEND'S SERIAL (T-1122 review fold): unique across the lender's restarts - a borrower keeps an ended lend's key (lender|id|serial)
   to say its ending again, and a count restarting at 1 met a stale ending. The high 16 bits are a per-process draw (`seed`), the low 16
   a count that carries into them, so serials only grow within a process (ReturnApplies' `> stampHad`); 0 is never one. */
inline unsigned int LendSerialNext(unsigned int last, unsigned int seed)
{
    if (last == 0) { unsigned int hi = seed & 0xFFFF0000u; if (hi == 0) hi = 0x10000u; return hi | 1u; }
    const unsigned int n = last + 1u;
    return n != 0 ? n : (0x10000u | 1u);
}

/* THE ROSTER GAP (T-1122 review fold): 1 = the other side of a lend has been missing from the roster for kLendGoneMs. there: present
   (StoreRosterSlotInWorld != 0); *seen / *since: the row's open gap, cleared while it is present. A blip shorter than that leaves the
   lend open on both games - a lender that resumed at a blip dropped the copy's RETURN / DIED as stale (two squads), and a borrower that
   gave its copy up at a blip never said an ending. */
inline int GoneFor(int there, int* seen, unsigned long* since, unsigned long nowMs)
{
    if (there != 0) { *seen = 0; *since = 0; return 0; }
    if (*seen == 0) { *seen = 1; *since = nowMs; return 0; }
    return (unsigned long)(nowMs - *since) >= kLendGoneMs ? 1 : 0;
}

/* THE BORROWER'S KEEP ANSWER for a lent copy (store.cpp detour_isPersistent, ANY THREAD; T-1122 review fold). The engine deletes an awake
   raid squad with no raid and no home once it leaves the loaded area (Read 1.0.65) - never putting it to sleep - so a copy answered "do
   not keep" was destroyed at this game's area edge, its destroy said DIED and the lender removed a living raid. ended: its lend ended here
   (RETURN / DIED said, or the lender left); living: LivingMembersPod (>0 living, 0 none living, -2 no awake members, -1 not read).
   1 = kept: the engine puts it to sleep, RaidLentAtSleep gives it back by RETURN and the 2 s sweep removes it. 0 = the engine's own
   discard: nobody living (the destroy says DIED), or an ended lend (nothing more to give back). -2 / -1 are kept - the sleep judges. */
/* T-1122 attempt 2 fold 2 (re-check HIGH): raidOver - the engine ended this copy's raid campaign here (phase 2, or gone from its war
   manager's list); engineKeeps - the engine's own keep answer (Platoon::isPersistentSquad, the original 0x7EB9D0). A raid squad is made
   with its kept byte 0, so with its campaign over the engine answers "not kept" (decomp_7eb9d0: the campaign lookup is its only keep;
   decomp_9d1790: the campaign's removal sets every squad's map entry to it to none - Read): in one-player play it is discarded at its next
   put-away, and asleep it is woken at its 60 game-s lifetime and discarded outside a loaded area (decomp_7edbf0 / 6ba810, Read in
   05-findings). No walk home is given (9d1790 only unlinks and deletes the campaign). A copy whose raid is over here takes that answer, so
   this game's engine does with it what one-player play does; its destroy says DIED and its lender removes its own squad, which left its
   campaign at the RAID_END. */
inline int LentKeep(int ended, int living, int raidOver = 0, int engineKeeps = 1)
{
    if (ended != 0) return 0;
    if (living == 0) return 0;
    if (raidOver != 0) return engineKeeps != 0 ? 1 : 0;
    return 1;
}
/* the ending said when the engine puts a lent copy away (living read on the main thread just before the sleep): nobody living (0) or no
   awake members (-2) is DIED - a RETURN with no people would rebuild the template's people on the lender; otherwise RETURN */
inline int LentSleepEnding(int living) { return (living == 0 || living == -2) ? kLendKindDied : kLendKindReturn; }

/* EVERY ANSWER HAS AN ENDING (the lender). ev: an answer kind (kLendKind*), or kEvPeerGone (the borrower left the world), or kEvSilent
   (the ask / status gap passed with no answer). sameSerial: the answer names the row's open serial (a stale one changes nothing).
   linkUp: the world-server link. Never resumes while TAKEN and the link is up, unless the borrower says the copy is not there. */
enum { kEvPeerGone = 100, kEvSilent = 101 };
enum { kActNone = 0, kActTaken = 1, kActResume = 2, kActApplyReturn = 3, kActRemove = 4, kActReask = 5, kActStillTaken = 6 };
inline int LendAnswer(int state, int ev, int sameSerial, int linkUp)
{
    if (state == kLendIdle) return kActNone;                         /* nothing open: a late or duplicate answer */
    if (ev == kEvPeerGone) return kActResume;                         /* the borrower's copy left with it */
    if (ev == kEvSilent) return linkUp != 0 ? kActReask : (state == kLendAsked ? kActResume : kActNone);
    if (sameSerial == 0) return kActNone;
    if (ev == kLendKindReturn) return kActApplyReturn;
    if (ev == kLendKindDied) return kActRemove;
    if (ev == kLendKindUnknown) return kActResume;                    /* the borrower knows nothing of it: no copy there */
    if (ev == kLendKindKeepOwn) return kActResume;                    /* T-1122 lend by reference: its copy's state was not written there - the own squad resumes as lent */
    if (state == kLendAsked)
    {
        if (ev == kLendKindTaken) return kActTaken;
        if (ev == kLendKindRefused) return kActResume;
        return kActNone;
    }
    if (ev == kLendKindTaken) return kActStillTaken;                  /* TAKEN: the borrower's answer to a status ask (or a duplicate TAKEN) is a
                                                                         fresh answer - the copy is still lent there; the ask timer restarts (T-1122 F2,
                                                                         run T1974: counted stale, A re-asked every 30 s as 'lent and silent') */
    return kActNone;                                                  /* TAKEN: a late REFUSED changes nothing */
}
/* the lender's new state after an action */
inline int LendStateAfter(int state, int act)
{
    if (act == kActTaken) return kLendTaken;
    if (act == kActResume || act == kActApplyReturn || act == kActRemove) return kLendIdle;
    return state;
}

/* THE BORROWER'S ANSWER to a LEND. full: a lend naming its world record (T-1122 lend by reference; 0 = a status ask); active: a live lent copy here under this key;
   ended: this key's lend ended here (its outcome kept to say again); worldReady / holds: this game has a running world and holds the
   area at the squad's position; inWake: the squad's spot against this game's wake box now (1 inside, 0 outside - refused
   kRefOutsideWake with the box, -1 no box: the area is not loaded here - refused kRefNotHolder); capFull: the lent table is full. */
enum { kBorrowBuild = 0, kBorrowSayTaken = 1, kBorrowSayEnd = 2, kBorrowRefuse = 3, kBorrowUnknown = 4 };
inline int BorrowDecide(int full, int active, int ended, int worldReady, int holds, int inWake, int capFull, int* refWhy)
{
    *refWhy = kRefNone;
    if (active != 0) return kBorrowSayTaken;
    if (ended != 0) return kBorrowSayEnd;
    if (full == 0) return kBorrowUnknown;
    if (worldReady == 0) { *refWhy = kRefNoWorld; return kBorrowRefuse; }
    if (holds != 1) { *refWhy = kRefNotHolder; return kBorrowRefuse; }
    if (inWake < 0) { *refWhy = kRefNotHolder; return kBorrowRefuse; }
    if (inWake != 1) { *refWhy = kRefOutsideWake; return kBorrowRefuse; }
    if (capFull != 0) { *refWhy = kRefCapFull; return kBorrowRefuse; }
    return kBorrowBuild;
}

/* THE MESSAGE: u32 'RLD1' | u8 kind | u32 serial | i32 lenderSlot | u8 idLen + id | per kind:
     LEND:    u8 full | u8 facLen + faction | u8 sqLen + squad template | f32 x y z | u8 hasTarget | f32 tx ty tz
              | u8 hasCamp | (hasCamp only, game protocol 208) u8 kLen + campaign kind | i32 phase | u64 t18 t20
              | (full only - T-1122 lend by reference, game protocol 219) u8 idLen + world id | u64 writtenAt | u32 writer slot: the world record the
              lender's write at the lend made - never the squad's bytes (they rode here before 219, capped at 60000: a raid squad never fit)
     REFUSED: u8 why | (why kRefOutsideWake only, game protocol 206) f32 x0 z0 x1 z1 - the borrower's wake box
     RETURN: f32 x y z | u8 form 1 | u8 idLen + world id | u64 writtenAt | u32 writer slot - the world record the holder's put-away wrote (T-1122
             RETURN by reference: the awake lend's since game protocol 218, the sleeping lend's since 219; form 0, the bytes, is gone and refused)
     TAKEN / DIED / UNKNOWN / KEEPOWN / WITHDRAWN: nothing
     AWAKE (T-1122 awake hand, game protocol 215): the LEND layout with full 0 (no bytes) | u32 leader - the hand-over message's key uid */
struct LendMsg
{
    int kind; unsigned int serial; int lender; std::string id;
    int full; std::string faction, squad; float x, y, z; int hasTarget; float tx, ty, tz; int why;
    WakeBox box;   /* REFUSED kRefOutsideWake: the borrower's wake box for the lend's area */
    int hasCamp; std::string campKind; int campPhase; unsigned long long campT18, campT20;   /* LEND (protocol 208): the lender's raid campaign - its kind's name, phase +0xC0 and times +0x18 / +0x20 (raw) */
    unsigned int leader;   /* AWAKE: the uid the ordinary hand-over message is keyed by (XFER's leader, RELEASE's key uid); never 0 */
    int byRef; std::string recId; long long recAt; int recWriter;   /* RETURN form 1 (T-1122 RETURN by reference, game protocol 218) - and a full LEND's recId / recAt / recWriter
                                                                       (T-1122 lend by reference, 219: the record the lender's write made; byRef is RETURN's form byte only): the world
                                                                                            record the holder's sleep save wrote at the put-away - its id, writtenAt, writer
                                                                                            slot (the record's owner field); no bytes (T-1122 review fold 5: no seq - the holder's is unstamped and the owner never used it) */
    LendMsg() : kind(0), serial(0), lender(-1), full(0), x(0), y(0), z(0), hasTarget(0), tx(0), ty(0), tz(0), why(0), hasCamp(0), campPhase(-1), campT18(0), campT20(0), leader(0), byRef(0), recAt(0), recWriter(-1) {}
};
inline void LendPutU32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFF)); }
inline void LendPutF(std::vector<char>* b, float f) { unsigned int v; std::memcpy(&v, &f, 4); LendPutU32(b, v); }
inline void LendPutU64(std::vector<char>* b, unsigned long long v) { LendPutU32(b, (unsigned int)(v & 0xFFFFFFFFull)); LendPutU32(b, (unsigned int)(v >> 32)); }
inline int LendPutStr(std::vector<char>* b, const std::string& s) { if (s.size() > 255) return 0; b->push_back((char)s.size()); b->insert(b->end(), s.begin(), s.end()); return 1; }
inline int EncodeLend(const LendMsg& m, std::vector<char>* out)
{
    out->clear();
    if (m.kind < kLendKindLend || m.kind > kLendKindWithdrawn || m.id.empty()) return 0;
    if (m.kind == kLendKindAwake && m.leader == 0) return 0;
    LendPutU32(out, kLendMagic); out->push_back((char)m.kind); LendPutU32(out, m.serial); LendPutU32(out, (unsigned int)m.lender);
    if (!LendPutStr(out, m.id)) return 0;
    if (m.kind == kLendKindLend || m.kind == kLendKindAwake)
    {   /* T-1122 awake hand: AWAKE is the LEND layout with full 0 (no saved bytes), then the leader's uid */
        const int full = (m.kind == kLendKindLend && m.full != 0) ? 1 : 0;
        if (full != 0 && (m.recId.empty() || m.recId.size() > 255 || m.recAt <= 0)) return 0;   /* T-1122 lend by reference: a lend names its world record */
        out->push_back((char)full);
        if (!LendPutStr(out, m.faction) || !LendPutStr(out, m.squad)) return 0;
        LendPutF(out, m.x); LendPutF(out, m.y); LendPutF(out, m.z);
        out->push_back((char)(m.hasTarget != 0 ? 1 : 0)); LendPutF(out, m.tx); LendPutF(out, m.ty); LendPutF(out, m.tz);
        out->push_back((char)(m.hasCamp != 0 ? 1 : 0));
        if (m.hasCamp != 0) { if (m.campKind.empty() || !LendPutStr(out, m.campKind)) return 0; LendPutU32(out, (unsigned int)m.campPhase); LendPutU64(out, m.campT18); LendPutU64(out, m.campT20); }
        if (full != 0) { LendPutStr(out, m.recId); LendPutU64(out, (unsigned long long)m.recAt); LendPutU32(out, (unsigned int)m.recWriter); }   /* the record it names - never bytes */
        if (m.kind == kLendKindAwake) LendPutU32(out, m.leader);
    }
    else if (m.kind == kLendKindRefused)
    {
        out->push_back((char)m.why);
        if (m.why == kRefOutsideWake) { if (m.box.ok != 1) return 0; LendPutF(out, m.box.x0); LendPutF(out, m.box.z0); LendPutF(out, m.box.x1); LendPutF(out, m.box.z1); }
    }
    else if (m.kind == kLendKindReturn)
    {   /* form 1 (T-1122 RETURN by reference): the record's id and stamp - never bytes; every RETURN since game protocol 219 (form 0, the bytes, is gone) */
        if (m.byRef == 0 || m.recId.empty() || m.recId.size() > 255 || m.recAt <= 0) return 0;
        LendPutF(out, m.x); LendPutF(out, m.y); LendPutF(out, m.z);
        out->push_back((char)1);
        LendPutStr(out, m.recId); LendPutU64(out, (unsigned long long)m.recAt); LendPutU32(out, (unsigned int)m.recWriter);
    }
    return 1;
}
struct LendRd
{
    const char* p; size_t n, at; int ok;
    LendRd(const char* p_, size_t n_) : p(p_), n(n_), at(0), ok(1) {}
    unsigned int U32() { if (!ok || at + 4 > n) { ok = 0; return 0; } unsigned int v = 0; for (int i = 0; i < 4; ++i) v |= (unsigned int)(unsigned char)p[at + i] << (8 * i); at += 4; return v; }
    int U8() { if (!ok || at + 1 > n) { ok = 0; return 0; } return (int)(unsigned char)p[at++]; }
    float F() { unsigned int v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
    unsigned long long U64() { const unsigned long long lo = U32(); const unsigned long long hi = U32(); return lo | (hi << 32); }
    std::string Str() { const int l = U8(); if (!ok || at + (size_t)l > n) { ok = 0; return std::string(); } std::string s(p + at, p + at + l); at += l; return s; }
};
/* a position may be used: not NaN and not infinite (the exponent bits not all set - no float library, the same on every compiler) */
inline int LendFinite(float f) { unsigned int v; std::memcpy(&v, &f, 4); return ((v >> 23) & 0xFFu) != 0xFFu ? 1 : 0; }
/* 1 = a whole message of a known kind, nothing left over, every position finite (T-1122 review fold: a NaN or infinite position is
   refused - it would be written into a squad's sleeping position) */
inline int DecodeLend(const char* p, size_t n, LendMsg* m)
{
    if (p == 0) return 0;
    *m = LendMsg();   /* every field from this message only: a reused LendMsg keeps nothing (a status ask after a full LEND had the old bytes) */
    LendRd r(p, n);
    if (r.U32() != kLendMagic) return 0;
    m->kind = r.U8(); m->serial = r.U32(); m->lender = (int)r.U32(); m->id = r.Str();
    if (!r.ok || m->kind < kLendKindLend || m->kind > kLendKindWithdrawn || m->id.empty()) return 0;
    if (m->kind == kLendKindLend || m->kind == kLendKindAwake)
    {
        m->full = r.U8(); m->faction = r.Str(); m->squad = r.Str();
        m->x = r.F(); m->y = r.F(); m->z = r.F();
        m->hasTarget = r.U8(); m->tx = r.F(); m->ty = r.F(); m->tz = r.F();
        m->hasCamp = r.U8();
        if (m->hasCamp == 1) { m->campKind = r.Str(); m->campPhase = (int)r.U32(); m->campT18 = r.U64(); m->campT20 = r.U64(); if (m->campKind.empty()) return 0; }
        if (m->kind == kLendKindAwake) { if (m->full != 0) return 0; m->leader = r.U32(); if (!r.ok || m->leader == 0) return 0; }   /* T-1122 awake hand */
        else if (m->full == 1) { m->recId = r.Str(); m->recAt = (long long)r.U64(); m->recWriter = (int)r.U32(); if (!r.ok || m->recId.empty() || m->recAt <= 0) return 0; }   /* T-1122 lend by reference */
        if (m->full > 1 || m->hasTarget > 1 || m->hasCamp > 1) return 0;
    }
    else if (m->kind == kLendKindRefused)
    {
        m->why = r.U8();
        if (m->why == kRefOutsideWake)
        {   /* the box: finite, not inverted */
            m->box.x0 = r.F(); m->box.z0 = r.F(); m->box.x1 = r.F(); m->box.z1 = r.F(); m->box.ok = 1;
            if (!LendFinite(m->box.x0) || !LendFinite(m->box.z0) || !LendFinite(m->box.x1) || !LendFinite(m->box.z1) || m->box.x0 > m->box.x1 || m->box.z0 > m->box.z1) return 0;
        }
    }
    else if (m->kind == kLendKindReturn)
    {
        m->x = r.F(); m->y = r.F(); m->z = r.F();
        m->byRef = r.U8();
        if (m->byRef != 1) return 0;   /* form 0 (the bytes) is gone since game protocol 219; any other form is unknown */
        m->recId = r.Str(); m->recAt = (long long)r.U64(); m->recWriter = (int)r.U32(); if (!r.ok || m->recId.empty() || m->recAt <= 0) return 0;
    }
    if (!LendFinite(m->x) || !LendFinite(m->y) || !LendFinite(m->z) || !LendFinite(m->tx) || !LendFinite(m->ty) || !LendFinite(m->tz)) return 0;
    return (r.ok && r.at == n) ? 1 : 0;
}

/* RETURN applies a newer local version: the returned record is taken only for the row's open serial, and only once (the row then goes
   idle); `stampHad` is the serial last applied to this squad (0 none) - an older or equal one is not applied again */
inline int ReturnApplies(int state, unsigned int openSerial, unsigned int msgSerial, unsigned int stampHad)
{
    return (state != kLendIdle && msgSerial == openSerial && msgSerial > stampHad) ? 1 : 0;
}

/* ==== T-1122 save fold (manager 2026-11-03, design (b)): THE LENT COPIES A SAVE HOLDS ====
   The engine's faction save writes every squad (0x372920 -> squad save 0x372DB0; design read 2026-11-03), so a lent copy here goes into
   the borrower's save; after a reload it is an ordinary squad, its lender is told 'unknown lend' and resumes its own - the raider twice.
   Beside each save the borrower writes the lends it holds (kLentFile), and at that save's load each listed copy is removed (store.cpp
   RaidLentLoadTick). THE FILE: u32 'RLF1' | u32 count | per entry: u32 lenderSlot | u32 serial | u8 facLen + faction | u8 idLen + id. */
const char* const kLentFile = "multiplayer.lent";
const unsigned int kLentFileMagic = 0x31464C52u;   /* 'RLF1' */
const size_t kLentFileCap = 256;                   /* entries read at most (the borrower keeps 64 rows) */
struct LentEntry { int lender; unsigned int serial; std::string faction, id; LentEntry() : lender(-1), serial(0) {} };
/* 1 = *out the file (none listed: *out empty - no file is written); 0 = an entry cannot be written (an empty id or faction, a name over 255) */
inline int EncodeLentFile(const std::vector<LentEntry>& v, std::string* out)
{
    out->clear();
    if (v.empty()) return 1;
    if (v.size() > kLentFileCap) return 0;
    std::vector<char> b;
    LendPutU32(&b, kLentFileMagic); LendPutU32(&b, (unsigned int)v.size());
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].id.empty() || v[i].faction.empty()) return 0;
        LendPutU32(&b, (unsigned int)v[i].lender); LendPutU32(&b, v[i].serial);
        if (!LendPutStr(&b, v[i].faction) || !LendPutStr(&b, v[i].id)) return 0;
    }
    out->assign(b.begin(), b.end());
    return 1;
}
/* 1 = a whole file: its magic, the count it says (1..kLentFileCap), every entry with an id and a faction, nothing left over */
inline int DecodeLentFile(const char* p, size_t n, std::vector<LentEntry>* out)
{
    out->clear();
    if (p == 0) return 0;
    LendRd r(p, n);
    if (r.U32() != kLentFileMagic) return 0;
    const unsigned int count = r.U32();
    if (!r.ok || count == 0 || count > kLentFileCap) return 0;
    std::vector<LentEntry> v;
    for (unsigned int i = 0; i < count; ++i)
    {
        LentEntry e;
        e.lender = (int)r.U32(); e.serial = r.U32(); e.faction = r.Str(); e.id = r.Str();
        if (!r.ok || e.faction.empty() || e.id.empty()) return 0;
        v.push_back(e);
    }
    if (r.at != n) return 0;
    out->swap(v);
    return 1;
}
/* 1 = the file beside a save is that save's: written (fileFt, FILETIME) at or after its quick.save (saveFt); 0 = none, or an older save's
   left behind (the rule of the discovered outposts, baseshow::DiscoveredFileCurrent) */
inline int LentFileCurrent(unsigned long long fileFt, unsigned long long saveFt)
{
    return (fileFt != 0ULL && saveFt != 0ULL && fileFt >= saveFt) ? 1 : 0;
}
/* THE MATCH RULE (the load's removal): 1 = this squad is the listed lent copy and is removed - the listed id AND the listed faction, not
   one of this game's own raid squads (ownRaid), not a copy of a lend open now (lentNow: the lender lent it again after the load), kept
   byte 0 (a lent copy is built with 0), no running campaign aimed at this game's own player's base (madeHere 1 = made here; -1 not read:
   a lent copy is in no campaign here), no binding and no world record of its id (a lent copy never gets either - a squad with one is a
   world squad of the same id). */
inline int LentCopyRemovable(int idSame, int facSame, int ownRaid, int lentNow, int keptByte, int madeHere, int bound, int hasRecord)
{
    return (idSame != 0 && facSame != 0 && ownRaid == 0 && lentNow == 0 && keptByte == 0 && madeHere != 1 && bound == 0 && hasRecord == 0) ? 1 : 0;
}
/* the entry's answer from the squads of its id in its faction: kLentRemove = exactly one passes the match rule; kLentAmbiguous = more than
   one passes (none is removed - which is the copy cannot be told); kLentRefused = some of that id, none passes; kLentWait = none of that
   id yet (the load's squads may not be listed yet) - kLentNotFound once waitedMs reaches kLentLoadWaitMs */
const unsigned long kLentLoadWaitMs = 30000;
const int kLentWait = 0, kLentRemove = 1, kLentAmbiguous = 2, kLentRefused = 3, kLentNotFound = 4;
inline int LentEntryAct(int seen, int passing, unsigned long waitedMs)
{
    if (passing == 1) return kLentRemove;
    if (passing > 1) return kLentAmbiguous;
    if (seen > 0) return kLentRefused;
    return waitedMs >= kLentLoadWaitMs ? kLentNotFound : kLentWait;
}

/* ==== T-1122 redesign (b) (owner 1465 a, 2026-11-04): THE RAID CAMPAIGN OF A LENT COPY, AND ITS ENDING ====
   Run T1987 (Confirmed): the woken copy with a plain walk order passed B's character 69 u away with combat off - a walk order carries
   no raid. In single player the raid CAMPAIGN drives its squads (design read 2026-11-04 and the builder's re-read, Read on 1.0.65):
   the war-manager start 0x9C8960 makes the campaign through its kind (kind vt+0x18), lists it in the war manager (faction +0x88:
   +0x68 array, +0x70 count), then FILLS it (0x9C4170 - which makes new squads at the faction's towns, so that start is never the
   borrower's road); each squad joins through the campaign's vt+0x70 (decomp_9c4170:177/193/240/292; 0x9C3850 by its body, row
   RaidCampaignJoin: the squad into the faction's squad->campaign map, the campaign's squad list +0xC8 / +0xD0, its size added to
   +0x38, a new leader (+0x80 hand) when the flag is set or none leads); 0x9C3A90 (row RaidCampaignDrop) takes one out (map value
   cleared, list entry removed, a new leader when it led). The update 0x9C6220 runs phase +0xC0: 0 travelling until the leading squad
   is near the target, then 1 (the assault) or 2 when the kind has no assault (kind +0x110), stamping the times +0x18 / +0x20; each
   phase runs the kind's own AI ('pt1 AI', 'pt2 AI', 'retreat AI' - decomp_9c6f70:297-314); the target is the campaign's hand +0x40.
   LEND carries the lender's campaign block; RAID_END (87) tells the lender how the borrower's campaign ended for its copy.
   Probe P242 (runs T2002 / T2009, Confirmed): the engine joins a raid's squads through the campaign's vt+0x70 on the main thread, the
   campaign already listed in its war manager, its kind at +0x10 one of the war manager's kinds (+0xD8 count, +0xE0 array). The borrower
   runs a campaign of its own for the copy (owner 1479 a): the engine's start without its fill (store.cpp BorrowCampStart). */
const unsigned int kMsgRaidEnd = 87;              /* MSG_RAID_END (net/transport.h, liveenvelope.h kInnerRaidEnd and store.cpp assert it) */
const unsigned int kRaidEndMagic = 0x31444E52u;   /* 'RND1' */
enum { kCampTravel = 0, kCampAssault = 1, kCampOver = 2 };
enum { kEndNone = 0, kEndArrived = 1, kEndDefeated = 2, kEndRetreated = 3, kEndReturned = 4 };

/* the lender's campaign block is usable by the borrower: present, a kind named, a phase the campaign still runs (an over campaign is
   never started again for a copy) */
inline int CampBlockUsable(int hasCamp, size_t kindLen, int phase)
{
    return (hasCamp == 1 && kindLen > 0 && (phase == kCampTravel || phase == kCampAssault)) ? 1 : 0;
}
/* THE BORROWER'S CAMPAIGN FOR A WOKEN COPY. blockOk: CampBlockUsable; kindFound: a campaign kind of that name in the copy's faction's
   war manager here; targetFound: the object at the lend's target found here; joined: the copy is in a campaign here already. */
enum { kCampGo = 0, kCampHave = 1, kCampNoBlock = 2, kCampNoKind = 3, kCampNoTarget = 4, kCampNoOrigin = 5, kCampNoRows = 6 };
/* originFound: the raiders' town the engine's start would take (RaidCampaignOrigin 0x9C2E00, not given up - decomp_9c8960:52-58); rowsOk:
   the four address rows the campaign needs are bound (join, take-out, origin, list grow) */
inline int BorrowCampDecide(int blockOk, int kindFound, int targetFound, int joined, int originFound = 1, int rowsOk = 1)
{
    if (joined != 0) return kCampHave;
    if (rowsOk == 0) return kCampNoRows;
    if (blockOk == 0) return kCampNoBlock;
    if (kindFound == 0) return kCampNoKind;
    if (targetFound == 0) return kCampNoTarget;
    if (originFound == 0) return kCampNoOrigin;
    return kCampGo;
}
inline const char* BorrowCampWhy(int d)
{
    return d == kCampGo ? "made" : d == kCampHave ? "already in a campaign" : d == kCampNoBlock ? "the lend carried no running campaign"
         : d == kCampNoKind ? "no campaign kind of that name in the raiders' war manager here" : d == kCampNoTarget ? "no town at the lend's target here"
         : d == kCampNoOrigin ? "the engine gives no raiders' town to start from" : d == kCampNoRows ? "an address row is not bound" : "?";
}
/* the target here: the town nearest the lend's target point (TownListNearest, any faction) within kCampTargetMatchU on the ground plane
   (both games read the same town's vt+0x40 spot), owned by another faction than the raiders' own */
const float kCampTargetMatchU = 300.0f;
inline int CampTargetMatch(float dist, int ownerIsRaiders) { return (dist >= 0.0f && dist <= kCampTargetMatchU && ownerIsRaiders == 0) ? 1 : 0; }
/* THE CAMPAIGN'S TIMES (+0x18 / +0x20, doubles on the game clock - the constructor stamps both with now, disassembly 0x9D39EB-0x9D3A04
   on 1.0.65). The borrower takes the lender's stamp when it reads as a time no later than its own stamp at the making (both games run one
   world clock - Inferred); anything else keeps its own. */
inline double CampRawD(unsigned long long r) { double d; std::memcpy(&d, &r, 8); return d; }
inline unsigned long long CampDRaw(double d) { unsigned long long r; std::memcpy(&r, &d, 8); return r; }
inline int CampTimeOk(double d) { return (d == d && d >= 0.0 && d < 1.0e9) ? 1 : 0; }
inline unsigned long long CampTimeTake(unsigned long long lendRaw, unsigned long long freshRaw)
{
    const double l = CampRawD(lendRaw), f = CampRawD(freshRaw);
    if (CampTimeOk(f) == 0 || CampTimeOk(l) == 0 || l > f) return freshRaw;
    return lendRaw;
}
/* the borrower's own end of its campaign (the copy taken out): phase 2 and its +0x18 stamp moved kCampOverBack back, so the engine's own
   update removes and deletes it at its next run (decomp_9c6220:241-246: phase 2, now - +0x18 above 3.0 -> 0x9D1790) - no handler, so no
   follow-up raid */
const double kCampOverBack = 1000.0;
inline unsigned long long CampOverStamp(unsigned long long t18Raw)
{
    const double t = CampRawD(t18Raw);
    return CampDRaw(CampTimeOk(t) != 0 ? t - kCampOverBack : -kCampOverBack);
}
/* the phase the borrower reads for its campaign: listed (and still its kind) - its own phase; gone from the war manager's list - over (the
   engine's update removes a campaign only from phase 2); not read - -1 */
inline int CampPhaseRead(int readOk, int listed, int phase)
{
    if (readOk == 0) return -1;
    if (listed == 0) return kCampOver;
    return (phase == kCampTravel || phase == kCampAssault || phase == kCampOver) ? phase : -1;
}
/* EVERY ENDING OF A LENT COPY'S CAMPAIGN (07-bug-classes.md :227 - a state is not added without each of its endings). ev: what
   happened to the copy here; living: LivingMembersPod (>0 living, 0 none, -2 no awake members, -1 not read); phaseWas / phaseNow:
   the borrower's campaign phase at the last read and now (kCamp*, -1 not read). The outcome said by RAID_END (kEndNone = nothing). */
enum { kCeTick = 0, kCeSleep = 1, kCeReturn = 2, kCeWorldClose = 3, kCeLinkLost = 4, kCeAllDead = 5 };
inline int RaidEndOf(int ev, int living, int phaseWas, int phaseNow)
{
    if (ev == kCeLinkLost) return kEndNone;     /* no road to the lender: its own peer-gone ending (LendAnswer kEvPeerGone) resumes the squad there */
    if (ev == kCeAllDead) return kEndDefeated;
    if (ev == kCeSleep || ev == kCeReturn || ev == kCeWorldClose) return (living == 0 || living == -2) ? kEndDefeated : kEndReturned;
    if (ev != kCeTick || phaseNow < 0 || phaseWas < 0 || phaseNow == phaseWas) return kEndNone;
    if (phaseNow == kCampAssault && phaseWas == kCampTravel) return kEndArrived;
    if (phaseNow == kCampOver) return living == 0 ? kEndDefeated : (phaseWas == kCampTravel ? kEndArrived : kEndRetreated);   /* travel -> over: a kind with no assault arrived */
    return kEndNone;
}
/* the copy leaves the borrower's campaign on every ending except a phase change (the campaign keeps it while it runs) */
inline int RaidEndLeaves(int ev) { return (ev == kCeSleep || ev == kCeReturn || ev == kCeWorldClose || ev == kCeLinkLost || ev == kCeAllDead) ? 1 : 0; }
/* THE LENDER'S APPLY of a RAID_END. state: its row's lend state; sameKey: the message names the row's open serial, from its borrower.
   Defeated: the lender removes its own squad (the DIED road - the engine's own removal, after which its campaign counts the squad
   gone, Read in T-1122's hand-over read). THE RAID IS OVER THERE (T-1122 review fold 1): retreated, or arrived with the borrower's
   campaign over (phase 2 - a kind with no assault): the lender takes its squad out of its own campaign through the engine's take-out
   (RaidCampaignDrop) and ends that campaign when no squad is left in it, so a later lend carries no campaign and the beaten raid does
   not come back. Arrived with the campaign still running (its attack stage, phase 1 - not proof the assault started: T2077) and
   returned (the copy left the borrower's area
   with its raid unfinished): noted; the squad stays in its campaign (a returned copy's bytes come by RETURN). phase: RAID_END's. */
enum { kEndActNone = 0, kEndActNote = 1, kEndActRemove = 2, kEndActLeave = 3 };
inline int RaidEndApply(int state, int sameKey, int outcome, int phase = -1)
{
    if (state != kLendTaken || sameKey == 0) return kEndActNone;
    if (outcome == kEndDefeated) return kEndActRemove;
    if (outcome == kEndRetreated || (outcome == kEndArrived && phase == kCampOver)) return kEndActLeave;
    if (outcome == kEndArrived || outcome == kEndReturned) return kEndActNote;
    return kEndActNone;
}
/* the lender's own campaign after its squad's take-out (fold 1): ended by phase 2 only when no squad is left in it and it still runs */
inline int LenderCampEnds(int took, int squadsLeft, int phase) { return (took == 1 && squadsLeft == 0 && (phase == kCampTravel || phase == kCampAssault)) ? 1 : 0; }
/* T-1122 attempt 2 fold 2 (re-check HIGH): the lender's row says the raid is over at the borrower - the RAID_END it applied takes the squad out
   of its campaign (RaidEndApply kEndActLeave: retreated, or arrived with the campaign over). Such a squad is never lent or moved again. */
inline int LendRaidOver(int outcome, int phase) { return (outcome == kEndRetreated || (outcome == kEndArrived && phase == kCampOver)) ? 1 : 0; }
/* fold 5: a 24-slot copy of a campaign class's table is right only when the slot after the 24th does not point into the game's code */
inline int CampVtUsable(int slotsRead, int nextInCode) { return (slotsRead == 1 && nextInCode == 0) ? 1 : 0; }
/* fold 2 (decomp of the war manager's save 1.0.65 0x9C59C0, Read): the save writes EVERY campaign in its list ('num actives' = +0x70,
   0x9C5BB0 loop, each by 0x9C4CB0) and every squad->campaign map node whose campaign is set (0x9C5EC0-0x9C5F87: the squad's handle and
   the campaign's id +0x08). A reload would bring the borrowed campaign back with the class's own ending handlers and the copy's map
   entry. So around the game's save the borrower's running campaign leaves the list and the copy's node value is 0 (the save skips such a
   node, 0x9C5ECD), both put back when the save returns. Only on the main thread (the world stands still there - F-15587). */
inline int CampHideAtSave(int made, int ended, int listed, int onMain) { return (made == 1 && ended == 0 && listed == 1 && onMain == 1) ? 1 : 0; }
/* RAID_END: u32 'RND1' | u32 serial | i32 lenderSlot | u8 idLen + id | u8 outcome (kEnd*, 1-4) | i32 phase (the borrower's campaign
   phase, -1 none) | u8 endedBy (T-1122 attempt 3, game protocol 226: the engine's ending handler that really ran on the borrower's copy -
   kEndByNone 0 none, kEndByEnded 1 'has ended' (vt+0x10), kEndByDefeated 2 'defeated' (vt+0x18); store.cpp CampHandlerTake) | i32 defeatByte
   (T-1122 attempt 3 fold 1, still 226 - unmerged: the borrower's copy's campaign byte +0x3C read when its 'defeated' ran, 0-255; -1 none / not
   read. 'defeated' (decomp_9cb050:218-247) picks the loss follow-up (kind +400) or a retry of the same kind (kind +0x138) by it, so the lender
   writes it into its own campaign just before it runs 'defeated' there) */
enum { kEndByNone = 0, kEndByEnded = 1, kEndByDefeated = 2 };
const int kDefeatByteNone = -1;
struct RaidEndMsg
{
    unsigned int serial; int lender; std::string id; int outcome; int phase; int endedBy; int defeatByte;
    RaidEndMsg() : serial(0), lender(-1), outcome(0), phase(-1), endedBy(kEndByNone), defeatByte(kDefeatByteNone) {}
};
inline int EncodeRaidEnd(const RaidEndMsg& m, std::vector<char>* out)
{
    out->clear();
    if (m.id.empty() || m.outcome < kEndArrived || m.outcome > kEndReturned || m.phase < -1 || m.phase > kCampOver || m.endedBy < kEndByNone || m.endedBy > kEndByDefeated
        || m.defeatByte < kDefeatByteNone || m.defeatByte > 255) return 0;
    LendPutU32(out, kRaidEndMagic); LendPutU32(out, m.serial); LendPutU32(out, (unsigned int)m.lender);
    if (!LendPutStr(out, m.id)) { out->clear(); return 0; }
    out->push_back((char)m.outcome); LendPutU32(out, (unsigned int)m.phase); out->push_back((char)m.endedBy); LendPutU32(out, (unsigned int)m.defeatByte);
    return 1;
}
inline int DecodeRaidEnd(const char* p, size_t n, RaidEndMsg* m)
{
    if (p == 0) return 0;
    *m = RaidEndMsg();
    LendRd r(p, n);
    if (r.U32() != kRaidEndMagic) return 0;
    m->serial = r.U32(); m->lender = (int)r.U32(); m->id = r.Str(); m->outcome = r.U8(); m->phase = (int)r.U32(); m->endedBy = r.U8(); m->defeatByte = (int)r.U32();
    if (!r.ok || r.at != n || m->id.empty() || m->outcome < kEndArrived || m->outcome > kEndReturned || m->phase < -1 || m->phase > kCampOver
        || m->endedBy < kEndByNone || m->endedBy > kEndByDefeated || m->defeatByte < kDefeatByteNone || m->defeatByte > 255) return 0;
    return 1;
}
/* T-1122 attempt 3 (F-T2349-CHAINSKIP): the lender ends its own campaign of a lent raid that ended at the borrower (LenderCampLeave). The
   engine's own end runs the campaign's ending handler, which queues the kind's follow-up raid (decomp_9c6220: 'has ended' vt+0x10, 'defeated'
   vt+0x18); the borrower's copy's handlers are stubs (only noted), so the lender runs the handler that really ran there, on its own campaign,
   when that campaign ends here (ends: LenderCampEnds). The campaign's table slot to call (0x10 / 0x18), 0 none */
inline int LenderEndSlot(int ends, int endedBy)
{
    if (ends == 0) return 0;
    return endedBy == kEndByEnded ? 0x10 : endedBy == kEndByDefeated ? 0x18 : 0;
}
inline const char* EndedByWord(int endedBy) { return endedBy == kEndByEnded ? "has ended" : endedBy == kEndByDefeated ? "defeated" : "none"; }
/* T-1122 attempt 3 fold 1 (review MEDIUM): the byte the lender writes into its own campaign's +0x3C just before it runs the handler at `slot`
   - the borrower's copy's byte (RAID_END defeatByte) only for 'defeated' (0x18) and only when it was read there; otherwise -1 (nothing written) */
inline int LenderDefeatByte(int slot, int defeatByte) { return (slot == 0x18 && defeatByte >= 0 && defeatByte <= 255) ? defeatByte : kDefeatByteNone; }
/* fold 1 (review LOW): the outcome the borrower says when its copy's campaign ends here (BorrowCampEnd), or its phase change is read
   (BorrowCampWatch, ev kCeTick). When the engine already ended that campaign here - its ending handler noted (endedBy) or phase 2 read - the
   tick's own outcome (retreated / defeated / arrived), so the lender ends its campaign and runs that handler, never 'returned' (only noted
   there); a phase never read before (-1) counts as the attack stage. A link loss or the world's close: RaidEndOf (no road / nothing said) */
/* fold 2 (re-check LOW): 1 = CampEndOutcome takes its engine-end branch - a road to the lender, and the engine ended the campaign here (its
   ending handler noted, or phase 2 read) */
inline int CampEngineEnded(int ev, int phaseNow, int endedBy)
{
    if (ev == kCeLinkLost || ev == kCeWorldClose) return 0;
    return (endedBy == kEndByEnded || endedBy == kEndByDefeated || phaseNow == kCampOver) ? 1 : 0;
}
inline int CampEndOutcome(int ev, int living, int phaseWas, int phaseNow, int endedBy)
{
    if (CampEngineEnded(ev, phaseNow, endedBy) == 0) return RaidEndOf(ev, living, phaseWas, phaseNow);
    const int was = (phaseWas == kCampTravel || phaseWas == kCampAssault) ? phaseWas : kCampAssault;
    return RaidEndOf(kCeTick, living, was, kCampOver);
}
/* fold 2 (re-check LOW): the phase the RAID_END of BorrowCampEnd carries - over (2) whenever CampEndOutcome took its engine-end branch (a
   handler noted while the read faulted, or before phase 2 was written, still says the engine's end, so the lender's apply runs that handler);
   otherwise the phase read (-1 none / not read) */
inline int CampEndPhase(int ev, int phaseNow, int endedBy)
{
    if (CampEngineEnded(ev, phaseNow, endedBy) != 0) return kCampOver;
    return phaseNow >= 0 ? phaseNow : -1;
}
/* fold 2 (re-check LOW): the holder's tick watches its awake lend's campaign while the copy is listed here - awake OR asleep (awake 0: its
   living members are not read, -1), so an engine end inside the put-away's settle is read as phase 2 and said with its handler, no RETURN */
inline int AwakeCampWatched(int listed, int campMade, int campEnded) { return (listed != 0 && campMade == 1 && campEnded == 0) ? 1 : 0; }
/* fold 1 (review LOW, the once-per-serial guard): the lender runs the borrower's handler only for its row's FIRST RAID_END that ends the raid -
   prevOutcome / prevPhase: the row's last applied RAID_END (kEndNone / -1 none); a raid already over (left, or defeated) runs none again */
inline int LenderEndedByOnce(int prevOutcome, int prevPhase, int endedBy)
{
    if (LendRaidOver(prevOutcome, prevPhase) != 0 || prevOutcome == kEndDefeated) return kEndByNone;
    return (endedBy == kEndByEnded || endedBy == kEndByDefeated) ? endedBy : kEndByNone;
}
/* fold 1 (review LOW): the lender's squad leaves its own campaign (LenderCampLeave) for this RAID_END - the raid over there (kEndActLeave), or
   defeated by the engine's ending there (kEndActRemove with endedBy set: the handler runs BEFORE the removal); defeated with no handler (all
   dead, the borrower ended its own copy): the removal only. squadHere: a sleeping lend, or an awake lend whose squad is still listed here */
inline int LenderLeavesCamp(int act, int endedBy, int squadHere)
{
    if (squadHere == 0) return 0;
    if (act == kEndActLeave) return 1;
    return (act == kEndActRemove && endedBy != kEndByNone) ? 1 : 0;
}
/* T-1122 kind choice (T2076): 'arrived' is a phase change from travelling, to the attack stage (1) or straight to over (2: the kind has
   no assault, or its talk ended the campaign) - the text says which, so a 0 -> 2 ending is never printed as an assault. Phase 1 starts
   right after the announcement talk and does NOT mean the assault started (engine read 2026-11-05; T2077: phase 0 -> 1 at 803 s, the
   assault start never ran). phase: RAID_END's (-1 = not read). */
inline const char* RaidEndWord(int outcome, int phase = -1)
{
    if (outcome == kEndArrived)
        return phase == kCampAssault ? "arrived (the raid's attack stage, phase 0 -> 1)" : phase == kCampOver ? "ended without an assault (phase 0 -> 2)"
             : "arrived (phase not read)";
    return outcome == kEndDefeated ? "defeated" : outcome == kEndRetreated ? "retreated" : outcome == kEndReturned ? "returned" : "none";
}

/* ==== T-1122 awake hand (owner 1524, 2026-11-06): AN AWAKE RAID SQUAD'S RAID GOES WITH IT ACROSS THE ORDINARY HAND-OVER ====
   Run T2094 (Confirmed): on A a raid campaign made its squad awake ('Approaching fast'); A left; A's ordinary hand-over gave the squad to
   B with no raid, and B re-issued each member's carried goal as a standing order - the squad stood 23 minutes, no attack. Now A, at each
   ordinary hand-over send of a squad whose raid campaign A made (store.cpp RaidCampaignMadeHere 1, read live at the send - main thread),
   sends AWAKE (kLendKindAwake) to the receiver BEFORE the hand-over message, on the same road (net::SendRaidLendSlotRoad, the road XFER
   and RELEASE take): the lend's key (lender | id | serial), the squad's position, its target and campaign block - no saved bytes - and
   the hand-over message's key uid (`leader`). A keeps a lend row for it, TAKEN at once (the status ask runs as for any taken lend), and
   keeps the squad in its own campaign until the raid is over there (RAID_END, AwakeEndApply). B keeps the block by that uid; when it
   takes the squad it issues no carried order (AwakeTakeOrder) and runs a campaign of the block's kind for the squad it took
   (BorrowCampStart) - the squad stays an ordinary world squad (NOT lent: written, kept and released by the ordinary roads). A block whose
   squad B does not run within kAwakeBlockDropMs is dropped (A's next status ask then hears UNKNOWN and forgets its row). */
const unsigned long kAwakeBlockDropMs = kLendAskGapMs;   /* the lend road's wait for a lend nobody has built: the lender then asks again */

/* the carried goal of a member taken by the ordinary hand-over. blockHere: an AWAKE block (or an applied awake row) for the hand-over's
   key uid here; intentType: the member's carried goal (0 none). Skip: the raid's campaign drives the squad (T2094: the carried goal
   pinned it). ReRun (T-1416 fix attempt 2, F1556): on every other take the carried goal is NOT issued either - issued, it became a
   player-style order that the engine's goal chooser picks first and that never finishes, so a struck member went back to it instead
   of fighting (F1555); the taker's AI re-chooses from its refilled goals and the squad's package, as the engine does for its own
   squads. No take issues the carried goal (the old 'issue' answer is gone). */
enum { kTakeOrderNone = 0, kTakeOrderReRun = 1, kTakeOrderSkip = 2 };
inline int AwakeTakeOrder(int blockHere, int intentType)
{
    if (intentType <= 0) return kTakeOrderNone;
    return blockHere != 0 ? kTakeOrderSkip : kTakeOrderReRun;
}
/* the sender: 1 = AWAKE goes before the hand-over message - on the main thread, its campaign made on this game, its id read */
inline int AwakeHandSends(int onMain, int madeHere, int haveId) { return (onMain == 1 && madeHere == 1 && haveId == 1) ? 1 : 0; }
/* the sender's row: an awake row open to the same receiver keeps its serial (the block said again - a resend, or the next road's send
   of the same squad); anything else takes a new serial */
inline int AwakeSerialKeep(int haveRow, int rowAwake, int rowState, int rowBorrower, int toSlot)
{
    return (haveRow != 0 && rowAwake != 0 && rowState == kLendTaken && rowBorrower == toSlot) ? 1 : 0;
}
/* the receiver's stored block, at its arrival, at the take and each tick: runsLeader - this game runs the key uid and it is in a squad
   here; ageMs - since the block arrived */
enum { kBlockWait = 0, kBlockApply = 1, kBlockDrop = 2 };
inline int AwakeBlockAct(int runsLeader, unsigned long ageMs)
{
    if (runsLeader != 0) return kBlockApply;
    return ageMs >= kAwakeBlockDropMs ? kBlockDrop : kBlockWait;
}
/* the receiver's awake row, each tick (T-1122 review fold 3: judged by the SQUAD, never by the key member alone). listedAwake /
   listedAsleep: its squad found under its id in its faction's awake / sleeping list; runHere: its people (read while awake, kept for its
   sleep) still run on this game; releaseOpen: an open RELEASE offer of them (the RELEASE road decides first - a game that takes them takes
   the raid, AWAKE sent onward); asleepMs: since it was first seen asleep; living: LivingMembersPod (-1 not read). kAhLeaderLeft is never
   answered (a key member that leaves the squad, or is handed on alone, leaves the raid with the squad). */
enum { kAhKeep = 0, kAhPutAway = 1, kAhGone = 2, kAhAllDead = 3, kAhHandedOn = 4, kAhLeaderLeft = 5 };
const unsigned long kAwakePutAwaySettleMs = 3000;   /* asleep this long, its people still run here and no release open: out of sight */
inline int AwakeHandCheck(int listedAwake, int listedAsleep, int runHere, int releaseOpen, unsigned long asleepMs, int living)
{
    if (listedAwake == 0 && listedAsleep == 0) return kAhGone;
    if (listedAwake == 0)
    {
        if (releaseOpen != 0 || asleepMs < kAwakePutAwaySettleMs) return kAhKeep;
        return runHere != 0 ? kAhPutAway : kAhHandedOn;
    }
    if (living == 0) return kAhAllDead;
    if (runHere == 0) return kAhHandedOn;
    return kAhKeep;
}
/* each check's campaign ending (kCe*, RaidEndOf): put away - sleep; all dead - all dead; handed back or on, the key uid left the squad,
   the squad gone - return */
inline int AwakeHandEv(int d)
{
    if (d == kAhPutAway) return kCeSleep;
    if (d == kAhAllDead) return kCeAllDead;
    return kCeReturn;
}
/* THE SENDER'S APPLY of a RAID_END for an awake row. The people run on the receiver; the sender's squad is a copy it does not run, so it
   is never removed here (the receiver's world records carry its deaths). The raid over there (defeated, retreated, or arrived with the
   campaign over): the squad leaves the sender's campaign (LenderCampLeave - the campaign ends by phase 2 when no squad is left) and the
   row goes. Returned (handed back, handed on, put away there, or gone there): the row goes, the squad stays in the sender's campaign (its
   engine drives it again once this game runs it). Arrived at the attack stage: noted, the row stays. */
enum { kEndActForget = 4 };
inline int AwakeEndApply(int state, int sameKey, int outcome, int phase)
{
    if (state != kLendTaken || sameKey == 0) return kEndActNone;
    if (outcome == kEndDefeated || outcome == kEndRetreated || (outcome == kEndArrived && phase == kCampOver)) return kEndActLeave;
    if (outcome == kEndReturned) return kEndActForget;
    if (outcome == kEndArrived) return kEndActNote;
    return kEndActNone;
}

/* ==== T-1122 review folds (2026-11-06, owner 1527) ====
   (2) ONWARD: a game that holds the squad by an awake borrow sends AWAKE on at its own ordinary hand-over (to a third game, or back to the
   raid's owner), naming the raid's owner as lender (the owner stays the game that made the raid). (6) The owner sends no AWAKE over a
   lend row of the same squad that is not an awake one. otherRow: such a row; borrowRowHere: an awake borrow row here for the squad. */
enum { kAwSendNone = 0, kAwSendOwn = 1, kAwSendOnward = 2 };
inline int AwakeSendChoice(int onMain, int madeHere, int haveId, int otherRow, int borrowRowHere, int whole)
{   /* whole (T-1122 re-check fold 5): every member this game runs goes in this hand-over (none held back behind another flight) */
    if (onMain != 1 || haveId != 1) return kAwSendNone;
    if (borrowRowHere != 0) return whole != 0 ? kAwSendOnward : kAwSendNone;   /* part held back: the raid stays here with the rest (no second campaign there) */
    if (madeHere != 1) return kAwSendNone;
    return otherRow != 0 ? kAwSendNone : kAwSendOwn;
}
/* (2) the owner's row follows the squad: a TAKEN for its awake lend (same serial) from another game - the game the squad was handed on to -
   makes that game its borrower (status asks and endings from there) */
inline int AwakeBorrowerMoves(int rowAwake, int state, int kind, int sameSerial, int from, int borrower)
{
    return (rowAwake != 0 && state == kLendTaken && kind == kLendKindTaken && sameSerial != 0 && from >= 0 && from != borrower) ? 1 : 0;
}
/* (4, owner 1527; T-1122 RETURN by reference 2026-11-06) what the holder says at its row's ending (d: AwakeHandCheck): out of sight (put
   away, nobody took it) - RETURN naming the world record this game's sleep save wrote at the put-away (written: ReturnRefWritten said so;
   the owner loads that record into its own copy and travels it on in its campaign), or RAID_END when the save wrote none; handed on (or
   gone after it) with AWAKE sent onward - nothing (the raid went on with it); else RAID_END */
enum { kAwSayNothing = 0, kAwSayRaidEnd = 1, kAwSayReturn = 2 };
inline int AwakeEndSays(int d, int forwarded, int written)
{
    if (d == kAhPutAway) return written != 0 ? kAwSayReturn : kAwSayRaidEnd;
    if ((d == kAhHandedOn || d == kAhGone) && forwarded != 0) return kAwSayNothing;
    return kAwSayRaidEnd;
}

/* ==== T-1122 re-check folds (2026-11-06) ====
   (1) after its RETURN the holder removes its OWN sleeping copy of the squad (an admin removal: no gone mark, no delete - the world record
   stays where the squad was put away, and this game does not place it from that record again in this world), so the owner's copy is the
   one copy any game can wake. Only a copy still asleep here: one that woke here first is kept (and said). */
inline int AwakeHereRemoves(int say, int asleepHere) { return (say == kAwSayReturn && asleepHere != 0) ? 1 : 0; }
/* (1) the owner: an incoming position update never moves a squad noted here as this game's own raid squad - this game is its one traveller */
inline int SleepPosTakes(int ownRaidHere) { return ownRaidHere != 0 ? 0 : 1; }
/* (3) the owner's sleeping copy is frozen (no travel, no wake) while its lend is open: an ordinary lend while asked or taken; an awake
   lend exactly while TAKEN - lent out and not given back. Given back (RETURN applied: an ordinary own raid row, idle) or ended (the row and
   its note gone): the ordinary roads - it wakes where this game's own off-screen update would wake it. */
inline int LendCopyFrozen(int rowAwake, int state)
{
    if (state == kLendIdle) return 0;
    return rowAwake != 0 ? (state == kLendTaken ? 1 : 0) : 1;
}
/* (6) the holder that sent the squad on (AWAKE onward) keeps quiet to the owner's status ask for kAwakeOnwardQuietMs: no UNKNOWN (the
   owner would drop its row before the new holder's TAKEN reaches it) and no TAKEN (a late TAKEN from here would move the owner's row back
   from the new holder - AwakeBorrowerMoves); the owner keeps its row and asks again after its status gap. After the window: the ordinary
   answer (BorrowDecide). */
const unsigned long kAwakeOnwardQuietMs = 10000;
inline int AwakeOnwardQuiet(int statusAsk, int active, int ended, unsigned long sinceMs)
{
    return (statusAsk != 0 && active == 0 && ended == 0 && sinceMs < kAwakeOnwardQuietMs) ? 1 : 0;
}

/* ==== T-1122 RETURN by reference (2026-11-06, design (a); game protocol 218) ====
   The awake lend's holder gives the raid back by naming the world record its own sleep save wrote at the put-away; the record reaches the
   owner by the store road, which has no 64 KB limit (run T2103: the squad's bytes were 106 KB, over kLendBytesCap, so no RETURN went and
   the raid ended by RAID_END). One source per fact: the record in the shared store. The SLEEPING lend's RETURN kept its bytes (form 0) at
   first; since game protocol 219 it names the world record the borrower's put-away writes too (T-1122 lend by reference, below).
   THE HOLDER at the put-away, read live from its own record entry: noted - a sleep write of the squad noted since it was last seen awake
   here, once sent to the world server (noteAt its writtenAt; T-1122 review fold 2, AwakeSleepNote); held - this game's record entry of that world id (heldAt, gone, file); mine - that entry's writer is this
   game. Written only while the entry still is that write. */
enum { kRetRefWritten = 0, kRetRefNoWrite = 1, kRetRefNoRecord = 2, kRetRefReplaced = 3, kRetRefNotMine = 4, kRetRefUnusable = 5 };
inline int ReturnRefWritten(int noted, long long noteAt, int held, long long heldAt, int heldGone, int heldFile, int mine)
{
    if (noted == 0 || noteAt <= 0) return kRetRefNoWrite;
    if (held == 0) return kRetRefNoRecord;
    if (heldAt != noteAt) return kRetRefReplaced;
    if (mine == 0) return kRetRefNotMine;
    if (heldGone != 0 || heldFile == 0) return kRetRefUnusable;
    return kRetRefWritten;
}
/* THE OWNER at the RETURN: 1 = it holds that world id's record at the RETURN's stamp or newer, usable (not gone, its file named) - loaded
   now; 0 = a waiting note */
inline int ReturnRefHeld(int held, long long heldAt, int heldGone, int heldFile, long long stamp)
{
    return (held != 0 && heldGone == 0 && heldFile != 0 && stamp > 0 && heldAt >= stamp) ? 1 : 0;
}
/* THE WAITING NOTE at its events (never a timer). kRwRecv: that world id's record arrived here (recAt: its writtenAt); copyOk: the copy is
   still this game's own raid squad, asleep under its id, its lend row idle - else the note is dropped. kRwWake: the copy woke here first
   (recAt: the record the wake road loaded or left to the engine's read, 0 none) - the wake settles the note either way. */
enum { kRwRecv = 1, kRwWake = 2 };
enum { kRwKeep = 0, kRwApply = 1, kRwDrop = 2, kRwWakeApplied = 3, kRwWakeMissed = 4 };
inline int ReturnWaitOn(int ev, long long recAt, long long stamp, int copyOk)
{
    if (ev == kRwWake) return (recAt > 0 && recAt >= stamp) ? kRwWakeApplied : kRwWakeMissed;
    if (recAt < stamp) return kRwKeep;   /* an older record: the note waits for the named one */
    return copyOk != 0 ? kRwApply : kRwDrop;
}
/* ==== T-1122 review folds of the RETURN by reference (2026-11-06) ====
   (fold 2) THE HOLDER'S NOTE of its sleep write: only a write handed to the world server's reliable road (store.cpp SendOne true) is
   noted. SendOne false (no link, the send refused, or the record's file unreadable) is not queued for a retry: the write is re-pushed only
   at a later relink, and not when the world server already lists that id - a RETURN naming it could name a record the owner never gets.
   Such a write is kept apart (unsentAt), for the RAID_END's reason. */
inline void AwakeSleepNote(int sent, long long writtenAt, long long* wroteAt, long long* unsentAt)
{
    if (sent != 0) { *wroteAt = writtenAt; *unsentAt = 0; }
    else { *wroteAt = 0; *unsentAt = writtenAt; }
}
/* (fold 3) THE HOLDER'S OWN COPY GOES FIRST, THEN THE RETURN: the put-away's RETURN waits on its row (still held, so the owner's status
   ask hears TAKEN meanwhile) for kLendReturnSweepMs; then, after the row walk, this game's own copy - still asleep here under its id, the
   named record still this game's put-away write (ref: ReturnRefWritten read again) - is removed (no write: the named record stays valid),
   and only a removal that took sends the RETURN. A copy no longer asleep here (it woke first, or went), a record no longer the named one,
   or a removal that did not take: RAID_END with that reason - never both games running the squad. */
inline int AwakeReturnDue(int pending, unsigned long sinceMs) { return (pending != 0 && sinceMs > kLendReturnSweepMs) ? 1 : 0; }
enum { kArfReturn = 0, kArfNotAsleep = 1, kArfRecord = 2, kArfNotRemoved = 3 };
inline int AwakeReturnFinish(int asleep, int ref, int removed)
{
    if (asleep == 0) return kArfNotAsleep;
    if (ref != kRetRefWritten) return kArfRecord;
    return removed == 1 ? kArfReturn : kArfNotRemoved;
}
/* (fold 4, re-check) THE OWNER'S WAITING NOTE at the tick: dropped only when the copy is no longer this game's own raid squad (freed,
   gone, woke) - so the squad can be lent again (LendStartOne). Never because the RETURN's sender left the world: the record comes from the
   world server, and the sender sent the RETURN only after its record was sent there (fold 2), so the record still arrives and loads */
inline int ReturnWaitTickOn(int copyOwn) { return copyOwn == 0 ? kRwDrop : kRwKeep; }
/* (fold 6) THE OWNER'S HANDED-OVER MARK goes only when the RETURN is taken for this copy - its named record loaded (load 1), or a note
   left (waits); never for a RETURN naming another world id, nor for a load that failed (the copy keeps its old people: its sleep save
   must not write them over the returned record) */
inline int ReturnRefUnmarks(int wrongId, int load, int waits) { return (wrongId == 0 && (load == 1 || waits != 0)) ? 1 : 0; }

/* ==== T-1122 lend by reference (2026-11-06, design (a); game protocol 219) ====
   A raid squad's state never fit the byte lend (kLendBytesCap 60000, removed: T2116's record 96-98 KB, T2103's 106 KB - after the return
   the lender refused its lend 32 times and the raid stalled; T2116 log-A 925.7-1886.2). The world server's store road keeps squad records
   of that size (T2116 log-store: 86-98 KB RECORD lines) - it has no 64 KB limit - so the SLEEPING lend goes by reference too: the lender
   writes the squad's state at the lend's own save as its WORLD record on the store road and LENDs naming it (world id, writtenAt, writer
   slot) - no bytes. The borrower builds its copy from that record: NOW, when it already holds that id's record at the named stamp or newer
   (ReturnRefHeld), or AT ITS ARRIVAL (the RECV marks the wait, the tick builds after BorrowDecide is asked again, live). There is no copy
   here yet to wake, so the RETURN's third road (the wake) is a bound here: a record not here within kLendRecordWaitMs is refused
   kRefNoRecord, the lender gone drops the wait - the lender then resumes its own squad (never frozen for ever). At its put-away the
   borrower writes the copy's state as the world record and RETURNs naming it (form 1, as the awake lend); a state that cannot be saved,
   written or sent says KEEPOWN - the lender resumes its own squad as it lent it; never DIED for size (DIED removed a living squad).
   THE WRITER (squadwriterreg.h Judge): a full write from a game that is not the squad's registered writer is taken only when that writer is
   away (left, RELEASED, or a keeper no longer standing for the area) or is a keeper; else it is refused 'not the writer'. So each side
   releases the squad (SQUAD_RELEASE, sent after its write on the same ordered channel; the world server holds it behind that write -
   store_main.cpp) right after its write: the lender at the lend (it runs none of the squad's people while lent), the borrower at its
   put-away write. The awake holder needs no release of its own: its put-away is the ordinary sleep road, whose release follows its sleep
   write (store.cpp detour_deactivate's SwPutAway) - T2116, read again in the review folds: the holder's sleep write (log-store 20:27:49,
   slot 1) was followed by its release (Inferred, not Confirmed: the world server's releaseTaken 88 -> 91 by 20:28:47 counts every squad's releases); the world server prints one SQUAD line per
   id, verdict and kind, and 'release' for that id was printed at 20:20:08, so no line was due. The owner wrote nothing after the return
   (its 32 'NOT lent' lines were the 60000-byte cap). A write this game knows refused (store.cpp g_swRefused) is not named; a refusal heard
   while the lend is still ASKED ends it there (LendAskRefused). */
const unsigned long kLendRecordWaitMs = 30000;   /* the borrower: a LEND's named record not here this long after the LEND - refused kRefNoRecord */
/* THE LENDER at the lend's write, read live: haveId - the squad's world id; refused - the world server refuses this game's writes of it
   now; saved - its state saved and written to the record's file; sent - the record handed to the world server's reliable road */
enum { kLwGo = 0, kLwNoId = 1, kLwRefused = 2, kLwNotSaved = 3, kLwNotSent = 4 };
inline int LendWriteGo(int haveId, int refused, int saved, int sent)
{
    if (haveId == 0) return kLwNoId;
    if (refused != 0) return kLwRefused;
    if (saved == 0) return kLwNotSaved;
    return sent != 0 ? kLwGo : kLwNotSent;
}
/* THE BORROWER'S WAIT for a LEND's record: held - its record of that id usable at the named stamp or newer (ReturnRefHeld); there - the
   lender is in the world; sinceMs - since the LEND. kLrBuild: built now (BorrowDecide asked again first); kLrGone: dropped (no answer has
   a road - the lender's own peer-gone ending applies there); kLrTimeout: refused kRefNoRecord */
enum { kLrKeep = 0, kLrBuild = 1, kLrTimeout = 2, kLrGone = 3 };
inline int LendRecWaitOn(int held, int there, unsigned long sinceMs)
{
    if (held != 0) return kLrBuild;
    if (there == 0) return kLrGone;
    return sinceMs >= kLendRecordWaitMs ? kLrTimeout : kLrKeep;
}
/* THE BORROWER at the put-away (or a give-back asleep): its copy's state written and sent as the world record - RETURN naming it; else KEEPOWN */
inline int LentPutAwaySays(int written) { return written != 0 ? kLendKindReturn : kLendKindKeepOwn; }
/* THE LENDER while its lend is ASKED: the world server refused this game's write of the named record (named: the row names one) - the lend
   ends here (resumed, as a refusal); nothing can be built from that record */
inline int LendAskRefused(int state, int named, int refusedNow) { return (state == kLendAsked && named != 0 && refusedNow != 0) ? 1 : 0; }

/* ==== T-1122 review folds on lend by reference (2026-11-06; game protocol 219) ====
   WITHDRAWN (kind 9). The lender can end a lend while the borrower still builds its copy: its world-server link drops while the lend is
   ASKED (LendAnswer resumes), the borrower opens a fresh wait for a re-ask that crossed its why-8 refusal, or the lender's write is refused
   while the borrower already holds a newer record of that id. The borrower's TAKEN then reaches an idle row - it was counted stale, and two
   copies of the people ran. Now the lender answers a TAKEN or RETURN naming a serial it has not open with WITHDRAWN (that serial); the
   borrower drops a wait under that key, and an open copy ends there (ended kLendKindWithdrawn: the keep question no longer keeps it) and is
   removed - asleep by the 2 s sweep, awake put to sleep first and removed (store.cpp LentSavedCopyRemove). An awake hand's row is an ordinary
   world squad there - never removed for it. A RETURN said again after it was applied is not answered.
   THE HELD ENDING. The sleeping copy's RETURN (or KEEPOWN) waits on its row until the copy's removal takes (as the awake road's retPending):
   it is sent when the copy is destroyed here - by the 2 s sweep, or the engine's own removal - so a copy that woke inside the sweep's 2 s is
   put to sleep and removed, never left running beside the lender's resumed squad. While it waits the lender's status ask hears TAKEN. At the
   send the named record is read again: no longer this game's put-away write (the world server refused it - its rollback) - KEEPOWN instead.
   A REFUSAL AFTER THE RETURN. A world-server refusal of the put-away write that arrives after the RETURN went (a RETURN kept to say again,
   naming that world id) is followed by KEEPOWN with that serial; the lender, its row idle with that serial applied and its note still
   waiting for the record, drops the note and keeps its own people (the record named will never come). */
/* THE LENDER, an answer its row's act ignored: haveRow - a row of that id here; awake - an awake lend's row (its people run on the receiver);
   applied - the serial whose RETURN this row applied last. 1 = WITHDRAWN to the sender */
inline int LendWithdrawSays(int haveRow, int awake, int state, unsigned int openSerial, unsigned int applied, int kind, unsigned int msgSerial)
{
    if (haveRow == 0 || awake != 0) return 0;
    if (kind != kLendKindTaken && kind != kLendKindReturn) return 0;
    if (state != kLendIdle && openSerial == msgSerial) return 0;                     /* open under that serial: LendAnswer's to judge */
    if (kind == kLendKindReturn && applied != 0 && applied == msgSerial) return 0;   /* a RETURN said again after it was applied */
    return 1;
}
/* THE BORROWER at WITHDRAWN: haveRow - a row under that key; held - its ending waits for its removal */
enum { kWdNone = 0, kWdRemove = 1, kWdAwakeHand = 2, kWdEnded = 3, kWdHeld = 4 };
inline int WithdrawnAct(int haveRow, int awakeHand, int ended, int held)
{
    if (haveRow == 0) return kWdNone;
    if (awakeHand != 0) return kWdAwakeHand;   /* an ordinary world squad here - never removed for a lend's answer */
    if (held != 0) return kWdHeld;             /* its ending is not sent: the lender wants none - the copy is removed */
    if (ended != 0) return kWdEnded;           /* already said and on its way out */
    return kWdRemove;
}
/* THE BORROWER'S HELD ENDING at the copy's removal: a RETURN whose named record is no longer this game's put-away write here (ReturnRefWritten,
   read now) says KEEPOWN */
inline int HeldEndingKind(int kind, int recStill) { return (kind == kLendKindReturn && recStill != kRetRefWritten) ? kLendKindKeepOwn : kind; }
/* an ended copy still awake past the sweep's time is put to sleep and removed when its ending waits for that (held) or it was withdrawn */
inline int LentEndedRetire(int ended, int held, int awake, unsigned long sinceEndMs)
{
    return (ended != 0 && (held != 0 || ended == kLendKindWithdrawn) && awake != 0 && sinceEndMs > kLendReturnSweepMs) ? 1 : 0;
}
/* THE LENDER at KEEPOWN for a serial whose RETURN it applied: its note still waits for that RETURN's record - dropped, its people kept */
inline int KeepOwnAfterReturn(int state, unsigned int applied, unsigned int msgSerial, int noteWaits)
{
    return (state == kLendIdle && msgSerial != 0 && applied == msgSerial && noteWaits != 0) ? 1 : 0;
}

/* ==== T-1122 re-check folds (2026-11-06; game protocol stays 219) ====
   THE HELD ENDING'S BOUND. A held ending waits for its copy's removal (BorrowSendHeld at RaidLentGone). A copy the sweep cannot find (asleep
   but not in its faction's sleeping list, or not readable) or whose put-to-sleep-and-remove keeps failing would hold it - and the lender's
   squad, whose status ask hears TAKEN meanwhile - for ever. kLendHeldMaxMs after its end the ending is sent though the copy is not removed
   (said so, counted); the put-to-sleep-and-remove of an ended copy (held or withdrawn) is tried at most kLendRetireMaxTries times (1 Hz, as
   RefusedWakesTick), then given up with one line - the copy is left to the engine, as a copy whose lender left (discarded at the area edge).
   A REFUSAL NAMES ITS WRITE. A refused full write answers a RETURN kept to say again only when the record's writtenAt at the refusal (read
   before the rollback) is the stamp that RETURN named - a later refused write of the same world id leaves it alone.
   WITHDRAWN IS SAID PER ARRIVAL. The lender keeps no memory of a WITHDRAWN it said: every TAKEN or RETURN naming a serial it has not open is
   answered again (LendWithdrawSays reads the row only), so one lost on a link drop is said again at the borrower's next TAKEN or RETURN. */
const unsigned long kLendHeldMaxMs = 30000;   /* the borrower: a held ending is sent this long after its end though its copy is not removed */
const int kLendRetireMaxTries = 30;            /* the borrower: put-to-sleep-and-remove tries for one ended copy (1 Hz), then given up */
inline int HeldEndingOverdue(int held, unsigned long sinceEndMs) { return (held != 0 && sinceEndMs >= kLendHeldMaxMs) ? 1 : 0; }
inline int LentRetireTry(int wanted, int failedTries) { return (wanted != 0 && failedTries < kLendRetireMaxTries) ? 1 : 0; }
inline int LentRetireGaveUp(int failedTries) { return failedTries >= kLendRetireMaxTries ? 1 : 0; }
/* the refused write's stamp (the record's writtenAt before the rollback) against the stamp a RETURN named */
inline int RefusedWriteNamesReturn(long long refusedAt, long long namedAt) { return (refusedAt != 0 && refusedAt == namedAt) ? 1 : 0; }

/* ==== T-1122 fix A (owner 1575 a, 2026-11-09; F-T1122-LENDBACK, run T2265; game protocol stays 219) ====
   A SQUAD LENT BY REFERENCE, HANDED BACK TO ITS OWNER BY THE ORDINARY HAND-OVER, TAKES ITS RAID WITH IT.
   Run T2265 (Confirmed): A lent its sleeping raid squad to B by reference; B's copy walked into an area A holds and B's ordinary hand-over
   (the forced XFER) passed its 14 people to A with no raid - AWAKE went only for a squad held by an awake borrow (AwakeSendChoice); B then
   ended the raid 'returned' and RETURNed; A's RETURN load woke its frozen copy, which was refused as a duplicate of the people it had just
   taken and destroyed - no raider reached A's base. Broken assumption: "a squad lent by reference comes back only by the RETURN road".
   THE HOLDER (B), at each ordinary hand-over send of a lent copy (its borrow row open, the copy in g_lentPlatoons) - the XFER roads and the
   RELEASE offer alike: AWAKE goes onward to the raid's owner (AwakeSendOnward - the owner as lender, the lend's id and serial) when the
   receiver IS the lender, every member this game runs goes (whole), and those members were read awake now (their uids kept on the row).
   Else nothing new: the RETURN road at its put-away, as before (a hand-over to a third game is not covered by this fix; an asleep copy's
   RELEASE offer neither - its put-away has already started the RETURN road).
   T-1122 fix A review fold 1 (2026-11-09) - BOTH SIDES DECIDE FROM ONE FACT, THE OWNER'S TAKE: (1) THE OWNER swaps only on a FULL take - every
   member the hand-over message named taken there (the XFER's taken == its count, the RELEASE's adopted == its rows); a partial take
   (a member with no copy there, or refused) swaps nothing, closes nothing and says nothing: the people kept by the holder stay in its copy,
   alive and in its campaign there, and come home by the next hand-over (whose full take swaps them) or the RETURN road. (2) THE OWNER'S
   CONFIRMATION: having swapped (or dropped the copy - the raid lost), it closes its row and says WITHDRAWN (kind 9) to the holder - "no lend
   open under that serial, this game runs the squad" - so the holder's copy ends with NOTHING said (no RAID_END, no RETURN) and is removed
   (BorrowOnWithdrawn, the existing road). (3) THE HOLDER at the copy's put-away before that word came: AWAKE went onward to the lender and none
   of the uids sent runs here (the owner took them all) - no RAID_END now; its RETURN is written and HELD (the existing held ending) until the
   copy's removal: WITHDRAWN in that time drops it (kWdHeld); none (the owner did not take them into its raid) - the RETURN goes, the old road.
   Any of the uids still running here: the ordinary put-away (RAID_END and RETURN), as before.
   THE OWNER (A), at the take (AwakeApply: the block's lender is this game and no awake lend row of it is open): its row of that lend (by
   reference, open, from that borrower) and a full take - the platoon the people were taken into joins this game's own campaign of the squad
   (the engine's join, vt+0x70) and its sleeping copy leaves it (the engine's take-out, vt+0x80); the copy is removed (an admin removal: no gone
   mark, no delete - its people run in the platoon taken), the row goes and WITHDRAWN is said. No campaign here, its slots not the known ones,
   or the two not of one faction: the copy leaves what it can and is removed - the raid is lost, said (never two copies). */
/* the holder at the hand-over send: lentRowOpen - a borrow row of a lent copy of this platoon (not ended, no held ending); toLender - the
   receiver is that lend's lender; whole - every member this game runs goes; uidsRead - the members this game runs were read (awake now) */
inline int LentHandBackSends(int onMain, int lentRowOpen, int toLender, int whole, int uidsRead)
{
    return (onMain == 1 && lentRowOpen != 0 && toLender != 0 && whole != 0 && uidsRead != 0) ? 1 : 0;
}
/* the holder at the copy's put-away: 1 = no RAID_END now, the RETURN held for the owner's WITHDRAWN - AWAKE was sent onward to the lender,
   the uids sent were kept, and none of them runs here now (the owner took them all) */
inline int LentHandBackHolds(int forwardedToLender, int uidsKnown, int anyRunHere)
{
    return (forwardedToLender != 0 && uidsKnown != 0 && anyRunHere == 0) ? 1 : 0;
}
/* the owner at the take: rowState - its row of that lend (kLendIdle: none open); fromBorrower - the block came from that row's borrower;
   fullTake - every member the hand-over message named was taken here (1; 0 partial; -1 not known: the block came after the take);
   takenRead - the platoon the people were taken into is read (awake here, not the sleeping copy); campHere - the sleeping copy's campaign
   here; slotsOk - that campaign's join and take-out slots are the known ones; sameFaction - the two platoons are of one faction */
enum { kLhNone = 0, kLhSwap = 1, kLhDropOnly = 2 };
inline int LentHomeAct(int rowState, int fromBorrower, int fullTake, int takenRead, int campHere, int slotsOk, int sameFaction)
{
    if (rowState == kLendIdle || fromBorrower == 0 || fullTake != 1 || takenRead == 0) return kLhNone;
    return (campHere != 0 && slotsOk != 0 && sameFaction != 0) ? kLhSwap : kLhDropOnly;
}
/* the owner's sleeping copy is removed only when it is still asleep here, listed under its id */
inline int LentHomeRemovesOwn(int act, int ownAsleep) { return (act != kLhNone && ownAsleep != 0) ? 1 : 0; }
/* the owner says WITHDRAWN to the holder exactly when it closed its row (it acted) */
inline int LentHomeConfirms(int act) { return act != kLhNone ? 1 : 0; }
/* T-1122 fix A re-check fold (2026-11-09): (1) THE HOLDER'S HELD PUT-AWAY WRITES NOTHING. The owner took every person sent, so their live
   state is the owner's (its heartbeat writes the squad); a put-away record written and sent from the holder's copy - which still holds those
   people asleep - could be placed by an area holder as a second set of them (the owner removed its own copy with no gone mark). So the held
   ending is KEEPOWN (no record named, no write, no writer release): WITHDRAWN drops it; with no WITHDRAWN it goes at the copy's removal and
   the owner keeps its own squad as it lent it - the same outcome the RETURN had there (its frozen copy's wake meets the people it took).
   (2) THE TAKE'S FULLNESS IS KEPT: a take notes it by the hand-over's key uid (RaidAwakeTakeNote, every take road, block or not) for
   kAwakeBlockDropMs; a block that arrives after the take, or is applied by the tick, is judged with it (else -1, not known).
   (3) KNOWN GAP (Inferred, rare): after a PARTIAL take the people the holder kept come home by its later RETURN, whose wake the owner refuses
   by squad id while the people it took run under that id - they are lost. Waking only the members the owner does not run would split a
   sleeping squad's members at its load, which the mod has no road for; a partial take needs a member with no copy on the owner. */
inline int LentHeldEndingKind() { return kLendKindKeepOwn; }
inline int AwakeTakeNoteUsable(int haveNote, unsigned long ageMs) { return (haveNote != 0 && ageMs < kAwakeBlockDropMs) ? 1 : 0; }

/* ==== T-1122 whole or none (owner 1581 a, 2026-11-09; run T2274; game protocol unchanged) ====
   ONE GAME RUNS A LENT SQUAD, AND A HAND-OVER MOVES ALL OF ITS PEOPLE OR NONE (established multiplayer practice: one authority per group).
   Run T2274 (Confirmed in logs; mechanism Read): the hand-over took people one by one - the receiver took only those it had a live copy of
   (Nomads_1: A took 11 of 21, B kept 10; Western Hive_11's RELEASE: 3 adopted, 9 dropped) - while the squad's record is one container,
   written whole at the put-away and loaded whole by the owner, whose duplicate-wake refusal then destroyed the whole copy: the people
   the holder kept were LOST. Broken assumption: "a squad's people can change games one by one".
   THE RECEIVER (handoff.cpp ApplyRemoteXfer / ApplyRemoteRelease): a hand-over whose own message says "lent squad - whole or none"
   (T-1122 lent flag, protocol 224: LentWholeJudged - with or without the block), or that carries the squad's raid block (AWAKE came first on
   the same road, or this game holds that squad by an awake borrow - RaidAwakeBlockHas) is a lent squad's: every member is judged first,
   with no change made; all can be taken (a live copy here and this game's engine keeps it at its spot) - all are taken; else NONE is
   taken (the XFER's ACK names none, the sender keeps the whole squad and backs off until its sector or receiver changes; a RELEASE's rows
   all go back dropped - or deferred when only defers stood in the way - so the giver tries the next candidate or keeps them asleep). The
   missing members are NOT built from the squad's record: a member with no copy here stands where this game's engine would put it away
   (its copies follow its area), so the squad stays with the game that runs it until all of it stands in the receiver's area - or its
   engine puts it away and its own ending (the RETURN) gives it back whole.
   THE SENDER (store.cpp RaidAwakeSendImpl, before each XFER and RELEASE offer): a lent squad here whose raid block does not go with this
   hand-over (a copy lent by reference handed to a third game, or with a member held back, or its members not read; an awake-borrowed squad
   with a member held back, or its onward block not sent; a lent squad whose lend ended or whose ending is held) is NOT handed over: the XFER
   is not sent this pass; the RELEASE offer is not made - its people stay asleep here, whole, and the lent copy's own ending gives the squad
   back. THE CLOCK: every age on the lend roads is LendAgeMs - 0 when the stamp is later than the tick's `now` (RaidLendTick reads `now`
   once; a build or ending later in that tick stamps a newer GetTickCount, and the unsigned difference wrapped to ~4.29e9: T2274's Dust
   Bandits copy was given back as 'never woke' 10 ms after its build). */
inline unsigned long LendAgeMs(unsigned long now, unsigned long at)
{   /* a stamp later than now (by less than half the counter) is age 0; the tick counter's own wrap still gives the true age */
    const unsigned long d = now - at;
    return d > 0x7FFFFFFFUL ? 0UL : d;
}
/* the sender's gate: lentHere - this game holds the squad as a lent one (any lend road, any state); blockSent - its raid block went with
   this hand-over (the receiver then judges it whole or none) */
/* T-1122 lent flag (game-to-game protocol 224): kLgLentWhole - a lent squad here whose block went with this hand-over. The hand-over
   message itself then says "lent squad - whole or none" (the XFER's reason word, the RELEASE's flags - liveowner.h), so the receiver
   judges it whole from the message even when the block was dropped (AwakeBlockArrive: its table full, or a sender it does not know
   yet - T2274's loss road). kLgWholeAtReceiver - this game's own raid whose block went (not lent here): flagged too (review LOW-A,
   2026-11-09) - with the block kept the receiver already judges it whole, so the flag changes only the dropped-block road. */
enum { kLgHoldBack = -1, kLgOrdinary = 0, kLgWholeAtReceiver = 1, kLgLentWhole = 2 };
inline int LentGateOf(int lentHere, int blockSent)
{
    if (blockSent != 0) return lentHere != 0 ? kLgLentWhole : kLgWholeAtReceiver;
    return lentHere != 0 ? kLgHoldBack : kLgOrdinary;
}
inline int LentGateHolds(int gate) { return gate == kLgHoldBack ? 1 : 0; }
inline int LentWholeFlagOf(int gate) { return (gate == kLgLentWhole || gate == kLgWholeAtReceiver) ? 1 : 0; }   /* the message's "whole or none" flag: any squad whose raid block went (review LOW-A) */
/* the receiver: lentSquad - the hand-over carries the squad's raid block; takeable - members this game can take (a live copy here, kept
   at its spot, or already run here); count - members the message names */
enum { kTwEach = 0, kTwAll = 1, kTwNone = 2 };
inline int LentTakeWhole(int lentSquad, int takeable, int count)
{
    if (lentSquad == 0) return kTwEach;
    return (count > 0 && takeable >= count) ? kTwAll : kTwNone;
}
/* T-1122 lent flag: the receiver's lentSquad - the message's flag, or the squad's raid block here (an own raid's block: as before). The
   flag alone is enough: a flagged hand-over refused whole is refused whole even when no block arrived. Under a whole refusal, members
   this game ALREADY runs are still named (the XFER's ACK names them taken, a RELEASE row it runs is ACKed adopted): they are this
   game's - naming none left two games running them (re-check item 3, the LOW-2 fold reverted); the [XFER] / [RELEASE] line counts
   them as "already run here" (alreadyHere=), and they are not a split. */
inline int LentWholeJudged(int msgFlag, int blockHere) { return (msgFlag != 0 || blockHere != 0) ? 1 : 0; }
/* the holder's put-away of a lent copy handed back with its raid: which of the people sent run here now. All - its record is all of them
   (the ordinary put-away); none - nothing written (fix A's held KEEPOWN); part - unreachable under whole or none, counted and said (the
   ordinary put-away, as before) */
enum { kPwAll = 0, kPwNone = 1, kPwPartial = 2 };
inline int LentPutAwayWrites(int anyRunsHere, int allRunHere)
{
    if (anyRunsHere == 0) return kPwNone;
    return allRunHere != 0 ? kPwAll : kPwPartial;
}
/* the raidLendWhole token's counters */
enum { kLwXferAll = 0, kLwXferNone = 1, kLwRelAll = 2, kLwRelNone = 3, kLwSendHeld = 4, kLwRelHeld = 5, kLwPutAwayPartial = 6, kLwCount = 7 };

}   /* namespace raidsq */
