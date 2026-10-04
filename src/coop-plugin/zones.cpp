// zones.cpp - M-A step 1. See zones.h.
#include "zones.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "store.h"   /* StoreSendAreas, StoreRelayLinked (decision 32) */
#include "ai_spike.h"      // GetTarget(): the watched player character (P006)
#include "net/session.h"
#include "playerfaction.h"   // LocalPlayerFaction (the ownbuilding lever)
#include "replicate.h"       // E25: PeerPlayerPosition - where the OTHER player was last seen
#include "../common/squadwriter.h"   /* M7a3f5 [m7a3f5-zc0]: PutAwayOtherKeeps, swept by the offline suite */
#include "../common/areadrop.h"   /* W1-b (review 2026-09-22 H2/H3): the effective area map as a pure function, swept by the offline suite */
#include "../common/orphanhold.h"   /* T-306 (owner decision 226; fold 1): when an orphaned zone tick sends its player sector as unknown, swept by the offline suite */
#include "../common/presence.h"   /* M11 C1: is another player in this world - the old link or the roster (PresenceDecide) */
#include "../common/peergone.h"   /* M8 review F9: the loaded-bit rule of a departure (PeerGoneMaskBits), swept by the offline suite */
#include "coop_log.h"
#include "game/Character.h"
#include "game/RootObjectBase.h"
#include "game/GameWorld.h"   /* recruit3: activeCharacters (recruitlist) */
#include "../common/recruitmult.h"   /* recruit3: RecruitSlotCount */
#include "game/lektor.h"   // F478 diagnostic: the engine fills a lektor of active zones
/* inv5: ZoneMapContent::items is read as a raw field at +0x110 (no ZoneMapContent class is declared) */
#include "game/hand.h"        /* inv5: hand::getItem */
#include <ogre/OgreVector3.h>
#include <Windows.h>
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace coop {
namespace {

const float kSectorSize   = 4608.0f;
const float kSectorOrigin = 147456.0f;   // 4608 * 32
unsigned long long kZoneManagerPtrRva = 0; static coop::AddrReg kZoneManagerPtrRva_reg("ZoneManagerPtr", &kZoneManagerPtrRva);   /* P8h: the address table fills this. Steam_1.0.65 0x2133960 */   // DAT_142133960: ZoneManager*
unsigned long long kIsZoneLoadedTRva = 0; static coop::AddrReg kIsZoneLoadedTRva_reg("IsZoneLoadedT", &kIsZoneLoadedTRva);   /* P8h: the address table fills this. Steam_1.0.65 0xA0D560 */    // bool ZoneManager::isZoneLoadedT(const Ogre::Vector3&) - resolver-verified
const int kRing = 3;                               // 7x7 sectors probed around the player's sector each second
const int kTickEvery = 118;                        // retired by P4s: the report is gated by the clock (review-p4o Q5: 118 frames is 10 s below 12 fps)

typedef bool (*IsZoneLoadedFn)(void* zoneManager, const Ogre::Vector3& pos);

std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string F1(float v) { std::ostringstream o; o.precision(1); o << std::fixed << v; return o.str(); }
double NowSec() { static LARGE_INTEGER f; static bool have = false; if (!have) { QueryPerformanceFrequency(&f); have = true; } LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }   /* review-p4q: the frequency is a constant */

bool PlausiblePod(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }

// POD SEH wrappers
// ZoneManager::getAllActiveZones(lektor<ZoneMap*>&) 0xA09840, ZoneMap::isActivationType(ZoneActivationType) 0xA07B70 (0 camera,
// 1 player character, 2 town), ZoneMap::getDeactivationCountdown 0xA084B0 = max(camera, player) countdown - address-table rows.
static unsigned long long kZnIsActivationTypeRva = 0; static coop::AddrReg kZnIsActivationTypeRva_reg("ZoneIsActivationType", &kZnIsActivationTypeRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0xA07B70 */
static unsigned long long kZnDeactivationCountdownRva = 0; static coop::AddrReg kZnDeactivationCountdownRva_reg("ZoneDeactivationCountdown", &kZnDeactivationCountdownRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0xA084B0 */
static unsigned long long kZnGetAllActiveZonesRva = 0; static coop::AddrReg kZnGetAllActiveZonesRva_reg("ZoneGetAllActiveZones", &kZnGetAllActiveZonesRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0xA09840 */
static unsigned long long kZnTownListRva = 0; static coop::AddrReg kZnTownListRva_reg("TownList", &kZnTownListRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x21330A0 */
typedef void  (*GetAllActiveZonesFn)(void* zm, lektor<void*>* out);
typedef bool  (*IsActivationTypeFn)(void* zoneMap, int type);
typedef float (*DeactivationCountdownFn)(void* zoneMap);
int ReadZoneReasonPod(void* m, int* cx, int* cy, char* c, char* p, char* t, float* cd)
{
    if (kZnIsActivationTypeRva == 0 || kZnDeactivationCountdownRva == 0) return 0;
    __try
    {
        *cx = *(int*)((char*)m + 0x18); *cy = *(int*)((char*)m + 0x1C);
        IsActivationTypeFn isType = (IsActivationTypeFn)coop::AddrAbs(kZnIsActivationTypeRva);
        *c = isType(m, 0) ? 'c' : '-'; *p = isType(m, 1) ? 'p' : '-'; *t = isType(m, 2) ? 't' : '-';
        *cd = ((DeactivationCountdownFn)coop::AddrAbs(kZnDeactivationCountdownRva))(m);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int FillActiveZonesPod(void* zm, lektor<void*>* zones)
{
    if (kZnGetAllActiveZonesRva == 0) return 0;
    __try { ((GetAllActiveZonesFn)coop::AddrAbs(kZnGetAllActiveZonesRva))(zm, zones); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// P6w (review-p6n HIGH-1). WHY THERE IS NO ACTIVITY TEST IN THE BUILDINGS WALK, written here because
// P6n put one in and it had to come out again.
//
// THE SHORT OF IT. Membership in the list `ZoneManager::getAllActiveZones` hands back already IS the
// liveness test. The test P6n added measured a KEEP-ALIVE LEASE, not destruction, so it could only ever
// withhold zones that were still loaded and still full of buildings. And the observation it was built on
// - F544(e)'s resolve of a zone-20.32 building at 256 s, 55 s after that zone's level data was written -
// is unexplained: F544 recorded it as unexplained and it is unexplained still, because the decompiles
// below say the active list cannot go stale on the ordinary path. P6n's comment here promoted it into
// "the shape this exists to make visible", which is an inference written as an observation.
//
// P6n's premise was that `ZoneManager::getAllActiveZones` 0xA09840 can hand back a zone the engine has
// stopped keeping, so the walk should ask `ZoneMap::isActivationType` 0xA07B70 before reading one. The first
// half is true: 0xA09840 applies no test at all - it walks the manager's own hash set and copies every node's
// key (build/decomp_a09840.txt, Confirmed). THE CONCLUSION IS NOT. `ZoneManager::deactivateZone` 0xA09BB0 is
// three statements (build/decomp_a09bb0.txt, Confirmed): ZoneMap::deactivate 0xA09620, then 0x9F0100 on the
// object at ZoneManager+0x168108. 0x9F0100 IS a boost unordered_set erase-by-key (build/decomp_9f0100.txt:
// it hashes *key, walks bucket [+0x38][idx], unlinks the node and frees it), and that container base is
// exactly the one 0xA09840 reads - +0x168120 bucket index, +0x168128 size, +0x168140 buckets. So an
// ordinarily unloaded zone is REMOVED FROM THE VERY LIST THIS WALK READS, in the same call that unloaded it.
// MEMBERSHIP IN THE LIST IS ALREADY THE LIVENESS TEST, and nothing needs to be asked on top of it. The one
// path that leaves an entry standing is `ZoneManager::deactivateAll` 0xA098E0, which walks the fixed 64x64
// array and never touches the set - but it also writes 0 to the ZoneMap's content pointer (*ZoneMap = 0), so
// ContentOfZonePod returns 0 and PlausiblePod skips it one line later, as it did before P6n.
//
// WHAT THE LEASE TEST ACTUALLY MEASURED, and why keeping it was worse than not having it. 0xA07B70 is
// `DAT_141680b38 < *(float*)(ZoneMap + 0xC0 + type*4)` (Confirmed from the bytes) and ZoneMap::activate
// 0xA0D6A0 writes that slot with a DURATION. It is a KEEP-ALIVE LEASE, not a record of destruction. The only
// zones it can answer 0 for are ones still in the manager's set, still holding their content and still full
// of buildings - a zone whose lease has run out but whose deactivateZone call has not happened yet. The walk
// therefore withheld LIVE zones from the box resolver, and the cost was a legitimate box move refused as
// `keyUnresolved` - the very symptom the effort exists to remove. It also disagreed, inside one commit, with
// the live area read the item layer had just been given: that reads the loaded byte at ZoneMap+0xB3 under the
// zone's own mutex (0xA0D560, Confirmed) and ZoneMap::deactivate clears +0xB1, so the two gates read two
// different fields and could not flip together.
//
// AND THE OBSERVATION IT WAS BUILT ON IS STILL UNEXPLAINED. F544(e) recorded that T230's boxtest resolved a
// building of zone 20.32 at 256 s although that zone's level data had been written at 200.9 s. P6n's comment
// promoted that into "the shape this exists to make visible" - an inference written as an observation. It is
// not evidence for a stale active list, because the decompiles above say the list cannot be stale on the
// ordinary path. Two explanations fit and NEITHER IS EXCLUDED: a [ZONE] save line is not an unload at all
// (ZoneMapContent::saveLevelData 0x36DCA0 has three callers - the unload, SaveManager's explicit save through
// 0x36E310, and the level editor's 0x36EAD0), and GameWorld::destroy 0x798F50 has a branch that QUEUES an
// object instead of destroying it, so a building can outlive the frame its zone was taken apart on. F544(e)
// stands as UNEXPLAINED, which is what it was recorded as.
//
// The engine's own lease is still readable where it belongs: the `buildings` command prints every listed
// zone's three reasons and its deactivation countdown, through ReadZoneReasonPod above.
// piece 3b levers (build/building-owner-read.md 3.2, Read): ZoneMap -> ? (the content is reached from the ZoneMap through the
// engine's ZoneMapContent+0xD0 back-pointer only; the forward link is ZoneMap+0x?? - not read), so the walk goes through the
// active zones' contents via ZoneManager::getAllActiveZones and each ZoneMap's content pointer read from the ZoneMap header:
// ZoneMap.h has `ZoneMapContent* content` - offset taken from the header at build time (kZoneMapContentOffset below).
// things = lektor<RootObject*> at ZoneMapContent+0x50 (count +0x58, array +0x60); getDataType = vtbl+0x20 (0 = BUILDING);
// getOwnerFaction = vtbl+0x58; setFaction(Faction*, ActivePlatoon*) = vtbl+0xA0; getPosition = vtbl+0x40 (Vector3* out).
typedef int    (*GetDataTypeFn)(void* obj);
typedef void*  (*GetFactionFn)(void* obj);
typedef void   (*SetFactionFn)(void* obj, void* faction, void* activePlatoon);
typedef void*  (*GetPositionFn)(void* obj, float* out);
int CopyStdStringPodZ(const char* s, char* buf, int cap)
{
    __try
    {
        const size_t len = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
        const char* p = (res >= 16) ? *(const char* const*)s : s;
        size_t n = len; if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
        for (size_t i = 0; i < n; ++i) buf[i] = p[i];
        buf[n] = 0; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
long long g_leverReadFaults = 0;
void* ReadPtrAtPod(void** arr, unsigned k)
{
    __try { return arr[k]; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadThingsPod(void* content, void*** arr, unsigned* n)
{
    __try { *n = *(unsigned*)((char*)content + 0x58); *arr = *(void***)((char*)content + 0x60); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BuildingInfoPod(void* obj, void** faction, float* pos)
{
    __try
    {
        void** vt = *(void***)obj;
        if (((GetDataTypeFn)vt[4])(obj) != 0) return 0;          // vtbl+0x20: 0 = BUILDING
        *faction = ((GetFactionFn)vt[11])(obj);                   // vtbl+0x58
        float v[3] = { 0, 0, 0 }; ((GetPositionFn)vt[8])(obj, v); pos[0] = v[0]; pos[1] = v[1]; pos[2] = v[2];   // vtbl+0x40
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int SetBuildingFactionPod(void* obj, void* faction)
{
    __try { void** vt = *(void***)obj; ((SetFactionFn)vt[20])(obj, faction, 0); return 1; }   // vtbl+0xA0
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// ZoneMap+0x0 = ZoneMapContent* mapContent (ZoneManager.h) - Read from the header; the content's things list holds every loaded object
bool ReadPosition(::Character* c, float* x, float* y, float* z);   // defined below
void* ContentOfZonePod(void* zoneMap)
{
    __try { return *(void**)zoneMap; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string FactionNamePod(void* f)
{
    if (!PlausiblePod(f)) return "<none>";
    char buf[96]; if (!CopyStdStringPodZ((const char*)f + 0x1A8, buf, 96)) return "<unreadable>";   // Faction::name +0x1A8
    return std::string(buf);
}
// `buildings`: every building in the active zones, counted by owner faction name, with the player's and the peer's named
std::string BuildingsReport(void* zm)
{
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return "<getAllActiveZones faulted>";
    std::map<std::string, int> byOwner; int total = 0, faults = 0;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        void* content = ContentOfZonePod(zones[i]); if (!PlausiblePod(content)) continue;
        void** arr = 0; unsigned n = 0; if (!ReadThingsPod(content, &arr, &n) || !PlausiblePod(arr) || n > 20000) continue;
        for (unsigned k = 0; k < n; ++k)
        {
            void* obj = ReadPtrAtPod(arr, k); if (!PlausiblePod(obj)) { if (obj == 0 && k < n) ++g_leverReadFaults; continue; }
            void* fac = 0; float pos[3];
            const int r = BuildingInfoPod(obj, &fac, pos);
            if (r < 0) { ++faults; continue; }
            if (r == 0) continue;
            ++total; ++byOwner[FactionNamePod(fac)];
        }
    }
    std::string out = "buildings=" + N(total) + " faults=" + N(faults) + " readFaults=" + N(g_leverReadFaults);
    for (std::map<std::string, int>::const_iterator it = byOwner.begin(); it != byOwner.end(); ++it) out += " '" + it->first + "'=" + N(it->second);
    return out;
}
// `ownbuilding`: the building nearest the watched player becomes my player faction's (Building::setFaction vtbl+0xA0 = 0x556EC0)
std::string OwnNearestBuilding(void* zm, ::Faction* mine)
{
    ::Character* pc = GetTarget(); float px = 0, py = 0, pz = 0;
    if (!PlausiblePod(pc) || !ReadPosition(pc, &px, &py, &pz)) return "error ownbuilding: no player";
    if (!PlausiblePod(mine)) return "error ownbuilding: no player faction";
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return "error ownbuilding: zones unreadable";
    void* best = 0; double bestD2 = 0; void* bestFac = 0;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        void* content = ContentOfZonePod(zones[i]); if (!PlausiblePod(content)) continue;
        void** arr = 0; unsigned n = 0; if (!ReadThingsPod(content, &arr, &n) || !PlausiblePod(arr) || n > 20000) continue;
        for (unsigned k = 0; k < n; ++k)
        {
            void* obj = ReadPtrAtPod(arr, k); if (!PlausiblePod(obj)) { if (obj == 0 && k < n) ++g_leverReadFaults; continue; }
            void* fac = 0; float pos[3];
            if (BuildingInfoPod(obj, &fac, pos) != 1) continue;
            const double dx = pos[0] - px, dz = pos[2] - pz, d2 = dx * dx + dz * dz;
            if (best == 0 || d2 < bestD2) { best = obj; bestD2 = d2; bestFac = fac; }
        }
    }
    if (best == 0) return "error ownbuilding: no building in the active zones";
    const std::string before = FactionNamePod(bestFac);
    if (SetBuildingFactionPod(best, mine) != 1) return "error ownbuilding: setFaction faulted";
    void* after = 0; float pos[3]; BuildingInfoPod(best, &after, pos);
    char buf[160]; _snprintf(buf, sizeof buf - 1, "ok ownbuilding %p at %.0f,%.0f dist=%.0f owner '%s' -> '%s'", best, pos[0], pos[2], sqrt(bestD2), before.c_str(), FactionNamePod(after).c_str()); buf[sizeof buf - 1] = 0;
    DebugLog(std::string("[ZONES] ") + buf);
    return std::string(buf);
}
std::string ActiveZoneReasons(void* zm)
{
    // review-p3h H3: lektor never frees what the engine's grow allocated - one reused list, cleared each call (the engine's
    // grow check is capacity <= count, so it allocates once and reuses)
    static lektor<void*> zones;
    zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return "<getAllActiveZones faulted>";
    std::string out;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        void* m = zones[i];
        if (!PlausiblePod(m)) continue;
        int cx = 0, cy = 0; char c = '-', p = '-', t = '-'; float cd = -1.0f;
        if (!ReadZoneReasonPod(m, &cx, &cy, &c, &p, &t, &cd)) continue;
        if (!out.empty()) out += " ";
        if (!(cd > -1e6f && cd < 1e6f)) cd = -1.0f;   // review-p3h H4: a garbage countdown must not overrun the print
        char buf[96]; _snprintf(buf, sizeof buf - 1, "%d,%d[%c%c%c %.0fs]", cx, cy, c, p, t, cd); buf[sizeof buf - 1] = 0; out += buf;
    }
    return out.empty() ? std::string("<none>") : out;
}
void* ZoneManagerPtr()
{
    __try { return *(void**)((uintptr_t)::GetModuleHandleA(0) + kZoneManagerPtrRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int IsLoadedAt(void* zm, float x, float y, float z)   // 1 loaded, 0 not, -1 faulted
{
    __try
    {
        IsZoneLoadedFn fn = (IsZoneLoadedFn)((uintptr_t)::GetModuleHandleA(0) + kIsZoneLoadedTRva);
        return fn(zm, Ogre::Vector3(x, y, z)) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool ReadPosition(::Character* c, float* x, float* y, float* z)
{
    __try { Ogre::Vector3 p = c->worldPosition(); *x = p.x; *y = p.y; *z = p.z; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

long long g_tick = 0;
std::vector<Sector> g_loaded;              // this instance's loaded set, last computed
Sector g_playerSector = { -1, -1 };
long long g_ticks = 0, g_probes = 0, g_probeFaults = 0, g_noZoneManager = 0, g_noPlayer = 0;   /* M2 (decisions 32/44/54): sent / recv, the session-link ZONES counters, are retired with the message - a readout writes ABSENT */
// review-p5j MEDIUM-4: IsPositionLoadedHere answered "not loaded" for a probe that FAULTED, and E15 wired that answer
// into both of its legs - so an unreadable zone manager during a world transition would withdraw every announced
// character and re-announce none, the same unload storm E15 exists to remove. These two say which it was. They are
// kept apart on purpose (a counter that counts two things cannot answer either question - review-p5j LOW-1).
long long g_posLoadedFault = 0, g_posLoadedNoZm = 0;
/* T-306 (owner decision 226, 2026-09-30): "a game keeps control of the areas it holds while its player is connected, dead or
   alive". THE LAST KNOWN CENTRE. The watched character is this tick's centre; ForgetTarget clears the watch when the engine
   DESTROYS that character (T674: the corpse was eaten, log-A.txt:21277), and the tick used to bail out from then on with no
   AREAS report at all - so the notebook released every area this game held after its 10 s lease (log-store 09:00:07
   "AREA 43,11 slot 0 -> 1") and the other game adopted everything standing in it. A dead body that still exists was never the
   problem (it stays the watch and its position still reads). Now a tick with no readable watched character keeps probing
   around the LAST position it read and reports EXACTLY what a live tick would: every area that reads loaded there (fold 1,
   manager ruling on the review of 584c3b71 - a dead or spectating player's game keeps streaming what its camera sees, like a
   live one; worldsync reads this report as "the areas I can see", and the notebook's own lease and hand-over rules decide who
   holds what, exactly as for a live player whose camera moves). It differs from a live tick in two things only: the ring-1
   halo stays cleared, and from the second consecutive orphaned tick (cooporphan::OrphanSendUnknownSector) the player sector
   goes out as unknown (kNoSector) - one unreadable tick while alive must not erase this game's player row at the notebook.
   Main thread only; the world teardown forgets the centre. */
int g_orphanCentreValid = 0; float g_orphanX = 0.0f, g_orphanY = 0.0f, g_orphanZ = 0.0f;
int g_orphanRun = 0;   /* consecutive orphaned ticks in the current spell, this one included; 0 = the tick is live */
long long g_orphanTicks = 0, g_orphanEntered = 0, g_orphanEnded = 0, g_orphanSentUnknown = 0;
int g_areasLastNonEmpty = 0; volatile LONG g_areasLeaveOwed = 0; long long g_areasLeaveSent = 0;   /* M6 fold 1: the last AREAS sent listed sectors / a teardown owes one empty AREAS / how many went (ZonesAreasLeave) */
int g_lastSentAny = 0, g_lastSentX = 0, g_lastSentY = 0;   /* the player sector the last AREAS that reached the wire carried (x < 0 = unknown) */

/* M2: THE NO-NOTEBOOK FEEDS ARE DELETED - the A6 first-loader owner map (OwnerRow / g_map / HostNote and its claims
   counters), the host's copy of the peer's MSG_ZONES set (g_peerLoaded), both games' B9 grid of it (g_peerLoadedGrid)
   and the client's copy of the host's SECTORMAP (g_clientMap). A co-op save never runs without its notebook
   (decision 44), so there was no world for them to serve; every reader answers from the notebook's map below. */
// P4b (F503): the same map as a fixed 64x64 byte grid under a lock, for readers OFF the main thread (the town-generation detour).
CRITICAL_SECTION g_heldLock; bool g_heldLockInit = false; unsigned char g_hostHeld[64][64]; double g_hostHeldAt = 0.0;
/* P97 (p97-zones, owner 334 a / 337 a): F666's bounded no-map wait and its outage stamp are RETIRED - with no
   fresh area map every gate refuses on every game until the map is back. */
// T215: the announce question is not "who HOLDS this area" but "does another game have it LOADED" - a client's own town is owned by the client, so the host never held it and 50 of 51 townspeople were withheld. Same lock, same clock, filled from the relay's loadedMask.
unsigned char g_otherLoaded[64][64]; double g_otherLoadedAt = 0.0;
unsigned g_areaMapApplySeq = 0;   /* M7a3-owed: +1 at every map that stamps g_otherLoadedAt. Under g_heldLock */
/* W1-b (T240; review 2026-09-22 H2/H3): THE EFFECTIVE AREA MAP - per sector, the loadedMask each reporter is TAKEN to hold
   (coopdrop::AreaEffectiveMask). A reporter listed in the latest AREAMAP is taken exactly; one ABSENT from it keeps its last
   known bits here, so a notebook restart whose first map knows only one game never reads the other as holding nothing.
   g_otherLoaded above is derived from THIS grid, not from the raw map. Under g_heldLock. Zeroed at HeldLockInit, at a world
   teardown that drops the relay grid (ZonesForgetHeldGrid(0)), and on the SESSION peer-gone path (ZonesForgetEffectiveMask) -
   never at a notebook link-down: the peer did not go anywhere when the notebook did (M2: the relay-absent fills it was also
   kept from are deleted). During a notebook outage this map is CARRIED UNCHANGED - see the freeze rule at OtherHasSector. */
unsigned g_effMask[64][64][coopdrop::kAreaMaskWords];       /* M9 (T-197; world-server protocol 65): SEAT-indexed, 256 wide (8 words a cell, 128 KB) */
unsigned g_areaNewMask[64][64][coopdrop::kAreaMaskWords];   /* ApplyRelayAreaMap's scratch: this map's loadedMask per sector, 0 = omitted. Under g_heldLock */
/* M9 (T-197) + M9f1 (M9 review M1, LOW "apply core"): g_areaBook = the seat book (the slot each seat COLUMN of g_effMask
   belongs to), the armed seats (W1-c) and the pending slots (named gone before any map gave them a column; settled at the next
   non-empty map, 15 s at most) - coopdrop::AreaBook, applied by coopdrop::AreaApplyMapCore. All under g_heldLock. */
coopdrop::AreaBook g_areaBook;
struct AreaBookBoot { AreaBookBoot() { coopdrop::AreaBookInit(&g_areaBook); } } g_areaBookBoot;
unsigned char g_areaCellFlags[64 * 64];   /* M9f1: AreaApplyMapCore's per-cell answer (kAreaCell*), cell = x * 64 + y. Under g_heldLock */
long long g_areaMapPendingArmed = 0, g_areaMapPendingDropped = 0, g_areaMapPendingExpired = 0;   /* M9f1: areaMapOdd[...,pendingArmed,pendingDropped,pendingExpired]. Under g_heldLock */
long long g_areaMapRows = 0, g_areaMapSeats = 0, g_areaMapRekeyed = 0, g_areaMapBytes = 0, g_areaMapRekeyDropped = 0, g_areaMapMalformed = 0;   /* M9: areaMap[rows,seats,rekeyed,bytes] (latest map, rekeyed cumulative) and areaMapOdd[rekeyDropped,malformed] */
long long g_announceAbsentReporter = 0;  /* maps in which a slot other than mine held something in the effective map and was absent from the map. Under g_heldLock */
/* M9f1: the W1-c arms (seat-indexed words, moved by the rekey with their columns) live in g_areaBook.dropOnAbsence */
long long g_effDroppedAfterPeerGone = 0; /* W1-c: maps in which an armed slot was absent and its bits were dropped. Under g_heldLock */
int g_effCarriedCells = 0;               /* latest map: sectors whose effective mask differs from the map's own mask (an absent reporter's bits carried). Under g_heldLock */
// decision 35: the third grid - the areas THIS game holds (owner == my slot). "No other player holds it" and "I hold it" are different facts: an area nobody holds may still be LOADED by the other game, and a game must not invent people into an area the other game is already showing. Same lock, same clock as g_hostHeld.
unsigned char g_mineHeld[64][64];
// The OWNER SLOT the relay's map named for each area. P025 (F527/E13) kept it beside the three booleans above so a refusal could
// report the input it was decided from; M7b slice 2 (T-197) DECIDES on it (AreaHolderSlotTS: an item message goes to the area's
// holder), so it is no longer inside the P025 markers. An int, not a signed char: a slot above 126 read as nobody (owner S2-67).
// -2 = the map carried no row for this area at all, -1 = a row that names nobody, >= 0 = the slot. Same lock, same clock
// (g_hostHeldAt) as g_hostHeld/g_mineHeld.
int g_ownerSlot[64][64];
static void OwnerSlotClear() { for (int x = 0; x < 64; ++x) for (int y = 0; y < 64; ++y) g_ownerSlot[x][y] = -2; }
// decision 37 AMENDED (review-p5g HIGH-2): the FOURTH grid - the areas THIS GAME has loaded, as a lockable copy of g_loaded.
// The three grids above all come from the relay's map, and an area the relay's map does not mention at all reads held=0 and
// mine=0 on both games: MayInventHereTS then answered 0 to everyone, so every town outside both games' loaded rings was refused
// by both games permanently, and a town's residents - generated ONCE per zone load, in the second before the relay names the
// arriving game the holder - were lost for that load. This grid is the first-there presumption: an unclaimed area I have
// LOADED is mine to invent in. It is filled on the main thread at the zone tick (from the same `loaded` vector the relay is
// told about) and read on any thread through AreaViewTS, so it needs the same lock as the other three. It carries NO clock:
// it is this game's own state, not a remote report, so there is nothing to go stale.
unsigned char g_loadedHere[64][64];
/* B10-b (review-b10 M-6): THE HEIGHT THE LAST PROBE PASS USED. ZonesTick asks the engine "is this point
   loaded" at each sector's centre using the PLAYER'S OWN Y - a world height, not zero - and a road that has
   only a sector must ask the same question at the same height or it is asking a different one. Written under
   g_heldLock beside the grids, in the same pass that produced them. */
float g_playerYTS = 0.0f;
// E13 attempt 2 / decision 37 RING-1 PRESUMPTION (2026-09-04). H047 (F529, T224): the zone-loaded flag and everything downstream of it stay down for ~10-12 s after a teleport while the engine populates the town; the player's own ring-1 sectors are being loaded by this game and nobody else.
// g_playerSector above is written by the main thread and read by the main thread; this is the SAME value under
// g_heldLock, written at the one place g_playerSector changes and at the world teardown that clears it, so a worker
// thread inside a creation gate can ask "is this area within ring 1 of my own player" from the same locked read that
// answers held/mine/loadedHere. (-1,-1 = unset, exactly as SectorInMyRing reads g_playerSector.) It carries no clock:
// like g_loadedHere it is this game's own state, not a remote report, so there is nothing to go stale.
Sector g_playerSectorTS = { -1, -1 };
// E25 / review-p5p HIGH-1 (2026-09-04) - THE OTHER PLAYER'S SECTOR, the same field for the other side of the world.
// The ring-1 presumption as P5p built it is UNILATERAL: it says "an area next to my player is being loaded by this
// game and nobody else", which is false the moment the other player is standing two sectors away - and that is
// precisely the arrangement the presumption exists to serve, because two players arriving at one town is what T226
// is about. Both games then presume, both invent, and the town's crowd is duplicated: the very defect decision 37
// was written to remove, reintroduced by the exception to it.
//
// This is filled from the copies of the peer's characters this game already holds (replicate.cpp
// PeerPlayerPosition), on the main thread at the zone tick, under the same lock and out of the same tick as
// g_playerSectorTS - so the two halves of the tie-break cannot come from different moments. (-1,-1 = unset, and it
// answers 0, exactly as an unset player sector does.)
Sector g_peerSectorTS = { -1, -1 };
// E25 RE-DESIGNED (verify-p5t HIGH-1, decision 37 amended 2026-09-04) - THE OTHER PLAYER'S SECTOR FROM THE NOTEBOOK
// PROCESS, which is the only source that still answers inside the window the tie-break exists for.
// g_peerSectorTS above comes from PeerPlayerPosition, i.e. from the copies of the peer's characters this game holds
// - and those copies exist only while the peer's announce pass is announcing them. review-p5t HIGH-1 traced the
// chain: in the H047 window the announcing game's own IsPositionLoadedHere reads 0 for ~10-12 s after a teleport
// (worldsync.cpp:443,446), so it sends an UNLOAD, the puppet row is erased (spawn.cpp DropPuppet) and
// PeerPlayerPosition answers false. The tie-break therefore read "the other player is not beside this area" at
// precisely the moment two players were converging on one town, which is the ONE case it was written for.
// The relay hears each game's AREAS message directly and republishes every game's player sector once a second, so
// this table answers whether or not either game's zones are loaded. Slots 0..15 (the relay assigns from 0 and a
// session is two games; a higher slot is dropped and shows as recv > valid, never as a silent zero).
// The table is REPLACED wholesale by each message (ApplyRelayPlayerSectors below), exactly as the AREAMAP replaces
// the grids: the relay sends every current sector every second, so a slot missing from a message is a slot that has
// gone quiet and must stop answering here too.
struct PeerSectorRow { int slot, x, y, valid; double at; };   /* area2 fold: at = when the notebook last heard the row (NowSec), 0 = unknown. M9 (T-197): slot = the player's slot - a row per player in message order; the table is KEYED by slot (any of 0..65519), no longer indexed by it */
const int kPeerSectorRows = 256;   /* M9: the world server links at most 256 games at once */
PeerSectorRow g_peerSectorRelay[kPeerSectorRows];
/* recruit3: every slot the relay's player-sector table has ever named in this process (sticky; bit = slot) - `recruitmult auto` counts it */
std::vector<int> g_slotsSeen;   /* M9 (T-197): the distinct slots (sorted), any number - the 16-bit mask keyed by slot could not hold slot 40000. Under g_heldLock */
double g_peerSectorRelayAt = 0.0;   /* the table's OWN clock - review-p5b HIGH-1's rule: the guard reads the clock the answer comes from */
/* P6q (review-p6j MEDIUM-1): and the LINK GENERATION it was applied under, under the same lock. A clock says how
   old a table is; this says whose numbering its slot column belongs to, which is the thing the row-skip needs and
   the thing a clock cannot express. -1 = no table has been applied in this process. */
int g_peerSectorRelayGen = -1;
const double kPeerSectorRelayFreshSec = 5.0;
// E25: peerSect[recv,valid,ring1Relay,ring1Puppet]. `recv` is entries the relay sent (per entry, not per message),
// `valid` is entries actually stored - so a slot above 15 or an out-of-range sector is visible as the difference,
// never as a zero that reads like "the relay said nothing". `ring1Relay` / `ring1Puppet` are the two sources of a
// peerRing1 == 1 answer, counted apart because the whole point of this change is which one answered.
volatile LONG64 g_peerSectRecv = 0, g_peerSectValid = 0, g_peerSectRing1Relay = 0, g_peerSectRing1Puppet = 0;
// P6c (p5z MEDIUM-3) - THE BOTH-PRESUME RESIDUAL, SIZED. The design states it: for the seconds between this game
// linking to the relay and the OTHER game reporting its first sector, the table is FRESH and EMPTY, so both games
// answer "nobody beside me" and both may presume. `peerSect[valid]` cannot size that - it says whether the relay
// ever carried a peer sector cumulatively - and `ring1Relay` counts the presumptions that YIELDED. This counts the
// other side of the same test: a view in which the table was fresh, named nobody beside this sector, and this
// game's own player IS beside it, which is the input that grants the ring-1 presumption.
// P6q (review-p6j MEDIUM-2) - THIS PARAGRAPH USED TO END BY SAYING THE OPPOSITE OF WHAT THE NUMBER NOW MEANS, and
// that is the version a reader would have found first. P6j moved the increment OUT of AreaViewTS and into
// MayInventFromView's ring-1 grant - which was the right move - and left this comment saying "read it as how often
// the empty-table presumption was ON OFFER ... and not as how many towns were invented on it". It is now exactly
// the second of those: a count of grants, taken at the grant, after every answer that could have short-circuited
// above it (another player holds the area, the relay names me holder, I have it loaded) has already returned, and
// with a yield to a lower slot excluded. The report field is renamed to `presumedEmptyGranted` in the same change,
// because a P6h log and a P6j log printed the same name for two different quantities and nothing said so.
volatile LONG64 g_peerSectPresumedEmpty = 0;
// P6q (review-p6j MEDIUM-1) - PLAYERSECTORS MESSAGES DROPPED BECAUSE THIS LINK'S WELCOME HAD NOT ARRIVED YET.
// The relay broadcasts the table to every CONNECTED peer with no welcome test, and the plugin used to apply it
// with none either - so for the window between the ENet link coming up and the WELCOME being drained, a game with
// no slot of its own was reading a table in which one of the rows was its own echo. P6j's fallback (skip the row
// of the slot this game was LAST given) is reachable only in that same window, and it is exactly the window in
// which the last-known slot names the PREVIOUS link's numbering: the relay assigns slots by first-free index in
// HELLO order, so after a restart or a reconnect in the other order that index is the OTHER player, and the skip
// removes a real peer. The table is simply not applied until the WELCOME of the current link generation has been
// processed. A non-zero here is the window being real, not an error.
volatile LONG64 g_peerSectPreWelcomeIgnored = 0;
// P6c (p5z HIGH-2) - AREAS SENT WITH AN EMPTY LOADED SET. The player sector is a trailing field of AREAS, and AREAS
// used to be sent only when this game had at least one sector loaded. The 10-12 s after a teleport is precisely when
// the engine's own zone-loaded flag reads 0 (H047/F529), so the set is empty and the one message that reports where
// the player is standing was not sent - during the exact window the channel exists for. The guard is gone; this
// counts the sends it used to drop, so "the notebook knew where each player was throughout the arrival window" is a
// number rather than a hope.
long long g_areasSentEmpty = 0;
/* P8a (build/read-roster-t236.md 4b): THE COUNTER THAT DID NOT EXIST.  T236a could not tell whether the host
   ever reported its areas to the notebook, because `[ZONES] REPORT`'s sent= is g_sent - the SESSION-link
   ZONES message, which only a client sends - and areasSentEmpty only moves when the loaded set is EMPTY.
   The relay logs a line only when an award CHANGES, so it says nothing about a message it received and
   ignored.  This is StoreSendAreas's own return value (M2: one arm now, and g_sent / SendZones are retired). */
long long g_areasSent = 0;
// P6c (p5z MEDIUM-2) - INVENTIONS REFUSED BECAUSE THE ENGINE IS TEARING THE WORLD DOWN. E33 keeps the sector grid
// alive across the engine's own teardown so the sleep hook can still ask who holds an area; g_loadedHere has no
// clock and ZonesTick's bail-outs do not clear it, so during that window it keeps answering 1 for the last live
// world's sectors and MayInventFromView returned 1 where it used to return -1 (which a declared client refuses).
// This is that widening closed, and the count is what says whether it was ever reached.
volatile LONG64 g_inventRefusedTeardown = 0;
volatile LONG64 g_yieldedStale = 0;   /* area2 fold: loaded-area yields REFUSED because the lower slot's row was older than 2 s */
const double kPeerLowRowFreshSec = 2.0;   /* area2 fold (review-area2 MED): the loaded-area yield trusts a row this fresh */
// How old the peer's last streamed sample may be and still count. It matches the relay's own ownership grace
// (store_main.cpp kGraceSec): after that long without hearing from a game the relay stops holding its areas, and a
// halo that outlived the relay's own patience is a frozen inference of exactly the kind E25.7 removes below.
const double kPeerSectorMaxAgeSec = 10.0;
// review-p4b HIGH-1: initialised ONCE at install on the main thread (ZonesInitLocks); the lazy form raced from worker threads and its memset could zero a filled grid
void HeldLockInit() { if (!g_heldLockInit) { ::InitializeCriticalSection(&g_heldLock); g_heldLockInit = true; std::memset(g_hostHeld, 0, sizeof(g_hostHeld)); std::memset(g_otherLoaded, 0, sizeof(g_otherLoaded)); std::memset(g_effMask, 0, sizeof(g_effMask)); /* W1-b */ std::memset(g_mineHeld, 0, sizeof(g_mineHeld)); std::memset(g_loadedHere, 0, sizeof(g_loadedHere)); std::memset(g_peerSectorRelay, 0, sizeof(g_peerSectorRelay)); g_peerSectorRelayAt = 0.0; g_peerSectorRelayGen = -1; /* E25: the relay's player-sector table starts empty and unstamped, so it answers "no fresh table" until one lands */   /* P6q: and belongs to no link */ OwnerSlotClear(); /* M7b slice 2: -2 = no row (an int grid - no longer a memset) */ g_playerSectorTS.x = -1; g_playerSectorTS.y = -1; g_peerSectorTS.x = -1; g_peerSectorTS.y = -1; /* E13 attempt 2: unset, not 0,0 - sector 0,0 is a real sector. E25: the peer's halo starts unset too */ } }
// E25.7 / review-p5p MEDIUM-2 - THE HALO EXPIRES WITH THE TICK THAT DRAWS IT. Every other grid in the view either
// carries a 5 s clock and answers -1 when it goes stale, or is rewritten wholesale by each completed tick.
// g_playerSectorTS is written ONLY by a completed tick, and the tick bails out for good the moment the watched
// character is destroyed (ai_spike.cpp ForgetTarget) - so the halo kept naming wherever that character last stood
// until the world was torn down, while the relay released this game's areas after ten seconds of silence. A frozen
// OBSERVATION (g_loadedHere) is a record of something that was true; a frozen INFERENCE is a claim about sectors
// nothing ever confirmed, and only the second one can be wrong in a direction that invents people.
// T-306 (owner decision 226) CORRECTION: the tick no longer bails out for good when the watched character is destroyed - it
// carries on ORPHANED around the last known position (see g_orphanCentreValid) and keeps this halo CLEARED on every orphaned
// tick (ZonesTick calls ForgetPlayerHaloTS and re-clears after the grids), so the frozen-halo hazard above stays closed, and
// the areas are no longer released for silence.
void ForgetPlayerHaloTS()
{
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    g_playerSectorTS.x = -1; g_playerSectorTS.y = -1;
    g_peerSectorTS.x = -1; g_peerSectorTS.y = -1;
    /* E25: the relay's table is NOT cleared here. ForgetPlayerHaloTS runs when THIS game's zone tick cannot answer
       where its own player is, which says nothing at all about where the other player is - and the relay's answer
       has its own clock and expires on it. Clearing it here would let one game's bail-out grant the presumption to
       both games at once, which is the defect this whole mechanism removes. */
    ::LeaveCriticalSection(&g_heldLock);
}

/* T-274 (t274-zone-scan.md S1): ONE zone's walk - the per-zone loop LoadedBuildings and LoadedBuildingsInSector share (one copy of
   it). Every thing of the zone's content that passes BuildingInfoPod is appended to out[written..cap); returns the new count. The
   flags are LoadedBuildings' (see its notes): a content pointer that will not read is skipped UNFLAGGED (the deactivateAll case,
   P7g L-4), an object array that will not read (or claims more than 20,000 things) raises `truncated` (P7a), and so does a walk
   that stopped with things unexamined (P7g M-2 - it counts BUILDINGS LOST, not zones that are merely large; the index is hoisted
   for that test, C89 declaration). */
int ZoneBuildingsOfPod(void* zone, void** out, int cap, int written, int* truncated)
{
    void* content = ContentOfZonePod(zone); if (!PlausiblePod(content)) return written;
    void** arr = 0; unsigned n = 0;
    if (!ReadThingsPod(content, &arr, &n) || !PlausiblePod(arr) || n > 20000)
    { if (truncated != 0) *truncated = 1; return written; }   /* P7a: a whole zone skipped is a truncation */
    unsigned k = 0;
    for (; k < n && written < cap; ++k)
    {
        void* obj = ReadPtrAtPod(arr, k); if (!PlausiblePod(obj)) continue;
        void* fac = 0; float pos[3];
        if (BuildingInfoPod(obj, &fac, pos) != 1) continue;
        out[written++] = obj;
    }
    if (truncated != 0 && k < n) *truncated = 1;   /* the output cap bit: things in this zone unexamined */
    return written;
}

} // namespace

std::string BuildingsCommand() { void* zm = ZoneManagerPtr(); if (!PlausiblePod(zm)) return "error buildings: no zone manager"; const std::string r = BuildingsReport(zm); DebugLog("[ZONES] " + r); return "ok " + r; }
/* P18 fold 1 (`buyhouse nearest`): the building nearest the watched player that `want` accepts (answers 1) - OwnNearestBuilding's walk */
void* NearestBuildingWhere(int (*want)(void*), double* dist, std::string* err, unsigned* seen)
{
    if (seen != 0) *seen = 0;   /* every building the walk examined, accepted or not - a refusal can say how many were looked at */
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) { *err = "no zone manager"; return 0; }
    ::Character* pc = GetTarget(); float px = 0, py = 0, pz = 0;
    if (!PlausiblePod(pc) || !ReadPosition(pc, &px, &py, &pz)) { *err = "no player"; return 0; }
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) { *err = "zones unreadable"; return 0; }
    void* best = 0; double bestD2 = 0;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        void* content = ContentOfZonePod(zones[i]); if (!PlausiblePod(content)) continue;
        void** arr = 0; unsigned n = 0; if (!ReadThingsPod(content, &arr, &n) || !PlausiblePod(arr) || n > 20000) continue;
        for (unsigned k = 0; k < n; ++k)
        {
            void* obj = ReadPtrAtPod(arr, k); if (!PlausiblePod(obj)) continue;
            void* fac = 0; float pos[3];
            if (BuildingInfoPod(obj, &fac, pos) != 1) continue;
            if (seen != 0) ++*seen;
            if (want != 0 && want(obj) != 1) continue;
            const double dx = pos[0] - px, dz = pos[2] - pz, d2 = dx * dx + dz * dz;
            if (best == 0 || d2 < bestD2) { best = obj; bestD2 = d2; }
        }
    }
    if (best == 0) { *err = "no matching building in the active zones"; return 0; }
    *dist = sqrt(bestD2);
    return best;
}
std::string OwnBuildingCommand() { void* zm = ZoneManagerPtr(); if (!PlausiblePod(zm)) return "error ownbuilding: no zone manager"; return OwnNearestBuilding(zm, LocalPlayerFaction()); }

// E22c (P6e). See zones.h. `BuildingInfoPod` IS the "is this thing a building" test (it answers 0 for
// anything whose getDataType is not BUILDING) and it is called here rather than re-implemented, for the
// reason lesson 11 gives: a predicate kept in step by hand is a predicate that drifts. The 64-zone and
// 20,000-object bounds are the ones the two existing levers already refuse beyond.
/* P7a (review-p6t MEDIUM-3). THE THREE SILENT TRUNCATIONS NOW HAVE A SIGNAL. This function gives up in
   three places and used to tell the caller nothing about any of them: more than 64 active zones, a zone whose
   object array will not read (or claims more than 20,000 things), and the `cap` on what it writes out. Under
   any of them a LIVE building is missing from the list - and items.cpp's box resolver uses membership in this
   list as its LIVENESS TEST, so a missing live building makes it refuse a cache row that was correct and book
   `boxCacheNotListed`, a counter whose own comment says every one of those is a move into a building the
   engine no longer had. That sentence is the whole justification for review-p6k HIGH-1, so it must be
   readable, and it is only true while `*truncated` stays 0. One out-param in the callee rather than a test
   repeated at each of the three call sites (lesson 11).
   P7g (review-p7a M-2): THE `cap` TERM USED TO FIRE ON THE ZONE'S RAW THING COUNT, so it could raise the flag
   on a walk that dropped nothing. It now fires only when the inner loop actually stopped with things still
   unexamined - it counts BUILDINGS LOST, not zones that are merely large.
   P7g (review-p7a L-4): "THREE PLACES" IS NOT EXHAUSTIVE AND NEVER WAS. There is a FOURTH skip that raises
   nothing - a zone whose content pointer will not read (`PlausiblePod(content)` below). It is left unflagged
   deliberately: that is overwhelmingly the `deactivateAll` case, a zone that is going, so what is missing
   from the list is not a live building. Named here so the count of three is not read as complete. */
int LoadedBuildings(void** out, int cap, int* zonesSeen, int* truncated)
{
    if (zonesSeen != 0) *zonesSeen = 0;
    if (truncated != 0) *truncated = 0;
    if (out == 0 || cap <= 0) return 0;
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) return 0;
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return 0;
    if (truncated != 0 && zones.size() > 64) *truncated = 1;   /* zones past the 64th are never walked */
    int written = 0;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        if (zonesSeen != 0) ++(*zonesSeen);
        /* P6w (review-p6n HIGH-1): NO ACTIVITY TEST HERE. Being on this list is the test - deactivateZone
           erases an unloaded zone from the set getAllActiveZones reads. The null content pointer below is
           what catches deactivateAll's entries, as it did before P6n. See the note above ContentOfZonePod. */
        written = ZoneBuildingsOfPod(zones[i], out, cap, written, truncated);   /* T-274: the per-zone walk, one copy (P7a / P7g flags inside) */
    }
    return written;
}

/* par1 (docs/design-loot2.md rev 2, 0.3-0.4): the SECTOR of every active zone - the same list LoadedBuildings walks, the same
   content-pointer skip - read without touching a single object in it. items.cpp's ItParityTick compares it drain to drain: a
   sector that joins it is a ZONE ARRIVAL (the furniture registry is rebuilt and the box-parity request is armed), one that
   leaves it is forgotten. ZoneMap+0x18/+0x1C are the sector (the reads ReadZoneReasonPod and P085 make). MAIN THREAD. */
namespace {
int ZoneSectorPod(void* m, int* cx, int* cy)
{
    __try
    {
        *cx = *(const int*)((const char*)m + 0x18);
        *cy = *(const int*)((const char*)m + 0x1C);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}   /* namespace (par1) */
int ActiveZoneSectors(int* sx, int* sy, int cap)
{
    if (sx == 0 || sy == 0 || cap <= 0) return -1;
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) return -1;
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return -1;
    int n = 0;
    for (unsigned i = 0; i < zones.size() && n < cap; ++i)
    {
        if (!PlausiblePod(zones[i])) continue;
        void* content = ContentOfZonePod(zones[i]); if (!PlausiblePod(content)) continue;
        if (ZoneSectorPod(zones[i], sx + n, sy + n) == 0) continue;
        ++n;
    }
    return n;
}
/* T-274 (t274-zone-scan.md S1): LoadedBuildings restricted to the ONE active zone whose sector (ZoneMap+0x18/+0x1C) is (sx, sy) -
   the same zone list (a zone off it is not live), the same per-zone walk (ZoneBuildingsOfPod) and the same `truncated` flag. One
   ZoneMap per sector (ZoneManager + 0xC8 + (sx * 64 + sy) * 0x168), so the first match is the zone. -1 = the zone list could not be
   read; otherwise the count written (0 = no active zone has that sector). MAIN THREAD. */
int LoadedBuildingsInSector(int sx, int sy, void** out, int cap, int* truncated)
{
    if (truncated != 0) *truncated = 0;
    if (out == 0 || cap <= 0) return -1;
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) return -1;
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return -1;
    int written = 0;
    for (unsigned i = 0; i < zones.size(); ++i)
    {
        if (!PlausiblePod(zones[i])) continue;
        int cx = -1, cy = -1;
        if (ZoneSectorPod(zones[i], &cx, &cy) == 0 || cx != sx || cy != sy) continue;
        written = ZoneBuildingsOfPod(zones[i], out, cap, written, truncated);
        break;
    }
    return written;
}

// PROBE-START: P085
/* PROBE P085 (loot2a4): per active zone, its sector (ZoneMap+0x18/+0x1C, as ReadZoneReasonPod), ZoneMapContent+0xA8 (the
   fresh byte, answers-loot2b R3), ZoneMap+0x24 (hasFile, answers-loot2c Q4) and ZoneMap+0xA8 (build.cpp's read). Read-only,
   every read guarded. MAIN THREAD (the zone list is the one LoadedBuildings takes). */
namespace {
int P085ZoneReadPod(void* m, int* cx, int* cy, int* fresh, int* hasFile, int* mapA8)
{
    __try
    {
        *cx = *(const int*)((const char*)m + 0x18); *cy = *(const int*)((const char*)m + 0x1C);
        *hasFile = (int)*(const unsigned char*)((const char*)m + 0x24);
        *mapA8 = (int)*(const unsigned char*)((const char*)m + 0xA8);
        const void* c = *(void* const*)m;
        *fresh = PlausiblePod(c) ? (int)*(const unsigned char*)((const char*)c + 0xA8) : -1;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}   /* namespace (P085) */
int LoadedZoneFreshness(int* sx, int* sy, int* fresh, int* hasFile, int* mapA8, int cap)
{
    if (sx == 0 || sy == 0 || fresh == 0 || hasFile == 0 || mapA8 == 0 || cap <= 0) return 0;
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) return 0;
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return 0;
    int n = 0;
    for (unsigned i = 0; i < zones.size() && n < cap; ++i)
    {
        if (!PlausiblePod(zones[i])) continue;
        if (P085ZoneReadPod(zones[i], sx + n, sy + n, fresh + n, hasFile + n, mapA8 + n) == 0) continue;
        ++n;
    }
    return n;
}
// PROBE-END: P085

/* inv5 (phase 0 probe P090, kept by phase 1: the ground finder and groundtest walk it). The zone list and the content-pointer skip are LoadedBuildings' own; per zone the ground set
   is ZoneMapContent::items (header +0x110, the set saveItems 0x36B710 walks), each hand resolved by hand::getItem (the
   engine's own lookup; a stale hand answers null and is skipped). Read-only. MAIN THREAD. */
/* The walk of one zone ground set under a fault guard: the resource thread can join items into it while we read (0x761BA0 ->
   addItem 0x9FD150 takes no lock). Plain locals only (C2712). Returns the new written count, or -1 on a fault. */
static int GroundSetWalkPod(const void* content, void** out, int cap, int written, int* truncated)
{
    __try
    {
        const GameHashSet<hand>::type& set = *(const GameHashSet<hand>::type*)((const char*)content + 0x110);
        if (set.size() > 20000) { if (truncated != 0) *truncated = 1; return written; }
        for (GameHashSet<hand>::type::const_iterator it = set.begin(); it != set.end(); ++it)
        {
            if (written >= cap) { if (truncated != 0) *truncated = 1; break; }
            void* item = (void*)it->getItem();
            if (PlausiblePod(item)) out[written++] = item;
        }
        return written;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int LoadedGroundItems(void** out, int cap, int* zonesSeen, int* truncated)
{
    if (zonesSeen != 0) *zonesSeen = 0;
    if (truncated != 0) *truncated = 0;
    if (out == 0 || cap <= 0) return 0;
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) return 0;
    static lektor<void*> zones; zones.clear();
    if (!FillActiveZonesPod(zm, &zones)) return 0;
    if (truncated != 0 && zones.size() > 64) *truncated = 1;
    int written = 0;
    for (unsigned i = 0; i < zones.size() && i < 64; ++i)
    {
        if (zonesSeen != 0) ++(*zonesSeen);
        void* content = ContentOfZonePod(zones[i]); if (!PlausiblePod(content)) continue;
        const int w = GroundSetWalkPod(content, out, cap, written, truncated);   /* review-inv5p0 M: guarded (the set may grow off-thread) */
        if (w < 0) { if (truncated != 0) *truncated = 1; continue; }
        written = w;
    }
    return written;
}

Sector SectorOf(float x, float z)
{
    Sector s;
    s.x = (int)floorf((kSectorOrigin + x) / kSectorSize);
    s.y = (int)floorf((kSectorOrigin + z) / kSectorSize);
    if (s.x < 0) s.x = 0; if (s.x > 63) s.x = 63; if (s.y < 0) s.y = 0; if (s.y > 63) s.y = 63;
    return s;
}
std::string SectorString(const Sector& s) { return N(s.x) + "," + N(s.y); }

// E25: what goes on the wire for "where my player is". kNoSector is INT_MIN written without dragging <climits> in;
// the relay stores nothing for it, so an unknown sector expires the previous answer rather than replacing it with
// sector 0,0 - which is a real sector. This reads g_playerSector, the SAME value the tick copies into
// g_playerSectorTS three statements after it is written, so the two halves of the tie-break agree by construction.
const int kNoSector = (-2147483647 - 1);
static int MySectorForRelayX() { return (g_playerSector.x >= 0 && g_playerSector.y >= 0) ? g_playerSector.x : kNoSector; }
static int MySectorForRelayY() { return (g_playerSector.x >= 0 && g_playerSector.y >= 0) ? g_playerSector.y : kNoSector; }
/* T-306 fold 1: a player sector as it went on the wire - "unknown" for kNoSector (any negative), else "x,y". */
static std::string SentSectorString(int x, int y) { return (x < 0 || y < 0) ? std::string("unknown") : (N(x) + "," + N(y)); }
/* M6 fold 1 (review of 49bac485, MED 2) - LEAVING A WORLD EMPTIES THIS GAME'S AREA SETS AT ONCE. A game with no world sends no
   AREAS (the two bail-outs in ZonesTick), so the world server kept its last delivery set for up to its 10 s grace and AREA
   traffic kept reaching a game with no world (owner decision 57). ONE AREAS with count 0 and no player sector goes out on the
   world teardown (store.cpp TeardownBroadcastLateLog, the normal path; if an unwind skipped it, the flag ZonesForgetHeldGrid
   raised makes the next tick send it first) and when the tick bails out for no zone manager or no player - only if the last
   AREAS this game sent listed sectors, so it goes ONCE, never every tick. The world server's answer: empty sets and cleared
   catch-up stamps (whatever this game loads next is caught up in full). MAIN THREAD. */
void ZonesAreasLeave(const char* why)
{
    if (g_areasLastNonEmpty == 0) { g_areasLeaveOwed = 0; return; }
    if (!StoreSendAreas(0, 0, kNoSector, kNoSector)) return;   /* no notebook link: its PeerGone already dropped the sets; tried again on the next bail-out */
    g_areasLastNonEmpty = 0; g_areasLeaveOwed = 0; ++g_areasLeaveSent;
    DebugLog(std::string("[ZONES] M6 fold 1: one empty AREAS sent (") + (why != 0 ? why : "?") + ") - the world server stops sending this game AREA traffic now (areasLeaveSent "
             + N(g_areasLeaveSent) + ")");
}
void ZonesTick()
{
    ++g_tick;
    static double lastReport = 0.0; const double nowR = NowSec();
    if (nowR - lastReport < 1.0) return;   /* P4s: once a second by the clock */
    void* zm = ZoneManagerPtr();
    /* E25.7 (review-p5p MEDIUM-2): a tick that cannot answer "where is my player" must not leave the last answer
       standing as though it still held. Both bail-outs clear the halo, so `ring1` reads 0 rather than granting nine
       sectors around a position nobody is at any more. */
    if (!PlausiblePod(zm)) { ++g_noZoneManager; ForgetPlayerHaloTS(); ZonesAreasLeave("no zone manager"); return; }   /* M6 fold 1: once, if the last report listed sectors */
    ::Character* pc = GetTarget();
    float px = 0, py = 0, pz = 0;
    /* T-306: no readable watched character is not "gone" - with a last known centre the tick carries on ORPHANED (see the
       note at g_orphanCentreValid); without one (nothing watched yet in this world) it bails out as before. */
    int orphan = 0;
    if (!PlausiblePod(pc) || !ReadPosition(pc, &px, &py, &pz))
    {
        ForgetPlayerHaloTS();
        if (g_orphanCentreValid == 0) { ++g_noPlayer; ZonesAreasLeave("no player"); return; }   /* M6 fold 1: once, if the last report listed sectors */
        orphan = 1; px = g_orphanX; py = g_orphanY; pz = g_orphanZ;
    }
    else
    {
        g_orphanCentreValid = 1; g_orphanX = px; g_orphanY = py; g_orphanZ = pz;
        if (g_orphanRun > 0)
        {
            ++g_orphanEnded;
            DebugLog("[ZONES] T-306 ORPHAN ends: a player character is watched again after " + N((long long)g_orphanRun)
                     + " orphaned tick(s) - the live report and the live player sector resume");
            g_orphanRun = 0;
        }
    }
    lastReport = nowR;   /* review-p4y MEDIUM-2: the second is spent HERE, below the last bail-out - a tick that reported nothing must not consume it */
    ++g_ticks;   /* review-p5a MEDIUM-3: the clock is spent here, so the count belongs here too - g_ticks is REPORTS again, not frames spent inside a bail-out window (the bail-out counters below stay frames, which is what they are) */
    g_playerSector = SectorOf(px, pz);
    std::vector<Sector> loaded;
    for (int dy = -kRing; dy <= kRing; ++dy)
        for (int dx = -kRing; dx <= kRing; ++dx)
        {
            Sector s; s.x = g_playerSector.x + dx; s.y = g_playerSector.y + dy;
            if (s.x < 0 || s.x > 63 || s.y < 0 || s.y > 63) continue;
            // the sector's centre in world units
            const float cx = (s.x + 0.5f) * kSectorSize - kSectorOrigin;
            const float cz = (s.y + 0.5f) * kSectorSize - kSectorOrigin;
            ++g_probes;
            const int r = IsLoadedAt(zm, cx, py, cz);
            if (r < 0) { ++g_probeFaults; continue; }
            if (r == 1) loaded.push_back(s);
        }
    /* T-306 fold 1: WHAT GOES ON THE WIRE is the loaded set, live or orphaned - the same areas a live tick reports. Orphaned,
       only the player sector differs, and only from the second consecutive orphaned tick (cooporphan::OrphanSendUnknownSector). */
    if (orphan != 0)
    {
        ++g_orphanRun; ++g_orphanTicks;
        if (g_orphanRun == 1)
        {
            ++g_orphanEntered;
            DebugLog("[ZONES] T-306 ORPHAN: no live watched character - this game keeps reporting every area loaded around its last known position (sector "
                     + SectorString(g_playerSector) + "), as a live tick would; the player sector goes out as unknown from orphaned tick "
                     + N((long long)cooporphan::kOrphanUnknownAfterTicks));
        }
    }
    const int sendUnknown = (orphan != 0 && cooporphan::OrphanSendUnknownSector(g_orphanRun) != 0) ? 1 : 0;
    const int sendX = sendUnknown ? kNoSector : MySectorForRelayX();
    const int sendY = sendUnknown ? kNoSector : MySectorForRelayY();
    g_loaded = loaded;
    /* decision 37 amended (review-p5g HIGH-2): the same set as a locked grid, so a worker thread can ask "have I got this area
       loaded" without touching the std::vector the main thread reassigns here every second. Written under g_heldLock at the one
       place g_loaded changes, so the grid and the vector cannot disagree. */
    /* E25: where the OTHER player was last seen, resolved BEFORE the lock is taken - PeerPlayerPosition walks the
       puppet map, and no engine-side walk belongs inside g_heldLock (every holder of that lock is a bounded memset
       or grid loop, and review-p4z audited them all on that basis). An answer older than the relay's own ownership
       grace is discarded rather than used: a stale peer halo would make this game YIELD a town it is entitled to. */
    Sector peerSec; peerSec.x = -1; peerSec.y = -1;
    {
        float qx = 0, qy = 0, qz = 0; double qAge = -1.0;
        if (PeerPlayerPosition(&qx, &qy, &qz, &qAge) && qAge >= 0.0 && qAge <= kPeerSectorMaxAgeSec) peerSec = SectorOf(qx, qz);
    }
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    std::memset(g_loadedHere, 0, sizeof(g_loadedHere));
    for (size_t li = 0; li < loaded.size(); ++li)
        if (loaded[li].x >= 0 && loaded[li].x < 64 && loaded[li].y >= 0 && loaded[li].y < 64) g_loadedHere[loaded[li].x][loaded[li].y] = 1;
    g_playerYTS = py;                    /* B10-b (review-b10 M-6): out of the SAME pass as the grids */
    g_playerSectorTS = g_playerSector;   /* E13 attempt 2: the ring-1 answer comes off the same locked read as the three grids, and out of the same tick that produced them */
    g_peerSectorTS = peerSec;            /* E25: and the tie-break's other half out of the same one */
    if (orphan != 0) { g_playerSectorTS.x = -1; g_playerSectorTS.y = -1; g_peerSectorTS.x = -1; g_peerSectorTS.y = -1; }   /* T-306: orphaned, there is no player to draw a halo round (ForgetPlayerHaloTS above) - keep it cleared */
    ::LeaveCriticalSection(&g_heldLock);
    // PROBE-START: P025
    /* P025 (F527/E13): the client is refused throughout its own town both before and after the relay names it holder. This
       samples the SAME view the refusal path reads, for the player's own sector, every 10 s on the main thread - so the
       refusals have a continuous background to be read against instead of eight isolated lines. */
    {
        static double lastEyeP025 = 0.0;
        if (nowR - lastEyeP025 >= 10.0)
        {
            lastEyeP025 = nowR;
            const Sector ps = MyPlayerSector();
            int hP = 0, mP = 0, olP = 0, lP = 0, r1P = 0, pr1P = 0, lowP = -2, oP = -2; double ageP = -1.0;
            AreaViewTS(ps, &hP, &mP, &olP, &lP, &r1P, &pr1P, &lowP);
            AreaProbeTS(ps, &oP, &ageP);
            char bP[256];
            /* E25.9 (review-p5p MEDIUM-4): `ring1` IS GONE FROM THIS LINE. The eye asks about the player's own
               sector, from inside the tick that has just written g_playerSectorTS three statements above, so the
               Chebyshev test was abs(0) <= 1 on every line this probe could ever print - a 1 fixed by control flow,
               which a readout was reading as corroboration that the presumption was live. `peerRing1` is not
               tautological: it says whether the other player was beside this one, which is the new question. */
            /* E25 re-designed: WHICH SOURCE ANSWERED peerRing1. `relay` = the notebook process's player-sector
               table (the one that still answers inside the H047 window); `puppet` = the old path, this game's
               copies of the peer's characters, used only when no fresh relay table exists. A run in which the
               tie-break matters and this says `puppet` is a run in which the re-design did not take effect. */
            _snprintf(bP, 255, "[PROBE] P025 eye: sector %d,%d held=%d mine=%d loadedHere=%d peerRing1=%d peerRing1Src=%s peerLowSlot=%d owner=%d heldAge=%.2f mySlot=%d",
                      ps.x, ps.y, hP, mP, lP, pr1P, (lowP == -2 ? "puppet" : "relay"), lowP, oP, ageP, StoreMySlot());
            bP[255] = 0; DebugLog(bP);
        }
    }
    // PROBE-END: P025
    std::string list;
    for (size_t i = 0; i < loaded.size(); ++i) { if (i) list += " "; list += SectorString(loaded[i]); }
    if (g_ticks % 5 == 1)   // one line every ~5 s; the [VERDICT] at report carries the set
    {
        DebugLog("[ZONES] player sector=" + SectorString(g_playerSector) + " loaded(" + N((long long)loaded.size()) + ")=" + list
                 + (orphan != 0 ? " orphan=1 (last known position, no live watched character; orphaned tick " + N((long long)g_orphanRun)
                                  + ") sent=" + SentSectorString(sendX, sendY) : std::string()));   /* T-306 fold 1: what actually went out while orphaned */
        // F478: the engine's own active-zone list with the reason each is alive - c = camera, p = player character, t = town
        // (ZoneMap::isActivationType 0xA07B70 over activatedCountdown[3] at +0xC0) and the seconds left on its deactivation.
        DebugLog("[ZONES] active: " + ActiveZoneReasons(zm));
    }
    /* M2 (decisions 32/44/54): ONE REPORT, TO THE NOTEBOOK, FROM EITHER ROLE. The host's arm used to keep the A6
       first-loader owner map (HostNote) and send MSG_ZONES and MSG_SECTORMAP to the other game, and the client's arm
       sent MSG_ZONES; all three went with the no-notebook fallback they fed, which left the two arms the same code.
       P6c (p5z HIGH-2) - NO !empty GUARD. An empty loaded set is a REPORT, not a reason to stay silent: this message
       also carries where this game's own player is standing, and the window in which the set is empty (the 10-12 s
       after a teleport, while the engine's zone-loaded flag is still down) is the window the other game needs that
       answer in. StoreSendAreas accepts count 0 and builds a valid 12-byte payload for it. The pointer is 0 rather
       than &v[0] because taking the first element of an empty vector is undefined even when nothing reads it.
       P6j (verify-p6c MEDIUM-1): THE SEND, NOT THE TICK - StoreSendAreas refuses with no notebook link and when the
       send fails, and the counters read its answer. */
    {
        if (g_areasLeaveOwed != 0) ZonesAreasLeave("world torn down");   /* M6 fold 1: an unwind skipped the teardown's send - the empty report goes BEFORE the new world's first one */
        std::vector<int> xy; xy.reserve(loaded.size() * 2);
        for (size_t i = 0; i < loaded.size(); ++i) { xy.push_back(loaded[i].x); xy.push_back(loaded[i].y); }
        const bool sentAreas = StoreSendAreas(xy.empty() ? 0 : &xy[0], (int)loaded.size(), sendX, sendY);   /* T-306 fold 1: sendX/sendY - the live sector, or unknown from the second orphaned tick */   /* decision 32: the notebook keeps the map */   /* E25: and the player sector, out of the same tick as the halo above */
        if (sentAreas) ++g_areasSent;
        if (sentAreas) { g_lastSentAny = 1; g_lastSentX = sendX; g_lastSentY = sendY; if (sendUnknown != 0) ++g_orphanSentUnknown; }   /* T-306 fold 1: for the VERDICT */
        if (sentAreas && xy.empty()) ++g_areasSentEmpty;
        if (sentAreas) g_areasLastNonEmpty = xy.empty() ? 0 : 1;   /* M6 fold 1: what ZonesAreasLeave reads */
    }
}

/* M2 (decisions 32/44/54) - THE NOTEBOOK'S ANSWER AND NO OTHER. The two role-specific fallbacks that stood here
   (the host walking the peer's MSG_ZONES set, the client reading the host's SECTORMAP) are deleted with
   ApplyRemoteZones, ApplyRemoteSectorMap and OwnedByOther. With no fresh notebook map this answers false, so a
   caller that must not read that as "nobody has it" asks RelayMapFresh() first. ANY THREAD: OtherLoadedTS takes
   g_heldLock.
   THE FREEZE RULE (manager ruling, 2026-09-22). While the NOTEBOOK link is down mid-session (T240/T241's
   kill-and-restart) there is NO live source of who has which area loaded. Nothing is inferred in its place: the
   announce pass (worldsync.cpp AnnouncePass) neither withdraws nor re-announces while the map is not fresh and
   counts each frozen pass announceStale; the W1-b effective map is carried unchanged; the writer ladder answers
   NO-ANSWER for anything it cannot decide without the notebook. */
bool OtherHasSector(const Sector& s) { return OtherLoadedTS(s) == 1; }
/* P6w (review-p6n MEDIUM-2). THE THIRD ANSWER, KEPT. 1 loaded, 0 not loaded, -1 the question could not be
   asked at all - no ZoneManager, or the engine's own probe faulted. Both of those still increment the two
   counters they always did; what is new is that a caller can now SEE which answer it got instead of reading
   a false that means three different things. IsPositionLoadedHere is unchanged in behaviour: it folds -1 into
   false, which is the safe direction for every caller that only wants a yes. */
int IsPositionLoadedHereTri(float x, float y, float z)
{
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) { ++g_posLoadedNoZm; return -1; }
    const int r = IsLoadedAt(zm, x, y, z);
    if (r < 0) { ++g_posLoadedFault; return -1; }
    return (r == 1) ? 1 : 0;
}
bool IsPositionLoadedHere(float x, float y, float z) { return IsPositionLoadedHereTri(x, y, z) == 1; }
/* mmo8a3 (zone-building-load-order.md sec. 2 item 3): the engine's OWN "this zone is loaded" byte, ZoneMap+0xB1 (the byte
   ZoneMap::isLoadedMT 0xA081E0 returns). processLoading 0xA0E950 sets it in phase 4 - after the zone's saved buildings were
   created (ZoneMapContent::_activate 0x9FEC00, phase 2) and after it joined activeZones (phase 3) - and the unload clears it
   first thing (0xA09620:14), so unlike +0xB3 (what IsPositionLoadedHere reads through 0xA0D560) it never reads a stale
   "loaded". The ZoneMap is located exactly as 0xA0D560 does it: sector = sectorRecord 0x9B1EC0 of (x, z), UNCLAMPED
   (SectorOf clamps; the engine does not), refused unless 0 <= sx < 64 and 0 <= sy < 64 (the engine answers false there),
   ZoneMap = ZoneManager + 0xC8 + (sx * 64 + sy) * 0x168. MAIN THREAD ONLY - the flag and the ZoneMap array are the frame
   thread's; any other thread is refused (-1) and counted. 1 = loaded, 0 = not, -1 = refused / no ZoneManager / faulted. */
long long g_zbOffMain = 0, g_zbNoZm = 0;
/* T-274 (t274-zone-scan.md S1): the +0xB1 read by SECTOR - the thread test, the ZoneManager test, the bounds (0 outside, as the
   engine answers) and the guarded byte, in that order (ZoneBuildingsInHereTri's, unchanged). */
int ZoneLoadedSectorTri(int sx, int sy)
{
    const unsigned long mt = StoreMainThreadId();
    if (mt == 0 || ::GetCurrentThreadId() != (DWORD)mt) { ++g_zbOffMain; return -1; }
    void* zm = ZoneManagerPtr();
    if (!PlausiblePod(zm)) { ++g_zbNoZm; return -1; }
    if (sx < 0 || sx >= 64 || sy < 0 || sy >= 64) return 0;
    __try
    {
        const unsigned char b1 = *(const unsigned char*)((uintptr_t)zm + 0xC8 + (uintptr_t)(sx * 64 + sy) * 0x168 + 0xB1);
        return (b1 != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int ZoneBuildingsInHereTri(float x, float y, float z)
{
    (void)y;
    const int sx = (int)floorf((kSectorOrigin + x) / kSectorSize);
    const int sy = (int)floorf((kSectorOrigin + z) / kSectorSize);
    return ZoneLoadedSectorTri(sx, sy);   /* T-274: the sector arithmetic + the by-sector read */
}
void ZoneBuildingsGateRefusals(long long* offMain, long long* noZm) { *offMain = g_zbOffMain; *noZm = g_zbNoZm; }
Sector MyPlayerSector() { return g_playerSector; }
bool SectorInMyRing(const Sector& s, int ring) { return g_playerSector.x >= 0 && abs(s.x - g_playerSector.x) <= ring && abs(s.y - g_playerSector.y) <= ring; }
bool SectorLoadedHere(const Sector& s) { for (size_t i = 0; i < g_loaded.size(); ++i) if (g_loaded[i].x == s.x && g_loaded[i].y == s.y) return true; return false; }   /* review-p4w HIGH-1: the 7x7 probe box is NOT what the relay knows; g_loaded is */

void ReportZones()
{
    const std::string q(1, (char)34);
    std::string list;
    for (size_t i = 0; i < g_loaded.size(); ++i) { if (i) list += " "; list += SectorString(g_loaded[i]); }
    DebugLog("[ZONES] REPORT ticks=" + N(g_ticks) + " probes=" + N(g_probes) + " probeFaults=" + N(g_probeFaults)
             + " noZoneManager=" + N(g_noZoneManager) + " noPlayer=" + N(g_noPlayer) + " orphan[entered,ticks,ended,sentUnknown,runNow]=" + N(g_orphanEntered) + "," + N(g_orphanTicks) + "," + N(g_orphanEnded) + "," + N(g_orphanSentUnknown) + "," + N((long long)g_orphanRun) + " posLoadedFault=" + N(g_posLoadedFault) + " posLoadedNoZm=" + N(g_posLoadedNoZm)
             + " playerSector=" + SectorString(g_playerSector) + " loaded=" + N((long long)g_loaded.size())
             + " yieldedStale=" + N((long long)g_yieldedStale) + " peerSect[recv,valid,ring1Relay,ring1Puppet,presumedEmptyGranted,preWelcomeIgnored]=" + N((long long)g_peerSectRecv) + "," + N((long long)g_peerSectValid) + "," + N((long long)g_peerSectRing1Relay) + "," + N((long long)g_peerSectRing1Puppet) + "," + N((long long)g_peerSectPresumedEmpty) + "," + N((long long)g_peerSectPreWelcomeIgnored) + "   (P6q: presumedEmptyGranted is NOT the old presumedEmpty - it counts ring-1 presumptions actually GRANTED, where the old name counted views on which one was on offer; preWelcomeIgnored is PLAYERSECTORS tables dropped because this link's WELCOME had not been processed yet)"
             + " areasSent=" + N(g_areasSent) + " areasSentEmpty=" + N(g_areasSentEmpty) + " areasLeaveSent=" + N(g_areasLeaveSent) + " inventRefusedTeardown=" + N((long long)g_inventRefusedTeardown)
             + "   (P8a: areasSent is AREAS messages this game got onto the wire to the NOTEBOOK; areasSentEmpty is"
               " the span of those whose loaded set was empty. A zero here on a linked game is a game never reporting"
               " its areas. M2: mapSent, mapRecv, mapSkippedRelay, sent and recv - the session-link SECTORMAP / ZONES"
               " counters - are RETIRED with the two messages, so a readout writes ABSENT for them; the sectormap"
               " VERDICT went with the A6 owner map it printed.)");
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "zones" + q + "," + q + "host" + q + ":" + (net::SessionIsHost() ? "1" : "0")
             + "," + q + "playerSector" + q + ":" + q + SectorString(g_playerSector) + q
             + "," + q + "orphan" + q + ":" + q + (g_orphanRun > 0 ? "1" : "0") + q + "," + q + "sentSector" + q + ":" + q + (g_lastSentAny != 0 ? SentSectorString(g_lastSentX, g_lastSentY) : std::string("none")) + q   /* T-306 fold 1 */
             + "," + q + "loaded" + q + ":" + q + list + q + "}");
}

} // namespace coop

namespace coop {
// P4b: thread-safe "does the host hold this sector" for the client (fails closed: no map or a stale one -> -1). Key() = y*64 + x.
int HostHoldsSectorTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int held = (g_hostHeldAt > 0.0 && NowSec() - g_hostHeldAt <= 5.0) ? (int)g_hostHeld[s.x][s.y] : -1;
    ::LeaveCriticalSection(&g_heldLock);
    return held;   // 1 held, 0 not held, -1 no fresh map
}
// T215: "does ANOTHER game have this area loaded" - the same lock and the same 5-s freshness rule, over the grid the relay's
// loadedMask fills. This is the announce question; holding is a different one (HostHoldsSectorTS above).
int OtherLoadedTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int on = (g_otherLoadedAt > 0.0 && NowSec() - g_otherLoadedAt <= 5.0) ? (int)g_otherLoaded[s.x][s.y] : -1;
    ::LeaveCriticalSection(&g_heldLock);
    return on;   // 1 another game has it loaded, 0 not, -1 no fresh relay map
}
/* M7a A1 build 2 [a1b2-zc0]: OtherKeepsBetterTS / OtherKeepsBetterDiagTS and their stamps are RETIRED (design 2.2) - the giver no longer
   predicts another game's engine; the receiver decides (liveowner.h AdoptDecide). */
// decision 35: "do I hold this area myself" - the relay's owner column read back for MY slot, on the same lock and the same clock
// as HostHoldsSectorTS (both are filled from one AREAMAP, so one clock covers both).
int MineHeldTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int mine = (g_hostHeldAt > 0.0 && NowSec() - g_hostHeldAt <= 5.0) ? (int)g_mineHeld[s.x][s.y] : -1;
    ::LeaveCriticalSection(&g_heldLock);
    return mine;   // 1 I hold it, 0 I do not, -1 no fresh map
}
// review-p5i CRASH-2: THE LOADED-HERE QUESTION, ASKABLE FROM A WORKER. SectorLoadedHere (above) walks g_loaded, and ZonesTick
// reassigns that whole vector on the main thread every second - the assignment frees the old buffer, so a worker that has
// already read .size() can dereference freed heap. g_loadedHere is the SAME set as a fixed grid, written under g_heldLock at
// the one place g_loaded changes, so the two cannot disagree. No clock is applied: unlike the relay's maps, this game's own
// loaded set is never stale, only empty. Out of range answers 0, exactly as the other any-thread queries above do.
int SectorLoadedHereTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int here = (int)g_loadedHere[s.x][s.y];
    ::LeaveCriticalSection(&g_heldLock);
    return here;   // 1 this game has the area loaded, 0 it has not
}
/* B10-b (review-b10 M-6). THE CENTRE POINT OF A SECTOR, so a road that holds only a sector - the zone-file
   writer - can take the very same LIVE POINT READ the box road takes instead of a 1 Hz grid flag. ANY
   THREAD: the only shared state read is g_playerYTS, under g_heldLock like every other TS reader here.
   The height is the one ZonesTick's own probe pass used; with no pass yet it is 0, which is a real world
   height and is the same answer the probe would have had. */
void SectorCentrePointTS(const Sector& s, float* xyz)
{
    if (!xyz) return;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const float py = g_playerYTS;
    ::LeaveCriticalSection(&g_heldLock);
    xyz[0] = (s.x + 0.5f) * kSectorSize - kSectorOrigin;
    xyz[1] = py;
    xyz[2] = (s.y + 0.5f) * kSectorSize - kSectorOrigin;
}
/* B9 (design-e46-store 2.3); M2: the NOTEBOOK'S grid - see zones.h for the three answers. It reads exactly what
   OtherLoadedTS reads, on the same 5 s clock, so the two cannot disagree about "fresh". Out of range answers -1, NOT 0: a sector nobody can name is a question that was not
   answered, and the caller drops a player's move on the difference. */
int PeerSectorLoadedTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return -1;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int on = (g_otherLoadedAt > 0.0 && NowSec() - g_otherLoadedAt <= 5.0) ? (int)g_otherLoaded[s.x][s.y] : -1;
    ::LeaveCriticalSection(&g_heldLock);
    return on;
}
// review-p5b HIGH-1: the freshness of the RELAY's loaded-by map, on its own clock. The announce pass's guard used to read the
// session clock (PeerZonesFresh) while its answer came from here, so a stalled relay with the session link still up read as
// "the peer holds nothing" and sent an UNLOAD for every announced character - H030/F445's failure, through a second clock.
bool RelayMapFresh()
{
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const bool fresh = (g_otherLoadedAt > 0.0 && NowSec() - g_otherLoadedAt <= 5.0);
    ::LeaveCriticalSection(&g_heldLock);
    return fresh;
}
/* M7a3-owed (owner decision 334 a): see zones.h. */
unsigned AreaMapApplySeqTS()
{
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const unsigned seq = g_areaMapApplySeq;
    ::LeaveCriticalSection(&g_heldLock);
    return seq;
}
int SectorHoldersTS(const Sector& s, unsigned* words, int n)
{
    for (int w = 0; w < n; ++w) words[w] = 0;
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const bool fresh = (g_otherLoadedAt > 0.0 && NowSec() - g_otherLoadedAt <= 5.0);
    if (fresh) for (int w = 0; w < n && w < coopdrop::kAreaMaskWords; ++w) words[w] = g_effMask[s.x][s.y][w];
    ::LeaveCriticalSection(&g_heldLock);
    return fresh ? 1 : -1;
}
}

namespace coop {
// `towns`: the engine's TownList (global pointer at RVA 0x21330A0; getAllTowns = this+0x50: count +0x58, pointers +0x60 - decomp_b85f0 /
// decomp_927d30) listed nearest-first from the watched player: sid, sector, position, held-by-the-host (client) - a recipe aid for T192+.
struct TownRow { float d; float x, y, z; char sid[96]; char name[96]; };
static bool TownRowLess(const TownRow& a, const TownRow& b) { return a.d < b.d; }
static int ReadTownListPod(void** count_out, void*** ptrs_out)
{
    __try
    {
        if (kZnTownListRva == 0) return 0;
        void* tl = *(void**)(uintptr_t)coop::AddrAbs(kZnTownListRva);
        if (tl == 0) return 0;
        const unsigned n = *(unsigned*)((char*)tl + 0x58); void** ptrs = *(void***)((char*)tl + 0x60);
        if (n > 4096 || (n != 0 && ptrs == 0)) return 0;
        *count_out = (void*)(size_t)n; *ptrs_out = ptrs; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int ReadTownPtrPod(void** ptrs, unsigned i, void** out) { __try { *out = ptrs[i]; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }   // review-p4c HIGH-1: the element read under SEH
static int TownRowPod(void* town, TownRow* r)
{
    __try
    {
        void** vt = *(void***)town; float v[3] = { 0, 0, 0 }; ((GetPositionFn)vt[8])(town, v);
        if (v[0] == 0.0f && v[2] == 0.0f) return 0;   // (0,0) = no write (review-p4c MEDIUM: sector 32,32 otherwise)
        r->x = v[0]; r->y = v[1]; r->z = v[2]; r->sid[0] = 0; r->name[0] = 0;
        void* gd = ((RootObjectBase*)town)->getRecord();
        if (gd != 0) { CopyStdStringPodZ((const char*)gd + 0x58, r->sid, 96); CopyStdStringPodZ((const char*)gd + 0x28, r->name, 96); }   /* GameData stringID +0x58, name +0x28 (build.cpp kGdName) */
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string TownsCommand()
{
    ::Character* pc = GetTarget(); float px = 0, py = 0, pz = 0;
    if (!PlausiblePod(pc) || !ReadPosition(pc, &px, &py, &pz)) return "error towns: no player";
    void* countv = 0; void** ptrs = 0;
    if (!ReadTownListPod(&countv, &ptrs)) return "error towns: no town list";
    const unsigned n = (unsigned)(size_t)countv;
    std::vector<TownRow> rows; rows.reserve(n);
    unsigned townFaults = 0, townRejected = 0;
    for (unsigned i = 0; i < n; ++i) { TownRow r; void* tp = 0; if (!ReadTownPtrPod(ptrs, i, &tp) || tp == 0) { ++townFaults; continue; } if (!TownRowPod(tp, &r)) { ++townRejected; continue; } { r.d = sqrtf((r.x - px) * (r.x - px) + (r.z - pz) * (r.z - pz)); rows.push_back(r); } }
    std::sort(rows.begin(), rows.end(), TownRowLess);
    std::ostringstream o; o.imbue(std::locale::classic()); o << "towns=" << n << " rows=" << rows.size() << " faults=" << townFaults << " rejected=" << townRejected << " listed=" << (rows.size() < 16 ? rows.size() : 16) << " from " << F1(px) << "," << F1(pz) << ":";
    for (size_t i = 0; i < rows.size() && i < 16; ++i)
    {
        const Sector s = SectorOf(rows[i].x, rows[i].z);
        o << " | '" << rows[i].sid << "' d=" << F1(rows[i].d) << " sector=" << s.x << "," << s.y << " pos=" << F1(rows[i].x) << "," << F1(rows[i].y) << "," << F1(rows[i].z);
        if (!net::SessionIsHost()) o << " host=" << HostHoldsSectorTS(s);
    }
    const std::string line = o.str(); DebugLog("[TOWNS] " + line); return line;
}

// towns1 (loot2 test ground): `townlist` - EVERY town in the engine's TownList (ruins and labs are towns too), one log line
// each with its record name, sid, sector and position, so a test can teleport to a named place. Read-only; the same guarded
// reads as `towns`. The status line is the tally only.
std::string TownListCommand()
{
    void* countv = 0; void** ptrs = 0;
    if (!ReadTownListPod(&countv, &ptrs)) return "error townlist: no town list";
    const unsigned n = (unsigned)(size_t)countv;
    unsigned faults = 0, rejected = 0, listed = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        TownRow r; void* tp = 0;
        if (!ReadTownPtrPod(ptrs, i, &tp) || tp == 0) { ++faults; continue; }
        if (!TownRowPod(tp, &r)) { ++rejected; continue; }
        const Sector s = SectorOf(r.x, r.z);
        std::ostringstream o; o.imbue(std::locale::classic());
        o << "[TOWNS] town name='" << r.name << "' sid='" << r.sid << "' sector=" << s.x << "," << s.y << " pos=" << F1(r.x) << "," << F1(r.y) << "," << F1(r.z);
        DebugLog(o.str()); ++listed;
    }
    std::ostringstream t; t.imbue(std::locale::classic());
    t << "townlist towns=" << n << " listed=" << listed << " faults=" << faults << " rejected=" << rejected;
    DebugLog("[TOWNS] " + t.str()); return t.str();
}
bool TownDistances(float x, float z, std::vector<std::pair<std::string, float> >* out, unsigned* faults)
{
    out->clear(); *faults = 0;
    void* countv = 0; void** ptrs = 0;
    if (!ReadTownListPod(&countv, &ptrs)) return false;
    const unsigned n = (unsigned)(size_t)countv;
    for (unsigned i = 0; i < n; ++i)
    {
        TownRow r; void* tp = 0;
        if (!ReadTownPtrPod(ptrs, i, &tp) || tp == 0 || !TownRowPod(tp, &r)) { ++*faults; continue; }
        out->push_back(std::make_pair(std::string(r.name), sqrtf((r.x - x) * (r.x - x) + (r.z - z) * (r.z - z))));
    }
    return true;
}
}

namespace coop { void ZonesInitLocks() { HeldLockInit(); } }   // called from InstallTownGen (preload, main thread) before any worker can reach HostHoldsSectorTS
// P6j (verify-p6c HIGH-2) - THE NOTEBOOK'S PLAYER-SECTOR TABLE IS THE LINK'S, AND IT DIES WITH THE LINK.
// This is deliberately NOT folded into ZonesForgetHeldGrid: that one runs at a world teardown, and the table is not
// world state - it describes where the players are, which outlives any one world. The link is what it belongs to.
// Clearing the clock alone would be enough for every reader (a stale clock answers "no fresh table" and `low` stays
// -2), but the valid flags are cleared as well so a later message that fills only some rows cannot leave an old row
// standing beside the new ones. Under g_heldLock because AreaViewTS reads this table from worker threads.
namespace coop { void ZonesForgetPeerSectors() { HeldLockInit(); ::EnterCriticalSection(&g_heldLock); for (int fps = 0; fps < kPeerSectorRows; ++fps)   /* M9 (T-197) */ { g_peerSectorRelay[fps].valid = 0; g_peerSectorRelay[fps].x = 0; g_peerSectorRelay[fps].y = 0; } g_peerSectorRelayAt = 0.0; g_peerSectorRelayGen = -1;   /* P6q (review-p6j MEDIUM-1): the generation goes with the table - the next link is a different numbering */ ::LeaveCriticalSection(&g_heldLock); } }
// P6q (review-p6j HIGH-1) - THE FOUR RELAY-WRITTEN GRIDS ARE KEPT WHEN A NOTEBOOK PROCESS IS LINKED.
// They are the ONLY things here that something outside this game refreshes: ApplyRelayAreaMap replaces all four and
// both clocks wholesale, once a second. Wiping them at a world teardown made AreaViewTS answer held = -1 at the
// instant the engine started building the next world's towns, and a declared client's bar-fly gate refuses on -1
// and drains the town's resident list for the life of that world (review-p6c HIGH-1, review-p6j HIGH-1). A KEPT map
// answers the right question about each area instead - mine, theirs, or unclaimed.
// P6x (review-p6q MEDIUM-3) - AND THE BOUND IS 5 s SINCE THE LAST AREAMAP, NOT ONE SECOND. One second is the
// relay's publication interval. What the readers apply is a 5 s staleness cap on the clock this grid carries -
// HostHoldsSectorTS, OtherLoadedTS, MineHeldTS, RelayMapFresh and AreaViewTS all answer -1 past it. Two
// consequences, in opposite directions, and both belong in writing: the kept map CANNOT FREEZE, because it expires
// on that cap by itself 5 s after the last AREAMAP whether or not a teardown happened (so "a frozen map is a claim
// nobody is maintaining" is guarded twice, not once); and the repair is therefore AT MOST 5 s WIDE while the load
// it covers is not bounded by 5 s. Kenshi's load blocks the main thread and PumpLink runs on the main-thread tick,
// so no AREAMAP arrives during the load and the kept clock simply ages; if the new world's towns are generated
// more than 5 s after the last map that landed before the teardown, held is -1 again and a declared client drains
// that town exactly as before, with cause `stale-lease`. T228 measured ~9.1 s of held stop-state across that
// window. The residual is visible rather than silent: `stale-lease` is its own cause string and P027 prints the
// inputs behind it.
// EVERYTHING ELSE IS EMPTIED IN BOTH CASES, and each for its own reason: g_loadedHere has no clock and nothing
// refreshes it during a teardown, so it would keep answering 1 for the world being freed (it is the reason the
// no-inventing gate exists at all); the two halos are this game's own inference about where the players are and a
// frozen inference is the E25.7 mistake; the loaded set and the player sector are the same world state one level
// up. (M2 deleted g_clientMap / g_peerLoaded / g_map, the no-notebook fallback containers this also emptied.)
namespace coop { void HandoffForgetWorldRequest(); }   /* M7a3f1: handoff.cpp - an interlocked POD flag, nothing else (this runs where TeardownBroadcastLateFlags runs) */
namespace coop { void ZonesForgetHeldGrid(int keepRelayGrid)
{
    HandoffForgetWorldRequest();   /* M7a3f1 (review fold 1 #1/#2): the owed hand-overs belong to the world being freed - only ASKED here (Z1-c: this may run off the main thread, no allocation, no logging); the main thread's next HandoffTick / ACK clears them */
    HeldLockInit(); ::EnterCriticalSection(&g_heldLock);
    if (keepRelayGrid == 0)
    {
        std::memset(g_hostHeld, 0, sizeof(g_hostHeld)); g_hostHeldAt = 0.0;
        std::memset(g_otherLoaded, 0, sizeof(g_otherLoaded)); g_otherLoadedAt = 0.0;
        std::memset(g_mineHeld, 0, sizeof(g_mineHeld));
        OwnerSlotClear();   /* M7b slice 2 */
        std::memset(g_effMask, 0, sizeof(g_effMask));   /* W1-b: the effective map is the relay grid's source, so it goes when the relay grid goes and is kept when it is kept */
    }
    std::memset(g_loadedHere, 0, sizeof(g_loadedHere));
    g_playerSectorTS.x = -1; g_playerSectorTS.y = -1; g_peerSectorTS.x = -1; g_peerSectorTS.y = -1;   /* E13 attempt 2: the player's sector is stale world state too, and it is cleared UNDER THE LOCK because a worker reads it. E25: the peer's with it */
    ::LeaveCriticalSection(&g_heldLock);
    g_loaded.clear(); g_playerSector.x = -1; g_playerSector.y = -1;
    g_orphanCentreValid = 0; g_orphanRun = 0; g_lastSentAny = 0; g_areasLeaveOwed = 1;   /* T-306: the last known centre (and what the last report said) belong to the world being freed */
} }   /* review-p4y MEDIUM-3: the loaded set and the player sector are stale world state too (-1,-1 = unset, as SectorInMyRing reads it) */   /* world teardown: the grid under its lock; the loaded set unlocked because every reader of it is main-thread (the any-thread readers read only the locked grids; M2 deleted the no-notebook maps that stood beside it) - TeardownBroadcast probes and logs an off-thread call (review-p4n) */
namespace coop { int HeldByOtherTS(const Sector& s) { return HostHoldsSectorTS(s); } }   /* decision 31(b): on either side, "held by someone other than me" (the grid is filled from each side's own view) */
namespace coop {
// review-p5f MEDIUM-2: ALL THREE GRIDS AND BOTH CLOCKS UNDER ONE LOCK ACQUISITION. The composed question below used to call
// HeldByOtherTS, MineHeldTS and OtherLoadedTS in turn, taking and releasing g_heldLock three times, so ApplyRelayAreaMap could
// swap the whole map between two of them. The interleaving that mattered turned a refusal into an allow: a worker read
// "held == 0" from a populated map, an empty AREAMAP zeroed both clocks, and the follow-up reads came back -1 from a map that
// no longer existed. The three single queries above are untouched - they still serve their own callers.
// Out of range answers 0 in every slot, exactly as those queries do; -1 means "no fresh map for that grid".
void AreaViewTS(const Sector& s, int* held, int* mine, int* otherLoaded, int* loadedHere, int* ring1, int* peerRing1, int* peerRing1LowestSlot, int* presumedEmptyOffer, int* peerLowFresh)
{
    int h = 0, m = 0, o = 0, l = 0, r1 = 0, pr1 = 0, low = -2, pe = 0;
    int lf = 1;   /* area2 fold: the lowest ring-1 slot's row is <= 2 s old (or its age is unknown) */
    const int mySlot = StoreMySlot();   /* read OUTSIDE the lock: a plain aligned int the main thread writes once per WELCOME, exactly as MySlotLower has always read it */
    /* P6j (verify-p6c HIGH-2): and the slot this game was LAST given, read the same way. The row-skip below needs it
       because g_mySlot is reset to -1 at link-down (P6c, correctly - a slot outliving its numbering is worse) while
       the notebook's table and its 5 s clock were left standing, and the relay echoes this game's OWN row back to it.
       For those seconds a game with no slot found itself in the table, called itself the other player, and - on a
       client - flipped invent into refuse. The table is invalidated at link-down now as well; this is the second
       half, and it holds for any other route by which a table could outlive a slot. */
    const int lastSlot = StoreLastKnownSlot();
    /* P6q (review-p6j MEDIUM-1): and the generation the table would have to belong to. Read outside the lock, like
       the two slots above, and for the same reason - it is an interlocked read of a value the main thread writes on
       a link edge, and a worker one edge behind decides as it would have decided a moment earlier. */
    const int linkGen = StoreLinkGen();
    if (s.x >= 0 && s.x < 64 && s.y >= 0 && s.y < 64)
    {
        HeldLockInit();
        ::EnterCriticalSection(&g_heldLock);
        const double now = NowSec();
        const bool heldFresh = (g_hostHeldAt > 0.0 && now - g_hostHeldAt <= 5.0);
        const bool loadedFresh = (g_otherLoadedAt > 0.0 && now - g_otherLoadedAt <= 5.0);
        h = heldFresh ? (int)g_hostHeld[s.x][s.y] : -1;
        m = heldFresh ? (int)g_mineHeld[s.x][s.y] : -1;
        o = loadedFresh ? (int)g_otherLoaded[s.x][s.y] : -1;
        l = (int)g_loadedHere[s.x][s.y];   /* decision 37 amended: no clock - my own loaded set is never "stale", only empty */
        /* E13 attempt 2: Chebyshev distance <= 1 from THIS game's own player, off the locked copy of the sector so it
           cannot come from a different tick than l did. An unset player sector (-1,-1) answers 0, never 1. */
        r1 = (g_playerSectorTS.x >= 0 && g_playerSectorTS.y >= 0
              && abs(s.x - g_playerSectorTS.x) <= 1 && abs(s.y - g_playerSectorTS.y) <= 1) ? 1 : 0;
        /* E25 RE-DESIGNED (verify-p5t HIGH-1): the same test about the OTHER player, off the same locked read, so
           "I am beside it" and "they are beside it" can never come from two different ticks - but asked of the
           NOTEBOOK PROCESS'S table when that table is fresh. Any slot other than mine whose player is within
           Chebyshev 1 of this sector makes the answer 1, and the LOWEST such slot is handed out, because that is
           the only thing MySlotLower can actually compare against. -1 = the table is fresh and nobody else is
           beside it. The puppet path below is kept for a session with no relay at all, where there is no table to
           read; it is the source review-p5t refuted for the relay case, and `low` stays -2 so the P025 eye and
           MySlotLower can both tell which one answered. */
        /* P6q (review-p6j MEDIUM-1): FRESH IS NOT ENOUGH - THE TABLE HAS TO BELONG TO THE LINK THIS GAME IS ON.
           A slot column is only meaningful under the numbering that issued it, and every link-up edge is a new
           numbering. ApplyRelayPlayerSectors refuses a table that arrives before this link's WELCOME, so a stored
           table is always post-WELCOME for the generation it carries; this second test covers the other direction,
           a table that was applied under a generation the game has since left. */
        const bool tableThisLink = (g_peerSectorRelayGen == linkGen);
        const bool relayFresh = (g_peerSectorRelayAt > 0.0 && now - g_peerSectorRelayAt <= kPeerSectorRelayFreshSec && tableThisLink);
        if (relayFresh)
        {
            low = -1;
            double lowAt = 0.0;   /* M9 (T-197): the lowest slot's row age - the table is keyed by slot now, not indexed by it */
            for (int ri = 0; ri < kPeerSectorRows; ++ri)
            {
                if (g_peerSectorRelay[ri].valid == 0) continue;
                const int si = g_peerSectorRelay[ri].slot;
                if (mySlot >= 0 && si == mySlot) continue;   // my own row is not "the other player"
                /* P6j (verify-p6c HIGH-2): and it is still not the other player after the link went down and took the
                   slot with it. P6q (review-p6j MEDIUM-1): BOTH CONDITIONS ARE STATED HERE - no slot of my own, AND
                   a table that belongs to the link I am on (i.e. one that arrived after this link's WELCOME, which
                   is what ApplyRelayPlayerSectors enforces). `tableThisLink` is already implied by `relayFresh`
                   above; it is written out again because this line is the one review-p6j MEDIUM-1 is about, and a
                   reader must not have to reconstruct the precondition from another branch. Without it the skip
                   fires in exactly the window where g_lastKnownSlot names the PREVIOUS numbering, and removes a
                   real peer's row instead of this game's own echo. */
                if (mySlot < 0 && lastSlot >= 0 && tableThisLink && si == lastSlot) continue;
                if (abs(s.x - g_peerSectorRelay[ri].x) > 1 || abs(s.y - g_peerSectorRelay[ri].y) > 1) continue;
                pr1 = 1;
                if (low < 0 || si < low) { low = si; lowAt = g_peerSectorRelay[ri].at; }   /* M9 (T-197): row ri, slot si */
            }
            if (low >= 0) { const double la = lowAt;   /* M9 (T-197) */ lf = (la <= 0.0 || now - la <= kPeerLowRowFreshSec) ? 1 : 0; }   /* area2 fold */
            if (pr1 == 1) ::InterlockedIncrement64(&g_peerSectRing1Relay);
            /* P6c (p5z MEDIUM-3), CORRECTED BY P6j (verify-p6c MEDIUM-4): the table was fresh, it named NOBODY beside
               this sector, and this game's player IS beside it - the configuration in which the ring-1 presumption is
               granted with nothing to yield to. In the arrival window that is true on BOTH games at once, which is the
               residual the design admits. THE COUNT NO LONGER HAPPENS HERE. This function is asked about an area long
               before anyone decides anything with the answer, and it cannot see the three short-circuits above the
               grant - so every view of an area another player HOLDS was being counted as a presumption on offer, and
               so was every pass of the 10 s eye, which never invents anything. The configuration is handed out as an
               OFFER and MayInventFromView counts it where it is actually granted. */
            else if (low == -1 && r1 == 1) pe = 1;
        }
        else
        {
            pr1 = (g_peerSectorTS.x >= 0 && g_peerSectorTS.y >= 0
                   && abs(s.x - g_peerSectorTS.x) <= 1 && abs(s.y - g_peerSectorTS.y) <= 1) ? 1 : 0;
            if (pr1 == 1) ::InterlockedIncrement64(&g_peerSectRing1Puppet);
        }
        ::LeaveCriticalSection(&g_heldLock);
    }
    if (held != 0) *held = h;
    if (mine != 0) *mine = m;
    if (otherLoaded != 0) *otherLoaded = o;
    if (loadedHere != 0) *loadedHere = l;
    if (ring1 != 0) *ring1 = r1;
    if (peerRing1 != 0) *peerRing1 = pr1;
    if (peerRing1LowestSlot != 0) *peerRing1LowestSlot = low;
    if (peerLowFresh != 0) *peerLowFresh = lf;
    if (presumedEmptyOffer != 0) *presumedEmptyOffer = pe;   /* P6j (verify-p6c MEDIUM-4) */
}
// PROBE-START: P025
// P025 (F527/E13): the two inputs AreaViewTS does not return - WHICH slot the relay's map named for this area, and HOW OLD
// that map is. Read under the same lock so the owner and the age cannot come from two different maps. -2 = no row for
// this area, -1 = a row naming nobody. heldAge is seconds since g_hostHeldAt, -1.0 if no map has ever landed.
void AreaProbeTS(const Sector& s, int* owner, double* heldAge)
{
    int o = -2; double age = -1.0;
    if (s.x >= 0 && s.x < 64 && s.y >= 0 && s.y < 64)
    {
        HeldLockInit();
        ::EnterCriticalSection(&g_heldLock);
        o = (int)g_ownerSlot[s.x][s.y];
        age = (g_hostHeldAt > 0.0) ? (NowSec() - g_hostHeldAt) : -1.0;
        ::LeaveCriticalSection(&g_heldLock);
    }
    if (owner != 0) *owner = o;
    if (heldAge != 0) *heldAge = age;
}
// PROBE-END: P025
/* M7b slice 2 (T-197): the area's HOLDER for deciding - the same grid AreaProbeTS reports, read under the same lock with the
   freshness every other area answer has (a map no older than 5 s). -1 = unknown. */
int AreaHolderSlotTS(const Sector& s)
{
    int o = -1;
    if (s.x >= 0 && s.x < 64 && s.y >= 0 && s.y < 64)
    {
        HeldLockInit();
        ::EnterCriticalSection(&g_heldLock);
        if (g_hostHeldAt > 0.0 && NowSec() - g_hostHeldAt <= 5.0) o = g_ownerSlot[s.x][s.y];
        ::LeaveCriticalSection(&g_heldLock);
    }
    return (o >= 0) ? o : -1;
}
// THE RULE ITSELF, over a view somebody else has already read. One copy, called from the wrapper below AND from every gate
// that needs the refusal's CAUSE as well as the decision (review-p5g Q5: those gates used to ask a SECOND question through
// HeldByOtherTS after the decision, so an AREAMAP arriving between the two could label a refusal with a different map's
// answer). This project's rule about two things kept in step by hand applies to a predicate as much as to a counter.
// E25 RE-DESIGNED - WHICH OF US GOES FIRST WHEN BOTH OF US COULD, as a real comparison.
// The relay hands every game a slot in its WELCOME (store.cpp g_mySlot, decision 32), and a slot is the only totally
// ordered name the games share, so it is what breaks the tie. `peerRing1LowestSlot` is the lowest slot, other than
// mine, whose player AreaViewTS has just found within ring 1 of the area in question, so "my slot is lower than
// every one of them" is exactly `mine < lowest` - no assumption about how many games there are, and the two halves
// come out of one locked read.
//
// verify-p5t MEDIUM-2 - WHAT THIS REPLACES, AND WHY THE OLD JUSTIFICATION WAS FALSE. The old rule was
// `(StoreMySlot() > 0) ? 0 : 1`, which answers "I am lower" for a slot of -1 - i.e. on BOTH games at once whenever
// there is no relay, or before either WELCOME lands. Its stated reason was that "with no relay `peerRing1` is 0
// anyway, so nothing turns on it", and that is not true: `peerRing1` came from the replicated puppets, which arrive
// over the SESSION link and need no relay at all, and the no-relay grid fill stamps a fresh clock so the escape at
// `held < 0` does not fire either. Both games therefore reached the tie-break, both answered "lower", and both
// invented - the tie-break was inoperative in exactly the configuration its own comment said it could not matter in.
// With no relay slot there is no shared numbering to compare, so the session role is used instead: the host is the
// lower one. It is the same kind of name (totally ordered, and both games agree on it), and it is the only other
// one they share.
//
// g_mySlot is a plain aligned int written once per WELCOME on the main thread and read here from worker threads:
// a torn read is not possible on this architecture, and a worker seeing the previous value for one tick decides the
// tie the way it would have a tick earlier.
// P6c (p5z MEDIUM-1) - THE TEST IS ON THE TABLE THAT ANSWERED, NOT ON WHETHER I HAVE A SLOT.
// `peerRing1LowestSlot` carries WHICH SOURCE answered, and the three values mean three different things:
//   low == -2  no fresh relay table at all, so AreaViewTS answered peerRing1 from the replicated puppets and NO
//              slot is known - and in that branch peerRing1 may perfectly well be 1. This is not "nobody is beside
//              this area"; it is "the notebook did not answer". There is no shared numbering to compare, so the
//              session role decides, exactly as it does when this game has never had a slot.
//   low == -1  the table WAS fresh and named nobody else beside this area: there is nobody to yield to.
//   low >= 0   the lowest other slot beside this area; the comparison is the real one.
// The old first line asked `me >= 0` instead, and `g_mySlot` survived a relay outage (it had two writers in the
// whole tree - its initialiser and the WELCOME). So during an outage both games still held slots >= 0, the table
// aged past its 5 s freshness, `low` fell back to -2, both games took "I have a slot, nobody is beside me", both
// answered "I am lower" and both invented - verbatim the configuration verify-p5t MEDIUM-2 refuted, narrowed from
// "no relay at all" to "the relay went away". P6c closes the other half too: g_mySlot is reset to -1 at link-down
// (store.cpp, PumpLink), so the slot cannot outlive the numbering it belongs to.
/* P97 (owner 334 a / 337 a): F666's bounded no-map wait - the 20 s after which the game holding slot 0
   generated and announced with no fresh area map - is RETIRED with its fresh-sample and reset calls.  The
   town gates set refused work aside (T-392); the character factory and the announce sweep refuse/skip. */
/* B9-b (review-b9 M-3): ONE BODY, and the caller may now ask which of the two answers it got. The
   role-decided arm is the one `SessionIsHost()` return below and nothing else.
   M11 C1 (T-197, to-do M11): THE NO-TABLE ARMS ASK THE ROSTER BEFORE THE ROLE. "The host" is no ordering with three games or a
   headless server - nobody is the host of the others, so nobody wrote - and a world-road game has no session role at all. With the
   old link DOWN and a slot of my own, the LOWEST IN_WORLD slot on this link's PLAYERS roster writes (mppresence::TieBreakNoTable,
   the offline suite): every game reads the same roster, so exactly one answers "lower". With the old link UP, no slot, or no
   IN_WORLD row: the session role as before, so a two-game session run decides exactly as it did. */
int MySlotLowerEx(int peerRing1LowestSlot, int* byRoleOut)
{
    if (byRoleOut != 0) *byRoleOut = 0;
    const int me = StoreMySlot();
    if (peerRing1LowestSlot == -2 || me < 0)   // the notebook did not answer (-2), or a fresh table but no slot of my own to compare with
    {
        const int byRoster = mppresence::TieBreakByRoster(StorePresenceOldLinkTS(), me, StoreRosterLowestInWorldTS());
        StorePresenceTieNote(byRoster >= 0 ? 1 : 0);
        if (byRoster >= 0) return byRoster;   // M11 C1: the lowest IN_WORLD slot on the roster - the one ordering every game in the world shares
        if (byRoleOut != 0) *byRoleOut = 1;
        return net::SessionIsHost() ? 1 : 0;   // the old link up (or no roster / no slot): the session role is the only ordering the two games share
    }
    if (peerRing1LowestSlot < 0) return 1;             // low == -1: the table is fresh and no other game's player is beside this area
    return (me < peerRing1LowestSlot) ? 1 : 0;
}
int MySlotLower(int peerRing1LowestSlot) { return MySlotLowerEx(peerRing1LowestSlot, 0); }

int MayInventFromView(int held, int mine, int loadedHere, int ring1, int peerRing1, int mySlotLower, int presumedEmptyOffer, int peerLowFresh)
{
    /* P6c (p5z MEDIUM-2) - NOTHING IS INVENTED WHILE THE ENGINE IS DESTROYING THE WORLD. E33 moved
       ZonesForgetHeldGrid() to AFTER the engine's own teardown on purpose, so a sleep-time note taken during that
       window can still ask who holds an area instead of getting "no fresh map" - and that is an improvement. This
       is the other half of the same retention, pointing the other way: g_loadedHere has NO clock and neither of
       ZonesTick's bail-outs clears it, so once the teardown stops the zone manager or the player from resolving it
       freezes at the last live world's loaded set and keeps answering 1. With held = 0 and mine = 0 that reaches
       `loadedHere == 1` below and returns 1 where it used to return -1, and a declared client refuses on -1 but
       acts on 1 - so a client could populate an area while the world it belongs to is being freed. This is FIRST
       because it is not a question about the area at all.
       P6q (review-p6j LOW-1) - TWO CORRECTIONS TO THE SENTENCE THAT USED TO SIT HERE. It named g_tearingDown; the
       flag this gate reads has been g_engineTeardown since P6j, and the two have different spans. And it borrowed
       g_mySlot's staleness argument, which does not transfer: a one-tick-stale slot is a valid EARLIER ANSWER to a
       question about ordering, while a one-tick-stale `false` here is "invent into a world the engine is freeing
       right now", which is the one thing this gate exists to forbid - and the window it guards is 171 ms wide, not
       a run. StoreTearingDown() is an interlocked read now (store.cpp); the gate is asked from worker threads
       (towngen's two gates, worldgen's leaf gate) and it costs nothing per creation. */
    /* P6j (verify-p6c HIGH-1 + MEDIUM-2). TWO corrections to the line P6c wrote here.
       WHAT IT ASKS: StoreTearingDown() now reports a flag that is raised in TeardownBroadcast and lowered in
       TeardownBroadcastLate - the engine's own teardown and nothing else, 171 ms in T228. It used to report
       g_tearingDown, whose only clear anywhere in the tree is the first SquadContainer load of the NEXT world, and
       the teardown that fires on a world LOAD is resetGame: so this gate was up across the opening stretch of every
       load, and a refusal here drains a town's bar-fly list for the life of that world.
       WHAT IT ANSWERS: -2, not 0. A bare 0 is the answer every caller already had a cause for, so a refusal during a
       teardown was filed under `not-mine` / `gatedNotMine` / `mineArea[notMine]` - three pre-existing labels, one of
       them the exact shape a probe is watching for. All four callers name this one for what it is. */
    if (StoreTearingDown() != 0) { ::InterlockedIncrement64(&g_inventRefusedTeardown); return kInventTeardown; }
    if (held == 1) return 0;      // another player holds the area - theirs to populate (decision 31b, unchanged)
    if (held < 0) return -1;      // no fresh map - say so, do not guess
    if (mine == 1) return 1;      // the relay names me the holder (decision 37 as built in P5g)
    // decision 37 amended (review-p5g HIGH-2): an unclaimed area I have loaded is mine to invent in - the relay names me holder
    // within a second, but residents are generated once per zone load inside that second
    /* P5t (T418): AND IT YIELDS ON THE RING-1 TERMS. The notebook now gives an unclaimed area to the lowest slot whose player
       is within ring 1 of it (store_main.cpp OnAreas, coopstore::AreaAssignDecide), so a game that has the area loaded but is
       outranked by another player beside it steps aside here as well - the same condition as the ring-1 grant below. A
       claimed area was answered above (held / mine: decision 15, sticky). */
    if (loadedHere == 1)
    {
        if (peerRing1 != 1 || mySlotLower != 0) return 1;
        /* area2 fold (review-area2 MED): the lower slot's row may be up to 5 s old - it may have moved away and not have this
           area loaded any more, and then neither game would invent here. Yield only on a row <= 2 s old; counted. */
        if (peerLowFresh == 0) { ::InterlockedIncrement64(&g_yieldedStale); return 1; }
    }
    // E13 ATTEMPT 2, THE RING-1 PRESUMPTION (2026-09-04). H047 (F529, T224): the zone-loaded flag and everything downstream of it stay down for ~10-12 s after a teleport while the engine populates the town; the player's own ring-1 sectors are being loaded by this game and nobody else.
    // T224 measured it: the client teleported to sector 23,33 at 186.582 s, its own eye probe read
    // `held=0 mine=0 loadedHere=0 owner=-2` at 188.088 s, and the town-squad gate refused `44852-rebirth.mod` at 23,33
    // and `16842-gamedata.base` at 24,33 with exactly those inputs between 193.3 s and 195.0 s - the whole life of that
    // load's resident generation. The first sample with `loadedHere=1` is the ALLOWED line at 195.146 s, and the eye did
    // not read `mine=1 loadedHere=1 owner=1` until 198.140 s. So the refusals are not a wrong answer to the question
    // asked; they are the right answer to a question asked while every input was still down.
    // Ring 1 is the one fact that is true immediately and cannot belong to anybody else: the engine streams only the
    // ~3x3 sectors around the player, so an area next to MY player is being loaded by THIS game, and a game three or
    // four sectors away is not loading it and has nothing to have trampled. A HELD area is still refused first, above
    // this line, so the presumption can never take an area away from the player the relay named.
    // E25 / review-p5p HIGH-1 - AND IT YIELDS. "Nobody else is loading it" is the whole justification for the
    // presumption, and it stops being true the moment the other player is standing within a sector of the same
    // place: both games then presume, both invent, and the town's crowd is doubled - the F514c defect decision 37
    // removed, coming back through the exception to it. Two players converging on one town is not a corner case
    // here; it is what T226 is. So when they are beside it too, only the lower relay slot invents. Neither game
    // needs to agree with the other about anything but the slot order, which the relay gave them both.
    /* P6j (verify-p6c MEDIUM-4): THE GRANT, which is where the both-presume residual is really spent. Everything
       that could have answered instead - another player holds it, the relay names me holder, I have it loaded - has
       already returned above, so reaching this line with the offer set is the one event the counter claims to
       describe. `presumedEmptyOffer` says the table was fresh and named nobody; the rest of the condition is the
       ring-1 rule itself, so a yield to a lower slot is not counted as a presumption either. */
    if (ring1 == 1 && (peerRing1 != 1 || mySlotLower != 0))
    {
        if (presumedEmptyOffer == 1) ::InterlockedIncrement64(&g_peerSectPresumedEmpty);
        return 1;
    }
    return 0;                     // nobody holds it, I have not got it loaded, and either it is not next to my own player or the other player is next to it as well and outranks me here
}
// A GAME INVENTS PEOPLE ONLY WHERE THE NOTEBOOK PROCESS NAMES IT THE HOLDER OF THE AREA.
// decision 37 (E6, 2026-09-04): with the relay present a game invents people only where the relay names it the holder; an
// unclaimed area is claimed by the first reporter within a second and the engine's own top-up retries (recurrence-covered);
// without the relay the no-map rule applies. Decision 31(b) asked only "does another player hold this area", and "nobody holds
// it" was enough to invent - which is how two games standing far apart both populated the same edge town in an area neither of
// them held (T208 and T218, F514c). This function is the one question every creation gate asks: town squads, bar residents,
// the character factory, and the engine's own off-screen update.
//   1  = invent here (the relay names ME the holder of this area, OR nobody holds it and I have it loaded)
//   0  = do not invent here (another player holds it, or nobody does and I have not got it loaded)
//  -1  = no fresh map; the caller's own no-map rule decides, the same on every game (decision 48 - no role decides it). T-392
//        (owner 337 a): the town gates SET THE WORK ASIDE and re-offer it from the town's own check-up; the character
//        factory and the announce sweep refuse until the map is back (P97: the bounded wait is retired).
//
// AMENDED 2026-09-04 (review-p5g HIGH-2). As built in P5g the answer for an area the relay's map does not mention AT ALL was
// the same as for an area another player owns: held=0, mine=0 -> 0. Nobody has such an area loaded, so nobody ever reports it,
// so it never enters the map - every town outside both games' loaded rings was refused by BOTH games, permanently. And even
// inside a ring the map lags the load by up to a second, which is the whole life of a town's resident generation. The fourth
// answer closes both: an unclaimed area THIS game has loaded is this game's (first-there presumption). A HELD area is still
// refused first, so the amendment can never take an area away from the player the relay named.
int MayInventHereTS(const Sector& s)
{
    int held = 0, mine = 0, otherLoaded = 0, loadedHere = 0, ring1 = 0, peerRing1 = 0, peerLow = -2, presumedEmpty = 0, peerLowFresh = 1;
    AreaViewTS(s, &held, &mine, &otherLoaded, &loadedHere, &ring1, &peerRing1, &peerLow, &presumedEmpty, &peerLowFresh);   /* one lock, one map: the eight answers cannot come from eight different maps */
    return MayInventFromView(held, mine, loadedHere, ring1, peerRing1, MySlotLower(peerLow), presumedEmpty, peerLowFresh);   /* P6j: and the presumption offer rides the same read as everything it is composed with */
}
}
namespace coop {
// E25 / decision 37 amended - THE NOTEBOOK PROCESS'S ANSWER TO "WHERE IS THE OTHER PLAYER".
// MAIN THREAD (the store link's receive), writing under g_heldLock because AreaViewTS reads the table from worker
// threads. The whole table is REPLACED by each message, exactly as ApplyRelayAreaMap replaces the grids: the relay
// sends every current sector every second, so a slot missing from this message is a slot that has said nothing for
// the relay's own window and must stop answering here as well.
// An EMPTY message (n == 0, buf may be 0) is a fresh answer, not a missing one - it says no game reported a player
// sector inside that window - and it stamps the clock like any other. The residual, stated because it is one: for
// the seconds between this game linking to the relay and the OTHER game reporting its first sector, the table is
// fresh and empty, so both games answer "nobody beside me" and both may presume. `peerSect[recv,valid,...]` is what
// makes that visible - valid == 0 across a run means the relay never carried a peer sector at all.
void ApplyRelayPlayerSectors(const char* buf, int n, const char* ages)
{
    if (buf == 0) n = 0;
    if (n < 0) n = 0;
    /* P6q (review-p6j MEDIUM-1) - NOTHING IS APPLIED UNTIL THIS LINK'S WELCOME HAS BEEN PROCESSED. The relay
       broadcasts this table to every CONNECTED peer with no welcome test of its own, so it can and does land
       before the handshake that tells this game its slot. Applying it then gives AreaViewTS a table in which one
       row is this game's own echo and no way to tell which - and P6j's last-known-slot fallback, which exists for
       exactly that, is wrong in the same window, because a new link is a new numbering. Dropping the message is
       the conservative answer: with no fresh table the tie-break falls back to the session role, which is the
       designed no-relay behaviour, and the next broadcast is one second away. */
    if (StoreWelcomedThisLink() == 0) { ::InterlockedIncrement64(&g_peerSectPreWelcomeIgnored); return; }
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    std::memset(g_peerSectorRelay, 0, sizeof(g_peerSectorRelay));
    int rows = 0;
    for (int i = 0; i < n; ++i)
    {
        ::InterlockedIncrement64(&g_peerSectRecv);
        /* M9 (T-197; world-server protocol 65): 10 bytes a row - u16 slot, i32 x, i32 y. A row per player, keyed by its slot. */
        const int slot = (int)(unsigned char)buf[i * 10] | ((int)(unsigned char)buf[i * 10 + 1] << 8);
        int px = 0, py = 0;
        std::memcpy(&px, buf + i * 10 + 2, 4); std::memcpy(&py, buf + i * 10 + 6, 4);
        if (rows >= kPeerSectorRows) continue;                        // more rows than the 256 players the world server links: recv > valid
        if (px < 0 || px >= 64 || py < 0 || py >= 64) continue;      // INT_MIN ("I do not know") and anything off the grid
        PeerSectorRow& pr = g_peerSectorRelay[rows++];
        pr.slot = slot; pr.x = px; pr.y = py; pr.valid = 1;
        { const int ag = (ages != 0) ? (int)(unsigned char)ages[i] : 255; pr.at = (ag == 255) ? 0.0 : NowSec() - (double)ag / 10.0; }   /* area2 fold */
        if (!std::binary_search(g_slotsSeen.begin(), g_slotsSeen.end(), slot)) g_slotsSeen.insert(std::lower_bound(g_slotsSeen.begin(), g_slotsSeen.end(), slot), slot);   /* recruit3: a player slot seen in this world */
        ::InterlockedIncrement64(&g_peerSectValid);
    }
    g_peerSectorRelayAt = NowSec();
    g_peerSectorRelayGen = StoreLinkGen();   /* P6q (review-p6j MEDIUM-1): stamped with the link whose WELCOME the test above just confirmed */
    ::LeaveCriticalSection(&g_heldLock);
}
// W1-b: the session peer-gone path - the ONE event that says every other reporter is gone. Not the notebook link-down and not the
// relay-absent fills: a notebook restart says nothing about where the peer is, and that is exactly the T240 input.
int ZonesForgetEffectiveMask(int slot)
{
    HeldLockInit(); ::EnterCriticalSection(&g_heldLock);
    /* M8 (T-197 piece 8): a KNOWN slot forgets and arms only its own bit - every other player's carried bits stand (owner decision
       54). M9: any slot with a column arms that seat's bit; one with none arms nothing here and goes pending (M9f1, below). An UNKNOWN slot (-1: a session peer that
       never announced its number) keeps the pre-M8 rule: every bit (W1-c, below). The rule is cooppg::PeerGoneMaskBits (peergone.h). */
    /* M9 (T-197) + M9f1 (M9 review M1): the mask is SEAT-indexed; the slot's seat is its column in the seat book. A slot with no
       column yet goes PENDING (coopdrop::AreaBookForget): the next non-empty map arms its seat if it lists the slot and DROPS the
       entry if it does not, and an entry goes after kAreaPendingSec (15 s) whatever comes - left standing, it would land on the
       seat that player takes when it rejoins, and its next absence would drop its bits (duplicates, then a withdraw storm). */
    const double nowSec = NowSec();
    const int seat = coopdrop::AreaSeatOfSlot(g_areaBook.slotOfSeat, slot);
    unsigned int bits[coopdrop::kAreaMaskWords];
    const int armed = cooppg::PeerGoneMaskBits(slot, seat, bits);
    if (coopdrop::AreaMaskAny(bits)) for (int x = 0; x < 64; ++x) for (int y = 0; y < 64; ++y) for (int w = 0; w < coopdrop::kAreaMaskWords; ++w) g_effMask[x][y][w] &= ~bits[w];
    /* W1-c (re-check 2026-09-22): the notebook keeps listing a departed player for its own grace (store_main.cpp kGraceSec 10 s), so
       the clear above alone is undone by the next maps and the player's bits are then CARRIED as absent-but-known forever. Arm
       the seat: its next absence from a map drops its bits instead of carrying them, and the arm is spent. */
    g_areaMapPendingExpired += coopdrop::AreaBookExpirePending(&g_areaBook, nowSec);
    coopdrop::AreaBookForget(&g_areaBook, slot, seat, bits, nowSec);
    ::LeaveCriticalSection(&g_heldLock);
    return armed;
}
long long ZonesAnnounceAbsentReporter() { HeldLockInit(); ::EnterCriticalSection(&g_heldLock); const long long v = g_announceAbsentReporter; ::LeaveCriticalSection(&g_heldLock); return v; }
/* W1-c: the session link is back, so the peer did not leave - an arm still standing would drop its bits at its NEXT absence
   (a notebook restart, say), which is T240's storm by another road. Called from the link-up edge in WorldSyncTick. */
void ZonesDisarmDropOnAbsence() { HeldLockInit(); ::EnterCriticalSection(&g_heldLock); coopdrop::AreaBookDisarm(&g_areaBook);   /* M9 (T-197); M9f1: the arms and the pending slots */ ::LeaveCriticalSection(&g_heldLock); }
std::string ZonesAreaMapCounters()   /* M9 (T-197): areaMap[rows,seats,rekeyed,bytes] areaMapOdd[rekeyDropped,malformed,pendingArmed,pendingDropped,pendingExpired] (M9f1) */
{
    HeldLockInit(); ::EnterCriticalSection(&g_heldLock);
    const std::string v = N(g_areaMapRows) + "," + N(g_areaMapSeats) + "," + N(g_areaMapRekeyed) + "," + N(g_areaMapBytes)
        + " areaMapOdd[rekeyDropped,malformed,pendingArmed,pendingDropped,pendingExpired]=" + N(g_areaMapRekeyDropped) + "," + N(g_areaMapMalformed)
        + "," + N(g_areaMapPendingArmed) + "," + N(g_areaMapPendingDropped) + "," + N(g_areaMapPendingExpired);   /* M9f1 */
    ::LeaveCriticalSection(&g_heldLock);
    return v;
}
long long ZonesEffDroppedAfterPeerGone() { HeldLockInit(); ::EnterCriticalSection(&g_heldLock); const long long v = g_effDroppedAfterPeerGone; ::LeaveCriticalSection(&g_heldLock); return v; }
int ZonesEffCarriedCells() { HeldLockInit(); ::EnterCriticalSection(&g_heldLock); const int v = g_effCarriedCells; ::LeaveCriticalSection(&g_heldLock); return v; }
// decision 32: the relay's owner map (x, y, slot triples). The locked grid becomes "held by a slot other than mine"; while the relay
// is present its map wins over the host-computed one (both games see the same map from one clock).
double g_mySince[64][64];   /* inv4 fold 4: when my slot's bit appeared in each cell's effective mask (0 = clear); under g_heldLock */
int ApplyRelayAreaMap(const char* msg, int bytes, int mySlot)
{
    /* M9 (T-197; world-server protocol 65; design-many D6 (a)): the map is {seat table, rows} (coopdrop::AreaMapLayout) and every
       mask is 256 SEATS wide. Returns 1 applied, 0 malformed (counted areaMapOdd[malformed]; nothing is touched - the last good map
       ages out on its own 5 s clock, as a lost one does). */
    int listed[coopdrop::kAreaSeats]; int seatCount = 0, count = 0; size_t rowsAt = 0;
    const int ok = (msg != 0 && bytes > 0) ? coopdrop::AreaMapLayout((const unsigned char*)msg, (size_t)bytes, listed, &seatCount, &rowsAt, &count) : 0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    if (!ok) { ++g_areaMapMalformed; ::LeaveCriticalSection(&g_heldLock); return 0; }
    g_areaMapRows = count; g_areaMapSeats = seatCount; g_areaMapBytes = bytes;
    std::memset(g_hostHeld, 0, sizeof(g_hostHeld));
    std::memset(g_otherLoaded, 0, sizeof(g_otherLoaded));
    std::memset(g_mineHeld, 0, sizeof(g_mineHeld));
    OwnerSlotClear();   /* M7b slice 2: cleared with the grids it sits beside */
    const double nowSec = NowSec();
    if (count == 0)
    {
        /* review-p5d HIGH-1 + HIGH-2: an empty area map is NO map, not a fresh one, and it never stamps freshness (no game has
           reported an area inside the notebook's grace). Zeroing both clocks makes HeldByOtherTS / OtherLoadedTS / MineHeldTS all
           return -1 and RelayMapFresh() false. W1-b: g_effMask is NOT touched - no reporter in an empty map is gone. M9: nor is
           the seat book - an empty map re-seats nobody. M9f1 (M9 review M1): only the pending slots' 15 s limit runs. */
        g_areaMapPendingExpired += coopdrop::AreaBookExpirePending(&g_areaBook, nowSec);
        g_hostHeldAt = 0.0;
        g_otherLoadedAt = 0.0;
        ::LeaveCriticalSection(&g_heldLock);
        return 1;
    }
    /* M9f1 (M9 review M2): rows are variable length - walked with a cursor over the payload AreaMapLayout has already walked */
    std::memset(g_areaNewMask, 0, sizeof(g_areaNewMask));
    size_t rowAt = rowsAt;
    for (int i = 0; i < count; ++i)
    {
        int x = 0, y = 0, o = -1; unsigned mask[coopdrop::kAreaMaskWords];
        if (!coopdrop::AreaMapNextRow((const unsigned char*)msg, (size_t)bytes, &rowAt, &x, &y, &o, mask)) break;   /* cannot fail: the layout check walked every row */
        if (x < 0 || x >= 64 || y < 0 || y >= 64) continue;
        if (o >= 0 && o != mySlot) g_hostHeld[x][y] = 1;
        if (o >= 0 && mySlot >= 0 && o == mySlot) g_mineHeld[x][y] = 1;   /* decision 35: the areas I hold, read back out of the same owner column */
        g_ownerSlot[x][y] = (o >= 0) ? o : -1;   /* M7b slice 2: this row's owner column verbatim, any slot (S2-67); -1 = the row names nobody */
        for (int w = 0; w < coopdrop::kAreaMaskWords; ++w) g_areaNewMask[x][y][w] |= mask[w];   /* W1-b: g_otherLoaded is written from the EFFECTIVE map below, not from this row */
    }
    /* M9f1 (M9 review LOW "apply core"): THE REKEY, the pending slots, my own seat, every cell (W1-b: a present reporter is taken
       exactly - ANDed with the seats the map's table lists; an absent one keeps its last known bits; W1-c: an armed seat's absence
       drops them), the spent arms and the freed columns - one pure function, swept offline (m9f1_restart_sequence, m9f1_pending_*). */
    coopdrop::AreaApplyTally t;
    coopdrop::AreaApplyMapCore(&g_areaBook, listed, mySlot, nowSec, &g_effMask[0][0][0], &g_areaNewMask[0][0][0], 64 * 64, g_areaCellFlags, &t);
    for (int x = 0; x < 64; ++x)
        for (int y = 0; y < 64; ++y)
        {
            const unsigned char f = g_areaCellFlags[x * 64 + y];
            if ((f & coopdrop::kAreaCellMineNow) != 0) { if ((f & coopdrop::kAreaCellMineBefore) == 0 || g_mySince[x][y] == 0.0) g_mySince[x][y] = nowSec; }   /* inv4 fold 4 */
            else g_mySince[x][y] = 0.0;
            g_otherLoaded[x][y] = ((f & coopdrop::kAreaCellOther) != 0) ? 1 : 0;
        }
    g_areaMapRekeyed += t.moved; g_areaMapRekeyDropped += t.lost;
    g_areaMapPendingArmed += t.pendingArmed; g_areaMapPendingDropped += t.pendingDropped; g_areaMapPendingExpired += t.pendingExpired;
    if (t.absentOther) ++g_announceAbsentReporter;
    if (t.droppedArmed) ++g_effDroppedAfterPeerGone;
    g_effCarriedCells = t.carried;
    g_hostHeldAt = NowSec();
    g_otherLoadedAt = g_hostHeldAt;
    ++g_areaMapApplySeq;   /* M7a3-owed: a new fresh map - what the owed hand-overs wait for */
    ::LeaveCriticalSection(&g_heldLock);
    return 1;
}
/* inv4 fold (review-inv4 fold 1): THE ONE INPUT BOTH GAMES SHARE for a player-owned box - is `slot`'s bit set in the
   notebook's EFFECTIVE loaded map for this sector (the mask ApplyRelayAreaMap keeps, both games from the same maps). ANY THREAD.
   1 set, 0 clear (M9: also a slot with no column), -1 no fresh map (the same 5 s clock PeerSectorLoadedTS reads), a sector out of
   range or a negative slot. M9f1 */
int SlotMapLoadedTS(const Sector& s, int slot)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64 || slot < 0) return -1;   /* M9 (T-197): any slot - its seat is looked up below */
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const int on = (g_otherLoadedAt > 0.0 && NowSec() - g_otherLoadedAt <= 5.0) ? coopdrop::AreaMaskTest(g_effMask[s.x][s.y], coopdrop::AreaSeatOfSlot(g_areaBook.slotOfSeat, slot)) : -1;   /* M9 (T-197): 0 for a slot with no column */
    ::LeaveCriticalSection(&g_heldLock);
    return on;
}
/* inv4 fold 4: when this game's own bit last APPEARED in this sector's effective map (0 = not set now). ANY THREAD. */
double MyMapLoadedSinceTS(const Sector& s)
{
    if (s.x < 0 || s.x >= 64 || s.y < 0 || s.y >= 64) return 0.0;
    HeldLockInit();
    ::EnterCriticalSection(&g_heldLock);
    const double v = g_mySince[s.x][s.y];
    ::LeaveCriticalSection(&g_heldLock);
    return v;
}
}

/* recruit3 */
namespace coop {
void ZonesSlotsSeenReset(const char* why)
{
    (void)why;   /* no log line: world teardown may run off the main thread */
    HeldLockInit(); ::EnterCriticalSection(&g_heldLock); g_slotsSeen.clear(); ::LeaveCriticalSection(&g_heldLock);   /* M9 (T-197) */
}
int ZonesSlotsSeen()
{
    /* M9 (T-197): the distinct slots of any number, plus my own (was coopr::RecruitSlotCount over a 16-bit mask keyed by slot:
       slots 16 and past were not counted). RecruitMultEffective caps what it is given. */
    const int me = StoreMySlot();
    HeldLockInit(); ::EnterCriticalSection(&g_heldLock);
    int n = (int)g_slotsSeen.size();
    if (me >= 0 && !std::binary_search(g_slotsSeen.begin(), g_slotsSeen.end(), me)) ++n;
    ::LeaveCriticalSection(&g_heldLock);
    return n;
}
static void* RlFactionPod(::Character* c)
{
    __try { return (void*)c->getOwnerFaction(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string RecruitListCommand(const std::string& arg)
{
    double cx = 0.0, cz = 0.0, r = 0.0;
    if (std::sscanf(arg.c_str(), "%lf %lf %lf", &cx, &cz, &r) < 3 || r <= 0.0) return "error recruitlist: usage recruitlist <x> <z> <r>";
    if (StoreTearingDown() != 0) return "error recruitlist: the engine is tearing the world down - try again";
    if (coop::GameWorldPtr() == 0) return "error recruitlist: no game world";
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return "error recruitlist: implausible character list";
    std::map<std::string, int> byFac; int total = 0, noPos = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = *it;
        if (c == 0) continue;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!ReadPosition(c, &x, &y, &z)) { ++noPos; continue; }
        const double dx = (double)x - cx, dz = (double)z - cz;
        if (dx * dx + dz * dz > r * r) continue;
        ++total; ++byFac[FactionNamePod(RlFactionPod(c))];
    }
    const std::map<std::string, int>::const_iterator d = byFac.find("Drifters");
    char h[160];
    _snprintf(h, 159, "recruitlist at %.0f,%.0f r=%.0f chars=%d drifters=%d noPos=%d factions:", cx, cz, r, total, d != byFac.end() ? d->second : 0, noPos);
    h[159] = 0;
    std::string s(h);
    int shown = 0;
    for (std::map<std::string, int>::const_iterator f = byFac.begin(); f != byFac.end() && shown < 24; ++f, ++shown) s += " '" + f->first + "'=" + N(f->second);
    if ((int)byFac.size() > shown) s += " (+" + N((long long)byFac.size() - shown) + " more)";
    DebugLog("[RECRUIT] list " + s);
    return "ok " + s;
}
}
namespace coop {
/* M7a A1 build 1 [a1b1-zc0] (design 2.6 / 3.13): the published player-sector rows of THIS link - every slot, this game's own too - when
   the table is fresh (kPeerSectorRelayFreshSec) and belongs to this link; -1 = no such table. ANY THREAD, under g_heldLock. */
int PeerPlayerSectorsTS(int* slots, int* xs, int* ys, int cap)
{
    HeldLockInit();
    const int linkGen = StoreLinkGen();
    const double now = NowSec();
    int n = 0;
    ::EnterCriticalSection(&g_heldLock);
    const bool fresh = g_peerSectorRelayAt > 0.0 && now - g_peerSectorRelayAt <= kPeerSectorRelayFreshSec && g_peerSectorRelayGen == linkGen;
    for (int ri = 0; fresh && ri < kPeerSectorRows && n < cap; ++ri)
    {
        if (g_peerSectorRelay[ri].valid == 0) continue;
        slots[n] = g_peerSectorRelay[ri].slot; xs[n] = g_peerSectorRelay[ri].x; ys[n] = g_peerSectorRelay[ri].y; ++n;
    }
    ::LeaveCriticalSection(&g_heldLock);
    return fresh ? n : -1;
}
}
namespace coop {
// PROBE-START: P119 (M7a A1 P-a, 2026-10-02) - the engine's put-away condition (squad position vs the loaded edge): two main-thread readers
/* the engine's own activation flags and seconds left for sector (sx, sy), as ActiveZoneReasons prints them; "none" = no active zone there */
std::string P119ZoneText(int sx, int sy)
{
    const std::string all = ActiveZoneReasons(ZoneManagerPtr());
    const std::string key = N((long long)sx) + "," + N((long long)sy) + "[";
    size_t at = 0;
    while ((at = all.find(key, at)) != std::string::npos)
    {
        if (at == 0 || all[at - 1] == ' ') { const size_t e = all.find(']', at); return e == std::string::npos ? all.substr(at) : all.substr(at, e - at + 1); }
        at += key.size();
    }
    return "none";
}
/* world units from (x, z) to the nearest sector within 3 of its own that this game has NOT loaded (SectorLoadedHere); -1 = its own sector
   is not loaded; 99999 = every sector within 3 is loaded */
float P119EdgeDist(float x, float z)
{
    const Sector s = SectorOf(x, z);
    if (!SectorLoadedHere(s)) return -1.0f;
    float best = 99999.0f;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
        {
            Sector n; n.x = s.x + dx; n.y = s.y + dy;
            if (n.x < 0 || n.x > 63 || n.y < 0 || n.y > 63 || SectorLoadedHere(n)) continue;
            const float x0 = n.x * kSectorSize - kSectorOrigin, z0 = n.y * kSectorSize - kSectorOrigin;
            const float ddx = x < x0 ? x0 - x : (x > x0 + kSectorSize ? x - (x0 + kSectorSize) : 0.0f);
            const float ddz = z < z0 ? z0 - z : (z > z0 + kSectorSize ? z - (z0 + kSectorSize) : 0.0f);
            const float d = sqrtf(ddx * ddx + ddz * ddz);
            if (d < best) best = d;
        }
    return best;
}
// PROBE-END: P119
}
