/* src/common/campgame.h - THE GAME'S SIDE OF THE WORLD'S CAMP LIST (T-929, CAMP 93, src/common/campwire.h). Pure: the game
 * (src/coop-plugin/camp.cpp) and the offline suite (src/coop-test) compile this same header.
 *
 * THE ENGINE'S CAMP PASS (1.0.65 0x8F6440, build/decomp_8f6440.txt), every 2 s on the engine's background worker: for each region
 * whose camps number less than its limit times the world's camp scale (region+0 int, region+0x40 count, the float at 0x2132538), it
 * picks a camp kind from the region's "nests" list (0x57F880), a spot (0x8F5480) and builds the camp (0x582970), then returns. The
 * game turns that pass into ONE ROAD whatever the player count:
 *   DRAW   the engine's own kind and spot become a PROPOSE (nothing is built); later regions are skipped; the pass ends.
 *   BUILD  an accepted camp (this game's proposal, or one another game's proposal added to the list) is built by the same pass with
 *          its kind and spot forced in the two pickers: the engine's own build, town-list entry and furniture step run as for any camp.
 *   SKIP   the pass does nothing (a proposal waits for its answer, or the world's list is not here).
 * A single-player game answers its own proposal (there is no other game to agree with): accepted at once, then built by BUILD. */
#ifndef COOP_COMMON_CAMPGAME_H
#define COOP_COMMON_CAMPGAME_H

#include <cmath>
#include "campwire.h"

namespace campg {

/* THE REGION'S LIMIT AS THE ENGINE APPLIES IT: the pass builds while count < limit * scale, so the most camps the region holds is the
   smallest whole number n with !(n < limit * scale) - ceil(limit * scale), the product taken in float as the engine takes it. 0 =
   unreadable (no proposal is made from it). */
const unsigned int kCampCapMax = 100000;
inline unsigned int CampCapOf(int limit, float scale)
{
    if (limit <= 0 || !(scale > 0.0f) || !(scale < 1.0e6f)) return 0;
    const float vf = (float)limit * scale;   /* the engine multiplies in float (decomp_8f6440: (float)limit * scale) */
    const double v = (double)vf;
    if (!(v > 0.0) || v > (double)kCampCapMax) return 0;
    const double c = std::ceil(v);
    return c < 1.0 ? 1u : (unsigned int)c;
}

/* WHAT ONE ENGINE PASS DOES (read at the pass's start, under the camp lock). hooked = all four hooks are in (else the engine runs as
   itself - nothing of this rule is in force); single = a single-player game; scanDue = this game's own camps are to be read (SCAN: the
   pass reads the town list's camps instead of running - for the seed or for matching the list, CampScanWhy); haveBuild = an accepted
   camp waits to be built (CampTakeViewRetry says when a camp waiting out the view rule counts); waiting = this game's proposal waits
   for its answer; listComplete / listState = the world server's whole list arrived (BEGIN..END) and its state (campl::kListNone = no
   list yet: the seed comes first; kListUnreadable = the world server refuses every change this session). */
enum { kPassEngineAlone = 0, kPassBuild = 1, kPassDraw = 2, kPassSkip = 3, kPassScan = 4 };
inline int CampPassMode(int hooked, int single, int scanDue, int haveBuild, int waiting, int listComplete, unsigned int listState)
{
    if (!hooked) return kPassEngineAlone;
    if (scanDue) return kPassScan;
    if (haveBuild) return kPassBuild;
    if (waiting) return kPassSkip;
    if (single) return kPassDraw;
    if (!listComplete || listState != (unsigned int)campl::kListHere) return kPassSkip;
    return kPassDraw;
}

/* WHO ANSWERS A CALL TO THE KIND PICKER 0x57F880 ON A CAMP PASS'S THREAD. The picker serves every caller of the engine: the pass asks
   it for a camp kind (the region's "nests" list, type 13 - decomp_8f6440), and the camp's own build asks it again inside 0x582970 (town
   setup 0x9344F0 picks the camp's "material", type 0x2C - decomp_9344f0, reached through decomp_580ce0). Only the pass's own call -
   outside the build (spawnDepth 0) and of the town-record type - follows the rule; every other call is the engine's own pick. */
enum { kKindEngine = 0, kKindRule = 1 };
inline int CampKindAnswer(int inPass, int spawnDepth, int type, int townType) { return (inPass && spawnDepth == 0 && type == townType) ? kKindRule : kKindEngine; }

/* A STORED outside a whole list (an accepted proposal: this game's own answer, or another game's camp added to the list). It is built
   here unless this game already holds it: the key is in the list it holds (a duplicate's answer carries the stored camp it collided
   with) or this game built or queued it this session. */
enum { kStoredBuild = 0, kStoredKnown = 1 };
inline int CampStoredAction(int inList, int builtHere) { return (inList || builtHere) ? kStoredKnown : kStoredBuild; }

/* A REFUSED to this game. key "?" = the world server could not read a CAMP message: every waiting proposal ends. Otherwise the answer
   to the waiting proposal ends it; "region full" also marks the region (no proposal for it until a camp of it is removed). A REFUSED
   for another key is stale (counted only). */
enum { kRefusedStale = 0, kRefusedEnds = 1, kRefusedEndsMarkFull = 2 };
inline int CampRefusedAction(int waiting, int keyIsWaiting, int keyUnreadable, unsigned int reason)
{
    if (!waiting) return kRefusedStale;
    if (keyUnreadable) return kRefusedEnds;
    if (!keyIsWaiting) return kRefusedStale;
    return reason == (unsigned int)campl::kCampRegionFull ? kRefusedEndsMarkFull : kRefusedEnds;
}

/* THE END OF A WHOLE LIST: complete only when a BEGIN opened it and the STOREDs held number exactly its count (an END's count is the
   BEGIN's). Anything else leaves the list incomplete - no proposal until the next whole list. */
inline int CampListEndComplete(int begun, unsigned int beginCount, unsigned int endCount, unsigned int held)
{
    return (begun && beginCount == endCount && held == beginCount) ? 1 : 0;
}

/* A FORCED BUILD'S ENDING, read after the engine's pass returned: the forced kind was handed out in the camp's region, the spot was
   near this game's view point (CampNearView - the spot was withheld), the forced spot was handed out after the kind, and the engine's
   build (0x582970) returned a camp. Built ends it; NEAR VIEW keeps it (it waits for the view point to leave - CampTakeViewRetry); every
   other outcome ends it as NOT_BUILT (campl::kNbEngineRefused) - the region was not reached this pass (it holds its limit on this
   game, or it is not in the engine's table) or the engine refused the spot. */
enum { kBuiltOk = 0, kBuiltRegionNotReached = 1, kBuiltEngineRefused = 2, kBuiltNearView = 3 };
inline int CampBuildOutcome(int kindGiven, int nearView, int spotGiven, int spawned)
{
    if (!kindGiven) return kBuiltRegionNotReached;
    if (nearView) return kBuiltNearView;
    if (spotGiven && spawned) return kBuiltOk;
    return kBuiltEngineRefused;
}

/* T-929 part 7 - THE ENGINE'S VIEW RULE ON EVERY GAME. The engine never draws a camp spot closer than its distance (the constant
   1382.4 the spot test 0x8F4200 copies from 0x172F4F0, the CampViewDistance row) to this game's view point (PlayerInterface 0x7F1DC0):
   a camp never appears in front of a player. The proposer's draw obeys it for its own view point only, so every game applies it to
   its own view point when it BUILDS a camp of the list - the real position, read on the pass's thread as the engine's test reads it.
   A spot within the distance (squared 3-D distance, as Ogre::Vector3::squaredDistance) is not built now: it waits. */
inline int CampNearView(const float* view, const float* spot, float dist)
{
    if (!(dist > 0.0f)) return 0;
    const float dx = view[0] - spot[0], dy = view[1] - spot[1], dz = view[2] - spot[2];
    return (dx * dx + dy * dy + dz * dz < dist * dist) ? 1 : 0;
}
/* A camp waiting out the view rule is tried again only on every other pass while nothing else is queued, so a view point that
   stays put cannot stop this game's other builds or its draws: the pass retries it when the build queue is empty, one waits, and the
   previous pass was not itself such a retry. */
inline int CampTakeViewRetry(int buildQueued, int viewWaiting, int lastWasRetry) { return (!buildQueued && viewWaiting && !lastWasRetry) ? 1 : 0; }

/* T-929 parts 2 / 3 - WHY THIS GAME READS ITS OWN CAMPS (main thread, when a whole list ends or a world was loaded). SEED: the world
   has no list and this game has not sent its seed on this link (owner 1376 - its camps become the list); a single-player game reads
   its own once per world load (its list is its own). MATCH: the list is here - this game's camps are made to match it (owner 1376: a
   camp not on the list is removed, a list camp it lacks is built). NONE: the list is incomplete or unreadable (every game keeps its
   own camps), or the seed went out on this link already. */
enum { kScanNone = 0, kScanSeed = 1, kScanMatch = 2 };
inline int CampScanWhy(int single, int loadScanned, int listComplete, unsigned int listState, int seededThisLink)
{
    if (single) return loadScanned ? kScanNone : kScanSeed;
    if (!listComplete) return kScanNone;
    if (listState == (unsigned int)campl::kListNone) return seededThisLink ? kScanNone : kScanSeed;
    if (listState == (unsigned int)campl::kListHere) return kScanMatch;
    return kScanNone;
}

/* ONE CAMP READ FROM THIS GAME'S TOWN LIST (its kind's sid, its region's world-record sid, the region's limit, the world's camp scale,
   its position) as a list record: false when it cannot be one (campl::CampValid). */
inline bool CampFromScan(const std::string& kind, const std::string& region, int limit, float scale, float x, float y, float z, campl::Camp* out)
{
    campl::Camp c;
    c.kind = kind; c.region = region; c.x = x; c.y = y; c.z = z;
    c.regionCap = CampCapOf(limit, scale);
    c.key = campl::CampKey(kind, x, z);
    if (!campl::CampValid(c)) return false;
    *out = c;
    return true;
}

/* WHICH LIST CAMP A CAMP OF THIS GAME IS. Its key first (the kind and the spot rounded); else the nearest list camp of the same kind
   within kCampMatchDist that no other camp of this game took (a camp's centre may sit off its spot by a rounding step). Each list camp
   names at most one camp of this game. */
const float kCampMatchDist = 4.0f;
struct LiveCamp { std::string kind; float x, z; LiveCamp() : x(0), z(0) {} };
/* toBuild: list keys no camp of this game is; toRemove: indexes into live of camps no list camp names (owner 1376). */
/* liveKey (optional): the list key each camp of this game is, "" for one to remove. */
inline void CampMatchLive(const std::map<std::string, campl::Camp>& list, const std::vector<LiveCamp>& live, std::vector<std::string>* toBuild, std::vector<size_t>* toRemove,
                          std::vector<std::string>* liveKey = 0)
{
    toBuild->clear(); toRemove->clear();
    if (liveKey != 0) liveKey->assign(live.size(), std::string());
    std::set<std::string> used;
    std::vector<int> matched(live.size(), 0);
    for (size_t i = 0; i < live.size(); ++i)
    {
        const std::string k = campl::CampKey(live[i].kind, live[i].x, live[i].z);
        const std::map<std::string, campl::Camp>::const_iterator it = list.find(k);
        if (it != list.end() && it->second.kind == live[i].kind && used.insert(k).second) { matched[i] = 1; if (liveKey != 0) (*liveKey)[i] = k; }
    }
    for (size_t i = 0; i < live.size(); ++i)
    {
        if (matched[i]) continue;
        std::string best; float bestD2 = kCampMatchDist * kCampMatchDist;
        for (std::map<std::string, campl::Camp>::const_iterator it = list.begin(); it != list.end(); ++it)
        {
            if (it->second.kind != live[i].kind || used.find(it->first) != used.end()) continue;
            const float dx = it->second.x - live[i].x, dz = it->second.z - live[i].z, d2 = dx * dx + dz * dz;
            if (d2 <= bestD2) { bestD2 = d2; best = it->first; }
        }
        if (!best.empty()) { used.insert(best); matched[i] = 1; if (liveKey != 0) (*liveKey)[i] = best; }
    }
    for (size_t i = 0; i < live.size(); ++i) if (!matched[i]) toRemove->push_back(i);
    for (std::map<std::string, campl::Camp>::const_iterator it = list.begin(); it != list.end(); ++it) if (used.find(it->first) == used.end()) toBuild->push_back(it->first);
}
/* The list key one camp of this game is (the same rule, alone); "" = none. */
inline std::string CampMatchOne(const std::map<std::string, campl::Camp>& list, const LiveCamp& c)
{
    std::vector<LiveCamp> one(1, c); std::vector<std::string> b; std::vector<size_t> r;
    std::map<std::string, campl::Camp> sub;
    for (std::map<std::string, campl::Camp>::const_iterator it = list.begin(); it != list.end(); ++it) if (it->second.kind == c.kind) sub.insert(*it);
    CampMatchLive(sub, one, &b, &r);
    if (!r.empty()) return std::string();
    std::set<std::string> unbuilt(b.begin(), b.end());
    for (std::map<std::string, campl::Camp>::const_iterator it = sub.begin(); it != sub.end(); ++it) if (unbuilt.find(it->first) == unbuilt.end()) return it->first;
    return std::string();
}

/* T-929 part 4 - A CAMP GONE FROM THIS GAME. The engine removes a camp in play only through its cell check 0x8F5100 (a camp whose spot
   no longer holds when its cell loads: Nest vt+0x208, then TownDestroy 0x92A4F0 - decomp_8f5100); its other TownDestroy callers are
   the town list's clear (camps already unlisted), the save import's "remove every camp" 0x92B6D0 (from 0x3788F0), the town merge
   (player towns) and an editor tool. A wiped-out camp is NOT removed (0x92E140 only shows its message). So a GONE is sent only for a
   TownDestroy whose caller is that cell check (its return address, the CampNestCheckRet row), for a camp of the list, while the list
   is whole on the link - the world server takes it only from the camp's area holder. Single player: the camp leaves its own list. */
inline int CampGoneSend(int fromNestCheck, int ownRemoval, int haveKey, int single, int listComplete, int linkUp)
{
    if (!fromNestCheck || ownRemoval || !haveKey) return 0;
    return (single || (listComplete && linkUp)) ? 1 : 0;
}

/* A removal of this game's camp (part 3) runs only when the camp is still the one the scan read: still on the town list's camp list,
   the same kind, within kCampMatchDist of where the scan saw it. */
inline int CampRemoveStill(int listed, int sameKind, float dx, float dz) { return (listed && sameKind && dx * dx + dz * dz <= kCampMatchDist * kCampMatchDist) ? 1 : 0; }
/* ...and only while its area is not loaded on this game - the engine removes a camp only as its area loads (the cell check 0x8F5100 from
   processLoading 0xA0E950, before the area's loaded byte is set), never in front of a player. keep = the engine's own test of the camp's
   area, as its camp pass reads it (0xA081E0 / 0xA08200: ZoneMap +0xB1 or +0xB0 set - ZoneKeepSectorTri): 1 kept, 0 not, -1 unreadable.
   A held removal is tried again on every later tick until its area is no longer kept. */
enum { kRemoveNow = 0, kRemoveHold = 1 };
inline int CampRemoveWhen(int keep) { return keep == 0 ? kRemoveNow : kRemoveHold; }

/* THIS GAME'S CAMPS ARE READ (SCAN) ONLY ONCE ITS WORLD RUNS: the current world load reached its first running tick (the in-game frame
   counter moved - StoreWorldLoadAgeMs is known) and no engine write is blocked (no load or teardown under way - EngineWritesBlocked). A
   town list read before that may hold part of the save's camps, and a seed made from it would become the world's list. */
inline int CampScanWorldReady(int loadAgeKnown, int writesBlocked) { return (loadAgeKnown && !writesBlocked) ? 1 : 0; }

/* A LIST CAMP THAT ENDED NOT BUILT here (its region was not reached - it held its limit on this game, e.g. while a removal waits for its
   area to unload - or the engine refused the build) is queued again at the next match, at most kCampBuildTries times in one world load.
   A camp whose kind this game does not have is never tried again (CampTriesNoKind). */
const int kCampBuildTries = 3;
const int kCampTriesNoKind = 1000;
inline int CampRetryBuild(int tries) { return tries < kCampBuildTries ? 1 : 0; }

/* T-929 part 6 - A GROUP'S HOME CAMP. A group whose home is a camp of the list names it by the camp's list key; the kind's sid alone
   names the FIRST camp of that kind (GetTownBySid), the wrong one where a kind has several camps. The receiver resolves a key to its
   own camp of that key (none while it is not built here - never the kind's first camp); a group with no key (its home is a town, or the
   sender holds no list) keeps the sid lookup. A home that is on the engine's camp list but names no list key on the sender (not matched
   there yet, or a camp the list does not hold) travels as the marker kCampKeyUnknown - never a key (every key holds '@') - and the
   receiver gives no home town for it (and owes none). */
const char* const kCampKeyUnknown = "?";
enum { kHomeNone = 0, kHomeBySid = 1, kHomeByCampKey = 2, kHomeCampUnknown = 3 };
inline int CampHomeWay(int haveCampKey, int keyUnknown, int haveSid) { return keyUnknown ? kHomeCampUnknown : haveCampKey ? kHomeByCampKey : haveSid ? kHomeBySid : kHomeNone; }
/* what the sender puts in the key field: the key when its camp of that key still reads as it; the marker when it holds the whole list
   and the home is on the engine's camp list; else nothing (a town, or no list here - the sid decides) */
enum { kSendNoKey = 0, kSendKey = 1, kSendKeyUnknown = 2 };
inline int CampKeyToSend(int mappedStill, int listHeld, int onCampList) { return mappedStill ? kSendKey : (listHeld && onCampList) ? kSendKeyUnknown : kSendNoKey; }
/* the home building on the key path (a key or the marker): kept only when the engine's own town of it (BuildingGetTown) is the keyed camp
   - a sent handle can name another camp's building here by chance; else none, and the owed-home check gives it by its position key */
inline int CampHomeBuildingKept(int keyPath, int haveBuilding, int buildingTownIsCamp) { return (haveBuilding && (!keyPath || buildingTownIsCamp)) ? 1 : 0; }
/* A GROUP BUILT WITH A KEY WHOSE CAMP IS NOT BUILT HERE is owed its home town: listed (at most kCampHomeOwedCap groups), and given when
   that key's camp is built or matched here; an entry leaves when the group goes or the world unloads. */
const unsigned int kCampHomeOwedCap = 512;
enum { kOweNo = 0, kOweYes = 1, kOweFull = 2 };
inline int CampHomeOwe(int homeWay, int townFound, unsigned int owedNow) { return (homeWay != kHomeByCampKey || townFound) ? kOweNo : owedNow < kCampHomeOwedCap ? kOweYes : kOweFull; }

/* A PROPOSAL'S WAIT ENDS when its answer arrives (above), or when the link it went out on is gone: the link is down, or a newer link
   (another connection) is up - that link's world server never saw it. */
inline int CampWaitLinkGone(int linkUp, long sentGen, long nowGen) { return (!linkUp || sentGen != nowGen) ? 1 : 0; }

/* A LIST CAMP'S FURNITURE IS LAID OUT AROUND ITS LIST SPOT (option A, 2026-11-02, run T1860). The engine lays a camp's furniture out
   again at every area load (CampDebrisPlace 0x937570) around a point it queued - the camp's position, or a home building's current
   position (decomp_9344f0:328-335, decomp_57e760:96-98) - and seeds its random stream from that point's whole x / z
   (decomp_937570:111-117): a live point, so the layout differed between games and between one game's visits. For this game's camp of
   a list key (still reading as that camp) the point's x and z become the list spot; the height stays the engine's (the list's height
   can be the engine's "no ground" -99). Every other step keeps the engine's point: a town's houses, a camp the list names no key for
   here, a step off the main thread (the key map is the main thread's), and a step centred on one of the camp's buildings (a
   'nest' building's step, decomp_57e760:68-98, passes that building as its hand; only the camp's own step passes none,
   decomp_9344f0:335 - review 2026-11-02: forcing a building's step would lay its pieces at the camp's centre). One road: single
   player names its own camps by key too. */
enum { kDebrisEngine = 0, kDebrisForced = 1 };
inline int CampDebrisPoint(int onMain, int listCamp, int campCentre, float listX, float listZ, const float* engine, float* out)
{
    out[0] = engine[0]; out[1] = engine[1]; out[2] = engine[2];
    if (!onMain || !listCamp || !campCentre) return kDebrisEngine;
    out[0] = listX; out[2] = listZ;
    return kDebrisForced;
}

}   // namespace campg

#endif   // COOP_COMMON_CAMPGAME_H
