// zones.h - M-A: map sectors, the loaded set on this instance, and the notebook's area map as this game reads it.
//
// The world is a 64x64 grid of 4608-unit sectors (origin offset 147456); the loaded set is the union of the
// sectors around every player-faction character (.modding/03-systems/zones.md, 2026-09-02). Decision 1-3 of
// docs/authority-model.md: each instance owns the sectors it loaded FIRST; the host keeps the map and tie-breaks.
// Step 1 (this file): both instances know and log their loaded set and report it to the notebook every second.
// M2 (decisions 32/44/54): the session-link half - the client's set to the host, the host's first-loader owner map
// and its SECTORMAP back - is DELETED; the notebook's AREAMAP is the one map. Nothing is simulated
// differently yet - this is the map the later milestones (M-D handoff, M-E host entering) act on.
#pragma once
#include <string>
#include <utility>
#include <vector>
namespace coop {

struct Sector { int x, y; };
// World position -> sector (the engine's own formula, ZoneManager::sectorRecord 0x9B1EC0: Confirmed constants).
Sector SectorOf(float x, float z);
std::string SectorString(const Sector& s);

// MAIN THREAD. Once per second: recompute this instance's loaded set (probes the engine's own
// `isZoneLoadedT` on a 7x7 ring around the watched player's sector), log it, and report it to the notebook (AREAS).
void ZonesTick();
// `report`: the loaded set as a [VERDICT] record (M2: the host's sectormap record went with the A6 owner map).
void ReportZones();
std::string BuildingsCommand();     // piece 3b lever: buildings in the active zones by owner faction
std::string TownsCommand();         // P4c lever: every town in the engine's TownList, nearest first (sid, sector, position, held-by-host)
std::string TownListCommand();      // towns1: every town, one [TOWNS] town line each (name, sid, sector, position)
// The engine's TownList as (record name, distance on the ground from x,z) - the same guarded reads as `towns`; a town whose row
// does not read is left out. false = no town list. *faults = the towns left out. MAIN THREAD.
bool TownDistances(float x, float z, std::vector<std::pair<std::string, float> >* out, unsigned* faults);
std::string OwnBuildingCommand();   // piece 3b lever: the nearest building becomes my player faction's
void* NearestBuildingWhere(int (*want)(void*), double* dist, std::string* err, unsigned* seen = 0);   // P18 fold 1: the nearest building `want` accepts (buyhouse nearest)
// E22c (P6e), MAIN THREAD: every BUILDING in this game's currently active zones, written into the caller's
// buffer as plain pointers. Nothing is dereferenced by the caller on the strength of this list alone - the
// point of it is that a pointer the engine no longer lists is a pointer nothing may touch, which is how the
// item layer's box-key cache is invalidated at a zone unload without a hook for one. Returns the count
// written, never more than `cap`. This is the walk `buildings` and `ownbuilding` already do, called rather
// than copied, so the two cannot drift apart.
/* P6w (review-p6n HIGH-1): `inactiveSkipped` IS GONE, and so is the test that fed it. P6n added it on the
   premise that getAllActiveZones can list a zone the engine has stopped keeping. The first half of that is
   true - 0xA09840 applies no test and copies every node of the manager's set - but the conclusion is not:
   ZoneManager::deactivateZone 0xA09BB0 ERASES the zone from that same set (boost unordered_set::erase
   0x9F0100 on the container at ZoneManager+0x168108, which is exactly the object 0xA09840 walks: +0x168120
   bucket index, +0x168128 size, +0x168140 buckets - both decompiles read, Confirmed), so an ordinarily
   unloaded zone is gone from the list before this walk can see it. MEMBERSHIP IN THE LIST IS THE LIVENESS
   TEST. The one path that does leave the entry standing, ZoneManager::deactivateAll 0xA098E0, nulls the
   ZoneMap's content pointer, which PlausiblePod already skips. `zonesSeen` stays: it is the walk's own total
   and it may be 0. */
/* P7a (review-p6t MEDIUM-3): `truncated` is set to 1 when any of the three internal limits bit - more than
   64 active zones, a zone whose object array would not read, or the output cap - so a caller using membership
   in the result as a LIVENESS test can tell "the engine has dropped it" from "the walk could not list it".
   P7g (review-p7a M-2): the output-cap term means A THING WAS NEVER EXAMINED (the inner loop stopped early),
   not that the zone was large - P7a compared the zone's raw thing count against the room left and so could
   raise this on a walk that dropped nothing. P7g (review-p7a L-4): a zone whose CONTENT pointer will not read
   is a fourth skip and is deliberately NOT flagged - it is the deactivateAll case, so nothing live is lost. */
int LoadedBuildings(void** out, int cap, int* zonesSeen = 0, int* truncated = 0);
/* T-274: LoadedBuildings restricted to the active zone of sector (sx, sy) (ZoneMap+0x18/+0x1C) - the same zone list, the same per-zone
   walk and `truncated` flag. -1 = the zone list could not be read; else the count written (0 = no such active zone). MAIN THREAD. */
int LoadedBuildingsInSector(int sx, int sy, void** out, int cap, int* truncated);
/* PROBE-START: P085 - per active zone: sector, ZoneMapContent+0xA8 fresh (-1 no content), ZoneMap+0x24, ZoneMap+0xA8; returns the count. MAIN THREAD. */
int LoadedZoneFreshness(int* sx, int* sy, int* fresh, int* hasFile, int* mapA8, int cap);
/* par1: the sector (ZoneMap+0x18/+0x1C) of every active zone with a readable content pointer - the list LoadedBuildings
   walks, read without walking any object. MAIN THREAD. -1 = the zone list could not be read (not "no zones"). */
int ActiveZoneSectors(int* sx, int* sy, int cap);
/* PROBE-END: P085 */
/* inv5 (kept from phase 0 by phase 1): every ITEM lying on the ground in this game's active zones (ZoneMapContent::items, the
   unordered_set<hand> at +0x110 that saveItems writes), each hand resolved through the engine's own hand::getItem 0x791D90
   (table). Read-only. MAIN THREAD. Returns the count written; `truncated` 1 = a zone or the cap cut the walk short. */
int LoadedGroundItems(void** out, int cap, int* zonesSeen, int* truncated);
// M-D - does ANOTHER game have this sector loaded? M2: answered from the NOTEBOOK'S area map alone (OtherLoadedTS == 1).
// With no fresh map it answers false, so a caller that must not read that as 'nobody has it' asks RelayMapFresh() first.
// OwnedByOther (the A6 owner map) and PeerZonesFresh (the session-link clock) are deleted. ANY THREAD.
bool OtherHasSector(const Sector& s);
// H030: true while the notebook's loaded-by map is younger than its 5-s grace. A stale map answers NOTHING - T131's
// host read 'stale' as 'holds nothing' and unloaded 48 live copies. During a notebook outage the announce pass freezes
// on this (the freeze rule, zones.cpp OtherHasSector). Takes g_heldLock.
bool RelayMapFresh();
/* M7a3-owed: the count of applied (non-empty, freshness-stamping) area maps - "a new map arrived" for the owed hand-overs. ANY THREAD. */
unsigned AreaMapApplySeqTS();
/* M7a3-owed: the sector's holder set - the effective loaded mask (seat bits), up to n words. 1 read from a fresh map, -1 no fresh
   map, 0 out of range (all three zero what is not read). ANY THREAD. */
int SectorHoldersTS(const Sector& s, unsigned* words, int n);
/* P97 (p97-zonesh, owner 334 a / 337 a): F666's bounded no-map wait is RETIRED - with no fresh area map every
   gate refuses on every game until the map is back. */
void ZonesInitLocks();
/* P6j (verify-p6c HIGH-2): MAIN THREAD - the notebook process's player-sector table and its clock, dropped. Called
   at link-down beside the slot reset, because the table is that link's numbering: the relay broadcasts every row to
   every game INCLUDING the one that wrote it, and once this game has no slot of its own it can no longer recognise
   its own row by number. A table with no fresh clock answers "no table" (low stays -2) and the tie-break falls back
   to the session role, which is the designed outage behaviour - exactly one of the two games invents. */
void ZonesForgetPeerSectors();
/* P6q (review-p6j HIGH-1): `keepRelayGrid` != 0 KEEPS the four relay-written grids (held, mine, owner, otherLoaded)
   and their two clocks, and empties everything else exactly as before. It is passed 1 at a world teardown while the
   notebook process is linked, because the relay republishes the whole map every second.
   P6x (review-p6q MEDIUM-3) - THE BOUND ON WHAT THIS CAN HOLD IS 5 s SINCE THE LAST AREAMAP, NOT ONE SECOND. One
   second is the relay's PUBLICATION interval; the cap every reader of these clocks applies is 5 s, and past it they
   all answer -1. So the kept map cannot freeze - it expires by itself 5 s after the last AREAMAP - and equally this
   only covers a world load whose towns are built inside that window; a longer load reaches them with held = -1
   again. A map at most 5 s since the last AREAMAP beats
   "no map" at the instant a loading world generates its towns, and "no map" is what made AreaViewTS answer held = -1
   and the bar-fly gate drain a town's residents for the life of that world. NO DEFAULT ARGUMENT - every caller has
   to answer the question, because getting it wrong in the other direction keeps a frozen map with nothing to
   refresh it. What is emptied in BOTH cases: this game's own loaded grid and loaded set, its player sector and both
   halos (M2: the session-link-only containers it also emptied are deleted). */
/* W1-b (T240; review 2026-09-22 H2/H3): the EFFECTIVE area map (coopdrop::AreaEffectiveMask) - a reporter listed in an AREAMAP is
   taken exactly, one absent from it keeps its last known bits. ZonesForgetEffectiveMask forgets a departed player in it and is called ONLY from the
   session peer-gone path (WorldsyncPeerGone) and PLAYER_GONE (WorldsyncPlayerGone, M8): not at a notebook link-down - the peer did not go anywhere when the notebook did,
   and the map is carried unchanged through the outage (M2 freeze rule; the relay-absent fills are deleted). The two counters feed the [P033] REPORT line. All three any thread, under g_heldLock. */
int ZonesForgetEffectiveMask(int slot);   /* M8: slot >= 0 forgets and arms that slot's bit only; -1 = every bit, as before. M9 (T-197): the bit is the SEAT the slot's column holds (returns 1; 0 when it has no column yet - it is armed at the first map that lists it); -1 returns 256 */
std::string ZonesAreaMapCounters();   /* M9: "rows,seats,rekeyed,bytes areaMapOdd[rekeyDropped,malformed]=d,m" for the report line */
long long ZonesAnnounceAbsentReporter();   /* maps in which a slot other than mine was carried because it was absent from the map */
void ZonesDisarmDropOnAbsence();              /* W1-c: session link came back - the peer did not leave */
long long ZonesEffDroppedAfterPeerGone();   /* W1-c: maps that dropped a slot armed by the session peer-gone */
int  ZonesEffCarriedCells();               /* latest map: sectors whose effective mask differs from the map's own mask */
void ZonesForgetHeldGrid(int keepRelayGrid);               /* world teardown: the locked sector grids are emptied as described above */                    // review-p4b HIGH-1: init the sector-grid lock once, main thread
int  HostHoldsSectorTS(const Sector& s);
int  OtherLoadedTS(const Sector& s);      /* T215: any thread; 1 another game has this area LOADED, 0 not, -1 no fresh relay map */
int  HeldByOtherTS(const Sector& s);      /* decision 31(b): any thread, either side; 1 held by another player, 0 not, -1 no fresh map */   // P4b: any thread; 1 held by the host, 0 not, -1 no fresh map (client side)
int  MineHeldTS(const Sector& s);         /* decision 35: any thread; 1 = I hold this area myself, 0 = I do not, -1 = no fresh map */
void ZonesSlotsSeenReset(const char* why);   /* review-recruit3 5b: forget the slots seen (world teardown, a new notebook link); any thread */
int  ZonesSlotsSeen();   /* recruit3: player slots seen in this world (the relay's sector table, sticky, plus my own slot); any thread */
std::string RecruitListCommand(const std::string& arg);   /* recruit3 `recruitlist <x> <z> <r>`: read-only, loaded characters near a point by faction; MAIN THREAD */
void ApplyRelayPlayerSectors(const char* buf, int n, const char* ages = 0);   /* area2 fold: ages = n bytes, each row's age in tenths of a second (255 unknown), or 0 */   /* E25 / decision 37 amended: MSG_PLAYERSECTORS from the notebook process - {u8 slot, i32 x, i32 y} * n, already past the count byte. MAIN THREAD (the store link's receive); the table it fills is read from any thread under g_heldLock. n == 0 (buf may be 0) is a FRESH answer meaning "no game reported a player sector inside the relay's window", not a missing one. */
void AreaViewTS(const Sector& s, int* held, int* mine, int* otherLoaded, int* loadedHere, int* ring1, int* peerRing1, int* peerRing1LowestSlot = 0, int* presumedEmptyOffer = 0, int* peerLowFresh = 0);   /* P6j (verify-p6c MEDIUM-4): `presumedEmptyOffer` is 1 when the relay table was FRESH, named nobody else beside this sector, and this game's own player IS beside it - the input configuration in which the ring-1 presumption would be granted with nothing to yield to. It is an OFFER and not an outcome: this function cannot see the held/mine/loadedHere answers that short-circuit above the grant, which is precisely why the counter moved out of here and into MayInventFromView. 0 in every other case, including a table that is not fresh. */   /* E25 re-designed (verify-p5t HIGH-1): `peerRing1LowestSlot` is the LOWEST relay slot, other than mine, whose player is within ring 1 of this sector; -1 = the relay table is fresh and nobody else is beside it; -2 = there is no fresh relay table, so `peerRing1` was answered from the replicated puppets instead and no slot is known. MySlotLower() takes exactly this value. */   /* E25 (review-p5p HIGH-1): `peerRing1` is 1 when the sector is within Chebyshev distance 1 of the OTHER player's last-known position - the same question as `ring1`, asked about them. 0 when they are not, when nothing of theirs is replicated here, or when their last sample is older than the relay's own 10 s ownership grace. Same lock, same read as `ring1`, so the two cannot come from different ticks. */   /* E13 attempt 2: `ring1` is 1 when the sector is within Chebyshev distance 1 of THIS game's own player (a locked copy of the player sector, so it comes off the same read as the grids); 0 otherwise, never -1 - it has no clock and no map behind it */   /* review-p5f MEDIUM-2: all the area grids under ONE g_heldLock acquisition; any pointer may be 0; -1 = no fresh map. `loadedHere` (decision 37 amended) is THIS game's own loaded set and has no clock: 1 or 0, never -1 */
int AreaHolderSlotTS(const Sector& s);   /* M7b slice 2 (T-197): ANY THREAD - the slot the relay's map names as this sector's HOLDER, under g_heldLock with AreaViewTS's 5 s freshness; -1 = unknown (no fresh map, no row, a row naming nobody, off the grid). Decided on (net/session.cpp: an item message goes to it), not a probe. */
// PROBE-START: P025
void AreaProbeTS(const Sector& s, int* owner, double* heldAge);   /* P025 (F527/E13): ANY THREAD, under the same g_heldLock as AreaViewTS - the relay map's OWNER SLOT for this sector (-1 the row says nobody, -2 no row at all) and the age in seconds of the map that filled it (-1 = never filled). Diagnostic only: nothing decides on it. */
// PROBE-END: P025
/* P6j (verify-p6c MEDIUM-2): THE TEARDOWN REFUSAL'S OWN ANSWER. MayInventFromView used to return a bare 0 while the
   engine was freeing the world, which every caller was already using for "another player holds this area, or nobody
   does" - so a teardown was booked to a pre-existing cause at all four of them and read, in the log, as the very
   defect the P025 probe was added to catch. -2 is a fifth answer and every caller must test it BEFORE its `may < 0`
   no-map arm, because it is negative too. */
const int kInventTeardown = -2;
int  MayInventFromView(int held, int mine, int loadedHere, int ring1, int peerRing1, int mySlotLower, int presumedEmptyOffer, int peerLowFresh = 1);   /* P6j: `presumedEmptyOffer` comes out of the SAME AreaViewTS read as the rest of the view and is counted only if the ring-1 grant is actually reached */   /* E25 (review-p5p HIGH-1): the ring-1 presumption YIELDS. Both players standing within two sectors of each other each presumed the shared town was theirs alone; now, when the other player is within ring 1 of it too, only the lower relay slot invents. */
/* B9-b (review-b9 M-3). MySlotLower, PLUS THE ONE FACT ITS ANSWER ALONE CANNOT CARRY: did a real slot
   comparison decide, or did the SESSION ROLE? It falls back to the role on TWO inputs - no slot of my own,
   and no fresh relay table (peerRing1LowestSlot == -2) - and the second happens with a perfectly good
   slot, so a caller testing StoreMySlot() >= 0 mis-reads it as a slot decision. *byRoleOut is 1 exactly
   when the role decided. MySlotLower is this function with the flag thrown away, so the two cannot drift
   apart (lesson 11: remove the hand). ANY THREAD. */
int  MySlotLowerEx(int peerRing1LowestSlot, int* byRoleOut);
int  MySlotLower(int peerRing1LowestSlot);   /* E25 re-designed (verify-p5t MEDIUM-2): a REAL comparison - is my relay slot lower than every slot whose player is within ring 1 of the area AreaViewTS was just asked about? With no relay slot there is no shared ordering to compare, so the session role decides (the host is the lower one) instead of both games answering "lower" as they did before. ANY THREAD. */   /* E13 attempt 2 (H047): the fifth answer - an unclaimed area within ring 1 of this game's own player is this game's to invent in, because nobody else is loading it */   /* review-p5g Q5: the decision 37 rule over a view already read - so a gate can label its refusal from the SAME locked read that decided it */
int  MayInventHereTS(const Sector& s);   /* decision 37: 1 = this game holds the area and may invent people in it; 0 = must not (someone else holds it, or nobody does); -1 = no fresh map; -2 = kInventTeardown, the engine is freeing the world (P6j) */
// P1c: the engine's own answer for one world position (ZoneManager::isZoneLoadedT).
bool IsPositionLoadedHere(float x, float y, float z);
/* P6w (review-p6n MEDIUM-2): THE SAME QUESTION WITH ITS THIRD ANSWER KEPT. 1 loaded, 0 not loaded,
   -1 THE QUESTION COULD NOT BE ASKED (no ZoneManager, or the engine probe faulted). IsPositionLoadedHere is
   this function with -1 folded into false, which is the safe direction for every caller that only wants a
   yes - but a caller that DROPS A PLAYER'S MOVE on the answer has to be able to say which of the two nos it
   got, because during a world transition the manager is unreadable for ~10-12 s and every drop in that window
   is a "could not ask" reported as an area answer. The two shared counters posLoadedFault / posLoadedNoZm
   cannot answer it: worldsync and store increment them too, so the box path's share cannot be subtracted. */
int IsPositionLoadedHereTri(float x, float y, float z);
/* mmo8a3: MAIN THREAD ONLY. The engine's own ZoneMap+0xB1 "loaded" byte for the zone holding (x, z) - set only after that
   zone's saved buildings are all created, cleared first thing at its unload. 1 loaded, 0 not, -1 refused off the main
   thread / no ZoneManager / faulted. Used by build.cpp for RESTORE rows only; every other caller keeps IsPositionLoadedHere. */
int ZoneBuildingsInHereTri(float x, float y, float z);
/* T-274: the same ZoneMap+0xB1 byte by SECTOR (0..63 each; outside = 0). 1 loaded, 0 not, -1 refused off the main thread / no
   ZoneManager / faulted (counted as ZoneBuildingsInHereTri's are). ZoneBuildingsInHereTri is the sector arithmetic + this; build.cpp's
   per-zone scan (BdZoneScanTick) polls it for every active zone. */
int ZoneLoadedSectorTri(int sx, int sy);
void ZoneBuildingsGateRefusals(long long* offMain, long long* noZm);   /* mmo8a3: ZoneBuildingsInHereTri's -1s off the main thread / with no ZoneManager */
Sector MyPlayerSector();
// PROBE-START: P119 - MAIN THREAD: the engine's activation text for a sector; the distance to the nearest sector this game has not loaded
std::string P119ZoneText(int sx, int sy);
float P119EdgeDist(float x, float z);
// PROBE-END: P119
int PeerPlayerSectorsTS(int* slots, int* xs, int* ys, int cap);   /* M7a A1 build 1 [a1b1-zh0]: this link's fresh player-sector rows (every slot), -1 = no fresh table. ANY THREAD */
void ZonesAreasLeave(const char* why);   /* M6 fold 1: MAIN THREAD - one empty AREAS if the last one listed sectors (world teardown, no world) */
bool SectorInMyRing(const Sector& s, int ring);
bool SectorLoadedHere(const Sector& s);   /* MAIN THREAD: is s in this game's actually-loaded set (the same set the relay is told about) */
/* B10-b (review-b10 M-6): ANY THREAD. Fills xyz[3] with the world position of a sector's CENTRE, at the
   height ZonesTick's own probe pass last used, so a caller holding only a sector can ask the engine the same
   live "is this point loaded" question the box road asks. */
void SectorCentrePointTS(const Sector& s, float* xyz);
int  SectorLoadedHereTS(const Sector& s);   /* review-p5i CRASH-2: ANY THREAD - the same question over the locked 64x64 grid, never the std::vector the main thread reassigns every second. 1 loaded here, 0 not (out of range is 0); no clock, because this game's own loaded set is never stale, only empty */
/* B9 (design-e46-store 2.3); M2 (decisions 32/44/54). ANY THREAD, under g_heldLock like every other TS reader.
   DOES ANOTHER GAME HAVE THIS SECTOR LOADED - the writer ladder's R2 input.
     1  = the notebook's effective area map says another game has it loaded (map fresh within 5 s)
     0  = the map is fresh and no other game has it
    -1  = no fresh notebook map (an outage, the first seconds of a link), or a sector out of range
   UNTIL M2 this read the other game's own MSG_ZONES report over the SESSION link, precisely so R2 could still
   answer while the notebook was silent. That is the no-notebook fallback decision 44 retires: with no fresh map
   this answers -1 and R2 does not decide. It is OtherLoadedTS except that out of range is -1, not 0. */
int  PeerSectorLoadedTS(const Sector& s);
int  SlotMapLoadedTS(const Sector& s, int slot);   /* inv4 fold: ANY THREAD - `slot`'s bit in the notebook's effective loaded map; 1 / 0 / -1 no fresh map */
double MyMapLoadedSinceTS(const Sector& s);       /* inv4 fold: ANY THREAD - when my bit appeared in that cell (0 = clear) */

} // namespace coop
