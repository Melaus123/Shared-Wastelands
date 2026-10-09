// build.cpp - build1-a/b (docs/design-build1.md sections 2-3; build/answers-build1b.md; cloud/ANSWERS.md "build1 - answers").
// build1-b: a NEW piece THIS game's player faction owns (the commit hook, or `buildtest place`) is sent as MSG_BUILD PLACE
// (src/common/buildwire.h); the other game queues it and creates a COPY owned by coop-peer at the K2 safe point
// (BuildCopyDrain, the answers' Safe call recipe). A copy is registered via=copy and is never sent back.
// build1-c: the builder's game sends MSG_BUILD STATE {key, progress, needed, complete, delivered mats} for its own pieces
// (BuildTick, from the progress hook's queued events: a 5%-of-needed step, a complete flip, a materials change; one per key
// per tick). The other game keeps the latest STATE per key and writes it onto its copy at the K2 safe point (BdApplyOne):
// through vt+0x228 (state +4 progress, capped below needed while not complete; mats[i]+0xC), setBroken(0), then
// updatePhysicalWithProgress 0x558680; a complete flip runs the engine's own vt+0x230(FLT_MAX). Copies are re-found by key
// every time; no pointer is kept.
// build1-d: when an own piece's isDismantled (+0x162) flips 0 -> 1 in addDismantleProgress (read before and after the
// call; for a wall segment its OWN +0x162), the builder's game sends MSG_BUILD REMOVE {key, reason 1} once per key and marks
// the row removed (a late STATE is then ignored). The other game, at the K2 safe point (BdRemoveOne): re-finds the copy by
// key (skipped if +0x162 is already set), zeroes its delivered mats (+0xC each, so the refund 0x29DBD0 drops nothing), then
// runs the engine's own vt+0x248(FLT_MAX). A WallBuilding defers (0x548860): the call is retried on later safe points until
// +0x162 is set or the key no longer resolves (bounded). A wall is identified POSITIVELY by its RTTI class (T325 fold). A REMOVE for a copy still pending drops its PLACE / held STATE
// instead. 0x29DBD0 is hooked only to COUNT refunds on coop-peer pieces. A wall copy's STATE writes mats only.
// review-build1d fold: reason 2 (destroyed) is no longer sent and is ignored on receipt - a destroyed piece is a repairable
// ruin on the builder's game (destroyed-state sync is a later effort). A REMOVE whose copy no longer resolves in a LOADED
// area is finished (removeAlreadyGone); a wall copy's mats are not zeroed; removed rows are erased 60 s after the removal.
// P15 (protocol 98): the DESTROYED state crosses in STATE (the complete byte's bit 1). The WRITER is the piece's owner (the game whose
// row is own; NPC / world buildings are not in this registry and are not synced). A real +0x1A1 flip on an own piece (setDestroyed
// 0x5567F0 either way - the ruin reset inside addConstructionProgress 0x558B10 ends in setDestroyed(0)) sends a STATE; the copy, at
// the K2 safe point (BdApplyOne), runs the engine's own setDestroyed(1) (vt+0x340) for a ruin, and the engine's repair road
// (vt+0x230(0): the ruin reset, then setDestroyed(0); a wall: vt+0x340(0)) for a piece made whole again. A copy made a ruin by THIS
// game's engine is put back to the owner's last STATE (BdCopyDestroyedHere); a repair on a ruin copy is help (HELP_WORK with the
// ruin reset, which the owner's game runs first on its complete ruin) and is refused on a wall ruin copy. Lever: `buildtest destroy`.
// build1-e: FURNITURE. A new own piece created with a host (isIndoorsOf / furnitureOf non-null) sends PLACE with the HOST's
// P7n key, pos/rot exactly as createBuilding received them (host-local), the furnitureOf form (0 *(host+0x1F0)+0x30, 1 the
// commit's vt+0x40 branch, 2 isIndoorsOf only), floor and outside (protocol 62). The other game re-finds the host by key
// (+0x1F0 required; not resolved -> hostPending, retried every kRetryGap safe points), rebuilds furnitureOf from ITS host in
// the same form and calls createBuilding with isIndoorsOf = host. A furniture piece lives in its host's layout, not on the
// zone list ObjectByPositionKey walks (BED1), so it is re-found among the host's interior pieces (BdFindInHost).
// recheck-build1d F2: a REMOVE finishes as already-gone only after a COMPLETE empty search (ObjectByPositionKeyVerdict ==
// kObjKeyNone) and never for a copy whose created key differed from the builder's; otherwise it waits, then gives up.
// Lever: `buildtest into near <sid>`.
// build1h H1 (investigations/build1h-house-owner-design.md 2(a)/(c); owner decisions 2026-09-26): a new piece whose host
// building belongs to ANOTHER player becomes the HOUSE OWNER's at placement. Right after createBuilding returns inside the
// commit (detour_create; the lever `buildtest into` the same way) the placer's game runs the engine's own setFaction vt+0xA0
// to that slot's stand-in (BdHandToHouseOwner, the pure rule coopown::HouseOwnerOfNewPiece), registers the piece as a COPY row
// and sends its PLACE with the owner-slot byte (protocol 74). The owner's game makes it as its OWN piece (via 5 "handed") and is
// its STATE / REMOVE source from then on. A town house (nobody's) is unchanged: the placer keeps the piece. Every row records
// the owner's slot; BuildRecordedOwnerSlot is the box-writer class's first rung (items.cpp ItBoxOwnerOf).
// Lever: `buildtest into <near|host-key-substring> <sid> [indoors]` (indoors = form 2, isIndoorsOf only, at (-25, 0, -25)).
// mmo8a3 (B): the scan described next also runs once per world load with the link DOWN (own pieces from the save are
// registered, so their dismantle retires their pp.build row and their progress is recorded); the request and sends wait for the link.
// build1-f ROSTER: when the session link comes up (the edge DoorsTick uses for DoorsOnLinkUp), and when a world loads while
// linked, BdRosterTick (BuildTick, main thread) first scans the loaded zones once (LoadedBuildings) for own-player-faction
// pieces the registry does not know - built in an earlier session and loaded from the save - and registers the free-standing
// ones as own (via=scan: key, sid, +0x48 position, vt+0xC0 rotation). Then, for every own row that is not removed and still
// resolves to a live, undismantled own piece here, it sends a PLACE (the kept placement inputs, the live progress/complete)
// and a STATE (the live mats): at most kRosterPerTick pieces per tick, the rest on the next ticks; a failed send stops the
// round there and the next tick resumes (never dropped). A receiver that already has the key treats the PLACE as a duplicate
// and applies the STATE. review-build1f H2 (protocol 63): a message that reaches a game at its title screen waits in the
// arrival queue under that world generation and is dropped as stale when the game loads, so a link-up roster sent to a late
// joiner is lost. Each game therefore sends MSG_BUILD kind 4 ROSTER_REQUEST once per world + link when its world runs
// (retried every tick until the send goes), and the game that receives it owes a whole roster round.
//
// THE HOOKS (probe P082 - removed when build1-b replaces them with the real sync). Each forwards every argument unchanged
// and only counts, reads and queues:
//   PlacementCommit 0x4D6810 (PreviewBuildingList*)  - main thread; marks "inside the commit" for its duration.
//   CreateBuilding  0x57C1E0 (16 args)               - pass-through; a top-level call INSIDE the commit is a new piece.
//   AddConstructionProgress 0x558B10 (b, amount)     - vt+0x230 of every class but WallBuilding; main thread (Read).
//   AddDismantleProgress    0x2A2820 (b, amount)     - vt+0x248; MAY RUN ON THE AI WORKER (Inferred).
//   SetDestroyed            0x5567F0 (b, bool)       - vt+0x340; only the main-thread half does the work.
// Every event detour builds a POD record (P7n position key, construction state) and queues it under a CRITICAL_SECTION;
// BuildTick (main thread) logs it and updates the registry. No std::string in any detour.
//
// THE REGISTRY `buildreg`: position key -> sid, owner name, progress. Filled by the commit (via=hook) and by the lever's
// placement (via=test), which run the same BdOnNewPiece; also by copies (via=copy), the link-up scan (via=scan), a piece
// handed to this game (via=handed) and, mmo8a, a piece re-made at load from this game's own pp.build record (via=restored).
// mmo8a: every own row (own, not a copy, not removed) is also kept as a PLACE + STATE row in the own-records store (pp.build,
// store.cpp OwnBuildWrite); at load a row whose key has no live piece is re-made through the copy queue with the RESTORE
// flag (this game's own faction, no stand-in wait, no HAND_ACK) after the same complete empty search furniture needs.
// MAIN THREAD only. Building pointers are never kept across
// frames: the lever and `buildlist` re-find each piece by key (ObjectByPositionKey) and re-check its key.
//
// THE LEVER `buildtest` (test-only): armed by the verb on the main thread, executed at the K2 safe point 0x7D17E0.
//   place <sid> [dx dz]    createBuilding per the "Safe call recipe" with THIS game's player faction as owner, next to the
//                          first own player character (+30,0 by default); refused while zone+0xA8 is set.
//   progress <sub> <amt>   vt+0x230(b, amt) - the engine's path, so the progress hook fires.
//   dismantle <sub>        vt+0x248(b, FLT_MAX) - what confirmDismantle does.
#include "build.h"
#include "farm.h"           /* par16: farms ride this channel (MSG_BUILD kinds 6 / 7) */
#include "addresses.h"      /* the five names below come from the address table */
#include "store.h"          /* EngineWritesBlocked, StoreMainThreadId */
#include "items.h"          /* ObjectPositionKey, ObjectByPositionKey - the one P7n key builder and its resolver */
#include "zones.h"          /* SectorOf - the same cell arithmetic as the engine's 0x9B1EC0 */
#include "spawn.h"          /* MirrorCapacity, MirrorSlot, SafeReadPosition */
#include "playerfaction.h"  /* LocalPlayerFaction */
#include "policy.h"         /* PolicyVtableRvaPod, PolicyClassNamePod - the RTTI class name (the wall test) */
#include "doors.h"          /* P15 fold 1: DoorsForgetOwner - a building's door rows are dropped before its setDestroyed flip */
#include "net/session.h"    /* IsUidMine, SendBuild */
#include "ownstore.h"        /* help1 fold: OwnWriterOn, OwnStoreCommittedSeq - pp.build landed (MED 4) */
#include "../common/buildwire.h"   /* build1-b: the MSG_BUILD payload */
#include "../common/boxowner.h"    /* ScanSkipsWorldNode - a world-placed production building is not an own piece */
#include "../common/slotwire.h"    /* P18 fold 2: StandInSlotOfId / IsLegacyPeerId - a saved owner id read as a stand-in record */
#include "../common/boxowner.h"    /* build1h: HouseOwnerOfNewPiece */
#include "../common/ownrec.h"      /* mmo8a: pp.build rows (EncodeBuildRec / BuildRow) */
#include "../common/placeinput.h"  /* a placement input height = live y less the ground height */
#include "../common/tradecharge.h" /* buyhouse cats=<n>: ParseCats / PayToSet, the purse set the buytest levers use */
#include <cstddef>
#include <vector>
#include "coop_log.h"
#include "hooks.h"     /* coop::AddHook (own MinHook) */
#include "game/Character.h"
#include "game/Faction.h"
#include "game/GameWorld.h"   /* T328: activeCharacters */
#include "game/hand.h"   /* build1-e: a host's interior pieces are hands (as spawn.cpp BED1) */
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>        /* house2: the removed-keys list */
#include "../common/groundkey.h"   /* P3: coopground::GroundBuildingSource - BuildGroundSource */
#include <deque>
#include <io.h>         /* house2 fold: _commit / _fileno */
#include <sstream>
#include <string>
#include "u8file.h"   /* UTF-8 paths through the wide Windows file calls */

namespace coop {

int GroundTerrainHeightAt(float x, float z, float* out);   /* combat.cpp - the terrain height at (x,z) (getTerrainHeight, SEH-wrapped): 1 height, 0 no terrain (-99), -1 no row / faulted. MAIN THREAD */

// PROBE-START: P082
static unsigned long long kBdCommitRva = 0;    static AddrReg kBdCommitRva_reg("PlacementCommit", &kBdCommitRva);              /* Steam_1.0.65 0x4D6810 */
static unsigned long long kBdCreateRva = 0;    static AddrReg kBdCreateRva_reg("CreateBuilding", &kBdCreateRva);               /* Steam_1.0.65 0x57C1E0 */
static unsigned long long kBdProgressRva = 0;  static AddrReg kBdProgressRva_reg("AddConstructionProgress", &kBdProgressRva);  /* Steam_1.0.65 0x558B10 */
static unsigned long long kBdDismantleRva = 0; static AddrReg kBdDismantleRva_reg("AddDismantleProgress", &kBdDismantleRva);   /* Steam_1.0.65 0x2A2820 */
static unsigned long long kBdDestroyRva = 0;   static AddrReg kBdDestroyRva_reg("SetDestroyed", &kBdDestroyRva);               /* Steam_1.0.65 0x5567F0 */
// PROBE-END: P082
static unsigned long long kBdSetFactionRva = 0; static AddrReg kBdSetFactionRva_reg("Building_setFaction", &kBdSetFactionRva);  /* Steam_1.0.65 0x556EC0 - P18: the owner-change hook */
static unsigned long long kBdIsForSaleRva = 0; static AddrReg kBdIsForSaleRva_reg("Building_isForSale", &kBdIsForSaleRva);  /* Steam_1.0.65 0x29C4C0 - P18 fold 1 (item 5): Building vt+0x2C0, hooked */
static unsigned long long kBdSaleValueRva = 0; static AddrReg kBdSaleValueRva_reg("Building_calculateSaleValue", &kBdSaleValueRva);  /* Steam_1.0.65 0x7AC760 - the price the buy callback charges (buyhouse prints it) */
static unsigned long long kOwnRemoveRva = 0; static AddrReg kOwnRemoveRva_reg("Ownerships_removeOwnedObject", &kOwnRemoveRva);  /* Steam_1.0.65 0x7EB550 - P18 fold 1 (item 2) */
static unsigned long long kOwnAddRva = 0; static AddrReg kOwnAddRva_reg("Ownerships_addOwnedObject", &kOwnAddRva);  /* Steam_1.0.65 0x7EBF60 - P18 fold 1 (item 2) */
static unsigned long long kFacHomedInRva = 0; static AddrReg kFacHomedInRva_reg("Faction_platoonsHomedIn", &kFacHomedInRva);  /* Steam_1.0.65 0x6BA220 - P18 fold 1 (item 2): the buy callback's resident list */
static unsigned long long kPlatNewHomeRva = 0; static AddrReg kPlatNewHomeRva_reg("Platoon_findNewHome", &kPlatNewHomeRva);  /* Steam_1.0.65 0x99D810 - P18 fold 1 (item 2) */
static unsigned long long kBdRefundRva = 0;    static AddrReg kBdRefundRva_reg("DismantleRefund", &kBdRefundRva);            /* Steam_1.0.65 0x29DBD0 - build1-d: counted only */
static unsigned long long kBdOrderRva = 0;     static AddrReg kBdOrderRva_reg("AddOrderSelectedCharacters", &kBdOrderRva);   /* Steam_1.0.65 0x7F9280 - house1b: the order gate */
static unsigned long long kBdJobRva = 0;       static AddrReg kBdJobRva_reg("AddJobSelectedCharacters", &kBdJobRva);       /* Steam_1.0.65 0x7F4EF0 - house2: the Shift-job gate */

namespace {

typedef void  (*BdCommitFn)(void* previewList);
typedef void* (*BdCreateFn)(void* factory, void* data, void* pos, void* town, void* owner, void* rot, void* cb,
                            void* furnitureOf, void* isDoorOf, void* save, void* isIndoorsOf, bool invisible, bool completed,
                            bool isFoliage, int floor, bool outsideFurniture);
typedef void  (*BdProgressFn)(void* b, float amount);
typedef unsigned long long (*BdDismantleFn)(void* b, float amount);   /* bool in al; the whole rax is forwarded */
typedef void  (*BdDestroyFn)(void* b, bool destroyed);
typedef char* (*BdStateFn)(void* b);                                  /* vt+0x228 getBuildState */
typedef void* (*BdGetPtrFn)(void* b);                                 /* vt+0x58 getOwnerFaction */
typedef void  (*BdSetFactionFn)(void* b, void* f, void* platoon);     /* vt+0xA0 setFaction(f, 0) */
typedef void  (*BdVoidFn)(void* b);                                   /* vt+0x2D8 setupMiningResourceLevel */
typedef void* (*BdGdcGetDataFn)(void* container, const std::string* sid);
typedef void  (*BdSetBoolFn)(void* b, bool v);                        /* build1-c: vt+0x310 setBroken */
typedef void  (*BdRefundFn)(void* b);                                  /* build1-d: 0x29DBD0 (decompile: void(Building*)) */
typedef void* (*BdLayoutSubFn)(void* layout);                          /* build1-e: Layout vt+0x40 (4d6810:177) */
typedef void* (*BdOrientFn)(void* b, float* out);                      /* build1-e: Building vt+0xC0 -> Quaternion* (4d6810:199) */
/* house1b: 0x7F9280 PlayerInterface::addOrderSelectedCharacters(Building* indoors, TaskType, RootObject* subject, bool shift, bool addDontClear,
   const Vector3& location) - void; every argument an integer-class slot, forwarded whole */
typedef void  (*BdOrderFn)(void* pi, void* indoors, void* task, void* subject, void* shift, void* addDontClear, void* location);
/* house2: 0x7F4EF0 PlayerInterface::addJobSelectedCharacters(TaskType task, RootObject* subject, bool shift, bool add, const Vector3& location)
   - void (7f4ef0 entry: edx task, r8 subject, r9b shift, [rsp+28] add, [rsp+30] location); every argument forwarded whole */
typedef void  (*BdJobFn)(void* pi, void* task, void* subject, void* shift, void* add, void* location);

const size_t kVtFaction = 0x58, kVtSetFaction = 0xA0, kVtState = 0x228, kVtProgress = 0x230, kVtDismantle = 0x248, kVtMining = 0x2D8;
const size_t kVtSetBroken = 0x310;     /* build1-c: setBroken(bool), as 558b10:116 calls it (answers-build1b R2) */
const size_t kVtSetDestroyed = 0x340;  /* P15: setDestroyed(bool) 0x5567F0 (558b10:86 calls it through this slot) */
/* build1-c: Building::ConstructionState (the pointer vt+0x228 returns; ANSWERS build1):
   +0 complete, +2 isDismantled, +4 progress, lektor<BuildMaterial*> mats at +0x10 = {allocator 8, u32 count +0x18,
   u32 maxSize +0x1C, BuildMaterial** +0x20}, +0x28 needed. BuildMaterial: GameData* +0, f32 total +8, f32 delivered +0xC. */
const size_t kStMatCount = 0x18, kStMatArray = 0x20, kMatDelivered = 0xC;
const unsigned int kStMatSane = 1024;  /* a count above this is not a materials list */
const size_t kObjData = 0x40;          /* RootObjectBase::data (GameData*) */
const size_t kGdName = 0x28, kGdType = 0x50, kGdStringId = 0x58;   /* GameData (game/gamelayout.h) */
const size_t kBDestroyed = 0x1A1;      /* Building "destroyed" (serialise 0x551BA0) */
const size_t kBOwnDismantled = 0x162;  /* review-build1d D2: Building isDismantled, the piece's OWN byte (0x2A2820 tests it) */
const size_t kWorldFactory = 0x4A0;    /* GameWorld::objectFactory (RootObjectFactory*) */
const size_t kBInterior = 0x1F0;       /* build1-e: Building::myInterior (items.cpp kBldMyInterior) */
const size_t kInteriorLayout = 0x30;   /* build1-e: the interior's layout = the commit's furnitureOf (answers-build1b R1) */
const size_t kLayoutOwner = 0x90;      /* build1-e: Layout -> its building = createBuilding's isIndoorsOf override (57c1e0:232) */
const size_t kObjPos = 0x48;           /* build1-e: RootObjectBase position x,y,z - the plain field getPosition copies (items.h podPos) */
const size_t kVtLayoutSub = 0x40;      /* build1-e: the Layout virtual the commit's other furniture branch calls */
const size_t kVtOrient = 0xC0;         /* build1-e: the host orientation the commit inverts for the local frame */
const int    kBdInteriorCap = 512;     /* build1-e: interior pieces looked at per host search */
const int    kBdHostMissing = 6;       /* build1-e: a furniture row's host does not resolve here (a verdict beside items.h kObjKey*) */
const int    kLineCap = 60;            /* [BUILD] placed / dismantled / destroyed / PLACE / copy lines per run */
const int    kProgLineCap = 20;        /* review-build1a B5: progress lines have their own cap, so they never starve the lines above */
const int    kQueueCap = 512;          /* build1-b (T321 queueDropped 1141): headroom for an area load in play; see detour_progress */
const int    kPendCap = 4096;          /* build1-c (review-build1b M1): PLACEs waiting for their copy, both lists together; STATEs held */
const int    kCopyTries = 3;           /* build1-b: createBuilding null returns before a PLACE is given up */
const unsigned int kRetryGap = 30;     /* build1-c (review-build1b notes 1, 6): safe points between retries / area re-checks */
const int    kStateLineCap = 40;       /* build1-c: -> STATE / <- STATE lines have their own cap */
const int    kStateApplyPerDrain = 8;  /* build1-c: STATEs handled per safe point */
const int    kRegCap = 8192;          /* review-build1c M3: own pieces and copies share it; 512 capped a large base */
const int    kKeyCap = 64;
const int    kDestroyDeferTries = 10;  /* P15 fold 1 (LOW 4): safe points a STATE is kept while the engine defers setDestroyed (message 16) */
const int    kRemoveTries = 10;        /* build1-d: vt+0x248 calls on one copy before its REMOVE is given up (walls defer) */
const int    kRemoveLineCap = 40;      /* build1-d: -> REMOVE / <- REMOVE lines have their own cap */
const int    kRemovePerDrain = 8;      /* build1-d: REMOVEs handled per safe point */
const DWORD  kRemovedKeepMs = 60000;   /* review-build1d D5: a removed row is erased this long after its removal (late-STATE guard) */
const float  kRemoveRetryAmount = 1.0e30f;   /* build1-d: a retry's amount - finite, so FLT_MAX + it stays FLT_MAX (no +inf in a wall's state+8) */
const int    kRosterPerTick = 32;      /* build1-f: roster pieces (a PLACE and a STATE each) per BuildTick; the rest on the next ticks */
const int    kRosterLineCap = 40;      /* review-build1f: ROSTER / scan lines have their own cap, so they never eat the placed / copy lines */
const size_t kBImADoor = 0x1A0;        /* build1-f: Building::imADoor - a door a building made for itself */
const size_t kBFurnitureOf = 0x238;    /* build1-f: Building::isFurnitureOf (Layout*) - layout furniture (items.cpp kBldFurnitureLayout) */

// PROBE-START: P082
BdCommitFn    orig_commit = 0;
BdCreateFn    orig_create = 0;
BdProgressFn  orig_progress = 0;
BdDismantleFn orig_dismantle = 0;
BdDestroyFn   orig_destroy = 0;
int g_hkCommit = 0, g_hkCreate = 0, g_hkProgress = 0, g_hkDismantle = 0, g_hkDestroy = 0;
/* P18 (parity P18): a building's owner CHANGE. Building::setFaction (vt+0xA0, 0x556EC0) is hooked; a change TO this game's player faction
   that the mod did not write (a house bought in town; the `ownbuilding` lever) queues the building's key, and BdOwnerChangeDrain registers
   it as this game's own piece exactly as the zone scan does (BdScanPiece), so its PLACE + STATE go out on the existing building road and
   the other game ADOPTS its copy of the building through THE ONE OWNER RULE (BdCopyOne: the engine's own setFaction to this player's
   stand-in). Rules: coopbuild::BuildOwnerChangeAct / BuildOwnerDrainAct / BuildOwnerAdoptFromWorld (offline tests). No new kind, no protocol change. */
BdSetFactionFn orig_setfaction = 0;
int g_hkOwner = 0;        /* P18: 1 installed, -1 failed, -2 no address row */
int g_bdOwnerWrite = 0;   /* P18, MAIN THREAD: > 0 while the mod's own setFaction runs (BdSetFactionPod) - THE LOOP REFUSAL */
int g_ocDepth = 0;        /* P18, MAIN THREAD: setFaction nesting (the engine recurses over a building's doors and interior) */
struct BdOcEnt { std::string key; std::string before; int tries; };
std::vector<BdOcEnt> g_ocQ;   /* P18, MAIN THREAD: owner changes to this game's player waiting for the tick */
const size_t kOcQueueCap = 64;
const int kOcMaxTries = 20;
const long long kOcLineCap = 40;
long long g_ocOffMain = 0, g_ocSeen = 0, g_ocQueued = 0, g_ocModWrite = 0, g_ocNested = 0, g_ocNotMine = 0, g_ocWasMine = 0, g_ocNoKey = 0;
long long g_ocQueueFull = 0, g_ocDup = 0, g_ocRegistered = 0, g_ocNotTaken = 0, g_ocKnown = 0, g_ocCopyRow = 0, g_ocNotMineNow = 0, g_ocGaveUp = 0;
long long g_ocWalkMoved = 0, g_ocApplied = 0, g_ocApplyFaulted = 0, g_ocLines = 0, g_ocSuppressed = 0;
/* P18 fold 1 */
long long g_ocDepthReset = 0, g_ocSkippedNotBuilding = 0, g_ocAdoptOther = 0, g_ocApplySkip = 0, g_ocBuyLever = 0, g_ocBuyRefused = 0;
long long g_ocFxTownOut = 0, g_ocFxTownIn = 0, g_ocFxDoorsOpened = 0, g_ocFxInterior = 0, g_ocFxResidents = 0, g_ocFxNewHomes = 0;
long long g_ocFxResSkipped = 0, g_ocFxFaulted = 0, g_ocFxStepsSkipped = 0;
volatile long long g_ocNotForSale = 0;   /* ANY THREAD (InterlockedIncrement64): isForSale refused for a stand-in's house */
std::string g_ocLastKey;                 /* MAIN THREAD: the last house bought (lever) or applied here - `buyhouse show last` */
typedef bool (*BdIsForSaleFn)(void* b);
BdIsForSaleFn orig_isforsale = 0;
int g_hkForSale = 0;      /* P18 fold 1: 1 installed, -1 failed, -2 no address row */
/* P18 fold 2 (P19 review): the town rebuild's saved-building drop - ZoneMapContent::_activate (0x9FEC00) calls it with the zone's
   "0-buildinglist" record and the town being rebuilt (build/decomp_9fd470.txt). Hooked: a saved house another player owns is held. */
static unsigned long long kBdRebuildDropRva = 0; static AddrReg kBdRebuildDropRva_reg("ZoneMapContent_dropTownBuildings", &kBdRebuildDropRva);  /* Steam_1.0.65 0x9FD470 */
static unsigned long long kBdRbGetTypedRva = 0; static AddrReg kBdRbGetTypedRva_reg("GdcGetDataTyped", &kBdRbGetTypedRva);  /* Steam_1.0.65 0x6BFB10 - the engine's own state lookup in 0x9FD470 */
static unsigned long long kBdRbSdataRva = 0; static AddrReg kBdRbSdataRva_reg("StringFieldIndex", &kBdRbSdataRva);  /* Steam_1.0.65 0x6D150 - the engine's own "owner faction ID" read in 0x9FD470 */
typedef void (*BdRebuildDropFn)(void* list, void* town);
BdRebuildDropFn orig_rebuilddrop = 0;
int g_hkRebuild = 0;      /* P18 fold 2: 1 installed, -1 failed, -2 no address row */
volatile long long g_rbCalls = 0, g_rbStates = 0, g_rbHeld = 0, g_rbOverflow = 0, g_rbFaults = 0;   /* ANY THREAD (Interlocked): the zone loader calls the drop */
const size_t kBInteriorObj = 0xF8;     /* P18 fold 1: Building::isAnInteriorObject */
const size_t kBResidentSquad = 0xD0;   /* P18 fold 1: Building::residentSquad (a hand: +0x8 its type, 0xB = none) - the buy callback clears it */
const size_t kFacOwnerships = 0x80;    /* P18 fold 1: Faction::factionOwnerships (Ownerships*) */
const size_t kRootHandle = 0x58;       /* P18 fold 1: RootObjectBase::handle - the hand the buy callback passes (param_1 + 0xb) */
const size_t kRootOwner = 0x10;        /* P18 fold 1: RootObjectBase::owner (Faction*) - the buy callback's param_1[2] */
void BdLogOc(const char* line)   /* P18: its own cap */
{
    if (g_ocLines < kOcLineCap) { ++g_ocLines; DebugLog(std::string(line)); }
    else ++g_ocSuppressed;
}
int g_installed = 0;

long long g_commitSeen = 0, g_placedSeen = 0, g_progressSeen = 0, g_dismantleSeen = 0, g_destroySeen = 0;
long long g_dismantleOffMain = 0, g_destroyOffMain = 0, g_commitOffMain = 0, g_commitNewDropped = 0;
long long g_queued = 0, g_queueDropped = 0;
// PROBE-END: P082
long long g_testArmed = 0, g_testPlaced = 0, g_testPlaceFailed = 0, g_zoneFreshRefused = 0, g_ownerFixed = 0;
long long g_testProgress = 0, g_testDismantle = 0, g_testNoEntry = 0, g_testNotLoaded = 0, g_testBlocked = 0, g_testFaulted = 0;
long long g_lines = 0, g_linesSuppressed = 0, g_keyFailed = 0, g_regFull = 0;
long long g_progLines = 0, g_progSuppressed = 0, g_loadBurstSkipped = 0, g_destroyNoChange = 0;
/* build1-b */
long long g_placeSent = 0, g_placeRecv = 0, g_copyCreated = 0, g_copyAdopted = 0, g_copyDup = 0, g_copyKeyMismatch = 0;
long long g_copyFailed = 0, g_copyOwnerFixed = 0, g_pendingDropped = 0, g_placeRecvBad = 0, g_copyZoneFresh = 0;
long long g_placeLinkDown = 0, g_placeFurniture = 0, g_placeNoPos = 0, g_placeUnencodable = 0;
long long g_copyWaitNoPeer = 0, g_copyWaitNotLoaded = 0, g_copyBlocked = 0;
/* build1-c */
long long g_stateSent = 0, g_stateRecv = 0, g_stateApplied = 0, g_completeApplied = 0, g_stateNoCopy = 0, g_stateStale = 0;
long long g_stateLinkDown = 0, g_stateNoPlace = 0, g_stateUnencodable = 0, g_stateWaitArea = 0, g_stateFaulted = 0, g_stateDropped = 0;
long long g_matsCountDiff = 0, g_stateLines = 0, g_stateSuppressed = 0, g_sidSubstituted = 0, g_copyAdoptAfterNull = 0;
long long g_copyOffMap = 0, g_waitResumed = 0, g_forgotWorlds = 0;
unsigned int g_drainNo = 0;   /* MAIN THREAD: safe points seen (BuildCopyDrain) - the retry clock */
int g_hostLookups = 0;         /* review-build1e D2: furniture host resolves this safe point (each walks every loaded zone) */
const int kHostLookupsPerDrain = 4;
/* build1-d */
long long g_removeSent = 0, g_removeRecv = 0, g_copyRemoved = 0, g_removeRetried = 0, g_removeNoCopy = 0, g_removePendingDropped = 0;
long long g_removeLinkDown = 0, g_removeNoPlace = 0, g_removeWall = 0, g_removeAlready = 0, g_removeWaitArea = 0, g_removeGaveUp = 0;
long long g_removeFaulted = 0, g_matsZeroed = 0, g_stateWall = 0, g_removeLines = 0, g_removeSuppressed = 0, g_stateAfterRemove = 0;
long long g_removeDestroyedIgnored = 0, g_removeAlreadyGone = 0, g_removedRowsErased = 0;   /* review-build1d fold */
long long g_destroySent = 0, g_repairSent = 0, g_destroyApplied = 0, g_repairApplied = 0, g_destroyAlready = 0, g_destroyPosted = 0,
          g_destroyFaulted = 0, g_destroyWaitRemove = 0, g_wholeSeen = 0, g_copyDestroyReverted = 0, g_copyDestroyWait = 0,
          g_repairOrderHelp = 0, g_repairRefused = 0, g_repairHelpReset = 0, g_testDestroy = 0;   /* P15 */
long long g_testAlreadyRuin = 0, g_destroyDeferRetried = 0, g_destroyDeferGaveUp = 0, g_repairDeferred = 0, g_copyWallRuinKept = 0;   /* P15 fold 1 */
/* build1-e */
long long g_hostPending = 0, g_hostResolved = 0, g_hostFailed = 0, g_hostKeyFailed = 0, g_removeUnclean = 0;
long long g_testInto = 0, g_testIntoFailed = 0;
long long g_houseHanded = 0, g_houseOwnerUnresolved = 0, g_houseTown = 0, g_handedRecv = 0, g_progressOnHandedCopy = 0, g_handedWaitSlot = 0;   /* build1h H1 */
/* house1b (review-house1) */
long long g_orderSkipped = 0, g_orderKept = 0, g_orderBlocked = 0, g_owedMade = 0, g_owedResent = 0, g_owedResendLinkDown = 0;
long long g_ackSent = 0, g_ackRecv = 0, g_ackNoOwed = 0, g_owedLoaded = 0, g_owedSaveFailed = 0, g_owedRemoved = 0;
long long g_standInFromRecord = 0, g_handedNoStandIn = 0, g_dupRecorded = 0, g_liveAliasEvents = 0, g_handPreflightFailed = 0;
BdOrderFn orig_order = 0;
int g_hkOrder = 0;
BdJobFn orig_job = 0;   /* house2 */
int g_hkJob = 0;
long long g_jobBlocked = 0, g_jobKept = 0, g_lateRevivedRefused = 0, g_owedSaveRefused = 0, g_owedLoadUnread = 0;
long long g_refundSeen = 0, g_refundOffMain = 0;              /* 0x29DBD0 calls, any thread (interlocked) */
long long g_refundCalledOnCopy = 0, g_refundOnCopy = 0;       /* MAIN THREAD: on a coop-peer piece; of those, with delivered mats > 0 (a drop) */
BdRefundFn orig_refund = 0;
int g_hkRefund = 0;
/* build1-f ROSTER (MAIN THREAD) */
long long g_rosterSent = 0, g_rosterStateSent = 0, g_rosterNotLive = 0, g_rosterNoPos = 0, g_rosterLinkDown = 0, g_rosterGone = 0;
long long g_rosterNotMine = 0, g_rosterRounds = 0, g_rosterUnencodable = 0;
long long g_scanRuns = 0, g_scanFound = 0, g_scanRegistered = 0, g_scanFurnitureSkipped = 0, g_scanInteriorSkipped = 0;
long long g_scanDoorSkipped = 0, g_scanKnown = 0, g_scanNoKey = 0, g_scanNoPos = 0, g_scanDismantled = 0, g_scanTruncated = 0;
long long g_scanWorldProduction = 0;   /* own-faction production buildings with no build record and no materials the scans left unregistered (world-placed) */
long long g_scanPlayerProduction = 0;  /* own-faction production buildings with no build record but with materials the scans registered (player-built) */
long long g_rosterReqSent = 0, g_rosterReqRecv = 0, g_rosterReqLinkDown = 0;   /* review-build1f H2 */
long long g_rosterLines = 0, g_rosterSuppressed = 0, g_scanWalkMoved = 0;       /* review-build1f cap, C1 */
/* T-274 (t274-zone-scan.md S3): the per-zone scan. Sector (sx * 64 + sy) -> 1 = its zone was walked in this world while it stayed
   active (coopbuild::BuildZoneScan*); cleared by BuildForgetWorld. */
unsigned char g_bdZoneScanned[4096];
unsigned char g_bdZoneActiveNow[4096];   /* BdZoneScanTick's scratch: the sectors active this tick */
long long g_zsZones = 0, g_zsFound = 0, g_zsRegistered = 0, g_zsKnown = 0, g_zsFurn = 0, g_zsInner = 0, g_zsSkipped = 0;
long long g_zsWalkMoved = 0, g_zsRefused = 0, g_zsTruncated = 0, g_zsLines = 0, g_zsSuppressed = 0;
const int kZoneScanLineCap = 100;   /* one ZONESCAN line per walked zone (~9-25 per world load); its own cap, not the roster's 40 */
/* T-274 fold (review 2026-09-29) */
unsigned char g_bdZoneMoves[4096];   /* LOW-2: each sector's moved walks in a row (coopbuild::BuildZoneScanDone's give-up count) */
int g_bdZoneLastPick = -1;           /* LOW-2: the sector (index) picked last - the next pick starts after it (round-robin) */
long long g_zsGaveUp = 0, g_zsListFull = 0, g_zsKnownAlias = 0, g_scanKnownAlias = 0;   /* LOW-2, LOW-3, MED-1 */
long long g_zsPieceLines = 0, g_zsPieceSuppressed = 0;   /* LOW-5: the per-piece 'ZONESCAN registered' lines' own cap */
const int kZoneScanPieceLineCap = 40;
int g_rosterOwed = 0, g_rosterActive = 0, g_rosterLinkWasUp = 0;
int g_bdRosterTo = -1;   /* M11 C2: the one player the owed / active roster round goes to (a world-server arrival); -1 = every game, as before */
int g_rosterReqDone = 0;   /* review-build1f H2: this world + link has sent its ROSTER_REQUEST */
std::vector<std::string> g_rosterQ;   /* this round's keys, in registry order */
size_t g_rosterAt = 0;                /* the next one to send */
long long g_rdSent = 0, g_rdNotLive = 0, g_rdScanFound = 0, g_rdScanReg = 0, g_rdFurn = 0, g_rdInner = 0;   /* this round's */
int g_rdTrunc = 0;

struct BdEvent
{
    int kind;            /* 1 progress, 2 dismantle, 3 destroy */
    int keyOk, stateOk, onMain;
    float amount, progress, needed;
    int complete, dismantled, destroyed;
    int dismBefore;      /* build1-d: isDismantled read BEFORE the call (-1 not read) - a REMOVE needs a real 0 -> 1 flip */
    int nMats;           /* build1-c: delivered mats read after the call (-1 unreadable) */
    float mats[coopbuild::kBuildMaxMats];
    char key[kKeyCap];
};
BdEvent g_q[kQueueCap];
int g_qn = 0;
CRITICAL_SECTION g_qcs;
volatile LONG g_qInit = 0;
BdEvent g_drain[kQueueCap];   /* MAIN THREAD: BuildTick's copy */

long g_bdGen = 0;   /* mmo8a2 (review-mmo8a H1), MAIN THREAD: the world generation, bumped by BuildForgetWorld; a row carries the one it was registered or last touched live in, and pp.build takes only this one's rows */
struct BdReg
{
    std::string sid, owner;
    float progress, needed;
    int complete, dismantled, destroyed, via;
    int own;                                     /* build1-c: THIS game's player faction owned it at registration (a STATE source) */
    int nMats;                                   /* build1-c: delivered mats as last read (-1 unreadable) */
    float mats[coopbuild::kBuildMaxMats];
    int sentOk, sentComplete, sentN;             /* build1-c: what the other game was last sent (the PLACE, then each STATE) */
    float sentProgress;
    float sentMats[coopbuild::kBuildMaxMats];
    int removed;                                 /* build1-d: REMOVE sent (own) or the copy removed here - later STATEs are ignored */
    int hasPos;                                  /* review-build1d D3: a copy's PLACE position is known (the REMOVE's zone check) */
    float pos[3];                                /* build1-e: for furniture, its HOST's world position */
    std::string hostKey;                         /* build1-e: furniture - the host building's P7n key (empty: free-standing) */
    std::string liveKey;                         /* build1-e: a copy's own key when it differed from the builder's */
    int hostForm;                                /* build1-e: coopbuild::kBuildHost* */
    int keySame;                                 /* build1-e (F2): 0 = the copy's created key differed from the builder's */
    int placeOk;                                 /* build1-f: an own piece's placement inputs below are known (the roster's PLACE) */
    float placePos[3], placeRot[4];              /* build1-f: as createBuilding received them (furniture: host-local), or read by the scan */
    int floor, outside;                          /* build1-f: furniture's floor / outside flag, as createBuilding received them */
    int ownerSlot;                               /* build1h: the owner slot the PLACE named (-1 not recorded; an own row answers MySlotForWire) */
    int handed;                                  /* build1h: 1 = placed here and handed to the house owner (a copy row), 2 = handed to THIS game (own), 3 = to a third slot */
    long gen;                                    /* mmo8a2 (H1): g_bdGen when registered or last touched live (BuildOwnRows writes only the current one) */
    unsigned int nonce;                          /* house2 fold: the placement's nonce (drawn by the placer; a copy / handed row keeps the PLACE's; 0 = unknown) */
    int helpBaseOk;                              /* help1: a copy's delivered amounts after the mod's own last write (helpBase) are known */
    float helpBase[coopbuild::kBuildMaxMats];    /* help1: ... - the helper's deliveries are measured against them, never the mod's writes */
    std::vector<coopbuild::BuildHelpAck> helpAcks;   /* help1: an own piece - per helper slot the last HELP_WORK seq applied and the refused materials */
    int helpForce;                               /* help1: an own piece - the next STATE goes now (it carries a confirmation) */
    std::vector<coopbuild::BuildHelpAck> helpAcksLanded;    /* help1 fold (review MED 4): an own piece - the confirmation rows pp.build holds ON DISK (what a STATE's tail carries) */
    std::vector<coopbuild::BuildHelpAck> helpAcksWriting;   /* help1 fold: the rows captured into the pp.build record last handed to the writer */
    int helpAckDirty, helpAckWait;                          /* help1 fold: helpAcks changed since the last capture; 1 captured / 2 its record seq known / 3 no record keeps the row */
    unsigned long long helpAckSeq;                          /* help1 fold: the store seq of that record */
    DWORD helpAckAt;                                        /* help1 fold: when its seq became known */
    int helpTailKnown;                                      /* help1 fold (review MED 5/6): a copy - the owner's STATE tail was read this session ... */
    unsigned int helpTailSeq;                               /* ... and the last seq it says it applied from this game (0 = none) */
    unsigned int helpTailLive;                              /* help1 fold 3 (finding 1): ... and its LIVE last applied (>= helpTailSeq) - new entries go past it */
    int stateAnnounce;                                      /* help1 fold 5 (T-269): an own piece - its first real STATE after the PLACE is owed */
    int sentDestroyed;                                      /* P15: an own piece - the destroyed state the other game was last sent */
    int ownerKnown;                                         /* P15: a copy - ownerLast holds the owner's last STATE applied here */
    coopbuild::BuildMsg ownerLast;                          /* P15: ... put back when this game's engine makes the copy a ruin */
    BdReg() : progress(0.0f), needed(0.0f), complete(0), dismantled(0), destroyed(0), via(0), own(0), nMats(0), sentOk(0),
              sentComplete(0), sentN(0), sentProgress(0.0f), removed(0), hasPos(0), hostForm(0), keySame(1), placeOk(0), floor(0), outside(0), ownerSlot(-1), handed(0),
              nonce(0), gen(g_bdGen), helpBaseOk(0), helpForce(0), helpAckDirty(0), helpAckWait(0), helpAckSeq(0), helpAckAt(0),
              helpTailKnown(0), helpTailSeq(0), helpTailLive(0), stateAnnounce(0), sentDestroyed(0), ownerKnown(0)
    {
        for (unsigned int i = 0; i < coopbuild::kBuildMaxMats; ++i) { mats[i] = 0.0f; sentMats[i] = 0.0f; helpBase[i] = 0.0f; }
        pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f;
        for (int i = 0; i < 3; ++i) placePos[i] = 0.0f;
        placeRot[0] = 1.0f; placeRot[1] = 0.0f; placeRot[2] = 0.0f; placeRot[3] = 0.0f;
    }
};
std::map<std::string, BdReg> g_reg;   /* MAIN THREAD */
std::map<std::string, std::string> g_liveAlias;   /* build1h, MAIN THREAD: a copy's own created key -> the builder's key (BuildRecordedOwnerSlot) */
/* house1b (review-house1 items 2, 4), MAIN THREAD: hand-offs this game OWES a house owner - the PLACE exactly as sent (ownerSlot = the
   owner's slot). Kept outside the registry (BuildForgetWorld does not touch it) and in the placer's own file in the world folder
   (build_handoffs_owed_<player key>.txt), re-sent at every roster round (link-up, roster request, world load) until the owner's
   HAND_ACK. An acknowledged row stays (acked=1) so the placer's build / dismantle orders on the piece stay refused; the owner's
   REMOVE ends it. */
struct BdOwed
{
    coopbuild::BuildMsg m;
    int acked;
    BdOwed() : acked(0) {}
};
std::map<std::string, BdOwed> g_owed;
std::string g_owedFor;   /* the file the map was loaded from ("" = not yet) */
std::string g_owedTried;   /* house2 fold (MED1): the last file a load was attempted for - the map is cleared only when the path differs */
std::set<std::string> g_owedErasedUnread;   /* house2 fold 2: keys a REMOVE ended while that file was unread - never merged back */
/* ---- help1 (owner 172, .modding/investigations/p51-help1-design.md): a partner helps build ANOTHER player's piece ------------------
   THE HELPER'S GAME (its copy, via=3, owned by the owner's stand-in): detour_progress records what each engine call on the copy did
   (progress before / after; the delivered materials against the copy's baseline) - never the mod's own writes (g_bdSelf) - and caps the
   copy below complete; BdHelpTick turns the recorded work into HELP_WORK messages (MSG_BUILD kind 8), kept in g_help until the owner's
   STATE confirms their seq, in a crash-safe per-world file, re-sent at link-up. THE OWNER'S GAME: BdApplyHelp (the K2 safe point)
   applies each to the real piece and forces a STATE whose tail confirms the seq and carries the refused materials back. MAIN THREAD. */
typedef std::pair<std::string, unsigned int> BdPlKey;   /* help1 fold 3: (the owner's key, the placement nonce) */
struct BdHelpRow
{
    unsigned int nextSeq;
    float pendP;                                  /* recorded, not yet in an entry */
    int pendN, pendReset, pendAny;
    float pendM[coopbuild::kBuildMaxMats];
    DWORD pendSince;
    float need;                                   /* the copy's needed at the last record (the 1% send step) */
    int ownerSlot;                                /* the owner's slot (the copy row's) - for the line */
    std::vector<coopbuild::BuildHelpEntry> owed;  /* entries not yet confirmed, sent or not */
    int refunded[coopbuild::kBuildMaxMats];       /* items handed back so far (whole units of the owner's cumulative refused amounts) */
    std::string sid[coopbuild::kBuildMaxMats];    /* help1 fold (MED 3): each material's item sid, read when delivered (a HELP_GONE refund names it) */
    float pos[3];                                 /* help1 fold (MED 3): the copy's position at the last record (a refund's receiver) */
    int posOk, waitLogged;                        /* help1 fold: pos known; the 'waits for the owner's STATE' line went */
    unsigned int nonce;                           /* help1 fold 2 (re-check HIGH 1): the owner's placement this row helped (the copy's PLACE nonce; 0 = unknown); fold 3: every entry carries it too */
    int refundDue;                                /* help1 fold 4 (leftover 3): the owner's last STATE left a refund not handed back (the inventory refused it) - memory only */
    BdHelpRow() : nextSeq(1), pendP(0.0f), pendN(0), pendReset(0), pendAny(0), pendSince(0), need(0.0f), ownerSlot(-1), posOk(0), waitLogged(0), nonce(0), refundDue(0)
    {
        for (unsigned int i = 0; i < coopbuild::kBuildMaxMats; ++i) { pendM[i] = 0.0f; refunded[i] = 0; }
        pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f;
    }
};
std::map<std::string, BdHelpRow> g_help;          /* the helper's rows by the owner's key; kept across a world teardown (the file is per world) */
std::map<BdPlKey, BdHelpRow> g_helpOld;           /* help1 fold 3 (findings 2, 3), the helper's: rows of a placement the owner replaced at its key, by (key, nonce) - their
                                                     entries stay owed under their own nonce, sent on, until the owner's HELP_GONE for that placement settles them (O lines) */
std::string g_helpFor, g_helpTried;               /* the file the map was loaded from / last tried (the BdOwedLoad rule: never saved unread) */
struct BdHelpIn { coopbuild::BuildMsg m; int slot; unsigned int nextTry; int waitLogged; int heldLogged; };
std::vector<BdHelpIn> g_helpIn;                   /* the owner's: HELP_WORK received, applied at the K2 safe point in arrival order */
struct BdAckRestore { unsigned int nonce; std::vector<coopbuild::BuildHelpAck> acks; BdAckRestore() : nonce(0) {} };
std::map<std::string, BdAckRestore> g_helpAckRestore;   /* the owner's: confirmation rows pp.build brought back; help1 fold 3 (finding 2): with the nonce of their row's PLACE - used only for that placement */
std::map<std::string, unsigned int> g_placeNonceRestore;   /* help1 fold 4 (re-check #1), the owner's: each own key's placement nonce as pp.build's PLACE record holds it */
long long g_helpNonceRestored = 0, g_helpNonceHeld = 0, g_helpGoneUnproven = 0, g_bdCopyCollide = 0, g_helpGoneCarried = 0, g_helpOldProbe = 0, g_helpAckKeptUnknown = 0;   /* help1 fold 4 */
int g_helpLinkWasUp = 0;
int g_bdSelf = 0;   /* > 0 while the mod's own K2 writes run (copies made, STATEs, REMOVEs, help applied) - vt+0x230 inside them is never help */
long long g_helpWorkSent = 0, g_helpWorkOwed = 0, g_helpWorkRecv = 0, g_helpApplied = 0, g_helpDup = 0, g_helpDropRemoved = 0;
long long g_helpDropComplete = 0, g_helpMatsClamped = 0, g_helpCopyCapped = 0, g_orderOnCopy = 0, g_helpPredicted = 0;
long long g_helpRefundItems = 0, g_helpRefundFailed = 0, g_helpWaitArea = 0, g_helpRecvDropped = 0, g_helpSaveFailed = 0, g_helpFaulted = 0;
long long g_helpResent = 0, g_helpConfirmed = 0, g_helpLines = 0, g_helpSuppressed = 0, g_helpLoadUnread = 0, g_testMats = 0;
const int   kHelpOwedCap = 64;        /* unconfirmed entries per piece; past it the work waits in the row */
const DWORD kHelpSendMs = 1000;       /* recorded work goes out at least this often ... */
const float kHelpSendStep = 0.01f;    /* ... or at each 1% of needed, whichever first */
const DWORD kHelpResendMs = 30000;    /* an entry sent and not confirmed this long goes again (the owner drops a duplicate and confirms it) */
const int   kHelpLineCap = 400;       /* help1 fold (review LOW 13): the oracle lines (applied / dup / held / refund / dropped / gone / confirmed) up to this; the frequent ones apart (BdLogHelpFreq) */
const DWORD kHelpSaveMs = 5000;       /* help1 fold (review MED 8): the owed file is saved at most this often (and at link-down, world teardown, quit; at once after a refund) */
const DWORD kHelpAckStuckMs = 15000;  /* help1 fold (review MED 4): a confirmation whose pp.build record has not landed this long is written again */
long long g_helpHeld = 0, g_helpWallRefused = 0, g_helpGoneSent = 0, g_helpGoneRecv = 0, g_helpGoneItems = 0, g_helpRecvTwice = 0;
long long g_helpAckLanded = 0, g_helpAckStuck = 0, g_helpWaitTail = 0, g_neededHealed = 0, g_helpSendLines = 0;
int g_helpSaveDirty = 0;                          /* help1 fold (MED 8): the owed file has changes not yet saved */
DWORD g_helpSaveAt = 0;
std::set<std::string> g_helpAckAwait;             /* help1 fold (MED 4), the owner's: own keys whose confirmation waits for pp.build to land */
/* help1 fold (MED 3), the owner's: own keys removed here whose row the sweep erased (this world). fold 2 (re-check HIGH 2, MED 3): with
   each helper slot's last applied seq (a HELP_GONE carries it), kept in pp.build as a gone marker and read back at load.
   help1 fold 3 (findings 2, 3, 5): a DURABLE record per (key, placement nonce) of what this game applied and refused from each helper
   slot for a placement that is no longer its current piece at the key (removed, dismantled, handed, swept, placed over) - a gone marker
   row in pp.build (BuildGoneRowKey), read back at load. Work carrying that nonce is answered HELP_GONE from it, only once the record
   holding it has landed (dirty == 0 and wait == 0: landed == acks - the SETTLED rule), and is never applied to another placement.
   At most kGoneRecCap records (a record not kept counts goneRecFull). */
struct BdGoneRec
{
    std::vector<coopbuild::BuildHelpAck> acks;      /* per helper slot: last applied seq + cumulative refused amounts */
    std::vector<coopbuild::BuildHelpAck> landed;    /* what pp.build holds on disk (what a HELP_GONE carries) */
    std::vector<coopbuild::BuildHelpAck> writing;   /* captured into the pp.build record last handed to the writer */
    int dirty, wait;                                /* acks changed since the last capture; 1 captured / 2 its store seq known / 3 no record keeps it */
    unsigned long long seq;                         /* the store seq of that record */
    DWORD at;                                       /* when that seq became known */
    BdGoneRec() : dirty(0), wait(0), seq(0), at(0) {}
};
std::map<BdPlKey, BdGoneRec> g_ownGone;
std::set<BdPlKey> g_goneAwait;                      /* help1 fold 3: records whose pp.build row has not landed yet */
std::set<std::string> g_ownSwept;                   /* help1 fold 3: own keys the sweep erased with nothing applied from any helper (no record: GONE applied 0) */
const size_t kGoneRecCap = 4096;   /* help1 fold 4: was 512 */
struct BdHelpGoneIn { std::string key; unsigned int seq; unsigned int applied; int slot; unsigned int nonce; int nEx; float ex[coopbuild::kBuildMaxMats]; };   /* fold 3: + the placement and its refused tail */
long long g_helpGoneOldNonce = 0, g_helpGoneNoRecord = 0, g_goneRecFull = 0, g_goneWaitLand = 0, g_helpMovedOld = 0, g_helpGoneRefused = 0;   /* help1 fold 3 */
long long g_helpResynced = 0, g_helpSuperseded = 0, g_helpUnencodable = 0, g_helpReplaced = 0, g_helpFloorDropped = 0;   /* help1 fold 2 */
long long g_goneRowsKept = 0, g_goneRowsRead = 0, g_goneRowUnkept = 0;   /* help1 fold 2 (MED 3): pp.build gone markers */
volatile LONG g_helpWallRefusedOff = 0;           /* help1 fold 2 (LOW 7): wall-copy orders refused off the main thread (counted, no line) */
std::vector<BdHelpGoneIn> g_helpGoneIn;           /* help1 fold (MED 3), the helper's: HELP_GONE received, handled at the K2 safe point */
std::map<BdPlKey, coopown::HelpRecRow> g_helpRec;   /* help1 fold (MED 5/6), the helper's: pp.help as read for this store + world; fold 3: by (key, placement nonce) */
int g_helpRecState = 0;                           /* help1 fold: pp.help 0 not read yet, 1 read (or none there), 2 unreadable */
int g_helpRecDirty = 0;                           /* help1 fold: 1 a seq moved (batched), 2 items handed back (written now) */
/* a row by the key a live piece reads (a copy's own created key maps to the builder's through g_liveAlias) */
std::map<std::string, BdReg>::iterator BdRowByLiveKey(const std::string& k)
{
    std::map<std::string, BdReg>::iterator it = g_reg.find(k);
    if (it != g_reg.end()) return it;
    std::map<std::string, std::string>::const_iterator a = g_liveAlias.find(k);
    if (a == g_liveAlias.end()) return g_reg.end();
    it = g_reg.find(a->second);
    return (it != g_reg.end() && it->second.liveKey == k) ? it : g_reg.end();
}
/* the owner's confirmation rows for a piece: the row's own, else those pp.build brought back (0 = none); help1 fold 3: for its placement only */
const std::vector<coopbuild::BuildHelpAck>* BdHelpAcksRead(const std::string& key, const BdReg& r)
{
    if (!r.helpAcks.empty()) return &r.helpAcks;
    std::map<std::string, BdAckRestore>::const_iterator it = g_helpAckRestore.find(key);
    return (it != g_helpAckRestore.end() && it->second.nonce == r.nonce) ? &it->second.acks : 0;   /* help1 fold 3: never another placement's */
}
void BdLogHelp(const char* line)
{
    if (g_helpLines < kHelpLineCap) { ++g_helpLines; DebugLog(std::string(line)); }
    else ++g_helpSuppressed;
}
/* help1 fold (review LOW 13): the frequent lines (work sent / owed, copy capped, predicted, waits) - the first 10, then every 30th */
void BdLogHelpFreq(const char* line)
{
    ++g_helpSendLines;
    if (coopbuild::BuildHelpFreqLine(g_helpSendLines)) DebugLog(std::string(line));
    else ++g_helpSuppressed;
}
/* help1 fold (review MED 4): the confirmation rows a STATE carries - those pp.build holds on disk (the landed rows, else those pp.build
   brought back); never rows applied but not yet written */
const std::vector<coopbuild::BuildHelpAck>* BdHelpAcksLandedRead(const std::string& key, const BdReg& r)
{
    if (!r.helpAcksLanded.empty()) return &r.helpAcksLanded;
    std::map<std::string, BdAckRestore>::const_iterator it = g_helpAckRestore.find(key);
    return (it != g_helpAckRestore.end() && it->second.nonce == r.nonce) ? &it->second.acks : 0;   /* help1 fold 3: never another placement's */
}
/* build1-b: PLACE messages waiting for their copy. MAIN THREAD (queued by the session dispatch, drained at the safe point).
   build1-c (review-build1b M1): g_pend = ready to try (a null return waits kRetryGap safe points); g_wait = the area is not
   loaded here, re-checked every kRetryGap safe points. Both together are bounded by kPendCap. */
struct BdPending
{
    coopbuild::BuildMsg m;
    int tries, waitLogged;
    unsigned int nextTry;
    int hostLogged;   /* build1-e: its hostPending line went out */
    int slotWaitCounted;   /* house1b (item 10): handedWaitSlot counts this PLACE once */
    int restore;           /* mmo8a: 1 = a piece from this game's own pp.build record - made as this game's OWN piece, never a copy */
    int rsOutcome;         /* mmo8a: 1 present, 2 recreated, 3 refused, 4 failed (0 when it finishes = failed) */
    int rsHasState;        /* mmo8a: st holds the row's STATE */
    coopbuild::BuildMsg st;
    int fromSlot;          /* M7b slice 4 fold 1 (F6): the PLACE's sender's slot - its HAND_ACK goes back by SLOT (fold 2 D1: -2 = the session peer before its slot was known; -1 none) */
    BdPending() : fromSlot(-1) {}
};
std::vector<BdPending> g_pend;
int g_bdAckSlot = -1;   /* fold 1 (F6): the placer of the PLACE BdCopyOne is working on (-1 outside it) */
long long g_ackNoSlot = 0, g_helpNoOwnerSlot = 0;   /* fold 1 (F6): a HAND_ACK / HELP_WORK held for want of its target's slot */
std::vector<BdPending> g_wait;
/* mmo8a2 (review-mmo8a M2, M4, LOWs) and mmo8a3, MAIN THREAD */
std::map<std::string, long> g_ownRetireOwed;   /* M2: own keys retired here and owed to pp.build (key -> g_bdGen) - the 60 s sweep does not clear it */
std::vector<std::string> g_rsOwePlace;         /* M4: rebuilt pieces whose PLACE + STATE the other game is owed (BdRosterTick) */
int g_rsSearches = 0;                          /* restore searches this safe point */
const int kRsSearchPerDrain = 4;
long long g_rsSearchCapped = 0, g_rsRekeyed = 0, g_rsPlaceSent = 0, g_ownRowsOldWorld = 0;
long long g_rsGateB1Loaded = 0, g_rsGateB1Not = 0, g_rsGateB1Fault = 0;   /* mmo8a3 (A): a restore row's area check - the engine's ZoneMap+0xB1 */
std::vector<std::string> g_rsOweRemove;   /* mmo8a3 (C): a re-keyed restore's OLD key - the other game is owed its REMOVE (before the new key's PLACE) */
long long g_rsOweRemoveSent = 0, g_rsPlaceSkipRound = 0, g_scanNoLink = 0;   /* mmo8a3 (C, D4, B) */
std::vector<std::string> g_dmOweRemove;   /* par13 (parity P13): an own piece dismantled while the link was down - the other game is owed its REMOVE */
bool BdDmOweSendOne(const std::string& key);   /* par13 fold (review-par13 F2): defined beside BdDmOweSend */
long long g_dmOweAdded = 0, g_dmOweSent = 0, g_dmOweForgot = 0;   /* par13 */
int g_scanOwed = 1;   /* mmo8a3 (B): the post-load scan is owed (a world load; the first world of the process) - run with or without the link */
/* mmo8a: pp.build - the dirty mark store.cpp's writer takes (0 none, 1 progress, 2 placed / handed here / removed) and the
   restore's counters; one [BUILD] restore line per row outcome, capped */
int g_ownDirty = 0;
long long g_rsRows = 0, g_rsPresent = 0, g_rsRecreated = 0, g_rsRefused = 0, g_rsFailed = 0, g_rsQueued = 0, g_rsLines = 0, g_rsSuppressed = 0;
long long g_rsPresentRegistered = 0, g_stateAnnounced = 0;   /* help1 fold 5 (T-268 / T-269) */
long long g_placeFromLive = 0, g_placeFromLiveNoGround = 0, g_placeFromLiveRetried = 0;   /* live positions turned into placement inputs; no ground height read; read again by a later scan */
const long long kRsLineCap = 40;
void BdOwnDirty(int level) { if (level > g_ownDirty) g_ownDirty = level; }
/* P87 (to-do P87): pieces dismantled while the other player was away. THE OWNER'S SIDE: every own piece dismantled here is a
   TOMBSTONE of this world, kept in pp.build as a "~t" row (BuildTombRowKey: a REMOVE and an empty STATE) so a quit or a reload does
   not lose it, read back at load (BuildRestoreQueue); at every roster round (the link-up, the other game's ROSTER_REQUEST - its world
   runs -, this game's own world load) each tombstone is owed its REMOVE again, ahead of any PLACE (g_dmOweRemove, BdRosterTick). A
   tombstone ends when this game holds a live own piece at the key again (a new piece there, or a round finding one): the owner's list
   wins. THE RECEIVER'S SIDE (BdReconDrain, the K2 safe point): a REMOVE for a key with no row here is reconciled - the live piece at
   the key, when the sender's stand-in owns it (a copy this game's save brought back, not a copy row), is taken as the sender's copy
   and removed through BdRemoveOne (mats zeroed first: no refund here). Never this game's own piece, never another owner's, never a
   key the owner lists (a row, a pending PLACE). No piece in the loaded zones: parked until a zone finishes loading (BdZoneScanTick). */
std::vector<std::string> g_tomb;         /* this world's own dismantled keys, oldest first */
std::vector<std::string> g_tombRetire;   /* tombstone row keys pp.build drops at its next write */
int g_tombSendOwed = 0;                  /* tombstones read from pp.build - queued at the next tick the world is scanned and the link up */
long long g_tombPersisted = 0, g_tombLoaded = 0, g_tombQueued = 0, g_tombDropListed = 0, g_tombEvicted = 0, g_tombRowsEmitted = 0, g_tombUnkept = 0;
/* P87 fold 1 (H1, H1b, H2): what each tombstone carries besides its key (one entry per g_tomb key). readBack: read from pp.build, the
   dismantle not seen in this world - its REMOVE goes only once VERIFIED (BdTombVerify: the zone holding the key - furniture: its host's
   - was walked by the zone scan in this world and the receiver's own live lookup finds no own piece at the key). A tombstone made in
   this world (the owner saw the dismantle) is sent as before. tail: the pp.build row carried the fold-1 tail; a row written before
   fold 1 does not say whether it is furniture - never verified: kept, never sent. nonce: the dismantled placement's (0 unknown) - on
   every REMOVE, so the receiver keeps a newer placement at the key. sent: a REMOVE for it went once (a repeat says so, L3). A key
   missing here reads as an unverifiable read-back: held. */
struct BdTombInfo
{
    unsigned int nonce;
    int readBack, verified, tail, furn, sent;
    std::string hostKey;
    coopbuild::BuildLedger led;   /* P14 fold 1: the escape ledger (the '~t' row's STATE bytes) */
    BdTombInfo() : nonce(0), readBack(1), verified(0), tail(0), furn(0), sent(0) {}
};
std::map<std::string, BdTombInfo> g_tombInfo;
int g_tombVerifyOwed = 0;   /* a zone was walked: the read-back tombstones are looked at again (BdRosterTick) */
/* P87 fold 2 (LOW 4): g_tombHeld is a SNAPSHOT - the held count of the last BdTombQueue pass, not a running total */
long long g_tombVerified = 0, g_tombHeld = 0, g_tombResent = 0, g_tombScanDropped = 0, g_tombUnowed = 0, g_tombNotShared = 0, g_scanRevived = 0;
/* P87 fold 2 (LOW 3): the zone (sector index) the zone scan walked last - its read-back tombstones are looked up first; the others
   round-robin from g_tombVerifyCursor, at most kTombVerifyPerCall live lookups per BdTombQueue call */
int g_tombVerifyZone = -1;
size_t g_tombVerifyCursor = 0;
const int kTombVerifyPerCall = 8;
long long g_tombNearHeld = 0, g_tombVerifyDeferred = 0, g_tombNotLoaded = 0;   /* P87 fold 2 */
long long g_removeNonceSkipped = 0, g_removeResendNoCopy = 0, g_reconLinkUnknown = 0;
struct BdRecon
{
    unsigned int nextTry;
    int parked, rearms, tries;
    int fromSlot;   /* the player whose list no longer holds the key (the REMOVE's sender, or a copy-list row's slot): its stand-in is the one a returning copy carries */
    BdRecon() : nextTry(0), parked(0), rearms(0), tries(0), fromSlot(-1) {}
};
std::map<std::string, BdRecon> g_recon;   /* the receiver's REMOVEs for keys with no row here */
long long g_ownerRecordAdopt = 0, g_ownerRecordRecon = 0, g_ownerRecordRemove = 0, g_ownerFixFailed = 0;   /* P87 fold 3: a save's stand-in piece taken as the sender's (adopt / reconcile / REMOVE); an owner write that failed */
long long g_reconQueued = 0, g_reconRemoved = 0, g_reconKept = 0, g_reconListed = 0, g_reconParked = 0, g_reconFull = 0;   /* P87 fold 2 (LOW 4): gaveUp gone (fold 1 M1: no give-up) */
long long g_p87Lines = 0, g_p87Suppressed = 0;
const size_t kReconCap = 512;
const int kReconPerDrain = 4;     /* key lookups per safe point */
const int kReconRearms = 16;      /* P87 fold 2 (LOW 4): UNUSED since fold 1 (M1) - no cap: a parked key is looked up again at EVERY zone load (BdReconRearm); rearms only counts them */
const int kReconTries = 8;        /* incomplete lookups before the key is parked */
const long long kP87LineCap = 60;
/* P14 (mmo8b, mmo8-buildings-read.md 4b), MAIN THREAD: own pieces standing at a READ-BACK tombstone's key (coopbuild::BuildTombSeenAct)
   - the save's copy of a piece the records retired after that save (its refund already in the records). Queued by the scans and the
   tombstone check, removed at the K2 safe point (BdTombKillDrain) with no refund; a queued key holds its tombstone (neither dropped nor
   sent) until the piece is gone. Cleared with the world, and with its tombstone (BdTombDrop, eviction, a new placement). */
struct BdTombKill
{
    unsigned int nextTry;
    int tries, misses, waitLogged;
    float matsBefore;
    long long refunded;        /* P14 fold 2 (D4): refund calls seen during this removal's calls - a retry zeroes the mats */
    int wall;
    int unread;                /* P14 fold 4 (L1): safe points whose mats could not be read (no call made) */
    BdTombKill() : nextTry(0), tries(0), misses(0), waitLogged(0), matsBefore(0.0f), refunded(0), wall(0), unread(0) {}
};
std::map<std::string, BdTombKill> g_tombKill;
/* P14 fold 1: keys whose removal was cancelled or given up in this world - an own piece there ends the tombstone as before P14 */
std::set<std::string> g_tombKillNo;
/* P14 fold 1 (the escape ledger): an own piece's refund bracket - the building pointer -> its key, until its items land (<= 30 s); the
   refund items of a piece whose tombstone is not made yet (BdTombNote takes them) */
struct BdRefundSrc { std::string key; DWORD at; };
std::map<void*, BdRefundSrc> g_refundSrc;
std::map<std::string, coopbuild::BuildLedger> g_ledgerPend;
long long g_ledRefund = 0, g_ledRefundNoPiece = 0, g_ledMissed = 0, g_ledSpill = 0, g_ledEscOwn = 0, g_ledEscPeer = 0, g_ledEscNoRec = 0,
          g_ledEscFull = 0, g_ledEscOver = 0, g_ledWriteNow = 0, g_ledWriteLate = 0, g_ledRead = 0, g_ledUnkept = 0, g_ledCounted = 0,
          g_ledPeerCounted = 0, g_ledNotCounted = 0, g_ledNoSid = 0, g_ledLines = 0, g_ledSuppressed = 0;
long long g_ledEscHolder = 0, g_ledHolderCounted = 0;   /* P14 fold 5: refund items handed to the area's holder; their escapes counted at reload */
long long g_ledUnkeyed = 0, g_ledMatEsc = 0, g_ledEscMerged = 0, g_ledBlind = 0, g_ledPruned = 0, g_ledKeptRefuse = 0, g_ledRefused = 0, g_ledHandRetry = 0,
          g_p14RetryZeroed = 0, g_p14MatsLeft = 0, g_p14MatsZeroBack = 0;   /* P14 fold 2 */
long long g_ledRefuseAll = 0, g_ledSpillBlind = 0;   /* P14 fold 3 (N2/N1) */
long long g_ledZoneUnread = 0, g_ledMatShared = 0, g_p14MatsUnread = 0;   /* P14 fold 4 (e5/L2/L1) */
long long g_p14Interior = 0, g_p14KeyMismatch = 0, g_p14Waits = 0, g_p14Rescans = 0, g_p14NearPlaced = 0;
void BdLogLedger(const char* line)   /* P14 fold 1: the ledger's own cap */
{
    if (g_ledLines < 40) { ++g_ledLines; DebugLog(std::string(line)); }
    else ++g_ledSuppressed;
}
long long g_p14Queued = 0, g_p14Removed = 0, g_p14Gone = 0, g_p14Cancelled = 0, g_p14GaveUp = 0, g_p14Faulted = 0, g_p14MatsZeroed = 0, g_p14Calls = 0;
long long g_p14Lines = 0, g_p14Suppressed = 0;
const long long kP14LineCap = 40;
void BdLogP87(const char* line)
{
    if (g_p87Lines < kP87LineCap) { ++g_p87Lines; DebugLog(std::string(line)); }
    else ++g_p87Suppressed;
}
void BdLogP14(const char* line)   /* P14: its own cap - the run's oracle lines never lost to P87's */
{
    if (g_p14Lines < kP14LineCap) { ++g_p14Lines; DebugLog(std::string(line)); }
    else ++g_p14Suppressed;
}
int BdOwnLiveRow(const std::string& key)
{
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(key);
    return (it != g_reg.end() && it->second.own != 0 && it->second.via != 3 && it->second.removed == 0 && it->second.dismantled == 0) ? 1 : 0;
}
/* P87 root (T651, T661: two owner-rule folds failed) - THE RECEIVER'S COPY LIST. This game's save names a copy's owner by the stand-in's
   record id 'coop-p<slot>'; the engine resolves it while the world loads, BEFORE the mod makes the stand-in, fails, and gives the piece to
   a town's faction (p87-rootcause Q1). The owner pointer does not survive a restart; the key does. Every copy made or adopted here is
   listed (key -> the owner's slot, the sid, the placement nonce, the PLACE pose / host) and kept in pp.build as a '~c' row
   (coopbuild::BuildCopyRowKey), written at the pp.build write points (every change is an event write; the teardown flush) and read back
   at world load (BuildRestoreQueue, the inv7a edge: before the stand-in, any adopt, reconcile or REMOVE). The one owner rule
   (BdOwnerClassAt) asks it FIRST. Retired when the copy is removed, handed to this game, or an own piece is placed at the key.
   MAIN THREAD. */
struct BdCopyEnt
{
    coopbuild::BuildCopyRec rec;
    int readBack;   /* read from pp.build in this world */
    int diag;       /* 0 = the load diagnostic line is still owed (a read-back entry whose piece was not looked at yet) */
    int rmReady;              /* P87 rf1: 1 = the removed mark may retire at a landed save; 0 = a read-back mark this world has not settled */
    unsigned long rmReq;      /* P87 rf1: g_bdSaveReq when the mark was set or settled */
    int evict;                /* P87 rf1: the load's search reached kBuildCopyEmptyLoads - dropped by the diag pass */
    std::string aliasKey;     /* P87 rf1: this entry's live-key alias entry (in memory; the alias is a row of its own) */
    BdCopyEnt() : readBack(0), diag(1), rmReady(0), rmReq(0), evict(0) {}
};
std::map<std::string, BdCopyEnt> g_copyList;
std::vector<std::string> g_copyRetire;   /* copy-list row keys pp.build drops at its next write */
int g_copyDiagOwed = 0;
unsigned long g_bdSaveReq = 0;   /* P87 rf1: save requests the engine accepted in this process (store.cpp BuildSaveTrackNote) */
int g_clListPending = 1;         /* P87 rf1: 1 = this world's copy list is not read yet - the scan, roster round, reconcile and adopts wait */
DWORD g_clHoldT0 = 0, g_clFeedAt = 0;
std::string g_clDiagCursor;      /* P87 rf1 (LOW): the diag pass's rotating cursor */
long long g_clMarked = 0, g_clMarkRetired = 0, g_clMarkReady = 0, g_clMarkRead = 0, g_clMarkRecon = 0, g_clMarkNoRow = 0, g_clGaveUpKept = 0;
long long g_clEvicted = 0, g_clAliases = 0, g_clHeld = 0, g_clHoldTimeout = 0, g_clBaseRead = 0, g_clLoadFull = 0, g_clSavesLanded = 0;
long long g_clRead = 0, g_clAdded = 0, g_clRefreshed = 0, g_clRetired = 0, g_clRowsEmitted = 0, g_clUnkept = 0, g_clFull = 0, g_clBadRows = 0;
long long g_clOwnWins = 0, g_clAdopt = 0, g_clRecon = 0, g_clRemove = 0, g_clNotTaken = 0, g_clNear = 0, g_clScanSkipped = 0, g_clScanBefore = 0;
long long g_clScanDemoted = 0, g_clNoSlot = 0, g_clDiagLogged = 0, g_clLines = 0, g_clSuppressed = 0;
const long long kClLineCap = 300;
void BdLogCopyList(const char* line)
{
    if (g_clLines < kClLineCap) { ++g_clLines; DebugLog(std::string(line)); }
    else ++g_clSuppressed;
}
/* this game's own piece at the key: a live own row, or its own pp.build row waiting to be restored - own rows always win */
int BdCopyOwnRowAt(const std::string& key)
{
    {   /* P87 rf1 (LOW): a live own row of THIS world - a row kept across the unload is the world before's */
        std::map<std::string, BdReg>::const_iterator ow = g_reg.find(key);
        if (ow != g_reg.end() && ow->second.gen == g_bdGen && BdOwnLiveRow(key) != 0) return 1;
    }
    for (size_t i = 0; i < g_pend.size(); ++i) if (g_pend[i].restore != 0 && g_pend[i].m.key == key) return 1;
    for (size_t i = 0; i < g_wait.size(); ++i) if (g_wait[i].restore != 0 && g_wait[i].m.key == key) return 1;
    return 0;
}
/* the entry for a live piece's key: the exact key, else the ONE entry within ItBoxResolve's window of it (BoxKeyNearKey: same sector and
   record, x/z +/-0.1 u - layout keys drift across a reload); two near entries = none. *listKey (may be 0) = the entry's own key. */
BdCopyEnt* BdCopyListFind(const std::string& key, std::string* listKey)
{
    if (g_copyList.empty() || key.empty()) return 0;
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    if (it == g_copyList.end())
    {
        std::string pre;   /* a near key starts with the same text up to its last '@' (record sid, sector): only that run of the sorted list is asked */
        if (!coopbuild::BuildKeyNearPrefix(key, &pre)) return 0;
        std::map<std::string, BdCopyEnt>::iterator hit = g_copyList.end();
        int n = 0;
        for (std::map<std::string, BdCopyEnt>::iterator c = g_copyList.lower_bound(pre); c != g_copyList.end() && c->first.compare(0, pre.size(), pre) == 0; ++c)
            if (BoxKeyNearKey(key.c_str(), c->first.c_str()) != 0) { hit = c; ++n; }
        if (n != 1) return 0;
        it = hit;
        ++g_clNear;
    }
    if (listKey != 0) *listKey = it->first;
    return &it->second;
}
/* P87 rf1 (LOW): a copy whose own (live) key differs from the owner's key is listed under the live key too - the scan computes the live
   key, so after a restart it still skips the copy. Furniture is never registered by the scan: not aliased. */
void BdCopyListAlias(const std::string& key, const BdReg& r, const coopbuild::BuildCopyRec& c)
{
    if (r.liveKey.empty() || r.liveKey == key || !c.hostKey.empty()) return;
    std::map<std::string, BdCopyEnt>::iterator mi = g_copyList.find(key);
    if (mi != g_copyList.end()) mi->second.aliasKey = r.liveKey;
    coopbuild::BuildCopyRec a = c;
    a.key = r.liveKey; a.alias = 1; a.removed = 0; a.emptyLoads = 0;
    if (r.hasPos != 0) for (int i = 0; i < 3; ++i) a.pos[i] = r.pos[i];
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(a.key);
    if (it == g_copyList.end())
    {
        if (g_copyList.size() >= coopbuild::kBuildCopyListCap) { ++g_clFull; return; }
        BdCopyEnt& e = g_copyList[a.key];
        e.rec = a; e.readBack = 0; e.diag = 1;
        ++g_clAliases;
    }
    else
    {
        if (it->second.rec.alias == 0) return;   /* an entry of its own at the live key: left as it is */
        it->second.rmReady = 0; it->second.rmReq = 0; it->second.evict = 0;
        if (coopbuild::BuildCopyRecSame(it->second.rec, a)) return;
        it->second.rec = a;
    }
    BdOwnDirty(2);
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s alias=%.63s: the copy stands at its own (live) key - listed there too, so the scan skips it after a restart; copyListAliases=%lld",
                key.c_str(), a.key.c_str(), g_clAliases);
    BdLogCopyList(line);
}
/* a copy made or adopted here (BdMarkOwner): listed, or its entry refreshed; pp.build takes it at once (event write) */
void BdCopyListNote(const std::string& key, const BdReg& r, const coopbuild::BuildMsg& m, int slot)
{
    char line[400];
    if (slot < 0 || slot > (int)coopbuild::kBuildOwnerSlotMax) { ++g_clNoSlot; return; }
    coopbuild::BuildCopyRec c;
    c.key = key; c.sid = r.sid.empty() ? m.sid : r.sid; c.slot = slot; c.nonce = m.nonce;
    for (int i = 0; i < 3; ++i) c.pos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) c.rot[i] = m.rot[i];
    if (!m.hostKey.empty()) { c.hostKey = m.hostKey; c.hostForm = m.hostForm; c.floor = m.floor; c.outside = m.outside; }
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    const int isNew = (it == g_copyList.end()) ? 1 : 0;
    if (isNew != 0)
    {
        if (g_copyList.size() >= coopbuild::kBuildCopyListCap)
        {
            ++g_clFull;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s NOT listed: the list is full (%u) - after a restart this copy falls back to the owner rule; copyListFull=%lld",
                        key.c_str(), (unsigned int)coopbuild::kBuildCopyListCap, g_clFull);
            BdLogCopyList(line);
            return;
        }
        BdCopyEnt& e = g_copyList[key];
        e.rec = c; e.readBack = 0; e.diag = 1;
        ++g_clAdded;
    }
    else
    {
        if (it->second.diag == 0 && g_copyDiagOwed > 0) --g_copyDiagOwed;
        it->second.diag = 1;
        it->second.rmReady = 0; it->second.rmReq = 0; it->second.evict = 0;   /* P87 rf1: made or adopted here again - live (c.removed is 0) */
        if (coopbuild::BuildCopyRecSame(it->second.rec, c)) { BdCopyListAlias(key, r, c); return; }
        it->second.rec = c;
        ++g_clRefreshed;
    }
    BdOwnDirty(2);
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s slot=%d sid=%.60s nonce=%08x %s - a '~c' row in pp.build: after a restart this key is still slot %d's copy; entries=%u",
                key.c_str(), slot, c.sid.c_str(), c.nonce, isNew != 0 ? "listed" : "refreshed", slot, (unsigned int)g_copyList.size());
    BdLogCopyList(line);
    BdCopyListAlias(key, r, c);   /* P87 rf1 */
}
/* the copy at the key is gone, handed to this game, or replaced by an own piece: its '~c' row leaves pp.build at the next write (the row
   key is dropped even when the entry is not in memory - a world whose pp.build was not read still retires it) */
void BdCopyListRetire(const std::string& key, const char* why)
{
    g_copyRetire.push_back(coopbuild::BuildCopyRowKey(key));
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    if (it == g_copyList.end()) return;
    if (it->second.diag == 0 && g_copyDiagOwed > 0) --g_copyDiagOwed;
    const int slot = it->second.rec.slot;
    const std::string alias = it->second.aliasKey;   /* P87 rf1: its live-key alias goes with it */
    g_copyList.erase(it);
    if (!alias.empty() && alias != key)
    {
        std::map<std::string, BdCopyEnt>::iterator al = g_copyList.find(alias);
        if (al != g_copyList.end() && al->second.rec.alias != 0)
        {
            if (al->second.diag == 0 && g_copyDiagOwed > 0) --g_copyDiagOwed;
            g_copyList.erase(al);
            g_copyRetire.push_back(coopbuild::BuildCopyRowKey(alias));
        }
    }
    ++g_clRetired;
    BdOwnDirty(2);
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s retired: %s (was slot %d's copy) - its '~c' row leaves pp.build; entries=%u",
                key.c_str(), why, slot, (unsigned int)g_copyList.size());
    BdLogCopyList(line);
}
/* P87 rf1 (HIGH): the copy at the key was REMOVED here. Its row is NOT dropped: this game's save is separate from pp.build, and a load of
   an earlier save, a quit without saving or a crash brings the copy back - with no row the reconcile fell back and KEPT it (T661). The
   entry is marked removed (its row's STATE destroyed bit) and kept until a save ASKED AFTER this removal has landed (BuildSaveLanded); a
   mark read back at a load makes the reconcile remove the returning copy. */
void BdCopyListMarkRemoved(const std::string& key, const BdReg& r)
{
    char line[400];
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    if (it == g_copyList.end())
    {   /* not listed here (the list was full, or the copy predates the list): marked from its row */
        coopbuild::BuildCopyRec c;
        c.key = key; c.sid = r.sid; c.slot = r.ownerSlot; c.nonce = r.nonce; c.removed = 1;
        if (r.hasPos != 0) for (int i = 0; i < 3; ++i) c.pos[i] = r.pos[i];
        if (!r.hostKey.empty()) { c.hostKey = r.hostKey; c.hostForm = (unsigned char)r.hostForm; }
        std::string rk;
        std::vector<char> cp, cs;
        if (g_copyList.size() >= coopbuild::kBuildCopyListCap || !coopbuild::BuildCopyRowMake(c, &rk, &cp, &cs))
        {
            ++g_clMarkNoRow;
            g_copyRetire.push_back(coopbuild::BuildCopyRowKey(key));   /* no mark can be kept: any old row goes, as before rf1 */
            BdOwnDirty(2);
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s removed here but NOT marked (the list is full or the row cannot be written) - a load of an earlier save falls back to the owner rule; copyListMarkNoRow=%lld",
                        key.c_str(), g_clMarkNoRow);
            BdLogCopyList(line);
            return;
        }
        BdCopyEnt& ne = g_copyList[key];
        ne.rec = c; ne.readBack = 0; ne.diag = 1;
        it = g_copyList.find(key);
    }
    BdCopyEnt& e = it->second;
    if (e.diag == 0 && g_copyDiagOwed > 0) --g_copyDiagOwed;
    e.diag = 1;
    e.rec.removed = 1; e.rmReady = 1; e.rmReq = g_bdSaveReq; e.evict = 0;
    ++g_clMarked;
    BdOwnDirty(2);
    if (!e.aliasKey.empty())
    {
        std::map<std::string, BdCopyEnt>::iterator al = g_copyList.find(e.aliasKey);
        if (al != g_copyList.end() && al->second.rec.alias != 0) { al->second.rec.removed = 1; al->second.rmReady = 1; al->second.rmReq = g_bdSaveReq; }
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s marked REMOVED (slot %d's copy was removed here) - its '~c' row stays in pp.build until a save asked after this (request > %lu) has landed; copyListMarked=%lld",
                key.c_str(), e.rec.slot, g_bdSaveReq, g_clMarked);
    BdLogCopyList(line);
}
/* P87 rf1: a read-back removed mark is settled in this world (a complete search found no piece at its key) - it retires at the next
   save asked from now on */
void BdCopyListSettled(const std::string& key, const char* how)
{
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    if (it == g_copyList.end() || it->second.rec.removed == 0 || it->second.rmReady != 0) return;
    it->second.rmReady = 1; it->second.rmReq = g_bdSaveReq;
    ++g_clMarkReady;
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s removed mark settled: %s - it retires once a save asked from now on has landed; copyListMarkReady=%lld",
                key.c_str(), how, g_clMarkReady);
    BdLogCopyList(line);
}
void BdCopyListReadDone(const char* from)
{
    if (g_clListPending == 0) return;
    g_clListPending = 0;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list: this world's list is read (%s) - %u entries; the zone scan, roster round, reconcile and adopts go ahead (held %lld times)",
                from, (unsigned int)g_copyList.size(), g_clHeld);
    BdLogCopyList(line);
}
/* P87 rf1 (MED, LOW-MED), MAIN THREAD: 1 = hold - this world's copy list is not read yet. Once a second the store is asked for the pp.build
   base (OwnBuildCopyListFeed: only when the load edge will not hand the list); past kClHoldMs the held work goes ahead without it. */
const DWORD kClHoldMs = 30000;
int BdCopyListHold()
{
    if (g_clListPending == 0) return 0;
    const DWORD now = ::GetTickCount();
    if (g_clHoldT0 == 0) g_clHoldT0 = (now == 0) ? 1 : now;
    if (g_clFeedAt == 0 || (DWORD)(now - g_clFeedAt) >= 1000)
    {
        g_clFeedAt = (now == 0) ? 1 : now;
        OwnBuildCopyListFeed();
        if (g_clListPending == 0) return 0;
    }
    if ((DWORD)(now - g_clHoldT0) >= kClHoldMs)
    {
        ++g_clHoldTimeout;
        g_clListPending = 0;
        char line[300];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list NOT read %lu ms into this world (the store's record could not be read, or no load edge came) - the held scan / roster / reconcile / adopts go ahead WITHOUT it; copyListHoldTimeout=%lld",
                    (unsigned long)(now - g_clHoldT0), g_clHoldTimeout);
        BdLogCopyList(line);
        return 0;
    }
    ++g_clHeld;
    return 1;
}
void BdReconQueue(const std::string& key, int fromSlot);   /* P87 rf1: defined with the reconcile below (this namespace) */
void BdTombAddKey(const std::string& key, long long* counter)
{
    std::vector<std::string> ev;
    if (coopbuild::BuildTombAdd(&g_tomb, key, coopbuild::kBuildTombCap, &ev) != 0) ++*counter;
    for (size_t i = 0; i < ev.size(); ++i) { g_tombRetire.push_back(coopbuild::BuildTombRowKey(ev[i])); ++g_tombEvicted; g_tombInfo.erase(ev[i]); g_tombKill.erase(ev[i]); /* P14 */ }
}
void BdTombDrop(const std::string& key, const char* why, int unOwe);   /* P14 fold 1: BdTombDropNear below */
void BdTombNote(const std::string& key, const BdReg& r)   /* an own piece dismantled here - P87 fold 1: a tombstone of THIS world (sent as before) */
{
    BdTombAddKey(key, &g_tombPersisted);
    BdTombInfo& ti = g_tombInfo[key];
    ti.readBack = 0; ti.verified = 0; ti.tail = 1; ti.nonce = r.nonce;
    ti.furn = (!r.hostKey.empty() || r.hostForm != 0) ? 1 : 0;   /* layout furniture: verified inside its host after a reload */
    ti.hostKey = (ti.furn != 0) ? r.hostKey : std::string();
    ti.led = coopbuild::BuildLedger();   /* P14 fold 1: this dismantle's escape ledger - its refund items landed so far */
    {
        std::map<std::string, coopbuild::BuildLedger>::iterator lp = g_ledgerPend.find(key);
        if (lp != g_ledgerPend.end()) { ti.led = lp->second; g_ledgerPend.erase(lp); }
    }
    BdOwnDirty(2);
}
/* P14 fold 1 (F2): a new own piece placed - a tombstone of the same record and sector within ItBoxResolve's window of its key ends too */
void BdTombDropNear(const std::string& key)
{
    const size_t at = key.rfind('@');
    if (at == std::string::npos || g_tombInfo.empty()) return;
    const size_t pl = at + 1;
    std::vector<std::string> hit;
    for (std::map<std::string, BdTombInfo>::const_iterator it = g_tombInfo.begin(); it != g_tombInfo.end(); ++it)
        if (it->first != key && it->first.size() >= pl && it->first.compare(0, pl, key, 0, pl) == 0 && BoxKeyNearKey(key.c_str(), it->first.c_str()) != 0)
            hit.push_back(it->first);
    for (size_t i = 0; i < hit.size(); ++i) { ++g_p14NearPlaced; BdTombDrop(hit[i], "P14 fold 1: a new own piece of the same record was placed within ItBoxResolve's window of the key", 0); }
}
void BdTombDrop(const std::string& key, const char* why, int unOwe)
{
    if (coopbuild::BuildOweRemoveDrop(&g_tomb, key) == 0) return;
    g_tombInfo.erase(key);
    g_tombKill.erase(key);   /* P14: no removal without its tombstone */
    if (unOwe != 0 && coopbuild::BuildOweRemoveDrop(&g_dmOweRemove, key) != 0) ++g_tombUnowed;   /* P87 fold 1: an own piece stands there - its queued REMOVE never goes */
    g_tombRetire.push_back(coopbuild::BuildTombRowKey(key));
    ++g_tombDropListed;
    BdOwnDirty(2);
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 tombstone key=%.63s dropped: %s (tombstones=%u)", key.c_str(), why, (unsigned int)g_tomb.size());
    BdLogP87(line);
}
void BdReconRearm()   /* a zone finished loading: every parked key is looked up again */
{
    for (std::map<std::string, BdRecon>::iterator it = g_recon.begin(); it != g_recon.end(); )
    {
        if (it->second.parked == 0) { ++it; continue; }
        /* P87 fold 1 (M1): no give-up - a parked key waits for the next zone load until the world is torn down (kReconCap bounds them) */
        ++it->second.rearms; it->second.parked = 0; it->second.nextTry = 0; it->second.tries = 0;
        ++it;
    }
}
int BdReconAnyDue()
{
    for (std::map<std::string, BdRecon>::const_iterator it = g_recon.begin(); it != g_recon.end(); ++it)
        if (it->second.parked == 0 && it->second.nextTry <= g_drainNo) return 1;
    return 0;
}
/* help1 fold 4 (re-check #1): an own row with no nonce (the load scan registers the save's pieces with 0) takes its placement's nonce
   from the pp.build PLACE record of its key - called wherever either side arrives (the scan, the restore, the drain, the sweep, a new
   placement over it, the pp.build write), so the order they run in does not matter. 1 = the row has a nonce it did not have */
int BdRestoreNonce(const std::string& key, BdReg& r, int markDirty)
{
    if (r.nonce != 0 || r.gen != g_bdGen) return 0;
    std::map<std::string, unsigned int>::iterator it = g_placeNonceRestore.find(key);
    const int known = (it != g_placeNonceRestore.end()) ? 1 : 0;
    const unsigned int n = coopbuild::BuildOwnNonceRestore(r.nonce, r.own, r.via == 3 ? 1 : 0, known, known != 0 ? it->second : 0u);
    if (n == 0) return 0;
    r.nonce = n;
    g_placeNonceRestore.erase(it);
    ++g_helpNonceRestored;
    char line[360];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help own row key=%.63s takes its placement nonce %08x from its pp.build PLACE record (registered with none) - its confirmation rows and the work for it are judged by it now nonceRestored=%lld",
                key.c_str(), n, g_helpNonceRestored);
    BdLogHelp(line);
    if (markDirty != 0) BdOwnDirty(1);
    return 1;
}
void BdLogRestore(const char* line)
{
    if (g_rsLines < kRsLineCap) { ++g_rsLines; DebugLog(std::string(line)); }
    else ++g_rsSuppressed;
}
/* a restore that finished without present / recreated / refused gave up (the copy FAILED / DROPPED line says why) */
void BdRestoreDone(BdPending& p)
{
    if (p.restore == 0 || p.rsOutcome != 0) return;
    ++g_rsFailed; p.rsOutcome = 4;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s FAILED - not re-made (the copy line above says why) owner=mine", p.m.key.c_str());
    BdLogRestore(line);
}
/* the registry's via names (buildlist, the roster): 1 hook, 3 copy, 4 scan, 5 handed, 6 restored (mmo8a), else test */
const char* BdViaName(int via)
{
    return via == 1 ? "hook" : (via == 3 ? "copy" : (via == 4 ? "scan" : (via == 5 ? "handed" : (via == 6 ? "restored" : "test"))));
}
/* build1-c: the LATEST STATE per key, applied at the safe point (kept while its copy is pending or its area is unloaded) */
struct BdStatePend
{
    coopbuild::BuildMsg m;
    unsigned int nextTry;
    int fromPlace, waitLogged;
    int deferTries;   /* P15 fold 1 (LOW 4): tries this STATE was kept because the engine deferred setDestroyed (message 16) */
};
std::map<std::string, BdStatePend> g_pendState;
/* build1-d: REMOVEs waiting for the K2 safe point (MAIN THREAD), one per key */
struct BdRemovePend
{
    int reason, tries, waitLogged, deferLogged;
    unsigned int nextTry;
    float matsBefore;
    int missTries;    /* build1-e (F2): lookups in a loaded area that were not a complete empty search */
};
std::map<std::string, BdRemovePend> g_pendRemove;
std::string g_removeCursor;   /* review-build1d D3: the last REMOVE key handled - the next safe point starts after it */
/* review-build1d D5, MAIN THREAD: key -> GetTickCount when its row was marked removed; BdSweepRemoved erases the row later */
std::map<std::string, DWORD> g_recentRemoved;
/* house2 (item 4) + fold (review-house2 MED2, LOW c, d), MAIN THREAD: every placement this game has seen removed (a REMOVE sent or
   received) as (world, key, nonce) - the world is the world folder, so another world's entries never match and a reload of the same
   world keeps them. FIFO-bounded; a repeat removal moves to the back. A hand-over PLACE naming a removed placement is late (BdCopyOne). */
std::set<std::string> g_removedKeys;              /* "<world>\n<key>\n<nonce hex>" */
std::deque<std::string> g_removedOrder;           /* the same entries, oldest first */
std::map<std::string, int> g_removedKeyCount;     /* "<world>\n<key>" -> entries listed (the no-nonce rule) */
const size_t kRemovedKeysCap = 4096;
std::string BdRemovedWk(const std::string& key) { return StoreNotebookDir() + "\n" + key; }
std::string BdRemovedEntry(const std::string& wk, unsigned int nonce)
{
    char h[16];
    _snprintf_s(h, sizeof(h), _TRUNCATE, "%08x", nonce);
    return wk + "\n" + h;
}
void BdNoteRemovedNonce(const std::string& key, unsigned int nonce)
{
    if (key.empty()) return;
    const std::string wk = BdRemovedWk(key);
    const std::string e = BdRemovedEntry(wk, nonce);
    if (!g_removedKeys.insert(e).second)   /* LOW d: a repeat removal is the newest again */
    {
        for (std::deque<std::string>::iterator it = g_removedOrder.begin(); it != g_removedOrder.end(); ++it)
            if (*it == e) { g_removedOrder.erase(it); break; }
        g_removedOrder.push_back(e);
        return;
    }
    ++g_removedKeyCount[wk];
    g_removedOrder.push_back(e);
    while (g_removedOrder.size() > kRemovedKeysCap)
    {
        const std::string old = g_removedOrder.front();
        g_removedOrder.pop_front();
        g_removedKeys.erase(old);
        std::map<std::string, int>::iterator kc = g_removedKeyCount.find(old.substr(0, old.rfind('\n')));
        if (kc != g_removedKeyCount.end() && --kc->second <= 0) g_removedKeyCount.erase(kc);
    }
}
int BdRemovedExact(const std::string& key, unsigned int nonce) { return g_removedKeys.find(BdRemovedEntry(BdRemovedWk(key), nonce)) != g_removedKeys.end() ? 1 : 0; }
int BdRemovedAnyNonce(const std::string& key) { return g_removedKeyCount.find(BdRemovedWk(key)) != g_removedKeyCount.end() ? 1 : 0; }
/* a row removed here: its own nonce (0 = unknown, e.g. a row the load scan rebuilt) */
void BdNoteRemoved(const std::string& key)
{
    g_recentRemoved[key] = ::GetTickCount();
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(key);
    BdNoteRemovedNonce(key, it != g_reg.end() ? it->second.nonce : 0u);
    if (it != g_reg.end() && it->second.via == 3) BdCopyListMarkRemoved(key, it->second);   /* P87 rf1 (HIGH): marked, kept until a save asked after it has landed */
}
/* P87 rf1 (MED): a REMOVE GIVEN UP (removed=0 dismantled=0) - the copy may still stand: its nonce is noted as before, its list entry stays live */
void BdNoteRemovedGaveUp(const std::string& key)
{
    g_recentRemoved[key] = ::GetTickCount();
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(key);
    BdNoteRemovedNonce(key, it != g_reg.end() ? it->second.nonce : 0u);
    if (it != g_reg.end() && it->second.via == 3 && g_copyList.find(key) != g_copyList.end()) ++g_clGaveUpKept;
}
/* house2 fold (MED2): a REMOVE received - the placement(s) it ends here: this game's copy row, and every PLACE still pending for the key
   (dropped by the REMOVE); nothing here = the key with an unknown nonce. A key that is THIS game's own piece is not noted. */
void BdNoteRemovedRecv(const std::string& key, unsigned int removeNonce)
{
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(key);
    const int ownHere = (it != g_reg.end() && it->second.via != 3) ? 1 : 0;   /* fold 2: the own row is not noted, its pending PLACEs are */
    /* P87 fold 1 (H2): only the placements the REMOVE's nonce names (all of them when either nonce is unknown) */
    if (it != g_reg.end() && ownHere == 0 && coopbuild::BuildRemoveNonceSkip(removeNonce, it->second.nonce) == 0) BdNoteRemovedNonce(key, it->second.nonce);
    for (size_t i = 0; i < g_pend.size(); ++i) if (g_pend[i].m.key == key && coopbuild::BuildRemoveNonceSkip(removeNonce, g_pend[i].m.nonce) == 0) BdNoteRemovedNonce(key, g_pend[i].m.nonce);
    for (size_t i = 0; i < g_wait.size(); ++i) if (g_wait[i].m.key == key && coopbuild::BuildRemoveNonceSkip(removeNonce, g_wait[i].m.nonce) == 0) BdNoteRemovedNonce(key, g_wait[i].m.nonce);
    /* P87 fold 1 (L2): no row and no pending PLACE here - nothing is noted (a tombstone re-sent at every round must not list the key) */
}
/* house2 fold (MED2): a new placement's nonce - never 0 (0 = none on the wire) */
unsigned int BdNewNonce()
{
    static unsigned int s_n = 0;
    LARGE_INTEGER q; q.QuadPart = 0;
    ::QueryPerformanceCounter(&q);
    unsigned int x = (unsigned int)q.LowPart ^ ((unsigned int)q.HighPart * 0x85EBCA6Bu) ^ (::GetTickCount() * 0x9E3779B1u)
                   ^ ((++s_n) * 0xC2B2AE35u) ^ (::GetCurrentProcessId() << 16);
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x != 0 ? x : 1u;
}

/* build1-e: a furniture piece is added to its host's LAYOUT, not to the zone list ObjectByPositionKey walks (BED1), so it
   is looked for among the host's interior pieces (items.cpp InteriorPieceIds, POD): each hand resolved through the engine's
   handle table, its identity re-checked against its own handle, its key built as the builder's was. `alt` = the copy's own
   created key when it differed. *verdict: kObjKeyFound, kObjKeyNone (a complete, untruncated list without it),
   kObjKeyAmbiguous, kObjKeyTruncated. MAIN THREAD. No __try here (a hand is an object): the reads are guarded helpers. */
unsigned int g_bdIds[kBdInteriorCap * 5];
void* BdFindInHost(void* host, const char* key, const char* alt, int* verdict)
{
    int listed = -1, trunc = 0, torn = 0;
    const int n = InteriorPieceIds(host, g_bdIds, kBdInteriorCap, &listed, &trunc, &torn);
    void* found = 0;
    int hits = 0, typed = 0, keyed = 0;   /* review-build1e D1: 'none' only when every building piece was resolved and keyed */
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = g_bdIds + (size_t)i * 5;
        if (f[0] != 0 || (f[3] == 0 && f[4] == 0)) continue;   /* BUILDING (0) only, as 0x54A220 keeps */
        ++typed;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = (void*)h.asBuilding();
        unsigned int own[5];
        if (b == 0 || ObjectHandIds(b, own) == 0 || std::memcmp(own, f, sizeof(own)) != 0) continue;
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) continue;
        ++keyed;
        if (std::strcmp(k, key) != 0 && (alt == 0 || alt[0] == 0 || std::strcmp(k, alt) != 0)) continue;
        if (b != found) { found = b; ++hits; }
    }
    if (hits == 1) { *verdict = kObjKeyFound; return found; }
    if (hits > 1) { *verdict = kObjKeyAmbiguous; return 0; }
    *verdict = (listed >= 0 && trunc == 0 && torn == 0 && n == listed && keyed == typed) ? kObjKeyNone : kObjKeyTruncated;
    return 0;
}
/* build1-e: a registry row's live piece - a free piece by its key (ObjectByPositionKey), furniture inside its host.
   *verdict says why nothing came back (recheck-build1d F2). MAIN THREAD. */
void* BdFindPiece(const std::string& key, const BdReg& r, int* verdict)
{
    if (r.hostKey.empty())
    {
        void* b = ObjectByPositionKey(key.c_str());
        *verdict = ObjectByPositionKeyVerdict();
        return b;
    }
    void* host = ObjectByPositionKey(r.hostKey.c_str());
    if (host == 0) { *verdict = kBdHostMissing; return 0; }
    return BdFindInHost(host, key.c_str(), r.liveKey.c_str(), verdict);
}
const char* BdVerdictWord(int v)
{
    switch (v)
    {
    case kObjKeyFound: return "found";
    case kObjKeyNone: return "none (a complete search, no candidate)";
    case kObjKeyAmbiguous: return "ambiguous (two or more candidates)";
    case kObjKeyTruncated: return "truncated (the walk could not list everything)";
    case kObjKeyTeardown: return "teardown";
    case kBdHostMissing: return "the host does not resolve here";
    default: return "refused (malformed key)";
    }
}

/* the commit: MAIN THREAD (Read). g_commitTid lets the createBuilding detour on any other thread pass straight through. */
LONG  g_commitDepth = 0;
DWORD g_commitTid = 0;
LONG  g_createDepth = 0;
void* g_commitNew[16];
int   g_commitNewN = 0;
float g_commitPos[16][3];   /* build1-b: the floats each of those createBuilding calls received */
float g_commitRot[16][4];
int   g_commitMeta[16];     /* bit 0 pos+rot read, bit 1 furniture (furnitureOf or isIndoorsOf set) */
void* g_commitHost[16];     /* build1-e: the host building (BdHostFormPod) */
int   g_commitForm[16];     /* build1-e: coopbuild::kBuildHost* */
int   g_commitFloor[16], g_commitOutside[16];   /* build1-e: createBuilding's floor / outsideFurniture */
int   g_commitHanded[16], g_commitHandWhy[16];  /* build1h: the slot each piece was handed to at the commit (-1 kept), and why */

/* the lever: armed on the main thread, drained at the safe point (also the main thread) */
volatile LONG g_btPending = 0;   /* 1 place, 2 progress, 3 dismantle, 4 into near (build1-e) */
std::string g_btArg;
std::string g_btHost;    /* build1h: `into <near|host-key-substring>` */
int g_btIndoors = 0;     /* build1h: `into ... indoors` = form 2 */
float g_btDx = 30.0f, g_btDz = 0.0f, g_btAmount = 0.0f;

int BdOnMain()
{
    const unsigned long m = StoreMainThreadId();
    return (m != 0 && ::GetCurrentThreadId() == (DWORD)m) ? 1 : 0;
}

// ---- guarded POD helpers (no objects with destructors in any of these) ----
int BdReadState(void* b, float* prog, float* need, int* complete, int* dism, int* destroyed)
{
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return 0;
        *complete = st[0] != 0 ? 1 : 0;
        *dism = st[2] != 0 ? 1 : 0;
        *prog = *(const float*)(st + 4);
        *need = *(const float*)(st + 0x28);
        *destroyed = *((const unsigned char*)b + kBDestroyed) != 0 ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* build1-c: the delivered amount of each material (layout above). Returns how many were copied (<= cap), -1 unreadable;
   *total = the engine's count. */
int BdReadMats(void* b, float* out, int cap, int* total)
{
    *total = -1;
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return -1;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        const char* const* arr = *(const char* const* const*)(st + kStMatArray);
        if (n > kStMatSane || (n > 0 && arr == 0)) return -1;
        *total = (int)n;
        int k = 0;
        for (; k < cap && (unsigned int)k < n; ++k)
        {
            const char* bm = arr[k];
            out[k] = (bm != 0) ? *(const float*)(bm + kMatDelivered) : 0.0f;
        }
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *total = -1; return -1; }
}
/* build1-c, the copy's STATE write (answers-build1b R2 + Safe call recipe step 4), K2 safe point only. Mats first
   (mats[i]+0xC for i < min(n, engine count)); then, when writeProgress: state+4 = progress (the caller has capped it below
   needed), setBroken(0) if progress > 0, and updatePhysicalWithProgress 0x558680 (upd). *stage says how far it got. */
int BdApplyStatePod(void* b, int writeProgress, float progress, int nMats, const float* mats, int* matsLive, uintptr_t upd, int* stage)
{
    *stage = 0;
    __try
    {
        char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return 0;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        char* const* arr = *(char* const* const*)(st + kStMatArray);
        *matsLive = (n > kStMatSane || (n > 0 && arr == 0)) ? -1 : (int)n;
        if (*matsLive > 0)
            for (int i = 0; i < nMats && (unsigned int)i < n; ++i)
                if (arr[i] != 0)   /* review-build1c L1: clamp to 0..the material's own total (+8) - a peer's value is never trusted */
                {
                    const float total = *(const float*)(arr[i] + 8);
                    float v = mats[i] < 0.0f ? 0.0f : mats[i];
                    if (total >= 0.0f && v > total) v = total;
                    *(float*)(arr[i] + kMatDelivered) = v;
                }
        *stage = 1;
        if (writeProgress == 0) return 1;
        *(float*)(st + 4) = progress;
        *stage = 2;
        if (progress > 0.0f) ((BdSetBoolFn)((*(void***)b)[kVtSetBroken / 8]))(b, false);
        *stage = 3;
        ((BdVoidFn)upd)(b);
        *stage = 4;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* BdOwnerPod(void* b)
{
    __try { return ((BdGetPtrFn)((*(void***)b)[kVtFaction / 8]))(b); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* ground5 fold 2: the same owner read, telling a FAULT (0) from a piece nobody owns (1 with *out 0) - BuildGroundSource */
int BdOwnerReadPod(void* b, void** out)
{
    __try { *out = ((BdGetPtrFn)((*(void***)b)[kVtFaction / 8]))(b); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
int BdSetFactionPodRaw(void* b, void* f)
{
    __try { ((BdSetFactionFn)((*(void***)b)[kVtSetFaction / 8]))(b, f, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* P18: every setFaction the mod makes is marked, so the owner-change hook never sends it back (the loop refusal) */
int BdSetFactionPod(void* b, void* f)
{
    ++g_bdOwnerWrite;
    const int r = BdSetFactionPodRaw(b, f);
    --g_bdOwnerWrite;
    return r;
}
/* P18 fold 1 (item 2): A BOUGHT HOUSE'S WORLD SIDE ON THE RECEIVING GAME. Building::buyMeCallback 0x7ACB20 (build/decomp_7acb20.txt),
   on its confirm branch, in this order: (1) the owner's platoons homed in the house (0x6BA220(getOwnerFaction(), &list, this)) lose it -
   each one's Ownerships (Platoon vt+0x98) gets its home blanked and Ownerships::removeOwnedObject 0x7EB550(own, &house.handle);
   (2) the OLD owner's Ownerships (owner+0x10 -> +0x80) removes the house; (3) this->setFaction(player); (4) the NEW owner's Ownerships
   adds it (addOwnedObject 0x7EBF60); (5) the residentSquad hand (+0xD0) is cleared (+0xD8 = 0xB, +0xE0 / +0xE4 = 0); (6) each door
   (+0x1B8 lektor: count +0x1C0, array +0x1C8) is setFaction'd and opened (openDoor 0x297000 on door vt+0x3C8); (7) the interior's
   +0x88 / +0x90 list is setFaction'd; (8) each resident platoon finds a new home (Platoon 0x99D810(p, &hand)). The SAME engine
   functions run here with the sender's stand-in as the new owner. 0x7EB550 blanks the home itself when it equals the house (its
   tail), which is what step 1's blanking does. NOT REPEATED (counted fxStepsSkipped, 7 per purchase, named in the line): 0x54EA40
   _destroyAllInternalBuildings (destroys furniture - the buyer's furniture road carries its own); 0x38A5E0 on the global at
   0x2133568 (location nodes - no var row, its effect not read); the interior's +0x100 map walk (0x32E2B0 / 0x3305C0); the town
   step 0x296900 / 0x928010; interior+0x30 vt+0x60; 0x4D2340; and the residents' AI orders 0x5D1640(c, 0, 0x16|0x13) - the call
   site sets 3 of its 7 arguments, so it is not callable safely; their homes are moved, the AI re-plans from those. The buy
   callback itself is never called here (it charges this game's player). MAIN THREAD (BdCopyOne), every step guarded. */
const unsigned kBdBuyResCap = 256;
const unsigned kBdBuyDoorCap = 16;
const unsigned kBdBuyInteriorCap = 512;
typedef void (*BdOwnListFn)(void* ownerships, void* what);
typedef void (*BdHomedInFn)(void* faction, void* list, void* building);
typedef void* (*BdNewHomeFn)(void* platoon, void* outHand);
typedef unsigned char (*BdOpenDoorFn)(void* doorStuff);
typedef void (*BdBuyCbFn)(void* b, int result);
struct BdLektor { void* vft; unsigned int count; unsigned int cap; void** data; };   /* lektor<T*>: +0x8 count, +0xC room, +0x10 elements */
struct BdBuyFx
{
    int res; unsigned resN; void* resP[kBdBuyResCap];
    int townOut, townIn, squad, doors, interior, homes;
    unsigned doorsN, doorsOpened, interiorN, newHomes;
};
int BdIsForSalePod(void* b)   /* the engine's own Building::isForSale (vt+0x2C0): 1 / 0, -1 faulted */
{
    __try { return ((BdIsForSaleFn)((*(void***)b)[0x2C0 / 8]))(b) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyResidentsOutPod(void* b, void* o, BdBuyFx* fx)   /* step 1: 1 done, 0 no rows, -2 does not fit, -1 faulted */
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kFacHomedInRva == 0 || kOwnRemoveRva == 0 || o == 0) return 0;
    __try
    {
        const unsigned na = *(const unsigned*)((const char*)o + 0x210), nu = *(const unsigned*)((const char*)o + 0x228);
        if (coopbuild::BuildBuyResidentsFit(na, nu, kBdBuyResCap) == 0) return -2;
        BdLektor l; l.vft = 0; l.count = 0; l.cap = kBdBuyResCap; l.data = fx->resP;
        ((BdHomedInFn)(base + (uintptr_t)kFacHomedInRva))(o, &l, b);
        fx->resN = (l.count <= kBdBuyResCap) ? l.count : kBdBuyResCap;
        for (unsigned i = 0; i < fx->resN; ++i)
        {
            void* p = fx->resP[i];
            if (p == 0) continue;
            void* own = ((BdGetPtrFn)((*(void***)p)[0x98 / 8]))(p);
            if (own != 0) ((BdOwnListFn)(base + (uintptr_t)kOwnRemoveRva))(own, (char*)b + kRootHandle);
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyOwnListPod(void* b, uintptr_t fn)   /* steps 2 / 4: the CURRENT owner's Ownerships list, remove or add the house */
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (fn == 0) return 0;
    __try
    {
        void* f = *(void**)((char*)b + kRootOwner);
        if (f == 0) return 0;
        void* own = *(void**)((char*)f + kFacOwnerships);
        if (own == 0) return 0;
        ((BdOwnListFn)(base + fn))(own, (char*)b + kRootHandle);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuySquadClearPod(void* b)   /* step 5 */
{
    __try
    {
        *(long long*)((char*)b + kBResidentSquad + 0x8) = 0xB;
        *(long long*)((char*)b + kBResidentSquad + 0x14) = 0;
        *(int*)((char*)b + kBResidentSquad + 0x10) = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyDoorsPod(void* b, void* want, BdBuyFx* fx)   /* step 6 */
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const unsigned long long od = DoorsOpenDoorRva();
    __try
    {
        const unsigned n = *(const unsigned*)((const char*)b + 0x1C0);
        void** arr = *(void***)((char*)b + 0x1C8);
        if (n == 0 || arr == 0) return 0;
        for (unsigned i = 0; i < n && i < kBdBuyDoorCap; ++i)
        {
            void* d = arr[i];
            if (d == 0) continue;
            ++fx->doorsN;
            BdSetFactionPod(d, want);
            void* ds = ((BdGetPtrFn)((*(void***)d)[0x3C8 / 8]))(d);
            if (ds != 0 && od != 0) ((BdOpenDoorFn)(base + (uintptr_t)od))(ds);
            if (ds != 0 && *(const int*)((const char*)ds + 0x380) != 0) ++fx->doorsOpened;   /* doorsync.h kDoorClosed = 0 */
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyInteriorPod(void* b, void* want, BdBuyFx* fx)   /* step 7 */
{
    __try
    {
        void* in = *(void**)((char*)b + kBInterior);
        if (in == 0) return 0;
        const unsigned n = *(const unsigned*)((const char*)in + 0x88);
        void** arr = *(void***)((char*)in + 0x90);
        if (n == 0 || arr == 0) return 0;
        for (unsigned i = 0; i < n && i < kBdBuyInteriorCap; ++i)
            if (arr[i] != 0 && BdSetFactionPod(arr[i], want) == 1) ++fx->interiorN;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyNewHomesPod(BdBuyFx* fx)   /* step 8 */
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kPlatNewHomeRva == 0) return 0;
    __try
    {
        for (unsigned i = 0; i < fx->resN; ++i)
        {
            if (fx->resP[i] == 0) continue;
            unsigned char outHand[64];
            ((BdNewHomeFn)(base + (uintptr_t)kPlatNewHomeRva))(fx->resP[i], outHand);
            ++fx->newHomes;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void BdBuyFxBefore(void* b, void* o, BdBuyFx* fx)   /* steps 1 - 2, before the owner write */
{
    fx->res = BdBuyResidentsOutPod(b, o, fx);
    fx->townOut = BdBuyOwnListPod(b, (uintptr_t)kOwnRemoveRva);
}
void BdBuyFxAfter(void* b, void* want, BdBuyFx* fx)   /* steps 4 - 8, after it */
{
    fx->townIn = BdBuyOwnListPod(b, (uintptr_t)kOwnAddRva);
    fx->squad = BdBuySquadClearPod(b);
    fx->doors = BdBuyDoorsPod(b, want, fx);
    fx->interior = BdBuyInteriorPod(b, want, fx);
    fx->homes = (fx->res == 1) ? BdBuyNewHomesPod(fx) : 0;
}
int BdMiningPod(void* b)
{
    __try { ((BdVoidFn)((*(void***)b)[kVtMining / 8]))(b); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdProgressPod(void* b, float amount)
{
    __try { ((BdProgressFn)((*(void***)b)[kVtProgress / 8]))(b, amount); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* P15: the engine's own setDestroyed (vt+0x340 -> 0x5567F0) through the vtable, so the hook (detour_destroy) sees it like any call */
int BdDestroyPod(void* b, bool v)
{
    __try { ((BdSetBoolFn)((*(void***)b)[kVtSetDestroyed / 8]))(b, v); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdDismantlePod(void* b, float amount, int* ret)
{
    __try { *ret = (int)(((BdDismantleFn)((*(void***)b)[kVtDismantle / 8]))(b, amount) & 0xFF); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* build1-d: every delivered amount (all n of the engine's list, not only the 16 a STATE carries) summed into *sum (> 0 only)
   and, when zero != 0, set to 0 - the refund 0x29DBD0 drops items only for delivered > 0. -1 unreadable, else the count. */
int BdMatsSumZero(void* b, int zero, float* sum)
{
    *sum = 0.0f;
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return -1;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        char* const* arr = *(char* const* const*)(st + kStMatArray);
        if (n > kStMatSane || (n > 0 && arr == 0)) return -1;
        for (unsigned int i = 0; i < n; ++i)
        {
            if (arr[i] == 0) continue;
            float* d = (float*)(arr[i] + kMatDelivered);
            if (*d > 0.0f) *sum += *d;
            if (zero != 0) *d = 0.0f;
        }
        return (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* build1-d (review-build1c LOW walls). T325 fold (Confirmed: B logged wall=1 for the UseableStuff chest - "vt+0x230 is not
   0x558B10" was a negative test): POSITIVE identification by the object's RTTI class name. 1 = the complete object's class
   is exactly `.?AVWallBuilding@@`; 0 = anything else, including an
   unreadable vtable or RTTI. The one wall vtable RVA seen is cached. ANY THREAD, POD only, no allocation. */
volatile LONG g_wallVtRva = 0;
int BdIsWall(void* b)
{
    const unsigned int vt = PolicyVtableRvaPod(b);
    if (vt == 0) return 0;
    const LONG known = ::InterlockedCompareExchange(&g_wallVtRva, 0, 0);
    if (known != 0 && (unsigned int)known == vt) return 1;
    char cls[48];
    cls[0] = 0;
    if (PolicyClassNamePod(vt, cls, (int)sizeof cls) == 0) return 0;
    if (std::strcmp(cls, ".?AVWallBuilding@@") != 0) return 0;
    ::InterlockedExchange(&g_wallVtRva, (LONG)vt);
    return 1;
}
/* an engine std::string (VS2010 layout: 16-byte buffer, size +0x10, capacity +0x18) copied into `out` */
int BdStrPod(const char* s, char* out, int cap)
{
    out[0] = 0;
    __try
    {
        const size_t n = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
        if (n > 4096 || res < n) return 0;
        const char* p = (res >= 16) ? *(const char* const*)s : s;
        int i = 0;
        for (; i < cap - 1 && (size_t)i < n; ++i) out[i] = p[i];
        out[i] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
void* BdPtrAt(const void* p, size_t off)
{
    __try { return *(void* const*)((const char*)p + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdIntAt(const void* p, size_t off, int* out)
{
    __try { *out = *(const int*)((const char*)p + off); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdByteAt(const void* p, size_t off, int* out)
{
    __try { *out = *((const unsigned char*)p + off); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdByteAtOr0(const void* p, size_t off) { int v = 0; return BdByteAt(p, off, &v) != 0 ? v : 0; }   /* review-build1e N3 */
/* review-build1d D2: isDismantled as 0x2A2820 tests it - the piece's OWN +0x162. For every class but WallBuilding that is the
   byte vt+0x228()+2 already gave (`stateDism`, returned unchanged); a wall segment's vt+0x228 is the run's shared state, so
   its own byte is read. ANY THREAD, POD only. */
int BdDismOf(void* b, int stateDism)
{
    if (BdIsWall(b) != 1) return stateDism;
    int own = 0;
    if (!BdByteAt(b, kBOwnDismantled, &own)) return stateDism;
    return own != 0 ? 1 : 0;
}
void BdSidOf(void* b, char* out, int cap)
{
    out[0] = 0;
    void* gd = BdPtrAt(b, kObjData);
    if (gd != 0) BdStrPod((const char*)gd + kGdStringId, out, cap);
}
const std::string* BdFacNamePtr(::Faction* f)
{
    __try { return &f->getName(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
::Faction* BdCharFaction(::Character* c)
{
    __try { return c->getOwnerFaction(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* BdGetData(uintptr_t fn, uintptr_t container, const std::string* sid)
{
    __try { return ((BdGdcGetDataFn)fn)((void*)container, sid); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* BdCallCreate(BdCreateFn fn, void* factory, void* data, float* pos, void* owner, float* rot, bool completed, int* faulted)
{
    /* the recipe: town 0 (the engine picks it), cb 0, furnitureOf 0, isDoorOf 0, save 0, isIndoorsOf 0,
       invisible 0, completed as given (0 for the lever, the PLACE's flag for a copy), foliage 0, floor 0, outside 0 */
    __try { return fn(factory, data, pos, 0, owner, rot, 0, 0, 0, 0, 0, false, completed, false, 0, false); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = 1; return 0; }
}

/* review-build1a B3 minor: the engine's name string is copied into a char buffer by BdStrPod (inside __try), never as a
   raw std::string copy outside SEH */
int BdFacNamePod(void* f, char* out, int cap)
{
    out[0] = 0;
    const std::string* p = BdFacNamePtr((::Faction*)f);
    return p != 0 ? BdStrPod((const char*)p, out, cap) : 0;
}
std::string BdFactionName(void* f)
{
    if (f == 0) return "(none)";
    char n[96];
    return BdFacNamePod(f, n, (int)sizeof n) != 0 ? std::string(n) : std::string("(unreadable)");
}
/* build1-b: the floats createBuilding received (Vector3* / Quaternion*), copied under SEH */
int BdCopyF(const void* src, float* dst, int n)
{
    __try { for (int i = 0; i < n; ++i) dst[i] = ((const float*)src)[i]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* build1-e (answers-build1b R1 "Furniture inputs"): the building a furniture createBuilding puts its piece in, and the form
   its furnitureOf took - 0 = *(host+0x1F0)+0x30 (the host's interior layout), 1 = anything else (the commit's vt+0x40 branch,
   4d6810:176-178, taken on the host's interior layout), 2 = no furnitureOf (isIndoorsOf only). Candidates: isIndoorsOf,
   then the layout's owner *(furnitureOf+0x90) (createBuilding's own override, 57c1e0:232). POD loads only. ANY THREAD. */
int BdHostFormPod(void* fo, void* indoors, void** host, int* form)
{
    *host = 0; *form = 0;
    __try
    {
        void* cand[2];
        cand[0] = indoors;
        cand[1] = (fo != 0) ? *(void* const*)((const char*)fo + kLayoutOwner) : 0;
        for (int i = 0; i < 2; ++i)
        {
            if (cand[i] == 0 || fo == 0) continue;
            const char* in = *(const char* const*)((const char*)cand[i] + kBInterior);
            if (in != 0 && fo == (const void*)(in + kInteriorLayout)) { *host = cand[i]; *form = 0; return 1; }
        }
        if (fo != 0)
        {
            for (int i = 0; i < 2; ++i)
                if (cand[i] != 0 && *(void* const*)((const char*)cand[i] + kBInterior) != 0) { *host = cand[i]; *form = 1; return 1; }
            return 0;
        }
        if (indoors != 0) { *host = indoors; *form = 2; return 1; }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *host = 0; *form = 0; return 0; }
}
/* build1-e: the host layout's vt+0x40, the commit's other furnitureOf form. MAIN THREAD (K2 safe point). */
void* BdLayoutSubPod(void* layout, int* faulted)
{
    __try { return ((BdLayoutSubFn)((*(void***)layout)[kVtLayoutSub / 8]))(layout); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = 1; return 0; }
}
/* build1-e: the host's orientation (Ogre w,x,y,z) through vt+0xC0, as the commit reads it. MAIN THREAD. */
int BdOrientPod(void* b, float* q)
{
    __try
    {
        float tmp[4];
        tmp[0] = 1.0f; tmp[1] = 0.0f; tmp[2] = 0.0f; tmp[3] = 0.0f;
        const float* r = (const float*)((BdOrientFn)((*(void***)b)[kVtOrient / 8]))(b, tmp);
        if (r == 0) return 0;
        for (int i = 0; i < 4; ++i) q[i] = r[i];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* build1-e: createBuilding with a host - the recipe's call with furnitureOf, isIndoorsOf, floor and outside as given */
void* BdCallCreateEx(BdCreateFn fn, void* factory, void* data, float* pos, void* owner, float* rot, void* fo, void* indoors,
                     bool completed, int floor, bool outside, int* faulted)
{
    __try { return fn(factory, data, pos, 0, owner, rot, 0, fo, 0, 0, indoors, false, completed, false, floor, outside); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = 1; return 0; }
}
/* build1-e: v rotated by the INVERSE of the unit quaternion q (Ogre w,x,y,z): the host's local frame (answers R1) */
void BdRotateInv(const float* q, const float* v, float* out)
{
    const float w = q[0], x = -q[1], y = -q[2], z = -q[3];
    const float ux = y * v[2] - z * v[1], uy = z * v[0] - x * v[2], uz = x * v[1] - y * v[0];
    const float uux = y * uz - z * uy, uuy = z * ux - x * uz, uuz = x * uy - y * ux;
    out[0] = v[0] + 2.0f * (w * ux + uux);
    out[1] = v[1] + 2.0f * (w * uy + uuy);
    out[2] = v[2] + 2.0f * (w * uz + uuz);
}

void BdLogCapped(const char* line)
{
    if (g_lines < kLineCap) { ++g_lines; DebugLog(std::string(line)); }
    else ++g_linesSuppressed;
}
void BdLogProg(const char* line)
{
    if (g_progLines < kProgLineCap) { ++g_progLines; DebugLog(std::string(line)); }
    else ++g_progSuppressed;
}
void BdLogState(const char* line)   /* build1-c: its own cap, so the engine's own progress lines never starve it */
{
    if (g_stateLines < kStateLineCap) { ++g_stateLines; DebugLog(std::string(line)); }
    else ++g_stateSuppressed;
}
/* build1-c: a difference in any delivered amount (a missing slot reads as 0) */
int BdMatsDiffer(int na, const float* a, int nb, const float* b)
{
    const int n = (na > nb) ? na : nb;
    for (int i = 0; i < n && i < (int)coopbuild::kBuildMaxMats; ++i)
    {
        const float x = (i < na) ? a[i] : 0.0f, y = (i < nb) ? b[i] : 0.0f;
        if (std::fabs(x - y) > 0.0001f) return 1;
    }
    return 0;
}

// PROBE-START: P082
// ANY THREAD, no allocation: one POD record into the queue BuildTick drains.
void BdQueueEvent(int kind, void* b, float amount, int onMain, int dismBefore)
{
    BdEvent e;
    std::memset(&e, 0, sizeof e);
    e.kind = kind;
    e.amount = amount;
    e.onMain = onMain;
    e.dismBefore = dismBefore;   /* build1-d */
    e.keyOk = ObjectPositionKey(b, e.key, kKeyCap, "", 3, 0);
    if (e.keyOk == 0) e.key[0] = 0;
    e.stateOk = BdReadState(b, &e.progress, &e.needed, &e.complete, &e.dismantled, &e.destroyed);
    if (e.stateOk != 0) e.dismantled = BdDismOf(b, e.dismantled);   /* review-build1d D2: a wall segment's own +0x162 */
    int matTotal = -1;
    e.nMats = BdReadMats(b, e.mats, (int)coopbuild::kBuildMaxMats, &matTotal);   /* build1-c */
    if (::InterlockedCompareExchange(&g_qInit, 0, 0) == 0) { ::InterlockedIncrement64(&g_queueDropped); return; }
    ::EnterCriticalSection(&g_qcs);
    const int ok = (g_qn < kQueueCap) ? 1 : 0;
    if (ok) g_q[g_qn++] = e;
    ::LeaveCriticalSection(&g_qcs);
    ::InterlockedIncrement64(ok ? &g_queued : &g_queueDropped);
}
// PROBE-END: P082

/* house1b: the owed hand-offs' file - the placer's own, in the world folder beside the take journal */
std::string BdOwedPath()
{
    const std::string d = StoreNotebookDir();
    const std::string me = StorePlayerKey();
    if (d.empty() || me.empty()) return std::string();
    return d + "\\build_handoffs_owed_" + me + ".txt";
}
void BdOwedSave()
{
    const std::string path = BdOwedPath();
    if (path.empty()) { ++g_owedSaveFailed; return; }
    if (path != g_owedFor)   /* house2 (item 3): the map was not read from this file (never loaded, not readable, no path at load) */
    {
        ++g_owedSaveRefused;
        char sl[360];
        _snprintf_s(sl, sizeof(sl), _TRUNCATE, "[BUILD] owed hand-offs save REFUSED: the map was not loaded from %.200s (owedSaveRefused=%lld)",
                    path.c_str(), g_owedSaveRefused);
        BdLogCapped(sl);
        return;
    }
    const std::string tmp = path + ".tmp";   /* house2 (item 2): written aside, then renamed over the file in one step */
    std::FILE* f = U8fopen(tmp.c_str(), "wb");
    if (f == 0) { ++g_owedSaveFailed; return; }
    static const char hx[] = "0123456789abcdef";
    int bad = 0;
    for (std::map<std::string, BdOwed>::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it)
    {
        std::vector<char> b;
        if (!coopbuild::EncodeBuild(&b, it->second.m)) continue;
        std::string line(it->second.acked != 0 ? "1 " : "0 ");
        for (size_t i = 0; i < b.size(); ++i) { const unsigned char ch = (unsigned char)b[i]; line += hx[ch >> 4]; line += hx[ch & 15]; }
        line += "\n";
        if (std::fwrite(line.data(), 1, line.size(), f) != line.size()) bad = 1;
    }
    if (std::fflush(f) != 0) bad = 1;
    if (::_commit(::_fileno(f)) != 0) bad = 1;   /* house2 fold (LOW b): on the disk before the rename */
    if (std::fclose(f) != 0) bad = 1;
    if (bad != 0 || U8MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        ++g_owedSaveFailed;   /* the old file stays whole */
        U8DeleteFile(tmp.c_str());
    }
}
int BdHexVal(char ch) { return (ch >= '0' && ch <= '9') ? ch - '0' : ((ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1); }
/* MAIN THREAD: (re)load when this game's world folder / player key names another file than the one loaded */
void BdOwedLoad()
{
    const std::string path = BdOwedPath();
    if (path.empty())   /* house2 (item 3): no file named - the map stays empty and nothing is saved; a later path loads its file */
    {
        g_owed.clear();
        g_owedFor.clear();
        g_owedTried.clear();
        g_owedErasedUnread.clear();   /* house2 fold 2 */
        return;
    }
    if (path == g_owedFor) return;
    if (path != g_owedTried)   /* house2 fold (MED1): another file - cleared once; a retry of the same file keeps the rows recorded meanwhile */
    {
        g_owed.clear();
        g_owedTried = path;
        g_owedErasedUnread.clear();   /* house2 fold 2: another file */
    }
    g_owedFor.clear();   /* house2 (item 3): set only once the file is read or confirmed absent - BdOwedSave refuses otherwise */
    std::FILE* f = U8fopen(path.c_str(), "rb");
    if (f == 0)
    {
        const DWORD at = U8GetFileAttributes(path.c_str());
        const DWORD er = ::GetLastError();
        if (at == INVALID_FILE_ATTRIBUTES && (er == ERROR_FILE_NOT_FOUND || er == ERROR_PATH_NOT_FOUND)) { g_owedFor = path; return; }
        ++g_owedLoadUnread;
        char ul[360];
        _snprintf_s(ul, sizeof(ul), _TRUNCATE, "[BUILD] owed hand-offs file NOT READ (open failed, attr=0x%lx err=%lu) - saves refused until it reads: %.200s",
                    (unsigned long)at, (unsigned long)er, path.c_str());
        BdLogCapped(ul);
        return;
    }
    std::map<std::string, BdOwed> fileRows;   /* house2 fold (MED1): read aside, then merged - an in-memory row wins */
    char buf[1024];
    while (std::fgets(buf, (int)sizeof buf, f) != 0)
    {
        if ((buf[0] != '0' && buf[0] != '1') || buf[1] != ' ') continue;
        std::vector<char> b;
        const char* h = buf + 2;
        while (BdHexVal(h[0]) >= 0 && BdHexVal(h[1]) >= 0) { b.push_back((char)(BdHexVal(h[0]) * 16 + BdHexVal(h[1]))); h += 2; }
        coopbuild::BuildMsg m;
        if (b.empty() || coopbuild::DecodeBuildSaved(&b[0], b.size(), &m) != coopbuild::kBuildDecodeOk || m.kind != coopbuild::kBuildPlace
            || m.ownerSlot == coopbuild::kBuildOwnerSender) continue;
        BdOwed& o = fileRows[m.key];
        o.m = m; o.acked = (buf[0] == '1') ? 1 : 0;
    }
    if (std::ferror(f) != 0)   /* house2 fold (LOW a): a read error - nothing of the file is taken, saves stay refused, retried at the next load */
    {
        std::fclose(f);
        g_owedFor.clear();
        ++g_owedLoadUnread;
        char el[320];
        _snprintf_s(el, sizeof(el), _TRUNCATE, "[BUILD] owed hand-offs file READ ERROR - nothing taken, saves refused until it reads: %.200s", path.c_str());
        BdLogCapped(el);
        return;
    }
    std::fclose(f);
    g_owedFor = path;
    unsigned int kept = 0, dropped = 0;
    for (std::map<std::string, BdOwed>::const_iterator it = fileRows.begin(); it != fileRows.end(); ++it)
    {
        const int mr = coopbuild::BuildOwedMergeRow(&g_owed, &g_owedErasedUnread, it->first, it->second);
        if (mr == 1) ++g_owedLoaded;
        else if (mr == 2) ++dropped;   /* house2 fold 2: a REMOVE ended it while the file was unread */
        else ++kept;
    }
    g_owedErasedUnread.clear();
    if (dropped != 0) BdOwedSave();   /* the file loses the ended rows now */
    char line[360];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] owed hand-offs loaded: %u from %.200s (%u in-memory rows kept over the file's)",
                (unsigned int)g_owed.size(), path.c_str(), kept);
    DebugLog(std::string(line));
}
/* ---- help1: the engine reads / writes (POD, guarded) ---- */
/* each material's total (+8; -1 for an empty slot). -1 unreadable, else how many were copied */
int BdReadMatTotals(void* b, float* out, int cap)
{
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return -1;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        const char* const* arr = *(const char* const* const*)(st + kStMatArray);
        if (n > kStMatSane || (n > 0 && arr == 0)) return -1;
        int k = 0;
        for (; k < cap && (unsigned int)k < n; ++k) out[k] = (arr[k] != 0) ? *(const float*)(arr[k] + 8) : -1.0f;
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* material i's GameData (BuildMaterial +0) - the item a refund hands back; 0 unreadable */
void* BdMatDataPod(void* b, int i)
{
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0 || i < 0) return 0;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        const char* const* arr = *(const char* const* const*)(st + kStMatArray);
        if (n > kStMatSane || arr == 0 || (unsigned int)i >= n || arr[i] == 0) return 0;
        return *(void* const*)arr[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the copy's needed (state+0x28), raised around the helper's own engine call and restored at once (design: the copy never completes) */
int BdSetNeededPod(void* b, float v)
{
    __try
    {
        char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return 0;
        *(float*)(st + 0x28) = v;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* help1 fold (review LOW 11): the copy's needed put back to v - only while it still holds the raised value (an engine write inside the
   call wins). 1 = put back */
int BdRestoreNeededPod(void* b, float raisedTo, float v)
{
    __try
    {
        char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return 0;
        if (*(float*)(st + 0x28) != raisedTo) return 0;
        *(float*)(st + 0x28) = v;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* ---- help1 fold 3 (findings 2, 3, 5): work belongs to the placement it was done on ------------------------------------------ */
/* THE OWNER'S GAME: a placement's applied / refused rows become (or refresh) its (key, nonce) gone record - written into pp.build as a
   gone marker; a HELP_GONE from it waits until that has landed. markDirty 0 = from inside BuildOwnRows (the same pass writes it).
   1 = kept, 0 = the cap is full (goneRecFull; not kept) */
int BdGoneNote(const std::string& key, unsigned int nonce, const std::vector<coopbuild::BuildHelpAck>& acks, int markDirty)
{
    const BdPlKey gk(key, nonce);
    std::map<BdPlKey, BdGoneRec>::iterator it = g_ownGone.find(gk);
    if (it == g_ownGone.end())
    {
        if (g_ownGone.size() >= kGoneRecCap)
        {
            ++g_goneRecFull;
            char line[320];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help gone record key=%.63s nonce=%08x NOT KEPT - %u records (the cap); later work for it is answered from the row's landed rows, else GONE applied 0 goneRecFull=%lld",
                        key.c_str(), nonce, (unsigned int)g_ownGone.size(), g_goneRecFull);
            BdLogHelp(line);
            return 0;
        }
        it = g_ownGone.insert(std::make_pair(gk, BdGoneRec())).first;
    }
    else if (coopbuild::BuildHelpAcksSame(it->second.acks, acks)) return 1;
    it->second.acks = acks;
    it->second.dirty = 1;
    g_goneAwait.insert(gk);
    if (markDirty != 0) BdOwnDirty(2);
    return 1;
}
/* THE OWNER'S GAME (finding 2): a NEW placement at a key whose registry row it reuses - the earlier placement's rows (applied, refused;
   this world's row only) go to that placement's own gone record; the new piece starts with no helper rows and no refused tail. Rows
   pp.build brought back stay: BdHelpAcksRead hands them only to the placement of their nonce (a restored piece takes its PLACE's) */
void BdHelpPlacementEnds(const std::string& key, BdReg& r)
{
    if (r.own != 0 && r.via != 3) BdRestoreNonce(key, r, 1);   /* help1 fold 4: its rows go to its REAL (key, nonce) record */
    const std::vector<coopbuild::BuildHelpAck>* ga = BdHelpAcksRead(key, r);
    if (r.gen == g_bdGen && ga != 0 && !ga->empty()) BdGoneNote(key, r.nonce, *ga, 1);
    r.helpAcks.clear(); r.helpAcksLanded.clear(); r.helpAcksWriting.clear();
    r.helpAckDirty = 0; r.helpAckWait = 0; r.helpForce = 0;
    g_helpAckAwait.erase(key);
}
/* THE HELPER'S GAME: entries of a row whose placement was unknown (0) take the row's nonce once it is known */
void BdHelpEntriesAdopt(BdHelpRow& h)
{
    if (h.nonce == 0) return;
    for (size_t i = 0; i < h.owed.size(); ++i) if (h.owed[i].nonce == 0) h.owed[i].nonce = h.nonce;
}
/* THE HELPER'S GAME: a row of the SAME placement joins another (the owed file, memory, a replaced placement's rows): the higher next seq
   and handed-back counts, the sids, position, owner slot and entries it lacks (seq order kept, one per seq) */
void BdHelpRowJoin(BdHelpRow& r, const BdHelpRow& from)
{
    if (r.nonce == 0 && from.nonce != 0) { r.nonce = from.nonce; BdHelpEntriesAdopt(r); }
    if (from.nextSeq > r.nextSeq) r.nextSeq = from.nextSeq;
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) if (from.refunded[i] > r.refunded[i]) r.refunded[i] = from.refunded[i];
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) if (r.sid[i].empty()) r.sid[i] = from.sid[i];
    if (r.posOk == 0 && from.posOk != 0) { r.pos[0] = from.pos[0]; r.pos[1] = from.pos[1]; r.pos[2] = from.pos[2]; r.posOk = 1; }
    if (r.ownerSlot < 0) r.ownerSlot = from.ownerSlot;
    for (size_t e = 0; e < from.owed.size(); ++e)
    {
        const coopbuild::BuildHelpEntry& fe = from.owed[e];
        size_t at = 0;
        int same = 0;
        while (at < r.owed.size() && r.owed[at].seq <= fe.seq) { if (r.owed[at].seq == fe.seq) same = 1; ++at; }
        if (same == 0) r.owed.insert(r.owed.begin() + (std::ptrdiff_t)at, fe);
    }
}
/* THE HELPER'S GAME: a replaced placement's row joins the (key, nonce) rows of its map */
void BdHelpOldJoin(std::map<BdPlKey, BdHelpRow>& into, const std::string& key, const BdHelpRow& from)
{
    std::map<BdPlKey, BdHelpRow>::iterator o = into.find(BdPlKey(key, from.nonce));
    if (o == into.end()) into[BdPlKey(key, from.nonce)] = from;
    else BdHelpRowJoin(o->second, from);
}
struct BdHelpSidLine { std::string key; int i; std::string sid; unsigned int nonce; };   /* help1 fold 3: an owed file S line, placed once every row is read */
/* a copy's baseline: its delivered amounts now (as made, or after the mod's own write) */
void BdHelpBaseRead(BdReg& r, void* b)
{
    float m[coopbuild::kBuildMaxMats];
    int tot = -1;
    const int n = BdReadMats(b, m, (int)coopbuild::kBuildMaxMats, &tot);
    if (n < 0) { r.helpBaseOk = 0; return; }
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) r.helpBase[i] = (i < n) ? m[i] : 0.0f;
    r.helpBaseOk = 1;
}
void BdHelpMsgOf(const std::string& key, const coopbuild::BuildHelpEntry& e, coopbuild::BuildMsg* m)
{
    m->kind = coopbuild::kBuildHelpWork; m->key = key; m->seq = e.seq; m->progress = e.dP; m->reset = e.reset;
    m->nonce = e.nonce;   /* help1 fold 3: the placement it was done on */
    m->nMats = e.n;
    for (int k = 0; k < (int)e.n && k < (int)coopbuild::kBuildMaxMats; ++k) m->mats[k] = e.d[k];
}
/* ---- help1: the helper's owed work, in the world folder (build_help_owed_<player key>.txt; the BdOwedSave pattern: written aside,
   committed, renamed over the file; never saved unless read from that file). Per piece "K <hex key> <nextSeq> <16 refunded> <nonce>"
   (help1 fold 2: the nonce - absent in an older file, read as 0), one
   "S <hex key> <i> <hex sid>" per material whose item is known (help1 fold, MED 3) and one "W <hex HELP_WORK>" per unconfirmed entry.
   help1 fold 3: an S line ends with its row's nonce; the HELP_WORK carries its entry's nonce (an older line without it reads as the K
   row's); a row of a replaced placement (g_helpOld) is written "O ..." exactly like K.
   help1 fold (review MED 8): saved at most every 5 s (BdHelpSaveSoon), at link-down, world teardown and quit; at once after a refund. */
std::string BdHelpPath()
{
    const std::string d = StoreNotebookDir();
    const std::string me = StorePlayerKey();
    if (d.empty() || me.empty()) return std::string();
    return d + "\\build_help_owed_" + me + ".txt";
}
void BdHelpHex(const char* p, size_t n, std::string* out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) { const unsigned char ch = (unsigned char)p[i]; *out += hx[ch >> 4]; *out += hx[ch & 15]; }
}
/* help1 fold 3: one row's text - "<K|O> <hex key> <nextSeq> <16 refunded> <nonce>", its "S <hex key> <i> <hex sid> <nonce>" lines and one
   "W <hex HELP_WORK>" per unconfirmed entry (the HELP_WORK carries the entry's nonce) */
void BdHelpSaveRow(char letter, const std::string& key, const BdHelpRow& h, std::string* out)
{
    std::string& line = *out;
    line += letter;
    line += ' ';
    BdHelpHex(key.data(), key.size(), &line);
    char num[40];
    _snprintf_s(num, sizeof(num), _TRUNCATE, " %u", h.nextSeq);
    line += num;
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) { _snprintf_s(num, sizeof(num), _TRUNCATE, " %d", h.refunded[i]); line += num; }
    _snprintf_s(num, sizeof(num), _TRUNCATE, " %u", h.nonce);   /* help1 fold 2 */
    line += num;
    line += "\n";
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i)
    {
        if (h.sid[i].empty()) continue;
        line += "S ";
        BdHelpHex(key.data(), key.size(), &line);
        _snprintf_s(num, sizeof(num), _TRUNCATE, " %d ", i);
        line += num;
        BdHelpHex(h.sid[i].data(), h.sid[i].size(), &line);
        _snprintf_s(num, sizeof(num), _TRUNCATE, " %u", h.nonce);   /* help1 fold 3: the row it belongs to */
        line += num;
        line += "\n";
    }
    for (size_t e = 0; e < h.owed.size(); ++e)
    {
        coopbuild::BuildMsg m;
        BdHelpMsgOf(key, h.owed[e], &m);
        std::vector<char> b;
        if (!coopbuild::EncodeBuild(&b, m)) continue;
        line += "W ";
        BdHelpHex(&b[0], b.size(), &line);
        line += "\n";
    }
}
void BdHelpSave()
{
    const std::string path = BdHelpPath();
    g_helpSaveDirty = 1;   /* help1 fold (MED 8): cleared only by a save that landed - a failed one is tried again by BdHelpTick */
    if (path.empty() || path != g_helpFor) { ++g_helpSaveFailed; return; }
    const std::string tmp = path + ".tmp";
    std::FILE* f = U8fopen(tmp.c_str(), "wb");
    if (f == 0) { ++g_helpSaveFailed; return; }
    int bad = 0;
    for (std::map<std::string, BdHelpRow>::const_iterator it = g_help.begin(); it != g_help.end(); ++it)
    {
        std::string line;
        BdHelpSaveRow('K', it->first, it->second, &line);
        if (std::fwrite(line.data(), 1, line.size(), f) != line.size()) bad = 1;
    }
    for (std::map<BdPlKey, BdHelpRow>::const_iterator it = g_helpOld.begin(); it != g_helpOld.end(); ++it)   /* help1 fold 3: rows of a replaced placement */
    {
        std::string line;
        BdHelpSaveRow('O', it->first.first, it->second, &line);
        if (std::fwrite(line.data(), 1, line.size(), f) != line.size()) bad = 1;
    }
    if (std::fflush(f) != 0) bad = 1;
    if (::_commit(::_fileno(f)) != 0) bad = 1;
    if (std::fclose(f) != 0) bad = 1;
    if (bad != 0 || U8MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        ++g_helpSaveFailed;   /* the old file stays whole */
        U8DeleteFile(tmp.c_str());
        return;
    }
    g_helpSaveDirty = 0;   /* help1 fold (MED 8) */
}
void BdHelpSaveSoon() { g_helpSaveDirty = 1; }   /* help1 fold (review MED 8): BdHelpTick saves at most every kHelpSaveMs */
/* MAIN THREAD: (re)load when the world folder / player key names another file than the one loaded. A file row MERGES into a row
   recorded in memory meanwhile: the higher nextSeq and refunded counts, the file's entries the memory row lacks (seq order kept). */
void BdHelpLoad()
{
    const std::string path = BdHelpPath();
    if (path.empty() || path == g_helpFor) return;
    if (path != g_helpTried) { if (!g_helpTried.empty()) { g_help.clear(); g_helpOld.clear(); } g_helpTried = path; }   /* another world: its own work (fold 3: its replaced placements' rows too) */
    std::FILE* f = U8fopen(path.c_str(), "rb");
    if (f == 0)
    {
        const DWORD at = U8GetFileAttributes(path.c_str());
        const DWORD er = ::GetLastError();
        if (at == INVALID_FILE_ATTRIBUTES && (er == ERROR_FILE_NOT_FOUND || er == ERROR_PATH_NOT_FOUND)) { g_helpFor = path; return; }
        ++g_helpLoadUnread;
        char ul[360];
        _snprintf_s(ul, sizeof(ul), _TRUNCATE, "[BUILD] help owed file NOT READ (open failed, attr=0x%lx err=%lu) - saves refused until it reads: %.200s",
                    (unsigned long)at, (unsigned long)er, path.c_str());
        BdLogHelp(ul);
        return;
    }
    std::map<std::string, BdHelpRow> fileRows;   /* the K rows: the key's current placement as the file had it */
    std::map<BdPlKey, BdHelpRow> fileOld;        /* help1 fold 3: the O rows (a replaced placement) and entries of another nonce */
    std::vector<BdHelpSidLine> sids;             /* help1 fold 3: placed once every row is read */
    std::vector<coopbuild::BuildMsg> works;
    char buf[1024];
    while (std::fgets(buf, (int)sizeof buf, f) != 0)
    {
        if ((buf[0] != 'K' && buf[0] != 'O' && buf[0] != 'W' && buf[0] != 'S') || buf[1] != ' ') continue;
        std::vector<char> b;
        const char* h = buf + 2;
        while (BdHexVal(h[0]) >= 0 && BdHexVal(h[1]) >= 0) { b.push_back((char)(BdHexVal(h[0]) * 16 + BdHexVal(h[1]))); h += 2; }
        if (b.empty()) continue;
        if (buf[0] == 'S')   /* help1 fold: a material's item sid; fold 3: then its row's nonce (absent in an older file: the K row's) */
        {
            const std::string key(b.begin(), b.end());
            std::istringstream is((std::string(h)));
            int i = -1;
            std::string hs;
            if (key.size() > coopbuild::kBuildMaxKey || !(is >> i >> hs) || i < 0 || i >= (int)coopbuild::kBuildMaxMats) continue;
            BdHelpSidLine sl;
            sl.key = key; sl.i = i; sl.nonce = 0;
            for (size_t q = 0; q + 1 < hs.size() && BdHexVal(hs[q]) >= 0 && BdHexVal(hs[q + 1]) >= 0; q += 2) sl.sid += (char)(BdHexVal(hs[q]) * 16 + BdHexVal(hs[q + 1]));
            unsigned int nn = 0;
            if (is >> nn) sl.nonce = nn;
            if (!sl.sid.empty() && sl.sid.size() < 128) sids.push_back(sl);
            continue;
        }
        if (buf[0] == 'K' || buf[0] == 'O')   /* help1 fold 3: O = a replaced placement's row, the K format */
        {
            const std::string key(b.begin(), b.end());
            if (key.size() > coopbuild::kBuildMaxKey) continue;
            BdHelpRow f1;
            std::istringstream is((std::string(h)));
            unsigned int ns = 0;
            if ((is >> ns) && ns > f1.nextSeq) f1.nextSeq = ns;
            for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) { int v = 0; if ((is >> v) && v > f1.refunded[i]) f1.refunded[i] = v; }
            unsigned int nn = 0;
            if ((is >> nn) && nn != 0) f1.nonce = nn;   /* help1 fold 2: absent in an older file (0) */
            if (buf[0] == 'K')
            {
                std::map<std::string, BdHelpRow>::iterator k = fileRows.find(key);
                if (k == fileRows.end()) fileRows[key] = f1; else BdHelpRowJoin(k->second, f1);
            }
            else if (f1.nonce != 0) BdHelpOldJoin(fileOld, key, f1);
            continue;
        }
        coopbuild::BuildMsg m;
        int dr = coopbuild::DecodeBuild(&b[0], b.size(), &m);
        if (dr != coopbuild::kBuildDecodeOk) { b.resize(b.size() + 4, 0); dr = coopbuild::DecodeBuild(&b[0], b.size(), &m); }   /* help1 fold 3: a line written before the nonce - nonce 0 (the K row's) */
        if (dr != coopbuild::kBuildDecodeOk || m.kind != coopbuild::kBuildHelpWork) continue;
        works.push_back(m);
    }
    if (std::ferror(f) != 0)
    {
        std::fclose(f);
        ++g_helpLoadUnread;
        char el[320];
        _snprintf_s(el, sizeof(el), _TRUNCATE, "[BUILD] help owed file READ ERROR - nothing taken, saves refused until it reads: %.200s", path.c_str());
        BdLogHelp(el);
        return;
    }
    std::fclose(f);
    g_helpFor = path;
    for (size_t w = 0; w < works.size(); ++w)   /* help1 fold 3: each entry to the row of the placement it was done on */
    {
        const coopbuild::BuildMsg& m = works[w];
        coopbuild::BuildHelpEntry e;
        e.seq = m.seq; e.dP = m.progress; e.reset = m.reset; e.n = m.nMats; e.nonce = m.nonce;
        e.loaded = 1;   /* help1 fold 2 (re-check MED 4): it may have gone before the restart - frozen, new work never joins it */
        for (int k = 0; k < (int)m.nMats; ++k) e.d[k] = m.mats[k];
        std::map<std::string, BdHelpRow>::iterator kr = fileRows.find(m.key);
        BdHelpRow* r = 0;
        if (kr == fileRows.end() || e.nonce == 0 || kr->second.nonce == 0 || kr->second.nonce == e.nonce) r = &fileRows[m.key];
        else r = &fileOld[BdPlKey(m.key, e.nonce)];
        if (r->nonce == 0 && e.nonce != 0) r->nonce = e.nonce;
        r->owed.push_back(e);
        if (r->nextSeq <= e.seq) r->nextSeq = e.seq + 1;
    }
    for (size_t q = 0; q < sids.size(); ++q)   /* help1 fold 3: a sid to its own placement's row */
    {
        const BdHelpSidLine& sl = sids[q];
        std::map<std::string, BdHelpRow>::iterator kr = fileRows.find(sl.key);
        BdHelpRow* r = 0;
        if (sl.nonce != 0 && (kr == fileRows.end() || kr->second.nonce != sl.nonce))
        {
            std::map<BdPlKey, BdHelpRow>::iterator o = fileOld.find(BdPlKey(sl.key, sl.nonce));
            if (o != fileOld.end()) r = &o->second;
        }
        if (r == 0) r = &fileRows[sl.key];
        r->sid[sl.i] = sl.sid;
    }
    unsigned int entries = 0;
    for (std::map<std::string, BdHelpRow>::iterator it = fileRows.begin(); it != fileRows.end(); ++it)
    {
        BdHelpEntriesAdopt(it->second);
        entries += (unsigned int)it->second.owed.size();
        std::map<std::string, BdHelpRow>::iterator mem = g_help.find(it->first);
        if (mem == g_help.end()) { g_help[it->first] = it->second; continue; }
        if (coopbuild::BuildHelpRowStale(mem->second.nonce, it->second.nonce))   /* help1 fold 2: the file row helped another placement at the key - memory's is the live one; */
        {                                                                        /* fold 3: the file's entries stay owed to theirs */
            if (!it->second.owed.empty()) BdHelpOldJoin(g_helpOld, it->first, it->second);
            continue;
        }
        BdHelpRowJoin(mem->second, it->second);
    }
    for (std::map<BdPlKey, BdHelpRow>::iterator it = fileOld.begin(); it != fileOld.end(); ++it)   /* help1 fold 3: rows of a replaced placement */
    {
        BdHelpEntriesAdopt(it->second);
        entries += (unsigned int)it->second.owed.size();
        std::map<std::string, BdHelpRow>::iterator mem = g_help.find(it->first.first);
        if (mem != g_help.end() && mem->second.nonce == it->first.second) BdHelpRowJoin(mem->second, it->second);
        else if (!it->second.owed.empty()) BdHelpOldJoin(g_helpOld, it->first.first, it->second);
    }
    char line[360];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help owed loaded: %u pieces, %u unconfirmed entries from %.200s",
                (unsigned int)fileRows.size(), entries, path.c_str());
    DebugLog(std::string(line));
}
/* help1 fold (review MED 5/6): the helper's record (pp.help) joins a row - the higher next seq and given counts; a record row of another
   owner slot at the same key is not joined; help1 fold 3: only the record row of the row's own placement (nonce) */
void BdHelpRecMerge(const std::string& key, BdHelpRow& h)
{
    std::map<BdPlKey, coopown::HelpRecRow>::const_iterator rc = g_helpRec.find(BdPlKey(key, h.nonce));   /* help1 fold 3: pp.help keeps one row per (key, nonce) */
    if (rc == g_helpRec.end()) return;
    if (h.ownerSlot >= 0 && rc->second.slot >= 0 && rc->second.slot != h.ownerSlot) return;
    if (rc->second.nextSeq > h.nextSeq) h.nextSeq = rc->second.nextSeq;
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats && i < (int)coopown::kHelpRecMats; ++i)
        if (rc->second.given[i] > h.refunded[i]) h.refunded[i] = rc->second.given[i];
    if (rc->second.goneSeq == 0 || !coopbuild::BuildHelpFloorApplies(h.nonce, rc->second.nonce)) return;   /* help1 fold 3 (finding 6): the settled floor only when both nonces are known and equal */
    size_t dropped = 0;   /* help1 fold 2 (re-check LOW 5): an entry READ BACK from the owed file at or below the settled floor was handed back by an earlier GONE */
    for (size_t i = 0; i < h.owed.size(); )
    {
        if (h.owed[i].loaded != 0 && h.owed[i].seq <= rc->second.goneSeq) { h.owed.erase(h.owed.begin() + (std::ptrdiff_t)i); ++dropped; }
        else ++i;
    }
    if (dropped == 0) return;
    g_helpFloorDropped += (long long)dropped;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help key=%.63s: %u entries read back from the owed file at or below seq %u were settled by an earlier HELP_GONE (pp.help) - dropped, never handed back twice",
                key.c_str(), (unsigned int)dropped, rc->second.goneSeq);
    BdLogHelp(line);
    BdHelpSaveSoon();
}
/* help1 fold (MED 5/6): items are handed back only once the helper's own records were read (or there is no writer, so no record) -
   never twice after a lost owed file */
int BdHelpRefundsReady() { return (g_helpRecState == 1 || !OwnWriterOn()) ? 1 : 0; }
/* help1 fold 2 (re-check HIGH 1): the row helped ANOTHER placement at this key (the owner removed it and placed a new piece there).
   help1 fold 3 (finding 2): work belongs to the placement it was done on - the old row (its unconfirmed entries, its unsent work as one
   more entry, its items-handed-back counts, sids) moves whole to g_helpOld under its own nonce: sent on, settled only by the owner's
   HELP_GONE for that nonce (applied / refused from the owner's record of it). The key's row starts EMPTY for the new placement: next seq
   from the owner's STATE, handed-back counts from 0, its own pp.help row (the old placement's stays - one per (key, nonce)). */
void BdHelpNonceFit(const std::string& key, BdHelpRow& h, unsigned int nonce)
{
    if (coopbuild::BuildHelpRowStale(h.nonce, nonce))
    {
        ++g_helpReplaced;
        BdHelpRow old = h;
        if (old.pendAny != 0)
        {   /* recorded on the old copy, not yet an entry: an entry of the old placement (never joined into the new piece's work) */
            coopbuild::BuildHelpEntry ne;
            ne.seq = old.nextSeq++; ne.nonce = old.nonce; ne.reset = (unsigned char)(old.pendReset != 0 ? 1 : 0);
            ne.dP = (old.pendP > coopbuild::kBuildHelpMax) ? coopbuild::kBuildHelpMax : old.pendP;
            ne.n = (unsigned char)((old.pendN > (int)coopbuild::kBuildMaxMats) ? (int)coopbuild::kBuildMaxMats : old.pendN);
            for (int i = 0; i < (int)ne.n; ++i) ne.d[i] = (old.pendM[i] > coopbuild::kBuildHelpMax) ? coopbuild::kBuildHelpMax : old.pendM[i];
            old.owed.push_back(ne);
            old.pendAny = 0; old.pendP = 0.0f; old.pendN = 0; old.pendReset = 0;
        }
        if (old.owed.empty() && coopbuild::BuildHelpOldKeep(old.owed.size(), old.refundDue) != 0)
        {   /* help1 fold 4 (leftover 3): no work owed, a refund of that placement still due - one empty entry brings back its HELP_GONE (the refused tail) */
            coopbuild::BuildHelpEntry pe;
            pe.seq = old.nextSeq++; pe.nonce = old.nonce;
            old.owed.push_back(pe);
            ++g_helpOldProbe;
        }
        BdHelpEntriesAdopt(old);
        const unsigned int moved = (unsigned int)old.owed.size();
        if (coopbuild::BuildHelpOldKeep(old.owed.size(), old.refundDue) != 0) { BdHelpOldJoin(g_helpOld, key, old); ++g_helpMovedOld; }
        char line[420];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help row key=%.63s is stale: the owner placed a new piece at this key (nonce %08x, the row helped %08x) - its %u unconfirmed entries stay owed to that placement (sent on under its nonce, settled by its HELP_GONE); the new piece's row starts empty; replaced=%lld",
                    key.c_str(), nonce, old.nonce, moved, g_helpReplaced);
        BdLogHelp(line);
        BdHelpRow fresh;
        fresh.ownerSlot = old.ownerSlot; fresh.posOk = old.posOk;
        for (int i = 0; i < 3; ++i) fresh.pos[i] = old.pos[i];
        h = fresh;
        if (g_helpRecDirty < 1) g_helpRecDirty = 1;
        BdHelpSaveSoon();
    }
    if (nonce != 0 && h.nonce != nonce) { h.nonce = nonce; BdHelpEntriesAdopt(h); }   /* fold 3: entries of an unknown placement take it */
}
BdHelpRow& BdHelpRowFor(const std::string& key, const BdReg& r)
{
    if (g_help.find(key) == g_help.end()) BdHelpLoad();
    BdHelpRow& h = g_help[key];
    if (r.ownerSlot >= 0) h.ownerSlot = r.ownerSlot;
    BdHelpNonceFit(key, h, r.nonce);   /* help1 fold 2 (HIGH 1) */
    BdHelpRecMerge(key, h);   /* help1 fold (MED 5/6): the record's counts - never lower */
    return h;
}
unsigned int BdHelpUnacked()
{
    unsigned int u = 0;
    for (std::map<std::string, BdHelpRow>::const_iterator it = g_help.begin(); it != g_help.end(); ++it) u += (unsigned int)it->second.owed.size();
    for (std::map<BdPlKey, BdHelpRow>::const_iterator it = g_helpOld.begin(); it != g_helpOld.end(); ++it) u += (unsigned int)it->second.owed.size();   /* help1 fold 3 */
    return u;
}
/* help1, MAIN THREAD, from detour_progress right after the engine's call on a copy (the owner's stand-in holds it; not the mod's own
   write). p0 / n0 / m0 were read before the call; needed was raised for it (not on a ruin). */
void BdHelpAfter(void* b, float p0, float n0, int ruin, int m0n, const float* m0)
{
    /* help1 fold (review LOW 11): the raised needed was already put back by BdProgressRaised's __finally (only while it still held the raised value) */
    float p1 = 0.0f, n1 = 0.0f;
    int c1 = 0, d1 = 0, x1 = 0;
    if (BdReadState(b, &p1, &n1, &c1, &d1, &x1) == 0) return;
    char k[kKeyCap];
    k[0] = 0;
    const int keyOk = ObjectPositionKey(b, k, kKeyCap, "", 3, 0);
    int capped = 0;
    const float capTo = coopbuild::BuildHelpCopyCap(p1, n1, (ruin != 0 || x1 != 0) ? 1 : 0, &capped);
    if (c1 == 0 && d1 == 0 && ruin == 0 && x1 == 0 && n0 > 0.0f)
    {   /* the picture the engine drew inside the call saw the raised needed: progress (capped), setBroken(0), updatePhysicalWithProgress again */
        const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
        int ml = -1, stg = 0;
        if (rUpd != 0) BdApplyStatePod(b, 1, capTo, 0, 0, &ml, (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd, &stg);
    }
    if (capped != 0)
    {
        ++g_helpCopyCapped;
        char cl[300];
        _snprintf_s(cl, sizeof(cl), _TRUNCATE, "[BUILD] help copy capped key=%s %.2f/%.2f (the work reached %.2f - only the owner's STATE completes it) copyCapped=%lld",
                    keyOk != 0 ? k : "?", capTo, n1, p1, g_helpCopyCapped);
        BdLogHelpFreq(cl);
    }
    if (keyOk == 0) return;
    std::map<std::string, BdReg>::iterator it = BdRowByLiveKey(std::string(k));
    if (it == g_reg.end() || it->second.via != 3 || it->second.removed != 0) return;
    BdReg& r = it->second;
    float m1[coopbuild::kBuildMaxMats];
    int tot = -1;
    const int m1n = BdReadMats(b, m1, (int)coopbuild::kBuildMaxMats, &tot);
    const int reset = (ruin != 0 && x1 == 0) ? 1 : 0;   /* the engine rebuilt a ruin (part-built, mats scaled) - the owner runs the same reset */
    float dP = (reset != 0) ? 0.0f : p1 - p0;
    if (!(dP > 0.0f)) dP = 0.0f;
    float dm[coopbuild::kBuildMaxMats];
    int nd = 0, any = (dP > 0.0f || reset != 0) ? 1 : 0;
    if (m1n > 0 && reset == 0)
    {
        for (int i = 0; i < m1n; ++i)
        {
            const float base = (r.helpBaseOk != 0) ? r.helpBase[i] : ((i < m0n) ? m0[i] : m1[i]);
            dm[i] = m1[i] - base;
            if (dm[i] > 0.0001f) any = 1; else dm[i] = 0.0f;
        }
        nd = m1n;
    }
    if (m1n >= 0)
    {
        for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) r.helpBase[i] = (i < m1n) ? m1[i] : 0.0f;
        r.helpBaseOk = 1;
    }
    if (any == 0) return;
    BdHelpRow& h = BdHelpRowFor(it->first, r);
    h.pendP += dP;
    for (int i = 0; i < nd; ++i) h.pendM[i] += dm[i];
    if (nd > h.pendN) h.pendN = nd;
    if (reset != 0) h.pendReset = 1;
    if (h.pendAny == 0) { h.pendAny = 1; h.pendSince = ::GetTickCount(); }
    h.need = n1;
    for (int i = 0; i < nd && i < (int)coopbuild::kBuildMaxMats; ++i)   /* help1 fold (MED 3): the item a HELP_GONE refund hands back */
    {
        if (!(dm[i] > 0.0f) || !h.sid[i].empty()) continue;
        char sid[128];
        sid[0] = 0;
        void* gd = BdMatDataPod(b, i);
        if (gd != 0) BdStrPod((const char*)gd + kGdStringId, sid, (int)sizeof(sid));
        if (sid[0] != 0) { h.sid[i] = sid; BdHelpSaveSoon(); }
    }
    h.pos[0] = r.pos[0]; h.pos[1] = r.pos[1]; h.pos[2] = r.pos[2]; h.posOk = 1;
}
/* help1, MAIN THREAD (BdHelpTick): a row's unsent entries go (all of them again at link-up, any left unconfirmed for kHelpResendMs);
   help1 fold 3: for the key's row and for each replaced placement's row alike (each entry carries its own nonce) */
void BdHelpSendRow(const std::string& key, BdHelpRow& h, int edge, DWORD now)
{
    char line[400];
    for (size_t i = 0; i < h.owed.size(); )
    {
        coopbuild::BuildHelpEntry& e = h.owed[i];
        if (e.applied != 0) { ++i; continue; }   /* help1 fold 3 (finding 1): the owner applied it (live) - its landed confirmation follows; never re-sent */
        const int again = (e.sent != 0 && (edge != 0 || now - (DWORD)e.sentAt >= kHelpResendMs)) ? 1 : 0;
        if (e.sent != 0 && again == 0) { ++i; continue; }
        coopbuild::BuildMsg m;
        BdHelpMsgOf(key, e, &m);
        if (!coopbuild::BuildEncodable(m))
        {   /* help1 fold 2 (re-check HIGH 1): never erased - its seq would leave a gap every later entry is held behind */
            const int fixedN = coopbuild::BuildHelpEntrySanitize(&e);
            BdHelpMsgOf(key, e, &m);
            BdHelpSaveSoon();
            ++g_helpUnencodable;
            if (!coopbuild::BuildEncodable(m))
            {
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s seq=%u does NOT ENCODE (key %u bytes) - kept and tried again in %lu s, never erased (a gap holds every later entry); unencodable=%lld",
                            key.c_str(), e.seq, (unsigned int)key.size(), (unsigned long)(kHelpResendMs / 1000), g_helpUnencodable);
                BdLogHelp(line);
                e.sent = 1; e.sentAt = (unsigned int)now;
                ++i;
                continue;
            }
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s seq=%u made encodable (%d fields were non-finite or out of range) - kept and sent; unencodable=%lld",
                        key.c_str(), e.seq, fixedN, g_helpUnencodable);
            BdLogHelp(line);
        }
        if (net::BuildSlotTarget(h.ownerSlot) == -1)   /* fold 1 (F6): to the piece's owner only - never to every game; fold 2 (D1): -1 = nobody now (kAddrOwnerNone) - a slot not known goes to the raw session peer while the session link is up */
        {
            ++g_helpNoOwnerSlot;
            if (g_helpNoOwnerSlot <= 5 || (g_helpNoOwnerSlot % 100) == 0)
            {
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s held: the owner's slot is not known here and there is no session link - never sent to every game (fold 2 D1); helpNoOwnerSlot=%lld",
                            key.c_str(), g_helpNoOwnerSlot);
                BdLogHelp(line);
            }
            break;
        }
        if (!net::SendBuildToSlot(m, h.ownerSlot)) break;   /* no road, or the owner's game not in the world: the rest wait, in order */
        if (again != 0) ++g_helpResent; else ++g_helpWorkSent;
        e.sent = 1; e.sentAt = (unsigned int)now;
        float ms = 0.0f;
        for (int k = 0; k < (int)e.n; ++k) ms += e.d[k];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s owner=slot%d seq=%u dP=%.3f dMats=%.2f sent unacked=%u%s",
                    key.c_str(), h.ownerSlot, e.seq, e.dP, ms, (unsigned int)h.owed.size(), again != 0 ? " (again: not yet confirmed)" : "");
        BdLogHelpFreq(line);
        ++i;
    }
}
/* help1, MAIN THREAD (BuildTick): the recorded work becomes entries (each second, each 1% of needed, or at a reset); unsent entries go
   while the link is up (all of them again at link-up, and any left unconfirmed for kHelpResendMs). help1 fold (review MED 5/6): a new
   entry is made only once the owner's STATE told this session its last applied seq (the copy row's tail) - its seq is past that, past
   this row's own and past pp.help's (BuildHelpStartSeq). help1 fold (MED 8): the owed file is saved at most every kHelpSaveMs and at
   link-down, not at every new entry. */
void BdHelpTick()
{
    const int up = net::LinkIsUp() ? 1 : 0;
    const int edge = (up != 0 && g_helpLinkWasUp == 0) ? 1 : 0;
    const int down = (up == 0 && g_helpLinkWasUp != 0) ? 1 : 0;
    g_helpLinkWasUp = up;
    if (edge != 0) BdHelpLoad();
    const DWORD now = ::GetTickCount();
    char line[400];
    for (std::map<std::string, BdHelpRow>::iterator it = g_help.begin(); it != g_help.end(); ++it)
    {
        BdHelpRow& h = it->second;
        if (h.pendAny != 0 && (now - h.pendSince >= kHelpSendMs || h.pendReset != 0 || (h.need > 0.0f && h.pendP >= kHelpSendStep * h.need)))
        {
            std::map<std::string, BdReg>::const_iterator cr = g_reg.find(it->first);
            const int tail = (cr != g_reg.end() && cr->second.via == 3 && cr->second.helpTailKnown != 0) ? 1 : 0;
            coopbuild::BuildHelpEntry* e = 0;
            if (!h.owed.empty() && coopbuild::BuildHelpJoinable(h.owed.back())) e = &h.owed.back();   /* not on the link yet: the work joins it; fold 2 (MED 4): never one read back from the owed file */
            else if (tail == 0)
            {
                if (h.waitLogged == 0)
                {
                    h.waitLogged = 1;
                    ++g_helpWaitTail;
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s owner=slot%d waits: no STATE from the owner yet this session (its last applied seq sets this game's next) - kept, sent after it; waitTail=%lld",
                                it->first.c_str(), h.ownerSlot, g_helpWaitTail);
                    BdLogHelp(line);
                }
            }
            else if ((int)h.owed.size() < kHelpOwedCap)
            {
                BdHelpRecMerge(it->first, h);
                const unsigned int ns = coopbuild::BuildHelpStartSeq(h.nextSeq, 0, cr->second.helpTailLive);   /* help1 fold 3 (finding 1): past the owner's LIVE last applied */
                h.waitLogged = 0;
                coopbuild::BuildHelpEntry ne;
                ne.seq = ns;
                ne.nonce = h.nonce;   /* help1 fold 3: the placement it is done on */
                h.nextSeq = ns + 1;
                h.owed.push_back(ne);
                e = &h.owed.back();
                if (g_helpRecDirty < 1) g_helpRecDirty = 1;
            }
            if (e != 0)
            {
                e->dP += h.pendP;
                if (e->dP > coopbuild::kBuildHelpMax) e->dP = coopbuild::kBuildHelpMax;
                for (int i = 0; i < h.pendN && i < (int)coopbuild::kBuildMaxMats; ++i)
                {
                    e->d[i] += h.pendM[i];
                    if (e->d[i] > coopbuild::kBuildHelpMax) e->d[i] = coopbuild::kBuildHelpMax;
                }
                if (h.pendN > (int)e->n) e->n = (unsigned char)h.pendN;
                if (h.pendReset != 0) e->reset = 1;
                h.pendP = 0.0f; h.pendN = 0; h.pendReset = 0; h.pendAny = 0;
                for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) h.pendM[i] = 0.0f;
                BdHelpSaveSoon();
                if (up == 0)
                {
                    ++g_helpWorkOwed;
                    float ms = 0.0f;
                    for (int i = 0; i < (int)e->n; ++i) ms += e->d[i];
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s owner=slot%d seq=%u dP=%.3f dMats=%.2f owed unacked=%u (link down - sent at link-up)",
                                it->first.c_str(), h.ownerSlot, e->seq, e->dP, ms, (unsigned int)h.owed.size());
                    BdLogHelpFreq(line);
                }
            }
        }
        if (up == 0) continue;
        BdHelpSendRow(it->first, h, edge, now);
    }
    if (up != 0)   /* help1 fold 3: a replaced placement's entries go on under their own nonce (the owner answers them HELP_GONE) */
        for (std::map<BdPlKey, BdHelpRow>::iterator ot = g_helpOld.begin(); ot != g_helpOld.end(); ++ot) BdHelpSendRow(ot->first.first, ot->second, edge, now);
    if (g_helpSaveDirty != 0 && (down != 0 || (DWORD)(now - g_helpSaveAt) >= kHelpSaveMs)) { g_helpSaveAt = now; BdHelpSave(); }
}
/* help1 (owner 172): a build or add-materials order on a COPY of another player's piece (its faction is that player's stand-in) is help
   - given and counted (orderOnCopy); the helper's game then sends the work to the owner (HELP_WORK). help1 fold (review HIGH 2): NOT on a
   wall copy - wall progress (0x5594A0) is not hooked, so no work would reach the owner and the copy would complete here alone; refused and
   counted (helpWallRefused) until wall progress syncs (to-do T-221). 1 = help given, 2 = refused (a wall copy), 0 = not such an order. */
int BdHelpOrder(int t, void* subject, const char* road)
{
    if (t == coopbuild::kBuildTaskRepair)   /* P15: a repair on a RUIN copy - help (the owner's game runs the ruin reset first); a wall ruin copy - refused */
    {
        ::Faction* rf = (::Faction*)BdOwnerPod(subject);
        int rx = 0;
        BdByteAt(subject, kBDestroyed, &rx);
        const int rv = coopbuild::BuildRepairOrderVerdict(t, (rf != 0 && IsStandInFaction(rf)) ? 1 : 0, rx > 0 ? 1 : 0, BdIsWall(subject));
        if (rv == coopbuild::kBuildRepairOrderNone) return 0;
        char rk[kKeyCap];
        rk[0] = 0;
        if (ObjectPositionKey(subject, rk, kKeyCap, "", 3, 0) == 0) { rk[0] = '?'; rk[1] = 0; }
        char rl[380];
        if (rv == coopbuild::kBuildRepairOrderRefused)
        {
            ++g_repairRefused;
            _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] order task=95 (repair) on a WALL ruin copy of slot%d key=%s NOT GIVEN (%s) - only the owner's game repairs its wall (wall work is not synced, T-221); repairRefused=%lld",
                        StandInSlotOf(rf), rk, road, g_repairRefused);
            BdLogHelp(rl);
            return 2;
        }
        ++g_repairOrderHelp;
        _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] order task=95 (repair) on a ruin copy of slot%d key=%s - help (%s): the work goes to the owner's game with the ruin reset; repairOrderHelp=%lld",
                    StandInSlotOf(rf), rk, road, g_repairOrderHelp);
        BdLogHelp(rl);
        return 1;
    }
    if (t != coopbuild::kBuildTaskBuild && t != coopbuild::kBuildTaskAddMaterials) return 0;
    ::Faction* f = (::Faction*)BdOwnerPod(subject);
    const int v = coopbuild::BuildHelpOrderVerdict(t, (f != 0 && IsStandInFaction(f)) ? 1 : 0, BdIsWall(subject));   /* help1 fold 2 (LOW 7): the rule every road shares */
    if (v == coopbuild::kBuildHelpOrderNone) return 0;
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(subject, k, kKeyCap, "", 3, 0) == 0) { k[0] = '?'; k[1] = 0; }
    char line[340];
    if (v == coopbuild::kBuildHelpOrderWallRefused)
    {
        ++g_helpWallRefused;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a WALL copy of slot%d key=%s NOT GIVEN (%s) - wall progress is not synced yet (to-do T-221); helpWallRefused=%lld",
                    t, StandInSlotOf(f), k, road, g_helpWallRefused);
        BdLogHelp(line);
        return 2;
    }
    ++g_orderOnCopy;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a copy of slot%d key=%s - help (%s); orderOnCopy=%lld",
                t, StandInSlotOf(f), k, road, g_orderOnCopy);
    BdLogHelp(line);
    return 1;
}
/* help1 fold 2 (re-check LOW 7), ANY THREAD (a guarded vtable call, RTTI and pointer compares - BdIsWall / IsStandInFaction are any-thread):
   off the main thread the wall-copy refusal is made too (the handed test is not) - counted (helpWallRefusedOff, the REPORT line), no
   line. 1 = refused. */
int BdHelpWallOff(int t, void* subject)
{
    if (t != coopbuild::kBuildTaskBuild && t != coopbuild::kBuildTaskAddMaterials) return 0;
    ::Faction* f = (::Faction*)BdOwnerPod(subject);
    if (coopbuild::BuildHelpOrderVerdict(t, (f != 0 && IsStandInFaction(f)) ? 1 : 0, BdIsWall(subject)) != coopbuild::kBuildHelpOrderWallRefused) return 0;
    ::InterlockedIncrement(&g_helpWallRefusedOff);
    return 1;
}
/* house1b (item 2), MAIN THREAD: the owner's game confirms it holds a piece handed to it */
void BdSendAck(const std::string& key)
{
    coopbuild::BuildMsg a;
    a.kind = coopbuild::kBuildHandAck;
    a.key = key;
    const bool noOne = net::BuildSlotTarget(g_bdAckSlot) == -1;   /* fold 2 (D1): -1 = nobody now; -2 (the session peer before its slot was known) goes on the session link */
    if (noOne) ++g_ackNoSlot;
    const bool sent = coopbuild::BuildEncodable(a) && !noOne && net::SendBuildToSlot(a, g_bdAckSlot);   /* fold 1 (F6): to the placer only */
    if (sent) ++g_ackSent;
    char line[260];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> HAND_ACK key=%.63s (this game holds the handed piece)%s", key.c_str(),
                sent ? "" : (noOne ? " NOT SENT (placer's slot unknown, no session link - fold 2 D1) - the placer sends its PLACE again at the next roster round"
                                              : " NOT SENT (no road, or the placer's game not in the world) - the placer sends its PLACE again at the next roster round"));
    BdLogCapped(line);
}
unsigned int BdOwedUnacked()
{
    unsigned int u = 0;
    for (std::map<std::string, BdOwed>::const_iterator it = g_owed.begin(); it != g_owed.end(); ++it) if (it->second.acked == 0) ++u;
    return u;
}
/* house1b (item 2), MAIN THREAD: every owed, unacknowledged hand-off PLACE again */
void BdOwedResend(const char* why)
{
    BdOwedLoad();
    int n = 0, down = 0;
    for (std::map<std::string, BdOwed>::iterator it = g_owed.begin(); it != g_owed.end(); ++it)
    {
        if (it->second.acked != 0) continue;
        if (net::SendBuild(it->second.m)) { ++n; ++g_owedResent; }
        else { ++down; ++g_owedResendLinkDown; }
    }
    if (n == 0 && down == 0) return;
    char line[320];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] owed hand-offs re-sent: %d PLACEs (%d not sent, link down) - %s; owed=%u until the owner's HAND_ACK",
                n, down, why, (unsigned int)g_owed.size());
    BdLogCapped(line);
}
/* house1b (items 1, 6), MAIN THREAD: a piece this game handed to a house owner? During the commit by the commit's own list (its row is
   filed after the commit); later by its key - an owed record (acked or not) or a copy row marked handed here. */
int BdIsHandedAway(void* b)
{
    if (b == 0) return 0;
    if (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid)
        for (int i = 0; i < g_commitNewN && i < 16; ++i) if (g_commitNew[i] == b && g_commitHanded[i] >= 0) return 1;
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(b, k, kKeyCap, "", 3, 0) == 0) return 0;
    const std::string key(k);
    BdOwedLoad();
    if (g_owed.find(key) != g_owed.end()) return 1;
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(key);
    return (it != g_reg.end() && it->second.via == 3 && it->second.handed == 1) ? 1 : 0;
}
/* house1b (review-house1 items 1, 6): 0x7F9280 addOrderSelectedCharacters. The placement commit ends with
   addOrderSelectedCharacters(0, BUILD, piece) (4d6810:452) on the piece it just made, and the click handlers (buildingSelected
   0x7FBB40, every base-access setting) issue the same call. A build / add-materials / repair / dismantle order on a piece this game
   handed to a house owner is not given - the owner's game builds and dismantles it; every other order (use) goes on as before. */
/* house2: the gated TaskTypes (build 2, add-materials 71, repair 95, dismantle 96) are coopbuild::BuildTaskGated (suite-tested) */
void detour_order(void* pi, void* indoors, void* task, void* subject, void* shift, void* addDontClear, void* location)
{
    const int t = (int)(unsigned int)(uintptr_t)task;
    /* help1 (owner 172): a build / add-materials order on a copy of another player's piece is help - given and counted (orderOnCopy); this
       was the house1b refusal for a handed piece. Repair and dismantle on a handed piece stay refused (repair waits for damage sync). */
    const int helpOnCopy = (coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() != 0)
        ? BdHelpOrder(t, subject, (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid) ? "the placement commit's own build order" : "a click") : 0;
    if (helpOnCopy == 2) return;   /* help1 fold (review HIGH 2): a wall copy - refused (BdHelpOrder logged it) */
    if (coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() == 0 && BdHelpWallOff(t, subject) != 0) return;   /* fold 2 (LOW 7): off the main thread too */
    if (helpOnCopy == 0 && coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() != 0
        && BdIsHandedAway(subject) != 0)
    {
        const int inCommit = (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid) ? 1 : 0;
        if (inCommit != 0) ++g_orderSkipped; else ++g_orderBlocked;
        char line[300];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a handed piece NOT GIVEN (%s) - the house owner's game builds and dismantles it; orderSkipped=%lld orderBlocked=%lld",
                    t, inCommit != 0 ? "the placement commit's own build order" : "a click", g_orderSkipped, g_orderBlocked);
        BdLogCapped(line);
        return;
    }
    orig_order(pi, indoors, task, subject, shift, addDontClear, location);
}
/* house2 (item 1): 0x7F4EF0 addJobSelectedCharacters - the Shift-queued job road: newPlayerTask 0x7F9AB0 and medicButton 0x7FA080 call it
   instead of 0x7F9280 while shift is held. The same rule as detour_order, once per call: a refused job is jobBlocked; a gated task
   let through unchecked (off the main thread, where the handed test is not made) is jobKept; everything else passes untouched. */
void detour_job(void* pi, void* task, void* subject, void* shift, void* add, void* location)
{
    const int t = (int)(unsigned int)(uintptr_t)task;
    if (coopbuild::BuildTaskGated(t) && subject != 0)
    {
        if (BdOnMain() == 0)
        {
            if (BdHelpWallOff(t, subject) != 0) return;   /* help1 fold 2 (LOW 7): a wall copy is refused off the main thread too */
            ++g_jobKept;
        }
        else
        {
            const int ho = BdHelpOrder(t, subject, "a Shift job");   /* help1 (owner 172): 1 = help on a copy - given; fold (HIGH 2): 2 = a wall copy - refused */
            if (ho == 2) return;
            if (ho == 0 && coopbuild::BuildTaskRefused(t, BdIsHandedAway(subject)))
            {
                ++g_jobBlocked;
                char line[300];
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] Shift job task=%d on a handed piece NOT GIVEN - the house owner's game builds and dismantles it; jobBlocked=%lld",
                            t, g_jobBlocked);
                BdLogCapped(line);
                return;
            }
        }
    }
    orig_job(pi, task, subject, shift, add, location);
}

/* build1h H1 (design 2(a)), MAIN THREAD: a new piece whose host building belongs to ANOTHER player becomes the house owner's at
   once - the engine's own setFaction vt+0xA0 (0x556EC0: the old faction's list gives it up, the new one's takes it) to that slot's
   stand-in. Called right after createBuilding returned inside the commit (detour_create) and by the `buildtest into` lever.
   Returns the slot handed to, or -1 = the placer keeps it; *why = coopown::kHo*, kHoNoStandIn or kHoSetFailed. No line here (the
   commit's detour): BdOnNewPiece logs it. */
const int kHoNoStandIn = 10, kHoSetFailed = 11, kHoPreflight = 12;   /* house1b: 12 = a check BdOnNewPiece makes would fail */
int BdHandToHouseOwner(void* b, void* host, int* why, int posOk = 1)
{
    *why = coopown::kHoNoHost;
    if (b == 0 || host == 0) return -1;
    int hostSlot = -1;
    const int hostCls = BuildingOwnerClass(host, &hostSlot);
    const int k = coopown::HouseOwnerOfNewPiece(1, hostCls, hostSlot, MySlotForWire(), why);
    if (*why == coopown::kHoUnresolved) { ++g_houseOwnerUnresolved; return -1; }
    if (*why == coopown::kHoTown) ++g_houseTown;
    if (k < 0) return -1;
    /* house1b (item 8): the checks BdOnNewPiece makes before the PLACE goes out, made FIRST - a piece whose PLACE could not go out is
       never re-labelled (it stays the placer's; the line says why=12) */
    {
        char pk[kKeyCap], hk2[kKeyCap];
        pk[0] = 0; hk2[0] = 0;
        if (posOk == 0 || ObjectPositionKey(b, pk, kKeyCap, "", 3, 0) == 0 || ObjectPositionKey(host, hk2, kKeyCap, "", 3, 0) == 0
            || (g_reg.find(std::string(pk)) == g_reg.end() && (int)g_reg.size() >= kRegCap))
        { *why = kHoPreflight; ++g_handPreflightFailed; return -1; }
    }
    ::Faction* to = StandInForSlot(k);
    if (to == 0)   /* house1b (item 4): an owner not linked since this world loaded - the house's own faction IS that slot's record (coop-p<k>) */
    {
        ::Faction* hf = (::Faction*)BdOwnerPod(host);
        if (hf != 0 && StandInRecordSlot(hf) == k) { to = hf; ++g_standInFromRecord; }
    }
    if (to == 0) { *why = kHoNoStandIn; ++g_houseOwnerUnresolved; return -1; }
    if (BdSetFactionPod(b, (void*)to) == 0) { *why = kHoSetFailed; ++g_houseOwnerUnresolved; return -1; }
    ++g_houseHanded;
    return k;
}

// MAIN THREAD. A piece that has just been created (the commit's createBuilding, or the lever's): registry + one line.
// build1-b: `pos` / `rot` are the floats createBuilding received; `meta` bit 0 = they were read, bit 1 = furniture.
void BdMaybeSendState(const std::string& key, BdReg& r);   /* help1 fold 5 (T-269): BdOnNewPiece announces the STATE after its PLACE */
void BdOnNewPiece(void* b, int via, const float* pos, const float* rot, int meta, void* host, int hostForm, int floor, int outside,
                  int handedTo = -1, int handWhy = 0)   /* build1h: the slot the piece was handed to (-1 none) and why */
{
    int wasCopy = 0;
    /* build1-e: the host building's own P7n key (the piece's host - createBuilding's args, same call, so live) */
    char hkey[kKeyCap];
    hkey[0] = 0;
    const int hkOk = (host != 0) ? ObjectPositionKey(host, hkey, kKeyCap, "", 3, 0) : 0;
    char key[kKeyCap];
    key[0] = 0;
    const int keyOk = ObjectPositionKey(b, key, kKeyCap, "", 3, 0);
    char sid[96];
    BdSidOf(b, sid, (int)sizeof sid);
    float p = 0, n = 0;
    int c = 0, d = 0, x = 0;
    const int stOk = BdReadState(b, &p, &n, &c, &d, &x);
    const std::string owner = BdFactionName(BdOwnerPod(b));
    float nm[coopbuild::kBuildMaxMats];
    int nmTotal = -1;
    const int nmN = BdReadMats(b, nm, (int)coopbuild::kBuildMaxMats, &nmTotal);   /* build1-c */
    if (via == 1) ++g_placedSeen;
    if (keyOk != 0)
    {
        std::map<std::string, BdReg>::iterator it = g_reg.find(std::string(key));
        if (it == g_reg.end() && (int)g_reg.size() >= kRegCap) ++g_regFull;
        else
        {
            BdReg& r = g_reg[std::string(key)];
            wasCopy = (it != g_reg.end() && it->second.via == 3) ? 1 : 0;
            if (it != g_reg.end() && wasCopy == 0) BdHelpPlacementEnds(std::string(key), r);   /* help1 fold 3 (finding 2): the key's earlier placement keeps its help in its own gone record; this piece starts with none */
            if (wasCopy != 0)   /* help1 fold 4 (re-check #5): the piece stays that copy - it keeps the copy's nonce and help state (nothing taken over) */
            {
                ++g_bdCopyCollide;
                char cl[360];
                _snprintf_s(cl, sizeof(cl), _TRUNCATE, "[BUILD] help new piece key=%.63s over a COPY row (the owner's placement %08x): it stays that copy and keeps its nonce - this game's help for that placement stays bound to it copyCollide=%lld",
                            key, it->second.nonce, g_bdCopyCollide);
                BdLogHelp(cl);
            }
            else g_placeNonceRestore.erase(std::string(key));   /* help1 fold 4: a new placement - pp.build's PLACE nonce was the earlier one's */
            r.sid = sid; r.owner = owner; r.progress = p; r.needed = n;
            r.complete = c; r.dismantled = d; r.destroyed = x; r.via = wasCopy ? 3 : via;
            /* build1-c */
            r.own = (wasCopy == 0 && LocalPlayerFaction() != 0 && BdOwnerPod(b) == (void*)LocalPlayerFaction()) ? 1 : 0;
            if (r.own != 0) BdCopyListRetire(std::string(key), "this game registered its own piece at the key");   /* P87 rf1 (LOW-MED): at once - the returns below no longer skip it */
            r.nMats = nmN;
            for (int i = 0; i < nmN; ++i) r.mats[i] = nm[i];
            r.sentOk = 0;
            r.removed = 0;   /* build1-d: a new piece at this key */
            r.gen = g_bdGen;   /* mmo8a2 (H1) */
            if (wasCopy == 0)   /* build1-e: an own piece's host, so the lever and buildlist can re-find furniture */
            {
                r.hostKey = (hkOk != 0) ? std::string(hkey) : std::string();
                r.hostForm = hostForm; r.liveKey.clear(); r.keySame = 1;
                /* build1-f: what the PLACE carries, kept so the link-up roster can send it again */
                r.placeOk = ((meta & 1) != 0 && pos != 0 && rot != 0 && ((meta & 2) == 0 || hkOk != 0)) ? 1 : 0;
                for (int i = 0; i < 3; ++i) r.placePos[i] = ((meta & 1) != 0 && pos != 0) ? pos[i] : 0.0f;
                for (int i = 0; i < 4; ++i) r.placeRot[i] = ((meta & 1) != 0 && rot != 0) ? rot[i] : 0.0f;
                r.floor = ((meta & 2) != 0) ? floor : 0;
                r.outside = ((meta & 2) != 0 && outside != 0) ? 1 : 0;
            }
            r.handed = (handedTo >= 0) ? 1 : (wasCopy != 0 ? r.handed : 0);   /* build1h */
            r.nonce = coopbuild::BuildNewPieceNonce(wasCopy, r.nonce, wasCopy != 0 ? 0u : BdNewNonce());   /* house2 fold (MED2): each NEW placement draws its own; help1 fold 4: a copy keeps its own */
            if (wasCopy == 0) r.ownerSlot = -1;
            if (handedTo >= 0)   /* build1h: handed to the house owner - a COPY row here; the owner's game is its STATE / REMOVE source */
            {
                r.via = 3; r.own = 0; r.ownerSlot = handedTo;
                r.hasPos = (host != 0 && BdCopyF((const char*)host + kObjPos, r.pos, 3)) ? 1 : 0;   /* the host's world position (the REMOVE's zone check) */
            }
            if (r.own != 0 && r.via != 3) BdOwnDirty(2);   /* mmo8a: an own piece placed here - its pp.build row is written now */
        }
    }
    else ++g_keyFailed;
    char line[512];
    std::sprintf(line, "[BUILD] placed key=%s sid=%s owner='%.60s' complete=%d progress=%.2f/%.2f via=%s%s",
                 keyOk != 0 ? key : "?", sid[0] != 0 ? sid : "?", owner.c_str(), c, p, n, via == 1 ? "hook" : (wasCopy ? "copy" : "test"),
                 stOk != 0 ? "" : " state=unreadable");
    BdLogCapped(line);
    ItFurnMarkStale(1);   /* T-454: a piece createBuilding just made here - the furniture registry is out of date */
    if (handedTo >= 0 || handWhy == coopown::kHoUnresolved || handWhy == kHoNoStandIn || handWhy == kHoSetFailed || handWhy == kHoPreflight)   /* build1h; house1b */
    {
        std::sprintf(line, "[BUILD] house %s key=%s host=%.63s form=%d ownerSlot=%d why=%d houseHanded=%lld houseOwnerUnresolved=%lld",
                     handedTo >= 0 ? "handed (the piece is the house owner's)" : "owner UNRESOLVED (the placer keeps it)", keyOk != 0 ? key : "?",
                     hkOk != 0 ? hkey : "?", hostForm, handedTo, handWhy, g_houseHanded, g_houseOwnerUnresolved);
        BdLogCapped(line);
    }
    /* build1-b: only a piece THIS game's player faction owns crosses; a copy never does */
    if (keyOk == 0 || wasCopy != 0) return;
    ::Faction* mine = LocalPlayerFaction();
    if (handedTo < 0 && (mine == 0 || BdOwnerPod(b) != (void*)mine)) return;   /* build1h: or handed to a house owner at this commit */
    const int furn = ((meta & 2) != 0) ? 1 : 0;   /* build1-e: furniture carries its host's key */
    if (furn != 0 && hkOk == 0)
    {
        ++g_hostKeyFailed;
        std::sprintf(line, "[BUILD] -> PLACE key=%s NOT SENT: furniture whose host key could not be built (host %s, form %d)", key,
                     host != 0 ? "found" : "not found", hostForm);
        BdLogCapped(line);
        return;
    }
    if ((meta & 1) == 0 || pos == 0 || rot == 0) { ++g_placeNoPos; return; }
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildPlace;
    m.key = key; m.sid = sid;
    for (int i = 0; i < 3; ++i) m.pos[i] = pos[i];
    for (int i = 0; i < 4; ++i) m.rot[i] = rot[i];
    m.complete = (unsigned char)(c != 0 ? 1 : 0);
    m.progress = p; m.needed = n;
    if (handedTo >= 0) m.ownerSlot = (unsigned short)handedTo;   /* the house owner's slot (HouseOwnerOfNewPiece keeps it a notebook slot) */
    {   /* house2 fold (MED2): the placement's nonce, as its row keeps it (the owed-file row keeps it inside the PLACE) */
        std::map<std::string, BdReg>::const_iterator nr = g_reg.find(std::string(key));
        m.nonce = (nr != g_reg.end() && nr->second.nonce != 0) ? nr->second.nonce : BdNewNonce();
    }
    if (furn != 0)   /* build1-e: pos / rot above are host-local, exactly as createBuilding received them */
    {
        m.hostKey = hkey;
        m.hostForm = (unsigned char)hostForm;
        m.floor = floor;
        m.outside = (unsigned char)(outside != 0 ? 1 : 0);
        ++g_placeFurniture;
    }
    if (!coopbuild::BuildEncodable(m)) { ++g_placeUnencodable; return; }
    if (handedTo >= 0)   /* house1b (item 2): owed until the owner's HAND_ACK - survives world teardown and reload (the placer's own file) */
    {
        BdOwedLoad();
        BdOwed& ow = g_owed[std::string(key)];
        ow.m = m; ow.acked = 0;
        ++g_owedMade;
        BdOwedSave();
    }
    /* par13 fold (review-par13 F2): keys come from position, so a piece rebuilt on a spot whose dismantle is still owed its REMOVE has
       the SAME key.  The owed REMOVE goes first, here, or it would follow this PLACE and delete the new piece's copy on the other
       game.  If it cannot go (link down), this PLACE cannot either, and BdRosterTick sends the REMOVE before any PLACE. */
    for (size_t oi = 0; oi < g_dmOweRemove.size(); ++oi)
    {
        if (g_dmOweRemove[oi] != key) continue;
        if (BdDmOweSendOne(g_dmOweRemove[oi])) g_dmOweRemove.erase(g_dmOweRemove.begin() + (std::ptrdiff_t)oi);
        break;
    }
    BdTombDrop(std::string(key), "a new own piece was placed at the key", 0);   /* P87: the owner lists the key again */
    BdTombDropNear(std::string(key));   /* P14 fold 1 (F2) */
    BdCopyListRetire(std::string(key), "this game placed its own piece at the key");   /* P87 root: replaced by this game's own piece */
    const bool sent = net::SendBuild(m);
    if (sent) ++g_placeSent; else ++g_placeLinkDown;
    if (sent)   /* build1-c: the copy starts from the PLACE's values and no delivered mats - what later STATEs are measured against */
    {
        std::map<std::string, BdReg>::iterator row = g_reg.find(std::string(key));
        if (row != g_reg.end())
        {
            row->second.sentOk = 1; row->second.sentProgress = p; row->second.sentComplete = c; row->second.sentN = 0;
            row->second.stateAnnounce = 1;   /* help1 fold 5 (T-269): the copy is made from this PLACE; help on it waits for a real STATE */
        }
    }
    std::sprintf(line, "[BUILD] -> PLACE key=%s sid=%s pos=(%.1f, %.1f, %.1f) complete=%d progress=%.2f/%.2f host=%s form=%d floor=%d ownerSlot=%d%s", key, sid,
                 pos[0], pos[1], pos[2], c, p, n, furn != 0 ? hkey : "-", furn != 0 ? hostForm : 0, furn != 0 ? floor : 0, handedTo,
                 sent ? "" : " NOT SENT (link down)");
    BdLogCapped(line);
    if (sent)   /* help1 fold 5 (T-269): the owner's first real STATE goes right after its PLACE (a copy / handed row sends none) */
    {
        std::map<std::string, BdReg>::iterator ar = g_reg.find(std::string(key));
        if (ar != g_reg.end()) BdMaybeSendState(ar->first, ar->second);
    }
}

/* build1-c, MAIN THREAD (BuildTick): send STATE for an own piece when progress moved >= 5% of needed (1% for large pieces) since the last send, on
   a complete flip (always), or when a delivered amount changed. Only after its PLACE went out; never for a copy. */
const float kLargeNeeded = 10.0f;   /* user 2026-09-26: at or above this, progress crosses in 1% steps */
void BdMaybeSendState(const std::string& key, BdReg& r)
{
    if (r.own == 0 || r.via == 3 || r.dismantled != 0 || r.removed != 0) return;   /* a dismantle is build1-d's REMOVE */
    if (r.sentOk == 0) { ++g_stateNoPlace; return; }
    const char* why = 0;
    /* user 2026-09-26: 5% steps for small constructions, 1% for large ones (those that take a while to reach even 1%);
       'large' = kLargeNeeded work units or more (a storage chest needs 1, T321; town houses 20-35, T321's load events) */
    const float step = (r.needed >= kLargeNeeded ? 0.01f : 0.05f) * r.needed;
    if (r.destroyed != r.sentDestroyed) why = (r.destroyed != 0) ? "destroyed" : "repaired";   /* P15 (protocol 98): the writer's +0x1A1 flip */
    else if (r.complete != r.sentComplete) why = "complete";
    else if (r.progress != r.sentProgress && (r.needed <= 0.0f || std::fabs(r.progress - r.sentProgress) >= step)) why = "progress";
    else if (r.nMats >= 0 && BdMatsDiffer(r.nMats, r.mats, r.sentN, r.sentMats)) why = "mats";
    if (why == 0 && r.helpForce != 0) why = "help";   /* help1: a helper's work was applied (or re-sent) - the STATE carries the confirmation now */
    if (why == 0 && coopbuild::BuildOwnStateAnnounce(r.own, r.via == 3 ? 1 : 0, r.sentOk, r.stateAnnounce) != 0) why = "announce";   /* help1 fold 5 (T-269) */
    if (why == 0) return;
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildState;
    m.key = key;
    m.progress = r.progress; m.needed = r.needed;
    m.complete = (unsigned char)(r.complete != 0 ? 1 : 0);
    m.destroyed = (unsigned char)(r.destroyed != 0 ? 1 : 0);   /* P15 */
    const int n = (r.nMats < 0) ? 0 : (r.nMats > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : r.nMats);
    m.nMats = (unsigned char)n;
    for (int i = 0; i < n; ++i) m.mats[i] = r.mats[i];
    const std::vector<coopbuild::BuildHelpAck>* ha = BdHelpAcksLandedRead(key, r);   /* help1: the tail - each helper's last applied seq and refused materials; fold (MED 4): as pp.build holds them on disk */
    coopbuild::BuildHelpTailOf(ha, BdHelpAcksRead(key, r), &m.helpAcks);   /* help1 fold 3 (finding 1): + each slot's LIVE last applied (the helper keeps, never renumbers, what it covers) */
    if (!coopbuild::BuildEncodable(m)) { ++g_stateUnencodable; return; }
    const bool sent = net::SendBuild(m);
    char line[400];
    std::sprintf(line, "[BUILD] -> STATE key=%.63s %.2f/%.2f complete=%d destroyed=%d mats=%d why=%s help=%u%s", key.c_str(), r.progress, r.needed,
                 (int)m.complete, (int)m.destroyed, n, why, (unsigned int)m.helpAcks.size(), sent ? "" : " NOT SENT (link down)");
    BdLogState(line);
    if (!sent) { ++g_stateLinkDown; return; }   /* not marked sent: the next event tries again */
    ++g_stateSent;
    r.helpForce = 0;
    if (r.stateAnnounce != 0) { r.stateAnnounce = 0; ++g_stateAnnounced; }   /* help1 fold 5: any real STATE is the announcement */
    r.sentProgress = r.progress; r.sentComplete = r.complete; r.sentN = n;
    if (r.destroyed != r.sentDestroyed) { if (r.destroyed != 0) ++g_destroySent; else ++g_repairSent; }   /* P15 */
    r.sentDestroyed = r.destroyed;
    for (int i = 0; i < n; ++i) r.sentMats[i] = r.mats[i];
}

void BdLogRemove(const char* line)   /* build1-d: its own cap */
{
    if (g_removeLines < kRemoveLineCap) { ++g_removeLines; DebugLog(std::string(line)); }
    else ++g_removeSuppressed;
}
/* build1-d, MAIN THREAD (BuildTick): an own piece was dismantled (reason 1; review-build1d D1: 2 is never sent) - REMOVE once per key; the row
   is marked removed whatever happens (no later STATE: the piece is gone here). Sent only if its PLACE went out. */
void BdSendRemove(const std::string& key, BdReg& r, int reason)
{
    if (r.own == 0 || r.via == 3 || r.removed != 0) return;
    r.removed = 1;
    BdNoteRemoved(key);   /* review-build1d D5 */
    BdOwnDirty(2);   /* mmo8a: the removed flag retires the pp.build row - written now, whatever the REMOVE send below does */
    BdRestoreNonce(key, r, 1);   /* P87 fold 1 (H2): the placement's nonce (from its pp.build record if the row had none yet) - the tombstone and the REMOVE carry it */
    if (coopbuild::BuildTombNeeded(r.sentOk, r.progress, (r.via == 1 && r.gen == g_bdGen) ? 1 : 0) != 0)
        BdTombNote(key, r);   /* P87: kept in pp.build and owed its REMOVE at every roster round until a live own piece stands here again */
    else ++g_tombNotShared;   /* P87 fold 1 (L1): a blueprint placed here in this world and never shared - no copy anywhere, no tombstone */
    if (r.sentOk == 0) { ++g_removeNoPlace; return; }   /* the other game never got its PLACE: no copy there */
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildRemove;
    m.key = key;
    m.reason = (unsigned char)reason;
    m.nonce = r.nonce;   /* P87 fold 1 (H2): the placement this REMOVE ends (0 unknown) */
    const bool enc = coopbuild::BuildEncodable(m);
    const bool sent = enc && net::SendBuild(m);
    if (sent)
    {
        ++g_removeSent;
        std::map<std::string, BdTombInfo>::iterator ts = g_tombInfo.find(key);
        if (ts != g_tombInfo.end()) ts->second.sent = 1;
    }
    else ++g_removeLinkDown;
    /* par13 (parity P13): the other game still holds its copy - the REMOVE is owed and BdRosterTick sends it once the link is up */
    if (!sent && enc && coopbuild::BuildOweRemoveAdd(&g_dmOweRemove, key) != 0) ++g_dmOweAdded;
    char line[300];
    std::sprintf(line, "[BUILD] -> REMOVE key=%.63s reason=%d (%s)%s", key.c_str(), reason, reason == 1 ? "dismantled" : "destroyed",
                 sent ? "" : (enc ? " NOT SENT (link down) - owed: sent when the link is up" : " NOT SENT (not encodable)"));
    BdLogRemove(line);
}

// PROBE-START: P082
// ---- the detours ----
void detour_commit(void* list)
{
    ::InterlockedIncrement64(&g_commitSeen);
    const DWORD tid = ::GetCurrentThreadId();
    const int outer = (g_commitDepth == 0) ? 1 : 0;
    if (outer == 0 && tid != g_commitTid) { orig_commit(list); return; }   /* a second thread: untouched */
    if (outer != 0) { g_commitTid = tid; g_commitNewN = 0; }
    ++g_commitDepth;
    orig_commit(list);
    --g_commitDepth;
    if (outer == 0) return;
    const int n = g_commitNewN;
    g_commitNewN = 0;
    g_commitTid = 0;
    if (BdOnMain() == 0) { g_commitOffMain += n; return; }
    for (int i = 0; i < n; ++i) BdOnNewPiece(g_commitNew[i], 1, g_commitPos[i], g_commitRot[i], g_commitMeta[i], g_commitHost[i],
                                             g_commitForm[i], g_commitFloor[i], g_commitOutside[i], g_commitHanded[i], g_commitHandWhy[i]);   /* the same call, so the pointers are still live */
    int handedN = 0;   /* house2 (item 5): the commit issues ONE build order (4d6810:452) - counted once, not once per handed piece */
    for (int i = 0; i < n; ++i) if (g_commitHanded[i] >= 0) ++handedN;
    g_orderKept += coopbuild::BuildCommitOrderKept(handedN, g_hkOrder == 1 ? 1 : 0);   /* house1b: no order gate - the build order went to the placer's workers */
}

void* detour_create(void* factory, void* data, void* pos, void* town, void* owner, void* rot, void* cb, void* furnitureOf,
                    void* isDoorOf, void* save, void* isIndoorsOf, bool invisible, bool completed, bool isFoliage, int floor,
                    bool outsideFurniture)
{
    const int inCommit = (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid) ? 1 : 0;
    if (inCommit != 0) ++g_createDepth;
    void* r = orig_create(factory, data, pos, town, owner, rot, cb, furnitureOf, isDoorOf, save, isIndoorsOf, invisible,
                          completed, isFoliage, floor, outsideFurniture);
    if (inCommit != 0)
    {
        --g_createDepth;
        if (g_createDepth == 0 && r != 0)   /* top level only: a door the piece creates for itself is not a placement */
        {
            if (g_commitNewN < 16)
            {
                const int k = g_commitNewN++;
                g_commitNew[k] = r;
                /* build1-b: the exact floats this createBuilding received - what the PLACE carries */
                const int got = (pos != 0 && rot != 0 && BdCopyF(pos, g_commitPos[k], 3) != 0 && BdCopyF(rot, g_commitRot[k], 4) != 0) ? 1 : 0;
                g_commitMeta[k] = got | ((furnitureOf != 0 || isIndoorsOf != 0) ? 2 : 0);
                /* build1-e: the host and the furnitureOf form (POD loads only), floor and outside as received */
                g_commitHost[k] = 0; g_commitForm[k] = 0;
                g_commitFloor[k] = floor; g_commitOutside[k] = outsideFurniture ? 1 : 0;
                if (furnitureOf != 0 || isIndoorsOf != 0) BdHostFormPod(furnitureOf, isIndoorsOf, &g_commitHost[k], &g_commitForm[k]);
                /* build1h H1: inside another player's house -> the house owner's NOW, before the commit goes on (design 2(a)) */
                g_commitHanded[k] = -1; g_commitHandWhy[k] = coopown::kHoNoHost;
                if (g_commitHost[k] != 0 && BdOnMain() != 0) g_commitHanded[k] = BdHandToHouseOwner(r, g_commitHost[k], &g_commitHandWhy[k], got);   /* house1b: got = the PLACE's pos / rot */
            }
            else ++g_commitNewDropped;
        }
    }
    return r;
}

/* help1 fold (review LOW 11): the engine's call with the copy's needed raised; __finally puts it back even when the call faults (only
   while the field still holds the raised value). No C++ object in this frame (C2712). */
void BdProgressRaised(void* b, float amount, float raisedTo, float n0)
{
    __try { orig_progress(b, amount); }
    __finally { BdRestoreNeededPod(b, raisedTo, n0); }
}
void detour_progress(void* b, float amount)
{
    /* help1 (owner 172): the helper's own work on its copy of another player's piece (a stand-in holds it) - main thread, not the mod's own
       write (g_bdSelf), not an FLT_MAX-class call; the copy's needed is raised for this call only (never on a ruin), so it never completes here */
    int help = 0;
    float p0 = 0.0f, n0 = 0.0f, raisedTo = 0.0f;
    int c0 = 0, d0 = 0, x0 = 0, m0n = -1;
    float m0[coopbuild::kBuildMaxMats];
    if (g_bdSelf == 0 && amount < FLT_MAX * 0.5f && BdOnMain() != 0 && IsStandInFaction((::Faction*)BdOwnerPod(b))
        && BdReadState(b, &p0, &n0, &c0, &d0, &x0) != 0 && (c0 == 0 || (x0 != 0 && BdIsWall(b) == 0)) && d0 == 0)   /* P15 fold 1 (LOW 5): never a wall ruin */   /* P15: a COMPLETE ruin copy's repair is help too (the reset goes to the owner) */
    {
        int tot = -1;
        m0n = BdReadMats(b, m0, (int)coopbuild::kBuildMaxMats, &tot);
        help = 1;
        if (x0 == 0 && n0 > 0.0f)
        {
            raisedTo = (n0 + std::fabs(amount)) * 1000.0f + 1000.0f;
            if (BdSetNeededPod(b, raisedTo) == 0) { help = 0; raisedTo = 0.0f; }
        }
    }
    if (raisedTo > 0.0f) BdProgressRaised(b, amount, raisedTo, n0);   /* help1 fold (review LOW 11): put back in __finally */
    else orig_progress(b, amount);
    if (help != 0) BdHelpAfter(b, p0, n0, x0, m0n, m0);
    ::InterlockedIncrement64(&g_progressSeen);
    /* build1-b (T321: ~1400 FLT_MAX completions at world load, queueDropped 1141): an FLT_MAX-class amount while the world
       is loading is the engine finishing pieces it has just read, not construction - counted, never queued */
    if (amount >= FLT_MAX * 0.5f && EngineWritesBlocked()) { ::InterlockedIncrement64(&g_loadBurstSkipped); return; }
    BdQueueEvent(1, b, amount, BdOnMain(), -1);
}

unsigned long long detour_dismantle(void* b, float amount)
{
    /* build1-d: isDismantled BEFORE the call, read only for a real step (amount 0 is the ~12k/s routine call) */
    int before = -1;
    if (amount > 0.0f)
    {
        float p0 = 0, n0 = 0;
        int c0 = 0, d0 = 0, x0 = 0;
        if (BdReadState(b, &p0, &n0, &c0, &d0, &x0)) before = BdDismOf(b, d0);   /* review-build1d D2: a wall segment's own +0x162 */
    }
    const unsigned long long r = orig_dismantle(b, amount);
    const int onMain = BdOnMain();
    ::InterlockedIncrement64(&g_dismantleSeen);
    if (onMain == 0) ::InterlockedIncrement64(&g_dismantleOffMain);
    /* T319: the engine calls this ~12k times a second with amount 0 (routine upkeep, not a dismantle) - only a real step is
       an event; dismantleSeen still counts every call */
    if (amount > 0.0f) BdQueueEvent(2, b, amount, onMain, before);   /* POD only: this may be the AI worker */
    return r;
}

void detour_destroy(void* b, bool destroyed)
{
    int before = -1, after = -1;
    BdByteAt(b, kBDestroyed, &before);   /* review-build1a B5: the engine returns early when +0x1A1 already equals the argument */
    /* P15 fold 1 (review-p15 CRASH 1a): a flip is about to run (either way, or +0x1A1 unreadable) - the building's door rows go FIRST,
       before the original deletes (ruin) or remakes (repair) its doors; the engine's own flips reach nothing else of the mod. Off the
       main thread the engine re-posts the call as message 16 and deletes nothing here; the re-posted call comes back through this hook. */
    if (BdOnMain() != 0 && (before < 0 || (before > 0 ? 1 : 0) != (destroyed ? 1 : 0))) DoorsForgetOwner(b);
    orig_destroy(b, destroyed);
    if (BdOnMain() == 0) { ::InterlockedIncrement64(&g_destroyOffMain); return; }   /* the engine re-posts it as message 16 */
    (void)destroyed;   /* P15: the flip is read from +0x1A1 itself (setDestroyed(0) is also the end of the ruin reset addConstructionProgress runs) */
    BdByteAt(b, kBDestroyed, &after);
    if (!((before == 0 && after > 0) || (before > 0 && after == 0))) { ::InterlockedIncrement64(&g_destroyNoChange); return; }   /* P15: a real flip either way */
    ::InterlockedIncrement64(after > 0 ? &g_destroySeen : &g_wholeSeen);
    BdQueueEvent(3, b, g_bdSelf > 0 ? 1.0f : 0.0f, 1, -1);   /* P15: amount 1 = the mod's own write (a STATE applied here), 0 = this game's engine */
}

int BdHook(uintptr_t base, unsigned long long rva, void* detour, void** orig)
{
    if (rva == 0) return -2;
    return (coop::AddHook((void*)(base + (uintptr_t)rva), detour, orig) == coop::SUCCESS) ? 1 : -1;
}
// PROBE-END: P082

/* ground5 fold 1 (F7): the refund's original inside __try / __finally, so items.cpp's GroundRefundEnd (the end of its begin/end
   bracket) runs on every exit, an exception out of the original included. No C++ temporaries in this function (C2712). */
void BdRefundBracketed(void* b, int onMain)
{
    __try { orig_refund(b); }
    __finally { if (onMain != 0) GroundRefundEnd(); }
}
/* build1-d: the dismantle refund 0x29DBD0 (void(Building*), Read decompile; called from 0x2A2820's <= 0 branch). COUNTED only,
   its one argument forwarded. On the main thread a coop-peer piece is a copy: refundCalledOnCopy, and refundOnCopy when it
   still had delivered mats (the refund would drop items). Read BEFORE the original, which reads the same mats. */
/* P14 fold 1 (the escape ledger), MAIN THREAD (detour_refund, before the original): an own piece's refund - its key, so the items the
   ground road lands later (BuildRefundLanded) are listed under it */
void BdRefundNoteSrc(void* b)
{
    const DWORD now = ::GetTickCount();
    for (std::map<void*, BdRefundSrc>::iterator it = g_refundSrc.begin(); it != g_refundSrc.end(); )
    {
        if ((DWORD)(now - it->second.at) > 30000UL) g_refundSrc.erase(it++);
        else ++it;
    }
    ::Faction* mine = LocalPlayerFaction();
    if (b == 0 || mine == 0 || BdOwnerPod(b) != (void*)mine) return;
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) { ++g_ledRefundNoPiece; return; }
    BdRefundSrc s;
    s.key = k; s.at = now;
    g_refundSrc[b] = s;
}
coopbuild::BuildLedger* BdLedgerOf(const std::string& pk, int* gen)   /* the ledger of a piece key: its tombstone's, else the pending one */
{
    std::map<std::string, BdTombInfo>::iterator ti = g_tombInfo.find(pk);
    if (ti != g_tombInfo.end()) { if (gen != 0) *gen = (ti->second.readBack != 0) ? 1 : 0; return &ti->second.led; }
    if (gen != 0) *gen = 0;
    if (g_ledgerPend.size() >= 64 && g_ledgerPend.find(pk) == g_ledgerPend.end()) return 0;
    return &g_ledgerPend[pk];
}
void BdRefundNoteMissed(void* b, long long n)
{
    if (n <= 0) return;
    std::map<void*, BdRefundSrc>::const_iterator it = g_refundSrc.find(b);
    if (it == g_refundSrc.end()) return;
    coopbuild::BuildLedger* L = BdLedgerOf(it->second.key, 0);
    if (L == 0) return;
    L->missed += (int)n;
    g_ledMissed += n;
    BdOwnDirty(2);
}
void detour_refund(void* b)
{
    ::InterlockedIncrement64(&g_refundSeen);
    if (BdOnMain() == 0) ::InterlockedIncrement64(&g_refundOffMain);
    else
    {
        if (IsStandInFaction((::Faction*)BdOwnerPod(b)))   /* stand1 fold (2b): a save's coop-peer too */   /* stand1: ANY player's stand-in (coop-p<n>) - a copy of another game's piece */
        {
            ++g_refundCalledOnCopy;
            float sum = 0.0f;
            if (BdMatsSumZero(b, 0, &sum) >= 0 && sum > 0.0f) ++g_refundOnCopy;
        }
    }
    /* P3: the refund places its items with Item::activate inside the original (build/decomp_29dbd0.txt) - items.cpp notes each one
       and sends it down the one ground drop road (GROUND ADD from the holder / PUT from a non-holder for this game's own piece; a
       copy's is removed here, and an NPC / world piece's is removed on a non-holder - ground5 fold 1). The source is read before the
       original; End runs on every exit (F7, BdRefundBracketed). */
    const int onMain = BdOnMain();
    long long miss0 = 0;
    if (onMain != 0) { BdRefundNoteSrc(b); miss0 = GroundRefundMissedCount(); GroundRefundBegin(b, BuildGroundSource(b)); }   /* P14 fold 1: the piece's key first */
    BdRefundBracketed(b, onMain);
    if (onMain != 0) BdRefundNoteMissed(b, GroundRefundMissedCount() - miss0);   /* P14 fold 1: items the ground road never keyed (> 64, not on the ground, no key) */
}

const char* HkWord(int s) { return s == 1 ? "installed" : (s == -2 ? "NOT IN THE ADDRESS TABLE" : "FAILED"); }

// ---- the lever's pieces ----
void BdTestFail(const std::string& why)
{
    ++g_testPlaceFailed;
    DebugLog("[BUILD] buildtest place FAILED: " + why);
}

int BdFindMyCharPos(::Faction* mine, float* pos)
{
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0 || !net::IsUidMine(u)) continue;
        if (BdCharFaction(c) != mine) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(c, &v)) continue;
        pos[0] = v.x; pos[1] = v.y; pos[2] = v.z;
        return 1;
    }
    /* T328: before any link the mirror names no own uid (a late-join host builds alone) - fall back to the engine's own
       character update list, as speech.cpp attacknear walks it (main thread, at the safe point) */
    if (coop::GameWorldPtr() == 0) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = *it;
        if (c == 0 || BdCharFaction(c) != mine) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(c, &v)) continue;
        pos[0] = v.x; pos[1] = v.y; pos[2] = v.z;
        return 1;
    }
    return 0;
}

void BdTestPlace()
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kBdCreateRva == 0) { BdTestFail("CreateBuilding is not in the address table"); return; }
    const unsigned long long rGet = Rva("GdcGetData"), rCont = Rva("GdContainer"), rZm = Rva("ZoneManagerPtr");
    if (rGet == 0 || rCont == 0 || rZm == 0) { BdTestFail("GdcGetData / GdContainer / ZoneManagerPtr missing from the address table"); return; }
    void* gd = BdGetData(base + (uintptr_t)rGet, base + (uintptr_t)rCont, &g_btArg);
    if (gd == 0) { BdTestFail("no GameData record with stringID '" + g_btArg + "'"); return; }
    int type = -1;
    char gname[96];
    BdIntAt(gd, kGdType, &type);
    BdStrPod((const char*)gd + kGdName, gname, (int)sizeof gname);
    if (type != 0)
    {
        char b[240];
        std::sprintf(b, "record '%.80s' ('%.60s') is itemType %d, not BUILDING (0)", g_btArg.c_str(), gname, type);
        BdTestFail(std::string(b));
        return;
    }
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) { BdTestFail("this game has no player faction"); return; }
    float cp[3];
    if (!BdFindMyCharPos(mine, cp)) { BdTestFail("no own player-faction character with a readable position"); return; }
    float pos[3];
    /* createBuilding adds the ground height at (x, z) to the y it is given (it takes a height above the ground), so 0 sets the
       piece on the ground; the character's own height would float it by about the ground height. */
    pos[0] = cp[0] + g_btDx; pos[1] = 0.0f; pos[2] = cp[2] + g_btDz;
    /* the recipe's zone check: zone = ZoneManager + 200 + (cellX*64 + cellZ)*0x168 (0xA07C10), newGameFirstTimeLoaded +0xA8 */
    void* zm = BdPtrAt((const void*)(base + (uintptr_t)rZm), 0);
    if (zm == 0) { BdTestFail("no zone manager"); return; }
    const Sector s = SectorOf(pos[0], pos[2]);
    int fresh = 0;
    if (!BdByteAt(zm, 200 + ((size_t)s.x * 64 + (size_t)s.y) * 0x168 + 0xA8, &fresh)) { BdTestFail("the zone flag could not be read"); return; }
    if (fresh != 0)   /* review-build1a / T319: the owner swap needs a NON-player owner (answers-build1b R1) - ours is this
                         game's player faction, so the flag is noted, not refused (the copy path in build1-b must refuse) */
    {
        ++g_zoneFreshRefused;
        char b[200];
        std::sprintf(b, "[BUILD] buildtest place: zone %d,%d has newGameFirstTimeLoaded (+0xA8) set - exempt, the owner is a player faction",
                     s.x, s.y);
        DebugLog(std::string(b));
    }
    void* factory = (coop::GameWorldPtr() != 0) ? BdPtrAt((const void*)coop::GameWorldPtr(), kWorldFactory) : 0;
    if (factory == 0) { BdTestFail("no RootObjectFactory (GameWorld+0x4A0)"); return; }
    float rot[4];
    rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;   /* Ogre::Quaternion w,x,y,z - identity */
    const BdCreateFn fn = (orig_create != 0) ? orig_create : (BdCreateFn)(base + (uintptr_t)kBdCreateRva);
    int faulted = 0;
    void* b = BdCallCreate(fn, factory, gd, pos, (void*)mine, rot, false, &faulted);
    if (b == 0)
    {
        char e[240];
        std::sprintf(e, "createBuilding %s for '%.80s' ('%.60s') at (%.1f, %.1f, %.1f)", faulted ? "FAULTED" : "returned null",
                     g_btArg.c_str(), gname, pos[0], pos[1], pos[2]);
        BdTestFail(std::string(e));
        return;
    }
    void* o = BdOwnerPod(b);
    int fixed = 0;
    if (o != (void*)mine)
    {
        fixed = BdSetFactionPod(b, (void*)mine) ? 1 : -1;
        ++g_ownerFixed;
    }
    const int mined = BdMiningPod(b);
    ++g_testPlaced;
    char l[400];
    std::sprintf(l, "[BUILD] buildtest place CREATED '%.80s' ('%.60s') at (%.1f, %.1f, %.1f) zone %d,%d ownerAtReturn=%s%s vt2D8=%s",
                 g_btArg.c_str(), gname, pos[0], pos[1], pos[2], s.x, s.y, o == (void*)mine ? "mine" : "OTHER",
                 fixed == 0 ? "" : (fixed == 1 ? " ownerFixed(vt+0xA0)" : " ownerFix FAULTED"), mined ? "ok" : "FAULTED");
    DebugLog(std::string(l));
    BdOnNewPiece(b, 2, pos, rot, 1, 0, 0, 0, 0);
}

/* build1-e lever: `buildtest into near <sid>` - furniture <sid> inside the NEAREST loaded building with an interior (+0x1F0)
   within 3000 u of this game's first own player character. Created as the commit does: furnitureOf = *(host+0x1F0)+0x30
   (form 0), isIndoorsOf = host, town 0, owner = this game's player faction; pos in the host's LOCAL frame (inverse host
   rotation times (world - host position), answers R1) for the world point host centre + (25, 0, 25); rot = identity in
   that frame (the host's own facing). Then the same new-piece path (registry + PLACE with the host key). */
void BdIntoFail(const std::string& why)
{
    ++g_testIntoFailed;
    BdTestFail("into: " + why);
}
void BdTestInto()
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kBdCreateRva == 0) { BdIntoFail("CreateBuilding is not in the address table"); return; }
    const unsigned long long rGet = Rva("GdcGetData"), rCont = Rva("GdContainer");
    if (rGet == 0 || rCont == 0) { BdIntoFail("GdcGetData / GdContainer missing from the address table"); return; }
    void* gd = BdGetData(base + (uintptr_t)rGet, base + (uintptr_t)rCont, &g_btArg);
    int type = -1;
    if (gd != 0) BdIntAt(gd, kGdType, &type);
    if (gd == 0 || type != 0) { BdIntoFail("no BUILDING record with stringID '" + g_btArg + "'"); return; }
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) { BdIntoFail("this game has no player faction"); return; }
    float cp[3];
    if (!BdFindMyCharPos(mine, cp)) { BdIntoFail("no own player-faction character with a readable position"); return; }
    static void* list[4096];
    int zones = 0, trunc = 0;
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    void* host = 0;
    float hp[3], best = (g_btHost == "near") ? 3000.0f * 3000.0f : 1.0e30f;   /* build1h: a named host at any distance */
    hp[0] = 0; hp[1] = 0; hp[2] = 0;
    int withInterior = 0, matched = 0, hostPl = 0;
    for (int i = 0; i < n; ++i)
    {
        float q[3];
        if (BdPtrAt(list[i], kBInterior) == 0) continue;
        if (!BdCopyF((const char*)list[i] + kObjPos, q, 3)) continue;
        ++withInterior;
        int plNow = 0;
        if (g_btHost != "near")   /* build1h: a host named by a substring of its key - the matching building nearest the character */
        {
            char mk[kKeyCap];
            mk[0] = 0;
            if (ObjectPositionKey(list[i], mk, kKeyCap, "", 3, 1) == 0 || std::strstr(mk, g_btHost.c_str()) == 0) continue;
            ++matched;
            int ps = -1;   /* the test spot has town copies of the same house record: a PLAYER's house wins, then the nearest */
            const int pc = BuildingOwnerClass(list[i], &ps);
            plNow = (pc == coopown::kOwnPeer) ? 1 : 0;   /* house1b (item 9): ANOTHER player's house - the placer's own is no hand-off host */
        }
        const float dx = q[0] - cp[0], dy = q[1] - cp[1], dz = q[2] - cp[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (plNow > hostPl || (plNow == hostPl && d2 < best)) { hostPl = plNow; best = d2; host = list[i]; hp[0] = q[0]; hp[1] = q[1]; hp[2] = q[2]; }
    }
    char l[600];
    if (host == 0)
    {
        std::sprintf(l, "no loaded building with an interior (+0x1F0) %s (%.0f, %.0f, %.0f) - %d buildings, %d with an interior,"
                     " walkTruncated=%d host='%.60s' keyMatches=%d", g_btHost == "near" ? "within 3000 of" : "whose key matches (any distance; nearest to)",
                     cp[0], cp[1], cp[2], n, withInterior, trunc, g_btHost.c_str(), matched);
        BdIntoFail(std::string(l));
        return;
    }
    char hk[kKeyCap];
    hk[0] = 0;
    if (ObjectPositionKey(host, hk, kKeyCap, "", 3, 1) == 0) { BdIntoFail("the host's key could not be built"); return; }
    float hq[4];
    if (!BdOrientPod(host, hq)) { BdIntoFail("the host's rotation (vt+0xC0) could not be read"); return; }
    char* interior = (char*)BdPtrAt(host, kBInterior);
    void* factory = (coop::GameWorldPtr() != 0) ? BdPtrAt((const void*)coop::GameWorldPtr(), kWorldFactory) : 0;
    if (interior == 0 || factory == 0) { BdIntoFail("the host's interior or the RootObjectFactory went away"); return; }
    float d[3], local[3], rot[4];
    const float off = (g_btIndoors != 0) ? -25.0f : 25.0f;   /* build1h: form 2 at (-25, 0, -25), so the two pieces' keys differ */
    d[0] = off; d[1] = 0.0f; d[2] = off;   /* world point - host position */
    BdRotateInv(hq, d, local);
    rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;
    void* fo = (g_btIndoors != 0) ? (void*)0 : (void*)(interior + kInteriorLayout);   /* build1h: indoors = isIndoorsOf only (form 2) */
    const BdCreateFn fn = (orig_create != 0) ? orig_create : (BdCreateFn)(base + (uintptr_t)kBdCreateRva);
    int faulted = 0;
    void* b = BdCallCreateEx(fn, factory, gd, local, (void*)mine, rot, fo, host, false, 0, false, &faulted);
    if (b == 0)
    {
        std::sprintf(l, "createBuilding %s for '%.80s' in host=%s local=(%.1f, %.1f, %.1f)", faulted ? "FAULTED" : "returned null",
                     g_btArg.c_str(), hk, local[0], local[1], local[2]);
        BdIntoFail(std::string(l));
        return;
    }
    void* o = BdOwnerPod(b);
    int fixed = 0;
    if (o != (void*)mine) { fixed = BdSetFactionPod(b, (void*)mine) ? 1 : -1; ++g_ownerFixed; }
    const int mined = BdMiningPod(b);
    ++g_testInto;
    ++g_testPlaced;
    std::sprintf(l, "[BUILD] buildtest into CREATED '%.80s' host=%s dist=%.0f hostPos=(%.1f, %.1f, %.1f) hostRot=(%.3f, %.3f, %.3f, %.3f)"
                 " local=(%.1f, %.1f, %.1f) form=%d ownerAtReturn=%s%s vt2D8=%s keyMatches=%d", g_btArg.c_str(), hk, std::sqrt(best), hp[0], hp[1], hp[2],
                 hq[0], hq[1], hq[2], hq[3], local[0], local[1], local[2], g_btIndoors != 0 ? 2 : 0, o == (void*)mine ? "mine" : "OTHER",
                 fixed == 0 ? "" : (fixed == 1 ? " ownerFixed(vt+0xA0)" : " ownerFix FAULTED"), mined ? "ok" : "FAULTED", matched);
    DebugLog(std::string(l));
    int hw = coopown::kHoNoHost;
    const int handed = BdHandToHouseOwner(b, host, &hw);   /* build1h: the same step as the commit's */
    BdOnNewPiece(b, 2, local, rot, 1 | 2, host, g_btIndoors != 0 ? (int)coopbuild::kBuildHostIndoorsOnly : (int)coopbuild::kBuildHostPlain, 0, 0,
                 handed, hw);
}

/* the first registry entry whose key contains `sub` (a live one before a dismantled one) */
int BdFindReg(const std::string& sub, std::string* key)
{
    std::map<std::string, BdReg>::iterator fallback = g_reg.end();
    for (std::map<std::string, BdReg>::iterator it = g_reg.begin(); it != g_reg.end(); ++it)
    {
        if (it->first.find(sub) == std::string::npos) continue;
        if (it->second.dismantled == 0) { *key = it->first; return 1; }
        if (fallback == g_reg.end()) fallback = it;
    }
    if (fallback == g_reg.end()) return 0;
    *key = fallback->first;
    return 1;
}

void BdTestOnEntry(LONG kind)
{
    const char* verb = (kind == 2) ? "progress" : "dismantle";
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog(std::string("[BUILD] buildtest ") + verb + " SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator trow = g_reg.find(key);
    void* b = (trow != g_reg.end()) ? BdFindPiece(key, trow->second, &tv) : 0;   /* build1-e: furniture inside its host */
    char k2[kKeyCap];
    k2[0] = 0;
    if (b == 0 || ObjectPositionKey(b, k2, kKeyCap, "", 3, 0) == 0 || key != std::string(k2))
    {
        ++g_testNotLoaded;
        DebugLog(std::string("[BUILD] buildtest ") + verb + " SKIPPED: key=" + key + (b == 0 ? " is not loaded here (no live piece)" : " live piece's key reads '" + std::string(k2) + "'"));
        return;
    }
    float p0 = 0, n0 = 0, p1 = 0, n1 = 0;
    int c0 = 0, d0 = 0, x0 = 0, c1 = 0, d1 = 0, x1 = 0;
    BdReadState(b, &p0, &n0, &c0, &d0, &x0);
    if (d0 != 0)
    {
        ++g_testNotLoaded;
        DebugLog(std::string("[BUILD] buildtest ") + verb + " SKIPPED: key=" + key + " is already dismantled (+0x162)");
        return;
    }
    int ok = 0, ret = -1;
    if (kind == 2) ok = BdProgressPod(b, g_btAmount);
    else ok = BdDismantlePod(b, FLT_MAX, &ret);
    const int stOk = BdReadState(b, &p1, &n1, &c1, &d1, &x1);
    if (!ok) ++g_testFaulted;
    else if (kind == 2) ++g_testProgress;
    else ++g_testDismantle;
    char l[400];
    if (kind == 2)
        std::sprintf(l, "[BUILD] buildtest progress %s key=%s amount=%g progress %.2f/%.2f -> %.2f/%.2f complete %d->%d (vt+0x230)",
                     ok ? "CALLED" : "FAULTED", key.c_str(), g_btAmount, p0, n0, p1, n1, c0, c1);
    else
        std::sprintf(l, "[BUILD] buildtest dismantle %s key=%s returned=%d progress %.2f/%.2f -> %.2f/%.2f isDismantled=%d%s (vt+0x248 FLT_MAX)",
                     ok ? "CALLED" : "FAULTED", key.c_str(), ret, p0, n0, p1, n1, d1, stOk ? "" : " state=unreadable");
    DebugLog(std::string(l));
    if (kind == 2 && ok && c0 == 0 && c1 != 0) ItFurnMarkStale(3);   /* T-454: the lever completed the piece (0 -> 1) */
}

/* P15 TEST-ONLY `buildtest destroy <key-substring>` (final-form test code: nothing happens unless the test tool sends it). The piece is made
   a ruin by the ENGINE's own setDestroyed (vt+0x340 -> 0x5567F0, the call a wall's melee hit makes at 0 health, 29d520), outside the
   mod's own-write scope - exactly this game's engine destroying it. On an OWN piece its STATE (why=destroyed) follows; on a COPY the
   owner's last STATE goes back on (the owner is the writer). The repair road is `buildtest progress <key> <amount>`: vt+0x230 on a ruin
   runs the engine's ruin reset and setDestroyed(0) (558b10:59-86). */
void BdTestDestroy()
{
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog("[BUILD] buildtest destroy SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator trow = g_reg.find(key);
    void* b = (trow != g_reg.end()) ? BdFindPiece(key, trow->second, &tv) : 0;
    char k2[kKeyCap];
    k2[0] = 0;
    if (b == 0 || ObjectPositionKey(b, k2, kKeyCap, "", 3, 0) == 0 || key != std::string(k2))
    {
        ++g_testNotLoaded;
        DebugLog("[BUILD] buildtest destroy SKIPPED: key=" + key + (b == 0 ? " is not loaded here (no live piece)" : " live piece's key reads '" + std::string(k2) + "'"));
        return;
    }
    float p0 = 0, n0 = 0, p1 = 0, n1 = 0;
    int c0 = 0, d0 = 0, x0 = 0, c1 = 0, d1 = 0, x1 = 0;
    BdReadState(b, &p0, &n0, &c0, &d0, &x0);
    if (d0 != 0 || x0 != 0)
    {
        ++g_testAlreadyRuin;   /* P15 fold 1 (LOW 8): not "not loaded" - the piece is here */
        DebugLog("[BUILD] buildtest destroy SKIPPED: key=" + key + (d0 != 0 ? " is dismantled (+0x162)" : " is already a ruin (+0x1A1)"));
        return;
    }
    DoorsForgetOwner(b);   /* P15 fold 1 (CRASH 1a): the building's door rows go before its doors do */
    const int ok = BdDestroyPod(b, true);
    BdReadState(b, &p1, &n1, &c1, &d1, &x1);
    if (!ok) ++g_testFaulted;
    else ++g_testDestroy;
    char l[400];
    std::sprintf(l, "[BUILD] buildtest destroy %s key=%s row=%s +0x1A1 %d -> %d progress %.2f/%.2f -> %.2f/%.2f complete %d->%d (vt+0x340 setDestroyed 0x5567F0)",
                 ok ? "CALLED" : "FAULTED", key.c_str(), trow->second.via == 3 ? "copy" : "own", x0, x1, p0, n0, p1, n1, c0, c1);
    DebugLog(std::string(l));
}

/* help1 (TEST-ONLY `buildtest mats <key-substring> <n>`): n more of EVERY material delivered to the piece, each capped at its total.
   A write of the delivered amounts through the STATE's own guarded write (BdApplyStatePod, mats only) - no engine addMaterials is in
   the address table; an own piece's STATE follows. */
void BdTestMats()
{
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog("[BUILD] buildtest mats SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    std::map<std::string, BdReg>::iterator row = g_reg.find(key);
    int tv = 0;
    void* b = (row != g_reg.end()) ? BdFindPiece(key, row->second, &tv) : 0;
    if (b == 0) { ++g_testNotLoaded; DebugLog("[BUILD] buildtest mats SKIPPED: key=" + key + " is not loaded here (no live piece)"); return; }
    float cur[coopbuild::kBuildMaxMats], tot[coopbuild::kBuildMaxMats], nw[coopbuild::kBuildMaxMats];
    int ct = -1;
    const int cn = BdReadMats(b, cur, (int)coopbuild::kBuildMaxMats, &ct);
    const int tn = BdReadMatTotals(b, tot, (int)coopbuild::kBuildMaxMats);
    if (cn <= 0) { ++g_testFaulted; DebugLog("[BUILD] buildtest mats SKIPPED: key=" + key + " lists no readable materials"); return; }
    char ms[400];
    ms[0] = 0;
    for (int i = 0; i < cn; ++i)
    {
        float ex = 0.0f;
        nw[i] = coopbuild::BuildHelpMatApply(cur[i], g_btAmount, (i < tn) ? tot[i] : -1.0f, &ex);
        char one[64];
        _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%.1f->%.1f/%.1f", i == 0 ? "" : ",", cur[i], nw[i], (i < tn) ? tot[i] : -1.0f);
        if (std::strlen(ms) + std::strlen(one) < sizeof(ms) - 1) std::strcat(ms, one);
    }
    int ml = -1, stg = 0;
    const int ok = BdApplyStatePod(b, 0, 0.0f, cn, nw, &ml, 0, &stg);   /* mats only: 0x558680 is never reached */
    if (ok) ++g_testMats; else ++g_testFaulted;
    BdReg& r = row->second;
    int tt = -1;
    r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &tt);
    char l[700];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] buildtest mats %s key=%s +%g each (delivered->new/total): [%s] own=%d via=%d (a delivered-amount write - no engine addMaterials is in the table)",
                ok ? "CALLED" : "FAULTED", key.c_str(), g_btAmount, ms, r.own, r.via);
    DebugLog(std::string(l));
    if (ok && r.own != 0 && r.via != 3) { BdOwnDirty(1); BdMaybeSendState(key, r); }
}

// ---- build1-b: the copy (build/answers-build1b.md "Safe call recipe"; the K2 safe point 0x7D17E0 only) ----
const char* BdWhose(void* o, void* peer)
{
    if (o == 0) return "none";
    if (o == (void*)LocalPlayerFaction()) return "mine";
    if (o == peer) return "peer";
    return IsStandInFaction((::Faction*)o) ? "other-stand-in" : "other";
}
/* MAIN THREAD. Every copy belongs to the stand-in faction of ONE player - the player whose piece it is - and every PLACE, STATE and
   REMOVE is judged against that player's stand-in, never "the one other player": with the world server relaying every game, a
   building's copy carries the stand-in of the slot that placed it. BdSenderStandIn: the stand-in of a sender slot as recorded at
   arrival (coopbuild::BuildSenderSlotNow; 0 = the slot is not known yet or its stand-in does not exist here yet - the entry waits).
   BdRowStandIn: the stand-in of the owner slot a copy row recorded when it was made or adopted. */
::Faction* BdSenderStandIn(int fromSlot, int* slotOut)
{
    const int s = coopbuild::BuildSenderSlotNow(fromSlot, LinkPeerSlot());
    if (slotOut != 0) *slotOut = s;
    return (s >= 0) ? StandInForSlot(s) : 0;
}
::Faction* BdRowStandIn(const BdReg& r) { return (r.ownerSlot >= 0) ? StandInForSlot(r.ownerSlot) : 0; }
/* P87 fold 3 (T651), MAIN THREAD: the engine reads for coopbuild::BuildOwnerClass - THE ONE OWNER RULE BdCopyOne (PLACE adopt),
   BdReconDrain (reconcile) and BdRemoveOne (REMOVE apply) ask. senderStandIn: the sender's live stand-in (0: none yet); senderSlot:
   the sender's notebook slot (-1 unknown: the record rule is off). The record id (StandInRecordSlot, guarded reads) is what survives
   a restart - the stand-in's pointer does not. */
int BdOwnerClass(void* o, void* senderStandIn, int senderSlot, int listSlot = -1, int listSidOk = 0, int ownRowHere = 0)
{
    ::Faction* f = (::Faction*)o;
    const int isMine = (o != 0 && o == (void*)LocalPlayerFaction()) ? 1 : 0;
    const int isSenderLive = (o != 0 && isMine == 0 && ((senderStandIn != 0 && o == senderStandIn) || (senderSlot >= 0 && StandInSlotOf(f) == senderSlot))) ? 1 : 0;
    const int rec = (o != 0 && isMine == 0 && isSenderLive == 0) ? StandInRecordSlot(f) : -1;
    return coopbuild::BuildOwnerClass(o == 0 ? 1 : 0, isMine, isSenderLive, (o != 0 && IsStandInFaction(f)) ? 1 : 0, rec, senderSlot,
                                      listSlot, listSidOk, ownRowHere);   /* P87 root: the copy list first */
}
/* P87 fold 3, MAIN THREAD (the K2 safe point): a piece a save's stand-in owns takes the sender's live stand-in as its owner - what the
   PLACE adopt has done since build1h - so every later pointer test (STATE, REMOVE, refund counts) sees the sender's copy.
   1 fixed, -1 the write failed, 0 nothing to fix. */
int BdOwnerFixToSender(void* b, int cls, void* senderStandIn)
{
    if ((cls != coopbuild::kBuildLiveOwnSenderRecord && cls != coopbuild::kBuildLiveOwnSenderList) || b == 0 || senderStandIn == 0) return 0;
    if (BdOwnerPod(b) == senderStandIn) return 0;   /* P87 root: a listed copy the live stand-in already owns */
    return BdSetFactionPod(b, senderStandIn) ? 1 : -1;
}
/* P87 root (step 5), MAIN THREAD, READ-ONLY: the owner a read-back copy's piece has before the mod touches it - measures the root cause's
   Inferred claims (the save's 'coop-p<slot>' unresolved at load, the town's faction instead). Once per read-back entry: at the first
   look of the owner rule (road) or the zone-load pass, whichever comes first. live 0 = no piece (verdict says why). */
void BdCopyListDiagOne(const std::string& key, BdCopyEnt& e, void* live, int verdict, const char* at)
{
    if (e.diag != 0) return;
    e.diag = 1;
    if (g_copyDiagOwed > 0) --g_copyDiagOwed;
    ++g_clDiagLogged;
    if (live != 0) { if (e.rec.emptyLoads != 0) { e.rec.emptyLoads = 0; BdOwnDirty(2); } }   /* P87 rf1: found - the count starts again */
    else if (verdict == kObjKeyNone)
    {   /* P87 rf1: a complete empty search in this load */
        ++e.rec.emptyLoads;
        BdOwnDirty(2);
        if (coopbuild::BuildCopyEvict(e.rec.emptyLoads) != 0) e.evict = 1;
        if (e.rec.removed != 0) BdCopyListSettled(key, "the load's complete search found no piece at the key");
    }
    void* o = (live != 0) ? BdOwnerPod(live) : 0;
    char sid[160];
    sid[0] = 0;
    if (live != 0) BdSidOf(live, sid, (int)sizeof(sid));
    ::Faction* f = (::Faction*)o;
    const int rec = (o != 0) ? StandInRecordSlot(f) : -1;
    const int isMine = (o != 0 && o == (void*)LocalPlayerFaction()) ? 1 : 0;
    const int isStandIn = (o != 0 && IsStandInFaction(f)) ? 1 : 0;
    const std::string on = (o != 0) ? BdFactionName(o) : std::string("-");
    char line[700];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list: key=%.63s slot=%d sid=%.60s ownerAtLoad='%.60s' ownerRecordSlot=%d ownerIsMine=%d ownerIsStandIn=%d found=%s liveSid=%.60s at=%s - read-only, before any re-own",
                key.c_str(), e.rec.slot, e.rec.sid.c_str(), on.c_str(), rec, isMine, isStandIn, live != 0 ? "yes" : BdVerdictWord(verdict),
                live != 0 ? sid : "-", at);
    BdLogCopyList(line);
}
/* P87 root (step 5), MAIN THREAD (BuildCopyDrain, every kRetryGap safe points, at most 4 lookups): the read-back entries not looked at yet.
   A free piece once its zone's saved pieces are all in (ZoneMap+0xB1, as the restore's search), furniture once its host resolves; only a
   found piece or a complete empty search is logged - anything else is asked again. */
void BdCopyListDiagTick()
{
    if (g_copyDiagOwed <= 0 || g_copyList.empty()) { g_copyDiagOwed = 0; return; }
    if ((g_drainNo % kRetryGap) != 0) return;
    int looks = 0;
    std::vector<std::string> evict;
    /* P87 rf1 (LOW): a rotating cursor - entries looked at without an answer (a host not resolved) no longer hold the first slots */
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.upper_bound(g_clDiagCursor);
    const size_t nEnt = g_copyList.size();
    for (size_t seen = 0; seen < nEnt && looks < 4; ++seen, ++it)
    {
        if (it == g_copyList.end()) it = g_copyList.begin();
        g_clDiagCursor = it->first;
        BdCopyEnt& e = it->second;
        if (e.diag != 0) continue;
        void* b = 0;
        int v = kObjKeyRefused;
        if (e.rec.hostKey.empty())
        {
            if (ZoneBuildingsInHereTri(e.rec.pos[0], e.rec.pos[1], e.rec.pos[2]) <= 0) continue;
            ++looks;
            b = ObjectByPositionKey(it->first.c_str());
            v = ObjectByPositionKeyVerdict();
        }
        else
        {
            ++looks;
            void* host = ObjectByPositionKey(e.rec.hostKey.c_str());
            if (host == 0) continue;
            b = BdFindInHost(host, it->first.c_str(), "", &v);
        }
        if (b == 0 && v != kObjKeyNone) continue;
        BdCopyListDiagOne(it->first, e, b, v, "zone-load");
        if (e.evict != 0) evict.push_back(it->first);
    }
    for (size_t i = 0; i < evict.size(); ++i)   /* P87 rf1 (MED): stale entries leave the list */
    {
        std::map<std::string, BdCopyEnt>::iterator ev = g_copyList.find(evict[i]);
        if (ev == g_copyList.end()) continue;
        const int loads = ev->second.rec.emptyLoads, sl = ev->second.rec.slot, wasRm = ev->second.rec.removed;
        g_copyList.erase(ev);
        g_copyRetire.push_back(coopbuild::BuildCopyRowKey(evict[i]));
        ++g_clEvicted;
        BdOwnDirty(2);
        char line[400];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s EVICTED: %d loads in a row found no piece at the key (slot %d's%s) - its '~c' row leaves pp.build; copyListEvicted=%lld",
                    evict[i].c_str(), loads, sl, wasRm != 0 ? ", marked removed" : "", g_clEvicted);
        BdLogCopyList(line);
    }
}
/* P87 root, MAIN THREAD: THE ONE OWNER RULE with its first test - BdCopyOne (adopt), BdReconDrain (reconcile) and BdRemoveOne (REMOVE)
   ask this. live: the piece found at key (0: none - the list is not asked). The list entry (exact key, else BoxKeyNearKey's window) of
   the sender's slot with the live piece's sid makes it the sender's copy ('copy-list'), unless this game holds its own piece at the key. */
int BdOwnerClassAt(void* live, const std::string& key, void* senderStandIn, int senderSlot, const char* road)
{
    void* o = (live != 0) ? BdOwnerPod(live) : 0;
    int listSlot = -1, sidOk = 0, ownHere = 0;
    std::string lk;
    BdCopyEnt* ce = 0;
    if (live != 0 && !g_copyList.empty())
    {
        ownHere = BdCopyOwnRowAt(key);
        if (ownHere == 0) ce = BdCopyListFind(key, &lk);
    }
    char sid[160];
    sid[0] = 0;
    if (ce != 0)
    {
        BdCopyListDiagOne(lk, *ce, live, kObjKeyFound, road);   /* the owner the save gave it, before anything below re-owns it */
        BdSidOf(live, sid, (int)sizeof(sid));
        listSlot = ce->rec.slot;
        sidOk = (sid[0] != 0 && ce->rec.sid == sid) ? 1 : 0;
    }
    const int cls = BdOwnerClass(o, senderStandIn, senderSlot, listSlot, sidOk, ownHere);
    if (ce != 0 && cls != coopbuild::kBuildLiveOwnSenderList)
    {
        ++g_clNotTaken;
        char line[500];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s NOT taken by the %s: listed as slot %d's (sid %.60s), the live piece's sid=%.60s, sender slot %d - the fallback rule answered %s; copyListNotTaken=%lld",
                    key.c_str(), road, listSlot, ce->rec.sid.c_str(), sid[0] != 0 ? sid : "?", senderSlot, coopbuild::BuildOwnerWord(cls), g_clNotTaken);
        BdLogCopyList(line);
    }
    return cls;
}
/* P87 root: an adopted copy's entry keeps the sid the engine's piece really carries (a SID SUBSTITUTED piece) */
void BdCopyListSidLive(const std::string& key, void* live)
{
    std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(key);
    if (it == g_copyList.end() || live == 0) return;
    char sid[160];
    sid[0] = 0;
    BdSidOf(live, sid, (int)sizeof(sid));
    if (sid[0] == 0 || it->second.rec.sid == sid) return;
    it->second.rec.sid = sid;
    ++g_clRefreshed;
    BdOwnDirty(2);
}
/* P87 root (step 2), MAIN THREAD (BuildRestoreQueue, the load edge): the '~c' rows pp.build brought back become the list. ownKeys: the
   keys of this game's own pp.build PLACE rows in the same record - own rows always win (the entry is retired). A key the load scan
   already registered as this game's own (via=scan, this world, never sent) before the list was read is taken back from the registry. */
void BdCopyListLoaded(const std::vector<coopbuild::BuildCopyRec>& rows, const std::set<std::string>& ownKeys)
{
    if (rows.empty()) return;
    char line[500];
    unsigned int kept = 0, fullNow = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const coopbuild::BuildCopyRec& c = rows[i];
        ++g_clRead;
        std::map<std::string, BdReg>::iterator rg = g_reg.find(c.key);
        const int scanRow = (rg != g_reg.end() && rg->second.own != 0 && rg->second.via == 4 && rg->second.gen == g_bdGen && rg->second.removed == 0) ? 1 : 0;
        if (ownKeys.find(c.key) != ownKeys.end() || (scanRow == 0 && BdCopyOwnRowAt(c.key) != 0))
        {
            ++g_clOwnWins;
            g_copyRetire.push_back(coopbuild::BuildCopyRowKey(c.key));
            BdOwnDirty(2);
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s read back and dropped: this game holds (or restores) its own piece there - own rows always win; copyListOwnWins=%lld",
                        c.key.c_str(), g_clOwnWins);
            BdLogCopyList(line);
            continue;
        }
        if (g_copyList.find(c.key) == g_copyList.end() && g_copyList.size() >= coopbuild::kBuildCopyListCap) { ++g_clFull; ++g_clLoadFull; ++fullNow; continue; }
        BdCopyEnt& e = g_copyList[c.key];
        e.rec = c; e.readBack = 1;
        e.rmReady = 0; e.rmReq = 0; e.evict = 0;   /* P87 rf1: a read-back removed mark waits until this world settles it */
        if (c.removed != 0) { ++g_clMarkRead; BdReconQueue(c.key, c.slot); }   /* P87 rf1 (HIGH): the reconcile removes a returning copy - of the slot the list names */
        if (e.diag != 0) { e.diag = 0; ++g_copyDiagOwed; }
        ++kept;
        if (scanRow != 0)
        {
            ++g_clScanBefore;
            const int never = (rg->second.sentOk == 0) ? 1 : 0;
            if (never != 0)
            {
                g_reg.erase(rg);
                for (size_t j = 0; j < g_rsOwePlace.size(); ) { if (g_rsOwePlace[j] == c.key) g_rsOwePlace.erase(g_rsOwePlace.begin() + (std::ptrdiff_t)j); else ++j; }
                coopown::BuildRetireNote(&g_ownRetireOwed, c.key, g_bdGen);
                BdOwnDirty(2);
                ++g_clScanDemoted;
            }
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s: the load scan registered it as THIS game's own before the list was read - %s; copyListScanBefore=%lld",
                        c.key.c_str(), never != 0 ? "taken back (never sent: its own row and PLACE are dropped)" : "LEFT (its PLACE already went to the other game)", g_clScanBefore);
            BdLogCopyList(line);
        }
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list read key=%.63s slot=%d sid=%.60s nonce=%08x%s%s%s emptyLoads=%d",
                    c.key.c_str(), c.slot, c.sid.c_str(), c.nonce, c.hostKey.empty() ? "" : " (furniture)",
                    c.removed != 0 ? " REMOVED-mark (a returning copy is removed by the reconcile)" : "", c.alias != 0 ? " alias" : "", c.emptyLoads);
        BdLogCopyList(line);
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list read: %u copies kept of %u '~c' rows (ownWins=%lld, badRows=%lld, notKeptListFull=%u of cap %u) - the owner rule asks it first (adopt, reconcile, REMOVE)",
                kept, (unsigned int)rows.size(), g_clOwnWins, g_clBadRows, fullNow, (unsigned int)coopbuild::kBuildCopyListCap);
    BdLogCopyList(line);
}

/* build1-c: a copy that has just been made or adopted takes the PLACE's own progress (recipe step 4), unless a newer STATE is
   already held for it; any held STATE is due at once (the same safe point's STATE pass). */
void BdQueuePlaceState(const coopbuild::BuildMsg& m)
{
    std::map<std::string, BdStatePend>::iterator it = g_pendState.find(m.key);
    if (it != g_pendState.end()) { it->second.nextTry = 0; return; }
    if (m.complete != 0 || !(m.progress > 0.0f)) return;
    BdStatePend& s = g_pendState[m.key];
    s.m = coopbuild::BuildMsg();
    s.m.kind = coopbuild::kBuildState; s.m.key = m.key; s.m.progress = m.progress; s.m.needed = m.needed; s.m.complete = 0; s.m.nMats = 0;
    s.nextTry = 0; s.fromPlace = 1; s.waitLogged = 0; s.deferTries = 0;   /* P15 fold 1 */
}

/* mmo8a3 (A), MAIN THREAD (the K2 safe point): is a pending entry's area loaded here. A RESTORE row asks the engine's own
   "this zone is loaded" byte (zones.cpp ZoneBuildingsInHereTri, ZoneMap+0xB1): it is set only after the zone's saved pieces
   were all created and the zone joined activeZones, and cleared first thing at its unload, so the complete search that
   follows is a search of a zone whose saved pieces are all present (a refused or faulted read is "not loaded": the row
   waits). Every other entry keeps IsPositionLoadedHere (+0xB3) unchanged. */
int BdAreaReady(const BdPending& p, float x, float y, float z)
{
    if (p.restore == 0) return IsPositionLoadedHere(x, y, z) ? 1 : 0;
    const int g = ZoneBuildingsInHereTri(x, y, z);
    if (g > 0) { ++g_rsGateB1Loaded; return 1; }
    if (g == 0) ++g_rsGateB1Not;
    else ++g_rsGateB1Fault;
    return 0;
}

/* build1h H1, MAIN THREAD: the owner slot a copy row records, and for a piece HANDED TO THIS GAME the own row that makes this game its
   STATE / REMOVE source (via 5 "handed"): marked sent (the placer's game made its copy from the same PLACE) and the placement inputs
   kept for the link-up roster. */
void BdMarkOwner(BdReg& r, const coopbuild::BuildMsg& m, int toMe, int slot)
{
    r.ownerSlot = slot;
    r.gen = g_bdGen;   /* mmo8a3 (D2): touched live in THIS world - pp.build takes only the current world's rows */
    r.nonce = m.nonce;   /* house2 fold (MED2): the accepted placement's nonce - what a later REMOVE here records */
    r.handed = (m.ownerSlot == coopbuild::kBuildOwnerSender) ? 0 : (toMe != 0 ? 2 : 3);
    if (toMe == 0) { BdCopyListNote(m.key, r, m, slot); return; }   /* P87 root: a copy made or adopted here - listed ('~c' in pp.build) */
    BdCopyListRetire(m.key, "handed to this game (its own piece now)");   /* P87 root */
    r.via = 5; r.own = 1;
    BdTombDrop(m.key, "a PLACE handed to this game landed at the key", 0);   /* P87 fold 1 (H2): the owner holds a piece there again */
    r.sentOk = 1; r.sentProgress = r.progress; r.sentComplete = r.complete; r.sentN = 0;
    r.placeOk = 1;
    for (int i = 0; i < 3; ++i) r.placePos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) r.placeRot[i] = m.rot[i];
    r.floor = m.floor; r.outside = (m.outside != 0) ? 1 : 0;
    ++g_handedRecv;
    BdOwnDirty(2);   /* mmo8a: handed to this game - its pp.build row now */
    BdSendAck(m.key);   /* house1b (item 2): the placer stops re-sending */
}

/* mmo8a, MAIN THREAD (BdCopyOne): a piece re-made from this game's own pp.build row. The row is this game's own (via 6
   "restored"): no HAND_ACK (no hand-over happened), NOT marked sent (mmo8a2 M4: BdRosterTick sends its PLACE + STATE as one
   roster piece, so the other game gets its copy without waiting for the next link-up roster), the placement inputs kept for the roster, and the row's STATE written
   through the same guarded write a copy's STATE uses - progress capped below needed (0x558680 completes at >= needed). */
void BdMarkRestored(BdReg& r, BdPending& p, void* b, int slot)
{
    const coopbuild::BuildMsg& m = p.m;
    r.ownerSlot = slot; r.nonce = m.nonce; r.handed = 0;
    r.via = 6; r.own = 1; r.removed = 0;
    r.placeOk = 1;
    for (int i = 0; i < 3; ++i) r.placePos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) r.placeRot[i] = m.rot[i];
    r.floor = m.floor; r.outside = (m.outside != 0) ? 1 : 0;
    int applied = 0, stage = 0;
    if (p.rsHasState != 0 && r.complete == 0 && BdIsWall(b) == 0)
    {
        const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
        const float need = (r.needed > 0.0f) ? r.needed : p.st.needed;
        float pr = p.st.progress;
        if (pr < 0.0f) pr = 0.0f;
        if (pr > need * 0.999f) pr = need * 0.999f;   /* answers R2 trap: 0x558680 completes a piece at progress >= needed */
        const int writeProgress = (rUpd != 0 && pr > r.progress) ? 1 : 0;
        const uintptr_t upd = (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd;
        if (writeProgress != 0 || p.st.nMats > 0)
        {
            int matsLive = -1;
            applied = BdApplyStatePod(b, writeProgress, pr, (int)p.st.nMats, p.st.mats, &matsLive, upd, &stage) ? 1 : -1;
            float cp = 0, cn = 0;
            int cc = 0, cd = 0, cx = 0;
            if (BdReadState(b, &cp, &cn, &cc, &cd, &cx) != 0) { r.progress = cp; r.needed = cn; r.complete = cc; }
            float nm[coopbuild::kBuildMaxMats];
            int nmTotal = -1;
            const int nmN = BdReadMats(b, nm, (int)coopbuild::kBuildMaxMats, &nmTotal);
            if (nmN >= 0) { r.nMats = nmN; for (int i = 0; i < nmN; ++i) r.mats[i] = nm[i]; }
        }
    }
    r.sentOk = 0; r.sentProgress = r.progress; r.sentComplete = r.complete; r.sentN = 0;   /* mmo8a2 (M4): sent by BdRosterTick */
    r.gen = g_bdGen;
    g_rsOwePlace.push_back(m.key);
    p.rsOutcome = 2;
    ++g_rsRecreated;
    BdOwnDirty(1);
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s recreated owner=mine progress=%.2f/%.2f complete=%d state=%s (stage %d) via=restored",
                m.key.c_str(), r.progress, r.needed, r.complete, applied > 0 ? "applied" : (applied < 0 ? "FAULTED" : "none"), stage);
    BdLogRestore(line);
    ItFurnMarkStale(1);   /* T-454: the restore made the piece again here */
}

/* MAIN THREAD. createBuilding puts a piece at the ground height under its (x,z) plus the y it is given, so a placement input is a
   height above the ground. A standing piece's live position becomes its placement input by taking away the ground height at its
   (x,z), read with getTerrainHeight (the call the engine's own town placement reads the ground with). 1 = in[] holds the input;
   0 = no ground height was answered there (in[] untouched). The first 8 of each are logged. */
int BdPlaceInputFromLive(const std::string& key, const float* live, float* in)
{
    float g = 0.0f, y = 0.0f;
    const int gr = GroundTerrainHeightAt(live[0], live[2], &g);
    char line[400];
    if (placeinput::InputYFromLive(live[1], gr, g, &y) == 0)
    {
        ++g_placeFromLiveNoGround;
        if (g_placeFromLiveNoGround <= 8)
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] placement input key=%.63s NOT read: no ground height answered at (%.1f, %.1f) (read %d) - its PLACE is not sent until a scan reads it; placeFromLiveNoGround=%lld (first 8 logged)",
                        key.c_str(), live[0], live[2], gr, g_placeFromLiveNoGround);
            DebugLog(std::string(line));
        }
        return 0;
    }
    in[0] = live[0]; in[1] = y; in[2] = live[2];
    ++g_placeFromLive;
    if (g_placeFromLive <= 8)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] placement input key=%.63s from its live position: live y %.1f - ground %.1f = %.1f, the height above the ground createBuilding takes; placeFromLive=%lld (first 8 logged)",
                    key.c_str(), live[1], g, y, g_placeFromLive);
        DebugLog(std::string(line));
    }
    return 1;
}

/* build1-f ROSTER scan / help1 fold 5 (T-268), MAIN THREAD: a live piece of THIS game's player faction that the registry does not know
   is registered as this game's own row (via=4 scan): sid, owner, the live progress / complete / mats, not sent yet, this world's gen,
   the placement inputs given, then its PLACE's nonce from pp.build. b comes from the caller's own search on this thread, this tick.
   liveAbs 1: pos is the piece's LIVE position, turned into the placement input (height above the ground) by BdPlaceInputFromLive;
   when no ground height is answered the row keeps placeOk 0 (no PLACE is sent from it) and a later scan reads it again.
   liveAbs 0: pos already is a placement input (a PLACE record's own). */
BdReg& BdRegisterOwnLive(void* b, const std::string& key, const float* pos, const float* rot, float p, float nd, int c, int x, int liveAbs)
{
    char sid[96];
    BdSidOf(b, sid, (int)sizeof sid);
    float nm[coopbuild::kBuildMaxMats];
    int nmTotal = -1;
    const int nmN = BdReadMats(b, nm, (int)coopbuild::kBuildMaxMats, &nmTotal);
    BdReg& r = g_reg[key];
    r.sid = sid; r.owner = BdFactionName((void*)LocalPlayerFaction()); r.progress = p; r.needed = nd;
    r.complete = c; r.dismantled = 0; r.destroyed = x; r.via = 4; r.own = 1;
    r.nMats = nmN;
    for (int j = 0; j < nmN; ++j) r.mats[j] = nm[j];
    r.sentOk = 0; r.removed = 0;
    r.hostKey.clear(); r.hostForm = 0; r.liveKey.clear(); r.keySame = 1;
    r.gen = g_bdGen;   /* mmo8a2 (H1) */
    float in[3];
    for (int j = 0; j < 3; ++j) in[j] = pos[j];
    r.placeOk = (liveAbs == 0 || BdPlaceInputFromLive(key, pos, in) != 0) ? 1 : 0;
    for (int j = 0; j < 3; ++j) r.placePos[j] = in[j];
    for (int j = 0; j < 4; ++j) r.placeRot[j] = rot[j];
    r.floor = 0; r.outside = 0;
    BdRestoreNonce(key, r, 1);   /* help1 fold 4 (re-check #1): its placement nonce, when pp.build was read first */
    BdOwnDirty(1);   /* mmo8a: a piece from the save joins pp.build */
    return r;
}

/* mmo8a2 (review-mmo8a LOW), MAIN THREAD (BdCopyOne): a rebuilt piece whose created key differs from its record's key (KEY
   MISMATCH) - its row moves to the created piece's own key, so the next load's search finds it, and the old key is retired
   from pp.build (owed until written). A SID SUBSTITUTED piece needs nothing here: its row already keeps the engine's sid.
   mmo8a3 (C): the other game is owed a REMOVE for the OLD key (g_rsOweRemove, sent by BdRosterTick once the link is up and
   ahead of the owed PLACE for the new key), so it drops its copy at the old key instead of keeping it beside the new one. */
void BdRsRekey(const std::string& oldKey, const std::string& liveKey)
{
    std::map<std::string, BdReg>::iterator o = g_reg.find(oldKey);
    if (o == g_reg.end() || liveKey.empty() || liveKey == oldKey || g_reg.find(liveKey) != g_reg.end()) return;
    BdReg moved = o->second;
    g_reg.erase(o);
    moved.liveKey.clear(); moved.keySame = 1; moved.gen = g_bdGen;
    g_reg[liveKey] = moved;
    g_liveAlias.erase(liveKey);
    coopown::BuildRetireNote(&g_ownRetireOwed, oldKey, g_bdGen);
    for (size_t i = 0; i < g_rsOwePlace.size(); ++i) if (g_rsOwePlace[i] == oldKey) g_rsOwePlace[i] = liveKey;
    for (size_t i = 0; i < g_rsOweRemove.size(); ) { if (g_rsOweRemove[i] == liveKey) g_rsOweRemove.erase(g_rsOweRemove.begin() + (std::ptrdiff_t)i); else ++i; }   /* mmo8a3 (C): the new key is placed, not removed */
    coopbuild::BuildOweRemoveDrop(&g_dmOweRemove, liveKey);   /* par13: likewise for a dismantle's owed REMOVE */
    { int owed = 0; for (size_t i = 0; i < g_rsOweRemove.size(); ++i) if (g_rsOweRemove[i] == oldKey) owed = 1; if (owed == 0) g_rsOweRemove.push_back(oldKey); }   /* mmo8a3 (C) */
    ++g_rsRekeyed;
    BdOwnDirty(2);
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s re-keyed to %.63s (the created piece's own key) - the old key is retired from pp.build and its REMOVE is owed to the other game",
                oldKey.c_str(), liveKey.c_str());
    BdLogRestore(line);
}

/* 1 = finished with this PLACE (created, adopted, duplicate, or given up; mmo8a: a restore present / recreated / refused); 0 = keep it pending; 2 (build1-c) = its area is
   not loaded here, move it to the not-loaded list. *called = createBuilding ran.
   No __try here (C2712): every engine touch goes through the guarded Bd* helpers above. */
static int BdCopyOneBody(BdPending& p, int* called);
int BdCopyOne(BdPending& p, int* called)
{
    g_bdAckSlot = p.fromSlot;   /* M7b slice 4 fold 1 (F6): a HAND_ACK sent while this PLACE is worked goes to its placer */
    const int r = BdCopyOneBody(p, called);
    g_bdAckSlot = -1;
    return r;
}
static int BdCopyOneBody(BdPending& p, int* called)
{
    const coopbuild::BuildMsg& m = p.m;
    char line[600];
    /* build1h H1: whose the piece is. 0xFF = the sender's (its stand-in, as before); MY slot = handed to this game - made as its OWN
       piece; another slot = that slot's stand-in. A handed PLACE while this game does not know its own slot waits. */
    const int mySlot = MySlotForWire();
    const int handed = (m.ownerSlot != coopbuild::kBuildOwnerSender) ? 1 : 0;
    if (handed != 0 && mySlot < 0) { if (p.slotWaitCounted == 0) { p.slotWaitCounted = 1; ++g_handedWaitSlot; } p.nextTry = g_drainNo + kRetryGap; return 0; }
    const int toMe = (handed != 0 && (int)m.ownerSlot == mySlot) ? 1 : 0;
    /* house2 (item 4) + fold (MED2): a hand-over PLACE whose placement (key, nonce) was removed here is late (the placer re-sent it before
       the REMOVE reached it) - it goes the duplicate path: nothing made here, nothing removed anywhere, the owner confirms as a duplicate
       does; never a revived piece. A new placement at the same spot carries a new nonce and is made. No nonce: the key-only rule. */
    if (coopbuild::BuildLatePlaceRefused(m.ownerSlot, m.nonce, BdRemovedExact(m.key, m.nonce), BdRemovedAnyNonce(m.key)))
    {
        ++g_lateRevivedRefused; ++g_copyDup;
        std::map<std::string, BdReg>::iterator lr = g_reg.find(m.key);
        if (toMe != 0 && lr != g_reg.end() && lr->second.own != 0) BdSendAck(m.key);
        std::sprintf(line, "[BUILD] copy skipped key=%s: a hand-over PLACE after this key's REMOVE - late, taken as a duplicate, not revived (lateRevivedRefused=%lld)",
                     m.key.c_str(), g_lateRevivedRefused);
        BdLogCapped(line);
        return 1;
    }
    int senderSlot = -1;
    ::Faction* want = (toMe != 0 || p.restore != 0) ? 0 : BdSenderStandIn(p.fromSlot, &senderSlot);   /* the sender's own piece: the stand-in of the player who placed it */
    if (toMe != 0 || p.restore != 0) want = LocalPlayerFaction();   /* mmo8a: a restore is made as this game's OWN piece */
    else if (handed != 0)   /* house1b (item 7): another slot's piece needs THAT slot's stand-in - never the sender's under slot k's record */
    {
        ::Faction* third = StandInForSlot((int)m.ownerSlot);
        if (third == 0)
        {
            ++g_handedNoStandIn;
            std::sprintf(line, "[BUILD] copy REFUSED key=%s: handed to slot %d, which has no stand-in here - not made (handedNoStandIn=%lld)",
                         m.key.c_str(), (int)m.ownerSlot, g_handedNoStandIn);
            BdLogCapped(line);
            return 1;
        }
        want = third;
    }
    if (want == 0)   /* this game's faction, or the sender's stand-in, is not here yet: this entry waits, the others go on */
    {
        if (p.slotWaitCounted == 0) { p.slotWaitCounted = 1; if (handed != 0 || p.restore != 0) ++g_handedWaitSlot; else ++g_copyWaitNoPeer; }
        p.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    const int recSlot = (toMe != 0 || p.restore != 0) ? mySlot : (handed != 0 ? (int)m.ownerSlot : senderSlot);
    std::map<std::string, BdReg>::iterator have = g_reg.find(m.key);
    if (p.restore != 0 && have != g_reg.end())   /* mmo8a: a restore is judged by the LIVE piece, not by a registry row */
    {
        if (have->second.via == 3 && have->second.removed == 0)
        {
            ++g_rsRefused; p.rsOutcome = 3;
            std::sprintf(line, "[BUILD] restore key=%s refused (a copy of another game's piece holds this key here) owner=mine", m.key.c_str());
            BdLogRestore(line);
            return 1;
        }
        if (have->second.via == 3)
        {
            if (g_pendRemove.find(m.key) != g_pendRemove.end()) return 0;   /* that copy's removal still runs */
            g_reg.erase(have);
        }
        have = g_reg.end();   /* this game's own row (the process keeps own rows across a load) - the live search below decides */
    }
    /* build1-d: a PLACE after this key's REMOVE is a NEW piece at the same spot (one reliable channel keeps the order): once
       the old copy's removal has finished its removed row gives way; while the removal still runs, the PLACE waits */
    if (have != g_reg.end() && have->second.via == 3 && g_pendRemove.find(m.key) != g_pendRemove.end()) return 0;
    if (have != g_reg.end() && have->second.via == 3 && have->second.removed != 0) { g_reg.erase(have); have = g_reg.end(); }
    if (have != g_reg.end())
    {
        ++g_copyDup;
        if (toMe != 0 && have->second.own != 0) BdSendAck(m.key);   /* house1b: a re-sent hand-off this game already holds - confirm again */
        have->second.nonce = coopbuild::BuildAdoptNonce(have->second.nonce, m.ownerSlot, m.nonce);   /* house2 fold 2: a reloaded row learns its nonce */
        std::sprintf(line, "[BUILD] copy skipped key=%s: already in the registry here (via=%d) - duplicate", m.key.c_str(), have->second.via);
        BdLogCapped(line);
        return 1;
    }
    if ((int)g_reg.size() >= kRegCap)
    {
        ++g_regFull; ++g_pendingDropped;
        std::sprintf(line, "[BUILD] copy DROPPED key=%s: the registry is full (%d)", m.key.c_str(), kRegCap);
        BdLogCapped(line);
        return 1;
    }
    /* build1-c (review-build1b note 5): createBuilding's own position limits (answers R1) - such a piece is never made */
    if (std::fabs(m.pos[0]) > 150000.0f || std::fabs(m.pos[2]) > 150000.0f || m.pos[1] < -1000.0f)
    {
        ++g_copyOffMap;
        std::sprintf(line, "[BUILD] copy DROPPED key=%s: pos (%.1f, %.1f, %.1f) is outside createBuilding's limits", m.key.c_str(),
                     m.pos[0], m.pos[1], m.pos[2]);
        BdLogCapped(line);
        return 1;
    }
    /* the area must be loaded here (createBuilding returns null otherwise, answers R1). build1-c (M1): 2 = move it to the
       not-loaded list, re-checked every kRetryGap safe points */
    /* build1-e: furniture - pos / rot are in the HOST's local frame, so the area is the host's. The host is re-found by its key
       and must have an interior (+0x1F0); until then the PLACE is hostPending, retried every kRetryGap safe points. */
    const int furn = m.hostKey.empty() ? 0 : 1;
    void* host = 0;
    void* fo = 0;
    float hostPos[3];
    hostPos[0] = 0.0f; hostPos[1] = 0.0f; hostPos[2] = 0.0f;
    if (furn != 0)
    {
        const char* why = 0;
        if (g_hostLookups >= kHostLookupsPerDrain) { p.nextTry = g_drainNo + 1; return 0; }   /* review-build1e D2: next safe point */
        ++g_hostLookups;
        host = ObjectByPositionKey(m.hostKey.c_str());
        const int hv = ObjectByPositionKeyVerdict();
        char* interior = (host != 0) ? (char*)BdPtrAt(host, kBInterior) : 0;
        if (host == 0) why = BdVerdictWord(hv);
        else if (interior == 0 && m.hostForm != 2) why = "the host has no interior (+0x1F0) here";   /* review-build1e D3: form 2 needs none */
        else if (BdByteAtOr0(host, 0x162) != 0 || BdByteAtOr0(host, 0x1A1) != 0) why = "the host is dismantled or destroyed here";   /* review-build1e N3 */
        else if (!BdCopyF((const char*)host + kObjPos, hostPos, 3)) why = "the host's position could not be read";
        if (why != 0)
        {
            if (p.hostLogged == 0)
            {
                p.hostLogged = 1;
                ++g_hostPending;
                std::sprintf(line, "[BUILD] copy pending key=%s host=%s hostResolved=0: %s - retried every %u safe points", m.key.c_str(),
                             m.hostKey.c_str(), why, kRetryGap);
                BdLogCapped(line);
            }
            p.nextTry = g_drainNo + kRetryGap;
            return 0;
        }
        void* L0 = (void*)(interior + kInteriorLayout);
        if (m.hostForm == coopbuild::kBuildHostPlain) fo = L0;
        else if (m.hostForm == coopbuild::kBuildHostSub)   /* the builder's commit took the layout's vt+0x40: the same here */
        {
            int ff = 0;
            fo = BdLayoutSubPod(L0, &ff);
            if (fo == 0)
            {
                ++g_hostFailed;
                std::sprintf(line, "[BUILD] copy FAILED key=%s host=%s hostResolved=1: the host layout's vt+0x40 %s - given up", m.key.c_str(),
                             m.hostKey.c_str(), ff != 0 ? "FAULTED" : "returned null");
                BdLogCapped(line);
                return 1;
            }
        }
        /* kBuildHostIndoorsOnly: no furnitureOf, isIndoorsOf = host */
    }
    if (furn == 0 && BdAreaReady(p, m.pos[0], m.pos[1], m.pos[2]) == 0)   /* mmo8a3 (A): a restore row reads the engine's +0xB1 */
    {
        if (p.waitLogged == 0)
        {
            p.waitLogged = 1;
            ++g_copyWaitNotLoaded;
            std::sprintf(line, "[BUILD] copy pending key=%s: its area is not loaded here - re-checked every %u safe points", m.key.c_str(), kRetryGap);
            BdLogCapped(line);
        }
        return 2;
    }
    if (p.restore != 0)   /* mmo8a3 (A): the gate above read ZoneMap+0xB1 - the zone's saved pieces are all in, so the search runs now */
    {
        if (g_rsSearches >= kRsSearchPerDrain) { ++g_rsSearchCapped; p.nextTry = g_drainNo + 1; return 0; }   /* LOW: at most 4 restore searches per safe point */
        ++g_rsSearches;
    }
    /* re-find by key first: the area record may already have brought the piece in */
    int lv = 0;
    void* live = (furn != 0) ? BdFindInHost(host, m.key.c_str(), 0, &lv) : ObjectByPositionKey(m.key.c_str());   /* build1-e */
    if (furn == 0) lv = ObjectByPositionKeyVerdict();   /* mmo8a: the free-standing search's own verdict */
    if (live == 0 && p.restore != 0 && furn == 0 && lv != kObjKeyNone)   /* mmo8a: a restore makes a piece only after a COMPLETE empty search, as furniture */
    {
        if (p.hostLogged < 2)
        {
            p.hostLogged = 2;
            std::sprintf(line, "[BUILD] restore key=%s waiting (%s) owner=mine - searched again every %u safe points", m.key.c_str(), BdVerdictWord(lv), kRetryGap);
            BdLogRestore(line);
        }
        p.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    if (live == 0 && furn != 0 && lv != kObjKeyNone)   /* build1-e (F2's rule): a new copy only after a COMPLETE empty search */
    {
        if (p.hostLogged < 2)
        {
            p.hostLogged = 2;
            ++g_hostPending;
            std::sprintf(line, "[BUILD] copy pending key=%s host=%s hostResolved=1: the search inside the host was %s - retried every %u safe points",
                         m.key.c_str(), m.hostKey.c_str(), BdVerdictWord(lv), kRetryGap);
            BdLogCapped(line);
        }
        p.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    if (live != 0)
    {
        float lp = 0, ln = 0;
        int lc = 0, ld = 0, lx = 0;
        BdReadState(live, &lp, &ln, &lc, &ld, &lx);
        if (ld != 0) return 0;   /* dismantled: freed next frame (answers R3) - look again then */
        void* o = BdOwnerPod(live);
        if (p.restore != 0)   /* mmo8a: the save brought the piece in - nothing is made; another owner's piece at the key is refused */
        {
            const int mineLive = (o != 0 && o == (void*)LocalPlayerFaction()) ? 1 : 0;
            int regd = 0;
            if (mineLive != 0)
            {
                ++g_rsPresent; p.rsOutcome = 1;
                std::map<std::string, BdReg>::iterator pr = g_reg.find(m.key);   /* mmo8a2 (H1): the piece stands in THIS world */
                const int act = coopbuild::BuildRestorePresentAct(1, pr != g_reg.end() ? 1 : 0, pr != g_reg.end() ? pr->second.own : 0,
                                                                  (pr != g_reg.end() && pr->second.via == 3) ? 1 : 0);
                if (act == coopbuild::kBuildRsPresentNonce) { pr->second.gen = g_bdGen; BdRestoreNonce(m.key, pr->second, 1); }   /* help1 fold 4: its PLACE's nonce */
                else if (act == coopbuild::kBuildRsPresentRegister && (int)g_reg.size() >= kRegCap) ++g_regFull;
                else if (act == coopbuild::kBuildRsPresentRegister)   /* help1 fold 5 (T-268): the load scan ran before this area loaded - registered here, as the scan does */
                {
                    float rpos[3], rrot[4];
                    int livePos = (furn == 0 && BdCopyF((const char*)live + kObjPos, rpos, 3) && BdOrientPod(live, rrot)) ? 1 : 0;
                    float ground = 0.0f;
                    if (livePos == 1 && GroundTerrainHeightAt(rpos[0], rpos[2], &ground) != 1) livePos = 0;   /* no ground height here: the record's inputs */
                    for (int i = 0; i < 3 && livePos == 0; ++i) rpos[i] = m.pos[i];   /* furniture (host-local) or an unreadable pose: the PLACE record's own inputs */
                    for (int i = 0; i < 4 && livePos == 0; ++i) rrot[i] = m.rot[i];
                    BdReg& nr = BdRegisterOwnLive(live, m.key, rpos, rrot, lp, ln, lc, lx, livePos);   /* a live position becomes a height above the ground; a PLACE record's inputs are kept as they are */
                    if (furn != 0) { nr.hostKey = m.hostKey; nr.hostForm = m.hostForm; nr.floor = m.floor; nr.outside = (m.outside != 0) ? 1 : 0; }
                    g_rsOwePlace.push_back(m.key);   /* mmo8a2 (M4): BdRosterTick sends its PLACE + STATE as a roster piece (the STATE announces it - T-269) */
                    ++g_rsPresentRegistered; regd = 1;
                }
            }
            else { ++g_rsRefused; p.rsOutcome = 3; }
            std::sprintf(line, "[BUILD] restore key=%s %s owner=%s progress=%.2f/%.2f%s", m.key.c_str(),
                         mineLive != 0 ? "present (the loaded area already holds it)" : "refused (another owner's piece stands at this key)",
                         mineLive != 0 ? "mine" : BdWhose(o, 0), lp, ln,
                         regd != 0 ? " - no row here: registered as this game's own (via=scan), its PLACE + STATE owed to the other game" : "");
            BdLogRestore(line);
            return 1;
        }
        /* house1b (item 5): furniture the engine gave THIS game at a load (the placer kept it after a failed hand-off) has no row here;
           the PLACE's record - the sender's - wins: it is adopted below as the sender's copy, so exactly one game writes its box */
        const int ocls = BdOwnerClassAt(live, m.key, (void*)want, recSlot, "adopt");   /* P87 fold 3: the one owner rule (BdReconDrain and BdRemoveOne ask it too); P87 root: the copy list first */
        if (toMe == 0 && o != 0 && o == (void*)LocalPlayerFaction() && furn != 0 && ocls != coopbuild::kBuildLiveOwnSenderList) ++g_dupRecorded;
        if (toMe == 0 && o != 0 && o == (void*)LocalPlayerFaction() && furn == 0 && ocls != coopbuild::kBuildLiveOwnSenderList)   /* build1h: a piece handed to me is adopted as my own instead; P87 root: a listed copy the load gave this game is the sender's */
        {
            ++g_copyDup;
            std::sprintf(line, "[BUILD] copy skipped key=%s: the live piece there is THIS game's own - duplicate, left alone", m.key.c_str());
            BdLogCapped(line);
            return 1;
        }
        if (ocls == coopbuild::kBuildLiveOwnSenderRecord) ++g_ownerRecordAdopt;
        if (ocls == coopbuild::kBuildLiveOwnSenderList) ++g_clAdopt;   /* P87 root */
        int fixed = 0;
        /* P18 fold 1 (items 2 and 6): an adopt from the world is a PURCHASE only when this game's copy was for sale before the owner write
           (coopbuild::BuildOwnerAdoptIsPurchase); only then the bought house's world side runs (BdBuyFxBefore / BdBuyFxAfter) and the
           applied counter / line is written. Every other adopt from nobody / a third party is the ordinary adopt, counted adoptOther. */
        const int ocWorld = (furn == 0 && coopbuild::BuildOwnerAdoptFromWorld(ocls) != 0) ? 1 : 0;
        const int ocBuy = (ocWorld != 0) ? coopbuild::BuildOwnerAdoptIsPurchase(1, 0, BdIsForSalePod(live), (o != (void*)want) ? 1 : 0, (want != 0 && IsStandInFaction(want)) ? 1 : 0) : 0;   /* the new owner: a player's stand-in (never a third owner a record names) */
        static BdBuyFx fx;   /* MAIN THREAD; 2 KB of resident pointers - not on the stack */
        std::memset(&fx, 0, sizeof(fx));
        if (ocBuy != 0) BdBuyFxBefore(live, (void*)o, &fx);
        if (o != (void*)want) { fixed = BdSetFactionPod(live, (void*)want) ? 1 : -1; ++g_copyOwnerFixed; }   /* build1h: the owner the PLACE names */
        if (ocBuy != 0 && fixed == 1) BdBuyFxAfter(live, (void*)want, &fx);
        if (ocBuy != 0)   /* P18: a town / NPC house the sender's player bought - the owner change applied here */
        {
            const int oa = coopbuild::BuildOwnerApplyOutcome(fixed);
            if (oa == coopbuild::kBuildOaApplied) ++g_ocApplied; else if (oa == coopbuild::kBuildOaSkip) ++g_ocApplySkip; else ++g_ocApplyFaulted;
            if (fx.townOut == 1) ++g_ocFxTownOut;
            if (fx.townIn == 1) ++g_ocFxTownIn;
            g_ocFxDoorsOpened += fx.doorsOpened; g_ocFxInterior += fx.interiorN; g_ocFxNewHomes += fx.newHomes;
            if (fx.res == 1) g_ocFxResidents += fx.resN; else if (fx.res != 0) ++g_ocFxResSkipped;
            if (fx.res == -1 || fx.townOut < 0 || fx.townIn < 0 || fx.squad < 0 || fx.doors < 0 || fx.interior < 0 || fx.homes < 0) ++g_ocFxFaulted;
            g_ocFxStepsSkipped += 7;
            g_ocLastKey = m.key;
            char ol[1100];
            _snprintf_s(ol, sizeof(ol), _TRUNCATE, "[BUILD] P18 owner applied key=%.63s: '%.60s' -> '%.60s' (the sender's stand-in; this game's copy was for sale - a house bought) through the engine's own setFaction vt+0xA0 - %s;"
                        " world side: residents %s (%u platoons homed here, %u new homes), town list out=%d in=%d, residentSquad cleared=%d, doors %u re-owned / %u open (%d), interior %u re-owned (%d);"
                        " NOT repeated: 0x54EA40 furniture teardown, 0x38A5E0 location nodes, interior +0x100 walk, town step 0x296900/0x928010, interior+0x30 vt+0x60, 0x4D2340, residents' AI orders 0x5D1640",
                        m.key.c_str(), BdFactionName((void*)o).c_str(), BdFactionName(BdOwnerPod(live)).c_str(),
                        oa == coopbuild::kBuildOaApplied ? "applied" : (oa == coopbuild::kBuildOaSkip ? "SKIP (not written: already the named owner or an empty target)" : "FAULTED"),
                        fx.res == 1 ? "moved" : (fx.res == 0 ? "none / no address row" : (fx.res == -2 ? "SKIPPED (too many platoons to list safely)" : "FAULTED")), fx.resN, fx.newHomes,
                        fx.townOut, fx.townIn, fx.squad, fx.doorsN, fx.doorsOpened, fx.doors, fx.interiorN, fx.interior);
            BdLogOc(ol);
        }
        else if (ocWorld != 0) ++g_ocAdoptOther;   /* P18 fold 1 (item 6): not for sale here - an ordinary adopt from nobody / a third party */
        BdReg& r = g_reg[m.key];
        r.sid = m.sid; r.owner = BdFactionName(BdOwnerPod(live)); r.progress = lp; r.needed = ln;
        r.complete = lc; r.dismantled = 0; r.destroyed = lx; r.via = 3;
        r.helpTailKnown = 0; r.helpTailSeq = 0; r.helpTailLive = 0;   /* help1 fold 2 (HIGH 1): a (re)made copy - new help waits for the owner's STATE again */
        r.hasPos = 1; for (int i = 0; i < 3; ++i) r.pos[i] = (furn != 0) ? hostPos[i] : m.pos[i];   /* review-build1d D3; build1-e: the host's */
        r.hostKey = m.hostKey; r.hostForm = m.hostForm; r.liveKey.clear(); r.keySame = 1;   /* build1-e: found by the builder's key */
        if (furn != 0) ++g_hostResolved;
        ++g_copyAdopted;
        BdHelpBaseRead(r, live);   /* help1: the copy's baseline */
        if (p.tries > 0) ++g_copyAdoptAfterNull;   /* review-build1b C3 residue: the engine added it before returning null */
        BdMarkOwner(r, m, toMe, recSlot);   /* build1h */
        if (toMe == 0) BdQueuePlaceState(m);   /* build1h: an own piece takes no STATE from the placer */
        if (toMe == 0) BdCopyListSidLive(m.key, live);   /* P87 root: the list keeps the sid the engine's piece carries */
        std::sprintf(line, "[BUILD] copy adopted key=%s host=%s owner='%.60s' ownerWas=%s fixed=%d complete=%d progress=%.2f/%.2f own=%d handed=%d",
                     m.key.c_str(), furn != 0 ? m.hostKey.c_str() : "-", r.owner.c_str(), coopbuild::BuildOwnerWord(ocls), fixed, lc, lp, ln, toMe, handed);
        BdLogCapped(line);
        ItFurnMarkStale(2);   /* T-454: a live piece taken on as the copy */
        return 1;
    }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const unsigned long long rGet = Rva("GdcGetData"), rCont = Rva("GdContainer"), rZm = Rva("ZoneManagerPtr");
    void* factory = (coop::GameWorldPtr() != 0) ? BdPtrAt((const void*)coop::GameWorldPtr(), kWorldFactory) : 0;
    if (kBdCreateRva == 0 || rGet == 0 || rCont == 0 || rZm == 0 || factory == 0)
    {
        ++g_copyFailed;
        std::sprintf(line, "[BUILD] copy FAILED key=%s: CreateBuilding / GdcGetData / GdContainer / ZoneManagerPtr or the factory (GameWorld+0x4A0) is missing",
                     m.key.c_str());
        BdLogCapped(line);
        return 1;
    }
    void* gd = BdGetData(base + (uintptr_t)rGet, base + (uintptr_t)rCont, &m.sid);
    int type = -1;
    if (gd != 0) BdIntAt(gd, kGdType, &type);
    if (gd == 0 || type != 0)
    {
        ++g_copyFailed;
        std::sprintf(line, "[BUILD] copy FAILED key=%s: record '%.80s' %s", m.key.c_str(), m.sid.c_str(),
                     gd == 0 ? "is not in this game's data" : "is not a BUILDING record");
        BdLogCapped(line);
        return 1;
    }
    /* zone+0xA8 newGameFirstTimeLoaded (T321: SET in normal play). With a non-player owner the engine may hand the piece to
       the town's faction, or run the faction's building substitution 0x7F3BD0, which can return null. Read and reported,
       not refused: the owner is put back below and a null return is counted. */
    const Sector s = (furn != 0) ? SectorOf(hostPos[0], hostPos[2]) : SectorOf(m.pos[0], m.pos[2]);   /* build1-e: the host's cell */
    int fresh = -1;
    void* zm = BdPtrAt((const void*)(base + (uintptr_t)rZm), 0);
    if (zm != 0) BdByteAt(zm, 200 + ((size_t)s.x * 64 + (size_t)s.y) * 0x168 + 0xA8, &fresh);
    if (fresh > 0) ++g_copyZoneFresh;
    float pos[3], rot[4];
    for (int i = 0; i < 3; ++i) pos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) rot[i] = m.rot[i];
    const BdCreateFn fn = (orig_create != 0) ? orig_create : (BdCreateFn)(base + (uintptr_t)kBdCreateRva);
    int faulted = 0;
    *called = 1;
    /* the recipe: town 0, owner coop-peer, cb / furnitureOf / isDoorOf / save / isIndoorsOf 0, completed = the PLACE's flag.
       orig_create bypasses our own detour, and this is not inside a placement commit, so the copy is never taken for a
       new piece of ours. */
    void* b = (furn != 0)   /* build1-e: furnitureOf rebuilt from THIS game's host in the builder's form, isIndoorsOf = host */
        ? BdCallCreateEx(fn, factory, gd, pos, (void*)want, rot, fo, host, m.complete != 0, m.floor, m.outside != 0, &faulted)
        : BdCallCreate(fn, factory, gd, pos, (void*)want, rot, m.complete != 0, &faulted);
    if (b == 0)
    {
        ++g_copyFailed;
        ++p.tries;
        p.nextTry = g_drainNo + kRetryGap;   /* build1-c (review-build1b note 6): spaced, and re-found by key first */
        const int giveUp = (faulted != 0 || p.tries >= kCopyTries) ? 1 : 0;
        if (giveUp != 0 && furn != 0) ++g_hostFailed;   /* build1-e */
        std::sprintf(line, "[BUILD] copy FAILED key=%s: createBuilding %s (zone %d,%d newGameFirstTimeLoaded(+0xA8)=%d%s) try %d/%d%s",
                     m.key.c_str(), faulted ? "FAULTED" : "returned null", s.x, s.y, fresh,
                     fresh > 0 ? " - the faction building substitution 0x7F3BD0 may have returned null" : "",
                     p.tries, kCopyTries, giveUp ? " - given up" : " - retried in 30 safe points (re-found by key first)");
        BdLogCapped(line);
        return giveUp;
    }
    void* o = BdOwnerPod(b);
    const char* at = BdWhose(o, (void*)want);
    int fixed = 0;
    if (o != (void*)want) { fixed = BdSetFactionPod(b, (void*)want) ? 1 : -1; ++g_copyOwnerFixed; }   /* the engine's own fix-up, 57c1e0:1127 */
    const int mined = BdMiningPod(b);
    char k2[kKeyCap];
    k2[0] = 0;
    const int k2ok = ObjectPositionKey(b, k2, kKeyCap, "", 3, 0);   /* b came from createBuilding this call: the virtual read is safe */
    const int keySame = (k2ok != 0 && m.key == k2) ? 1 : 0;
    if (keySame == 0) ++g_copyKeyMismatch;
    float cp = 0, cn = 0;
    int cc = 0, cd = 0, cx = 0;
    BdReadState(b, &cp, &cn, &cc, &cd, &cx);
    /* build1-c (review-build1b C4): the record the engine really used - 0x7F3BD0 can substitute a town-faction variant */
    char liveSid[96];
    BdSidOf(b, liveSid, (int)sizeof liveSid);
    const int sidSame = (liveSid[0] != 0 && m.sid == liveSid) ? 1 : 0;
    if (sidSame == 0) ++g_sidSubstituted;
    BdReg& r = g_reg[m.key];   /* under the BUILDER's key: every later message names the piece by it */
    r.sid = (liveSid[0] != 0) ? std::string(liveSid) : m.sid; r.owner = BdFactionName(BdOwnerPod(b)); r.progress = cp; r.needed = cn;
    r.complete = cc; r.dismantled = cd; r.destroyed = cx; r.via = 3;
    r.helpTailKnown = 0; r.helpTailSeq = 0; r.helpTailLive = 0;   /* help1 fold 2 (HIGH 1): a (re)made copy - new help waits for the owner's STATE again */
    r.hasPos = 1; for (int i = 0; i < 3; ++i) r.pos[i] = (furn != 0) ? hostPos[i] : m.pos[i];   /* review-build1d D3; build1-e: the host's */
    /* build1-e (recheck-build1d F2): marked at creation - a copy whose own key differs is never finished early by a REMOVE */
    r.hostKey = m.hostKey; r.hostForm = m.hostForm; r.keySame = keySame;
    r.liveKey = (keySame == 0 && k2ok != 0) ? std::string(k2) : std::string();
    if (!r.liveKey.empty()) g_liveAlias[r.liveKey] = m.key;   /* build1h: the box road asks by the live key */
    if (furn != 0) ++g_hostResolved;
    if (p.restore != 0) BdMarkRestored(r, p, b, recSlot);   /* mmo8a: via 6 "restored", no HAND_ACK, the row's STATE */
    else
    {
        ++g_copyCreated;
        BdHelpBaseRead(r, b);   /* help1: the copy's baseline - its delivered amounts as made */
        BdMarkOwner(r, m, toMe, recSlot);   /* build1h */
        if (toMe == 0) BdQueuePlaceState(m);
    }
    char hf[160];
    hf[0] = 0;
    if (furn != 0) std::sprintf(hf, " host=%.63s hostResolved=1 form=%d floor=%d", m.hostKey.c_str(), (int)m.hostForm, m.floor);
    std::sprintf(line, "[BUILD] copy created key=%s%s owner='%.60s' ownerAtReturn=%s fixed=%d zone %d,%d fresh(+0xA8)=%d complete=%d"
                 " progress=%.2f/%.2f (sent %.2f/%.2f) vt2D8=%s own=%d handed=%d restored=%d%s%s",
                 m.key.c_str(), hf, r.owner.c_str(), at, fixed, s.x, s.y, fresh, cc, cp, cn, m.progress, m.needed, mined ? "ok" : "FAULTED",
                 (toMe != 0 || p.restore != 0) ? 1 : 0, handed, p.restore,
                 keySame != 0 ? "" : " KEY MISMATCH live=", keySame != 0 ? "" : (k2ok != 0 ? k2 : "?"));
    BdLogCapped(line);
    ItFurnMarkStale(2);   /* T-454: the copy createBuilding just made here */
    if (sidSame == 0)
    {
        std::sprintf(line, "[BUILD] copy key=%s SID SUBSTITUTED: sent '%.80s', the engine made '%.80s'", m.key.c_str(), m.sid.c_str(),
                     liveSid[0] != 0 ? liveSid : "?");
        BdLogCapped(line);
    }
    if (p.restore != 0 && keySame == 0 && k2ok != 0) BdRsRekey(m.key, std::string(k2));   /* mmo8a2 (LOW): the next load searches the created piece's own key */
    return 1;
}

/* build1-c: is a PLACE for this key still waiting (either list)? */
int BdPlacePending(const std::string& key)
{
    for (size_t i = 0; i < g_pend.size(); ++i) if (g_pend[i].m.key == key) return 1;
    for (size_t i = 0; i < g_wait.size(); ++i) if (g_wait[i].m.key == key) return 1;
    return 0;
}

/* help1: the mod's own K2 writes - vt+0x230 inside them is never a helper's work */
struct BdSelfScope
{
    BdSelfScope() { ++g_bdSelf; }
    ~BdSelfScope() { --g_bdSelf; }
};
/* help1, MAIN THREAD: this game's own character nearest to a position - a refund's receiver (the delivering character is not recorded) */
::Character* BdNearestOwnChar(const float* pos)
{
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return 0;
    ::Character* best = 0;
    float bd = 0.0f;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0;
        ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0 || !net::IsUidMine(u)) continue;
        if (BdCharFaction(c) != mine) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(c, &v)) continue;
        const float dx = v.x - pos[0], dy = v.y - pos[1], dz = v.z - pos[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (best == 0 || d < bd) { best = c; bd = d; }
    }
    return best;
}
/* help1 fold (review MED 4), THE OWNER'S GAME: a helper's confirmation row changed (work applied, or refunded at completion) - pp.build
   takes it on its next write (BuildOwnRows captures it) and a STATE carries it once that record has landed (BdHelpAckTick) */
void BdHelpAckChanged(const std::string& key, BdReg& r)
{
    r.helpAckDirty = 1;
    g_helpAckAwait.insert(key);
    BdOwnDirty(1);
}
/* help1 fold (MED 4): a row pp.build cannot keep (no PLACE inputs, not encodable) has no record to wait for - its rows go at once */
void BdHelpAckUnkept(BdReg& r)
{
    if (r.helpAckDirty == 0) return;
    r.helpAcksWriting = r.helpAcks; r.helpAckDirty = 0; r.helpAckWait = 3;
}
/* help1 fold (review MED 4), MAIN THREAD (BuildTick): a confirmation reaches the helper only once the owner's record holding it (the
   piece's progress and the last applied seq) has landed on disk - the store's committed seq for pp.build at or past that record's. No
   writer: no record to wait for. A record not landed after kHelpAckStuckMs (a failed write) is written again. help1 fold 3: the
   (key, nonce) gone records land the same way - a HELP_GONE is answered from one only after (the SETTLED rule). */
void BdHelpAckTick()
{
    if (g_helpAckAwait.empty() && g_goneAwait.empty()) return;
    const DWORD now = ::GetTickCount();
    const int writer = OwnWriterOn() ? 1 : 0;
    unsigned long long cs = 0;
    const int haveCs = (writer != 0 && OwnStoreCommittedSeq(coopown::BuildRecKey(), &cs)) ? 1 : 0;
    char line[360];
    for (std::set<BdPlKey>::iterator k = g_goneAwait.begin(); k != g_goneAwait.end(); )   /* help1 fold 3 */
    {
        std::map<BdPlKey, BdGoneRec>::iterator it = g_ownGone.find(*k);
        if (it == g_ownGone.end()) { g_goneAwait.erase(k++); continue; }
        BdGoneRec& gr = it->second;
        int land = 0;
        if (writer == 0) { gr.writing = gr.acks; gr.dirty = 0; land = 1; }
        else if (gr.wait == 3) land = 1;
        else if (gr.wait == 2 && haveCs != 0 && cs >= gr.seq) land = 1;
        else if (gr.wait == 2 && (DWORD)(now - gr.at) >= kHelpAckStuckMs)
        {
            ++g_helpAckStuck;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help gone record key=%.63s nonce=%08x NOT landed %lu ms after pp.build record seq %llu went to the writer (committed %llu) - written again; ackStuck=%lld",
                        k->first.c_str(), k->second, (unsigned long)(now - gr.at), gr.seq, cs, g_helpAckStuck);
            BdLogHelp(line);
            gr.wait = 0; gr.dirty = 1;
            BdOwnDirty(2);
        }
        if (land != 0) { gr.landed = gr.writing; gr.wait = 0; ++g_helpAckLanded; }
        if (gr.dirty == 0 && gr.wait == 0) g_goneAwait.erase(k++);
        else ++k;
    }
    for (std::set<std::string>::iterator k = g_helpAckAwait.begin(); k != g_helpAckAwait.end(); )
    {
        std::map<std::string, BdReg>::iterator it = g_reg.find(*k);
        if (it == g_reg.end() || it->second.own == 0 || it->second.via == 3) { g_helpAckAwait.erase(k++); continue; }
        BdReg& r = it->second;
        int land = 0;
        if (writer == 0) { r.helpAcksWriting = r.helpAcks; r.helpAckDirty = 0; land = 1; }
        else if (r.helpAckWait == 3) land = 1;
        else if (r.helpAckWait == 2 && haveCs != 0 && cs >= r.helpAckSeq) land = 1;
        else if (r.helpAckWait == 2 && (DWORD)(now - r.helpAckAt) >= kHelpAckStuckMs)
        {
            ++g_helpAckStuck;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help confirmation key=%.63s NOT landed %lu ms after pp.build record seq %llu went to the writer (committed %llu) - written again; ackStuck=%lld",
                        k->c_str(), (unsigned long)(now - r.helpAckAt), r.helpAckSeq, cs, g_helpAckStuck);
            BdLogHelp(line);
            r.helpAckWait = 0; r.helpAckDirty = 1;
            BdOwnDirty(1);
        }
        if (land != 0)
        {
            r.helpAcksLanded = r.helpAcksWriting;
            r.helpAckWait = 0;
            ++g_helpAckLanded;
            r.helpForce = 1;
            BdMaybeSendState(*k, r);
        }
        if (r.helpAckDirty == 0 && r.helpAckWait == 0) g_helpAckAwait.erase(k++);
        else ++k;
    }
}
/* help1 fold (review MED 3), THE OWNER'S GAME: work for a piece this game no longer holds as its own is answered HELP_GONE - the helper
   drops every entry at or below seq and hands its own delivered materials back. Not sent (link down): the helper sends the work again. */
void BdSendHelpGone(const std::string& key, unsigned int nonce, unsigned int seq, const coopbuild::BuildHelpAck* a, int slot, const char* why)
{
    coopbuild::BuildMsg g;
    const unsigned int applied = (a != 0) ? a->seq : 0;
    g.kind = coopbuild::kBuildHelpGone; g.key = key; g.seq = seq; g.applied = applied;   /* help1 fold 2 (HIGH 2): what it applied is never handed back */
    g.nonce = nonce;   /* help1 fold 3: the placement the work was done on */
    g.nMats = (unsigned char)((a != 0) ? a->nEx : 0);   /* help1 fold 3 (finding 4): that slot's cumulative refused amounts for it - the helper hands back what it has not yet */
    float rf = 0.0f;
    for (unsigned int i = 0; i < g.nMats && i < coopbuild::kBuildMaxMats; ++i) { g.mats[i] = a->ex[i]; rf += a->ex[i]; }
    const bool sent = coopbuild::BuildEncodable(g) && net::SendBuild(g);
    if (sent) ++g_helpGoneSent;
    char line[480];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u answered GONE (%s; last applied %u) - the helper drops it (at or below %u: applied, not handed back) and gets the rest's materials back%s goneSent=%lld nonce=%08x refused=%.2f",
                key.c_str(), slot, seq, why, applied, applied, sent ? "" : " NOT SENT (link down - the helper sends it again)", g_helpGoneSent, nonce, rf);
    BdLogHelp(line);
}
/* help1 fold (review MED 3), K2 safe point, THE HELPER'S GAME: the owner no longer holds the piece. Every entry at or below seq goes and
   its delivered materials come back as items to this game's nearest own character - only when this game's records were read, every
   item can be named and a character exists (else the entries stay: sent again, answered again); a material the inventory refuses (or a
   faulted add) is not retried - never given twice. With no entry left the row ends (its unsent work refunded with it) and leaves
   pp.help. Saved at once. */
void BdHelpOnGone(const BdHelpGoneIn& g)
{
    char line[640];
    BdHelpLoad();
    std::map<std::string, BdHelpRow>::iterator it = g_help.find(g.key);
    std::map<BdPlKey, BdHelpRow>::iterator ot = g_helpOld.find(BdPlKey(g.key, g.nonce));
    BdHelpRow* hp = 0;   /* help1 fold 3: the row of the placement the GONE names - the key's row when it is that placement, else a replaced one's */
    int isOld = 0;
    if (it != g_help.end() && it->second.nonce == g.nonce) hp = &it->second;
    else if (ot != g_helpOld.end()) { hp = &ot->second; isOld = 1; }
    if (hp == 0)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help GONE key=%.63s seq<=%u from=slot%d - no row here (answered before) nonce=%08x", g.key.c_str(), g.seq, g.slot, g.nonce);
        BdLogHelp(line);
        return;
    }
    BdHelpRow& h = *hp;
    if (h.ownerSlot >= 0 && g.slot != h.ownerSlot)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help GONE key=%.63s seq<=%u from=slot%d IGNORED - the piece's owner is slot%d", g.key.c_str(), g.seq, g.slot, h.ownerSlot);
        BdLogHelp(line);
        return;
    }
    float back[coopbuild::kBuildMaxMats];
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) back[i] = 0.0f;
    int n = 0;
    BdHelpRecMerge(g.key, h);   /* help1 fold 2 (LOW 5): entries read back from the owed file that an earlier GONE settled go first */
    const unsigned int settle = coopbuild::BuildHelpGoneSettled(g.seq, g.applied);   /* help1 fold 2 (HIGH 2): at or below the owner's last applied = applied */
    size_t settledN = 0;
    for (size_t q = 0; q < h.owed.size(); ++q) if (h.owed[q].seq <= settle) ++settledN;
    const size_t k = coopbuild::BuildHelpGoneSum(h.owed, g.seq, g.applied, back, &n);
    const int all = (settledN == h.owed.size()) ? 1 : 0;
    const int withPend = (all != 0 && h.pendAny != 0) ? 1 : 0;
    if (withPend != 0)
    {
        for (int i = 0; i < h.pendN && i < (int)coopbuild::kBuildMaxMats; ++i) back[i] += h.pendM[i];
        if (h.pendN > n) n = h.pendN;
    }
    int owe[coopbuild::kBuildMaxMats];   /* help1 fold 3 (finding 4): the refused tail - what the owner refused for this placement, less what was already handed back */
    const int refusedN = coopbuild::BuildHelpGoneRefused(g.ex, g.nEx, h.refunded, owe);
    int whole[coopbuild::kBuildMaxMats];
    int anyWhole = 0, unnamed = 0;
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i)
    {
        whole[i] = ((i < n && back[i] > 0.0f) ? (int)(back[i] + 0.001f) : 0) + owe[i];
        if (whole[i] > 0) { anyWhole = 1; if (h.sid[i].empty()) unnamed = 1; }
    }
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    ::Character* c = (anyWhole != 0) ? BdNearestOwnChar(h.posOk != 0 ? h.pos : zero) : 0;
    const char* wait = (BdHelpRefundsReady() == 0) ? "this game's own records (pp.help) are not read yet"
                     : (unnamed != 0 ? "a material's item is not known here" : ((anyWhole != 0 && c == 0) ? "no own character" : 0));
    if (wait != 0)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help GONE key=%.63s seq<=%u (%u entries) waits: %s - the entries stay (sent again, answered again)",
                    g.key.c_str(), g.seq, (unsigned int)k, wait);
        BdLogHelpFreq(line);
        return;
    }
    int items = 0, lost = 0, carried = 0;
    int givenAt[coopbuild::kBuildMaxMats];   /* help1 fold 4 (leftover 6): 1 given, 0 the inventory refused (carried, retried), < 0 faulted */
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i)
    {
        givenAt[i] = 1;
        if (whole[i] <= 0) continue;
        int units = 0;   /* T-447 leftover 1: a refusal may follow a partial placement (units absorbed into the character's stacks) */
        const int given = ItemsGiveMaterial(c, h.sid[i], whole[i], &units);
        givenAt[i] = given;
        if (given == 0 && units > 0 && units < whole[i]) { items += units; whole[i] -= units; }   /* only the rest is carried */
        if (given == 1) items += whole[i]; else if (given < 0) ++lost; else carried += whole[i];
    }
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) h.refunded[i] += owe[i];   /* help1 fold 3: counted handed back whether or not the inventory took them (as the entries: never given twice) */
    g_helpGoneItems += items;
    g_helpGoneRefused += refusedN;
    const size_t gone = coopbuild::BuildHelpConfirm(&h.owed, settle);
    {   /* help1 fold 4 (leftover 6): what the inventory refused rides one more entry of this placement - sent again, answered GONE again */
        coopbuild::BuildHelpEntry ce;
        if (coopbuild::BuildHelpGoneCarry(whole, givenAt, h.nextSeq, h.nonce, &ce) != 0)
        {
            ++h.nextSeq;
            h.owed.push_back(ce);
            ++g_helpGoneCarried;
            char cl[360];
            _snprintf_s(cl, sizeof(cl), _TRUNCATE, "[BUILD] help GONE key=%.63s refund x%d NOT GIVEN (the inventory refused it) - carried as entry seq=%u of placement %08x: sent again, handed back at its GONE goneCarried=%lld",
                        g.key.c_str(), carried, ce.seq, h.nonce, g_helpGoneCarried);
            BdLogHelp(cl);
        }
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help GONE key=%.63s seq<=%u applied<=%u (%u entries%s; %u applied before it went - dropped, not handed back) - the owner no longer holds the piece; x%d items handed back%s unacked=%u goneItems=%lld refusedTail=x%d nonce=%08x%s",
                g.key.c_str(), settle, g.applied, (unsigned int)gone, withPend != 0 ? " + unsent work" : "", (unsigned int)(gone - k), items,
                lost != 0 ? " (some NOT GIVEN - the engine's add faulted; never retried)" : "", (unsigned int)h.owed.size(), g_helpGoneItems,
                refusedN, g.nonce, isOld != 0 ? " (a replaced placement)" : "");
    BdLogHelp(line);
    {   /* help1 fold 2 (re-check LOW 5): pp.help keeps the settled floor - an owed file read back later (a failed save, a restart) never hands these back again; fold 3: this placement's row */
        coopown::HelpRecRow mine;
        mine.key = g.key; mine.slot = h.ownerSlot; mine.nextSeq = h.nextSeq; mine.goneSeq = settle; mine.nonce = h.nonce;
        for (int i = 0; i < (int)coopown::kHelpRecMats && i < (int)coopbuild::kBuildMaxMats; ++i) mine.given[i] = h.refunded[i];
        std::map<BdPlKey, coopown::HelpRecRow>::iterator o = g_helpRec.find(BdPlKey(g.key, h.nonce));
        if (o == g_helpRec.end() || coopown::HelpRecJoin(&o->second, mine) == 0) g_helpRec[BdPlKey(g.key, h.nonce)] = mine;
    }
    if (h.owed.empty())   /* the row ends; its pp.help row stays (the floor) */
    {
        if (isOld != 0) g_helpOld.erase(ot);
        else g_help.erase(it);
    }
    if (g_helpRecDirty < 2) g_helpRecDirty = 2;
    BdHelpSave();
}
/* help1 fold 3 (findings 2, 3, 5), K2 safe point, THE OWNER'S GAME: work for a placement this game does not hold as its current piece at
   the key is answered HELP_GONE from that placement's (key, nonce) record - the sender slot's applied seq and refused tail - and only once
   the record has landed (0 = kept and retried: the SETTLED rule). The same placement gone here (removed, dismantled, handed) first makes
   or refreshes its record from the row. No record at all: GONE applied 0 (the helper gets its materials back; goneNoRecord). Never
   applied to another placement's piece. 1 = answered. */
int BdHelpGoneFor(BdHelpIn& in, BdReg* r, int oldNonce, const char* why)
{
    const coopbuild::BuildMsg& m = in.m;
    char line[480];
    /* help1 fold 4 (re-check #2, #3): the verdict (BuildHelpGoneVerdict) - never GONE applied 0 without proof; at the cap (no record kept)
       only from this placement's row once what it applied is on disk (landed == live) */
    const std::vector<coopbuild::BuildHelpAck>* ga = (r != 0) ? BdHelpAcksRead(m.key, *r) : 0;
    const int rowAcks = (ga != 0 && !ga->empty()) ? 1 : 0;
    if (rowAcks != 0) BdGoneNote(m.key, m.nonce, *ga, 1);
    std::map<BdPlKey, BdGoneRec>::const_iterator gr = g_ownGone.find(BdPlKey(m.key, m.nonce));
    const std::vector<coopbuild::BuildHelpAck>* la = 0;
    int rowSettled = 0, rowWriting = 0;
    if (rowAcks != 0)
    {
        la = BdHelpAcksLandedRead(m.key, *r);
        rowSettled = (la != 0 && coopbuild::BuildHelpAcksSame(*la, *ga) && r->helpAckDirty == 0 && r->helpAckWait == 0) ? 1 : 0;
        rowWriting = (r->helpAckWait != 0 || (r->helpAckDirty != 0 && r->removed == 0)) ? 1 : 0;
    }
    const int rec = (gr == g_ownGone.end()) ? 0 : ((gr->second.dirty != 0 || gr->second.wait != 0) ? 2 : 1);
    const int v = coopbuild::BuildHelpGoneVerdict(rec, r != 0 ? 1 : 0, rowAcks, rowSettled, rowWriting);
    if (v == coopbuild::kBuildGoneWait)
    {
        if (in.waitLogged == 0)
        {
            in.waitLogged = 1;
            ++g_goneWaitLand;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u nonce=%08x waits: %s is not on disk yet - answered GONE once it has landed goneWaitLand=%lld",
                        m.key.c_str(), in.slot, m.seq, m.nonce, rec == 2 ? "that placement's gone record" : "that placement's row (no gone record kept: the cap)", g_goneWaitLand);
            BdLogHelp(line);
        }
        in.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    if (v == coopbuild::kBuildGoneDrop)
    {
        ++g_helpGoneUnproven;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u nonce=%08x NOT ANSWERED: %s - never GONE applied 0 without proof; dropped unconfirmed (the helper keeps it owed and sends it again) goneUnproven=%lld",
                    m.key.c_str(), in.slot, m.seq, m.nonce,
                    r == 0 ? "no record of that placement here" : "no gone record kept (the cap) and its row's applied work is not all on disk", g_helpGoneUnproven);
        BdLogHelpFreq(line);
        return 1;
    }
    const coopbuild::BuildHelpAck* a = 0;
    if (v == coopbuild::kBuildGoneAnswer)
    {
        const std::vector<coopbuild::BuildHelpAck>& from = (rec == 1) ? gr->second.landed : *la;
        for (size_t j = 0; j < from.size(); ++j) if ((int)from[j].slot == in.slot) a = &from[j];
    }
    else
    {   /* ZERO: this placement's own row is here and holds no helper row - proven nothing was applied */
        ++g_helpGoneNoRecord;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u nonce=%08x: that placement's own row is here and holds no helper row (nothing applied from any helper) - answered GONE applied 0 goneNoRecord=%lld",
                    m.key.c_str(), in.slot, m.seq, m.nonce, g_helpGoneNoRecord);
        BdLogHelp(line);
    }
    if (oldNonce != 0) ++g_helpGoneOldNonce;
    BdSendHelpGone(m.key, m.nonce, m.seq, a, in.slot, why);
    return 1;
}
/* help1, K2 safe point, THE OWNER'S GAME: one HELP_WORK onto this game's own piece. Re-found by key. help1 fold (review HIGH 1): only
   the NEXT seq from that slot is applied (last applied + 1); a higher one is HELD here until the gap fills (the helper re-sends every
   unconfirmed entry) - never applied out of order; one at or below it is a duplicate (the landed confirmation goes again). help1 fold
   (MED 3): work for a piece this game no longer holds as its own (removed, dismantled, handed, not own, or its removed row already swept)
   is answered HELP_GONE; a key not seen here at all is dropped unconfirmed (sent again). The ruin reset first (vt+0x230(0)); material
   deltas capped at each material's total, the rest REFUNDED (the STATE tail carries it back); progress reaching needed completes through
   the engine's vt+0x230(FLT_MAX), else the STATE's own guarded write. help1 fold (MED 4): the confirmation goes once pp.build holding it
   has landed (BdHelpAckTick). 1 = done with it, 0 = keep (its area is not loaded here, or fold 3: a gone record not yet landed), 2 = held
   (an earlier seq first). help1 fold 3 (findings 2, 3): work carrying another placement's nonce is never applied here - it is answered
   GONE from that placement's record (BdHelpGoneFor). */
int BdApplyHelp(BdHelpIn& in)
{
    const coopbuild::BuildMsg& m = in.m;
    char line[500];
    if (in.slot < 0 || in.slot > (int)coopbuild::kBuildOwnerSlotMax)
    {
        ++g_helpDropRemoved;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dropped (the sender's slot is unknown) dropRemoved=%lld", m.key.c_str(), in.slot, m.seq, g_helpDropRemoved);
        BdLogHelp(line);
        return 1;
    }
    std::map<std::string, BdReg>::iterator row = g_reg.find(m.key);
    if (row != g_reg.end()) BdRestoreNonce(m.key, row->second, 1);   /* help1 fold 4 (re-check #1) */
    if (row != g_reg.end() && coopbuild::BuildHelpNonceWait(1, row->second.own, row->second.via == 3 ? 1 : 0, row->second.nonce, m.nonce) != 0)
    {   /* help1 fold 4: the own row's placement nonce is not known yet (pp.build not read) - HELD, re-judged at the next safe point */
        if (in.heldLogged == 0)
        {
            in.heldLogged = 1;
            ++g_helpNonceHeld;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u nonce=%08x held: the own row's placement nonce is not known yet (its pp.build record not read) - never answered GONE before it is; re-judged next safe point nonceHeld=%lld",
                        m.key.c_str(), in.slot, m.seq, m.nonce, g_helpNonceHeld);
            BdLogHelpFreq(line);
        }
        return 2;
    }
    const int have = (row != g_reg.end()) ? 1 : 0;
    const int samePl = (have != 0 && row->second.nonce == m.nonce) ? 1 : 0;   /* help1 fold 3 (findings 2, 3): the work was done on THIS row's placement */
    int listed = (have != 0 || g_ownSwept.find(m.key) != g_ownSwept.end()) ? 1 : 0;   /* fold 3: the key is known here - another placement's row, a swept key or a gone record */
    if (listed == 0)
    {
        std::map<BdPlKey, BdGoneRec>::const_iterator lb = g_ownGone.lower_bound(BdPlKey(m.key, 0u));
        listed = (lb != g_ownGone.end() && lb->first.first == m.key) ? 1 : 0;
    }
    const int rv = coopbuild::BuildHelpRowVerdict(samePl, samePl != 0 ? row->second.own : 0, (samePl != 0 && row->second.via == 3) ? 1 : 0,
                                                  samePl != 0 ? row->second.removed : 0, samePl != 0 ? row->second.dismantled : 0, listed);
    if (rv == coopbuild::kBuildHelpRowUnknown)
    {
        ++g_helpDropRemoved;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dropped (no piece here by that key - not confirmed; the helper sends it again) dropRemoved=%lld",
                    m.key.c_str(), in.slot, m.seq, g_helpDropRemoved);
        BdLogHelp(line);
        return 1;
    }
    if (rv == coopbuild::kBuildHelpRowGone)   /* help1 fold 3: answered from the (key, nonce) record once it has landed - never applied to another placement */
        return BdHelpGoneFor(in, samePl != 0 ? &row->second : 0, (have != 0 && samePl == 0) ? 1 : 0,
                             samePl == 0 ? (have != 0 ? "another placement holds the key now" : "removed here (its row already swept)")
                             : (row->second.removed != 0 ? "removed" : (row->second.dismantled != 0 ? "dismantled" : "the key is not this game's own piece")));
    BdReg& r = row->second;
    if (r.helpAcks.empty())   /* the rows pp.build brought back become the row's own (and are what it holds on disk) */
    {
        const std::vector<coopbuild::BuildHelpAck>* rs = BdHelpAcksRead(m.key, r);
        if (rs != 0) { r.helpAcks = *rs; if (r.helpAcksLanded.empty()) r.helpAcksLanded = *rs; }
    }
    const unsigned short slot = (unsigned short)in.slot;
    const coopbuild::BuildHelpAck* a0 = coopbuild::BuildHelpAckFor(&r.helpAcks, slot, 0);
    const unsigned int last = (a0 != 0) ? a0->seq : 0;
    const int verdict = coopbuild::BuildHelpSeqVerdict(last, m.seq);
    if (verdict == coopbuild::kBuildHelpDup)
    {
        ++g_helpDup;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dup (last applied %u) - its confirmation goes again (once on disk)", m.key.c_str(), in.slot, m.seq, last);
        BdLogHelp(line);
        r.helpForce = 1;
        BdMaybeSendState(m.key, r);
        return 1;
    }
    if (verdict == coopbuild::kBuildHelpHold)
    {
        if (in.heldLogged == 0)
        {
            in.heldLogged = 1;
            ++g_helpHeld;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u held (last applied %u: seq %u first - never applied out of order) held=%lld",
                        m.key.c_str(), in.slot, m.seq, last, last + 1, g_helpHeld);
            BdLogHelp(line);
        }
        return 2;
    }
    if (a0 == 0 && r.helpAcks.size() >= coopbuild::kBuildMaxHelpers)
    {
        ++g_helpDropRemoved;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dropped (the STATE's helper tail is full: %u slots)", m.key.c_str(), in.slot, m.seq, (unsigned int)r.helpAcks.size());
        BdLogHelp(line);
        return 1;
    }
    int fv = 0;
    void* b = BdFindPiece(m.key, r, &fv);
    if (b == 0)
    {
        if (in.waitLogged == 0)
        {
            in.waitLogged = 1;
            ++g_helpWaitArea;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u waits: the piece's area is not loaded here", m.key.c_str(), in.slot, m.seq);
            BdLogHelp(line);
        }
        in.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    float p0 = 0.0f, n0 = 0.0f;
    int c0 = 0, d0 = 0, x0 = 0;
    if (BdReadState(b, &p0, &n0, &c0, &d0, &x0) == 0)
    {
        ++g_helpFaulted;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dropped (the piece's state is unreadable; the helper sends it again)", m.key.c_str(), in.slot, m.seq);
        BdLogHelp(line);
        return 1;
    }
    float ex[coopbuild::kBuildMaxMats];
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) ex[i] = 0.0f;
    if (m.reset != 0 && x0 != 0 && c0 != 0)   /* P15: the helper repaired a COMPLETE ruin - the engine's own ruin reset here first, so the work is not refused as complete */
    {
        BdProgressPod(b, 0.0f);
        BdReadState(b, &p0, &n0, &c0, &d0, &x0);
        ++g_repairHelpReset;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u: the helper repaired a ruin - the engine's ruin reset here too (+0x1A1 now %d, %.2f/%.2f complete=%d); repairHelpReset=%lld",
                    m.key.c_str(), in.slot, m.seq, x0, p0, n0, c0, g_repairHelpReset);
        BdLogHelp(line);
    }
    if (c0 != 0 || d0 != 0)   /* complete before the work arrived: every delivered amount goes back to the helper */
    {
        float back = 0.0f;
        coopbuild::BuildHelpAck* a = coopbuild::BuildHelpAckFor(&r.helpAcks, slot, 1);
        for (int i = 0; i < (int)m.nMats; ++i) { a->ex[i] += m.mats[i]; back += m.mats[i]; }
        if (m.nMats > a->nEx) a->nEx = m.nMats;
        a->seq = m.seq;
        ++g_helpDropComplete;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u dropped (complete) - mats %.2f refunded to the helper (confirmed once pp.build holds it)", m.key.c_str(), in.slot, m.seq, back);
        BdLogHelp(line);
        BdHelpAckChanged(m.key, r);
        return 1;
    }
    const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
    if (rUpd == 0)
    {
        ++g_helpFaulted;
        BdLogHelp("[BUILD] <- HELP dropped: UpdatePhysicalWithProgress (0x558680) is not in the address table");
        return 1;
    }
    const uintptr_t upd = (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd;
    if (m.reset != 0 && x0 != 0)   /* the helper rebuilt a ruin: the engine's own reset here too (part-built, mats scaled) */
    {
        BdProgressPod(b, 0.0f);
        BdReadState(b, &p0, &n0, &c0, &d0, &x0);
    }
    float cur[coopbuild::kBuildMaxMats], tot[coopbuild::kBuildMaxMats], nw[coopbuild::kBuildMaxMats];
    int ct = -1;
    const int cn = BdReadMats(b, cur, (int)coopbuild::kBuildMaxMats, &ct);
    const int tn = BdReadMatTotals(b, tot, (int)coopbuild::kBuildMaxMats);
    const int n = (cn < 0) ? 0 : cn;
    int clamped = 0;
    float added = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float d = (i < (int)m.nMats) ? m.mats[i] : 0.0f;
        nw[i] = coopbuild::BuildHelpMatApply(cur[i], d, (i < tn) ? tot[i] : -1.0f, &ex[i]);
        added += nw[i] - cur[i];
        if (ex[i] > 0.0f) ++clamped;
    }
    for (int i = n; i < (int)m.nMats; ++i) if (m.mats[i] > 0.0f) { ex[i] = m.mats[i]; ++clamped; }   /* a material this piece does not list (or unreadable) */
    const int wall = BdIsWall(b);
    const int completes = (wall == 0) ? coopbuild::BuildHelpCompletes(p0, m.progress, n0) : 0;
    int ok = 0, stage = 0, ml = -1;
    if (wall != 0 || !(m.progress > 0.0f)) ok = BdApplyStatePod(b, 0, 0.0f, n, nw, &ml, upd, &stage);
    else if (completes != 0)
    {
        ok = BdApplyStatePod(b, 0, 0.0f, n, nw, &ml, upd, &stage);
        if (ok) ok = BdProgressPod(b, FLT_MAX);   /* the engine's own complete-now path */
    }
    else ok = BdApplyStatePod(b, 1, p0 + m.progress, n, nw, &ml, upd, &stage);   /* below needed (BuildHelpCompletes) */
    if (!ok)
    {
        ++g_helpFaulted;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u FAULTED at stage %d%s - not confirmed (the helper sends it again)",
                    m.key.c_str(), in.slot, m.seq, stage, completes != 0 ? " (vt+0x230 FLT_MAX)" : "");
        BdLogHelp(line);
        return 1;
    }
    coopbuild::BuildHelpAck* a = coopbuild::BuildHelpAckFor(&r.helpAcks, slot, 1);
    const int w = (n > (int)m.nMats) ? n : (int)m.nMats;
    for (int i = 0; i < w && i < (int)coopbuild::kBuildMaxMats; ++i) a->ex[i] += ex[i];
    if (w > (int)a->nEx) a->nEx = (unsigned char)(w > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : w);
    a->seq = m.seq;
    g_helpMatsClamped += clamped;
    float p1 = 0.0f, n1 = 0.0f;
    int c1 = 0, d1 = 0, x1 = 0, t1 = -1;
    if (BdReadState(b, &p1, &n1, &c1, &d1, &x1) != 0) { r.progress = p1; r.needed = n1; r.complete = c1; r.destroyed = x1; }
    r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &t1);
    r.gen = g_bdGen;
    ++g_helpApplied;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u applied %.2f->%.2f/%.2f mats+%.2f clamped=%d complete=%d%s (confirmed once pp.build holds it)",
                m.key.c_str(), in.slot, m.seq, p0, p1, n1, added, clamped, c1, wall != 0 ? " wall(mats only)" : "");
    BdLogHelp(line);
    if (completes != 0 && c1 != 0) ItFurnMarkStale(3);   /* T-454: the help completed the piece (0 -> 1) */
    BdHelpAckChanged(m.key, r);   /* help1 fold (MED 4): pp.build now (progress + last applied), the confirmation once it has landed */
    return 1;
}
/* help1, the K2 safe point: the owners' HELP_GONE answers first; then the received work in arrival order - an entry waiting for its area
   holds the later ones of its slot + key; help1 fold (HIGH 1): an entry held for its gap (BdApplyHelp 2) stays and is not a turn of the
   budget - the gap's entry, arriving later, is applied and the held one follows at the next safe point */
void BdHelpDrain()
{
    for (size_t g = 0; g < g_helpGoneIn.size(); ++g) BdHelpOnGone(g_helpGoneIn[g]);
    g_helpGoneIn.clear();
    if (g_helpIn.empty()) return;
    std::set<std::string> held;
    int done = 0;
    for (size_t i = 0; i < g_helpIn.size() && done < kStateApplyPerDrain; )
    {
        char sk[32];
        _snprintf_s(sk, sizeof(sk), _TRUNCATE, "%d|%08x|", g_helpIn[i].slot, g_helpIn[i].m.nonce);   /* help1 fold 3: per placement too */
        const std::string hk = std::string(sk) + g_helpIn[i].m.key;
        if (held.find(hk) != held.end() || g_helpIn[i].nextTry > g_drainNo) { held.insert(hk); ++i; continue; }
        for (size_t j = 0; j < i; )   /* help1 fold 2 (HIGH 1): held earlier under a seq at or above this one - superseded (renumbered / re-sent from lower) */
        {
            if (g_helpIn[j].slot == g_helpIn[i].slot && g_helpIn[j].m.key == g_helpIn[i].m.key && g_helpIn[j].m.nonce == g_helpIn[i].m.nonce   /* help1 fold 3: the same placement's */
                && coopbuild::BuildHelpSuperseded(g_helpIn[j].m.seq, g_helpIn[i].m.seq))
            {
                ++g_helpSuperseded;
                char sl[360];
                _snprintf_s(sl, sizeof(sl), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u (held) superseded by seq=%u arriving later - dropped (the helper renumbered or re-sent from lower; it sends again what it still owes) superseded=%lld",
                            g_helpIn[j].m.key.c_str(), g_helpIn[j].slot, g_helpIn[j].m.seq, g_helpIn[i].m.seq, g_helpSuperseded);
                BdLogHelp(sl);
                g_helpIn.erase(g_helpIn.begin() + (std::ptrdiff_t)j);
                --i;
            }
            else ++j;
        }
        const int res = BdApplyHelp(g_helpIn[i]);
        if (res == 2) { ++i; continue; }
        ++done;
        if (res != 0) g_helpIn.erase(g_helpIn.begin() + (std::ptrdiff_t)i);
        else { held.insert(hk); ++i; }
    }
}
/* help1, K2 safe point, THE HELPER'S GAME: the owner's STATE was just written onto the copy. Its tail confirms this game's work (those
   entries go), the materials the owner refused come back as items to this game's nearest own character, and the work still unconfirmed
   goes back on the copy (no flicker back); then the copy's baseline is re-read, so the mod's own write is never counted as help.
   help1 fold (MED 5/6): a real STATE (not one made from a PLACE) tells this session the owner's last applied seq from this game (0 =
   none) - new entries wait for it; refunds wait for this game's own records (pp.help) and are saved at once, never handed back twice. */
void BdHelpOnState(const std::string& key, BdReg& r, void* b, const coopbuild::BuildMsg& m, uintptr_t upd, int wall, int fromPlace)
{
    char line[420];
    const int me = MySlotForWire();
    BdHelpLoad();
    std::map<std::string, BdHelpRow>::iterator h = g_help.find(key);
    if (h != g_help.end()) { BdHelpNonceFit(key, h->second, r.nonce); BdHelpRecMerge(key, h->second); }   /* help1 fold 2: a re-placed piece's row starts again */
    if (fromPlace == 0 && me >= 0)
    {
        unsigned int t = 0, tl = 0;
        for (size_t j = 0; j < m.helpAcks.size(); ++j) if ((int)m.helpAcks[j].slot == me) { t = m.helpAcks[j].seq; tl = (m.helpAcks[j].live > t) ? m.helpAcks[j].live : t; }
        r.helpTailKnown = 1; r.helpTailSeq = t; r.helpTailLive = tl;   /* help1 fold 3 (finding 1): landed confirms; live = what the owner applied (kept, new work past it) */
    }
    const int ready = BdHelpRefundsReady();
    int changed = 0, gave = 0;
    for (size_t j = 0; j < m.helpAcks.size() && me >= 0; ++j)
    {
        const coopbuild::BuildHelpAck& a = m.helpAcks[j];
        if ((int)a.slot != me) continue;
        if (h == g_help.end()) { h = g_help.insert(std::make_pair(key, BdHelpRow())).first; BdHelpNonceFit(key, h->second, r.nonce); BdHelpRecMerge(key, h->second); }
        BdHelpRow& hr = h->second;
        const size_t gone = coopbuild::BuildHelpConfirm(&hr.owed, a.seq);
        const unsigned int ns = coopbuild::BuildHelpNextSeq(hr.nextSeq, (a.live > a.seq) ? a.live : a.seq);   /* help1 fold 3: past the live last applied - never lowered */
        if (gone != 0 || ns != hr.nextSeq) changed = 1;
        if (ns != hr.nextSeq && g_helpRecDirty < 1) g_helpRecDirty = 1;
        hr.nextSeq = ns;
        if (gone != 0)
        {
            g_helpConfirmed += (long long)gone;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help confirmed key=%.63s seq<=%u (%u entries) unacked=%u", key.c_str(), a.seq, (unsigned int)gone, (unsigned int)hr.owed.size());
            BdLogHelp(line);
        }
        int dueNow = 0;   /* help1 fold 4 (leftover 3) */
        for (int i = 0; i < (int)a.nEx && i < (int)coopbuild::kBuildMaxMats; ++i)
        {
            const int owe = coopbuild::BuildHelpRefundOwed(a.ex[i], hr.refunded[i]);
            if (owe <= 0) continue;
            if (ready == 0)
            {
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help refund key=%.63s mat=%d x%d waits: this game's own records (pp.help) are not read yet - never handed back twice",
                            key.c_str(), i, owe);
                BdLogHelpFreq(line);
                dueNow = 1;
                continue;
            }
            char sid[128];
            sid[0] = 0;
            void* gd = BdMatDataPod(b, i);
            if (gd != 0) BdStrPod((const char*)gd + kGdStringId, sid, (int)sizeof(sid));
            ::Character* c = (sid[0] != 0) ? BdNearestOwnChar(r.pos) : 0;
            int units = 0;   /* T-447 leftover 1 */
            const int given = (c != 0) ? ItemsGiveMaterial(c, std::string(sid), owe, &units) : 0;
            if (given == 1) { hr.refunded[i] += owe; g_helpRefundItems += owe; gave = 1; }
            else
            {
                ++g_helpRefundFailed;
                if (given == 0) dueNow = 1;   /* help1 fold 4: still due */
                if (given == 0 && units > 0 && units < owe)
                {   /* T-447 leftover 1: the units placed before the refusal are handed back; only the rest stays due */
                    hr.refunded[i] += units; g_helpRefundItems += units; gave = 1;
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help refund key=%.63s mat=%d PARTIAL: %d of %d placed before the inventory refused the rest - only the rest stays due",
                                key.c_str(), i, units, owe);
                    BdLogHelp(line);
                }
            }
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help refund key=%.63s mat=%d sid=%.60s x%d %s (the owner's game refused them: past the material's total)",
                        key.c_str(), i, sid[0] != 0 ? sid : "?", owe,
                        given == 1 ? "-> the nearest own character"
                        : (sid[0] == 0 ? "NOT GIVEN (the material is unreadable) - the next STATE tries again"
                        : (c == 0 ? "NOT GIVEN (no own character) - the next STATE tries again"
                        : (given < 0 ? "NOT GIVEN (the engine's add FAULTED - left alone)" : "NOT GIVEN (the inventory refused it) - the next STATE tries again"))));
            BdLogHelp(line);
            if (given < 0) { hr.refunded[i] += owe; gave = 1; }   /* a faulted add may have half-filed the item: never retried (items.cpp M-3) */
        }
        hr.refundDue = dueNow;   /* help1 fold 4 */
    }
    if (fromPlace == 0 && me >= 0 && h != g_help.end())
    {   /* help1 fold 2 (re-check HIGH 1): nothing above the owner's last applied was applied (strictly in order) - a gap no entry fills
           (pp.help's next ahead of a lost owed file, a re-placed piece's old next) renumbers this game's entries from it, never held forever */
        BdHelpRow& hr = h->second;
        coopbuild::BuildHelpConfirm(&hr.owed, r.helpTailSeq);
        const unsigned int was = hr.nextSeq;
        unsigned int lo = 0;   /* help1 fold 3: the lowest entry above the owner's LIVE last applied */
        for (size_t q = 0; q < hr.owed.size(); ++q) if (hr.owed[q].seq > r.helpTailLive && (lo == 0 || hr.owed[q].seq < lo)) lo = hr.owed[q].seq;
        const size_t rn = coopbuild::BuildHelpResync(&hr.owed, &hr.nextSeq, r.helpTailSeq, r.helpTailLive);   /* help1 fold 3 (finding 1): (landed, live] kept as applied; renumbered only above live; next never lowered to live or below */
        if (rn != 0)
        {
            ++g_helpResynced;
            for (size_t q = 0; q < hr.owed.size(); ++q) if (hr.owed[q].applied == 0) hr.owed[q].sent = 0;   /* they go at once under their new numbers (fold 3: not those the owner applied) */
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help resync key=%.63s: the owner applied up to seq %u and no entry holds %u (lowest was %u) - %u entries renumbered from %u, next %u; resynced=%lld landed=%u",
                        key.c_str(), r.helpTailLive, r.helpTailLive + 1, lo, (unsigned int)rn, r.helpTailLive + 1, hr.nextSeq, g_helpResynced, r.helpTailSeq);
            BdLogHelp(line);
            BdHelpSaveSoon();
        }
        if (hr.nextSeq != was)
        {
            std::map<BdPlKey, coopown::HelpRecRow>::iterator rc = g_helpRec.find(BdPlKey(key, hr.nonce));   /* the record's next would raise it back (BdHelpRecMerge); fold 3: this placement's row */
            if (rc != g_helpRec.end()) rc->second.nextSeq = hr.nextSeq;
            if (g_helpRecDirty < 1) g_helpRecDirty = 1;
            BdHelpSaveSoon();
        }
    }
    if (h != g_help.end() && r.complete == 0 && wall == 0)
    {
        const BdHelpRow& hr = h->second;
        float sP = hr.pendP;
        int sn = hr.pendN;
        float sd[coopbuild::kBuildMaxMats];
        for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) sd[i] = hr.pendM[i];
        coopbuild::BuildHelpSum(hr.owed, &sP, &sn, sd);
        int anyM = 0;
        for (int i = 0; i < sn && i < (int)coopbuild::kBuildMaxMats; ++i) if (sd[i] > 0.0f) anyM = 1;
        float cp = 0.0f, cn = 0.0f;
        int cc = 0, cd = 0, cx = 0;
        if ((sP > 0.0f || anyM != 0) && BdReadState(b, &cp, &cn, &cc, &cd, &cx) != 0 && cc == 0 && cx == 0 && cd == 0)
        {
            float cm[coopbuild::kBuildMaxMats];
            int ctot = -1;
            const int cmn = BdReadMats(b, cm, (int)coopbuild::kBuildMaxMats, &ctot);
            const int n = (cmn < 0) ? 0 : cmn;
            for (int i = 0; i < n && i < sn; ++i) cm[i] += sd[i];
            int capped = 0;
            const float np = coopbuild::BuildHelpCopyCap(cp + sP, cn, 0, &capped);
            int ml = -1, stg = 0;
            if (BdApplyStatePod(b, 1, np, n, cm, &ml, upd, &stg) != 0)
            {
                ++g_helpPredicted;
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help predicted key=%.63s %.2f -> %.2f/%.2f (this game's unconfirmed work back on the copy after the owner's STATE) unacked=%u",
                            key.c_str(), cp, np, cn, (unsigned int)hr.owed.size());
                BdLogHelpFreq(line);
            }
        }
    }
    float ap = 0.0f, an = 0.0f;
    int ac = 0, ad = 0, ax = 0, at = -1;
    if (BdReadState(b, &ap, &an, &ac, &ad, &ax) != 0) { r.progress = ap; r.needed = an; r.complete = ac; }
    r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &at);
    BdHelpBaseRead(r, b);
    if (gave != 0)
    {
        if (g_helpRecDirty < 2) g_helpRecDirty = 2;   /* pp.help now */
        BdHelpSave();   /* the owed file at once: an item handed back is never handed back again */
    }
    else if (changed != 0) BdHelpSaveSoon();
}

/* build1-c, K2 safe point: one held STATE onto its copy. 1 = done with it (applied or dropped), 0 = keep it. */
int BdApplyOne(const std::string& key, BdStatePend& s)
{
    const coopbuild::BuildMsg& m = s.m;
    char line[500];
    std::map<std::string, BdReg>::iterator row = g_reg.find(key);
    if (row != g_reg.end() && row->second.removed != 0)   /* build1-d: a STATE after the copy's REMOVE */
    {
        if (BdPlacePending(key)) { s.nextTry = g_drainNo + kRetryGap; return 0; }   /* a new piece at the same key */
        ++g_stateAfterRemove;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s dropped: the copy was removed (REMOVE)", key.c_str());
        BdLogState(line);
        return 1;
    }
    if (row == g_reg.end() || row->second.via != 3)
    {
        if (row == g_reg.end() && BdPlacePending(key)) { s.nextTry = g_drainNo + kRetryGap; return 0; }   /* applied when made */
        ++g_stateNoCopy;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s dropped: %s", key.c_str(),
                     row == g_reg.end() ? "no copy and no pending PLACE here" : "the key is THIS game's own piece");
        BdLogState(line);
        return 1;
    }
    void* const owner = (void*)BdRowStandIn(row->second);   /* the stand-in of the player the copy belongs to */
    if (owner == 0 && row->second.ownerSlot >= 0) { s.nextTry = g_drainNo + kRetryGap; return 0; }   /* that player's stand-in is not here yet: kept, the latest wins */
    int fv = 0;
    void* b = BdFindPiece(key, row->second, &fv);   /* re-found every time: never a kept pointer (build1-e: furniture in its host) */
    if (b == 0)
    {
        if (s.waitLogged == 0) { s.waitLogged = 1; ++g_stateWaitArea; }
        s.nextTry = g_drainNo + kRetryGap;   /* the copy's area is not loaded here: kept, the latest wins */
        return 0;
    }
    float lp = 0, ln = 0;
    int lc = 0, ld = 0, lx = 0;
    if (!BdReadState(b, &lp, &ln, &lc, &ld, &lx)) { ++g_stateFaulted; return 1; }
    void* o = BdOwnerPod(b);
    if (ld != 0 || owner == 0 || o != owner)
    {
        ++g_stateNoCopy;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s dropped: the live piece %s", key.c_str(), ld != 0 ? "is dismantled" : "is not owned by its owner's stand-in");
        BdLogState(line);
        return 1;
    }
    if (m.needed > 0.0f && ln > 0.0f && std::fabs(ln - m.needed) > 0.001f * m.needed && BdIsWall(b) == 0 && BdSetNeededPod(b, m.needed) != 0)
    {   /* help1 fold (review LOW 11): a copy's needed off the owner's (a raise never put back) heals to the STATE's */
        ++g_neededHealed;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s healed the copy's needed %.2f -> %.2f neededHealed=%lld", key.c_str(), ln, m.needed, g_neededHealed);
        BdLogState(line);
        ln = m.needed;
    }
    /* P15 (protocol 98): the owner's destroyed state first - the engine's own setDestroyed(1) for a ruin, its repair road for a piece made
       whole again (coopbuild::BuildDestroyedAction; a STATE made from a PLACE carries none). Never while this key's REMOVE is pending. */
    const int dAct = coopbuild::BuildDestroyedAction(s.fromPlace, (int)m.destroyed, lx);
    if (dAct != coopbuild::kBuildDestroyKeep && g_pendRemove.find(key) != g_pendRemove.end())
    {
        ++g_destroyWaitRemove;
        s.nextTry = g_drainNo + kRetryGap;   /* the removal runs first; this STATE is then dropped as after-REMOVE */
        return 0;
    }
    if (dAct == coopbuild::kBuildDestroyApply || dAct == coopbuild::kBuildDestroyAlready)
    {
        int mlD = -1, stD = 0;
        const int okM = BdApplyStatePod(b, 0, 0.0f, (int)m.nMats, m.mats, &mlD, 0, &stD);   /* delivered mats only (stage 1; no progress, no 0x558680) */
        if (dAct == coopbuild::kBuildDestroyApply) DoorsForgetOwner(b);   /* P15 fold 1 (CRASH 1a): the door rows go before the doors do */
        const int okD = (dAct == coopbuild::kBuildDestroyApply) ? BdDestroyPod(b, true) : 1;
        float dp = 0, dn = 0;
        int dc = 0, dd = 0, dx = 0;
        BdReadState(b, &dp, &dn, &dc, &dd, &dx);
        BdReg& rdr = row->second;
        rdr.progress = dp; rdr.needed = dn; rdr.complete = dc; rdr.dismantled = dd; rdr.destroyed = dx;
        rdr.ownerLast = m; rdr.ownerKnown = 1;
        const char* how = "APPLIED via setDestroyed(1) vt+0x340";
        if (!okD || !okM) { ++g_destroyFaulted; how = "FAULTED"; }
        else if (dAct == coopbuild::kBuildDestroyAlready) { ++g_destroyAlready; how = "already a ruin here (mats only)"; }
        else if (dx != 0) ++g_destroyApplied;
        else
        {
            ++g_destroyPosted; how = "NOT SET (the engine deferred it - message 16)";
            /* P15 fold 1 (LOW 4): the call ran and +0x1A1 still reads 0 - the engine posted it (world lock -> message 16). The STATE is
               kept and tried again; the next try finds the ruin (kBuildDestroyAlready) once the message has run. Bounded. */
            if (s.deferTries < kDestroyDeferTries)
            {
                ++s.deferTries;
                ++g_destroyDeferRetried;
                s.nextTry = g_drainNo + kRetryGap;
                std::sprintf(line, "[BUILD] <- STATE destroyed key=%.63s NOT SET (the engine deferred it - message 16): kept, tried again in %u safe points (%d of %d)",
                             key.c_str(), kRetryGap, s.deferTries, kDestroyDeferTries);
                BdLogState(line);
                return 0;
            }
            ++g_destroyDeferGaveUp;
        }
        const unsigned long long rUpdD = Rva("UpdatePhysicalWithProgress");
        if (rUpdD != 0) BdHelpOnState(key, rdr, b, m, (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpdD, BdIsWall(b), s.fromPlace);   /* help1: the tail's confirmations */
        std::sprintf(line, "[BUILD] <- STATE destroyed key=%.63s %s: +0x1A1 %d -> %d progress %.2f/%.2f complete=%d (sent %.2f/%.2f complete=%d destroyed=1) destroyApplied=%lld",
                     key.c_str(), how, lx, dx, dp, dn, dc, m.progress, m.needed, (int)m.complete, g_destroyApplied);
        BdLogState(line);
        return 1;
    }
    if (dAct == coopbuild::kBuildDestroyRepair && BdIsWall(b) != 0 && row->second.ownerKnown != 0 && row->second.ownerLast.destroyed == 0)
    {   /* P15 fold 2 (re-check #2 HIGH): a copy WALL ruin that the owner never had (its last word was whole) is this game's own
           engine's ruin, kept until wall progress syncs (T-221) - an ordinary owner STATE is not a repair; repairing here deleted and
           remade the gate doors and mounted pieces on every owner STATE while the engine ruined the wall again */
        ++g_copyWallRuinKept;
        return 1;
    }
    if (dAct == coopbuild::kBuildDestroyRepair)
    {
        const int wallR = BdIsWall(b);
        DoorsForgetOwner(b);   /* P15 fold 1 (CRASH 1a): the dead rows go before the repair makes new doors under the same keys */
        const int okR = (wallR != 0) ? BdDestroyPod(b, false) : BdProgressPod(b, 0.0f);   /* the engine's ruin reset (part-built, then setDestroyed(0)); a wall: setDestroyed(0) */
        const int lx0 = lx;
        const int rOk = (okR != 0) ? BdReadState(b, &lp, &ln, &lc, &ld, &lx) : 0;
        /* P15 fold 1 (LOW 4 / 8): the call ran and +0x1A1 still reads 1 - the engine deferred it (message 16), which is not a fault */
        const int repDeferred = (rOk != 0 && lx != 0 && ld == 0) ? 1 : 0;
        if (rOk != 0 && lx == 0)
        {
            ++g_repairApplied;
            /* P15 fold 1 (MED 3): the owner's word is recorded now, before any early return below - it is what BdCopyDestroyedHere
               reverts to, and a wall repaired here reads complete=1 against the STATE's complete=0 (the stale test is skipped for it) */
            row->second.ownerLast = m; row->second.ownerKnown = 1;
        }
        else if (repDeferred != 0) ++g_repairDeferred;
        else ++g_destroyFaulted;
        std::sprintf(line, "[BUILD] <- STATE repaired key=%.63s %s: +0x1A1 %d -> %d via %s, now %.2f/%.2f complete=%d - the STATE (%.2f/%.2f complete=%d) goes on next; repairApplied=%lld",
                     key.c_str(), (rOk != 0 && lx == 0) ? "APPLIED" : (repDeferred != 0 ? "NOT SET (deferred - message 16; P15 fold 1)" : "FAULTED"), lx0, lx, wallR != 0 ? "setDestroyed(0) vt+0x340 (wall)" : "the ruin reset vt+0x230(0)",
                     lp, ln, lc, m.progress, m.needed, (int)m.complete, g_repairApplied);
        BdLogState(line);
        if (repDeferred != 0 && s.deferTries < kDestroyDeferTries) { ++s.deferTries; s.nextTry = g_drainNo + kRetryGap; return 0; }   /* P15 fold 1 (LOW 4): kept, bounded */
        if (repDeferred != 0) ++g_destroyDeferGaveUp;
        if (rOk == 0 || lx != 0 || ld != 0) return 1;
    }
    if (lc != 0 && m.complete == 0 && (dAct != coopbuild::kBuildDestroyRepair || BdIsWall(b) != 0))   /* P15 fold 2 (#3): the guard stays on for walls */   /* un-completing is build1-d's dismantle, never a STATE; P15 fold 1 (MED 3): not after the repair road */
    {
        ++g_stateStale;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s ignored: the copy is complete here and the STATE says %.2f/%.2f complete=0",
                     key.c_str(), m.progress, m.needed);
        BdLogState(line);
        return 1;
    }
    const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
    if (rUpd == 0)
    {
        ++g_stateFaulted;
        BdLogState("[BUILD] <- STATE dropped: UpdatePhysicalWithProgress (0x558680) is not in the address table");
        return 1;
    }
    const uintptr_t upd = (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd;
    const float need = (ln > 0.0f) ? ln : m.needed;   /* the live needed, re-read now */
    int matsLive = -1, stage = 0, ok = 0, completeCalled = 0;
    const int wall = BdIsWall(b);   /* review-build1c LOW (walls): 0x558680 reads a segment's own fields, not the shared state */
    if (wall != 0)
    {
        ok = BdApplyStatePod(b, 0, 0.0f, (int)m.nMats, m.mats, &matsLive, upd, &stage);   /* mats only: no progress, no 0x558680, no FLT_MAX */
        ++g_stateWall;
    }
    else if (m.complete == 0)
    {
        float pr = m.progress;
        if (pr < 0.0f) pr = 0.0f;
        if (pr > need * 0.999f) pr = need * 0.999f;   /* answers R2 trap: 0x558680 completes a piece at progress >= needed */
        ok = BdApplyStatePod(b, 1, pr, (int)m.nMats, m.mats, &matsLive, upd, &stage);
    }
    else
    {
        ok = BdApplyStatePod(b, 0, 0.0f, (int)m.nMats, m.mats, &matsLive, upd, &stage);
        if (ok && lc == 0) { ok = BdProgressPod(b, FLT_MAX); completeCalled = 1; }   /* the engine's own complete-now path */
    }
    if (!ok)
    {
        ++g_stateFaulted;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s FAULTED at stage %d%s", key.c_str(), stage, completeCalled ? " (vt+0x230 FLT_MAX)" : "");
        BdLogState(line);
        return 1;
    }
    if (m.nMats > 0 && matsLive >= 0 && matsLive != (int)m.nMats) ++g_matsCountDiff;
    ++g_stateApplied;
    if (completeCalled) ++g_completeApplied;
    float ap = 0, an = 0;
    int ac = 0, ad = 0, ax = 0, aTotal = -1;
    BdReadState(b, &ap, &an, &ac, &ad, &ax);
    BdReg& r = row->second;
    r.progress = ap; r.needed = an; r.complete = ac; r.dismantled = ad; r.destroyed = ax;
    r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &aTotal);
    BdHelpOnState(key, r, b, m, upd, wall, s.fromPlace);   /* help1: the confirmation, the refund, this game's unconfirmed work back on, the baseline */
    if (s.fromPlace == 0) { r.ownerLast = m; r.ownerKnown = 1; }   /* P15: the owner's last state - put back if this game's engine makes the copy a ruin */
    std::sprintf(line, "[BUILD] <- STATE applied key=%.63s %.2f/%.2f complete=%d mats=%d/%d (sent %.2f/%.2f complete=%d)%s%s%s", key.c_str(),
                 ap, an, ac, (int)m.nMats, matsLive, m.progress, m.needed, (int)m.complete,
                 completeCalled ? " via vt+0x230(FLT_MAX)" : "", s.fromPlace ? " from=PLACE" : "", wall != 0 ? " wall(mats only)" : "");
    BdLogState(line);
    if (lc == 0 && ac != 0) ItFurnMarkStale(3);   /* T-454: the owner's STATE completed the copy (0 -> 1) */
    return 1;
}

/* build1-d, K2 safe point: one REMOVE onto its copy. 1 = done with it, 0 = keep it (area not loaded here, or a deferred
   wall). No __try here (C2712): every engine touch goes through the guarded Bd* helpers. */
int BdRemoveOne(const std::string& key, BdRemovePend& rp)
{
    char line[500];
    std::map<std::string, BdReg>::iterator row = g_reg.find(key);
    if (row == g_reg.end() || row->second.via != 3 || row->second.removed != 0)
    {
        ++g_removeNoCopy;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 - %s", key.c_str(),
                     row == g_reg.end() ? "no copy here" : (row->second.via != 3 ? "the key is THIS game's own piece" : "the copy was already removed"));
        BdLogRemove(line);
        return 1;
    }
    BdReg& r = row->second;
    int fv = 0;
    void* b = BdFindPiece(key, r, &fv);   /* re-found every time: never a kept pointer (build1-e: furniture in its host) */
    if (b == 0)
    {
        if (rp.tries > 0)   /* our call ran and the piece is gone: the kill list freed it (answers-build1b R3: one frame later) */
        {
            r.removed = 1; r.dismantled = 1;
            BdNoteRemoved(key);   /* review-build1d D5 */
            ++g_copyRemoved;
            std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=1 dismantled=1 how=freed tries=%d reason=%d matsBefore=%.1f",
                         key.c_str(), rp.tries, rp.reason, rp.matsBefore);
            BdLogRemove(line);
            return 1;
        }
        /* review-build1d D3: no call of ours ran and the key does not resolve. In a LOADED area (the zone check the copy's
           creation uses) the copy is already gone (a wall run's cascade, a mounted piece...): finished, and a PLACE waiting
           for this key goes ahead. Only an area that is not loaded here waits. */
        /* build1-e (recheck-build1d F2): only a COMPLETE, untruncated search that found NO candidate, for a copy whose created
           key was the builder's, means gone */
        const int loaded = (r.hasPos != 0 && IsPositionLoadedHere(r.pos[0], r.pos[1], r.pos[2])) ? 1 : 0;
        if (loaded != 0 && fv == kObjKeyNone && r.keySame != 0)
        {
            r.removed = 1; r.dismantled = 1;
            BdNoteRemoved(key);
            ++g_removeAlreadyGone;
            std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=1 dismantled=1 how=already-gone (area loaded, the key no longer resolves) reason=%d",
                         key.c_str(), rp.reason);
            BdLogRemove(line);
            return 1;
        }
        if (loaded != 0)   /* build1-e (F2): loaded, but the miss proves nothing - retried, then given up (never finished early) */
        {
            ++rp.missTries;
            if (rp.missTries == 1)
            {
                ++g_removeUnclean;
                std::sprintf(line, "[BUILD] <- REMOVE key=%.63s held: the lookup was %s%s - retried every %u safe points, given up after %d",
                             key.c_str(), BdVerdictWord(fv), r.keySame != 0 ? "" : " and the copy's own key differs from the builder's",
                             kRetryGap, kRemoveTries);
                BdLogRemove(line);
            }
            if (rp.missTries >= kRemoveTries)
            {
                r.removed = 1;
                BdNoteRemovedGaveUp(key);   /* P87 rf1 (MED): the copy may still stand - its list entry stays */
                ++g_removeGaveUp;
                std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 GIVEN UP after %d lookups that were no complete empty search (%s)",
                             key.c_str(), rp.missTries, BdVerdictWord(fv));
                BdLogRemove(line);
                return 1;
            }
            rp.nextTry = g_drainNo + kRetryGap;
            return 0;
        }
        if (rp.waitLogged == 0)
        {
            rp.waitLogged = 1;
            ++g_removeWaitArea;
            std::sprintf(line, "[BUILD] <- REMOVE key=%.63s held: the copy's area is not loaded here - re-checked every %u safe points",
                         key.c_str(), kRetryGap);
            BdLogRemove(line);
        }
        rp.nextTry = g_drainNo + kRetryGap;
        return 0;
    }
    float lp = 0, ln = 0;
    int lc = 0, ld = 0, lx = 0;
    if (!BdReadState(b, &lp, &ln, &lc, &ld, &lx)) { ++g_removeFaulted; return 1; }
    void* const owner = (void*)BdRowStandIn(r);   /* the stand-in of the player the copy belongs to */
    if (owner == 0 && r.ownerSlot >= 0) { rp.nextTry = g_drainNo + kRetryGap; return 0; }   /* that player's stand-in is not here yet: held */
    const int rmCls = BdOwnerClassAt(b, key, owner, r.ownerSlot, "REMOVE");   /* P87 fold 3 (T651): the one owner rule (BdCopyOne, BdReconDrain); P87 root: the copy list first */
    if (rmCls == coopbuild::kBuildLiveOwnSenderList)
    {
        const int rmFix = BdOwnerFixToSender(b, rmCls, owner);
        ++g_clRemove;
        if (rmFix < 0) ++g_ownerFixFailed;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s the live piece is in this game's copy list as slot %d's copy (key, sid) - the sender's copy; ownerFixed=%d copyListRemove=%lld",
                     key.c_str(), r.ownerSlot, rmFix, g_clRemove);
        BdLogRemove(line);
    }
    if (rmCls == coopbuild::kBuildLiveOwnSenderRecord)
    {
        const int rmFix = BdOwnerFixToSender(b, rmCls, owner);
        ++g_ownerRecordRemove;
        if (rmFix < 0) ++g_ownerFixFailed;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s the live piece's owner is a save's stand-in of the sender's slot %d (its record, not the live object) - the sender's copy; ownerFixed=%d ownerRecordRemove=%lld",
                     key.c_str(), r.ownerSlot, rmFix, g_ownerRecordRemove);
        BdLogRemove(line);
    }
    if (coopbuild::BuildOwnerIsSender(rmCls) == 0)
    {
        ++g_removeNoCopy;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=%d - the live piece is not the sender's copy (ownerWas=%s)", key.c_str(), ld, coopbuild::BuildOwnerWord(rmCls));
        BdLogRemove(line);
        return 1;
    }
    const int wall = BdIsWall(b);
    ld = BdDismOf(b, ld);   /* review-build1d D2: a wall segment's own +0x162, not the run's shared state */
    if (ld != 0)   /* +0x162 set: our earlier call landed (a wall's deferred flush), or something else dismantled it first - skipped */
    {
        r.removed = 1; r.dismantled = 1;
        BdNoteRemoved(key);   /* review-build1d D5 */
        if (rp.tries > 0) ++g_copyRemoved;
        else ++g_removeAlready;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=%d dismantled=1 how=%s tries=%d wall=%d reason=%d matsBefore=%.1f",
                     key.c_str(), rp.tries > 0 ? 1 : 0, rp.tries > 0 ? "flag" : "already-dismantled-here", rp.tries, wall, rp.reason,
                     rp.matsBefore);
        BdLogRemove(line);
        return 1;
    }
    if (rp.tries >= kRemoveTries)
    {
        r.removed = 1;
        BdNoteRemovedGaveUp(key);   /* review-build1d D5; P87 rf1 (MED): the copy may still stand - its list entry stays */
        ++g_removeGaveUp;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 GIVEN UP after %d vt+0x248 calls wall=%d", key.c_str(),
                     rp.tries, wall);
        BdLogRemove(line);
        return 1;
    }
    /* FIRST the delivered mats go to 0 (on every call: nothing may re-deliver in between), so the engine's refund drops nothing.
       review-build1d D4: not for a wall copy - 0x29DBD0 never refunds a wall run's shared state, and zeroing it would wipe the
       whole run's delivered mats here (only read, for the log). */
    float before = 0.0f, after = 0.0f;
    const int nz = BdMatsSumZero(b, wall == 1 ? 0 : 1, &before);
    if (rp.tries == 0)
    {
        rp.matsBefore = before;
        if (nz >= 0 && wall != 1) ++g_matsZeroed;
        if (wall == 1) ++g_removeWall;
    }
    else ++g_removeRetried;
    const long long rc0 = g_refundCalledOnCopy, rd0 = g_refundOnCopy;
    int ret = -1;
    const int ok = BdDismantlePod(b, rp.tries == 0 ? FLT_MAX : kRemoveRetryAmount, &ret);   /* the engine's own dismantle path */
    ++rp.tries;
    float ap = 0, an = 0;
    int ac = 0, ad = 0, ax = 0;
    const int stOk = BdReadState(b, &ap, &an, &ac, &ad, &ax);   /* still alive: the kill list frees it next frame */
    if (stOk != 0) ad = BdDismOf(b, ad);   /* review-build1d D2: a wall segment's own +0x162 */
    BdMatsSumZero(b, 0, &after);
    if (!ok)
    {
        ++g_removeFaulted;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 vt+0x248 FAULTED (try %d)", key.c_str(), rp.tries);
        BdLogRemove(line);
        return 1;
    }
    if (stOk != 0 && ad != 0)
    {
        r.removed = 1; r.dismantled = 1;
        BdNoteRemoved(key);   /* review-build1d D5 */
        ++g_copyRemoved;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=1 dismantled=1 how=call tries=%d wall=%d reason=%d mats(%d) %.1f -> %.1f"
                     " refundCalls=%lld refundDrops=%lld returned=%d progress %.2f/%.2f",
                     key.c_str(), rp.tries, wall, rp.reason, nz, before, after, g_refundCalledOnCopy - rc0, g_refundOnCopy - rd0, ret, ap, an);
        BdLogRemove(line);
        return 1;
    }
    if (rp.deferLogged == 0)
    {
        rp.deferLogged = 1;
        std::sprintf(line, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 deferred wall=%d mats(%d) %.1f -> %.1f progress %.2f/%.2f"
                     " - vt+0x248 retried at the next safe points (up to %d calls)", key.c_str(), wall, nz, before, after, ap, an, kRemoveTries);
        BdLogRemove(line);
    }
    rp.nextTry = g_drainNo + 1;
    return 0;
}

/* review-build1d D5, MAIN THREAD (BuildTick): a row marked removed is erased kRemovedKeepMs after its removal. Until then a
   late STATE is still recognised as late; after it the row no longer takes a registry slot and a rebuild at the spot starts
   clean. A row a new piece has taken again (removed back to 0), or whose REMOVE is still running here, is kept. */
void BdSweepRemoved()
{
    if (g_recentRemoved.empty()) return;
    const DWORD now = ::GetTickCount();
    for (std::map<std::string, DWORD>::iterator it = g_recentRemoved.begin(); it != g_recentRemoved.end(); )
    {
        if ((DWORD)(now - it->second) < kRemovedKeepMs) { ++it; continue; }
        std::map<std::string, BdReg>::iterator row = g_reg.find(it->first);
        if (row != g_reg.end() && row->second.removed != 0 && g_pendRemove.find(it->first) == g_pendRemove.end())
        {
            if (row->second.own != 0 && row->second.via != 3)
            {
                coopown::BuildRetireNote(&g_ownRetireOwed, it->first, row->second.gen);   /* mmo8a2 (M2): still owed to pp.build */
                BdRestoreNonce(it->first, row->second, 1);   /* help1 fold 4: its record under its REAL nonce */
                const std::vector<coopbuild::BuildHelpAck>* ga = BdHelpAcksRead(it->first, row->second);   /* help1 fold (MED 3): later work for it is answered HELP_GONE; */
                const int kept = (ga != 0 && !ga->empty()) ? BdGoneNote(it->first, row->second.nonce, *ga, 1) : 0;   /* fold 2 (HIGH 2): with its last applied seqs; fold 3: its (key, nonce) record */
                if (kept == 0) g_ownSwept.insert(it->first);   /* help1 fold 4: the key stays known (at the cap too); a swept mark alone never answers GONE applied 0 */
            }
            g_reg.erase(row);
            ++g_removedRowsErased;
        }
        g_recentRemoved.erase(it++);
    }
}

void BdLogRoster(const char* line)   /* review-build1f: the roster's own cap (kRosterLineCap) */
{
    if (g_rosterLines < kRosterLineCap) { ++g_rosterLines; DebugLog(std::string(line)); }
    else ++g_rosterSuppressed;
}

/* P14 (mmo8b), MAIN THREAD (the scans, BdTombQueue): an own piece stands at a tombstone's key. 1 = it is the save's copy of a piece the
   records retired (a READ-BACK tombstone, coopbuild::BuildTombSeenAct) - its removal is queued (or already was): the caller neither
   registers it nor drops the tombstone. 0 = the tombstone ends as before (the caller drops it). */
int BdTombSeenOwn(const std::string& key, const char* road)
{
    std::map<std::string, BdTombInfo>::const_iterator ti = g_tombInfo.find(key);
    if (ti == g_tombInfo.end()) return 0;
    if (g_tombKillNo.count(key) != 0) return 0;   /* P14 fold 1: its removal was cancelled or given up in this world - the tombstone ends as before P14 */
    if (coopbuild::BuildTombSeenAct(ti->second.readBack, ti->second.tail, BdOwnLiveRow(key), BdPlacePending(key)) != coopbuild::kBuildTombKill) return 0;
    if (g_tombKill.find(key) != g_tombKill.end()) return 1;
    g_tombKill[key] = BdTombKill();
    ++g_p14Queued;
    char line[420];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: the %s found an own piece standing where pp.build says it was dismantled"
                " (after the save this zone came from; nonce=%08x%s) - removed at the safe point with no refund, not registered; p14Queued=%lld",
                key.c_str(), road, ti->second.nonce, ti->second.furn != 0 ? ", furniture" : "", g_p14Queued);
    BdLogP14(line);
    return 1;
}

/* P87 fold 1 (H1, H1b), MAIN THREAD (BdScanPiece): an own layout-furniture piece the scan walked - a tombstone at its key ends (the
   owner still has it) and its queued REMOVE never goes. b is this tick's walk pointer; the key is built from the plain field (podPos 1). */
void BdTombSeenOwnPiece(void* b)
{
    if (g_tombInfo.empty() || BdByteAtOr0(b, kBOwnDismantled) != 0) return;
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) return;
    /* P87 fold 2 (MED): a layout piece's key can drift across a reload (keySame / BdRsRekey) - every tombstone within ItBoxResolve's
       window of it (BoxKeyNearKey: same sector and record, x/z +/-0.1 u) ends, as the exact one does. Prefilter: the matcher needs the
       "sid@sx,sy@" text (up to the last '@') to be the same - both keys come from the one builder */
    const char* at = std::strrchr(k, '@');
    if (at == 0) return;
    const size_t pl = (size_t)(at - k) + 1;
    std::vector<std::string> hit;
    for (std::map<std::string, BdTombInfo>::const_iterator it = g_tombInfo.begin(); it != g_tombInfo.end(); ++it)
    {
        if (it->first.size() < pl || it->first.compare(0, pl, k, pl) != 0) continue;
        if (it->first == k || BoxKeyNearKey(k, it->first.c_str()) != 0) hit.push_back(it->first);
    }
    for (size_t i = 0; i < hit.size(); ++i)
    {
        if (hit[i] == k && BdTombSeenOwn(hit[i], "furniture scan") != 0) continue;   /* P14 fold 1 (F2): only the EXACT key is the retired piece */   /* P14 (mmo8b): a read-back tombstone - the piece is removed, the tombstone kept */
        ++g_tombScanDropped;
        const int exact = (hit[i] == k) ? 1 : 0;
        if (exact == 0) ++g_tombNearHeld;
        BdTombDrop(hit[i], exact != 0 ? "the scan found an own furniture piece standing at the key"
                                      : "the scan found an own furniture piece within ItBoxResolve's window of the key (a drifted layout key)", 1);
    }
}

/* build1-f ROSTER, MAIN THREAD (BdRosterTick; once per world load - mmo8a3 (B): with or without the link - and per link-up; never on a hot path). One
   zone walk (LoadedBuildings - the walk nearlist and ItBoxResolve use): an own-player-faction piece the registry does not
   know yet (built in an earlier session and loaded from the save) is registered as own (via=4 scan) with its key, sid,
   position (+0x48, the plain field getPosition copies) and rotation (vt+0xC0, as the commit reads a host's), so the roster
   sends it. FREE-STANDING PIECES ONLY: layout furniture (+0x238 set) is counted in scanFurnitureSkipped and the pieces in an
   own building's interior set in scanInteriorSkipped - their host-local frame, furnitureOf form and floor are not read back
   here. A door a building made for itself (+0x1A0) and a dismantled piece (+0x162) are skipped. The pointers come from this
   walk, on this thread, this tick; the key is built from the plain position field (podPos 1). */
int BdScanPiece(void* b, ::Faction* mine, long long gen0, int zoneScan)   /* T-274 (S2): one piece; 1 registered, 0 not (counted), -1 the walk moved */
{
    if (BoxWalkGen() != gen0) { if (zoneScan != 0) ++g_zsWalkMoved; else ++g_scanWalkMoved; return -1; }   /* review-build1f C1: before this piece's calls */
    if (b == 0 || BdOwnerPod(b) != (void*)mine) return 0;
    if (zoneScan != 0) ++g_zsFound; else { ++g_rdScanFound; ++g_scanFound; }
    if (BdPtrAt(b, kBFurnitureOf) != 0)
    {
        BdTombSeenOwnPiece(b);   /* P87 fold 1 (H1b) */
        if (zoneScan != 0) ++g_zsFurn; else { ++g_rdFurn; ++g_scanFurnitureSkipped; }
        return 0;
    }
    if (BdByteAtOr0(b, kBImADoor) != 0) { if (zoneScan != 0) ++g_zsSkipped; else ++g_scanDoorSkipped; return 0; }
    if (BdByteAtOr0(b, kBOwnDismantled) != 0) { if (zoneScan != 0) ++g_zsSkipped; else ++g_scanDismantled; return 0; }
    if (BdPtrAt(b, kBInterior) != 0)   /* its interior's pieces are not registered (see above): counted */
    {
        int listed = -1, tr = 0, torn = 0;
        InteriorPieceIds(b, g_bdIds, kBdInteriorCap, &listed, &tr, &torn);
        if (listed > 0 && zoneScan != 0) g_zsInner += listed;
        else if (listed > 0) { g_rdInner += listed; g_scanInteriorSkipped += listed; }
        if (torn != 0 || BoxWalkGen() != gen0) { if (zoneScan != 0) ++g_zsWalkMoved; else ++g_scanWalkMoved; return -1; }   /* recheck-build1f K2: the walk saw a teardown */
    }
    char key[kKeyCap];
    key[0] = 0;
    if (ObjectPositionKey(b, key, kKeyCap, "", 3, 1) == 0) { if (zoneScan != 0) ++g_zsSkipped; else ++g_scanNoKey; return 0; }
    if (ProductionBuildingIs(b) != 0)
    {   /* a production building with no build record and no building materials is one the world placed (a mining node): its
           faction mark is the engine's, read by each game on its own copy - never registered as this game's own piece (no PLACE;
           its box stays on the area rule, items.cpp ItBoxOwnerOf). One WITH materials is a player-built machine the registry
           lacks a row for - registered as before (coopown::ScanSkipsWorldNode). */
        const int hasRow = (g_reg.find(std::string(key)) != g_reg.end() || BdRowByLiveKey(std::string(key)) != g_reg.end()) ? 1 : 0;
        int matN = -1;
        float matNone[1];
        if (hasRow == 0) BdReadMats(b, matNone, 0, &matN);
        if (coopown::ScanSkipsWorldNode(1, hasRow, matN) != 0)
        {
            if (zoneScan != 0) ++g_zsSkipped;
            ++g_scanWorldProduction;
            if (g_scanWorldProduction <= 20 || g_scanWorldProduction % 100 == 0)
            {
                char wl[300];
                _snprintf_s(wl, sizeof(wl), _TRUNCATE, "[BUILD] %s key=%.63s: a production building with no build record and no building materials (world-placed) carries this game's faction - not registered as an own piece; scanWorldProduction=%lld",
                            zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", key, g_scanWorldProduction);
                DebugLog(std::string(wl));
            }
            return 0;
        }
        if (hasRow == 0)
        {
            ++g_scanPlayerProduction;
            if (g_scanPlayerProduction <= 20 || g_scanPlayerProduction % 100 == 0)
            {
                char pl[300];
                _snprintf_s(pl, sizeof(pl), _TRUNCATE, "[BUILD] %s key=%.63s: a production building with no build record and materials=%d (player-built) - registered as an own piece; scanPlayerProduction=%lld",
                            zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", key, matN, g_scanPlayerProduction);
                DebugLog(std::string(pl));
            }
        }
    }
    if (!g_copyList.empty() && BdCopyOwnRowAt(std::string(key)) == 0)
    {   /* P87 root (step 4): a listed key is ANOTHER game's copy the load gave this game's faction (a player town) - never registered as
           this game's own piece (its PLACE would go back to its owner as a new piece) */
        std::string lk;
        BdCopyEnt* ce = BdCopyListFind(std::string(key), &lk);
        if (ce != 0)
        {
            ++g_clScanSkipped;
            if (zoneScan != 0) ++g_zsSkipped;
            char sl[400];
            _snprintf_s(sl, sizeof(sl), _TRUNCATE, "[BUILD] P87 copy list key=%.63s: the %s found it owned by THIS game, but it is listed as slot %d's copy - not registered as an own piece; copyListScanSkipped=%lld",
                        lk.c_str(), zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", ce->rec.slot, g_clScanSkipped);
            BdLogCopyList(sl);
            return 0;
        }
    }
    if (!g_tombInfo.empty() && g_tombInfo.find(std::string(key)) != g_tombInfo.end())   /* P87 fold 1 (H1): the owner still has a piece at a tombstone's key */
    {
        if (BdTombSeenOwn(std::string(key), zoneScan != 0 ? "ZONESCAN" : "ROSTER scan") != 0)
        {   /* P14 (mmo8b): a read-back tombstone - the save's copy of a retired piece: removed at the safe point, never registered here */
            if (zoneScan != 0) ++g_zsSkipped;
            return 0;
        }
        ++g_tombScanDropped;
        BdTombDrop(std::string(key), "the scan found an own piece standing at the key", 1);
    }
    {
        std::map<std::string, BdReg>::iterator kn = g_reg.find(std::string(key));
        int knownAlias = 0;
        if (kn == g_reg.end())   /* T-274 fold (MED-1): a handed copy that landed at another spot (KEY MISMATCH) - filed under the partner's key */
        {
            kn = BdRowByLiveKey(std::string(key));
            knownAlias = (kn != g_reg.end()) ? 1 : 0;
        }
        if (kn != g_reg.end())   /* the hook, the lever, a copy, the restore or an earlier scan has it */
        {
            if (kn->second.own != 0 && kn->second.via != 3 && kn->second.removed != 0)   /* P87 fold 2 (LOW 2): an alias hit too - on the row kn points to */
            {   /* P87 fold 1 (H1a): an own row still marked removed (own rows outlive a reload; a removed row is erased kRemovedKeepMs after its
                   removal) whose piece stands live and not dismantled - the piece is back (a reload): live again, its PLACE owed */
                kn->second.removed = 0; kn->second.dismantled = 0; kn->second.gen = g_bdGen;
                g_recentRemoved.erase(kn->first);   /* the 60 s sweep no longer erases it */
                BdOwnDirty(2);   /* its pp.build row comes back */
                if (zoneScan != 0) g_rsOwePlace.push_back(kn->first);   /* a roster round's scan sends it in that round */
                ++g_scanRevived;
                char vl[300];
                _snprintf_s(vl, sizeof(vl), _TRUNCATE, "[BUILD] P87 %s key=%.63s%s: an own row marked removed stands live again (a reload) - live again, its PLACE owed; scanRevived=%lld",
                            zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", kn->first.c_str(), knownAlias != 0 ? " (found by its live key)" : "", g_scanRevived);
                BdLogP87(vl);
            }
            if (kn->second.own != 0 && kn->second.via != 3 && kn->second.gen != g_bdGen) { kn->second.gen = g_bdGen; BdOwnDirty(1); }   /* mmo8a2 (H1): an own piece of THIS world */
            if (kn->second.own != 0 && kn->second.via == 4 && kn->second.placeOk == 0 && kn->second.hostKey.empty() && BoxWalkGen() == gen0)
            {   /* a scan-registered piece whose ground height was not answered then: its placement input is read again now */
                float lpos[3], lrot[4], in[3];
                if (BdCopyF((const char*)b + kObjPos, lpos, 3) && BdOrientPod(b, lrot) && BdPlaceInputFromLive(kn->first, lpos, in) != 0)
                {
                    kn->second.placeOk = 1;
                    for (int j = 0; j < 3; ++j) kn->second.placePos[j] = in[j];
                    for (int j = 0; j < 4; ++j) kn->second.placeRot[j] = lrot[j];
                    ++g_placeFromLiveRetried;
                    BdOwnDirty(1);
                    if (zoneScan != 0) g_rsOwePlace.push_back(kn->first);   /* its PLACE + STATE go out as a roster piece */
                }
            }
            if (zoneScan != 0) { ++g_zsKnown; if (knownAlias != 0) ++g_zsKnownAlias; } else { ++g_scanKnown; if (knownAlias != 0) ++g_scanKnownAlias; }
            return 0;
        }
    }
    if ((int)g_reg.size() >= kRegCap) { ++g_regFull; return 0; }
    if (BoxWalkGen() != gen0) { if (zoneScan != 0) ++g_zsWalkMoved; else ++g_scanWalkMoved; return -1; }   /* review-build1f C1: again before the reads */
    float pos[3], rot[4];
    if (!BdCopyF((const char*)b + kObjPos, pos, 3) || !BdOrientPod(b, rot)) { if (zoneScan != 0) ++g_zsSkipped; else ++g_scanNoPos; return 0; }
    float p = 0, nd = 0;
    int c = 0, d = 0, x = 0;
    BdReadState(b, &p, &nd, &c, &d, &x);
    BdReg& r = BdRegisterOwnLive(b, std::string(key), pos, rot, p, nd, c, x, 1);   /* pos is the live position: stored as a height above the ground */
    if (zoneScan != 0)
    {
        ++g_zsRegistered;
        g_rsOwePlace.push_back(std::string(key));   /* T-274 (S2): its PLACE + STATE go out as a roster piece (as the restore's present-register, mmo8a2 M4); BdRosterWillSend skips it when a round sends it */
    }
    else { ++g_rdScanReg; ++g_scanRegistered; }
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] %s registered key=%s sid=%.80s pos=(%.1f, %.1f, %.1f) complete=%d progress=%.2f/%.2f",
                 zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", key, !r.sid.empty() ? r.sid.c_str() : "?", pos[0], pos[1], pos[2], c, p, nd);
    if (zoneScan == 0) BdLogRoster(line);
    else if (g_zsPieceLines < kZoneScanPieceLineCap) { ++g_zsPieceLines; DebugLog(std::string(line)); }   /* T-274 fold (LOW-5): not the roster's run-wide 40 */
    else ++g_zsPieceSuppressed;
    return 1;
}

/* the whole-world scan (the load scan and every roster round): every loaded zone's walk through BdScanPiece. T-274: unchanged in
   behaviour - the per-piece body moved to BdScanPiece. */
int BdScanLoaded()   /* 1 = done; 0 = the zone walk moved under it (review-build1f C1) - owed again, next tick */
{
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return 1;
    ++g_scanRuns;
    static void* list[4096];
    int zones = 0, trunc = 0;
    const long long gen0 = BoxWalkGen();   /* review-build1f C1: a zone deactivation (any thread) frees what the list names */
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    if (trunc != 0) ++g_scanTruncated;
    g_rdTrunc = trunc;
    for (int i = 0; i < n; ++i)
        if (BdScanPiece(list[i], mine, gen0, 0) < 0) { g_rosterOwed = 1; return 0; }   /* review-build1f C1: owed again, next tick */
    return 1;
}

/* P18, the setFaction hook. Off the main thread (a zone loading its buildings) only counted. On the main thread the owner is read before
   and after the engine's own call and only the outermost call is judged (coopbuild::BuildOwnerChangeAct): a change TO this game's player
   that the mod did not write is queued by key for BdOwnerChangeDrain. No __try here (C2712): the reads are the guarded Bd* helpers. */
void detour_setfaction(void* b, void* f, void* platoon)
{
    if (BdOnMain() == 0) { ::InterlockedIncrement64(&g_ocOffMain); orig_setfaction(b, f, platoon); return; }
    void* before = (b != 0) ? BdOwnerPod(b) : 0;
    const int depth = ++g_ocDepth;
    orig_setfaction(b, f, platoon);
    --g_ocDepth;
    ++g_ocSeen;
    void* after = (b != 0) ? BdOwnerPod(b) : 0;
    const int wasMine = (before != 0 && IsPlayerFaction((::Faction*)before)) ? 1 : 0;
    const int nowMine = (after != 0 && IsPlayerFaction((::Faction*)after)) ? 1 : 0;
    const int act0 = coopbuild::BuildOwnerChangeAct(1, (g_bdOwnerWrite > 0 || g_bdSelf > 0) ? 1 : 0, depth, wasMine, nowMine);
    /* P18 fold 1 (item 4): only a building with a key is queued - furniture, an interior object and a door are the zone scan's refusals */
    const int act = (act0 == coopbuild::kBuildOcRegister && b != 0)
        ? coopbuild::BuildOwnerChangeQueueAct(act0, (BdPtrAt(b, kBFurnitureOf) != 0 || BdByteAtOr0(b, kBInteriorObj) != 0) ? 1 : 0, BdByteAtOr0(b, kBImADoor) != 0 ? 1 : 0)
        : act0;
    if (act == coopbuild::kBuildOcNotBuilding) { ++g_ocSkippedNotBuilding; return; }
    if (act == coopbuild::kBuildOcModWrite) { ++g_ocModWrite; return; }
    if (act == coopbuild::kBuildOcNested) { ++g_ocNested; return; }
    if (act == coopbuild::kBuildOcNotMine) { ++g_ocNotMine; return; }
    if (act == coopbuild::kBuildOcWasMine) { ++g_ocWasMine; return; }
    if (act != coopbuild::kBuildOcRegister) return;
    char key[kKeyCap];
    key[0] = 0;
    if (ObjectPositionKey(b, key, kKeyCap, "", 3, 1) == 0) { ++g_ocNoKey; return; }
    for (size_t i = 0; i < g_ocQ.size(); ++i) if (g_ocQ[i].key == key) { ++g_ocDup; return; }
    if (g_ocQ.size() >= kOcQueueCap) { ++g_ocQueueFull; return; }
    BdOcEnt e;
    e.key = key; e.before = BdFactionName(before); e.tries = 0;
    g_ocQ.push_back(e);
    ++g_ocQueued;
}

/* P18 fold 1 (item 5), ANY THREAD (the AI asks too): Building::isForSale (vt+0x2C0, 0x29C4C0, build/decomp_29c4c0.txt). The engine's own
   answer, refused only for a house a stand-in owns (coopbuild::BuildForSaleKeep) - pointer compares, no lock, an interlocked count. */
bool detour_isforsale(void* b)
{
    const bool r = orig_isforsale(b);
    if (!r || b == 0) return r;
    void* o = *(void* const*)((const char*)b + kRootOwner);   /* the engine's own read in isForSale (param_1[2]) */
    if (coopbuild::BuildForSaleKeep(1, (o != 0 && IsStandInFaction((::Faction*)o)) ? 1 : 0) != 0) return r;
    ::InterlockedIncrement64(&g_ocNotForSale);
    return false;
}

/* P18 fold 2 (P19 review), ANY THREAD (the zone loader): a town rebuild drops the zone's saved world buildings of that town that the
   local player does not own (0x9FD470, build/decomp_9fd470.txt: the instance map's head at list+0x90; each instance's 'created' short
   at node+0x88 is -1 for a world building; its state ids at node+0x98 (count) / +0xA0 (stride 0x28); the FIRST id found as a building
   state (0x23) decides - "owner faction ID" equal to the player's id is kept - and one found as 0x53 first is dropped). The engine's
   own walk is repeated BEFORE it runs: an instance whose first building state names a player stand-in record (coop-p<n> / coop-peer,
   coopbuild::BuildRebuildHold) has its 'created' set off -1 - the engine's loop then passes it untouched, as it passes every instance
   a player made - and set back to -1 after. Nothing else is changed: an NPC's, a town's and this player's are the engine's answer. */
const short kRbHoldMark = 0x7FFF;
const int kRbHoldMax = 512;
const std::string g_rbOwnerKey("owner faction ID");   /* the engine's own key (decomp_9fd470) */
typedef void* (*BdRbGetTypedFn)(void* container, const void* sid, int type);
typedef void* (*BdRbSdataFn)(void* stringFields, const std::string* key);
int BdRbHeadPod(void* list, char** head, char** first, void** source)
{
    __try { *source = *(void**)((char*)list + 0x10); *head = *(char**)((char*)list + 0x90); *first = (*head != 0) ? *(char**)*head : 0; return (*head != 0 && *first != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* one instance, and the next in the engine's own order (decomp_9fd470's in-order step; +0xA9 = the head's flag) */
int BdRbNodePod(char* node, short* created, unsigned* n, char** ids, char** next)
{
    __try
    {
        *created = *(short*)(node + 0x88); *n = *(unsigned*)(node + 0x98); *ids = *(char**)(node + 0xA0);
        char* r = *(char**)(node + 0x10);
        if (*(r + 0xA9) == 0) { while (*(*(char**)r + 0xA9) == 0) r = *(char**)r; *next = r; return 1; }
        char* c = node; char* p = *(char**)(node + 0x8);
        while (*(p + 0xA9) == 0 && c == *(char**)(p + 0x10)) { c = p; p = *(char**)(p + 0x8); }
        *next = p; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the engine's first state for one instance, in its own order: 1 = a building state found first (its owner id copied; "" when longer
   than cap), 2 = the other kind (0x53) found first, 0 = neither, -1 = fault */
int BdRbFirstStatePod(void* source, const char* ids, unsigned n, char* owner, int cap)
{
    __try
    {
        const char* base = (const char*)::GetModuleHandleA(0);
        const BdRbGetTypedFn get = (BdRbGetTypedFn)(base + kBdRbGetTypedRva);
        for (unsigned k = 0; k < n; ++k)
        {
            const char* sid = ids + (size_t)k * 0x28;
            void* st = get(source, sid, 0x23);
            if (st != 0)
            {
                const char* node = (const char*)((BdRbSdataFn)(base + kBdRbSdataRva))((char*)st + 0x138, &g_rbOwnerKey);
                if (node == 0) return -1;
                const char* v = node + 0x28;   /* the value, a std::string (VS2010: buffer at +0, size +0x10, capacity +0x18) */
                const size_t size = *(const size_t*)(v + 0x10), res = *(const size_t*)(v + 0x18);
                owner[0] = 0;
                if (size >= (size_t)cap) return 1;
                const char* t = (res >= 16) ? *(const char* const*)v : v;
                if (t == 0) return -1;
                std::memcpy(owner, t, size); owner[size] = 0;
                return 1;
            }
            if (get(source, sid, 0x53) != 0) return 2;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdRbMarkPod(char* node, short v) { __try { *(short*)(node + 0x88) = v; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
/* the walk, before the engine's: every instance it would judge (created -1, a state list) whose first building state names a stand-in */
int BdRbHold(void* list, char** held, int cap, std::string* firstOwner)
{
    char* head = 0; char* node = 0; void* source = 0;
    if (BdRbHeadPod(list, &head, &node, &source) != 1) return 0;   /* no instance, or unreadable: nothing held, the engine's own run */
    int nh = 0;
    for (int steps = 0; node != 0 && node != head && steps < 200000; ++steps)
    {
        short created = 0; unsigned n = 0; char* ids = 0; char* next = 0;
        if (BdRbNodePod(node, &created, &n, &ids, &next) != 1) { ::InterlockedIncrement64(&g_rbFaults); break; }
        if (created == -1 && n != 0 && n <= 4096 && ids != 0)
        {
            char owner[96];
            const int r = BdRbFirstStatePod(source, ids, n, owner, (int)sizeof(owner));
            if (r < 0) ::InterlockedIncrement64(&g_rbFaults);
            else if (r == 1)
            {
                ::InterlockedIncrement64(&g_rbStates);
                const std::string id(owner);
                const int slot = coopbuild::BuildRebuildOwnerSlot(coopslot::StandInSlotOfId(id), coopslot::IsLegacyPeerId(id) ? 1 : 0);
                if (coopbuild::BuildRebuildHold(1, slot) != 0)
                {
                    if (nh >= cap) ::InterlockedIncrement64(&g_rbOverflow);
                    else if (BdRbMarkPod(node, kRbHoldMark) == 1) { held[nh++] = node; if (firstOwner->empty()) *firstOwner = id; }
                    else ::InterlockedIncrement64(&g_rbFaults);
                }
            }
        }
        node = next;
    }
    return nh;
}
void detour_rebuilddrop(void* list, void* town)
{
    ::InterlockedIncrement64(&g_rbCalls);
    char* held[kRbHoldMax]; std::string firstOwner;
    const int nh = (list != 0) ? BdRbHold(list, held, kRbHoldMax, &firstOwner) : 0;
    orig_rebuilddrop(list, town);
    for (int i = 0; i < nh; ++i) if (BdRbMarkPod(held[i], -1) != 1) ::InterlockedIncrement64(&g_rbFaults);
    if (nh <= 0) return;
    ::InterlockedExchangeAdd64(&g_rbHeld, nh);
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P18 town rebuild: held %d saved building(s) another player owns (first owner '%.40s') - the engine dropped only the rest", nh, firstOwner.c_str());
    DebugLog(std::string(line));
}

/* P18, MAIN THREAD (BdRosterTick, link or not): each queued owner change, its building re-found by key (coopbuild::BuildOwnerDrainAct).
   A building still this game's player's with no row here is registered exactly as the zone scan registers an own piece (BdScanPiece,
   zoneScan 1: furniture, a door, a dismantled piece, a listed copy and a known key are refused there and counted in its zs counters): its
   PLACE + STATE are owed (g_rsOwePlace, sent when the link is up) and its pp.build row is written, so the change survives this game's reload. */
void BdOwnerChangeDrain()
{
    if (coopbuild::BuildOcDepthReset(&g_ocDepth) != 0) ++g_ocDepthReset;   /* P18 fold 1 (item 1): a stuck nesting count (a fault inside a guarded owner change) */
    if (g_ocQ.empty()) return;
    ::Faction* mine = LocalPlayerFaction();
    const int hold = (EngineWritesBlocked() || BdCopyListHold() != 0 || mine == 0) ? 1 : 0;
    size_t w = 0;
    for (size_t i = 0; i < g_ocQ.size(); ++i)
    {
        BdOcEnt e = g_ocQ[i];
        void* live = 0;
        int nowMine = 0, rowOwn = 0, rowCopy = 0;
        long long gen0 = 0;
        if (hold == 0)
        {
            gen0 = BoxWalkGen();   /* P18 fold 1 (item 6): the zone list's counter taken BEFORE the lookup it guards */
            live = ObjectByPositionKey(e.key.c_str());
            nowMine = (live != 0 && BdOwnerPod(live) == (void*)mine) ? 1 : 0;
            std::map<std::string, BdReg>::const_iterator r = g_reg.find(e.key);
            if (r != g_reg.end()) { if (r->second.own != 0 && r->second.via != 3) rowOwn = 1; else rowCopy = 1; }
        }
        const int act = coopbuild::BuildOwnerDrainAct(hold, e.tries, kOcMaxTries, live != 0 ? 1 : 0, nowMine, rowOwn, rowCopy);
        if (act == coopbuild::kBuildOdWait) { if (hold == 0) ++e.tries; g_ocQ[w++] = e; continue; }
        const char* what = "GAVE UP: the building did not resolve by its key";
        if (act == coopbuild::kBuildOdScan)
        {
            const int reg = BdScanPiece(live, mine, gen0, 1);   /* P18 fold 1 (item 6) */
            if (reg < 0 && e.tries + 1 < kOcMaxTries) { ++e.tries; ++g_ocWalkMoved; g_ocQ[w++] = e; continue; }
            if (reg > 0) { ++g_ocRegistered; what = "registered as this game's own building - its PLACE + STATE owed; the other game adopts its copy as this player's"; }
            else { ++g_ocNotTaken; what = "NOT registered (furniture, a door, dismantled, no position, a listed copy, a full registry or a moved walk - see the zs counters)"; }
        }
        else if (act == coopbuild::kBuildOdKnown) { ++g_ocKnown; what = "already this game's own piece - its own road carries it"; }
        else if (act == coopbuild::kBuildOdCopyRow) { ++g_ocCopyRow; what = "REFUSED: the key is another game's piece (a copy row here) - never claimed"; }
        else if (act == coopbuild::kBuildOdNotMine) { ++g_ocNotMineNow; what = "no longer this game's player's at the tick - nothing sent"; }
        else ++g_ocGaveUp;
        char line[520];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P18 owner change key=%.63s '%.60s' -> this game's player: %s", e.key.c_str(), e.before.c_str(), what);
        BdLogOc(line);
    }
    g_ocQ.resize(w);
}

/* T-274 (t274-zone-scan.md S3), MAIN THREAD (BdRosterTick, every tick, link or not). The load scan runs once when the world first
   runs - before the later zones load - so an own piece with no pp.build record (every piece of a single-player save brought into
   multiplayer) in a zone that finished loading after it was never registered, and so never shared. This polls the engine's own
   per-zone "loaded" byte (ZoneMap+0xB1, ZoneLoadedSectorTri: set only after the zone's saved buildings were created) of every active
   zone each tick and walks ONE newly loaded, not-yet-walked zone per tick (LoadedBuildingsInSector + BdScanPiece): its unknown own
   pieces are registered and owed their PLACE + STATE (g_rsOwePlace). A zone that leaves the active list loses its mark (a re-arrival
   is walked again; its known keys are only counted). A walk that moved (BoxWalkGen) leaves the zone unmarked: walked again on its turn.
   T-274 fold (LOW-2): the pick is round-robin (from the sector after the one picked last), and a zone whose walk moved
   kBuildZoneScanGiveUp times in a row is marked anyway (zoneScanGaveUp). LOW-3: a full 64-entry active list is counted (zoneScanListFull).
   The mark rules are coopbuild::BuildZoneScan* (offline tests). */
void BdZoneScanTick()
{
    if (EngineWritesBlocked()) return;
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return;
    if (BdCopyListHold() != 0) return;   /* P87 rf1 (LOW-MED): a listed copy must not be registered as own - wait for this world's list */
    static int sx[64], sy[64], ld[64];
    const int n = ActiveZoneSectors(sx, sy, 64);
    if (n < 0) { ++g_zsRefused; return; }   /* the zone list would not read: nothing cleared, asked again next tick */
    if (n == 64) ++g_zsListFull;   /* T-274 fold (LOW-3): the list filled its 64 entries - more active zones may have been cut */
    coopbuild::BuildZoneScanClearGone(g_bdZoneScanned, g_bdZoneMoves, g_bdZoneActiveNow, sx, sy, n);
    for (int i = 0; i < n; ++i)
    {
        const int c = coopbuild::BuildZoneSectorIndex(sx[i], sy[i]);
        ld[i] = (c >= 0 && g_bdZoneScanned[c] == 0) ? ZoneLoadedSectorTri(sx[i], sy[i]) : 0;   /* only an unmarked zone's byte is read */
        if (ld[i] < 0) ++g_zsRefused;
    }
    const int pick = coopbuild::BuildZoneScanPick(g_bdZoneScanned, sx, sy, ld, n, g_bdZoneLastPick);
    if (pick < 0) return;
    g_bdZoneLastPick = coopbuild::BuildZoneSectorIndex(sx[pick], sy[pick]);   /* T-274 fold (LOW-2): the next pick starts after it */
    static void* list[4096];
    int trunc = 0;
    const long long gen0 = BoxWalkGen();   /* taken before the list, as BdScanLoaded does */
    const int m = LoadedBuildingsInSector(sx[pick], sy[pick], list, 4096, &trunc);
    if (m < 0) { ++g_zsRefused; return; }
    const long long f0 = g_zsFound, r0 = g_zsRegistered, k0 = g_zsKnown, u0 = g_zsFurn, i0 = g_zsInner, s0 = g_zsSkipped;
    int moved = 0;
    for (int i = 0; i < m && moved == 0; ++i)
        if (BdScanPiece(list[i], mine, gen0, 1) < 0) moved = 1;
    if (coopbuild::BuildZoneScanDone(g_bdZoneScanned, g_bdZoneMoves, sx[pick], sy[pick], moved) != 0)
    {
        ++g_zsGaveUp;   /* T-274 fold (LOW-2): marked after kBuildZoneScanGiveUp moved walks in a row */
        if (g_zsLines < kZoneScanLineCap)
        {
            ++g_zsLines;
            char gl[200];
            _snprintf_s(gl, sizeof(gl), _TRUNCATE, "[BUILD] ZONESCAN sector=%d.%d gave up: its walk moved %d times in a row - marked until it leaves the active list",
                         sx[pick], sy[pick], coopbuild::kBuildZoneScanGiveUp);
            DebugLog(std::string(gl));
        }
        else ++g_zsSuppressed;
    }
    if (moved != 0) return;   /* counted in g_zsWalkMoved; walked again on its turn (round-robin) unless it was given up */
    ++g_zsZones;
    if (!g_tomb.empty()) g_tombVerifyOwed = 1;   /* P87 fold 1 (H1): a read-back tombstone in this zone may be verified now */
    g_tombVerifyZone = g_bdZoneLastPick;   /* P87 fold 2 (LOW 3): its tombstones are looked up first */
    if (!g_recon.empty()) BdReconRearm();   /* P87: a zone finished loading - the parked reconciles look again */
    if (trunc != 0) ++g_zsTruncated;
    if (g_zsLines >= kZoneScanLineCap) { ++g_zsSuppressed; return; }
    ++g_zsLines;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ZONESCAN sector=%d.%d found=%lld registered=%lld known=%lld furn=%lld interior=%lld skipped=%lld truncated=%d",
                 sx[pick], sy[pick], g_zsFound - f0, g_zsRegistered - r0, g_zsKnown - k0, g_zsFurn - u0, g_zsInner - i0, g_zsSkipped - s0, trunc);
    DebugLog(std::string(line));
}

/* P87 fold 1 (H1, H1b), MAIN THREAD (BdTombQueue): verifying one READ-BACK tombstone - the zone holding the key (furniture: its host's)
   was walked by the zone scan in this world, then the live lookup the receiver uses (ObjectByPositionKey; furniture: BdFindInHost in
   its host) answers. 1 = verified absent, 0 = not yet, -1 = an own piece stands at the key. */
/* P87 fold 2 (LOW 3): the zone (sector index) a tombstone is verified in - its key's; furniture: its host's. -1 = none */
int BdTombZone(const std::string& key, const BdTombInfo& ti, int* sxOut, int* syOut)
{
    const std::string& zk = (ti.furn != 0) ? ti.hostKey : key;
    int sx = -1, sy = -1;
    const int c = (!zk.empty() && BoxKeySector(zk.c_str(), &sx, &sy) != 0) ? coopbuild::BuildZoneSectorIndex(sx, sy) : -1;
    if (sxOut != 0) *sxOut = sx;
    if (syOut != 0) *syOut = sy;
    return c;
}
/* P87 fold 2 (MED), MAIN THREAD: the furniture branch's second look. BdFindInHost compares keys exactly, but a layout piece's key can
   drift across a reload - an own keyed piece inside the host within ItBoxResolve's window of the tombstone key (BoxKeyNearKey) is
   taken as the owner's piece. 1 = an own piece, not dismantled, stands there; 2 = only an own dismantling one; 3 = none found but the
   list was not complete (a building not resolved or not keyed, a truncated or torn list); 0 = a complete list, none */
int BdTombNearOwnInHost(void* host, const std::string& key, void** found = 0)   /* P14: *found = the own piece (result 1) */
{
    int listed = -1, trunc = 0, torn = 0;
    const int n = InteriorPieceIds(host, g_bdIds, kBdInteriorCap, &listed, &trunc, &torn);
    void* mine = (void*)LocalPlayerFaction();
    int typed = 0, checked = 0, dismNear = 0;
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = g_bdIds + (size_t)i * 5;
        if (f[0] != 0 || (f[3] == 0 && f[4] == 0)) continue;   /* BUILDING (0) only, as BdFindInHost */
        ++typed;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = (void*)h.asBuilding();
        unsigned int ids[5];
        if (b == 0 || ObjectHandIds(b, ids) == 0 || std::memcmp(ids, f, sizeof(ids)) != 0) continue;
        void* o = BdOwnerPod(b);
        if (mine == 0 || o != mine) { ++checked; continue; }
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) continue;
        ++checked;
        if (key != k && BoxKeyNearKey(k, key.c_str()) == 0) continue;
        if (BdByteAtOr0(b, kBOwnDismantled) == 0) { if (found != 0) *found = b; return 1; }   /* P14 */
        dismNear = 1;
    }
    if (dismNear != 0) return 2;
    return (mine != 0 && listed >= 0 && trunc == 0 && torn == 0 && n == listed && checked == typed) ? 0 : 3;
}
/* P87 fold 2: mayLook 0 = no live lookup allowed (this call's cap spent) - a tombstone that would be looked up is left for later
   (*looked = -1); *looked = 1 when a lookup was made. LOW 1: the zone must be loaded NOW (ZoneLoadedSectorTri, the engine's own
   ZoneMap+0xB1 byte) at the lookup, not only walked once in this world. MED: furniture - an own piece within ItBoxResolve's window of
   the key (BdTombNearOwnInHost) counts as found. */
int BdTombVerify(const std::string& key, const BdTombInfo& ti, int mayLook, int* looked)
{
    int sx = -1, sy = -1;
    const int c = BdTombZone(key, ti, &sx, &sy);
    int walked = (c >= 0 && g_bdZoneScanned[c] != 0) ? 1 : 0;
    int hostFound = 1, look = 2, own = 0, dism = 0;
    if (looked != 0) *looked = 0;
    if (ti.tail != 0 && walked != 0 && !EngineWritesBlocked())
    {
        if (ZoneLoadedSectorTri(sx, sy) != 1) { walked = 0; ++g_tombNotLoaded; }   /* LOW 1: not loaded now - not yet */
        else if (mayLook == 0) { walked = 0; ++g_tombVerifyDeferred; if (looked != 0) *looked = -1; }   /* LOW 3: left for the next call */
        else
        {
            if (looked != 0) *looked = 1;
            void* b = 0;
            void* host = 0;
            int v = kObjKeyRefused;
            if (ti.furn == 0)
            {
                b = ObjectByPositionKey(key.c_str());
                v = ObjectByPositionKeyVerdict();
            }
            else
            {
                host = ObjectByPositionKey(ti.hostKey.c_str());
                hostFound = (host != 0 && ObjectByPositionKeyVerdict() == kObjKeyFound) ? 1 : 0;
                if (hostFound != 0) b = BdFindInHost(host, key.c_str(), "", &v);
            }
            look = (b != 0 && v == kObjKeyFound) ? 1 : (v == kObjKeyNone ? 0 : 2);
            if (look == 1)
            {
                void* o = BdOwnerPod(b);
                own = (o != 0 && o == (void*)LocalPlayerFaction()) ? 1 : 0;
                dism = (BdByteAtOr0(b, kBOwnDismantled) != 0) ? 1 : 0;
            }
            if (ti.furn != 0 && hostFound != 0 && !(look == 1 && own != 0 && dism == 0))   /* MED: not already an own live piece */
            {
                const int nearOwn = BdTombNearOwnInHost(host, key);
                const int wouldVerify = (look == 0 || (look == 1 && own == 0)) ? 1 : 0;
                if (nearOwn == 1) { look = 1; own = 1; dism = 0; ++g_tombNearHeld; }   /* the owner still has it: dropped */
                else if (nearOwn == 2 && wouldVerify != 0) { look = 1; own = 1; dism = 1; ++g_tombNearHeld; }   /* being dismantled: held */
                else if (nearOwn == 3 && wouldVerify != 0) look = 2;   /* an incomplete list: held */
            }
        }
    }
    return coopbuild::BuildTombVerifyVerdict(ti.tail, ti.furn, walked, hostFound, look, own, dism);
}
/* P87, MAIN THREAD (BdRosterTick, this world scanned): every tombstone is owed its REMOVE again, ahead of any PLACE. A key this game
   holds a live own piece at again - or a PLACE for it waits here - is dropped instead (the owner's list wins: never a REMOVE for a
   piece it still lists). P87 fold 1 (H1): a READ-BACK tombstone is sent only once verified (BdTombVerify), else held in pp.build.
   verifyOnly (a zone was walked): only the read-back tombstones verified by this call are queued - the others wait for a round. */
void BdTombQueue(const char* why, int verifyOnly)
{
    if (verifyOnly == 0) g_tombSendOwed = 0;
    g_tombVerifyOwed = 0;
    unsigned int q = 0, d = 0, h = 0, v = 0;
    /* P87 fold 2 (LOW 3): the verification lookups first, at most kTombVerifyPerCall live ones - the tombstones of the zone walked last
       first (pass 0), then the others round-robin from g_tombVerifyCursor (pass 1). A lookup left for later owes this call again (the
       next tick). fresh: verified by this call; seen: an own piece stands at the key (dropped below) */
    std::set<std::string> fresh, seen;
    {
        int budget = kTombVerifyPerCall, deferred = 0;
        const size_t nt = g_tomb.size();
        for (int pass = 0; pass < 2; ++pass)
        {
            for (size_t j = 0; j < nt; ++j)
            {
                const size_t i = (pass == 0) ? j : (g_tombVerifyCursor + j) % nt;
                const std::string key = g_tomb[i];
                BdTombInfo& ti = g_tombInfo[key];   /* never missing (one per key); a missing one reads as an unverifiable read-back */
                if (ti.readBack == 0 || ti.verified != 0) continue;
                const int inZone = (g_tombVerifyZone >= 0 && BdTombZone(key, ti, 0, 0) == g_tombVerifyZone) ? 1 : 0;
                if ((pass == 0 ? 1 : 0) != inZone) continue;
                if (BdOwnLiveRow(key) != 0 || BdPlacePending(key) != 0) continue;   /* dropped below, no lookup */
                if (g_tombKill.find(key) != g_tombKill.end()) continue;   /* P14: its removal is queued - held below, no lookup */
                int looked = 0;
                const int vv = BdTombVerify(key, ti, budget > 0 ? 1 : 0, &looked);
                if (looked > 0) { --budget; if (pass != 0) g_tombVerifyCursor = (i + 1) % nt; }
                else if (looked < 0) deferred = 1;
                if (vv < 0) { if (BdTombSeenOwn(key, "tombstone check") == 0) seen.insert(key); }   /* P14: a read-back one - the piece is removed, not the tombstone */
                else if (vv > 0) { ti.verified = 1; fresh.insert(key); ++v; ++g_tombVerified; }
            }
        }
        if (deferred != 0) g_tombVerifyOwed = 1;
    }
    for (size_t i = 0; i < g_tomb.size(); )
    {
        const std::string key = g_tomb[i];
        BdTombInfo& ti = g_tombInfo[key];   /* never missing (one per key); a missing one reads as an unverifiable read-back */
        const int wasVerified = (ti.verified != 0 && fresh.count(key) == 0) ? 1 : 0;   /* P87 fold 2: as before this call's lookups */
        int ownHere = BdOwnLiveRow(key);
        const int pend = BdPlacePending(key);
        if (seen.count(key) != 0) ownHere = 1;   /* P87 fold 2: the lookup above found an own piece at the key */
        const int act = coopbuild::BuildTombSendAct(ti.readBack, ti.verified, ownHere, pend);
        if (act != coopbuild::kBuildTombDrop && g_tombKill.find(key) != g_tombKill.end()) { ++i; ++h; continue; }   /* P14: the removal first - held */
        if (act == coopbuild::kBuildTombDrop)
        {
            g_tombRetire.push_back(coopbuild::BuildTombRowKey(key));
            g_tomb.erase(g_tomb.begin() + (std::ptrdiff_t)i);
            g_tombInfo.erase(key);
            g_tombKill.erase(key);   /* P14: a new placement at the key - nothing to remove */
            if (coopbuild::BuildOweRemoveDrop(&g_dmOweRemove, key) != 0) ++g_tombUnowed;   /* P87 fold 1: a REMOVE queued earlier never goes */
            ++d; ++g_tombDropListed;
            BdOwnDirty(2);
            continue;
        }
        ++i;
        if (act == coopbuild::kBuildTombHold) { ++h; continue; }
        if (verifyOnly != 0 && !(ti.readBack != 0 && wasVerified == 0)) continue;   /* a zone walk queues only the newly verified */
        if (coopbuild::BuildOweRemoveAdd(&g_dmOweRemove, key) != 0) ++q;
    }
    g_tombQueued += q;
    g_tombHeld = (long long)h;
    if (q == 0 && d == 0 && v == 0) return;
    char line[360];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 tombstones: %u REMOVEs owed again (%s), %u dropped (a live own piece or a waiting PLACE at the key), %u read-back verified absent, %u held (not verified yet); tombstones=%u",
                q, why, d, v, h, (unsigned int)g_tomb.size());
    BdLogP87(line);
}
/* P87, MAIN THREAD (BuildRestoreQueue): a tombstone row read back from pp.build. A key whose own PLACE row is being restored, or that
   has a live own row, is no tombstone any more (its row is dropped). P87 fold 1: a READ-BACK tombstone - held until verified. */
void BdTombLoaded(const coopbuild::BuildMsg& m, const std::string& ledBytes)   /* P14 fold 1: + the row's STATE bytes (the escape ledger) */
{
    const std::string& key = m.key;
    char line[360];
    if (BdOwnLiveRow(key) != 0 || BdPlacePending(key) != 0)
    {
        g_tombRetire.push_back(coopbuild::BuildTombRowKey(key));
        ++g_tombDropListed;
        BdOwnDirty(2);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 tombstone key=%.63s read back and dropped: this game holds (or restores) its own piece there", key.c_str());
        BdLogP87(line);
        return;
    }
    const int isNew = (g_tombInfo.find(key) == g_tombInfo.end()) ? 1 : 0;
    BdTombAddKey(key, &g_tombLoaded);
    if (isNew != 0)
    {
        BdTombInfo& ti = g_tombInfo[key];
        ti.readBack = 1; ti.verified = 0; ti.sent = 1; ti.nonce = m.nonce;
        ti.tail = ((m.rmFlags & coopbuild::kBuildRmTombTail) != 0) ? 1 : 0;
        ti.furn = ((m.rmFlags & coopbuild::kBuildRmFurniture) != 0) ? 1 : 0;
        ti.hostKey = (ti.furn != 0) ? m.hostKey : std::string();
        if (!ledBytes.empty() && coopbuild::BuildLedgerDecode(ledBytes, &ti.led)) ++g_ledRead;   /* P14 fold 1: a row before fold 1 holds an empty STATE - no ledger */
    }
    g_tombSendOwed = 1;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 tombstone key=%.63s read back from pp.build (nonce=%08x%s) - its REMOVE goes once the zone holding the key was walked here with no own piece there; tombstonesLoaded=%lld",
                key.c_str(), m.nonce, (m.rmFlags & coopbuild::kBuildRmTombTail) == 0 ? ", written before fold 1: never verified, kept" :
                ((m.rmFlags & coopbuild::kBuildRmFurniture) != 0 ? ", furniture" : ""), g_tombLoaded);
    BdLogP87(line);
}
/* P14 fold 1 (F5): the piece stays - its zone is walked again and the post-load scan is owed, so the scan registers it */
void BdP14Rescan(const std::string& key, const BdTombInfo& ti)
{
    const int c = BdTombZone(key, ti, 0, 0);
    if (c >= 0) g_bdZoneScanned[c] = 0;
    g_scanOwed = 1;
    ++g_p14Rescans;
}
/* P14 fold 1: a cancelled removal - an own piece at the key ends the tombstone as before P14 (never queued again in this world) */
void BdP14Release(const std::string& key, const BdTombInfo& ti)
{
    g_tombKillNo.insert(key);
    BdP14Rescan(key, ti);
}
/* P14 fold 1: POD - the engine's material list: per slot its GameData* (+0x0, the record 0x29DBD0 makes the refund's items from) and the
   address of its delivered amount (+0xC). -1 unreadable, else the slots filled (at most cap) */
int BdMatSlotsPod(void* b, void** gd, float** dl, int cap)
{
    __try
    {
        const char* st = ((BdStateFn)((*(void***)b)[kVtState / 8]))(b);
        if (st == 0) return -1;
        const unsigned int n = *(const unsigned int*)(st + kStMatCount);
        char* const* arr = *(char* const* const*)(st + kStMatArray);
        if (n > kStMatSane || (n > 0 && arr == 0)) return -1;
        int k = 0;
        for (unsigned int i = 0; i < n && k < cap; ++i)
        {
            if (arr[i] == 0) continue;
            gd[k] = *(void* const*)arr[i];
            dl[k] = (float*)(arr[i] + kMatDelivered);
            ++k;
        }
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdFloatGetPod(const float* p, float* out) { __try { *out = *p; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
int BdFloatSetPod(float* p, float v) { __try { *p = v; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
/* P14 fold 1 (manager decision 2026-09-30), K2 SAFE POINT (BdTombKillDrain, before the engine's dismantle): each material slot keeps
   coopbuild::BuildLedgerKeep of its delivered amount - the first drop's quantity (the ledger's gen 0 entries of that material) less the
   escapes that count: our pickup when this load's overlay APPLIED that squad record at or past the entry's seq (OwnOverlayAppliedSeq),
   the other player's always. -1 unreadable, else the slot count. logIt: the ledger line (the first call). */
int BdMatsLedgerApply(void* b, const std::string& key, coopbuild::BuildLedger* led, int logIt, float* before, std::vector<float>* saved)
{
    *before = 0.0f;
    void* gd[64];
    float* dl[64];
    const int n = BdMatSlotsPod(b, gd, dl, 64);
    if (n < 0) return -1;
    std::vector<int> ok(led->esc.size(), 0);
    int cOwn = 0, cPeer = 0, cHolder = 0, nNot = 0, nRefused = 0;
    for (size_t i = 0; i < led->esc.size(); ++i)
    {
        const int peer = (led->esc[i].kind == coopbuild::kBuildLedgerEscPeer) ? 1 : 0;
        const int holder = (led->esc[i].kind == coopbuild::kBuildLedgerEscHolder) ? 1 : 0;   /* P14 fold 5: names no squad record */
        ok[i] = coopbuild::BuildLedgerEscCounts(led->esc[i], (peer != 0 || holder != 0) ? 0 : OwnOverlayAppliedSeq(led->esc[i].rec));
        if (ok[i] == 0) ++nNot; else if (holder != 0) ++cHolder; else if (peer != 0) ++cPeer; else ++cOwn;
    }
    if (saved != 0) saved->assign((size_t)n, -1.0f);
    std::string detail;
    float kept = 0.0f;
    for (int s = 0; s < n; ++s)
    {
        float d = 0.0f;
        if (BdFloatGetPod(dl[s], &d) == 0) continue;
        if (saved != 0) (*saved)[(size_t)s] = d;
        if (d > 0.0f) *before += d;
        char sid[160];
        sid[0] = 0;
        const int hasSid = (gd[s] != 0 && GameDataSidPod(gd[s], sid, (int)sizeof sid) == 1) ? 1 : 0;
        if (hasSid == 0 && logIt != 0) ++g_ledNoSid;
        int owed = -1, esc = 0, refused = 1;
        /* P14 fold 2 (D1): a material nothing in the ledger names (no sid, a tombstone before fold 1, a refund never seen) or a BLIND one
           (a pickup of it could not be recorded) is refused - 0: a loss of its refund, never a duplicate of an unrecorded pickup */
        const float keep = (hasSid != 0) ? coopbuild::BuildLedgerKeepOf(*led, ok, std::string(sid), d, &owed, &esc, &refused) : (d > 0.0f ? 0.0f : d);
        if (refused != 0 && d > 0.0f) ++nRefused;
        if (keep != d) BdFloatSetPod(dl[s], keep);
        if (keep > 0.0f) kept += keep;
        if (logIt != 0 && detail.size() < 280)
        {
            char p[200];
            _snprintf_s(p, sizeof(p), _TRUNCATE, " %.40s:%.1f->%.1f(owed=%d,escaped=%d%s)", hasSid != 0 ? sid : "?", d, keep, owed, esc, refused != 0 ? ",refused" : "");
            detail += p;
        }
    }
    if (logIt != 0)
    {
        g_ledCounted += cOwn; g_ledPeerCounted += cPeer; g_ledNotCounted += nNot; g_ledRefused += nRefused; g_ledHolderCounted += cHolder;
        const int pruned = coopbuild::BuildLedgerPruneUncounted(led, ok);   /* P14 fold 2 (D2): never counted at a later reload */
        if (pruned > 0) { g_ledPruned += pruned; BdOwnDirty(2); }
        char line[900];   /* P14 fold 5: room for the holder figure */
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: refund entries=%u missed=%d spill=%d; escapes counted own=%d other-player=%d (inexact if both games went"
                    " down within ~2 s of it) not counted=%d; delivered %.1f -> %.1f:%s; pruned=%d refused=%d; escapes counted holder=%d (handed to the area's holder: on its ground)", key.c_str(), (unsigned int)led->refund.size(), led->missed, led->spill,
                    cOwn, cPeer, nNot, *before, kept, detail.c_str(), pruned, nRefused, cHolder);
        BdLogP14(line);
    }
    return n;
}
/* P14 fold 2 (D7): a removal that ends with the piece standing (a faulted call, a give-up) - 0 once a call's refund fired (the refund
   went once); P14 fold 3 (D7): else the amounts the removal set stay (the ledger-reduced ones - never its full delivered mats back: its
   tombstone and ledger go, and a later hand dismantle would refund the first drop's picked-up items again); a wall run's shared state
   was never changed */
void BdP14MatsBack(void* b, int found, const BdTombKill& kl)
{
    if (b == 0 || found == 0 || kl.tries == 0) return;
    const int act = coopbuild::BuildTombKillRestore(kl.wall, kl.refunded);
    if (act == coopbuild::kBuildTombMatsZero) { float s = 0.0f; if (BdMatsSumZero(b, 1, &s) >= 0) ++g_p14MatsZeroBack; }
    else if (kl.wall == 0) ++g_p14MatsLeft;
}
/* P14 (mmo8b, mmo8-buildings-read.md 4b), K2 SAFE POINT (BuildCopyDrain; link or not - no stand-in needed): the queued removals. The
   key is looked up again every time (never a kept pointer): free-standing by ObjectByPositionKey, furniture in its host (the exact key,
   then ItBoxResolve's window - BdTombNearOwnInHost). Only while the key's zone is loaded NOW (ZoneMap+0xB1), only THIS game's own piece
   (never a copy-list key, never another owner's), and FIRST its delivered mats go to 0 so the engine's refund drops nothing (the records
   already hold that refund); then the engine's own dismantle (vt+0x248), as BdRemoveOne does for a copy (a wall run's shared state is
   not zeroed: 0x29DBD0 never refunds it). The tombstone stays: once the piece is gone its REMOVE reaches the other game through the P87
   verification (g_tombVerifyOwed). Given up (kRemoveTries): the save's piece stays and the tombstone ends, as before P14. No __try (C2712). */
void BdTombKillDrain()
{
    if (g_tombKill.empty() || EngineWritesBlocked()) return;
    if (BdCopyListHold() != 0) return;   /* the copy list says which pieces are another game's copies - wait for this world's list */
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return;
    std::vector<std::string> due;
    for (std::map<std::string, BdTombKill>::const_iterator it = g_tombKill.begin(); it != g_tombKill.end(); ++it)
        if (it->second.nextTry <= g_drainNo) due.push_back(it->first);
    int looked = 0;
    char line[600];
    for (size_t k = 0; k < due.size() && looked < kReconPerDrain; ++k)
    {
        const std::string key = due[k];
        std::map<std::string, BdTombKill>::iterator it = g_tombKill.find(key);
        if (it == g_tombKill.end()) continue;
        std::map<std::string, BdTombInfo>::const_iterator ti = g_tombInfo.find(key);
        if (ti == g_tombInfo.end() || BdOwnLiveRow(key) != 0 || BdPlacePending(key) != 0)
        {
            g_tombKill.erase(it);
            ++g_p14Cancelled;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s removal cancelled: %s; p14Cancelled=%lld", key.c_str(),
                        ti == g_tombInfo.end() ? "the tombstone ended" : "a live own row or a waiting PLACE holds the key (a new placement)", g_p14Cancelled);
            BdLogP14(line);
            continue;
        }
        ++looked;
        const BdTombInfo tinfo = ti->second;
        BdTombKill& kl = it->second;
        int sx = -1, sy = -1;
        const int zoneLoaded = (BdTombZone(key, tinfo, &sx, &sy) >= 0 && ZoneLoadedSectorTri(sx, sy) == 1) ? 1 : 0;
        void* b = 0;
        int look = 2, own = 0, listed = 0, dism = 0, exact = 0, named = 0, inner = 0;   /* P14 fold 1: + the found piece's key / row / interior */
        std::string foundKey;
        if (zoneLoaded != 0)
        {
            if (tinfo.furn == 0)
            {
                b = ObjectByPositionKey(key.c_str());
                const int v = ObjectByPositionKeyVerdict();
                look = (b != 0 && v == kObjKeyFound) ? 1 : (v == kObjKeyNone ? 0 : 2);
            }
            else
            {
                void* host = ObjectByPositionKey(tinfo.hostKey.c_str());
                const int hv = ObjectByPositionKeyVerdict();
                if (host != 0 && hv == kObjKeyFound)
                {
                    int v = kObjKeyRefused;
                    b = BdFindInHost(host, key.c_str(), "", &v);
                    look = (b != 0 && v == kObjKeyFound) ? 1 : (v == kObjKeyNone ? 0 : 2);
                    if (look != 1)
                    {   /* a layout key can drift across a reload (P87 fold 2): an own piece within ItBoxResolve's window is the piece */
                        void* nb = 0;
                        const int nr = BdTombNearOwnInHost(host, key, &nb);
                        if (nr == 1 && nb != 0) { b = nb; look = 1; }
                        else if (nr == 2 || nr == 3) look = 2;   /* one being dismantled, or an incomplete list: not settled */
                    }
                }
                else look = (hv == kObjKeyNone) ? 0 : 2;   /* no host at all: its furniture is gone with it */
            }
            if (look == 1)
            {
                own = (BdOwnerPod(b) == (void*)mine) ? 1 : 0;
                std::string lk;
                listed = (BdCopyListFind(key, &lk) != 0) ? 1 : 0;
                float p = 0, nd = 0;
                int c = 0, d = 0, x = 0;
                if (!BdReadState(b, &p, &nd, &c, &d, &x)) look = 2;
                else dism = BdDismOf(b, d);
                char fk[kKeyCap];
                fk[0] = 0;
                if (look == 1 && ObjectPositionKey(b, fk, kKeyCap, "", 3, 1) != 0)
                {   /* P14 fold 1 (F2): the FOUND piece's own key - the lookup's window (0.1 u) can answer a rebuilt piece */
                    foundKey = fk;
                    exact = (foundKey == key) ? 1 : 0;
                    std::map<std::string, BdReg>::const_iterator rn = g_reg.find(foundKey);
                    named = (rn != g_reg.end() && rn->second.removed == 0 && rn->second.dismantled == 0) ? 1 : 0;
                }
                else if (look == 1) look = 2;   /* its key unreadable: not settled */
                inner = (look == 1 && BdPtrAt(b, kBInterior) != 0) ? 1 : 0;   /* F4 */
            }
        }
        const int step = coopbuild::BuildTombKillStep(zoneLoaded, look, own, listed, dism, kl.tries, kl.misses, kRemoveTries, exact, named, inner);
        if (step == coopbuild::kBuildTombKillWait)
        {
            if (zoneLoaded != 0 && look == 1 && dism != 0 && kl.tries > 0)
            {   /* P14 fold 1 (F3): our call took - the engine frees the piece next frame, or a wall's deferred dismantle runs: waited, never a miss */
                ++g_p14Waits;
                if (kl.waitLogged == 0)
                {
                    kl.waitLogged = 1;
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: dismantled by the call (try %d) - waiting for the engine to free it (a wall defers); p14Waits=%lld",
                                key.c_str(), kl.tries, g_p14Waits);
                    BdLogP14(line);
                }
            }
            kl.nextTry = g_drainNo + kRetryGap;
            continue;
        }
        if (step == coopbuild::kBuildTombKillMiss) { ++kl.misses; kl.nextTry = g_drainNo + kRetryGap; continue; }
        if (step == coopbuild::kBuildTombKillDone)
        {
            if (kl.tries > 0) ++g_p14Removed; else ++g_p14Gone;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: %s (calls=%d matsBefore=%.1f) - the tombstone stays; its REMOVE goes to the"
                        " other game once verified absent; p14Removed=%lld p14Gone=%lld", key.c_str(),
                        kl.tries > 0 ? "the save's piece is GONE - removed here, its refund dropped again less the escape ledger" : "no piece stands at the key any more (nothing removed here)",
                        kl.tries, kl.matsBefore, g_p14Removed, g_p14Gone);
            BdLogP14(line);
            g_tombKill.erase(it);
            g_tombVerifyOwed = 1;   /* P87: the read-back tombstone is looked at again - verified absent, its REMOVE is owed */
            continue;
        }
        if (step == coopbuild::kBuildTombKillCancel)
        {
            ++g_p14Cancelled;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s removal cancelled: the piece at the key is %s - not touched; p14Cancelled=%lld",
                        key.c_str(), listed != 0 ? "in this game's copy list (another game's copy) - the tombstone is dropped" : "not this game's own", g_p14Cancelled);
            BdLogP14(line);
            g_tombKill.erase(it);
            if (listed != 0) BdTombDrop(key, "P14: the piece at the key is another game's listed copy", 1);   /* P14 fold 1 (F7) */
            else BdP14Release(key, tinfo);
            continue;
        }
        if (step == coopbuild::kBuildTombKillMismatch)
        {   /* P14 fold 1 (F2) */
            ++g_p14Cancelled; ++g_p14KeyMismatch;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s removal cancelled: the piece the lookup found has key %.63s%s - not the retired piece, not touched;"
                        " the tombstone ends as before P14; p14KeyMismatch=%lld", key.c_str(), foundKey.c_str(), named != 0 ? " (a registry row names it)" : "", g_p14KeyMismatch);
            BdLogP14(line);
            g_tombKill.erase(it);
            BdP14Release(key, tinfo);
            continue;
        }
        if (step == coopbuild::kBuildTombKillExclude)
        {   /* P14 fold 1 (F4): its interior's pieces and contents are outside this fix - left standing, counted (leftover row) */
            ++g_p14Interior;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s NOT removed: the save's piece has an interior - left standing as before P14 (leftover); p14Interior=%lld",
                        key.c_str(), g_p14Interior);
            BdLogP14(line);
            g_tombKill.erase(it);
            BdTombDrop(key, "P14: the save's piece has an interior - left standing", 1);
            BdP14Rescan(key, tinfo);
            continue;
        }
        if (step == coopbuild::kBuildTombKillGiveUp)
        {
            ++g_p14GaveUp;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s removal GIVEN UP after %d calls and %d unsettled looks (last look=%d dismantled=%d)"
                        " - the save's piece stays and the tombstone ends (as before P14); p14GaveUp=%lld", key.c_str(), kl.tries, kl.misses, look, dism, g_p14GaveUp);
            BdLogP14(line);
            BdP14MatsBack(b, look == 1 ? 1 : 0, kl);   /* P14 fold 3 (D7): the piece stays - 0 once a refund fired, else the ledger-reduced mats stay */
            g_tombKill.erase(it);
            BdTombDrop(key, "P14 removal given up - the save's piece stays", 1);
            BdP14Rescan(key, tinfo);   /* P14 fold 1 (F5): the piece is registered by the scan again */
            continue;
        }
        /* kBuildTombKillCall - P14 fold 1 (manager decision 2026-09-30): the engine's refund drops again, less the escape ledger: each
           material slot keeps at most the first drop's quantity less the counted escapes (BdMatsLedgerApply) - a wall run's shared
           state is never refunded (0x29DBD0), only read */
        const int wall = BdIsWall(b);
        float before = 0.0f, after = 0.0f;
        const int mode = coopbuild::BuildTombKillMats(wall == 1 ? 1 : 0, kl.refunded);   /* P14 fold 2 (D4): once a call's refund fired, a retry zeroes them */
        int nz = -1;
        kl.wall = (wall == 1) ? 1 : 0;
        if (mode == coopbuild::kBuildTombMatsZero) { nz = BdMatsSumZero(b, 1, &before); ++g_p14RetryZeroed; }
        else if (mode == coopbuild::kBuildTombMatsLedger)
        {   /* the STORED ledger: the first call prunes it (D2) and saves the delivered amounts (D7) */
            std::map<std::string, BdTombInfo>::iterator tw = g_tombInfo.find(key);
            if (tw != g_tombInfo.end()) nz = BdMatsLedgerApply(b, key, &tw->second.led, kl.tries == 0 ? 1 : 0, &before, 0);
        }
        else nz = BdMatsSumZero(b, 0, &before);
        const int unr = coopbuild::BuildTombMatsUnread(mode, nz, kl.unread, kRemoveTries);
        if (unr != coopbuild::kBuildTombUnreadCall)
        {   /* P14 fold 4 (L1): the mats could not be read (or the tomb info is missing) - the call would refund the FULL delivered mats
               while earlier picked-up items are in the squad (a duplicate): never called; a later safe point looks again (bounded) */
            ++kl.unread; ++g_p14MatsUnread;
            if (unr == coopbuild::kBuildTombUnreadRetry)
            {
                if (kl.unread == 1)
                {
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: its mats could not be read (%s) - NOT dismantled (it would refund"
                                " the full mats); looked at again later; p14MatsUnread=%lld", key.c_str(),
                                (mode == coopbuild::kBuildTombMatsLedger && g_tombInfo.find(key) == g_tombInfo.end()) ? "no tomb info" : "the material slots", g_p14MatsUnread);
                    BdLogP14(line);
                }
                kl.nextTry = g_drainNo + kRetryGap;
                continue;
            }
            ++g_p14GaveUp;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s removal GIVEN UP: its mats could not be read at %d safe points - never"
                        " dismantled at full mats; the save's piece stays and the tombstone ends; p14GaveUp=%lld p14MatsUnread=%lld", key.c_str(), kl.unread, g_p14GaveUp, g_p14MatsUnread);
            BdLogP14(line);
            BdP14MatsBack(b, 1, kl);
            g_tombKill.erase(it);
            BdTombDrop(key, "P14 removal given up - its mats could not be read", 1);
            BdP14Rescan(key, tinfo);
            continue;
        }
        if (kl.tries == 0)
        {
            kl.matsBefore = before;
            if (nz >= 0 && wall != 1) ++g_p14MatsZeroed;
        }
        const long long rf0 = g_refundSeen;
        int ret = -1;
        const int ok = BdDismantlePod(b, kl.tries == 0 ? FLT_MAX : kRemoveRetryAmount, &ret);
        ++kl.tries;
        ++g_p14Calls;
        if (g_refundSeen - rf0 > 0) kl.refunded += g_refundSeen - rf0;   /* P14 fold 2 (D4): this call's refund fired */
        if (!ok)
        {   /* P14 fold 1 (F6): a faulted vt+0x248 is not called again */
            ++g_p14Faulted; ++g_p14GaveUp;
            BdP14MatsBack(b, 1, kl);   /* P14 fold 3 (D7): the piece stays - 0 once a refund fired, else the ledger-reduced mats stay */
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: vt+0x248 FAULTED (try %d) - removal GIVEN UP, the save's piece stays and the tombstone ends; p14Faulted=%lld",
                        key.c_str(), kl.tries, g_p14Faulted);
            BdLogP14(line);
            g_tombKill.erase(it);
            BdTombDrop(key, "P14 removal given up - vt+0x248 faulted", 1);
            BdP14Rescan(key, tinfo);
            continue;
        }
        float ap = 0, an = 0;
        int ac = 0, ad = 0, ax = 0;
        const int stOk = BdReadState(b, &ap, &an, &ac, &ad, &ax);   /* still alive: the kill list frees it next frame */
        if (stOk != 0) ad = BdDismOf(b, ad);
        BdMatsSumZero(b, 0, &after);
        kl.nextTry = g_drainNo + 1;   /* the next safe point looks again: freed = done */
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 tombstone key=%.63s: the save's own piece dismantled here (try %d) wall=%d mats(%d) %.1f -> %.1f"
                    " refundCalls=%lld dismantled=%d returned=%d - its refund drops again less the escape ledger", key.c_str(),
                    kl.tries, wall, nz, before, after, g_refundSeen - rf0, stOk != 0 ? ad : -1, ret);
        BdLogP14(line);
    }
}
std::string BdP14Text()
{
    char b[1400];
    long long hc[5] = { 0, 0, 0, 0, 0 };
    GroundPickHoldCounts(hc);   /* P14 fold 2 (D5/D8) */
    _snprintf_s(b, sizeof(b), _TRUNCATE, " p14[pending=%u,queued=%lld,removed=%lld,gone=%lld,cancelled=%lld,gaveUp=%lld,calls=%lld,faulted=%lld,matsLedgered=%lld,"
                "keyMismatch=%lld,interior=%lld,waits=%lld,rescans=%lld,nearPlaced=%lld,lines=%lld,suppressed=%lld,retryZeroed=%lld,matsLeft=%lld,matsZeroBack=%lld]"
                " ledger[refund=%lld,noPiece=%lld,missed=%lld,spill=%lld,escOwn=%lld,escPeer=%lld,escNoRec=%lld,escFull=%lld,escOver=%lld,writeNow=%lld,writeLate=%lld,"
                "read=%lld,unkept=%lld,counted=%lld,peerCounted=%lld,notCounted=%lld,noSid=%lld,lines=%lld,suppressed=%lld,unkeyed=%lld,matEsc=%lld,merged=%lld,blind=%lld,"
                "pruned=%lld,keptRefuse=%lld,refused=%lld,handRetry=%lld,holdExpired=%lld,holdNoSlot=%lld,holdParked=%lld,holdParkHanded=%lld,holdParkExpired=%lld,"
                "refuseAll=%lld,spillBlind=%lld,zoneUnread=%lld,matShared=%lld,matsUnread=%lld,escHolder=%lld,holderCounted=%lld]",
                (unsigned int)g_tombKill.size(), g_p14Queued, g_p14Removed, g_p14Gone, g_p14Cancelled, g_p14GaveUp, g_p14Calls, g_p14Faulted, g_p14MatsZeroed,
                g_p14KeyMismatch, g_p14Interior, g_p14Waits, g_p14Rescans, g_p14NearPlaced, g_p14Lines, g_p14Suppressed, g_p14RetryZeroed, g_p14MatsLeft, g_p14MatsZeroBack,
                g_ledRefund, g_ledRefundNoPiece, g_ledMissed, g_ledSpill, g_ledEscOwn, g_ledEscPeer, g_ledEscNoRec, g_ledEscFull, g_ledEscOver, g_ledWriteNow, g_ledWriteLate,
                g_ledRead, g_ledUnkept, g_ledCounted, g_ledPeerCounted, g_ledNotCounted, g_ledNoSid, g_ledLines, g_ledSuppressed, g_ledUnkeyed, g_ledMatEsc, g_ledEscMerged,
                g_ledBlind, g_ledPruned, g_ledKeptRefuse, g_ledRefused, g_ledHandRetry, hc[0], hc[1], hc[2], hc[3], hc[4], g_ledRefuseAll, g_ledSpillBlind,
                g_ledZoneUnread, g_ledMatShared, g_p14MatsUnread, g_ledEscHolder, g_ledHolderCounted);
    return std::string(b);
}
/* P87, MAIN THREAD (BuildNoteRecv): a REMOVE for a key with no row here - reconciled at the K2 safe point (BdReconDrain). fromSlot: the
   player whose list no longer holds the key (as BdPending::fromSlot records a sender) - a later queue of the same key names the latest. */
void BdReconQueue(const std::string& key, int fromSlot)
{
    if (key.empty()) return;
    std::map<std::string, BdRecon>::iterator it = g_recon.find(key);
    if (it != g_recon.end()) { it->second.parked = 0; it->second.nextTry = 0; it->second.tries = 0; it->second.rearms = 0; if (fromSlot != -1) it->second.fromSlot = fromSlot; return; }
    if (g_recon.size() >= kReconCap) { ++g_reconFull; return; }
    BdRecon rc;
    rc.fromSlot = fromSlot;
    g_recon[key] = rc;
    ++g_reconQueued;
}
/* P87, MAIN THREAD (BuildCopyDrain: the K2 safe point). At most kReconPerDrain lookups per safe point. Each key is judged against the
   stand-in of ITS sender; a key whose sender's slot or stand-in is not known here yet is asked again later (reconLinkUnknown) - never
   judged against another player's stand-in. */
void BdReconDrain()
{
    if (g_recon.empty()) return;
    int looked = 0;
    char line[500];
    for (std::map<std::string, BdRecon>::iterator it = g_recon.begin(); it != g_recon.end() && looked < kReconPerDrain; )
    {
        BdRecon& rc = it->second;
        if (rc.parked != 0 || rc.nextTry > g_drainNo) { ++it; continue; }
        int senderSlot = -1;
        ::Faction* const peer = BdSenderStandIn(rc.fromSlot, &senderSlot);
        if (peer == 0) { ++g_reconLinkUnknown; rc.nextTry = g_drainNo + kRetryGap; ++it; continue; }
        ++looked;
        const std::string key = it->first;
        const int listed = (g_reg.find(key) != g_reg.end() || BdPlacePending(key) != 0 || g_pendRemove.find(key) != g_pendRemove.end()
                            || g_owed.find(key) != g_owed.end()) ? 1 : 0;   /* P87 fold 1 (H2): a hand-over still owed at the key (the REMOVE named another placement) */
        void* live = 0;
        int v = kObjKeyRefused;
        if (listed == 0)
        {
            live = ObjectByPositionKey(key.c_str());
            v = ObjectByPositionKeyVerdict();
        }
        float lp = 0, ln = 0;
        int lc = 0, ld = 0, lx = 0;
        if (live != 0 && !BdReadState(live, &lp, &ln, &lc, &ld, &lx)) live = 0;
        void* o = (live != 0) ? BdOwnerPod(live) : 0;
        const int ocls = BdOwnerClassAt(live, key, (void*)peer, senderSlot, "reconcile");   /* P87 fold 3 (T651): the one owner rule - a save's stand-in of the sender's slot is the sender's; P87 root: the copy list first */
        const int mine = (ocls == coopbuild::kBuildLiveOwnMine) ? 1 : 0;
        const int sender = coopbuild::BuildOwnerIsSender(ocls);
        const int act = coopbuild::BuildReconcileAct(listed, (live != 0 && v == kObjKeyFound) ? 1 : 0, v == kObjKeyNone ? 1 : 0, mine, sender, ld);
        if (act == coopbuild::kBuildReconRetry)
        {
            if (++rc.tries < kReconTries) rc.nextTry = g_drainNo + kRetryGap;
            else { rc.parked = 1; ++g_reconParked; }   /* the next zone that finishes loading asks again */
            ++it;
            continue;
        }
        if (act == coopbuild::kBuildReconPark)
        {
            rc.parked = 1; ++g_reconParked;
            BdCopyListSettled(key, "the reconcile's complete search found no piece at the key");   /* P87 rf1 */
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 reconcile key=%.63s parked: no piece answers the key in this game's loaded zones - looked up again when a zone finishes loading (looked %d times; kept until the world is torn down)",
                        key.c_str(), rc.rearms);
            BdLogP87(line);
            ++it;
            continue;
        }
        if (act == coopbuild::kBuildReconRemove)
        {
            if ((int)g_reg.size() >= kRegCap) { ++g_regFull; rc.nextTry = g_drainNo + kRetryGap; ++it; continue; }
            char sid[160];
            sid[0] = 0;
            BdSidOf(live, sid, (int)sizeof(sid));
            const int ownFix = BdOwnerFixToSender(live, ocls, (void*)peer);   /* P87 fold 3: a save's stand-in piece takes the live stand-in, as the PLACE adopt does */
            if (ocls == coopbuild::kBuildLiveOwnSenderRecord) ++g_ownerRecordRecon;
            if (ocls == coopbuild::kBuildLiveOwnSenderList) ++g_clRecon;   /* P87 root */
            {   /* P87 rf1: a removed mark read back - the returning copy is removed */
                std::map<std::string, BdCopyEnt>::const_iterator mk = g_copyList.find(key);
                if (ocls == coopbuild::kBuildLiveOwnSenderList && mk != g_copyList.end() && mk->second.rec.removed != 0) ++g_clMarkRecon;
            }
            if (ownFix < 0) ++g_ownerFixFailed;
            BdReg& r = g_reg[key];
            r.sid = sid; r.owner = BdFactionName(BdOwnerPod(live)); r.progress = lp; r.needed = ln;
            r.complete = lc; r.dismantled = 0; r.destroyed = lx; r.via = 3; r.own = 0; r.removed = 0;
            r.hasPos = BdCopyF((const char*)live + kObjPos, r.pos, 3) ? 1 : 0;
            r.hostKey.clear(); r.liveKey.clear(); r.keySame = 1;
            r.ownerSlot = senderSlot; r.gen = g_bdGen; r.nonce = 0; r.handed = 0;
            BdRemovePend& rp = g_pendRemove[key];
            rp.reason = (int)coopbuild::kBuildReasonDismantled; rp.tries = 0; rp.waitLogged = 0; rp.deferLogged = 0; rp.nextTry = 0; rp.matsBefore = 0.0f;
            rp.missTries = 0;
            ++g_reconRemoved;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 reconcile key=%.63s sid=%.80s ownerWas=%s ownerFixed=%d REMOVED: its owner (slot %d) no longer lists it - the piece this game's save brought back is taken as its copy and removed the REMOVE way (mats zeroed: no refund); reconcileRemoved=%lld",
                        key.c_str(), sid, coopbuild::BuildOwnerWord(ocls), ownFix, r.ownerSlot, g_reconRemoved);
            BdLogP87(line);
        }
        else if (act == coopbuild::kBuildReconKeepOwn || act == coopbuild::kBuildReconKeepOther)
        {
            ++g_reconKept;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 reconcile key=%.63s KEPT: the live piece there is %s (ownerWas=%s owner='%.60s' ownerRecordSlot=%d senderSlot=%d); reconcileKept=%lld", key.c_str(),
                        act == coopbuild::kBuildReconKeepOwn ? "THIS game's own - never removed" : "another owner's, not the sender's stand-in (live or a save's)",
                        coopbuild::BuildOwnerWord(ocls), BdFactionName(o).c_str(), (o != 0) ? StandInRecordSlot((::Faction*)o) : -3, senderSlot, g_reconKept);
            BdLogP87(line);
        }
        else ++g_reconListed;   /* the owner lists the key here (a row, a pending PLACE or REMOVE): its own road */
        g_recon.erase(it++);
    }
}
std::string BdP87Text()
{
    char b[600];
    _snprintf_s(b, sizeof(b), _TRUNCATE, " p87[tombstones=%u,tombstonesPersisted=%lld,tombstonesLoaded=%lld,tombQueued=%lld,tombDropListed=%lld,tombEvicted=%lld,tombRowsEmitted=%lld,tombUnkept=%lld"
                ",reconcilePending=%u,reconcileQueued=%lld,reconcileRemoved=%lld,reconcileKept=%lld,reconcileListed=%lld,reconcileParked=%lld,reconcileFull=%lld,lines=%lld,suppressed=%lld]",
                (unsigned int)g_tomb.size(), g_tombPersisted, g_tombLoaded, g_tombQueued, g_tombDropListed, g_tombEvicted, g_tombRowsEmitted, g_tombUnkept,
                (unsigned int)g_recon.size(), g_reconQueued, g_reconRemoved, g_reconKept, g_reconListed, g_reconParked, g_reconFull,
                g_p87Lines, g_p87Suppressed);
    char f[500];   /* P87 fold 1 */
    _snprintf_s(f, sizeof(f), _TRUNCATE, " p87f1[tombVerified=%lld,tombHeld=%lld,tombResent=%lld,tombScanDropped=%lld,tombUnowed=%lld,tombNotShared=%lld,scanRevived=%lld"
                ",removeNonceSkipped=%lld,removeResendNoCopy=%lld,reconcileLinkUnknown=%lld]",
                g_tombVerified, g_tombHeld, g_tombResent, g_tombScanDropped, g_tombUnowed, g_tombNotShared, g_scanRevived,
                g_removeNonceSkipped, g_removeResendNoCopy, g_reconLinkUnknown);
    char f2[400];   /* P87 fold 2 (tombHeld above is a snapshot of the last pass); P87 fold 3: its own group */
    _snprintf_s(f2, sizeof(f2), _TRUNCATE, " p87f2[tombNearHeld=%lld,tombVerifyDeferred=%lld,tombNotLoaded=%lld] p87f3[ownerRecordAdopt=%lld,ownerRecordRecon=%lld,ownerRecordRemove=%lld,ownerFixFailed=%lld]",
                g_tombNearHeld, g_tombVerifyDeferred, g_tombNotLoaded, g_ownerRecordAdopt, g_ownerRecordRecon, g_ownerRecordRemove, g_ownerFixFailed);
    char f3[700];   /* P87 root: the copy list */
    _snprintf_s(f3, sizeof(f3), _TRUNCATE, " p87root[copyList=%u,read=%lld,added=%lld,refreshed=%lld,retired=%lld,rowsEmitted=%lld,unkept=%lld,full=%lld,badRows=%lld,ownWins=%lld"
                ",adopt=%lld,recon=%lld,remove=%lld,notTaken=%lld,near=%lld,scanSkipped=%lld,scanBefore=%lld,scanDemoted=%lld,noSlot=%lld,diagLogged=%lld,diagOwed=%d,lines=%lld,suppressed=%lld]",
                (unsigned int)g_copyList.size(), g_clRead, g_clAdded, g_clRefreshed, g_clRetired, g_clRowsEmitted, g_clUnkept, g_clFull, g_clBadRows, g_clOwnWins,
                g_clAdopt, g_clRecon, g_clRemove, g_clNotTaken, g_clNear, g_clScanSkipped, g_clScanBefore, g_clScanDemoted, g_clNoSlot, g_clDiagLogged, g_copyDiagOwed,
                g_clLines, g_clSuppressed);
    char f4[600];   /* P87 rf1 */
    _snprintf_s(f4, sizeof(f4), _TRUNCATE, " p87rf1[marked=%lld,markRetired=%lld,markReady=%lld,markRead=%lld,markRecon=%lld,markNoRow=%lld,gaveUpKept=%lld,evicted=%lld,aliases=%lld"
                ",held=%lld,holdTimeout=%lld,baseRead=%lld,loadFull=%lld,savesAccepted=%lu,savesLanded=%lld,listPending=%d]",
                g_clMarked, g_clMarkRetired, g_clMarkReady, g_clMarkRead, g_clMarkRecon, g_clMarkNoRow, g_clGaveUpKept, g_clEvicted, g_clAliases,
                g_clHeld, g_clHoldTimeout, g_clBaseRead, g_clLoadFull, g_bdSaveReq, g_clSavesLanded, g_clListPending);
    return std::string(b) + std::string(f) + std::string(f2) + std::string(f3) + std::string(f4) + BdP14Text();   /* P14 */
}

/* build1-f: the round's one summary line (at its end, or when the link / the world goes away in the middle of it) */
void BdRosterSummary(const char* why)
{
    char line[600];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ROSTER sent=%lld pending=%u scanFound=%lld scanRegistered=%lld scanFurnitureSkipped=%lld interiorSkipped=%lld"
                 " notLive=%lld rows=%u walkTruncated=%d%s%s", g_rdSent, (unsigned int)(g_rosterQ.size() - g_rosterAt), g_rdScanFound, g_rdScanReg,
                 g_rdFurn, g_rdInner, g_rdNotLive, (unsigned int)g_rosterQ.size(), g_rdTrunc, why[0] != 0 ? " " : "", why);
    DebugLog(std::string(line));
}

/* build1-f, MAIN THREAD: one roster piece - PLACE (the kept placement inputs, the LIVE progress / complete), then STATE (the
   live mats). 1 = sent, 0 = skipped (counted), -1 = a send failed (the link is down): the round stops here, the next tick
   resumes at this piece. The piece is re-found by key (furniture inside its host) and re-checked live. */
int BdRosterOne(const std::string& key, int to)   /* [m11c2f1-1] to: the one slot it goes to (an addressed round's), -1 = every game */
{
    std::map<std::string, BdReg>::iterator row = g_reg.find(key);
    if (row == g_reg.end() || row->second.own == 0 || row->second.via == 3 || row->second.removed != 0) { ++g_rosterGone; return 0; }
    BdReg& r = row->second;
    const char* via = BdViaName(r.via);   /* mmo8a: one list of names (restored included) */
    char line[600];
    if (r.placeOk == 0)
    {
        ++g_rosterNoPos;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ROSTER key=%.63s NOT SENT: its placement inputs were never read (via=%s)", key.c_str(), via);
        BdLogRoster(line);
        return 0;
    }
    int v = 0;
    void* b = BdFindPiece(key, r, &v);
    float p = 0, nd = 0;
    int c = 0, d = 0, x = 0;
    const int stOk = (b != 0) ? BdReadState(b, &p, &nd, &c, &d, &x) : 0;
    if (stOk != 0) d = BdDismOf(b, d);   /* review-build1f H4: a wall segment's own +0x162, as every other reader */
    if (b == 0 || stOk == 0 || d != 0)
    {
        ++g_rosterNotLive; ++g_rdNotLive;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ROSTER key=%.63s not sent: %s (via=%s)", key.c_str(),
                     b == 0 ? BdVerdictWord(v) : (stOk == 0 ? "its state is unreadable" : "it is dismantled"), via);
        BdLogRoster(line);
        return 0;
    }
    if (BdOwnerPod(b) != (void*)LocalPlayerFaction())
    {
        ++g_rosterNotMine;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ROSTER key=%.63s not sent: the live piece is no longer this game's player faction's (via=%s)", key.c_str(), via);
        BdLogRoster(line);
        return 0;
    }
    float nm[coopbuild::kBuildMaxMats];
    int nmTotal = -1;
    const int nmN = BdReadMats(b, nm, (int)coopbuild::kBuildMaxMats, &nmTotal);
    const int k = (nmN < 0) ? 0 : (nmN > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : nmN);
    const int furn = r.hostKey.empty() ? 0 : 1;
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildPlace;
    m.key = key; m.sid = r.sid;
    for (int i = 0; i < 3; ++i) m.pos[i] = r.placePos[i];
    for (int i = 0; i < 4; ++i) m.rot[i] = r.placeRot[i];
    m.complete = (unsigned char)(c != 0 ? 1 : 0);
    m.progress = p; m.needed = nd;
    m.nonce = r.nonce;   /* house2 fold 2: the placement's nonce (0 = unknown - none on the wire) */
    if (furn != 0)
    {
        m.hostKey = r.hostKey;
        m.hostForm = (unsigned char)r.hostForm;
        m.floor = r.floor;
        m.outside = (unsigned char)(r.outside != 0 ? 1 : 0);
    }
    coopbuild::BuildMsg st;
    st.kind = coopbuild::kBuildState;
    st.key = key;
    st.progress = p; st.needed = nd;
    st.complete = m.complete;
    st.destroyed = (unsigned char)(x != 0 ? 1 : 0);   /* P15 (protocol 98) */
    st.nMats = (unsigned char)k;
    for (int i = 0; i < k; ++i) st.mats[i] = nm[i];
    coopbuild::BuildHelpTailOf(BdHelpAcksLandedRead(key, r), BdHelpAcksRead(key, r), &st.helpAcks);   /* help1 fold 5 (T-269): a real STATE - the helper's tail, as BdMaybeSendState's */
    if (!coopbuild::BuildEncodable(m) || !coopbuild::BuildEncodable(st)) { ++g_rosterUnencodable; return 0; }
    if (!(to >= 0 ? net::SendBuildToSlot(m, to) : net::SendBuild(m))) { ++g_rosterLinkDown; return -1; }   /* M11 C2: addressed when the round is one newcomer's */
    ++g_rosterSent; ++g_rdSent;
    const bool stSent = to >= 0 ? net::SendBuildToSlot(st, to) : net::SendBuild(st);   /* M11 C2 */
    if (stSent) ++g_rosterStateSent;
    else ++g_rosterLinkDown;
    r.progress = p; r.needed = nd; r.complete = c; r.destroyed = x;
    if (nmN >= 0) { r.nMats = nmN; for (int i = 0; i < k; ++i) r.mats[i] = nm[i]; }
    /* what the other game now holds - later STATEs (BdMaybeSendState) are measured against it */
    r.sentOk = 1; r.sentProgress = p; r.sentComplete = c; r.sentN = stSent ? k : 0;
    if (stSent) r.sentDestroyed = x;   /* P15: the roster's STATE carried the destroyed bit */
    if (stSent) r.stateAnnounce = 0;   /* help1 fold 5: the roster's STATE announced it */
    for (int i = 0; i < k; ++i) r.sentMats[i] = stSent ? nm[i] : 0.0f;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] ROSTER -> PLACE+STATE key=%.63s sid=%.80s via=%s pos=(%.1f, %.1f, %.1f) complete=%d progress=%.2f/%.2f mats=%d host=%.63s%s",
                 key.c_str(), r.sid.c_str(), via, m.pos[0], m.pos[1], m.pos[2], c, p, nd, k, furn != 0 ? r.hostKey.c_str() : "-",
                 stSent ? "" : " STATE NOT SENT (link down) - the piece is sent again next tick");
    BdLogRoster(line);
    return stSent ? 1 : -1;
}

/* mmo8a3 (C), MAIN THREAD (BdRosterTick, link up): one owed REMOVE for a re-keyed restore's OLD key, through the same message
   BdSendRemove sends (reason 1). 1 = sent (or not encodable: dropped, logged), 0 = the send failed - kept for the next tick. The
   other game removes a COPY ROW it holds at that key (BdRemoveOne); a piece it has only from its own save is not a copy row there. */
int BdSendOwedRemove(const std::string& key)
{
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildRemove;
    m.key = key;
    m.reason = (unsigned char)coopbuild::kBuildReasonDismantled;
    char line[300];
    if (!coopbuild::BuildEncodable(m))
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> REMOVE key=%.63s (re-keyed restore) NOT ENCODABLE - dropped", key.c_str());
        BdLogRemove(line);
        return 1;
    }
    if (!net::SendBuild(m)) { ++g_removeLinkDown; return 0; }
    ++g_removeSent; ++g_rsOweRemoveSent;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> REMOVE key=%.63s reason=1 (a re-keyed restore's old key: the other game's copy there goes; the piece's PLACE follows under its created key)",
                key.c_str());
    BdLogRemove(line);
    return 1;
}
/* par13 (parity P13), MAIN THREAD (BdRosterTick, link up): one owed REMOVE for an own piece dismantled while the link was down,
   the message BdSendRemove sends (reason 1). true = sent (or not encodable: dropped, logged), false = the send failed - kept, in
   order, for the next tick. The other game removes the copy row it holds at that key; a key it does not hold is 'no copy here'. */
struct BdDmOweSend
{
    bool operator()(const std::string& key)
    {
        coopbuild::BuildMsg m;
        m.kind = coopbuild::kBuildRemove;
        m.key = key;
        m.reason = (unsigned char)coopbuild::kBuildReasonDismantled;
        std::map<std::string, BdTombInfo>::iterator ti = g_tombInfo.find(key);   /* P87 fold 1: a tombstone's REMOVE carries its placement's nonce (H2); a repeat says so (L3) */
        if (ti != g_tombInfo.end()) { m.nonce = ti->second.nonce; if (ti->second.sent != 0) m.rmFlags = coopbuild::kBuildRmResend; }
        char line[300];
        if (!coopbuild::BuildEncodable(m))
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> REMOVE key=%.63s (owed dismantle) NOT ENCODABLE - dropped", key.c_str());
            BdLogRemove(line);
            return true;
        }
        if (!net::SendBuild(m)) { ++g_removeLinkDown; return false; }
        ++g_removeSent; ++g_dmOweSent;
        if (ti != g_tombInfo.end()) ti->second.sent = 1;
        if (m.rmFlags != 0) { ++g_tombResent; return true; }   /* P87 fold 1 (L3): a tombstone's repeat - counted, kept out of the REMOVE log's cap */
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> REMOVE key=%.63s reason=1 (dismantled while the link was down - the owed REMOVE, sent now the link is up)",
                    key.c_str());
        BdLogRemove(line);
        return true;
    }
};

/* par13 fold (review-par13 F2): one owed REMOVE, sent ahead of a PLACE at the same key (BdOnNewPiece). true = it went */
bool BdDmOweSendOne(const std::string& key)
{
    BdDmOweSend s;
    return s(key);
}

/* mmo8a3 (D4): 1 = the active roster round still has this key to send (at or after g_rosterAt) - an owed PLACE for it is skipped */
int BdRosterWillSend(const std::string& key)
{
    if (g_rosterActive == 0 || g_bdRosterTo >= 0) return 0;   /* [m11c2f1-1] an addressed round reaches one game: the owed PLACE still goes to every game */
    for (size_t i = g_rosterAt; i < g_rosterQ.size(); ++i) if (g_rosterQ[i] == key) return 1;
    return 0;
}

/* build1-f, MAIN THREAD (BuildTick, every tick). The link-up edge is the one DoorsTick takes for DoorsOnLinkUp; a world load
   (BuildForgetWorld) owes a round too. An owed round waits - re-asked every tick, no timer - until this game has a world and
   a player faction (the link comes up at the title screen); then the scan runs once and the round is queued. */
void BdRosterTick()
{
    const int up = StorePlayerReachable(-1);   /* another game reachable: the session link, or another player in the world through the world server (two games: the session link exactly) */
    {   /* M11 C2: a player ENTERING THE WORLD owes a whole round (the old link's up edge is the session peer's arrival - two games: as before) */
        static long s_arrCursor = 0;
        int to = -1;
        if (StoreArrivalsSince(kArrServeBuild, &s_arrCursor, &to) > 0)
        {
            g_bdRosterTo = StoreArrivalRoundTarget(to, (g_rosterOwed != 0 || g_rosterActive != 0) ? 1 : 0);   /* every game when a round is already owed or running, or the newcomer may be the session peer or is the only other player in the world */
            g_rosterOwed = 1;
        }
    }
    if (up == 0 && g_rosterLinkWasUp != 0)
    {
        if (g_rosterActive != 0)
        {
            BdRosterSummary("interrupted=link-down (the next link-up sends a whole roster)");
            g_rosterActive = 0; g_rosterQ.clear(); g_rosterAt = 0;
            g_rosterOwed = 1;   /* a world-road re-dial brings no arrival, so the interrupted round is owed again here (two games: the next link-up owes it anyway) */
        }
        g_bdRosterTo = -1;   /* the round's target goes at the interruption, a round owed but not started included: the next round goes to every game */
    }
    g_rosterLinkWasUp = up;
    BdZoneScanTick();   /* T-274: each newly loaded zone's own pieces - link or not, before the link gate (as mmo8a3 B) */
    BdOwnerChangeDrain();   /* P18: a building this game's player now owns (a house bought) - registered as own, its PLACE + STATE owed */
    /* mmo8a3 (B): with the link down the post-load scan still runs once per world (it registers own pieces present from the
       save, so a later dismantle retires their pp.build row and their progress is recorded); a walk that moved leaves it owed.
       With the link up the round below scans (the same idempotent walk: a known key is only counted scanKnown). */
    if (up == 0 && g_scanOwed != 0 && !EngineWritesBlocked() && LocalPlayerFaction() != 0 && BdScanLoaded() != 0) { g_scanOwed = 0; ++g_scanNoLink; }
    if (up == 0) { g_rosterReqDone = 0; return; }   /* the next link asks again - the roster request and every send stay link-gated */
    /* review-build1f H2 (protocol 63): once per world + link, when this game's world runs, ask the other game for its whole
       roster - what it sent at its link-up edge reached this game at the title screen and was dropped as stale at the load.
       Re-asked every tick until the send goes (no timer). */
    if (g_rosterReqDone == 0 && !EngineWritesBlocked() && LocalPlayerFaction() != 0)
    {
        coopbuild::BuildMsg rq;
        rq.kind = coopbuild::kBuildRosterRequest;
        if (net::SendBuild(rq))
        {
            g_rosterReqDone = 1; ++g_rosterReqSent;
            DebugLog("[BUILD] ROSTER -> request: this game's world runs and the link is up - the other game owes its roster");
        }
        else ++g_rosterReqLinkDown;
    }
    if (g_rosterOwed != 0)
    {
        if (EngineWritesBlocked() || LocalPlayerFaction() == 0) return;   /* no world yet: still owed */
        if (BdCopyListHold() != 0) return;   /* P87 rf1 (LOW-MED): the round's scan waits for this world's copy list - still owed */
        if (g_rosterActive != 0) BdRosterSummary("superseded=a new round is owed (a roster request or a world load)");
        g_rosterOwed = 0;
        ++g_rosterRounds;
        BdOwedResend("a roster round (link-up, the other game's roster request or a world load)");   /* house1b (item 2) */
        g_rdSent = 0; g_rdNotLive = 0; g_rdScanFound = 0; g_rdScanReg = 0; g_rdFurn = 0; g_rdInner = 0; g_rdTrunc = 0;
        if (BdScanLoaded() == 0)   /* review-build1f C1: the walk moved under the scan - owed again, next tick */
        {
            g_rosterActive = 0; g_rosterQ.clear(); g_rosterAt = 0;
            return;
        }
        g_scanOwed = 0;   /* mmo8a3 (B): this world is scanned */
        BdTombQueue("a roster round", 0);   /* P87: after the scan, so a key with a live own piece again is dropped, not sent */
        g_rosterQ.clear(); g_rosterAt = 0;
        for (std::map<std::string, BdReg>::iterator it = g_reg.begin(); it != g_reg.end(); ++it)
            if (it->second.own != 0 && it->second.via != 3 && it->second.removed == 0 && it->second.dismantled == 0) g_rosterQ.push_back(it->first);
        g_rosterActive = 1;
        /* T-274 fold (LOW-1): the round just queued sends every row that passes the test above (the zone scan's, the restore's and
           the rebuilt pieces' rows included); an owed PLACE for such a row would go twice (the owed walk runs 32 a tick in its own
           order, BdRosterWillSend only sees keys the round has not reached). Those entries are dropped here; an entry the round
           does not cover stays for the owed walk (BdRosterOne judges it as before). */
        if (g_bdRosterTo < 0)   /* [m11c2f1-1] only a round to every game covers them: an addressed one leaves them for the owed walk */
        {
            size_t w = 0;
            for (size_t i = 0; i < g_rsOwePlace.size(); ++i)
            {
                std::map<std::string, BdReg>::const_iterator o = g_reg.find(g_rsOwePlace[i]);
                const int inRound = (o != g_reg.end() && o->second.own != 0 && o->second.via != 3 && o->second.removed == 0 && o->second.dismantled == 0) ? 1 : 0;
                if (inRound == 0) g_rsOwePlace[w++] = g_rsOwePlace[i];
            }
            g_rsOwePlace.resize(w);
        }
    }
    if (g_tombSendOwed != 0 && g_rosterOwed == 0 && g_scanOwed == 0 && !EngineWritesBlocked()) BdTombQueue("tombstones read back from pp.build", 0);   /* P87 */
    if (g_tombVerifyOwed != 0 && g_rosterOwed == 0 && g_scanOwed == 0 && !EngineWritesBlocked()) BdTombQueue("a read-back tombstone verified after a zone walk", 1);   /* P87 fold 1 (H1) */
    if (!g_dmOweRemove.empty() && !EngineWritesBlocked())   /* par13 (parity P13): a dismantle made while the link was down - its REMOVE, once */
    {
        BdDmOweSend dmSend;
        coopbuild::BuildOweRemoveFlush(&g_dmOweRemove, (size_t)kRosterPerTick, dmSend);
        if (!g_dmOweRemove.empty()) return;   /* the rest next tick: no PLACE goes ahead of an owed REMOVE */
    }
    if (!g_rsOweRemove.empty() && !EngineWritesBlocked())   /* mmo8a3 (C): a re-keyed restore's old key - its REMOVE goes first */
    {
        size_t k = 0;
        while (k < g_rsOweRemove.size() && k < (size_t)kRosterPerTick && BdSendOwedRemove(g_rsOweRemove[k]) != 0) ++k;
        g_rsOweRemove.erase(g_rsOweRemove.begin(), g_rsOweRemove.begin() + (std::ptrdiff_t)k);
        if (!g_rsOweRemove.empty()) return;   /* the rest next tick: no owed PLACE goes ahead of an owed REMOVE */
    }
    if (!g_rsOwePlace.empty() && !EngineWritesBlocked() && LocalPlayerFaction() != 0)   /* mmo8a2 (M4): a rebuilt piece's PLACE + STATE, as a roster piece */
    {
        size_t k = 0;
        while (k < g_rsOwePlace.size() && k < (size_t)kRosterPerTick)
        {
            if (BdRosterWillSend(g_rsOwePlace[k]) != 0) { ++g_rsPlaceSkipRound; ++k; continue; }   /* mmo8a3 (D4): the round sends it */
            const int s = BdRosterOne(g_rsOwePlace[k], -1);   /* [m11c2f1-1] zone scan / restore / rebuild, outside the round: every game, never the round's target */
            if (s < 0) break;   /* the link went down: the rest wait (the next link-up roster sends every own row anyway) */
            if (s > 0) ++g_rsPlaceSent;
            ++k;
        }
        g_rsOwePlace.erase(g_rsOwePlace.begin(), g_rsOwePlace.begin() + (std::ptrdiff_t)k);
    }
    if (g_rosterActive == 0 || EngineWritesBlocked()) return;
    int sentNow = 0;
    while (g_rosterAt < g_rosterQ.size() && sentNow < kRosterPerTick)
    {
        if (BdRosterOne(g_rosterQ[g_rosterAt], g_bdRosterTo) < 0) break;   /* a failed send: resumed at this piece next tick */
        ++g_rosterAt; ++sentNow;
    }
    if (g_rosterAt >= g_rosterQ.size())
    {
        BdRosterSummary("");
        g_rosterActive = 0; g_rosterQ.clear(); g_rosterAt = 0;
        g_bdRosterTo = -1;   /* [m11c2f1-1] the round is over: its target goes with it */
    }
}

} // namespace

/* P3 (items.cpp's one ground drop road): who a building's spill / refund belongs to - a copy of another game's piece (a stand-in
   faction owns it), this game's own piece (ground5 fold 1 F2: its player faction, LocalPlayerFaction, owns it), or anything else
   (an NPC's box, a town building, no owner: kGroundSrcWorld - every game runs it, so the area holder's own spill is the one
   announced and a non-holder removes its spill) - and whether the mod's own K2 writes are running (BdSelfScope: a peer's REMOVE /
   STATE applied to that copy here, when the owner's game spills the real contents). Help applied to this game's OWN piece stays
   own (a real refund). MAIN THREAD (LocalPlayerFaction walks the faction list); both callers read it BEFORE the engine's original. */
int BuildGroundSource(void* b)
{
    /* ground5 fold 2 (the re-check of fold 1, LOW-MED 2): an owner read that FAULTED, or no local player faction to compare with,
       is taken as this game's own piece - its spill / refund is then PUT (worst case a duplicate), never removed as a world spill */
    void* o = 0;
    if (BdOwnerReadPod(b, &o) == 0) return coopground::kGroundSrcOwn;
    const int isCopy = (o != 0 && IsStandInFaction((::Faction*)o)) ? 1 : 0;
    ::Faction* mine = (isCopy == 0) ? LocalPlayerFaction() : 0;
    if (isCopy == 0 && mine == 0) return coopground::kGroundSrcOwn;
    const int isOwn = (mine != 0 && o == (void*)mine) ? 1 : 0;
    const int applying = (BdOnMain() != 0 && g_bdSelf > 0) ? 1 : 0;
    return coopground::GroundBuildingSource(isCopy, isOwn, applying);
}

// ------------------------------------------------------------------------------------------------------------------
void InstallBuild()
{
    if (g_installed != 0) return;
    g_installed = 1;
    ::InitializeCriticalSection(&g_qcs);
    ::InterlockedExchange(&g_qInit, 1);
    // PROBE-START: P082
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    g_hkCommit = BdHook(base, kBdCommitRva, (void*)&detour_commit, (void**)&orig_commit);
    /* createBuilding is hooked only to see the commit's own new pieces; without the commit hook it has nothing to do */
    g_hkCreate = (g_hkCommit == 1) ? BdHook(base, kBdCreateRva, (void*)&detour_create, (void**)&orig_create) : -3;
    g_hkProgress = BdHook(base, kBdProgressRva, (void*)&detour_progress, (void**)&orig_progress);
    g_hkDismantle = BdHook(base, kBdDismantleRva, (void*)&detour_dismantle, (void**)&orig_dismantle);
    g_hkDestroy = BdHook(base, kBdDestroyRva, (void*)&detour_destroy, (void**)&orig_destroy);
    const std::string msg = std::string("[BUILD] build1-a observe hooks (P082): placementCommit 0x4D6810 ") + HkWord(g_hkCommit)
        + ", createBuilding 0x57C1E0 " + (g_hkCreate == -3 ? "skipped (no commit hook)" : HkWord(g_hkCreate))
        + ", addConstructionProgress 0x558B10 " + HkWord(g_hkProgress)
        + ", addDismantleProgress 0x2A2820 " + HkWord(g_hkDismantle)
        + ", setDestroyed 0x5567F0 " + HkWord(g_hkDestroy) + " (build1-b: a new own-faction piece is sent as PLACE; build1-c: its progress, materials and completion as STATE)";
    if (g_hkCommit == 1 && g_hkCreate == 1 && g_hkProgress == 1 && g_hkDismantle == 1 && g_hkDestroy == 1) DebugLog(msg);
    else ErrorLog(msg);
    // PROBE-END: P082
    /* P18: the owner-change hook (Building::setFaction 0x556EC0, vt+0xA0) */
    g_hkOwner = BdHook(base, kBdSetFactionRva, (void*)&detour_setfaction, (void**)&orig_setfaction);
    char ocAt[40];
    _snprintf_s(ocAt, sizeof(ocAt), _TRUNCATE, "0x%llX ", kBdSetFactionRva);   /* P18 fold 1 (item 6): the address resolved for the running version */
    const std::string ocmsg = std::string("[BUILD] P18 owner-change hook: Building::setFaction ") + ocAt + HkWord(g_hkOwner)
        + " (a building that becomes this game's player's - a house bought - is registered as own and sent as PLACE + STATE; the other game adopts it)";
    if (g_hkOwner == 1) DebugLog(ocmsg);
    else ErrorLog(ocmsg);
    /* P18 fold 1 (item 5): Building::isForSale (vt+0x2C0) - a stand-in's house is not for sale */
    g_hkForSale = BdHook(base, kBdIsForSaleRva, (void*)&detour_isforsale, (void**)&orig_isforsale);
    char fsAt[40];
    _snprintf_s(fsAt, sizeof(fsAt), _TRUNCATE, "0x%llX ", kBdIsForSaleRva);
    const std::string fsmsg = std::string("[BUILD] P18 for-sale hook: Building::isForSale ") + fsAt + HkWord(g_hkForSale)
        + " (a house another player owns - a stand-in - is not offered for sale, as a player-owned house is not in single player)";
    if (g_hkForSale == 1) DebugLog(fsmsg);
    else ErrorLog(fsmsg);
    /* P18 fold 2 (P19 review): the town rebuild's saved-building drop (0x9FD470) - a saved house another player owns is held */
    g_hkRebuild = (kBdRbGetTypedRva == 0 || kBdRbSdataRva == 0) ? -2 : BdHook(base, kBdRebuildDropRva, (void*)&detour_rebuilddrop, (void**)&orig_rebuilddrop);
    char rbAt[40];
    _snprintf_s(rbAt, sizeof(rbAt), _TRUNCATE, "0x%llX ", kBdRebuildDropRva);
    const std::string rbmsg = std::string("[BUILD] P18 town-rebuild hook: the saved-building drop ") + rbAt + HkWord(g_hkRebuild)
        + " (a town rebuild keeps a saved house another player owns - a stand-in record - as it keeps this player's)";
    if (g_hkRebuild == 1) DebugLog(rbmsg);
    else ErrorLog(rbmsg);
    /* build1-d: the refund counter (0x29DBD0 is called only when a piece is dismantled) */
    const uintptr_t rbase = (uintptr_t)::GetModuleHandleA(0);
    g_hkRefund = (kBdRefundRva == 0) ? -2
        : ((coop::AddHook((void*)(rbase + (uintptr_t)kBdRefundRva), (void*)&detour_refund, (void**)&orig_refund) == coop::SUCCESS) ? 1 : -1);
    const std::string rmsg = std::string("[BUILD] build1-d refund counter: dismantleRefund 0x29DBD0 ") + HkWord(g_hkRefund);
    if (g_hkRefund == 1) DebugLog(rmsg);
    else ErrorLog(rmsg);
    /* house1b (review-house1 items 1, 6): the order gate */
    g_hkOrder = (kBdOrderRva == 0) ? -2
        : ((coop::AddHook((void*)(rbase + (uintptr_t)kBdOrderRva), (void*)&detour_order, (void**)&orig_order) == coop::SUCCESS) ? 1 : -1);
    const std::string omsg = std::string("[BUILD] house1b order gate: addOrderSelectedCharacters 0x7F9280 ") + HkWord(g_hkOrder)
        + " (no build / add-materials / repair / dismantle order on a piece handed to a house owner)";
    if (g_hkOrder == 1) DebugLog(omsg);
    else ErrorLog(omsg);
    /* house2 (item 1): the Shift-job gate - the same rule on 0x7F4EF0 */
    g_hkJob = (kBdJobRva == 0) ? -2
        : ((coop::AddHook((void*)(rbase + (uintptr_t)kBdJobRva), (void*)&detour_job, (void**)&orig_job) == coop::SUCCESS) ? 1 : -1);
    const std::string jmsg = std::string("[BUILD] house2 job gate: addJobSelectedCharacters 0x7F4EF0 ") + HkWord(g_hkJob)
        + " (no Shift-queued build / add-materials / repair / dismantle job on a piece handed to a house owner)";
    if (g_hkJob == 1) DebugLog(jmsg);
    else ErrorLog(jmsg);
    InstallFarm();   /* par16: FarmBuilding::update / operate - one writer per farm */
}

/* P15, MAIN THREAD (BuildTick): a copy's +0x1A1 flipped. The mod's own apply (the event's amount 1) is nothing more. This game's engine
   making the copy a ruin (combat here) is put back to the owner's last applied STATE when that says whole - the owner's game is the
   writer (coopbuild::BuildLocalDestroyVerdict); a STATE already held here is newer and goes on as it is. */
void BdCopyDestroyedHere(const std::string& key, BdReg& r, int modsOwn, int destroyedNow)
{
    const int v = coopbuild::BuildLocalDestroyVerdict(1, modsOwn, destroyedNow, r.ownerKnown, (int)r.ownerLast.destroyed);
    if (v == coopbuild::kBuildLocalDestroyIgnore) return;
    char line[400];
    if (v == coopbuild::kBuildLocalDestroyWait)
    {
        ++g_copyDestroyWait;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy key=%.63s made a ruin HERE by this game's engine - no owner STATE applied yet: it waits for the owner's next STATE; copyDestroyWait=%lld",
                    key.c_str(), g_copyDestroyWait);
        BdLogState(line);
        return;
    }
    {
        /* P15 fold 1 (review-p15 HIGH 2): A WALL IS NOT PUT BACK.  Wall health is not synced (T-221), so this game's engine keeps
           ruining a copy wall, and each revert (setDestroyed(0), progress still 0) deleted and remade its gate doors and permanently
           deleted mounted pieces the owner still has - a loop.  The ruin is kept here until T-221 syncs wall progress; the owner's
           next STATE still applies as it arrives. */
        int fvW = 0;
        void* bw = BdFindPiece(key, r, &fvW);
        if (bw != 0 && BdIsWall(bw) == 1)
        {
            ++g_copyWallRuinKept;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy key=%.63s (a WALL) made a ruin HERE by this game's engine - kept a ruin, not put back: wall health is not synced (T-221); copyWallRuinKept=%lld",
                        key.c_str(), g_copyWallRuinKept);
            BdLogState(line);
            return;
        }
    }
    std::map<std::string, BdStatePend>::iterator it = g_pendState.find(key);
    if (it == g_pendState.end())
    {
        if ((int)g_pendState.size() >= kPendCap) { ++g_stateDropped; return; }
        BdStatePend& s = g_pendState[key];
        s.m = r.ownerLast; s.nextTry = 0; s.fromPlace = 0; s.waitLogged = 0; s.deferTries = 0;   /* P15 fold 1 */
    }
    ++g_copyDestroyReverted;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy key=%.63s made a ruin HERE by this game's engine - the owner's game is the writer: its last STATE (whole, %.2f/%.2f complete=%d) goes back on at the next safe point; copyDestroyReverted=%lld",
                key.c_str(), r.ownerLast.progress, r.ownerLast.needed, (int)r.ownerLast.complete, g_copyDestroyReverted);
    BdLogState(line);
}

void BuildTick()
{
    BdSweepRemoved();   /* review-build1d D5 */
    FarmTick();   /* par16: farm verdicts, a held game's harvest sent to the writer */
    if (::InterlockedCompareExchange(&g_qInit, 0, 0) == 0) return;
    ::EnterCriticalSection(&g_qcs);
    const int n = g_qn;
    if (n > 0) std::memcpy(g_drain, g_q, sizeof(BdEvent) * (size_t)n);
    g_qn = 0;
    ::LeaveCriticalSection(&g_qcs);
    std::vector<std::string> touched;   /* build1-c: own pieces whose progress event arrived this tick - one STATE check per key */
    for (int i = 0; i < n; ++i)
    {
        const BdEvent& e = g_drain[i];
        if (e.keyOk != 0 && e.stateOk != 0)
        {
            std::map<std::string, BdReg>::iterator it = g_reg.find(std::string(e.key));
            if (it == g_reg.end())   /* house1b (item 3): a piece whose created key differs from the builder's - its row is under the builder's key */
            {
                std::map<std::string, std::string>::const_iterator a = g_liveAlias.find(std::string(e.key));
                if (a != g_liveAlias.end())
                {
                    it = g_reg.find(a->second);
                    if (it != g_reg.end() && it->second.liveKey != e.key) it = g_reg.end();
                    if (it != g_reg.end()) ++g_liveAliasEvents;
                }
            }
            if (it != g_reg.end())
            {
                if (e.complete != 0 && it->second.complete == 0) ItFurnMarkStale(3);   /* T-454: complete 0 -> 1 against the row's last state */
                it->second.progress = e.progress; it->second.needed = e.needed; it->second.complete = e.complete;
                if (e.dismantled != 0) it->second.dismantled = 1;
                it->second.destroyed = e.destroyed;   /* P15: the live +0x1A1 either way (a ruin made whole again reads 0) */
                if (e.nMats >= 0)
                {
                    it->second.nMats = e.nMats;
                    for (int j = 0; j < e.nMats; ++j) it->second.mats[j] = e.mats[j];
                }
                /* build1-d: a real isDismantled 0 -> 1 flip (read before and after the call; a wall segment's own +0x162) - REMOVE
                   once per key; BdSendRemove skips copies and rows already removed. review-build1d D1: setDestroyed's 0 -> 1 sends
                   nothing - the piece stays here as a ruin that can be repaired, so the other game's copy must not vanish. */
                /* P15 (protocol 98): an own piece's +0x1A1 flip is a STATE this tick (BdMaybeSendState: why destroyed / repaired); a copy's
                   flip made by this game's engine goes back to the owner's last STATE (BdCopyDestroyedHere) */
                if (e.kind == 3 && it->second.own != 0 && it->second.via != 3 && it->second.removed == 0)
                {
                    BdOwnDirty(1);   /* P15 fold 1 (LOW 6): the destroyed flag rides the pp.build STATE - written after the writer's gap */
                    size_t t3 = 0;
                    while (t3 < touched.size() && touched[t3] != it->first) ++t3;
                    if (t3 == touched.size()) touched.push_back(it->first);
                }
                if (e.kind == 3 && it->second.via == 3 && it->second.removed == 0)
                    BdCopyDestroyedHere(it->first, it->second, e.amount > 0.5f ? 1 : 0, e.destroyed);
                if (e.kind == 2 && e.dismBefore == 0 && e.dismantled != 0) BdSendRemove(it->first, it->second, (int)coopbuild::kBuildReasonDismantled);
                if (e.kind == 1 && it->second.via == 3 && it->second.handed == 1) ++g_progressOnHandedCopy;   /* build1h: work on a piece handed away here */
                if (it->second.own != 0 && it->second.via != 3) it->second.gen = g_bdGen;   /* mmo8a2 (H1): an event on the piece - it stands in THIS world */
                if (e.kind == 1 && it->second.own != 0 && it->second.via != 3)
                {
                    BdOwnDirty(1);   /* mmo8a: progress on an own piece - pp.build after the writer's 2 s gap */
                    size_t t = 0;
                    while (t < touched.size() && touched[t] != it->first) ++t;
                    if (t == touched.size()) touched.push_back(it->first);
                }
            }
        }
        const char* k = (e.keyOk != 0) ? e.key : "?";
        char line[400];
        if (e.kind == 1)
            std::sprintf(line, "[BUILD] progress key=%s %.2f/%.2f complete=%d amount=%g", k, e.progress, e.needed, e.complete, e.amount);
        else if (e.kind == 2)
            std::sprintf(line, "[BUILD] dismantled key=%s progress=%.2f/%.2f isDismantled=%d amount=%g thread=%s", k, e.progress, e.needed,
                         e.dismantled, e.amount, e.onMain ? "main" : "OTHER");
        else
            std::sprintf(line, "[BUILD] destroyed key=%s progress=%.2f/%.2f destroyed=%d by=%s", k, e.progress, e.needed, e.destroyed,
                         e.amount > 0.5f ? "the mod (a STATE applied)" : "this game's engine");   /* P15: either flip */
        if (e.kind == 1) BdLogProg(line);
        else BdLogCapped(line);
    }
    for (size_t t = 0; t < touched.size(); ++t)   /* coalesced: the registry now holds the tick's last whole state */
    {
        std::map<std::string, BdReg>::iterator it = g_reg.find(touched[t]);
        if (it != g_reg.end()) BdMaybeSendState(it->first, it->second);
    }
    BdHelpTick();   /* help1: the helper's recorded work out as HELP_WORK, owed across a link loss */
    BdHelpAckTick();   /* help1 fold (review MED 4): confirmations go once pp.build holding them has landed */
    BdRosterTick();   /* build1-f */
}

std::string BuildTestArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string sub, a1, a2, a3;
    is >> sub >> a1 >> a2 >> a3;
    const char* usage = "error buildtest: usage buildtest place <sid> [dx dz] | progress <key-substring> <amount> | dismantle <key-substring>"
                        " | into <near|host-key-substring> <sid> [indoors] | mats <key-substring> <n> | destroy <key-substring>";
    if (sub == "farm")   /* par16: the farm lever (farm.cpp FarmTestArm) */
    {
        const std::string::size_type at = arg.find("farm");
        return FarmTestArm(arg.substr(at + 4));
    }
    if (EngineWritesBlocked())
    {
        ++g_testBlocked;
        DebugLog("[BUILD] buildtest REFUSED: engine writes are blocked (world loading or tearing down)");
        return "error buildtest: engine writes are blocked";
    }
    LONG kind = 0;
    if (sub == "place")
    {
        if (a1.empty()) return usage;
        const float dx = a2.empty() ? 30.0f : (float)std::atof(a2.c_str());
        const float dz = a3.empty() ? 0.0f : (float)std::atof(a3.c_str());
        if (!(dx >= -500.0f && dx <= 500.0f && dz >= -500.0f && dz <= 500.0f)) return "error buildtest: dx/dz must be within 500";
        g_btDx = dx; g_btDz = dz; kind = 1;
    }
    else if (sub == "progress")
    {
        if (a1.empty() || a2.empty()) return usage;
        const float amt = (float)std::atof(a2.c_str());
        if (!(amt > 0.0f && amt <= 1000000.0f)) return "error buildtest: amount must be in (0, 1e6]";
        g_btAmount = amt; kind = 2;
    }
    else if (sub == "dismantle")
    {
        if (a1.empty()) return usage;
        kind = 3;
    }
    else if (sub == "mats")   /* help1: TEST-ONLY - n more of every material delivered to the piece */
    {
        if (a1.empty() || a2.empty()) return usage;
        const float amt = (float)std::atof(a2.c_str());
        if (!(amt > 0.0f && amt <= 100000.0f)) return "error buildtest: mats amount must be in (0, 1e5]";
        g_btAmount = amt; kind = 5;
    }
    else if (sub == "destroy")   /* P15: TEST-ONLY - the engine's own setDestroyed(1) on the piece */
    {
        if (a1.empty()) return usage;
        kind = 6;
    }
    else if (sub == "into")   /* build1-e */
    {
        if (a1.empty() || a2.empty() || (!a3.empty() && a3 != "indoors")) return usage;   /* build1h: a key substring or near; indoors = form 2 */
        g_btHost = a1; g_btIndoors = (a3 == "indoors") ? 1 : 0;
        kind = 4;
    }
    else return usage;
    g_btArg = (kind == 4) ? a2 : a1;
    ::InterlockedExchange(&g_btPending, kind);
    ++g_testArmed;
    DebugLog("[BUILD] buildtest " + arg + " ARMED - applied at the next safe point (threadSafeRagdollUpdates)");
    return "ok buildtest armed";
}

void BuildTestDrain()
{
    const LONG kind = ::InterlockedExchange(&g_btPending, 0);
    if (kind == 0) return;
    if (EngineWritesBlocked())
    {
        ++g_testBlocked;
        DebugLog("[BUILD] buildtest skipped: engine writes are blocked (world loading or tearing down)");
        return;
    }
    if (kind == 1) BdTestPlace();
    else if (kind == 4) BdTestInto();   /* build1-e */
    else if (kind == 5) BdTestMats();   /* help1 */
    else if (kind == 6) BdTestDestroy();   /* P15 */
    else BdTestOnEntry(kind);
}

/* build1-b: the K2 safe point (combat.cpp detour_tsRagdollUpdates, right after BuildTestDrain). At most two createBuilding
   calls per frame; an entry that cannot be made yet stays pending (the next safe point is the retry - every frame). */
void BuildCopyDrain()
{
    ++g_drainNo;
    g_hostLookups = 0;
    g_rsSearches = 0;   /* mmo8a2 */
    FarmDrain();   /* par16: the writer's farm state onto copies, requests run by the writer, the writer's publish */
    if (g_copyDiagOwed != 0 && !EngineWritesBlocked()) BdCopyListDiagTick();   /* P87 root (step 5): read-only, before any adopt / reconcile / REMOVE below */
    if (g_pend.empty() && g_wait.empty() && g_pendState.empty() && g_pendRemove.empty() && g_helpIn.empty() && g_helpGoneIn.empty() && BdReconAnyDue() == 0
        && g_tombKill.empty()) return;   /* P87: + a reconcile due; P14: + a removal queued */
    if (EngineWritesBlocked()) { ++g_copyBlocked; return; }
    BdSelfScope selfScope;   /* help1: every engine call below is the mod's own - vt+0x230 inside it is never a helper's work */
    BdHelpDrain();   /* help1: the helpers' work onto this game's own pieces (no stand-in needed) */
    BdTombKillDrain();   /* P14 (mmo8b): the save's own pieces the records retired - removed, no refund (no stand-in needed) */
    /* Every copy belongs to the stand-in of the player whose piece it is: a PLACE is judged by its sender's slot (BdPending::fromSlot), a
       STATE or a REMOVE by the owner slot its copy row recorded, a reconcile by its REMOVE's sender. An entry whose player's stand-in does
       not exist here yet waits on its own; a restore makes this game's OWN piece and never waits for a stand-in. */
    /* build1-c (review-build1b M1): the not-loaded list, looked at every kRetryGap safe points */
    if (!g_wait.empty() && (g_drainNo % kRetryGap) == 0)
        for (size_t i = 0; i < g_wait.size(); )
        {
            if (BdAreaReady(g_wait[i], g_wait[i].m.pos[0], g_wait[i].m.pos[1], g_wait[i].m.pos[2]) != 0)   /* mmo8a3 (A): a restore row reads +0xB1 */
            {
                BdPending p = g_wait[i];
                p.waitLogged = 0; p.nextTry = 0;
                g_pend.push_back(p);
                g_wait.erase(g_wait.begin() + (std::ptrdiff_t)i);
                ++g_waitResumed;
            }
            else ++i;
        }
    const int clHold = BdCopyListHold();   /* P87 rf1 (LOW-MED) */
    int calls = 0;
    for (size_t i = 0; i < g_pend.size() && calls < 2; )
    {
        if (g_pend[i].nextTry > g_drainNo) { ++i; continue; }   /* a null return: spaced retries */
        if (clHold != 0 && g_pend[i].restore == 0) { ++i; continue; }   /* P87 rf1: an adopt asks the copy list - it waits until the list is read */
        int called = 0;
        const int r = BdCopyOne(g_pend[i], &called);
        calls += called;
        if (r != 0 && r != 2) BdRestoreDone(g_pend[i]);   /* mmo8a: a restore finished without an outcome gave up - failed */
        if (r == 2) { g_wait.push_back(g_pend[i]); g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i); }
        else if (r != 0) g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
    /* build1-c: the held STATEs, after the PLACEs, so a copy made just now takes its STATE at this same safe point */
    int handled = 0;
    for (std::map<std::string, BdStatePend>::iterator it = g_pendState.begin(); it != g_pendState.end() && handled < kStateApplyPerDrain; )
    {
        if (it->second.nextTry > g_drainNo) { ++it; continue; }
        ++handled;
        if (BdApplyOne(it->first, it->second) != 0) g_pendState.erase(it++);
        else ++it;
    }
    if (clHold == 0) BdReconDrain();   /* P87 rf1: not while this world's copy list is unread; P87: REMOVEs for keys with no row here - before the REMOVEs, so a piece taken as a copy is removed at this safe point */
    /* build1-d: the REMOVEs (BuildNoteRecv already dropped any STATE held for the same key) */
    /* review-build1d D3: round robin - each safe point starts after the last key handled, so with more than kRemovePerDrain
       due at once (a wall run's segments retry every safe point) every REMOVE gets its turn; each is looked at once per point */
    int removes = 0;
    size_t visited = 0;
    const size_t total = g_pendRemove.size();
    std::map<std::string, BdRemovePend>::iterator rit = g_pendRemove.upper_bound(g_removeCursor);
    while (visited < total && removes < kRemovePerDrain && !g_pendRemove.empty())
    {
        if (rit == g_pendRemove.end()) rit = g_pendRemove.begin();
        ++visited;
        if (rit->second.nextTry > g_drainNo) { ++rit; continue; }
        ++removes;
        g_removeCursor = rit->first;
        if (BdRemoveOne(rit->first, rit->second) != 0) g_pendRemove.erase(rit++);
        else ++rit;
    }
}

/* build1-c (review-build1b M2): the world is being torn down (store.cpp, next to ResetPlayerFactionState). Pending PLACEs and
   STATEs and the copy rows belong to that world; this game's own rows stay (they name its own pieces). */
void BuildForgetWorld()
{
    FarmForgetWorld();   /* par16 */
    g_ocQ.clear(); g_ocDepth = 0;   /* P18: the queued keys belong to the world going away */
    const size_t a = g_pend.size(), w = g_wait.size(), s = g_pendState.size();
    size_t rows = 0;
    for (std::map<std::string, BdReg>::iterator it = g_reg.begin(); it != g_reg.end(); )
    {
        if (it->second.via == 3) { g_reg.erase(it++); ++rows; }
        else ++it;
    }
    const size_t rm = g_pendRemove.size();
    g_pend.clear(); g_wait.clear(); g_pendState.clear();
    g_helpIn.clear(); g_helpAckRestore.clear(); g_placeNonceRestore.clear();   /* help1 fold 4: + the PLACE nonces; help1: that world's received work (the helper re-sends what stays unconfirmed); g_help stays - its file is per world */
    g_helpGoneIn.clear(); g_helpAckAwait.clear(); g_ownGone.clear(); g_goneAwait.clear(); g_ownSwept.clear();   /* help1 fold: that world's answers, confirmations waiting to land (pp.build was flushed first; the next roster STATE carries them) and swept keys; fold 3: its gone records (pp.build holds them) */
    if (g_helpSaveDirty != 0) BdHelpSave();   /* help1 fold (MED 8): the batched owed-file save before the world goes */
    g_helpRec.clear(); g_helpRecState = 0;   /* help1 fold (MED 5/6): pp.help is read again for the world that loads (store.cpp wrote it first) */
    /* build1-f: a world load owes a scan + roster once the new world runs (BdRosterTick asks the link); a round in flight was
       for the old world */
    if (g_rosterActive != 0) BdRosterSummary("interrupted=world-teardown (the loaded world gets a whole roster)");
    g_rosterOwed = 1; g_rosterActive = 0; g_rosterQ.clear(); g_rosterAt = 0; g_bdRosterTo = -1;   /* M11 C2: a world load's round goes to every game */
    g_rosterReqDone = 0;   /* review-build1f H2: the loaded world asks for the other game's roster once it runs */
    g_pendRemove.clear();   /* build1-d */
    ++g_bdGen;   /* mmo8a2 (H1): own rows kept across the unload are the world before's - pp.build takes only rows of the world that loads next */
    g_dmOweForgot += (long long)coopbuild::BuildOweRemoveForget(&g_dmOweRemove);   /* par13: a dismantle's owed REMOVE belongs to the world it was made in */
    g_clListPending = 1; g_clHoldT0 = 0; g_clFeedAt = 0;   /* P87 rf1: the next world's copy list is still to be read */
    g_copyList.clear(); g_copyRetire.clear(); g_copyDiagOwed = 0;   /* P87 root: the copy list is in pp.build (OwnBuildTeardownFlush wrote it first) and read back at the next load */
    g_tomb.clear(); g_tombRetire.clear(); g_tombSendOwed = 0; g_tombInfo.clear(); g_tombVerifyOwed = 0; g_tombVerifyZone = -1; g_tombVerifyCursor = 0; g_recon.clear(); g_tombKill.clear(); g_tombKillNo.clear(); g_refundSrc.clear(); g_ledgerPend.clear(); /* P14 (+ fold 1) */   /* P87: a world's tombstones are in its pp.build (written first); received REMOVEs belong to that world */
    g_ownRetireOwed.clear(); g_rsOwePlace.clear(); g_rsOweRemove.clear();   /* mmo8a2 (M2, M4), mmo8a3 (C): they go with their world (store.cpp handed pp.build's dirty rows to the writer first - mmo8a3 D1) */
    g_scanOwed = 1;   /* mmo8a3 (B): the loaded world is scanned once, link or not */
    memset(g_bdZoneScanned, 0, sizeof(g_bdZoneScanned));   /* T-274: the loaded world's zones are walked again as each finishes loading */
    memset(g_bdZoneMoves, 0, sizeof(g_bdZoneMoves)); g_bdZoneLastPick = -1;   /* T-274 fold (LOW-2) */
    ++g_forgotWorlds;
    char line[400];   /* review-build1f C2 */
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] world teardown: forgot %u pending PLACEs, %u waiting for their area, %u held STATEs, %u copy rows, %u held REMOVEs",
                (unsigned int)a, (unsigned int)w, (unsigned int)s, (unsigned int)rows, (unsigned int)rm);
    DebugLog(std::string(line));
}

/* build1-b: MAIN THREAD (session dispatch). The newer whole state replaces a pending one for the same key. */
void BuildNoteRecv(const coopbuild::BuildMsg& m, unsigned int fromPeer)
{
    if (m.kind == coopbuild::kBuildHelpWork)   /* help1 (protocol 90): a helper's work on its copy of this game's piece - the K2 safe point applies it */
    {
        ++g_helpWorkRecv;
        const int from = net::PlayerSlotOfKey(fromPeer);   /* M7b slice 4 (P106): the sender's own slot - the notebook's stamp, the session peer's on the session link */
        for (size_t i = 0; i < g_helpIn.size(); ++i)   /* help1 fold: a resend of an entry still waiting here (held for its gap) - one copy is enough */
            if (g_helpIn[i].slot == from && g_helpIn[i].m.seq == m.seq && g_helpIn[i].m.key == m.key && g_helpIn[i].m.nonce == m.nonce)   /* help1 fold 3: of the same placement */
            {   /* fold 2 (HIGH 1): the latest copy wins, queued at its arrival (a renumbered entry may reuse a held seq with other work) */
                ++g_helpRecvTwice;
                g_helpIn.erase(g_helpIn.begin() + (std::ptrdiff_t)i);
                break;
            }
        if ((int)g_helpIn.size() >= kPendCap) { ++g_helpRecvDropped; return; }   /* the helper sends it again (unconfirmed) */
        BdHelpIn in;
        in.m = m; in.slot = from; in.nextTry = 0; in.waitLogged = 0; in.heldLogged = 0;
        g_helpIn.push_back(in);
        return;
    }
    if (m.kind == coopbuild::kBuildHelpGone)   /* help1 fold (protocol 91): the owner no longer holds the piece - the K2 safe point drops the work and hands its materials back */
    {
        ++g_helpGoneRecv;
        if ((int)g_helpGoneIn.size() >= kPendCap) return;   /* the entries stay: sent again, answered again */
        BdHelpGoneIn g;
        g.key = m.key; g.seq = m.seq; g.applied = m.applied; g.slot = net::PlayerSlotOfKey(fromPeer);   /* M7b slice 4 (P106): the sender's slot */
        g.nonce = m.nonce; g.nEx = (int)m.nMats;   /* help1 fold 3: the placement and its refused tail */
        for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) g.ex[i] = (i < (int)m.nMats) ? m.mats[i] : 0.0f;
        g_helpGoneIn.push_back(g);
        return;
    }
    if (m.kind == coopbuild::kBuildHandAck)   /* house1b (protocol 75): the house owner holds the piece this game handed it */
    {
        ++g_ackRecv;
        BdOwedLoad();
        std::map<std::string, BdOwed>::iterator ow = g_owed.find(m.key);
        const int had = (ow != g_owed.end() && ow->second.acked == 0) ? 1 : 0;
        if (had != 0) { ow->second.acked = 1; BdOwedSave(); }
        else ++g_ackNoOwed;
        char al[260];
        _snprintf_s(al, sizeof(al), _TRUNCATE, "[BUILD] <- HAND_ACK key=%.63s from peer %u: %s", m.key.c_str(), fromPeer,
                    had != 0 ? "the owner holds it - no longer re-sent" : "nothing owed here (already acknowledged or never owed)");
        BdLogCapped(al);
        return;
    }
    if (m.kind == coopbuild::kBuildRosterRequest)   /* review-build1f H2 (protocol 63): the other game's world runs - it is owed a whole roster */
    {
        ++g_rosterReqRecv;
        g_rosterOwed = 1; g_bdRosterTo = -1;   /* M11 C2: a request's round goes to every game, as before */
        char ql[160];
        _snprintf_s(ql, sizeof(ql), _TRUNCATE, "[BUILD] ROSTER <- request from peer %u: a whole roster round is owed", fromPeer);
        DebugLog(std::string(ql));
        return;
    }
    if (m.kind == coopbuild::kBuildRemove)   /* build1-d: queued for the K2 safe point; a copy not made yet is never made */
    {
        ++g_removeRecv;
        if (m.reason == coopbuild::kBuildReasonDestroyed)   /* review-build1d D1: still decoded (protocol 61), never a removal */
        {
            ++g_removeDestroyedIgnored;
            char dl[300];
            std::sprintf(dl, "[BUILD] <- REMOVE key=%.63s reason=2 (destroyed) IGNORED - a destroyed piece is a repairable ruin, not a removal",
                         m.key.c_str());
            BdLogRemove(dl);
            return;
        }
        {   /* P87 fold 1 (H2): the REMOVE names the placement it ends (its nonce); this game's copy row at the key is ANOTHER placement (a
               newer one - e.g. this game's own piece built there and handed to the owner while the owner was away): nothing is touched */
            std::map<std::string, BdReg>::const_iterator nr = g_reg.find(m.key);
            if (nr != g_reg.end() && nr->second.via == 3 && nr->second.removed == 0 && coopbuild::BuildRemoveNonceSkip(m.nonce, nr->second.nonce) != 0)
            {
                ++g_removeNonceSkipped;
                char nl[320];
                _snprintf_s(nl, sizeof(nl), _TRUNCATE, "[BUILD] <- REMOVE key=%.63s nonce=%08x KEPT: this game's copy there is another placement (nonce=%08x)%s; removeNonceSkipped=%lld",
                            m.key.c_str(), m.nonce, nr->second.nonce, (m.rmFlags & coopbuild::kBuildRmResend) != 0 ? " - a tombstone's repeat" : "", g_removeNonceSkipped);
                BdLogP87(nl);
                return;
            }
        }
        BdNoteRemovedRecv(m.key, m.nonce);   /* house2 (item 4, fold MED2): the placement(s) this REMOVE ends here, by (key, nonce) */
        {   /* house1b: the owner dismantled a piece this game handed it - nothing is owed any more */
            BdOwedLoad();
            std::map<std::string, BdOwed>::iterator ow = g_owed.find(m.key);
            if (g_owedFor.empty() && !g_owedTried.empty()) g_owedErasedUnread.insert(m.key);   /* house2 fold 2: the file is unread - its row must not come back */
            if (ow != g_owed.end() && coopbuild::BuildRemoveNonceSkip(m.nonce, ow->second.m.nonce) == 0) { g_owed.erase(ow); ++g_owedRemoved; BdOwedSave(); }   /* P87 fold 1 (H2): a hand-over of another placement stays owed */
        }
        g_pendState.erase(m.key);   /* a STATE held for it never lands */
        int dropped = 0;
        for (size_t i = 0; i < g_pend.size(); )
        {
            if (g_pend[i].m.key == m.key && coopbuild::BuildRemoveNonceSkip(m.nonce, g_pend[i].m.nonce) == 0) { g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i); ++dropped; }   /* P87 fold 1 (H2) */
            else ++i;
        }
        for (size_t i = 0; i < g_wait.size(); )
        {
            if (g_wait[i].m.key == m.key && coopbuild::BuildRemoveNonceSkip(m.nonce, g_wait[i].m.nonce) == 0) { g_wait.erase(g_wait.begin() + (std::ptrdiff_t)i); ++dropped; }   /* P87 fold 1 (H2) */
            else ++i;
        }
        std::map<std::string, BdReg>::iterator row = g_reg.find(m.key);
        const int copyHere = (row != g_reg.end() && row->second.via == 3 && row->second.removed == 0) ? 1 : 0;
        char rl[400];
        if (dropped != 0)
        {
            ++g_removePendingDropped;
            std::sprintf(rl, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 - its PLACE was still pending here: dropped, never created (reason=%d)",
                         m.key.c_str(), (int)m.reason);
            BdLogRemove(rl);
        }
        if (copyHere == 0)
        {
            if (dropped == 0 && (m.rmFlags & coopbuild::kBuildRmResend) != 0) ++g_removeResendNoCopy;   /* P87 fold 1 (L3): a tombstone's repeat - its own counter, no line */
            else if (dropped == 0)
            {
                ++g_removeNoCopy;
                std::sprintf(rl, "[BUILD] <- REMOVE key=%.63s removed=0 dismantled=0 - %s", m.key.c_str(),
                             row == g_reg.end() ? "no copy and no pending PLACE here"
                             : (row->second.via != 3 ? "the key is THIS game's own piece" : "the copy was already removed"));
                BdLogRemove(rl);
            }
            if (row == g_reg.end()) BdReconQueue(m.key, net::PlayerAddrOfKey(fromPeer));   /* P87: no row here - the piece this game's save brought back may stand at the key; judged against its sender's stand-in */
            return;
        }
        if (g_pendRemove.find(m.key) != g_pendRemove.end()) return;   /* one per key */
        BdRemovePend& rp = g_pendRemove[m.key];
        rp.reason = (int)m.reason; rp.tries = 0; rp.waitLogged = 0; rp.deferLogged = 0; rp.nextTry = 0; rp.matsBefore = 0.0f;
        rp.missTries = 0;   /* build1-e */
        return;
    }
    if (m.kind == coopbuild::kBuildState)   /* build1-c: keep only the latest per key; applied at the safe point */
    {
        ++g_stateRecv;
        std::map<std::string, BdStatePend>::iterator it = g_pendState.find(m.key);
        if (it != g_pendState.end())
        {
            if (it->second.fromPlace == 0) ++g_stateStale;   /* the older one never applied */
            it->second.m = m; it->second.fromPlace = 0; it->second.nextTry = 0;
            it->second.deferTries = 0;   /* P15 fold 1 (LOW 4): a newer owner word starts its own bounded retries */
            return;
        }
        if ((int)g_pendState.size() >= kPendCap) { ++g_stateDropped; return; }
        BdStatePend& s = g_pendState[m.key];
        s.m = m; s.nextTry = 0; s.fromPlace = 0; s.waitLogged = 0; s.deferTries = 0;   /* P15 fold 1 */
        (void)fromPeer;
        return;
    }
    ++g_placeRecv;
    g_recon.erase(m.key);   /* P87: the owner lists this key again - a reconcile waiting for it is over */
    char line[400];
    std::sprintf(line, "[BUILD] <- PLACE key=%s sid=%.80s pos=(%.1f, %.1f, %.1f) complete=%d progress=%.2f/%.2f host=%.63s form=%d floor=%d peer=%u",
                 m.key.c_str(), m.sid.c_str(), m.pos[0], m.pos[1], m.pos[2], (int)m.complete, m.progress, m.needed,
                 m.hostKey.empty() ? "-" : m.hostKey.c_str(), (int)m.hostForm, m.floor, fromPeer);
    BdLogCapped(line);
    for (size_t i = 0; i < g_pend.size(); ++i)
        if (g_pend[i].m.key == m.key) { ++g_copyDup; g_pend[i].m = m; g_pend[i].fromSlot = net::PlayerAddrOfKey(fromPeer); return; }
    for (size_t i = 0; i < g_wait.size(); ++i)
        if (g_wait[i].m.key == m.key) { ++g_copyDup; g_wait[i].m = m; g_wait[i].fromSlot = net::PlayerAddrOfKey(fromPeer); return; }
    if ((int)(g_pend.size() + g_wait.size()) >= kPendCap)
    {
        ++g_pendingDropped;
        std::sprintf(line, "[BUILD] <- PLACE key=%s DROPPED: %d copies already pending", m.key.c_str(), kPendCap);
        BdLogCapped(line);
        return;
    }
    BdPending p;
    p.m = m;
    p.tries = 0;
    p.waitLogged = 0;
    p.nextTry = 0;
    p.hostLogged = 0;   /* build1-e */
    p.slotWaitCounted = 0;   /* house1b */
    p.restore = 0; p.rsOutcome = 0; p.rsHasState = 0;   /* mmo8a: a PLACE from the other game */
    p.fromSlot = net::PlayerAddrOfKey(fromPeer);   /* fold 1 (F6): its HAND_ACK goes back to this slot; fold 2 (D1): -2 = the session peer before its slot was known */
    g_pend.push_back(p);
}

void BuildNoteBad() { ++g_placeRecvBad; }

/* ---- mmo8a (mmo8-buildings-read.md 5): pp.build, the buildings this player owns --------------------------------------- */
/* help1 fold 2 (re-check MED 3): an own piece removed here that a helper worked on stays in pp.build as a GONE MARKER - its PLACE bytes
   are a REMOVE of the key, its STATE carries each helper slot's last applied seq - so after a reload this game still answers that
   helper's work HELP_GONE with the right floor (never UNKNOWN, never handing back what it applied). BuildRestoreQueue reads it into
   g_ownGone. help1 fold 3: one row per (key, nonce) under BuildGoneRowKey (the REMOVE names the piece's key) - a new own piece at the key
   no longer replaces it. Same write rules as every pp.build row. 1 = written, 0 = not encodable (goneRowUnkept). */
int BdGoneRow(const std::string& key, unsigned int nonce, const std::vector<coopbuild::BuildHelpAck>& acks, std::vector<coopown::BuildRow>* rows)
{
    coopbuild::BuildMsg rm, st;
    rm.kind = coopbuild::kBuildRemove; rm.key = key; rm.reason = coopbuild::kBuildReasonDismantled;
    st.kind = coopbuild::kBuildState; st.key = key; st.helpAcks = acks;
    std::vector<char> pb, sb;
    if (!coopbuild::BuildEncodable(rm) || !coopbuild::BuildEncodable(st) || !coopbuild::EncodeBuild(&pb, rm) || !coopbuild::EncodeBuild(&sb, st)) { ++g_goneRowUnkept; return 0; }
    coopown::BuildRow row;
    row.key = coopbuild::BuildGoneRowKey(key, nonce);   /* help1 fold 3: per (key, nonce) */
    row.place.assign(pb.begin(), pb.end()); row.state.assign(sb.begin(), sb.end());
    rows->push_back(row);
    ++g_goneRowsKept;
    return 1;
}
void BuildOwnRows(std::vector<coopown::BuildRow>* rows, std::vector<std::string>* retired, int* skipped)
{
    rows->clear(); retired->clear(); *skipped = 0;
    long long oldWorld = 0;   /* mmo8a2 (H1) */
    for (std::map<std::string, BdReg>::iterator it = g_reg.begin(); it != g_reg.end(); ++it)
    {
        BdReg& r = it->second;
        const int emit = coopown::BuildRowEmit(r.own, r.via, r.removed, r.dismantled, r.gen, g_bdGen);   /* mmo8a2 (H1): this world's rows only */
        if (emit == coopown::kBuildRowSkip) { if (r.own != 0 && r.via != 3) ++oldWorld; continue; }
        if (emit == coopown::kBuildRowRetire)   /* taken down here: its row is retired */
        {
            BdRestoreNonce(it->first, r, 0);   /* help1 fold 4 */
            retired->push_back(it->first);
            const std::vector<coopbuild::BuildHelpAck>* ga = BdHelpAcksRead(it->first, r);   /* help1 fold 2 (MED 3): helped - its gone marker instead */
            if (ga != 0 && !ga->empty()) BdGoneNote(it->first, r.nonce, *ga, 0);   /* help1 fold 3: its (key, nonce) record - written with the records below */
            continue;
        }
        BdRestoreNonce(it->first, r, 0);   /* help1 fold 4 (re-check #1): the PLACE record keeps its nonce */
        if (r.placeOk == 0) { ++*skipped; BdHelpAckUnkept(r); continue; }   /* its placement inputs were never read: no PLACE to keep */
        coopbuild::BuildMsg m;
        m.kind = coopbuild::kBuildPlace;
        m.key = it->first; m.sid = r.sid;
        for (int i = 0; i < 3; ++i) m.pos[i] = r.placePos[i];
        for (int i = 0; i < 4; ++i) m.rot[i] = r.placeRot[i];
        m.complete = (unsigned char)(r.complete != 0 ? 1 : 0);
        m.progress = r.progress; m.needed = r.needed;
        m.ownerSlot = coopbuild::kBuildOwnerSender;
        m.nonce = r.nonce;
        if (!r.hostKey.empty())
        {
            m.hostKey = r.hostKey;
            m.hostForm = (unsigned char)r.hostForm;
            m.floor = r.floor;
            m.outside = (unsigned char)(r.outside != 0 ? 1 : 0);
        }
        coopbuild::BuildMsg st;
        st.kind = coopbuild::kBuildState;
        st.key = it->first;
        st.progress = r.progress; st.needed = r.needed;
        st.complete = m.complete;
        st.destroyed = (unsigned char)(r.destroyed != 0 ? 1 : 0);   /* P15 fold 1 (LOW 6): the ruin survives a save */
        const int k = (r.nMats < 0) ? 0 : (r.nMats > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : r.nMats);
        st.nMats = (unsigned char)k;
        for (int i = 0; i < k; ++i) st.mats[i] = r.mats[i];
        const std::vector<coopbuild::BuildHelpAck>* ha = BdHelpAcksRead(it->first, r);   /* help1: the last applied seq per helper slot lives on in pp.build */
        if (ha == 0 && r.nonce == 0)   /* help1 fold 4 (re-check #1a): rows pp.build brought back, the nonce not learned yet - written back (with it), never erased */
        {
            std::map<std::string, BdAckRestore>::const_iterator kr = g_helpAckRestore.find(it->first);
            if (kr != g_helpAckRestore.end() && coopbuild::BuildAckRestoreFor(r.nonce, kr->second.nonce, 1) != 0) { ha = &kr->second.acks; m.nonce = kr->second.nonce; ++g_helpAckKeptUnknown; }
        }
        if (ha != 0) st.helpAcks = *ha;
        std::vector<char> pb, sb;
        if (!coopbuild::BuildEncodable(m) || !coopbuild::BuildEncodable(st) || !coopbuild::EncodeBuild(&pb, m) || !coopbuild::EncodeBuild(&sb, st))
        { ++*skipped; BdHelpAckUnkept(r); continue; }
        coopown::BuildRow row;
        row.key = it->first; row.place.assign(pb.begin(), pb.end()); row.state.assign(sb.begin(), sb.end());
        rows->push_back(row);
        if (r.helpAckDirty != 0) { r.helpAcksWriting = r.helpAcks; r.helpAckDirty = 0; r.helpAckWait = 1; }   /* help1 fold (MED 4): these acks ride this record */
    }
    for (std::map<BdPlKey, BdGoneRec>::iterator og = g_ownGone.begin(); og != g_ownGone.end(); ++og)   /* help1 fold 2 (MED 3) + fold 3: one gone marker per (key, nonce) record */
    {
        BdGoneRec& gr = og->second;
        const int kept = BdGoneRow(og->first.first, og->first.second, gr.acks, rows);
        if (gr.dirty != 0) { gr.writing = gr.acks; gr.dirty = 0; gr.wait = (kept != 0) ? 1 : 3; }   /* 3: no record keeps it - it lands at once (BdHelpAckTick), as BdHelpAckUnkept */
    }
    for (size_t t = 0; t < g_tomb.size(); ++t)   /* P87: this world's tombstones - one "~t" row each (a REMOVE and an empty STATE) */
    {
        coopbuild::BuildMsg tm, ts;
        tm.kind = coopbuild::kBuildRemove; tm.key = g_tomb[t]; tm.reason = coopbuild::kBuildReasonDismantled;
        {   /* P87 fold 1: the tail - its placement's nonce and, for layout furniture, its host's key (a row read back without the tail stays without it) */
            std::map<std::string, BdTombInfo>::const_iterator ti = g_tombInfo.find(g_tomb[t]);
            if (ti != g_tombInfo.end() && ti->second.tail != 0)
            {
                tm.nonce = ti->second.nonce;
                tm.rmFlags = (unsigned char)(coopbuild::kBuildRmTombTail | (ti->second.furn != 0 ? coopbuild::kBuildRmFurniture : 0));
                if (ti->second.furn != 0) tm.hostKey = ti->second.hostKey;
            }
        }
        ts.kind = coopbuild::kBuildState; ts.key = g_tomb[t];
        std::vector<char> tb, tsb;
        if (!coopbuild::BuildEncodable(tm) || !coopbuild::BuildEncodable(ts) || !coopbuild::EncodeBuild(&tb, tm) || !coopbuild::EncodeBuild(&tsb, ts)) { ++g_tombUnkept; continue; }
        coopown::BuildRow row;
        row.key = coopbuild::BuildTombRowKey(g_tomb[t]);
        row.place.assign(tb.begin(), tb.end()); row.state.assign(tsb.begin(), tsb.end());
        {   /* P14 fold 1: the escape ledger rides the row's STATE bytes (no reader before fold 1 reads them); P14 fold 2 (D6): never an empty
               STATE over a ledger; P14 fold 3 (D6): a failed encoding writes a ledger that REFUSES every re-drop (the last good one lacks
               newer pickups - a duplicate; this is a loss) */
            std::map<std::string, BdTombInfo>::iterator tl = g_tombInfo.find(g_tomb[t]);
            if (tl != g_tombInfo.end() && !coopbuild::BuildLedgerEmpty(tl->second.led))
            {
                std::string lb;
                if (coopbuild::BuildLedgerEncode(tl->second.led, &lb)) row.state = lb;
                else
                {
                    ++g_ledUnkept;
                    tl->second.led.refuse = 1;
                    coopbuild::BuildLedger rf;
                    rf.refuse = 1; rf.missed = tl->second.led.missed; rf.spill = tl->second.led.spill;
                    if (coopbuild::BuildLedgerEncode(rf, &lb)) { row.state = lb; ++g_ledKeptRefuse; }
                }
            }
        }
        rows->push_back(row);
        ++g_tombRowsEmitted;
    }
    for (std::map<std::string, BdCopyEnt>::const_iterator ce = g_copyList.begin(); ce != g_copyList.end(); ++ce)   /* P87 root: the copy list - one '~c' row each */
    {
        std::string rk;
        std::vector<char> cp, cs;
        if (!coopbuild::BuildCopyRowMake(ce->second.rec, &rk, &cp, &cs)) { ++g_clUnkept; continue; }
        coopown::BuildRow row;
        row.key = rk; row.place.assign(cp.begin(), cp.end()); row.state.assign(cs.begin(), cs.end());
        rows->push_back(row);
        ++g_clRowsEmitted;
    }
    for (size_t t = 0; t < g_copyRetire.size(); ++t) retired->push_back(g_copyRetire[t]);   /* P87 root: entries that ended (a live row of the same key wins) */
    g_copyRetire.clear();
    for (size_t t = 0; t < g_tombRetire.size(); ++t) retired->push_back(g_tombRetire[t]);   /* P87: tombstones that ended (a live row of the same key wins) */
    g_tombRetire.clear();
    coopown::BuildRetireEmit(g_ownRetireOwed, g_bdGen, retired);   /* mmo8a2 (M2): keys the 60 s sweep erased, still owed */
    g_ownRowsOldWorld = oldWorld;
}
/* P14 fold 1 (the escape ledger), MAIN THREAD (items.cpp GrOnDropFound): an own piece's refund item landed at gkey (the key the ground
   road routes it by). Listed under the piece (its tombstone's ledger, or pending until BdTombNote); gen 1 when the tombstone was read
   back (the P14 removal's own re-drop - matched, never owed again); an item landing in another zone is counted as spill (inexact). */
void BuildRefundLanded(void* who, const char* gkey, int qty)
{
    if (who == 0 || gkey == 0 || gkey[0] == 0) return;
    std::map<void*, BdRefundSrc>::const_iterator it = g_refundSrc.find(who);
    if (it == g_refundSrc.end()) { ++g_ledRefundNoPiece; return; }
    const std::string pk = it->second.key, gk(gkey);
    std::string sid;
    if (!coopbuild::BuildGroundKeySid(gk, &sid)) { ++g_ledNoSid; return; }
    int gen = 0;
    coopbuild::BuildLedger* L = BdLedgerOf(pk, &gen);
    if (L == 0) { ++g_ledRefundNoPiece; return; }
    const int q = qty < 1 ? 1 : qty;
    const int kr = coopbuild::BuildLedgerAddRefund(L, sid, gk, q, gen);   /* P14 fold 2 (D3): 2 = no room for its key - owed by material */
    if (kr == 2) ++g_ledUnkeyed; else if (kr == 0) g_ledMissed += q;
    int psx = 0, psy = 0, gsx = 0, gsy = 0;
    const size_t at = gk.find('@');
    const int pOk = (BoxKeySector(pk.c_str(), &psx, &psy) != 0) ? 1 : 0;
    const int gOk = (at != std::string::npos && std::sscanf(gk.c_str() + at + 1, "%d,%d", &gsx, &gsy) == 2) ? 1 : 0;
    const int spill = (pOk != 0 && gOk != 0 && (gsx != psx || gsy != psy)) ? 1 : 0;
    const int same = coopbuild::BuildZoneShownSame(pOk, psx, psy, gOk, gsx, gsy);   /* P14 fold 4 (e5): an unparsed sector is not shown equal */
    if (spill != 0) { L->spill += q; g_ledSpill += q; }
    if (same == 0 && spill == 0) ++g_ledZoneUnread;
    const int sb = coopbuild::BuildLedgerSpillUnkeyed(L, sid, kr, same != 0 ? 0 : 1);   /* P14 fold 3 (N1) + fold 4 (e5): an unkeyed item not SHOWN in its piece's zone - its pickup is never charged */
    if (sb != 0) ++g_ledSpillBlind;
    if (sb == 2) ++g_ledRefuseAll;
    ++g_ledRefund;
    BdOwnDirty(2);
    char line[520];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: refund item %.100s qty=%d landed (%s%s%s); refund entries=%u",
                pk.c_str(), gk.c_str(), q, gen != 0 ? "a re-drop" : "the dismantle's refund", spill != 0 ? ", in another zone: spill" : (same == 0 ? ", its zone not shown (a sector did not parse): as another zone" : ""),
                kr == 2 ? (sb == 1 ? ", no room for its key: owed by material - BLIND, its pickup there is never charged (its re-drop refused)"
                : (sb == 2 ? ", no room for its key: owed by material - no line for the blind mark: the whole ledger REFUSES" : ", no room for its key: owed by material"))
                : (kr == 0 ? ", NOT in the ledger: missed" : ""), (unsigned int)L->refund.size());
    BdLogLedger(line);
}
/* P14 fold 1 (the escape ledger), MAIN THREAD (items.cpp: GrOnGone / GrOnPart / GrOnAsk / GroundNoteConfirm - our pickup; ApplyItemPlaced
   ok 1 - the other player's): qty taken from the ground at gkey. A refund item of a dismantled own piece gets an entry: ours names the
   picker's squad record and the seq that record must reach (any squad write after this call has a larger seq, so it carries the item);
   the pp.build record carrying the entry is handed to the writer NOW, before that squad's write-now (and "pp.build" commits first).
   P14 fold 5 (T732): holder 1 - our PUT of it was applied by the area's holder (kBuildLedgerEscHolder, peer 1: no squad record). */
int BdRefundEscape(const char* gkey, int qty, void* picker, int peer, int holder)
{
    if (gkey == 0 || gkey[0] != 'G' || qty <= 0) return 1;
    const std::string gk(gkey);
    coopbuild::BuildLedger* L = 0;
    std::string pk, gsid;
    int byMat = 0;
    for (std::map<std::string, BdTombInfo>::iterator it = g_tombInfo.begin(); it != g_tombInfo.end() && L == 0; ++it)
        if (coopbuild::BuildLedgerFind(it->second.led, gk) >= 0) { L = &it->second.led; pk = it->first; }
    for (std::map<std::string, coopbuild::BuildLedger>::iterator it = g_ledgerPend.begin(); it != g_ledgerPend.end() && L == 0; ++it)
        if (coopbuild::BuildLedgerFind(it->second, gk) >= 0) { L = &it->second; pk = it->first; }
    if (L == 0 && coopbuild::BuildGroundKeySid(gk, &gsid))
    {   /* P14 fold 2 (D1/D3): no refund entry keys it - a ledger whose first drop of this material went unkeyed, its piece in the same
           zone, takes it by material */
        int gsx = 0, gsy = 0, psx = 0, psy = 0;
        const size_t at = gk.find('@');
        if (at != std::string::npos && std::sscanf(gk.c_str() + at + 1, "%d,%d", &gsx, &gsy) == 2)
        {
            std::vector<coopbuild::BuildLedger*> cand;   /* P14 fold 4 (L2): every same-zone ledger with room for the material */
            std::vector<std::string> candKey;
            for (std::map<std::string, BdTombInfo>::iterator it = g_tombInfo.begin(); it != g_tombInfo.end(); ++it)
                if (coopbuild::BuildLedgerMatRoom(it->second.led, gsid) > 0 && BoxKeySector(it->first.c_str(), &psx, &psy) != 0 && psx == gsx && psy == gsy)
                { cand.push_back(&it->second.led); candKey.push_back(it->first); }
            for (std::map<std::string, coopbuild::BuildLedger>::iterator it = g_ledgerPend.begin(); it != g_ledgerPend.end(); ++it)
                if (coopbuild::BuildLedgerMatRoom(it->second, gsid) > 0 && BoxKeySector(it->first.c_str(), &psx, &psy) != 0 && psx == gsx && psy == gsy)
                { cand.push_back(&it->second); candKey.push_back(it->first); }
            if (cand.size() == 1) { L = cand[0]; pk = candKey[0]; byMat = 1; }
            else if (cand.size() > 1)
            {   /* P14 fold 4 (L2): which piece dropped it is not known - charging the first could let another re-drop it in full (a
                   duplicate): the material is BLIND in every candidate (each re-drop of it refused - a loss, never a duplicate) */
                const int ra = coopbuild::BuildLedgerBlindAll(cand, gsid);
                ++g_ledMatShared;
                g_ledBlind += (long long)cand.size() - ra;
                g_ledRefuseAll += ra;
                BdOwnDirty(2);
                const int nowS = OwnBuildWriteNow("P14 ledger");
                if (nowS != 0) ++g_ledWriteNow; else ++g_ledWriteLate;
                char sl[640];
                _snprintf_s(sl, sizeof(sl), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: refund item %.100s qty=%d picked up by material, but %u same-zone ledgers have room"
                            " for it - which piece dropped it is not known: the material is BLIND in every one (their re-drops of it refused)%s - pp.build %s; matShared=%lld",
                            candKey[0].c_str(), gk.c_str(), qty, (unsigned int)cand.size(), ra > 0 ? "; no line for the mark in some: those ledgers REFUSE every re-drop" : "",
                            nowS != 0 ? "handed to the writer first" : "NOT handed now (the squad writes wait for it)", g_ledMatShared);
                BdLogLedger(sl);
                return nowS != 0 ? 1 : 0;
            }
        }
    }
    if (L == 0) return 1;   /* not a refund item of a dismantled own piece (the common case) */
    char line[680];
    std::string rec;
    if (peer == 0 && (picker == 0 || OwnSquadKeyOfCharNow(picker, &rec) == 0 || rec.empty()))
    {
        ++g_ledEscNoRec;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: refund item %.100s qty=%d picked up, but no own squad record holds the picker - NOT in the ledger"
                    " (a reload drops it again: inexact); escNoRec=%lld", pk.c_str(), gk.c_str(), qty, g_ledEscNoRec);
        BdLogLedger(line);
        return 1;
    }
    const unsigned long long need = (peer == 0) ? OwnSeqNow() + 1 : 0;
    const unsigned char kind = (holder != 0) ? coopbuild::kBuildLedgerEscHolder : ((peer != 0) ? coopbuild::kBuildLedgerEscPeer : coopbuild::kBuildLedgerEscOwn);
    int full = 0;
    const int got = (byMat != 0) ? coopbuild::BuildLedgerAddEscMat(L, gsid, qty, kind, rec, need, &full) : coopbuild::BuildLedgerAddEsc(L, gk, qty, kind, rec, need, &full);
    if (full == 1 || full == 3) ++g_ledEscFull;
    if (full == 1) ++g_ledBlind;
    if (full == 3) ++g_ledRefuseAll;   /* P14 fold 3 (N2): no line for the blind mark - the whole ledger refuses */
    if (full == 2) ++g_ledEscMerged;
    if (byMat != 0 && got > 0) ++g_ledMatEsc;
    if (got < qty) g_ledEscOver += qty - got;
    if (got <= 0 && full != 1 && full != 3)   /* P14 fold 3 (N2): a refuse-all ledger is handed first too (the hold kept) */
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: refund item %.100s qty=%d taken - NOT recorded (%s); escFull=%lld escOver=%lld",
                    pk.c_str(), gk.c_str(), qty, full != 0 ? "the ledger is full and the material has no line: inexact" : "its refund quantity is used up", g_ledEscFull, g_ledEscOver);
        BdLogLedger(line);
        return 1;
    }
    if (got > 0) { if (holder != 0) ++g_ledEscHolder; else if (peer != 0) ++g_ledEscPeer; else ++g_ledEscOwn; }
    BdOwnDirty(2);
    const int now = OwnBuildWriteNow("P14 ledger");
    if (now != 0) ++g_ledWriteNow; else ++g_ledWriteLate;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P14 ledger key=%.63s: %s took %d of refund item %.100s%s%s (seq >= %llu)%s%s - pp.build %s; escapes=%u",
                pk.c_str(), holder != 0 ? "the area's holder (our PUT it applied: on its ground)" : (peer != 0 ? "the other player" : "our picker"), got, gk.c_str(), peer != 0 ? "" : " into squad record ", rec.c_str(), need,
                byMat != 0 ? " by material (its first drop went unkeyed)" : "",
                full == 1 ? "; the rest NOT recorded - the material is BLIND (its re-drop refused)"
                : (full == 3 ? "; the rest NOT recorded, no line for the blind mark - the whole ledger REFUSES (every re-drop refused)"
                : (full == 2 ? "; merged into an entry (the caps full: inexact)" : "")),
                now != 0 ? "handed to the writer first" : "NOT handed now (the squad writes wait for it)", (unsigned int)L->esc.size());
    BdLogLedger(line);
    return now != 0 ? 1 : 0;
}
int BuildRefundTaken(const char* gkey, int qty, void* picker, int peer) { return BdRefundEscape(gkey, qty, picker, peer, 0); }
/* P14 fold 5 (T732), MAIN THREAD (items.cpp GroundNoteConfirm, a PUT the holder applied): a refund item now on the holder's ground
   - an escape that always counts at reload; pp.build to the writer at once, as a pickup's */
int BuildRefundHanded(const char* gkey, int qty) { return BdRefundEscape(gkey, qty, 0, 1, 1); }
/* P14 fold 2 (D5), MAIN THREAD (items.cpp GrPickHoldParkTick): 1 = pp.build handed to the writer (nothing dirty left, or handed now) */
int BuildLedgerHandNow()
{
    if (BuildOwnDirty() == 0) return 1;
    const int now = OwnBuildWriteNow("P14 ledger retry");
    if (now != 0) ++g_ledHandRetry;
    return now;
}
/* ---- help1 fold (review MED 4, MED 5/6): the store's side of the confirmations and of pp.help ---- */
void BuildOwnWriteHanded(unsigned long long seq)
{
    const DWORD now = ::GetTickCount();
    for (std::set<std::string>::const_iterator k = g_helpAckAwait.begin(); k != g_helpAckAwait.end(); ++k)
    {
        std::map<std::string, BdReg>::iterator it = g_reg.find(*k);
        if (it == g_reg.end() || it->second.helpAckWait != 1) continue;
        it->second.helpAckWait = 2; it->second.helpAckSeq = seq; it->second.helpAckAt = now;
    }
    for (std::set<BdPlKey>::const_iterator k = g_goneAwait.begin(); k != g_goneAwait.end(); ++k)   /* help1 fold 3: the gone records this write carries */
    {
        std::map<BdPlKey, BdGoneRec>::iterator it = g_ownGone.find(*k);
        if (it == g_ownGone.end() || it->second.wait != 1) continue;
        it->second.wait = 2; it->second.seq = seq; it->second.at = now;
    }
}
void BuildHelpRecLoaded(const std::vector<coopown::HelpRecRow>& rows)
{
    g_helpRec.clear();
    for (size_t i = 0; i < rows.size(); ++i) g_helpRec[BdPlKey(rows[i].key, rows[i].nonce)] = rows[i];   /* help1 fold 3: one per (key, nonce) */
    g_helpRecState = 1;
    for (std::map<std::string, BdHelpRow>::iterator it = g_help.begin(); it != g_help.end(); ++it) BdHelpRecMerge(it->first, it->second);
    for (std::map<BdPlKey, BdHelpRow>::iterator it = g_helpOld.begin(); it != g_helpOld.end(); ++it) BdHelpRecMerge(it->first.first, it->second);   /* help1 fold 3 */
    char line[200];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help records (pp.help) read: %u rows - refunds and new help count from them", (unsigned int)rows.size());
    DebugLog(std::string(line));
}
void BuildHelpRecUnread()
{
    if (g_helpRecState == 2) return;
    g_helpRecState = 2;
    DebugLog("[BUILD] help records (pp.help) NOT READ - refunds wait (never handed back twice); new help counts from the owner's STATE");
}
/* help1 fold 3: a row's counts into pp.help's rows - joined into its own placement's row (key, nonce), else that row is this one */
void BdHelpRecOut(std::map<BdPlKey, coopown::HelpRecRow>* out, const std::string& key, const BdHelpRow& h)
{
    coopown::HelpRecRow mine;
    mine.key = key; mine.slot = h.ownerSlot; mine.nextSeq = h.nextSeq; mine.nonce = h.nonce;   /* help1 fold 2 */
    for (int i = 0; i < (int)coopown::kHelpRecMats && i < (int)coopbuild::kBuildMaxMats; ++i) mine.given[i] = h.refunded[i];
    std::map<BdPlKey, coopown::HelpRecRow>::iterator o = out->find(BdPlKey(key, h.nonce));
    if (o == out->end() || coopown::HelpRecJoin(&o->second, mine) == 0) (*out)[BdPlKey(key, h.nonce)] = mine;   /* another owner's old row at this key: the live one */
}
void BuildHelpRecRows(std::vector<coopown::HelpRecRow>* rows)
{
    rows->clear();
    BdHelpLoad();   /* the rows of the world this store is on (another world's file clears the map) */
    std::map<BdPlKey, coopown::HelpRecRow> out = g_helpRec;
    for (std::map<std::string, BdHelpRow>::const_iterator it = g_help.begin(); it != g_help.end(); ++it) BdHelpRecOut(&out, it->first, it->second);
    for (std::map<BdPlKey, BdHelpRow>::const_iterator it = g_helpOld.begin(); it != g_helpOld.end(); ++it) BdHelpRecOut(&out, it->first.first, it->second);   /* help1 fold 3 */
    for (std::map<BdPlKey, coopown::HelpRecRow>::const_iterator o = out.begin(); o != out.end(); ++o) rows->push_back(o->second);
}
int BuildHelpRecDirty() { return g_helpRecDirty; }
void BuildHelpRecDirtyClear() { g_helpRecDirty = 0; }
void BuildHelpQuitFlush() { if (g_helpSaveDirty != 0) BdHelpSave(); }
int BuildOwnDirty() { return g_ownDirty; }
void BuildOwnDirtyClear() { g_ownDirty = 0; }
void BuildOwnDirtyMark(int level) { BdOwnDirty(level); }
void BuildOwnRetiredTaken(const std::vector<std::string>& taken) { coopown::BuildRetireTaken(&g_ownRetireOwed, taken, g_bdGen); }
/* P87 rf1 (HIGH), MAIN THREAD: store.cpp BuildSaveTrackNote - the engine accepted a save request (SaveManager::save 0x47B0B0, after orig) */
unsigned long BuildSaveAccepted() { return ++g_bdSaveReq; }
/* P87 rf1 (HIGH), MAIN THREAD: store.cpp BuildSaveTrackTick - save request reqNo has landed (its quick.save written after the request): every
   removed mark set before that request and settled in this world retires - the save no longer holds the copy */
void BuildSaveLanded(unsigned long reqNo, const char* name)
{
    ++g_clSavesLanded;
    std::vector<std::string> gone;
    for (std::map<std::string, BdCopyEnt>::const_iterator it = g_copyList.begin(); it != g_copyList.end(); ++it)
        if (coopbuild::BuildCopyMarkRetires(it->second.rec.removed, it->second.rmReady, it->second.rmReq, reqNo) != 0) gone.push_back(it->first);
    for (size_t i = 0; i < gone.size(); ++i)
    {
        std::map<std::string, BdCopyEnt>::iterator it = g_copyList.find(gone[i]);
        if (it == g_copyList.end()) continue;
        if (it->second.diag == 0 && g_copyDiagOwed > 0) --g_copyDiagOwed;
        g_copyList.erase(it);
        g_copyRetire.push_back(coopbuild::BuildCopyRowKey(gone[i]));
        ++g_clMarkRetired;
        BdOwnDirty(2);
        char line[400];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list key=%.63s removed mark RETIRED: save '%.60s' (request %lu, asked after the removal) has landed - its '~c' row leaves pp.build; copyListMarkRetired=%lld",
                    gone[i].c_str(), name != 0 ? name : "?", reqNo, g_clMarkRetired);
        BdLogCopyList(line);
    }
}
/* P87 rf1 (MED), MAIN THREAD: store.cpp OwnBuildCopyListFeed - this world's copy list from the pp.build base, when the load edge did not
   hand pp.build (a single / identity / claim-busy / writer-off load, nothing to restore, or a store chosen with the world running).
   Own PLACE rows (bare keys) win over a '~c' row of the same key, as at the edge. Once per world. */
void BuildCopyListFromBase(const std::vector<coopown::BuildRow>& rows, const char* why)
{
    if (g_clListPending == 0) return;
    std::vector<coopbuild::BuildCopyRec> cl;
    std::set<std::string> own;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (coopbuild::BuildCopyRowIs(rows[i].key))
        {
            coopbuild::BuildCopyRec cr;
            if (coopbuild::BuildCopyRowRead(rows[i].key, rows[i].place, rows[i].state, &cr)) cl.push_back(cr);
            else ++g_clBadRows;
            continue;
        }
        if (rows[i].key.empty() || rows[i].key[0] == '~') continue;   /* a tombstone / gone marker: not an own PLACE row */
        coopbuild::BuildMsg m;
        if (coopbuild::DecodeBuildSaved(rows[i].place.data(), rows[i].place.size(), &m) == coopbuild::kBuildDecodeOk && m.kind == coopbuild::kBuildPlace) own.insert(m.key);
    }
    ++g_clBaseRead;
    BdCopyListLoaded(cl, own);
    BdCopyListReadDone(why != 0 ? why : "the pp.build base");
}
int BuildRestoreQueue(const std::vector<coopown::BuildRow>& rows)
{
    int queued = 0;
    char line[400];
    std::vector<coopbuild::BuildCopyRec> clRows;   /* P87 root: the copy list's '~c' rows */
    std::set<std::string> ownKeys;                  /* P87 root: this game's own PLACE rows - they win over a '~c' row of the same key */
    for (size_t i = 0; i < rows.size(); ++i)
    {
        ++g_rsRows;
        coopbuild::BuildMsg m, st;
        const int a = coopbuild::DecodeBuildSaved(rows[i].place.data(), rows[i].place.size(), &m);
        const int s = coopbuild::DecodeBuildSaved(rows[i].state.data(), rows[i].state.size(), &st);
        if (coopbuild::BuildCopyRowIs(rows[i].key))
        {   /* P87 root: a copy-list row - read after every own row (BdCopyListLoaded) */
            coopbuild::BuildCopyRec cr;
            if (coopbuild::BuildCopyRowRead(rows[i].key, rows[i].place, rows[i].state, &cr)) clRows.push_back(cr);
            else
            {
                ++g_clBadRows;
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P87 copy list row %.63s NOT read: not a v%d copy-list row - left in pp.build, not used; copyListBadRows=%lld",
                            rows[i].key.c_str(), coopbuild::kBuildCopyRowVersion, g_clBadRows);
                BdLogCopyList(line);
            }
            continue;
        }
        if (a == coopbuild::kBuildDecodeOk && m.kind == coopbuild::kBuildRemove && coopbuild::BuildTombRowOf(rows[i].key, m.key))
        {   /* P87: a tombstone - an own piece dismantled in this world; its REMOVE is owed to the other game at every roster round */
            BdTombLoaded(m, rows[i].state);   /* P87 fold 1: with its tail (nonce, furniture); P14 fold 1: + the escape ledger */
            continue;
        }
        unsigned int gn = 0;   /* help1 fold 3: the placement a gone marker row names (its row key); a fold 2 marker under the bare key reads as 0 */
        if (a == coopbuild::kBuildDecodeOk && m.kind == coopbuild::kBuildRemove && (m.key == rows[i].key || coopbuild::BuildGoneRowOf(rows[i].key, m.key, &gn)))
        {   /* help1 fold 2 (re-check MED 3): a gone marker (BdGoneRow) - no piece; its last applied seqs answer later help HELP_GONE; fold 3: its (key, nonce) record, landed */
            const int ok = (s == coopbuild::kBuildDecodeOk && st.kind == coopbuild::kBuildState && st.key == m.key) ? 1 : 0;
            if (ok != 0 && !st.helpAcks.empty())
            {
                BdGoneRec& gr = g_ownGone[BdPlKey(m.key, gn)];
                gr.acks = st.helpAcks; gr.landed = st.helpAcks; gr.writing = st.helpAcks; gr.dirty = 0; gr.wait = 0;
            }
            else g_ownSwept.insert(m.key);
            if (m.key == rows[i].key) coopown::BuildRetireNote(&g_ownRetireOwed, rows[i].key, g_bdGen);   /* fold 3: a fold 2 bare-key marker goes; its record is written under its fold 3 row key */
            ++g_goneRowsRead;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s is a GONE MARKER (removed here; %u helper rows%s) - later help for it is answered HELP_GONE",
                        rows[i].key.c_str(), ok != 0 ? (unsigned int)st.helpAcks.size() : 0u, ok != 0 ? "" : ", its STATE unreadable");
            BdLogRestore(line);
            continue;
        }
        if (s == coopbuild::kBuildDecodeOk && st.kind == coopbuild::kBuildState && st.key == rows[i].key && !st.helpAcks.empty())
        {   /* help1: the confirmation rows come back with the row (a present piece too); fold 3: with its PLACE's nonce - used only for that placement */
            BdAckRestore& ar = g_helpAckRestore[rows[i].key];
            ar.acks = st.helpAcks;
            ar.nonce = (a == coopbuild::kBuildDecodeOk && m.kind == coopbuild::kBuildPlace) ? m.nonce : 0;
        }
        if (a != coopbuild::kBuildDecodeOk || m.kind != coopbuild::kBuildPlace || m.key != rows[i].key)
        {
            ++g_rsFailed;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s FAILED: its PLACE bytes do not decode as this key's PLACE (%d) owner=mine", rows[i].key.c_str(), a);
            BdLogRestore(line);
            continue;
        }
        ownKeys.insert(m.key);   /* P87 root */
        if (m.nonce != 0) g_placeNonceRestore[m.key] = m.nonce;   /* help1 fold 4 (re-check #1): the placement's nonce, for the row the load scan registers */
        {
            std::map<std::string, BdReg>::iterator er = g_reg.find(m.key);
            if (er != g_reg.end() && er->second.gen == g_bdGen && er->second.own != 0 && er->second.via != 3)
            {
                if (er->second.nonce == 0) BdRestoreNonce(m.key, er->second, 1);   /* the scan ran first */
                else if (m.nonce != 0 && er->second.nonce != m.nonce)
                {   /* a new placement took the key before pp.build was read: the record's rows are the earlier placement's - its own gone record */
                    std::map<std::string, BdAckRestore>::const_iterator kr = g_helpAckRestore.find(m.key);
                    if (kr != g_helpAckRestore.end() && kr->second.nonce == m.nonce && !kr->second.acks.empty()) BdGoneNote(m.key, m.nonce, kr->second.acks, 1);
                    g_placeNonceRestore.erase(m.key);
                }
            }
        }
        m.ownerSlot = coopbuild::kBuildOwnerSender;
        int dup = 0;
        for (size_t j = 0; j < g_pend.size(); ++j) if (g_pend[j].m.key == m.key) dup = 1;
        for (size_t j = 0; j < g_wait.size(); ++j) if (g_wait[j].m.key == m.key) dup = 1;
        if (dup != 0)
        {
            ++g_rsRefused;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s refused (a PLACE for this key is already waiting here) owner=mine", m.key.c_str());
            BdLogRestore(line);
            continue;
        }
        if ((int)(g_pend.size() + g_wait.size()) >= kPendCap)
        {
            ++g_rsFailed;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%.63s FAILED: the pending list is full (%d) owner=mine", m.key.c_str(), kPendCap);
            BdLogRestore(line);
            continue;
        }
        BdPending p;
        p.m = m; p.tries = 0; p.waitLogged = 0; p.nextTry = 0; p.hostLogged = 0; p.slotWaitCounted = 0;
        p.restore = 1; p.rsOutcome = 0;
        p.rsHasState = (s == coopbuild::kBuildDecodeOk && st.kind == coopbuild::kBuildState && st.key == m.key) ? 1 : 0;
        if (p.rsHasState != 0) p.st = st;
        g_pend.push_back(p);
        ++queued; ++g_rsQueued;
    }
    BdCopyListLoaded(clRows, ownKeys);   /* P87 root (step 2): the copy list, before any adopt / reconcile / REMOVE (the stand-in comes later) */
    BdCopyListReadDone("the restore edge");   /* P87 rf1 */
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore queued %d of %u pp.build rows - each is made only where its loaded area answers a complete search with no piece",
                queued, (unsigned int)rows.size());
    DebugLog(std::string(line));
    return queued;
}
std::string BuildRestoreText()
{
    long long waiting = 0, pending = 0;
    for (size_t i = 0; i < g_wait.size(); ++i) if (g_wait[i].restore != 0) ++waiting;
    for (size_t i = 0; i < g_pend.size(); ++i) if (g_pend[i].restore != 0) ++pending;
    long long zbOff = 0, zbNoZm = 0;
    ZoneBuildingsGateRefusals(&zbOff, &zbNoZm);   /* mmo8a3 (A) */
    char b[1300];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "own[buildRestore rows=%lld,present=%lld,recreated=%lld,waitingArea=%lld,refusedVerdict=%lld,failed=%lld]"
                " buildRestoreMore[queued,pending,lines,suppressed]=%lld,%lld,%lld,%lld"
                " buildRestore2[searchCapped,rekeyed,placeOwed,placeSent,retireOwed,oldWorldRows,gen]=%lld,%lld,%lld,%lld,%lld,%lld,%ld"
                " gate[b1Loaded,b1Not,b1Fault]=%lld,%lld,%lld gateRefused[offMain,noZm]=%lld,%lld"
                " buildRestore3[removeOwed,removeSent,placeSkippedRound,scanNoLink]=%lld,%lld,%lld,%lld"
                " dismantleOwed[owed,added,sent,forgot]=%lld,%lld,%lld,%lld",
                g_rsRows, g_rsPresent, g_rsRecreated, waiting, g_rsRefused, g_rsFailed, g_rsQueued, pending, g_rsLines, g_rsSuppressed,
                g_rsSearchCapped, g_rsRekeyed, (long long)g_rsOwePlace.size(), g_rsPlaceSent, (long long)g_ownRetireOwed.size(),
                g_ownRowsOldWorld, g_bdGen, g_rsGateB1Loaded, g_rsGateB1Not, g_rsGateB1Fault, zbOff, zbNoZm,
                (long long)g_rsOweRemove.size(), g_rsOweRemoveSent, g_rsPlaceSkipRound, g_scanNoLink,
                (long long)g_dmOweRemove.size(), g_dmOweAdded, g_dmOweSent, g_dmOweForgot);
    return std::string(b) + BdP87Text();   /* P87 */
}

/* P18 fold 1 (item 3): TEST LEVER `buyhouse <buildingKey|nearest>` - THE REAL PURCHASE on this game: the engine's own
   Building::buyMeCallback (vt+0x288, 0x7ACB20) with the argument its dialogue's confirm button passes (2 - the only branch that
   buys; buyMeAsk 0x7AC870 binds it as a CMethodDelegate1<Building,int>), so the price is taken and every side effect runs. Only a
   building the engine offers for sale (isForSale vt+0x2C0) is bought, as the UI only offers those. `nearest` = the nearest for-sale
   building to the watched player. The answer names the price (Building::calculateSaleValue, the buy callback's own price; "unknown" when the
   row is missing); an optional last word `cats=<n>` sets the buyer's purse to n first. `buyhouse show <buildingKey|last>` (read only): the owner, the doors' states and the residentSquad
   of the house - the bought house's world side, the same on both games. MAIN THREAD (the command channel). */
int BdForSalePred(void* b) { return BdIsForSalePod(b); }
int BdMoneyPod(void* f)   /* Faction::factionOwnerships (+0x80) -> Ownerships::money (+0x88); -1 unread */
{
    __try { void* own = *(void**)((char*)f + kFacOwnerships); return own != 0 ? *(const int*)((const char*)own + 0x88) : -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdBuyCallbackPod(void* b)
{
    __try { ((BdBuyCbFn)((*(void***)b)[0x288 / 8]))(b, 2); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
typedef int (*BdSaleValueFn)(void* b);
typedef char (*BdTakeMoneyFn)(void* own, int amount);
int BdSaleValuePod(void* b)   /* the price: Building::calculateSaleValue, the call the buy callback makes before its afford test; -1 no row, -2 faulted */
{
    if (kBdSaleValueRva == 0) return -1;
    __try { return ((BdSaleValueFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBdSaleValueRva))(b); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int BdPurseSetPod(void* f, int target)   /* the faction's purse set to `target` through Ownerships::takeMoney (vtable slot 0) - the call the buy
                                            callback charges through; a negative amount is a gift. 1 set, 0 refused, -1 unread or faulted */
{
    __try
    {
        void* own = *(void**)((char*)f + kFacOwnerships);
        if (own == 0) return -1;
        const int cur = *(const int*)((const char*)own + 0x88);
        return ((BdTakeMoneyFn)((*(void***)own)[0]))(own, tradecharge::PayToSet(cur, target)) != 0 ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdDoorStatePod(void* d)   /* door vt+0x3C8 (Building -> DoorStuff, as the buy callback converts) -> DoorStuff::state +0x380; -1 none, -2 faulted */
{
    __try { void* ds = ((BdGetPtrFn)((*(void***)d)[0x3C8 / 8]))(d); return ds != 0 ? *(const int*)((const char*)ds + 0x380) : -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
unsigned BdDoorsPod(void* b, void** out, unsigned cap)
{
    __try
    {
        const unsigned n = *(const unsigned*)((const char*)b + 0x1C0);
        void** arr = *(void***)((char*)b + 0x1C8);
        unsigned k = 0;
        for (unsigned i = 0; arr != 0 && i < n && k < cap; ++i) if (arr[i] != 0) out[k++] = arr[i];
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdResidentSquadNonePod(void* b)   /* 1 the residentSquad hand is none (type 0xB), 0 set, -1 faulted */
{
    __try { return (*(const int*)((const char*)b + kBResidentSquad + 0x8) == 0xB) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
std::string BdHouseShow(const std::string& which)
{
    const std::string key = (which == "last") ? g_ocLastKey : which;
    if (key.empty()) return "error buyhouse show: no key (no house bought or applied on this game yet)";
    void* b = ObjectByPositionKey(key.c_str());
    if (b == 0) return "error buyhouse show: key not loaded here: " + key;
    void* o = BdOwnerPod(b);
    const char* holder = (o != 0 && o == (void*)LocalPlayerFaction()) ? "mine" : ((o != 0 && IsStandInFaction((::Faction*)o)) ? "standin" : "other");
    void* doors[kBdBuyDoorCap];
    const unsigned nd = BdDoorsPod(b, doors, kBdBuyDoorCap);
    std::string st;
    unsigned open = 0;
    for (unsigned i = 0; i < nd; ++i)
    {
        const int ds = BdDoorStatePod(doors[i]);
        char one[16];
        _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%d", i != 0 ? "," : "", ds);
        st += one;
        if (ds == 1 || ds == 2) ++open;   /* doorsync.h kDoorOpen 1, kDoorOpening 2 */
    }
    const int none = BdResidentSquadNonePod(b);
    const int pass = (std::strcmp(holder, "other") != 0 && open == nd && none == 1) ? 1 : 0;
    char out[600];
    _snprintf_s(out, sizeof(out), _TRUNCATE, "ok buyhouse show key=%.63s owner='%.60s' holder=%s doors=%u open=%u states=[%s] residentSquad=%s forSale=%d house=%s",
                key.c_str(), BdFactionName(o).c_str(), holder, nd, open, st.c_str(), none == 1 ? "none" : (none == 0 ? "SET" : "unread"), BdIsForSalePod(b),
                pass != 0 ? "PASS (a player's house: doors open, residents gone)" : "FAIL");
    DebugLog(std::string("[BUILD] ") + out);
    return out;
}
std::string BuyHouseCommand(const std::string& argIn)
{
    if (argIn.empty()) return "error buyhouse: usage `buyhouse <buildingKey|nearest> [cats=<n>]` or `buyhouse show <buildingKey|last>`";
    if (argIn.size() > 5 && argIn.compare(0, 5, "show ") == 0) return BdHouseShow(argIn.substr(5));
    /* `cats=<n>` as the last word: the buyer's purse is set to n just before the buy, so a run is not voided by a short purse */
    std::string arg = argIn;
    int cats = -1;
    const size_t sp = arg.rfind(' ');
    if (sp != std::string::npos && arg.compare(sp + 1, 5, "cats=") == 0)
    {
        if (tradecharge::ParseCats(arg.c_str() + sp + 1, &cats) == 0) return "error buyhouse: cats=<n> takes a whole number 0..100000000";
        arg.erase(sp);
    }
    if (BdOnMain() == 0) return "error buyhouse: not on the main thread";
    if (EngineWritesBlocked()) return "error buyhouse: engine writes blocked (a load or a teardown)";
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return "error buyhouse: no player faction";
    void* b = 0;
    double dist = -1;
    if (arg == "nearest")
    {
        std::string err;
        unsigned seen = 0;
        b = NearestBuildingWhere(&BdForSalePred, &dist, &err, &seen);
        if (b == 0)
        {
            char nf[300];
            _snprintf_s(nf, sizeof(nf), _TRUNCATE, "error buyhouse nearest: %.80s - %u building(s) checked in the active zones, none for sale here (Building::isForSale vt+0x2C0) - nothing bought",
                        err.c_str(), seen);
            DebugLog(std::string("[BUILD] ") + nf);
            return nf;
        }
    }
    else
    {
        b = ObjectByPositionKey(arg.c_str());
        if (b == 0) return "error buyhouse: key not loaded here: " + arg;
    }
    char key[kKeyCap];
    key[0] = 0;
    if (ObjectPositionKey(b, key, kKeyCap, "", 3, 1) == 0) return "error buyhouse: the building has no key";
    const int forSale = BdIsForSalePod(b);
    const std::string before = BdFactionName(BdOwnerPod(b));
    char out[700];
    if (forSale != 1)
    {
        ++g_ocBuyRefused;
        _snprintf_s(out, sizeof(out), _TRUNCATE, "error buyhouse key=%.63s: not for sale on this game (Building::isForSale vt+0x2C0 = %d) owner '%.60s' - nothing bought",
                    key, forSale, before.c_str());
        DebugLog(std::string("[BUILD] ") + out);
        return out;
    }
    const int price = BdSaleValuePod(b);
    char priceText[24];
    if (price >= 0) _snprintf_s(priceText, sizeof(priceText), _TRUNCATE, "%d", price);
    else _snprintf_s(priceText, sizeof(priceText), _TRUNCATE, "unknown");
    char catsText[96];
    catsText[0] = 0;
    if (cats >= 0)
    {
        const int from = BdMoneyPod((void*)mine);
        const int set = BdPurseSetPod((void*)mine, cats);
        _snprintf_s(catsText, sizeof(catsText), _TRUNCATE, " cats=%d(from=%d set=%d)", cats, from, set);
    }
    const int m0 = BdMoneyPod((void*)mine);
    const int rc = BdBuyCallbackPod(b);
    void* after = BdOwnerPod(b);
    const int m1 = BdMoneyPod((void*)mine);
    const int bought = (after == (void*)mine) ? 1 : 0;
    ++g_ocBuyLever;
    if (bought != 0) g_ocLastKey = key;
    else ++g_ocBuyRefused;   /* the engine's own refusal: on the confirm argument its only no-buy branch is the afford test */
    char why[160];
    why[0] = 0;
    if (rc < 0) _snprintf_s(why, sizeof(why), _TRUNCATE, " FAULTED");
    else if (bought == 0)
        _snprintf_s(why, sizeof(why), _TRUNCATE, " - NOT bought (the engine refused: price %s, purse %d%s)", priceText, m0,
                    (price >= 0 && price > m0) ? " - can't afford" : "");
    _snprintf_s(out, sizeof(out), _TRUNCATE, "%s buyhouse key=%.63s dist=%.0f price=%s%s owner '%.60s' -> '%.60s' money %d -> %d (paid %d) - the engine's own Building::buyMeCallback vt+0x288(2)%s",
                bought != 0 ? "ok" : "error", key, dist, priceText, catsText, before.c_str(), BdFactionName(after).c_str(), m0, m1, m0 - m1, why);
    DebugLog(std::string("[BUILD] ") + out);
    return out;
}

/* TEST-ONLY levers (MAIN THREAD, the command channel). `standin <slot> [read]`: make (or take into the table) the stand-in for any
   player number through the one lookup (OwnerFactionForSlot) and read it back; `read` makes nothing. Either way the loaded buildings
   that faction owns are counted (first 4 keys). `buildgive <key-substring|nearest> <slot>`: give the loaded building whose key holds
   the substring (the nearest such) - or the building nearest this game's first own character - to that player's faction, through
   the engine's own setFaction. */
int BdOwnedByCount(void* f, std::string* keys)
{
    static void* list[4096];
    int zones = 0, trunc = 0, c = 0;
    if (f == 0) return 0;
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    for (int i = 0; i < n; ++i)
    {
        if (BdOwnerPod(list[i]) != f) continue;
        if (++c > 4) continue;
        char k[kKeyCap]; k[0] = 0;
        if (ObjectPositionKey(list[i], k, kKeyCap, "", 3, 1) == 0) { k[0] = '?'; k[1] = 0; }
        *keys += std::string(c > 1 ? " " : "") + k;
    }
    return c;
}
std::string StandInCommand(const std::string& arg)
{
    int slot = -1; char mode[16]; mode[0] = 0;
    const int got = std::sscanf(arg.c_str(), "%d %15s", &slot, mode);
    const bool readOnly = (got == 2 && std::strcmp(mode, "read") == 0);
    if (got < 1 || (got == 2 && !readOnly) || slot < 0 || slot > coopslot::kSlotMax) return "error standin usage: standin <slot 0..1023> [read]";
    if (!readOnly && EngineWritesBlocked()) return "error standin: engine writes are blocked (a load or a teardown) - try again";
    ::Faction* f = OwnerFactionForSlot(slot, !readOnly);
    const bool inTable = (f != 0 && f != LocalPlayerFaction());
    if (f == 0 && readOnly) f = StandInRecordFaction(slot);
    std::string keys;
    const int owned = BdOwnedByCount((void*)f, &keys);
    char l[640];
    std::sprintf(l, "%s standin player=%d id=%s faction=%p name='%.60s' mine=%d table=%d placeholder=%d worldRecord=%d recordSlot=%d ownsLoadedBuildings=%d [%.300s]",
                 (f != 0 || readOnly) ? "ok" : "error", slot, coopslot::StandInId(slot).c_str(), (void*)f, f != 0 ? f->getName().c_str() : "",
                 (f != 0 && f == LocalPlayerFaction()) ? 1 : 0, inTable ? 1 : 0, StandInIsPlaceholder(slot), StandInRecordFaction(slot) != 0 ? 1 : 0,
                 f != 0 ? StandInRecordSlot(f) : -1, owned, keys.c_str());
    DebugLog(std::string("[BUILD] P106 ") + l);
    return std::string(l);
}
std::string BuildGiveCommand(const std::string& arg)
{
    char what[64]; what[0] = 0; int slot = -1;
    if (std::sscanf(arg.c_str(), "%63s %d", what, &slot) != 2 || slot < 0 || slot > coopslot::kSlotMax) return "error buildgive usage: buildgive <key-substring|nearest> <slot 0..1023>";
    if (EngineWritesBlocked()) return "error buildgive: engine writes are blocked (a load or a teardown) - try again";
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return "error buildgive: this game has no player faction";
    ::Faction* f = OwnerFactionForSlot(slot, true);
    if (f == 0) return "error buildgive: no faction for that player (no world, no number of mine yet, or the stand-in table is full)";
    float cp[3];
    if (!BdFindMyCharPos(mine, cp)) return "error buildgive: no own player-faction character with a readable position";
    static void* list[4096];
    int zones = 0, trunc = 0, matched = 0;
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    const bool nearest = std::strcmp(what, "nearest") == 0;
    void* best = 0; float bd = 1.0e30f;
    for (int i = 0; i < n; ++i)
    {
        float q[3];
        if (!BdCopyF((const char*)list[i] + kObjPos, q, 3)) continue;
        if (!nearest)
        {
            char mk[kKeyCap]; mk[0] = 0;
            if (ObjectPositionKey(list[i], mk, kKeyCap, "", 3, 1) == 0 || std::strstr(mk, what) == 0) continue;
            ++matched;
        }
        const float dx = q[0] - cp[0], dy = q[1] - cp[1], dz = q[2] - cp[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bd) { bd = d2; best = list[i]; }
    }
    char l[640];
    if (best == 0)
    {
        std::sprintf(l, "error buildgive: no loaded building %s '%.60s' (%d buildings, %d key matches, walkTruncated=%d)", nearest ? "for" : "whose key holds", what, n, matched, trunc);
        return std::string(l);
    }
    char k[kKeyCap]; k[0] = 0;
    if (ObjectPositionKey(best, k, kKeyCap, "", 3, 1) == 0) { k[0] = '?'; k[1] = 0; }
    ::Faction* before = (::Faction*)BdOwnerPod(best);
    const std::string beforeName = (before != 0) ? before->getName() : std::string("none");
    BdSelfScope selfScope;   /* the mod's own engine call */
    const int ok = BdSetFactionPod(best, (void*)f);
    void* after = BdOwnerPod(best);
    std::sprintf(l, "%s buildgive key=%s dist=%.0f player=%d owner %p '%.40s' -> %p '%.40s' (record id %s, recordSlot=%d)%s",
                 (ok && after == (void*)f) ? "ok" : "error", k, std::sqrt(bd), slot, (void*)before, beforeName.c_str(), after, f->getName().c_str(),
                 coopslot::StandInId(slot).c_str(), StandInRecordSlot(f), ok ? "" : " - setFaction FAULTED");
    DebugLog(std::string("[BUILD] P106 ") + l);
    return std::string(l);
}
std::string BuildListCommand()
{
    int n = 0, live = 0;
    for (std::map<std::string, BdReg>::iterator it = g_reg.begin(); it != g_reg.end(); ++it)
    {
        const BdReg& r = it->second;
        int lvv = 0;
        void* b = BdFindPiece(it->first, r, &lvv);   /* build1-e: furniture inside its host */
        float p = r.progress, nd = r.needed;
        int c = r.complete, d = r.dismantled, x = r.destroyed;
        const int ok = (b != 0) ? BdReadState(b, &p, &nd, &c, &d, &x) : 0;
        const std::string owner = (b != 0) ? BdFactionName(BdOwnerPod(b)) : r.owner;
        /* build1-c: the delivered mats, live when the piece is */
        float mt[coopbuild::kBuildMaxMats];
        int mTotal = -1;
        const int mn = (b != 0) ? BdReadMats(b, mt, (int)coopbuild::kBuildMaxMats, &mTotal) : -1;
        char ms[200];
        ms[0] = 0;
        if (mn < 0) std::strcpy(ms, "?");
        else
            for (int j = 0; j < mn && j < 16; ++j)
            {
                char one[32];   /* review-build1c L1: a peer's huge amount must not overflow the line buffers */
                _snprintf_s(one, sizeof(one), _TRUNCATE, j == 0 ? "%.1f" : ",%.1f", mt[j]);
                if (std::strlen(ms) + std::strlen(one) < sizeof(ms) - 1) std::strcat(ms, one);
            }
        if (ok != 0) ++live;
        ++n;
        char line[900];
        std::sprintf(line, "[BUILD] list key=%s sid=%.95s owner='%.60s' live=%d complete=%d progress=%.2f/%.2f dismantled=%d destroyed=%d removed=%d via=%s mats=[%s]"
                     " host=%.63s keySame=%d own=%d ownerSlot=%d handed=%d",
                     it->first.c_str(), r.sid.c_str(), owner.c_str(), ok, c, p, nd, d, x, r.removed, BdViaName(r.via), ms,
                     r.hostKey.empty() ? "-" : r.hostKey.c_str(), r.keySame, r.own, r.own != 0 ? MySlotForWire() : r.ownerSlot, r.handed);
        DebugLog(std::string(line));
    }
    for (size_t i = 0; i < g_pend.size(); ++i)
    {
        char pl[300];
        std::sprintf(pl, "[BUILD] list pending key=%s sid=%.80s tries=%d", g_pend[i].m.key.c_str(), g_pend[i].m.sid.c_str(), g_pend[i].tries);
        DebugLog(std::string(pl));
    }
    for (size_t i = 0; i < g_wait.size() && i < 20; ++i)
    {
        char pl[300];
        std::sprintf(pl, "[BUILD] list waiting (area not loaded) key=%s sid=%.80s", g_wait[i].m.key.c_str(), g_wait[i].m.sid.c_str());
        DebugLog(std::string(pl));
    }
    for (std::map<std::string, BdStatePend>::iterator it = g_pendState.begin(); it != g_pendState.end(); ++it)
    {
        char pl[300];
        std::sprintf(pl, "[BUILD] list held STATE key=%.63s %.2f/%.2f complete=%d mats=%d%s", it->first.c_str(), it->second.m.progress,
                     it->second.m.needed, (int)it->second.m.complete, (int)it->second.m.nMats, it->second.fromPlace ? " from=PLACE" : "");
        DebugLog(std::string(pl));
    }
    char s[240];
    std::sprintf(s, "buildlist %d entries, %d live, %u pending, %u waiting, %u held STATEs (registry: the placement hook, buildtest place, the link-up scan, copies, hand-overs and pp.build restores)",
                 n, live, (unsigned int)g_pend.size(), (unsigned int)g_wait.size(), (unsigned int)g_pendState.size());
    DebugLog(std::string("[BUILD] ") + s);
    return std::string("ok ") + s;
}

/* build1h H1 (design 2(c)), MAIN THREAD: the owner slot the registry recorded for the piece at this P7n key - the builder's key, or a
   copy's own created key - so both games class a box from the SAME record. An own row answers this game's slot; -1 = no live row. */
int BuildRecordedOwnerSlot(const char* key)
{
    if (key == 0 || key[0] == 0 || BdOnMain() == 0) return -1;
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(std::string(key));
    if (it == g_reg.end())
    {
        std::map<std::string, std::string>::const_iterator a = g_liveAlias.find(std::string(key));
        if (a == g_liveAlias.end()) return -1;
        it = g_reg.find(a->second);
        if (it == g_reg.end() || it->second.liveKey != key) return -1;
    }
    const BdReg& r = it->second;
    if (r.removed != 0 || r.dismantled != 0) return -1;
    if (r.own != 0) return MySlotForWire();
    return r.ownerSlot;
}

void ReportBuild()
{
    char b[3800];   /* build1-d: the remove / refund groups */
    std::sprintf(b, "[BUILD] REPORT build[placeSent,placeRecv,copyCreated,copyAdopted,copyPending,copyDup,copyKeyMismatch,copyFailed,copyOwnerFixed,pendingDropped]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " obs[placedSeen,progressSeen,dismantleSeen,destroySeen,testPlaced,testPlaceFailed,zoneFreshRefused,ownerFixed,queued,queueDropped]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld hooks[commit,create,progress,dismantle,destroy]=%d,%d,%d,%d,%d"
                 " commitSeen=%lld offMain[dismantle,destroy,commit]=%lld,%lld,%lld commitNewDropped=%lld"
                 " test[armed,progress,dismantle,noEntry,notLoaded,blocked,faulted]=%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " reg=%u regFull=%lld keyFailed=%lld lines=%lld suppressed=%lld"
                 " place[linkDown,furniture,noPos,unencodable]=%lld,%lld,%lld,%lld recvBad=%lld copyWait[noPeerDrains,notLoaded,blockedDrains]=%lld,%lld,%lld"
                 " copyZoneFresh=%lld loadBurstSkipped=%lld destroyNoChange=%lld progLines=%lld progSuppressed=%lld"
                 " state[stateSent,stateRecv,stateApplied,completeApplied,stateNoCopy,stateStale]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " stateMore[linkDown,noPlace,unencodable,waitArea,faulted,dropped,matsCountDiff,held,lines,suppressed]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld,%lld"
                 " fold1b[sidSubstituted,adoptAfterNull,offMap,waiting,waitResumed,forgotWorlds]=%lld,%lld,%lld,%u,%lld,%lld"
                 " remove[removeSent,removeRecv,copyRemoved,removeRetried,removeNoCopy,removePendingDropped,refundOnCopy,matsZeroed]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " removeMore[linkDown,noPlace,wall,already,waitArea,gaveUp,faulted,held,stateAfterRemove,stateWall,lines,suppressed]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld,%lld,%lld,%lld"
                 " refund[hook,seen,offMain,calledOnCopy]=%d,%lld,%lld,%lld"
                 " fold1d[destroyedIgnored,alreadyGone,rowsErased,recentRemoved]=%lld,%lld,%lld,%u"
                 " build1e[hostPending,hostResolved,hostFailed,hostKeyFailed,placeFurniture,removeUnclean,testInto,testIntoFailed]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_placeSent, g_placeRecv, g_copyCreated, g_copyAdopted, (long long)(g_pend.size() + g_wait.size()), g_copyDup, g_copyKeyMismatch,
                 g_copyFailed, g_copyOwnerFixed, g_pendingDropped,
                 g_placedSeen, g_progressSeen, g_dismantleSeen, g_destroySeen, g_testPlaced, g_testPlaceFailed, g_zoneFreshRefused,
                 g_ownerFixed, g_queued, g_queueDropped, g_hkCommit, g_hkCreate, g_hkProgress, g_hkDismantle, g_hkDestroy,
                 g_commitSeen, g_dismantleOffMain, g_destroyOffMain, g_commitOffMain, g_commitNewDropped,
                 g_testArmed, g_testProgress, g_testDismantle, g_testNoEntry, g_testNotLoaded, g_testBlocked, g_testFaulted,
                 (unsigned int)g_reg.size(), g_regFull, g_keyFailed, g_lines, g_linesSuppressed,
                 g_placeLinkDown, g_placeFurniture, g_placeNoPos, g_placeUnencodable, g_placeRecvBad, g_copyWaitNoPeer, g_copyWaitNotLoaded,
                 g_copyBlocked, g_copyZoneFresh, g_loadBurstSkipped, g_destroyNoChange, g_progLines, g_progSuppressed,
                 g_stateSent, g_stateRecv, g_stateApplied, g_completeApplied, g_stateNoCopy, g_stateStale,
                 g_stateLinkDown, g_stateNoPlace, g_stateUnencodable, g_stateWaitArea, g_stateFaulted, g_stateDropped, g_matsCountDiff,
                 (unsigned int)g_pendState.size(), g_stateLines, g_stateSuppressed,
                 g_sidSubstituted, g_copyAdoptAfterNull, g_copyOffMap, (unsigned int)g_wait.size(), g_waitResumed, g_forgotWorlds,
                 g_removeSent, g_removeRecv, g_copyRemoved, g_removeRetried, g_removeNoCopy, g_removePendingDropped, g_refundOnCopy, g_matsZeroed,
                 g_removeLinkDown, g_removeNoPlace, g_removeWall, g_removeAlready, g_removeWaitArea, g_removeGaveUp, g_removeFaulted,
                 (unsigned int)g_pendRemove.size(), g_stateAfterRemove, g_stateWall, g_removeLines, g_removeSuppressed,
                 g_hkRefund, g_refundSeen, g_refundOffMain, g_refundCalledOnCopy,
                 g_removeDestroyedIgnored, g_removeAlreadyGone, g_removedRowsErased, (unsigned int)g_recentRemoved.size(),
                 g_hostPending, g_hostResolved, g_hostFailed, g_hostKeyFailed, g_placeFurniture, g_removeUnclean, g_testInto, g_testIntoFailed);
    char t[1200];   /* build1-f */
    _snprintf_s(t, sizeof(t), _TRUNCATE, " roster[rosterSent,rosterPending,rosterStateSent,rosterNotLive,rosterNoPos,rosterLinkDown,rosterGone,rosterNotMine,rosterUnencodable,rosterRounds,owed,active]"
                 "=%lld,%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d"
                 " scan[scanRuns,scanFound,scanRegistered,scanFurnitureSkipped,scanInteriorSkipped,scanDoorSkipped,scanKnown,scanNoKey,scanNoPos,scanDismantled,scanTruncated]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld rosterReq[sent,recv,linkDown,done]=%lld,%lld,%lld,%d"
                 " rosterLines[lines,suppressed]=%lld,%lld scanWalkMoved=%lld"
                 " zoneScan[zones,found,registered,known,furn,interior,skipped,walkMoved,refused,truncated,lines,suppressed,gaveUp,listFull,knownAlias,pieceLines,pieceSuppressed]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld scanKnownAlias=%lld"
                 " house1[houseHanded,houseOwnerUnresolved,houseTown,handedRecv,progressOnHandedCopy,handedWaitSlot]=%lld,%lld,%lld,%lld,%lld,%lld",
                 g_rosterSent, (unsigned int)(g_rosterQ.size() - g_rosterAt), g_rosterStateSent, g_rosterNotLive, g_rosterNoPos, g_rosterLinkDown,
                 g_rosterGone, g_rosterNotMine, g_rosterUnencodable, g_rosterRounds, g_rosterOwed, g_rosterActive,
                 g_scanRuns, g_scanFound, g_scanRegistered, g_scanFurnitureSkipped, g_scanInteriorSkipped, g_scanDoorSkipped, g_scanKnown,
                 g_scanNoKey, g_scanNoPos, g_scanDismantled, g_scanTruncated, g_rosterReqSent, g_rosterReqRecv, g_rosterReqLinkDown,
                 g_rosterReqDone, g_rosterLines, g_rosterSuppressed, g_scanWalkMoved,
                 g_zsZones, g_zsFound, g_zsRegistered, g_zsKnown, g_zsFurn, g_zsInner, g_zsSkipped, g_zsWalkMoved, g_zsRefused, g_zsTruncated, g_zsLines, g_zsSuppressed,
                 g_zsGaveUp, g_zsListFull, g_zsKnownAlias, g_zsPieceLines, g_zsPieceSuppressed, g_scanKnownAlias,
                 g_houseHanded, g_houseOwnerUnresolved, g_houseTown, g_handedRecv, g_progressOnHandedCopy, g_handedWaitSlot);
    DebugLog(std::string(b) + t);
    {
        char pl[200];
        _snprintf_s(pl, sizeof(pl), _TRUNCATE, "[BUILD] REPORT placeFromLive[converted,noGround,retried]=%lld,%lld,%lld",
                    g_placeFromLive, g_placeFromLiveNoGround, g_placeFromLiveRetried);
        DebugLog(std::string(pl));
    }
    char h[900];   /* house1b (review-house1) */
    _snprintf_s(h, sizeof(h), _TRUNCATE, "[BUILD] REPORT house1b[orderGate,orderSkipped,orderKept,orderBlocked]=%d,%lld,%lld,%lld"
                " owed[made,resent,resendLinkDown,ackSent,ackRecv,ackNoOwed,loaded,saveFailed,removed,held,unacked]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%u"
                " handoff[standInFromRecord,handedNoStandIn,dupRecorded,liveAliasEvents,preflightFailed]=%lld,%lld,%lld,%lld,%lld"
                " house2[jobGate,jobBlocked,jobKept,lateRevivedRefused,owedSaveRefused,owedLoadUnread]=%d,%lld,%lld,%lld,%lld,%lld",
                g_hkOrder, g_orderSkipped, g_orderKept, g_orderBlocked,
                g_owedMade, g_owedResent, g_owedResendLinkDown, g_ackSent, g_ackRecv, g_ackNoOwed, g_owedLoaded, g_owedSaveFailed, g_owedRemoved,
                (unsigned int)g_owed.size(), BdOwedUnacked(),
                g_standInFromRecord, g_handedNoStandIn, g_dupRecorded, g_liveAliasEvents, g_handPreflightFailed,
                g_hkJob, g_jobBlocked, g_jobKept, g_lateRevivedRefused, g_owedSaveRefused, g_owedLoadUnread);
    DebugLog(std::string(h));
    char p15[700];   /* P15 */
    _snprintf_s(p15, sizeof(p15), _TRUNCATE, "[BUILD] REPORT destroyed[destroySent,repairSent,destroyApplied,repairApplied,destroyAlready,destroyPosted,destroyFaulted,destroyWaitRemove,"
                "destroySeen,wholeSeen,copyDestroyReverted,copyDestroyWait,repairOrderHelp,repairRefused,repairHelpReset,testDestroy]"
                "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                g_destroySent, g_repairSent, g_destroyApplied, g_repairApplied, g_destroyAlready, g_destroyPosted, g_destroyFaulted, g_destroyWaitRemove,
                g_destroySeen, g_wholeSeen, g_copyDestroyReverted, g_copyDestroyWait, g_repairOrderHelp, g_repairRefused, g_repairHelpReset, g_testDestroy);
    DebugLog(std::string(p15));
    char p15f[400];   /* P15 fold 1 */
    _snprintf_s(p15f, sizeof(p15f), _TRUNCATE, "[BUILD] REPORT destroyedFold1[testAlreadyRuin,destroyDeferRetried,destroyDeferGaveUp,repairDeferred,copyWallRuinKept]=%lld,%lld,%lld,%lld,%lld"
                " (destroyPosted counts every deferred ruin try; destroyFaulted no longer counts a deferred repair)",
                g_testAlreadyRuin, g_destroyDeferRetried, g_destroyDeferGaveUp, g_repairDeferred, g_copyWallRuinKept);
    DebugLog(std::string(p15f));
    char hf[700];   /* help1 fold */
    _snprintf_s(hf, sizeof(hf), _TRUNCATE, "[BUILD] REPORT helpFold[held,wallRefused,goneSent,goneRecv,goneItems,recvTwice,ackLanded,ackStuck,ackAwait,waitTail,neededHealed,freqLines,recState,recRows,saveDirty,resynced,superseded,unencodable,replaced,floorDropped,wallRefusedOff,goneRowsKept,goneRowsRead,goneRowUnkept,ownGone]"
                "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld,%lld,%lld,%d,%u,%d,%lld,%lld,%lld,%lld,%lld,%ld,%lld,%lld,%lld,%u",
                g_helpHeld, g_helpWallRefused, g_helpGoneSent, g_helpGoneRecv, g_helpGoneItems, g_helpRecvTwice, g_helpAckLanded, g_helpAckStuck,
                (unsigned int)g_helpAckAwait.size(), g_helpWaitTail, g_neededHealed, g_helpSendLines, g_helpRecState, (unsigned int)g_helpRec.size(), g_helpSaveDirty,
                g_helpResynced, g_helpSuperseded, g_helpUnencodable, g_helpReplaced, g_helpFloorDropped, (long)::InterlockedCompareExchange(&g_helpWallRefusedOff, 0, 0),
                g_goneRowsKept, g_goneRowsRead, g_goneRowUnkept, (unsigned int)g_ownGone.size());
    DebugLog(std::string(hf));

    char hp[1200];   /* help1 (owner 172) */
    _snprintf_s(hp, sizeof(hp), _TRUNCATE, "[BUILD] REPORT help[workSent,workOwed,workRecv,applied,dup,dropRemoved,dropComplete,matsClamped,copyCapped,orderOnCopy,predicted,goneOldNonce,goneNoRecord,goneRecFull,goneWaitLand,movedOld,oldRows,goneRefused]"
                "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld"
                " helpMore[resent,confirmed,unacked,rows,waitArea,recvDropped,held,refundItems,refundFailed,faulted,saveFailed,loadUnread,testMats,lines,suppressed]"
                "=%lld,%lld,%u,%u,%lld,%lld,%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                " helpFold4[nonceRestored,nonceHeld,goneUnproven,copyCollide,goneCarried,oldProbe,ackKeptUnknown]=%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                g_helpWorkSent, g_helpWorkOwed, g_helpWorkRecv, g_helpApplied, g_helpDup, g_helpDropRemoved, g_helpDropComplete, g_helpMatsClamped,
                g_helpCopyCapped, g_orderOnCopy, g_helpPredicted,
                g_helpGoneOldNonce, g_helpGoneNoRecord, g_goneRecFull, g_goneWaitLand, g_helpMovedOld, (unsigned int)g_helpOld.size(), g_helpGoneRefused,   /* help1 fold 3 */
                g_helpResent, g_helpConfirmed, BdHelpUnacked(), (unsigned int)g_help.size(), g_helpWaitArea, g_helpRecvDropped, (unsigned int)g_helpIn.size(),
                g_helpRefundItems, g_helpRefundFailed, g_helpFaulted, g_helpSaveFailed, g_helpLoadUnread, g_testMats, g_helpLines, g_helpSuppressed,
                g_helpNonceRestored, g_helpNonceHeld, g_helpGoneUnproven, g_bdCopyCollide, g_helpGoneCarried, g_helpOldProbe, g_helpAckKeptUnknown);   /* help1 fold 4 */
    DebugLog(std::string(hp));
    char p18[700];   /* P18 */
    _snprintf_s(p18, sizeof(p18), _TRUNCATE, "[BUILD] REPORT p18[hook,seen,queued,offMain,modWrite,nested,notMine,wasMine,noKey,dup,queueFull,registered,notTaken,known,copyRow,notMineNow,gaveUp,walkMoved,applied,applyFaulted,waiting,lines,suppressed]"
                "=%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld,%lld",
                g_hkOwner, g_ocSeen, g_ocQueued, (long long)::InterlockedCompareExchange64(&g_ocOffMain, 0, 0), g_ocModWrite, g_ocNested, g_ocNotMine, g_ocWasMine,
                g_ocNoKey, g_ocDup, g_ocQueueFull, g_ocRegistered, g_ocNotTaken, g_ocKnown, g_ocCopyRow, g_ocNotMineNow, g_ocGaveUp, g_ocWalkMoved,
                g_ocApplied, g_ocApplyFaulted, (unsigned int)g_ocQ.size(), g_ocLines, g_ocSuppressed);
    DebugLog(std::string(p18));
    char p18f[600];   /* P18 fold 1 */
    _snprintf_s(p18f, sizeof(p18f), _TRUNCATE, "[BUILD] REPORT p18f1[depthReset,skippedNotBuilding,adoptOther,applySkip,forSaleHook,notForSale,buyLever,buyRefused,fxTownOut,fxTownIn,fxDoorsOpened,fxInterior,fxResidents,fxNewHomes,fxResSkipped,fxFaulted,fxStepsSkipped]"
                "=%lld,%lld,%lld,%lld,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                g_ocDepthReset, g_ocSkippedNotBuilding, g_ocAdoptOther, g_ocApplySkip, g_hkForSale, (long long)::InterlockedCompareExchange64(&g_ocNotForSale, 0, 0),
                g_ocBuyLever, g_ocBuyRefused, g_ocFxTownOut, g_ocFxTownIn, g_ocFxDoorsOpened, g_ocFxInterior, g_ocFxResidents, g_ocFxNewHomes,
                g_ocFxResSkipped, g_ocFxFaulted, g_ocFxStepsSkipped);
    DebugLog(std::string(p18f));
    char p18r[300];   /* P18 fold 2 */
    _snprintf_s(p18r, sizeof(p18r), _TRUNCATE, "[BUILD] REPORT p18f2[rebuildHook,calls,buildingStates,held,overflow,faults]=%d,%lld,%lld,%lld,%lld,%lld",
                g_hkRebuild, (long long)::InterlockedCompareExchange64(&g_rbCalls, 0, 0), (long long)::InterlockedCompareExchange64(&g_rbStates, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_rbHeld, 0, 0), (long long)::InterlockedCompareExchange64(&g_rbOverflow, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_rbFaults, 0, 0));
    DebugLog(std::string(p18r));
    ReportFarm();   /* par16 */
}

/* MAIN THREAD - another game's PLACE or STATE for this piece still waits here (a copy not made yet, its area not loaded, or a state -
   an owner change such as a purchase - not applied yet): 1 yes, 0 no, -1 the piece's key is unreadable. The town refill
   (towngen.cpp) leaves such a building alone. */
int BuildRowPendingFor(void* b)
{
    char k[kKeyCap];
    if (b == 0 || ObjectPositionKey(b, k, kKeyCap, "", 3, 0) == 0) return -1;
    const std::string key(k);
    if (g_pendState.find(key) != g_pendState.end()) return 1;
    for (size_t i = 0; i < g_pend.size(); ++i) if (g_pend[i].m.key == key) return 1;
    for (size_t i = 0; i < g_wait.size(); ++i) if (g_wait[i].m.key == key) return 1;
    return 0;
}

} // namespace coop
