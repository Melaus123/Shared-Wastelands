/* src/common/areaclaim.h - WHAT THE NOTEBOOK REMEMBERS ACROSS A RESTART, AS PURE DECISIONS (B13).
 *
 * Same charter as storelink.h and clockmath.h: nothing in here reads a global, opens a file, or includes a
 * Windows, ENet or Ogre header. Every function is a pure function of its arguments, and the same
 * header is compiled into the notebook (SharedWastelandsServer.exe) and into the offline test exe - so the rule that
 * decides who holds an area after a restart, and the format of the three files that carry it, exist once.
 *
 * WHY IT EXISTS (T239, Confirmed). The notebook's area map, its slot numbers and its authority were all in
 * memory and all re-derived from DIAL ORDER. A relay killed and restarted therefore handed the areas, the
 * slot numbers (which RECORD.owner carries) and the authority flag to whichever game happened to reconnect
 * first: the map flipped, and the other game's replayed move was refused as a collision because the relay
 * had just told that game it was the area's writer. B13 gives every player a stable id, writes the id ->
 * slot table, the area map and the operator beside the records, and restores all three at start.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_COMMON_AREACLAIM_H
#define COOP_COMMON_AREACLAIM_H

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace coopstore {

/* ================= THE PLAYER ID =================
   32 lowercase hex characters, generated once by the plugin into shared_wastelands.cfg and carried in every HELLO.
   The test is a SHAPE test and nothing more - this header cannot know which ids exist. It is here rather than
   in cfgtext.h because the relay refuses a malformed id too, and one rule may not have two implementations. */
const size_t kPlayerIdLen = 32;

inline int PlayerIdOk(const std::string& id)
{
    if (id.size() != kPlayerIdLen) return 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        const int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) return 0;
    }
    return 1;
}

/* prof1 (docs/design-profiles1.md s1; src/common/profiles.h): A NOTEBOOK SLOT KEY - the name slots.txt and areas.txt key
   a player on. A person's profile 1 keeps the bare 32-hex person id (so a world from before profiles hands each person's slot
   and areas to their profile 1 unchanged); profile n >= 2 is "<person>.<n>", n written without a leading zero, 2..9999. */
inline int SlotKeyOk(const std::string& key)
{
    const size_t d = key.find('.');
    if (d == std::string::npos) return PlayerIdOk(key);
    if (!PlayerIdOk(key.substr(0, d))) return 0;
    const std::string n = key.substr(d + 1);
    if (n.empty() || n.size() > 4 || n[0] == '0') return 0;
    for (size_t i = 0; i < n.size(); ++i) if (n[i] < '0' || n[i] > '9') return 0;
    return std::atoi(n.c_str()) >= 2;
}

/* ================= ONE ID, ONE GAME - BUT A PLAYER OUTLIVES ITS OWN CONNECTION (B13-c) =================
   A HELLO carrying an id the notebook already has on ANOTHER connection is one of two things, and the
   notebook must tell them apart: two installs sharing a copied shared_wastelands.cfg (refuse the newcomer, in
   words), or the SAME game back on a new socket after a re-dial, a crash or a dropped network, while the
   notebook still holds the socket it left (drop the old one and take the new). Nothing in the HELLO says
   which. What does say is the old connection's SILENCE: ENet answers every ping within a round trip and
   the game reports its clock once a second, so a live connection is never quiet for long, and one the game
   abandoned is quiet from the moment it left. The notebook measures how long since the old connection last
   acknowledged anything and asks this function.

   The bound is a time, which the design principles forbid as a proxy for state; it is allowed here because
   the liveness of a remote socket HAS no event - the absence of one is the whole question - and ENet itself
   declares a peer dead by the same silence, only after 5 to 30 s. Three seconds is six missed ping rounds
   and three missed clock reports. At or inside the bound the old connection is live and the newcomer is
   REFUSED (the copied-file case, or a second machine that is merely lagging - it re-dials and asks again);
   past it the old connection is evicted. review-b13-b Q2 (2026-09-18, Read): without this, B12-e's re-dial
   after a non-graceful drop was refused as a duplicate of ITSELF for up to the ENet timeout, and the refusal
   told a single-install player to delete the playerid line from "one of the two" settings files.

   PRE-PATCH EQUIVALENT: refuse always. The offline row that fails on the old behaviour: 3.001 s of silence
   -> kDupIdEvictOld. */
const double kDupIdLiveSec = 3.0;

enum DuplicateIdAction
{
    kDupIdRefuse   = 0,   /* the connection already holding this id is live: the newcomer is refused in words */
    kDupIdEvictOld = 1    /* the old connection has been silent past the bound: it is dropped, the newcomer takes the id */
};

inline int DuplicateIdDecide(double oldPeerSilentSec, double liveSec)
{
    if (oldPeerSilentSec <= liveSec) return kDupIdRefuse;   /* the boundary itself is LIVE - a connection heard from within the window stands */
    return kDupIdEvictOld;
}

/* ================= THE SLOT NUMBER, KEYED ON THE ID =================
   A known id gets its old number back - even when a new id dialled first and would have taken it under
   AssignSlot's smallest-free rule. A new id gets the smallest number NO KNOWN ID holds, which is not the
   same as the smallest number no CONNECTED game holds: a number belonging to a player who is offline is
   still that player's, or RECORD.owner would rename past writers at the next restart. */
/* M1 (decisions 54 / 56(b), design-many D2): the numbers run 0 .. kSlotLifetimeMax - 1. It is a LIFETIME limit -
   a number is never handed back, because a player keeps it for the world's whole life - and a new id past it is
   REFUSED in words (storelink.h kRefuseLifetimeFull), never "given none". 65,520 is D2(b)'s figure: 16 blocks of
   65,536 group numbers plus 65,504 blocks of 32,768 is exactly 2^31, so every group number stays a positive int. */
const int kSlotLifetimeMax = 65520;

/* The search, over the SET of numbers already held - which is what makes it cheap. A full world answers -1 at
   once; otherwise the walk starts at lower_bound(floor), O(log n), and steps only over the run of consecutive
   held numbers that begins at the floor (M1: the old version scanned every known id for every candidate
   number, quadratic, and stopped at 1024). floor is --first-slot, a TEST option that lets a run reach high
   numbers without 20,000 games; it is 0 on every shipping path. */
inline int SlotFirstFreeDecide(const std::set<int>& used, int floor)
{
    if (floor < 0) floor = 0;
    if ((int)used.size() >= kSlotLifetimeMax) return -1;
    int c = floor;
    for (std::set<int>::const_iterator it = used.lower_bound(floor); it != used.end() && *it == c; ++it) ++c;
    return c < kSlotLifetimeMax ? c : -1;
}

/* The rule, for a caller that keeps the set beside the map (the notebook: g_slotsUsed). -1 = this world has
   admitted its lifetime limit of players and this id is new. */
inline int SlotAssignDecide(const std::map<std::string, int>& known, const std::set<int>& used,
                            const std::string& id, int floor)
{
    std::map<std::string, int>::const_iterator it = known.find(id);
    if (it != known.end()) return it->second;
    return SlotFirstFreeDecide(used, floor);
}

/* The same rule for a caller holding only the map (the offline suite): it builds the set, O(n log n). */
inline int SlotAssignDecide(const std::map<std::string, int>& known, const std::string& id, int floor = 0)
{
    std::map<std::string, int>::const_iterator it = known.find(id);
    if (it != known.end()) return it->second;
    std::set<int> used;
    for (std::map<std::string, int>::const_iterator k = known.begin(); k != known.end(); ++k) used.insert(k->second);
    return SlotFirstFreeDecide(used, floor);
}

/* ================= THE CONNECTED LIMIT (M1, decision 56(b)) =================
   How many games may be linked to the notebook at the same moment - a separate limit from the lifetime one
   above. connectedNow counts the admitted games OTHER than the one asking. At the limit the one asking is
   refused in words (storelink.h kRefuseFull) and may try again later. Named kConnect* because storelink.h's
   refusal REASON is already called kRefuseFull in this namespace. */
enum ConnectAdmitAction { kConnectAdmit = 0, kConnectRefuseFull = 1 };

inline int ConnectAdmitDecide(int connectedNow, int maxConnected)
{
    return (connectedNow < maxConnected) ? kConnectAdmit : kConnectRefuseFull;
}

/* ================= THE THREE FILES, ONE LINE AT A TIME =================
   All three are written in clock.txt's shape by the caller (a temp file plus MoveFileExA); what lives here is
   the LINE: the format and the parse, together, so that what the notebook writes is what it reads back. A
   line that does not parse is DROPPED and never guessed at - a corrupt slots.txt row that was guessed would
   hand one player's number, and therefore that player's past records, to somebody else. */

inline std::string AcNum(long long v) { char b[32]; std::sprintf(b, "%lld", v); return b; }

/* Split on tabs, exactly as the store's own loaders do, and drop a trailing CR so a file that has been
   through a text editor still reads. */
inline void AcFields(const std::string& lineIn, std::vector<std::string>* out)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    out->clear();
    size_t at = 0;
    while (at <= line.size())
    {
        const size_t t = line.find('\t', at);
        if (t == std::string::npos) { out->push_back(line.substr(at)); break; }
        out->push_back(line.substr(at, t - at));
        at = t + 1;
    }
}

/* ---- slots.txt: <playerId> <slot> <lastSeenUnix> ---- */
inline std::string SlotsLineFormat(const std::string& playerId, int slot, long long lastSeenUnix)
{
    return "v1\t" + playerId + "\t" + AcNum((long long)slot) + "\t" + AcNum(lastSeenUnix);
}

inline int SlotsLineParse(const std::string& line, std::string* playerId, int* slot, long long* lastSeenUnix)
{
    std::vector<std::string> f;
    AcFields(line, &f);
    if (f.size() < 4 || f[0] != "v1") return 0;
    if (!SlotKeyOk(f[1])) return 0;   /* prof1: a profile's key */
    const int s = std::atoi(f[2].c_str());
    if (s < 0 || s >= kSlotLifetimeMax) return 0;
    *playerId = f[1];
    *slot = s;
    *lastSeenUnix = (long long)std::atof(f[3].c_str());
    return 1;
}

/* ---- areas.txt: <x>,<y> <ownerPlayerId> <assignedUnix> ---- */
inline std::string AreasLineFormat(int x, int y, const std::string& ownerId, long long assignedUnix)
{
    return "v1\t" + AcNum((long long)x) + "," + AcNum((long long)y) + "\t" + ownerId + "\t" + AcNum(assignedUnix);
}

inline int AreasLineParse(const std::string& line, int* x, int* y, std::string* ownerId, long long* assignedUnix)
{
    std::vector<std::string> f;
    AcFields(line, &f);
    if (f.size() < 4 || f[0] != "v1") return 0;
    const size_t comma = f[1].find(',');
    if (comma == std::string::npos) return 0;
    const int px = std::atoi(f[1].substr(0, comma).c_str());
    const int py = std::atoi(f[1].substr(comma + 1).c_str());
    if (px < 0 || px >= 64 || py < 0 || py >= 64) return 0;
    if (!SlotKeyOk(f[2])) return 0;   /* prof1: a profile's key */
    *x = px; *y = py; *ownerId = f[2];
    *assignedUnix = (long long)std::atof(f[3].c_str());
    return 1;
}

/* decision 1556 (owner 2026-11-08; T-1285 restored; owner decisions 15 / 580 / 1552): WHO IS IN CHARGE OF ONE AREA. A game is in charge
   of an area it has LOADED when it is the first or only game with it loaded, or charge was passed to it; nothing else (no player character
   nearby) is asked. "Loaded" = the engine still KEEPS the area - the game's latest report WITHOUT the loading mark lists it (zones.cpp
   lists only areas one of whose three keep-alive countdowns is at its renewal value, loadedzones.h WhyFromCountdowns; an area counting
   down to unload after being left is not listed - T2182 LEG 5). A report WITH the loading mark (the engine's zone manager is loading,
   AreasReportListing 0) changes no listing and no charge. No clock decides a move here; the caller decides "gone" from events (link
   closed, clean quit, a new connection of the same player, its JOIN_STAGE LOADING) or from the reports stopping (the 10 s silence limit).
     holder      - the slot in charge now, -1 nobody.
     holderGone  - 1 the holder's game is gone.
     listers     - the slots whose latest unmarked report lists the area (the holder may be among them).
     since       - per lister, when its unbroken kept-listing of the area began (server arrival time of the first report of the run,
                   AreaListedSince); 0 = no times (every lister equal, the lowest slot decides).
   The answer, and *why:
     kChargeKeep           - the holder is here and lists the area: it keeps it (sticky - another game listing it changes nothing, so a
                             game coming back never takes back an area it gave up).
     kChargeAssign         - nobody held it: the first or only lister (several only after a hold ended: AreaChargePick).
     kChargePassDropped    - the holder's latest report no longer lists it and another game lists it: that game, at once.
     kChargePassGone       - the holder's game is gone and another game lists it: that game, at once.
     kChargeReleaseDropped - the holder's latest report no longer lists it and nobody else lists it: nobody (-1), at that report.
     kChargeRelease        - the holder's game is gone and nobody else lists it: nobody (-1).
     kChargeNone           - nobody held it and nobody lists it: -1.
   review M5 (accepted, counted): when BOTH games flap the same edge area, A dropping it passes it to B and B dropping it later passes it
   back to A; every move is counted (the area's `switches`, areaCharge[switches]). */
const int kChargeKeep = 0, kChargeAssign = 1, kChargePassDropped = 2, kChargePassGone = 3, kChargeRelease = 5, kChargeNone = 6, kChargeReleaseDropped = 7;
/* WHICH LISTER, when several list an area nobody (else) holds: the earliest unbroken kept-listing by server arrival time, then the lowest
   slot. `skip` (the holder) is never picked. The world server takes reports one at a time (store_main.cpp's one receive loop), so an
   area nobody holds goes to the first game whose report lists it; several listers meet only when a hold ends (its holder gone or no
   longer listing it). -1 = no lister. */
/* fold 1 (review L1): a lister whose latest report carries the loading mark (loading[i] 1 - it still lists from its last unmarked report)
   is picked only when no settled lister lists the area, so a frozen game is never given an area ahead of a settled one. loading 0 = none. */
inline int AreaChargePick(int nListers, const int* listers, const double* since, int skip, const int* loading = 0)
{
    int pick = -1, pickLoading = 0; double pickSince = 0.0;
    for (int i = 0; i < nListers; ++i)
    {
        const int s = listers[i];
        if (s < 0 || s == skip) continue;
        const double t = since != 0 ? since[i] : 0.0;
        const int l = (loading != 0 && loading[i] != 0) ? 1 : 0;
        if (pick < 0 || l < pickLoading || (l == pickLoading && (t < pickSince || (t == pickSince && s < pick)))) { pick = s; pickSince = t; pickLoading = l; }
    }
    return pick;
}
inline int AreaChargeDecide(int holder, int holderGone, int nListers, const int* listers, const double* since, int* why, const int* loading = 0)
{
    int holderLists = 0;
    for (int i = 0; i < nListers; ++i) if (holder >= 0 && listers[i] == holder) holderLists = 1;
    if (holder >= 0 && holderGone == 0 && holderLists != 0) { if (why != 0) *why = kChargeKeep; return holder; }
    const int pick = AreaChargePick(nListers, listers, since, holder, loading);
    if (pick >= 0) { if (why != 0) *why = holder < 0 ? kChargeAssign : (holderGone != 0 ? kChargePassGone : kChargePassDropped); return pick; }
    if (why != 0) *why = holder < 0 ? kChargeNone : (holderGone != 0 ? kChargeRelease : kChargeReleaseDropped);
    return -1;
}
/* fold 2 (review L5): THE SWEEP'S ANSWER FOR ONE AREA (store_main.cpp AreaChargeSweep), pure so the offline suite walks marked and
   unmarked reports through the restored-holder branch.
     held / holder       - the area has a holder (its row names a player) / that player's slot (-1 a player slots.txt does not know).
     goneEvent           - the holder's game is gone at this event (link closed, quit, JOIN_STAGE LOADING, a new connection of its player).
     restoredUnreported  - held, and the holder's player has sent no report WITHOUT the loading mark since this process started
                           (AreasReportListing: a loading-marked report does not count - T-1269, a save load is one long freeze).
     notBack / connected - that player is in T-1269's 'not back' set / has a connection now.
     heard               - the holder's slot sent a report of either kind within the grace (AreaSlotHeard).
   A restored claim stands (kChargeRestoredStands, the holder kept whoever lists the area) while its player is not back, or connected
   and not yet reported unmarked; otherwise a restored claim's holder is gone. Any other holder is gone at its event or its silence.
   Then AreaChargeDecide. */
const int kChargeRestoredStands = 8;
inline int AreaSweepDecide(int held, int holder, int goneEvent, int restoredUnreported, int notBack, int connected, int heard,
                           int nListers, const int* listers, const double* since, const int* loading, int* why)
{
    if (held != 0 && goneEvent == 0 && restoredUnreported != 0 && (notBack != 0 || connected != 0)) { if (why != 0) *why = kChargeRestoredStands; return holder; }
    int gone = 0;
    if (held != 0) gone = (goneEvent != 0 || restoredUnreported != 0 || holder < 0 || heard == 0) ? 1 : 0;
    return AreaChargeDecide(holder, gone, nListers, listers, since, why, loading);
}
/* T-1363 (design C, owner 1666): THE HAND-OVER WAIT. When the sweep's answer is kChargePassDropped (the holder's latest report no
   longer lists the area and another game lists it) and the holder is heard and live, the holder keeps the area while it writes it: the
   world server sends it an AREA_FRESH WRITE (the existing round, flagged as a hand-over) and passes charge on at the FIRST of
     - its record stored (the round answered FRESH)            -> kHandOverStored
     - a CANT of the round, any reason                          -> kHandOverCant
     - kChargeHandOverMs since the wait began                    -> kHandOverTimeout
     - the holder gone (link closed, the silence rule, its connection no longer live) -> kHandOverGone
   While waiting, the holder listing the area again ends the wait and it keeps it (kHandOverBack); nobody else listing it any more ends
   the wait and the area goes to nobody, as before (kHandOverNobody - release to nobody is unchanged). Never held forever: the wait has
   its own clock and every other end is an event the sweep sees.
     chargeWhy   - AreaSweepDecide's *why for the area now.
     waiting     - 1 a hand-over of this area is in progress (its WRITE out).
     holderLive  - 1 the holder's game is connected on the connection the wait began with and may be written to.
     answer      - kHandOverAnswerNone / Stored / Cant: what the round has said since the wait began.
     waitedMs    - how long the wait has run.
   The action: kHandOverApply (apply chargeWhy as before; *end says how a wait in progress ended, kHandOverNoEnd when none was),
   kHandOverStart (keep the holder, send the WRITE, start the clock), kHandOverWait (keep the holder, still waiting). */
const double kChargeHandOverMs = 8000.0;
const int kHandOverApply = 0, kHandOverStart = 1, kHandOverWait = 2;
const int kHandOverAnswerNone = 0, kHandOverAnswerStored = 1, kHandOverAnswerCant = 2;
const int kHandOverNoEnd = 0, kHandOverStored = 1, kHandOverCant = 2, kHandOverTimeout = 3, kHandOverGone = 4, kHandOverBack = 5, kHandOverNobody = 6;
inline const char* HandOverEndName(int end)
{
    switch (end)
    {
    case kHandOverStored: return "stored";
    case kHandOverCant: return "cant";
    case kHandOverTimeout: return "timeout";
    case kHandOverGone: return "gone";
    case kHandOverBack: return "back";
    case kHandOverNobody: return "nobody-else";
    default: return "none";
    }
}
inline int AreaHandOverDecide(int chargeWhy, int waiting, int holderLive, int answer, double waitedMs, int* end)
{
    if (end != 0) *end = kHandOverNoEnd;
    if (waiting == 0) return (chargeWhy == kChargePassDropped && holderLive != 0) ? kHandOverStart : kHandOverApply;
    int e = kHandOverNoEnd;
    if (chargeWhy == kChargeKeep) e = kHandOverBack;
    else if (answer == kHandOverAnswerStored) e = kHandOverStored;
    else if (answer == kHandOverAnswerCant) e = kHandOverCant;
    else if (chargeWhy == kChargeReleaseDropped) e = kHandOverNobody;
    else if (chargeWhy != kChargePassDropped || holderLive == 0) e = kHandOverGone;
    else if (waitedMs >= kChargeHandOverMs) e = kHandOverTimeout;
    if (e == kHandOverNoEnd) return kHandOverWait;
    if (end != 0) *end = e;
    return kHandOverApply;
}
/* T-1363 fold 1 (review 3): may a hand-over's WRITE join the AREA_FRESH round already out for its area? Only a WRITE to this holder
   (sameHolder) on the connection it holds from (sameConnect), not answered yet, under the round limit (ageMs < limitMs) AND sent at or
   after the report that dropped the area (sentAt >= dropAt, one clock): an older WRITE's FRESH carries a record written before the drop,
   and would end the wait 'stored' with every later change lost. 1 = join it, 0 = start a new round (a new WRITE). */
inline int AreaHandOverJoin(int havePending, int sameHolder, int sameConnect, int answered, double ageMs, double limitMs, double sentAt, double dropAt)
{
    return (havePending != 0 && sameHolder != 0 && sameConnect != 0 && answered == 0 && ageMs < limitMs && sentAt >= dropAt) ? 1 : 0;
}
/* T-1363 fold 1 (review 5): does the area's wait take a hand-over round's answer? Only while a wait is in progress (hoStart > 0), when
   the area's holder is still the round's holder, on the connection the WRITE went to, and the wait has no answer yet (the first answer
   wins). Any other answer of the round leaves the wait as it is. */
inline int AreaHandOverTakes(int rowFound, double hoStart, int owner, int holder, unsigned rowConnect, unsigned holderConnect, int answer)
{
    return (rowFound != 0 && hoStart > 0.0 && owner == holder && rowConnect == holderConnect && answer == kHandOverAnswerNone) ? 1 : 0;
}
/* T-1363 fold 2 (re-check 3): AN ANSWER TO THE ROUND A HAND-OVER REPLACED NEVER ENDS THE HAND-OVER. When a new round's WRITE goes to the
   same holder connection while that connection's earlier WRITE is still unanswered, one answer is owed to the old round. The game answers
   its WRITEs in the order they came (one queue, store.cpp AfHolderStep) and sends each answer, and each area record, on one ordered
   reliable channel (AfSend / SendRecordAny: g_link->Send(0, ..., CH_RELIABLE)); the world server sends both WRITEs on one line too. So
   the first FRESH or CANT from that connection after the new WRITE is the old round's, and is ignored. A record of the area refused as
   unreadable while an answer is still owed was sent before the old round's answer, so it is the old round's too: ignored, and not
   counted (the old round's FRESH still comes after it). If the game merged the two WRITEs into one ("one write answers both"), its single
   answer is ignored and the hand-over ends at its 8 s limit - late, never wrong.
   AreaRoundOwed: the owed count after a new round goes out - roundOut / answered = the row's round before it, sameHolderConnect = the new
   WRITE goes to the slot and connection the old one went to (otherwise the old connection's answers fail the row's own check: 0). */
const int kRoundOwedMax = 8;
inline int AreaRoundOwed(int owed, int roundOut, int answered, int sameHolderConnect)
{
    if (sameHolderConnect == 0) return 0;
    const int n = (owed > 0 ? owed : 0) + ((roundOut != 0 && answered == 0) ? 1 : 0);
    return n > kRoundOwedMax ? kRoundOwedMax : n;
}
/* what the row does with an answer from its holder's connection: refusal = 1 for its area record refused as unreadable, 0 for a FRESH
   or CANT. kAnswerTake = this round's answer (as before); kAnswerOwed = the replaced round's answer (ignored, one less owed);
   kAnswerOldRecord = the replaced round's refused record (ignored, still owed). */
const int kAnswerTake = 0, kAnswerOwed = 1, kAnswerOldRecord = 2;
inline int AreaAnswerOwed(int owed, int refusal)
{
    if (owed <= 0) return kAnswerTake;
    return refusal != 0 ? kAnswerOldRecord : kAnswerOwed;
}
/* THE LISTING CLOCKS (pure, so the offline suite walks them). The world server keeps per slot two arrival times - `stamp`, its latest
   report WITHOUT the loading mark (what it lists is what it keeps), and `heard`, its latest report of either kind (the silence clock) -
   and per area per slot `lastSeen`, the stamp of the latest unmarked report that listed it, and `since`, when that unbroken listing began.
   A slot lists an area when lastSeen equals its stamp and the slot has been heard within the grace: a loading-marked report moves only
   `heard`, so a game loading for 44 s (T2203 LEG T) still lists - and keeps - what its last unmarked report listed, and its first
   unmarked report afterwards decides (T-1269: a save load is one long freeze). */
inline int AreasReportListing(int loadingMark) { return loadingMark != 0 ? 0 : 1; }   /* 1 = the report's list is a kept listing; 0 = loading: silence clock and position only */
inline int AreaSlotHeard(int haveHeard, double heard, double now, double grace) { return (haveHeard != 0 && now - heard <= grace) ? 1 : 0; }
inline int AreaListedNow(int haveStamp, double stamp, int haveHeard, double heard, int haveSeen, double lastSeen, double now, double grace)
{
    if (haveStamp == 0 || haveSeen == 0 || AreaSlotHeard(haveHeard, heard, now, grace) == 0) return 0;
    return lastSeen == stamp ? 1 : 0;
}
/* an unmarked report arriving at `now` lists the area: the run goes on when the slot's previous unmarked report (its stamp, havePrev)
   listed it too (lastSeen == that stamp), else it starts now. */
inline double AreaListedSince(int havePrev, double prevStamp, int haveSeen, double lastSeen, int haveSince, double since, double now)
{
    return (havePrev != 0 && haveSeen != 0 && haveSince != 0 && lastSeen == prevStamp) ? since : now;
}

/* T-1442 step 1 (owner: events over timers): THE AREA MAP SENT AT THE CHANGE (store_main.cpp AreaChargeSweep / AreaMapFlush).
   AreaMapDueMark: the 'map due' mark after a sweep step - set when a holder moved (holderMoved 1), kept when already set; only a map
   that went out clears it (AreaMapFlush, AreaTick). AreaMapFlushDecide: after an event, kMapFlushSend when a map is due and the maps are
   not stopped (the TEST-ONLY --stop-areamaps-after); kMapFlushStopped when due but stopped (nothing sent, the mark kept); kMapFlushNone
   when nothing is due. */
const int kMapFlushNone = 0, kMapFlushSend = 1, kMapFlushStopped = 2;
inline int AreaMapDueMark(int due, int holderMoved) { return (due != 0 || holderMoved != 0) ? 1 : 0; }
inline int AreaMapFlushDecide(int due, int stopped)
{
    if (due == 0) return kMapFlushNone;
    return stopped != 0 ? kMapFlushStopped : kMapFlushSend;
}
/* fold 1b (review 5): THE STEP AreaMapFlush RUNS - the decision, and a map sent clears the mark (a stopped flush keeps it) */
inline int AreaMapFlushStep(int* due, int stopped)
{
    const int d = AreaMapFlushDecide(*due, stopped);
    if (d == kMapFlushSend) *due = 0;
    return d;
}

}   /* namespace coopstore */

#endif
